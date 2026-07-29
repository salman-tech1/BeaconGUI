/*
 * system_info.h
 *
 *  Created on: Jul 26, 2026
 *      Author: Muhmmad Salman
 * The "Blackboard". A single source of truth for the entire system.
 * Drivers write to it. The UI reads from it.
 */

#ifndef COMMON_SYSTEM_INFO_H
#define COMMON_SYSTEM_INFO_H

#include <stdint.h>
#include <stdbool.h>



typedef struct {

    float temp;

    float inverter_ac_voltage_v;
    float inverter_ac_current_a;
    float inverter_output_power_w;

    /* --- System --- */
    uint32_t uptime_seconds;

} SystemInfo_t;

/*
 * Thread-Safe API
 * Drivers call the 'set' functions. UI calls the 'get' function.
 */
void sysinfo_init(void);

// Setting the temperature and storing in the data structure
void sysinfo_set_board_temp(float temp_c, float humidity_pct) ;

void sysinfo_set_inverter_data(float voltage_v, float current_a, float power_w); ;


/* Getter (Called by UI task) */
void sysinfo_get_snapshot(SystemInfo_t *out_snapshot);

#endif /* COMMON_SYSTEM_INFO_H */
