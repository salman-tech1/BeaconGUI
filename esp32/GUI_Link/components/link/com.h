/*
 * com.h 
 *  Author: Muhmmad Salman
 */

#ifndef COM_H
#define COM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    COM_OK      =  0,   /**< Operation successful */
    COM_ERROR   = -1,   /**< Generic error (bad args, not initialized) */
    COM_TIMEOUT = -2,   /**< Transmit did not complete in time */
    COM_BUSY    = -3,   /**< Transport busy */
} Com_Status_t;

typedef enum {
    COM_EVENT_RX_DATA, /**< New bytes are sitting in the transport buffer —
                             call Com_ReadAvailable() to pull them out. */
    COM_EVENT_ERROR,   /**< Transport hit a hardware error — call
                             Com_Recover() before anything else. */
} Com_Event_t;

/**
 * @brief  Event notification.
 * @note   Unlike the STM32 side, this is NOT fired from a real ISR —
 *         the ESP-IDF UART driver doesn't expose a user-hookable ISR.
 *         It's invoked from whichever task is currently inside
 *         Com_ReadAvailable() (in practice, link_rx_task). Keep
 *         implementations minimal anyway so the two sides stay easy
 *         to compare.
 */
typedef void (*Com_EventCallback_t)(Com_Event_t event);

/**
 * @brief  Bring up the transport (UART peripheral + driver ring buffers).
 */
Com_Status_t Com_Init(void);

/**
 * @brief  Register the callback invoked on RX/error events.
 * @note   Call once, before Com_Init(), same ordering rule as the STM32 side.
 */
void Com_RegisterEventCallback(Com_EventCallback_t callback);

/**
 * @brief  Transmit a raw byte buffer (blocking, with timeout).
 * @note   NOT reentrant at this layer — the ESP-IDF driver serializes
 *         uart_write_bytes() internally, but if more than one task can
 *         call this, serialize access yourself (link_Send() does this).
 */
Com_Status_t Com_Send(const uint8_t *data, uint16_t length);

/**
 * @brief  Pull whatever new bytes have arrived since the last call.
 * @param  dst          Destination buffer
 * @param  max_length   Capacity of dst
 * @retval Number of bytes copied into dst (0 if nothing new).
 * @note   Call this in a loop until it returns 0 — a single call only
 *         returns one uart_read_bytes() burst.
 */
uint16_t Com_ReadAvailable(uint8_t *dst, uint16_t max_length);

/**
 * @brief  Recover the transport after a COM_EVENT_ERROR notification.
 * @note   Flushes the driver's RX ring buffer and drops any queued
 *         UART events. Safe to call from link_rx_task's own context.
 */
Com_Status_t Com_Recover(void);

#ifdef __cplusplus
}
#endif

#endif /* COM_H */