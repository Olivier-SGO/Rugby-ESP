#include "WebServer.h"
#include "DisplayPrefs.h"
#include "WiFiManager.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>

static AsyncWebServer server(80);
WebUI Web;

void WebUI::begin(MatchDB* db) {
    // Serve static files from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // GET /status
    server.on("/status", HTTP_GET, [db](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["wifi"]   = WiFi.SSID();
        doc["ip"]     = WiFi.localIP().toString();
        doc["heap"]   = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /config  body: {"brightness":80}
    server.on("/config", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
              [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) == DeserializationError::Ok) {
            Preferences prefs;
            prefs.begin("rugby", false);
            if (doc["brightness"].is<int>()) {
                int b = doc["brightness"] | 80;
                Display.setBrightness(b);
                prefs.putInt("brightness", b);
            }
            prefs.end();
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /restart
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Restarting...");
        delay(500);
        ESP.restart();
    });

    // GET /next-scene
    server.on("/next-scene", HTTP_GET, [](AsyncWebServerRequest* req) {
        Scenes.nextScene();
        req->send(200, "text/plain", "OK");
    });

    // GET /prefs
    server.on("/prefs", HTTP_GET, [](AsyncWebServerRequest* req) {
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
        req->send(200, "application/json", out);
    });

    // POST /prefs  body: {"top14":{"enabled":true,"scores":true,...}, "score_s":8, ...}
    server.on("/prefs", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
              [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
            req->send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        DisplayPrefs p;
        loadDisplayPrefs(p); // start from current values
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
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // GET /wifi  → list of configured networks
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        WiFiManager::loadNetworks();
        JsonDocument doc;
        auto arr = doc.to<JsonArray>();
        for (int i = 0; i < WiFiManager::count; i++) {
            auto o = arr.add<JsonObject>();
            o["ssid"] = WiFiManager::nets[i].ssid;
            // never send passwords back to client
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /wifi  body: [{"ssid":"...","password":"..."}, ...]
    server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
              [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
            req->send(400, "application/json", "{\"error\":\"invalid json\"}");
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
        req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
    });

    server.begin();
    Serial.printf("Web UI at http://%s\n", WiFi.localIP().toString().c_str());
}
