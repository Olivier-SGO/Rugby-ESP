#include "DataFetcher.h"
#include "IdalgoParser.h"
#include "LNRParser.h"
#include "WorldRugbyAPI.h"
#include <WiFi.h>
#include "credentials.h"
#include "config.h"
#include <Arduino.h>

DataFetcher Fetcher;

void DataFetcher::begin(MatchDB* db) {
    _db = db;
    xTaskCreatePinnedToCore(taskFunc, "DataFetcher", 16384, this, 1, nullptr, 0);
}

void DataFetcher::taskFunc(void* param) {
    DataFetcher* self = static_cast<DataFetcher*>(param);
    self->connectWiFi();
    self->syncNTP();
    for (;;) {
        self->loop();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void DataFetcher::connectWiFi() {
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

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("WiFi lost — reconnecting...");
        WiFi.reconnect();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void DataFetcher::syncNTP() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.print("NTP sync");
    uint32_t start = millis();
    time_t now = 0;
    while (now < 1000000000L && millis() - start < 15000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        Serial.print('.');
    }
    _timeSynced = (now > 1000000000L);
    Serial.println(_timeSynced ? " OK" : " FAILED");
}

void DataFetcher::loop() {
    uint32_t now = millis();
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
    IdalgoParser idalgo;
    LNRParser lnr;

    CompetitionData top14, prod2, cc;

    if (idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/top-14/phase-reguliere/resultats", top14))
        _db->updateTop14(top14);

    if (idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/pro-d2/phase-reguliere/resultats", prod2))
        _db->updateProd2(prod2);

    if (idalgo.fetch("https://www.ladepeche.fr/sports/resultats-sportifs/rugby/champions-cup/resultats", cc))
        _db->updateCC(cc);

    CompetitionData standings14, standingsPd2;
    if (millis() - _lastLNR > POLL_LNR_MS || _lastLNR == 0) {
        if (lnr.fetch("https://top14.lnr.fr/classement", standings14))
            _db->updateStandingsTop14(standings14);
        if (lnr.fetch("https://prod2.lnr.fr/classement", standingsPd2))
            _db->updateStandingsProd2(standingsPd2);
        _lastLNR = millis();
    }

    _db->persist();
}
