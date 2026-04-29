#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "config.h"

class DisplayManager {
public:
    bool begin();
    void setBrightness(uint8_t b);
    uint8_t getBrightness() const { return _brightness; }

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

    // Draw text with a 1px dark-gray drop shadow (+1,+1 offset)
    void drawTextShadow(int16_t x, int16_t y, const char* str, uint16_t color,
                        const GFXfont* font = nullptr, uint8_t size = 1);

    // Draw text with a 2px embossed relief (shadow + highlight for 3D pop)
    void drawTextRelief(int16_t x, int16_t y, const char* str, uint16_t color,
                        const GFXfont* font = nullptr, uint8_t size = 1);

    // Swap back→front buffer (call once per frame after all drawing)
    void flip();

    // Release DMA buffers (call before a fetch to free ~134KB for SSL)
    bool end();

    MatrixPanel_I2S_DMA* panel() { return _panel; }

private:
    MatrixPanel_I2S_DMA* _panel = nullptr;
    uint8_t _brightness = 80;
};

extern DisplayManager Display;
