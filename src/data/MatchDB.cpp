#include "MatchDB.h"
#include "MatchRecord.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "JsonAllocator.h"

MatchDB DB;

void MatchDB::begin() {
    _mutex = xSemaphoreCreateMutex();
    _top14.clear(); _prod2.clear(); _cc.clear();
    load();
}

static bool sameMatch(const MatchData& a, const MatchData& b) {
    if (a.home_slug[0] && b.home_slug[0])
        return strcmp(a.home_slug, b.home_slug) == 0 && strcmp(a.away_slug, b.away_slug) == 0;
    return strcmp(a.home_name, b.home_name) == 0 && strcmp(a.away_name, b.away_name) == 0;
}

static int findMatch(const MatchData* arr, uint8_t cnt, const MatchData& m) {
    for (int i = 0; i < cnt; i++) if (sameMatch(arr[i], m)) return i;
    return -1;
}

static void removeMatch(MatchData* arr, uint8_t& cnt, int idx) {
    for (int i = idx; i < cnt - 1; i++) arr[i] = arr[i + 1];
    cnt--;
}

static void mergeCompetition(CompetitionData& dst, const CompetitionData& src) {
    // Merge results: update existing, add new
    for (int i = 0; i < src.result_count; i++) {
        int idx = findMatch(dst.results, dst.result_count, src.results[i]);
        if (idx >= 0) {
            dst.results[idx] = src.results[i];
        } else if (dst.result_count < CompetitionData::MAX_MATCHES) {
            dst.results[dst.result_count++] = src.results[i];
        }
    }
    // Merge fixtures: update existing, add new
    for (int i = 0; i < src.fixture_count; i++) {
        int idx = findMatch(dst.fixtures, dst.fixture_count, src.fixtures[i]);
        if (idx >= 0) {
            dst.fixtures[idx] = src.fixtures[i];
        } else if (dst.fixture_count < CompetitionData::MAX_MATCHES) {
            dst.fixtures[dst.fixture_count++] = src.fixtures[i];
        }
    }
    // If a match moved from fixtures to results, remove it from fixtures
    for (int i = 0; i < src.result_count; i++) {
        int idx = findMatch(dst.fixtures, dst.fixture_count, src.results[i]);
        if (idx >= 0) removeMatch(dst.fixtures, dst.fixture_count, idx);
    }
    // Standings: replace if new data available
    if (src.standing_count > 0) {
        dst.standing_count = src.standing_count;
        for (int i = 0; i < src.standing_count; i++) dst.standings[i] = src.standings[i];
    }
    // Update metadata
    if (src.current_round > 0) dst.current_round = src.current_round;

    for (int i = 0; i < 40; i++) {
        if (src.round_ids[i] > 0) dst.round_ids[i] = src.round_ids[i];
    }
    dst.last_updated = time(nullptr);
}

void MatchDB::updateTop14(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    mergeCompetition(_top14, d);
    xSemaphoreGive(_mutex);
}
void MatchDB::updateProd2(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    mergeCompetition(_prod2, d);
    xSemaphoreGive(_mutex);
}
void MatchDB::updateCC(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    mergeCompetition(_cc, d);
    xSemaphoreGive(_mutex);
}

static const CompetitionData* acquireWithTimeout(SemaphoreHandle_t mutex, const CompetitionData* d, const char* label) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        TaskHandle_t holder = xQueueGetMutexHolder(mutex);
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        const char* holderName = holder ? pcTaskGetName(holder) : "(none)";
        const char* selfName = self ? pcTaskGetName(self) : "?";
        Serial.printf("MatchDB: %s mutex timeout — holder=%s self=%s\n", label, holderName, selfName);
        return nullptr;
    }
    return d;
}

const CompetitionData* MatchDB::acquireTop14() {
    return acquireWithTimeout(_mutex, &_top14, "Top14");
}
const CompetitionData* MatchDB::acquireProd2() {
    return acquireWithTimeout(_mutex, &_prod2, "ProD2");
}
const CompetitionData* MatchDB::acquireCC() {
    return acquireWithTimeout(_mutex, &_cc, "CC");
}
void MatchDB::release() {
    xSemaphoreGive(_mutex);
}

void MatchDB::acquireAll(const CompetitionData*& t14, const CompetitionData*& pd2, const CompetitionData*& cc) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    t14 = &_top14;
    pd2 = &_prod2;
    cc  = &_cc;
}

void MatchDB::releaseAll() {
    xSemaphoreGive(_mutex);
}

bool MatchDB::hasLive() const {
    return liveMask() != 0;
}

uint8_t MatchDB::liveMask() const {
    auto check = [](const CompetitionData& d) {
        time_t now = time(nullptr);
        for (int i = 0; i < d.result_count; i++) {
            if (d.results[i].status == MatchStatus::Live) {
                // Ignore stale "live" matches older than 4 hours
                if (now > 0 && d.results[i].kickoff_utc > 0 && now - d.results[i].kickoff_utc > 14400)
                    continue;
                return true;
            }
        }
        return false;
    };
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint8_t mask = (check(_top14) ? 1u : 0u)
                 | (check(_prod2) ? 2u : 0u)
                 | (check(_cc)    ? 4u : 0u);
    xSemaphoreGive(_mutex);
    return mask;
}

static void serializeMatch(JsonObject obj, const MatchData& m) {
    obj["hn"] = m.home_name;  obj["an"] = m.away_name;
    obj["ha"] = m.home_abbrev; obj["aa"] = m.away_abbrev;
    obj["hs"] = m.home_slug;   obj["as_"] = m.away_slug;
    obj["sc_h"] = m.home_score; obj["sc_a"] = m.away_score;
    obj["st"] = (int)m.status; obj["min"] = m.minute;
    obj["ko"] = (long)m.kickoff_utc; obj["rnd"] = m.round;
    obj["grp"] = m.group;
}

static void deserializeMatch(JsonObjectConst obj, MatchData& m) {
    strlcpy(m.home_name,   obj["hn"]  | "", sizeof(m.home_name));
    strlcpy(m.away_name,   obj["an"]  | "", sizeof(m.away_name));
    strlcpy(m.home_abbrev, obj["ha"]  | "", sizeof(m.home_abbrev));
    strlcpy(m.away_abbrev, obj["aa"]  | "", sizeof(m.away_abbrev));
    strlcpy(m.home_slug,   obj["hs"]  | "", sizeof(m.home_slug));
    strlcpy(m.away_slug,   obj["as_"] | "", sizeof(m.away_slug));
    m.home_score  = obj["sc_h"] | -1;
    m.away_score  = obj["sc_a"] | -1;
    m.status      = (MatchStatus)(obj["st"] | 0);
    m.minute      = obj["min"] | 0;
    m.kickoff_utc = obj["ko"]  | 0L;
    m.round       = obj["rnd"] | 0;
    strlcpy(m.group, obj["grp"] | "", sizeof(m.group));
}

static void serializeStanding(JsonObject obj, const StandingEntry& s) {
    obj["n"]  = s.name;
    obj["a"]  = s.abbrev;
    obj["sl"] = s.slug;
    obj["r"]  = s.rank;
    obj["p"]  = s.points;
    obj["pl"] = s.played;
    obj["d"]  = s.diff;
}

static void deserializeStanding(JsonObjectConst obj, StandingEntry& s) {
    strlcpy(s.name,   obj["n"]  | "", sizeof(s.name));
    strlcpy(s.abbrev, obj["a"]  | "", sizeof(s.abbrev));
    strlcpy(s.slug,   obj["sl"] | "", sizeof(s.slug));
    s.rank   = obj["r"]  | 0;
    s.points = obj["p"]  | 0;
    s.played = obj["pl"] | 0;
    s.diff   = obj["d"]  | 0;
}

static void serializeCompetition(JsonObject obj, const CompetitionData& d) {
    auto ra = obj["r"].to<JsonArray>();
    for (int i = 0; i < d.result_count; i++) serializeMatch(ra.add<JsonObject>(), d.results[i]);
    auto fa = obj["f"].to<JsonArray>();
    for (int i = 0; i < d.fixture_count; i++) serializeMatch(fa.add<JsonObject>(), d.fixtures[i]);
    auto sa = obj["s"].to<JsonArray>();
    for (int i = 0; i < d.standing_count; i++) serializeStanding(sa.add<JsonObject>(), d.standings[i]);
    obj["rnd"] = d.current_round;

    auto rid = obj["rid"].to<JsonObject>();
    for (int i = 0; i < 40; i++) {
        if (d.round_ids[i] > 0) {
            char k[4]; snprintf(k, sizeof(k), "%d", i);
            rid[k] = d.round_ids[i];
        }
    }
}

static void deserializeCompetition(JsonObjectConst obj, CompetitionData& d) {
    d.clear();
    auto ra = obj["r"].as<JsonArrayConst>();
    for (JsonObjectConst o : ra) {
        if (d.result_count >= CompetitionData::MAX_MATCHES) break;
        deserializeMatch(o, d.results[d.result_count++]);
    }
    auto fa = obj["f"].as<JsonArrayConst>();
    for (JsonObjectConst o : fa) {
        if (d.fixture_count >= CompetitionData::MAX_MATCHES) break;
        deserializeMatch(o, d.fixtures[d.fixture_count++]);
    }
    auto sa = obj["s"].as<JsonArrayConst>();
    for (JsonObjectConst o : sa) {
        if (d.standing_count >= CompetitionData::MAX_STANDING) break;
        deserializeStanding(o, d.standings[d.standing_count++]);
    }
    d.current_round = obj["rnd"] | 0;

    auto rid = obj["rid"].as<JsonObjectConst>();
    for (JsonPairConst kv : rid) {
        int round = atoi(kv.key().c_str());
        if (round > 0 && round < 40) d.round_ids[round] = kv.value().as<uint32_t>();
    }
}

void MatchDB::persist() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    JsonDocument doc(&spiRamAlloc);
    serializeCompetition(doc["t14"].to<JsonObject>(), _top14);
    serializeCompetition(doc["pd2"].to<JsonObject>(), _prod2);
    serializeCompetition(doc["cc" ].to<JsonObject>(), _cc);
    File f = LittleFS.open("/cache.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
    xSemaphoreGive(_mutex);
}

void MatchDB::load() {
    File f = LittleFS.open("/cache.json", "r");
    if (!f) return;
    JsonDocument doc(&spiRamAlloc);
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        deserializeCompetition(doc["t14"], _top14);
        deserializeCompetition(doc["pd2"], _prod2);
        deserializeCompetition(doc["cc"],  _cc);
    }
    f.close();
}

// ── Compact binary persistence for Champions Cup ─────────────────────────────

void MatchDB::persistCCBinary(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    File f = LittleFS.open("/cc_data.bin", "w");
    if (!f) { xSemaphoreGive(_mutex); return; }

    uint32_t magic = 0x52474343; // "RGCC"
    f.write((uint8_t*)&magic, 4);
    uint8_t version = 1;
    f.write(version);
    f.write(d.result_count);
    f.write(d.fixture_count);
    f.write(d.current_round);

    for (int i = 0; i < d.result_count && i < CompetitionData::MAX_MATCHES; i++) {
        CCMatchRecord rec;
        MatchRecord::fromMatchData(d.results[i], rec);
        MatchRecord::writeRecord(f, rec);
    }
    for (int i = 0; i < d.fixture_count && i < CompetitionData::MAX_MATCHES; i++) {
        CCMatchRecord rec;
        MatchRecord::fromMatchData(d.fixtures[i], rec);
        MatchRecord::writeRecord(f, rec);
    }
    f.close();
    xSemaphoreGive(_mutex);
}

bool MatchDB::loadCCBinary(CompetitionData& d) {
    File f = LittleFS.open("/cc_data.bin", "r");
    if (!f) return false;

    uint32_t magic = 0;
    if (f.read((uint8_t*)&magic, 4) != 4 || magic != 0x52474343) {
        f.close(); return false;
    }
    uint8_t version = f.read();
    if (version != 1) { f.close(); return false; }

    uint8_t result_count = f.read();
    uint8_t fixture_count = f.read();
    d.current_round = f.read();
    d.result_count = 0;
    d.fixture_count = 0;

    for (int i = 0; i < result_count && i < CompetitionData::MAX_MATCHES; i++) {
        CCMatchRecord rec;
        if (!MatchRecord::readRecord(f, rec)) break;
        uint8_t crc = MatchRecord::crc8((const uint8_t*)&rec, sizeof(rec) - 1);
        if (crc != rec.crc8) continue;
        MatchRecord::toMatchData(rec, d.results[d.result_count++]);
    }
    for (int i = 0; i < fixture_count && i < CompetitionData::MAX_MATCHES; i++) {
        CCMatchRecord rec;
        if (!MatchRecord::readRecord(f, rec)) break;
        uint8_t crc = MatchRecord::crc8((const uint8_t*)&rec, sizeof(rec) - 1);
        if (crc != rec.crc8) continue;
        MatchRecord::toMatchData(rec, d.fixtures[d.fixture_count++]);
    }
    f.close();
    return true;
}
