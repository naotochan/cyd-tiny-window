#include "app.h"
#include "weather.h"
#include <stdlib.h>
#include <string.h>
#include <esp_system.h>

static char line[128];
static size_t linelen = 0;

static void reply(const char *msg) { Serial.println(msg); }

static bool eq(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static bool parse_onoff(const char *s, bool *out) {
  if (!s) return false;
  if (eq(s, "1") || eq(s, "on") || eq(s, "true") || eq(s, "yes")) {
    *out = true;
    return true;
  }
  if (eq(s, "0") || eq(s, "off") || eq(s, "false") || eq(s, "no")) {
    *out = false;
    return true;
  }
  return false;
}

void log_help() {
  Serial.println(F("commands (CR/LF, case-insensitive):"));
  Serial.println(F("  help | status | ident"));
  Serial.println(F("  test | scene | mode test|scene"));
  Serial.println(F("  weather clear|cloud|cloudy|rain"));
  Serial.println(F("  phase morning|day|evening|night|auto"));
  Serial.println(F("  brightness 0-255 | bright N | bl N"));
  Serial.println(F("  fps 1-30"));
  Serial.println(F("  rotate 0-3"));
  Serial.println(F("  invert 0|1 | swap 0|1 | quiet 0|1"));
  Serial.println(F("  cpu 80|160|240"));
  Serial.println(F("  weather live|auto | weather clear|cloud|rain"));
  Serial.println(F("  fetch | lat [N] | lon [N]"));
  Serial.println(F("  wifi | wifi reset | wifi ap"));
  Serial.println(F("weather live = Open-Meteo every 10 min. manual weather pauses live."));
}

void log_status() {
  Serial.printf("fw=%s %s  profile=%s  driver=%s color=%s invert_build=%d\n",
                FIRMWARE_NAME, FIRMWARE_VERSION, PROFILE_NAME, PROFILE_DRIVER,
                PROFILE_COLOR, PROFILE_INVERT);
  Serial.printf("mode=%s phase=%s%s weather=%s invert=%d swapRB=%d swapBytes=%d\n",
                g.mode == MODE_TEST ? "test" : "scene", phase_name(g.phase),
                g.phase_auto ? "(auto)" : "", weather_name(g.weather),
                (int)g.invert, (int)g.swap_rb, (int)tft.getSwapBytes());
  Serial.printf("brightness=%u fps_target=%u fps_meas=%u rot=%u t=%.1f frames=%lu\n",
                g.brightness, g.fps, g.measured_fps, g.rotation, g.t,
                (unsigned long)g.frames);
  Serial.printf("chip=%s rev=%d cpu=%dMHz flash=%u psram=%u heap=%u minheap=%u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
                (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  Serial.printf("pins driven: TFT MOSI=%d SCK=%d CS=%d DC=%d MISO=%d BL=%d RGB_LED=%d/%d/%d(off)\n",
                PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_MISO,
                PIN_TFT_BL, PIN_LED_R, PIN_LED_G, PIN_LED_B);
  Serial.println(F("bt=off deep_sleep=never  lcd chip not assumed"));
  weather_log_status();
}

static void try_ident() {
  // Best-effort register dump. CYD MISO is GPIO12; some panels do not answer.
  // Do not treat the result as a certain controller ID.
  Serial.println(F("ident: reading 04h / D3h (unverified on this host, may be 00)"));
  uint8_t d04[4] = {0};
  uint8_t d3[4] = {0};
  for (int i = 0; i < 4; ++i) d04[i] = tft.readcommand8(0x04, i);
  for (int i = 0; i < 4; ++i) d3[i] = tft.readcommand8(0xD3, i);
  Serial.printf("RDDID(04h)= %02X %02X %02X %02X\n", d04[0], d04[1], d04[2], d04[3]);
  Serial.printf("ID4(D3h)=   %02X %02X %02X %02X\n", d3[0], d3[1], d3[2], d3[3]);
  Serial.println(F("hint: ST7789 often 85 85 52 on 04h; ILI9341 often 00 93 41 on indexed D3h"));
  Serial.println(F("hint: a zero dump does not prove the chip; try another profile if the picture is wrong"));
}

static void handle(char *buf) {
  // trim
  while (*buf == ' ' || *buf == '\t') ++buf;
  char *sp = buf + strlen(buf);
  while (sp > buf && (sp[-1] == ' ' || sp[-1] == '\t')) *--sp = 0;
  if (!*buf) return;
  if (buf[0] == '#') return;

  char *cmd = strtok(buf, " \t");
  char *arg = strtok(nullptr, " \t");
  char *arg2 = strtok(nullptr, " \t");
  (void)arg2;
  if (!cmd) return;

  if (eq(cmd, "help") || eq(cmd, "?")) {
    log_help();
    return;
  }
  if (eq(cmd, "status")) {
    log_status();
    return;
  }
  if (eq(cmd, "ident")) {
    try_ident();
    return;
  }
  if (eq(cmd, "test") || (eq(cmd, "mode") && arg && eq(arg, "test"))) {
    g.mode = MODE_TEST;
    reply("mode=test");
    return;
  }
  if (eq(cmd, "scene") || (eq(cmd, "mode") && arg && eq(arg, "scene"))) {
    g.mode = MODE_SCENE;
    scene_invalidate();
    reply("mode=scene");
    return;
  }
  if (eq(cmd, "weather")) {
    if (!arg) {
      weather_log_status();
      return;
    }
    if (eq(arg, "live") || eq(arg, "auto")) {
      weather_set_live(true);
      return;
    }
    if (eq(arg, "manual") || eq(arg, "off")) {
      weather_set_live(false);
      return;
    }
    Weather w;
    if (eq(arg, "clear") || eq(arg, "sunny") || eq(arg, "s")) w = WE_CLEAR;
    else if (eq(arg, "cloud") || eq(arg, "cloudy") || eq(arg, "c")) w = WE_CLOUDY;
    else if (eq(arg, "rain") || eq(arg, "r")) w = WE_RAIN;
    else {
      reply("err weather (live|clear|cloud|rain)");
      return;
    }
    weather_set_live(false);
    g.weather = w;
    scene_invalidate();
    Serial.printf("weather=%s (manual, live paused)\n", weather_name(g.weather));
    return;
  }
  if (eq(cmd, "fetch") || (eq(cmd, "wifi") && arg && eq(arg, "fetch"))) {
    weather_set_live(true);
    weather_request_fetch();
    return;
  }
  if (eq(cmd, "lat")) {
    float lat, lon;
    weather_get_location(&lat, &lon);
    if (!arg) {
      Serial.printf("lat=%.4f\n", lat);
      return;
    }
    weather_set_location((float)atof(arg), lon, true);
    weather_request_fetch();
    return;
  }
  if (eq(cmd, "lon")) {
    float lat, lon;
    weather_get_location(&lat, &lon);
    if (!arg) {
      Serial.printf("lon=%.4f\n", lon);
      return;
    }
    weather_set_location(lat, (float)atof(arg), true);
    weather_request_fetch();
    return;
  }
  if (eq(cmd, "wifi")) {
    if (!arg || eq(arg, "status")) {
      weather_log_status();
      return;
    }
    if (eq(arg, "reset") || eq(arg, "erase")) {
      weather_reset_wifi();
      return;
    }
    if (eq(arg, "ap") || eq(arg, "setup")) {
      weather_start_portal();
      return;
    }
    reply("err wifi (status|reset|ap|fetch)");
    return;
  }
  if (eq(cmd, "phase") && arg) {
    if (eq(arg, "auto") || eq(arg, "a")) {
      g.phase_auto = true;
      weather_set_live(true);
      reply("phase=auto (live weather)");
      return;
    }
    g.phase_auto = false;
    weather_set_live(false);
    if (eq(arg, "morning") || eq(arg, "1")) g.phase = PH_MORNING;
    else if (eq(arg, "day") || eq(arg, "2")) g.phase = PH_DAY;
    else if (eq(arg, "evening") || eq(arg, "3")) g.phase = PH_EVENING;
    else if (eq(arg, "night") || eq(arg, "4")) g.phase = PH_NIGHT;
    else {
      reply("err phase (morning|day|evening|night|auto)");
      return;
    }
    scene_invalidate();
    Serial.printf("phase=%s\n", phase_name(g.phase));
    return;
  }
  if ((eq(cmd, "brightness") || eq(cmd, "bright") || eq(cmd, "bl")) && arg) {
    int v = atoi(arg);
    v = clampi(v, 0, 255);
    set_brightness((uint8_t)v);
    Serial.printf("brightness=%u (GPIO21 PWM; some CYD BL transistors are near on/off)\n",
                  g.brightness);
    return;
  }
  if (eq(cmd, "fps") && arg) {
    int v = clampi(atoi(arg), 1, 30);
    g.fps = (uint8_t)v;
    Serial.printf("fps_target=%u (unmeasured on device in this build host)\n", g.fps);
    return;
  }
  if (eq(cmd, "rotate") && arg) {
    int v = atoi(arg);
    if (v < 0 || v > 3) {
      reply("err rotate 0-3");
      return;
    }
    g.rotation = (uint8_t)v;
    apply_rotation();
    Serial.printf("rotate=%u\n", g.rotation);
    return;
  }
  if (eq(cmd, "invert") && arg) {
    bool v;
    if (!parse_onoff(arg, &v)) {
      reply("err invert 0|1");
      return;
    }
    g.invert = v;
    apply_invert();
    Serial.printf("invert=%d\n", (int)g.invert);
    return;
  }
  if (eq(cmd, "swap") && arg) {
    bool v;
    if (!parse_onoff(arg, &v)) {
      reply("err swap 0|1");
      return;
    }
    g.swap_rb = v;
    Serial.printf("swapRB=%d (software, scene+test push path)\n", (int)g.swap_rb);
    return;
  }
  if (eq(cmd, "frame")) {
    reply("frame drawing removed; bezel is the physical case");
    return;
  }
  if (eq(cmd, "quiet") && arg) {
    parse_onoff(arg, &g.quiet);
    Serial.printf("quiet=%d\n", (int)g.quiet);
    return;
  }
  if (eq(cmd, "cpu") && arg) {
    int v = atoi(arg);
    if (v != 80 && v != 160 && v != 240) {
      reply("err cpu 80|160|240");
      return;
    }
    setCpuFrequencyMhz(v);
    Serial.printf("cpu=%d\n", ESP.getCpuFreqMHz());
    return;
  }
  Serial.printf("err unknown '%s' (help)\n", cmd);
}

void serial_poll() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      line[linelen] = 0;
      linelen = 0;
      handle(line);
      continue;
    }
    if (linelen + 1 < sizeof(line)) line[linelen++] = (char)c;
  }
}
