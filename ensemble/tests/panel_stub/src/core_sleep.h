// Host stand-in for src/core_sleep.h.
#pragma once
namespace stub { inline bool sleeping = false; }
namespace PatternflowSleep {
inline bool isSleeping() { return stub::sleeping; }
}
