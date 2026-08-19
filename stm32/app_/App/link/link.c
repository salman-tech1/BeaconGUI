/*
 * link.c
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

#include "iwdg.h"
#include "version.h"
#include "log.h"
#include "link.h"
#include "com.h"
#include "frame.h"
#include "ota.h"
#include "tasks_config.h"
#include "system_info.h"

/* Payload structs for specific commands */
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

static link_TimeInfo_t  s_time_info;
static SemaphoreHandle_t s_time_mutex;

static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex;
static volatile bool     s_need_recover   = false;

// sync on when the stm32 reset but esp32 not
static volatile bool     s_esp32_alive    = false;  /* Set true on first heartbeat */
static volatile bool     s_initial_sync_done = false;  /* Set true after first status request */
static uint16_t         s_sync_seq       = 0U;

static uint32_t s_last_ota_timeout_check = 0U;

static void com_event_from_isr(Com_Event_t event)
{
    if (event == COM_EVENT_ERROR)
    {
        s_need_recover = true;
    }
}

static void on_packet_received(const frame_Packet_t *packet)
{
    if (xQueueSend(s_rx_queue, packet, 0) != pdTRUE)
    {
        link_stats.rx_queue_full++;
    }
}

/*
 * link_rx_task — priority 5.
 * Polls frame_Poll() every 10ms; on error, recovers the transport.
 */
static void link_rx_task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {


        HAL_IWDG_Refresh(&hiwdg);
        if (s_need_recover)
        {
            s_need_recover = false;
            Com_Recover();
            link_stats.rx_uart_errors++;
        }

        frame_Poll();

        /* Throttled to once/second — no need to check every 10ms, and this
                * keeps ota_receiver_check_timeout()'s internal HAL_GetTick() diff
                * cheap to compute here regardless. */
               uint32_t now = HAL_GetTick();
               if ((now - s_last_ota_timeout_check) >= 1000U)
               {
                   s_last_ota_timeout_check = now;
                   ota_receiver_check_timeout();
               }


        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*
 * Send initial sync requests to ESP32.
 * Called once after we confirm ESP32 is alive (first heartbeat).
 */
static void request_initial_sync(void)
{
    if (s_initial_sync_done) return;
    s_initial_sync_done = true;

    Log_Printf(LOG_LEVEL_INFO, "LINK", "ESP32 alive — requesting initial sync...");

    /* Request WiFi status */
    link_Send(CMD_WIFI_STATUS_REQ, s_sync_seq++, 0, NULL, 0U);

    /* Request current time */
    link_Send(CMD_TIME_REQ, s_sync_seq++, 0, NULL, 0U);
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
                /* ─ On first heartbeat, request full state sync ── */
                if (!s_esp32_alive)
                {
                    s_esp32_alive = true;
                    /*
                     * Delay slightly to let the heartbeat ACK go out first.
                     * This prevents the two requests from colliding with the ACK
                     * on a slow UART. 50ms is safe at 115200 baud.
                     */
                    vTaskDelay(pdMS_TO_TICKS(50));
                    request_initial_sync();
                }

                /* Reply immediately, same seq, FLAG_ACK */
                link_Send(CMD_HEARTBEAT_ACK, packet.seq, FRAME_FLAG_ACK, NULL, 0U);
                break;

            case CMD_WIFI_STATUS:
                if (packet.payload_length >= sizeof(link_wifi_status_payload_t))
                {
                    link_wifi_status_payload_t status;
                    memcpy(&status, packet.payload, sizeof(status));

                    sysinfo_set_wifi_status(
                        (status.connected != 0),
                        status.rssi,
                        status.ip_str
                    );

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

                    bool is_synced = (time_payload.synced != 0);

                    sysinfo_set_time(time_payload.unix_timestamp, is_synced);

                    if (s_time_mutex != NULL)
                    {
                        if (xSemaphoreTake(s_time_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
                        {
                            s_time_info.unix_timestamp = time_payload.unix_timestamp;
                            s_time_info.synced         = is_synced;
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
                Log_Printf(LOG_LEVEL_WARN, "LINK", "Unexpected CMD_TIME_REQ from ESP32");
                break;

            case CMD_WIFI_STATUS_REQ:
                Log_Printf(LOG_LEVEL_WARN, "LINK", "Unexpected CMD_WIFI_STATUS_REQ from ESP32");
                break;

            case CMD_SLOT_INFO_REQ:
            {
                /* ESP32 asking which slot to target for OTA.
                   Must reply immediately — never block. */
                uint8_t resp[4];
                resp[0] = ota_receiver_get_target_slot();
                resp[1] = FW_VERSION_MAJOR;  /* from version.h */
                resp[2] = FW_VERSION_MINOR;
                resp[3] = FW_VERSION_PATCH;
                link_Send(CMD_SLOT_INFO_RESP, packet.seq, 0, resp, sizeof(resp));
                break;
            }

            case CMD_OTA_START:
            {
                /* Begin OTA — validate size + erase target slot.
                 * NOTE: Blocks link_app_task for ~14 seconds during erase.
                 * For production, move erase to a separate task. */
                ota_receiver_on_start(packet.payload, packet.payload_length, packet.seq);
                break;
            }

            case CMD_OTA_DATA:
            {
                /* OTA firmware chunk — write to flash + ACK.
                 * NOTE: Blocks link_app_task for the duration of the transfer. */
                ota_receiver_on_data(packet.payload, packet.payload_length, packet.seq);
                break;
            }

            case CMD_OTA_END:
            {
                /* OTA complete — verify CRC + write metadata + reboot.
                 * NOTE: Blocks link_app_task during CRC check (896KB bitwise CRC is ~100ms on 400MHz).
                 * For production, compute CRC in a separate task or use DMA2D. */
                ota_receiver_on_end(packet.payload, packet.payload_length, packet.seq);
                break;
            }

            case CMD_OTA_ACK:
                /* Should only come from our ota_receiver, not from ESP32.
                   If it does, log and ignore. */
                Log_Printf(LOG_LEVEL_WARN, "LINK", "Unexpected CMD_OTA_ACK from ESP32");
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
        *out_info = s_time_info;
        valid = s_time_info.valid;
    }

    return valid;
}


void link_Init(void)
{
    /*  Reset sync state — fresh start every boot */
    s_esp32_alive     = false;
    s_initial_sync_done = false;
    s_sync_seq        = 0U;

    /*  queue/mutex before anything that could use them */
    s_rx_queue   = xQueueCreate(LINK_RX_QUEUE_DEPTH, sizeof(frame_Packet_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_time_mutex = xSemaphoreCreateMutex();
    memset(&link_stats, 0, sizeof(link_stats));

    /* Initialize time info as invalid */
    memset(&s_time_info, 0, sizeof(s_time_info));
    s_time_info.valid = false;

    /*  bring up the layers below, lowest first */
    Com_Init();
    frame_Init();

    /*  wire the layers together via callback registration */
    Com_RegisterEventCallback(com_event_from_isr);
    frame_RegisterPacketCallback(on_packet_received);

    /*  tasks last — they start running immediately */
    xTaskCreate(link_rx_task, "link_rx", LINK_RX_TASK_STACK, NULL,
                LINK_RX_TASK_PRIORITY, NULL);
    xTaskCreate(link_app_task, "link_app", LINK_APP_TASK_STACK, NULL,
                LINK_APP_TASK_PRIORITY, NULL);
}

void link_CheckOtaTimeout(void)
{
    ota_receiver_check_timeout();
}
