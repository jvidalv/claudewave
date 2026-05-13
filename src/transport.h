#pragma once

#include "sessions.h"

namespace transport {

// Poll the USB-CDC Serial port for newline-terminated JSON payloads of the
// shape {"v":1,"s":[{"n":"…","st":"working|waiting|idle|done|error",
// "ago":<ms>}…]} and forward parsed snapshots to sessions::set(). Call
// from loop().
void tick();

}  // namespace transport
