/*
 * TFT backlight control (TFT_BL -> R11 100R -> MP3302 EN, GPIO46, LEDC ch0).
 *
 * The MP3302 boost converter's input is the same +5V rail PLC power feeds
 * (via BD1/D8/U9) - there's no separate supply for it. Snapping EN straight
 * to its target duty at power-up asks the boost converter for full current
 * at the exact moment the PLC rectifier's own smoothing cap is still
 * charging from cable insertion, and the rail sags enough to blank the
 * panel. init()/set(true) now ramp duty up over kRampMs instead of
 * stepping it, so the two inrushes don't stack. Call tick() every loop()
 * iteration to advance the ramp - it is non-blocking.
 */
#pragma once

#include <Arduino.h>

namespace backlight {

void init(uint8_t pin, uint8_t ledc_channel = 0);
void set(bool on);
bool isOn();

// 0-100. Lower further if the panel still blanks on PLC connect.
void setBrightness(uint8_t percent);

// Non-blocking: advances the power-on soft-start ramp. Call every loop().
void tick();

} // namespace backlight
