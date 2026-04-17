#include "TodoActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

static constexpr const char* TODO_PATH = "/todo/todo.md";
static constexpr int CHECKBOX_SIZE = 12;

// ─── File I/O ────────────────────────────────────────────────────────────────

void TodoActivity::parseLine(const int lineIndex) {
  const std::string& line = rawLines[lineIndex];

  // Section header: ## text
  if (line.size() >= 3 && line[0] == '#' && line[1] == '#' && line[2] == ' ') {
    std::string text = line.substr(3);
    items.push_back({text, false, true, -1});
    return;
  }

  // Checkbox item: - [ ] text  or  - [x] text  or  - [X] text
  if (line.size() >= 6 && line[0] == '-' && line[1] == ' ' && line[2] == '[') {
    const char mark = line[3];
    if ((mark == ' ' || mark == 'x' || mark == 'X') && line[4] == ']' && line[5] == ' ') {
      const bool checked = (mark == 'x' || mark == 'X');
      items.push_back({line.substr(6), checked, false, lineIndex});
    }
  }
}

void TodoActivity::loadFile() {
  rawLines.clear();
  items.clear();

  if (!Storage.exists(TODO_PATH)) {
    LOG_ERR("TODO", "File not found: %s", TODO_PATH);
    return;
  }

  // readFile() holds the StorageLock for its entire duration — same pattern as
  // CrossPointSettings, WifiCredentialStore, RecentBooksStore.
  const String content = Storage.readFile(TODO_PATH);
  if (content.isEmpty()) {
    LOG_DBG("TODO", "File empty: %s", TODO_PATH);
    return;
  }

  // Split into lines, preserving rawLines for round-trip save
  int start = 0;
  const int len = static_cast<int>(content.length());
  while (start <= len) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = len;

    String line = content.substring(start, nl);
    // Strip trailing \r (Windows line endings)
    if (line.length() > 0 && line[static_cast<int>(line.length()) - 1] == '\r') {
      line = line.substring(0, static_cast<int>(line.length()) - 1);
    }

    rawLines.push_back(std::string(line.c_str()));
    parseLine(static_cast<int>(rawLines.size()) - 1);

    if (nl >= len) break;
    start = nl + 1;
  }

  LOG_DBG("TODO", "Loaded %d lines, %d items from %s", static_cast<int>(rawLines.size()),
          static_cast<int>(items.size()), TODO_PATH);

  selectorIndex = firstSelectableIndex();
  viewOffset = 0;
}

void TodoActivity::saveFile() const {
  // Rebuild file content: update checkbox lines, keep all other lines unchanged
  String content = "";
  std::vector<std::string> updated = rawLines;
  for (const auto& item : items) {
    if (item.rawLineIndex >= 0 && item.rawLineIndex < static_cast<int>(updated.size())) {
      updated[item.rawLineIndex] = (item.checked ? "- [x] " : "- [ ] ") + item.text;
    }
  }
  for (const auto& line : updated) {
    content += line.c_str();
    content += "\n";
  }

  if (!Storage.writeFile(TODO_PATH, content)) {
    LOG_ERR("TODO", "Failed to save %s", TODO_PATH);
  } else {
    LOG_DBG("TODO", "Saved %s", TODO_PATH);
  }
}

// ─── Navigation helpers ───────────────────────────────────────────────────────

int TodoActivity::firstSelectableIndex() const {
  for (int i = 0; i < static_cast<int>(items.size()); i++) {
    if (!items[i].isSection) return i;
  }
  return 0;
}

int TodoActivity::nextSelectableIndex(const int from, const int dir) const {
  const int n = static_cast<int>(items.size());
  int i = from + dir;
  while (i >= 0 && i < n) {
    if (!items[i].isSection) return i;
    i += dir;
  }
  return from;  // no other selectable item in that direction
}

int TodoActivity::computePageItems() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return contentHeight / metrics.listRowHeight;
}

void TodoActivity::adjustViewOffset(const int pageItems) {
  if (selectorIndex < viewOffset) {
    viewOffset = selectorIndex;
  } else if (selectorIndex >= viewOffset + pageItems) {
    viewOffset = selectorIndex - pageItems + 1;
  }
  const int maxOffset = std::max(0, static_cast<int>(items.size()) - pageItems);
  viewOffset = std::max(0, std::min(viewOffset, maxOffset));
}

// ─── Activity lifecycle ───────────────────────────────────────────────────────

void TodoActivity::onEnter() {
  Activity::onEnter();
  dirty = false;
  loadFile();
  requestUpdate();
}

void TodoActivity::onExit() {
  if (dirty) saveFile();
  Activity::onExit();
}

// ─── Input loop ───────────────────────────────────────────────────────────────

void TodoActivity::loop() {
  const int pageItems = computePageItems();

#if CROSSPOINT_PAPERS3
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;

  // Hold: keep gray bar tracking the finger
  if (!items.empty() &&
      (mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
       mappedInput.isPressed(MappedInputManager::Button::Left) ||
       mappedInput.isPressed(MappedInputManager::Button::Right))) {
    const int16_t touchY = mappedInput.getTouchY();
    if (touchY >= contentTop) {
      const int row = (touchY - contentTop) / rowHeight;
      const int idx = viewOffset + row;
      if (idx >= 0 && idx < static_cast<int>(items.size()) && !items[idx].isSection &&
          idx != selectorIndex) {
        selectorIndex = idx;
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasTapped()) {
    const int16_t touchY = mappedInput.getTouchY();
    if (touchY >= contentTop && !items.empty()) {
      const int row = (touchY - contentTop) / rowHeight;
      const int idx = viewOffset + row;
      if (idx >= 0 && idx < static_cast<int>(items.size()) && !items[idx].isSection) {
        selectorIndex = idx;
        // One-directional: can only check, not uncheck
        if (!items[selectorIndex].checked) {
          items[selectorIndex].checked = true;
          dirty = true;
        }
        requestUpdate();
        return;
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selectorIndex = nextSelectableIndex(selectorIndex, -1);
    adjustViewOffset(pageItems);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectorIndex = nextSelectableIndex(selectorIndex, 1);
    adjustViewOffset(pageItems);
    requestUpdate();
    return;
  }
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!items.empty() && selectorIndex < static_cast<int>(items.size()) &&
        !items[selectorIndex].isSection && !items[selectorIndex].checked) {
      items[selectorIndex].checked = true;
      dirty = true;
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  buttonNavigator.onNextRelease([this, pageItems] {
    selectorIndex = nextSelectableIndex(selectorIndex, 1);
    adjustViewOffset(pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, pageItems] {
    selectorIndex = nextSelectableIndex(selectorIndex, -1);
    adjustViewOffset(pageItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, pageItems] {
    const int next = nextSelectableIndex(selectorIndex, pageItems);
    selectorIndex = next;
    adjustViewOffset(pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, pageItems] {
    const int prev = nextSelectableIndex(selectorIndex, -pageItems);
    selectorIndex = prev;
    adjustViewOffset(pageItems);
    requestUpdate();
  });
#endif
}

// ─── Render ───────────────────────────────────────────────────────────────────

void TodoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int pageItems = contentHeight / rowHeight;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODO));

  if (items.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_TODO_ITEMS));
  } else {
    const int itemFontH = renderer.getLineHeight(UI_10_FONT_ID);
    const int sectionFontH = renderer.getLineHeight(UI_12_FONT_ID);
    const int textX = metrics.contentSidePadding + CHECKBOX_SIZE + 6;

    int y = contentTop;
    for (int i = viewOffset; i < static_cast<int>(items.size()) && y < contentTop + contentHeight; i++) {
      const auto& item = items[i];

      if (item.isSection) {
        // Section header: bold, separator line at bottom of row
        const int textY = y + (rowHeight - sectionFontH) / 2;
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, textY, item.text.c_str(), true,
                          EpdFontFamily::BOLD);
        renderer.drawLine(metrics.contentSidePadding, y + rowHeight - 1,
                          pageWidth - metrics.contentSidePadding, y + rowHeight - 1);
      } else {
        const bool selected = (i == selectorIndex);

        // Selection highlight (black row)
        if (selected) {
          renderer.fillRect(0, y, pageWidth, rowHeight);
        }

        // inv = true when selected so drawing is white-on-black
        const int cbY = y + (rowHeight - CHECKBOX_SIZE) / 2;
        const int textY = y + (rowHeight - itemFontH) / 2;

        // Checkbox: filled square for checked, outline for unchecked
        if (item.checked) {
          renderer.fillRect(metrics.contentSidePadding, cbY, CHECKBOX_SIZE, CHECKBOX_SIZE, !selected);
        } else {
          renderer.drawRect(metrics.contentSidePadding, cbY, CHECKBOX_SIZE, CHECKBOX_SIZE, !selected);
        }

        // Item text
        renderer.drawText(UI_10_FONT_ID, textX, textY, item.text.c_str(), !selected);

        // Strikethrough for completed items
        if (item.checked) {
          const int textW = renderer.getTextWidth(UI_10_FONT_ID, item.text.c_str());
          const int lineY = textY + itemFontH / 2;
          renderer.drawLine(textX, lineY, textX + textW, lineY, !selected);
        }
      }

      y += rowHeight;
    }

    // Scroll bar if content overflows
    if (static_cast<int>(items.size()) > pageItems) {
      const int barX = pageWidth - metrics.scrollBarRightOffset - metrics.scrollBarWidth;
      const int totalItems = static_cast<int>(items.size());
      const int barH = contentHeight * pageItems / totalItems;
      const int barY = contentTop + contentHeight * viewOffset / totalItems;
      renderer.fillRect(barX, barY, metrics.scrollBarWidth, barH);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
