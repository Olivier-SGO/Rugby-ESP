#include "StandingsScene.h"
#include "CompLogos.h"
#include "LogoLoader.h"
#include "DisplayManager.h"
#include "config.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include <Arduino.h>
#include <math.h>

void StandingsScene::setData(const StandingEntry* standings, uint8_t count, const char* comp,
                              uint16_t headerColor, uint8_t playoffCutoff,
                              uint8_t relegationStart, uint32_t durationMs) {
    _standing_count = count;
    for (int i = 0; i < count; i++) _standings[i] = standings[i];
    strlcpy(_comp, comp, sizeof(_comp));
    _headerColor = headerColor;
    _playoffCutoff = playoffCutoff;
    _relegationStart = relegationStart;
    _contentH = count * ROW_H;
    _compIdx = compIndex(comp);
    _durationMs = durationMs;
    for (int i = 0; i < CompetitionData::MAX_STANDING; i++) _logoCache[i] = nullptr;
}

void StandingsScene::onActivate() {
    _scrollY = 0.0f;
    _sceneStartMs = millis();
    // Cache mini-logos once per activation to avoid LittleFS I/O every frame
    for (int i = 0; i < _standing_count; i++) {
        _logoCache[i] = loadLogo(_standings[i].slug, true);
    }
}

void StandingsScene::render() {
    Display.fillScreen(C_BLACK);

    const GFXfont* f8 = (const GFXfont*)&AtkinsonHyperlegible8pt7b;

    // Competition logo (large) on the right, ~40px from edge — drawn after rows as overlay
    // (see end of render())

    // Scrolling rows
    for (int i = 0; i < _standing_count; i++) {
        int16_t y = (int16_t)(i * ROW_H - _scrollY);
        if (y + ROW_H < 0) continue;
        if (y > DISPLAY_H) break;

        const StandingEntry& e = _standings[i];
        uint16_t col = rankColor(e.rank);

        // Mini logo — from cache loaded in onActivate()
        if (_logoCache[i])
            Display.drawBitmap565(2, y + (ROW_H - LOGO_SM_H)/2, LOGO_SM_W, LOGO_SM_H, _logoCache[i]);

        // Rank + name
        char rankStr[4];
        snprintf(rankStr, sizeof(rankStr), "%d.", e.rank);
        Display.drawText(LOGO_SM_W + 4, y + ROW_H - 4, rankStr, col, f8);

        int16_t x1, y1; uint16_t tw, th;
        Display.getTextBounds(rankStr, 0, 0, &x1, &y1, &tw, &th, f8);
        Display.drawText(LOGO_SM_W + 4 + tw + 2, y + ROW_H - 4, e.name, col, f8);

        // Points right-aligned
        char pts[8];
        snprintf(pts, sizeof(pts), "%d", e.points);
        Display.getTextBounds(pts, 0, 0, &x1, &y1, &tw, &th, f8);
        Display.drawText(DISPLAY_W - tw - 4, y + ROW_H - 4, pts, col, f8);
    }

    // Draw competition logo overlay last so standings scroll behind it
    {
        int lw = gCompLogoLgW[_compIdx];
        Display.drawBitmap565(DISPLAY_W - lw - 40, 0, lw, LOGO_COMP_H, gCompLogoLg[_compIdx]);
    }

    Display.flip();

    // Time-based scroll: 1s pause at start, 0.5s pause at end
    uint32_t elapsed = millis() - _sceneStartMs;
    int16_t maxScroll = _contentH - DISPLAY_H;
    if (maxScroll < 0) maxScroll = 0;

    if (elapsed < 1000) {
        _scrollY = 0.0f;
    } else if (elapsed >= _durationMs - 500) {
        _scrollY = (float)maxScroll;
    } else {
        uint32_t scrollTime = _durationMs - 1500; // total scroll duration in ms
        uint32_t scrollElapsed = elapsed - 1000;
        _scrollY = (float)maxScroll * (float)scrollElapsed / (float)scrollTime;
    }

    if (elapsed >= _durationMs) {
        _sceneStartMs = millis(); // loop
    }
}

uint16_t StandingsScene::rankColor(uint8_t rank) const {
    if (rank <= _playoffCutoff)  return C_GOLD;
    if (rank >= _relegationStart) return C_RED;
    return C_WHITE;
}
