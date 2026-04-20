#pragma once
#include "MatchData.h"
#include <WiFiClientSecure.h>

class LNRParser {
public:
    bool fetch(const char* url, CompetitionData& out, WiFiClientSecure& client);

private:
    static const size_t BUF_SZ  = 6144;  // LNR rows are ~800B apart; 2KB overlap sufficient
    static const size_t OVERLAP = 2048;

    char* _buf = nullptr;
    int parseChunk(const char* chunk, size_t len, CompetitionData& out, bool isLast);
};
