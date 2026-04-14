#pragma once
#include <cstdint>
#include "config.h"

// Loads a pre-converted RGB565 .bin logo from LittleFS.
// Caller owns the returned buffer (must free with delete[]).
// Returns nullptr on failure.
uint16_t* loadLogo(const char* slug, bool small = false);
