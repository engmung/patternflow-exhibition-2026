#pragma once
#include "../flowlocal_config.h"

// The exhibit build has no media at all: no USB-host stick (the native USB
// jack is the laptop's MIDI device there) and no SD. Nothing in its
// composition (flowlocal.ino) includes this header, and this makes sure it
// stays that way, so EspUsbHost can never be linked in or wait on a stick.
#if FL_EXHIBIT
#  error "fl_store.h (USB-host / SD media) is not part of the exhibit build"
#endif

#ifndef FL_USE_SD
#  define FL_USE_SD 0
#endif

#if FL_USE_SD
#  include "fl_sd.h"
namespace FlStore = FlSd;
#else
#  include "fl_usb.h"
namespace FlStore = FlUsb;
#endif
