/*
 * system_info.c
 *
 *  Created on: Jul 26, 2026
 *      Author: Muhmmad Salman
 */


#include "system_info.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

static SystemInfo_t s_info;
static SemaphoreHandle_t s_mutex;

void sysinfo_init(void)
{
    memset(&s_info, 0, sizeof(SystemInfo_t));
    s_mutex = xSemaphoreCreateMutex();
}

// set the mutex
void sysinfo_set_board_temp(float temp_c, float humidity_pct)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_info.temp = temp_c;
        // s_info.board_humidity_pct = humidity_pct;
        xSemaphoreGive(s_mutex);
    }
}

void sysinfo_set_inverter_data(float voltage_v, float current_a, float power_w)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_info.inverter_ac_voltage_v = voltage_v;
        s_info.inverter_ac_current_a = current_a;
        s_info.inverter_output_power_w = power_w;
        xSemaphoreGive(s_mutex);
    }
}

/*
 * The Snapshot trick:
 * Instead of the UI locking/unlocking 10 times to read 10 different values,
 * it locks ONCE, copies the whole thing to a local variable, and unlocks.
 * The UI then reads from its local copy. Zero lag, zero risk.
 */
void sysinfo_get_snapshot(SystemInfo_t *out_snapshot)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out_snapshot = s_info; /* Fast struct copy */
        xSemaphoreGive(s_mutex);
    }
}
