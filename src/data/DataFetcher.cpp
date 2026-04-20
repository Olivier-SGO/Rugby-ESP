#include "DataFetcher.h"
#include "IdalgoParser.h"
#include "LNRParser.h"
#include "WorldRugbyAPI.h"
#include "DisplayManager.h"
#include "SceneManager.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "credentials.h"
#include "config.h"
#include <Arduino.h>

DataFetcher Fetcher;

void DataFetcher::begin(MatchDB* db) {
    _db = db;
    // Reset WiFi state so the task reconnects (caller may have called WiFi.mode(WIFI_OFF))
    _wifiOk = false;
    // Don't re-fetch immediately after boot fetch; respect normal poll interval
    _lastIdalgo = millis();

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("WiFi lost — reconnecting...");
        WiFi.reconnect();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    xTaskCreatePinnedToCore(taskFunc, "DataFetcher", 16384, this, 1, nullptr, 0);
}

void DataFetcher::taskFunc(void* param) {
    DataFetcher* self = static_cast<DataFetcher*>(param);
    self->connectWiFi();
    if (self->isWiFiConnected() && !self->_timeSynced) self->syncNTP();
    for (;;) {
        self->loop();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void DataFetcher::connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) { _wifiOk = true; return; }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print('.');
    }
    _wifiOk = (WiFi.status() == WL_CONNECTED);
    Serial.println(_wifiOk ? " OK" : " FAILED");
}

void DataFetcher::syncNTP() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    configTime(0, 0, "time.cloudflare.com", "162.159.200.1", "216.239.35.0");
    Serial.print("NTP sync");
    uint32_t start = millis();
    time_t now = 0;
    while (now < 1000000000L && millis() - start < 20000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        Serial.print('.');
    }
    _timeSynced = (now > 1000000000L);
    Serial.printf(" %s (epoch=%lld)\n", _timeSynced ? "OK" : "FAILED", (long long)now);
}

void DataFetcher::loop() {
    uint32_t now = millis();
    if (!_wifiOk) { connectWiFi(); return; }

    if (!_timeSynced && now - _lastNTP > 60000) {
        syncNTP();
        _lastNTP = now;
    }
    if (_timeSynced && now - _lastNTP > NTP_INTERVAL_MS) {
        syncNTP();
        _lastNTP = now;
    }

    bool live = _db->hasLive();
    uint32_t pollMs = live ? POLL_LIVE_MS : POLL_NORMAL_MS;
    if (now - _lastIdalgo > pollMs || _lastIdalgo == 0) {
        if (live)
            fetchLive();
        else
            fetchAll();

        Scenes.markDirty();
        _lastIdalgo = now;
    }
}

void DataFetcher::fetchAll() {
    Scenes.freeAllLogos();  // reclaim logo heap before TLS alloc (~16KB per active scene)
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20); // 20s socket timeout covers TLS handshake

    IdalgoParser idalgo;
    LNRParser lnr;

    CompetitionData d;

    d.clear();
    idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats", d, client);
    idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/calendrier", d, client);
    fetchNextJournee(d, "top-14/phase-reguliere", idalgo, client);
    if (d.result_count || d.fixture_count) _db->updateTop14(d);
    Serial.printf("Top14: round=%d res=%d fix=%d\n", d.current_round, d.result_count, d.fixture_count);

    d.clear();
    idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats", d, client);
    idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/calendrier", d, client);
    fetchNextJournee(d, "pro-d2/phase-reguliere", idalgo, client);
    if (d.result_count || d.fixture_count) _db->updateProd2(d);
    Serial.printf("ProD2: round=%d res=%d fix=%d\n", d.current_round, d.result_count, d.fixture_count);

    d.clear();
    fetchCC(client, idalgo, d);

    if (millis() - _lastLNR > POLL_LNR_MS || _lastLNR == 0) {
        d.clear();
        if (lnr.fetch("https://top14.lnr.fr/classement", d, client))
            _db->updateStandingsTop14(d);
        d.clear();
        if (lnr.fetch("https://prod2.lnr.fr/classement", d, client))
            _db->updateStandingsProd2(d);
        _lastLNR = millis();
    }

    _firstFetchDone = true;
    Serial.printf("fetchAll done — heap: %u\n", ESP.getFreeHeap());
    _db->persist();
}

void DataFetcher::fetchLive() {
    Scenes.freeAllLogos();
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);

    IdalgoParser idalgo;
    CompetitionData d;

    uint8_t mask = _db->liveMask();  // bit0=top14, bit1=prod2, bit2=cc

    if (mask & 1) {
        d.clear();
        idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats", d, client);
        idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/calendrier", d, client);
        fetchNextJournee(d, "top-14/phase-reguliere", idalgo, client);
        if (d.result_count || d.fixture_count) _db->updateTop14(d);
    }

    if (mask & 2) {
        d.clear();
        idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats", d, client);
        idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/calendrier", d, client);
        fetchNextJournee(d, "pro-d2/phase-reguliere", idalgo, client);
        if (d.result_count || d.fixture_count) _db->updateProd2(d);
    }

    if (mask & 4) {
        d.clear();
        fetchCC(client, idalgo, d);
    }

    _firstFetchDone = true;
    Serial.printf("fetchLive (mask=%u) done — heap: %u\n", mask, ESP.getFreeHeap());
    _db->persist();
}

void DataFetcher::fetchNextJournee(CompetitionData& d, const char* compPath,
                                    IdalgoParser& idalgo, WiFiClientSecure& client) {
    if (d.current_round == 0) {
        Serial.printf("fetchNextJournee: no round detected for %s\n", compPath);
        return;
    }
    char url[192];

    // Fetch current round results explicitly — catches split-weekend journées
    uint8_t prevRes = d.result_count;
    snprintf(url, sizeof(url),
        "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/%s/journee-%u/resultats",
        compPath, (unsigned)d.current_round);
    idalgo.fetch(url, d, client);
    if (d.result_count > prevRes)
        Serial.printf("fetchNextJournee: +%d results from journee-%u/resultats\n",
                      d.result_count - prevRes, (unsigned)d.current_round);

    // Fetch next round fixtures
    snprintf(url, sizeof(url),
        "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/%s/journee-%u/calendrier",
        compPath, (unsigned)(d.current_round + 1));
    uint8_t prevFix = d.fixture_count;
    idalgo.fetch(url, d, client);
    for (int i = prevFix; i < d.fixture_count; i++)
        d.fixtures[i].round = d.current_round + 1;
    if (d.fixture_count > prevFix)
        Serial.printf("fetchNextJournee: +%d fixtures from journee-%u\n",
                      d.fixture_count - prevFix, (unsigned)(d.current_round + 1));
}

void DataFetcher::fetchCC(WiFiClientSecure& client, IdalgoParser& idalgo, CompetitionData& d) {
    static const char* PHASES[] = {
        "1-4-de-finale", "phase-reguliere", "demi-finales", "finale", nullptr
    };
    const char* base = "https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup";

    // Try cached phase first
    if (_ccPhaseBase[0]) {
        char url[160];
        snprintf(url, sizeof(url), "%s/resultats", _ccPhaseBase);
        idalgo.fetch(url, d, client);
        snprintf(url, sizeof(url), "%s/calendrier", _ccPhaseBase);
        idalgo.fetch(url, d, client);
        if (d.result_count > 0 || d.fixture_count > 0) {
            _db->updateCC(d);
            Serial.printf("CC(%s): %d res %d fix\n", _ccPhaseBase + strlen(base) + 1,
                          d.result_count, d.fixture_count);
            return;
        }
        Serial.println("CC: cached phase empty — scanning list");
        _ccPhaseBase[0] = '\0';
    }

    // Try each known phase, stop at first with data
    for (int i = 0; PHASES[i]; i++) {
        d.clear();
        char url[160];
        snprintf(url, sizeof(url), "%s/%s/resultats", base, PHASES[i]);
        idalgo.fetch(url, d, client);
        snprintf(url, sizeof(url), "%s/%s/calendrier", base, PHASES[i]);
        idalgo.fetch(url, d, client);
        if (d.result_count > 0 || d.fixture_count > 0) {
            snprintf(_ccPhaseBase, sizeof(_ccPhaseBase), "%s/%s", base, PHASES[i]);
            Serial.printf("CC phase locked: %s (%d res %d fix)\n",
                          PHASES[i], d.result_count, d.fixture_count);
            _db->updateCC(d);
            return;
        }
    }
    Serial.println("CC: no data in any phase");
}
