/*
 * sensor.c
 */

#include "sensor.h"
#include "temp.h"
#include "inverter.h"
#include "system_info.h"
#include "log.h"
#include "tasks_config.h"
#include "FreeRTOS.h"
#include "task.h"

#define SENSOR_POLL_INTERVAL_MS   2000

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
    InverterData_t inverter_data;

    Log_String(LOG_LEVEL_INFO, "sensor_task", "Initializing SHT31...");

    bool temp_ready = (Temp_Init() == TEMP_OK);
    if (!temp_ready) {
        Log_String(LOG_LEVEL_ERROR, "sensor_task", "SHT31 Init Failed! Check I2C.");
    } else {
        Log_String(LOG_LEVEL_INFO, "Sensors", "SHT31 Ready.");
    }

    Log_String(LOG_LEVEL_INFO, "Sensors", "Initializing Inverter (Modbus)...");

    bool inverter_ready = (Inverter_Init(INVERTER_SLAVE_ADDR, INVERTER_BAUDRATE) == INVERTER_OK);
    if (!inverter_ready) {
        Log_String(LOG_LEVEL_ERROR, "Sensors", "Inverter Init Failed! Check RS485 wiring.");
    } else {
        Log_String(LOG_LEVEL_INFO, "Sensors", "Inverter Ready.");
    }

    if (!temp_ready && !inverter_ready) {
        for (;;) {
            Log_String(LOG_LEVEL_INFO, "Sensors Task", "Neither inverter nor Temp sensor initialized");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    Log_String(LOG_LEVEL_INFO, "Sensors", "Entering poll loop.");

    for (;;)
    {
        /* ── Temperature ── */
        if (temp_ready) {
            if (Temp_Read(&temp_data) == TEMP_OK) {
                sysinfo_set_board_temp(temp_data.temperature_c, temp_data.humidity_rh);
             //   Log_Float(LOG_LEVEL_INFO, "SHT31", "temperature_c", temp_data.temperature_c, 1);
             //   Log_Float(LOG_LEVEL_INFO, "SHT31", "humidity_rh", temp_data.humidity_rh, 1);
            } else {
                Log_String(LOG_LEVEL_WARN, "Sensors", "SHT31 Read CRC/Comm Error");
            }
        }

        /* ── Inverter ── */
        if (inverter_ready) {
            Inverter_Status_t inv_stat = Inverter_Read(&inverter_data);

            if (inv_stat == INVERTER_OK) {
                sysinfo_set_inverter_data(
                    inverter_data.ac_voltage_v,
                    inverter_data.ac_current_a,
                    inverter_data.output_power_w
                );
              //  Log_Printf(LOG_LEVEL_INFO, "Sensors", "Inv: %.1fV %.1fA %.0fW",
                //           (double)inverter_data.ac_voltage_v,
                 //          (double)inverter_data.ac_current_a,
                  //         (double)inverter_data.output_power_w);
            } else {
                /* THIS is the line that was never reached before the fix —
                 * the misplaced else meant this never logged, and the Modbus
                 * FIFO hang prevented the loop from continuing at all. */
                Log_Printf(LOG_LEVEL_WARN, "Sensors", "Inverter Read Error (code=%d)", (int)inv_stat);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}

void sensor_init(void)
{
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
