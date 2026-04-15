#pragma once
#include "Scene.h"
#include "MatchData.h"

class StandingsScene : public Scene {
public:
    void setData(const CompetitionData& data, const char* comp,
                 uint16_t headerColor, uint8_t playoffCutoff, uint8_t relegationStart);
    void onActivate() override;
    void render() override;
    uint32_t durationMs() const override { return SCENE_STANDING_MS; }

private:
    CompetitionData _data;
    char      _comp[16];
    uint16_t  _headerColor;
    uint8_t   _playoffCutoff;    // ranks <= this get gold
    uint8_t   _relegationStart;  // ranks >= this get red

    float _scrollY    = 0.0f;
    int16_t _contentH = 0;       // total scroll content height in px

    static const int16_t HEADER_H = 16;
    static const int16_t ROW_H    = 16;
    static const float   SCROLL_SPEED;  // px per frame

    uint16_t* _logos[CompetitionData::MAX_STANDING] = {};

    uint16_t rankColor(uint8_t rank) const;
};
