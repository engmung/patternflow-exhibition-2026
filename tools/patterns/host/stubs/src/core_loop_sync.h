// Host stand-in: one thread, always the frame task.
#pragma once
namespace PFLoopSync {
inline bool onLoopTask() { return true; }
}
