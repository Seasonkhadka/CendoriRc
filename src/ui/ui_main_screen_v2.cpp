#include "ui_main_screen_v2.h"
#include "ui_dashboard_chrome.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "fonts/ui_fonts.h"
#include "ui_lang.h"

#include <math.h>
#include <stdio.h>

using namespace ui_theme;
using ui_lang::tr;

namespace ui_main_screen_v2 {

namespace {

// ---- widgets the setter API below needs to reach again later ----------
lv_obj_t *s_tooltip_temp_label = nullptr;
lv_obj_t *s_tooltip_rh_label = nullptr;
lv_obj_t *s_mode_badge_label = nullptr;
lv_obj_t *s_aqi_num_label = nullptr;
lv_obj_t *s_aqi_status_label = nullptr;
lv_obj_t *s_trend_dot = nullptr;

constexpr int kSegCount = 6;
lv_obj_t *s_fan_segs[kSegCount] = {};  // 0 = bottom
lv_obj_t *s_fan_level_labels[3] = {};  // 0 약, 1 중, 2 강
lv_obj_t *s_vsp_supply_label = nullptr;
lv_obj_t *s_vsp_exhaust_label = nullptr;

// ---- small style helpers (same convention as ui_main_screen.cpp / ---
// ui_settings_screen.cpp - kept local rather than shared, since each
// screen's component set differs enough that a shared header wouldn't
// save much and would couple unrelated files together).

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

lv_obj_t *make_card(lv_obj_t *parent, uint32_t bg) {
  lv_obj_t *card = make_plain(parent);
  lv_obj_set_style_bg_color(card, c(bg), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, c(BORDER), 0);
  elevate(card);
  return card;
}

// ---- AQI trend-chart card ------------------------------------------

constexpr int kSamples = 49; // every 30 min across 24h - dense enough
                             // that straight segments between points
                             // read as a smooth curve.
constexpr lv_coord_t kPlotW = 350;
lv_obj_t *s_trend_fill[kSamples] = {};      // fill columns, recoloured by AQI level
lv_obj_t *s_trend_line[kSamples - 1] = {};  // stroke segments
constexpr lv_coord_t kPlotH = 176; // fits the 270px card (shared with
                                    // design 1) alongside the header and
                                    // axis-label rows - see build_trend_card

// Static demo curve: a gentle morning peak (~7h) plus a small evening
// rise, shaped to resemble the reference mockup. Not live data.
float trend_value(float hour) {
  float d = hour - 7.0f;
  float hump = 0.42f * expf(-(d * d) / (2.0f * 3.0f * 3.0f));
  float tail = hour > 16.0f ? 0.10f * ((hour - 16.0f) / 8.0f) : 0.0f;
  float v = 0.38f + hump + tail;
  if (v < 0.05f) v = 0.05f;
  if (v > 0.95f) v = 0.95f;
  return v;
}

void build_trend_card(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 8, 0);

  lv_obj_t *header = make_row(card, LV_PCT(100), 30, 6);
  lv_obj_set_flex_grow(header, 0);
  make_label(header, "AQI", &ui_font_md, TEXT_SECONDARY);
  s_aqi_num_label = make_label(header, "--", &ui_font_md, TEXT_PRIMARY);
  make_label(header, "/", &ui_font_md, TEXT_DIM);
  s_aqi_status_label = make_label(header, "-", &ui_font_md, AQI_NONE);

  // Precompute the curve once - reused for the fill columns, the stroke
  // segments and the endpoint marker.
  lv_coord_t xs[kSamples], ys[kSamples];
  for (int i = 0; i < kSamples; i++) {
    float hour = (float)i * 24.0f / (kSamples - 1);
    float v = trend_value(hour);
    xs[i] = (lv_coord_t)((float)i * kPlotW / (kSamples - 1));
    ys[i] = kPlotH - (lv_coord_t)(v * kPlotH);
  }

  lv_obj_t *plot = make_plain(card);
  lv_obj_set_size(plot, kPlotW, kPlotH);
  lv_obj_add_flag(plot, LV_OBJ_FLAG_OVERFLOW_VISIBLE); // tooltip may sit right at the top edge

  // Gradient-filled area under the curve: one thin column per sample,
  // fading from accent cyan at the curve down to the card's own
  // background colour - LVGL gradients only interpolate colour (not
  // opacity), so blending to the card's bg colour is what makes the
  // bottom read as "fading away" instead of a hard-edged rectangle.
  lv_coord_t col_w = kPlotW / (kSamples - 1) + 2;
  for (int i = 0; i < kSamples; i++) {
    lv_coord_t h = kPlotH - ys[i];
    if (h < 2) h = 2;
    lv_obj_t *col = lv_obj_create(plot);
    s_trend_fill[i] = col;
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, col_w, h);
    lv_obj_set_pos(col, xs[i] - col_w / 2, kPlotH - h);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(col, c(AQI_NONE), 0);
    lv_obj_set_style_bg_grad_color(col, c(CARD_BG), 0);
    lv_obj_set_style_bg_grad_dir(col, LV_GRAD_DIR_VER, 0);
  }

  // The stroke line itself, drawn as short segments on top of the fill.
  for (int i = 1; i < kSamples; i++) {
    lv_point_t *pts = new lv_point_t[2]{{xs[i - 1], ys[i - 1]}, {xs[i], ys[i]}};
    lv_obj_t *seg = lv_line_create(plot);
    s_trend_line[i - 1] = seg;
    lv_line_set_points(seg, pts, 2);
    lv_obj_set_style_line_width(seg, 3, 0);
    lv_obj_set_style_line_color(seg, c(AQI_NONE), 0);
    lv_obj_set_style_line_rounded(seg, true, 0);
  }

  // Current-reading marker at the last sample.
  lv_coord_t ex = xs[kSamples - 1], ey = ys[kSamples - 1];
  lv_obj_t *dot = lv_obj_create(plot);
  s_trend_dot = dot;
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, c(TEXT_PRIMARY), 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_set_style_border_color(dot, c(AQI_NONE), 0);
  lv_obj_set_pos(dot, ex - 5, ey - 5);

  // Tooltip bubble showing the current reading.
  lv_obj_t *bubble = make_card(plot, CARD_BG_ALT);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(bubble, 10, 0);
  lv_obj_set_style_pad_ver(bubble, 6, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  s_tooltip_temp_label = make_label(bubble, "--", &ui_font_sm, TEXT_PRIMARY);
  s_tooltip_rh_label = make_label(bubble, "--", &ui_font_xs, TEXT_SECONDARY);
  lv_coord_t bubbleY = ey - 62;
  if (bubbleY < 0) bubbleY = 0;
  lv_coord_t bubbleX = ex - 95;
  if (bubbleX < 0) bubbleX = 0;
  lv_obj_set_pos(bubble, bubbleX, bubbleY);

  // X-axis hour labels.
  lv_obj_t *axis = make_row(card, LV_PCT(100), 18, 0);
  lv_obj_set_flex_grow(axis, 0);
  lv_obj_set_flex_align(axis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const char *hourLabels[5] = {"00:00", "06:00", "12:00", "16:00", "24:00"};
  for (int i = 0; i < 5; i++) make_label(axis, hourLabels[i], &ui_font_xs, TEXT_DIM);
}

// ---- heat-recovery card with segmented fan-level meters --------------

constexpr int kLitDemo = 4; // initial "중" level until the first RC refresh
constexpr lv_coord_t kSegW = 64; // fan level segment width

void format_vsp_text(char *buf, size_t n, int units) {
  if (units <= 0) {
    snprintf(buf, n, "--");
    return;
  }
  const int cv = units * 5;
  snprintf(buf, n, "%d.%02dV", cv / 100, cv % 100);
}

// Lights the bottom 2 segments per fan level (1 약 / 2 중 / 3 강) and
// highlights the matching level label.
void style_fan_meter(int level) {
  const int lit = level * 2;
  for (int seg = 0; seg < kSegCount; seg++) {
    lv_obj_t *s = s_fan_segs[seg];
    if (!s) continue;
    const bool on = seg < lit;
    const bool marker = seg == lit - 1;
    lv_obj_set_style_bg_color(s, c(!on ? TRACK : (marker ? TEXT_PRIMARY : ACCENT_CYAN)), 0);
    lv_obj_set_style_shadow_width(s, marker ? 8 : 0, 0);
    lv_obj_set_style_shadow_color(s, c(ACCENT_CYAN), 0);
    lv_obj_set_style_shadow_opa(s, LV_OPA_60, 0);
  }
  for (int i = 0; i < 3; i++) {
    lv_obj_t *l = s_fan_level_labels[i];
    if (!l) continue;
    const bool cur = (i + 1) == level;
    lv_obj_set_style_text_color(l, c(cur ? ACCENT_CYAN : TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(l, cur ? &ui_font_sm : &ui_font_xs, 0);
  }
}

lv_obj_t *build_vsp_col(lv_obj_t *parent, const char *tag, lv_obj_t **out_value) {
  lv_obj_t *col = make_plain(parent);
  lv_obj_set_size(col, 78, LV_PCT(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 4, 0);
  make_label(col, tag, &ui_font_sm, TEXT_SECONDARY);
  *out_value = make_label(col, "--", &ui_font_md, TEXT_PRIMARY);
  return col;
}

lv_obj_t *build_fan_diagram(lv_obj_t *parent) {
  lv_obj_t *row = make_row(parent, LV_PCT(100), LV_PCT(100), 10);
  lv_obj_set_flex_grow(row, 1);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_vsp_col(row, tr("급기", "Supply"), &s_vsp_supply_label);

  lv_obj_t *meterRow = make_row(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 8);

  const lv_coord_t meterH = 134; // fits the 270px card alongside the
                                  // badge pill and reservation rows

  lv_obj_t *labels = make_plain(meterRow);
  lv_obj_set_size(labels, LV_SIZE_CONTENT, meterH);
  lv_obj_set_flex_flow(labels, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(labels, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  s_fan_level_labels[2] = make_label(labels, tr("강", "High"), &ui_font_xs, TEXT_SECONDARY);
  s_fan_level_labels[1] = make_label(labels, tr("중", "Mid"), &ui_font_sm, ACCENT_CYAN);
  s_fan_level_labels[0] = make_label(labels, tr("약", "Low"), &ui_font_xs, TEXT_SECONDARY);

  lv_obj_t *bar = make_plain(meterRow);
  lv_obj_set_size(bar, kSegW, meterH);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int seg = kSegCount - 1; seg >= 0; seg--) {
    bool lit = seg < kLitDemo;
    bool marker = seg == kLitDemo - 1; // topmost lit segment = current level

    lv_obj_t *s = lv_obj_create(bar);
    s_fan_segs[seg] = s;
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, LV_PCT(100), 20);
    lv_obj_set_style_radius(s, 3, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, c(!lit ? TRACK : (marker ? TEXT_PRIMARY : ACCENT_CYAN)), 0);
    if (marker) {
      lv_obj_set_style_shadow_width(s, 8, 0);
      lv_obj_set_style_shadow_color(s, c(ACCENT_CYAN), 0);
      lv_obj_set_style_shadow_opa(s, LV_OPA_60, 0);
    }
  }

  build_vsp_col(row, tr("배기", "Exhaust"), &s_vsp_exhaust_label);
  return row;
}

void build_heat_recovery_card(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 8, 0);

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
  s_mode_badge_label = make_label(pill, tr("전열교환", "HEAT RECOVERY"), &ui_font_sm, BADGE_TEXT);

  build_fan_diagram(card);

  lv_obj_t *resv = make_row(card, LV_PCT(100), 24, 6);
  lv_obj_set_flex_grow(resv, 0);
  lv_obj_set_flex_align(resv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  ui_icons::clock(resv, 16, c(TEXT_SECONDARY));
  make_label(resv, tr("가동예약 08:00", "Start 08:00"), &ui_font_sm, TEXT_SECONDARY);
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
  build_trend_card(mid_row);
  build_heat_recovery_card(mid_row);

  ui_dashboard_chrome::build_bottom_bar(root);

  return root;
}

void set_aqi(int aqi, const char *label, uint32_t color) {
  if (s_aqi_num_label) {
    char buf[16];
    if (aqi < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d", aqi);
    lv_label_set_text(s_aqi_num_label, buf);
  }
  if (s_aqi_status_label) {
    lv_label_set_text(s_aqi_status_label, label);
    lv_obj_set_style_text_color(s_aqi_status_label, c(color), 0);
  }
  // Graph follows the AQI level colour, same as the dashboard A ring.
  for (lv_obj_t *o : s_trend_fill)
    if (o) lv_obj_set_style_bg_color(o, c(color), 0);
  for (lv_obj_t *o : s_trend_line)
    if (o) lv_obj_set_style_line_color(o, c(color), 0);
  if (s_trend_dot) lv_obj_set_style_border_color(s_trend_dot, c(color), 0);
}

void set_indoor(float temp_c, int humidity_pct) {
  if (s_tooltip_temp_label) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", temp_c);
    lv_label_set_text(s_tooltip_temp_label, buf);
  }
  if (s_tooltip_rh_label) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%% RH", humidity_pct);
    lv_label_set_text(s_tooltip_rh_label, buf);
  }
}

void set_mode_badge(const char *text) {
  if (s_mode_badge_label) lv_label_set_text(s_mode_badge_label, text);
}

void set_fan_level(int level) {
  if (level < 0 || level > 3) return;
  style_fan_meter(level);
}

void set_fan_vsp(int supply_units, int exhaust_units) {
  char buf[16];
  if (s_vsp_supply_label) {
    format_vsp_text(buf, sizeof(buf), supply_units);
    lv_label_set_text(s_vsp_supply_label, buf);
  }
  if (s_vsp_exhaust_label) {
    format_vsp_text(buf, sizeof(buf), exhaust_units);
    lv_label_set_text(s_vsp_exhaust_label, buf);
  }
}

} // namespace ui_main_screen_v2
