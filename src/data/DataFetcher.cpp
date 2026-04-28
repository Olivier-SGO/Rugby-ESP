#include "DataFetcher.h"
#include "IdalgoParser.h"
#include "WiFiManager.h"
#include <WiFi.h>
#include "config.h"
#include "DisplayPrefs.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

DataFetcher Fetcher;

void DataFetcher::begin(MatchDB* db) {
    _db = db;
    xTaskCreatePinnedToCore(taskFunc, "DataFetcher", 20480, this, 1, nullptr, 0);
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
}

void DataFetcher::loop() {
    uint32_t now = millis();
    if (WiFi.getMode() & WIFI_AP) {
        // AP config mode: don't try to connect to external WiFi (would kill the AP)
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        _wifiOk = false;
        connectWiFi();
        return;
    }
    if (!_wifiOk) { connectWiFi(); return; }
    if (!_timeSynced) { syncNTP(); return; }

    uint32_t pollMs = _db->hasLive() ? POLL_LIVE_MS : POLL_NORMAL_MS;
    if (now - _lastIdalgo > pollMs || _lastIdalgo == 0) {
        fetchAll();
        _lastIdalgo = now;
    }
    if (now - _lastNTP > NTP_INTERVAL_MS) {
        syncNTP();
        _lastNTP = now;
    }
}

void DataFetcher::fetchAll() {
    if (_rendererHandle) vTaskSuspend(_rendererHandle);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("FetchAll: WiFi not connected (status=%d), aborting\n", WiFi.status());
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

    Serial.printf("[FETCH] start: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());

    if (prefs.comp[2].enabled && cc) {
        fetchCC(idalgo, *cc);
        Serial.printf("[FETCH] after CC fetch: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
        _db->updateCC(*cc);
        Serial.printf("[FETCH] after CC updateDB: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
    } else {
        Serial.println("[FETCH] CC disabled, skipping");
    }

    if (prefs.comp[0].enabled && top14) {
        Serial.println("[FETCH] before Top14 fetch");
        if (idalgo.fetch(top14Base, *top14)) {
            Serial.println("[FETCH] Top14 fetch OK, before fetchNextJournee");
            fetchNextJournee(*top14, top14Base, idalgo);
            Serial.println("[FETCH] before updateTop14");
            _db->updateTop14(*top14);
            Serial.println("[FETCH] after updateTop14");
        }
        Serial.printf("[FETCH] after Top14: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
    } else {
        Serial.println("[FETCH] Top14 disabled, skipping");
    }

    if (prefs.comp[1].enabled && prod2) {
        Serial.println("[FETCH] before ProD2 fetch");
        if (idalgo.fetch(prod2Base, *prod2)) {
            Serial.println("[FETCH] ProD2 fetch OK, before fetchNextJournee");
            fetchNextJournee(*prod2, prod2Base, idalgo);
            Serial.println("[FETCH] before updateProd2");
            _db->updateProd2(*prod2);
            Serial.println("[FETCH] after updateProd2");
        }
        Serial.printf("[FETCH] after ProD2: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
    } else {
        Serial.println("[FETCH] ProD2 disabled, skipping");
    }

    if (top14) heap_caps_free(top14);
    if (prod2) heap_caps_free(prod2);
    if (cc)    heap_caps_free(cc);

    _firstFetchDone = true;
    Serial.printf("[FETCH] before persist: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
    _db->persist();
    Serial.printf("[FETCH] done persist: heap=%u max=%u psram=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());

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

void DataFetcher::fetchCC(IdalgoParser& idalgo, CompetitionData& d) {
    // Single calendar page covers pools + all final phases
    idalgo.fetchCalendar("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/calendrier", d);
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
