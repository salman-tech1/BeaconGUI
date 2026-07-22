/*
 * qspi_config.h
 *
 *  Created on: Jul 19, 2026
 *      Author: Muhmmad Salman
 */

#ifndef QSPI_CONFIG_H
#define QSPI_CONFIG_H


#include "stm32h7xx_hal.h"

/*
 * CHIP IDENTITY
 * Source: W25Q128JV datasheet
 **/

#define W25Q128_JEDEC_MANUFACTURER     0xEFU        /**< Winbond manufacturer ID */
#define W25Q128_JEDEC_MEMORY_TYPE      0x40U        /**< SPI NOR flash type */
#define W25Q128_JEDEC_CAPACITY         0x18U        /**< 2^24 = 16MB capacity code */
#define W25Q128_JEDEC_ID               0xEF4018UL   /**< Full 24-bit JEDEC ID */

/*
  MEMORY GEOMETRY
 * Source: W25Q128JV datasheet, section 1 (Features)
 *
 * HIERARCHY (memorize this):
 *   Chip   = 256 Blocks  = 4096 Sectors = 65536 Pages
 *   Block  = 16  Sectors = 256  Pages   = 65536 Bytes (64KB)
 *   Sector = 16  Pages               =  4096 Bytes (4KB)  <- minimum erase
 *   Page   = 256 Bytes                              <- maximum write unit
 */

#define W25Q128_FLASH_SIZE             (16UL * 1024UL * 1024UL)  /**< 16 MB total */
#define W25Q128_PAGE_SIZE              256UL                      /**< 256 B: max write */
#define W25Q128_SECTOR_SIZE            (4UL  * 1024UL)           /**< 4  KB: min erase */
#define W25Q128_BLOCK32_SIZE           (32UL * 1024UL)           /**< 32 KB: mid erase */
#define W25Q128_BLOCK64_SIZE           (64UL * 1024UL)           /**< 64 KB: fast erase */
#define W25Q128_PAGE_COUNT             65536UL
#define W25Q128_SECTOR_COUNT           4096UL
#define W25Q128_BLOCK64_COUNT          256UL
#define W25Q128_ADDRESS_BITS           24U           /**< 3-byte addressing */
#define W25Q128_ADDRESS_MAX            (W25Q128_FLASH_SIZE - 1UL)

/*
 COMMAND OPCODES
 * Source: W25Q128JV datasheet, section 8 (Instruction Set Table)
 *
 * Naming convention: CMD_VERB_MODIFIER
 * Every opcode has an exact hardware meaning — never guess these values.
*/

/* --- Identification ---------------------------------------------------- */
#define W25Q128_CMD_READ_JEDEC_ID      0x9FU  /**< Returns MF+Type+Capacity */
#define W25Q128_CMD_READ_MANUF_DEV_ID  0x90U  /**< Returns MF+DeviceID */

/* --- Read Operations --------------------------------------------------- */
#define W25Q128_CMD_READ_DATA          0x03U  /**< Normal read, no dummy cycles */
#define W25Q128_CMD_FAST_READ          0x0BU  /**< 1-1-1: 8 dummy cycles */
#define W25Q128_CMD_FAST_READ_DUAL     0x3BU  /**< 1-1-2: 8 dummy cycles */
#define W25Q128_CMD_FAST_READ_QUAD     0x6BU  /**< 1-1-4: 8 dummy cycles  <-- primary read */
#define W25Q128_CMD_FAST_READ_QUAD_IO  0xEBU  /**< 1-4-4: 4 dummy cycles */

/* --- Write Operations -------------------------------------------------- */
#define W25Q128_CMD_PAGE_PROGRAM       0x02U  /**< 1-1-1 Page Program (max 256B) */
#define W25Q128_CMD_QUAD_PAGE_PROGRAM  0x32U  /**< 1-1-4 Quad input */

/* --- Erase Operations -------------------------------------------------- */
#define W25Q128_CMD_SECTOR_ERASE       0x20U  /**< 4KB  erase (tSE max 400ms) */
#define W25Q128_CMD_BLOCK_ERASE_32K    0x52U  /**< 32KB erase (tBE1 max 1600ms) */
#define W25Q128_CMD_BLOCK_ERASE_64K    0xD8U  /**< 64KB erase (tBE2 max 2000ms) */
#define W25Q128_CMD_CHIP_ERASE         0xC7U  /**< Full erase (tCE max 200s!) */

/* --- Write Control ----------------------------------------------------- */
#define W25Q128_CMD_WRITE_ENABLE       0x06U  /**< Sets WEL bit in SR1 */
#define W25Q128_CMD_WRITE_DISABLE      0x04U  /**< Clears WEL bit */
#define W25Q128_CMD_VOLATILE_SR_WE     0x50U  /**< Write enable for volatile SR only */

/* --- Status Registers -------------------------------------------------- */
#define W25Q128_CMD_READ_SR1           0x05U
#define W25Q128_CMD_READ_SR2           0x35U
#define W25Q128_CMD_READ_SR3           0x15U
#define W25Q128_CMD_WRITE_SR1          0x01U
#define W25Q128_CMD_WRITE_SR2          0x31U
#define W25Q128_CMD_WRITE_SR3          0x11U

/* --- Power Management -------------------------------------------------- */
#define W25Q128_CMD_POWER_DOWN         0xB9U
#define W25Q128_CMD_RELEASE_POWER_DOWN 0xABU

/* --- Reset ------------------------------------------------------------- */
#define W25Q128_CMD_ENABLE_RESET       0x66U
#define W25Q128_CMD_RESET_DEVICE       0x99U

/*
STATUS REGISTER BIT MASKS
 * Source: W25Q128JV datasheet, section 7.1
 *
 * Why separate masks and not magic numbers inline?
 * Because SR1 bit 0 is BUSY — writing (sr & 0x01) everywhere
 * is unreadable. (sr & W25Q128_SR1_BUSY) documents intent.
 */

/* Status Register 1 (SR1) */
#define W25Q128_SR1_BUSY               0x01U  /**< [0] Operation in progress */
#define W25Q128_SR1_WEL                0x02U  /**< [1] Write Enable Latch */
#define W25Q128_SR1_BP0                0x04U  /**< [2] Block Protect 0 */
#define W25Q128_SR1_BP1                0x08U  /**< [3] Block Protect 1 */
#define W25Q128_SR1_BP2                0x10U  /**< [4] Block Protect 2 */
#define W25Q128_SR1_TB                 0x20U  /**< [5] Top/Bottom protect */
#define W25Q128_SR1_SEC                0x40U  /**< [6] Sector protect */
#define W25Q128_SR1_SRP0               0x80U  /**< [7] Status Register Protect */

/* Status Register 2 (SR2) */
#define W25Q128_SR2_SRP1               0x01U  /**< [0] SR Protect 1 */
#define W25Q128_SR2_QE                 0x02U  /**< [1] Quad Enable ← CRITICAL */
#define W25Q128_SR2_LB1                0x08U  /**< [3] Security Lock 1 */
#define W25Q128_SR2_LB2                0x10U  /**< [4] Security Lock 2 */
#define W25Q128_SR2_LB3                0x20U  /**< [5] Security Lock 3 */
#define W25Q128_SR2_CMP                0x40U  /**< [6] Complement protect */
#define W25Q128_SR2_SUS                0x80U  /**< [7] Suspend status */

/*
 *QSPI INTERFACE CONFIGURATION
 */

#define QSPI_MEMORY_BASE_ADDRESS       0x90000000UL  /**< STM32H743 QSPI window */
#define QSPI_DUMMY_CYCLES_FAST_READ    8U            /**< 0x0B: 8 dummy cycles */
#define QSPI_DUMMY_CYCLES_QUAD_READ    8U            /**< 0x6B: 8 dummy cycles */

/*

 *
 * RULE: Always use MAX from datasheet, never typical.
 * Typical = what you see on the bench on a good day.
 * Maximum = what happens at end of flash life, at temperature extremes.
 * Your timeout must survive the maximum case.
 */

#define QSPI_TIMEOUT_DEFAULT           100U      /**< General HAL command timeout */
#define QSPI_TIMEOUT_PAGE_PROGRAM      3U        /**< tPP max = 3ms */
#define QSPI_TIMEOUT_SECTOR_ERASE      400U      /**< tSE max = 400ms */
#define QSPI_TIMEOUT_BLOCK_ERASE_32K   1600U     /**< tBE1 max = 1600ms */
#define QSPI_TIMEOUT_BLOCK_ERASE_64K   2000U     /**< tBE2 max = 2000ms */
#define QSPI_TIMEOUT_CHIP_ERASE        200000U   /**< tCE max = 200 seconds */
#define QSPI_TIMEOUT_WRITE_SR          15U       /**< tW max = 15ms */
#define QSPI_TIMEOUT_RESET             1U        /**< tRST max = 30u*/

#endif /* QSPI_CONFIG_H_ */
