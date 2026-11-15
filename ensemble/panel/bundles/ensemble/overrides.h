// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - what this firmware calls itself, and what it sets
//
// Included from config.h before any default, so anything `#ifndef`-guarded
// anywhere in the tree can be set here. The feature's own tunables are in
// features/ensemble/ensemble_config.h.
//
// Wi-Fi is NOT here. Each panel joins the hub's network like any other
// Wi-Fi: from its own Wi-Fi page, or compiled in through the gitignored
// firmware/patternflow/patternflow_secrets.h (PF_WIFI_SSID / PF_WIFI_PASS),
// which is the quick way to prepare ten panels at once.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

// Reported in /api/status as `variant` / `variantVersion`, and to the hub,
// which shows it on the console beside each panel.
#define PF_VARIANT          "ensemble"
#define PF_VARIANT_VERSION  "v0.2.0"

// A hub that restarts must not leave the panels away for minutes. The core
// raises a panel's own hotspot 15 s after its network went (net_config.h),
// and from then on looks for a network only every five minutes, trying the
// saved ones in turn. A hub is back 13 s after a restart (1 s of boot and
// HUB_AP_START_DELAY_MS) and later after a firmware update. Seen 2026-11-14:
// the hub was flashed, both panels went to their own hotspots and were still
// there four minutes after the hub was back. The hotspot stays as it is for
// a panel that has no network at all; it just looks again every 20 s.
#define PF_HOTSPOT_STA_RETRY_MS 20000

// The show player's night schedule draws on a pattern of its own, Black. Its
// code has to be there for the player to build; it is NOT put in the pattern
// list (no PF_FEATURE_PRESETS), so every panel's numbers stay what they
// were: Origin, then the modules. The schedule is not used here.
#define PF_FEATURE_PRESET_INCLUDE "show/preset_black.h"

// The show player's tables: how many shows a panel lists, and how long a
// playlist may be. Its own numbers are 100 and 64, held in memory whether or
// not a show exists - 12.9 KB out of the one piece of memory a pattern module
// must fit in. A panel of an ensemble holds a handful (install.py makes the
// player read these; features_local.h refuses to build if it does not).
#define PF_SHOW_CACHE_MAX 16
#define PF_SHOW_PLAYLIST_MAX 8
