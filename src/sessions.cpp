#include "sessions.h"

#include <Arduino.h>
#include <math.h>
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

constexpr uint8_t     kRowScale    = 4;
// Title baseline anchor. Title is drawn above this line; list starts
// kTitleToListGap pixels below it. Keeps the two adjustable independently.
constexpr int16_t     kRowsAreaTop     = 110;
constexpr int16_t     kTitleToListGap  = 40;
constexpr int16_t     kRowsAreaBot = 580;
constexpr uint16_t    kRowsMax     = 6;
// 14 cells × 24 px = 336 px name column; the icon claims the rest.
constexpr uint16_t    kNameMaxChars = 14;

// Icon slot — square cell on the left of each row.
constexpr int16_t kIconSize   = 36;
constexpr int16_t kIconLeft   = 16;          // x offset of icon's left edge
constexpr int16_t kNameLeft   = kIconLeft + kIconSize + 12;
constexpr int16_t kRowHeight  = kRowScale * 8;  // GFX 6×8 cell × scale

// Spinner cadence — one "tick" every kSpinnerStepMs, kSpinnerPhases per loop.
// 16 phases at 60 ms = 960 ms per revolution; feels lively without being frantic.
constexpr uint32_t kSpinnerStepMs = 60;
constexpr uint8_t  kSpinnerPhases = 16;
constexpr uint8_t  kCometLength   = 6;

// Status palette
constexpr uint16_t kColWorking  = 0x4D9F;   // soft blue
constexpr uint16_t kColWaiting  = 0xFD20;   // amber
constexpr uint16_t kColIdle     = 0xC618;   // mid gray (75% lightness)
constexpr uint16_t kColDone     = 0x07E0;   // pure RGB565 green — was too dim
constexpr uint16_t kColError    = 0xF800;   // pure RGB565 red — was too brown
constexpr uint16_t kColInactive = 0xEF7D;   // ~90% gray — clearly readable

// Internal state ---------------------------------------------------------
Arduino_GFX *s_gfx = nullptr;

constexpr uint16_t kMaxSessions = 32;
sessions::Session  s_items[kMaxSessions];
uint16_t           s_count = 0;
bool               s_dirty = true;  // full repaint on next tick/show

// Per-row y-coordinate of the icon slot's top edge; populated by repaint()
// and reused by tick() to repaint only the icon cells.
int16_t  s_icon_y[kRowsMax]  = {0};
// Indices into s_items for the rows currently shown, in display order.
uint16_t s_shown_idx[kRowsMax] = {0};
uint16_t s_shown_count = 0;

uint8_t  s_spinner_phase   = 0;
uint32_t s_spinner_last_ms = 0;

// Scale an RGB565 colour by `num/256` brightness. Used to fade comet-trail
// dots without needing a per-pixel palette.
uint16_t scale_color(uint16_t c, uint8_t num) {
  uint32_t r = (c >> 11) & 0x1F;
  uint32_t g = (c >> 5)  & 0x3F;
  uint32_t b =  c        & 0x1F;
  r = (r * num) >> 8;
  g = (g * num) >> 8;
  b = (b * num) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

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

bool status_animates(sessions::Status s) {
  return s == sessions::Status::Working || s == sessions::Status::Waiting;
}

// Draw the status icon centered in a kIconSize × kIconSize slot at (x, y).
// Wipes a slightly oversized region first so animated icons can't leave
// stray pixels just outside the nominal slot.
void draw_icon(int16_t x, int16_t y, sessions::Status status, uint8_t phase) {
  constexpr int16_t kSlotPad = 4;
  s_gfx->fillRect(x - kSlotPad, y - kSlotPad,
                  kIconSize + 2 * kSlotPad, kIconSize + 2 * kSlotPad,
                  RGB565_BLACK);
  const int16_t cx = x + kIconSize / 2;
  const int16_t cy = y + kIconSize / 2;
  const uint16_t color = status_color(status);

  switch (status) {
    case sessions::Status::Working: {
      // Comet: bright leading dot with a fading trail behind it on a
      // circular orbit. Trail dots are drawn first so the head paints on
      // top — gives the impression of a head pulling colour through space.
      const int16_t r = kIconSize / 2 - 4;
      for (int i = kCometLength - 1; i >= 0; --i) {
        const int8_t  step = (int8_t)phase - (int8_t)i;
        const uint8_t p    = (uint8_t)((step + kSpinnerPhases * 4) % kSpinnerPhases);
        const float   ang  = (float)p * (2.0f * PI / (float)kSpinnerPhases);
        const int16_t dx   = (int16_t)(cosf(ang) * (float)r);
        const int16_t dy   = (int16_t)(sinf(ang) * (float)r);
        // Head = full brightness, trail fades to ~25%.
        const uint8_t bright = (uint8_t)(255 - i * 38);
        const int16_t dot_r  = (i == 0) ? 5 : (i < 3 ? 4 : 3);
        s_gfx->fillCircle(cx + dx, cy + dy, dot_r, scale_color(color, bright));
      }
      break;
    }
    case sessions::Status::Waiting: {
      // Filled amber disc with a white exclamation. Disc brightness
      // breathes between 70% and 100% so the row reads as "wanting
      // attention" but is always visible.
      const float t = (float)phase / (float)kSpinnerPhases;
      const float pulse = 0.85f + 0.15f * cosf(t * 2.0f * PI);
      const uint8_t bright = (uint8_t)(pulse * 255.0f);
      const uint16_t c = scale_color(color, bright);
      const int16_t r = kIconSize / 2 - 3;
      s_gfx->fillCircle(cx, cy, r, c);
      // "!" — vertical bar over a dot.
      s_gfx->fillRect(cx - 2, cy - 9, 5, 10, RGB565_WHITE);
      s_gfx->fillRect(cx - 2, cy + 4, 5, 5,  RGB565_WHITE);
      break;
    }
    case sessions::Status::Idle: {
      // Two filled vertical bars — pause glyph, unambiguous.
      const int16_t bar_w = 6;
      const int16_t bar_h = kIconSize - 14;
      s_gfx->fillRect(cx - bar_w - 3, cy - bar_h / 2, bar_w, bar_h, color);
      s_gfx->fillRect(cx + 3,         cy - bar_h / 2, bar_w, bar_h, color);
      break;
    }
    case sessions::Status::Done: {
      // Filled green disc with a white check painted on top — keep it as
      // simple and high-contrast as possible so it can never be missed.
      const int16_t r = kIconSize / 2 - 3;
      s_gfx->fillCircle(cx, cy, r, color);
      const int16_t p1x = cx - 8, p1y = cy;
      const int16_t p2x = cx - 2, p2y = cy + 7;
      const int16_t p3x = cx + 9, p3y = cy - 6;
      for (int o = -1; o <= 1; ++o) {
        s_gfx->drawLine(p1x, p1y + o, p2x, p2y + o, RGB565_WHITE);
        s_gfx->drawLine(p2x, p2y + o, p3x, p3y + o, RGB565_WHITE);
      }
      break;
    }
    case sessions::Status::Error: {
      // Filled red disc with a white X.
      const int16_t r = kIconSize / 2 - 3;
      s_gfx->fillCircle(cx, cy, r, color);
      const int16_t a = r - 6;
      for (int o = -1; o <= 1; ++o) {
        s_gfx->drawLine(cx - a, cy - a + o, cx + a, cy + a + o, RGB565_WHITE);
        s_gfx->drawLine(cx - a, cy + a + o, cx + a, cy - a - o, RGB565_WHITE);
      }
      break;
    }
  }
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

void draw_name(int16_t y, const char *name) {
  s_gfx->setTextSize(kRowScale);
  s_gfx->setTextColor(0xFFFF);
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
  s_shown_count = 0;

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
    const int16_t icon_y = text_y + (kRowHeight - kIconSize) / 2;
    const sessions::Session &s = s_items[order[i]];
    draw_icon(kIconLeft, icon_y, s.status, s_spinner_phase);
    draw_name(text_y, s.name);
    s_icon_y[i]      = icon_y;
    s_shown_idx[i]   = order[i];
  }
  s_shown_count = shown;

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

// Repaint only the icon slots — used between full repaints to animate the
// spinner without touching the name column.
void repaint_icons() {
  for (uint16_t i = 0; i < s_shown_count; ++i) {
    const sessions::Session &s = s_items[s_shown_idx[i]];
    draw_icon(kIconLeft, s_icon_y[i], s.status, s_spinner_phase);
  }
}

bool any_animated() {
  for (uint16_t i = 0; i < s_shown_count; ++i) {
    if (status_animates(s_items[s_shown_idx[i]].status)) return true;
  }
  return false;
}

}  // namespace

namespace sessions {

void begin(Arduino_GFX *display) {
  s_gfx = display;
  s_count = 0;
  s_dirty = true;
  s_spinner_phase   = 0;
  s_spinner_last_ms = millis();
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
  if (s_dirty) {
    repaint();
    return;
  }
  if (!any_animated()) return;
  const uint32_t now = millis();
  if (now - s_spinner_last_ms < kSpinnerStepMs) return;
  s_spinner_last_ms = now;
  s_spinner_phase   = (s_spinner_phase + 1) % kSpinnerPhases;
  repaint_icons();
}

}  // namespace sessions
