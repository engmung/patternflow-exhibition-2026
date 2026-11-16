#pragma once

// ===== Patternflow pattern =====
// Title:    BAKWAN
// Author:   @engmung
// Date:     2026-09-19
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================
//
// Translated from patterns_js/new/bakwan.js (the exhibit file).
//
// Knob 1: Foliage 0.25..1     (canopy fullness, carpet coverage, falling leaves)
// Knob 2: Wind    -0.75..3.25 (direction and strength; centre +1.25)
// Knob 3: Speed   0.35..1.65  (tempo of sway, flutter and falling leaves)
// Knob 4: unused
// K1 press: the next tree from TREE_SEEDS (cycling), and a ~2.6 s leaf shower
//           from the new canopy. K2/K3 presses: unused.
// Idle return puts back treeIndex = 0 and showerT = SHOWER_DUR; update()
// rebuilds the first tree because treeIndex != builtIndex.
//
// How the frame is made (same picture as the JS, less work per frame):
//  - Every layer writes a ramp INDEX (round(value * 255), as the lab's
//    setValue does) into a 64x128 byte buffer, in the JS draw order; one pass
//    at the end turns indices into colours.
//  - The sky, the leaf carpet and the roots do not move. They are baked into
//    a background buffer, rebuilt only when Foliage (carpet coverage) or the
//    tree changes, and copied in each frame. The carpet's three sines per
//    pixel run once, at boot, with exact sinf.
//  - getTreeSway(y) = (1.2 W + 0.45 sin(1.8 t - 0.03 y) max(|W|, 0.8)) hr^1.7
//    = A P(y) + B (sin(1.8 t) Pc(y) - cos(1.8 t) Ps(y)), with P = hr^1.7,
//    Pc = P cos(0.03 y), Ps = P sin(0.03 y) read from a 1-px table (linear
//    interpolation, < 0.001 px off), so a frame needs sin/cos(1.8 t) once and
//    no pow or sine per bough sample or leaf. Every "sin(k t + per-leaf
//    phase)" is done the same way, with the phase's sin/cos stored per leaf.
//  - All tree storage is one PFMem block allocated in setup() at the true
//    maximum sizes; a K1 press rebuilds the tree in place (no allocation).

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"
#include "src/core_mem.h"

namespace BAKWAN {

const char* NAME = "BAKWAN";
const char* const KNOB_LABELS[4] = {"Foliage", "Wind", "Speed", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;   // @matrix 64x128
constexpr int FRAME_H = 128;
constexpr int HORIZON_Y = 82;
constexpr int GROUND_ROWS = FRAME_H - HORIZON_Y;   // 46

// @ramp linear 0.046:#748a9a 1:#614700 0.105:#0095c7 0.141:#34180e 0.304:#a35914 0.5:#401212 0.52:#000000 0.833:#f0a400
// Generated with buildRampLUTRGBA (web/src/lib/pattern/harness.ts); alpha is 255 everywhere.
static const uint8_t RAMP_LUT[256][3] = {
  {116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},
  {116,138,154},{116,138,154},{116,138,154},{116,138,154},{114,138,155},{106,139,158},{98,140,161},{91,140,164},
  {83,141,167},{75,142,170},{68,143,173},{60,143,176},{52,144,179},{45,145,182},{37,146,185},{29,146,188},
  {21,147,191},{14,148,194},{6,148,197},{1,146,194},{7,132,174},{13,119,154},{18,105,134},{24,91,114},
  {30,78,94},{35,64,74},{41,51,53},{47,37,33},{52,24,14},{55,26,14},{57,27,14},{60,29,14},
  {63,30,15},{65,32,15},{68,33,15},{71,35,15},{73,37,15},{76,38,15},{79,40,15},{81,41,16},
  {84,43,16},{87,44,16},{90,46,16},{92,48,16},{95,49,16},{98,51,16},{100,52,17},{103,54,17},
  {106,55,17},{108,57,17},{111,58,17},{114,60,17},{116,62,17},{119,63,18},{122,65,18},{124,66,18},
  {127,68,18},{130,69,18},{132,71,18},{135,73,18},{138,74,19},{140,76,19},{143,77,19},{146,79,19},
  {148,80,19},{151,82,19},{154,83,19},{156,85,20},{159,87,20},{162,88,20},{162,88,20},{160,87,20},
  {158,85,20},{156,84,20},{154,83,20},{152,81,20},{150,80,20},{148,78,20},{146,77,20},{144,76,20},
  {142,74,20},{140,73,20},{138,71,20},{136,70,19},{134,68,19},{132,67,19},{130,66,19},{128,64,19},
  {126,63,19},{124,61,19},{122,60,19},{120,58,19},{118,57,19},{116,56,19},{115,54,19},{113,53,19},
  {111,51,19},{109,50,19},{107,49,19},{105,47,19},{103,46,19},{101,44,19},{99,43,19},{97,41,19},
  {95,40,19},{93,39,19},{91,37,19},{89,36,19},{87,34,18},{85,33,18},{83,31,18},{81,30,18},
  {79,29,18},{77,27,18},{75,26,18},{73,24,18},{71,23,18},{69,22,18},{67,20,18},{65,19,18},
  {58,16,16},{45,13,13},{33,9,9},{20,6,6},{8,2,2},{1,1,0},{4,3,0},{7,5,0},
  {10,7,0},{13,9,0},{16,11,0},{19,13,0},{22,15,0},{25,17,0},{28,19,0},{31,21,0},
  {34,23,0},{37,25,0},{40,28,0},{43,30,0},{46,32,0},{49,34,0},{52,36,0},{55,38,0},
  {58,40,0},{61,42,0},{64,44,0},{67,46,0},{70,48,0},{73,50,0},{76,52,0},{79,54,0},
  {82,56,0},{85,58,0},{88,60,0},{91,62,0},{94,65,0},{97,67,0},{100,69,0},{103,71,0},
  {106,73,0},{109,75,0},{112,77,0},{115,79,0},{118,81,0},{121,83,0},{124,85,0},{127,87,0},
  {131,89,0},{134,91,0},{137,93,0},{140,95,0},{143,97,0},{146,99,0},{149,102,0},{152,104,0},
  {155,106,0},{158,108,0},{161,110,0},{164,112,0},{167,114,0},{170,116,0},{173,118,0},{176,120,0},
  {179,122,0},{182,124,0},{185,126,0},{188,128,0},{191,130,0},{194,132,0},{197,134,0},{200,136,0},
  {203,138,0},{206,141,0},{209,143,0},{212,145,0},{215,147,0},{218,149,0},{221,151,0},{224,153,0},
  {227,155,0},{230,157,0},{233,159,0},{236,161,0},{239,163,0},{238,163,0},{235,161,0},{231,158,0},
  {228,156,0},{225,154,0},{221,152,0},{218,150,0},{215,147,0},{211,145,0},{208,143,0},{204,141,0},
  {201,139,0},{198,137,0},{194,134,0},{191,132,0},{188,130,0},{184,128,0},{181,126,0},{178,123,0},
  {174,121,0},{171,119,0},{168,117,0},{164,115,0},{161,112,0},{157,110,0},{154,108,0},{151,106,0},
  {147,104,0},{144,102,0},{141,99,0},{137,97,0},{134,95,0},{131,93,0},{127,91,0},{124,88,0},
  {121,86,0},{117,84,0},{114,82,0},{110,80,0},{107,78,0},{104,75,0},{100,73,0},{97,71,0}
};

// Curated tree seeds: 3831 tall crown (author) / 102 low round / 137 slender spire /
// 7 low and broad / 95 tall twin crown / 33 even trio / 43 big and full
static const int TREE_SEEDS[7] = {3831, 102, 137, 7, 95, 33, 43};
constexpr int TREE_COUNT = 7;

constexpr int SHOWER_COUNT = 48;
constexpr float SHOWER_DUR = 2.6f;

// Every use of time is sin/cos(t * f + c) with f in {1.8, 2.5, 3.0, 3.5, 3.8, 4.0}: 20*PI is an exact period.
// C++ 번역 시: 이 주기(20π)를 그대로 유지
constexpr float TIME_WRAP = 62.83185307179586f;

// ── Capacities: the true maxima ─────────────────────────────────────────
// Theoretical bounds of buildTree (the 7 curated seeds reach: boughs 57,
// roots 9, branch leaves 280, root leaves 121, trunk rows 64):
//  - boughs: 19 primary (2 wings + 2 bridges + 2 mid + 4 crown forks + 9 side
//    shoots) + at most 2 twigs each = 57
//  - roots: 3 trunks x at most 3 = 9
//  - root leaves: 8 + floor(rad * 2) per root, rad <= 3.7 -> 15 x 9 = 135
//  - trunk rows: floor(y0 - y1) + 1, height <= 64 -> 65
//  - root pixels: rows 94..125 (groundY >= 98, rad <= 3.7, length <= 20)
constexpr int MAX_BOUGHS = 57;
constexpr int MAX_ROOTS = 9;
constexpr int MAX_BRANCH_LEAVES = 280;
constexpr int MAX_ROOT_LEAVES = 135;
constexpr int MAX_TRUNK_ROWS = 65;
constexpr int NUM_DRIFTERS = 60;
constexpr int NUM_NEIGHBORS = 40;
constexpr int B_STEPS = 34;           // bough samples 0..34
constexpr float INV_MIN_RAD = 1.0f / 0.7f;   // bough radius floor 0.7
constexpr int R_STEPS = 24;           // root samples 0..24
constexpr int ROOT_Y0 = 80;           // root overlay covers rows 80..127
constexpr int ROOT_ROWS = FRAME_H - ROOT_Y0;

struct Trunk {
  float x0, y0, x1, y1, wBase, wTop, seed, sc;
};

// One trunk row, fixed per tree. cx = baseCx + sway, sway from swP/swPc/swPs (x 0.75 sc).
struct TrunkRow {
  float baseCx, halfW, invDiam, cy24, sA, cA;   // sA/cA = sin/cos(cy * 0.18)
  float swP, swPc, swPs;
  int iy;
};

struct Bough {
  float p0x, p0y, p1x, p1y, p2x, p2y, rad, seed, invRad;
};

// Sway table: P, Pc, Ps at integer y from SW_Y0 to SW_Y0 + SW_N - 1 (outside: clamped).
// Every y the pattern asks for lies in about -2..106 (top leaf jitter .. trunk bases).
constexpr int SW_Y0 = -64;
constexpr int SW_N = 193;   // y -64..128

struct Root {
  float p0x, p0y, p1x, p1y, p2x, p2y, rad;
};

struct Leaf {
  float x, y, size, tone;
  float sa, ca;        // sin/cos(seed * 2): the phase of flutter, flicker and shake
  float P, Pc, Ps;     // sway terms at leaf y
};

struct Drifter {
  float originX, originY, fallDist, invCycle, prog, wobble;
  float s25, c25, s30, c30;   // sin/cos(seed * 2.5), sin/cos(seed * 3)
};

struct Neighbor {
  float startY, fallDist, invCycle, prog, seed;
  float s20, c20, sSd, cSd;   // sin/cos(seed * 2), sin/cos(seed)
};

struct Shower {
  float x, y, delay, land, grav, wob, rad;
};

// exhibit: every array of the pattern in one block, PFMem in setup() (68,124 B, PSRAM).
struct Store {
  uint8_t idx[FRAME_W * FRAME_H];            // this frame's ramp indices
  uint8_t bg[FRAME_W * FRAME_H];             // sky + carpet + roots
  float groundPt[GROUND_ROWS * FRAME_W];     // |p1 * p2| of the carpet
  uint8_t groundFlake[GROUND_ROWS * FRAME_W];// carpet flake index
  uint8_t rootIdx[ROOT_ROWS * FRAME_W];      // root layer, 0 = no root
  float colSin35[FRAME_W], colCos35[FRAME_W];
  float trunkColW[3][FRAME_W];               // sin(px * 0.45 + seed) * 1.6
  Trunk trunks[3];
  TrunkRow rows[3][MAX_TRUNK_ROWS];
  int rowCount[3];
  Bough boughs[MAX_BOUGHS];
  float sway[SW_N][3];
  Root roots[MAX_ROOTS];
  Leaf branchLeaves[MAX_BRANCH_LEAVES];
  Leaf rootLeaves[MAX_ROOT_LEAVES];
  Drifter drifters[NUM_DRIFTERS];
  Neighbor neighbors[NUM_NEIGHBORS];
  Shower shower[SHOWER_COUNT];
  int boughCount, rootCount, branchLeafCount, rootLeafCount;
};
static Store* S = nullptr;

// Bezier weights and the bough radius factor per sample, fixed.
static float bW0[B_STEPS + 1], bW1[B_STEPS + 1], bW2[B_STEPS + 1], bRadF[B_STEPS + 1], bInvRadF[B_STEPS + 1];

// Knobs (K1-K3 through PFParams; K4 never reaches the pattern)
static float foliage = 0.625f;
static float windDir = 1.25f;
static float speed = 1.0f;
static float k4Unused = 0.0f;

// K1 press state (the idle return resets exactly these two)
static int treeIndex = 0;
static float showerT = SHOWER_DUR;
static int builtIndex = -1;

static float timeAcc = 0.0f;
static float shake = 0.0f;
static float bgFoliage = -1.0f;   // foliage the background was built for
static bool bgDirty = true;

static inline uint8_t toIdx(float v) {
  return (uint8_t)(int)(v * 255.0f + 0.5f);
}

static inline int iceil(float x) {
  int i = (int)x;
  return ((float)i < x) ? i + 1 : i;
}

// ── Tree build (runs at boot and on a K1 press; float, in place) ─────────

static uint32_t rngS = 0;
static inline float rnd() {
  rngS = (rngS * 0x41C64E6Du + 0x3039u) & 0x7FFFFFFFu;
  return (float)rngS / 2147483647.0f;
}

static inline float trunkX(int tIdx, float y) {
  const Trunk& tr = S->trunks[tIdx];
  float f = PFMath::clamp01((tr.y0 - y) / (tr.y0 - tr.y1));
  return tr.x0 * (1.0f - f) + tr.x1 * f;
}

static void addBough(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float rad, float seed, float ownerScale) {
  if (S->boughCount >= MAX_BOUGHS) return;
  Bough& b = S->boughs[S->boughCount++];
  b.p0x = p0x; b.p0y = p0y; b.p1x = p1x; b.p1y = p1y; b.p2x = p2x; b.p2y = p2y;
  b.rad = rad * ownerScale;
  b.seed = seed;
  b.invRad = 1.0f / b.rad;
}

static void addRoot(float p0x, float p0y, float ownerScale, float dir, float bias, float lenFactor) {
  // rnd() is drawn even when full, so the sequence never shifts (never happens: MAX_ROOTS is the bound)
  float baseLen = 14.0f + rnd() * 6.0f;
  float finalLen = baseLen * ownerScale * lenFactor;
  float midSpread = (8.0f + rnd() * 6.0f) * dir * bias;
  float endSpread = (16.0f + rnd() * 8.0f) * dir * bias;
  float rad = (3.2f + rnd() * 0.5f) * ownerScale * lenFactor;
  if (S->rootCount >= MAX_ROOTS) return;
  Root& r = S->roots[S->rootCount++];
  r.p0x = p0x; r.p0y = p0y;
  r.p1x = p0x + midSpread * ownerScale; r.p1y = p0y + finalLen * 0.4f;
  r.p2x = p0x + endSpread * ownerScale; r.p2y = p0y + finalLen;
  r.rad = rad;
}

// The y part of getTreeSway, from the table: P, P cos(0.03 y), P sin(0.03 y)
static inline void swayTerms(float y, float& P, float& Pc, float& Ps) {
  float fy = y - (float)SW_Y0;
  if (fy < 0.0f) fy = 0.0f;
  if (fy > (float)(SW_N - 1)) fy = (float)(SW_N - 1);
  int i = (int)fy;
  if (i > SW_N - 2) i = SW_N - 2;
  const float f = fy - (float)i;
  const float* a = S->sway[i];
  const float* b = S->sway[i + 1];
  P = a[0] + (b[0] - a[0]) * f;
  Pc = a[1] + (b[1] - a[1]) * f;
  Ps = a[2] + (b[2] - a[2]) * f;
}

static void buildTree(int seedInt) {
  rngS = (((uint32_t)seedInt ^ 0x6C62272Eu) * 0x5DEECE66u + 0xBu) & 0x7FFFFFFFu;
  S->boughCount = 0;
  S->rootCount = 0;
  S->branchLeafCount = 0;
  S->rootLeafCount = 0;

  const float sc0 = 0.60f + rnd() * 0.40f;
  const float sc1 = 0.60f + rnd() * 0.40f;
  const float sc2 = 0.60f + rnd() * 0.40f;

  const float lean = (rnd() - 0.5f) * 4.0f;
  const float clusterWidth = 18.0f + rnd() * 6.0f;
  const float centerX = 32.0f + (rnd() - 0.5f) * 4.0f;

  const float t0BaseX = centerX - clusterWidth * (0.45f + rnd() * 0.1f);
  const float t1BaseX = centerX + (rnd() - 0.5f) * 2.5f;
  const float t2BaseX = centerX + clusterWidth * (0.45f + rnd() * 0.1f);

  const float t0H = (46.0f + rnd() * 8.0f) * sc0;
  const float t1H = (56.0f + rnd() * 8.0f) * sc1;
  const float t2H = (46.0f + rnd() * 8.0f) * sc2;

  Trunk* T = S->trunks;
  {
    float wb = (11.5f + rnd() * 2.0f) * sc0; float wt = (8.0f + rnd() * 1.5f) * sc0; float sd = rnd() * 8.0f;
    T[0] = {t0BaseX, 104.0f, t0BaseX + (lean - 2.0f) * sc0, 104.0f - t0H, wb, wt, sd, sc0};
  }
  {
    float wb = (14.0f + rnd() * 2.5f) * sc1; float wt = (9.5f + rnd() * 2.0f) * sc1; float sd = rnd() * 8.0f;
    T[1] = {t1BaseX, 106.0f, t1BaseX + lean * sc1, 106.0f - t1H, wb, wt, sd, sc1};
  }
  {
    float wb = (11.5f + rnd() * 2.0f) * sc2; float wt = (8.0f + rnd() * 1.5f) * sc2; float sd = rnd() * 8.0f;
    T[2] = {t2BaseX, 104.0f, t2BaseX + (lean + 2.0f) * sc2, 104.0f - t2H, wb, wt, sd, sc2};
  }

  // 1. Primary lateral wing boughs
  float leftReachX = t0BaseX - (26.0f + rnd() * 6.0f) * sc0;
  if (leftReachX < 1.0f) leftReachX = 1.0f;
  float rightReachX = t2BaseX + (26.0f + rnd() * 6.0f) * sc2;
  if (rightReachX > 62.0f) rightReachX = 62.0f;
  const float leftMidY = 104.0f - 28.0f * sc0;
  const float rightMidY = 104.0f - 28.0f * sc2;
  {
    float y = 104.0f - 26.0f * sc0;
    float e = leftMidY - 3.0f + rnd() * 5.0f; float sd = rnd() * 6.0f;
    addBough(trunkX(0, y), y, t0BaseX - 12.0f * sc0, leftMidY, leftReachX, e, 3.4f, sd, sc0);
  }
  {
    float y = 104.0f - 26.0f * sc2;
    float e = rightMidY - 3.0f + rnd() * 5.0f; float sd = rnd() * 6.0f;
    addBough(trunkX(2, y), y, t2BaseX + 12.0f * sc2, rightMidY, rightReachX, e, 3.4f, sd, sc2);
  }

  const float scMid = (sc0 + sc2) * 0.5f;
  const float bridgeY1 = 104.0f - 38.0f * scMid;
  const float bridgeY2 = 104.0f - 34.0f * scMid;
  addBough(trunkX(0, bridgeY1 + 1.0f), bridgeY1 + 1.0f, centerX, bridgeY1, trunkX(2, bridgeY1 - 1.0f), bridgeY1 - 1.0f, 3.0f, rnd() * 6.0f, scMid);
  addBough(trunkX(2, bridgeY2 + 1.0f), bridgeY2 + 1.0f, centerX, bridgeY2, trunkX(0, bridgeY2 - 1.0f), bridgeY2 - 1.0f, 2.8f, rnd() * 6.0f, scMid);

  float mLeftX = t0BaseX - (22.0f + rnd() * 5.0f) * sc0;
  if (mLeftX < 2.0f) mLeftX = 2.0f;
  float mRightX = t2BaseX + (22.0f + rnd() * 5.0f) * sc2;
  if (mRightX > 61.0f) mRightX = 61.0f;
  const float mLeftY = 104.0f - 44.0f * sc0;
  const float mRightY = 104.0f - 44.0f * sc2;
  addBough(trunkX(0, 104.0f - 40.0f * sc0), 104.0f - 40.0f * sc0, t0BaseX - 10.0f * sc0, mLeftY + 6.0f, mLeftX, mLeftY, 2.9f, rnd() * 6.0f, sc0);
  addBough(trunkX(2, 104.0f - 40.0f * sc2), 104.0f - 40.0f * sc2, t2BaseX + 10.0f * sc2, mRightY + 6.0f, mRightX, mRightY, 2.9f, rnd() * 6.0f, sc2);

  struct CrownFork { int tid; float dx, dy, rad, sc; };
  const CrownFork forks[4] = {
    {0, -12.0f, -24.0f, 2.5f, sc0},
    {1, -8.0f, -28.0f, 2.4f, sc1},
    {1, 8.0f, -28.0f, 2.4f, sc1},
    {2, 12.0f, -24.0f, 2.5f, sc2},
  };
  for (int c = 0; c < 4; c++) {
    const CrownFork& cf = forks[c];
    const Trunk& tr = T[cf.tid];
    float endX = tr.x1 + cf.dx * cf.sc + (rnd() - 0.5f) * 4.0f;
    float endY = tr.y1 + cf.dy * cf.sc + (rnd() - 0.5f) * 4.0f;
    float midX = tr.x1 * 0.5f + endX * 0.5f + (rnd() - 0.5f) * 3.0f;
    float midY = tr.y1 * 0.5f + endY * 0.5f;
    addBough(tr.x1, tr.y1, midX, midY, endX, endY, cf.rad, rnd() * 6.0f, cf.sc);
  }

  // 2. Trunk lateral side shoots
  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = T[tid];
    const float sc = tr.sc;
    for (int step = 1; step <= 3; step++) {
      float frac = (float)step / 4.0f;
      float cy = tr.y0 - (tr.y0 - tr.y1) * frac;
      float cx = trunkX(tid, cy);
      float dir = (tid == 0) ? -1.0f : (tid == 2 ? 1.0f : (rnd() > 0.5f ? 1.0f : -1.0f));
      float drop = (rnd() > 0.45f) ? -5.0f * sc : 3.0f * sc;
      float reach = (5.0f + rnd() * 4.0f) * sc;
      addBough(cx, cy, cx + dir * (reach * 0.5f), cy + drop * 0.5f, cx + dir * reach, cy + drop, 1.6f, rnd() * 6.0f, sc);
    }
  }

  // 3. Mid-bough lateral twigs
  const int primaryCount = S->boughCount;
  for (int i = 0; i < primaryCount; i++) {
    const Bough b = S->boughs[i];   // copy: addBough appends to the same array
    if (b.rad < 1.4f) continue;
    for (int ui = 0; ui < 2; ui++) {
      const float u = ui == 0 ? 0.35f : 0.70f;
      const float om = 1.0f - u;
      float ox = om * om * b.p0x + 2.0f * om * u * b.p1x + u * u * b.p2x;
      float oy = om * om * b.p0y + 2.0f * om * u * b.p1y + u * u * b.p2y;
      float tx = b.p2x - b.p0x;
      float ty = b.p2y - b.p0y;
      float len = sqrtf(tx * tx + ty * ty) + 0.1f;
      float normX = tx / len;
      float normY = ty / len;
      float crossDir = (ui == 0) ? 1.0f : -1.0f;
      float reach = (4.0f + rnd() * 4.0f) * (b.rad / 3.0f);
      float px = -normY * crossDir * reach;
      float py = normX * crossDir * reach - 1.5f;
      addBough(ox, oy, ox + px * 0.5f, oy + py * 0.5f, ox + px, oy + py, b.rad * 0.5f, rnd() * 6.0f, 1.0f);
    }
  }

  // 4. Ground roots
  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = T[tid];
    const float sc = tr.sc;
    const float groundY = tr.y0 - 6.0f;
    const int rootN = (rnd() > 0.45f) ? 3 : 2;
    if (tid == 0) {
      addRoot(tr.x0 - 2.0f * sc, groundY, sc, -1.0f, 1.0f, 1.0f);
      addRoot(tr.x0 + 1.0f * sc, groundY + 1.0f, sc, 1.0f, 0.4f, 0.9f);
      if (rootN == 3) addRoot(tr.x0 - 0.5f * sc, groundY + 0.5f, sc, -1.0f, 0.3f, 0.95f);
    } else if (tid == 1) {
      addRoot(tr.x0 - 1.5f * sc, groundY, sc, -1.0f, 0.8f, 1.0f);
      addRoot(tr.x0 + 1.5f * sc, groundY, sc, 1.0f, 0.8f, 1.0f);
      if (rootN == 3) {
        float centralDir = (rnd() > 0.5f) ? 1.0f : -1.0f;
        addRoot(tr.x0, groundY + 0.5f, sc, centralDir, 0.2f, 0.95f);
      }
    } else {
      addRoot(tr.x0 + 2.0f * sc, groundY, sc, 1.0f, 1.0f, 1.0f);
      addRoot(tr.x0 - 1.0f * sc, groundY + 1.0f, sc, -1.0f, 0.4f, 0.9f);
      if (rootN == 3) addRoot(tr.x0 + 0.5f * sc, groundY + 0.5f, sc, 1.0f, 0.3f, 0.95f);
    }
  }

  // 5. Canopy foliage clusters (up to 1400 attempts, powf each - float, as the JS asks)
  int leafAttempts = 0;
  while (S->branchLeafCount < MAX_BRANCH_LEAVES && leafAttempts < 1400) {
    leafAttempts++;
    int bi = (int)(rnd() * (float)S->boughCount);
    if (bi >= S->boughCount) bi = S->boughCount - 1;
    const Bough& b = S->boughs[bi];
    const float u = 0.15f + 0.85f * rnd();
    const float om = 1.0f - u;
    float bx = om * om * b.p0x + 2.0f * om * u * b.p1x + u * u * b.p2x;
    float by = om * om * b.p0y + 2.0f * om * u * b.p1y + u * u * b.p2y;
    float upperFactor = PFMath::clamp01((82.0f - by) / 60.0f);
    float keepProb = powf(upperFactor, 1.6f);
    if (rnd() < keepProb) {
      const int n = S->branchLeafCount;
      float jx = sinf((float)leafAttempts * 4.3f + rnd()) * (4.0f + (float)(leafAttempts % 4));
      float jy = cosf((float)leafAttempts * 3.7f + rnd()) * (3.0f + (float)(leafAttempts % 4));
      float seed = (float)leafAttempts * 0.73f + rnd() * 2.0f;
      Leaf& lf = S->branchLeaves[n];
      lf.x = bx + jx;
      lf.y = by + jy;
      lf.size = 1.2f + (float)(n % 3) * 0.35f;
      lf.tone = 0.84f + ((float)(n % 6) / 6.0f) * 0.14f;
      lf.sa = sinf(seed * 2.0f);
      lf.ca = cosf(seed * 2.0f);
      swayTerms(lf.y, lf.P, lf.Pc, lf.Ps);
      S->branchLeafCount = n + 1;
    }
  }

  // 6. Leaves resting on roots
  for (int r = 0; r < S->rootCount; r++) {
    const Root& rt = S->roots[r];
    const int perRoot = 8 + (int)(rt.rad * 2.0f);
    for (int k = 0; k < perRoot; k++) {
      const float u = 0.2f + 0.8f * ((float)k / ((float)perRoot - 1.0f));
      const float om = 1.0f - u;
      float rx = om * om * rt.p0x + 2.0f * om * u * rt.p1x + u * u * rt.p2x;
      float ry = om * om * rt.p0y + 2.0f * om * u * rt.p1y + u * u * rt.p2y;
      float offX = (rnd() - 0.5f) * (rt.rad * 1.5f);
      float offY = (rnd() - 0.5f) * (rt.rad * 1.3f);
      float size = 1.2f + rnd() * 0.4f;
      float tone = 0.84f + rnd() * 0.14f;
      if (S->rootLeafCount >= MAX_ROOT_LEAVES) continue;
      Leaf& lf = S->rootLeaves[S->rootLeafCount++];
      lf.x = rx + offX;
      lf.y = ry + offY;
      lf.size = size;
      lf.tone = tone;
    }
  }

  // 7. Drifting falling leaves
  const int bl = S->branchLeafCount;
  for (int k = 0; k < NUM_DRIFTERS; k++) {
    int pick = (int)(rnd() * 5.0f);
    const Leaf& host = S->branchLeaves[bl > 0 ? (k * 7 + pick) % bl : 0];
    Drifter& df = S->drifters[k];
    float seed = (float)k * 1.73f + rnd();
    df.originX = host.x;
    df.originY = host.y;
    float fallRate = 42.0f + (float)(k % 5) * 9.0f;
    float phaseOffset = rnd() * 100.0f;
    df.wobble = rnd() * 5.0f;
    df.s25 = sinf(seed * 2.5f); df.c25 = cosf(seed * 2.5f);
    df.s30 = sinf(seed * 3.0f); df.c30 = cosf(seed * 3.0f);
    // rebuildTree: cycle time and the progress the old (t + phaseOffset) % cycleTime gave at t = 0
    df.fallDist = 118.0f - df.originY;
    float cycleTime = df.fallDist / fallRate;
    df.invCycle = 1.0f / cycleTime;
    df.prog = PFMath::jsMod(phaseOffset, cycleTime) / cycleTime;
  }

  for (int j = 0; j < NUM_NEIGHBORS; j++) {
    Neighbor& nd = S->neighbors[j];
    nd.seed = (float)j * 2.37f + rnd();
    nd.startY = 4.0f + (float)((j * 17) % 65);
    float fallRate = 45.0f + (float)(j % 5) * 10.0f;
    float phaseOffset = rnd() * 100.0f;
    nd.s20 = sinf(nd.seed * 2.0f); nd.c20 = cosf(nd.seed * 2.0f);
    nd.sSd = sinf(nd.seed); nd.cSd = cosf(nd.seed);
    nd.fallDist = 120.0f - nd.startY;
    float cycleTime = nd.fallDist / fallRate;
    nd.invCycle = 1.0f / cycleTime;
    nd.prog = PFMath::jsMod(phaseOffset, cycleTime) / cycleTime;
  }

  // ── Derived per-tree tables (no rnd() below) ──

  // Trunk rows and columns
  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = T[tid];
    int steps = PFMath::ifloor(tr.y0 - tr.y1);
    if (steps < 1) steps = 1;
    if (steps > MAX_TRUNK_ROWS - 1) steps = MAX_TRUNK_ROWS - 1;
    S->rowCount[tid] = steps + 1;
    for (int i = 0; i <= steps; i++) {
      TrunkRow& row = S->rows[tid][i];
      float frac = (float)i / (float)steps;
      float cy = tr.y0 - frac * (tr.y0 - tr.y1);
      row.halfW = (tr.wBase * (1.0f - frac) + tr.wTop * frac) * 0.5f;
      row.invDiam = 1.0f / (row.halfW * 2.0f);
      row.baseCx = (tr.x0 * (1.0f - frac) + tr.x1 * frac) + sinf(cy * 0.22f + tr.seed * 3.0f) * 0.5f;
      row.cy24 = cy * 0.24f;
      row.sA = sinf(cy * 0.18f);
      row.cA = cosf(cy * 0.18f);
      swayTerms(cy, row.swP, row.swPc, row.swPs);
      const float k = 0.75f * tr.sc;   // getTreeSway(cy) * 0.75 * sc
      row.swP *= k; row.swPc *= k; row.swPs *= k;
      row.iy = PFMath::ifloor(cy);
    }
    for (int px = 0; px < FRAME_W; px++) S->trunkColW[tid][px] = sinf((float)px * 0.45f + tr.seed) * 1.6f;
  }

  // Root layer (static): the JS root loop, written into its own overlay
  memset(S->rootIdx, 0, sizeof(S->rootIdx));
  for (int r = 0; r < S->rootCount; r++) {
    const Root& rt = S->roots[r];
    for (int s = 0; s <= R_STEPS; s++) {
      const float u = (float)s / (float)R_STEPS;
      const float om = 1.0f - u;
      float rx = om * om * rt.p0x + 2.0f * om * u * rt.p1x + u * u * rt.p2x;
      float ry = om * om * rt.p0y + 2.0f * om * u * rt.p1y + u * u * rt.p2y;
      float curR = rt.rad * (1.0f - u * 0.50f);
      int rLim = iceil(curR);
      float shadeX = 1.0f - (rx / (float)FRAME_W) * 0.20f;
      const float invR = 1.0f / curR;
      for (int dy = -rLim; dy <= rLim; dy++) {
        for (int dx = -rLim; dx <= rLim; dx++) {
          if ((float)(dx * dx + dy * dy) <= curR * curR) {
            float sideFactor = 0.5f - ((float)dx * invR) * 0.45f;
            float v = PFMath::clamp((0.26f + sideFactor * 0.18f) * shadeX, 0.20f, 0.46f);
            int px = PFMath::ifloor(rx + (float)dx);
            int py = PFMath::ifloor(ry + (float)dy);
            if (px >= 0 && px < FRAME_W && py >= ROOT_Y0 && py < FRAME_H) {
              S->rootIdx[(py - ROOT_Y0) * FRAME_W + px] = toIdx(v);   // 51..117, never 0
            }
          }
        }
      }
    }
  }
  bgDirty = true;
}

// Build the tree for treeIndex and everything derived from it; then the shower
// starts from the new canopy (the JS does this in update() after rebuildTree).
static void rebuildTree() {
  buildTree(TREE_SEEDS[treeIndex]);
  builtIndex = treeIndex;
  const int L = S->branchLeafCount;
  for (int k = 0; k < SHOWER_COUNT; k++) {
    const Leaf& lf = S->branchLeaves[L > 0 ? (k * 37 + 11) % L : 0];
    S->shower[k].x = lf.x;
    S->shower[k].y = lf.y;
  }
}

// Sky + carpet (+ roots) for the current Foliage
static void rebuildBackground() {
  uint8_t* bg = S->bg;
  for (int y = 0; y < HORIZON_Y; y++) {
    float skyTone = 0.03f + ((float)y / (float)HORIZON_Y) * 0.06f;
    memset(bg + y * FRAME_W, toIdx(skyTone), FRAME_W);
  }
  for (int y = HORIZON_Y; y < FRAME_H; y++) {
    const int gy = y - HORIZON_Y;
    const float depth = (float)gy / (float)GROUND_ROWS;
    const float coverage = 0.28f + depth * 0.52f * foliage;
    const uint8_t baseIdx = toIdx(0.72f + depth * 0.10f);
    const float* pt = S->groundPt + gy * FRAME_W;
    const uint8_t* fl = S->groundFlake + gy * FRAME_W;
    uint8_t* row = bg + y * FRAME_W;
    for (int x = 0; x < FRAME_W; x++) row[x] = (pt[x] < coverage) ? fl[x] : baseIdx;
    if (y >= ROOT_Y0) {
      const uint8_t* rr = S->rootIdx + (y - ROOT_Y0) * FRAME_W;
      for (int x = 0; x < FRAME_W; x++) if (rr[x]) row[x] = rr[x];
    }
  }
  bgFoliage = foliage;
  bgDirty = false;
}

// drawChunkyLeaf: a disc of one index, the centre pixel +0.04
static inline void chunkyLeaf(uint8_t* idx, float cx, float cy, float rad, float tone) {
  const int rCeil = iceil(rad);
  const float r2 = rad * rad;
  const uint8_t vN = toIdx(PFMath::clamp(tone, 0.82f, 0.98f));
  const uint8_t vC = toIdx(PFMath::clamp(tone + 0.04f, 0.82f, 0.98f));
  const int x0 = PFMath::ifloor(cx);
  const int y0 = PFMath::ifloor(cy);
  for (int dy = -rCeil; dy <= rCeil; dy++) {
    const int py = y0 + dy;
    if (py < 0 || py >= FRAME_H) continue;
    uint8_t* row = idx + py * FRAME_W;
    const int dy2 = dy * dy;
    for (int dx = -rCeil; dx <= rCeil; dx++) {
      if ((float)(dx * dx + dy2) <= r2) {
        const int px = x0 + dx;
        if (px >= 0 && px < FRAME_W) row[px] = (dx == 0 && dy == 0) ? vC : vN;
      }
    }
  }
}

void setup() {
  PFMath::buildSinLUT();
  // exhibit: one PFMem block instead of namespace statics (compiled-in statics live in internal RAM)
  if (!S) S = (Store*)PFMem::alloc(sizeof(Store));
  if (!S) return;

  for (int s = 0; s <= B_STEPS; s++) {
    const float u = (float)s / (float)B_STEPS;
    const float om = 1.0f - u;
    bW0[s] = om * om;
    bW1[s] = 2.0f * om * u;
    bW2[s] = u * u;
    bRadF[s] = 1.0f - u * 0.55f;
    bInvRadF[s] = 1.0f / bRadF[s];
  }
  for (int i = 0; i < SW_N; i++) {
    const float y = (float)(SW_Y0 + i);
    const float hr = PFMath::clamp01((110.0f - y) / 110.0f);
    const float p = (hr > 0.0f) ? powf(hr, 1.7f) : 0.0f;
    S->sway[i][0] = p;
    S->sway[i][1] = p * cosf(y * 0.03f);
    S->sway[i][2] = p * sinf(y * 0.03f);
  }
  for (int px = 0; px < FRAME_W; px++) {
    S->colSin35[px] = sinf((float)px * 0.35f);
    S->colCos35[px] = cosf((float)px * 0.35f);
  }

  // The carpet texture: fixed per pixel, exact sinf once
  for (int y = HORIZON_Y; y < FRAME_H; y++) {
    const int gy = y - HORIZON_Y;
    const float fy = (float)y;
    const float depth = (float)gy / (float)GROUND_ROWS;
    const float mound = sinf(fy * 0.16f + cosf(fy * 0.09f)) * 2.5f;
    for (int x = 0; x < FRAME_W; x++) {
      const float fx = (float)x;
      const float p1 = sinf(fx * 2.1f + fy * 2.7f + mound);
      const float p2 = cosf(fx * 3.7f - fy * 1.9f + mound * 0.5f);
      const float microGlint = sinf(fx * 5.3f + fy * 4.9f);
      float flakeVal = 0.82f + depth * 0.12f + (p1 - p2) * 0.06f;
      if (microGlint > 0.75f) flakeVal += 0.08f;
      S->groundPt[gy * FRAME_W + x] = fabsf(p1 * p2);
      S->groundFlake[gy * FRAME_W + x] = toIdx(PFMath::clamp(flakeVal, 0.74f, 0.98f));
    }
  }

  timeAcc = 0.0f;
  treeIndex = 0;
  showerT = SHOWER_DUR;
  builtIndex = -1;
  rebuildTree();

  // Shower leaves: fixed per-leaf constants; the start points come from the canopy at each press
  for (int k = 0; k < SHOWER_COUNT; k++) {
    Shower& sl = S->shower[k];
    const float a = (float)k * 0.61803398875f;
    const float f = a - PFMath::floorF(a);
    sl.delay = f * 0.35f;
    sl.land = 100.0f + (float)(k % 5) * 3.0f;
    sl.grav = 36.0f + (float)((k * 5) % 9) * 4.0f;
    sl.wob = (float)k * 1.37f;
    sl.rad = 1.0f + (float)(k % 3) * 0.25f;
  }
  // setup() in the JS never places the shower (showerT starts at SHOWER_DUR)
}

void update(float dt, const InputFrame& input) {
  if (!S) return;
  PFParams::apply(input, 0, &foliage, 0.25f, 1.0f, 0.0234375f);
  PFParams::apply(input, 1, &windDir, -0.75f, 3.25f, 0.125f);
  PFParams::apply(input, 2, &speed, 0.35f, 1.65f, 0.040625f);
  PFParams::apply(input, 3, &k4Unused, 0.0f, 1.0f, 0.03125f);   // K4 never reaches the pattern

  // K1 press: next tree, and the shower starts from the new canopy (pressing again re-triggers it)
  if (input.btnPressed[0] && !input.paramAbsoluteActive[0] && !input.knobAudioActive[0]) {
    treeIndex = (treeIndex + 1) % TREE_COUNT;
    showerT = 0.0f;
  }
  if (treeIndex < 0 || treeIndex >= TREE_COUNT) treeIndex = 0;
  if (treeIndex != builtIndex) rebuildTree();

  // C++ 번역 시: showerT는 SHOWER_DUR에서 멈춘다
  if (showerT < SHOWER_DUR) {
    showerT += dt;
    if (showerT > SHOWER_DUR) showerT = SHOWER_DUR;
  }
  const float e = 1.0f - showerT * (1.0f / SHOWER_DUR);
  shake = e * e;

  // C++ 번역 시: 이 주기(20π)를 그대로 유지
  timeAcc += dt * speed;
  if (timeAcc >= TIME_WRAP) timeAcc -= TIME_WRAP;

  // Per-leaf fall progress, wrapped at exactly one cycle
  const float step = dt * speed;
  for (int d = 0; d < NUM_DRIFTERS; d++) {
    Drifter& df = S->drifters[d];
    df.prog += step * df.invCycle;
    df.prog -= PFMath::floorF(df.prog);
  }
  for (int j = 0; j < NUM_NEIGHBORS; j++) {
    Neighbor& nd = S->neighbors[j];
    nd.prog += step * nd.invCycle;
    nd.prog -= PFMath::floorF(nd.prog);
  }
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);
  if (!S) { PFCanvas::present(); return; }

  if (bgDirty || foliage != bgFoliage) rebuildBackground();
  uint8_t* idx = S->idx;
  memcpy(idx, S->bg, sizeof(S->idx));   // 1. sky, 2. carpet (and 4. roots, see trunks)

  const float t = timeAcc;
  const float W = windDir;
  const float absW = W < 0.0f ? -W : W;
  // getTreeSway(y) = A * P + B * (s18 * Pc - c18 * Ps)
  const float swA = W * 1.2f;
  const float swB = 0.45f * (absW > 0.8f ? absW : 0.8f);
  const float s18 = PFMath::fastSin(t * 1.8f), c18 = PFMath::fastCos(t * 1.8f);
  const float bS18 = swB * s18, bC18 = swB * c18;

  // 3. Trunks. Roots are drawn after them in the JS, so a trunk pixel under a root stays root.
  for (int tid = 0; tid < 3; tid++) {
    const float* colW = S->trunkColW[tid];
    const int n = S->rowCount[tid];
    for (int i = 0; i < n; i++) {
      const TrunkRow& row = S->rows[tid][i];
      const int iy = row.iy;
      if (iy < 0 || iy >= FRAME_H) continue;
      const float sway = swA * row.swP + bS18 * row.swPc - bC18 * row.swPs;
      const float left = row.baseCx + sway - row.halfW;
      int minX = PFMath::ifloor(left);
      int maxX = iceil(left + row.halfW * 2.0f);
      if (minX < 0) minX = 0;
      if (maxX > FRAME_W - 1) maxX = FRAME_W - 1;
      uint8_t* out = idx + iy * FRAME_W;
      const uint8_t* rootRow = (iy >= ROOT_Y0) ? S->rootIdx + (iy - ROOT_Y0) * FRAME_W : nullptr;
      const float sA22 = row.sA * 2.2f, cA22 = row.cA * 2.2f;
      for (int px = minX; px <= maxX; px++) {
        const float u = ((float)px - left) * row.invDiam;
        if (u < 0.0f || u > 1.0f) continue;
        if (rootRow && rootRow[px]) continue;
        const float macroShadow = 1.0f - (float)px * (0.24f / (float)FRAME_W);
        const float cylinder = PFMath::fastSin(u * PI);
        const float warpY = row.cy24 + colW[px];
        // sin(cy * 0.18 + px * 0.35), by the addition formula
        const float warpX = u * 13.0f + sA22 * S->colCos35[px] + cA22 * S->colSin35[px];
        const float barkGrain = PFMath::fastSin(warpX) * 0.045f + PFMath::fastCos(warpY * 1.8f + warpX * 0.6f) * 0.035f;
        const float tone = (0.24f + cylinder * 0.10f + (1.0f - u) * 0.18f) * macroShadow + barkGrain;
        out[px] = toIdx(PFMath::clamp(tone, 0.20f, 0.48f));
      }
    }
  }

  // 5. Boughs
  for (int b = 0; b < S->boughCount; b++) {
    const Bough& bg = S->boughs[b];
    for (int s = 0; s <= B_STEPS; s++) {
      const float bx = bW0[s] * bg.p0x + bW1[s] * bg.p1x + bW2[s] * bg.p2x;
      const float by = bW0[s] * bg.p0y + bW1[s] * bg.p1y + bW2[s] * bg.p2y;
      float P, Pc, Ps;
      swayTerms(by, P, Pc, Ps);
      const float px = bx + swA * P + bS18 * Pc - bC18 * Ps;
      float curRad = bg.rad * bRadF[s];
      float invRad = bg.invRad * bInvRadF[s];   // 1 / curRad
      if (curRad < 0.7f) { curRad = 0.7f; invRad = INV_MIN_RAD; }
      const int radCeil = iceil(curRad);
      const float radSq = curRad * curRad;
      const float macroFalloff = 1.0f - px * (0.22f / (float)FRAME_W);
      // limbVal = (0.31 + (-dx - dy) * 0.7 * invRad * 0.12) * macroFalloff
      const float base = 0.31f * macroFalloff;
      const float slope = 0.084f * invRad * macroFalloff;
      const int ix0 = PFMath::ifloor(px);
      const int iy0 = PFMath::ifloor(by);
      for (int dy = -radCeil; dy <= radCeil; dy++) {
        const int iy = iy0 + dy;
        if (iy < 0 || iy >= FRAME_H) continue;
        uint8_t* out = idx + iy * FRAME_W;
        const float rem = radSq - (float)(dy * dy);
        if (rem < 0.0f) continue;
        for (int dx = -radCeil; dx <= radCeil; dx++) {
          if ((float)(dx * dx) > rem) continue;
          const int ix = ix0 + dx;
          if (ix < 0 || ix >= FRAME_W) continue;
          const float v = base - slope * (float)(dx + dy);
          out[ix] = toIdx(PFMath::clamp(v, 0.20f, 0.48f));
        }
      }
    }
  }

  const float fol = foliage;

  // 6. Root leaves
  const int rootLeafN = PFMath::ifloor((float)S->rootLeafCount * fol);
  for (int i = 0; i < rootLeafN; i++) {
    const Leaf& lf = S->rootLeaves[i];
    chunkyLeaf(idx, lf.x, lf.y, lf.size, lf.tone);
  }

  // 7. Canopy foliage clusters: sin(k t + seed * 2) by the addition formula
  const float s25 = PFMath::fastSin(t * 2.5f), c25 = PFMath::fastCos(t * 2.5f);
  const bool shaking = shake > 0.0f;
  const float sSh = shaking ? PFMath::fastSin(showerT * 19.0f) : 0.0f;
  const float cSh = shaking ? PFMath::fastCos(showerT * 19.0f) : 0.0f;
  const float shAmp = 1.4f * shake;
  const int leafN = PFMath::ifloor((float)S->branchLeafCount * fol);
  for (int i = 0; i < leafN; i++) {
    const Leaf& lf = S->branchLeaves[i];
    const float sway = swA * lf.P + bS18 * lf.Pc - bC18 * lf.Ps;
    float flutter = (s25 * lf.ca + c25 * lf.sa) * 0.45f;
    if (shaking) flutter += (sSh * lf.ca + cSh * lf.sa) * shAmp;   // press: canopy shakes, fading out
    const float flicker = 0.90f + 0.10f * (s18 * lf.ca + c18 * lf.sa);
    chunkyLeaf(idx, lf.x + sway + flutter, lf.y, lf.size, lf.tone * flicker);
  }

  // 8. Native falling leaves
  const float s35 = PFMath::fastSin(t * 3.5f), c35 = PFMath::fastCos(t * 3.5f);
  const float s40 = PFMath::fastSin(t * 4.0f), c40 = PFMath::fastCos(t * 4.0f);
  const int drifterN = PFMath::ifloor((float)NUM_DRIFTERS * fol);
  for (int d = 0; d < drifterN; d++) {
    const Drifter& df = S->drifters[d];
    const float prog = df.prog;
    const float ly = df.originY + prog * df.fallDist;
    if (!(ly >= df.originY && ly < 122.0f)) continue;
    const float windPush = prog * W * 28.0f;
    const float flutter = PFMath::fastSin(prog * (PI * 4.0f) + df.wobble) * 2.2f;
    float lx = df.originX + windPush + flutter;
    lx = PFMath::jsMod(PFMath::jsMod(lx, (float)FRAME_W) + (float)FRAME_W, (float)FRAME_W);
    const float birthFade = PFMath::clamp(prog * 8.0f, 0.0f, 1.0f);
    const float over = ly - 98.0f;
    float groundFade = 1.0f - (over > 0.0f ? over : 0.0f) * (1.0f / 22.0f);
    if (groundFade < 0.0f) groundFade = 0.0f;
    if (birthFade * groundFade > 0.15f) {
      float tumble = s35 * df.c25 + c35 * df.s25;
      if (tumble < 0.0f) tumble = -tumble;
      const float shimmer = 0.88f + 0.12f * (s40 * df.c30 + c40 * df.s30);
      chunkyLeaf(idx, lx, ly, 0.8f + tumble * 0.6f, shimmer);
    }
  }

  // 9. Neighbouring tree leaves
  if (absW > 0.2f) {
    const float s30 = PFMath::fastSin(t * 3.0f), c30 = PFMath::fastCos(t * 3.0f);
    const float s38 = PFMath::fastSin(t * 3.8f), c38 = PFMath::fastCos(t * 3.8f);
    const float startX = (W > 0.0f) ? -10.0f : ((float)FRAME_W + 10.0f);
    const int nN = PFMath::ifloor((float)NUM_NEIGHBORS * fol);
    for (int j = 0; j < nN; j++) {
      const Neighbor& nd = S->neighbors[j];
      const float prog = nd.prog;
      const float ly = nd.startY + prog * nd.fallDist;
      if (!(ly < 122.0f)) continue;
      const float lx = startX + prog * W * 38.0f + PFMath::fastSin(prog * (PI * 5.0f) + nd.seed) * 2.0f;
      if (lx >= -2.0f && lx < (float)FRAME_W + 2.0f) {
        // cos(3 t + 2 seed), sin(3.8 t + seed)
        float tumble = c30 * nd.c20 - s30 * nd.s20;
        if (tumble < 0.0f) tumble = -tumble;
        const float shimmer = 0.86f + 0.14f * (s38 * nd.cSd + c38 * nd.sSd);
        chunkyLeaf(idx, lx, ly, 0.7f + tumble * 0.55f, shimmer);
      }
    }
  }

  // 10. Leaf shower after a K1 press (per leaf, not per pixel)
  if (showerT < SHOWER_DUR) {
    for (int k = 0; k < SHOWER_COUNT; k++) {
      const Shower& sl = S->shower[k];
      const float st = showerT - sl.delay;
      if (st <= 0.0f) continue;
      const float ly = sl.y + 0.5f * sl.grav * st * st + 6.0f * st;
      if (ly >= sl.land) continue;
      const float lx = sl.x + W * 7.0f * st + PFMath::fastSin(st * 7.0f + sl.wob) * 1.6f;
      float tumble = PFMath::fastSin(st * 9.0f + sl.wob);
      if (tumble < 0.0f) tumble = -tumble;
      chunkyLeaf(idx, lx, ly, sl.rad + tumble * 0.45f, 0.83f);   // 0.83 = the ramp's brightest gold
    }
  }

  // Indices -> colours
  for (int y = 0; y < FRAME_H; y++) {
    const uint8_t* row = idx + y * FRAME_W;
    for (int x = 0; x < FRAME_W; x++) {
      const uint8_t* c = RAMP_LUT[row[x]];
      PFCanvas::setPixel(x, y, c[0], c[1], c[2]);
    }
  }
  PFCanvas::present();
}

} // namespace BAKWAN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
