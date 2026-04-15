#pragma once
#include "MatchDB.h"
#include "ScoreboardScene.h"
#include "FixturesScene.h"
#include "StandingsScene.h"
#include <vector>

struct SceneSlot {
    Scene*   scene;
    uint32_t durationMs;
};

class SceneManager {
public:
    void begin(MatchDB* db);

    // Call from Core 1 render task at ~30fps
    void tick();

    void nextScene();
    void setLivePriority(bool live);

private:
    MatchDB* _db = nullptr;
    bool     _livePriority = false;
    uint32_t _lastLiveDetect = 0;

    std::vector<SceneSlot> _slots;
    size_t   _current = 0;
    uint32_t _sceneStart = 0;

    void rebuildSlots();
    void activateCurrent();

    // Reusable scene objects (avoid heap churn)
    static const int MAX_SCORE_SCENES = 12;
    static const int MAX_FIX_SCENES   = 12;
    static const int MAX_STAND_SCENES = 3;
    ScoreboardScene _scoreScenes[MAX_SCORE_SCENES];
    FixturesScene   _fixScenes[MAX_FIX_SCENES];
    StandingsScene  _standScenes[MAX_STAND_SCENES];
};

extern SceneManager Scenes;
