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
#include <stdbool.h>
#include "frame.h"

typedef struct {
    uint32_t rx_queue_full;       /* validated packets dropped — app queue was full */
    uint32_t rx_uart_errors;      /* transport errors that triggered Com_Recover() */
    uint32_t tx_packets_sent;     /* frames handed to Com_Send() successfully */
    uint32_t tx_send_failures;    /* link_Send() calls that failed */
} link_Stats_t;

/* Time info received from ESP32 */
typedef struct {
    uint32_t unix_timestamp;      /* Unix epoch in seconds */
    bool     synced;              /* true if NTP sync was successful */
    bool     valid;               /* true if we've received at least one time sync */
} link_TimeInfo_t;

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

/**
 * @brief  Send CMD_WIFI_STATUS_REQ to ESP32 to request current WiFi status.
 * @param  seq  Sequence number for the request.
 */
void link_RequestWifiStatus(uint16_t seq);

/**
 * @brief  Send CMD_TIME_REQ to ESP32 to request current time.
 * @param  seq  Sequence number for the request.
 */
void link_RequestTime(uint16_t seq);

/**
 * @brief  Get the last received time info from ESP32.
 * @param  out_info  Pointer to store the time info.
 * @return true if valid time has been received, false otherwise.
 */
bool link_GetTimeInfo(link_TimeInfo_t *out_info);

/**
 * @brief  Check for OTA timeout (call periodically from a task)
 * @note   Safe to call even when no OTA is in progress.
 */
void link_CheckOtaTimeout(void);

#ifdef __cplusplus
}
#endif

#endif /* LINK_H_ */
