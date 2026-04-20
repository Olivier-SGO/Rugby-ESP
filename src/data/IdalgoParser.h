#pragma once
#include "MatchData.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class IdalgoParser {
public:
    bool fetch(const char* url, CompetitionData& out, WiFiClientSecure& client);

private:
    static const size_t BUF_SZ        = 4096;  // must be > MAX_BLOCK_SCAN
    static const size_t MAX_BLOCK_SCAN = 2048;  // look for match data within 2KB of data-match

    char* _buf = nullptr;

    // Returns byte offset of deferred content (start of last incomplete match), or len if all done.
    size_t parseChunk(const char* chunk, size_t len, CompetitionData& out, bool isLast);
    const char* parseMatchBlock(const char* start, const char* end, MatchData& match);
    static bool readAttrVal(const char* html, const char* attr, char* dst, size_t dstLen);
    static bool readClassText(const char* html, const char* cls, char* dst, size_t dstLen);
};
