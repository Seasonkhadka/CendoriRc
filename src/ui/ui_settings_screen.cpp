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
#include "rc/RcConfig.h"
#include "rc/RcController.h"

#include <stdio.h>
#include <string.h>
#include <string>

#include "io/sensors.h"
#include "net/WifiController.h"
#include "nvs.h"
#include "rs485/Rs485Config.h"
#include "ui_lang.h"

using namespace ui_theme;
using ui_lang::tr;

namespace ui_settings_screen {

// Rebuilds whichever content panel is currently shown - defined further
// down (outer namespace, alongside create()/select_menu()/etc.), but the
// AHU send/confirm plumbing inside the anonymous namespace below needs
// to call it before that point, hence the forward declaration here (same
// outer scope as the real definition - a declaration inside the nested
// anonymous namespace would just create a second, unrelated symbol).
void refresh_content();

namespace {

constexpr int kMenuCount = 7;

struct MenuItem {
  const char *title;    // Korean title
  const char *subtitle; // "(English)" under the Korean title
  const char *title_en; // English title (English UI, no subtitle)
  bool enabled; // false = greyed out / not yet available
  bool locked;  // true = show a lock icon (password-protected section)
};

const MenuItem kMenu[kMenuCount] = {
    {"풍량 설정", "(Airflow)", "Airflow", true, false},
    {"예약 설정", "(Schedule)", "Schedule", true, false},
    {"네트워크 설정", "(Wi-Fi)", "Wi-Fi", true, false},
    {"필터 상태", "(Filter)", "Filter", true, false},
    {"시간 설정", "(Clock)", "Clock", true, false},
    {"언어 설정", "(Language)", "Language", true, false},
    {"기기 설정", "(Engineer Mode)", "Engineer Mode", true, true},
};

// Engineer Mode is a locked sub-area with its own nested sidebar (3 items)
// and its own content panel per item - selecting it in the outer sidebar
// shows a simple "portal" panel; pressing OK on that portal drills in.
constexpr int kEngMenuCount = 3;
struct EngMenuItem {
  const char *title;
  const char *subtitle;
  const char *title_en;
};
const EngMenuItem kEngMenu[kEngMenuCount] = {
    {"ID 설정", "(Device ID)", "Device ID"},
    {"VSP 설정", "(Motor VSP)", "Motor VSP"},
    {"PIN 설정", "(Change PIN)", "Change PIN"},
};

const char *menu_title(int i) { return tr(kMenu[i].title, kMenu[i].title_en); }
const char *eng_menu_title(int i) { return tr(kEngMenu[i].title, kEngMenu[i].title_en); }

enum class Mode { Outer, Engineer };
Mode s_mode = Mode::Outer;

lv_obj_t *s_breadcrumbRoot = nullptr;
lv_obj_t *s_breadcrumbSection = nullptr;
lv_obj_t *s_breadcrumbLock = nullptr;
lv_obj_t *s_breadcrumbWifi = nullptr;
lv_obj_t *s_breadcrumbTime = nullptr;
// Wi-Fi state, pushed in through set_wifi_state().
WifiDto s_wifi;
// Wi-Fi panel page while editing it: status / QR setup / network list /
// password keyboard. Outside the panel it always shows the status page.
enum class WifiView { Main, Qr, Scan, Keyboard };
WifiView s_wifiView = WifiView::Main;
int s_wifiSel = 0;          // network list selection (index apCount = "rescan")
char s_kbSsid[33] = "";     // network the keyboard types a password for
std::string s_kbText;       // password typed so far
int s_kbPage = 0;           // 0 abc / 1 ABC / 2 #+=
int s_kbRow = 0, s_kbCol = 0;
// Current RTC time, pushed in through set_time() (Schedule "acts at" line).
bool s_clockValid = false;
int s_clockY = 2026, s_clockMo = 1, s_clockD = 1, s_clockH = 0, s_clockM = 0;

lv_obj_t *s_sidebarSlot = nullptr; // rebuilt on Outer <-> Engineer switch
lv_obj_t *s_menuCards[kMenuCount] = {};
lv_obj_t *s_menuTitles[kMenuCount] = {};
lv_obj_t *s_menuSubtitles[kMenuCount] = {};
lv_obj_t *s_engMenuCards[kEngMenuCount] = {};
lv_obj_t *s_engMenuTitles[kEngMenuCount] = {};
lv_obj_t *s_engMenuSubtitles[kEngMenuCount] = {};

lv_obj_t *s_contentPanel = nullptr;
int s_selected = 0;    // outer sidebar index (0-4)
int s_engSelected = 0; // engineer sub-sidebar index (0-2), while s_mode == Engineer
bool s_pinOpen = false;
int s_pinPos = 0;
int s_pinDigit[4] = {};
bool s_pinWrong = false;
int s_savedPin = RC_ENGINEER_PIN;

int clamp_pin(int v) {
  if (v < 0) return 0;
  if (v > 9999) return 9999;
  return v;
}

void digits_from_pin(int pin, int d[4]) {
  pin = clamp_pin(pin);
  d[0] = pin / 1000;
  d[1] = (pin / 100) % 10;
  d[2] = (pin / 10) % 10;
  d[3] = pin % 10;
}

int pin_from_digits(const int d[4]) {
  return clamp_pin(d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3]);
}

void load_pin() {
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READONLY, &h) != ESP_OK) return;
  uint16_t v = 0;
  if (nvs_get_u16(h, "eng_pin", &v) == ESP_OK) s_savedPin = clamp_pin((int)v);
  nvs_close(h);
}

void save_pin(int pin) {
  s_savedPin = clamp_pin(pin);
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u16(h, "eng_pin", (uint16_t)s_savedPin);
  nvs_commit(h);
  nvs_close(h);
}

void pin_reset() {
  s_pinOpen = false;
  s_pinPos = 0;
  s_pinWrong = false;
  s_pinDigit[0] = s_pinDigit[1] = s_pinDigit[2] = s_pinDigit[3] = 0;
}

bool pin_matches() {
  return pin_from_digits(s_pinDigit) == s_savedPin;
}

// Right-panel edit session. While s_in_panel is true, arrows change the
// working copy (s_data). s_snap is the last saved copy; Back restores it.
bool s_in_panel = false;
int s_panel_row = 0;

struct SettingsData {
  // Local picks for the RC controls - seeded from RcController state on
  // entering the panel, sent only by fire_*() on OK.
  int ahu_mode = 0;        // top-level mode: 0 전열 / 1 바이패스 / 2 청정 / 3 자동 / 4 AI
  int vent_sub = 0;        // 전열 / 바이패스 방식: 0 일반 (balanced) / 1 양압 / 2 음압
  int press_kind = RC::PRESS_POSITIVE; // "다시 설정" row: which pressure mode to set up again
  int ahu_volume = 1;      // 1 low / 2 mid / 3 high (stop = power key)
  int resv_type = 0;       // 0 가동예약 / 1 정지예약 / 2 취침 / 3 취소 (old RC order)
  int resv_min = 60;       // 10..1440, 10-minute steps
  int eng_id = 1;        // device ID 1..99 (decimal = ID byte)
  // VSP edit buffer, 0.05 V units, old RC order:
  // 전열 배기 L/M/H, 전열 급기 L/M/H, 바이패스 배기 L/M/H, 바이패스 급기 L/M/H
  int vsp[12] = {};
  int vsp_slot = 0;      // 0..11 - Left/Right moves, Up/Down changes the value
  int filter_init = RC_FILTER_INIT_DEFAULT_X100; // x100 h
  int clock_y = 2026, clock_mo = 1, clock_d = 1, clock_h = 12, clock_mi = 0;
  int lang = 0;         // ui_lang::Lang: 0 한국어 / 1 English
  int pin = 0;          // 0000~9999, Engineer PIN edit
};

// Set when a new language was saved; main.cpp rebuilds the screens.
bool s_langChanged = false;

SettingsData s_data;
SettingsData s_snap;

// Pressure-mode setup wizard (Airflow panel): room type -> people / toilets ->
// result -> [적용]. Only Apply reaches the controller; nothing before it
// changes the unit.
struct PressWizard {
  bool open = false;
  uint8_t kind = RC::PRESS_POSITIVE;
  int step = 0;   // 0 room type, 1 count, 2 result
  int room = 0;
  int count = 1;
};
PressWizard s_wiz;

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

// ---- RC state (RcController) -------------------------------------------
// s_data.ahu_* is the local pick edited with Left/Right. s_rc is the
// controller's current state, pushed in through set_state(). OK only calls
// a controller command: the change reaches MAIN in the next poll reply.
RcDto s_rc;
lv_obj_t *s_screen = nullptr;
char s_ahuResultBuf[96] = "";
const char *s_ahuResultMsg = nullptr;


void show_result(bool ok) {
  snprintf(s_ahuResultBuf, sizeof(s_ahuResultBuf), "%s",
           ok ? tr("적용됨 - 다음 응답에 전송", "Queued - sent in the next reply")
              : tr("무시됨 - LOCK/OFF/AUTO/취침/양압·음압", "Ignored - LOCK/OFF/AUTO/SLEEP/PRESSURE"));
  s_ahuResultMsg = s_ahuResultBuf;
  refresh_content();
}

void open_wizard(uint8_t kind) {
  int n = 0;
  const RC::PressureRoom *rooms = RC::pressureRooms(kind, n);
  const RC::PressureSetup *saved = s_rc.pressureSetup(kind);
  const bool have = saved && saved->valid && saved->room < n;
  s_wiz.open = true;
  s_wiz.kind = kind;
  s_wiz.step = 0;
  s_wiz.room = have ? saved->room : 0;
  s_wiz.count = have ? saved->count : rooms[s_wiz.room].defCount;
  s_ahuResultMsg = nullptr;
  refresh_content();
}

// Mode only - fan speed and power are untouched. A pressure mode that was
// never set up opens the setup wizard instead (nothing is sent).
void fire_mode() {
  static const char *names[RC::UI_MODE_COUNT] = {"ventilation", "bypass", "clean", "auto", "ai",
                                                 "positive",    "negative", "bypass_positive", "bypass_negative"};
  // 전열 / 바이패스 + 양압 / 음압 -> the RC pressure modes on that air path;
  // + 일반 -> plain ventilation / bypass.
  int m = s_data.ahu_mode;
  if (m == RC::UI_VENTILATION && s_data.vent_sub == 1) m = RC::UI_POSITIVE;
  if (m == RC::UI_VENTILATION && s_data.vent_sub == 2) m = RC::UI_NEGATIVE;
  if (m == RC::UI_BYPASS && s_data.vent_sub == 1) m = RC::UI_BP_POSITIVE;
  if (m == RC::UI_BYPASS && s_data.vent_sub == 2) m = RC::UI_BP_NEGATIVE;
  if (m >= RC::UI_POSITIVE) {
    const uint8_t kind = (m == RC::UI_POSITIVE || m == RC::UI_BP_POSITIVE) ? RC::PRESS_POSITIVE : RC::PRESS_NEGATIVE;
    const RC::PressureSetup *saved = s_rc.pressureSetup(kind);
    if (!saved || !saved->valid) {
      open_wizard(kind);
      return;
    }
  }
  bool ok = RcController::instance().setModeState(names[m]);
  s_snap.ahu_mode = s_data.ahu_mode;
  s_snap.vent_sub = s_data.vent_sub;
  show_result(ok);
}

void fire_volume() {
  bool ok = RcController::instance().setVolumeState(std::to_string(s_data.ahu_volume));
  s_snap.ahu_volume = s_data.ahu_volume;
  show_result(ok);
}

// Schedule panel send row: one-shot, leaves the panel afterwards.
void fire_reservation() {
  std::string cmd;
  switch (s_data.resv_type) {
    case 0: cmd = "start_reserve_" + std::to_string(s_data.resv_min); break;
    case 1: cmd = "stop_reserve_" + std::to_string(s_data.resv_min); break;
    case 2: cmd = "sleep"; break;
    default: cmd = "cancel"; break;
  }
  bool ok = RcController::instance().setReservationState(cmd);
  s_in_panel = false;
  s_panel_row = 0;
  show_result(ok);
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
  elevate(card);
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

  if (sub_text[0]) make_label(pill, sub_text, &ui_font_xs, selected ? PILL_TEXT_SUB : TEXT_DIM);
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
  if (sub_text[0]) make_label(pill, sub_text, &ui_font_xs, selected ? PILL_TEXT_SUB : TEXT_DIM);
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
  const char *text = s_rc.linkLost ? tr("통신 끊김", "No link to main unit")
                                   : (s_ahuResultMsg ? s_ahuResultMsg : tr("PLC 대기", "PLC idle"));
  make_label(card, text, &ui_font_xs, s_rc.linkLost ? ACCENT_CYAN : TEXT_DIM);
}

// Current RC state (what the next reply to MAIN will carry), separate from
// the editable pick below.
void build_ahu_confirmed_badge(lv_obj_t *parent) {
  static const char *modeShort[RC::UI_MODE_COUNT] = {"Ventilation", "Bypass", "Clean", "Auto", "AI",
                                                     "Positive",    "Negative", "Bypass positive", "Bypass negative"};
  char buf[96];
  snprintf(buf, sizeof(buf), "%s: %s / %s / Fan %d%s%s", tr("현재 상태", "State"),
           s_rc.isRunning() ? "ON" : "OFF",
           modeShort[s_rc.uiMode()], (int)s_rc.effectiveVolume(), s_rc.night ? " / SLEEP" : "",
           s_rc.ahuLock ? " / LOCK" : "");
  lv_obj_t *l = make_label(parent, buf, &ui_font_xs, TEXT_DIM);
  lv_obj_set_flex_grow(l, 0);

  // Pressure-mode status: both airflows, offset, CO2 boost.
  uint16_t sup = 0, exh = 0;
  if (s_rc.pressureAirflow(sup, exh)) {
    const RC::PressureSetup *ps = s_rc.pressureSetup(s_rc.pressure);
    const bool pos = s_rc.pressure == RC::PRESS_POSITIVE;
    snprintf(buf, sizeof(buf), "%s %c%u%%  %s %u m³/h · %s %u m³/h%s", pos ? tr("양압 모드", "Positive mode")
             : tr("음압 모드", "Negative mode"), pos ? '+' : '-', (unsigned)ps->offsetPct, tr("급기", "Supply"),
             (unsigned)sup, tr("배기", "Exhaust"), (unsigned)exh,
             s_rc.pressBoost ? tr(" · CO₂ 부스트 동작 중", " · CO₂ boost active") : "");
    lv_obj_t *pl = make_label(parent, buf, &ui_font_xs, ACCENT_CYAN);
    lv_obj_set_flex_grow(pl, 0);
  }
}

// ---- Airflow section -> repurposed as live AHU control -----------------
// Up/Down selects a row; Left/Right only edits the LOCAL pick
// (s_data.ahu_mode/ahu_volume) - nothing is sent yet. OK
// (handle_ok()) sends the focused row's command and stays in the panel;
// Back discards whatever was picked but never sent (see handle_back()).

void build_pressure_wizard(lv_obj_t *parent);

// Airflow panel rows. 전열 and 바이패스 show their 방식 row (일반 / 양압 / 음압)
// and the pressure re-run row; the other modes show only mode + fan.
enum AirRow { AIR_MODE, AIR_VENT, AIR_FAN, AIR_RERUN };

bool vent_picked() { return s_data.ahu_mode == RC::UI_VENTILATION || s_data.ahu_mode == RC::UI_BYPASS; }

int airflow_row_count() { return vent_picked() ? 4 : 2; }

AirRow airflow_row(int idx) {
  static const AirRow vent[4] = {AIR_MODE, AIR_VENT, AIR_FAN, AIR_RERUN};
  static const AirRow other[2] = {AIR_MODE, AIR_FAN};
  return vent_picked() ? vent[idx < 4 ? idx : 0] : other[idx < 2 ? idx : 0];
}

bool airflow_focused(AirRow r) { return airflow_row(s_panel_row) == r; }

// One choice in a row card: white filled pill when picked, plain text
// otherwise - the only selection indicator. icon: 0 none, 1 양압, 2 음압.
void make_choice(lv_obj_t *parent, const char *text, bool sel, int icon) {
  lv_obj_t *pill = make_plain(parent);
  lv_obj_set_height(pill, LV_PCT(100));
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_style_radius(pill, 12, 0);
  lv_obj_set_style_bg_color(pill, c(PILL_BG), 0);
  lv_obj_set_style_bg_opa(pill, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 5, 0);
  const uint32_t col = sel ? PILL_TEXT : TEXT_SECONDARY;
  if (icon == 1) ui_icons::pressure_out(pill, 16, c(col));
  if (icon == 2) ui_icons::pressure_in(pill, 16, c(col));
  make_label(pill, text, &ui_font_sm, col);
}

// Row card: optional title on the left, choices filling the rest.
lv_obj_t *make_choice_card(lv_obj_t *parent, lv_coord_t h, const char *title, bool focused) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), h);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_set_style_pad_left(card, title ? 16 : 6, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 6, 0);
  apply_focus_frame(card, focused);
  if (title) {
    lv_obj_t *t = make_label(card, title, &ui_font_xs, TEXT_PRIMARY);
    lv_obj_set_width(t, 76);
  }
  return card;
}

void build_airflow_panel(lv_obj_t *parent) {
  if (s_wiz.open) {
    build_pressure_wizard(parent);
    return;
  }
  build_ahu_confirmed_badge(parent);

  // Row: mode (전열 / 바이패스 / 청정 / 자동 / AI)
  static const char *modeMain[5] = {"전열", "바이패스", "청정", "자동", "AI"};
  static const char *modeEn[5] = {"Vent", "Bypass", "Clean", "Auto", "AI"};
  lv_obj_t *mode_card = make_choice_card(parent, 52, nullptr, airflow_focused(AIR_MODE));
  for (int i = 0; i < 5; i++) make_choice(mode_card, tr(modeMain[i], modeEn[i]), i == s_data.ahu_mode, 0);

  // Row: 전열 / 바이패스 방식 (only under those two) - 일반 / 양압 / 음압
  if (vent_picked()) {
    static const char *subMain[3] = {"일반", "양압", "음압"};
    static const char *subEn[3] = {"Normal", "Positive", "Negative"};
    const bool bp = s_data.ahu_mode == RC::UI_BYPASS;
    lv_obj_t *sub = make_choice_card(parent, 48, bp ? tr("바이패스 방식", "Bypass type") : tr("전열 방식", "Vent type"),
                                     airflow_focused(AIR_VENT));
    for (int i = 0; i < 3; i++) make_choice(sub, tr(subMain[i], subEn[i]), i == s_data.vent_sub, i);
  }

  // Row: fan level
  static const char *volMain[3] = {"약", "중", "강"};
  static const char *volEn[3] = {"Low", "Mid", "High"};
  lv_obj_t *vol = make_choice_card(parent, 48, tr("풍량", "Fan"), airflow_focused(AIR_FAN));
  for (int i = 0; i < 3; i++) make_choice(vol, tr(volMain[i], volEn[i]), i + 1 == s_data.ahu_volume, 0);

  // Row: re-run the pressure setup (only under 전열 / 바이패스) - plain text.
  // Left/Right picks 양압 / 음압, OK opens the wizard.
  if (vent_picked()) {
    lv_obj_t *rerun = make_card(parent, CARD_BG);
    lv_obj_set_size(rerun, LV_PCT(100), 40);
    lv_obj_set_flex_grow(rerun, 0);
    lv_obj_set_style_pad_hor(rerun, 16, 0);
    lv_obj_set_flex_flow(rerun, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rerun, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const bool focus = airflow_focused(AIR_RERUN);
    apply_focus_frame(rerun, focus);
    make_label(rerun, tr("양압/음압 다시 설정", "Re-run pressure setup"), &ui_font_xs, TEXT_PRIMARY);
    const uint8_t k = (uint8_t)s_data.press_kind;
    const RC::PressureSetup *ps = s_rc.pressureSetup(k);
    char sub[24];
    if (ps && ps->valid) snprintf(sub, sizeof(sub), "%c%u%%", k == RC::PRESS_POSITIVE ? '+' : '-', (unsigned)ps->offsetPct);
    else snprintf(sub, sizeof(sub), "%s", tr("미설정", "Not set"));
    char text[64];
    snprintf(text, sizeof(text), (s_in_panel && focus) ? "<  %s %s  >" : "%s %s",
             k == RC::PRESS_POSITIVE ? tr("양압", "Positive") : tr("음압", "Negative"), sub);
    make_label(rerun, text, &ui_font_xs, (s_in_panel && focus) ? TEXT_PRIMARY : TEXT_SECONDARY);
  }

  build_ahu_status_strip(parent);
}

// ---- Schedule section - old RC reservation (SHR-L2 2.3) ---------------
// Row 0: type pills, Left/Right cycles 가동예약 -> 정지예약 -> 취침 -> 취소.
// Row 1: time stepper in 10-minute steps (start / stop reservation only).
// OK on either row sends (fire_reservation()) and leaves the panel.

// Round "-" / "+" key of the time stepper.
void make_step_key(lv_obj_t *parent, const char *glyph, bool active) {
  lv_obj_t *key = make_plain(parent);
  lv_obj_set_size(key, 52, 52);
  lv_obj_set_style_radius(key, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(key, c(CARD_BG_ALT), 0);
  lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(key, 1, 0);
  lv_obj_set_style_border_color(key, c(active ? TEXT_SECONDARY : BORDER), 0);
  lv_obj_t *l = make_label(key, glyph, &ui_font_md, active ? TEXT_PRIMARY : TEXT_DIM);
  lv_obj_center(l);
}

// "PM 06:22 자동 정지 예약 / Auto Off at 18:22" - when the reservation will
// act, from the current RTC time; falls back to "in H:MM" without a clock.
void reservation_info(char *buf, size_t n) {
  const int type = s_data.resv_type;
  if (type == 2) {
    snprintf(buf, n, "%s", tr("취침 모드 - 약풍", "Sleep mode - Low fan"));
    return;
  }
  if (type == 3) {
    snprintf(buf, n, "%s", tr("예약 취소", "Cancel reservation"));
    return;
  }
  const char *ko = type == 0 ? "자동 가동 예약" : "자동 정지 예약";
  const char *en = type == 0 ? "Auto On" : "Auto Off";
  if (!s_clockValid) {
    if (ui_lang::en()) snprintf(buf, n, "%s in %d:%02d", en, s_data.resv_min / 60, s_data.resv_min % 60);
    else snprintf(buf, n, "%s %d:%02d 후", ko, s_data.resv_min / 60, s_data.resv_min % 60);
    return;
  }
  const int at = (s_clockH * 60 + s_clockM + s_data.resv_min) % (24 * 60);
  const int h24 = at / 60, m = at % 60;
  const int h12 = h24 % 12 == 0 ? 12 : h24 % 12;
  if (ui_lang::en()) snprintf(buf, n, "%s at %s %02d:%02d", en, h24 < 12 ? "AM" : "PM", h12, m);
  else snprintf(buf, n, "%s %s %02d:%02d", ko, h24 < 12 ? "AM" : "PM", h12, m);
}

void build_schedule_panel(lv_obj_t *parent) {
  static const char *typeMain[4] = {"가동 예약", "정지 예약", "취침", "취소"};
  static const char *typeEn[4] = {"Start", "Stop", "Sleep", "Cancel"};
  const bool timed = s_data.resv_type <= 1;

  lv_obj_t *type_row = make_row(parent, LV_PCT(100), 64, 8);
  lv_obj_set_flex_grow(type_row, 0);
  apply_focus_frame(type_row, s_panel_row == 0);
  for (int i = 0; i < 4; i++) {
    make_mode_option(type_row, tr(typeMain[i], typeEn[i]), "", i == s_data.resv_type);
  }

  // Time stepper card: [-]  1:30 후  [+], with the "acts at" line below.
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), 150);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 10, 0);
  apply_focus_frame(card, s_panel_row == 1);

  lv_obj_t *stepper = make_row(card, LV_PCT(100), 80, 0);
  lv_obj_set_flex_align(stepper, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(stepper, 24, 0);
  make_step_key(stepper, "-", timed);

  lv_obj_t *value = make_row(stepper, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 10);
  lv_obj_set_flex_align(value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  char time_buf[16];
  if (timed) snprintf(time_buf, sizeof(time_buf), "%d:%02d", s_data.resv_min / 60, s_data.resv_min % 60);
  else snprintf(time_buf, sizeof(time_buf), "--:--");
  make_label(value, time_buf, &ui_font_xxl, timed ? TEXT_PRIMARY : TEXT_DIM);
  make_label(value, tr("후", "later"), &ui_font_md, timed ? TEXT_SECONDARY : TEXT_DIM);

  make_step_key(stepper, "+", timed);

  lv_obj_t *info = make_card(card, CARD_BG_ALT);
  lv_obj_set_size(info, LV_PCT(100), 34);
  lv_obj_set_style_radius(info, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  char info_buf[80];
  reservation_info(info_buf, sizeof(info_buf));
  make_label(info, info_buf, &ui_font_xs, TEXT_SECONDARY);

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

// Filter values: MAIN's D6 when it sends one, otherwise the RC's own
// running-time count (RC_FILTER_LOCAL_COUNT). OK sends the initial value
// (B5 Data2 = 5), which also resets the filter time to it.
void build_filter_panel(lv_obj_t *parent) {
  const bool have = s_rc.hasFilterInfo();
  const int init = s_in_panel ? s_data.filter_init : s_rc.filterInit;
  const int remainH = s_rc.filterRemainHours();
  const int pct = have ? s_rc.filterRemainPct() : 0;
  const char *chipText = tr("정상", "Normal");
  uint32_t chipColor = ACCENT_CYAN;
  if (s_rc.filterReplaceDue()) {
    chipText = tr("교체", "Replace");
    chipColor = DANGER;
  } else if (s_rc.errCode & PLC::FILTER_CLEAN_ALARM) {
    chipText = tr("청소", "Clean");
    chipColor = WARN;
  }
  const char *hours = tr("시간", "h");

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
  lv_arc_set_value(ring, pct);
  lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(ring, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, c(TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, c(ACCENT_CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

  char pct_buf[8];
  if (have) snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
  else snprintf(pct_buf, sizeof(pct_buf), "--%%");
  lv_obj_t *pct_label = make_label(gauge, pct_buf, &ui_font_xxl, TEXT_PRIMARY);
  lv_obj_align(pct_label, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t *chip = make_status_chip(gauge, chipText, chipColor);
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
  make_label(usage, tr("누적 사용:", "Used:"), &ui_font_sm, TEXT_SECONDARY);
  char hours_buf[20];
  if (have) {
    snprintf(hours_buf, sizeof(hours_buf), "%d %s", s_rc.filterUsedHours(), hours);
  } else {
    snprintf(hours_buf, sizeof(hours_buf), "-- %s", hours);
  }
  make_label(usage, hours_buf, &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *replace = make_plain(info);
  lv_obj_set_flex_flow(replace, LV_FLEX_FLOW_COLUMN);
  make_label(replace, tr("잔여 시간:", "Remaining:"), &ui_font_sm, TEXT_SECONDARY);
  lv_obj_t *dateRow = make_row(replace, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  char remain_buf[20];
  if (have) snprintf(remain_buf, sizeof(remain_buf), "%d %s", remainH, hours);
  else snprintf(remain_buf, sizeof(remain_buf), "-- %s", hours);
  make_label(dateRow, remain_buf, &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *note = make_label(parent,
                              tr("* 실내외 공기질 및 사용 환경에 따라\n차이가 있을 수 있습니다.",
                                 "* May vary with indoor/outdoor air quality\nand usage."),
                              &ui_font_xs, TEXT_DIM);
  lv_obj_set_size(note, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(note, 0);

  lv_obj_t *reset_btn = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(reset_btn, LV_PCT(100), 52);
  lv_obj_set_style_radius(reset_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_align(reset_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(reset_btn, true);
  lv_obj_t *reset_row = make_row(reset_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 8);
  ui_icons::refresh(reset_row, 20, c(TEXT_PRIMARY));
  char reset_buf[48];
  if (ui_lang::en()) snprintf(reset_buf, sizeof(reset_buf), "Filter RESET - initial %d h", init * 100);
  else snprintf(reset_buf, sizeof(reset_buf), "필터 초기화 - 초기값 %d 시간", init * 100);
  make_label(reset_row, reset_buf, &ui_font_sm, TEXT_PRIMARY);
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
  if (sub_text[0]) make_label(text, sub_text, &ui_font_xs, TEXT_SECONDARY);
  return btn;
}

// Signal-strength icon: the first `bars` of 4 lit, the rest dim.
void make_signal(lv_obj_t *parent, lv_coord_t size, int bars, uint32_t color) {
  lv_obj_t *icon = ui_icons::wifi_bars(parent, size, c(color));
  for (int i = bars; i < 4; i++) {
    lv_obj_t *bar = lv_obj_get_child(icon, i);
    if (bar) lv_obj_set_style_bg_color(bar, c(TRACK), 0);
  }
}

// A label that wraps inside its parent's width.
lv_obj_t *make_wrap_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = make_label(parent, text, font, color);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

// "'SSID' 연결 실패 - 비밀번호를 확인하세요" for the last failed new network.
void wifi_fail_text(char *buf, size_t n) {
  const char *ssid = s_wifi.failSsid;
  switch (s_wifi.fail) {
    case WIFI::FAIL_AUTH:
      snprintf(buf, n, tr("'%s' 연결 실패 - 비밀번호를 확인하세요", "'%s' failed - check the password"), ssid);
      break;
    case WIFI::FAIL_NO_AP:
      snprintf(buf, n, tr("'%s' 연결 실패 - 네트워크를 찾을 수 없음", "'%s' failed - network not found"), ssid);
      break;
    default:
      snprintf(buf, n, tr("'%s' 연결 실패", "'%s' failed to connect"), ssid);
      break;
  }
}

// Main view: live status + the three actions (QR setup, scan, forget).
void build_wifi_main(lv_obj_t *parent) {
  const bool connected = s_wifi.connected();
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_obj_t *ssid_row = make_row(card, LV_PCT(100), LV_SIZE_CONTENT, 8);
  lv_obj_set_flex_align(ssid_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const bool named = s_wifi.ssid[0] && s_wifi.state != WIFI::OFF;
  make_label(ssid_row, named ? s_wifi.ssid : tr("네트워크 없음", "No network"), &ui_font_md, TEXT_PRIMARY);
  make_signal(ssid_row, 22, connected ? WIFI::bars(s_wifi.rssi) : 0, ACCENT_CYAN);

  switch (s_wifi.state) {
    case WIFI::CONNECTED:
      make_status_chip(card, tr("연결됨", "Connected"), ACCENT_CYAN);
      break;
    case WIFI::CONNECTING:
      make_status_chip(card, tr("연결 중...", "Connecting..."), WARN);
      break;
    case WIFI::FAILED:
      make_status_chip(card, tr("연결 실패", "Failed"), DANGER);
      break;
    default:
      make_status_chip(card, tr("미연결", "Not connected"), TEXT_DIM);
      break;
  }
  if (s_wifi.fail != WIFI::FAIL_NONE && s_wifi.failSsid[0]) {
    char fail[96];
    wifi_fail_text(fail, sizeof(fail));
    make_wrap_label(card, fail, &ui_font_xs, DANGER);
  }

  lv_obj_t *ip_row = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(ip_row, "IP:", &ui_font_sm, TEXT_SECONDARY);
  make_label(ip_row, s_wifi.ip[0] ? s_wifi.ip : "-", &ui_font_sm, TEXT_PRIMARY);
  lv_obj_t *mac_row = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  make_label(mac_row, "MAC:", &ui_font_sm, TEXT_SECONDARY);
  make_label(mac_row, s_wifi.mac[0] ? s_wifi.mac : "-", &ui_font_sm, TEXT_PRIMARY);

  lv_obj_t *actions_row = make_row(parent, LV_PCT(100), 72, 8);
  lv_obj_set_flex_grow(actions_row, 0);
  lv_obj_t *pair_btn = make_action_button(actions_row, ui_icons::phone, tr("스마트폰 간편 연결", "Phone / QR Setup"),
                                          "");
  lv_obj_t *scan_btn = make_action_button(actions_row, ui_icons::search, tr("주변 Wi-Fi 검색", "Scan Wi-Fi"),
                                          "");
  apply_focus_frame(pair_btn, s_panel_row == 0);
  apply_focus_frame(scan_btn, s_panel_row == 1);

  lv_obj_t *disconnect_btn = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(disconnect_btn, LV_PCT(100), 44);
  lv_obj_set_flex_grow(disconnect_btn, 0);
  lv_obj_set_style_radius(disconnect_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_flex_align(disconnect_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(disconnect_btn, s_panel_row == 2);
  lv_obj_t *dc_row = make_row(disconnect_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 8);
  ui_icons::disconnect(dc_row, 18, c(TEXT_PRIMARY));
  make_label(dc_row, tr("네트워크 초기화", "Disconnect & forget"), &ui_font_sm, TEXT_PRIMARY);
}

// QR view: the phone scans the code, joins the setup AP and gets the
// portal page (WifiPortal) to pick the home network and type the password.
void build_wifi_qr(lv_obj_t *parent) {
  lv_obj_t *row = make_row(parent, LV_PCT(100), LV_PCT(100), 14);
  lv_obj_set_flex_grow(row, 1);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // White quiet zone around the code so phone cameras lock on easily.
  lv_obj_t *qr_box = make_plain(row);
  lv_obj_set_size(qr_box, 196, 196);
  lv_obj_set_style_bg_color(qr_box, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(qr_box, 12, 0);
  char qr_text[96];
  snprintf(qr_text, sizeof(qr_text), "WIFI:T:WPA;S:%s;P:%s;;", s_wifi.apSsid, s_wifi.apPass);
  lv_obj_t *qr = lv_qrcode_create(qr_box, 172, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr, qr_text, strlen(qr_text));
  lv_obj_center(qr);

  lv_obj_t *info = make_plain(row);
  lv_obj_set_size(info, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(info, 1);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(info, 8, 0);

  make_wrap_label(info, tr("스마트폰으로 연결", "Connect with your phone"), &ui_font_md, TEXT_PRIMARY);
  make_wrap_label(info, tr("1. 휴대폰 카메라로 QR 코드를 스캔하세요", "1. Scan the QR code with the phone camera"),
                  &ui_font_xs, TEXT_SECONDARY);
  make_wrap_label(info,
                  tr("2. 열린 페이지에서 Wi-Fi 를 선택하고 비밀번호를 입력하세요",
                     "2. On the page that opens, pick your Wi-Fi and enter the password"),
                  &ui_font_xs, TEXT_SECONDARY);
  make_wrap_label(info, tr("페이지가 안 열리면 브라우저에서 " WIFI_AP_IP " 접속",
                           "If no page opens, browse to " WIFI_AP_IP),
                  &ui_font_xs, TEXT_DIM);

  char buf[64];
  snprintf(buf, sizeof(buf), "%s %s", tr("네트워크:", "Network:"), s_wifi.apSsid);
  make_label(info, buf, &ui_font_sm, TEXT_PRIMARY);
  snprintf(buf, sizeof(buf), "%s %s", tr("비밀번호:", "Password:"), s_wifi.apPass);
  make_label(info, buf, &ui_font_sm, TEXT_PRIMARY);

  char status[96];
  uint32_t color = TEXT_DIM;
  if (s_wifi.state == WIFI::CONNECTED && s_wifi.setup) {
    snprintf(status, sizeof(status), "%s %s", tr("연결됨:", "Connected:"), s_wifi.ssid);
    color = ACCENT_CYAN;
  } else if (s_wifi.state == WIFI::CONNECTING && s_wifi.ssid[0]) {
    snprintf(status, sizeof(status), "%s %s", tr("연결 중:", "Connecting:"), s_wifi.ssid);
    color = WARN;
  } else if (s_wifi.fail != WIFI::FAIL_NONE && s_wifi.failSsid[0]) {
    wifi_fail_text(status, sizeof(status));
    color = DANGER;
  } else if (!s_wifi.setup) {
    snprintf(status, sizeof(status), "%s", tr("준비 중...", "Starting..."));
  } else if (s_wifi.apClients > 0) {
    snprintf(status, sizeof(status), "%s", tr("휴대폰 연결됨", "Phone connected"));
    color = ACCENT_CYAN;
  } else {
    snprintf(status, sizeof(status), "%s", tr("휴대폰 연결 대기 중", "Waiting for the phone"));
  }
  make_wrap_label(info, status, &ui_font_sm, color);
}

// Scan view: nearby networks (strongest first) + a "rescan" row at the end.
constexpr int kScanVisible = 6;

void build_wifi_scan(lv_obj_t *parent) {
  const int n = s_wifi.apCount;
  if (s_wifiSel > n) s_wifiSel = n;

  lv_obj_t *head = make_row(parent, LV_PCT(100), LV_SIZE_CONTENT, 8);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(head, tr("주변 Wi-Fi", "Nearby networks"), &ui_font_sm, TEXT_SECONDARY);
  if (s_wifi.scanning) make_label(head, tr("검색 중...", "Scanning..."), &ui_font_xs, WARN);

  if (n == 0) {
    lv_obj_t *empty = make_card(parent, CARD_BG);
    lv_obj_set_size(empty, LV_PCT(100), 80);
    lv_obj_set_flex_grow(empty, 0);
    lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(empty,
               s_wifi.scanning ? tr("검색 중...", "Scanning...")
                               : tr("네트워크를 찾지 못했습니다", "No networks found"),
               &ui_font_sm, TEXT_DIM);
  }

  // Window of kScanVisible rows that keeps the selection visible.
  const int total = n + 1;
  int first = s_wifiSel - (kScanVisible - 1);
  if (first < 0) first = 0;
  const int last = first + kScanVisible < total ? first + kScanVisible : total;
  for (int i = first; i < last; i++) {
    lv_obj_t *item = make_card(parent, CARD_BG);
    lv_obj_set_size(item, LV_PCT(100), 40);
    lv_obj_set_flex_grow(item, 0);
    lv_obj_set_style_pad_hor(item, 12, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 10, 0);
    apply_focus_frame(item, i == s_wifiSel);
    if (i < n) {
      const WIFI::ApInfo &ap = s_wifi.aps[i];
      make_signal(item, 18, WIFI::bars(ap.rssi), ACCENT_CYAN);
      lv_obj_t *name = make_label(item, ap.ssid, &ui_font_sm, TEXT_PRIMARY);
      lv_obj_set_flex_grow(name, 1);
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      if (s_wifi.connected() && strcmp(ap.ssid, s_wifi.ssid) == 0) {
        make_label(item, tr("연결됨", "Connected"), &ui_font_xs, ACCENT_CYAN);
      }
      if (ap.secure) ui_icons::lock(item, 14, c(TEXT_SECONDARY));
    } else {
      ui_icons::refresh(item, 18, c(TEXT_SECONDARY));
      make_label(item, tr("다시 검색", "Rescan"), &ui_font_sm, TEXT_SECONDARY);
    }
  }
}

// Keyboard view: password entry with the arrow keys. Rows 0-3 are the
// character grid of the current page, row 4 the special keys.
const char *const kKbPages[3][4] = {
    {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-_."},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-_."},
    {"!@#$%^&*()", "`~-_=+[]{}", "\\|;:'\",<>", "./?"},
};
constexpr int kKbRows = 5;
constexpr int kKbSpecial = 4; // page / space / delete / connect
constexpr size_t kKbMaxLen = 63;

int kb_row_len(int row) { return row < 4 ? (int)strlen(kKbPages[s_kbPage][row]) : kKbSpecial; }

lv_obj_t *make_key(lv_obj_t *parent, const char *text, const lv_font_t *font, bool selected, lv_coord_t w,
                   uint32_t text_color) {
  lv_obj_t *key = make_card(parent, CARD_BG);
  lv_obj_set_size(key, w, 34);
  lv_obj_set_style_radius(key, 8, 0);
  lv_obj_set_flex_align(key, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (selected) {
    lv_obj_set_style_bg_color(key, c(PILL_BG), 0);
    lv_obj_set_style_shadow_width(key, 12, 0);
    lv_obj_set_style_shadow_color(key, c(PILL_BG), 0);
    lv_obj_set_style_shadow_opa(key, LV_OPA_40, 0);
  }
  make_label(key, text, font, selected ? PILL_TEXT : text_color);
  return key;
}

void build_wifi_keyboard(lv_obj_t *parent) {
  char title[64];
  snprintf(title, sizeof(title), "'%s' %s", s_kbSsid, tr("비밀번호", "password"));
  lv_obj_t *t = make_label(parent, title, &ui_font_sm, TEXT_SECONDARY);
  lv_obj_set_width(t, LV_PCT(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);

  lv_obj_t *field = make_card(parent, CARD_BG_ALT);
  lv_obj_set_size(field, LV_PCT(100), 40);
  lv_obj_set_flex_grow(field, 0);
  lv_obj_set_style_pad_hor(field, 12, 0);
  lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  std::string shown = s_kbText + "_";
  lv_obj_t *text = make_label(field, shown.c_str(), &ui_font_md, TEXT_PRIMARY);
  lv_obj_set_flex_grow(text, 1);
  lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
  if (!s_kbText.empty() && s_kbText.size() < 8) {
    make_label(field, tr("8자 이상", "min 8 chars"), &ui_font_xs, WARN);
  }

  lv_obj_t *grid = make_plain(parent);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, 5, 0);
  for (int r = 0; r < 4; r++) {
    lv_obj_t *row = make_row(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 5);
    const char *chars = kKbPages[s_kbPage][r];
    for (int i = 0; chars[i]; i++) {
      char ch[2] = {chars[i], 0};
      make_key(row, ch, &ui_font_sm, r == s_kbRow && i == s_kbCol, 44, TEXT_PRIMARY);
    }
  }
  static const char *kNextPage[3] = {"ABC", "#+=", "abc"};
  lv_obj_t *special = make_row(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 5);
  const bool onSpecial = s_kbRow == 4;
  make_key(special, kNextPage[s_kbPage], &ui_font_sm, onSpecial && s_kbCol == 0, 80, TEXT_SECONDARY);
  make_key(special, tr("공백", "Space"), &ui_font_sm, onSpecial && s_kbCol == 1, 140, TEXT_SECONDARY);
  make_key(special, LV_SYMBOL_BACKSPACE, &lv_font_montserrat_20, onSpecial && s_kbCol == 2, 80, TEXT_SECONDARY);
  make_key(special, tr("연결", "Connect"), &ui_font_sm, onSpecial && s_kbCol == 3, 140, ACCENT_CYAN);
}

void build_wifi_panel(lv_obj_t *parent) {
  switch (s_in_panel ? s_wifiView : WifiView::Main) {
    case WifiView::Qr:
      build_wifi_qr(parent);
      break;
    case WifiView::Scan:
      build_wifi_scan(parent);
      break;
    case WifiView::Keyboard:
      build_wifi_keyboard(parent);
      break;
    default:
      build_wifi_main(parent);
      break;
  }
}

// ---- Wi-Fi panel keys ----------------------------------------------------

enum class Key { Up, Down, Left, Right, Ok, Back };

bool wifi_panel_active() { return s_in_panel && s_mode == Mode::Outer && s_selected == 2; }

void wifi_main_ok() {
  WifiController &wifi = WifiController::instance();
  if (s_panel_row == 0) {
    wifi.startSetup();
    s_wifiView = WifiView::Qr;
  } else if (s_panel_row == 1) {
    wifi.startScan();
    s_wifiSel = 0;
    s_wifiView = WifiView::Scan;
  } else {
    wifi.forget();
  }
}

void wifi_scan_ok() {
  if (s_wifiSel >= s_wifi.apCount) {
    WifiController::instance().startScan();
    return;
  }
  const WIFI::ApInfo &ap = s_wifi.aps[s_wifiSel];
  if (!ap.secure) {
    WifiController::instance().connect(ap.ssid, "");
    s_wifiView = WifiView::Main;
    return;
  }
  snprintf(s_kbSsid, sizeof(s_kbSsid), "%s", ap.ssid);
  s_kbText.clear();
  s_kbPage = 0;
  s_kbRow = 1;
  s_kbCol = 0;
  s_wifiView = WifiView::Keyboard;
}

void wifi_keyboard_ok() {
  if (s_kbRow < 4) {
    if (s_kbText.size() < kKbMaxLen) s_kbText += kKbPages[s_kbPage][s_kbRow][s_kbCol];
    return;
  }
  switch (s_kbCol) {
    case 0:
      s_kbPage = (s_kbPage + 1) % 3;
      break;
    case 1:
      if (s_kbText.size() < kKbMaxLen) s_kbText += ' ';
      break;
    case 2:
      if (!s_kbText.empty()) s_kbText.pop_back();
      break;
    default:
      // WPA needs 8+ characters; an empty password is an open network.
      if (s_kbText.empty() || s_kbText.size() >= 8) {
        WifiController::instance().connect(s_kbSsid, s_kbText);
        s_kbText.clear();
        s_wifiView = WifiView::Main;
      }
      break;
  }
}

// Handles a key in the Wi-Fi panel. Returns false if the generic panel
// handling should run instead (main view: row moves, Back leaves).
bool wifi_key(Key k) {
  if (!wifi_panel_active()) return false;
  switch (s_wifiView) {
    case WifiView::Main:
      if (k != Key::Ok) return false;
      wifi_main_ok();
      break;
    case WifiView::Qr:
      if (k == Key::Back) {
        WifiController::instance().stopSetup();
        s_wifiView = WifiView::Main;
      }
      break;
    case WifiView::Scan: {
      const int total = s_wifi.apCount + 1;
      if (k == Key::Up || k == Key::Left) s_wifiSel = wrap(s_wifiSel - 1, total);
      else if (k == Key::Down || k == Key::Right) s_wifiSel = wrap(s_wifiSel + 1, total);
      else if (k == Key::Ok) wifi_scan_ok();
      else s_wifiView = WifiView::Main;
      break;
    }
    case WifiView::Keyboard:
      if (k == Key::Up && s_kbRow > 0) s_kbRow--;
      else if (k == Key::Down && s_kbRow < kKbRows - 1) s_kbRow++;
      else if (k == Key::Left) s_kbCol = wrap(s_kbCol - 1, kb_row_len(s_kbRow));
      else if (k == Key::Right) s_kbCol = wrap(s_kbCol + 1, kb_row_len(s_kbRow));
      else if (k == Key::Ok) wifi_keyboard_ok();
      else if (k == Key::Back) s_wifiView = WifiView::Scan;
      if (s_kbCol >= kb_row_len(s_kbRow)) s_kbCol = kb_row_len(s_kbRow) - 1;
      break;
  }
  refresh_content();
  return true;
}

// ---- Clock (user sets RTC date + time) --------------------------------

int days_in_month(int y, int m) {
  static const int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  if (m < 1 || m > 12) return 31;
  return kDays[m];
}

// ---- Pressure-mode setup wizard -----------------------------------------

const char *count_unit(const RC::PressureRoom &r) {
  return r.countKind == RC::COUNT_TOILETS ? tr("개", "toilets") : tr("명", "people");
}

void wizard_apply() {
  RC::PressureSetup r = {};
  const uint8_t kind = s_wiz.kind;
  const bool pos = kind == RC::PRESS_POSITIVE;
  // Starts on the air path picked on the page (전열 / 바이패스).
  const bool bpPath = s_data.ahu_mode == RC::UI_BYPASS;
  const bool started =
      RcController::instance().applyPressureSetup(kind, (uint8_t)s_wiz.room, (uint8_t)s_wiz.count, bpPath, &r);
  if (kind == RC::PRESS_NEGATIVE && !s_rc.gasWarned) RcController::instance().markGasWarned();
  s_wiz.open = false;
  if (started) {
    s_data.ahu_mode = s_snap.ahu_mode = bpPath ? RC::UI_BYPASS : RC::UI_VENTILATION;
    s_data.vent_sub = s_snap.vent_sub = pos ? 1 : 2;
    snprintf(s_ahuResultBuf, sizeof(s_ahuResultBuf), "%s",
             pos ? tr("양압 모드 시작 - 설정 저장됨", "Positive mode on - setpoints saved")
                 : tr("음압 모드 시작 - 설정 저장됨", "Negative mode on - setpoints saved"));
  } else {
    snprintf(s_ahuResultBuf, sizeof(s_ahuResultBuf), "%s",
             tr("설정 저장됨 - 전원 Off/잠금이라 시작 안 함", "Saved - not started (power off or locked)"));
  }
  s_ahuResultMsg = s_ahuResultBuf;
  refresh_content();
}

// Wizard keys while it is open on the Airflow panel. Returns true if used.
bool wizard_key(Key k) {
  if (!(s_wiz.open && s_in_panel && s_mode == Mode::Outer && s_selected == 0)) return false;
  int n = 0;
  const RC::PressureRoom *rooms = RC::pressureRooms(s_wiz.kind, n);
  const RC::PressureRoom &rt = rooms[s_wiz.room];
  int delta = 0;
  switch (k) {
    case Key::Left: delta = -1; break;
    case Key::Right: delta = 1; break;
    case Key::Up: delta = s_wiz.step == 1 ? 1 : 0; break;
    case Key::Down: delta = s_wiz.step == 1 ? -1 : 0; break;
    case Key::Ok:
      if (s_wiz.step < 2) s_wiz.step++;
      else {
        wizard_apply();
        return true;
      }
      break;
    case Key::Back:
      if (s_wiz.step > 0) s_wiz.step--;
      else s_wiz.open = false;  // closed, nothing changed on the unit
      break;
  }
  if (delta && s_wiz.step == 0) {
    s_wiz.room = wrap(s_wiz.room + delta, n);
    s_wiz.count = rooms[s_wiz.room].defCount;  // people <-> toilets: start from that room's default
  } else if (delta && s_wiz.step == 1) {
    s_wiz.count += delta;
    if (s_wiz.count < 1) s_wiz.count = 1;
    if (s_wiz.count > rt.maxCount) s_wiz.count = rt.maxCount;
  }
  refresh_content();
  return true;
}

void build_pressure_wizard(lv_obj_t *parent) {
  const uint8_t kind = s_wiz.kind;
  const bool pos = kind == RC::PRESS_POSITIVE;
  int n = 0;
  const RC::PressureRoom *rooms = RC::pressureRooms(kind, n);
  const RC::PressureRoom &rt = rooms[s_wiz.room];
  const char *modeName = pos ? tr("양압 모드", "Positive mode") : tr("음압 모드", "Negative mode");
  char buf[160];

  // Header: icon, "양압 모드 설정", step n/3
  lv_obj_t *head = make_row(parent, LV_PCT(100), 36, 10);
  lv_obj_set_flex_grow(head, 0);
  if (pos) ui_icons::pressure_out(head, 30, c(ACCENT_CYAN));
  else ui_icons::pressure_in(head, 30, c(ACCENT_CYAN));
  snprintf(buf, sizeof(buf), "%s %s", modeName, tr("설정", "setup"));
  lv_obj_t *title = make_label(head, buf, &ui_font_md, TEXT_PRIMARY);
  lv_obj_set_flex_grow(title, 1);
  snprintf(buf, sizeof(buf), "%d / 3", s_wiz.step + 1);
  make_label(head, buf, &ui_font_xs, TEXT_DIM);

  static const char *stepKo[3] = {"공간 종류를 고르세요", "", "결과를 확인하고 적용하세요"};
  static const char *stepEn[3] = {"Choose the room type", "", "Check the result, then apply"};
  if (s_wiz.step == 1) {
    snprintf(buf, sizeof(buf), "%s", rt.countKind == RC::COUNT_TOILETS ? tr("변기 수를 정하세요", "Set the number of toilets")
                                                                      : tr("인원 수를 정하세요", "Set the number of people"));
  } else {
    snprintf(buf, sizeof(buf), "%s", tr(stepKo[s_wiz.step], stepEn[s_wiz.step]));
  }
  lv_obj_t *hint = make_label(parent, buf, &ui_font_xs, TEXT_SECONDARY);
  lv_obj_set_flex_grow(hint, 0);

  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 8, 0);
  apply_focus_frame(card, true);

  if (s_wiz.step == 0) {
    lv_obj_t *opts = make_row(card, LV_PCT(100), 64, 8);
    for (int i = 0; i < n; i++) make_mode_option(opts, tr(rooms[i].ko, rooms[i].en), "", i == s_wiz.room);
    snprintf(buf, sizeof(buf), "%s %u m³/h · %s %c%u%%",
             rt.countKind == RC::COUNT_TOILETS ? tr("변기 1개당", "Per toilet") : tr("1인당", "Per person"),
             (unsigned)rt.perUnit, modeName, pos ? '+' : '-', (unsigned)rt.offsetPct);
    make_label(card, buf, &ui_font_xs, TEXT_DIM);
  } else if (s_wiz.step == 1) {
    lv_obj_t *stepper = make_row(card, LV_PCT(100), 80, 0);
    lv_obj_set_flex_align(stepper, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(stepper, 24, 0);
    make_step_key(stepper, "-", s_wiz.count > 1);
    lv_obj_t *value = make_row(stepper, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 10);
    lv_obj_set_flex_align(value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    snprintf(buf, sizeof(buf), "%d", s_wiz.count);
    make_label(value, buf, &ui_font_xxl, TEXT_PRIMARY);
    make_label(value, count_unit(rt), &ui_font_md, TEXT_SECONDARY);
    make_step_key(stepper, "+", s_wiz.count < rt.maxCount);
    make_label(card, tr(rt.ko, rt.en), &ui_font_xs, TEXT_DIM);
  } else {
    // Preview only - the unit changes when Apply is pressed.
    const RC::PressureSetup r = RC::pressureCompute(kind, (uint8_t)s_wiz.room, (uint8_t)s_wiz.count);
    snprintf(buf, sizeof(buf), "%s · %d%s", tr(rt.ko, rt.en), s_wiz.count, count_unit(rt));
    make_label(card, buf, &ui_font_xs, TEXT_DIM);
    snprintf(buf, sizeof(buf), "%s %u m³/h · %s %u m³/h", tr("급기", "Supply"), (unsigned)r.supply,
             tr("배기", "Exhaust"), (unsigned)r.exhaust);
    make_label(card, buf, &ui_font_md, TEXT_PRIMARY);
    snprintf(buf, sizeof(buf), "%s %c%u%%", modeName, pos ? '+' : '-', (unsigned)r.offsetPct);
    make_label(card, buf, &ui_font_sm, ACCENT_CYAN);
    if (!r.capped && (uint32_t)s_wiz.count * rt.perUnit < RC_PRESS_MIN_M3H) {
      snprintf(buf, sizeof(buf), tr("최소 풍량 %d m³/h 적용", "Minimum airflow %d m³/h applied"), RC_PRESS_MIN_M3H);
      make_label(card, buf, &ui_font_xs, TEXT_DIM);
    }
    // Warnings: one plain sentence each - what is happening and what to do.
    if (r.capped) {
      snprintf(buf, sizeof(buf),
               tr("필요 풍량 %u m³/h가 정격 %u m³/h보다 커서 %u m³/h로 제한되니, 더 큰 장치를 검토하세요.",
                  "Unit too small: it needs %u m³/h but is rated %u m³/h, so it runs at %u m³/h - consider a larger unit."),
               (unsigned)r.needed, (unsigned)RC_FAN_RATED_M3H, (unsigned)RC_FAN_RATED_M3H);
      lv_obj_t *w = make_wrap_label(card, buf, &ui_font_xs, WARN);
      lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (!pos && !s_rc.gasWarned) {
      lv_obj_t *w = make_wrap_label(card,
                                    tr("음압 모드는 가스기기의 배기가스를 실내로 끌어들일 수 있으니, 가스기기를 쓸 때는 창문을 조금 열어 두세요.",
                                       "Negative mode can pull fumes back from gas appliances, so open a window slightly while one is in use."),
                                    &ui_font_xs, WARN);
      lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *apply_row = make_row(card, 140, 40, 0);
    make_mode_option(apply_row, tr("적용", "Apply"), "", true);
  }
}

void clamp_clock_day() {
  const int maxd = days_in_month(s_data.clock_y, s_data.clock_mo);
  if (s_data.clock_d > maxd) s_data.clock_d = maxd;
  if (s_data.clock_d < 1) s_data.clock_d = 1;
}

void make_clock_field(lv_obj_t *parent, int row, const char *text) {
  const bool on = s_in_panel && s_panel_row == row;
  lv_obj_t *box = make_plain(parent);
  lv_obj_set_height(box, 80);
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_style_radius(box, 14, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, c(on ? PILL_BG : CARD_BG_ALT), 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  apply_focus_frame(box, on);
  make_label(box, text, &ui_font_xxl, on ? PILL_TEXT : TEXT_PRIMARY);
}

void build_clock_panel(lv_obj_t *parent) {
  const int y = s_in_panel ? s_data.clock_y : (s_clockValid ? s_clockY : 2026);
  const int mo = s_in_panel ? s_data.clock_mo : (s_clockValid ? s_clockMo : 1);
  const int d = s_in_panel ? s_data.clock_d : (s_clockValid ? s_clockD : 1);
  const int h = s_in_panel ? s_data.clock_h : (s_clockValid ? s_clockH : 0);
  const int mi = s_in_panel ? s_data.clock_mi : (s_clockValid ? s_clockM : 0);

  char yb[8], mb[8], db[8], hb[8], mib[8];
  snprintf(yb, sizeof(yb), "%d", y);
  snprintf(mb, sizeof(mb), "%02d", mo);
  snprintf(db, sizeof(db), "%02d", d);
  snprintf(hb, sizeof(hb), "%02d", h);
  snprintf(mib, sizeof(mib), "%02d", mi);

  lv_obj_t *date_card = make_card(parent, CARD_BG);
  lv_obj_set_size(date_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(date_card, 0);
  lv_obj_set_style_pad_all(date_card, 16, 0);
  lv_obj_set_flex_flow(date_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(date_card, 12, 0);
  make_label(date_card, tr("일자", "Date"), &ui_font_sm, TEXT_SECONDARY);
  lv_obj_t *date_row = make_row(date_card, LV_PCT(100), 80, 8);
  make_clock_field(date_row, 0, yb);
  make_label(date_row, ".", &ui_font_xxl, TEXT_DIM);
  make_clock_field(date_row, 1, mb);
  make_label(date_row, ".", &ui_font_xxl, TEXT_DIM);
  make_clock_field(date_row, 2, db);

  lv_obj_t *time_card = make_card(parent, CARD_BG);
  lv_obj_set_size(time_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(time_card, 0);
  lv_obj_set_style_pad_all(time_card, 16, 0);
  lv_obj_set_flex_flow(time_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(time_card, 12, 0);
  make_label(time_card, tr("시간", "Time"), &ui_font_sm, TEXT_SECONDARY);
  lv_obj_t *time_row = make_row(time_card, LV_PCT(100), 80, 8);
  make_clock_field(time_row, 3, hb);
  make_label(time_row, ":", &ui_font_xxl, TEXT_DIM);
  make_clock_field(time_row, 4, mib);
  make_label(time_row, h < 12 ? "AM" : "PM", &ui_font_md, TEXT_SECONDARY);

  make_label(parent, tr("* ▲▼ 변경  ◀▶ 이동  OK 저장", "* Up/Down change, Left/Right move, OK save"),
             &ui_font_xs, TEXT_DIM);
}

// ---- Language section ---------------------------------------------------
// Left/Right picks 한국어 / English; OK saves it, and main.cpp rebuilds the
// screens in the new language (see take_language_changed()).

void build_language_panel(lv_obj_t *parent) {
  const int lang = s_in_panel ? s_data.lang : (int)ui_lang::get();

  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(card, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 12, 0);
  make_label(card, tr("화면 언어", "Display Language"), &ui_font_sm, TEXT_SECONDARY);

  lv_obj_t *row = make_row(card, LV_PCT(100), 80, 10);
  apply_focus_frame(row, true);
  make_mode_option(row, "한국어", "", lang == (int)ui_lang::Lang::Ko);
  make_mode_option(row, "English", "", lang == (int)ui_lang::Lang::En);

  make_label(parent, tr("* OK = 저장 후 바로 적용", "* Applied when you press OK"), &ui_font_xs,
             TEXT_DIM);
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

void make_pin_boxes(lv_obj_t *parent, const int digits[4], int pos, bool reveal) {
  lv_obj_t *row = make_row(parent, LV_PCT(80), 80, 10);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 4; i++) {
    const bool on = reveal && pos == i;
    lv_obj_t *box = make_plain(row);
    lv_obj_set_size(box, 56, 72);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, c(on ? PILL_BG : CARD_BG_ALT), 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    apply_focus_frame(box, on);
    char d[2] = {reveal ? (char)('0' + digits[i]) : '*', 0};
    make_label(box, d, &ui_font_xxl, on ? PILL_TEXT : TEXT_PRIMARY);
  }
}

// Shown in the OUTER content panel when "기기 설정" is highlighted but not
// yet entered - OK drills into its own nested sidebar (see enter_engineer).
void build_engineer_portal_panel(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 12, 0);

  ui_icons::lock(card, 28, c(TEXT_SECONDARY));
  make_label(card, tr("기기 설정", "Engineer Mode"), &ui_font_sm, TEXT_PRIMARY);

  if (!s_pinOpen) {
    make_label(card, tr("OK = 4자리 PIN", "OK = 4-digit PIN"), &ui_font_xs, TEXT_DIM);
    return;
  }

  make_label(card, "PIN", &ui_font_sm, TEXT_SECONDARY);
  make_pin_boxes(card, s_pinDigit, s_pinPos, true);
  if (s_pinWrong) make_label(card, tr("PIN 실패", "Wrong PIN"), &ui_font_sm, DANGER);
  else make_label(card, tr("* ▲▼ 변경  ◀▶ 이동  OK 확인", "* Up/Down change, Left/Right move, OK"),
                  &ui_font_xs, TEXT_DIM);
}

void build_engineer_pin_panel(lv_obj_t *parent) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 12, 0);

  ui_icons::lock(card, 28, c(TEXT_SECONDARY));
  make_label(card, tr("PIN 변경", "Change PIN"), &ui_font_sm, TEXT_PRIMARY);

  int digits[4] = {};
  digits_from_pin(s_in_panel ? s_data.pin : s_savedPin, digits);
  make_pin_boxes(card, digits, s_panel_row, s_in_panel);

  if (s_in_panel) {
    make_label(card, tr("* ▲▼ 변경  ◀▶ 이동  OK 저장", "* Up/Down change, Left/Right move, OK save"),
               &ui_font_xs, TEXT_DIM);
  } else {
    make_label(card, tr("OK = PIN 변경", "OK = change PIN"), &ui_font_xs, TEXT_DIM);
  }
}

// "ID 설정 (RS-485 ID)" - fully designed, per the reference mockup.
void build_engineer_id_panel(lv_obj_t *parent) {
  lv_obj_t *current = make_card(parent, CARD_BG);
  lv_obj_set_size(current, LV_PCT(100), 110);
  lv_obj_set_flex_grow(current, 0);
  lv_obj_set_flex_flow(current, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(current, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(current, 4, 0);
  char current_buf[40];
  const char *cur = tr("현재 ID", "Current ID");
  if (s_rc.deviceId == 0) {
    snprintf(current_buf, sizeof(current_buf), "%s : 00 (%s)", cur, tr("기본값", "Default"));
  } else {
    snprintf(current_buf, sizeof(current_buf), "%s : %d (0x%02X)", cur, s_rc.deviceId, s_rc.deviceId);
  }
  make_label(current, current_buf, &ui_font_xxl, TEXT_PRIMARY);

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
  snprintf(id_buf, sizeof(id_buf), "[ %d ]", s_data.eng_id);
  make_label(idRow, id_buf, &ui_font_md, TEXT_PRIMARY);

  lv_obj_t *plus = make_card(row, CARD_BG_ALT);
  lv_obj_set_size(plus, 40, 40);
  lv_obj_set_flex_align(plus, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_label(plus, "+", &ui_font_md, TEXT_PRIMARY);

  make_label(stepper, tr("(설정 범위: 1 ~ 99, OK = 저장)", "(Range: 1 ~ 99, OK = Save)"), &ui_font_xs, TEXT_DIM);

  make_label(parent, "* PLC B5 ID byte = same number (98 -> 0x62). Must match MAIN.", &ui_font_xs, TEXT_DIM);
  make_label(parent, tr("* PC (RS-485) 도 같은 ID 사용 - PC 는 1~32 만 지원", "* PC (RS-485) uses the same ID - PC supports 1~32 only"),
             &ui_font_xs, s_data.eng_id > RS485_ID_DOC_MAX && s_in_panel ? WARN : TEXT_DIM);
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

// "[ 강 : 2.45 V ]" from a value in 0.05 V units (V x 20).
void format_vsp(char *buf, size_t n, const char *level, int units) {
  const int cv = units * 5;
  snprintf(buf, n, "[ %s : %d.%02d V ]", level, cv / 100, cv % 100);
}

// VSP buffer in old RC order (see SettingsData::vsp).
void vsp_from_state(int out[12]) {
  const uint8_t v[12] = {s_rc.vspExhaustL,   s_rc.vspExhaustM,   s_rc.vspExhaustH,
                         s_rc.vspSupplyL,    s_rc.vspSupplyM,    s_rc.vspSupplyH,
                         s_rc.vspBpExhaustL, s_rc.vspBpExhaustM, s_rc.vspBpExhaustH,
                         s_rc.vspBpSupplyL,  s_rc.vspBpSupplyM,  s_rc.vspBpSupplyH};
  for (int i = 0; i < 12; i++) out[i] = v[i];
}

// Volts string for the controller ("2.45").
std::string vsp_volts(int units) {
  char b[24];
  const int cv = units * 5;
  snprintf(b, sizeof(b), "%d.%02d", cv / 100, cv % 100);
  return b;
}

// Old RC VSP setting (SHR-L2 3.1): Left/Right steps through
// [전열 배기] 약 중 강 -> [전열 급기] -> [바이패스 배기] -> [바이패스 급기],
// Up/Down changes the selected value by 0.05 V, OK saves, Back cancels.
void build_engineer_vsp_panel(lv_obj_t *parent) {
  int v[12];
  if (s_in_panel) {
    for (int i = 0; i < 12; i++) v[i] = s_data.vsp[i];
  } else {
    vsp_from_state(v);
  }
  const int slot = s_in_panel ? s_data.vsp_slot : -1;
  const bool bypass = slot >= 6;
  const int exhGroup = bypass ? 2 : 0;
  const int supGroup = bypass ? 3 : 1;
  const char *tag = bypass ? tr(" [바이패스]", " [Bypass]") : tr(" [전열]", " [Heat Recovery]");
  // selected_idx in build_fan_voltage_card: 0 = 강, 1 = 중, 2 = 약
  const int supSel = (slot >= 0 && slot / 3 == supGroup) ? 2 - slot % 3 : -1;
  const int exhSel = (slot >= 0 && slot / 3 == exhGroup) ? 2 - slot % 3 : -1;
  const char *hi = tr("강", "High");
  const char *mid = tr("중", "Mid");
  const char *lo = tr("약", "Low");

  char title[48], h[28], m[28], l[28];
  snprintf(title, sizeof(title), "%s%s", tr("급기", "Supply Air"), tag);
  make_label(parent, title, &ui_font_md, TEXT_PRIMARY);
  format_vsp(h, sizeof(h), hi, v[supGroup * 3 + 2]);
  format_vsp(m, sizeof(m), mid, v[supGroup * 3 + 1]);
  format_vsp(l, sizeof(l), lo, v[supGroup * 3 + 0]);
  build_fan_voltage_card(parent, tr("급기 팬", "Supply Fan"), supSel, h, m, l, supSel >= 0, supSel >= 0);

  snprintf(title, sizeof(title), "%s%s", tr("배기", "Exhaust Air"), tag);
  make_label(parent, title, &ui_font_md, TEXT_PRIMARY);
  format_vsp(h, sizeof(h), hi, v[exhGroup * 3 + 2]);
  format_vsp(m, sizeof(m), mid, v[exhGroup * 3 + 1]);
  format_vsp(l, sizeof(l), lo, v[exhGroup * 3 + 0]);
  build_fan_voltage_card(parent, tr("배기 팬", "Exhaust Fan"), exhSel, h, m, l, exhSel >= 0,
                         exhSel >= 0);
}

void restyle_eng_menu_item(int idx) {
  bool selected = (idx == s_engSelected);
  lv_obj_set_style_border_color(s_engMenuCards[idx], c(selected ? ACCENT_CYAN : BORDER), 0);
  lv_obj_set_style_border_width(s_engMenuCards[idx], selected ? 2 : 1, 0);
  lv_obj_set_style_text_color(s_engMenuTitles[idx], c(selected ? ACCENT_CYAN : TEXT_PRIMARY), 0);
  if (s_engMenuSubtitles[idx]) {
    lv_obj_set_style_text_color(s_engMenuSubtitles[idx], c(selected ? ACCENT_CYAN : TEXT_SECONDARY), 0);
  }
}

lv_obj_t *build_eng_menu_item(lv_obj_t *parent, int idx) {
  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  s_engMenuCards[idx] = card;

  s_engMenuTitles[idx] = make_label(card, eng_menu_title(idx), &ui_font_sm, TEXT_PRIMARY);
  s_engMenuSubtitles[idx] = nullptr;

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
    case 2:
      build_engineer_pin_panel(s_contentPanel);
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
      build_filter_panel(s_contentPanel);
      set_footer();
      break;
    case 4:
      build_clock_panel(s_contentPanel);
      set_footer();
      break;
    case 5:
      build_language_panel(s_contentPanel);
      set_footer();
      break;
    case 6:
      build_engineer_portal_panel(s_contentPanel);
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
  if (s_menuSubtitles[idx]) lv_obj_set_style_text_color(s_menuSubtitles[idx], c(subColor), 0);
}

lv_obj_t *build_menu_item(lv_obj_t *parent, int idx) {
  const MenuItem &item = kMenu[idx];

  lv_obj_t *card = make_card(parent, CARD_BG);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(card, 1);
  // 7 items share the sidebar height, so keep the vertical padding small.
  lv_obj_set_style_pad_hor(card, 14, 0);
  lv_obj_set_style_pad_ver(card, 2, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  s_menuCards[idx] = card;

  lv_obj_t *titleRow = make_row(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 6);
  s_menuTitles[idx] = make_label(titleRow, menu_title(idx), &ui_font_sm, TEXT_PRIMARY);
  if (item.locked) ui_icons::lock(titleRow, 16, c(TEXT_SECONDARY));
  s_menuSubtitles[idx] = nullptr;

  restyle_menu_item(idx);
  return card;
}

void build_sidebar_outer(lv_obj_t *parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 6, 0);
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
  if (s_breadcrumbRoot) {
    lv_label_set_text(s_breadcrumbRoot, eng ? tr("기기설정", "ENGINEER MODE")
                                            : tr("설정", "SETTINGS"));
  }
  if (s_breadcrumbSection) {
    lv_label_set_text(s_breadcrumbSection, eng ? eng_menu_title(s_engSelected) : menu_title(s_selected));
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
  s_breadcrumbRoot = make_label(left, tr("설정", "SETTINGS"), &ui_font_sm, TEXT_PRIMARY);
  make_label(left, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, TEXT_DIM);
  s_breadcrumbSection = make_label(left, menu_title(s_selected), &ui_font_sm, TEXT_SECONDARY);

  lv_obj_t *right = make_row(bar, LV_SIZE_CONTENT, LV_PCT(100), 8);
  s_breadcrumbLock = ui_icons::lock(right, 18, c(TEXT_SECONDARY));
  lv_obj_add_flag(s_breadcrumbLock, LV_OBJ_FLAG_HIDDEN); // shown only in Engineer Mode
  s_breadcrumbTime = make_label(right, "--", &ui_font_sm, TEXT_SECONDARY);
  s_breadcrumbWifi = ui_icons::wifi(right, 20, c(TEXT_PRIMARY));
  lv_obj_set_style_opa(s_breadcrumbWifi, s_wifi.connected() ? LV_OPA_COVER : LV_OPA_40, 0);
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
  if (s_in_panel && s_mode == Mode::Outer && s_selected == 0 && s_wiz.open) {
    footer_add_ok(s_wiz.step == 2 ? tr("적용", "Apply") : tr("다음", "Next"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, tr("변경", "Change"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, s_wiz.step == 0 ? tr("닫기", "Close") : tr("뒤로", "Back"));
  } else if (s_in_panel && s_mode == Mode::Outer && s_selected == 1) {
    footer_add_ok(tr("예약 시작", "Start"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, tr("변경", "Change"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("이동", "Move"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("취소", "Cancel"));
  } else if (s_in_panel && s_mode == Mode::Outer && s_selected == 2) {
    switch (s_wifiView) {
      case WifiView::Qr:
        footer_add_icons(LV_SYMBOL_NEW_LINE, tr("설정 종료", "Exit setup"));
        break;
      case WifiView::Scan:
        footer_add_ok(tr("선택", "Select"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("이동", "Move"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_NEW_LINE, tr("뒤로", "Back"));
        break;
      case WifiView::Keyboard:
        footer_add_ok(tr("입력", "Type"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_DOWN LV_SYMBOL_RIGHT, tr("이동", "Move"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_NEW_LINE, tr("뒤로", "Back"));
        break;
      default:
        footer_add_ok(tr("실행", "Run"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_RIGHT, tr("선택", "Select"));
        footer_add_sep();
        footer_add_icons(LV_SYMBOL_NEW_LINE, tr("뒤로", "Back"));
        break;
    }
  } else if (s_in_panel && s_mode == Mode::Engineer && s_engSelected == 2) {
    footer_add_ok(tr("저장", "Save"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("변경", "Change"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, tr("이동", "Move"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("취소", "Cancel"));
  } else if (s_in_panel) {
    footer_add_ok(tr("저장", "Save"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_UP LV_SYMBOL_RIGHT, tr("선택/이동", "Select/Move"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("취소", "Cancel"));
  } else if (s_mode == Mode::Outer && s_selected == 6 && s_pinOpen) {
    footer_add_ok(tr("확인", "OK"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("변경", "Change"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, tr("이동", "Move"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("취소", "Cancel"));
  } else if (s_mode == Mode::Outer && s_selected == 6) {
    footer_add_ok(tr("PIN 입력", "Enter PIN"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("메뉴", "Menu"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("나가기", "Exit"));
  } else {
    footer_add_ok(tr("선택", "Enter"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_UP LV_SYMBOL_DOWN, tr("메뉴", "Menu"));
    footer_add_sep();
    footer_add_icons(LV_SYMBOL_NEW_LINE, tr("나가기", "Exit"));
  }
}

} // namespace

lv_obj_t *create() {
  load_pin();
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

  s_screen = scr;
  return scr;
}

void set_state(const RcDto &state) {
  s_rc = state;
  // The Wi-Fi page shows no RC state; skip it so the QR code / keyboard are
  // not rebuilt on every PLC frame.
  const bool wifiPage = s_mode == Mode::Outer && s_selected == 2;
  if (s_screen && lv_scr_act() == s_screen && !wifiPage) refresh_content();
}

void select_menu(int idx) {
  if (s_mode != Mode::Outer || s_in_panel) return;
  if (idx < 0 || idx >= kMenuCount || idx == s_selected) return;
  if (s_pinOpen) pin_reset();
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
  if (s_mode != Mode::Outer || s_selected != 6) return; // only from the Engineer Mode portal
  pin_reset();
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
  pin_reset();

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
  s_wifiView = WifiView::Main;
  // Seed the local pick from the current RC state.
  if (s_mode == Mode::Outer && s_selected == 0) {
    // 양압 / 음압 are 전열 / 바이패스 sub-types on this page.
    const RC::UiMode um = s_rc.uiMode();
    if (um == RC::UI_POSITIVE || um == RC::UI_NEGATIVE) s_data.ahu_mode = RC::UI_VENTILATION;
    else if (um == RC::UI_BP_POSITIVE || um == RC::UI_BP_NEGATIVE) s_data.ahu_mode = RC::UI_BYPASS;
    else s_data.ahu_mode = um;
    s_data.vent_sub = (um == RC::UI_POSITIVE || um == RC::UI_BP_POSITIVE) ? 1
                      : ((um == RC::UI_NEGATIVE || um == RC::UI_BP_NEGATIVE) ? 2 : 0);
    s_data.ahu_volume = s_rc.volume == 0 ? 1 : s_rc.volume;
    s_data.press_kind = s_rc.pressure == RC::PRESS_NEGATIVE ? RC::PRESS_NEGATIVE : RC::PRESS_POSITIVE;
    s_wiz.open = false;
  }
  if (s_mode == Mode::Outer && s_selected == 1) {
    s_data.resv_type = 0;
    s_data.resv_min = s_rc.startMin >= RC_RESERVE_STEP_MIN ? s_rc.startMin : 60;
  }
  if (s_mode == Mode::Outer && s_selected == 3) {
    s_data.filter_init = s_rc.filterInit;
  }
  if (s_mode == Mode::Outer && s_selected == 4) {
    if (sensors::hasRtc()) {
      s_data.clock_y = sensors::getYear();
      s_data.clock_mo = sensors::getMonth();
      s_data.clock_d = sensors::getDay();
      s_data.clock_h = sensors::getHour24();
      s_data.clock_mi = sensors::getMinute();
    }
    if (s_data.clock_y < 2020 || s_data.clock_y > 2099) s_data.clock_y = 2026;
    if (s_data.clock_mo < 1 || s_data.clock_mo > 12) s_data.clock_mo = 1;
    clamp_clock_day();
  }
  if (s_mode == Mode::Outer && s_selected == 5) {
    s_data.lang = (int)ui_lang::get();
  }
  if (s_mode == Mode::Engineer && s_engSelected == 0) {
    s_data.eng_id = s_rc.deviceId == 0 ? 1 : s_rc.deviceId;
  }
  if (s_mode == Mode::Engineer && s_engSelected == 1) {
    vsp_from_state(s_data.vsp);
    s_data.vsp_slot = 0;
  }
  if (s_mode == Mode::Engineer && s_engSelected == 2) {
    s_data.pin = s_savedPin;
  }
  s_ahuResultMsg = nullptr;
  snapshot_data();
  s_in_panel = true;
  s_panel_row = 0;
  refresh_content();
}

void leave_panel(bool save) {
  if (!s_in_panel) return;
  if (save) {
    if (s_mode == Mode::Outer && s_selected == 4) {
      sensors::setRtcTime(s_data.clock_y, s_data.clock_mo, s_data.clock_d, s_data.clock_h,
                          s_data.clock_mi, 0);
    }
    if (s_mode == Mode::Outer && s_selected == 5) {
      if (ui_lang::set((ui_lang::Lang)s_data.lang)) s_langChanged = true;
    }
    if (s_mode == Mode::Engineer && s_engSelected == 0) {
      RcController::instance().setDeviceId(s_data.eng_id);
    }
    if (s_mode == Mode::Engineer && s_engSelected == 1) {
      const int *v = s_data.vsp;
      RcController &rc = RcController::instance();
      rc.setVspSetting(vsp_volts(v[2]), vsp_volts(v[1]), vsp_volts(v[0]), vsp_volts(v[5]), vsp_volts(v[4]),
                       vsp_volts(v[3]));
      rc.setVspBypassSetting(vsp_volts(v[8]), vsp_volts(v[7]), vsp_volts(v[6]), vsp_volts(v[11]),
                             vsp_volts(v[10]), vsp_volts(v[9]));
    }
    if (s_mode == Mode::Engineer && s_engSelected == 2) {
      save_pin(s_data.pin);
    }
    if (s_mode == Mode::Outer && s_selected == 3) {
      RcController::instance().setFilterInitial(s_data.filter_init);
    }
    snapshot_data();
  } else {
    restore_data();
  }
  s_in_panel = false;
  s_panel_row = 0;
  s_wiz.open = false;
  refresh_content();
}

int panel_row_count() {
  if (s_mode == Mode::Engineer) {
    if (s_engSelected == 0) return 1;
    if (s_engSelected == 2) return 4;
    return 2;
  }
  switch (s_selected) {
    case 0: return airflow_row_count(); // mode [/ 전열 방식] / fan [/ re-run pressure setup]
    case 1: return 2; // reservation: type / time
    case 2: return 3; // wifi: pair / scan / disconnect
    case 3: return 1; // filter: reset
    case 4: return 5; // clock: y/m/d/h/min
    case 5: return 1; // language
    default: return 1;
  }
}

void change_id(int delta) { s_data.eng_id = wrap(s_data.eng_id - 1 + delta, 99) + 1; }

// Mode/volume/clean (case 0) fire their backend call immediately here -
// "cycled by button", not an edit deferred to OK. Reservation hour
// pickers (case 1, rows 0/1) are the one spot left that's still a plain
// local value; rows 2/3 there have nothing to adjust via Left/Right,
// they're one-shot actions fired from handle_ok() instead.
void panel_adjust(int delta) {
  if (s_mode == Mode::Engineer) {
    if (s_engSelected == 0) {
      change_id(delta);
    } else if (s_engSelected == 2) {
      int d[4] = {};
      digits_from_pin(s_data.pin, d);
      d[s_panel_row] = wrap(d[s_panel_row] + delta, 10);
      s_data.pin = pin_from_digits(d);
    } else {
      s_data.vsp_slot = wrap(s_data.vsp_slot + delta, 12);
    }
    refresh_content();
    return;
  }

  switch (s_selected) {
    case 0:
      // Local pick only - nothing is sent until OK (see handle_ok()).
      switch (airflow_row(s_panel_row)) {
        case AIR_MODE: s_data.ahu_mode = wrap(s_data.ahu_mode + delta, 5); break;
        case AIR_VENT:
          s_data.vent_sub = wrap(s_data.vent_sub + delta, 3);
          if (s_data.vent_sub) s_data.press_kind = s_data.vent_sub;  // re-run row follows the pick
          break;
        case AIR_FAN: s_data.ahu_volume = wrap(s_data.ahu_volume - 1 + delta, 3) + 1; break;
        case AIR_RERUN:
          s_data.press_kind = s_data.press_kind == RC::PRESS_POSITIVE ? RC::PRESS_NEGATIVE : RC::PRESS_POSITIVE;
          break;
      }
      break;
    case 1: {
      const int steps = RC_RESERVE_MAX_MIN / RC_RESERVE_STEP_MIN;
      if (s_panel_row == 0) {
        s_data.resv_type = wrap(s_data.resv_type + delta, 4);
      } else if (s_panel_row == 1 && s_data.resv_type <= 1) {
        const int idx = wrap(s_data.resv_min / RC_RESERVE_STEP_MIN - 1 + delta, steps);
        s_data.resv_min = (idx + 1) * RC_RESERVE_STEP_MIN;
      }
      break;
    }
    case 3:
      // Initial value fixed at 2,000 h for now (RC_FILTER_INIT_FIXED): OK only resets.
      if (!RC_FILTER_INIT_FIXED) {
        s_data.filter_init = wrap(s_data.filter_init - RC_FILTER_INIT_MIN + delta,
                                  RC_FILTER_INIT_MAX - RC_FILTER_INIT_MIN + 1) + RC_FILTER_INIT_MIN;
      }
      break;
    case 2:
      s_panel_row = wrap(s_panel_row + delta, 3);
      break;
    case 4:
      if (s_panel_row == 0) s_data.clock_y = wrap(s_data.clock_y - 2020 + delta, 80) + 2020;
      else if (s_panel_row == 1) s_data.clock_mo = wrap(s_data.clock_mo - 1 + delta, 12) + 1;
      else if (s_panel_row == 2) {
        s_data.clock_d = wrap(s_data.clock_d - 1 + delta, days_in_month(s_data.clock_y, s_data.clock_mo)) + 1;
      } else if (s_panel_row == 3) s_data.clock_h = wrap(s_data.clock_h + delta, 24);
      else s_data.clock_mi = wrap(s_data.clock_mi + delta, 60);
      clamp_clock_day();
      break;
    case 5:
      s_data.lang = wrap(s_data.lang + delta, 2);
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
  if (s_mode == Mode::Engineer && s_engSelected == 1) {
    // Up raises the value (delta -1 = Up key), 0.05 V per press, kept inside 0.5..5.5 V
    int &val = s_data.vsp[s_data.vsp_slot];
    val -= delta;
    if (val <= RC::VSP_MIN) val = RC::VSP_MIN + 1;
    if (val >= RC::VSP_MAX) val = RC::VSP_MAX - 1;
    refresh_content();
    return;
  }
  s_panel_row = wrap(s_panel_row + delta, panel_row_count());
  refresh_content();
}

bool is_in_panel() { return s_in_panel; }

bool take_language_changed() {
  const bool changed = s_langChanged;
  s_langChanged = false;
  return changed;
}

void set_time(int year, int month, int day, int hour24, int minute) {
  s_clockValid = true;
  s_clockY = year;
  s_clockMo = month;
  s_clockD = day;
  s_clockH = hour24;
  s_clockM = minute;
  if (!s_breadcrumbTime) return;
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d/%02d/%02d %s %02d:%02d", year, month, day, hour24 < 12 ? "AM" : "PM", hour12,
           minute);
  lv_label_set_text(s_breadcrumbTime, buf);
}

void set_wifi_state(const WifiDto &state) {
  const bool wasSetup = s_wifi.setup;
  s_wifi = state;
  // Setup mode ended (connected, timed out): back to the status page.
  if (s_wifiView == WifiView::Qr && wasSetup && !state.setup) s_wifiView = WifiView::Main;
  if (s_breadcrumbWifi) lv_obj_set_style_opa(s_breadcrumbWifi, state.connected() ? LV_OPA_COVER : LV_OPA_40, 0);
  if (s_screen && lv_scr_act() == s_screen && s_mode == Mode::Outer && s_selected == 2) refresh_content();
}

void handle_up() {
  if (wifi_key(Key::Up)) return;
  if (wizard_key(Key::Up)) return;
  if (s_pinOpen && s_mode == Mode::Outer && s_selected == 6) {
    s_pinDigit[s_pinPos] = wrap(s_pinDigit[s_pinPos] + 1, 10);
    s_pinWrong = false;
    refresh_content();
    return;
  }
  if (s_in_panel && s_mode == Mode::Engineer && s_engSelected == 2) {
    panel_adjust(1);
    return;
  }
  if (s_in_panel && s_mode == Mode::Outer && s_selected == 4) {
    panel_adjust(1);
    return;
  }
  if (s_in_panel) {
    panel_move_row(-1);
    return;
  }
  if (s_mode == Mode::Engineer) engineer_select_prev();
  else select_prev();
}

void handle_down() {
  if (wifi_key(Key::Down)) return;
  if (wizard_key(Key::Down)) return;
  if (s_pinOpen && s_mode == Mode::Outer && s_selected == 6) {
    s_pinDigit[s_pinPos] = wrap(s_pinDigit[s_pinPos] - 1, 10);
    s_pinWrong = false;
    refresh_content();
    return;
  }
  if (s_in_panel && s_mode == Mode::Engineer && s_engSelected == 2) {
    panel_adjust(-1);
    return;
  }
  if (s_in_panel && s_mode == Mode::Outer && s_selected == 4) {
    panel_adjust(-1);
    return;
  }
  if (s_in_panel) {
    panel_move_row(1);
    return;
  }
  if (s_mode == Mode::Engineer) engineer_select_next();
  else select_next();
}

void handle_left() {
  if (wifi_key(Key::Left)) return;
  if (wizard_key(Key::Left)) return;
  if (s_pinOpen && s_mode == Mode::Outer && s_selected == 6) {
    s_pinPos = wrap(s_pinPos - 1, 4);
    refresh_content();
    return;
  }
  if (s_in_panel && s_mode == Mode::Engineer && s_engSelected == 2) {
    panel_move_row(-1);
    return;
  }
  if (s_in_panel && s_mode == Mode::Outer && s_selected == 4) {
    panel_move_row(-1);
    return;
  }
  if (s_in_panel) panel_adjust(-1);
}

void handle_right() {
  if (wifi_key(Key::Right)) return;
  if (wizard_key(Key::Right)) return;
  if (s_pinOpen && s_mode == Mode::Outer && s_selected == 6) {
    s_pinPos = wrap(s_pinPos + 1, 4);
    refresh_content();
    return;
  }
  if (s_in_panel && s_mode == Mode::Engineer && s_engSelected == 2) {
    panel_move_row(1);
    return;
  }
  if (s_in_panel && s_mode == Mode::Outer && s_selected == 4) {
    panel_move_row(1);
    return;
  }
  if (s_in_panel) panel_adjust(1);
}

void handle_ok() {
  if (wifi_key(Key::Ok)) return;
  if (wizard_key(Key::Ok)) return;
  if (s_in_panel) {
    if (s_mode == Mode::Outer && s_selected == 0) {
      // AHU control: OK sends ONLY the focused row's command - Left/Right
      // only edited the local pick, nothing was sent yet. Stays in the
      // panel afterward (success or failure) so other rows can still be
      // picked and sent in the same visit; Back is what leaves.
      switch (airflow_row(s_panel_row)) {
        case AIR_MODE:
        case AIR_VENT: fire_mode(); break;
        case AIR_FAN: fire_volume(); break;
        case AIR_RERUN: open_wizard((uint8_t)s_data.press_kind); break;
      }
      return;
    }
    if (s_mode == Mode::Outer && s_selected == 1) {
      // Reservation: OK sends from either row.
      fire_reservation();
      return;
    }
    leave_panel(true);
    return;
  }
  if (s_mode == Mode::Outer && s_selected == 6) {
    if (!s_pinOpen) {
      pin_reset();
      s_pinOpen = true;
      refresh_content();
      return;
    }
    if (pin_matches()) {
      enter_engineer();
    } else {
      s_pinWrong = true;
      refresh_content();
    }
    return;
  }
  enter_panel();
}

bool handle_back() {
  if (wifi_key(Key::Back)) return true;
  if (wizard_key(Key::Back)) return true;
  if (s_in_panel) {
    // AHU control (s_selected == 0) needs no special case here anymore:
    // the generic restore_data() below correctly discards only whatever
    // local pick was never sent - fire_mode/volume/clean() already lock
    // a sent field into s_snap the moment it's confirmed, so Back can't
    // undo a row that already reached the controller.
    leave_panel(false);
    return true;
  }
  if (s_pinOpen && s_mode == Mode::Outer && s_selected == 6) {
    pin_reset();
    refresh_content();
    return true;
  }
  if (s_mode == Mode::Engineer) {
    exit_engineer();
    return true;
  }
  return false;
}

} // namespace ui_settings_screen
