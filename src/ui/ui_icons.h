/*
 * Small hand-drawn vector icons (LVGL primitives only - no image assets),
 * used across the dashboard status bar / cards / stat strip.
 *
 * Every function creates a small square container of `size` px and returns
 * it; the caller positions it with lv_obj_align()/lv_obj_set_pos().
 */
#pragma once

#include <lvgl.h>

namespace ui_icons {

lv_obj_t *wifi(lv_obj_t *parent, lv_coord_t px, lv_color_t color);
lv_obj_t *refresh(lv_obj_t *parent, lv_coord_t px, lv_color_t color);
lv_obj_t *fan(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *clock(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *sun(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *dust(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *cloud(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *gasring(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *thermo(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *lock(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *wifi_bars(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *phone(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *search(lv_obj_t *parent, lv_coord_t size, lv_color_t color);
lv_obj_t *disconnect(lv_obj_t *parent, lv_coord_t size, lv_color_t color);

} // namespace ui_icons
