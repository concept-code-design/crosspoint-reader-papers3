#pragma once

#include <Arduino.h>
#include <Wire.h>

// Minimal GT911 capacitive touch driver for M5PaperS3
// Reads single-point touch coordinates over I2C without M5Unified dependency.
class HalTouch {
 public:
  HalTouch() = default;

  // Initialize GT911 on the given I2C pins. Returns true on success.
  bool begin(int sda = 41, int scl = 42, int intPin = 48, uint8_t addr = 0x14);

  // Poll touch state. Call once per frame. Returns true if a touch is active.
  bool update();

  bool isTouched() const { return _touched; }
  int16_t getX() const { return _x; }
  int16_t getY() const { return _y; }

 private:
  static constexpr uint16_t REG_STATUS = 0x814E;
  static constexpr uint16_t REG_POINT1 = 0x8150;

  bool readRegisters(uint16_t reg, uint8_t* buf, uint8_t len);
  bool writeRegister(uint16_t reg, uint8_t val);

  TwoWire* _wire = nullptr;
  uint8_t _addr = 0x14;
  int16_t _x = 0;
  int16_t _y = 0;
  bool _touched = false;
  bool _wasTouched = false;
  unsigned long _lastTouchMs = 0;
  static constexpr unsigned long DEBOUNCE_MS = 150;
};
