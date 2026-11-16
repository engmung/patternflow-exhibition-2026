#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — exhibit self-recovery, the part without hardware
//
// Pure C++ (no Arduino, TinyUSB or FreeRTOS), so the host test compiles it as
// it is. fl_midi_bridge.h does the rest: the RTC memory the boot record lives
// in, esp_reset_reason(), the task watchdog, tud_disconnect() / tud_connect()
// and esp_restart().
//
//   - resetReasonName(): esp_reset_reason() as the PF monitor's rsn=;
//   - BootRecord, startBoot(), noteRestart(): boots since power-on (bc=), and
//     the note FlowLocal leaves just before it restarts itself (why=), in RTC
//     memory that every reset but a power-on leaves alone; automatic restarts
//     are counted there too, so they stop after a few (the caller's limit);
//   - HeartbeatWatch: loop()'s check that the bridge task still passes;
//   - UsbWatch: the laptop keeps asking for status and no reply gets out:
//     take the device off the bus and back (a USB soft reconnect), and ask
//     for a restart if that did not help. Only arriving status requests can
//     start it: with nobody asking (no monitor, Live closed, laptop asleep) it
//     never acts.
//
// Every clock here is a 32-bit millisecond clock (millis()) that may wrap:
// times are only ever subtracted, never compared.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace FlRecovery {

// ── rsn=: why the chip last reset ─────────────────────────────────────────
// The values of ESP-IDF 5.5's esp_reset_reason_t (esp_system.h), in its
// order; fl_midi_bridge.h checks them against the real enum. On the S3 the RST
// button cuts the chip's power, so it reads as a power-on.
enum ResetReason : int {
    RST_UNKNOWN = 0, RST_POWERON, RST_EXT, RST_SW, RST_PANIC, RST_INT_WDT, RST_TASK_WDT, RST_WDT,
    RST_DEEPSLEEP, RST_BROWNOUT, RST_SDIO, RST_USB, RST_JTAG, RST_EFUSE, RST_PWR_GLITCH, RST_CPU_LOCKUP,
};

inline const char* resetReasonName(int reason) {
    static const char* const names[] = {
        "unknown", "poweron", "ext", "sw", "panic", "int_wdt", "task_wdt", "wdt", "deepsleep",
        "brownout", "sdio", "usb", "jtag", "efuse", "pwr_glitch", "cpu_lockup",
    };
    return reason >= 0 && reason < (int)(sizeof names / sizeof names[0]) ? names[reason] : "unknown";
}

// ── why=: what FlowLocal noted just before it restarted itself ────────────
// The next boot reports it once and forgets it. rsn= is then "sw"
// (esp_restart), or "task_wdt" when the task watchdog's reset was the bridge
// task's (the watchdog's hook notes that). Empty: a power-on, or a reset
// FlowLocal did not ask for (a crash, the chip's watchdogs, the brown-out
// detector, a firmware update from /update).
enum class Note : uint8_t {
    None = 0,
    Command,      // "cmd12"       the PF monitor's command 12, reboot FlowLocal
    Web,          // "web"         the status page's Reboot (POST /api/reboot)
    BridgeHung,   // "bridge_hung" the bridge task stopped passing
    UsbWedged,    // "usb_wedged"  requests kept coming, no reply got out, a soft reconnect did not help
    Channel,      // "cmd13"       the PF monitor's command 13 stored a Wi-Fi channel for the next boot
};
constexpr uint32_t NOTE_COUNT = 6;

inline const char* noteName(Note n) {
    switch (n) {
        case Note::Command:    return "cmd12";
        case Note::Web:        return "web";
        case Note::BridgeHung: return "bridge_hung";
        case Note::UsbWedged:  return "usb_wedged";
        case Note::Channel:    return "cmd13";
        default:               return "";
    }
}

// An automatic restart: counted, and allowed only while fewer than the
// caller's limit happened since power-on (recoveryAllowed).
constexpr bool isRecovery(Note n) { return n == Note::BridgeHung || n == Note::UsbWedged; }
constexpr bool recoveryAllowed(uint32_t recoveries, uint32_t max) { return recoveries < max; }

// ── bc= and why=: the boot record ─────────────────────────────────────────
// Lives in RTC memory that no reset but a power-on clears (RTC_NOINIT_ATTR,
// fl_midi_bridge.h): esp_restart(), a crash, the watchdogs and the brown-out
// detector's restart keep it. After a power-on it holds whatever the memory
// woke up with, as it may after a brown-out deep enough to lose RTC memory;
// magic and check tell a record this firmware wrote from that. No member
// initialisers: the variable must not be given an initialiser, or its
// section would be zeroed (or not placed) at start-up.
struct BootRecord {
    uint32_t magic;
    uint32_t boots;        // boots since power-on, the current one included
    uint32_t note;         // Note for the next boot, 0 = none
    uint32_t recoveries;   // automatic restarts (isRecovery notes) since power-on
    uint32_t check;
};
static_assert(std::is_trivially_default_constructible<BootRecord>::value,
              "BootRecord lives in RTC no-init memory: no constructor, no member initialisers");
constexpr uint32_t BOOT_MAGIC = 0x46524231u;   // "FRB1"; a new layout takes a new magic

constexpr uint32_t mix(uint32_t h, uint32_t v) {
    h ^= v;
    h *= 0x01000193u;   // FNV-1a prime, with a shift so every bit reaches every bit
    return h ^ (h >> 15);
}
constexpr uint32_t checkOf(const BootRecord& r) {
    return mix(mix(mix(mix(0x811C9DC5u, r.magic), r.boots), r.note), r.recoveries) ^ 0x5AA5C33Cu;
}
constexpr bool valid(const BootRecord& r) {
    return r.magic == BOOT_MAGIC && r.check == checkOf(r) && r.boots >= 1 && r.note < NOTE_COUNT;
}

// What this boot reports.
struct BootInfo {
    uint32_t boots = 1;           // bc=: 1 after a power-on
    Note     note = Note::None;   // why=
    uint32_t recoveries = 0;      // automatic restarts since power-on, the one that led here included
    bool     carried = false;     // the record came through the reset (false: power-on, or it did not)
};

// First thing at boot, before anything else writes the record. powerOn:
// esp_reset_reason() is a power-on; the count starts again then, whatever the
// memory holds. The note is taken out, so it is reported by this boot only.
inline BootInfo startBoot(BootRecord& r, bool powerOn) {
    BootInfo b;
    if (!powerOn && valid(r)) {
        b.carried = true;
        b.boots = r.boots < UINT32_MAX ? r.boots + 1 : r.boots;
        b.note = (Note)r.note;
        b.recoveries = r.recoveries;
    }
    r.magic = BOOT_MAGIC;
    r.boots = b.boots;
    r.note = 0;
    r.recoveries = b.recoveries;
    r.check = checkOf(r);
    return b;
}

// A note is waiting for the next boot: FlowLocal has decided to restart.
constexpr bool notePending(const BootRecord& r) { return valid(r) && r.note != 0; }

// Just before FlowLocal restarts itself. An automatic restart is counted.
// A record that is not valid (only if something overwrote RTC memory since
// startBoot) is started over as a first boot, so the note still gets through.
inline void noteRestart(BootRecord& r, Note n) {
    if (!valid(r)) startBoot(r, true);
    r.note = (uint32_t)n;
    if (isRecovery(n) && r.recoveries < UINT32_MAX) ++r.recoveries;
    r.check = checkOf(r);
}

// ── The bridge task's heartbeat, as loop() sees it ────────────────────────
// The task advances a count every pass (1 ms). stalled() is true once the
// count has not changed for limitMs. The first call only starts watching:
// loop() makes it after setup(), so nothing slow at boot (the hotspot's 12 s
// silence, the channel scan) can count against the task.
class HeartbeatWatch {
public:
    explicit constexpr HeartbeatWatch(uint32_t limitMs) : limit_(limitMs) {}
    bool stalled(uint32_t now, uint32_t beat) {
        if (!started_ || beat != beat_) {
            started_ = true;
            beat_ = beat;
            since_ = now;
            return false;
        }
        return now - since_ >= limit_;
    }
    // How long the count has stood still, 0 before the first call.
    uint32_t quietMs(uint32_t now) const { return started_ ? now - since_ : 0; }
    void reset() { started_ = false; }

private:
    uint32_t limit_;
    uint32_t beat_ = 0;
    uint32_t since_ = 0;
    bool     started_ = false;
};

// ── USB: requests keep coming, no reply gets out ──────────────────────────
// Evidence is status requests that arrived and got no reply written whole.
// A whole reply proves the laptop read the IN endpoint (a reply is longer
// than everything the device can hold on its side, which fl_midi_bridge.h
// checks), so any whole reply clears the evidence. "Wedged" is: at least
// minAsked unanswered requests, the first windowMs ago or more, none more
// than gapMs after the one before (so the monitor asked all along), while a
// host has the device configured, decided only as a request arrives: time
// alone never tips it, so evidence left over when the laptop stops asking
// (asleep, monitor closed) can never act. TinyUSB may think the bus
// suspended meanwhile (a resume it missed): requests that arrive prove it is
// not, and the evidence notes it.
//
// Then: Disconnect (off the bus for offMs), Connect, and within verifyMs:
//   - a reply gets out whole: Recovered;
//   - the host never took the device back (it was not seen leaving the
//     configured state and entering it again): Restart;
//   - it did, and requests came again (two or more, the first settleMs ago
//     or more) with no reply out: Restart;
//   - it did, and nobody asked since: Inconclusive, watching again.
// Soft reconnects are at most one per spacingMs, and that doubles after each
// one no reply followed (up to 32 x: 60 s, 2, 4, 8, 16, 32 min), so a wedge
// that neither a reconnect nor the restarts the caller still allows can mend
// does not take the port away from Live every minute; a reply that gets out
// starts it over. The evidence is kept while the next one waits. Restart is
// only asked for; the caller's restart limit decides.
struct UsbConfig {
    uint32_t windowMs;    // unanswered requests this long, from the first ...
    uint32_t minAsked;    // ... at least this many ...
    uint32_t gapMs;       // ... none further apart than this
    uint32_t offMs;       // off the bus this long
    uint32_t verifyMs;    // a reply must get out within this after Connect
    uint32_t settleMs;    // a request unanswered this long after the device came back counts
    uint32_t spacingMs;   // soft reconnects at most once per this
};

struct UsbSample {
    uint32_t now;         // millis()
    bool     mounted;     // tud_mounted(): configured by a host, suspended or not
    bool     ready;       // mounted and not suspended (the bridge's usbReady())
    uint32_t requests;    // status requests received so far
    uint32_t replies;     // status replies written whole so far
};

enum class UsbStep : uint8_t {
    None,
    Disconnect,     // take the device off the bus (tud_disconnect)
    Connect,        // put it back (tud_connect); the host enumerates it again
    Recovered,      // a reply got out whole after the reconnect
    Inconclusive,   // back, but nobody asked: watching again
    Restart,        // still wedged after the reconnect
};

class UsbWatch {
public:
    enum class Phase : uint8_t { Watching, Off, Verifying };

    explicit constexpr UsbWatch(const UsbConfig& c) : c_(c) {}

    UsbStep step(const UsbSample& s) {
        const uint32_t asked = primed_ ? s.requests - requests_ : 0;
        const uint32_t answered = primed_ ? s.replies - replies_ : 0;
        primed_ = true;
        requests_ = s.requests;
        replies_ = s.replies;
        switch (phase_) {
            case Phase::Watching:
                return watch(s, asked, answered);
            case Phase::Off:
                if (!s.ready) sawDown_ = true;
                if (s.now - phaseMs_ < c_.offMs) return UsbStep::None;
                phase_ = Phase::Verifying;
                phaseMs_ = s.now;
                remounted_ = false;
                askedAfter_ = 0;
                return UsbStep::Connect;
            case Phase::Verifying:
                if (answered > 0) {
                    phase_ = Phase::Watching;
                    clearEvidence();
                    failedInRow_ = 0;
                    return UsbStep::Recovered;
                }
                if (!s.ready) sawDown_ = true;
                else if (sawDown_) remounted_ = true;
                if (remounted_ && asked > 0) {
                    if (askedAfter_ == 0) firstAfterMs_ = s.now;
                    askedAfter_ += asked;
                }
                if (s.now - phaseMs_ < c_.verifyMs) return UsbStep::None;
                phase_ = Phase::Watching;
                clearEvidence();
                lastRemounted_ = remounted_;
                if (!remounted_) return UsbStep::Restart;
                if (askedAfter_ >= 2 && s.now - firstAfterMs_ >= c_.settleMs) return UsbStep::Restart;
                return UsbStep::Inconclusive;
        }
        return UsbStep::None;
    }

    Phase    phase() const { return phase_; }
    uint32_t asked() const { return asked_; }                    // the current evidence
    uint32_t askedSpanMs() const { return asked_ ? lastAskedMs_ - firstAskedMs_ : 0; }
    bool     askedWhileSuspended() const { return suspended_; }
    uint32_t reconnects() const { return reconnects_; }
    bool     lastRemounted() const { return lastRemounted_; }   // the last verify saw the device come back
    uint32_t askedAfter() const { return askedAfter_; }
    // The wait before the next soft reconnect may start, after the last.
    uint32_t spacingMs() const {
        const uint32_t doublings = failedInRow_ > 1 ? failedInRow_ - 1 : 0;
        return c_.spacingMs << (doublings < 5 ? doublings : 5);
    }

    void reset() {
        phase_ = Phase::Watching;
        primed_ = false;
        clearEvidence();
        reconnects_ = 0;
        failedInRow_ = 0;
        sawDown_ = remounted_ = lastRemounted_ = false;
        askedAfter_ = 0;
    }

private:
    UsbStep watch(const UsbSample& s, uint32_t asked, uint32_t answered) {
        if (answered > 0) failedInRow_ = 0;   // USB works: the next reconnect need not wait longer
        if (answered > 0 || !s.mounted) {     // a reply got out, or no host: nothing to hold against USB
            clearEvidence();
            return UsbStep::None;
        }
        if (asked == 0) return UsbStep::None;   // only a request that just arrived can tip it
        if (asked_ == 0 || s.now - lastAskedMs_ > c_.gapMs) {   // the first, or the monitor paused
            asked_ = 0;
            firstAskedMs_ = s.now;
            suspended_ = false;
        }
        asked_ += asked;
        lastAskedMs_ = s.now;
        if (!s.ready) suspended_ = true;
        if (asked_ < c_.minAsked || s.now - firstAskedMs_ < c_.windowMs) return UsbStep::None;
        if (reconnects_ > 0 && s.now - lastReconnectMs_ < spacingMs()) return UsbStep::None;
        ++reconnects_;
        ++failedInRow_;                      // until a reply gets out
        lastReconnectMs_ = s.now;
        phase_ = Phase::Off;
        phaseMs_ = s.now;
        sawDown_ = !s.ready;
        return UsbStep::Disconnect;
    }
    void clearEvidence() {
        asked_ = 0;
        suspended_ = false;
    }

    UsbConfig c_;
    Phase     phase_ = Phase::Watching;
    bool      primed_ = false;
    uint32_t  requests_ = 0, replies_ = 0;       // the counts at the last step
    uint32_t  asked_ = 0;                         // unanswered requests in the evidence
    uint32_t  firstAskedMs_ = 0, lastAskedMs_ = 0;
    bool      suspended_ = false;                 // some arrived while TinyUSB called the bus suspended
    uint32_t  reconnects_ = 0;
    uint32_t  failedInRow_ = 0;                   // reconnects since a reply last got out
    uint32_t  lastReconnectMs_ = 0;
    uint32_t  phaseMs_ = 0;                       // Off / Verifying since
    bool      sawDown_ = false;                   // not ready since Disconnect ...
    bool      remounted_ = false;                 // ... and ready again after that
    bool      lastRemounted_ = false;
    uint32_t  askedAfter_ = 0;                    // requests since it came back
    uint32_t  firstAfterMs_ = 0;
};

}  // namespace FlRecovery
