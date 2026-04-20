#include "WiFiManager.h"
#include "credentials.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Arduino.h>

WiFiManager::Net WiFiManager::nets[MAX_NETS];
int WiFiManager::count = 0;

int WiFiManager::loadNetworks() {
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) {
        Serial.println("WiFiManager: /wifi.json not found");
        count = 0;
        return 0;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        Serial.println("WiFiManager: invalid /wifi.json");
        f.close();
        count = 0;
        return 0;
    }
    f.close();

    count = 0;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        if (count >= MAX_NETS) break;
        strlcpy(nets[count].ssid,     o["ssid"]     | "", sizeof(nets[count].ssid));
        strlcpy(nets[count].password, o["password"] | "", sizeof(nets[count].password));
        if (nets[count].ssid[0]) count++;
    }
    Serial.printf("WiFiManager: loaded %d networks\n", count);
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
    File f = LittleFS.open("/wifi.json", "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
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
