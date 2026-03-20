#pragma once
#include <GfxRenderer.h>

#include <functional>
#include <string>
#include <utility>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Reusable keyboard entry activity for text input.
 * Can be started from any activity that needs text entry via startActivityForResult()
 */
class KeyboardEntryActivity : public Activity {
 public:
  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param mappedInput Reference to MappedInputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   */
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "",
                                 const size_t maxLength = 0, const bool isPassword = false)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        maxLength(maxLength),
        isPassword(isPassword) {}

  // Activity overrides
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  std::string text;
  size_t maxLength;
  bool isPassword;

  ButtonNavigator buttonNavigator;

  // Keyboard state
  int selectedRow = 0;
  int selectedCol = 0;
  int shiftState = 0;  // 0 = lower, 1 = upper, 2 = shift lock (X4) / numbers (Paper S3)

  // Handlers
  void onComplete(std::string text);
  void onCancel();

#if CROSSPOINT_PAPERS3
  // Paper S3: 4-row touch keyboard with tap-to-type
  static constexpr int NUM_ROWS = 4;
  static constexpr int KEYS_PER_ROW = 10;

  // Key geometry (540px screen)
  static constexpr int TK_KEY_W = 50;
  static constexpr int TK_KEY_H = 80;
  static constexpr int TK_SPACING = 4;
  static constexpr int TK_WIDE_W = 75;   // shift / backspace
  static constexpr int TK_MODE_W = 100;  // mode toggle / OK
  static constexpr int TK_BOTTOM_PAD = 10;

  // Layouts: [mode][row] — mode 0=lower, 1=upper, 2=numbers
  static const char* const touchKb[3][NUM_ROWS];

  int touchRowCharCount(int row) const;
  bool handleTouchAt(int16_t x, int16_t y);
  int getKeyboardStartY() const;
#else
  // X4: 5-row button-navigated keyboard
  static constexpr int NUM_ROWS = 5;
  static constexpr int KEYS_PER_ROW = 13;
  static const char* const keyboard[NUM_ROWS];
  static const char* const keyboardShift[NUM_ROWS];
  static const char* const shiftString[3];

  static constexpr int SPECIAL_ROW = 4;
  static constexpr int SHIFT_COL = 0;
  static constexpr int SPACE_COL = 2;
  static constexpr int BACKSPACE_COL = 7;
  static constexpr int DONE_COL = 9;

  char getSelectedChar() const;
  bool handleKeyPress();  // false if onComplete was triggered
  int getRowLength(int row) const;
#endif
};
