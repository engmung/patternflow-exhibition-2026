#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — compile-time defaults
//
// Per-device overrides live in flowlocal_secrets.h (gitignored).
// Copy flowlocal_secrets.example.h → flowlocal_secrets.h and
// edit values there.  Any #define set in the secrets file wins;
// the #ifndef blocks here only fill in safe defaults.
// ═══════════════════════════════════════════════════════════

// Exhibition 2026 build (env:exhibit): Wi-Fi hotspot + RTP-MIDI -> USB-MIDI
// bridge only. Defined before the secrets file so it can pick credentials.
#ifndef FL_EXHIBIT
#  define FL_EXHIBIT 0
#endif

#if __has_include("flowlocal_secrets.h")
#  include "flowlocal_secrets.h"
#endif

// ── Version ──────────────────────────────────────────────────
#define FL_VERSION "0.7.0"

// ── AP (Access Point) ────────────────────────────────────────
// Fixed credentials shared with all Patternflow panels so they
// can connect automatically when FlowLocal is in range.
// Change them in flowlocal_secrets.h — never commit credentials.
#ifndef FL_AP_SSID
#  define FL_AP_SSID "PatternflowLocal"
#endif
#ifndef FL_AP_PASS
#  define FL_AP_PASS "patternflow"   // min 8 chars for WPA2
#endif
// AP channel (1-13).  Keep at 1 or 6 or 11 to avoid 2.4 GHz overlap.
// The exhibit build uses it only as the fallback of its automatic channel.
#ifndef FL_AP_CHANNEL
#  define FL_AP_CHANNEL 6
#endif
// Exhibit build: 0 = scan at boot and start the AP on the least busy of
// 1 / 6 / 11 (fl_wifi.h), 1..13 = always this channel, no scan.
#ifndef FL_EXHIBIT_AP_CHANNEL
#  define FL_EXHIBIT_AP_CHANNEL 0
#endif
// Exhibit build: seconds of silence after which the AP drops a station.
// ESP-IDF's default is 300 s, so a panel that lost power held its slot (and
// read as "on the hotspot") for five minutes. A live panel is never quiet
// that long: its RTP sync runs every 10 s and it invites every few seconds
// while no session is up. 10 is the driver's minimum; not stored in flash.
#ifndef FL_EXHIBIT_AP_INACTIVE_S
#  define FL_EXHIBIT_AP_INACTIVE_S 30
#endif
// Stations the AP admits at once. 4 is ESP-IDF's own softAP default, so the
// regular builds behave as before; the exhibit env sets 3 (panel + operator
// phone + one spare).
#ifndef FL_AP_MAX_CONN
#  define FL_AP_MAX_CONN 4
#endif
// AP IP / gateway / subnet — panels use .2+ addresses.
#ifndef FL_AP_IP
#  define FL_AP_IP      "192.168.66.1"
#endif
#ifndef FL_AP_GATEWAY
#  define FL_AP_GATEWAY "192.168.66.1"
#endif
#ifndef FL_AP_SUBNET
#  define FL_AP_SUBNET  "255.255.255.0"
#endif

// ── MQTT broker ───────────────────────────────────────────────
// PicoMQTT listens on FL_AP_IP:FL_MQTT_PORT (TCP) and :FL_MQTT_WS_PORT (WS).
// Panels and Director connect here instead of Mosquitto.
#ifndef FL_MQTT_PORT
#  define FL_MQTT_PORT 1883
#endif
#ifndef FL_MQTT_WS_PORT
#  define FL_MQTT_WS_PORT 9001   // WebSocket port for Director (browser)
#endif
#ifndef FL_MQTT_MAX_CLIENTS
#  define FL_MQTT_MAX_CLIENTS 8
#endif

// ── Web console ───────────────────────────────────────────────
#ifndef FL_WEB_PORT
#  define FL_WEB_PORT 80
#endif
// mDNS hostname — http://flowlocal.local in the browser.
#ifndef FL_HOSTNAME
#  define FL_HOSTNAME "flowlocal"
#endif

// ── NVS namespace for runtime settings ───────────────────────
#define FL_NVS_NS "flowlocal"

// ── NTP (when upstream STA is connected) ─────────────────────
#ifndef FL_NTP_SERVER
#  define FL_NTP_SERVER "pool.ntp.org"
#endif
// Default timezone offset minutes east of UTC (overridable in NVS / web UI).
#ifndef FL_TZ_OFFSET_MIN
#  define FL_TZ_OFFSET_MIN 0
#endif

#ifndef FL_USB_MOUNT
#  define FL_USB_MOUNT "/usb"
#endif

#ifndef FL_SD_CS
#  define FL_SD_CS   4
#endif
#ifndef FL_SD_MOSI
#  define FL_SD_MOSI 6
#endif
#ifndef FL_SD_SCK
#  define FL_SD_SCK  5
#endif
#ifndef FL_SD_MISO
#  define FL_SD_MISO 7
#endif
#ifndef FL_SD_HZ
#  define FL_SD_HZ   1000000
#endif

#include "src/fl_pins_display.h"
