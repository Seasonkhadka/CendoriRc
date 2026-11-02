/*
 * CO cell analog input (GPIO8, ADC1_CHANNEL_7 on ESP32-S3), via
 * adc_oneshot + line-fitting calibration - replaces Arduino's
 * analogReadMilliVolts().
 */
#pragma once

namespace co_adc {

void init(int gpioPin);
int readMilliVolts(); // -1 if init() failed or wasn't called

} // namespace co_adc
