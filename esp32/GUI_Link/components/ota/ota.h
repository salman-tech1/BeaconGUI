#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    OTA_MGR_OK               = 0,
    OTA_MGR_ERR_HTTP         = 1,
    OTA_MGR_ERR_VERSION      = 2,
    OTA_MGR_ERR_DOWNLOAD     = 3,
    OTA_MGR_ERR_TRANSFER     = 4,
    OTA_MGR_ERR_VERIFICATION = 5,
    OTA_MGR_ERR_SLOT_QUERY   = 6,
} OTA_MGR_Status_t;

typedef enum {
    OTA_TARGET_SLOT_A = 0,
    OTA_TARGET_SLOT_B = 1,
} OTA_TargetSlot_t;

typedef struct {
    char     version[16];
    char     download_url[256];
    uint32_t firmware_size;
} OTA_FirmwareInfo_t;

typedef struct {
    OTA_TargetSlot_t target_slot;
    uint8_t          running_major;
    uint8_t          running_minor;
    uint8_t          running_patch;
} OTA_SlotQueryResult_t;

/**
 * @brief  Initialize the OTA Manager and start its internal task.
 * @note   The task queries the STM32, checks GitHub, and runs the transfer
 *         automatically. It deletes itself when finished (success or fail).
 */
void ota_init(void);

/**
 * @brief  Called by link_app_task when CMD_SLOT_INFO_RESP arrives.
 */
void ota_notify_slot_info_resp(const uint8_t *payload, uint16_t len);

/**
 * @brief  Called by link_app_task when CMD_OTA_ACK arrives.
 */
void ota_notify_ota_ack(void);

void ota_notify_ota_ready(void);

#endif /* OTA_H */