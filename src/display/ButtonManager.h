#pragma once
#include <Arduino.h>

class ButtonManager {
public:
    void begin();
    void update();

private:
    bool _upWasPressed = false;
    bool _downWasPressed = false;
    uint32_t _upPressStart = 0;
    uint32_t _downPressStart = 0;
    uint32_t _upLastRelease = 0;
    uint32_t _downLastRelease = 0;

    static constexpr uint32_t LONG_PRESS_MS = 600;
    static constexpr uint32_t DEBOUNCE_MS = 50;

    void adjustBrightness(int delta);
};

extern ButtonManager Buttons;
