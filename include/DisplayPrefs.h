#pragma once
#include <Preferences.h>

struct CompPrefs {
    bool enabled   = true;
    bool scores    = true;
    bool fixtures  = true;
    bool standings = true;
};

// comp[0]=Top14, comp[1]=ProD2, comp[2]=CC
struct DisplayPrefs {
    CompPrefs comp[3];
    uint16_t  score_s    = 8;
    uint16_t  fixture_s  = 8;
    uint16_t  standing_s = 20;
};

inline void loadDisplayPrefs(DisplayPrefs& p) {
    Preferences prefs;
    prefs.begin("rugby", true);
    const char* pfx[3] = {"t14_", "pd2_", "cc_"};
    for (int i = 0; i < 3; i++) {
        char k[12];
        snprintf(k, sizeof(k), "%sen", pfx[i]); p.comp[i].enabled   = prefs.getBool(k, true);
        snprintf(k, sizeof(k), "%ssc", pfx[i]); p.comp[i].scores    = prefs.getBool(k, true);
        snprintf(k, sizeof(k), "%sfx", pfx[i]); p.comp[i].fixtures  = prefs.getBool(k, true);
        snprintf(k, sizeof(k), "%sst", pfx[i]); p.comp[i].standings = prefs.getBool(k, true);
    }
    p.score_s    = prefs.getUShort("sc_s", 8);
    p.fixture_s  = prefs.getUShort("fx_s", 8);
    p.standing_s = prefs.getUShort("st_s", 20);
    prefs.end();
}

inline void saveDisplayPrefs(const DisplayPrefs& p) {
    Preferences prefs;
    prefs.begin("rugby", false);
    const char* pfx[3] = {"t14_", "pd2_", "cc_"};
    for (int i = 0; i < 3; i++) {
        char k[12];
        snprintf(k, sizeof(k), "%sen", pfx[i]); prefs.putBool(k, p.comp[i].enabled);
        snprintf(k, sizeof(k), "%ssc", pfx[i]); prefs.putBool(k, p.comp[i].scores);
        snprintf(k, sizeof(k), "%sfx", pfx[i]); prefs.putBool(k, p.comp[i].fixtures);
        snprintf(k, sizeof(k), "%sst", pfx[i]); prefs.putBool(k, p.comp[i].standings);
    }
    prefs.putUShort("sc_s", p.score_s);
    prefs.putUShort("fx_s", p.fixture_s);
    prefs.putUShort("st_s", p.standing_s);
    prefs.end();
}
