/*
 * link.c  (ESP32 side)
 *
 * App layer: the glue between Com (transport) + Frame (framing) and
 * FreeRTOS. Task/queue/dispatch logic lives here.
 *
 *  Author: Muhmmad Salman
 */

#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "system_info.h"

#include "Tasks_common.h"
#include "link.h"
#include "com.h"
#include "frame.h"

static const char *TAG = "LINK";

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
static volatile bool     s_need_recover = false;

static void com_event_handler(Com_Event_t event)
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

static void link_rx_task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        frame_Poll();

        if (s_need_recover)
        {
            s_need_recover = false;
            Com_Recover();
            link_stats.rx_uart_errors++;
        }

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
            
                ESP_LOGI(TAG, "<< HEARTBEAT (loopback) seq=%d", packet.seq);
                break;

            case CMD_HEARTBEAT_ACK:
            // when the Stm32 send ACK this Executes 
                ESP_LOGI(TAG, "<< HEARTBEAT_ACK seq=%d", packet.seq);
                break;

            case CMD_WIFI_STATUS:
                ESP_LOGI(TAG, "CMD_WIFI_STATUS seq=%d len=%d",
                         packet.seq, packet.payload_length);
                break;

            case CMD_WIFI_STATUS_REQ:
            // When stm32 requests wifi status 
                link_SendWifiStatus(packet.seq);
                break;

            case CMD_TIME_REQ:
                /* TODO: link_SendTimeSync(packet.seq); */
                break;

            case CMD_SLOT_INFO_RESP:
                /* TODO: ota_manager_notify_slot_info_resp() */
                break;

            case CMD_OTA_ACK:
                /* TODO: ota_manager_notify_ota_ack(); */
                break;

            case CMD_OTA_START:
                /* TODO: add OTA start handler here */
                break;

            case CMD_OTA_DATA:
                /* TODO: add OTA chunk handler here */
                break;

            case CMD_OTA_END:
                /* TODO: add OTA end handler here */
                break;

            default:
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
        ESP_LOGE(TAG, "link_Send failed cmd=0x%02X seq=%d", cmd, seq);
    }

    if (s_send_mutex != NULL)
    {
        xSemaphoreGive(s_send_mutex);
    }

    return status;
}

void link_SendWifiStatus(uint16_t seq)
{
    SystemInfo_t sys;
    sysinfo_get_snapshot(&sys);

    link_wifi_status_payload_t payload;
    memset(&payload, 0, sizeof(payload));

    payload.connected = sys.wifi.wifi_connected ? 1U : 0U;
    payload.rssi      = sys.wifi.wifi_rssi;

    // copy the ip into payload.ip
    strncpy(payload.ip_str, sys.wifi.wifi_ip, sizeof(payload.ip_str) - 1U);

    payload.ip_str[sizeof(payload.ip_str) - 1U] = '\0';

    frame_Status_t st = link_Send(CMD_WIFI_STATUS, seq, FRAME_FLAG_EVENT,
                                   (const uint8_t *)&payload, sizeof(payload));

    ESP_LOGI(TAG, "WiFi status sent: connected=%d rssi=%d ip=%s st=%d",
             payload.connected, payload.rssi, payload.ip_str, st);
}

/* void link_SendTimeSync(uint16_t seq) */
/* { */
/*     link_time_payload_t payload; */
/*     payload.unix_timestamp = sntp_client_get_timestamp(); */
/*     payload.synced         = sntp_client_is_synced() ? 1U : 0U; */
/*     link_Send(CMD_TIME_SYNC, seq, FRAME_FLAG_RESPONSE, */
/*               (const uint8_t *)&payload, sizeof(payload)); */
/* } */

void link_Init(void)
{
    /* 1. queue/mutex before anything that could use them */
    s_rx_queue   = xQueueCreate(LINK_RX_QUEUE_DEPTH, sizeof(frame_Packet_t));
    s_send_mutex = xSemaphoreCreateMutex();
    memset(&link_stats, 0, sizeof(link_stats));

    /* 2. bring up the layers below, lowest first */
    if (Com_Init() != COM_OK)
    {
        ESP_LOGE(TAG, "Com_Init failed");
    }

    frame_Init();

    /* 3. wire the layers together via callback registration */
    Com_RegisterEventCallback(com_event_handler);
    frame_RegisterPacketCallback(on_packet_received);

    /* 4. tasks last — they start running immediately */
    xTaskCreate(link_rx_task, "link_rx", LINK_RX_TASK_STACK, NULL,
                LINK_RX_TASK_PRIORITY, NULL);
    xTaskCreate(link_app_task, "link_app", LINK_APP_TASK_STACK, NULL,
                LINK_APP_TASK_PRIORITY, NULL);
}