/*
 * modbus_rtu.h
 *
 *
 *
 *  This is a fully blocking/polled driver — no DMA, no interrupts, no
 *  RTOS primitives inside.
 *
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    MODBUS_OK           =  0,   /**< Response received and validated */
    MODBUS_ERROR        = -1,   /**< Generic / bus error */
    MODBUS_TIMEOUT      = -2,   /**< No response within the timeout */
    MODBUS_CRC_ERROR    = -3,   /**< Response CRC16 mismatch */
    MODBUS_BAD_SLAVE    = -4,   /**< Response came from an unexpected slave address */
    MODBUS_BAD_FUNCTION = -5,   /**< Response function code didn't match the request */
    MODBUS_EXCEPTION    = -6,   /**< Slave returned a Modbus exception response */
    MODBUS_SHORT_FRAME  = -7,   /**< Response length inconsistent with its own header */
    MODBUS_NOT_READY    = -8,   /**< Called before Modbus_RTU_Init() succeeded */
} Modbus_Status_t;

/**
 * @brief  Bring up the RS485 bus (UART + direction GPIO) for use as a
 *         Modbus RTU master.
 * @param  baudrate  Bus baud rate (e.g. 9600)
 * @retval MODBUS_OK on success, error code otherwise
 */
Modbus_Status_t Modbus_RTU_Init(uint32_t baudrate);

/**
 * @brief  FC03 — Read Holding Registers (blocking).
 * @param  slave_addr  Slave address (1-247)
 * @param  start_reg   First register address (0-based / PDU addressing)
 * @param  qty         Number of registers to read (1-125)
 * @param  out_regs    Output buffer, must hold at least `qty` uint16_t.
 *                      Only written when MODBUS_OK is returned.
 * @retval MODBUS_OK on success, error code otherwise
 */
Modbus_Status_t Modbus_RTU_ReadHoldingRegisters(uint8_t slave_addr, uint16_t start_reg,
                                                 uint16_t qty, uint16_t *out_regs);

/**
 * @brief  FC04 — Read Input Registers (blocking).
 *         Same semantics as Modbus_RTU_ReadHoldingRegisters().
 */
Modbus_Status_t Modbus_RTU_ReadInputRegisters(uint8_t slave_addr, uint16_t start_reg,
                                               uint16_t qty, uint16_t *out_regs);

/**
 * @brief  FC06 — Write Single Register (blocking).
 * @param  slave_addr  Slave address (1-247)
 * @param  reg_addr    Register address (0-based / PDU addressing)
 * @param  value       Value to write
 * @retval MODBUS_OK on success (slave echoed the write back), error code otherwise
 */
Modbus_Status_t Modbus_RTU_WriteSingleRegister(uint8_t slave_addr, uint16_t reg_addr,
                                                uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */
