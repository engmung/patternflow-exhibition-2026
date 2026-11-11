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
// The setup is written to flash this long after the last change, so
// dragging a panel across the map is one write and not fifty.
#ifndef HUB_SAVE_AFTER_MS
#  define HUB_SAVE_AFTER_MS 1500
#endif
