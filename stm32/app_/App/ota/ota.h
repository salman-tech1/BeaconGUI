/*
 * ota.h
 */

#ifndef OTA_H
#define OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ── Flash Memory Map ────────────────────────────────────────────────────── */

/* SLOT_A / SLOT_B are an enum, not #defines — never reintroduce plain
 * #defines alongside this. The two conflict and produce a preprocessor-
 * mangled syntax error (e.g. "typedef enum { 0U = 0, 1U, }"). Bit a sibling
 * project once already. */
typedef enum
{
    SLOT_A = 0,
    SLOT_B = 1,
} OTA_Slot_t;

#define SLOT_A_BASE             0x08020000UL
#define SLOT_B_BASE             0x08100000UL
#define SLOT_A_SIZE             (896U * 1024U)
#define SLOT_B_SIZE             (896U * 1024U)

#define OTA_METADATA_BASE       0x081E0000UL
#define OTA_METADATA_MAGIC      0xDEADBEEFUL
#define OTA_MAX_BOOT_TRIES      3U

/* Max silence (ms) allowed between OTA packets once RECEIVING has started. */
#define OTA_RX_TIMEOUT_MS       60000U

/* ── Slot State Machine ─────────────────────────────────────────────────── */

typedef enum
{
    SLOT_STATE_EMPTY       = 0x00U,
    SLOT_STATE_DOWNLOADING = 0x01U,
    SLOT_STATE_COMPLETE    = 0x02U,
    SLOT_STATE_CONFIRMED   = 0x03U,
    SLOT_STATE_INVALID     = 0xFFU
} OTA_SlotState_t;

/* ── Per-Slot Info (MUST be defined before OTA_Metadata_t uses it) ───────── */

typedef struct __attribute__((packed))
{
    uint32_t fw_version;
    uint32_t image_size;
    uint32_t crc32;
    uint8_t  sha256[32];
    uint8_t  state;        /* OTA_SlotState_t */
    uint8_t  reserved[3];  /* pad to 4-byte boundary */
} OTA_SlotInfo_t;

/* ── OTA Metadata Struct (128 bytes, 32-byte aligned for H743 flash) ─────── */

typedef struct __attribute__((packed))
{
    uint32_t       magic;
    uint32_t       metadata_version;
    uint32_t       active_slot;
    uint32_t       boot_counter;
    uint32_t       confirmed;
    OTA_SlotInfo_t slot[2];
    uint8_t        reserved[8];     /* pad to 128 bytes */
    uint32_t       struct_crc32;
} OTA_Metadata_t;

_Static_assert( (sizeof(OTA_Metadata_t) == 128U), "OTA_Metadata_t must be exactly 128 bytes");

/* ── OTA Receive State ────────────────────────────────────────────────────── */

typedef enum
{
    OTA_RX_IDLE        = 0x00,
    OTA_RX_RECEIVING   = 0x01,
    OTA_RX_COMPLETE    = 0x02,
    OTA_RX_ERROR       = 0x03,
} OTA_RX_State_t;

/* ── Flash Internal Status ────────────────────────────────────────────────── */

typedef enum
{
    FLASH_INTERNAL_OK       = 0,
    FLASH_INTERNAL_ERROR    = 1,
    FLASH_INTERNAL_ADDR_ERR = 2,
} FlashInternal_Status_t;

/* ── Public API ────────────────────────────────────────────────────────── */

/*
 * app_confirm_ota_slot()
 * Call once at startup, after critical subsystems are confirmed healthy
 * (and the watchdog, if any, is being fed). Reads metadata; if the running
 * slot (meta.active_slot) is not yet confirmed, marks it confirmed=1,
 * boot_counter=0, slot state=CONFIRMED, and writes metadata back.
 * No-op on a normal already-confirmed boot or invalid/blank metadata.
 */
void     app_confirm_ota_slot(void);

void     ota_receiver_init(void);
void     ota_receiver_on_start(const uint8_t *payload, uint16_t len, uint16_t seq);
void     ota_receiver_on_data (const uint8_t *payload, uint16_t len, uint16_t seq);
void     ota_receiver_on_end  (const uint8_t *payload, uint16_t len, uint16_t seq);
OTA_RX_State_t ota_receiver_get_state(void);
uint8_t  ota_receiver_get_target_slot(void);
void     ota_receiver_check_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
