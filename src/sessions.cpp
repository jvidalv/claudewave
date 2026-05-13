#include "sessions.h"

#include <Arduino.h>
#include <string.h>

namespace {

constexpr int16_t kPanelW = 450;
constexpr int16_t kPanelH = 600;

constexpr const char *kTitle       = "claudewave";
constexpr uint8_t     kTitleScale  = 7;
constexpr int16_t     kTitleMargin = 12;
// Same warm tan that the splash creature's body uses, so the brand mark
// stays consistent when we cross-fade between screens.
constexpr uint16_t    kTitleColor  = 0xCBED;

constexpr uint8_t     kRowScale       = 4;  // 24 px tall glyphs
constexpr int16_t     kRowsAreaTop    = 110;
constexpr int16_t     kTitleToListGap = 40;
constexpr int16_t     kRowsAreaBot    = 580;
constexpr uint16_t    kRowsMax        = 6;
// At scale=4 the GFX 6-px cell × 17 chars = 408 px, fitting the 450 panel
// with margins. Status is conveyed by row text colour, not an icon.
constexpr uint16_t    kNameMaxChars   = 17;
constexpr int16_t     kNameLeft       = 20;

// Status palette
constexpr uint16_t kColWorking  = 0x4D9F;   // soft blue
constexpr uint16_t kColWaiting  = 0xFD20;   // amber
constexpr uint16_t kColIdle     = 0xC618;   // mid gray
constexpr uint16_t kColDone     = 0x07E0;   // green
constexpr uint16_t kColError    = 0xF800;   // red
constexpr uint16_t kColInactive = 0xEF7D;   // ~90% gray for "+N inactive"

Arduino_GFX *s_gfx = nullptr;

constexpr uint16_t kMaxSessions = 32;
sessions::Session  s_items[kMaxSessions];
uint16_t           s_count = 0;
bool               s_dirty = true;

uint16_t status_color(sessions::Status s) {
  switch (s) {
    case sessions::Status::Working: return kColWorking;
    case sessions::Status::Waiting: return kColWaiting;
    case sessions::Status::Idle:    return kColIdle;
    case sessions::Status::Done:    return kColDone;
    case sessions::Status::Error:   return kColError;
  }
  return kColIdle;
}

void draw_title() {
  s_gfx->fillRect(0, 0, kPanelW, kRowsAreaTop, RGB565_BLACK);
  s_gfx->setTextColor(kTitleColor);
  s_gfx->setTextSize(kTitleScale);
  int16_t  x1 = 0, y1 = 0;
  uint16_t tw = 0, th = 0;
  s_gfx->getTextBounds(kTitle, 0, 0, &x1, &y1, &tw, &th);
  s_gfx->setCursor((kPanelW - (int16_t)tw) / 2 - x1,
                   kRowsAreaTop - kTitleMargin - (int16_t)th - y1);
  s_gfx->print(kTitle);
}

// Insertion sort indices by last_activity_ms desc.
void sorted_indices(uint16_t *out) {
  for (uint16_t i = 0; i < s_count; ++i) out[i] = i;
  for (uint16_t i = 1; i < s_count; ++i) {
    uint16_t j = i;
    while (j > 0 &&
           s_items[out[j - 1]].last_activity_ms <
               s_items[out[j]].last_activity_ms) {
      uint16_t tmp = out[j];
      out[j] = out[j - 1];
      out[j - 1] = tmp;
      --j;
    }
  }
}

void draw_row(int16_t y, const char *name, uint16_t color) {
  s_gfx->setTextSize(kRowScale);
  s_gfx->setTextColor(color);
  s_gfx->setCursor(kNameLeft, y);
  char buf[kNameMaxChars + 1];
  const size_t n = strnlen(name, kNameMaxChars);
  memcpy(buf, name, n);
  buf[n] = '\0';
  s_gfx->print(buf);
}

void repaint() {
  s_gfx->fillScreen(RGB565_BLACK);
  draw_title();

  if (s_count == 0) {
    s_gfx->setTextSize(kRowScale);
    s_gfx->setTextColor(kColIdle);
    const char *msg = "no sessions";
    int16_t  x1 = 0, y1 = 0;
    uint16_t tw = 0, th = 0;
    s_gfx->getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
    s_gfx->setCursor((kPanelW - (int16_t)tw) / 2 - x1,
                     kRowsAreaTop + kTitleToListGap);
    s_gfx->print(msg);
    s_dirty = false;
    return;
  }

  uint16_t order[kMaxSessions];
  sorted_indices(order);

  const uint16_t shown    = (s_count > kRowsMax) ? kRowsMax : s_count;
  const int16_t  list_top = kRowsAreaTop + kTitleToListGap;
  const int16_t  step     = (kRowsAreaBot - list_top) / (kRowsMax + 1);

  for (uint16_t i = 0; i < shown; ++i) {
    const int16_t text_y = list_top + (int16_t)i * step;
    const sessions::Session &s = s_items[order[i]];
    draw_row(text_y, s.name, status_color(s.status));
  }

  const uint16_t hidden = s_count - shown;
  if (hidden > 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "+%u inactive", (unsigned)hidden);
    s_gfx->setTextSize(kRowScale - 1);
    s_gfx->setTextColor(kColInactive);
    int16_t  x1 = 0, y1 = 0;
    uint16_t tw = 0, th = 0;
    s_gfx->getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
    s_gfx->setCursor((kPanelW - (int16_t)tw) / 2 - x1,
                     kRowsAreaBot - (int16_t)th - 4);
    s_gfx->print(buf);
  }

  s_dirty = false;
}

}  // namespace

namespace sessions {

void begin(Arduino_GFX *display) {
  s_gfx = display;
  s_count = 0;
  s_dirty = true;
}

void set(const Session *items, uint16_t count) {
  if (count > kMaxSessions) count = kMaxSessions;
  for (uint16_t i = 0; i < count; ++i) s_items[i] = items[i];
  s_count = count;
  s_dirty = true;
}

bool has_recent_activity(uint32_t window_ms) {
  const uint32_t now = millis();
  for (uint16_t i = 0; i < s_count; ++i) {
    if (now - s_items[i].last_activity_ms <= window_ms) return true;
  }
  return false;
}

void show() {
  s_dirty = true;
  repaint();
}

void tick() {
  if (s_dirty) repaint();
}

}  // namespace sessions
