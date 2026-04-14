#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEM_COUNT = 3;
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  // Reset selection
  selectedIndex = 0;

  // Trigger first update
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Handle confirm button - select current option
#if CROSSPOINT_PAPERS3
  // During a hold, track finger position so the gray bar follows the touched item.
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.isPressed(MappedInputManager::Button::Left) ||
      mappedInput.isPressed(MappedInputManager::Button::Right)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int16_t touchY = mappedInput.getTouchY();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int rowHeight = metrics.listWithSubtitleRowHeight;
    if (touchY >= contentTop) {
      const int tappedRow = (touchY - contentTop) / rowHeight;
      if (tappedRow >= 0 && tappedRow < MENU_ITEM_COUNT && tappedRow != selectedIndex) {
        selectedIndex = tappedRow;
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasTapped()) {
    // Tap-to-select: map touch Y to menu item
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int16_t touchY = mappedInput.getTouchY();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int rowHeight = metrics.listWithSubtitleRowHeight;
    if (touchY >= contentTop) {
      int tappedRow = (touchY - contentTop) / rowHeight;
      if (tappedRow >= 0 && tappedRow < MENU_ITEM_COUNT) {
        selectedIndex = tappedRow;
      }
    }
#else
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
#endif
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) {
      mode = NetworkMode::CONNECT_CALIBRE;
    } else if (selectedIndex == 2) {
      mode = NetworkMode::CREATE_HOTSPOT;
    }
    onModeSelected(mode);
    return;
  }

  // Handle navigation — press-only on touch devices to avoid continuous scroll
  // advancing selectedIndex during a hold and showing the wrong highlighted row.
#if CROSSPOINT_PAPERS3
  buttonNavigator.onNextPress([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousPress([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
#else
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
#endif
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FILE_TRANSFER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  // Menu items and descriptions
  static constexpr StrId menuItems[MENU_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS,
                                                       StrId::STR_CREATE_HOTSPOT};
  static constexpr StrId menuDescs[MENU_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC,
                                                       StrId::STR_HOTSPOT_DESC};
  static constexpr UIIcon menuIcons[MENU_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot};

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEM_COUNT), selectedIndex,
      [](int index) { return std::string(I18N.get(menuItems[index])); },
      [](int index) { return std::string(I18N.get(menuDescs[index])); }, [](int index) { return menuIcons[index]; });

  // Draw help text at bottom
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
