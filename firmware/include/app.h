#pragma once

#include "config.h"
#include "color.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

enum Mode { MODE_TEST = 0, MODE_SCENE = 1 };
enum Phase { PH_MORNING = 0, PH_DAY = 1, PH_EVENING = 2, PH_NIGHT = 3, PH_COUNT = 4 };
enum Weather { WE_CLEAR = 0, WE_CLOUDY = 1, WE_RAIN = 2, WE_COUNT = 3 };

struct AppState {
  Mode mode;
  Phase phase;
  Weather weather;
  bool phase_auto;
  bool invert;
  bool swap_rb;
  uint8_t brightness;
  uint8_t fps;
  uint8_t rotation;
  bool quiet;
  float t;
  uint32_t frames;
  uint32_t last_fps_ms;
  uint16_t measured_fps;
};

extern TFT_eSPI tft;
extern AppState g;
extern uint16_t g_fb[SW * SH];

void set_brightness(uint8_t v);
void apply_rotation();
void apply_invert();
void push_fb_2x();
void log_status();
void log_help();
void serial_poll();

void lcd_test_draw();

void scene_init();
void scene_invalidate();
void scene_snap();
void scene_draw(float t);

inline const char *phase_name(Phase p) {
  switch (p) {
    case PH_MORNING: return "morning";
    case PH_DAY: return "day";
    case PH_EVENING: return "evening";
    case PH_NIGHT: return "night";
    default: return "?";
  }
}

inline const char *weather_name(Weather w) {
  switch (w) {
    case WE_CLEAR: return "clear";
    case WE_CLOUDY: return "cloudy";
    case WE_RAIN: return "rain";
    default: return "?";
  }
}

inline Phase phase_for_hour(int h) {
  if (h >= 5 && h < 9) return PH_MORNING;
  if (h >= 9 && h < 16) return PH_DAY;
  if (h >= 16 && h < 19) return PH_EVENING;
  return PH_NIGHT;
}
