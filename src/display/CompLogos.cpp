#include "CompLogos.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

uint8_t  gCompLogoLgW[3] = {};
uint8_t  gCompLogoSmW[3] = {};
uint16_t* gCompLogoLg[3] = {nullptr, nullptr, nullptr};
uint16_t* gCompLogoSm[3] = {nullptr, nullptr, nullptr};

static const char* SLUGS[] = {"comp_top14", "comp_prod2", "comp_cc"};

// Returns actual pixel width loaded (0 on failure).
// Width is derived from file size: pixels = size/2, width = pixels/height.
static uint8_t loadBin(const char* slug, bool sm, uint16_t* buf,
                        uint8_t maxW, uint8_t fixedH) {
    char path[48];
    snprintf(path, sizeof(path), "/logos/%s%s.bin", slug, sm ? "_sm" : "");
    File f = LittleFS.open(path, "r");
    if (!f) { Serial.printf("CompLogos: missing %s\n", path); return 0; }
    size_t sz = f.size();
    uint8_t w = (uint8_t)min((size_t)(sz / (2u * fixedH)), (size_t)maxW);
    if (w == 0) { f.close(); return 0; }
    f.read((uint8_t*)buf, (size_t)w * fixedH * 2);
    f.close();
    Serial.printf("CompLogos: %s → %dx%d\n", path, w, fixedH);
    return w;
}

void loadCompLogos() {
    for (int i = 0; i < 3; i++) {
        if (!gCompLogoLg[i]) {
            gCompLogoLg[i] = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * LOGO_COMP_MAX_W * LOGO_COMP_H, MALLOC_CAP_SPIRAM);
        }
        if (!gCompLogoSm[i]) {
            gCompLogoSm[i] = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * LOGO_COMP_SM_MAX_W * LOGO_COMP_SM_H, MALLOC_CAP_SPIRAM);
        }
        if (gCompLogoLg[i]) {
            gCompLogoLgW[i] = loadBin(SLUGS[i], false, gCompLogoLg[i], LOGO_COMP_MAX_W, LOGO_COMP_H);
        }
        if (gCompLogoSm[i]) {
            gCompLogoSmW[i] = loadBin(SLUGS[i], true,  gCompLogoSm[i], LOGO_COMP_SM_MAX_W, LOGO_COMP_SM_H);
        }
    }
}

int compIndex(const char* comp) {
    if (strstr(comp, "14")) return 0;
    if (strstr(comp, "D2")) return 1;
    return 2;
}
