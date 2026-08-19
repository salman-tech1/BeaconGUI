/*
 * system_info.c  (ESP32 side)
 *
 * Tracks WiFi/time state for publishing to STM32 via UART.
 *
 * FIXED: Added periodic "resync" broadcasts to handle STM32 reset.
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

void set_time_info(uint32_t timestamp, bool synced)
{
    if (xSemaphoreTake(sys_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        sys_info.time.unix_timestamp = timestamp;
        sys_info.time.synced         = synced;
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


/* How often to force-broadcast state, even if unchanged.
 * 30 seconds is a good balance: fast enough for user to not notice
 * a stale display after reset, slow enough to not spam the UART. */
#define RESYNC_INTERVAL_MS     (30U * 1000U)

/* How often to send heartbeats (unchanged) */
#define HEARTBEAT_INTERVAL_MS  6000U

/* Counter for resync timing */
static uint32_t s_last_resync_tick = 0U;

static void publisher_task(void *arg)
{
    (void)arg;

    uint16_t hb_seq   = 0U;
    uint16_t wifi_seq = 0U;
    uint16_t time_seq = 0U;

    /* WiFi change tracking */
    bool   have_sent_wifi = false;
    bool   last_connected = false;
    int8_t last_rssi      = 0;
    char   last_ip[WIFI_MAX_IP_LEN] = {0};

    /* Time change tracking */
    bool     have_sent_time  = false;
    bool     last_synced     = false;
    uint32_t last_timestamp = 0U;

    /* Get initial tick for resync timer */
    s_last_resync_tick = xTaskGetTickCount();

    for (;;)
    {
        SystemInfo_t sys;
        sysinfo_get_snapshot(&sys);

        uint32_t now = xTaskGetTickCount();

        /* ── 1. Heartbeat (every 6 seconds) ─────────────────────────── */
        if (is_linkinit())
        {
            link_Send(CMD_HEARTBEAT, hb_seq++, 0, NULL, 0);
        }

        /* ─ WiFi status change detection ────────────────────────── */
        bool ip_changed = (strncmp(sys.wifi.wifi_ip, last_ip, sizeof(last_ip)) != 0);

        bool wifi_changed = (!have_sent_wifi)
                          || (sys.wifi.wifi_connected != last_connected)
                          || (sys.wifi.wifi_rssi      != last_rssi)
                          || ip_changed;

        /*  Time change detection ──────────────────────────────── */
        bool time_changed = (!have_sent_time)
                          || (sys.time.synced         != last_synced)
                          || (sys.time.unix_timestamp != last_timestamp);

        /*  Periodic resync check ──────────────────────────────── */
        bool resync_due = ((now - s_last_resync_tick) >= pdMS_TO_TICKS(RESYNC_INTERVAL_MS));

        /* ─ Send WiFi status if: changed OR resync due ─────────── */
        if (wifi_changed || (resync_due && sys.wifi.wifi_connected))
        {
            if (is_linkinit())
            {
                link_SendWifiStatus(wifi_seq++);
            }

            have_sent_wifi = true;
            last_connected = sys.wifi.wifi_connected;
            last_rssi      = sys.wifi.wifi_rssi;
            memset(last_ip, 0, sizeof(last_ip));
            strncpy(last_ip, sys.wifi.wifi_ip, sizeof(last_ip) - 1U);
        }

        /*  Send time sync if: changed AND synced, OR resync due ─ */
        if ((time_changed && sys.time.synced) || 
            (resync_due && sys.time.synced))
        {
            if (is_linkinit())
            {
                link_SendTimeSync(time_seq++);
            }

            have_sent_time = true;
            last_synced    = sys.time.synced;
            last_timestamp = sys.time.unix_timestamp;
        }

        /* ─ Reset resync timer ─────────────────────────────────── */
        if (resync_due)
        {
            s_last_resync_tick = now;
            ESP_LOGD(TAG, "Periodic resync sent");
        }

        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}