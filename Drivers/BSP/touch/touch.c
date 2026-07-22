/*
 * touch.c
 *
 *  Created on: Jul 21, 2026
 *      Author: Muhmmad Salman
 *
 */

#include "touch.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <string.h>

/*
 * GT911 REGISTER MAP / PINOUT / TIMING  (private to this file)
  */

#define GT911_REG_PRODUCT_ID        0x8140U   /* 4 bytes: "911\0" */
#define GT911_REG_POINT_INFO        0x814EU   /* 1 byte: status + touch count */
#define GT911_POINT_DATA_SIZE       8U        /* Bytes per touch point */
#define GT911_MAX_TOUCH_POINTS      5U        /* GT911 supports up to 5 points */

/** GT911 I2C 7-bit slave address.
 *  0x5D: INT pulled LOW during RST release (most common for 7" panels)
 *  0x14: INT pulled HIGH during RST release */
#define GT911_I2C_ADDR               (0x5D << 1)   /* HAL uses 8-bit addr format */
#define GT911_I2C_TIMEOUT            10U           /* ms; 10x margin over a ~1ms full read */

#define GT911_I2C_SCL_PIN            GPIO_PIN_12
#define GT911_I2C_SDA_PIN            GPIO_PIN_13
#define GT911_I2C_GPIO_PORT          GPIOD

#define GT911_INT_GPIO_PORT          GPIOE
#define GT911_INT_GPIO_PIN           GPIO_PIN_3
#define GT911_INT_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOE_CLK_ENABLE()
#define GT911_INT_EXTI_IRQn          EXTI3_IRQn
#define GT911_INT_EXTI_PRIORITY      5U    /* lower than LTDC VSYNC, higher than SysTick */
#define GT911_INT_EXTI_SUBPRIORITY   0U

#define GT911_RST_GPIO_PORT          GPIOH
#define GT911_RST_GPIO_PIN           GPIO_PIN_7
#define GT911_RST_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOH_CLK_ENABLE()

/* Datasheet timing — do not shrink these without checking the datasheet. */
#define GT911_RST_ASSERT_MS          10U   /* Datasheet: >=100us; 10ms for safety */
#define GT911_INT_HOLD_AFTER_RST_MS  10U   /* Datasheet: >=5ms; 10ms for safety */
#define GT911_BOOT_DELAY_MS          200U  /* Datasheet: ~50ms typical; 200ms for safety */

#define GT911_RECOVER_THRESHOLD      3U    /* consecutive I2C errors before bus recovery */


typedef enum {
    GT911_STATE_UNINIT = 0,
    GT911_STATE_RESET,
    GT911_STATE_BOOTING,
    GT911_STATE_READY,
    GT911_STATE_ERROR,
} GT911_State_t;

static I2C_HandleTypeDef      s_gt911_i2c_handle;
static volatile uint8_t       s_touch_pending   = 0U;
static uint8_t                s_i2c_error_count = 0U;
static GT911_State_t          s_gt911_state     = GT911_STATE_UNINIT;

 // Lower level gt911 driver
static Touch_Status_t GT911_I2C_Init(void);
static void            GT911_I2C_BusRecover(void);
static Touch_Status_t GT911_WriteReg(uint16_t reg_addr, const uint8_t *data, uint16_t length);
static Touch_Status_t GT911_ReadReg(uint16_t reg_addr, uint8_t *data, uint16_t length);
static Touch_Status_t GT911_ClearStatus(void);
static Touch_Status_t GT911_VerifyID(void);
static Touch_Status_t GT911_HardwareInit(void);
static Touch_Status_t GT911_ReadTouchRaw(TouchState_t *state);
static void            GT911_GPIO_Init(void);
static void            GT911_INT_AsOutput(uint8_t level);
static void            GT911_INT_AsInput(void);
static void            GT911_RST_Assert(void);
static void            GT911_RST_Release(void);



static void GT911_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GT911_INT_GPIO_CLK_ENABLE();
    GT911_RST_GPIO_CLK_ENABLE();

    /* RST: output, push-pull, start HIGH (de-asserted) */
    HAL_GPIO_WritePin(GT911_RST_GPIO_PORT, GT911_RST_GPIO_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin   = GT911_RST_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_RST_GPIO_PORT, &GPIO_InitStruct);

    /* INT: output, push-pull, start LOW (selects I2C address 0x5D at RST release) */
    HAL_GPIO_WritePin(GT911_INT_GPIO_PORT, GT911_INT_GPIO_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = GT911_INT_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_INT_GPIO_PORT, &GPIO_InitStruct);
}

static void GT911_INT_AsOutput(uint8_t level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Set level BEFORE switching to output to avoid a glitch */
    HAL_GPIO_WritePin(GT911_INT_GPIO_PORT, GT911_INT_GPIO_PIN,
                       (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = GT911_INT_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_INT_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @note  Called once the reset sequence is done. From here on GT911 drives
 *        INT low when new touch data is ready; the EXTI ISR (wired to
 *        Touch_EXTI_Callback) catches the falling edge.
 */
static void GT911_INT_AsInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = GT911_INT_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT911_INT_GPIO_PORT, &GPIO_InitStruct);

    __HAL_GPIO_EXTI_CLEAR_IT(GT911_INT_GPIO_PIN);
    HAL_NVIC_SetPriority(GT911_INT_EXTI_IRQn, GT911_INT_EXTI_PRIORITY, GT911_INT_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(GT911_INT_EXTI_IRQn);
}

static void GT911_RST_Assert(void)
{
    HAL_GPIO_WritePin(GT911_RST_GPIO_PORT, GT911_RST_GPIO_PIN, GPIO_PIN_RESET);
}

static void GT911_RST_Release(void)
{
    HAL_GPIO_WritePin(GT911_RST_GPIO_PORT, GT911_RST_GPIO_PIN, GPIO_PIN_SET);
}


static Touch_Status_t GT911_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    PeriphClkInitStruct.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
     //   Log("[touch] I2C clock config failed  %s %d\r\n", __FILE__, __LINE__);
        return TOUCH_ERROR;
    }

    /* PD12 -> I2C4_SCL, PD13 -> I2C4_SDA */
    GPIO_InitStruct.Pin       = GT911_I2C_SCL_PIN | GT911_I2C_SDA_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GT911_I2C_GPIO_PORT, &GPIO_InitStruct);

    __HAL_RCC_I2C4_CLK_ENABLE();

    s_gt911_i2c_handle.Instance             = I2C4;
    s_gt911_i2c_handle.Init.Timing          = 0x10C0ECFF;
    s_gt911_i2c_handle.Init.OwnAddress1     = 0;
    s_gt911_i2c_handle.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_gt911_i2c_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_gt911_i2c_handle.Init.OwnAddress2     = 0;
    s_gt911_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_gt911_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_gt911_i2c_handle.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_gt911_i2c_handle) != HAL_OK) {
       // printf("[touch] HAL_I2C_Init failed  %s %d\r\n", __FILE__, __LINE__);
        return TOUCH_ERROR;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&s_gt911_i2c_handle, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
      //  printf("[touch] analog filter config failed  %s %d\r\n", __FILE__, __LINE__);
        return TOUCH_ERROR;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&s_gt911_i2c_handle, 0) != HAL_OK) {
      //  printf("[touch] digital filter config failed  %s %d\r\n", __FILE__, __LINE__);
        return TOUCH_ERROR;
    }

    s_i2c_error_count = 0U;
    return TOUCH_OK;
}

/**
 * @brief  Bit-bang I2C bus recovery for a stuck bus (e.g. SDA held low
 *         after a power-glitch mid-transfer). Not called automatically —
 *         triggered internally after GT911_RECOVER_THRESHOLD consecutive
 *         I2C failures.
 */
static void GT911_I2C_BusRecover(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = GT911_I2C_SCL_PIN | GT911_I2C_SDA_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GT911_I2C_GPIO_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SCL_PIN | GT911_I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Up to 9 SCL pulses to clock out a stuck byte */
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SCL_PIN, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SCL_PIN, GPIO_PIN_SET);
        HAL_Delay(1);
        if (HAL_GPIO_ReadPin(GT911_I2C_GPIO_PORT, GT911_I2C_SDA_PIN) == GPIO_PIN_SET) {
            break;  /* bus released */
        }
    }

    /* STOP condition: SDA low->high while SCL high */
    HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SDA_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GT911_I2C_GPIO_PORT, GT911_I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    HAL_I2C_DeInit(&s_gt911_i2c_handle);
    HAL_Delay(10);
    HAL_I2C_Init(&s_gt911_i2c_handle);
 //   printf("[touch] I2C bus recovery run  %s %d\r\n", __FILE__, __LINE__);
}

static Touch_Status_t GT911_WriteReg(uint16_t reg_addr, const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Write(
        &s_gt911_i2c_handle, GT911_I2C_ADDR, reg_addr, I2C_MEMADD_SIZE_16BIT,
        (uint8_t *)data, length, GT911_I2C_TIMEOUT);

    switch (hal_status) {
        case HAL_OK:
            s_i2c_error_count = 0U;
            return TOUCH_OK;
        case HAL_TIMEOUT:
           // printf("[touch] WriteReg 0x%04X TIMEOUT\r\n", reg_addr);
            ++s_i2c_error_count;
            return TOUCH_TIMEOUT;
        case HAL_BUSY:
           // printf("[touch] WriteReg 0x%04X BUSY\r\n", reg_addr);
            ++s_i2c_error_count;
            return TOUCH_BUSY;
        default:
            ++s_i2c_error_count;
            return TOUCH_ERROR;
    }
}

/**
 * @note  Polling mode (no DMA), so no D-cache maintenance is needed here.
 *        If this is ever switched to DMA, invalidate the D-cache on `data`
 *        after the transfer completes.
 */
static Touch_Status_t GT911_ReadReg(uint16_t reg_addr, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Read(
        &s_gt911_i2c_handle, GT911_I2C_ADDR, reg_addr, I2C_MEMADD_SIZE_16BIT,
        data, length, GT911_I2C_TIMEOUT);

    switch (hal_status) {
        case HAL_OK:
            s_i2c_error_count = 0U;
            return TOUCH_OK;
        case HAL_TIMEOUT:
         //   printf("[touch] ReadReg 0x%04X TIMEOUT\r\n", reg_addr);
            ++s_i2c_error_count;
            return TOUCH_TIMEOUT;
        case HAL_BUSY:
         //   printf("[touch] ReadReg 0x%04X BUSY\r\n", reg_addr);
            ++s_i2c_error_count;
            return TOUCH_BUSY;
        default:
        //    printf("[touch] ReadReg 0x%04X ERROR\r\n", reg_addr);
            ++s_i2c_error_count;
            return TOUCH_ERROR;
    }
}

/**
 * @note  MUST be called after every touch read. Until it is, GT911 keeps
 *        INT asserted, never refreshes the coordinate buffer, and the
 *        EXTI ISR fires continuously.
 */
static Touch_Status_t GT911_ClearStatus(void)
{
    uint8_t clear_val = 0x00U;
    return GT911_WriteReg(GT911_REG_POINT_INFO, &clear_val, 1U);
}

/**
 * @brief  Confirms the product ID register reads back "911" — validates
 *         both that I2C is working and that the right address was latched.
 */
static Touch_Status_t GT911_VerifyID(void)
{
    uint8_t product_id[4] = {0};
    Touch_Status_t status = GT911_ReadReg(GT911_REG_PRODUCT_ID, product_id, 4U);

    if (status != TOUCH_OK) {
        return status;
    }
    if ((product_id[0] != '9') || (product_id[1] != '1') || (product_id[2] != '1')) {
      //  printf("[touch] product ID mismatch  %s %d\r\n", __FILE__, __LINE__);
        return TOUCH_ID_ERROR;
    }
    return TOUCH_OK;
}

/**
 * @brief  Full GT911 bring-up: GPIO, reset/address-latch sequence, I2C
 *         init, ID verification. ~220ms total — a one-time startup cost,
 *         do not call from a time-critical path.
 *
 *   t=0      GPIO init (INT=output LOW, RST=HIGH)
 *   t=6ms    RST -> LOW (reset asserted)
 *   t=16ms   RST -> HIGH (GT911 latches INT=LOW -> address 0x5D)
 *   t=26ms   INT reconfigured as EXTI input (address window closed)
 *   t=226ms  I2C init + product ID verification -> READY
 */
static Touch_Status_t GT911_HardwareInit(void)
{
    Touch_Status_t status;
    s_gt911_state = GT911_STATE_RESET;

    GT911_GPIO_Init();
    GT911_I2C_BusRecover();   /* clock out any byte stuck from a previous boot */
    HAL_Delay(5);

    GT911_RST_Assert();
    HAL_Delay(GT911_RST_ASSERT_MS);

    s_gt911_state = GT911_STATE_BOOTING;
    GT911_RST_Release();                       /* GT911 latches INT=LOW here -> 0x5D */
    HAL_Delay(GT911_INT_HOLD_AFTER_RST_MS);     /* hold INT during the latch window */

    GT911_INT_AsInput();
    HAL_Delay(GT911_BOOT_DELAY_MS);

    if (GT911_I2C_Init() != TOUCH_OK) {
        s_gt911_state = GT911_STATE_ERROR;
        return TOUCH_ERROR;
    }

    status = TOUCH_ERROR;
    for (int i = 0; i < 3; i++) {
        status = GT911_VerifyID();
        if (status == TOUCH_OK) {
            break;
        }
    //    printf("[touch] ID verify attempt %d failed, retrying\r\n", i + 1);
        HAL_I2C_DeInit(&s_gt911_i2c_handle);
        HAL_Delay(5U);
        if (GT911_I2C_Init() != TOUCH_OK) {
            break;
        }
        HAL_Delay(20U);
    }

    if (status != TOUCH_OK) {
//        printf("[touch] Init FAILED after 3 attempts at I2C addr 0x%02X\r\n"
//               "        Checklist:\r\n"
//               "        1. Pull-ups on PD12(SCL)/PD13(SDA)? (4.7k recommended)\r\n"
//               "        2. NACK: wrong address? try 0x14 instead of 0x5D\r\n"
//               "        3. BUSY: SDA stuck? power-cycle the board\r\n"
//               "        4. Verify RST=PH7 and INT=PE3 wiring against schematic\r\n",
//               GT911_I2C_ADDR);
        s_gt911_state = GT911_STATE_ERROR;
        return status;
    }

    status = GT911_ClearStatus();
    if (status != TOUCH_OK) {
        s_gt911_state = GT911_STATE_ERROR;
        return status;
    }

    s_gt911_state = GT911_STATE_READY;
   // printf("[touch] GT911 ready\r\n");
    return TOUCH_OK;
}

/**
 * @brief  Read + parse one touch frame straight into the public TouchState_t.
 *
 * @details Single 41-byte burst read (status + all 5 points) instead of
 *          six separate reads: fewer I2C address phases, one atomic
 *          snapshot, ~1ms instead of ~5ms. Status register is cleared
 *          immediately after the read (not after parsing) so GT911 can
 *          start preparing the next sample without losing scan time.
 */
static Touch_Status_t GT911_ReadTouchRaw(TouchState_t *state)
{
    Touch_Status_t status;
    uint8_t zero = 0x00U;
    uint8_t buf[1U + GT911_MAX_TOUCH_POINTS * GT911_POINT_DATA_SIZE];
    uint8_t touch_count;

    memset(state, 0, sizeof(*state));

    if (s_gt911_state != GT911_STATE_READY) {
        s_touch_pending = 0U;
        return TOUCH_ERROR;
    }

    status = GT911_ReadReg(GT911_REG_POINT_INFO, buf, sizeof(buf));
    if (status != TOUCH_OK) {
        s_touch_pending = 0U;

        if (s_i2c_error_count >= GT911_RECOVER_THRESHOLD) {

        //	Log_Printf(LOG_LEVEL_INFO,"GT911_ReadTouchRaw","%d consecutive errors, triggering I2C recovery",s_i2c_error_count);

            HAL_I2C_DeInit(&s_gt911_i2c_handle);
            HAL_Delay(5U);
            GT911_I2C_BusRecover();

            if (GT911_I2C_Init() != TOUCH_OK) {
                s_gt911_state = GT911_STATE_ERROR;
                return TOUCH_ERROR;
            }
            HAL_Delay(5U);
            /* best-effort clear; GT911 may or may not ACK right after recovery */
            HAL_I2C_Mem_Write(&s_gt911_i2c_handle, GT911_I2C_ADDR, GT911_REG_POINT_INFO,
                               I2C_MEMADD_SIZE_16BIT, &zero, 1U, 20U);
            s_i2c_error_count = 0U;

            return TOUCH_OK;
        }
        return status;
    }

    s_touch_pending = 0U;
    (void)GT911_ClearStatus();

    if (!(buf[0] & 0x80U)) {
        /* buffer-status bit not set: no new sample (e.g. polled without INT) */
        return TOUCH_OK;
    }

    state->large_detect = (buf[0] & 0x40U) ? true : false;
    touch_count = buf[0] & 0x0FU;
    if (touch_count > TOUCH_MAX_POINTS) {
        touch_count = TOUCH_MAX_POINTS;
    }
    state->touch_count = touch_count;

    /*
     * Per point (8 bytes, starting at buf[1]):
     *   byte0 = track_id[3:0] | reserved
     *   byte1,2 = X (12-bit, byte2[3:0] is the high nibble)
     *   byte3,4 = Y (12-bit, byte4[3:0] is the high nibble)
     *   byte5   = area
     *   byte6-7 = reserved
     */
    for (uint8_t i = 0; i < touch_count; i++) {
        const uint8_t *pt = &buf[1u + i * GT911_POINT_DATA_SIZE];
        state->points[i].track_id = pt[0] & 0x0FU;
        state->points[i].x        = (uint16_t)(((uint16_t)(pt[2] & 0x0FU) << 8U) | pt[1]);
        state->points[i].y        = (uint16_t)(((uint16_t)(pt[4] & 0x0FU) << 8U) | pt[3]);
        state->points[i].area     = pt[5];
    }

    return TOUCH_OK;
}



Touch_Status_t Touch_Init(void)
{
    return GT911_HardwareInit();
}

Touch_Status_t Touch_Read(TouchState_t *state)
{
    if (state == NULL) {
        return TOUCH_ERROR;
    }
    return GT911_ReadTouchRaw(state);
}

bool Touch_IsPending(void)
{
    return (s_touch_pending != 0U);
}

void Touch_EXTI_Callback(void)
{
    /* ISR context: do exactly this and nothing more. */
    s_touch_pending = 1U;
}

void Touch_Debug_PingTest(void)
{
    uint8_t product_id[4] = {0};

    if (GT911_ReadReg(GT911_REG_PRODUCT_ID, product_id, 4U) == TOUCH_OK) {
       // Log_Printf(LOG_LEVEL_INFO,"Touch_Debug_PingTest","Product ID: %c%c%c\r\n", product_id[0], product_id[1], product_id[2]);
    } else {
//       Log_Printf(LOG_LEVEL_INFO,"Touch_Debug_PingTest","Communication FAILED. Check I2C addr (0x%02X), pull-ups, GPIO pins\r\n",
//               GT911_I2C_ADDR);
    }
}

void Touch_Debug_CoordinateMonitor(void)
{
    TouchState_t touch = {0};

    if (Touch_IsPending() && (Touch_Read(&touch) == TOUCH_OK)) {
        if (touch.touch_count > 0) {
//        	 Log_Printf(LOG_LEVEL_INFO,"Touch_Debug_CoordinateMonitor", "Count=%d P1: X=%4d Y=%4d Area=%3d ID=%d",
//                   touch.touch_count, touch.points[0].x, touch.points[0].y,
//                  touch.points[0].area, touch.points[0].track_id);

        } else {
          // Log_Printf(Log_Printf,"Touch_Debug_CoordinateMonitor"," Released\r\n");
        }
    }
}


void EXTI3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GT911_INT_GPIO_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GT911_INT_GPIO_PIN) {
    	Touch_EXTI_Callback();
    }

}
