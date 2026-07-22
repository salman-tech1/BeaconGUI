/*
 * touch.h
 *
 *  Created on: Jul 21, 2026
 *      Author: Muhmmad Salman
 *
 */

#ifndef TOUCH_H
#define TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** Maximum simultaneous touch points this layer reports.
 *  Sized for the currently active controller (GT911: up to 5).
 *  Raising this later (for a chip that supports more) does not
 *  break callers — TouchState_t simply has more valid slots. */
#define TOUCH_MAX_POINTS   5U

typedef enum {
    TOUCH_OK       =  0,   /**< Operation successful */
    TOUCH_ERROR    = -1,   /**< Generic error */
    TOUCH_TIMEOUT  = -2,   /**< Communication timeout */
    TOUCH_BUSY     = -3,   /**< Bus busy */
    TOUCH_ID_ERROR = -4,   /**< Device identity mismatch (wrong/missing chip) */
} Touch_Status_t;

/** Single touch point, in panel pixel coordinates (origin top-left) */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  area;      /**< Contact area (larger = wider finger contact) */
    uint8_t  track_id;  /**< Persistent ID for tracking a finger across frames */
} TouchPoint_t;

/** Complete touch state — populated by Touch_Read() */
typedef struct {
    uint8_t      touch_count;              /**< Number of active points (0..TOUCH_MAX_POINTS) */
    TouchPoint_t points[TOUCH_MAX_POINTS];
    bool         large_detect;             /**< True if a large object (e.g. palm) was detected */
} TouchState_t;

/**
 * @brief  Initialize the touch subsystem (GPIO, bus, reset sequence, ID check)
 * @retval TOUCH_OK on success, error code otherwise
 */
Touch_Status_t Touch_Init(void);

/**
 * @brief  Read the current touch state
 * @param  state  Pointer to a TouchState_t to populate
 * @retval TOUCH_OK if the read succeeded (touch_count may be 0)
 * @retval TOUCH_ERROR if state is NULL or the touch subsystem isn't ready
 * @retval other Touch_Status_t values on communication failure
 */
Touch_Status_t Touch_Read(TouchState_t *state);

/**
 * @brief  Check whether a touch interrupt is pending (set by the EXTI ISR)
 * @retval true if new data is waiting to be read via Touch_Read()
 */
bool Touch_IsPending(void);

/**
 * @brief  Touch EXTI interrupt entry point — call from your EXTIx_IRQHandler
 * @note   Minimal by design: only records that new data is available.
 *         Do the actual read (Touch_Read) from the main loop, not the ISR.
 */
void Touch_EXTI_Callback(void);

/**
 * @brief  Print the connected touch controller's identity over the debug UART
 */
void Touch_Debug_PingTest(void);

/**
 * @brief  Continuously print live touch coordinates — call from a debug loop/task
 */
void Touch_Debug_CoordinateMonitor(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_H */
