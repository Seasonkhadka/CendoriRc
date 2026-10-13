#include "ui_dashboard_chrome.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "fonts/ui_fonts.h"

#include <stdio.h>

using namespace ui_theme;

namespace ui_dashboard_chrome {

namespace {

// ---- widgets the setter API below needs to reach again later -------------
// build_top_bar()/build_bottom_bar() are each called ONCE PER DASHBOARD
// DESIGN (main.cpp builds both design 1 and design 2 at boot, so they can
// be swapped live with LEFT/RIGHT). Each call creates its own fresh set of
// label objects, so a single static pointer here would just get
// overwritten by the second call, silently orphaning the first design's
// widgets (they'd stay on screen but never update again). Small fixed
// arrays - one slot per design - so every setter below updates all of them.
constexpr int kMaxInstances = 2;

lv_obj_t *s_mode_label[kMaxInstances] = {};
lv_obj_t *s_wifi_icon[kMaxInstances] = {};
lv_obj_t *s_weather_label[kMaxInstances] = {};
lv_obj_t *s_time_label[kMaxInstances] = {};
lv_obj_t *s_filter_label[kMaxInstances] = {};
int s_topBarCount = 0;

lv_obj_t *s_pm25_val[kMaxInstances] = {};
lv_obj_t *s_pm10_val[kMaxInstances] = {};
lv_obj_t *s_co2_val[kMaxInstances] = {};
lv_obj_t *s_tvoc_val[kMaxInstances] = {};
lv_obj_t *s_co_val[kMaxInstances] = {};
lv_obj_t *s_temp_val[kMaxInstances] = {};
lv_obj_t *s_hum_val[kMaxInstances] = {};
int s_bottomBarCount = 0;

// ---- small style helpers (same convention as every other ui_*.cpp) ----

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

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  return l;
}

// ---- bottom sensor strip -------------------------------------------------

lv_obj_t *build_stat_card(lv_obj_t *parent, lv_obj_t *(*icon_fn)(lv_obj_t *, lv_coord_t, lv_color_t),
                           const char *label, const char *value, const char *unit, lv_obj_t **out_value,
                           lv_obj_t **out_unit = nullptr, bool compact_value = false) {
  lv_obj_t *card = make_plain(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_bg_color(card, c(CARD_BG_ALT), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_clip_corner(card, true, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, c(BORDER), 0);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 4, 0);

  lv_obj_t *top = make_row(card, LV_PCT(100), LV_SIZE_CONTENT, 4);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (icon_fn) icon_fn(top, 15, c(TEXT_SECONDARY));
  make_label(top, label, &ui_font_xs, TEXT_SECONDARY);

  lv_obj_t *values = make_plain(card);
  lv_obj_set_width(values, LV_PCT(100));
  lv_obj_set_height(values, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(values, compact_value ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(values, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(values, 0, 0);
  lv_obj_set_style_pad_column(values, 3, 0);

  const lv_font_t *value_font = compact_value ? &ui_font_sm : &ui_font_md;
  lv_obj_t *val = make_label(values, value, value_font, TEXT_PRIMARY);
  lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
  if (unit && unit[0]) {
    const lv_font_t *unit_font = compact_value ? value_font : &ui_font_xs;
    uint32_t unit_color = compact_value ? TEXT_PRIMARY : TEXT_DIM;
    lv_obj_t *unit_lbl = make_label(values, unit, unit_font, unit_color);
    if (out_unit) *out_unit = unit_lbl;
  }
  if (out_value) *out_value = val;

  return card;
}

} // namespace

lv_obj_t *build_top_bar(lv_obj_t *parent) {
  int idx = (s_topBarCount < kMaxInstances) ? s_topBarCount++ : kMaxInstances - 1;

  lv_obj_t *bar = make_plain(parent);
  lv_obj_set_size(bar, LV_PCT(100), 52);
  lv_obj_set_style_bg_color(bar, c(CARD_BG), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, 14, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, c(BORDER), 0);
  lv_obj_set_style_pad_hor(bar, 18, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Wi-Fi / operating mode
  lv_obj_t *g1 = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 6);
  s_wifi_icon[idx] = ui_icons::wifi(g1, 20, c(TEXT_PRIMARY));
  s_mode_label[idx] = make_label(g1, "전열모드", &ui_font_sm, TEXT_PRIMARY);

  // Weather
  lv_obj_t *g2 = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 6);
  ui_icons::sun(g2, 20, c(WARN));
  s_weather_label[idx] = make_label(g2, "맑음 22°C", &ui_font_sm, TEXT_SECONDARY);

  // Clock
  lv_obj_t *g3 = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 6);
  ui_icons::clock(g3, 18, c(TEXT_SECONDARY));
  s_time_label[idx] = make_label(g3, "PM 02:30", &ui_font_sm, TEXT_SECONDARY);

  // Filter status
  s_filter_label[idx] = make_label(bar, "필터 정상", &ui_font_sm, GOOD);

  return bar;
}

lv_obj_t *build_bottom_bar(lv_obj_t *parent) {
  int idx = (s_bottomBarCount < kMaxInstances) ? s_bottomBarCount++ : kMaxInstances - 1;

  lv_obj_t *bar = make_row(parent, LV_PCT(100), 110, 8);

  // PM2.5/PM10/TVOC/CO show "--" until a real, calibrated reading is wired
  // in (see sensors.cpp) - a dash is honest about "no data yet"; a static
  // placeholder number like "12" or "0.08" looks like a live reading and
  // isn't. CO2 and Temp/Humidity are genuinely live (SCD40/SHT40), so they
  // keep real starting values.
  build_stat_card(bar, ui_icons::dust, "PM2.5:", "--", "ug/m3", &s_pm25_val[idx]);
  build_stat_card(bar, ui_icons::dust, "PM10:", "--", "ug/m3", &s_pm10_val[idx]);
  build_stat_card(bar, ui_icons::cloud, "CO2:", "560", "ppm", &s_co2_val[idx]);
  build_stat_card(bar, nullptr, "TVOC:", "--", "", &s_tvoc_val[idx]);
  build_stat_card(bar, ui_icons::gasring, "CO:", "--", "ppm", &s_co_val[idx]);
  build_stat_card(bar, ui_icons::thermo, "Temp:", "24.5°C", "48%", &s_temp_val[idx], &s_hum_val[idx], true);

  return bar;
}

// ---------------------------------------------------------------------
// Live-data setters. Not wired to anything yet - hook these up once the
// corresponding sensor / RTC / network IO is defined.
// ---------------------------------------------------------------------

void set_wifi_connected(bool connected) {
  for (int i = 0; i < s_topBarCount; i++) {
    if (s_wifi_icon[i]) lv_obj_set_style_opa(s_wifi_icon[i], connected ? LV_OPA_COVER : LV_OPA_40, 0);
  }
}

void set_mode_text(const char *text) {
  for (int i = 0; i < s_topBarCount; i++) {
    if (s_mode_label[i]) lv_label_set_text(s_mode_label[i], text);
  }
}

void set_weather(const char *summary, float outdoor_c) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %.0f°C", summary, outdoor_c);
  for (int i = 0; i < s_topBarCount; i++) {
    if (s_weather_label[i]) lv_label_set_text(s_weather_label[i], buf);
  }
}

void set_time(int hour24, int minute) {
  const char *ampm = (hour24 < 12) ? "AM" : "PM";
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%s %02d:%02d", ampm, hour12, minute);
  for (int i = 0; i < s_topBarCount; i++) {
    if (s_time_label[i]) lv_label_set_text(s_time_label[i], buf);
  }
}

void set_filter_status(const char *text, bool ok) {
  for (int i = 0; i < s_topBarCount; i++) {
    if (!s_filter_label[i]) continue;
    lv_label_set_text(s_filter_label[i], text);
    lv_obj_set_style_text_color(s_filter_label[i], c(ok ? GOOD : DANGER), 0);
  }
}

void set_pm25(float ug_m3) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f", ug_m3);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_pm25_val[i]) lv_label_set_text(s_pm25_val[i], buf);
  }
}

void set_pm10(float ug_m3) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f", ug_m3);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_pm10_val[i]) lv_label_set_text(s_pm10_val[i], buf);
  }
}

void set_co2(int ppm) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", ppm);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_co2_val[i]) lv_label_set_text(s_co2_val[i], buf);
  }
}

void set_tvoc(int voc_index) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", voc_index);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_tvoc_val[i]) lv_label_set_text(s_tvoc_val[i], buf);
  }
}

void set_co(float ppm) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%.1f", ppm);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_co_val[i]) lv_label_set_text(s_co_val[i], buf);
  }
}

void set_temp_humidity(float temp_c, int humidity_pct) {
  char tbuf[12];
  snprintf(tbuf, sizeof(tbuf), "%.1f°C", temp_c);
  char hbuf[8];
  snprintf(hbuf, sizeof(hbuf), "%d%%", humidity_pct);
  for (int i = 0; i < s_bottomBarCount; i++) {
    if (s_temp_val[i]) lv_label_set_text(s_temp_val[i], tbuf);
    if (s_hum_val[i]) lv_label_set_text(s_hum_val[i], hbuf);
  }
}

} // namespace ui_dashboard_chrome
