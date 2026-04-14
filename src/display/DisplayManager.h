#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "config.h"

class DisplayManager {
public:
    bool begin();
    void setBrightness(uint8_t b);

    // Draw primitives — thin wrappers around the DMA panel
    void fillScreen(uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawText(int16_t x, int16_t y, const char* str, uint16_t color,
                  const GFXfont* font = nullptr, uint8_t size = 1);
    void getTextBounds(const char* str, int16_t x, int16_t y,
                       int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h,
                       const GFXfont* font = nullptr);

    // Draw a preloaded RGB565 bitmap (from LittleFS)
    void drawBitmap565(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t* bmp);

    // Swap back→front buffer (call once per frame after all drawing)
    void flip();

    MatrixPanel_I2S_DMA* panel() { return _panel; }

private:
    MatrixPanel_I2S_DMA* _panel = nullptr;
};

extern DisplayManager Display;
