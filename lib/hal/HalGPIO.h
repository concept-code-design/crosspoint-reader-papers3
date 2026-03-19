#pragma once

#include <Arduino.h>

// Number of virtual buttons (touch zones + power)
#define HALGPIO_NUM_BUTTONS 7

class HalGPIO {
 public:
  HalGPIO() = default;

  // Start touch input via M5Unified and setup SD card SPI
  void begin();

  // Button input methods (touch zones mapped to virtual buttons)
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

  // Check if USB is connected
  bool isUsbConnected() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices (same as original - touch zones map to these)
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;     // Page back (left half of screen)
  static constexpr uint8_t BTN_DOWN = 5;   // Page forward (right half of screen)
  static constexpr uint8_t BTN_POWER = 6;

 private:
  // Touch zone detection: converts physical touch coordinates to button index
  int touchZoneToButton(int16_t touchX, int16_t touchY) const;

  // Button state tracking (per-frame edge detection)
  uint8_t currentState = 0;   // Bitmask of currently pressed buttons
  uint8_t previousState = 0;  // Bitmask from last frame
  unsigned long pressStartTime = 0;
  uint8_t lastPressedButton = 0xFF;
};
