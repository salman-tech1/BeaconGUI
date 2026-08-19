/*
 * gui.c
 *
 * Hardware interface and LVGL render loop.
 * Owns the LTDC, DMA2D, and Touch driver initialization.
 * Runs the core lv_timer_handler() tick.
 */

#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "lcd.h"
#include "dma2d.h"
#include "touch.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "lvgl.h"
#include "lv_draw_dma2d.h"
#include "tasks_config.h"

static TaskHandle_t  s_task_handle     = NULL;
static QueueHandle_t s_event_queue     = NULL;
static lv_display_t *s_disp            = NULL;
static TouchState_t  s_touch           = {0};
static EventGroupHandle_t s_gui_event_group = NULL;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void gui_task_run(void *arg);
static void gui_hw_init(void);
static void handle_event(const GuiEvent_t *evt);
static void build_initial_ui(void);
static void gui_touch_visualizer(void);


StaticTask_t GuiTCB;
TaskHandle_t GuiHandle;


__attribute__((section(".dtcm_stack")))
StackType_t GuiTaskStack[GUI_TASK_STACK_WORDS];
/*
 * gui_init() must be called BEFORE vTaskStartScheduler().
 * It only creates OS primitives; hardware init is deferred to the task context.
 */
void gui_init(void)
{
    s_gui_event_group = xEventGroupCreate();
    configASSERT(s_gui_event_group != NULL);

    s_event_queue = xQueueCreate(GUI_EVENT_QUEUE_DEPTH, sizeof(GuiEvent_t));
    configASSERT(s_event_queue != NULL);
    size_t free = xPortGetFreeHeapSize();
    size_t min  = xPortGetMinimumEverFreeHeapSize();


//    BaseType_t r = xTaskCreate(gui_task_run, "GUI", GUI_TASK_STACK_WORDS,
//                               NULL, GUI_TASK_PRIORITY, &s_task_handle);
//    configASSERT(r == pdPASS);
    GuiHandle = xTaskCreateStatic(
    		gui_task_run,
            "Gui Task",
    		GUI_TASK_STACK_WORDS,
            NULL,
            GUI_TASK_PRIORITY,
			GuiTaskStack,
            &GuiTCB
        );

    	  configASSERT(GuiHandle != NULL);

}

void gui_wait_ready(void)
{
    if (s_gui_event_group == NULL) return;
    xEventGroupWaitBits(s_gui_event_group, GUI_READY_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

BaseType_t gui_send_event(const GuiEvent_t *event)
{
    if (s_event_queue == NULL) return pdFALSE;
    return xQueueSend(s_event_queue, event, 0);
}

TaskHandle_t gui_get_handle(void)
{
    return s_task_handle;
}

/*
 * Main GUI task loop.
 * Handles event queue draining, rendering, and input reading.
 */
static void gui_task_run(void *arg)
{
    (void)arg;

    gui_hw_init();
    build_initial_ui();

    /* Signal to other tasks that LVGL is ready to accept commands */
    xEventGroupSetBits(s_gui_event_group, GUI_READY_BIT);
    Log_String(LOG_LEVEL_INFO, "gui_task_run", "Ready. Entering render loop.");

    for (;;) {
        /* Process incoming inter-task commands */
        GuiEvent_t evt;
        while (xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
            lv_lock();
            handle_event(&evt);
            lv_unlock();
        }

        /*
         * Run LVGL internals (rendering + input polling).
         * Returns the time until the next scheduled timer.
         */
        lv_lock();
        uint32_t sleep_ms = lv_timer_handler();
        gui_touch_visualizer();
        lv_unlock();

        /* Yield to lower-priority tasks, but don't sleep longer than 33ms
         * to maintain responsive touch reading. */
        if (sleep_ms == 0 || sleep_ms > 33) sleep_ms = 33;
        if (sleep_ms < 1)                   sleep_ms = 1;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/* Placeholder for future queue-driven events (alarms, network drops, etc.) */
static void handle_event(const GuiEvent_t *evt)
{
    (void)evt;
    /* Currently, data updates use the direct mutex approach in screen.c */
}

/*
 * Sets up a blank black screen.
 * The actual UI is pushed on top of this by the application task via screen.c.
 */
static void build_initial_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
}

/* ISR callback from LTDC line interrupt */
static void lcd_swap_complete_cb(void)
{
    lv_display_flush_ready(s_disp);
}

/*
 * Hardware bootstrap.
 * Runs inside the GUI task context to avoid BSP calls before the scheduler is up.
 */
static void gui_hw_init(void)
{
    Log_String(LOG_LEVEL_INFO, "gui_hw_init", "Initializing GUI ");

    if (LCD_Init() != LCD_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "LCD INIT ERROR ");
        for(;;) {}
    }

    LCD_SetSwapCompleteCallback(lcd_swap_complete_cb);

    LCD_LayerConfig_t cfg = {
        .x = 0, .y = 0,
        .width = LCD_WIDTH, .height = LCD_HEIGHT,
        .fb_address = LCD_FB_A, .alpha = 255,
    };

    if (LCD_LayerInit(LCD_LAYER_0, &cfg) != LCD_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "LCD_LayerInit ERROR ");
        for(;;) {}
    }

    LCD_SetFramebuffer(LCD_LAYER_0, LCD_FB_A);

    if (dma2d_init() != DMA2D_OK) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "DMA2D init ERROR ");
        for(;;) {}
    }

    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)HAL_GetTick);
    lv_draw_dma2d_init();

    /* Display setup with double-buffering */
    s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    if (s_disp == NULL) {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "lv_display_create ERROR ");
        for(;;) {}
    }

    lv_display_set_buffers(s_disp, (void *)LCD_FB_A, (void *)LCD_FB_B,
                           LCD_FB_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_0);
    lv_display_set_flush_cb(s_disp, flush_cb);

    /* Touch input device */
    if (Touch_Init() == TOUCH_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        Log_String(LOG_LEVEL_INFO, "gui_hw_init", "Touch ready ");
    } else {
        Log_String(LOG_LEVEL_ERROR, "gui_hw_init", "Touch init ERROR ");
    }

    Log_Printf(LOG_LEVEL_INFO, "gui_hw_init", "Init complete: %dx%d RGB565.", (int)LCD_WIDTH, (int)LCD_HEIGHT);
}

/*
 * Double-buffer flush handler.
 * Only triggers the hardware swap on the final chunk of a render area.
 */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    /* Kick off DMA/LTDC swap. Completion is handled asynchronously by lcd_swap_complete_cb */
    LCD_SwapBuffers(LCD_LAYER_0, (uint32_t)(uintptr_t)px_map);
}

/*
 * Touch polling callback.
 * Only reads hardware if the touch controller's interrupt flag is set.
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (Touch_IsPending()) Touch_Read(&s_touch);

    // Log_Printf(LOG_LEVEL_INFO, "TOUCH","x : %d y : %d ",s_touch.points[0].x,s_touch.points[0].y) ;

    if (s_touch.touch_count > 0) {
        data->point.x = s_touch.points[0].x;
        data->point.y = s_touch.points[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/*
 * Debug overlay: Draws a hollow circle under the finger.
 * Placed on lv_layer_top() so it remains visible across screen switches.
 */
static void gui_touch_visualizer(void)
{
    static lv_obj_t *s_dot = NULL;
    if (s_dot == NULL) {
        s_dot = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_dot, 20, 20);
        lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dot, LV_OPA_TRANSP, LV_PART_MAIN); /* Hollow */
        lv_obj_set_style_border_color(s_dot, lv_color_hex(0x365e6f), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_dot, 2, LV_PART_MAIN);
        lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
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
