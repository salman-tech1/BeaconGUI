/*
 * screen_dashboard.h
 *
 * Energy-monitoring dashboard screen (BeaconView-style layout).
 * Owns every LVGL object for this screen. gui.c should only call the
 * public API below and never touch lv_obj_t pointers directly — that
 * keeps widget lifetime/locking entirely inside this file.
 *
 * Threading: screen_dashboard_create() must run on the GUI task, before
 * GUI_READY_BIT is set. All the setter functions below are only safe to
 * call from inside handle_event() in gui.c (i.e. already under lv_lock()).
 *
 *  Created on: Jul 23, 2026
 */

#ifndef SCREEN_DASHBOARD_H
#define SCREEN_DASHBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

typedef enum {
    DASHBOARD_BATTERY_IDLE = 0,
    DASHBOARD_BATTERY_CHARGING,
    DASHBOARD_BATTERY_DISCHARGING,
} DashboardBatteryState_t;

typedef enum {
    DASHBOARD_CHART_PRODUCTION = 0,
    DASHBOARD_CHART_CONSUMPTION,
    DASHBOARD_CHART_GRID,
} DashboardChartId_t;

/**
 * @brief Build the dashboard screen tree under `parent`.
 *
 * Call once from gui.c's build_initial_ui(), e.g.:
 *     screen_dashboard_create(lv_scr_act());
 *
 * Populates every widget with placeholder data matching the reference
 * design so you can see the full layout immediately, before any real
 * data source is wired up.
 */
void screen_dashboard_create(lv_obj_t *parent);

/* ── Data-binding API — wire these into gui.c's handle_event() ──────── */

/** kw: instantaneous battery power flow. state drives the pill label/color. */
void screen_dashboard_set_battery(float kw, DashboardBatteryState_t state);

void screen_dashboard_set_production(float kw, uint32_t today_kwh);
void screen_dashboard_set_consumption(float kw, uint32_t today_kwh);
void screen_dashboard_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh);

/** Push one new sample onto a sparkline. Call at whatever cadence you sample. */
void screen_dashboard_chart_push(DashboardChartId_t chart, int32_t value);

void screen_dashboard_set_weather(const char *city, int temp_c, const char *condition);
void screen_dashboard_set_status(const char *bel_eye_state, bool grid_connected,
                                  bool grid_export_enabled, const char *disco_name);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_DASHBOARD_H */
