// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble hub - looking after the ESP32-S3's USB controller
//
// What was seen (2026-11-14, six panels, the console and MIDI on the USB
// jack together): after seconds or minutes one of the two pipes stopped for
// good. The hub had handed TinyUSB a packet, the computer kept asking, and
// nothing went until the hub restarted. Now and then a console line arrived
// with four bytes of a MIDI message in the middle of it.
//
// Both come from the USB driver inside the Arduino core 2.0.x (a compiled
// library; its source is esp32-arduino-lib-builder, dcd_esp32sx.c). Three
// things about it, and what this file does for each:
//
// 1. It is not safe on two cores. A packet reaches the wire by an interrupt
//    ("this endpoint's transmit FIFO is empty: fill it") that is switched on
//    per endpoint in one register all endpoints share. The driver switches
//    it on with a plain |= from whichever task starts a transfer and off
//    with a plain &= ~ in its interrupt handler; and the handler, having
//    seen one transfer complete, goes on to look at the endpoint's size
//    register and bookkeeping while TinyUSB's task - which the core lets
//    run on either core - is already writing the next transfer into them.
//    The result is a transfer whose switch is off, or one whose bytes went
//    into the FIFO at a moment the controller did not count them: either
//    way it never completes. With one pipe this is rare enough that the
//    core gets away with it; with two it took under a minute.
//
//    -> Everything that touches the controller runs on one core: the
//       driver's interrupt is allocated on the core USB.begin() is called
//       from, and TinyUSB's task is created there too (the linker's --wrap
//       in platformio.ini sends the core's xTaskCreate through
//       __wrap_xTaskCreatePinnedToCore below, which pins the task named
//       "usbd"). main.cpp puts its own USB writer on the same core. On one
//       core the handler cannot run beside the task it disagrees with.
//
// 2. What is left of 1 (a task interrupted halfway through its |= by a
//    task of higher priority that starts a transfer on the other pipe) and
//    anything of the kind not thought of:
//
//    -> tick() looks at each pipe every pass of loop(). A transfer under
//       way with bytes not yet in the FIFO and its switch off gets the
//       switch back on ("rearmed"). One with its switch on that still does
//       not move because bytes were left in the FIFO gets the FIFO emptied
//       ("unstuck"). Both are counted in /api/status; neither loses data.
//
// 3. It puts the transmit FIFOs in the wrong place. The controller has 200
//    words of FIFO memory to divide; the driver computes a place for each
//    endpoint's FIFO and writes it into the register of the FIFO with the
//    endpoint's number instead of the FIFO it gave the endpoint. The serial
//    port alone ends up in a FIFO nobody placed (the reset value: 256 words
//    starting at word 512, of 200), which wraps onto the memory of the
//    receive FIFO; with MIDI beside it, MIDI's FIFO is the unplaced one and
//    lies across everything, the serial port's included - hence the MIDI
//    bytes inside console lines.
//
//    -> When the computer has configured the device, tick() gives each
//       pipe's FIFO its own stretch of the memory after the receive FIFO
//       and endpoint 0's, before anything is sent (ready() is false until
//       then, and main.cpp sends nothing while it is).
//
// The Arduino core 3.x has another driver without these; the hub stays on
// 2.0.17 because the panel firmware does (platformio.ini).
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#include "usb_s3.h"

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp32-hal-tinyusb.h"   // tud_mounted (tusb.h)
#include "device/usbd_pvt.h"     // usbd_edpt_busy: has TinyUSB a transfer under way on an endpoint
#include "soc/usb_struct.h"      // USB0: the controller's registers

#include "../hub_config.h"

// ── 1. TinyUSB's task on the core of the driver's interrupt ────────────
namespace {
volatile bool usbTaskPinned = false;
}

extern "C" {

BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t code, const char* const name, const uint32_t stack,
                                          void* const arg, UBaseType_t priority, TaskHandle_t* const handle,
                                          const BaseType_t core);

// Every task the firmware creates comes through here (-Wl,--wrap). All but
// one go on unchanged.
BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t code, const char* const name, const uint32_t stack,
                                          void* const arg, UBaseType_t priority, TaskHandle_t* const handle,
                                          const BaseType_t core) {
  BaseType_t where = core;
  if (HUB_USB_ONE_CORE && name && strcmp(name, "usbd") == 0) {
    // Created inside USB.begin(), as the driver's interrupt was a moment before: here.
    where = xPortGetCoreID();
    usbTaskPinned = true;
  }
  return __real_xTaskCreatePinnedToCore(code, name, stack, arg, priority, handle, where);
}

}  // extern "C"

namespace usb_s3 {
namespace {

// The registers, by the names of the controller's manual (Synopsys DWC2).
constexpr uint32_t CTL_ENABLED = 1u << 31;      // DIEPCTLn: a transfer is programmed and not finished
constexpr uint32_t CTL_SET_NAK = 1u << 27;      //           answer the computer "nothing yet"
constexpr uint32_t CTL_CLEAR_NAK = 1u << 26;
constexpr int CTL_FIFO_SHIFT = 22;              //           which transmit FIFO the endpoint uses (4 bits)
constexpr uint32_t CTL_ACTIVE = 1u << 15;       //           the endpoint is open
constexpr uint32_t INT_NAK_EFFECTIVE = 1u << 6; // DIEPINTn: SET_NAK has taken hold
constexpr uint32_t INT_FIFO_EMPTY = 1u << 7;    //           the endpoint's FIFO is empty
constexpr uint32_t SIZE_BYTES = 0x7FFFF;        // DIEPTSIZn: bytes of the transfer not yet in the FIFO
constexpr int SIZE_PACKETS_SHIFT = 19;          //            packets still to go (10 bits)
constexpr uint32_t RST_FLUSH_TX = 1u << 5;      // GRSTCTL: empty a transmit FIFO ...
constexpr int RST_FIFO_SHIFT = 6;               //          ... this one (5 bits)

constexpr uint32_t FIFO_MIN = 16;               // words: one full packet of 64 bytes
constexpr uint32_t FIFO_MOST = 64;              // more is of no use: the driver puts in a packet at a time
constexpr int MAX_PIPES = 4;                    // the controller has four transmit FIFOs besides endpoint 0's

struct Pipe {
  uint8_t ep = 0;
  uint8_t dropped = 0;    // passes in a row it looked dropped
  uint8_t stale = 0;      // passes in a row it looked stuck behind left-over bytes
};
Pipe pipes[MAX_PIPES];
int pipeCount = 0;
Pipe control;             // endpoint 0: answers to the computer's requests

Counts tally;
volatile bool isReady = false;
bool layoutGivenUp = false;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

inline usb_in_endpoint_t& reg(uint8_t ep) { return USB0.in_ep_reg[ep & 0x0F]; }
inline uint32_t fifoOf(uint8_t ep) { return (reg(ep).diepctl >> CTL_FIFO_SHIFT) & 0xF; }

// ── 3. Where each pipe's FIFO should lie: (words << 16) | first word ───
// After the receive FIFO and endpoint 0's, which the driver does place.
uint32_t placeFor(int index) {
  const uint32_t total = USB0.ghwcfg3 >> 16;
  const uint32_t from = (USB0.gnptxfsiz & 0xFFFF) + (USB0.gnptxfsiz >> 16);
  if (!pipeCount || total <= from) return 0;
  uint32_t each = (total - from) / (uint32_t)pipeCount;
  if (each > FIFO_MOST) each = FIFO_MOST;
  if (each < FIFO_MIN) return 0;
  return (each << 16) | (from + each * (uint32_t)index);
}

// Empties one transmit FIFO. The caller holds the lock.
bool flushFifo(uint32_t fifo) {
  const uint32_t keep = USB0.grstctl & (0x1Fu << RST_FIFO_SHIFT);
  USB0.grstctl = RST_FLUSH_TX | (fifo << RST_FIFO_SHIFT);
  bool done = false;
  for (int spin = 0; spin < 50000 && !done; spin++) done = !(USB0.grstctl & RST_FLUSH_TX);
  USB0.grstctl = keep;
  return done;
}

bool inPlace() {
  for (int i = 0; i < pipeCount; i++) {
    const uint32_t ctl = reg(pipes[i].ep).diepctl, fifo = fifoOf(pipes[i].ep);
    if (!(ctl & CTL_ACTIVE) || fifo < 1 || fifo > 4) return false;
    if (USB0.dieptxf[fifo - 1] != placeFor(i)) return false;
  }
  return true;
}

// Right after the computer configured the device: nothing has been sent on
// these endpoints yet (main.cpp waits for ready()).
bool layOut() {
  bool ok = true;
  portENTER_CRITICAL(&lock);
  for (int i = 0; i < pipeCount && ok; i++) {
    const uint32_t ctl = reg(pipes[i].ep).diepctl, fifo = fifoOf(pipes[i].ep), place = placeFor(i);
    if (!(ctl & CTL_ACTIVE) || (ctl & CTL_ENABLED) || fifo < 1 || fifo > 4 || !place) {
      ok = false;
      break;
    }
    USB0.dieptxf[fifo - 1] = place;
    ok = flushFifo(fifo);
  }
  portEXIT_CRITICAL(&lock);
  return ok;
}

// ── 2. A transfer that has stopped ──────────────────────────────────────
// Bytes were left in the endpoint's FIFO, so "the FIFO is empty: fill it"
// never comes. Hold the endpoint (the computer is told "nothing yet"),
// empty the FIFO, let go: the interrupt comes and the transfer's own bytes
// go in. The caller holds the lock.
bool unstick(uint8_t ep) {
  usb_in_endpoint_t& r = reg(ep);
  r.diepint = INT_NAK_EFFECTIVE;
  r.diepctl |= CTL_SET_NAK;
  for (int spin = 0; spin < 50000 && !(r.diepint & INT_NAK_EFFECTIVE); spin++) {
  }
  const bool emptied = flushFifo(fifoOf(ep));
  r.diepint = INT_NAK_EFFECTIVE;
  r.diepctl |= CTL_CLEAR_NAK;
  USB0.dtknqr4_fifoemptymsk |= 1u << (ep & 0x0F);
  return emptied;
}

void repair(Pipe& p, bool onePacketTransfers) {
  usb_in_endpoint_t& r = reg(p.ep);
  const uint32_t bit = 1u << (p.ep & 0x0F);
  const uint32_t ctl = r.diepctl, size = r.dieptsiz;
  // A transfer under way with bytes that are not in the FIFO yet. In a
  // healthy one that lasts the microseconds until the interrupt has run.
  if (!usbd_edpt_busy(0, p.ep) || !(ctl & CTL_ENABLED) || !(size & SIZE_BYTES)) {
    p.dropped = p.stale = 0;
    return;
  }
  if (!(USB0.dtknqr4_fifoemptymsk & bit)) {
    // The switch is off: nothing is going to put them in. Seen on two
    // passes, so that it is not a transfer being started this instant.
    p.stale = 0;
    if (++p.dropped < 2) return;
    p.dropped = 0;
    portENTER_CRITICAL(&lock);
    USB0.dtknqr4_fifoemptymsk |= bit;
    portEXIT_CRITICAL(&lock);
    tally.rearmed++;
    return;
  }
  p.dropped = 0;
  // The switch is on and still nothing: the FIFO is not empty. For a
  // transfer of one packet (all the hub's are) that is never right - its
  // bytes go in at once or not at all, and the transfer before it was
  // complete, so what is in there belongs to nobody.
  const uint32_t fifo = (ctl >> CTL_FIFO_SHIFT) & 0xF;
  const uint32_t depth = (fifo >= 1 && fifo <= 4) ? USB0.dieptxf[fifo - 1] >> 16 : 0;
  const bool leftOvers = depth && (r.dtxfsts & 0xFFFF) < depth && !(r.diepint & INT_FIFO_EMPTY);
  if (!onePacketTransfers || !leftOvers || ((size >> SIZE_PACKETS_SHIFT) & 0x3FF) != 1) {
    p.stale = 0;
    return;
  }
  if (++p.stale < 3) return;
  p.stale = 0;
  portENTER_CRITICAL(&lock);
  const bool ok = unstick(p.ep);
  portEXIT_CRITICAL(&lock);
  if (ok) tally.unstuck++;
  else tally.failed++;
}

}  // namespace

void carry(uint8_t epIn) {
  if (pipeCount >= MAX_PIPES || !(epIn & 0x0F)) return;
  pipes[pipeCount++].ep = (uint8_t)(0x80 | epIn);
}

bool begin() {
  control.ep = 0x80;
  return !HUB_USB_ONE_CORE || usbTaskPinned;
}

void tick() {
  if (!tud_mounted()) {
    isReady = false;
    layoutGivenUp = false;
    return;
  }
  if (!layoutGivenUp && !inPlace()) {
    isReady = false;
    if (layOut() && inPlace()) {
      tally.laidOut++;
    } else if (!placeFor(0)) {
      // No room to divide (not this controller): leave the FIFOs where the driver put them.
      layoutGivenUp = true;
      tally.failed++;
    } else {
      return;   // an endpoint was not ready to be moved: next pass
    }
  }
  isReady = true;
  repair(control, false);
  for (int i = 0; i < pipeCount; i++) repair(pipes[i], true);
}

bool ready() { return isReady; }

const Counts& counts() { return tally; }

size_t dump(char* out, size_t cap) {
  int n = snprintf(out, cap,
                   "{\"pinned\":%d,\"ready\":%d,\"laid_out\":%lu,\"rearmed\":%lu,\"unstuck\":%lu,\"failed\":%lu,"
                   "\"fifo_words\":%lu,\"rx\":%lu,\"ep0\":\"%lu@%lu\",\"gintsts\":\"%08lx\",\"gintmsk\":\"%08lx\","
                   "\"dcfg\":\"%08lx\",\"dctl\":\"%08lx\",\"dsts\":\"%08lx\",\"daint\":\"%08lx\",\"daintmsk\":\"%08lx\","
                   "\"empmsk\":\"%08lx\",\"grstctl\":\"%08lx\",\"fifos\":[",
                   (int)usbTaskPinned, (int)isReady, (unsigned long)tally.laidOut, (unsigned long)tally.rearmed,
                   (unsigned long)tally.unstuck, (unsigned long)tally.failed, (unsigned long)(USB0.ghwcfg3 >> 16),
                   (unsigned long)(USB0.grxfsiz & 0xFFFF), (unsigned long)(USB0.gnptxfsiz >> 16),
                   (unsigned long)(USB0.gnptxfsiz & 0xFFFF), (unsigned long)USB0.gintsts, (unsigned long)USB0.gintmsk,
                   (unsigned long)USB0.dcfg, (unsigned long)USB0.dctl, (unsigned long)USB0.dsts,
                   (unsigned long)USB0.daint, (unsigned long)USB0.daintmsk,
                   (unsigned long)USB0.dtknqr4_fifoemptymsk, (unsigned long)USB0.grstctl);
  for (int f = 0; f < 4 && n > 0 && (size_t)n < cap; f++) {
    n += snprintf(out + n, cap - n, "%s\"%lu@%lu\"", f ? "," : "", (unsigned long)(USB0.dieptxf[f] >> 16),
                  (unsigned long)(USB0.dieptxf[f] & 0xFFFF));
  }
  if (n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - n, "]");
  for (int i = -1; i < pipeCount && n > 0 && (size_t)n < cap; i++) {
    const uint8_t ep = i < 0 ? 0x80 : pipes[i].ep;
    const usb_in_endpoint_t& r = reg(ep);
    n += snprintf(out + n, cap - n,
                  ",\"in%d\":{\"ctl\":\"%08lx\",\"int\":\"%08lx\",\"tsiz\":\"%08lx\",\"room\":%lu,\"busy\":%d}",
                  ep & 0x0F, (unsigned long)r.diepctl, (unsigned long)r.diepint, (unsigned long)r.dieptsiz,
                  (unsigned long)(r.dtxfsts & 0xFFFF), (int)usbd_edpt_busy(0, ep));
  }
  if (n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - n, "}");
  return n > 0 ? (size_t)n : 0;
}

}  // namespace usb_s3
