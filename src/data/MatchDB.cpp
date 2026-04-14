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
void MatchDB::updateStandingsTop14(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (int i = 0; i < d.standing_count && i < CompetitionData::MAX_STANDING; i++)
        _top14.standings[i] = d.standings[i];
    _top14.standing_count = d.standing_count;
    xSemaphoreGive(_mutex);
}
void MatchDB::updateStandingsProd2(const CompetitionData& d) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (int i = 0; i < d.standing_count && i < CompetitionData::MAX_STANDING; i++)
        _prod2.standings[i] = d.standings[i];
    _prod2.standing_count = d.standing_count;
    xSemaphoreGive(_mutex);
}

const CompetitionData* MatchDB::acquireTop14() {
    xSemaphoreTake(_mutex, portMAX_DELAY); return &_top14;
}
const CompetitionData* MatchDB::acquireProd2() {
    xSemaphoreTake(_mutex, portMAX_DELAY); return &_prod2;
}
const CompetitionData* MatchDB::acquireCC() {
    xSemaphoreTake(_mutex, portMAX_DELAY); return &_cc;
}
void MatchDB::release() {
    xSemaphoreGive(_mutex);
}

bool MatchDB::hasLive() const {
    auto check = [](const CompetitionData& d) {
        for (int i = 0; i < d.result_count; i++)
            if (d.results[i].status == MatchStatus::Live) return true;
        return false;
    };
    return check(_top14) || check(_prod2) || check(_cc);
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

void MatchDB::persist() {
    JsonDocument doc;
    auto arr = doc["t14_r"].to<JsonArray>();
    for (int i = 0; i < _top14.result_count; i++)
        serializeMatch(arr.add<JsonObject>(), _top14.results[i]);
    File f = LittleFS.open("/cache.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

void MatchDB::load() {
    File f = LittleFS.open("/cache.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        auto arr = doc["t14_r"].as<JsonArrayConst>();
        _top14.result_count = 0;
        for (JsonObjectConst obj : arr) {
            if (_top14.result_count >= CompetitionData::MAX_MATCHES) break;
            deserializeMatch(obj, _top14.results[_top14.result_count++]);
        }
    }
    f.close();
}
