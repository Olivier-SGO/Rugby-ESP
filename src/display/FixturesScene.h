#pragma once
#include "Scene.h"
#include "MatchData.h"

class FixturesScene : public Scene {
public:
    void setMatch(const MatchData& m, const char* comp, uint16_t headerColor,
                  uint8_t index, uint8_t total);
    void onActivate() override;
    void render() override;
    void freeLogos() { delete[] _homeLogo; _homeLogo = nullptr; delete[] _awayLogo; _awayLogo = nullptr; }
    uint32_t durationMs() const override { return SCENE_FIXTURE_MS; }

private:
    MatchDisplay _md;
    char         _comp[16];
    uint16_t  _headerColor;
    uint8_t   _index, _total;
    int       _compIdx = 0;
    uint16_t* _homeLogo = nullptr;
    uint16_t* _awayLogo = nullptr;
};
