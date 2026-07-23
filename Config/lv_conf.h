/**
 * @file lv_conf.h
 */

#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#if 0 && defined(__ASSEMBLY__)
#include "my_include.h"
#endif

/* Color */
#define LV_COLOR_DEPTH 16

/* Stdlib */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_STDINT_INCLUDE    <stdint.h>
#define LV_STDDEF_INCLUDE    <stddef.h>
#define LV_STDBOOL_INCLUDE   <stdbool.h>
#define LV_INTTYPES_INCLUDE  <inttypes.h>
#define LV_LIMITS_INCLUDE    <limits.h>
#define LV_STDARG_INCLUDE    <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    #define LV_MEM_SIZE              (128U * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE  0
    #define LV_MEM_ADR               0
    #if LV_MEM_ADR == 0
        #undef LV_MEM_POOL_INCLUDE
        #undef LV_MEM_POOL_ALLOC
    #endif
#endif

/* HAL */
#define LV_DEF_REFR_PERIOD       16
#define LV_INDEV_DEF_READ_PERIOD 10
#define LV_DPI_DEF               170

/* ── Operating System ───────────────────────────────────────────────── */
#define LV_USE_OS   LV_OS_FREERTOS

#if LV_USE_OS == LV_OS_FREERTOS
    /*
     * Use counting semaphores instead of task notifications.
     * TASK_NOTIFY=1 shares slot 0 between _draw_info.sync and
     * interrupt_signal, causing deadlocks. Semaphores are independent.
     * This define must NOT be inside #if LV_USE_OS == LV_OS_NONE —
     * that condition is never true when OS=FREERTOS.
     */
    #define LV_USE_FREERTOS_TASK_NOTIFY 0
#endif

/* ── Rendering ──────────────────────────────────────────────────────── */
#define LV_DRAW_BUF_STRIDE_ALIGN     32
#define LV_DRAW_BUF_ALIGN            32
#define LV_DRAW_TRANSFORM_USE_MATRIX  0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (24 * 1024)
#define LV_DRAW_LAYER_MAX_MEMORY      0
#define LV_DRAW_THREAD_STACK_SIZE     (8 * 1024)
#define LV_DRAW_THREAD_PRIO           LV_THREAD_PRIO_HIGH

/* ── SW renderer ────────────────────────────────────────────────────── */
#define LV_USE_DRAW_SW  1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_SUPPORT_RGB565               1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED        0
    #define LV_DRAW_SW_SUPPORT_RGB565A8              1  /* needed: layer buffers for opacity/blend on arc, bar, chart */
    #define LV_DRAW_SW_SUPPORT_RGB888                0  /* no IMAGE/CANVAS decoders enabled */
    #define LV_DRAW_SW_SUPPORT_XRGB8888              0
    #define LV_DRAW_SW_SUPPORT_ARGB8888              0
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
    #define LV_DRAW_SW_SUPPORT_L8                    0
    #define LV_DRAW_SW_SUPPORT_AL88                  0
    #define LV_DRAW_SW_SUPPORT_A8                    0
    #define LV_DRAW_SW_SUPPORT_I1                    0
    #define LV_DRAW_SW_I1_LUM_THRESHOLD              127
    #define LV_DRAW_SW_DRAW_UNIT_CNT                 1
    #define LV_USE_DRAW_ARM2D_SYNC                   0
    #define LV_USE_NATIVE_HELIUM_ASM                 0
    #define LV_DRAW_SW_COMPLEX                       1
    #if LV_DRAW_SW_COMPLEX
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE         0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE         4
    #endif
    #define LV_USE_DRAW_SW_ASM                       LV_DRAW_SW_ASM_NONE
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS         0
#endif

/* ── DMA2D acceleration ─────────────────────────────────────────────── */
#define LV_USE_DRAW_DMA2D  1
#if LV_USE_DRAW_DMA2D
    #define LV_DRAW_DMA2D_HAL_INCLUDE    "stm32h7xx_hal.h"
    /*
     * 0 = polling mode.
     * lv_draw_dma2d_private.h: LV_DRAW_DMA2D_ASYNC = 0 when INTERRUPT=0.
     * check_transfer_completion() polls DMA2D->CR & DMA2D_CR_START.
     * No interrupt_signal created. No DMA2D ISR needed for LVGL.
     * BSP DMA2D HAL callbacks still work via HAL_DMA2D_IRQHandler in ISR.
     */
    #define LV_USE_DRAW_DMA2D_INTERRUPT  0
#endif

/* Other GPU (disabled) */
#define LV_USE_NEMA_GFX      0
#define LV_USE_PXP           0
#define LV_USE_G2D           0
#define LV_USE_DRAW_DAVE2D   0
#define LV_USE_DRAW_SDL      0
#define LV_USE_DRAW_VG_LITE  0
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_PPA           0

/* ── Logging ─────────────────────────────────────────────────────────── */
#define LV_USE_LOG 0
#if LV_USE_LOG
    #define LV_LOG_LEVEL             LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF            0
    #define LV_LOG_USE_TIMESTAMP     1
    #define LV_LOG_USE_FILE_LINE     1
    #define LV_LOG_TRACE_MEM         0
    #define LV_LOG_TRACE_TIMER       0
    #define LV_LOG_TRACE_INDEV       1
    #define LV_LOG_TRACE_DISP_REFR   1
    #define LV_LOG_TRACE_EVENT       0
    #define LV_LOG_TRACE_OBJ_CREATE  0
    #define LV_LOG_TRACE_LAYOUT      0
    #define LV_LOG_TRACE_ANIM        0
    #define LV_LOG_TRACE_CACHE       0
#endif

/* ── Asserts ─────────────────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL           1
#define LV_USE_ASSERT_MALLOC         1
#define LV_USE_ASSERT_STYLE          0
#define LV_USE_ASSERT_MEM_INTEGRITY  0
#define LV_USE_ASSERT_OBJ            0
#define LV_ASSERT_HANDLER_INCLUDE    <stdint.h>
#define LV_ASSERT_HANDLER            while(1);

/* ── Fonts ───────────────────────────────────────────────────────────── */

#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0



#define LV_FONT_UNSCII_8       0
#define LV_FONT_UNSCII_16      0
#define LV_FONT_CUSTOM_DECLARE
#define LV_FONT_DEFAULT        &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE  0
#define LV_USE_FONT_SUBPX      0
#define LV_USE_FONT_PLACEHOLDER  1

/* ── Text ────────────────────────────────────────────────────────────── */
#define LV_TXT_ENC                    LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS            " ,.;:-_)]}/"
#define LV_TXT_LINE_BREAK_LONG_LEN    0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN  3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD              "#"
#define LV_USE_BIDI                   0
#define LV_USE_ARABIC_PERSIAN_CHARS   0

/* ── Widgets ─────────────────────────────────────────────────────────── */
#define LV_WIDGETS_HAS_DEFAULT_VALUE  1
#define LV_USE_ANIMIMG     0
#define LV_USE_ARC         1   /* gauge */
#define LV_USE_BAR         1
#define LV_USE_BUTTON      1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR    0
#define LV_USE_CANVAS      0
#define LV_USE_CHART       1
#define LV_USE_CHECKBOX    0
#define LV_USE_DROPDOWN    1
#define LV_USE_IMAGE       1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD    0
#define LV_USE_LABEL       1   /* kept: DROPDOWN depends on it, and it's used for text on buttons/chart/scale */
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION  1
    #define LV_LABEL_LONG_TXT_HINT   1
    #define LV_LABEL_WAIT_CHAR_COUNT 3
#endif
#define LV_USE_LED         1
#define LV_USE_LINE        1
#define LV_USE_LIST        0
#define LV_USE_LOTTIE      0
#define LV_USE_MENU        0
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      0
#define LV_USE_SCALE       1
#define LV_USE_SLIDER      1
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_SWITCH      1
#define LV_USE_TABLE       0
#define LV_USE_TABVIEW     0
#define LV_USE_TEXTAREA    0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0
#define LV_USE_3DTEXTURE   0

/* ── Themes ──────────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK            0
    #define LV_THEME_DEFAULT_GROW            1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE  0  /* enable only if you call lv_theme_simple_init() somewhere */
#define LV_USE_THEME_MONO    0  /* enable only if you call lv_theme_mono_init() somewhere */

/* ── Layouts ─────────────────────────────────────────────────────────── */
#define LV_USE_FLEX  1
#define LV_USE_GRID  1

/* ── File system (disabled) ──────────────────────────────────────────── */
#define LV_FS_DEFAULT_DRIVER_LETTER '\0'
#define LV_USE_FS_STDIO    0
#define LV_USE_FS_POSIX    0
#define LV_USE_FS_WIN32    0
#define LV_USE_FS_FATFS    0
#define LV_USE_FS_MEMFS    0
#define LV_USE_FS_LITTLEFS 0

/* ── Image decoders (disabled) ───────────────────────────────────────── */
#define LV_USE_LODEPNG          0
#define LV_USE_BMP              0
#define LV_USE_TJPGD            0
#define LV_USE_GIF              0
#define LV_BIN_DECODER_RAM_LOAD 0
#define LV_USE_FREETYPE         0
#define LV_USE_TINY_TTF         0
#define LV_USE_VECTOR_GRAPHIC   0
#define LV_USE_THORVG_INTERNAL  0
#define LV_USE_THORVG_EXTERNAL  0
#define LV_USE_SVG              0
#define LV_USE_LZ4_INTERNAL     0

/* ── Sysmon ──────────────────────────────────────────────────────────── */
#define LV_USE_SYSMON  0
#if LV_USE_SYSMON
    /* lv_os_get_idle_percent() is available with LV_OS_FREERTOS */
    #define LV_SYSMON_GET_IDLE            lv_os_get_idle_percent
    #define LV_SYSMON_PROC_IDLE_AVAILABLE 0
    #define LV_USE_PERF_MONITOR           1
    #if LV_USE_PERF_MONITOR
        #define LV_USE_PERF_MONITOR_POS       LV_ALIGN_BOTTOM_RIGHT
        #define LV_USE_PERF_MONITOR_LOG_MODE  0
    #endif
    #define LV_USE_MEM_MONITOR  0
#endif

#define LV_USE_SNAPSHOT  0
#define LV_USE_OBSERVER  1
#define LV_USE_PROFILER  0
#define LV_USE_MONKEY    0

/* ── Devices (all disabled — using custom BSP) ───────────────────────── */
#define LV_USE_SDL          0
#define LV_USE_ST_LTDC      0
#define LV_USE_LINUX_FBDEV  0
#define LV_USE_NUTTX        0
#define LV_USE_EVDEV        0
#define LV_USE_LIBINPUT     0
#define LV_USE_ST7789       0
#define LV_USE_ILI9341      0
#define LV_USE_WINDOWS      0

/* ── Build ───────────────────────────────────────────────────────────── */
#define LV_BUILD_EXAMPLES  0
#define LV_BUILD_DEMOS     0

#endif /* LV_CONF_H */
#endif /* Content enable */
