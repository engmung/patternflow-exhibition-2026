// Host stand-in for features/pf_feature.h: the descriptor a feature fills
// in, with the fields in the device's order. A descriptor is a positional
// initializer, so the ORDER is the contract; ensemble/tests/run_tests.py
// --patternflow <checkout> compares this list with the real header's.
#pragma once
#include "../Arduino.h"
#include "../src/core_encoders.h"

struct PFFeatureFrame {
  float dt;
  const char* patternName;
  bool running;
  bool chromeVisible;
  int patternIndex;
  int appMode;
};

struct PFFeature {
  const char* name;
  const char* cap;
  void (*setup)();
  void (*onNetwork)();
  void (*loop)(const PFFeatureFrame&);
  void (*observeFrame)(const InputFrame&, const PFFeatureFrame&);
  void (*fillInput)(InputFrame&);
  void (*onUserInput)();
  bool (*claimsPattern)();
  bool (*takePattern)(int* idx);
  void (*onSleep)(bool sleeping);
  bool (*requestSleep)(bool* sleeping);
  const char* shortName;
  bool (*isRuntimeEnabled)();
  void (*setRuntimeEnabled)(bool);
  void (*appendStatus)(String&);
  void (*drawOverlay)(const PFFeatureFrame&);
  const char* navPath;
  const char* navLabel;
  const char* navDesc;
  const uint8_t* (*composeFrame)(const uint8_t* frame, int w, int h);
  bool (*handleSelectInput)(InputFrame&);
  bool (*drawSelect)();
  void (*decorateSelect)();
};
