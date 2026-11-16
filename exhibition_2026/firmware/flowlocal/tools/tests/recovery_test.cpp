// Host test for the exhibit's self-recovery decisions (src/fl_recovery.h):
// reset reason names, the boot record in RTC memory, loop()'s heartbeat
// watch and the USB watch. No stand-ins: the header is plain C++. Built and
// run by tools/check_exhibit_bridge.py; the bridge side (the task watchdog,
// tud_disconnect / tud_connect, the status keys) is in exhibit_bridge_test.cpp.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "src/fl_recovery.h"

using namespace FlRecovery;

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

#define CHECK_STR(have, want)                                                               \
    do {                                                                                    \
        ++g_checks;                                                                         \
        const std::string h_ = (have), w_ = (want);                                         \
        if (h_ != w_) {                                                                     \
            std::printf("  FAIL line %d: %s = [%s], want [%s]\n", __LINE__, #have,          \
                        h_.c_str(), w_.c_str());                                            \
            ++g_failures;                                                                   \
        }                                                                                   \
    } while (0)

static void section(const char* name) { std::printf("- %s\n", name); }

// The bridge's USB watch settings (fl_midi_bridge.h's defaults).
static UsbConfig config() {
    UsbConfig c{};
    c.windowMs = 8000;
    c.minAsked = 3;
    c.gapMs = 5000;
    c.offMs = 500;
    c.verifyMs = 10000;
    c.settleMs = 3000;
    c.spacingMs = 60000;
    return c;
}

static const char* stepName(UsbStep s) {
    switch (s) {
        case UsbStep::Disconnect:   return "Disconnect";
        case UsbStep::Connect:      return "Connect";
        case UsbStep::Recovered:    return "Recovered";
        case UsbStep::Inconclusive: return "Inconclusive";
        case UsbStep::Restart:      return "Restart";
        default:                    return "None";
    }
}

// The bridge task's view, one sample per millisecond, and a model of the
// laptop: its monitor asks every 2 s (if asking), each request is answered
// in the same pass (if answering and TinyUSB calls the bus ready). A
// Disconnect takes the device from the host at once; after a Connect the host
// enumerates it again 50 ms later (unless it ignores it), and the monitor,
// having reopened the port, asks 500 ms after that. Times in the log are ms
// since the first tick.
struct Sim {
    UsbWatch w{config()};
    uint32_t now;
    uint32_t start;
    bool mounted = true;
    bool ready = true;
    uint32_t requests = 0;
    uint32_t replies = 0;
    bool asking = false;         // the monitor sends a request every askEveryMs
    uint32_t askEveryMs = 2000;
    uint32_t nextAskMs = 0;
    bool answering = true;       // each request gets a reply written whole
    bool hostTakesBack = true;   // after Connect the host enumerates the device again
    bool fixedByReconnect = true;   // and after that the replies get out
    bool sleepyAfter = false;    // after Connect the monitor does not ask again
    uint32_t connectedAt = 0;
    bool reenumerating = false;
    std::vector<std::pair<uint32_t, UsbStep>> steps;   // (ms since the first tick, step)

    explicit Sim(uint32_t t0) : now(t0), start(t0 + 1) { w.step({now, mounted, ready, requests, replies}); }

    void startAsking(bool answered) {   // the first request at the next tick
        asking = true;
        answering = answered;
        nextAskMs = now + 1;
    }
    void tick() {
        ++now;
        if (reenumerating && now - connectedAt >= 50) {   // the host's bus reset and enumeration
            reenumerating = false;
            mounted = ready = true;
            if (fixedByReconnect) answering = true;
            if (sleepyAfter) asking = false;
            nextAskMs = now + 500;
        }
        if (asking && mounted && (int32_t)(now - nextAskMs) >= 0) {
            nextAskMs = now + askEveryMs;
            ++requests;
            if (answering && ready) ++replies;
        }
        const UsbStep s = w.step({now, mounted, ready, requests, replies});
        if (s == UsbStep::None) return;
        steps.push_back({now - start, s});
        if (s == UsbStep::Disconnect) {
            mounted = ready = false;   // the host lets go of the device at once
        } else if (s == UsbStep::Connect && hostTakesBack) {
            connectedAt = now;
            reenumerating = true;
        }
    }
    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; ++i) tick();
    }
    std::string log() const {
        std::string out;
        for (const auto& s : steps) {
            if (!out.empty()) out += ", ";
            out += std::string(stepName(s.second)) + "@" + std::to_string(s.first);
        }
        return out;
    }
};

int main() {
    std::printf("self-recovery host test\n");

    section("rsn=: every esp_reset_reason_t value has its name");
    {
        const char* want[] = {"unknown", "poweron", "ext", "sw", "panic", "int_wdt", "task_wdt", "wdt",
                              "deepsleep", "brownout", "sdio", "usb", "jtag", "efuse", "pwr_glitch",
                              "cpu_lockup"};
        for (int i = 0; i < 16; ++i) CHECK_STR(resetReasonName(i), want[i]);
        CHECK_EQ(RST_CPU_LOCKUP, 15);
        CHECK_STR(resetReasonName(-1), "unknown");
        CHECK_STR(resetReasonName(16), "unknown");
        CHECK_STR(resetReasonName(1000), "unknown");
        for (int i = -1; i < 17; ++i) {   // fit for the reply: no ';', '=' or space, at most 16
            const std::string n = resetReasonName(i);
            CHECK(!n.empty() && n.size() <= 16 && n.find_first_of("; =") == std::string::npos);
        }
    }

    section("why=: the notes, and which ones are automatic restarts");
    {
        CHECK_STR(noteName(Note::None), "");
        CHECK_STR(noteName(Note::Command), "cmd12");
        CHECK_STR(noteName(Note::Web), "web");
        CHECK_STR(noteName(Note::BridgeHung), "bridge_hung");
        CHECK_STR(noteName(Note::UsbWedged), "usb_wedged");
        CHECK_STR(noteName(Note::Channel), "cmd13");
        CHECK_STR(noteName((Note)77), "");
        CHECK_EQ(NOTE_COUNT, 6);
        for (uint32_t n = 1; n < NOTE_COUNT; ++n) {       // fit for the reply: no ';', '=' or space, at most 16
            const std::string s = noteName((Note)n);
            CHECK(!s.empty() && s.size() <= 16 && s.find_first_of("; =") == std::string::npos);
        }
        CHECK(!isRecovery(Note::None) && !isRecovery(Note::Command) && !isRecovery(Note::Web));
        CHECK(!isRecovery(Note::Channel));                // asked for by a person, like cmd12
        CHECK(isRecovery(Note::BridgeHung) && isRecovery(Note::UsbWedged));
        CHECK(recoveryAllowed(0, 3) && recoveryAllowed(2, 3));
        CHECK(!recoveryAllowed(3, 3) && !recoveryAllowed(4, 3) && !recoveryAllowed(0, 0));
    }

    section("boot record: power-on and garbage count from 1; a restart counts on");
    {
        BootRecord r;
        std::memset(&r, 0xA7, sizeof r);   // what RTC memory may hold after power-on
        BootInfo b = startBoot(r, false);
        CHECK_EQ(b.boots, 1);
        CHECK(!b.carried && b.note == Note::None && b.recoveries == 0);
        CHECK(valid(r));
        b = startBoot(r, false);           // a software restart
        CHECK_EQ(b.boots, 2);
        CHECK(b.carried);
        b = startBoot(r, false);
        CHECK_EQ(b.boots, 3);
        b = startBoot(r, true);            // power-on (or RST), whatever the memory holds
        CHECK_EQ(b.boots, 1);
        CHECK(!b.carried);
        std::memset(&r, 0, sizeof r);      // all zero is garbage too
        CHECK(!valid(r));
        CHECK_EQ(startBoot(r, false).boots, 1);
    }

    section("boot record: the note is reported by the next boot only; automatic restarts are counted");
    {
        BootRecord r;
        std::memset(&r, 0x3C, sizeof r);
        CHECK_EQ(startBoot(r, true).boots, 1);           // power-on
        CHECK(!notePending(r));
        noteRestart(r, Note::BridgeHung);
        CHECK(notePending(r));                            // the task watchdog's hook leaves it alone
        BootInfo b = startBoot(r, false);                 // rsn=sw
        CHECK(!notePending(r));
        CHECK_EQ(b.boots, 2);
        CHECK(b.note == Note::BridgeHung);
        CHECK_EQ(b.recoveries, 1);
        noteRestart(r, Note::Command);                    // not automatic: not counted
        b = startBoot(r, false);
        CHECK_EQ(b.boots, 3);
        CHECK(b.note == Note::Command);
        CHECK_EQ(b.recoveries, 1);
        noteRestart(r, Note::UsbWedged);
        b = startBoot(r, false);
        CHECK(b.note == Note::UsbWedged);
        CHECK_EQ(b.recoveries, 2);
        b = startBoot(r, false);                          // a crash: FlowLocal noted nothing
        CHECK_EQ(b.boots, 5);
        CHECK(b.note == Note::None);
        CHECK_EQ(b.recoveries, 2);
        noteRestart(r, Note::Channel);                    // command 13: not automatic, not counted
        b = startBoot(r, false);
        CHECK_EQ(b.boots, 6);
        CHECK(b.note == Note::Channel);
        CHECK_EQ(b.recoveries, 2);
        noteRestart(r, Note::Web);
        b = startBoot(r, true);                           // power-on forgets everything
        CHECK_EQ(b.boots, 1);
        CHECK(b.note == Note::None && b.recoveries == 0);
    }

    section("boot record: any changed bit, or a note it does not know, is garbage");
    {
        BootRecord r;
        std::memset(&r, 0, sizeof r);
        startBoot(r, true);
        noteRestart(r, Note::UsbWedged);
        startBoot(r, false);
        noteRestart(r, Note::BridgeHung);
        const BootRecord good = r;
        CHECK(valid(good));
        int caught = 0;
        for (size_t byte = 0; byte < sizeof r; ++byte) {
            for (int bit = 0; bit < 8; ++bit) {
                BootRecord bad = good;
                reinterpret_cast<uint8_t*>(&bad)[byte] ^= (uint8_t)(1u << bit);
                caught += !valid(bad);
            }
        }
        CHECK_EQ(caught, (int)(sizeof r * 8));
        BootRecord odd = good;
        odd.note = NOTE_COUNT;                            // well formed, unknown note
        odd.check = checkOf(odd);
        CHECK(!valid(odd));
        CHECK_EQ(startBoot(odd, false).boots, 1);
        BootRecord zeroBoots = good;
        zeroBoots.boots = 0;
        zeroBoots.check = checkOf(zeroBoots);
        CHECK(!valid(zeroBoots));
        BootRecord worn;                                   // noteRestart on garbage starts clean
        std::memset(&worn, 0x55, sizeof worn);
        noteRestart(worn, Note::BridgeHung);
        const BootInfo b = startBoot(worn, false);
        CHECK_EQ(b.boots, 2);
        CHECK(b.note == Note::BridgeHung);
        CHECK_EQ(b.recoveries, 1);
    }

    section("boot record: counts stop at their top instead of wrapping");
    {
        BootRecord r;
        std::memset(&r, 0, sizeof r);
        startBoot(r, true);
        r.boots = UINT32_MAX;
        r.recoveries = UINT32_MAX;
        r.check = checkOf(r);
        noteRestart(r, Note::BridgeHung);
        const BootInfo b = startBoot(r, false);
        CHECK(b.boots == UINT32_MAX && b.recoveries == UINT32_MAX);
    }

    section("heartbeat: the first look only starts watching; 3 s without a change is a stall");
    {
        HeartbeatWatch h(3000);
        CHECK(!h.stalled(1000, 7));
        CHECK(!h.stalled(3999, 7));
        CHECK(h.stalled(4000, 7));
        CHECK_EQ(h.quietMs(4000), 3000);
        CHECK(!h.stalled(4500, 8));                        // it passes again
        CHECK(!h.stalled(7499, 8));
        CHECK(h.stalled(7500, 8));
        h.reset();
        CHECK(!h.stalled(100000, 8));                      // after reset, the same value starts a new watch
        CHECK(!h.stalled(102999, 8));
        CHECK(h.stalled(103000, 8));
    }

    section("heartbeat: a task passing every 1 ms, looked at every 500 ms, never stalls; one that stops does within 3.5 s");
    {
        HeartbeatWatch h(3000);
        uint32_t beat = 0;
        bool anyStall = false;
        uint32_t t = 12345;
        for (uint32_t ms = 0; ms < 600000; ++ms) {        // 10 minutes
            ++t;
            ++beat;
            if (ms % 500 == 0) anyStall = anyStall || h.stalled(t, beat);
        }
        CHECK(!anyStall);
        const uint32_t stoppedAt = t;                     // no pass after this
        uint32_t seenAt = 0;
        for (uint32_t ms = 1; ms <= 10000 && !seenAt; ++ms) {
            ++t;
            if (ms % 500 == 0 && h.stalled(t, beat)) seenAt = t;
        }
        CHECK(seenAt != 0);
        CHECK(seenAt - stoppedAt >= 3000 && seenAt - stoppedAt <= 3500);
    }

    section("heartbeat: across millis() and count wraps");
    {
        HeartbeatWatch h(3000);
        CHECK(!h.stalled(0xFFFFF000u, 0xFFFFFFFFu));
        CHECK(!h.stalled(0xFFFFFE00u, 0u));               // the count wraps: still a change
        CHECK(!h.stalled(0x00000800u, 0u));               // 2560 ms later, across millis()' wrap
        CHECK(h.stalled(0x00000A00u, 0u));                // 3072 ms
    }

    section("usb: a healthy laptop (every request answered) never triggers, over an hour");
    {
        Sim s(1000);
        s.startAsking(true);
        s.run(3600000);
        CHECK_STR(s.log(), "");
        CHECK_EQ(s.requests, 1800);
        CHECK_EQ(s.w.reconnects(), 0);
    }

    section("usb: nobody asking never triggers: awake, suspended, unplugged, for an hour each");
    {
        Sim s(1000);
        s.run(3600000);
        s.ready = false;                                   // laptop asleep (suspended)
        s.run(3600000);
        s.mounted = false;                                 // unplugged / Live closed makes no difference
        s.run(3600000);
        CHECK_STR(s.log(), "");
    }

    section("usb: requests unanswered (IN stopped): off the bus after 8 s, back 500 ms later, a reply: recovered");
    {
        Sim s(1000);
        s.startAsking(false);
        s.run(20000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Recovered@9050");
        CHECK_EQ(s.w.reconnects(), 1);
        CHECK(s.w.phase() == UsbWatch::Phase::Watching);
    }

    section("usb: the evidence at the disconnect (for the log)");
    {
        Sim s(1000);
        s.startAsking(false);
        s.run(8000);                                       // requests at 0, 2, 4 and 6 s
        CHECK_STR(s.log(), "");
        CHECK_EQ(s.w.asked(), 4);
        s.tick();                                          // the 5th request, 8 s after the first
        CHECK_STR(s.log(), "Disconnect@8000");
        CHECK_EQ(s.w.asked(), 5);
        CHECK_EQ(s.w.askedSpanMs(), 8000);
        CHECK(!s.w.askedWhileSuspended());
    }

    section("usb: a suspend TinyUSB never saw end (requests while not ready): the same, noted");
    {
        Sim s(1000);
        s.ready = false;                                   // tud_suspended() stuck
        s.startAsking(false);
        s.run(8001);
        CHECK_STR(s.log(), "Disconnect@8000");
        CHECK(s.w.askedWhileSuspended());
        s.run(12000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Recovered@9050");
    }

    section("usb: still no reply after the reconnect: restart 10 s after it");
    {
        Sim s(1000);
        s.fixedByReconnect = false;
        s.startAsking(false);
        s.run(20000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Restart@18500");
        CHECK(s.w.lastRemounted());
        CHECK(s.w.askedAfter() >= 2);
    }

    section("usb: the host never takes the device back: restart");
    {
        Sim s(1000);
        s.hostTakesBack = false;
        s.startAsking(false);
        s.run(20000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Restart@18500");
        CHECK(!s.w.lastRemounted());
    }

    section("usb: TinyUSB never notices the reconnect either (ready throughout): restart");
    {
        UsbWatch w(config());
        uint32_t t = 5000, req = 0;
        w.step({t, true, true, req, 0});
        std::string log;
        for (uint32_t ms = 1; ms <= 21000; ++ms) {
            ++t;
            if (ms % 2000 == 0) ++req;
            const UsbStep st = w.step({t, true, true, req, 0});   // nothing ever changes
            if (st != UsbStep::None) log += std::string(log.empty() ? "" : ", ") + stepName(st) + "@" + std::to_string(ms);
        }
        CHECK_STR(log, "Disconnect@10000, Connect@10500, Restart@20500");
    }

    section("usb: back, but nobody asks since: inconclusive, no restart");
    {
        Sim s(1000);
        s.sleepyAfter = true;                              // the monitor was closed meanwhile
        s.fixedByReconnect = false;
        s.startAsking(false);
        s.run(30000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Inconclusive@18500");
    }

    section("usb: back, one request late in the window only: inconclusive (it may still be answered)");
    {
        UsbWatch w(config());
        uint32_t t = 0, req = 0;
        w.step({t, true, true, req, 0});
        for (int i = 0; i < 5; ++i) {                      // 0, 2, 4, 6, 8 s unanswered
            t += (i == 0) ? 0 : 2000;
            ++req;
            const UsbStep st = w.step({t, true, true, req, 0});
            if (i == 4) CHECK(st == UsbStep::Disconnect);
        }
        t += 500;
        CHECK(w.step({t, false, false, req, 0}) == UsbStep::Connect);
        t += 100;
        CHECK(w.step({t, true, true, req, 0}) == UsbStep::None);    // back
        t += 9000;
        ++req;                                             // 9.1 s after Connect
        CHECK(w.step({t, true, true, req, 0}) == UsbStep::None);
        t += 900;
        CHECK(w.step({t, true, true, req, 0}) == UsbStep::Inconclusive);
    }

    section("usb: after an inconclusive reconnect, the next one waits 60 s from the last");
    {
        Sim s(1000);
        s.sleepyAfter = true;
        s.fixedByReconnect = false;
        s.startAsking(false);
        s.run(20000);                                      // Disconnect@8000 ... Inconclusive@18500
        s.sleepyAfter = false;
        s.startAsking(false);                              // the monitor asks again, still no reply
        s.run(80000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Inconclusive@18500, Disconnect@68000, "
                           "Connect@68500, Restart@78500");
        CHECK_EQ(s.w.reconnects(), 2);
    }

    section("usb: a wedge nothing mends: reconnects 1, 2, 4, 8, 16 min apart, never more often (an hour)");
    {
        Sim s(1000);
        s.fixedByReconnect = false;                        // the caller's restarts are used up too
        s.startAsking(false);
        s.run(3600000);
        std::vector<uint32_t> d;
        for (const auto& st : s.steps)
            if (st.second == UsbStep::Disconnect) d.push_back(st.first);
        CHECK_EQ(d.size(), 6);
        const uint32_t least[] = {60000, 120000, 240000, 480000, 960000};
        for (size_t i = 1; i < d.size() && i <= 5; ++i) {
            const uint32_t gap = d[i] - d[i - 1];
            if (gap < least[i - 1] || gap > least[i - 1] + 3000)
                std::printf("  FAIL reconnect %u came %u ms after the one before (want %u..%u)\n", (unsigned)i + 1,
                            (unsigned)gap, (unsigned)least[i - 1], (unsigned)least[i - 1] + 3000);
            CHECK(gap >= least[i - 1] && gap <= least[i - 1] + 3000);
        }
        CHECK_EQ(s.w.spacingMs(), 1920000);                // 32 min at most
        CHECK_EQ(s.w.reconnects(), 6);
    }

    section("usb: a reply that gets out starts the back-off over");
    {
        Sim s(1000);
        s.fixedByReconnect = false;
        s.startAsking(false);
        s.run(20000);                                      // one reconnect, no reply after it
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Restart@18500");
        s.run(60000);                                      // the second, still none
        CHECK_EQ(s.w.reconnects(), 2);
        CHECK_EQ(s.w.spacingMs(), 120000);
        s.answering = true;                                // the laptop reads again
        s.run(3000);
        CHECK_EQ(s.w.spacingMs(), 60000);
        CHECK_EQ(s.w.asked(), 0);
    }

    section("usb: fewer than 3 requests, or gaps over 5 s: never");
    {
        Sim a(1000);
        a.askEveryMs = 6000;                               // the monitor at a 6 s pace: every gap starts over
        a.startAsking(false);
        a.run(600000);
        CHECK_STR(a.log(), "");
        Sim b(1000);
        b.startAsking(false);
        b.run(2500);                                       // two requests (0 s and 2 s), then silence
        b.asking = false;
        b.run(600000);
        CHECK_STR(b.log(), "");
        Sim c(1000);
        c.askEveryMs = 4500;                               // three in 9 s, gaps of 4.5 s: yes
        c.startAsking(false);
        c.run(9500);
        CHECK_STR(c.log(), "Disconnect@9000");
    }

    section("usb: evidence from before a pause (laptop asleep) does not count after it");
    {
        Sim s(1000);
        s.startAsking(false);
        s.run(5000);                                       // 0, 2, 4 s unanswered
        s.asking = false;
        s.ready = false;                                   // asleep for 20 min
        s.run(1200000);
        CHECK_STR(s.log(), "");                            // time alone never tips it
        s.ready = true;
        s.startAsking(false);                              // wakes; still unanswered
        s.run(7999);
        CHECK_STR(s.log(), "");                            // not at once: a new window
        s.run(2);
        CHECK_EQ(s.steps.size(), 1);
        CHECK(!s.steps.empty() && s.steps[0].second == UsbStep::Disconnect &&
              s.steps[0].first == 5000 + 1200000 + 8000);
    }

    section("usb: one reply clears the evidence; unmounted clears it too");
    {
        UsbWatch w(config());
        uint32_t t = 100, req = 0, rep = 0;
        w.step({t, true, true, req, rep});
        for (int i = 0; i < 4; ++i) {                      // 0..6 s unanswered
            ++req;
            CHECK(w.step({t, true, true, req, rep}) == UsbStep::None);
            t += 2000;
        }
        ++rep;                                             // a reply got out at 8 s
        CHECK(w.step({t, true, true, req, rep}) == UsbStep::None);
        CHECK_EQ(w.asked(), 0);
        for (int i = 0; i < 4; ++i) {
            ++req;
            CHECK(w.step({t, true, true, req, rep}) == UsbStep::None);
            t += 2000;
        }
        CHECK_EQ(w.asked(), 4);
        CHECK(w.step({t, false, false, req, rep}) == UsbStep::None);   // cable pulled
        CHECK_EQ(w.asked(), 0);
    }

    section("usb: a request and its reply in the same pass count as answered");
    {
        Sim s(1000);
        s.startAsking(true);
        s.run(60000);
        CHECK_EQ(s.w.asked(), 0);
        CHECK_STR(s.log(), "");
    }

    section("usb: the whole chain across millis()' wrap, and across the counters' wrap");
    {
        Sim s(0xFFFFF000u);                                // millis() wraps 4.1 s into the evidence
        s.requests = 0xFFFFFFFEu;
        s.replies = 0xFFFFFFF0u;
        s.w.reset();
        s.w.step({s.now, s.mounted, s.ready, s.requests, s.replies});
        s.fixedByReconnect = false;
        s.startAsking(false);
        s.run(20000);
        CHECK_STR(s.log(), "Disconnect@8000, Connect@8500, Restart@18500");
        Sim r(0xFFFFF000u);
        r.startAsking(false);
        r.run(20000);
        CHECK_STR(r.log(), "Disconnect@8000, Connect@8500, Recovered@9050");
    }

    section("usb: reset() forgets everything");
    {
        Sim s(1000);
        s.startAsking(false);
        s.run(8001);
        CHECK(s.w.phase() == UsbWatch::Phase::Off);
        s.w.reset();
        CHECK(s.w.phase() == UsbWatch::Phase::Watching);
        CHECK_EQ(s.w.reconnects(), 0);
        CHECK_EQ(s.w.asked(), 0);
    }

    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
