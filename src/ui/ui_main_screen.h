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
// Air-quality index 0-100 (-1 = no reading, "--"), level label and colour.
void set_aqi(int aqi, const char *label, uint32_t color);
void set_indoor(float temp_c, int humidity_pct);

// --- Heat-recovery card ---
void set_humidify_reservation(int hour24, int minute);

// Live RC state: mode badge, fan level 0-3, and the active-state VSP
// (0.05 V units; 0 = fan stopped / no voltage).
void set_mode_badge(const char *text);
void set_fan_level(int level);
void set_fan_vsp(int supply_units, int exhaust_units);

} // namespace ui_main_screen
