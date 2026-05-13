#include <Arduino.h>
#include <Arduino_GFX_Library.h>

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

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    kLcdCs, kLcdSck, kLcdD0, kLcdD1, kLcdD2, kLcdD3);

static Arduino_GFX *gfx = new Arduino_CO5300(
    bus, kLcdReset, 0 /* rotation */, kLcdWidth, kLcdHeight,
    kLcdColOffset, 0, 0, 0);

void setup() {
  Serial.begin(115200);
  Serial.println("ClaudeWave: booting");

  while (!gfx->begin()) {
    Serial.println("display init failed, retrying in 1s");
    delay(1000);
  }
  splash::begin(gfx);
}

void loop() {
  splash::tick();
  delay(20);
}
