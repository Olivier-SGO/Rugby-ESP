#pragma once
#include "Scene.h"
#include "MatchData.h"

class StandingsScene : public Scene {
public:
    void setData(const StandingEntry* standings, uint8_t count, const char* comp,
                 uint16_t headerColor, uint8_t playoffCutoff, uint8_t relegationStart);
    void onActivate() override;
    void render() override;
    uint32_t durationMs() const override { return SCENE_STANDING_MS; }

private:
    StandingEntry _standings[CompetitionData::MAX_STANDING];
    uint8_t   _standing_count = 0;
    char      _comp[16];
    uint16_t  _headerColor;
    uint8_t   _playoffCutoff;    // ranks <= this get gold
    uint8_t   _relegationStart;  // ranks >= this get red
    int       _compIdx = 0;

    float _scrollY    = 0.0f;
    int16_t _contentH = 0;       // total scroll content height in px

    static const int16_t HEADER_H = 26;
    static const int16_t ROW_H    = 16;
    static const float   SCROLL_SPEED;  // px per frame

    // Sliding cache for mini logos: avoid opening LittleFS at 30fps
    static const int LOGO_CACHE = 4;
    uint16_t* _logoCache[LOGO_CACHE] = {};
    int8_t    _cacheStandingIdx[LOGO_CACHE] = {-1, -1, -1, -1};

    uint16_t rankColor(uint8_t rank) const;
};
