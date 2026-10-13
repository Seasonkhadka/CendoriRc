#include "backlight.h"

namespace backlight {

namespace {
uint8_t s_channel = 0;
bool s_on = false;
// Low on purpose: PLC-derived +5V cannot feed a full MP3302 load.
uint8_t s_brightnessPercent = 25;

constexpr uint32_t kRampMs = 600; // spreads power-up inrush out
bool s_ramping = false;
uint32_t s_rampStartMs = 0;

uint8_t dutyForBrightness() {
  return (uint8_t)((uint16_t)s_brightnessPercent * 255 / 100);
}
} // namespace

void init(uint8_t pin, uint8_t ledc_channel) {
  s_channel = ledc_channel;
  // Slow PWM on EN. 20 kHz made the boost drop out; DC HIGH pulled 5V down.
  ledcSetup(s_channel, 200, 8);
  ledcAttachPin(pin, s_channel);
  ledcWrite(s_channel, 0);
  set(true);
}

void set(bool on) {
  s_on = on;
  if (!on) {
    s_ramping = false;
    ledcWrite(s_channel, 0);
    return;
  }
  s_ramping = true;
  s_rampStartMs = millis();
  ledcWrite(s_channel, 0);
}

void tick() {
  if (!s_ramping) return;
  uint32_t elapsed = millis() - s_rampStartMs;
  if (elapsed >= kRampMs) {
    ledcWrite(s_channel, dutyForBrightness());
    s_ramping = false;
    return;
  }
  ledcWrite(s_channel, (uint8_t)(((uint32_t)dutyForBrightness() * elapsed) / kRampMs));
}

void setBrightness(uint8_t percent) {
  s_brightnessPercent = percent > 100 ? 100 : percent;
  if (s_on && !s_ramping) ledcWrite(s_channel, dutyForBrightness());
}

bool isOn() { return s_on; }

} // namespace backlight
