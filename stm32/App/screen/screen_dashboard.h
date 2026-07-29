/*
 * screen_dashboard.h
 *
 * Energy-monitoring dashboard screen interface.
 *
 * This module owns every LVGL object for the dashboard. Nothing outside
 * this file should touch its lv_obj_t pointers directly—this keeps widget
 * lifecycle management completely internalized.
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

/*
 * Builds the dashboard widget tree onto the provided parent object.
 * Called exactly once by the screen manager when the dashboard is shown for the first time.
 */
void screen_dashboard_create(lv_obj_t *parent);

/* Data-binding API. Call these to update the UI with live sensor data. */
void screen_dashboard_set_battery(float kw, DashboardBatteryState_t state);
void screen_dashboard_set_production(float kw, uint32_t today_kwh);
void screen_dashboard_set_consumption(float kw, uint32_t today_kwh);
void screen_dashboard_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh);

/* Pushes a new sample to the sparkline chart. */
void screen_dashboard_chart_push(DashboardChartId_t chart, int32_t value);

void screen_dashboard_set_weather(const char *city, int temp_c, const char *condition);
void screen_dashboard_set_status(const char *bel_eye_state, bool grid_connected,
                                  bool grid_export_enabled, const char *disco_name);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_DASHBOARD_H */
