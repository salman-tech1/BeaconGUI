/*
 * screen.c
 *
 * Implementation of the application UI facade.
 * Acts as a middleman: translates generic screen.h calls into specific
 * gui.c and screen_dashboard.c calls, handling all LVGL thread-safety here.
 */

#include "screen.h"
#include "gui.h"
#include "screen_dashboard.h"
#include "screen_menu.h"

#include "lvgl.h"

static lv_obj_t *s_screens[SCREEN_COUNT] = { NULL };
static bool s_menu_initialized = false;

void screen_system_init(void)
{
    gui_init();
}

void screen_wait_ready(void)
{
    gui_wait_ready();
}

void screen_show(ScreenId_t id)
{
    screen_wait_ready();
    lv_lock();


    /* Initialize the global menu overlay exactly once */
      if (!s_menu_initialized) {
          screen_menu_init();
          s_menu_initialized = true;
      }


    /* Lazy-initialize screens on first request to save RAM at boot */
    if (s_screens[id] == NULL) {
        s_screens[id] = lv_obj_create(NULL);

        switch (id) {
            case SCREEN_DASHBOARD:
                screen_dashboard_create(s_screens[id]);
                break;
            case SCREEN_SETTINGS:
                lv_obj_set_style_bg_color(s_screens[id], lv_color_hex(0x16213E), LV_PART_MAIN);
                break;
            default: break;
        }
    }
    lv_screen_load(s_screens[id]);
    lv_unlock();
}

/* ── Thread-safe setters ────────────────────────────────────────────
 * We acquire the LVGL mutex here so the caller (e.g. a sensor task)
 * doesn't have to know about LVGL internals.
 */
void screen_set_battery(float kw, ScreenBatteryState_t state)
{
    lv_lock();
    screen_dashboard_set_battery(kw, (DashboardBatteryState_t)state);
    lv_unlock();
}

void screen_set_production(float kw, uint32_t today_kwh)
{
    lv_lock();
    screen_dashboard_set_production(kw, today_kwh);
    lv_unlock();
}

void screen_set_consumption(float kw, uint32_t today_kwh)
{
    lv_lock();
    screen_dashboard_set_consumption(kw, today_kwh);
    lv_unlock();
}

void screen_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh)
{
    lv_lock();
    screen_dashboard_set_grid(kw, bought_kwh, sold_kwh);
    lv_unlock();
}

void screen_push_chart(ScreenChartId_t chart, int32_t value)
{
    lv_lock();
    screen_dashboard_chart_push((DashboardChartId_t)chart, value);
    lv_unlock();
}

void screen_set_weather(const char *city, int temp_c, const char *condition)
{
    lv_lock();
    screen_dashboard_set_weather(city, temp_c, condition);
    lv_unlock();
}

void screen_set_status(const char *bel_eye_state, bool grid_connected,
                       bool grid_export_enabled, const char *disco_name)
{
    lv_lock();
    screen_dashboard_set_status(bel_eye_state, grid_connected, grid_export_enabled, disco_name);
    lv_unlock();
}
