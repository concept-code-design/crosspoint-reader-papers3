#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

// AXP2101 PMIC I2C address and battery registers
static constexpr uint8_t AXP2101_ADDR = 0x34;
static constexpr uint8_t AXP2101_REG_BATTERY_PERCENT = 0xA4;  // SOC (State of Charge) 0-100

// M5PaperS3 power-off pulse pin (active-high pulse turns off PMIC)
static constexpr int PWROFF_PULSE_PIN = 44;

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
#if CROSSPOINT_PAPERS3
  // PaperS3 has a dedicated PMIC for power management and deep-sleeps via
  // GPIO44 pulse.  CPU throttling from 240→10 MHz only adds touch latency
  // (~50 ms) with negligible battery savings.  Keep full speed always.
  (void)enabled;
  return;
#else
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
#endif
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }

  // Power off via GPIO44 pulse to PMIC
  // This turns off the device completely; wakeup is via power button through PMIC
  pinMode(PWROFF_PULSE_PIN, OUTPUT);
  digitalWrite(PWROFF_PULSE_PIN, HIGH);
  delay(100);
  digitalWrite(PWROFF_PULSE_PIN, LOW);

  // If powerOff doesn't halt (e.g., USB connected), fall back to deep sleep
  // with a 5-second timer wakeup as safety net — without a wakeup source the
  // device would be stuck in unrecoverable deep sleep.
  esp_sleep_enable_timer_wakeup(5 * 1000 * 1000);  // 5 seconds in microseconds
  esp_deep_sleep_start();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
#if CROSSPOINT_PAPERS3
  // Read battery SOC from AXP2101 PMIC via Wire1 (shared with GT911 touch).
  // Both callers run on the main loop task so no bus contention.
  Wire1.beginTransmission(AXP2101_ADDR);
  Wire1.write(AXP2101_REG_BATTERY_PERCENT);
  if (Wire1.endTransmission(false) != 0) {
    LOG_ERR("PWR", "AXP2101 I2C write failed");
    return 0;
  }
  if (Wire1.requestFrom(AXP2101_ADDR, (uint8_t)1) != 1) {
    LOG_ERR("PWR", "AXP2101 I2C read failed");
    return 0;
  }
  uint8_t soc = Wire1.read();
  if (soc > 100) soc = 100;
  return soc;
#else
  return 100;
#endif
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
