#pragma once
#include "MatchDB.h"
#include "ScoreboardScene.h"
#include "FixturesScene.h"
#include "StandingsScene.h"
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
    void prevScene();
    void setLivePriority(bool live);

    // Signal that display content needs one re-render (call after data update)
    void markDirty();

    // Free all loaded logo bitmaps to reclaim heap before network fetch
    void freeAllLogos();

    // Request a rebuild on next tick (thread-safe — call from any core)
    void requestRebuild() { _needsRebuild = true; }

private:
    MatchDB* _db = nullptr;
    bool     _livePriority = false;
    uint32_t _lastLiveDetect = 0;
    volatile bool _dirty = true;
    volatile bool _needsRebuild = true; // rebuild on first tick

    static constexpr size_t MAX_SLOTS = 60;
    SceneSlot _slots[MAX_SLOTS];
    size_t    _slotCount = 0;
    size_t    _current = 0;
    uint32_t  _sceneStart = 0;

    void activateCurrent();
    void rebuildSlots();

    // Reusable scene objects (avoid heap churn)
    static const int MAX_SCORE_SCENES = 24;
    static const int MAX_FIX_SCENES   = 24;
    static const int MAX_STAND_SCENES = 3;
    ScoreboardScene _scoreScenes[MAX_SCORE_SCENES];
    FixturesScene   _fixScenes[MAX_FIX_SCENES];
    StandingsScene  _standScenes[MAX_STAND_SCENES];
};

extern SceneManager Scenes;
