#pragma once
#include <Arduino.h>

class OTAUpdater {
public:
    static void begin();                  // load auto_update pref from NVS
    static bool checkForUpdate();         // fetch version.json from GitHub, return true if newer
    static bool applyUpdate();            // download + flash both partitions, then restart
    static bool isUpdateAvailable();
    static const char* getRemoteVersion();
    static void setAutoUpdate(bool enabled);
    static bool getAutoUpdate();
    static const char* getLastError();

    // Generic stream flasher (used by manual upload or external callers)
    static bool flashFromStream(Stream& stream, size_t expectedSize, int command);

private:
    static bool _flashFromURL(const char* url, size_t expectedSize, int command);
    static bool _parseVersionJson(const char* json);
    static bool _compareVersion(const char* remote);

    static bool   _updateAvailable;
    static bool   _autoUpdate;
    static char   _remoteVersion[32];
    static char   _firmwareURL[160];
    static char   _littlefsURL[160];
    static size_t _firmwareSize;
    static size_t _littlefsSize;
    static char   _lastError[64];
    static bool   _busy;
};
