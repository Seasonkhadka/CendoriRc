/*
 * Shared dashboard "chrome" - the top status bar and bottom sensor strip
 * that look identical across every main-screen design (ui_main_screen,
 * ui_main_screen_v2, ...). Only the middle AQI/heat-recovery content is
 * allowed to differ between designs; this is the one place that content
 * so it can't drift out of sync between them.
 *
 * Each design's create() calls build_top_bar()/build_bottom_bar() into
 * its own root. Both dashboards stay alive, so setters write every
 * chrome copy (dash A and dash B), not only the last one built.
 */
#pragma once

#include <lvgl.h>

namespace ui_dashboard_chrome {

lv_obj_t *build_top_bar(lv_obj_t *parent);
lv_obj_t *build_bottom_bar(lv_obj_t *parent);

// Forgets the built bars, before both dashboards are rebuilt (language
// change). The old screens must be deleted by the caller.
void reset();

// --- Top status bar ---
void set_wifi_connected(bool connected);                  // icon + "연결됨" / "미연결"
void set_weather(const char *summary, float outdoor_c);    // e.g. "맑음", 22.0f
void set_time(int year, int month, int day, int hour24, int minute); // "2026/10/27 PM 02:30"
void set_filter_status(const char *text, bool ok);         // e.g. "필터 정상"

// --- Bottom sensor strip ---
void set_pm25(float ug_m3);
void set_pm10(float ug_m3);
void set_co2(int ppm);
void set_tvoc(int ppb); // TVOC ppb, < 0 = "--"
void set_co(int ppm);  // CO ppm, < 0 = "--"
void set_temp_humidity(float temp_c, int humidity_pct);

} // namespace ui_dashboard_chrome
