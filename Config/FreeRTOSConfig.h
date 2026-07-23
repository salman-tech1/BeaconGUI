
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

#ifndef CMSIS_device_header
#define CMSIS_device_header "stm32h7xx.h"
#endif

/* ── Scheduler ──────────────────────────────────────────────────── */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0

/* ── Clock / Tick ───────────────────────────────────────────────── */
#define configCPU_CLOCK_HZ      ( SystemCoreClock )
#define configTICK_RATE_HZ      ( (TickType_t)1000 )
#define configUSE_16_BIT_TICKS  0

/* ── Heap ───────────────────────────────────────────────────────── */
/*
 * Heap breakdown (approximate):
 *   GUI task:    4096 words x4 = 16 KB
 *   APP task:     512 words x4 =  2 KB
 *   Timer task:   256 words x4 =  1 KB
 *   Idle task:    256 words x4 =  1 KB
 *   TCBs + queues + semaphores = ~5 KB
 *   Headroom                   = ~103 KB
 *
 * LVGL lv_malloc pool (128 KB from lv_conf.h LV_MEM_SIZE) is a
 * SEPARATE static array. It does NOT come from this heap.
 */
#define configTOTAL_HEAP_SIZE       ( (size_t)(128 * 1024) )
#define configSUPPORT_DYNAMIC_ALLOCATION  1
#define configSUPPORT_STATIC_ALLOCATION   0

/* ── Tasks ──────────────────────────────────────────────────────── */
#define configMAX_PRIORITIES     56
/*
 * 256 words = 1 KB. MUST be >= 256 with Cortex-M7 FPU enabled.
 * FPU context save (configENABLE_FPU=1) pushes ~100 extra bytes
 * onto idle and timer stacks. 128 words (512 bytes) overflows.
 */
#define configMINIMAL_STACK_SIZE ((uint16_t)256)
#define configMAX_TASK_NAME_LEN  16

/* ── Task Notifications  ← CRITICAL for LVGL DMA2D async ───────── */
/*
 * lv_freertos.c uses ulTaskNotifyTake/vTaskNotifyGiveFromISR.
 * Without this = 1, the draw thread blocks forever on DMA2D tasks.
 */
#define configUSE_TASK_NOTIFICATIONS             1
/*
 * lv_freertos.c:208 stores xCurrentTaskHandle in pxCond->xTaskToNotify.
 * Slot 0 is used by LVGL. Provide at least 1 slot.
 */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  3

/* ── Sync primitives ────────────────────────────────────────────── */
/* xSemaphoreCreateRecursiveMutex for lv_general_mutex */
#define configUSE_MUTEXES              1
#define configUSE_RECURSIVE_MUTEXES    1
#define configUSE_COUNTING_SEMAPHORES  1
#define configUSE_QUEUE_SETS           0
#define configQUEUE_REGISTRY_SIZE      8

/* ── Hooks ──────────────────────────────────────────────────────── */
#define configUSE_IDLE_HOOK            0
#define configUSE_TICK_HOOK            0
#define configUSE_MALLOC_FAILED_HOOK   0   /* void vApplicationMallocFailedHook(void) */
#define configCHECK_FOR_STACK_OVERFLOW 2   /* void vApplicationStackOverflowHook(...) */

/* ── Runtime stats / trace ──────────────────────────────────────── */
#define configGENERATE_RUN_TIME_STATS        0
#define configUSE_TRACE_FACILITY             1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1

/* ── Software timers ────────────────────────────────────────────── */
#define configUSE_TIMERS              1
#define configTIMER_TASK_PRIORITY     (tskIDLE_PRIORITY + 1U)
#define configTIMER_QUEUE_LENGTH      8
#define configTIMER_TASK_STACK_DEPTH  256

/* ── Co-routines (unused) ───────────────────────────────────────── */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES 2

/* ── Interrupt Priorities ───────────────────────────────────────── */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS  __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS  4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_API_CALL_INTERRUPT_PRIORITY \
    configMAX_SYSCALL_INTERRUPT_PRIORITY

/* ── Assert ─────────────────────────────────────────────────────── */
#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

/* ── Hardware ───────────────────────────────────────────────────── */
/*
 * MUST be 1. Without FPU context saving, floating-point registers
 * corrupt between FreeRTOS task switches during LVGL rendering.
 */
#define configENABLE_FPU        1
#define configENABLE_MPU        0
#define configENABLE_TRUSTZONE  0

/* ── INCLUDE flags ──────────────────────────────────────────────── */
/*
 * xTaskGetCurrentTaskHandle required by lv_freertos.c:201
 * lv_thread_sync_wait() calls it to identify the waiting task.
 */
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskCleanUpResources        0
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_xTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_eTaskGetState                1

/* ── Misc ───────────────────────────────────────────────────────── */
#define configMESSAGE_BUFFER_LENGTH_TYPE  size_t

/* ── Port-specific ──────────────────────────────────────────────── */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* SysTick_Handler is xPortSysTickHandler (from port.c). Do NOT define it in stm32h7xx_it.c */
#define USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION 0

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_CONFIG_H */
