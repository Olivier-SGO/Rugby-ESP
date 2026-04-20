#pragma once
#include <cstring>

struct TeamEntry {
    const char* idalgo;       // name as it appears on ladepeche.fr
    const char* worldrugby;   // name as it appears on WorldRugby API
    const char* lnr;          // name as it appears on lnr.fr
    const char* canonical;    // display name (stripped accents)
    const char* abbrev;       // 2-4 char abbreviation
    const char* slug;         // logo filename slug
};

static const TeamEntry TEAM_TABLE[] = {
  // Top 14
  {"Stade Toulousain",    "Stade Toulousain",             "Stade Toulousain",       "Stade Toulousain",  "TLS",  "toulouse"},
  {"Racing 92",           "Racing 92",                    "Racing 92",              "Racing 92",         "R92",  "racing92"},
  {"Bordeaux-Bègles",     "Union Bordeaux Begles",        "UBB",                    "Bordeaux-Begles",   "UBB",  "bordeaux-begles"},
  {"La Rochelle",         "Stade Rochelais",              "La Rochelle",            "La Rochelle",       "SR",   "la-rochelle"},
  {"Clermont",            "ASM Clermont Auvergne",        "Clermont",               "Clermont",          "ASM",  "clermont"},
  {"Toulon",              "RC Toulonnais",                "Toulon",                 "Toulon",            "RCT",  "toulon"},
  {"Montpellier",         "Montpellier Herault Rugby Club","Montpellier Herault",   "Montpellier",       "MHR",  "montpellier"},
  {"Lyon",                "Lyon Olympique Universitaire", "Lyon OU",                "Lyon",              "LOU",  "lyon"},
  {"Stade Français",      "Stade Francais Paris",         "Stade Francais Paris",   "Stade Francais",    "SFP",  "paris"},
  {"Castres",             "Castres Olympique",            "Castres Olympique",      "Castres",           "CO",   "castres"},
  {"Bayonne",             "Aviron Bayonnais",             "Bayonne",                "Bayonne",           "AB",   "bayonne"},
  {"Pau",                 "Section Paloise",              "Section Paloise",        "Pau",               "PAU",  "pau"},
  {"Perpignan",           "USA Perpignan",                "USA Perpignan",          "Perpignan",         "USAP", "perpignan"},
  {"Vannes",              "RC Vannes",                    "RC Vannes",              "Vannes",            "RCV",  "vannes"},
  // Pro D2
  {"Montauban",           "US Montauban",                 "US Montauban",           "Montauban",         "USM",  "montauban"},
  {"Brive",               "CA Brive",                     "Brive",                  "Brive",             "CAB",  "brive"},
  {"Oyonnax",             "Oyonnax Rugby",                "Oyonnax",                "Oyonnax",           "OYO",  "oyonnax"},
  {"Aurillac",            "SC Aurillac",                  "Aurillac",               "Aurillac",          "SCA",  "aurillac"},
  {"Grenoble",            "FC Grenoble",                  "Grenoble",               "Grenoble",          "FCG",  "grenoble"},
  {"Rouen",               "Rouen Normandie Rugby",        "Rouen",                  "Rouen",             "RNR",  "rouen"},
  {"Nevers",              "SC Nevers",                    "Nevers",                 "Nevers",            "SCN",  "nevers"},
  {"Carcassonne",         "AS Carcassonne",               "Carcassonne",            "Carcassonne",       "ASC",  "carcassonne"},
  {"Valence Romans",      "Valence Romans Drome Rugby",   "Valence Romans",         "Valence Romans",    "VRDR", "valence-romans"},
  {"Provence Rugby",      "Provence Rugby",               "Provence Rugby",         "Provence",          "PR",   "provence"},
  {"Angouleme",           "Soyaux Angouleme XV",          "Soyaux Angouleme",       "Angouleme",         "SAXV", "angouleme"},
  {"Narbonne",            "RC Narbonne",                  "Narbonne",               "Narbonne",          "RCN",  "narbonne"},
  {"Beziers",             "AS Beziers",                   "Beziers",                "Beziers",           "ASB",  "beziers"},
  {"Agen",                "SU Agen",                      "Agen",                   "Agen",              "SUA",  "agen"},
  {"Biarritz",            "Biarritz Olympique",           "Biarritz",               "Biarritz",          "BO",   "biarritz"},
  {"Mont-de-Marsan",      "Stade Montois",                "Mont-de-Marsan",         "Mont-de-Marsan",    "SMR",  "mont-de-marsan"},
  {"Mont de Marsan",      "Stade Montois",                "Mont de Marsan",         "Mont-de-Marsan",    "SMR",  "mont-de-marsan"},
  {"Dax",                 "US Dax",                       "Dax",                    "Dax",               "USD",  "dax"},
  {"Colomiers",           "US Colomiers",                 "Colomiers",              "Colomiers",         "USC",  "colomiers"},
  // Champions Cup (non-French clubs)
  {"Leinster",            "Leinster Rugby",               "",                       "Leinster",          "LEI",  "leinster"},
  {"Munster",             "Munster Rugby",                "",                       "Munster",           "MUN",  "munster"},
  {"Ulster",              "Ulster Rugby",                 "",                       "Ulster",            "ULS",  "ulster"},
  {"Connacht",            "Connacht Rugby",               "",                       "Connacht",          "CON",  "connacht"},
  {"Bath",                "Bath Rugby",                   "",                       "Bath",              "BAT",  "bath"},
  {"Sale",                "Sale Sharks",                  "",                       "Sale",              "SAL",  "sale"},
  {"Northampton",         "Northampton Saints",           "",                       "Northampton",       "NOR",  "northampton"},
  {"Gloucester",          "Gloucester Rugby",             "",                       "Gloucester",        "GLO",  "gloucester"},
  {"Bristol",             "Bristol Rugby",                "",                       "Bristol",           "BRI",  "bristol"},
  {"Harlequins",          "Harlequins",                   "",                       "Harlequins",        "HAR",  "harlequins"},
  {"Exeter",              "Exeter Chiefs",                "",                       "Exeter",            "EXE",  "exeter"},
  {"Glasgow",             "Glasgow Warriors",             "",                       "Glasgow",           "GLA",  "glasgow"},
  {"Edinburgh",           "Edinburgh Rugby",              "",                       "Edinburgh",         "EDI",  "edinburgh"},
  {"Scarlets",            "Scarlets",                     "",                       "Scarlets",          "SCL",  "scarlets"},
  {"Stormers",            "DHL Stormers",                 "",                       "Stormers",          "STO",  "stormers"},
  {"Bulls",               "Vodacom Bulls",                "",                       "Bulls",             "BUL",  "bulls"},
  {"Lions",               "Emirates Lions",               "",                       "Lions",             "LIO",  "lions"},
  {"Sharks",              "Cell C Sharks",                "",                       "Sharks",            "SHA",  "sharks"},
  {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}  // sentinel
};

// Strip French accents — writes into dst, returns dst
inline const char* stripAccents(const char* src, char* dst, size_t dstLen) {
    if (!src || !dst || dstLen == 0) { if (dst && dstLen > 0) dst[0] = '\0'; return dst; }  // add this line
    static const struct { const char* from; char to; } MAP[] = {
        {"é",'e'},{"è",'e'},{"ê",'e'},{"ë",'e'},
        {"à",'a'},{"â",'a'},{"ä",'a'},
        {"î",'i'},{"ï",'i'},
        {"ô",'o'},{"ö",'o'},
        {"û",'u'},{"ù",'u'},{"ü",'u'},
        {"ç",'c'},
        {"É",'E'},{"È",'E'},{"Ê",'E'},
        {"À",'A'},{"Â",'A'},
        {"Î",'I'},{"Ô",'O'},{"Û",'U'},{"Ç",'C'},
        {nullptr, 0}
    };
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dstLen) {
        bool replaced = false;
        for (int m = 0; MAP[m].from; m++) {
            size_t flen = strlen(MAP[m].from);
            if (strncmp(src + i, MAP[m].from, flen) == 0) {
                dst[j++] = MAP[m].to;
                i += flen;
                replaced = true;
                break;
            }
        }
        if (!replaced) dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return dst;
}

// Find team entry by any source name (Idalgo, WorldRugby, or LNR).
// Compares both raw and accent-stripped to handle HTML encoding variations.
inline const TeamEntry* findTeam(const char* name) {
    if (!name || !name[0]) return nullptr;
    char stripped[64];
    stripAccents(name, stripped, sizeof(stripped));
    for (int i = 0; TEAM_TABLE[i].idalgo; i++) {
        if (strcasecmp(name, TEAM_TABLE[i].idalgo)    == 0) return &TEAM_TABLE[i];
        if (strcasecmp(name, TEAM_TABLE[i].worldrugby) == 0) return &TEAM_TABLE[i];
        if (TEAM_TABLE[i].lnr[0] && strcasecmp(name, TEAM_TABLE[i].lnr) == 0)
            return &TEAM_TABLE[i];
        // Retry with accent-stripped comparison
        char si[64], sw[64], sl[64];
        stripAccents(TEAM_TABLE[i].idalgo,    si, sizeof(si));
        stripAccents(TEAM_TABLE[i].worldrugby, sw, sizeof(sw));
        if (strcasecmp(stripped, si) == 0) return &TEAM_TABLE[i];
        if (strcasecmp(stripped, sw) == 0) return &TEAM_TABLE[i];
        if (TEAM_TABLE[i].lnr[0]) {
            stripAccents(TEAM_TABLE[i].lnr, sl, sizeof(sl));
            if (strcasecmp(stripped, sl) == 0) return &TEAM_TABLE[i];
        }
    }
    return nullptr;
}
