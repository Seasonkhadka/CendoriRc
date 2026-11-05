#include "ui_main_screen_v3.h"
#include "ui_icons.h"
#include "fonts/ui_fonts.h"
#include "ui_lang.h"

#include <stdio.h>

// Pre-rendered artwork (tools/gen_dash_c_art.py). The layout constants
// below must match that script (ring centre / size, airflow panel).
LV_IMG_DECLARE(dash_c_bg);
LV_IMG_DECLARE(dash_c_glow);
LV_IMG_DECLARE(dash_c_ring_good);
LV_IMG_DECLARE(dash_c_ring_normal);
LV_IMG_DECLARE(dash_c_ring_bad);
LV_IMG_DECLARE(dash_c_ring_vbad);
LV_IMG_DECLARE(dash_c_ic_pinwheel);
LV_IMG_DECLARE(dash_c_ic_alarm);

using ui_lang::tr;

namespace ui_main_screen_v3 {

namespace {

// ---- palette (sampled from the reference mockup) -------------------------
constexpr uint32_t TXT = 0x2F3437;       // values
constexpr uint32_t TXT2 = 0x4A4F52;      // labels, header
constexpr uint32_t TXT3 = 0x8C9196;      // scale, dim icons
constexpr uint32_t GLASS = 0xFFFFFF;
constexpr uint32_t ALERT = 0xDC2626;
constexpr uint32_t OK_TXT = 0x14917F;    // filter normal (A/B use their green)
constexpr uint32_t BAR_FROM = 0x1E7FE0;  // fan bar: blue -> cyan, lavender track
constexpr uint32_t BAR_TO = 0x7BE8F4;
constexpr uint32_t BAR_TRACK = 0xDCD3F7;

// AQI level colours for text / glow (index = AQI::Level)
constexpr uint32_t kLevelColor[5] = {0x8C9196, 0x2AA597, 0x16A34A, 0xEA580C, 0xDC2626};
constexpr uint32_t kGlowColor[5] = {0xB8C2CC, 0x2CC9D6, 0x4ADE80, 0xFDBA74, 0xFCA5A5};
const char *const kLevelText[5] = {"--", "EXCELLENT", "MODERATE", "POOR", "VERY POOR"};
const lv_img_dsc_t *const kRingImg[5] = {&dash_c_ring_good, &dash_c_ring_good, &dash_c_ring_normal,
                                          &dash_c_ring_bad, &dash_c_ring_vbad};

// ---- layout (800 x 480, measured from the reference) -------------------
constexpr lv_coord_t kRingCx = 162, kRingCy = 238, kRingSize = 250, kRingWidth = 28, kGlowSize = 330;
// Top bar and bottom cards keep the A/B sizes: 14 px screen padding, 52 px
// bar, 10 px gaps.
constexpr lv_coord_t kPad = 14, kBarH = 52;
constexpr lv_coord_t kMidX = 323, kMidY = 76, kMidW = 250, kMidH = 322;
constexpr lv_coord_t kCardX = 576, kCardW = 210;
constexpr lv_coord_t kPillY = 408, kPillH = 58;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// ---- widgets the setters reach -----------------------------------------
lv_obj_t *s_wifiIcon = nullptr;
lv_obj_t *s_wifiText = nullptr;
lv_obj_t *s_weather = nullptr;
lv_obj_t *s_clock = nullptr;
lv_obj_t *s_filter = nullptr;
lv_obj_t *s_aqiHead = nullptr;
lv_obj_t *s_aqiWord = nullptr;
lv_obj_t *s_aqiNum = nullptr;
lv_obj_t *s_aqiText = nullptr;
lv_obj_t *s_glow = nullptr;
lv_obj_t *s_ring = nullptr;
lv_obj_t *s_temp = nullptr;
lv_obj_t *s_rh = nullptr;
lv_obj_t *s_btnMode = nullptr;
lv_obj_t *s_btnModeText = nullptr;
lv_obj_t *s_btnModeDetail = nullptr;
lv_obj_t *s_fanBar = nullptr;
lv_obj_t *s_fanIcon = nullptr;
lv_obj_t *s_fanLevel = nullptr;
lv_obj_t *s_vsp[2] = {};  // supply, exhaust
lv_obj_t *s_resvName = nullptr;
lv_obj_t *s_resvTime = nullptr;
lv_obj_t *s_pillVal[6] = {};
lv_obj_t *s_humVal = nullptr;

lv_obj_t *make_plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  return l;
}

// Recolourable ALPHA_8 icon.
lv_obj_t *make_icon(lv_obj_t *parent, const lv_img_dsc_t *src, uint32_t color) {
  lv_obj_t *i = lv_img_create(parent);
  lv_img_set_src(i, src);
  lv_obj_set_style_img_recolor(i, c(color), 0);
  lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
  return i;
}

void recolor(lv_obj_t *icon, uint32_t color) {
  if (icon) lv_obj_set_style_img_recolor(icon, c(color), 0);
}

// Frosted panel: translucent white over the artwork, thin white rim.
lv_obj_t *make_glass(lv_obj_t *parent, lv_opa_t opa, lv_coord_t radius, lv_opa_t rim = LV_OPA_70) {
  lv_obj_t *o = make_plain(parent);
  lv_obj_set_style_bg_color(o, c(GLASS), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, c(GLASS), 0);
  lv_obj_set_style_border_opa(o, rim, 0);
  return o;
}

lv_obj_t *make_row(lv_obj_t *parent, lv_coord_t gap) {
  lv_obj_t *o = make_plain(parent);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

lv_obj_t *make_column(lv_obj_t *parent, lv_coord_t gap) {
  lv_obj_t *o = make_plain(parent);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}

// Centres a content-sized object on (cx, cy) once its size is known.
void place_center(lv_obj_t *o, lv_coord_t cx, lv_coord_t cy) {
  lv_obj_update_layout(o);
  lv_obj_set_pos(o, cx - lv_obj_get_width(o) / 2, cy - lv_obj_get_height(o) / 2);
}

// ---- top bar: same elements and size as the A/B bar, light glass style --

void build_top_bar(lv_obj_t *root) {
  lv_obj_t *bar = make_glass(root, LV_OPA_50, 14, LV_OPA_80);
  lv_obj_set_pos(bar, kPad, kPad);
  lv_obj_set_size(bar, 800 - 2 * kPad, kBarH);
  lv_obj_set_style_pad_hor(bar, 18, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Wi-Fi state
  lv_obj_t *g1 = make_row(bar, 6);
  s_wifiIcon = ui_icons::wifi(g1, 20, c(TXT2));
  s_wifiText = make_label(g1, tr("미연결", "Not connected"), &ui_font_sm, TXT2);
  // Weather
  lv_obj_t *g2 = make_row(bar, 6);
  ui_icons::sun(g2, 20, c(0xF59E0B));
  s_weather = make_label(g2, "--", &ui_font_sm, TXT2);
  // Clock (date + time)
  s_clock = make_label(bar, "--", &ui_font_sm, TXT2);
  // Filter status
  s_filter = make_label(bar, tr("필터 정상", "Filter OK"), &ui_font_sm, OK_TXT);
}

// ---- AQI header, ring glow, ring, indoor readout -----------------------

void build_aqi(lv_obj_t *root) {
  s_aqiHead = make_row(root, 10);
  lv_obj_set_flex_align(s_aqiHead, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_t *nums = make_row(s_aqiHead, 8);
  s_aqiWord = make_label(nums, "AQI", &ui_font_aqi, kLevelColor[0]);
  s_aqiNum = make_label(nums, "--", &ui_font_aqi, kLevelColor[0]);
  s_aqiText = make_label(s_aqiHead, kLevelText[0], &ui_font_xs, kLevelColor[0]);
  lv_obj_set_style_text_letter_space(s_aqiText, 1, 0);
  lv_obj_set_style_pad_bottom(s_aqiText, 6, 0);
  place_center(s_aqiHead, kRingCx + 6, 94);

  // Soft halo in the level colour (ALPHA_8 image, recoloured).
  s_glow = lv_img_create(root);
  lv_img_set_src(s_glow, &dash_c_glow);
  lv_obj_set_pos(s_glow, kRingCx - kGlowSize / 2, kRingCy - kGlowSize / 2);
  lv_obj_set_style_img_recolor(s_glow, c(kGlowColor[0]), 0);
  lv_obj_set_style_img_recolor_opa(s_glow, LV_OPA_COVER, 0);

  // Ring: an arc whose indicator is masked out of a conic-gradient image
  // (one per AQI level); fuller ring = cleaner air.
  s_ring = lv_arc_create(root);
  lv_obj_remove_style_all(s_ring);
  lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_ring, kRingSize, kRingSize);
  lv_obj_set_pos(s_ring, kRingCx - kRingSize / 2, kRingCy - kRingSize / 2);
  lv_arc_set_rotation(s_ring, 270);
  lv_arc_set_bg_angles(s_ring, 0, 360);
  lv_arc_set_range(s_ring, 0, 100);
  lv_arc_set_value(s_ring, 0);
  lv_obj_set_style_arc_width(s_ring, kRingWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_ring, c(GLASS), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_ring, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_ring, kRingWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_ring, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_img_src(s_ring, kRingImg[0], LV_PART_INDICATOR);

  // "24.5°C" (°C raised) and "48 %/RH" below it.
  lv_obj_t *tRow = make_row(root, 2);
  lv_obj_set_flex_align(tRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  s_temp = make_label(tRow, "--", &ui_font_hero, TXT);
  lv_obj_t *tUnit = make_label(tRow, "°C", &ui_font_md, TXT);
  lv_obj_set_style_pad_top(tUnit, 8, 0);

  lv_obj_t *hRow = make_row(root, 3);
  lv_obj_set_flex_align(hRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_rh = make_label(hRow, "--", &ui_font_xxl, TXT);
  lv_obj_t *hUnit = make_column(hRow, -6);
  make_label(hUnit, "%", &ui_font_sm, TXT);
  make_label(hUnit, "RH", &ui_font_tiny, TXT2);

  place_center(tRow, kRingCx, kRingCy - 4);
  place_center(hRow, kRingCx, kRingCy + 52);
}

// Keeps the readout centred on the ring after its text width changes.
void recenter_readout() {
  if (s_temp) place_center(lv_obj_get_parent(s_temp), kRingCx, kRingCy - 4);
  if (s_rh) place_center(lv_obj_get_parent(s_rh), kRingCx, kRingCy + 52);
}

// ---- airflow panel (the crescents are in the background image) ---------

void build_airflow(lv_obj_t *root) {
  lv_obj_t *panel = make_glass(root, LV_OPA_10, 20, LV_OPA_40);
  lv_obj_set_pos(panel, kMidX, kMidY);
  lv_obj_set_size(panel, kMidW, kMidH);

  // Fan name + a frosted chip with that fan's VSP voltage for the current speed.
  const char *names[2] = {tr("급기", "Supply"), tr("배기", "Exhaust")};
  const lv_coord_t pos[2][2] = {{338, 164}, {494, 260}};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *col = make_column(root, 4);
    lv_obj_set_pos(col, pos[i][0], pos[i][1]);
    make_label(col, names[i], &ui_font_md, TXT);
    lv_obj_t *chip = make_glass(col, LV_OPA_70, 10, LV_OPA_90);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 3, 0);
    s_vsp[i] = make_label(chip, "--", &ui_font_sm, TXT);  // value only, e.g. "2.40V"
  }
}

// ---- SMART AI VENTILATION card ----------------------------------------

lv_obj_t *make_mode_button(lv_obj_t *parent, lv_coord_t w, const char *text, lv_obj_t **label_out) {
  lv_obj_t *b = make_glass(parent, LV_OPA_60, 10);
  lv_obj_set_size(b, w, 35);
  lv_obj_t *l = make_label(b, text, &ui_font_xs, TXT2);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

void style_mode_button(lv_obj_t *b, bool on) {
  if (!b) return;
  lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_60, 0);
  lv_obj_set_style_shadow_width(b, on ? 12 : 0, 0);
  lv_obj_set_style_shadow_color(b, c(0x9EE7EF), 0);
  lv_obj_set_style_shadow_opa(b, LV_OPA_60, 0);
  lv_obj_t *l = lv_obj_get_child(b, 0);
  if (l) lv_obj_set_style_text_color(l, c(on ? 0x0E8A9A : TXT2), 0);
}

lv_obj_t *make_card_row(lv_obj_t *card, lv_coord_t y, lv_coord_t h) {
  lv_obj_t *row = make_glass(card, LV_OPA_60, 12, LV_OPA_80);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_size(row, kCardW - 2 * 16, h);
  return row;
}

// One fan row: the protocol has a single shared fan speed (E5 D1 / B5 D3).
void build_fan_row(lv_obj_t *card, lv_coord_t y, lv_coord_t h) {
  lv_obj_t *row = make_card_row(card, y, h);

  lv_obj_t *title = make_label(row, tr("풍량", "Fan speed"), &ui_font_xs, TXT2);
  lv_obj_set_pos(title, 12, 12);
  s_fanLevel = make_label(row, "--", &ui_font_sm, TXT);
  lv_obj_align(s_fanLevel, LV_ALIGN_TOP_RIGHT, -12, 8);

  const lv_coord_t mid = 60;  // 40 px fan icon, larger than the text
  s_fanIcon = make_icon(row, &dash_c_ic_pinwheel, 0x2B2F33);
  lv_obj_set_pos(s_fanIcon, 8, mid - 20);

  lv_obj_t *bar = lv_bar_create(row);
  s_fanBar = bar;
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 110, 10);
  lv_obj_set_pos(bar, 56, mid - 5);
  lv_bar_set_range(bar, 0, 3);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, c(BAR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, c(BAR_FROM), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar, c(BAR_TO), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_shadow_width(bar, 10, LV_PART_INDICATOR);
  lv_obj_set_style_shadow_color(bar, c(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_shadow_opa(bar, LV_OPA_50, LV_PART_INDICATOR);

  lv_obj_t *sc = make_plain(row);
  lv_obj_set_size(sc, 110, LV_SIZE_CONTENT);
  lv_obj_set_pos(sc, 56, mid + 10);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(sc, "Low", &ui_font_tiny, TXT3);
  make_label(sc, "Mid", &ui_font_tiny, TXT3);
  make_label(sc, "High", &ui_font_tiny, TXT3);
}

void build_control_card(lv_obj_t *root) {
  lv_obj_t *card = make_glass(root, LV_OPA_40, 20);
  lv_obj_set_pos(card, kCardX, kMidY);  // rows below are card-relative
  lv_obj_set_size(card, kCardW, kMidH);
  lv_obj_set_style_pad_hor(card, 16, 0);

  // card-relative y: title 18, mode 50 (h 48), fan 112 (h 104), timer 230 (h 76)
  lv_obj_t *title = make_label(card, "SMART AI VENTILATION", &ui_font_xs, 0x374151);
  lv_obj_set_pos(title, 2, 18);

  // One mode button showing the current mode (AI included).
  s_btnMode = make_mode_button(card, kCardW - 2 * 16, tr("환기모드", "Ventilation"), &s_btnModeText);
  lv_obj_set_height(s_btnMode, 48);
  lv_obj_set_style_text_font(s_btnModeText, &ui_font_sm, 0);
  lv_obj_set_pos(s_btnMode, 0, 50);
  // Second line (pressure modes only): "급기 320 · 배기 295 m³/h"
  s_btnModeDetail = make_label(s_btnMode, "", &ui_font_tiny, TXT2);
  lv_obj_align(s_btnModeDetail, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_flag(s_btnModeDetail, LV_OBJ_FLAG_HIDDEN);

  build_fan_row(card, 112, 104);

  lv_obj_t *resv = make_card_row(card, 230, 76);
  lv_obj_t *ic = make_icon(resv, &dash_c_ic_alarm, TXT2);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 13, 0);
  s_resvName = make_label(resv, tr("예약 없음", "No timer"), &ui_font_xs, TXT2);
  lv_obj_align(s_resvName, LV_ALIGN_LEFT_MID, 38, 0);
  s_resvTime = make_label(resv, "--:--", &ui_font_sm, TXT);
  lv_obj_align(s_resvTime, LV_ALIGN_RIGHT_MID, -12, 0);
}

// ---- bottom cards: same elements as the A/B bottom bar, pastel pills ----

using IconFn = lv_obj_t *(*)(lv_obj_t *, lv_coord_t, lv_color_t);

struct PillDef {
  IconFn icon;          // same icons as A/B (nullptr = none, as A/B TVOC)
  const char *label;
  const char *unit;
  uint32_t tint;
};

const PillDef kPills[6] = {
    {ui_icons::dust, "PM2.5:", "µg/m³", 0xC9D4EE},   // periwinkle
    {ui_icons::dust, "PM10:", "µg/m³", 0xB7E3D9},    // aqua
    {ui_icons::cloud, "CO₂:", "ppm", 0xC8E4C4},      // mint
    {nullptr, "TVOC:", "ppb", 0xF6DE9E},             // butter
    {ui_icons::gasring, "CO:", "ppm", 0xF8CDA9},     // peach
    {ui_icons::thermo, "Temp:", "", 0xDACBE8},       // lilac
};

void build_pills(lv_obj_t *root) {
  lv_obj_t *row = make_row(root, 8);
  lv_obj_set_pos(row, kPad, kPillY);
  lv_obj_set_width(row, 800 - 2 * kPad);
  for (int i = 0; i < 6; i++) {
    lv_obj_t *p = make_plain(row);
    lv_obj_set_height(p, kPillH);
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_style_bg_color(p, c(kPills[i].tint), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_90, 0);
    lv_obj_set_style_radius(p, 20, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, c(GLASS), 0);
    lv_obj_set_style_border_opa(p, LV_OPA_60, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 0, 0);

    lv_obj_t *top = make_row(p, 4);
    if (kPills[i].icon) kPills[i].icon(top, 14, c(0x4B5257));
    make_label(top, kPills[i].label, &ui_font_xs, 0x4B5257);

    lv_obj_t *vals = make_row(p, 3);
    lv_obj_set_flex_align(vals, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    if (i < 5) {
      s_pillVal[i] = make_label(vals, "--", &ui_font_md, 0x30363A);
      lv_obj_t *u = make_label(vals, kPills[i].unit, &ui_font_xs, 0x6B7176);
      lv_obj_set_style_pad_bottom(u, 3, 0);
    } else {
      // Temp card: "24.5°C  48%", as the A/B temp card
      s_pillVal[i] = make_label(vals, "--", &ui_font_sm, 0x30363A);
      s_humVal = make_label(vals, "--", &ui_font_sm, 0x30363A);
    }
  }
}

void set_pill(int idx, const char *text) {
  if (s_pillVal[idx]) lv_label_set_text(s_pillVal[idx], text);
}

} // namespace

lv_obj_t *create(lv_obj_t *parent) {
  lv_obj_t *root = make_plain(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(root, c(0xF2EDE4), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

  lv_obj_t *bg = lv_img_create(root);
  lv_img_set_src(bg, &dash_c_bg);
  lv_obj_set_pos(bg, 0, 0);

  build_top_bar(root);
  build_airflow(root);
  build_control_card(root);
  build_aqi(root);
  build_pills(root);
  return root;
}

// ---- top bar -------------------------------------------------------------

void set_wifi_connected(bool connected) {
  if (s_wifiIcon) lv_obj_set_style_opa(s_wifiIcon, connected ? LV_OPA_COVER : LV_OPA_40, 0);
  if (s_wifiText) {
    lv_label_set_text(s_wifiText, connected ? tr("연결됨", "Connected") : tr("미연결", "Not connected"));
    lv_obj_set_style_text_color(s_wifiText, c(connected ? TXT : TXT3), 0);
  }
}

void set_weather(const char *summary, float outdoor_c) {
  if (!s_weather) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %.0f°C", summary, outdoor_c);
  lv_label_set_text(s_weather, buf);
}

void set_time(int year, int month, int day, int hour24, int minute) {
  if (!s_clock) return;
  const char *ampm = (hour24 < 12) ? "AM" : "PM";
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d/%02d/%02d %s %02d:%02d", year, month, day, ampm, hour12, minute);
  lv_label_set_text(s_clock, buf);
}

void set_filter_status(const char *text, bool ok) {
  if (!s_filter) return;
  lv_label_set_text(s_filter, text);
  lv_obj_set_style_text_color(s_filter, c(ok ? OK_TXT : ALERT), 0);
}

// ---- body ----------------------------------------------------------------

void set_aqi(int aqi, int level) {
  if (level < 0 || level > 4) level = 0;
  const lv_color_t col = c(kLevelColor[level]);
  if (s_aqiNum) {
    char buf[12];
    if (aqi < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d", aqi);
    lv_label_set_text(s_aqiNum, buf);
    lv_obj_set_style_text_color(s_aqiNum, col, 0);
    lv_obj_set_style_text_color(s_aqiWord, col, 0);
    lv_label_set_text(s_aqiText, kLevelText[level]);
    lv_obj_set_style_text_color(s_aqiText, col, 0);
    place_center(s_aqiHead, kRingCx + 6, 94);
  }
  if (s_glow) lv_obj_set_style_img_recolor(s_glow, c(kGlowColor[level]), 0);
  if (s_ring) {
    int comfort = aqi < 0 ? 0 : 100 - (aqi * 100) / 500;
    if (comfort < 0) comfort = 0;
    if (aqi >= 0 && comfort < 3) comfort = 3; // keep a visible sliver at AQI 500
    lv_obj_set_style_arc_img_src(s_ring, kRingImg[level], LV_PART_INDICATOR);
    lv_arc_set_value(s_ring, comfort);
  }
}

void set_indoor(float temp_c, int humidity_pct) {
  char buf[12];
  if (s_temp) {
    if (temp_c <= -99.0f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f", temp_c);
    lv_label_set_text(s_temp, buf);
  }
  if (s_rh) {
    if (humidity_pct < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d", humidity_pct);
    lv_label_set_text(s_rh, buf);
  }
  recenter_readout();
  // Temp card (same text as the A/B temp card)
  if (s_pillVal[5]) {
    if (temp_c <= -99.0f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f°C", temp_c);
    lv_label_set_text(s_pillVal[5], buf);
  }
  if (s_humVal) {
    if (humidity_pct < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", humidity_pct);
    lv_label_set_text(s_humVal, buf);
  }
}

void set_mode(const char *mode_text, bool running, const char *detail) {
  if (!s_btnModeText) return;
  lv_label_set_text(s_btnModeText, mode_text);
  const bool two = detail && detail[0];
  if (two) {
    lv_label_set_text(s_btnModeDetail, detail);
    lv_obj_clear_flag(s_btnModeDetail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_btnModeText, LV_ALIGN_TOP_MID, 0, 4);
  } else {
    lv_obj_add_flag(s_btnModeDetail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(s_btnModeText);
  }
  style_mode_button(s_btnMode, running);
}

void set_fan(int level, bool err) {
  if (!s_fanBar) return;
  if (level < 0 || level > 3) level = 0;
  lv_bar_set_value(s_fanBar, err ? 0 : level, LV_ANIM_OFF);
  recolor(s_fanIcon, err ? ALERT : (level > 0 ? 0x2B2F33 : TXT3));
  static const char *const kKo[4] = {"정지", "약", "중", "강"};
  static const char *const kEn[4] = {"Off", "Low", "Mid", "High"};
  lv_label_set_text(s_fanLevel, err ? tr("오류", "Error") : tr(kKo[level], kEn[level]));
  lv_obj_set_style_text_color(s_fanLevel, c(err ? ALERT : TXT), 0);
}

void set_fan_vsp(int supply_units, int exhaust_units) {
  const int u[2] = {supply_units, exhaust_units};
  for (int i = 0; i < 2; i++) {
    if (!s_vsp[i]) continue;
    char buf[24];
    if (u[i] <= 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d.%02dV", (u[i] * 5) / 100, (u[i] * 5) % 100);
    lv_label_set_text(s_vsp[i], buf);
  }
}

void set_reservation(int type, int minutes) {
  if (!s_resvName || !s_resvTime) return;
  char buf[24];
  if (type == 1 || type == 2) {
    lv_label_set_text(s_resvName, type == 1 ? tr("가동예약", "Start timer") : tr("정지예약", "Stop timer"));
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    lv_label_set_text(s_resvTime, buf);
  } else {
    lv_label_set_text(s_resvName, tr("예약 없음", "No timer"));
    lv_label_set_text(s_resvTime, "--:--");
  }
}

void set_sensors(int pm25, int pm10, int co2, int tvoc_ppb, int co_ppm) {
  // values only - label and unit are fixed, as on the A/B bottom cards
  const int v[5] = {pm25, pm10, co2, tvoc_ppb, co_ppm};
  for (int i = 0; i < 5; i++) {
    char buf[12];
    if (v[i] < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d", v[i]);
    set_pill(i, buf);
  }
}

} // namespace ui_main_screen_v3
