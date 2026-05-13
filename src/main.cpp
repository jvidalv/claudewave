#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "sessions.h"
#include "splash.h"

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

// Fall back to splash only after a real lull: 15 min without any session
// reporting activity. Brief silences keep the sessions screen on.
constexpr uint32_t kIdleSplashThresholdMs = 15UL * 60 * 1000;

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    kLcdCs, kLcdSck, kLcdD0, kLcdD1, kLcdD2, kLcdD3);

static Arduino_GFX *gfx = new Arduino_CO5300(
    bus, kLcdReset, 0 /* rotation */, kLcdWidth, kLcdHeight,
    kLcdColOffset, 0, 0, 0);

enum class Screen { None, Splash, Sessions };
static Screen s_screen = Screen::None;

// Synthetic data exercising both screens. `last_activity_ms` is *relative
// to boot* — set in setup() once we know millis(). Six "recent" entries
// fill the 6-row list; two "stale" entries collapse into "+2 inactive".
// 30 s after boot every entry is past the 10 s threshold so the screen
// flips to splash on its own.
struct FakeRow {
  const char        *name;
  sessions::Status   status;
  uint32_t           ago_ms;  // how long ago this session was last active
};

static const FakeRow kFake[] = {
    {"claudewave",       sessions::Status::Working,    0},
    {"twitter-bot",      sessions::Status::Waiting,    1500},
    {"infra-migration",  sessions::Status::Working,    3000},
    {"taxes-2025",       sessions::Status::Done,       4500},
    {"birthday-cards",   sessions::Status::Error,      6000},
    {"newsletter-draft", sessions::Status::Idle,       7500},
    {"old-experiment-a", sessions::Status::Idle,       60000},
    {"old-experiment-b", sessions::Status::Done,       90000},
};

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

  constexpr size_t kN = sizeof(kFake) / sizeof(kFake[0]);
  sessions::Session list[kN];
  const uint32_t now = millis();
  for (size_t i = 0; i < kN; ++i) {
    list[i].name             = kFake[i].name;
    list[i].status           = kFake[i].status;
    list[i].last_activity_ms = (kFake[i].ago_ms > now) ? 0 : now - kFake[i].ago_ms;
  }
  sessions::set(list, kN);
}

void loop() {
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
