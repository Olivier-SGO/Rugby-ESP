#pragma once
#include <Arduino.h>
#include "DisplayManager.h"
#include <WiFi.h>

/**
 * Draw a small WiFi-disconnected icon (crossed-out WiFi) at (x, y).
 * Size: 7x6 pixels. Drawn in red with a dark shadow for visibility.
 * Call when WiFi.status() != WL_CONNECTED.
 */
inline void drawWiFiDisconnectedIcon(int16_t x, int16_t y) {
    // WiFi arcs (grey — faint signal shape)
    Display.drawPixel(x + 2, y + 0, 0x4208);
    Display.drawPixel(x + 3, y + 0, 0x4208);
    Display.drawPixel(x + 4, y + 0, 0x4208);
    Display.drawPixel(x + 1, y + 1, 0x4208);
    Display.drawPixel(x + 5, y + 1, 0x4208);
    Display.drawPixel(x + 3, y + 2, 0x4208);

    // Red diagonal slash (top-left to bottom-right)
    Display.drawPixel(x + 0, y + 0, C_RED);
    Display.drawPixel(x + 1, y + 1, C_RED);
    Display.drawPixel(x + 2, y + 2, C_RED);
    Display.drawPixel(x + 3, y + 3, C_RED);
    Display.drawPixel(x + 4, y + 4, C_RED);
    Display.drawPixel(x + 5, y + 5, C_RED);
    Display.drawPixel(x + 6, y + 5, C_RED);
}

/**
 * Conditionally draw the WiFi disconnected icon next to a competition logo.
 * 
 * @param logoLeftX  X coordinate where the competition logo starts
 * @param logoY      Y coordinate of the competition logo (top)
 */
inline void drawWiFiStatusIfNeeded(int16_t logoLeftX, int16_t logoY) {
    if (WiFi.status() != WL_CONNECTED) {
        // Draw 9px to the left of the logo, vertically centred with logo
        drawWiFiDisconnectedIcon(logoLeftX - 9, logoY + 2);
    }
}
