/*
 * system_info.h  (ESP32 side)
 *
 * Tracks WiFi/connection state for publishing to STM32 via UART.
 */

#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stdint.h>
#include <stdbool.h>

#define WIFI_MAX_SSID_LEN   32U
#define WIFI_MAX_PASS_LEN   64U
#define WIFI_MAX_IP_LEN     16U

typedef struct
{
    char    wifi_ssid[WIFI_MAX_SSID_LEN];
    char    wifi_password[WIFI_MAX_PASS_LEN];
    char    wifi_ip[WIFI_MAX_IP_LEN];
    int8_t  wifi_rssi;
    bool    wifi_connected;
} wifi_info_t;

typedef struct
{
    wifi_info_t wifi;

    bool    http_server_started;
    uint32_t uptime_seconds;
} SystemInfo_t;

/*
 * Thread-Safe API
 * Drivers call the 'set' functions. Publisher task calls the 'get' function.
 */
void sysinfo_init(void);
void set_wifi_info(wifi_info_t *w);
void sysinfo_get_snapshot(SystemInfo_t *out_snapshot);

#endif /* SYSTEM_INFO_H */