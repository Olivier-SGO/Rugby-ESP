#pragma once
#include <cstdint>
#include "config.h"

// Loads a pre-converted RGB565 .bin logo from LittleFS.
// Caller owns the returned buffer (must free with delete[]).
// Returns nullptr on failure.
uint16_t* loadLogo(const char* slug, bool small = false);

// Draws a 16x16 logo directly from LittleFS with no heap allocation (512-byte stack buffer).
// Returns false if the file is absent.
bool drawLogoFromFS(int16_t x, int16_t y, const char* slug);
