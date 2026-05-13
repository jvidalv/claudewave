#pragma once

#include <Arduino_GFX_Library.h>
#include <stdint.h>

namespace sessions {

enum class Status : uint8_t {
  Working,  // agent turn in flight (tool use / thinking / generating)
  Waiting,  // needs user input — last event was a question or permission prompt
  Idle,     // session open, nothing pending
  Done,     // session closed cleanly
  Error,    // last event was a tool failure or crash
};

struct Session {
  const char *name;
  Status      status;
  // Milliseconds since boot when this session last produced any activity.
  // Used both to sort the list and to drive the "fall back to splash if
  // nothing happened recently" gate in main.
  uint32_t    last_activity_ms;
};

void begin(Arduino_GFX *display);

// Replace the in-memory list. Triggers a repaint on the next show()/tick().
void set(const Session *items, uint16_t count);

// True if any session reported activity within `window_ms` of now. Note this
// is "saw an event recently", not "status == Working".
bool has_recent_activity(uint32_t window_ms);

void show();
void tick();

}  // namespace sessions
