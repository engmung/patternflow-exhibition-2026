#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — the hotspot channel the laptop's PF monitor chose (command 13),
// kept in NVS across restarts. Exhibit build only.
//
//   0      automatic: the boot scan picks the least busy of 1 / 6 / 11
//   1..13  that channel on every boot until the monitor sends another choice
//
// Written by the bridge task when command 13 arrives (FlowLocal restarts
// right after, fl_midi_bridge.h); read once per boot by FlWifi::planChannel()
// (fl_wifi.h). FL_EXHIBIT_AP_CHANNEL, when a build sets it, wins over it.
// Its own namespace: the regular build's "flowlocal" keys (hotspot name,
// password, upstream Wi-Fi) are never read by this build, and never written.
// Erasing NVS (esptool erase-region 0x9000 0x5000, or erase-flash) forgets it.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include <cstdint>
#include <Preferences.h>

namespace FlChannelStore {

constexpr const char* NAMESPACE = "fl_exhibit";   // at most 15 characters (NVS)
constexpr const char* KEY       = "ap_ch";

// 0 (automatic) when nothing was ever stored, when NVS cannot be read, or for
// a value no channel can be.
inline uint8_t load() {
    Preferences p;
    // A read-only begin() fails while the namespace has never been written:
    // nothing chosen, automatic.
    if (!p.begin(NAMESPACE, /*readOnly=*/true)) return 0;
    const uint8_t v = p.getUChar(KEY, 0);
    p.end();
    return v <= 13 ? v : 0;
}

// True once the choice (0..13) is committed to flash.
inline bool save(uint8_t choice) {
    if (choice > 13) return false;
    Preferences p;
    if (!p.begin(NAMESPACE, /*readOnly=*/false)) return false;
    const bool ok = p.putUChar(KEY, choice) == 1;   // nvs_set_u8 + nvs_commit
    p.end();
    return ok;
}

}  // namespace FlChannelStore
