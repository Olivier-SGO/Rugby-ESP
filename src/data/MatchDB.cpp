#include "MatchDB.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

MatchDB DB;

void MatchDB::begin() {
    _mutex = xSemaphoreCreateMutex();
    _top14.clear(); _prod2.clear(); _cc.clear();
    load();
}

void MatchDB::updateTop14(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _top14 = d;
    xSemaphoreGive(_mutex);
}
void MatchDB::updateProd2(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _prod2 = d;
    xSemaphoreGive(_mutex);
}
void MatchDB::updateCC(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _cc = d;
    xSemaphoreGive(_mutex);
}

static const CompetitionData* acquireWithTimeout(SemaphoreHandle_t mutex, const CompetitionData* d, const char* label) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Serial.printf("MatchDB: %s mutex timeout — task deadlocked?\n", label);
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

bool MatchDB::hasLive() const {
    return liveMask() != 0;
}

uint8_t MatchDB::liveMask() const {
    auto check = [](const CompetitionData& d) {
        for (int i = 0; i < d.result_count; i++)
            if (d.results[i].status == MatchStatus::Live) return true;
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
    obj["ub"] = d.round_url_base;
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
    d.round_url_base = obj["ub"] | 0;
}

void MatchDB::persist() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    JsonDocument doc;
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
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        deserializeCompetition(doc["t14"], _top14);
        deserializeCompetition(doc["pd2"], _prod2);
        deserializeCompetition(doc["cc"],  _cc);
    }
    f.close();
}
