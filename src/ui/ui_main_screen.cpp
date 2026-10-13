#include "ui_main_screen.h"
#include "ui_dashboard_chrome.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "fonts/ui_fonts.h"

#include <stdio.h>

using namespace ui_theme;

namespace ui_main_screen {

namespace {

// ---- widgets the setter API below needs to reach again later -------------
lv_obj_t *s_aqi_num_label = nullptr;
lv_obj_t *s_aqi_status_label = nullptr;
lv_obj_t *s_aqi_arc = nullptr;
lv_obj_t *s_aqi_temp_label = nullptr;
lv_obj_t *s_aqi_rh_label = nullptr;

lv_obj_t *s_supply_arc = nullptr;
lv_obj_t *s_supply_level_label = nullptr;
lv_obj_t *s_exhaust_arc = nullptr;
lv_obj_t *s_exhaust_level_label = nullptr;
lv_obj_t *s_reservation_label = nullptr;

// ---- small style helpers ---------------------------------------------

lv_obj_t *make_plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

lv_obj_t *make_row(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t col_gap) {
  lv_obj_t *row = make_plain(parent);
  lv_obj_set_size(row, w, h);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, col_gap, 0);
  return row;
}

lv_obj_t *make_card(lv_obj_t *parent) {
  lv_obj_t *card = make_plain(parent);
  lv_obj_set_style_bg_color(card, c(CARD_BG), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, c(BORDER), 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 8, 0);
  return card;
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  return l;
}

// Decorative outer tick ring for the AQI hero gauge - no indicator/needle,
// just the dashed scale. The glowing value sits on a separate, distinctly
// smaller arc (make_value_arc) with a clear gap between the two, matching
// the two-ring look of the mockup instead of one thick donut band.
lv_obj_t *make_tick_ring(lv_obj_t *parent, lv_coord_t size) {
  lv_obj_t *meter = lv_meter_create(parent);
  lv_obj_remove_style_all(meter);
  lv_obj_set_size(meter, size, size);
  lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);
  lv_meter_scale_t *scale = lv_meter_add_scale(meter);
  lv_meter_set_scale_range(meter, scale, 0, 100, 270, 135);
  lv_meter_set_scale_ticks(meter, scale, 31, 2, 6, c(TRACK));
  return meter;
}

// A single value arc: dim background track + glowing rounded indicator.
// Angle span is always 135deg -> 45deg the long way (270deg sweep, gap at
// the bottom) to match the mockup's gauge style.
lv_obj_t *make_value_arc(lv_obj_t *parent, lv_coord_t size, lv_coord_t width, int value) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_remove_style_all(arc);
  lv_obj_set_size(arc, size, size);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(arc, 135, 45);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, value);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, c(TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, c(ACCENT_CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  return arc;
}

// ---- AQI / comfort card -------------------------------------------------

void build_card_aqi(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_add_flag(card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  lv_obj_t *header = make_row(card, LV_PCT(100), 26, 6);
  make_label(header, "AQI", &ui_font_md, TEXT_SECONDARY);
  s_aqi_num_label = make_label(header, "32", &ui_font_md, TEXT_PRIMARY);
  make_label(header, "/", &ui_font_md, TEXT_DIM);
  s_aqi_status_label = make_label(header, "쾌적", &ui_font_md, GOOD);

  lv_obj_t *wrapper = make_plain(card);
  lv_obj_set_size(wrapper, LV_PCT(100), 208);
  lv_obj_add_flag(wrapper, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // Outer decorative tick ring, and a distinctly smaller glowing arc
  // nested well inside it (clear gap between the two - two rings, not a
  // single thick donut band).
  lv_obj_t *ticks = make_tick_ring(wrapper, 200);
  lv_obj_center(ticks);

  s_aqi_arc = make_value_arc(wrapper, 140, 7, 72);
  lv_obj_center(s_aqi_arc);

  s_aqi_temp_label = make_label(wrapper, "24.5°C", &ui_font_xxl, TEXT_PRIMARY);
  lv_obj_align(s_aqi_temp_label, LV_ALIGN_CENTER, 0, -14);

  s_aqi_rh_label = make_label(wrapper, "48% RH", &ui_font_sm, TEXT_SECONDARY);
  lv_obj_align(s_aqi_rh_label, LV_ALIGN_CENTER, 0, 20);
}

// ---- heat-recovery card --------------------------------------------------

lv_obj_t *build_fan_gauge(lv_obj_t *parent, const char *tag, int value, lv_obj_t **out_arc,
                           lv_obj_t **out_level_label) {
  lv_obj_t *col = make_plain(parent);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 4, 0);

  *out_level_label = make_label(col, "중", &ui_font_xs, TEXT_PRIMARY);

  lv_obj_t *wrapper = make_plain(col);
  lv_obj_set_size(wrapper, LV_PCT(100), 110);

  const lv_coord_t gsize = 110;
  *out_arc = make_value_arc(wrapper, gsize, 8, value);
  lv_obj_center(*out_arc);

  ui_icons::fan(wrapper, 34, c(TEXT_PRIMARY));
  lv_obj_t *last = lv_obj_get_child(wrapper, lv_obj_get_child_cnt(wrapper) - 1);
  lv_obj_center(last);

  make_label(wrapper, "약", &ui_font_xs, TEXT_DIM);
  lv_obj_t *weak = lv_obj_get_child(wrapper, lv_obj_get_child_cnt(wrapper) - 1);
  lv_obj_align(weak, LV_ALIGN_LEFT_MID, 4, 10);

  make_label(wrapper, "강", &ui_font_xs, TEXT_DIM);
  lv_obj_t *strong = lv_obj_get_child(wrapper, lv_obj_get_child_cnt(wrapper) - 1);
  lv_obj_align(strong, LV_ALIGN_RIGHT_MID, -4, 10);

  make_label(col, tag, &ui_font_md, TEXT_PRIMARY);

  return col;
}

void build_card_climate(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // "전열교환 (HEAT RECOVERY)" pill
  lv_obj_t *pill = make_plain(card);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, 32);
  lv_obj_set_style_bg_color(pill, c(BADGE_BG), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill, 16, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, c(BADGE_BORDER), 0);
  lv_obj_set_style_pad_hor(pill, 12, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 6, 0);
  ui_icons::refresh(pill, 18, c(BADGE_TEXT));
  make_label(pill, "전열교환 (HEAT RECOVERY)", &ui_font_sm, BADGE_TEXT);

  // Supply / exhaust fan gauges
  lv_obj_t *gauges_row = make_row(card, LV_PCT(100), 170, 16);
  lv_obj_set_flex_align(gauges_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  build_fan_gauge(gauges_row, "급기", 55, &s_supply_arc, &s_supply_level_label);
  build_fan_gauge(gauges_row, "배기", 60, &s_exhaust_arc, &s_exhaust_level_label);

  // Humidify reservation
  lv_obj_t *resv = make_row(card, LV_PCT(100), 24, 6);
  lv_obj_set_flex_align(resv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  ui_icons::clock(resv, 16, c(TEXT_SECONDARY));
  s_reservation_label = make_label(resv, "가습예약 08:00", &ui_font_sm, TEXT_SECONDARY);
}

} // namespace

lv_obj_t *create(lv_obj_t *parent) {
  lv_obj_t *root = make_plain(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(root, c(BG), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(root, 14, 0);
  lv_obj_set_style_pad_row(root, 10, 0);

  ui_dashboard_chrome::build_top_bar(root);

  lv_obj_t *mid_row = make_row(root, LV_PCT(100), 270, 12);
  build_card_aqi(mid_row);
  build_card_climate(mid_row);

  ui_dashboard_chrome::build_bottom_bar(root);

  return root;
}

// ---------------------------------------------------------------------
// Live-data setters. Not wired to anything yet - hook these up once the
// corresponding sensor / RTC / network IO is defined.
// ---------------------------------------------------------------------

void set_aqi(int aqi, const char *label) {
  if (s_aqi_num_label) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", aqi);
    lv_label_set_text(s_aqi_num_label, buf);
  }
  if (s_aqi_status_label) lv_label_set_text(s_aqi_status_label, label);
  if (s_aqi_arc) {
    int comfort = 100 - aqi;
    if (comfort < 0) comfort = 0;
    if (comfort > 100) comfort = 100;
    lv_arc_set_value(s_aqi_arc, comfort);
  }
}

void set_indoor(float temp_c, int humidity_pct) {
  if (s_aqi_temp_label) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", temp_c);
    lv_label_set_text(s_aqi_temp_label, buf);
  }
  if (s_aqi_rh_label) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%% RH", humidity_pct);
    lv_label_set_text(s_aqi_rh_label, buf);
  }
}

void set_fan_supply(int level_pct, const char *level_label) {
  if (s_supply_arc) lv_arc_set_value(s_supply_arc, level_pct);
  if (s_supply_level_label) lv_label_set_text(s_supply_level_label, level_label);
}

void set_fan_exhaust(int level_pct, const char *level_label) {
  if (s_exhaust_arc) lv_arc_set_value(s_exhaust_arc, level_pct);
  if (s_exhaust_level_label) lv_label_set_text(s_exhaust_level_label, level_label);
}

void set_humidify_reservation(int hour24, int minute) {
  if (!s_reservation_label) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "가습예약 %02d:%02d", hour24, minute);
  lv_label_set_text(s_reservation_label, buf);
}

} // namespace ui_main_screen
