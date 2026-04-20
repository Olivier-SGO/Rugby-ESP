#pragma once
#include "MatchData.h"
#include <WiFiClientSecure.h>

class WorldRugbyAPI {
public:
    bool fetchFixtures(const char* competitionId, CompetitionData& out, WiFiClientSecure& client);

private:
    static bool isTBD(time_t kickoffUtc);
};
