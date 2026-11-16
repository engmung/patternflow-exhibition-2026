#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — RTP-MIDI arrival timing from the panel, the part without hardware
//
// The panel stamps every RTP-MIDI packet with its media clock (AppleMIDI
// 3.5.0: rtpMidiClock, MIDI_SAMPLING_RATE_DEFAULT = 10 kHz, counted from its
// millis(), so 1 ms steps). The bridge notes when each packet reaches it on
// its own clock in the same units. Transit = arrival - timestamp is then the
// delay plus a constant clock offset, and its changes are what the Wi-Fi
// link and FlowLocal's own pass add:
//
//   jitterMs()   RFC 3550 interarrival jitter (6.4.1 / A.8):
//                D = transit_i - transit_i-1, J += (|D| - J) / 16
//   spreadMs()   the largest transit minus the smallest of the packets that
//                arrived in the last 50-60 s: how much later than the quickest
//                packet the slowest one arrived (messages held up and then
//                delivered in a bunch show here)
//
// Both are -1 until known: jitter needs two packets from one sender, the
// spread one packet in the window. A new sender (SSRC) or a transit jump of
// more than RESET_TICKS (a clock that started over) starts both again.
// Both clocks drift apart by the crystals' tolerance (tens of ppm), which the
// spread takes as a few ms per minute at most.
//
// Pure C++, so the host test compiles it as it is.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include <cstdint>

namespace FlRtpJitter {

constexpr uint32_t CLOCK_HZ     = 10000;               // AppleMIDI 3.5.0 MIDI_SAMPLING_RATE_DEFAULT
constexpr uint32_t TICKS_PER_MS = CLOCK_HZ / 1000;
constexpr int32_t  RESET_TICKS  = 10 * (int32_t)CLOCK_HZ;   // 10 s
constexpr uint32_t BUCKET_MS    = 10000;
constexpr uint32_t BUCKETS      = 6;                   // window: the current 10 s plus the 5 before

class Estimator {
public:
    // One packet: its sender, its RTP timestamp, when it arrived here (local
    // clock, CLOCK_HZ, wrapping like the timestamp) and now in ms (for the
    // window).
    void sample(uint32_t ssrc, uint32_t rtpTimestamp, uint32_t arrivalTicks, uint32_t nowMs) {
        const uint32_t transit = arrivalTicks - rtpTimestamp;
        if (!have_ || ssrc != ssrc_) {
            start(ssrc, transit);
        } else {
            const int32_t d = (int32_t)(transit - lastTransit_);
            if (d > RESET_TICKS || d < -RESET_TICKS) {
                start(ssrc, transit);
            } else {
                const uint32_t a = (uint32_t)(d < 0 ? -d : d);
                // J scaled by 16: J16 += |D| - J16 / 16, rounded (RFC 3550 A.8).
                jitter16_ += a - ((jitter16_ + 8) >> 4);
                ++samples_;
                lastTransit_ = transit;
            }
        }
        const int32_t rel = (int32_t)(transit - anchor_);
        if (rel > RESET_TICKS || rel < -RESET_TICKS) {   // drifted far from the anchor: new window
            start(ssrc, transit);
        }
        note((int32_t)(transit - anchor_), nowMs);
    }

    // Interarrival jitter in ms, rounded; -1 before two packets of one sender.
    int32_t jitterMs() const {
        if (!have_ || samples_ < 2) return -1;
        return (int32_t)((jitter16_ + 8 * TICKS_PER_MS) / (16 * TICKS_PER_MS));
    }

    // Largest minus smallest transit in the window, ms, rounded; -1 with no
    // packet in it.
    int32_t spreadMs(uint32_t nowMs) const {
        const uint32_t epoch = nowMs / BUCKET_MS;
        bool any = false;
        int32_t lo = 0, hi = 0;
        for (const Bucket& b : buckets_) {
            if (!b.used || epoch - b.epoch >= BUCKETS) continue;
            if (!any || b.lo < lo) lo = b.lo;
            if (!any || b.hi > hi) hi = b.hi;
            any = true;
        }
        if (!any) return -1;
        return (int32_t)(((uint32_t)(hi - lo) + TICKS_PER_MS / 2) / TICKS_PER_MS);
    }

    void reset() { *this = Estimator(); }
    uint32_t ssrc() const { return ssrc_; }

private:
    struct Bucket {
        bool     used = false;
        uint32_t epoch = 0;
        int32_t  lo = 0, hi = 0;   // transit relative to anchor_, ticks
    };

    void start(uint32_t ssrc, uint32_t transit) {
        have_ = true;
        ssrc_ = ssrc;
        anchor_ = transit;
        lastTransit_ = transit;
        jitter16_ = 0;
        samples_ = 1;
        for (Bucket& b : buckets_) b.used = false;
    }
    void note(int32_t rel, uint32_t nowMs) {
        const uint32_t epoch = nowMs / BUCKET_MS;
        Bucket& b = buckets_[epoch % BUCKETS];
        if (!b.used || b.epoch != epoch) {
            b.used = true;
            b.epoch = epoch;
            b.lo = b.hi = rel;
            return;
        }
        if (rel < b.lo) b.lo = rel;
        if (rel > b.hi) b.hi = rel;
    }

    bool     have_ = false;
    uint32_t ssrc_ = 0;
    uint32_t anchor_ = 0;
    uint32_t lastTransit_ = 0;
    uint32_t jitter16_ = 0;
    uint32_t samples_ = 0;
    Bucket   buckets_[BUCKETS];
};

}  // namespace FlRtpJitter
