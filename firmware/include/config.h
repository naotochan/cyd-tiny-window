#pragma once

#include <stdint.h>

#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME "tiny-window"
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.4"
#endif
#ifndef PROFILE_NAME
#define PROFILE_NAME "unknown"
#endif
#ifndef PROFILE_DRIVER
#define PROFILE_DRIVER "unknown"
#endif
#ifndef PROFILE_COLOR
#define PROFILE_COLOR "?"
#endif
#ifndef PROFILE_INVERT
#define PROFILE_INVERT 0
#endif

// Landscape 320x240. v0.1.1 used 1; v0.1.2 uses 3 (180°) for the case-mounted panel.
// Override at build time: -DDEFAULT_ROTATION=1
#ifndef DEFAULT_ROTATION
#define DEFAULT_ROTATION 3
#endif

#ifndef DEFAULT_LATITUDE
#define DEFAULT_LATITUDE 35.68f
#endif
#ifndef DEFAULT_LONGITUDE
#define DEFAULT_LONGITUDE 139.76f
#endif

#ifndef WIFI_SETUP_AP_NAME
#define WIFI_SETUP_AP_NAME "TinyWindow-Setup"
#endif

#ifndef WEATHER_FETCH_INTERVAL_MS
#define WEATHER_FETCH_INTERVAL_MS 600000UL  // 10 minutes
#endif

#ifndef WEATHER_TRANSITION_MS
#define WEATHER_TRANSITION_MS 4000UL
#endif

// Internal scene buffer (pixel-window v0.2.0) and 2x nearest to the panel.
static const int SW = 160;
static const int SH = 120;
static const int LCD_W = 320;
static const int LCD_H = 240;
static const int HORIZON = 74;

// Documented CYD / ESP32-2432S028 pins. Only these outputs are driven.
static const int PIN_TFT_MISO = 12;
static const int PIN_TFT_MOSI = 13;
static const int PIN_TFT_SCLK = 14;
static const int PIN_TFT_CS = 15;
static const int PIN_TFT_DC = 2;
static const int PIN_TFT_BL = 21;
static const int PIN_LED_R = 4;   // active LOW
static const int PIN_LED_G = 16;  // active LOW
static const int PIN_LED_B = 17;  // active LOW

static const int BL_LEDC_CH = 0;
static const int BL_LEDC_FREQ = 5000;
static const int BL_LEDC_BITS = 8;

static const uint32_t SERIAL_BAUD = 115200;
static const int DEFAULT_FPS = 15;
static const int DEFAULT_BRIGHTNESS = 90;  // 0.1.3: ILI9341 midtones sit dark; 90 keeps heat modest vs 255

static const int MAX_CLOUD_BLOBS = 48;
static const int MAX_DROPS = 90;
static const int MAX_RIPPLES = 9;
static const int MAX_GLINTS = 26;
static const int MAX_REEDS = 20;
static const int MAX_TREES = 4;
