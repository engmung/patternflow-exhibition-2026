// Host test for the PF monitor protocol's pure part (src/fl_pf_monitor.h):
// the SysEx assembler (status request and commands, command 13's argument),
// the command gate, the reply and ack text (the restart keys rsn / bc / why /
// ur, then cs, last) and their USB-MIDI packets. No stand-ins: the header is
// plain C++. Built and run by tools/check_exhibit_bridge.py; the bridge side
// (when the task answers, what a command does) is in exhibit_bridge_test.cpp.

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "src/fl_pf_monitor.h"

using namespace FlPfMonitor;

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
            std::printf("  FAIL line %d: %s\n      [%s]\n    want\n      [%s]\n", __LINE__, \
                        #have, h_.c_str(), w_.c_str());                                     \
            ++g_failures;                                                                   \
        }                                                                                   \
    } while (0)

static void section(const char* name) { std::printf("- %s\n", name); }

struct Pkt { uint8_t h, a, b, c; };

// Totals over a run of packets.
struct Totals {
    int requests = 0;
    int commands = 0;
    uint8_t command = 0;   // the last one
    uint8_t arg = 0;       // and its argument
    int discarded = 0;
    int sysex = 0;      // packets the assembler took
    int notSysex = 0;   // packets left to the caller
};
static Totals run(SysexRx& rx, std::initializer_list<Pkt> packets) {
    Totals t;
    for (const Pkt& p : packets) {
        const RxResult r = rx.feed(p.h, p.a, p.b, p.c);
        t.requests += r.statusRequest ? 1 : 0;
        if (r.command) {
            ++t.commands;
            t.command = r.command;
            t.arg = r.arg;
        }
        t.discarded += r.discarded;
        (r.sysex ? t.sysex : t.notSysex) += 1;
    }
    return t;
}

// The reply bytes back out of its packets, checking every packet's CIN and
// padding as USB-MIDI 1.0 cuts SysEx.
static bool unpack(const uint8_t* msg, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    const size_t count = packetCount(len);
    uint8_t p[4];
    for (size_t i = 0; i < count; ++i) {
        if (!replyPacket(msg, len, i, p)) return false;
        const bool last = (i + 1 == count);
        const size_t n = last ? (size_t)(p[0] - 0x4) : 3;
        if (!last && p[0] != 0x4) return false;
        if (last && (p[0] < 0x5 || p[0] > 0x7)) return false;
        for (size_t k = 0; k < 3; ++k) {
            if (k < n) out.push_back(p[1 + k]);
            else if (p[1 + k] != 0) return false;
        }
    }
    return !replyPacket(msg, len, count, p);
}

int main() {
    std::printf("PF monitor protocol host test\n");

    section("the status request, as USB-MIDI packs it (CIN 0x4 + CIN 0x7)");
    {
        SysexRx rx;
        const Totals t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.requests, 1);
        CHECK_EQ(t.discarded, 0);
        CHECK_EQ(t.sysex, 2);
        CHECK(!rx.active());
    }

    section("back to back, cable number ignored, real-time bytes inside do not cut it");
    {
        SysexRx rx;
        const Totals t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0xF7},
                                  {0x14, 0xF0, 0x7D, 0x50}, {0x0F, 0xF8, 0, 0}, {0x0F, 0xFE, 0, 0},
                                  {0x17, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.requests, 2);
        CHECK_EQ(t.discarded, 0);
        CHECK_EQ(t.notSysex, 2);
    }

    section("other SysEx: counted packet by packet, never a request");
    {
        SysexRx rx;
        CHECK_EQ(run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x02, 0xF7}}).discarded, 2);   // reply shape
        CHECK_EQ(run(rx, {{0x04, 0xF0, 0x7E, 0x7F}, {0x07, 0x06, 0x01, 0xF7}}).discarded, 2);   // identity request
        CHECK_EQ(run(rx, {{0x04, 0xF0, 0x7D, 0x51}, {0x07, 0x46, 0x01, 0xF7}}).discarded, 2);   // wrong signature
        CHECK_EQ(run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x06, 0x46, 0xF7, 0x00}}).discarded, 2);   // too short
        CHECK_EQ(run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x01, 0x00}, {0x05, 0xF7, 0, 0}}).discarded, 3);
        CHECK_EQ(run(rx, {{0x06, 0xF0, 0xF7, 0x00}}).discarded, 1);                             // empty
        const Totals longOne = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x01, 1}, {0x04, 2, 3, 4},
                                        {0x04, 5, 6, 7}, {0x04, 8, 9, 10}, {0x07, 11, 12, 0xF7}});
        CHECK_EQ(longOne.discarded, 6);
        CHECK_EQ(longOne.requests, 0);
        CHECK(!rx.active());
    }

    section("cut short: by a status byte, by a new F0, by an ending packet without F7");
    {
        SysexRx rx;
        Totals t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x0B, 0xBF, 0x77, 0x7F}});   // the resync CC ends it
        CHECK_EQ(t.discarded, 1);
        CHECK_EQ(t.notSysex, 1);
        CHECK(!rx.active());
        t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.discarded, 1);   // the first one; the second is a request
        CHECK_EQ(t.requests, 1);
        t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0x02}});   // CIN 0x7 without F7
        CHECK_EQ(t.discarded, 2);
        CHECK_EQ(t.requests, 0);
        CHECK(!rx.active());
        t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x05, 0xF6, 0, 0}});          // tune request (CIN 0x5, not F7)
        CHECK_EQ(t.discarded, 1);
        CHECK_EQ(t.notSysex, 1);
        t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x90, 0x01}, {0x05, 0xF7, 0, 0}});   // status byte inside
        CHECK_EQ(t.discarded, 3);
        CHECK_EQ(t.requests, 0);
    }

    section("strays: an end without a start, data without F0");
    {
        SysexRx rx;
        CHECK_EQ(run(rx, {{0x05, 0xF7, 0, 0}}).discarded, 1);
        Totals t = run(rx, {{0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.discarded, 1);
        CHECK_EQ(t.requests, 0);
        t = run(rx, {{0x04, 0x7D, 0x50, 0x46}, {0x06, 0x01, 0xF7, 0}});   // a request without its F0
        CHECK_EQ(t.discarded, 2);
        CHECK_EQ(t.requests, 0);
        // A request right after a stray: still recognised.
        t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.requests, 1);
        CHECK_EQ(t.discarded, 0);
    }

    section("channel messages pass untouched when no SysEx is open");
    {
        SysexRx rx;
        const Totals t = run(rx, {{0x0B, 0xB0, 24, 1}, {0x0C, 0xC0, 3, 0}, {0x09, 0x90, 60, 100}, {0x0B, 0xBF, 0x77, 0x7F}});
        CHECK_EQ(t.notSysex, 4);
        CHECK_EQ(t.discarded, 0);
        CHECK_EQ(t.requests, 0);
    }

    section("commands, as USB-MIDI packs them (CIN 0x4, 0x4, 0x6), each with its guard");
    {
        SysexRx rx;
        const uint8_t cmds[3][3] = {{0x10, 'R', 'C'}, {0x11, 'R', 'P'}, {0x12, 'R', 'F'}};
        for (const auto& c : cmds) {
            const Totals t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, c[0], c[1]}, {0x06, c[2], 0xF7, 0}});
            CHECK_EQ(t.commands, 1);
            CHECK_EQ(t.command, c[0]);
            CHECK_EQ(t.requests, 0);
            CHECK_EQ(t.discarded, 0);
            CHECK_EQ(t.sysex, 3);
        }
        // Real-time bytes inside, and a status request right after: both kept.
        const Totals t = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x0F, 0xF8, 0, 0}, {0x04, 0x46, 0x10, 'R'},
                                  {0x0F, 0xFE, 0, 0}, {0x06, 'C', 0xF7, 0},
                                  {0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.commands, 1);
        CHECK_EQ(t.command, 0x10);
        CHECK_EQ(t.requests, 1);
        CHECK_EQ(t.discarded, 0);
        CHECK_EQ(t.notSysex, 2);
        // 13 with its argument byte (CIN 0x4, 0x4, 0x7): every byte 0..127
        // comes through; the bridge refuses those no channel can be.
        const uint8_t args[] = {0x00, 0x01, 0x06, 0x0D, 0x0E, 0x7E, 0x7F};
        for (uint8_t a : args) {
            const Totals c = run(rx, {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'C'}, {0x07, 'H', a, 0xF7}});
            CHECK_EQ(c.commands, 1);
            CHECK_EQ(c.command, CMD_SET_CHANNEL);
            CHECK_EQ(c.arg, a);
            CHECK_EQ(c.discarded, 0);
            CHECK_EQ(c.sysex, 3);
        }
        CHECK(validChannelChoice(CHANNEL_AUTO) && validChannelChoice(1) && validChannelChoice(13) &&
              validChannelChoice(CHANNEL_NEXT));
        CHECK(!validChannelChoice(14) && !validChannelChoice(0x7E) && !validChannelChoice(0x80));
        CHECK_EQ(commandArgs(CMD_SET_CHANNEL), 1);
        CHECK_EQ(commandArgs(CMD_REBOOT_FLOWLOCAL), 0);
    }

    section("not a command: wrong guard, another command's guard, wrong length, unknown byte");
    {
        SysexRx rx;
        struct Case { const char* what; std::vector<Pkt> packets; int discarded; };
        const Case cases[] = {
            {"guard R D",           {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'R'}, {0x06, 'D', 0xF7, 0}}, 3},
            {"guard r c",           {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'r'}, {0x06, 'c', 0xF7, 0}}, 3},
            {"10 with 11's guard",  {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'R'}, {0x06, 'P', 0xF7, 0}}, 3},
            {"12 with 10's guard",  {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x12, 'R'}, {0x06, 'C', 0xF7, 0}}, 3},
            {"no guard",            {{0x04, 0xF0, 0x7D, 0x50}, {0x07, 0x46, 0x10, 0xF7}}, 2},
            {"half a guard",        {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'R'}, {0x05, 0xF7, 0, 0}}, 3},
            {"a byte too many",     {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'R'}, {0x07, 'C', 0x00, 0xF7}}, 3},
            {"13 with 10's guard, no argument", {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'R'}, {0x06, 'C', 0xF7, 0}}, 3},
            {"13 without its argument", {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'C'}, {0x06, 'H', 0xF7, 0}}, 3},
            {"13 with two arguments", {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'C'}, {0x04, 'H', 6, 6}, {0x05, 0xF7, 0, 0}}, 4},
            {"13 with guard R H",   {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'R'}, {0x07, 'H', 6, 0xF7}}, 3},
            {"13 with guard c h",   {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x13, 'c'}, {0x07, 'h', 6, 0xF7}}, 3},
            {"12 with an argument", {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x12, 'R'}, {0x07, 'F', 6, 0xF7}}, 3},
            {"unknown command 14",  {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x14, 'C'}, {0x07, 'H', 6, 0xF7}}, 3},
            {"status request + guard", {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x01, 'R'}, {0x06, 'C', 0xF7, 0}}, 3},
            {"ack's own shape",     {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x03, 'R'}, {0x06, 'C', 0xF7, 0}}, 3},
            {"cut by a CC",         {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 'R'}, {0x0B, 0xB0, 1, 2}}, 2},
            {"status byte inside",  {{0x04, 0xF0, 0x7D, 0x50}, {0x04, 0x46, 0x10, 0xD2}, {0x06, 'C', 0xF7, 0}}, 3},
        };
        for (const Case& c : cases) {
            Totals t;
            for (const Pkt& p : c.packets) {
                const RxResult r = rx.feed(p.h, p.a, p.b, p.c);
                t.commands += r.command ? 1 : 0;
                t.requests += r.statusRequest ? 1 : 0;
                t.discarded += r.discarded;
            }
            if (t.commands != 0 || t.requests != 0 || t.discarded != c.discarded)
                std::printf("  FAIL %s: %d command(s), %d request(s), %d discarded (want %d)\n", c.what,
                            t.commands, t.requests, t.discarded, c.discarded);
            CHECK(t.commands == 0 && t.requests == 0 && t.discarded == c.discarded);
            CHECK(!rx.active());
        }
    }

    section("command gate: each command once per interval, on its own clock");
    {
        CommandGate g(3000);
        CHECK(g.allow(CMD_RECONNECT_PANEL, 0));            // 0 is a valid time
        CHECK(!g.allow(CMD_RECONNECT_PANEL, 2999));
        CHECK_EQ(g.waitMs(CMD_RECONNECT_PANEL, 2999), 1);
        CHECK_EQ(g.waitMs(CMD_RECONNECT_PANEL, 1000), 2000);
        CHECK(g.allow(CMD_REBOOT_PANEL, 1000));             // another command is not held up
        CHECK(g.allow(CMD_REBOOT_FLOWLOCAL, 1500));
        CHECK(g.allow(CMD_RECONNECT_PANEL, 3000));          // a refused one did not restart the interval
        CHECK_EQ(g.waitMs(CMD_RECONNECT_PANEL, 6000), 0);
        CHECK(g.allow(CMD_SET_CHANNEL, 100000));           // 13 on its own clock
        CHECK(!g.allow(CMD_SET_CHANNEL, 102999));
        CHECK(!g.allow(0x14, 100000));
        CHECK(!g.allow(STATUS_REQUEST, 100000));
        CHECK(!g.allow(0x0F, 100000));
        // Across the millisecond clock's wrap.
        CommandGate w(3000);
        CHECK(w.allow(CMD_REBOOT_PANEL, 0xFFFFFC00u));
        CHECK(!w.allow(CMD_REBOOT_PANEL, 0x00000100u));     // 1280 ms later
        CHECK(w.allow(CMD_REBOOT_PANEL, 0x00000800u));      // 3072 ms later
        g.reset();
        CHECK(g.allow(CMD_RECONNECT_PANEL, 3001));
    }

    section("ack text and bytes");
    {
        char text[ACK_TEXT_MAX + 1];
        size_t n = formatAck(text, sizeof text, CMD_RECONNECT_PANEL, true, "ended 1 RTP session, the panel invites again");
        CHECK_STR(std::string(text, n), "cmd=10;ok=1;msg=ended 1 RTP session, the panel invites again");
        n = formatAck(text, sizeof text, CMD_REBOOT_PANEL, false, "a;b=c\x01" "d");
        CHECK_STR(std::string(text, n), "cmd=11;ok=0;msg=a_b=c_d");
        n = formatAck(text, sizeof text, CMD_REBOOT_FLOWLOCAL, true, "");
        CHECK_STR(std::string(text, n), "cmd=12;ok=1;msg=");
        n = formatAck(text, sizeof text, CMD_SET_CHANNEL, true, "channel 11 from next boot, restarting now");
        CHECK_STR(std::string(text, n), "cmd=13;ok=1;msg=channel 11 from next boot, restarting now");
        const std::string longMsg(100, 'm');
        n = formatAck(text, sizeof text, CMD_REBOOT_PANEL, false, longMsg.c_str());
        CHECK_EQ(n, 16 + ACK_MSG_MAX);
        CHECK_EQ(ACK_TEXT_MAX, n);
        uint8_t msg[REPLY_MAX];
        const size_t len = buildMessage(msg, sizeof msg, COMMAND_ACK, text, n);
        std::vector<uint8_t> back;
        CHECK(unpack(msg, len, back));
        CHECK(back.size() == len && back[4] == 0x03 && back[len - 1] == 0xF7);
        std::printf("    ack example: cmd=10;ok=1;msg=ended 1 RTP session, the panel invites again\n");
    }

    section("reset forgets a half-read SysEx");
    {
        SysexRx rx;
        run(rx, {{0x04, 0xF0, 0x7D, 0x50}});
        CHECK(rx.active());
        rx.reset();
        CHECK(!rx.active());
        const Totals t = run(rx, {{0x07, 0x46, 0x01, 0xF7}});
        CHECK_EQ(t.requests, 0);
        CHECK_EQ(t.discarded, 1);
    }

    section("reply packets for every text length 0..240: CINs, padding, bytes");
    {
        uint8_t msg[REPLY_MAX];
        std::vector<uint8_t> back;
        std::string text;
        bool allOk = true;
        for (size_t n = 0; n <= TEXT_MAX; ++n) {
            text.assign(n, 'a');
            for (size_t i = 0; i < n; ++i) text[i] = (char)(0x20 + (i * 7) % 95);
            const size_t len = buildReply(msg, sizeof msg, text.data(), text.size());
            bool ok = len == n + 6 && unpack(msg, len, back) && back.size() == len &&
                      back[0] == 0xF0 && back[1] == 0x7D && back[2] == 0x50 && back[3] == 0x46 &&
                      back[4] == 0x02 && back[len - 1] == 0xF7 &&
                      std::string(back.begin() + 5, back.end() - 1) == text &&
                      packetCount(len) == (len + 2) / 3;
            for (size_t i = 1; ok && i + 1 < len; ++i) ok = back[i] < 0x80;
            if (!ok) {
                std::printf("  FAIL text length %u\n", (unsigned)n);
                allOk = false;
            }
        }
        CHECK(allOk);
        // The three endings, spelled out.
        uint8_t p[4];
        size_t len = buildReply(msg, sizeof msg, "", 0);             // 6 bytes: 4 + 7
        CHECK_EQ(len, 6);
        CHECK(replyPacket(msg, len, 0, p) && p[0] == 0x4 && p[1] == 0xF0 && p[2] == 0x7D && p[3] == 0x50);
        CHECK(replyPacket(msg, len, 1, p) && p[0] == 0x7 && p[1] == 0x46 && p[2] == 0x02 && p[3] == 0xF7);
        CHECK(!replyPacket(msg, len, 2, p));
        len = buildReply(msg, sizeof msg, "v", 1);                   // 7 bytes: 4 + 4 + 5
        CHECK(replyPacket(msg, len, 2, p) && p[0] == 0x5 && p[1] == 0xF7 && p[2] == 0 && p[3] == 0);
        len = buildReply(msg, sizeof msg, "v=", 2);                  // 8 bytes: 4 + 4 + 6
        CHECK(replyPacket(msg, len, 2, p) && p[0] == 0x6 && p[1] == '=' && p[2] == 0xF7 && p[3] == 0);
        // Longest: 246 bytes, 82 packets.
        std::string longest(TEXT_MAX + 10, 'x');
        len = buildReply(msg, sizeof msg, longest.data(), longest.size());
        CHECK_EQ(len, REPLY_MAX);
        CHECK_EQ(packetCount(len), REPLY_PACKETS_MAX);
        CHECK_EQ(REPLY_PACKETS_MAX, 82);
        // Bytes a reply must not carry go out as '?'.
        len = buildReply(msg, sizeof msg, "a\x01\xF7\x7F" "b", 5);
        CHECK(msg[5] == 'a' && msg[6] == '?' && msg[7] == '?' && msg[8] == '?' && msg[9] == 'b' && msg[10] == 0xF7);
        // Too small a buffer.
        CHECK_EQ(buildReply(msg, 5, "", 0), 0);
    }

    section("reply text: typical values, in the protocol's key order");
    {
        Status s;
        s.fw = "flx-mon3";
        s.up = 3725;
        s.ap = 1;
        s.ch = 1;
        s.chm = 1;
        s.cl = 2;
        s.cm = 3;
        s.ps = 1;
        s.pip = 0x0242A8C0u;   // 192.168.66.2
        s.prsKnown = true;
        s.prs = -47;
        s.hd = 1834;
        s.ra = 212;
        s.lp = 5;
        s.knobs = 3;
        s.knobSeen = 5;        // K2 not seen
        s.knobValue[0] = 64;
        s.knobValue[1] = 99;
        s.knobValue[2] = 127;
        s.jt = 3;
        s.jm = 41;
        s.rx = 5120; s.tx = 6144; s.dr = 0; s.se = 2; s.to = 1; s.rj = 0; s.gp = 3; s.rr = 4; s.ud = 0;
        s.sr = 1862; s.hi = 181234;
        s.ur = 0; s.rsn = "poweron"; s.bc = 1; s.why = "";
        s.scanned = true; s.cs[0] = 0; s.cs[1] = 2350; s.cs[2] = 3425;   // the home scan (link_quality_test)
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        CHECK_STR(std::string(text, n),
                  "v=1;fw=flx-mon3;up=3725;ap=1;ch=1;chm=auto;cl=2;cm=3;ps=1;pip=192.168.66.2;prs=-47;"
                  "hd=1834;ra=212;lp=5;kn=64,-,127;jt=3;jm=41;rx=5120;tx=6144;dr=0;se=2;to=1;rj=0;gp=3;rr=4;"
                  "ud=0;sr=1862;hi=181234;rsn=poweron;bc=1;why=;ur=0;cs=1:0,6:24,11:35");
        std::printf("    typical reply text (%u bytes): %s\n", (unsigned)n, text);
    }

    section("reply text: unknown values empty (or -1 where the protocol says so), every key there");
    {
        Status s;
        s.fw = "flx-mon3";
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        CHECK_STR(std::string(text, n),
                  "v=1;fw=flx-mon3;up=0;ap=;ch=;chm=;cl=;cm=;ps=0;pip=;prs=;hd=-1;ra=-1;lp=-1;kn=;"
                  "jt=-1;jm=-1;rx=0;tx=0;dr=0;se=0;to=0;rj=0;gp=0;rr=0;ud=0;sr=0;hi=0;rsn=;bc=;why=;ur=0;cs=");
        s.knobs = 3;
        s.ap = 0;
        s.chm = 0;
        s.ch = 11;
        const size_t m = formatStatus(text, sizeof text, s);
        CHECK(std::string(text, m).find(";ap=0;ch=11;chm=fixed;") != std::string::npos);
        CHECK(std::string(text, m).find(";kn=-,-,-;") != std::string::npos);
        s.chm = CHM_MANUAL;
        size_t k = formatStatus(text, sizeof text, s);
        CHECK(std::string(text, k).find(";ch=11;chm=manual;") != std::string::npos);
        s.chm = CHM_AUTO;
        k = formatStatus(text, sizeof text, s);
        CHECK(std::string(text, k).find(";chm=auto;") != std::string::npos);
        s.chm = 7;                                   // no such mode: unknown
        k = formatStatus(text, sizeof text, s);
        CHECK(std::string(text, k).find(";chm=;") != std::string::npos);
    }

    section("reply text: a busy exhibition day keeps every key up to hi; the restart keys go first");
    {
        Status s;
        s.fw = "flx-mon3";
        s.up = 86400;
        s.ap = 1; s.ch = 11; s.chm = 1; s.cl = 3; s.cm = 3; s.ps = 1;
        s.pip = 0x6442A8C0u;                   // 192.168.66.100
        s.prsKnown = true; s.prs = -85;
        s.hd = 9999; s.ra = 3599999; s.lp = 127;
        s.knobs = 3; s.knobSeen = 7;
        s.knobValue[0] = s.knobValue[1] = s.knobValue[2] = 127;
        s.jt = 250; s.jm = 59999;
        s.rx = s.tx = 9999999u;                // 115 messages a second all day
        s.dr = s.gp = 99999u;
        s.se = s.to = 999u; s.rj = 99u;
        s.rr = s.ud = 9999u;
        s.sr = 99999u;                         // a request every 2 s: 43200
        s.hi = 999999u;
        s.ur = 1; s.rsn = "task_wdt"; s.bc = 2; s.why = "bridge_hung";
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        const std::string t(text, n);
        std::printf("    a busy day's reply text (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        CHECK(t.size() >= 6 && t.compare(t.size() - 6, 6, "999999") == 0);   // ends with hi=, whole
        CHECK(t.find(";sr=99999;hi=999999") != std::string::npos);
        CHECK(t.find(";rsn=") == std::string::npos);  // 238 bytes up to hi: no room for the restart keys
        CHECK(t.find(";cs=") == std::string::npos);   // nor for cs
    }

    section("reply text: ten hours of a busy panel keep every key, the restart keys too");
    {
        Status s;
        s.fw = "flx-mon3";
        s.up = 36000;
        s.ap = 1; s.ch = 11; s.chm = 1; s.cl = 2; s.cm = 3; s.ps = 1;
        s.pip = 0x0242A8C0u;                   // 192.168.66.2
        s.prsKnown = true; s.prs = -55;
        s.hd = 1834; s.ra = 212; s.lp = 6;
        s.knobs = 3; s.knobSeen = 7;
        s.knobValue[0] = 64; s.knobValue[1] = 99; s.knobValue[2] = 127;
        s.jt = 4; s.jm = 120;
        s.rx = 1500000u; s.tx = 1500050u;      // 40 messages a second for ten hours
        s.dr = 12; s.se = 3; s.to = 1; s.rj = 0; s.gp = 250; s.rr = 40; s.ud = 0;
        s.sr = 18000; s.hi = 181234;
        s.rsn = "poweron"; s.bc = 1; s.why = ""; s.ur = 0;
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        const std::string t(text, n);
        std::printf("    ten hours (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        const std::string tail = ";hi=181234;rsn=poweron;bc=1;why=;ur=0;cs=";   // no scan (a fixed channel)
        CHECK(t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0);
        CHECK_EQ(n, TEXT_MAX);                          // exactly full
        s.scanned = true;                               // with scores cs= does not fit any more: it goes, whole
        s.cs[0] = 0; s.cs[1] = 2350; s.cs[2] = 3425;
        const size_t m = formatStatus(text, sizeof text, s);
        const std::string u(text, m);
        const std::string ends = ";hi=181234;rsn=poweron;bc=1;why=;ur=0";
        CHECK(u.size() >= ends.size() && u.compare(u.size() - ends.size(), ends.size(), ends) == 0);
    }

    section("reply text: right after a restart (small counters) the restart keys are all there");
    {
        Status s;
        s.fw = "flx-mon3";
        s.up = 25;
        s.ap = 1; s.ch = 11; s.chm = 1; s.cl = 1; s.cm = 3; s.ps = 1;
        s.pip = 0x6442A8C0u;                   // 192.168.66.100
        s.prsKnown = true; s.prs = -85;
        s.hd = 3200; s.ra = 3200; s.lp = 127;
        s.knobs = 3; s.knobSeen = 7;
        s.knobValue[0] = s.knobValue[1] = s.knobValue[2] = 127;
        s.jt = 25; s.jm = 1200;
        s.rx = 40; s.tx = 45; s.dr = 0; s.se = 1; s.to = 0; s.rj = 0; s.gp = 0; s.rr = 1; s.ud = 0;
        s.sr = 12; s.hi = 181234;
        s.rsn = "task_wdt";
        s.bc = 4;
        s.why = "bridge_hung";
        s.ur = 2;
        char text[TEXT_MAX + 1];
        size_t n = formatStatus(text, sizeof text, s);
        std::string t(text, n);
        std::printf("    after a restart (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        const std::string tail = ";hi=181234;rsn=task_wdt;bc=4;why=bridge_hung;ur=2";
        CHECK(t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0);
        CHECK(t.find(";cs=") == std::string::npos);    // 237 bytes: cs= is the one that goes
        // Every restart key at its widest, on a fresh boot's small values.
        Status w;
        w.fw = "flx-mon3";
        w.up = 3; w.ap = 0; w.chm = 1; w.cl = 0; w.cm = 3; w.knobs = 3; w.sr = 1; w.hi = 190000;
        w.rsn = "cpu_lockup"; w.bc = 4294967295u; w.why = "bridge_hung"; w.ur = 4294967295u;
        n = formatStatus(text, sizeof text, w);
        t.assign(text, n);
        CHECK(n <= TEXT_MAX);
        const std::string wide = ";rsn=cpu_lockup;bc=4294967295;why=bridge_hung;ur=4294967295;cs=";
        CHECK(t.size() >= wide.size() && t.compare(t.size() - wide.size(), wide.size(), wide) == 0);
    }

    section("reply text: rsn= and why= are made fit for the text, at most 16 characters");
    {
        Status s;
        s.fw = "flx-mon3";
        s.rsn = "a;b\x01" "c";
        s.why = "0123456789abcdefXYZ";
        s.bc = 7;
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        const std::string t(text, n);
        const std::string tail = ";rsn=a_b_c;bc=7;why=0123456789abcdef;ur=0;cs=";
        CHECK(t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0);
    }

    section("reply text: the shortest is STATUS_TEXT_MIN bytes (the bridge's proof that a whole reply was read)");
    {
        Status s;                              // every value at its shortest
        s.fw = "";
        s.hd = 0; s.ra = 0; s.lp = 0; s.jt = 0; s.jm = 0;
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        std::printf("    shortest reply text (%u bytes): %s\n", (unsigned)n, text);
        CHECK_EQ(n, STATUS_TEXT_MIN);
        // A text that loses fields to TEXT_MAX is longer: at least TEXT_MAX less the longest field.
        CHECK(STATUS_TEXT_MIN < TEXT_MAX - FIELD_MAX);
        CHECK_EQ(packetCount(MESSAGE_HEAD + STATUS_TEXT_MIN + 1), 54);   // TinyUSB holds 32 (64 + 64 bytes)
    }

    section("reply text: a month without a restart may lose its last keys, whole");
    {
        // Every key at a value a month of running could reach, the long ones
        // at their widest. 240 bytes cannot hold all of it: the keys that do
        // not fit are the last ones, left out whole (a reader sees them as
        // unknown), and everything up to the packet gaps is there.
        Status s;
        s.fw = "flx-mon3";
        s.up = 31u * 24u * 3600u;              // 2678400
        s.ap = 1; s.ch = 11; s.chm = 1; s.cl = 3; s.cm = 3; s.ps = 2;
        s.pip = 0xFFFFA8C0u;                   // 192.168.255.255
        s.prsKnown = true; s.prs = -100;
        s.hd = 2678400000ll; s.ra = 2678400000ll; s.lp = 127;
        s.knobs = 3; s.knobSeen = 7;
        s.knobValue[0] = s.knobValue[1] = s.knobValue[2] = 127;
        s.jt = 9999; s.jm = 99999;
        s.rx = s.tx = 99999999u;               // 37 messages a second for a month
        s.dr = s.gp = 9999999u;
        s.se = s.to = s.rj = 99999u;
        s.rr = s.ud = 999999u;
        s.sr = 9999999u;                       // a request every 2 s: 1.34 million
        s.hi = 999999u;
        s.ur = 3; s.rsn = "sw"; s.bc = 2; s.why = "usb_wedged";
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        const std::string t(text, n);
        std::printf("    a month's reply text (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        const std::string all = "v,fw,up,ap,ch,chm,cl,cm,ps,pip,prs,hd,ra,lp,kn,jt,jm,rx,tx,dr,se,to,rj,gp,rr,ud,sr,hi,"
                                "rsn,bc,why,ur,cs";
        std::string have;
        for (size_t at = 0;;) {
            const size_t end = t.find(';', at);
            const std::string f = t.substr(at, end == std::string::npos ? std::string::npos : end - at);
            have += (have.empty() ? "" : ",") + f.substr(0, f.find('='));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        CHECK(all.compare(0, have.size(), have) == 0);     // a prefix of the key list
        CHECK(have.find(",gp") != std::string::npos);
        CHECK(t.back() != ';');
    }

    section("reply text: every value at its widest still fits, cut only between fields");
    {
        Status s;
        s.fw = "abcdefghijklmnopqrstuvwxyz012345";   // 32, the longest tag
        s.up = 4294967295u;
        s.ap = 1; s.cl = 32767; s.cm = 32767; s.ps = 2147483647;
        s.pip = 0xFFFFFFFFu;
        s.prsKnown = true; s.prs = -2147483647 - 1;
        s.hd = 4294967295ll; s.ra = 4294967295ll; s.lp = 127;
        s.knobs = 8; s.knobSeen = 0xFF;
        for (uint8_t& v : s.knobValue) v = 127;
        s.rx = s.tx = s.dr = s.se = s.to = s.rj = s.gp = s.rr = s.ud = s.sr = s.hi = 4294967295u;
        s.ur = s.bc = 4294967295u;
        s.rsn = "pwr_glitch";
        s.why = "bridge_hung";
        s.scanned = true;
        s.cs[0] = s.cs[1] = s.cs[2] = 4294967295u;
        char text[TEXT_MAX + 1];
        const size_t n = formatStatus(text, sizeof text, s);
        const std::string t(text, n);
        std::printf("    widest reply text (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        CHECK(n > 200);
        CHECK(t.back() != ';');
        CHECK(t.compare(0, 4, "v=1;") == 0);
        // Each field whole: key=value with a value made of the characters it may have.
        size_t at = 0;
        bool whole = true;
        for (;;) {
            const size_t end = t.find(';', at);
            const std::string f = t.substr(at, end == std::string::npos ? std::string::npos : end - at);
            const size_t eq = f.find('=');
            whole = whole && eq != std::string::npos && eq >= 1 && eq <= 3;
            if (end == std::string::npos) break;
            at = end + 1;
        }
        CHECK(whole);
        CHECK(t.find(";hi=") == std::string::npos);   // the last fields went; the first stayed
        CHECK(t.find(";why=") == std::string::npos);
        CHECK(t.find("kn=127,127,127,127,127,127,127,127") != std::string::npos);
        // With a smaller buffer the same holds.
        char small[40];
        const size_t m = formatStatus(small, sizeof small, s);
        CHECK(m < sizeof small);
        CHECK_STR(std::string(small, m), "v=1;fw=abcdefghijklmnopqrstuvwxyz012345");
    }

    section("reply text: cs= channel:score for 1 / 6 / 11 in hundreds rounded up; empty without a scan; the first key to go");
    {
        CHECK_EQ(scoreHundreds(0), 0);
        CHECK_EQ(scoreHundreds(1), 1);                  // a network heard at all is never 0
        CHECK_EQ(scoreHundreds(100), 1);
        CHECK_EQ(scoreHundreds(101), 2);
        CHECK_EQ(scoreHundreds(2350), 24);
        CHECK_EQ(scoreHundreds(3425), 35);
        CHECK_EQ(scoreHundreds(4294967295u), 42949673);
        // Right after the restart command 13 asks for, in a crowded hall:
        // small counters, cs= there. It stays the same for the whole boot, so
        // a reader keeps it, as it keeps rsn= / bc= / why=.
        Status h;
        h.fw = "flx-mon3";
        h.up = 40; h.ap = 1; h.ch = 6; h.chm = CHM_MANUAL; h.cl = 1; h.cm = 3; h.ps = 1;
        h.pip = 0x0242A8C0u;
        h.prsKnown = true; h.prs = -48;
        h.hd = 8439; h.ra = 21003; h.lp = 5;
        h.knobs = 3; h.knobSeen = 7;
        h.knobValue[0] = 64; h.knobValue[1] = 64; h.knobValue[2] = 64;
        h.jt = 2; h.jm = 15;
        h.rx = 40; h.tx = 45; h.dr = 0; h.se = 1; h.to = 0; h.rj = 0; h.gp = 0; h.rr = 1; h.ud = 0;
        h.sr = 14; h.hi = 138776;
        h.rsn = "sw"; h.bc = 8; h.why = "cmd13"; h.ur = 0;
        h.scanned = true; h.cs[0] = 48000; h.cs[1] = 52350; h.cs[2] = 61001;
        char text[TEXT_MAX + 1];
        size_t n = formatStatus(text, sizeof text, h);
        std::string t(text, n);
        std::printf("    after command 13's restart, a crowded hall (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        CHECK(t.find(";ch=6;chm=manual;") != std::string::npos);
        std::string tail = ";rsn=sw;bc=8;why=cmd13;ur=0;cs=1:480,6:524,11:611";
        CHECK(t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0);
        // An hour on (the board's own reply of 2026-09-29 had these ra= and
        // hd=): cs= no longer fits and goes, whole; the restart keys stay.
        h.up = 3725; h.cl = 2; h.ra = 1167117; h.jt = 3; h.jm = 41;
        h.rx = 5120; h.tx = 6144; h.se = 2; h.to = 1; h.gp = 3; h.rr = 4; h.sr = 1862; h.hi = 181234;
        n = formatStatus(text, sizeof text, h);
        t.assign(text, n);
        std::printf("    the same boot an hour on (%u bytes): %s\n", (unsigned)n, text);
        CHECK(n <= TEXT_MAX);
        tail = ";rsn=sw;bc=8;why=cmd13;ur=0";
        CHECK(t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0);
        CHECK(t.find(";cs=") == std::string::npos);
    }

    section("reply text: the fw tag is made fit for the text");
    {
        Status s;
        s.fw = "bad tag;x=\x01y";
        char text[TEXT_MAX + 1];
        formatStatus(text, sizeof text, s);
        CHECK(std::string(text).compare(0, 19, "v=1;fw=bad_tag_x__y") == 0);
        for (const char* c = text; *c; ++c) CHECK(*c >= 0x20 && *c <= 0x7E);
        CHECK(validTag("flx-mon1"));
        CHECK(validTag("flx-mon2"));
        CHECK(validTag("flx-mon3"));
        CHECK(!validTag(""));
        CHECK(!validTag("a b"));
        CHECK(!validTag("a;b"));
        CHECK(!validTag("a=b"));
        CHECK(!validTag("abcdefghijklmnopqrstuvwxyz0123456"));   // 33
    }

    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
