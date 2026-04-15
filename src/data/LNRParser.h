#pragma once
#include "MatchData.h"

class LNRParser {
public:
    // Fetches lnr.fr classement URL, fills out.standings only
    bool fetch(const char* url, CompetitionData& out);
};
