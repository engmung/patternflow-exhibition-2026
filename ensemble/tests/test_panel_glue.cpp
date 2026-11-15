// Patternflow Ensemble - host test of the panel feature's device side.
//
// features/ensemble/core_ensemble.h is the part of the panel that touches
// the device: the socket, the frame hooks, the menu lock, the number on the
// LEDs. This compiles it as it is installed, against stand-ins for the
// device's headers (tests/panel_stub/), and plays frames through the
// feature's descriptor with a real hub (pfe_conductor.h) on the other end
// of the stand-in socket.
//
// It proves the glue is whole and does what it says. It does not prove it
// compiles in the real Patternflow tree: the stand-ins mirror that tree's
// headers, they are not them. tests/run_tests.py --patternflow <checkout>
// compares the one thing whose ORDER matters (the PFFeature descriptor).
//
// License: MIT
#include "features/ensemble/feature_ensemble.h"
#include "features/ensemble/pfe_conductor.h"

#include <cmath>
#include <cstdio>
#include <string>

int NUM_PATTERNS = 8;   // the sketch's pattern list length (pattern_registry.h)
// The sketch's brightness (patternflow.ino).
uint8_t currentBrightness = 128;
bool brightnessDirty = false;
uint32_t brightnessIdleAtMs = 0;
float contentNoticeTimer = 0.0f;        // the sketch's "show the pattern's name" countdown

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      g_failed++;                                                         \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                     \
  } while (0)

static pfe::Conductor hub;
static const IPAddress HUB_IP(192, 168, 66, 1);
static const IPAddress BROADCAST(255, 255, 255, 255);
static bool hubUp = true;
static IPAddress lastTo;
static int toBroadcast = 0, toHub = 0;
static int pattern = 0;          // what the "device" is running
static bool chrome = false;
static InputFrame input;
static const PFFeature& F = PFFeatureEnsemble::descriptor;

static void hubSend(void*, const pfe::Endpoint& to, const uint8_t* data, size_t len) {
  if (to.port != PF_ENSEMBLE_LOCAL_PORT) return;
  stub::Datagram d;
  d.ip = HUB_IP;
  d.port = pfe::UDP_PORT;
  d.data.assign(data, data + len);
  stub::inbox.push_back(d);
}

// One frame of the device, in the sketch's order: loop, the input frame,
// the pattern request. Returns whether the feature asked for a pattern.
static bool frame(int k = -1, int detents = 0, bool press = false) {
  stub::nowMs += 16;
  PFFeatureFrame f{0.016f, "Wave Saw", true, chrome, pattern, 0};
  F.loop(f);
  input = InputFrame{};
  input.now = stub::nowMs;
  if (k >= 0) { input.knobDeltas[k] = detents; input.btnPressed[k] = press; }
  F.fillInput(input);
  int idx = -1;
  const bool asked = F.takePattern(&idx);
  if (asked && idx >= 0 && idx < NUM_PATTERNS) pattern = idx;   // the sketch performs the switch
  F.drawOverlay(f);
  // The air: what the panel sent reaches the hub (if there is one), and
  // the hub's answers wait in the panel's socket for its next frame.
  while (!stub::sent.empty()) {
    const stub::Datagram d = stub::sent.front();
    stub::sent.pop_front();
    lastTo = d.ip;
    if (d.ip == BROADCAST) toBroadcast++; else toHub++;
    if (!hubUp || d.port != pfe::UDP_PORT) continue;
    pfe::Endpoint ep;
    ep.ip = 0xC0A84202u;
    ep.port = PF_ENSEMBLE_LOCAL_PORT;
    hub.onPacket(ep, d.data.data(), d.data.size(), stub::nowMs);
  }
  if (hubUp) hub.tick(stub::nowMs);
  return asked;
}
static void frames(int n) { while (n-- > 0) frame(); }

int main() {
  pfe::ConductorIo io;
  io.send = hubSend;
  hub.begin(io, stub::nowMs);

  // Every hook the descriptor names is there, and nothing claims a slot it
  // does not use.
  CHECK(std::string(F.name) == "ensemble" && std::string(F.cap) == "ensemble");
  CHECK(F.setup && F.onNetwork && F.loop && F.fillInput && F.takePattern && F.claimsPattern);
  CHECK(F.appendStatus && F.drawOverlay);
  CHECK(!F.observeFrame && !F.onUserInput && !F.onSleep && F.requestSleep && !F.shortName);
  CHECK(!F.composeFrame && !F.handleSelectInput && !F.drawSelect && !F.decorateSelect);

  F.setup();
  CHECK(stub::serial.find("[ENSEMBLE] panel 246f28010203") != std::string::npos);
  CHECK(stub::serial.find("found by broadcast") != std::string::npos);

  // No Wi-Fi yet: nothing leaves, and the knobs already hold positions.
  stub::wifiStatus = WL_DISCONNECTED;
  frame();
  CHECK(stub::sent.empty() && toBroadcast == 0);
  CHECK(input.knobAudioActive[0] && std::fabs(input.knobAudioValue[0] - 0.5f) < 1e-6f);
  CHECK(!input.knobAudioActive[3]);            // K4 steps the pattern by default: no lane

  // Wi-Fi up: the panel calls out to everybody until a hub answers.
  stub::wifiStatus = WL_CONNECTED;
  F.onNetwork();
  CHECK(stub::boundPort == PF_ENSEMBLE_LOCAL_PORT);

  // Two saved networks, the hub's first at boot, the panel on the other one,
  // no hub yet (switched on together, the panel was quicker than the hub's
  // hotspot). In its first three minutes it tries the hub's network anyway:
  // 20 s in, then once a minute; after that it is a panel used on its own.
  PatternflowWifi::savedSsids[0] = "PatternflowEnsemble";
  PatternflowWifi::savedPasses[0] = "hub-pass";
  PatternflowWifi::savedSsids[1] = "Studio";
  PatternflowWifi::savedPasses[1] = "studio-pass";
  PatternflowWifi::savedCountValue = 2;
  PatternflowWifi::bootIdx = 0;
  PatternflowWifi::activeSsid = "Studio";
  hubUp = false;
  size_t triedAt20 = 0;
  for (int s = 0; s < 240; s++) {                 // four minutes, a second at a time
    frames(1000 / 16);
    if (PatternflowWifi::activeSsid == PatternflowWifi::savedSsids[0])
      PatternflowWifi::activeSsid = "Studio";    // the hub's network is not there: the core goes back
    if (s == 25) triedAt20 = stub::joins.size();
  }
  CHECK(triedAt20 == 1);                          // the first try, about 20 s in
  CHECK(stub::joins.size() == 3);                 // and at about 80 s and 140 s; none after three minutes
  CHECK(!stub::joins.empty() && stub::joins[0].ssid == "PatternflowEnsemble");
  stub::joins.clear();
  hubUp = true;
  PatternflowWifi::activeSsid = "PatternflowEnsemble";
  frame();
  CHECK(toBroadcast >= 1 && toHub == 0);
  frames(8);
  CHECK(lastTo == HUB_IP);                     // ...and from then on talks to it alone
  CHECK(hub.onlineCount() == 1);
  CHECK(stub::serial.find("[ENSEMBLE] hub at 192.168.66.1") != std::string::npos);
  const int me = 0;
  CHECK(hub.panel(me).id == 1);
  CHECK(std::string(hub.panel(me).fw) == "3.12.0");
  CHECK(std::string(hub.panel(me).variant) == "ensemble v0.1.0");
  frames(40);
  CHECK(std::string(hub.panel(me).patternName) == "Wave Saw");
  CHECK(hub.panel(me).patternCount == 8);

  // A hand: the detents leave the frame, the lane carries the position, and
  // the hub hears where the knob is.
  frame(0, 3);
  CHECK(input.knobDeltas[0] == 0);
  CHECK(input.knobAudioActive[0] && std::fabs(input.knobAudioValue[0] - 0.594f) < 1e-3f);
  frames(6);
  CHECK(hub.panel(me).tKnob[0] == 594);
  CHECK(hub.panel(me).hands == 1);

  // A press: that knob's lane is off for the frame (patterns act on a press
  // only while the knob is not remote-driven).
  frame(1, 0, true);
  CHECK(!input.knobAudioActive[1] && input.knobAudioActive[0]);
  CHECK(input.btnPressed[1]);                  // the press itself is the pattern's

  // The console moves this panel: the lane glides there.
  const int knobs[4] = {-1, 800, -1, -1};
  hub.setPanel(me, -1, knobs, 0);
  frames(8);
  CHECK(std::fabs(input.knobAudioValue[1] - 0.8f) < 1e-6f);

  // K4, three detents: the next pattern, a person's choice (not claimed).
  CHECK(!frame(3, 2));
  CHECK(input.knobDeltas[3] == 0);
  CHECK(frame(3, 1));
  CHECK(pattern == 1);
  CHECK(!F.claimsPattern());
  frames(6);
  CHECK(hub.panel(me).tPattern == 1);

  // The hub's pattern: handed over as a claim, for that one frame.
  const int none[4] = {-1, -1, -1, -1};
  hub.setPanel(me, 5, none, 0);
  bool asked = false, claimed = false;
  for (int i = 0; i < 10 && !asked; i++) {
    asked = frame();
    claimed = F.claimsPattern();
  }
  CHECK(asked && pattern == 5 && claimed);
  frame();
  CHECK(!F.claimsPattern());

  // The device's own screen up: its knobs are its own again.
  chrome = true;
  frame(0, 4);
  CHECK(input.knobDeltas[0] == 4);
  chrome = false;
  frames(4);
  CHECK(hub.panel(me).tKnob[0] == 594);

  // The menu lock, from the hub's setting.
  CHECK(!btn1.longPressFired);
  hub.config().lockMenus = true;
  hub.configChanged();
  frames(6);
  CHECK(btn1.longPressFired && btn2.longPressFired && btn3.longPressFired && btn4.longPressFired);

  // Brightness from the console: set the way /api/display sets it, so the
  // sketch applies it through the power clamp and stores it once still.
  CHECK(hub.panel(me).rep.brightness == 128);   // the hub hears the panel's own
  CHECK(!brightnessDirty);
  hub.setAllBrightness(60);
  frames(6);
  CHECK(currentBrightness == 60);
  CHECK(brightnessDirty && brightnessIdleAtMs > 0 && brightnessIdleAtMs <= stub::nowMs);
  frames(40);
  CHECK(hub.panel(me).rep.brightness == 60);
  // Changed at the panel (K1's long press) afterwards: it stays.
  brightnessDirty = false;
  currentBrightness = 150;
  frames(80);
  CHECK(currentBrightness == 150 && !brightnessDirty);
  CHECK(hub.panel(me).rep.brightness == 150);

  // Back to the hub's network. The hub goes (a firmware update), the core
  // lands on the studio Wi-Fi and would stay: the panel tries the hub's
  // network again 20 s after it got there, then once a minute.
  CHECK(stub::joins.empty());
  hubUp = false;
  frames(4000 / 16);                              // the hub is missed, the panel is still home
  CHECK(stub::joins.empty());
  PatternflowWifi::activeSsid = "Studio";         // the core moved on
  frames(18000 / 16);
  CHECK(stub::joins.empty());
  frames(4000 / 16);
  CHECK(stub::joins.size() == 1);
  CHECK(!stub::joins.empty() && stub::joins[0].ssid == "PatternflowEnsemble" && stub::joins[0].pass == "hub-pass");
  CHECK(stub::serial.find("no hub on \"Studio\" - trying \"PatternflowEnsemble\" again") != std::string::npos);
  // The hub's network is not there yet; the core goes back to the studio.
  PatternflowWifi::activeSsid = "Studio";
  frames(30000 / 16);
  CHECK(stub::joins.size() == 1);                 // not before a minute
  frames(32000 / 16);
  CHECK(stub::joins.size() == 2);
  // The core in the middle of a join: left alone.
  stub::wifiStatus = WL_DISCONNECTED;
  frames(70000 / 16);
  CHECK(stub::joins.size() == 2);
  stub::wifiStatus = WL_CONNECTED;
  // Home, and the hub is back: found again, nothing more to try.
  PatternflowWifi::activeSsid = "PatternflowEnsemble";
  hubUp = true;
  frames(60);
  CHECK(hub.panel(me).online);
  frames(70000 / 16);
  CHECK(stub::joins.size() == 2);

  // "Which panel is this?"
  stub::drawn.clear();
  // The console's order to sleep reaches the sketch once, and so does the
  // one to wake; a second asking finds nothing.
  bool wantSleep = false;
  CHECK(!F.requestSleep(&wantSleep));
  hub.setSleep(me, true);
  frames(6);
  CHECK(F.requestSleep(&wantSleep) && wantSleep);
  CHECK(!F.requestSleep(&wantSleep));
  stub::sleeping = true;                     // the sketch did it
  frames(6);
  CHECK(!F.requestSleep(&wantSleep));         // and is not asked again while it sleeps
  hub.setSleep(me, false);
  frames(6);
  CHECK(F.requestSleep(&wantSleep) && !wantSleep);
  stub::sleeping = false;
  frames(3);

  // Another feature has the panel (a show is playing): the feature stands
  // back. The hand's detents stay in the frame, no lane is laid over the
  // pattern, a pattern that feature switches to is not taken back, and the
  // hub hears it. Afterwards the panel is the ensemble's again.
  {
    static bool showPlaying = false;
    PatternflowEnsemble::soloNow = [] { return showPlaying; };
    frames(10);
    frame(0, 2);
    CHECK(input.knobDeltas[0] == 0 && input.knobAudioActive[0]);      // the ensemble's knob, as before
    const int hubPattern = pattern;
    showPlaying = true;
    frames(3);
    frame(0, 2);
    CHECK(input.knobDeltas[0] == 2);
    CHECK(!input.knobAudioActive[0]);
    pattern = (hubPattern + 1) % NUM_PATTERNS;                        // the show's pattern
    frames(120);
    CHECK(pattern == (hubPattern + 1) % NUM_PATTERNS);
    CHECK(hub.panel(me).rep.flags & pfe::RF_SOLO);
    showPlaying = false;
    frames(120);
    CHECK(pattern == hubPattern);
    CHECK(!(hub.panel(me).rep.flags & pfe::RF_SOLO));
    frame(0, 1);
    CHECK(input.knobDeltas[0] == 0 && input.knobAudioActive[0]);
    frame(0, -3);                                                     // the knob back where this began
    frames(10);
    PatternflowEnsemble::soloNow = nullptr;
  }

  // The sketch starts its countdown to show the new pattern's name; the
  // feature has it back at zero before the name would be drawn.
  contentNoticeTimer = 1.0f;
  frames(1);
  CHECK(contentNoticeTimer == 0.0f);

  stub::lastShape.clear();
  frames(3);
  CHECK(stub::lastShape.empty());
  hub.identify(me, true);
  frames(6);
  CHECK(stub::drawn.empty());                                  // the mark alone, no number
  CHECK(stub::lastShape == "circle" && stub::lastFill == theDisplay.color565(255, 0, 0));   // 1: a red circle
  // The mark follows the number the console gives it: 6 is a green triangle.
  const int six = 6;
  hub.editPanel(me, nullptr, nullptr, &six, nullptr, nullptr);
  frames(6);
  CHECK(stub::lastShape == "triangle" && stub::lastFill == theDisplay.color565(0, 220, 0));
  const int one = 1;
  hub.editPanel(me, nullptr, nullptr, &one, nullptr, nullptr);
  hub.identify(me, false);
  frames(6);

  // /api/status.
  String json;
  F.appendStatus(json);
  const std::string st = json.c_str();
  CHECK(st.rfind(",\"ensemble\":{", 0) == 0 && st.back() == '}');
  CHECK(st.find("\"uid\":\"246f28010203\"") != std::string::npos);
  CHECK(st.find("\"linked\":true") != std::string::npos);
  CHECK(st.find("\"id\":1") != std::string::npos);
  CHECK(st.find("\"hub\":\"192.168.66.1\"") != std::string::npos);
  CHECK(st.find("\"knob\":[594,800,500,500]") != std::string::npos);

  // The hub goes away: after five seconds the panel calls out again, and
  // the knobs go on working meanwhile.
  hubUp = false;
  stub::inbox.clear();
  toBroadcast = 0;
  frames(250);                                  // four seconds
  CHECK(toBroadcast == 0);
  frame(2, -2);
  CHECK(std::fabs(input.knobAudioValue[2] - 0.438f) < 1e-3f);
  frames(120);
  CHECK(toBroadcast > 0);
  CHECK(stub::serial.find("hub silent") != std::string::npos);
  json = String();
  F.appendStatus(json);
  CHECK(std::string(json.c_str()).find("\"linked\":false") != std::string::npos);
  // It comes back: found again, and the turn made meanwhile is told.
  hubUp = true;
  frames(40);
  CHECK(lastTo == HUB_IP);
  CHECK(hub.panel(me).tKnob[2] == 438);

  // A reboot from the console: 300 ms later, once.
  CHECK(stub::restarts == 0);
  hub.rebootPanel(me);
  frames(4);
  CHECK(stub::restarts == 0);
  frames(30);
  CHECK(stub::restarts >= 1);

  std::printf("%s  %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
