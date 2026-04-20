#include "StandingsScene.h"
#include "CompLogos.h"
#include "LogoLoader.h"
#include "DisplayManager.h"
#include "config.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include <Arduino.h>
#include <math.h>

const float StandingsScene::SCROLL_SPEED = 0.5f; // px per frame → ~15px/sec at 30fps

void StandingsScene::setData(const StandingEntry* standings, uint8_t count, const char* comp,
                              uint16_t headerColor, uint8_t playoffCutoff,
                              uint8_t relegationStart) {
    _standing_count = count;
    for (int i = 0; i < count; i++) _standings[i] = standings[i];
    strlcpy(_comp, comp, sizeof(_comp));
    _headerColor = headerColor;
    _playoffCutoff = playoffCutoff;
    _relegationStart = relegationStart;
    _contentH = count * ROW_H;
    _compIdx = compIndex(comp);
}

void StandingsScene::onActivate() {
    _scrollY = 0.0f;
    for (int c = 0; c < LOGO_CACHE; c++) {
        delete[] _logoCache[c];
        _logoCache[c] = nullptr;
        _cacheStandingIdx[c] = -1;
    }
}

void StandingsScene::render() {
    Display.fillScreen(C_BLACK);

    const GFXfont* f8 = (const GFXfont*)&AtkinsonHyperlegible8pt7b;

    // Fixed header: competition logo + "CLASSEMENT"
    int sw = gCompLogoSmW[_compIdx];
    Display.drawBitmap565(2, 2, sw, LOGO_COMP_SM_H, gCompLogoSm[_compIdx]);
    Display.drawText(sw + 6, HEADER_H - 2, "CLASSEMENT", _headerColor, f8);
    Display.fillRect(0, HEADER_H, DISPLAY_W, 1, C_GREY);

    // Scrolling rows
    for (int i = 0; i < _standing_count; i++) {
        int16_t y = HEADER_H + 1 + (int16_t)(i * ROW_H - _scrollY);
        if (y + ROW_H < HEADER_H) continue;
        if (y > DISPLAY_H) break;

        const StandingEntry& e = _standings[i];
        uint16_t col = rankColor(e.rank);

        // Mini logo — cached to avoid LittleFS thrashing at 30fps
        int slot = i % LOGO_CACHE;
        if (_cacheStandingIdx[slot] != i) {
            delete[] _logoCache[slot];
            _logoCache[slot] = loadLogo(e.slug, true);
            _cacheStandingIdx[slot] = i;
        }
        if (_logoCache[slot])
            Display.drawBitmap565(2, y + (ROW_H - LOGO_SM_H)/2, LOGO_SM_W, LOGO_SM_H, _logoCache[slot]);

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

    Display.flip();

    // Advance scroll
    _scrollY += SCROLL_SPEED;
    int16_t maxScroll = _contentH - (DISPLAY_H - HEADER_H);
    if (maxScroll < 0) maxScroll = 0;
    if (_scrollY > maxScroll + ROW_H) _scrollY = 0.0f; // loop back
}

uint16_t StandingsScene::rankColor(uint8_t rank) const {
    if (rank <= _playoffCutoff)  return C_GOLD;
    if (rank >= _relegationStart) return C_RED;
    return C_WHITE;
}
