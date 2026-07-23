/**
 * @file lcd.h
 *
 */

#ifndef LCD_H
#define LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define LCD_WIDTH    ((uint32_t)1024U)
#define LCD_HEIGHT   ((uint32_t)600U)

#define LCD_FB_SIZE  ((uint32_t)(LCD_WIDTH * LCD_HEIGHT * 2U))
#define LCD_FB_A     ((uint32_t)0xC0000000U)
#define LCD_FB_B     ((uint32_t)(LCD_FB_A + LCD_FB_SIZE))

#define LCD_LAYER_0   ((uint8_t)0U)
#define LCD_MAX_LAYER ((uint8_t)1U)

#define LCD_LTDC_IRQ_PRIORITY  7
#define LCD_SWAP_TIMEOUT_MS    50U

typedef enum {
    LCD_OK      = 0,
    LCD_ERROR   = 1,
    LCD_TIMEOUT = 2,
} LCD_StatusTypeDef;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t fb_address;
    uint8_t  alpha;
} LCD_LayerConfig_t;

/* Callback invoked from LTDC line interrupt when buffer swap completes */
typedef void (*LCD_SwapCompleteCallback_t)(void);


LCD_StatusTypeDef   LCD_Init(void);
LCD_StatusTypeDef   LCD_LayerInit(uint8_t layer, const LCD_LayerConfig_t *cfg);
LCD_StatusTypeDef   LCD_SetFramebuffer(uint8_t layer, uint32_t fb_addr);

/**
 * @brief Request VSYNC swap to display exactly display_addr.
 * @param layer        Layer index (LCD_LAYER_0)
 * @param display_addr Address to display (= px_map from flush_cb)
 */
LCD_StatusTypeDef   LCD_SwapBuffers(uint8_t layer, uint32_t display_addr);
LCD_StatusTypeDef   LCD_WaitForSwap(uint8_t layer);
void                    LCD_VSYNC_Callback(LTDC_HandleTypeDef *hltdc);
LTDC_HandleTypeDef     *LCD_GetHandle(void);


void LCD_SetSwapCompleteCallback(LCD_SwapCompleteCallback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* LCD_H */
