#include "DataFetcher.h"
#include "IdalgoParser.h"
#include <WiFi.h>
#include "credentials.h"
#include "config.h"
#include <Arduino.h>

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
    for (;;) {
        self->loop();
        vTaskDelay(pdMS_TO_TICKS(5000));
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
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print('.');
    }
    _wifiOk = (WiFi.status() == WL_CONNECTED);
    Serial.println(_wifiOk ? " OK" : " FAILED");
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
    // Suspend renderer to prevent heap fragmentation during TLS handshakes
    if (_rendererHandle) vTaskSuspend(_rendererHandle);

    IdalgoParser idalgo;
    CompetitionData top14, prod2, cc;

    const char* top14Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats";
    const char* prod2Base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats";

    // CC first — needs the most contiguous heap for TLS handshake
    Serial.printf("FetchAll: heap=%u max=%u (before CC)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    fetchCC(idalgo, cc);
    _db->updateCC(cc);
    vTaskDelay(pdMS_TO_TICKS(5000));

    Serial.printf("FetchAll: heap=%u max=%u (before Top14)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    if (idalgo.fetch(top14Base, top14)) {
        fetchNextJournee(top14, top14Base, idalgo);
        _db->updateTop14(top14);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));

    Serial.printf("FetchAll: heap=%u max=%u (before ProD2)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    if (idalgo.fetch(prod2Base, prod2)) {
        fetchNextJournee(prod2, prod2Base, idalgo);
        _db->updateProd2(prod2);
    }

    _firstFetchDone = true;
    _db->persist();
    Serial.printf("Fetch done — heap: %u max=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

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

    CompetitionData next;
    if (!idalgo.fetch(url, next)) return;

    for (int i = 0; i < next.fixture_count && d.fixture_count < CompetitionData::MAX_MATCHES; i++) {
        if (!isDuplicateMatch(d, next.fixtures[i], true))
            d.fixtures[d.fixture_count++] = next.fixtures[i];
    }
    for (int i = 0; i < next.result_count && d.result_count < CompetitionData::MAX_MATCHES; i++) {
        if (!isDuplicateMatch(d, next.results[i], false))
            d.results[d.result_count++] = next.results[i];
    }
}
