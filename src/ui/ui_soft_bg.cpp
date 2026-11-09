/*
 * Soft colour-wash background (dashboard C palette, no artwork).
 *
 * Each patch is an elliptical Gaussian exp(-2*((x-cx)^2/rx^2 + (y-cy)^2/ry^2)),
 * which separates into a row table times a column table, so the whole image
 * costs a few multiplies per pixel. Ordered dithering hides RGB565 banding.
 */
#include "ui_soft_bg.h"

#include <math.h>
#include <stdint.h>

#include "esp_heap_caps.h"

namespace ui_soft_bg {
namespace {

constexpr int W = 800;
constexpr int H = 480;

struct Wash {
  float cx, cy, rx, ry;
  uint32_t color;
  float alpha;
};

// Same colours as gen_dash_c_art.py make_background(), spread around the
// screen instead of gathered around the airflow picture.
constexpr Wash kWashes[] = {
    {140, 40, 300, 160, 0x8EE6F0, 0.90f},   // cyan, top left
    {720, 30, 260, 150, 0xFFD3B5, 0.75f},   // light peach, top right
    {430, 250, 230, 170, 0xFFE0C8, 0.55f},  // soft peach, centre
    {790, 310, 180, 230, 0xFFC49A, 0.75f},  // peach, right edge
    {30, 410, 220, 180, 0xBFEFEA, 0.80f},   // mint, bottom left
    {610, 470, 260, 140, 0xE2D4FF, 0.80f},  // lavender, bottom right
    {280, 470, 200, 110, 0x8EE6F0, 0.45f},  // faint cyan, bottom
};
constexpr int N = sizeof(kWashes) / sizeof(kWashes[0]);

constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

lv_img_dsc_t s_dsc;
bool s_built = false;

inline float ch(uint32_t c, int sh) { return (float)((c >> sh) & 0xFF); }

}  // namespace

const lv_img_dsc_t *get() {
  if (s_built) return &s_dsc;

  const size_t bytes = (size_t)W * H * sizeof(lv_color_t);
  lv_color_t *px = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  float *colT = (float *)heap_caps_malloc(sizeof(float) * N * W, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!px || !colT) {
    heap_caps_free(px);
    heap_caps_free(colT);
    return nullptr;
  }
  for (int k = 0; k < N; k++)
    for (int x = 0; x < W; x++) {
      float dx = (x - kWashes[k].cx) / kWashes[k].rx;
      colT[k * W + x] = expf(-2.f * dx * dx);
    }

  for (int y = 0; y < H; y++) {
    float rowT[N];
    for (int k = 0; k < N; k++) {
      float dy = (y - kWashes[k].cy) / kWashes[k].ry;
      rowT[k] = expf(-2.f * dy * dy) * kWashes[k].alpha;
    }
    // Cream base, slightly darker toward the bottom (as dashboard C).
    float t = (float)y / (H - 1);
    float br = ch(0xF4EFE7, 16) * (1 - t) + ch(0xEFE8DE, 16) * t;
    float bg = ch(0xF4EFE7, 8) * (1 - t) + ch(0xEFE8DE, 8) * t;
    float bb = ch(0xF4EFE7, 0) * (1 - t) + ch(0xEFE8DE, 0) * t;
    for (int x = 0; x < W; x++) {
      float r = br, g = bg, b = bb;
      for (int k = 0; k < N; k++) {
        float a = rowT[k] * colT[k * W + x];
        if (a < 0.002f) continue;
        uint32_t c = kWashes[k].color;
        r += (ch(c, 16) - r) * a;
        g += (ch(c, 8) - g) * a;
        b += (ch(c, 0) - b) * a;
      }
      // Dither: RGB565 steps are 8 (R/B) and 4 (G).
      float d = (kBayer[y & 3][x & 3] + 0.5f) / 16.f - 0.5f;
      int ri = (int)(r + d * 8.f), gi = (int)(g + d * 4.f), bi = (int)(b + d * 8.f);
      ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
      gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
      bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);
      px[y * W + x] = lv_color_make(ri, gi, bi);
    }
  }
  heap_caps_free(colT);

  s_dsc.header.always_zero = 0;
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.header.w = W;
  s_dsc.header.h = H;
  s_dsc.data_size = bytes;
  s_dsc.data = (const uint8_t *)px;
  s_built = true;
  return &s_dsc;
}

}  // namespace ui_soft_bg
