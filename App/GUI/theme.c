/*
 * screen_dashboard.c
 *
 * Static layout pass for the energy-monitoring dashboard.
 * Icons use built-in LV_SYMBOL_* glyphs as placeholders — swap for a
 * custom icon font later (see the ICON_* defines below, that's the
 * only place you'll need to touch when you do).
 *
 *  Created on: Jul 23, 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "theme.h"

/* ────────────────────────────────────────────────────────────────────
 * Palette  (pulled from the reference screenshot)
 * ────────────────────────────────────────────────────────────────── */
#define C_BG        lv_color_hex(0x0A0E1A)   /* screen background        */
#define C_CARD      lv_color_hex(0x141A2E)   /* card / icon-circle fill  */
#define C_BORDER    lv_color_hex(0x1F2740)   /* card borders, dividers   */
#define C_TRACK     lv_color_hex(0x232A45)   /* arc background track    */
#define C_TEXT      lv_color_hex(0xE8E8EC)   /* primary text             */
#define C_TEXT_DIM  lv_color_hex(0x6B7280)   /* secondary / kicker text  */
#define C_CYAN      lv_color_hex(0x00D4FF)   /* production / battery    */
#define C_AMBER     lv_color_hex(0xF5A623)   /* consumption / grid       */
#define C_GREEN     lv_color_hex(0x3DD68C)   /* charging state           */

/* ────────────────────────────────────────────────────────────────────
 * Fonts — guarded so this compiles regardless of which sizes you've
 * enabled in lv_conf.h. Enable more LV_FONT_MONTSERRAT_xx for a closer
 * match to the reference (it uses roughly 12/14/20/28/32).
 * ────────────────────────────────────────────────────────────────── */
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

#if LV_FONT_MONTSERRAT_24
  #define FONT_VALUE_MD &lv_font_montserrat_24
#else
  #define FONT_VALUE_MD LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_32
  #define FONT_VALUE_LG &lv_font_montserrat_32
#else
  #define FONT_VALUE_LG LV_FONT_DEFAULT
#endif

/* ────────────────────────────────────────────────────────────────────
 * Icon placeholders — the ONLY block to touch when you swap to a
 * custom icon font. Replace each string with your font's glyph.
 * ────────────────────────────────────────────────────────────────── */
#define ICON_MENU        LV_SYMBOL_BARS
#define ICON_WIFI        LV_SYMBOL_WIFI
#define ICON_USB         LV_SYMBOL_USB
#define ICON_BATTERY_TAG LV_SYMBOL_CHARGE
#define ICON_LOCATION    LV_SYMBOL_GPS
#define ICON_PRODUCTION  LV_SYMBOL_CHARGE
#define ICON_CONSUMPTION LV_SYMBOL_HOME
#define ICON_GRID        LV_SYMBOL_REFRESH

/* ────────────────────────────────────────────────────────────────────
 * Layout constants (tuned for 1024x600; derived from parent size at
 * runtime so this also survives a resolution change without edits).
 * ────────────────────────────────────────────────────────────────── */
#define TOP_BAR_H     56
#define SIDE_MARGIN   16
#define ARC_SIZE      280
#define ROW_H         170   /* height budget per right-panel stat row */
#define CHART_POINTS  20

/* ────────────────────────────────────────────────────────────────────
 * Widget state kept alive for later updates via the setter API.
 * ────────────────────────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *battery_arc;
    lv_obj_t *battery_value_lbl;
    lv_obj_t *battery_pill;
    lv_obj_t *battery_pill_lbl;

    lv_obj_t *weather_city_lbl;
    lv_obj_t *weather_temp_lbl;
    lv_obj_t *weather_updated_lbl;

    lv_obj_t *status_val_lbl[4]; /* BEL eye, grid connectivity, grid export, disco */

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

/* lv_line point storage must stay alive for the object's lifetime */
static lv_point_precise_t s_connector_pts[2][2];

/* ────────────────────────────────────────────────────────────────────
 * Small helpers
 * ────────────────────────────────────────────────────────────────── */

static lv_obj_t *make_icon_circle(lv_obj_t *parent, const char *symbol, lv_color_t accent,
                                   int32_t cx, int32_t cy, int32_t d)
{
    lv_obj_t *circ = lv_obj_create(parent);
    lv_obj_remove_flag(circ, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(circ, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(circ, d, d);
    lv_obj_set_pos(circ, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(circ, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(circ, C_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(circ, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(circ, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(circ, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(circ, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(circ);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, FONT_BODY, LV_PART_MAIN);
    lv_obj_center(lbl);
    return circ;
}

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

    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /* hide point dots */
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_series_t *series = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_next_value(chart, series, 40 + (rand() % 30));
    }

    if (series_out) *series_out = series;
    return chart;
}

/**
 * @brief Build one right-panel stat row (icon + kicker + value + sparkline).
 * @param first_footer_lbl_out receives the value label of the single-line
 *        footer this always creates; grid row adds a 2nd footer separately.
 * @return the row's text-column container, so a caller can attach an
 *         extra footer line (used only by the grid row).
 */
static lv_obj_t *create_stat_row(lv_obj_t *parent, int32_t row_y, const char *icon,
                                  const char *kicker, lv_color_t accent,
                                  bool connector_below, int32_t icon_x,
                                  lv_obj_t **value_lbl_out, lv_chart_series_t **series_out,
                                  lv_obj_t **chart_out, const char *footer_text,
                                  lv_obj_t **footer_value_out)
{
    int32_t icon_cy = row_y + 20;
    make_icon_circle(parent, icon, accent, icon_x, icon_cy, 40);

    if (connector_below) {
        static int s_connector_idx = 0; /* picks a free slot in s_connector_pts each call */
        int idx = s_connector_idx++;
        lv_obj_t *line = lv_line_create(parent);
        s_connector_pts[idx][0].x = icon_x;
        s_connector_pts[idx][0].y = row_y + 40;
        s_connector_pts[idx][1].x = icon_x;
        s_connector_pts[idx][1].y = row_y + ROW_H;
        lv_line_set_points(line, s_connector_pts[idx], 2);
        lv_obj_set_style_line_color(line, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(line, 4, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(line, 5, LV_PART_MAIN);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }

    int32_t text_x = icon_x + 44;
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_pos(col, text_x, row_y);
    lv_obj_set_size(col, 400, ROW_H);

    make_label(col, kicker, FONT_KICKER, C_TEXT_DIM, 0, 0);

    lv_obj_t *value = make_label(col, "0.0 kw", FONT_VALUE_MD, C_TEXT, 0, 20);
    if (value_lbl_out) *value_lbl_out = value;

    lv_obj_t *chart = make_sparkline(col, accent, 0, 56, 260, 40, series_out);
    if (chart_out) *chart_out = chart;

    make_label(col, footer_text, FONT_KICKER, C_TEXT_DIM, 0, 100);
    lv_obj_t *fval = make_label(col, "0 kwh", FONT_KICKER, C_TEXT, 220, 100);
    if (footer_value_out) *footer_value_out = fval;

    return col;
}

/* ────────────────────────────────────────────────────────────────────
 * Section builders
 * ────────────────────────────────────────────────────────────────── */

static void create_top_bar(lv_obj_t *parent, int32_t w)
{
    lv_obj_t *menu = lv_label_create(parent);
    lv_label_set_text(menu, ICON_MENU);
    lv_obj_set_style_text_color(menu, C_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(menu, FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_pos(menu, SIDE_MARGIN, 16);

    lv_obj_t *usb = lv_label_create(parent);
    lv_label_set_text(usb, ICON_USB);
    lv_obj_set_style_text_color(usb, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(usb, FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_pos(usb, w - SIDE_MARGIN - 70, 16);

    lv_obj_t *wifi = lv_label_create(parent);
    lv_label_set_text(wifi, ICON_WIFI);
    lv_obj_set_style_text_color(wifi, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi, FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_pos(wifi, w - SIDE_MARGIN - 30, 16);
}

static void create_battery_gauge(lv_obj_t *parent, int32_t left_panel_x, int32_t left_panel_w,
                                  int32_t top_y)
{
    int32_t arc_x = left_panel_x + (left_panel_w - ARC_SIZE) / 2;

    lv_obj_t *arc = lv_arc_create(parent);
    s_w.battery_arc = arc;
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(arc, arc_x, top_y);
    lv_arc_set_bg_angles(arc, 120, 60);   /* 300 deg sweep, gap at the bottom */
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 62);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(arc);
    s_w.battery_value_lbl = value;
    lv_label_set_text(value, "2.05 kW");
    lv_obj_set_style_text_font(value, FONT_VALUE_LG, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, C_TEXT, LV_PART_MAIN);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *caption = lv_label_create(arc);
    lv_label_set_text(caption, "BATTERY POWER");
    lv_obj_set_style_text_font(caption, FONT_KICKER, LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *pill = lv_obj_create(arc);
    s_w.battery_pill = pill;
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(pill, 140, 32);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_bg_color(pill, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);

    lv_obj_t *pill_lbl = lv_label_create(pill);
    s_w.battery_pill_lbl = pill_lbl;
    lv_label_set_text(pill_lbl, "DISCHARGING");
    lv_obj_set_style_text_font(pill_lbl, FONT_KICKER, LV_PART_MAIN);
    lv_obj_set_style_text_color(pill_lbl, C_CYAN, LV_PART_MAIN);
    lv_obj_center(pill_lbl);

    /* Bolt badge overlapping the bottom of the arc's gap */
    make_icon_circle(parent, ICON_BATTERY_TAG, C_CYAN,
                      arc_x + ARC_SIZE / 2, top_y + ARC_SIZE, 36);
}

static void create_weather_row(lv_obj_t *parent, int32_t left_panel_x, int32_t y)
{
    make_icon_circle(parent, LV_SYMBOL_WARNING, C_AMBER, left_panel_x + 20, y + 18, 36);
    /* ^ placeholder "sun" blob — swap symbol/colors once you have a weather icon set */

    lv_obj_t *loc = lv_label_create(parent);
    lv_label_set_text(loc, ICON_LOCATION " Islamabad");
    lv_obj_set_style_text_font(loc, FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(loc, C_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(loc, left_panel_x + 50, y);

    lv_obj_t *temp = make_label(parent, "28, Sunny", FONT_TITLE, C_TEXT,
                                 left_panel_x + 50, y + 18);
    s_w.weather_temp_lbl = temp;

    lv_obj_t *updated = make_label(parent, "Last updated 11:44 AM, 26/03", FONT_BODY,
                                    C_TEXT_DIM, left_panel_x + 240, y + 8);
    s_w.weather_updated_lbl = updated;
}

static void create_status_grid(lv_obj_t *parent, int32_t left_panel_x, int32_t y)
{
    /* indexed as idx = col*2 + row, matching the (c,r) loop below */
    static const char *labels[4] = {
        "BEL EYE OPERATION", "GRID CONNECTIVITY", "GRID EXPORT", "DISCO"
    };
    static const char *values[4] = {
        "Active", "Disconnected", "Enabled", "K-Electric"
    };
    /* row0: col0 label/value, col1 label/value ; row1: same */
    int32_t label_x[2] = { 0, 300 };
    int32_t value_x[2] = { 170, 420 };
    int32_t row_y[2]   = { 0, 26 };

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            int idx = c * 2 + r; /* labels[] ordered col-major to match layout above */
            make_label(parent, labels[idx], FONT_KICKER, C_TEXT_DIM,
                       left_panel_x + label_x[c], y + row_y[r]);
            s_w.status_val_lbl[idx] = make_label(parent, values[idx], FONT_BODY, C_TEXT,
                                                  left_panel_x + value_x[c], y + row_y[r]);
        }
    }
}

static void create_right_panel(lv_obj_t *parent, int32_t panel_x, int32_t top_y)
{
    int32_t icon_x = panel_x + 20;

    lv_obj_t *col0 = create_stat_row(parent, top_y + 0 * ROW_H, ICON_PRODUCTION,
                                      "ENERGY PRODUCTION", C_CYAN, true, icon_x,
                                      &s_w.production_value_lbl, &s_w.production_series,
                                      &s_w.production_chart, "Today's Production",
                                      &s_w.production_footer_lbl);
    (void)col0;

    lv_obj_t *col1 = create_stat_row(parent, top_y + 1 * ROW_H, ICON_CONSUMPTION,
                                      "ENERGY CONSUMPTION", C_AMBER, true, icon_x,
                                      &s_w.consumption_value_lbl, &s_w.consumption_series,
                                      &s_w.consumption_chart, "Today's Consumption",
                                      &s_w.consumption_footer_lbl);
    (void)col1;

    lv_obj_t *col2 = create_stat_row(parent, top_y + 2 * ROW_H, ICON_GRID,
                                      "GRID BUY", C_AMBER, false, icon_x,
                                      &s_w.grid_value_lbl, &s_w.grid_series,
                                      &s_w.grid_chart, "Energy Bought Today",
                                      &s_w.grid_footer_bought_lbl);

    /* Grid row gets a 2nd footer line for "Energy Sold Today" */
    s_w.grid_footer_sold_lbl = make_label(col2, "30 kwh", FONT_KICKER, C_TEXT,
                                           220, 118);
    make_label(col2, "Energy Sold Today", FONT_KICKER, C_TEXT_DIM, 0, 118);
}

/* ────────────────────────────────────────────────────────────────────
 * Public API
 * ────────────────────────────────────────────────────────────────── */

void screen_dashboard_create(lv_obj_t *parent)
{
    memset(&s_w, 0, sizeof(s_w));

    int32_t w = lv_obj_get_width(parent);
    int32_t h = lv_obj_get_height(parent);

    lv_obj_set_style_bg_color(parent, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(parent, w);

    int32_t content_y = TOP_BAR_H + 8;
    int32_t content_h = h - content_y - SIDE_MARGIN;

    int32_t left_x = SIDE_MARGIN;
    int32_t left_w = w / 2 - SIDE_MARGIN - 16;
    int32_t divider_x = w / 2;
    int32_t right_x = w / 2 + 16;

    /* vertical divider between the two panels */
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(divider, 1, content_h);
    lv_obj_set_pos(divider, divider_x, content_y);
    lv_obj_set_style_bg_color(divider, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);

    create_battery_gauge(parent, left_x, left_w, content_y + 8);
    create_weather_row(parent, left_x, content_y + 8 + ARC_SIZE + 36);
    create_status_grid(parent, left_x, content_y + 8 + ARC_SIZE + 36 + 56);

    create_right_panel(parent, right_x, content_y + 8);
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
        case DASHBOARD_BATTERY_DISCHARGING: text = "DISCHARGING"; color = C_CYAN;  break;
        default:                            text = "IDLE";        color = C_TEXT_DIM; break;
    }
    lv_label_set_text(s_w.battery_pill_lbl, text);
    lv_obj_set_style_text_color(s_w.battery_pill_lbl, color, LV_PART_MAIN);

    /* Placeholder mapping: arc fill from power magnitude until you have
     * a real state-of-charge % to bind here instead. */
    int32_t pct = (int32_t)(kw > 0 ? kw * 20.0f : -kw * 20.0f);
    if (pct > 100) pct = 100;
    lv_arc_set_value(s_w.battery_arc, pct);
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
    snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)bought_kwh);
    lv_label_set_text(s_w.grid_footer_bought_lbl, buf);
    snprintf(buf, sizeof(buf), "%lu kwh", (unsigned long)sold_kwh);
    lv_label_set_text(s_w.grid_footer_sold_lbl, buf);
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
    }
}

void screen_dashboard_set_weather(const char *city, int temp_c, const char *condition)
{
    if (!s_w.weather_temp_lbl) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "%d, %s", temp_c, condition);
    lv_label_set_text(s_w.weather_temp_lbl, buf);
    (void)city; /* wire into the location label too if you make it store-able */
}

void screen_dashboard_set_status(const char *bel_eye_state, bool grid_connected,
                                  bool grid_export_enabled, const char *disco_name)
{
    /* idx0=BEL EYE, idx1=GRID CONNECTIVITY, idx2=GRID EXPORT, idx3=DISCO */
    if (s_w.status_val_lbl[0]) lv_label_set_text(s_w.status_val_lbl[0], bel_eye_state);
    if (s_w.status_val_lbl[1]) lv_label_set_text(s_w.status_val_lbl[1],
                                                  grid_connected ? "Connected" : "Disconnected");
    if (s_w.status_val_lbl[2]) lv_label_set_text(s_w.status_val_lbl[2],
                                                  grid_export_enabled ? "Enabled" : "Disabled");
    if (s_w.status_val_lbl[3]) lv_label_set_text(s_w.status_val_lbl[3], disco_name);
}
