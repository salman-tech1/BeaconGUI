/*
 * tasks_config.h
 *
 *  Created on: Jul 22, 2026
 *      Author: Muhmmad Salman
 */

#ifndef TASKS_CONFIG_H_
#define TASKS_CONFIG_H_

/* GUI Task: Must be the highest priority. If it starves, the screen freezes. */
#define GUI_TASK_STACK_WORDS  4096U              /* 16 KB — LVGL + DMA2D needs this */
#define GUI_TASK_PRIORITY     (tskIDLE_PRIORITY + 3U)  /* BOOSTED: Highest App Priority */
#define GUI_EVENT_QUEUE_DEPTH 8U

/* Renderer Task: Consumes data and pushes to LVGL. Needs decent stack to create screens. */
#define RENDERER_TASK_STACK_SIZE 2048U           /* DROPPED from 4096: Saves 8KB RAM, still totally safe */
#define RENDERER_TASK_PRIORITY   (tskIDLE_PRIORITY + 2U)

/* Sensor/Modbus Task: Polls I2C and UART. Slow, blocking I/O goes here. */
#define SENSOR_TASK_STACK_SIZE  1024U            /* BOOSTED from 512: Modbus frames + HAL I2C need >2KB */
#define SENSOR_TASK_PRIORITY    (tskIDLE_PRIORITY + 1U)  /* DROPPED: Lowest priority */


#endif /* TASKS_CONFIG_H_ */
