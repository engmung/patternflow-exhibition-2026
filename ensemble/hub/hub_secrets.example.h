// Patternflow Ensemble hub - this installation's Wi-Fi.
//
// Copy to hub_secrets.h (gitignored) and edit. The panels need the same
// network name and password: in the panel tree's patternflow_secrets.h
// (PF_WIFI_SSID / PF_WIFI_PASS), or typed in on each panel's Wi-Fi page.
#pragma once

// The hub's own hotspot (the default way).
#define HUB_AP_SSID "PatternflowEnsemble"
#define HUB_AP_PASS "change-this-password"   // 8 to 63 characters

// Router mode: uncomment both and the hub joins this network instead of
// opening a hotspot. The panels join the same router and find the hub by
// themselves. Use it from about ten panels on (see platformio.ini).
// #define HUB_STA_SSID "venue-router"
// #define HUB_STA_PASS "its-password"
