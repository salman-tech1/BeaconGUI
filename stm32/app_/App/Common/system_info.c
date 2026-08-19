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
    s_info.wifi_ip[0] = '\0';
    s_mutex = xSemaphoreCreateMutex();
}

void sysinfo_set_board_temp(float temp_c, float humidity_pct)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_info.temp = temp_c;
        /* s_info.board_humidity_pct = humidity_pct; */
        (void)humidity_pct;
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

void sysinfo_set_wifi_status(bool connected, int8_t rssi, const char *ip)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_info.wifi_connected = connected;
        s_info.wifi_rssi      = rssi;
        s_info.wifi_valid     = true;

        /* Safely copy IP string — always null-terminate */
        if (ip != NULL) {
            strncpy(s_info.wifi_ip, ip, sizeof(s_info.wifi_ip) - 1);
            s_info.wifi_ip[sizeof(s_info.wifi_ip) - 1] = '\0';
        } else {
            s_info.wifi_ip[0] = '\0';
        }

        xSemaphoreGive(s_mutex);
    }
}

void sysinfo_set_time(uint32_t unix_timestamp, bool synced)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_info.unix_timestamp = unix_timestamp;
        s_info.time_synced    = synced;
        s_info.time_valid     = true;
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
