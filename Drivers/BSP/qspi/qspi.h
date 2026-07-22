/*
 * qspi.h
 *
 *  Created on: Jul 19, 2026
 *      Author: Muhmmad Salman
 */

#ifndef QSPI_H
#define QSPI_H


#include "qspi_config.h"
#include <stdint.h>
#include <stdbool.h>

/* qspi.h — public interface */

typedef enum {
    QSPI_OK              = 0x00,  /**< Success */
    QSPI_ERROR           = 0x01,  /**< HAL-level communication error */
    QSPI_BUSY            = 0x02,  /**< Flash currently in operation */
    QSPI_TIMEOUT         = 0x03,  /**< Operation exceeded max time */
    QSPI_NOT_ALIGNED     = 0x04,  /**< Address not aligned to required boundary */
    QSPI_OUT_OF_RANGE    = 0x05,  /**< Address + size exceeds flash bounds */
    QSPI_ID_MISMATCH     = 0x06,  /**< JEDEC ID != W25Q128 (wrong chip or no power) */
    QSPI_WRITE_PROTECTED = 0x07,  /**< WEL bit failed to set after Write Enable */
    QSPI_INVALID_PARAM   = 0x08,  /**< NULL pointer or zero size passed */
} QSPI_StatusTypeDef;


typedef enum {
    QSPI_MODE_INDIRECT    = 0,  /**< Command-driven (normal) mode */
    QSPI_MODE_MEMORYMAPPED = 1, /**< XIP mode: 0x90000000 window active */
} QSPI_ModeTypeDef;



/* Init and verify chip responds
@return : QSPI_StatusTypeDef
*/
QSPI_StatusTypeDef QSPI_Init(void);

/* De-Init and verify chip responds
@return : QSPI_StatusTypeDef
*/
QSPI_StatusTypeDef QSPI_DeInit(void);


/* Read JEDEC ID — call first, verify = 0xEF4018
 *@param  pointer to id
 *@param  length of id
 * @return : QSPI_StatusTypeDef */
QSPI_StatusTypeDef QSPI_ReadID(uint32_t  *id, uint8_t len) ;

/*
 * Read Status Register 1.
 * @return : QSPI_StatusTypeDef
 */
QSPI_StatusTypeDef QSPI_GetStatus(void);




/* Erase one 4KB sector at given address
 * address   Sector base address (0x000000, 0x001000, 0x002000 ... )
 * @return : QSPI_StateTypeDef

 *  */
QSPI_StatusTypeDef QSPI_EraseSector(uint32_t address);


/**
 *  Erase 64KB block. Address must be 64KB-aligned.
 * @param  addr   Block base address (0x000000, 0x010000, 0x020000 ...)
   @return : QSPI_StateTypeDef
 */
QSPI_StatusTypeDef QSPI_EraseBlock(uint32_t address);

/**
  Erase entire chip (all bytes → 0xFF).
 * @warning Duration up to 200 SECONDS. Factory/test use only.
 *  @return : QSPI_StateTypeDef
 */
QSPI_StatusTypeDef QSPI_EraseChip(void);



/* Write arbitary Bytes at arbitary addres
   , Handles page splitting internally
*  @param  address   Flash address
 * @param  data      Source buffer
 * @param  size   Total bytes to write
 * @return : QSPI_StatusTypeDef
*/
QSPI_StatusTypeDef QSPI_Write(uint32_t      address,
                                        const uint8_t *data,
                                        uint32_t       size);


/* Write up to 256 bytes (one page)
 * Caller is responsible for alignment — driver enforces it
 * @param  address   Start address within a page
 * @param  data   Source buffer
 * @param  size   Bytes to write (1..256, must fit within page)
 *  @return : QSPI_StatusTypeDef
 * */
QSPI_StatusTypeDef QSPI_WritePage(uint32_t address,
                                           const uint8_t *data,
                                           uint16_t size);





/* Read N bytes from address ( Read data using Quad Output Fast Read (0x6B) )
 * @param  address Flash address [0x000000 .. 0xFFFFFF]
 * @param  data    Destination buffer
 * @param  size   Number of bytes
 *  @return : QSPI_StatusTypeDef
 * */
QSPI_StatusTypeDef QSPI_Read(uint32_t address,
                                      uint8_t *data,
                                      uint32_t size);

/* Switch to memory-mapped mode — after this, no write/erase
 *
 * */
QSPI_StatusTypeDef QSPI_EnableMemoryMappedMode(void);

/* Exit memory-mapped mode — required before write/erase */
QSPI_StatusTypeDef QSPI_DisableMemoryMappedMode(void);


#endif /* QSPI_H */
