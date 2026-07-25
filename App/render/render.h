/*
 * renderer.h
 *
 * Application data-to-UI bridge.
 * Hides the FreeRTOS task details and data simulation from main.c.
 * In production, this module will consume real sensor data instead of
 * generating sine waves.
 */

#ifndef RENDER_H_
#define RENDER_H_


/*
 * Initializes the renderer task.
 * Call once from main() before vTaskStartScheduler().
 */
void renderer_init(void);



#endif /* RENDER_H_ */
