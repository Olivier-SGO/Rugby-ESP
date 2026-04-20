#include "LogoLoader.h"
#include "DisplayManager.h"
#include <LittleFS.h>
#include <Arduino.h>

bool drawLogoFromFS(int16_t x, int16_t y, const char* slug) {
    if (!slug || slug[0] == '\0') return false;
    char path[48];
    snprintf(path, sizeof(path), "/logos/%s_sm.bin", slug);
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    uint16_t buf[LOGO_SM_W * LOGO_SM_H];  // 512 bytes on stack
    size_t n = f.read((uint8_t*)buf, sizeof(buf));
    f.close();
    if (n == sizeof(buf))
        Display.drawBitmap565(x, y, LOGO_SM_W, LOGO_SM_H, buf);
    return n == sizeof(buf);
}
