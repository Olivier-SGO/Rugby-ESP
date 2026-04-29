#pragma once
#include "MatchData.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

class IdalgoParser {
public:
    // Fetch URL, stream-parse, fill out. Returns true on success.
    bool fetch(const char* url, CompetitionData& out);
    // Champions Cup calendar page — single source for pools + finals
    bool fetchCalendar(const char* url, CompetitionData& out);

private:
    int parseChunk(const char* chunk, size_t len, CompetitionData& out, bool isLast);
    const char* parseMatchBlock(const char* start, const char* end, MatchData& match);
    static void extractRoundLinks(const char* chunk, size_t len, CompetitionData& out);
    static void parseStandingsChunk(const char* chunk, size_t len, CompetitionData& out);
    static bool parseStandingBlock(const char* start, const char* end, StandingEntry& entry);

    // Calendar parsing: single format covers pools + finals
    int parseCalendarChunk(const char* chunk, size_t len,
                            MatchData* temp, int& tempCount, int tempMax,
                            bool isLast);
    bool parseCalendarPoolBlock(const char* start, const char* end, MatchData& match);
    static bool readAttrVal(const char* html, const char* attr, char* dst, size_t dstLen);
    static bool readClassText(const char* html, const char* cls, char* dst, size_t dstLen);
};
