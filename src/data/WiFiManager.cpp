#include "WiFiManager.h"
#include "credentials.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>

WiFiManager::Net WiFiManager::nets[MAX_NETS];
int WiFiManager::count = 0;

int WiFiManager::loadNetworks() {
    Preferences prefs;
    if (!prefs.begin("wifi", true)) {
        Serial.println("WiFiManager: Preferences init failed");
        count = 0;
        return 0;
    }
    String json = prefs.getString("nets", "");
    prefs.end();

    if (json.length() == 0) {
        Serial.println("WiFiManager: no saved networks in Preferences");
        count = 0;
        return 0;
    }

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("WiFiManager: invalid JSON in Preferences");
        count = 0;
        return 0;
    }

    count = 0;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        if (count >= MAX_NETS) break;
        strlcpy(nets[count].ssid,     o["ssid"]     | "", sizeof(nets[count].ssid));
        strlcpy(nets[count].password, o["password"] | "", sizeof(nets[count].password));
        if (nets[count].ssid[0]) count++;
    }
    Serial.printf("WiFiManager: loaded %d networks from Preferences\n", count);
    return count;
}

bool WiFiManager::saveNetworks() {
    JsonDocument doc;
    auto arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        auto o = arr.add<JsonObject>();
        o["ssid"]     = nets[i].ssid;
        o["password"] = nets[i].password;
    }
    String json;
    serializeJson(doc, json);

    Preferences prefs;
    if (!prefs.begin("wifi", false)) return false;
    bool ok = prefs.putString("nets", json);
    prefs.end();
    return ok;
}

bool WiFiManager::waitForConnect(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::connect() {
    if (WiFi.status() == WL_CONNECTED) return true;

    loadNetworks();

    if (count == 0) {
        Serial.println("WiFiManager: no saved networks, using compile-time credentials");
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        bool ok = waitForConnect(30000);
        Serial.println(ok ? "WiFi: connected (fallback)" : "WiFi: fallback failed");
        return ok;
    }

    Serial.println("WiFiManager: scanning...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("WiFiManager: no networks found");
        return false;
    }
    Serial.printf("WiFiManager: %d networks found\n", n);

    // Collect RSSI and sort descending (bubble sort, small n)
    struct { int idx; int rssi; } order[16];
    int scanned = min(n, 16);
    for (int i = 0; i < scanned; i++) {
        order[i].idx  = i;
        order[i].rssi = WiFi.RSSI(i);
    }
    for (int i = 0; i < scanned - 1; i++) {
        for (int j = i + 1; j < scanned; j++) {
            if (order[j].rssi > order[i].rssi) {
                auto t = order[i]; order[i] = order[j]; order[j] = t;
            }
        }
    }

    for (int i = 0; i < scanned; i++) {
        const char* scannedSsid = WiFi.SSID(order[i].idx).c_str();
        int rssi = order[i].rssi;
        for (int j = 0; j < count; j++) {
            if (strcmp(scannedSsid, nets[j].ssid) == 0) {
                Serial.printf("WiFi: trying %s (%d dBm)\n", nets[j].ssid, rssi);
                WiFi.mode(WIFI_STA);
                WiFi.begin(nets[j].ssid, nets[j].password);
                if (waitForConnect(20000)) {
                    Serial.printf("WiFi: connected to %s\n", nets[j].ssid);
                    return true;
                }
                Serial.printf("WiFi: %s failed\n", nets[j].ssid);
            }
        }
    }

    Serial.println("WiFiManager: no known network reachable");
    return false;
}
