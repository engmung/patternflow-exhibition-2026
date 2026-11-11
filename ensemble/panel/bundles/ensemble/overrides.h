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
#define PF_VARIANT_VERSION  "v0.1.0"
