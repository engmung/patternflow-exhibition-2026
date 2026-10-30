// Native runner for ONE exhibit pattern header (tools/patterns/host/pfhost.py
// builds and drives it; see README.md).
//
// Compiled together with the REAL portable core headers (core_math, core_color,
// core_noise, core_tables, core_mem, core_params -> abi/pf_params, and
// core_canvas with its real frame mapping), the REAL InputFrame struct cut out
// of src/core_encoders.h, and small stubs for the hardware (stubs/).
//
// runner_config.h (written by pfhost.py) names the pattern:
//   PF_NS          the pattern's namespace
//   PF_K4_VAR      the variable update() hands to PFParams for K4, if found
//   PF_LANE_SCALE  net_config.h's PF_LANE_MOTION_SCALE
//
// Reads script.txt (the expanded script, one line per frame) from the working
// directory, writes frames.rgb (the captured frames, RGB888, in the pattern's
// own LOGICAL orientation - the panel rotation undone) and prints one JSON
// object on stdout.
//
// The frame is built the way the exhibit build builds it (core_exhibit.h
// driveLanes + patternflow.ino applyLaneMotion): K1-K3 arrive as lanes -
// knobAudioActive = !btnPressed[i], knobAudioValue = position - so
// PFParams::apply gives lo + (hi - lo) * position; knobDeltas carry the core's
// lane motion (which PFParams ignores under a lane); K4 never reaches the
// pattern (no lane, no delta, no press).
//
// License: MIT
#include "runner_config.h"
#include "pattern.h"

#include <chrono>
#include <string>
#include <vector>

MatrixPanel_I2S_DMA g_hostPanel;
MatrixPanel_I2S_DMA* dma_display = &g_hostPanel;

namespace {

struct Step {
  float pos[3];
  int pressMask;
  int heldMask;
  int capture;
};

// patternflow.ino applyLaneMotion(), for K1-K3 (no hand detents ever remain in
// the frame on the exhibit build: moveKnobs() takes them out first).
struct LaneMotion {
  bool wasActive[4] = {false, false, false, false};
  float prevValue[4] = {0, 0, 0, 0};
  float residual[4] = {0, 0, 0, 0};
  void apply(InputFrame& in) {
    for (int i = 0; i < 4; i++) {
      if (!in.knobAudioActive[i]) {
        wasActive[i] = false;
        residual[i] = 0.0f;
        in.knobAudioActive[i] = false;
        continue;
      }
      float value = constrain(in.knobAudioValue[i], 0.0f, 1.0f);
      if (!wasActive[i]) {
        prevValue[i] = 0.5f;
        residual[i] = 0.0f;
        wasActive[i] = true;
      }
      float movement = (value - prevValue[i]) * PF_LANE_SCALE + residual[i];
      int delta = (int)roundf(movement);
      residual[i] = movement - (float)delta;
      if (fabsf(residual[i]) < 0.001f) residual[i] = 0.0f;
      prevValue[i] = value;
      in.knobDeltas[i] = delta;
    }
  }
};

// The capture: the pattern's LOGICAL frame (what its JS canvas shows), read
// back out of the panel-shaped canvas with core_canvas.h's own mapping undone.
struct Capture {
  bool want = false;
  bool done = false;
  int presents = 0;
  int noPresent = 0;
  int w = 0, h = 0;
  std::vector<uint8_t> pix;
} g_cap;

void captureFrame(const uint8_t* canvas) {
  const int w = PFCanvas::frameW, h = PFCanvas::frameH;
  g_cap.w = w;
  g_cap.h = h;
  g_cap.pix.assign((size_t)w * h * 3, 0);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int px, py;
      if (PFCanvas::frameDirect) { px = x; py = y; }
      else if (PFCanvas::frameRotate) { px = PFCanvas::W - 1 - y; py = x; }
      else { px = x + PFCanvas::frameOffX; py = y + PFCanvas::frameOffY; }
      if ((unsigned)px >= (unsigned)PFCanvas::W || (unsigned)py >= (unsigned)PFCanvas::H) continue;
      const uint8_t* src = &canvas[((size_t)py * PFCanvas::W + px) * 3];
      uint8_t* d = &g_cap.pix[((size_t)y * w + x) * 3];
      d[0] = src[0]; d[1] = src[1]; d[2] = src[2];
    }
  }
  g_cap.done = true;
}

void onBlit(const uint8_t* rgb) {
  g_cap.presents++;
  if (g_cap.want) captureFrame(rgb);   // the last present() of the frame wins
}

double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
  int benchExtra = 0;
  for (int a = 1; a + 1 < argc; a++)
    if (std::string(argv[a]) == "--bench") benchExtra = atoi(argv[a + 1]);

  FILE* sf = fopen("script.txt", "r");
  if (!sf) { fprintf(stderr, "runner: cannot open script.txt\n"); return 2; }
  int n = 0;
  if (fscanf(sf, "frames %d", &n) != 1 || n <= 0) { fprintf(stderr, "runner: bad script header\n"); return 2; }
  std::vector<Step> steps((size_t)n);
  for (int f = 0; f < n; f++) {
    Step& s = steps[(size_t)f];
    if (fscanf(sf, "%f %f %f %d %d %d", &s.pos[0], &s.pos[1], &s.pos[2], &s.pressMask, &s.heldMask, &s.capture) != 6) {
      fprintf(stderr, "runner: bad script line %d\n", f + 1);
      return 2;
    }
  }
  fclose(sf);

  FILE* out = fopen("frames.rgb", "wb");
  if (!out) { fprintf(stderr, "runner: cannot write frames.rgb\n"); return 2; }

  const float dt = 1.0f / 30.0f;
  g_hostMillis = 1000;
  auto t0 = std::chrono::steady_clock::now();
  PF_NS::setup();
  const double setupMs = msSince(t0);

  double k4 = 0.0;
  int k4Known = 0;
#ifdef PF_K4_VAR
  k4 = (double)(PF_K4_VAR);   // K4 never reaches the pattern: this is what it keeps
  k4Known = 1;
#endif

  LaneMotion lanes;
  std::vector<double> ms;
  ms.reserve((size_t)(n + benchExtra));
  g_hostBlitHook = onBlit;
  int capW = 0, capH = 0, captured = 0;
  bool sizeChanged = false;
  const int total = n + benchExtra;

  for (int f = 1; f <= total; f++) {
    const Step& s = steps[(size_t)((f <= n ? f : n) - 1)];
    const bool inScript = f <= n;
    InputFrame input{};
    input.now = 1000u + (uint32_t)((f * 1000) / 30);
    g_hostMillis = input.now;
    for (int i = 0; i < 3; i++) {
      const bool pressed = inScript && ((s.pressMask >> i) & 1);
      input.btnPressed[i] = pressed;
      input.btnHeld[i] = inScript && ((s.heldMask >> i) & 1);
      input.knobAudioActive[i] = !pressed;       // core_exhibit.h driveLanes()
      input.knobAudioValue[i] = s.pos[i];
    }
    lanes.apply(input);

    g_cap.want = inScript && s.capture;
    g_cap.done = false;
    g_cap.presents = 0;
    auto f0 = std::chrono::steady_clock::now();
    PF_NS::update(dt, input);
    PF_NS::draw();
    const double frameMs = msSince(f0);
    if (!g_cap.want) ms.push_back(frameMs);   // a capture's copy is not the pattern's time

    if (inScript && s.capture) {
      // A preset presents its own frame (the registry's draw() is the
      // pattern's draw(), nothing after it); the blit hook took the frame
      // then. One that never presents is read back from the canvas here.
      if (!g_cap.done) { captureFrame(PFCanvas::buffer); g_cap.noPresent++; }
      if (g_cap.w != capW || g_cap.h != capH) {
        if (captured == 0) { capW = g_cap.w; capH = g_cap.h; }
        else sizeChanged = true;
      }
      fwrite(g_cap.pix.data(), 1, g_cap.pix.size(), out);
      captured++;
    }
  }
  fclose(out);

  // Steady state: skip the first 10 frames (first-touch page faults, lazy
  // table builds), then median / p90 over everything else.
  std::vector<double> steady(ms.begin() + (ms.size() > 20 ? 10 : 0), ms.end());
  std::sort(steady.begin(), steady.end());
  auto pct = [&](double p) { return steady.empty() ? 0.0 : steady[(size_t)(p * (steady.size() - 1))]; };
  double mean = 0;
  for (double v : steady) mean += v;
  if (!steady.empty()) mean /= (double)steady.size();

  printf("{\"w\":%d,\"h\":%d,\"captured\":%d,\"frames\":%d,\"benchFrames\":%d,\"sizeChanged\":%s,"
         "\"setupMs\":%.4f,\"medianMs\":%.5f,\"p10Ms\":%.5f,\"p90Ms\":%.5f,\"meanMs\":%.5f,\"maxMs\":%.5f,"
         "\"k4\":%.9g,\"k4Known\":%d}\n",
         capW, capH, captured, n, benchExtra, sizeChanged ? "true" : "false", setupMs, pct(0.5), pct(0.1),
         pct(0.9), mean, steady.empty() ? 0.0 : steady.back(), k4, k4Known, g_cap.noPresent);
  return 0;
}
