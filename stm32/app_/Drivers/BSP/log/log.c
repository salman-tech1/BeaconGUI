/*
 * log.c
 *
 *  Created on: Jul 21, 2026
 *      Author: Muhmmad Salman
 */

#include "log.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* =========================================================================
 * USART1 pin / peripheral config  (private to this file)
 * ========================================================================= */

#define LOG_UART_INSTANCE           USART1
#define LOG_UART_BAUDRATE           115200U

/* Default STM32 USART1 pins. Change these if your board wires USART1
 * elsewhere (e.g. PB6/PB7) — TX and RX are configured independently
 * below, so they don't need to share a GPIO port. */
#define LOG_UART_TX_GPIO_PORT       GPIOA
#define LOG_UART_TX_PIN             GPIO_PIN_9
#define LOG_UART_RX_GPIO_PORT       GPIOA
#define LOG_UART_RX_PIN             GPIO_PIN_10
#define LOG_UART_GPIO_AF            GPIO_AF7_USART1
#define LOG_UART_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()

#define LOG_UART_TIMEOUT_MS         50U    /* a full 160-byte line at 115200 baud takes ~14ms */
#define LOG_LINE_BUF_SIZE           160U   /* max formatted line length, incl. prefix + CRLF */

#define LOG_INCLUDE_TIMESTAMP       1      /* prefix each line with [ms since boot] */

/* =========================================================================
 * Private state
 * ========================================================================= */

static UART_HandleTypeDef s_log_uart_handle;
static Log_Level_t        s_log_level   = LOG_LEVEL_INFO;
static bool                s_log_enabled = true;
static bool                s_log_ready   = false;


static void        Log_GPIO_Init(void);
static void        Log_UART_Write(const char *data, uint16_t length);
static bool        Log_ShouldEmit(Log_Level_t level);
static const char *Log_LevelTag(Log_Level_t level);
static int          Log_BuildPrefix(char *buf, size_t buf_size, Log_Level_t level, const char *tag);
static void         Log_FormatFloat(char *buf, size_t buf_size, float value, uint8_t decimals);

static void Log_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};


     RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

       PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
       PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
       if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
       {
    	   // Err
         // Error_Handler();
       }

     /* Peripheral clock enable */
     __HAL_RCC_USART1_CLK_ENABLE();



    LOG_UART_GPIO_CLK_ENABLE();
    /* If TX and RX end up on different GPIO ports on your board, enable
     * that port's clock here too (e.g. __HAL_RCC_GPIOB_CLK_ENABLE()). */

    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = LOG_UART_GPIO_AF;

    GPIO_InitStruct.Pin = LOG_UART_TX_PIN;
    HAL_GPIO_Init(LOG_UART_TX_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LOG_UART_RX_PIN;
    HAL_GPIO_Init(LOG_UART_RX_GPIO_PORT, &GPIO_InitStruct);
}

void Log_Init(void)
{
    Log_GPIO_Init();

    __HAL_RCC_USART1_CLK_ENABLE();

    s_log_uart_handle.Instance                    = LOG_UART_INSTANCE;
    s_log_uart_handle.Init.BaudRate                = LOG_UART_BAUDRATE;
    s_log_uart_handle.Init.WordLength              = UART_WORDLENGTH_8B;
    s_log_uart_handle.Init.StopBits                = UART_STOPBITS_1;
    s_log_uart_handle.Init.Parity                  = UART_PARITY_NONE;
    s_log_uart_handle.Init.Mode                    = UART_MODE_TX_RX;
    s_log_uart_handle.Init.HwFlowCtl               = UART_HWCONTROL_NONE;
    s_log_uart_handle.Init.OverSampling            = UART_OVERSAMPLING_16;
    s_log_uart_handle.AdvancedInit.AdvFeatureInit  = UART_ADVFEATURE_NO_INIT;
    s_log_uart_handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_log_uart_handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;




    s_log_ready = (HAL_UART_Init(&s_log_uart_handle) == HAL_OK);

    if (HAL_UARTEx_SetTxFifoThreshold(&s_log_uart_handle, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
         {
    	s_log_ready = 0 ;
         }
         if (HAL_UARTEx_SetRxFifoThreshold(&s_log_uart_handle, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
         {
        	 s_log_ready = 0 ;
         }
         if (HAL_UARTEx_DisableFifoMode(&s_log_uart_handle) != HAL_OK)
         {
        	 s_log_ready = 0 ;
         }
    /* If this fails, s_log_ready stays false and every Log_* call below
     * quietly no-ops instead of hanging on an unconfigured peripheral. */
}

/* =========================================================================
 * Low-level transmit
 * ========================================================================= */

static void Log_UART_Write(const char *data, uint16_t length)
{
    if (!s_log_ready || (data == NULL) || (length == 0U)) {
        return;
    }
    (void)HAL_UART_Transmit(&s_log_uart_handle, (uint8_t *)data, length, LOG_UART_TIMEOUT_MS);
}

/* =========================================================================
 * Level / enable state
 * ========================================================================= */

void Log_SetLevel(Log_Level_t level)
{
    s_log_level = level;
}

Log_Level_t Log_GetLevel(void)
{
    return s_log_level;
}

void Log_Enable(bool enable)
{
    s_log_enabled = enable;
}

bool Log_IsEnabled(void)
{
    return s_log_enabled;
}

/**
 * @brief  Single gate all Log_* entry points funnel through: runtime
 *         enable flag, hardware readiness, and the configured level.
 */
static bool Log_ShouldEmit(Log_Level_t level)
{
    if (!s_log_enabled || !s_log_ready) {
        return false;
    }
    if (level == LOG_LEVEL_NONE) {
        return false;   /* NONE is a setting for Log_SetLevel(), not a message level */
    }
    return (level <= s_log_level);
}

static const char *Log_LevelTag(Log_Level_t level)
{
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERR";
        case LOG_LEVEL_WARN:  return "WRN";
        case LOG_LEVEL_INFO:  return "INF";
        case LOG_LEVEL_DEBUG: return "DBG";
        default:               return "?";
    }
}

static int Log_BuildPrefix(char *buf, size_t buf_size, Log_Level_t level, const char *tag)
{
#if LOG_INCLUDE_TIMESTAMP
    return snprintf(buf, buf_size, "[%8lu][%s][%s] ",
                     (unsigned long)HAL_GetTick(), Log_LevelTag(level), tag ? tag : "-");
#else
    (void)HAL_GetTick();
    return snprintf(buf, buf_size, "[%s][%s] ", Log_LevelTag(level), tag ? tag : "-");
#endif
}

/* =========================================================================
 * Float formatting — manual conversion, no dependency on newlib %f
 * ========================================================================= */

static void Log_FormatFloat(char *buf, size_t buf_size, float value, uint8_t decimals)
{
    int32_t  int_part;
    float    frac;
    bool     negative = false;
    uint32_t scale = 1U;
    uint32_t frac_int;

    if (decimals > 6U) {
        decimals = 6U;   /* keeps `scale` well inside uint32_t range */
    }
    if (value < 0.0f) {
        negative = true;
        value = -value;
    }

    int_part = (int32_t)value;
    frac     = value - (float)int_part;

    for (uint8_t i = 0; i < decimals; i++) {
        scale *= 10U;
    }

    frac_int = (uint32_t)((frac * (float)scale) + 0.5f);   /* round to nearest */
    if (frac_int >= scale) {
        /* rounding pushed the fraction into the next whole number, e.g. 2.9996 -> 3.000 */
        frac_int -= scale;
        int_part += 1;
    }

    if (decimals == 0U) {
        snprintf(buf, buf_size, "%s%ld", negative ? "-" : "", (long)int_part);
    } else {
        snprintf(buf, buf_size, "%s%ld.%0*lu",
                  negative ? "-" : "", (long)int_part, (int)decimals, (unsigned long)frac_int);
    }
}

/* =========================================================================
 * Public typed output primitives
 * ========================================================================= */

void Log_String(Log_Level_t level, const char *tag, const char *str)
{
    char line[LOG_LINE_BUF_SIZE];
    int  len;

    if (!Log_ShouldEmit(level) || (str == NULL)) {
        return;
    }

    len = Log_BuildPrefix(line, sizeof(line), level, tag);
    if ((len < 0) || ((size_t)len >= sizeof(line))) {
        return;
    }

    len += snprintf(&line[len], sizeof(line) - (size_t)len, "%s\r\n", str);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    Log_UART_Write(line, (uint16_t)len);
}

void Log_Number(Log_Level_t level, const char *tag, const char *label, int32_t value)
{
    char line[LOG_LINE_BUF_SIZE];
    int  len;

    if (!Log_ShouldEmit(level)) {
        return;
    }

    len = Log_BuildPrefix(line, sizeof(line), level, tag);
    if ((len < 0) || ((size_t)len >= sizeof(line))) {
        return;
    }

    len += snprintf(&line[len], sizeof(line) - (size_t)len,
                     "%s=%ld\r\n", label ? label : "value", (long)value);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    Log_UART_Write(line, (uint16_t)len);
}

void Log_Float(Log_Level_t level, const char *tag, const char *label, float value, uint8_t decimals)
{
    char line[LOG_LINE_BUF_SIZE];
    char num[24];
    int  len;

    if (!Log_ShouldEmit(level)) {
        return;
    }

    Log_FormatFloat(num, sizeof(num), value, decimals);

    len = Log_BuildPrefix(line, sizeof(line), level, tag);
    if ((len < 0) || ((size_t)len >= sizeof(line))) {
        return;
    }

    len += snprintf(&line[len], sizeof(line) - (size_t)len,
                     "%s=%s\r\n", label ? label : "value", num);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }

    Log_UART_Write(line, (uint16_t)len);
}

void Log_Printf(Log_Level_t level, const char *tag, const char *fmt, ...)
{
    char    line[LOG_LINE_BUF_SIZE];
    int     len;
    va_list args;

    if (!Log_ShouldEmit(level) || (fmt == NULL)) {
        return;
    }

    len = Log_BuildPrefix(line, sizeof(line), level, tag);
    if ((len < 0) || ((size_t)len >= sizeof(line))) {
        return;
    }

    va_start(args, fmt);
    len += vsnprintf(&line[len], sizeof(line) - (size_t)len, fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }
    if ((size_t)len <= sizeof(line) - 3U) {
        line[len++] = '\r';
        line[len++] = '\n';
    }

    Log_UART_Write(line, (uint16_t)len);
}
