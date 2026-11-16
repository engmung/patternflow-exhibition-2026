// Host test for the exhibit MIDI bridge (src/fl_midi_bridge.h), no hardware.
// Built and run by tools/check_exhibit_bridge.py, which copies the real header
// with its AppleMIDI session specialisations cut out (exhibit_session_test.cpp
// runs them in the real library) and leaves empty stub headers for everything
// exhibit_stubs.h and the front ends below stand in for. See that script's
// docstring.
//
// The bridge task's real loop runs one pass at a time (a pass ends where the
// task reads its stack high-water mark) against a model USB host: a 16-packet
// FIFO the laptop drains every 1 ms frame while it reads. Every check is on
// the packets the laptop read, in order.

#include "exhibit_stubs.h"

// ── AppleMIDI / MIDI Library front ends ────────────────────────────────────
#define APPLEMIDI_LIBRARY_VERSION 0x030500
#define APPLEMIDI_NAMESPACE appleMidi
#define BEGIN_APPLEMIDI_NAMESPACE namespace appleMidi {
#define END_APPLEMIDI_NAMESPACE }
#define MIDI_NAMESPACE midi_stub
#define MIDI_CHANNEL_OMNI 0

namespace appleMidi {
typedef uint32_t ssrc_t;
enum Exception {
    BufferFullException, ParseException, UnexpectedParseException, TooManyParticipantsException,
    ListenerTimeOutException, ReceivedPacketsDropped, ParticipantNotFoundException,
};
struct DefaultSettings { static const uint8_t MaxNumberOfParticipants = 1; };
struct AppleMIDISettings {};
// The session as the bridge uses it: its callbacks, and the participants the
// library would hold (sendEndSession() sends each a BY and drops it, calling
// "disconnected" for each, as AppleMIDI 3.5.0 does).
template <class Udp, class Settings>
class AppleMIDISession {
public:
    AppleMIDISession(const char*, uint16_t) {}
    void setHandleConnected(void (*f)(const ssrc_t&, const char*)) { connected = f; }
    void setHandleDisconnected(void (*f)(const ssrc_t&)) { disconnected = f; }
    template <class F> void setHandleException(F) {}
    ssrc_t getSynchronizationSource() const { return 0x1234; }
    void sendEndSession() {
        while (!participants.empty()) {
            const ssrc_t s = participants.front();
            byes.push_back(s);
            participants.erase(participants.begin());
            if (disconnected) disconnected(s);
        }
    }
    void (*connected)(const ssrc_t&, const char*) = nullptr;
    void (*disconnected)(const ssrc_t&) = nullptr;
    std::vector<ssrc_t> participants;   // handshake done or not
    std::vector<ssrc_t> byes;           // BYs sent by sendEndSession(), in order
};
}  // namespace appleMidi

namespace midi_stub {
enum MidiType : uint8_t {
    InvalidType = 0x00, NoteOff = 0x80, NoteOn = 0x90, AfterTouchPoly = 0xA0, ControlChange = 0xB0,
    ProgramChange = 0xC0, AfterTouchChannel = 0xD0, PitchBend = 0xE0, Clock = 0xF8,
};
struct RtpMsg {
    MidiType type;
    uint8_t channel;   // 1..16
    uint8_t d1;
    uint8_t d2;
};
static std::deque<RtpMsg> rtpIn;   // what the panel sent, parsed
template <class Transport, class Settings>
class MidiInterface {
public:
    explicit MidiInterface(Transport&) {}
    void begin(int) {}
    void turnThruOff() {}
    bool read() {
        if (rtpIn.empty()) return false;
        cur_ = rtpIn.front();
        rtpIn.pop_front();
        return true;
    }
    MidiType getType() const { return cur_.type; }
    uint8_t getChannel() const { return cur_.channel; }
    uint8_t getData1() const { return cur_.d1; }
    uint8_t getData2() const { return cur_.d2; }
private:
    RtpMsg cur_{};
};
}  // namespace midi_stub

#define FL_EXHIBIT 1
#define FL_AP_MAX_CONN 3          // flowlocal_config.h's stand-in is empty; env:exhibit sets 3
#ifndef FL_EXHIBIT_AP_CHANNEL
#define FL_EXHIBIT_AP_CHANNEL 0   // and flowlocal_config.h's default: automatic channel
#endif
#include "src/fl_midi_bridge.h"

// ═══════════════════════════════════════════════════════════════════════════
using namespace FlMidiBridge;

static_assert(FL_EXHIBIT_KNOB_CC_FIRST == 24 && FL_EXHIBIT_KNOB_CC_LAST == 26 &&
              FL_EXHIBIT_KNOB_CC_CHANNEL == 1, "this test spells out CC 24-26 on channel 1");
static_assert(FL_EXHIBIT_PROGRAM_CC == 0 || (FL_EXHIBIT_PROGRAM_CC == 118 && FL_EXHIBIT_PROGRAM_CC_CHANNEL == 16),
              "this test spells out the mirror as BF 76 <n>");

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

// Expected-sequence pieces that exist only in some builds.
static const char* K(const char* s) { return FL_EXHIBIT_KNOB_RESEND ? s : ""; }   // knob re-send
static const char* M(const char* s) { return FL_EXHIBIT_PROGRAM_CC ? s : ""; }    // program mirror
// chm= before setup() has published this boot's plan: what the build alone says.
static const char* const CHM_BUILD = FL_EXHIBIT_AP_CHANNEL != 0 ? "fixed" : "auto";

static std::string seq(std::initializer_list<const char*> parts) {
    std::string out;
    for (const char* p : parts) {
        if (!*p) continue;
        if (!out.empty()) out += ", ";
        out += p;
    }
    return out;
}

static const midiEventPacket_t FILLER = {0x0F, 0xFE, 0, 0};   // active sensing, not shown

// PF monitor replies and acks the laptop read (their text), newest last.
static std::vector<std::string> g_replies;
static std::vector<std::string> g_acks;

// How the laptop sees SysEx: packets with CIN 0x4..0x7 (0x5 only with F7)
// carry it; any other status byte ends an unfinished one, which the laptop
// throws away. Shown as
//   PF-REPLY              a whole F0 7D 50 46 02 <printable text> F7 (text
//                         kept in g_replies)
//   PF-ACK                a whole F0 7D 50 46 03 <printable text> F7 (text
//                         kept in g_acks)
//   SYSEX-CUT(n)          n bytes from F0 on, ended by another message
//   SYSEX-OPEN(n)         n bytes from F0 on, nothing after them yet
//   SYSEX(hex)            any other whole SysEx
// and "(BAD SYSEX CIN)" / "(BAD PAD)" for a packet cut against USB-MIDI 1.0.
static std::string fmtGot() {
    std::string out;
    std::vector<uint8_t> sx;
    bool inSx = false;
    auto emit = [&](const std::string& s) {
        if (!out.empty()) out += ", ";
        out += s;
    };
    auto cut = [&](const char* how) {
        if (!inSx) return;
        emit(std::string(how) + "(" + std::to_string(sx.size()) + ")");
        inSx = false;
        sx.clear();
    };
    for (const midiEventPacket_t& p : host.got) {
        if (p.header == FILLER.header && p.byte1 == FILLER.byte1) continue;
        const uint8_t pcin = p.header & 0x0F;
        if (pcin == 0x4 || pcin == 0x6 || pcin == 0x7 || (pcin == 0x5 && p.byte1 == 0xF7)) {
            const int n = pcin == 0x4 ? 3 : pcin - 0x4;
            const uint8_t bytes[3] = {p.byte1, p.byte2, p.byte3};
            bool badCin = false;
            for (int k = 0; k < 3; ++k) {
                const uint8_t b = bytes[k];
                if (k >= n) {
                    if (b != 0) emit("(BAD PAD)");
                    continue;
                }
                const bool last = (k == n - 1);
                if (pcin != 0x4 && last != (b == 0xF7)) badCin = true;   // F7 only as an ending packet's last byte
                if (pcin == 0x4 && b == 0xF7) badCin = true;
                if (b == 0xF0) {
                    cut("SYSEX-CUT");
                    inSx = true;
                    sx.assign(1, b);
                    continue;
                }
                if (!inSx) {
                    emit("SYSEX-STRAY");
                    continue;
                }
                sx.push_back(b);
                if (b != 0xF7) continue;
                const bool pf = sx.size() >= 6 && sx[1] == 0x7D && sx[2] == 0x50 && sx[3] == 0x46 &&
                                (sx[4] == 0x02 || sx[4] == 0x03);
                if (pf) {
                    std::string text(sx.begin() + 5, sx.end() - 1);
                    bool printable = true;
                    for (char c : text) printable = printable && c >= 0x20 && c <= 0x7E;
                    const std::string what = sx[4] == 0x02 ? "PF-REPLY" : "PF-ACK";
                    emit(printable ? what : what + " (BAD TEXT)");
                    (sx[4] == 0x02 ? g_replies : g_acks).push_back(text);
                } else {
                    std::string hex;
                    for (uint8_t x : sx) {
                        char h[4];
                        std::snprintf(h, sizeof h, "%s%02X", hex.empty() ? "" : " ", x);
                        hex += h;
                    }
                    emit("SYSEX(" + hex + ")");
                }
                inSx = false;
                sx.clear();
            }
            if (badCin) emit("(BAD SYSEX CIN)");
            continue;
        }
        cut("SYSEX-CUT");
        char b[40];
        const uint8_t cin = p.byte1 >> 4;
        const bool two = (cin == 0xC || cin == 0xD);
        if (two) std::snprintf(b, sizeof b, "%02X %02X", p.byte1, p.byte2);
        else std::snprintf(b, sizeof b, "%02X %02X %02X", p.byte1, p.byte2, p.byte3);
        if (!out.empty()) out += ", ";
        out += b;
        if (p.header != cin) out += " (BAD CIN)";
        if (two && p.byte3 != 0) out += " (BAD PAD)";
    }
    cut("SYSEX-OPEN");
    return out;
}

// What the laptop read since the last call, all of it.
static void expectGot(const std::string& want, int line) {
    ++g_checks;
    host.frame();
    const std::string have = fmtGot();
    host.got.clear();
    if (have != want) {
        std::printf("  FAIL line %d: laptop read\n      [%s]\n    want\n      [%s]\n", line,
                    have.c_str(), want.c_str());
        ++g_failures;
    }
}
#define EXPECT_GOT(...) expectGot(seq({__VA_ARGS__}), __LINE__)

static void section(const char* name) { std::printf("- %s\n", name); }

// One pass of the bridge task, then the 1 ms it waits.
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
// Stamps are millis() | 1: on an even millisecond they lie 1 ms ahead. The
// checks that compare a stamp with the same pass's clock run on one.
static void evenNow() { g_now += g_now & 1u; }

static void panelCc(uint8_t ch, uint8_t cc, uint8_t v) {
    midi_stub::rtpIn.push_back({midi_stub::ControlChange, ch, cc, v});
}
static void panelPc(uint8_t ch, uint8_t program) {
    midi_stub::rtpIn.push_back({midi_stub::ProgramChange, ch, program, 0});
}
static void panelNote(uint8_t ch, uint8_t note, uint8_t velocity) {
    midi_stub::rtpIn.push_back({velocity ? midi_stub::NoteOn : midi_stub::NoteOff, ch, note, velocity});
}
static void laptopSends(uint8_t status, uint8_t d1, uint8_t d2) {
    host.toDevice.push_back({(uint8_t)(status >> 4), status, d1, d2});
}
static void resyncRequest() { laptopSends(0xBF, 119, 127); }
static void laptopPacket(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3) {
    host.toDevice.push_back({cin, b1, b2, b3});
}
// F0 7D 50 46 01 F7 as USB-MIDI packs it: CIN 0x4 with the first three
// bytes, CIN 0x7 with the last three.
static void statusRequest() {
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    laptopPacket(0x7, 0x46, 0x01, 0xF7);
}
// F0 7D 50 46 <cmd> <g1> <g2> F7: CIN 0x4, 0x4, then 0x6 with the last two.
static void monitorCommand(uint8_t cmd, uint8_t g1, uint8_t g2) {
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    laptopPacket(0x4, 0x46, cmd, g1);
    laptopPacket(0x6, g2, 0xF7, 0x00);
}
static void reconnectPanelCmd() { monitorCommand(0x10, 'R', 'C'); }
static void rebootPanelCmd() { monitorCommand(0x11, 'R', 'P'); }
static void rebootFlowLocalCmd() { monitorCommand(0x12, 'R', 'F'); }
// F0 7D 50 46 13 43 48 <c> F7: CIN 0x4, 0x4, then 0x7 with the last three.
static void channelCmd(uint8_t arg) {
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    laptopPacket(0x4, 0x46, 0x13, 'C');
    laptopPacket(0x7, 'H', arg, 0xF7);
}
// Command 13's choice in the NVS model, -1 when nothing was stored.
static int storedChannel() {
    const auto n = nvs.u8.find("fl_exhibit");
    if (n == nvs.u8.end()) return -1;
    const auto k = n->second.find("ap_ch");
    return k == n->second.end() ? -1 : k->second;
}
static std::string lastAck() { return g_acks.empty() ? std::string() : g_acks.back(); }

// A panel whose session is up: the library lists it, the bridge counts it.
static void panelSession(ssrc_t s, uint32_t ip) {
    acceptedTokens[0] = {s, 0x42, ip};
    rtpSession.participants.push_back(s);
    rtpSession.connected(s, "panel");
}

// A reply field's value, "<absent>" without the key.
static std::string field(const std::string& text, const char* key) {
    const std::string k = std::string(key) + "=";
    size_t at = 0;
    for (;;) {
        const size_t end = text.find(';', at);
        const std::string f = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (f.compare(0, k.size(), k) == 0) return f.substr(k.size());
        if (end == std::string::npos) return "<absent>";
        at = end + 1;
    }
}
// The keys of a reply, in order, joined by ','.
static std::string keys(const std::string& text) {
    std::string out;
    size_t at = 0;
    for (;;) {
        const size_t end = text.find(';', at);
        const std::string f = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!out.empty()) out += ",";
        out += f.substr(0, f.find('='));
        if (end == std::string::npos) return out;
        at = end + 1;
    }
}
static std::string lastReply() { return g_replies.empty() ? std::string() : g_replies.back(); }
static void checkField(const std::string& text, const char* key, const std::string& want, int line) {
    ++g_checks;
    const std::string have = field(text, key);
    if (have != want) {
        std::printf("  FAIL line %d: %s=%s, want %s\n      reply: %s\n", line, key, have.c_str(),
                    want.c_str(), text.c_str());
        ++g_failures;
    }
}
#define CHECK_FIELD(text, key, want) checkField(text, key, want, __LINE__)
static long long num(const std::string& s) {
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    return (end && *end == '\0' && !s.empty()) ? v : -999999;
}

// The laptop's monitor: a status request every everyMs while the device is
// on the bus, passes of the bridge task in between, for ms of model time.
static uint32_t g_nextAsk = 0;
static void monitorAsks(uint32_t ms, uint32_t everyMs = 2000) {
    const uint32_t end = g_now + ms;
    while ((int32_t)(g_now - end) < 0) {
        if (host.mounted && (int32_t)(g_now - g_nextAsk) >= 0) {
            statusRequest();
            g_nextAsk = g_now + everyMs;
        }
        pass();
    }
}

// Fill the FIFO so only `room` packets fit, and stop the laptop reading.
static void clogFifo(size_t room) {
    host.reading = false;
    while (host.fifo.size() + room < host.capacity) host.fifo.push_back(FILLER);
}
static void unclog() {
    host.reading = true;
    host.frame();
}

static void resetBridge() {
    host = HostModel{};
    midi_stub::rtpIn.clear();
    g_now += 10000;
    programOwed = false;
    mirrorOwed = false;
    knobOwed = 0;
    stalled = false;
    stallStartMs = lastFailMs = lastResendTryMs = lastResyncMs = 0;
    lastProgram.store(-1);
    lastProgramChannel.store(1);
    knobSeen.store(0);
    for (auto& v : knobValue) v.store(0);
    usbMounted.store(false);
    webResendRequested.store(false);
    for (std::atomic<uint32_t>* c : {&rxCount, &ignoredCount, &txCount, &droppedCount, &resendCount,
                                     &mirrorCount, &knobResendCount, &resyncRequests, &webResends,
                                     &usbInDiscarded, &sessions, &rtpGaps, &rtpRejected, &rtpTimeouts,
                                     &lastRxMs, &lastHeardMs, &panelIp, &statusRequests, &statusReplies,
                                     &statusUnanswered, &statusAbandoned, &commandsReceived,
                                     &commandsRefused, &acksSent, &acksLost, &panelReboots, &rtpUnknown,
                                     &rtpByeSent, &lastUnknownMs, &rtpReplaced, &rtpEvicted, &rtpRetryOks})
        c->store(0);
    channelMode.store((uint8_t)(FL_EXHIBIT_AP_CHANNEL != 0 ? FlApChannel::Mode::Fixed : FlApChannel::Mode::Auto));
    channelScored.store(false);
    for (uint32_t& s : channelScore) s = 0;
    nvs = NvsModel{};
    restartCmd = 0;
    restartNote = FlRecovery::Note::Command;
    peers.store(0);
    for (size_t i = 0; i < RtpSettings::MaxNumberOfParticipants; ++i) {
        peerSsrc[i] = 0;
        peerIp[i] = 0;
        acceptedTokens[i] = AcceptedToken{};
    }
    rtpSession.participants.clear();
    rtpSession.byes.clear();
    rtpReady.store(true);
    sysexIn.reset();
    commandGate.reset();
    replyOwed = false;
    replyAskedMs = lastReplyMs = 0;
    ackHead = ackCount = 0;
    sessionEnds = 0;
    restartAsked = false;
    restartAskedMs = restartAtMs = 0;
    for (bool& b : tooSoonAcked) b = false;
    lastSpreadMs = 0;
    byeQuiet = false;
    byeQuietSinceMs = 0;
    lastStranger = 0;
    lastStrangerByeMs = 0;
    rtpTiming.reset();
    rtpJitterMs.store(-1);
    rtpSpreadMs.store(-1);
    panelRebootBusy.store(false);
    panelRebootDone.store(false);
    panelRebootOk = false;
    panelRebootMsg[0] = '\0';
    apRunning.store(0);
    apStationCount.store(0);
    apChannelNow.store(-1);
    panelRssiEntry.store(0);
    wifi = WifiModel{};
    http = HttpModel{};
    g_apStarted = true;
    g_channel = 1;
    g_restarts = 0;
    g_taskCreateFails = false;
    g_replies.clear();
    g_acks.clear();
    g_log.clear();
    usbWatch.reset();
    usbReconnects.store(0);
    usbWatchPhase.store(0);
    bridgeWatch.reset();
    bridgeStalled.store(false);
    g_nextAsk = g_now;
}

// The laptop is plugged in and awake, and the panel has sent pattern 3 and
// K1-K3 = 64 / 70 / 80.
static void connectedWithKnobs() {
    resetBridge();
    host.mounted = true;
    passes(2);
    panelPc(1, 3);
    panelCc(1, 24, 64);
    panelCc(1, 25, 70);
    panelCc(1, 26, 80);
    pass();
    host.got.clear();
}

static std::string statusJson() {
    JsonDocument doc;
    appendStatus(doc);
    std::string out;
    serializeJson(doc["midi"], out);
    return out;
}
static std::string statusJsonAll() {          // the whole document: "restart" is at the top level
    JsonDocument doc;
    appendStatus(doc);
    std::string out;
    serializeJson(doc, out);
    return out;
}
static bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
// What the laptop read since the last call, as EXPECT_GOT shows it.
static std::string gotText() {
    host.frame();
    const std::string have = fmtGot();
    host.got.clear();
    return have;
}
// The task stops passing; loop() looks every 500 ms until it restarts
// FlowLocal (or maxLooks). Returns the looks it took.
static int loopLooksWhileHung(int maxLooks = 20) {
    const int before = g_restarts;
    int looks = 0;
    while (g_restarts == before && looks < maxLooks) {
        g_now += 500;
        watchBridge();
        ++looks;
    }
    return looks;
}

int main() {
    std::printf("exhibit bridge host test: knob re-send %s, program mirror %s\n",
                FL_EXHIBIT_KNOB_RESEND ? "on" : "off", FL_EXHIBIT_PROGRAM_CC ? "on" : "off");
    g_resetReason = ESP_RST_POWERON;
    bootBegin();                             // first thing in setup()
    CHECK_EQ(bootInfo.boots, 1);
    beginUsb();
    CHECK_EQ(g_taskCreated, 1);              // the task starts with USB ...
    CHECK(g_taskNames.size() == 1 && g_taskNames[0] == "fl-midi");
    CHECK(!rtpReady.load());                 // ... and leaves RTP alone until begin()
    CHECK(logged("[midi] usb watch: soft reconnect after 3 status requests over 8 s without a reply, "
                 "restart if none gets out 10 s later"));

    section("before begin() (the hotspot's boot silence): status answered, RTP not read, no reconnect");
    host.mounted = true;
    passes(2);
    CHECK_EQ(g_wdtAdds, 1);                  // the task subscribed to the task watchdog, once
    CHECK(taskWdtOn.load());
    CHECK(logged("[midi] bridge watchdog: task watchdog on; loop() restarts after 3000 ms without a pass"));
    CHECK_EQ(g_wdtFeeds, 2);                 // and fed it every pass
    midi_stub::rtpIn.push_back({midi_stub::ControlChange, 1, 24, 5});
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_EQ(midi_stub::rtpIn.size(), 1);    // not read
    CHECK_EQ(rxCount.load(), 0);
    {
        const std::string r = lastReply();
        CHECK_FIELD(r, "ap", "0");
        CHECK_FIELD(r, "ch", "");
        CHECK_FIELD(r, "chm", CHM_BUILD);
        CHECK_FIELD(r, "cl", "0");
        CHECK_FIELD(r, "ps", "0");
    }
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=0;msg=no RTP yet: the hotspot is still starting");
    midi_stub::rtpIn.clear();

    begin();
    CHECK_EQ(g_taskCreated, 1);
    CHECK(rtpReady.load());
    CHECK(logged("[midi] pf monitor: commands 10 reconnect panel, 11 reboot panel (POST /api/wifi/reboot), "
                 "12 reboot FlowLocal, 13 Wi-Fi channel (kept in NVS, then a restart), each at most once per 3 s"));
    CHECK(logged("[midi] rtp sessions: never refused (the one heard from least recently makes room), "
                 "ended after 25 s unheard, a repeated invitation gets its OK again"));

    section("USB mounts before the panel has sent anything: nothing goes out");
    resetBridge();
    host.mounted = true;
    passes(3);
    EXPECT_GOT();
    CHECK(!programOwed && !mirrorOwed && knobOwed == 0);

    section("the panel's messages pass through as they came; a fresh program owes no knobs");
    panelPc(1, 3);
    panelCc(1, 24, 64);
    panelCc(1, 25, 70);
    panelCc(1, 26, 80);
    panelNote(1, 60, 127);
    panelNote(1, 60, 0);
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), "B0 18 40", "B0 19 46", "B0 1A 50", "90 3C 7F", "80 3C 00");
    passes(600);
    EXPECT_GOT();
    CHECK_EQ(resendCount.load(), 0);
    CHECK_EQ(knobResendCount.load(), 0);
    CHECK_EQ(txCount.load(), FL_EXHIBIT_PROGRAM_CC ? 7 : 6);

    section("a pattern switch from the panel: program and mirror only");
    panelPc(1, 4);
    pass();
    EXPECT_GOT("C0 04", M("BF 76 04"));
    passes(600);
    EXPECT_GOT();

    section("only CC 24-26 on channel 1 are kept");
    panelCc(1, 27, 5);     // K4's number (the exhibit panel never sends it)
    panelCc(2, 24, 9);
    panelCc(16, 25, 9);
    panelCc(1, 23, 9);
    panelCc(1, 118, 9);
    panelCc(1, 119, 9);
    pass();
    EXPECT_GOT("B0 1B 05", "B1 18 09", "BF 19 09", "B0 17 09", "B0 76 09", "B0 77 09");
    if (FL_EXHIBIT_KNOB_RESEND) {
        CHECK_EQ(knobSeen.load(), 7);
        CHECK_EQ(knobValue[0].load(), 64);
        CHECK_EQ(knobValue[1].load(), 70);
        CHECK_EQ(knobValue[2].load(), 80);
    } else {
        CHECK_EQ(knobSeen.load(), 0);
    }

    section("resync request from Live: program, mirror, then K1-K3");
    resyncRequest();
    pass();
    EXPECT_GOT("C0 04", M("BF 76 04"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    CHECK_EQ(resyncRequests.load(), 1);
    CHECK_EQ(resendCount.load(), 1);
    CHECK_EQ(knobResendCount.load(), FL_EXHIBIT_KNOB_RESEND ? 3 : 0);
    passes(600);
    EXPECT_GOT();

    section("a second request within 250 ms counts once; after that it re-sends again");
    g_now += 300;
    resyncRequest();
    pass();
    resyncRequest();
    passes(100);
    EXPECT_GOT("C0 04", M("BF 76 04"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    g_now += 300;
    resyncRequest();
    pass();
    EXPECT_GOT("C0 04", M("BF 76 04"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    CHECK_EQ(resyncRequests.load(), 4);   // the merged one is counted, not answered
    CHECK_EQ(resendCount.load(), 3);

    section("the status page button: the same, with the knob count in its answer");
    CHECK(requestResend());
    CHECK_EQ(knownKnobs(), FL_EXHIBIT_KNOB_RESEND ? 3 : 0);
    pass();
    EXPECT_GOT("C0 04", M("BF 76 04"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    CHECK_EQ(webResends.load(), 1);

    section("a knob the panel has not sent is skipped");
    resetBridge();
    host.mounted = true;
    passes(2);
    panelPc(1, 2);
    panelCc(1, 25, 10);
    pass();
    EXPECT_GOT("C0 02", M("BF 76 02"), "B0 19 0A");
    CHECK(requestResend());
    pass();
    EXPECT_GOT("C0 02", M("BF 76 02"), K("B0 19 0A"));
    CHECK_EQ(knobResendCount.load(), FL_EXHIBIT_KNOB_RESEND ? 1 : 0);
    {
        const std::string s = statusJson();
        if (FL_EXHIBIT_KNOB_RESEND) {
            CHECK(has(s, "\"knob_resend\":\"CC 24-26 ch 1 value=last\""));
            CHECK(has(s, "\"knob_values\":[null,10,null]"));
            CHECK(has(s, "\"knob_resends\":1"));
        } else {
            CHECK(has(s, "\"knob_resend\":\"off\""));
            CHECK(has(s, "\"knob_values\":[]"));
            CHECK(has(s, "\"knob_resends\":0"));
        }
        if (FL_EXHIBIT_PROGRAM_CC) CHECK(has(s, "\"program_mirror\":\"CC 118 ch 16 value=program\""));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }

    section("knob values but no program yet: nothing is re-sent");
    resetBridge();
    host.mounted = true;
    passes(2);
    panelCc(1, 24, 30);
    pass();
    EXPECT_GOT("B0 18 1E");
    CHECK(!requestResend());
    resyncRequest();
    passes(300);
    EXPECT_GOT();
    CHECK(!programOwed && !mirrorOwed && knobOwed == 0);
    panelPc(1, 5);   // fresh: nothing was owed any more
    passes(300);
    EXPECT_GOT("C0 05", M("BF 76 05"));
    CHECK(requestResend());
    pass();
    EXPECT_GOT("C0 05", M("BF 76 05"), K("B0 18 1E"));

    section("USB gone while a knob turns, then back: program, mirror, newest knob values");
    connectedWithKnobs();
    host.mounted = false;
    pass();
    panelCc(1, 24, 100);
    pass();
    CHECK_EQ(droppedCount.load(), 1);
    host.mounted = true;
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 64"), K("B0 19 46"), K("B0 1A 50"));

    section("laptop sleeps and wakes (suspend / resume): the same");
    host.suspended = true;
    pass();
    host.suspended = false;
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 64"), K("B0 19 46"), K("B0 1A 50"));

    section("a dropped program from the panel: re-sent later with its mirror and the knobs");
    connectedWithKnobs();
    clogFifo(0);
    panelPc(1, 5);
    panelCc(1, 26, 90);   // dropped too, but kept
    pass();
    CHECK_EQ(droppedCount.load(), 2);
    CHECK(programOwed);
    unclog();
    host.got.clear();   // the filler
    passes(300);
    EXPECT_GOT("C0 05", M("BF 76 05"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 5A"));
    CHECK_EQ(resendCount.load(), 1);
    passes(600);
    EXPECT_GOT();

    section("a fresh program pays an owed one; the knob values still follow it");
    connectedWithKnobs();
    resyncRequest();
    panelPc(1, 6);   // read in the same pass, after the request
    pass();
    EXPECT_GOT("C0 06", M("BF 76 06"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    CHECK_EQ(resendCount.load(), 0);
    CHECK_EQ(knobResendCount.load(), FL_EXHIBIT_KNOB_RESEND ? 3 : 0);
    passes(600);
    EXPECT_GOT();

#if FL_EXHIBIT_PROGRAM_CC
    section("a forwarded program whose mirror was dropped: the mirror alone, no knobs");
    connectedWithKnobs();
    clogFifo(1);
    panelPc(1, 2);
    pass();
    CHECK(mirrorOwed && !programOwed && knobOwed == 0);
    unclog();
    passes(300);
    EXPECT_GOT("C0 02", "BF 76 02");
    CHECK_EQ(knobResendCount.load(), 0);
    passes(600);
    EXPECT_GOT();
#endif

    section("room for only part of a re-send: the rest goes later, in order, with newer values");
    connectedWithKnobs();
    // Room for the program, the mirror and K1, and nothing after them.
    clogFifo((FL_EXHIBIT_PROGRAM_CC ? 2 : 1) + (FL_EXHIBIT_KNOB_RESEND ? 1 : 0));
    resyncRequest();
    pass();
    CHECK(!programOwed && !mirrorOwed);
    CHECK_EQ(knobOwed, FL_EXHIBIT_KNOB_RESEND ? 6 : 0);
    panelCc(1, 25, 71);   // K2 moves meanwhile: the FIFO is full, dropped, kept
    pass();
    unclog();
    passes(300);
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 47"), K("B0 1A 50"));
    CHECK_EQ(resendCount.load(), 1);
    CHECK_EQ(knobResendCount.load(), FL_EXHIBIT_KNOB_RESEND ? 3 : 0);
    CHECK_EQ(knobOwed, 0);
    passes(600);
    EXPECT_GOT();

    section("a program re-send that does not fit waits whole: nothing of it goes out early");
    connectedWithKnobs();
    clogFifo(0);
    resyncRequest();
    pass();
    CHECK(programOwed);
    passes(1000);   // the laptop still reads nothing
    CHECK(programOwed);
    unclog();
    host.got.clear();
    passes(300);
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));

    section("other messages from the laptop are discarded");
    connectedWithKnobs();
    laptopSends(0xB0, 24, 1);      // Live's Remote echo of a mapped parameter
    laptopSends(0xBF, 118, 3);     // the mirror echoed back
    laptopSends(0xBF, 119, 0);     // a resync request needs value > 0
    passes(300);
    EXPECT_GOT();
    CHECK_EQ(usbInDiscarded.load(), 3);
    CHECK_EQ(resyncRequests.load(), 0);

    // ── PF monitor status request / reply ──────────────────────────────────
    const std::string kn = FL_EXHIBIT_KNOB_RESEND ? "64,71,80" : "";

    section("PF monitor: one whole reply, after the panel's messages of the same pass");
    connectedWithKnobs();                   // pattern 3, K1-K3 = 64 / 70 / 80
    peers.store(1);
    sessions.store(2);
    panelIp.store(0x0242A8C0u);             // 192.168.66.2
    rtpGaps.store(3);
    lastHeardMs.store((g_now - 1500) | 1);
    apRunning.store(1);
    apStationCount.store(2);
    apChannelNow.store(11);
    panelRssiEntry.store(((uint64_t)0x0242A8C0u << 32) | (uint32_t)(int32_t)-47);
    statusRequest();
    panelCc(1, 25, 71);                     // read in the same pass as the request
    evenNow();
    pass();
    EXPECT_GOT("B0 19 47", "PF-REPLY");
    CHECK_EQ(g_replies.size(), 1);
    {
        const std::string r = lastReply();
        std::printf("    reply text (%u bytes): %s\n", (unsigned)r.size(), r.c_str());
        CHECK(r.size() <= FlPfMonitor::TEXT_MAX);
        CHECK(keys(r) == "v,fw,up,ap,ch,chm,cl,cm,ps,pip,prs,hd,ra,lp,kn,jt,jm,rx,tx,dr,se,to,rj,gp,rr,ud,sr,hi,"
                         "rsn,bc,why,ur,cs");
        CHECK_FIELD(r, "v", "1");
        CHECK_FIELD(r, "fw", "flx-mon3");
        CHECK_FIELD(r, "rsn", "poweron");
        CHECK_FIELD(r, "bc", "1");
        CHECK_FIELD(r, "why", "");
        CHECK_FIELD(r, "ur", "0");
        CHECK(num(field(r, "up")) >= (long long)(g_now / 1000) - 1 && num(field(r, "up")) <= (long long)(g_now / 1000));
        CHECK_FIELD(r, "ap", "1");
        CHECK_FIELD(r, "ch", "11");
        CHECK_FIELD(r, "chm", CHM_BUILD);
        CHECK_FIELD(r, "jt", "-1");
        CHECK_FIELD(r, "jm", "-1");
        CHECK_FIELD(r, "cl", "2");
        CHECK_FIELD(r, "cm", "3");
        CHECK_FIELD(r, "ps", "1");
        CHECK_FIELD(r, "pip", "192.168.66.2");
        CHECK_FIELD(r, "prs", "-47");
        CHECK(num(field(r, "hd")) >= 1499 && num(field(r, "hd")) <= 1510);
        CHECK_FIELD(r, "ra", "0");          // the CC above, in the same pass
        CHECK_FIELD(r, "lp", "3");
        CHECK_FIELD(r, "kn", kn);
        CHECK_FIELD(r, "rx", "5");
        CHECK_FIELD(r, "tx", std::to_string(txCount.load()));   // the reply is not counted in tx
        CHECK_EQ(txCount.load(), FL_EXHIBIT_PROGRAM_CC ? 6 : 5);
        CHECK_FIELD(r, "dr", "0");
        CHECK_FIELD(r, "se", "2");
        CHECK_FIELD(r, "to", "0");
        CHECK_FIELD(r, "rj", "0");
        CHECK_FIELD(r, "gp", "3");
        CHECK_FIELD(r, "rr", "0");
        CHECK_FIELD(r, "ud", "0");
        CHECK_FIELD(r, "sr", "1");
        CHECK_FIELD(r, "hi", "181234");
    }
    CHECK_EQ(statusRequests.load(), 1);
    CHECK_EQ(statusReplies.load(), 1);
    CHECK_EQ(statusUnanswered.load(), 0);
    CHECK_EQ(statusAbandoned.load(), 0);
    CHECK_EQ(usbInDiscarded.load(), 0);

    section("PF monitor: at most one reply per 200 ms; the others are counted, not answered");
    g_now += 100;
    statusRequest();
    pass();
    EXPECT_GOT();
    CHECK_EQ(statusUnanswered.load(), 1);
    g_now += 150;
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "sr", "2");
    g_now += 300;
    statusRequest();
    statusRequest();   // same pass
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_EQ(statusRequests.load(), 5);
    CHECK_EQ(statusReplies.load(), 3);
    CHECK_EQ(statusUnanswered.load(), 2);

    section("PF monitor: a resync request with a status request: the re-send first, then the reply");
    connectedWithKnobs();
    statusRequest();
    resyncRequest();
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY");
    CHECK_FIELD(lastReply(), "rr", "1");

    section("PF monitor: a reply waits behind an owed re-send, then follows it");
    connectedWithKnobs();
    clogFifo(0);
    resyncRequest();
    statusRequest();
    evenNow();
    pass();
    CHECK(programOwed && replyOwed);
    unclog();
    host.got.clear();   // the filler
    passes(300);
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY");
    CHECK_EQ(statusAbandoned.load(), 0);

    section("PF monitor: an owed re-send that never fits: the reply is given up, none of it sent");
    connectedWithKnobs();
    clogFifo(0);
    resyncRequest();
    statusRequest();
    passes(1100);
    CHECK(!replyOwed);
    CHECK_EQ(statusAbandoned.load(), 1);
    CHECK_EQ(statusReplies.load(), 0);
    unclog();
    host.got.clear();
    passes(300);
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));

    section("PF monitor: nobody reads: given up after 20 ms; the next message ends it for the laptop");
    connectedWithKnobs();
    host.reading = false;
    statusRequest();
    {
        const uint32_t t0 = g_now;
        pass();
        CHECK(g_now - t0 <= FL_EXHIBIT_MON_WRITE_MS + 2);
    }
    CHECK_EQ(statusAbandoned.load(), 1);
    CHECK_EQ(statusReplies.load(), 0);
    unclog();
    panelCc(1, 24, 1);
    pass();
    EXPECT_GOT("SYSEX-CUT(48)", "B0 18 01");   // the 16 packets the FIFO took
    g_now += 250;
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "sr", "1");

    section("PF monitor: other SysEx is discarded and counted packet by packet, never answered");
    connectedWithKnobs();
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);   // the reply's shape (command 02)
    laptopPacket(0x7, 0x46, 0x02, 0xF7);
    laptopPacket(0x4, 0xF0, 0x7E, 0x7F);   // universal identity request
    laptopPacket(0x7, 0x06, 0x01, 0xF7);
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);   // the request with one byte more
    laptopPacket(0x4, 0x46, 0x01, 0x00);
    laptopPacket(0x5, 0xF7, 0x00, 0x00);
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);   // longer than the 8-byte buffer
    laptopPacket(0x4, 0x46, 0x01, 0x00);
    laptopPacket(0x4, 0x00, 0x00, 0x00);
    laptopPacket(0x4, 0x00, 0x00, 0x00);
    laptopPacket(0x6, 0x00, 0xF7, 0x00);
    passes(3);
    EXPECT_GOT();
    CHECK_EQ(usbInDiscarded.load(), 12);
    CHECK_EQ(statusRequests.load(), 0);
    CHECK_EQ(statusReplies.load(), 0);

    section("PF monitor: a request cut short by the resync request: the resync as before, the request discarded");
    connectedWithKnobs();
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    resyncRequest();
    laptopPacket(0x7, 0x46, 0x01, 0xF7);
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));
    CHECK_EQ(resyncRequests.load(), 1);
    CHECK_EQ(usbInDiscarded.load(), 2);
    CHECK_EQ(statusRequests.load(), 0);

    section("PF monitor: a real-time byte inside the request does not cut it");
    connectedWithKnobs();
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    laptopPacket(0xF, 0xF8, 0x00, 0x00);   // MIDI clock: discarded as always
    laptopPacket(0x7, 0x46, 0x01, 0xF7);
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "ud", "1");

    section("PF monitor: a request half read when USB goes away is forgotten");
    connectedWithKnobs();
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    pass();
    host.mounted = false;
    pass();
    host.mounted = true;
    laptopPacket(0x7, 0x46, 0x01, 0xF7);
    pass();
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"));   // the remount's re-send
    CHECK_EQ(statusRequests.load(), 0);
    CHECK_EQ(usbInDiscarded.load(), 1);

    section("PF monitor: before anything is known");
    resetBridge();
    host.mounted = true;
    passes(2);
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    {
        const std::string r = lastReply();
        CHECK_FIELD(r, "ap", "0");          // not polled yet: down, as before FlWifi::begin()
        CHECK_FIELD(r, "ch", "");
        CHECK_FIELD(r, "chm", CHM_BUILD);
        CHECK_FIELD(r, "cl", "0");
        CHECK_FIELD(r, "cm", "3");
        CHECK_FIELD(r, "jt", "-1");
        CHECK_FIELD(r, "jm", "-1");
        CHECK_FIELD(r, "ps", "0");
        CHECK_FIELD(r, "pip", "");
        CHECK_FIELD(r, "prs", "");
        CHECK_FIELD(r, "hd", "-1");
        CHECK_FIELD(r, "ra", "-1");
        CHECK_FIELD(r, "lp", "-1");
        CHECK_FIELD(r, "kn", FL_EXHIBIT_KNOB_RESEND ? "-,-,-" : "");
        CHECK_FIELD(r, "rx", "0");
        CHECK_FIELD(r, "sr", "1");
    }
    {   // an age past 24.8 days (2^31 ms) is still an age, as up to 49.7 days
        lastRxMs.store((g_now - 3000000000u) | 1);
        CHECK(ageMs(lastRxMs) >= 3000000000 && ageMs(lastRxMs) <= 3000000001);   // +1: an odd stamp
        lastRxMs.store(0);
    }

    section("PF monitor: pollHotspot gives the stations and the RSSI of the panel's own station");
    resetBridge();
    host.mounted = true;
    passes(2);
    wifi.stations = {{{2, 0, 0, 0, 0, 1}, -70, 0x0342A8C0u},    // a phone, .3
                     {{2, 0, 0, 0, 0, 2}, -52, 0x0242A8C0u},    // the panel, .2
                     {{2, 0, 0, 0, 0, 3}, -80, 0}};             // no lease yet
    panelIp.store(0x0242A8C0u);
    g_channel = 6;
    pollHotspot();
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "ap", "1");
    CHECK_FIELD(lastReply(), "ch", "6");
    CHECK_FIELD(lastReply(), "cl", "3");
    CHECK_FIELD(lastReply(), "pip", "192.168.66.2");
    CHECK_FIELD(lastReply(), "prs", "-52");
    g_now += 300;
    panelIp.store(0x0342A8C0u);   // the session moved; no poll since: no RSSI for it yet
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "pip", "192.168.66.3");
    CHECK_FIELD(lastReply(), "prs", "");
    pollHotspot();
    g_now += 300;
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "prs", "-70");
    panelIp.store(0);
    pollHotspot();
    CHECK_EQ(panelRssiEntry.load(), 0);
    g_apStarted = false;    // the AP's boot silence: not started yet
    pollHotspot();
    CHECK_EQ(apRunning.load(), 0);
    CHECK_EQ(apStationCount.load(), 0);
    CHECK_EQ(apChannelNow.load(), -1);

    section("PF monitor: /api/status");
    {
        const std::string s = statusJson();
        CHECK(has(s, "\"status_request\":\"F0 7D 50 46 01 F7\""));
        CHECK(has(s, "\"status_fw\":\"flx-mon3\""));
        CHECK(has(s, "\"status_requests\":3"));
        CHECK(has(s, "\"status_replies\":3"));
        CHECK(has(s, "\"status_unanswered\":0"));
        CHECK(has(s, "\"status_abandoned\":0"));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }

    // ── PF monitor commands ────────────────────────────────────────────────

    section("command 10 reconnect panel: a BY to every session the library holds, then the ack");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    CHECK_EQ(peers.load(), 1);
    CHECK_EQ(panelIp.load(), 0x0242A8C0u);
    rtpSession.participants.push_back(0xBBBB0004u);   // another one, handshake not done
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=1;msg=ended 2 RTP sessions, the panel invites again");
    CHECK(rtpSession.byes.size() == 2 && rtpSession.byes[0] == 0xAAAA0002u && rtpSession.byes[1] == 0xBBBB0004u);
    CHECK(rtpSession.participants.empty());
    CHECK_EQ(peers.load(), 0);
    CHECK_EQ(panelIp.load(), 0);
    CHECK_EQ(commandsReceived.load(), 1);
    CHECK_EQ(commandsRefused.load(), 0);
    CHECK_EQ(acksSent.load(), 1);
    CHECK_EQ(usbInDiscarded.load(), 0);
    CHECK_EQ(txCount.load(), FL_EXHIBIT_PROGRAM_CC ? 5 : 4);   // acks are not MIDI messages
    CHECK(logged("[midi] pf monitor: command 10 (reconnect panel) ok: ended 2 RTP sessions"));
    // The panel's packets still under way get no BY of their own for a while.
    CHECK(!shouldByeStranger(0xAAAA0002u));
    CHECK_EQ(rtpByeSent.load(), 0);
    g_now += FL_EXHIBIT_RECONNECT_QUIET_MS;
    CHECK(shouldByeStranger(0xAAAA0002u));
    CHECK_EQ(rtpByeSent.load(), 1);

    section("command 10: no stranger after it, and 24.8 days later a stranger still gets its BY");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    g_now += 0x80000000u + FL_EXHIBIT_RECONNECT_QUIET_MS;
    CHECK(shouldByeStranger(0xAAAA0003u));
    CHECK_EQ(rtpByeSent.load(), 1);

    section("command 10 with no session, and again within 3 s: refused with a reason");
    connectedWithKnobs();
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=0;msg=no RTP session to end");
    g_now += 1000;
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=0;msg=too soon: once per 3 s, wait 2 s");
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    g_now += 2000;                                     // 3 s after the first
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=1;msg=ended 1 RTP session, the panel invites again");
    CHECK_EQ(commandsReceived.load(), 3);
    CHECK_EQ(commandsRefused.load(), 2);
    CHECK_EQ(acksSent.load(), 3);

    section("a command repeated in a loop: one refusal ack a second, the rest only counted");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    for (int i = 0; i < 5; ++i) reconnectPanelCmd();   // 15 packets: one pass reads them all
    pass();
    EXPECT_GOT("PF-ACK", "PF-ACK");
    CHECK(g_acks.size() == 2 && g_acks[0] == "cmd=10;ok=1;msg=ended 1 RTP session, the panel invites again");
    CHECK(lastAck() == "cmd=10;ok=0;msg=too soon: once per 3 s, wait 3 s");
    CHECK_EQ(commandsReceived.load(), 5);
    CHECK_EQ(commandsRefused.load(), 4);
    CHECK_EQ(acksSent.load(), 2);
    g_now += 1000;
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=10;ok=0;msg=too soon: once per 3 s, wait 2 s");

    section("not commands: another command's guard, lowercase guard, unknown byte, no guard");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    monitorCommand(0x10, 'R', 'P');
    monitorCommand(0x12, 'r', 'f');
    monitorCommand(0x13, 'R', 'C');
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);
    laptopPacket(0x7, 0x46, 0x12, 0xF7);
    passes(3);
    EXPECT_GOT();
    CHECK_EQ(usbInDiscarded.load(), 11);
    CHECK_EQ(commandsReceived.load(), 0);
    CHECK(rtpSession.byes.empty());
    CHECK_EQ(peers.load(), 1);
    passes(1000);
    CHECK_EQ(g_restarts, 0);

    section("an ack and a status reply in one pass: the ack first, each whole");
    connectedWithKnobs();
    statusRequest();
    reconnectPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK", "PF-REPLY");
    CHECK(lastAck() == "cmd=10;ok=0;msg=no RTP session to end");

    section("an ack waits behind an owed re-send, then follows it");
    connectedWithKnobs();
    clogFifo(0);
    resyncRequest();
    reconnectPanelCmd();
    evenNow();
    pass();
    CHECK(programOwed);
    CHECK_EQ(ackCount, 1);
    unclog();
    host.got.clear();   // the filler
    passes(300);
    EXPECT_GOT("C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-ACK");
    CHECK_EQ(acksLost.load(), 0);

    section("command 11 reboot panel: its own task POSTs /api/wifi/reboot, the ack follows the answer");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    {
        const size_t tasksBefore = g_taskNames.size();
        http.answer = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true,\"rebooting\":true}";
        rebootPanelCmd();
        pass();
        EXPECT_GOT();                                  // nothing until the request is done
        CHECK(g_taskNames.size() == tasksBefore + 1 && g_taskNames.back() == "fl-pf-http");
        CHECK(panelRebootBusy.load());
        CHECK_EQ(http.connects, 0);
        runPanelReboot();                              // what that task does
        CHECK_EQ(http.connects, 1);
        CHECK_EQ(http.ip, 0x0242A8C0u);
        CHECK_EQ(http.port, 80);
        CHECK_EQ(http.timeout, 2000);
        CHECK(http.got == "POST /api/wifi/reboot HTTP/1.1\r\nHost: 192.168.66.2\r\nContent-Length: 0\r\n"
                          "Connection: close\r\n\r\n");
        pass();
        EXPECT_GOT("PF-ACK");
        CHECK(lastAck() == "cmd=11;ok=1;msg=panel 192.168.66.2 is rebooting");
        CHECK(!panelRebootBusy.load());
        CHECK_EQ(panelReboots.load(), 1);
        CHECK(logged("[midi] pf monitor: POST http://192.168.66.2/api/wifi/reboot: panel 192.168.66.2 is rebooting"));
        CHECK(rtpSession.byes.empty());                // the session is left to the panel's restart
        CHECK_EQ(peers.load(), 1);
    }

    section("command 11: one at a time; HTTP 404; a panel that never answers; no connection");
    connectedWithKnobs();
    panelSession(0xAAAA0002u, 0x0242A8C0u);
    rebootPanelCmd();
    pass();
    CHECK(panelRebootBusy.load());
    g_now += 3000;
    rebootPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=busy: the last reboot request is still running");
    http.answer = "HTTP/1.1 404 Not Found\r\n\r\n";
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=panel 192.168.66.2 answered HTTP 404");
    g_now += 3000;
    http = HttpModel{};
    http.closeAfter = false;                           // connects, then says nothing
    rebootPanelCmd();
    pass();
    {
        const uint32_t t0 = g_now;
        runPanelReboot();
        CHECK(g_now - t0 >= 2000 && g_now - t0 <= 2010);
    }
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=no HTTP answer from panel 192.168.66.2");
    g_now += 3000;
    http = HttpModel{};
    http.accept = false;
    rebootPanelCmd();
    pass();
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=no connection to panel 192.168.66.2");
    CHECK_EQ(panelReboots.load(), 0);
    CHECK_EQ(commandsRefused.load(), 4);

    section("command 11 with no session: the one hotspot station with a lease, else nothing is sent");
    connectedWithKnobs();
    wifi.stations = {{{2, 0, 0, 0, 0, 9}, -60, 0x0942A8C0u},   // .9
                     {{2, 0, 0, 0, 0, 8}, -70, 0}};            // no lease yet
    http.answer = "HTTP/1.1 200 OK\r\n\r\n";
    rebootPanelCmd();
    pass();
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=1;msg=panel 192.168.66.9 is rebooting");
    CHECK_EQ(http.ip, 0x0942A8C0u);
    wifi.stations.push_back({{2, 0, 0, 0, 0, 7}, -65, 0x0742A8C0u});
    g_now += 3000;
    http = HttpModel{};
    rebootPanelCmd();
    pass();
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=no panel session and 2 devices on the hotspot");
    CHECK_EQ(http.connects, 0);
    wifi.stations.clear();
    g_now += 3000;
    rebootPanelCmd();
    pass();
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=no panel session and no device on the hotspot");
    g_apStarted = false;
    g_now += 3000;
    rebootPanelCmd();
    pass();
    runPanelReboot();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=no panel session and the hotspot is not up");
    g_apStarted = true;
    g_taskCreateFails = true;
    g_now += 3000;
    rebootPanelCmd();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=11;ok=0;msg=could not start the request (no memory)");
    CHECK(!panelRebootBusy.load());
    g_taskCreateFails = false;
    CHECK_EQ(http.connects, 0);

    section("command 12 reboot FlowLocal: the ack goes out, the restart 300 ms after it");
    connectedWithKnobs();
    rebootFlowLocalCmd();
    evenNow();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=12;ok=1;msg=restarting: USB back in seconds, hotspot in about 13 s");
    CHECK_EQ(g_restarts, 0);
    passes(FL_EXHIBIT_MON_RESTART_MS - 5);
    CHECK_EQ(g_restarts, 0);
    passes(10);
    CHECK_EQ(g_restarts, 1);
    CHECK(logged("[midi] pf monitor: restarting FlowLocal (command 12)"));
    CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::Command);   // the next boot says why=cmd12 ...
    CHECK_EQ(bootRecord.recoveries, 0);                                // ... and it is no automatic restart
    passes(3000);
    CHECK_EQ(g_restarts, 1);

    section("command 12 with nobody reading: the ack is given up, the restart still comes");
    connectedWithKnobs();
    host.reading = false;
    rebootFlowLocalCmd();
    pass();
    CHECK_EQ(acksLost.load(), 1);
    CHECK_EQ(acksSent.load(), 0);
    passes(FL_EXHIBIT_MON_RESTART_MS + 5);
    CHECK_EQ(g_restarts, 1);
    host = HostModel{};

    section("command 12 behind a re-send that never fits: restart at most 2 s after the command");
    connectedWithKnobs();
    clogFifo(0);
    resyncRequest();
    rebootFlowLocalCmd();
    pass();
    passes(1500);
    CHECK_EQ(g_restarts, 1);                           // 300 ms after the ack was given up
    CHECK_EQ(acksLost.load(), 1);
    host = HostModel{};

    // ── Command 13: the hotspot's channel from the next boot ───────────────
#if FL_EXHIBIT_AP_CHANNEL == 0
    section("command 13 channel 6: stored in NVS, acked, the restart 300 ms after the ack, why=cmd13");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    channelCmd(6);
    evenNow();
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=1;msg=channel 6 from next boot, restarting now");
    CHECK_EQ(storedChannel(), 6);
    CHECK_EQ(nvs.writes, 1);
    CHECK_EQ(commandsReceived.load(), 1);
    CHECK_EQ(commandsRefused.load(), 0);
    CHECK_EQ(usbInDiscarded.load(), 0);
    CHECK(logged("[midi] pf monitor: command 13 (Wi-Fi channel) ok: channel 6 from next boot, restarting now"));
    CHECK_EQ(g_restarts, 0);
    passes(FL_EXHIBIT_MON_RESTART_MS - 5);
    CHECK_EQ(g_restarts, 0);
    passes(10);
    CHECK_EQ(g_restarts, 1);
    CHECK(logged("[midi] pf monitor: restarting FlowLocal (command 13)"));
    CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::Channel);
    CHECK_EQ(bootRecord.recoveries, 0);                // asked for by a person: not an automatic restart
    passes(3000);
    CHECK_EQ(g_restarts, 1);
    g_resetReason = ESP_RST_SW;                        // the next boot
    bootBegin();
    CHECK(bootInfo.note == FlRecovery::Note::Channel);
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "rsn", "sw");
    CHECK_FIELD(lastReply(), "why", "cmd13");
    g_resetReason = ESP_RST_POWERON;
    bootBegin();

    section("command 13 automatic (0), and the next of 1 / 6 / 11 (127) from the channel the hotspot is on");
    connectedWithKnobs();
    nvs.u8["fl_exhibit"]["ap_ch"] = 11;
    channelCmd(0);
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=1;msg=auto from next boot, restarting now");
    CHECK_EQ(storedChannel(), 0);
    passes(FL_EXHIBIT_MON_RESTART_MS + 5);
    CHECK_EQ(g_restarts, 1);
    {
        const int16_t on[] = {1, 6, 11, 3, 9, 13};
        const int next[] = {6, 11, 1, 6, 11, 1};
        for (size_t i = 0; i < sizeof on / sizeof on[0]; ++i) {
            connectedWithKnobs();
            apChannelNow.store(on[i]);
            channelCmd(0x7F);
            pass();
            EXPECT_GOT("PF-ACK");
            CHECK(lastAck() == "cmd=13;ok=1;msg=channel " + std::to_string(next[i]) + " from next boot, restarting now");
            CHECK_EQ(storedChannel(), next[i]);
        }
    }
    connectedWithKnobs();
    apChannelNow.store(-1);                            // the boot silence: no channel to go on from
    channelCmd(0x7F);
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=0;msg=no channel to go on from: the hotspot is still starting");
    CHECK_EQ(storedChannel(), -1);
    passes(3000);
    CHECK_EQ(g_restarts, 0);

    section("command 13 with an argument no channel can be: refused before the 3 s gate, one ack a second");
    connectedWithKnobs();
    channelCmd(14);
    channelCmd(126);                                   // the same pass: counted, no second ack
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=0;msg=no channel 14: 0 auto, 1-13, 127 next of 1/6/11");
    CHECK_EQ(commandsReceived.load(), 2);
    CHECK_EQ(commandsRefused.load(), 2);
    CHECK_EQ(nvs.writes, 0);
    channelCmd(11);                                    // corrected at once: the gate was not used
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=1;msg=channel 11 from next boot, restarting now");
    CHECK_EQ(storedChannel(), 11);
    passes(FL_EXHIBIT_MON_RESTART_MS + 5);
    CHECK_EQ(g_restarts, 1);

    section("command 13 when NVS cannot be written: refused, nothing restarts; again within 3 s: too soon");
    connectedWithKnobs();
    nvs.failWrites = true;
    channelCmd(1);
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=0;msg=could not save the channel (NVS): nothing changed");
    g_now += 1500;
    channelCmd(1);
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=0;msg=too soon: once per 3 s, wait 2 s");
    passes(3000);
    CHECK_EQ(g_restarts, 0);
    CHECK_EQ(storedChannel(), -1);
#else
    section("command 13 on a build with a fixed channel: refused, nothing stored, no restart");
    connectedWithKnobs();
    channelCmd(1);
    pass();
    EXPECT_GOT("PF-ACK");
    CHECK(lastAck() == "cmd=13;ok=0;msg=channel fixed at " + std::to_string(FL_EXHIBIT_AP_CHANNEL) +
                       " by this build: nothing changed");
    CHECK_EQ(nvs.writes, 0);
    CHECK_EQ(storedChannel(), -1);
    passes(3000);
    CHECK_EQ(g_restarts, 0);
#endif

    section("not command 13: another guard, no argument, two arguments");
    connectedWithKnobs();
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);               // guard 'R' 'H'
    laptopPacket(0x4, 0x46, 0x13, 'R');
    laptopPacket(0x7, 'H', 6, 0xF7);
    monitorCommand(0x13, 'C', 'H');                    // no argument
    laptopPacket(0x4, 0xF0, 0x7D, 0x50);               // two argument bytes
    laptopPacket(0x4, 0x46, 0x13, 'C');
    laptopPacket(0x4, 'H', 6, 6);
    laptopPacket(0x5, 0xF7, 0, 0);
    passes(3);
    EXPECT_GOT();
    CHECK_EQ(usbInDiscarded.load(), 10);
    CHECK_EQ(commandsReceived.load(), 0);
    CHECK_EQ(nvs.writes, 0);
    passes(3000);
    CHECK_EQ(g_restarts, 0);

    section("chm= and cs= from the plan setup() publishes; /api/status's session fields");
    connectedWithKnobs();
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "chm", CHM_BUILD);
    CHECK_FIELD(lastReply(), "cs", "");                // no scan yet (the boot silence)
    publishChannelMode(FlApChannel::Mode::Manual);
    {
        FlApChannel::Scores sc;
        sc.score[0] = 0;
        sc.score[1] = 2350;
        sc.score[2] = 3425;
        publishChannelScan(false, sc);                 // no scan: nothing
        g_now += 300;
        statusRequest();
        pass();
        EXPECT_GOT("PF-REPLY");
        CHECK_FIELD(lastReply(), "chm", "manual");
        CHECK_FIELD(lastReply(), "cs", "");
        publishChannelScan(true, sc);
        sc.score[0] = 99;
        publishChannelScan(true, sc);                  // once per boot: the first stays
    }
    g_now += 300;
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    {
        const std::string r = lastReply();
        std::printf("    reply text with a manual channel and scores (%u bytes): %s\n", (unsigned)r.size(), r.c_str());
        CHECK_FIELD(r, "cs", "1:0,6:24,11:35");   // in hundreds, rounded up
        CHECK(r.size() <= FlPfMonitor::TEXT_MAX);
        CHECK(keys(r) == "v,fw,up,ap,ch,chm,cl,cm,ps,pip,prs,hd,ra,lp,kn,jt,jm,rx,tx,dr,se,to,rj,gp,rr,ud,sr,hi,"
                         "rsn,bc,why,ur,cs");
    }
    publishChannelMode(FlApChannel::Mode::Auto);
    g_now += 300;
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "chm", "auto");
    {
        const std::string s = statusJson();
        CHECK(has(s, "\"rtp_session_timeout_ms\":25000"));
        CHECK(has(s, "\"rtp_evicted\":0"));
        CHECK(has(s, "\"rtp_retries_answered\":0"));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }
    channelScored.store(false);
    publishChannelMode(FL_EXHIBIT_AP_CHANNEL != 0 ? FlApChannel::Mode::Fixed : FlApChannel::Mode::Auto);

    section("RTP timing: jt= and jm= from the panel's timestamps against arrival");
    connectedWithKnobs();
    {
        // A packet every 10 ms by the panel's clock, every other one 6 ms late.
        const uint32_t base = g_now;
        for (uint32_t i = 0; i < 300; ++i) {
            g_now = base + i * 10 + (i % 2 ? 6 : 0);
            noteRtpTiming(0xAAAA0002u, 5000u + i * 100u);
        }
        pass();
        statusRequest();
        pass();
        EXPECT_GOT("PF-REPLY");
        CHECK_FIELD(lastReply(), "jt", "6");
        CHECK_FIELD(lastReply(), "jm", "6");
        CHECK_EQ(rtpJitterMs.load(), 6);
        CHECK_EQ(rtpSpreadMs.load(), 6);
        const std::string s = statusJson();
        CHECK(has(s, "\"rtp_jitter_ms\":6"));
        CHECK(has(s, "\"rtp_spread_ms\":6"));
        g_now += 61000;                                // a quiet minute
        pass();
        CHECK_EQ(rtpSpreadMs.load(), -1);
        statusRequest();
        pass();
        EXPECT_GOT("PF-REPLY");
        CHECK_FIELD(lastReply(), "jt", "6");           // the last estimate stays
        CHECK_FIELD(lastReply(), "jm", "-1");
    }

    section("PF monitor commands: /api/status");
    {
        const std::string s = statusJson();
        CHECK(has(s, "\"monitor_commands\":0"));
        CHECK(has(s, "\"panel_reboot_path\":\"/api/wifi/reboot\""));
        CHECK(has(s, "\"max_sessions\":2"));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }

    // ── Self-recovery: restart reason, bridge watchdog, USB watch ──────────

    section("bridge watchdog: fed every pass; loop()'s check is quiet while the task passes");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    {
        const int feeds = g_wdtFeeds;
        const uint32_t beats = heartbeat.load();
        passes(100);
        CHECK_EQ(g_wdtFeeds - feeds, 100);
        CHECK_EQ(heartbeat.load() - beats, 100u);
        for (int i = 0; i < 40; ++i) {         // 20 s of loop() every 500 ms, the task passing meanwhile
            watchBridge();
            passes(500);
        }
        CHECK_EQ(g_restarts, 0);
        CHECK(!bridgeStalled.load());
    }

    section("bridge watchdog: no pass for 3 s: loop() restarts FlowLocal, the next boot says why=bridge_hung");
    watchBridge();
    {
        const int looks = loopLooksWhileHung();
        CHECK_EQ(g_restarts, 1);
        CHECK(looks == 6);                     // 3 s after the last pass loop() saw, at its next look
        CHECK(bridgeStalled.load());
        CHECK(logged("[midi] bridge watchdog: no pass of the bridge task for 3000 ms"));
        CHECK(logged("[boot] restarting FlowLocal (bridge_hung, automatic restart 1 of 3 since power-on)"));
        CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::BridgeHung);
        CHECK_EQ(bootRecord.recoveries, 1);
        pass();                                // (the host test's ESP.restart() returns): the task passes again
        watchBridge();
        CHECK(!bridgeStalled.load());
        CHECK(logged("[midi] bridge watchdog: the bridge task passes again"));
    }
    g_resetReason = ESP_RST_SW;                // the next boot: the record came through
    bootBegin();
    CHECK_EQ(bootInfo.boots, 2);
    CHECK(bootInfo.note == FlRecovery::Note::BridgeHung);
    CHECK_EQ(bootInfo.recoveries, 1);
    CHECK_EQ(bootRecord.note, 0);              // reported by this boot only
    g_log.clear();
    logBoot();
    CHECK(logged("[FlowLocal] boot 2 since power-on, reset reason sw, noted bridge_hung, automatic restarts 1 of 3"));
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "rsn", "sw");
    CHECK_FIELD(lastReply(), "bc", "2");
    CHECK_FIELD(lastReply(), "why", "bridge_hung");
    CHECK_FIELD(lastReply(), "ur", "0");
    {
        const std::string s = statusJsonAll();
        CHECK(has(s, "\"restart\":{\"reset_reason\":\"sw\",\"restart_note\":\"bridge_hung\","
                     "\"boots_since_power_on\":2,\"recovery_restarts\":1,\"recovery_restarts_max\":3}"));
        CHECK(has(s, "\"usb_reconnects\":0,\"usb_watch\":\"watching\",\"bridge_stalled\":false,\"task_watchdog\":true"));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }

    section("bridge watchdog: automatic restarts stop at 3 since power-on; a power-on gives them back");
    for (int k = 2; k <= 3; ++k) {
        bridgeWatch.reset();
        bridgeStalled.store(false);
        passes(5);
        watchBridge();
        const int before = g_restarts;
        loopLooksWhileHung();
        CHECK_EQ(g_restarts, before + 1);
        pass();
        g_resetReason = ESP_RST_SW;
        bootBegin();
        CHECK_EQ(bootInfo.recoveries, (uint32_t)k);
        CHECK_EQ(bootInfo.boots, (uint32_t)(k + 1));
    }
    {
        bridgeWatch.reset();
        bridgeStalled.store(false);
        passes(5);
        watchBridge();
        const int before = g_restarts;
        loopLooksWhileHung();
        CHECK_EQ(g_restarts, before);          // refused: the 4th since power-on
        CHECK(bridgeStalled.load());
        CHECK(logged("[boot] bridge_hung: no automatic restart left (3 since power-on, the most): press RST or replug"));
        // A task starting after such a boot leaves the task watchdog alone.
        taskWdtAsked = false;
        taskWdtOn.store(false);
        g_wdtAdds = 0;
        pass();
        CHECK_EQ(g_wdtAdds, 0);
        CHECK(!taskWdtOn.load());
        CHECK(logged("[midi] bridge watchdog: task watchdog off, 3 automatic restarts since power-on (the most)"));
        // RST (a power-on) gives them back.
        g_resetReason = ESP_RST_POWERON;
        bootBegin();
        CHECK_EQ(bootInfo.boots, 1);
        CHECK_EQ(recoveriesSoFar(), 0);
        taskWdtAsked = false;
        pass();
        CHECK_EQ(g_wdtAdds, 1);
        CHECK(taskWdtOn.load());
    }

    section("task watchdog hook: notes bridge_hung only when this task's heartbeat is what stopped");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    passes(3);
    esp_task_wdt_isr_user_handler();           // another task's timeout (AsyncTCP, core 0 idle): the task just passed
    CHECK_EQ(bootRecord.note, 0);
    CHECK_EQ(bootRecord.recoveries, 0);
    g_now += 5000;                             // no pass for the watchdog's 5 s: this task's
    esp_task_wdt_isr_user_handler();
    CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::BridgeHung);
    CHECK_EQ(bootRecord.recoveries, 1);
    g_resetReason = ESP_RST_TASK_WDT;          // the watchdog's panic restarted the chip
    bootBegin();
    passes(2);
    statusRequest();
    pass();
    EXPECT_GOT("PF-REPLY");
    CHECK_FIELD(lastReply(), "rsn", "task_wdt");
    CHECK_FIELD(lastReply(), "bc", "2");
    CHECK_FIELD(lastReply(), "why", "bridge_hung");
    taskWdtOn.store(false);                    // not subscribed: never this task's
    g_now += 5000;
    esp_task_wdt_isr_user_handler();
    CHECK_EQ(bootRecord.note, 0);
    taskWdtOn.store(true);
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    // A restart FlowLocal asked for whose esp_restart() got stuck (a shutdown
    // handler): the watchdog finishes it and its own note stays.
    passes(2);
    restartFor(FlRecovery::Note::UsbWedged);   // (the host test's ESP.restart() returns: "stuck")
    CHECK_EQ(bootRecord.recoveries, 1);
    g_now += 5000;
    esp_task_wdt_isr_user_handler();
    CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::UsbWedged);
    CHECK_EQ(bootRecord.recoveries, 1);        // counted once
    g_resetReason = ESP_RST_POWERON;
    bootBegin();

    section("usb watch: nobody asking (Live and the monitor closed, laptop asleep): never, 2 min each");
    connectedWithKnobs();
    host.reading = false;                      // nobody drains the IN endpoint
    panelCc(1, 24, 1);                         // the panel still plays: forwards fail and are dropped
    passes(120000);
    host.suspended = true;                     // laptop asleep
    passes(120000);
    host.mounted = false;                      // unplugged
    passes(20000);
    CHECK_EQ(host.disconnects, 0);
    CHECK_EQ(usbReconnects.load(), 0);
    CHECK_EQ(g_restarts, 0);

    section("usb watch: the monitor asks and every reply gets out: never, over 2 min");
    connectedWithKnobs();
    monitorAsks(120000);
    CHECK_EQ(host.disconnects, 0);
    CHECK(statusReplies.load() >= 59);
    CHECK_EQ(statusAbandoned.load(), 0);

    section("usb watch: requests get no reply for 8 s (IN stopped): soft reconnect, then replies again");
    connectedWithKnobs();                      // pattern 3, K1-K3 = 64 / 70 / 80
    host.reading = false;
    monitorAsks(7900);
    CHECK_EQ(host.disconnects, 0);             // four requests in 6 s: not yet
    monitorAsks(700);
    CHECK_EQ(host.disconnects, 1);             // the fifth, 8 s after the first
    CHECK_EQ(host.connects, 1);                // back 500 ms later
    CHECK(logged("[midi] usb watch: 5 status requests in 8 s, no reply got out: soft reconnect 1"));
    CHECK(logged("[midi] usb watch: back on the bus; a status reply must get out within 10 s"));
    monitorAsks(3000);                         // enumerated again: the re-send, then the reply
    {
        const std::string got = gotText();
        CHECK(got.compare(0, seq({"C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY"}).size(),
                          seq({"C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY"})) == 0);
        if (g_failures) std::printf("    laptop read: %s\n", got.c_str());
    }
    CHECK(logged("[midi] usb watch: a status reply got out: USB works again"));
    CHECK_FIELD(lastReply(), "ur", "1");
    CHECK_EQ(usbReconnects.load(), 1);
    CHECK_EQ(g_restarts, 0);
    monitorAsks(120000);                       // and it stays that way
    CHECK_EQ(host.disconnects, 1);

    section("usb watch: TinyUSB stuck 'suspended' while the laptop sends: read anyway, reconnect, replies again");
    connectedWithKnobs();
    host.suspended = true;                     // a resume TinyUSB never saw
    pass();
    resyncRequest();                           // read now (counted), answered once USB is back
    monitorAsks(8600);
    CHECK_EQ(resyncRequests.load(), 1);
    CHECK(statusRequests.load() >= 5);
    CHECK_EQ(statusReplies.load(), 0);
    CHECK_EQ(host.disconnects, 1);
    CHECK(logged("no reply got out (TinyUSB called the bus suspended): soft reconnect 1"));
    monitorAsks(3000);
    {
        const std::string got = gotText();
        CHECK(got.compare(0, seq({"C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY"}).size(),
                          seq({"C0 03", M("BF 76 03"), K("B0 18 40"), K("B0 19 46"), K("B0 1A 50"), "PF-REPLY"})) == 0);
        if (g_failures) std::printf("    laptop read: %s\n", got.c_str());
    }
    CHECK(logged("[midi] usb watch: a status reply got out: USB works again"));

    section("usb watch: still no reply after the reconnect: restart, the next boot says why=usb_wedged");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    host.reading = false;
    host.resetFixesReading = false;            // the bus reset does not help
    monitorAsks(20000);
    CHECK_EQ(host.disconnects, 1);
    CHECK_EQ(host.connects, 1);
    CHECK_EQ(g_restarts, 1);
    CHECK(logged("[midi] usb watch: the soft reconnect did not help (status requests still get no reply)"));
    CHECK(logged("[boot] restarting FlowLocal (usb_wedged, automatic restart 1 of 3 since power-on)"));
    CHECK_EQ(bootRecord.note, (uint32_t)FlRecovery::Note::UsbWedged);
    monitorAsks(40000);                        // the next reconnect waits 60 s from the first
    CHECK_EQ(host.disconnects, 1);
    monitorAsks(20000);
    CHECK_EQ(host.disconnects, 2);
    g_resetReason = ESP_RST_SW;
    bootBegin();
    CHECK(bootInfo.note == FlRecovery::Note::UsbWedged);

    section("usb watch: the laptop does not take the device back: restart");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    host.reading = false;
    host.takesBack = false;
    monitorAsks(20000);
    CHECK_EQ(host.disconnects, 1);
    CHECK_EQ(host.connects, 1);
    CHECK_EQ(g_restarts, 1);
    CHECK(logged("[midi] usb watch: the soft reconnect did not help (the laptop did not take the device back)"));

    section("usb watch: back, but the monitor stopped asking: no restart");
    connectedWithKnobs();
    g_resetReason = ESP_RST_POWERON;
    bootBegin();
    host.reading = false;
    host.resetFixesReading = false;
    monitorAsks(8600);
    CHECK_EQ(host.disconnects, 1);
    passes(15000);                             // nobody asks any more
    CHECK_EQ(g_restarts, 0);
    CHECK(logged("[midi] usb watch: back, but no status request since: watching again"));
    {
        const std::string s = statusJson();
        CHECK(has(s, "\"usb_reconnects\":1,\"usb_watch\":\"watching\""));
        if (g_failures) std::printf("    status: %s\n", s.c_str());
    }

    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
