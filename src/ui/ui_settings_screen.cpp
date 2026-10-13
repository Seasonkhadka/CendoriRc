/*
 * Settings screen - sidebar + right-hand panel.
 *
 * The left list only picks a section. OK moves focus into the right panel
 * so Up/Down/Left/Right can change that page. OK again saves; Back
 * cancels. See ui_settings_screen.h for the full button model.
 */
#include "ui_settings_screen.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "fonts/ui_fonts.h"
#include "io/ahu_protocol.h"

#include <stdio.h>

using namespace ui_theme;

namespace ui_settings_screen {

// Rebuilds whichever content panel is currently shown - defined further
// down (outer namespace, alongside create()/select_menu()/etc.), but the
// AHU send/confirm plumbing inside the anonymous namespace below needs
// to call it before that point, hence the forward declaration here (same
// outer scope as the real definition - a declaration inside the nested
// anonymous namespace would just create a second, unrelated symbol).
void refresh_content();

namespace {

constexpr int kMenuCount = 5;

struct MenuItem {
  const char *title;
  const char *subtitle;
  bool enabled; // false = greyed out / not yet available
  bool locked;  // true = show a lock icon (password-protected section)
};

const MenuItem kMenu[kMenuCount] = {
    {"풍량 설정", "(Airflow)", true, false},
    {"예약 설정", "(Schedule)", true, false},
    {"네트워크 설정", "(Wi-Fi)", true, false},
    {"기기 설정", "(Engineer Mode)", true, true},
    {"필터 상태", "(Filter)", true, false},
};

// Engineer Mode is a locked sub-area with its own nested sidebar (2 items)
// and its own content panel per item - selecting it in the outer sidebar
// shows a simple "portal" panel; pressing OK on that portal drills in.
constexpr int kEngMenuCount = 2;
struct EngMenuItem {
  const char *title;
  const char *subtitle;
};
const EngMenuItem kEngMenu[kEngMenuCount] = {
    {"ID 설정", "(RS-485 ID)"},
    {"VSP 설정", "(Motor VSP)"},
};

enum class Mode { Outer, Engineer };
Mode s_mode = Mode::Outer;

lv_obj_t *s_breadcrumbRoot = nullptr;
lv_obj_t *s_breadcrumbSection = nullptr;
lv_obj_t *s_breadcrumbLock = nullptr;
lv_obj_t *s_breadcrumbWifi = nullptr;

lv_obj_t *s_sidebarSlot = nullptr; // rebuilt on Outer <-> Engineer switch
lv_obj_t *s_menuCards[kMenuCount] = {};
lv_obj_t *s_menuTitles[kMenuCount] = {};
lv_obj_t *s_menuSubtitles[kMenuCount] = {};
lv_obj_t *s_engMenuCards[kEngMenuCount] = {};
lv_obj_t *s_engMenuTitles[kEngMenuCount] = {};
lv_obj_t *s_engMenuSubtitles[kEngMenuCount] = {};

lv_obj_t *s_contentPanel = nullptr;
int s_selected = 0;    // outer sidebar index (0-4)
int s_engSelected = 0; // engineer sub-sidebar index (0-1), while s_mode == Engineer

// Right-panel edit session. While s_in_panel is true, arrows change the
// working copy (s_data). s_snap is the last saved copy; Back restores it.
bool s_in_panel = false;
int s_panel_row = 0;

struct SettingsData {
  // AHU live control (RS-485, ahu_protocol.h) - these mirror the last
  // CONFIRMED controller response, not a pending edit: fire_*() below
  // only writes them after a successful send, never optimistically.
  int ahu_mode = 0;        // 0 Off / 1 Ventilation / 2 Bypass / 3 Auto
  int ahu_volume = 1;      // 0 off / 1 low / 2 mid / 3 high
  bool ahu_clean = false;
  int ahu_start_hour = 22; // 0..23 - local picker only, sent on "start"
  int ahu_stop_hour = 6;   // 0..23 - local picker only, sent on "stop"
  int eng_id = 1;        // 0..15
  int vsp_supply = 1;    // 0 강 / 1 중 / 2 약
  int vsp_exhaust = 1;
  int filter_pct = 78;
  int filter_hours = 1320;
  bool wifi_connected = true;
  int wifi_action = 0;   // 0 none / 1 pair / 2 scan / 3 disconnect
};

SettingsData s_data;
SettingsData s_snap;

int wrap(int v, int n) { return (v % n + n) % n; }

void apply_focus_frame(lv_obj_t *obj, bool focused) {
  if (!obj) return;
  if (focused && s_in_panel) {
    lv_obj_set_style_outline_width(obj, 2, 0);
    lv_obj_set_style_outline_color(obj, c(ACCENT_CYAN), 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_pad(obj, 3, 0);
  } else {
    lv_obj_set_style_outline_width(obj, 0, 0);
  }
}

void snapshot_data() { s_snap = s_data; }
void restore_data() { s_data = s_snap; }

// ---- AHU (RS-485 ventilation controller) live send/confirm plumbing ---
//
// Two distinct things, kept deliberately separate:
//   s_data.ahu_mode/ahu_volume/ahu_clean - the LOCAL PICK. Up/Down
//     selects a row, Left/Right edits this freely with nothing sent.
//     Nothing here ever touches hardware by itself.
//   s_confirmed* - what the real controller has actually acknowledged.
//     Only ever updated inside fire_*() after a response that passed
//     parity/header checks. This is what the confirmed-state badge
//     shows, never the local pick above.
// Mode/volume share one shadow byte in ahu_protocol, so one flag covers
// both; Clean is a separate command header with no shared state, so it
// gets its own flag. Both start false at boot - until the first real
// confirmation, the badge says so plainly instead of guessing.
int s_confirmedMode = 0;
int s_confirmedVolume = 0;
bool s_confirmedClean = false;
bool s_ahuModeEverConfirmed = false;
bool s_ahuCleanEverConfirmed = false;
bool s_ahuSending = false;
char s_ahuResultBuf[48] = "";
const char *s_ahuResultMsg = nullptr;

// Mirrors ahu_protocol.cpp's internal kMinCommandGapMs (not exposed from
// there, and that file is settled/untouched here) so a press inside the
// cooldown window can show "wait" instead of "failed - no response" -
// those are different situations and the distinction was asked for.
constexpr uint32_t kUiAhuGapMs = 1500;
uint32_t s_lastAhuAttemptMs = 0;

bool ahu_attempt_allowed() {
  uint32_t now = millis();
  return s_lastAhuAttemptMs == 0 || now - s_lastAhuAttemptMs >= kUiAhuGapMs;
}

void show_ahu_wait() {
  snprintf(s_ahuResultBuf, sizeof(s_ahuResultBuf), "%s", "대기 - 너무 빠름 (Wait)");
  s_ahuResultMsg = s_ahuResultBuf;
  refresh_content();
}

// Forces one real render+flush before the blocking RS-485 call, the same
// way main.cpp's boot sequence pushes a real frame before turning the
// backlight on - without this, "sending..." would never actually reach
// the panel: this whole screen runs on one cooperative loop, so nothing
// between setting a label and the blocking call below gets flushed.
void begin_ahu_send() {
  s_ahuSending = true;
  s_ahuResultMsg = nullptr;
  refresh_content();
  lv_timer_handler();
}

void end_ahu_send(bool ok) {
  s_ahuSending = false;
  snprintf(s_ahuResultBuf, sizeof(s_ahuResultBuf), "%s", ok ? "전송 완료 (Sent)" : "실패 - 응답 없음 (Failed)");
  s_ahuResultMsg = s_ahuResultBuf;
  refresh_content();
}

// Display-only decode of the last D5/B5 mode byte. Bypass is shown first
// if both bypass and auto bits are set.
int decode_mode_from_shadow() {
  uint8_t m = ahu_protocol::shadowMode();
  if (m & 0x80) return 2; // Bypass
  if (m & 0x08) return 3; // Auto
  if (m & 0x10) return 1; // Ventilation
  return 0;               // Off
}

// Sends whatever is CURRENTLY in s_data.ahu_mode (the local pick) - the
// caller (handle_ok(), only while row 0 is focused) decides when that
// happens, not this function.
void fire_mode() {
  if (!ahu_attempt_allowed()) { show_ahu_wait(); return; }
  s_lastAhuAttemptMs = millis();
  begin_ahu_send();
  bool ok;
  switch (s_data.ahu_mode) {
    case 0: ok = ahu_protocol::power_off(); break;
    case 1: ok = ahu_protocol::set_ventilation(); break;
    case 2: ok = ahu_protocol::set_bypass(); break;
    default: ok = ahu_protocol::set_auto(); break;
  }
  if (ok) {
    s_ahuModeEverConfirmed = true;
    s_confirmedMode = decode_mode_from_shadow();
    s_confirmedVolume = ahu_protocol::shadowMode() & 0x03;
    s_snap.ahu_mode = s_data.ahu_mode; // lock in - a later Back must not undo a row already sent
  }
  end_ahu_send(ok);
}

void fire_volume() {
  if (!ahu_attempt_allowed()) { show_ahu_wait(); return; }
  s_lastAhuAttemptMs = millis();
  begin_ahu_send();
  bool ok = ahu_protocol::set_volume((uint8_t)s_data.ahu_volume);
  if (ok) {
    s_ahuModeEverConfirmed = true;
    s_confirmedVolume = ahu_protocol::shadowMode() & 0x03;
    s_confirmedMode = decode_mode_from_shadow();
    s_snap.ahu_volume = s_data.ahu_volume;
  }
  end_ahu_send(ok);
}

void fire_clean() {
  if (!ahu_attempt_allowed()) { show_ahu_wait(); return; }
  s_lastAhuAttemptMs = millis();
  begin_ahu_send();
  bool ok = ahu_protocol::set_clean(s_data.ahu_clean);
  if (ok) {
    s_ahuCleanEverConfirmed = true;
    s_confirmedClean = s_data.ahu_clean;
    s_snap.ahu_clean = s_data.ahu_clean;
  }
  end_ahu_send(ok);
}

// One-shot actions (Schedule panel rows 2/3) - exit the panel once the
// attempt resolves, success or failure, same as Filter's existing reset
// button.
void fire_start_reservation() {
  if (!ahu_attempt_allowed()) { show_ahu_wait(); return; }
  s_lastAhuAttemptMs = millis();
  begin_ahu_send();
  bool ok = ahu_protocol::start_reservation((uint8_t)s_data.ahu_start_hour, (uint8_t)s_data.ahu_stop_hour);
  if (ok) s_ahuModeEverConfirmed = true;
  end_ahu_send(ok);
  s_in_panel = false;
  s_panel_row = 0;
  refresh_content();
}

void fire_stop_reservation() {
  if (!ahu_attempt_allowed()) { show_ahu_wait(); return; }
  s_lastAhuAttemptMs = millis();
  begin_ahu_send();
  bool ok = ahu_protocol::stop_reservation((uint8_t)s_data.ahu_stop_hour);
  if (ok) s_ahuModeEverConfirmed = true;
  end_ahu_send(ok);
  s_in_panel = false;
  s_panel_row = 0;
  refresh_content();
}

// Persistent footer bar. Labels are rebuilt by set_footer() whenever
// the user enters/leaves the right panel or changes section.
lv_obj_t *s_footerBar = nullptr;
void set_footer();

// ---- small style helpers (mirrors ui_main_screen's, kept local since
// the two screens' component sets differ enough not to share yet) -----

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
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, c(BORDER), 0);
  return card;
}

// A segmented-control option that always shows a radio dot (filled when
// selected, hollow when not) - used for the airflow-mode row.
lv_obj_t *make_mode_option(lv_obj_t *parent, const char *main_text, const char *sub_text, bool selected) {
  lv_obj_t *pill = make_plain(parent);
  lv_obj_set_size(pill, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_style_radius(pill, 12, 0);
  lv_obj_set_style_bg_opa(pill, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(pill, c(PILL_BG), 0);
  if (selected) {
    lv_obj_set_style_shadow_width(pill, 14, 0);
    lv_obj_set_style_shadow_color(pill, c(PILL_BG), 0);
    lv_obj_set_style_shadow_opa(pill, LV_OPA_40, 0);
  }
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *top = make_row(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 5);
  lv_obj_t *dot = lv_obj_create(top);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 9, 9);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  if (selected) {
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, c(PILL_TEXT), 0);
  } else {
    lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, c(TEXT_SECONDARY), 0);
  }
  make_label(top, main_text, &ui_font_sm, selected ? PILL_TEXT : TEXT_PRIMARY);

  make_label(pill, sub_text, &ui_font_xs, selected ? PILL_TEXT_SUB : TEXT_DIM);
  return pill;
}

// A single-line two-way toggle option (schedule screen's mode switches).
// Always shows a bordered pill; selected fills it white. The unselected
// dot can be a hollow ring or a plain filled bullet, per hollow_dot -
// the mockup uses hollow for a real on/off choice and filled for a plain
// category tab, so both are supported rather than picking one.
lv_obj_t *make_toggle_pill(lv_obj_t *parent, const char *text, bool selected, bool hollow_dot) {
  lv_obj_t *pill = make_card(parent, CARD_BG);
  lv_obj_set_size(pill, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pill, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(pill, c(PILL_BG), 0);
  lv_obj_set_style_border_color(pill, c(selected ? PILL_BG : BORDER), 0);
  if (selected) {
    lv_obj_set_style_shadow_width(pill, 12, 0);
    lv_obj_set_style_shadow_color(pill, c(PILL_BG), 0);
    lv_obj_set_style_shadow_opa(pill, LV_OPA_40, 0);
  }
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 6, 0);

  lv_obj_t *dot = lv_obj_create(pill);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  bool showHollow = hollow_dot && !selected;
  lv_obj_set_style_bg_opa(dot, showHollow ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  if (showHollow) {
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, c(TEXT_SECONDARY), 0);
  } else {
    lv_obj_set_style_bg_color(dot, c(selected ? PILL_TEXT : TEXT_PRIMARY), 0);
  }

  make_label(pill, text, &ui_font_sm, selected ? PILL_TEXT : TEXT_PRIMARY);
  return pill;
}

// A plain fan-level option inside a shared row card - selected shows a
// glowing pill with a bullet, unselected is just dim/plain text.
lv_obj_t *make_level_option(lv_obj_t *parent, const char *main_text, const char *sub_text, bool selected) {
  lv_obj_t *slot = make_plain(parent);
  lv_obj_set_size(slot, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(slot, 1);
  lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *pill = make_plain(slot);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_PCT(82));
  lv_obj_set_style_pad_hor(pill, 14, 0);
  lv_obj_set_style_radius(pill, 12, 0);
  lv_obj_set_style_bg_opa(pill, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(pill, c(PILL_BG), 0);
  if (selected) {
    lv_obj_set_style_shadow_width(pill, 14, 0);
    lv_obj_set_style_shadow_color(pill, c(PILL_BG), 0);
    lv_obj_set_style_shadow_opa(pill, LV_OPA_40, 0);
  }
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  if (selected) {
    lv_obj_t *top = make_row(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 4);
    lv_obj_t *dot = lv_obj_create(top);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, c(PILL_TEXT), 0);
    make_label(top, main_text, &ui_font_sm, PILL_TEXT);
  } else {
    make_label(pill, main_text, &ui_font_sm, TEXT_PRIMARY);
  }
  make_label(pill, sub_text, &ui_font_xs, selected ? PILL_TEXT_SUB : TEXT_DIM);
  return slot;
}

// Small status strip shared by the Airflow and Schedule panels: shows
// "sending...", the last send result, or idle - never silent about which
// of the three it is.
void build_ahu_status_strip(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(card, LV_PCT(100), 36);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const char *text = s_ahuSending ? "전송 중... (Sending)" : (s_ahuResultMsg ? s_ahuResultMsg : "RS-485 대기 (Idle)");
  make_label(card, text, &ui_font_xs, s_ahuSending ? ACCENT_CYAN : TEXT_DIM);
}

// Shows what the controller has actually acknowledged - separate from
// the editable pick below, which never flips this on its own.
void build_ahu_confirmed_badge(lv_obj_t *parent) {
  static const char *modeShort[4] = {"Off", "Ventilation", "Bypass", "Auto"};
  char buf[96];
  if (s_ahuModeEverConfirmed && s_ahuCleanEverConfirmed) {
    snprintf(buf, sizeof(buf), "확인됨 (Confirmed): %s / Vol %d / Clean %s", modeShort[s_confirmedMode],
             s_confirmedVolume, s_confirmedClean ? "ON" : "OFF");
  } else if (s_ahuModeEverConfirmed) {
    snprintf(buf, sizeof(buf), "확인됨 (Confirmed): %s / Vol %d / Clean 미확인", modeShort[s_confirmedMode],
             s_confirmedVolume);
  } else {
    snprintf(buf, sizeof(buf), "확인됨 (Confirmed): 아직 없음 (none yet - send a command first)");
  }
  lv_obj_t *l = make_label(parent, buf, &ui_font_xs, TEXT_DIM);
  lv_obj_set_flex_grow(l, 0);
}

// ---- Airflow section -> repurposed as live AHU control -----------------
// Up/Down selects a row; Left/Right only edits the LOCAL pick
// (s_data.ahu_mode/ahu_volume/ahu_clean) - nothing is sent yet. OK
// (handle_ok()) sends the focused row's command and stays in the panel;
// Back discards whatever was picked but never sent (see handle_back()).

void build_airflow_panel(lv_obj_t *parent) {
  build_ahu_confirmed_badge(parent);

  static const char *modeMain[4] = {"정지", "환기", "바이패스", "자동"};
  static const char *modeSub[4] = {"(Off)", "(Ventilation)", "(Bypass)", "(Auto)"};

  lv_obj_t *mode_row = make_row(parent, LV_PCT(100), 72, 8);
  lv_obj_set_flex_grow(mode_row, 0);
  apply_focus_frame(mode_row, s_panel_row == 0);
  for (int i = 0; i < 4; i++) {
    make_mode_option(mode_row, modeMain[i], modeSub[i], i == s_data.ahu_mode);
  }

  static const char *volMain[4] = {"꺼짐", "약", "중", "강"};
  static const char *volSub[4] = {"(Off)", "(Low)", "(Mid)", "(High)"};
  lv_obj_t *vol_row = make_card(parent, CARD_BG);
  lv_obj_set_size(vol_row, LV_PCT(100), 72);
  lv_obj_set_flex_grow(vol_row, 0);
  lv_obj_set_style_pad_hor(vol_row, 16, 0);
  lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(vol_row, s_panel_row == 1);
  lv_obj_t *vol_label = make_plain(vol_row);
  lv_obj_set_size(vol_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(vol_label, LV_FLEX_FLOW_COLUMN);
  make_label(vol_label, "풍량", &ui_font_sm, TEXT_PRIMARY);
  make_label(vol_label, "(Fan Volume)", &ui_font_xs, TEXT_SECONDARY);
  lv_obj_t *vol_options = make_row(vol_row, LV_PCT(100), LV_PCT(100), 4);
  lv_obj_set_flex_grow(vol_options, 1);
  for (int i = 0; i < 4; i++) {
    make_level_option(vol_options, volMain[i], volSub[i], i == s_data.ahu_volume);
  }

  lv_obj_t *clean_row = make_row(parent, LV_PCT(100), 56, 8);
  lv_obj_set_flex_grow(clean_row, 0);
  apply_focus_frame(clean_row, s_panel_row == 2);
  bool cleanOn = s_data.ahu_clean;
  make_toggle_pill(clean_row, "청정 꺼짐 (Off)", !cleanOn, true);
  make_toggle_pill(clean_row, "청정 가동 (Clean)", cleanOn, true);

  build_ahu_status_strip(parent);
}

// ---- Schedule section -> repurposed as AHU absolute-hour reservation --
// Rows 0/1 are plain local pickers (Left/Right adjusts, nothing sent).
// Rows 2/3 are one-shot actions: OK fires start_reservation()/
// stop_reservation() respectively (handle_ok() branches on s_panel_row
// for this section) and then exits the panel, success or failure.

void build_hour_stepper(lv_obj_t *parent, const char *label_main, const char *label_sub, int hour, bool focused) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), 72);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_hor(card, 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(card, focused);

  lv_obj_t *label = make_plain(card);
  lv_obj_set_flex_flow(label, LV_FLEX_FLOW_COLUMN);
  make_label(label, label_main, &ui_font_sm, TEXT_PRIMARY);
  make_label(label, label_sub, &ui_font_xs, TEXT_SECONDARY);

  lv_obj_t *value_row = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 10);
  make_label(value_row, LV_SYMBOL_LEFT, &lv_font_montserrat_14, TEXT_DIM);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:00", hour);
  make_label(value_row, buf, &ui_font_xxl, TEXT_PRIMARY);
  make_label(value_row, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, TEXT_DIM);
}

void build_reservation_action(lv_obj_t *parent, const char *text, bool focused) {
  lv_obj_t *btn = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(btn, LV_PCT(100), 52);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_grow(btn, 0);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(btn, focused);
  make_label(btn, text, &ui_font_sm, TEXT_PRIMARY);
}

void build_schedule_panel(lv_obj_t *parent) {
  build_hour_stepper(parent, "예약 시작", "(Start Hour)", s_data.ahu_start_hour, s_panel_row == 0);
  build_hour_stepper(parent, "예약 종료", "(Stop Hour)", s_data.ahu_stop_hour, s_panel_row == 1);
  build_reservation_action(parent, "예약 시작 전송 (Send Start Reservation)", s_panel_row == 2);
  build_reservation_action(parent, "예약 해제 전송 (Send Stop Reservation)", s_panel_row == 3);
  build_ahu_status_strip(parent);
}

// ---- Filter status section (per the reference mockup) -----------------

// Small outlined status chip (e.g. "정상"/Normal) - distinct from the
// solid glowing PILL used for selected options elsewhere.
lv_obj_t *make_status_chip(lv_obj_t *parent, const char *text, uint32_t color) {
  lv_obj_t *chip = make_plain(parent);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, c(color), 0);
  lv_obj_set_style_pad_hor(chip, 10, 0);
  lv_obj_set_style_pad_ver(chip, 2, 0);
  make_label(chip, text, &ui_font_xs, color);
  return chip;
}

void build_filter_panel(lv_obj_t *parent) {
  lv_obj_t *top_row = make_row(parent, LV_PCT(100), 190, 20);
  lv_obj_set_flex_grow(top_row, 0);

  // Full-circle percentage ring (no gap, unlike the gauges elsewhere).
  lv_obj_t *gauge = make_plain(top_row);
  lv_obj_set_size(gauge, 180, 180);

  lv_obj_t *ring = lv_arc_create(gauge);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, 180, 180);
  lv_obj_center(ring);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 100);
  lv_arc_set_value(ring, s_data.filter_pct);
  lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(ring, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, c(TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, c(ACCENT_CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d%%", s_data.filter_pct);
  lv_obj_t *pct = make_label(gauge, pct_buf, &ui_font_xxl, TEXT_PRIMARY);
  lv_obj_align(pct, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t *chip = make_status_chip(gauge, "정상", ACCENT_CYAN);
  lv_obj_align(chip, LV_ALIGN_CENTER, 0, 24);

  // Usage stats, to the right of the ring.
  lv_obj_t *info = make_plain(top_row);
  lv_obj_set_size(info, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(info, 1);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(info, 16, 0);

  lv_obj_t *usage = make_plain(info);
  lv_obj_set_flex_flow(usage, LV_FLEX_FLOW_COLUMN);
  make_label(usage, "누적 사용:", &ui_font_sm, TEXT_SECONDARY);
  char hours_buf[20];
  snprintf(hours_buf, sizeof(hours_buf), "%d 시간", s_data.filter_hours);
  make_label(usage, hours_buf, &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *replace = make_plain(info);
  lv_obj_set_flex_flow(replace, LV_FLEX_FLOW_COLUMN);
  make_label(replace, "교체 권장일:", &ui_font_sm, TEXT_SECONDARY);
  lv_obj_t *dateRow = make_row(replace, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(dateRow, "2027.03.15", &ui_font_md, TEXT_PRIMARY);
  make_label(dateRow, "(권장)", &ui_font_xs, TEXT_DIM);

  lv_obj_t *note = make_label(parent, "* 실내외 공기질 및 사용 환경에 따라\n차이가 있을 수 있습니다.", &ui_font_xs, TEXT_DIM);
  lv_obj_set_size(note, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(note, 0);

  lv_obj_t *reset_btn = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(reset_btn, LV_PCT(100), 52);
  lv_obj_set_style_radius(reset_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_align(reset_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(reset_btn, true);
  lv_obj_t *reset_row = make_row(reset_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 8);
  ui_icons::refresh(reset_row, 20, c(TEXT_PRIMARY));
  make_label(reset_row, "필터 초기화 (RESET)", &ui_font_sm, TEXT_PRIMARY);
}

// ---- Wi-Fi section (per the reference mockup) --------------------------

lv_obj_t *make_action_button(lv_obj_t *parent, lv_obj_t *(*icon_fn)(lv_obj_t *, lv_coord_t, lv_color_t),
                              const char *main_text, const char *sub_text) {
  lv_obj_t *btn = make_card(parent, CARD_BG);
  lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(btn, 1);
  lv_obj_set_style_pad_hor(btn, 14, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn, 10, 0);

  icon_fn(btn, 26, c(TEXT_PRIMARY));

  lv_obj_t *text = make_plain(btn);
  lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
  make_label(text, main_text, &ui_font_sm, TEXT_PRIMARY);
  make_label(text, sub_text, &ui_font_xs, TEXT_SECONDARY);
  return btn;
}

void build_wifi_panel(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), 150);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 8, 0);

  lv_obj_t *ssid_row = make_row(card, LV_PCT(100), LV_SIZE_CONTENT, 8);
  lv_obj_set_flex_align(ssid_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(ssid_row, s_data.wifi_connected ? "CENDORI_AI_Air_2.4G" : "네트워크 없음", &ui_font_md, TEXT_PRIMARY);
  ui_icons::wifi_bars(ssid_row, 22, c(s_data.wifi_connected ? ACCENT_CYAN : TEXT_DIM));

  if (s_data.wifi_connected) {
    make_status_chip(card, "정상 연결됨 (Connected)", ACCENT_CYAN);
  } else {
    make_status_chip(card, "연결 끊김 (Disconnected)", TEXT_DIM);
  }

  lv_obj_t *ip_row = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(ip_row, "IP:", &ui_font_sm, TEXT_SECONDARY);
  make_label(ip_row, s_data.wifi_connected ? "192.168.1.105" : "-", &ui_font_sm, TEXT_PRIMARY);

  lv_obj_t *mac_row = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(mac_row, "MAC:", &ui_font_sm, TEXT_SECONDARY);
  make_label(mac_row, "AC:67:B2:44:8A:1C", &ui_font_sm, TEXT_PRIMARY);

  lv_obj_t *actions_row = make_row(parent, LV_PCT(100), 80, 8);
  lv_obj_set_flex_grow(actions_row, 0);
  lv_obj_t *pair_btn = make_action_button(actions_row, ui_icons::phone, "스마트폰 간편 연결", "(App / QR Pairing)");
  lv_obj_t *scan_btn = make_action_button(actions_row, ui_icons::search, "주변 Wi-Fi 검색", "(Scan AP List)");
  apply_focus_frame(pair_btn, s_panel_row == 0);
  apply_focus_frame(scan_btn, s_panel_row == 1);

  lv_obj_t *disconnect_btn = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(disconnect_btn, LV_PCT(100), 48);
  lv_obj_set_style_radius(disconnect_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_align(disconnect_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(disconnect_btn, s_panel_row == 2);
  lv_obj_t *dc_row = make_row(disconnect_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 8);
  ui_icons::disconnect(dc_row, 18, c(TEXT_PRIMARY));
  make_label(dc_row, "네트워크 초기화 (Disconnect)", &ui_font_sm, TEXT_PRIMARY);
}

// ---- Placeholder sections (not yet designed) --------------------------

void build_placeholder_panel(lv_obj_t *parent, const char *label) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(card, label, &ui_font_sm, TEXT_DIM);
}

// ---- Engineer Mode (nested sub-area) -----------------------------------

// Shown in the OUTER content panel when "기기 설정" is highlighted but not
// yet entered - OK drills into its own nested sidebar (see enter_engineer).
void build_engineer_portal_panel(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 10, 0);

  ui_icons::lock(card, 28, c(TEXT_SECONDARY));
  make_label(card, "기기 설정 (Engineer Mode)", &ui_font_sm, TEXT_PRIMARY);
  make_label(card, "OK 버튼으로 진입 (Press OK to enter)", &ui_font_xs, TEXT_DIM);
}

// "ID 설정 (RS-485 ID)" - fully designed, per the reference mockup.
void build_engineer_id_panel(lv_obj_t *parent) {
  lv_obj_t *current = make_card(parent, CARD_BG);
  lv_obj_set_size(current, LV_PCT(100), 110);
  lv_obj_set_flex_grow(current, 0);
  lv_obj_set_flex_flow(current, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(current, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(current, 4, 0);
  char current_buf[24];
  snprintf(current_buf, sizeof(current_buf), "현재 ID : %02d", s_snap.eng_id);
  make_label(current, current_buf, &ui_font_xxl, TEXT_PRIMARY);
  make_label(current, "(Current ID)", &ui_font_sm, TEXT_SECONDARY);

  lv_obj_t *stepper = make_card(parent, CARD_BG);
  lv_obj_set_size(stepper, LV_PCT(100), 110);
  lv_obj_set_flex_grow(stepper, 1);
  lv_obj_set_flex_flow(stepper, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stepper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(stepper, 6, 0);
  apply_focus_frame(stepper, true);

  lv_obj_t *row = make_row(stepper, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 16);
  lv_obj_t *minus = make_card(row, CARD_BG_ALT);
  lv_obj_set_size(minus, 40, 40);
  lv_obj_set_flex_align(minus, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(minus, "-", &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *idRow = make_row(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(idRow, "ID", &ui_font_sm, TEXT_SECONDARY);
  char id_buf[12];
  snprintf(id_buf, sizeof(id_buf), "[ %02d ]", s_data.eng_id);
  make_label(idRow, id_buf, &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *plus = make_card(row, CARD_BG_ALT);
  lv_obj_set_size(plus, 40, 40);
  lv_obj_set_flex_align(plus, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(plus, "+", &ui_font_md, TEXT_PRIMARY);

  make_label(stepper, "(설정 범위: 00 ~ 15 / Hex 0x00 ~ 0x0F)", &ui_font_xs, TEXT_DIM);

  make_label(parent, "* 단지 홈네트워크 RS-485 통신 국번 설정 (중복 주의)", &ui_font_xs, TEXT_DIM);
}

// "VSP 설정 (Motor VSP)" - fully designed, per the reference mockup.

// One "[ 강 : 4.8 V ]"-style option: selected gets a cyan glow border and
// a leading dot, unselected is a plain outlined box.
lv_obj_t *make_voltage_option(lv_obj_t *parent, const char *text, bool selected) {
  lv_obj_t *box = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(box, LV_PCT(100), 44);
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_border_color(box, c(selected ? ACCENT_CYAN : BORDER), 0);
  lv_obj_set_style_border_width(box, selected ? 2 : 1, 0);
  if (selected) {
    lv_obj_set_style_shadow_width(box, 12, 0);
    lv_obj_set_style_shadow_color(box, c(ACCENT_CYAN), 0);
    lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);
  }
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *row = make_row(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 4);
  if (selected) {
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, c(ACCENT_CYAN), 0);
  }
  make_label(row, text, &ui_font_sm, selected ? ACCENT_CYAN : TEXT_PRIMARY);
  return box;
}

// A fan's "강/중/약" voltage row inside its own labeled card. selected_idx
// is 0=High/1=Mid/2=Low, or -1 for none selected (Exhaust in the mockup).
// show_step_hint adds the "▲/▼" hint under the row (Supply only).
void build_fan_voltage_card(lv_obj_t *parent, const char *header, int selected_idx, const char *high_text,
                             const char *mid_text, const char *low_text, bool show_step_hint, bool focused) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);
  apply_focus_frame(card, focused);

  make_label(card, header, &ui_font_sm, TEXT_SECONDARY);

  lv_obj_t *row = make_row(card, LV_PCT(100), 44, 8);
  make_voltage_option(row, high_text, selected_idx == 0);
  make_voltage_option(row, mid_text, selected_idx == 1);
  make_voltage_option(row, low_text, selected_idx == 2);

  if (show_step_hint) {
    lv_obj_t *hint = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 3);
    make_label(hint, LV_SYMBOL_UP, &lv_font_montserrat_14, ACCENT_CYAN);
    make_label(hint, "/", &ui_font_xs, ACCENT_CYAN);
    make_label(hint, LV_SYMBOL_DOWN, &lv_font_montserrat_14, ACCENT_CYAN);
  }
}

void build_engineer_vsp_panel(lv_obj_t *parent) {
  make_label(parent, "급기 - Supply Air", &ui_font_md, TEXT_PRIMARY);
  build_fan_voltage_card(parent, "급기 팬 (Supply Fan)", s_data.vsp_supply, "[ 강 : 4.8 V ]", "[ 중 : 3.2 V ]",
                        "[ 약 : 1.8 V ]", true, s_panel_row == 0);

  make_label(parent, "배기 - Exhaust Air", &ui_font_md, TEXT_PRIMARY);
  build_fan_voltage_card(parent, "배기 팬 (Exhaust Fan)", s_data.vsp_exhaust, "[ 강 : 4.6 V ]", "[ 중 : 3.0 V ]",
                        "[ 약 : 1.6 V ]", false, s_panel_row == 1);
}

void restyle_eng_menu_item(int idx) {
  bool selected = (idx == s_engSelected);
  lv_obj_set_style_border_color(s_engMenuCards[idx], c(selected ? ACCENT_CYAN : BORDER), 0);
  lv_obj_set_style_border_width(s_engMenuCards[idx], selected ? 2 : 1, 0);
  lv_obj_set_style_text_color(s_engMenuTitles[idx], c(selected ? ACCENT_CYAN : TEXT_PRIMARY), 0);
  lv_obj_set_style_text_color(s_engMenuSubtitles[idx], c(selected ? ACCENT_CYAN : TEXT_SECONDARY), 0);
}

lv_obj_t *build_eng_menu_item(lv_obj_t *parent, int idx) {
  const EngMenuItem &item = kEngMenu[idx];

  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  s_engMenuCards[idx] = card;

  s_engMenuTitles[idx] = make_label(card, item.title, &ui_font_sm, TEXT_PRIMARY);
  s_engMenuSubtitles[idx] = make_label(card, item.subtitle, &ui_font_xs, TEXT_SECONDARY);

  restyle_eng_menu_item(idx);
  return card;
}

void build_engineer_content_for(int idx) {
  lv_obj_clean(s_contentPanel);
  switch (idx) {
    case 0:
      build_engineer_id_panel(s_contentPanel);
      set_footer();
      break;
    case 1:
      build_engineer_vsp_panel(s_contentPanel);
      set_footer();
      break;
    default:
      break;
  }
}

void build_content_for(int idx) {
  lv_obj_clean(s_contentPanel);
  switch (idx) {
    case 0:
      build_airflow_panel(s_contentPanel);
      set_footer();
      break;
    case 1:
      build_schedule_panel(s_contentPanel);
      set_footer();
      break;
    case 2:
      build_wifi_panel(s_contentPanel);
      set_footer();
      break;
    case 3:
      build_engineer_portal_panel(s_contentPanel);
      set_footer();
      break;
    case 4:
      build_filter_panel(s_contentPanel);
      set_footer();
      break;
    default:
      break;
  }
}

void restyle_menu_item(int idx) {
  bool selected = (idx == s_selected);
  const MenuItem &item = kMenu[idx];

  uint32_t titleColor = selected ? ACCENT_CYAN : (item.enabled ? TEXT_PRIMARY : TEXT_DIM);
  uint32_t subColor = selected ? ACCENT_CYAN : (item.enabled ? TEXT_SECONDARY : TEXT_DIM);

  lv_obj_set_style_border_color(s_menuCards[idx], c(selected ? ACCENT_CYAN : BORDER), 0);
  lv_obj_set_style_border_width(s_menuCards[idx], selected ? 2 : 1, 0);
  lv_obj_set_style_bg_color(s_menuCards[idx], c(selected ? CARD_BG : CARD_BG), 0);

  lv_obj_set_style_text_color(s_menuTitles[idx], c(titleColor), 0);
  lv_obj_set_style_text_color(s_menuSubtitles[idx], c(subColor), 0);
}

lv_obj_t *build_menu_item(lv_obj_t *parent, int idx) {
  const MenuItem &item = kMenu[idx];

  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  s_menuCards[idx] = card;

  lv_obj_t *titleRow = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  s_menuTitles[idx] = make_label(titleRow, item.title, &ui_font_sm, TEXT_PRIMARY);
  if (item.locked) ui_icons::lock(titleRow, 16, c(TEXT_SECONDARY));

  s_menuSubtitles[idx] = make_label(card, item.subtitle, &ui_font_xs, TEXT_SECONDARY);

  restyle_menu_item(idx);
  return card;
}

void build_sidebar_outer(lv_obj_t *parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 10, 0);
  for (int i = 0; i < kMenuCount; i++) build_menu_item(parent, i);
}

void build_sidebar_engineer(lv_obj_t *parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 10, 0);
  for (int i = 0; i < kEngMenuCount; i++) build_eng_menu_item(parent, i);
}

// Reflects the current mode/selection into the breadcrumb bar: which
// root label shows ("설정" vs "기기설정"), the trailing section name, and
// the lock/Wi-Fi icons (Engineer Mode shows a lock, not the Wi-Fi glyph).
void update_breadcrumb() {
  bool eng = (s_mode == Mode::Engineer);
  if (s_breadcrumbRoot) lv_label_set_text(s_breadcrumbRoot, eng ? "기기설정 (ENGINEER MODE)" : "설정 (SETTINGS)");
  if (s_breadcrumbSection) {
    lv_label_set_text(s_breadcrumbSection, eng ? kEngMenu[s_engSelected].title : kMenu[s_selected].title);
  }
  if (s_breadcrumbLock) {
    if (eng) lv_obj_clear_flag(s_breadcrumbLock, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_breadcrumbLock, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_breadcrumbWifi) {
    if (eng) lv_obj_add_flag(s_breadcrumbWifi, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s_breadcrumbWifi, LV_OBJ_FLAG_HIDDEN);
  }
}

void build_breadcrumb(lv_obj_t *parent) {
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

  lv_obj_t *left = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 8);
  make_label(left, LV_SYMBOL_LEFT, &lv_font_montserrat_20, TEXT_SECONDARY);
  s_breadcrumbRoot = make_label(left, "설정 (SETTINGS)", &ui_font_sm, TEXT_PRIMARY);
  make_label(left, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, TEXT_DIM);
  s_breadcrumbSection = make_label(left, kMenu[0].title, &ui_font_sm, TEXT_SECONDARY);

  lv_obj_t *right = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 8);
  s_breadcrumbLock = ui_icons::lock(right, 18, c(TEXT_SECONDARY));
  lv_obj_add_flag(s_breadcrumbLock, LV_OBJ_FLAG_HIDDEN); // shown only in Engineer Mode
  make_label(right, "PM 04:40", &ui_font_sm, TEXT_SECONDARY);
  s_breadcrumbWifi = ui_icons::wifi(right, 20, c(TEXT_PRIMARY));
}

void build_footer(lv_obj_t *parent) {
  s_footerBar = make_plain(parent);
  lv_obj_set_size(s_footerBar, LV_PCT(100), 32);
  lv_obj_set_flex_flow(s_footerBar, LV_FLEX_FLOW_ROW);
  // Right-aligned rather than centered: keeps the legend under the main
  // content column instead of spanning/centering across the sidebar too.
  lv_obj_set_flex_align(s_footerBar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_footerBar, 10, 0);
}

// Footer content is rebuilt from scratch each time set_footer() runs -
// simpler than juggling a persistent set of labels, and cheap since it
// only happens on section changes, not every frame.
void footer_add_sep() { make_label(s_footerBar, "|", &ui_font_xs, TEXT_DIM); }

// The one shared "keycap" component every footer button hint uses - a
// small bordered box around just the glyph (OK text, or a montserrat
// arrow icon), per the reference mockup showing all three hints boxed
// the same way, not just OK.
lv_obj_t *footer_make_key(lv_obj_t *parent, const char *glyph, const lv_font_t *font) {
  lv_obj_t *key = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(key, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(key, 8, 0);
  lv_obj_set_style_pad_ver(key, 2, 0);
  make_label(key, glyph, font, TEXT_SECONDARY);
  return key;
}

void footer_add_ok(const char *text) {
  lv_obj_t *g = make_row(s_footerBar, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  footer_make_key(g, "OK", &ui_font_xs);
  make_label(g, text, &ui_font_xs, TEXT_DIM);
}

void footer_add_icons(const char *icons, const char *text) {
  lv_obj_t *g = make_row(s_footerBar, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  footer_make_key(g, icons, &lv_font_montserrat_14);
  make_label(g, text, &ui_font_xs, TEXT_DIM);
}

void set_footer() {
  if (!s_footerBar) return; // footer bar not built yet - see create()'s ordering note
  lv_obj_clean(s_footerBar);
  if (s_in_panel) {
    footer_add_ok("저장 (Save)");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_RIGHT, "선택/이동");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, "취소");
  } else if (s_mode == Mode::Outer && s_selected == 3) {
    footer_add_ok("진입 (Enter)");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, "메뉴");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, "나가기");
  } else {
    footer_add_ok("선택 (Enter)");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, "메뉴");
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, "나가기");
  }
}

} // namespace

lv_obj_t *create() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, c(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scr, 14, 0);
  lv_obj_set_style_pad_row(scr, 10, 0);

  build_breadcrumb(scr);

  lv_obj_t *body = make_row(scr, LV_PCT(100), 342, 12);

  s_sidebarSlot = make_plain(body);
  lv_obj_set_size(s_sidebarSlot, 232, LV_PCT(100));
  build_sidebar_outer(s_sidebarSlot);

  s_contentPanel = make_plain(body);
  lv_obj_set_size(s_contentPanel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(s_contentPanel, 1);
  lv_obj_set_flex_flow(s_contentPanel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_contentPanel, 10, 0);

  // build_footer() must run before build_content_for(): the latter calls
  // set_footer(), which populates s_footerBar - it has to exist first.
  // It's still created here (after body) so it lands last in scr's
  // flex-column order, i.e. visually at the bottom.
  build_footer(scr);
  snapshot_data();
  build_content_for(s_selected);

  return scr;
}

void select_menu(int idx) {
  if (s_mode != Mode::Outer || s_in_panel) return;
  if (idx < 0 || idx >= kMenuCount || idx == s_selected) return;
  s_selected = idx;
  for (int i = 0; i < kMenuCount; i++) restyle_menu_item(i);
  update_breadcrumb();
  build_content_for(idx);
}

void select_next() {
  if (s_mode != Mode::Outer || s_in_panel) return;
  int idx = s_selected;
  for (int step = 0; step < kMenuCount; step++) {
    idx = (idx + 1) % kMenuCount;
    if (kMenu[idx].enabled) {
      select_menu(idx);
      return;
    }
  }
}

void select_prev() {
  if (s_mode != Mode::Outer || s_in_panel) return;
  int idx = s_selected;
  for (int step = 0; step < kMenuCount; step++) {
    idx = (idx - 1 + kMenuCount) % kMenuCount;
    if (kMenu[idx].enabled) {
      select_menu(idx);
      return;
    }
  }
}

bool is_in_engineer() { return s_mode == Mode::Engineer; }

void enter_engineer() {
  if (s_mode != Mode::Outer || s_selected != 3) return; // only from the Engineer Mode portal
  s_mode = Mode::Engineer;
  s_engSelected = 0;
  s_in_panel = false;
  s_panel_row = 0;

  lv_obj_clean(s_sidebarSlot);
  build_sidebar_engineer(s_sidebarSlot);
  update_breadcrumb();
  build_engineer_content_for(s_engSelected);
}

void exit_engineer() {
  if (s_mode != Mode::Engineer) return;
  s_in_panel = false;
  s_panel_row = 0;
  s_mode = Mode::Outer;

  lv_obj_clean(s_sidebarSlot);
  build_sidebar_outer(s_sidebarSlot);
  update_breadcrumb();
  build_content_for(s_selected);
}

void engineer_select_next() {
  if (s_mode != Mode::Engineer || s_in_panel) return;
  int idx = (s_engSelected + 1) % kEngMenuCount;
  s_engSelected = idx;
  for (int i = 0; i < kEngMenuCount; i++) restyle_eng_menu_item(i);
  update_breadcrumb();
  build_engineer_content_for(idx);
}

void engineer_select_prev() {
  if (s_mode != Mode::Engineer || s_in_panel) return;
  int idx = (s_engSelected - 1 + kEngMenuCount) % kEngMenuCount;
  s_engSelected = idx;
  for (int i = 0; i < kEngMenuCount; i++) restyle_eng_menu_item(i);
  update_breadcrumb();
  build_engineer_content_for(idx);
}

void refresh_content() {
  if (s_mode == Mode::Engineer) {
    build_engineer_content_for(s_engSelected);
  } else {
    build_content_for(s_selected);
  }
}

void enter_panel() {
  if (s_in_panel) return;
  // AHU control: seed the local pick from whatever's actually confirmed,
  // so editing starts from reality instead of a stale pick from the last
  // visit. Left as-is (0/1/false) if nothing has ever been confirmed -
  // the panel shows that plainly via the confirmed badge.
  if (s_mode == Mode::Outer && s_selected == 0) {
    if (s_ahuModeEverConfirmed) {
      s_data.ahu_mode = s_confirmedMode;
      s_data.ahu_volume = s_confirmedVolume;
    }
    if (s_ahuCleanEverConfirmed) s_data.ahu_clean = s_confirmedClean;
  }
  snapshot_data();
  s_in_panel = true;
  s_panel_row = 0;
  refresh_content();
}

void leave_panel(bool save) {
  if (!s_in_panel) return;
  if (save) {
    if (s_mode == Mode::Outer && s_selected == 2) {
      if (s_panel_row == 0) s_data.wifi_action = 1;
      else if (s_panel_row == 1) s_data.wifi_action = 2;
      else {
        s_data.wifi_action = 3;
        s_data.wifi_connected = false;
      }
      if (s_data.wifi_action == 1 || s_data.wifi_action == 2) s_data.wifi_connected = true;
    }
    if (s_mode == Mode::Outer && s_selected == 4) {
      s_data.filter_pct = 100;
      s_data.filter_hours = 0;
    }
    snapshot_data();
  } else {
    restore_data();
  }
  s_in_panel = false;
  s_panel_row = 0;
  refresh_content();
}

int panel_row_count() {
  if (s_mode == Mode::Engineer) return (s_engSelected == 0) ? 1 : 2;
  switch (s_selected) {
    case 0: return 3; // AHU control: mode / volume / clean
    case 1: return 4; // AHU reservation: start-hour / stop-hour / start action / stop action
    case 2: return 3; // wifi: pair / scan / disconnect
    case 4: return 1; // filter: reset
    default: return 1;
  }
}

void change_id(int delta) { s_data.eng_id = wrap(s_data.eng_id + delta, 16); }

// Mode/volume/clean (case 0) fire their backend call immediately here -
// "cycled by button", not an edit deferred to OK. Reservation hour
// pickers (case 1, rows 0/1) are the one spot left that's still a plain
// local value; rows 2/3 there have nothing to adjust via Left/Right,
// they're one-shot actions fired from handle_ok() instead.
void panel_adjust(int delta) {
  if (s_mode == Mode::Engineer) {
    if (s_engSelected == 0) {
      change_id(delta);
    } else {
      int *level = (s_panel_row == 0) ? &s_data.vsp_supply : &s_data.vsp_exhaust;
      *level = wrap(*level + delta, 3);
    }
    refresh_content();
    return;
  }

  switch (s_selected) {
    case 0:
      // Local pick only - nothing is sent until OK (see handle_ok()).
      if (s_panel_row == 0) s_data.ahu_mode = wrap(s_data.ahu_mode + delta, 4);
      else if (s_panel_row == 1) s_data.ahu_volume = wrap(s_data.ahu_volume + delta, 4);
      else s_data.ahu_clean = !s_data.ahu_clean;
      break;
    case 1:
      if (s_panel_row == 0) s_data.ahu_start_hour = wrap(s_data.ahu_start_hour + delta, 24);
      else if (s_panel_row == 1) s_data.ahu_stop_hour = wrap(s_data.ahu_stop_hour + delta, 24);
      break;
    case 2:
      s_panel_row = wrap(s_panel_row + delta, 3);
      break;
    default:
      break;
  }
  refresh_content();
}

void panel_move_row(int delta) {
  if (s_mode == Mode::Engineer && s_engSelected == 0) {
    change_id(delta);
    refresh_content();
    return;
  }
  s_panel_row = wrap(s_panel_row + delta, panel_row_count());
  refresh_content();
}

bool is_in_panel() { return s_in_panel; }

void handle_up() {
  if (s_in_panel) {
    panel_move_row(-1);
    return;
  }
  if (s_mode == Mode::Engineer) engineer_select_prev();
  else select_prev();
}

void handle_down() {
  if (s_in_panel) {
    panel_move_row(1);
    return;
  }
  if (s_mode == Mode::Engineer) engineer_select_next();
  else select_next();
}

void handle_left() {
  if (s_in_panel) panel_adjust(-1);
}

void handle_right() {
  if (s_in_panel) panel_adjust(1);
}

void handle_ok() {
  if (s_in_panel) {
    if (s_mode == Mode::Outer && s_selected == 0) {
      // AHU control: OK sends ONLY the focused row's command - Left/Right
      // only edited the local pick, nothing was sent yet. Stays in the
      // panel afterward (success or failure) so other rows can still be
      // picked and sent in the same visit; Back is what leaves.
      if (s_panel_row == 0) fire_mode();
      else if (s_panel_row == 1) fire_volume();
      else fire_clean();
      return;
    }
    if (s_mode == Mode::Outer && s_selected == 1) {
      // AHU reservation: OK only does something on the two action rows.
      // Rows 0/1 (hour pickers) are no-ops here - navigate to the action
      // row to actually send.
      if (s_panel_row == 2) fire_start_reservation();
      else if (s_panel_row == 3) fire_stop_reservation();
      return;
    }
    leave_panel(true);
    return;
  }
  if (s_mode == Mode::Outer && s_selected == 3) {
    enter_engineer();
    return;
  }
  enter_panel();
}

bool handle_back() {
  if (s_in_panel) {
    // AHU control (s_selected == 0) needs no special case here anymore:
    // the generic restore_data() below correctly discards only whatever
    // local pick was never sent - fire_mode/volume/clean() already lock
    // a sent field into s_snap the moment it's confirmed, so Back can't
    // undo a row that already reached the controller.
    leave_panel(false);
    return true;
  }
  if (s_mode == Mode::Engineer) {
    exit_engineer();
    return true;
  }
  return false;
}

} // namespace ui_settings_screen
