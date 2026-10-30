#pragma once

// ===== Patternflow pattern =====
// Title:    BAKWAN
// Author:   @engmung
// Date:     2026-09-16
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

#include <Arduino.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"
#include "src/core_mem.h"

namespace BAKWAN {

const char* NAME = "BAKWAN";
const char* const KNOB_LABELS[4] = {"Foliage", "WindDir", "TreeSeed", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;

static const uint8_t RAMP_LUT[256][3] = {
  {116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},{116,138,154},
  {116,138,154},{116,138,154},{116,138,154},{116,138,154},{114,138,155},{106,139,158},{98,140,161},{91,140,164},
  {83,141,167},{75,142,170},{68,143,173},{60,143,176},{52,144,179},{45,145,182},{37,146,185},{29,146,188},
  {21,147,191},{14,148,194},{6,148,197},{1,146,194},{7,132,174},{13,119,154},{18,105,134},{24,91,114},
  {30,78,94},{35,64,74},{41,51,53},{47,37,33},{52,24,14},{53,24,14},{54,25,15},{55,25,15},
  {57,26,15},{58,26,16},{59,26,16},{60,27,16},{61,27,16},{62,28,17},{64,28,17},{65,28,17},
  {66,29,18},{67,29,18},{68,30,18},{69,30,19},{70,30,19},{72,31,19},{73,31,20},{74,31,20},
  {75,32,20},{76,32,20},{77,33,21},{78,33,21},{80,33,21},{81,34,22},{82,34,22},{83,35,22},
  {84,35,23},{85,35,23},{87,36,23},{88,36,24},{89,37,24},{90,37,24},{91,37,24},{92,38,25},
  {97,41,25},{109,50,24},{121,58,23},{133,67,22},{145,76,21},{157,84,20},{162,88,20},{160,87,20},
  {158,85,20},{156,84,20},{154,83,20},{152,81,20},{150,80,20},{148,78,20},{146,77,20},{144,76,20},
  {142,74,20},{140,73,20},{138,71,20},{136,70,19},{134,68,19},{132,67,19},{130,66,19},{128,64,19},
  {126,63,19},{124,61,19},{122,60,19},{120,58,19},{118,57,19},{116,56,19},{115,54,19},{113,53,19},
  {111,51,19},{109,50,19},{107,49,19},{105,47,19},{103,46,19},{101,44,19},{99,43,19},{97,41,19},
  {95,40,19},{93,39,19},{91,37,19},{89,36,19},{87,34,18},{85,33,18},{83,31,18},{81,30,18},
  {79,29,18},{77,27,18},{75,26,18},{73,24,18},{71,23,18},{69,22,18},{67,20,18},{65,19,18},
  {58,16,16},{45,13,13},{33,9,9},{20,6,6},{8,2,2},{2,2,0},{9,8,0},{15,14,0},
  {21,20,0},{27,26,0},{33,32,0},{39,38,0},{45,44,0},{52,50,0},{58,56,0},{64,62,0},
  {70,68,0},{76,74,0},{82,80,0},{88,86,0},{94,92,0},{101,97,0},{107,103,0},{113,109,0},
  {119,115,0},{125,121,0},{131,127,0},{137,133,0},{144,139,0},{150,145,0},{156,151,0},{162,157,0},
  {168,163,0},{174,169,0},{180,175,0},{187,181,0},{193,187,0},{199,193,0},{205,198,0},{211,204,0},
  {217,210,0},{223,216,0},{229,222,0},{236,228,0},{242,234,0},{248,240,0},{254,246,0},{255,245,0},
  {254,243,0},{254,241,0},{253,239,0},{253,237,0},{253,234,0},{252,232,0},{252,230,0},{252,228,0},
  {251,226,0},{251,223,0},{250,221,0},{250,219,0},{250,217,0},{249,215,0},{249,213,0},{248,210,0},
  {248,208,0},{248,206,0},{247,204,0},{247,202,0},{246,200,0},{246,197,0},{246,195,0},{245,193,0},
  {245,191,0},{244,189,0},{244,187,0},{244,184,0},{243,182,0},{243,180,0},{243,178,0},{242,176,0},
  {242,174,0},{241,171,0},{241,169,0},{241,167,0},{240,165,0},{238,163,0},{235,161,0},{231,158,0},
  {228,156,0},{225,154,0},{221,152,0},{218,150,0},{215,147,0},{211,145,0},{208,143,0},{204,141,0},
  {201,139,0},{198,137,0},{194,134,0},{191,132,0},{188,130,0},{184,128,0},{181,126,0},{178,123,0},
  {174,121,0},{171,119,0},{168,117,0},{164,115,0},{161,112,0},{157,110,0},{154,108,0},{151,106,0},
  {147,104,0},{144,102,0},{141,99,0},{137,97,0},{134,95,0},{131,93,0},{127,91,0},{124,88,0},
  {121,86,0},{117,84,0},{114,82,0},{110,80,0},{107,78,0},{104,75,0},{100,73,0},{97,71,0}
};

struct Trunk {
  float x0;
  float y0;
  float x1;
  float y1;
  float wBase;
  float wTop;
  float seed;
  float sc;
};

struct Bough {
  float p0[2];
  float p1[2];
  float p2[2];
  float rad;
  float seed;
};

struct Root {
  float p0[2];
  float p1[2];
  float p2[2];
  float rad;
};

struct Leaf {
  float x;
  float y;
  float size;
  float seed;
  float tone;
};

struct Drifter {
  float seed;
  float originX;
  float originY;
  float fallRate;
  float phaseOffset;
  float wobbleSeed;
};

struct NeighborDrifter {
  float seed;
  float startY;
  float fallRate;
  float phaseOffset;
};

static Trunk trunks[3];
static float scales[3];

static constexpr int MAX_BOUGHS = 80;
static Bough boughs[MAX_BOUGHS];
static int boughCount = 0;

static constexpr int MAX_ROOTS = 12;
static Root roots[MAX_ROOTS];
static int rootCount = 0;

static constexpr int MAX_BRANCH_LEAVES = 280;
static Leaf branchLeaves[MAX_BRANCH_LEAVES];
static int branchLeafCount = 0;

static constexpr int MAX_ROOT_LEAVES = 180;
static Leaf rootLeaves[MAX_ROOT_LEAVES];
static int rootLeafCount = 0;

static constexpr int MAX_DRIFTERS = 60;
static Drifter drifters[MAX_DRIFTERS];
static int drifterCount = 0;

static constexpr int MAX_NEIGHBOR_DRIFTERS = 40;
static NeighborDrifter neighborDrifters[MAX_NEIGHBOR_DRIFTERS];
static int neighborDrifterCount = 0;

static float* valField = nullptr;

static float param_foliage = 0.631f;
static float param_windDir = 1.247f;
static float param_treeSeed = 3831.403f;
static float param_unused = 1.0f;

static int currentSeed = -1;
static float timeAcc = 0.0f;

static uint32_t rng_state = 1;
static inline float rnd() {
  rng_state = (rng_state * 1103515245U + 12345U) & 0x7FFFFFFFU;
  return (float)rng_state / 2147483647.0f;
}

static inline float getTrunkX(int tIdx, float y) {
  const Trunk& tr = trunks[tIdx];
  float f = (tr.y0 - y) / (tr.y0 - tr.y1);
  f = PFMath::clamp01(f);
  return tr.x0 * (1.0f - f) + tr.x1 * f;
}

static void addBough(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float rad, float seed, float ownerScale) {
  if (boughCount >= MAX_BOUGHS) return;
  Bough& b = boughs[boughCount++];
  b.p0[0] = p0x; b.p0[1] = p0y;
  b.p1[0] = p1x; b.p1[1] = p1y;
  b.p2[0] = p2x; b.p2[1] = p2y;
  b.rad = rad * ownerScale;
  b.seed = seed;
}

static void addProceduralRoot(float p0x, float p0y, float ownerScale, float dir, float bias, float lenFactor) {
  if (rootCount >= MAX_ROOTS) return;
  float baseLen = 14.0f + rnd() * 6.0f;
  float finalLen = baseLen * ownerScale * lenFactor;
  float midSpread = (8.0f + rnd() * 6.0f) * dir * bias;
  float endSpread = (16.0f + rnd() * 8.0f) * dir * bias;

  Root& r = roots[rootCount++];
  r.p0[0] = p0x;
  r.p0[1] = p0y;
  r.p1[0] = p0x + midSpread * ownerScale;
  r.p1[1] = p0y + (finalLen * 0.4f);
  r.p2[0] = p0x + endSpread * ownerScale;
  r.p2[1] = p0y + finalLen;
  r.rad = (3.2f + rnd() * 0.5f) * ownerScale * lenFactor;
}

static void buildTree(int seedInt) {
  uint32_t s0 = ((uint32_t)seedInt ^ 0x6C62272EU);
  rng_state = (s0 * 0x5DEECE66U + 0xBU) & 0x7FFFFFFFU;

  boughCount = 0;
  rootCount = 0;
  branchLeafCount = 0;
  rootLeafCount = 0;
  drifterCount = 0;
  neighborDrifterCount = 0;

  float sc0 = 0.60f + rnd() * 0.40f;
  float sc1 = 0.60f + rnd() * 0.40f;
  float sc2 = 0.60f + rnd() * 0.40f;
  scales[0] = sc0;
  scales[1] = sc1;
  scales[2] = sc2;

  float lean = (rnd() - 0.5f) * 4.0f;
  float clusterWidth = 18.0f + rnd() * 6.0f;
  float centerX = 32.0f + (rnd() - 0.5f) * 4.0f;

  float t0BaseX = centerX - clusterWidth * (0.45f + rnd() * 0.1f);
  float t1BaseX = centerX + (rnd() - 0.5f) * 2.5f;
  float t2BaseX = centerX + clusterWidth * (0.45f + rnd() * 0.1f);

  float t0H = (46.0f + rnd() * 8.0f) * sc0;
  float t1H = (56.0f + rnd() * 8.0f) * sc1;
  float t2H = (46.0f + rnd() * 8.0f) * sc2;

  float t0TopY = 104.0f - t0H;
  float t1TopY = 106.0f - t1H;
  float t2TopY = 104.0f - t2H;

  trunks[0] = { t0BaseX, 104.0f, t0BaseX + (lean - 2.0f) * sc0, t0TopY, (11.5f + rnd() * 2.0f) * sc0, (8.0f + rnd() * 1.5f) * sc0, rnd() * 8.0f, sc0 };
  trunks[1] = { t1BaseX, 106.0f, t1BaseX + lean * sc1,          t1TopY, (14.0f + rnd() * 2.5f) * sc1, (9.5f + rnd() * 2.0f) * sc1, rnd() * 8.0f, sc1 };
  trunks[2] = { t2BaseX, 104.0f, t2BaseX + (lean + 2.0f) * sc2, t2TopY, (11.5f + rnd() * 2.0f) * sc2, (8.0f + rnd() * 1.5f) * sc2, rnd() * 8.0f, sc2 };

  float leftReachX = t0BaseX - (26.0f + rnd() * 6.0f) * sc0;
  if (leftReachX < 1.0f) leftReachX = 1.0f;
  float rightReachX = t2BaseX + (26.0f + rnd() * 6.0f) * sc2;
  if (rightReachX > 62.0f) rightReachX = 62.0f;
  float leftMidY = 104.0f - 28.0f * sc0;
  float rightMidY = 104.0f - 28.0f * sc2;

  addBough(getTrunkX(0, 104.0f - 26.0f * sc0), 104.0f - 26.0f * sc0, t0BaseX - 12.0f * sc0, leftMidY, leftReachX, leftMidY - 3.0f + rnd() * 5.0f, 3.4f, rnd() * 6.0f, sc0);
  addBough(getTrunkX(2, 104.0f - 26.0f * sc2), 104.0f - 26.0f * sc2, t2BaseX + 12.0f * sc2, rightMidY, rightReachX, rightMidY - 3.0f + rnd() * 5.0f, 3.4f, rnd() * 6.0f, sc2);

  float scMid = (sc0 + sc2) * 0.5f;
  float bridgeY1 = 104.0f - 38.0f * scMid;
  float bridgeY2 = 104.0f - 34.0f * scMid;
  addBough(getTrunkX(0, bridgeY1 + 1.0f), bridgeY1 + 1.0f, centerX, bridgeY1, getTrunkX(2, bridgeY1 - 1.0f), bridgeY1 - 1.0f, 3.0f, rnd() * 6.0f, scMid);
  addBough(getTrunkX(2, bridgeY2 + 1.0f), bridgeY2 + 1.0f, centerX, bridgeY2, getTrunkX(0, bridgeY2 - 1.0f), bridgeY2 - 1.0f, 2.8f, rnd() * 6.0f, scMid);

  float mLeftX = t0BaseX - (22.0f + rnd() * 5.0f) * sc0;
  if (mLeftX < 2.0f) mLeftX = 2.0f;
  float mRightX = t2BaseX + (22.0f + rnd() * 5.0f) * sc2;
  if (mRightX > 61.0f) mRightX = 61.0f;
  float mLeftY = 104.0f - 44.0f * sc0;
  float mRightY = 104.0f - 44.0f * sc2;
  addBough(getTrunkX(0, 104.0f - 40.0f * sc0), 104.0f - 40.0f * sc0, t0BaseX - 10.0f * sc0, mLeftY + 6.0f, mLeftX, mLeftY, 2.9f, rnd() * 6.0f, sc0);
  addBough(getTrunkX(2, 104.0f - 40.0f * sc2), 104.0f - 40.0f * sc2, t2BaseX + 10.0f * sc2, mRightY + 6.0f, mRightX, mRightY, 2.9f, rnd() * 6.0f, sc2);

  struct CrownFork { int tid; float dx; float dy; float rad; float sc; };
  const CrownFork crownForks[4] = {
    { 0, -12.0f, -24.0f, 2.5f, sc0 },
    { 1,  -8.0f, -28.0f, 2.4f, sc1 },
    { 1,   8.0f, -28.0f, 2.4f, sc1 },
    { 2,  12.0f, -24.0f, 2.5f, sc2 }
  };

  for (int c = 0; c < 4; c++) {
    const CrownFork& cf = crownForks[c];
    const Trunk& tr = trunks[cf.tid];
    float endX = tr.x1 + cf.dx * cf.sc + (rnd() - 0.5f) * 4.0f;
    float endY = tr.y1 + cf.dy * cf.sc + (rnd() - 0.5f) * 4.0f;
    float midX = tr.x1 * 0.5f + endX * 0.5f + (rnd() - 0.5f) * 3.0f;
    float midY = tr.y1 * 0.5f + endY * 0.5f;
    addBough(tr.x1, tr.y1, midX, midY, endX, endY, cf.rad, rnd() * 6.0f, cf.sc);
  }

  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = trunks[tid];
    float sc = scales[tid];
    for (int step = 1; step <= 3; step++) {
      float frac = (float)step * 0.25f;
      float cy = tr.y0 - (tr.y0 - tr.y1) * frac;
      float cx = getTrunkX(tid, cy);
      float dir = (tid == 0) ? -1.0f : (tid == 2 ? 1.0f : (rnd() > 0.5f ? 1.0f : -1.0f));
      float drop = (rnd() > 0.45f) ? -5.0f * sc : 3.0f * sc;
      float reach = (5.0f + rnd() * 4.0f) * sc;
      addBough(cx, cy, cx + dir * (reach * 0.5f), cy + drop * 0.5f, cx + dir * reach, cy + drop, 1.6f, rnd() * 6.0f, sc);
    }
  }

  int primaryCount = boughCount;
  for (int i = 0; i < primaryCount; i++) {
    const Bough& b = boughs[i];
    if (b.rad < 1.4f) continue;

    const float uVals[2] = {0.35f, 0.70f};
    for (int ui = 0; ui < 2; ui++) {
      float u = uVals[ui];
      float oneMinusU = 1.0f - u;
      float ox = oneMinusU * oneMinusU * b.p0[0] + 2.0f * oneMinusU * u * b.p1[0] + u * u * b.p2[0];
      float oy = oneMinusU * oneMinusU * b.p0[1] + 2.0f * oneMinusU * u * b.p1[1] + u * u * b.p2[1];

      float tx = b.p2[0] - b.p0[0];
      float ty = b.p2[1] - b.p0[1];
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

  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = trunks[tid];
    float sc = tr.sc;
    float groundY = tr.y0 - 6.0f;
    int rCount = (rnd() > 0.45f) ? 3 : 2;

    if (tid == 0) {
      addProceduralRoot(tr.x0 - 2.0f * sc, groundY, sc, -1.0f, 1.0f, 1.0f);
      addProceduralRoot(tr.x0 + 1.0f * sc, groundY + 1.0f, sc, 1.0f, 0.4f, 0.9f);
      if (rCount == 3) {
        addProceduralRoot(tr.x0 - 0.5f * sc, groundY + 0.5f, sc, -1.0f, 0.3f, 0.95f);
      }
    } else if (tid == 1) {
      addProceduralRoot(tr.x0 - 1.5f * sc, groundY, sc, -1.0f, 0.8f, 1.0f);
      addProceduralRoot(tr.x0 + 1.5f * sc, groundY, sc, 1.0f, 0.8f, 1.0f);
      if (rCount == 3) {
        float centralDir = (rnd() > 0.5f) ? 1.0f : -1.0f;
        addProceduralRoot(tr.x0, groundY + 0.5f, sc, centralDir, 0.2f, 0.95f);
      }
    } else {
      addProceduralRoot(tr.x0 + 2.0f * sc, groundY, sc, 1.0f, 1.0f, 1.0f);
      addProceduralRoot(tr.x0 - 1.0f * sc, groundY + 1.0f, sc, -1.0f, 0.4f, 0.9f);
      if (rCount == 3) {
        addProceduralRoot(tr.x0 + 0.5f * sc, groundY + 0.5f, sc, 1.0f, 0.3f, 0.95f);
      }
    }
  }

  int leafAttempts = 0;
  while (branchLeafCount < MAX_BRANCH_LEAVES && leafAttempts < 1400) {
    leafAttempts++;
    if (boughCount == 0) break;
    int bIdx = (int)(rnd() * (float)boughCount);
    if (bIdx >= boughCount) bIdx = boughCount - 1;
    const Bough& b = boughs[bIdx];

    float u = 0.15f + 0.85f * rnd();
    float oneMinusU = 1.0f - u;
    float bx = oneMinusU * oneMinusU * b.p0[0] + 2.0f * oneMinusU * u * b.p1[0] + u * u * b.p2[0];
    float by = oneMinusU * oneMinusU * b.p0[1] + 2.0f * oneMinusU * u * b.p1[1] + u * u * b.p2[1];

    float upperFactor = (82.0f - by) / 60.0f;
    upperFactor = PFMath::clamp01(upperFactor);
    float keepProb = PFMath::fastPow(upperFactor, 1.6f);

    if (rnd() < keepProb) {
      float jx = sinf((float)leafAttempts * 4.3f + rnd()) * (4.0f + (float)(leafAttempts % 4));
      float jy = cosf((float)leafAttempts * 3.7f + rnd()) * (3.0f + (float)(leafAttempts % 4));

      Leaf& lf = branchLeaves[branchLeafCount];
      lf.x = bx + jx;
      lf.y = by + jy;
      lf.size = 1.2f + (float)(branchLeafCount % 3) * 0.35f;
      lf.seed = (float)leafAttempts * 0.73f + rnd() * 2.0f;
      lf.tone = 0.84f + ((float)(branchLeafCount % 6) / 6.0f) * 0.14f;
      branchLeafCount++;
    }
  }

  for (int r = 0; r < rootCount; r++) {
    const Root& rt = roots[r];
    int leavesPerRoot = 8 + (int)(rt.rad * 2.0f);
    for (int k = 0; k < leavesPerRoot; k++) {
      if (rootLeafCount >= MAX_ROOT_LEAVES) break;
      float u = 0.2f + 0.8f * ((float)k / (float)(leavesPerRoot - 1));
      float oneMinusU = 1.0f - u;
      float rx = oneMinusU * oneMinusU * rt.p0[0] + 2.0f * oneMinusU * u * rt.p1[0] + u * u * rt.p2[0];
      float ry = oneMinusU * oneMinusU * rt.p0[1] + 2.0f * oneMinusU * u * rt.p1[1] + u * u * rt.p2[1];
      float offX = (rnd() - 0.5f) * (rt.rad * 1.5f);
      float offY = (rnd() - 0.5f) * (rt.rad * 1.3f);

      Leaf& rlf = rootLeaves[rootLeafCount++];
      rlf.x = rx + offX;
      rlf.y = ry + offY;
      rlf.size = 1.2f + rnd() * 0.4f;
      rlf.seed = ((float)r * 14.0f + (float)k) * 0.61f;
      rlf.tone = 0.84f + rnd() * 0.14f;
    }
  }

  if (branchLeafCount > 0) {
    for (int k = 0; k < MAX_DRIFTERS; k++) {
      int hostIdx = (k * 7 + (int)(rnd() * 5.0f)) % branchLeafCount;
      const Leaf& hostLeaf = branchLeaves[hostIdx];

      Drifter& df = drifters[drifterCount++];
      df.seed = (float)k * 1.73f + rnd();
      df.originX = hostLeaf.x;
      df.originY = hostLeaf.y;
      df.fallRate = 42.0f + (float)(k % 5) * 9.0f;
      df.phaseOffset = rnd() * 100.0f;
      df.wobbleSeed = rnd() * 5.0f;
    }
  }

  for (int j = 0; j < MAX_NEIGHBOR_DRIFTERS; j++) {
    NeighborDrifter& nd = neighborDrifters[neighborDrifterCount++];
    nd.seed = (float)j * 2.37f + rnd();
    nd.startY = 4.0f + (float)((j * 17) % 65);
    nd.fallRate = 45.0f + (float)(j % 5) * 10.0f;
    nd.phaseOffset = rnd() * 100.0f;
  }
}

static inline float getTreeSway(float y, float t, float windDir) {
  float heightRatio = (110.0f - y) / 110.0f;
  heightRatio = PFMath::clamp01(heightRatio);
  float gentleBreathe = PFMath::fastSin(t * 1.8f - y * 0.03f) * 0.45f;
  float absWind = windDir < 0.0f ? -windDir : windDir;
  if (absWind < 0.001f) absWind = 0.8f;
  float curve = PFMath::fastPow(heightRatio, 1.7f);
  return (windDir * 1.2f + gentleBreathe * absWind) * curve;
}

static inline void drawChunkyLeaf(float cx, float cy, float rad, float tone) {
  int rCeil = (int)ceilf(rad);
  float radSq = rad * rad;
  for (int dy = -rCeil; dy <= rCeil; dy++) {
    int py = PFMath::ifloor(cy + (float)dy);
    if (py < 0 || py >= FRAME_H) continue;
    float dySq = (float)(dy * dy);
    int rowOffset = py * FRAME_W;

    for (int dx = -rCeil; dx <= rCeil; dx++) {
      if ((float)(dx * dx) + dySq <= radSq) {
        int px = PFMath::ifloor(cx + (float)dx);
        if (px >= 0 && px < FRAME_W) {
          float core = (dx == 0 && dy == 0) ? 0.04f : 0.0f;
          float val = tone + core;
          val = PFMath::clamp(val, 0.82f, 0.98f);
          valField[rowOffset + px] = val;
        }
      }
    }
  }
}

void setup() {
  PFMath::buildSinLUT();
  if (!valField) {
    valField = PFMem::allocFloats(FRAME_W * FRAME_H);
  }
  timeAcc = 0.0f;
  currentSeed = -1;
}

void update(float dt, const InputFrame& input) {
  if (!valField) return;

  PFParams::apply(input, 0, &param_foliage, 0.2f, 1.0f, 0.017f);
  PFParams::apply(input, 1, &param_windDir, -3.0f, 3.0f, 0.125f);
  PFParams::apply(input, 2, &param_treeSeed, 1.0f, 9999.0f, 208.292f);
  PFParams::apply(input, 3, &param_unused, 0.0f, 1.0f, 0.021f);

  timeAcc += dt;
  constexpr float PERIOD = TWO_PI * 100.0f;
  if (timeAcc >= PERIOD) {
    timeAcc = PFMath::jsMod(timeAcc, PERIOD);
  }

  int seedInt = PFMath::ifloor(param_treeSeed);
  if (seedInt != currentSeed) {
    currentSeed = seedInt;
    buildTree(seedInt);
  }
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);

  if (!valField) {
    PFCanvas::present();
    return;
  }

  const float t = timeAcc;
  const float foliage = param_foliage;
  const float windDir = param_windDir;
  constexpr int horizonY = 82;

  for (int y = 0; y < horizonY; y++) {
    float skyTone = 0.03f + ((float)y / (float)horizonY) * 0.06f;
    int rowOffset = y * FRAME_W;
    for (int x = 0; x < FRAME_W; x++) {
      valField[rowOffset + x] = skyTone;
    }
  }

  for (int y = horizonY; y < FRAME_H; y++) {
    float depth = (float)(y - horizonY) / (float)(FRAME_H - horizonY);
    float mound = PFMath::fastSin((float)y * 0.16f + PFMath::fastCos((float)y * 0.09f)) * 2.5f;
    int rowOffset = y * FRAME_W;
    float coverage = 0.28f + depth * 0.52f * foliage;
    float baseGround = 0.72f + depth * 0.10f;
    float baseFlake = 0.82f + depth * 0.12f;

    for (int x = 0; x < FRAME_W; x++) {
      float fx = (float)x;
      float fy = (float)y;
      float p1 = PFMath::fastSin(fx * 2.1f + fy * 2.7f + mound);
      float p2 = PFMath::fastCos(fx * 3.7f - fy * 1.9f + mound * 0.5f);
      float microGlint = PFMath::fastSin(fx * 5.3f + fy * 4.9f);
      float pointillism = p1 * p2;
      if (pointillism < 0.0f) pointillism = -pointillism;

      float lightTilt = (p1 - p2) * 0.06f;
      float flakeVal = baseFlake + lightTilt;
      if (microGlint > 0.75f) flakeVal += 0.08f;

      if (pointillism < coverage) {
        valField[rowOffset + x] = PFMath::clamp(flakeVal, 0.74f, 0.98f);
      } else {
        valField[rowOffset + x] = baseGround;
      }
    }
  }

  for (int tid = 0; tid < 3; tid++) {
    const Trunk& tr = trunks[tid];
    int steps = PFMath::ifloor(tr.y0 - tr.y1);
    if (steps < 1) steps = 1;
    float invSteps = 1.0f / (float)steps;

    for (int i = 0; i <= steps; i++) {
      float frac = (float)i * invSteps;
      float cy = tr.y0 - frac * (tr.y0 - tr.y1);
      int iy = PFMath::ifloor(cy);
      if (iy < 0 || iy >= FRAME_H) continue;

      float halfW = (tr.wBase * (1.0f - frac) + tr.wTop * frac) * 0.5f;
      float swayOffset = getTreeSway(cy, t, windDir) * 0.75f * tr.sc;
      float organicContour = PFMath::fastSin(cy * 0.22f + tr.seed * 3.0f) * 0.5f;
      float cx = (tr.x0 * (1.0f - frac) + tr.x1 * frac) + swayOffset + organicContour;

      int minX = PFMath::ifloor(cx - halfW);
      int maxX = (int)ceilf(cx + halfW);
      if (minX < 0) minX = 0;
      if (maxX >= FRAME_W) maxX = FRAME_W - 1;
      float invDiameter = 1.0f / (halfW * 2.0f);
      int rowOffset = iy * FRAME_W;

      for (int px = minX; px <= maxX; px++) {
        float u = ((float)px - (cx - halfW)) * invDiameter;
        if (u >= 0.0f && u <= 1.0f) {
          float directionalLight = 1.0f - u;
          float macroShadow = 1.0f - ((float)px / (float)FRAME_W) * 0.24f;
          float cylinder = PFMath::fastSin(u * PI);

          float warpY = cy * 0.24f + PFMath::fastSin((float)px * 0.45f + tr.seed) * 1.6f;
          float warpX = u * 13.0f + PFMath::fastSin(cy * 0.18f + (float)px * 0.35f) * 2.2f;
          float barkGrain = PFMath::fastSin(warpX) * 0.045f + PFMath::fastCos(warpY * 1.8f + warpX * 0.6f) * 0.035f;

          float tone = (0.24f + cylinder * 0.10f + directionalLight * 0.18f) * macroShadow + barkGrain;
          valField[rowOffset + px] = PFMath::clamp(tone, 0.20f, 0.48f);
        }
      }
    }
  }

  for (int r = 0; r < rootCount; r++) {
    const Root& rt = roots[r];
    constexpr int rSteps = 24;
    constexpr float invRSteps = 1.0f / (float)rSteps;

    for (int s = 0; s <= rSteps; s++) {
      float u = (float)s * invRSteps;
      float oneMinusU = 1.0f - u;
      float rx = oneMinusU * oneMinusU * rt.p0[0] + 2.0f * oneMinusU * u * rt.p1[0] + u * u * rt.p2[0];
      float ry = oneMinusU * oneMinusU * rt.p0[1] + 2.0f * oneMinusU * u * rt.p1[1] + u * u * rt.p2[1];
      float curR = rt.rad * (1.0f - u * 0.50f);
      int rLim = (int)ceilf(curR);
      float curRSq = curR * curR;
      float invCurR = 1.0f / curR;
      float macroFalloff = 1.0f - (rx / (float)FRAME_W) * 0.20f;

      for (int dy = -rLim; dy <= rLim; dy++) {
        int py = PFMath::ifloor(ry + (float)dy);
        if (py < 0 || py >= FRAME_H) continue;
        float dySq = (float)(dy * dy);
        int rowOffset = py * FRAME_W;

        for (int dx = -rLim; dx <= rLim; dx++) {
          if ((float)(dx * dx) + dySq <= curRSq) {
            int px = PFMath::ifloor(rx + (float)dx);
            if (px >= 0 && px < FRAME_W) {
              float sideFactor = 0.5f - ((float)dx * invCurR) * 0.45f;
              float rootVal = (0.26f + sideFactor * 0.18f) * macroFalloff;
              valField[rowOffset + px] = PFMath::clamp(rootVal, 0.20f, 0.46f);
            }
          }
        }
      }
    }
  }

  for (int b = 0; b < boughCount; b++) {
    const Bough& bg = boughs[b];
    constexpr int bSteps = 34;
    constexpr float invBSteps = 1.0f / (float)bSteps;

    for (int s = 0; s <= bSteps; s++) {
      float u = (float)s * invBSteps;
      float oneMinusU = 1.0f - u;
      float bx = oneMinusU * oneMinusU * bg.p0[0] + 2.0f * oneMinusU * u * bg.p1[0] + u * u * bg.p2[0];
      float by = oneMinusU * oneMinusU * bg.p0[1] + 2.0f * oneMinusU * u * bg.p1[1] + u * u * bg.p2[1];

      float swayOffset = getTreeSway(by, t, windDir);
      float px = bx + swayOffset;
      float py = by;
      float curRad = bg.rad * (1.0f - u * 0.55f);
      if (curRad < 0.7f) curRad = 0.7f;
      int radCeil = (int)ceilf(curRad);
      float curRadSq = curRad * curRad;
      float invCurRad = 1.0f / curRad;
      float macroFalloff = 1.0f - (px / (float)FRAME_W) * 0.22f;

      for (int dy = -radCeil; dy <= radCeil; dy++) {
        int iy = PFMath::ifloor(py + (float)dy);
        if (iy < 0 || iy >= FRAME_H) continue;
        float dySq = (float)(dy * dy);
        int rowOffset = iy * FRAME_W;

        for (int dx = -radCeil; dx <= radCeil; dx++) {
          if ((float)(dx * dx) + dySq <= curRadSq) {
            int ix = PFMath::ifloor(px + (float)dx);
            if (ix >= 0 && ix < FRAME_W) {
              float lightVec = (-(float)dx * 0.7f - (float)dy * 0.7f) * invCurRad;
              float reliefShade = 0.31f + lightVec * 0.12f;
              float limbVal = reliefShade * macroFalloff;
              valField[rowOffset + ix] = PFMath::clamp(limbVal, 0.20f, 0.48f);
            }
          }
        }
      }
    }
  }

  int numRootLeaves = PFMath::ifloor((float)rootLeafCount * foliage);
  for (int i = 0; i < numRootLeaves; i++) {
    const Leaf& rLeaf = rootLeaves[i];
    drawChunkyLeaf(rLeaf.x, rLeaf.y, rLeaf.size, rLeaf.tone);
  }

  int numLeaves = PFMath::ifloor((float)branchLeafCount * foliage);
  for (int i = 0; i < numLeaves; i++) {
    const Leaf& lf = branchLeaves[i];
    float swayOffset = getTreeSway(lf.y, t, windDir);
    float flutter = PFMath::fastSin(t * 2.5f + lf.seed * 2.0f) * 0.45f;
    float lx = lf.x + swayOffset + flutter;
    float ly = lf.y;

    float flicker = 0.90f + 0.10f * PFMath::fastSin(t * 1.8f + lf.seed * 2.0f);
    float val = lf.tone * flicker;
    drawChunkyLeaf(lx, ly, lf.size, val);
  }

  int activeDrifterCount = PFMath::ifloor((float)drifterCount * foliage);
  for (int d = 0; d < activeDrifterCount; d++) {
    const Drifter& df = drifters[d];
    float fallDistance = 118.0f - df.originY;
    float cycleTime = fallDistance / df.fallRate;
    float localTime = PFMath::jsMod(t + df.phaseOffset, cycleTime);
    float prog = localTime / cycleTime;
    float ly = df.originY + prog * fallDistance;

    if (ly >= df.originY && ly < 122.0f) {
      float windPush = prog * windDir * 28.0f;
      float flutter = PFMath::fastSin(prog * TWO_PI * 2.0f + df.wobbleSeed) * 2.2f;
      float lx = df.originX + windPush + flutter;
      lx = PFMath::jsMod(PFMath::jsMod(lx, (float)FRAME_W) + (float)FRAME_W, (float)FRAME_W);

      float birthFade = prog * 8.0f;
      if (birthFade > 1.0f) birthFade = 1.0f;
      float groundDiff = ly - 98.0f;
      if (groundDiff < 0.0f) groundDiff = 0.0f;
      float groundFade = 1.0f - groundDiff / 22.0f;
      if (groundFade < 0.0f) groundFade = 0.0f;
      float alpha = birthFade * groundFade;

      if (alpha > 0.15f) {
        float tumble = PFMath::fastSin(t * 3.5f + df.seed * 2.5f);
        if (tumble < 0.0f) tumble = -tumble;
        float flakeRad = 0.8f + tumble * 0.6f;
        float shimmer = 0.88f + 0.12f * PFMath::fastSin(t * 4.0f + df.seed * 3.0f);
        drawChunkyLeaf(lx, ly, flakeRad, shimmer);
      }
    }
  }

  float absWindDir = windDir < 0.0f ? -windDir : windDir;
  if (absWindDir > 0.2f) {
    int activeNeighborCount = PFMath::ifloor((float)neighborDrifterCount * foliage);
    for (int j = 0; j < activeNeighborCount; j++) {
      const NeighborDrifter& nd = neighborDrifters[j];
      float fallDistance = 120.0f - nd.startY;
      float cycleTime = fallDistance / nd.fallRate;
      float localTime = PFMath::jsMod(t + nd.phaseOffset, cycleTime);
      float prog = localTime / cycleTime;
      float ly = nd.startY + prog * fallDistance;

      if (ly < 122.0f) {
        float startX = (windDir > 0.0f) ? -10.0f : ((float)FRAME_W + 10.0f);
        float windPush = prog * windDir * 38.0f;
        float flutter = PFMath::fastSin(prog * PI * 5.0f + nd.seed) * 2.0f;
        float lx = startX + windPush + flutter;

        if (lx >= -2.0f && lx < (float)FRAME_W + 2.0f) {
          float tumble = PFMath::fastCos(t * 3.0f + nd.seed * 2.0f);
          if (tumble < 0.0f) tumble = -tumble;
          float flakeRad = 0.7f + tumble * 0.55f;
          float shimmer = 0.86f + 0.14f * PFMath::fastSin(t * 3.8f + nd.seed);
          drawChunkyLeaf(lx, ly, flakeRad, shimmer);
        }
      }
    }
  }

  for (int y = 0; y < FRAME_H; y++) {
    int rowOffset = y * FRAME_W;
    for (int x = 0; x < FRAME_W; x++) {
      float v = PFMath::clamp01(valField[rowOffset + x]);
      int li = (int)(v * 255.0f + 0.5f);
      PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
    }
  }

  PFCanvas::present();
}

} // namespace BAKWAN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
