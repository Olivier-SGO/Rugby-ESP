#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>

#include "config.h"
#include "DisplayManager.h"
#include "SceneManager.h"
#include "MatchDB.h"
#include "DataFetcher.h"
#include "WebServer.h"

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
        delay(1000);
    }
    Display.fillScreen(C_BLACK);
    Display.drawText(4, 20, "HUB75 OK  256x64", 0xFFFF, nullptr);
    Display.drawText(4, 36, "R  G  B  W  OK",   0x07E0, nullptr);
    Display.flip();
    delay(2000);
}

static TaskHandle_t rendererHandle = nullptr;

// ── Renderer task (Core 1) ────────────────────────────────────────────────────
static void renderTask(void*) {
    const TickType_t frameDelay = pdMS_TO_TICKS(33); // ~30fps
    for (;;) {
        Scenes.tick();
        vTaskDelay(frameDelay);
        esp_task_wdt_reset();
    }
}

// ── Boot fetch task (Core 0, 16KB stack — avoids setup() stack overflow) ─────
static volatile bool s_bootFetchDone = false;
static void bootFetchTask(void*) {
    Fetcher.connectWiFi();
    if (Fetcher.isWiFiConnected()) Fetcher.syncNTP();
    Fetcher.fetchAll();
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

    neo.begin();
    neoSet(0, 0, 64); // blue = booting

    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed (run uploadfs)");
    } else {
        // Check a known file to confirm uploadfs was done
        if (LittleFS.exists("/logos/toulouse.bin")) {
            Serial.println("LittleFS: logos OK");
        } else {
            Serial.println("LittleFS: logos MISSING — run: pio run -e matrixportal_s3 --target uploadfs");
        }
    }

    DB.begin();
    DB.load();

    // Boot fetch in a dedicated 16KB task so SSL/HTTP don't overflow setup()'s stack.
    // Must happen before Display.begin() so SSL has 230KB+ of free heap.
    // Set _db before the task starts so fetchAll() can call DB.updateTop14() etc.
    neoSet(0, 64, 64); // cyan = fetching
    Fetcher.setDB(&DB);
    xTaskCreatePinnedToCore(bootFetchTask, "BootFetch", 16384, nullptr, 1, nullptr, 0);
    { uint32_t t0 = millis(); while (!s_bootFetchDone && millis()-t0 < 90000) delay(100); }
    if (!s_bootFetchDone) Serial.println("Boot fetch timeout — continuing");

    // Turn WiFi off so Display.begin() can get 134KB of contiguous DMA-capable SRAM.
    // The DataFetcher periodic task will reconnect WiFi when it starts.
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

    // Scene manager + renderer on Core 1
    Scenes.begin(&DB);
    xTaskCreatePinnedToCore(renderTask, "Renderer", 8192, nullptr, 2, &rendererHandle, 1);

    // DataFetcher periodic polling task — reconnects WiFi, handles OTA/Web after first fetch
    Fetcher.begin(&DB); // resets _wifiOk → task reconnects WiFi

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    Serial.printf("Boot complete — heap: %u\n", ESP.getFreeHeap());
}

void loop() {
    ArduinoOTA.handle();
    static bool servicesStarted = false;
    if (!servicesStarted && Fetcher.isWiFiConnected()) {
        ArduinoOTA.setHostname("rugby-display");
        ArduinoOTA.begin();
        MDNS.begin("rugby-display");
        MDNS.addService("http", "tcp", 80);
        Web.begin(&DB);
        String ip = WiFi.localIP().toString();
        Serial.printf("OTA + Web started — heap: %u\n", ESP.getFreeHeap());
        Serial.printf(">>> Web UI: http://rugby-display.local  (IP: %s)\n", ip.c_str());
        Scenes.markDirty();
        servicesStarted = true;
    }
    delay(10);
}
