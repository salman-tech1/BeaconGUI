/*
 * inverter.c
 *
 */

#include "inverter.h"
#include "modbus_rtu.h"
#include <stddef.h>



#define INVERTER_REG_BLOCK_START      0x0000U   /* first holding register of the telemetry block */
#define INVERTER_REG_BLOCK_COUNT      6U        /* registers read in one Modbus transaction (offsets 0..5) */

/* Offsets within the block read by Inverter_Read() */
#define INVERTER_OFF_AC_VOLTAGE       0U   /* raw x 0.1  -> V  */
#define INVERTER_OFF_AC_CURRENT       1U   /* raw x 0.1  -> A  */
#define INVERTER_OFF_AC_FREQUENCY     2U   /* raw x 0.01 -> Hz */
#define INVERTER_OFF_OUTPUT_POWER     3U   /* raw x 1    -> W  */
#define INVERTER_OFF_DC_VOLTAGE       4U   /* raw x 0.1  -> V  */
#define INVERTER_OFF_STATUS_WORD      5U   /* raw bitfield, no scaling */

/* Separate single-register control point (on/off, mode, setpoint...).
 * Adjust the address and the meaning of `value` in
 * Inverter_WriteControlRegister() to match your inverter. */
#define INVERTER_REG_CONTROL          0x0010U

typedef enum {
    INVERTER_STATE_UNINIT = 0,
    INVERTER_STATE_READY,
    INVERTER_STATE_ERROR,
} Inverter_State_t;

static uint8_t          s_inverter_slave_addr = 1U;
static Inverter_State_t s_inverter_state      = INVERTER_STATE_UNINIT;

/* Lower level helpers — private to this file */
static Inverter_Status_t Modbus_ToInverterStatus(Modbus_Status_t modbus_status);
static void              Inverter_ConvertRaw(const uint16_t *raw, InverterData_t *data);


/* Translate the transport-layer (Modbus) status into this driver's own
 * status codes, the same way temp.c keeps I2C/HAL errors behind
 * Temp_Status_t so callers never need to know what's underneath. */
static Inverter_Status_t Modbus_ToInverterStatus(Modbus_Status_t modbus_status)
{
    switch (modbus_status) {
        case MODBUS_OK:        return INVERTER_OK;
        case MODBUS_TIMEOUT:   return INVERTER_TIMEOUT;
        case MODBUS_CRC_ERROR: return INVERTER_CRC_ERROR;
        case MODBUS_NOT_READY: return INVERTER_NOT_READY;
        default:                return INVERTER_ERROR;   /* bad slave/function/exception/etc. */
    }
}

/* raw[] holds INVERTER_REG_BLOCK_COUNT registers, already read starting
 * at INVERTER_REG_BLOCK_START. */
static void Inverter_ConvertRaw(const uint16_t *raw, InverterData_t *data)
{
    data->ac_voltage_v    = (float)raw[INVERTER_OFF_AC_VOLTAGE]   * 0.1f;
    data->ac_current_a    = (float)raw[INVERTER_OFF_AC_CURRENT]   * 0.1f;
    data->ac_frequency_hz = (float)raw[INVERTER_OFF_AC_FREQUENCY] * 0.01f;
    data->output_power_w  = (float)raw[INVERTER_OFF_OUTPUT_POWER];
    data->dc_voltage_v    = (float)raw[INVERTER_OFF_DC_VOLTAGE]   * 0.1f;
    data->status_word     = raw[INVERTER_OFF_STATUS_WORD];
}




Inverter_Status_t Inverter_Init(uint8_t slave_addr, uint32_t baudrate)
{
    s_inverter_slave_addr = slave_addr;

    Modbus_Status_t status = Modbus_RTU_Init(baudrate);
    if (status != MODBUS_OK) {
        s_inverter_state = INVERTER_STATE_ERROR;
        return Modbus_ToInverterStatus(status);
    }

    s_inverter_state = INVERTER_STATE_READY;
    return INVERTER_OK;
}

Inverter_Status_t Inverter_Read(InverterData_t *data)
{
    if (data == NULL) {
        return INVERTER_ERROR;
    }
    if (s_inverter_state != INVERTER_STATE_READY) {
        return INVERTER_NOT_READY;
    }

    // raw data structure to read
    uint16_t raw[INVERTER_REG_BLOCK_COUNT];

    Modbus_Status_t status = Modbus_RTU_ReadHoldingRegisters(
                                  s_inverter_slave_addr,
                                  INVERTER_REG_BLOCK_START,
                                  INVERTER_REG_BLOCK_COUNT,
                                  raw);
    if (status != MODBUS_OK) {
        return Modbus_ToInverterStatus(status);
    }

    // convert the raw values to float to display
    Inverter_ConvertRaw(raw, data);
    return INVERTER_OK;
}

Inverter_Status_t Inverter_WriteControlRegister(uint16_t value)
{
    if (s_inverter_state != INVERTER_STATE_READY) {
        return INVERTER_NOT_READY;
    }

    Modbus_Status_t status = Modbus_RTU_WriteSingleRegister(
                                  s_inverter_slave_addr,
                                  INVERTER_REG_CONTROL,
                                  value);
    return Modbus_ToInverterStatus(status);
}
