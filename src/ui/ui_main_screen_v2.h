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

// Updates the trend-chart tooltip bubble (the "24.5°C / 48% RH" readout).
void set_indoor(float temp_c, int humidity_pct);

} // namespace ui_main_screen_v2
