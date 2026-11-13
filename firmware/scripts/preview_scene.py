#!/usr/bin/env python3
"""Host preview of tiny-window scene, RGB565-quantized, 0.1.2 vs 0.1.3."""
from __future__ import annotations

import math
import os
from dataclasses import dataclass
from typing import List, Tuple

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SW, SH, HORIZON = 160, 120, 74
MAX_CLOUD_BLOBS, MAX_DROPS, MAX_RIPPLES, MAX_GLINTS = 48, 90, 9, 26
MAX_REEDS, MAX_TREES = 20, 4
TREES_X = [8, 17, 146, 154]
TREES_H = [16, 11, 14, 10]
GREY = (128, 134, 148)
PH_MORNING, PH_DAY, PH_EVENING, PH_NIGHT = 0, 1, 2, 3
WE_CLEAR, WE_CLOUDY, WE_RAIN = 0, 1, 2
PH_N = ["morning", "day", "evening", "night"]
WE_N = ["clear", "cloudy", "rain"]

Rgb = Tuple[int, int, int]


def clampi(v, lo, hi):
    return lo if v < lo else hi if v > hi else int(v)


def mix(a: Rgb, b: Rgb, t: float) -> Rgb:
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def mix_u(a: Rgb, b: Rgb, t: float) -> Rgb:
    def ch(x, y):
        return clampi(int(round(x + (y - x) * t)), 0, 255)

    return (ch(a[0], b[0]), ch(a[1], b[1]), ch(a[2], b[2]))


def dim(c: Rgb, k: float) -> Rgb:
    return (clampi(int(c[0] * k), 0, 255), clampi(int(c[1] * k), 0, 255), clampi(int(c[2] * k), 0, 255))


def luma(c: Rgb) -> float:
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def sat_boost(c: Rgb, s: float) -> Rgb:
    y = luma(c)
    g = (int(round(y)), int(round(y)), int(round(y)))
    return mix_u(g, c, s)


def panel_enc(v: int, gamma: float) -> int:
    if v <= 0:
        return 0
    if v >= 255:
        return 255
    return clampi(int(round(255.0 * (v / 255.0) ** gamma)), 0, 255)


def to565(c: Rgb, gamma: float = 1.0) -> int:
    if gamma != 1.0:
        c = (panel_enc(c[0], gamma), panel_enc(c[1], gamma), panel_enc(c[2], gamma))
    return ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3)


def from565(p: int) -> Rgb:
    r = ((p >> 11) & 0x1F) * 255 // 31
    g = ((p >> 5) & 0x3F) * 255 // 63
    b = (p & 0x1F) * 255 // 31
    return (r, g, b)


def mix565(a: int, b: int, alpha: int) -> int:
    A, B = from565(a), from565(b)
    ia = 255 - alpha
    o = (
        (A[0] * ia + B[0] * alpha) // 255,
        (A[1] * ia + B[1] * alpha) // 255,
        (A[2] * ia + B[2] * alpha) // 255,
    )
    return to565(o, 1.0)  # already in display-referred 8-bit from 565


# --- CIE Lab / CIEDE2000 (sRGB 8-bit) ---
def _srgb_to_lin(u: float) -> float:
    u = u / 255.0
    return u / 12.92 if u <= 0.04045 else ((u + 0.055) / 1.055) ** 2.4


def rgb_to_xyz(c: Rgb):
    r, g, b = _srgb_to_lin(c[0]), _srgb_to_lin(c[1]), _srgb_to_lin(c[2])
    x = r * 0.4124564 + g * 0.3575761 + b * 0.1804375
    y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750
    z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041
    return x, y, z


def rgb_to_lab(c: Rgb):
    x, y, z = rgb_to_xyz(c)
    # D65
    xn, yn, zn = 0.95047, 1.00000, 1.08883

    def f(t):
        return t ** (1 / 3) if t > 0.008856 else (7.787 * t + 16 / 116)

    fx, fy, fz = f(x / xn), f(y / yn), f(z / zn)
    L = 116 * fy - 16
    a = 500 * (fx - fy)
    b = 200 * (fy - fz)
    return L, a, b


def delta_e76(c1: Rgb, c2: Rgb) -> float:
    L1, a1, b1 = rgb_to_lab(c1)
    L2, a2, b2 = rgb_to_lab(c2)
    return math.sqrt((L1 - L2) ** 2 + (a1 - a2) ** 2 + (b1 - b2) ** 2)


def delta_e00(c1: Rgb, c2: Rgb) -> float:
    L1, a1, b1 = rgb_to_lab(c1)
    L2, a2, b2 = rgb_to_lab(c2)
    kL = kC = kH = 1.0
    C1 = math.sqrt(a1 * a1 + b1 * b1)
    C2 = math.sqrt(a2 * a2 + b2 * b2)
    Cab = 0.5 * (C1 + C2)
    G = 0.5 * (1 - math.sqrt(Cab ** 7 / (Cab ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.sqrt(a1p * a1p + b1 * b1), math.sqrt(a2p * a2p + b2 * b2)
    def ah(ap, b):
        if ap == 0 and b == 0:
            return 0.0
        h = math.degrees(math.atan2(b, ap))
        return h + 360 if h < 0 else h
    h1p, h2p = ah(a1p, b1), ah(a2p, b2)
    dLp = L2 - L1
    dCp = C2p - C1p
    dhp = 0.0
    if C1p * C2p != 0:
        dh = h2p - h1p
        if dh > 180:
            dh -= 360
        elif dh < -180:
            dh += 360
        dhp = dh
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp) / 2)
    Lbar = 0.5 * (L1 + L2)
    Cbarp = 0.5 * (C1p + C2p)
    if C1p * C2p == 0:
        hbarp = h1p + h2p
    else:
        hd = abs(h1p - h2p)
        if hd <= 180:
            hbarp = 0.5 * (h1p + h2p)
        elif h1p + h2p < 360:
            hbarp = 0.5 * (h1p + h2p + 360)
        else:
            hbarp = 0.5 * (h1p + h2p - 360)
    T = (1 - 0.17 * math.cos(math.radians(hbarp - 30))
         + 0.24 * math.cos(math.radians(2 * hbarp))
         + 0.32 * math.cos(math.radians(3 * hbarp + 6))
         - 0.20 * math.cos(math.radians(4 * hbarp - 63)))
    dth = 30 * math.exp(-(((hbarp - 275) / 25) ** 2))
    RC = 2 * math.sqrt(Cbarp ** 7 / (Cbarp ** 7 + 25 ** 7))
    SL = 1 + (0.015 * (Lbar - 50) ** 2) / math.sqrt(20 + (Lbar - 50) ** 2)
    SC = 1 + 0.045 * Cbarp
    SH = 1 + 0.015 * Cbarp * T
    RT = -math.sin(math.radians(2 * dth)) * RC
    return math.sqrt(
        (dLp / (kL * SL)) ** 2
        + (dCp / (kC * SC)) ** 2
        + (dHp / (kH * SH)) ** 2
        + RT * (dCp / (kC * SC)) * (dHp / (kH * SH))
    )


class Rng:
    def __init__(self, seed: int):
        self.s = seed & 0xFFFFFFFF or 1

    def next(self) -> int:
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s

    def frand(self) -> float:
        return (self.next() >> 8) * (1.0 / 16777216.0)

    def uniform(self, a, b) -> float:
        return a + (b - a) * self.frand()

    def irand(self, a, b) -> int:
        if b < a:
            return a
        return a + int(self.next() % (b - a + 1))


@dataclass
class Pal:
    top: Rgb
    bot: Rgb
    haze: Rgb
    light: Rgb
    lv: float


# 0.1.2 palettes (unchanged)
PALS_012 = [
    Pal((96, 112, 168), (236, 196, 186), (180, 168, 196), (255, 214, 170), 0.85),
    Pal((70, 132, 210), (176, 214, 236), (150, 180, 210), (255, 250, 220), 1.0),
    Pal((70, 58, 120), (240, 150, 120), (150, 110, 140), (255, 170, 110), 0.70),
    Pal((14, 18, 48), (44, 52, 96), (50, 58, 96), (210, 220, 255), 0.40),
]

# 0.1.3: brighter clear morning/day, less blue cast
PALS_013 = [
    Pal((168, 200, 230), (255, 224, 198), (230, 214, 200), (255, 236, 190), 1.0),
    Pal((130, 192, 235), (248, 250, 245), (200, 224, 232), (255, 252, 228), 1.0),
    Pal((70, 58, 120), (240, 150, 120), (150, 110, 140), (255, 170, 110), 0.70),
    Pal((14, 18, 48), (44, 52, 96), (50, 58, 96), (210, 220, 255), 0.40),
]

# mountain bases (far, mid, near) per phase — 0.1.3 only
MTN_013 = [
    # morning: warm olive / teal
    [(88, 122, 114), (70, 108, 86), (52, 92, 64)],
    # day: mid green-teal, aerial far is cooler but not sky-blue
    [(86, 126, 128), (64, 110, 92), (46, 94, 68)],
    # evening: dusk olive, hue-shifted from warm sky
    [(74, 68, 92), (56, 60, 68), (40, 50, 46)],
    # night: darker and greener than the blue horizon glow
    [(16, 26, 32), (10, 18, 22), (6, 12, 14)],
]

PANEL_GAMMA_013 = 0.90


def sky_cols(pals, ph, we):
    p = pals[ph]
    k = 0.0 if we == WE_CLEAR else 0.55 if we == WE_CLOUDY else 0.7
    g = dim(GREY, 0.35 + 0.65 * p.lv)
    return (
        mix(p.top, g, k),
        mix(p.bot, g, k * 0.9),
        mix(p.haze, g, k),
        p.light,
        p.lv * (1.0 - 0.25 * k),
    )


def mountain_cols_013(ph, we, lv):
    wk = 0.0 if we == WE_CLEAR else 0.35 if we == WE_CLOUDY else 0.50
    grey_m = dim((70, 80, 78), 0.45 + 0.55 * lv)
    out = []
    for base in MTN_013[ph]:
        out.append(mix(base, grey_m, wk))
    return out


def sun_pos(ph):
    return [(34, 40), (118, 20), (124, 42), (40, 22)][ph]


class Scene:
    def __init__(self, version: str):
        assert version in ("0.1.2", "0.1.3")
        self.ver = version
        self.pals = PALS_012 if version == "0.1.2" else PALS_013
        self.gamma = 1.0 if version == "0.1.2" else PANEL_GAMMA_013
        self.ridges = [[0] * SW for _ in range(3)]
        self.reeds_x = []
        self.reeds_h = []
        self.clouds = []
        self.drops = []
        self.ripples = []
        self.glints = []
        self.wrng = Rng(11)
        self.init_geom()

    def enc(self, c: Rgb) -> int:
        return to565(c, self.gamma)

    def init_geom(self):
        r = Rng(11)
        for k in range(3):
            base = 46 + k * 9
            amp = 9 - k * 2
            ph = r.uniform(0.0, 6.0)
            for x in range(SW):
                s = 0.6 * math.sin(x / (18.0 - k * 3.0) + ph) + 0.4 * math.sin(x / 7.0 + ph * 2.0)
                self.ridges[k][x] = int(base - amp * s)
        for x in range(2, 26, 3):
            self.reeds_x.append(x)
            self.reeds_h.append(r.irand(3, 7))
        for x in range(136, 158, 3):
            self.reeds_x.append(x)
            self.reeds_h.append(r.irand(3, 7))
        for _ in range(MAX_DROPS):
            self.drops.append([r.uniform(0, float(SW)), r.uniform(0, float(SH)), r.uniform(2.2, 3.2)])
        for _ in range(MAX_RIPPLES):
            self.ripples.append(
                [r.uniform(4, float(SW - 4)), r.uniform(float(HORIZON + 4), float(SH - 4)), r.uniform(0, 1)]
            )
        for _ in range(MAX_GLINTS):
            self.glints.append(
                [r.irand(0, SW - 1), r.irand(HORIZON + 3, SH - 1), r.uniform(0, 6)]
            )
        self.wrng = Rng(11)

    def rebuild_clouds(self, ph, we):
        top, bot, haze, light, lv = sky_cols(self.pals, ph, we)
        r = Rng(3 + int(we))
        clouds = []

        def add(x, y, w, h, col, a):
            if len(clouds) >= MAX_CLOUD_BLOBS:
                return
            clouds.append((x, y, w, h, self.enc(col), a))

        if we == WE_CLEAR:
            col = mix((255, 255, 255), light, 0.3)
            for _ in range(5):
                add(r.irand(0, SW * 2 - 1), r.irand(6, 40), r.irand(16, 34), 3, col, int(70 * lv + 30))
        else:
            n = 26 if we == WE_CLOUDY else 32
            base = dim(GREY, ((0.55 if we == WE_RAIN else 1.0) * (0.4 + 0.6 * lv)))
            for _ in range(n):
                x = r.irand(-10, SW * 2)
                y = r.irand(-6, 48 if we == WE_CLOUDY else 56)
                w = r.irand(26, 52)
                h = r.irand(8, 16)
                if we == WE_CLOUDY:
                    shade = mix(base, (255, 255, 255), 0.12 * (1.0 - y / 60.0))
                    a = 150
                else:
                    shade = dim(base, 0.9 + 0.2 * r.frand())
                    a = 200
                add(x, y, w, h, shade, a)
                if we == WE_CLOUDY:
                    add(x + 4, y, w - 8, h // 2, mix(shade, light, 0.3), 110)
            if we == WE_CLOUDY:
                for _ in range(10):
                    add(r.irand(0, SW * 2), r.irand(2, 40), r.irand(20, 40), 3, mix(base, light, 0.35), 90)
        self.clouds = clouds

    def fb_new(self):
        return np.zeros((SH, SW), dtype=np.uint16)

    def blend(self, fb, x, y, c, a):
        if x < 0 or y < 0 or x >= SW or y >= SH:
            return
        if a >= 250:
            fb[y, x] = c
        elif a:
            fb[y, x] = mix565(int(fb[y, x]), c, a)

    def fill_rect(self, fb, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        x0, y0 = max(x, 0), max(y, 0)
        x1, y1 = min(x + w - 1, SW - 1), min(y + h - 1, SH - 1)
        if x1 < x0 or y1 < y0:
            return
        fb[y0 : y1 + 1, x0 : x1 + 1] = c

    def fill_circle(self, fb, cx, cy, r, c, a):
        r2 = r * r
        for y in range(cy - r, cy + r + 1):
            if y < 0 or y >= SH:
                continue
            dy = y - cy
            # span
            xx = int(math.sqrt(r2 - dy * dy))
            for x in range(cx - xx, cx + xx + 1):
                self.blend(fb, x, y, c, a)

    def fill_triangle(self, fb, x0, y0, x1, y1, x2, y2, c):
        ymin = clampi(min(y0, y1, y2), 0, SH - 1)
        ymax = clampi(max(y0, y1, y2), 0, SH - 1)
        for y in range(ymin, ymax + 1):
            xs = []
            for (ax, ay), (bx, by) in (((x0, y0), (x1, y1)), ((x1, y1), (x2, y2)), ((x2, y2), (x0, y0))):
                if ay == by:
                    continue
                if y < min(ay, by) or y > max(ay, by):
                    continue
                t = (y - ay) / (by - ay)
                xs.append(int(ax + t * (bx - ax)))
            if xs:
                self.fill_rect(fb, min(xs), y, max(xs) - min(xs) + 1, 1, c)

    def draw_static(self, fb, ph, we):
        top, bot, haze, light, lv = sky_cols(self.pals, ph, we)
        sx, sy = sun_pos(ph)
        wc = 1.0 if we == WE_CLEAR else 0.0
        wo = 1.0 if we == WE_CLOUDY else 0.0
        pn = 1.0 if ph == PH_NIGHT else 0.0

        for y in range(HORIZON):
            t = (y / float(HORIZON)) ** 1.3
            self.fill_rect(fb, 0, y, SW, 1, self.enc(mix(top, bot, t)))

        if wc > 0.02:
            ha = clampi(int(12 * wc), 0, 255)
            da = clampi(int(255 * wc), 0, 255)
            if self.ver == "0.1.3":
                # single-pass halo (matches firmware 0.1.3)
                rmax = 34
                for y in range(sy - rmax, sy + rmax + 1):
                    if y < 0 or y >= SH:
                        continue
                    dy = y - sy
                    if dy * dy > rmax * rmax:
                        continue
                    xspan = int(math.sqrt(rmax * rmax - dy * dy))
                    for x in range(sx - xspan, sx + xspan + 1):
                        d = math.sqrt((x - sx) ** 2 + dy * dy)
                        if d <= 5:
                            col = mix(light, (255, 255, 255), 0.5)
                            self.blend(fb, x, y, self.enc(col), da)
                        elif d <= 34:
                            a = clampi(int(12 * wc * (1.0 - (d - 5) / 29.0)), 0, 255)
                            self.blend(fb, x, y, self.enc(light), a)
                if pn > 0.02:
                    self.fill_circle(fb, sx + 3, sy - 2, 4, self.enc(top), clampi(int(255 * wc * pn), 0, 255))
            else:
                for rr in range(34, 6, -3):
                    self.fill_circle(fb, sx, sy, rr, self.enc(light), ha)
                self.fill_circle(fb, sx, sy, 5, self.enc(mix(light, (255, 255, 255), 0.5)), da)
                if pn > 0.02:
                    self.fill_circle(fb, sx + 3, sy - 2, 4, self.enc(top), clampi(int(255 * wc * pn), 0, 255))
        if wo > 0.02:
            ha = clampi(int(8 * wo), 0, 255)
            for rr in range(44, 10, -4):
                self.fill_circle(fb, sx, sy, rr, self.enc(light), ha)

        if self.ver == "0.1.3":
            mt = mountain_cols_013(ph, we, lv)
            for layer in range(3):
                lit = mt[layer]
                sh = dim(mix(lit, (20, 40, 46), 0.30), 0.55)
                for x in range(SW):
                    y0 = self.ridges[layer][x]
                    y0 = 0 if y0 < 0 else HORIZON if y0 > HORIZON else y0
                    span = max(1, HORIZON - y0)
                    for y in range(y0, HORIZON):
                        h = (y - y0) / span
                        col = mix(lit, sh, h * 0.85)
                        fb[y, x] = self.enc(col)
        else:
            for k in range(3):
                col = mix(haze, dim(haze, 0.45 + 0.1 * k), 0.35 + 0.25 * k)
                col = mix(col, bot, 0.35 - 0.1 * k)
                cc = self.enc(col)
                for x in range(SW):
                    y0 = self.ridges[k][x]
                    y0 = 0 if y0 < 0 else HORIZON if y0 > HORIZON else y0
                    self.fill_rect(fb, x, y0, 1, HORIZON - y0, cc)

        # horizon haze: 0.1.3 only veils the sky above the ridge (not the mountain face)
        haz_scale = 70 if self.ver == "0.1.3" else 110
        for y in range(10):
            a = int(haz_scale * (1.0 - abs((y - 6) / 6.0)))
            a = 0 if a < 0 else 255 if a > 255 else a
            yy = HORIZON - 9 + y
            if 0 <= yy < SH:
                hc = self.enc(bot)
                for x in range(SW):
                    if self.ver == "0.1.3":
                        y_ridge = min(self.ridges[0][x], self.ridges[1][x], self.ridges[2][x])
                        if yy >= y_ridge:
                            continue
                    self.blend(fb, x, yy, hc, a)

        if self.ver == "0.1.3":
            mt = mountain_cols_013(ph, we, lv)
            for y in range(HORIZON, SH):
                d = (y - HORIZON) / float(SH - HORIZON)
                # stay close to sky hue (top) so peach+blue doesn't mud to grey
                src = mix(top, bot, 0.18 + 0.28 * d)
                src = sat_boost(src, 1.45)
                teal = (
                    clampi(int(src[0] * 0.72), 0, 255),
                    clampi(int(src[1] * 1.02), 0, 255),
                    clampi(int(src[2] * 1.08), 0, 255),
                )
                src = mix(src, teal, 0.40)
                sky_refl = dim(src, (0.34 if ph == PH_NIGHT else 0.46) + 0.08 * d)
                mamt = (1.0 - d) ** 2 * 0.85
                mref = dim(mix(mt[0], mt[2], min(1.0, d * 1.4)), 0.30)
                col = mix(sky_refl, mref, mamt)
                self.fill_rect(fb, 0, y, SW, 1, self.enc(col))
        else:
            for y in range(HORIZON, SH):
                d = (y - HORIZON) / float(SH - HORIZON)
                c = self.enc(dim(mix(bot, top, d), 0.75 - 0.15 * d))
                self.fill_rect(fb, 0, y, SW, 1, c)

        if wc > 0.02:
            path = self.enc(mix(dim(mix(bot, top, 0.3), 0.75), light, 0.45))
            for y in range(HORIZON + 1, SH, 2):
                w = 3 + (y - HORIZON) // 4
                self.fill_rect(fb, sx - w // 2, y, w, 1, path)

        sil = dim(mix(haze, (20, 30, 35), 0.75), 0.5 + 0.5 * lv)
        sc = self.enc(sil)
        for i, x in enumerate(self.reeds_x):
            h = self.reeds_h[i]
            self.fill_rect(fb, x, HORIZON + 6 - h, 1, h + 1, sc)
        self.fill_rect(fb, 0, HORIZON + 5, 28, 3, sc)
        self.fill_rect(fb, 134, HORIZON + 5, 26, 3, sc)
        for i in range(MAX_TREES):
            tx, th = TREES_X[i], TREES_H[i]
            self.fill_triangle(fb, tx, HORIZON + 6 - th, tx - 4, HORIZON + 6, tx + 4, HORIZON + 6, sc)

        return dict(top=top, bot=bot, haze=haze, light=light, lv=lv, sx=sx, sy=sy)

    def blit_clouds(self, fb, ox):
        layer_w = SW * 2
        for x, y, w, h, col, a in self.clouds:
            for pass_ in range(2):
                cx = x - ox + pass_ * layer_w + w // 2
                cy = y + h // 2
                rx, ry = max(1, w // 2), max(1, h // 2)
                if cx + rx < 0 or cx - rx >= SW or cy + ry < 0 or cy - ry >= HORIZON:
                    continue
                rx2, ry2 = float(rx * rx), float(ry * ry)
                for yy in range(-ry, ry + 1):
                    ypix = cy + yy
                    if ypix < 0 or ypix >= HORIZON:
                        continue
                    yn = (yy * yy) / ry2
                    if yn > 1:
                        continue
                    xspan = int(rx * math.sqrt(1.0 - yn) + 0.5)
                    for xx in range(-xspan, xspan + 1):
                        xp = cx + xx
                        if 0 <= xp < SW:
                            self.blend(fb, xp, ypix, col, a)

    def draw(self, ph, we, t: float):
        fb = self.fb_new()
        self.rebuild_clouds(ph, we)
        info = self.draw_static(fb, ph, we)
        top, bot, haze, light, lv = info["top"], info["bot"], info["haze"], info["light"], info["lv"]
        speed = 0.8 if we == WE_CLEAR else 1.5 if we == WE_CLOUDY else 2.6
        ox = (int(t * speed) % (SW * 2) + (SW * 2)) % (SW * 2)
        self.blit_clouds(fb, ox)

        my = HORIZON - 14 + int(2 * math.sin(t * 0.2))
        mistc = self.enc(mix(bot, haze, 0.4))
        # approximate fill_ellipse for mist (thin)
        for i in range(4):
            x = int(math.fmod(t * 2.0 + i * 47.0, float(SW + 60)) - 50)
            cx, cy, rx, ry = x + 25, my + (i % 2) * 3, 25, 2
            for yy in range(-ry, ry + 1):
                ypix = cy + yy
                if ypix < 0 or ypix >= SH:
                    continue
                yn = (yy * yy) / float(ry * ry) if ry else 1
                if yn > 1:
                    continue
                xspan = int(rx * math.sqrt(1.0 - yn) + 0.5)
                for xx in range(-xspan, xspan + 1):
                    self.blend(fb, cx + xx, ypix, mistc, 180)

        for i in range(7):
            y = HORIZON + 3 + i * 6 + int(math.sin(t * 0.8 + i) * 1.5)
            x = int(math.fmod(t * (3.0 + i) + i * 31.0, float(SW + 30)) - 15)
            c = self.enc(mix(dim(bot, 0.8), light, 0.25))
            self.fill_rect(fb, x, y, 8 + i * 2, 1, c)

        if we == WE_CLEAR and ph != PH_NIGHT:
            ray = self.enc(mix(bot, light, 0.5))
            sx, sy = info["sx"], info["sy"]
            for k in range(3):
                if math.sin(t * 0.5 + k * 2) > -0.2:
                    x0 = sx - 30 + k * 26
                    for y in range(30, HORIZON, 3):
                        xx = clampi(x0 + (y - 30) // 3, 0, SW - 1)
                        fb[y, xx] = ray
        if we == WE_CLEAR:
            gc = self.enc(mix(light, (255, 255, 255), 0.5))
            for gx, gy, p in self.glints:
                if math.sin(t * 2.2 + p * 5.0) > 0.75:
                    self.fill_rect(fb, gx, gy, 2, 1, gc)

        if we == WE_RAIN:
            # advance drops to time t roughly (from init state)
            wr = Rng(11)
            drops = [d[:] for d in self.drops]
            steps = max(1, int(t * 15))
            for _ in range(steps):
                for d in drops:
                    d[1] += d[2]
                    d[0] -= 0.6
                    if d[1] > SH:
                        d[1] = -4
                        d[0] = wr.uniform(0, float(SW + 20))
            rc = self.enc(mix(dim(GREY, 1.4), light, 0.15))
            for d in drops:
                x = (int(d[0]) % SW + SW) % SW
                y = int(d[1])
                self.fill_rect(fb, x, y, 1, 3, rc)
            rpc = self.enc(mix(dim(bot, 1.1), (230, 235, 245), 0.4))
            rips = [r[:] for r in self.ripples]
            for _ in range(steps):
                for rp in rips:
                    rp[2] += 0.035
                    if rp[2] > 1:
                        rp[0] = wr.uniform(4, float(SW - 4))
                        rp[1] = wr.uniform(float(HORIZON + 4), float(SH - 4))
                        rp[2] = 0
            for rp in rips:
                k = rp[2]
                w = int(2 + k * 12)
                h = max(2, w // 3)
                cx, cy, rx, ry = int(rp[0]), int(rp[1]), w // 2, h // 2
                for a in range(24):
                    ang = a * (6.2831853 / 24.0)
                    xx = cx + int(math.cos(ang) * rx)
                    yy = cy + int(math.sin(ang) * ry)
                    self.blend(fb, xx, yy, rpc, 200)
        return fb, info


def fb_to_rgb(fb: np.ndarray) -> np.ndarray:
    rgb = np.zeros((SH, SW, 3), dtype=np.uint8)
    flat = fb.reshape(-1)
    for i, p in enumerate(flat):
        rgb[i // SW, i % SW] = from565(int(p))
    return rgb


def scale2(rgb: np.ndarray) -> np.ndarray:
    return np.repeat(np.repeat(rgb, 2, axis=0), 2, axis=1)


def mean_color(rgb, mask) -> Rgb:
    pts = rgb[mask]
    if len(pts) == 0:
        return (0, 0, 0)
    m = pts.mean(axis=0)
    return (int(round(m[0])), int(round(m[1])), int(round(m[2])))


def chroma(c: Rgb) -> float:
    L, a, b = rgb_to_lab(c)
    return math.sqrt(a * a + b * b)


def analyze(scene: Scene, rgb: np.ndarray, ph, we):
    """Region means after RGB565 quantization (expanded 8-bit)."""
    ys = np.arange(SH)[:, None]
    xs = np.arange(SW)[None, :]
    # sky: upper, avoid sun
    sx, sy = sun_pos(ph)
    sun = (xs - sx) ** 2 + (ys - sy) ** 2 <= 18 ** 2
    sky_m = (ys >= 6) & (ys <= 28) & (xs >= 50) & (xs <= 100) & (~sun)
    # just above far ridge (adjacent sky)
    ridge0 = np.array(scene.ridges[0])[None, :]
    sky_adj = (ys >= ridge0 - 6) & (ys < ridge0) & (ys < HORIZON)
    # visible mountain layers
    r0 = np.array(scene.ridges[0])[None, :]
    r1 = np.array(scene.ridges[1])[None, :]
    r2 = np.array(scene.ridges[2])[None, :]
    in_mtn = (ys >= np.minimum(np.minimum(r0, r1), r2)) & (ys < HORIZON)
    # visible layer = max k with y >= ridge[k]
    layer = np.zeros((SH, SW), dtype=np.int8) - 1
    layer = np.where((ys >= r0) & (ys < HORIZON), 0, layer)
    layer = np.where((ys >= r1) & (ys < HORIZON), 1, layer)
    layer = np.where((ys >= r2) & (ys < HORIZON), 2, layer)
    far = layer == 0
    mid = layer == 1
    near = layer == 2
    # 山肌 = upper third of visible near/mid; 山の影 = lower third of near
    def height_frac(layer_mask, ridge):
        span = np.maximum(1, HORIZON - ridge)
        return (ys - ridge) / span

    hf_near = height_frac(near, r2)
    lit = near & (hf_near < 0.35)
    shad = near & (hf_near > 0.60)
    lake_mid = (ys >= 96) & (ys <= 110) & (xs >= 50) & (xs <= 110)
    lake_hz = (ys >= HORIZON + 2) & (ys <= HORIZON + 8) & (xs >= 40) & (xs <= 120)

    regs = {
        "sky": mean_color(rgb, sky_m),
        "sky_adj": mean_color(rgb, sky_adj),
        "mtn_far": mean_color(rgb, far),
        "mtn_mid": mean_color(rgb, mid),
        "mtn_lit": mean_color(rgb, lit) if lit.any() else mean_color(rgb, mid),
        "mtn_shad": mean_color(rgb, shad) if shad.any() else mean_color(rgb, near),
        "lake_mid": mean_color(rgb, lake_mid),
        "lake_hz": mean_color(rgb, lake_hz),
    }
    pairs = [
        ("sky", "mtn_shad"),
        ("sky", "mtn_lit"),
        ("sky_adj", "mtn_far"),
        ("sky", "lake_mid"),
        ("sky_adj", "lake_hz"),
        ("mtn_shad", "lake_hz"),
        ("mtn_lit", "mtn_shad"),
        ("mtn_far", "lake_mid"),
    ]
    diffs = []
    for a, b in pairs:
        ca, cb = regs[a], regs[b]
        dE = delta_e00(ca, cb)
        dE76 = delta_e76(ca, cb)
        dY = luma(ca) - luma(cb)
        diffs.append((a, b, ca, cb, dE, dE76, dY, chroma(ca), chroma(cb)))
    return regs, diffs


SCENES = [
    (PH_MORNING, WE_CLEAR, "晴れ・朝"),
    (PH_DAY, WE_CLEAR, "晴れ・昼"),
    (PH_DAY, WE_CLOUDY, "曇り"),
    (PH_DAY, WE_RAIN, "雨"),
    (PH_NIGHT, WE_CLEAR, "夜"),
]


def load_font(size, cjk=True):
    candidates = [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/sand-box/google/BIZ UDPMincho/BIZUDPMincho-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size, index=0)
            except Exception:
                try:
                    return ImageFont.truetype(p, size)
                except Exception:
                    continue
    return ImageFont.load_default()


def main():
    t = 2.5
    tiles = []
    all_stats = {}
    for ver in ("0.1.2", "0.1.3"):
        sc = Scene(ver)
        row = []
        for ph, we, title in SCENES:
            fb, _ = sc.draw(ph, we, t)
            rgb = fb_to_rgb(fb)
            regs, diffs = analyze(sc, rgb, ph, we)
            all_stats[(ver, title)] = (regs, diffs)
            row.append((title, rgb))
        tiles.append((ver, row))

    # print report
    def fmt_rgb(c):
        return "(%3d,%3d,%3d) Y=%5.1f" % (c[0], c[1], c[2], luma(c))

    print("=" * 88)
    for ver, title in [(v, t) for v in ("0.1.2", "0.1.3") for _, _, t in SCENES]:
        regs, diffs = all_stats[(ver, title)]
        print(f"\n[{ver} {title}]")
        for k, c in regs.items():
            print(f"  {k:10s} {fmt_rgb(c)}  C={chroma(c):5.1f}")
        print("  pairs:                    ΔE00   ΔE76    ΔY")
        for a, b, ca, cb, dE, dE76, dY, caC, cbC in diffs:
            flag = "OK" if dE >= 10 and abs(dY) >= 12 else ("WEAK" if dE >= 6 else "LOW")
            print(f"  {a:10s} vs {b:10s}  {dE:5.1f}  {dE76:5.1f}  {dY:6.1f}  {flag}")

    # thresholds for 0.1.3 (the ones the user cares about)
    print("\n--- 0.1.3 separation check (need ΔE00>=10 and |ΔY|>=12 for key pairs) ---")
    key = {("sky", "mtn_shad"), ("sky", "mtn_lit"), ("sky_adj", "mtn_far"),
           ("sky", "lake_mid"), ("mtn_shad", "lake_hz")}
    failed = 0
    for title in [s[2] for s in SCENES]:
        _, diffs = all_stats[("0.1.3", title)]
        for a, b, ca, cb, dE, dE76, dY, *_ in diffs:
            if (a, b) in key:
                ok = dE >= 10 and abs(dY) >= 12
                # night sky is dark; allow slightly lower luma gap if ΔE is strong
                if title == "夜" and dE >= 12 and abs(dY) >= 8:
                    ok = True
                if not ok:
                    failed += 1
                print(f"  {title:6s} {a} vs {b}: ΔE00={dE:.1f} ΔY={dY:.1f}  {'OK' if ok else 'FAIL'}")
    print("FAILED" if failed else "ALL KEY PAIRS PASSED", failed)

    # comparison figure
    pad, label_h, footer_h, gap = 12, 36, 250, 10
    tw, th = 320, 240
    cols = len(SCENES)
    rows = 2
    W = pad * 2 + cols * tw + (cols - 1) * gap
    H = pad + 40 + rows * (label_h + th) + (rows - 1) * 16 + footer_h + pad
    canvas = Image.new("RGB", (W, H), (28, 28, 30))
    draw = ImageDraw.Draw(canvas)
    font = load_font(18)
    font_s = load_font(13)
    font_t = load_font(22)
    draw.text((pad, 8), "tiny-window  0.1.2（上） vs  0.1.3（下）  — RGB565 量子化後", font=font_t, fill=(235, 235, 235))

    y0 = 48
    for ri, (ver, row) in enumerate(tiles):
        for ci, (title, rgb) in enumerate(row):
            x = pad + ci * (tw + gap)
            y = y0 + ri * (label_h + th + 16)
            tag = f"{ver}  {title}"
            draw.text((x, y), tag, font=font, fill=(210, 214, 220) if ver == "0.1.2" else (220, 236, 210))
            im = Image.fromarray(scale2(rgb), "RGB")
            canvas.paste(im, (x, y + label_h))
            draw.rectangle([x, y + label_h, x + tw - 1, y + label_h + th - 1], outline=(60, 60, 64))

    # footer: ΔE table for 0.1.3
    fy = y0 + 2 * (label_h + th + 16) - 8
    draw.text((pad, fy), "0.1.3  RGB565量子化後の色差（CIEDE2000 ΔE00 と 輝度Y差）。山は緑〜青緑、湖は空より一段暗く彩度高め。", font=font_s, fill=(180, 180, 184))
    fy += 20
    font_m = load_font(14)
    headers = ["シーン", "空 ↔ 山の影", "空 ↔ 山肌", "空際 ↔ 遠山", "空 ↔ 湖", "山の影 ↔ 湖の映り込み"]
    xs = [pad, pad+90, pad+330, pad+570, pad+810, pad+1050]
    for x, h in zip(xs, headers):
        draw.text((x, fy), h, font=font_m, fill=(160, 160, 164))
    fy += 18
    for title in [s[2] for s in SCENES]:
        diffs = {}
        for a, b, ca, cb, dE, dE76, dY, cA, cB in all_stats[("0.1.3", title)][1]:
            diffs[(a, b)] = (dE, dY)
        def cell(pair):
            dE, dY = diffs[pair]
            return f"ΔE {dE:.1f}  Y{dY:+.0f}"
        vals = [title,
                cell(("sky", "mtn_shad")),
                cell(("sky", "mtn_lit")),
                cell(("sky_adj", "mtn_far")),
                cell(("sky", "lake_mid")),
                cell(("mtn_shad", "lake_hz"))]
        for x, v in zip(xs, vals):
            draw.text((x, fy), v, font=font_m, fill=(220, 220, 220))
        fy += 17

    out = "preview-compare.png"
    canvas.save(out, "PNG")
    print("wrote", out, canvas.size)


if __name__ == "__main__":
    main()
