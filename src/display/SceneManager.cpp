#include "SceneManager.h"
#include "CompLogos.h"
#include "DisplayPrefs.h"
#include "DisplayManager.h"
#include "WiFiIcon.h"
#include "config.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include <Arduino.h>
#include <WiFi.h>

SceneManager Scenes;

// Chronological order for knockout phases (CC pools + domestic barrages/quarters + CC + T14/PD2 finals).
// Empty group (regular season) = 0 so they sort before knockouts or keep relative order via secondary.
static int phaseOrder(const char* group) {
    if (!group || !group[0]) return 0;
    if (strncmp(group, "Gr.", 3) == 0) return 0;
    if (strcmp(group, "1/8F") == 0) return 1;
    // barrages treated as 1/4 equivalent for this season (press often calls the barrage "quart")
    if (strcmp(group, "1/4F") == 0 ||
        strcmp(group, "Barr.") == 0 ||
        strncmp(group, "Barr", 4) == 0) return 2;
    if (strcmp(group, "1/2F") == 0) return 3;
    if (strcmp(group, "Finale") == 0) return 4;
    return 0;
}

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
    if (_slotCount == 0) {
        static uint32_t lastEmptyRebuild = 0;
        static uint32_t lastEmptyRender = 0;
        if (millis() - lastEmptyRebuild > 5000) {
            lastEmptyRebuild = millis();
            rebuildSlots();
        }
        if (millis() - lastEmptyRender > 1000) {
            lastEmptyRender = millis();
            Display.fillScreen(C_BLACK);
            const GFXfont* f8 = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
            const char* msg1 = "EN ATTENTE";
            const char* msg2 = "DE DONNEES";
            int16_t x1, y1; uint16_t tw, th;
            Display.getTextBounds(msg1, 0, 0, &x1, &y1, &tw, &th, f8);
            Display.drawTextRelief(CENTER_MID - tw/2, 22, msg1, C_GOLD, f8);
            Display.getTextBounds(msg2, 0, 0, &x1, &y1, &tw, &th, f8);
            Display.drawTextRelief(CENTER_MID - tw/2, 40, msg2, C_GOLD, f8);
            if (WiFi.status() != WL_CONNECTED) {
                Display.drawBitmap565(CENTER_MID - 8, 50, 16, 16, WIFI_DISCONNECTED_ICON);
            }
            Display.flip();
        }
        return;
    }

    if (_db->hasLive()) {
        _livePriority = true;
        _lastLiveDetect = millis();
    } else if (_livePriority && millis() - _lastLiveDetect > LIVE_GRACE_MS) {
        _livePriority = false;
    }

    // Live priority: jump to the first live match scene if we're not already on one
    if (_livePriority && !_slots[_current].scene->isLiveMatch()) {
        for (int i = 0; i < _slotCount; i++) {
            if (_slots[i].scene->isLiveMatch()) {
                _current = i;
                activateCurrent();
                _sceneStart = millis();
                _dirty = true;
                break;
            }
        }
    }

    uint32_t dur = _slots[_current].durationMs;
    if (millis() - _sceneStart > dur) {
        if (!_livePriority) {
            nextScene();
        } else {
            // Cycle through live scenes only — don't go back to non-live
            int next = (_current + 1) % _slotCount;
            int startAt = next;
            do {
                if (_slots[next].scene->isLiveMatch()) break;
                next = (next + 1) % _slotCount;
            } while (next != startAt);
            if (next != _current) {
                _current = next;
                activateCurrent();
                _sceneStart = millis();
                _dirty = true;
            }
        }
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

    // Rate-limit animated scenes to ~10fps to reduce tearing without double buffering
    static uint32_t lastAnimatedRender = 0;
    if (animated && millis() - lastAnimatedRender < 100) return;
    if (animated) lastAnimatedRender = millis();

    // Force re-render live matches every 20 seconds even if data hasn't changed
    bool isLive = s->isLiveMatch();
    static uint32_t lastLiveRender = 0;
    if (isLive && millis() - lastLiveRender > 20000) {
        _dirty = true;
        lastLiveRender = millis();
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

void SceneManager::prevScene() {
    if (_slotCount == 0) return;
    _current = (_current + _slotCount - 1) % _slotCount;
    activateCurrent();
    _sceneStart = millis();
    _dirty = true;
}

void SceneManager::setLivePriority(bool live) {
    _livePriority = live;
    if (live) _lastLiveDetect = millis();
}

void SceneManager::nextComp() {
    if (_slotCount == 0) return;
    uint8_t cur = _slots[_current].compIdx;
    for (size_t i = 1; i <= _slotCount; i++) {
        size_t idx = (_current + i) % _slotCount;
        if (_slots[idx].compIdx != cur) {
            _current = idx;
            activateCurrent();
            _sceneStart = millis();
            _dirty = true;
            return;
        }
    }
}

void SceneManager::prevComp() {
    if (_slotCount == 0) return;
    uint8_t cur = _slots[_current].compIdx;
    // Step back to find the previous comp's first slot
    // First find any slot of a different comp going backwards
    size_t prevCompAny = _slotCount; // sentinel
    for (size_t i = 1; i <= _slotCount; i++) {
        size_t idx = (_current + _slotCount - i) % _slotCount;
        if (_slots[idx].compIdx != cur) { prevCompAny = idx; break; }
    }
    if (prevCompAny == _slotCount) return;
    // Now rewind to the first slot of that comp
    uint8_t targetComp = _slots[prevCompAny].compIdx;
    size_t first = prevCompAny;
    for (size_t i = 1; i < _slotCount; i++) {
        size_t idx = (prevCompAny + _slotCount - i) % _slotCount;
        if (_slots[idx].compIdx != targetComp) break;
        first = idx;
    }
    _current = first;
    activateCurrent();
    _sceneStart = millis();
    _dirty = true;
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

    static size_t lastSlotCount = SIZE_MAX;
    bool changed = false;

    struct CompInfo { const char* name; uint16_t color; uint8_t playoff; uint8_t relStart; };
    CompInfo comps[] = {
        {"TOP14",     C_BLUE,   6, 13},
        {"PRO D2",    C_ORANGE, 6, 15},
        {"CHAMP CUP", C_PURPLE, 8, 99},
    };

    int currentComp = 0;
    auto addSlot = [&](Scene* s, uint32_t ms) {
        if (_slotCount < MAX_SLOTS) _slots[_slotCount++] = {s, ms, (uint8_t)currentComp};
    };

    uint32_t scoreMs   = (uint32_t)prefs.score_s    * 1000;
    uint32_t fixtureMs = (uint32_t)prefs.fixture_s  * 1000;
    uint32_t standMs   = (uint32_t)prefs.standing_s * 1000;

    // Static buffers in PSRAM to avoid SRAM consumption in renderTask
    static MatchData* liveBuf = nullptr;
    static MatchData* nonliveBuf = nullptr;
    static MatchData* sortedBuf = nullptr;
    if (!liveBuf)   liveBuf   = (MatchData*)heap_caps_malloc(sizeof(MatchData) * CompetitionData::MAX_MATCHES, MALLOC_CAP_SPIRAM);
    if (!nonliveBuf) nonliveBuf = (MatchData*)heap_caps_malloc(sizeof(MatchData) * CompetitionData::MAX_MATCHES, MALLOC_CAP_SPIRAM);
    if (!sortedBuf)  sortedBuf  = (MatchData*)heap_caps_malloc(sizeof(MatchData) * CompetitionData::MAX_MATCHES, MALLOC_CAP_SPIRAM);
    if (!liveBuf || !nonliveBuf || !sortedBuf) {
        Serial.println("[WARN] SceneManager: failed to allocate PSRAM buffers");
        return;
    }

    auto addComp = [&](const CompetitionData* d, int ci) {
        if (!d) return;
        const CompPrefs& p = prefs.comp[ci];
        if (!p.enabled) return;
        currentComp = ci;

        if (p.scores) {
            // Separate live matches (always first) from non-live
            MatchData* live = liveBuf;
            MatchData* nonlive = nonliveBuf;
            int nLive = 0, nNonlive = 0;
            for (int i = 0; i < d->result_count; i++) {
                if (d->results[i].status == MatchStatus::Live)
                    live[nLive++] = d->results[i];
                else
                    nonlive[nNonlive++] = d->results[i];
            }

            // Sort non-live by phase order (CC + T14/PD2 final phases that now carry group).
            // Regular season (no group) stays in parse/DB order (o==0 for all).
            if (nNonlive > 1) {
                // Only run phase sort if it's CC or at least one match has a group label (final phase data)
                bool needsPhaseSort = (ci == 2);
                if (!needsPhaseSort) {
                    for (int k = 0; k < nNonlive; k++) {
                        if (nonlive[k].group[0]) { needsPhaseSort = true; break; }
                    }
                }
                if (needsPhaseSort) {
                    for (int i = 0; i < nNonlive - 1; i++) {
                        for (int j = 0; j < nNonlive - i - 1; j++) {
                            int o1 = phaseOrder(nonlive[j].group);
                            int o2 = phaseOrder(nonlive[j+1].group);
                            if (o1 > o2 || (o1 == o2 && nonlive[j].round > nonlive[j+1].round)) {
                                MatchData tmp = nonlive[j];
                                nonlive[j] = nonlive[j+1];
                                nonlive[j+1] = tmp;
                            }
                        }
                    }
                }
            }

            int total = nLive + nNonlive;
            for (int i = 0; i < total && scoreIdx < MAX_SCORE_SCENES; i++) {
                const MatchData* m = (i < nLive) ? &live[i] : &nonlive[i - nLive];
                _scoreScenes[scoreIdx].setMatch(*m, comps[ci].name, comps[ci].color,
                                                i, total);
                addSlot(&_scoreScenes[scoreIdx], scoreMs);
                scoreIdx++;
            }
        }

        if (p.fixtures) {
            time_t now = time(nullptr);
            // Sort fixtures by phase for CC and for T14/PD2 when they carry final-phase groups (barrages etc.)
            bool usePhase = (ci == 2);
            if (!usePhase && d->fixture_count > 0) {
                for (int k = 0; k < d->fixture_count; k++) {
                    if (d->fixtures[k].group[0]) { usePhase = true; break; }
                }
            }
            if (usePhase && d->fixture_count > 1) {
                MatchData* sorted = sortedBuf;
                memcpy(sorted, d->fixtures, sizeof(MatchData) * d->fixture_count);
                for (int i = 0; i < d->fixture_count - 1; i++) {
                    for (int j = 0; j < d->fixture_count - i - 1; j++) {
                        int o1 = phaseOrder(sorted[j].group);
                        int o2 = phaseOrder(sorted[j+1].group);
                        if (o1 > o2 || (o1 == o2 && sorted[j].round > sorted[j+1].round)) {
                            MatchData tmp = sorted[j];
                            sorted[j] = sorted[j+1];
                            sorted[j+1] = tmp;
                        }
                    }
                }
                for (int i = 0; i < d->fixture_count && fixIdx < MAX_FIX_SCENES; i++) {
                    // Skip fixtures whose kickoff is more than 2h in the past
                    if (sorted[i].kickoff_utc > 0 && now > 0 && sorted[i].kickoff_utc < now - 7200) continue;
                    _fixScenes[fixIdx].setMatch(sorted[i], comps[ci].name, comps[ci].color,
                                                i, d->fixture_count);
                    addSlot(&_fixScenes[fixIdx], fixtureMs);
                    fixIdx++;
                }
            } else {
                for (int i = 0; i < d->fixture_count && fixIdx < MAX_FIX_SCENES; i++) {
                    // Skip fixtures whose kickoff is more than 2h in the past
                    if (d->fixtures[i].kickoff_utc > 0 && now > 0 && d->fixtures[i].kickoff_utc < now - 7200) continue;
                    _fixScenes[fixIdx].setMatch(d->fixtures[i], comps[ci].name, comps[ci].color,
                                                i, d->fixture_count);
                    addSlot(&_fixScenes[fixIdx], fixtureMs);
                    fixIdx++;
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

    int t14r = -1, t14f = -1, pd2r = -1, pd2f = -1, ccr = -1, ccf = -1;
    const CompetitionData* t14 = _db->acquireTop14();
    if (t14) { t14r = t14->result_count; t14f = t14->fixture_count; addComp(t14, 0); _db->release(); }
    const CompetitionData* pd2 = _db->acquireProd2();
    if (pd2) { pd2r = pd2->result_count; pd2f = pd2->fixture_count; addComp(pd2, 1); _db->release(); }
    const CompetitionData* cc  = _db->acquireCC();
    if (cc)  { ccr = cc->result_count;  ccf = cc->fixture_count;  addComp(cc,  2); _db->release(); }

    changed = (_slotCount != lastSlotCount);
    lastSlotCount = _slotCount;
    if (changed) {
        Serial.printf("SceneManager: %zu slots (T14:r=%d/f=%d, PD2:r=%d/f=%d, CC:r=%d/f=%d)\n",
                      _slotCount, t14r, t14f, pd2r, pd2f, ccr, ccf);
    }

    if (_slotCount == 0) return;
    if (_current >= _slotCount) _current = 0;
    _dirty = true;
}
