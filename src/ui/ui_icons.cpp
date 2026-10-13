#include "ui_icons.h"
#include <math.h>

namespace ui_icons {

// A transparent, non-scrollable size x size container every icon is built on.
static lv_obj_t *box(lv_obj_t *parent, lv_coord_t size) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, size, size);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  return o;
}

static lv_obj_t *dot(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_coord_t r, lv_color_t color) {
  lv_obj_t *d = lv_obj_create(parent);
  lv_obj_remove_style_all(d);
  lv_obj_set_size(d, r * 2, r * 2);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(d, color, 0);
  lv_obj_set_pos(d, cx - r, cy - r);
  return d;
}

lv_obj_t *wifi(lv_obj_t *parent, lv_coord_t px, lv_color_t color) {
  lv_obj_t *b = box(parent, px);
  lv_obj_t *lbl = lv_label_create(b);
  lv_label_set_text(lbl, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(lbl, px >= 18 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_center(lbl);
  return b;
}

lv_obj_t *refresh(lv_obj_t *parent, lv_coord_t px, lv_color_t color) {
  lv_obj_t *b = box(parent, px);
  lv_obj_t *lbl = lv_label_create(b);
  lv_label_set_text(lbl, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_font(lbl, px >= 18 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_center(lbl);
  return b;
}

// A stylized 3-blade fan/propeller (used at the center of the supply/
// exhaust gauges): three short curved blades around a small hub, all drawn
// with lv_arc strokes so no image asset or rotation support is needed.
lv_obj_t *fan(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);

  lv_coord_t blade_w = LV_MAX(3, size / 5);
  static const int starts[3] = {20, 140, 260};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *blade = lv_arc_create(b);
    lv_obj_remove_style_all(blade);
    lv_obj_set_size(blade, size, size);
    lv_obj_center(blade);
    lv_obj_clear_flag(blade, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(blade, starts[i], starts[i] + 80);
    lv_obj_set_style_arc_width(blade, blade_w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(blade, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(blade, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(blade, true, LV_PART_MAIN);
  }

  dot(b, size / 2, size / 2, LV_MAX(2, size / 7), color);
  return b;
}

lv_obj_t *clock(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);

  lv_obj_t *ring = lv_obj_create(b);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, size, size);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, LV_MAX(1, size / 10), 0);
  lv_obj_set_style_border_color(ring, color, 0);
  lv_obj_set_pos(ring, 0, 0);

  lv_coord_t c = size / 2;
  lv_point_t *min_pts = new lv_point_t[2]{{c, c}, {c, (lv_coord_t)(size * 0.18f)}};
  lv_point_t *hr_pts = new lv_point_t[2]{{c, c}, {(lv_coord_t)(size * 0.66f), (lv_coord_t)(size * 0.34f)}};

  lv_obj_t *min_line = lv_line_create(b);
  lv_line_set_points(min_line, min_pts, 2);
  lv_obj_set_style_line_width(min_line, LV_MAX(1, size / 9), 0);
  lv_obj_set_style_line_color(min_line, color, 0);
  lv_obj_set_style_line_rounded(min_line, true, 0);

  lv_obj_t *hr_line = lv_line_create(b);
  lv_line_set_points(hr_line, hr_pts, 2);
  lv_obj_set_style_line_width(hr_line, LV_MAX(1, size / 9), 0);
  lv_obj_set_style_line_color(hr_line, color, 0);
  lv_obj_set_style_line_rounded(hr_line, true, 0);

  return b;
}

lv_obj_t *sun(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  lv_coord_t c = size / 2;
  lv_coord_t r_core = (lv_coord_t)(size * 0.20f);
  lv_coord_t r_in = (lv_coord_t)(size * 0.30f);
  lv_coord_t r_out = (lv_coord_t)(size * 0.48f);

  dot(b, c, c, r_core, color);

  for (int i = 0; i < 8; i++) {
    float a = (float)i * 3.14159265f / 4.0f;
    lv_coord_t x1 = c + (lv_coord_t)(r_in * cosf(a));
    lv_coord_t y1 = c + (lv_coord_t)(r_in * sinf(a));
    lv_coord_t x2 = c + (lv_coord_t)(r_out * cosf(a));
    lv_coord_t y2 = c + (lv_coord_t)(r_out * sinf(a));
    lv_point_t *pts = new lv_point_t[2]{{x1, y1}, {x2, y2}};
    lv_obj_t *ray = lv_line_create(b);
    lv_line_set_points(ray, pts, 2);
    lv_obj_set_style_line_width(ray, LV_MAX(1, size / 12), 0);
    lv_obj_set_style_line_color(ray, color, 0);
    lv_obj_set_style_line_rounded(ray, true, 0);
  }
  return b;
}

lv_obj_t *dust(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  struct P { float x, y, r; };
  static const P pts[3] = {{0.30f, 0.62f, 0.14f}, {0.58f, 0.30f, 0.18f}, {0.72f, 0.66f, 0.11f}};
  for (auto &p : pts) {
    lv_coord_t r = (lv_coord_t)(size * p.r);
    dot(b, (lv_coord_t)(size * p.x), (lv_coord_t)(size * p.y), r, color);
  }
  return b;
}

lv_obj_t *cloud(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);

  lv_obj_t *base = lv_obj_create(b);
  lv_obj_remove_style_all(base);
  lv_coord_t bw = (lv_coord_t)(size * 0.62f), bh = (lv_coord_t)(size * 0.32f);
  lv_obj_set_size(base, bw, bh);
  lv_obj_set_style_radius(base, bh / 2, 0);
  lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(base, color, 0);
  lv_obj_set_pos(base, (size - bw) / 2, (lv_coord_t)(size * 0.52f));

  struct P { float x, y, r; };
  static const P puffs[2] = {{0.36f, 0.40f, 0.19f}, {0.60f, 0.34f, 0.23f}};
  for (auto &p : puffs) {
    lv_coord_t r = (lv_coord_t)(size * p.r);
    dot(b, (lv_coord_t)(size * p.x), (lv_coord_t)(size * p.y), r, color);
  }
  return b;
}

lv_obj_t *gasring(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  lv_coord_t r1 = (lv_coord_t)(size * 0.20f);
  lv_coord_t r2 = (lv_coord_t)(size * 0.15f);
  lv_coord_t x1 = (lv_coord_t)(size * 0.30f), y1 = (lv_coord_t)(size * 0.50f);
  lv_coord_t x2 = (lv_coord_t)(size * 0.68f), y2 = (lv_coord_t)(size * 0.42f);

  lv_point_t *pts = new lv_point_t[2]{{x1, y1}, {x2, y2}};
  lv_obj_t *bond = lv_line_create(b);
  lv_line_set_points(bond, pts, 2);
  lv_obj_set_style_line_width(bond, LV_MAX(1, size / 10), 0);
  lv_obj_set_style_line_color(bond, color, 0);
  lv_obj_set_style_line_rounded(bond, true, 0);

  dot(b, x1, y1, r1, color);
  dot(b, x2, y2, r2, color);
  return b;
}

lv_obj_t *thermo(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);

  lv_coord_t stem_w = (lv_coord_t)(size * 0.22f);
  lv_coord_t stem_h = (lv_coord_t)(size * 0.55f);
  lv_coord_t bulb_d = (lv_coord_t)(size * 0.40f);

  lv_obj_t *stem = lv_obj_create(b);
  lv_obj_remove_style_all(stem);
  lv_obj_set_size(stem, stem_w, stem_h);
  lv_obj_set_style_radius(stem, stem_w / 2, 0);
  lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(stem, color, 0);
  lv_obj_set_pos(stem, (size - stem_w) / 2, (lv_coord_t)(size * 0.06f));

  lv_obj_t *bulb = lv_obj_create(b);
  lv_obj_remove_style_all(bulb);
  lv_obj_set_size(bulb, bulb_d, bulb_d);
  lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bulb, color, 0);
  lv_obj_set_pos(bulb, (size - bulb_d) / 2, size - bulb_d - (lv_coord_t)(size * 0.04f));

  return b;
}

lv_obj_t *lock(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);

  lv_coord_t body_w = (lv_coord_t)(size * 0.62f);
  lv_coord_t body_h = (lv_coord_t)(size * 0.46f);
  lv_coord_t body_y = (lv_coord_t)(size * 0.46f);

  lv_obj_t *shackle = lv_obj_create(b);
  lv_obj_remove_style_all(shackle);
  lv_coord_t sh_d = (lv_coord_t)(size * 0.44f);
  lv_obj_set_size(shackle, sh_d, sh_d);
  lv_obj_set_style_radius(shackle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(shackle, LV_MAX(2, size / 8), 0);
  lv_obj_set_style_border_color(shackle, color, 0);
  lv_obj_set_style_border_side(shackle, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_pos(shackle, (size - sh_d) / 2, (lv_coord_t)(size * 0.06f));

  // The opaque body is created after (drawn on top of) the shackle, so it
  // naturally covers the shackle's lower half - no explicit mask needed.
  lv_obj_t *body = lv_obj_create(b);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, body_w, body_h);
  lv_obj_set_style_radius(body, LV_MAX(2, size / 8), 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(body, color, 0);
  lv_obj_set_pos(body, (size - body_w) / 2, body_y);

  return b;
}

lv_obj_t *wifi_bars(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  constexpr int n = 4;
  lv_coord_t bar_w = LV_MAX(2, size / 6);
  lv_coord_t gap = LV_MAX(1, size / 10);
  lv_coord_t total_w = n * bar_w + (n - 1) * gap;
  lv_coord_t x = (size - total_w) / 2;
  for (int i = 0; i < n; i++) {
    lv_coord_t h = (lv_coord_t)(size * (0.32f + 0.18f * i));
    lv_obj_t *bar = lv_obj_create(b);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, bar_w, h);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_pos(bar, x, size - h);
    x += bar_w + gap;
  }
  return b;
}

lv_obj_t *phone(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  lv_coord_t w = (lv_coord_t)(size * 0.55f);
  lv_coord_t h = (lv_coord_t)(size * 0.9f);
  lv_coord_t body_y = (size - h) / 2;

  lv_obj_t *body = lv_obj_create(b);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, w, h);
  lv_obj_set_style_radius(body, 4, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, LV_MAX(1, size / 14), 0);
  lv_obj_set_style_border_color(body, color, 0);
  lv_obj_set_pos(body, (size - w) / 2, body_y);

  dot(b, size / 2, body_y + h - (lv_coord_t)(h * 0.12f), LV_MAX(1, size / 16), color);
  return b;
}

lv_obj_t *search(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  lv_coord_t d = (lv_coord_t)(size * 0.62f);

  lv_obj_t *ring = lv_obj_create(b);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, d, d);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, LV_MAX(2, size / 9), 0);
  lv_obj_set_style_border_color(ring, color, 0);
  lv_obj_set_pos(ring, 0, 0);

  lv_point_t *pts = new lv_point_t[2]{{(lv_coord_t)(d * 0.78f), (lv_coord_t)(d * 0.78f)},
                                      {(lv_coord_t)(size * 0.98f), (lv_coord_t)(size * 0.98f)}};
  lv_obj_t *handle = lv_line_create(b);
  lv_line_set_points(handle, pts, 2);
  lv_obj_set_style_line_width(handle, LV_MAX(2, size / 9), 0);
  lv_obj_set_style_line_color(handle, color, 0);
  lv_obj_set_style_line_rounded(handle, true, 0);
  return b;
}

lv_obj_t *disconnect(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *b = box(parent, size);
  lv_coord_t bar_w = LV_MAX(2, size / 7);
  lv_coord_t bar_h = (lv_coord_t)(size * 0.7f);
  lv_coord_t y = (size - bar_h) / 2;
  lv_coord_t gap = (lv_coord_t)(size * 0.22f);
  lv_coord_t x0 = (size - (bar_w * 2 + gap)) / 2;

  for (int i = 0; i < 2; i++) {
    lv_obj_t *bar = lv_obj_create(b);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, bar_w, bar_h);
    lv_obj_set_style_radius(bar, bar_w / 2, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_pos(bar, x0 + i * (bar_w + gap), y);
  }
  return b;
}

} // namespace ui_icons
