#pragma once

#include <freertos/semphr.h>

// Shared mutex to protect all M5Unified subsystem access (Display, Touch, Power, etc.)
// M5GFX is not thread-safe; concurrent M5.update() and M5.Display calls from
// different FreeRTOS tasks cause spinlock assertion failures.
namespace HalM5Mutex {

inline SemaphoreHandle_t& get() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

inline void lock() { xSemaphoreTake(get(), portMAX_DELAY); }
inline void unlock() { xSemaphoreGive(get()); }

}  // namespace HalM5Mutex
