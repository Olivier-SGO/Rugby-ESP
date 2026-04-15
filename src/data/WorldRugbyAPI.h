#pragma once
#include "MatchData.h"

class WorldRugbyAPI {
public:
    // Fetch upcoming fixtures for a competition ID. Returns true on success.
    bool fetchFixtures(const char* competitionId, CompetitionData& out);

private:
    static bool isTBD(time_t kickoffUtc);
};
