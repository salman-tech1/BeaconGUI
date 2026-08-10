#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
 

#include "esp_wifi.h"
#include "wifi.h"
 
static const char *TAG = "WIFI_TEST";
 
void app_main(void)
{
    /* wifi_init() reads/writes credentials in NVS, so NVS must be ready first. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
 
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
 
    ESP_LOGI(TAG, "AP should be up regardless — SSID=BEACONGUI_WIFI  PASS=11223344  IP=192.168.0.2");
    ESP_LOGI(TAG, "Try joining that AP from a phone/laptop and pinging 192.168.0.2");
 
  
    ESP_LOGI(TAG, "Running a test scan...");
 
    wifi_ap_record_t nets[WIFI_MAX_SCAN_RESULTS];
    uint16_t count = 0;
    esp_err_t scan_err = wifi_scan_networks(nets, WIFI_MAX_SCAN_RESULTS, &count);
 
    if (scan_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Found %u unique network(s):", count);
        for (uint16_t i = 0; i < count; i++)
        {
            ESP_LOGI(TAG, "  %2u) %-32s RSSI=%4d  ch=%2u  %s",
                     (unsigned)(i + 1),
                     (const char *)nets[i].ssid,
                     nets[i].rssi,
                     (unsigned)nets[i].primary,
                     (nets[i].authmode == WIFI_AUTH_OPEN) ? "open" : "secured");
        }
    }
    else
    {
        ESP_LOGE(TAG, "wifi_scan_networks() failed: 0x%x", scan_err);
    }
 
    /* Heartbeat — watch this to confirm reconnect/retry behaviour, RSSI
     * changes, etc. over time from the serial monitor. */
static uint32_t ind = 0 ; 
     while (1)
    {
        ESP_LOGI(TAG, "connected=%d  ip=%s  rssi=%d dBm",
                 wifi_is_connected(), wifi_get_ip(), wifi_get_rssi());


    if(ind > 4 )
        {
            wifi_sta_connect_to("MUHMMAD_SALMAN 6712","E943d01/",10000) ; 
            }
        else 
        {
       ++ind ; 

        }    
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}