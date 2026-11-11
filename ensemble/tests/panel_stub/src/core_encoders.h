// Host stand-in for src/core_encoders.h: the input frame, field for field
// as the device has it, and the four buttons' one flag the menu lock sets.
#pragma once
#include <cstdint>

struct InputFrame {
  long knobs[4];
  int knobDeltas[4];
  bool btnPressed[4];
  bool btnHeld[4];
  uint32_t now;
  bool knobAudioActive[4];
  float knobAudioValue[4];
  bool paramAbsoluteActive[4];
  uint16_t paramAbsolute[4];
};

struct Button {
  bool longPressFired = false;
  bool clickedFlag = false;
};
inline Button btn1, btn2, btn3, btn4;
