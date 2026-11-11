// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the panel's side
//
// One panel of an ensemble. It keeps one position per knob, 0..1000, shows
// it (as a lane, the way the 2026 exhibition's panel did), tells the hub
// where it is, and goes where the hub says.
//
// Two rules make it safe on a lossy link:
//
//   Hands win.  A knob counts its hand's moves in `touch`. The hub's target
//               for that knob is obeyed only when the STATE carrying it says
//               it has seen the same count (State::ack). A target sent before
//               the hub heard about the last turn is simply not for this
//               moment, and is ignored - no timers, nothing to tune.
//   State only. Every message is the whole state. A lost one costs nothing
//               the next one does not bring.
//
// The pattern works the same way, with `patTouch` and State::patAck.
//
// No Arduino here: features/ensemble/core_ensemble.h feeds this from the
// device's frame, the tests and the console's demo feed it from a script.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "pfe_proto.h"

namespace pfe {

struct FollowerConfig {
  uint8_t detents = 32;          // from one end of a knob to the other (about a turn and a half)
  uint8_t k4Steps = 3;           // K4 detents per pattern step, until the hub says otherwise
  bool k4Pattern = false;        // K4's role until the hub says otherwise
  uint16_t start[KNOBS] = {500, 500, 500, 500};
  const char* fw = "";
  const char* variant = "";
};

// What the device knows at the top of a frame.
struct FrameIn {
  uint32_t nowMs = 0;
  int delta[KNOBS] = {0, 0, 0, 0};       // the hand's detents, this frame
  bool pressed[KNOBS] = {false, false, false, false};
  int pattern = -1;                      // registry index now running
  int patternCount = 0;                  // 0 = unknown
  const char* patternName = nullptr;
  bool running = true;                   // RUNNING mode
  bool chrome = false;                   // a device screen has the knobs
  bool sleeping = false;
  int8_t rssi = 0;
  uint8_t fps = 0;
};

// What the frame should carry on to the pattern.
struct FrameOut {
  bool ownKnob[KNOBS] = {false, false, false, false};     // take this knob's detents out of the frame
  bool laneActive[KNOBS] = {false, false, false, false};
  float lane[KNOBS] = {0.5f, 0.5f, 0.5f, 0.5f};           // 0..1
  bool lockMenus = false;
  bool identify = false;
  uint8_t panelId = 0;
};

class Follower {
 public:
  void begin(const uint8_t uid[6], const FollowerConfig& cfg, uint32_t nowMs) {
    copyBytes(uid_, uid, 6);
    cfg_ = cfg;
    if (cfg_.detents < 1) cfg_.detents = 1;
    if (cfg_.k4Steps < 1) cfg_.k4Steps = 1;
    for (int k = 0; k < KNOBS; k++) {
      value_[k] = clampTo<uint16_t>(cfg_.start[k], 0, KNOB_MAX);
      lane_[k] = (float)value_[k] / (float)KNOB_MAX;
      rate_[k] = 0.0f;
      touch_[k] = 0;
    }
    bootMs_ = nowMs;
    lastFrameMs_ = nowMs;
    lastReportMs_ = nowMs - REPORT_HEARTBEAT_MS;   // the first report goes out at once
    lastInfoMs_ = nowMs;
    infoDue_ = true;
    booted_ = true;
  }

  // A datagram from the hub. Returns true when it was one of ours.
  bool onPacket(const uint8_t* data, size_t len, uint32_t nowMs) {
    const uint8_t type = packetType(data, len);
    if (type == MSG_STATE) {
      State s;
      if (!decode(data, len, &s)) return false;
      // Newer than the one held, or the hub has started over (its counter
      // went back, and nothing was heard for a while).
      const bool newer = !have_ || (int16_t)(s.seq - st_.seq) > 0 ||
                         since(nowMs, lastStateMs_) > STATE_STALE_MS;
      lastStateMs_ = nowMs;
      rxStates_++;
      if (!newer) return true;
      if (!have_ || s.seq != st_.seq) ackDue_ = true;
      st_ = s;
      have_ = true;
      if (s.flags & SF_INFO_REQ) infoDue_ = true;
      return true;
    }
    if (type == MSG_CMD) {
      Cmd c;
      if (!decode(data, len, &c)) return false;
      if (c.cmd == CMD_REBOOT) rebootRequested_ = true;
      return true;
    }
    return false;
  }

  // Once per frame, with the hand's detents. Order on the device: after the
  // physical encoders are read, before the pattern sees the frame.
  void frame(const FrameIn& in, FrameOut& out) {
    out = FrameOut();
    uint32_t dt = since(in.nowMs, lastFrameMs_);
    if (dt > 100) dt = 100;
    lastFrameMs_ = in.nowMs;
    nowMs_ = in.nowMs;
    rssi_ = in.rssi;
    fps_ = in.fps;
    running_ = in.running;
    chrome_ = in.chrome;
    sleeping_ = in.sleeping;
    if (in.patternCount > 0) patternCount_ = in.patternCount > 255 ? 255 : (uint8_t)in.patternCount;

    trackPattern(in);

    const bool k4Pattern = k4IsPattern();
    const bool canDrive = in.running && !in.chrome && !in.sleeping;
    const int knobCount = k4Pattern ? KNOBS - 1 : KNOBS;

    // The hand first: it outranks anything the hub said.
    if (canDrive) {
      for (int k = 0; k < knobCount; k++) {
        out.ownKnob[k] = true;
        const int d = in.delta[k];
        if (d == 0) continue;
        const int det = clampTo<int>(detentOf(value_[k]) + d, 0, cfg_.detents);
        value_[k] = valueOf(det);
        lane_[k] = (float)value_[k] / (float)KNOB_MAX;   // a hand is felt at once
        rate_[k] = 0.0f;
        touch_[k]++;
        handMoves_++;
        dirty_ = true;
      }
      if (k4Pattern) {
        out.ownKnob[KNOBS - 1] = true;
        stepPattern(in.delta[KNOBS - 1]);
      }
    } else {
      k4Accum_ = 0;
    }

    // Then the hub, for whatever the hand has not just moved.
    if (have_) {
      for (int k = 0; k < knobCount; k++) {
        if (st_.ack[k] != touch_[k]) continue;        // sent before the hub knew the last turn
        if (st_.knob[k] == value_[k]) continue;
        value_[k] = st_.knob[k];
        const float goal = (float)value_[k] / (float)KNOB_MAX;
        rate_[k] = st_.glideMs ? absF(goal - lane_[k]) / (float)st_.glideMs : 0.0f;
        if (!st_.glideMs) lane_[k] = goal;
      }
      if (st_.pattern != NO_PATTERN && st_.patAck == patTouch_ &&
          (int)st_.pattern != desired_ && st_.pattern != refused_) {
        desired_ = st_.pattern;
        desiredFromHub_ = true;
        tries_ = 0;
      }
      if (booting()) dirty_ = true;   // the hub hears "boot is over" promptly
      everLinked_ = true;
    }

    // Glide towards the positions.
    for (int k = 0; k < KNOBS; k++) {
      const float goal = (float)value_[k] / (float)KNOB_MAX;
      const float diff = goal - lane_[k];
      if (diff == 0.0f) continue;
      const float step = rate_[k] * (float)dt;
      if (rate_[k] <= 0.0f || absF(diff) <= step) lane_[k] = goal;
      else lane_[k] += diff > 0 ? step : -step;
    }

    // A pattern still to reach: ask again, but not every frame. A registry
    // index this panel does not have would otherwise be asked for sixty times
    // a second for ever.
    if (desired_ >= 0 && desired_ != actual_ && pending_ < 0 && canDrive &&
        (tries_ == 0 || since(in.nowMs, lastAskMs_) >= PATTERN_RETRY_MS)) {
      if (tries_ >= PATTERN_TRIES) {
        if (desiredFromHub_) refused_ = (uint8_t)desired_;
        desired_ = actual_;
        patternRefusals_++;
        dirty_ = true;
      } else {
        pending_ = desired_;
        pendingFromHub_ = desiredFromHub_;
        lastAskMs_ = in.nowMs;
        tries_++;
      }
    }

    // Lanes: the positions, as an absolute 0..1 reading per knob. Not on the
    // frame a knob's button goes down - patterns act on a press only while
    // the knob is not remote-driven, and here the lane IS the hand.
    for (int k = 0; k < knobCount; k++) {
      out.laneActive[k] = !in.pressed[k];
      out.lane[k] = lane_[k];
    }
    out.lockMenus = have_ && (st_.flags & SF_LOCK_MENUS);
    out.identify = have_ && linked(in.nowMs) && (st_.flags & SF_IDENTIFY);
    out.panelId = have_ ? st_.panelId : 0;
  }

  // The pattern this panel wants to switch to, once. `fromHub` says whether
  // the hub asked (not a person's choice, so not one to remember in flash).
  bool takePattern(int* idx, bool* fromHub) {
    if (pending_ < 0) return false;
    *idx = pending_;
    if (fromHub) *fromHub = pendingFromHub_;
    handed_ = pending_;
    pending_ = -1;
    return true;
  }

  // The next datagram for the hub, or 0. Call until it returns 0.
  size_t poll(uint8_t* buf, size_t cap, uint32_t nowMs) {
    if (!booted_ || cap < MAX_PACKET) return 0;
    const uint32_t age = since(nowMs, lastReportMs_);
    const bool due = age >= REPORT_HEARTBEAT_MS || (dirty_ && age >= REPORT_MIN_MS) ||
                     (ackDue_ && age >= REPORT_ACK_MS);
    if (due) {
      dirty_ = false;
      ackDue_ = false;
      lastReportMs_ = nowMs;
      return encode(makeReport(nowMs), buf);
    }
    if (infoDue_ || since(nowMs, lastInfoMs_) >= INFO_PERIOD_MS) {
      infoDue_ = false;
      lastInfoMs_ = nowMs;
      return encode(makeInfo(), buf);
    }
    return 0;
  }

  // ── What the device and the tests may ask ─────────────────────────────
  bool linked(uint32_t nowMs) const { return have_ && since(nowMs, lastStateMs_) < LINK_TIMEOUT_MS; }
  bool everLinked() const { return everLinked_; }
  bool booting() const { return !everLinked_; }
  bool rebootRequested() const { return rebootRequested_; }
  bool k4IsPattern() const { return have_ ? (st_.flags & SF_K4_PATTERN) != 0 : cfg_.k4Pattern; }
  uint16_t value(int k) const { return value_[k]; }
  float lane(int k) const { return lane_[k]; }
  uint8_t touch(int k) const { return touch_[k]; }
  uint8_t patTouch() const { return patTouch_; }
  int desiredPattern() const { return desired_; }
  int actualPattern() const { return actual_; }
  uint8_t panelId() const { return have_ ? st_.panelId : 0; }
  uint32_t statesHeard() const { return rxStates_; }
  uint32_t handMoves() const { return handMoves_; }
  uint32_t patternRefusals() const { return patternRefusals_; }
  const State& lastState() const { return st_; }
  const uint8_t* uid() const { return uid_; }

  Report makeReport(uint32_t nowMs) {
    Report r;
    r.flags = (uint8_t)((booting() ? RF_BOOT : 0) | (sleeping_ ? RF_SLEEPING : 0) |
                        (chrome_ ? RF_CHROME : 0) | (running_ ? 0 : RF_NOT_RUNNING));
    copyBytes(r.uid, uid_, 6);
    r.seq = ++reportSeq_;
    r.ack = have_ ? st_.seq : 0;
    const int shown = desired_ >= 0 ? desired_ : actual_;
    r.pattern = shown >= 0 && shown < 255 ? (uint8_t)shown : NO_PATTERN;
    r.patTouch = patTouch_;
    for (int k = 0; k < KNOBS; k++) {
      r.knob[k] = value_[k];
      r.touch[k] = touch_[k];
    }
    r.rssi = rssi_;
    r.fps = fps_;
    r.uptimeS = since(nowMs, bootMs_) / 1000;
    return r;
  }

  Info makeInfo() const {
    Info m;
    copyBytes(m.uid, uid_, 6);
    m.pattern = actual_ >= 0 && actual_ < 255 ? (uint8_t)actual_ : NO_PATTERN;
    m.patternCount = patternCount_;
    m.detents = cfg_.detents;
    m.k4Steps = k4Steps();
    setText(m.fw, sizeof(m.fw), cfg_.fw);
    setText(m.variant, sizeof(m.variant), cfg_.variant);
    setText(m.patternName, sizeof(m.patternName), name_);
    return m;
  }

 private:
  static constexpr uint32_t REPORT_HEARTBEAT_MS = 500;
  static constexpr uint32_t REPORT_MIN_MS = 33;      // a turning knob: 30 reports a second at most
  static constexpr uint32_t REPORT_ACK_MS = 100;     // "I applied your STATE": 10 a second at most
  static constexpr uint32_t INFO_PERIOD_MS = 10000;
  static constexpr uint32_t LINK_TIMEOUT_MS = 3000;
  static constexpr uint32_t STATE_STALE_MS = 1500;
  static constexpr uint32_t PATTERN_RETRY_MS = 1000;
  static constexpr int PATTERN_TRIES = 5;

  int detentOf(uint16_t v) const {
    return (int)(((uint32_t)v * cfg_.detents + KNOB_MAX / 2) / KNOB_MAX);
  }
  uint16_t valueOf(int det) const {
    return (uint16_t)(((uint32_t)det * KNOB_MAX + cfg_.detents / 2) / cfg_.detents);
  }
  uint8_t k4Steps() const {
    return have_ && st_.k4Steps ? st_.k4Steps : cfg_.k4Steps;
  }

  // What is running, and whether somebody other than this engine chose it.
  void trackPattern(const FrameIn& in) {
    if (in.pattern != actual_) {
      const bool first = actual_ < 0;
      const bool mine = in.pattern == handed_ || in.pattern == desired_;
      actual_ = in.pattern;
      if (mine) {
        tries_ = 0;
      } else if (!first && in.pattern >= 0) {
        // The device's own browser, its web console, another feature: a
        // person picked it. It becomes this panel's pattern, and the hub is
        // told the way a knob turn is.
        desired_ = in.pattern;
        desiredFromHub_ = false;
        pending_ = -1;
        tries_ = 0;
        patTouch_++;
        dirty_ = true;
      }
      handed_ = -1;
      infoDue_ = true;
    }
    if (desired_ < 0 && actual_ >= 0) desired_ = actual_;
    // The name can arrive a frame after the index (a module still loading).
    const char* nm = in.patternName ? in.patternName : "";
    bool same = true;
    for (size_t i = 0; i < sizeof(name_); i++) {
      if (name_[i] != nm[i]) { same = false; break; }
      if (!nm[i]) break;
    }
    if (!same) {
      setText(name_, sizeof(name_), nm);
      infoDue_ = true;
    }
  }

  // K4 as the pattern switch: every k4Steps detents one pattern on, wrapping
  // inside the hub's range. A reversal cancels (the count truncates to zero).
  void stepPattern(int d) {
    if (d == 0) return;
    const int per = k4Steps();
    k4Accum_ += d;
    int steps = k4Accum_ / per;
    k4Accum_ -= steps * per;
    if (steps == 0) return;
    int lo = have_ ? st_.patMin : 0;
    int hi = have_ && st_.patMax != NO_PATTERN ? st_.patMax : (patternCount_ ? patternCount_ - 1 : -1);
    if (patternCount_ && hi > patternCount_ - 1) hi = patternCount_ - 1;
    if (hi < lo) return;                       // no range known yet
    const int span = hi - lo + 1;
    int base = desired_ >= 0 ? desired_ : actual_;
    if (base < lo || base > hi) {              // outside the range: enter it at the near end
      base = steps > 0 ? lo : hi;
      steps += steps > 0 ? -1 : 1;
    }
    const int idx = lo + (((base - lo + steps) % span) + span) % span;
    patTouch_++;                               // a hand asked, even for the same pattern
    dirty_ = true;
    desired_ = idx;
    desiredFromHub_ = false;
    refused_ = NO_PATTERN;
    tries_ = 0;
    pending_ = -1;
  }

  uint8_t uid_[6] = {0, 0, 0, 0, 0, 0};
  FollowerConfig cfg_;
  bool booted_ = false;

  uint16_t value_[KNOBS] = {500, 500, 500, 500};
  float lane_[KNOBS] = {0.5f, 0.5f, 0.5f, 0.5f};
  float rate_[KNOBS] = {0, 0, 0, 0};        // lane units per ms while gliding
  uint8_t touch_[KNOBS] = {0, 0, 0, 0};
  uint8_t patTouch_ = 0;

  int actual_ = -1;        // what the device runs
  int desired_ = -1;       // where this panel means to be
  int pending_ = -1;       // waiting for takePattern
  int handed_ = -1;        // handed over, not yet seen running
  bool desiredFromHub_ = false;
  bool pendingFromHub_ = false;
  uint8_t refused_ = NO_PATTERN;   // a hub target this panel could not load
  int tries_ = 0;
  uint32_t lastAskMs_ = 0;
  int k4Accum_ = 0;
  uint8_t patternCount_ = 0;
  char name_[24] = {0};

  State st_;
  bool have_ = false;
  bool everLinked_ = false;
  uint32_t lastStateMs_ = 0;
  uint32_t rxStates_ = 0;
  bool rebootRequested_ = false;

  uint32_t bootMs_ = 0;
  uint32_t nowMs_ = 0;
  uint32_t lastFrameMs_ = 0;
  uint32_t lastReportMs_ = 0;
  uint32_t lastInfoMs_ = 0;
  uint16_t reportSeq_ = 0;
  bool dirty_ = false;
  bool ackDue_ = false;
  bool infoDue_ = false;
  int8_t rssi_ = 0;
  uint8_t fps_ = 0;
  bool running_ = true;
  bool chrome_ = false;
  bool sleeping_ = false;
  uint32_t handMoves_ = 0;
  uint32_t patternRefusals_ = 0;
};

}  // namespace pfe
