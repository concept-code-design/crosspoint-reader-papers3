#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct TodoItem {
  std::string text;       // display text (without checkbox prefix)
  bool checked = false;
  bool isSection = false;
  int rawLineIndex = -1;  // index into rawLines[] for checkbox items; -1 for sections
};

class TodoActivity final : public Activity {
  std::vector<std::string> rawLines;
  std::vector<TodoItem> items;
  int selectorIndex = 0;
  int viewOffset = 0;
  bool dirty = false;

  ButtonNavigator buttonNavigator;

  void loadFile();
  void saveFile() const;
  void parseLine(int lineIndex);
  int firstSelectableIndex() const;
  int nextSelectableIndex(int from, int dir) const;
  void adjustViewOffset(int pageItems);
  int computePageItems() const;

 public:
  explicit TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Todo", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
