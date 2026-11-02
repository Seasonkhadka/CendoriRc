/*
 * Shared I2C sensor bus (SDA=40, SCL=41) + CO analog input.
 *
 * Confirmed devices (see the I2C scan + each chip's public datasheet -
 * these are standard parts, not reverse-engineered like the touch IC):
 *   SHT40   0x44  temperature / humidity
 *   SGP40   0x59  VOC raw signal (no calibrated index yet - see note below)
 *   SCD40   0x62  CO2 (ppm, direct from the sensor) - auto-detected, 0x64 also tried
 *   MCP7940 0x6F  RTC (BCD registers, no CRC)
 *   PM2012  0x28  PM1.0/PM2.5/PM10 (ug/m3, direct from the sensor) - was
 *                 gated off until a real I2C scan confirmed 0x28 ACKs (pin
 *                 8/P bodge-wired to GND); see kPm2012HardwareReady there.
 *
 * NOT handled here yet (deferred, per plan):
 *   - CO ppm: only the raw ADC millivolt reading is exposed; converting
 *     it to ppm needs the sensor cell's sensitivity spec.
 *   - AQI: derived from PM2.5 - available now, but AQI math itself hasn't
 *     been requested yet.
 *
 * poll() is a non-blocking round-robin scheduler - call it every loop()
 * iteration. It performs at most one sensor transaction per call (so a
 * single call's worst-case blocking time is one short measurement wait,
 * never several stacked up), and each sensor is only actually re-read
 * once its own interval has elapsed. Returns true the calls where a
 * reading actually changed, so callers can skip redundant UI refreshes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace sensors {

void init(int coAdcPin);
bool poll();

// Sensirion sensors (SHT40 / SGP40 / SCD40): valid only after the first
// successful read: check hasX() before using getX().
bool hasTemp();
float getTempC();
float getHumidityPct();

bool hasCo2();
int getCo2Ppm();

bool hasVocRaw();
uint16_t getVocRaw(); // raw SGP40 signal ticks - NOT a calibrated index
int getVocIndex();    // Sensirion VOC Index, 1-500 (100 = normal baseline) - valid whenever hasVocRaw() is
int getTvocUg();      // TVOC ug/m3 converted from the VOC Index (protocol B6 / D9 unit)

bool hasRtc();
// Sets the MCP7940 RTC (24-hour, local time) and re-reads it. Call from the
// same task as poll() (I2C bus). Returns false if the RTC does not answer.
bool setRtcTime(int year, int month, int day, int hour, int minute, int second);
int getHour24();
int getMinute();
int getSecond();
int getDay();
int getMonth();
int getYear();

bool hasCo();
int getCoMilliVolts(); // filtered SEN_VOUT millivolts (log / calibration)
float getCoPpm();      // ME2-CO ppm - valid after the warm-up (hasCo())

bool hasPm(); // false until kPm2012HardwareReady is flipped on in sensors.cpp
int getPm1Ug();
int getPm25Ug();
int getPm10Ug();

} // namespace sensors
