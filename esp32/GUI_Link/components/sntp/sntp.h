/*
 * sntp_client.h  (ESP32 side)
 *
 * SNTP time synchronization client.
 * Syncs with NTP servers and stores the result in system_info.
 * The publisher_task handles sending to STM32.
 */

#ifndef SNTP_CLIENT_H
#define SNTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#define SNTP_SERVER_PRIMARY    "pool.ntp.org"
#define SNTP_SERVER_SECONDARY  "time.google.com"
#define SNTP_SYNC_INTERVAL_MS  (60U * 1000U)    /* update system_info every 60s */
#define SNTP_SYNC_WAIT_MS      (10U * 1000U)    /* wait up to 10s for first sync */
#define SNTP_POLL_PERIOD_MS    500U              /* check status every 500ms */

/**
 * @brief  Initialize SNTP client and create the background task.
 * @note   Call once from app_main(). The task waits for WiFi internally.
 */
void sntp_init_(void);

/**
 * @brief  Check if at least one successful NTP sync has completed.
 */
bool sntp_is_synced(void);

/**
 * @brief  Get current Unix epoch in seconds.
 * @return Unix timestamp, or 0 if not yet synced.
 */
uint32_t sntp_get_timestamp(void);

#endif /* SNTP_CLIENT_H */