/**
 * Created by : Muhammad Salman
 * @file lcd.c
 *
 *
 */

#include "lcd.h"
#include "stm32h7xx_hal.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Pin definitions (match your schematic) */
#define LCD_BCKLT_PIN   GPIO_PIN_6
#define LCD_BCKLT_PORT  GPIOH
#define LCD_RESET_PIN   GPIO_PIN_2
#define LCD_RESET_PORT  GPIOE

/* ── Internal state */
static LTDC_HandleTypeDef s_hltdc;

typedef struct {
    volatile uint8_t  swap_pending;
    volatile uint32_t active_addr;
    volatile uint32_t pending_addr;
} LCD_Layer_t;

static LCD_Layer_t s_layer[LCD_MAX_LAYER];

/* ── Private prototypes ─────────────────────────────────────────────── */
static void ltdc_clock_config(void);
static void ltdc_gpio_config(void);
static void lcd_reset(void);
static void lcd_backlight(bool on);

/*   LCD_Init : Bare-metal (no RTOS dependency)  */
LCD_StatusTypeDef LCD_Init(void)
{
    /* Enable LTDC peripheral clock */
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_LTDC_FORCE_RESET();
    __HAL_RCC_LTDC_RELEASE_RESET();

    /* LTDC timing for 1024x600 at 25 MHz pixel clock */
    s_hltdc.Instance                = LTDC;
    s_hltdc.Init.HSPolarity         = LTDC_HSPOLARITY_AL;
    s_hltdc.Init.VSPolarity         = LTDC_VSPOLARITY_AL;
    s_hltdc.Init.DEPolarity         = LTDC_DEPOLARITY_AL;
    s_hltdc.Init.PCPolarity         = LTDC_PCPOLARITY_IPC;
    s_hltdc.Init.HorizontalSync     = 0;
    s_hltdc.Init.VerticalSync       = 2;
    s_hltdc.Init.AccumulatedHBP     = 46;
    s_hltdc.Init.AccumulatedVBP     = 25;
    s_hltdc.Init.AccumulatedActiveW = 1070;
    s_hltdc.Init.AccumulatedActiveH = 625;
    s_hltdc.Init.TotalWidth         = 1110;
    s_hltdc.Init.TotalHeigh         = 635;
    s_hltdc.Init.Backcolor.Blue     = 20;
    s_hltdc.Init.Backcolor.Green    = 20;
    s_hltdc.Init.Backcolor.Red      = 20;

    /* HAL_LTDC_Init calls HAL_LTDC_MspInit which configures PLL3, GPIO,
     * panel reset and backlight — no need to call those separately here. */
    if (HAL_LTDC_Init(&s_hltdc) != HAL_OK) {
        printf("[LCD] ERROR: HAL_LTDC_Init failed\r\n");
        return LCD_ERROR;
    }

    /* Clear both framebuffers to black */
    memset((void *)LCD_FB_A, 0, LCD_FB_SIZE);
    memset((void *)LCD_FB_B, 0, LCD_FB_SIZE);

    /* Init layer tracking */
    s_layer[LCD_LAYER_0].active_addr  = LCD_FB_A;
    s_layer[LCD_LAYER_0].pending_addr = LCD_FB_A;
    s_layer[LCD_LAYER_0].swap_pending = 0;

    /* LTDC NVIC — priority is arbitrary in a bare-metal build; pick any
     * value that fits your application's overall interrupt priority scheme */
    HAL_NVIC_SetPriority(LTDC_IRQn, LCD_LTDC_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);

    /* Arm line event at start of VBlank (line 600) — NOT line 0
     * fire interrupt at rendering line 600 */
    HAL_LTDC_ProgramLineEvent(&s_hltdc, LCD_HEIGHT);

     // TODO : LOG

    return LCD_OK;
}

/*
 *  LCD_LayerInit */
LCD_StatusTypeDef LCD_LayerInit(uint8_t layer, const LCD_LayerConfig_t *cfg)
{
    LTDC_LayerCfgTypeDef lc = {0};

    lc.WindowX0        = cfg->x;
    lc.WindowX1        = cfg->x + cfg->width;
    lc.WindowY0        = cfg->y;
    lc.WindowY1        = cfg->y + cfg->height;
    lc.PixelFormat     = LTDC_PIXEL_FORMAT_RGB565;
    lc.FBStartAdress   = cfg->fb_address;
    lc.Alpha           = cfg->alpha;
    lc.Alpha0          = 0;
    lc.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    lc.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    lc.ImageWidth      = cfg->width;
    lc.ImageHeight     = cfg->height;
    lc.Backcolor.Blue  = 0;
    lc.Backcolor.Green = 0;
    lc.Backcolor.Red   = 0;

    if (HAL_LTDC_ConfigLayer(&s_hltdc, &lc, layer) != HAL_OK) {
        return LCD_ERROR;
    }
    return LCD_OK;
}

/*
 *  LCD_SetFramebuffer — immediate (no VSYNC sync, used during init)*/
LCD_StatusTypeDef LCD_SetFramebuffer(uint8_t layer, uint32_t fb_addr)
{
    if (HAL_LTDC_SetAddress(&s_hltdc, fb_addr, layer) != HAL_OK) {
        return LCD_ERROR;
    }
    s_layer[layer].active_addr = fb_addr;
    return LCD_OK;
}

/*
 *  LCD_SwapBuffers — request VSYNC swap to display_addr
 *
 *  display_addr = px_map from lvgl flush_cb.
 *  LVGL renders into px_map; LTDC must show THAT buffer.
 *  */
LCD_StatusTypeDef LCD_SwapBuffers(uint8_t layer, uint32_t display_addr)
{
    if (layer >= LCD_MAX_LAYER) return LCD_ERROR;

    s_layer[layer].pending_addr = display_addr;
    s_layer[layer].swap_pending = 1;

    return LCD_OK;
}

/*
 *  LCD_WaitForSwap — block (busy-wait) until LTDC ISR confirms swap
 *
 */
LCD_StatusTypeDef LCD_WaitForSwap(uint8_t layer)
{
    if (layer >= LCD_MAX_LAYER) return LCD_ERROR;
    if (!s_layer[layer].swap_pending) return LCD_OK;

    uint32_t tick_start = HAL_GetTick();

    while (s_layer[layer].swap_pending) {
        if ((HAL_GetTick() - tick_start) > LCD_SWAP_TIMEOUT_MS) {
        	// TODO : LOG
            s_layer[layer].swap_pending = 0;
            return LCD_TIMEOUT;
        }
    }
    return LCD_OK;
}

/*
 *  LCD_VSYNC_Callback — called from HAL_LTDC_LineEventCallback
 *  Fires at line LCD_HEIGHT (600) = start of VBlank.
 *  */
void LCD_VSYNC_Callback(LTDC_HandleTypeDef *hltdc)
{
    for (uint8_t i = 0; i < LCD_MAX_LAYER; i++) {
        if (s_layer[i].swap_pending) {
            /* Write new address to LTDC shadow register */
            LTDC_LAYER(hltdc, i)->CFBAR = s_layer[i].pending_addr;

            /* Immediate Reload — safe because we are in VBlank */
            hltdc->Instance->SRCR   = LTDC_SRCR_IMR;

            s_layer[i].active_addr  = s_layer[i].pending_addr;

            /* Clearing swap_pending last is what unblocks
             * LCD_WaitForSwap()'s polling loop */
            s_layer[i].swap_pending = 0;
        }
    }

    /* Re-arm for next VBlank */
    HAL_LTDC_ProgramLineEvent(hltdc, LCD_HEIGHT);
}

/* ── HAL_LTDC_LineEventCallback (weak override)  */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
    __HAL_LTDC_CLEAR_FLAG(hltdc, LTDC_FLAG_LI);
    LCD_VSYNC_Callback(hltdc);
}

/* ── LCD_GetHandle  */
LTDC_HandleTypeDef *LCD_GetHandle(void) { return &s_hltdc; }

/*
 *  HAL_LTDC_MspInit — called automatically by HAL_LTDC_Init
 *  Performs ALL hardware setup for LTDC: PLL3, GPIO, reset, backlight.
  */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;

    ltdc_clock_config(); /* PLL3 → 25 MHz pixel clock */
    ltdc_gpio_config();  /* All LTDC pins as AF14/AF13/AF9 */
    lcd_reset();         /* Hardware panel reset sequence */
    lcd_backlight(true); /* Enable backlight */
}

/* ── PLL3 → 25 MHz LTDC pixel clock ────────────────────────────────── */
static void ltdc_clock_config(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};

    pclk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    pclk.PLL3.PLL3M           = 25;
    pclk.PLL3.PLL3N           = 500;
    pclk.PLL3.PLL3P           = 2;
    pclk.PLL3.PLL3Q           = 2;
    pclk.PLL3.PLL3R           = 20;
    pclk.PLL3.PLL3RGE         = RCC_PLL3VCIRANGE_0;
    pclk.PLL3.PLL3VCOSEL      = RCC_PLL3VCOWIDE;
    pclk.PLL3.PLL3FRACN       = 0;

    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
    	// TODO : LOG
    }
}

/* ── LTDC GPIO configuration ────────────────────────────────────────── */
static void ltdc_gpio_config(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    /* GPIOI: VSYNC, HSYNC, G5, G6, G7, B5, B6, B7 */
    g.Alternate = GPIO_AF14_LTDC;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_5
          | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &g);

    /* GPIOF: DE (PF10) */
    g.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &g);

    /* GPIOH: R2–R5, G3 */
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &g);

    /* GPIOG: CLK (PG7), G0 (PG6) */
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOG, &g);

    /* GPIOG: R7 (PG12) — AF9 */
    g.Alternate = GPIO_AF9_LTDC;
    g.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &g);

    /* GPIOA: R6 (PA8) — AF13 */
    g.Alternate = GPIO_AF13_LTDC;
    g.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &g);

    /* GPIOE: G4 (PE11) — AF14 */
    g.Alternate = GPIO_AF14_LTDC;
    g.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOE, &g);

    /* Backlight and Reset as outputs */
    g.Mode      = GPIO_MODE_OUTPUT_PP;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = 0;

    HAL_GPIO_WritePin(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_RESET);
    g.Pin = LCD_RESET_PIN;
    HAL_GPIO_Init(LCD_RESET_PORT, &g);

    HAL_GPIO_WritePin(LCD_BCKLT_PORT, LCD_BCKLT_PIN, GPIO_PIN_RESET);
    g.Pin = LCD_BCKLT_PIN;
    HAL_GPIO_Init(LCD_BCKLT_PORT, &g);
}

/* ── Panel hardware reset */
static void lcd_reset(void)
{
    HAL_GPIO_WritePin(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
}

/* ── Backlight enable ───────────────────────────────────────────────── */
static void lcd_backlight(bool on)
{
    HAL_GPIO_WritePin(LCD_BCKLT_PORT, LCD_BCKLT_PIN, (GPIO_PinState)on);
    HAL_Delay(20);
}


/**
  * @brief This function handles LTDC global interrupt.
  */
void LTDC_IRQHandler(void)
{

  HAL_LTDC_IRQHandler(&s_hltdc);

}

