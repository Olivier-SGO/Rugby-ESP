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
        // Wait for DNS to become functional. Right after DHCP, lwIP has the
        // resolver address but the first queries fail for a few seconds until the
        // AP starts forwarding — that breaks NTP and the very first fetch at boot.
        // Poll until a real resolution succeeds (up to ~15s) before returning.
        IPAddress testIp;
        bool dnsOk = false;
        for (int i = 0; i < 15; i++) {
            if (WiFi.hostByName("www.ladepeche.fr", testIp) && testIp != IPAddress(0, 0, 0, 0)) {
                Serial.printf("DNS test: www.ladepeche.fr → %s (try %d)\n", testIp.toString().c_str(), i + 1);
                dnsOk = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!dnsOk) Serial.println("DNS test: www.ladepeche.fr → FAILED after 15s, proceeding anyway");
    }
}

#include <esp_sntp.h>

void DataFetcher::syncNTP() {
    static bool configured = false;
    if (!configured) {
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        // Cloudflare hostname + two NTP IPs (Cloudflare, Google) as fallback so
        // time sync still works when DNS is momentarily unavailable at boot.
        configTime(0, 0, "time.cloudflare.com", "162.159.200.1", "216.239.35.0");
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
        // Full scan+connect cycle every 20s — tries all saved SSIDs by RSSI
        if (now - _lastWiFiFullReconnect > 20000) {
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
    if (_rendererHandle) vTaskSuspend(_rendererHandle);

    if (WiFi.status() != WL_CONNECTED) {
        if (_rendererHandle) vTaskResume(_rendererHandle);
        return;
    }

    releaseTLSReserve();
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (ESP.getFreeHeap() < 50000) {
        Serial.printf("[FETCH] freeHeap=%u < 50K, skipping fetchRotating\n", ESP.getFreeHeap());
        if (_rendererHandle) vTaskResume(_rendererHandle);
        return;
    }

    DisplayPrefs prefs;
    loadDisplayPrefs(prefs);

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
                bool baseOk = idalgo.fetch(top14Base, *d);
                if (baseOk) fetchNextJournee(*d, top14Base, idalgo);
                // Final phases live on separate pages — fetch them even if the
                // regular-phase page no longer responds after season end.
                enrichWithFinalPhases(*d, top14Base, idalgo);
                if (baseOk || d->result_count > 0 || d->fixture_count > 0) {
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
                bool baseOk = idalgo.fetch(prod2Base, *d);
                if (baseOk) fetchNextJournee(*d, prod2Base, idalgo);
                // Final phases live on separate pages — fetch them even if the
                // regular-phase page no longer responds after season end.
                enrichWithFinalPhases(*d, prod2Base, idalgo);
                if (baseOk || d->result_count > 0 || d->fixture_count > 0) {
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
        Scenes.markDirty();  // force scenes to rebuild with fresh (pruned) data
    }

    reclaimTLSReserve();
    if (_rendererHandle) vTaskResume(_rendererHandle);
}

void DataFetcher::fetchAll(bool forceAll) {
    FetchingFlag fetching;
    if (_rendererHandle) vTaskSuspend(_rendererHandle);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("FetchAll: WiFi not connected (status=%d), aborting\n", WiFi.status());
        if (_rendererHandle) vTaskResume(_rendererHandle);
        return;
    }

    releaseTLSReserve();
    // Let lwIP stabilize after WiFi connect/NTP before any TLS handshake.
    // Boot warm-up: the link is "cold" for a few seconds after association and the
    // first TLS connections get refused — wait longer here to avoid a wasted burst.
    vTaskDelay(pdMS_TO_TICKS(5000));

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

    bool ccOk = false;
    if ((forceAll || prefs.comp[2].enabled) && cc) {
        if (forceAll && !prefs.comp[2].enabled) Serial.println("[FETCH] CC forced (boot)");
        Serial.println("[FETCH] before CC fetch");
        ccOk = fetchCC(idalgo, *cc);
        Serial.printf("[FETCH] after CC fetch: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
        if (ccOk) {
            _db->updateCC(*cc);
            Serial.printf("[FETCH] after CC updateDB: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
        Serial.println("[FETCH] CC disabled, skipping");
    }

    if ((forceAll || prefs.comp[0].enabled) && top14) {
        if (forceAll && !prefs.comp[0].enabled) Serial.println("[FETCH] Top14 forced (boot)");
        Serial.println("[FETCH] before Top14 fetch");
        bool t14Base = idalgo.fetch(top14Base, *top14);
        if (!t14Base)
            Serial.println("[FETCH] Top14 base (phase-reguliere) fetch failed — trying final phases anyway");
        // Final phases live on separate pages — fetch them even if the regular-phase
        // page no longer responds after season end (otherwise updates freeze on barrages).
        enrichWithFinalPhases(*top14, top14Base, idalgo);
        if (t14Base || top14->result_count > 0 || top14->fixture_count > 0) {
            _db->updateTop14(*top14);
            Serial.printf("[FETCH] updateTop14 done (base=%d, %d res, %d fix)\n",
                          t14Base, top14->result_count, top14->fixture_count);
        } else {
            Serial.println("[FETCH] Top14: no data at all, skipping updateTop14");
        }
        Serial.printf("[FETCH] after Top14: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
        vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
        Serial.println("[FETCH] Top14 disabled, skipping");
    }

    if ((forceAll || prefs.comp[1].enabled) && prod2) {
        if (forceAll && !prefs.comp[1].enabled) Serial.println("[FETCH] ProD2 forced (boot)");
        Serial.println("[FETCH] before ProD2 fetch");
        bool pd2Base = idalgo.fetch(prod2Base, *prod2);
        if (!pd2Base)
            Serial.println("[FETCH] ProD2 base (phase-reguliere) fetch failed — trying final phases anyway");
        // Final phases live on separate pages — fetch them even if the regular-phase
        // page no longer responds after season end (otherwise updates freeze on barrages).
        enrichWithFinalPhases(*prod2, prod2Base, idalgo);
        if (pd2Base || prod2->result_count > 0 || prod2->fixture_count > 0) {
            _db->updateProd2(*prod2);
            Serial.printf("[FETCH] updateProd2 done (base=%d, %d res, %d fix)\n",
                          pd2Base, prod2->result_count, prod2->fixture_count);
        } else {
            Serial.println("[FETCH] ProD2: no data at all, skipping updateProd2");
        }
        Serial.printf("[FETCH] after ProD2: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
    } else {
        Serial.println("[FETCH] ProD2 disabled, skipping");
    }

fetchCleanup:

    if (top14) heap_caps_free(top14);
    if (prod2) heap_caps_free(prod2);
    if (cc)    heap_caps_free(cc);

    _firstFetchDone = true;
    Serial.printf("[FETCH] before persist: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());
    _db->persist();
    Scenes.markDirty();  // force scenes to rebuild with fresh (pruned) data
    Serial.printf("[FETCH] done persist: heap=%u free=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreeHeap(), ESP.getFreePsram());

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
    // Single calendar page covers pools + all final phases
    return idalgo.fetchCalendar("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/calendrier", d);
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

void DataFetcher::enrichWithFinalPhases(CompetitionData& d, const char* compPath,
                                        IdalgoParser& idalgo) {
    // Known final phase slugs on ladepeche (from nav on /resultats and phase pages 2026 season).
    // "barrages" currently carries the high-stakes opening playoff matches (press: "quart de finale" level).
    // Labels short for the 5x7 bottom-left indicator, consistent with CC.
    static const struct { const char* slug; const char* label; } PHASES[] = {
        {"barrages",     "1/4F"},
        {"demi-finales", "1/2F"},
        {"finale",       "Finale"},
        {nullptr, nullptr}
    };

    const char* comp = strstr(compPath, "top-14") ? "top-14" : "pro-d2";

    bool gotPhaseData = false;

    for (int p = 0; PHASES[p].slug; ++p) {
        char url[220];
        snprintf(url, sizeof(url),
                 "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/%s/%s/resultats",
                 comp, PHASES[p].slug);

        CompetitionData* pd = (CompetitionData*)heap_caps_malloc(sizeof(CompetitionData), MALLOC_CAP_SPIRAM);
        if (!pd) continue;
        memset(pd, 0, sizeof(CompetitionData));

        Serial.printf("[FETCH] Phase %s fetch (heap=%u)...\n", PHASES[p].slug, ESP.getFreeHeap());
        if (idalgo.fetch(url, *pd)) {
            if (pd->result_count > 0 || pd->fixture_count > 0) {
                gotPhaseData = true;
            }

            // Tag every match from this phase page with the short group label (if not already set by parser)
            for (int i = 0; i < pd->result_count; i++) {
                if (!pd->results[i].group[0])
                    strlcpy(pd->results[i].group, PHASES[p].label, sizeof(pd->results[i].group));
            }
            for (int i = 0; i < pd->fixture_count; i++) {
                if (!pd->fixtures[i].group[0])
                    strlcpy(pd->fixtures[i].group, PHASES[p].label, sizeof(pd->fixtures[i].group));
            }

            // Merge non-duplicates (reuse the same dedup as fetchNextJournee)
            for (int i = 0; i < pd->result_count && d.result_count < CompetitionData::MAX_MATCHES; i++) {
                if (!isDuplicateMatch(d, pd->results[i], false))
                    d.results[d.result_count++] = pd->results[i];
            }
            for (int i = 0; i < pd->fixture_count && d.fixture_count < CompetitionData::MAX_MATCHES; i++) {
                if (!isDuplicateMatch(d, pd->fixtures[i], true))
                    d.fixtures[d.fixture_count++] = pd->fixtures[i];
            }
            Serial.printf("[FETCH] Phase %s: +%d res +%d fix (now T%d/F%d)\n",
                          PHASES[p].slug, pd->result_count, pd->fixture_count,
                          d.result_count, d.fixture_count);
        }
        heap_caps_free(pd);
        vTaskDelay(pdMS_TO_TICKS(3000)); // space TLS handshakes — boot bursts get refused
    }

    // When final phases are active (any phase page returned data), drop *all* regular-season
    // Finished results brought by the /phase-reguliere page (J26, J30, etc.).
    // This prevents the last regular matchday from mixing with or reappearing alongside
    // barrages / demi-finales / finale scores.
    if (gotPhaseData) {
        int kept = 0;
        for (int i = 0; i < d.result_count; i++) {
            const MatchData& m = d.results[i];
            // Keep phase matches (have group) + any Live + any Scheduled.
            // Drop Finished regular-season matches (no group).
            if (m.group[0] || m.status == MatchStatus::Live || m.status == MatchStatus::Scheduled) {
                if (kept != i) d.results[kept] = m;
                kept++;
            }
        }
        if (kept < d.result_count) {
            Serial.printf("[FETCH] Pruned %d regular season results (final phases active, now %d results)\n",
                          d.result_count - kept, kept);
        }
        d.result_count = kept;
    }
}
