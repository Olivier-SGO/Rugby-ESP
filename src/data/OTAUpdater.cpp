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

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) {
        strlcpy(_lastError, "Out of memory", sizeof(_lastError));
        return false;
    }
    client->setInsecure();

    HTTPClient http;
    http.setTimeout(15000);
    http.begin(*client, VERSION_URL);
    int code = http.GET();
    if (code != 200) {
        snprintf(_lastError, sizeof(_lastError), "HTTP %d", code);
        http.end();
        delete client;
        return false;
    }

    String body = http.getString();
    http.end();
    delete client;

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

    if (_littlefsURL[0] && _littlefsSize) {
        Serial.println("[OTA] Flashing LittleFS...");
        if (!_flashFromURL(_littlefsURL, _littlefsSize, U_SPIFFS)) {
            // Firmware was flashed OK but filesystem failed. Reboot anyway —
            // old filesystem will work with the new firmware.
            strlcpy(_lastError, "FS flash failed, rebooting with new FW", sizeof(_lastError));
            Serial.println(_lastError);
        }
    }

    Serial.println("[OTA] Restarting...");
    ESP.restart();
    return true; // never reached
}

// ── Stream a remote binary into the Update subsystem ─────────────────────────
bool OTAUpdater::_flashFromURL(const char* url, size_t expectedSize, int command) {
    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) {
        strlcpy(_lastError, "Out of memory (client)", sizeof(_lastError));
        return false;
    }
    client->setInsecure();

    HTTPClient http;
    http.setTimeout(30000);
    http.begin(*client, url);
    int code = http.GET();
    if (code != 200) {
        snprintf(_lastError, sizeof(_lastError), "HTTP %d on %s",
                 code, command == U_FLASH ? "firmware" : "littlefs");
        http.end();
        delete client;
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen > 0 && (size_t)contentLen != expectedSize) {
        Serial.printf("[OTA] Warn: content-length %d != expected %zu\n", contentLen, expectedSize);
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!Update.begin(expectedSize, command)) {
        snprintf(_lastError, sizeof(_lastError), "Update.begin failed: %s", Update.errorString());
        http.end();
        delete client;
        return false;
    }

    uint8_t buf[1024];
    size_t written = 0;
    unsigned long lastPrint = millis();
    while (http.connected() || stream->available()) {
        size_t avail = stream->available();
        if (!avail) {
            delay(1);
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
                delete client;
                return false;
            }
            written += n;
            if (millis() - lastPrint > 5000) {
                Serial.printf("[OTA] %s: %zu / %zu bytes\n",
                              command == U_FLASH ? "FW" : "FS", written, expectedSize);
                lastPrint = millis();
            }
        }
    }

    if (!Update.end()) {
        snprintf(_lastError, sizeof(_lastError), "Update.end failed: %s", Update.errorString());
        http.end();
        delete client;
        return false;
    }

    http.end();
    delete client;
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
