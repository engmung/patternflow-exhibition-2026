// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the panel link (PFE/1), as bytes
//
// What travels over UDP between a hub and its panels. The contract is
// ensemble/PROTOCOL.md; this file is that document in code, and
// ensemble/tests/test_core.cpp holds a message as the bytes it must be.
//
// Four messages, all little-endian, all starting  'P' 'F' 'E' <version> <type>:
//
//   REPORT  panel -> hub   where the panel is: pattern, four knob positions,
//                          and a counter per knob that moves when a hand does
//   INFO    panel -> hub   who the panel is: firmware, the pattern's name
//   STATE   hub -> panel   where the panel should be
//   CMD     hub -> panel   reboot
//
// Both directions carry whole state, never a change. A lost packet is
// repaired by the next one, and a panel that restarts is told everything
// again in one message.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "pfe_util.h"

namespace pfe {

constexpr uint16_t UDP_PORT = 7400;      // the hub listens here
constexpr uint8_t PROTO_VERSION = 1;

constexpr int KNOBS = 4;
constexpr uint16_t KNOB_MAX = 1000;      // the absolute bus's scale (PF_BUS_MAX)
constexpr uint8_t NO_PATTERN = 0xFF;

enum MsgType : uint8_t {
  MSG_REPORT = 0x01,
  MSG_INFO = 0x02,
  MSG_STATE = 0x10,
  MSG_CMD = 0x11,
};

// REPORT.flags
constexpr uint8_t RF_BOOT = 0x01;      // no STATE applied yet since this panel started
constexpr uint8_t RF_SLEEPING = 0x02;  // the panel is asleep
constexpr uint8_t RF_CHROME = 0x04;    // one of the device's own screens has the knobs
constexpr uint8_t RF_NOT_RUNNING = 0x08;  // not in RUNNING mode (the pattern browser is open)

// STATE.flags
constexpr uint8_t SF_K4_PATTERN = 0x01;  // K4 steps through patterns instead of being a knob
constexpr uint8_t SF_LOCK_MENUS = 0x02;  // the long-press screens stay shut
constexpr uint8_t SF_IDENTIFY = 0x04;    // show the panel's number on its LEDs
constexpr uint8_t SF_INFO_REQ = 0x08;    // send an INFO now

constexpr uint8_t CMD_REBOOT = 0x01;

constexpr size_t HEADER_LEN = 5;
constexpr size_t REPORT_LEN = 36;
constexpr size_t INFO_LEN = 72;
constexpr size_t STATE_LEN = 32;
constexpr size_t CMD_LEN = 8;
constexpr size_t MAX_PACKET = 96;

struct Report {
  uint8_t flags = 0;
  uint8_t uid[6] = {0, 0, 0, 0, 0, 0};
  uint16_t seq = 0;          // counts this panel's reports
  uint16_t ack = 0;          // seq of the last STATE it applied, 0 for none
  uint8_t pattern = NO_PATTERN;
  uint8_t patTouch = 0;      // moves when a person picks a pattern at the panel
  uint16_t knob[KNOBS] = {500, 500, 500, 500};   // 0..1000
  uint8_t touch[KNOBS] = {0, 0, 0, 0};           // moves when a hand turns that knob
  int8_t rssi = 0;
  uint8_t fps = 0;
  uint32_t uptimeS = 0;
};

struct Info {
  uint8_t uid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t pattern = NO_PATTERN;
  uint8_t patternCount = 0;  // 0 = the panel does not know
  uint8_t detents = 0;       // detents from one end of a knob to the other
  uint8_t k4Steps = 0;       // K4 detents per pattern step
  char fw[16] = {0};
  char variant[16] = {0};
  char patternName[24] = {0};
};

struct State {
  uint8_t flags = 0;
  uint16_t seq = 0;
  uint8_t panelId = 0;       // the number the console shows; 0 = none yet
  uint8_t pattern = NO_PATTERN;
  uint8_t patAck = 0;        // the patTouch this target already accounts for
  uint8_t patMin = 0;        // K4's range, both ends included
  uint8_t patMax = NO_PATTERN;   // 0xFF = up to the panel's last pattern
  uint8_t k4Steps = 0;       // 0 = the panel's own default
  uint16_t knob[KNOBS] = {500, 500, 500, 500};
  uint8_t ack[KNOBS] = {0, 0, 0, 0};   // the touch each target accounts for
  uint16_t glideMs = 0;      // time to reach a new target; 0 = at once
  uint32_t hubMs = 0;        // the hub's clock, for the panel's log only
};

struct Cmd {
  uint8_t cmd = 0;
};

inline void putHeader(uint8_t* p, uint8_t type) {
  p[0] = 'P'; p[1] = 'F'; p[2] = 'E'; p[3] = PROTO_VERSION; p[4] = type;
}

// The type of a packet, or 0 when it is not one of ours (wrong magic, wrong
// version, shorter than its type needs). Longer is fine: a later version may
// append fields, and an older reader ignores them.
inline uint8_t packetType(const uint8_t* p, size_t len) {
  if (len < HEADER_LEN || p[0] != 'P' || p[1] != 'F' || p[2] != 'E') return 0;
  if (p[3] != PROTO_VERSION) return 0;
  switch (p[4]) {
    case MSG_REPORT: return len >= REPORT_LEN ? MSG_REPORT : 0;
    case MSG_INFO: return len >= INFO_LEN ? MSG_INFO : 0;
    case MSG_STATE: return len >= STATE_LEN ? MSG_STATE : 0;
    case MSG_CMD: return len >= CMD_LEN ? MSG_CMD : 0;
    default: return 0;
  }
}

inline size_t encode(const Report& r, uint8_t* p) {
  zeroBytes(p, REPORT_LEN);
  putHeader(p, MSG_REPORT);
  p[5] = r.flags;
  copyBytes(p + 6, r.uid, 6);
  putU16(p + 12, r.seq);
  putU16(p + 14, r.ack);
  p[16] = r.pattern;
  p[17] = r.patTouch;
  for (int k = 0; k < KNOBS; k++) putU16(p + 18 + 2 * k, r.knob[k]);
  for (int k = 0; k < KNOBS; k++) p[26 + k] = r.touch[k];
  p[30] = (uint8_t)r.rssi;
  p[31] = r.fps;
  putU32(p + 32, r.uptimeS);
  return REPORT_LEN;
}

inline bool decode(const uint8_t* p, size_t len, Report* r) {
  if (packetType(p, len) != MSG_REPORT) return false;
  r->flags = p[5];
  copyBytes(r->uid, p + 6, 6);
  r->seq = getU16(p + 12);
  r->ack = getU16(p + 14);
  r->pattern = p[16];
  r->patTouch = p[17];
  for (int k = 0; k < KNOBS; k++) {
    const uint16_t v = getU16(p + 18 + 2 * k);
    r->knob[k] = v > KNOB_MAX ? KNOB_MAX : v;
  }
  for (int k = 0; k < KNOBS; k++) r->touch[k] = p[26 + k];
  r->rssi = (int8_t)p[30];
  r->fps = p[31];
  r->uptimeS = getU32(p + 32);
  return true;
}

inline size_t encode(const Info& m, uint8_t* p) {
  zeroBytes(p, INFO_LEN);
  putHeader(p, MSG_INFO);
  copyBytes(p + 6, m.uid, 6);
  p[12] = m.pattern;
  p[13] = m.patternCount;
  p[14] = m.detents;
  p[15] = m.k4Steps;
  copyBytes(p + 16, m.fw, 16);
  copyBytes(p + 32, m.variant, 16);
  copyBytes(p + 48, m.patternName, 24);
  return INFO_LEN;
}

inline bool decode(const uint8_t* p, size_t len, Info* m) {
  if (packetType(p, len) != MSG_INFO) return false;
  copyBytes(m->uid, p + 6, 6);
  m->pattern = p[12];
  m->patternCount = p[13];
  m->detents = p[14];
  m->k4Steps = p[15];
  copyBytes(m->fw, p + 16, 16);
  copyBytes(m->variant, p + 32, 16);
  copyBytes(m->patternName, p + 48, 24);
  // Fixed fields off the air: whatever the sender did, they end in a zero.
  m->fw[15] = 0;
  m->variant[15] = 0;
  m->patternName[23] = 0;
  return true;
}

inline size_t encode(const State& s, uint8_t* p) {
  zeroBytes(p, STATE_LEN);
  putHeader(p, MSG_STATE);
  p[5] = s.flags;
  putU16(p + 6, s.seq);
  p[8] = s.panelId;
  p[9] = s.pattern;
  p[10] = s.patAck;
  p[11] = s.patMin;
  p[12] = s.patMax;
  p[13] = s.k4Steps;
  for (int k = 0; k < KNOBS; k++) putU16(p + 14 + 2 * k, s.knob[k]);
  for (int k = 0; k < KNOBS; k++) p[22 + k] = s.ack[k];
  putU16(p + 26, s.glideMs);
  putU32(p + 28, s.hubMs);
  return STATE_LEN;
}

inline bool decode(const uint8_t* p, size_t len, State* s) {
  if (packetType(p, len) != MSG_STATE) return false;
  s->flags = p[5];
  s->seq = getU16(p + 6);
  s->panelId = p[8];
  s->pattern = p[9];
  s->patAck = p[10];
  s->patMin = p[11];
  s->patMax = p[12];
  s->k4Steps = p[13];
  for (int k = 0; k < KNOBS; k++) {
    const uint16_t v = getU16(p + 14 + 2 * k);
    s->knob[k] = v > KNOB_MAX ? KNOB_MAX : v;
  }
  for (int k = 0; k < KNOBS; k++) s->ack[k] = p[22 + k];
  s->glideMs = getU16(p + 26);
  s->hubMs = getU32(p + 28);
  return true;
}

// A command carries two guard bytes after its number, as the exhibition's
// monitor commands did: a stray or truncated datagram must not restart a panel.
inline size_t encode(const Cmd& c, uint8_t* p) {
  zeroBytes(p, CMD_LEN);
  putHeader(p, MSG_CMD);
  p[5] = c.cmd;
  p[6] = 'P';
  p[7] = 'F';
  return CMD_LEN;
}

inline bool decode(const uint8_t* p, size_t len, Cmd* c) {
  if (packetType(p, len) != MSG_CMD) return false;
  if (p[6] != 'P' || p[7] != 'F') return false;
  c->cmd = p[5];
  return true;
}

}  // namespace pfe
