#include "HalTouch.h"

bool HalTouch::begin(int sda, int scl, int intPin, uint8_t addr) {
  _addr = addr;

  // Configure interrupt pin as input (GT911 signals data-ready via INT)
  if (intPin >= 0) {
    pinMode(intPin, INPUT);
  }

  // Use Wire1 (second I2C port) — same bus M5Unified used for touch on PaperS3
  _wire = &Wire1;
  _wire->begin(sda, scl, 400000);

  // Verify GT911 is present by reading config version register (0x8047)
  uint8_t cfgVer = 0;
  if (!readRegisters(0x8047, &cfgVer, 1)) {
    // Try alternate address
    _addr = (_addr == 0x14) ? 0x5D : 0x14;
    if (!readRegisters(0x8047, &cfgVer, 1)) {
      return false;
    }
  }

  if (Serial) Serial.printf("[%lu] HalTouch: GT911 found at 0x%02X, config v%d\n", millis(), _addr, cfgVer);
  return true;
}

bool HalTouch::update() {
  _touched = false;

  // Read status register
  uint8_t status = 0;
  if (!readRegisters(REG_STATUS, &status, 1)) return false;

  // Bit 7 = buffer ready, bits 3:0 = number of touch points
  bool bufferReady = status & 0x80;
  uint8_t numPoints = status & 0x0F;

  if (bufferReady) {
    if (numPoints > 0 && numPoints <= 5) {
      uint8_t buf[8];
      if (readRegisters(REG_POINT1, buf, 8)) {
        // M5PaperS3 GT911 layout at 0x8150: xL, xH, yL, yH, sizeL, sizeH, ...
        // (no track ID byte at offset 0 — differs from some GT911 datasheets)
        _x = (int16_t)(buf[0] | (buf[1] << 8));
        _y = (int16_t)(buf[2] | (buf[3] << 8));

        // Debounce: suppress new touch-down if within DEBOUNCE_MS of last release
        if (!_wasTouched && (millis() - _lastTouchMs < DEBOUNCE_MS)) {
          // Finger just came down but too soon after last release — ignore
        } else {
          _touched = true;
        }
      }
    }
    // Clear buffer status (write 0 to acknowledge)
    writeRegister(REG_STATUS, 0);
  }

  // Track release timing for debounce
  if (_wasTouched && !_touched) {
    _lastTouchMs = millis();
  }
  _wasTouched = _touched;

  return _touched;
}

bool HalTouch::readRegisters(uint16_t reg, uint8_t* buf, uint8_t len) {
  if (!_wire) return false;

  _wire->beginTransmission(_addr);
  _wire->write((uint8_t)(reg >> 8));    // Register address high byte
  _wire->write((uint8_t)(reg & 0xFF));  // Register address low byte
  if (_wire->endTransmission(false) != 0) return false;

  uint8_t got = _wire->requestFrom(_addr, len);
  if (got != len) return false;

  for (uint8_t i = 0; i < len; i++) {
    buf[i] = _wire->read();
  }
  return true;
}

bool HalTouch::writeRegister(uint16_t reg, uint8_t val) {
  if (!_wire) return false;

  _wire->beginTransmission(_addr);
  _wire->write((uint8_t)(reg >> 8));
  _wire->write((uint8_t)(reg & 0xFF));
  _wire->write(val);
  return _wire->endTransmission() == 0;
}
