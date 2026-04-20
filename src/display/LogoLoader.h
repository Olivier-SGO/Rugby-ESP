#pragma once
#include <cstdint>
#include "config.h"
#include "LogoCache.h"

// Returns a logo from the PSRAM cache. nullptr if absent.
// The returned pointer is owned by the cache — caller must NOT free it.
inline const uint16_t* loadLogo(const char* slug, bool small = false) {
    return getLogo(slug, small);
}

// Legacy fallback: draws a 16x16 logo directly from LittleFS with a 512-byte stack buffer.
// Returns false if the file is absent.
bool drawLogoFromFS(int16_t x, int16_t y, const char* slug);
