/*
 * screen_menu.c
 *
 *  Created on: Jul 25, 2026
 *      Author: Muhmmad Salman
 */


#include "screen_menu.h"
#include "screen.h" /* To trigger screen switches when items are clicked */
#include <stdlib.h>
#include <string.h>
/* Layout & Palette (Mirrors your dashboard design) */
#define MENU_Y_START    50      /* Starts exactly under the top bar */
#define MENU_HEIGHT     160     /* Fully extended height */
#define MENU_WIDTH      190     /* Don't span the whole screen, looks cleaner */
#define MENU_X_PAD      25

#define C_MENU_BG       lv_color_hex(0x111629) /* Arc track color as requested */
#define C_MENU_TEXT     lv_color_hex(0xE0E0E0) /* Dashboard text color */
#define C_MENU_PRESSED  lv_color_hex(0x3F6E7E) /* Divider/pressed color */
#define C_OVERLAY_BG    lv_color_hex(0x000000)

/* Guarded the same way screen_dashboard.c guards its fonts — don't assume
 * every Montserrat size is enabled in lv_conf.h. */
#if LV_FONT_MONTSERRAT_18
  #define FONT_MENU_ITEM &lv_font_montserrat_18
#elif LV_FONT_MONTSERRAT_20
  #define FONT_MENU_ITEM &lv_font_montserrat_20
#elif LV_FONT_MONTSERRAT_14
  #define FONT_MENU_ITEM &lv_font_montserrat_14
#else
  #define FONT_MENU_ITEM LV_FONT_DEFAULT
#endif

/* Static handles */
static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_menu_panel = NULL;
static bool s_is_open = false;

/* Forward declarations */
static void menu_anim_cb(void *var, int32_t v);
static void menu_close_ready_cb(lv_anim_t *a);
static void overlay_click_cb(lv_event_t *e);
static void menu_item_click_cb(lv_event_t *e);
static void create_menu_items(void);
static void open_menu(void);
static void close_menu(void);

/* ------------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------------ */
void screen_menu_init(void)
{
    /* 1. Full-screen transparent overlay to catch clicks and dim background */
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, C_OVERLAY_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, LV_PART_MAIN); /* 50% black = dim blur effect */
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN); /* Hidden by default */
    lv_obj_add_event_cb(s_overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* 2. The actual menu panel that slides down */
    s_menu_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_menu_panel, MENU_WIDTH, 0); /* Start with 0 height for animation */
    lv_obj_set_pos(s_menu_panel, MENU_X_PAD, MENU_Y_START);
    lv_obj_set_style_bg_color(s_menu_panel, C_MENU_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_menu_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_menu_panel, 8, LV_PART_MAIN); /* Slightly rounded corners */
    lv_obj_set_style_pad_top(s_menu_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_menu_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_menu_panel, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_menu_panel, 20, LV_PART_MAIN);

    create_menu_items();
}

/* ------------------------------------------------------------------
 * Animation Control
 * ------------------------------------------------------------------ */
static void menu_anim_cb(void *var, int32_t v)
{
    lv_obj_set_height(var, v);
}

static void open_menu(void)
{
    s_is_open = true;
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_menu_panel);
    lv_anim_set_values(&a, 0, MENU_HEIGHT);
    lv_anim_set_duration(&a, 250); /* 250ms slide down */
    lv_anim_set_exec_cb(&a, menu_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void close_menu(void)
{
    s_is_open = false;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_menu_panel);
    lv_anim_set_values(&a, MENU_HEIGHT, 0);
    lv_anim_set_duration(&a, 200); /* 200ms roll up */
    lv_anim_set_exec_cb(&a, menu_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);

    /* When the roll-up animation finishes, hide the overlay */
    lv_anim_set_ready_cb(&a, menu_close_ready_cb);

    lv_anim_start(&a);
}

/* Called by LVGL when close_menu()'s roll-up animation finishes. */
static void menu_close_ready_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void screen_menu_toggle(void)
{
    if (s_is_open) {
        close_menu();
    } else {
        open_menu();
    }
}

/* ------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------ */
static void overlay_click_cb(lv_event_t *e)
{
    (void)e;
    /* Clicking the dark background closes the menu */
    close_menu();
}

static void menu_item_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *txt = lv_label_get_text(label);

    /* Close the menu immediately */
    close_menu();

    /* Route to the correct screen based on text */
    if (strcmp(txt, "Settings") == 0) {
        screen_show(SCREEN_SETTINGS);
    }
    else if (strcmp(txt, "Battery") == 0) {
        // screen_show(SCREEN_BATTERY); /* For future use */
    }
    else if (strcmp(txt, "Historical Data") == 0) {
        // screen_show(SCREEN_HISTORY); /* For future use */
    }
}

/* ------------------------------------------------------------------
 * Menu Item Creation
 * ------------------------------------------------------------------ */
static void create_menu_item(lv_obj_t *parent, const char *text, int32_t y_offset)
{
    /* We use a basic lv_obj as a button row */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_set_pos(row, 0, y_offset);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Strip default styling to make it look like a flat clickable area */
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);

    /* Highlight color when pressed (Divider color as requested) */
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row, C_MENU_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_add_event_cb(row, menu_item_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, C_MENU_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, FONT_MENU_ITEM, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static void create_menu_items(void)
{
    /* Stack the items vertically with a gap */
    create_menu_item(s_menu_panel, "Settings",        0);
    create_menu_item(s_menu_panel, "Battery",         44);
    create_menu_item(s_menu_panel, "Historical Data", 88);
}
