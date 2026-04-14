#include "IdalgoParser.h"
#include "TeamData.h"
#include <WiFiClientSecure.h>
#include <Arduino.h>

bool IdalgoParser::fetch(const char* url, CompetitionData& out) {
    out.clear();
    HTTPClient http;
    WiFiClientSecure* client = new WiFiClientSecure;
    client->setInsecure(); // TODO: use cert bundle in production

    http.begin(*client, url);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept-Language", "fr-FR,fr;q=0.9");
    http.addHeader("Accept-Encoding", "identity"); // disable gzip

    int code = http.GET();
    if (code != 200) {
        Serial.printf("Idalgo %s → HTTP %d\n", url, code);
        http.end(); delete client; return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t overlapLen = 0;
    size_t totalRead  = 0;
    const size_t NET_CHUNK = BUF_SZ - OVERLAP;

    while (http.connected() || stream->available()) {
        size_t avail = stream->available();
        if (!avail) { delay(5); continue; }

        size_t toRead = min((size_t)avail, NET_CHUNK);
        size_t read   = stream->readBytes(_buf + overlapLen, toRead);
        if (!read) break;

        _buf[overlapLen + read] = '\0';
        bool isLast = !http.connected() && !stream->available();
        parseChunk(_buf, overlapLen + read, out, isLast);

        // Keep overlap for next chunk
        if (!isLast && (overlapLen + read) > OVERLAP) {
            memmove(_buf, _buf + (overlapLen + read) - OVERLAP, OVERLAP);
            overlapLen = OVERLAP;
        }
        totalRead += read;
        if (totalRead > 500000) break; // safety
    }

    http.end(); delete client;
    Serial.printf("Idalgo: %u bytes, %d results, %d fixtures\n",
                  totalRead, out.result_count, out.fixture_count);
    return true;
}

int IdalgoParser::parseChunk(const char* chunk, size_t len,
                              CompetitionData& out, bool isLast) {
    const char* pos = chunk;
    const char* end = chunk + len;
    int found = 0;

    while (pos < end) {
        const char* divStart = strstr(pos, "data-match=\"");
        if (!divStart) break;

        const char* nextDiv = strstr(divStart + 12, "data-match=\"");
        const char* blockEnd = nextDiv ? nextDiv : end;

        if (!isLast && blockEnd == end) break;

        MatchData m = {};
        m.home_score = -1; m.away_score = -1;

        if (parseMatchBlock(divStart, blockEnd, m)) {
            if (m.status == MatchStatus::Finished &&
                out.result_count < CompetitionData::MAX_MATCHES)
                out.results[out.result_count++] = m;
            else if (m.status == MatchStatus::Scheduled &&
                     out.fixture_count < CompetitionData::MAX_MATCHES)
                out.fixtures[out.fixture_count++] = m;
            else if (m.status == MatchStatus::Live &&
                     out.result_count < CompetitionData::MAX_MATCHES)
                out.results[out.result_count++] = m;
            found++;
        }
        pos = blockEnd;
    }
    return found;
}

const char* IdalgoParser::parseMatchBlock(const char* start, const char* end, MatchData& match) {
    char status[4] = {};
    if (!readAttrVal(start, "data-status", status, sizeof(status))) return nullptr;

    if (strcmp(status, "0") == 0)
        match.status = MatchStatus::Scheduled;
    else if (strcmp(status, "1") == 0 || strcmp(status, "3") == 0)
        match.status = MatchStatus::Finished;
    else
        match.status = MatchStatus::Live; // "7", "2"

    char raw[64] = {};
    if (readClassText(start, "localteam_txt", raw, sizeof(raw))) {
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.home_name,   t->canonical, sizeof(match.home_name));
            strlcpy(match.home_abbrev, t->abbrev,    sizeof(match.home_abbrev));
            strlcpy(match.home_slug,   t->slug,      sizeof(match.home_slug));
        } else {
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(match.home_name, stripped, sizeof(match.home_name));
            strlcpy(match.home_abbrev, stripped, 5);
        }
    }

    if (readClassText(start, "visitorteam_txt", raw, sizeof(raw))) {
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.away_name,   t->canonical, sizeof(match.away_name));
            strlcpy(match.away_abbrev, t->abbrev,    sizeof(match.away_abbrev));
            strlcpy(match.away_slug,   t->slug,      sizeof(match.away_slug));
        } else {
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(match.away_name, stripped, sizeof(match.away_name));
            strlcpy(match.away_abbrev, stripped, 5);
        }
    }

    const char* scoreTag = "span_idalgo_score_square_score_txt";
    const char* p1 = strstr(start, scoreTag);
    if (p1) {
        const char* gt = strchr(p1, '>');
        if (gt) { match.home_score = atoi(gt + 1); }
        const char* p2 = strstr(p1 + strlen(scoreTag), scoreTag);
        if (p2) {
            gt = strchr(p2, '>');
            if (gt) { match.away_score = atoi(gt + 1); }
        }
    }

    if (match.status == MatchStatus::Live) {
        if (readClassText(start, "span_idalgo_score_square_score_status", raw, sizeof(raw))) {
            if (strcmp(raw, "MT") == 0) match.minute = -1;
            else match.minute = atoi(raw);
        }
    }

    if (match.status == MatchStatus::Scheduled) {
        if (readClassText(start, "idalgo_date_timezone", raw, sizeof(raw))) {
            int h = 0, m2 = 0;
            if (sscanf(raw, "%d:%d", &h, &m2) == 2) {
                match.kickoff_utc = (time_t)(h * 3600 + m2 * 60);
            }
        }
    }

    return end;
}

bool IdalgoParser::readAttrVal(const char* html, const char* attr, char* dst, size_t dstLen) {
    char search[64];
    snprintf(search, sizeof(search), "%s=\"", attr);
    const char* pos = strstr(html, search);
    if (!pos) return false;
    pos += strlen(search);
    size_t i = 0;
    while (pos[i] && pos[i] != '"' && i + 1 < dstLen) { dst[i] = pos[i]; i++; }
    dst[i] = '\0';
    return i > 0;
}

bool IdalgoParser::readClassText(const char* html, const char* cls, char* dst, size_t dstLen) {
    const char* pos = strstr(html, cls);
    if (!pos) return false;
    const char* gt = strchr(pos, '>');
    if (!gt) return false;
    gt++;
    size_t i = 0;
    while (gt[i] && gt[i] != '<' && i + 1 < dstLen) { dst[i] = gt[i]; i++; }
    dst[i] = '\0';
    while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '\n' || dst[i-1] == '\r'))
        dst[--i] = '\0';
    return i > 0;
}
