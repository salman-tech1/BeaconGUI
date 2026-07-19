/*
 * sdram.c
 *
 *  Created on: Jul 19, 2026
 *      Author: Muhmmad Salman
 */



#include <sdram.h>
#include <stdio.h>
#include <stdlib.h>


#include "main.h"


/* Mode Register value — YOU will calculate this below */
#define SDRAM_MODEREG_BURST_LENGTH_1         ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL  ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_2          ((uint16_t)1U << 5 )
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE ((uint16_t)1U << 9 )  /* */

#define SDRAM_MODEREG_VALUE  (SDRAM_MODEREG_BURST_LENGTH_1       | \
                              SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL | \
                              SDRAM_MODEREG_CAS_LATENCY_2         | \
                              SDRAM_MODEREG_OPERATING_MODE_STANDARD | \
                              SDRAM_MODEREG_WRITEBURST_MODE_SINGLE)

static SDRAM_HandleTypeDef hsdram1 ;


/* FMC initialization function */
static int32_t MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */
	  GPIO_InitTypeDef GPIO_InitStruct ={0};


	  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	  /** Initializes the peripherals clock
	  */
	    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC;
	    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_D1HCLK;
	    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
	    {
	    	// TODO : LOG
	    	return -1 ;
	    }

	  /* Peripheral clock enable */
	  __HAL_RCC_FMC_CLK_ENABLE();

	  /** FMC GPIO Configuration
	  PF0   ------> FMC_A0
	  PF1   ------> FMC_A1
	  PF2   ------> FMC_A2
	  PF3   ------> FMC_A3
	  PF4   ------> FMC_A4
	  PF5   ------> FMC_A5
	  PH2   ------> FMC_SDCKE0
	  PH3   ------> FMC_SDNE0
	  PH5   ------> FMC_SDNWE
	  PF11   ------> FMC_SDNRAS
	  PF12   ------> FMC_A6
	  PF13   ------> FMC_A7
	  PF14   ------> FMC_A8
	  PF15   ------> FMC_A9
	  PG0   ------> FMC_A10
	  PG1   ------> FMC_A11
	  PE7   ------> FMC_D4
	  PE8   ------> FMC_D5
	  PE9   ------> FMC_D6
	  PE10   ------> FMC_D7
	  PE11   ------> FMC_D8
	  PE12   ------> FMC_D9
	  PE13   ------> FMC_D10
	  PE14   ------> FMC_D11
	  PE15   ------> FMC_D12
	  PD8   ------> FMC_D13
	  PD9   ------> FMC_D14
	  PD10   ------> FMC_D15
	  PD14   ------> FMC_D0
	  PD15   ------> FMC_D1
	  PG2   ------> FMC_A12
	  PG4   ------> FMC_BA0
	  PG5   ------> FMC_BA1
	  PG8   ------> FMC_SDCLK
	  PD0   ------> FMC_D2
	  PD1   ------> FMC_D3
	  PG15   ------> FMC_SDNCAS
	  PE0   ------> FMC_NBL0
	  PE1   ------> FMC_NBL1
	  */
	  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
	                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_11|GPIO_PIN_12
	                          |GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
	  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
	  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

	  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_5;
	  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
	  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

	  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_4
	                          |GPIO_PIN_5|GPIO_PIN_8|GPIO_PIN_15;
	  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
	  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

	  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10
	                          |GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14
	                          |GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1;
	  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
	  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_14
	                          |GPIO_PIN_15|GPIO_PIN_0|GPIO_PIN_1;
	  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
	  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 8;
  SdramTiming.SelfRefreshTime = 5;
  SdramTiming.RowCycleDelay = 6;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
	  // TODO : LOG
	  return -1 ;
  }

  return 0;

}



// These Commands need to be send to enable the chip
static int32_t SDRAM_InitSequence(void)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};

    /* Step 1: Enable SDCLK — chip sees clock for first time */
    cmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY) !=  HAL_OK )
    {
    	// TODO : LOG
    	return -1 ;
    }

    HAL_Delay(1);  /* Wait > 100µs for clock and voltage to stabilize */

    /* Step 2: Precharge ALL banks — force all banks to idle */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
   if( HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY) != HAL_OK )
   {
	   // TODO : LOG
       	return -1 ;
       }

    /* Step 3: 8× Auto-Refresh — normalize all capacitor charges */
    cmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY) != HAL_OK )
    {
    	// TODO : LOG
        	return -1 ;
        }
    /* Step 4: Load Mode Register — program CL and burst into chip */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = SDRAM_MODEREG_VALUE;  /* ← completes here */
    if(HAL_SDRAM_SendCommand(&hsdram1, &cmd, HAL_MAX_DELAY) != HAL_OK )
    {
    	// TODO : LOG
        	return -1 ;
        }

    /* Step 5: Set refresh rate
       Formula: (refresh_period_in_cycles) - 20
       = (7.8µs × 100MHz) - 20
       = 780 - 20 = 760                                */
   if( HAL_SDRAM_ProgramRefreshRate(&hsdram1, 760) != HAL_OK)
   {
	   // TODO : LOG
	   return -1 ;
   }
   return 0;
}


SDRAM_StatusTypeDef SDRAM_Init(void)
{
	int32_t stat = 0 ;

	stat = MX_FMC_Init() ;
	if(stat != 0 )
	{
		// TODO : LOG
		return SDRAM_ERROR ;
	}

	HAL_Delay(10) ;
	stat = SDRAM_InitSequence() ;

	if(stat != 0 )
		{
		// TODO : LOG
			return SDRAM_ERROR ;
		}
return SDRAM_OK ;

}


SDRAM_StatusTypeDef SDRAM_Test(void)
{
	   uint32_t startaddress = 0xC0000000 ;
	   uint32_t size = 0x800000 ; // size in bytes
	        uint32_t numWords = size / 4;
		    volatile uint32_t *p = (volatile uint32_t *)startaddress;

		    /* : || not && — a number can't be BOTH less than X AND
		               greater than X+size at the same time                  */
		    if (startaddress < 0xC0000000 || startaddress >= (0xC0000000 + 0x1000000))
		        return SDRAM_ERROR;

		    /* ── Test 1: Stuck-at-0 ────────────────────────────────────── */
		    for (uint32_t n = 0; n < numWords; n++)
		        *(p + n) = 0xFFFFFFFF;

		    /* FIX 2: compare against 0xFFFFFFFF not 0x1                    */
		    for (uint32_t n = 0; n < numWords; n++)
		        if (*(p + n) != 0xFFFFFFFF)
		            return SDRAM_ERROR; /* return EXACT failing address */

		    /* ── Test 2: Stuck-at-1 ────────────────────────────────────── */
		    for (uint32_t n = 0; n < numWords; n++)
		        *(p + n) = 0x00000000;

		    for (uint32_t n = 0; n < numWords; n++)
		        if (*(p + n) != 0x00000000)
		            return SDRAM_ERROR;

		    /* ── Test 3: Checkerboard (cell coupling) ───────────────────── */
		    for (uint32_t n = 0; n < numWords; n += 2) {
		        *(p + n)     = 0xAAAAAAAA;
		        *(p + n + 1) = 0x55555555;
		    }

		    for (uint32_t n = 0; n < numWords; n += 2) {
		        if (*(p + n)     != 0xAAAAAAAA) return SDRAM_ERROR;
		        if (*(p + n + 1) != 0x55555555) return SDRAM_ERROR;
		    }

		    /* ── Test 4: Address uniqueness (catches address line faults) ── */
		    for (uint32_t n = 0; n < numWords; n++)
		        *(p + n) = startaddress + (n * 4); /* FIX 3: uint32_t math,
		                                              NOT pointer cast        */

		    for (uint32_t n = 0; n < numWords; n++)
		        if (*(p + n) != (startaddress + (n * 4)))
		            return SDRAM_ERROR; /* FIX 4: n*4 not just n  */

		    return SDRAM_OK ;

}
