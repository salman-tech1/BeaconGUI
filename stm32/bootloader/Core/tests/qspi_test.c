/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "qspi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define TEST_ADDR_SECTOR_ERASE       0x005000UL   /* 4KB, sector 5 */
#define TEST_ADDR_WRITE_READ         0x006000UL   /* 4KB, sector 6 (+ offset) */
#define TEST_ADDR_MEMMAP             0x007000UL   /* 4KB, sector 7 */
#define TEST_WRITE_NONALIGNED_OFFSET 0x0040UL     /* Byte 64 into sector = non-page-aligned */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

void QSPI_Test_ReadID(void)
{
    uint32_t jedec_id     = 0;
    QSPI_StatusTypeDef status;

    status = QSPI_ReadID(&jedec_id,3);

    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    // TODO : LOG

    if (jedec_id == W25Q128_JEDEC_ID) {
    	// TODO : LOG
    } else {
    	// TODO : LOG
    }
}

void QSPI_Test_SectorErase(void)
{
    static uint8_t verify_buf[64];
    uint32_t i;
    QSPI_StatusTypeDef status;

    status = QSPI_EraseSector(TEST_ADDR_SECTOR_ERASE);
    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    /* Read first 64 bytes of erased sector and verify all 0xFF */
    status = QSPI_Read(TEST_ADDR_SECTOR_ERASE, verify_buf, sizeof(verify_buf));
    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    for (i = 0; i < sizeof(verify_buf); i++) {
        if (verify_buf[i] != 0xFFU) {
        	// TODO : LOG
            return;
        }
    }

    // TODO : LOG
}

void QSPI_Test_WriteRead(void)
{
    static uint8_t write_buf[600];
    static uint8_t read_buf[600];
    uint32_t write_addr = TEST_ADDR_WRITE_READ + TEST_WRITE_NONALIGNED_OFFSET;
    uint32_t i;
    uint32_t mismatch_count = 0;
    QSPI_StatusTypeDef status;

    /* Fill with rolling incrementing pattern — easy to spot shifts */
    for (i = 0; i < sizeof(write_buf); i++) {
        write_buf[i] = (uint8_t)(i & 0xFFU);
    }
    memset(read_buf, 0, sizeof(read_buf));

    // TODO : LOG

    /* Erase containing sector first — page must be 0xFF before write */
    status = QSPI_EraseSector(TEST_ADDR_WRITE_READ);
    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    /* Write 600 bytes at non-aligned address */
    status = QSPI_Write(write_addr, write_buf, 600);
    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    /* Read back */
    status = QSPI_Read(write_addr, read_buf, 600);
    if (status != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    /* Byte-precise comparison — print first 4 mismatches */
    for (i = 0; i < 600U; i++) {
        if (read_buf[i] != write_buf[i]) {
            if (mismatch_count < 4U) {
            	// TODO : LOG
            }
            mismatch_count++;
        }
    }

    if (mismatch_count == 0U) {
    	// TODO : LOG
    } else {
    	// TODO : LOG
    }
}


void QSPI_Test_MemoryMapped(void)
{
    static const uint8_t pattern[16] = {
        0xDE, 0xAD, 0xBE, 0xEF,
        0xCA, 0xFE, 0xBA, 0xBE,
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0
    };
    const volatile uint8_t *mem_ptr;
    uint32_t i;

    /* Write known pattern via indirect mode first */
    QSPI_EraseSector(TEST_ADDR_MEMMAP);
    QSPI_WritePage(TEST_ADDR_MEMMAP, pattern, 16U);

    /* Enable memory-mapped mode */
    if (QSPI_EnableMemoryMappedMode() != QSPI_OK) {
    	// TODO : LOG
        return;
    }

    /*
     * Direct pointer access into QSPI window.
     * The 'volatile' qualifier prevents the compiler from optimizing
     * these reads away or caching them in a register.
     * QSPI_MEMORY_BASE_ADDRESS (0x90000000) + flash offset = CPU address.
     */
    mem_ptr = (const volatile uint8_t *)(QSPI_MEMORY_BASE_ADDRESS + TEST_ADDR_MEMMAP);

    for (i = 0; i < 16U; i++) {
        if (mem_ptr[i] != pattern[i]) {
        	// TODO : LOG
            /* Don't disable — leave active for FPS test */
            return;
        }
    }


    // TODO : LOG

    /* Leave memory-mapped mode ACTIVE for T5 FPS test */
}

void QSPI_Test_ReadThroughput(void)
{
    static uint8_t  dummy_buf[512];    /* Small buffer — just measuring time */
    uint32_t        start_tick, end_tick, elapsed_cycles;
    float           elapsed_ms, throughput_mbps;
    uint32_t        bytes_to_read = 153600UL;   /* 320x240 RGB565 */
    uint32_t        bytes_read    = 0;
    uint32_t        chunk;
    const uint8_t  *flash_ptr;

    /* Ensure memory-mapped mode is active */

        if (QSPI_EnableMemoryMappedMode() != QSPI_OK) {
        	// TODO : LOG
            return;

    }

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    /* ---- COLD CACHE READ (worst case) --------------------------------- */
    /* Invalidate D-Cache to force all accesses to hit QSPI bus */
  //  SCB_InvalidateDCache();

    flash_ptr  = (const uint8_t *)QSPI_MEMORY_BASE_ADDRESS;
    bytes_read = 0;
    start_tick = DWT->CYCCNT;

    while (bytes_read < bytes_to_read) {
        chunk = (bytes_to_read - bytes_read);
        if (chunk > sizeof(dummy_buf)) {
            chunk = sizeof(dummy_buf);
        }
        memcpy(dummy_buf, flash_ptr + bytes_read, chunk);
        bytes_read += chunk;
    }

    end_tick       = DWT->CYCCNT;
    elapsed_cycles = end_tick - start_tick;
    elapsed_ms     = (float)elapsed_cycles / ((float)SystemCoreClock / 1000.0f);
    throughput_mbps = ((float)bytes_to_read / (elapsed_ms / 1000.0f)) / (1024.0f * 1024.0f);

    // TODO : LOG

    /* ---- WARM CACHE READ (best case) ---------------------------------- */
    /* Don't invalidate — let cache serve the data */
    bytes_read = 0;
    start_tick = DWT->CYCCNT;

    while (bytes_read < bytes_to_read) {
        chunk = (bytes_to_read - bytes_read);
        if (chunk > sizeof(dummy_buf)) {
            chunk = sizeof(dummy_buf);
        }
        memcpy(dummy_buf, flash_ptr + bytes_read, chunk);
        bytes_read += chunk;
    }

    end_tick       = DWT->CYCCNT;
    elapsed_cycles = end_tick - start_tick;
    elapsed_ms     = (float)elapsed_cycles / ((float)SystemCoreClock / 1000.0f);
    throughput_mbps = ((float)bytes_to_read / (elapsed_ms / 1000.0f)) / (1024.0f * 1024.0f);

    // TODO : LOG
    // TODO : LOG
}


void QSPI_Test_ErrorHandling(void)
{
    QSPI_StatusTypeDef status;
    uint8_t dummy = 0;
    bool all_ok = true;

    /* NULL buffer → INVALID_PARAM */
    status = QSPI_Read(0x000000, NULL, 10);
    if (status != QSPI_INVALID_PARAM) {
    	// TODO : LOG
        all_ok = false;
    }

    /* Zero size → INVALID_PARAM */
    status = QSPI_Read(0x000000, &dummy, 0);
    if (status != QSPI_INVALID_PARAM) {
    	// TODO : LOG
        all_ok = false;
    }

    /* Address out of range → OUT_OF_RANGE */
    status = QSPI_Read(0xFF0000, &dummy, 65536);
    if (status != QSPI_OUT_OF_RANGE) {
    	// TODO : LOG
        all_ok = false;
    }

    /* Unaligned sector erase → NOT_ALIGNED */
    status = QSPI_EraseSector(0x003A00);
    if (status != QSPI_NOT_ALIGNED) {
    	// TODO : LOG
        all_ok = false;
    }

    /* Unaligned block erase → NOT_ALIGNED */
    status = QSPI_EraseBlock(0x001000);
    if (status != QSPI_NOT_ALIGNED) {
    	// TODO : LOG
        all_ok = false;
    }

    if (all_ok) {
    	// TODO : LOG
    } else {
    	// TODO : LOG
    }
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/


/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */
//  QSPI_Init();
//
//  	  QSPI_Test_ReadID();
//      QSPI_Test_SectorErase();
//      QSPI_Test_WriteRead();
//      QSPI_Test_MemoryMapped();
//      QSPI_Test_ReadThroughput();
//      QSPI_Test_ErrorHandling();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 160;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief QUADSPI Initialization Function
  * @param None
  * @retval None
  */


/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
