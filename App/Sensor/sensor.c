/*
 * sensor.c
 *
 *  Created on: Jul 26, 2026
 *      Author: Muhmmad Salman
 *
 * Periodically reads hardware (I2C temp/humidity sensor + RS485
 * inverter) and updates the system blackboard.
 */



#include "sensor.h"
#include "temp.h"
#include "inverter.h"
#include "system_info.h"
#include "log.h"
#include "tasks_config.h"
#include "FreeRTOS.h"
#include "task.h"

#define SENSOR_POLL_INTERVAL_MS   2000  /* Read temp + inverter every 2 seconds */

/* Modbus/inverter bus parameters. Adjust to match your slave. */
#define INVERTER_SLAVE_ADDR       1U
#define INVERTER_BAUDRATE         9600U

StaticTask_t SensorTCB;
TaskHandle_t SensorHandle;


__attribute__((section(".dtcm_stack")))
StackType_t SensorTaskStack[SENSOR_TASK_STACK_SIZE];


static void sensor_task(void *pvParameters)
{
    (void)pvParameters;

    TempData_t     temp_data;
    InverterData_t inverter_data; // modbus inverter data structure

    /* 1. Initialize the temperature/humidity sensor (I2C) */
    Log_String(LOG_LEVEL_INFO, "sensor_task", "Initializing SHT31...");

    bool temp_ready = (Temp_Init() == TEMP_OK);
    if (!temp_ready) {
        Log_String(LOG_LEVEL_ERROR, "sensor_task", "SHT31 Init Failed! Check I2C.");
    } else {
        Log_String(LOG_LEVEL_INFO, "Sensors", "SHT31 Ready.");
    }

    /* 2. Initialize the inverter (RS485 / Modbus RTU) */
    Log_String(LOG_LEVEL_INFO, "Sensors", "Initializing Inverter (Modbus)...");

    bool inverter_ready = (Inverter_Init(INVERTER_SLAVE_ADDR, INVERTER_BAUDRATE) == INVERTER_OK);
    if (!inverter_ready) {
        Log_String(LOG_LEVEL_ERROR, "Sensors", "Inverter Init Failed! Check RS485 wiring.");
    } else {
        Log_String(LOG_LEVEL_INFO, "Sensors", "Inverter Ready.");
    }

    /* If neither device came up, there's nothing left to poll. Don't
     * crash — just stall this task, same as the original single-sensor
     * behavior. */
    if (!temp_ready && !inverter_ready) {
        for (;;) {
        	 Log_String(LOG_LEVEL_INFO, "Sensors Task", "Nor inverter nor Temp sensor initialized ");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    /* 3. Continuous polling loop */
    for (;;)
    {
        if (temp_ready) {
            if (Temp_Read(&temp_data) == TEMP_OK) {
                sysinfo_set_board_temp(temp_data.temperature_c, temp_data.humidity_rh);
            } else {
            	// CRC mismatch error
                Log_String(LOG_LEVEL_WARN, "Sensors", "SHT31 Read CRC/Comm Error");
            }
        }

        if (inverter_ready) {
            if (Inverter_Read(&inverter_data) == INVERTER_OK) {
                /* Same "keep last good value" policy as the temp sensor above. */
            	sysinfo_set_inverter_data(inverter_data.ac_voltage_v,
            			inverter_data.ac_current_a,
													  inverter_data.output_power_w);
            	        }
            } else {
                Log_String(LOG_LEVEL_WARN, "Sensors", "Inverter Read Error");
            }
        	/* Sleep until next poll */
             vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
        }


    }

void sensor_init(void)
{

//    BaseType_t status = xTaskCreate(
//    	sensor_task,
//        "Sensors",
//		SENSOR_TASK_STACK_SIZE,
//        NULL,
//		SENSOR_TASK_PRIORITY,
//        NULL
//    );
//    configASSERT(status == pdPASS);

	SensorHandle = xTaskCreateStatic(
		sensor_task,
        "Sensor Task",
		SENSOR_TASK_STACK_SIZE,
        NULL,
        SENSOR_TASK_PRIORITY,
		SensorTaskStack,
        &SensorTCB
    );

	  configASSERT(SensorHandle != NULL);

}
