/*
 * gui.h
 *
 *  Created on: Jul 22, 2026
 *      Author: Muhmmad Salman
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

/* ── Event types sent from other tasks → GUI task ───────────────────── */
typedef enum {
    GUI_EVT_UPDATE_SENSOR_DATA  = 0,
    GUI_EVT_ALARM_TRIGGERED     = 1,
    GUI_EVT_NETWORK_STATUS      = 2,
    GUI_EVT_NAVIGATE_TO_SCREEN  = 3,
    /* Add your application events here */
} GuiEventType_t;


/*
 * Set by the GUI task once hardware + LVGL + the initial screen are ready.
 * Other tasks that send GUI events should call gui_wait_ready() at their
 * startup to make sure LVGL is initialised before they send anything.
 */
#define GUI_READY_BIT  ((EventBits_t)0x01)


/* ── Event payload  */
typedef struct {
    GuiEventType_t type;
    uint32_t       param;     /* generic parameter — meaning depends on type */
    void          *data_ptr;  /* optional pointer to heap-allocated data */
} GuiEvent_t;


/**
 * @brief Create the GUI task and its IPC primitives.
 *
 * Call once from main(), before vTaskStartScheduler(). Creates the event
 * group, the event queue, and the GUI FreeRTOS task. All display/LVGL
 * hardware init happens later, inside the task itself (after the scheduler
 * is running), so no BSP hardware is touched from this call.
 */
void gui_init(void);

/**
 * @brief Send an event to the GUI task from any other task.
 *
 * Thread-safe. Non-blocking — drops the event if the queue is full.
 * The GUI task drains this queue at the start of every render cycle.
 *
 * @param event  Event to send. Copied into the queue.
 * @return pdTRUE on success, pdFALSE if queue was full.
 */
BaseType_t gui_send_event(const GuiEvent_t *event);

/**
 * @brief Get the GUI task handle (for direct task notifications if needed).
 */
TaskHandle_t gui_get_handle(void);

/**
 * @brief Block the calling task until the GUI is fully initialised.
 *
 * Call from any service task at its startup, before sending events.
 * Returns immediately if the GUI is already ready.
 * Uses xEventGroupWaitBits() — requires the scheduler to be running.
 */
void gui_wait_ready(void);

/**
 * @brief Return the LVGL display handle (valid once GUI_READY_BIT is set).
 */
lv_display_t *gui_get_display(void);

#ifdef __cplusplus
}
#endif


#endif
