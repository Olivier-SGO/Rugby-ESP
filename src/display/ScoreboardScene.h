#pragma once
#include "Scene.h"
#include "MatchData.h"
#include <cstdint>

class ScoreboardScene : public Scene {
public:
    // comp: "TOP14", "PRO D2", "CHAMP CUP"
    // headerColor: C_BLUE / C_ORANGE / C_PURPLE
    void setMatch(const MatchData& m, const char* comp, uint16_t headerColor,
                  uint8_t index, uint8_t total);

    void onActivate() override;
    void render() override;
    uint32_t durationMs() const override { return SCENE_SCORE_MS; }

private:
    MatchData _match;
    char      _comp[16];
    uint16_t  _headerColor;
    uint8_t   _index, _total;
    uint16_t* _homeLogo = nullptr;
    uint16_t* _awayLogo = nullptr;

    void drawHeader();
    void drawLogos();
    void drawScores();
    void drawFooter();

    static uint16_t scoreColor(int16_t h, int16_t a, bool isHome, MatchStatus st);
};
