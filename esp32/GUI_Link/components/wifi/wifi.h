#ifndef WIFI_MANGER_H
#define WIFI_MANGER_H

#include "esp_err.h"
#include "esp_wifi_types.h"   /* wifi_ap_record_t */
#include <stdbool.h>
#include <stdint.h>

#define WIFI_OK           ESP_OK
#define WIFI_ERR_NO_CREDS ESP_ERR_NOT_FOUND   /* NVS empty, need creds   */
#define WIFI_ERR_TIMEOUT  ESP_ERR_TIMEOUT      /* Could not connect in time */
#define WIFI_ERR_FAILED   ESP_FAIL             /* Auth fail / AP not found  */


#define WIFI_DEFAULT_SSID      "Oppo"        /* fallback if NVS empty */
#define WIFI_DEFAULT_PASS      "Ludokhan2247"    /* fallback if NVS empty */
#define WIFI_CONNECT_TIMEOUT_MS 15000            /* 15 seconds to get IP  */
#define WIFI_MAX_RETRY          5                /* attempts before FAILED */

/* mDNS: device will be reachable at http://beacon.local instead of an IP */
#define WIFI_MDNS_HOSTNAME      "beacon"
#define WIFI_MDNS_INSTANCE_NAME "Beacon Device"

/* Scan results are copied into a fixed-size buffer owned by wifi.c */
#define WIFI_MAX_SCAN_RESULTS   20


/*
 * wifi_manager_init
 *
 * Initialises the WiFi stack, loads credentials from NVS (or uses defaults),
 * starts AP+STA mode, starts mDNS (http://beacon.local), and waits up to
 * WIFI_CONNECT_TIMEOUT_MS for an IP on the station interface.
 *
 * NOTE: the AP interface (and therefore the local webserver you attach to
 * it) comes up regardless of whether the STA connection succeeds — that is
 * intentional, since a failed/absent STA connection is exactly the case
 * where a user needs to reach the AP + webpage to pick a new network.
 *
 * Must be called AFTER nvs_flash_init() in app_main.
 * Must be called BEFORE any task that needs network (SNTP, OTA, HTTP).
 *
 * Returns:
 *   ESP_OK           — connected, IP assigned
 *   WIFI_ERR_TIMEOUT  — no IP within timeout (AP may be down)
 *   WIFI_ERR_FAILED   — auth failure, wrong password
 */

esp_err_t wifi_init(void);

/*
 * wifi_manager_is_connected
 * Non-blocking. Returns true if we currently have an IP address.
 * Safe to call from any task or ISR.
 */
bool wifi_is_connected(void);

/*
 * wifi_manager_wait_connected
 * Blocks the calling task until WiFi is connected or timeout_ms elapses.
 * Pass portMAX_DELAY to wait forever.
 * Returns ESP_OK if connected, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t wifi_wait_connected(uint32_t timeout_ms);

/*
 * wifi_manager_get_ip
 * Returns the current IP as a string e.g. "192.168.1.42".
 * Returns "0.0.0.0" if not connected.
 * The returned pointer is to an internal static buffer — do not free it.
 */
const char *wifi_get_ip(void);

/*
 * wifi_manager_get_rssi
 * Returns signal strength in dBm. Returns 0 if not connected.
 * Typical values: -50 (excellent) to -90 (barely usable).
 */
int8_t wifi_get_rssi(void);

/*
 * wifi_manager_save_creds
 * Saves new SSID and password to NVS.
 * Call this from the HTTP server when the user submits new credentials.
 * The new credentials take effect on the NEXT call to wifi_manager_init()
 * or after esp_restart(), UNLESS you use wifi_sta_connect_to() below,
 * which applies them immediately.
 */
esp_err_t wifi_save_creds(const char *ssid, const char *pass);

/*
 * wifi_scan_networks
 *
 * Performs a BLOCKING active scan (typically ~1-3s) and copies up to
 * max_records results into out_records, sorted by RSSI (strongest first)
 * and de-duplicated by SSID. Safe to call while STA is already connected
 * (the radio briefly interleaves the scan, same as any station scan).
 *
 * out_records: caller-owned array, e.g. wifi_ap_record_t buf[WIFI_MAX_SCAN_RESULTS]
 * max_records: capacity of out_records
 * out_count:   number of entries actually written
 *
 * Returns ESP_OK on success, or the underlying esp_wifi_scan_* error.
 */
esp_err_t wifi_scan_networks(wifi_ap_record_t *out_records,
                              uint16_t max_records,
                              uint16_t *out_count);

/*
 * wifi_sta_connect_to
 *
 * Saves the given SSID/password to NVS AND immediately reconfigures and
 * reconnects the STA interface to it — the AP interface (and webserver)
 * stays up throughout. Intended to be called from the HTTP "connect"
 * handler when a user picks a network from the scanned list.
 *
 * Blocks up to timeout_ms waiting for an IP.
 *
 * Returns ESP_OK on success, WIFI_ERR_TIMEOUT / WIFI_ERR_FAILED otherwise.
 */
esp_err_t wifi_sta_connect_to(const char *ssid, const char *pass, uint32_t timeout_ms);


#endif