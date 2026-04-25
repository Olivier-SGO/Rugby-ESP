#include "MatchRecord.h"
#include "TeamData.h"
#include <cstring>

uint8_t MatchRecord::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t b = *data++;
        crc ^= b;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

void MatchRecord::fromMatchData(const MatchData& src, CCMatchRecord& dst) {
    dst.kickoffEpoch = (uint32_t)src.kickoff_utc;
    strlcpy(dst.homeSlug, src.home_slug, sizeof(dst.homeSlug));
    strlcpy(dst.awaySlug, src.away_slug, sizeof(dst.awaySlug));
    dst.homeScore = src.home_score;
    dst.awayScore = src.away_score;
    dst.status    = (uint8_t)src.status;
    dst.round     = src.round;
    strlcpy(dst.group, src.group, sizeof(dst.group));
    dst.crc8 = 0;
    dst.crc8 = crc8((const uint8_t*)&dst, sizeof(dst) - 1);
}

void MatchRecord::toMatchData(const CCMatchRecord& src, MatchData& dst) {
    memset(&dst, 0, sizeof(dst));
    dst.kickoff_utc = (time_t)src.kickoffEpoch;
    strlcpy(dst.home_slug, src.homeSlug, sizeof(dst.home_slug));
    strlcpy(dst.away_slug, src.awaySlug, sizeof(dst.away_slug));
    dst.home_score = src.homeScore;
    dst.away_score = src.awayScore;
    dst.status     = (MatchStatus)src.status;
    dst.minute     = 0;
    dst.round      = src.round;
    strlcpy(dst.group, src.group, sizeof(dst.group));

    // Derive display names from slugs via TeamData lookup
    const TeamEntry* ht = findTeamBySlug(dst.home_slug);
    if (ht) {
        strlcpy(dst.home_name,   ht->canonical, sizeof(dst.home_name));
        strlcpy(dst.home_abbrev, ht->abbrev,    sizeof(dst.home_abbrev));
    } else {
        strlcpy(dst.home_name,   dst.home_slug, sizeof(dst.home_name));
        strlcpy(dst.home_abbrev, dst.home_slug, sizeof(dst.home_abbrev));
    }
    const TeamEntry* at = findTeamBySlug(dst.away_slug);
    if (at) {
        strlcpy(dst.away_name,   at->canonical, sizeof(dst.away_name));
        strlcpy(dst.away_abbrev, at->abbrev,    sizeof(dst.away_abbrev));
    } else {
        strlcpy(dst.away_name,   dst.away_slug, sizeof(dst.away_name));
        strlcpy(dst.away_abbrev, dst.away_slug, sizeof(dst.away_abbrev));
    }
}
