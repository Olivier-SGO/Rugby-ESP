#pragma once
#include <cstdint>

class WiFiManager {
public:
    // Try to connect to the best known network.
    // Scans, sorts by RSSI, tries each known SSID until one succeeds.
    // Falls back to compile-time credentials.h if /wifi.json is absent.
    static bool connect();

    // Load network list from /wifi.json into internal cache.
    static int loadNetworks();

    // Save current internal cache to /wifi.json.
    static bool saveNetworks();

    // Access Point mode for WiFi configuration
    static bool startAP();
    static void stopAP();
    static bool isAPMode();

    // In-memory network table (max 8 networks)
    struct Net {
        char ssid[32];
        char password[64];
    };
    static const int MAX_NETS = 8;
    static Net nets[MAX_NETS];
    static int count;

private:
    static bool waitForConnect(uint32_t timeoutMs);
};
