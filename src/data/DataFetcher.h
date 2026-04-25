#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "MatchDB.h"
#include "IdalgoParser.h"

class DataFetcher {
public:
    void begin(MatchDB* db);
    void setDB(MatchDB* db) { _db = db; }  // call before boot task; begin() also sets it
    void connectWiFi();
    void syncNTP();
    void fetchAll();
    void setRendererHandle(TaskHandle_t h) { _rendererHandle = h; }

    bool isWiFiConnected() const { return _wifiOk; }
    bool isTimeSync() const { return _timeSynced; }
    bool isFirstFetchDone() const { return _firstFetchDone; }

private:
    MatchDB* _db = nullptr;
    bool _wifiOk = false;
    bool _timeSynced = false;
    bool _firstFetchDone = false;
    uint32_t _lastIdalgo = 0;
    uint32_t _lastNTP = 0;
    TaskHandle_t _rendererHandle = nullptr;
    char _ccPhaseBase[128] = {};  // cached CC base URL, e.g. ".../champions-cup/1-4-de-finale"

    void fetchLive();
    void loop();
    static void taskFunc(void* param);

    // Fetch CC results+fixtures, trying each known phase until one has data
    void fetchCC(IdalgoParser& idalgo, CompetitionData& d);
    // If d.current_round > 0, fetch journee-(round+1)/calendrier to get full next round fixtures
    void fetchNextJournee(CompetitionData& d, const char* compPath,
                          IdalgoParser& idalgo);
};

extern DataFetcher Fetcher;
