#include "WebServer.h"
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

    server.begin();
    Serial.printf("Web UI at http://%s\n", WiFi.localIP().toString().c_str());
}
