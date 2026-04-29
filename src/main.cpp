// Rugby ESP32 Display — Firmware v1.0.0-functional (2026-04-24)
// See config.h for validated feature list

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <DNSServer.h>

#include "config.h"
#include "DisplayManager.h"
#include "SceneManager.h"
#include "MatchDB.h"
#include "DataFetcher.h"
#include "WiFiManager.h"
#include "WebUI.h"
#include "LogoCache.h"
#include "OTAUpdater.h"
#include "fonts/AtkinsonHyperlegible8pt7b.h"
#include "fonts/AtkinsonHyperlegibleBold12pt7b.h"

// MatrixPortal S3 onboard NeoPixel
#define NEO_PIN   4
#define NEO_COUNT 1
static Adafruit_NeoPixel neo(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

static void neoSet(uint8_t r, uint8_t g, uint8_t b) {
    neo.setPixelColor(0, neo.Color(r, g, b));
    neo.show();
}

// ── Hardware color test ───────────────────────────────────────────────────────
static void hwTest() {
    struct { uint16_t color; const char* label; } steps[] = {
        { 0xF800, "RED   " },
        { 0x07E0, "GREEN " },
        { 0x001F, "BLUE  " },
        { 0xFFFF, "WHITE " },
    };
    for (auto& s : steps) {
        Serial.printf("HW TEST: %s\n", s.label);
        Display.fillScreen(s.color);
        Display.flip();
        delay(500);
    }
    Display.fillScreen(C_BLACK);
    Display.drawText(4, 16, "HUB75 OK  256x64", 0xFFFF, nullptr);
    Display.drawText(4, 32, "FW v" FIRMWARE_VERSION, 0x07E0, nullptr);
    Display.drawText(4, 48, "R  G  B  W  OK",   0xFFFF, nullptr);
    Display.flip();
    delay(1500);
}

TaskHandle_t rendererHandle = nullptr;

// Global TLS heap reserve — allocated early when heap is contiguous,
// released before each fetch session to create a clean block for TLS handshakes.
uint8_t* gTLSReserve = nullptr;

// ── Boot info screen (IP + mDNS) ─────────────────────────────────────────────
static void showBootInfo() {
    if (!rendererHandle) return;
    vTaskSuspend(rendererHandle);

    String ip = WiFi.localIP().toString();
    const char* host = "rugby-display.local";

    Display.fillScreen(C_BLACK);
    const GFXfont* f8  = (const GFXfont*)&AtkinsonHyperlegible8pt7b;
    const GFXfont* f12 = (const GFXfont*)&AtkinsonHyperlegibleBold12pt7b;

    int16_t x1, y1; uint16_t tw, th;

    Display.getTextBounds("Rugby Display", 0, 0, &x1, &y1, &tw, &th, f12);
    Display.drawTextRelief(CENTER_MID - tw/2, 14, "Rugby Display", C_WHITE, f12);

    Display.getTextBounds(host, 0, 0, &x1, &y1, &tw, &th, f8);
    Display.drawText(CENTER_MID - tw/2, 34, host, C_GOLD, f8);

    char ipBuf[40];
    snprintf(ipBuf, sizeof(ipBuf), "IP: %s", ip.c_str());
    Display.getTextBounds(ipBuf, 0, 0, &x1, &y1, &tw, &th, f8);
    Display.drawText(CENTER_MID - tw/2, 50, ipBuf, C_GOLD, f8);

    Display.flip();
    delay(5000);

    vTaskResume(rendererHandle);
    Scenes.markDirty();
}

// ── Renderer task (Core 1) ────────────────────────────────────────────────────
static void renderTask(void*) {
    const TickType_t frameDelay = pdMS_TO_TICKS(33); // ~30fps
    static uint32_t lastStackLog = 0;
    for (;;) {
        Scenes.tick();
        vTaskDelay(frameDelay);
        esp_task_wdt_reset();
        if (millis() - lastStackLog > 30000) {
            lastStackLog = millis();
            UBaseType_t hw = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[STACK] Renderer high-water: %u bytes\n", hw);
        }
    }
}

// ── Boot fetch task (Core 0, 16KB stack — avoids setup() stack overflow) ─────
static volatile bool s_bootFetchDone = false;
static void bootFetchTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // laisser le WiFi s'initialiser
    if (rendererHandle) vTaskSuspend(rendererHandle); // stop render allocs during fetch
    Fetcher.connectWiFi();
    if (Fetcher.isWiFiConnected()) Fetcher.syncNTP();

    Fetcher.fetchAll();

    // OTA auto-check AFTER Idalgo fetches — match data is already cached
    // Release TLS reserve so the GitHub handshake has a clean contiguous block.
    // fetchAll() reclaimed it; we free it again for OTA and reclaim afterward.
    if (gTLSReserve) {
        free(gTLSReserve);
        gTLSReserve = nullptr;
        Serial.println("[HEAP] TLS reserve released for OTA check");
    }
    if (rendererHandle) vTaskSuspend(rendererHandle);
    OTAUpdater::begin();
    if (OTAUpdater::getAutoUpdate() && WiFi.status() == WL_CONNECTED) {
        if (OTAUpdater::checkForUpdate()) {
            OTAUpdater::applyUpdate(); // restarts on success, never returns
        }
    }
    if (rendererHandle) vTaskResume(rendererHandle);
    gTLSReserve = (uint8_t*)malloc(49152);
    Serial.println(gTLSReserve ? "[HEAP] TLS reserve reclaimed after OTA" : "[HEAP] WARNING: TLS reserve reclaim failed after OTA");

    UBaseType_t hw = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[STACK] BootFetch high-water: %u bytes\n", hw);

    s_bootFetchDone = true;
    vTaskDelete(nullptr);
}

// ── Arduino setup ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    delay(200);
    Serial.println("\n=== Rugby ESP32 Display ===");
    Serial.printf("FW version: %s\n", FIRMWARE_VERSION);

    neo.begin();
    neoSet(0, 0, 64); // blue = booting

    // Reserve a contiguous SRAM block for TLS handshakes before the heap gets fragmented.
    // Despite max_frag_len=4096, mbedTLS in this SDK uses MAX_CONTENT_LEN=16384 per buffer
    // (input + output = 32KB total) PLUS session/handshake structures (~10-15KB).
    // We need ~48KB contiguous for a reliable handshake.
    gTLSReserve = (uint8_t*)malloc(49152);
    if (gTLSReserve) {
        Serial.println("[HEAP] 48KB reserved for TLS (pre-fragmentation)");
    } else {
        Serial.println("[HEAP] WARNING: failed to reserve 48KB TLS block");
    }

    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed (run uploadfs)");
    } else {
        if (LittleFS.exists("/logos/toulouse.bin")) {
            Serial.println("LittleFS: logos OK");
        } else {
            Serial.println("LittleFS: logos MISSING — run: pio run -e matrixportal_s3 --target uploadfs");
        }
        initLogoCache();
    }

    if (psramFound()) {
        Serial.printf("PSRAM: %u KB total, %u KB free\n",
                      ESP.getPsramSize() / 1024,
                      ESP.getFreePsram() / 1024);
        // Test PSRAM integrity — detects bad PSRAM or init failure
        void* test = heap_caps_malloc(65536, MALLOC_CAP_SPIRAM);
        bool psramTest = false;
        if (test) {
            memset(test, 0xA5, 65536);
            psramTest = (((uint8_t*)test)[0] == 0xA5 && ((uint8_t*)test)[65535] == 0xA5);
            heap_caps_free(test);
        }
        Serial.printf("[HW] PSRAM integrity test: %s\n", psramTest ? "PASS" : "FAIL");
    } else {
        Serial.println("PSRAM: NOT detected");
    }

    Serial.println("[TLS] max_frag_len patch: ACTIVE (4096)");

    DB.begin();
    DB.load();

    // Turn WiFi off to free contiguous SRAM for Display.begin().
    // With logos in PSRAM we still have plenty of heap left for SSL later.
    WiFi.mode(WIFI_OFF);
    delay(200);
    Serial.printf("heap before Display.begin: %u free, %u max-block\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // Load saved brightness before Display.begin() so begin() picks it up
    Preferences prefs;
    prefs.begin("rugby", true);
    Display.setBrightness(prefs.getInt("brightness", 80));
    prefs.end();

    if (!Display.begin()) {
        Serial.println("Display init failed — check wiring");
        neoSet(64, 0, 0);
        while (1) { neoSet(64, 0, 0); delay(500); neoSet(0, 0, 0); delay(500); }
    }
    neoSet(0, 64, 0); // green = display OK
    hwTest();

    // NOTE: heap_caps_malloc_extmem_enable is intentionally DISABLED.
    // It forces mbedtls/WiFiClientSecure buffers into PSRAM which is too slow
    // for TLS handshakes and causes systematic connection failures (HTTP -1).
    // Large allocations are explicitly routed to PSRAM where we control them:
    // - LogoCache (heap_caps_malloc SPIRAM)
    // - readEntireStream (heap_caps_malloc SPIRAM)
    // - JsonDocument (JsonAllocator.h / spiRamAlloc)
    if (psramFound()) {
        Serial.println("PSRAM: explicit-allocs only (extmem_enable disabled for TLS stability)");
    }

    // Start showing cached data immediately while fetch runs in background
    Serial.println("setup: starting Scenes.begin()");
    Scenes.begin(&DB);
    Serial.println("setup: Scenes.begin() done");

    xTaskCreatePinnedToCore(renderTask, "Renderer", 6144, nullptr, 2, &rendererHandle, 1);
    Serial.println("setup: renderer task created");

    // Boot fetch in background — renderer stays alive so user sees old data
    neoSet(0, 64, 64); // cyan = fetching
    Fetcher.setDB(&DB);
    Fetcher.setRendererHandle(rendererHandle);
    xTaskCreatePinnedToCore(bootFetchTask, "BootFetch", 12288, nullptr, 1, nullptr, 0);

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    Serial.printf("Boot setup done — heap: %u  maxBlock: %u  PSRAM: %u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
}

void loop() {
    ArduinoOTA.handle();
    Web.handle();
    if (Web.shouldRestart()) {
        delay(500);
        ESP.restart();
    }

    // When boot fetch finishes, rebuild scenes, show IP info, then start services
    static bool bootFetchHandled = false;
    if (!bootFetchHandled && s_bootFetchDone) {
        bootFetchHandled = true;
        Scenes.markDirty();
        Scenes.requestRebuild();
        showBootInfo();
        Fetcher.begin(&DB);
        Serial.println("setup: DataFetcher started");
    }

    static bool servicesStarted = false;
    if (!servicesStarted && bootFetchHandled && ESP.getFreeHeap() >= 50000) {
        Web.begin(&DB);
        servicesStarted = true;
        Serial.printf("Web server started — heap: %u\n", ESP.getFreeHeap());
    }

    static bool apStarted = false;
    static bool mdnsStarted = false;
    static DNSServer dnsServer;
    if (bootFetchHandled && servicesStarted) {
        if (!apStarted && WiFi.status() != WL_CONNECTED) {
            WiFiManager::startAP();
            dnsServer.start(53, "*", WiFi.softAPIP());
            Serial.println("DNS captive portal started");
            apStarted = true;
            Scenes.markDirty();
        }
        if (apStarted) {
            dnsServer.processNextRequest();
        }
        if (apStarted && WiFi.status() == WL_CONNECTED) {
            WiFiManager::stopAP();
            apStarted = false;
            Scenes.markDirty();
        }
        if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
            ArduinoOTA.setHostname("rugby-display");
            ArduinoOTA.begin();
            MDNS.begin("rugby-display");
            MDNS.addService("http", "tcp", 80);
            String ip = WiFi.localIP().toString();
            Serial.printf("OTA + mDNS started — IP: %s\n", ip.c_str());
            mdnsStarted = true;
            Scenes.markDirty();
        }
    }
    delay(10);
}
