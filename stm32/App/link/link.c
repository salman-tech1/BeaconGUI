/*
 * link.c
 *
 *  App layer: the glue between Com (transport) + Frame (framing) and
 *  FreeRTOS. Task/queue/dispatch logic lives here; the DMA/HAL half
 *  lives in BSP/com.c, and the framing/CRC half lives in
 *  Middlewares/frame/frame.c.
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

#include "link.h"
#include "com.h"
#include "frame.h"
#include "tasks_config.h"




//#include "version.h"
//#include "ota_reciever.h"




/* Payload structs for specific commands — same wire layout as before */
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

static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex;
static TaskHandle_t      s_rx_task_handle = NULL;
static volatile bool     s_need_recover   = false;

/* ============================================================
   Com event callback — ISR CONTEXT.
   Minimal by design, same rule as Touch_EXTI_Callback: just flag/notify.
   This is the one place FreeRTOS-aware code reaches down into an
   ISR-driven event from the RTOS-agnostic BSP layer.
   ============================================================ */
static void com_event_from_isr(Com_Event_t event)
{
    BaseType_t woken = pdFALSE;

    if (event == COM_EVENT_ERROR) {
        s_need_recover = true;
    }

    if (s_rx_task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_rx_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* ============================================================
   Link packet callback — TASK CONTEXT (called from inside Link_Poll(),
   which link_rx_task calls). Safe to touch the FreeRTOS queue here.
   ============================================================ */
static void on_packet_received(const frame_Packet_t *packet)
{
    if (xQueueSend(s_rx_queue, packet, 0) != pdTRUE) {
    	link_stats.rx_queue_full++;
    }
}

/* ============================================================
   link_rx_task — priority 5.
   Blocks on notification from Com; on error, recovers the transport;
   otherwise polls Link, which drains Com and feeds the parser.
   ============================================================ */
static void link_rx_task(void *pvParameters)
{
    (void)pvParameters;
    s_rx_task_handle = xTaskGetCurrentTaskHandle();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (s_need_recover) {
            s_need_recover = false;
            Com_Recover();
            link_stats.rx_uart_errors++;
            continue;
        }

        frame_Poll();
    }
}

/* ============================================================
   link_app_task — priority 4.
   Same command dispatch as the old uart_app_task. CRITICAL: never
   block inside a case handler — copy data and return, or the queue
   fills and packets get dropped silently (rx_queue_full).
   ============================================================ */
static void link_app_task(void *pvParameters)
{
    (void)pvParameters;
    frame_Packet_t packet;

    for (;;) {
        if (xQueueReceive(s_rx_queue, &packet, portMAX_DELAY) == pdTRUE) {
            switch (packet.cmd) {

            case CMD_HEARTBEAT:
                /* ESP32 keep-alive ping — reply immediately, same seq,
                   FLAG_ACK set, no payload. */
            //    link_Send(CMD_HEARTBEAT_ACK, packet.seq, FRAME_FLAG_ACK, NULL, 0U);
                break;

            case CMD_WIFI_STATUS:
                //if (packet.payload_length >= sizeof(link_wifi_status_payload_t)) {
            //        link_wifi_status_payload_t status;
               //     memcpy(&status, packet.payload, sizeof(status));
              //      printf("[WIFI] connected=%d rssi=%d ip=%s\r\n",
              //             status.connected, status.rssi, status.ip_str);
                    /* draw this on LCD later */
              //  }
                break;

            case CMD_TIME_SYNC:
             //   if (packet.payload_length >= sizeof(link_time_payload_t)) {
              //      link_time_payload_t t;
              //      memcpy(&t, packet.payload, sizeof(t));
              //      if (t.synced) {
              //          printf("[TIME] Unix=%lu (synced)\r\n", (unsigned long)t.unix_timestamp);
              //          /* write to STM32 RTC here when ready */
                //    } else {
                //        printf("[TIME] Unix=%lu (not synced yet)\r\n", (unsigned long)t.unix_timestamp);
               //     }
            //    }
                break;

            case CMD_SLOT_INFO_REQ:
            //{
                /* ESP32 asking which slot to target for its next OTA —
                   must reply immediately, same no-blocking rule applies. */
             //   uint8_t resp[4];
             //   resp[0] = ota_receiver_get_target_slot();
             //   resp[1] = (uint8_t)FW_VERSION_MAJOR;
             //   resp[2] = (uint8_t)FW_VERSION_MINOR;
             //   resp[3] = (uint8_t)FW_VERSION_PATCH;
             //   link_Send(CMD_SLOT_INFO_RESP, packet.seq, 0, resp, sizeof(resp));
                break;
        //    }

            case CMD_OTA_START:
              //  ota_receiver_on_start(packet.payload, packet.payload_length, packet.seq);
                break;

            case CMD_OTA_DATA:
             //   ota_receiver_on_data(packet.payload, packet.payload_length, packet.seq);
                break;

            case CMD_OTA_END:
             //   ota_receiver_on_end(packet.payload, packet.payload_length, packet.seq);
                break;

            default:
                /* unknown command — silently ignore */
                break;
            }
        }
    }
}

/* ============================================================
   PUBLIC API
   ============================================================ */

frame_Status_t link_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                                const uint8_t *payload, uint16_t payload_length)
{
	frame_Status_t status;

    if (s_send_mutex != NULL) {
        xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    }

    status = frame_Send(cmd, seq, flags, payload, payload_length);

    if (s_send_mutex != NULL) {
        xSemaphoreGive(s_send_mutex);
    }

    return status;
}

void link_Init(void)
{
    /* 1. queue/mutex before anything that could use them */
    s_rx_queue   = xQueueCreate(LINK_RX_QUEUE_DEPTH, sizeof(frame_Packet_t));
    s_send_mutex = xSemaphoreCreateMutex();
    memset(&link_stats, 0, sizeof(link_stats));

    /* 2. bring up the layers below, lowest first */
    Com_Init();
    frame_Init();

    /* 3. wire the layers together via callback registration —
          this is the only place any of this gets connected */
    Com_RegisterEventCallback(com_event_from_isr);
    frame_RegisterPacketCallback(on_packet_received);

    /* 4. tasks last — they start running immediately */
    xTaskCreate(link_rx_task, "link_rx", LINK_RX_TASK_STACK, NULL,
                LINK_RX_TASK_PRIORITY, NULL);
    xTaskCreate(link_app_task, "link_app", LINK_APP_TASK_STACK, NULL,
                LINK_APP_TASK_PRIORITY, NULL);
}
