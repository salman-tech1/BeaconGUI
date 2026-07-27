/*
 * screen_menu.h
 *
 *  Created on: Jul 25, 2026
 *      Author: Muhmmad Salman
 *       * Global dropdown menu component.
 * Lives on lv_layer_top() so it can overlay and dim any active screen.
 */

#ifndef SCREEN_MENU_H
#define SCREEN_MENU_H




#include "lvgl.h"

/* Call this once during system boot, after LVGL is initialized */
void screen_menu_init(void);

/* Call this when the hamburger icon is clicked */
void screen_menu_toggle(void);

#endif /* SCREEN_MENU_H_ */
