#include "IdalgoParser.h"
#include "TeamData.h"
#include "WiFiClientSecureSmall.h"
#include <Arduino.h>
#include <time.h>

// Replace common HTML entities in-place (e.g. &ccedil; -> ç, &egrave; -> è)
static void decodeHtmlEntities(char* s) {
    if (!s || !s[0]) return;
    static const struct { const char* ent; const char* repl; } MAP[] = {
        {"ccedil;","ç"},{"egrave;","è"},{"eacute;","é"},{"agrave;","à"},
        {"ecirc;","ê"},{"icirc;","î"},{"ocirc;","ô"},{"ucirc;","û"},
        {"uuml;","ü"},{"auml;","ä"},{"euml;","ë"},{"iuml;","ï"},{"ouml;","ö"},
        {"Ccedil;","Ç"},{"Eacute;","É"},{"Egrave;","È"},{"Agrave;","À"},
        {"Ecirc;","Ê"},{"Icirc;","Î"},{"Ocirc;","Ô"},{"Ucirc;","Û"},
        {"Auml;","Ä"},{"Euml;","Ë"},{"Iuml;","Ï"},{"Ouml;","Ö"},{"Uuml;","Ü"},
        {"apos;","'"},{"amp;","&"},{"quot;","\""},{"lt;","<"},{"gt;",">"},
        {nullptr, nullptr}
    };
    size_t i = 0, j = 0;
    while (s[i]) {
        if (s[i] == '&') {
            // Numeric entity &#NNN;
            if (s[i+1] == '#') {
                int val = 0;
                size_t k = i + 2;
                while (s[k] >= '0' && s[k] <= '9') {
                    val = val * 10 + (s[k] - '0');
                    k++;
                }
                if (s[k] == ';' && val > 0) {
                    if (val < 128) {
                        s[j++] = (char)val;
                    } else if (val < 0x800) {
                        s[j++] = (char)(0xC0 | (val >> 6));
                        s[j++] = (char)(0x80 | (val & 0x3F));
                    } else {
                        s[j++] = (char)(0xE0 | (val >> 12));
                        s[j++] = (char)(0x80 | ((val >> 6) & 0x3F));
                        s[j++] = (char)(0x80 | (val & 0x3F));
                    }
                    i = k + 1;
                    continue;
                }
            }
            bool replaced = false;
            for (int m = 0; MAP[m].ent; m++) {
                size_t elen = strlen(MAP[m].ent);
                if (strncmp(s + i + 1, MAP[m].ent, elen) == 0) {
                    size_t rlen = strlen(MAP[m].repl);
                    for (size_t r = 0; r < rlen; r++) s[j++] = MAP[m].repl[r];
                    i += elen + 1; // skip &ent; (elen already includes ;)
                    replaced = true;
                    break;
                }
            }
            if (replaced) continue;
        }
        s[j++] = s[i++];
    }
    s[j] = '\0';
}

// Read entire HTTP stream into a dynamically-growing buffer (forced PSRAM).
#include <esp_heap_caps.h>
static char* readEntireStream(WiFiClient* stream, HTTPClient& http, size_t& outLen) {
    int expectedSize = http.getSize();
    size_t cap;
    if (expectedSize > 0) {
        cap = (size_t)expectedSize + 1;
    } else {
        cap = 524288;  // ProD2/Top14 pages can exceed 256KB; PSRAM has plenty of room
    }
    char* buf = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.printf("[OOM] readEntireStream: initial PSRAM alloc failed (%u bytes)\n", (unsigned)cap);
        return nullptr;
    }
    size_t len = 0;
    uint32_t lastData = millis();

    if (expectedSize > 0) {
        // Known size: read exactly expectedSize bytes (no realloc needed)
        while (len < (size_t)expectedSize) {
            int avail = stream->available();
            if (!avail) {
                if (millis() - lastData > 120000) {
                    Serial.printf("[PSRAM] readEntireStream: timeout at %zu / %d bytes\n", len, expectedSize);
                    break;
                }
                if (!http.connected() && !stream->available()) break;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            int toRead = min(avail, 4096);
            int rd = stream->readBytes(buf + len, toRead);
            len += rd;
            if (rd > 0) lastData = millis();
            if (len > 600000) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else {
        // Unknown size: read with timeout, but avoid realloc to prevent heap corruption
        while (true) {
            int avail = stream->available();
            if (!avail) {
                if (millis() - lastData > 60000) break;
                if (!http.connected() && !stream->available()) break;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (len + avail + 1 > cap) {
                // Cap exceeded: abort rather than risk realloc corruption
                Serial.printf("[PSRAM] readEntireStream: cap exceeded (%zu + %d > %zu), aborting\n", len, avail, cap);
                heap_caps_free(buf);
                return nullptr;
            }
            int toRead = min(avail, 4096);
            int rd = stream->readBytes(buf + len, toRead);
            len += rd;
            if (rd > 0) lastData = millis();
            if (len > 600000) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    buf[len] = '\0';
    outLen = len;
    Serial.printf("[PSRAM] readEntireStream: %u bytes (cap=%u)\n", (unsigned)len, (unsigned)cap);
    return buf;
}

bool IdalgoParser::fetch(const char* url, CompetitionData& out) {
    out.clear();
    WiFiClientSecureSmall client;
    client.setInsecure();
    client.setHandshakeTimeout(30);
    client.setTimeout(30000); // 30s for the whole connect operation
    HTTPClient http;
    http.setTimeout(60000);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept-Language", "fr-FR,fr;q=0.9");
    http.addHeader("Accept-Encoding", "identity");
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("Idalgo %s → HTTP %d (%s)\n", url, code, http.errorToString(code).c_str());
        http.end();
        return false;
    }

    String enc = http.header("Content-Encoding");
    Serial.printf("Idalgo: HTTP 200, Content-Encoding=%s\n", enc.c_str());

    WiFiClient* stream = http.getStreamPtr();
    size_t totalLen = 0;
    char* page = readEntireStream(stream, http, totalLen);
    if (!page) {
        Serial.println("Idalgo: failed to allocate page buffer");
        http.end();
        return false;
    }

    Serial.printf("Idalgo: %u bytes total, parsing...\n", totalLen);
    parseChunk(page, totalLen, out, true);
    heap_caps_free(page);
    http.end();
    vTaskDelay(pdMS_TO_TICKS(500)); // laisser lwIP libérer le socket et le heap se stabiliser
    Serial.printf("Idalgo: %u bytes, %d results, %d fixtures\n",
                  totalLen, out.result_count, out.fixture_count);
    return true;
}

int IdalgoParser::parseChunk(const char* chunk, size_t len,
                              CompetitionData& out, bool isLast) {
    const char* pos = chunk;
    const char* end = chunk + len;
    int found = 0;

    // Extract journee links and standings first (usually in nav before match blocks)
    extractRoundLinks(chunk, len, out);
    parseStandingsChunk(chunk, len, out);

    while (pos < end) {
        const char* divStart = strstr(pos, "<div class=\"div_idalgo_dom_match div_idalgo_dom_match_rugby");
        if (!divStart) break;

        const char* blockEnd = strstr(divStart, "</li>");
        if (blockEnd) blockEnd += 5; // include </li>
        else blockEnd = end;

        if (!isLast && blockEnd == end) break;

        MatchData m = {};
        m.home_score = -1; m.away_score = -1;
        m.round = out.current_round;

        if (parseMatchBlock(divStart, blockEnd, m)) {
            // Deduplicate by (home, away) pair
            bool exists = false;
            for (int i = 0; i < out.result_count && !exists; i++) {
                if (strcmp(out.results[i].home_slug, m.home_slug) == 0 &&
                    strcmp(out.results[i].away_slug, m.away_slug) == 0)
                    exists = true;
            }
            for (int i = 0; i < out.fixture_count && !exists; i++) {
                if (strcmp(out.fixtures[i].home_slug, m.home_slug) == 0 &&
                    strcmp(out.fixtures[i].away_slug, m.away_slug) == 0)
                    exists = true;
            }
            if (!exists) {
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
        }
        pos = blockEnd;
    }

    // current_round must come from actually parsed matches, not nav links
    uint8_t maxRound = 0;
    for (int i = 0; i < out.result_count; i++)
        if (out.results[i].round > maxRound) maxRound = out.results[i].round;
    for (int i = 0; i < out.fixture_count; i++)
        if (out.fixtures[i].round > maxRound) maxRound = out.fixtures[i].round;
    if (maxRound > 0) out.current_round = maxRound;

    return found;
}

void IdalgoParser::extractRoundLinks(const char* chunk, size_t len, CompetitionData& out) {
    const char* end = chunk + len;

    // Extract current round from navigation (e.g. "Journée 22")
    const char* cur = strstr(chunk, "span_idalgo_content_competition_navigation_days_listbox_current");
    if (cur && cur < end) {
        const char* gt = strchr(cur, '>');
        if (gt) {
            const char* p = gt + 1;
            while (*p && !isdigit((unsigned char)*p)) p++;
            int round = atoi(p);
            if (round > 0 && round < 40) out.current_round = (uint8_t)round;
        }
    }

    const char* p = chunk;
    while (p < end) {
        p = strstr(p, "resultats/");
        if (!p) break;
        p += 10; // skip "resultats/"
        char* numEnd;
        unsigned long id = strtoul(p, &numEnd, 10);
        if (id > 0 && numEnd < end && strncmp(numEnd, "/journee-", 9) == 0) {
            int round = atoi(numEnd + 9);
            if (round > 0 && round < 40) {
                out.round_ids[round] = (uint32_t)id;
            }
            p = numEnd + 9;
        } else {
            p = numEnd ? numEnd : (chunk + len);
        }
    }
}

void IdalgoParser::parseStandingsChunk(const char* chunk, size_t len, CompetitionData& out) {
    const char* pos = chunk;
    const char* end = chunk + len;
    while (pos < end) {
        const char* liStart = strstr(pos, "<li class=\"li_idalgo_content_standing li_idalgo_content_standing_team_");
        if (!liStart) break;
        const char* liEnd = strstr(liStart, "</li>");
        if (!liEnd) break;
        StandingEntry e = {};
        if (parseStandingBlock(liStart, liEnd, e)) {
            // Stop when we hit a second table (rank 1 after we already have entries)
            if (e.rank == 1 && out.standing_count > 0) break;
            if (out.standing_count < CompetitionData::MAX_STANDING)
                out.standings[out.standing_count++] = e;
        }
        pos = liEnd + 5;
    }
}

bool IdalgoParser::parseStandingBlock(const char* start, const char* end, StandingEntry& entry) {
    char raw[64] = {};

    // Team name
    if (readClassText(start, "a_idalgo_content_standing_name", raw, sizeof(raw))) {
        decodeHtmlEntities(raw);
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(entry.name,   t->canonical, sizeof(entry.name));
            strlcpy(entry.abbrev, t->abbrev,    sizeof(entry.abbrev));
            strlcpy(entry.slug,   t->slug,      sizeof(entry.slug));
        } else {
            Serial.printf("Unknown standing team: '%s'\n", raw);
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(entry.name, stripped, sizeof(entry.name));
            strlcpy(entry.abbrev, stripped, 5);
        }
    } else {
        return false;
    }

    // Position (mandatory)
    if (readClassText(start, "span_idalgo_content_standing_position", raw, sizeof(raw)))
        entry.rank = (uint8_t)atoi(raw);
    else
        return false;

    // Points
    if (readClassText(start, "span_idalgo_content_standing_points", raw, sizeof(raw)))
        entry.points = (int16_t)atoi(raw);

    // Played
    if (readClassText(start, "span_idalgo_content_standing_played", raw, sizeof(raw)))
        entry.played = (uint8_t)atoi(raw);

    // Diff
    if (readClassText(start, "span_idalgo_content_standing_dif", raw, sizeof(raw)))
        entry.diff = (int16_t)atoi(raw);

    return true;
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
        decodeHtmlEntities(raw);
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.home_name,   t->canonical, sizeof(match.home_name));
            strlcpy(match.home_abbrev, t->abbrev,    sizeof(match.home_abbrev));
            strlcpy(match.home_slug,   t->slug,      sizeof(match.home_slug));
        } else {
            Serial.printf("Unknown home team: '%s'\n", raw);
            char stripped[64];
            stripAccents(raw, stripped, sizeof(stripped));
            strlcpy(match.home_name, stripped, sizeof(match.home_name));
            strlcpy(match.home_abbrev, stripped, 5);
        }
    }

    if (readClassText(start, "visitorteam_txt", raw, sizeof(raw))) {
        decodeHtmlEntities(raw);
        const TeamEntry* t = findTeam(raw);
        if (t) {
            strlcpy(match.away_name,   t->canonical, sizeof(match.away_name));
            strlcpy(match.away_abbrev, t->abbrev,    sizeof(match.away_abbrev));
            strlcpy(match.away_slug,   t->slug,      sizeof(match.away_slug));
        } else {
            Serial.printf("Unknown away team: '%s'\n", raw);
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
        if (readAttrVal(start, "data-value-default", raw, sizeof(raw))) {
            // Parse "Sat Apr 25 2026 14:30:00 +0200"
            char mon[4];
            int day, year, hh, mm, ss, tzh;
            if (sscanf(raw, "%*s %3s %d %d %d:%d:%d %d", mon, &day, &year, &hh, &mm, &ss, &tzh) == 7) {
                static const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
                int month = 0;
                for (int i = 0; i < 12; i++) {
                    if (strcasecmp(mon, MONTHS[i]) == 0) { month = i + 1; break; }
                }
                struct tm tm = {};
                tm.tm_year = year - 1900;
                tm.tm_mon  = month - 1;
                tm.tm_mday = day;
                tm.tm_hour = hh;
                tm.tm_min  = mm;
                tm.tm_sec  = ss;
                match.kickoff_utc = mktime(&tm);
            }
        } else if (readClassText(start, "idalgo_date_timezone", raw, sizeof(raw))) {
            // Fallback to HH:MM only (legacy pages without data-value-default)
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

// ── Champions Cup calendar page — pools + finals, single source ─────────────

bool IdalgoParser::fetchCalendar(const char* url, CompetitionData& out) {
    out.clear();
    const int TEMP_MAX = 80;
    MatchData* temp = (MatchData*)heap_caps_malloc(sizeof(MatchData) * TEMP_MAX, MALLOC_CAP_SPIRAM);
    int tempCount = 0;

    WiFiClientSecureSmall client;
    client.setInsecure();
    client.setHandshakeTimeout(30);
    client.setTimeout(30000); // 30s for the whole connect operation
    HTTPClient http;
    http.setTimeout(60000);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept-Language", "fr-FR,fr;q=0.9");
    http.addHeader("Accept-Encoding", "identity");
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("IdalgoCalendar %s → HTTP %d (%s)\n", url, code, http.errorToString(code).c_str());
        http.end();
        heap_caps_free(temp);
        return false;
    }

    String enc = http.header("Content-Encoding");
    Serial.printf("IdalgoCalendar: HTTP 200, Content-Encoding=%s\n", enc.c_str());

    WiFiClient* stream = http.getStreamPtr();
    size_t totalLen = 0;
    char* page = readEntireStream(stream, http, totalLen);
    if (!page) {
        Serial.println("IdalgoCalendar: failed to allocate page buffer");
        http.end();
        heap_caps_free(temp);
        return false;
    }

    Serial.printf("IdalgoCalendar: %u bytes total, parsing...\n", totalLen);
    parseCalendarChunk(page, totalLen, temp, tempCount, TEMP_MAX, true);
    heap_caps_free(page);
    http.end();
    vTaskDelay(pdMS_TO_TICKS(500)); // laisser lwIP libérer le socket et le heap se stabiliser

    // Determine the most advanced knockout phase present (1/8F < 1/4F < 1/2F < Finale)
    auto knockoutOrder = [](const char* g) -> int {
        if (strcmp(g, "1/8F") == 0) return 1;
        if (strcmp(g, "1/4F") == 0) return 2;
        if (strcmp(g, "1/2F") == 0) return 3;
        if (strcmp(g, "Finale") == 0) return 4;
        return 0;
    };
    // Keep the most advanced phase that has actually been played (Finished/Live).
    // If no knockout phase has been played yet, fall back to the most advanced listed phase.
    int maxKnockoutPhase = 0;
    for (int i = 0; i < tempCount; i++) {
        int o = knockoutOrder(temp[i].group);
        if (o > maxKnockoutPhase &&
            (temp[i].status == MatchStatus::Finished || temp[i].status == MatchStatus::Live))
            maxKnockoutPhase = o;
    }
    if (maxKnockoutPhase == 0) {
        for (int i = 0; i < tempCount; i++) {
            int o = knockoutOrder(temp[i].group);
            if (o > maxKnockoutPhase) maxKnockoutPhase = o;
        }
    }

    // ── Filter pools: scheduled + last 12 finished ──
    int scheduledPools = 0;
    int totalPoolFinished = 0;
    for (int i = 0; i < tempCount; i++) {
        bool isPool = (strncmp(temp[i].group, "Gr.", 3) == 0);
        if (isPool && temp[i].status == MatchStatus::Scheduled) scheduledPools++;
        if (isPool && temp[i].status == MatchStatus::Finished) totalPoolFinished++;
    }

    int poolFinishedSeen = 0;
    for (int i = 0; i < tempCount; i++) {
        bool isPool = (strncmp(temp[i].group, "Gr.", 3) == 0);
        bool keep = false;
        if (isPool) {
            if (scheduledPools > 0) {
                // Pool phase ongoing → keep scheduled + last 12 finished
                if (temp[i].status == MatchStatus::Scheduled) keep = true;
                else if (temp[i].status == MatchStatus::Finished) {
                    poolFinishedSeen++;
                    keep = (poolFinishedSeen > totalPoolFinished - 12);
                }
            } else {
                // Pools finished → discard
                keep = false;
            }
        } else {
            // Knockout finals: keep the most advanced phase that has results,
            // plus all later phases (fixtures). Also keep matches with
            // unrecognized/empty group to avoid losing data on HTML changes.
            int order = knockoutOrder(temp[i].group);
            if (maxKnockoutPhase == 0) {
                keep = true; // no recognizable finals found — keep all non-pool
            } else {
                keep = (order >= maxKnockoutPhase) || (order == 0);
            }
        }
        if (!keep) continue;

        if (temp[i].status == MatchStatus::Finished && out.result_count < CompetitionData::MAX_MATCHES)
            out.results[out.result_count++] = temp[i];
        else if (temp[i].status == MatchStatus::Scheduled && out.fixture_count < CompetitionData::MAX_MATCHES)
            out.fixtures[out.fixture_count++] = temp[i];
        else if (temp[i].status == MatchStatus::Live && out.result_count < CompetitionData::MAX_MATCHES)
            out.results[out.result_count++] = temp[i];
    }

    heap_caps_free(temp);
    Serial.printf("IdalgoCalendar: %u bytes, %d temp, %d results, %d fixtures\n",
                  totalLen, tempCount, out.result_count, out.fixture_count);
    return true;
}

int IdalgoParser::parseCalendarChunk(const char* chunk, size_t len,
                                      MatchData* temp, int& tempCount, int tempMax,
                                      bool isLast) {
    const char* pos = chunk;
    const char* end = chunk + len;
    int found = 0;

    // The calendar page uses a single format for all matches (pools + finals).
    // Finals are also present in <li class="li_idalgo_content_result_match"> but
    // those are 100% redundant — we skip them.
    while (pos < end && tempCount < tempMax) {
        const char* liStart = strstr(pos, "<li class=\"li_idalgo_content_calendar_cup_date_match\"");
        if (!liStart) break;
        const char* liEnd = strstr(liStart, "</li>");
        if (!liEnd) break;

        MatchData m = {};
        m.home_score = -1; m.away_score = -1;
        if (parseCalendarPoolBlock(liStart, liEnd, m)) {
            temp[tempCount++] = m;
            found++;
        }
        pos = liEnd + 5;
    }
    return found;
}

bool IdalgoParser::parseCalendarPoolBlock(const char* start, const char* end, MatchData& match) {
    char raw[64] = {};

    // data-state: 0=scheduled, 1=finished
    if (!readAttrVal(start, "data-state", raw, sizeof(raw))) return false;
    if (strcmp(raw, "0") == 0)
        match.status = MatchStatus::Scheduled;
    else if (strcmp(raw, "1") == 0)
        match.status = MatchStatus::Finished;
    else
        match.status = MatchStatus::Live;

    // Round
    if (readAttrVal(start, "data-round", raw, sizeof(raw)))
        match.round = (uint8_t)atoi(raw);

    // Group / pool: <div class="div_idalgo_content_calendar_cup_date_match_ctx"><span>Gr. 1</span>
    const char* ctxPos = strstr(start, "div_idalgo_content_calendar_cup_date_match_ctx");
    if (ctxPos && ctxPos < end) {
        const char* spanPos = strstr(ctxPos, "<span>");
        if (spanPos && spanPos < end) {
            spanPos += 6;
            size_t i = 0;
            while (spanPos[i] && spanPos[i] != '<' && i + 1 < sizeof(raw)) {
                raw[i] = spanPos[i]; i++;
            }
            raw[i] = '\0';
            // Normalise knockout phase labels (source sometimes omits the 'F')
            if (strcmp(raw, "1/8") == 0) strlcpy(match.group, "1/8F", sizeof(match.group));
            else if (strcmp(raw, "1/4") == 0) strlcpy(match.group, "1/4F", sizeof(match.group));
            else if (strcmp(raw, "1/2") == 0) strlcpy(match.group, "1/2F", sizeof(match.group));
            else strlcpy(match.group, raw, sizeof(match.group));
        }
    }

    // Home team
    const char* homePos = strstr(start, "a_idalgo_content_calendar_cup_date_match_local");
    if (homePos && homePos < end) {
        if (readAttrVal(homePos, "title", raw, sizeof(raw))) {
            decodeHtmlEntities(raw);
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
    }

    // Away team
    const char* awayPos = strstr(start, "a_idalgo_content_calendar_cup_date_match_visitor");
    if (awayPos && awayPos < end) {
        if (readAttrVal(awayPos, "title", raw, sizeof(raw))) {
            decodeHtmlEntities(raw);
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
    }

    // Score
    const char* scoreLeft = strstr(start, "span_idalgo_score_part_left");
    if (scoreLeft && scoreLeft < end) {
        const char* gt = strchr(scoreLeft, '>');
        if (gt) match.home_score = atoi(gt + 1);
    }
    const char* scoreRight = strstr(start, "span_idalgo_score_part_right");
    if (scoreRight && scoreRight < end) {
        const char* gt = strchr(scoreRight, '>');
        if (gt) match.away_score = atoi(gt + 1);
    }

    // Absolute date
    if (readAttrVal(start, "data-value-default", raw, sizeof(raw))) {
        char mon[4];
        int day, year, hh, mm, ss, tzh;
        if (sscanf(raw, "%*s %3s %d %d %d:%d:%d %d", mon, &day, &year, &hh, &mm, &ss, &tzh) == 7) {
            static const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            int month = 0;
            for (int i = 0; i < 12; i++) {
                if (strcasecmp(mon, MONTHS[i]) == 0) { month = i + 1; break; }
            }
            struct tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon  = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hh;
            tm.tm_min  = mm;
            tm.tm_sec  = ss;
            match.kickoff_utc = mktime(&tm);
        }
    }

    // Fallback: website sometimes leaves data-state="0" even after the match ends.
    // If kickoff is more than 4 hours in the past, treat as Finished so the
    // match is not lost during knockout-phase filtering.
    if (match.status == MatchStatus::Scheduled && match.kickoff_utc > 0) {
        time_t now = time(nullptr);
        if (now > match.kickoff_utc + 4 * 3600) {
            match.status = MatchStatus::Finished;
        }
    }

    return true;
}


