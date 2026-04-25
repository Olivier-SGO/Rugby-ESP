#include "SceneManager.h"
#include "CompLogos.h"
#include "DisplayPrefs.h"
#include "config.h"
#include <Arduino.h>

SceneManager Scenes;

void SceneManager::begin(MatchDB* db) {
    _db = db;
    loadCompLogos();
    rebuildSlots();
    activateCurrent();
    _needsRebuild = false;
}

void SceneManager::tick() {
    if (_needsRebuild) {
        _needsRebuild = false;
        rebuildSlots();
    }
    if (_slotCount == 0) { rebuildSlots(); return; }

    if (_db->hasLive()) {
        _livePriority = true;
        _lastLiveDetect = millis();
    } else if (_livePriority && millis() - _lastLiveDetect > LIVE_GRACE_MS) {
        _livePriority = false;
    }

    uint32_t dur = _slots[_current].durationMs;
    if (!_livePriority && millis() - _sceneStart > dur) {
        nextScene();
    }

    static uint32_t lastRebuild = 0;
    if (millis() - lastRebuild > 300000) {  // 5 min — prefs rarely change
        rebuildSlots();
        lastRebuild = millis();
    }

    Scene* s = _slots[_current].scene;

    // Standings scenes are animated (scroll) — always render.
    // All other scenes are static — render only when dirty.
    bool animated = false;
    for (int i = 0; i < MAX_STAND_SCENES; i++) {
        if (s == &_standScenes[i]) { animated = true; break; }
    }

    if (_dirty || animated) {
        s->render();
        if (!animated) _dirty = false;
    }
}

void SceneManager::markDirty() { _dirty = true; _needsRebuild = true; }

void SceneManager::freeAllLogos() {
    // No-op: all logos are pre-loaded into PSRAM at boot.
    // Previously this freed per-scene SRAM buffers to make room for TLS heap.
}

void SceneManager::nextScene() {
    _current = (_current + 1) % _slotCount;
    activateCurrent();
    _sceneStart = millis();
    _dirty = true;
}

void SceneManager::setLivePriority(bool live) {
    _livePriority = live;
    if (live) _lastLiveDetect = millis();
}

void SceneManager::activateCurrent() {
    if (_slotCount > 0) _slots[_current].scene->onActivate();
    _dirty = true;
}

void SceneManager::rebuildSlots() {
    _slotCount = 0;
    int scoreIdx = 0, fixIdx = 0, standIdx = 0;

    DisplayPrefs prefs;
    loadDisplayPrefs(prefs);

    struct CompInfo { const char* name; uint16_t color; uint8_t playoff; uint8_t relStart; };
    CompInfo comps[] = {
        {"TOP14",     C_BLUE,   6, 13},
        {"PRO D2",    C_ORANGE, 6, 15},
        {"CHAMP CUP", C_PURPLE, 8, 99},
    };

    auto addSlot = [&](Scene* s, uint32_t ms) {
        if (_slotCount < MAX_SLOTS) _slots[_slotCount++] = {s, ms};
    };

    uint32_t scoreMs   = (uint32_t)prefs.score_s    * 1000;
    uint32_t fixtureMs = (uint32_t)prefs.fixture_s  * 1000;
    uint32_t standMs   = (uint32_t)prefs.standing_s * 1000;

    auto addComp = [&](const CompetitionData* d, int ci) {
        if (!d) return;
        const CompPrefs& p = prefs.comp[ci];
        if (!p.enabled) return;

        if (p.scores) {
            // For Champions Cup, sort results by group so same pool plays consecutively
            if (ci == 2 && d->result_count > 1) {
                MatchData sorted[CompetitionData::MAX_MATCHES];
                memcpy(sorted, d->results, sizeof(MatchData) * d->result_count);
                for (int i = 0; i < d->result_count - 1; i++) {
                    for (int j = 0; j < d->result_count - i - 1; j++) {
                        if (strcmp(sorted[j].group, sorted[j+1].group) > 0) {
                            MatchData tmp = sorted[j];
                            sorted[j] = sorted[j+1];
                            sorted[j+1] = tmp;
                        }
                    }
                }
                for (int i = 0; i < d->result_count && scoreIdx < MAX_SCORE_SCENES; i++, scoreIdx++) {
                    _scoreScenes[scoreIdx].setMatch(sorted[i], comps[ci].name, comps[ci].color,
                                                    i, d->result_count);
                    addSlot(&_scoreScenes[scoreIdx], scoreMs);
                }
            } else {
                for (int i = 0; i < d->result_count && scoreIdx < MAX_SCORE_SCENES; i++, scoreIdx++) {
                    _scoreScenes[scoreIdx].setMatch(d->results[i], comps[ci].name, comps[ci].color,
                                                    i, d->result_count);
                    addSlot(&_scoreScenes[scoreIdx], scoreMs);
                }
            }
        }

        if (p.fixtures) {
            // Sort CC fixtures by group too
            if (ci == 2 && d->fixture_count > 1) {
                MatchData sorted[CompetitionData::MAX_MATCHES];
                memcpy(sorted, d->fixtures, sizeof(MatchData) * d->fixture_count);
                for (int i = 0; i < d->fixture_count - 1; i++) {
                    for (int j = 0; j < d->fixture_count - i - 1; j++) {
                        if (strcmp(sorted[j].group, sorted[j+1].group) > 0) {
                            MatchData tmp = sorted[j];
                            sorted[j] = sorted[j+1];
                            sorted[j+1] = tmp;
                        }
                    }
                }
                for (int i = 0; i < d->fixture_count && fixIdx < MAX_FIX_SCENES; i++, fixIdx++) {
                    _fixScenes[fixIdx].setMatch(sorted[i], comps[ci].name, comps[ci].color,
                                                i, d->fixture_count);
                    addSlot(&_fixScenes[fixIdx], fixtureMs);
                }
            } else {
                for (int i = 0; i < d->fixture_count && fixIdx < MAX_FIX_SCENES; i++, fixIdx++) {
                    _fixScenes[fixIdx].setMatch(d->fixtures[i], comps[ci].name, comps[ci].color,
                                                i, d->fixture_count);
                    addSlot(&_fixScenes[fixIdx], fixtureMs);
                }
            }
        }

        if (p.standings && d->standing_count > 0 && standIdx < MAX_STAND_SCENES) {
            _standScenes[standIdx].setData(d->standings, d->standing_count, comps[ci].name,
                                            comps[ci].color, comps[ci].playoff, comps[ci].relStart,
                                            standMs);
            addSlot(&_standScenes[standIdx], standMs);
            standIdx++;
        }
    };

    const CompetitionData* t14 = _db->acquireTop14();
    if (t14) { addComp(t14, 0); _db->release(); }
    const CompetitionData* pd2 = _db->acquireProd2();
    if (pd2) { addComp(pd2, 1); _db->release(); }
    const CompetitionData* cc  = _db->acquireCC();
    if (cc)  { addComp(cc,  2); _db->release(); }

    if (_slotCount == 0) return;
    if (_current >= _slotCount) _current = 0;
    _dirty = true;
}
