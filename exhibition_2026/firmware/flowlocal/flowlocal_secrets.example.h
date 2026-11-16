#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — shared AP defaults (same as flowlocal_secrets.h)
//
// FL_AP_SSID / FL_AP_PASS are the fixed PatternflowLocal network.
// Panels must use the same values in patternflow_secrets.h
// (PF_WIFI_SSID / PF_WIFI_PASS). They are not private secrets —
// every touring kit needs the same known AP.
// ═══════════════════════════════════════════════════════════

// ── AP credentials (shared with every panel) ─────────────────
#define FL_AP_SSID "PatternflowLocal"
#define FL_AP_PASS "patternflow"

// ── Optional: also connect to an upstream router (STA mode) ──
// When defined the device runs AP+STA simultaneously: panels and
// Director connect to the AP, while FlowLocal reaches the internet
// for NTP sync. Leave commented for pure offline use.
// #define FL_STA_SSID "YOUR_FL_STA_SSID"
// #define FL_STA_PASS "YOUR_FL_STA_PASS"
