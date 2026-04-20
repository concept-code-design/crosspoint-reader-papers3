#pragma once

#include <Arduino.h>

class HalRTC;
extern HalRTC halRTC;

// Hardware abstraction for the BM8563 RTC (PCF8563-compatible) on I2C1 (SDA=41, SCL=42).
// Responsibilities:
//   begin()       — init RTC, read BM8563 → set ESP32 system clock
//   syncWithNTP() — poll NTP server → update system clock → write BM8563
class HalRTC {
 public:
  // Initialize the BM8563. Call after gpio.begin() (I2C port 1 already configured
  // by HalTouch). Reads the stored RTC time and sets the ESP32 system clock.
  // Sets isSynced() = true only when the RTC battery is good and time is valid.
  void begin();

  // Synchronise from NTP (blocking, up to 5 s). Writes the result to the BM8563.
  // Must be called with WiFi connected. Sets isSynced() = true on success.
  void syncWithNTP(const char* server = "ntp0.nl.net");

  // Returns true once the system clock holds a valid time (either from a healthy
  // RTC battery on boot, or after a successful NTP sync).
  bool isSynced() const { return synced; }

 private:
  bool synced = false;
};
