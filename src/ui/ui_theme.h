/*
 * Shared color palette for the CendoriRC dashboard.
 * Dark HVAC-panel theme matching the approved UI mockup.
 */
#pragma once

#include <lvgl.h>

namespace ui_theme {

// Backgrounds
constexpr uint32_t BG          = 0x0A0D12; // screen background
constexpr uint32_t CARD_BG     = 0x161B23; // card surface
constexpr uint32_t CARD_BG_ALT = 0x11151C; // inset / stat-bar surface
constexpr uint32_t BORDER      = 0x242A35; // card / badge border
constexpr uint32_t TRACK       = 0x242A35; // gauge background track

// Text
constexpr uint32_t TEXT_PRIMARY   = 0xF3F6FA;
constexpr uint32_t TEXT_SECONDARY = 0x8A93A6;
constexpr uint32_t TEXT_DIM       = 0x5B6472;

// Accents
constexpr uint32_t ACCENT_CYAN     = 0x5AD1E8;
constexpr uint32_t ACCENT_CYAN_DIM = 0x2E5C68;
constexpr uint32_t GOOD            = 0x4ADE80;
constexpr uint32_t WARN            = 0xFBBF24;
constexpr uint32_t DANGER          = 0xF87171;

// "전열교환" badge pill
constexpr uint32_t BADGE_BG     = 0x14232E;
constexpr uint32_t BADGE_BORDER = 0x2C5468;
constexpr uint32_t BADGE_TEXT   = 0xA9E4F5;

// Selected option "pill" (settings screen radio/segmented controls)
constexpr uint32_t PILL_BG        = 0xF3F6FA; // near-white, glowing
constexpr uint32_t PILL_TEXT      = 0x14181F;
constexpr uint32_t PILL_TEXT_SUB  = 0x4B5563;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

} // namespace ui_theme
