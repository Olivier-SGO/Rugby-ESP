#pragma once
#include "MatchData.h"
#include <HTTPClient.h>

class IdalgoParser {
public:
    // Fetch URL, stream-parse, fill out. Returns true on success.
    bool fetch(const char* url, CompetitionData& out);

private:
    static const size_t BUF_SZ   = 8192;
    static const size_t OVERLAP  = 2048;

    char _buf[BUF_SZ + OVERLAP + 1];

    int parseChunk(const char* chunk, size_t len, CompetitionData& out, bool isLast);
    const char* parseMatchBlock(const char* start, const char* end, MatchData& match);
    static bool readAttrVal(const char* html, const char* attr, char* dst, size_t dstLen);
    static bool readClassText(const char* html, const char* cls, char* dst, size_t dstLen);
};
