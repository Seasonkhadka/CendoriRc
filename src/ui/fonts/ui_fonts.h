/*
 * Custom subset fonts for the CendoriRC dashboard.
 *
 * Generated from Windows "Malgun Gothic" (regular / bold) with lv_font_conv,
 * containing only the glyphs the UI actually uses:
 *   - Basic Latin 0x20-0x7E + degree sign (°)
 *   - Hangul syllables from src/ui labels (see tools/gen_fonts.sh)
 *
 * Regenerate with tools/gen_fonts.sh if new Korean text is added.
 */
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(ui_font_xs);   /* 14px regular - units, tick labels          */
LV_FONT_DECLARE(ui_font_sm);   /* 18px regular - body text                   */
LV_FONT_DECLARE(ui_font_md);   /* 22px bold    - emphasized values           */
LV_FONT_DECLARE(ui_font_xxl);  /* 34px bold    - hero temperature readout    */

#ifdef __cplusplus
}
#endif
