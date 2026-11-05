/*
 * LVGL memory pool in PSRAM (lv_conf.h LV_MEM_POOL_ALLOC).
 *
 * LVGL keeps its own TLSF allocator and lv_mem_monitor(); only the pool
 * itself moves from internal SRAM to PSRAM, so it can be larger (three
 * dashboards + settings) and the internal RAM goes to Wi-Fi / drivers.
 * Falls back to internal RAM if PSRAM is not available.
 */
#pragma once

#include <stddef.h>

#include "esp_heap_caps.h"

static inline void *lv_mem_psram_pool(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  return p;
}
