/*
 * Shared color palette for the CendoriRC dashboard.
 * Cool navy HVAC panel, lifted so cards and text read brighter.
 */
#pragma once

#include <lvgl.h>

namespace ui_theme {

// Backgrounds
constexpr uint32_t BG          = 0x1A2433; // screen background
constexpr uint32_t CARD_BG     = 0x2C3A50; // card surface
constexpr uint32_t CARD_BG_ALT = 0x243246; // inset / stat-bar surface
constexpr uint32_t BORDER      = 0x4E6280; // card / badge border
constexpr uint32_t TRACK       = 0x3A4C64; // gauge background track

// Text
constexpr uint32_t TEXT_PRIMARY   = 0xFFFFFF;
constexpr uint32_t TEXT_SECONDARY = 0xC5D0E0;
constexpr uint32_t TEXT_DIM       = 0x93A0B5;

// Accents
constexpr uint32_t ACCENT_CYAN     = 0x7DEEFA;
constexpr uint32_t ACCENT_CYAN_DIM = 0x3D8A9A;
constexpr uint32_t GOOD            = 0x6EE7A8;
constexpr uint32_t WARN            = 0xFCD34D;
constexpr uint32_t DANGER          = 0xFB8B8B;

// Air-quality index levels (app: #0341cd / #00ac38 / #ff6000 / #d90000 /
// #aaaaaa). Same hues, lifted so they read on the cards.
constexpr uint32_t AQI_GOOD     = 0x5B9BFF; // 좋음 - blue
constexpr uint32_t AQI_NORMAL   = 0x34D399; // 보통 - green
constexpr uint32_t AQI_BAD      = 0xFF9440; // 나쁨 - orange
constexpr uint32_t AQI_VERY_BAD = 0xFF6B6B; // 매우 나쁨 - red
constexpr uint32_t AQI_NONE     = 0xA8B4C4; // no reading - gray

// "전열교환" badge pill
constexpr uint32_t BADGE_BG     = 0x1E4A5C;
constexpr uint32_t BADGE_BORDER = 0x5AB0C8;
constexpr uint32_t BADGE_TEXT   = 0xD4F6FF;

// Selected option "pill" (settings screen radio/segmented controls)
constexpr uint32_t PILL_BG        = 0xFFFFFF;
constexpr uint32_t PILL_TEXT      = 0x1A2433;
constexpr uint32_t PILL_TEXT_SUB  = 0x4B5563;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

inline void elevate(lv_obj_t *obj) {
  if (!obj) return;
  lv_obj_set_style_shadow_width(obj, 18, 0);
  lv_obj_set_style_shadow_ofs_y(obj, 4, 0);
  lv_obj_set_style_shadow_color(obj, c(ACCENT_CYAN), 0);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
}

} // namespace ui_theme
