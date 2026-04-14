#pragma once
#include <Arduino.h>
#include "MatchDB.h"

class DataFetcher {
public:
    void begin(MatchDB* db);
    void loop();
    bool isWiFiConnected() const { return _wifiOk; }
    bool isTimeSync() const { return _timeSynced; }

private:
    MatchDB* _db = nullptr;
    bool _wifiOk = false;
    bool _timeSynced = false;
    uint32_t _lastIdalgo = 0;
    uint32_t _lastLNR = 0;
    uint32_t _lastNTP = 0;

    void connectWiFi();
    void syncNTP();
    void fetchAll();

    static void taskFunc(void* param);
};

extern DataFetcher Fetcher;
