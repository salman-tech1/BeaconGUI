/*
 * temp.c
 *
 */

#include "temp.h"
#include "stm32h7xx_hal.h"


/* ============================================================
 *  SHT31 REGISTER MAP / COMMANDS / PINOUT / TIMING  (private)
 * ============================================================ */

/* ADDR pin on this PCB is tied to GND -> 7-bit address 0x44 */
#define SHT31_I2C_ADDR              (0x44U << 1)
#define SHT31_I2C_TIMEOUT_MS        100U

#define SHT31_CMD_MEAS_HIGHREP      0x2400U   /* clock stretch OFF, high repeatability */
#define SHT31_CMD_SOFT_RESET        0x30A2U
#define SHT31_CMD_HEATER_ON         0x306DU
#define SHT31_CMD_HEATER_OFF        0x3066U
#define SHT31_CMD_READ_STATUS       0xF32DU

#define SHT31_MEAS_DELAY_MS         20U   /* datasheet max 15ms (high rep) + margin */
#define SHT31_RESET_DELAY_MS         5U   /* datasheet min 1.5ms + margin */

#define SHT31_RAW_DATA_SIZE          6U   /* [T_MSB][T_LSB][T_CRC][H_MSB][H_LSB][H_CRC] */

#define SHT31_CRC_POLYNOMIAL        0x31U
#define SHT31_CRC_INIT              0xFFU

#define SHT31_I2C_SCL_PIN            GPIO_PIN_4
#define SHT31_I2C_SDA_PIN            GPIO_PIN_11
#define SHT31_I2C_SCL_PORT           GPIOH
#define SHT31_I2C_SDA_PORT           GPIOB

typedef enum {
    SHT31_STATE_UNINIT = 0,
    SHT31_STATE_READY,
    SHT31_STATE_ERROR,
} SHT31_State_t;

static I2C_HandleTypeDef s_sht31_i2c_handle;
static SHT31_State_t     s_sht31_state = SHT31_STATE_UNINIT;

/* Lower level SHT31 driver — private to this file */
static Temp_Status_t SHT31_I2C_Init(void);
static Temp_Status_t SHT31_SendCommand(uint16_t cmd);
static Temp_Status_t SHT31_ReadRaw(uint8_t *buf, uint16_t len);
static Temp_Status_t SHT31_SoftReset(void);
static uint8_t        SHT31_ComputeCRC(const uint8_t *data, uint8_t len);
static bool           SHT31_CheckCRC(const uint8_t *data, uint8_t received_crc);
static void           SHT31_ConvertRaw(const uint8_t *raw, float *temp_c, float *hum_rh);


static Temp_Status_t SHT31_I2C_Init(void)
{
    GPIO_InitTypeDef         GPIO_InitStruct     = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
    PeriphClkInitStruct.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        return TEMP_ERROR;
    }

    __HAL_RCC_GPIOH_CLK_ENABLE();   /* PH4  = SCL */
    __HAL_RCC_GPIOB_CLK_ENABLE();   /* PB11 = SDA */

    GPIO_InitStruct.Pin       = SHT31_I2C_SCL_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(SHT31_I2C_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SHT31_I2C_SDA_PIN;
    HAL_GPIO_Init(SHT31_I2C_SDA_PORT, &GPIO_InitStruct);

    __HAL_RCC_I2C2_CLK_ENABLE();

    s_sht31_i2c_handle.Instance              = I2C2;
    s_sht31_i2c_handle.Init.Timing           = 0x009034B6;
    s_sht31_i2c_handle.Init.OwnAddress1      = 0;
    s_sht31_i2c_handle.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    s_sht31_i2c_handle.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    s_sht31_i2c_handle.Init.OwnAddress2      = 0;
    s_sht31_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_sht31_i2c_handle.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    s_sht31_i2c_handle.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_sht31_i2c_handle) != HAL_OK) {
        return TEMP_ERROR;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&s_sht31_i2c_handle, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        return TEMP_ERROR;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&s_sht31_i2c_handle, 0) != HAL_OK) {
        return TEMP_ERROR;
    }

    return TEMP_OK;
}

static Temp_Status_t SHT31_SendCommand(uint16_t cmd)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(cmd >> 8);    /* MSB first — SHT31 requirement */
    buf[1] = (uint8_t)(cmd & 0xFF);

    HAL_StatusTypeDef hal_status = HAL_I2C_Master_Transmit(
                                        &s_sht31_i2c_handle,
                                        SHT31_I2C_ADDR,
                                        buf, 2U,
                                        SHT31_I2C_TIMEOUT_MS);

    if (hal_status == HAL_TIMEOUT) return TEMP_TIMEOUT;
    if (hal_status == HAL_BUSY)    return TEMP_BUSY;
    if (hal_status != HAL_OK)      return TEMP_ERROR;
    return TEMP_OK;
}

static Temp_Status_t SHT31_ReadRaw(uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef hal_status = HAL_I2C_Master_Receive(
                                        &s_sht31_i2c_handle,
                                        SHT31_I2C_ADDR,
                                        buf, len,
                                        SHT31_I2C_TIMEOUT_MS);

    if (hal_status == HAL_TIMEOUT) return TEMP_TIMEOUT;
    if (hal_status == HAL_BUSY)    return TEMP_BUSY;
    if (hal_status != HAL_OK)      return TEMP_ERROR;
    return TEMP_OK;
}

static Temp_Status_t SHT31_SoftReset(void)
{
    Temp_Status_t status = SHT31_SendCommand(SHT31_CMD_SOFT_RESET);
    if (status != TEMP_OK) return status;

    HAL_Delay(SHT31_RESET_DELAY_MS);
    return TEMP_OK;
}

/* CRC-8, polynomial 0x31, init 0xFF. MSB-first, no reflection, no final XOR. */
static uint8_t SHT31_ComputeCRC(const uint8_t *data, uint8_t len)
{
    uint8_t crc = SHT31_CRC_INIT;

    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80U)
                ? (uint8_t)((crc << 1) ^ SHT31_CRC_POLYNOMIAL)
                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Sensor sends each pair as [MSB][LSB][CRC_of_MSB_LSB]. */
static bool SHT31_CheckCRC(const uint8_t *data, uint8_t received_crc)
{
    return (SHT31_ComputeCRC(data, 2U) == received_crc);
}

/* Datasheet 4.13:  T(°C) = -45 + 175*(raw/65535)   RH(%) = 100*(raw/65535) */
static void SHT31_ConvertRaw(const uint8_t *raw, float *temp_c, float *hum_rh)
{
    uint16_t raw_temp = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_hum  = ((uint16_t)raw[3] << 8) | raw[4];

    *temp_c = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *hum_rh = 100.0f          * ((float)raw_hum  / 65535.0f);
}


/* ============================================================
 *  PUBLIC API  (declared in temp.h)
 * ============================================================ */

Temp_Status_t Temp_Init(void)
{
    Temp_Status_t status = SHT31_I2C_Init();
    if (status != TEMP_OK) {
        s_sht31_state = SHT31_STATE_ERROR;
        return status;
    }

    status = SHT31_SoftReset();
    if (status != TEMP_OK) {
        s_sht31_state = SHT31_STATE_ERROR;
        return status;
    }

    /* Read the status register back so a dead/absent sensor is caught
     * here, at init, instead of on the first real measurement. */
    uint8_t stat_buf[3] = {0};

    status = SHT31_SendCommand(SHT31_CMD_READ_STATUS);
    if (status != TEMP_OK) {
        s_sht31_state = SHT31_STATE_ERROR;
        return status;
    }

    status = SHT31_ReadRaw(stat_buf, 3U);
    if (status != TEMP_OK) {
        s_sht31_state = SHT31_STATE_ERROR;
        return status;
    }

    if (!SHT31_CheckCRC(stat_buf, stat_buf[2])) {
        s_sht31_state = SHT31_STATE_ERROR;
        return TEMP_CRC_ERROR;
    }

    s_sht31_state = SHT31_STATE_READY;
    return TEMP_OK;
}

Temp_Status_t Temp_Read(TempData_t *data)
{
    if (data == NULL) {
        return TEMP_ERROR;
    }
    if (s_sht31_state != SHT31_STATE_READY) {
        return TEMP_NOT_READY;
    }

    uint8_t raw[SHT31_RAW_DATA_SIZE];

    Temp_Status_t status = SHT31_SendCommand(SHT31_CMD_MEAS_HIGHREP);
    if (status != TEMP_OK) return status;

    HAL_Delay(SHT31_MEAS_DELAY_MS);   /* sensor is converting, nothing to do but wait */

    status = SHT31_ReadRaw(raw, SHT31_RAW_DATA_SIZE);
    if (status != TEMP_OK) return status;

    if (!SHT31_CheckCRC(&raw[0], raw[2])) return TEMP_CRC_ERROR;   /* temperature bytes */
    if (!SHT31_CheckCRC(&raw[3], raw[5])) return TEMP_CRC_ERROR;   /* humidity bytes    */

    SHT31_ConvertRaw(raw, &data->temperature_c, &data->humidity_rh);
    return TEMP_OK;
}

Temp_Status_t Temp_SetHeater(bool enable)
{
    if (s_sht31_state != SHT31_STATE_READY) {
        return TEMP_NOT_READY;
    }
    uint16_t cmd = enable ? SHT31_CMD_HEATER_ON : SHT31_CMD_HEATER_OFF;
    return SHT31_SendCommand(cmd);
}

void Temp_Debug_PingTest(void)
{
    uint8_t buf[3] = {0};

    if (SHT31_SendCommand(SHT31_CMD_READ_STATUS) == TEMP_OK &&
        SHT31_ReadRaw(buf, 3U) == TEMP_OK &&
        SHT31_CheckCRC(buf, buf[2])) {
    	__unused
        uint16_t status_reg = ((uint16_t)buf[0] << 8) | buf[1];
        // printf("[temp] SHT31 status = 0x%04X\r\n", status_reg);
    } else {
       //  printf("[temp] SHT31 not responding — check I2C wiring/address (0x44/0x45)\r\n");
    }
}
