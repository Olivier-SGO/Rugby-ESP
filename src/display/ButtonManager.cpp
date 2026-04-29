#include "ButtonManager.h"
#include "SceneManager.h"
#include "DisplayManager.h"
#include <Preferences.h>

ButtonManager Buttons;

void ButtonManager::begin() {
    pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
    Serial.println("[BTN] UP/DOWN buttons init (GPIO 6/7)");
}

void ButtonManager::adjustBrightness(int delta) {
    int b = (int)Display.getBrightness() + delta;
    if (b < 10) b = 10;
    if (b > 255) b = 255;
    Display.setBrightness((uint8_t)b);
    Preferences prefs;
    prefs.begin("rugby", false);
    prefs.putInt("brightness", b);
    prefs.end();
    Serial.printf("[BTN] brightness -> %d\n", b);
}

void ButtonManager::update() {
    uint32_t now = millis();

    // ── UP button ─────────────────────────────────────────────────────────────
    bool upPressed = (digitalRead(PIN_BUTTON_UP) == LOW);
    if (upPressed && !_upWasPressed && (now - _upLastRelease > DEBOUNCE_MS)) {
        _upPressStart = now;
        _upWasPressed = true;
    } else if (!upPressed && _upWasPressed) {
        uint32_t dur = now - _upPressStart;
        if (dur >= LONG_PRESS_MS) {
            adjustBrightness(+10);
        } else {
            Scenes.nextScene();
            Serial.println("[BTN] next scene");
        }
        _upWasPressed = false;
        _upLastRelease = now;
    }

    // ── DOWN button ───────────────────────────────────────────────────────────
    bool downPressed = (digitalRead(PIN_BUTTON_DOWN) == LOW);
    if (downPressed && !_downWasPressed && (now - _downLastRelease > DEBOUNCE_MS)) {
        _downPressStart = now;
        _downWasPressed = true;
    } else if (!downPressed && _downWasPressed) {
        uint32_t dur = now - _downPressStart;
        if (dur >= LONG_PRESS_MS) {
            adjustBrightness(-10);
        } else {
            Scenes.prevScene();
            Serial.println("[BTN] prev scene");
        }
        _downWasPressed = false;
        _downLastRelease = now;
    }
}
