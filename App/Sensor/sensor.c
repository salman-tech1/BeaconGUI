/*
 * sensor.c
 *
 *  Created on: Jul 26, 2026
 *      Author: Muhmmad Salman
 *
 * Periodically reads hardware
 * sensors and updates the system blackboard.
 */



#include "sensor.h"
#include "temp.h"
#include "system_info.h"
#include "log.h"
#include "tasks_config.h"
#include "FreeRTOS.h"
#include "task.h"

#define SENSOR_POLL_INTERVAL_MS  2000  /* Read temp every 2 seconds */

static void sensor_task(void *pvParameters)
{
    (void)pvParameters;
    TempData_t sensor_data;

    /* 1. Initialize the hardware via the BSP */
    Log_String(LOG_LEVEL_INFO, "Sensors", "Initializing SHT31...");

    if (Temp_Init() != TEMP_OK) {
        Log_String(LOG_LEVEL_ERROR, "Sensors", "SHT31 Init Failed! Check I2C.");

        /* If hardware is dead, don't crash. Just stall this task. */
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    Log_String(LOG_LEVEL_INFO, "Sensors", "SHT31 Ready.");

    /* 2. Continuous Polling Loop */
    for (;;)
    {
        /* Ask the BSP to read the sensor */
        if (Temp_Read(&sensor_data) == TEMP_OK)
        {
            /* Success: Push to the blackboard */
            sysinfo_set_board_temp(sensor_data.temperature_c, sensor_data.humidity_rh);
        }
        else
        {
            /*
             * Error: CRC failed or I2C NAK.
             * We DO NOT update sysinfo. The UI will just keep displaying the
             * last known good temperature, which is much better than showing
             * "0.00" or garbage data.
             */
            Log_String(LOG_LEVEL_WARN, "Sensors", "SHT31 Read CRC/Comm Error");
        }

        /* Sleep until next poll */
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}

void sensor_init(void)
{
    BaseType_t status = xTaskCreate(
    	sensor_task,
        "Sensors",
		SENSOR_TASK_STACK_SIZE,       /* Small stack, no heavy math here */
        NULL,
		SENSOR_TASK_PRIORITY, /* Higher priority than renderer, but lower than GUI core */
        NULL
    );
    configASSERT(status == pdPASS);
}
