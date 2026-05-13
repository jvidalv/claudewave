// Animation data and original render kernel lifted from
// HermannBjorgvin/Clawdmeter (MIT). Adapted from LVGL canvas to direct
// Arduino_GFX blits.

#include "splash.h"
#include "splash_animations.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

constexpr int      kGrid     = 20;
constexpr int      kCell     = 22;
constexpr int      kCanvas   = kGrid * kCell;          // 440 × 440
constexpr int      kPanelW   = 450;
constexpr int      kPanelH   = 600;
constexpr int16_t  kOriginX  = (kPanelW - kCanvas) / 2; // 5
constexpr int16_t  kOriginY  = (kPanelH - kCanvas) / 2; // 80

constexpr uint32_t kRotateMs = 20000;

constexpr const char *kTitle       = "claudewave";
constexpr uint8_t     kTitleScale  = 7;
constexpr int16_t     kTitleMargin = 12;

// Palette slot used as the "body" colour of the Clawdmeter creatures —
// also the colour we paint the title in so the two read as one piece.
constexpr uint8_t kBodyPaletteIndex = 1;

Arduino_GFX *s_gfx        = nullptr;
uint16_t    *s_canvas     = nullptr;
uint16_t     s_cur_anim   = 0;
uint16_t     s_cur_frame  = 0;
uint32_t     s_frame_started_ms = 0;
uint32_t     s_last_rotate_ms   = 0;

void render_frame(const uint8_t *cells, const uint16_t *palette) {
  for (int gy = 0; gy < kGrid; ++gy) {
    uint16_t row[kCanvas];
    for (int gx = 0; gx < kGrid; ++gx) {
      uint8_t code = cells[gy * kGrid + gx];
      uint16_t color =
          (palette && code < SPLASH_PALETTE_SIZE) ? palette[code] : RGB565_BLACK;
      uint16_t *p = &row[gx * kCell];
      for (int i = 0; i < kCell; ++i) p[i] = color;
    }
    for (int dy = 0; dy < kCell; ++dy) {
      memcpy(&s_canvas[(gy * kCell + dy) * kCanvas], row,
             kCanvas * sizeof(uint16_t));
    }
  }
  s_gfx->draw16bitRGBBitmap(kOriginX, kOriginY, s_canvas, kCanvas, kCanvas);
}

void draw_title(uint16_t color) {
  // Wipe the strip above the canvas — a previous animation's palette may
  // have left a brighter colour here than the new one renders.
  s_gfx->fillRect(0, 0, kPanelW, kOriginY, RGB565_BLACK);

  s_gfx->setTextColor(color);
  s_gfx->setTextSize(kTitleScale);
  int16_t  x1 = 0, y1 = 0;
  uint16_t tw = 0, th = 0;
  s_gfx->getTextBounds(kTitle, 0, 0, &x1, &y1, &tw, &th);
  s_gfx->setCursor((kPanelW - (int16_t)tw) / 2 - x1,
                   kOriginY - kTitleMargin - (int16_t)th - y1);
  s_gfx->print(kTitle);
}

void show(uint16_t idx) {
  s_cur_anim  = idx % SPLASH_ANIM_COUNT;
  s_cur_frame = 0;
  const splash_anim_def_t *a = &splash_anims[s_cur_anim];
  draw_title(a->palette[kBodyPaletteIndex]);
  render_frame(a->frames[0], a->palette);
  Serial.printf("splash: -> %s\n", a->name);
}

}  // namespace

namespace splash {

bool begin(Arduino_GFX *display) {
  s_gfx = display;
  if (!s_canvas) {
    s_canvas = (uint16_t *)heap_caps_malloc(
        kCanvas * kCanvas * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_canvas) {
      Serial.println("splash: PSRAM alloc failed");
      return false;
    }
  }
  s_gfx->fillScreen(RGB565_BLACK);
  // Resume on the same animation we were on (keeps the cycle continuous
  // across screen flips); if we're re-entering after a hide, that means
  // jumping to the next animation feels less abrupt than restarting at 0.
  show(s_cur_anim);
  const uint32_t now = millis();
  s_frame_started_ms = now;
  s_last_rotate_ms   = now;
  return true;
}

void tick() {
  if (!s_canvas) return;
  const uint32_t now = millis();
  if (now - s_last_rotate_ms >= kRotateMs) {
    show(s_cur_anim + 1);
    s_frame_started_ms = now;
    s_last_rotate_ms   = now;
    return;
  }
  const splash_anim_def_t *a = &splash_anims[s_cur_anim];
  if (a->frame_count == 0) return;
  if (now - s_frame_started_ms < a->holds[s_cur_frame]) return;
  s_cur_frame = (s_cur_frame + 1) % a->frame_count;
  s_frame_started_ms = now;
  render_frame(a->frames[s_cur_frame], a->palette);
}

}  // namespace splash
