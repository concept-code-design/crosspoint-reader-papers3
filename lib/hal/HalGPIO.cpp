#include <HalGPIO.h>
#include <Logging.h>
#include <SPI.h>

// Touch zones in LOGICAL portrait coordinates (540 wide x 960 tall).
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// GT911 on M5PaperS3 reports in portrait: x[0-539], y[0-959].
static constexpr int16_t PORT_W = 540;
static constexpr int16_t PORT_H = 960;

// Zone boundaries (portrait logical coordinates)
static constexpr int16_t TOP_ZONE_H = 70;     // Back zone at top
static constexpr int16_t BOT_ZONE_TOP = 890;  // Bottom strip for Left/Right/Back
static constexpr int16_t EDGE_STRIP_W = 108;  // 20% width for Up/Down edge strips

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
  // These map to our portrait logical space without further rotation.
  int16_t logX = touchX;
  int16_t logY = touchY;

  // Clamp to portrait bounds
  if (logX < 0 || logX >= PORT_W || logY < 0 || logY >= PORT_H) return -1;

  // --- Top zone: BACK ---
  if (logY < TOP_ZONE_H) {
    return BTN_BACK;
  }

  // --- Bottom strip: BACK / LEFT / RIGHT ---
  if (logY >= BOT_ZONE_TOP) {
    int16_t third = PORT_W / 3;  // 180
    if (logX < third) return BTN_BACK;
    if (logX < third * 2) return BTN_LEFT;
    return BTN_RIGHT;
  }

  // --- Content area (between top and bottom zones) ---
  // Left edge strip (20%): UP / Page Back
  if (logX < EDGE_STRIP_W) return BTN_UP;
  // Right edge strip (20%): DOWN / Page Forward
  if (logX >= PORT_W - EDGE_STRIP_W) return BTN_DOWN;
  // Center (60%): CONFIRM / Select
  return BTN_CONFIRM;
}

void HalGPIO::update() {
  previousState = currentState;
  currentState = 0;

  // During cooldown (after activity transition), drain touch events but don't act on them
  if (millis() < cooldownUntil) {
    touch.update();  // Drain pending touch reports
    return;
  }

  // Poll GT911 touch
  if (touch.update()) {
    lastTouchX = touch.getX();
    lastTouchY = touch.getY();
    int btn = touchZoneToButton(lastTouchX, lastTouchY);
    LOG_DBG("TOUCH", "raw=(%d,%d) btn=%d", lastTouchX, lastTouchY, btn);
    if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
      currentState |= (1 << btn);
    }
  }

  // Track press timing
  uint8_t newPresses = currentState & ~previousState;
  if (newPresses) {
    pressStartTime = millis();
    // Find the first newly pressed button
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