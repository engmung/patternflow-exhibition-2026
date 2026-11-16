// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the node graph, as the hub runs it
//
// On the console a person wires nodes: a MIDI controller here, some
// arithmetic, a panel's knobs there. What reaches the hub is that graph
// flattened into a small program - registers of numbers and one instruction
// per node - and this runs it: twenty-five times a second, ONCE FOR EVERY
// PANEL, with that panel's own number, place and knobs to hand. So one
// controller can feed every panel, and a line of arithmetic on a panel's
// place turns it into a wave across the room.
//
//   in     MIDI controllers the program names (channel + number, 0..1),
//          time, the panel (number, order, place, a random number of its
//          own, its knobs as it reports them)
//   out    the panel's knobs (0..1), its pattern (a number), its brightness
//          (0..1) - each only where the program writes it, to the panels the
//          program says, and sent on only when the value changes: a hand on
//          a knob keeps it until the program's value for it moves again
//
// A controller a program names is the program's: it is not also read the
// plain way (pfe_midi.h). Everything else goes on as before.
//
// The graph has a switch, and it starts off: a program that is there but
// switched off drives nothing and takes no controller, and MIDI is read the
// plain way throughout. Nodes are for when somebody wants them.
//
// The program (bytes, from console/graph.js):
//   'P' 'F' 'G' 1, registers, controllers, then per controller
//   { channel 1..16, number 0..127, value until it is first heard 0..127 },
//   then instructions (below) up to END.
//
// No Arduino here, and no libm: the hub, the PC tests and the console's demo
// run the same arithmetic to the last bit.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pfe_conductor.h"

namespace pfe {

constexpr size_t GRAPH_MAX_BYTES = 2048;
constexpr int GRAPH_MAX_REGS = 128;
constexpr int GRAPH_MAX_CC = 32;
constexpr int GRAPH_MAX_STATE = 32;        // instructions that remember (SMOOTH, PHASE)
constexpr uint32_t GRAPH_TICK_MS = 40;
constexpr int GRAPH_GLIDE_MS = 50;         // a panel moves to each new value over about a tick
constexpr uint32_t GRAPH_MONITOR_MS = 200;

// Instructions. Operands are register numbers unless said otherwise.
enum GraphOp : uint8_t {
  G_END = 0,
  G_CONST,      // dst, float32 (little-endian)
  G_CC,         // dst, controller slot
  G_TIME,       // dst                          seconds since the program started
  G_PANEL,      // dst, what (GraphPanel)
  G_KNOB,       // dst, knob 0..3               this panel's knob as it reports it, 0..1
  G_ADD, G_SUB, G_MUL, G_DIV, G_MIN, G_MAX, G_POW, G_MOD, G_GT, G_LT,   // dst, a, b
  G_ABS, G_NEG, G_SIN, G_COS, G_FLOOR, G_ROUND, G_FRACT, G_SQRT, G_SIGN, G_NOISE,   // dst, a
  G_CLAMP,      // dst, a, lo, hi
  G_MIX,        // dst, a, b, t
  G_SELECT,     // dst, cond, a, b              cond > 0.5 ? a : b
  G_SMOOTH,     // dst, a, seconds              follows a, getting there in about that long
  G_PHASE,      // dst, rate                    0..1 round and round, `rate` times a second
  G_TARGET,     // count, then that many panel numbers (0: every panel): who the OUTs after it are for
  G_OUT,        // a, what (GraphOut)
  G_OP_COUNT
};
enum GraphPanel : uint8_t { GP_ID = 0, GP_ORDER, GP_ORDER01, GP_X, GP_Y, GP_Z, GP_RANDOM, GP_COUNT, GP_WHAT_COUNT };
enum GraphOut : uint8_t { GO_K1 = 0, GO_K2, GO_K3, GO_K4, GO_PATTERN, GO_BRIGHT, GO_WHAT_COUNT };

// ── Arithmetic without a library ────────────────────────────────────────
namespace gmath {
constexpr float GPI = 3.14159265358979f;
inline float floorF(float x) {
  if (!(x > -2.0e9f && x < 2.0e9f)) return x;
  const long i = (long)x;
  return (float)((float)i > x ? i - 1 : i);
}
inline float sinF(float x) {
  if (!(x > -1.0e6f && x < 1.0e6f)) return 0.0f;
  x -= 2.0f * GPI * floorF(x / (2.0f * GPI) + 0.5f);           // into -pi..pi
  if (x > GPI / 2) x = GPI - x;
  else if (x < -GPI / 2) x = -GPI - x;
  const float x2 = x * x;
  return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}
inline float logF(float x) {                                  // natural, for x > 0
  uint32_t bits;
  __builtin_memcpy(&bits, &x, 4);
  const int e = (int)((bits >> 23) & 255) - 127;
  bits = (bits & 0x007FFFFFu) | 0x3F800000u;
  float m;
  __builtin_memcpy(&m, &bits, 4);                             // 1..2
  const float y = (m - 1.0f) / (m + 1.0f), y2 = y * y;
  return 2.0f * y * (1.0f + y2 * (1.0f / 3 + y2 * (1.0f / 5 + y2 * (1.0f / 7 + y2 * (1.0f / 9))))) + (float)e * 0.69314718f;
}
inline float expF(float x) {
  if (x > 80.0f) return 5.0e34f;
  if (x < -80.0f) return 0.0f;
  const float n = floorF(x / 0.69314718f + 0.5f);
  const float f = x - n * 0.69314718f;                        // -0.35..0.35
  const float p = 1.0f + f * (1.0f + f * (0.5f + f * (1.0f / 6 + f * (1.0f / 24 + f * (1.0f / 120 + f * (1.0f / 720))))));
  uint32_t bits = (uint32_t)((int)n + 127) << 23;
  float scale;
  __builtin_memcpy(&scale, &bits, 4);
  return p * scale;
}
inline float powF(float a, float b) {
  if (b == 0.0f) return 1.0f;
  if (a <= 0.0f) return 0.0f;                                  // no roots of negatives here
  return expF(b * logF(a));
}
// A number between 0 and 1 that wanders smoothly as x moves along.
inline float noiseF(float x) {
  auto at = [](long i) {
    uint32_t h = (uint32_t)i * 0x9E3779B1u;
    h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
    return (float)(h & 0xFFFFFF) / 16777215.0f;
  };
  const float fl = floorF(x);
  const float f = x - fl, s = f * f * (3.0f - 2.0f * f);
  const long i = (long)fl;
  return at(i) + (at(i + 1) - at(i)) * s;
}
}  // namespace gmath

class Graph {
 public:
  void begin(Conductor* c, const ConductorIo& io) {
    c_ = c;
    io_ = io;
  }

  // ── A program arriving ────────────────────────────────────────────────
  // In pieces (the console sends base64 lines): start, more, end. `end`
  // answers with what is wrong, or nullptr and the program is running.
  void stageStart(size_t want) {
    stageLen_ = 0;
    stageWant_ = want <= GRAPH_MAX_BYTES ? want : 0;
  }
  bool stageMore(const uint8_t* data, size_t n) {
    if (!stageWant_ || stageLen_ + n > stageWant_) { stageWant_ = 0; return false; }
    copyBytes(stage_ + stageLen_, data, n);
    stageLen_ += n;
    return true;
  }
  const char* stageEnd(uint32_t nowMs) {
    if (!stageWant_ || stageLen_ != stageWant_) { stageWant_ = 0; return "the program did not arrive whole"; }
    stageWant_ = 0;
    return load(stage_, stageLen_, nowMs);
  }

  // The whole program at once (from the hub's flash, or `stageEnd`).
  const char* load(const uint8_t* bytes, size_t n, uint32_t nowMs) {
    const char* problem = check(bytes, n);
    if (problem) return problem;
    copyBytes(code_, bytes, n);
    len_ = n;
    regs_ = code_[4];
    ccCount_ = code_[5];
    for (int i = 0; i < ccCount_; i++) cc_[i] = (float)code_[6 + i * 3 + 2] / 127.0f;
    start_ = 6 + (size_t)ccCount_ * 3;
    restart(nowMs);
    changed_ = true;
    return nullptr;
  }
  void clear() {
    len_ = 0;
    changed_ = true;
  }
  bool active() const { return len_ > 0; }            // there is a program (running or not)
  // The switch. Off: nothing is driven and no controller is the graph's.
  void setEnabled(bool on, uint32_t nowMs) {
    if (on == enabled_) return;
    enabled_ = on;
    changed_ = true;
    if (on) restart(nowMs);                           // everything it writes is sent afresh
  }
  bool enabled() const { return enabled_; }
  bool running() const { return len_ > 0 && enabled_; }
  const uint8_t* bytes() const { return code_; }
  size_t size() const { return len_; }
  // The hub's glue keeps the program in flash: has it changed since this was last asked?
  bool takeChanged() { const bool was = changed_; changed_ = false; return was; }
  int controllers() const { return len_ ? ccCount_ : 0; }
  void controller(int i, int* channel, int* number) const {
    *channel = code_[6 + i * 3];
    *number = code_[6 + i * 3 + 1];
  }

  // ── MIDI in ───────────────────────────────────────────────────────────
  // A controller's value, 0..127. True if the program names it (and so it
  // is not for anybody else).
  bool onCC(int channel, int number, int value, uint32_t nowMs) {
    bool mine = false;
    for (int i = 0; len_ && enabled_ && i < ccCount_; i++) {
      if (code_[6 + i * 3] != channel || code_[6 + i * 3 + 1] != number) continue;
      cc_[i] = (float)clampTo<int>(value, 0, 127) / 127.0f;
      mine = true;
    }
    if (monitor_) sayMidi(channel, number, value, nowMs);
    return mine;
  }
  // A raw message from a MIDI port. Only controllers are a graph's business.
  bool onMidi(const uint8_t* m, size_t len, uint32_t nowMs) {
    if (len < 3 || (m[0] & 0xF0) != 0xB0) return false;
    return onCC((m[0] & 0x0F) + 1, m[1] & 0x7F, m[2] & 0x7F, nowMs);
  }

  // The console is looking: say what comes in over MIDI, and what the
  // registers hold for panel number `panelId` (0: the first one online).
  void setMonitor(bool on, int panelId) {
    monitor_ = on;
    monitorId_ = panelId;
    for (auto& s : seen_) s.ms = 0;
  }

  // ── Every pass of the hub's loop ──────────────────────────────────────
  void tick(uint32_t nowMs) {
    if (!c_ || !len_ || !enabled_) return;
    const uint32_t dtMs = since(nowMs, lastMs_);
    if (dtMs < GRAPH_TICK_MS) return;
    lastMs_ = nowMs;
    const float dt = (float)(dtMs > 250 ? 250 : dtMs) / 1000.0f;
    const float time = (float)since(nowMs, startMs_) / 1000.0f;

    int count = 0;
    for (int i = 0; i < MAX_PANELS; i++) count += c_->panel(i).used ? 1 : 0;
    const bool monitorDue = monitor_ && since(nowMs, monitorMs_) >= GRAPH_MONITOR_MS;
    bool monitored = false;

    for (int i = 0; i < MAX_PANELS; i++) {
      const Panel& p = c_->panel(i);
      if (!p.used || !p.online) continue;
      int order = 0;                                   // how many panels have a lower number
      for (int j = 0; j < MAX_PANELS; j++) order += c_->panel(j).used && c_->panel(j).id < p.id ? 1 : 0;
      float out[GO_WHAT_COUNT];
      uint8_t wrote = 0;
      run(i, p, order, count, time, dt, out, &wrote);

      int knob[KNOBS] = {-1, -1, -1, -1};
      bool any = false;
      for (int k = 0; k < KNOBS; k++) {
        if (!(wrote & (1 << k))) continue;
        const int v = clampTo<int>((int)(out[k] * (float)KNOB_MAX + 0.5f), 0, KNOB_MAX);
        if (v == sent_[i][k]) continue;
        sent_[i][k] = (int16_t)v;
        knob[k] = v;
        any = true;
      }
      int pattern = -1;
      if (wrote & (1 << GO_PATTERN)) {
        const int v = clampTo<int>((int)gmath::floorF(out[GO_PATTERN] + 0.5f), 0, NO_PATTERN - 1);
        if (v != sent_[i][GO_PATTERN]) { sent_[i][GO_PATTERN] = (int16_t)v; pattern = v; any = true; }
      }
      if (any) c_->setPanel(i, pattern, knob, GRAPH_GLIDE_MS);
      if (wrote & (1 << GO_BRIGHT)) {
        const int v = BRIGHTNESS_MIN + (int)(clampTo<float>(out[GO_BRIGHT], 0.0f, 1.0f) * (float)(255 - BRIGHTNESS_MIN) + 0.5f);
        if (v != sent_[i][GO_BRIGHT]) { sent_[i][GO_BRIGHT] = (int16_t)v; c_->setBrightness(i, v); }
      }

      if (monitorDue && !monitored && (monitorId_ == 0 || p.id == monitorId_)) {
        monitored = true;
        sayRegisters(p.id);
      }
    }
    if (monitorDue) monitorMs_ = nowMs;
  }

  // What is wrong with a program, or nullptr. Nothing that passes can read
  // or write outside its registers, its controllers or its own bytes.
  static const char* check(const uint8_t* b, size_t n) {
    if (n < 7 || n > GRAPH_MAX_BYTES) return "not a program (size)";
    if (b[0] != 'P' || b[1] != 'F' || b[2] != 'G' || b[3] != 1) return "not a program (or of another version)";
    const int regs = b[4], ccs = b[5];
    if (regs < 1 || regs > GRAPH_MAX_REGS) return "too many registers";
    if (ccs > GRAPH_MAX_CC) return "too many MIDI controllers";
    size_t pc = 6 + (size_t)ccs * 3;
    if (pc >= n) return "no instructions";
    for (int i = 0; i < ccs; i++) {
      const uint8_t* e = b + 6 + i * 3;
      if (e[0] < 1 || e[0] > 16 || e[1] > 127 || e[2] > 127) return "a MIDI controller out of range";
    }
    int states = 0;
    for (;;) {
      if (pc >= n) return "no END";
      const uint8_t op = b[pc];
      if (op == G_END) return pc + 1 == n ? nullptr : "bytes after END";
      if (op >= G_OP_COUNT) return "an instruction this hub does not know";
      int regOps = 0, extra = 0;                       // operands that are registers, and bytes that are not
      if (op == G_CONST) { regOps = 1; extra = 4; }
      else if (op == G_CC) { regOps = 1; extra = 1; }
      else if (op == G_TIME) regOps = 1;
      else if (op == G_PANEL || op == G_KNOB) { regOps = 1; extra = 1; }
      else if (op >= G_ADD && op <= G_LT) regOps = 3;
      else if (op >= G_ABS && op <= G_NOISE) regOps = 2;
      else if (op == G_CLAMP || op == G_MIX || op == G_SELECT) regOps = 4;
      else if (op == G_SMOOTH) { regOps = 3; states++; }
      else if (op == G_PHASE) { regOps = 2; states++; }
      else if (op == G_TARGET) {
        if (pc + 1 >= n) return "cut short";
        extra = 1 + b[pc + 1];
      } else if (op == G_OUT) { regOps = 1; extra = 1; }
      if (pc + 1 + (size_t)regOps + (size_t)extra > n) return "cut short";
      for (int r = 0; r < regOps; r++)
        if (b[pc + 1 + r] >= regs) return "a register out of range";
      const uint8_t arg = extra ? b[pc + 1 + regOps] : 0;
      if (op == G_CC && arg >= ccs) return "a MIDI controller the program does not list";
      if (op == G_PANEL && arg >= GP_WHAT_COUNT) return "an unknown thing about a panel";
      if (op == G_KNOB && arg >= KNOBS) return "a knob out of range";
      if (op == G_OUT && arg >= GO_WHAT_COUNT) return "an unknown output";
      if (states > GRAPH_MAX_STATE) return "too many nodes that remember (smooth, LFO)";
      pc += 1 + (size_t)regOps + (size_t)extra;
    }
  }

 private:
  void restart(uint32_t nowMs) {
    startMs_ = nowMs;
    lastMs_ = nowMs - GRAPH_TICK_MS;
    for (int i = 0; i < MAX_PANELS; i++) {
      for (int s = 0; s < GRAPH_MAX_STATE; s++) { state_[i][s] = 0.0f; fresh_[i][s] = true; }
      for (int o = 0; o < GO_WHAT_COUNT; o++) sent_[i][o] = -1;
    }
  }

  static float random01(const uint8_t uid[6]) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= uid[i]; h *= 16777619u; }
    h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
    return (float)(h & 0xFFFFFF) / 16777215.0f;
  }

  // The program once, for one panel.
  void run(int index, const Panel& p, int order, int count, float time, float dt, float* out, uint8_t* wrote) {
    float* r = reg_;
    for (int i = 0; i < regs_; i++) r[i] = 0.0f;
    const uint8_t* b = code_;
    size_t pc = start_;
    int state = 0;
    bool mine = true;                                  // the panel is among those the OUTs are for
    for (;;) {
      const uint8_t op = b[pc];
      const uint8_t* a = b + pc + 1;
      switch (op) {
        case G_END: return;
        case G_CONST: { float v; __builtin_memcpy(&v, a + 1, 4); r[a[0]] = v; pc += 6; break; }
        case G_CC: r[a[0]] = cc_[a[1]]; pc += 3; break;
        case G_TIME: r[a[0]] = time; pc += 2; break;
        case G_PANEL: {
          float v = 0.0f;
          switch (a[1]) {
            case GP_ID: v = (float)p.id; break;
            case GP_ORDER: v = (float)order; break;
            case GP_ORDER01: v = count > 1 ? (float)order / (float)(count - 1) : 0.0f; break;
            case GP_X: v = p.x; break;
            case GP_Y: v = p.y; break;
            case GP_Z: v = p.z; break;
            case GP_RANDOM: v = random01(p.uid); break;
            default: v = (float)count; break;
          }
          r[a[0]] = v;
          pc += 3;
          break;
        }
        case G_KNOB: r[a[0]] = (float)p.rep.knob[a[1]] / (float)KNOB_MAX; pc += 3; break;
        case G_ADD: r[a[0]] = r[a[1]] + r[a[2]]; pc += 4; break;
        case G_SUB: r[a[0]] = r[a[1]] - r[a[2]]; pc += 4; break;
        case G_MUL: r[a[0]] = r[a[1]] * r[a[2]]; pc += 4; break;
        case G_DIV: r[a[0]] = r[a[2]] != 0.0f ? r[a[1]] / r[a[2]] : 0.0f; pc += 4; break;
        case G_MIN: r[a[0]] = r[a[1]] < r[a[2]] ? r[a[1]] : r[a[2]]; pc += 4; break;
        case G_MAX: r[a[0]] = r[a[1]] > r[a[2]] ? r[a[1]] : r[a[2]]; pc += 4; break;
        case G_POW: r[a[0]] = gmath::powF(r[a[1]], r[a[2]]); pc += 4; break;
        case G_MOD: r[a[0]] = r[a[2]] != 0.0f ? r[a[1]] - r[a[2]] * gmath::floorF(r[a[1]] / r[a[2]]) : 0.0f; pc += 4; break;
        case G_GT: r[a[0]] = r[a[1]] > r[a[2]] ? 1.0f : 0.0f; pc += 4; break;
        case G_LT: r[a[0]] = r[a[1]] < r[a[2]] ? 1.0f : 0.0f; pc += 4; break;
        case G_ABS: r[a[0]] = absF(r[a[1]]); pc += 3; break;
        case G_NEG: r[a[0]] = -r[a[1]]; pc += 3; break;
        case G_SIN: r[a[0]] = gmath::sinF(r[a[1]]); pc += 3; break;
        case G_COS: r[a[0]] = gmath::sinF(r[a[1]] + gmath::GPI / 2); pc += 3; break;
        case G_FLOOR: r[a[0]] = gmath::floorF(r[a[1]]); pc += 3; break;
        case G_ROUND: r[a[0]] = gmath::floorF(r[a[1]] + 0.5f); pc += 3; break;
        case G_FRACT: r[a[0]] = r[a[1]] - gmath::floorF(r[a[1]]); pc += 3; break;
        case G_SQRT: r[a[0]] = r[a[1]] > 0.0f ? sqrtF(r[a[1]]) : 0.0f; pc += 3; break;
        case G_SIGN: r[a[0]] = r[a[1]] > 0.0f ? 1.0f : (r[a[1]] < 0.0f ? -1.0f : 0.0f); pc += 3; break;
        case G_NOISE: r[a[0]] = gmath::noiseF(r[a[1]]); pc += 3; break;
        case G_CLAMP: {
          const float lo = r[a[2]] < r[a[3]] ? r[a[2]] : r[a[3]], hi = r[a[2]] < r[a[3]] ? r[a[3]] : r[a[2]];
          r[a[0]] = r[a[1]] < lo ? lo : (r[a[1]] > hi ? hi : r[a[1]]);
          pc += 5;
          break;
        }
        case G_MIX: r[a[0]] = r[a[1]] + (r[a[2]] - r[a[1]]) * r[a[3]]; pc += 5; break;
        case G_SELECT: r[a[0]] = r[a[1]] > 0.5f ? r[a[2]] : r[a[3]]; pc += 5; break;
        case G_SMOOTH: {
          float& s = state_[index][state];
          const float goal = r[a[1]], secs = r[a[2]];
          if (fresh_[index][state] || !(secs > 0.001f)) s = goal;            // starts where its input is
          else s += (goal - s) * (dt >= secs ? 1.0f : 1.0f - gmath::expF(-3.0f * dt / secs));
          fresh_[index][state] = false;
          r[a[0]] = s;
          state++;
          pc += 4;
          break;
        }
        case G_PHASE: {
          float& s = state_[index][state];
          if (!fresh_[index][state]) s += r[a[1]] * dt;
          fresh_[index][state] = false;
          s -= gmath::floorF(s);
          r[a[0]] = s;
          state++;
          pc += 3;
          break;
        }
        case G_TARGET: {
          mine = a[0] == 0;
          for (int i = 0; i < a[0]; i++) mine = mine || a[1 + i] == p.id;
          pc += 2 + a[0];
          break;
        }
        case G_OUT:
          if (mine) { out[a[1]] = r[a[0]]; *wrote |= (uint8_t)(1 << a[1]); }
          pc += 3;
          break;
        default: return;                               // check() let none of these through
      }
    }
  }

  // ── What the console sees while it is looking ─────────────────────────
  void sayMidi(int channel, int number, int value, uint32_t nowMs) {
    // A turning controller sends a hundred a second; twenty-five a second of
    // each is plenty to see it by.
    int slot = 0;
    for (int i = 0; i < SEEN; i++) {
      if (seen_[i].ms && seen_[i].channel == channel && seen_[i].number == number) { slot = i; break; }
      if (seen_[i].ms == 0 || since(nowMs, seen_[i].ms) > since(nowMs, seen_[slot].ms)) slot = i;
    }
    Seen& s = seen_[slot];
    const bool same = s.ms && s.channel == channel && s.number == number;
    if (same && since(nowMs, s.ms) < 40) { s.held = (int8_t)value; return; }
    s.channel = (uint8_t)channel;
    s.number = (uint8_t)number;
    s.ms = nowMs ? nowMs : 1;
    s.held = -1;
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"mi\",\"ch\":").i32(channel).str(",\"cc\":").i32(number).str(",\"v\":").i32(value).ch('}');
    if (!o.overflow && io_.line) io_.line(io_.ctx, o.buf, o.len);
  }
  void sayRegisters(int panelId) {
    // The value a controller stopped on, if the last line about it said an earlier one.
    for (auto& s : seen_) {
      if (s.ms && s.held >= 0) {
        Out o(line_, sizeof(line_));
        o.str("{\"t\":\"mi\",\"ch\":").i32(s.channel).str(",\"cc\":").i32(s.number).str(",\"v\":").i32(s.held).ch('}');
        if (!o.overflow && io_.line) io_.line(io_.ctx, o.buf, o.len);
        s.held = -1;
      }
    }
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"nv\",\"p\":").i32(panelId).str(",\"v\":[");
    for (int i = 0; i < regs_; i++) { if (i) o.ch(','); o.num(reg_[i]); }
    o.str("]}");
    if (!o.overflow && io_.line) io_.line(io_.ctx, o.buf, o.len);
  }

  Conductor* c_ = nullptr;
  ConductorIo io_;
  uint8_t code_[GRAPH_MAX_BYTES];
  size_t len_ = 0, start_ = 0;
  int regs_ = 0, ccCount_ = 0;
  float cc_[GRAPH_MAX_CC];
  float reg_[GRAPH_MAX_REGS];
  float state_[MAX_PANELS][GRAPH_MAX_STATE];
  bool fresh_[MAX_PANELS][GRAPH_MAX_STATE];
  int16_t sent_[MAX_PANELS][GO_WHAT_COUNT];        // what this graph last gave each panel (-1: nothing yet)
  uint32_t startMs_ = 0, lastMs_ = 0;
  bool changed_ = false;
  bool enabled_ = false;

  uint8_t stage_[GRAPH_MAX_BYTES];
  size_t stageLen_ = 0, stageWant_ = 0;

  bool monitor_ = false;
  int monitorId_ = 0;
  uint32_t monitorMs_ = 0;
  static constexpr int SEEN = 8;
  struct Seen { uint8_t channel = 0, number = 0; int8_t held = -1; uint32_t ms = 0; };
  Seen seen_[SEEN];
  char line_[1024];
};

}  // namespace pfe
