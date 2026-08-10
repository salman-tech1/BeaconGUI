/*
 * com.c  (STM32 side)
 * Author: Muhmmad Salman
 *  Created on: Jul 21, 2026
 */

#include "com.h"
#include "main.h"     /* Error_Handler(), device/HAL headers */
#include <string.h>
#include "pinouts.h"

/* Circular DMA receive buffer size in bytes. */
#define COM_RX_DMA_BUF_SIZE   2048U

/* Largest single buffer any upper layer will ever hand to Com_Send(). */
#define COM_MAX_TX_SIZE       600U

#define COM_USART2_BAUDRATE       115200U

#define COM_USART2_NVIC_PRIORITY     5U
#define COM_USART2_NVIC_SUBPRIORITY  0U
#define COM_DMA_NVIC_PRIORITY        5U
#define COM_DMA_NVIC_SUBPRIORITY     0U

/* Static handles and buffers */
static UART_HandleTypeDef s_huart2;
static DMA_HandleTypeDef  s_hdma_usart2_rx;

/* DMA buffers — 32-byte aligned. STM32H743 D-Cache lines are 32 bytes. */
static uint8_t s_dma_rx_buf[COM_RX_DMA_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t s_dma_tx_buf[COM_MAX_TX_SIZE]     __attribute__((aligned(32)));

static uint16_t             s_last_dma_pos = 0U;
static Com_EventCallback_t  s_event_cb     = NULL;

/*
 * Deferred rearm flag.
 * When a UART error occurs, HAL internally aborts the DMA. We set this
 * flag in the ISR and perform the actual restart in Com_ReadAvailable()
 * (task context) to avoid locking up the CPU if a floating pin spams errors.
 */
static volatile bool        s_need_rearm    = false;


static void USART2_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* TX pin: push-pull, no pull */
    GPIO_InitStruct.Pin       = COM_USART2_TX_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(COM_USART2_GPIO_PORT, &GPIO_InitStruct);

    /* RX pin: push-pull, WITH PULL-UP to prevent floating when disconnected */
    GPIO_InitStruct.Pin       = COM_USART2_RX_PIN;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    HAL_GPIO_Init(COM_USART2_GPIO_PORT, &GPIO_InitStruct);
}

static void USART2_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_hdma_usart2_rx.Instance                 = DMA1_Stream2;
    s_hdma_usart2_rx.Init.Request             = DMA_REQUEST_USART2_RX;
    s_hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    s_hdma_usart2_rx.Init.Mode                = DMA_CIRCULAR;
    s_hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_LOW;
    s_hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_hdma_usart2_rx) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&s_huart2, hdmarx, s_hdma_usart2_rx);

    HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, COM_DMA_NVIC_PRIORITY, COM_DMA_NVIC_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);
}

/*  @brief  UART MSP init — called automatically by HAL_UART_Init()  */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (huart->Instance != USART2)
    {
        return;   /* not ours */
    }

    PeriphClkInitStruct.PeriphClockSelection      = RCC_PERIPHCLK_USART2;
    PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RCC_USART2_CLK_ENABLE();

    USART2_GPIO_Init();
    USART2_DMA_Init();

    HAL_NVIC_SetPriority(USART2_IRQn, COM_USART2_NVIC_PRIORITY, COM_USART2_NVIC_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

static Com_Status_t USART2_PeripheralInit(void)
{
    s_huart2.Instance                   = USART2;
    s_huart2.Init.BaudRate               = COM_USART2_BAUDRATE;
    s_huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    s_huart2.Init.StopBits               = UART_STOPBITS_1;
    s_huart2.Init.Parity                 = UART_PARITY_NONE;
    s_huart2.Init.Mode                   = UART_MODE_TX_RX;
    s_huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    s_huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    s_huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    s_huart2.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    s_huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&s_huart2) != HAL_OK)
    {
        return COM_ERROR;
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&s_huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return COM_ERROR;
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&s_huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return COM_ERROR;
    }
    if (HAL_UARTEx_DisableFifoMode(&s_huart2) != HAL_OK)
    {
        return COM_ERROR;
    }

    return COM_OK;
}

/*
 * Plain circular DMA — no idle-line detection.
 * The frame parser in frame.c handles all sync/framing.
 */
static Com_Status_t com_start_dma(void)
{
    HAL_StatusTypeDef st = HAL_UART_Receive_DMA(&s_huart2,
                                                 s_dma_rx_buf,
                                                 COM_RX_DMA_BUF_SIZE);
    return (st == HAL_OK) ? COM_OK : COM_ERROR;
}

Com_Status_t Com_Init(void)
{
    memset(s_dma_rx_buf, 0, sizeof(s_dma_rx_buf));
    memset(s_dma_tx_buf, 0, sizeof(s_dma_tx_buf));
    s_last_dma_pos = 0U;
    s_need_rearm   = false;

    if (USART2_PeripheralInit() != COM_OK)
    {
        return COM_ERROR;
    }

    return com_start_dma();
}

void Com_RegisterEventCallback(Com_EventCallback_t callback)
{
    s_event_cb = callback;
}

Com_Status_t Com_Send(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) || (length > COM_MAX_TX_SIZE))
    {
        return COM_ERROR;
    }

    memcpy(s_dma_tx_buf, data, length);

    /* Note: SCB_CleanDCache not strictly needed for polling TX,
     * but kept for safety if someone switches to DMA TX later */

    HAL_StatusTypeDef st = HAL_UART_Transmit(&s_huart2, s_dma_tx_buf, length, 100U);
    return (st == HAL_OK) ? COM_OK : COM_TIMEOUT;
}

uint16_t Com_ReadAvailable(uint8_t *dst, uint16_t max_length)
{
    if ((dst == NULL) || (max_length == 0U))
    {
        return 0U;
    }

    /* ---------------------------------------------------------
     * DEFERRED REARM (Self-Healing)
     * ---------------------------------------------------------
     * If an error occurred, HAL aborted the DMA. We catch the
     * flag here (task context), safely restart the DMA, and
     * reset our read position.
     * --------------------------------------------------------- */
    if (s_need_rearm)
    {
        s_need_rearm = false;
        s_last_dma_pos = 0U;

        if (com_start_dma() != COM_OK)
        {
            /* If rearm failed (e.g. HAL_BUSY), retry next poll */
            s_need_rearm = true;
        }
        return 0U; /* Just rearmed, no valid data to return this cycle */
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *)s_dma_rx_buf, COM_RX_DMA_BUF_SIZE);

    uint16_t current_pos = (uint16_t)(COM_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(s_huart2.hdmarx));

    if (current_pos == s_last_dma_pos)
    {
        return 0U;
    }

    uint16_t copied;

    if (current_pos > s_last_dma_pos)
    {
        uint16_t avail = (uint16_t)(current_pos - s_last_dma_pos);
        uint16_t n     = (avail > max_length) ? max_length : avail;
        memcpy(dst, &s_dma_rx_buf[s_last_dma_pos], n);
        s_last_dma_pos = (uint16_t)(s_last_dma_pos + n);
        copied = n;
    }
    else
    {
        uint16_t tail_avail = (uint16_t)(COM_RX_DMA_BUF_SIZE - s_last_dma_pos);
        uint16_t n          = (tail_avail > max_length) ? max_length : tail_avail;
        memcpy(dst, &s_dma_rx_buf[s_last_dma_pos], n);
        s_last_dma_pos = (uint16_t)((s_last_dma_pos + n) % COM_RX_DMA_BUF_SIZE);
        copied = n;
    }

    return copied;
}

Com_Status_t Com_Recover(void)
{
    /* Cancel any pending deferred rearm since we are doing it manually now */
    s_need_rearm = false;

    HAL_UART_AbortReceive(&s_huart2);

    s_last_dma_pos = 0U;
    memset(s_dma_rx_buf, 0, sizeof(s_dma_rx_buf));
    SCB_CleanDCache_by_Addr((uint32_t *)s_dma_rx_buf, COM_RX_DMA_BUF_SIZE);

    return com_start_dma();
}


/* ISR callbacks */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_FLAG(huart,
            UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);

        /*
         * HAL internally aborts the DMA reception on error.
         * We defer the restart to Com_ReadAvailable() (task context)
         * to avoid locking up the CPU if a floating pin generates
         * errors continuously.
         */
        s_need_rearm = true;

        if (s_event_cb != NULL)
        {
            s_event_cb(COM_EVENT_ERROR);
        }
    }
}

/* NOTE: HAL_UARTEx_RxEventCallback REMOVED — not used with plain circular DMA */

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart2);
}

void DMA1_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_usart2_rx);
}
