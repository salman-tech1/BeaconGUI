/*
 * renderer.c
 *
 * Owns the application-level UI update loop. It acts as the consumer
 * of data from system_info and translates it into screen.h API calls.
 */

#include "system_info.h"
#include "render.h"
#include "screen.h"
#include "sensor.h"
#include "tasks_config.h"

#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>

StaticTask_t RenderTCB;
TaskHandle_t RenderHandle;

/* DTCM is dedicated for FreeRTOS tasks */
__attribute__((section(".dtcm_stack")))
StackType_t RenderTaskStack[RENDERER_TASK_STACK_SIZE];

/*
 * Set this to your deployment's UTC offset. The ESP32 sends UTC (that's
 * what CMD_TIME_SYNC carries), so this is the only place that knows
 * "local" from "UTC" — everything upstream of here stays UTC on purpose.
 * Example from the epoch converter: 1786690864 UTC = 07:01:04 AM UTC =
 * 12:01:04 PM at UTC+5 (Pakistan Standard Time). Change the hours below
 * to match wherever the unit is actually installed; for a half-hour zone
 * like India (UTC+5:30) use e.g. (5 * 3600 + 30 * 60).
 */
#define LOCAL_TZ_OFFSET_SEC   (5 * 3600)   /* UTC+5 */

/*
 * Manual unix-timestamp → local HH:MM AM/PM, DD/MM conversion.
 * Avoids <time.h> / gmtime_r which are often stubbed in newlib-nano.
 * Works correctly from Unix epoch (1970-01-01) through 2099.
 */
static void format_time(uint32_t unix_ts_utc, char *buf_out, size_t buf_len, bool short_fmt)
{
    if (buf_out == NULL || buf_len == 0) return;

    /* Shift UTC -> local BEFORE splitting into fields, so the date
     * (not just the clock) rolls over correctly near midnight too.
     * int64_t so this can't underflow if unix_ts_utc is small (e.g. in
     * test builds before the first real time sync). */
    int64_t local_signed = (int64_t)unix_ts_utc + LOCAL_TZ_OFFSET_SEC;
    if (local_signed < 0) local_signed = 0;
    uint32_t unix_ts = (uint32_t)local_signed;

    /* Days and seconds within the day (now local) */
    uint32_t days    = unix_ts / 86400UL;
    uint32_t secs    = unix_ts % 86400UL;
    uint32_t hours24 = secs / 3600UL;
    uint32_t minutes = (secs % 3600UL) / 60UL;

    /* Convert day-count to year / month / day (1970 base) */
    uint32_t year = 1970;
    for (;;)
    {
        uint32_t days_in_year = 365U;
        /* Leap year: divisible by 4, but not by 100 unless also by 400 */
        if ((year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U)))
        {
            days_in_year = 366U;
        }

        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }

    /* Days-in-each-month for a leap year */
    static const uint8_t leap_md[12] = {31,29,31,30,31,30,31,31,30,31,30,31};
    /* Days-in-each-month for a normal year */
    static const uint8_t norm_md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    const uint8_t *md = norm_md;
    if ((year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U)))
    {
        md = leap_md;
    }

    uint32_t month = 0;
    for (month = 0; month < 12U; month++)
    {
        if (days < md[month]) break;
        days -= md[month];
    }
    uint32_t day = days + 1U;   /* 1-based */
    uint32_t mon = month + 1U;  /* 1-based */

    /* 24h -> 12h + AM/PM */
    const char *ampm = (hours24 < 12U) ? "AM" : "PM";
    uint32_t hours12 = hours24 % 12U;
    if (hours12 == 0U) hours12 = 12U;

    if (short_fmt)
    {
        snprintf(buf_out, buf_len, "%02lu:%02lu %s",
                 (unsigned long)hours12, (unsigned long)minutes, ampm);
    }
    else
    {
        snprintf(buf_out, buf_len, "%02lu:%02lu %s, %02lu/%02lu",
                 (unsigned long)hours12, (unsigned long)minutes, ampm,
                 (unsigned long)day, (unsigned long)mon);
    }
}

/*
 * Convert RSSI (dBm) to a human-friendly quality label.
 * Not called yet - screen_dashboard.c ended up with its own copy of this
 * exact logic instead (it can't reach up into render.c to call this one -
 * wrong direction for the layering). Marked unused rather than deleted in
 * case you want to use it here too, e.g. in a log line.
 */
static const char *rssi_to_quality(int8_t rssi) __attribute__((unused));
static const char *rssi_to_quality(int8_t rssi)
{
    if (rssi >= -50)  return "Excellent";
    if (rssi >= -60)  return "Good";
    if (rssi >= -70)  return "Fair";
    return "Weak";
}

static void renderer_task(void *pvParameters)
{
    (void)pvParameters;

    /* Local snapshot — all reads come from here, zero locking in the loop */
    SystemInfo_t sys;

    /* Boot OS Modules (Order matters: Screen depends on GUI) */
    screen_system_init();

    /* 1. Wait for the renderer to be ready */
    screen_wait_ready();

    /* 2. Show the dashboard */
    screen_show(SCREEN_DASHBOARD);

    /* 3. Simulated Data (will be replaced by real sensor data) */
    float kw = 1.0f;
    uint32_t kwh = 0;
    float time_step = 0.0f;

    for (;;)
    {
        /* ── Grab one snapshot — all data comes from this ── */
        sysinfo_get_snapshot(&sys);

        /* ── Simulated sine wave (temporary until real sensors) ── */
        time_step += 0.3f;
        kw = 3.0f + (2.0f * sinf(time_step));
        kwh++;

        /*
         * UPDATE THE UI:
         * No lv_lock()! No LVGL types!
         * screen.c handles all thread-safety internally.
         */

        /* ── Battery ── */
        screen_set_battery(kw,
            (kw > 3.0f) ? SCREEN_BATTERY_DISCHARGING : SCREEN_BATTERY_CHARGING);

        /* ── Production (using simulated data for now) ── */
        screen_set_production(kw * 2.0f, kwh * 2);

        /* ── Consumption (using inverter voltage from snapshot) ── */
        screen_set_consumption(sys.inverter_ac_voltage_v, kwh);

        /* ── Grid (using inverter current from snapshot) ── */
        screen_set_grid(sys.inverter_ac_current_a * 1.5f, kwh * 3, kwh * 2);

        /* ── Charts ── */
        screen_push_chart(SCREEN_CHART_PRODUCTION, (int32_t)(kw * 10));
        screen_push_chart(SCREEN_CHART_CONSUMPTION, (int32_t)(kw * 15));
        screen_push_chart(SCREEN_CHART_GRID, (int32_t)(kw * 20));

        /* ── Weather (temp from snapshot) ── */
        screen_set_weather("Mardan", (int)sys.temp, "cloudy");

        /* ── WiFi Status (from ESP32 via link → system_info) ── */
        if (sys.wifi_valid)
        {
            screen_set_wifi_status(sys.wifi_connected, sys.wifi_rssi, sys.wifi_ip);
        }

        /* ── Time Display (from ESP32 via link → system_info) ── */
        if (sys.time_valid)
        {
            char time_str[32];
            format_time(sys.unix_timestamp, time_str, sizeof(time_str), false);
            screen_set_time(time_str, sys.time_synced);
        }
        else
        {
            screen_set_time("--:--, --/--", false);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void renderer_init(void)
{
    RenderHandle = xTaskCreateStatic(
        renderer_task,
        "render Task",
        RENDERER_TASK_STACK_SIZE,
        NULL,
        RENDERER_TASK_PRIORITY,
        RenderTaskStack,
        &RenderTCB
    );

    /* Assert that the task handle is valid (not NULL) */
    configASSERT(RenderHandle != NULL);
}
