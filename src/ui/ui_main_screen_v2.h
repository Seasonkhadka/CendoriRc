/*
 * Main dashboard - alternate design (24h trend chart + segmented fan
 * level meters, per the second reference mockup). Selectable alongside
 * the original circular-gauge dashboard (ui_main_screen) via the
 * physical LEFT/RIGHT buttons - see main.cpp.
 */
#pragma once

#include <lvgl.h>

namespace ui_main_screen_v2 {

// Builds the dashboard into the given parent (usually a fresh blank
// screen from lv_obj_create(NULL), same convention as ui_main_screen).
lv_obj_t *create(lv_obj_t *parent);

// Card header "AQI 89 / 보통": index 0-500 (-1 = no reading), label, colour.
void set_aqi(int aqi, const char *label, uint32_t color);

// Updates the trend-chart tooltip bubble (the "24.5°C / 48% RH" readout).
void set_indoor(float temp_c, int humidity_pct);

// Live RC state: mode badge, fan level 0-3, and the active-state VSP
// (0.05 V units; 0 = fan stopped / no voltage).
void set_mode_badge(const char *text);
void set_fan_level(int level);
void set_fan_vsp(int supply_units, int exhaust_units);

} // namespace ui_main_screen_v2
