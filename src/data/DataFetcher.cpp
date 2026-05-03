#include "DataFetcher.h"
#include "IdalgoParser.h"
#include "WiFiManager.h"
#include "OTAUpdater.h"
#include "SceneManager.h"
#include <WiFi.h>
#include "config.h"
#include "DisplayPrefs.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <climits>

extern uint8_t* gTLSReserve;
bool gFetching = false;

// RAII helper to ensure gFetching is always cleared on exit
struct FetchingFlag {
    FetchingFlag()  { gFetching = true; }
    ~FetchingFlag() { gFetching = false; }
};

static void releaseTLSReserve() {
    if (gTLSReserve) {
        free(gTLSReserve);
        gTLSReserve = nullptr;
        Serial.println("[HEAP] TLS reserve released for fetch");
    }
}

static void reclaimTLSReserve() {
    if (!gTLSReserve) {
        gTLSReserve = (uint8_t*)malloc(49152);
        if (gTLSReserve) {
            Serial.println("[HEAP] TLS reserve reclaimed");
        } else {
            Serial.println("[HEAP] WARNING: failed to reclaim TLS reserve");
        }
    }
}

// Return the index (0=Top14, 1=ProD2, 2=CC) of the competition that has
// a match within the hot window (-30 min … +3 h around kickoff).
static int hotCompetitionIndex(const CompetitionData* t14,
                                const CompetitionData* pd2,
                                const CompetitionData* cc,
                                const DisplayPrefs& prefs) {
    time_t now = time(nullptr);
    if (now <= 0) return -1;
    const time_t HOT_BEFORE = 30 * 60;   // 30 min before kickoff
    const time_t HOT_AFTER  = 30 * 60;  // 3 hours after kickoff

    auto compHotness = [&](const CompetitionData* d) -> int {
        if (!d) return INT_MAX;
        int best = INT_MAX;
        for (int i = 0; i < d->result_count; i++) {
            time_t ko = d->results[i].kickoff_utc;
            if (ko <= 0) continue;
            time_t delta = now - ko;
            if (delta >= -HOT_BEFORE && delta <= HOT_AFTER) {
                int absDelta = delta < 0 ? -(int)delta : (int)delta;
                if (absDelta < best) best = absDelta;
            }
        }
        for (int i = 0; i < d->fixture_count; i++) {
            time_t ko = d->fixtures[i].kickoff_utc;
            if (ko <= 0) continue;
            time_t delta = now - ko;
            if (delta >= -HOT_BEFORE && delta <= HOT_AFTER) {
                int absDelta = delta < 0 ? -(int)delta : (int)delta;
                if (absDelta < best) best = absDelta;
            }
        }
        return best;
    };

    int bestIdx = -1;
    int bestScore = INT_MAX;
    const CompetitionData* comps[3] = {t14, pd2, cc};
    for (int i = 0; i < 3; i++) {
        if (!prefs.comp[i].enabled) continue;
        int h = compHotness(comps[i]);
        if (h < bestScore) {
            bestScore = h;
            bestIdx = i;
        }
    }
    return bestIdx;
}

DataFetcher Fetcher;

void DataFetcher::begin(MatchDB* db) {
    _db = db;
    xTaskCreatePinnedToCore(taskFunc, "DataFetcher", 8192, this, 1, nullptr, 0);
}

void DataFetcher::taskFunc(void* param) {
    DataFetcher* self = static_cast<DataFetcher*>(param);
    if (WiFi.status() != WL_CONNECTED) self->connectWiFi();
    if (!self->_timeSynced) self->syncNTP();
    self->_lastIdalgo = millis();  // don't fetch immediately — boot fetch just finished
    static uint32_t lastStackLog = 0;
    for (;;) {
        self->loop();
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (millis() - lastStackLog > 30000) {
            lastStackLog = millis();
            UBaseType_t hw = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[STACK] DataFetcher high-water: %u bytes\n", hw);
        }
    }
}

void DataFetcher::connectWiFi() {
    if (WiFi.getMode() & WIFI_AP) {
        _wifiOk = false;
        return; // don't kill AP config mode
    }
    if (WiFi.status() == WL_CONNECTED) {
        _wifiOk = true;
        return;
    }
    _wifiOk = WiFiManager::connect();
    Serial.println(_wifiOk ? "WiFi connected OK" : "WiFi connection FAILED");
    if (!_wifiOk) Serial.printf("WiFi status: %d (SSID=%s)\n", WiFi.status(), WiFi.SSID().c_str());
    if (_wifiOk) {
        Serial.printf("WiFi IP: %s  GW: %s  DNS: %s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP().toString().c_str());
        // Quick DNS test
        IPAddress testIp;
        if (WiFi.hostByName("www.ladepeche.fr", testIp)) {
            Serial.printf("DNS test: www.ladepeche.fr → %s\n", testIp.toString().c_str());
        } else {
            Serial.println("DNS test: www.ladepeche.fr → FAILED");
        }
    }
}

#include <esp_sntp.h>

void DataFetcher::syncNTP() {
    static bool configured = false;
    if (!configured) {
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        configured = true;
    }
    Serial.print("NTP sync");
    uint32_t start = millis();
    time_t now = 0;
    while (now < 1000000000L && millis() - start < 20000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        Serial.print('.');
    }
    _timeSynced = (now > 1000000000L);
    Serial.println(_timeSynced ? " OK" : " FAILED");
    if (_timeSynced) {
        esp_sntp_stop();  // Stop SNTP thread — its UDP socket corrupts subsequent TLS handshakes
        Serial.println("[NTP] SNTP stopped to preserve TLS stability");
    }
}

void DataFetcher::loop() {
    uint32_t now = millis();
    if (WiFi.getMode() & WIFI_AP) {
        // AP config mode: don't try to connect to external WiFi (would kill the AP)
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        if (_wifiOk) {
            _wifiOk = false;
            Serial.println("[WIFI] disconnected");
        }
        // Light reconnect every 5s (no scan — lets ESP auto-reconnect kick in)
        if (now - _lastWiFiReconnectAttempt > 5000) {
            _lastWiFiReconnectAttempt = now;
            WiFi.reconnect();
            Serial.println("[WIFI] reconnect() called");
        }
        // Full scan+connect cycle every 30s as fallback
        if (now - _lastWiFiFullReconnect > 30000) {
            _lastWiFiFullReconnect = now;
            Serial.println("[WIFI] attempting full scan reconnect...");
            connectWiFi();
        }
        return;
    }
    if (!_wifiOk) {
        connectWiFi();
        if (_wifiOk) {
            Serial.println("[FETCH] WiFi reconnected — triggering immediate fetch");
            if (!_timeSynced) syncNTP();
            fetchRotating();
            _lastIdalgo = millis();
            _lastNTP = millis();
        }
        return;
    }
    if (!_timeSynced) { syncNTP(); return; }

    if (gOTADownloading) {
        Serial.println("[FETCH] OTA download in progress, skipping fetch");
        return;
    }
    uint32_t pollMs = _db->hasLive() ? POLL_LIVE_MS : POLL_NORMAL_MS;
    if (pollMs == POLL_NORMAL_MS) {
        DisplayPrefs prefs;
        loadDisplayPrefs(prefs);
        const CompetitionData *t14 = nullptr, *pd2 = nullptr, *cc = nullptr;
        _db->acquireAll(t14, pd2, cc);
        int hot = hotCompetitionIndex(t14, pd2, cc, prefs);
        _db->releaseAll();
        if (hot >= 0) {
            pollMs = POLL_HOT_MS;
            static int lastLoggedHot = -1;
            if (hot != lastLoggedHot) {
                lastLoggedHot = hot;
                const char* names[] = {"Top14", "ProD2", "CC"};
                Serial.printf("[FETCH] Hot competition detected: %s → fast poll %us\n",
                              names[hot], POLL_HOT_MS / 1000);
            }
        }
    }
    if (now - _lastIdalgo > pollMs || _lastIdalgo == 0) {
        fetchRotating();
        _lastIdalgo = now;
    }
    if (now - _lastNTP > NTP_INTERVAL_MS) {
        syncNTP();
        _lastNTP = now;
    }
}

void DataFetcher::fetchRotating() {
    FetchingFlag fetching;

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    UBaseType_t stackStart = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[STACK] fetchRotating start: high-water=%u\n", stackStart);

    releaseTLSReserve();
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (ESP.getFreeHeap() < 50000) {
        Serial.printf("[FETCH] freeHeap=%u < 50K, skipping fetchRotating\n", ESP.getFreeHeap());
        return;
    }

    DisplayPrefs prefs;
    loadDisplayPrefs(prefs);

    // If a competition has an imminent/in-progress match, ensure it is
    // fetched on the next cycle by skipping the rotation index to it.
    {
        const CompetitionData *t14 = nullptr, *pd2 = nullptr, *cc = nullptr;
        _db->acquireAll(t14, pd2, cc);
        int hot = hotCompetitionIndex(t14, pd2, cc, prefs);
        _db->releaseAll();
        if (hot >= 0 && (_fetchIndex % 3) != hot) {
            _fetchIndex = hot;
            const char* names[] = {"Top14", "ProD2", "CC"};
            Serial.printf("[FETCH] Rotating skip to hot: %s\n", names[hot]);
        }
    }

    IdalgoParser idalgo;

    const char* top14Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats";
    const char* prod2Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats";

    bool fetched = false;
    for (int attempts = 0; attempts < 3 && !fetched; attempts++) {
        int idx = _fetchIndex % 3;
        _fetchIndex = (idx + 1) % 3;

        if (idx == 0 && prefs.comp[0].enabled) {
            Serial.println("[FETCH] Rotating → Top14");
            CompetitionData* d = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
            if (d) {
                memset(d, 0, sizeof(CompetitionData));
                if (idalgo.fetch(top14Base, *d)) {
                    if (d->fixture_count == 0) fetchNextJournee(*d, top14Base, idalgo);
                    _db->updateTop14(*d);
                    fetched = true;
                }
                heap_caps_free(d);
            }
        } else if (idx == 1 && prefs.comp[1].enabled) {
            Serial.println("[FETCH] Rotating → ProD2");
            CompetitionData* d = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
            if (d) {
                memset(d, 0, sizeof(CompetitionData));
                if (idalgo.fetch(prod2Base, *d)) {
                    if (d->fixture_count == 0) fetchNextJournee(*d, prod2Base, idalgo);
                    _db->updateProd2(*d);
                    fetched = true;
                }
                heap_caps_free(d);
            }
        } else if (idx == 2 && prefs.comp[2].enabled) {
            Serial.println("[FETCH] Rotating → CC");
            CompetitionData* d = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
            if (d) {
                memset(d, 0, sizeof(CompetitionData));
                if (fetchCC(idalgo, *d)) {
                    _db->updateCC(*d);
                    fetched = true;
                }
                heap_caps_free(d);
            }
        }
    }

    if (fetched) {
        _firstFetchDone = true;
        Serial.printf("[FETCH] Rotating persist: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
        _db->persist();
        Scenes.markDirty();
    }

    UBaseType_t stackEnd = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[STACK] fetchRotating end: high-water=%u (start=%u)\n", stackEnd, stackStart);

    reclaimTLSReserve();
}

void DataFetcher::fetchAll(bool forceAll) {
    FetchingFlag fetching;
    UBaseType_t stackStart = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[STACK] fetchAll start: high-water=%u\n", stackStart);
    if (_rendererHandle) vTaskSuspend(_rendererHandle);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("FetchAll: WiFi not connected (status=%d), aborting\n", WiFi.status());
        if (_rendererHandle) vTaskResume(_rendererHandle);
        return;
    }

    releaseTLSReserve();
    // Let lwIP stabilize after WiFi connect/NTP before any TLS handshake
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (ESP.getFreeHeap() < 50000) {
        Serial.printf("[FETCH] freeHeap=%u < 50K, aborting fetchAll\n", ESP.getFreeHeap());
        if (_rendererHandle) vTaskResume(_rendererHandle);
        return;
    }

    DisplayPrefs prefs;
    loadDisplayPrefs(prefs);
    Serial.printf("[FETCH] prefs: T14=%d PD2=%d CC=%d\n",
                  prefs.comp[0].enabled, prefs.comp[1].enabled, prefs.comp[2].enabled);

    IdalgoParser idalgo;

    CompetitionData* top14 = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
    CompetitionData* prod2 = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
    CompetitionData* cc    = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
    if (top14) memset(top14, 0, sizeof(CompetitionData));
    if (prod2) memset(prod2, 0, sizeof(CompetitionData));
    if (cc)    memset(cc,    0, sizeof(CompetitionData));

    const char* top14Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats";
    const char* prod2Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats";

    Serial.printf("[FETCH] start: heap=%u maxBlock=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());

    // Boot priority: fetch the competition with an imminent/in-progress match first
    int bootOrder[3];
    int bootOrderCount = 0;
    bool bootFetched[3] = {false, false, false};
    int hotIdx = -1;
    {
        const CompetitionData *t14 = nullptr, *pd2 = nullptr, *cc = nullptr;
        _db->acquireAll(t14, pd2, cc);
        hotIdx = hotCompetitionIndex(t14, pd2, cc, prefs);
        _db->releaseAll();
        if (hotIdx >= 0) {
            const char* names[] = {"Top14", "ProD2", "CC"};
            Serial.printf("[FETCH] Boot priority: %s first (hot match detected)\n", names[hotIdx]);
        }
    }
    int defaultOrder[3] = {2, 0, 1};
    if (hotIdx >= 0) {
        bootOrder[bootOrderCount++] = hotIdx;
        bootFetched[hotIdx] = true;
    }
    for (int i = 0; i < 3; i++) {
        int idx = defaultOrder[i];
        if (!bootFetched[idx]) {
            bootOrder[bootOrderCount++] = idx;
            bootFetched[idx] = true;
        }
    }

    bool ccOk = false, t14Ok = false, pd2Ok = false;
    for (int step = 0; step < bootOrderCount; step++) {
        int idx = bootOrder[step];
        if (idx == 2) {
            if ((forceAll || prefs.comp[2].enabled) && cc) {
                if (forceAll && !prefs.comp[2].enabled) Serial.println("[FETCH] CC forced (boot)");
                for (int attempt = 0; attempt < 2 && !ccOk; attempt++) {
                    if (attempt > 0) {
                        Serial.println("[FETCH] CC retry after 2s...");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                    Serial.println("[FETCH] before CC fetch");
                    ccOk = fetchCC(idalgo, *cc);
                }
                Serial.printf("[FETCH] after CC fetch: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
                if (ccOk) {
                    _db->updateCC(*cc);
                    Serial.printf("[FETCH] after CC updateDB: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
                }
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                Serial.println("[FETCH] CC disabled, skipping");
            }
        } else if (idx == 0) {
            if ((forceAll || prefs.comp[0].enabled) && top14) {
                if (forceAll && !prefs.comp[0].enabled) Serial.println("[FETCH] Top14 forced (boot)");
                bool ok = false;
                for (int attempt = 0; attempt < 2 && !ok; attempt++) {
                    if (attempt > 0) {
                        Serial.println("[FETCH] Top14 retry after 2s...");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                    Serial.println("[FETCH] before Top14 fetch");
                    ok = idalgo.fetch(top14Base, *top14);
                }
                if (ok) {
                    Serial.println("[FETCH] Top14 fetch OK, before updateTop14");
                    _db->updateTop14(*top14);
                    fetchNextJournee(*top14, top14Base, idalgo);
                    Serial.println("[FETCH] after updateTop14");
                } else {
                    Serial.println("[FETCH] Top14 fetch failed, continuing");
                }
                Serial.printf("[FETCH] after Top14: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                Serial.println("[FETCH] Top14 disabled, skipping");
            }
        } else if (idx == 1) {
            if ((forceAll || prefs.comp[1].enabled) && prod2) {
                if (forceAll && !prefs.comp[1].enabled) Serial.println("[FETCH] ProD2 forced (boot)");
                bool ok = false;
                for (int attempt = 0; attempt < 2 && !ok; attempt++) {
                    if (attempt > 0) {
                        Serial.println("[FETCH] ProD2 retry after 2s...");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                    Serial.println("[FETCH] before ProD2 fetch");
                    ok = idalgo.fetch(prod2Base, *prod2);
                }
                if (ok) {
                    Serial.println("[FETCH] ProD2 fetch OK, before updateProd2");
                    _db->updateProd2(*prod2);
                    fetchNextJournee(*prod2, prod2Base, idalgo);
                    Serial.println("[FETCH] after updateProd2");
                } else {
                    Serial.println("[FETCH] ProD2 fetch failed, continuing");
                }
                Serial.printf("[FETCH] after ProD2: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                Serial.println("[FETCH] ProD2 disabled, skipping");
            }
        }
        if (_db->hasLive()) {
            Serial.println("[FETCH] Live detected — aborting fetchAll");
            break;
        }
    }

fetchCleanup:

    if (top14) heap_caps_free(top14);
    if (prod2) heap_caps_free(prod2);
    if (cc)    heap_caps_free(cc);

    _firstFetchDone = true;
    Serial.printf("[FETCH] before persist: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
    _db->persist();
    Serial.printf("[FETCH] done persist: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
    Scenes.markDirty();

    UBaseType_t stackEnd = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[STACK] fetchAll end: high-water=%u (start=%u)\n", stackEnd, stackStart);

    reclaimTLSReserve();
    if (_rendererHandle) vTaskResume(_rendererHandle);
}

void DataFetcher::fetchLive() {
    // Placeholder — réintroduit en Phase 2 si nécessaire
    fetchAll();
}

static bool isDuplicateMatch(const CompetitionData& d, const MatchData& m, bool fixture) {
    const MatchData* arr = fixture ? d.fixtures : d.results;
    int cnt = fixture ? d.fixture_count : d.result_count;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(arr[i].home_slug, m.home_slug) == 0 &&
            strcmp(arr[i].away_slug, m.away_slug) == 0)
            return true;
    }
    return false;
}

bool DataFetcher::fetchCC(IdalgoParser& idalgo, CompetitionData& d) {
    // 1. Calendar page: complete fixtures + historical results (pools + knockouts)
    bool ok = idalgo.fetchCalendar("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/calendrier", d);
    if (!ok) return false;

    // 2. Results page: live scores for the current knockout phase.
    // Only fetched when a match is in the hot window (-30 min … +3 h) to
    // save heap / TLS handshakes during quiet periods.
    time_t now = time(nullptr);
    bool needLive = false;
    if (now > 0) {
        for (int i = 0; i < d.result_count && !needLive; i++) {
            time_t ko = d.results[i].kickoff_utc;
            if (ko > 0 && now >= ko - 30 * 60 && now <= ko + 30 * 60) needLive = true;
        }
        for (int i = 0; i < d.fixture_count && !needLive; i++) {
            time_t ko = d.fixtures[i].kickoff_utc;
            if (ko > 0 && now >= ko - 30 * 60 && now <= ko + 30 * 60) needLive = true;
        }
    }

    if (needLive) {
        Serial.println("[FETCH] CC hot — fetching results page for live scores");
        // Let lwIP / heap stabilise after the calendar TLS session before a second handshake
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (ESP.getFreeHeap() < 50000) {
            Serial.printf("[FETCH] CC results skipped: freeHeap=%u < 50K after calendar\n", ESP.getFreeHeap());
        } else {
            CompetitionData* live = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
            if (!live) {
                Serial.println("[FETCH] CC live alloc failed — keeping calendar data only");
            } else {
                memset(live, 0, sizeof(CompetitionData));
                if (idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/resultats", *live)) {
                    // Merge live data into calendar data (results page wins)
                    for (int i = 0; i < live->result_count; i++) {
                        int idx = -1;
                        for (int j = 0; j < d.result_count; j++) {
                            if (strcmp(d.results[j].home_slug, live->results[i].home_slug) == 0 &&
                                strcmp(d.results[j].away_slug, live->results[i].away_slug) == 0) {
                                idx = j; break;
                            }
                        }
                        if (idx >= 0) {
                            d.results[idx] = live->results[i];
                        } else if (d.result_count < CompetitionData::MAX_MATCHES) {
                            d.results[d.result_count++] = live->results[i];
                        }
                    }
                    for (int i = 0; i < live->fixture_count; i++) {
                        int idx = -1;
                        for (int j = 0; j < d.fixture_count; j++) {
                            if (strcmp(d.fixtures[j].home_slug, live->fixtures[i].home_slug) == 0 &&
                                strcmp(d.fixtures[j].away_slug, live->fixtures[i].away_slug) == 0) {
                                idx = j; break;
                            }
                        }
                        if (idx >= 0) {
                            d.fixtures[idx] = live->fixtures[i];
                        } else if (d.fixture_count < CompetitionData::MAX_MATCHES) {
                            d.fixtures[d.fixture_count++] = live->fixtures[i];
                        }
                    }
                    // Remove from fixtures any match that now appears in results
                    for (int i = 0; i < live->result_count; i++) {
                        for (int j = 0; j < d.fixture_count; j++) {
                            if (strcmp(d.fixtures[j].home_slug, live->results[i].home_slug) == 0 &&
                                strcmp(d.fixtures[j].away_slug, live->results[i].away_slug) == 0) {
                                for (int k = j; k < d.fixture_count - 1; k++) d.fixtures[k] = d.fixtures[k + 1];
                                d.fixture_count--;
                                break;
                            }
                        }
                    }
                    Serial.printf("[FETCH] CC merged %d results + %d fixtures from results page\n",
                                  live->result_count, live->fixture_count);
                } else {
                    Serial.println("[FETCH] CC results page fetch failed — keeping calendar data only");
                }
                heap_caps_free(live);
            }
        }
    }
    return true;
}

void DataFetcher::fetchNextJournee(CompetitionData& d, const char* compPath,
                                    IdalgoParser& idalgo) {
    if (d.current_round == 0) return;
    int nextRound = d.current_round + 1;
    if (nextRound >= 40) return;
    uint32_t nextId = d.round_ids[nextRound];
    if (nextId == 0) return;

    char url[200];
    snprintf(url, sizeof(url), "%s/%lu/journee-%d", compPath, nextId, nextRound);
    Serial.printf("NextJournee: %s\n", url);

    CompetitionData* next = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
    if (!next) return;
    memset(next, 0, sizeof(CompetitionData));
    if (!idalgo.fetch(url, *next)) {
        heap_caps_free(next);
        return;
    }

    for (int i = 0; i < next->fixture_count && d.fixture_count < CompetitionData::MAX_MATCHES; i++) {
        if (!isDuplicateMatch(d, next->fixtures[i], true))
            d.fixtures[d.fixture_count++] = next->fixtures[i];
    }
    for (int i = 0; i < next->result_count && d.result_count < CompetitionData::MAX_MATCHES; i++) {
        if (!isDuplicateMatch(d, next->results[i], false))
            d.results[d.result_count++] = next->results[i];
    }
    heap_caps_free(next);
}
