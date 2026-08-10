/*
 * link.c  (STM32 side)
 *
 * App layer: the glue between Com (transport) + Frame (framing) and
 * FreeRTOS. Task/queue/dispatch logic lives here.
 *
 *  Created on: Jul 29, 2026
 *      Author: Muhmmad Salman
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "log.h"
#include "link.h"
#include "com.h"
#include "frame.h"
#include "tasks_config.h"

/* Payload structs for specific commands — same wire layout as ESP32 side */
typedef struct __attribute__((packed)) {
    uint8_t  connected;
    int8_t   rssi;
    char     ip_str[16];
} link_wifi_status_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t unix_timestamp;
    uint8_t  synced;
} link_time_payload_t;

link_Stats_t  link_stats;

/* Stored time info from ESP32 */
static link_TimeInfo_t  s_time_info;
static SemaphoreHandle_t s_time_mutex;

static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex;
static volatile bool     s_need_recover   = false;

/* Sequence counter for outgoing requests */
static uint16_t s_req_seq = 0;

/*
   Com event callback — ISR CONTEXT.
   Minimal by design: just set a flag. The rx task polls every 10ms.
*/
static void com_event_from_isr(Com_Event_t event)
{
    if (event == COM_EVENT_ERROR)
    {
        s_need_recover = true;
    }
    /* No notification needed — link_rx_task polls every 10 ms */
}

static void on_packet_received(const frame_Packet_t *packet)
{
    if (xQueueSend(s_rx_queue, packet, 0) != pdTRUE)
    {
        link_stats.rx_queue_full++;
    }
}

/*
   link_rx_task — priority 5.
   Polls frame_Poll() every 10ms; on error, recovers the transport.
*/
static void link_rx_task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        /* 1. Handle deferred error recovery first */
        if (s_need_recover)
        {
            s_need_recover = false;
            Com_Recover();
            link_stats.rx_uart_errors++;
        }

        /* 2. Drain whatever the DMA has collected */
        frame_Poll();

        /* 3. Pace the loop — matches ESP32's 10 ms uart_read_bytes timeout.
         *     Without this, the task CPU-spins at 100% when idle. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void link_app_task(void *pvParameters)
{
    (void)pvParameters;
    frame_Packet_t packet;

    for (;;)
    {
        if (xQueueReceive(s_rx_queue, &packet, portMAX_DELAY) == pdTRUE)
        {
            switch (packet.cmd)
            {
            case CMD_HEARTBEAT:
                /* ESP32 keep-alive ping -> reply immediately, same seq, FLAG_ACK */
                link_Send(CMD_HEARTBEAT_ACK, packet.seq, FRAME_FLAG_ACK, NULL, 0U);
                break;

            case CMD_WIFI_STATUS:
                if (packet.payload_length >= sizeof(link_wifi_status_payload_t))
                {
                    link_wifi_status_payload_t status;
                    memcpy(&status, packet.payload, sizeof(status));

                    Log_Printf(LOG_LEVEL_INFO, "LINK",
                               "WiFi status: connected=%d rssi=%d ip=%s",
                               status.connected, status.rssi, status.ip_str);
                }
                break;

            case CMD_TIME_SYNC:
                if (packet.payload_length >= sizeof(link_time_payload_t))
                {
                    link_time_payload_t time_payload;
                    memcpy(&time_payload, packet.payload, sizeof(time_payload));

                    /* Store the time info (thread-safe) */
                    if (s_time_mutex != NULL)
                    {
                        if (xSemaphoreTake(s_time_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
                        {
                            s_time_info.unix_timestamp = time_payload.unix_timestamp;
                            s_time_info.synced         = (time_payload.synced != 0);
                            s_time_info.valid          = true;
                            xSemaphoreGive(s_time_mutex);
                        }
                    }

                    Log_Printf(LOG_LEVEL_INFO, "LINK",
                               "Time sync: ts=%lu synced=%d",
                               (unsigned long)time_payload.unix_timestamp,
                               time_payload.synced);


                }
                break;

            case CMD_TIME_REQ:
                /* ESP32 should never send this to us, but handle gracefully */
                Log_Printf(LOG_LEVEL_WARN, "LINK", "Unexpected CMD_TIME_REQ from ESP32");
                break;

            case CMD_WIFI_STATUS_REQ:
                /* ESP32 should never send this to us, but handle gracefully */
                Log_Printf(LOG_LEVEL_WARN, "LINK", "Unexpected CMD_WIFI_STATUS_REQ from ESP32");
                break;

            case CMD_SLOT_INFO_REQ:
                /* TODO: Reply with OTA slot info */
                break;

            case CMD_OTA_START:
                /* TODO: Handle OTA start */
                break;

            case CMD_OTA_DATA:
                /* TODO: Handle OTA data chunk */
                break;

            case CMD_OTA_END:
                /* TODO: Handle OTA end */
                break;

            default:
                /* unknown command — silently ignore */
                break;
            }
        }
    }
}

frame_Status_t link_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_length)
{
    frame_Status_t status;

    if (s_send_mutex != NULL)
    {
        xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    }

    status = frame_Send(cmd, seq, flags, payload, payload_length);

    if (status == FRAME_OK)
    {
        link_stats.tx_packets_sent++;
    }
    else
    {
        link_stats.tx_send_failures++;
        Log_Printf(LOG_LEVEL_ERROR, "LINK", "link_Send failed cmd=0x%02X seq=%d", cmd, seq);
    }

    if (s_send_mutex != NULL)
    {
        xSemaphoreGive(s_send_mutex);
    }

    return status;
}

void link_RequestWifiStatus(uint16_t seq)
{
    link_Send(CMD_WIFI_STATUS_REQ, seq, 0, NULL, 0);
    Log_Printf(LOG_LEVEL_INFO, "LINK", "Sent WIFI_STATUS_REQ seq=%d", seq);
}

void link_RequestTime(uint16_t seq)
{
    link_Send(CMD_TIME_REQ, seq, 0, NULL, 0);
    Log_Printf(LOG_LEVEL_INFO, "LINK", "Sent TIME_REQ seq=%d", seq);
}

bool link_GetTimeInfo(link_TimeInfo_t *out_info)
{
    if (out_info == NULL)
    {
        return false;
    }

    bool valid = false;

    if (s_time_mutex != NULL)
    {
        if (xSemaphoreTake(s_time_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            *out_info = s_time_info;
            valid = s_time_info.valid;
            xSemaphoreGive(s_time_mutex);
        }
    }
    else
    {
        /* Fallback if mutex not initialized (shouldn't happen) */
        *out_info = s_time_info;
        valid = s_time_info.valid;
    }

    return valid;
}

void link_Init(void)
{
    /* 1. queue/mutex before anything that could use them */
    s_rx_queue   = xQueueCreate(LINK_RX_QUEUE_DEPTH, sizeof(frame_Packet_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_time_mutex = xSemaphoreCreateMutex();
    memset(&link_stats, 0, sizeof(link_stats));

    /* Initialize time info as invalid */
    memset(&s_time_info, 0, sizeof(s_time_info));
    s_time_info.valid = false;

    /* 2. bring up the layers below, lowest first */
    Com_Init();
    frame_Init();

    /* 3. wire the layers together via callback registration */
    Com_RegisterEventCallback(com_event_from_isr);
    frame_RegisterPacketCallback(on_packet_received);

    /* 4. tasks last — they start running immediately */
    xTaskCreate(link_rx_task, "link_rx", LINK_RX_TASK_STACK, NULL,
                LINK_RX_TASK_PRIORITY, NULL);
    xTaskCreate(link_app_task, "link_app", LINK_APP_TASK_STACK, NULL,
                LINK_APP_TASK_PRIORITY, NULL);
}
