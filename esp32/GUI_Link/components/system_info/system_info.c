/*
 * system_info.c  (ESP32 side)
 *
 * Tracks WiFi/connection state for publishing to STM32 via UART.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "Tasks_common.h"
#include "link.h"
#include "system_info.h"

static const char *TAG = "systemINFO";

static SystemInfo_t sys_info;
static SemaphoreHandle_t sys_mutex;

static void publisher_task(void *arg);

void sysinfo_init(void)
{
    memset(&sys_info, 0, sizeof(SystemInfo_t));
    sys_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "sysinfo_init : Initialized");

    if (xTaskCreate(publisher_task, "publisher_task", SYSTEM_INFO_TASK_STACK,
                    NULL, SYSTEM_INFO_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create publisher_task");
    }
}

void set_wifi_info(wifi_info_t *w)
{
    if (xSemaphoreTake(sys_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        sys_info.wifi.wifi_connected = w->wifi_connected;
        sys_info.wifi.wifi_rssi      = w->wifi_rssi;

        strncpy(sys_info.wifi.wifi_ssid,     w->wifi_ssid,     sizeof(sys_info.wifi.wifi_ssid) - 1U);
        strncpy(sys_info.wifi.wifi_password,  w->wifi_password,  sizeof(sys_info.wifi.wifi_password) - 1U);
        strncpy(sys_info.wifi.wifi_ip,        w->wifi_ip,        sizeof(sys_info.wifi.wifi_ip) - 1U);

        sys_info.wifi.wifi_ssid[sizeof(sys_info.wifi.wifi_ssid) - 1U]         = '\0';
        sys_info.wifi.wifi_password[sizeof(sys_info.wifi.wifi_password) - 1U] = '\0';
        sys_info.wifi.wifi_ip[sizeof(sys_info.wifi.wifi_ip) - 1U]            = '\0';

        xSemaphoreGive(sys_mutex);
    }
}

void sysinfo_get_snapshot(SystemInfo_t *out_snapshot)
{
    if (xSemaphoreTake(sys_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        *out_snapshot = sys_info;
        xSemaphoreGive(sys_mutex);
    }
}

static void publisher_task(void *arg)
{
    (void)arg;

    uint16_t hb_seq   = 0U;
    uint16_t wifi_seq = 0U;

    bool   have_sent_wifi = false;
    bool   last_connected = false;
    int8_t last_rssi      = 0;
    char   last_ip[WIFI_MAX_IP_LEN] = {0};

    for (;;)
    {
        SystemInfo_t sys;
        sysinfo_get_snapshot(&sys);

        /* keep-alive ping — unconditional */
        link_Send(CMD_HEARTBEAT, hb_seq++, 0, NULL, 0);

        /* wifi status — only on a real change */
        bool ip_changed = (strncmp(sys.wifi.wifi_ip, last_ip, sizeof(last_ip)) != 0);

        bool wifi_changed = (!have_sent_wifi)
                          || (sys.wifi.wifi_connected != last_connected)
                          || (sys.wifi.wifi_rssi      != last_rssi)
                          || ip_changed;

        if (wifi_changed)
        {
            link_SendWifiStatus(wifi_seq++);

            have_sent_wifi = true;
            last_connected = sys.wifi.wifi_connected;
            last_rssi      = sys.wifi.wifi_rssi;
            memset(last_ip, 0, sizeof(last_ip));
            strncpy(last_ip, sys.wifi.wifi_ip, sizeof(last_ip) - 1U);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}