/*
 * system_info.h
 *
 * Centralized system state repository. All producer tasks (link, sensors)
 * write here; all consumer tasks (renderer) read via snapshot.
 *
 *  Created on: Jul 26, 2026
 *      Author: Muhmmad Salman
 */

#ifndef SYSTEM_INFO_H_
#define SYSTEM_INFO_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* ── Board Sensors ──────────────────────────────────────────── */
    float temp;
    /* float board_humidity_pct; */  /* Reserved for future use */

    /* ── Inverter Data ──────────────────────────────────────────── */
    float inverter_ac_voltage_v;
    float inverter_ac_current_a;
    float inverter_output_power_w;

    /* ── WiFi Status */
    bool     wifi_connected;      /* true = WiFi connected to AP */
    int8_t   wifi_rssi;           /* Signal strength in dBm (negative) */
    char     wifi_ip[16];         /* "192.168.1.100" */
    bool     wifi_valid;          /* true after first CMD_WIFI_STATUS */

    /* ── Time Sync */
    uint32_t unix_timestamp;      /* Unix epoch seconds */
    bool     time_synced;         /* true if ESP32 got NTP sync */
    bool     time_valid;          /* true after first CMD_TIME_SYNC */
} SystemInfo_t;

/**
 * @brief  Initialize the system info struct and its mutex.
 *         Call once BEFORE any other sysinfo_ function and BEFORE
 *         the scheduler starts.
 */
void sysinfo_init(void);

/**
 * @brief  Update board temperature (and optionally humidity).
 */
void sysinfo_set_board_temp(float temp_c, float humidity_pct);

/**
 * @brief  Update inverter AC measurements.
 */
void sysinfo_set_inverter_data(float voltage_v, float current_a, float power_w);

/**
 * @brief  Update WiFi status received from ESP32.
 * @param  connected  true if WiFi is connected to an AP
 * @param  rssi       Signal strength in dBm (typically -30 to -90)
 * @param  ip         IP address string (e.g., "192.168.1.100")
 */
void sysinfo_set_wifi_status(bool connected, int8_t rssi, const char *ip);

/**
 * @brief  Update time info received from ESP32.
 * @param  unix_timestamp  Unix epoch in seconds
 * @param  synced          true if ESP32 successfully synced with NTP
 */
void sysinfo_set_time(uint32_t unix_timestamp, bool synced);

/**
 * @brief  Atomically copy the entire system info struct to a local variable.
 *         The caller then reads from its local copy — zero locks, zero risk.
 * @param  out_snapshot  Pointer to caller's local struct to receive the copy.
 */
void sysinfo_get_snapshot(SystemInfo_t *out_snapshot);

#endif /* SYSTEM_INFO_H_ */
