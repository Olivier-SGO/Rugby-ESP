#pragma once
#include "MatchData.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class MatchDB {
public:
    void begin();

    // Thread-safe writers (called from Core 0)
    void updateTop14(const CompetitionData& d);
    void updateProd2(const CompetitionData& d);
    void updateCC(const CompetitionData& d);

    // Thread-safe readers (called from Core 1) — caller must call release() after use
    const CompetitionData* acquireTop14();
    const CompetitionData* acquireProd2();
    const CompetitionData* acquireCC();
    void release();

    bool    hasLive() const;
    uint8_t liveMask() const;  // bit0=top14, bit1=prod2, bit2=cc
    void persist();
    void load();

private:
    CompetitionData _top14, _prod2, _cc;
    SemaphoreHandle_t _mutex = nullptr;
};

extern MatchDB DB;
