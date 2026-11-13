#include "app.h"
#include <math.h>

namespace {

struct Pal {
  Rgb top, bot, haze, light;
  float lv;
};

struct Blob {
  int16_t x, y, w, h;
  uint16_t col;
  uint8_t a;
};

struct Drop {
  float x, y, sp;
};
struct Ripple {
  float x, y, k;
};
struct Glint {
  int16_t x, y;
  float p;
};

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s = s * 1664525u + 1013904223u;
    return s;
  }
  float frand() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float uniform(float a, float b) { return a + (b - a) * frand(); }
  int irand(int a, int b) {  // inclusive
    if (b < a) return a;
    return a + (int)(next() % (uint32_t)(b - a + 1));
  }
};

// 0.1.3: clear morning/day are brighter and less blue-cast.
// Evening/night hues kept; mountains have their own green-teal bases.
const Pal PALS[PH_COUNT] = {
    {{168, 200, 230}, {255, 224, 198}, {230, 214, 200}, {255, 236, 190}, 1.00f},
    {{130, 192, 235}, {248, 250, 245}, {200, 224, 232}, {255, 252, 228}, 1.00f},
    {{70, 58, 120}, {240, 150, 120}, {150, 110, 140}, {255, 170, 110}, 0.70f},
    {{14, 18, 48}, {44, 52, 96}, {50, 58, 96}, {210, 220, 255}, 0.40f},
};

// Far / mid / near ridge bases. Hue-shifted from the sky (green-teal), 
// far ridges use aerial perspective but stay darker than the sky.
const Rgb MTN_BASE[PH_COUNT][3] = {
    {{88, 122, 114}, {70, 108, 86}, {52, 92, 64}},     // morning, warm olive
    {{86, 126, 128}, {64, 110, 92}, {46, 94, 68}},     // day, mid green-teal
    {{74, 68, 92}, {56, 60, 68}, {40, 50, 46}},        // evening
    {{16, 26, 32}, {10, 18, 22}, {6, 12, 14}},         // night, darker than glow
};

int16_t ridges[3][SW];
int16_t reeds_x[MAX_REEDS];
int8_t reeds_h[MAX_REEDS];
int nreeds = 0;
int16_t trees_x[MAX_TREES] = {8, 17, 146, 154};
int8_t trees_h[MAX_TREES] = {16, 11, 14, 10};

// Static lake/sky is redrawn into g_fb each frame (160x120 is cheap at 240MHz;
// a second RGB565 copy overflowed ESP32 DRAM). Clouds stay as a blob list.

Blob clouds[MAX_CLOUD_BLOBS];
int nclouds = 0;
Phase cloud_ph = PH_DAY;
Weather cloud_we = WE_CLEAR;
bool clouds_valid = false;

Drop drops[MAX_DROPS];
Ripple ripples[MAX_RIPPLES];
Glint glints[MAX_GLINTS];
Rng wrng(11);

inline uint16_t *pix(uint16_t *buf, int x, int y) { return &buf[y * SW + x]; }

void fill_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  int x0 = clampi(x, 0, SW - 1);
  int y0 = clampi(y, 0, SH - 1);
  int x1 = clampi(x + w - 1, 0, SW - 1);
  int y1 = clampi(y + h - 1, 0, SH - 1);
  for (int yy = y0; yy <= y1; ++yy) {
    uint16_t *row = buf + yy * SW;
    for (int xx = x0; xx <= x1; ++xx) row[xx] = c;
  }
}

void blend_pixel(uint16_t *buf, int x, int y, uint16_t c, uint8_t a) {
  if ((unsigned)x >= (unsigned)SW || (unsigned)y >= (unsigned)SH) return;
  if (a >= 250) {
    *pix(buf, x, y) = c;
    return;
  }
  if (a == 0) return;
  *pix(buf, x, y) = mix565(*pix(buf, x, y), c, a);
}

void fill_circle(uint16_t *buf, int cx, int cy, int r, uint16_t c, uint8_t a) {
  int r2 = r * r;
  for (int y = cy - r; y <= cy + r; ++y) {
    if ((unsigned)y >= (unsigned)SH) continue;
    int dy = y - cy;
    int xspan = (int)sqrtf((float)(r2 - dy * dy));
    for (int x = cx - xspan; x <= cx + xspan; ++x) blend_pixel(buf, x, y, c, a);
  }
}

void fill_ellipse(uint16_t *buf, int bw, int bh, int cx, int cy, int rx, int ry,
                  uint16_t c, uint8_t a, bool wrap_x) {
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  float rx2 = (float)(rx * rx);
  float ry2 = (float)(ry * ry);
  for (int y = -ry; y <= ry; ++y) {
    int yy = cy + y;
    if (yy < 0 || yy >= bh) continue;
    float yn = (float)(y * y) / ry2;
    if (yn > 1.f) continue;
    int xspan = (int)(rx * sqrtf(1.f - yn) + 0.5f);
    for (int x = -xspan; x <= xspan; ++x) {
      int xx = cx + x;
      if (wrap_x) {
        xx %= bw;
        if (xx < 0) xx += bw;
      } else if (xx < 0 || xx >= bw) {
        continue;
      }
      if (bw == SW) blend_pixel(buf, xx, yy, c, a);
      else {
        // cloud overlay path uses SW framebuffer only; unused
        (void)bh;
      }
    }
  }
}

void fill_triangle(uint16_t *buf, int x0, int y0, int x1, int y1, int x2, int y2,
                   uint16_t c) {
  // Flat-ish tree: apex (x0,y0), base (x1,y1)-(x2,y2)
  int ymin = clampi((y0 < y1 ? y0 : y1) < y2 ? (y0 < y1 ? y0 : y1) : y2, 0, SH - 1);
  int ymax = clampi((y0 > y1 ? y0 : y1) > y2 ? (y0 > y1 ? y0 : y1) : y2, 0, SH - 1);
  for (int y = ymin; y <= ymax; ++y) {
    int minx = SW, maxx = -1;
    auto edge = [&](int ax, int ay, int bx, int by) {
      if (ay == by) return;
      if ((y < ay && y < by) || (y > ay && y > by)) return;
      float t = (float)(y - ay) / (float)(by - ay);
      int x = (int)(ax + t * (bx - ax));
      if (x < minx) minx = x;
      if (x > maxx) maxx = x;
    };
    edge(x0, y0, x1, y1);
    edge(x1, y1, x2, y2);
    edge(x2, y2, x0, y0);
    if (maxx < minx) continue;
    fill_rect(buf, minx, y, maxx - minx + 1, 1, c);
  }
}

void sun_pos(Phase ph, int *sx, int *sy) {
  switch (ph) {
    case PH_MORNING: *sx = 34; *sy = 40; break;
    case PH_DAY: *sx = 118; *sy = 20; break;
    case PH_EVENING: *sx = 124; *sy = 42; break;
    default: *sx = 40; *sy = 22; break;
  }
}

void sky_cols(Phase ph, Weather we, Rgb *top, Rgb *bot, Rgb *haze, Rgb *light, float *lv) {
  const Pal &p = PALS[ph];
  float k = (we == WE_CLEAR) ? 0.f : (we == WE_CLOUDY) ? 0.55f : 0.7f;
  Rgb g = dim(GREY, 0.35f + 0.65f * p.lv);
  *top = mix(p.top, g, k);
  *bot = mix(p.bot, g, k * 0.9f);
  *haze = mix(p.haze, g, k);
  *light = p.light;
  *lv = p.lv * (1.f - 0.25f * k);
}

void mountain_cols(Phase ph, Weather we, float lv, Rgb out[3]) {
  // Grey toward a green-grey, never toward the sky hue.
  float wk = (we == WE_CLEAR) ? 0.f : (we == WE_CLOUDY) ? 0.35f : 0.50f;
  Rgb greym = dim(Rgb{70, 80, 78}, 0.45f + 0.55f * lv);
  for (int i = 0; i < 3; ++i) out[i] = mix(MTN_BASE[ph][i], greym, wk);
}

void mountain_cols_mix(Phase ph0, Weather we0, float lv0, Phase ph1, Weather we1, float lv1,
                       float k, Rgb out[3]) {
  Rgb a[3], b[3];
  mountain_cols(ph0, we0, lv0, a);
  mountain_cols(ph1, we1, lv1, b);
  for (int i = 0; i < 3; ++i) out[i] = mix(a[i], b[i], k);
}

void sky_cols_mix(Phase ph0, Weather we0, Phase ph1, Weather we1, float k,
                  Rgb *top, Rgb *bot, Rgb *haze, Rgb *light, float *lv) {
  Rgb t0, b0, h0, l0, t1, b1, h1, l1;
  float v0, v1;
  sky_cols(ph0, we0, &t0, &b0, &h0, &l0, &v0);
  sky_cols(ph1, we1, &t1, &b1, &h1, &l1, &v1);
  *top = mix(t0, t1, k);
  *bot = mix(b0, b1, k);
  *haze = mix(h0, h1, k);
  *light = mix(l0, l1, k);
  *lv = v0 + (v1 - v0) * k;
}

float w_of(Weather w, Weather a, Weather b, float k) {
  return (a == w ? 1.f - k : 0.f) + (b == w ? k : 0.f);
}

float p_of(Phase p, Phase a, Phase b, float k) {
  return (a == p ? 1.f - k : 0.f) + (b == p ? k : 0.f);
}

Phase trans_from_ph = PH_DAY, trans_to_ph = PH_DAY;
Weather trans_from_we = WE_CLEAR, trans_to_we = WE_CLEAR;
float trans_k = 1.f;
uint32_t trans_start_ms = 0;

void draw_static(uint16_t *dst, Phase ph0, Weather we0, Phase ph1, Weather we1, float k) {
  Rgb top, bot, haze, light;
  float lv;
  sky_cols_mix(ph0, we0, ph1, we1, k, &top, &bot, &haze, &light, &lv);
  int sx0, sy0, sx1, sy1;
  sun_pos(ph0, &sx0, &sy0);
  sun_pos(ph1, &sx1, &sy1);
  int sx = (int)lroundf(sx0 + (sx1 - sx0) * k);
  int sy = (int)lroundf(sy0 + (sy1 - sy0) * k);
  float wc = w_of(WE_CLEAR, we0, we1, k);
  float wo = w_of(WE_CLOUDY, we0, we1, k);
  float pn = p_of(PH_NIGHT, ph0, ph1, k);

  for (int y = 0; y < HORIZON; ++y) {
    float t = powf((float)y / (float)HORIZON, 1.3f);
    uint16_t c = to565_fx(mix(top, bot, t));
    fill_rect(dst, 0, y, SW, 1, c);
  }

  if (wc > 0.02f) {
    uint8_t da = (uint8_t)clampi((int)(255 * wc), 0, 255);
    // Single-pass halo (cheaper than stacked fill_circle rings; helps fps).
    const int rmax = 34;
    uint16_t disc = to565_fx(mix(light, Rgb{255, 255, 255}, 0.5f));
    uint16_t halo = to565_fx(light);
    for (int y = sy - rmax; y <= sy + rmax; ++y) {
      if ((unsigned)y >= (unsigned)SH) continue;
      int dy = y - sy;
      int d2 = rmax * rmax - dy * dy;
      if (d2 < 0) continue;
      int xspan = (int)sqrtf((float)d2);
      for (int x = sx - xspan; x <= sx + xspan; ++x) {
        float d = sqrtf((float)((x - sx) * (x - sx) + dy * dy));
        if (d <= 5.f) {
          blend_pixel(dst, x, y, disc, da);
        } else if (d <= 34.f) {
          uint8_t a = (uint8_t)clampi((int)(12 * wc * (1.f - (d - 5.f) / 29.f)), 0, 255);
          blend_pixel(dst, x, y, halo, a);
        }
      }
    }
    if (pn > 0.02f) {
      uint8_t ma = (uint8_t)clampi((int)(255 * wc * pn), 0, 255);
      fill_circle(dst, sx + 3, sy - 2, 4, to565_fx(top), ma);
    }
  }
  if (wo > 0.02f) {
    uint8_t ha = (uint8_t)clampi((int)(8 * wo), 0, 255);
    for (int rr = 44; rr > 10; rr -= 4) fill_circle(dst, sx, sy, rr, to565_fx(light), ha);
  }

  Rgb mt[3];
  // lv at each endpoint of the transition
  float lv0, lv1;
  {
    Rgb _t, _b, _h, _l;
    sky_cols(ph0, we0, &_t, &_b, &_h, &_l, &lv0);
    sky_cols(ph1, we1, &_t, &_b, &_h, &_l, &lv1);
  }
  mountain_cols_mix(ph0, we0, lv0, ph1, we1, lv1, k, mt);

  for (int layer = 0; layer < 3; ++layer) {
    Rgb lit = mt[layer];
    Rgb sh = dim(mix(lit, Rgb{20, 40, 46}, 0.30f), 0.55f);
    for (int x = 0; x < SW; ++x) {
      int y0 = ridges[layer][x];
      if (y0 < 0) y0 = 0;
      if (y0 > HORIZON) y0 = HORIZON;
      int span = HORIZON - y0;
      if (span < 1) span = 1;
      for (int y = y0; y < HORIZON; ++y) {
        float h = (float)(y - y0) / (float)span;
        dst[y * SW + x] = to565_fx(mix(lit, sh, h * 0.85f));
      }
    }
  }

  // Atmosphere only on the sky above the ridge, not on the mountain face.
  for (int y = 0; y < 10; ++y) {
    int a = (int)(70 * (1.f - fabsf((float)(y - 6) / 6.f)));
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    int yy = HORIZON - 9 + y;
    if ((unsigned)yy >= (unsigned)SH) continue;
    uint16_t hc = to565_fx(bot);
    for (int x = 0; x < SW; ++x) {
      int y_ridge = ridges[0][x];
      if (ridges[1][x] < y_ridge) y_ridge = ridges[1][x];
      if (ridges[2][x] < y_ridge) y_ridge = ridges[2][x];
      if (yy >= y_ridge) continue;
      blend_pixel(dst, x, yy, hc, (uint8_t)a);
    }
  }

  // Lake: sky reflection, one step darker and more saturated, with a teal bias
  // so peach+blue does not mud to grey. Mountain reflection is darker than the hills.
  float pn_l = p_of(PH_NIGHT, ph0, ph1, k);
  for (int y = HORIZON; y < SH; ++y) {
    float d = (float)(y - HORIZON) / (float)(SH - HORIZON);
    Rgb src = mix(top, bot, 0.18f + 0.28f * d);
    src = sat_boost(src, 1.45f);
    Rgb teal = {(uint8_t)clampi((int)(src.r * 0.72f), 0, 255),
                (uint8_t)clampi((int)(src.g * 1.02f), 0, 255),
                (uint8_t)clampi((int)(src.b * 1.08f), 0, 255)};
    src = mix(src, teal, 0.40f);
    Rgb sky_refl = dim(src, ((pn_l > 0.5f) ? 0.34f : 0.46f) + 0.08f * d);
    float mamt = (1.f - d) * (1.f - d) * 0.85f;
    Rgb mref = dim(mix(mt[0], mt[2], d * 1.4f > 1.f ? 1.f : d * 1.4f), 0.30f);
    uint16_t c = to565_fx(mix(sky_refl, mref, mamt));
    fill_rect(dst, 0, y, SW, 1, c);
  }
  if (wc > 0.02f) {
    uint16_t path = to565_fx(mix(dim(mix(bot, top, 0.3f), 0.75f), light, 0.45f));
    for (int y = HORIZON + 1; y < SH; y += 2) {
      int w = 3 + (y - HORIZON) / 4;
      fill_rect(dst, sx - w / 2, y, w, 1, path);
    }
  }

  Rgb sil = dim(mix(haze, Rgb{20, 30, 35}, 0.75f), 0.5f + 0.5f * lv);
  uint16_t sc = to565_fx(sil);
  for (int i = 0; i < nreeds; ++i) {
    fill_rect(dst, reeds_x[i], HORIZON + 6 - reeds_h[i], 1, reeds_h[i] + 1, sc);
  }
  fill_rect(dst, 0, HORIZON + 5, 28, 3, sc);
  fill_rect(dst, 134, HORIZON + 5, 26, 3, sc);
  for (int i = 0; i < MAX_TREES; ++i) {
    int tx = trees_x[i], th = trees_h[i];
    fill_triangle(dst, tx, HORIZON + 6 - th, tx - 4, HORIZON + 6, tx + 4, HORIZON + 6, sc);
  }
}

void rebuild_clouds(Phase ph, Weather we) {
  Rgb top, bot, haze, light;
  float lv;
  sky_cols(ph, we, &top, &bot, &haze, &light, &lv);
  Rng r(3 + (int)we);
  nclouds = 0;
  auto add = [&](int x, int y, int w, int h, Rgb col, uint8_t a) {
    if (nclouds >= MAX_CLOUD_BLOBS) return;
    Blob &b = clouds[nclouds++];
    b.x = (int16_t)x;
    b.y = (int16_t)y;
    b.w = (int16_t)w;
    b.h = (int16_t)h;
    b.col = to565_fx(col);
    b.a = a;
  };
  if (we == WE_CLEAR) {
    Rgb col = mix(Rgb{255, 255, 255}, light, 0.3f);
    for (int i = 0; i < 5; ++i) {
      int x = r.irand(0, SW * 2 - 1);
      int y = r.irand(6, 40);
      int w = r.irand(16, 34);
      add(x, y, w, 3, col, (uint8_t)(int)(70 * lv + 30));
    }
  } else {
    int n = (we == WE_CLOUDY) ? 26 : 32;
    Rgb base = dim(GREY, ((we == WE_RAIN) ? 0.55f : 1.0f) * (0.4f + 0.6f * lv));
    for (int i = 0; i < n; ++i) {
      int x = r.irand(-10, SW * 2);
      int y = r.irand(-6, we == WE_CLOUDY ? 48 : 56);
      int w = r.irand(26, 52);
      int h = r.irand(8, 16);
      Rgb shade = (we == WE_CLOUDY) ? mix(base, Rgb{255, 255, 255}, 0.12f * (1.f - y / 60.f))
                                    : dim(base, 0.9f + 0.2f * r.frand());
      uint8_t a = (we == WE_CLOUDY) ? 150 : 200;
      add(x, y, w, h, shade, a);
      if (we == WE_CLOUDY) add(x + 4, y, w - 8, h / 2, mix(shade, light, 0.3f), 110);
    }
    if (we == WE_CLOUDY) {
      for (int i = 0; i < 10; ++i) {
        int x = r.irand(0, SW * 2);
        int y = r.irand(2, 40);
        add(x, y, r.irand(20, 40), 3, mix(base, light, 0.35f), 90);
      }
    }
  }
  clouds_valid = true;
  cloud_ph = ph;
  cloud_we = we;
}

void blit_clouds(uint16_t *dst, int ox) {
  const int layer_w = SW * 2;
  for (int i = 0; i < nclouds; ++i) {
    const Blob &b = clouds[i];
    for (int pass = 0; pass < 2; ++pass) {
      int cx = b.x - ox + pass * layer_w + b.w / 2;
      int cy = b.y + b.h / 2;
      int rx = b.w / 2;
      int ry = b.h / 2;
      if (rx < 1) rx = 1;
      if (ry < 1) ry = 1;
      // Only draw if ellipse may overlap the 160-wide view
      if (cx + rx < 0 || cx - rx >= SW || cy + ry < 0 || cy - ry >= HORIZON) continue;
      float rx2 = (float)(rx * rx);
      float ry2 = (float)(ry * ry);
      for (int y = -ry; y <= ry; ++y) {
        int yy = cy + y;
        if ((unsigned)yy >= (unsigned)HORIZON) continue;
        float yn = (float)(y * y) / ry2;
        if (yn > 1.f) continue;
        int xspan = (int)(rx * sqrtf(1.f - yn) + 0.5f);
        for (int x = -xspan; x <= xspan; ++x) {
          int xx = cx + x;
          if ((unsigned)xx >= (unsigned)SW) continue;
          blend_pixel(dst, xx, yy, b.col, b.a);
        }
      }
    }
  }
}

}  // namespace

void scene_init() {
  Rng r(11);
  for (int k = 0; k < 3; ++k) {
    int base = 46 + k * 9;
    int amp = 9 - k * 2;
    float ph = r.uniform(0.f, 6.f);
    for (int x = 0; x < SW; ++x) {
      float s = 0.6f * sinf(x / (18.f - k * 3.f) + ph) + 0.4f * sinf(x / 7.f + ph * 2.f);
      ridges[k][x] = (int16_t)(base - amp * s);
    }
  }
  nreeds = 0;
  for (int x = 2; x < 26; x += 3) {
    reeds_x[nreeds] = (int16_t)x;
    reeds_h[nreeds] = (int8_t)r.irand(3, 7);
    nreeds++;
  }
  for (int x = 136; x < 158; x += 3) {
    reeds_x[nreeds] = (int16_t)x;
    reeds_h[nreeds] = (int8_t)r.irand(3, 7);
    nreeds++;
  }
  for (int i = 0; i < MAX_DROPS; ++i) {
    drops[i].x = r.uniform(0, (float)SW);
    drops[i].y = r.uniform(0, (float)SH);
    drops[i].sp = r.uniform(2.2f, 3.2f);
  }
  for (int i = 0; i < MAX_RIPPLES; ++i) {
    ripples[i].x = r.uniform(4, (float)(SW - 4));
    ripples[i].y = r.uniform((float)(HORIZON + 4), (float)(SH - 4));
    ripples[i].k = r.uniform(0, 1);
  }
  for (int i = 0; i < MAX_GLINTS; ++i) {
    glints[i].x = (int16_t)r.irand(0, SW - 1);
    glints[i].y = (int16_t)r.irand(HORIZON + 3, SH - 1);
    glints[i].p = r.uniform(0, 6);
  }
  wrng = Rng(11);
  clouds_valid = false;
}

void scene_invalidate() { clouds_valid = false; }

void scene_snap() {
  trans_from_ph = trans_to_ph = g.phase;
  trans_from_we = trans_to_we = g.weather;
  trans_k = 1.f;
  trans_start_ms = millis();
  clouds_valid = false;
}

void scene_draw(float t) {
  if (g.phase != trans_to_ph || g.weather != trans_to_we) {
    trans_from_ph = trans_to_ph;
    trans_from_we = trans_to_we;
    trans_to_ph = g.phase;
    trans_to_we = g.weather;
    trans_k = 0.f;
    trans_start_ms = millis();
    clouds_valid = false;
  }
  if (trans_k < 1.f) {
    float dt = (millis() - trans_start_ms) / 1000.f;
    trans_k = dt / (WEATHER_TRANSITION_MS / 1000.f);
    if (trans_k > 1.f) trans_k = 1.f;
  }
  float k = trans_k * trans_k * (3.f - 2.f * trans_k);  // smoothstep
  Phase ph0 = trans_from_ph, ph1 = trans_to_ph;
  Weather we0 = trans_from_we, we1 = trans_to_we;

  if (!clouds_valid || cloud_ph != ph1 || cloud_we != we1) rebuild_clouds(ph1, we1);

  draw_static(g_fb, ph0, we0, ph1, we1, k);

  Rgb top, bot, haze, light;
  float lv;
  sky_cols_mix(ph0, we0, ph1, we1, k, &top, &bot, &haze, &light, &lv);

  float wc = w_of(WE_CLEAR, we0, we1, k);
  float wo = w_of(WE_CLOUDY, we0, we1, k);
  float wr = w_of(WE_RAIN, we0, we1, k);
  float pn = p_of(PH_NIGHT, ph0, ph1, k);
  float speed = 0.8f * wc + 1.5f * wo + 2.6f * wr;
  int ox = ((int)(t * speed) % (SW * 2) + (SW * 2)) % (SW * 2);
  blit_clouds(g_fb, ox);

  int my = HORIZON - 14 + (int)(2 * sinf(t * 0.2f));
  uint16_t mistc = to565_fx(mix(bot, haze, 0.4f));
  for (int i = 0; i < 4; ++i) {
    int x = (int)fmodf(t * 2.f + i * 47.f, (float)(SW + 60)) - 50;
    fill_ellipse(g_fb, SW, SH, x + 25, my + (i % 2) * 3, 25, 2, mistc, 180, false);
  }

  for (int i = 0; i < 7; ++i) {
    int y = HORIZON + 3 + i * 6 + (int)(sinf(t * 0.8f + i) * 1.5f);
    int x = (int)fmodf(t * (3.f + i) + i * 31.f, (float)(SW + 30)) - 15;
    uint16_t c = to565_fx(mix(dim(bot, 0.8f), light, 0.25f));
    fill_rect(g_fb, x, y, 8 + i * 2, 1, c);
  }

  if (wc > 0.02f) {
    if (pn < 0.85f) {
      uint16_t ray = to565_fx(mix(bot, light, 0.5f));
      int sx0, sy0, sx1, sy1;
      sun_pos(ph0, &sx0, &sy0);
      sun_pos(ph1, &sx1, &sy1);
      int sx = (int)lroundf(sx0 + (sx1 - sx0) * k);
      (void)sy0; (void)sy1;
      for (int k = 0; k < 3; ++k) {
        if (sinf(t * 0.5f + k * 2) > -0.2f) {
          int x0 = sx - 30 + k * 26;
          for (int y = 30; y < HORIZON; y += 3) {
            int xx = clampi(x0 + (y - 30) / 3, 0, SW - 1);
            g_fb[y * SW + xx] = ray;
          }
        }
      }
    }
    uint16_t gc = to565_fx(mix(light, Rgb{255, 255, 255}, 0.5f));
    for (int i = 0; i < MAX_GLINTS; ++i) {
      if (sinf(t * 2.2f + glints[i].p * 5.f) > 0.75f) {
        fill_rect(g_fb, glints[i].x, glints[i].y, 2, 1, gc);
      }
    }
  }

  if (wr > 0.02f) {
    uint16_t rc = to565_fx(mix(dim(GREY, 1.4f), light, 0.15f));
    int ndrop = clampi((int)(MAX_DROPS * wr + 0.5f), 1, MAX_DROPS);
    for (int i = 0; i < ndrop; ++i) {
      drops[i].y += drops[i].sp;
      drops[i].x -= 0.6f;
      if (drops[i].y > SH) {
        drops[i].y = -4;
        drops[i].x = wrng.uniform(0, (float)SW + 20);
      }
      int x = ((int)drops[i].x % SW + SW) % SW;
      int y = (int)drops[i].y;
      fill_rect(g_fb, x, y, 1, 3, rc);
    }
    uint16_t rpc = to565_fx(mix(dim(bot, 1.1f), Rgb{230, 235, 245}, 0.4f));
    for (int i = 0; i < MAX_RIPPLES; ++i) {
      ripples[i].k += 0.035f;
      if (ripples[i].k > 1.f) {
        ripples[i].x = wrng.uniform(4, (float)(SW - 4));
        ripples[i].y = wrng.uniform((float)(HORIZON + 4), (float)(SH - 4));
        ripples[i].k = 0;
      }
      float k = ripples[i].k;
      int w = (int)(2 + k * 12);
      int h = w / 3;
      if (h < 2) h = 2;
      int cx = (int)ripples[i].x;
      int cy = (int)ripples[i].y;
      int rx = w / 2, ry = h / 2;
      // outline ellipse
      for (int a = 0; a < 24; ++a) {
        float ang = a * (6.2831853f / 24.f);
        int xx = cx + (int)(cosf(ang) * rx);
        int yy = cy + (int)(sinf(ang) * ry);
        blend_pixel(g_fb, xx, yy, rpc, 200);
      }
    }
  }
}
