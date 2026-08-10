/*
 * link.h  (STM32 side)
 *
 *  Created on: Jul 29, 2026
 *      Author: Muhmmad Salman
 */
#ifndef LINK_H_
#define LINK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "frame.h"

typedef struct {
    uint32_t rx_queue_full;       /* validated packets dropped — app queue was full */
    uint32_t rx_uart_errors;      /* transport errors that triggered Com_Recover() */
    uint32_t tx_packets_sent;     /* frames handed to Com_Send() successfully */
    uint32_t tx_send_failures;    /* link_Send() calls that failed */
} link_Stats_t;

extern link_Stats_t link_stats;

/**
 * @brief  Create the queue/mutex, wire Com + Link together, start the
 *         rx and app tasks.
 * @note   Call once from main() AFTER all HAL peripheral inits and
 *         BEFORE vTaskStartScheduler().
 */
void link_Init(void);

/**
 * @brief  Thread-safe wrapper around frame_Send(). Any task that wants to
 *         transmit a packet should call THIS, not frame_Send() directly.
 */
frame_Status_t link_Send(uint8_t cmd, uint16_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_length);

#ifdef __cplusplus
}
#endif

#endif /* LINK_H_ */
