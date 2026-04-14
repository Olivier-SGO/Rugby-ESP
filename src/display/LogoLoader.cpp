#include "LogoLoader.h"
#include <LittleFS.h>
#include <Arduino.h>

uint16_t* loadLogo(const char* slug, bool small) {
    if (!slug || slug[0] == '\0') return nullptr;

    char path[64];
    if (small)
        snprintf(path, sizeof(path), "/logos/%s_sm.bin", slug);
    else
        snprintf(path, sizeof(path), "/logos/%s.bin", slug);

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("Logo not found: %s\n", path);
        return nullptr;
    }

    int w = small ? LOGO_SM_W : LOGO_LG_W;
    int h = small ? LOGO_SM_H : LOGO_LG_H;
    size_t sz = w * h * sizeof(uint16_t);

    uint16_t* buf = new uint16_t[w * h];
    if (!buf) { f.close(); return nullptr; }

    f.read((uint8_t*)buf, sz);
    f.close();
    return buf;
}
