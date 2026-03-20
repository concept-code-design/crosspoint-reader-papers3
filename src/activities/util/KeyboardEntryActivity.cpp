#include "KeyboardEntryActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// =============================================================================
#if CROSSPOINT_PAPERS3
// Paper S3 touch keyboard — 4-row layout, tap-to-type
// =============================================================================

// Layouts: [mode][row]  mode 0=lower, 1=upper, 2=numbers
const char* const KeyboardEntryActivity::touchKb[3][NUM_ROWS] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm", ""},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", ""},
    {"1234567890", "-/:;()$&@\"", ".,?!'", ""},
};

int KeyboardEntryActivity::touchRowCharCount(const int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;
  const int mode = (shiftState == 2) ? 2 : shiftState;
  return static_cast<int>(strlen(touchKb[mode][row]));
}

int KeyboardEntryActivity::getKeyboardStartY() const {
  const int screenH = renderer.getScreenHeight();
  const int kbHeight = NUM_ROWS * TK_KEY_H + (NUM_ROWS - 1) * TK_SPACING;
  return screenH - TK_BOTTOM_PAD - kbHeight;
}

bool KeyboardEntryActivity::handleTouchAt(const int16_t tx, const int16_t ty) {
  const int screenW = renderer.getScreenWidth();
  const int kbY = getKeyboardStartY();
  const int mode = (shiftState == 2) ? 2 : shiftState;
  const char* const* layout = touchKb[mode];

  // Determine which row
  if (ty < kbY) return false;
  const int rowIdx = (ty - kbY) / (TK_KEY_H + TK_SPACING);
  if (rowIdx < 0 || rowIdx >= NUM_ROWS) return false;
  const int rowY = kbY + rowIdx * (TK_KEY_H + TK_SPACING);
  if (ty > rowY + TK_KEY_H) return false;  // in the gap

  // ---- Row 3: MODE | SPACE | OK ----
  if (rowIdx == 3) {
    const int margin = 4;
    const int spaceW = screenW - 2 * margin - 2 * TK_MODE_W - 2 * TK_SPACING;
    int cx = margin;
    // MODE button
    if (tx >= cx && tx < cx + TK_MODE_W) {
      shiftState = (shiftState == 2) ? 0 : 2;
      return true;
    }
    cx += TK_MODE_W + TK_SPACING;
    // SPACE
    if (tx >= cx && tx < cx + spaceW) {
      if (maxLength == 0 || text.length() < maxLength) text += ' ';
      return true;
    }
    cx += spaceW + TK_SPACING;
    // OK
    if (tx >= cx && tx < cx + TK_MODE_W) {
      onComplete(text);
      return false;
    }
    return false;
  }

  // ---- Row 2: SHIFT/toggle + chars + BACKSPACE ----
  if (rowIdx == 2) {
    const int nChars = touchRowCharCount(2);
    const int charsW = nChars * TK_KEY_W + (nChars - 1) * TK_SPACING;
    const int totalW = TK_WIDE_W + TK_SPACING + charsW + TK_SPACING + TK_WIDE_W;
    const int margin = (screenW - totalW) / 2;
    int cx = margin;

    // SHIFT / #+= toggle
    if (tx >= cx && tx < cx + TK_WIDE_W) {
      if (shiftState == 2) {
        // In number mode: no shift, ignore (or could add #+= page)
      } else {
        shiftState = (shiftState == 0) ? 1 : 0;
      }
      return true;
    }
    cx += TK_WIDE_W + TK_SPACING;

    // Character keys
    for (int i = 0; i < nChars; i++) {
      const int kx = cx + i * (TK_KEY_W + TK_SPACING);
      if (tx >= kx && tx < kx + TK_KEY_W) {
        const char c = layout[2][i];
        if (c != '\0' && (maxLength == 0 || text.length() < maxLength)) {
          text += c;
          if (shiftState == 1) shiftState = 0;  // auto-unshift
        }
        return true;
      }
    }
    cx += charsW + TK_SPACING;

    // BACKSPACE
    if (tx >= cx && tx < cx + TK_WIDE_W) {
      if (!text.empty()) text.pop_back();
      return true;
    }
    return false;
  }

  // ---- Rows 0-1: regular character keys ----
  const int nKeys = touchRowCharCount(rowIdx);
  if (nKeys <= 0) return false;
  const int totalW = nKeys * TK_KEY_W + (nKeys - 1) * TK_SPACING;
  const int margin = (screenW - totalW) / 2;

  const int col = (tx - margin + TK_SPACING / 2) / (TK_KEY_W + TK_SPACING);
  if (col < 0 || col >= nKeys) return false;

  const char c = layout[rowIdx][col];
  if (c != '\0' && (maxLength == 0 || text.length() < maxLength)) {
    text += c;
    if (shiftState == 1) shiftState = 0;
  }
  return true;
}

void KeyboardEntryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Any single-finger tap in any zone
  const bool anyTap = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                      mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
                      mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (anyTap) {
    const int16_t tx = mappedInput.getTouchX();
    const int16_t ty = mappedInput.getTouchY();
    if (tx >= 0 && ty >= 0) {
      handleTouchAt(tx, ty);
      requestUpdate();
    }
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int screenW = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title.c_str());

  // ---- Input field ----
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 5;

  std::string displayText = isPassword ? std::string(text.length(), '*') : text;
  displayText += "_";

  int inputHeight = 0;
  int lineStartIdx = 0;
  int lineEndIdx = static_cast<int>(displayText.length());
  int textWidth = 0;
  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextWidth(UI_12_FONT_ID, lineText.c_str());
    if (textWidth <= screenW - 2 * metrics.contentSidePadding) {
      renderer.drawCenteredText(UI_12_FONT_ID, inputStartY + inputHeight, lineText.c_str());
      if (lineEndIdx == static_cast<int>(displayText.length())) break;
      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = static_cast<int>(displayText.length());
    } else {
      lineEndIdx -= 1;
    }
  }
  GUI.drawTextField(renderer, Rect{0, inputStartY, screenW, inputHeight}, textWidth);

  // ---- Keyboard ----
  const int kbY = getKeyboardStartY();
  const int mode = (shiftState == 2) ? 2 : shiftState;
  const char* const* layout = touchKb[mode];

  // Rows 0-1: regular character keys
  for (int row = 0; row < 2; row++) {
    const int nKeys = static_cast<int>(strlen(layout[row]));
    const int totalW = nKeys * TK_KEY_W + (nKeys - 1) * TK_SPACING;
    const int margin = (screenW - totalW) / 2;
    const int rowYPos = kbY + row * (TK_KEY_H + TK_SPACING);

    for (int col = 0; col < nKeys; col++) {
      const int kx = margin + col * (TK_KEY_W + TK_SPACING);
      std::string label(1, layout[row][col]);
      GUI.drawKeyboardKey(renderer, Rect{kx, rowYPos, TK_KEY_W, TK_KEY_H}, label.c_str(), false);
    }
  }

  // Row 2: SHIFT/toggle + chars + BACKSPACE
  {
    const int nChars = static_cast<int>(strlen(layout[2]));
    const int charsW = nChars * TK_KEY_W + (nChars - 1) * TK_SPACING;
    const int totalW = TK_WIDE_W + TK_SPACING + charsW + TK_SPACING + TK_WIDE_W;
    const int margin = (screenW - totalW) / 2;
    const int rowYPos = kbY + 2 * (TK_KEY_H + TK_SPACING);
    int cx = margin;

    // Shift / #+= label
    const char* shiftLabel = (shiftState == 2) ? "#+=": (shiftState == 1) ? "SHIFT" : "shift";
    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_WIDE_W, TK_KEY_H}, shiftLabel, shiftState == 1);
    cx += TK_WIDE_W + TK_SPACING;

    for (int i = 0; i < nChars; i++) {
      std::string label(1, layout[2][i]);
      GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_KEY_W, TK_KEY_H}, label.c_str(), false);
      cx += TK_KEY_W + TK_SPACING;
    }

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_WIDE_W, TK_KEY_H}, "<-", false);
  }

  // Row 3: MODE | SPACE | OK
  {
    const int margin = 4;
    const int spaceW = screenW - 2 * margin - 2 * TK_MODE_W - 2 * TK_SPACING;
    const int rowYPos = kbY + 3 * (TK_KEY_H + TK_SPACING);
    int cx = margin;

    const char* modeLabel = (shiftState == 2) ? "ABC" : "123";
    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_MODE_W, TK_KEY_H}, modeLabel, false);
    cx += TK_MODE_W + TK_SPACING;

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, spaceW, TK_KEY_H}, "space", false);
    cx += spaceW + TK_SPACING;

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_MODE_W, TK_KEY_H}, tr(STR_OK_BUTTON), false);
  }

  renderer.displayBuffer();
}

// =============================================================================
#else
// X4 button-navigated keyboard — 5-row layout with cursor navigation
// =============================================================================

const char* const KeyboardEntryActivity::keyboard[NUM_ROWS] = {
    "`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./",
    "^  _____<OK"
};

const char* const KeyboardEntryActivity::keyboardShift[NUM_ROWS] = {"~!@#$%^&*()_+", "QWERTYUIOP{}|", "ASDFGHJKL:\"",
                                                                    "ZXCVBNM<>?", "SPECIAL ROW"};

const char* const KeyboardEntryActivity::shiftString[3] = {"shift", "SHIFT", "LOCK"};

int KeyboardEntryActivity::getRowLength(const int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;
  switch (row) {
    case 0: return 13;
    case 1: return 13;
    case 2: return 11;
    case 3: return 10;
    case 4: return 11;
    default: return 0;
  }
}

char KeyboardEntryActivity::getSelectedChar() const {
  const char* const* layout = shiftState ? keyboardShift : keyboard;
  if (selectedRow < 0 || selectedRow >= NUM_ROWS) return '\0';
  if (selectedCol < 0 || selectedCol >= getRowLength(selectedRow)) return '\0';
  return layout[selectedRow][selectedCol];
}

bool KeyboardEntryActivity::handleKeyPress() {
  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
      shiftState = (shiftState + 1) % 3;
      return true;
    }
    if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
      if (maxLength == 0 || text.length() < maxLength) text += ' ';
      return true;
    }
    if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
      if (!text.empty()) text.pop_back();
      return true;
    }
    if (selectedCol >= DONE_COL) {
      onComplete(text);
      return false;
    }
  }

  const char c = getSelectedChar();
  if (c == '\0') return true;

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;
    if (shiftState == 1) shiftState = 0;
  }
  return true;
}

void KeyboardEntryActivity::loop() {
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selectedRow = ButtonNavigator::previousIndex(selectedRow, NUM_ROWS);
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > maxCol) selectedCol = maxCol;
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    selectedRow = ButtonNavigator::nextIndex(selectedRow, NUM_ROWS);
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > maxCol) selectedCol = maxCol;
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedRow == SPECIAL_ROW) {
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) selectedCol = maxCol;
      else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) selectedCol = SHIFT_COL;
      else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) selectedCol = SPACE_COL;
      else if (selectedCol >= DONE_COL) selectedCol = BACKSPACE_COL;
    } else {
      selectedCol = ButtonNavigator::previousIndex(selectedCol, maxCol + 1);
    }
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedRow == SPECIAL_ROW) {
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) selectedCol = SPACE_COL;
      else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) selectedCol = BACKSPACE_COL;
      else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) selectedCol = DONE_COL;
      else if (selectedCol >= DONE_COL) selectedCol = SHIFT_COL;
    } else {
      selectedCol = ButtonNavigator::nextIndex(selectedCol, maxCol + 1);
    }
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (handleKeyPress()) requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.verticalSpacing * 4;
  int inputHeight = 0;

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }
  displayText += "_";

  int lineStartIdx = 0;
  int lineEndIdx = displayText.length();
  int textWidth = 0;
  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextWidth(UI_12_FONT_ID, lineText.c_str());
    if (textWidth <= pageWidth - 2 * metrics.contentSidePadding) {
      if (metrics.keyboardCenteredText) {
        renderer.drawCenteredText(UI_12_FONT_ID, inputStartY + inputHeight, lineText.c_str());
      } else {
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, inputStartY + inputHeight, lineText.c_str());
      }
      if (lineEndIdx == static_cast<int>(displayText.length())) break;
      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = displayText.length();
    } else {
      lineEndIdx -= 1;
    }
  }

  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, textWidth);

  const int keyboardStartY = metrics.keyboardBottomAligned
                                 ? pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing -
                                       (metrics.keyboardKeyHeight + metrics.keyboardKeySpacing) * NUM_ROWS
                                 : inputStartY + inputHeight + metrics.verticalSpacing * 4;
  const int keyWidth = metrics.keyboardKeyWidth;
  const int keyHeight = metrics.keyboardKeyHeight;
  const int keySpacing = metrics.keyboardKeySpacing;

  const char* const* layout = shiftState ? keyboardShift : keyboard;

  const int maxRowWidth = KEYS_PER_ROW * (keyWidth + keySpacing);
  const int leftMargin = (pageWidth - maxRowWidth) / 2;

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (keyHeight + keySpacing);
    const int startX = leftMargin;

    if (row == SPECIAL_ROW) {
      int currentX = startX;

      const bool shiftSelected = (selectedRow == SPECIAL_ROW && selectedCol >= SHIFT_COL && selectedCol < SPACE_COL);
      const int shiftXWidth = (SPACE_COL - SHIFT_COL) * (keyWidth + keySpacing);
      GUI.drawKeyboardKey(renderer, Rect{currentX, rowY, shiftXWidth, keyHeight}, shiftString[shiftState],
                          shiftSelected);
      currentX += shiftXWidth;

      const bool spaceSelected =
          (selectedRow == SPECIAL_ROW && selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL);
      const int spaceXWidth = (BACKSPACE_COL - SPACE_COL) * (keyWidth + keySpacing);
      GUI.drawKeyboardKey(renderer, Rect{currentX, rowY, spaceXWidth, keyHeight}, "_____", spaceSelected);
      currentX += spaceXWidth;

      const bool bsSelected = (selectedRow == SPECIAL_ROW && selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL);
      const int bsXWidth = (DONE_COL - BACKSPACE_COL) * (keyWidth + keySpacing);
      GUI.drawKeyboardKey(renderer, Rect{currentX, rowY, bsXWidth, keyHeight}, "<-", bsSelected);
      currentX += bsXWidth;

      const bool okSelected = (selectedRow == SPECIAL_ROW && selectedCol >= DONE_COL);
      const int okXWidth = (getRowLength(row) - DONE_COL) * (keyWidth + keySpacing);
      GUI.drawKeyboardKey(renderer, Rect{currentX, rowY, okXWidth, keyHeight}, tr(STR_OK_BUTTON), okSelected);
    } else {
      for (int col = 0; col < getRowLength(row); col++) {
        const char c = layout[row][col];
        std::string keyLabel(1, c);
        const int keyX = startX + col * (keyWidth + keySpacing);
        const bool isSelected = row == selectedRow && col == selectedCol;
        GUI.drawKeyboardKey(renderer, Rect{keyX, rowY, keyWidth, keyHeight}, keyLabel.c_str(), isSelected);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}

#endif
