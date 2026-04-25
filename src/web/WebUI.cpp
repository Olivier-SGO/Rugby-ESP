#include "WebUI.h"
#include "DisplayPrefs.h"
#include "WiFiManager.h"
#include "MatchRecord.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>

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
        JsonDocument doc;
        doc["wifi"]   = WiFi.SSID();
        doc["ip"]     = WiFi.localIP().toString();
        doc["heap"]   = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // POST /config  body: {"brightness":80}
    server.on("/config", HTTP_POST, []() {
        JsonDocument doc;
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
    server.on("/restart", HTTP_POST, []() {
        server.send(200, "text/plain", "Restarting...");
        delay(500);
        ESP.restart();
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
        JsonDocument doc;
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
        JsonDocument doc;
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
        JsonDocument doc;
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
        JsonDocument doc;
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

        JsonDocument doc;
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

    server.begin();
    Serial.printf("Web UI at http://%s\n", WiFi.localIP().toString().c_str());
}

void WebUI::handle() {
    server.handleClient();
}
