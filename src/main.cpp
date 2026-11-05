/*
 * CendoriRC  —  CD-WD-RMC-V1.0-260811  (SL Technologies)
 * MCU   : ESP32-S3-WROOM-1-N8R8   (8 MB flash / 8 MB octal PSRAM)
 * Panel : T050800480-B8WMN-037    (5.0" 800x480, ST7262 RGB565, 40p CN1)
 *
 * Physical buttons:
 *   POWER   -> toggle LCD backlight, and send AHU power on/off to the PCB
 *   SETTING -> go to settings
 *   In settings: OK enters the right panel, arrows change values,
 *   OK saves, BACK cancels / leaves. See ui_settings_screen.h.
 */
#include <stdio.h>

#include <cmath>

#include <lvgl.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "io/ahu_link.h"
#include "io/ahu_protocol.h"
#include "io/backlight.h"
#include "io/plc_link.h"
#include "io/plc_protocol.h"
#include "io/rs485_link.h"
#include "io/sensors.h"
#include "io/touch_buttons.h"
#include "port/esp_time.h"
#include "port/i2c_bus.h"
#include "net/WifiController.h"
#include "rc/AirIndex.h"
#include "rc/RcController.h"
#include "ui/ui_dashboard_chrome.h"
#include "ui/ui_lang.h"
#include "ui/ui_main_screen.h"
#include "ui/ui_main_screen_v2.h"
#include "ui/ui_main_screen_v3.h"
#include "ui/ui_settings_screen.h"
#include "ui/ui_theme.h"

/* --- CN1 / U7 net map (schematic sheet 1) ------------------------------ */
#define PIN_BL   46   // TFT_BL  -> R11 100R -> MP3302 EN  (HIGH = backlight on)
#define PIN_DE   47   // TFT_DE    -> CN1.34
#define PIN_VS   38   // TFT_VSYNC -> CN1.33
#define PIN_HS   39   // TFT_HSYNC -> CN1.32
#define PIN_CLK  21   // TFT_PCLK  -> R66 22R -> CN1.30 DCLK

/* --- Sensor bus / analog input (sheet 1) -------------------------------- */
#define PIN_I2C_SDA 40  // shared bus: SHT40, SGP40, SCD40, RTC, touch IC
#define PIN_I2C_SCL 41
#define PIN_CO_ADC  8   // SEN_VOUT - CO cell, conditioned by the op-amp stage

/* RGB565: R4..R0 -> CN1 R7..R3, G5..G0 -> G7..G2, B4..B0 -> B7..B3 */
#define LCD_R0 4
#define LCD_R1 3
#define LCD_R2 2
#define LCD_R3 1
#define LCD_R4 42
#define LCD_G0 10
#define LCD_G1 9
#define LCD_G2 15
#define LCD_G3 7
#define LCD_G4 6
#define LCD_G5 5
#define LCD_B0 16
#define LCD_B1 14
#define LCD_B2 13
#define LCD_B3 12
#define LCD_B4 11

/* CN1.35 RESET = RC power-on (R62/C57), CN1.31 DISP = 10k pull-up,
 * CN1.39 CS / CN1.40 OTP = 10k pull-up.  None of them need a GPIO. */

static esp_lcd_panel_handle_t lcd = nullptr;

/* --- LVGL plumbing ------------------------------------------------------ */
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  esp_lcd_panel_draw_bitmap(lcd, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
  lv_disp_flush_ready(drv);
}

/* --- Screens + button dispatch ------------------------------------------
 * Physical keys come from the touch IC as touch::Button. We do not read
 * GPIOs here. touch::poll() in loop() calls onButtonPress() once when a
 * key goes down (the "press edge").
 *
 *   Back    Ok    Right   Down
 *   Up      Left  Setting Power
 */
using ui_lang::tr;

static lv_obj_t *s_dashA = nullptr;      // circular gauges
static lv_obj_t *s_dashB = nullptr;      // trend chart
static lv_obj_t *s_dashC = nullptr;      // light glass design
static lv_obj_t *s_settingsScreen = nullptr;
static int s_dashIndex = 0;              // 0 = dash A, 1 = dash B, 2 = dash C
constexpr int kDashCount = 3;

// Set after a language change: the dashboards still show the old language
// and are rebuilt the next time one is shown.
static bool s_dashStale = false;

static void buildDashboards();
static void logLvMem(const char *what);
static void uiRefresh(const RcDto &rc);
static void refreshLocalSensors();

static void showDashboard() {
  if (s_dashStale) {
    buildDashboards();
    uiRefresh(RcController::instance().getState());
    refreshLocalSensors();
    logLvMem("after dashboard rebuild");
  }
  lv_obj_t *const dash[kDashCount] = {s_dashA, s_dashB, s_dashC};
  lv_scr_load(dash[s_dashIndex]);
}

static bool settingsIsOpen() {
  return lv_scr_act() == s_settingsScreen;
}

// Indoor AQI number 0-500 (worst sub-index) and colour from sensor-data
// average (not from AQI). Ring fill follows the number only.
static AQI::Result refreshAirIndex(const RC::SensorCache &cache) {
  static const char *labelKo[] = {"-", "좋음", "보통", "나쁨", "매우 나쁨"};
  static const char *labelEn[] = {"-", "Good", "Moderate", "Bad", "Very bad"};
  static const uint32_t colors[] = {ui_theme::AQI_NONE, ui_theme::AQI_GOOD, ui_theme::AQI_NORMAL, ui_theme::AQI_BAD,
                                    ui_theme::AQI_VERY_BAD};
  const AQI::Result r = AQI::evaluate(cache);
  const int shown = r.level == AQI::NONE ? -1 : (int)std::lround(r.aqi);
  const char *label = tr(labelKo[r.level], labelEn[r.level]);
  ui_main_screen::set_aqi(shown, label, colors[r.level]);
  ui_main_screen_v2::set_aqi(shown, label, colors[r.level]);
  ui_main_screen_v3::set_aqi(shown, (int)r.level);
  return r;
}

// Pushes the RC's own sensor readings into both dashboards (both stay
// alive, only one is on screen) and into the controller's cache for the
// B6/B7/B8 replies. TVOC on the wire is ug/m3; the dashboard shows ppb.
static void refreshLocalSensors() {
  RC::SensorCache cache;
  cache.hasTemp = sensors::hasTemp();
  cache.tempC = sensors::getTempC();
  cache.humidity = sensors::getHumidityPct();
  cache.hasCo2 = sensors::hasCo2();
  cache.co2 = cache.hasCo2 ? sensors::getCo2Ppm() : -1;
  cache.hasVoc = sensors::hasVocRaw();
  cache.tvoc = cache.hasVoc ? sensors::getTvocUg() : -1;
  const int tvocPpb = cache.hasVoc ? sensors::getTvocPpb() : -1;
  cache.hasPm = sensors::hasPm();
  cache.pm25 = cache.hasPm ? sensors::getPm25Ug() : -1;
  cache.pm10 = cache.hasPm ? sensors::getPm10Ug() : -1;
  cache.pm1 = cache.hasPm ? sensors::getPm1Ug() : -1;
  cache.hasCo = sensors::hasCo();
  cache.co = cache.hasCo ? (int)std::lround(sensors::getCoPpm()) : -1;

  ui_dashboard_chrome::set_temp_humidity(cache.hasTemp ? cache.tempC : -99.0f,
                                         cache.hasTemp ? (int)(cache.humidity + 0.5f) : -1);
  ui_main_screen::set_indoor(cache.hasTemp ? cache.tempC : -99.0f,
                            cache.hasTemp ? (int)(cache.humidity + 0.5f) : -1);
  ui_main_screen_v2::set_indoor(cache.hasTemp ? cache.tempC : -99.0f,
                               cache.hasTemp ? (int)(cache.humidity + 0.5f) : -1);
  ui_main_screen_v3::set_indoor(cache.hasTemp ? cache.tempC : -99.0f,
                               cache.hasTemp ? (int)(cache.humidity + 0.5f) : -1);
  ui_main_screen_v3::set_sensors(cache.pm25, cache.pm10, cache.co2, tvocPpb, cache.co);
  ui_dashboard_chrome::set_co2(cache.co2);
  ui_dashboard_chrome::set_tvoc(tvocPpb);
  ui_dashboard_chrome::set_pm25((float)cache.pm25);
  ui_dashboard_chrome::set_pm10((float)cache.pm10);
  ui_dashboard_chrome::set_co(cache.co);

  if (sensors::hasRtc()) {
    ui_dashboard_chrome::set_time(sensors::getYear(), sensors::getMonth(), sensors::getDay(),
                                  sensors::getHour24(), sensors::getMinute());
    ui_settings_screen::set_time(sensors::getYear(), sensors::getMonth(), sensors::getDay(),
                                 sensors::getHour24(), sensors::getMinute());
    ui_main_screen_v3::set_time(sensors::getYear(), sensors::getMonth(), sensors::getDay(),
                                sensors::getHour24(), sensors::getMinute());
  }
  RcController::instance().setLocalSensors(cache);
  const AQI::Result aqi = refreshAirIndex(cache);

  static uint32_t lastPrintMs = 0;
  uint32_t now = millis();
  if (now - lastPrintMs >= 500) {
    lastPrintMs = now;
    printf("[SEN] indoor T=%.1fC RH=%.0f%% CO2=%d VOC=%d(idx %d) CO=%d(%dmV) PM2.5=%d PM10=%d aqi=%d\n",
           cache.hasTemp ? cache.tempC : -99.0f, cache.hasTemp ? cache.humidity : -1.0f,
           cache.hasCo2 ? cache.co2 : -1, cache.hasVoc ? cache.tvoc : -1, sensors::getVocIndex(),
           cache.co, sensors::getCoMilliVolts(),
           cache.hasPm ? cache.pm25 : -1, cache.hasPm ? cache.pm10 : -1,
           aqi.level == AQI::NONE ? -1 : (int)std::lround(aqi.aqi));
  }
}

static const char *modeBadge(const RcDto &rc) {
  static const char *names[RC::UI_MODE_COUNT] = {"전열교환", "바이패스", "청정", "자동", "AI 모드",
                                                 "양압 모드", "음압 모드", "바이패스 양압", "바이패스 음압"};
  static const char *namesEn[RC::UI_MODE_COUNT] = {"HEAT RECOVERY", "BYPASS", "CLEAN", "AUTO", "AI",
                                                   "POSITIVE", "NEGATIVE", "BYPASS POSITIVE", "BYPASS NEGATIVE"};
  if (!rc.isRunning()) return tr("정지", "OFF");
  if (rc.autoActive() && rc.effectiveClean()) return tr("자동 청정", "AUTO CLEAN");
  return tr(names[rc.uiMode()], namesEn[rc.uiMode()]);
}

// RcController data callback - UI task only.
static void uiRefresh(const RcDto &rc) {
  // Shown from RcDto only - the same values the next B5 carries (sleep -> low,
  // auto -> the RC's own Table 4 result).
  ui_main_screen::set_mode_badge(modeBadge(rc));
  ui_main_screen_v2::set_mode_badge(modeBadge(rc));
  const uint8_t vol = rc.effectiveVolume();
  ui_main_screen::set_fan_level(vol);
  ui_main_screen_v2::set_fan_level(vol);

  // Dashboard C: mode buttons, per-fan state (급기모드 = supply only,
  // 배기모드 = exhaust only, fan error bits from D5 Data4), reservation.
  static const char *modeBtn[RC::UI_MODE_COUNT] = {"전열 모드", "바이패스", "청정모드", "자동모드", "AI 모드",
                                                   "양압 모드", "음압 모드", "바이패스 양압", "바이패스 음압"};
  static const char *modeBtnEn[RC::UI_MODE_COUNT] = {"Ventilation", "Bypass", "Clean", "Auto", "AI Mode",
                                                     "Positive mode", "Negative mode", "Bypass positive",
                                                     "Bypass negative"};
  const RC::UiMode uiMode = rc.uiMode();
  // Pressure modes: "양압 모드 +8%" and a second line with both airflows
  // (+ CO2 boost) - the pressure-mode status the spec asks for.
  uint16_t supM3h = 0, exhM3h = 0;
  if (rc.pressureAirflow(supM3h, exhM3h)) {
    const RC::PressureSetup *ps = rc.pressureSetup(rc.pressure);
    const bool pos = rc.pressure == RC::PRESS_POSITIVE;
    char title[40], detail[64];
    snprintf(title, sizeof(title), "%s %c%u%%", tr(modeBtn[uiMode], modeBtnEn[uiMode]), pos ? '+' : '-',
             (unsigned)ps->offsetPct);
    if (rc.pressBoost)  // keep it inside the 178 px button
      snprintf(detail, sizeof(detail), "%s %u / %u m³/h", tr("CO₂ 부스트", "CO₂ boost"), (unsigned)supM3h,
               (unsigned)exhM3h);
    else
      snprintf(detail, sizeof(detail), "%s %u · %s %u m³/h", tr("급기", "Supply"), (unsigned)supM3h,
               tr("배기", "Exhaust"), (unsigned)exhM3h);
    ui_main_screen_v3::set_mode(title, rc.isRunning(), detail);
  } else {
    ui_main_screen_v3::set_mode(tr(modeBtn[uiMode], modeBtnEn[uiMode]), rc.isRunning(), "");
  }
  const int fanLevel = rc.isRunning() ? vol : 0;
  ui_main_screen_v3::set_fan(fanLevel, rc.errCode & (PLC::SUPPLY_FAN_ERR | PLC::EXHAUST_FAN_ERR));
  if (rc.mode == RC::START_RESERVE) ui_main_screen_v3::set_reservation(1, rc.startMin);
  else if (rc.mode == RC::STOP_RESERVE) ui_main_screen_v3::set_reservation(2, rc.stopMin);
  else ui_main_screen_v3::set_reservation(0, 0);

  // VSP for the active fan step (약/중/강) and current 전열/바이패스 set
  // (pressure modes: the setpoint table the RC sent to MAIN).
  uint8_t supplyVsp = 0;
  uint8_t exhaustVsp = 0;
  rc.currentVsp(supplyVsp, exhaustVsp);
  ui_main_screen::set_fan_vsp(supplyVsp, exhaustVsp);
  ui_main_screen_v2::set_fan_vsp(supplyVsp, exhaustVsp);
  // Dashboard C: "--" for a fan that is stopped (OFF, 배기모드 -> supply off,
  // 급기모드 -> exhaust off).
  ui_main_screen_v3::set_fan_vsp((fanLevel && !rc.exhaust) ? supplyVsp : 0,
                                 (fanLevel && !rc.supply) ? exhaustVsp : 0);

  // Old RC error codes (SHR-L2 2.7): E1 supply fan, E2 exhaust fan, E3 both.
  const bool supErr = rc.errCode & PLC::SUPPLY_FAN_ERR;
  const bool exhErr = rc.errCode & PLC::EXHAUST_FAN_ERR;
  const char *status = tr("필터 정상", "Filter OK");
  bool statusOk = false;
  if (supErr && exhErr) status = "E3";
  else if (supErr) status = "E1";
  else if (exhErr) status = "E2";
  else if (rc.errCode & PLC::OA_FAN_ERR) status = "FAN ERR";
  else if (rc.filterReplaceDue()) // MAIN's alarm bit, or the RC's own 2,000 h count
    status = tr("필터 교체", "Replace filter");
  else if (rc.errCode & PLC::FILTER_CLEAN_ALARM) status = tr("필터 청소", "Clean filter");
  else statusOk = true;
  ui_dashboard_chrome::set_filter_status(status, statusOk);
  ui_main_screen_v3::set_filter_status(status, statusOk);

  if (rc.rxMask & RC::RX_D5) {
    ui_dashboard_chrome::set_weather(tr("외기", "Outdoor"), (float)rc.outdoorTemp);
    ui_main_screen_v3::set_weather(tr("외기", "Outdoor"), (float)rc.outdoorTemp);
  }
  ui_settings_screen::set_state(rc);
}

// LVGL pool use, to watch the headroom left by a rebuild.
static void logLvMem(const char *what) {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  printf("[UI] %s: LVGL pool used %u%% free %u B (largest %u B, frag %u%%)\n", what, (unsigned)mon.used_pct,
         (unsigned)mon.free_size, (unsigned)mon.free_biggest_size, (unsigned)mon.frag_pct);
}

// Builds both dashboards in the current language. The old ones are deleted
// FIRST: the LVGL pool (LV_MEM_SIZE) only fits one set of screens, so old
// and new side by side runs it out. Must not be called while a dashboard
// is the active screen.
static void buildDashboards() {
  if (s_dashA) lv_obj_del(s_dashA);
  if (s_dashB) lv_obj_del(s_dashB);
  if (s_dashC) lv_obj_del(s_dashC);
  ui_dashboard_chrome::reset();
  s_dashA = lv_obj_create(NULL);
  ui_main_screen::create(s_dashA);
  s_dashB = lv_obj_create(NULL);
  ui_main_screen_v2::create(s_dashB);
  s_dashC = lv_obj_create(NULL);
  ui_main_screen_v3::create(s_dashC);
  ui_dashboard_chrome::set_wifi_connected(WifiController::instance().getState().connected());
  ui_main_screen_v3::set_wifi_connected(WifiController::instance().getState().connected());
  s_dashStale = false;
}

// WifiController data callback - UI task only.
static void wifiRefresh(const WifiDto &wifi) {
  ui_dashboard_chrome::set_wifi_connected(wifi.connected());
  ui_main_screen_v3::set_wifi_connected(wifi.connected());
  ui_settings_screen::set_wifi_state(wifi);
}

// Builds the settings screen in the current language. On a rebuild
// (language change) the shown screen is swapped for an empty placeholder
// and deleted before the new one is built (same pool limit as above).
// Nothing is drawn in between, so the user only sees the new screen.
static void buildSettings() {
  lv_obj_t *placeholder = nullptr;
  if (s_settingsScreen) {
    placeholder = lv_obj_create(NULL);
    lv_scr_load(placeholder);
    lv_obj_del(s_settingsScreen);
  }
  s_settingsScreen = ui_settings_screen::create();
  ui_settings_screen::set_wifi_state(WifiController::instance().getState());
  if (placeholder) {
    lv_scr_load(s_settingsScreen);
    lv_obj_del(placeholder);
  }
}

// Language change: only the settings screen (on screen now) is rebuilt
// here, so the switch is one quick redraw. The dashboards follow when the
// user leaves settings.
static void onLanguageChanged() {
  const uint32_t t0 = millis();
  buildSettings();
  s_dashStale = true;
  ui_settings_screen::set_state(RcController::instance().getState());
  refreshLocalSensors();
  printf("[UI] settings rebuilt in %u ms\n", (unsigned)(millis() - t0));
  logLvMem("after settings rebuild");
}

static void onButtonPress(touch::Button b) {
  // Power toggles the LCD and sends the matching AHU command (any screen).
  if (b == touch::Button::Power) {
    if (millis() < 2000) return;
    const RcDto rc = RcController::instance().getState();
    const bool running = rc.isRunning();
    if (RcController::instance().setPowerState(!running)) backlight::set(!running);
    return;
  }

  if (settingsIsOpen()) {
    switch (b) {
      case touch::Button::Back:
        if (!ui_settings_screen::handle_back()) showDashboard();
        break;
      case touch::Button::Up:
        ui_settings_screen::handle_up();
        break;
      case touch::Button::Down:
        ui_settings_screen::handle_down();
        break;
      case touch::Button::Left:
        ui_settings_screen::handle_left();
        break;
      case touch::Button::Right:
        ui_settings_screen::handle_right();
        break;
      case touch::Button::Ok:
        ui_settings_screen::handle_ok();
        if (ui_settings_screen::take_language_changed()) onLanguageChanged();
        break;
      default:
        break;
    }
    return;
  }

  // Dashboard
  switch (b) {
    case touch::Button::Setting:
      lv_scr_load(s_settingsScreen);
      break;
    case touch::Button::Left:
      s_dashIndex = (s_dashIndex + kDashCount - 1) % kDashCount;
      showDashboard();
      break;
    case touch::Button::Right:
      s_dashIndex = (s_dashIndex + 1) % kDashCount;
      showDashboard();
      break;
    default:
      break;
  }
}

// D5 status poll for this PCB (id 0x64). Ignore IoT/other station frames.
constexpr bool kAhuIdTestEnabled = false;
// Real id is 0x62, not 0x64 - the id echoed in prior replies was never
// proof of the unit's real address, it just mirrored whatever id we sent.
constexpr uint8_t kAhuTestId = 0x62;

// One-shot diagnostic: does MAIN react AT ALL to a single, isolated, clean
// B5 transmit - independent of whether we ever successfully decode a D5/E5
// first? plc_protocol::sendDiagnosticProbeOnce() sends exactly one valid B5
// frame through the real GPIO19/Q5 path, then it's done - never looped.
// Reset check: logResetReason() already runs first thing in app_main() on
// EVERY boot, so if this TX browns out the board, the very next thing in
// the capture will be a fresh "[BOOT] reset_reason=BROWNOUT" line - no
// extra detection code needed, just watch for that line right after the
// "[PLC] DIAG probe" line.
// Already did its job this session (confirmed TX path is electrically
// clean and safe, no brownout) - off now so it doesn't interleave with the
// real decode+reply path below and confuse the capture.
constexpr bool kPlcDiagProbeEnabled = false;

// TEMPORARY diagnostic: listen on the RS-485 bus for durationMs WITHOUT
// transmitting anything, to check whether the "B5 00 64 00 00 1E 00 00 00
// CF" frame we keep getting back from E5/D5 is actually a reply to us, or
// just station 0x64's own periodic broadcast landing in our listen window
// regardless of what (or whether) we send. If the same bytes repeat here
// on their own cadence with nothing transmitted, every "response" logged
// by test_ahu_status_poll()/power_on() so far was never a reply at all.
static void test_ahu_passive_listen(uint32_t durationMs) {
  uart_flush_input(UART_NUM_1);
  uint32_t start = millis();
  while (millis() - start < durationMs) {
    uint8_t b;
    (void)uart_read_bytes(UART_NUM_1, &b, 1, 0);
  }
}

static void test_ahu_status_poll() {
  uint8_t frame[10] = {
      0xD5, 0x00, kAhuTestId, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  uint8_t parity = 0;
  for (int i = 0; i < 9; i++) parity ^= frame[i];
  frame[9] = parity;

  for (int n = 1; n <= 3; n++) {
    uint8_t resp[16] = {0};
    (void)ahu_link::transact(frame, sizeof(frame), resp, sizeof(resp));
    delay(500);
  }
}

static void appSetup() {
  // Q5 first: USB-JTAG on GPIO19 would otherwise load PLC+ and kill 5V
  // before the panel ever comes up. Then a dim backlight so a PLC-only
  // boot is visible even if RGB init is slow.
  plc_link::holdQ5Off();
  backlight::init(PIN_BL);
  backlight::setBrightness(50);

  printf("[power] GPIO19 LOW (Q5 off), backlight on\n");

  // Shared I2C bus - started once here, not per-module (touch_buttons.cpp
  // and sensors.cpp both just assume it's already running).
  i2c_bus::begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);

  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
  panel_config.timings.pclk_hz = 16000000; // 16 MHz DCLK
  panel_config.timings.h_res = 800;
  panel_config.timings.v_res = 480;
  panel_config.timings.hsync_pulse_width = 30;
  panel_config.timings.hsync_back_porch = 16;
  panel_config.timings.hsync_front_porch = 210;
  panel_config.timings.vsync_pulse_width = 13;
  panel_config.timings.vsync_back_porch = 10;
  panel_config.timings.vsync_front_porch = 22;
  panel_config.timings.flags.pclk_active_neg = 1;
  panel_config.data_width = 16;
  panel_config.in_color_format = LCD_COLOR_FMT_RGB565;
  panel_config.num_fbs = 1;
  // The LCD peripheral DMAs pixels straight out of the frame buffer every
  // PCLK; with the fb in PSRAM, anything else contending for the PSRAM bus
  // (our own LVGL draw-buffer copies, I2C/UART ISRs, flash reads) can
  // starve that DMA and show up as tearing/glitching ("buffering"). A
  // bounce buffer in fast internal SRAM absorbs that - the DMA reads from
  // it instead of PSRAM directly. 10 lines is the usual starting point in
  // Espressif's own RGB+PSRAM examples.
  panel_config.bounce_buffer_size_px = 800 * 10;
  panel_config.dma_burst_size = 64;
  panel_config.hsync_gpio_num = (gpio_num_t)PIN_HS;
  panel_config.vsync_gpio_num = (gpio_num_t)PIN_VS;
  panel_config.de_gpio_num = (gpio_num_t)PIN_DE;
  panel_config.pclk_gpio_num = (gpio_num_t)PIN_CLK;
  panel_config.disp_gpio_num = (gpio_num_t)-1;
  // LSB-first: bit0..4=B, bit5..10=G, bit11..15=R (RGB565 wire order).
  panel_config.data_gpio_nums[0] = (gpio_num_t)LCD_B0;
  panel_config.data_gpio_nums[1] = (gpio_num_t)LCD_B1;
  panel_config.data_gpio_nums[2] = (gpio_num_t)LCD_B2;
  panel_config.data_gpio_nums[3] = (gpio_num_t)LCD_B3;
  panel_config.data_gpio_nums[4] = (gpio_num_t)LCD_B4;
  panel_config.data_gpio_nums[5] = (gpio_num_t)LCD_G0;
  panel_config.data_gpio_nums[6] = (gpio_num_t)LCD_G1;
  panel_config.data_gpio_nums[7] = (gpio_num_t)LCD_G2;
  panel_config.data_gpio_nums[8] = (gpio_num_t)LCD_G3;
  panel_config.data_gpio_nums[9] = (gpio_num_t)LCD_G4;
  panel_config.data_gpio_nums[10] = (gpio_num_t)LCD_G5;
  panel_config.data_gpio_nums[11] = (gpio_num_t)LCD_R0;
  panel_config.data_gpio_nums[12] = (gpio_num_t)LCD_R1;
  panel_config.data_gpio_nums[13] = (gpio_num_t)LCD_R2;
  panel_config.data_gpio_nums[14] = (gpio_num_t)LCD_R3;
  panel_config.data_gpio_nums[15] = (gpio_num_t)LCD_R4;
  panel_config.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&panel_config, &lcd) != ESP_OK ||
      esp_lcd_panel_reset(lcd) != ESP_OK ||
      esp_lcd_panel_init(lcd) != ESP_OK) {
    return;
  }

  lv_init();

  // Partial draw buffers (40 lines each) live happily in PSRAM. PSRAM is
  // not optional here - esp_lcd_new_rgb_panel's own full frame buffer
  // (~750KB) requires it regardless of what these are backed by.
  const uint32_t buf_lines = 40;
  const size_t buf_px = (size_t)800 * buf_lines;
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) {
    return;
  }
  lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = 800;
  s_disp_drv.ver_res = 480;
  s_disp_drv.flush_cb = disp_flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  ui_lang::load();
  buildDashboards();
  buildSettings();
  showDashboard();
  logLvMem("boot");

  for (int i = 0; i < 5; i++) {
    lv_timer_handler();
    delay(5);
  }

  touch::init();
  touch::onPress(onButtonPress);

  sensors::init(PIN_CO_ADC);

  // Settings must be loaded before the first reply to MAIN.
  RcController::instance().init();
  RcController::instance().setDataCallback(uiRefresh);
  plc_link::init();
  plc_protocol::init();

  // RS-485 to the monitoring PC (we answer as MAIN). Uses UART1 / GPIO17,18,48,
  // which the old ahu_link master code used - that one is no longer started.
  rs485_link::init();

  // After NVS (RcController::init). Connects to the saved network, if any.
  WifiController::instance().setDataCallback(wifiRefresh);
  WifiController::instance().init();
}

static void appLoop() {
  // D5 poll after the serial monitor is usually open.
  if (kAhuIdTestEnabled) {
    static bool s_passiveListenDone = false;
    if (!s_passiveListenDone && millis() >= 3000) {
      s_passiveListenDone = true;
      test_ahu_passive_listen(4000);
    }

    static bool s_idTestDone = false;
    if (!s_idTestDone && millis() >= 8000) {
      s_idTestDone = true;
      test_ahu_status_poll();
    }
  }

  if (kPlcDiagProbeEnabled) {
    static bool s_diagDone = false;
    if (!s_diagDone && millis() >= 5000) {
      s_diagDone = true;
      plc_protocol::sendDiagnosticProbeOnce();
    }
  }

  // Protocol provider's RX idle-state check, second sample: same read as
  // plc_link::init()'s boot-time log, but taken once the PLC bus has had
  // time to settle (10s in), to see whether the idle level differs from
  // the immediate post-boot reading.
  {
    static bool s_rxIdleLateDone = false;
    if (!s_rxIdleLateDone && millis() >= 10000) {
      s_rxIdleLateDone = true;
      printf("[PLC] RX pin idle state (later-boot, t=%ums): %d\n", (unsigned)millis(),
             plc_link::rxPinLevel());
    }
  }

  touch::poll();

  // Old RC 3.4: hold Up + Down for 3 s on the dashboard toggles heater use.
  {
    static uint32_t s_comboStartMs = 0;
    static bool s_comboFired = false;
    const bool held = !settingsIsOpen() && touch::isPressed(touch::Button::Up) &&
                      touch::isPressed(touch::Button::Down);
    if (!held) {
      s_comboStartMs = 0;
      s_comboFired = false;
    } else if (s_comboStartMs == 0) {
      s_comboStartMs = millis();
    } else if (!s_comboFired && millis() - s_comboStartMs >= 3000) {
      s_comboFired = true;
      const bool on = !RcController::instance().getState().heater;
      RcController::instance().setFunctionState("heater", on);
      printf("[KEY] Up+Down 3s -> heater %s\n", on ? "ON" : "OFF");
    }
  }
  plc_protocol::poll();
  backlight::tick();

  if (sensors::poll()) refreshLocalSensors();
  RcController::instance().uiTick();
  WifiController::instance().uiTick();
  lv_timer_handler();
  delay(5);
}

static void appTask(void *) {
  appSetup();
  for (;;) appLoop();
}

// Prints why this boot happened - specifically so an unthrottled B5 TX
// burst (GPIO19/Q5 switching onto the PLC+ rail every ~450ms) can be told
// apart from a normal reset if the board ever brownouts during testing.
static void logResetReason() {
  esp_reset_reason_t r = esp_reset_reason();
  const char *name = "UNKNOWN";
  switch (r) {
    case ESP_RST_POWERON: name = "POWERON"; break;
    case ESP_RST_EXT: name = "EXT"; break;
    case ESP_RST_SW: name = "SW"; break;
    case ESP_RST_PANIC: name = "PANIC"; break;
    case ESP_RST_INT_WDT: name = "INT_WDT"; break;
    case ESP_RST_TASK_WDT: name = "TASK_WDT"; break;
    case ESP_RST_WDT: name = "WDT"; break;
    case ESP_RST_DEEPSLEEP: name = "DEEPSLEEP"; break;
    case ESP_RST_BROWNOUT: name = "BROWNOUT"; break;
    case ESP_RST_SDIO: name = "SDIO"; break;
    default: break;
  }
  printf("[BOOT] reset_reason=%s (%d)\n", name, (int)r);
}

extern "C" void app_main() {
  logResetReason();
  // LVGL + the sensor/protocol polling loop is stack-heavier than the
  // default main task budget - run it on its own task with real headroom.
  xTaskCreatePinnedToCore(appTask, "app", 16384, nullptr, 5, nullptr, 1);
}
