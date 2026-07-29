/*
 * inverter.h
 *
 *  Created on: Jul 27, 2026
 *      Author: Muhmmad Salman
 */

#ifndef INVERTER_H
#define INVERTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    INVERTER_OK        =  0,   /**< Operation successful */
    INVERTER_ERROR     = -1,   /**< Generic / bus error */
    INVERTER_TIMEOUT   = -2,   /**< Communication timeout */
    INVERTER_CRC_ERROR = -3,   /**< Response failed CRC16 check */
    INVERTER_NOT_READY = -4,   /**< Called before Inverter_Init() succeeded */
} Inverter_Status_t;

/** Last telemetry snapshot, already converted to engineering units. */
typedef struct {
    float    ac_voltage_v;      /**< AC output voltage, Volts */
    float    ac_current_a;      /**< AC output current, Amps */
    float    ac_frequency_hz;   /**< AC output frequency, Hz */
    float    output_power_w;    /**< AC output power, Watts */
    float    dc_voltage_v;      /**< DC bus/input voltage, Volts */
    uint16_t status_word;       /**< Raw inverter status bitfield */
} InverterData_t;

/**
 * @brief  Bring up the RS485/Modbus bus and select the inverter's
 *         slave address.
 * @param  slave_addr  Modbus slave address of the inverter (1-247)
 * @param  baudrate    Bus baud rate (e.g. 9600)
 * @retval INVERTER_OK on success, error code otherwise
 */
Inverter_Status_t Inverter_Init(uint8_t slave_addr, uint32_t baudrate);

/**
 * @brief  Read the inverter's telemetry block and convert it to
 *         engineering units in one shot.
 * @param  data  Output: populated only when INVERTER_OK is returned
 * @retval INVERTER_OK on success, error code otherwise
 */
Inverter_Status_t Inverter_Read(InverterData_t *data);

/**
 * @brief  Write a raw value to the inverter's control register (FC06).
 *         See INVERTER_REG_CONTROL in inverter.c — adjust it (and the
 *         accepted values) to match your inverter's actual protocol.
 * @retval INVERTER_OK on success, error code otherwise
 */
Inverter_Status_t Inverter_WriteControlRegister(uint16_t value);

#ifdef __cplusplus
}
#endif



#endif /* INVERTER_H */
