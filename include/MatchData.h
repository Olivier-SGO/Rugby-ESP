#pragma once
#include <cstdint>
#include <ctime>

enum class MatchStatus : uint8_t {
    Scheduled = 0,
    Live      = 1,
    Finished  = 2
};

struct MatchData {
    char     home_name[40];
    char     away_name[40];
    char     home_abbrev[8];
    char     away_abbrev[8];
    char     home_slug[32];   // logo filename without .bin
    char     away_slug[32];
    int16_t  home_score;      // -1 = not played yet
    int16_t  away_score;
    MatchStatus status;
    int8_t   minute;          // -1 = HT, 0 = unknown, >0 = minute
    time_t   kickoff_utc;     // 0 = TBD
    uint8_t  round;           // journée number
};

struct StandingEntry {
    char    name[40];
    char    abbrev[8];
    char    slug[32];
    uint8_t rank;
    int16_t points;
    uint8_t played;
    int16_t diff;
};

struct CompetitionData {
    static const uint8_t MAX_MATCHES  = 12;
    static const uint8_t MAX_STANDING = 18;

    MatchData    results[MAX_MATCHES];
    MatchData    fixtures[MAX_MATCHES];
    StandingEntry standings[MAX_STANDING];
    uint8_t      result_count;
    uint8_t      fixture_count;
    uint8_t      standing_count;
    uint8_t      current_round;
    time_t       last_updated;

    void clear() {
        result_count = fixture_count = standing_count = current_round = 0;
        last_updated = 0;
    }
};
