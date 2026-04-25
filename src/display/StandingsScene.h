#pragma once
#include "Scene.h"
#include "MatchData.h"

class StandingsScene : public Scene {
public:
    void setData(const StandingEntry* standings, uint8_t count, const char* comp,
                 uint16_t headerColor, uint8_t playoffCutoff, uint8_t relegationStart,
                 uint32_t durationMs = SCENE_STANDING_MS);
    void onActivate() override;
    void render() override;
    uint32_t durationMs() const override { return _durationMs; }

private:
    StandingEntry _standings[CompetitionData::MAX_STANDING];
    uint8_t   _standing_count = 0;
    char      _comp[16];
    uint16_t  _headerColor;
    uint8_t   _playoffCutoff;    // ranks <= this get gold
    uint8_t   _relegationStart;  // ranks >= this get red
    int       _compIdx = 0;

    float _scrollY      = 0.0f;
    int16_t _contentH   = 0;       // total scroll content height in px
    uint32_t _sceneStartMs = 0;    // for time-based scroll
    uint32_t _durationMs = SCENE_STANDING_MS;

    static const int16_t ROW_H = 16;

    uint16_t rankColor(uint8_t rank) const;
};
