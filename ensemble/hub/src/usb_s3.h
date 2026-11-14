// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble hub - looking after the ESP32-S3's USB controller
//
// The hub's USB jack carries two pipes to the computer at once: the
// console's serial port and MIDI. The driver the Arduino core 2.0.x is
// built with handles one busy pipe; with two it loses transfers and mixes
// their bytes. usb_s3.cpp says how, and holds what the hub does about it.
// main.cpp only has to:
//
//   usb_s3::carry(endpoint)   once per pipe to the computer, before begin()
//   usb_s3::begin()           after USB.begin()
//   usb_s3::tick()            every pass of loop()
//   usb_s3::ready()           before handing a pipe anything
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace usb_s3 {

struct Counts {
  uint32_t laidOut = 0;   // times the transmit FIFOs were put where they belong (once per enumeration)
  uint32_t rearmed = 0;   // transfers whose "fill the FIFO" interrupt had been lost, switched back on
  uint32_t unstuck = 0;   // transfers waiting behind bytes left in their FIFO, the FIFO emptied
  uint32_t failed = 0;    // repairs that could not be made
};

// An endpoint that carries data to the computer (0x84, 0x81 ...).
void carry(uint8_t epIn);

// After USB.begin(), on the task that called it. False: TinyUSB's task is
// not on this core (platformio.ini lost its --wrap line, or the core names
// the task differently now); everything still runs, less safely.
bool begin();

// Every pass of loop(): puts the FIFOs in place after the computer has
// configured the device, and repairs a transfer that has stopped.
void tick();

// The computer has configured the device and the FIFOs are in place:
// the pipes may be given data.
bool ready();

const Counts& counts();

// The controller's registers as JSON, for /api/usb in a debug build.
size_t dump(char* out, size_t cap);

}  // namespace usb_s3
