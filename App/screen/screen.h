/*
 * screen.h
 *
 * Application-layer UI facade.
 * This is the ONLY file your application tasks should include for UI control.
 * It intentionally hides LVGL, FreeRTOS, and gui.c completely.
 */

#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SCREEN_DASHBOARD = 0,
    SCREEN_SETTINGS,
    SCREEN_COUNT
} ScreenId_t;

typedef enum {
    SCREEN_BATTERY_IDLE = 0,
    SCREEN_BATTERY_CHARGING,
    SCREEN_BATTERY_DISCHARGING,
} ScreenBatteryState_t;

typedef enum {
    SCREEN_CHART_PRODUCTION = 0,
    SCREEN_CHART_CONSUMPTION,
    SCREEN_CHART_GRID,
} ScreenChartId_t;

/* System */
void screen_system_init(void);  /* Call once in main() before vTaskStartScheduler() */
void screen_wait_ready(void);   /* Blocks until hardware and LVGL are initialized */

/* Navigation */
void screen_show(ScreenId_t id);

/*
 * Data Binding
 * These are 100% thread-safe. External tasks do not need to manage any
 * mutexes or locking mechanisms before calling these.
 */
void screen_set_battery(float kw, ScreenBatteryState_t state);
void screen_set_production(float kw, uint32_t today_kwh);
void screen_set_consumption(float kw, uint32_t today_kwh);
void screen_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh);
void screen_push_chart(ScreenChartId_t chart, int32_t value);
void screen_set_weather(const char *city, int temp_c, const char *condition);
void screen_set_status(const char *bel_eye_state, bool grid_connected,
                       bool grid_export_enabled, const char *disco_name);

#endif /* SCREEN_H */
