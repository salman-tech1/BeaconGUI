/*
 * sntp_client.c  (ESP32 side)
 *
 * SNTP time synchronization client.
 * Blocks until WiFi is available, then syncs with NTP servers and
 * stores the result in system_info. The publisher_task picks it up
 * and sends CMD_TIME_SYNC to STM32 — this file never touches the
 * link layer directly.
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "esp_log.h"

#include "sntp.h"
#include "wifi.h"
#include "system_info.h"

static const char *TAG = "SNTP";

#define SNTP_TASK_STACK     4096U
#define SNTP_TASK_PRIORITY  4

/* ── Module state ────────────────────────────────────────────────────────── */
static volatile bool s_synced = false;

/* ── Sync notification callback ──────────────────────────────────────────── */
static void sntp_sync_notification_cb(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "NTP sync complete — epoch=%lld", (long long)tv->tv_sec);
}

/* ── Task entry point ────────────────────────────────────────────────────── */
static void sntp_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task started — waiting for WiFi...");

    /* ── Step 1: Wait for WiFi ──────────────────────────────────────────── */
    esp_err_t wifi_err = wifi_wait_connected(30000);
    if (wifi_err != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi not available — will retry when connected");
    }
    else
    {
        ESP_LOGI(TAG, "WiFi ready — starting SNTP");
    }

    /* ── Step 2: Configure SNTP ─────────────────────────────────────────── */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER_PRIMARY);
    esp_sntp_setservername(1, SNTP_SERVER_SECONDARY);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_sync_interval(15000);

    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);

    /* ── Step 3: Start the SNTP engine ──────────────────────────────────── */
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP engine started — waiting for first sync...");

    /* ── Step 4: Wait for first sync ────────────────────────────────────── */
    uint32_t waited_ms = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
           waited_ms < SNTP_SYNC_WAIT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(SNTP_POLL_PERIOD_MS));
        waited_ms += SNTP_POLL_PERIOD_MS;
        ESP_LOGI(TAG, "Waiting for sync... %lums", (unsigned long)waited_ms);
    }

    /* ── Step 5: Store first sync in system_info ────────────────────────── */
    if (s_synced)
    {
        ESP_LOGI(TAG, "First sync completed in %lums", (unsigned long)waited_ms);
        set_time_info(sntp_get_timestamp(), true);
    }
    else
    {
        ESP_LOGW(TAG, "First sync timed out after %lums — will retry",
                 (unsigned long)waited_ms);
    }

    /* ── Step 6: Main loop — update system_info periodically ────────────── */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(SNTP_SYNC_INTERVAL_MS));

        if (!wifi_is_connected())
        {
            ESP_LOGW(TAG, "WiFi lost — skipping time update this cycle");
            continue;
        }

        sntp_sync_status_t status = sntp_get_sync_status();
        if (status == SNTP_SYNC_STATUS_RESET && !s_synced)
        {
            ESP_LOGW(TAG, "Clock not synced — skipping time update");
            continue;
        }

        set_time_info(sntp_get_timestamp(), s_synced);

        ESP_LOGI(TAG, "Time updated in system_info: ts=%lu synced=%d RSSI=%ddBm",
                 (unsigned long)sntp_get_timestamp(), s_synced, wifi_get_rssi());
    }
}

static void configure_timezone(void)
{
    setenv("TZ", "PKT-5", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to PKT (UTC+5)");
}

void sntp_init_(void)
{   
    // Configure timezone 
    configure_timezone() ; 

    if (xTaskCreate(sntp_task, "sntp", SNTP_TASK_STACK, NULL,
                    SNTP_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create SNTP task");
    }
}

bool sntp_is_synced(void)
{
    return s_synced;
}

uint32_t sntp_get_timestamp(void)
{
    if (!s_synced) return 0;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}