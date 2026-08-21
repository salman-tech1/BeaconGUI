/*
 * bootloader.h
 *
 *  Created on: Aug 20, 2026
 *      Author: Muhmmad Salman
 */

#ifndef BOOTLOADER_H_
#define BOOTLOADER_H_


#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTLOADER_VERSION "v1.0"

/** Bootloader status codes */
typedef enum
{
    BOOTLOADER_OK            = 0,
    BOOTLOADER_ERROR         = 1,
    BOOTLOADER_NO_VALID_APP  = 2,
    BOOTLOADER_FLASH_ERROR   = 3
} Bootloader_Status_t;

/** Slot identifiers */
typedef enum
{
    BOOTLOADER_SLOT_A = 0U,
    BOOTLOADER_SLOT_B = 1U
} Bootloader_Slot_t;

/** Boot decision result */
typedef struct
{
    uint32_t         boot_address;
    Bootloader_Slot_t slot;
    bool             is_pending_ota;
    uint32_t         boot_attempt;
} Bootloader_Decision_t;


/**
 * @brief  Determine which application to boot
 * @retval Bootloader_Decision_t with boot address and status
 */
Bootloader_Decision_t bootloader_get_boot_decision(void);

/**
 * @brief  Check if a valid application exists at the given address
 * @param  app_start_addr: Flash address where app vector table resides
 * @retval true if valid app found, false otherwise
 */
bool bootloader_is_app_valid(uint32_t app_start_addr);

/**
 * @brief  Jump to application at specified address
 * @param  app_start_addr: Flash address of app vector table
 * @note   This function does not return on success
 */
void bootloader_jump_to_app(uint32_t app_start_addr);


/**
 * @brief  Confirm the currently running slot as stable
 * @retval BOOTLOADER_OK on success, error code otherwise
 * @note   Application should call this after successful initialization
 *         to prevent rollback on next boot
 */
Bootloader_Status_t bootloader_confirm_current_slot(void);

/*------------------------------------------------------------------------------
 * Public Functions - Status Query
 *----------------------------------------------------------------------------*/

/**
 * @brief  Get the currently active slot
 * @retval BOOTLOADER_SLOT_A or BOOTLOADER_SLOT_B
 */
Bootloader_Slot_t bootloader_get_active_slot(void);

/**
 * @brief  Get the base address of a slot
 * @param  slot: Slot identifier
 * @retval Base flash address of the slot
 */
uint32_t bootloader_get_slot_base_address(Bootloader_Slot_t slot);

#ifdef __cplusplus
}
#endif



#endif /* BOOTLOADER_H_ */
