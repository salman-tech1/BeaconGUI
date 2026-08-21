/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "log.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Common fault reporter: logs the faulting PC/LR and fault status registers,
   then resets. Called from the naked fault handlers with the stacked frame. */
void fault_report_c(uint32_t *frame, uint32_t exc_return, uint32_t id);
void fault_report_c(uint32_t *frame, uint32_t exc_return, uint32_t id)
{
    static const char *const names[] = { "?", "MEMMANAGE", "BUSFAULT", "USAGEFAULT", "HARDFAULT" };
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t pc   = frame[6];
    uint32_t lr   = frame[5];

    Log_Printf(LOG_LEVEL_ERROR, "FAULT",
               "%s PC=0x%08lX LR=0x%08lX CFSR=0x%08lX HFSR=0x%08lX BFAR=0x%08lX (EXC=0x%08lX)",
               names[(id <= 4U) ? id : 0U],
               (unsigned long)pc, (unsigned long)lr,
               (unsigned long)cfsr, (unsigned long)hfsr,
               (unsigned long)SCB->BFAR, (unsigned long)exc_return);

    NVIC_SystemReset();
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/


extern TIM_HandleTypeDef htim7;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  HAL_RCC_NMI_IRQHandler();
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4          \n"
    "ite eq              \n"
    "mrseq r0, msp       \n"
    "mrsne r0, psp       \n"
    "mov  r1, lr         \n"
    "movs r2, #4         \n"
    "b    fault_report_c \n"
  );
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile (
    "tst lr, #4          \n"
    "ite eq              \n"
    "mrseq r0, msp       \n"
    "mrsne r0, psp       \n"
    "mov  r1, lr         \n"
    "movs r2, #1         \n"
    "b    fault_report_c \n"
  );
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4          \n"
    "ite eq              \n"
    "mrseq r0, msp       \n"
    "mrsne r0, psp       \n"
    "mov  r1, lr         \n"
    "movs r2, #2         \n"
    "b    fault_report_c \n"
  );
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile (
    "tst lr, #4          \n"
    "ite eq              \n"
    "mrseq r0, msp       \n"
    "mrsne r0, psp       \n"
    "mov  r1, lr         \n"
    "movs r2, #3         \n"
    "b    fault_report_c \n"
  );
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}




/**
  * @brief This function handles TIM7 global interrupt.
  */
void TIM7_IRQHandler(void)
{
  /* USER CODE BEGIN TIM7_IRQn 0 */

  /* USER CODE END TIM7_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_IRQn 1 */

  /* USER CODE END TIM7_IRQn 1 */
}



/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
