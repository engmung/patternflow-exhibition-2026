// Host test for the two pure pieces of the exhibit's Wi-Fi link reporting:
// src/fl_rtp_jitter.h (jt= / jm= from RTP timestamps against arrival) and
// src/fl_ap_channel.h (the boot scan's choice of 1 / 6 / 11). No stand-ins:
// both headers are plain C++. Built and run by tools/check_exhibit_bridge.py.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "src/fl_ap_channel.h"
#include "src/fl_rtp_jitter.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);       \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

#define CHECK_EQ(have, want)                                                                \
    do {                                                                                    \
        ++g_checks;                                                                         \
        const long long h_ = (long long)(have), w_ = (long long)(want);                     \
        if (h_ != w_) {                                                                     \
            std::printf("  FAIL line %d: %s = %lld, want %lld\n", __LINE__, #have, h_, w_); \
            ++g_failures;                                                                   \
        }                                                                                   \
    } while (0)

static void section(const char* name) { std::printf("- %s\n", name); }

using FlRtpJitter::Estimator;
constexpr uint32_t T = FlRtpJitter::TICKS_PER_MS;   // 10 ticks per ms

// A panel that sends a packet every `everyMs` and whose packets take
// baseMs + delayMs(i) to arrive. Clocks: sender starts at ts0, receiver at rx0.
struct Link {
    Estimator e;
    uint32_t ssrc = 0x1234;
    uint32_t ts0 = 1000;
    uint32_t rx0 = 777777;
    uint32_t sentMs = 0;      // sender time of the next packet, ms since start
    uint32_t nowMs = 5000;    // receiver millis() at start
    void send(uint32_t delayMs) {
        const uint32_t ts = ts0 + sentMs * T;
        const uint32_t arrival = rx0 + (sentMs + 3 + delayMs) * T;
        e.sample(ssrc, ts, arrival, nowMs + sentMs + 3 + delayMs);
    }
    void advance(uint32_t ms) { sentMs += ms; }
};

int main() {
    std::printf("link quality host test (RTP jitter, AP channel)\n");

    section("jitter: unknown until two packets, spread until one");
    {
        Estimator e;
        CHECK_EQ(e.jitterMs(), -1);
        CHECK_EQ(e.spreadMs(0), -1);
        e.sample(1, 100, 5000, 1000);
        CHECK_EQ(e.jitterMs(), -1);
        CHECK_EQ(e.spreadMs(1000), 0);
        e.sample(1, 200, 5100, 1010);
        CHECK_EQ(e.jitterMs(), 0);
        CHECK_EQ(e.spreadMs(1010), 0);
    }

    section("jitter: a steady link reads 0 whatever the clock offset");
    {
        Link l;
        for (int i = 0; i < 500; ++i) { l.send(0); l.advance(20); }
        CHECK_EQ(l.e.jitterMs(), 0);
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 0);
    }

    section("jitter: RFC 3550 steps (J += (|D| - J) / 16, in ticks scaled by 16)");
    {
        Estimator e;
        e.sample(7, 0, 0, 0);
        e.sample(7, 100, 100 + 160, 10);    // D = 160 ticks (16 ms): J = 10 ticks = 1 ms
        CHECK_EQ(e.jitterMs(), 1);
        e.sample(7, 200, 200, 20);          // D = -160: J = 10 + (160 - 10) / 16 = 19.4 ticks
        CHECK_EQ(e.jitterMs(), 2);
        // Converges on |D| when it stays the same: alternating 0 / +5 ms.
        Link l;
        for (int i = 0; i < 400; ++i) { l.send(i % 2 ? 5 : 0); l.advance(10); }
        CHECK_EQ(l.e.jitterMs(), 5);
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 5);
    }

    section("spread: messages held up and delivered in a bunch");
    {
        // Every 20 ms for a second; the Wi-Fi holds them all and delivers them
        // together at the end (the "one by one" feeling): the first waited 1 s.
        Link l;
        for (int i = 0; i < 20; ++i) { l.send(0); l.advance(20); }
        const uint32_t held = l.sentMs;
        for (int i = 0; i < 50; ++i) {
            const uint32_t waited = held + 1000 - l.sentMs;   // all arrive at held + 1000
            l.send(waited);
            l.advance(20);
        }
        const int32_t jtBunch = l.e.jitterMs();
        for (int i = 0; i < 20; ++i) { l.send(0); l.advance(20); }
        const int32_t spread = l.e.spreadMs(l.nowMs + l.sentMs);
        std::printf("    bunched second: jt=%d ms right after it, %d ms 0.4 s later, jm=%d ms\n",
                    (int)jtBunch, (int)l.e.jitterMs(), (int)spread);
        CHECK_EQ(spread, 1000);
        CHECK(jtBunch >= 15);
        CHECK(l.e.jitterMs() < jtBunch);
    }

    section("spread: a late packet leaves the window after 50-60 s; nothing in it: -1");
    {
        Link l;
        l.send(300);                     // 300 ms late, at the start
        l.advance(1000);
        for (int i = 0; i < 44; ++i) { l.send(0); l.advance(1000); }
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 300);    // 45 s on: still in the window
        for (int i = 0; i < 20; ++i) { l.send(0); l.advance(1000); }
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 0);      // 65 s on: gone
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs + 61000), -1);   // a quiet minute
        CHECK(l.e.jitterMs() >= 0);                         // jitter keeps its last value
    }

    section("wrap: RTP timestamp and arrival clock across 2^32, millis() across 2^32");
    {
        Link l;
        l.ts0 = 0xFFFFFF00u;
        l.rx0 = 0xFFFFF000u;
        l.nowMs = 0xFFFFF000u;
        for (int i = 0; i < 400; ++i) { l.send(i % 2 ? 4 : 0); l.advance(20); }
        CHECK_EQ(l.e.jitterMs(), 4);
        const int32_t s = l.e.spreadMs(l.nowMs + l.sentMs);
        CHECK(s == 4 || s == -1 || s == 0);   // the window may start over once at millis()' wrap
        l.nowMs = 1000;                         // then it fills again
        for (int i = 0; i < 20; ++i) { l.send(i % 2 ? 4 : 0); l.advance(10); }
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 4);
    }

    section("a new sender, or a clock that jumped, starts over");
    {
        Link l;
        for (int i = 0; i < 100; ++i) { l.send(i % 2 ? 30 : 0); l.advance(10); }
        CHECK(l.e.jitterMs() > 10);
        l.ssrc = 0x9999;                        // the panel rebooted: new SSRC, new clock
        l.ts0 = 5;
        l.send(0);
        CHECK_EQ(l.e.jitterMs(), -1);
        CHECK_EQ(l.e.ssrc(), 0x9999);
        l.advance(10);
        l.send(0);
        CHECK_EQ(l.e.jitterMs(), 0);
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 0);
        l.ts0 -= 20 * FlRtpJitter::CLOCK_HZ;    // same SSRC, timestamps 20 s off
        l.advance(10);
        l.send(0);
        CHECK_EQ(l.e.jitterMs(), -1);
        CHECK_EQ(l.e.spreadMs(l.nowMs + l.sentMs), 0);
    }

    // ── AP channel ─────────────────────────────────────────────────────────
    using namespace FlApChannel;

    section("channel: weights and overlap");
    {
        CHECK_EQ(weight(-100), 0);
        CHECK_EQ(weight(-120), 0);
        CHECK_EQ(weight(-99), 1);
        CHECK_EQ(weight(-70), 30);
        CHECK_EQ(weight(-30), 70);
        CHECK_EQ(weight(10), 70);
        CHECK_EQ(overlapPercent(0), 100);
        CHECK_EQ(overlapPercent(-1), 75);
        CHECK_EQ(overlapPercent(2), 50);
        CHECK_EQ(overlapPercent(-3), 25);
        CHECK_EQ(overlapPercent(4), 10);
        CHECK_EQ(overlapPercent(5), 0);
        CHECK_EQ(overlapPercent(-10), 0);
    }

    section("channel: nothing heard keeps the preferred channel");
    {
        Scores s;
        CHECK_EQ(choose(s, 6), 6);
        CHECK_EQ(choose(s, 11), 11);
        CHECK_EQ(choose(s, 3), 1);   // not a candidate: the lowest of a tie
    }

    section("channel: the measured home scan (2026-09-25) picks 1");
    {
        // Home router and its mesh on 9 (~60 % in Windows, about -70 dBm), a
        // neighbour on 8 (35 %, about -83 dBm). FlowLocal's own AP is off
        // during the scan.
        Scores s;
        add(s, {9, 0, -70});
        add(s, {9, 0, -70});
        add(s, {8, 0, -83});
        std::printf("    scores ch1 %u, ch6 %u, ch11 %u\n", (unsigned)s.score[0], (unsigned)s.score[1],
                    (unsigned)s.score[2]);
        CHECK_EQ(s.networks, 3);
        CHECK_EQ(s.score[0], 0);
        CHECK_EQ(s.score[1], 30 * 25 * 2 + 17 * 50);    // 2350
        CHECK_EQ(s.score[2], 30 * 50 * 2 + 17 * 25);    // 3425
        CHECK_EQ(choose(s, 6), 1);
    }

    section("channel: a crowded 1 and 6, a quiet 11");
    {
        Scores s;
        for (int i = 0; i < 5; ++i) add(s, {1, 0, -60});
        for (int i = 0; i < 3; ++i) add(s, {6, 0, -55});
        add(s, {11, 0, -90});
        CHECK_EQ(choose(s, 6), 11);
    }

    section("channel: one loud network next door outweighs several far away");
    {
        Scores s;
        add(s, {11, 0, -35});                              // 11: 65 x 100 = 6500
        for (int i = 0; i < 4; ++i) add(s, {1, 0, -88});   // 1: 4 x 12 x 100 = 4800
        for (int i = 0; i < 3; ++i) add(s, {6, 0, -60});   // 6: 3 x 40 x 100 = 12000
        CHECK_EQ(s.score[0], 4800);
        CHECK_EQ(s.score[1], 12000);
        CHECK_EQ(s.score[2], 6500);
        CHECK_EQ(choose(s, 11), 1);
    }

    section("channel: an HT40 network's secondary counts at half weight");
    {
        Scores s;
        add(s, {1, 1, -60});       // primary 1, secondary 5 (above)
        // 5 is 4 channels from 1 (10 %) and 1 from 6 (75 %).
        CHECK_EQ(s.score[0], 40 * 100 + (40 * 10) / 2);
        CHECK_EQ(s.score[1], 40 * 0 + (40 * 75) / 2);
        CHECK_EQ(s.score[2], 0);
        Scores b;
        add(b, {11, 2, -60});      // primary 11, secondary 7 (below)
        CHECK_EQ(b.score[1], 40 * 0 + (40 * 75) / 2);
        Scores edge;
        add(edge, {13, 1, -60});   // secondary 17 does not exist: primary only
        CHECK_EQ(edge.score[2], 40 * 50);
        CHECK_EQ(edge.networks, 1);
    }

    section("channel: ties go to the preferred channel, then the lower one; bad channels are ignored");
    {
        Scores s;
        add(s, {2, 0, -70});        // 1: 75 %, 6: 10 %, 11: 0
        add(s, {10, 0, -70});       // 1: 0, 6: 10 %, 11: 75 %
        CHECK_EQ(s.score[0], s.score[2]);
        CHECK(s.score[1] < s.score[0]);
        CHECK_EQ(choose(s, 1), 6);
        Scores t;
        add(t, {6, 0, -70});
        CHECK_EQ(t.score[0], t.score[2]);
        CHECK_EQ(choose(t, 11), 11);
        CHECK_EQ(choose(t, 6), 1);
        Scores bad;
        add(bad, {0, 0, -40});
        add(bad, {15, 0, -40});
        add(bad, {200, 0, -40});
        CHECK_EQ(bad.networks, 0);
        CHECK_EQ(choose(bad, 6), 6);
    }

    section("channel plan: a build's fixed channel wins, then the PF monitor's choice (1..13), else auto");
    {
        CHECK(modeFor(0, 0) == Mode::Auto);
        CHECK(modeFor(0, 6) == Mode::Manual);
        CHECK(modeFor(0, 13) == Mode::Manual);
        CHECK(modeFor(0, 14) == Mode::Auto);        // no channel: automatic
        CHECK(modeFor(0, 255) == Mode::Auto);
        CHECK(modeFor(11, 0) == Mode::Fixed);
        CHECK(modeFor(11, 6) == Mode::Fixed);       // the build wins over NVS
        CHECK(std::string(modeName(Mode::Auto)) == "auto");
        CHECK(std::string(modeName(Mode::Fixed)) == "fixed");
        CHECK(std::string(modeName(Mode::Manual)) == "manual");
        CHECK_EQ((int)Mode::Fixed, 0);              // the PF monitor's chm= numbers
        CHECK_EQ((int)Mode::Auto, 1);
        CHECK_EQ((int)Mode::Manual, 2);
        CHECK(!validManual(0) && validManual(1) && validManual(13) && !validManual(14));
    }

    section("command 13's next: round 1 -> 6 -> 11 -> 1, a channel between them to the next one up");
    {
        CHECK_EQ(nextCandidate(1), 6);
        CHECK_EQ(nextCandidate(6), 11);
        CHECK_EQ(nextCandidate(11), 1);
        CHECK_EQ(nextCandidate(2), 6);
        CHECK_EQ(nextCandidate(5), 6);
        CHECK_EQ(nextCandidate(7), 11);
        CHECK_EQ(nextCandidate(10), 11);
        CHECK_EQ(nextCandidate(12), 1);
        CHECK_EQ(nextCandidate(13), 1);
        CHECK_EQ(nextCandidate(0), 1);
        // Three presses from any candidate come back to it.
        for (int c : {1, 6, 11}) CHECK_EQ(nextCandidate(nextCandidate(nextCandidate(c))), c);
    }

    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
