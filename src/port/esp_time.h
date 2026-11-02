/*
 * Arduino-shaped timing shims over ESP-IDF, so each ported module doesn't
 * have to rewrite every millis()/delay()/delayMicroseconds() call site by
 * hand. Pull this in instead of <Arduino.h>.
 */
#pragma once

#include <stdint.h>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

inline void delay(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

inline void delayMicroseconds(uint32_t us) {
  esp_rom_delay_us(us);
}
