#include "ScoreboardScene.h"
#include "CompLogos.h"
#include "LogoLoader.h"
#include "WiFiIcon.h"
#include "config.h"
#include "DisplayManager.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include "fonts/AtkinsonHyperlegibleBold12pt7b.h"
#include <Arduino.h>
#include <cstdio>

void ScoreboardScene::setMatch(const MatchData& m, const char* comp,
                                uint16_t headerColor, uint8_t index, uint8_t total) {
    strlcpy(_md.home_abbrev, m.home_abbrev, sizeof(_md.home_abbrev));
    strlcpy(_md.away_abbrev, m.away_abbrev, sizeof(_md.away_abbrev));
    strlcpy(_md.home_slug,   m.home_slug,   sizeof(_md.home_slug));
    strlcpy(_md.away_slug,   m.away_slug,   sizeof(_md.away_slug));
    _md.home_score  = m.home_score;
    _md.away_score  = m.away_score;
    _md.status      = m.status;
    _md.minute      = m.minute;
    _md.kickoff_utc = m.kickoff_utc;
    _md.round       = m.round;
    strlcpy(_md.group, m.group, sizeof(_md.group));
    strlcpy(_comp, comp, sizeof(_comp));
    _headerColor = headerColor;
    _index = index; _total = total;
}

void ScoreboardScene::onActivate() {
    _homeLogo = loadLogo(_md.home_slug);
    _awayLogo = loadLogo(_md.away_slug);
    _compIdx  = compIndex(_comp);
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

    // Round / group indicator, bottom-left corner
    const GFXfont* af = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
    if (_md.group[0]) {
        Display.drawText(2, 63, _md.group, C_GREY, af);
    } else if (_md.round > 0) {
        char rnd[8];
        snprintf(rnd, sizeof(rnd), "J%d", _md.round);
        Display.drawText(2, 63, rnd, C_GREY, af);
    }
}

void ScoreboardScene::drawHeader() {
    int lw = gCompLogoLgW[_compIdx];
    int logoX = CENTER_MID - lw / 2;
    Display.drawBitmap565(logoX, 0, lw, LOGO_COMP_H, gCompLogoLg[_compIdx]);
    drawWiFiStatusIfNeeded(logoX, 0);
}

void ScoreboardScene::drawScores() {
    const GFXfont* sf = (const GFXfont*)&AtkinsonHyperlegibleBold12pt7b;
    const GFXfont* af = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
    int16_t x1, y1; uint16_t tw, th;

    // Centers of the spaces beside the competition logo
    int lw = gCompLogoLgW[_compIdx];
    int lc = (CENTER_X + CENTER_MID - lw / 2) / 2;           // center of left space
    int rc = (CENTER_MID + lw / 2 + CENTER_X + CENTER_W) / 2; // center of right space

    uint16_t hColor = scoreColor(_md.home_score, _md.away_score, true,  _md.status);
    uint16_t aColor = scoreColor(_md.home_score, _md.away_score, false, _md.status);

    // Scores at upper third (y=21), beside comp logo
    if (_md.home_score < 0) {
        Display.getTextBounds("vs", 0, 0, &x1, &y1, &tw, &th, sf);
        Display.drawTextRelief(CENTER_MID - tw / 2, 21, "vs", C_WHITE, sf);
    } else {
        char hs[8], as_[8];
        snprintf(hs,  sizeof(hs),  "%d", _md.home_score);
        snprintf(as_, sizeof(as_), "%d", _md.away_score);
        uint16_t hw, aw;
        Display.getTextBounds(hs,  0, 0, &x1, &y1, &hw, &th, sf);
        Display.getTextBounds(as_, 0, 0, &x1, &y1, &aw, &th, sf);
        Display.drawTextRelief(lc - hw / 2, 21, hs, hColor, sf);
        Display.drawTextRelief(rc - aw / 2, 21, as_, aColor, sf);
    }

    // Club names (regular 8pt) at 3/4 height (y=48)
    Display.getTextBounds(_md.home_abbrev, 0, 0, &x1, &y1, &tw, &th, af);
    Display.drawTextShadow(lc - tw / 2, 48, _md.home_abbrev, C_WHITE, af);
    Display.getTextBounds(_md.away_abbrev, 0, 0, &x1, &y1, &tw, &th, af);
    Display.drawTextShadow(rc - tw / 2, 48, _md.away_abbrev, C_WHITE, af);

    // Status at very bottom center:
    //   - Finished → "Final" in grey
    //   - Live     → "MT" or "45'" in green
    //   - Scheduled → nothing
    char st[16] = {};
    uint16_t stColor = C_GREY;
    if (_md.status == MatchStatus::Finished) {
        strlcpy(st, "Final", sizeof(st));
    } else if (_md.status == MatchStatus::Live) {
        stColor = C_GREEN;
        if (_md.minute == -1)    strlcpy(st, "MT", sizeof(st));
        else if (_md.minute > 0) snprintf(st, sizeof(st), "%d'", _md.minute);
        else                      strlcpy(st, "Live", sizeof(st)); // minute=0 fallback
    }
    if (st[0]) {
        Display.getTextBounds(st, 0, 0, &x1, &y1, &tw, &th, af);
        Display.drawText(CENTER_MID - tw / 2, 63, st, stColor, af);
    }
}

void ScoreboardScene::drawFooter() {
    const GFXfont* af = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
    int16_t x1, y1; uint16_t tw, th;

    // Counter: bottom-right corner, overlaid on away logo
    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", _index + 1, _total);
    Display.getTextBounds(counter, 0, 0, &x1, &y1, &tw, &th, af);
    Display.drawText(DISPLAY_W - tw - 2, 63, counter, C_GREY, af);
}

uint16_t ScoreboardScene::scoreColor(int16_t h, int16_t a, bool isHome, MatchStatus st) {
    if (st == MatchStatus::Live) return C_GREEN;
    if (h < 0 || a < 0) return C_WHITE;
    if (h == a) return C_WHITE;
    bool homeWins = h > a;
    return (isHome == homeWins) ? C_GOLD : C_WHITE;
}
