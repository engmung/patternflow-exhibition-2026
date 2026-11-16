// Host test for the exhibit bridge's RTP session handling: the AppleMIDI
// 3.5.0 session members src/fl_midi_bridge.h specialises, compiled into the
// REAL library (.pio/libdeps/exhibit: AppleMIDI, MIDI Library) with the whole
// bridge header, over the model UDP socket in exhibit_stubs.h. Built and run
// by tools/check_exhibit_bridge.py.
//
// A scripted panel sends what AppleMIDI 3.5.0 sends as initiator: IN on the
// control port, IN on the data port, CK0 heartbeats, RTP MIDI, BY. Every check
// is on what FlowLocal sent back (OK / NO / BY / CK1, in order), on the
// bridge's bookkeeping (peers, the panel address, the tokens on record) and
// on its counters. Receiver feedback (RS) is left out of the sequences.

#include "exhibit_stubs.h"

#define FL_EXHIBIT 1
#define FL_AP_MAX_CONN 3
#ifndef FL_EXHIBIT_AP_CHANNEL
#define FL_EXHIBIT_AP_CHANNEL 0
#endif
#include "src/fl_midi_bridge.h"
#include "AppleMIDI.cpp"   // the library's `now` and refreshNow(), as the firmware links them

using namespace FlMidiBridge;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_EQ(have, want)                                                                  \
    do {                                                                                      \
        ++g_checks;                                                                           \
        const long long h_ = (long long)(have), w_ = (long long)(want);                       \
        if (h_ != w_) {                                                                       \
            std::printf("  FAIL line %d: %s = %lld, want %lld\n", __LINE__, #have, h_, w_);   \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++g_checks;                                                                           \
        if (!(cond)) {                                                                        \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);                            \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

static void section(const char* name) { std::printf("- %s\n", name); }

static std::string seq(std::initializer_list<const char*> parts) {
    std::string out;
    for (const char* p : parts) {
        if (!*p) continue;
        if (!out.empty()) out += ", ";
        out += p;
    }
    return out;
}

// ── The panel ──────────────────────────────────────────────────────────────
constexpr uint32_t ip4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return a | b << 8 | c << 16 | d << 24; }
constexpr uint32_t PANEL_IP = ip4(192, 168, 66, 2);
constexpr uint32_t PHONE_IP = ip4(192, 168, 66, 4);
constexpr uint32_t OTHER_IP = ip4(192, 168, 66, 5);

static void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}
static void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int s = 56; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

// What AppleMIDI 3.5.0 sends as the session initiator (AppleMIDI.hpp:
// manageSessionInvites, sendSynchronization, writeRtpMidiBuffer,
// sendEndSession), from ctl (its control port) and ctl + 1 (data).
struct Panel {
    uint32_t ip;
    uint16_t ctl;
    uint32_t ssrc;
    uint32_t token;
    uint16_t rtpSeq = 100;

    void send(uint16_t toLocal, uint16_t fromPort, std::vector<uint8_t> bytes) const {
        Datagram d;
        d.local = toLocal;
        d.ip = ip;
        d.port = fromPort;
        d.bytes = std::move(bytes);
        udp.inbound[toLocal].push_back(std::move(d));
    }
    void invite(bool dataPort = false) const {
        std::vector<uint8_t> b = {0xFF, 0xFF, 'I', 'N', 0, 0, 0, 2};
        put32(b, token);
        put32(b, ssrc);
        for (const char* c = "Patternflow"; *c; ++c) b.push_back((uint8_t)*c);
        b.push_back(0);
        send(dataPort ? 5005 : 5004, dataPort ? (uint16_t)(ctl + 1) : ctl, b);
    }
    void ck0() const {
        std::vector<uint8_t> b = {0xFF, 0xFF, 'C', 'K'};
        put32(b, ssrc);
        b.push_back(0);   // count: CK0
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        put64(b, (uint64_t)g_now * 10);
        put64(b, 0);
        put64(b, 0);
        send(5005, (uint16_t)(ctl + 1), b);
    }
    void cc(uint8_t control, uint8_t value) {
        std::vector<uint8_t> b = {0x80, 0x61, (uint8_t)(rtpSeq >> 8), (uint8_t)rtpSeq};
        ++rtpSeq;
        put32(b, g_now * 10);
        put32(b, ssrc);
        b.push_back(0x03);   // MIDI command section: short header, 3 bytes, no journal
        b.push_back(0xB0);
        b.push_back(control);
        b.push_back(value);
        send(5005, (uint16_t)(ctl + 1), b);
    }
    void bye() const {
        std::vector<uint8_t> b = {0xFF, 0xFF, 'B', 'Y', 0, 0, 0, 2};
        put32(b, 0);
        put32(b, ssrc);
        send(5004, ctl, b);
    }
};

// ── What FlowLocal sent ────────────────────────────────────────────────────
static uint32_t be32(const std::vector<uint8_t>& v, size_t at) {
    return (uint32_t)v[at] << 24 | (uint32_t)v[at + 1] << 16 | (uint32_t)v[at + 2] << 8 | v[at + 3];
}
static int g_nos = 0;   // NO answers, ever: there must be none
static int g_rs = 0;    // receiver feedback, not shown

// "OK ctl>.2:5004 t=42" (answer, FlowLocal's port, the other end's last
// octet and port, the initiator token), "BY ctl>.4:5004", "CK1 data>.2:5005".
// A packet that does not carry FlowLocal's SSRC is marked.
static std::string fmtSent(const Datagram& d) {
    const std::vector<uint8_t>& v = d.bytes;
    const uint32_t ours = rtpSession.getSynchronizationSource();
    const char* from = d.local == 5004 ? "ctl" : d.local == 5005 ? "data" : "?";
    char b[96];
    if (v.size() < 4 || v[0] != 0xFF || v[1] != 0xFF) return "(not AppleMIDI)";
    const std::string cmd{(char)v[2], (char)v[3]};
    if ((cmd == "OK" || cmd == "NO") && v.size() >= 17) {
        if (cmd == "NO") ++g_nos;
        std::snprintf(b, sizeof b, "%s %s>.%u:%u t=%X%s", cmd.c_str(), from, (unsigned)(d.ip >> 24),
                      (unsigned)d.port, (unsigned)be32(v, 8), be32(v, 12) == ours ? "" : " (NOT OUR SSRC)");
    } else if (cmd == "BY" && v.size() >= 16) {
        std::snprintf(b, sizeof b, "BY %s>.%u:%u%s", from, (unsigned)(d.ip >> 24), (unsigned)d.port,
                      be32(v, 12) == ours ? "" : " (NOT OUR SSRC)");
    } else if (cmd == "CK" && v.size() >= 36) {
        std::snprintf(b, sizeof b, "CK%u %s>.%u:%u%s", (unsigned)v[8], from, (unsigned)(d.ip >> 24),
                      (unsigned)d.port, be32(v, 4) == ours ? "" : " (NOT OUR SSRC)");
    } else {
        std::snprintf(b, sizeof b, "%s %s>.%u:%u", cmd.c_str(), from, (unsigned)(d.ip >> 24), (unsigned)d.port);
    }
    return b;
}

static size_t g_seen = 0;   // udp.sent up to here is checked
static std::string sentText() {
    std::string out;
    for (; g_seen < udp.sent.size(); ++g_seen) {
        const Datagram& d = udp.sent[g_seen];
        if (d.bytes.size() >= 4 && d.bytes[2] == 'R' && d.bytes[3] == 'S') {
            ++g_rs;
            continue;
        }
        if (!out.empty()) out += ", ";
        out += fmtSent(d);
    }
    return out;
}
static void expectSent(const std::string& want, int line) {
    ++g_checks;
    const std::string have = sentText();
    if (have != want) {
        std::printf("  FAIL line %d: FlowLocal sent\n      [%s]\n    want\n      [%s]\n", line, have.c_str(),
                    want.c_str());
        ++g_failures;
    }
}
#define EXPECT_SENT(...) expectSent(seq({__VA_ARGS__}), __LINE__)

// ── The bridge task ────────────────────────────────────────────────────────
static void pass() {
    g_passesLeft = 1;
    try {
        task(nullptr);
    } catch (const EndOfPass&) {
    }
    vTaskDelay(1);
}
static void passes(int n) {
    for (int i = 0; i < n; ++i) pass();
}
// Passes until everything the panel sent has been read (the library takes at
// most one control and one data datagram per read), then two more.
static void settle() {
    for (int i = 0; i < 200 && (!udp.inbound[5004].empty() || !udp.inbound[5005].empty()); ++i) pass();
    passes(2);
}

// A panel with its session up: control IN, data IN, both answered.
static void join(Panel& p) {
    p.invite();
    settle();
    p.invite(true);
    settle();
}

// Every session the library holds ended (a BY each), and the bridge's
// bookkeeping and counters back to zero.
static void resetSessions() {
    rtpSession.sendEndSession();
    udp = UdpNet{};
    g_seen = 0;
    g_now += 5000;
    for (std::atomic<uint32_t>* c : {&sessions, &rxCount, &droppedCount, &rtpRejected, &rtpTimeouts, &rtpReplaced,
                                     &rtpEvicted, &rtpRetryOks, &rtpUnknown, &rtpByeSent, &lastUnknownMs,
                                     &commandsReceived, &commandsRefused, &acksSent, &acksLost})
        c->store(0);
    byeQuiet = false;
    byeQuietSinceMs = 0;
    lastStranger = 0;
    lastStrangerByeMs = 0;
    sessionEnds = 0;
    commandGate.reset();
    g_log.clear();
    passes(2);
}

int main() {
    std::printf("exhibit RTP session host test (AppleMIDI %d.%d.%d, the bridge's specialisations)\n",
                APPLEMIDI_LIBRARY_VERSION_MAJOR, APPLEMIDI_LIBRARY_VERSION_MINOR, APPLEMIDI_LIBRARY_VERSION_PATCH);
    bootBegin();
    beginUsb();
    begin();                                   // the session listener on 5004 / 5005
    CHECK(rtpReady.load());
    CHECK(logged("[midi] rtp sessions: never refused (the one heard from least recently makes room), "
                 "ended after 25 s unheard, a repeated invitation gets its OK again"));
    const uint32_t ours = rtpSession.getSynchronizationSource();
    CHECK(ours == localSsrc() && ours != 0);

    section("a panel joins: the control OK (our SSRC and name), the data OK, one session");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x11110000u, 0x42};
        p.invite();
        settle();
        CHECK(udp.sent.size() == 1);
        if (!udp.sent.empty()) {
            const std::vector<uint8_t>& ok = udp.sent[0].bytes;
            CHECK(ok.size() == 16 + 10 && std::string(ok.begin() + 16, ok.end() - 1) == "FlowLocal" && ok.back() == 0);
        }
        EXPECT_SENT("OK ctl>.2:5004 t=42");
        CHECK_EQ(peers.load(), 0);                     // not before the data handshake
        p.invite(true);
        settle();
        EXPECT_SENT("OK data>.2:5005 t=42");
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(sessions.load(), 1);
        CHECK_EQ(panelIp.load(), PANEL_IP);
        CHECK(logged("[midi] rtp session joined by \"Patternflow\" (1)"));
        CHECK(tokenFor(p.ssrc) != nullptr && tokenFor(p.ssrc)->token == 0x42 && tokenFor(p.ssrc)->ip == PANEL_IP);
        p.ck0();
        settle();
        EXPECT_SENT("CK1 data>.2:5005");
        p.cc(24, 64);
        settle();
        CHECK_EQ(rxCount.load(), 1);
        EXPECT_SENT();
    }

    section("the OK lost on the air: the panel's repeat (same SSRC and token) gets the same OK again, one session");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x11110002u, 0x51};
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=51");
        const std::vector<uint8_t> first = udp.sent.back().bytes;
        g_now += 1000;                                 // the panel repeats a second later (lost: it never saw it)
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=51");
        CHECK(udp.sent.back().bytes == first);         // the same OK, byte for byte
        CHECK_EQ(rtpRetryOks.load(), 1);
        CHECK_EQ(rtpReplaced.load(), 0);
        CHECK(logged("[midi] rtp invitation repeated by 192.168.66.2: the same OK again"));
        g_now += 1000;                                 // and again: answered again
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=51");
        CHECK_EQ(rtpRetryOks.load(), 2);
        p.invite(true);                                // the third OK arrived: the data handshake
        settle();
        EXPECT_SENT("OK data>.2:5005 t=51");
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(sessions.load(), 1);
        CHECK(logged("[midi] rtp session joined by \"Patternflow\" (1)"));
        // A copy of the control invitation arriving after the data handshake
        // is stale: the panel had its OK. No answer.
        p.invite();
        settle();
        EXPECT_SENT();
        CHECK_EQ(rtpRetryOks.load(), 2);
        CHECK_EQ(peers.load(), 1);
        // Still one session in the library's list: a second device fits
        // without evicting anyone, a third makes the list evict.
        Panel phone{PHONE_IP, 5004, 0x22220000u, 0x61};
        phone.invite();
        settle();
        EXPECT_SENT("OK ctl>.4:5004 t=61");
        CHECK_EQ(rtpEvicted.load(), 0);
        g_now += 10;
        Panel other{OTHER_IP, 5004, 0x33330000u, 0x71};
        other.invite();
        settle();
        CHECK_EQ(rtpEvicted.load(), 1);
        EXPECT_SENT("BY ctl>.2:5004", "OK ctl>.5:5004 t=71");   // the panel was heard least recently
    }

    section("the panel started over (same SSRC, new token): the half-made session is replaced, no BY");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x11110004u, 0x81};
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=81");
        g_now += 4000;
        p.token = 0x82;
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=82");
        CHECK_EQ(rtpReplaced.load(), 1);
        CHECK_EQ(rtpRetryOks.load(), 0);
        CHECK(tokenFor(p.ssrc) != nullptr && tokenFor(p.ssrc)->token == 0x82);
        p.invite(true);
        settle();
        EXPECT_SENT("OK data>.2:5005 t=82");
        CHECK_EQ(peers.load(), 1);
        // The same token from another port is a new start too, not a repeat.
        Panel moved = p;
        moved.ctl = 6004;
        moved.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:6004 t=82");
        CHECK_EQ(rtpReplaced.load(), 2);
        CHECK_EQ(rtpRetryOks.load(), 0);
        CHECK_EQ(peers.load(), 0);                     // the old one left; the new one has no data handshake yet
    }

    section("the panel rebooted (same address and port, new SSRC): its session is replaced, no BY");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x11110006u, 0x91};
        join(p);
        EXPECT_SENT("OK ctl>.2:5004 t=91", "OK data>.2:5005 t=91");
        CHECK_EQ(peers.load(), 1);
        Panel rebooted{PANEL_IP, 5004, 0x44440000u, 0x92};
        rebooted.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=92");
        CHECK_EQ(rtpReplaced.load(), 1);
        CHECK_EQ(peers.load(), 0);
        CHECK(tokenFor(p.ssrc) == nullptr);            // the old record went with its session
        rebooted.invite(true);
        settle();
        EXPECT_SENT("OK data>.2:5005 t=92");
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(sessions.load(), 2);
        CHECK_EQ(panelIp.load(), PANEL_IP);
    }

    section("both slots taken: the one heard from least recently ends with a BY, the invitation is accepted, no NO");
    {
        resetSessions();
        Panel a{PANEL_IP, 5004, 0xAAAA0000u, 0xA1};
        join(a);
        g_now += 1000;
        Panel b{PHONE_IP, 5004, 0xBBBB0000u, 0xB1};
        join(b);
        g_now += 1000;
        a.ck0();                                       // a heard last: b is the one heard least recently
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=A1", "OK data>.2:5005 t=A1", "OK ctl>.4:5004 t=B1", "OK data>.4:5005 t=B1",
                    "CK1 data>.2:5005");
        CHECK_EQ(peers.load(), 2);
        CHECK_EQ(panelIp.load(), PHONE_IP);            // the newest session
        Panel c{OTHER_IP, 5004, 0xCCCC0000u, 0xC1};
        c.invite();
        settle();
        EXPECT_SENT("BY ctl>.4:5004", "OK ctl>.5:5004 t=C1");
        CHECK_EQ(rtpEvicted.load(), 1);
        CHECK_EQ(rtpRejected.load(), 0);
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(panelIp.load(), PANEL_IP);
        CHECK(tokenFor(b.ssrc) == nullptr);
        CHECK(logged("[midi] rtp session list full: ended 192.168.66.4's session (heard "));
        CHECK(logged(" with a BY for a new invitation"));
        c.invite(true);
        settle();
        EXPECT_SENT("OK data>.5:5005 t=C1");
        CHECK_EQ(peers.load(), 2);
        CHECK_EQ(panelIp.load(), OTHER_IP);
        // b's packets still under way get no BY of our own for 1 s; later ones do.
        b.ck0();
        settle();
        EXPECT_SENT();
        CHECK_EQ(rtpUnknown.load(), 1);
        g_now += FL_EXHIBIT_RECONNECT_QUIET_MS;
        b.ck0();
        settle();
        EXPECT_SENT("BY ctl>.4:5004");
        CHECK_EQ(g_nos, 0);
    }

    section("both slots taken, the least recently heard on the inviting address: it goes without a BY");
    {
        resetSessions();
        Panel a{PANEL_IP, 5004, 0xAAAA0002u, 0xA2};
        join(a);
        g_now += 1000;
        Panel b{PHONE_IP, 5004, 0xBBBB0002u, 0xB2};
        join(b);
        sentText();
        // The same device, another port and SSRC (a second app, or a reboot
        // onto another port): a BY to it would end the session this OK starts.
        Panel a2{PANEL_IP, 7004, 0xAAAA0004u, 0xA4};
        a2.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:7004 t=A4");
        CHECK_EQ(rtpEvicted.load(), 1);
        CHECK_EQ(peers.load(), 1);                     // b
        CHECK(logged(", no BY (same address as the invitation) for a new invitation"));
        CHECK_EQ(g_nos, 0);
    }

    section("a session unheard for 25 s ends with a BY; heartbeats and MIDI keep it; the panel invites again");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x11110008u, 0xD1};
        join(p);
        sentText();
        for (int i = 0; i < 6; ++i) {                  // heartbeats every 10 s: a minute up
            g_now += 10000;
            p.ck0();
            settle();
            EXPECT_SENT("CK1 data>.2:5005");
        }
        for (int i = 0; i < 6; ++i) {                  // MIDI only, every 20 s: two minutes up
            g_now += 20000;
            p.cc(25, (uint8_t)i);
            settle();
        }
        EXPECT_SENT();
        CHECK_EQ(rxCount.load(), 6);
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(rtpTimeouts.load(), 0);
        g_now += FL_EXHIBIT_SESSION_TIMEOUT_MS - 100;  // silent since the last MIDI: 24.9 s
        pass();
        EXPECT_SENT();
        CHECK_EQ(peers.load(), 1);
        passes(200);                                   // past 25 s
        EXPECT_SENT("BY ctl>.2:5004");
        CHECK_EQ(rtpTimeouts.load(), 1);
        CHECK_EQ(peers.load(), 0);
        CHECK_EQ(panelIp.load(), 0);
        CHECK(tokenFor(p.ssrc) == nullptr);
        CHECK(logged("[midi] rtp session of 192.168.66.2 silent for 250"));
        CHECK(logged(" ms: ended with a BY"));
        p.ck0();                                       // one of its packets under way: no second BY
        settle();
        EXPECT_SENT();
        g_now += 1000;                                 // the panel read the BY and invites again
        p.token = 0xD2;
        join(p);
        EXPECT_SENT("OK ctl>.2:5004 t=D2", "OK data>.2:5005 t=D2");
        CHECK_EQ(peers.load(), 1);
        CHECK_EQ(sessions.load(), 2);
    }

    section("a half-made session (control OK, no data invitation) ends after 25 s with a BY");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x1111000Au, 0xE1};
        p.invite();
        settle();
        EXPECT_SENT("OK ctl>.2:5004 t=E1");
        g_now += FL_EXHIBIT_SESSION_TIMEOUT_MS - 100;
        pass();
        EXPECT_SENT();
        passes(200);
        EXPECT_SENT("BY ctl>.2:5004");
        CHECK_EQ(rtpTimeouts.load(), 1);
        CHECK_EQ(peers.load(), 0);
        CHECK_EQ(sessions.load(), 0);
    }

    section("two sessions on one address: the silent one ends without a BY, the other stays");
    {
        resetSessions();
        Panel a{PANEL_IP, 5004, 0xAAAA0006u, 0xF1};
        join(a);
        Panel b{PANEL_IP, 7004, 0xBBBB0006u, 0xF2};
        join(b);
        EXPECT_SENT("OK ctl>.2:5004 t=F1", "OK data>.2:5005 t=F1", "OK ctl>.2:7004 t=F2", "OK data>.2:7005 t=F2");
        CHECK_EQ(peers.load(), 2);
        for (int i = 0; i < 2; ++i) {                  // b keeps its heartbeat, a is silent
            g_now += 10000;
            b.ck0();
            settle();
        }
        EXPECT_SENT("CK1 data>.2:7005", "CK1 data>.2:7005");
        CHECK_EQ(rtpTimeouts.load(), 0);
        g_now += 10000;                                // a silent for 30 s: it ends, without a BY
        b.ck0();
        settle();
        EXPECT_SENT("CK1 data>.2:7005");
        CHECK_EQ(rtpTimeouts.load(), 1);
        CHECK_EQ(peers.load(), 1);
        CHECK(logged(", no BY (another session on that address)"));
        b.ck0();                                       // b is still listed
        settle();
        EXPECT_SENT("CK1 data>.2:7005");
    }

    section("the panel's own BY ends its session (the stock library), and its record with it");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x1111000Cu, 0x1C};
        join(p);
        sentText();
        p.bye();
        settle();
        EXPECT_SENT();
        CHECK_EQ(peers.load(), 0);
        CHECK(tokenFor(p.ssrc) == nullptr);
        CHECK(logged("[midi] rtp session left (0)"));
    }

    section("reconnect panel (command 10) on the real library: a BY to the session, then no stranger BY for 1 s");
    {
        resetSessions();
        Panel p{PANEL_IP, 5004, 0x1111000Eu, 0x1E};
        join(p);
        sentText();
        host.mounted = true;
        passes(2);
        host.toDevice.push_back({0x4, 0xF0, 0x7D, 0x50});
        host.toDevice.push_back({0x4, 0x46, 0x10, 'R'});
        host.toDevice.push_back({0x6, 'C', 0xF7, 0x00});
        pass();
        EXPECT_SENT("BY ctl>.2:5004");
        CHECK_EQ(peers.load(), 0);
        CHECK(tokenFor(p.ssrc) == nullptr);
        CHECK(logged("[midi] pf monitor: command 10 (reconnect panel) ok: ended 1 RTP session, the panel invites again"));
        p.cc(24, 1);                                   // sent before it read the BY
        settle();
        EXPECT_SENT();
        g_now += 1000;
        p.token = 0x1F;                                // it invites again
        join(p);
        EXPECT_SENT("OK ctl>.2:5004 t=1F", "OK data>.2:5005 t=1F");
        CHECK_EQ(peers.load(), 1);
        host.mounted = false;
        passes(2);
    }

    CHECK_EQ(g_nos, 0);                                // never a NO, in any section
    std::printf("    (receiver feedback packets seen and left out: %d)\n", g_rs);
    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
