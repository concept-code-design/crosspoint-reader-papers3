#include <HalGPIO.h>
#include <Logging.h>
#include <SPI.h>

// Touch zones in LOGICAL portrait coordinates (540 wide x 960 tall).
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// GT911 on M5PaperS3 reports in portrait: x[0-539], y[0-959].
static constexpr int16_t PORT_W = 540;
static constexpr int16_t PORT_H = 960;

// 3-zone vertical split: each zone is 1/3 of screen width (180px)
static constexpr int16_t ZONE_LEFT_END = PORT_W / 3;         // 180
static constexpr int16_t ZONE_RIGHT_START = PORT_W * 2 / 3;  // 360

void HalGPIO::begin() {
  // Initialize SD card SPI bus with PaperS3 pins
  SPI.begin(PAPERS3_SD_SCK, PAPERS3_SD_MISO, PAPERS3_SD_MOSI, PAPERS3_SD_CS);
  // Initialize GT911 touch on I2C1 (SDA=41, SCL=42, INT=48)
  if (!touch.begin(41, 42, 48, 0x14)) {
    LOG_ERR("GPIO", "GT911 touch init failed");
  }
}

int HalGPIO::touchZoneToButton(int16_t touchX, int16_t touchY) const {
  // GT911 on M5PaperS3 reports portrait coordinates directly: x[0-539], y[0-959].
  if (touchX < 0 || touchX >= PORT_W || touchY < 0 || touchY >= PORT_H) return -1;

  // Footer nav bar: bottom footerHeight pixels are split into 4 equal tap zones
  // mapping to Back / Confirm / Up / Down (matches drawButtonHints layout)
  if (footerHeight > 0 && touchY >= PORT_H - footerHeight) {
    const int16_t quarter = PORT_W / 4;
    if (touchX < quarter) return BTN_BACK;
    if (touchX < quarter * 2) return BTN_CONFIRM;
    if (touchX < quarter * 3) return BTN_UP;
    return BTN_DOWN;
  }

  // Simple 3-zone vertical split across the content area
  if (touchX < ZONE_LEFT_END) return BTN_LEFT;
  if (touchX >= ZONE_RIGHT_START) return BTN_RIGHT;
  return BTN_CONFIRM;
}

void HalGPIO::update() {
  previousState = currentState;
  currentState = 0;

  // During cooldown (after activity transition), drain touch events but don't act on them
  if (millis() < cooldownUntil) {
    touch.update();  // Drain pending touch reports
    touchActive = false;
    sawMultiTouch = false;
    return;
  }

  bool touching = touch.update();
  uint8_t numPoints = touch.getNumPoints();

  if (touching) {
    lastTouchX = touch.getX();
    lastTouchY = touch.getY();

    // Track multi-touch: if we ever see 2+ fingers during this sequence, remember it
    if (numPoints >= 2) {
      sawMultiTouch = true;
    }

    // Record start position on initial touch-down
    if (!touchActive) {
      touchActive = true;
      touchStartX = lastTouchX;
      touchStartY = lastTouchY;
    }

    // While finger is down, report the zone button as pressed (for held-time detection)
    int btn = touchZoneToButton(lastTouchX, lastTouchY);
    if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
      currentState |= (1 << btn);
    }
  } else if (touchActive) {
    // Finger just lifted — classify the gesture
    touchActive = false;

    if (sawMultiTouch) {
      // 2-finger tap → BACK
      currentState |= (1 << BTN_BACK);
      currentState |= (1 << BTN_TWO_FINGER);
      LOG_DBG("TOUCH", "2-finger tap -> BACK");
    } else {
      // Single finger: check for swipe vs tap
      int16_t deltaY = lastTouchY - touchStartY;

      if (deltaY < -SWIPE_THRESHOLD) {
        // Swiped up (finger moved upward)
        currentState |= (1 << BTN_SWIPE_UP);
        currentState |= (1 << BTN_UP);
        LOG_DBG("TOUCH", "swipe up dy=%d", deltaY);
      } else if (deltaY > SWIPE_THRESHOLD) {
        // Swiped down (finger moved downward)
        currentState |= (1 << BTN_SWIPE_DOWN);
        currentState |= (1 << BTN_DOWN);
        LOG_DBG("TOUCH", "swipe down dy=%d", deltaY);
      } else {
        // Tap — map to zone based on touch-down position
        int btn = touchZoneToButton(touchStartX, touchStartY);
        if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
          currentState |= (1 << btn);
        }
        LOG_DBG("TOUCH", "tap at (%d,%d) btn=%d", touchStartX, touchStartY, btn);
      }
    }

    sawMultiTouch = false;
  }

  // Track press timing
  uint16_t newPresses = currentState & ~previousState;
  if (newPresses) {
    pressStartTime = millis();
    for (uint8_t i = 0; i < HALGPIO_NUM_BUTTONS; i++) {
      if (newPresses & (1 << i)) {
        lastPressedButton = i;
        break;
      }
    }
  }
}

void HalGPIO::clearState() {
  previousState = 0;
  currentState = 0;
  pressStartTime = 0;
  touchActive = false;
  sawMultiTouch = false;
  cooldownUntil = millis() + 200;  // Suppress input for 200ms after activity transition
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  return currentState & (1 << buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Rising edge: pressed now but not before
  return (currentState & (1 << buttonIndex)) && !(previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const { return (currentState & ~previousState) != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Falling edge: not pressed now but was before
  return !(currentState & (1 << buttonIndex)) && (previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const { return (previousState & ~currentState) != 0; }

unsigned long HalGPIO::getHeldTime() const {
  if (currentState == 0) return 0;
  return millis() - pressStartTime;
}

bool HalGPIO::isUsbConnected() const {
  // With ARDUINO_USB_CDC_ON_BOOT=1, Serial is USB CDC.
  // operator bool() returns true when a USB host has connected (DTR asserted).
  // Serial.begin() must be called before this for reliable results.
  return Serial;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0 || wakeupCause == ESP_SLEEP_WAKEUP_EXT1 ||
      wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  // ESP32-S3: After flash, esptool hard-resets via RTS → ESP_RST_POWERON (not ESP_RST_UNKNOWN like ESP32-C3).
  // On M5PaperS3, the PMIC handles charging independently, so any cold boot with USB connected
  // should proceed to boot normally (treat as AfterFlash).
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && usbConnected &&
      (resetReason == ESP_RST_UNKNOWN || resetReason == ESP_RST_POWERON)) {
    return WakeupReason::AfterFlash;
  }
  return WakeupReason::Other;
}