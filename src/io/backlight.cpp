#include "backlight.h"

#include "driver/ledc.h"
#include "port/esp_time.h"

namespace backlight {

namespace {
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE; // only mode on ESP32-S3
constexpr ledc_timer_bit_t kDutyRes = LEDC_TIMER_8_BIT;

ledc_channel_t s_channel = LEDC_CHANNEL_0;
ledc_timer_t s_timer = LEDC_TIMER_0;
bool s_on = false;
// Low on purpose: PLC-derived +5V cannot feed a full MP3302 load.
uint8_t s_brightnessPercent = 50;

constexpr uint32_t kRampMs = 600; // spreads power-up inrush out
bool s_ramping = false;
uint32_t s_rampStartMs = 0;

uint8_t dutyForBrightness() {
  return (uint8_t)((uint16_t)s_brightnessPercent * 255 / 100);
}

void writeDuty(uint8_t duty) {
  ledc_set_duty(kSpeedMode, s_channel, duty);
  ledc_update_duty(kSpeedMode, s_channel);
}
} // namespace

void init(uint8_t pin, uint8_t ledc_channel) {
  s_channel = (ledc_channel_t)ledc_channel;
  s_timer = (ledc_timer_t)ledc_channel; // one timer per channel - simplest, plenty free

  // Slow PWM on EN. 20 kHz made the boost drop out; DC HIGH pulled 5V down.
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = kSpeedMode;
  timer_cfg.duty_resolution = kDutyRes;
  timer_cfg.timer_num = s_timer;
  timer_cfg.freq_hz = 200;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.gpio_num = pin;
  ch_cfg.speed_mode = kSpeedMode;
  ch_cfg.channel = s_channel;
  ch_cfg.timer_sel = s_timer;
  ch_cfg.duty = 0;
  ch_cfg.hpoint = 0;
  ledc_channel_config(&ch_cfg);

  writeDuty(0);
  set(true);
}

void set(bool on) {
  s_on = on;
  if (!on) {
    s_ramping = false;
    writeDuty(0);
    return;
  }
  s_ramping = true;
  s_rampStartMs = millis();
  writeDuty(0);
}

void tick() {
  if (!s_ramping) return;
  uint32_t elapsed = millis() - s_rampStartMs;
  if (elapsed >= kRampMs) {
    writeDuty(dutyForBrightness());
    s_ramping = false;
    return;
  }
  writeDuty((uint8_t)(((uint32_t)dutyForBrightness() * elapsed) / kRampMs));
}

void setBrightness(uint8_t percent) {
  s_brightnessPercent = percent > 100 ? 100 : percent;
  if (s_on && !s_ramping) writeDuty(dutyForBrightness());
}

bool isOn() { return s_on; }

} // namespace backlight
