#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "httpServer.h"
#include "wifi.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "link.h"
#include "system_info.h"
#include "sntp.h"
#include "ota.h"

static const char *TAG = "MAIN";

void app_main(void)
{
     /* NVS must be ready before wifi_init() reads/writes credentials */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Initializing system info...");
    sysinfo_init();

    
    ESP_LOGI(TAG, "Initializing UART link...");
    link_Init();
    ota_init();

    ESP_LOGI(TAG, "Calling wifi_init()...");
    esp_err_t wifi_status = wifi_init();

    switch (wifi_status)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "STA connected. IP=%s  RSSI=%d dBm", wifi_get_ip(), wifi_get_rssi());
        break;
    case WIFI_ERR_TIMEOUT:
        ESP_LOGW(TAG, "STA did not get an IP in time (wrong SSID / AP out of range?)");
        break;
    case WIFI_ERR_FAILED:
        ESP_LOGW(TAG, "STA auth failed (wrong password?)");
        break;
    default:
        ESP_LOGE(TAG, "wifi_init() returned unexpected error: 0x%x", wifi_status);
        break;
    }

    /* Start HTTP server for network provisioning */
    http_server_start();

    /* Start SNTP client — task waits for WiFi internally */
    ESP_LOGI(TAG, "Starting SNTP client...");
    sntp_init_();

    vTaskDelete(NULL);
}