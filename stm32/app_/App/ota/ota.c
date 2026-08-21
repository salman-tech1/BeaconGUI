/*
 * ota.c
 */


#include "stm32h7xx_hal.h"
#include <stddef.h>   /* offsetof */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "iwdg.h"
#include "ota.h"
#include "link.h"
#include "log.h"
#include "version.h"   /* FW_VERSION_MAJOR/MINOR/PATCH */


/* ============================================================
   CRC-32 (IEEE 802.3 / zlib)
   Reflected polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
   ============================================================ */
static uint32_t ota_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0U; b < 8U; b++)
        {
            if (crc & 1U)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ============================================================
   Metadata Operations
   ============================================================ */
static void metadata_reset_defaults(OTA_Metadata_t *m)
{
    memset(m, 0x00, sizeof(OTA_Metadata_t));
    m->magic            = OTA_METADATA_MAGIC;
    m->metadata_version = 1U;
    m->active_slot      = SLOT_A;
    m->boot_counter     = 0U;
    m->confirmed        = 0U;
    m->slot[0].state    = SLOT_STATE_EMPTY;
    m->slot[1].state    = SLOT_STATE_EMPTY;
    m->struct_crc32     = 0U;
}

static bool metadata_is_valid(const OTA_Metadata_t *m)
{
    if (m->magic != OTA_METADATA_MAGIC) return false;
    return (ota_crc32((const uint8_t *)m, offsetof(OTA_Metadata_t, struct_crc32)) == m->struct_crc32);
}

static void metadata_read(OTA_Metadata_t *out)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)OTA_METADATA_BASE, sizeof(OTA_Metadata_t));
    memcpy(out, (const void *)OTA_METADATA_BASE, sizeof(OTA_Metadata_t));
}

/* forward declaration — defined further down, but metadata_write() needs it */
static FlashInternal_Status_t flash_internal_write_metadata(const OTA_Metadata_t *meta);



	static FlashInternal_Status_t metadata_write(OTA_Metadata_t *m)
	{
	    m->struct_crc32 = ota_crc32((const uint8_t *)m, offsetof(OTA_Metadata_t, struct_crc32));
	    return flash_internal_write_metadata(m);
	}


/* ============================================================
   Internal Flash Operations
   ============================================================ */

static FlashInternal_Status_t flash_internal_erase_sector(uint32_t addr)
{
    if (addr < SLOT_A_BASE || addr >= (OTA_METADATA_BASE + 128U * 1024U))
    {
        return FLASH_INTERNAL_ADDR_ERR;
    }
    if ((addr % (128U * 1024U)) != 0U)
    {
        return FLASH_INTERNAL_ADDR_ERR;
    }

    uint32_t sector = 0;
    uint32_t bank   = FLASH_BANK_1;

    if (addr >= SLOT_B_BASE)
    {
        bank = FLASH_BANK_2;
        sector = (addr - SLOT_B_BASE) / (128U * 1024U);
    }
    else
    {
        bank = FLASH_BANK_1;
        sector = (addr - SLOT_A_BASE) / (128U * 1024U);
        sector += 1U;  /* Sector 0 = bootloader */
    }

    FLASH_EraseInitTypeDef erase_init = {0};
    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks        = bank;
    erase_init.Sector       = sector;
    erase_init.NbSectors    = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t sector_error = 0U;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();

    if (status != HAL_OK || sector_error != 0xFFFFFFFFU)
    {
        return FLASH_INTERNAL_ERROR;
    }

    return FLASH_INTERNAL_OK;
}

static FlashInternal_Status_t flash_internal_write(uint32_t dest_addr, const uint8_t *src, uint32_t size)
{
    if (dest_addr < SLOT_A_BASE || (dest_addr + size) > (OTA_METADATA_BASE + 128U * 1024U))
    {
        return FLASH_INTERNAL_ADDR_ERR;
    }
    if ((dest_addr % 32U) != 0U || (size % 32U) != 0U)
    {
        return FLASH_INTERNAL_ADDR_ERR;
    }

    uint8_t write_buf[32] __attribute__((aligned(32)));
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < size; i += 32U)
    {
        memcpy(write_buf, src + i, 32U);
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                                   dest_addr + i,
                                   (uint32_t)write_buf);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return FLASH_INTERNAL_ERROR;
        }
    }

    HAL_FLASH_Lock();
    return FLASH_INTERNAL_OK;
}

static FlashInternal_Status_t flash_internal_write_metadata(const OTA_Metadata_t *meta)
{
    FlashInternal_Status_t status = flash_internal_erase_sector(OTA_METADATA_BASE);
    if (status != FLASH_INTERNAL_OK)
    {
        return status;
    }
    return flash_internal_write(OTA_METADATA_BASE,
                               (const uint8_t *)meta,
                               sizeof(OTA_Metadata_t));
}

/* ============================================================
   Running Slot Detection
   ============================================================ */
static OTA_Slot_t ota_get_running_slot(void)
{
    uint32_t vtor = SCB->VTOR;

    if ((vtor >= SLOT_A_BASE) && (vtor < (SLOT_A_BASE + SLOT_A_SIZE)))
    {
        return SLOT_A;
    }
    else if ((vtor >= SLOT_B_BASE) && (vtor < (SLOT_B_BASE + SLOT_B_SIZE)))
    {
        return SLOT_B;
    }
    else
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "VTOR 0x%08lX matches neither slot — assuming A",
                   (unsigned long)vtor);
        return SLOT_A;
    }
}

/* ============================================================
   Confirm / Rollback
   ============================================================ */

void app_confirm_ota_slot(void)
{
    OTA_Metadata_t meta;
    metadata_read(&meta);

    if (!metadata_is_valid(&meta))
    {
        return;   /* factory / blank boot — nothing to confirm */
    }

    if (meta.confirmed == 0U)
    {
        uint8_t slot = (uint8_t)meta.active_slot;   /* never hardcode A or B here */

        Log_Printf(LOG_LEVEL_INFO, "OTA", "Running from Slot %c — confirming...",
                   (slot == SLOT_A) ? 'A' : 'B');

        meta.confirmed         = 1U;
        meta.boot_counter      = 0U;
        meta.slot[slot].state  = SLOT_STATE_CONFIRMED;

        if (metadata_write(&meta) == FLASH_INTERNAL_OK)
        {
            Log_Printf(LOG_LEVEL_INFO, "OTA", "Slot %c confirmed. Rollback disarmed.",
                       (slot == SLOT_A) ? 'A' : 'B');
        }
        else
        {
            Log_Printf(LOG_LEVEL_ERROR, "OTA", "Failed to write confirmation metadata");
        }
    }
}

/* ============================================================
   OTA Receiver State Machine
   ============================================================ */

static OTA_RX_State_t s_state          = OTA_RX_IDLE;
static uint32_t       s_expected_size  = 0;
static uint32_t       s_bytes_written  = 0;
static uint32_t       s_write_addr     = 0;

static uint8_t s_target_fw_major = 0;
static uint8_t s_target_fw_minor = 0;
static uint8_t s_target_fw_patch = 0;

static OTA_Slot_t     s_target_slot   = SLOT_B;
static uint32_t       s_target_base   = 0;
static uint32_t       s_target_size   = 0;

static uint32_t s_last_activity_tick = 0;

void ota_receiver_init(void)
{
    s_state              = OTA_RX_IDLE;
    s_bytes_written       = 0;
    s_last_activity_tick = HAL_GetTick();
    Log_Printf(LOG_LEVEL_INFO, "MAIN", "Metadata struct size: %u bytes", (unsigned)sizeof(OTA_Metadata_t));
}

void ota_receiver_on_start(const uint8_t *payload, uint16_t len, uint16_t seq)
{
    (void)seq;

    /* Accept a fresh START from IDLE or a previously-failed ERROR state —
     * but never interrupt a transfer already in progress. Rejecting ERROR
     * here would permanently block all future OTA attempts after a single
     * failure until the device is power-cycled. */
    if (s_state != OTA_RX_IDLE && s_state != OTA_RX_ERROR)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "START while RECEIVING (state=%d) — rejected", s_state);
        return;
    }

    if (len < 7U)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "Invalid START payload (need 7 bytes, got %u)", (unsigned)len);
        return;
    }

    memcpy(&s_expected_size, payload, sizeof(uint32_t));
    s_target_fw_major = payload[4];
    s_target_fw_minor = payload[5];
    s_target_fw_patch = payload[6];

    /* Target whichever slot is NOT currently running */
    OTA_Slot_t running_slot = ota_get_running_slot();
    s_target_slot = (running_slot == SLOT_A) ? SLOT_B : SLOT_A;
    s_target_base = (s_target_slot == SLOT_A) ? SLOT_A_BASE : SLOT_B_BASE;
    s_target_size = (s_target_slot == SLOT_A) ? SLOT_A_SIZE : SLOT_B_SIZE;

    Log_Printf(LOG_LEVEL_INFO, "OTA",
               "Running from Slot %c — targeting Slot %c, expecting %lu bytes (fw v%u.%u.%u)",
               (running_slot == SLOT_A) ? 'A' : 'B',
               (s_target_slot == SLOT_A) ? 'A' : 'B',
               (unsigned long)s_expected_size,
               s_target_fw_major, s_target_fw_minor, s_target_fw_patch);

    if (s_expected_size > s_target_size)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "Firmware too large: %lu bytes (max %lu)",
                   (unsigned long)s_expected_size, (unsigned long)s_target_size);
        s_state = OTA_RX_ERROR;
        return;
    }

    s_bytes_written = 0;
    s_write_addr    = s_target_base;
    s_state         = OTA_RX_RECEIVING;
    s_last_activity_tick = HAL_GetTick();

    Log_Printf(LOG_LEVEL_INFO, "OTA", "Erasing Slot %c (%lu sectors)...",
               (s_target_slot == SLOT_A) ? 'A' : 'B',
               (unsigned long)(s_target_size / (128U * 1024U)));

    for (uint32_t i = 0; i < (s_target_size / (128U * 1024U)); i++)
    {
    	 HAL_IWDG_Refresh(&hiwdg);
        uint32_t sector_addr = s_target_base + (i * 128U * 1024U);
        Log_Printf(LOG_LEVEL_INFO, "OTA", "  Sector %lu/%lu at 0x%08lX...",
                       (unsigned long)(i + 1),
                       (unsigned long)(s_target_size / (128U * 1024U)),
                       (unsigned long)sector_addr);

        if (flash_internal_erase_sector(sector_addr) != FLASH_INTERNAL_OK)
        {
            Log_Printf(LOG_LEVEL_ERROR, "OTA", "Erase failed at 0x%08lX",
                       (unsigned long)sector_addr);
            s_state = OTA_RX_ERROR;
            return;
        }
    }

    Log_Printf(LOG_LEVEL_INFO, "OTA", "Slot %c erased — sending OTA_READY",
               (s_target_slot == SLOT_A) ? 'A' : 'B');

    link_Send(CMD_OTA_READY, seq, 0, NULL, 0);
}

void ota_receiver_on_data(const uint8_t *payload, uint16_t len, uint16_t seq)
{
    (void)seq;

    if (s_state != OTA_RX_RECEIVING)
    {
        Log_Printf(LOG_LEVEL_WARN, "OTA", "DATA but not RECEIVING — ignored");
        return;
    }

    s_last_activity_tick = HAL_GetTick();

    if (s_bytes_written + len > s_expected_size)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "DATA overflow — received more than expected");
        s_state = OTA_RX_ERROR;
        return;
    }

    uint16_t write_len = len;
    static uint8_t pad_buf[32];
    uint32_t bytes_before = s_bytes_written;   /* for progress-log throttling below */

    if ((len % 32U) != 0U)
    {
        bool is_final_chunk = (s_bytes_written + len == s_expected_size);

        if (!is_final_chunk)
        {
            /* Only the FINAL chunk of the whole image may be non-32-byte-
             * aligned. If an intermediate chunk were ever non-aligned,
             * applying the padding logic below would advance s_write_addr
             * past the true logical byte position, silently shifting every
             * subsequent write out of sync with the image — corruption
             * that would only surface much later as a CRC mismatch, after
             * the entire slot has already been overwritten with garbage. */
            Log_Printf(LOG_LEVEL_ERROR, "OTA",
                       "Non-32-byte-aligned chunk (%u bytes) mid-transfer (at %lu/%lu). "
                       "Only FINAL chunk may be unaligned. Aborting.",
                       (unsigned)len,
                       (unsigned long)s_bytes_written, (unsigned long)s_expected_size);
            s_state = OTA_RX_ERROR;
            return;
        }

        memset(pad_buf, 0xFF, sizeof(pad_buf));
        memcpy(pad_buf, payload + (len - (len % 32U)), len % 32U);

        if (len >= 32U)
        {
            if (flash_internal_write(s_write_addr, payload, len - (len % 32U)) != FLASH_INTERNAL_OK)
            {
                Log_Printf(LOG_LEVEL_ERROR, "OTA", "Flash write failed at 0x%08lX",
                           (unsigned long)s_write_addr);
                s_state = OTA_RX_ERROR;
                return;
            }
            s_write_addr += (len - (len % 32U));
        }

        if (flash_internal_write(s_write_addr, pad_buf, 32U) != FLASH_INTERNAL_OK)
        {
            Log_Printf(LOG_LEVEL_ERROR, "OTA", "Flash write failed at 0x%08lX",
                       (unsigned long)s_write_addr);
            s_state = OTA_RX_ERROR;
            return;
        }
        s_write_addr += 32U;
    }
    else
    {
        if (flash_internal_write(s_write_addr, payload, write_len) != FLASH_INTERNAL_OK)
        {
            Log_Printf(LOG_LEVEL_ERROR, "OTA", "Flash write failed at 0x%08lX",
                       (unsigned long)s_write_addr);
            s_state = OTA_RX_ERROR;
            return;
        }
        s_write_addr += write_len;
    }
    HAL_IWDG_Refresh(&hiwdg);
    s_bytes_written += len;
    link_Send(CMD_OTA_ACK, seq, 0, NULL, 0);

    /* Log roughly every 10KB. (bytes_written % 10000 == 0) almost never
     * fires exactly with 512-byte chunks — compare 10KB "buckets" crossed
     * instead, so this actually triggers as intended. */
    if ((bytes_before / 10000U) != (s_bytes_written / 10000U) ||
        s_bytes_written == s_expected_size)
    {
        Log_Printf(LOG_LEVEL_INFO, "OTA", "Progress: %lu/%lu (%lu%%)",
                   (unsigned long)s_bytes_written,
                   (unsigned long)s_expected_size,
                   (unsigned long)((s_bytes_written * 100U) / s_expected_size));
    }
}

void ota_receiver_on_end(const uint8_t *payload, uint16_t len, uint16_t seq)
{
    (void)seq;

    if (s_state != OTA_RX_RECEIVING)
    {
        Log_Printf(LOG_LEVEL_WARN, "OTA", "END but not RECEIVING — ignored");
        return;
    }

    if (s_bytes_written != s_expected_size)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "END size mismatch: got %lu, expected %lu",
                   (unsigned long)s_bytes_written,
                   (unsigned long)s_expected_size);
        s_state = OTA_RX_ERROR;
        return;
    }

    if (len < sizeof(uint32_t))
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "END missing CRC — rejecting");
        s_state = OTA_RX_ERROR;
        return;
    }

    uint32_t golden_crc;
    memcpy(&golden_crc, payload, sizeof(uint32_t));

    uint32_t cache_size = (s_expected_size + 31U) & ~31U;
    HAL_IWDG_Refresh(&hiwdg);
    SCB_InvalidateDCache_by_Addr((uint32_t *)s_target_base, (int32_t)cache_size);

    uint32_t image_crc = ota_crc32((const uint8_t *)s_target_base, s_expected_size);

    Log_Printf(LOG_LEVEL_INFO, "OTA", "CRC: golden=0x%08lX computed=0x%08lX %s",
               (unsigned long)golden_crc, (unsigned long)image_crc,
               (golden_crc == image_crc) ? "PASS" : "FAIL");

    if (image_crc != golden_crc)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "CRC FAIL — staying on Slot %c",
                   (s_target_slot == SLOT_A) ? 'B' : 'A');
        s_state = OTA_RX_ERROR;
        return;
    }

    Log_Printf(LOG_LEVEL_INFO, "OTA", "Writing metadata...");

    OTA_Metadata_t meta;
    metadata_read(&meta);
    if (!metadata_is_valid(&meta))
    {
        metadata_reset_defaults(&meta);
    }

    meta.slot[s_target_slot].fw_version =
        ((uint32_t)s_target_fw_major << 16) |
        ((uint32_t)s_target_fw_minor << 8)  |
        ((uint32_t)s_target_fw_patch);
    meta.slot[s_target_slot].image_size = s_expected_size;
    meta.slot[s_target_slot].crc32      = image_crc;
    memset(meta.slot[s_target_slot].sha256, 0, sizeof(meta.slot[s_target_slot].sha256));
    meta.slot[s_target_slot].state      = SLOT_STATE_COMPLETE;

    meta.confirmed    = 0U;
    meta.boot_counter = 0U;

    if (metadata_write(&meta) != FLASH_INTERNAL_OK)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "Metadata write FAILED — staying on Slot %c",
                   (s_target_slot == SLOT_A) ? 'B' : 'A');
        s_state = OTA_RX_ERROR;
        return;
    }

    s_state = OTA_RX_COMPLETE;
    Log_Printf(LOG_LEVEL_INFO, "OTA", "Slot %c COMPLETE — Rebooting...",
               (s_target_slot == SLOT_A) ? 'A' : 'B');

    HAL_Delay(50);
    NVIC_SystemReset();
}

OTA_RX_State_t ota_receiver_get_state(void)
{
    return s_state;
}

uint8_t ota_receiver_get_target_slot(void)
{
    OTA_Slot_t running_slot = ota_get_running_slot();
    OTA_Slot_t target_slot  = (running_slot == SLOT_A) ? SLOT_B : SLOT_A;
    return (target_slot == SLOT_A) ? 0U : 1U;
}

void ota_receiver_check_timeout(void)
{
    if (s_state != OTA_RX_RECEIVING)
    {
        return;
    }

    uint32_t elapsed = HAL_GetTick() - s_last_activity_tick;

    if (elapsed > OTA_RX_TIMEOUT_MS)
    {
        Log_Printf(LOG_LEVEL_ERROR, "OTA", "TIMEOUT — no data for %lu ms, abandoning (no metadata written)",
                   (unsigned long)elapsed);
        s_state         = OTA_RX_IDLE;
        s_bytes_written = 0;
    }
}
