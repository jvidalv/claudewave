#pragma once

#include <Arduino_GFX_Library.h>

namespace splash {

bool begin(Arduino_GFX *display);
void tick();

}  // namespace splash
