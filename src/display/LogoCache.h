#pragma once
#include <cstdint>

// Pre-load all RGB565 .bin logos from LittleFS into PSRAM at boot.
// Once initialized, getLogo() returns a pointer into PSRAM with zero heap/SRAM usage
// and no filesystem I/O during rendering.

bool initLogoCache();

// Get a logo from PSRAM. Returns nullptr if not found (should not happen after preload).
// small=true → 16x16, small=false → 64x64
const uint16_t* getLogo(const char* slug, bool small = false);
