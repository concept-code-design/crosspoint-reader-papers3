#include <HalGPIO.h>
#include <HalM5Mutex.h>
#include <Logging.h>
#include <M5Unified.h>
#include <SPI.h>

// Touch zones in LOGICAL portrait coordinates (540 wide x 960 tall).
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// Coordinate transform from touch space to portrait is auto-detected at runtime.
static constexpr int16_t PORT_W = 540;
static constexpr int16_t PORT_H = 960;

// Zone boundaries (portrait logical coordinates)
static constexpr int16_t TOP_ZONE_H   = 70;   // Back zone at top
static constexpr int16_t BOT_ZONE_TOP = 890;   // Bottom strip for Left/Right/Back
static constexpr int16_t EDGE_STRIP_W = 108;   // 20% width for Up/Down edge strips

void HalGPIO::begin() {
  // Initialize SD card SPI bus with PaperS3 pins
  SPI.begin(PAPERS3_SD_SCK, PAPERS3_SD_MISO, PAPERS3_SD_MOSI, PAPERS3_SD_CS);
  // Touch is initialized by M5Unified in M5.begin()
}

int HalGPIO::touchZoneToButton(int16_t touchX, int16_t touchY) const {
  // Auto-detect coordinate space from current display rotation.
  // If display reports landscape (width > height), touch is in native 960x540 space
  // and we must rotate to portrait.  Otherwise touch is already portrait.
  int16_t logX, logY;
  if (M5.Display.width() > M5.Display.height()) {
    // Native landscape touch → portrait: logX = maxY - touchY, logY = touchX
    logX = (M5.Display.height() - 1) - touchY;
    logY = touchX;
  } else {
    // Already portrait coordinates
    logX = touchX;
    logY = touchY;
  }

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

  // Acquire M5 mutex - M5GFX is not thread-safe; display operations run on render task
  HalM5Mutex::lock();

  // Update M5Unified state (reads touch + buttons)
  M5.update();

  // Check physical power button via M5Unified
  if (M5.BtnPWR.isPressed()) {
    currentState |= (1 << BTN_POWER);
  }

  // Check touch input
  auto touchCount = M5.Touch.getCount();
  if (touchCount > 0) {
    auto detail = M5.Touch.getDetail(0);
    if (detail.isPressed()) {
      int btn = touchZoneToButton(detail.x, detail.y);
      LOG_DBG("TOUCH", "raw=(%d,%d) btn=%d disp=(%d,%d)", detail.x, detail.y, btn,
              M5.Display.width(), M5.Display.height());
      if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
        currentState |= (1 << btn);
      }
    }
  }

  HalM5Mutex::unlock();

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

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  return currentState & (1 << buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Rising edge: pressed now but not before
  return (currentState & (1 << buttonIndex)) && !(previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const {
  return (currentState & ~previousState) != 0;
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Falling edge: not pressed now but was before
  return !(currentState & (1 << buttonIndex)) && (previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const {
  return (previousState & ~currentState) != 0;
}

unsigned long HalGPIO::getHeldTime() const {
  if (currentState == 0) return 0;
  return millis() - pressStartTime;
}

bool HalGPIO::isUsbConnected() const {
  // ESP32-S3 has native USB - check if USB is connected via VBUS detection
  // On PaperS3, M5Unified handles USB detection
  return M5.Power.isCharging();
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