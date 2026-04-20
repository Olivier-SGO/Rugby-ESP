#include "IdalgoParser.h"
#include "TeamData.h"
#include <Arduino.h>

bool IdalgoParser::fetch(const char* url, CompetitionData& out, WiFiClientSecure& client) {
    // Note: caller is responsible for out.clear() before the first fetch.
    // This allows accumulating results + fixtures from two URLs into the same struct.

    {
        const char* hostStart = strstr(url, "://");
        if (hostStart) {
            hostStart += 3;
            const char* hostEnd = strchr(hostStart, '/');
            char host[64] = {};
            if (hostEnd) strlcpy(host, hostStart, min((size_t)(hostEnd - hostStart + 1), sizeof(host)));
            IPAddress ip;
            bool dnsOk = WiFi.hostByName(host, ip);
            Serial.printf("Idalgo DNS %s → %s\n", host, dnsOk ? ip.toString().c_str() : "FAILED");
        }
    }

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, url);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept-Language", "fr-FR,fr;q=0.9");
    http.addHeader("Accept-Encoding", "identity");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("Idalgo %s → HTTP %d (%s)\n", url, code, http.errorToString(code).c_str());
        http.end();
        return false;
    }

    // Allocate parse buffer AFTER SSL handshake
    _buf = (char*)malloc(BUF_SZ + 1);
    if (!_buf) {
        Serial.println("Idalgo: OOM for parse buffer");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t overlapLen = 0;
    size_t totalRead  = 0;
    uint32_t stallMs = millis();

    while (http.connected() || stream->available()) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - stallMs > 12000) { Serial.println("Idalgo: stream stall"); break; }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        stallMs = millis();

        size_t space = BUF_SZ - overlapLen;
        if (space == 0) {
            // Buffer full but no deferred match found — skip (shouldn't happen)
            Serial.println("Idalgo: buffer full, dropping");
            overlapLen = 0;
            space = BUF_SZ;
        }

        size_t toRead = min((size_t)avail, space);
        size_t rd = stream->readBytes(_buf + overlapLen, toRead);
        if (!rd) break;

        size_t total = overlapLen + rd;
        _buf[total] = '\0';
        bool isLast = !http.connected() && !stream->available();
        size_t deferOffset = parseChunk(_buf, total, out, isLast);

        if (!isLast && deferOffset < total) {
            // Keep from deferOffset to end as overlap for next chunk
            size_t remaining = total - deferOffset;
            memmove(_buf, _buf + deferOffset, remaining);
            overlapLen = remaining;
        } else {
            overlapLen = 0;
        }
        totalRead += rd;
        if (totalRead > 500000) break;
    }

    free(_buf); _buf = nullptr;
    // http destructor runs after this return, client is still alive (owned by caller)
    Serial.printf("Idalgo: %u bytes, %d results, %d fixtures, %d standings, round=%d\n",
                  totalRead, out.result_count, out.fixture_count, out.standing_count, out.current_round);
    return true;
}

size_t IdalgoParser::parseChunk(const char* chunk, size_t len,
                                CompetitionData& out, bool isLast) {
    size_t deferOffset = len;
    const char* pos = chunk;
    const char* end = chunk + len;

    while (pos < end) {
        const char* divStart = strstr(pos, "data-match=\"");
        if (!divStart) break;

        const char* nextDiv = strstr(divStart + 12, "data-match=\"");

        // Use next data-match as block boundary if available.
        // Otherwise cap at MAX_BLOCK_SCAN bytes from divStart (all relevant data is within).
        const char* blockEnd;
        if (nextDiv) {
            blockEnd = nextDiv;
        } else {
            blockEnd = divStart + MAX_BLOCK_SCAN;
            if (blockEnd > end) blockEnd = end;
        }

        // Not enough data yet to cover MAX_BLOCK_SCAN — defer and wait for more
        if (!isLast && !nextDiv && (size_t)(end - divStart) < MAX_BLOCK_SCAN) {
            size_t d = (size_t)(divStart - chunk);
            if (d < deferOffset) deferOffset = d;
            break;
        }

        MatchData m = {};
        m.home_score = -1; m.away_score = -1; m.round = out.current_round;

        if (parseMatchBlock(divStart, blockEnd, m) && m.home_name[0] && m.away_name[0]) {
            if (m.round > out.current_round) out.current_round = m.round;

            // Dedup: skip if same home+away pair already stored
            auto isDupe = [&](const MatchData* arr, uint8_t cnt) -> bool {
                for (int di = 0; di < cnt; di++) {
                    if (m.home_slug[0] && arr[di].home_slug[0] &&
                        strcmp(arr[di].home_slug, m.home_slug) == 0 &&
                        strcmp(arr[di].away_slug, m.away_slug) == 0) return true;
                    if (!m.home_slug[0] &&
                        strcmp(arr[di].home_name, m.home_name) == 0 &&
                        strcmp(arr[di].away_name, m.away_name) == 0) return true;
                }
                return false;
            };

            if (m.status == MatchStatus::Finished &&
                out.result_count < CompetitionData::MAX_MATCHES &&
                !isDupe(out.results, out.result_count))
                out.results[out.result_count++] = m;
            else if (m.status == MatchStatus::Scheduled &&
                     out.fixture_count < CompetitionData::MAX_MATCHES &&
                     !isDupe(out.fixtures, out.fixture_count))
                out.fixtures[out.fixture_count++] = m;
            else if (m.status == MatchStatus::Live &&
                     out.result_count < CompetitionData::MAX_MATCHES &&
                     !isDupe(out.results, out.result_count))
                out.results[out.result_count++] = m;
            Serial.printf("Idalgo match: %s vs %s status=%d\n",
                          m.home_name, m.away_name, (int)m.status);
        }
        // Advance past divStart (not blockEnd) so next match starts from its own data-match
        pos = nextDiv ? blockEnd : (divStart + 1);
    }

    // Parse standings on every chunk (duplicates are filtered inside)
    {
        size_t sd = parseStandings(chunk, len, out, isLast);
        if (sd < deferOffset) deferOffset = sd;
    }

    // Detect current round from the "current day" nav indicator (most reliable).
    // "Journée 21" inside span_idalgo_content_competition_navigation_days_listbox_current
    {
        char roundStr[24] = {};
        if (readClassText(chunk, "span_idalgo_content_competition_navigation_days_listbox_current", roundStr, sizeof(roundStr))) {
            const char* p = roundStr;
            while (*p && !isdigit((unsigned char)*p)) p++;
            int n = atoi(p);
            if (n > 0) {
                out.current_round = (uint8_t)n;
                Serial.printf("Idalgo: detected round=%d from nav current\n", n);
            }
        }
    }

    // Fallback: infer from "/journee-N/xxx" hrefs in nav links
    // Also extracts round_url_base = id + round from /resultats/ID/journee-N links.
    int journeeLinks = 0;
    const char* jp = chunk;
    while ((jp = strstr(jp, "/journee-")) != nullptr && jp < end) {
        int n = atoi(jp + 9);
        const char* sl = strchr(jp + 9, '/');
        if (n > 0 && (!sl || strncmp(sl, "/resultats", 10) == 0 || strncmp(sl, "/calendrier", 11) == 0)) {
            journeeLinks++;
            // Only use link rounds for round_url_base, NOT for current_round
            // (links include past and future rounds, max is wrong)
            // Extract ID from /resultats/ID/journee-N pattern
            const char* rp = jp;
            while (rp > chunk && *(rp - 1) != '"') rp--; // rewind to start of href
            const char* res = strstr(rp, "/resultats/");
            if (res && res < jp) {
                int id = atoi(res + 11);
                if (id > 0 && n > 0) {
                    uint32_t base = (uint32_t)(id + n);
                    if (base > out.round_url_base) out.round_url_base = base;
                    Serial.printf("Idalgo: round_url_base=%u (id=%d round=%d)\n", base, id, n);
                }
            }
        }
        jp++;
    }
    if (out.current_round == 0 && journeeLinks > 0) {
        // Last resort: take max from links if nav current wasn't found
        jp = chunk;
        while ((jp = strstr(jp, "/journee-")) != nullptr && jp < end) {
            int n = atoi(jp + 9);
            const char* sl = strchr(jp + 9, '/');
            if (n > 0 && (!sl || strncmp(sl, "/resultats", 10) == 0 || strncmp(sl, "/calendrier", 11) == 0)) {
                if (n > (int)out.current_round) out.current_round = (uint8_t)n;
            }
            jp++;
        }
        if (out.current_round > 0)
            Serial.printf("Idalgo: detected round=%d from link fallback\n", out.current_round);
    }

    return deferOffset;
}

size_t IdalgoParser::parseStandings(const char* chunk, size_t len,
                                    CompetitionData& out, bool isLast) {
    const char* spos = chunk;
    const char* end = chunk + len;

    while (spos < end && out.standing_count < CompetitionData::MAX_STANDING) {
        const char* lineStart = strstr(spos, "div_idalgo_content_standing_line");
        if (!lineStart || lineStart >= end) break;

        const char* nextLine = strstr(lineStart + 30, "div_idalgo_content_standing_line");
        const char* lineEnd = nextLine ? nextLine : end;

        // If line is cut at chunk boundary and this isn't the last chunk, defer it
        if (!isLast && !nextLine) {
            return (size_t)(lineStart - chunk);
        }

        // Filter out header row: position cell must contain a number
        char posStr[8] = {};
        readClassText(lineStart, "span_idalgo_content_standing_position", posStr, sizeof(posStr));
        int rank = atoi(posStr);
        if (rank <= 0 || rank > CompetitionData::MAX_STANDING) {
            spos = lineEnd;
            continue;
        }

        StandingEntry entry = {};
        entry.rank = (uint8_t)rank;

        char name[64] = {};
        // Try text between <a class="a_idalgo_content_standing_name">...</a>
        if (!readClassText(lineStart, "a_idalgo_content_standing_name", name, sizeof(name))) {
            // Fallback: title attribute (some mobile templates use it)
            const char* aPos = strstr(lineStart, "a_idalgo_content_standing_name");
            if (aPos && aPos < lineEnd) {
                readAttrVal(aPos, "title", name, sizeof(name));
            }
        }

        if (name[0]) {
            const TeamEntry* t = findTeam(name);
            if (t) {
                strlcpy(entry.name,   t->canonical, sizeof(entry.name));
                strlcpy(entry.abbrev, t->abbrev,    sizeof(entry.abbrev));
                strlcpy(entry.slug,   t->slug,      sizeof(entry.slug));
            } else {
                char stripped[64];
                stripAccents(name, stripped, sizeof(stripped));
                strlcpy(entry.name, stripped, sizeof(entry.name));
            }
        }

        char pts[8] = {}, played[8] = {}, diffStr[8] = {};
        readClassText(lineStart, "span_idalgo_content_standing_points", pts, sizeof(pts));
        readClassText(lineStart, "span_idalgo_content_standing_played", played, sizeof(played));
        readClassText(lineStart, "span_idalgo_content_standing_dif",    diffStr, sizeof(diffStr));

        entry.points = atoi(pts);
        entry.played = atoi(played);
        entry.diff   = atoi(diffStr);

        // Deduplicate by name (same table may appear multiple times in HTML)
        bool dupe = false;
        for (int i = 0; i < out.standing_count; i++) {
            if (strcmp(out.standings[i].name, entry.name) == 0) { dupe = true; break; }
        }
        if (!dupe && entry.name[0] && out.standing_count < CompetitionData::MAX_STANDING) {
            out.standings[out.standing_count++] = entry;
        }
        spos = lineEnd;
    }
    return len;  // all processed
}

const char* IdalgoParser::parseMatchBlock(const char* start, const char* end, MatchData& match) {
    // Status: data-status may be absent on some matches (e.g. last fixture in list)
    char status[4] = {};
    bool hasStatus = readAttrVal(start, "data-status", status, sizeof(status));
    if (hasStatus) {
        if (strcmp(status, "0") == 0)
            match.status = MatchStatus::Scheduled;
        else if (strcmp(status, "1") == 0 || strcmp(status, "3") == 0)
            match.status = MatchStatus::Finished;
        else
            match.status = MatchStatus::Live;
    } else {
        match.status = MatchStatus::Scheduled; // default, overridden by score below
    }

    // Round number: try data-round, data-journee, data-week in order
    char rndStr[4] = {};
    bool hasRound = readAttrVal(start, "data-round",   rndStr, sizeof(rndStr));
    if (!hasRound) hasRound = readAttrVal(start, "data-journee", rndStr, sizeof(rndStr));
    if (!hasRound) hasRound = readAttrVal(start, "data-week",    rndStr, sizeof(rndStr));
    int rn = atoi(rndStr);
    if (rn > 0) {
        match.round = (uint8_t)rn;
        Serial.printf("Idalgo: match %s vs %s has round attr=%d\n", match.home_name, match.away_name, rn);
    }

    char raw[64] = {};
    if (readClassText(start, "localteam_txt", raw, sizeof(raw))) {
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.home_name,   t->canonical, sizeof(match.home_name));
            strlcpy(match.home_abbrev, t->abbrev,    sizeof(match.home_abbrev));
            strlcpy(match.home_slug,   t->slug,      sizeof(match.home_slug));
        } else {
            Serial.printf("Idalgo: unknown home team [%s]\n", raw);
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(match.home_name, stripped, sizeof(match.home_name));
            // Build a readable 4-char abbrev from first letters of words
            char abbr[8] = {}; int ai = 0;
            bool space = true;
            for (int ci = 0; stripped[ci] && ai < 4; ci++) {
                if (stripped[ci] == ' ' || stripped[ci] == '-') { space = true; }
                else if (space) { abbr[ai++] = stripped[ci]; space = false; }
            }
            if (ai == 0) strlcpy(abbr, stripped, 5);
            strlcpy(match.home_abbrev, abbr, sizeof(match.home_abbrev));
        }
    }

    if (readClassText(start, "visitorteam_txt", raw, sizeof(raw))) {
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.away_name,   t->canonical, sizeof(match.away_name));
            strlcpy(match.away_abbrev, t->abbrev,    sizeof(match.away_abbrev));
            strlcpy(match.away_slug,   t->slug,      sizeof(match.away_slug));
        } else {
            Serial.printf("Idalgo: unknown away team [%s]\n", raw);
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(match.away_name, stripped, sizeof(match.away_name));
            char abbr[8] = {}; int ai = 0;
            bool space = true;
            for (int ci = 0; stripped[ci] && ai < 4; ci++) {
                if (stripped[ci] == ' ' || stripped[ci] == '-') { space = true; }
                else if (space) { abbr[ai++] = stripped[ci]; space = false; }
            }
            if (ai == 0) strlcpy(abbr, stripped, 5);
            strlcpy(match.away_abbrev, abbr, sizeof(match.away_abbrev));
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
        // If we found scores but had no data-status, infer Finished
        if (!hasStatus && match.home_score >= 0 && match.away_score >= 0)
            match.status = MatchStatus::Finished;
    }

    if (match.status == MatchStatus::Live) {
        if (readClassText(start, "span_idalgo_score_square_score_status", raw, sizeof(raw))) {
            if (strcmp(raw, "MT") == 0) match.minute = -1;
            else match.minute = atoi(raw);
        }
    }

    if (match.status == MatchStatus::Scheduled) {
        // Parse time
        char timeStr[8] = {};
        readClassText(start, "idalgo_date_timezone", timeStr, sizeof(timeStr));
        int kh = 0, km = 0;
        bool hasTime = (sscanf(timeStr, "%d:%d", &kh, &km) == 2);

        // Parse date: try data-date (DD/MM/YYYY or YYYY-MM-DD) then datetime ISO attr
        char dateStr[32] = {};
        readAttrVal(start, "data-date", dateStr, sizeof(dateStr));
        if (!dateStr[0]) readAttrVal(start, "datetime", dateStr, sizeof(dateStr));

        int yr = 0, mo = 0, dy = 0;
        bool hasDate = false;

        if (dateStr[0]) {
            // ISO 8601: 2026-04-26T15:35:00+02:00 — also captures time
            if (sscanf(dateStr, "%d-%d-%dT%d:%d", &yr, &mo, &dy, &kh, &km) >= 3 && yr > 2020) {
                hasDate = true; hasTime = true;
            // DD/MM/YYYY
            } else if (sscanf(dateStr, "%d/%d/%d", &dy, &mo, &yr) == 3 && yr > 2020) {
                hasDate = true;
            // YYYY-MM-DD
            } else if (sscanf(dateStr, "%d-%d-%d", &yr, &mo, &dy) == 3 && yr > 2020) {
                hasDate = true;
            }
        }

        if (hasDate && hasTime) {
            struct tm t = {};
            t.tm_year = yr - 1900; t.tm_mon = mo - 1; t.tm_mday = dy;
            t.tm_hour = kh; t.tm_min = km; t.tm_isdst = -1;
            match.kickoff_utc = mktime(&t); // Paris local time → UTC epoch
        } else if (hasTime) {
            match.kickoff_utc = (time_t)(kh * 3600 + km * 60);
            if (match.home_name[0] && match.away_name[0])
                Serial.printf("Idalgo: no date for %s vs %s (time only)\n",
                              match.home_name, match.away_name);
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

// Decode common French HTML entities in-place (e.g. &egrave; → è in UTF-8)
static void decodeHtmlEntities(char* s) {
    static const struct { const char* entity; const char* utf8; } ENT[] = {
        {"&eacute;", "\xc3\xa9"}, {"&egrave;", "\xc3\xa8"}, {"&ecirc;",  "\xc3\xaa"},
        {"&ecirc;",  "\xc3\xaa"}, {"&agrave;", "\xc3\xa0"}, {"&acirc;",  "\xc3\xa2"},
        {"&ccedil;", "\xc3\xa7"}, {"&ocirc;",  "\xc3\xb4"}, {"&ucirc;",  "\xc3\xbb"},
        {"&ugrave;", "\xc3\xb9"}, {"&Eacute;", "\xc3\x89"}, {"&amp;",    "&"},
        {"&nbsp;",   " "},        {nullptr, nullptr}
    };
    for (int e = 0; ENT[e].entity; e++) {
        size_t elen = strlen(ENT[e].entity);
        size_t ulen = strlen(ENT[e].utf8);
        char* p;
        while ((p = strstr(s, ENT[e].entity)) != nullptr) {
            memmove(p + ulen, p + elen, strlen(p + elen) + 1);
            memcpy(p, ENT[e].utf8, ulen);
        }
    }
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
    decodeHtmlEntities(dst);
    return dst[0] != '\0';
}
