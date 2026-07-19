/*
 * qspi.c
 *
 *  Created on: Jul 19, 2026
 *      Author: Muhmmad Salman
 */

#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "qspi.h"

static QSPI_HandleTypeDef hqspi;

#define IS_VALID_ADDRESS(addr)       ((addr) <= W25Q128_ADDRESS_MAX)
#define IS_VALID_RANGE(addr, size)   (((uint32_t)(addr) + (uint32_t)(size)) <= W25Q128_FLASH_SIZE)
#define IS_SECTOR_ALIGNED(addr)      (((addr) & (W25Q128_SECTOR_SIZE  - 1UL)) == 0UL)
#define IS_BLOCK_ALIGNED(addr)       (((addr) & (W25Q128_BLOCK64_SIZE - 1UL)) == 0UL)
#define IS_PAGE_ALIGNED(addr)        (((addr) & (W25Q128_PAGE_SIZE    - 1UL)) == 0UL)

#define GET_PAGE_REMAINING(addr)     (W25Q128_PAGE_SIZE - ((addr) & (W25Q128_PAGE_SIZE - 1UL)))

/* Mask lower 12 bits to get sector base address */
#define GET_SECTOR_ADDR(addr)        ((addr) & ~(W25Q128_SECTOR_SIZE - 1UL))

// tracks modes indirect or memory mapped
static uint8_t is_memory_mapped = QSPI_MODE_INDIRECT ;
static uint8_t is_quadMode_enable = 0 ;

static int32_t MX_QSPI_MspDeInit(QSPI_HandleTypeDef* hqspi) ;
static int32_t MX_QUADSPI_Init(void) ;



/**
  * @brief QSPI MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hqspi: QSPI handle pointer
  * @retval None
  */
static int32_t  MX_QSPI_MspDeInit(QSPI_HandleTypeDef* hqspi)
{
  if(hqspi->Instance==QUADSPI)
  {

    __HAL_RCC_QSPI_CLK_DISABLE();

    /**QUADSPI GPIO Configuration
    PF6     ------> QUADSPI_BK1_IO3
    PF7     ------> QUADSPI_BK1_IO2
    PF8     ------> QUADSPI_BK1_IO0
    PF9     ------> QUADSPI_BK1_IO1
    PB2     ------> QUADSPI_CLK
    PB6     ------> QUADSPI_BK1_NCS
    */
    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2|GPIO_PIN_6);

  }
  return 0 ;
}



static int32_t MX_QUADSPI_Init(void)
{

	GPIO_InitTypeDef GPIO_InitStruct = {0};
		  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};


		  /** Initializes the peripherals clock
		  */
		    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
		    PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
		    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
		    {
		    	//  printf("MX_QUADSPI_Init  %s %d \r\n",__FILE__,__LINE__) ;

		    			return  -1 ;
		    }

		    /* Peripheral clock enable */
		    __HAL_RCC_QSPI_CLK_ENABLE();

		    __HAL_RCC_GPIOF_CLK_ENABLE();
		    __HAL_RCC_GPIOB_CLK_ENABLE();
		    /**QUADSPI GPIO Configuration
		    PF6     ------> QUADSPI_BK1_IO3
		    PF7     ------> QUADSPI_BK1_IO2
		    PF8     ------> QUADSPI_BK1_IO0
		    PF9     ------> QUADSPI_BK1_IO1
		    PB2     ------> QUADSPI_CLK
		    PB6     ------> QUADSPI_BK1_NCS
		    */
		    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
		    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		    GPIO_InitStruct.Pull = GPIO_NOPULL;
		    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
		    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

		    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
		    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		    GPIO_InitStruct.Pull = GPIO_NOPULL;
		    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		    GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
		    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

		    GPIO_InitStruct.Pin = GPIO_PIN_2;
		    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		    GPIO_InitStruct.Pull = GPIO_NOPULL;
		    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
		    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

		    GPIO_InitStruct.Pin = GPIO_PIN_6;
		    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		    GPIO_InitStruct.Pull = GPIO_NOPULL;
		    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		    GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
		    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

		    /* USER CODE BEGIN QUADSPI_MspInit 1 */

		    /* USER CODE END QUADSPI_MspInit 1 */



	 /* QUADSPI parameter configuration*/
	 hqspi.Instance = QUADSPI;
	 hqspi.Init.ClockPrescaler = 1; // HCLK3 / 2 = 100Mhz = 1cycle = 10ns
	 hqspi.Init.FifoThreshold = 4; // AFter it reach 4 byte  DMA request
	 hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE; // shift sample for PCB tracing issue
	 hqspi.Init.FlashSize = 23; // Flash size  2^24 = 16Mb
	 hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE; // at least 2 cycle delay before change like CS HIGH another transaction 2 cycle wait than
	 hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0; // Mode 0 polarity idle low
	 hqspi.Init.FlashID = QSPI_FLASH_ID_1; // Flash id in case of 2 QSPI flashes
	 hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE; // disable it
	 if (HAL_QSPI_Init(&hqspi) != HAL_OK)
	 {
		//  printf("MX_QUADSPI_Init  %s %d \r\n",__FILE__,__LINE__) ;
		  	  return -1 ;
	 }
	 HAL_Delay(1);
	// SCB_DisableDCache();
	// SCB_DisableICache() ;

	return 1 ;



}

/*
 * QSPI auto-polling to monitor the BUSY bit.
 */

static QSPI_StatusTypeDef QSPI_WaitForReady(uint32_t timeout_ms)
{
    QSPI_CommandTypeDef     cmd  = {0};
    QSPI_AutoPollingTypeDef poll = {0};

    cmd.Instruction       = W25Q128_CMD_READ_SR1;
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DummyCycles       = 0;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.NbData            = 1;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /*
     * Match = 0x00 (want BUSY=0)
     * Mask  = 0x01 (only check bit 0)
     * Condition: (response & 0x01) == 0x00 → not busy
     */
    poll.Match           = 0x00U;
    poll.Mask            = W25Q128_SR1_BUSY;
    poll.Interval        = 0x10U;
    poll.StatusBytesSize = 1;
    poll.MatchMode       = QSPI_MATCH_MODE_AND;
    poll.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    if (HAL_QSPI_AutoPolling(&hqspi, &cmd, &poll, timeout_ms) != HAL_OK) {

    	 // TODO : Log Error
    	return QSPI_TIMEOUT;
    }

    return QSPI_OK;
}

/*
 * Write Enable
 */
static QSPI_StatusTypeDef QSPI_WriteEnable(void)
{
    QSPI_CommandTypeDef     cmd  = {0};
    QSPI_AutoPollingTypeDef poll = {0};

    /* Send Write Enable command */
    cmd.Instruction       = W25Q128_CMD_WRITE_ENABLE;
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DummyCycles       = 0;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {

    	 // TODO : Log Error

    	return QSPI_ERROR;
    }

    /*
     * Verify WEL bit set via auto-polling on SR1.
     *
     * Auto-polling sends Read SR1 (0x05) repeatedly in hardware,
     * compares response byte against (Match & Mask), and fires TC flag
     * when they match. CPU does not spin here — HAL blocks on the flag.
     *
     * Match = 0x02 (want WEL=1)
     * Mask  = 0x02 (only check bit 1)
     * Condition: (response & 0x02) == 0x02 → WEL is set
     */
    cmd.Instruction = W25Q128_CMD_READ_SR1;
    cmd.DataMode    = QSPI_DATA_1_LINE;
    cmd.NbData      = 1;

    poll.Match           = W25Q128_SR1_WEL;
    poll.Mask            = W25Q128_SR1_WEL;
    poll.Interval        = 0x10U;
    poll.StatusBytesSize = 1;
    poll.MatchMode       = QSPI_MATCH_MODE_AND;
    poll.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    if (HAL_QSPI_AutoPolling(&hqspi, &cmd, &poll, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
    	 // TODO : Log Error

    	return QSPI_WRITE_PROTECTED;
    }

    return QSPI_OK;
}


/*
 * Enable Quad Mode setting QE bit in Status Register 2
 *
 */
static QSPI_StatusTypeDef QSPI_EnableQuadMode(void)
{
    QSPI_CommandTypeDef cmd = {0};
    QSPI_StatusTypeDef stat = QSPI_OK ;
       uint8_t sr2   = 0;

    /* Read current SR2 */
    cmd.Instruction       = W25Q128_CMD_READ_SR2; // 0x35U . QE bit
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE; // line 1
    cmd.AddressMode       = QSPI_ADDRESS_NONE; // No need address line
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DummyCycles       = 0;
    cmd.DataMode          = QSPI_DATA_1_LINE; // Data line
    cmd.NbData            = 1;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {

    	 // TODO : Log Error
    	return QSPI_ERROR;
    }
    if (HAL_QSPI_Receive(&hqspi, &sr2, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
    	 // TODO : Log Error
    	return QSPI_ERROR;
    }

    /* QE already set? Done. */
    if ((sr2 & W25Q128_SR2_QE) != 0U) {
        return QSPI_OK;
    }

    /* Set QE bit */
    sr2 |= W25Q128_SR2_QE;

    /* Write Enable required before modifying Status Register */
    if (QSPI_WriteEnable() != QSPI_OK) {
    	 // TODO : Log Error
        return QSPI_WRITE_PROTECTED;
    }

    /* Write SR2 with QE bit set */
    cmd.Instruction = W25Q128_CMD_WRITE_SR2;
    cmd.DataMode    = QSPI_DATA_1_LINE;
    cmd.NbData      = 1;

    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
    	 // TODO : Log Error
    	return QSPI_ERROR;
    }
    if (HAL_QSPI_Transmit(&hqspi, &sr2, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
    	 // TODO : Log Error
    	return QSPI_ERROR;
    }

    stat =  QSPI_WaitForReady(QSPI_TIMEOUT_WRITE_SR);

    if(stat == QSPI_OK )
    is_quadMode_enable = 1 ;

    /* Wait for SR write to complete (max 15ms) */
    return stat ;
}


QSPI_StatusTypeDef QSPI_Init(void)
{
	 QSPI_StatusTypeDef status;
	 int32_t stat = 0 ;

	    uint32_t jedec_id = 0;

	    stat =  MX_QUADSPI_Init() ;

		if(stat  < 0  )
		{
			 // TODO : Log Error
			return QSPI_ERROR ;
		}
	    is_memory_mapped       = QSPI_MODE_INDIRECT;
		// Read and confirm ID
	    status = QSPI_ReadID(&jedec_id,3);
	    if (status != QSPI_OK) {
	    	 // TODO : Log Error
	    	 // TODO : Log Error
	        return status;
	    }


	    if (jedec_id != W25Q128_JEDEC_ID) {
	    	 // TODO : Log Error
	        return QSPI_ID_MISMATCH;
	    }



	    status = QSPI_EnableQuadMode();
	    if (status != QSPI_OK) {
	    	 // TODO : Log Error
	        return status;
	    }


	    return QSPI_OK;
}

/*
 *   De-initialize the driver.
 */

QSPI_StatusTypeDef QSPI_DeInit(void)
{
	if (is_memory_mapped == QSPI_MODE_MEMORYMAPPED) {
	    QSPI_DisableMemoryMappedMode();
	}

		MX_QSPI_MspDeInit(&hqspi) ;

	    HAL_QSPI_DeInit(&hqspi);
	    is_memory_mapped        = QSPI_MODE_INDIRECT;

	    return QSPI_OK;
}


/* Read JEDEC ID — call first, verify = 0xEF4018
 *@param  pointer to id
 *@param  length of id in bytes
 * @return : QSPI_StatusTypeDef */
QSPI_StatusTypeDef QSPI_ReadID(uint32_t  *id, uint8_t len)
{
	QSPI_CommandTypeDef cmd = {0};
	    uint8_t id_buf[3]       = {0, 0, 0};

	    if (id == NULL) {

	    	 // TODO : Log Error
	        return QSPI_INVALID_PARAM;

	    }

	    cmd.Instruction       = W25Q128_CMD_READ_JEDEC_ID;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.AddressMode       = QSPI_ADDRESS_NONE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;   /* Ignored (NONE), set for clarity */
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_1_LINE;
	    cmd.NbData            = len ; // Number of data to transfer (read/write )
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    if (HAL_QSPI_Receive(&hqspi, id_buf, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    /*
	     * Pack three bytes into one 32-bit integer.
	     * Bit shifting: id_buf[0] is the most significant byte (manufacturer).
	     * Result format: 0x00XXXXXX where XX = [MF][Type][Capacity]
	     */
	    *id = ((uint32_t)id_buf[0] << 16U) |
	                ((uint32_t)id_buf[1] <<  8U) |
	                ((uint32_t)id_buf[2]);

	    return QSPI_OK;
}

/*
 * Read Status Register 1 and report BUSY/READY state
 * @return : QSPI_StatusTypeDef
 */
QSPI_StatusTypeDef QSPI_GetStatus(void)
{
	QSPI_CommandTypeDef cmd = {0};
	    uint8_t sr1 = 0;

	    cmd.Instruction       = W25Q128_CMD_READ_SR1;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.AddressMode       = QSPI_ADDRESS_NONE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_1_LINE;
	    cmd.NbData            = 1;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    if (HAL_QSPI_Receive(&hqspi, &sr1, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    return (sr1 & W25Q128_SR1_BUSY) ? QSPI_BUSY : QSPI_OK;
}






/* Erase one 4KB sector at given address
 * address   Sector base address (0x000000, 0x001000, 0x002000 ... )
 * @return : QSPI_StateTypeDef

 *  */
QSPI_StatusTypeDef QSPI_EraseSector(uint32_t address)
{
	 QSPI_CommandTypeDef cmd = {0};
	 QSPI_StatusTypeDef stat = QSPI_OK ;
	 // Check the sector  (address & 0xFFF == 0)
	    if (!IS_SECTOR_ALIGNED(address)) {
	    	 // TODO : Log Error
	        return QSPI_NOT_ALIGNED;
	    }
	    if (!IS_VALID_ADDRESS(address)) {
	    	 // TODO : Log Error
	        return QSPI_OUT_OF_RANGE;
	    }
	    stat = QSPI_WriteEnable() ;
	    if (stat  != QSPI_OK) {
	    	 // TODO : Log Error
	        return QSPI_WRITE_PROTECTED;
	    }

	    cmd.Instruction       = W25Q128_CMD_SECTOR_ERASE;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.Address           = address;
	    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_NONE;  /* Erase has no data phase */
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    stat = QSPI_WaitForReady(QSPI_TIMEOUT_SECTOR_ERASE) ;
	    return stat ;
}

/**
 *  Erase 64KB block. Address must be 64KB-aligned.
 * @param  addr   Block base address (0x000000, 0x010000, 0x020000 ...)
   @return : QSPI_StateTypeDef
 */
QSPI_StatusTypeDef QSPI_EraseBlock(uint32_t address)
{
	 QSPI_CommandTypeDef cmd = {0};
	 QSPI_StatusTypeDef stat = QSPI_OK ;
	 // Check Block alignment  (address & 0xFFFF == 0)
	    if (!IS_BLOCK_ALIGNED(address)) {
	    	 // TODO : Log Error
	        return QSPI_NOT_ALIGNED;
	    }
	    if (!IS_VALID_ADDRESS(address)) {
	    	 // TODO : Log Error
	        return QSPI_OUT_OF_RANGE;
	    }
	    stat = QSPI_WriteEnable() ;
	    if (stat  != QSPI_OK) {
	    	 // TODO : Log Error
	        return QSPI_WRITE_PROTECTED;
	    }

	    cmd.Instruction       = W25Q128_CMD_BLOCK_ERASE_64K;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.Address           = address;
	    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_NONE;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    stat = QSPI_WaitForReady(QSPI_TIMEOUT_SECTOR_ERASE) ;
	    return stat;
}

/**
  Erase entire chip (all bytes → 0xFF).
 * @warning Duration up to 200 SECONDS. Factory/test use only.
 *  @return : QSPI_StateTypeDef
 */
QSPI_StatusTypeDef QSPI_EraseChip(void)
{

	QSPI_CommandTypeDef cmd = {0};
	 QSPI_StatusTypeDef stat = QSPI_OK ;
	 stat =QSPI_WriteEnable()  ;
	    if ( stat != QSPI_OK) {
	    	 // TODO : Log Error
	        return QSPI_WRITE_PROTECTED;
	    }

	    cmd.Instruction       = W25Q128_CMD_CHIP_ERASE;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.AddressMode       = QSPI_ADDRESS_NONE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_NONE;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    stat = QSPI_WaitForReady(QSPI_TIMEOUT_CHIP_ERASE)  ;
	    return stat  ;
}
/* Write up to 256 bytes (one page)
 * Caller is responsible for alignment — driver enforces it
 * Target bytes must be 0xFF (caller's responsibility to erase first)
 *
 * @param  address   Start address within a page
 * @param  data   Source buffer
 * @param  size   Bytes to write (1..256, must fit within page)
 *  @return : QSPI_StatusTypeDef
 * */
QSPI_StatusTypeDef QSPI_WritePage(uint32_t address,
                                           const uint8_t *data,
                                           uint16_t size)
{
	QSPI_CommandTypeDef cmd = {0};
	 QSPI_StatusTypeDef stat = QSPI_OK ;
	    volatile uint32_t page_space = 0 ;


	    if (data == NULL || size == 0U) {
	    	 // TODO : Log Error
	        return QSPI_INVALID_PARAM;
	    }
	    // Validate: address + size must not cross page boundary
	    if (!IS_VALID_RANGE(address, size)) {
	    	 // TODO : Log Error
	        return QSPI_OUT_OF_RANGE;
	    }
            // Validate: size <= 256
	    if (size > W25Q128_PAGE_SIZE) {
	    	 // TODO : Log Error
	        return QSPI_INVALID_PARAM;
	    }

	    /* Check page boundary: write must fit within the current page */
	    page_space = GET_PAGE_REMAINING(address);
	    if ((uint32_t)size > page_space) {
	    	 // TODO : Log Error
	        return QSPI_NOT_ALIGNED;
	    }
	    stat = QSPI_WriteEnable() ;
	    /* Write Enable before every Page Program */
	    if ( stat != QSPI_OK) {

	    	 // TODO : Log Error
	        return QSPI_WRITE_PROTECTED;
	    }

	    /*
	     * Standard Page Program (1-1-1, opcode 0x02):
	     *   Instruction: 1 line
	     *   Address:     1 line, 24-bit
	     *   Data OUT:    1 line (up to 256 bytes)
	     *   No dummy cycles — data follows address immediately
	     *
	     * NOTE: We use 1-line (not quad) page program for reliability.
	     * Quad page program (0x32) requires IO2/IO3 driven correctly during
	     * the write phase. Standard 0x02 is simpler and works on all variants.
	     */
	    cmd.Instruction       = W25Q128_CMD_PAGE_PROGRAM;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.Address           = address;
	    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = 0;
	    cmd.DataMode          = QSPI_DATA_1_LINE;
	    cmd.NbData            = size;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)(uintptr_t)data, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    stat = QSPI_WaitForReady(QSPI_TIMEOUT_PAGE_PROGRAM);
	    /* Page program takes 0.4ms typical, 3ms max. Wait for completion. */
	    return stat;
}



/* Write arbitary Bytes at arbitary addres
   , Handles page splitting internally
*  @param  address   Flash address
 * @param  data      Source buffer
 * @param  size   Total bytes to write
 * @return : QSPI_StatusTypeDef
*/
QSPI_StatusTypeDef QSPI_Write(uint32_t      address,
                                        const uint8_t *data,
                                        uint32_t       size)
{

		QSPI_StatusTypeDef status = QSPI_OK;
	    uint32_t bytes_remaining = size;
	    uint32_t current_address = address;
	    uint32_t data_offset     = 0;
	    uint32_t size_to_write;

	    if (data == NULL || size == 0U) {
	    	 // TODO : Log Error

	        return QSPI_INVALID_PARAM;
	    }
	    if (!IS_VALID_RANGE(address, size)) {
	    	 // TODO : Log Error

	        return QSPI_OUT_OF_RANGE;
	    }

	    while (bytes_remaining > 0U) {
	        /*
	         * Space remaining in current page from current_address.
	         * For page-aligned address:  256 - 0   = 256 (full page)
	         * For address at byte 64:    256 - 64  = 192 (partial page)
	         * CRITICAL: formula uses 256, NOT 0xFF (255). Off-by-one here
	         * causes misalignment bug that is very hard to debug.
	         */
	        size_to_write = GET_PAGE_REMAINING(current_address);

	        /* Clamp: don't write more than what's left */
	        if (size_to_write > bytes_remaining) {
	            size_to_write = bytes_remaining;
	        }

	        status = QSPI_WritePage(current_address,
	                                    data + data_offset,
	                                    (uint16_t)size_to_write);
	        if (status != QSPI_OK) {
	        	 // TODO : Log Error
	            return status;   /* Early exit: propagate error to caller */
	        }

	        /* Advance all three cursors by the same amount */
	        current_address  += size_to_write;
	        data_offset      += size_to_write;
	        bytes_remaining  -= size_to_write;
	    }

	    return QSPI_OK;
}







/* Read N bytes from address ( Read data using Quad Output Fast Read (0x6B) )
 * @param  address Flash address [0x000000 .. 0xFFFFFF]
 * @param  data    Destination buffer
 * @param  size   Number of bytes
 *  @return : QSPI_StatusTypeDef
 * */
QSPI_StatusTypeDef QSPI_Read(uint32_t address,
                                      uint8_t *data,
                                      uint32_t size)
{
	QSPI_CommandTypeDef cmd = {0};

	    if (data == NULL || size == 0U) {
	    	 // TODO : Log Error
	        return QSPI_INVALID_PARAM;
	    }
	    if (!IS_VALID_RANGE(address, size) || (address > W25Q128_ADDRESS_MAX) ) {
	    	 // TODO : Log Error
	        return QSPI_OUT_OF_RANGE;
	    }



	    if(is_quadMode_enable != 1 )
	    {
	    	QSPI_EnableQuadMode() ;
	    }

	    cmd.Instruction       = W25Q128_CMD_FAST_READ_QUAD;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.Address           = address;
	    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = QSPI_DUMMY_CYCLES_QUAD_READ; //  8 dummy clock pulses at 100MHz = 80ns of setup time.  Using 6 cycles = reading before data is valid = bit-shifted garbage output.
	    cmd.DataMode          = QSPI_DATA_4_LINES;
	    cmd.NbData            = size;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    if (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }
	    if (HAL_QSPI_Receive(&hqspi, data, QSPI_TIMEOUT_DEFAULT) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    return QSPI_OK;
}

/* Switch to memory-mapped mode — after this, no write/erase
 *
 * */
QSPI_StatusTypeDef QSPI_EnableMemoryMappedMode(void)
{
	QSPI_CommandTypeDef      cmd = {0};
	    QSPI_MemoryMappedTypeDef cfg = {0};

	    if (is_memory_mapped == QSPI_MODE_MEMORYMAPPED) {
	        return QSPI_OK;  /* Idempotent: already active */
	    }

	    if(is_quadMode_enable != 1 )
	   	    {
	   	    	QSPI_EnableQuadMode() ;
	   	    }


	    /*
	     * Configure the read command that QUADSPI will automatically issue
	     * for every AHB read to the 0x90000000 window.
	     * Must use the same command, address bits, and dummy cycles as
	     * manual reads — the flash chip doesn't know it's automatic.
	     */
	    cmd.Instruction       = W25Q128_CMD_FAST_READ_QUAD;
	    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
	    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
	    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	    cmd.DummyCycles       = QSPI_DUMMY_CYCLES_QUAD_READ;
	    cmd.DataMode          = QSPI_DATA_4_LINES;
	    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
	    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

	    cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;

	    if (HAL_QSPI_MemoryMapped(&hqspi, &cmd, &cfg) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    is_memory_mapped = QSPI_MODE_MEMORYMAPPED;
	    return QSPI_OK;
}

/* Exit memory-mapped mode — required before write/erase */
QSPI_StatusTypeDef QSPI_DisableMemoryMappedMode(void)
{
	if (is_memory_mapped  == QSPI_MODE_INDIRECT) {
		 // TODO : Log Error
	        return QSPI_OK;  /* Idempotent: already in indirect mode */
	    }

	    if (HAL_QSPI_Abort(&hqspi) != HAL_OK) {
	    	 // TODO : Log Error
	        return QSPI_ERROR;
	    }

	    is_memory_mapped  = QSPI_MODE_INDIRECT;
	    return QSPI_OK;
}





