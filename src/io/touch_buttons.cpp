#include "touch_buttons.h"
#include <Wire.h>

namespace touch {

namespace {

constexpr uint8_t kBtnCount = static_cast<uint8_t>(Button::Count);
constexpr uint8_t kDebounceMs = 30;

bool s_pressed[kBtnCount] = {};
bool s_justPressed[kBtnCount] = {};
uint32_t s_lastChangeMs[kBtnCount] = {};
Callback s_callback = nullptr;

// ANMG08KPL1 touch IC, confirmed by datasheet + on-board verification:
// I2C address 0x24, output-data register 0x2A, bit N = CS(N+1) = 1 when
// touched. Bit order (0..7 = CS1..CS8) already matches the Button enum
// above (Back..Power), so the raw byte can be used directly as the mask.
constexpr uint8_t kTouchAddr = 0x24;
constexpr uint8_t kOutputDataReg = 0x2A;

uint8_t read_raw_mask() {
  Wire.beginTransmission(kTouchAddr);
  Wire.write(kOutputDataReg);
  if (Wire.endTransmission(false) != 0) return 0x00; // bus error -> nothing pressed
  Wire.requestFrom((int)kTouchAddr, 1, (int)true);
  if (Wire.available()) return Wire.read();
  return 0x00;
}

} // namespace

void init() {
  // Wire.begin() happens once in main.cpp - this is the same shared
  // sensor bus (SDA=40/SCL=41) that sensors.cpp also uses.
}

void poll() {
  uint8_t mask = read_raw_mask();
  uint32_t now = millis();

  for (uint8_t i = 0; i < kBtnCount; i++) {
    bool raw = mask & (1 << i);
    s_justPressed[i] = false;

    if (raw != s_pressed[i]) {
      if (now - s_lastChangeMs[i] >= kDebounceMs) {
        s_pressed[i] = raw;
        s_lastChangeMs[i] = now;
        if (raw) {
          s_justPressed[i] = true;
          if (s_callback) s_callback(static_cast<Button>(i));
        }
      }
    } else {
      s_lastChangeMs[i] = now;
    }
  }
}

bool isPressed(Button b) { return s_pressed[static_cast<uint8_t>(b)]; }
bool justPressed(Button b) { return s_justPressed[static_cast<uint8_t>(b)]; }

void onPress(Callback cb) { s_callback = cb; }

} // namespace touch
