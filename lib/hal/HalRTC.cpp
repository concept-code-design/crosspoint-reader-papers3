#include "HalRTC.h"

#include <Logging.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.hpp>

// ── Module-level RTC instance ──────────────────────────────────────────────────
// The PCF8563_Class (BM8563) is constructed by RTC_Class::begin() on the heap.
// We keep one file-scoped instance so both begin() and syncWithNTP() share state.
static m5::RTC_Class rtc;

// ── I2C pins (shared with GT911 touch, same physical bus) ─────────────────────
static constexpr int kSDA     = 41;
static constexpr int kSCL     = 42;
static constexpr int kI2CPort = 1;  // I2C_NUM_1

// ── Singleton definition ───────────────────────────────────────────────────────
HalRTC halRTC;

// ── begin ──────────────────────────────────────────────────────────────────────

void HalRTC::begin() {
  // I2C port 1 is already physically initialised by HalTouch (GT911 @ 0x14).
  // Configure m5::In_I2C so the PCF8563 driver knows which port to use.
  m5::In_I2C.begin(static_cast<i2c_port_t>(kI2CPort), kSDA, kSCL);

  if (!rtc.begin(&m5::In_I2C)) {
    LOG_ERR("RTC", "BM8563 init failed — system clock unchanged");
    return;
  }

  if (rtc.getVoltLow()) {
    LOG_ERR("RTC", "BM8563 voltage low — stored time may be invalid, clock will show --:--");
    return;
  }

  // Apply RTC time to the ESP32 system clock (UTC).
  rtc.setSystemTimeFromRtc();
  synced = true;

  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  LOG_DBG("RTC", "System time set from BM8563: %04d-%02d-%02d %02d:%02d:%02d UTC",
          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

// ── syncWithNTP ────────────────────────────────────────────────────────────────

void HalRTC::syncWithNTP(const char* server) {
  LOG_DBG("RTC", "NTP sync: %s", server);

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, server);
  esp_sntp_init();

  // Wait up to 5 s (50 × 100 ms) for the sync to complete.
  static constexpr int kMaxRetries = 50;
  int retry = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < kMaxRetries) {
    vTaskDelay(pdMS_TO_TICKS(100));
    ++retry;
  }

  esp_sntp_stop();

  if (retry >= kMaxRetries) {
    LOG_ERR("RTC", "NTP sync timed out");
    return;
  }

  // System clock now holds accurate UTC time — write it to the BM8563.
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  rtc.setDateTime(&t);
  synced = true;

  LOG_DBG("RTC", "BM8563 updated: %04d-%02d-%02d %02d:%02d:%02d UTC",
          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}
