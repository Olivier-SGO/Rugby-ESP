#include "WebUI.h"
#include "DisplayPrefs.h"
#include "WiFiManager.h"
#include "MatchRecord.h"
#include "OTAUpdater.h"
#include "JsonAllocator.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Update.h>
#include <Arduino.h>

extern TaskHandle_t rendererHandle;

static WebServer server(80);
WebUI Web;

void WebUI::begin(MatchDB* db) {
    // Serve static index.html
    server.on("/", HTTP_GET, []() {
        File f = LittleFS.open("/index.html", "r");
        if (!f) {
            server.send(404, "text/plain", "Not found");
            return;
        }
        server.streamFile(f, "text/html");
        f.close();
    });

    // GET /status
    server.on("/status", HTTP_GET, []() {
        JsonDocument doc(&spiRamAlloc);
        doc["wifi"]    = WiFi.SSID();
        doc["ip"]      = WiFi.localIP().toString();
        doc["heap"]    = ESP.getFreeHeap();
        doc["uptime"]  = millis() / 1000;
        doc["ap_mode"] = (WiFi.getMode() & WIFI_AP) != 0;
        if (WiFi.getMode() & WIFI_AP) {
            doc["ap_ip"] = WiFi.softAPIP().toString();
        }
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // GET /scan → list available WiFi networks
    server.on("/scan", HTTP_GET, []() {
        int n = WiFi.scanNetworks();
        JsonDocument doc(&spiRamAlloc);
        auto arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            auto o = arr.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
            o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        }
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // POST /config  body: {"brightness":80}
    server.on("/config", HTTP_POST, []() {
        JsonDocument doc(&spiRamAlloc);
        if (deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok) {
            Preferences prefs;
            prefs.begin("rugby", false);
            if (doc["brightness"].is<int>()) {
                int b = doc["brightness"] | 80;
                Display.setBrightness(b);
                prefs.putInt("brightness", b);
            }
            prefs.end();
        }
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // POST /restart
    server.on("/restart", HTTP_POST, [this]() {
        server.send(200, "text/plain", "Restarting...");
        _restartPending = true;
    });

    // GET /next-scene
    server.on("/next-scene", HTTP_GET, []() {
        Scenes.nextScene();
        server.send(200, "text/plain", "OK");
    });

    // GET /prefs
    server.on("/prefs", HTTP_GET, []() {
        DisplayPrefs p;
        loadDisplayPrefs(p);
        JsonDocument doc(&spiRamAlloc);
        const char* keys[3] = {"top14", "prod2", "cc"};
        for (int i = 0; i < 3; i++) {
            doc[keys[i]]["enabled"]   = p.comp[i].enabled;
            doc[keys[i]]["scores"]    = p.comp[i].scores;
            doc[keys[i]]["fixtures"]  = p.comp[i].fixtures;
            doc[keys[i]]["standings"] = p.comp[i].standings;
        }
        doc["score_s"]    = p.score_s;
        doc["fixture_s"]  = p.fixture_s;
        doc["standing_s"] = p.standing_s;
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // POST /prefs  body: {"top14":{"enabled":true,"scores":true,...}, "score_s":8, ...}
    server.on("/prefs", HTTP_POST, []() {
        JsonDocument doc(&spiRamAlloc);
        if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        DisplayPrefs p;
        loadDisplayPrefs(p);
        const char* keys[3] = {"top14", "prod2", "cc"};
        for (int i = 0; i < 3; i++) {
            if (!doc[keys[i]].isNull()) {
                if (!doc[keys[i]]["enabled"].isNull())   p.comp[i].enabled   = doc[keys[i]]["enabled"].as<bool>();
                if (!doc[keys[i]]["scores"].isNull())    p.comp[i].scores    = doc[keys[i]]["scores"].as<bool>();
                if (!doc[keys[i]]["fixtures"].isNull())  p.comp[i].fixtures  = doc[keys[i]]["fixtures"].as<bool>();
                if (!doc[keys[i]]["standings"].isNull()) p.comp[i].standings = doc[keys[i]]["standings"].as<bool>();
            }
        }
        if (doc["score_s"].is<int>())    p.score_s    = constrain((int)doc["score_s"],    3, 60);
        if (doc["fixture_s"].is<int>())  p.fixture_s  = constrain((int)doc["fixture_s"],  3, 60);
        if (doc["standing_s"].is<int>()) p.standing_s = constrain((int)doc["standing_s"], 5, 120);
        saveDisplayPrefs(p);
        Scenes.requestRebuild();
        Scenes.markDirty();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // GET /wifi  → list of configured networks
    server.on("/wifi", HTTP_GET, []() {
        WiFiManager::loadNetworks();
        JsonDocument doc(&spiRamAlloc);
        auto arr = doc.to<JsonArray>();
        for (int i = 0; i < WiFiManager::count; i++) {
            auto o = arr.add<JsonObject>();
            o["ssid"] = WiFiManager::nets[i].ssid;
        }
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // POST /wifi  body: [{"ssid":"...","password":"..."}, ...]
    server.on("/wifi", HTTP_POST, []() {
        JsonDocument doc(&spiRamAlloc);
        if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        WiFiManager::count = 0;
        for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
            if (WiFiManager::count >= WiFiManager::MAX_NETS) break;
            const char* ssid = o["ssid"] | "";
            const char* pass = o["password"] | "";
            if (ssid[0]) {
                strlcpy(WiFiManager::nets[WiFiManager::count].ssid,     ssid, sizeof(WiFiManager::nets[0].ssid));
                strlcpy(WiFiManager::nets[WiFiManager::count].password, pass, sizeof(WiFiManager::nets[0].password));
                WiFiManager::count++;
            }
        }
        bool ok = WiFiManager::saveNetworks();
        server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
    });

    // GET /dump/cc — export compact binary CC data as JSON
    server.on("/dump/cc", HTTP_GET, []() {
        File f = LittleFS.open("/cc_data.bin", "r");
        if (!f) {
            server.send(404, "application/json", "{\"error\":\"no cc_data.bin\"}");
            return;
        }
        uint32_t magic = 0;
        f.read((uint8_t*)&magic, 4);
        if (magic != 0x52474343) {
            f.close();
            server.send(500, "application/json", "{\"error\":\"bad magic\"}");
            return;
        }
        uint8_t version = f.read();
        uint8_t result_count = f.read();
        uint8_t fixture_count = f.read();
        uint8_t current_round = f.read();

        JsonDocument doc(&spiRamAlloc);
        doc["version"] = version;
        doc["current_round"] = current_round;
        auto results = doc["results"].to<JsonArray>();
        auto fixtures = doc["fixtures"].to<JsonArray>();

        for (int i = 0; i < result_count; i++) {
            CCMatchRecord rec;
            if (!MatchRecord::readRecord(f, rec)) break;
            auto o = results.add<JsonObject>();
            o["home"] = rec.homeSlug;
            o["away"] = rec.awaySlug;
            o["homeScore"] = rec.homeScore;
            o["awayScore"] = rec.awayScore;
            o["status"] = rec.status;
            o["round"] = rec.round;
            o["group"] = rec.group;
            o["kickoff"] = (long)rec.kickoffEpoch;
        }
        for (int i = 0; i < fixture_count; i++) {
            CCMatchRecord rec;
            if (!MatchRecord::readRecord(f, rec)) break;
            auto o = fixtures.add<JsonObject>();
            o["home"] = rec.homeSlug;
            o["away"] = rec.awaySlug;
            o["homeScore"] = rec.homeScore;
            o["awayScore"] = rec.awayScore;
            o["status"] = rec.status;
            o["round"] = rec.round;
            o["group"] = rec.group;
            o["kickoff"] = (long)rec.kickoffEpoch;
        }
        f.close();
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // ── OTA update endpoints ─────────────────────────────────────────────────
    server.on("/update/status", HTTP_GET, []() {
        JsonDocument doc(&spiRamAlloc);
        doc["current"] = FIRMWARE_VERSION;
        doc["available"] = OTAUpdater::isUpdateAvailable();
        doc["remote"] = OTAUpdater::getRemoteVersion();
        doc["auto_update"] = OTAUpdater::getAutoUpdate();
        doc["error"] = OTAUpdater::getLastError();
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/update/check", HTTP_POST, []() {
        // Suspend renderer to free contiguous heap for TLS handshake
        extern TaskHandle_t rendererHandle;
        if (rendererHandle) vTaskSuspend(rendererHandle);
        bool ok = OTAUpdater::checkForUpdate();
        if (rendererHandle) vTaskResume(rendererHandle);
        JsonDocument doc(&spiRamAlloc);
        doc["ok"] = ok;
        doc["available"] = OTAUpdater::isUpdateAvailable();
        doc["remote"] = OTAUpdater::getRemoteVersion();
        doc["error"] = OTAUpdater::getLastError();
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/update/auto", HTTP_POST, []() {
        JsonDocument doc(&spiRamAlloc);
        if (deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok && doc["enabled"].is<bool>()) {
            OTAUpdater::setAutoUpdate(doc["enabled"]);
        }
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/update/apply", HTTP_POST, []() {
        extern TaskHandle_t rendererHandle;
        if (rendererHandle) vTaskSuspend(rendererHandle);
        server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Updating and restarting...\"}");
        delay(500);
        bool ok = OTAUpdater::applyUpdate(); // restarts on success
        if (rendererHandle) vTaskResume(rendererHandle);
        if (!ok) {
            server.send(500, "application/json", "{\"error\":true,\"msg\":\"Update failed\"}");
        }
    });

    // Manual upload helpers
    auto handleUpload = [](int command, const char* label) {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("OTA %s: %s (%u bytes)\n", label, upload.filename.c_str(), upload.totalSize);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
                Serial.printf("Update.begin failed: %s\n", Update.errorString());
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.printf("Update.write failed\n");
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (!Update.end(true)) {
                Serial.printf("Update.end failed: %s\n", Update.errorString());
            } else {
                Serial.printf("OTA %s success\n", label);
            }
        }
    };

    server.on("/update/firmware", HTTP_POST,
        []() { server.send(200, "application/json", Update.hasError() ? "{\"error\":true}" : "{\"ok\":true}"); },
        [&]() { handleUpload(U_FLASH, "firmware"); }
    );

    server.on("/update/littlefs", HTTP_POST,
        []() { server.send(200, "application/json", Update.hasError() ? "{\"error\":true}" : "{\"ok\":true}"); },
        [&]() { handleUpload(U_SPIFFS, "littlefs"); }
    );

    server.begin();
    Serial.printf("Web UI at http://%s\n", WiFi.localIP().toString().c_str());
}

void WebUI::handle() {
    server.handleClient();
}
