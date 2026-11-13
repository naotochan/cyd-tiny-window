#include "app.h"
#include "weather.h"
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
AppState g;
uint16_t g_fb[SW * SH];

extern void serial_poll();

static uint32_t last_frame_ms = 0;
static uint32_t fps_window_frames = 0;
static bool test_dirty = true;
static uint8_t last_test_bl = 0xFF;
static uint8_t last_test_rot = 0xFF;
static bool last_test_inv = false;
static bool last_test_swap = false;

void set_brightness(uint8_t v) {
  g.brightness = v;
  ledcWrite(BL_LEDC_CH, v);
}

void apply_rotation() {
  tft.setRotation(g.rotation);
  test_dirty = true;
}

void apply_invert() {
  tft.invertDisplay(g.invert);
  test_dirty = true;
}

void push_fb_2x() {
  uint16_t line[LCD_W];
  // ILI9341 wants RGB565 high-byte first on SPI. fillRect/pushBlock always
  // byte-swaps; pushPixels does not unless setSwapBytes(true). Without this,
  // a vertical sky gradient becomes cyan/pink/green horizontal bands.
  tft.setSwapBytes(true);
  tft.startWrite();
  tft.setAddrWindow(0, 0, LCD_W, LCD_H);
  for (int y = 0; y < SH; ++y) {
    const uint16_t *row = g_fb + y * SW;
    for (int x = 0; x < SW; ++x) {
      uint16_t p = row[x];
      if (g.swap_rb) p = swapRB565(p);
      line[x * 2] = p;
      line[x * 2 + 1] = p;
    }
    tft.pushPixels(line, LCD_W);
    tft.pushPixels(line, LCD_W);
  }
  tft.endWrite();
}

static void rgb_led_off() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);
}

static void radios_off() {
  // Bluetooth stays off. Wi-Fi is started later by weather_begin() (setup AP /
  // Open-Meteo). btStop() is a no-op if the controller was never started.
  btStop();
}

static void backlight_pwm_begin() {
  ledcSetup(BL_LEDC_CH, BL_LEDC_FREQ, BL_LEDC_BITS);
  ledcAttachPin(PIN_TFT_BL, BL_LEDC_CH);
  ledcWrite(BL_LEDC_CH, g.brightness);
}

void setup() {
  rgb_led_off();
  radios_off();
  setCpuFrequencyMhz(240);

  Serial.begin(SERIAL_BAUD);
  delay(50);

  g.mode = MODE_SCENE;
  g.phase = PH_DAY;
  g.weather = WE_CLEAR;
  g.phase_auto = false;
  g.invert = (PROFILE_INVERT != 0);
  g.swap_rb = false;
  g.brightness = DEFAULT_BRIGHTNESS;
  g.fps = DEFAULT_FPS;
  g.rotation = DEFAULT_ROTATION;
  g.quiet = false;
  g.t = 0.f;
  g.frames = 0;
  g.last_fps_ms = millis();
  g.measured_fps = 0;

  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(g.rotation);
  tft.invertDisplay(g.invert);
  tft.fillScreen(TFT_BLACK);
  backlight_pwm_begin();

  scene_init();
  weather_begin();  // may apply last-known weather from NVS
  scene_snap();

  Serial.println();
  Serial.println(F("================================"));
  Serial.printf("%s %s  visual-port-of pixel-window 0.2.0\n", FIRMWARE_NAME,
                FIRMWARE_VERSION);
  Serial.println(F("tiny window / lake  |  Open-Meteo  |  rot=3 (180)"));
  Serial.println(F("================================"));
  log_status();
  weather_log_status();
  Serial.println(F("default mode=scene brightness=90  (send: test)"));
  Serial.println(F("swapBytes=1  (pushPixels endian matches fillRect/pushBlock)"));
  log_help();

  last_frame_ms = millis();
}

void loop() {
  serial_poll();
  weather_loop();

  uint32_t now = millis();
  uint32_t min_dt = 1000u / (g.fps ? g.fps : 1);
  if ((uint32_t)(now - last_frame_ms) < min_dt) {
    delay(1);  // frame cap; never deep-sleep
    return;
  }
  float dt = (now - last_frame_ms) / 1000.0f;
  if (dt > 0.25f) dt = 0.25f;
  last_frame_ms = now;
  g.t += dt;
  g.frames++;
  fps_window_frames++;

  // Phase follows the local clock whenever auto is on (live weather default).
  // 0.1.3 only did this when live was off, so a stale morning observation
  // could stick all day if a later fetch failed or the clock had no NTP.
  if (g.phase_auto && weather_clock_valid()) {
    time_t ts = time(nullptr);
    struct tm tmnow;
    localtime_r(&ts, &tmnow);
    Phase np = phase_for_hour(tmnow.tm_hour);
    if (np != g.phase) {
      g.phase = np;
      scene_invalidate();
      if (!g.quiet) {
        Serial.printf("phase auto -> %s (local %02d:%02d JST)\n", phase_name(g.phase),
                      tmnow.tm_hour, tmnow.tm_min);
      }
    }
  }

  if (g.mode == MODE_TEST) {
    bool need = test_dirty || last_test_bl != g.brightness || last_test_rot != g.rotation ||
                last_test_inv != g.invert || last_test_swap != g.swap_rb;
    if (need) {
      lcd_test_draw();
      test_dirty = false;
      last_test_bl = g.brightness;
      last_test_rot = g.rotation;
      last_test_inv = g.invert;
      last_test_swap = g.swap_rb;
    }
  } else {
    scene_draw(g.t);
    push_fb_2x();
  }

  if (now - g.last_fps_ms >= 3000) {
    g.measured_fps = (uint16_t)((fps_window_frames * 1000ul) / (now - g.last_fps_ms));
    fps_window_frames = 0;
    g.last_fps_ms = now;
    if (!g.quiet) {
      char clk[32];
      weather_format_clock(clk, sizeof(clk));
      Serial.printf("log fps_meas=%u heap=%u mode=%s weather=%s phase=%s bl=%u clk=%s\n",
                    g.measured_fps, (unsigned)ESP.getFreeHeap(),
                    g.mode == MODE_TEST ? "test" : "scene", weather_name(g.weather),
                    phase_name(g.phase), g.brightness, clk);
    }
  }
}
