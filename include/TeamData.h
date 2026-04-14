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
  {"Perpignan",           "USA Perpignan",                "USA Perpignan",          "Perpignan",         "USA",  "perpignan"},
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
  // Champions Cup (non-French clubs)
  {"Leinster",            "Leinster Rugby",               "",                       "Leinster",          "LEI",  "leinster"},
  {"Munster",             "Munster Rugby",                "",                       "Munster",           "MUN",  "munster"},
  {"Ulster",              "Ulster Rugby",                 "",                       "Ulster",            "ULS",  "ulster"},
  {"Connacht",            "Connacht Rugby",               "",                       "Connacht",          "CON",  "connacht"},
  {"Bath Rugby",          "Bath Rugby",                   "",                       "Bath",              "BAT",  "bath"},
  {"Sale Sharks",         "Sale Sharks",                  "",                       "Sale",              "SAL",  "sale"},
  {"Northampton Saints",  "Northampton Saints",           "",                       "Northampton",       "NOR",  "northampton"},
  {"Gloucester Rugby",    "Gloucester Rugby",             "",                       "Gloucester",        "GLO",  "gloucester"},
  {"Bristol Bears",       "Bristol Rugby",                "",                       "Bristol",           "BRI",  "bristol"},
  {"Harlequins",          "Harlequins",                   "",                       "Harlequins",        "HAR",  "harlequins"},
  {"Exeter Chiefs",       "Exeter Chiefs",                "",                       "Exeter",            "EXE",  "exeter"},
  {"Glasgow Warriors",    "Glasgow Warriors",             "",                       "Glasgow",           "GLA",  "glasgow"},
  {"Edinburgh Rugby",     "Edinburgh Rugby",              "",                       "Edinburgh",         "EDI",  "edinburgh"},
  {"Scarlets",            "Scarlets",                     "",                       "Scarlets",          "SCA",  "scarlets"},
  {"Stormers",            "DHL Stormers",                 "",                       "Stormers",          "STO",  "stormers"},
  {"Bulls",               "Vodacom Bulls",                "",                       "Bulls",             "BUL",  "bulls"},
  {"Lions",               "Emirates Lions",               "",                       "Lions",             "LIO",  "lions"},
  {"Sharks",              "Cell C Sharks",                "",                       "Sharks",            "SHA",  "sharks"},
  {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}  // sentinel
};

// Strip French accents — writes into dst, returns dst
inline const char* stripAccents(const char* src, char* dst, size_t dstLen) {
    static const struct { const char* from; char to; } MAP[] = {
        {"é","e"},{"è","e"},{"ê","e"},{"ë","e"},
        {"à","a"},{"â","a"},{"ä","a"},
        {"î","i"},{"ï","i"},
        {"ô","o"},{"ö","o"},
        {"û","u"},{"ù","u"},{"ü","u"},
        {"ç","c"},
        {"É","E"},{"È","E"},{"Ê","E"},
        {"À","A"},{"Â","A"},
        {"Î","I"},{"Ô","O"},{"Û","U"},{"Ç","C"},
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

// Find team entry by any source name (Idalgo, WorldRugby, or LNR)
inline const TeamEntry* findTeam(const char* name) {
    for (int i = 0; TEAM_TABLE[i].idalgo; i++) {
        if (strcasecmp(name, TEAM_TABLE[i].idalgo)    == 0) return &TEAM_TABLE[i];
        if (strcasecmp(name, TEAM_TABLE[i].worldrugby) == 0) return &TEAM_TABLE[i];
        if (TEAM_TABLE[i].lnr[0] && strcasecmp(name, TEAM_TABLE[i].lnr) == 0)
            return &TEAM_TABLE[i];
    }
    return nullptr;
}
