#pragma once
#include <cstdint>
#include <FS.h>
#include "MatchData.h"

// ── Compact binary record for Champions Cup matches ──────────────────────────
// Replaces verbose JSON with a fixed-size binary format.
// 51 bytes per match → 12 matches = 612 bytes (vs ~8 KB JSON).

struct __attribute__((packed)) CCMatchRecord {
    uint32_t kickoffEpoch;   // epoch UTC, 0 = TBD
    char     homeSlug[16];   // canonical slug, zero-padded
    char     awaySlug[16];
    int16_t  homeScore;      // -1 = not played
    int16_t  awayScore;
    uint8_t  status;         // 0=Scheduled, 1=Live, 2=Finished
    uint8_t  round;
    char     group[8];       // pool / phase (e.g. "P1", "1/4F")
    uint8_t  crc8;           // checksum over preceding bytes
};

static_assert(sizeof(CCMatchRecord) == 51, "CCMatchRecord size mismatch");

namespace MatchRecord {
    // CRC-8 (Dallas/Maxim) — simple & fast
    uint8_t crc8(const uint8_t* data, size_t len);

    // Convert full MatchData to compact record
    void fromMatchData(const MatchData& src, CCMatchRecord& dst);

    // Restore MatchData from compact record
    void toMatchData(const CCMatchRecord& src, MatchData& dst);

    // Helpers
    inline bool writeRecord(File& f, const CCMatchRecord& rec) {
        return f.write((const uint8_t*)&rec, sizeof(rec)) == sizeof(rec);
    }
    inline bool readRecord(File& f, CCMatchRecord& rec) {
        return f.read((uint8_t*)&rec, sizeof(rec)) == sizeof(rec);
    }
}
