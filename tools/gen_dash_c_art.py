"""Generate the dashboard C artwork as LVGL 8 C images.

Matches the 2026-11-02 reference mockup (light "glass" design).
Outputs (src/ui/img/):
  dash_c_bg.c          800x480 RGB565 background: cream base, colour washes,
                       the supply / exhaust airflow crescents with arrows
  dash_c_ring_*.c      250x250 RGB565 conic gradients, used as the AQI ring
                       arc image (lv_obj_set_style_arc_img_src), one per level
  dash_c_glow.c        330x330 ALPHA_8 soft halo behind the ring, recoloured
                       to the AQI level colour at run time
  dash_c_ic_*.c        ALPHA_8 line icons (recoloured at run time)

Run from the repo root:  python tools/gen_dash_c_art.py [preview_dir]
A PNG preview of each image is written to preview_dir when given.
The layout constants below must match src/ui/ui_main_screen_v3.cpp.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 800, 480
OUT_DIR = os.path.join("src", "ui", "img")

# ---- layout shared with ui_main_screen_v3.cpp ---------------------------
RING_CX, RING_CY = 162, 238
RING_SIZE = 250
RING_WIDTH = 28
GLOW_SIZE = 330
MID_X0, MID_Y0, MID_X1, MID_Y1 = 323, 76, 573, 398     # airflow panel (below the 52 px top bar)


def hex_rgb(h):
    return np.array([(h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF], dtype=np.float32)


def lerp_stops(stops, t):
    """stops: list of (pos, 0xRRGGBB); t: array -> (..., 3) float."""
    t = np.clip(t, stops[0][0], stops[-1][0])
    out = np.zeros(t.shape + (3,), dtype=np.float32)
    for (p0, c0), (p1, c1) in zip(stops[:-1], stops[1:]):
        m = (t >= p0) & (t <= p1)
        u = ((t - p0) / max(p1 - p0, 1e-6))[m][:, None]
        out[m] = hex_rgb(c0) * (1 - u) + hex_rgb(c1) * u
    return out


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)


def wash(img, cx, cy, rx, ry, color, alpha):
    d2 = ((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2
    a = (alpha * np.exp(-d2 * 2.0))[..., None]
    img[:] = img * (1 - a) + hex_rgb(color) * a


def composite(img, layer):
    arr = np.asarray(layer).astype(np.float32)
    a = arr[..., 3:4] / 255.0
    img[:] = img * (1 - a) + arr[..., :3] * a


def add_glow(img, layer, radius, strength):
    glow = layer.filter(ImageFilter.GaussianBlur(radius))
    arr = np.asarray(glow).astype(np.float32)
    a = np.clip(arr[..., 3:4] / 255.0 * strength, 0, 1)
    img[:] = img * (1 - a) + arr[..., :3] * a


# ---- airflow crescents ---------------------------------------------------

def crescent(img, mask, ox, oy, rx, ry, a0, a1, half_w, stops, strand_cols, seed, body=0.95,
             glow_a1=None):
    """Glowing elliptic arc band: soft body, fine strands and sparkles.

    a0..a1: arc angles in degrees (screen coords, y down, 0 = right; angles
    past -180 are written as < -180). half_w: band half width in px.
    Colour runs from the inner edge (-1, towards the centre) to the outer
    edge (+1). glow_a1: optional later end angle for the soft colour glow
    only (the strands, sparkles and core stay within a0..a1).
    """
    rng = np.random.default_rng(seed)
    ex, ey = (XX - ox) / rx, (YY - oy) / ry
    rho = np.sqrt(ex * ex + ey * ey)
    ang = np.degrees(np.arctan2(ey, ex))
    ang = np.where(ang > 90, ang - 360, ang)            # continuous past -180
    reff = math.sqrt(rx * ry)
    t = (rho - 1.0) * reff / half_w
    span = a1 - a0
    fade = span * 0.22                                  # end fade, relative to the arc length
    env = smoothstep(a0 - 1, a0 + fade, ang) * (1 - smoothstep(a1 - fade, a1 + 1, ang))
    col = lerp_stops(stops, np.clip(t, -1.8, 1.8) / 1.8)

    halo = np.exp(-(t / 2.1) ** 2) * 0.55
    core = np.exp(-(t / 0.85) ** 2) * 0.62
    a = np.clip((halo + core) * env * body, 0, 0.95)
    if glow_a1 is not None:
        # colour glow running on past a1, fading out towards glow_a1
        # (runs along the whole band too, so there is no gap where the band fades)
        genv = smoothstep(a0 - 1, a0 + fade, ang) * (1 - smoothstep(a1 - fade, glow_a1, ang))
        gt = t
        glow = (np.exp(-(gt / 2.1) ** 2) * 0.40 + np.exp(-(gt / 0.9) ** 2) * 0.30) * genv * body
        a = a + np.clip(glow, 0, 0.8) * (1 - a)       # soft union, no seam
    a = a[..., None] * mask
    img[:] = img * (1 - a) + col * a

    ss = 2
    layer = Image.new("RGBA", (W * ss, H * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(40):
        off = float(np.clip(rng.normal(0, 0.45), -1.1, 1.1))
        start = a0 + rng.uniform(0.06, 0.24) * span
        end = a1 - rng.uniform(0.06, 0.22) * span
        th = np.radians(np.linspace(start, end, 180))
        wob = 0.07 * np.sin(np.linspace(0, rng.uniform(2, 5) * np.pi, len(th)) + rng.uniform(0, 6))
        rr = 1.0 + (off + wob) * half_w / reff
        px = ox + rx * rr * np.cos(th)
        py = oy + ry * rr * np.sin(th)
        cidx = 4 if rng.random() < 0.2 else int(np.clip((off + 1.1) / 2.2 * 4, 0, 3))
        cv = strand_cols[cidx]
        base = (cv >> 16 & 255, cv >> 8 & 255, cv & 255)
        alpha = rng.uniform(70, 200)
        wdt = int(rng.choice([1, 1, 1, 2]) * ss)
        for i in range(len(th) - 1):
            k = i / (len(th) - 1)
            fa = min(1.0, k * 5, (1 - k) * 5)
            d.line([(px[i] * ss, py[i] * ss), (px[i + 1] * ss, py[i + 1] * ss)],
                   fill=base + (int(alpha * fa),), width=wdt)
    for _ in range(80):
        th = math.radians(rng.uniform(a0 + 0.08 * span, a1 - 0.08 * span))
        off = float(np.clip(rng.normal(0, 0.6), -1.3, 1.3))
        rr = 1.0 + off * half_w / reff
        x = (ox + rx * rr * math.cos(th)) * ss
        y = (oy + ry * rr * math.sin(th)) * ss
        r = rng.choice([1.0, 1.4, 1.8, 2.4]) * ss / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, int(rng.uniform(150, 255))))
    layer = layer.resize((W, H), Image.LANCZOS)
    arr = np.asarray(layer).astype(np.float32)
    arr[..., 3:4] *= mask
    layer = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    add_glow(img, layer, 4, 0.9)
    composite(img, layer)


def arc_arrow(layer, ox, oy, rx, ry, a0, a1, color, head_start=False, head_end=False):
    """Thin elliptic arc a0 -> a1 (degrees) with arrow heads pointing outwards."""
    ss = 2
    big = Image.new("RGBA", (W * ss, H * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    th = np.radians(np.linspace(a0, a1, 80))
    px = ox + rx * np.cos(th)
    py = oy + ry * np.sin(th)
    d.line([(x * ss, y * ss) for x, y in zip(px, py)], fill=color, width=2 * ss)

    def head(i, j):
        dx, dy = px[i] - px[j], py[i] - py[j]
        L = math.hypot(dx, dy)
        dx, dy = dx / L, dy / L
        tip = (px[i] + dx * 4, py[i] + dy * 4)
        size = 9
        bx, by = tip[0] - dx * size, tip[1] - dy * size
        nx, ny = -dy, dx
        pts = [tip, (bx + nx * size * 0.5, by + ny * size * 0.5), (bx - nx * size * 0.5, by - ny * size * 0.5)]
        d.polygon([(x * ss, y * ss) for x, y in pts], fill=color)

    if head_end:
        head(-1, -4)
    if head_start:
        head(0, 3)
    layer.alpha_composite(big.resize((W, H), Image.LANCZOS))


def make_background():
    yy = np.linspace(0, 1, H, dtype=np.float32)[:, None, None]
    img = hex_rgb(0xF4EFE7) * (1 - yy) + hex_rgb(0xEFE8DE) * yy
    img = np.broadcast_to(img, (H, W, 3)).copy()

    # colour washes (reference: cyan across the top middle, peach behind the
    # right card, lavender low right, a little teal low left)
    wash(img, 480, 20, 240, 110, 0x8EE6F0, 0.75)
    wash(img, 610, 210, 120, 230, 0xFFC49A, 0.55)
    wash(img, 780, 60, 170, 130, 0xFFD3B5, 0.45)
    wash(img, 740, 430, 170, 110, 0xE2D4FF, 0.35)
    wash(img, 40, 330, 120, 140, 0xBFEFEA, 0.35)
    wash(img, 470, 380, 160, 110, 0xFFE0C8, 0.35)

    # light disc inside the AQI ring
    rr = np.sqrt((XX - RING_CX) ** 2 + (YY - RING_CY) ** 2)
    inner = (1 - smoothstep(70, 112, rr))[..., None]
    img[:] = img * (1 - inner * 0.55) + hex_rgb(0xFFFFFF) * inner * 0.55

    # airflow crescents: free glow (not clipped to the panel), only kept off
    # the ring column and the sensor pills
    mask = (smoothstep(300, 330, XX) * (1 - smoothstep(398, 408, YY)))[..., None]
    # supply: vertical at the top (its glow reaches the top bar), sweeping left at the bottom (bulges
    # down-right); exhaust: from the top right sweeping down to the bottom
    # (bulges up-left), its colour glow (not the strands) flowing on to
    # the right behind the control card. The two bulges almost touch in the middle.
    sx, sy, srx, sry = 300, 100, 155, 175
    ex_, ey_, erx, ery = 580, 390, 160, 200
    # supply bulges down-right towards the exhaust (centre up-left); its
    # light teal side faces up-left, the deep blue side faces the exhaust
    crescent(img, mask, sx, sy, srx, sry, -14, 88, 38,
             [(-1, 0x8CEBE2), (-0.55, 0x2FD8CF), (0.0, 0x19CFE6), (0.45, 0x16A6EA), (1, 0x1C6FDE)],
             strand_cols=[0x99F6E4, 0x14B8A6, 0x0EA5E9, 0x1E5FD0, 0xFFFFFF], seed=3, body=1.25)
    crescent(img, mask, ex_, ey_, erx, ery, -184, -98, 31,
             [(-1, 0x5B4BD0), (-0.45, 0x8E62EE), (-0.05, 0xD979C8), (0.45, 0xFF8E52), (1, 0xFFC08C)],
             strand_cols=[0x6D28D9, 0xA78BFA, 0xF9A8D4, 0xFB923C, 0xFFFFFF], seed=7, body=1.2,
             glow_a1=-40)
    arrows = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    arc_arrow(arrows, sx, sy, srx + 32, sry + 32, -6, 14, (30, 160, 215, 235), head_start=True)
    arc_arrow(arrows, sx, sy, srx + 20, sry + 20, 62, 80, (30, 160, 215, 235), head_end=True)
    arc_arrow(arrows, ex_, ey_, erx + 30, ery + 30, -122, -100, (130, 100, 235, 235), head_end=True)
    arc_arrow(arrows, ex_, ey_, erx + 24, ery + 24, -182, -165, (130, 100, 235, 235), head_start=True)
    add_glow(img, arrows, 3, 0.6)
    composite(img, arrows)
    return np.clip(img, 0, 255).astype(np.uint8)


# ---- AQI ring --------------------------------------------------------------

RING_STOPS = {
    # 0 = top of the ring, clockwise (the arc is rotated to start at 12 o'clock)
    "good":   [(0, 0x10A6A6), (0.25, 0x0A86D6), (0.5, 0x0570E0), (0.75, 0x06A2B0), (1, 0x10A6A6)],
    "normal": [(0, 0x84D94A), (0.25, 0x34D399), (0.5, 0x0F9F6E), (0.75, 0x22B07D), (1, 0x84D94A)],
    "bad":    [(0, 0xFDBA74), (0.25, 0xFB923C), (0.5, 0xE8590C), (0.75, 0xF97316), (1, 0xFDBA74)],
    "vbad":   [(0, 0xFB7185), (0.25, 0xF43F5E), (0.5, 0xC81E1E), (0.75, 0xE11D48), (1, 0xFB7185)],
}


def make_ring(stops):
    s = RING_SIZE
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
    c = s / 2
    ang = (np.degrees(np.arctan2(xx - c, -(yy - c))) + 360) % 360
    img = lerp_stops(stops, ang / 360.0)
    r = np.sqrt((xx - c) ** 2 + (yy - c) ** 2)
    u = np.clip((r - (c - RING_WIDTH)) / RING_WIDTH, 0, 1)[..., None]   # 0 inner -> 1 outer
    # tube shading: bright sheen a little outside the middle, darker edges
    # glossy highlight strongest at the top-left, like the reference
    gloss = 0.30 + 0.25 * np.clip(np.cos(np.radians(ang - 315)), 0, 1)[..., None]
    sheen = np.exp(-((u - 0.62) / 0.15) ** 2) * gloss
    edge = (np.abs(u - 0.5) * 2) ** 3 * 0.18
    img = img * (1 - sheen) + 255 * sheen
    img = img * (1 - edge)
    return np.clip(img, 0, 255).astype(np.uint8)


def make_glow():
    s = GLOW_SIZE
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32) + 0.5
    r = np.sqrt((xx - s / 2) ** 2 + (yy - s / 2) ** 2)
    rc = RING_SIZE / 2 - RING_WIDTH / 2
    a = 0.75 * np.exp(-((r - rc) / 30.0) ** 2) + 0.30 * np.exp(-((r - rc) / 60.0) ** 2)
    a *= 1 - smoothstep(s / 2 - 16, s / 2, r)
    return np.clip(a * 255, 0, 255).astype(np.uint8)


# ---- line icons (ALPHA_8, drawn 8x then downsampled) ----------------------

SS = 8


def outline(mask_img, stroke):
    """Filled mask -> outline of the given stroke width (in mask pixels)."""
    k = stroke * 2 + 1
    er = mask_img.filter(ImageFilter.MinFilter(k))
    a = np.asarray(mask_img).astype(np.int16) - np.asarray(er).astype(np.int16)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def stroked(n, build, stroke_frac=0.075):
    shape = Image.new("L", (n, n), 0)
    build(ImageDraw.Draw(shape), n)
    return outline(shape, max(1, int(n * stroke_frac)))


def ic_pinwheel(n):                 # supply fan: filled 4-blade pinwheel
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, R = n / 2, n * 0.48
    for k in range(4):
        a = math.radians(k * 90 - 90)
        tip = (c + R * math.cos(a), c + R * math.sin(a))
        side = (c + R * 0.62 * math.cos(a + math.radians(62)), c + R * 0.62 * math.sin(a + math.radians(62)))
        root = (c + n * 0.05 * math.cos(a - math.radians(90)), c + n * 0.05 * math.sin(a - math.radians(90)))
        d.polygon([root, tip, side, (c, c)], fill=255)
    d.ellipse([c - n * 0.09, c - n * 0.09, c + n * 0.09, c + n * 0.09], fill=255)
    return m


def ic_flower(n):                   # exhaust fan: 4-petal outline
    c, off, pr, pw = n / 2, n * 0.24, n * 0.24, n * 0.15
    acc = np.zeros((n, n), dtype=np.int16)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        def build(sd, n, dx=dx, dy=dy):
            x, y = c + dx * off, c + dy * off
            if dx:
                sd.ellipse([x - pr, y - pw, x + pr, y + pw], fill=255)
            else:
                sd.ellipse([x - pw, y - pr, x + pw, y + pr], fill=255)
        acc = np.maximum(acc, np.asarray(stroked(n, build)).astype(np.int16))
    m = Image.fromarray(acc.astype(np.uint8))
    ImageDraw.Draw(m).ellipse([c - n * 0.07, c - n * 0.07, c + n * 0.07, c + n * 0.07], fill=255)
    return m


def ic_sparkle(n):                  # PM2.5
    c = n / 2

    def build(sd, n):
        pts = []
        for k in range(8):
            rad = n * 0.46 if k % 2 == 0 else n * 0.13
            a = math.radians(k * 45 - 90)
            pts.append((c + rad * math.cos(a), c + rad * math.sin(a)))
        sd.polygon(pts, fill=255)
    return stroked(n, build)


def ic_blossom(n):                  # PM10
    c = n / 2

    def build(sd, n):
        for k in range(5):
            a = math.radians(k * 72 - 90)
            x, y = c + n * 0.24 * math.cos(a), c + n * 0.24 * math.sin(a)
            sd.ellipse([x - n * 0.19, y - n * 0.19, x + n * 0.19, y + n * 0.19], fill=255)
    m = stroked(n, build)
    ImageDraw.Draw(m).ellipse([c - n * 0.08, c - n * 0.08, c + n * 0.08, c + n * 0.08], fill=255)
    return m


def ic_cloud(n):                    # CO2
    def build(sd, n):
        sd.ellipse([n * 0.08, n * 0.42, n * 0.42, n * 0.78], fill=255)
        sd.ellipse([n * 0.26, n * 0.20, n * 0.68, n * 0.66], fill=255)
        sd.ellipse([n * 0.54, n * 0.38, n * 0.92, n * 0.78], fill=255)
        sd.rectangle([n * 0.25, n * 0.52, n * 0.74, n * 0.78], fill=255)
    return stroked(n, build)


def ic_person(n):                   # TVOC
    def build(sd, n):
        sd.ellipse([n * 0.32, n * 0.08, n * 0.68, n * 0.44], fill=255)
        sd.pieslice([n * 0.14, n * 0.52, n * 0.86, n * 1.24], 180, 360, fill=255)
    return stroked(n, build)


def ic_stopwatch(n):                # CO
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, w = n / 2, int(n * 0.075)
    d.ellipse([n * 0.14, n * 0.20, n * 0.86, n * 0.92], outline=255, width=w)
    d.rectangle([c - n * 0.10, n * 0.04, c + n * 0.10, n * 0.12], fill=255)
    d.line([(c, n * 0.10), (c, n * 0.22)], fill=255, width=w)
    d.line([(c, n * 0.56), (c + n * 0.15, n * 0.40)], fill=255, width=w)
    return m


def ic_sun(n):                      # weather
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, w = n / 2, int(n * 0.075)
    d.ellipse([c - n * 0.18, c - n * 0.18, c + n * 0.18, c + n * 0.18], outline=255, width=w)
    for k in range(8):
        a = math.radians(k * 45)
        d.line([(c + n * 0.30 * math.cos(a), c + n * 0.30 * math.sin(a)),
                (c + n * 0.45 * math.cos(a), c + n * 0.45 * math.sin(a))], fill=255, width=w)
    return m


def ic_alarm(n):                    # reservation
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, w = n / 2, int(n * 0.08)
    d.ellipse([n * 0.14, n * 0.16, n * 0.86, n * 0.88], outline=255, width=w)
    d.line([(c, n * 0.32), (c, n * 0.54), (c + n * 0.15, n * 0.63)], fill=255, width=w)
    d.arc([n * 0.0, n * 0.0, n * 0.36, n * 0.36], 180, 270, fill=255, width=w)
    d.arc([n * 0.64, n * 0.0, n * 1.0, n * 0.36], 270, 360, fill=255, width=w)
    d.line([(n * 0.26, n * 0.80), (n * 0.16, n * 0.96)], fill=255, width=w)
    d.line([(n * 0.74, n * 0.80), (n * 0.84, n * 0.96)], fill=255, width=w)
    return m


def ic_wifi(n):                     # header Wi-Fi
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, w = n / 2, int(n * 0.09)
    for r in (0.46, 0.30):
        d.arc([c - n * r, n * 0.80 - n * r, c + n * r, n * 0.80 + n * r], 225, 315, fill=255, width=w)
    d.ellipse([c - n * 0.08, n * 0.72, c + n * 0.08, n * 0.88], fill=255)
    return m


def ic_chat(n):                     # bottom row, left
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    w = int(n * 0.08)
    d.rounded_rectangle([n * 0.08, n * 0.14, n * 0.92, n * 0.70], radius=int(n * 0.14), outline=255, width=w)
    d.line([(n * 0.30, n * 0.68), (n * 0.24, n * 0.90), (n * 0.48, n * 0.70)], fill=255, width=w)
    return m


def ic_info(n):                     # bottom row, centre
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    c, w = n / 2, int(n * 0.08)
    d.ellipse([n * 0.06, n * 0.06, n * 0.94, n * 0.94], outline=255, width=w)
    d.ellipse([c - n * 0.065, n * 0.22, c + n * 0.065, n * 0.35], fill=255)
    d.line([(c, n * 0.44), (c, n * 0.76)], fill=255, width=w)
    return m


def ic_speaker(n):                  # bottom row, right
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    w = int(n * 0.08)
    d.polygon([(n * 0.06, n * 0.36), (n * 0.28, n * 0.36), (n * 0.54, n * 0.12), (n * 0.54, n * 0.88),
               (n * 0.28, n * 0.64), (n * 0.06, n * 0.64)], outline=255, width=w)
    d.arc([n * 0.44, n * 0.26, n * 0.84, n * 0.74], 300, 60, fill=255, width=w)
    return m


def ic_chev_l(n):
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).line([(n * 0.64, n * 0.16), (n * 0.32, n * 0.50), (n * 0.64, n * 0.84)],
                           fill=255, width=int(n * 0.10), joint="curve")
    return m


def ic_chev_r(n):
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).line([(n * 0.36, n * 0.16), (n * 0.68, n * 0.50), (n * 0.36, n * 0.84)],
                           fill=255, width=int(n * 0.10), joint="curve")
    return m


ICONS = {
    "pinwheel": (40, ic_pinwheel), "flower": (26, ic_flower), "alarm": (18, ic_alarm),
}


def make_icon(size, fn):
    big = fn(size * SS)
    return np.asarray(big.resize((size, size), Image.LANCZOS)).astype(np.uint8)


# ---- C output --------------------------------------------------------------

def c_bytes(flat):
    lines = []
    for i in range(0, len(flat), 32):
        lines.append("  " + ",".join("0x%02x" % x for x in flat[i:i + 32]) + ",")
    return "\n".join(lines)


def to_c_rgb565(name, rgb):
    h, w, _ = rgb.shape
    # ordered dither before quantising to RGB565 (less banding in the glows)
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.5
    th = np.tile(bayer, (h // 4 + 1, w // 4 + 1))[:h, :w][..., None]
    f = rgb.astype(np.float32) + th * np.array([8, 4, 8], dtype=np.float32)
    f = np.clip(f, 0, 255).astype(np.uint16)
    v = ((f[..., 0] >> 3) << 11) | ((f[..., 1] >> 2) << 5) | (f[..., 2] >> 3)
    data = np.empty((h, w, 2), dtype=np.uint8)
    data[..., 0] = v & 0xFF        # little endian, LV_COLOR_16_SWAP 0
    data[..., 1] = v >> 8
    return c_image(name, "LV_IMG_CF_TRUE_COLOR", w, h, data.reshape(-1))


def to_c_a8(name, a):
    h, w = a.shape
    return c_image(name, "LV_IMG_CF_ALPHA_8BIT", w, h, a.reshape(-1))


def c_image(name, cf, w, h, flat):
    return f"""/* Generated by tools/gen_dash_c_art.py - do not edit. */
#include <lvgl.h>

static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{
{c_bytes(flat)}
}};

const lv_img_dsc_t {name} = {{
  .header.cf = {cf},
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {len(flat)},
  .data = {name}_map,
}};
"""


def main():
    preview = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(OUT_DIR, exist_ok=True)
    for f in os.listdir(OUT_DIR):
        if f.startswith("dash_c_") and f.endswith(".c"):
            os.remove(os.path.join(OUT_DIR, f))

    def write(name, text, png):
        with open(os.path.join(OUT_DIR, name + ".c"), "w", newline="") as fh:
            fh.write(text)
        if preview:
            png.save(os.path.join(preview, name + ".png"))
        print("wrote", name, png.size[0], "x", png.size[1])

    bg = make_background()
    write("dash_c_bg", to_c_rgb565("dash_c_bg", bg), Image.fromarray(bg))
    for key, stops in RING_STOPS.items():
        ring = make_ring(stops)
        write("dash_c_ring_" + key, to_c_rgb565("dash_c_ring_" + key, ring), Image.fromarray(ring))
    glow = make_glow()
    write("dash_c_glow", to_c_a8("dash_c_glow", glow), Image.fromarray(glow))
    for key, (size, fn) in ICONS.items():
        a = make_icon(size, fn)
        write("dash_c_ic_" + key, to_c_a8("dash_c_ic_" + key, a), Image.fromarray(a))


if __name__ == "__main__":
    main()
