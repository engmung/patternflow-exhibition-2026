// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - MIDI, the hub's side
//
// The ensemble speaks the MIDI of one Patternflow panel (Patternflow
// docs/midi-spec.md, the Audio edition's mapping, the original), once per
// panel: a panel's MIDI channel is its number. The hub carries it for all of
// them, so the panels need nothing; on the hub it is a USB-MIDI device
// beside the console (hub/src/main.cpp), as FlowLocal was in the 2026
// exhibition.
//
//   Ensemble -> host (Live, for sound)
//     CC 24-27  knob 1-4 of panel n on channel n, 0..127, whenever what that
//               panel is to show changes: a hand on it, a value of another
//               panel arriving (at once, or when a wave reaches it), a scene,
//               the console. A wave is heard crossing the room.
//     Program Change  the pattern of panel n, on channel n.
//     Channel 16      the ensemble as one instrument: the value someone just
//                     turned anywhere (or the console sent), as it leaves.
//   Host -> ensemble (a DAW, a controller)
//     Channel n (1-15) is panel n; channel 16 is every panel.
//     CC 20-23  knob 1-4, absolute, 0..127 onto 0..1000
//     CC 24-27  knob 1-4, relative: 64 = no motion, 65 = one detent up
//     CC 28     brightness, 0..127 onto 5..255 (the ensemble's own)
//     Program Change  pattern
//     Anything else, and a channel that is no panel, is ignored. Nothing a
//     host sends comes back to it (no MIDI Thru).
//
// What goes to the host is state, not a queue: per channel and control, the
// value to say and the value the port last took. A port that takes sixteen
// messages at a time (USB-MIDI on the hub: 64 bytes) gets a scene of thirty
// in two goes and loses none, and one nobody is listening to is told, when
// somebody does, where every knob is now and not where it has been.
//
// No Arduino here: the hub hands it raw messages and takes raw messages back,
// and the PC tests run it against the simulated installation.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pfe_conductor.h"

namespace pfe {

constexpr uint8_t MIDI_ALL_CHANNEL = 16;        // every panel (host -> ensemble), the ensemble (-> host)
constexpr uint8_t MIDI_CC_KNOB_ABS = 20;        // 20-23
constexpr uint8_t MIDI_CC_KNOB_REL = 24;        // 24-27 in, and the knobs out
constexpr uint8_t MIDI_CC_BRIGHTNESS = 28;
constexpr int MIDI_DETENTS = 32;                 // a relative step is one detent of a panel (PF_ENSEMBLE_KNOB_DETENTS)

class Midi {
 public:
  // out: one message (2 or 3 bytes, status first). Returns whether the port
  // took it; one it did not take is offered again by flush().
  using Out = bool (*)(void* ctx, const uint8_t* msg, size_t len);

  void begin(Conductor* c, Out out, void* ctx) {
    c_ = c;
    out_ = out;
    ctx_ = ctx;
    for (int ch = 0; ch < 16; ch++)
      for (int k = 0; k <= KNOBS; k++) want_[ch][k] = told_[ch][k] = NONE;
    pending_ = 0;
    next_ = 0;
    ConductorHooks h;
    h.ctx = this;
    h.target = [](void* self, int index, uint8_t id, int ch, uint16_t value) {
      static_cast<Midi*>(self)->onTarget(index, id, ch, value);
    };
    h.emitted = [](void* self, int ch, uint16_t value, int origin) {
      static_cast<Midi*>(self)->onEmitted(ch, value, origin);
    };
    c_->setHooks(h);
  }

  // A message from the host. Returns whether it meant something.
  bool onMessage(const uint8_t* m, size_t len) {
    if (!c_ || len < 2) return false;
    const uint8_t type = m[0] & 0xF0, channel = (uint8_t)((m[0] & 0x0F) + 1);
    const bool all = channel == MIDI_ALL_CHANNEL;
    const int index = all ? -1 : c_->findPanelById(channel);
    if (!all && index < 0) return false;
    fromHost_ = true;   // what this changes is not sent back
    bool used = true;
    if (type == 0xC0) {
      setPattern(index, m[1]);
    } else if (type == 0xB0 && len >= 3) {
      const uint8_t cc = m[1], v = m[2] & 0x7F;
      if (cc >= MIDI_CC_KNOB_ABS && cc < MIDI_CC_KNOB_ABS + KNOBS) {
        setKnob(index, cc - MIDI_CC_KNOB_ABS, (v * KNOB_MAX + 63) / 127);
      } else if (cc >= MIDI_CC_KNOB_REL && cc < MIDI_CC_KNOB_REL + KNOBS) {
        nudgeKnob(index, cc - MIDI_CC_KNOB_REL, (int)v - 64);
      } else if (cc == MIDI_CC_BRIGHTNESS) {
        const int b = BRIGHTNESS_MIN + (v * (255 - BRIGHTNESS_MIN) + 63) / 127;
        if (all) c_->setAllBrightness(b);
        else c_->setBrightness(index, b);
      } else {
        used = false;
      }
    } else {
      used = false;
    }
    fromHost_ = false;
    if (used) received_++;
    return used;
  }

  // Says what is still to be said, as far as the port takes it. Saying
  // something calls this; the hub calls it every pass of its loop as well,
  // for what the port had no room for.
  void flush() {
    if (!out_) return;
    for (int n = 0; n < SLOTS && pending_ > 0; n++) {
      const int at = (next_ + n) % SLOTS;
      const int channel = at / (KNOBS + 1), k = at % (KNOBS + 1);
      const uint8_t v = want_[channel][k];
      if (v == NONE || v == told_[channel][k]) continue;
      uint8_t msg[3];
      size_t len;
      if (k == KNOBS) {
        msg[0] = (uint8_t)(0xC0 | channel);
        msg[1] = v;
        len = 2;
      } else {
        msg[0] = (uint8_t)(0xB0 | channel);
        msg[1] = (uint8_t)(MIDI_CC_KNOB_REL + k);
        msg[2] = v;
        len = 3;
      }
      if (!out_(ctx_, msg, len)) {
        next_ = at;      // the port is full: go on from here, so that every channel gets its turn
        return;
      }
      told_[channel][k] = v;
      pending_--;
      sentCount_++;
    }
  }

  uint32_t received() const { return received_; }
  uint32_t sent() const { return sentCount_; }
  int pending() const { return pending_; }      // values the port has not taken yet

 private:
  static constexpr uint8_t NONE = 0xFF;                 // no value yet (MIDI's are 0..127)
  static constexpr int SLOTS = 16 * (KNOBS + 1);

  void setPattern(int index, int pattern) {
    const int none[KNOBS] = {-1, -1, -1, -1};
    if (index < 0) c_->setAll(pattern, none, false, -1, -1);
    else c_->setPanel(index, pattern, none, -1);
  }

  void setKnob(int index, int k, int value) {
    int knob[KNOBS] = {-1, -1, -1, -1};
    knob[k] = value;
    if (index < 0) c_->setAll(-1, knob, false, -1, -1);
    else c_->setPanel(index, -1, knob, -1);
  }

  // Relative: each panel moves from where it is.
  void nudgeKnob(int index, int k, int detents) {
    if (detents == 0) return;
    for (int i = 0; i < MAX_PANELS; i++) {
      if (index >= 0 && i != index) continue;
      const Panel& p = c_->panel(i);
      if (!p.used) continue;
      int knob[KNOBS] = {-1, -1, -1, -1};
      knob[k] = clampTo<int>((int)p.tKnob[k] + detents * KNOB_MAX / MIDI_DETENTS, 0, KNOB_MAX);
      c_->setPanel(i, -1, knob, -1);
    }
  }

  // What the host itself set is not sent back to it, but remembered as what
  // it knows, so that a hand bringing a knob back to an earlier value is
  // still told. A pattern the host chose does come back, as every pattern
  // change does (midi-spec.md: "A Program Change from the host comes back").
  void onTarget(int, uint8_t id, int ch, uint16_t value) {
    if (id < 1 || id >= MIDI_ALL_CHANNEL) return;
    send(id, ch, value, !fromHost_ || ch == KNOBS);
  }

  void onEmitted(int ch, uint16_t value, int) {
    send(MIDI_ALL_CHANNEL, ch, value, !fromHost_ || ch == KNOBS);
  }

  // One knob or the pattern on a channel: what is to be said there from now
  // on. `out` false: the host said it itself, so it knows.
  void send(uint8_t channel, int ch, uint16_t value, bool out) {
    if (channel < 1 || channel > 16 || ch < 0 || ch > KNOBS) return;
    uint8_t v;
    if (ch == KNOBS) {
      if (value > 127) return;
      v = (uint8_t)value;
    } else {
      v = (uint8_t)((value * 127 + KNOB_MAX / 2) / KNOB_MAX);
    }
    uint8_t& want = want_[channel - 1][ch];
    uint8_t& told = told_[channel - 1][ch];
    const bool was = want != NONE && want != told;
    want = v;
    if (!out) told = v;
    const bool is = want != told;
    pending_ += (is ? 1 : 0) - (was ? 1 : 0);
    if (is) flush();
  }

  Conductor* c_ = nullptr;
  Out out_ = nullptr;
  void* ctx_ = nullptr;
  bool fromHost_ = false;
  uint8_t want_[16][KNOBS + 1];    // per channel and knob/pattern: the value to say (NONE: nothing yet)
  uint8_t told_[16][KNOBS + 1];    // ... and the value the port last took, or the host itself said
  int pending_ = 0;                // how many of them differ
  int next_ = 0;                   // where flush() goes on
  uint32_t received_ = 0;
  uint32_t sentCount_ = 0;
};

}  // namespace pfe
