#include "FixturesScene.h"
#include "CompLogos.h"
#include "LogoLoader.h"
#include "DisplayManager.h"
#include "config.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include "fonts/AtkinsonHyperlegible10pt7b.h"
#include <Arduino.h>
#include <time.h>

static const char* DAYS_FR[]   = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
static const char* MONTHS_FR[] = {"","jan","fev","mar","avr","mai","jun",
                                   "jul","aou","sep","oct","nov","dec"};

void FixturesScene::setMatch(const MatchData& m, const char* comp,
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
    _headerColor = headerColor; _index = index; _total = total;
}

void FixturesScene::onActivate() {
    _homeLogo = loadLogo(_md.home_slug);
    _awayLogo = loadLogo(_md.away_slug);
    _compIdx  = compIndex(_comp);
}

void FixturesScene::render() {
    Display.fillScreen(C_BLACK);

    // Team logos
    if (_homeLogo) Display.drawBitmap565(0, 0, LOGO_LG_W, LOGO_LG_H, _homeLogo);
    if (_awayLogo) Display.drawBitmap565(DISPLAY_W - LOGO_LG_W, 0, LOGO_LG_W, LOGO_LG_H, _awayLogo);

    const GFXfont* f8  = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
    const GFXfont* f10 = (const GFXfont*)&AtkinsonHyperlegible10pt7b;
    int16_t x1, y1; uint16_t tw, th;

    // Competition logo header
    int lw = gCompLogoLgW[_compIdx];
    Display.drawBitmap565(CENTER_MID - lw / 2, 0, lw, LOGO_COMP_H, gCompLogoLg[_compIdx]);

    // Round / group indicator, bottom-left corner
    if (_md.group[0]) {
        Display.drawText(2, 63, _md.group, C_GREY, f8);
    } else if (_md.round > 0) {
        char rnd[8];
        snprintf(rnd, sizeof(rnd), "J%d", _md.round);
        Display.drawText(2, 63, rnd, C_GREY, f8);
    }

    // Team abbreviations with shadow, pushed to edges
    Display.drawTextShadow(CENTER_X + 2, 36, _md.home_abbrev, C_WHITE, f8);
    Display.getTextBounds(_md.away_abbrev, 0, 0, &x1, &y1, &tw, &th, f8);
    Display.drawTextShadow(CENTER_X + CENTER_W - tw - 2, 36, _md.away_abbrev, C_WHITE, f8);

    // Date + time centered
    if (_md.kickoff_utc > 0) {
        struct tm* t = localtime(&_md.kickoff_utc);
        char dateLine[24];
        snprintf(dateLine, sizeof(dateLine), "%s %d %s",
                 DAYS_FR[t->tm_wday], t->tm_mday, MONTHS_FR[t->tm_mon + 1]);

        Display.getTextBounds(dateLine, 0, 0, &x1, &y1, &tw, &th, f10);
        Display.drawTextShadow(CENTER_MID - tw / 2, 48, dateLine, C_GOLD, f10);

        if (!(t->tm_hour <= 2 && t->tm_min == 0)) {
            char timeLine[8];
            snprintf(timeLine, sizeof(timeLine), "%02d:%02d", t->tm_hour, t->tm_min);
            Display.getTextBounds(timeLine, 0, 0, &x1, &y1, &tw, &th, f10);
            Display.drawTextShadow(CENTER_MID - tw / 2, 63, timeLine, C_GOLD, f10);
        }
    } else {
        Display.getTextBounds("Horaire TBD", 0, 0, &x1, &y1, &tw, &th, f8);
        Display.drawText(CENTER_MID - tw / 2, 48, "Horaire TBD", C_GREY, f8);
    }

    // Counter bottom-right
    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", _index + 1, _total);
    Display.getTextBounds(counter, 0, 0, &x1, &y1, &tw, &th, f8);
    Display.drawText(DISPLAY_W - tw - 2, 63, counter, C_GREY, f8);

    Display.flip();
}
