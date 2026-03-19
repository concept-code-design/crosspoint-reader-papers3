#include <HalGPIO.h>
#include <HalM5Mutex.h>
#include <M5Unified.h>
#include <SPI.h>

// Touch zone boundaries in LOGICAL portrait coordinates (540 wide x 960 tall)
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// Physical touch (phyX, phyY) → logical portrait: logX = 539 - phyY, logY = phyX
static constexpr int16_t LOGICAL_WIDTH = 540;
static constexpr int16_t LOGICAL_HEIGHT = 960;

// Bottom navigation bar
static constexpr int16_t NAV_BAR_TOP = 860;
static constexpr int16_t NAV_ZONE_WIDTH = LOGICAL_WIDTH / 4;  // 135px per button

// Main reading area (between header and nav bar)
static constexpr int16_t READING_TOP = 60;
static constexpr int16_t READING_BOTTOM = NAV_BAR_TOP;
static constexpr int16_t READING_MID_X = LOGICAL_WIDTH / 2;  // 270

void HalGPIO::begin() {
  // Initialize SD card SPI bus with PaperS3 pins
  SPI.begin(PAPERS3_SD_SCK, PAPERS3_SD_MISO, PAPERS3_SD_MOSI, PAPERS3_SD_CS);
  // Touch is initialized by M5Unified in M5.begin()
}

int HalGPIO::touchZoneToButton(int16_t touchX, int16_t touchY) const {
  // Transform physical touch coordinates (960x540 landscape) to logical portrait (540x960)
  // Physical panel: origin top-left in landscape, X goes right (0-960), Y goes down (0-540)
  // Portrait rotation (90° CW): logicalX = 539 - physicalY, logicalY = physicalX
  int16_t logX = 539 - touchY;
  int16_t logY = touchX;

  // Bottom navigation bar: 4 button zones
  if (logY >= NAV_BAR_TOP) {
    if (logX < NAV_ZONE_WIDTH) return BTN_BACK;
    if (logX < NAV_ZONE_WIDTH * 2) return BTN_LEFT;
    if (logX < NAV_ZONE_WIDTH * 3) return BTN_RIGHT;
    return BTN_CONFIRM;
  }

  // Main reading area: left half = page back, right half = page forward
  if (logY >= READING_TOP && logY < READING_BOTTOM) {
    if (logX < READING_MID_X) return BTN_UP;    // Page back
    return BTN_DOWN;                              // Page forward
  }

  // Top area: treat as confirm (open menus, etc.)
  if (logY < READING_TOP) {
    return BTN_CONFIRM;
  }

  return -1;
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