// Host stand-in for src/core_ui_text.h.
#pragma once
#include "core_display.h"
namespace PatternflowUiText {
inline void useMessageFont() {}
inline void useDefaultFont() {}
inline void drawLine(void (*chooseFont)(), const char* text, int, uint16_t) {
  chooseFont();
  stub::drawn = text;
}
}  // namespace PatternflowUiText
