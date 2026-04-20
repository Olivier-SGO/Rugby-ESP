#include "LogoCache.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

struct LogoEntry {
    char slug[24];
    bool small;
    const uint16_t* data;
};

static LogoEntry* _entries = nullptr;
static int _count = 0;

bool initLogoCache() {
    File dir = LittleFS.open("/logos");
    if (!dir || !dir.isDirectory()) {
        Serial.println("LogoCache: /logos not found");
        return false;
    }

    // Count .bin files
    int n = 0;
    File f = dir.openNextFile();
    while (f) {
        const char* name = f.name();
        if (strstr(name, ".bin")) n++;
        f = dir.openNextFile();
    }
    dir.rewindDirectory();

    if (n == 0) {
        Serial.println("LogoCache: no .bin files");
        return false;
    }

    _entries = (LogoEntry*)heap_caps_malloc(n * sizeof(LogoEntry), MALLOC_CAP_SPIRAM);
    if (!_entries) {
        Serial.println("LogoCache: PSRAM alloc failed for entries");
        return false;
    }

    size_t totalBytes = 0;
    f = dir.openNextFile();
    while (f && _count < n) {
        const char* name = f.name();
        if (!strstr(name, ".bin")) { f = dir.openNextFile(); continue; }

        size_t sz = f.size();
        uint16_t* buf = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!buf) {
            Serial.printf("LogoCache: PSRAM OOM for %s (%u bytes)\n", name, (unsigned)sz);
            f = dir.openNextFile();
            continue;
        }
        f.read((uint8_t*)buf, sz);
        totalBytes += sz;

        // Parse slug & size from filename basename, e.g. "toulouse.bin" or "toulouse_sm.bin"
        const char* base = strrchr(name, '/');
        if (base) base++; else base = name;
        char slug[24] = {};
        bool small = false;
        const char* sm = strstr(base, "_sm.bin");
        const char* dot = strstr(base, ".bin");
        if (sm) {
            size_t len = min((size_t)(sm - base), sizeof(slug) - 1);
            memcpy(slug, base, len);
            small = true;
        } else if (dot) {
            size_t len = min((size_t)(dot - base), sizeof(slug) - 1);
            memcpy(slug, base, len);
        }

        strlcpy(_entries[_count].slug, slug, sizeof(_entries[_count].slug));
        _entries[_count].small = small;
        _entries[_count].data = buf;
        _count++;

        f = dir.openNextFile();
    }

    Serial.printf("LogoCache: %d/%d logos loaded into PSRAM (%u bytes)\n",
                  _count, n, (unsigned)totalBytes);
    return _count > 0;
}

const uint16_t* getLogo(const char* slug, bool small) {
    if (!slug || !slug[0] || !_entries) return nullptr;
    for (int i = 0; i < _count; i++) {
        if (_entries[i].small == small && strcmp(_entries[i].slug, slug) == 0)
            return _entries[i].data;
    }
    return nullptr;
}
