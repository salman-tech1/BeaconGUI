/*
 * temp.h
 *
 *  Public API for the temperature/humidity sensor.
 *
 */

#ifndef TEMP_H
#define TEMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TEMP_OK        =  0,   /**< Operation successful */
    TEMP_ERROR     = -1,   /**< Generic / bus error */
    TEMP_TIMEOUT   = -2,   /**< Communication timeout */
    TEMP_BUSY      = -3,   /**< Bus busy */
    TEMP_CRC_ERROR = -4,   /**< Data failed integrity check */
    TEMP_NOT_READY = -5,   /**< Called before Temp_Init() succeeded */
} Temp_Status_t;

/** Last measurement, already converted to engineering units. */
typedef struct {
    float temperature_c;   /**< Degrees Celsius */
    float humidity_rh;     /**< Percent relative humidity */
} TempData_t;

/**
 * @brief  Bring up the temperature/humidity sensor (bus init, chip
 *         reset, presence check).
 * @retval TEMP_OK on success, error code otherwise
 */
Temp_Status_t Temp_Init(void);

/**
 * @brief  Trigger one measurement and read back temperature + humidity.
 * @param  data  Output: populated only when TEMP_OK is returned
 * @retval TEMP_OK on success, error code otherwise
 */
Temp_Status_t Temp_Read(TempData_t *data);

/**
 * @brief  Turn the sensor's self-heating element on/off.
 *         Used to drive off condensation on the sensing element.
 * @retval TEMP_OK on success, error code otherwise
 */
Temp_Status_t Temp_SetHeater(bool enable);

/**
 * @brief  Print the connected sensor's status over the debug UART.
 *         Handy for confirming wiring/bring-up without a scope.
 */
void Temp_Debug_PingTest(void);

#ifdef __cplusplus
}
#endif

#endif /* TEMP_H */
