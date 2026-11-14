// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the hub's side
//
// The hub keeps, for every panel it has ever met, where that panel SHOULD
// be: a pattern and four knob positions. Panels converge on that; the
// console edits it; and a hand on any panel edits it for the others,
// according to the link of the channel it touched:
//
//   solo     the turn stays on that panel
//   sync     every linked panel takes the value at once
//   ripple   the value leaves the panel that was turned and reaches each
//            other panel after  distance / speed  - distances taken from the
//            positions set on the console's map
//
// Five channels have a link each: K1..K4 and the pattern.
//
// A ripple is a list of values still travelling. Each panel takes, per
// channel, the most recently EMITTED value that has ARRIVED at it; so two
// people turning two panels cross over cleanly, and once everything has
// landed every panel shows the last thing anybody did.
//
// No Arduino, no heap, no libc: ensemble/hub wraps this in Wi-Fi, UDP and
// USB; the tests and the console's demo drive it directly.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "pfe_proto.h"

namespace pfe {

constexpr int MAX_PANELS = 24;
constexpr int CHANNELS = 5;          // 0..3 = K1..K4, 4 = the pattern
constexpr int CH_PATTERN = 4;
constexpr int MAX_EVENTS = 384;      // values in flight
constexpr int SCENES = 8;
constexpr int MAX_NAMES = 48;        // pattern names learned from the panels
constexpr size_t NAME_LEN = 24;
constexpr size_t SAVE_MAX = 4096;    // the most save() writes

constexpr uint32_t PANEL_TIMEOUT_MS = 3000;    // six missed heartbeats
constexpr uint32_t MAX_DELAY_MS = 30000;       // no wave takes longer than this to cross
constexpr uint32_t IDENTIFY_MS = 30000;        // an identify left on switches itself off
constexpr uint32_t STALE_WINDOW_MS = 2000;     // an older report this soon after a newer one came late
constexpr uint32_t REJOIN_HAND_MS = 10000;     // back within this, a turn made while away still counts

enum Link : uint8_t { LINK_SOLO = 0, LINK_SYNC = 1, LINK_RIPPLE = 2 };

inline const char* linkName(uint8_t l) {
  return l == LINK_SYNC ? "sync" : (l == LINK_RIPPLE ? "ripple" : "solo");
}

// Where a panel is reached. The hub's Wi-Fi fills it from the datagram's
// sender; the core only hands it back.
struct Endpoint {
  uint32_t ip = 0;       // a.b.c.d as (a<<24)|(b<<16)|(c<<8)|d
  uint16_t port = 0;
};

struct Config {
  uint8_t link[CHANNELS] = {LINK_RIPPLE, LINK_RIPPLE, LINK_RIPPLE, LINK_RIPPLE, LINK_RIPPLE};
  float speed = 2.0f;        // map units per second
  uint16_t glideMs = 80;     // how a panel eases into a value that arrived
  bool k4Pattern = true;     // K4 steps the pattern; false = K4 is a fourth knob
  bool lockMenus = false;    // the panels' long-press screens stay shut
  uint8_t patMin = 0;        // K4's range
  uint8_t patMax = NO_PATTERN;
  uint8_t k4Steps = 3;
};

struct SceneEntry {
  uint8_t uid[6];
  uint8_t pattern;
  uint16_t knob[KNOBS];
};

struct Scene {
  bool used = false;
  uint8_t count = 0;
  SceneEntry e[MAX_PANELS];
};

struct Panel {
  // Kept across restarts (save / load).
  bool used = false;
  uint8_t uid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t id = 0;            // the number on the map and on the panel's identify screen
  float x = 0, y = 0;
  char name[NAME_LEN] = {0};
  bool linked = true;        // takes part in sync and ripple

  // Live.
  bool online = false;
  bool everSeen = false;
  Endpoint ep;
  uint32_t lastSeenMs = 0;
  Report rep;                // the last report, whole
  bool haveTarget = false;
  uint8_t tPattern = NO_PATTERN;
  uint16_t tKnob[KNOBS] = {500, 500, 500, 500};
  uint16_t tGlide = 0;
  uint32_t applied[CHANNELS] = {0, 0, 0, 0, 0};   // the event each channel's target came from
  uint16_t stateSeq = 0;
  uint8_t sent[STATE_LEN] = {0};   // the last STATE sent, to tell a change from a refresh
  bool sentAny = false;
  uint32_t lastSendMs = 0;
  bool identify = false;
  uint32_t identifyAtMs = 0;
  bool infoReq = false;
  char fw[16] = {0};
  char variant[16] = {0};
  char patternName[NAME_LEN] = {0};
  uint8_t patternCount = 0;
  uint8_t detents = 0;
  // Counters for the console.
  uint32_t reports = 0;
  uint32_t lost = 0;         // reports that never arrived, by their sequence numbers
  uint32_t restarts = 0;
  uint32_t hands = 0;
  // Console line bookkeeping.
  bool uiDirty = false;
  uint32_t uiSentMs = 0;
};

enum EventKind : uint8_t { EV_VALUE = 0, EV_SCENE = 1 };

struct Event {
  uint32_t seq = 0;
  uint32_t tMs = 0;
  uint32_t maxDelayMs = 0;   // after this it has arrived everywhere
  float x = 0, y = 0;        // where it started
  uint16_t value = 0;        // the value, or the scene's slot
  uint16_t glideMs = 0;
  uint8_t mask = 0;          // channels it sets
  uint8_t kind = EV_VALUE;
  bool instant = false;      // arrives everywhere at once
  bool linkedOnly = false;   // a hand's wave reaches linked panels only
  int8_t origin = -1;        // the panel it started at, for the console's drawing
};

struct ConductorIo {
  void* ctx = nullptr;
  void (*send)(void* ctx, const Endpoint& to, const uint8_t* data, size_t len) = nullptr;
  void (*line)(void* ctx, const char* text, size_t len) = nullptr;   // one console line, no newline
};

struct Stats {
  uint32_t rx = 0;       // datagrams understood
  uint32_t bad = 0;      // datagrams that were not PFE/1
  uint32_t tx = 0;       // STATEs sent
  uint32_t events = 0;   // values emitted
  uint32_t dropped = 0;  // values applied early because the list was full
  uint32_t full = 0;     // reports from a panel there was no room for
  uint32_t stale = 0;    // reports that arrived after a newer one
};

class Conductor {
  // What a linked channel is showing across the ensemble: the last value
  // anybody emitted on it. A panel that joins takes this.
  struct Shared {
    bool valid = false;
    uint16_t value = 0;
    uint32_t seq = 0;
  };

 public:
  void begin(const ConductorIo& io, uint32_t nowMs) {
    io_ = io;
    nowMs_ = nowMs;
    startMs_ = nowMs;
    lastHubLineMs_ = nowMs;
    cfgDirty_ = true;
    namesDirty_ = true;
    scenesDirty_ = true;
  }

  // Back to an empty hub, in place (the object is too big to build a
  // second one on a small stack and copy it over).
  void reset() {
    cfg_ = Config();
    for (int i = 0; i < MAX_PANELS; i++) panels_[i] = Panel();
    for (int s = 0; s < SCENES; s++) { scenes_[s].used = false; scenes_[s].count = 0; }
    head_ = 0;
    count_ = 0;
    eventSeq_ = 0;
    for (int c = 0; c < CHANNELS; c++) shared_[c] = Shared();
    zeroBytes(names_, sizeof(names_));
    stats_ = Stats();
    lastWaveMs_ = 0;
    uiMinMs_ = 100;
    persistDirty_ = false;
    persistChangedMs_ = 0;
  }

  // ── The air ───────────────────────────────────────────────────────────
  void onPacket(const Endpoint& from, const uint8_t* data, size_t len, uint32_t nowMs) {
    nowMs_ = nowMs;
    const uint8_t type = packetType(data, len);
    if (type == MSG_REPORT) {
      Report r;
      if (decode(data, len, &r)) { stats_.rx++; onReport(from, r); return; }
    } else if (type == MSG_INFO) {
      Info m;
      if (decode(data, len, &m)) { stats_.rx++; onInfo(m); return; }
    }
    stats_.bad++;
  }

  void tick(uint32_t nowMs) {
    nowMs_ = nowMs;
    for (int i = 0; i < MAX_PANELS; i++) {
      Panel& p = panels_[i];
      if (!p.used) continue;
      if (p.online && since(nowMs, p.lastSeenMs) > PANEL_TIMEOUT_MS) {
        p.online = false;
        p.uiDirty = true;
        logPanel(i, "lost (no report for 3 s)");
      }
      if (p.identify && since(nowMs, p.identifyAtMs) > IDENTIFY_MS) {
        p.identify = false;
        p.uiDirty = true;
      }
    }
    if (count_ && since(nowMs, lastWaveMs_) >= WAVE_TICK_MS) {
      lastWaveMs_ = nowMs;
      runWaves();
    }
    sendStates();
    consoleLines();
  }

  // ── The console's verbs (pfe_console.h turns JSON lines into these) ───
  Config& config() { return cfg_; }
  const Config& config() const { return cfg_; }
  void configChanged() {
    cfg_.speed = clampTo<float>(cfg_.speed, 0.0f, 1000.0f);
    if (cfg_.k4Steps < 1) cfg_.k4Steps = 1;
    for (int c = 0; c < CHANNELS; c++) if (cfg_.link[c] > LINK_RIPPLE) cfg_.link[c] = LINK_SOLO;
    cfgDirty_ = true;
    touchPersist();
  }

  int findPanel(const uint8_t uid[6]) const {
    for (int i = 0; i < MAX_PANELS; i++)
      if (panels_[i].used && sameBytes(panels_[i].uid, uid, 6)) return i;
    return -1;
  }
  int findPanelById(int id) const {
    for (int i = 0; i < MAX_PANELS; i++)
      if (panels_[i].used && panels_[i].id == id) return i;
    return -1;
  }
  const Panel& panel(int i) const { return panels_[i]; }
  int panelCount() const {
    int n = 0;
    for (int i = 0; i < MAX_PANELS; i++) n += panels_[i].used ? 1 : 0;
    return n;
  }
  int onlineCount() const {
    int n = 0;
    for (int i = 0; i < MAX_PANELS; i++) n += panels_[i].used && panels_[i].online ? 1 : 0;
    return n;
  }

  // Where a panel sits on the map, what it is called, whether it follows.
  // A field left at its "keep" value is not changed.
  bool editPanel(int i, const float* x, const float* y, const int* id, const char* name,
                 const int* linked) {
    if (i < 0 || i >= MAX_PANELS || !panels_[i].used) return false;
    Panel& p = panels_[i];
    if (x) p.x = clampTo<float>(*x, -1000.0f, 1000.0f);
    if (y) p.y = clampTo<float>(*y, -1000.0f, 1000.0f);
    if (id && *id >= 1 && *id <= 250 && *id != p.id) {
      // Two panels never show the same number: the other one takes this one's.
      const int other = findPanelById(*id);
      if (other >= 0) { panels_[other].id = p.id; panels_[other].uiDirty = true; }
      p.id = (uint8_t)*id;
    }
    if (name) setTextUtf8(p.name, sizeof(p.name), name);
    if (linked) p.linked = *linked != 0;
    p.uiDirty = true;
    touchPersist();
    return true;
  }

  bool forgetPanel(int i) {
    if (i < 0 || i >= MAX_PANELS || !panels_[i].used) return false;
    logPanel(i, "forgotten");
    panels_[i] = Panel();
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"gone\",\"i\":").i32(i).ch('}');
    flush(o);
    touchPersist();
    return true;
  }

  // One panel, straight from the console: its pattern (-1 = leave) and its
  // knobs (-1 = leave). Not a hand, so nothing travels to the others - this
  // is how each panel is given a pattern of its own.
  bool setPanel(int i, int pattern, const int knob[KNOBS], int glideMs) {
    if (i < 0 || i >= MAX_PANELS || !panels_[i].used) return false;
    Panel& p = panels_[i];
    p.haveTarget = true;
    if (pattern >= 0 && pattern < NO_PATTERN) {
      p.tPattern = (uint8_t)pattern;
      p.applied[CH_PATTERN] = ++eventSeq_;
    }
    for (int k = 0; k < KNOBS; k++) {
      if (knob[k] < 0) continue;
      p.tKnob[k] = (uint16_t)clampTo<int>(knob[k], 0, KNOB_MAX);
      p.applied[k] = ++eventSeq_;
    }
    p.tGlide = (uint16_t)clampTo<int>(glideMs < 0 ? cfg_.glideMs : glideMs, 0, 60000);
    p.uiDirty = true;
    return true;
  }

  // Every panel, from the console: at once, or as a wave from panel `origin`.
  void setAll(int pattern, const int knob[KNOBS], bool ripple, int origin, int glideMs) {
    const uint16_t glide = (uint16_t)clampTo<int>(glideMs < 0 ? cfg_.glideMs : glideMs, 0, 60000);
    const bool wave = ripple && origin >= 0 && origin < MAX_PANELS && panels_[origin].used;
    if (pattern >= 0 && pattern < NO_PATTERN)
      emit(CH_PATTERN, (uint16_t)pattern, wave ? origin : -1, !wave, false, glide, EV_VALUE);
    for (int k = 0; k < KNOBS; k++) {
      if (knob[k] < 0) continue;
      emit(k, (uint16_t)clampTo<int>(knob[k], 0, KNOB_MAX), wave ? origin : -1, !wave, false, glide,
           EV_VALUE);
    }
    runWaves();
  }

  // Scenes: what every panel is showing, kept under a number.
  bool sceneSave(int slot) {
    if (slot < 0 || slot >= SCENES) return false;
    Scene& s = scenes_[slot];
    s.count = 0;
    for (int i = 0; i < MAX_PANELS; i++) {
      const Panel& p = panels_[i];
      if (!p.used || !p.haveTarget) continue;
      SceneEntry& e = s.e[s.count++];
      copyBytes(e.uid, p.uid, 6);
      e.pattern = p.tPattern;
      for (int k = 0; k < KNOBS; k++) e.knob[k] = p.tKnob[k];
    }
    s.used = s.count > 0;
    scenesDirty_ = true;
    touchPersist();
    return s.used;
  }
  bool sceneClear(int slot) {
    if (slot < 0 || slot >= SCENES) return false;
    scenes_[slot].used = false;
    scenes_[slot].count = 0;
    scenesDirty_ = true;
    touchPersist();
    return true;
  }
  bool sceneRecall(int slot, bool ripple, int origin, int glideMs) {
    if (slot < 0 || slot >= SCENES || !scenes_[slot].used) return false;
    const uint16_t glide = (uint16_t)clampTo<int>(glideMs < 0 ? cfg_.glideMs : glideMs, 0, 60000);
    const bool wave = ripple && origin >= 0 && origin < MAX_PANELS && panels_[origin].used;
    Event* e = push(0x1F, (uint16_t)slot, wave ? origin : -1, !wave, false, glide, EV_SCENE);
    (void)e;
    runWaves();
    return true;
  }
  bool sceneUsed(int slot) const { return slot >= 0 && slot < SCENES && scenes_[slot].used; }

  bool identify(int i, bool on) {
    if (i < 0 || i >= MAX_PANELS || !panels_[i].used) return false;
    panels_[i].identify = on;
    panels_[i].identifyAtMs = nowMs_;
    panels_[i].uiDirty = true;
    return true;
  }
  void identifyAll(bool on) {
    for (int i = 0; i < MAX_PANELS; i++) identify(i, on);
  }

  bool rebootPanel(int i) {
    if (i < 0 || i >= MAX_PANELS || !panels_[i].used || !panels_[i].online) return false;
    uint8_t buf[CMD_LEN];
    Cmd c;
    c.cmd = CMD_REBOOT;
    const size_t n = encode(c, buf);
    if (io_.send) io_.send(io_.ctx, panels_[i].ep, buf, n);
    logPanel(i, "asked to reboot");
    return true;
  }

  // Everything the console needs to draw itself from nothing.
  void sendAll() {
    cfgDirty_ = true;
    namesDirty_ = true;
    scenesDirty_ = true;
    for (int i = 0; i < MAX_PANELS; i++) {
      if (!panels_[i].used) continue;
      panels_[i].uiDirty = true;
      panels_[i].uiSentMs = nowMs_ - 100000;
    }
    lastHubLineMs_ = nowMs_ - 100000;
    consoleLines();
  }

  // Every panel the hub knows, as one JSON array: who it is, where it is
  // on the network and whether it is there. For a script that has to reach
  // each panel itself - to put firmware on it, or take it off again.
  void writePanels(Out& o) const {
    o.ch('[');
    bool first = true;
    for (int i = 0; i < MAX_PANELS; i++) {
      const Panel& p = panels_[i];
      if (!p.used) continue;
      if (!first) o.ch(',');
      first = false;
      o.str("{\"id\":").u32(p.id).str(",\"uid\":\"");
      for (int b = 0; b < 6; b++) o.hex8(p.uid[b]);
      o.str("\",\"n\":").json(p.name, NAME_LEN).str(",\"on\":").u32(p.online ? 1 : 0);
      o.str(",\"ip\":\"").u32(p.ep.ip >> 24).ch('.').u32((p.ep.ip >> 16) & 255).ch('.')
          .u32((p.ep.ip >> 8) & 255).ch('.').u32(p.ep.ip & 255).ch('"');
      o.str(",\"fw\":").json(p.fw, 16).str(",\"var\":").json(p.variant, 16).ch('}');
    }
    o.ch(']');
  }

  // How often a busy panel's line may go to the console. The hub lowers it
  // when the only console is on the 115200-baud UART.
  void setConsoleRate(uint32_t minMs) { uiMinMs_ = minMs < 20 ? 20 : minMs; }

  void log(const char* msg) {
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"log\",\"ms\":").u32(since(nowMs_, startMs_)).str(",\"m\":").json(msg, 200).ch('}');
    flush(o);
  }

  // ── Keeping the setup across a power cut ──────────────────────────────
  // Config, the panel table (uid, number, place, name, linked) and the
  // scenes. Live targets are not kept: after a restart the panels say where
  // they are and the hub takes that.
  bool persistDirty() const { return persistDirty_; }
  uint32_t persistChangedMs() const { return persistChangedMs_; }
  void clearPersistDirty() { persistDirty_ = false; }

  size_t save(uint8_t* b, size_t cap) const {
    if (cap < SAVE_MAX) return 0;
    size_t n = 0;
    b[n++] = 'P'; b[n++] = 'F'; b[n++] = 'E'; b[n++] = 'c'; b[n++] = SAVE_VERSION;
    for (int c = 0; c < CHANNELS; c++) b[n++] = cfg_.link[c];
    putU32(b + n, (uint32_t)(int32_t)roundToLong(cfg_.speed * 1000.0f)); n += 4;
    putU16(b + n, cfg_.glideMs); n += 2;
    b[n++] = (uint8_t)((cfg_.k4Pattern ? 1 : 0) | (cfg_.lockMenus ? 2 : 0));
    b[n++] = cfg_.patMin; b[n++] = cfg_.patMax; b[n++] = cfg_.k4Steps;
    b[n++] = (uint8_t)panelCount();
    for (int i = 0; i < MAX_PANELS; i++) {
      const Panel& p = panels_[i];
      if (!p.used) continue;
      copyBytes(b + n, p.uid, 6); n += 6;
      b[n++] = p.id;
      b[n++] = p.linked ? 1 : 0;
      putU32(b + n, (uint32_t)(int32_t)roundToLong(p.x * 1000.0f)); n += 4;
      putU32(b + n, (uint32_t)(int32_t)roundToLong(p.y * 1000.0f)); n += 4;
      copyBytes(b + n, p.name, NAME_LEN); n += NAME_LEN;
    }
    for (int s = 0; s < SCENES; s++) {
      const Scene& sc = scenes_[s];
      b[n++] = sc.used ? sc.count : 0;
      if (!sc.used) continue;
      for (int j = 0; j < sc.count; j++) {
        copyBytes(b + n, sc.e[j].uid, 6); n += 6;
        b[n++] = sc.e[j].pattern;
        for (int k = 0; k < KNOBS; k++) { putU16(b + n, sc.e[j].knob[k]); n += 2; }
      }
    }
    return n;
  }

  // All or nothing: a blob that does not parse to its end changes nothing.
  bool load(const uint8_t* b, size_t len) {
    if (!parse(b, len, false)) return false;
    parse(b, len, true);
    cfgDirty_ = true;
    scenesDirty_ = true;
    persistDirty_ = false;
    return true;
  }

  const Stats& stats() const { return stats_; }
  int inFlight() const { return count_; }
  const char* patternNameAt(int idx) const {
    return idx >= 0 && idx < MAX_NAMES ? names_[idx] : "";
  }

 private:
  static constexpr uint8_t SAVE_VERSION = 1;
  static constexpr uint32_t WAVE_TICK_MS = 5;
  static constexpr uint32_t SEND_MIN_MS = 30;      // a changing target: 33 STATEs a second at most
  static constexpr uint32_t RESEND_MS = 200;       // not acknowledged: again, five times a second
  static constexpr uint32_t REFRESH_MS = 1000;     // nothing changed: once a second all the same

  bool parse(const uint8_t* b, size_t len, bool store) {
    size_t n = 0;
    auto need = [&](size_t k) { return n + k <= len; };
    if (!need(5) || b[0] != 'P' || b[1] != 'F' || b[2] != 'E' || b[3] != 'c' || b[4] != SAVE_VERSION)
      return false;
    n = 5;
    if (!need(CHANNELS + 4 + 2 + 4 + 1)) return false;
    Config cfg;
    for (int c = 0; c < CHANNELS; c++) {
      const uint8_t l = b[n++];
      cfg.link[c] = l > LINK_RIPPLE ? (uint8_t)LINK_SOLO : l;
    }
    cfg.speed = (float)(int32_t)getU32(b + n) / 1000.0f; n += 4;
    cfg.glideMs = getU16(b + n); n += 2;
    cfg.k4Pattern = (b[n] & 1) != 0;
    cfg.lockMenus = (b[n] & 2) != 0;
    n++;
    cfg.patMin = b[n++];
    cfg.patMax = b[n++];
    cfg.k4Steps = b[n] ? b[n] : 1;
    n++;
    const int count = b[n++];
    if (count > MAX_PANELS) return false;
    if (store) {
      cfg_ = cfg;
      for (int i = 0; i < MAX_PANELS; i++) panels_[i] = Panel();
    }
    for (int i = 0; i < count; i++) {
      if (!need(6 + 2 + 8 + NAME_LEN)) return false;
      if (store) {
        Panel& p = panels_[i];
        p.used = true;
        copyBytes(p.uid, b + n, 6);
        p.id = b[n + 6];
        p.linked = b[n + 7] != 0;
        p.x = (float)(int32_t)getU32(b + n + 8) / 1000.0f;
        p.y = (float)(int32_t)getU32(b + n + 12) / 1000.0f;
        copyBytes(p.name, b + n + 16, NAME_LEN);
        p.name[NAME_LEN - 1] = 0;
      }
      n += 6 + 2 + 8 + NAME_LEN;
    }
    for (int s = 0; s < SCENES; s++) {
      if (!need(1)) return false;
      const int c = b[n++];
      if (c > MAX_PANELS || !need((size_t)c * 15)) return false;
      if (store) {
        Scene& sc = scenes_[s];
        sc.used = c > 0;
        sc.count = (uint8_t)c;
        for (int j = 0; j < c; j++) {
          const uint8_t* e = b + n + (size_t)j * 15;
          copyBytes(sc.e[j].uid, e, 6);
          sc.e[j].pattern = e[6];
          for (int k = 0; k < KNOBS; k++) sc.e[j].knob[k] = getU16(e + 7 + 2 * k);
        }
      }
      n += (size_t)c * 15;
    }
    return true;
  }

  // ── Reports ───────────────────────────────────────────────────────────
  void onReport(const Endpoint& from, const Report& r) {
    int i = findPanel(r.uid);
    if (i < 0) {
      i = addPanel(r.uid);
      if (i < 0) { stats_.full++; return; }
    }
    Panel& p = panels_[i];
    const bool known = p.everSeen;
    const bool wasOnline = p.online;
    const uint32_t quiet = known ? since(nowMs_, p.lastSeenMs) : 0;
    // A panel that started over: it says so (BOOT), or its uptime went back.
    const bool restarted = known && ((r.flags & RF_BOOT) || r.uptimeS + 2 < p.rep.uptimeS);
    // A report overtaken on the air by a later one. Believing it would turn
    // the knob back and send that out as a hand's move.
    if (known && !restarted && quiet < STALE_WINDOW_MS && (int16_t)(r.seq - p.rep.seq) <= 0) {
      stats_.stale++;
      return;
    }
    // Whether this report continues the last one, so that a counter that
    // moved means a hand. Not after a restart (the counters began again),
    // and not after a long silence: a panel that was away for a while takes
    // what the ensemble shows now, not what somebody did to it back then.
    const bool continuous = known && !restarted && quiet < REJOIN_HAND_MS;

    if (continuous && wasOnline) {
      const uint16_t gap = (uint16_t)(r.seq - p.rep.seq - 1);
      if (gap && gap < 1000) p.lost += gap;
    }
    if (restarted && wasOnline && !(p.rep.flags & RF_BOOT)) {
      p.restarts++;
      logPanel(i, "restarted");
    }

    const Report prev = p.rep;
    p.rep = r;
    p.ep = from;
    p.lastSeenMs = nowMs_;
    p.online = true;
    p.reports++;
    p.everSeen = true;

    if (!wasOnline) {
      p.uiDirty = true;
      p.infoReq = true;
      logPanel(i, known ? "back" : "joined");
    }

    if (!p.haveTarget) adopt(p);

    if (continuous) {
      // Hands. A knob whose counter moved was turned at the panel, and its
      // reported position is where the hand left it.
      for (int k = 0; k < KNOBS; k++) {
        if (r.touch[k] != prev.touch[k]) hand(i, k, r.knob[k]);
      }
      if (r.patTouch != prev.patTouch && r.pattern != NO_PATTERN) hand(i, CH_PATTERN, r.pattern);
    }

    if (r.pattern != prev.pattern || r.flags != prev.flags || !sameBytes(r.knob, prev.knob, sizeof(r.knob)))
      p.uiDirty = true;
  }

  void onInfo(const Info& m) {
    const int i = findPanel(m.uid);
    if (i < 0) return;
    Panel& p = panels_[i];
    if (!sameBytes(p.fw, m.fw, 16) || !sameBytes(p.variant, m.variant, 16) ||
        !sameBytes(p.patternName, m.patternName, NAME_LEN) || p.patternCount != m.patternCount) {
      p.uiDirty = true;
    }
    copyBytes(p.fw, m.fw, 16);
    copyBytes(p.variant, m.variant, 16);
    copyBytes(p.patternName, m.patternName, NAME_LEN);
    p.patternCount = m.patternCount;
    p.detents = m.detents;
    // The name of a pattern number, learned as panels visit it. One table
    // for the ensemble: the panels of one installation carry one image.
    if (m.pattern < MAX_NAMES && m.patternName[0] &&
        !sameBytes(names_[m.pattern], m.patternName, NAME_LEN)) {
      copyBytes(names_[m.pattern], m.patternName, NAME_LEN);
      namesDirty_ = true;
    }
  }

  int addPanel(const uint8_t uid[6]) {
    for (int i = 0; i < MAX_PANELS; i++) {
      if (panels_[i].used) continue;
      Panel& p = panels_[i];
      p = Panel();
      p.used = true;
      copyBytes(p.uid, uid, 6);
      int id = 1;
      while (findPanelById(id) >= 0) id++;
      p.id = (uint8_t)id;
      p.x = (float)(id - 1);     // in a row, one unit apart, until somebody places it
      p.y = 0;
      touchPersist();
      return i;
    }
    return -1;
  }

  // A panel the hub holds no target for. On a linked channel it joins what
  // the ensemble is showing; otherwise it keeps what it has. The first panel
  // to arrive after the hub starts is the one the others then join.
  void adopt(Panel& p) {
    for (int c = 0; c < CHANNELS; c++) {
      const uint16_t own = c == CH_PATTERN ? p.rep.pattern : p.rep.knob[c];
      const bool follows = p.linked && cfg_.link[c] != LINK_SOLO;
      if (follows && shared_[c].valid) {
        setTarget(p, c, shared_[c].value);
        p.applied[c] = shared_[c].seq;
      } else {
        setTarget(p, c, own);
        if (follows && !(c == CH_PATTERN && own == NO_PATTERN)) {
          shared_[c].valid = true;
          shared_[c].value = own;
          shared_[c].seq = eventSeq_;
        }
      }
    }
    p.tGlide = 0;
    p.haveTarget = true;
    p.uiDirty = true;
  }

  void setTarget(Panel& p, int ch, uint16_t v) {
    if (ch == CH_PATTERN) {
      if (v >= NO_PATTERN) return;
      if (p.tPattern != (uint8_t)v) { p.tPattern = (uint8_t)v; p.uiDirty = true; }
    } else {
      v = v > KNOB_MAX ? KNOB_MAX : v;
      if (p.tKnob[ch] != v) { p.tKnob[ch] = v; p.uiDirty = true; }
    }
  }

  // A hand moved channel `ch` of panel `i` to `v`.
  void hand(int i, int ch, uint16_t v) {
    Panel& p = panels_[i];
    p.hands++;
    const uint8_t link = p.linked ? cfg_.link[ch] : (uint8_t)LINK_SOLO;
    if (link == LINK_SOLO) {
      setTarget(p, ch, v);
      p.applied[ch] = ++eventSeq_;
      return;
    }
    emit(ch, v, i, link == LINK_SYNC, true, cfg_.glideMs, EV_VALUE);
    runWaves();
  }

  // A value starts travelling. From a panel (origin >= 0) it is already
  // there; from the console (origin < 0) it starts nowhere and is instant.
  void emit(int ch, uint16_t v, int origin, bool instant, bool linkedOnly, uint16_t glide,
            uint8_t kind) {
    Event* e = push((uint8_t)(1u << ch), v, origin, instant, linkedOnly, glide, kind);
    if (!e) return;
    shared_[ch].valid = true;
    shared_[ch].value = v;
    shared_[ch].seq = e->seq;
    if (origin >= 0 && linkedOnly) {
      // The hand's own panel is not waiting for its own wave.
      setTarget(panels_[origin], ch, v);
      panels_[origin].applied[ch] = e->seq;
    }
  }

  Event* push(uint8_t mask, uint16_t v, int origin, bool instant, bool linkedOnly, uint16_t glide,
              uint8_t kind) {
    if (count_ == MAX_EVENTS) {
      // Full: the oldest value lands everywhere now rather than be lost.
      applyEverywhere(events_[head_]);
      head_ = (head_ + 1) % MAX_EVENTS;
      count_--;
      stats_.dropped++;
    }
    Event& e = events_[(head_ + count_) % MAX_EVENTS];
    e = Event();
    e.seq = ++eventSeq_;
    e.tMs = nowMs_;
    e.mask = mask;
    e.value = v;
    e.kind = kind;
    e.glideMs = glide;
    e.instant = instant || origin < 0;
    e.linkedOnly = linkedOnly;
    e.origin = (int8_t)origin;
    if (origin >= 0) { e.x = panels_[origin].x; e.y = panels_[origin].y; }
    e.maxDelayMs = 0;
    if (!e.instant) {
      for (int i = 0; i < MAX_PANELS; i++) {
        if (!eligible(panels_[i], e)) continue;
        const uint32_t d = delayMs(panels_[i], e);
        if (d > e.maxDelayMs) e.maxDelayMs = d;
      }
    }
    count_++;
    stats_.events++;
    waveLine(e);
    return &e;
  }

  bool eligible(const Panel& p, const Event& e) const {
    return p.used && (!e.linkedOnly || p.linked);
  }

  uint32_t delayMs(const Panel& p, const Event& e) const {
    if (e.instant || cfg_.speed <= 0.0f) return 0;
    const float dx = p.x - e.x, dy = p.y - e.y;
    const float ms = sqrtF(dx * dx + dy * dy) / cfg_.speed * 1000.0f;
    return ms >= (float)MAX_DELAY_MS ? MAX_DELAY_MS : (uint32_t)ms;
  }

  void applyTo(Panel& p, const Event& e) {
    const Scene* sc = e.kind == EV_SCENE && e.value < SCENES ? &scenes_[e.value] : nullptr;
    const SceneEntry* entry = nullptr;
    if (sc) {
      for (int j = 0; j < sc->count; j++)
        if (sameBytes(sc->e[j].uid, p.uid, 6)) { entry = &sc->e[j]; break; }
    }
    bool changed = false;
    for (int c = 0; c < CHANNELS; c++) {
      if (!(e.mask & (1u << c)) || e.seq <= p.applied[c]) continue;
      p.applied[c] = e.seq;
      if (e.kind == EV_SCENE) {
        if (!entry) continue;         // the scene was saved without this panel
        setTarget(p, c, c == CH_PATTERN ? entry->pattern : entry->knob[c]);
      } else {
        setTarget(p, c, e.value);
      }
      changed = true;
    }
    if (changed) {
      p.haveTarget = true;
      p.tGlide = e.glideMs;
    }
  }

  void applyEverywhere(const Event& e) {
    for (int i = 0; i < MAX_PANELS; i++)
      if (eligible(panels_[i], e)) applyTo(panels_[i], e);
  }

  // Each panel takes, per channel, the newest emitted value that has arrived.
  void runWaves() {
    for (int i = 0; i < MAX_PANELS; i++) {
      Panel& p = panels_[i];
      if (!p.used) continue;
      uint8_t found = 0;
      for (int j = count_ - 1; j >= 0 && found != 0x1F; j--) {
        const Event& e = events_[(head_ + j) % MAX_EVENTS];
        if (!(e.mask & ~found) || !eligible(p, e)) continue;
        if (since(nowMs_, e.tMs) < delayMs(p, e)) continue;
        applyTo(p, e);
        found |= e.mask;
      }
    }
    // Values that have landed everywhere leave the list.
    while (count_) {
      const Event& e = events_[head_];
      if (since(nowMs_, e.tMs) < e.maxDelayMs) break;
      applyEverywhere(e);
      head_ = (head_ + 1) % MAX_EVENTS;
      count_--;
    }
  }

  // ── STATE out ─────────────────────────────────────────────────────────
  void buildState(const Panel& p, State* s) const {
    s->flags = (uint8_t)((cfg_.k4Pattern ? SF_K4_PATTERN : 0) | (cfg_.lockMenus ? SF_LOCK_MENUS : 0) |
                         (p.identify ? SF_IDENTIFY : 0) | (p.infoReq ? SF_INFO_REQ : 0));
    s->seq = 0;
    s->panelId = p.id;
    s->pattern = p.tPattern;
    s->patAck = p.rep.patTouch;
    s->patMin = cfg_.patMin;
    s->patMax = cfg_.patMax;
    s->k4Steps = cfg_.k4Steps;
    for (int k = 0; k < KNOBS; k++) {
      s->knob[k] = p.tKnob[k];
      s->ack[k] = p.rep.touch[k];
    }
    s->glideMs = p.tGlide;
    s->hubMs = 0;
  }

  void sendStates() {
    for (int i = 0; i < MAX_PANELS; i++) {
      Panel& p = panels_[i];
      if (!p.used || !p.online || !p.haveTarget) continue;
      State s;
      buildState(p, &s);
      uint8_t body[STATE_LEN];
      encode(s, body);                      // seq and hubMs still zero: the comparable part
      const uint32_t age = since(nowMs_, p.lastSendMs);
      const bool changed = !p.sentAny || !sameBytes(body, p.sent, STATE_LEN);
      const bool unacked = p.rep.ack != p.stateSeq;
      if (!((changed && age >= SEND_MIN_MS) || (unacked && age >= RESEND_MS) || age >= REFRESH_MS))
        continue;
      copyBytes(p.sent, body, STATE_LEN);
      p.sentAny = true;
      p.stateSeq++;
      if (p.stateSeq == 0) p.stateSeq = 1;  // 0 means "none" in a report's ack
      s.seq = p.stateSeq;
      s.hubMs = nowMs_;
      uint8_t out[STATE_LEN];
      const size_t n = encode(s, out);
      if (io_.send) io_.send(io_.ctx, p.ep, out, n);
      p.lastSendMs = nowMs_;
      p.infoReq = false;
      stats_.tx++;
    }
  }

  // ── Lines for the console ─────────────────────────────────────────────
  void flush(Out& o) {
    if (o.overflow || !io_.line) return;
    io_.line(io_.ctx, o.buf, o.len);
  }

  void touchPersist() {
    persistDirty_ = true;
    persistChangedMs_ = nowMs_;
  }

  void logPanel(int i, const char* what) {
    char msg[96];
    Out m(msg, sizeof(msg));
    const Panel& p = panels_[i];
    m.str("panel ").u32(p.id).str(" (");
    for (int b = 0; b < 6; b++) m.hex8(p.uid[b]);
    m.str(") ").str(what);
    log(msg);
  }

  void waveLine(const Event& e) {
    int ch = 0;
    while (ch < CHANNELS && !(e.mask & (1u << ch))) ch++;
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"w\",\"q\":").u32(e.seq).str(",\"c\":").i32(e.kind == EV_SCENE ? 5 : ch);
    o.str(",\"o\":").i32(e.origin).str(",\"x\":").num(e.x).str(",\"y\":").num(e.y);
    o.str(",\"v\":").u32(e.value).str(",\"now\":").u32(e.instant ? 1 : 0);
    o.str(",\"d\":").u32(e.maxDelayMs).ch('}');
    flush(o);
  }

  bool settled(const Panel& p) const {
    if (!p.haveTarget || !p.online) return false;
    if (p.tPattern != NO_PATTERN && p.rep.pattern != p.tPattern) return false;
    const int knobs = cfg_.k4Pattern ? KNOBS - 1 : KNOBS;
    for (int k = 0; k < knobs; k++)
      if (p.rep.knob[k] != p.tKnob[k]) return false;
    return true;
  }

  void panelLine(int i) {
    const Panel& p = panels_[i];
    Out o(line_, sizeof(line_));
    o.str("{\"t\":\"p\",\"i\":").i32(i).str(",\"uid\":\"");
    for (int b = 0; b < 6; b++) o.hex8(p.uid[b]);
    o.str("\",\"id\":").u32(p.id).str(",\"n\":").json(p.name, NAME_LEN);
    o.str(",\"x\":").num(p.x).str(",\"y\":").num(p.y).str(",\"l\":").u32(p.linked ? 1 : 0);
    o.str(",\"on\":").u32(p.online ? 1 : 0);
    o.str(",\"age\":").u32(p.everSeen ? since(nowMs_, p.lastSeenMs) : 0);
    o.str(",\"ip\":\"").u32(p.ep.ip >> 24).ch('.').u32((p.ep.ip >> 16) & 255).ch('.')
        .u32((p.ep.ip >> 8) & 255).ch('.').u32(p.ep.ip & 255).ch('"');
    o.str(",\"rssi\":").i32(p.rep.rssi).str(",\"fps\":").u32(p.rep.fps).str(",\"up\":").u32(p.rep.uptimeS);
    o.str(",\"fl\":").u32(p.rep.flags).str(",\"pat\":").i32(p.rep.pattern == NO_PATTERN ? -1 : p.rep.pattern);
    o.str(",\"pn\":").json(p.patternName, NAME_LEN).str(",\"pc\":").u32(p.patternCount);
    o.str(",\"k\":[");
    for (int k = 0; k < KNOBS; k++) { if (k) o.ch(','); o.u32(p.rep.knob[k]); }
    o.str("],\"tp\":").i32(p.haveTarget && p.tPattern != NO_PATTERN ? p.tPattern : -1).str(",\"tk\":[");
    for (int k = 0; k < KNOBS; k++) { if (k) o.ch(','); o.u32(p.tKnob[k]); }
    o.str("],\"ok\":").u32(settled(p) ? 1 : 0).str(",\"idf\":").u32(p.identify ? 1 : 0);
    o.str(",\"fw\":").json(p.fw, 16).str(",\"var\":").json(p.variant, 16);
    o.str(",\"rst\":").u32(p.restarts).str(",\"lost\":").u32(p.lost).str(",\"hands\":").u32(p.hands);
    o.str(",\"rep\":").u32(p.reports).ch('}');
    flush(o);
  }

  void consoleLines() {
    if (!io_.line) return;
    if (cfgDirty_) {
      cfgDirty_ = false;
      Out o(line_, sizeof(line_));
      o.str("{\"t\":\"cfg\",\"link\":[");
      for (int c = 0; c < KNOBS; c++) { if (c) o.ch(','); o.json(linkName(cfg_.link[c])); }
      o.str("],\"plink\":").json(linkName(cfg_.link[CH_PATTERN]));
      o.str(",\"speed\":").num(cfg_.speed).str(",\"glide\":").u32(cfg_.glideMs);
      o.str(",\"k4\":").boolean(cfg_.k4Pattern).str(",\"lock\":").boolean(cfg_.lockMenus);
      o.str(",\"pmin\":").u32(cfg_.patMin).str(",\"pmax\":").u32(cfg_.patMax);
      o.str(",\"k4s\":").u32(cfg_.k4Steps).str(",\"maxPanels\":").u32(MAX_PANELS);
      o.str(",\"scenes\":").u32(SCENES).ch('}');
      flush(o);
    }
    if (namesDirty_) {
      namesDirty_ = false;
      int last = -1;
      for (int n = 0; n < MAX_NAMES; n++) if (names_[n][0]) last = n;
      Out o(line_, sizeof(line_));
      o.str("{\"t\":\"names\",\"n\":[");
      for (int n = 0; n <= last; n++) { if (n) o.ch(','); o.json(names_[n], NAME_LEN); }
      o.str("]}");
      flush(o);
    }
    if (scenesDirty_) {
      scenesDirty_ = false;
      Out o(line_, sizeof(line_));
      o.str("{\"t\":\"scenes\",\"u\":[");
      for (int s = 0; s < SCENES; s++) { if (s) o.ch(','); o.u32(scenes_[s].used ? scenes_[s].count : 0); }
      o.str("]}");
      flush(o);
    }
    for (int i = 0; i < MAX_PANELS; i++) {
      Panel& p = panels_[i];
      if (!p.used) continue;
      const uint32_t age = since(nowMs_, p.uiSentMs);
      if ((p.uiDirty && age >= uiMinMs_) || age >= 2000) {
        p.uiDirty = false;
        p.uiSentMs = nowMs_;
        panelLine(i);
      }
    }
    if (since(nowMs_, lastHubLineMs_) >= 1000) {
      lastHubLineMs_ = nowMs_;
      Out o(line_, sizeof(line_));
      o.str("{\"t\":\"hub\",\"ms\":").u32(since(nowMs_, startMs_)).str(",\"proto\":").u32(PROTO_VERSION);
      o.str(",\"panels\":").i32(panelCount()).str(",\"online\":").i32(onlineCount());
      o.str(",\"rx\":").u32(stats_.rx).str(",\"tx\":").u32(stats_.tx).str(",\"bad\":").u32(stats_.bad);
      o.str(",\"ev\":").u32(stats_.events).str(",\"fly\":").i32(count_).str(",\"drop\":").u32(stats_.dropped);
      o.str(",\"full\":").u32(stats_.full).str(",\"stale\":").u32(stats_.stale).ch('}');
      flush(o);
    }
  }

  ConductorIo io_;
  Config cfg_;
  Panel panels_[MAX_PANELS];
  Scene scenes_[SCENES];
  Event events_[MAX_EVENTS];
  int head_ = 0;
  int count_ = 0;
  uint32_t eventSeq_ = 0;
  Shared shared_[CHANNELS];
  char names_[MAX_NAMES][NAME_LEN] = {{0}};
  Stats stats_;

  uint32_t nowMs_ = 0;
  uint32_t startMs_ = 0;
  uint32_t lastWaveMs_ = 0;
  uint32_t lastHubLineMs_ = 0;
  uint32_t uiMinMs_ = 100;
  bool cfgDirty_ = false;
  bool namesDirty_ = false;
  bool scenesDirty_ = false;
  bool persistDirty_ = false;
  uint32_t persistChangedMs_ = 0;
  char line_[1024];
};

}  // namespace pfe
