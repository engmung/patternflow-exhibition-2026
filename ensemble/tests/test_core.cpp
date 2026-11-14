// Patternflow Ensemble - host tests for the core (no board).
//
// Builds the same headers the hub, the panels and the console's demo use,
// and scripts whole installations through pfe_sim.h: virtual panels on a
// lossy virtual network, a real Conductor, real Followers.
//
//   g++ -std=c++17 -O1 -fsanitize=address,undefined -o test_core tests/test_core.cpp && ./test_core
//
// License: MIT
#include "../core/pfe_sim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace pfe;

static int g_checks = 0, g_failed = 0;
static const char* g_test = "";

#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_failed++;                                                                \
      std::printf("  FAIL %s:%d [%s]  %s\n", __FILE__, __LINE__, g_test, #cond); \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                                       \
  do {                                                                                       \
    g_checks++;                                                                              \
    const long long va = (long long)(a), vb = (long long)(b);                                \
    if (va != vb) {                                                                          \
      g_failed++;                                                                            \
      std::printf("  FAIL %s:%d [%s]  %s = %lld, expected %s = %lld\n", __FILE__, __LINE__,  \
                  g_test, #a, va, #b, vb);                                                   \
    }                                                                                        \
  } while (0)

// ── A fresh installation per test ───────────────────────────────────────
alignas(Sim) static unsigned char g_store[sizeof(Sim)];
static Sim* sim = nullptr;
static uint32_t T = 0;   // the test's clock, ms

static void fresh(int panels, uint32_t seed = 7) {
  if (sim) sim->~Sim();
  sim = new (g_store) Sim();
  T = 100000;   // not zero: a clock near its start hides wrap mistakes
  sim->begin(panels, seed, T);
  T = sim->now();
}
static void run(uint32_t ms) {
  while (ms) {
    const uint32_t step = ms > 1000 ? 1000 : ms;
    T += step;
    sim->tick(T);
    ms -= step;
  }
}
static void cmd(const std::string& line) { sim->command(line.c_str(), line.size()); }
static int hub(int simIdx) { return sim->conductor().findPanel(sim->panel(simIdx).uid); }
static const Panel& hp(int simIdx) { return sim->conductor().panel(hub(simIdx)); }
static uint16_t val(int simIdx, int k) { return sim->panel(simIdx).f.value(k); }
static std::string uidOf(int simIdx) {
  char b[16];
  const uint8_t* u = sim->panel(simIdx).uid;
  std::snprintf(b, sizeof(b), "%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2], u[3], u[4], u[5]);
  return b;
}
static std::vector<std::string> lines() {
  std::vector<std::string> out;
  std::string all(sim->out(), sim->outLen());
  size_t a = 0;
  while (a < all.size()) {
    size_t b = all.find('\n', a);
    if (b == std::string::npos) b = all.size();
    out.push_back(all.substr(a, b - a));
    a = b + 1;
  }
  return out;
}
static bool anyLine(const char* frag1, const char* frag2 = nullptr) {
  for (const auto& l : lines())
    if (l.find(frag1) != std::string::npos && (!frag2 || l.find(frag2) != std::string::npos)) return true;
  return false;
}
static void links(const char* knobs, const char* pattern) {
  cmd(std::string("{\"c\":\"cfg\",\"link\":[\"") + knobs + "\",\"" + knobs + "\",\"" + knobs + "\",\"" +
      knobs + "\"],\"plink\":\"" + pattern + "\"}");
}
static bool allValue(int k, uint16_t v) {
  for (int i = 0; i < sim->count(); i++)
    if (val(i, k) != v) return false;
  return true;
}
static bool allPattern(int p) {
  for (int i = 0; i < sim->count(); i++)
    if (sim->panel(i).actual != p) return false;
  return true;
}

// ── 1. Bytes ────────────────────────────────────────────────────────────
static void testCodec() {
  g_test = "codec";
  Report r;
  r.flags = RF_BOOT;
  const uint8_t uid[6] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
  std::memcpy(r.uid, uid, 6);
  r.seq = 0x0102;
  r.ack = 0x0304;
  r.pattern = 3;
  r.patTouch = 9;
  r.knob[0] = 0; r.knob[1] = 250; r.knob[2] = 1000; r.knob[3] = 531;
  r.touch[0] = 1; r.touch[1] = 2; r.touch[2] = 3; r.touch[3] = 255;
  r.rssi = -52;
  r.fps = 60;
  r.uptimeS = 0x01020304;
  uint8_t b[MAX_PACKET];
  CHECK_EQ(encode(r, b), REPORT_LEN);
  const uint8_t expect[REPORT_LEN] = {'P', 'F', 'E', 1, 0x01, 0x01, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6,
                                      0x02, 0x01, 0x04, 0x03, 3, 9, 0x00, 0x00, 0xfa, 0x00, 0xe8, 0x03,
                                      0x13, 0x02, 1, 2, 3, 255, 0xcc, 60, 0x04, 0x03, 0x02, 0x01};
  CHECK(std::memcmp(b, expect, REPORT_LEN) == 0);
  Report r2;
  CHECK(decode(b, REPORT_LEN, &r2));
  CHECK_EQ(r2.seq, r.seq);
  CHECK_EQ(r2.ack, r.ack);
  CHECK_EQ(r2.knob[3], 531);
  CHECK_EQ(r2.touch[3], 255);
  CHECK_EQ(r2.rssi, -52);
  CHECK_EQ(r2.uptimeS, r.uptimeS);
  CHECK(std::memcmp(r2.uid, uid, 6) == 0);

  // Too short, wrong magic, wrong version, wrong type: not ours.
  CHECK(!decode(b, REPORT_LEN - 1, &r2));
  b[0] = 'X';
  CHECK(!decode(b, REPORT_LEN, &r2));
  b[0] = 'P';
  b[3] = 2;
  CHECK(!decode(b, REPORT_LEN, &r2));
  b[3] = 1;
  State sx;
  CHECK(!decode(b, REPORT_LEN, &sx));
  // Longer is fine: a later version may append.
  uint8_t longer[REPORT_LEN + 8];
  std::memset(longer, 0xEE, sizeof(longer));
  std::memcpy(longer, b, REPORT_LEN);
  CHECK(decode(longer, sizeof(longer), &r2));
  // A knob above the scale is clamped, not believed.
  putU16(b + 18, 5000);
  CHECK(decode(b, REPORT_LEN, &r2));
  CHECK_EQ(r2.knob[0], 1000);

  State s;
  s.flags = SF_K4_PATTERN | SF_IDENTIFY;
  s.seq = 77;
  s.panelId = 4;
  s.pattern = 2;
  s.patAck = 5;
  s.patMin = 1;
  s.patMax = 6;
  s.k4Steps = 3;
  s.knob[0] = 10; s.knob[1] = 20; s.knob[2] = 30; s.knob[3] = 40;
  s.ack[0] = 4; s.ack[1] = 3; s.ack[2] = 2; s.ack[3] = 1;
  s.glideMs = 80;
  s.hubMs = 123456;
  CHECK_EQ(encode(s, b), STATE_LEN);
  State s2;
  CHECK(decode(b, STATE_LEN, &s2));
  CHECK_EQ(s2.flags, s.flags);
  CHECK_EQ(s2.seq, 77);
  CHECK_EQ(s2.panelId, 4);
  CHECK_EQ(s2.patMax, 6);
  CHECK_EQ(s2.knob[2], 30);
  CHECK_EQ(s2.ack[3], 1);
  CHECK_EQ(s2.glideMs, 80);
  CHECK_EQ(s2.hubMs, 123456);

  Info m;
  std::memcpy(m.uid, uid, 6);
  m.pattern = 1;
  m.patternCount = 8;
  m.detents = 32;
  m.k4Steps = 3;
  setText(m.fw, sizeof(m.fw), "3.12.0");
  setText(m.variant, sizeof(m.variant), "ensemble v0.1.0");
  setText(m.patternName, sizeof(m.patternName), "A name that is far too long for the field");
  CHECK_EQ(encode(m, b), INFO_LEN);
  Info m2;
  CHECK(decode(b, INFO_LEN, &m2));
  CHECK(std::strcmp(m2.fw, "3.12.0") == 0);
  CHECK(std::strcmp(m2.variant, "ensemble v0.1.0") == 0);
  CHECK_EQ(std::strlen(m2.patternName), 23);
  // An unterminated field off the air still ends in a zero.
  std::memset(b + 48, 'x', 24);
  CHECK(decode(b, INFO_LEN, &m2));
  CHECK_EQ(std::strlen(m2.patternName), 23);

  Cmd c;
  c.cmd = CMD_REBOOT;
  CHECK_EQ(encode(c, b), CMD_LEN);
  Cmd c2;
  CHECK(decode(b, CMD_LEN, &c2));
  CHECK_EQ(c2.cmd, CMD_REBOOT);
  b[6] = 0;   // the guard
  CHECK(!decode(b, CMD_LEN, &c2));
}

// ── 2. One panel, by hand ───────────────────────────────────────────────
static void testFollower() {
  g_test = "follower";
  Follower f;
  FollowerConfig cfg;
  const uint8_t uid[6] = {2, 0, 0, 0, 0, 9};
  f.begin(uid, cfg, 5000);
  FrameIn in;
  FrameOut out;
  in.nowMs = 5016;
  in.pattern = 0;
  in.patternCount = 8;
  f.frame(in, out);
  CHECK(out.ownKnob[0] && out.laneActive[0]);
  CHECK(std::fabs(out.lane[0] - 0.5f) < 1e-6f);
  CHECK(f.booting());

  // Three detents up: 16 -> 19 of 32.
  in.nowMs += 16;
  in.delta[0] = 3;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 594);   // round(19 * 1000 / 32)
  CHECK_EQ(f.touch(0), 1);
  CHECK(std::fabs(out.lane[0] - 0.594f) < 1e-4f);
  // The ends hold.
  in.delta[0] = 100;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 1000);
  in.delta[0] = -200;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 0);
  in.delta[0] = 0;
  CHECK_EQ(f.touch(0), 3);

  // A press: that knob's lane is off for the frame, the position stays.
  in.pressed[1] = true;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(!out.laneActive[1] && out.laneActive[0]);
  in.pressed[1] = false;

  // A STATE that has not seen the last turn is not obeyed...
  State s;
  s.seq = 1;
  s.pattern = 0;
  s.knob[0] = 800;
  s.ack[0] = 2;   // the panel is at touch 3
  s.glideMs = 0;
  uint8_t b[MAX_PACKET];
  CHECK(f.onPacket(b, encode(s, b), in.nowMs));
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 0);
  CHECK(!f.booting());
  // ...and one that has is.
  s.seq = 2;
  s.ack[0] = 3;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 800);
  CHECK(std::fabs(out.lane[0] - 0.8f) < 1e-6f);
  CHECK_EQ(f.touch(0), 3);   // the hub moving a knob is not a touch

  // An older STATE arriving late changes nothing.
  s.seq = 1;
  s.knob[0] = 100;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 800);

  // Glide: 800 -> 400 over 160 ms is ten frames, not one.
  s.seq = 3;
  s.knob[0] = 400;
  s.glideMs = 160;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.value(0), 400);            // the position is there
  CHECK(out.lane[0] < 0.8f && out.lane[0] > 0.72f);   // the picture is on its way
  for (int i = 0; i < 12; i++) { in.nowMs += 16; f.frame(in, out); }
  CHECK(std::fabs(out.lane[0] - 0.4f) < 1e-6f);
  // A hand during a glide lands at once, from the position.
  s.seq = 4;
  s.knob[0] = 1000;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  in.delta[0] = -1;
  in.nowMs += 16;
  f.frame(in, out);
  in.delta[0] = 0;
  CHECK_EQ(f.value(0), 969);            // 32 -> 31 detents
  CHECK(std::fabs(out.lane[0] - 0.969f) < 1e-4f);

  // While a device screen has the knobs the engine leaves them alone.
  in.chrome = true;
  in.delta[1] = 5;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(!out.ownKnob[1]);
  CHECK_EQ(f.touch(1), 0);
  in.chrome = false;
  in.delta[1] = 0;

  // The pattern from the hub: asked once, then seen running.
  s.seq = 5;
  s.ack[0] = f.touch(0);
  s.knob[0] = f.value(0);
  s.pattern = 3;
  s.patAck = 0;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  int idx = -1;
  bool fromHub = false;
  CHECK(f.takePattern(&idx, &fromHub));
  CHECK_EQ(idx, 3);
  CHECK(fromHub);
  CHECK(!f.takePattern(&idx, &fromHub));
  in.pattern = 3;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.patTouch(), 0);            // the hub's pattern is not a person's pick
  CHECK(!f.takePattern(&idx, &fromHub));

  // Somebody picks one on the panel itself: that is a touch.
  in.pattern = 5;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK_EQ(f.patTouch(), 1);
  CHECK_EQ(f.desiredPattern(), 5);
  // The hub's old target (patAck 0) no longer applies.
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(!f.takePattern(&idx, &fromHub));

  // K4 as the pattern switch: three detents a step, wrapping in 1..6.
  s.seq = 6;
  s.flags = SF_K4_PATTERN;
  s.pattern = 5;
  s.patAck = 1;
  s.patMin = 1;
  s.patMax = 6;
  s.k4Steps = 3;
  f.onPacket(b, encode(s, b), in.nowMs);
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(out.ownKnob[3] && !out.laneActive[3]);
  in.delta[3] = 2;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(!f.takePattern(&idx, &fromHub));   // two detents: not a step yet
  in.delta[3] = 1;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(f.takePattern(&idx, &fromHub));
  CHECK_EQ(idx, 6);
  CHECK(!fromHub);
  CHECK_EQ(f.patTouch(), 2);
  in.pattern = 6;
  in.delta[3] = 3;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(f.takePattern(&idx, &fromHub));
  CHECK_EQ(idx, 1);                        // 6 wraps to 1
  in.pattern = 1;
  in.delta[3] = -3;
  in.nowMs += 16;
  f.frame(in, out);
  CHECK(f.takePattern(&idx, &fromHub));
  CHECK_EQ(idx, 6);                        // and back
  in.pattern = 6;
  in.delta[3] = 0;
  CHECK_EQ(f.touch(3), 0);                 // K4 is not a knob in this mode

  // A pattern this panel does not have: asked five times, then given up,
  // and the same target is not taken up again.
  s.seq = 7;
  s.pattern = 40;
  s.patAck = f.patTouch();
  f.onPacket(b, encode(s, b), in.nowMs);
  int asks = 0;
  for (int i = 0; i < 600; i++) {          // ten seconds of frames
    in.nowMs += 16;
    f.frame(in, out);
    if (f.takePattern(&idx, &fromHub)) asks++;   // the device fails to load it
  }
  CHECK_EQ(asks, 5);
  CHECK_EQ(f.patternRefusals(), 1);
  CHECK_EQ(f.desiredPattern(), 6);

  // Reports: at once when a hand moves (33 ms apart at most), every 500 ms otherwise.
  uint8_t buf[MAX_PACKET];
  while (f.poll(buf, sizeof(buf), in.nowMs)) {}
  in.nowMs += 16;
  in.delta[1] = 1;
  f.frame(in, out);
  in.delta[1] = 0;
  CHECK_EQ(f.poll(buf, sizeof(buf), in.nowMs), 0);          // 16 ms since the last
  CHECK_EQ(f.poll(buf, sizeof(buf), in.nowMs + 20), REPORT_LEN);
  Report r;
  CHECK(decode(buf, REPORT_LEN, &r));
  CHECK_EQ(r.touch[1], 1);
  CHECK_EQ(r.pattern, 6);
  CHECK_EQ(r.ack, 7);
  CHECK(!(r.flags & RF_BOOT));
  CHECK_EQ(f.poll(buf, sizeof(buf), in.nowMs + 400), 0);
  CHECK_EQ(f.poll(buf, sizeof(buf), in.nowMs + 530), REPORT_LEN);

  // A reboot command needs its guard.
  Cmd c;
  c.cmd = CMD_REBOOT;
  size_t n = encode(c, b);
  b[7] = 'X';
  f.onPacket(b, n, in.nowMs);
  CHECK(!f.rebootRequested());
  b[7] = 'F';
  f.onPacket(b, n, in.nowMs);
  CHECK(f.rebootRequested());
}

// ── 3. Installations ────────────────────────────────────────────────────
static void testJoin() {
  g_test = "join";
  fresh(6);
  run(1500);
  CHECK_EQ(sim->conductor().panelCount(), 6);
  CHECK_EQ(sim->conductor().onlineCount(), 6);
  for (int i = 0; i < 6; i++) {
    CHECK_EQ(hp(i).id, i + 1);                    // numbered in the order they came
    CHECK(std::fabs(hp(i).x - (float)i) < 1e-6f); // in a row, one unit apart
    CHECK(sim->panel(i).f.linked(T));
    CHECK_EQ(sim->panel(i).f.panelId(), i + 1);
    CHECK(!sim->panel(i).f.booting());
  }
  CHECK(anyLine("\"t\":\"log\"", "joined"));
  CHECK(anyLine("\"t\":\"names\"", "Origin"));
  CHECK(anyLine("\"t\":\"p\"", "\"fw\":\"3.12.0\""));
  CHECK(anyLine("\"t\":\"hub\"", "\"online\":6"));
  // Every line is one whole JSON object.
  for (const auto& l : lines()) {
    if (l.empty()) continue;
    CHECK(l.front() == '{' && l.back() == '}');
  }
  // A seventh report from a full table would not crash; nothing is "bad".
  CHECK_EQ(sim->conductor().stats().bad, 0);
  // The list a script reads: every panel, its number, uid and address.
  static char list[6144];
  Out lo(list, sizeof(list));
  sim->conductor().writePanels(lo);
  CHECK(!lo.overflow);
  const std::string panels(list);
  CHECK(panels.front() == '[' && panels.back() == ']');
  CHECK(panels.find("{\"id\":1,\"uid\":\"025046000001\",\"n\":\"\",\"on\":1,\"ip\":\"192.168.66.2\",\"fw\":\"3.12.0\",\"var\":\"ensemble sim\"}") != std::string::npos);
  CHECK(panels.find("\"id\":6,\"uid\":\"025046000006\"") != std::string::npos);
  // Twenty-four of them still fit the hub's buffer.
  fresh(24, 2);
  run(1500);
  for (int i = 0; i < 24; i++)
    cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(i) + "\",\"name\":\"\uc77c\uc774\uc0bc\uc0ac\uc624\uc721\uce60\"}");
  Out full(list, sizeof(list));
  sim->conductor().writePanels(full);
  CHECK(!full.overflow);
  CHECK(full.len < 4200);
}

static void testSync() {
  g_test = "sync";
  fresh(8);
  links("sync", "sync");
  run(500);
  sim->hand(2, 0, 5);            // K1 on the third panel, five detents up
  run(40);
  CHECK_EQ(val(2, 0), 656);      // 21 of 32: the hand's own panel moved at once
  run(160);
  CHECK(allValue(0, 656));       // and everybody else within a fifth of a second
  CHECK(allValue(1, 500));       // only K1
  // The other way, from another panel.
  sim->hand(7, 0, -8);
  run(200);
  CHECK(allValue(0, 406));
  CHECK_EQ(hp(2).hands, 1);
  CHECK_EQ(hp(7).hands, 1);
  CHECK_EQ(hp(0).hands, 0);      // following is not a hand: no echo, no loop
  // The pattern, with K4.
  sim->hand(0, 3, 3);
  run(250);
  CHECK(allPattern(1));
  sim->hand(4, 3, -3);
  run(250);
  CHECK(allPattern(0));
  // It settles: the hub sees every panel on its target, and stops talking fast.
  run(1500);
  for (int i = 0; i < 8; i++) CHECK(anyLine(("\"i\":" + std::to_string(hub(i)) + ",").c_str(), "\"ok\":1"));
  const uint32_t tx0 = sim->conductor().stats().tx;
  run(5000);
  const uint32_t perPanelPerSecond = (sim->conductor().stats().tx - tx0) / 8 / 5;
  CHECK(perPanelPerSecond <= 2);
}

static void testRipple() {
  g_test = "ripple";
  fresh(6);
  links("ripple", "ripple");
  cmd("{\"c\":\"cfg\",\"speed\":2,\"glide\":0}");   // two units a second: 500 ms a panel
  run(500);
  sim->outClear();
  sim->hand(0, 1, 4);            // K2 on the first panel
  const uint32_t t0 = T;
  uint32_t arrived[6] = {0, 0, 0, 0, 0, 0};
  for (int step = 0; step < 400; step++) {
    run(10);
    for (int i = 0; i < 6; i++)
      if (!arrived[i] && val(i, 1) == 625) arrived[i] = T - t0;
  }
  for (int i = 0; i < 6; i++) {
    CHECK(arrived[i] != 0);
    const int expect = i * 500;
    // Not early, and not late by more than the air and a frame or two.
    CHECK((int)arrived[i] >= expect - 5);
    CHECK((int)arrived[i] <= expect + 90);
  }
  CHECK(anyLine("\"t\":\"w\"", "\"o\":0"));
  CHECK_EQ(sim->conductor().inFlight(), 0);   // landed everywhere: the list is empty again

  // From the middle it goes both ways.
  sim->hand(3, 1, -4);
  const uint32_t t1 = T;
  uint32_t got[6] = {0, 0, 0, 0, 0, 0};
  for (int step = 0; step < 250; step++) {
    run(10);
    for (int i = 0; i < 6; i++)
      if (!got[i] && val(i, 1) == 500) got[i] = T - t1;
  }
  CHECK(got[2] >= 495 && got[2] <= 590);
  CHECK(got[4] >= 495 && got[4] <= 590);
  CHECK(got[0] >= 1495 && got[0] <= 1590);
  CHECK(got[5] >= 995 && got[5] <= 1090);

  // Two people, two ends, a second apart: the waves cross, and in the end
  // every panel shows what the second person did.
  sim->hand(0, 0, 6);       // 688
  run(1000);
  sim->hand(5, 0, -6);      // 313
  run(400);
  CHECK_EQ(val(1, 0), 688); // the first wave has passed panel 2 (500 ms)
  CHECK_EQ(val(4, 0), 500); // the second (500 ms out) and the first (2000 ms out) are both still short
  run(200);
  CHECK_EQ(val(4, 0), 313); // the second wave is there
  run(3500);
  CHECK(allValue(0, 313));
  CHECK_EQ(sim->conductor().inFlight(), 0);

  // A sweep: the far panel replays it later, value by value, in order.
  sim->outClear();
  std::vector<uint16_t> seen;
  uint16_t last = val(5, 2);
  for (int step = 0; step < 60; step++) {   // one detent every 50 ms on panel 1
    sim->hand(0, 2, 1);
    for (int ms = 0; ms < 50; ms += 10) {
      run(10);
      if (val(5, 2) != last) { last = val(5, 2); seen.push_back(last); }
    }
  }
  for (int step = 0; step < 400; step++) {
    run(10);
    if (val(5, 2) != last) { last = val(5, 2); seen.push_back(last); }
  }
  CHECK(seen.size() >= 12);                 // 16 detents to the top; most of them seen one by one
  bool rising = true;
  for (size_t i = 1; i < seen.size(); i++) rising = rising && seen[i] > seen[i - 1];
  CHECK(rising);
  CHECK(allValue(2, 1000));
  CHECK_EQ(sim->conductor().stats().dropped, 0);
}

static void testSoloAndUnlinked() {
  g_test = "solo";
  fresh(4);
  cmd("{\"c\":\"cfg\",\"link\":[\"solo\",\"sync\",\"sync\",\"sync\"],\"plink\":\"solo\"}");
  run(300);
  sim->hand(1, 0, 4);
  sim->hand(1, 3, 3);        // pattern on
  run(600);
  CHECK_EQ(val(1, 0), 625);
  CHECK_EQ(val(0, 0), 500);
  CHECK_EQ(val(2, 0), 500);
  CHECK_EQ(sim->panel(1).actual, 1);
  CHECK_EQ(sim->panel(0).actual, 0);
  // A panel taken out of the links neither follows nor leads.
  cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(3) + "\",\"linked\":false}");
  sim->hand(0, 1, 4);
  run(300);
  CHECK_EQ(val(1, 1), 625);
  CHECK_EQ(val(3, 1), 500);
  sim->hand(3, 1, -4);
  run(300);
  CHECK_EQ(val(3, 1), 375);
  CHECK_EQ(val(0, 1), 625);
}

static void testConsoleSets() {
  g_test = "console";
  fresh(5);
  links("sync", "sync");
  run(300);
  // A pattern each, straight from the console.
  for (int i = 0; i < 5; i++)
    cmd("{\"c\":\"set\",\"to\":\"" + uidOf(i) + "\",\"pat\":" + std::to_string(i + 1) + "}");
  run(400);
  for (int i = 0; i < 5; i++) CHECK_EQ(sim->panel(i).actual, i + 1);
  // By number as well, with knobs; nothing travels to the others.
  cmd("{\"c\":\"set\",\"to\":\"#3\",\"k\":[100,null,900,null],\"glide\":0}");
  run(300);
  CHECK_EQ(val(2, 0), 100);
  CHECK_EQ(val(2, 1), 500);
  CHECK_EQ(val(2, 2), 900);
  CHECK_EQ(val(1, 0), 500);
  // Everybody at once.
  cmd("{\"c\":\"set\",\"to\":\"all\",\"kn\":1,\"v\":250}");
  run(300);
  CHECK(allValue(1, 250));
  // Everybody, as a wave from panel 5.
  cmd("{\"c\":\"cfg\",\"speed\":4}");
  cmd("{\"c\":\"set\",\"to\":\"all\",\"pat\":7,\"mode\":\"ripple\",\"from\":\"#5\"}");
  run(120);
  CHECK_EQ(sim->panel(4).actual, 7);
  CHECK_EQ(sim->panel(0).actual, 1);
  run(1100);
  CHECK(allPattern(7));
  // A hand after that still leads (the console did not take the knobs away).
  sim->hand(0, 1, 2);
  run(300);
  CHECK(allValue(1, 313));

  // Replies.
  sim->outClear();
  cmd("{\"c\":\"cfg\",\"speed\":3.5,\"r\":12}");
  CHECK(anyLine("\"t\":\"ack\"", "\"r\":12"));
  cmd("{\"c\":\"cfg\",\"link\":[\"fast\"]}");
  CHECK(anyLine("\"t\":\"err\"", "solo, sync or ripple"));
  cmd("{\"c\":\"set\",\"to\":\"0000000000aa\",\"pat\":1}");
  CHECK(anyLine("\"t\":\"err\"", "no such panel"));
  cmd("{\"c\":\"nonsense\"}");
  CHECK(anyLine("\"t\":\"err\"", "unknown command"));
  // Not JSON, or no "c": boot logs share the port, so silence.
  sim->outClear();
  cmd("[boot] hello");
  cmd("{\"x\":1}");
  cmd("{\"c\":");
  cmd("");
  CHECK_EQ(sim->outLen(), 0);
  run(1);
  CHECK(std::fabs(sim->conductor().config().speed - 3.5f) < 1e-6f);

  // Names, numbers, places.
  cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(0) + "\",\"name\":\"입구 \\\"A\\\"\",\"x\":-2.5,\"y\":1.25,\"id\":5}");
  run(200);
  CHECK_EQ(hp(0).id, 5);
  CHECK_EQ(hp(4).id, 1);            // the two swapped numbers
  CHECK(std::fabs(hp(0).x + 2.5f) < 1e-6f);
  CHECK(std::strcmp(hp(0).name, "입구 \"A\"") == 0);
  CHECK(anyLine("\"t\":\"p\"", "\"n\":\"입구 \\\"A\\\"\""));
  CHECK_EQ(sim->panel(0).f.panelId(), 5);
  // A long Korean name is cut between characters, never inside one.
  cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(1) +
      "\",\"name\":\"가나다라마바사아자차\"}");
  CHECK_EQ(std::strlen(hp(1).name), 21);    // seven whole syllables of three bytes

  // Identify reaches the panel and switches itself off.
  cmd("{\"c\":\"identify\",\"uid\":\"" + uidOf(2) + "\",\"on\":true}");
  run(200);
  CHECK(sim->panel(2).out.identify);
  CHECK(!sim->panel(1).out.identify);
  run(31000);
  CHECK(!sim->panel(2).out.identify);

  // Reboot one panel from the console.
  const uint32_t boots = sim->panel(3).boots;
  cmd("{\"c\":\"reboot\",\"uid\":\"" + uidOf(3) + "\"}");
  run(500);
  CHECK_EQ(sim->panel(3).boots, boots + 1);

  // Forget.
  sim->power(4, false);
  run(3500);
  CHECK_EQ(sim->conductor().onlineCount(), 4);
  cmd("{\"c\":\"forget\",\"uid\":\"" + uidOf(4) + "\"}");
  CHECK_EQ(sim->conductor().panelCount(), 4);
  CHECK(anyLine("\"t\":\"gone\""));
}

static void testScenes() {
  g_test = "scenes";
  fresh(4);
  links("solo", "solo");
  cmd("{\"c\":\"cfg\",\"speed\":2,\"glide\":0}");
  run(300);
  for (int i = 0; i < 4; i++)
    cmd("{\"c\":\"set\",\"to\":\"" + uidOf(i) + "\",\"pat\":" + std::to_string(i + 2) + ",\"k\":[" +
        std::to_string(100 * (i + 1)) + ",500,500,500]}");
  run(400);
  cmd("{\"c\":\"scene\",\"op\":\"save\",\"slot\":0}");
  CHECK(sim->conductor().sceneUsed(0));
  cmd("{\"c\":\"set\",\"to\":\"all\",\"pat\":0,\"k\":[900,900,900,900]}");
  run(400);
  CHECK(allPattern(0));
  cmd("{\"c\":\"scene\",\"op\":\"save\",\"slot\":1}");
  // Back to the first, as a wave from panel 1.
  cmd("{\"c\":\"scene\",\"op\":\"recall\",\"slot\":0,\"mode\":\"ripple\",\"from\":\"#1\"}");
  run(120);
  CHECK_EQ(sim->panel(0).actual, 2);
  CHECK_EQ(val(0, 0), 100);
  CHECK_EQ(sim->panel(3).actual, 0);       // 1.5 s away
  run(1700);
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(sim->panel(i).actual, i + 2);
    CHECK_EQ(val(i, 0), 100 * (i + 1));
  }
  // And to the second at once.
  cmd("{\"c\":\"scene\",\"op\":\"recall\",\"slot\":1}");
  run(300);
  CHECK(allPattern(0));
  CHECK(allValue(0, 900));
  sim->outClear();
  cmd("{\"c\":\"scene\",\"op\":\"recall\",\"slot\":5}");
  CHECK(anyLine("\"t\":\"err\"", "empty scene"));
  cmd("{\"c\":\"scene\",\"op\":\"clear\",\"slot\":1}");
  CHECK(!sim->conductor().sceneUsed(1));
}

static void testLossyAir() {
  g_test = "loss";
  fresh(10, 99);
  links("ripple", "ripple");
  cmd("{\"c\":\"cfg\",\"speed\":6}");
  sim->network(20, 30, 300);       // 20-50 ms and three datagrams in ten lost
  run(2000);
  CHECK_EQ(sim->conductor().onlineCount(), 10);
  for (int step = 0; step < 20; step++) {   // a slow turn on one panel, a fast one on another
    sim->hand(2, 0, 1);
    sim->hand(8, 1, -1);
    run(70);
  }
  sim->hand(5, 3, 6);              // and two pattern steps
  run(6000);
  CHECK(allValue(0, val(2, 0)));
  CHECK(allValue(1, val(8, 1)));
  CHECK(val(2, 0) > 900);          // the hand's own panel went all the way (16 + 20 detents, clamped)
  CHECK(val(8, 1) == 0);
  CHECK(allPattern(2));
  CHECK(sim->dropped() > 100);     // the air really was lossy
  CHECK_EQ(sim->conductor().onlineCount(), 10);
  // Hands win, even through this: a panel being turned is never dragged back.
  uint16_t low = val(4, 2);
  bool wentBack = false;
  for (int step = 0; step < 30; step++) {
    sim->hand(4, 2, 1);
    cmd("{\"c\":\"set\",\"to\":\"all\",\"kn\":2,\"v\":0}");   // the console pulling the other way
    run(40);
    if (val(4, 2) < low) wentBack = true;
    low = val(4, 2);
  }
  CHECK(!wentBack);
  run(5000);
  // Let go, and the last thing said wins everywhere. The console spoke last
  // only if its final set came after the hand's final turn was heard.
  CHECK(allValue(2, val(4, 2)));
}

static void testReordering() {
  g_test = "reorder";
  fresh(6, 3);
  links("sync", "sync");
  // The air holds a datagram 5 to 125 ms: reports 33 ms apart overtake each other.
  sim->network(5, 120, 0);
  run(1000);
  for (int step = 0; step < 40; step++) {   // a steady turn, a detent every 40 ms
    sim->hand(1, 0, 1);
    run(40);
  }
  run(1500);
  CHECK(allValue(0, 1000));
  CHECK(sim->conductor().stats().stale > 0);   // late ones did arrive, and were set aside
  // The knob only ever went up on the panels that followed: an old report
  // believed would have sent them down again.
  sim->network(5, 120, 0);
  uint16_t low = val(4, 1);
  bool wentBack = false;
  for (int step = 0; step < 40; step++) {
    sim->hand(1, 1, 1);
    for (int ms = 0; ms < 40; ms += 8) {
      run(8);
      if (val(4, 1) < low) wentBack = true;
      low = val(4, 1);
    }
  }
  run(1500);
  CHECK(!wentBack);
  CHECK(allValue(1, 1000));
}

static void testDropout() {
  g_test = "dropout";
  fresh(5);
  links("sync", "sync");
  run(500);
  // Panel 2's Wi-Fi goes for five seconds. Somebody turns it meanwhile.
  sim->network(3, 4, 1000);          // nothing gets through, either way
  run(1000);
  sim->hand(1, 0, 6);                // 688, heard by nobody
  run(4000);
  CHECK(!hp(1).online);
  CHECK_EQ(val(1, 0), 688);
  CHECK_EQ(val(0, 0), 500);
  sim->network(3, 4, 0);
  run(1200);
  CHECK(hp(1).online);
  CHECK(allValue(0, 688));           // back within ten seconds: the turn counts
  // Away for longer than that, the ensemble's present wins over an old turn.
  sim->network(3, 4, 1000);
  run(500);
  sim->hand(1, 0, -12);              // 313 on a panel nobody hears
  run(12000);
  sim->network(3, 4, 0);
  run(1500);
  CHECK(allValue(0, 688));
  CHECK_EQ(hp(1).hands, 1);
}

static void testPowerCycle() {
  g_test = "power";
  fresh(5);
  links("sync", "sync");
  run(300);
  sim->hand(0, 0, 8);      // 750
  sim->hand(0, 3, 6);      // pattern 2
  run(500);
  CHECK(allValue(0, 750));
  CHECK(allPattern(2));
  // Panel 3 loses power and comes back with its defaults (500, pattern 0).
  sim->power(2, false);
  run(1000);
  sim->power(2, true);
  run(40);
  CHECK_EQ(val(2, 0), 500);
  run(700);
  CHECK_EQ(val(2, 0), 750);            // told everything again
  CHECK_EQ(sim->panel(2).actual, 2);
  // ...and the others were not disturbed by its defaults.
  for (int i = 0; i < 5; i++) CHECK_EQ(val(i, 0), 750);
  CHECK(allPattern(2));
  CHECK_EQ(hp(2).hands, 0);
  CHECK(anyLine("\"t\":\"log\"", "restarted"));
  // Off for longer than the hub waits: lost, then back.
  sim->power(4, false);
  run(4000);
  CHECK(!hp(4).online);
  CHECK(anyLine("\"t\":\"log\"", "lost"));
  sim->hand(1, 0, -8);     // the ensemble moves on without it
  run(400);
  sim->power(4, true);
  run(800);
  CHECK(hp(4).online);
  CHECK_EQ(val(4, 0), 500);            // 750 - 8 detents: where everybody is now
  CHECK(allValue(0, 500));
  CHECK(allPattern(2));
}

static void testHubRestart() {
  g_test = "hub restart";
  fresh(4);
  cmd("{\"c\":\"cfg\",\"link\":[\"sync\",\"ripple\",\"solo\",\"sync\"],\"plink\":\"sync\",\"speed\":3.25,\"glide\":40,\"k4\":true,\"pmin\":1,\"pmax\":6}");
  cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(1) + "\",\"x\":2.5,\"y\":-1,\"name\":\"door\"}");
  run(300);
  sim->hand(0, 0, 8);
  sim->hand(0, 3, 3);
  run(500);
  cmd("{\"c\":\"scene\",\"op\":\"save\",\"slot\":3}");
  CHECK(sim->conductor().persistDirty());
  CHECK(allValue(0, 750));
  CHECK(allPattern(1));

  // Save, load into a second hub object: the same setup comes back.
  static uint8_t blob[SAVE_MAX];
  const size_t n = sim->conductor().save(blob, sizeof(blob));
  CHECK(n > 0 && n <= SAVE_MAX);
  static Conductor other;
  other.reset();
  CHECK(other.load(blob, n));
  CHECK_EQ(other.panelCount(), 4);
  CHECK_EQ(other.config().link[1], LINK_RIPPLE);
  CHECK_EQ(other.config().link[2], LINK_SOLO);
  CHECK(std::fabs(other.config().speed - 3.25f) < 1e-6f);
  CHECK_EQ(other.config().patMax, 6);
  CHECK(other.sceneUsed(3));
  const int j = other.findPanel(sim->panel(1).uid);
  CHECK(j >= 0);
  CHECK(std::fabs(other.panel(j).x - 2.5f) < 1e-6f);
  CHECK(std::strcmp(other.panel(j).name, "door") == 0);
  static uint8_t blob2[SAVE_MAX];
  CHECK_EQ(other.save(blob2, sizeof(blob2)), n);
  CHECK(std::memcmp(blob, blob2, n) == 0);
  // A cut or damaged blob changes nothing.
  CHECK(!other.load(blob, n - 3));
  CHECK_EQ(other.panelCount(), 4);
  blob2[4] = 99;
  CHECK(!other.load(blob2, n));
  CHECK(!other.load(blob, 3));

  // The real thing: the hub goes away and a new one starts from what was saved.
  sim->restartHub();
  run(1500);
  CHECK_EQ(sim->conductor().onlineCount(), 4);
  CHECK(std::fabs(hp(1).x - 2.5f) < 1e-6f);
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(val(i, 0), 750);          // nobody jumped
    CHECK_EQ(sim->panel(i).actual, 1);
    CHECK_EQ(hp(i).hands, 0);          // and the hub did not take the rejoin for hands
  }
  sim->hand(3, 0, -4);
  run(400);
  CHECK(allValue(0, 625));             // still an ensemble
}

static void testK4AsKnob() {
  g_test = "k4 knob";
  fresh(3);
  cmd("{\"c\":\"cfg\",\"k4\":false,\"link\":[\"sync\",\"sync\",\"sync\",\"sync\"]}");
  run(300);
  sim->hand(1, 3, 4);
  run(300);
  CHECK(allValue(3, 625));
  CHECK(allPattern(0));
  CHECK(sim->panel(0).out.laneActive[3]);
  // The pattern picked on a panel's own menu still travels (plink is ripple by default).
  cmd("{\"c\":\"cfg\",\"plink\":\"sync\"}");
  sim->pick(2, 4);
  run(400);
  CHECK(allPattern(4));
}

static void testManyPanels() {
  g_test = "24 panels";
  fresh(24, 5);
  links("ripple", "ripple");
  run(1500);
  CHECK_EQ(sim->conductor().onlineCount(), 24);
  // A 6 x 4 grid.
  for (int i = 0; i < 24; i++)
    cmd("{\"c\":\"panel\",\"uid\":\"" + uidOf(i) + "\",\"x\":" + std::to_string(i % 6) + ",\"y\":" +
        std::to_string(i / 6) + "}");
  cmd("{\"c\":\"cfg\",\"speed\":3}");
  sim->hand(0, 0, 8);
  run(300);
  CHECK_EQ(val(1, 0), 500);            // one unit away: 333 ms
  run(100);
  CHECK_EQ(val(1, 0), 750);
  CHECK_EQ(val(6, 0), 750);            // the panel below, same distance
  CHECK_EQ(val(7, 0), 500);            // the diagonal: 471 ms
  run(120);
  CHECK_EQ(val(7, 0), 750);
  run(2500);
  CHECK(allValue(0, 750));             // the far corner is 5.83 units: 1.94 s
  // Everyone turning everything for ten seconds: nothing is lost for good.
  for (int step = 0; step < 200; step++) {
    for (int i = 0; i < 24; i += 3) sim->hand(i, (i / 3 + step) % 3, (step % 7) - 3);
    run(50);
  }
  run(6000);
  for (int k = 0; k < 3; k++) CHECK(allValue(k, val(0, k)));
  CHECK_EQ(sim->conductor().onlineCount(), 24);
  CHECK_EQ(sim->conductor().inFlight(), 0);
}

int main() {
  testCodec();
  testFollower();
  testJoin();
  testSync();
  testRipple();
  testSoloAndUnlinked();
  testConsoleSets();
  testScenes();
  testLossyAir();
  testReordering();
  testDropout();
  testPowerCycle();
  testHubRestart();
  testK4AsKnob();
  testManyPanels();
  std::printf("%s  %d checks, %d failed\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
