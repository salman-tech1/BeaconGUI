/*
 * dma2d.c
 *
 *  Created on: Jul 20, 2026
 *      Author: Muhmmad Salman
 */


#include <stdio.h>
#include "main.h"
#include "dma2d.h"

/* ── Constants  */
#define CACHE_LINE_SIZE     32U    /* Cortex-M7 D-Cache line: 32 bytes */
#define DMA2D_TIMEOUT_MS    100U   /* Maximum wait for any transfer */
#define DMA2D_IRQ_PRIORITY  6U     /* Below LTDC (7) so pixel transfers can't starve VSYNC */

/* ── Forward declarations of HAL callbacks  */
void HAL_DMA2D_XferCpltCallback(DMA2D_HandleTypeDef *hdma2d);
void HAL_DMA2D_XferErrorCallback(DMA2D_HandleTypeDef *hdma2d);

/* ── Internal state types */
typedef enum {
    DMA2D_STATE_IDLE,
    DMA2D_STATE_TRANSFERRING,
    DMA2D_STATE_COMPLETE,
    DMA2D_STATE_ERROR
} DMA2D_State;

typedef struct {
    volatile DMA2D_State   state;
    volatile DMA2D_Status last_status;
    DMA2D_CompleteCB   callback;
    uint32_t               dst_cache_addr;
    uint32_t               dst_cache_size;
} DMA2D_Handle;

/* ── Static instances  */
static DMA2D_Handle        dma2d_h     = {0};
static DMA2D_HandleTypeDef dma2d_s = {0};

/**
 * @brief  Convert RGB565 → ARGB8888 for HAL DMA2D R2M pdata.
 *
 *  HAL_DMA2D_Start_IT() in R2M mode ALWAYS treats pdata as ARGB8888
 *  (0xAARRGGBB), regardless of dma2d_s.Init.ColorMode — it converts
 *  internally to the actual output format (RGB565 here). Since our
 *  public API accepts RGB565 from the caller, we must do this
 *  conversion before handing the color to HAL, or the channels land
 *  in the wrong byte positions and the fill comes out as the wrong
 *  color (or black).
 *
 *  Each channel's MSBs are replicated into the LSBs so max value is
 *  0xFF, not 0xF8/0xFC, for accurate full-brightness colors.
 */
static inline uint32_t rgb565_to_argb8888(uint16_t rgb565)
{
    uint32_t r5 = (rgb565 >> 11U) & 0x1FU;
    uint32_t g6 = (rgb565 >>  5U) & 0x3FU;
    uint32_t b5 = (rgb565 >>  0U) & 0x1FU;

    uint32_t r8 = (r5 << 3U) | (r5 >> 2U);
    uint32_t g8 = (g6 << 2U) | (g6 >> 4U);
    uint32_t b8 = (b5 << 3U) | (b5 >> 2U);

    return (0xFFU << 24U) | (r8 << 16U) | (g8 << 8U) | b8;
}

/* ── Cache helpers*/

/**
 * @brief Align an address DOWN to cache-line boundary; expand size UP
 *        so the aligned range covers the original [addr, addr+size).
 */
static void cache_align(uint32_t addr, uint32_t size,
                         uint32_t *out_addr, uint32_t *out_size)
{
    uint32_t aligned_addr = addr & ~(CACHE_LINE_SIZE - 1U);
    uint32_t offset       = addr - aligned_addr;
    uint32_t aligned_size = (size + offset + CACHE_LINE_SIZE - 1U)
                            & ~(CACHE_LINE_SIZE - 1U);
    *out_addr = aligned_addr;
    *out_size = aligned_size;
}

/**
 * @brief Calculate the true memory span of a strided 2D region.
 *        span = ((height - 1) * stride + width) * bpp
 *        This covers all cache lines DMA2D will read or write,
 *        including stride gaps that share cache lines with pixel data.
 */
static uint32_t region_span(uint16_t width, uint16_t height,
                              uint16_t stride, uint8_t bpp)
{
    return (((uint32_t)(height - 1U) * stride) + width) * bpp;
}

/* ══════════════════════════════════════════════════════════════════════
 *  HAL peripheral init (called only once)
 * ══════════════════════════════════════════════════════════════════════ */
static int32_t MX_DMA2D_Init(void)
{
    /* This must happen before HAL_DMA2D_Init(): nothing else in the
     * project enables the DMA2D peripheral clock. Without it, every
     * register write is a no-op, transfers never happen, the completion
     * IRQ never fires, and dma2d_wait() just spins until it times out. */
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_FORCE_RESET();
    __HAL_RCC_DMA2D_RELEASE_RESET();

    dma2d_s.Instance          = DMA2D;
    dma2d_s.Init.Mode         = DMA2D_R2M;
    dma2d_s.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    dma2d_s.Init.OutputOffset = 0U;

    if (HAL_DMA2D_Init(&dma2d_s) != HAL_OK) {
    	// TODO : LOG ERROR
    	return -1;
    }

    dma2d_s.XferCpltCallback  = HAL_DMA2D_XferCpltCallback;
    dma2d_s.XferErrorCallback = HAL_DMA2D_XferErrorCallback;

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  dma2d_init
 * ══════════════════════════════════════════════════════════════════════ */
DMA2D_Status dma2d_init(void)
{
    if (MX_DMA2D_Init() < 0) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    dma2d_h.state          = DMA2D_STATE_IDLE;
    dma2d_h.last_status    = DMA2D_OK;
    dma2d_h.callback       = NULL;
    dma2d_h.dst_cache_addr = 0U;
    dma2d_h.dst_cache_size = 0U;

    /* Without this, HAL_DMA2D_Start_IT() never completes — the ISR that
     * flips dma2d_h.state to COMPLETE is never called. */
    HAL_NVIC_SetPriority(DMA2D_IRQn, DMA2D_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);

    return DMA2D_OK;
}

/*
 *  dma2d_fill — R2M: fill rectangle with solid RGB565 colour
 *  */
DMA2D_Status dma2d_fill(
    uint32_t dst_addr, uint16_t dst_stride,
    uint16_t x,        uint16_t y,
    uint16_t width,    uint16_t height,
    uint32_t color,    DMA2D_CompleteCB cb)
{
    if (width == 0 || height == 0) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }
    if ((x + width) > dst_stride) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }
    if (dma2d_h.state == DMA2D_STATE_TRANSFERRING) {
        return DMA2D_BUSY;
    }

    uint32_t dest = dst_addr + ((uint32_t)y * dst_stride + x) * 2U;

    /* Save destination for post-transfer cache invalidation */
    uint32_t span = region_span(width, height, dst_stride, 2U);
    cache_align(dest, span, &dma2d_h.dst_cache_addr, &dma2d_h.dst_cache_size);

    /* Configure DMA2D for R2M mode */
    dma2d_s.Instance          = DMA2D;
    dma2d_s.Init.Mode         = DMA2D_R2M;
    dma2d_s.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    dma2d_s.Init.RedBlueSwap  = DMA2D_RB_REGULAR;
    dma2d_s.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    dma2d_s.Init.OutputOffset = (uint32_t)dst_stride - (uint32_t)width;

    if (HAL_DMA2D_Init(&dma2d_s) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    dma2d_h.callback = cb;
    dma2d_h.state    = DMA2D_STATE_TRANSFERRING;

    /* pdata must be ARGB8888 for R2M regardless of output ColorMode */
    uint32_t argb = rgb565_to_argb8888((uint16_t)color);

    if (HAL_DMA2D_Start_IT(&dma2d_s, argb, dest, width, height) != HAL_OK) {
        dma2d_h.state    = DMA2D_STATE_ERROR;
        dma2d_h.callback = NULL;
        // TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    return DMA2D_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  dma2d_copy — M2M: copy pixel rectangle (same RGB565 format)
 *  src_addr and dst_addr are pre-computed region start addresses.
 * ══════════════════════════════════════════════════════════════════════ */
DMA2D_Status dma2d_copy(
    uint32_t src_addr, uint16_t src_stride,
    uint32_t dst_addr, uint16_t dst_stride,
    uint16_t width,    uint16_t height,
    DMA2D_CompleteCB cb)
{
    if (width == 0 || height == 0) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }
    if (src_addr == dst_addr) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }
    if (dma2d_h.state == DMA2D_STATE_TRANSFERRING) {
        return DMA2D_BUSY;
    }

    /* Clean source in D-Cache so DMA2D reads current data */
    uint32_t src_span = region_span(width, height, src_stride, 2U);
    uint32_t src_cache_addr, src_cache_size;
    cache_align(src_addr, src_span, &src_cache_addr, &src_cache_size);
    SCB_CleanDCache_by_Addr((uint32_t *)src_cache_addr, (int32_t)src_cache_size);

    /* Save destination for post-transfer invalidation */
    uint32_t dst_span = region_span(width, height, dst_stride, 2U);
    cache_align(dst_addr, dst_span, &dma2d_h.dst_cache_addr, &dma2d_h.dst_cache_size);

    /* Configure DMA2D for M2M mode */
    dma2d_s.Instance          = DMA2D;
    dma2d_s.Init.Mode         = DMA2D_M2M;
    dma2d_s.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    dma2d_s.Init.OutputOffset = (uint32_t)dst_stride - (uint32_t)width;

    if (HAL_DMA2D_Init(&dma2d_s) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputColorMode = DMA2D_INPUT_RGB565;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputAlpha     = 0xFFU;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].RedBlueSwap    = DMA2D_RB_REGULAR;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputOffset    =
        (uint32_t)src_stride - (uint32_t)width;

    if (HAL_DMA2D_ConfigLayer(&dma2d_s, DMA2D_FOREGROUND_LAYER) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    dma2d_h.callback = cb;
    dma2d_h.state    = DMA2D_STATE_TRANSFERRING;

    if (HAL_DMA2D_Start_IT(&dma2d_s, src_addr, dst_addr, width, height) != HAL_OK) {
        dma2d_h.state    = DMA2D_STATE_ERROR;
        dma2d_h.callback = NULL;
        // TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    return DMA2D_OK;
}

/*
 *  dma2d_blend — M2M_BLEND: ARGB8888 foreground over RGB565 background
 *  dst_addr is used as both BG source and output destination (in-place).
  */
DMA2D_Status dma2d_blend(
    uint32_t fg_addr,  uint16_t fg_stride,
    uint32_t dst_addr, uint16_t dst_stride,
    uint16_t width,    uint16_t height,
    DMA2D_CompleteCB cb)
{
    if (width == 0 || height == 0) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }
    if (dma2d_h.state == DMA2D_STATE_TRANSFERRING) {
        return DMA2D_BUSY;
    }

    /* Clean FG source (ARGB8888) — DMA2D reads from it */
    uint32_t fg_span = region_span(width, height, fg_stride, 4U);
    uint32_t fg_cache_addr, fg_cache_size;
    cache_align(fg_addr, fg_span, &fg_cache_addr, &fg_cache_size);
    SCB_CleanDCache_by_Addr((uint32_t *)fg_cache_addr, (int32_t)fg_cache_size);

    /* Clean BG region (RGB565) — DMA2D reads existing content as background */
    uint32_t bg_span = region_span(width, height, dst_stride, 2U);
    uint32_t bg_cache_addr, bg_cache_size;
    cache_align(dst_addr, bg_span, &bg_cache_addr, &bg_cache_size);
    SCB_CleanDCache_by_Addr((uint32_t *)bg_cache_addr, (int32_t)bg_cache_size);

    /* Save destination for post-transfer invalidation */
    dma2d_h.dst_cache_addr = bg_cache_addr;
    dma2d_h.dst_cache_size = bg_cache_size;

    /* Configure DMA2D for M2M_BLEND mode */
    dma2d_s.Instance          = DMA2D;
    dma2d_s.Init.Mode         = DMA2D_M2M_BLEND;
    dma2d_s.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    dma2d_s.Init.OutputOffset = (uint32_t)dst_stride - (uint32_t)width;

    if (HAL_DMA2D_Init(&dma2d_s) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    /* Foreground layer: ARGB8888, per-pixel alpha */
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputColorMode = DMA2D_INPUT_ARGB8888;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputAlpha     = 0xFFU;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].RedBlueSwap    = DMA2D_RB_REGULAR;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    dma2d_s.LayerCfg[DMA2D_FOREGROUND_LAYER].InputOffset    =
        (uint32_t)fg_stride - (uint32_t)width;

    if (HAL_DMA2D_ConfigLayer(&dma2d_s, DMA2D_FOREGROUND_LAYER) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    /* Background layer: RGB565 framebuffer (existing content) */
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].InputColorMode = DMA2D_INPUT_RGB565;
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].InputAlpha     = 0xFFU;
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].RedBlueSwap    = DMA2D_RB_REGULAR;
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].AlphaInverted  = DMA2D_REGULAR_ALPHA;
    dma2d_s.LayerCfg[DMA2D_BACKGROUND_LAYER].InputOffset    =
        (uint32_t)dst_stride - (uint32_t)width;

    if (HAL_DMA2D_ConfigLayer(&dma2d_s, DMA2D_BACKGROUND_LAYER) != HAL_OK) {
    	// TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    dma2d_h.callback = cb;
    dma2d_h.state    = DMA2D_STATE_TRANSFERRING;

    /* In-place blend: BG source and output are the same address */
    if (HAL_DMA2D_BlendingStart_IT(&dma2d_s,
                                    fg_addr,  /* FG source */
                                    dst_addr, /* BG source */
                                    dst_addr, /* output    */
                                    width, height) != HAL_OK) {
        dma2d_h.state    = DMA2D_STATE_ERROR;
        dma2d_h.callback = NULL;
        // TODO : LOG ERROR
        return DMA2D_ERROR;
    }

    return DMA2D_OK;
}

/*
 *  dma2d_wait — spin on dma2d_h.state (set by ISR callback)
 *
 *  Does NOT call HAL_DMA2D_PollForTransfer() — that races with the ISR.
 *  After transfer, invalidates D-Cache so CPU reads see DMA2D output.
  */
DMA2D_Status dma2d_wait(uint32_t timeout_ms)
{
    if (dma2d_h.state == DMA2D_STATE_IDLE) {
        return DMA2D_OK;
    }

    uint32_t t_start = HAL_GetTick();

    while (dma2d_h.state == DMA2D_STATE_TRANSFERRING) {
        if ((HAL_GetTick() - t_start) > timeout_ms) {
            dma2d_h.state       = DMA2D_STATE_ERROR;
            dma2d_h.last_status = DMA2D_TIMEOUT;
            return DMA2D_TIMEOUT;
        }
    }

    /* Invalidate D-Cache for destination region.
     * DMA2D wrote directly to SDRAM via AXI — D-Cache holds stale data.
     * CPU reads after this call see the freshly DMA2D-written pixels. */
    if (dma2d_h.dst_cache_size > 0U) {
        SCB_InvalidateDCache_by_Addr(
            (uint32_t *)dma2d_h.dst_cache_addr,
            (int32_t)dma2d_h.dst_cache_size);
    }

    return (dma2d_h.last_status == DMA2D_OK) ? DMA2D_OK : DMA2D_ERROR;
}

/*
 *  HAL callbacks — override of HAL weak symbols
  */
void HAL_DMA2D_XferCpltCallback(DMA2D_HandleTypeDef *hdma2d)
{
    (void)hdma2d;

    dma2d_h.state       = DMA2D_STATE_COMPLETE;
    dma2d_h.last_status = DMA2D_OK;

    if (dma2d_h.callback != NULL) {
        dma2d_h.callback();
        dma2d_h.callback = NULL;
    }

    dma2d_h.state = DMA2D_STATE_IDLE;
}

void HAL_DMA2D_XferErrorCallback(DMA2D_HandleTypeDef *hdma2d)
{
    (void)hdma2d;

    dma2d_h.state       = DMA2D_STATE_ERROR;
    dma2d_h.last_status = DMA2D_ERROR;

    // TODO : LOG ERROR

    if (dma2d_h.callback != NULL) {
        dma2d_h.callback();
        dma2d_h.callback = NULL;
    }

    dma2d_h.state = DMA2D_STATE_IDLE;
}




void DMA2D_IRQHandler(void)
{
  HAL_DMA2D_IRQHandler(&dma2d_s);
}
