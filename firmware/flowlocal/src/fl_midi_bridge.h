#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — exhibition MIDI bridge: RTP-MIDI (Wi-Fi) → USB-MIDI (laptop)
//
//   Patternflow panel ──Wi-Fi, RTP-MIDI──▶ FlowLocal ──USB cable──▶ laptop / Live
//
// The panel joins this box's hotspot and invites 192.168.66.1:5004. FlowLocal
// is only the session LISTENER (it never invites anyone), named "FlowLocal".
// Every Control Change, Program Change, Note On and Note Off the panel sends
// comes out of the DevKit's native USB jack (labelled USB) on the same channel,
// as a class-compliant USB-MIDI device named "Patternflow Exhibit".
//
// One direction only. Nothing read from USB goes back to the panel, and MIDI
// Thru is off so nothing is echoed back over Wi-Fi. The USB messages that mean
// something are the resync request (Control Change 119 on channel 16,
// value > 0, bytes BF 77 7F), which makes the bridge send the last Program
// Change again, with its mirror and the knob values (below), and the PF
// monitor's SysEx: its status request and its four commands (below).
//
// Threading: one FreeRTOS task ("fl-midi", pinned) owns the RTP session, the
// MIDI parser and every TinyUSB MIDI read/write. FlowLocal's loop() is not in
// the path. The web console only reads and sets the atomics below. The task
// starts with USB, before the hotspot's boot silence (fl_wifi.h), so the PF
// monitor gets its replies from the first second; it touches the RTP session
// only once begin() has opened it after the AP is up (rtpReady).
//
// Program Change re-send. The last Program Change the panel sent goes out
// again, at most once per event, when
//   - it could not be written (USB FIFO full or no host),
//   - USB (re)mounts or the laptop wakes,
//   - the laptop asks (the resync request above: the Live device sends it
//     when it loads and from its button), or the operator presses the button
//     on the status page.
// Nothing else re-sends it: the FIFO keeps order, so if the newest Program
// Change was accepted, it is also the newest one the laptop will read.
//
// Program mirror. Right after every Program Change this bridge forwards or
// re-sends, it also sends Control Change FL_EXHIBIT_PROGRAM_CC (118) on
// channel FL_EXHIBIT_PROGRAM_CC_CHANNEL (16) with the program number as the
// value (bytes BF 76 <n>). It is the fallback for a Live that does not hand
// Program Change to a Max for Live [midiin]: the device's Alt CC setting reads
// that CC as a program. The mirror is written, retried and counted like the
// Program Change it follows. It never goes out ahead of its Program Change:
// when the Program Change could not be written, the mirror is owed with it;
// when only the mirror could not be written, the mirror alone is owed.
// FL_EXHIBIT_PROGRAM_CC 0 turns the mirror off.
//
// Knob re-send. The bridge keeps the last value of each Control Change
// FL_EXHIBIT_KNOB_CC_FIRST..LAST (24..26, the panel's K1-K3) the panel sent on
// channel FL_EXHIBIT_KNOB_CC_CHANNEL (1), whether or not it reached the FIFO:
// it is the panel's current position either way. Every Program Change re-send
// listed above is followed, after its mirror, by those values, in knob order,
// so a laptop that (re)connects or asks gets the knob positions with the
// pattern. A knob the panel has not sent since FlowLocal started is skipped;
// with no Program Change seen, nothing is re-sent at all. The values are
// written and counted like the re-sent mirror, and never go out ahead of the
// Program Change or mirror they follow. A fresh Program Change from the panel
// stands in for an owed one, but the knob values owed with it still follow
// it; they are read when written, so they are the newest the panel sent. A
// value the panel sent that could not be written is not re-sent by itself;
// only the events above re-send it. The values are replayed as they came, so
// they must be absolute: the exhibit panel sends K1-K3 as absolute positions.
// FL_EXHIBIT_KNOB_RESEND 0 turns this off.
//
// Panel address. The IP of the newest session with the handshake done is
// kept for the status page (links to the panel's own console pages). It is
// taken from the control invitation this bridge accepted for that SSRC.
//
// PF monitor (protocol v1, fl_pf_monitor.h). The laptop's monitor sends the
// status request F0 7D 50 46 01 F7 to this port every 2 s; the bridge answers
// F0 7D 50 46 02 <text> F7, "key=value;..." with the session, hotspot, knob,
// timing and counter values below. SysEx from USB is assembled packet by
// packet in the task's read loop; any other SysEx (a command with the wrong
// guard or length included) is discarded and counted with the other
// discarded packets. The resync request path is not touched. Replies and
// acks
//   - go out at the end of a pass, after the panel's messages and after any
//     owed re-send, which stays first (they wait behind it up to
//     FL_EXHIBIT_MON_HOLD_MS, then are given up); acks before a reply;
//   - are written whole, F0 to F7, by this task, the only USB writer, which
//     does nothing else meanwhile: a status byte inside one would end the
//     SysEx for the laptop. Each waits for FIFO room at most
//     FL_EXHIBIT_MON_WRITE_MS (20 ms) in all, then is given up, with anything
//     else still waiting in that pass; the laptop drops a SysEx without F7
//     when the next message's status byte arrives;
//   - are not counted in tx (that counts MIDI messages only).
// A status reply goes out at most once per FL_EXHIBIT_MON_MIN_MS (200 ms):
// requests closer than that, or while a reply waits, are counted, not
// answered.
//
// PF monitor commands, each at most once per FL_EXHIBIT_MON_CMD_MIN_MS (3 s;
// one sent sooner is refused with an ack, ok=0), each logged and acked
// "cmd=<10|11|12|13>;ok=<0|1>;msg=<why>":
//   10 reconnect panel: every RTP session FlowLocal holds is ended with a BY
//      (the library's sendEndSession(), which also drops each from the list),
//      so the panel invites again. For FL_EXHIBIT_RECONNECT_QUIET_MS after it,
//      the panel's packets still under way get no BY of their own (see
//      shouldByeStranger); acked at once, ok=0 with no session to end;
//   11 reboot panel: a short task of its own (never this one) sends
//      POST /api/wifi/reboot to the panel's HTTP server and reads the status
//      line, FL_EXHIBIT_PANEL_HTTP_MS (2 s) at most for connecting and as much
//      for the answer. The panel is the newest session's address, else the
//      one hotspot station with a DHCP lease; with none, or more than one and
//      no session, nothing is sent. Acked when that task is done, ok=1 only
//      for HTTP 200; one at a time;
//   12 reboot FlowLocal: acked, then the board restarts
//      FL_EXHIBIT_MON_RESTART_MS (300 ms) after the ack went out (or was
//      given up), 2 s after the command at the latest. It comes back with the
//      AP's boot silence, like the status page's Reboot.
//   13 Wi-Fi channel (guard 'C' 'H', one argument: 0 automatic, 1..13 that
//      channel, 127 the next of 1 / 6 / 11 after the current one): the choice
//      goes to NVS (fl_channel_store.h), is acked, and the board restarts as
//      for 12 (why=cmd13); the boot silence then starts the hotspot on it
//      (fl_wifi.h). Refused, with nothing stored, when the build fixes the
//      channel (FL_EXHIBIT_AP_CHANNEL), for "next" before the hotspot is up,
//      when NVS cannot be written, and for any other argument byte (that
//      refusal does not use up the command's 3 s).
// Acks wait in a queue of FL_EXHIBIT_MON_ACK_QUEUE; one that finds it full is
// dropped (counted).
//
// Arrival timing (fl_rtp_jitter.h): every RTP packet from a known session
// gives its timestamp (the panel's 10 kHz media clock) and when this task
// parsed it (esp_timer, same units). jt= is the RFC 3550 interarrival jitter
// and jm= how much later than the quickest packet of the last minute the
// slowest one arrived, both in ms. They cover the Wi-Fi link and this task's
// own delay (a pass of 1 ms, plus a reply being written).
//
// The hotspot facts (AP up, its channel, stations, the panel station's RSSI)
// need the Wi-Fi driver and the DHCP server, which this task does not call:
// pollHotspot() fetches them from loop() and publishes them in atomics.
//
// Full USB FIFO (16 packets). A burst while the laptop reads: the host empties
// the FIFO every USB frame (1 ms), so a failed write is retried for a few
// frames before it counts as dropped. Nobody reading (Live closed, if the OS
// does not drain the device then — not verified on hardware): once writes have
// kept failing for FL_EXHIBIT_STALL_MS, the bridge stops waiting.
//
// RTP session repairs (AppleMIDI 3.5.0 as pinned in platformio.ini). Five
// members of the session class are specialised for this one instantiation,
// below, and RtpSettings shortens one library timeout; the library files stay
// untouched. Why a BY is sent or not, throughout: every BY FlowLocal sends
// carries FlowLocal's own SSRC, the same for all its sessions, and the
// panel's library (the initiator) ends every session it holds whose SSRC that
// is (ReceivedEndSession matches the SSRC alone). A BY that reaches a device
// therefore ends whatever session that device has with FlowLocal, including
// one it has only just opened.
//   - begin(): FlowLocal's SSRC comes from the chip's MAC, so it is the same
//     after a reboot, and Arduino random() stays on the hardware RNG.
//   - ReceivedControlInvitation():
//     . a panel that rebooted invites from the same address and port with a
//       new SSRC. Its old session is dropped then and there, silently. The
//       stock code let it time out later with a BY carrying FlowLocal's SSRC,
//       which the panel matched against its NEW session and ended it. The
//       same SSRC with a new initiator token (the panel started over while
//       its old session is still listed here) is treated the same.
//     . the same SSRC and token again, from the same address: the panel
//       repeats an invitation because FlowLocal's OK did not reach it (Wi-Fi
//       lost it). While that session's data handshake has not begun (no data
//       invitation seen, so the panel cannot have had the OK), it gets the
//       same OK again. The stock code ignored it, and the panel re-sent into
//       silence until it gave the invitation up. A repeated OK cannot make a
//       second session on either side: here nothing is added (the one
//       participant stays), and the panel's library looks an OK up by its
//       token and only updates that participant (the worst it does with a
//       late extra OK is repeat its data invitation, answered as before, and
//       report the join twice, which the exhibit panel counts once, by SSRC).
//       Once the data invitation came, a repeat is a stale copy: ignored.
//     . never NO: with the list still full after that (two sessions from
//       elsewhere), the one heard from least recently makes room and the
//       invitation is accepted. The stock code answered NO, which the panel's
//       library drops without a word, and the panel waited 20 s. The one
//       evicted gets a BY only when it is on another address than the new
//       invitation (see above: a BY to the inviting device could end the
//       session this OK opens); on the same address it is dropped silently.
//   - ReceivedSynchronization() / ReceivedRtp(): a heartbeat or MIDI from a
//     session FlowLocal does not know (FlowLocal restarted, the panel did not)
//     is answered with a BY carrying FlowLocal's stable SSRC. The panel ends
//     that session and invites again at once, instead of sending into the
//     void until its heartbeat gives up after 60-70 s. MIDI now counts as
//     hearing from a session, as a heartbeat always did.
//   - manageSynchronization(), with RtpSettings::CK_MaxTimeOut: a session
//     FlowLocal has heard nothing from (no heartbeat, no MIDI, no repeated
//     invitation) for FL_EXHIBIT_SESSION_TIMEOUT_MS (25 s, the library's 61 s
//     before) is ended with a BY, counted in to=: a panel still alive but
//     unheard (its packets lost) invites again at once, and a dead one no
//     longer holds a slot for a minute. The BY cannot hit a newer session of
//     the same panel: a newer invitation with its SSRC, or from its address
//     and port, has already replaced it; and when another session here is on
//     the same address (a second app on one device), the silent one is
//     dropped without a BY (CHANGED from the stock code, which always sent
//     one).
// Each BY of FlowLocal's own (these, and "reconnect panel") starts the quiet
// time of shouldByeStranger(): the panel's packets still under way get no
// stranger BY that could reach it after its next session has begun.
//
// Self-recovery (the decisions are in fl_recovery.h, tested on the PC):
//   - Restart reason. bootBegin(), first thing in setup(), reads
//     esp_reset_reason() (rsn=) and the boot record in RTC no-init memory:
//     boots since power-on (bc=) and the note FlowLocal left just before it
//     restarted itself (why=: cmd12, web, bridge_hung, usb_wedged). The record
//     survives esp_restart(), a crash, the watchdogs and the brown-out
//     detector's restart; a power-on (the RST button too) starts it at 1.
//   - Bridge watchdog, two of them, since neither sees everything. This task
//     subscribes to the ESP task watchdog (this core's sdkconfig: panic after
//     5 s, as for AsyncTCP's task) and feeds it every pass; loop() checks the
//     task's heartbeat every 500 ms and restarts with why=bridge_hung after
//     FL_EXHIBIT_BRIDGE_STALL_MS (3 s) without a pass. A task blocked for good
//     (a lock, an lwIP call) is caught by loop() first, with that note. A
//     task spinning at priority 12 starves loop() (priority 1, same core) and
//     core 1's idle task (which the task watchdog does not watch): only the
//     watchdog's timer interrupt still runs, and its hook notes bridge_hung
//     when this task's heartbeat is what stopped (rsn=task_wdt); another
//     task's timeout (AsyncTCP's, core 0's idle task) leaves no note. loop()
//     starts after setup(), so the boot silence and the channel scan never
//     count; the task runs its passes through them as always.
//   - USB watch. The laptop's monitor keeps asking for status (so the OUT
//     endpoint works, and a program has the port open for input: the monitor
//     asks only then) and no reply gets out whole for
//     FL_EXHIBIT_USB_ASK_WINDOW_MS (8 s) while a host has the device
//     configured. That is an IN endpoint that stopped moving, or a suspend
//     TinyUSB never saw end (usbReady() false while the host sends: this task
//     reads USB whenever the MIDI interface is configured, suspended or not,
//     to see those requests). A reply written whole proves the laptop read it:
//     even the shortest is more packets than the TX FIFO and the endpoint
//     buffer hold (checked below). Then a soft reconnect: the pull-up goes off
//     for FL_EXHIBIT_USB_OFF_MS (tud_disconnect(); on the S3 it also forces
//     the pads' pull-downs) and back on (tud_connect()), and the host
//     enumerates the device again from a bus reset, which resets every
//     endpoint and TinyUSB's state. Counted (ur=) and logged. No reply out
//     within FL_EXHIBIT_USB_VERIFY_MS while requests come again, or the host
//     never takes the device back: restart (why=usb_wedged). At most one
//     reconnect per FL_EXHIBIT_USB_RECONNECT_MIN_MS. Nobody asking means no
//     action: Live closed, the monitor closed and a laptop asleep all look
//     the same from here, and all mean "leave USB alone".
//   - Silent both ways (nothing arrives, nothing gets out) cannot be told
//     apart on this side from nobody having the port open: TinyUSB shows
//     nothing of a host knocking on an endpoint that is not armed, and the
//     host polls IN only for a program that reads. A stuck bridge task is
//     caught above; an OUT endpoint that stopped taking data, or a stopped
//     controller, is not. Only the laptop can tell (its monitor gets no reply
//     at all): staff press RST or replug.
//   - Automatic restarts (bridge_hung, usb_wedged) stop after
//     FL_EXHIBIT_RECOVERY_RESTARTS_MAX (3) since power-on, counted in the boot
//     record: the loop() check and the USB watch then only log, and this task
//     does not subscribe to the task watchdog, so a fault that comes back
//     after every boot cannot restart FlowLocal (and its hotspot) forever.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include "../flowlocal_config.h"

#if !FL_EXHIBIT
#  error "fl_midi_bridge.h belongs to the exhibit build (FL_EXHIBIT=1)"
#endif
#if ARDUINO_USB_MODE
#  error "the exhibit MIDI bridge needs ARDUINO_USB_MODE=0 (native USB = TinyUSB device)"
#endif

#include <Arduino.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "USB.h"
#include "USBMIDI.h"
#include "tusb.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "fl_ap_channel.h"
#include "fl_channel_store.h"
#include "fl_pf_monitor.h"
#include "fl_recovery.h"
#include "fl_rtp_jitter.h"

#if !CONFIG_TINYUSB_MIDI_ENABLED
#  error "this Arduino core was built without TinyUSB MIDI"
#endif
// USB watch (header comment): a status reply written whole proves the laptop
// read the IN endpoint only while even the shortest reply is more USB-MIDI
// packets than the device holds on its side: TinyUSB's MIDI TX FIFO, then the
// endpoint's buffer (a transfer completes when the host has taken it).
static_assert(FlPfMonitor::packetCount(FlPfMonitor::MESSAGE_HEAD + FlPfMonitor::STATUS_TEXT_MIN + 1) >
                  (CFG_TUD_MIDI_TX_BUFSIZE + CFG_TUD_MIDI_TX_EPSIZE) / 4,
              "the shortest status reply must be more packets than TinyUSB's MIDI TX FIFO and endpoint buffer hold");

// Listener only: APPLEMIDI_INITIATOR stays undefined.
#define USE_EXT_CALLBACKS
#include <AppleMIDI.h>

// The specialisations below copy library internals. A different library
// version must be re-read before it is built, not after it misbehaves.
#if APPLEMIDI_LIBRARY_VERSION != 0x030500
#  error "fl_midi_bridge.h specialises AppleMIDI 3.5.0 session members; re-check them against the new version"
#endif
#if defined(ONE_PARTICIPANT) || defined(APPLEMIDI_INITIATOR) || defined(USE_DIRECTORY) || \
    defined(APPLEMIDI_NO_RECEIVER_FEEDBACK) || defined(NO_SESSION_NAME)
#  error "fl_midi_bridge.h's session specialisations assume the default AppleMIDI size gates"
#endif

#ifndef FL_EXHIBIT_USB_PRODUCT
#  define FL_EXHIBIT_USB_PRODUCT "Patternflow Exhibit"   // the port name Windows / Live show
#endif
#ifndef FL_EXHIBIT_USB_MANUFACTURER
#  define FL_EXHIBIT_USB_MANUFACTURER "Patternflow"
#endif
#ifndef FL_EXHIBIT_RTP_SESSION
#  define FL_EXHIBIT_RTP_SESSION "FlowLocal"
#endif
#ifndef FL_EXHIBIT_RTP_PORT
#  define FL_EXHIBIT_RTP_PORT 5004                        // control; data is +1 (5005)
#endif
#ifndef FL_EXHIBIT_BRIDGE_CORE
#  define FL_EXHIBIT_BRIDGE_CORE 1                        // Wi-Fi / lwIP live on core 0
#endif
#ifndef FL_EXHIBIT_BRIDGE_PRIORITY
#  define FL_EXHIBIT_BRIDGE_PRIORITY 12                   // above AsyncTCP (10), below lwIP / Wi-Fi / usbd
#endif
#ifndef FL_EXHIBIT_BRIDGE_STACK
#  define FL_EXHIBIT_BRIDGE_STACK 8192                    // bytes; high-water mark is in /api/status
#endif
#ifndef FL_EXHIBIT_RX_BUDGET
#  define FL_EXHIBIT_RX_BUDGET 32                         // RTP messages per 1 ms slice
#endif
#ifndef FL_EXHIBIT_WRITE_RETRY_MS
#  define FL_EXHIBIT_WRITE_RETRY_MS 4                     // frames to wait on a full FIFO
#endif
#ifndef FL_EXHIBIT_RESEND_RETRY_MS
#  define FL_EXHIBIT_RESEND_RETRY_MS 250                  // owed Program Change: one try per this
#endif
#ifndef FL_EXHIBIT_STALL_MS
#  define FL_EXHIBIT_STALL_MS 500                         // writes failing this long = nobody reads
#endif
#ifndef FL_EXHIBIT_RESYNC_CHANNEL
#  define FL_EXHIBIT_RESYNC_CHANNEL 16                    // laptop → FlowLocal resync request:
#endif
#ifndef FL_EXHIBIT_RESYNC_CC
#  define FL_EXHIBIT_RESYNC_CC 119                        //   CC 119, channel 16, value > 0
#endif
#ifndef FL_EXHIBIT_RESYNC_MIN_MS
#  define FL_EXHIBIT_RESYNC_MIN_MS 250                    // requests closer than this count once
#endif
#ifndef FL_EXHIBIT_PROGRAM_CC
#  define FL_EXHIBIT_PROGRAM_CC 118                       // FlowLocal → laptop program mirror:
#endif                                                    //   CC 118, value = program; 0 = off
#ifndef FL_EXHIBIT_PROGRAM_CC_CHANNEL
#  define FL_EXHIBIT_PROGRAM_CC_CHANNEL 16                //   on channel 16
#endif
#if FL_EXHIBIT_PROGRAM_CC < 0 || FL_EXHIBIT_PROGRAM_CC > 119
#  error "FL_EXHIBIT_PROGRAM_CC must be 0 (off) or a control number 1..119 (120-127 are channel mode messages)"
#endif
#if FL_EXHIBIT_PROGRAM_CC_CHANNEL < 1 || FL_EXHIBIT_PROGRAM_CC_CHANNEL > 16
#  error "FL_EXHIBIT_PROGRAM_CC_CHANNEL must be 1..16"
#endif
// Refused on every channel, not only the resync request's: Live can rewrite
// the channel of whatever a track sends back out to this port, so a mirror
// echoed back on another channel could still arrive as a resync request.
#if FL_EXHIBIT_PROGRAM_CC != 0 && FL_EXHIBIT_PROGRAM_CC == FL_EXHIBIT_RESYNC_CC
#  error "the program mirror must not use the resync request's control number (on any channel): a port echoing it back would ask for another re-send"
#endif
#ifndef FL_EXHIBIT_KNOB_RESEND
#  define FL_EXHIBIT_KNOB_RESEND 1                        // knob values after each re-sent program; 0 = off
#endif
#ifndef FL_EXHIBIT_KNOB_CC_FIRST
#  define FL_EXHIBIT_KNOB_CC_FIRST 24                     // K1..K3 = CC 24..26
#endif
#ifndef FL_EXHIBIT_KNOB_CC_LAST
#  define FL_EXHIBIT_KNOB_CC_LAST 26
#endif
#ifndef FL_EXHIBIT_KNOB_CC_CHANNEL
#  define FL_EXHIBIT_KNOB_CC_CHANNEL 1                    //   on channel 1, as the panel sends them
#endif
#if FL_EXHIBIT_KNOB_RESEND
#  if FL_EXHIBIT_KNOB_CC_FIRST < 0 || FL_EXHIBIT_KNOB_CC_LAST > 119 || \
      FL_EXHIBIT_KNOB_CC_LAST < FL_EXHIBIT_KNOB_CC_FIRST || \
      FL_EXHIBIT_KNOB_CC_LAST - FL_EXHIBIT_KNOB_CC_FIRST >= 8
#    error "FL_EXHIBIT_KNOB_CC_FIRST..LAST must be 1 to 8 control numbers within 0..119 (120-127 are channel mode messages)"
#  endif
#  if FL_EXHIBIT_KNOB_CC_CHANNEL < 1 || FL_EXHIBIT_KNOB_CC_CHANNEL > 16
#    error "FL_EXHIBIT_KNOB_CC_CHANNEL must be 1..16"
#  endif
// The re-sent knob values reach the laptop like the panel's own. On any
// channel (as for the mirror above): the resync request's number would ask for
// another re-send when echoed back, and the mirror's would read as a program.
#  if FL_EXHIBIT_KNOB_CC_FIRST <= FL_EXHIBIT_RESYNC_CC && FL_EXHIBIT_RESYNC_CC <= FL_EXHIBIT_KNOB_CC_LAST
#    error "the knob re-send range must not contain the resync request's control number"
#  endif
#  if FL_EXHIBIT_PROGRAM_CC != 0 && \
      FL_EXHIBIT_KNOB_CC_FIRST <= FL_EXHIBIT_PROGRAM_CC && FL_EXHIBIT_PROGRAM_CC <= FL_EXHIBIT_KNOB_CC_LAST
#    error "the knob re-send range must not contain the program mirror's control number"
#  endif
// Channel 16 is FlowLocal's own (resync request, program mirror): nothing the
// panel sends there is replayed.
#  if FL_EXHIBIT_KNOB_CC_CHANNEL == FL_EXHIBIT_RESYNC_CHANNEL || \
      (FL_EXHIBIT_PROGRAM_CC != 0 && FL_EXHIBIT_KNOB_CC_CHANNEL == FL_EXHIBIT_PROGRAM_CC_CHANNEL)
#    error "the knob re-send channel must not be the resync request's or the program mirror's channel"
#  endif
#endif
#ifndef FL_EXHIBIT_STRANGER_BYE_MS
#  define FL_EXHIBIT_STRANGER_BYE_MS 1000                 // one BY per unknown session per second
#endif
#ifndef FL_EXHIBIT_SESSION_TIMEOUT_MS
#  define FL_EXHIBIT_SESSION_TIMEOUT_MS 25000             // a session unheard this long is ended with a BY
#endif
// The panel's library sends a heartbeat (CK0) every 10 s once its session is
// up, and again 10 s after one that got no answer, so one lost heartbeat
// leaves 20 s between two that arrive: the timeout must be longer than that.
// At most the library's own 61 s.
#if FL_EXHIBIT_SESSION_TIMEOUT_MS < 21000 || FL_EXHIBIT_SESSION_TIMEOUT_MS > 61000
#  error "FL_EXHIBIT_SESSION_TIMEOUT_MS must be 21000..61000: longer than two of the panel's 10 s heartbeats, at most the library's own 61 s"
#endif
#ifndef FL_EXHIBIT_MON_FW_TAG
#  define FL_EXHIBIT_MON_FW_TAG "flx-mon3"            // fw= in the PF monitor's status reply
#endif
#ifndef FL_EXHIBIT_MON_MIN_MS
#  define FL_EXHIBIT_MON_MIN_MS 200                       // at most one status reply per this
#endif
#ifndef FL_EXHIBIT_MON_WRITE_MS
#  define FL_EXHIBIT_MON_WRITE_MS 20                      // a reply the FIFO has not taken by then is given up
#endif
#ifndef FL_EXHIBIT_MON_HOLD_MS
#  define FL_EXHIBIT_MON_HOLD_MS 1000                     // longest a reply or ack waits behind an owed re-send
#endif
#ifndef FL_EXHIBIT_MON_CMD_MIN_MS
#  define FL_EXHIBIT_MON_CMD_MIN_MS 3000                  // each PF monitor command at most once per this
#endif
#ifndef FL_EXHIBIT_MON_ACK_QUEUE
#  define FL_EXHIBIT_MON_ACK_QUEUE 4                      // acks waiting to be written
#endif
#ifndef FL_EXHIBIT_MON_RESTART_MS
#  define FL_EXHIBIT_MON_RESTART_MS 300                   // "reboot FlowLocal": restart this long after its ack
#endif
#ifndef FL_EXHIBIT_RECONNECT_QUIET_MS
#  define FL_EXHIBIT_RECONNECT_QUIET_MS 1000              // no stranger BY this long after "reconnect panel"
#endif
#ifndef FL_EXHIBIT_PANEL_HTTP_PORT
#  define FL_EXHIBIT_PANEL_HTTP_PORT 80                   // "reboot panel": the panel's web server
#endif
#ifndef FL_EXHIBIT_PANEL_HTTP_MS
#  define FL_EXHIBIT_PANEL_HTTP_MS 2000                   //   connect timeout, and again for the answer
#endif
#ifndef FL_EXHIBIT_PANEL_HTTP_STACK
#  define FL_EXHIBIT_PANEL_HTTP_STACK 6144                //   its task's stack, bytes
#endif
#ifndef FL_EXHIBIT_RECOVERY_RESTARTS_MAX
#  define FL_EXHIBIT_RECOVERY_RESTARTS_MAX 3              // automatic restarts per power-on, then only logs
#endif
#ifndef FL_EXHIBIT_BRIDGE_STALL_MS
#  define FL_EXHIBIT_BRIDGE_STALL_MS 3000                 // loop(): no bridge pass this long = restart
#endif
#ifndef FL_EXHIBIT_USB_ASK_WINDOW_MS
#  define FL_EXHIBIT_USB_ASK_WINDOW_MS 8000               // USB watch: status requests unanswered this long ...
#endif
#ifndef FL_EXHIBIT_USB_ASK_MIN
#  define FL_EXHIBIT_USB_ASK_MIN 3                        //   ... at least this many ...
#endif
#ifndef FL_EXHIBIT_USB_ASK_GAP_MS
#  define FL_EXHIBIT_USB_ASK_GAP_MS 5000                  //   ... none further apart (the monitor asks every 2 s)
#endif
#ifndef FL_EXHIBIT_USB_OFF_MS
#  define FL_EXHIBIT_USB_OFF_MS 500                       // soft reconnect: off the bus this long
#endif
#ifndef FL_EXHIBIT_USB_VERIFY_MS
#  define FL_EXHIBIT_USB_VERIFY_MS 10000                  // then a reply must get out within this, or restart
#endif
#ifndef FL_EXHIBIT_USB_SETTLE_MS
#  define FL_EXHIBIT_USB_SETTLE_MS 3000                   //   (a request counts once unanswered this long)
#endif
#ifndef FL_EXHIBIT_USB_RECONNECT_MIN_MS
#  define FL_EXHIBIT_USB_RECONNECT_MIN_MS 60000           // soft reconnects at most once per this
#endif
static_assert(FlPfMonitor::validTag(FL_EXHIBIT_MON_FW_TAG),
              "FL_EXHIBIT_MON_FW_TAG must be 1-32 printable characters without spaces, ';' or '='");
static_assert(FL_EXHIBIT_MON_ACK_QUEUE >= 1 && FL_EXHIBIT_MON_ACK_QUEUE <= 16,
              "FL_EXHIBIT_MON_ACK_QUEUE must be 1..16");
static_assert(FL_EXHIBIT_RECOVERY_RESTARTS_MAX >= 0 && FL_EXHIBIT_RECOVERY_RESTARTS_MAX <= 20,
              "FL_EXHIBIT_RECOVERY_RESTARTS_MAX must be 0..20 (0: never restart automatically)");
static_assert(FL_EXHIBIT_USB_ASK_MIN >= 2 && FL_EXHIBIT_USB_ASK_GAP_MS >= 3000 &&
              FL_EXHIBIT_USB_ASK_WINDOW_MS >= FL_EXHIBIT_USB_ASK_GAP_MS,
              "USB watch: at least 2 requests, a gap of 3 s or more (the monitor asks every 2 s), a window no shorter");
static_assert(FL_EXHIBIT_USB_SETTLE_MS > FL_EXHIBIT_MON_HOLD_MS + FL_EXHIBIT_MON_WRITE_MS &&
              FL_EXHIBIT_USB_VERIFY_MS > FL_EXHIBIT_USB_SETTLE_MS &&
              FL_EXHIBIT_USB_RECONNECT_MIN_MS >= FL_EXHIBIT_USB_OFF_MS + FL_EXHIBIT_USB_VERIFY_MS,
              "USB watch: a request must be able to wait out the re-send ahead of its reply before it counts, "
              "within the verify time, and a reconnect must be over before the next may start");
// The task watchdog's hook tells this task's timeout by its heartbeat being
// FL_EXHIBIT_BRIDGE_STALL_MS old; the watchdog fires later than that.
#if defined(CONFIG_ESP_TASK_WDT_TIMEOUT_S)
static_assert(FL_EXHIBIT_BRIDGE_STALL_MS >= 1000 && FL_EXHIBIT_BRIDGE_STALL_MS < CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
              "FL_EXHIBIT_BRIDGE_STALL_MS must be at least 1 s and shorter than the task watchdog's timeout");
#endif
// rsn= names esp_reset_reason_t by number (fl_recovery.h).
static_assert((int)ESP_RST_UNKNOWN == FlRecovery::RST_UNKNOWN && (int)ESP_RST_POWERON == FlRecovery::RST_POWERON &&
              (int)ESP_RST_EXT == FlRecovery::RST_EXT && (int)ESP_RST_SW == FlRecovery::RST_SW &&
              (int)ESP_RST_PANIC == FlRecovery::RST_PANIC && (int)ESP_RST_INT_WDT == FlRecovery::RST_INT_WDT &&
              (int)ESP_RST_TASK_WDT == FlRecovery::RST_TASK_WDT && (int)ESP_RST_WDT == FlRecovery::RST_WDT &&
              (int)ESP_RST_DEEPSLEEP == FlRecovery::RST_DEEPSLEEP && (int)ESP_RST_BROWNOUT == FlRecovery::RST_BROWNOUT &&
              (int)ESP_RST_SDIO == FlRecovery::RST_SDIO && (int)ESP_RST_USB == FlRecovery::RST_USB &&
              (int)ESP_RST_JTAG == FlRecovery::RST_JTAG && (int)ESP_RST_EFUSE == FlRecovery::RST_EFUSE &&
              (int)ESP_RST_PWR_GLITCH == FlRecovery::RST_PWR_GLITCH && (int)ESP_RST_CPU_LOCKUP == FlRecovery::RST_CPU_LOCKUP,
              "esp_reset_reason_t changed: update FlRecovery::ResetReason and resetReasonName()");

namespace FlMidiBridge {

using APPLEMIDI_NAMESPACE::ssrc_t;

// One panel is expected. The second slot is for a panel that rejoined from a
// different address while its old session times out; a rejoin from the same
// address replaces the old session at once, and an invitation that finds both
// slots taken ends the one heard from least recently (see
// ReceivedControlInvitation). A session not heard from for
// FL_EXHIBIT_SESSION_TIMEOUT_MS is ended (manageSynchronization).
struct RtpSettings : public APPLEMIDI_NAMESPACE::DefaultSettings {
    static const uint8_t MaxNumberOfParticipants = 2;
    static const unsigned long CK_MaxTimeOut = FL_EXHIBIT_SESSION_TIMEOUT_MS;
};

using Session   = APPLEMIDI_NAMESPACE::AppleMIDISession<WiFiUDP, RtpSettings>;
using Interface = MIDI_NAMESPACE::MidiInterface<Session, APPLEMIDI_NAMESPACE::AppleMIDISettings>;

// ── Counters (written by the bridge task, read by the web console) ─────────
inline std::atomic<bool>     usbStarted{false};
inline std::atomic<bool>     usbMounted{false};
inline std::atomic<bool>     taskRunning{false};
inline std::atomic<int32_t>  peers{0};            // RTP sessions with the data handshake done
inline std::atomic<uint32_t> sessions{0};         // sessions opened since boot
inline std::atomic<uint32_t> rxCount{0};          // MIDI messages parsed from RTP
inline std::atomic<uint32_t> ignoredCount{0};     // parsed but not a CC / PC / note
inline std::atomic<uint32_t> txCount{0};          // packets accepted by the USB FIFO
inline std::atomic<uint32_t> droppedCount{0};     // forwards that did not reach the FIFO
inline std::atomic<uint32_t> resendCount{0};      // last-program re-sends
inline std::atomic<uint32_t> mirrorCount{0};      // program mirrors (CC 118) accepted by the USB FIFO
inline std::atomic<uint32_t> knobResendCount{0};  // knob values (CC 24-26) re-sent and accepted by the USB FIFO
inline std::atomic<uint32_t> resyncRequests{0};   // resync requests the laptop sent
inline std::atomic<uint32_t> webResends{0};       // re-sends asked for on the status page
inline std::atomic<uint32_t> usbInDiscarded{0};   // other packets the laptop sent us, thrown away
inline std::atomic<int32_t>  lastProgram{-1};     // 0..127, -1 = none seen yet
inline std::atomic<uint8_t>  lastProgramChannel{1};
// The panel's knob values for the re-send (header comment): knobValue[i] is
// the last value of CC FL_EXHIBIT_KNOB_CC_FIRST + i, valid when bit i of
// knobSeen is set. Written by the bridge task only.
constexpr uint8_t KNOB_COUNT =
    FL_EXHIBIT_KNOB_RESEND ? (uint8_t)(FL_EXHIBIT_KNOB_CC_LAST - FL_EXHIBIT_KNOB_CC_FIRST + 1) : (uint8_t)1;
constexpr uint8_t KNOB_ALL = (uint8_t)((1u << KNOB_COUNT) - 1u);
inline std::atomic<uint8_t>  knobValue[KNOB_COUNT];
inline std::atomic<uint8_t>  knobSeen{0};
inline std::atomic<uint32_t> lastRxMs{0};         // last MIDI message from a session
inline std::atomic<uint32_t> lastHeardMs{0};      // last MIDI or heartbeat from a session
inline std::atomic<uint32_t> rtpGaps{0};          // packets the panel sent that never arrived
inline std::atomic<uint32_t> rtpErrors{0};        // parse errors / buffer overflows
inline std::atomic<uint32_t> rtpRejected{0};      // invitations refused (all slots busy); none since flx-mon3
inline std::atomic<uint32_t> rtpTimeouts{0};      // sessions dropped for silence (FL_EXHIBIT_SESSION_TIMEOUT_MS)
inline std::atomic<uint32_t> rtpReplaced{0};      // old sessions dropped because their panel rejoined
inline std::atomic<uint32_t> rtpEvicted{0};       // sessions ended to make room for an invitation (never a NO)
inline std::atomic<uint32_t> rtpRetryOks{0};      // repeated invitations answered with the same OK again
inline std::atomic<uint32_t> rtpUnknown{0};       // heartbeats / MIDI from a session we do not know
inline std::atomic<uint32_t> lastUnknownMs{0};
inline std::atomic<uint32_t> rtpByeSent{0};       // BYs sent to such a session
inline std::atomic<uint32_t> stackFree{0};
inline std::atomic<uint32_t> panelIp{0};          // newest connected session's IPv4 (IPAddress raw), 0 = none
inline std::atomic<bool>     webResendRequested{false};
// PF monitor (header comment).
inline std::atomic<bool>     rtpReady{false};      // begin() has opened the RTP session; set once
inline std::atomic<uint32_t> statusRequests{0};    // status requests the laptop sent
inline std::atomic<uint32_t> statusReplies{0};     // replies written whole (sr=)
inline std::atomic<uint32_t> statusUnanswered{0};  // requests within FL_EXHIBIT_MON_MIN_MS, or while a reply waited
inline std::atomic<uint32_t> statusAbandoned{0};   // replies given up (no room in time, no host, owed re-send too long)
inline std::atomic<uint32_t> commandsReceived{0};  // commands with a valid guard
inline std::atomic<uint32_t> commandsRefused{0};   // of those, refused (too soon, busy, nothing to do)
inline std::atomic<uint32_t> acksSent{0};          // acks written whole
inline std::atomic<uint32_t> acksLost{0};          // acks given up, or dropped on a full queue
// Hotspot facts for the status reply, written by pollHotspot() (loop()). The
// AP is down until FlWifi::begin() starts it, so they start at "down, empty".
inline std::atomic<int8_t>   apRunning{0};        // soft AP up 0 / 1
inline std::atomic<int16_t>  apStationCount{0};   // stations on the hotspot, -1 = unknown
inline std::atomic<int16_t>  apChannelNow{-1};    // AP channel, -1 = AP not up
// This boot's channel plan for chm= and cs=, from setup(): how the channel is
// chosen (FlWifi::planChannel(), before the boot silence), then the scan's
// scores (after it). Until then, what the build alone says.
inline std::atomic<uint8_t>  channelMode{(uint8_t)(FL_EXHIBIT_AP_CHANNEL != 0 ? FlApChannel::Mode::Fixed
                                                                              : FlApChannel::Mode::Auto)};
inline std::atomic<bool>     channelScored{false};   // channelScore is this boot's scan (release / acquire)
inline uint32_t              channelScore[FlApChannel::CANDIDATE_COUNT] = {};   // 1, 6, 11; written once
static_assert(FlApChannel::CANDIDATE_COUNT == 3 && FlApChannel::CANDIDATES[0] == 1 &&
                  FlApChannel::CANDIDATES[1] == 6 && FlApChannel::CANDIDATES[2] == 11,
              "cs= in the PF monitor's reply names the channels 1, 6 and 11 (fl_pf_monitor.h)");
static_assert((int)FlApChannel::Mode::Fixed == FlPfMonitor::CHM_FIXED &&
                  (int)FlApChannel::Mode::Auto == FlPfMonitor::CHM_AUTO &&
                  (int)FlApChannel::Mode::Manual == FlPfMonitor::CHM_MANUAL,
              "chm= numbers are FlApChannel::Mode's");
// The panel station's RSSI in one word, so a reply never pairs one station's
// RSSI with another's address: its IPv4 (IPAddress raw) in the high half, the
// RSSI (dBm, int32) in the low half; 0 = not known.
inline std::atomic<uint64_t> panelRssiEntry{0};
// Arrival timing of the panel's RTP packets (header comment), ms, -1 unknown.
inline std::atomic<int32_t>  rtpJitterMs{-1};
inline std::atomic<int32_t>  rtpSpreadMs{-1};
// "reboot panel" (header comment): its task's result for the bridge task.
inline std::atomic<bool>     panelRebootBusy{false};   // a request is running or its ack is not queued yet
inline std::atomic<bool>     panelRebootDone{false};   // the result below is ready (release / acquire)
inline bool                  panelRebootOk = false;
inline char                  panelRebootMsg[FlPfMonitor::ACK_MSG_MAX + 1] = "";
inline std::atomic<uint32_t> panelReboots{0};          // requests the panel answered with 200

inline TaskHandle_t taskHandle = nullptr;

// FlowLocal's own RTP SSRC: fixed per board, so a panel that kept its session
// through a FlowLocal reboot accepts the BY that ends it. Even and below
// INT32_MAX, like the library's random ones.
inline uint32_t localSsrc() {
    uint64_t x = ESP.getEfuseMac() ^ 0x9E3779B97F4A7C15ull;   // splitmix64 finaliser
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    const uint32_t s = (uint32_t)x & 0x7FFFFFFEu;
    return s ? s : 2u;
}

// ── Session bookkeeping for the specialisations (bridge task only) ─────────
// The initiator token each listed SSRC was accepted with. The library does
// not keep it for a listener; it tells a retry (same token) from a panel that
// started over (new token). The address the invitation came from is kept
// next to it: the session callbacks get only the SSRC, and the library's
// participant list is private.
struct AcceptedToken {
    ssrc_t   ssrc  = 0;
    uint32_t token = 0;
    uint32_t ip    = 0;   // IPAddress raw IPv4
};
inline AcceptedToken acceptedTokens[RtpSettings::MaxNumberOfParticipants];

// The token on record for a listed SSRC, or nullptr.
inline const AcceptedToken* tokenFor(ssrc_t s) {
    for (const AcceptedToken& t : acceptedTokens)
        if (t.ssrc == s && s != 0) return &t;
    return nullptr;
}
// A session left the list (the disconnect callback): its record goes too, so
// a record only ever describes a listed session.
inline void forgetToken(ssrc_t s) {
    for (AcceptedToken& t : acceptedTokens)
        if (t.ssrc == s) t = AcceptedToken{};
}

inline void noteHeard() { lastHeardMs.store(millis() | 1, std::memory_order_relaxed); }

// A heartbeat or MIDI from an SSRC with no session here. True when a BY should
// go out now (rate-limited per SSRC). None for a while after FlowLocal has
// ended a session with a BY of its own ("reconnect panel", a session ended
// for silence, one evicted for an invitation: byeQuiet): the panel's packets
// sent before it read that BY are still arriving, and a BY answering one of
// them could reach the panel after it has started its next session with
// FlowLocal, whose SSRC the BY carries. The quiet time is measured from that
// BY, unsigned, like the rate limit: a deadline under a signed test would
// turn quiet again 24.8 days on if no stranger had come to clear it.
inline ssrc_t   lastStranger = 0;
inline uint32_t lastStrangerByeMs = 0;
inline bool     byeQuiet = false;       // FlowLocal sent a BY of its own ...
inline uint32_t byeQuietSinceMs = 0;    // ... at this millis()
inline void quietAfterOwnBye(uint32_t now) {
    byeQuiet = true;
    byeQuietSinceMs = now;
}
inline bool shouldByeStranger(ssrc_t stranger) {
    const uint32_t now = millis();
    rtpUnknown.fetch_add(1, std::memory_order_relaxed);
    lastUnknownMs.store(now | 1, std::memory_order_relaxed);
    if (byeQuiet) {
        if (now - byeQuietSinceMs < FL_EXHIBIT_RECONNECT_QUIET_MS) return false;
        byeQuiet = false;
    }
    if (stranger == lastStranger && lastStrangerByeMs != 0 &&
        now - lastStrangerByeMs < FL_EXHIBIT_STRANGER_BYE_MS)
        return false;
    lastStranger = stranger;
    lastStrangerByeMs = now | 1;
    rtpByeSent.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// An RTP packet from a session here (header comment: arrival timing). The
// local clock is the 64-bit esp_timer in the RTP clock's units, so it wraps
// with the 32-bit timestamp, never on its own.
inline FlRtpJitter::Estimator rtpTiming;   // bridge task only
inline void noteRtpTiming(ssrc_t sender, uint32_t rtpTimestamp) {
    const uint32_t arrival = (uint32_t)(esp_timer_get_time() / (1000000 / FlRtpJitter::CLOCK_HZ));
    const uint32_t now = millis();
    rtpTiming.sample(sender, rtpTimestamp, arrival, now);
    rtpJitterMs.store(rtpTiming.jitterMs(), std::memory_order_relaxed);
    rtpSpreadMs.store(rtpTiming.spreadMs(now), std::memory_order_relaxed);
}

}  // namespace FlMidiBridge

// ═══════════════════════════════════════════════════════════
// Session member specialisations (see the header comment). Everything not
// marked CHANGED is the AppleMIDI 3.5.0 code for this configuration
// (listener, USE_EXT_CALLBACKS, KEEP_SESSION_NAME, receiver feedback on).
// ═══════════════════════════════════════════════════════════
BEGIN_APPLEMIDI_NAMESPACE

template <>
inline void AppleMIDISession<WiFiUDP, FlMidiBridge::RtpSettings, DefaultPlatform>::begin()
{
    _appleMIDIParser.session = this;
    _rtpMIDIParser.session = this;

    // CHANGED: a fixed SSRC instead of random(), and no randomSeed(millis()),
    // which would have switched Arduino random() to a software PRNG.
    this->ssrc = FlMidiBridge::localSsrc();

    controlPort.begin(port);
    dataPort.begin(port + 1);

    rtpMidiClock.Init(rtpMidiClock.Now(), MIDI_SAMPLING_RATE_DEFAULT);
}

template <>
inline void AppleMIDISession<WiFiUDP, FlMidiBridge::RtpSettings, DefaultPlatform>::ReceivedControlInvitation(AppleMIDI_Invitation_t &invitation)
{
    const IPAddress remoteIP   = controlPort.remoteIP();
    const uint16_t  remotePort = controlPort.remotePort();

    // CHANGED: an invitation for a session already listed (header comment).
    //  - same SSRC, same token, same address: the panel repeats an invitation
    //    whose OK it never got. The same OK again while no data invitation has
    //    come for it (so the panel cannot have had the first); afterwards a
    //    stale copy, ignored. Either way nothing is added to the list.
    //  - same SSRC, new token (or the same token from another address): the
    //    panel started over;
    //  - another SSRC from the same address and port: the panel rebooted.
    // A replaced session is dropped without a BY: the BY would carry our SSRC,
    // and the panel would end its new session.
    for (size_t i = 0; i < participants.size();)
    {
        auto &p = participants[i];
        bool replaced = false;
        if (p.ssrc == invitation.ssrc)
        {
            const FlMidiBridge::AcceptedToken *t = FlMidiBridge::tokenFor(p.ssrc);
            // No token on record: cannot tell a retry from a restart. Ignored,
            // as in the stock library (every accepted session has one).
            if (nullptr == t)
                return;
            if (t->token == invitation.initiatorToken && p.remoteIP == remoteIP && p.remotePort == remotePort)
            {
                if (p.remoteDataPort != 0)
                    return;   // its data handshake has begun: a stale copy
                p.lastSyncExchangeTime = now;   // heard from
                // The OK exactly as the first one: our name, the panel's token
                // (writeInvitation puts our SSRC in).
                strncpy(invitation.sessionName, localName, Settings::MaxSessionNameLen);
                invitation.sessionName[Settings::MaxSessionNameLen] = '\0';
                writeInvitation(controlPort, remoteIP, remotePort, invitation, amInvitationAccepted);
                FlMidiBridge::rtpRetryOks.fetch_add(1, std::memory_order_relaxed);
                Serial.printf("[midi] rtp invitation repeated by %s: the same OK again\n",
                              remoteIP.toString().c_str());
                return;
            }
            replaced = true;
        }
        else if (p.remoteIP == remoteIP && p.remotePort == remotePort)
        {
            replaced = true;
        }

        if (!replaced)
        {
            i++;
            continue;
        }
        const ssrc_t stale = p.ssrc;
        participants.erase(i);
        FlMidiBridge::rtpReplaced.fetch_add(1, std::memory_order_relaxed);
        if (nullptr != _disconnectedCallback)
            _disconnectedCallback(stale);
    }

    // CHANGED: never NO (the panel's library drops a NO without a word and
    // its app waits for its next try). The session heard from least recently
    // makes room. It gets a BY only on another address: a BY carries our
    // SSRC, which the device the new invitation comes from would match
    // against the session this OK starts.
    if (participants.full())
    {
        size_t oldest = 0;
        for (size_t i = 1; i < participants.size(); i++)
            if (participants[i].lastSyncExchangeTime < participants[oldest].lastSyncExchangeTime)
                oldest = i;
        Participant<Settings> *victim = &participants[oldest];
        const bool bye = victim->remoteIP != remoteIP;
        Serial.printf("[midi] rtp session list full: ended %s's session (heard %lu ms ago)%s for a new invitation\n",
                      victim->remoteIP.toString().c_str(), (unsigned long)(now - victim->lastSyncExchangeTime),
                      bye ? " with a BY" : ", no BY (same address as the invitation)");
        if (bye)
        {
            FlMidiBridge::quietAfterOwnBye(millis());
            sendEndSession(victim);   // BY, then the disconnect callback
        }
        else if (nullptr != _disconnectedCallback)
        {
            _disconnectedCallback(victim->ssrc);
        }
        participants.erase(oldest);
        FlMidiBridge::rtpEvicted.fetch_add(1, std::memory_order_relaxed);
    }

    Participant<Settings> participant;
    participant.kind = Listener;
    participant.ssrc = invitation.ssrc;
    participant.sendSequenceNr = random(1, UINT16_MAX); // http://www.rfc-editor.org/rfc/rfc6295.txt , 2.1.  RTP Header
    participant.remoteIP   = remoteIP;
    participant.remotePort = remotePort;
    participant.lastSyncExchangeTime = now;
    strncpy(participant.sessionName, invitation.sessionName, Settings::MaxSessionNameLen);
    participant.sessionName[Settings::MaxSessionNameLen] = '\0';

    // CHANGED: remember the token (writeInvitation overwrites it below) and
    // the address, for the status page once the data handshake is done.
    {
        FlMidiBridge::AcceptedToken *slot = nullptr;
        for (auto &t : FlMidiBridge::acceptedTokens)
            if (t.ssrc == invitation.ssrc) slot = &t;
        for (auto &t : FlMidiBridge::acceptedTokens)
            if (slot == nullptr && (t.ssrc == 0 || getParticipantBySSRC(t.ssrc) == nullptr)) slot = &t;
        if (slot != nullptr)
        {
            slot->ssrc  = invitation.ssrc;
            slot->token = invitation.initiatorToken;
            slot->ip    = (uint32_t)remoteIP;
        }
    }

    // Re-use the invitation for acceptance. Overwrite sessionName with ours
    strncpy(invitation.sessionName, localName, Settings::MaxSessionNameLen);
    invitation.sessionName[Settings::MaxSessionNameLen] = '\0';

    participants.push_back(participant);

    writeInvitation(controlPort, participant.remoteIP, participant.remotePort, invitation, amInvitationAccepted);
}

template <>
inline void AppleMIDISession<WiFiUDP, FlMidiBridge::RtpSettings, DefaultPlatform>::ReceivedSynchronization(AppleMIDI_Synchronization_t &synchronization)
{
    auto pParticipant = getParticipantBySSRC(synchronization.ssrc);
    if (nullptr == pParticipant)
    {
        if (nullptr != _exceptionCallback)
            _exceptionCallback(ssrc, ParticipantNotFoundException, synchronization.ssrc);

        // CHANGED: tell the sender its session is gone. Heartbeats come to the
        // data port; the sender's control port is its data port - 1.
        const IPAddress from = dataPort.remoteIP();
        const uint16_t  fromPort = dataPort.remotePort();
        if ((uint32_t)from != 0 && fromPort > 1 && FlMidiBridge::shouldByeStranger(synchronization.ssrc))
        {
            AppleMIDI_EndSession_t endSession;
            endSession.initiatorToken = 0;
            endSession.ssrc = this->ssrc;
            writeEndSession(from, (uint16_t)(fromPort - 1), endSession);
        }
        return;
    }

    switch (synchronization.count)
    {
    case SYNC_CK0: /* From session APPLEMIDI_INITIATOR */
        synchronization.timestamps[SYNC_CK1] = rtpMidiClock.Now();
        synchronization.count = SYNC_CK1;
        writeSynchronization(pParticipant->remoteIP, pParticipant->remoteDataPort, synchronization);
        break;
    case SYNC_CK1: /* From session LISTENER */
        break;
    case SYNC_CK2: /* From session APPLEMIDI_INITIATOR */
        // each party can estimate the offset between the two clocks using the following formula
        pParticipant->offsetEstimate = (uint32_t)(((synchronization.timestamps[2] + synchronization.timestamps[0]) / 2) - synchronization.timestamps[1]);
        break;
    }

    // All particpants need to check in regularly,
    // failing to do so will result in a lost connection.
    pParticipant->lastSyncExchangeTime = now;
    FlMidiBridge::noteHeard();   // CHANGED: for the status page
}

template <>
inline void AppleMIDISession<WiFiUDP, FlMidiBridge::RtpSettings, DefaultPlatform>::ReceivedRtp(const Rtp_t& rtp)
{
    auto pParticipant = getParticipantBySSRC(rtp.ssrc);

    _acceptIncomingMidi = (pParticipant != nullptr);
    if (!_acceptIncomingMidi)
    {
        if (nullptr != _exceptionCallback)
            _exceptionCallback(ssrc, ParticipantNotFoundException, rtp.ssrc);

        // CHANGED: as in ReceivedSynchronization. RTP MIDI arrives on the
        // data port too.
        const IPAddress from = dataPort.remoteIP();
        const uint16_t  fromPort = dataPort.remotePort();
        if ((uint32_t)from != 0 && fromPort > 1 && FlMidiBridge::shouldByeStranger(rtp.ssrc))
        {
            AppleMIDI_EndSession_t endSession;
            endSession.initiatorToken = 0;
            endSession.ssrc = this->ssrc;
            writeEndSession(from, (uint16_t)(fromPort - 1), endSession);
        }
        return;
    }

    if (pParticipant->doReceiverFeedback == false)
        pParticipant->receiverFeedbackStartTime = now;
    pParticipant->doReceiverFeedback = true;

    auto offset = (rtp.timestamp - pParticipant->offsetEstimate);
    auto latency = (int32_t)(rtpMidiClock.Now() - offset);

    if (pParticipant->firstMessageReceived == true)
        // avoids first message to generate sequence exception
        // as we do not know the last sequenceNr received.
        pParticipant->firstMessageReceived = false;
    else
    {
        // RFC 3550 serial arithmetic: uint16 wrap 65535 -> 0 is delta 1, not loss.
        int16_t delta = (int16_t)(rtp.sequenceNr - pParticipant->receiveSequenceNr);
        if (delta > 1) {
            if (nullptr != _exceptionCallback)
                _exceptionCallback(ssrc, ReceivedPacketsDropped, (int32_t)(delta - 1));
        }
    }

    if (nullptr != _receivedRtpCallback)
        _receivedRtpCallback(pParticipant->ssrc, rtp, latency);

    pParticipant->receiveSequenceNr = rtp.sequenceNr;
    pParticipant->lastSyncExchangeTime = now;   // CHANGED: MIDI is as good as a heartbeat (manageSynchronization)
    FlMidiBridge::noteHeard();   // CHANGED: for the status page
    FlMidiBridge::noteRtpTiming(rtp.ssrc, rtp.timestamp);   // CHANGED: jt= / jm=
}

// The listener's half of the stock code (no APPLEMIDI_INITIATOR): a session
// not heard from for Settings::CK_MaxTimeOut (FL_EXHIBIT_SESSION_TIMEOUT_MS,
// RtpSettings) is ended. Runs in every available(), so every rtpMidi.read().
template <>
inline void AppleMIDISession<WiFiUDP, FlMidiBridge::RtpSettings, DefaultPlatform>::manageSynchronization()
{
    for (size_t i = 0; i < participants.size();)
    {
        auto pParticipant = &participants[i];
        if (pParticipant->ssrc == 0)
        {
            i++;
            continue;
        }

        // The initiator must check in with the listener at least once every 60 seconds;
        // otherwise the responder may assume that the initiator has died and terminate the session.
        if (now - pParticipant->lastSyncExchangeTime > Settings::CK_MaxTimeOut)
        {
            if (nullptr != _exceptionCallback)
                _exceptionCallback(ssrc, ListenerTimeOutException, 0);

            // CHANGED: the BY only when no other session here is on the same
            // address: it carries our SSRC, and that device would end its
            // other session with it too (header comment).
            bool shared = false;
            for (size_t j = 0; j < participants.size(); j++)
                if (j != i && participants[j].remoteIP == pParticipant->remoteIP)
                    shared = true;
            Serial.printf("[midi] rtp session of %s silent for %lu ms: ended%s\n",
                          pParticipant->remoteIP.toString().c_str(),
                          (unsigned long)(now - pParticipant->lastSyncExchangeTime),
                          shared ? ", no BY (another session on that address)" : " with a BY");
            if (!shared)
            {
                FlMidiBridge::quietAfterOwnBye(millis());
                sendEndSession(pParticipant);   // BY, then the disconnect callback
            }
            else if (nullptr != _disconnectedCallback)
            {
                _disconnectedCallback(pParticipant->ssrc);
            }
            participants.erase(i);
            continue;
        }
        i++;
    }
}

// The panel runs the same library version: its RTP timestamps count at this
// rate, which fl_rtp_jitter.h assumes.
static_assert(MIDI_SAMPLING_RATE_DEFAULT == FlRtpJitter::CLOCK_HZ,
              "fl_rtp_jitter.h assumes the AppleMIDI media clock of 3.5.0 (10 kHz)");

END_APPLEMIDI_NAMESPACE

namespace FlMidiBridge {

// What APPLEMIDI_CREATE_INSTANCE would expand to, spelled out: that macro
// would define globals named MIDI / AppleMIDI. The USB object is usbMidi.
inline Session   rtpSession(FL_EXHIBIT_RTP_SESSION, FL_EXHIBIT_RTP_PORT);
inline Interface rtpMidi(rtpSession);
// Constructing USBMIDI registers the MIDI interface with TinyUSB; it must
// exist before USB.begin() assembles the configuration descriptor. A static
// object is constructed before setup() runs.
inline USBMIDI   usbMidi(FL_EXHIBIT_USB_PRODUCT);

// Configured by a host that is awake. A pulled cable or a sleeping laptop
// leaves the device suspended rather than unmounted.
inline bool usbReady() { return tud_midi_mounted() && !tud_suspended(); }

// ── Bridge-task-only state and helpers ────────────────────────────────────
// Connected sessions by SSRC. The library calls "connected" for every data
// invitation it answers (a retried one too) and "disconnected" for sessions
// that never finished the handshake, so a plain counter drifts.
inline ssrc_t   peerSsrc[RtpSettings::MaxNumberOfParticipants] = {};
inline uint32_t peerIp[RtpSettings::MaxNumberOfParticipants] = {};    // from acceptedTokens
inline uint32_t peerOrder[RtpSettings::MaxNumberOfParticipants] = {}; // higher = joined later
inline uint32_t peerJoins = 0;

// The address the control invitation for this SSRC came from, 0 if unknown.
inline uint32_t acceptedIp(ssrc_t s) {
    for (const AcceptedToken& t : acceptedTokens)
        if (t.ssrc == s) return t.ip;
    return 0;
}

// Peer count, and the address of the newest session for the status page.
inline void publishPeers() {
    int32_t n = 0;
    uint32_t newestIp = 0, newestOrder = 0;
    for (size_t i = 0; i < RtpSettings::MaxNumberOfParticipants; ++i) {
        if (peerSsrc[i] == 0) continue;
        ++n;
        if (peerIp[i] != 0 && peerOrder[i] >= newestOrder) {
            newestOrder = peerOrder[i];
            newestIp = peerIp[i];
        }
    }
    peers.store(n, std::memory_order_relaxed);
    panelIp.store(newestIp, std::memory_order_relaxed);
}
inline bool addPeer(ssrc_t s) {
    int freeSlot = -1;
    for (size_t i = 0; i < RtpSettings::MaxNumberOfParticipants; ++i) {
        if (peerSsrc[i] == s) return false;
        if (peerSsrc[i] == 0 && freeSlot < 0) freeSlot = (int)i;
    }
    if (freeSlot < 0) return false;   // cannot happen: one slot per participant
    peerSsrc[freeSlot]  = s;
    peerIp[freeSlot]    = acceptedIp(s);
    peerOrder[freeSlot] = ++peerJoins;
    publishPeers();
    return true;
}
inline bool removePeer(ssrc_t s) {
    for (size_t i = 0; i < RtpSettings::MaxNumberOfParticipants; ++i) {
        if (peerSsrc[i] == s) {
            peerSsrc[i] = 0;
            peerIp[i] = 0;
            publishPeers();
            return true;
        }
    }
    return false;
}

inline bool     programOwed = false;   // the newest Program Change (and its mirror) is not in the FIFO
inline bool     mirrorOwed = false;    // its Program Change is in the FIFO, its mirror is not
inline uint8_t  knobOwed = 0;          // bit i: knob value i goes out after the owed program / mirror
inline bool     stalled = false;       // no write has succeeded since the last failure
inline uint32_t stallStartMs = 0;      // first failure of this run of failures
inline uint32_t lastFailMs = 0;
inline uint32_t lastResendTryMs = 0;
inline uint32_t lastResyncMs = 0;

inline void noteWriteOk() { stalled = false; }
inline void noteWriteFailed(uint32_t now) {
    // A failure long after the previous one starts a new run: a single drop
    // must not make a later burst look like a port nobody reads.
    if (!stalled || now - lastFailMs > FL_EXHIBIT_STALL_MS) stallStartMs = now;
    stalled = true;
    lastFailMs = now;
}
// Writes have kept failing for FL_EXHIBIT_STALL_MS and still do: nobody reads.
inline bool longStall(uint32_t now) {
    return stalled && lastFailMs - stallStartMs >= FL_EXHIBIT_STALL_MS &&
           now - lastFailMs <= FL_EXHIBIT_STALL_MS;
}

inline bool writeUsb(uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t cin = status >> 4;             // cable 0; CIN = status high nibble
    const bool twoBytes = (cin == 0xC || cin == 0xD);
    midiEventPacket_t packet = {cin, status, d1, twoBytes ? (uint8_t)0 : d2};
    return usbReady() && usbMidi.writePacket(&packet);
}

// A message from the panel: written with a few frames of patience, counted in
// tx or dropped.
inline bool forward(uint8_t status, uint8_t d1, uint8_t d2) {
    bool ok = writeUsb(status, d1, d2);
    // A burst while the laptop reads: wait a frame or two for room. Once
    // writes have failed for a while, nobody is reading; do not hold the
    // Wi-Fi side up for it.
    for (int i = 0; !ok && i < FL_EXHIBIT_WRITE_RETRY_MS && usbReady() && !longStall(millis()); ++i) {
        vTaskDelay(1);
        ok = writeUsb(status, d1, d2);
    }

    if (ok) {
        txCount.fetch_add(1, std::memory_order_relaxed);
        noteWriteOk();
    } else {
        droppedCount.fetch_add(1, std::memory_order_relaxed);
        noteWriteFailed(millis());
    }
    return ok;
}

inline bool mirrorEnabled() { return FL_EXHIBIT_PROGRAM_CC != 0; }
inline uint8_t mirrorStatus() {
    return (uint8_t)(0xB0 | ((FL_EXHIBIT_PROGRAM_CC_CHANNEL - 1) & 0x0F));
}

inline bool knobResendEnabled() { return FL_EXHIBIT_KNOB_RESEND != 0; }
inline uint8_t knobStatus() {
    return (uint8_t)(0xB0 | ((FL_EXHIBIT_KNOB_CC_CHANNEL - 1) & 0x0F));
}

// A Control Change from the panel (ch = channel - 1): keep it if it is a knob.
inline void rememberKnob(uint8_t ch, uint8_t control, uint8_t value) {
    if (!knobResendEnabled() || ch != FL_EXHIBIT_KNOB_CC_CHANNEL - 1) return;
    const int i = (int)control - FL_EXHIBIT_KNOB_CC_FIRST;
    if (i < 0 || i >= KNOB_COUNT) return;
    knobValue[i].store(value, std::memory_order_relaxed);
    knobSeen.fetch_or((uint8_t)(1u << i), std::memory_order_release);
}

// The current pattern is owed again: its Program Change, then the mirror,
// then the knob values. A fresh Program Change from the panel pays the first
// two (forwardProgram); the knob values stay owed until they are written.
inline void owePattern() {
    programOwed = true;
    knobOwed = knobResendEnabled() ? KNOB_ALL : 0;
}

// A Program Change from the panel, then its mirror, with the same patience
// and counters. Whatever did not reach the FIFO is owed.
inline void forwardProgram(uint8_t status, uint8_t program) {
    mirrorOwed = false;                 // a newer program supersedes an owed mirror
    if (!forward(status, program, 0)) {
        owePattern();                   // the mirror and the knob values are owed with it
        return;
    }
    programOwed = false;                // knobOwed stays: they follow this program instead
    if (!mirrorEnabled()) return;
    if (forward(mirrorStatus(), (uint8_t)FL_EXHIBIT_PROGRAM_CC, program))
        mirrorCount.fetch_add(1, std::memory_order_relaxed);
    else
        mirrorOwed = true;
}

// Something asked for the current pattern again (the laptop or the operator).
inline void owe(uint32_t now) {
    if (lastResyncMs != 0 && now - lastResyncMs < FL_EXHIBIT_RESYNC_MIN_MS) return;
    lastResyncMs = now | 1;
    owePattern();
    lastResendTryMs = 0;
}

// Re-send what is owed, in this order: the Program Change, its mirror, the
// knob values. One try per FL_EXHIBIT_RESEND_RETRY_MS; a failed write ends
// the try, so nothing goes out ahead of what is still owed before it.
inline void maybeResendProgram() {
    const int32_t program = lastProgram.load(std::memory_order_relaxed);
    if (program < 0) {                  // nothing to owe yet
        programOwed = false;
        mirrorOwed = false;
        knobOwed = 0;
        return;
    }
    if (!(programOwed || mirrorOwed || knobOwed) || !usbReady()) return;
    const uint32_t now = millis();
    if (lastResendTryMs != 0 && now - lastResendTryMs < FL_EXHIBIT_RESEND_RETRY_MS) return;
    lastResendTryMs = now | 1;

    if (programOwed) {
        const uint8_t status = 0xC0 | ((lastProgramChannel.load(std::memory_order_relaxed) - 1) & 0x0F);
        if (!writeUsb(status, (uint8_t)program, 0)) {
            noteWriteFailed(now);
            return;
        }
        programOwed = false;
        noteWriteOk();
        txCount.fetch_add(1, std::memory_order_relaxed);
        resendCount.fetch_add(1, std::memory_order_relaxed);
        mirrorOwed = mirrorEnabled();
    }
    if (mirrorOwed) {
        if (!writeUsb(mirrorStatus(), (uint8_t)FL_EXHIBIT_PROGRAM_CC, (uint8_t)program)) {
            noteWriteFailed(now);
            return;
        }
        mirrorOwed = false;
        noteWriteOk();
        txCount.fetch_add(1, std::memory_order_relaxed);
        mirrorCount.fetch_add(1, std::memory_order_relaxed);
    }
    // The knob values as they are now; a knob the panel has not sent is skipped.
    const uint8_t seen = knobSeen.load(std::memory_order_relaxed);
    for (uint8_t i = 0; i < KNOB_COUNT && knobOwed != 0; ++i) {
        const uint8_t bit = (uint8_t)(1u << i);
        if (!(knobOwed & bit)) continue;
        if (seen & bit) {
            const uint8_t value = knobValue[i].load(std::memory_order_relaxed);
            if (!writeUsb(knobStatus(), (uint8_t)(FL_EXHIBIT_KNOB_CC_FIRST + i), value)) {
                noteWriteFailed(now);
                return;
            }
            noteWriteOk();
            txCount.fetch_add(1, std::memory_order_relaxed);
            knobResendCount.fetch_add(1, std::memory_order_relaxed);
        }
        knobOwed = (uint8_t)(knobOwed & ~bit);
    }
}

inline bool isResyncRequest(const midiEventPacket_t& in) {
    return (in.header & 0x0F) == 0x0B &&
           in.byte1 == (uint8_t)(0xB0 | ((FL_EXHIBIT_RESYNC_CHANNEL - 1) & 0x0F)) &&
           in.byte2 == (uint8_t)FL_EXHIBIT_RESYNC_CC && in.byte3 > 0;
}

// From the web console (any task): the bridge task sends it.
inline bool requestResend() {
    if (lastProgram.load(std::memory_order_relaxed) < 0) return false;
    webResendRequested.store(true, std::memory_order_relaxed);
    return true;
}

inline void handleRtpMessage() {
    using MIDI_NAMESPACE::MidiType;
    rxCount.fetch_add(1, std::memory_order_relaxed);
    lastRxMs.store(millis() | 1, std::memory_order_relaxed);

    const MidiType type = rtpMidi.getType();
    const uint8_t ch = (uint8_t)((rtpMidi.getChannel() - 1) & 0x0F);
    const uint8_t d1 = rtpMidi.getData1() & 0x7F;
    const uint8_t d2 = rtpMidi.getData2() & 0x7F;
    switch (type) {
        case MidiType::ControlChange:
            rememberKnob(ch, d1, d2);
            forward(0xB0 | ch, d1, d2);
            break;
        case MidiType::ProgramChange:
            lastProgram.store(d1, std::memory_order_relaxed);
            lastProgramChannel.store(ch + 1, std::memory_order_relaxed);
            forwardProgram(0xC0 | ch, d1);
            break;
        case MidiType::NoteOn:   // velocity 0 already arrives as NoteOff
            forward(0x90 | ch, d1, d2);
            break;
        case MidiType::NoteOff:
            forward(0x80 | ch, d1, d2);
            break;
        default:
            ignoredCount.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

// A stamp is millis() | 1 (0 = never), so it can be 1 ms ahead of millis().
// Measured from t & ~1 it never is (0, not 49 days, for the same
// millisecond), and an age still holds up to 49.7 days.
inline int64_t ageMs(const std::atomic<uint32_t>& stamp) {
    const uint32_t t = stamp.load(std::memory_order_relaxed);
    return t ? (int64_t)(uint32_t)(millis() - (t & ~1u)) : (int64_t)-1;
}

// ── PF monitor (header comment; the protocol is in fl_pf_monitor.h) ────────

// Literals, so tools/check_exhibit_image.py can find them in the image.
inline const char STATUS_REQUEST_TEXT[] = "F0 7D 50 46 01 F7";
inline const char PANEL_REBOOT_PATH[] = "/api/wifi/reboot";

// From loop(), never from the bridge task: the Wi-Fi driver's station list
// and channel, and the DHCP server's leases (the lease lookup is an IPC call
// into the lwIP task, as for /api/status). The station whose lease is the
// panel session's address gives prs=. "AP up" is the core's AP-started bit
// (set on WIFI_EVENT_AP_START, cleared on AP_STOP); it is clear during the
// boot silence, before WiFi.mode() starts the AP.
inline void pollHotspot() {
    wifi_sta_list_t sta{};
    const bool up = WiFi.AP.started();
    const bool listed = up && esp_wifi_ap_get_sta_list(&sta) == ESP_OK;
    apRunning.store(up ? 1 : 0, std::memory_order_relaxed);
    apStationCount.store(listed ? (int16_t)sta.num : (int16_t)(up ? -1 : 0), std::memory_order_relaxed);
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    const bool tuned = up && esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0;
    apChannelNow.store(tuned ? (int16_t)primary : (int16_t)-1, std::memory_order_relaxed);

    uint64_t entry = 0;
    const uint32_t ip = panelIp.load(std::memory_order_relaxed);
    wifi_sta_mac_ip_list_t leases{};
    if (listed && ip != 0 && sta.num > 0 && esp_wifi_ap_get_sta_list_with_ip(&sta, &leases) == ESP_OK) {
        for (int i = 0; i < leases.num && i < ESP_WIFI_MAX_CONN_NUM; ++i) {
            if (leases.sta[i].ip.addr != ip) continue;
            for (int j = 0; j < sta.num && j < ESP_WIFI_MAX_CONN_NUM; ++j)
                if (memcmp(sta.sta[j].mac, leases.sta[i].mac, 6) == 0)
                    entry = ((uint64_t)ip << 32) | (uint32_t)(int32_t)sta.sta[j].rssi;
        }
    }
    panelRssiEntry.store(entry, std::memory_order_relaxed);
}

inline void ipText(uint32_t ip, char out[16]) {
    snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip & 0xFFu), (unsigned)((ip >> 8) & 0xFFu),
             (unsigned)((ip >> 16) & 0xFFu), (unsigned)(ip >> 24));
}

// ── "reboot panel": its own short task (header comment) ───────────────────

// Where to send it, from that task: the newest session's address, else the
// one hotspot station with a DHCP lease. 0 with the reason in why.
inline uint32_t panelRebootTarget(char* why, size_t cap) {
    const uint32_t ip = panelIp.load(std::memory_order_relaxed);
    if (ip != 0) return ip;
    wifi_sta_list_t sta{};
    wifi_sta_mac_ip_list_t leases{};
    if (!WiFi.AP.started() || esp_wifi_ap_get_sta_list(&sta) != ESP_OK ||
        esp_wifi_ap_get_sta_list_with_ip(&sta, &leases) != ESP_OK) {
        snprintf(why, cap, "no panel session and the hotspot is not up");
        return 0;
    }
    uint32_t only = 0;
    int withLease = 0;
    for (int i = 0; i < leases.num && i < ESP_WIFI_MAX_CONN_NUM; ++i) {
        if (leases.sta[i].ip.addr == 0) continue;
        ++withLease;
        only = leases.sta[i].ip.addr;
    }
    if (withLease == 1) return only;
    if (withLease == 0) snprintf(why, cap, "no panel session and no device on the hotspot");
    else snprintf(why, cap, "no panel session and %d devices on the hotspot", withLease);
    return 0;
}

// POST PANEL_REBOOT_PATH to the panel and read the status line: true for
// HTTP 200 (the panel answers {"ok":true,"rebooting":true}, then restarts).
// The result, either way, as an ack message in msg.
inline bool postPanelReboot(uint32_t ip, char* msg, size_t cap) {
    char addr[16];
    ipText(ip, addr);
    WiFiClient client;
    if (!client.connect(IPAddress(ip), FL_EXHIBIT_PANEL_HTTP_PORT, FL_EXHIBIT_PANEL_HTTP_MS)) {
        snprintf(msg, cap, "no connection to panel %s", addr);
        return false;
    }
    char request[128];
    const int n = snprintf(request, sizeof request,
                           "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                           PANEL_REBOOT_PATH, addr);
    if (n <= 0 || (size_t)n >= sizeof request ||
        client.write(reinterpret_cast<const uint8_t*>(request), (size_t)n) != (size_t)n) {
        client.stop();
        snprintf(msg, cap, "panel %s closed the connection", addr);
        return false;
    }
    char line[48];
    size_t len = 0;
    const uint32_t start = millis();
    while (len < sizeof line - 1 && millis() - start < FL_EXHIBIT_PANEL_HTTP_MS) {
        const int c = client.available() > 0 ? client.read() : -1;
        if (c < 0) {
            if (!client.connected()) break;
            delay(5);
            continue;
        }
        if (c == '\r' || c == '\n') break;
        line[len++] = (char)c;
    }
    line[len] = '\0';
    client.stop();
    int code = 0;
    if (len >= 12 && strncmp(line, "HTTP/1.", 7) == 0 && line[8] == ' ') code = atoi(line + 9);
    if (code == 200) {
        snprintf(msg, cap, "panel %s is rebooting", addr);
        return true;
    }
    if (code > 0) snprintf(msg, cap, "panel %s answered HTTP %d", addr, code);
    else snprintf(msg, cap, "no HTTP answer from panel %s", addr);
    return false;
}

// The whole request, from its task; the bridge task picks the result up.
inline void runPanelReboot() {
    char msg[FlPfMonitor::ACK_MSG_MAX + 1] = "";
    bool ok = false;
    const uint32_t ip = panelRebootTarget(msg, sizeof msg);
    if (ip != 0) {
        char addr[16];
        ipText(ip, addr);
        ok = postPanelReboot(ip, msg, sizeof msg);
        Serial.printf("[midi] pf monitor: POST http://%s%s: %s\n", addr, PANEL_REBOOT_PATH, msg);
    }
    if (ok) panelReboots.fetch_add(1, std::memory_order_relaxed);
    memcpy(panelRebootMsg, msg, sizeof panelRebootMsg);
    panelRebootOk = ok;
    panelRebootDone.store(true, std::memory_order_release);
}

inline void panelRebootTask(void*) {
    runPanelReboot();
    vTaskDelete(nullptr);
}

// ── Self-recovery (header comment; the decisions are in fl_recovery.h) ─────

// The boot record: RTC memory the start-up code never clears, so no
// initialiser (fl_recovery.h). Written by setup() (bootBegin), and just before
// a restart by whichever task asks for it or by the task watchdog's
// interrupt, always under bootLock.
RTC_NOINIT_ATTR static FlRecovery::BootRecord bootRecord;
inline portMUX_TYPE bootLock = portMUX_INITIALIZER_UNLOCKED;
inline FlRecovery::BootInfo bootInfo;     // this boot's bc= / why=, set by bootBegin() before the task starts
inline int resetReason = FlRecovery::RST_UNKNOWN;   // esp_reset_reason(), likewise

inline std::atomic<uint32_t> heartbeat{0};       // bridge passes; loop()'s check reads it
inline std::atomic<uint32_t> heartbeatMs{0};     // millis() of the newest pass; the watchdog hook reads it
inline std::atomic<bool>     taskWdtOn{false};   // the task is subscribed to the task watchdog
inline std::atomic<bool>     bridgeStalled{false};   // loop() has seen no pass for FL_EXHIBIT_BRIDGE_STALL_MS
inline std::atomic<uint32_t> usbReconnects{0};   // soft reconnects since boot (ur=)
inline std::atomic<uint8_t>  usbWatchPhase{0};   // FlRecovery::UsbWatch::Phase, for /api/status
inline FlRecovery::HeartbeatWatch bridgeWatch(FL_EXHIBIT_BRIDGE_STALL_MS);   // loop() only
inline FlRecovery::UsbWatch usbWatch(FlRecovery::UsbConfig{                 // bridge task only
    FL_EXHIBIT_USB_ASK_WINDOW_MS, FL_EXHIBIT_USB_ASK_MIN, FL_EXHIBIT_USB_ASK_GAP_MS, FL_EXHIBIT_USB_OFF_MS,
    FL_EXHIBIT_USB_VERIFY_MS, FL_EXHIBIT_USB_SETTLE_MS, FL_EXHIBIT_USB_RECONNECT_MIN_MS});

// First thing in setup(), before beginUsb() starts the task that reports it.
inline void bootBegin() {
    resetReason = (int)esp_reset_reason();
    portENTER_CRITICAL(&bootLock);
    bootInfo = FlRecovery::startBoot(bootRecord, resetReason == ESP_RST_POWERON);
    portEXIT_CRITICAL(&bootLock);
}

// The serial log's boot line.
inline void logBoot() {
    const bool noted = bootInfo.note != FlRecovery::Note::None;
    Serial.printf("[FlowLocal] boot %lu since power-on, reset reason %s%s%s, automatic restarts %lu of %u\n",
                  (unsigned long)bootInfo.boots, FlRecovery::resetReasonName(resetReason),
                  noted ? ", noted " : "", FlRecovery::noteName(bootInfo.note),
                  (unsigned long)bootInfo.recoveries, (unsigned)FL_EXHIBIT_RECOVERY_RESTARTS_MAX);
}

// Automatic restarts since power-on, the ones noted this boot included.
inline uint32_t recoveriesSoFar() {
    portENTER_CRITICAL(&bootLock);
    const uint32_t n = FlRecovery::valid(bootRecord) ? bootRecord.recoveries : 0;
    portEXIT_CRITICAL(&bootLock);
    return n;
}
inline bool recoveryLeft() {
    return FlRecovery::recoveryAllowed(recoveriesSoFar(), FL_EXHIBIT_RECOVERY_RESTARTS_MAX);
}

// Any task: note why for the next boot, then restart. An automatic restart
// (bridge_hung, usb_wedged) only while fewer than
// FL_EXHIBIT_RECOVERY_RESTARTS_MAX happened since power-on; false, logged,
// when refused. (The host test's ESP.restart() returns: true then.)
inline bool restartFor(FlRecovery::Note note) {
    const bool automatic = FlRecovery::isRecovery(note);
    portENTER_CRITICAL(&bootLock);
    const uint32_t before = FlRecovery::valid(bootRecord) ? bootRecord.recoveries : 0;
    const bool allowed = !automatic || FlRecovery::recoveryAllowed(before, FL_EXHIBIT_RECOVERY_RESTARTS_MAX);
    if (allowed) FlRecovery::noteRestart(bootRecord, note);
    portEXIT_CRITICAL(&bootLock);
    if (!allowed) {
        Serial.printf("[boot] %s: no automatic restart left (%lu since power-on, the most): press RST or replug\n",
                      FlRecovery::noteName(note), (unsigned long)before);
        return false;
    }
    if (automatic)
        Serial.printf("[boot] restarting FlowLocal (%s, automatic restart %lu of %u since power-on)\n",
                      FlRecovery::noteName(note), (unsigned long)(before + 1),
                      (unsigned)FL_EXHIBIT_RECOVERY_RESTARTS_MAX);
    Serial.flush();
    ESP.restart();
    return true;
}

// The task watchdog's interrupt, just before its panic: if this task's
// heartbeat is what stopped, the next boot says bridge_hung (rsn=task_wdt).
// A restart FlowLocal already asked for keeps its own note: esp_restart()
// runs shutdown handlers (Wi-Fi's among them), and one that never returns
// leaves the task watchdog to finish that restart. Called from
// esp_task_wdt_isr_user_handler() below; interrupt context.
inline void noteTaskWatchdog() {
    if (!taskWdtOn.load(std::memory_order_acquire)) return;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);   // millis(), fit for an interrupt
    if (now - heartbeatMs.load(std::memory_order_relaxed) < FL_EXHIBIT_BRIDGE_STALL_MS) return;   // another task's
    portENTER_CRITICAL_ISR(&bootLock);
    if (!FlRecovery::notePending(bootRecord)) FlRecovery::noteRestart(bootRecord, FlRecovery::Note::BridgeHung);
    portEXIT_CRITICAL_ISR(&bootLock);
}

// This task, before its first pass (once). Not once the automatic restarts
// are used up: the watchdog's panic would restart FlowLocal anyway.
inline bool taskWdtAsked = false;   // this task only
inline void subscribeTaskWatchdog() {
    if (taskWdtAsked) return;
    taskWdtAsked = true;
    if (!recoveryLeft()) {
        Serial.printf("[midi] bridge watchdog: task watchdog off, %lu automatic restarts since power-on (the most)\n",
                      (unsigned long)recoveriesSoFar());
        return;
    }
    const bool ok = esp_task_wdt_add(nullptr) == ESP_OK;
    taskWdtOn.store(ok, std::memory_order_release);
    Serial.printf("[midi] bridge watchdog: task watchdog %s; loop() restarts after %u ms without a pass\n",
                  ok ? "on" : "FAILED to subscribe", (unsigned)FL_EXHIBIT_BRIDGE_STALL_MS);
}

// This task, every pass.
inline void beat() {
    heartbeatMs.store(millis(), std::memory_order_relaxed);
    heartbeat.fetch_add(1, std::memory_order_release);
    if (taskWdtOn.load(std::memory_order_relaxed)) esp_task_wdt_reset();
}

// loop(), every pass (500 ms), so only after setup(): no pass of the bridge
// task for FL_EXHIBIT_BRIDGE_STALL_MS is a restart (bridge_hung).
inline void watchBridge() {
    if (taskHandle == nullptr) return;   // never started: nothing to watch (the status page says so)
    const uint32_t now = millis();
    const bool stalled = bridgeWatch.stalled(now, heartbeat.load(std::memory_order_acquire));
    if (stalled == bridgeStalled.load(std::memory_order_relaxed)) return;
    bridgeStalled.store(stalled, std::memory_order_relaxed);
    if (!stalled) {
        Serial.println("[midi] bridge watchdog: the bridge task passes again");
        return;
    }
    Serial.printf("[midi] bridge watchdog: no pass of the bridge task for %lu ms\n",
                  (unsigned long)bridgeWatch.quietMs(now));
    restartFor(FlRecovery::Note::BridgeHung);
}

inline const char* usbWatchPhaseName(uint8_t phase) {
    switch ((FlRecovery::UsbWatch::Phase)phase) {
        case FlRecovery::UsbWatch::Phase::Off:       return "off the bus";
        case FlRecovery::UsbWatch::Phase::Verifying: return "reconnected, waiting for a reply";
        default:                                     return "watching";
    }
}

// This task, every pass, after the replies: the USB watch's step, and what
// it asks for.
inline void maybeRecoverUsb() {
    const FlRecovery::UsbSample s{millis(), tud_mounted(), usbReady(),
                                  statusRequests.load(std::memory_order_relaxed),
                                  statusReplies.load(std::memory_order_relaxed)};
    const FlRecovery::UsbStep step = usbWatch.step(s);
    usbWatchPhase.store((uint8_t)usbWatch.phase(), std::memory_order_relaxed);
    switch (step) {
        case FlRecovery::UsbStep::Disconnect:
            usbReconnects.fetch_add(1, std::memory_order_relaxed);
            Serial.printf("[midi] usb watch: %lu status requests in %lu s, no reply got out%s: soft reconnect %lu\n",
                          (unsigned long)usbWatch.asked(), (unsigned long)(usbWatch.askedSpanMs() / 1000),
                          usbWatch.askedWhileSuspended() ? " (TinyUSB called the bus suspended)" : "",
                          (unsigned long)usbWatch.reconnects());
            tud_disconnect();
            break;
        case FlRecovery::UsbStep::Connect:
            tud_connect();
            Serial.printf("[midi] usb watch: back on the bus; a status reply must get out within %u s\n",
                          (unsigned)(FL_EXHIBIT_USB_VERIFY_MS / 1000));
            break;
        case FlRecovery::UsbStep::Recovered:
            Serial.println("[midi] usb watch: a status reply got out: USB works again");
            break;
        case FlRecovery::UsbStep::Inconclusive:
            Serial.println("[midi] usb watch: back, but no status request since: watching again");
            break;
        case FlRecovery::UsbStep::Restart:
            Serial.printf("[midi] usb watch: the soft reconnect did not help (%s)\n",
                          usbWatch.lastRemounted() ? "status requests still get no reply"
                                                   : "the laptop did not take the device back");
            restartFor(FlRecovery::Note::UsbWedged);
            break;
        default:
            break;
    }
}

// ── Status replies and command acks (bridge task only) ─────────────────────

inline FlPfMonitor::SysexRx sysexIn;
inline FlPfMonitor::CommandGate commandGate(FL_EXHIBIT_MON_CMD_MIN_MS);
inline bool     replyOwed = false;      // a status request waits for its reply
inline uint32_t replyAskedMs = 0;
inline uint32_t lastReplyMs = 0;        // last reply written or given up, 0 = none yet
inline uint8_t  replyMsg[FlPfMonitor::REPLY_MAX];

struct Ack {
    uint8_t  cmd = 0;
    bool     ok = false;
    bool     restart = false;   // FlowLocal restarts FL_EXHIBIT_MON_RESTART_MS after this ack (12, 13)
    uint32_t askedMs = 0;
    char     msg[FlPfMonitor::ACK_MSG_MAX + 1] = "";
};
inline Ack      acks[FL_EXHIBIT_MON_ACK_QUEUE];   // oldest at ackHead
inline uint8_t  ackHead = 0;
inline uint8_t  ackCount = 0;
inline uint32_t sessionEnds = 0;        // disconnect callbacks, for "reconnect panel"'s count
inline bool     restartAsked = false;   // "reboot FlowLocal" or "Wi-Fi channel" accepted ...
inline uint32_t restartAskedMs = 0;     // ... at
inline uint32_t restartAtMs = 0;        // restart due at (its ack is out), 0 = not yet known
inline uint8_t  restartCmd = 0;         // the command that asked, for the log
inline FlRecovery::Note restartNote = FlRecovery::Note::Command;   // why= for the next boot
inline uint32_t lastSpreadMs = 0;

inline const char* commandName(uint8_t cmd) {
    switch (cmd) {
        case FlPfMonitor::CMD_RECONNECT_PANEL:  return "reconnect panel";
        case FlPfMonitor::CMD_REBOOT_PANEL:     return "reboot panel";
        case FlPfMonitor::CMD_REBOOT_FLOWLOCAL: return "reboot FlowLocal";
        case FlPfMonitor::CMD_SET_CHANNEL:      return "Wi-Fi channel";
        default:                                return "?";
    }
}

// Logs the command's outcome and queues its ack; false when the queue was full.
inline bool queueAck(uint8_t cmd, bool ok, const char* msg, uint32_t now, bool restart = false) {
    Serial.printf("[midi] pf monitor: command %02X (%s) %s: %s\n", (unsigned)cmd, commandName(cmd),
                  ok ? "ok" : "refused", msg);
    if (!ok) commandsRefused.fetch_add(1, std::memory_order_relaxed);
    if (ackCount >= FL_EXHIBIT_MON_ACK_QUEUE) {
        acksLost.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Ack& a = acks[(ackHead + ackCount) % FL_EXHIBIT_MON_ACK_QUEUE];
    a.cmd = cmd;
    a.ok = ok;
    a.restart = restart;
    a.askedMs = now;
    snprintf(a.msg, sizeof a.msg, "%s", msg);
    ++ackCount;
    return true;
}

// The oldest ack is done: written, or given up.
inline void popAck(bool written, uint32_t now) {
    const Ack& a = acks[ackHead];
    (written ? acksSent : acksLost).fetch_add(1, std::memory_order_relaxed);
    if (a.restart && restartAsked)
        restartAtMs = (now + FL_EXHIBIT_MON_RESTART_MS) | 1;
    ackHead = (uint8_t)((ackHead + 1) % FL_EXHIBIT_MON_ACK_QUEUE);
    --ackCount;
}

// 12 and 13: restart once the ack is out (maybeRestart), with this note.
inline void askRestart(uint8_t cmd, FlRecovery::Note note, const char* msg, uint32_t now) {
    restartAsked = true;
    restartAskedMs = now;
    restartAtMs = 0;
    restartCmd = cmd;
    restartNote = note;
    if (!queueAck(cmd, true, msg, now, /*restart=*/true))
        restartAtMs = (now + FL_EXHIBIT_MON_RESTART_MS) | 1;
}

// 10: BY to every session the library holds; each leaves through the
// disconnect callback (sessionEnds).
inline void reconnectPanel(uint32_t now) {
    const uint8_t cmd = FlPfMonitor::CMD_RECONNECT_PANEL;
    if (!rtpReady.load(std::memory_order_acquire)) {
        queueAck(cmd, false, "no RTP yet: the hotspot is still starting", now);
        return;
    }
    const uint32_t before = sessionEnds;
    rtpSession.sendEndSession();
    const uint32_t ended = sessionEnds - before;
    if (ended == 0) {
        queueAck(cmd, false, "no RTP session to end", now);
        return;
    }
    quietAfterOwnBye(now);
    char msg[FlPfMonitor::ACK_MSG_MAX + 1];
    snprintf(msg, sizeof msg, "ended %u RTP session%s, the panel invites again", (unsigned)ended,
             ended == 1 ? "" : "s");
    queueAck(cmd, true, msg, now);
}

// 11: starts the request's task; its ack follows when that task is done.
inline void rebootPanel(uint32_t now) {
    const uint8_t cmd = FlPfMonitor::CMD_REBOOT_PANEL;
    if (panelRebootBusy.load(std::memory_order_acquire)) {
        queueAck(cmd, false, "busy: the last reboot request is still running", now);
        return;
    }
    panelRebootBusy.store(true, std::memory_order_relaxed);
    panelRebootDone.store(false, std::memory_order_relaxed);
    // Priority below AsyncTCP, on the Wi-Fi core, away from this task's.
    if (xTaskCreatePinnedToCore(panelRebootTask, "fl-pf-http", FL_EXHIBIT_PANEL_HTTP_STACK, nullptr, 2,
                                nullptr, 0) != pdPASS) {
        panelRebootBusy.store(false, std::memory_order_relaxed);
        queueAck(cmd, false, "could not start the request (no memory)", now);
    }
}

// The request's result, once its task has written it.
inline void takePanelRebootResult(uint32_t now) {
    if (!panelRebootDone.load(std::memory_order_acquire)) return;
    panelRebootDone.store(false, std::memory_order_relaxed);
    queueAck(FlPfMonitor::CMD_REBOOT_PANEL, panelRebootOk, panelRebootMsg, now);
    panelRebootBusy.store(false, std::memory_order_release);
}

// 13: the hotspot's channel from the next boot, then a restart (header
// comment). arg is already one of 0, 1..13, 127 (runCommand). The choice goes
// to NVS here, in this task: a flash write of a few ms (tens when NVS has to
// tidy a page), well inside the task watchdog's 5 s and loop()'s 3 s, and
// MIDI is about to stop for the restart anyway.
inline void setChannel(uint8_t arg, uint32_t now) {
    const uint8_t cmd = FlPfMonitor::CMD_SET_CHANNEL;
    char msg[FlPfMonitor::ACK_MSG_MAX + 1];
    if (FL_EXHIBIT_AP_CHANNEL != 0) {
        snprintf(msg, sizeof msg, "channel fixed at %d by this build: nothing changed", (int)FL_EXHIBIT_AP_CHANNEL);
        queueAck(cmd, false, msg, now);
        return;
    }
    uint8_t choice = arg;
    if (arg == FlPfMonitor::CHANNEL_NEXT) {
        const int16_t current = apChannelNow.load(std::memory_order_relaxed);   // loop()'s poll of the driver
        if (current <= 0) {
            queueAck(cmd, false, "no channel to go on from: the hotspot is still starting", now);
            return;
        }
        choice = FlApChannel::nextCandidate(current);
    }
    if (!FlChannelStore::save(choice)) {
        queueAck(cmd, false, "could not save the channel (NVS): nothing changed", now);
        return;
    }
    if (choice == FlPfMonitor::CHANNEL_AUTO)
        snprintf(msg, sizeof msg, "auto from next boot, restarting now");
    else
        snprintf(msg, sizeof msg, "channel %u from next boot, restarting now", (unsigned)choice);
    askRestart(cmd, FlRecovery::Note::Channel, msg, now);
}

// A command with its guard, from the laptop. One sent too soon, or 13 with an
// argument no channel can be, is refused with an ack at most once a second
// per command (the others are only counted), so a monitor repeating it
// cannot keep this task writing acks. A bad argument is refused before the
// gate: the corrected command need not wait out the 3 s.
inline bool     tooSoonAcked[FlPfMonitor::CMD_COUNT] = {};
inline uint32_t tooSoonAckMs[FlPfMonitor::CMD_COUNT] = {};
inline void refuseLimited(uint8_t cmd, const char* msg, uint32_t now) {
    const unsigned i = (unsigned)(cmd - FlPfMonitor::CMD_FIRST) % FlPfMonitor::CMD_COUNT;
    if (tooSoonAcked[i] && now - tooSoonAckMs[i] < 1000) {
        commandsRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    tooSoonAcked[i] = true;
    tooSoonAckMs[i] = now;
    queueAck(cmd, false, msg, now);
}
inline void runCommand(uint8_t cmd, uint8_t arg, uint32_t now) {
    commandsReceived.fetch_add(1, std::memory_order_relaxed);
    if (cmd == FlPfMonitor::CMD_SET_CHANNEL && !FlPfMonitor::validChannelChoice(arg)) {
        char msg[FlPfMonitor::ACK_MSG_MAX + 1];
        snprintf(msg, sizeof msg, "no channel %u: 0 auto, 1-13, 127 next of 1/6/11", (unsigned)arg);
        refuseLimited(cmd, msg, now);
        return;
    }
    if (!commandGate.allow(cmd, now)) {
        char msg[FlPfMonitor::ACK_MSG_MAX + 1];
        snprintf(msg, sizeof msg, "too soon: once per %u s, wait %u s",
                 (unsigned)(FL_EXHIBIT_MON_CMD_MIN_MS / 1000),
                 (unsigned)((commandGate.waitMs(cmd, now) + 999) / 1000));
        refuseLimited(cmd, msg, now);
        return;
    }
    switch (cmd) {
        case FlPfMonitor::CMD_RECONNECT_PANEL:
            reconnectPanel(now);
            break;
        case FlPfMonitor::CMD_REBOOT_PANEL:
            rebootPanel(now);
            break;
        case FlPfMonitor::CMD_REBOOT_FLOWLOCAL:
            askRestart(cmd, FlRecovery::Note::Command, "restarting: USB back in seconds, hotspot in about 13 s", now);
            break;
        case FlPfMonitor::CMD_SET_CHANNEL:
            setChannel(arg, now);
            break;
        default:
            break;
    }
}

// "reboot FlowLocal" and "Wi-Fi channel": once the ack is out (or given up),
// after FL_EXHIBIT_MON_RESTART_MS; 2 s after the command at the latest.
inline void maybeRestart() {
    if (!restartAsked) return;
    const uint32_t now = millis();
    const bool due = (restartAtMs != 0 && (int32_t)(now - restartAtMs) >= 0) || now - restartAskedMs >= 2000;
    if (!due) return;
    restartAsked = false;
    Serial.printf("[midi] pf monitor: restarting FlowLocal (command %02X)\n", (unsigned)restartCmd);
    restartFor(restartNote);
}

inline void noteStatusRequest(uint32_t now) {
    statusRequests.fetch_add(1, std::memory_order_relaxed);
    if (replyOwed || (lastReplyMs != 0 && now - lastReplyMs < FL_EXHIBIT_MON_MIN_MS)) {
        statusUnanswered.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    replyOwed = true;
    replyAskedMs = now;
}

inline void giveUpReply(uint32_t now) {
    replyOwed = false;
    lastReplyMs = now | 1;
    statusAbandoned.fetch_add(1, std::memory_order_relaxed);
}

// Everything waiting is given up (nobody reads, or the FIFO did not drain).
inline void giveUpAll(uint32_t now) {
    while (ackCount) popAck(false, now);
    if (replyOwed) giveUpReply(now);
}

// The reply's values as they are now.
inline FlPfMonitor::Status monitorStatus() {
    FlPfMonitor::Status s;
    const uint32_t now = millis();
    s.fw  = FL_EXHIBIT_MON_FW_TAG;
    s.up  = (uint32_t)(esp_timer_get_time() / 1000000);   // 64-bit: no wrap at 49 days like millis()
    s.ap  = apRunning.load(std::memory_order_relaxed);
    s.ch  = apChannelNow.load(std::memory_order_relaxed);
    s.chm = (int8_t)channelMode.load(std::memory_order_relaxed);
    s.cl  = apStationCount.load(std::memory_order_relaxed);
    s.cm  = FL_AP_MAX_CONN;
    s.ps  = peers.load(std::memory_order_relaxed);
    s.pip = panelIp.load(std::memory_order_relaxed);
    const uint64_t rssi = panelRssiEntry.load(std::memory_order_relaxed);
    if (s.pip != 0 && (uint32_t)(rssi >> 32) == s.pip) {
        s.prsKnown = true;
        s.prs = (int32_t)(uint32_t)rssi;
    }
    s.hd  = ageMs(lastHeardMs);
    s.ra  = ageMs(lastRxMs);
    s.lp  = lastProgram.load(std::memory_order_relaxed);
    if (knobResendEnabled()) {   // otherwise the knob values are not kept: kn= is empty
        s.knobs = KNOB_COUNT;
        s.knobSeen = knobSeen.load(std::memory_order_acquire);
        for (uint8_t i = 0; i < KNOB_COUNT; ++i) s.knobValue[i] = knobValue[i].load(std::memory_order_relaxed);
    }
    s.jt  = rtpTiming.jitterMs();
    s.jm  = rtpTiming.spreadMs(now);
    s.rx  = rxCount.load(std::memory_order_relaxed);
    s.tx  = txCount.load(std::memory_order_relaxed);
    s.dr  = droppedCount.load(std::memory_order_relaxed);
    s.se  = sessions.load(std::memory_order_relaxed);
    s.to  = rtpTimeouts.load(std::memory_order_relaxed);
    s.rj  = rtpRejected.load(std::memory_order_relaxed);
    s.gp  = rtpGaps.load(std::memory_order_relaxed);
    s.rr  = resyncRequests.load(std::memory_order_relaxed);
    s.ud  = usbInDiscarded.load(std::memory_order_relaxed);
    s.sr  = statusReplies.load(std::memory_order_relaxed) + 1;   // this reply included
    s.hi  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s.ur  = usbReconnects.load(std::memory_order_relaxed);
    s.rsn = FlRecovery::resetReasonName(resetReason);
    s.bc  = bootInfo.boots;
    s.why = FlRecovery::noteName(bootInfo.note);
    s.scanned = channelScored.load(std::memory_order_acquire);
    for (size_t i = 0; s.scanned && i < FlApChannel::CANDIDATE_COUNT; ++i) s.cs[i] = channelScore[i];
    return s;
}

// setup(), before FlWifi::begin(): how this boot's channel is chosen, so chm=
// is right through the boot silence (and again after, in case the hotspot
// had to leave a manual channel).
inline void publishChannelMode(FlApChannel::Mode mode) {
    channelMode.store((uint8_t)mode, std::memory_order_relaxed);
}
// setup(), after FlWifi::begin(): the boot scan's scores for cs=, if it ran.
// Written once, before the flag the bridge task reads them by.
inline void publishChannelScan(bool scanned, const FlApChannel::Scores& scores) {
    if (!scanned || channelScored.load(std::memory_order_relaxed)) return;
    for (size_t i = 0; i < FlApChannel::CANDIDATE_COUNT; ++i) channelScore[i] = scores.score[i];
    channelScored.store(true, std::memory_order_release);
}

// One whole message, F0 to F7, with nothing between: this task is the only
// USB writer and does nothing else until the FIFO has taken the last packet
// or FL_EXHIBIT_MON_WRITE_MS have passed. The host empties the FIFO every
// frame, so a ~200-byte reply takes a few ms. False when it was given up.
inline bool writeMessage(uint8_t kind, const char* text, size_t textLen) {
    const size_t len = FlPfMonitor::buildMessage(replyMsg, sizeof replyMsg, kind, text, textLen);
    const size_t count = FlPfMonitor::packetCount(len);
    const uint32_t start = millis();
    for (size_t i = 0; i < count;) {
        uint8_t b[4];
        FlPfMonitor::replyPacket(replyMsg, len, i, b);
        midiEventPacket_t packet = {b[0], b[1], b[2], b[3]};
        if (usbReady() && usbMidi.writePacket(&packet)) {
            ++i;
            continue;
        }
        const uint32_t now = millis();
        if (!usbReady() || now - start >= FL_EXHIBIT_MON_WRITE_MS) {
            noteWriteFailed(now);
            return false;
        }
        vTaskDelay(1);
    }
    noteWriteOk();
    return true;
}

inline bool writeReply() {
    char text[FlPfMonitor::TEXT_MAX + 1];
    const size_t textLen = FlPfMonitor::formatStatus(text, sizeof text, monitorStatus());
    const bool ok = writeMessage(FlPfMonitor::STATUS_REPLY, text, textLen);
    const uint32_t now = millis();
    if (!ok) {
        giveUpReply(now);
        return false;
    }
    replyOwed = false;
    lastReplyMs = now | 1;
    if (statusReplies.fetch_add(1, std::memory_order_relaxed) == 0)
        Serial.printf("[midi] pf monitor: first status reply sent (%u bytes)\n", (unsigned)(textLen + 6));
    return true;
}

// End of a pass: the panel's messages and any owed re-send went first. Acks
// go before a reply; once one is given up, so is everything after it.
inline void maybeWriteMonitor() {
    if (!replyOwed && ackCount == 0) return;
    const uint32_t now = millis();
    if (!usbReady() || longStall(now)) {   // nobody to read them
        giveUpAll(now);
        return;
    }
    if (programOwed || mirrorOwed || knobOwed) {   // the re-send goes first
        while (ackCount && now - acks[ackHead].askedMs >= FL_EXHIBIT_MON_HOLD_MS) popAck(false, now);
        if (replyOwed && now - replyAskedMs >= FL_EXHIBIT_MON_HOLD_MS) giveUpReply(now);
        return;
    }
    while (ackCount) {
        const Ack& a = acks[ackHead];
        char text[FlPfMonitor::ACK_TEXT_MAX + 1];
        const size_t n = FlPfMonitor::formatAck(text, sizeof text, a.cmd, a.ok, a.msg);
        const bool ok = writeMessage(FlPfMonitor::COMMAND_ACK, text, n);
        popAck(ok, millis());
        if (!ok) {
            giveUpAll(millis());
            return;
        }
    }
    if (replyOwed) writeReply();
}

inline void task(void*) {
    taskRunning.store(true);
    subscribeTaskWatchdog();
    for (;;) {
        const bool ready = usbReady();
        if (ready != usbMounted.load(std::memory_order_relaxed)) {
            usbMounted.store(ready);
            Serial.printf("[midi] usb host %s\n", ready ? "connected" : "gone");
            // Fresh bus or a host that just woke: hand it the current pattern
            // and the knob values.
            if (ready) {
                stalled = false;
                owePattern();
                lastResendTryMs = 0;
            }
            // A SysEx, a status request or an ack from before the bus went
            // away is not finished or delivered; the monitor asks again. A
            // command already accepted still runs.
            sysexIn.reset();
            giveUpAll(millis());
        }

        // Laptop → FlowLocal: only the resync request and the PF monitor's
        // SysEx mean anything. Read every pass so a DAW that sends to this
        // port never stalls, and whenever the MIDI interface is configured,
        // suspended or not: a host that sends is awake, and the USB watch
        // needs its status requests to see a suspend TinyUSB never saw end.
        // What is owed back still waits for usbReady().
        if (tud_midi_mounted()) {
            midiEventPacket_t in;
            for (int i = 0; i < 16 && usbMidi.readPacket(&in); ++i) {
                // SysEx first: a packet of it is done here. A packet that is
                // not SysEx goes on as before, after it has ended any SysEx.
                const FlPfMonitor::RxResult sx = sysexIn.feed(in.header, in.byte1, in.byte2, in.byte3);
                if (sx.discarded) usbInDiscarded.fetch_add(sx.discarded, std::memory_order_relaxed);
                if (sx.statusRequest) noteStatusRequest(millis());
                if (sx.command) runCommand(sx.command, sx.arg, millis());
                if (sx.sysex) continue;
                if (isResyncRequest(in)) {
                    resyncRequests.fetch_add(1, std::memory_order_relaxed);
                    owe(millis());
                } else {
                    usbInDiscarded.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (webResendRequested.exchange(false, std::memory_order_relaxed)) {
            webResends.fetch_add(1, std::memory_order_relaxed);
            lastResyncMs = 0;           // an operator's press is never merged away
            owe(millis());
        }
        takePanelRebootResult(millis());

        if (rtpReady.load(std::memory_order_acquire)) {
            for (int i = 0; i < FL_EXHIBIT_RX_BUDGET; ++i) {
                if (!rtpMidi.read()) break;
                handleRtpMessage();
            }
        }

        maybeResendProgram();
        maybeWriteMonitor();
        maybeRestart();
        maybeRecoverUsb();

        {   // jm= leaves the window by itself: keep /api/status's copy current
            const uint32_t now = millis();
            if (now - lastSpreadMs >= 250) {
                lastSpreadMs = now;
                rtpSpreadMs.store(rtpTiming.spreadMs(now), std::memory_order_relaxed);
            }
        }

        beat();
        stackFree.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
        vTaskDelay(1);
    }
}

// Call first thing in setup(), right after bootBegin(): the laptop sees the
// MIDI port while Wi-Fi is still coming up, and the bridge task starts at
// once, so status requests are answered through the hotspot's boot silence
// (RTP waits for begin()).
inline void beginUsb() {
    usbMidi.begin();
    USB.productName(FL_EXHIBIT_USB_PRODUCT);
    USB.manufacturerName(FL_EXHIBIT_USB_MANUFACTURER);
    const bool ok = USB.begin();
    usbStarted.store(ok);
    Serial.printf("[midi] usb device \"%s\" %s\n", FL_EXHIBIT_USB_PRODUCT,
                  ok ? "started (native USB jack)" : "FAILED to start");
    Serial.printf("[midi] usb watch: soft reconnect after %u status requests over %u s without a reply, "
                  "restart if none gets out %u s later\n", (unsigned)FL_EXHIBIT_USB_ASK_MIN,
                  (unsigned)(FL_EXHIBIT_USB_ASK_WINDOW_MS / 1000), (unsigned)(FL_EXHIBIT_USB_VERIFY_MS / 1000));
    if (xTaskCreatePinnedToCore(task, "fl-midi", FL_EXHIBIT_BRIDGE_STACK, nullptr,
                                FL_EXHIBIT_BRIDGE_PRIORITY, &taskHandle,
                                FL_EXHIBIT_BRIDGE_CORE) != pdPASS) {
        taskHandle = nullptr;
        Serial.println("[midi] bridge task FAILED to start");
    }
}

// After FlWifi::begin(): the UDP sockets need the network stack.
inline void begin() {
    // All session callbacks run inside rtpMidi.read() or sendEndSession(),
    // i.e. in the bridge task.
    rtpSession.setHandleConnected([](const ssrc_t& s, const char* name) {
        if (!addPeer(s)) return;   // a retried data invitation for a live session
        sessions.fetch_add(1, std::memory_order_relaxed);
        Serial.printf("[midi] rtp session joined by \"%s\" (%d)\n", name ? name : "?",
                      (int)peers.load());
    });
    rtpSession.setHandleDisconnected([](const ssrc_t& s) {
        ++sessionEnds;
        forgetToken(s);               // its token and address describe no listed session any more
        if (!removePeer(s)) return;   // never finished its handshake
        Serial.printf("[midi] rtp session left (%d)\n", (int)peers.load());
    });
    rtpSession.setHandleException([](const ssrc_t&,
                                     const APPLEMIDI_NAMESPACE::Exception& error, int32_t value) {
        using namespace APPLEMIDI_NAMESPACE;
        switch (error) {
            case ReceivedPacketsDropped:
                if (value > 0) rtpGaps.fetch_add((uint32_t)value, std::memory_order_relaxed);
                break;
            case TooManyParticipantsException:   // no longer raised: an invitation is never refused
                rtpRejected.fetch_add(1, std::memory_order_relaxed);
                break;
            case ListenerTimeOutException:       // manageSynchronization ends it right after
                rtpTimeouts.fetch_add(1, std::memory_order_relaxed);
                break;
            case BufferFullException:
            case ParseException:
            case UnexpectedParseException:
                rtpErrors.fetch_add(1, std::memory_order_relaxed);
                break;
            default:   // ParticipantNotFound is counted where it is answered
                break;
        }
    });

    rtpMidi.begin(MIDI_CHANNEL_OMNI);   // binds UDP 5004 / 5005
    rtpMidi.turnThruOff();              // never echo back to the panel
    rtpReady.store(true, std::memory_order_release);

    if (taskHandle == nullptr) Serial.println("[midi] bridge task is not running: nothing is forwarded");
    Serial.printf("[midi] rtp listener \"%s\" on %s:%u (data %u), ssrc %08lx\n",
                  FL_EXHIBIT_RTP_SESSION, WiFi.softAPIP().toString().c_str(),
                  (unsigned)FL_EXHIBIT_RTP_PORT, (unsigned)(FL_EXHIBIT_RTP_PORT + 1),
                  (unsigned long)rtpSession.getSynchronizationSource());
    if (mirrorEnabled())
        Serial.printf("[midi] program mirror: CC %u ch %u after every Program Change\n",
                      (unsigned)FL_EXHIBIT_PROGRAM_CC, (unsigned)FL_EXHIBIT_PROGRAM_CC_CHANNEL);
    else
        Serial.println("[midi] program mirror: off");
    if (knobResendEnabled())
        Serial.printf("[midi] knob re-send: CC %u-%u ch %u after every re-sent Program Change\n",
                      (unsigned)FL_EXHIBIT_KNOB_CC_FIRST, (unsigned)FL_EXHIBIT_KNOB_CC_LAST,
                      (unsigned)FL_EXHIBIT_KNOB_CC_CHANNEL);
    else
        Serial.println("[midi] knob re-send: off");
    Serial.printf("[midi] pf monitor: status request %s answered with F0 7D 50 46 02 <text> F7 "
                  "(v%d, fw=%s, at most one per %u ms)\n",
                  STATUS_REQUEST_TEXT, FlPfMonitor::PROTOCOL_VERSION, FL_EXHIBIT_MON_FW_TAG,
                  (unsigned)FL_EXHIBIT_MON_MIN_MS);
    Serial.printf("[midi] pf monitor: commands 10 reconnect panel, 11 reboot panel (POST %s), "
                  "12 reboot FlowLocal, 13 Wi-Fi channel (kept in NVS, then a restart), each at most once per %u s\n",
                  PANEL_REBOOT_PATH, (unsigned)(FL_EXHIBIT_MON_CMD_MIN_MS / 1000));
    Serial.printf("[midi] rtp sessions: never refused (the one heard from least recently makes room), "
                  "ended after %u s unheard, a repeated invitation gets its OK again\n",
                  (unsigned)(FL_EXHIBIT_SESSION_TIMEOUT_MS / 1000));
}

// How many knob values the bridge holds (for the status page's re-send answer).
inline int knownKnobs() {
    const uint8_t seen = knobSeen.load(std::memory_order_acquire);
    int n = 0;
    for (uint8_t i = 0; i < KNOB_COUNT; ++i) n += (seen >> i) & 1;
    return n;
}

// "192.168.66.23", or an empty string with no session.
inline String panelIpString() {
    const uint32_t ip = panelIp.load(std::memory_order_relaxed);
    return ip ? IPAddress(ip).toString() : String();
}

#define FL_EXHIBIT_STR_(x) #x
#define FL_EXHIBIT_STR(x) FL_EXHIBIT_STR_(x)
inline const char RESYNC_REQUEST_TEXT[] =
    "CC " FL_EXHIBIT_STR(FL_EXHIBIT_RESYNC_CC) " ch " FL_EXHIBIT_STR(FL_EXHIBIT_RESYNC_CHANNEL) " value>0";
inline const char PROGRAM_MIRROR_TEXT[] =
    "CC " FL_EXHIBIT_STR(FL_EXHIBIT_PROGRAM_CC) " ch " FL_EXHIBIT_STR(FL_EXHIBIT_PROGRAM_CC_CHANNEL) " value=program";
inline const char KNOB_RESEND_TEXT[] =
    "CC " FL_EXHIBIT_STR(FL_EXHIBIT_KNOB_CC_FIRST) "-" FL_EXHIBIT_STR(FL_EXHIBIT_KNOB_CC_LAST)
    " ch " FL_EXHIBIT_STR(FL_EXHIBIT_KNOB_CC_CHANNEL) " value=last";

inline void appendStatus(JsonDocument& doc) {
    JsonObject m = doc["midi"].to<JsonObject>();
    m["usb_product"]    = FL_EXHIBIT_USB_PRODUCT;
    m["usb_started"]    = usbStarted.load();
    m["usb_mounted"]    = usbMounted.load();
    m["rtp_session"]    = FL_EXHIBIT_RTP_SESSION;
    m["rtp_port"]       = FL_EXHIBIT_RTP_PORT;
    m["task_running"]   = taskRunning.load();
    m["peers"]          = peers.load();
    m["max_sessions"]   = (int)RtpSettings::MaxNumberOfParticipants;   // a copy: the member has no definition
    m["sessions"]       = sessions.load();
    m["rx"]             = rxCount.load();
    m["tx"]             = txCount.load();
    m["dropped"]        = droppedCount.load();
    m["ignored"]        = ignoredCount.load();
    m["resent_program"] = resendCount.load();
    m["resync_requests"] = resyncRequests.load();
    m["web_resends"]    = webResends.load();
    // Literals, so tools/check_exhibit_image.py can read the numbers out of the image.
    m["resync_request"] = RESYNC_REQUEST_TEXT;
    m["program_mirror"] = mirrorEnabled() ? PROGRAM_MIRROR_TEXT : "off";
    m["program_mirrors"] = mirrorCount.load();
    m["knob_resend"]    = knobResendEnabled() ? KNOB_RESEND_TEXT : "off";
    JsonArray knobs = m["knob_values"].to<JsonArray>();   // null: not sent by the panel yet
    const uint8_t seen = knobSeen.load(std::memory_order_acquire);
    for (uint8_t i = 0; knobResendEnabled() && i < KNOB_COUNT; ++i) {
        if (seen & (1u << i)) knobs.add((int)knobValue[i].load(std::memory_order_relaxed));
        else knobs.add(nullptr);
    }
    m["knob_resends"]   = knobResendCount.load();
    m["usb_in_discarded"] = usbInDiscarded.load();
    m["last_program"]   = lastProgram.load();
    m["last_program_channel"] = lastProgramChannel.load();
    m["last_rx_age_ms"] = ageMs(lastRxMs);
    m["last_heard_age_ms"] = ageMs(lastHeardMs);
    m["rtp_gaps"]       = rtpGaps.load();
    m["rtp_errors"]     = rtpErrors.load();
    m["rtp_rejected"]   = rtpRejected.load();
    m["rtp_timeouts"]   = rtpTimeouts.load();
    m["rtp_session_timeout_ms"] = (uint32_t)FL_EXHIBIT_SESSION_TIMEOUT_MS;
    m["rtp_replaced"]   = rtpReplaced.load();
    m["rtp_evicted"]    = rtpEvicted.load();
    m["rtp_retries_answered"] = rtpRetryOks.load();
    m["rtp_unknown"]    = rtpUnknown.load();
    m["last_unknown_age_ms"] = ageMs(lastUnknownMs);
    m["rtp_bye_sent"]   = rtpByeSent.load();
    m["task_stack_free"] = stackFree.load();
    // PF monitor: the request it answers, its fw= tag and its counters.
    m["status_request"] = STATUS_REQUEST_TEXT;
    m["status_fw"]      = FL_EXHIBIT_MON_FW_TAG;
    m["status_requests"] = statusRequests.load();
    m["status_replies"] = statusReplies.load();
    m["status_unanswered"] = statusUnanswered.load();
    m["status_abandoned"] = statusAbandoned.load();
    // PF monitor commands, and the RTP arrival timing (jt= / jm=).
    m["monitor_commands"] = commandsReceived.load();
    m["monitor_commands_refused"] = commandsRefused.load();
    m["monitor_acks"]   = acksSent.load();
    m["monitor_acks_lost"] = acksLost.load();
    m["panel_reboots"]  = panelReboots.load();
    m["panel_reboot_path"] = PANEL_REBOOT_PATH;
    m["rtp_jitter_ms"]  = rtpJitterMs.load();
    m["rtp_spread_ms"]  = rtpSpreadMs.load();
    // Self-recovery: the USB watch (ur=) and the bridge watchdog.
    m["usb_reconnects"] = usbReconnects.load();
    m["usb_watch"]      = usbWatchPhaseName(usbWatchPhase.load());
    m["bridge_stalled"] = bridgeStalled.load();
    m["task_watchdog"]  = taskWdtOn.load();
    // Why this boot happened (rsn= / bc= / why=), at the top level.
    JsonObject r = doc["restart"].to<JsonObject>();
    r["reset_reason"]          = FlRecovery::resetReasonName(resetReason);
    r["restart_note"]          = FlRecovery::noteName(bootInfo.note);
    r["boots_since_power_on"]  = bootInfo.boots;
    r["recovery_restarts"]     = recoveriesSoFar();
    r["recovery_restarts_max"] = FL_EXHIBIT_RECOVERY_RESTARTS_MAX;
}

}  // namespace FlMidiBridge

// The task watchdog calls this from its interrupt when a subscribed task
// missed its timeout, just before the panic (this core's sdkconfig). A weak
// symbol in ESP-IDF; nothing else in this build defines it.
#if CONFIG_ESP_TASK_WDT_PANIC
extern "C" void esp_task_wdt_isr_user_handler(void) {
    FlMidiBridge::noteTaskWatchdog();
}
#endif
