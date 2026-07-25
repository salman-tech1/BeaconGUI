/*
 * gui.h
 *
 * Low-level service layer for the display, touch, and LVGL core.
 * Application code should NOT include this; use screen.h instead.
 */

#ifndef GUI_H
#define GUI_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tasks_config.h"

/* Events routed through the GUI task queue */
typedef enum {
    GUI_EVT_UPDATE_SENSOR_DATA  = 0,
    GUI_EVT_ALARM_TRIGGERED     = 1,
    GUI_EVT_NETWORK_STATUS      = 2,
    GUI_EVT_NAVIGATE_TO_SCREEN  = 3,
} GuiEventType_t;

#define GUI_READY_BIT  ((EventBits_t)0x01)

typedef struct {
    GuiEventType_t type;
    uint32_t       param;
    void          *data_ptr;
} GuiEvent_t;

void gui_init(void);
BaseType_t gui_send_event(const GuiEvent_t *event);
TaskHandle_t gui_get_handle(void);
void gui_wait_ready(void);
lv_display_t *gui_get_display(void);

#ifdef __cplusplus
}
#endif

#endif
