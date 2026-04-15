#include "LNRParser.h"
#include "TeamData.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>

bool LNRParser::fetch(const char* url, CompetitionData& out) {
    out.clear();
    HTTPClient http;
    WiFiClientSecure* client = new WiFiClientSecure;
    client->setInsecure();

    http.begin(*client, url);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");

    int code = http.GET();
    if (code != 200) {
        http.end(); delete client; return false;
    }

    // LNR pages are ~80KB — fits in heap if we're careful
    String body = http.getString();
    http.end(); delete client;

    if (body.isEmpty()) return false;

    // Critical fix: replace <template with <div so parser finds the table
    body.replace("<template", "<div");

    // Find ranking rows
    const char* html = body.c_str();
    const char* pos = html;
    uint8_t rank = 1;

    while (rank <= 18) {
        // Find next scrollable row
        const char* row = strstr(pos, "table-line--ranking-scrollable");
        if (!row) break;

        const char* rowEnd = strstr(row + 30, "table-line--ranking");
        if (!rowEnd) rowEnd = row + 2000;

        // Extract cells (first cell = club name)
        StandingEntry entry = {};
        entry.rank = rank;

        char cellText[48] = {};
        const char* cell = row;
        // Cell 0 = club name
        const char* gt = strchr(cell, '>');
        if (gt) {
            // Skip inner tags to get text
            const char* textStart = gt + 1;
            while (*textStart == '<') {
                const char* next = strchr(textStart, '>');
                if (!next) break;
                textStart = next + 1;
            }
            size_t i = 0;
            while (textStart[i] && textStart[i] != '<' && i + 1 < sizeof(cellText))
                cellText[i] = textStart[i++];
            cellText[i] = '\0';
        }

        // Points: find second cell div
        char pts[8] = {};
        const char* ptsPos = strstr(row + 30, "<div");
        if (ptsPos) {
            const char* ptsGt = strchr(ptsPos, '>');
            if (ptsGt) {
                size_t i = 0;
                ptsGt++;
                while (ptsGt[i] && ptsGt[i] != '<' && ptsGt[i] != ' ' && i + 1 < sizeof(pts))
                    pts[i] = ptsGt[i++];
                pts[i] = '\0';
            }
        }

        if (cellText[0]) {
            const TeamEntry* t = findTeam(cellText);
            if (t) {
                strlcpy(entry.name,   t->canonical, sizeof(entry.name));
                strlcpy(entry.abbrev, t->abbrev,    sizeof(entry.abbrev));
                strlcpy(entry.slug,   t->slug,      sizeof(entry.slug));
            } else {
                char stripped[48];
                stripAccents(cellText, stripped, sizeof(stripped));
                strlcpy(entry.name, stripped, sizeof(entry.name));
            }
            entry.points = atoi(pts);
            out.standings[out.standing_count++] = entry;
        }

        pos = rowEnd;
        rank++;
    }

    return out.standing_count > 0;
}
