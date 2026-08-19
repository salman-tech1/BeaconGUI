/*
 * screen_dashboard.c
 *
 * Dashboard layout and data binding. Hardcodes the BeaconView-style UI.
 * Knows nothing about screen switching or hardware drivers.
 */

#include "screen_dashboard.h"
#include "screen_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Palette ──────────────────────────────────────────────────────── */
#define C_BG          lv_color_hex(0x16213E)
#define C_BORDER      lv_color_hex(0x3F6E7E)
#define C_TEXT        lv_color_hex(0xE0E0E0)
#define C_TEXT_DIM    lv_color_hex(0x6B7280)
#define C_AMBER       lv_color_hex(0xF5A623)
#define C_YELLOW      lv_color_hex(0xFFD447)
#define C_GREEN       lv_color_hex(0x3DD68C)

#define C_ARC_BRIGHT  lv_color_hex(0x04acd2)
#define C_ARC_TRACK   lv_color_hex(0x00394C)
#define C_INNER_RING  lv_color_hex(0x3F6E7E)
#define C_PILL_BG     lv_color_hex(0xE0E0E0)
#define C_PILL_TEXT   lv_color_hex(0x02aab8)

#define C_ICON_GLYPH  lv_color_hex(0x04acd2)
#define C_CONNECTOR   lv_color_hex(0x8E9699)
#define C_CIRCLE_BG   lv_color_hex(0xE0E0E0)

/* ── Fonts (Fallback chain) ───────────────────────────────────────── */
#if LV_FONT_MONTSERRAT_10
  #define FONT_CAPTION  &lv_font_montserrat_10
#elif LV_FONT_MONTSERRAT_12
  #define FONT_CAPTION  &lv_font_montserrat_12
#else
  #define FONT_CAPTION  LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_12
  #define FONT_KICKER   &lv_font_montserrat_12
#else
  #define FONT_KICKER   LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_14
  #define FONT_BODY     &lv_font_montserrat_14
#else
  #define FONT_BODY     LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_20
  #define FONT_TITLE    &lv_font_montserrat_20
#else
  #define FONT_TITLE    LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_32
  #define FONT_MENU     &lv_font_montserrat_32
#elif LV_FONT_MONTSERRAT_28
  #define FONT_MENU     &lv_font_montserrat_28
#else
  #define FONT_MENU     FONT_TITLE
#endif

#if LV_FONT_MONTSERRAT_28
  #define FONT_VALUE_MD &lv_font_montserrat_28
#elif LV_FONT_MONTSERRAT_24
  #define FONT_VALUE_MD &lv_font_montserrat_24
#else
  #define FONT_VALUE_MD LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_48
  #define FONT_VALUE_LG &lv_font_montserrat_48
#elif LV_FONT_MONTSERRAT_40
  #define FONT_VALUE_LG &lv_font_montserrat_40
#elif LV_FONT_MONTSERRAT_36
  #define FONT_VALUE_LG &lv_font_montserrat_36
#elif LV_FONT_MONTSERRAT_32
  #define FONT_VALUE_LG &lv_font_montserrat_32
#else
  #define FONT_VALUE_LG LV_FONT_DEFAULT
#endif

/* ── Icons ────────────────────────────────────────────────────────── */
#define ICON_MENU        LV_SYMBOL_BARS
#define ICON_WIFI        LV_SYMBOL_WIFI
#define ICON_USB         LV_SYMBOL_USB
#define ICON_LOCATION    LV_SYMBOL_GPS
#define ICON_PRODUCTION  LV_SYMBOL_CHARGE
#define ICON_CONSUMPTION LV_SYMBOL_HOME
#define ICON_GRID        LV_SYMBOL_REFRESH

/* ── Layout (Tuned for 1024x600) ──────────────────────────────────── */
#define TOP_BAR_H     64
#define SIDE_MARGIN   16
#define ARC_SIZE      320
#define ARC_WIDTH     12
#define ROW_H         146

/*
 * 60 points across 260px means lines are ~4px long.
 * With LVGL anti-aliasing, this renders as a smooth continuous curve
 * rather than a jagged zigzag.
 */
#define CHART_POINTS  60

#define ROW_TEXT_COL_W      315
#define ROW_CHART_W         260
#define ROW_FOOTER_VALUE_X  235
#define PANEL_PUSH   64

/* Y offsets relative to the top of a stat row */
#define ROW_KICKER_Y       0
#define ROW_VALUE_Y        18
#define ROW_CHART_Y        56
#define ROW_CHART_H        32
#define ROW_FOOTER_Y       100
#define ROW_FOOTER2_Y      126
#define ROW_FOOTER_LINE_Y  128

/* Arc animation timing */
#define BATTERY_ANIM_MS        450
#define BATTERY_ANIM_BEZIER_P1 200
#define BATTERY_ANIM_BEZIER_P2 850

#define CONNECTOR_LINE_COUNT   2
#define FOOTER_LINE_COUNT      2

/* ── Widget Handles ───────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *battery_arc;
    lv_obj_t *battery_value_lbl;
    lv_obj_t *battery_pill;
    lv_obj_t *battery_pill_lbl;

    lv_obj_t *weather_city_lbl;
    lv_obj_t *weather_temp_lbl;
    lv_obj_t *weather_updated_lbl;

    lv_obj_t *wifi_icon;
    lv_obj_t *wifi_panel;
    lv_obj_t *wifi_status_lbl;
    lv_obj_t *wifi_rssi_lbl;
    lv_obj_t *wifi_ip_lbl;
    bool      wifi_panel_open;

    lv_obj_t *status_val_lbl[4];

    lv_obj_t          *production_value_lbl;
    lv_obj_t          *production_footer_lbl;
    lv_chart_series_t *production_series;
    lv_obj_t          *production_chart;

    lv_obj_t          *consumption_value_lbl;
    lv_obj_t          *consumption_footer_lbl;
    lv_chart_series_t *consumption_series;
    lv_obj_t          *consumption_chart;

    lv_obj_t          *grid_value_lbl;
    lv_obj_t          *grid_footer_bought_lbl;
    lv_obj_t          *grid_footer_sold_lbl;
    lv_chart_series_t *grid_series;
    lv_obj_t          *grid_chart;
} DashboardWidgets_t;

static DashboardWidgets_t s_w;

/* Static buffers for LVGL line objects (LVGL requires persistent arrays for lines) */
static lv_point_precise_t s_connector_pts[CONNECTOR_LINE_COUNT][2];
static lv_point_precise_t s_footer_line_pts[FOOTER_LINE_COUNT][2];
static int s_connector_idx  = 0;
static int s_footer_line_idx = 0;

static int32_t s_battery_pct = 0;




/* Forward declare the callback */
static void menu_btn_event_cb(lv_event_t *e);
static void wifi_icon_click_cb(lv_event_t *e);
static lv_obj_t *create_wifi_panel(lv_obj_t *parent, int32_t w);


/* The callback that talks to the global menu component */
static void menu_btn_event_cb(lv_event_t *e)
{
    (void)e;
    screen_menu_toggle();
}

/* Toggles the small WiFi status popover open/closed. Deliberately NOT
 * using a full-screen dim overlay + slide animation like screen_menu.c —
 * that pattern caused a real, measured frame-rate drop (translucent
 * full-screen objects force a full-scene recomposite on every show/hide).
 * A tiny opaque panel that just flips LV_OBJ_FLAG_HIDDEN is effectively
 * free by comparison, and a status popover doesn't need the ceremony a
 * navigation menu does. */
static void wifi_icon_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_w.wifi_panel) return;
    s_w.wifi_panel_open = !s_w.wifi_panel_open;
    if (s_w.wifi_panel_open) {
        lv_obj_remove_flag(s_w.wifi_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_w.wifi_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── Widget Factories ─────────────────────────────────────────────── */

/* Creates a colored circle with a centered LVGL symbol inside */
static lv_obj_t *make_icon_circle(lv_obj_t *parent, const char *symbol, lv_color_t glyph_color,
                                   lv_color_t bg_color, int32_t cx, int32_t cy, int32_t d)
{
    lv_obj_t *circ = lv_obj_create(parent);
    lv_obj_remove_flag(circ, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(circ, d, d);
    lv_obj_set_pos(circ, cx - d / 2, cy - d / 2);

    lv_obj_set_style_radius(circ, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(circ, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(circ, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(circ, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(circ, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(circ);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, glyph_color, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, FONT_BODY, LV_PART_MAIN);
    lv_obj_center(lbl);
    return circ;
}

/* Shorthand for placing unformatted text */
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                             lv_color_t color, int32_t x, int32_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

/* Small popover under the WiFi icon: connection state, RSSI, IP.
 * SSID is intentionally NOT included with live data — see the comment
 * in screen_dashboard_set_wifi_status() for why. */
static lv_obj_t *create_wifi_panel(lv_obj_t *parent, int32_t w)
{
    const int32_t panel_w = 200;
    const int32_t pad     = 14;
    const int32_t row_h   = 22;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(panel, panel_w, pad * 2 + row_h * 4);
    lv_obj_set_pos(panel, w - SIDE_MARGIN - panel_w, TOP_BAR_H + 4);
    lv_obj_set_style_bg_color(panel, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, pad, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); /* closed by default */

    s_w.wifi_status_lbl = make_label(panel, "WiFi: Disconnected", FONT_KICKER, C_TEXT,
                                      0, 0 * row_h);
    /* SSID line is static text, not a live field — see the note above */
    make_label(panel, "SSID: --", FONT_KICKER, C_TEXT_DIM, 0, 1 * row_h);
    s_w.wifi_rssi_lbl = make_label(panel, "RSSI: --", FONT_KICKER, C_TEXT_DIM,
                                    0, 2 * row_h);
    s_w.wifi_ip_lbl   = make_label(panel, "IP: --", FONT_KICKER, C_TEXT_DIM,
                                    0, 3 * row_h);

    return panel;
}

/* Generates a minimal, transparent sparkline chart */
static lv_obj_t *make_sparkline(lv_obj_t *parent, lv_color_t color, int32_t x, int32_t y,
                                 int32_t w, int32_t h, lv_chart_series_t **series_out)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_pos(chart, x, y);
    lv_obj_set_size(chart, w, h);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    /* Strip out chart chrome so it acts as a pure line graph */
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /* Hide dots */
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_series_t *series = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);

    /* Pre-fill with random data so the UI doesn't start completely flat */
    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_next_value(chart, series, 40 + (rand() % 30));
    }

    if (series_out) *series_out = series;
    return chart;
}

/* ── Battery Animation Helpers ────────────────────────────────────── */

static void update_battery_visual(int32_t pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    lv_arc_set_value(s_w.battery_arc, pct);
    s_battery_pct = pct;
}

static void battery_anim_exec_cb(void *var, int32_t v)
{
    (void)var;
    update_battery_visual(v);
}

/* Custom Bezier easing for a natural, non-linear sweep */
static int32_t battery_anim_path_bezier(const lv_anim_t *a)
{
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, 1024);
    int32_t step = lv_bezier3(t, 0, BATTERY_ANIM_BEZIER_P1, BATTERY_ANIM_BEZIER_P2, 1024);
    int32_t new_value = step * (a->end_value - a->start_value);
    new_value >>= 10;
    new_value += a->start_value;
    return new_value;
}

/* ── Complex Layout Builders ──────────────────────────────────────── */

/*
 * Creates a single row in the right-hand stat panel.
 * next_row_y is used to dynamically size the dashed vertical connector line.
 */
static lv_obj_t *create_stat_row(lv_obj_t *parent, int32_t row_y, int32_t next_row_y, const char *icon,
                                  const char *kicker, lv_color_t accent,
                                  bool add_footer_line, bool connector_below, int32_t icon_x,
                                  lv_obj_t **value_lbl_out, lv_chart_series_t **series_out,
                                  lv_obj_t **chart_out,
                                  const char *footer1_text, lv_obj_t **footer1_val_out,
                                  const char *footer2_text, lv_obj_t **footer2_val_out)
{
    int32_t icon_cy = row_y + 24;
    make_icon_circle(parent, icon, C_ICON_GLYPH, C_CIRCLE_BG, icon_x, icon_cy, 40);

    if (connector_below && next_row_y > 0) {
        int idx = s_connector_idx++;
        lv_obj_t *line = lv_line_create(parent);
        s_connector_pts[idx][0].x = icon_x;
        s_connector_pts[idx][0].y = row_y + 50;
        s_connector_pts[idx][1].x = icon_x;
        s_connector_pts[idx][1].y = next_row_y - 2;
        lv_line_set_points(line, s_connector_pts[idx], 2);
        lv_obj_set_style_line_color(line, C_CONNECTOR, LV_PART_MAIN);
        lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(line, 4, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(line, 8, LV_PART_MAIN);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Transparent container to hold text/values without messing up global layout */
    int32_t text_x = icon_x + 50;
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_pos(col, text_x, row_y);
    lv_obj_set_size(col, ROW_TEXT_COL_W, ROW_H);

    make_label(col, kicker, FONT_KICKER, C_TEXT, 0, ROW_KICKER_Y);

    lv_obj_t *value = make_label(col, "0.0 kw", FONT_VALUE_MD, C_TEXT, 0, ROW_VALUE_Y);
    if (value_lbl_out) *value_lbl_out = value;

    lv_obj_t *chart = make_sparkline(col, accent, 0, ROW_CHART_Y, ROW_CHART_W, ROW_CHART_H, series_out);
    if (chart_out) *chart_out = chart;

    if (footer1_text != NULL) {
        make_label(col, footer1_text, FONT_KICKER, C_TEXT_DIM, 0, ROW_FOOTER_Y);
        lv_obj_t *fval = make_label(col, "0 kwh", FONT_KICKER, C_TEXT, ROW_FOOTER_VALUE_X, ROW_FOOTER_Y);
        if (footer1_val_out) *footer1_val_out = fval;

        if (add_footer_line) {
            int fidx = s_footer_line_idx++;
            s_footer_line_pts[fidx][0].x = 0;
            s_footer_line_pts[fidx][0].y = ROW_FOOTER_LINE_Y;
            s_footer_line_pts[fidx][1].x = ROW_FOOTER_VALUE_X + 48;
            s_footer_line_pts[fidx][1].y = ROW_FOOTER_LINE_Y;

            lv_obj_t *footer_line = lv_line_create(col);
            lv_line_set_points(footer_line, s_footer_line_pts[fidx], 2);
            lv_obj_set_style_line_color(footer_line, C_BORDER, LV_PART_MAIN);
            lv_obj_set_style_line_width(footer_line, 1, LV_PART_MAIN);
            lv_obj_remove_flag(footer_line, LV_OBJ_FLAG_CLICKABLE);
        }
    } else {
        if (footer1_val_out) *footer1_val_out = NULL;
    }

    if (footer2_text != NULL) {
        make_label(col, footer2_text, FONT_KICKER, C_TEXT_DIM, 0, ROW_FOOTER2_Y);
        lv_obj_t *fval2 = make_label(col, "0 kwh", FONT_KICKER, C_TEXT, ROW_FOOTER_VALUE_X, ROW_FOOTER2_Y);
        if (footer2_val_out) *footer2_val_out = fval2;
    } else {
        if (footer2_val_out) *footer2_val_out = NULL;
    }

    return col;
}

/* ── Section Assembly ─────────────────────────────────────────────── */

static void create_top_bar(lv_obj_t *parent, int32_t w)
{
	  /*
	     * Wrap the icon in a transparent clickable button so we get
	     * visual feedback and reliable touch targets
	     */
	    lv_obj_t *menu_btn = lv_btn_create(parent);
	    lv_obj_set_size(menu_btn, 48, 48);
	    lv_obj_set_pos(menu_btn, SIDE_MARGIN - 4, 8);
	    lv_obj_remove_flag(menu_btn, LV_OBJ_FLAG_SCROLLABLE);

	    /* Make the button completely invisible */
	    lv_obj_set_style_bg_opa(menu_btn, LV_OPA_TRANSP, LV_PART_MAIN);
	    lv_obj_set_style_border_width(menu_btn, 0, LV_PART_MAIN);
	    lv_obj_set_style_shadow_width(menu_btn, 0, LV_PART_MAIN);

	    /* Show the divider color when pressed */
	    lv_obj_set_style_bg_opa(menu_btn, LV_OPA_COVER, LV_STATE_PRESSED);
	    lv_obj_set_style_bg_color(menu_btn, C_BORDER, LV_STATE_PRESSED);
	    lv_obj_set_style_radius(menu_btn, 8, LV_STATE_PRESSED);

	    lv_obj_add_event_cb(menu_btn, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);

	    /* The actual text icon */
	    lv_obj_t *menu = lv_label_create(menu_btn);
	    lv_label_set_text(menu, ICON_MENU);
	    lv_obj_set_style_text_color(menu, C_TEXT, LV_PART_MAIN);
	    lv_obj_set_style_text_font(menu, FONT_MENU, LV_PART_MAIN);
	    lv_obj_center(menu);

	    /* USB and WiFi stay exactly as they were */
	    lv_obj_t *usb = lv_label_create(parent);
	    lv_label_set_text(usb, ICON_USB);
	    lv_obj_set_style_text_color(usb, C_TEXT, LV_PART_MAIN);
	    lv_obj_set_style_text_font(usb, FONT_TITLE, LV_PART_MAIN);
	    lv_obj_set_pos(usb, w - SIDE_MARGIN - 70, 20);

	    /* WiFi icon — same clickable-button wrapper as the menu icon above,
	     * so it gets a real touch target and pressed-feedback instead of
	     * just being a bare label. Positioned/sized so the glyph lands on
	     * the exact same pixel it did before this was a button (w - 30, 20) —
	     * pad zeroed and the label pinned to (0,0) instead of centered,
	     * since centering it inside a same-sized-but-differently-anchored
	     * button is what shifted it last time. */
	    lv_obj_t *wifi_btn = lv_btn_create(parent);
	    lv_obj_set_size(wifi_btn, 30, 30);
	    lv_obj_set_pos(wifi_btn, w - SIDE_MARGIN - 30, 20);
	    lv_obj_remove_flag(wifi_btn, LV_OBJ_FLAG_SCROLLABLE);
	    lv_obj_set_style_pad_all(wifi_btn, 0, LV_PART_MAIN);
	    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, LV_PART_MAIN);
	    lv_obj_set_style_border_width(wifi_btn, 0, LV_PART_MAIN);
	    lv_obj_set_style_shadow_width(wifi_btn, 0, LV_PART_MAIN);
	    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_COVER, LV_STATE_PRESSED);
	    lv_obj_set_style_bg_color(wifi_btn, C_BORDER, LV_STATE_PRESSED);
	    lv_obj_set_style_radius(wifi_btn, 8, LV_STATE_PRESSED);
	    lv_obj_add_event_cb(wifi_btn, wifi_icon_click_cb, LV_EVENT_CLICKED, NULL);

	    lv_obj_t *wifi = lv_label_create(wifi_btn);
	    lv_label_set_text(wifi, ICON_WIFI);
	    /* Starts dim (disconnected) — screen_dashboard_set_wifi_status()
	     * switches it to white once actually connected. */
	    lv_obj_set_style_text_color(wifi, C_TEXT_DIM, LV_PART_MAIN);
	    lv_obj_set_style_text_font(wifi, FONT_TITLE, LV_PART_MAIN);
	    lv_obj_set_pos(wifi, 0, 0);
	    s_w.wifi_icon = wifi;

	    s_w.wifi_panel = create_wifi_panel(parent, w);
	    s_w.wifi_panel_open = false;
	}

static void create_battery_gauge(lv_obj_t *parent, int32_t left_panel_x, int32_t left_panel_w,
                                  int32_t top_y)
{
    int32_t arc_x = left_panel_x + (left_panel_w - ARC_SIZE) / 2;

    lv_obj_t *arc = lv_arc_create(parent);
    s_w.battery_arc = arc;
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(arc, arc_x, top_y);
    lv_arc_set_bg_angles(arc, 120, 60);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_arc_set_rotation(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    /* Background track */
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, C_ARC_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    /* Active indicator */
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, C_ARC_BRIGHT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    int32_t cx = arc_x + ARC_SIZE / 2;
    int32_t cy = top_y + ARC_SIZE / 2;

    /* Decorative inner ring */
    lv_obj_t *inner_ring = lv_obj_create(parent);
    lv_obj_remove_flag(inner_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    int32_t ring_d = ARC_SIZE - 64;
    lv_obj_set_size(inner_ring, ring_d, ring_d);
    lv_obj_set_pos(inner_ring, cx - ring_d / 2, cy - ring_d / 2);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner_ring, C_INNER_RING, LV_PART_MAIN);
    lv_obj_set_style_border_width(inner_ring, 1, LV_PART_MAIN);

    /* Center text stack */
    lv_obj_t *value = lv_label_create(arc);
    s_w.battery_value_lbl = value;
    lv_label_set_text(value, "2.05 kW");
    lv_obj_set_style_text_font(value, FONT_VALUE_LG, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, C_TEXT, LV_PART_MAIN);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *caption = lv_label_create(arc);
    lv_label_set_text(caption, "BATTERY POWER");
    lv_obj_set_style_text_font(caption, FONT_CAPTION, LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, C_TEXT, LV_PART_MAIN);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 0);

    /* Status pill (CHARGING / DISCHARGING) */
    lv_obj_t *pill = lv_obj_create(arc);
    s_w.battery_pill = pill;
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(pill, 112, 38);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(pill, C_PILL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);

    lv_obj_t *pill_lbl = lv_label_create(pill);
    s_w.battery_pill_lbl = pill_lbl;
    lv_label_set_text(pill_lbl, "DISCHARGING");
    lv_obj_set_style_text_font(pill_lbl, FONT_CAPTION, LV_PART_MAIN);
    lv_obj_set_style_text_color(pill_lbl, C_PILL_TEXT, LV_PART_MAIN);
    lv_obj_center(pill_lbl);

    update_battery_visual(62);
}

static void create_weather_row(lv_obj_t *parent, int32_t left_panel_x, int32_t y)
{
    make_icon_circle(parent, LV_SYMBOL_WARNING, C_AMBER, C_CIRCLE_BG,
                      left_panel_x + 20, y + 18, 36);

    lv_obj_t *loc = lv_label_create(parent);
    lv_label_set_text(loc, ICON_LOCATION " Islamabad");
    lv_obj_set_style_text_font(loc, FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(loc, C_TEXT, LV_PART_MAIN);
    lv_obj_set_pos(loc, left_panel_x + 50, y);
    s_w.weather_city_lbl = loc;

    lv_obj_t *temp = make_label(parent, "28, Sunny", FONT_TITLE, C_TEXT,
                                 left_panel_x + 50, y + 18);
    s_w.weather_temp_lbl = temp;

    lv_obj_t *updated = make_label(parent, "Last updated 11:44 AM, 26/03", FONT_BODY,
                                    C_TEXT, left_panel_x + 240, y + 8);
    s_w.weather_updated_lbl = updated;
}

static void create_status_grid(lv_obj_t *parent, int32_t left_panel_x, int32_t y)
{
    static const char *labels[4] = {
        "BEL EYE OPERATION", "GRID CONNECTIVITY", "GRID EXPORT", "DISCO"
    };
    static const char *values[4] = {
        "Active", "Disconnected", "Enabled", "K-Electric"
    };
    int32_t label_x[2] = { 0, 300 };
    int32_t value_x[2] = { 170, 420 };
    int32_t row_y[2]   = { 0, 26 };

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            int idx = c * 2 + r;
            make_label(parent, labels[idx], FONT_KICKER, C_TEXT_DIM,
                       left_panel_x + label_x[c], y + row_y[r]);
            s_w.status_val_lbl[idx] = make_label(parent, values[idx], FONT_BODY, C_TEXT,
                                                  left_panel_x + value_x[c], y + row_y[r]);
        }
    }
}

/*
 * Proportionally spaces the three right-hand stat rows.
 * Anchors to the top divider and bottom status grid, then divides the
 * remaining space perfectly in half to calculate dynamic connector lengths.
 */
static void create_right_panel(lv_obj_t *parent, int32_t panel_x, int32_t panel_w, int32_t top_y, int32_t bottom_align_y)
{
    (void)panel_w;
    int32_t icon_x = panel_x + 20;

    int32_t row0_y = top_y - 4;
    int32_t row2_y = bottom_align_y - ROW_FOOTER_Y;
    int32_t step_y = (row2_y - row0_y) / 2;
    int32_t row1_y = row0_y + step_y;

    lv_obj_t *col0 = create_stat_row(parent, row0_y, row1_y, ICON_PRODUCTION,
                                      "ENERGY PRODUCTION", C_ARC_BRIGHT, true, true, icon_x,
                                      &s_w.production_value_lbl, &s_w.production_series,
                                      &s_w.production_chart,
                                      "Today's Production", &s_w.production_footer_lbl,
                                      NULL, NULL);
    (void)col0;

    lv_obj_t *col1 = create_stat_row(parent, row1_y, row2_y, ICON_CONSUMPTION,
                                      "ENERGY CONSUMPTION", C_YELLOW, true, true, icon_x,
                                      &s_w.consumption_value_lbl, &s_w.consumption_series,
                                      &s_w.consumption_chart,
                                      "Today's Consumption", &s_w.consumption_footer_lbl,
                                      NULL, NULL);
    (void)col1;

    /* Last row has no successor, so connector is disabled (next_row_y = 0) */
    lv_obj_t *col2 = create_stat_row(parent, row2_y, 0, ICON_GRID,
                                      "GRID BUY", C_YELLOW, false, false, icon_x,
                                      &s_w.grid_value_lbl, &s_w.grid_series,
                                      &s_w.grid_chart,
                                      "Energy Bought Today", &s_w.grid_footer_bought_lbl,
                                      "Energy Sold Today", &s_w.grid_footer_sold_lbl);
    (void)col2;
}

/* ── Public API ───────────────────────────────────────────────────── */

void screen_dashboard_create(lv_obj_t *parent)
{
    memset(&s_w, 0, sizeof(s_w));
    s_connector_idx   = 0;
    s_footer_line_idx = 0;

    int32_t w = lv_obj_get_width(parent);
    int32_t h = lv_obj_get_height(parent);

    lv_obj_set_style_bg_color(parent, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(parent, w);

    /* Calculate main layout boundaries */
    int32_t content_y = TOP_BAR_H + 8;
    int32_t content_h = h - content_y - SIDE_MARGIN;
    int32_t content_bottom = content_y + content_h;

    int32_t left_x    = SIDE_MARGIN;
    int32_t left_w    = 500 + PANEL_PUSH;
    int32_t divider_x = left_x + left_w + 16;
    int32_t right_x   = divider_x + 16;
    int32_t right_w   = w - SIDE_MARGIN - right_x;

    /* Thin vertical separator */
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(divider, 1, content_h);
    lv_obj_set_pos(divider, divider_x, content_y);
    lv_obj_set_style_bg_color(divider, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);

    /* Assemble left panel (anchored to bottom) */
    create_battery_gauge(parent, left_x, left_w, content_y + 8);

    int32_t status_h  = 56;
    int32_t weather_h = 40;
    int32_t gap       = 16;

    int32_t status_y  = content_bottom - 5 - status_h;
    int32_t weather_y = status_y - gap - weather_h;

    create_weather_row(parent, left_x, weather_y);
    create_status_grid(parent, left_x, status_y);

    /* Assemble right panel (spans between top_y and status_y) */
    create_right_panel(parent, right_x, right_w, content_y, status_y);
}

void screen_dashboard_set_battery(float kw, DashboardBatteryState_t state)
{
    if (!s_w.battery_value_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f kW", kw);
    lv_label_set_text(s_w.battery_value_lbl, buf);

    const char *text;
    lv_color_t color;
    switch (state) {
        case DASHBOARD_BATTERY_CHARGING:    text = "CHARGING";    color = C_GREEN; break;
        case DASHBOARD_BATTERY_DISCHARGING: text = "DISCHARGING"; color = C_PILL_TEXT; break;
        default:                            text = "IDLE";        color = C_TEXT_DIM; break;
    }
    lv_label_set_text(s_w.battery_pill_lbl, text);
    lv_obj_set_style_text_color(s_w.battery_pill_lbl, color, LV_PART_MAIN);

    /* Map kw to 0-100% visually, then trigger the eased sweep animation */
    int32_t target_pct = (int32_t)(kw > 0 ? kw * 20.0f : -kw * 20.0f);
    if (target_pct > 100) target_pct = 100;
    if (target_pct < 0)   target_pct = 0;

    /* Cancel any in-flight animation before starting a new one */
    lv_anim_delete((void *)&s_w, battery_anim_exec_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, (void *)&s_w);
    lv_anim_set_values(&a, s_battery_pct, target_pct);
    lv_anim_set_duration(&a, BATTERY_ANIM_MS);
    lv_anim_set_exec_cb(&a, battery_anim_exec_cb);
    lv_anim_set_path_cb(&a, battery_anim_path_bezier);
    lv_anim_start(&a);
}

void screen_dashboard_set_production(float kw, uint32_t today_kwh)
{
    if (!s_w.production_value_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f kw", kw);
    lv_label_set_text(s_w.production_value_lbl, buf);
    snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)today_kwh);
    lv_label_set_text(s_w.production_footer_lbl, buf);
}

void screen_dashboard_set_consumption(float kw, uint32_t today_kwh)
{
    if (!s_w.consumption_value_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f kw", kw);
    lv_label_set_text(s_w.consumption_value_lbl, buf);
    snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)today_kwh);
    lv_label_set_text(s_w.consumption_footer_lbl, buf);
}

void screen_dashboard_set_grid(float kw, uint32_t bought_kwh, uint32_t sold_kwh)
{
    if (!s_w.grid_value_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f kw", kw);
    lv_label_set_text(s_w.grid_value_lbl, buf);
    if (s_w.grid_footer_bought_lbl) {
        snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)bought_kwh);
        lv_label_set_text(s_w.grid_footer_bought_lbl, buf);
    }
    if (s_w.grid_footer_sold_lbl) {
        snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)sold_kwh);
        lv_label_set_text(s_w.grid_footer_sold_lbl, buf);
    }
}

void screen_dashboard_chart_push(DashboardChartId_t chart, int32_t value)
{
    switch (chart) {
        case DASHBOARD_CHART_PRODUCTION:
            lv_chart_set_next_value(s_w.production_chart, s_w.production_series, value);
            break;
        case DASHBOARD_CHART_CONSUMPTION:
            lv_chart_set_next_value(s_w.consumption_chart, s_w.consumption_series, value);
            break;
        case DASHBOARD_CHART_GRID:
            lv_chart_set_next_value(s_w.grid_chart, s_w.grid_series, value);
            break;
        case DASHBOARD_CHART_COUNT:
        default:
            /* DASHBOARD_CHART_COUNT is a sentinel, not a real chart id */
            break;
    }
}

void screen_dashboard_set_weather(const char *city, int temp_c, const char *condition)
{
    if (!s_w.weather_temp_lbl) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "%d, %s", temp_c, condition);
    lv_label_set_text(s_w.weather_temp_lbl, buf);

    if (s_w.weather_city_lbl && city != NULL) {
        char city_buf[48];
        snprintf(city_buf, sizeof(city_buf), "%s %s", ICON_LOCATION, city);
        lv_label_set_text(s_w.weather_city_lbl, city_buf);
    }
}

void screen_dashboard_set_time(const char *time_str, bool synced)
{
    if (!s_w.weather_updated_lbl) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "Last updated %s", (time_str != NULL) ? time_str : "--:--, --/--");
    lv_label_set_text(s_w.weather_updated_lbl, buf);
    /* Dim it while we're still showing the "--:--" placeholder / not yet
     * NTP-synced, same visual language the rest of the dashboard uses for
     * "no data yet" (e.g. status_val_lbl before the first real update). */
    lv_obj_set_style_text_color(s_w.weather_updated_lbl, synced ? C_TEXT : C_TEXT_DIM, LV_PART_MAIN);
}

void screen_dashboard_set_wifi_status(bool connected, int8_t rssi, const char *ip)
{
    if (!s_w.wifi_icon) return;

    lv_obj_set_style_text_color(s_w.wifi_icon, connected ? lv_color_white() : C_TEXT_DIM,
                                 LV_PART_MAIN);

    if (s_w.wifi_status_lbl) {
        lv_label_set_text(s_w.wifi_status_lbl, connected ? "WiFi: Connected" : "WiFi: Disconnected");
    }

    if (s_w.wifi_rssi_lbl) {
        char buf[32];
        if (connected) {
            /* Same dBm -> quality bucketing used elsewhere in the app
             * (render.c's rssi_to_quality) — duplicated here on purpose,
             * since screen_dashboard.c shouldn't depend on the renderer. */
            const char *quality = "Weak";
            if (rssi >= -50)      quality = "Excellent";
            else if (rssi >= -60) quality = "Good";
            else if (rssi >= -70) quality = "Fair";
            snprintf(buf, sizeof(buf), "RSSI: %d dBm (%s)", (int)rssi, quality);
        } else {
            snprintf(buf, sizeof(buf), "RSSI: --");
        }
        lv_label_set_text(s_w.wifi_rssi_lbl, buf);
    }

    if (s_w.wifi_ip_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "IP: %s",
                 (connected && ip != NULL && ip[0] != '\0') ? ip : "--");
        lv_label_set_text(s_w.wifi_ip_lbl, buf);
    }

    /* SSID is deliberately NOT shown as live data here: it doesn't exist
     * anywhere in the current pipeline yet — link_wifi_status_payload_t
     * in link.c (the ESP32 wire format) never carried it, so it was never
     * in SystemInfo_t or this function's signature either. The popover
     * shows a static "SSID: --" placeholder instead of faking a value.
     * To wire it up for real: add an ssid field to
     * link_wifi_status_payload_t + SystemInfo_t + sysinfo_set_wifi_status()
     * + this function's signature (and the screen_set_wifi_status() /
     * screen_dashboard_set_wifi_status() calls above it) — that's an
     * ESP32-firmware change too, not just this file. */
}

void screen_dashboard_set_status(const char *bel_eye_state, bool grid_connected,
                                  bool grid_export_enabled, const char *disco_name)
{
    if (s_w.status_val_lbl[0]) lv_label_set_text(s_w.status_val_lbl[0], bel_eye_state);
    if (s_w.status_val_lbl[1]) lv_label_set_text(s_w.status_val_lbl[1],
                                                  grid_connected ? "Connected" : "Disconnected");
    if (s_w.status_val_lbl[2]) lv_label_set_text(s_w.status_val_lbl[2],
                                                  grid_export_enabled ? "Enabled" : "Disabled");
    if (s_w.status_val_lbl[3]) lv_label_set_text(s_w.status_val_lbl[3], disco_name);
}
