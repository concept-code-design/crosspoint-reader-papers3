#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  void clearState() const { gpio.clearState(); }

  // Raw touch coordinate access for tap-to-select navigation
  int16_t getTouchX() const { return gpio.getLastTouchX(); }
  int16_t getTouchY() const { return gpio.getLastTouchY(); }
  // Returns true if any touch in the content area was released (not Back zone).
  // Use this for list/menu activities where any tap should select an item.
  bool wasContentAreaTapped() const {
    return gpio.wasAnyReleased() && !gpio.wasReleased(HalGPIO::BTN_BACK);
  }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
