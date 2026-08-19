/*
 * screen_dashboard.h
 *
 * Dashboard layout and data binding API.
 * Only called from screen.c (which handles LVGL locking).
 */

#ifndef SCREEN_DASHBOARD_H_
#define SCREEN_DASHBOARD_H_

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* ── Battery State (mirrors screen.h) ────────────────────────────── */
typedef enum {
    DASHBOARD_BATTERY_IDLE = 0,
    DASHBOARD_BATTERY_CHARGING,
    DASHBOARD_BATTERY_DISCHARGING
} DashboardBatteryState_t;

/* ── Chart IDs (mirrors screen.h) ────────────────────────────────── */
typedef enum {
    DASHBOARD_CHART_PRODUCTION = 0,
    DASHBOARD_CHART_CONSUMPTION,
    DASHBOARD_CHART_GRID,
    DASHBOARD_CHART_COUNT
} DashboardChartId_t;

/* ── Creation ────────────────────────────────────────────────────── */
void screen_dashboard_create(lv_obj_t *parent);

/* ── Data Setters (callers MUST hold lv_lock) ────────────────────── */
void screen_dashboard_set_battery(float kw, DashboardBatteryState_t state);
void screen_dashboard_set_production(float kw, uint32_t today_kwh);
void screen_dashboard_set_consumption(float kw, uint32_t today_kwh);
void screen_dashboard_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh);
void screen_dashboard_chart_push(DashboardChartId_t chart, int32_t value);
void screen_dashboard_set_weather(const char *city, int temp_c, const char *condition);
void screen_dashboard_set_status(const char *bel_eye_state, bool grid_connected,
                                  bool grid_export_enabled, const char *disco_name);

/**
 * @param connected  true if WiFi is currently associated
 * @param rssi       signal strength in dBm (only meaningful when connected)
 * @param ip         current IP string, or NULL/empty if not connected.
 *                   NOTE: no ssid parameter yet - see the comment in
 *                   screen_dashboard_set_wifi_status()'s implementation
 *                   for why, and what it'd take to add one.
 */
void screen_dashboard_set_wifi_status(bool connected, int8_t rssi, const char *ip);
void screen_dashboard_set_time(const char *time_str, bool synced);

#endif /* SCREEN_DASHBOARD_H_ */
