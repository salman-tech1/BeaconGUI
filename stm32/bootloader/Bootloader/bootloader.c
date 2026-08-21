/*
 * bootloader.c
 */

#include "bootloader.h"
#include "stm32h7xx_hal.h"
#include <string.h>


#define SLOT_A_BASE             0x08020000UL
#define SLOT_B_BASE             0x08100000UL
#define SLOT_A_SIZE             (896U * 1024U)
#define SLOT_B_SIZE             (896U * 1024U)

#define OTA_METADATA_BASE       0x081E0000UL


#define OTA_METADATA_MAGIC      0xDEADBEEFUL
#define OTA_MAX_BOOT_TRIES      3U


typedef enum
{
    SLOT_STATE_EMPTY       = 0x00U,
    SLOT_STATE_DOWNLOADING = 0x01U,
    SLOT_STATE_COMPLETE    = 0x02U,
    SLOT_STATE_CONFIRMED   = 0x03U,
    SLOT_STATE_INVALID     = 0xFFU
} SlotState_t;


typedef struct __attribute__((packed))
{
    uint32_t fw_version;
    uint32_t image_size;
    uint32_t crc32;
    uint8_t  sha256[32];
    uint8_t  state;
    uint8_t  reserved[3];
} SlotInfo_t;

typedef struct __attribute__((packed))
{
    uint32_t   magic;
    uint32_t   metadata_version;
    uint32_t   active_slot;
    uint32_t   boot_counter;
    uint32_t   confirmed;
    SlotInfo_t slot[2];
    uint8_t    reserved[8];
    uint32_t   struct_crc32;
} OtaMetadata_t;

_Static_assert(sizeof(OtaMetadata_t) == 128U,
               "OtaMetadata_t must be exactly 128 bytes");


typedef enum
{
    FLASH_OPS_OK      = 0,
    FLASH_OPS_ERROR   = 1,
    FLASH_OPS_ADDR_ERR = 2
} FlashOps_Status_t;


static uint32_t ota_crc32(const uint8_t *data, uint32_t len);
static uint32_t metadata_calc_crc32(const OtaMetadata_t *m);


static void     metadata_reset_defaults(OtaMetadata_t *m);
static bool     metadata_is_valid(const OtaMetadata_t *m);
static void     metadata_read(OtaMetadata_t *out);
static int      metadata_write(OtaMetadata_t *m);

/*------------------------------------------------------------------------------
 * Private Function Prototypes - Flash Operations
 *----------------------------------------------------------------------------*/
static FlashOps_Status_t flash_write(uint32_t dest_addr, const uint8_t *src, uint32_t size);
static FlashOps_Status_t flash_erase_sector(uint32_t addr);
static FlashOps_Status_t flash_write_metadata(const void *meta);

/*------------------------------------------------------------------------------
 * Private Function Prototypes - Jump Handler
 *----------------------------------------------------------------------------*/
typedef void (*AppResetHandler_t)(void);

/*------------------------------------------------------------------------------
 * Private Functions - CRC Implementation
 *----------------------------------------------------------------------------*/
static uint32_t ota_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0U; b < 8U; b++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

static uint32_t metadata_calc_crc32(const OtaMetadata_t *m)
{
    return ota_crc32((const uint8_t *)m, offsetof(OtaMetadata_t, struct_crc32));
}


static void metadata_reset_defaults(OtaMetadata_t *m)
{
    memset(m, 0x00, sizeof(OtaMetadata_t));
    m->magic            = OTA_METADATA_MAGIC;
    m->metadata_version = 1U;
    m->active_slot      = BOOTLOADER_SLOT_A;
    m->boot_counter     = 0U;
    m->confirmed        = 0U;
    m->slot[0].state    = SLOT_STATE_EMPTY;
    m->slot[1].state    = SLOT_STATE_EMPTY;
    m->struct_crc32     = 0U;
}

static bool metadata_is_valid(const OtaMetadata_t *m)
{
    if (m->magic != OTA_METADATA_MAGIC)
    {
        return false;
    }
    return (metadata_calc_crc32(m) == m->struct_crc32);
}

static void metadata_read(OtaMetadata_t *out)
{
    memcpy(out, (const void *)OTA_METADATA_BASE, sizeof(OtaMetadata_t));
}

static int metadata_write(OtaMetadata_t *m)
{
    m->struct_crc32 = metadata_calc_crc32(m);
    return (int)flash_write_metadata(m);
}

/*------------------------------------------------------------------------------
 * Private Functions - Flash Operations Implementation
 *----------------------------------------------------------------------------*/
static FlashOps_Status_t flash_write(uint32_t dest_addr, const uint8_t *src, uint32_t size)
{
    if (dest_addr < SLOT_A_BASE)
    {
        return FLASH_OPS_ADDR_ERR;
    }

    if ((dest_addr % 32U) != 0U || (size % 32U) != 0U)
    {
        return FLASH_OPS_ADDR_ERR;
    }

    uint8_t write_buf[32] __attribute__((aligned(32)));
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    for (uint32_t i = 0U; i < size; i += 32U)
    {
        memcpy(write_buf, src + i, 32U);

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                                   dest_addr + i,
                                   (uint32_t)write_buf);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return FLASH_OPS_ERROR;
        }
    }

    HAL_FLASH_Lock();
    return FLASH_OPS_OK;
}

static FlashOps_Status_t flash_erase_sector(uint32_t addr)
{
    if (addr < SLOT_A_BASE)
    {
        return FLASH_OPS_ADDR_ERR;
    }

    uint32_t sector = 0U;
    uint32_t bank   = FLASH_BANK_1;

    if (addr >= SLOT_B_BASE)
    {
        bank   = FLASH_BANK_2;
        sector = (addr - SLOT_B_BASE) / (128U * 1024U);
    }
    else
    {
        bank   = FLASH_BANK_1;
        sector = (addr - SLOT_A_BASE) / (128U * 1024U);
        sector += 1U;
    }

    FLASH_EraseInitTypeDef erase_init = {0};
    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks        = bank;
    erase_init.Sector       = sector;
    erase_init.NbSectors    = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t sector_error = 0U;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();

    if (status != HAL_OK || sector_error != 0xFFFFFFFFU)
    {
        return FLASH_OPS_ERROR;
    }

    return FLASH_OPS_OK;
}

static FlashOps_Status_t flash_write_metadata(const void *meta)
{
    FlashOps_Status_t status = flash_erase_sector(OTA_METADATA_BASE);
    if (status != FLASH_OPS_OK)
    {
        return status;
    }

    return flash_write(OTA_METADATA_BASE,
                       (const uint8_t *)meta,
                       sizeof(OtaMetadata_t));
}

/*------------------------------------------------------------------------------
 * Public Functions - Boot Verification
 *----------------------------------------------------------------------------*/
bool bootloader_is_app_valid(uint32_t app_start_addr)
{
    uint32_t sp = *(volatile uint32_t *)(app_start_addr);
    uint32_t pc = *(volatile uint32_t *)(app_start_addr + 4U);

    /* Validate SP points to valid RAM region */
    bool sp_valid = ((sp >= 0x20000000UL) && (sp <= 0x20020000UL)) ||
                    ((sp >= 0x24000000UL) && (sp <= 0x24080000UL));

    /* Validate PC points within expected slot range */
    bool pc_valid = (pc >= app_start_addr) &&
                    (pc <  (app_start_addr + SLOT_A_SIZE)) &&
                    (pc != 0xFFFFFFFFUL);

    return sp_valid && pc_valid;
}

/*------------------------------------------------------------------------------
 * Public Functions - Jump to Application
 * (IDENTICAL to your working version)
 *----------------------------------------------------------------------------*/
void bootloader_jump_to_app(uint32_t app_start_addr)
{
    uint32_t app_sp    = *(volatile uint32_t *)(app_start_addr);
    uint32_t app_reset = *(volatile uint32_t *)(app_start_addr + 4U);

    /* Disable the Clock Security System first: HAL_RCC_DeInit() turns HSE
       off, and an armed CSS would raise an NMI (not masked by PRIMASK)
       that traps in the bootloader's default NMI_Handler -> hang. */
    HAL_RCC_DisableCSS() ;

    __disable_irq();

    if (SCB->CCR & SCB_CCR_DC_Msk)
    {
        SCB_CleanDCache();
        SCB_DisableDCache();
    }
    if (SCB->CCR & SCB_CCR_IC_Msk)
    {
        SCB_DisableICache();
    }

    HAL_DeInit();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    HAL_RCC_DeInit();

    /* Clear any pending NMI/fault latches before re-vectoring */
    __set_PRIMASK(1);

    SCB->VTOR = app_start_addr;
    __DSB();
    __ISB();

    __asm volatile
    (
        "msr msp, %[sp]   \n"
        "bx  %[rst]       \n"
        :
        : [sp] "r" (app_sp), [rst] "r" (app_reset)
        :
    );

    while (1) { }
}

/*------------------------------------------------------------------------------
 * Public Functions - Slot Confirmation
 *----------------------------------------------------------------------------*/
Bootloader_Status_t bootloader_confirm_current_slot(void)
{
    OtaMetadata_t meta;
    metadata_read(&meta);

    if (!metadata_is_valid(&meta))
    {
        return BOOTLOADER_ERROR;
    }

    uint32_t active = meta.active_slot;
    if (active > 1U)
    {
        return BOOTLOADER_ERROR;
    }

    meta.slot[active].state = SLOT_STATE_CONFIRMED;
    meta.confirmed    = 1U;
    meta.boot_counter = 0U;

    if (metadata_write(&meta) != (int)FLASH_OPS_OK)
    {
        return BOOTLOADER_FLASH_ERROR;
    }

    return BOOTLOADER_OK;
}

/*------------------------------------------------------------------------------
 * Public Functions - Status Query
 *----------------------------------------------------------------------------*/
Bootloader_Slot_t bootloader_get_active_slot(void)
{
    OtaMetadata_t meta;
    metadata_read(&meta);

    if (!metadata_is_valid(&meta))
    {
        return BOOTLOADER_SLOT_A;
    }

    if (meta.active_slot == BOOTLOADER_SLOT_B)
    {
        return BOOTLOADER_SLOT_B;
    }

    return BOOTLOADER_SLOT_A;
}

uint32_t bootloader_get_slot_base_address(Bootloader_Slot_t slot)
{
    if (slot == BOOTLOADER_SLOT_B)
    {
        return SLOT_B_BASE;
    }
    return SLOT_A_BASE;
}

Bootloader_Decision_t bootloader_get_boot_decision(void)
{
    Bootloader_Decision_t decision = {0};
    OtaMetadata_t meta;
    metadata_read(&meta);

    decision.boot_address   = SLOT_A_BASE;
    decision.slot           = BOOTLOADER_SLOT_A;
    decision.is_pending_ota = false;
    decision.boot_attempt   = 0U;

    if (!metadata_is_valid(&meta))
    {
        return decision;
    }

    int candidate = -1;
    if (meta.slot[0].state == SLOT_STATE_COMPLETE) candidate = 0;
    if (meta.slot[1].state == SLOT_STATE_COMPLETE) candidate = 1;

    if (candidate >= 0)
    {
        uint32_t cand_base = (candidate == 0) ? SLOT_A_BASE : SLOT_B_BASE;
        uint32_t fallback  = (candidate == 0) ? 1 : 0;

        if (meta.boot_counter >= OTA_MAX_BOOT_TRIES)
        {
            meta.slot[candidate].state = SLOT_STATE_INVALID;
            meta.confirmed    = 0U;
            meta.boot_counter = 0U;
            meta.active_slot  = fallback;
            metadata_write(&meta);

            decision.boot_address = (fallback == 0) ? SLOT_A_BASE : SLOT_B_BASE;
            decision.slot         = (fallback == 0) ? BOOTLOADER_SLOT_A : BOOTLOADER_SLOT_B;
        }
        else if (bootloader_is_app_valid(cand_base))
        {
            meta.boot_counter++;
            meta.active_slot = (uint32_t)candidate;
            metadata_write(&meta);

            decision.boot_address   = cand_base;
            decision.slot           = (candidate == 0) ? BOOTLOADER_SLOT_A : BOOTLOADER_SLOT_B;
            decision.is_pending_ota = true;
            decision.boot_attempt   = meta.boot_counter;
        }
        else
        {
            meta.slot[candidate].state = SLOT_STATE_INVALID;
            meta.confirmed    = 0U;
            meta.boot_counter = 0U;
            meta.active_slot  = fallback;
            metadata_write(&meta);

            decision.boot_address = (fallback == 0) ? SLOT_A_BASE : SLOT_B_BASE;
            decision.slot         = (fallback == 0) ? BOOTLOADER_SLOT_A : BOOTLOADER_SLOT_B;
        }
    }
    else
    {
        uint32_t active = meta.active_slot;
        if (active > 1U) active = 0U;

        decision.boot_address = (active == 0) ? SLOT_A_BASE : SLOT_B_BASE;
        decision.slot         = (active == 0) ? BOOTLOADER_SLOT_A : BOOTLOADER_SLOT_B;
    }

    return decision;
}
