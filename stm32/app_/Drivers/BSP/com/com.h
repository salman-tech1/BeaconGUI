/*
 * com.h
 * Author: Muhmmad Salman
 * This layer depends only on HAL or it's lower layer
 *  Created on: Jul 21, 2026
 */

#ifndef COM_H
#define COM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    COM_OK      =  0,   /**< Operation successful */
    COM_ERROR   = -1,   /**< Generic error (bad args, not initialized) */
    COM_TIMEOUT = -2,   /**< Transmit/receive did not complete in time */
    COM_BUSY    = -3,   /**< Transport busy */
} Com_Status_t;

typedef enum {
    COM_EVENT_RX_DATA, /**< New bytes are sitting in the transport buffer —
                             call Com_ReadAvailable() to pull them out. */
    COM_EVENT_ERROR,   /**< Transport hit a hardware error — call
                             Com_Recover() from TASK context (never from
                             inside this callback) before anything else. */
} Com_Event_t;

/**
 * @brief  Event notification, fired directly from ISR context.
 * @note   Implementations MUST do the absolute minimum — set a flag or
 *         notify a task — exactly like an EXTI callback. Never call
 *         Com_ReadAvailable() or Com_Recover() from inside this callback;
 *         do that from the task it wakes up.
 */
typedef void (*Com_EventCallback_t)(Com_Event_t event);

/**
 * @brief  Bring up the transport (GPIO/peripheral/DMA circular reception).
 */
Com_Status_t Com_Init(void);

/**
 * @brief  Register the callback invoked from ISR context on RX/error events.
 * @note   Call once, before any traffic can arrive (i.e. before/at Com_Init).
 */
void Com_RegisterEventCallback(Com_EventCallback_t callback);

/**
 * @brief  Transmit a raw byte buffer (blocking).
 * @note   NOT reentrant — internally copies into a single static, cache-
 *         aligned TX buffer. If more than one task can call this, serialize
 *         access yourself (the Link/App layers do this, see LinkService_Send).
 */
Com_Status_t Com_Send(const uint8_t *data, uint16_t length);

/**
 * @brief  Pull whatever new bytes have arrived since the last call.
 * @param  dst          Destination buffer
 * @param  max_length   Capacity of dst
 * @retval Number of bytes copied into dst (0 if nothing new).
 * @note   Call this in a loop until it returns 0 — a single call only
 *         drains one contiguous run of the internal circular buffer.
 */
uint16_t Com_ReadAvailable(uint8_t *dst, uint16_t max_length);

/**
 * @brief  Recover the transport after a COM_EVENT_ERROR notification.
 * @note   TASK CONTEXT ONLY. Aborts and restarts DMA reception.
 */
Com_Status_t Com_Recover(void);

#ifdef __cplusplus
}
#endif

#endif /* COM_H */
