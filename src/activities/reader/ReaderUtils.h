#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = !SETTINGS.longPressChapterSkip;
  const bool prev = usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) ||
                                input.wasPressed(MappedInputManager::Button::Left))
                             : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                input.wasReleased(MappedInputManager::Button::Left));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasPressed(MappedInputManager::Button::Right))
                             : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                input.wasReleased(MappedInputManager::Button::Right));
  return {prev, next};
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

// Single-pass grayscale anti-aliasing. Sets GRAYSCALE_DIRECT mode so the
// render callback writes EPD gray values (0-3) directly into the 8bpp
// framebuffer. No LSB/MSB bitplane conversion needed — EPD_Painter accepts
// 0-3 natively. Caller is responsible for displaying the buffer afterwards.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  renderFn();
  renderer.setRenderMode(GfxRenderer::BW);
}

}  // namespace ReaderUtils
