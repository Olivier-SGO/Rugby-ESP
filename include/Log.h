#pragma once
#include <Arduino.h>

// Simple timestamped logging macros.
// Usage: LOGF("heap=%u\n", ESP.getFreeHeap());   or   LOGLN("WiFi connected");
#define LOGF(fmt, ...) Serial.printf("[%10lu] " fmt, millis(), ##__VA_ARGS__)
#define LOGLN(msg)     Serial.printf("[%10lu] %s\n", millis(), msg)
