#include "ScoreboardScene.h"
#include "LogoLoader.h"
#include "config.h"
#include "DisplayManager.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include "fonts/AtkinsonHyperlegibleBold12pt7b.h"
#include <Arduino.h>
#include <cstdio>

void ScoreboardScene::setMatch(const MatchData& m, const char* comp,
                                uint16_t headerColor, uint8_t index, uint8_t total) {
    _match = m;
    strlcpy(_comp, comp, sizeof(_comp));
    _headerColor = headerColor;
    _index = index; _total = total;
}

void ScoreboardScene::onActivate() {
    delete[] _homeLogo; delete[] _awayLogo;
    _homeLogo = loadLogo(_match.home_slug);
    _awayLogo = loadLogo(_match.away_slug);
}

void ScoreboardScene::render() {
    Display.fillScreen(C_BLACK);
    drawLogos();
    drawHeader();
    drawScores();
    drawFooter();
    Display.flip();
}

void ScoreboardScene::drawLogos() {
    if (_homeLogo) Display.drawBitmap565(0, 0, LOGO_LG_W, LOGO_LG_H, _homeLogo);
    if (_awayLogo) Display.drawBitmap565(DISPLAY_W - LOGO_LG_W, 0, LOGO_LG_W, LOGO_LG_H, _awayLogo);
}

void ScoreboardScene::drawHeader() {
    char header[32];
    if (_match.round > 0)
        snprintf(header, sizeof(header), "%s · J%d", _comp, _match.round);
    else
        snprintf(header, sizeof(header), "%s", _comp);

    int16_t x1, y1; uint16_t tw, th;
    Display.getTextBounds(header, 0, 0, &x1, &y1, &tw, &th,
                          (const GFXfont*)&AtkinsonHyperlegible8pt7b);
    int16_t tx = CENTER_MID - tw/2;
    Display.drawText(tx, 10, header, _headerColor,
                     (const GFXfont*)&AtkinsonHyperlegible8pt7b);
}

void ScoreboardScene::drawScores() {
    char homeAbbr[8], awayAbbr[8];
    strlcpy(homeAbbr, _match.home_abbrev, sizeof(homeAbbr));
    strlcpy(awayAbbr, _match.away_abbrev, sizeof(awayAbbr));

    char scoreStr[16];
    if (_match.home_score >= 0 && _match.away_score >= 0)
        snprintf(scoreStr, sizeof(scoreStr), "%d - %d",
                 _match.home_score, _match.away_score);
    else
        snprintf(scoreStr, sizeof(scoreStr), "vs");

    const GFXfont* scoreFont = (const GFXfont*)&AtkinsonHyperlegibleBold12pt7b;
    const GFXfont* abbrFont  = (const GFXfont*)&AtkinsonHyperlegible8pt7b;

    int16_t x1, y1; uint16_t sw, sh;
    Display.getTextBounds(scoreStr, 0, 0, &x1, &y1, &sw, &sh, scoreFont);

    uint16_t ha_w, ha_h, aa_w, aa_h;
    Display.getTextBounds(homeAbbr, 0, 0, &x1, &y1, &ha_w, &ha_h, abbrFont);
    Display.getTextBounds(awayAbbr, 0, 0, &x1, &y1, &aa_w, &aa_h, abbrFont);

    uint16_t hColor = scoreColor(_match.home_score, _match.away_score, true,  _match.status);
    uint16_t aColor = scoreColor(_match.home_score, _match.away_score, false, _match.status);
    uint16_t sColor = (_match.status == MatchStatus::Live) ? C_GREEN : C_WHITE;

    bool stacked = (ha_w + 8 + sw + 8 + aa_w) > CENTER_W;

    if (!stacked) {
        // Inline: [homeAbbr]  [score]  [awayAbbr]  all on y≈36
        int16_t scoreX = CENTER_MID - sw/2;
        int16_t homeX  = CENTER_X + 4;
        int16_t awayX  = CENTER_X + CENTER_W - aa_w - 4;
        Display.drawText(homeX,  38, homeAbbr,  hColor, abbrFont);
        Display.drawText(scoreX, 40, scoreStr,  sColor, scoreFont);
        Display.drawText(awayX,  38, awayAbbr,  aColor, abbrFont);
    } else {
        // Stacked: abbreviations centered above their score
        Display.drawText(CENTER_X + CENTER_W/4 - ha_w/2, 28, homeAbbr, hColor, abbrFont);
        Display.drawText(CENTER_X + 3*CENTER_W/4 - aa_w/2, 28, awayAbbr, aColor, abbrFont);
        Display.drawText(CENTER_MID - sw/2, 44, scoreStr, sColor, scoreFont);
    }

    // Status line: "Final" or "45'" or "MT"
    char statusStr[16] = {};
    if (_match.status == MatchStatus::Finished)
        strlcpy(statusStr, "Final", sizeof(statusStr));
    else if (_match.status == MatchStatus::Live) {
        if (_match.minute == -1) strlcpy(statusStr, "MT", sizeof(statusStr));
        else if (_match.minute > 0) snprintf(statusStr, sizeof(statusStr), "%d'", _match.minute);
    }
    if (statusStr[0]) {
        uint16_t stw, sth;
        Display.getTextBounds(statusStr, 0, 0, &x1, &y1, &stw, &sth, abbrFont);
        Display.drawText(CENTER_MID - stw/2, 57, statusStr,
                         _match.status == MatchStatus::Live ? C_GREEN : C_GREY, abbrFont);
    }
}

void ScoreboardScene::drawFooter() {
    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", _index + 1, _total);
    int16_t x1, y1; uint16_t tw, th;
    Display.getTextBounds(counter, 0, 0, &x1, &y1, &tw, &th,
                          (const GFXfont*)&AtkinsonHyperlegible8pt7b);
    Display.drawText(DISPLAY_W - tw - 2, 62, counter, C_GREY,
                     (const GFXfont*)&AtkinsonHyperlegible8pt7b);
}

uint16_t ScoreboardScene::scoreColor(int16_t h, int16_t a, bool isHome, MatchStatus st) {
    if (st == MatchStatus::Live) return C_GREEN;
    if (h < 0 || a < 0) return C_WHITE;
    if (h == a) return C_WHITE;
    bool homeWins = h > a;
    return (isHome == homeWins) ? C_GOLD : C_RED;
}
