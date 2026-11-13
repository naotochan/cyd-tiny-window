#include "weather.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Credentials are never compiled in. SSID/password live in NVS via WiFiManager.
// Lat/lon defaults are Tokyo; change with serial `lat` / `lon` or the setup portal.

static Preferences prefs;
static WiFiManager wm;
static WiFiManagerParameter p_lat("lat", "Latitude", "35.68", 16);
static WiFiManagerParameter p_lon("lon", "Longitude", "139.76", 16);

static float g_lat = DEFAULT_LATITUDE;
static float g_lon = DEFAULT_LONGITUDE;
static bool g_live = true;
static bool portal_on = false;
static bool was_connected = false;
static uint32_t last_reconnect_ms = 0;
static uint32_t last_fetch_ms = 0;
static uint32_t last_fetch_attempt_ms = 0;
static volatile bool fetch_now = false;
static WeatherObs g_last = {};
static WeatherObs g_pending = {};
static volatile bool pending_ready = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

enum WifiState { ST_AP, ST_CONNECTING, ST_ONLINE, ST_OFFLINE };
static WifiState wifi_st = ST_CONNECTING;
static bool ntp_started = false;
static bool ntp_ok_logged = false;

bool weather_clock_valid() { return time(nullptr) > 1600000000L; }

void weather_format_clock(char *buf, size_t cap) {
  if (!buf || cap < 8) return;
  time_t ts = time(nullptr);
  if (ts <= 1600000000L) {
    snprintf(buf, cap, "unset");
    return;
  }
  struct tm tmnow;
  localtime_r(&ts, &tmnow);
  snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d JST", tmnow.tm_year + 1900, tmnow.tm_mon + 1,
           tmnow.tm_mday, tmnow.tm_hour, tmnow.tm_min);
}

static void start_ntp() {
  if (ntp_started) return;
  setenv("TZ", "JST-9", 1);
  tzset();
  configTzTime("JST-9", "ntp.nict.jp", "pool.ntp.org", "time.google.com");
  ntp_started = true;
  Serial.println(F("ntp starting (JST-9, ntp.nict.jp / pool.ntp.org)"));
}

Weather weather_map_wmo(int code, float precipitation, int cloud_cover) {
  // 0 clear, 1 mainly clear
  if (code == 0 || code == 1) return WE_CLEAR;
  // 2 partly cloudy, 3 overcast, 45/48 fog / rime fog
  if (code == 2 || code == 3 || code == 45 || code == 48) return WE_CLOUDY;
  // Drizzle, rain, freezing rain, snow, showers, thunder/hail -> rain
  if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) ||
      (code >= 71 && code <= 77) || (code >= 80 && code <= 82) ||
      (code >= 85 && code <= 86) || (code >= 95 && code <= 99)) {
    return WE_RAIN;
  }
  // Unknown code: fall back to sensors
  if (precipitation > 0.05f) return WE_RAIN;
  if (cloud_cover >= 40) return WE_CLOUDY;
  return WE_CLEAR;
}

Phase weather_map_phase(int is_day, int local_hour) {
  if (!is_day) return PH_NIGHT;
  if (local_hour >= 5 && local_hour < 9) return PH_MORNING;
  if (local_hour >= 9 && local_hour < 16) return PH_DAY;
  if (local_hour >= 16 && local_hour < 19) return PH_EVENING;
  return PH_DAY;
}

static const char *find_exact_key(const char *s, const char *key) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t n = strlen(pat);
  const char *p = s;
  while ((p = strstr(p, pat))) {
    char next = p[n];
    if (next == '\0' || next == ' ' || next == '\t' || next == '\n' || next == '\r' ||
        next == ':') {
      return p + n;
    }
    p += n;
  }
  return nullptr;
}

static bool json_number(const char *obj, const char *key, double *out) {
  const char *p = find_exact_key(obj, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (!*p || *p == 'n' || *p == 'N') return false;
  char *end = nullptr;
  *out = strtod(p, &end);
  return end != p;
}

static bool json_string(const char *obj, const char *key, char *out, size_t cap) {
  const char *p = find_exact_key(obj, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != '"') return false;
  ++p;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < cap) {
    if (*p == '\\' && p[1]) ++p;
    out[i++] = *p++;
  }
  out[i] = 0;
  return i > 0;
}

// Isolate the "current" object (not "current_units") into dst.
bool weather_extract_current(const char *json, char *dst, size_t cap) {
  const char *p = json;
  const char *found = nullptr;
  while ((p = strstr(p, "\"current\""))) {
    const char *k = p + 9;  // after "current"
    const char *q = k;
    while (*q == ' ' || *q == '\t') ++q;
    if (*q == ':') {
      found = q + 1;
      break;
    }
    p = k;
  }
  if (!found) return false;
  while (*found == ' ' || *found == '\t' || *found == '\n') ++found;
  if (*found != '{') return false;
  int depth = 0;
  const char *start = found;
  const char *s = found;
  for (; *s; ++s) {
    if (*s == '{') depth++;
    else if (*s == '}') {
      depth--;
      if (depth == 0) {
        size_t n = (size_t)(s - start + 1);
        if (n + 1 > cap) n = cap - 1;
        memcpy(dst, start, n);
        dst[n] = 0;
        return true;
      }
    }
  }
  return false;
}

bool weather_parse_current(const char *obj, WeatherObs *o) {
  memset(o, 0, sizeof(*o));
  double code = 0, is_day = 0, cloud = 0, precip = 0, temp = 0;
  if (!json_number(obj, "weather_code", &code)) return false;
  json_number(obj, "is_day", &is_day);
  json_number(obj, "cloud_cover", &cloud);
  json_number(obj, "precipitation", &precip);
  json_number(obj, "temperature_2m", &temp);
  json_string(obj, "time", o->time_iso, sizeof(o->time_iso));
  o->weather_code = (int)lround(code);
  o->is_day = (int)lround(is_day);
  o->cloud_cover = (int)lround(cloud);
  o->precipitation = (float)precip;
  o->temperature_c = (float)temp;
  int hour = 12;
  const char *t = strchr(o->time_iso, 'T');
  if (t && t[1]) hour = atoi(t + 1);
  o->mapped_weather = weather_map_wmo(o->weather_code, o->precipitation, o->cloud_cover);
  o->mapped_phase = weather_map_phase(o->is_day, hour);
  o->valid = true;
  return true;
}

static void apply_clock_from_iso(const char *iso) {
  // current.time is local (Asia/Tokyo). Treat it as JST wall time.
  int y, mo, d, h, mi;
  if (sscanf(iso, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) return;
  setenv("TZ", "JST-9", 1);
  tzset();
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_isdst = 0;
  time_t ts = mktime(&tm);
  if (ts > 0) {
    struct timeval tv = {ts, 0};
    settimeofday(&tv, nullptr);
  }
}

static void save_last(const WeatherObs &o) {
  prefs.begin("tw", false);
  prefs.putBool("have", true);
  prefs.putInt("wcode", o.weather_code);
  prefs.putInt("isday", o.is_day);
  prefs.putInt("cloud", o.cloud_cover);
  prefs.putFloat("precip", o.precipitation);
  prefs.putFloat("temp", o.temperature_c);
  prefs.putString("timeiso", o.time_iso);
  prefs.putUChar("we", (uint8_t)o.mapped_weather);
  prefs.putUChar("ph", (uint8_t)o.mapped_phase);
  prefs.end();
}

static bool load_last(WeatherObs *o) {
  prefs.begin("tw", true);
  bool have = prefs.getBool("have", false);
  if (!have) {
    prefs.end();
    return false;
  }
  memset(o, 0, sizeof(*o));
  o->weather_code = prefs.getInt("wcode", 0);
  o->is_day = prefs.getInt("isday", 1);
  o->cloud_cover = prefs.getInt("cloud", 0);
  o->precipitation = prefs.getFloat("precip", 0);
  o->temperature_c = prefs.getFloat("temp", 0);
  String ts = prefs.getString("timeiso", "");
  strncpy(o->time_iso, ts.c_str(), sizeof(o->time_iso) - 1);
  o->mapped_weather = (Weather)prefs.getUChar("we", (uint8_t)WE_CLEAR);
  o->mapped_phase = (Phase)prefs.getUChar("ph", (uint8_t)PH_DAY);
  o->valid = true;
  prefs.end();
  return true;
}

static void save_location() {
  prefs.begin("tw", false);
  prefs.putFloat("lat", g_lat);
  prefs.putFloat("lon", g_lon);
  prefs.end();
}

static void load_location() {
  prefs.begin("tw", true);
  g_lat = prefs.getFloat("lat", DEFAULT_LATITUDE);
  g_lon = prefs.getFloat("lon", DEFAULT_LONGITUDE);
  prefs.end();
}

static void apply_obs(const WeatherObs &o, bool snap) {
  if (!o.valid) return;
  g.weather = o.mapped_weather;
  g.phase_auto = true;
  // Weather type comes from the observation. Phase follows the clock once
  // NTP (or a fresh ISO timestamp) has set it, so a stale last-known morning
  // cannot freeze the scene all day.
  if (weather_clock_valid()) {
    time_t ts = time(nullptr);
    struct tm tmnow;
    localtime_r(&ts, &tmnow);
    g.phase = phase_for_hour(tmnow.tm_hour);
  } else {
    g.phase = o.mapped_phase;
  }
  if (snap) scene_snap();
  else scene_invalidate();
}

static void save_params() {
  g_lat = atof(p_lat.getValue());
  g_lon = atof(p_lon.getValue());
  if (g_lat < -90.f || g_lat > 90.f) g_lat = DEFAULT_LATITUDE;
  if (g_lon < -180.f || g_lon > 180.f) g_lon = DEFAULT_LONGITUDE;
  save_location();
  Serial.printf("weather location saved lat=%.4f lon=%.4f\n", g_lat, g_lon);
  fetch_now = true;
}

static bool has_wifi_creds() { return wm.getWiFiIsSaved(); }

static void start_portal() {
  char latb[16], lonb[16];
  snprintf(latb, sizeof(latb), "%.4f", g_lat);
  snprintf(lonb, sizeof(lonb), "%.4f", g_lon);
  p_lat.setValue(latb, 16);
  p_lon.setValue(lonb, 16);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);
  if (!portal_on) {
    wm.startConfigPortal(WIFI_SETUP_AP_NAME);
    portal_on = true;
  }
  wifi_st = ST_AP;
  Serial.printf("wifi AP \"%s\"  open http://192.168.4.1  (scene keeps running)\n",
                WIFI_SETUP_AP_NAME);
}

static bool do_fetch(WeatherObs *out) {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=weather_code,is_day,cloud_cover,precipitation,temperature_2m"
           "&timezone=Asia%%2FTokyo",
           g_lat, g_lon);

  WiFiClientSecure client;
  client.setInsecure();  // weather data only; no user credentials on the wire
  client.setTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setReuse(false);
  http.addHeader("Accept", "application/json");
  if (!http.begin(client, url)) {
    Serial.println(F("weather fetch fail begin()"));
    out->http_code = -1;
    return false;
  }
  int code = http.GET();
  out->http_code = code;
  if (code != HTTP_CODE_OK) {
    Serial.printf("weather fetch fail http=%d heap=%u\n", code, (unsigned)ESP.getFreeHeap());
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  char cur[768];
  if (!weather_extract_current(body.c_str(), cur, sizeof(cur))) {
    Serial.println(F("weather fetch fail: no current object"));
    return false;
  }
  if (!weather_parse_current(cur, out)) {
    Serial.println(F("weather fetch fail: parse current"));
    return false;
  }
  out->fetched_ms = millis();
  Serial.printf(
      "weather fetch ok t=%s code=%d is_day=%d cloud=%d precip=%.2f temp=%.1f "
      "map=%s phase=%s lat=%.4f lon=%.4f heap=%u\n",
      out->time_iso, out->weather_code, out->is_day, out->cloud_cover, out->precipitation,
      out->temperature_c, weather_name(out->mapped_weather), phase_name(out->mapped_phase),
      g_lat, g_lon, (unsigned)ESP.getFreeHeap());
  apply_clock_from_iso(out->time_iso);
  return true;
}

static void weather_task(void *) {
  for (;;) {
    bool due = false;
    uint32_t now = millis();
    if (fetch_now) due = true;
    else if (last_fetch_ms == 0) due = (now - last_fetch_attempt_ms > 5000);
    else if (now - last_fetch_ms >= WEATHER_FETCH_INTERVAL_MS) due = true;

    if (due && WiFi.status() == WL_CONNECTED) {
      fetch_now = false;
      last_fetch_attempt_ms = now;
      WeatherObs obs = {};
      if (do_fetch(&obs)) {
        portENTER_CRITICAL(&mux);
        g_pending = obs;
        pending_ready = true;
        portEXIT_CRITICAL(&mux);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void weather_begin() {
  setenv("TZ", "JST-9", 1);
  tzset();
  load_location();
  if (load_last(&g_last)) {
    Serial.printf("weather last-known t=%s map=%s phase=%s (until live fetch)\n",
                  g_last.time_iso, weather_name(g_last.mapped_weather),
                  phase_name(g_last.mapped_phase));
    if (g_live) apply_obs(g_last, true);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wm.setDebugOutput(false);
  wm.setHostname("tiny-window");
  wm.setTitle("Tiny Window");
  wm.setBreakAfterConfig(true);
  wm.setConfigPortalBlocking(false);
  wm.addParameter(&p_lat);
  wm.addParameter(&p_lon);
  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);

  char latb[16], lonb[16];
  snprintf(latb, sizeof(latb), "%.4f", g_lat);
  snprintf(lonb, sizeof(lonb), "%.4f", g_lon);
  p_lat.setValue(latb, 16);
  p_lon.setValue(lonb, 16);

  if (!has_wifi_creds()) {
    start_portal();
  } else {
    wifi_st = ST_CONNECTING;
    Serial.printf("wifi connecting to saved SSID (not stored in firmware)\n");
    WiFi.begin();
    last_reconnect_ms = millis();
  }

  xTaskCreatePinnedToCore(weather_task, "wx", 16384, nullptr, 1, nullptr, 0);
}

void weather_loop() {
  if (portal_on) wm.process();

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !was_connected) {
    was_connected = true;
    wifi_st = ST_ONLINE;
    Serial.printf("wifi online ip=%s rssi=%d ssid=%s\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(), WiFi.SSID().c_str());
    start_ntp();
    fetch_now = true;
    last_fetch_attempt_ms = 0;
  } else if (!connected && was_connected) {
    was_connected = false;
    wifi_st = has_wifi_creds() ? ST_OFFLINE : ST_AP;
    Serial.println(F("wifi offline; scene keeps last weather"));
  } else if (!connected && has_wifi_creds() && !portal_on) {
    wifi_st = ST_CONNECTING;
    uint32_t now = millis();
    if (now - last_reconnect_ms > 15000) {
      last_reconnect_ms = now;
      Serial.println(F("wifi reconnecting..."));
      WiFi.disconnect();
      WiFi.begin();
    }
  }

  if (ntp_started && !ntp_ok_logged && weather_clock_valid()) {
    ntp_ok_logged = true;
    char clk[32];
    weather_format_clock(clk, sizeof(clk));
    Serial.printf("ntp ok %s\n", clk);
  }

  if (pending_ready) {
    WeatherObs obs;
    portENTER_CRITICAL(&mux);
    obs = g_pending;
    pending_ready = false;
    portEXIT_CRITICAL(&mux);
    g_last = obs;
    last_fetch_ms = millis();
    save_last(obs);
    if (g_live) apply_obs(obs, false);
    else {
      Serial.println(F("weather live=0; observation stored, scene left on manual setting"));
    }
  }
}

void weather_request_fetch() {
  fetch_now = true;
  last_fetch_attempt_ms = 0;
  Serial.println(F("weather fetch requested"));
}

void weather_reset_wifi() {
  Serial.println(F("wifi reset: erasing saved SSID/password, rebooting into setup AP"));
  wm.resetSettings();
  WiFi.disconnect(true, true);
  delay(200);
  ESP.restart();
}

void weather_start_portal() {
  start_portal();
}

void weather_set_location(float lat, float lon, bool save) {
  if (lat >= -90.f && lat <= 90.f) g_lat = lat;
  if (lon >= -180.f && lon <= 180.f) g_lon = lon;
  if (save) save_location();
  Serial.printf("weather location lat=%.4f lon=%.4f saved=%d\n", g_lat, g_lon, (int)save);
}

void weather_get_location(float *lat, float *lon) {
  if (lat) *lat = g_lat;
  if (lon) *lon = g_lon;
}

void weather_set_live(bool on) {
  g_live = on;
  Serial.printf("weather live=%d\n", (int)on);
  if (on && g_last.valid) apply_obs(g_last, false);
}

bool weather_live_enabled() { return g_live; }

const WeatherObs &weather_last() { return g_last; }

const char *weather_wifi_state() {
  switch (wifi_st) {
    case ST_AP: return "setup-ap";
    case ST_CONNECTING: return "connecting";
    case ST_ONLINE: return "online";
    case ST_OFFLINE: return "offline";
    default: return "?";
  }
}

void weather_log_status() {
  float lat, lon;
  weather_get_location(&lat, &lon);
  char clk[32];
  weather_format_clock(clk, sizeof(clk));
  Serial.printf("wifi=%s live=%d ssid=%s ip=%s rssi=%d portal=%d\n", weather_wifi_state(),
                (int)g_live, WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0, (int)portal_on);
  Serial.printf("clock %s ntp=%s\n", clk,
                ntp_started ? (weather_clock_valid() ? "ok" : "waiting") : "off");
  Serial.printf("location lat=%.4f lon=%.4f  (Open-Meteo, no API key)\n", lat, lon);
  if (g_last.valid) {
    Serial.printf("last t=%s code=%d is_day=%d cloud=%d precip=%.2f temp=%.1f map=%s phase=%s\n",
                  g_last.time_iso, g_last.weather_code, g_last.is_day, g_last.cloud_cover,
                  g_last.precipitation, g_last.temperature_c, weather_name(g_last.mapped_weather),
                  phase_name(g_last.mapped_phase));
  } else {
    Serial.println(F("last=(none yet; demo weather until first fetch)"));
  }
}

// Expose parser helpers to the host test via a tiny compile guard? Not needed on device.
