#pragma once

#include "app.h"

struct WeatherObs {
  bool valid;
  int weather_code;
  int is_day;
  int cloud_cover;
  float precipitation;
  float temperature_c;
  char time_iso[24];
  Weather mapped_weather;
  Phase mapped_phase;
  uint32_t fetched_ms;
  int http_code;
};

// WMO weather_code + light context -> existing clear/cloudy/rain.
// Snow / fog / thunder map to the nearest of those three.
Weather weather_map_wmo(int code, float precipitation, int cloud_cover);
Phase weather_map_phase(int is_day, int local_hour);

void weather_begin();
void weather_loop();
void weather_request_fetch();
void weather_reset_wifi();  // erase saved SSID/password, reboot into setup AP
void weather_start_portal();
void weather_set_location(float lat, float lon, bool save);
void weather_get_location(float *lat, float *lon);
void weather_set_live(bool on);
bool weather_live_enabled();
void weather_log_status();
bool weather_clock_valid();
void weather_format_clock(char *buf, size_t cap);  // "unset" or "YYYY-MM-DD HH:MM JST"
const WeatherObs &weather_last();
const char *weather_wifi_state();
