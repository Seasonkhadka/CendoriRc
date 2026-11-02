/*
 * Settings screen: breadcrumb header, 5-item sidebar, dynamic content
 * panel, footer button legend. Airflow, Schedule, Wi-Fi and Filter
 * sections are fully designed.
 *
 * Button model (wired from main.cpp while this screen is open):
 *
 *   LEFT SIDEBAR
 *     Up / Down  -> select_next() / select_prev() (or engineer_* while nested)
 *     OK         -> enter the right-hand panel (or Engineer Mode from its portal)
 *     Back       -> leave settings (or exit Engineer Mode)
 *
 *   RIGHT PANEL (after OK)
 *     Up / Down / Left / Right -> move the highlight and change the focused value
 *     OK         -> save the working values and return to the sidebar
 *     Back       -> discard changes and return to the sidebar
 *
 * Engineer Mode (last menu item) is a locked sub-area: selecting it in the outer
 * sidebar shows a simple "portal" panel (OK to enter), which replaces the
 * whole sidebar + breadcrumb with its own nested 2-item sub-navigation
 * (ID 설정 / VSP 설정). From that nested sidebar, OK enters the right panel
 * the same way as the outer pages.
 */
#pragma once

#include <lvgl.h>

#include "net/WifiDto.h"
#include "rc/RcDto.h"

namespace ui_settings_screen {

lv_obj_t *create();

// RC state refresh (UI task only). Redraws the panel if settings is shown.
void set_state(const RcDto &state);

// Switches the outer sidebar selection / content panel directly (0-6:
// Airflow, Schedule, Wi-Fi, Filter, Clock, Language, Engineer Mode). No-op
// while nested in Engineer Mode, or while the right-hand panel is being edited.
void select_menu(int idx);

void select_next();
void select_prev();

bool is_in_engineer();

void enter_engineer();
void exit_engineer();

void engineer_select_next();
void engineer_select_prev();

// True while the user is editing the right-hand panel (OK has been pressed
// on a sidebar item, and they have not saved or cancelled yet).
bool is_in_panel();

// True once after a new language was saved on the Language page. main.cpp
// then rebuilds every screen (labels are read when a screen is built).
bool take_language_changed();

// Breadcrumb clock (RTC date + time), "2026/10/27 AM/PM hh:mm".
void set_time(int year, int month, int day, int hour24, int minute);

// Wi-Fi state (WifiController data callback, UI task only): breadcrumb icon
// and the Wi-Fi page (status, QR setup, network list, password keyboard).
void set_wifi_state(const WifiDto &state);

// Physical-button handlers. main.cpp calls these while settings is open.
// handle_back() returns true if settings consumed the key (stay on this
// screen). Returns false when Back should leave settings entirely.
void handle_up();
void handle_down();
void handle_left();
void handle_right();
void handle_ok();
bool handle_back();

} // namespace ui_settings_screen
