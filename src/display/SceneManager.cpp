#include "SceneManager.h"
#include "config.h"
#include <Arduino.h>

SceneManager Scenes;

void SceneManager::begin(MatchDB* db) {
    _db = db;
    rebuildSlots();
    activateCurrent();
}

void SceneManager::tick() {
    if (_slots.empty()) { rebuildSlots(); return; }

    // Check for live matches
    if (_db->hasLive()) {
        _livePriority = true;
        _lastLiveDetect = millis();
    } else if (_livePriority && millis() - _lastLiveDetect > LIVE_GRACE_MS) {
        _livePriority = false;
    }

    // Advance scene timer
    uint32_t dur = _slots[_current].durationMs;
    if (!_livePriority && millis() - _sceneStart > dur) {
        nextScene();
    }

    // Rebuild slots every minute (data may have been updated)
    static uint32_t lastRebuild = 0;
    if (millis() - lastRebuild > 60000) {
        rebuildSlots();
        lastRebuild = millis();
    }

    _slots[_current].scene->render();
}

void SceneManager::nextScene() {
    _current = (_current + 1) % _slots.size();
    activateCurrent();
    _sceneStart = millis();
}

void SceneManager::setLivePriority(bool live) {
    _livePriority = live;
    if (live) _lastLiveDetect = millis();
}

void SceneManager::activateCurrent() {
    if (!_slots.empty()) _slots[_current].scene->onActivate();
}

void SceneManager::rebuildSlots() {
    _slots.clear();
    int scoreIdx = 0, fixIdx = 0, standIdx = 0;

    struct CompInfo { const char* name; uint16_t color; uint8_t playoff; uint8_t relStart; };
    CompInfo comps[] = {
        {"TOP14",     C_BLUE,   6, 13},
        {"PRO D2",    C_ORANGE, 6, 15},
        {"CHAMP CUP", C_PURPLE, 8, 99},
    };

    auto addComp = [&](const CompetitionData* d, int ci) {
        if (!d) return;

        // Results / live
        for (int i = 0; i < d->result_count && scoreIdx < MAX_SCORE_SCENES; i++, scoreIdx++) {
            _scoreScenes[scoreIdx].setMatch(d->results[i], comps[ci].name, comps[ci].color,
                                            i, d->result_count);
            _slots.push_back({&_scoreScenes[scoreIdx], SCENE_SCORE_MS});
        }

        // Fixtures
        for (int i = 0; i < d->fixture_count && fixIdx < MAX_FIX_SCENES; i++, fixIdx++) {
            _fixScenes[fixIdx].setMatch(d->fixtures[i], comps[ci].name, comps[ci].color,
                                        i, d->fixture_count);
            _slots.push_back({&_fixScenes[fixIdx], SCENE_FIXTURE_MS});
        }

        // Standings
        if (d->standing_count > 0 && standIdx < MAX_STAND_SCENES) {
            _standScenes[standIdx].setData(*d, comps[ci].name, comps[ci].color,
                                            comps[ci].playoff, comps[ci].relStart);
            _slots.push_back({&_standScenes[standIdx], SCENE_STANDING_MS});
            standIdx++;
        }
    };

    const CompetitionData* t14 = _db->acquireTop14(); addComp(t14, 0); _db->release();
    const CompetitionData* pd2 = _db->acquireProd2(); addComp(pd2, 1); _db->release();
    const CompetitionData* cc  = _db->acquireCC();    addComp(cc,  2); _db->release();

    if (_slots.empty()) return;
    if (_current >= _slots.size()) _current = 0;
}
