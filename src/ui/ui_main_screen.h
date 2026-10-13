/*
 * Main dashboard - design 1 (circular gauges). The top status bar and
 * bottom sensor strip are shared chrome built via ui_dashboard_chrome.h -
 * see that header for their setters. Only the AQI/comfort and
 * heat-recovery cards belong to this design specifically.
 */
#pragma once

#include <lvgl.h>

namespace ui_main_screen {

lv_obj_t *create(lv_obj_t *parent);

// --- AQI / comfort card ---
void set_aqi(int aqi, const char *label);                  // e.g. 32, "쾌적"
void set_indoor(float temp_c, int humidity_pct);

// --- Heat-recovery card ---
void set_fan_supply(int level_pct, const char *level_label);  // 급기
void set_fan_exhaust(int level_pct, const char *level_label); // 배기
void set_humidify_reservation(int hour24, int minute);

} // namespace ui_main_screen
