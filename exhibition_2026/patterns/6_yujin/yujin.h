#pragma once

// ===== Patternflow pattern =====
// Title:    YUJIN
// Author:   @engmung
// Date:     2026-09-19
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================
//
// Translated from patterns_js/new/yujin.js (the exhibit's final version, with
// the calm K1 startle). Knobs, absolute (PFParams::apply, lo + (hi-lo)*pos):
//   K1 FishCount   4..24  (rounded like Math.round)
//   K2 CurrentLoop 0..1   bent with loop = p * (1.5 - 0.5 p), followed in ~0.2 s
//   K3 SwimSpeed   6..24
//   K4 unused (pattern switching)
// K1 press: the fish near one point of the school slide aside and rejoin the
// current within ~1.5 s (see update()).
// Idle return (PF_EXHIBIT_REST_VARS): the four knob variables plus startleS,
// startleE, calmS, startleN. The fish's fleeR is not listed: it is only read
// while startleE > 0, and a press made with startleE == 0 overwrites it.
//
// Cost notes: the swirl weight (sqrt + exp per pixel in the JS) is a table
// built once; the background's sin/cos use PFMath's LUT (interpolated for
// the swirl rotation, see lsin) and its sigmoid a small interpolated table
// baked into flash (SIG_TAB); every fish's body profile (sin, pow and the
// ceil'd half-heights per segment) is fixed for life and built in setup().
// The firmware compiles at -Os, which keeps PFMath::fastSin/fastCos/ifloor
// out of line (fastCos even calls fastSin): ~19 windowed calls per pixel. The
// per-pixel and per-segment code therefore uses YUJIN's own forced-inline
// copies of them (fsin, fcos, ifl below), the same expressions to the bit.
// What is left per pixel is one call, PFCanvas::setPixel (GCC 8.4 -Os object
// of this header: draw() went from 24 call sites to 7).
// update() keeps libm sinf/cosf/atan2f: 24 fish, and the school's paths are
// chaotic, so a coarse LUT there would steer them visibly off the JS.

#include <Arduino.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"
#include "src/core_mem.h"   // exhibit: PFMem, for the swirl table, the fish and their body segments

// Force inlining under -Os (see the cost notes); plain inline elsewhere.
#if defined(__GNUC__)
#define YUJIN_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define YUJIN_INLINE __forceinline
#else
#define YUJIN_INLINE inline
#endif

namespace YUJIN {

const char* NAME = "YUJIN";
const char* const KNOB_LABELS[4] = {"FishCount", "CurrentLoop", "SwimSpeed", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;

// Stops 0:#00103f 0.24:#2f9be6 0.421:#b8feff 0.47:#fbff00 0.509:#ff0000 0.664:#99ff00 0.839:#f000d0 0.984:#ffdd00 1:#000000; mode oklab; wrap off.
// Baked with buildRampLUTRGBA (web/src/lib/pattern/harness.ts), alpha premultiplied.
static const uint8_t RAMP_LUT[256][3] = {
  {0,16,63},{0,18,65},{0,20,68},{0,22,70},{0,24,73},{0,26,75},{0,28,78},{0,31,80},
  {0,33,83},{0,35,85},{0,37,88},{1,39,90},{1,41,93},{1,43,96},{1,45,98},{2,47,101},
  {2,50,103},{3,52,106},{3,54,109},{4,56,111},{4,58,114},{5,60,116},{5,63,119},{6,65,122},
  {7,67,124},{8,69,127},{9,71,130},{10,74,133},{11,76,135},{12,78,138},{13,80,141},{14,83,143},
  {15,85,146},{16,87,149},{17,90,152},{18,92,154},{19,94,157},{20,96,160},{21,99,163},{22,101,166},
  {23,103,168},{24,106,171},{25,108,174},{26,111,177},{28,113,180},{29,115,183},{30,118,186},{31,120,188},
  {32,123,191},{33,125,194},{34,127,197},{35,130,200},{36,132,203},{38,135,206},{39,137,209},{40,140,212},
  {41,142,215},{42,145,218},{43,147,221},{44,149,223},{46,152,226},{47,154,229},{50,157,231},{54,159,231},
  {57,161,232},{61,163,233},{64,165,233},{68,168,234},{71,170,234},{74,172,235},{77,174,236},{80,176,236},
  {83,178,237},{86,181,238},{89,183,238},{92,185,239},{95,187,239},{98,189,240},{101,191,241},{104,193,241},
  {107,196,242},{110,198,242},{113,200,243},{116,202,243},{118,204,244},{121,206,245},{124,208,245},{127,211,246},
  {130,213,246},{133,215,247},{135,217,247},{138,219,248},{141,221,248},{144,223,249},{147,225,249},{149,228,250},
  {152,230,250},{155,232,251},{158,234,251},{161,236,251},{163,238,252},{166,240,252},{169,243,253},{172,245,253},
  {175,247,254},{177,249,254},{180,251,254},{183,253,255},{188,254,247},{193,255,235},{200,255,223},{207,255,212},
  {212,255,201},{218,255,189},{223,255,177},{227,255,165},{232,255,151},{237,255,134},{241,255,114},{246,255,83},
  {252,252,0},{255,234,53},{255,216,74},{255,199,75},{255,181,70},{255,162,62},{255,142,53},{255,121,43},
  {255,95,32},{255,62,17},{255,9,0},{254,35,0},{254,50,0},{253,61,0},{252,71,0},{251,79,0},
  {250,87,0},{250,94,0},{248,101,0},{247,108,0},{246,114,0},{245,120,0},{244,126,0},{242,131,0},
  {241,137,0},{239,142,0},{237,147,0},{235,152,0},{233,158,0},{231,163,0},{229,167,0},{227,172,0},
  {225,177,0},{222,182,0},{220,187,0},{217,191,0},{214,196,0},{211,200,0},{208,205,0},{204,210,0},
  {200,214,0},{197,218,0},{192,223,0},{188,227,0},{184,232,0},{179,236,0},{173,241,0},{168,245,0},
  {162,249,0},{155,254,0},{156,253,27},{161,249,46},{165,245,59},{169,242,69},{172,238,77},{176,235,85},
  {179,231,92},{182,227,98},{185,224,103},{188,220,109},{191,216,114},{194,212,118},{196,209,123},{199,205,127},
  {201,201,131},{204,197,135},{206,193,138},{208,189,142},{210,185,145},{212,181,149},{214,177,152},{215,173,155},
  {217,169,158},{219,164,161},{220,160,164},{222,156,166},{223,151,169},{224,146,172},{226,142,174},{227,137,177},
  {228,132,179},{229,127,181},{230,122,184},{231,116,186},{232,111,188},{233,105,190},{234,99,192},{235,92,195},
  {236,85,197},{237,77,199},{238,69,201},{238,59,202},{239,47,204},{239,31,206},{240,2,208},{241,31,206},
  {241,46,203},{242,57,201},{243,66,198},{243,74,196},{244,82,193},{245,89,191},{245,95,188},{246,101,185},
  {246,107,182},{247,112,179},{248,117,177},{248,123,173},{249,127,170},{249,132,167},{250,137,164},{250,142,160},
  {250,146,157},{251,150,153},{251,155,150},{252,159,146},{252,163,142},{252,167,137},{253,171,133},{253,175,128},
  {253,179,123},{253,183,118},{254,187,112},{254,191,106},{254,195,100},{254,199,93},{255,203,85},{255,206,76},
  {255,210,66},{255,214,53},{255,218,36},{248,215,0},{169,146,0},{96,82,0},{32,27,0},{0,0,0}
};

constexpr float PI_F = 3.14159265358979f;
constexpr float TWO_PI_F = 6.28318530717959f;
constexpr float HALF_PI_F = 1.57079632679490f;

// params.time is only read as sin/cos(t * f + c) with every f a multiple of
// 0.1, so it wraps seamlessly at 20 pi (the JS's TIME_WRAP). Keep this period.
constexpr float TIME_WRAP = 20.0f * PI_F;

constexpr float CX = 32.0f;
constexpr float CY = 64.0f;
constexpr float RX = 20.0f;
constexpr float RY = 46.0f;

constexpr int MAX_FISH = 24;
// Longest body: len 16.6 -> ceil(16.6 * 1.5) = 25 steps -> 26 segments.
constexpr int MAX_SEGS = 26;

// One body segment, fixed for the fish's life (s = 0..steps, q = s / steps).
struct Seg {
  float distBack;  // q * len
  float wagAmp;    // q^1.8 * 0.55
  float wagPh;     // q * 2.5
  float eyeOff;    // hUp * 0.55
  float forkGap;   // topY * 0.42 (read on fork segments only)
  float bellyLim;  // -botY * 0.65
  int8_t top;      // ceil(hUp)
  int8_t bot;      // ceil(hDown)
  uint8_t flags;   // SEG_TAIL | SEG_FORK | SEG_EYE
  uint8_t pad;
};
constexpr uint8_t SEG_TAIL = 1;
constexpr uint8_t SEG_FORK = 2;
constexpr uint8_t SEG_EYE = 4;  // 0.16 <= q <= 0.22

struct Fish {
  float x, y, baseY, angle;
  float speedBase, len, height, tone;
  float wag, phase, orbitRx, orbitRy;
  float fleeA;  // startle: the direction chosen at the press (rad)
  float fleeK;  // startle: how strongly this fish slides aside, 0..1 (only shrinks at a wall)
  float fleeR;  // startle: > 0 while this fish reacts to the current startle (slow turns)
  int id, type, steps;
  uint8_t idx[2][2];  // ramp index of a body pixel [tail][belly]
};

// exhibit: moved to PSRAM, compiled-in statics live in internal RAM
static float* swirlW = nullptr;  // 64 x 128 floats = 32 KB, PFMem in setup()
static Fish* fishes = nullptr;   // MAX_FISH x Fish, PFMem in setup()
static Seg* segs = nullptr;      // MAX_FISH x MAX_SEGS x Seg, PFMem in setup()

// Depth sigmoid 1 / (1 + exp(-a)) on a in [-8, 8], 1/8 steps, read with linear
// interpolation (error < 2e-4; outside the range the true value is within
// 3.4e-4 of the end entries - far under one ramp step after the 0.31 tone swing).
// SIG_TAB[i] = 1.0f / (1.0f + expf(-a)), a = -8 + i / 8, i = 0..129, in float32:
// the values setup() used to compute, baked so the table sits in flash, not
// DRAM. Each literal is the float's shortest round-trip decimal; checked bit
// for bit against the float32 expression on the host compiler and against a
// correctly rounded expf (all 130 agree).
constexpr int SIG_N = 128;
constexpr float SIG_LO = -8.0f;
constexpr float SIG_SCALE = 8.0f;  // entries per unit
static const float SIG_TAB[SIG_N + 2] = {
  0.000335350138f, 0.0003799845f, 0.000430557062f, 0.00048785712f,
  0.000552778656f, 0.000626334164f, 0.000709670421f, 0.000804085925f,
  0.000911051175f, 0.00103223103f, 0.0011695103f, 0.00132502243f,
  0.00150118221f, 0.0017007225f, 0.00192673469f, 0.0021827165f,
  0.00247262302f, 0.00280092703f, 0.0031726826f, 0.00359360268f,
  0.00407013763f, 0.00460957224f, 0.00522012589f, 0.00591106853f,
  0.00669285096f, 0.0075772414f, 0.00857748557f, 0.00970847718f,
  0.0109869428f, 0.0124316504f, 0.0140636265f, 0.0159063917f,
  0.0179862101f, 0.0203323532f, 0.0229773689f, 0.0259573571f,
  0.0293122306f, 0.0330859795f, 0.0373268872f, 0.0420877263f,
  0.0474258736f, 0.0534033291f, 0.0600866526f, 0.0675466955f,
  0.0758581758f, 0.0850990489f, 0.0953494683f, 0.106690601f,
  0.119202919f, 0.132964239f, 0.148047194f, 0.164516464f,
  0.182425529f, 0.201813221f, 0.222700134f, 0.245085016f,
  0.268941432f, 0.294214964f, 0.320821285f, 0.348645121f,
  0.377540678f, 0.407333404f, 0.437823504f, 0.468790621f,
  0.5f, 0.531209409f, 0.562176526f, 0.592666626f,
  0.622459352f, 0.651354909f, 0.679178715f, 0.705785036f,
  0.731058598f, 0.754914999f, 0.777299881f, 0.798186779f,
  0.817574441f, 0.835483551f, 0.851952732f, 0.867035747f,
  0.880797029f, 0.893309414f, 0.904650509f, 0.914900959f,
  0.924141824f, 0.932453275f, 0.939913332f, 0.946596682f,
  0.952574134f, 0.957912266f, 0.962673068f, 0.966914058f,
  0.970687747f, 0.974042594f, 0.977022648f, 0.979667664f,
  0.982013762f, 0.984093606f, 0.985936403f, 0.987568319f,
  0.989013076f, 0.990291536f, 0.991422474f, 0.99242276f,
  0.993307173f, 0.994089007f, 0.994779944f, 0.995390415f,
  0.995929897f, 0.996406376f, 0.996827304f, 0.997199059f,
  0.997527421f, 0.997817278f, 0.99807322f, 0.998299301f,
  0.998498797f, 0.998674929f, 0.998830497f, 0.998967767f,
  0.999089003f, 0.999195874f, 0.999290347f, 0.999373734f,
  0.999447167f, 0.999512196f, 0.999569476f, 0.999619961f,
  0.999664664f, 0.999704063f,
};

// Knobs (K1-K3 absolute; K4 never reaches the pattern)
static float param_fishCount = 14.0f;
static float param_currentLoop = 0.5f;
static float param_swimSpeed = 15.0f;
static float param_unused = 0.0f;

static float simTime = 0.0f;   // params.time
static float loopS = 0.0f;     // params.loopS (glides to the bent K2)
static bool loopInit = false;  // false = params.loopS === undefined
static float loopK = 0.0f;     // params.loop
static int fishCount = 14;     // params.fishCount

// K1 press (startle)
static float startleS = 0.0f;   // 1 -> 0 over 1.3 s
static float startleE = 0.0f;   // envelope, follows startleS^2 ~0.15 s behind
static float calmS = 0.0f;      // back-to-depth pull 1 -> 0 over ~6 s
static int startleN = 0;        // press count, 0..63
static float startleX = 32.0f;  // startle point (recorded, as the JS does)
static float startleY = 64.0f;

// == atan2(sin(a), cos(a)) for the finite angles used here
inline float wrapPi(float a) {
  while (a > PI_F) a -= TWO_PI_F;
  while (a < -PI_F) a += TWO_PI_F;
  return a;
}

// PFMath::ifloor, PFMath::fastSin and PFMath::fastCos (src/core_math.h),
// repeated expression for expression so the picture stays bit-identical, and
// forced inline for the per-pixel / per-segment loops in draw().
YUJIN_INLINE int ifl(float x) {
  const int i = (int)x;
  return (float)i > x ? i - 1 : i;
}
YUJIN_INLINE float fsin(float x) {
  return PFMath::sinLUT[(int)(x * PFMath::ANGLE_TO_LUT) & (PFMath::SIN_LUT_SIZE - 1)];
}
YUJIN_INLINE float fcos(float x) { return fsin(x + (float)M_PI_2); }

YUJIN_INLINE float sigmoid(float a) {
  const float p = (a - SIG_LO) * SIG_SCALE;
  if (p <= 0.0f) return SIG_TAB[0];
  if (p >= (float)SIG_N) return SIG_TAB[SIG_N];
  const int i = (int)p;
  const float fr = p - (float)i;
  return SIG_TAB[i] + (SIG_TAB[i + 1] - SIG_TAB[i]) * fr;
}

// Distance from (x, y) along (ux, uy) to the frame edge (on-screen points only).
inline float rayToWall(float x, float y, float ux, float uy) {
  float hit = 1000.0f;
  if (ux < -0.02f) hit = x / -ux;
  else if (ux > 0.02f) hit = (63.0f - x) / ux;
  if (uy < -0.02f) {
    const float h = y / -uy;
    if (h < hit) hit = h;
  } else if (uy > 0.02f) {
    const float h = (127.0f - y) / uy;
    if (h < hit) hit = h;
  }
  return hit;
}

// sin/cos of the swirl rotation: PFMath's 1024-entry table read with linear
// interpolation (within 5e-6). The rotation multiplies the error by the radius
// (up to 32 px, x2.3 vertically), so the plain nearest-bucket fastSin (up to
// 0.006 off) moved the striation rows' floor() on ~0.7% of the pixels. The
// other background terms keep fastSin: their error stays far below a pixel.
YUJIN_INLINE float lsin(float x) {
  const float p = x * PFMath::ANGLE_TO_LUT;
  const int i = ifl(p);
  const float fr = p - (float)i;
  const float a = PFMath::sinLUT[i & (PFMath::SIN_LUT_SIZE - 1)];
  const float b = PFMath::sinLUT[(i + 1) & (PFMath::SIN_LUT_SIZE - 1)];
  return a + (b - a) * fr;
}
YUJIN_INLINE float lcos(float x) { return lsin(x + HALF_PI_F); }

inline bool onScreen(const Fish& g) {
  return g.x >= 0.0f && g.x < 64.0f && g.y >= 0.0f && g.y < 128.0f;
}

inline uint8_t rampIndex(float v) {
  v = PFMath::clamp(v, 0.64f, 1.0f);
  return (uint8_t)(int)(v * 255.0f + 0.5f);
}

void setup() {
  PFMath::buildSinLUT();

  // exhibit: moved to PSRAM, compiled-in statics live in internal RAM
  if (!swirlW) swirlW = PFMem::allocFloats(FRAME_W * FRAME_H);
  if (!fishes) fishes = (Fish*)PFMem::alloc(sizeof(Fish) * MAX_FISH);
  if (!segs) segs = (Seg*)PFMem::alloc(sizeof(Seg) * MAX_FISH * MAX_SEGS);
  if (!swirlW || !fishes || !segs) return;

  simTime = 0.0f;
  loopInit = false;
  startleS = 0.0f;
  startleE = 0.0f;
  calmS = 0.0f;
  startleN = 0;
  startleX = 32.0f;
  startleY = 64.0f;

  // The swirl weight depends on the pixel only (rot = k * swirlW[i]):
  // the band around the ellipse plus the radial term.
  for (int y = 0; y < FRAME_H; y++) {
    for (int x = 0; x < FRAME_W; x++) {
      const float dx = (float)x - CX;
      const float dy = ((float)y - CY) * (RX / RY);
      const float r = sqrtf(dx * dx + dy * dy);
      const float u = (r - RX) / 12.0f;
      const float band = expf(-(u * u));
      const float rn = r / 42.0f;
      swirlW[y * FRAME_W + x] = 1.75f * band + 0.3f * (1.0f - (rn < 1.0f ? rn : 1.0f));
    }
  }

  const float tones[4] = {0.98f, 0.88f, 0.78f, 0.65f};
  for (int i = 0; i < MAX_FISH; i++) {
    Fish& f = fishes[i];
    const int type = i % 4;
    float len = 11.0f;
    float height = 3.2f;
    if (type == 0) {  // large diamond body
      len = 13.0f + (float)(i % 3) * 1.8f;
      height = 4.3f + (float)(i % 2) * 0.6f;
    } else if (type == 1) {  // V fork tail
      len = 11.0f + (float)(i % 2) * 2.0f;
      height = 3.3f;
    } else if (type == 2) {  // small round minnow
      len = 7.2f + (float)(i % 3) * 1.2f;
      height = 2.4f + (float)(i % 2) * 0.5f;
    } else {  // slim chub
      len = 13.5f + (float)(i % 2) * 2.0f;
      height = 2.5f;
    }
    // most of the school near the bottom (y 92..124)
    const float depthBias = powf((float)i / 23.0f, 2.6f);
    const float bottomY = 122.0f - depthBias * 64.0f;

    f.id = i;
    f.type = type;
    f.x = -18.0f + PFMath::jsMod((float)i * 19.3f, 98.0f);
    f.y = bottomY;
    f.baseY = bottomY;
    f.angle = PI_F;
    f.speedBase = 12.0f + PFMath::jsMod((float)i * 3.7f, 8.5f);
    f.len = len;
    f.height = height;
    f.tone = tones[i % 4];
    f.wag = (float)i * 1.5f;
    f.phase = (float)i * 2.2f;
    f.orbitRx = 19.0f + PFMath::jsMod((float)i * 4.3f, 7.0f);
    f.orbitRy = 44.0f + PFMath::jsMod((float)i * 5.7f, 8.0f);
    f.fleeA = 0.0f;
    f.fleeK = 0.0f;
    f.fleeR = 0.0f;
    for (int tail = 0; tail < 2; tail++) {
      const float v = tail ? f.tone * 0.90f : f.tone;
      f.idx[tail][0] = rampIndex(v);
      f.idx[tail][1] = rampIndex(v - 0.04f);
    }

    // The body profile: none of it moves but the tail wave, so every
    // segment's half-heights, flags and offsets are built here once.
    int steps = (int)ceilf(len * 1.5f);
    if (steps > MAX_SEGS - 1) steps = MAX_SEGS - 1;
    f.steps = steps;
    Seg* sg = segs + i * MAX_SEGS;
    for (int s = 0; s <= steps; s++) {
      const float q = (float)s / (float)steps;
      float hUp = 0.0f;
      float hDown = 0.0f;
      uint8_t flags = 0;
      if (type == 0) {
        if (q < 0.72f) {
          const float prof = sinf((q / 0.72f) * PI_F);
          hUp = prof * (height * 0.58f);
          hDown = prof * (height * 0.42f);
        } else {
          const float tq = (q - 0.72f) / 0.28f;
          hUp = (0.18f + tq * 0.26f) * height;
          hDown = hUp;
          flags |= SEG_TAIL;
        }
      } else if (type == 1) {
        if (q < 0.70f) {
          const float prof = sinf((q / 0.70f) * PI_F);
          hUp = prof * (height * 0.48f);
          hDown = prof * (height * 0.52f);
        } else {
          const float tq = (q - 0.70f) / 0.30f;
          hUp = (0.16f + tq * 0.42f) * height;
          hDown = hUp;
          flags |= SEG_TAIL;
          if (tq > 0.42f) flags |= SEG_FORK;
        }
      } else if (type == 2) {
        if (q < 0.76f) {
          const float prof = sinf((q / 0.76f) * PI_F);
          hUp = prof * (height * 0.38f);
          hDown = prof * (height * 0.62f);
        } else {
          const float tq = (q - 0.76f) / 0.24f;
          const float leaf = sinf(tq * PI_F) * 0.32f + 0.14f;
          hUp = leaf * height;
          hDown = hUp;
          flags |= SEG_TAIL;
        }
      } else {
        if (q < 0.80f) {
          const float prof = sinf((q / 0.80f) * PI_F * 0.95f);
          hUp = prof * (height * 0.48f);
          hDown = prof * (height * 0.52f);
        } else {
          const float tq = (q - 0.80f) / 0.20f;
          float taper = 1.0f - tq * 0.85f;
          if (taper < 0.12f) taper = 0.12f;
          hUp = taper * 0.35f * height;
          hDown = hUp;
          flags |= SEG_TAIL;
        }
      }
      if (q >= 0.16f && q <= 0.22f) flags |= SEG_EYE;
      const int topY = (int)ceilf(hUp);
      const int botY = (int)ceilf(hDown);
      sg[s].distBack = q * len;
      sg[s].wagAmp = powf(q, 1.8f) * 0.55f;
      sg[s].wagPh = q * 2.5f;
      sg[s].eyeOff = hUp * 0.55f;
      sg[s].forkGap = (float)topY * 0.42f;
      sg[s].bellyLim = -(float)botY * 0.65f;
      sg[s].top = (int8_t)topY;
      sg[s].bot = (int8_t)botY;
      sg[s].flags = flags;
      sg[s].pad = 0;
    }
  }
}

void update(float dt, const InputFrame& input) {
  if (!swirlW || !fishes || !segs) return;

  PFParams::apply(input, 0, &param_fishCount, 4.0f, 24.0f, 0.625f);
  PFParams::apply(input, 1, &param_currentLoop, 0.0f, 1.0f, 0.03125f);
  PFParams::apply(input, 2, &param_swimSpeed, 6.0f, 24.0f, 0.5625f);
  PFParams::apply(input, 3, &param_unused, 0.0f, 1.0f, 0.03125f);

  const float speedKnob = param_swimSpeed;
  float dtc = dt < 0.05f ? dt : 0.05f;  // Math.min(dt, 0.05)
  if (dtc < 0.0f) dtc = 0.0f;
  const float simDt = dtc * (speedKnob * 0.15f);
  simTime += simDt;
  if (simTime >= TIME_WRAP) simTime -= TIME_WRAP;

  const int fc = PFMath::ifloor(param_fishCount + 0.5f);  // Math.round
  fishCount = fc < 4 ? 4 : (fc > 24 ? 24 : fc);

  // K2 bent so its middle sits at 0.625, followed in ~0.2 s
  const float p = PFMath::clamp01(param_currentLoop);
  const float loopTarget = p * (1.5f - 0.5f * p);
  if (!loopInit) {
    loopS = loopTarget;
    loopInit = true;
  }
  const float follow = dt * 6.0f;
  loopS += (loopTarget - loopS) * (follow < 1.0f ? follow : 1.0f);
  loopK = loopS;

  const float k = loopK;
  const float t = simTime;

  // K1 press: startle, e rises from this frame on
  if (input.btnPressed[0]) {
    const int n = startleN;
    // Startle point: 6 px ahead of the nose of the fish nearest to (on-screen
    // school centre + a golden-ratio offset per press, x +-12, y +-16).
    float gx = 0.0f;
    float gy = 0.0f;
    int gn = 0;
    for (int i = 0; i < fishCount; i++) {
      const Fish& g = fishes[i];
      if (onScreen(g)) {
        gx += g.x;
        gy += g.y;
        gn++;
      }
    }
    if (gn > 0) {
      gx /= (float)gn;
      gy /= (float)gn;
    } else {
      gx = CX;
      gy = CY;
    }
    const float u = 0.37f + (float)n * 0.6180339887f;
    const float v = 0.71f + (float)n * 0.7548776662f;
    float qx = gx + (PFMath::fract(u) - 0.5f) * 24.0f;
    float qy = gy + (PFMath::fract(v) - 0.5f) * 32.0f;
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < fishCount; i++) {
      const Fish& g = fishes[i];
      if (onScreen(g)) {
        const float d2 = (g.x - qx) * (g.x - qx) + (g.y - qy) * (g.y - qy);
        if (d2 < bestD) {
          bestD = d2;
          best = i;
        }
      }
    }
    if (best >= 0) {
      const Fish& g = fishes[best];
      qx = g.x + cosf(g.angle) * 6.0f;
      qy = g.y + sinf(g.angle) * 6.0f;
    }
    startleX = qx;
    startleY = qy;
    // Each fish's direction and strength are fixed at the press: full within
    // 12 px of the point, smoothstep to 0 at 28 px; off-screen fish get 0.
    const float prevE = startleE;
    for (int i = 0; i < MAX_FISH; i++) {
      Fish& g = fishes[i];
      if (i < fishCount && onScreen(g)) {
        const float ax = g.x - qx;
        const float ay = g.y - qy;
        const float aLen = sqrtf(ax * ax + ay * ay + 1.0f);
        float ux = ax / aLen;
        float uy = ay / aLen;
        // A wall within 20 px along the way: drop the component into the wall
        // and run along it, angled inward (head-on: the way it was swimming).
        float hit = 1000.0f;
        float wnx = 0.0f;  // outward normal of the wall ahead
        float wny = 0.0f;
        if (ux < -0.02f) {
          hit = g.x / -ux;
          wnx = -1.0f;
        } else if (ux > 0.02f) {
          hit = (63.0f - g.x) / ux;
          wnx = 1.0f;
        }
        if (uy < -0.02f && g.y / -uy < hit) {
          hit = g.y / -uy;
          wnx = 0.0f;
          wny = -1.0f;
        } else if (uy > 0.02f && (127.0f - g.y) / uy < hit) {
          hit = (127.0f - g.y) / uy;
          wnx = 0.0f;
          wny = 1.0f;
        }
        if (hit < 20.0f) {
          const float un = ux * wnx + uy * wny;
          float tx = ux - un * wnx;
          float ty = uy - un * wny;
          float tl = sqrtf(tx * tx + ty * ty);
          if (tl < 0.3f) {
            const float along = -cosf(g.angle) * wny + sinf(g.angle) * wnx;
            tx = along >= 0.0f ? -wny : wny;
            ty = along >= 0.0f ? wnx : -wnx;
            tl = 1.0f;
          }
          ux = tx / tl - 0.35f * wnx;
          uy = ty / tl - 0.35f * wny;
        }
        g.fleeA = atan2f(uy, ux);
        // Fold into 90 degrees of the heading (behind -> the same side; nearly
        // straight behind -> the side facing the frame centre): no zigzag, no
        // turning back.
        const float rel = wrapPi(g.fleeA - g.angle);
        if (fabsf(rel) > HALF_PI_F) {
          float side = rel >= 0.0f ? 1.0f : -1.0f;
          if (fabsf(sinf(rel)) < 0.25f) {
            side = cosf(g.angle) * (CY - g.y) - sinf(g.angle) * (CX - g.x) >= 0.0f ? 1.0f : -1.0f;
          }
          g.fleeA = g.angle + side * HALF_PI_F;
        }
        const float kk = PFMath::clamp01((28.0f - aLen) / 16.0f);
        g.fleeK = kk * kk * (3.0f - 2.0f * kk);
      } else {
        g.fleeK = 0.0f;
      }
      // A fish still reacting to an earlier startle keeps turning slowly
      // until the envelope ends (Math.max(fleeK, e > 0 ? fleeR : 0)).
      const float keep = prevE > 0.0f ? g.fleeR : 0.0f;
      g.fleeR = g.fleeK > keep ? g.fleeK : keep;
    }
    startleN = (n + 1) % 64;
    startleS = 1.0f;
    calmS = 1.0f;
  }
  const float sS = startleS;
  // Envelope: follows startleS^2 about 0.15 s behind (peak ~0.7 at ~0.2 s); a
  // new press rises from the current height; snapped to exactly 0 at rest.
  const float ef = dtc * 7.0f;
  float e = startleE + (sS * sS - startleE) * (ef < 1.0f ? ef : 1.0f);
  if (sS == 0.0f && e < 0.002f) e = 0.0f;
  startleE = e;
  const float sNext = sS - dtc / 1.3f;
  startleS = sNext > 0.0f ? sNext : 0.0f;
  const float calm = calmS;
  const float cNext = calm - dtc / 6.0f;
  calmS = cNext > 0.0f ? cNext : 0.0f;
  // Speed-up share: at most 0.5 (1.5x), less above SwimSpeed 15
  const float sr = 15.0f / speedKnob;
  const float boostMax = 0.5f * (sr < 1.0f ? sr : 1.0f);

  for (int i = 0; i < fishCount; i++) {
    Fish& f = fishes[i];
    float targetAngle = PI_F;

    if (k > 0.01f) {
      // follow an elliptic current orbit
      const float nx = (f.x - CX) / f.orbitRx;
      const float ny = (f.y - CY) / f.orbitRy;
      const float dist = sqrtf(nx * nx + ny * ny);
      const float phi = atan2f(ny, nx);
      const float tx = -sinf(phi) * f.orbitRx;
      const float ty = cosf(phi) * f.orbitRy;
      const float trackErr = 1.0f - dist;
      const float attractX = nx * f.orbitRx * trackErr * 1.35f;
      const float attractY = ny * f.orbitRy * trackErr * 1.35f;
      const float loopAngle = atan2f(ty + attractY, tx + attractX);
      const float cosB = -(1.0f - k) + cosf(loopAngle) * k;  // cos(PI) = -1
      const float sinB = sinf(loopAngle) * k;                // sin(PI) = 0
      targetAngle = atan2f(sinB, cosB);
    } else {
      targetAngle += sinf(t * 1.2f + f.phase) * 0.05f;
    }

    // After a startle, in the level cruise (k < 0.15): back to its own depth
    // beyond 6 px (nothing when calm is 0)
    if (calm > 0.0f && k < 0.15f) {
      const float dev = f.y - f.baseY;
      const float excess = dev > 6.0f ? dev - 6.0f : (dev < -6.0f ? dev + 6.0f : 0.0f);
      targetAngle += PFMath::clamp(excess * 0.04f, -0.7f, 0.7f) * calm;
    }

    // Startle: turn the target toward fleeA, the wrapped difference clipped
    // to +-1.6 rad (slides aside, never whips round)
    const bool on = onScreen(f);
    const float curA = targetAngle;  // the current's direction before the startle
    const bool startled = e > 0.0f && f.fleeR > 0.0f;
    float boost = 0.0f;
    if (e > 0.0f && f.fleeK > 0.0f) {
      if (on) {
        // wall distance along the flight: fades out from 24 px to 6 px, never back
        const float hit = rayToWall(f.x, f.y, cosf(f.fleeA), sinf(f.fleeA));
        const float lim = PFMath::clamp01((hit - 6.0f) / 18.0f);
        if (lim < f.fleeK) f.fleeK = lim;
      }
      float dF = wrapPi(f.fleeA - targetAngle);
      dF = PFMath::clamp(dF, -1.6f, 1.6f);
      targetAngle += e * f.fleeK * dF;
      boost = boostMax * e * f.fleeK;
    }

    // Startled in the big swirl (k >= 0.15): pushed inward within 10 px of an
    // edge, except where the current itself flows out through it
    if (startled && k >= 0.15f && on) {
      const float nx = f.x < 10.0f ? (10.0f - f.x) / 10.0f : (f.x > 53.0f ? (53.0f - f.x) / 10.0f : 0.0f);
      const float ny = f.y < 10.0f ? (10.0f - f.y) / 10.0f : (f.y > 117.0f ? (117.0f - f.y) / 10.0f : 0.0f);
      if (nx != 0.0f || ny != 0.0f) {
        const float nl = sqrtf(nx * nx + ny * ny);
        const float inward = (cosf(curA) * nx + sinf(curA) * ny) / nl;
        const float w = e * (nl < 1.0f ? nl : 1.0f) * PFMath::clamp01(1.0f + 2.5f * inward);
        const float inA = atan2f(ny, nx);
        targetAngle += w * wrapPi(inA - targetAngle);
      }
    }

    const float diff = wrapPi(targetAngle - f.angle);
    const float ts = simDt * 3.6f;
    float turn = diff * (ts < 1.0f ? ts : 1.0f);
    if (startled) {
      // slow, wide turns while sliding aside: at most simDt * (2 + 4(1 - e)) rad
      const float maxTurn = simDt * (2.0f + 4.0f * (1.0f - e));
      turn = PFMath::clamp(turn, -maxTurn, maxTurn);
    }
    f.angle += turn;
    // f.angle is only read through cos/sin/atan2: this +-pi, 2 pi wrap is exact.
    if (f.angle > PI_F) f.angle -= TWO_PI_F;
    else if (f.angle < -PI_F) f.angle += TWO_PI_F;

    const float hx = cosf(f.angle);
    const float hy = sinf(f.angle);
    if (boost > 0.0f && on) {
      // heading within 14 px of a wall: less speed-up, none at 2 px
      boost *= PFMath::clamp01((rayToWall(f.x, f.y, hx, hy) - 2.0f) / 12.0f);
    }
    const float currentSpeed = f.speedBase * (1.0f + k * 0.18f) * (1.0f + boost);
    f.x += hx * currentSpeed * simDt;
    f.y += hy * currentSpeed * simDt;

    // out fully (-24), back in from beyond the far side (88)
    if (k < 0.15f) {
      if (f.x < -24.0f) {
        f.x = 88.0f;
        f.y = f.baseY + sinf(t + (float)f.id) * 3.5f;
        f.angle = PI_F;
      }
    } else {
      if (f.x < -28.0f) f.x = 90.0f;
      if (f.x > 90.0f) f.x = -28.0f;
      if (f.y < -28.0f) f.y = 154.0f;
      if (f.y > 154.0f) f.y = -28.0f;
    }

    f.wag += simDt * (2.8f + currentSpeed * 0.08f);
    // f.wag is only read as sin(f.wag - ...): keep the 2 pi period.
    if (f.wag >= TWO_PI_F) f.wag -= TWO_PI_F;
  }
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);
  if (!swirlW || !fishes || !segs) {
    PFCanvas::present();
    return;
  }

  const float t = simTime;
  const float k = loopK;
  const bool swirl = k > 0.01f;

  // per-frame time terms
  const float tA = t * 1.2f;
  const float tB = t * 1.4f;
  const float tG = t * 0.8f;
  const float tH = t * 2.2f;
  const float tF = t * 1.3f;
  const float tK = t * 0.6f;
  const float tS = t * 1.1f;

  // 1. Oil-paint background: layered domain warp, a clear depth split near y = 48
  for (int y = 0; y < FRAME_H; y++) {
    const float fy = (float)y;
    const float dy = (fy - CY) * (RX / RY);
    const float* wRow = swirlW + y * FRAME_W;
    for (int x = 0; x < FRAME_W; x++) {
      float x0 = (float)x;
      float y0 = fy;
      if (swirl) {
        // K2: the deep water and the horizontal strokes roll round with the current
        const float dx = x0 - CX;
        const float rot = k * wRow[x];
        const float cosR = lcos(rot);   // cos(-rot)
        const float sinR = -lsin(rot);  // sin(-rot)
        x0 = CX + (dx * cosR - dy * sinR);
        y0 = CY + (dx * sinR + dy * cosR) * (RY / RX);
      }

      // two-level domain warp
      const float warpA = fsin(y0 * 0.16f + tA + fcos(x0 * 0.12f) * 1.6f) * 2.2f;
      const float warpB = fcos(x0 * 0.22f - y0 * 0.07f - tB) * 1.5f;
      const float microGrain = fsin(x0 * 0.45f + y0 * 0.3f - tG) * 0.7f;
      const float wx = x0 + warpA + microGrain;
      const float wy = y0 + warpB + warpA * 0.4f;

      // horizontal impasto striations
      const float streakRow = (float)ifl(wy * (1.0f / 2.8f));
      const float horizFlow = fsin(wx * 0.14f - tH + streakRow * 1.9f) * 0.035f;
      const float horizFine = fcos(wx * 0.28f - tF + wy * 0.08f) * 0.02f;
      const float kc = fabsf(fsin(wx * 0.08f + wy * 0.15f - tK));
      const float knifeChisel = kc * kc * kc * 0.025f;

      // depth shelf around y = 48
      const float shelfY = 48.0f + fsin(wx * 0.11f - tS) * 3.6f + fcos(wx * 0.26f) * 1.6f;
      const float depthMask = sigmoid((wy - shelfY) * 0.85f);

      // exhibit: the ramp's low end is a new navy -> bright blue (fish tones untouched) and the water sits lower on it
      // (was 0.40 / 0.09, clamp 0.06..0.44: pale water hid the yellow and lime fish, and dark blues merged on the LED)
      const float shallowTone = 0.24f + horizFlow + horizFine + knifeChisel;
      const float abyssTone = 0.01f + horizFlow * 0.5f + horizFine * 0.4f;
      float bgVal = shallowTone * (1.0f - depthMask) + abyssTone * depthMask;
      bgVal = PFMath::clamp(bgVal, 0.0f, 0.32f);
      const int li = (int)(bgVal * 255.0f + 0.5f);
      PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
    }
  }

  // 2. Fish: four body types (0.64 .. 1.00), drawn over the background
  const int count = fishCount;
  for (int i = 0; i < count; i++) {
    const Fish& f = fishes[i];
    const float fx = f.x;
    const float fy = f.y;
    // no pixel of the fish lies farther than len + height + 2 from its head
    const float reach = f.len + f.height + 2.0f;
    if (fx < -reach || fx > 63.0f + reach || fy < -reach || fy > 127.0f + reach) continue;

    const float dirX = cosf(f.angle);
    const float dirY = sinf(f.angle);
    const float normX = -dirY;
    const float normY = dirX;
    const Seg* sg = segs + i * MAX_SEGS;
    bool eyeDrawn = false;

    for (int s = 0; s <= f.steps; s++) {
      const Seg& g = sg[s];
      // gliding tail wave
      const float wag = fsin(f.wag - g.wagPh) * g.wagAmp;
      const float pxCenter = fx - dirX * g.distBack + normX * wag;
      const float pyCenter = fy - dirY * g.distBack + normY * wag;
      const bool fork = (g.flags & SEG_FORK) != 0;
      const uint8_t* idx = f.idx[(g.flags & SEG_TAIL) ? 1 : 0];

      for (int d = -g.bot; d <= g.top; d++) {
        const float fd = (float)d;
        if (fork && fabsf(fd) < g.forkGap) continue;
        const int px = ifl(pxCenter + normX * fd);
        const int py = ifl(pyCenter + normY * fd);
        if (px >= 0 && px < FRAME_W && py >= 0 && py < FRAME_H) {
          const uint8_t li = idx[fd < g.bellyLim ? 1 : 0];
          PFCanvas::setPixel(px, py, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
        }
      }

      // a single eye on the upper side
      if (!eyeDrawn && (g.flags & SEG_EYE)) {
        const int ex = ifl(pxCenter + normX * g.eyeOff);
        const int ey = ifl(pyCenter + normY * g.eyeOff);
        if (ex >= 0 && ex < FRAME_W && ey >= 0 && ey < FRAME_H) {
          PFCanvas::setPixel(ex, ey, RAMP_LUT[255][0], RAMP_LUT[255][1], RAMP_LUT[255][2]);
          eyeDrawn = true;
        }
      }
    }
  }

  PFCanvas::present();
}

} // namespace YUJIN

#undef YUJIN_INLINE

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
