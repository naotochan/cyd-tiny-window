#pragma once

#include <stdint.h>
#include <math.h>

#ifndef PANEL_GAMMA
#define PANEL_GAMMA 0.90f  // mild midtone lift for ILI9341
#endif

struct Rgb {
  uint8_t r, g, b;
};

inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

inline uint16_t to565(Rgb c) {
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

inline uint8_t panel_enc(uint8_t v) {
  if (v == 0 || v == 255) return v;
  return (uint8_t)clampi((int)lroundf(255.f * powf(v / 255.f, PANEL_GAMMA)), 0, 255);
}

// Quantize for the panel (gamma + RGB565). Use this on scene writes.
inline uint16_t to565_fx(Rgb c) {
  Rgb e = {panel_enc(c.r), panel_enc(c.g), panel_enc(c.b)};
  return to565(e);
}

inline Rgb from565(uint16_t p) {
  Rgb c;
  c.r = (uint8_t)((p >> 11) * 255 / 31);
  c.g = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
  c.b = (uint8_t)((p & 0x1F) * 255 / 31);
  return c;
}

inline uint16_t swapRB565(uint16_t p) {
  uint16_t r = (p >> 11) & 0x1F;
  uint16_t g = p & 0x07E0;
  uint16_t b = p & 0x1F;
  return (uint16_t)((b << 11) | g | r);
}

inline Rgb mix(Rgb a, Rgb b, float t) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  Rgb o;
  o.r = (uint8_t)(a.r + (b.r - a.r) * t);
  o.g = (uint8_t)(a.g + (b.g - a.g) * t);
  o.b = (uint8_t)(a.b + (b.b - a.b) * t);
  return o;
}

// Allows t outside 0..1 (saturation boost).
inline Rgb mix_u(Rgb a, Rgb b, float t) {
  auto ch = [](int x, int y, float t) -> uint8_t {
    return (uint8_t)clampi((int)lroundf(x + (y - x) * t), 0, 255);
  };
  Rgb o = {ch(a.r, b.r, t), ch(a.g, b.g, t), ch(a.b, b.b, t)};
  return o;
}

inline float luma(Rgb c) {
  return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

inline Rgb sat_boost(Rgb c, float s) {
  float y = luma(c);
  uint8_t g = (uint8_t)clampi((int)lroundf(y), 0, 255);
  Rgb grey = {g, g, g};
  return mix_u(grey, c, s);
}

inline Rgb dim(Rgb c, float k) {
  Rgb o;
  o.r = (uint8_t)clampi((int)(c.r * k), 0, 255);
  o.g = (uint8_t)clampi((int)(c.g * k), 0, 255);
  o.b = (uint8_t)clampi((int)(c.b * k), 0, 255);
  return o;
}

inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t alpha) {
  // alpha 0 = a, 255 = b
  Rgb A = from565(a), B = from565(b);
  uint16_t ia = 255 - alpha;
  Rgb o;
  o.r = (uint8_t)((A.r * ia + B.r * alpha) / 255);
  o.g = (uint8_t)((A.g * ia + B.g * alpha) / 255);
  o.b = (uint8_t)((A.b * ia + B.b * alpha) / 255);
  return to565(o);
}

static const Rgb GREY = {128, 134, 148};
