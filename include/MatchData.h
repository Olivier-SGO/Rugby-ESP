#pragma once
#include <cstdint>
#include <ctime>

enum class MatchStatus : uint8_t {
    Scheduled = 0,
    Live      = 1,
    Finished  = 2
};

struct MatchData {
    char     home_name[24];   // longest club name: "Stade Aurillacois" (17)
    char     away_name[24];
    char     home_abbrev[6];  // abbreviations ≤ 5 chars
    char     away_abbrev[6];
    char     home_slug[18];   // longest slug: "bordeaux-begles" (15) + null
    char     away_slug[18];
    int16_t  home_score;      // -1 = not played yet
    int16_t  away_score;
    MatchStatus status;
    int8_t   minute;          // -1 = HT, 0 = unknown, >0 = minute
    time_t   kickoff_utc;     // 0 = TBD
    uint8_t  round;           // journée number
};

// Lightweight copy stored in ScoreboardScene/FixturesScene — no team names needed
struct MatchDisplay {
    char        home_abbrev[6];
    char        away_abbrev[6];
    char        home_slug[18];
    char        away_slug[18];
    int16_t     home_score;
    int16_t     away_score;
    MatchStatus status;
    int8_t      minute;
    time_t      kickoff_utc;
    uint8_t     round;
};

struct StandingEntry {
    char    name[24];     // longest name: "Stade Aurillacois" (17)
    char    abbrev[6];    // abbreviations ≤ 5 chars
    char    slug[18];     // longest slug: "bordeaux-begles" (15) + null
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
    uint32_t     round_url_base; // base ID for journee URLs: id + round (e.g. 11608)
    time_t       last_updated;

    void clear() {
        result_count = fixture_count = standing_count = current_round = 0;
        round_url_base = 0;
        last_updated = 0;
    }
};
