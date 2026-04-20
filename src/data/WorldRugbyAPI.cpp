#include "WorldRugbyAPI.h"
#include "TeamData.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Arduino.h>

bool WorldRugbyAPI::fetchFixtures(const char* compId, CompetitionData& out, WiFiClientSecure& client) {
    out.fixture_count = 0;

    time_t now; time(&now);
    struct tm tm_start = *gmtime(&now);
    tm_start.tm_mday -= 1; mktime(&tm_start);
    struct tm tm_end = *gmtime(&now);
    tm_end.tm_mday += 14; mktime(&tm_end);

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.wr-rims-prod.pulselive.com/rugby/v3/match"
        "?startDate=%04d-%02d-%02d&endDate=%04d-%02d-%02d"
        "&sort=asc&page_size=20&competition=%s",
        tm_start.tm_year+1900, tm_start.tm_mon+1, tm_start.tm_mday,
        tm_end.tm_year+1900,   tm_end.tm_mon+1,   tm_end.tm_mday,
        compId);

    HTTPClient http;
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("WorldRugby HTTP %d\n", code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, *http.getStreamPtr());
    // http destructor runs after return, client still alive (owned by caller)
    if (err) return false;

    JsonArray matches = doc["content"].as<JsonArray>();
    for (JsonObject m : matches) {
        if (out.fixture_count >= CompetitionData::MAX_MATCHES) break;

        const char* statusType = m["status"]["type"] | "U";
        if (strcmp(statusType, "U") != 0) continue;

        MatchData fix = {};
        fix.home_score = -1; fix.away_score = -1;
        fix.status = MatchStatus::Scheduled;

        JsonArray teams = m["teams"].as<JsonArray>();
        if (teams.size() >= 2) {
            const char* hName = teams[0]["name"] | "";
            const char* aName = teams[1]["name"] | "";

            const TeamEntry* th = findTeam(hName);
            const TeamEntry* ta = findTeam(aName);

            strlcpy(fix.home_name,   th ? th->canonical : hName, sizeof(fix.home_name));
            strlcpy(fix.home_abbrev, th ? th->abbrev    : hName, sizeof(fix.home_abbrev));
            strlcpy(fix.home_slug,   th ? th->slug      : "",    sizeof(fix.home_slug));
            strlcpy(fix.away_name,   ta ? ta->canonical : aName, sizeof(fix.away_name));
            strlcpy(fix.away_abbrev, ta ? ta->abbrev    : aName, sizeof(fix.away_abbrev));
            strlcpy(fix.away_slug,   ta ? ta->slug      : "",    sizeof(fix.away_slug));
        }

        time_t kickoff = (time_t)(m["time"]["millis"].as<long long>() / 1000LL);
        fix.kickoff_utc = isTBD(kickoff) ? 0 : kickoff;

        out.fixtures[out.fixture_count++] = fix;
    }
    return out.fixture_count > 0;
}

bool WorldRugbyAPI::isTBD(time_t kickoffUtc) {
    struct tm* t = localtime(&kickoffUtc);
    return (t->tm_hour == 0 || t->tm_hour == 1 || t->tm_hour == 2) && t->tm_min == 0;
}
