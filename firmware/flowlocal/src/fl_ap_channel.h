#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — picking the hotspot's channel from a scan, the part without
// hardware
//
// The exhibit hotspot runs on 1, 6 or 11 (20 MHz wide, so the three do not
// overlap each other). Every other network the boot scan heard adds to each
// candidate's score:
//
//   weight(RSSI) x overlap(channel distance)
//
//   weight   RSSI + 100, clamped to 0..70 (-100 dBm and below count 0,
//            -30 dBm and above 70): a network next to the booth counts more
//            than one at the far end of the hall, and every one heard counts
//   overlap  percent of a 20 MHz channel that a network |d| channels away
//            shares with it: 100, 75, 50, 25, 10 for d = 0..4, 0 from 5 on
//
// A 40 MHz network (HT40) also sends on its secondary channel, 4 above or
// below its primary; that half counts at half weight. The candidate with the
// lowest score wins; on a tie the preferred channel (the build's fallback)
// wins, then the lower one. With no network heard every score is 0 and the
// preferred channel is kept.
//
// Where the boot's channel comes from (Mode): FL_EXHIBIT_AP_CHANNEL when the
// build sets one (fixed, no scan); else the choice the laptop's PF monitor
// stored with its command 13 (manual: 1..13, kept in NVS, fl_channel_store.h;
// the scan still runs so its scores are there to compare); else the scan
// (auto). Command 13's "next" goes round 1 -> 6 -> 11 -> 1 from the channel
// the hotspot is on.
//
// Pure C++, so the host test compiles it as it is.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>

namespace FlApChannel {

constexpr uint8_t CANDIDATES[3] = {1, 6, 11};
constexpr size_t  CANDIDATE_COUNT = 3;

// What the scan reports for one network. second as ESP-IDF's
// wifi_second_chan_t: 0 none (HT20), 1 above, 2 below.
struct Network {
    uint8_t primary = 0;
    uint8_t second = 0;
    int8_t  rssi = -100;
};

constexpr uint32_t weight(int rssi) {
    return rssi + 100 <= 0 ? 0u : (rssi + 100 >= 70 ? 70u : (uint32_t)(rssi + 100));
}
constexpr uint32_t overlapPercent(int distance) {
    return distance < 0 ? overlapPercent(-distance)
         : distance == 0 ? 100u : distance == 1 ? 75u : distance == 2 ? 50u
         : distance == 3 ? 25u : distance == 4 ? 10u : 0u;
}

struct Scores {
    uint32_t score[CANDIDATE_COUNT] = {};   // same order as CANDIDATES
    uint16_t networks = 0;                  // networks counted
};

// A channel number fit for 2.4 GHz (1..14); 0 for anything else.
constexpr int validChannel(int ch) { return ch >= 1 && ch <= 14 ? ch : 0; }

inline void add(Scores& s, const Network& n) {
    const int primary = validChannel(n.primary);
    if (primary == 0) return;
    const uint32_t w = weight(n.rssi);
    int secondary = 0;
    if (n.second == 1) secondary = validChannel(primary + 4);
    else if (n.second == 2) secondary = validChannel(primary - 4);
    for (size_t i = 0; i < CANDIDATE_COUNT; ++i) {
        s.score[i] += w * overlapPercent(primary - (int)CANDIDATES[i]);
        if (secondary) s.score[i] += (w * overlapPercent(secondary - (int)CANDIDATES[i])) / 2;
    }
    ++s.networks;
}

// The candidate with the lowest score; ties go to preferred (when it is a
// candidate), then to the lower channel.
inline uint8_t choose(const Scores& s, uint8_t preferred) {
    size_t best = CANDIDATE_COUNT;
    for (size_t i = 0; i < CANDIDATE_COUNT; ++i)
        if (CANDIDATES[i] == preferred) best = i;
    for (size_t i = 0; i < CANDIDATE_COUNT; ++i) {
        if (best == CANDIDATE_COUNT || s.score[i] < s.score[best]) best = i;
    }
    return CANDIDATES[best];
}

// Where this boot's channel came from. The values are the PF monitor's chm=
// numbers (fl_pf_monitor.h CHM_*): fixed 0, auto 1, manual 2.
enum class Mode : uint8_t {
    Fixed  = 0,   // FL_EXHIBIT_AP_CHANNEL (compile time), no scan
    Auto   = 1,   // the least busy of 1 / 6 / 11 by the boot scan
    Manual = 2,   // the PF monitor's command 13, stored in NVS
};

inline const char* modeName(Mode m) {
    switch (m) {
        case Mode::Fixed:  return "fixed";
        case Mode::Manual: return "manual";
        default:           return "auto";
    }
}

// A stored choice fit for the hotspot: 1..13 (2.4 GHz, as the build's own
// check allows); 0 or anything else means automatic.
constexpr bool validManual(int ch) { return ch >= 1 && ch <= 13; }

// compiled: FL_EXHIBIT_AP_CHANNEL (0 = none); stored: command 13's choice.
constexpr Mode modeFor(int compiled, int stored) {
    return compiled != 0 ? Mode::Fixed : validManual(stored) ? Mode::Manual : Mode::Auto;
}

// Command 13's "next": the next of 1 / 6 / 11 after the channel the hotspot
// is on, round (1 -> 6 -> 11 -> 1); a channel between them goes to the next
// one up (3 -> 6, 9 -> 11), one above 11 back to 1.
constexpr uint8_t nextCandidate(int current) {
    return current < 1 ? 1 : current < 6 ? 6 : current < 11 ? 11 : 1;
}

}  // namespace FlApChannel
