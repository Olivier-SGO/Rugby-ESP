#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "DisplayManager.h"
#include "SceneManager.h"
#include "MatchDB.h"
#include "DataFetcher.h"
#include "WebServer.h"

// ── Renderer task (Core 1) ────────────────────────────────────────────────────
static void renderTask(void*) {
    const TickType_t frameDelay = pdMS_TO_TICKS(33); // ~30fps
    for (;;) {
        Scenes.tick();
        vTaskDelay(frameDelay);
        esp_task_wdt_reset();
    }
}

// ── Arduino setup ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Rugby ESP32 Display ===");

    // LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    // Display
    if (!Display.begin()) {
        Serial.println("Display init failed — check wiring");
        while (1) delay(1000);
    }

    // Boot message
    Display.fillScreen(C_BLACK);
    Display.drawText(10, 30, "Rugby Display", C_BLUE, nullptr);
    Display.drawText(10, 46, "Connexion WiFi...", C_GREY, nullptr);
    Display.flip();

    // Load saved brightness
    Preferences prefs;
    prefs.begin("rugby", true);
    Display.setBrightness(prefs.getInt("brightness", 80));
    prefs.end();

    // Database
    DB.begin();
    DB.load();

    // OTA
    ArduinoOTA.setHostname("rugby-display");
    ArduinoOTA.begin();

    // Data fetcher (Core 0, starts WiFi+NTP internally)
    Fetcher.begin(&DB);

    // Wait up to 20s for WiFi before starting Web + Renderer
    uint32_t start = millis();
    while (!Fetcher.isWiFiConnected() && millis() - start < 20000)
        delay(200);

    if (Fetcher.isWiFiConnected()) Web.begin(&DB);

    // Scene manager + renderer on Core 1
    Scenes.begin(&DB);
    xTaskCreatePinnedToCore(renderTask, "Renderer", 8192, nullptr, 2, nullptr, 1);

    // Task watchdog
    esp_task_wdt_init(WDT_TIMEOUT_S, true);

    Serial.println("Boot complete");
}

void loop() {
    ArduinoOTA.handle();
    delay(10);
}
