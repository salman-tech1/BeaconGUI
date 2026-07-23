/*
 * gui.c
 *
 *  Created on: Jul 22, 2026
 *      Author: Muhmmad Salman
 */

#include <stdio.h>
#include <string.h>


#include "gui.h"
#include "lcd.h"
#include "dma2d.h"
#include "touch.h"
#include "log.h"

// LVGL specific
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "lvgl.h"
#include "lv_draw_dma2d.h"

#include "theme.h"
#include "tasks_config.h"




static TaskHandle_t  s_task_handle     = NULL;
static QueueHandle_t s_event_queue     = NULL;


static lv_display_t      *s_disp  = NULL;
static TouchState_t s_touch = {0};

static EventGroupHandle_t s_gui_event_group = NULL;  /* GUI_READY_BIT set when init done */


static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);


static void gui_task_run(void *arg);
static void gui_hw_init(void);
static void handle_event(const GuiEvent_t *evt);
static void build_initial_ui(void);
static void gui_touch_visualizer(void);


/*
 * gui_init() — public entry point, called once from main() before the
 * scheduler starts. Only creates FreeRTOS primitives; all hardware/LVGL
 * setup happens later inside gui_task_run() -> gui_hw_init(), once the
 * scheduler is running.
 */
void gui_init(void)
{
    s_gui_event_group = xEventGroupCreate();
    configASSERT(s_gui_event_group != NULL);

    s_event_queue = xQueueCreate(GUI_EVENT_QUEUE_DEPTH, sizeof(GuiEvent_t));
    configASSERT(s_event_queue != NULL);

    BaseType_t r = xTaskCreate(gui_task_run,
                                "GUI",
                                GUI_TASK_STACK_WORDS,
                                NULL,
                                GUI_TASK_PRIORITY,
                                &s_task_handle);
    configASSERT(r == pdPASS);
}

void gui_wait_ready(void)
{
    /*
     * Block until GUI_READY_BIT is set by gui_task_run().
     * Safe to call from any task, after the scheduler starts.
     * Returns immediately if the GUI is already ready (bit already set).
     */
    if (s_gui_event_group == NULL) return;

    xEventGroupWaitBits(s_gui_event_group,
                        GUI_READY_BIT,
                        pdFALSE,        /* do NOT clear the bit after reading */
                        pdTRUE,
                        portMAX_DELAY);
}


BaseType_t gui_send_event(const GuiEvent_t *event)
{
    if (s_event_queue == NULL) return pdFALSE;
    /* Non-blocking: drop the event if the queue is full.
     * The GUI task will get the next update on the following cycle. */
    return xQueueSend(s_event_queue, event, 0);
}

TaskHandle_t gui_get_handle(void)
{
    return s_task_handle;
}

/*
 *  gui_task_run() — the FreeRTOS task entry point
 *
 *  This is where lv_timer_handler() lives.
 *  Nothing else in the firmware calls lv_timer_handler().
 */
static void gui_task_run(void *arg)
{
    (void)arg;

    gui_hw_init();

    /* Safe to call lv_* here — no other task touches LVGL yet.
     * In production, delegate to screen_dashboard_create() etc. */
    build_initial_ui();

    xEventGroupSetBits(s_gui_event_group, GUI_READY_BIT);
    Log_String(LOG_LEVEL_INFO, "gui_task_run", "Ready. Entering render loop.");

    for (;;) {
        /* Step A: drain event queue — apply data to widgets.
         * lv_lock/unlock protects widget state from concurrent access
         * by the lv_general_mutex (LV_OS_FREERTOS creates this).
         * The GUI task can re-acquire it because it is a recursive mutex. */
        GuiEvent_t evt;
        while (xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
            lv_lock();
            handle_event(&evt);
            lv_unlock();
        }

        /* Step B: run LVGL — render dirty areas + call flush_cb.
         * Returns milliseconds until the next scheduled LVGL timer.
         *   ~0  ms → currently rendering, sleep just 1ms to yield
         *   ~16 ms → idle between frames, sleep 16ms so idle task runs */
        uint32_t sleep_ms = lv_timer_handler();

        /* Step C: touch debug overlay (remove in production) */
        gui_touch_visualizer();

        /* Step D: sleep for exactly as long as LVGL requests.
         * Cap: 1ms minimum (always yield), 33ms maximum (never miss touch). */
        if (sleep_ms == 0 || sleep_ms > 33) sleep_ms = 33;
        if (sleep_ms < 1)                   sleep_ms = 1;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/*
 *  handle_event() — apply an incoming event to the UI
 *
 *  Called inside lv_lock() / lv_unlock(). Safe to call lv_* here.
 *  In production, dispatch to the active screen's event handler.
 *  */
static lv_obj_t *s_info_lbl = NULL;   /* example label — replace with your widgets */

static void handle_event(const GuiEvent_t *evt)
{
    switch (evt->type) {
        case GUI_EVT_UPDATE_SENSOR_DATA:
            if (s_info_lbl) {
                char buf[64];
                snprintf(buf, sizeof(buf),
                         "Sensor: %lu", (unsigned long)evt->param);
                lv_label_set_text(s_info_lbl, buf);
            }
            break;

        case GUI_EVT_ALARM_TRIGGERED:
            /* TODO: navigate to alarm screen */
            break;

        case GUI_EVT_NETWORK_STATUS:
            /* TODO: update network status icon */
            break;

        case GUI_EVT_NAVIGATE_TO_SCREEN:
            /* TODO: call screen_manager_show((ScreenId_t)evt->param) */
            break;

        default:
            break;
    }
}

/*
 *  build_initial_ui() — create the startup screen
 *
 *  In production, replace the body with:
 *    screen_dashboard_create();
 *    screen_settings_create();
 *    screen_manager_show(SCREEN_DASHBOARD);
 *  */
static void build_initial_ui(void)
{

	  /* 1. Initialize the theme (creates the styles) */
	    //theme_init();
	lv_obj_t *scr = lv_scr_act();
	    screen_dashboard_create(scr);

//		lv_obj_t *scr = lv_scr_act();
//	    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
//	    lv_obj_set_style_bg_opa(scr,   LV_OPA_COVER,           LV_PART_MAIN);
//
//	    lv_obj_t *title = lv_label_create(scr);
//	    lv_label_set_text(title, "BeaconView  |  STM32H743  |  LVGL v9.5");
//	    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
//	    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
//
//	    lv_obj_t *card = lv_obj_create(scr);
//	    lv_obj_set_size(card, 600, 180);
//	    lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
//	    lv_obj_set_style_bg_color(card,     lv_color_hex(0x16213E), LV_PART_MAIN);
//	    lv_obj_set_style_border_color(card, lv_color_hex(0x0F3460), LV_PART_MAIN);
//	    lv_obj_set_style_border_width(card, 2,                       LV_PART_MAIN);
//	    lv_obj_set_style_radius(card,       12,                      LV_PART_MAIN);
//	    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
//
//	    s_info_lbl = lv_label_create(card);
//	    lv_label_set_text(s_info_lbl, "Waiting for data...");
//	    lv_obj_set_style_text_color(s_info_lbl, lv_color_hex(0x00D4FF), LV_PART_MAIN);
//	    lv_obj_align(s_info_lbl, LV_ALIGN_CENTER, 0, 0);
}


static void lcd_swap_complete_cb(void)
{
    /* Called from LTDC line interrupt. LVGL allows this call from ISR. */
    lv_display_flush_ready(s_disp);
}

/*
 * gui_hw_init() — display hardware + LVGL init.
 * Runs inside the GUI task, after the scheduler has started, so BSP
 * hardware is only ever touched from task context (never from gui_init()).
 */
static void gui_hw_init(void)
{
    /* 1. LCD hardware */

    Log_String(LOG_LEVEL_INFO, "gui_hw_init", "Initializing GUI ");

    // Initialize LTDC
    if (LCD_Init() != LCD_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "LCD INIT ERROR ");
        for(;;) {}
    }

    /* Register the callback that will mark the flush as ready.
     * Must be done after s_disp is valid. */
    LCD_SetSwapCompleteCallback(lcd_swap_complete_cb);

    // Configure the LCD Layer
    LCD_LayerConfig_t cfg = {
        .x = 0, .y = 0,
        .width = LCD_WIDTH, .height = LCD_HEIGHT,
        .fb_address = LCD_FB_A, .alpha = 255,
    };

    // Initialize Layer
    if (LCD_LayerInit(LCD_LAYER_0, &cfg) != LCD_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "LCD_LayerInit ERROR ");
        for(;;) {}
    }

    // Set frame Buffer
    LCD_SetFramebuffer(LCD_LAYER_0, LCD_FB_A);

    /* 2. DMA2D */
    Log_String(LOG_LEVEL_INFO, "gui_hw_init", "DMA2D init");
    if (dma2d_init() != DMA2D_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "DMA2D init ERROR ");
        for(;;) {}
    }

    /* 3. LVGL init + tick source */
    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)HAL_GetTick);

    /* 4. LVGL DMA2D draw unit (polling mode, LV_USE_DRAW_DMA2D_INTERRUPT=0) */
    lv_draw_dma2d_init();

    /* 5. Display object */
    s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    if (s_disp == NULL) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "lv_display_create ERROR ");
        for(;;) {}
    }

    // Set display buffer
    lv_display_set_buffers(s_disp,
        (void *)LCD_FB_A, (void *)LCD_FB_B,
        LCD_FB_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);

    // lvgl configuration
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);
    lv_display_set_flush_cb(s_disp, flush_cb);

    /* 6. Touch */
    if (Touch_Init() == TOUCH_OK) {

        // create touch input device
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        Log_String(LOG_LEVEL_INFO, "gui_hw_init", "Touch  ready ");
    } else {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "Touch initERROR ");
    }

    Log_Printf(LOG_LEVEL_INFO, "gui_hw_init", "Init complete: %dx%d RGB565.", (int)LCD_WIDTH, (int)LCD_HEIGHT);
}

/*  flush_cb  */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    LCD_SwapBuffers(LCD_LAYER_0, (uint32_t)(uintptr_t)px_map);
    /* lv_display_flush_ready() for the final area is called from
     * lcd_swap_complete_cb(), triggered by the LTDC line IRQ once the
     * swap has actually happened. */
}

// Called when the touch is pressed
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (Touch_IsPending()) Touch_Read(&s_touch);
    if (s_touch.touch_count > 0) {
        data->point.x = s_touch.points[0].x;
        data->point.y = s_touch.points[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/*
 * gui_touch_visualizer() — debug touch dot overlay.
 * Internal only; called once per render cycle from gui_task_run().
 * Remove this call (and this function) before shipping to production.
 */
static void gui_touch_visualizer(void)
{
    static lv_obj_t *s_dot = NULL;
    if (s_dot == NULL) {
        s_dot = lv_obj_create(lv_screen_active());
        lv_obj_set_size(s_dot, 40, 40);
        lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_dot, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dot, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dot, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_dot, lv_color_white(), LV_PART_MAIN);
        lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_FLOATING |
                                LV_OBJ_FLAG_IGNORE_LAYOUT |
                                LV_OBJ_FLAG_HIDDEN);
    }
    if (s_touch.touch_count > 0) {
        lv_obj_set_pos(s_dot,
            s_touch.points[0].x - lv_obj_get_width(s_dot)  / 2,
            s_touch.points[0].y - lv_obj_get_height(s_dot) / 2);
        lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_display_t *gui_get_display(void) { return s_disp; }
