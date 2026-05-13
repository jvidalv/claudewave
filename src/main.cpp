#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "sessions.h"
#include "splash.h"
#include "transport.h"

// Pins from espressif/arduino-esp32
// variants/waveshare_esp32_s3_touch_amoled_241/pins_arduino.h.
constexpr int8_t  kLcdCs    = 9;
constexpr int8_t  kLcdSck   = 10;
constexpr int8_t  kLcdD0    = 11;
constexpr int8_t  kLcdD1    = 12;
constexpr int8_t  kLcdD2    = 13;
constexpr int8_t  kLcdD3    = 14;
constexpr int8_t  kLcdReset = 21;

constexpr int16_t kLcdWidth     = 450;
constexpr int16_t kLcdHeight    = 600;
// CO5300 panel-specific addressing offset (matches Waveshare 2.06/2.41 demos).
constexpr uint8_t kLcdColOffset = 22;

// Fall back to splash after a real lull: 15 min without any session
// reporting activity. Brief silences keep the sessions screen on.
constexpr uint32_t kIdleSplashThresholdMs = 15UL * 60 * 1000;

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    kLcdCs, kLcdSck, kLcdD0, kLcdD1, kLcdD2, kLcdD3);

static Arduino_GFX *gfx = new Arduino_CO5300(
    bus, kLcdReset, 0 /* rotation */, kLcdWidth, kLcdHeight,
    kLcdColOffset, 0, 0, 0);

enum class Screen { None, Splash, Sessions };
static Screen s_screen = Screen::None;

void setup() {
  Serial.begin(115200);
  Serial.println("ClaudeWave: booting");

  while (!gfx->begin()) {
    Serial.println("display init failed, retrying in 1s");
    delay(1000);
  }
  gfx->fillScreen(RGB565_BLACK);

  splash::begin(gfx);
  sessions::begin(gfx);
  // Until the host script connects and pushes a snapshot, the session list
  // is empty so the splash takes the screen.
}

void loop() {
  transport::tick();

  const Screen want =
      sessions::has_recent_activity(kIdleSplashThresholdMs)
          ? Screen::Sessions
          : Screen::Splash;

  if (want != s_screen) {
    gfx->fillScreen(RGB565_BLACK);
    if (want == Screen::Sessions) {
      sessions::show();
    } else {
      splash::begin(gfx);
    }
    s_screen = want;
  }

  if (s_screen == Screen::Sessions) {
    sessions::tick();
  } else {
    splash::tick();
  }
  delay(20);
}
