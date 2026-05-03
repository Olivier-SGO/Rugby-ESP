#include "DisplayManager.h"
#include <Arduino.h>

DisplayManager Display;

bool DisplayManager::begin() {
    if (_panel) {
        Serial.println("[WARN] Display.begin() called twice — ignored");
        return true;
    }
    HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, CHAIN_LEN);
    cfg.gpio.r1  = HUB75_R1;  cfg.gpio.g1  = HUB75_G1;  cfg.gpio.b1  = HUB75_B1;
    cfg.gpio.r2  = HUB75_R2;  cfg.gpio.g2  = HUB75_G2;  cfg.gpio.b2  = HUB75_B2;
    cfg.gpio.a   = HUB75_A;   cfg.gpio.b   = HUB75_B;   cfg.gpio.c   = HUB75_C;
    cfg.gpio.d   = HUB75_D;   cfg.gpio.e   = HUB75_E;
    cfg.gpio.lat = HUB75_LAT; cfg.gpio.oe  = HUB75_OE;  cfg.gpio.clk = HUB75_CLK;
    cfg.double_buff = false;
    cfg.clkphase = false;

    _panel = new MatrixPanel_I2S_DMA(cfg);
    if (!_panel->begin()) {
        delete _panel;
        _panel = nullptr;
        return false;
    }
    _panel->setBrightness8(_brightness);
    _panel->clearScreen();
    return true;
}

bool DisplayManager::end() {
    if (!_panel) return false;
    delete _panel;
    _panel = nullptr;
    return true;
}

void DisplayManager::setBrightness(uint8_t b) {
    _brightness = b;
    if (_panel) _panel->setBrightness8(b);
}

void DisplayManager::fillScreen(uint16_t color) {
    if (_panel) _panel->fillScreen(color);
}

void DisplayManager::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (_panel) _panel->drawPixel(x, y, color);
}

void DisplayManager::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (_panel) _panel->fillRect(x, y, w, h, color);
}

void DisplayManager::drawText(int16_t x, int16_t y, const char* str,
                               uint16_t color, const GFXfont* font, uint8_t size) {
    if (!_panel) return;
    _panel->setFont(font);
    _panel->setTextSize(size);
    _panel->setTextColor(color);
    _panel->setCursor(x, y);
    _panel->print(str);
}

void DisplayManager::getTextBounds(const char* str, int16_t x, int16_t y,
                                    int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h,
                                    const GFXfont* font) {
    if (!_panel) { *x1=x; *y1=y; *w=0; *h=0; return; }
    _panel->setFont(font);
    _panel->getTextBounds(str, x, y, x1, y1, w, h);
}

void DisplayManager::drawBitmap565(int16_t x, int16_t y, int16_t w, int16_t h,
                                    const uint16_t* bmp, bool swap) {
    if (!_panel) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            uint16_t px = bmp[row * w + col];
            if (swap) px = (px >> 8) | (px << 8);
            if (px != 0x0000) // skip pure black = transparent
                _panel->drawPixel(x + col, y + row, px);
        }
    }
}

void DisplayManager::flip() {
    if (_panel) _panel->flipDMABuffer();
}

void DisplayManager::drawTextShadow(int16_t x, int16_t y, const char* str,
                                     uint16_t color, const GFXfont* font, uint8_t size) {
    drawText(x + 1, y + 1, str, 0x2104, font, size);  // RGB(32,32,32) shadow
    drawText(x,     y,     str, color,  font, size);
}

void DisplayManager::drawTextRelief(int16_t x, int16_t y, const char* str,
                                     uint16_t color, const GFXfont* font, uint8_t size) {
    // Single medium-grey shadow offset bottom-right (no highlight).
    // Keeps a subtle 3D pop without the double-shadow confusion.
    drawText(x + 2, y + 2, str, 0x7BEF, font, size); // medium grey shadow
    drawText(x,     y,     str, color,  font, size); // main text
}
