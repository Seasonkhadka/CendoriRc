/*
 * Main dashboard - design C (light "glass" theme, per the 2026-11-02
 * reference mockup): glowing AQI ring with indoor temp / RH, supply /
 * exhaust airflow artwork, SMART AI VENTILATION card and pastel sensor
 * pills. Static artwork (background, airflow crescents, ring gradients,
 * ring glow, icons) is pre-rendered by tools/gen_dash_c_art.py into
 * src/ui/img/.
 *
 * Third dashboard next to ui_main_screen (A) and ui_main_screen_v2 (B),
 * switched with the physical LEFT/RIGHT buttons - see main.cpp. The top
 * bar and the bottom cards have the same elements and sizes as the A/B
 * ones (ui_dashboard_chrome), only in the light style.
 */
#pragma once

#include <lvgl.h>

namespace ui_main_screen_v3 {

// Builds the dashboard into the given parent (a fresh blank screen from
// lv_obj_create(NULL), same convention as the other dashboards).
lv_obj_t *create(lv_obj_t *parent);

// ---- top bar (same data and formats as ui_dashboard_chrome) ----
void set_wifi_connected(bool connected);                       // icon + "연결됨" / "미연결"
void set_weather(const char *summary, float outdoor_c);        // e.g. "외기", 22.0f
void set_time(int year, int month, int day, int hour24, int minute); // "2026/11/02 PM 03:30"
void set_filter_status(const char *text, bool ok);

// AQI 0-500 (-1 = no reading) and its AQI::Level (0 none, 1 good,
// 2 normal, 3 bad, 4 very bad): number, label, ring colour / glow / fill.
void set_aqi(int aqi, int level);

// Ring centre readout. temp_c <= -99 or humidity_pct < 0 shows "--".
void set_indoor(float temp_c, int humidity_pct);

// Mode button: the current mode name (e.g. "환기모드", "양압 모드 +8%"), lit while
// running. detail = optional small second line (pressure modes: both airflows
// and "CO₂ 부스트"), "" = none.
void set_mode(const char *mode_text, bool running, const char *detail);

// The one shared fan speed 0-3 (0 = off) and fan error (either fan).
void set_fan(int level, bool err);

// Middle panel: each fan's VSP at the current speed, in PLC units of
// 0.05 V (48 -> "2.40V"); 0 = this fan is off / unknown -> "--".
void set_fan_vsp(int supply_units, int exhaust_units);

// Reservation row: type 0 none, 1 start (가동예약), 2 stop (정지예약);
// minutes = reservation time.
void set_reservation(int type, int minutes);

// Bottom cards (same items and units as the A/B bottom bar); any value < 0
// shows "--". The Temp card follows set_indoor().
void set_sensors(int pm25, int pm10, int co2, int tvoc_ppb, int co_ppm);

} // namespace ui_main_screen_v3
