#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — PF monitor SysEx protocol v1, the part without hardware
//
// A monitor on the laptop asks FlowLocal how the signal path is doing, and
// asks it to repair it, over the USB-MIDI port Live already uses
// ("Patternflow Exhibit"):
//
//   status request     laptop → FlowLocal   F0 7D 50 46 01 F7
//   status reply       FlowLocal → laptop   F0 7D 50 46 02 <text> F7
//   reconnect panel    laptop → FlowLocal   F0 7D 50 46 10 52 43 F7      ('R' 'C')
//   reboot panel       laptop → FlowLocal   F0 7D 50 46 11 52 50 F7      ('R' 'P')
//   reboot FlowLocal   laptop → FlowLocal   F0 7D 50 46 12 52 46 F7      ('R' 'F')
//   Wi-Fi channel      laptop → FlowLocal   F0 7D 50 46 13 43 48 <c> F7  ('C' 'H', then
//                                           c: 00 automatic, 01..0D that channel,
//                                           7F the next of 1 / 6 / 11)
//   command ack        FlowLocal → laptop   F0 7D 50 46 03 <text> F7
//
// 7D is the manufacturer ID MIDI keeps for non-commercial use; 50 46 is "PF".
// The two bytes after a command are its guard, then its argument bytes (only
// 13 has one): a command with other guard bytes, any other length or an
// unknown command byte is not a command. An argument byte out of range is
// the bridge's to refuse, with an ack.
// <text> is printable ASCII (0x20..0x7E): "key=value" fields joined by ';',
// at most TEXT_MAX (240) bytes. Readers ignore keys they do not know; a
// writer sends a key whose value it does not know with an empty value. The
// status keys are listed at Status below and in EXHIBIT.md; v1 only ever adds
// keys at the end (the restart keys rsn, bc, why, ur came after hi, and the
// channel scores cs after ur). An ack is
// "cmd=<10|11|12|13>;ok=<0|1>;msg=<short English, no ';'>".
//
// Pure C++ (no Arduino, TinyUSB or FreeRTOS), so the host test compiles it
// as it is:
//   - SysexRx assembles SysEx from the USB-MIDI event packets the laptop
//     sends (CIN 0x4..0x7) in an RX_MAX-byte buffer and recognises the status
//     request and the four commands; everything else is discarded, packet by
//     packet;
//   - CommandGate lets each command through at most once per its interval;
//   - formatStatus() / formatAck() write the reply and ack text;
//   - buildMessage() and replyPacket() give a whole reply or ack as bytes and
//     as USB-MIDI event packets (cable 0): CIN 0x4 for three bytes that start
//     or continue it, CIN 0x5 / 0x6 / 0x7 for the last one, two or three
//     bytes (the last of them F7).
// When and how the bridge task answers and runs commands (rate limits,
// nothing between F0 and F7, a message that does not fit in time is
// abandoned) is in fl_midi_bridge.h.
//
// License: MIT
// ═══════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace FlPfMonitor {

constexpr uint8_t MANUFACTURER_ID  = 0x7D;   // non-commercial / educational use
constexpr uint8_t SIGNATURE_1      = 0x50;   // 'P'
constexpr uint8_t SIGNATURE_2      = 0x46;   // 'F'
constexpr uint8_t STATUS_REQUEST   = 0x01;
constexpr uint8_t STATUS_REPLY     = 0x02;
constexpr uint8_t COMMAND_ACK      = 0x03;
constexpr uint8_t CMD_RECONNECT_PANEL  = 0x10;   // guard 'R' 'C'
constexpr uint8_t CMD_REBOOT_PANEL     = 0x11;   // guard 'R' 'P'
constexpr uint8_t CMD_REBOOT_FLOWLOCAL = 0x12;   // guard 'R' 'F'
constexpr uint8_t CMD_SET_CHANNEL      = 0x13;   // guard 'C' 'H', one argument byte
constexpr uint8_t CMD_FIRST = CMD_RECONNECT_PANEL;
constexpr uint8_t CMD_COUNT = 4;
constexpr int     PROTOCOL_VERSION = 1;
// Command 13's argument: automatic, a channel 1..13, or the next of 1 / 6 / 11.
constexpr uint8_t CHANNEL_AUTO = 0x00;
constexpr uint8_t CHANNEL_NEXT = 0x7F;

constexpr size_t TEXT_MAX          = 240;                          // reply / ack text bytes
constexpr size_t MESSAGE_HEAD      = 5;                            // F0 7D 50 46 <02|03>
constexpr size_t REPLY_MAX         = MESSAGE_HEAD + TEXT_MAX + 1;  // ... <text> F7
constexpr size_t REPLY_PACKETS_MAX = (REPLY_MAX + 2) / 3;
constexpr size_t RX_MAX            = 8;     // SysEx bytes kept between F0 and F7 (the longest command has 7)
constexpr size_t TAG_MAX           = 32;
constexpr size_t ACK_MSG_MAX       = 64;    // msg= characters in an ack
constexpr size_t ACK_TEXT_MAX      = 16 + ACK_MSG_MAX;   // "cmd=10;ok=1;msg=" + msg
constexpr size_t REASON_MAX        = 16;    // rsn= / why= characters
// The shortest status text formatStatus() can write: every key there, each
// value at its shortest (the host test pins it). A text that loses fields to
// TEXT_MAX is longer still (at least TEXT_MAX less its longest field), so no
// reply is shorter. fl_midi_bridge.h counts on it: a reply written whole is
// more USB-MIDI packets than the device can hold, so the laptop read it.
constexpr size_t STATUS_TEXT_MIN   = 155;

// The guard bytes a command must carry; false for a byte that is no command.
constexpr bool commandGuard(uint8_t cmd, uint8_t& g1, uint8_t& g2) {
    switch (cmd) {
        case CMD_RECONNECT_PANEL:  g1 = 'R'; g2 = 'C'; return true;
        case CMD_REBOOT_PANEL:     g1 = 'R'; g2 = 'P'; return true;
        case CMD_REBOOT_FLOWLOCAL: g1 = 'R'; g2 = 'F'; return true;
        case CMD_SET_CHANNEL:      g1 = 'C'; g2 = 'H'; return true;
        default:                   g1 = 0;   g2 = 0;   return false;
    }
}

// Argument bytes after a command's guard: exactly this many, or it is no command.
constexpr uint8_t commandArgs(uint8_t cmd) { return cmd == CMD_SET_CHANNEL ? 1 : 0; }

// Command 13's argument is one of these; any other byte is refused.
constexpr bool validChannelChoice(uint8_t arg) {
    return arg == CHANNEL_AUTO || (arg >= 1 && arg <= 13) || arg == CHANNEL_NEXT;
}

// A build tag fit for fw=: 1..TAG_MAX printable characters, no space, ';' or '='.
constexpr bool validTag(const char* s) {
    size_t n = 0;
    for (; s[n] != '\0'; ++n)
        if (s[n] <= 0x20 || s[n] > 0x7E || s[n] == ';' || s[n] == '=') return false;
    return n >= 1 && n <= TAG_MAX;
}

// ── Laptop → FlowLocal: SysEx assembly ─────────────────────────────────────

// What one packet from the laptop meant on the SysEx side.
struct RxResult {
    bool     sysex = false;          // a SysEx packet (CIN 0x4, 0x6, 0x7, or 0x5 carrying F7): nothing else to do with it
    bool     statusRequest = false;  // it completed F0 7D 50 46 01 F7
    uint8_t  command = 0;            // it completed a command with its guard (CMD_*), else 0
    uint8_t  arg = 0;                // that command's argument byte (commandArgs), else 0
    uint16_t discarded = 0;          // packets of SysEx that ended here and were neither
};

// Bytes between F0 and F7 go into an RX_MAX-byte buffer. A SysEx that is
// longer, has a status byte inside, starts without F0 or ends without F7 is
// marked bad and only counted. Its packets count when it ends: at its F7, at
// an ending packet (CIN 0x5..0x7) without F7, at a new F0, or at any packet
// that is not SysEx other than a real-time byte (MIDI 1.0: a status byte ends
// SysEx; F8..FF may sit inside one). The packet that cuts it short is left to
// the caller, like any other.
class SysexRx {
public:
    // header = cable << 4 | CIN, as USB-MIDI event packets carry it.
    RxResult feed(uint8_t header, uint8_t b1, uint8_t b2, uint8_t b3) {
        RxResult r;
        const uint8_t cin = header & 0x0F;
        uint8_t n = 0;
        if (cin == 0x4 || cin == 0x7) n = 3;
        else if (cin == 0x6) n = 2;
        else if (cin == 0x5 && b1 == 0xF7) n = 1;   // CIN 0x5 is also a one-byte system common (F6)
        if (n == 0) {
            const bool realtime = (cin == 0xF && b1 >= 0xF8);
            if (active_ && !realtime) drop(r);
            return r;
        }
        r.sysex = true;
        counted_ = false;
        const uint8_t bytes[3] = {b1, b2, b3};
        for (uint8_t i = 0; i < n; ++i) take(bytes[i], r);
        if (cin != 0x4 && active_) drop(r);   // an ending packet whose last byte was not F7
        return r;
    }

    bool active() const { return active_; }
    void reset() { active_ = false; bad_ = false; len_ = 0; packets_ = 0; counted_ = false; }

private:
    void start(bool bad) {
        active_ = true;
        bad_ = bad;
        len_ = 0;
        packets_ = 1;   // the packet being read
        counted_ = true;
    }
    void take(uint8_t b, RxResult& r) {
        if (b == 0xF0) {
            if (active_) drop(r);
            start(false);
            return;
        }
        if (!active_) {
            if (b == 0xF7) {              // a stray F7
                r.discarded = (uint16_t)(r.discarded + 1);
                return;
            }
            start(true);                  // data (or a status byte) without F0 before it
        } else if (!counted_) {           // the first byte of this packet in this SysEx
            ++packets_;
            counted_ = true;
        }
        if (b == 0xF7) {
            end(r);
            return;
        }
        if (b & 0x80) { bad_ = true; return; }
        if (bad_) return;
        if (len_ == RX_MAX) { bad_ = true; return; }
        buf_[len_++] = b;
    }
    void end(RxResult& r) {
        active_ = false;
        if (!bad_ && len_ >= 4 && buf_[0] == MANUFACTURER_ID && buf_[1] == SIGNATURE_1 &&
            buf_[2] == SIGNATURE_2) {
            uint8_t g1 = 0, g2 = 0;
            if (len_ == 4 && buf_[3] == STATUS_REQUEST) {
                r.statusRequest = true;
                return;
            }
            if (len_ >= 6 && commandGuard(buf_[3], g1, g2) && len_ == 6 + commandArgs(buf_[3]) &&
                buf_[4] == g1 && buf_[5] == g2) {
                r.command = buf_[3];
                r.arg = commandArgs(buf_[3]) ? buf_[6] : (uint8_t)0;
                return;
            }
        }
        r.discarded = (uint16_t)(r.discarded + packets_);
    }
    void drop(RxResult& r) {
        r.discarded = (uint16_t)(r.discarded + packets_);
        active_ = false;
    }

    uint8_t  buf_[RX_MAX] = {};
    uint8_t  len_ = 0;
    uint16_t packets_ = 0;      // packets of the current SysEx so far
    bool     counted_ = false;  // the packet being read is in packets_
    bool     active_ = false;
    bool     bad_ = false;
};

// Each command at most once per minMs (its own clock: one command does not
// hold up another). now is a millisecond clock; 0 is a valid time.
class CommandGate {
public:
    explicit CommandGate(uint32_t minMs) : minMs_(minMs) {}
    // True, and the command's interval starts again, when it may run now.
    bool allow(uint8_t cmd, uint32_t now) {
        const unsigned i = (unsigned)(uint8_t)(cmd - CMD_FIRST);
        if (i >= CMD_COUNT) return false;
        if (seen_[i] && now - last_[i] < minMs_) return false;
        seen_[i] = true;
        last_[i] = now;
        return true;
    }
    // Milliseconds until allow() would let cmd through (0 = now).
    uint32_t waitMs(uint8_t cmd, uint32_t now) const {
        const unsigned i = (unsigned)(uint8_t)(cmd - CMD_FIRST);
        if (i >= CMD_COUNT || !seen_[i] || now - last_[i] >= minMs_) return 0;
        return minMs_ - (now - last_[i]);
    }
    void reset() {
        for (unsigned i = 0; i < CMD_COUNT; ++i) { seen_[i] = false; last_[i] = 0; }
    }

private:
    uint32_t minMs_;
    uint32_t last_[CMD_COUNT] = {};
    bool     seen_[CMD_COUNT] = {};
};

// ── FlowLocal → laptop: the reply ──────────────────────────────────────────

// chm= values, by Status::chm: how this boot's hotspot channel was chosen.
constexpr int8_t CHM_FIXED  = 0;   // FL_EXHIBIT_AP_CHANNEL, compiled in
constexpr int8_t CHM_AUTO   = 1;   // the boot scan
constexpr int8_t CHM_MANUAL = 2;   // command 13, kept in NVS

// The reply's values. Unknown ones are sent with an empty value (ap, ch, chm,
// cl, cm, pip, prs, kn, rsn, bc, cs; why is empty when there is no note) or
// as -1 (hd, ra, lp, jt, jm), as the protocol says.
struct Status {
    const char* fw = "";         // fw=  build tag (made fit by formatStatus)
    uint32_t up = 0;             // up=  uptime, s
    int8_t   ap = -1;            // ap=  soft AP running 0 / 1; -1 = unknown
    int16_t  ch = -1;            // ch=  AP channel; -1 = unknown (AP not up)
    int8_t   chm = -1;           // chm= CHM_AUTO / CHM_FIXED / CHM_MANUAL; -1 = unknown
    int16_t  cl = -1;            // cl=  stations on the hotspot; -1 = unknown
    int16_t  cm = -1;            // cm=  hotspot max stations; -1 = unknown
    int32_t  ps = 0;             // ps=  RTP sessions up
    uint32_t pip = 0;            // pip= panel IPv4, first octet in the low byte (lwIP / IPAddress raw); 0 = none
    bool     prsKnown = false;   // prs= RSSI of the panel's station, dBm
    int32_t  prs = 0;
    int64_t  hd = -1;            // hd=  ms since MIDI or a heartbeat from a session, -1 never
    int64_t  ra = -1;            // ra=  ms since MIDI from a session, -1 never
    int32_t  lp = -1;            // lp=  last Program Change from the panel, -1 none
    uint8_t  knobs = 0;          // kn=  how many knob values, K1 first; 0 = unknown (empty)
    uint8_t  knobSeen = 0;       //      bit i: knobValue[i] is known, else '-'
    uint8_t  knobValue[8] = {};
    int32_t  jt = -1;            // jt=  RTP interarrival jitter, ms, -1 unknown
    int32_t  jm = -1;            // jm=  largest transit deviation in the last 60 s, ms, -1 unknown
    uint32_t rx = 0, tx = 0, dr = 0;   // messages from RTP / written to USB / dropped
    uint32_t se = 0;                   // sessions opened since boot
    uint32_t to = 0, rj = 0, gp = 0;   // sessions timed out / invitations rejected / packet gaps
    uint32_t rr = 0;                   // resync requests
    uint32_t ud = 0;                   // other USB-in packets discarded
    uint32_t sr = 0;                   // status requests answered
    uint32_t hi = 0;                   // free internal heap, bytes
    // Last, so a reply too long for TEXT_MAX loses these first, cs first,
    // then ur: cs, rsn, bc and why stay the same while FlowLocal runs (right
    // after a restart, when they matter, the counters above are small and
    // everything fits), and a reader keeps the ones it got; a soft reconnect
    // (ur) also shows as the port leaving and coming back while up= goes on.
    const char* rsn = "";              // rsn= why the chip last reset (esp_reset_reason), "" unknown
    uint32_t bc = 0;                   // bc=  boots since power-on, this one included; 0 = unknown (empty)
    const char* why = "";              // why= what FlowLocal noted before it restarted itself, "" none
    uint32_t ur = 0;                   // ur=  USB soft reconnects since boot
    bool     scanned = false;          // cs=  this boot's channel scan scores for 1, 6 and 11
    uint32_t cs[3] = {};               //      (fl_ap_channel.h; lower is quieter); empty without a scan
};

// cs= is "channel:score" for 1, 6 and 11, each score in hundreds, rounded up
// (0 only for a channel no network was heard near): "cs=1:0,6:24,11:35".
// Exact scores would not fit: a reply is about 220 bytes an hour into an
// ordinary day, and cs= comes last. /api/status and the log have them exact.
constexpr uint32_t scoreHundreds(uint32_t score) { return score / 100 + (score % 100 != 0 ? 1 : 0); }

// Joins "key=value" fields with ';' into at most TEXT_MAX bytes. A field that
// does not fit whole is left out, and so is every field after it, so a reply
// is never cut inside a value and always keeps its first fields.
class TextOut {
public:
    TextOut(char* out, size_t cap) : out_(out), cap_(cap > TEXT_MAX + 1 ? TEXT_MAX + 1 : cap) {
        if (cap_) out_[0] = '\0';
    }
    // n: what snprintf returned for field (its full length).
    void add(const char* field, int n) {
        if (full_ || n <= 0) { full_ = full_ || n < 0; return; }
        const size_t need = (len_ ? 1u : 0u) + (size_t)n;
        if (cap_ == 0 || len_ + need > cap_ - 1) { full_ = true; return; }
        if (len_) out_[len_++] = ';';
        std::memcpy(out_ + len_, field, (size_t)n);
        len_ += (size_t)n;
        out_[len_] = '\0';
    }
    size_t length() const { return len_; }
    bool   full() const { return full_; }

private:
    char*  out_;
    size_t cap_;
    size_t len_ = 0;
    bool   full_ = false;
};

// Field buffer: the longest field is msg= in an ack (4 + ACK_MSG_MAX).
constexpr size_t FIELD_MAX = 4 + ACK_MSG_MAX + 1;

// "key=" followed by the characters of s fit for a value (printable, no ';'),
// others as '_', at most max of them. Returns the field's length.
inline int cleanField(char* f, size_t cap, const char* key, const char* s, size_t max) {
    size_t n = 0;
    for (; key[n] != '\0' && n + 1 < cap; ++n) f[n] = key[n];
    for (size_t i = 0; s != nullptr && s[i] != '\0' && i < max && n + 1 < cap; ++i) {
        const char c = s[i];
        f[n++] = (c < 0x20 || c > 0x7E || c == ';') ? '_' : c;
    }
    f[n] = '\0';
    return (int)n;
}

// The reply text into out (cap bytes, TEXT_MAX + 1 is enough); returns its
// length. Every byte is 0x20..0x7E. Every key is always there, in this order,
// unless TEXT_MAX cuts the last ones (TextOut).
inline size_t formatStatus(char* out, size_t cap, const Status& s) {
    TextOut t(out, cap);
    char f[FIELD_MAX];
    t.add(f, std::snprintf(f, sizeof f, "v=%d", PROTOCOL_VERSION));
    {
        // fw=: the tag's characters that are fit for it, others as '_'.
        const int n = cleanField(f, sizeof f, "fw=", s.fw, TAG_MAX);
        for (int i = 3; i < n; ++i)
            if (f[i] == ' ' || f[i] == '=') f[i] = '_';
        t.add(f, n);
    }
    t.add(f, std::snprintf(f, sizeof f, "up=%lu", (unsigned long)s.up));
    if (s.ap >= 0) t.add(f, std::snprintf(f, sizeof f, "ap=%d", s.ap ? 1 : 0));
    else t.add(f, std::snprintf(f, sizeof f, "ap="));
    if (s.ch > 0) t.add(f, std::snprintf(f, sizeof f, "ch=%d", (int)s.ch));
    else t.add(f, std::snprintf(f, sizeof f, "ch="));
    t.add(f, std::snprintf(f, sizeof f, "chm=%s", s.chm == CHM_FIXED ? "fixed" : s.chm == CHM_AUTO ? "auto"
                                                  : s.chm == CHM_MANUAL ? "manual" : ""));
    if (s.cl >= 0) t.add(f, std::snprintf(f, sizeof f, "cl=%d", (int)s.cl));
    else t.add(f, std::snprintf(f, sizeof f, "cl="));
    if (s.cm >= 0) t.add(f, std::snprintf(f, sizeof f, "cm=%d", (int)s.cm));
    else t.add(f, std::snprintf(f, sizeof f, "cm="));
    t.add(f, std::snprintf(f, sizeof f, "ps=%ld", (long)s.ps));
    if (s.pip != 0)
        t.add(f, std::snprintf(f, sizeof f, "pip=%u.%u.%u.%u", (unsigned)(s.pip & 0xFFu),
                               (unsigned)((s.pip >> 8) & 0xFFu), (unsigned)((s.pip >> 16) & 0xFFu),
                               (unsigned)(s.pip >> 24)));
    else
        t.add(f, std::snprintf(f, sizeof f, "pip="));
    if (s.prsKnown) t.add(f, std::snprintf(f, sizeof f, "prs=%ld", (long)s.prs));
    else t.add(f, std::snprintf(f, sizeof f, "prs="));
    t.add(f, std::snprintf(f, sizeof f, "hd=%lld", (long long)(s.hd < 0 ? -1 : s.hd)));
    t.add(f, std::snprintf(f, sizeof f, "ra=%lld", (long long)(s.ra < 0 ? -1 : s.ra)));
    t.add(f, std::snprintf(f, sizeof f, "lp=%ld", (long)(s.lp < 0 ? -1 : s.lp)));
    {
        int n = std::snprintf(f, sizeof f, "kn=");
        for (uint8_t i = 0; i < s.knobs && i < 8 && n > 0 && (size_t)n < sizeof f; ++i) {
            const int w = (s.knobSeen >> i) & 1u
                ? std::snprintf(f + n, sizeof f - (size_t)n, "%s%u", i ? "," : "", (unsigned)(s.knobValue[i] & 0x7F))
                : std::snprintf(f + n, sizeof f - (size_t)n, "%s-", i ? "," : "");
            n = (w < 0) ? -1 : n + w;
        }
        t.add(f, (n > 0 && (size_t)n < sizeof f) ? n : -1);
    }
    t.add(f, std::snprintf(f, sizeof f, "jt=%ld", (long)(s.jt < 0 ? -1 : s.jt)));
    t.add(f, std::snprintf(f, sizeof f, "jm=%ld", (long)(s.jm < 0 ? -1 : s.jm)));
    t.add(f, std::snprintf(f, sizeof f, "rx=%lu", (unsigned long)s.rx));
    t.add(f, std::snprintf(f, sizeof f, "tx=%lu", (unsigned long)s.tx));
    t.add(f, std::snprintf(f, sizeof f, "dr=%lu", (unsigned long)s.dr));
    t.add(f, std::snprintf(f, sizeof f, "se=%lu", (unsigned long)s.se));
    t.add(f, std::snprintf(f, sizeof f, "to=%lu", (unsigned long)s.to));
    t.add(f, std::snprintf(f, sizeof f, "rj=%lu", (unsigned long)s.rj));
    t.add(f, std::snprintf(f, sizeof f, "gp=%lu", (unsigned long)s.gp));
    t.add(f, std::snprintf(f, sizeof f, "rr=%lu", (unsigned long)s.rr));
    t.add(f, std::snprintf(f, sizeof f, "ud=%lu", (unsigned long)s.ud));
    t.add(f, std::snprintf(f, sizeof f, "sr=%lu", (unsigned long)s.sr));
    t.add(f, std::snprintf(f, sizeof f, "hi=%lu", (unsigned long)s.hi));
    t.add(f, cleanField(f, sizeof f, "rsn=", s.rsn, REASON_MAX));
    if (s.bc > 0) t.add(f, std::snprintf(f, sizeof f, "bc=%lu", (unsigned long)s.bc));
    else t.add(f, std::snprintf(f, sizeof f, "bc="));
    t.add(f, cleanField(f, sizeof f, "why=", s.why, REASON_MAX));
    t.add(f, std::snprintf(f, sizeof f, "ur=%lu", (unsigned long)s.ur));
    if (s.scanned)
        t.add(f, std::snprintf(f, sizeof f, "cs=1:%lu,6:%lu,11:%lu", (unsigned long)scoreHundreds(s.cs[0]),
                               (unsigned long)scoreHundreds(s.cs[1]), (unsigned long)scoreHundreds(s.cs[2])));
    else
        t.add(f, std::snprintf(f, sizeof f, "cs="));
    return t.length();
}

// "cmd=<10|11|12|13>;ok=<0|1>;msg=<msg>" into out (cap bytes, ACK_TEXT_MAX + 1
// is enough); returns its length. msg keeps at most ACK_MSG_MAX characters,
// with ';' and bytes outside 0x20..0x7E as '_'.
inline size_t formatAck(char* out, size_t cap, uint8_t cmd, bool ok, const char* msg) {
    TextOut t(out, cap);
    char f[FIELD_MAX];
    t.add(f, std::snprintf(f, sizeof f, "cmd=%02X", (unsigned)cmd));
    t.add(f, std::snprintf(f, sizeof f, "ok=%d", ok ? 1 : 0));
    t.add(f, cleanField(f, sizeof f, "msg=", msg, ACK_MSG_MAX));
    return t.length();
}

// F0 7D 50 46 <kind> <text> F7 into msg (cap bytes, REPLY_MAX is enough);
// returns its length, 0 if cap cannot hold an empty message. Text past
// TEXT_MAX (or past cap) is left out; a byte outside 0x20..0x7E goes as '?'.
inline size_t buildMessage(uint8_t* msg, size_t cap, uint8_t kind, const char* text, size_t textLen) {
    if (cap < MESSAGE_HEAD + 1) return 0;
    size_t n = textLen;
    if (n > TEXT_MAX) n = TEXT_MAX;
    if (n > cap - MESSAGE_HEAD - 1) n = cap - MESSAGE_HEAD - 1;
    size_t at = 0;
    msg[at++] = 0xF0;
    msg[at++] = MANUFACTURER_ID;
    msg[at++] = SIGNATURE_1;
    msg[at++] = SIGNATURE_2;
    msg[at++] = (uint8_t)(kind & 0x7F);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = (uint8_t)text[i];
        msg[at++] = (c >= 0x20 && c <= 0x7E) ? c : (uint8_t)'?';
    }
    msg[at++] = 0xF7;
    return at;
}
inline size_t buildReply(uint8_t* msg, size_t cap, const char* text, size_t textLen) {
    return buildMessage(msg, cap, STATUS_REPLY, text, textLen);
}

// USB-MIDI event packets for a SysEx message of len bytes (F0 .. F7).
constexpr size_t packetCount(size_t len) { return (len + 2) / 3; }

// Packet i of that message into out[4] (header, then three bytes, unused ones
// 0); false past the last packet.
inline bool replyPacket(const uint8_t* msg, size_t len, size_t i, uint8_t out[4]) {
    const size_t at = i * 3;
    if (at >= len) return false;
    const size_t left = len - at;
    const size_t n = left > 3 ? 3 : left;
    out[0] = left > 3 ? (uint8_t)0x4 : (uint8_t)(0x4 + n);   // cable 0; 0x5 / 0x6 / 0x7 end it
    for (size_t k = 0; k < 3; ++k) out[1 + k] = k < n ? msg[at + k] : (uint8_t)0;
    return true;
}

}  // namespace FlPfMonitor
