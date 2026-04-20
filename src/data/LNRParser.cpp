#include "LNRParser.h"
#include "TeamData.h"
#include <HTTPClient.h>
#include <Arduino.h>

bool LNRParser::fetch(const char* url, CompetitionData& out, WiFiClientSecure& client) {
    out.clear();

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, url);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("LNR %s → HTTP %d\n", url, code);
        http.end();
        return false;
    }

    _buf = (char*)malloc(BUF_SZ + OVERLAP + 1);
    if (!_buf) {
        Serial.println("LNR: OOM for parse buffer");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t overlapLen = 0;
    size_t totalRead  = 0;
    const size_t NET_CHUNK = BUF_SZ - OVERLAP;
    uint32_t stallMs = millis();

    bool earlyExit = false;
    while (http.connected() || stream->available()) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - stallMs > 50000) {
                Serial.printf("LNR: stream stall at %uKB\n", (unsigned)(totalRead/1024));
                earlyExit = true; break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        stallMs = millis();

        size_t toRead = min((size_t)avail, NET_CHUNK);
        size_t rd = stream->readBytes(_buf + overlapLen, toRead);
        vTaskDelay(1);
        if (!rd) break;

        _buf[overlapLen + rd] = '\0';
        bool isLast = !http.connected() && !stream->available();
        int prevCount = out.standing_count;
        parseChunk(_buf, overlapLen + rd, out, isLast);
        if (prevCount == 0 && out.standing_count > 0)
            Serial.printf("LNR: first entry at ~%u bytes\n", (unsigned)totalRead);

        if (!isLast && (overlapLen + rd) > OVERLAP) {
            memmove(_buf, _buf + (overlapLen + rd) - OVERLAP, OVERLAP);
            overlapLen = OVERLAP;
        }
        totalRead += rd;
        if (totalRead % 100000 < rd)
            Serial.printf("LNR: %uKB read, %d standings so far\n",
                          (unsigned)(totalRead/1024), out.standing_count);
        if (totalRead > 800000) { earlyExit = true; break; }
    }

    // Always fire diagnostic when standings == 0 after full read
    if (out.standing_count == 0) {
        size_t diagLen = overlapLen > 0 ? overlapLen : 0;
        if (diagLen > 0)
            parseChunk(_buf, diagLen, out, true);
        // Scan entire final buffer for any ranking-related keywords
        if (out.standing_count == 0 && _buf) {
            bool hasRanking = strstr(_buf, "ranking") || strstr(_buf, "classement");
            Serial.printf("LNR diagnostic: %s in last buffer (%u bytes)\n",
                          hasRanking ? "ranking/classement found" : "NO ranking content",
                          (unsigned)diagLen);
            if (hasRanking) {
                // Log up to 200 chars around first "ranking" hit
                const char* hit = strstr(_buf, "ranking");
                if (!hit) hit = strstr(_buf, "classement");
                int off = (int)(hit - _buf);
                int from = off > 100 ? off - 100 : 0;
                char ctx[220] = {};
                strlcpy(ctx, _buf + from, sizeof(ctx));
                Serial.printf("LNR context: ...%s...\n", ctx);
            }
        }
    }

    free(_buf); _buf = nullptr;
    Serial.printf("LNR: %u bytes, %d standings\n", totalRead, out.standing_count);
    return out.standing_count > 0;
}

// CSS class candidates for a ranking row (LNR site may rename classes on redesign)
static const char* const LNR_ROW_CLASSES[] = {
    "table-line--ranking-scrollable",
    "table-line--ranking",
    "ranking-scrollable",
    nullptr
};

int LNRParser::parseChunk(const char* chunk, size_t len, CompetitionData& out, bool isLast) {
    const char* pos = chunk;
    const char* end = chunk + len;
    int found = 0;

    while (pos < end && out.standing_count < CompetitionData::MAX_STANDING) {
        // Try each known CSS class pattern in priority order
        const char* row = nullptr;
        for (int ci = 0; LNR_ROW_CLASSES[ci] && !row; ci++)
            row = strstr(pos, LNR_ROW_CLASSES[ci]);
        if (!row || row >= end) {
            if (isLast && out.standing_count == 0 && len > 0) {
                // Help diagnose: did we see any ranking-like content?
                if (strstr(pos, "ranking") || strstr(pos, "classement"))
                    Serial.println("LNR: ranking/classement text found but no row pattern matched");
                else
                    Serial.println("LNR: no ranking content in final chunk — site HTML may have changed");
            }
            break;
        }

        // Find start of NEXT row to delimit this one
        const char* rowEnd = strstr(row + 30, "table-line--ranking");
        if (!rowEnd) {
            if (!isLast) break; // defer: row may be cut at chunk boundary
            rowEnd = end;
        }
        if (rowEnd > end) rowEnd = end;

        // Extract club name: find first '>' then skip inner tags to get text
        char cellText[48] = {};
        const char* gt = strchr(row, '>');
        if (gt && gt < rowEnd) {
            const char* textStart = gt + 1;
            while (textStart < rowEnd && *textStart == '<') {
                const char* next = strchr(textStart, '>');
                if (!next || next >= rowEnd) break;
                textStart = next + 1;
            }
            size_t i = 0;
            while (textStart[i] && textStart[i] != '<' && i + 1 < sizeof(cellText))
                cellText[i] = textStart[i++];
            cellText[i] = '\0';
            // trim trailing whitespace
            while (i > 0 && (cellText[i-1] == ' ' || cellText[i-1] == '\n' || cellText[i-1] == '\r'))
                cellText[--i] = '\0';
        }

        // Extract points: find second <div> within the row
        char pts[8] = {};
        const char* ptsPos = strstr(row + 30, "<div");
        if (ptsPos && ptsPos < rowEnd) {
            const char* ptsGt = strchr(ptsPos, '>');
            if (ptsGt && ptsGt < rowEnd) {
                size_t i = 0;
                ptsGt++;
                while (ptsGt[i] && ptsGt[i] != '<' && ptsGt[i] != ' ' && i + 1 < sizeof(pts))
                    pts[i] = ptsGt[i++];
                pts[i] = '\0';
            }
        }

        if (cellText[0]) {
            StandingEntry entry = {};
            entry.rank = out.standing_count + 1;

            const TeamEntry* t = findTeam(cellText);
            if (t) {
                strlcpy(entry.name,   t->canonical, sizeof(entry.name));
                strlcpy(entry.abbrev, t->abbrev,    sizeof(entry.abbrev));
                strlcpy(entry.slug,   t->slug,      sizeof(entry.slug));
            } else {
                char stripped[48];
                stripAccents(cellText, stripped, sizeof(stripped));
                strlcpy(entry.name, stripped, sizeof(entry.name));
            }
            entry.points = atoi(pts);
            out.standings[out.standing_count++] = entry;
            found++;
        }

        pos = rowEnd;
    }
    return found;
}
