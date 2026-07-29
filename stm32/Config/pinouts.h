/*
 * pinouts.h
 *
 *  Created on: Jul 27, 2026
 *      Author: Muhmmad Salman
 */

#ifndef PINOUTS_H_
#define PINOUTS_H_

#include "stm32h7xx_hal.h"

// Modbus MAX485 pinouts
// USART3_TX : PC10 (AF7)   USART3_RX : PC11 (AF7)

#define MODBUS_UART_INSTANCE         USART3
#define MODBUS_UART_TX_PIN           GPIO_PIN_10
#define MODBUS_UART_RX_PIN           GPIO_PIN_11
#define MODBUS_UART_GPIO_PORT        GPIOC
/* RS485 direction pin — on this PCB the DE and /RE pins of the MAX485
 * are shorted together and driven by a single GPIO:
 *   HIGH -> DE=1, /RE=1 (driver on)  -> TRANSMIT
 *   LOW  -> DE=0, /RE=0 (receiver on) -> RECEIVE                       */
#define MODBUS_DIR_GPIO_PORT          GPIOB
#define MODBUS_DIR_GPIO_PIN           GPIO_PIN_8




#endif /* PINOUTS_H_ */
