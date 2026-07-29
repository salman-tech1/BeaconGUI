/*
 * modbus_rtu.c
 *
 */

#include "modbus_rtu.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include "pinouts.h"



#define MODBUS_UART_AF               GPIO_AF7_USART3



#define MODBUS_WORD_LENGTH           UART_WORDLENGTH_8B
#define MODBUS_STOP_BITS             UART_STOPBITS_1
#define MODBUS_PARITY                UART_PARITY_NONE

/* DE_ASSERT_GUARD: DIR=HIGH to first bit on the wire. MAX485 driver
 * enable propagation <= 600ns; 50us is conservative.
 * DE_DEASSERT_GUARD: extra guard added on top of one char-time after
 * the last TX byte, before releasing the driver back to RECEIVE.      */
#define MODBUS_DE_ASSERT_GUARD_US     50U
#define MODBUS_DE_DEASSERT_GUARD_US   50U

#define MODBUS_TX_TIMEOUT_MS         400U   /* our own request frames are <=8 bytes  */
#define MODBUS_RESPONSE_TIMEOUT_MS   400U   /* max time to wait for the slave to start replying */
#define MODBUS_BODY_MARGIN_MS         20U   /* extra margin once the header has arrived */

#define MODBUS_HEADER_LEN              3U   /* addr + fc + (bytecount | exccode | reg_hi) */
#define MODBUS_MAX_FRAME_LEN         256U   /* max Modbus RTU frame size */

// Registers function codes
#define MODBUS_FC_READ_HOLDING_REGISTERS   0x03U
#define MODBUS_FC_READ_INPUT_REGISTERS     0x04U
#define MODBUS_FC_WRITE_SINGLE_REGISTER    0x06U
#define MODBUS_EXCEPTION_BIT               0x80U

/* Modbus spec caps a single read at 125 registers; also clamp to
 * whatever actually fits inside one frame. */
#define MODBUS_MAX_REGISTERS \
    (((MODBUS_MAX_FRAME_LEN - 5U) / 2U) > 125U ? 125U : ((MODBUS_MAX_FRAME_LEN - 5U) / 2U))

typedef enum {
    MODBUS_STATE_UNINIT = 0,
    MODBUS_STATE_READY,
    MODBUS_STATE_ERROR,
} Modbus_State_t;

static UART_HandleTypeDef s_modbus_uart_handle;
static Modbus_State_t     s_modbus_state        = MODBUS_STATE_UNINIT;
static uint32_t           s_modbus_char_time_us  = 1042U;   /* recomputed for the real baud in Init */

/* Lower level RS485/Modbus driver — private to this file */
static Modbus_Status_t Modbus_GPIO_UART_Init(uint32_t baudrate);
static void             Modbus_SetDirection(bool transmit);
static void             Modbus_DelayUs(uint32_t us);
static uint32_t         Modbus_CalcCharTimeUs(uint32_t baudrate);
static uint16_t         Modbus_CRC16(const uint8_t *buf, uint16_t len);
static Modbus_Status_t  Modbus_SendFrame(const uint8_t *frame, uint8_t len);
static Modbus_Status_t  Modbus_ReceiveFrame(uint8_t *buf, uint16_t *out_len);
static Modbus_Status_t  Modbus_Transact(uint8_t slave_addr, uint8_t function,
                                        const uint8_t *req, uint8_t req_len,
                                        uint8_t *resp, uint16_t *resp_len);


static Modbus_Status_t Modbus_GPIO_UART_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef         GPIO_InitStruct     = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection      = RCC_PERIPHCLK_USART3;
    PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        return MODBUS_ERROR;
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();   /* PC10 = TX, PC11 = RX   */
    __HAL_RCC_GPIOB_CLK_ENABLE();   /* PB8  = RS485 direction */

    GPIO_InitStruct.Pin       = MODBUS_UART_TX_PIN | MODBUS_UART_RX_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = MODBUS_UART_AF;
    HAL_GPIO_Init(MODBUS_UART_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = MODBUS_DIR_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;   /* DIR switching is slow, no need for fast slew */
    HAL_GPIO_Init(MODBUS_DIR_GPIO_PORT, &GPIO_InitStruct);

    __HAL_RCC_USART3_CLK_ENABLE();

    s_modbus_uart_handle.Instance                    = MODBUS_UART_INSTANCE;
    s_modbus_uart_handle.Init.BaudRate               = baudrate;
    s_modbus_uart_handle.Init.WordLength             = MODBUS_WORD_LENGTH;
    s_modbus_uart_handle.Init.StopBits               = MODBUS_STOP_BITS;
    s_modbus_uart_handle.Init.Parity                 = MODBUS_PARITY;
    s_modbus_uart_handle.Init.Mode                   = UART_MODE_TX_RX;
    s_modbus_uart_handle.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    s_modbus_uart_handle.Init.OverSampling           = UART_OVERSAMPLING_16;
    s_modbus_uart_handle.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    s_modbus_uart_handle.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    s_modbus_uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&s_modbus_uart_handle) != HAL_OK) {
        return MODBUS_ERROR;
    }

    /* Safe default: bus starts in RECEIVE mode */
    Modbus_SetDirection(false);

    return MODBUS_OK;
}

static void Modbus_SetDirection(bool transmit)
{
    HAL_GPIO_WritePin(MODBUS_DIR_GPIO_PORT, MODBUS_DIR_GPIO_PIN,
                       transmit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Busy-wait using the CPU cycle counter (DWT->CYCCNT). Assumes DWT has
 * already been enabled once at startup (DEMCR.TRCENA + DWT->CTRL.CYCCNTENA)
 * — this driver does not enable it itself, since it's normally shared
 * with other timing code in the application (see DWT_Init() in main.c). */
static void Modbus_DelayUs(uint32_t us)
{
    if (us == 0U) {
        return;
    }

    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t delay_cycles = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start_cycles) < delay_cycles) {
        __NOP();
    }
}

/* Character time = 10 bits (1 start + 8 data + 1 stop, 8N1) / baudrate,
 * expressed in microseconds, rounded up. Guards against baudrate == 0. */
static uint32_t Modbus_CalcCharTimeUs(uint32_t baudrate)
{
    if (baudrate == 0U) {
        return 1042U;   /* default: 9600 baud */
    }
    return (10U * 1000000U + baudrate - 1U) / baudrate;
}

/* CRC16 (Modbus), poly 0xA001, init 0xFFFF, LSB-first on the wire. */
static uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* Assert DIR, transmit blocking, hold DIR until the last stop bit has
 * actually left the wire, then release back to RECEIVE. */
static Modbus_Status_t Modbus_SendFrame(const uint8_t *frame, uint8_t len)
{
	// Set direction to transmission
    Modbus_SetDirection(true);
    // assert guard
    Modbus_DelayUs(MODBUS_DE_ASSERT_GUARD_US);

    // transmit frame : Blocking transmit
    HAL_StatusTypeDef hal_status = HAL_UART_Transmit(&s_modbus_uart_handle,
                                                       (uint8_t *)frame, len,
                                                       MODBUS_TX_TIMEOUT_MS);
    // setting the mode to recieving
    Modbus_DelayUs(s_modbus_char_time_us + MODBUS_DE_DEASSERT_GUARD_US);
    Modbus_SetDirection(false);

    if (hal_status == HAL_TIMEOUT) return MODBUS_TIMEOUT;
    if (hal_status != HAL_OK)      return MODBUS_ERROR;
    return MODBUS_OK;
}

/*
 * Reads one full Modbus response frame, blocking, in two stages:
 *
 *   1. Read the 3-byte header (addr, fc, third_byte). We wait up to
 *      MODBUS_RESPONSE_TIMEOUT_MS here since this covers the slave's own
 *      turnaround/processing time, which we don't otherwise know.
 *   2. The third header byte tells us exactly how many more bytes are
 *      coming (byte-count for a register read, fixed size for an
 *      exception or a write echo), so we read exactly that many more
 *      with a short, wire-speed-based timeout.
 *
 * This two-stage read is what lets a fully blocking/polled driver
 */
static Modbus_Status_t Modbus_ReceiveFrame(uint8_t *buf, uint16_t *out_len)
{
    HAL_StatusTypeDef hal_status;

    // Recieve modbus frame
    hal_status = HAL_UART_Receive(&s_modbus_uart_handle, buf, MODBUS_HEADER_LEN,
                                   MODBUS_RESPONSE_TIMEOUT_MS);
    if (hal_status == HAL_TIMEOUT) return MODBUS_TIMEOUT;
    if (hal_status != HAL_OK)      return MODBUS_ERROR;

    uint8_t  fc = buf[1];
    // remaining packets
    uint16_t remaining;
    // check if exception occured
    if (fc & MODBUS_EXCEPTION_BIT) {
        /* addr + fc + exception_code already read; CRC(2) remain */
        remaining = 2U;
    } else if (fc == MODBUS_FC_READ_HOLDING_REGISTERS || fc == MODBUS_FC_READ_INPUT_REGISTERS) {
        /* buf[2] = byte count; remaining = byte count + CRC(2) */
        remaining = (uint16_t)buf[2] + 2U;
    } else if (fc == MODBUS_FC_WRITE_SINGLE_REGISTER) {
        /* Full echo frame is 8 bytes total; 3 already read */
        remaining = 5U;
    } else {
        /* Unrecognised function code — let the caller's function-code
         * check in Modbus_Transact() reject it instead of guessing a
         * length here. */
        *out_len = MODBUS_HEADER_LEN;
        return MODBUS_OK;
    }

    if ((uint16_t)(MODBUS_HEADER_LEN + remaining) > MODBUS_MAX_FRAME_LEN) {
        return MODBUS_SHORT_FRAME;
    }

    uint32_t body_timeout_ms = ((uint32_t)remaining * s_modbus_char_time_us) / 1000U
                                + MODBUS_BODY_MARGIN_MS;

    // recieve the remaing bytes
    hal_status = HAL_UART_Receive(&s_modbus_uart_handle, &buf[MODBUS_HEADER_LEN],
                                   remaining, body_timeout_ms);
    if (hal_status == HAL_TIMEOUT) return MODBUS_TIMEOUT;
    if (hal_status != HAL_OK)      return MODBUS_ERROR;

    /* Invalidate the second half of the frame for the exact same reason */
   //   SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)buf, MODBUS_HEADER_LEN + remaining);


    *out_len = MODBUS_HEADER_LEN + remaining;
    return MODBUS_OK;
}

/* One full request/response cycle: flush stale RX state, send, receive,
 * then validate CRC / slave address / function code. Shared by every
 * public request function below. */
static Modbus_Status_t Modbus_Transact(uint8_t slave_addr, uint8_t function,
                                        const uint8_t *req, uint8_t req_len,
                                        uint8_t *resp, uint16_t *resp_len)
{
    if (s_modbus_state != MODBUS_STATE_READY) {
        return MODBUS_NOT_READY;
    }

    Modbus_Status_t status = Modbus_SendFrame(req, req_len);
    if (status != MODBUS_OK) {
        return status;
    }

    /*
     * During the TX phase (and the DIR-pin switch back to receive) the
     * USART's receiver can pick up self-noise / line reflections with
     * nothing reading RDR, leaving a stale byte — and possibly the
     * Overrun (ORE)/Framing (FE)/Noise (NE)/Parity (PE) flags — sitting
     * there once we're done transmitting. Flushing *before* we transmit
     * (like the old code here used to) doesn't help, since the noise
     * happens during and right after transmission, not before it.
     * Flush RDR and clear those flags right here, immediately before we
     * start listening for the real response.
     */
    __HAL_UART_CLEAR_FLAG(&s_modbus_uart_handle,
                           UART_CLEAR_OREF | UART_CLEAR_FEF |
                           UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(&s_modbus_uart_handle, UART_RXDATA_FLUSH_REQUEST);

    uint16_t len = 0U;
    status = Modbus_ReceiveFrame(resp, &len);
    if (status != MODBUS_OK) {
        return status;
    }

    if (len < 4U) {
        return MODBUS_SHORT_FRAME;
    }

    uint16_t calc_crc = Modbus_CRC16(resp, len - 2U);
    uint16_t recv_crc = (uint16_t)resp[len - 2U] | ((uint16_t)resp[len - 1U] << 8);
    if (calc_crc != recv_crc) {
        return MODBUS_CRC_ERROR;
    }

    if (resp[0] != slave_addr) {
        return MODBUS_BAD_SLAVE;
    }

    uint8_t fc = resp[1];
    if (fc & MODBUS_EXCEPTION_BIT) {
        return MODBUS_EXCEPTION;
    }
    if (fc != function) {
        return MODBUS_BAD_FUNCTION;
    }

    *resp_len = len;
    return MODBUS_OK;
}


Modbus_Status_t Modbus_RTU_Init(uint32_t baudrate)
{
    if (baudrate == 0U) {
        s_modbus_state = MODBUS_STATE_ERROR;
        return MODBUS_ERROR;
    }

    s_modbus_char_time_us = Modbus_CalcCharTimeUs(baudrate);

    Modbus_Status_t status = Modbus_GPIO_UART_Init(baudrate);
    if (status != MODBUS_OK) {
        s_modbus_state = MODBUS_STATE_ERROR;
        return status;
    }

    s_modbus_state = MODBUS_STATE_READY;
    return MODBUS_OK;
}

Modbus_Status_t Modbus_RTU_ReadHoldingRegisters(uint8_t slave_addr, uint16_t start_reg,
                                                 uint16_t qty, uint16_t *out_regs)
{
	// check for errors
    if (out_regs == NULL || qty == 0U || qty > MODBUS_MAX_REGISTERS) {
        return MODBUS_ERROR;
    }

    uint8_t  req[8];
    uint8_t  resp[MODBUS_MAX_FRAME_LEN];
    uint16_t resp_len = 0U;

    req[0] = slave_addr;
    req[1] = MODBUS_FC_READ_HOLDING_REGISTERS;
    req[2] = (uint8_t)(start_reg >> 8);
    req[3] = (uint8_t)(start_reg & 0xFFU);
    req[4] = (uint8_t)(qty >> 8);
    req[5] = (uint8_t)(qty & 0xFFU);
    uint16_t crc = Modbus_CRC16(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    // request for bytes
    Modbus_Status_t status = Modbus_Transact(slave_addr, MODBUS_FC_READ_HOLDING_REGISTERS,
                                              req, (uint8_t)sizeof(req), resp, &resp_len);
    if (status != MODBUS_OK) {
        return status;
    }

    uint8_t  byte_count   = resp[2];
    uint16_t reg_count    = byte_count / 2U;
    uint16_t expected_len = 3U + (uint16_t)byte_count + 2U;

    if (resp_len != expected_len || byte_count != qty * 2U) {
        return MODBUS_SHORT_FRAME;
    }

    /* Registers are big-endian (high byte first) on the wire */
    for (uint16_t i = 0; i < reg_count; i++) {
        out_regs[i] = ((uint16_t)resp[3U + i * 2U] << 8) | resp[4U + i * 2U];
    }

    return MODBUS_OK;
}

Modbus_Status_t Modbus_RTU_ReadInputRegisters(uint8_t slave_addr, uint16_t start_reg,
                                               uint16_t qty, uint16_t *out_regs)
{
    if (out_regs == NULL || qty == 0U || qty > MODBUS_MAX_REGISTERS) {
        return MODBUS_ERROR;
    }

    uint8_t  req[8];
    uint8_t  resp[MODBUS_MAX_FRAME_LEN];
    uint16_t resp_len = 0U;

    req[0] = slave_addr;
    req[1] = MODBUS_FC_READ_INPUT_REGISTERS;
    req[2] = (uint8_t)(start_reg >> 8);
    req[3] = (uint8_t)(start_reg & 0xFFU);
    req[4] = (uint8_t)(qty >> 8);
    req[5] = (uint8_t)(qty & 0xFFU);
    uint16_t crc = Modbus_CRC16(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    Modbus_Status_t status = Modbus_Transact(slave_addr, MODBUS_FC_READ_INPUT_REGISTERS,
                                              req, (uint8_t)sizeof(req), resp, &resp_len);
    if (status != MODBUS_OK) {
        return status;
    }

    uint8_t  byte_count   = resp[2];
    uint16_t reg_count    = byte_count / 2U;
    uint16_t expected_len = 3U + (uint16_t)byte_count + 2U;

    if (resp_len != expected_len || byte_count != qty * 2U) {
        return MODBUS_SHORT_FRAME;
    }

    for (uint16_t i = 0; i < reg_count; i++) {
        out_regs[i] = ((uint16_t)resp[3U + i * 2U] << 8) | resp[4U + i * 2U];
    }

    return MODBUS_OK;
}

Modbus_Status_t Modbus_RTU_WriteSingleRegister(uint8_t slave_addr, uint16_t reg_addr,
                                                uint16_t value)
{
    uint8_t  req[8];
    uint8_t  resp[MODBUS_MAX_FRAME_LEN];
    uint16_t resp_len = 0U;

    req[0] = slave_addr;
    req[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    req[2] = (uint8_t)(reg_addr >> 8);
    req[3] = (uint8_t)(reg_addr & 0xFFU);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFFU);
    uint16_t crc = Modbus_CRC16(req, 6U);
    req[6] = (uint8_t)(crc & 0xFFU);
    req[7] = (uint8_t)(crc >> 8);

    Modbus_Status_t status = Modbus_Transact(slave_addr, MODBUS_FC_WRITE_SINGLE_REGISTER,
                                              req, (uint8_t)sizeof(req), resp, &resp_len);
    if (status != MODBUS_OK) {
        return status;
    }

    /* Normal response echoes the request exactly: addr+fc+reg(2)+val(2)+crc(2) = 8 */
    if (resp_len != 8U) {
        return MODBUS_SHORT_FRAME;
    }

    return MODBUS_OK;
}
