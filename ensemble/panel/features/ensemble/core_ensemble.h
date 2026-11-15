// ═══════════════════════════════════════════════════════════
// PatternFlow - the ensemble: one panel among several
//
// A room of panels and a hub. Turn a knob on one and the others follow, at
// once or as a wave across the room - which, and how fast, is the hub's to
// say (ensemble/README.md in patternflow-exhibition-2026). This feature is
// the panel's half:
//
//   K1-K4     one position per knob, 0..1000, kept here and handed to the
//             pattern as a lane, the way the 2026 exhibition's panel did
//             (features/exhibit): a pattern's parameter is that position in
//             the parameter's own range. A position is a thing two panels
//             can agree on; a count of detents is not.
//   K4        steps through the patterns instead, when the hub says so.
//   the hub   is told where this panel is twice a second and whenever a
//             hand moves, and says where it should be. A hand always wins:
//             a target the hub sent before it heard of the last turn is
//             ignored (pfe_follower.h says how).
//
// Everything that decides anything is pfe_follower.h, which has no Arduino
// in it and is tested on a PC against the hub's own code. This file is the
// device around it: the Wi-Fi socket, the frame hooks, the menu lock and
// the number drawn on the LEDs when the console asks which panel this is.
//
// No core file knows this feature exists. Threading: every hook except
// appendStatus runs on the frame task, so the state is plain; what
// appendStatus reports is copied into atomics once a frame.
//
// With no hub in reach the panel still works, and feels the same: the knobs
// keep their positions and K4 keeps its role.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <atomic>

#include "ensemble_config.h"
#include "../pf_feature.h"
#include "../../net_config.h"          // PF_IMPROV_FW_VERSION, PF_VARIANT
#include "../../src/core_display.h"    // dma_display
#include "../../src/core_encoders.h"   // InputFrame, btn1..btn4
#include "../../src/core_sleep.h"      // PatternflowSleep::isSleeping()
#include "../../src/core_ui_text.h"    // PatternflowUiText
#include "../../src/core_wifi.h"       // PatternflowWifi: the saved networks, and a join

#include "pfe_follower.h"

// The length of the pattern list (pattern_registry.h, which the sketch
// includes after the features). K4 needs it to wrap.
extern int NUM_PATTERNS;

// The panel's brightness, as the sketch keeps it (patternflow.ino): the value
// K1's long press sets, and the two words that have loop() store it in NVS
// once it has been still. Set here the way GET /api/display?brightness= does
// (core_display_http.h); the frame applies it through the power clamp.
extern uint8_t currentBrightness;
extern bool brightnessDirty;
extern uint32_t brightnessIdleAtMs;
// The sketch shows a pattern's name for a second whenever the pattern
// changes, from the countdown below (patternflow.ino). On a wall every
// change would flash a name across all the panels: drawOverlay(), which the
// sketch calls just before it draws the name, keeps the countdown at zero
// unless PF_ENSEMBLE_PATTERN_NAME asks for the name.
extern float contentNoticeTimer;

#if PF_ENSEMBLE_KNOB_DETENTS < 1 || PF_ENSEMBLE_KNOB_DETENTS > 250
#error "PF_ENSEMBLE_KNOB_DETENTS must be 1..250"
#endif
#if PF_ENSEMBLE_K4_STEPS < 1 || PF_ENSEMBLE_K4_STEPS > 24
#error "PF_ENSEMBLE_K4_STEPS must be 1..24"
#endif

namespace PatternflowEnsemble {

// ── Frame task ──────────────────────────────────────────────────────────
inline pfe::Follower engine;
inline pfe::FrameIn frameIn;
inline pfe::FrameOut frameOut;
inline WiFiUDP udp;
inline bool socketOpen = false;
inline IPAddress hubIp;
inline bool hubKnown = false;
inline bool hubFixed = false;        // PF_ENSEMBLE_HOST names it
inline uint32_t hubHeardMs = 0;
inline bool claimNow = false;        // the pattern handed over this frame was the hub's
// Whether another feature has the panel to itself right now - a show is
// playing. While it does, this panel is not one of the ensemble: its hands
// and the hub's values wait (pfe_follower.h, "solo"). The bundle that
// carries such a feature says how to ask (features_local.h); with none,
// nobody ever does.
inline bool (*soloNow)() = nullptr;
inline bool rebootArmed = false;
inline uint32_t rebootAtMs = 0;
inline int8_t rssi = 0;
inline uint32_t rssiAtMs = 0;
inline uint8_t fps = 0;
inline uint32_t fpsFrames = 0;
inline uint32_t fpsAtMs = 0;

// ── Read by /api/status on the network core ─────────────────────────────
inline std::atomic<bool> statLinked{false};
inline std::atomic<uint8_t> statId{0};
inline std::atomic<uint32_t> statStates{0};
inline std::atomic<uint32_t> statSent{0};
inline std::atomic<uint32_t> statHub{0};
inline std::atomic<uint16_t> statKnob[pfe::KNOBS] = {};
inline char statUid[13] = "";        // written once in setup(), before any request can come

inline void setup() {
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  WiFi.macAddress(mac);              // the station's address: this panel's name on the air
  pfe::FollowerConfig cfg;
  cfg.detents = (uint8_t)PF_ENSEMBLE_KNOB_DETENTS;
  cfg.k4Steps = (uint8_t)PF_ENSEMBLE_K4_STEPS;
  cfg.k4Pattern = PF_ENSEMBLE_K4_PATTERN != 0;
  cfg.fw = PF_IMPROV_FW_VERSION;
  cfg.variant = PF_VARIANT " " PF_VARIANT_VERSION;
  engine.begin(mac, cfg, millis());
  snprintf(statUid, sizeof(statUid), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  hubFixed = hubIp.fromString(PF_ENSEMBLE_HOST);
  hubKnown = hubFixed;
  Serial.printf("[ENSEMBLE] panel %02x%02x%02x%02x%02x%02x, hub %s, %d detents a knob, K4 %s\n", mac[0],
                mac[1], mac[2], mac[3], mac[4], mac[5], hubFixed ? PF_ENSEMBLE_HOST : "found by broadcast",
                (int)PF_ENSEMBLE_KNOB_DETENTS, PF_ENSEMBLE_K4_PATTERN ? "steps the pattern" : "is a knob");
}

// The Wi-Fi connect edge, and every reconnect.
inline void onNetwork() {
  if (WiFi.status() != WL_CONNECTED) return;
  udp.stop();
  socketOpen = udp.begin(PF_ENSEMBLE_LOCAL_PORT) == 1;
  if (!hubFixed) hubKnown = false;   // a new network: look for its hub
  Serial.printf("[ENSEMBLE] %s on :%d, hub port %d\n", socketOpen ? "listening" : "could NOT open a socket",
                (int)PF_ENSEMBLE_LOCAL_PORT, (int)PF_ENSEMBLE_PORT);
}

inline bool online() { return socketOpen && WiFi.status() == WL_CONNECTED; }

// ── Back to the hub's network ─────────────────────────────────────────
// A panel that has been with a hub belongs on the hub's network. When the
// hub goes away (a restart, a firmware update) the core moves on to the
// next saved network within seconds and stays wherever it lands, so a panel
// could sit on the studio Wi-Fi for good - seen 2026-11-14, both panels,
// after the hub was flashed. While no hub is heard and the panel is on a
// network other than the one it boots into, it asks the core to join that
// one again: PF_ENSEMBLE_HOME_FIRST_MS after it got there, then every
// PF_ENSEMBLE_HOME_EVERY_MS. Each try costs that network for as long as
// the join takes, so a panel that has not met a hub since it started is
// left alone - one being used on its own - except in its first
// PF_ENSEMBLE_HOME_AFTER_BOOT_MS: everything switched on at once, the
// panels are up before the hub's hotspot and land on the studio Wi-Fi
// (seen 2026-11-14, a panel that restarted while the hub was flashed).
inline bool away = false;       // on another network with no hub to hear
inline uint32_t awayAtMs = 0;    // since when (the flag says whether: a `now | 1` start can lie a millisecond ahead)
inline uint32_t homeAtMs = 0;    // the last try
inline uint32_t homeTries = 0;

inline void comeHome(uint32_t now) {
  if (PF_ENSEMBLE_HOME_FIRST_MS == 0) return;
  const bool justStarted = now < (uint32_t)PF_ENSEMBLE_HOME_AFTER_BOOT_MS;   // millis() since boot
  if (engine.linked(now) || (!engine.everLinked() && !justStarted)) {   // with a hub, or on its own
    away = false;
    homeTries = 0;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;          // the core is joining something: let it finish
  const int boot = PatternflowWifi::getBootIndex();
  if (PatternflowWifi::savedCount() < 2 || boot < 0 || boot >= PatternflowWifi::savedCount()) return;
  if (PatternflowWifi::currentSsid() == PatternflowWifi::savedSsid(boot)) {   // home; the hub is just quiet
    away = false;
    return;
  }
  if (!away) {
    away = true;
    awayAtMs = now;
    return;
  }
  const uint32_t waited = now - (homeTries ? homeAtMs : awayAtMs);
  if (waited < (homeTries ? (uint32_t)PF_ENSEMBLE_HOME_EVERY_MS : (uint32_t)PF_ENSEMBLE_HOME_FIRST_MS)) return;
  // Copies: the core reorders its list while it opens the join (addNetwork).
  const String ssid = PatternflowWifi::savedSsid(boot);
  const String pass = PatternflowWifi::savedPasses[boot];
  Serial.printf("[ENSEMBLE] no hub on \"%s\" - trying \"%s\" again\n", PatternflowWifi::currentSsid().c_str(),
                ssid.c_str());
  homeAtMs = now;
  homeTries++;
  PatternflowWifi::applyCredentials(ssid, pass);
}

// Whatever the engine has to say, to the hub - or, while no hub is known,
// to everybody on the network.
inline void flush(uint32_t now) {
  if (!online()) return;
  uint8_t buf[pfe::MAX_PACKET];
  for (int guard = 0; guard < 3; guard++) {
    const size_t n = engine.poll(buf, sizeof(buf), now);
    if (!n) return;
    const IPAddress to = hubKnown ? hubIp : IPAddress(255, 255, 255, 255);
    if (!udp.beginPacket(to, PF_ENSEMBLE_PORT)) continue;
    udp.write(buf, n);
    if (udp.endPacket()) statSent.fetch_add(1, std::memory_order_relaxed);
  }
}

// Top of the frame: what is running, what the hub said, and the things
// that have to happen whether or not a frame is drawn.
inline void loop(const PFFeatureFrame& frame) {
  const uint32_t now = millis();
  frameIn.pattern = frame.patternIndex;
  frameIn.patternName = frame.patternName;
  frameIn.patternCount = NUM_PATTERNS;
  frameIn.running = frame.running;
  frameIn.chrome = frame.chromeVisible;
  claimNow = false;

  fpsFrames++;
  if (now - fpsAtMs >= 1000) {
    fps = (uint8_t)(fpsFrames > 255 ? 255 : fpsFrames);
    fpsFrames = 0;
    fpsAtMs = now;
  }

  if (online()) {
    uint8_t buf[pfe::MAX_PACKET];
    for (int i = 0; i < PF_ENSEMBLE_RX_BUDGET; i++) {
      const int size = udp.parsePacket();
      if (size <= 0) break;
      const int n = udp.read(buf, sizeof(buf));
      if (n <= 0) continue;
      if (!engine.onPacket(buf, (size_t)n, now)) continue;
      // Whoever sends this panel a STATE is its hub.
      if (!hubFixed) {
        const IPAddress from = udp.remoteIP();
        if (!hubKnown || from != hubIp) Serial.printf("[ENSEMBLE] hub at %s\n", from.toString().c_str());
        hubIp = from;
        hubKnown = true;
      }
      hubHeardMs = now;
    }
    if (!hubFixed && hubKnown && now - hubHeardMs > (uint32_t)PF_ENSEMBLE_HUB_LOST_MS) {
      hubKnown = false;
      Serial.println("[ENSEMBLE] hub silent - calling out for one");
    }
    if (now - rssiAtMs >= 1000) {
      rssiAtMs = now;
      const int r = WiFi.RSSI();
      rssi = (int8_t)(r < -127 ? -127 : (r > 0 ? 0 : r));
    }
  }
  flush(now);
  comeHome(now);

  if (engine.rebootRequested() && !rebootArmed) {
    rebootArmed = true;
    rebootAtMs = now + 300;
    Serial.println("[ENSEMBLE] reboot asked by the hub");
  }
  if (rebootArmed && (int32_t)(now - rebootAtMs) >= 0) ESP.restart();

  statLinked.store(engine.linked(now), std::memory_order_relaxed);
  statId.store(engine.panelId(), std::memory_order_relaxed);
  statStates.store(engine.statesHeard(), std::memory_order_relaxed);
  statHub.store(hubKnown ? (uint32_t)hubIp : 0, std::memory_order_relaxed);
  for (int k = 0; k < pfe::KNOBS; k++) statKnob[k].store(engine.value(k), std::memory_order_relaxed);
}

// The menu lock, as the exhibition's panel did it (features/exhibit): the
// core opens its long-press screens with Button::longPressed(), which fires
// only while `longPressFired` is false. Set here on every frame - after the
// press edge clears it in readInputFrame, before the sketch reads it - no
// hold ever crosses the threshold. THIS DEPENDS ON src/core_encoders.h
// INTERNALS (the field's name, and where the sketch clears and reads it).
inline void lockMenus() {
  btn1.longPressFired = true;
  btn2.longPressFired = true;
  btn3.longPressFired = true;
  btn4.longPressFired = true;
}

// The frame holds the physical encoders and nothing else when it gets here
// (this feature goes first in PF_FEATURE_LIST for that reason).
inline int8_t sleepOrder = -1;   // 1: go to sleep, 0: wake, -1: nothing asked

// "Put the device to sleep / wake it, please" - once for each order.
inline bool requestSleep(bool* sleeping) {
  if (sleepOrder < 0) return false;
  *sleeping = sleepOrder == 1;
  sleepOrder = -1;
  return true;
}

inline void fillInput(InputFrame& input) {
  frameIn.nowMs = input.now;
  for (int k = 0; k < pfe::KNOBS; k++) {
    frameIn.delta[k] = input.knobDeltas[k];
    frameIn.pressed[k] = input.btnPressed[k];
  }
  frameIn.sleeping = PatternflowSleep::isSleeping();
  frameIn.rssi = rssi;
  frameIn.fps = fps;
  frameIn.brightness = currentBrightness;
  frameIn.solo = soloNow && soloNow();
  engine.frame(frameIn, frameOut);

  // The console's order to sleep or to wake, for the sketch to carry out
  // (requestSleep below): stopping the display is its job, not a feature's.
  if (frameOut.sleep) sleepOrder = frameOut.sleep == pfe::SLEEP_SLEEP ? 1 : 0;

  if (frameOut.brightness) {   // the console asked for a new brightness
    currentBrightness = frameOut.brightness;
    brightnessIdleAtMs = input.now;
    brightnessDirty = true;
  }

  for (int k = 0; k < pfe::KNOBS; k++) {
    // The hand's detents went into the position; taken out of the frame so
    // that neither the pattern nor the core's lane logic counts them again
    // (a delta in the frame is the core's "hand on the knob" signal, and
    // drops that knob's lane for five seconds).
    if (frameOut.ownKnob[k]) input.knobDeltas[k] = 0;
    if (frameOut.laneActive[k]) {
      input.knobAudioActive[k] = true;
      input.knobAudioValue[k] = frameOut.lane[k];
    }
  }
  if (frameOut.lockMenus) lockMenus();
  flush(input.now);   // a hand's move leaves in the frame it was made
}

// Asked by the sketch at the end of the frame. A pattern the hub chose is
// handed over as a claim, for this one call: the sketch then performs the
// switch without writing "the pattern a person last chose" to flash, which a
// wave of pattern changes would otherwise do on every panel every time.
inline bool takePattern(int* idx) {
  bool fromHub = false;
  if (!engine.takePattern(idx, &fromHub)) return false;
  claimNow = fromHub;
  return true;
}

inline bool claimsPattern() { return claimNow; }

// "Which panel is this?" - its number, large, over the pattern.
// "Which panel is this?" - a big shape in a colour on black (pfe_proto.h
// identifyMark, the same mark as its tile on the console's map). No number:
// a dark number on a lit shape read as a black blob from across a room
// (2026-11-14), and the shape and colour already say which panel it is.
inline void drawOverlay(const PFFeatureFrame& frame) {
  if (!PF_ENSEMBLE_PATTERN_NAME) contentNoticeTimer = 0.0f;
  if (!frameOut.identify || frame.chromeVisible || !frame.running) return;
  const int w = dma_display->width(), h = dma_display->height();
  const int cx = w / 2, cy = h / 2, r = h / 2 - 3;   // 128 x 64: a shape 58 LEDs tall
  const pfe::IdentifyMark m = pfe::identifyMark(frameOut.panelId);
  const uint16_t c = dma_display->color565(m.colour.r, m.colour.g, m.colour.b);
  dma_display->fillRect(0, 0, w, h, 0);
  if (m.shape == pfe::SHAPE_CIRCLE) dma_display->fillCircle(cx, cy, r, c);
  else if (m.shape == pfe::SHAPE_SQUARE) dma_display->fillRect(cx - r, cy - r, 2 * r, 2 * r, c);
  else dma_display->fillTriangle(cx, cy - r, cx - r - 6, cy + r, cx + r + 6, cy + r, c);
}

inline void appendStatus(String& json) {
  const uint32_t hub = statHub.load(std::memory_order_relaxed);
  // `uid` is the name the hub knows this panel by. Nothing else on this
  // API tells one panel from another, so a script matching the hub's list
  // to the panels it can reach matches on this.
  json += ",\"ensemble\":{\"uid\":\"";
  json += statUid;
  json += "\",\"linked\":";
  json += statLinked.load(std::memory_order_relaxed) ? "true" : "false";
  json += ",\"id\":";
  json += (int)statId.load(std::memory_order_relaxed);
  json += ",\"hub\":\"";
  if (hub) json += IPAddress(hub).toString();
  json += "\",\"states\":";
  json += statStates.load(std::memory_order_relaxed);
  json += ",\"sent\":";
  json += statSent.load(std::memory_order_relaxed);
  json += ",\"detents\":";
  json += (int)PF_ENSEMBLE_KNOB_DETENTS;
  json += ",\"knob\":[";
  for (int k = 0; k < pfe::KNOBS; k++) {
    if (k) json += ',';
    json += (int)statKnob[k].load(std::memory_order_relaxed);
  }
  json += "]}";
}

}  // namespace PatternflowEnsemble
