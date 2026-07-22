/*
 * dma2d.h
 *
 *  Created on: Jul 20, 2026
 *      Author: Muhmmad Salman
 */

#ifndef LCD_DMA2D_H
#define LCD_DMA2D_H

#include <stdint.h>
typedef enum {
    DMA2D_OK      = 0,
	DMA2D_BUSY    = 1,
	DMA2D_TIMEOUT = 2,
	DMA2D_ERROR   = 3
} DMA2D_Status;

typedef void (*DMA2D_CompleteCB)(void);  // completion callback type


/**
 * @brief  Initialise DMA2D peripheral and NVIC.
 *         Must be called once before any other dma2d_* function.
 */
DMA2D_Status dma2d_init(void);

// Operations (non-blocking — return immediately after START)

/**
 * @brief  Fill a rectangle with a solid RGB565 colour (R2M mode).
 *
 * @param  dst_addr   Framebuffer base address (e.g. 0xD0000000)
 * @param  dst_stride Framebuffer width in pixels (e.g. 800)
 * @param  x          Rectangle left edge  in pixels
 * @param  y          Rectangle top edge   in pixels
 * @param  width      Rectangle width      in pixels
 * @param  height     Rectangle height     in pixels
 * @param  color      RGB565 colour value  (16-bit packed)
 * @param  cb         Completion callback — NULL for fire-and-forget
 */
DMA2D_Status dma2d_fill(
    uint32_t dst_addr,      // destination in framebuffer
    uint16_t dst_stride,    // framebuffer width in pixels
    uint16_t x, uint16_t y,
    uint16_t width, uint16_t height,
    uint32_t color,         // RGB565 color value
	DMA2D_CompleteCB cb     // called from ISR on completion
);

/**
 * @brief  Copy a pixel rectangle (M2M mode, same format).
 *
 *         src_addr and dst_addr are pre-computed start-of-region
 *         addresses — the caller adds (y * stride + x) * bpp
 *         before calling. No separate x,y parameters here.
 *
 * @param  src_addr   Source region start address
 * @param  src_stride Source buffer width in pixels (stride)
 * @param  dst_addr   Destination region start address
 * @param  dst_stride Destination buffer width in pixels
 * @param  width      Region width  in pixels
 * @param  height     Region height in pixels
 * @param  cb         Completion callback
 */
DMA2D_Status dma2d_copy(
    uint32_t src_addr,      // source image address
    uint16_t src_stride,    // source buffer width in pixels
    uint32_t dst_addr,
    uint16_t dst_stride,
    uint16_t width, uint16_t height,
	DMA2D_CompleteCB cb
);


/**
 * @brief  Blend an ARGB8888 foreground over an RGB565 framebuffer.
 *         Uses M2M_BLEND mode with per-pixel alpha from ARGB8888 source.
 *         BG input and output destination are the same address (in-place).
 *
 * @param  fg_addr    ARGB8888 source start address (pre-computed)
 * @param  fg_stride  FG source buffer width in pixels
 * @param  dst_addr   Framebuffer destination start address (pre-computed)
 *                    Also used as BG source — read and overwritten in-place
 * @param  dst_stride Framebuffer width in pixels
 * @param  width      Region width  in pixels
 * @param  height     Region height in pixels
 * @param  cb         Completion callback
 */
DMA2D_Status dma2d_blend(
    uint32_t fg_addr,       // ARGB8888 foreground
    uint16_t fg_stride,
    uint32_t dst_addr,      // RGB565 framebuffer (BG and output)
    uint16_t dst_stride,
    uint16_t width, uint16_t height,
	DMA2D_CompleteCB cb
);

// Synchronous wait (blocks with timeout)
/**
 * @brief  Block until current DMA2D transfer completes or timeout.
 *         Also performs D-Cache invalidation on the output region
 *         so subsequent CPU reads see DMA2D-written data.
 *
 * @param  timeout_ms  Maximum wait in milliseconds
 * @return DMA2D_OK on success, DMA2D_TIMEOUT on expiry
 */
DMA2D_Status dma2d_wait(uint32_t timeout_ms);


#endif /* LCD_DMA2D_H */
