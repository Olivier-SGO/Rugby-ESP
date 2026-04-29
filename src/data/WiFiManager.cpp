#include "WiFiManager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>

WiFiManager::Net WiFiManager::nets[MAX_NETS];
int WiFiManager::count = 0;

int WiFiManager::loadNetworks() {
    Preferences prefs;
    // Open RW first to auto-create namespace if missing, then read
    if (!prefs.begin("wifi", false)) {
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

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    if (count == 0) {
        Serial.println("WiFiManager: no saved networks");
        return false;
    }

    // Fast path: try reconnecting to the first saved network directly
    // without scanning. This avoids scan interference with background
    // auto-reconnect and is much faster when the network is reachable.
    Serial.printf("WiFi: trying %s (fast reconnect)\n", nets[0].ssid);
    delay(200);
    WiFi.begin(nets[0].ssid, nets[0].password);
    if (waitForConnect(15000)) {
        Serial.printf("WiFi: connected to %s\n", nets[0].ssid);
        return true;
    }
    Serial.printf("WiFi: %s fast reconnect failed, falling back to scan\n", nets[0].ssid);

    // Fallback: scan and try all saved networks
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
                delay(200);
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

bool WiFiManager::startAP() {
    WiFi.mode(WIFI_AP);
    delay(200);
    // Channel 6, max 4 clients, WPA2 password
    bool ok = WiFi.softAP("RugbyDisplay-Setup", "rugby2024", 6, 0, 4);
    if (ok) {
        Serial.printf("AP started: %s, IP: %s, password: rugby2024\n",
                      WiFi.softAPSSID().c_str(),
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("AP start failed");
    }
    return ok;
}

void WiFiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    delay(100);
    Serial.println("AP stopped");
}

bool WiFiManager::isAPMode() {
    return WiFi.getMode() & WIFI_AP;
}
