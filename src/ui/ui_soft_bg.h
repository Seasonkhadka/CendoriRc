#pragma once
#include "lvgl.h"

namespace ui_soft_bg {

// 800x480 background with only dashboard C's soft colour patches (cyan, peach,
// lavender, mint) scattered over its cream base. No fan / airflow artwork.
// Built once into PSRAM on first call (no flash cost). Returns nullptr if
// PSRAM allocation fails; callers then keep a plain colour.
const lv_img_dsc_t *get();

}  // namespace ui_soft_bg
