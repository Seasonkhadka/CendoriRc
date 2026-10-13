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
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

#include "io/ahu_link.h"
#include "io/ahu_protocol.h"
#include "io/backlight.h"
#include "io/plc_link.h"
#include "io/sensors.h"
#include "io/touch_buttons.h"
#include "ui/ui_dashboard_chrome.h"
#include "ui/ui_main_screen.h"
#include "ui/ui_main_screen_v2.h"
#include "ui/ui_settings_screen.h"

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

Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
    PIN_DE, PIN_VS, PIN_HS, PIN_CLK,
    LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
    LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
    LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
    0 /*hsync_pol*/, 210 /*front*/, 30 /*pulse*/, 16 /*back*/,
    0 /*vsync_pol*/,  22 /*front*/, 13 /*pulse*/, 10 /*back*/,
    1 /*pclk_active_neg*/, 16000000 /*16 MHz DCLK*/);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, panel);

/* --- LVGL plumbing ------------------------------------------------------ */
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
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
static lv_obj_t *s_dashA = nullptr;      // circular gauges
static lv_obj_t *s_dashB = nullptr;      // trend chart
static lv_obj_t *s_settingsScreen = nullptr;
static int s_dashIndex = 0;              // 0 = dash A, 1 = dash B

static void showDashboard() {
  lv_scr_load(s_dashIndex == 0 ? s_dashA : s_dashB);
}

static bool settingsIsOpen() {
  return lv_scr_act() == s_settingsScreen;
}

// Pushes the latest sensor cache into both dashboards' widgets. Both
// s_dashA/s_dashB stay alive for the whole program (only one is ever on
// screen, lv_scr_load() just swaps which), so it's safe and cheap to
// update them unconditionally rather than track which one is visible.
//
// TVOC now uses Sensirion's official Gas Index Algorithm (sensors.cpp),
// so it's a real calibrated index and gets pushed to the UI. CO is still
// deliberately NOT pushed: the raw millivolt reading isn't converted to
// ppm yet (needs the sensor cell's sensitivity spec), so it keeps showing
// "--" rather than a number that looks real but isn't.
static void refreshDashboard() {
  if (sensors::hasTemp()) {
    float tempC = sensors::getTempC();
    int rhPct = (int)(sensors::getHumidityPct() + 0.5f);
    Serial.printf("[SHT40] temp=%.1fC rh=%d%%\n", tempC, rhPct);
    ui_dashboard_chrome::set_temp_humidity(tempC, rhPct);
    ui_main_screen::set_indoor(tempC, rhPct);
    ui_main_screen_v2::set_indoor(tempC, rhPct);
  }
  if (sensors::hasCo2()) {
    Serial.printf("[SCD40] co2=%dppm\n", sensors::getCo2Ppm());
    ui_dashboard_chrome::set_co2(sensors::getCo2Ppm());
  }
  if (sensors::hasVocRaw()) {
    Serial.printf("[SGP40] voc_index=%d (raw=%u)\n", sensors::getVocIndex(), sensors::getVocRaw());
    ui_dashboard_chrome::set_tvoc(sensors::getVocIndex());
  }
  if (sensors::hasCo()) {
    Serial.printf("[CO] %dmV (uncalibrated - not shown on screen)\n", sensors::getCoMilliVolts());
  }
  if (sensors::hasPm()) {
    Serial.printf("[PM2012] pm1.0=%d pm2.5=%d pm10=%d ug/m3\n", sensors::getPm1Ug(), sensors::getPm25Ug(),
                  sensors::getPm10Ug());
    ui_dashboard_chrome::set_pm25((float)sensors::getPm25Ug());
    ui_dashboard_chrome::set_pm10((float)sensors::getPm10Ug());
  }
  if (sensors::hasRtc()) {
    Serial.printf("[RTC] %02d:%02d:%02d %04d-%02d-%02d\n", sensors::getHour24(), sensors::getMinute(),
                  sensors::getSecond(), sensors::getYear(), sensors::getMonth(), sensors::getDay());
    ui_dashboard_chrome::set_time(sensors::getHour24(), sensors::getMinute());
  }
}

static void onButtonPress(touch::Button b) {
  Serial.printf("btn %d\n", static_cast<int>(b));

  // Power toggles the LCD and sends the matching AHU command (any screen).
  if (b == touch::Button::Power) {
    if (millis() < 2000) {
      Serial.println("[AHU] ignore Power during boot");
      return;
    }
    const bool on = !backlight::isOn();
    backlight::set(on);
    Serial.printf("[AHU] RC power %s\n", on ? "on" : "off");
    const bool ok = on ? ahu_protocol::power_on() : ahu_protocol::power_off();
    Serial.printf("[AHU] power %s %s\n", on ? "on" : "off", ok ? "ok" : "failed");
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
      s_dashIndex = 0;
      showDashboard();
      break;
    case touch::Button::Right:
      s_dashIndex = 1;
      showDashboard();
      break;
    default:
      break;
  }
}

// D5 status poll for this PCB (id 0x64). Ignore IoT/other station frames.
constexpr bool kAhuIdTestEnabled = true;
// Real id is 0x62, not 0x64 - the id echoed in prior replies was never
// proof of the unit's real address, it just mirrored whatever id we sent.
constexpr uint8_t kAhuTestId = 0x62;

// TEMPORARY diagnostic: listen on the RS-485 bus for durationMs WITHOUT
// transmitting anything, to check whether the "B5 00 64 00 00 1E 00 00 00
// CF" frame we keep getting back from E5/D5 is actually a reply to us, or
// just station 0x64's own periodic broadcast landing in our listen window
// regardless of what (or whether) we send. If the same bytes repeat here
// on their own cadence with nothing transmitted, every "response" logged
// by test_ahu_status_poll()/power_on() so far was never a reply at all.
static void test_ahu_passive_listen(uint32_t durationMs) {
  Serial.printf("[PASSIVE] listening %lums, no TX - watching for unsolicited traffic\n",
                (unsigned long)durationMs);
  while (Serial1.available()) Serial1.read(); // start from empty

  uint32_t start = millis();
  uint32_t lastByteAt = start;
  bool any = false;
  while (millis() - start < durationMs) {
    if (Serial1.available()) {
      uint32_t now = millis();
      if (any && now - lastByteAt > 50) Serial.println(); // gap = new frame
      if (!any || now - lastByteAt > 50) Serial.printf("[PASSIVE] t=%lums:", (unsigned long)(now - start));
      Serial.printf(" %02X", Serial1.read());
      lastByteAt = now;
      any = true;
    }
  }
  if (any) Serial.println();
  else Serial.println("[PASSIVE] nothing received - bus is silent without our TX");
}

static void test_ahu_status_poll() {
  uint8_t frame[10] = {
      0xD5, 0x00, kAhuTestId, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  uint8_t parity = 0;
  for (int i = 0; i < 9; i++) parity ^= frame[i];
  frame[9] = parity;

  Serial.printf("[D5 POLL] id=0x%02X  TX D5 00 %02X ... parity=%02X\n",
                kAhuTestId, kAhuTestId, frame[9]);
  for (int n = 1; n <= 3; n++) {
    uint8_t resp[16] = {0};
    size_t got = ahu_link::transact(frame, sizeof(frame), resp, sizeof(resp));
    if (got == 0) {
      Serial.printf("[D5 POLL] #%d no response\n", n);
    } else {
      Serial.printf("[D5 POLL] #%d got %u bytes:", n, (unsigned)got);
      for (size_t i = 0; i < got; i++) Serial.printf(" %02X", resp[i]);
      Serial.println();
    }
    delay(500);
  }
}

void setup() {
  // Q5 first: USB-JTAG on GPIO19 would otherwise load PLC+ and kill 5V
  // before the panel ever comes up. Then a dim backlight so a PLC-only
  // boot is visible even if RGB init is slow.
  plc_link::holdQ5Off();
  backlight::init(PIN_BL);

  Serial.begin(115200);
  Serial.println("[power] GPIO19 LOW (Q5 off), backlight on");

  // Shared I2C bus - started once here, not per-module (touch_buttons.cpp
  // and sensors.cpp both just assume it's already running).
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);

  if (!gfx->begin()) {
    Serial.println("RGB panel init FAILED");
    return;
  }
  Serial.printf("RGB panel OK: %dx%d\n", gfx->width(), gfx->height());
  gfx->fillScreen(BLACK);

  lv_init();

  // Partial draw buffers (40 lines each) live happily in PSRAM. PSRAM is
  // not optional here - esp_lcd_new_rgb_panel's own full frame buffer
  // (~750KB) requires it regardless of what these are backed by.
  const uint32_t buf_lines = 40;
  const size_t buf_px = (size_t)gfx->width() * buf_lines;
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) {
    Serial.println("LVGL draw buffer allocation FAILED");
    return;
  }
  lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = gfx->width();
  s_disp_drv.ver_res = gfx->height();
  s_disp_drv.flush_cb = disp_flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  s_dashA = lv_obj_create(NULL);
  ui_main_screen::create(s_dashA);
  s_dashB = lv_obj_create(NULL);
  ui_main_screen_v2::create(s_dashB);
  s_settingsScreen = ui_settings_screen::create();
  showDashboard();

  for (int i = 0; i < 5; i++) {
    lv_timer_handler();
    delay(5);
  }

  touch::init();
  touch::onPress(onButtonPress);

  sensors::init(PIN_CO_ADC);
  ahu_protocol::init();
  plc_link::init();
}

void loop() {
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

  // Heartbeat: proves whether ANYTHING is reaching the PC over Serial at
  // all, independent of sensors/boot timing. If this never shows up in
  // the monitor either, the problem is below the application - the COM
  // port isn't actually connected to this chip's Serial output.
  static uint32_t s_lastHeartbeat = 0;
  if (millis() - s_lastHeartbeat >= 1000) {
    s_lastHeartbeat = millis();
    Serial.printf("[alive] uptime=%lus\n", millis() / 1000);
  }

  touch::poll();
  plc_link::poll();
  backlight::tick();

  static bool s_plcWasConnected = false;
  const bool plcConnected = plc_link::isConnected();
  if (plcConnected != s_plcWasConnected) {
    s_plcWasConnected = plcConnected;
    Serial.printf("[PLC] link %s\n", plcConnected ? "detected" : "lost");
    // TODO: audible indicator here once there's a confirmed buzzer pin -
    // no piezo/speaker is in the known pin map (main.cpp:29-56), so this
    // can't be wired up without the actual GPIO it's wired to.
  }

  if (sensors::poll()) refreshDashboard();
  lv_timer_handler();
  delay(5);
}
