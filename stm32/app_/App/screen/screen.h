/*
 * screen.h
 *
 * Application UI facade. All UI updates go through these functions.
 * Thread-safety (LVGL mutex) is handled internally.
 */

#ifndef SCREEN_H_
#define SCREEN_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Screen IDs ──────────────────────────────────────────────────── */
typedef enum {
    SCREEN_DASHBOARD = 0,
    SCREEN_SETTINGS,
    SCREEN_COUNT
} ScreenId_t;

/* ── Battery State ───────────────────────────────────────────────── */
typedef enum {
    SCREEN_BATTERY_IDLE = 0,
    SCREEN_BATTERY_CHARGING,
    SCREEN_BATTERY_DISCHARGING
} ScreenBatteryState_t;

/* ── Chart IDs ───────────────────────────────────────────────────── */
typedef enum {
    SCREEN_CHART_PRODUCTION = 0,
    SCREEN_CHART_CONSUMPTION,
    SCREEN_CHART_GRID,
    SCREEN_CHART_COUNT
} ScreenChartId_t;

/* ── Initialization ──────────────────────────────────────────────── */
void screen_system_init(void);
void screen_wait_ready(void);
void screen_show(ScreenId_t id);

/* ── Data Setters (thread-safe) ──────────────────────────────────── */
void screen_set_battery(float kw, ScreenBatteryState_t state);
void screen_set_production(float kw, uint32_t today_kwh);
void screen_set_consumption(float kw, uint32_t today_kwh);
void screen_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh);
void screen_push_chart(ScreenChartId_t chart, int32_t value);
void screen_set_weather(const char *city, int temp_c, const char *condition);
void screen_set_status(const char *bel_eye_state, bool grid_connected,
                       bool grid_export_enabled, const char *disco_name);

/* ── NEW: WiFi & Time Setters ────────────────────────────────────── */

/**
 * @brief  Update WiFi status indicator on the dashboard.
 * @param  connected  true if WiFi is connected
 * @param  rssi       Signal strength in dBm (negative, e.g., -65)
 * @param  ip         Current IP string (e.g. "192.168.1.100"), or NULL/empty
 *                     when not connected.
 */
void screen_set_wifi_status(bool connected, int8_t rssi, const char *ip);

/**
 * @brief  Update the time display on the dashboard.
 * @param  time_str   Formatted time string (e.g., "14:30, 29/07")
 * @param  synced     true if time was NTP-synced
 */
void screen_set_time(const char *time_str, bool synced);

#endif /* SCREEN_H_ */
