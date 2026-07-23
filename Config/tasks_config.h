/*
 * tasks_config.h
 *
 *  Created on: Jul 22, 2026
 *      Author: Muhmmad Salman
 */

#ifndef TASKS_CONFIG_H_
#define TASKS_CONFIG_H_

#define GUI_TASK_STACK_WORDS  4096U              /* 16 KB — LVGL render path needs it */
#define GUI_TASK_PRIORITY     (tskIDLE_PRIORITY + 2U)
#define GUI_EVENT_QUEUE_DEPTH 8U

#define MODBUS_TASK_STACK_SIZE                512U   /* Words */
#define MODBUS_TASK_PRIORITY                  3U     /* Medium-high */


#endif /* TASKS_CONFIG_H_ */
