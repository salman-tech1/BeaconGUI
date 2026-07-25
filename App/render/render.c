/*
 * renderer.c
 *
 * Owns the application-level UI update loop. It acts as the consumer
 * of data (currently simulated, eventually from sensors) and translates
 * it into screen.h API calls.
 */

#include "render.h"
#include "screen.h"
#include "tasks_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>


static void renderer_task(void *pvParameters)
{
	    (void)pvParameters;
	    /* Boot OS Modules (Order matters: Screen depends on GUI) */
	     screen_system_init();

	    /* 1. Wait for the renderer to be ready */
	    screen_wait_ready();

	    /* 2. Show the dashboard */
	    screen_show(SCREEN_DASHBOARD);

	    /* 3. Simulate Data */
	    float kw = 1.0f;
	    uint32_t kwh = 0;
	    float time_step = 0.0f;

	    for (;;)
	    {
	    	 /* Create a smooth sine wave instead of a sharp sawtooth */
	    	  time_step += 0.3f; /* Speed of the wave */
	    	 /* Sinusoidal value oscillating between 1.0 and 5.0 */
	    	  kw = 3.0f + (2.0f * sinf(time_step));

	    	    kwh++;
	        /*
	         * UPDATE THE UI:
	         * No lv_lock()! No LVGL types!
	         * screen.c handles all thread-safety internally.
	         */
	        screen_set_battery(kw, (kw > 3.0f) ? SCREEN_BATTERY_DISCHARGING : SCREEN_BATTERY_CHARGING);

	        screen_set_production(kw * 2.0f, kwh * 2);
	        screen_set_consumption(kw * 1.5f, kwh);
	        screen_set_grid(kw * 1.5f,kwh*3,kwh*2) ;

	        screen_push_chart(SCREEN_CHART_PRODUCTION, (int32_t)(kw * 10));
	        screen_push_chart(SCREEN_CHART_CONSUMPTION, (int32_t)(kw * 15));
	        screen_push_chart(SCREEN_CHART_GRID, (int32_t)(kw * 20));

	        screen_set_weather("Islamabad", 17,"Cloudy");


	        vTaskDelay(pdMS_TO_TICKS(100));
	    }

}

void renderer_init(void)
{
    BaseType_t status = xTaskCreate(
        renderer_task,
        "Renderer",
        RENDERER_TASK_STACK_SIZE,
        NULL,
        RENDERER_TASK_PRIORITY,
        NULL
    );
    configASSERT(status == pdPASS);
}
