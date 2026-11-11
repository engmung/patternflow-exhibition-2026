// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - a whole installation with no hardware
//
// One Conductor, up to MAX_PANELS Followers, and a model of the air between
// them: every datagram is delayed, jittered and sometimes dropped. The
// conductor and the followers are the firmware's own code, not a copy, so
// what happens here is what the hub and the panels will do.
//
// Used two ways:
//   - ensemble/tests/test_core.cpp scripts it and checks what comes out;
//   - ensemble/core/pfe_wasm.cpp exports it, and the console's demo mode
//     runs it in the browser.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "pfe_console.h"
#include "pfe_follower.h"

namespace pfe {

constexpr int SIM_QUEUE = 1024;          // datagrams in the air
constexpr int SIM_PATTERNS = 8;          // every virtual panel carries the same eight
constexpr size_t SIM_OUT = 96 * 1024;    // console lines waiting to be read

inline const char* simPatternName(int i) {
  static const char* const names[SIM_PATTERNS] = {
      "Origin", "Wave Saw", "Lattice", "Bloom", "Drift", "Tide", "Ember", "Static"};
  return i >= 0 && i < SIM_PATTERNS ? names[i] : "";
}

struct SimPanel {
  Follower f;
  uint8_t uid[6] = {0, 0, 0, 0, 0, 0};
  bool powered = false;
  int actual = 0;                 // the pattern its LEDs show
  int delta[KNOBS] = {0, 0, 0, 0};
  bool press[KNOBS] = {false, false, false, false};
  uint32_t nextFrameMs = 0;
  FrameOut out;
  uint32_t boots = 0;
};

struct SimPacket {
  bool used = false;
  uint32_t dueMs = 0;
  int8_t to = 0;                  // -1 = the hub, else a panel
  int8_t from = 0;
  uint8_t len = 0;
  uint8_t data[MAX_PACKET];
};

class Sim {
 public:
  Sim() : console_(conductor_, io(), "\"fw\":\"sim\",\"hub\":\"demo\"") {}

  void begin(int panels, uint32_t seed, uint32_t nowMs) {
    rng_ = seed ? seed : 1;
    nowMs_ = nowMs;
    count_ = clampTo<int>(panels, 0, MAX_PANELS);
    conductor_.begin(io(), nowMs);
    for (int i = 0; i < count_; i++) {
      SimPanel& p = panels_[i];
      // A locally administered MAC per virtual panel.
      const uint8_t uid[6] = {0x02, 0x50, 0x46, 0x00, (uint8_t)(i >> 8), (uint8_t)(i + 1)};
      copyBytes(p.uid, uid, 6);
      p.actual = 0;
    }
    // Switched on one after another, so the hub numbers them in order.
    for (int i = 0; i < count_; i++) {
      power(i, true);
      tick(nowMs_ + 20);
    }
  }

  // Run the installation up to `nowMs`, a millisecond at a time.
  void tick(uint32_t nowMs) {
    // begin() runs the clock on a little while the panels come up; a caller
    // whose own clock is still behind that waits for it to catch up.
    if ((int32_t)(nowMs - nowMs_) <= 0) return;
    uint32_t steps = since(nowMs, nowMs_);
    // A browser tab that slept: the whole installation was frozen with it,
    // so skip the gap rather than replay it. (The tests step a second at a time.)
    if (steps > 5000) { nowMs_ = nowMs - 5000; steps = 5000; }
    for (uint32_t s = 0; s < steps; s++) {
      nowMs_++;
      deliver();
      conductor_.tick(nowMs_);
      for (int i = 0; i < count_; i++) frameIfDue(i);
    }
  }

  // ── What a person or a venue does ─────────────────────────────────────
  void hand(int i, int knob, int detents) {
    if (i < 0 || i >= count_ || knob < 0 || knob >= KNOBS) return;
    panels_[i].delta[knob] += detents;
  }
  void press(int i, int knob) {
    if (i < 0 || i >= count_ || knob < 0 || knob >= KNOBS) return;
    panels_[i].press[knob] = true;
  }
  // Somebody picks a pattern on the panel's own menu or web console.
  void pick(int i, int pattern) {
    if (i < 0 || i >= count_ || pattern < 0 || pattern >= SIM_PATTERNS) return;
    panels_[i].actual = pattern;
  }
  // Pull the plug, or put it back: the panel starts over from its defaults.
  void power(int i, bool on) {
    if (i < 0 || i >= count_) return;
    SimPanel& p = panels_[i];
    if (on && !p.powered) {
      p.f = Follower();
      FollowerConfig cfg;
      cfg.fw = "3.12.0";
      cfg.variant = "ensemble sim";
      p.f.begin(p.uid, cfg, nowMs_);
      p.actual = 0;
      p.nextFrameMs = nowMs_ + 1 + (uint32_t)i % 16;   // panels do not share a frame clock
      p.out = FrameOut();
      p.boots++;
      for (int k = 0; k < KNOBS; k++) { p.delta[k] = 0; p.press[k] = false; }
    }
    p.powered = on;
  }
  void network(uint32_t latencyMs, uint32_t jitterMs, uint32_t lossPermille) {
    latencyMs_ = latencyMs;
    jitterMs_ = jitterMs;
    lossPermille_ = lossPermille > 1000 ? 1000 : lossPermille;
  }
  // Take the hub away and bring a new one, with what the old one had saved.
  void restartHub() {
    static uint8_t blob[SAVE_MAX];
    const size_t n = conductor_.save(blob, sizeof(blob));
    conductor_.reset();
    conductor_.begin(io(), nowMs_);
    if (n) conductor_.load(blob, n);
  }

  void command(const char* line, size_t len) { console_.handle(line, len); }

  // ── What can be seen ──────────────────────────────────────────────────
  int count() const { return count_; }
  Conductor& conductor() { return conductor_; }
  const SimPanel& panel(int i) const { return panels_[i]; }
  SimPanel& panel(int i) { return panels_[i]; }
  uint32_t now() const { return nowMs_; }
  const char* out() const { return out_; }
  size_t outLen() const { return outLen_; }
  void outClear() { outLen_ = 0; out_[0] = 0; }
  uint32_t sent() const { return sent_; }
  uint32_t dropped() const { return dropped_; }

 private:
  ConductorIo io() {
    ConductorIo c;
    c.ctx = this;
    c.send = [](void* ctx, const Endpoint& to, const uint8_t* data, size_t len) {
      Sim* self = (Sim*)ctx;
      self->air(-1, (int)(to.port - 50000), data, len);
    };
    c.line = [](void* ctx, const char* text, size_t len) {
      Sim* self = (Sim*)ctx;
      if (self->outLen_ + len + 2 >= SIM_OUT) return;   // nobody is reading: drop, state repeats
      copyBytes(self->out_ + self->outLen_, text, len);
      self->outLen_ += len;
      self->out_[self->outLen_++] = '\n';
      self->out_[self->outLen_] = 0;
    };
    return c;
  }

  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  void air(int from, int to, const uint8_t* data, size_t len) {
    if (len > MAX_PACKET || to < -1 || to >= count_) return;
    sent_++;
    if (lossPermille_ && random() % 1000 < lossPermille_) { dropped_++; return; }
    for (int i = 0; i < SIM_QUEUE; i++) {
      SimPacket& q = queue_[(next_ + i) % SIM_QUEUE];
      if (q.used) continue;
      q.used = true;
      q.dueMs = nowMs_ + latencyMs_ + (jitterMs_ ? random() % (jitterMs_ + 1) : 0);
      q.from = (int8_t)from;
      q.to = (int8_t)to;
      q.len = (uint8_t)len;
      copyBytes(q.data, data, len);
      next_ = (next_ + i + 1) % SIM_QUEUE;
      inAir_++;
      return;
    }
    dropped_++;   // the air is full
  }

  void deliver() {
    if (!inAir_) return;
    for (int i = 0; i < SIM_QUEUE; i++) {
      SimPacket& q = queue_[i];
      if (!q.used || (int32_t)(nowMs_ - q.dueMs) < 0) continue;
      q.used = false;
      inAir_--;
      if (q.to < 0) {
        Endpoint ep;
        ep.ip = 0xC0A84202u + (uint32_t)q.from;      // 192.168.66.2 and up
        ep.port = (uint16_t)(50000 + q.from);
        conductor_.onPacket(ep, q.data, q.len, nowMs_);
      } else if (panels_[q.to].powered) {
        panels_[q.to].f.onPacket(q.data, q.len, nowMs_);
      }
    }
  }

  // One 60 fps frame of a virtual panel: what features/ensemble does on the
  // device, with a pattern switch that always succeeds for a pattern it has.
  void frameIfDue(int i) {
    SimPanel& p = panels_[i];
    if (!p.powered || (int32_t)(nowMs_ - p.nextFrameMs) < 0) return;
    p.nextFrameMs += 16;
    FrameIn in;
    in.nowMs = nowMs_;
    for (int k = 0; k < KNOBS; k++) {
      in.delta[k] = p.delta[k];
      in.pressed[k] = p.press[k];
      p.delta[k] = 0;
      p.press[k] = false;
    }
    in.pattern = p.actual;
    in.patternCount = SIM_PATTERNS;
    in.patternName = simPatternName(p.actual);
    in.rssi = (int8_t)(-45 - (i * 7) % 30);
    in.fps = 60;
    p.out = FrameOut();
    p.f.frame(in, p.out);
    int idx;
    bool fromHub;
    if (p.f.takePattern(&idx, &fromHub) && idx >= 0 && idx < SIM_PATTERNS) p.actual = idx;
    uint8_t buf[MAX_PACKET];
    for (size_t n; (n = p.f.poll(buf, sizeof(buf), nowMs_)) != 0;) air(i, -1, buf, n);
    if (p.f.rebootRequested()) { power(i, false); power(i, true); }
  }

  Conductor conductor_;
  Console console_;
  SimPanel panels_[MAX_PANELS];
  SimPacket queue_[SIM_QUEUE];
  int count_ = 0;
  int next_ = 0;
  int inAir_ = 0;
  uint32_t nowMs_ = 0;
  uint32_t rng_ = 1;
  uint32_t latencyMs_ = 3;
  uint32_t jitterMs_ = 4;
  uint32_t lossPermille_ = 0;
  uint32_t sent_ = 0;
  uint32_t dropped_ = 0;
  char out_[SIM_OUT];
  size_t outLen_ = 0;
};

}  // namespace pfe
