// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble hub - compile-time settings
//
// Per-installation values go in hub_secrets.h (gitignored; copy
// hub_secrets.example.h). Anything defined there wins over the defaults here.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#if __has_include("hub_secrets.h")
#  include "hub_secrets.h"
#endif

#define HUB_VERSION "0.1.0"

// ── The hotspot ─────────────────────────────────────────────
#ifndef HUB_AP_SSID
#  define HUB_AP_SSID "PatternflowEnsemble"
#endif
#ifndef HUB_AP_PASS
#  define HUB_AP_PASS "change-this-password"
#  define HUB_AP_PASS_IS_DEFAULT 1
#endif
// 1, 6 or 11: the three 2.4 GHz channels that do not overlap at 20 MHz.
#ifndef HUB_AP_CHANNEL
#  define HUB_AP_CHANNEL 6
#endif
// Stations admitted at once: eight panels and two phones. The radio would
// take 15 (ESP_WIFI_MAX_CONN_NUM), but the hotspot's DHCP server remembers
// only eight leases (CONFIG_LWIP_DHCPS_MAX_STATION_NUM=8 in the Arduino
// core's build) and forgets the oldest when a ninth device asks, so two
// panels can end up with one address. For more than eight panels use router
// mode, below.
#ifndef HUB_AP_MAX_CONN
#  define HUB_AP_MAX_CONN 10
#endif
// The same address family as FlowLocal: the hub is .1, panels get .2 and up.
#ifndef HUB_AP_IP
#  define HUB_AP_IP 192, 168, 66, 1
#endif
// The hotspot stays off this long after every start. A panel that was
// associated before the hub restarted has to notice the hub is gone before
// it will join afresh; when the hotspot came back within a second, the
// exhibition's panel stayed deaf for over two minutes (FlowLocal EXHIBIT.md,
// measured 2026-09-19: 15 s of silence, and it was back 1 s after). USB and
// the console work through the silence.
#ifndef HUB_AP_START_DELAY_MS
#  define HUB_AP_START_DELAY_MS 12000
#endif
// Seconds without a frame after which the hotspot drops a station. ESP-IDF's
// 300 s would keep a panel that lost power "on the hotspot" for five minutes.
#ifndef HUB_AP_INACTIVE_S
#  define HUB_AP_INACTIVE_S 30
#endif

// ── Router mode ─────────────────────────────────────────────
// Define HUB_STA_SSID / HUB_STA_PASS (hub_secrets.h) and the hub joins that
// network instead of opening a hotspot. The panels join the same router and
// find the hub by broadcast, so nothing else changes - and an ordinary
// router has neither of the limits above.

// ── Names ───────────────────────────────────────────────────
// http://pf-ensemble.local/ in router mode, and the USB port's name.
#ifndef HUB_HOSTNAME
#  define HUB_HOSTNAME "pf-ensemble"
#endif
#ifndef HUB_USB_NAME
#  define HUB_USB_NAME "Patternflow Ensemble"
#endif
// The USB-MIDI interface beside the console (README 5½). 0 leaves the USB
// jack a serial port only.
#ifndef HUB_USB_MIDI
#  define HUB_USB_MIDI 1
#endif
// The USB jack's identity. Not the core's default 303A:1001: that is also the
// S3's own USB-Serial/JTAG unit, and a Windows that has had its WinUSB driver
// installed (ESP-IDF, Zadig) puts that driver on interface 2 of any 303A:1001 -
// which here is the MIDI interface, so no MIDI device appeared (2026-11-14).
// Any other product number gets Windows' own serial and MIDI drivers.
#ifndef HUB_USB_VID
#  define HUB_USB_VID 0x303A
#endif
#ifndef HUB_USB_PID
#  define HUB_USB_PID 0x8E50
#endif

// A console port that has said nothing for this long gets no more lines.
// The page sends a ping every three seconds.
#ifndef HUB_CONSOLE_QUIET_MS
#  define HUB_CONSOLE_QUIET_MS 10000
#endif
// The USB port taking nothing for this long counts as a console that has
// stopped reading; the hub then sends it nothing for HUB_USB_QUIET_MS.
#ifndef HUB_USB_STALL_MS
#  define HUB_USB_STALL_MS 500
#endif
#ifndef HUB_USB_QUIET_MS
#  define HUB_USB_QUIET_MS 2000
#endif
// 1 puts a line about the USB jack's two pipes (serial and MIDI) in the log
// every two seconds - what was handed over, what is waiting for the
// computer, what src/usb_s3.cpp had to repair - and answers /api/usb with
// the USB controller's registers. For when the jack misbehaves;
// -DHUB_USB_DEBUG=1.
#ifndef HUB_USB_DEBUG
#  define HUB_USB_DEBUG 0
#endif
// Everything that touches the USB controller on one core (src/usb_s3.cpp,
// point 1). 0 leaves TinyUSB's task and the hub's USB writer where they
// used to be, which is how the stalls were found and is of no other use.
#ifndef HUB_USB_ONE_CORE
#  define HUB_USB_ONE_CORE 1
#endif
// The setup is written to flash this long after the last change, so
// dragging a panel across the map is one write and not fifty.
#ifndef HUB_SAVE_AFTER_MS
#  define HUB_SAVE_AFTER_MS 1500
#endif
