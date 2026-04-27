#include "OTAUpdater.h"
#include "config.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>

bool   OTAUpdater::_updateAvailable = false;
bool   OTAUpdater::_autoUpdate      = false;
char   OTAUpdater::_remoteVersion[32] = {0};
char   OTAUpdater::_firmwareURL[160]  = {0};
char   OTAUpdater::_littlefsURL[160]  = {0};
size_t OTAUpdater::_firmwareSize      = 0;
size_t OTAUpdater::_littlefsSize      = 0;
char   OTAUpdater::_lastError[64]     = {0};

static constexpr const char* VERSION_URL =
    "https://github.com/Olivier-SGO/Rugby-ESP/releases/latest/download/version.json";

// ── Init ─────────────────────────────────────────────────────────────────────
void OTAUpdater::begin() {
    Preferences prefs;
    prefs.begin("rugby", true);
    _autoUpdate = prefs.getBool("auto_update", false);
    prefs.end();
}

// ── Check remote version ─────────────────────────────────────────────────────
bool OTAUpdater::checkForUpdate() {
    _updateAvailable = false;
    _remoteVersion[0] = '\0';
    _firmwareURL[0]   = '\0';
    _littlefsURL[0]   = '\0';
    _firmwareSize = 0;
    _littlefsSize = 0;
    _lastError[0] = '\0';

    if (WiFi.status() != WL_CONNECTED) {
        strlcpy(_lastError, "WiFi not connected", sizeof(_lastError));
        return false;
    }
    if (ESP.getMaxAllocHeap() < 45000) {
        strlcpy(_lastError, "Heap too low for TLS", sizeof(_lastError));
        Serial.printf("[OTA] heap too low (max=%u)\n", ESP.getMaxAllocHeap());
        return false;
    }
    HTTPClient http;
    http.setTimeout(30000);
    http.begin(String(VERSION_URL));
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible; RugbyESP32/1.0)");
    const char* locKey = "Location";
    http.collectHeaders(&locKey, 1);
    Serial.printf("[OTA] Checking GitHub for update... heap=%u max=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    int code = http.GET();

    // Manual redirect: end() destroys the TLS client, begin() creates a fresh one
    int redirects = 0;
    while ((code == 301 || code == 302 || code == 307 || code == 308) && redirects < 3) {
        String location = http.header("Location");
        Serial.printf("[OTA] Redirect %d -> %s\n", redirects + 1, location.c_str());
        http.end();
        http.begin(location);
        http.addHeader("User-Agent", "Mozilla/5.0 (compatible; RugbyESP32/1.0)");
        http.collectHeaders(&locKey, 1);
        code = http.GET();
        redirects++;
    }

    if (code != 200) {
        snprintf(_lastError, sizeof(_lastError), "HTTP %d", code);
        Serial.printf("[OTA] version.json failed: HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    if (!_parseVersionJson(body.c_str())) {
        strlcpy(_lastError, "JSON parse error", sizeof(_lastError));
        return false;
    }

    if (_compareVersion(_remoteVersion)) {
        _updateAvailable = true;
        Serial.printf("[OTA] Update available: %s (current %s)\n", _remoteVersion, FIRMWARE_VERSION);
        return true;
    }

    Serial.printf("[OTA] Up to date: %s\n", FIRMWARE_VERSION);
    return false;
}

// ── Download + flash both partitions ─────────────────────────────────────────
bool OTAUpdater::applyUpdate() {
    _lastError[0] = '\0';
    if (!_updateAvailable) {
        strlcpy(_lastError, "No update available", sizeof(_lastError));
        return false;
    }
    if (!_firmwareURL[0] || !_firmwareSize) {
        strlcpy(_lastError, "Missing firmware info", sizeof(_lastError));
        return false;
    }

    Serial.println("[OTA] Flashing firmware...");
    if (!_flashFromURL(_firmwareURL, _firmwareSize, U_FLASH)) {
        return false;
    }

    // Brief pause between firmware and filesystem flash to let Update subsystem settle
    delay(500);

    if (_littlefsURL[0] && _littlefsSize) {
        Serial.println("[OTA] Flashing LittleFS...");
        if (!_flashFromURL(_littlefsURL, _littlefsSize, U_SPIFFS)) {
            // Firmware was flashed OK but filesystem failed. Reboot anyway —
            // old filesystem will work with the new firmware.
            Serial.printf("[OTA] FS error: %s\n", _lastError);
        }
    }

    Serial.println("[OTA] Restarting...");
    ESP.restart();
    return true; // never reached
}

// ── Stream a remote binary into the Update subsystem ─────────────────────────
bool OTAUpdater::_flashFromURL(const char* url, size_t expectedSize, int command) {
    HTTPClient http;
    http.setTimeout(30000);
    http.begin(String(url));
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible; RugbyESP32/1.0)");
    const char* locKey = "Location";
    http.collectHeaders(&locKey, 1);
    int code = http.GET();

    int redirects = 0;
    while ((code == 301 || code == 302 || code == 307 || code == 308) && redirects < 3) {
        String location = http.header("Location");
        Serial.printf("[OTA] Redirect %d -> %s\n", redirects + 1, location.c_str());
        http.end();
        http.begin(location);
        http.addHeader("User-Agent", "Mozilla/5.0 (compatible; RugbyESP32/1.0)");
        http.collectHeaders(&locKey, 1);
        code = http.GET();
        redirects++;
    }

    if (code != 200) {
        snprintf(_lastError, sizeof(_lastError), "HTTP %d on %s",
                 code, command == U_FLASH ? "firmware" : "littlefs");
        Serial.printf("[OTA] %s\n", _lastError);
        http.end();
        Update.abort();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen > 0 && (size_t)contentLen != expectedSize) {
        Serial.printf("[OTA] Warn: content-length %d != expected %zu\n", contentLen, expectedSize);
    }

    WiFiClient* stream = http.getStreamPtr();
    Serial.printf("[OTA] Update.begin(%s) heap=%u\n",
                  command == U_FLASH ? "FW" : "FS", ESP.getFreeHeap());
    if (!Update.begin(expectedSize, command)) {
        snprintf(_lastError, sizeof(_lastError), "Update.begin failed: %s", Update.errorString());
        Serial.printf("[OTA] %s\n", _lastError);
        http.end();
        return false;
    }

    uint8_t buf[4096];
    size_t written = 0;
    uint32_t lastData = millis();
    uint32_t lastPrint = millis();
    while (written < expectedSize) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - lastData > 30000) {
                Serial.printf("[OTA] Read timeout at %zu / %zu bytes\n", written, expectedSize);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        size_t toRead = min(avail, sizeof(buf));
        size_t n = stream->readBytes(buf, toRead);
        if (n > 0) {
            size_t w = Update.write(buf, n);
            if (w != n) {
                snprintf(_lastError, sizeof(_lastError), "Update.write failed at %zu", written);
                Update.abort();
                http.end();
                return false;
            }
            written += n;
            lastData = millis();
            if (millis() - lastPrint > 5000) {
                Serial.printf("[OTA] %s: %zu / %zu bytes\n",
                              command == U_FLASH ? "FW" : "FS", written, expectedSize);
                lastPrint = millis();
            }
        }
    }

    if (written != expectedSize) {
        snprintf(_lastError, sizeof(_lastError), "Incomplete download: %zu / %zu", written, expectedSize);
        Serial.printf("[OTA] %s\n", _lastError);
        Update.abort();
        http.end();
        return false;
    }

    if (!Update.end()) {
        snprintf(_lastError, sizeof(_lastError), "Update.end failed: %s", Update.errorString());
        Serial.printf("[OTA] %s\n", _lastError);
        http.end();
        return false;
    }

    http.end();
    Serial.printf("[OTA] %s flashed OK: %zu bytes\n",
                  command == U_FLASH ? "Firmware" : "LittleFS", written);
    return true;
}

// ── Generic stream flasher (manual upload) ───────────────────────────────────
bool OTAUpdater::flashFromStream(Stream& stream, size_t expectedSize, int command) {
    _lastError[0] = '\0';
    if (!Update.begin(expectedSize, command)) {
        snprintf(_lastError, sizeof(_lastError), "Update.begin failed: %s", Update.errorString());
        return false;
    }

    uint8_t buf[1024];
    size_t written = 0;
    while (written < expectedSize) {
        size_t avail = stream.available();
        if (!avail) {
            delay(1);
            continue;
        }
        size_t toRead = min(avail, sizeof(buf));
        size_t n = stream.readBytes(buf, toRead);
        if (n > 0) {
            size_t w = Update.write(buf, n);
            if (w != n) {
                snprintf(_lastError, sizeof(_lastError), "Update.write failed at %zu", written);
                Update.abort();
                return false;
            }
            written += n;
        }
    }

    if (!Update.end()) {
        snprintf(_lastError, sizeof(_lastError), "Update.end failed: %s", Update.errorString());
        return false;
    }
    return true;
}

// ── Parse version.json ───────────────────────────────────────────────────────
bool OTAUpdater::_parseVersionJson(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    const char* v = doc["version"];
    if (!v || !v[0]) return false;
    strlcpy(_remoteVersion, v, sizeof(_remoteVersion));

    const char* fwu = doc["firmware_url"];
    if (fwu) strlcpy(_firmwareURL, fwu, sizeof(_firmwareURL));

    const char* fsu = doc["littlefs_url"];
    if (fsu) strlcpy(_littlefsURL, fsu, sizeof(_littlefsURL));

    _firmwareSize = doc["firmware_size"] | 0;
    _littlefsSize = doc["littlefs_size"] | 0;

    return true;
}

// ── Version comparison ───────────────────────────────────────────────────────
bool OTAUpdater::_compareVersion(const char* remote) {
    // Exact string compare — works as long as FIRMWARE_VERSION is unique per release.
    return strcmp(remote, FIRMWARE_VERSION) != 0;
}

// ── Getters / setters ────────────────────────────────────────────────────────
bool OTAUpdater::isUpdateAvailable() { return _updateAvailable; }
const char* OTAUpdater::getRemoteVersion() { return _remoteVersion; }
const char* OTAUpdater::getLastError() { return _lastError; }

void OTAUpdater::setAutoUpdate(bool enabled) {
    _autoUpdate = enabled;
    Preferences prefs;
    prefs.begin("rugby", false);
    prefs.putBool("auto_update", enabled);
    prefs.end();
}

bool OTAUpdater::getAutoUpdate() { return _autoUpdate; }
