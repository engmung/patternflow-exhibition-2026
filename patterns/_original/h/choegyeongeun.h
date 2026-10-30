#pragma once

// ===== Patternflow pattern =====
// Title:    CHOEGYEONGEUN
// Author:   @engmung
// Date:     2026-09-18
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

#include <Arduino.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"

namespace CHOEGYEONGEUN {

const char* NAME = "CHOEGYEONGEUN";
const char* const KNOB_LABELS[4] = {"DoorOpen", "Zoom", "FlowerSpeed", "-"};
constexpr bool ABSOLUTE_READY = true;

// Pre-baked ramp LUT (verbatim from specification)
static const uint8_t RAMP_LUT[256][3] = {
  {0,0,0},{3,0,0},{20,0,0},{42,0,0},{66,0,0},{92,0,0},{119,0,0},{147,0,0},
  {176,0,0},{204,6,4},{199,45,40},{193,65,60},{187,79,78},{180,92,93},{173,103,109},{165,112,123},
  {156,122,138},{146,130,152},{135,138,167},{121,146,181},{109,153,193},{125,159,187},{141,165,181},{156,170,174},
  {171,175,166},{186,180,158},{201,185,149},{216,190,139},{231,194,128},{246,199,115},{252,201,113},{247,201,125},
  {241,201,136},{234,201,147},{228,201,157},{222,201,166},{216,200,175},{210,200,184},{204,199,193},{196,195,195},
  {186,180,179},{176,165,163},{165,150,147},{155,135,131},{144,120,116},{134,106,101},{124,92,87},{113,78,72},
  {103,64,59},{92,51,45},{81,38,32},{71,24,19},{60,10,6},{51,0,0},{53,1,0},{55,2,0},
  {57,3,0},{59,5,0},{61,6,0},{63,7,0},{65,9,0},{67,11,0},{69,13,1},{71,14,1},
  {72,16,1},{74,18,1},{76,19,1},{78,21,1},{80,22,1},{82,24,1},{84,26,1},{86,27,2},
  {88,29,2},{90,30,2},{92,32,2},{94,34,2},{96,35,3},{98,37,3},{100,38,3},{102,40,3},
  {104,42,3},{106,43,4},{108,45,4},{110,46,4},{113,48,4},{115,50,5},{117,51,5},{119,53,5},
  {121,55,6},{123,56,6},{125,58,6},{127,59,7},{129,61,7},{131,63,8},{133,64,8},{135,66,8},
  {138,68,9},{140,69,9},{142,71,10},{144,73,10},{146,74,11},{148,76,11},{150,78,12},{153,79,12},
  {155,81,13},{157,83,13},{159,85,14},{161,86,14},{163,88,15},{166,90,15},{168,91,16},{170,93,16},
  {172,95,17},{174,97,17},{177,98,18},{179,100,18},{181,102,19},{183,104,19},{185,105,20},{188,107,20},
  {190,109,21},{192,111,21},{194,113,22},{197,114,22},{199,116,23},{201,118,23},{203,120,24},{206,121,24},
  {210,119,23},{213,117,21},{217,115,20},{221,113,19},{224,111,17},{228,109,16},{231,107,14},{235,105,12},
  {238,102,10},{242,100,8},{245,97,6},{249,94,4},{252,91,2},{255,93,3},{255,109,17},{255,123,27},
  {255,136,34},{255,148,41},{255,160,47},{255,171,51},{255,182,55},{255,192,58},{255,203,59},{255,213,57},
  {255,224,47},{255,234,0},{255,228,57},{255,223,79},{255,217,93},{255,211,104},{255,206,113},{255,200,120},
  {255,194,127},{255,188,132},{255,182,138},{255,176,143},{255,170,147},{255,164,152},{255,157,156},{255,151,160},
  {255,144,164},{255,137,167},{255,129,171},{255,121,174},{255,113,178},{255,103,181},{255,94,185},{255,82,188},
  {255,69,192},{255,53,195},{255,28,198},{252,10,200},{247,24,201},{242,32,202},{237,39,203},{232,44,204},
  {227,49,205},{222,52,206},{217,56,207},{211,59,208},{206,61,208},{201,63,209},{196,66,210},{191,67,211},
  {186,69,212},{180,70,212},{175,72,213},{170,73,214},{165,74,215},{159,75,216},{154,75,216},{149,76,217},
  {143,77,218},{138,77,219},{133,77,219},{127,77,220},{122,78,221},{116,78,221},{110,77,222},{104,77,223},
  {98,77,223},{92,77,224},{86,76,225},{79,76,225},{72,75,226},{65,74,227},{57,73,227},{48,72,228},
  {38,71,229},{24,70,229},{1,69,230},{9,76,231},{18,82,233},{26,89,234},{34,95,235},{41,101,236},
  {48,106,237},{54,112,239},{61,117,240},{68,123,241},{74,128,242},{81,133,243},{87,138,244},{94,143,245},
  {100,148,246},{107,153,247},{113,158,247},{120,163,248},{126,167,249},{133,172,250},{139,177,250},{146,182,251},
  {153,186,252},{159,191,252},{166,196,253},{173,200,253},{179,205,253},{186,210,254},{193,214,254},{200,219,254},
  {207,223,255},{213,228,255},{220,232,255},{227,237,255},{234,241,255},{241,246,255},{248,250,255},{255,255,255},
};

static float PETAL_SHAPE_LUT[4][256];

inline void writeValue(int x, int y, float v) {
  if (x < 0 || x >= PANEL_RES_W || y < 0 || y >= PANEL_RES_H) return;
  float cv = PFMath::clamp01(v);
  int li = (int)(cv * 255.0f + 0.5f);
  PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
}

inline void writePixelValue(int x, int y, float v) {
  float cv = PFMath::clamp01(v);
  int li = (int)(cv * 255.0f + 0.5f);
  PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
}

struct Petal {
  float baseAngle;
  float cosA;
  float sinA;
  float offset;
  float length;
  float invSpan;
  float width;
  bool pointed;
  float shade;
  bool notch;
};

struct Flower {
  float baseR;
  float invR;
  float maxPetalDistSq;
  float depth;
  float phase;
  float phaseY;
  float turn;
  float turnSpeed;
  float speedMult;
  float driftFreq;
  float driftAmp;
  float progressX;
  float progressY;
  float x;
  float y;
  bool loose;
  int colorTier;
  bool hasLeaf;
  float leafAngle;
  int petalCount;
  Petal petals[16];
};

static constexpr int SCENE_COUNTS[4] = {6, 5, 6, 7};
static Flower scenes[4][7];

// Dynamic parameter states
static float knobDoorOpen = 1.0f;
static float knobZoom = 0.8f;
static float knobFlowerSpeed = 8.0f;
static float knobUnused = 1.0f;

static float doorOpen = 1.0f;
static float zoomBase = 0.8f;
static float zoom = 0.8f;
static float flowerSpeed = 8.0f;
static float doorVelocity = 0.0f;
static float zoomVelocity = 0.0f;
static constexpr float DOOR_RESPONSE = 12.0f;
static constexpr float ZOOM_RESPONSE = 9.0f;

static int sceneIndex = 0;
static bool canSwitch = false;
static float animTime = 0.0f;

static inline float flowerHash(float n) {
  float v = sinf(n * 127.1f + 31.7f) * 43758.5453f;
  return PFMath::fract(v);
}

static inline void dampCabinet(float& val, float& vel, float target, float response, float dt, float minV, float maxV) {
  float offset = val - target;
  float j = vel + response * offset;
  float decay = expf(-response * dt);
  float nextVal = target + (offset + j * dt) * decay;
  float nextVel = (vel - response * j * dt) * decay;

  if (nextVal < minV || nextVal > maxV) {
    nextVal = PFMath::clamp(nextVal, minV, maxV);
    nextVel = 0.0f;
  }
  if (fabsf(nextVal - target) < 1e-4f && fabsf(nextVel) < 1e-3f) {
    nextVal = target;
    nextVel = 0.0f;
  }
  val = nextVal;
  vel = nextVel;
}

static inline float woodTexture(float ox, float oy, float seed) {
  return (PFMath::fastSin(ox * 1.8f + PFMath::fastCos(oy * 0.15f + seed) * 3.2f) * 0.5f +
          PFMath::fastSin(ox * 3.4f + oy * 0.5f + seed * 2.0f) * 0.25f) * 0.05f;
}

void setup() {
  PFMath::buildSinLUT();

  for (int i = 0; i < 256; i++) {
    float normX = (float)i / 255.0f;
    PETAL_SHAPE_LUT[0][i] = sqrtf(normX) * (1.0f - normX * 0.95f);
    PETAL_SHAPE_LUT[1][i] = sinf(normX * 3.14159f) * (1.0f - normX * 0.4f);
    PETAL_SHAPE_LUT[2][i] = sinf(normX * 3.14159f) * (1.1f - normX * 0.3f);
    PETAL_SHAPE_LUT[3][i] = sqrtf(normX) * (1.0f - normX * 0.95f);
  }

  doorOpen = 1.0f;
  zoomBase = 0.8f;
  zoom = 0.8f;
  flowerSpeed = 8.0f;
  doorVelocity = 0.0f;
  zoomVelocity = 0.0f;
  sceneIndex = 0;
  canSwitch = false;
  animTime = 0.0f;

  for (int s = 0; s < 4; s++) {
    int count = SCENE_COUNTS[s];
    for (int i = 0; i < count; i++) {
      Flower& f = scenes[s][i];
      float seed = (float)(i + s * 37);
      f.depth = 0.55f + flowerHash(seed + 3.0f) * 0.45f;

      const float baseRadii[4] = {5.3f, 5.5f, 5.2f, 5.0f};
      float randomScale = 1.9f + flowerHash(seed + 16.0f) * 1.0f;
      f.baseR = baseRadii[s] * (0.88f + f.depth * 0.22f) * randomScale;
      f.invR = 1.0f / f.baseR;
      f.maxPetalDistSq = f.baseR * f.baseR * 0.82f;

      float normX = ((float)i + 0.5f + (flowerHash(seed + 1.0f) - 0.5f) * 0.5f) / (float)count;
      float normY = PFMath::fract((float)i * 0.618033f + flowerHash(seed + 2.0f) * 0.3f);

      f.phase = flowerHash(seed + 8.0f) * 6.28318f;
      f.phaseY = flowerHash(seed + 14.0f) * 6.28318f;
      f.turn = flowerHash(seed + 9.0f) * 6.28318f;
      f.turnSpeed = (flowerHash(seed + 10.0f) - 0.5f) * (0.35f + f.depth * 0.45f);

      f.speedMult = 0.8f + flowerHash(seed + 13.0f) * 0.4f;
      f.driftFreq = 0.32f + flowerHash(seed + 21.0f) * 0.25f;
      f.driftAmp = 1.2f + flowerHash(seed + 22.0f) * 1.5f;

      f.progressX = normX;
      f.progressY = normY;
      f.x = 44.0f + normX * 39.0f;
      f.y = 12.0f + normY * 39.0f;

      f.loose = (s == 3 && (i % 3 == 0));
      f.colorTier = s % 5;
      f.hasLeaf = (i % 2 == 1);
      f.leafAngle = flowerHash(seed + 15.0f) * 6.28318f;
      f.petalCount = 0;

      auto addWhorl = [&](int n, float r, float length, float width, float phase, bool pointed) {
        for (int k = 0; k < n; k++) {
          if (f.petalCount >= 16) break;
          Petal& p = f.petals[f.petalCount++];
          p.baseAngle = (float)k * 6.28318f / (float)n + phase;
          p.cosA = 1.0f;
          p.sinA = 0.0f;
          float variation = 0.95f + 0.1f * flowerHash(seed * 31.0f + (float)k + r * 17.0f);
          p.length = r * length * variation;
          p.offset = r * 0.42f;
          p.invSpan = 1.0f / (p.length + p.offset);
          p.width = r * width;
          p.pointed = pointed;
          p.shade = flowerHash((float)k + seed * 7.0f) * 0.015f;
          p.notch = (s == 3 && !f.loose);
        }
      };

      if (s == 0) {
        addWhorl(5, 1.0f, 0.62f, 0.38f, 0.1f, false);
        addWhorl(5, 0.72f, 0.58f, 0.36f, 0.72f, false);
      } else if (s == 1) {
        addWhorl(3, 1.0f, 0.75f, 0.28f, 0.0f, true);
        addWhorl(3, 0.9f, 0.70f, 0.28f, 1.047f, true);
      } else if (s == 2) {
        addWhorl(8, 1.0f, 0.68f, 0.42f, 0.0f, false);
        addWhorl(8, 0.72f, 0.55f, 0.38f, 0.392f, false);
      } else {
        if (f.loose) {
          addWhorl(1, 1.0f, 0.72f, 0.44f, 0.0f, false);
        } else {
          addWhorl(5, 1.0f, 0.66f, 0.40f, 0.0f, false);
        }
      }
    }

    for (int i = 0; i < count - 1; i++) {
      for (int j = i + 1; j < count; j++) {
        if (scenes[s][i].depth > scenes[s][j].depth) {
          Flower tmp = scenes[s][i];
          scenes[s][i] = scenes[s][j];
          scenes[s][j] = tmp;
        }
      }
    }
  }
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &knobDoorOpen, 0.0f, 1.0f, 0.021f);
  PFParams::apply(input, 1, &knobZoom, 0.8f, 2.5f, 0.035f);
  PFParams::apply(input, 2, &knobFlowerSpeed, 0.2f, 8.0f, 0.163f);
  PFParams::apply(input, 3, &knobUnused, 0.0f, 1.0f, 0.021f);

  float cdt = PFMath::clamp(dt, 0.0f, 0.1f);
  float openTarget = PFMath::clamp01(knobDoorOpen);
  float zoomTarget = PFMath::clamp(knobZoom, 0.8f, 2.5f);
  float speedTarget = PFMath::clamp(knobFlowerSpeed, 0.2f, 8.0f);

  dampCabinet(doorOpen, doorVelocity, openTarget, DOOR_RESPONSE, cdt, 0.0f, 1.0f);
  dampCabinet(zoomBase, zoomVelocity, zoomTarget, ZOOM_RESPONSE, cdt, 0.8f, 2.5f);
  flowerSpeed += (speedTarget - flowerSpeed) * (1.0f - expf(-cdt * 5.0f));

  float simDt = cdt * flowerSpeed;
  animTime += simDt;
  if (animTime > 1256.637f) {
    animTime -= 1256.637f;
  }

  float zoomT = PFMath::clamp01((zoomBase - 0.8f) / (2.5f - 0.8f));
  float breathAmp = 0.008f + PFMath::fastPow(zoomT, 1.4f) * 0.047f;
  float breath = PFMath::fastSin(animTime * 0.95f) * breathAmp;
  zoom = PFMath::clamp(zoomBase + breath, 0.78f, 2.6f);

  if (doorOpen < 0.05f) {
    if (canSwitch) {
      sceneIndex = (sceneIndex + 1) % 4;
      canSwitch = false;
    }
  } else if (doorOpen > 0.38f) {
    canSwitch = true;
  }

  int s = sceneIndex;
  float t = animTime;
  constexpr float xMin = 44.0f, yMin = 12.0f;
  constexpr float spanX = 39.0f, spanY = 39.0f;

  int count = SCENE_COUNTS[s];
  for (int i = 0; i < count; i++) {
    Flower& f = scenes[s][i];
    float margin = f.baseR + 6.0f;
    f.turn += simDt * f.turnSpeed * 1.5f;
    if (f.turn > TWO_PI) f.turn -= TWO_PI;
    else if (f.turn < -TWO_PI) f.turn += TWO_PI;

    for (int pIdx = 0; pIdx < f.petalCount; pIdx++) {
      Petal& p = f.petals[pIdx];
      float ang = p.baseAngle + f.turn;
      p.cosA = PFMath::fastCos(ang);
      p.sinA = PFMath::fastSin(ang);
    }

    if (s == 0) {
      float flowRate = (0.05f * f.speedMult) * simDt;
      f.progressY = PFMath::fract(f.progressY - flowRate + 1.0f);
      f.x = xMin + f.progressX * spanX + PFMath::fastSin(t * f.driftFreq + f.phase) * f.driftAmp;
      f.y = (yMin - margin) + f.progressY * (spanY + margin * 2.0f);
    } else if (s == 1) {
      float flowRate = (0.055f * f.speedMult) * simDt;
      f.progressX = PFMath::fract(f.progressX + flowRate);
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2.0f);
      f.y = yMin + f.progressY * spanY + PFMath::fastSin(t * f.driftFreq + f.phaseY) * f.driftAmp;
    } else if (s == 2) {
      float orbitSpeed = (0.24f * f.speedMult) * simDt;
      f.phase += orbitSpeed;
      if (f.phase > TWO_PI) f.phase -= TWO_PI;
      f.phaseY += orbitSpeed * 0.75f;
      if (f.phaseY > TWO_PI) f.phaseY -= TWO_PI;
      f.x = 63.5f + PFMath::fastSin(f.phase) * (14.0f * (0.7f + f.depth * 0.3f));
      f.y = 31.5f + PFMath::fastCos(f.phaseY) * (13.0f * (0.7f + f.depth * 0.3f));
    } else {
      float flowRateX = (0.045f * f.speedMult) * simDt;
      float flowRateY = (0.07f * f.speedMult) * simDt;
      f.progressX = PFMath::fract(f.progressX + flowRateX);
      f.progressY = PFMath::fract(f.progressY + flowRateY);
      float sway = f.loose ? 3.2f : 1.8f;
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2.0f) + PFMath::fastSin(t * 1.2f * f.driftFreq + f.phase) * sway;
      f.y = (yMin - margin) + f.progressY * (spanY + margin * 2.0f);
    }
  }
}

void draw() {
  const int w = PANEL_RES_W;
  const int h = PANEL_RES_H;
  const float openK = doorOpen;
  const float z = zoom;
  const int s = sceneIndex;

  constexpr float cx = 63.5f;
  constexpr float cy = 31.5f;
  const float invZ = 1.0f / z;

  // 1. Frame boundaries
  const int boxLeft = PFMath::ifloor(cx + (38.0f - cx) * z);
  const int boxRight = PFMath::ifloor(cx + (89.0f - cx) * z + 0.999f);
  const int boxTop = PFMath::ifloor(cy + (6.0f - cy) * z);
  const int boxBottom = PFMath::ifloor(cy + (57.0f - cy) * z + 0.999f);

  const int innerLeft = (int)(cx + (44.0f - cx) * z + (cx + (44.0f - cx) * z >= 0 ? 0.5f : -0.5f));
  const int innerRight = (int)(cx + (83.0f - cx) * z + (cx + (83.0f - cx) * z >= 0 ? 0.5f : -0.5f));
  const int innerTop = (int)(cy + (12.0f - cy) * z + (cy + (12.0f - cy) * z >= 0 ? 0.5f : -0.5f));
  const int innerBottom = (int)(cy + (51.0f - cy) * z + (cy + (51.0f - cy) * z >= 0 ? 0.5f : -0.5f));

  // 2. Clear outer boundary to pure black (rectangular block clears)
  if (boxTop > 0) {
    int topLim = PFMath::clamp(boxTop, 0, h);
    for (int y = 0; y < topLim; y++) {
      for (int x = 0; x < w; x++) PFCanvas::setPixel(x, y, 0, 0, 0);
    }
  }
  if (boxBottom < h - 1) {
    int botLim = PFMath::clamp(boxBottom + 1, 0, h);
    for (int y = botLim; y < h; y++) {
      for (int x = 0; x < w; x++) PFCanvas::setPixel(x, y, 0, 0, 0);
    }
  }
  int yMid0 = PFMath::clamp(boxTop, 0, h - 1);
  int yMid1 = PFMath::clamp(boxBottom, 0, h - 1);
  if (boxLeft > 0) {
    int leftLim = PFMath::clamp(boxLeft, 0, w);
    for (int y = yMid0; y <= yMid1; y++) {
      for (int x = 0; x < leftLim; x++) PFCanvas::setPixel(x, y, 0, 0, 0);
    }
  }
  if (boxRight < w - 1) {
    int rightLim = PFMath::clamp(boxRight + 1, 0, w);
    for (int y = yMid0; y <= yMid1; y++) {
      for (int x = rightLim; x < w; x++) PFCanvas::setPixel(x, y, 0, 0, 0);
    }
  }

  // 3. Cabinet interior backing
  const int yMinInner = PFMath::clamp(innerTop, 0, h - 1);
  const int yMaxInner = PFMath::clamp(innerBottom, 0, h - 1);
  const int xMinInner = PFMath::clamp(innerLeft, 0, w - 1);
  const int xMaxInner = PFMath::clamp(innerRight, 0, w - 1);

  constexpr float bgSlots[4] = {0.04f, 0.08f, 0.12f, 0.16f};
  const int bgIdx = (int)(bgSlots[s % 4] * 255.0f + 0.5f);
  const uint8_t bgR = RAMP_LUT[bgIdx][0], bgG = RAMP_LUT[bgIdx][1], bgB = RAMP_LUT[bgIdx][2];
  const int edgeIdx = (int)(0.20f * 255.0f + 0.5f);
  const uint8_t edgeR = RAMP_LUT[edgeIdx][0], edgeG = RAMP_LUT[edgeIdx][1], edgeB = RAMP_LUT[edgeIdx][2];

  for (int py = yMinInner; py <= yMaxInner; py++) {
    bool isTopBottomEdge = (py == innerTop || py == innerBottom);
    for (int px = xMinInner; px <= xMaxInner; px++) {
      if (isTopBottomEdge || px == innerLeft || px == innerRight) {
        PFCanvas::setPixel(px, py, edgeR, edgeG, edgeB);
      } else {
        PFCanvas::setPixel(px, py, bgR, bgG, bgB);
      }
    }
  }

  // 4. Rasterize flowers (Zoom optimized: tight reach bounds + shape LUT)
  constexpr float tierBases[5] = {0.55f, 0.64f, 0.74f, 0.82f, 0.91f};
  const Flower* flowerList = scenes[s];
  const int flowerCount = SCENE_COUNTS[s];
  const float* curShapeLUT = PETAL_SHAPE_LUT[s];

  for (int i = 0; i < flowerCount; i++) {
    const Flower& f = flowerList[i];
    const float baseTone = tierBases[f.colorTier];

    const float screenFX = cx + (f.x - cx) * z;
    const float screenFY = cy + (f.y - cy) * z;
    const float maxReach = f.hasLeaf ? (f.baseR + 3.4f) : (f.baseR * 0.92f);
    const float screenR = maxReach * z + 1.0f;

    const int minPX = PFMath::clamp(PFMath::ifloor(screenFX - screenR), xMinInner, xMaxInner);
    const int maxPX = PFMath::clamp(PFMath::ifloor(screenFX + screenR + 0.999f), xMinInner, xMaxInner);
    const int minPY = PFMath::clamp(PFMath::ifloor(screenFY - screenR), yMinInner, yMaxInner);
    const int maxPY = PFMath::clamp(PFMath::ifloor(screenFY + screenR + 0.999f), yMinInner, yMaxInner);

    if (minPX > maxPX || minPY > maxPY) continue;

    const bool hasLeaf = f.hasLeaf;
    const float lx = f.x + PFMath::fastCos(f.leafAngle) * (f.baseR + 1.4f);
    const float ly = f.y + PFMath::fastSin(f.leafAngle) * (f.baseR + 1.4f);

    for (int py = minPY; py <= maxPY; py++) {
      const float oy = (float)(py - cy) * invZ + cy;
      const float dy = oy - f.y;
      const float dySq = dy * dy;

      float ldy = (oy - ly) * 1.4f;
      float ldySq = ldy * ldy;
      bool leafRowPossible = hasLeaf && (ldySq <= 4.0f);
      bool stamenRowPossible = (dySq <= 1.8f);

      // Skip entire row if beyond maximum petal radius and cannot intersect leaf
      if (dySq > f.maxPetalDistSq && !leafRowPossible) continue;

      const float v = dy * f.invR;

      for (int px = minPX; px <= maxPX; px++) {
        const float ox = (float)(px - cx) * invZ + cx;
        const float dx = ox - f.x;
        const float distSq = dx * dx + dySq;

        // Center Core Stamen
        if (stamenRowPossible && distSq <= 1.8f) {
          writePixelValue(px, py, 0.99f);
          continue;
        }

        // Leaf Foliage
        if (leafRowPossible) {
          float ldx = ox - lx;
          if (ldx * ldx + ldySq <= 4.0f) {
            writePixelValue(px, py, 0.52f);
          }
        }

        // Beyond petal reach, skip petal tests
        if (distSq > f.maxPetalDistSq) continue;

        const float u = dx * f.invR;

        // Petal loop with precomputed LUT
        for (int pIdx = 0; pIdx < f.petalCount; pIdx++) {
          const Petal& p = f.petals[pIdx];
          float nx = u * p.cosA + v * p.sinA;

          if (nx > -p.offset && nx < p.length) {
            float ny = -u * p.sinA + v * p.cosA;
            float absNY = fabsf(ny);
            if (absNY >= p.width) continue;

            float normX = (nx + p.offset) * p.invSpan;
            int lutIdx = (int)(normX * 255.0f);
            lutIdx = PFMath::clamp(lutIdx, 0, 255);
            float yBound = p.width * curShapeLUT[lutIdx];

            if (absNY < yBound) {
              if (p.notch && normX > 0.78f && absNY < yBound * 0.35f) {
                continue;
              }
              float shading = (1.0f - normX) * 0.03f + p.shade;
              writePixelValue(px, py, baseTone + shading);
              break;
            }
          }
        }
      }
    }
  }

  // 5. Cabinet posts and lintel/sill beams (Row-hoisted trig)
  const int yPostMin = PFMath::clamp(PFMath::ifloor(cy + (2.0f - cy) * z), 0, h - 1);
  const int yPostMax = PFMath::clamp(PFMath::ifloor(cy + (61.0f - cy) * z + 0.999f), 0, h - 1);

  const int xL0 = PFMath::clamp(boxLeft, 0, w - 1);
  const int xL1 = PFMath::clamp(innerLeft - 1, 0, w - 1);
  const int xR0 = PFMath::clamp(innerRight + 1, 0, w - 1);
  const int xR1 = PFMath::clamp(boxRight, 0, w - 1);

  for (int py = yPostMin; py <= yPostMax; py++) {
    const float oy = (float)(py - cy) * invZ + cy;
    float cosY_L = PFMath::fastCos(oy * 0.15f + 1.2f) * 3.2f;
    float phaseY_L = oy * 0.5f + 2.4f;

    for (int px = xL0; px <= xL1; px++) {
      const float ox = (float)(px - cx) * invZ + cx;
      float grain = (PFMath::fastSin(ox * 1.8f + cosY_L) * 0.5f + PFMath::fastSin(ox * 3.4f + phaseY_L) * 0.25f) * 0.05f;
      writePixelValue(px, py, PFMath::clamp(0.35f + grain, 0.10f, 0.48f));
    }

    float cosY_R = PFMath::fastCos(oy * 0.15f + 2.7f) * 3.2f;
    float phaseY_R = oy * 0.5f + 5.4f;

    for (int px = xR0; px <= xR1; px++) {
      const float ox = (float)(px - cx) * invZ + cx;
      float grain = (PFMath::fastSin(ox * 1.8f + cosY_R) * 0.5f + PFMath::fastSin(ox * 3.4f + phaseY_R) * 0.25f) * 0.05f;
      writePixelValue(px, py, PFMath::clamp(0.34f + grain, 0.10f, 0.48f));
    }
  }

  // Top lintel beam
  const int xBeam0 = PFMath::clamp(innerLeft, 0, w - 1);
  const int xBeam1 = PFMath::clamp(innerRight, 0, w - 1);
  const int yTop0 = PFMath::clamp(boxTop, 0, h - 1);
  const int yTop1 = PFMath::clamp(innerTop, 0, h - 1);

  for (int py = yTop0; py <= yTop1; py++) {
    const float oy = (float)(py - cy) * invZ + cy;
    bool isEdge = (py == boxTop);
    float baseTone = isEdge ? 0.46f : 0.36f;
    float cosY_Beam = PFMath::fastCos((oy * 1.5f) * 0.15f + 3.5f) * 3.2f;
    float phaseY_Beam = (oy * 1.5f) * 0.5f + 7.0f;

    for (int px = xBeam0; px <= xBeam1; px++) {
      const float ox05 = ((float)(px - cx) * invZ + cx) * 0.5f;
      float grain = (PFMath::fastSin(ox05 * 1.8f + cosY_Beam) * 0.5f + PFMath::fastSin(ox05 * 3.4f + phaseY_Beam) * 0.25f) * 0.05f;
      writePixelValue(px, py, PFMath::clamp(baseTone + grain, 0.12f, 0.48f));
    }
  }

  // Bottom threshold beam
  const int yBot0 = PFMath::clamp(innerBottom, 0, h - 1);
  const int yBot1 = PFMath::clamp(boxBottom, 0, h - 1);

  for (int py = yBot0; py <= yBot1; py++) {
    const float oy = (float)(py - cy) * invZ + cy;
    float cosY_Bot = PFMath::fastCos((oy * 1.5f) * 0.15f + 4.8f) * 3.2f;
    float phaseY_Bot = (oy * 1.5f) * 0.5f + 9.6f;

    for (int px = xBeam0; px <= xBeam1; px++) {
      const float ox05 = ((float)(px - cx) * invZ + cx) * 0.5f;
      float grain = (PFMath::fastSin(ox05 * 1.8f + cosY_Bot) * 0.5f + PFMath::fastSin(ox05 * 3.4f + phaseY_Bot) * 0.25f) * 0.05f;
      writePixelValue(px, py, PFMath::clamp(0.30f + grain, 0.10f, 0.46f));
    }
  }

  // Post corner notch details
  const int jointCorners[4][2] = {
    {boxLeft + (int)(1.0f * z + 0.5f), PFMath::ifloor(cy + (3.0f - cy) * z)},
    {boxRight - (int)(2.0f * z + 0.5f), PFMath::ifloor(cy + (3.0f - cy) * z)},
    {boxLeft + (int)(1.0f * z + 0.5f), PFMath::ifloor(cy + (60.0f - cy) * z + 0.999f)},
    {boxRight - (int)(2.0f * z + 0.5f), PFMath::ifloor(cy + (60.0f - cy) * z + 0.999f)}
  };
  for (int c = 0; c < 4; c++) {
    int jx = jointCorners[c][0];
    int jy = jointCorners[c][1];
    writeValue(jx, jy, 0.10f);
    writeValue(jx + 1, jy, 0.10f);
  }

  // 6. Hinged double doors
  const int hingeLeft = innerLeft;
  const int hingeRight = innerRight;
  const float doorWidth = 20.8f * z;

  const float angle = openK * 2.89f;
  const float cosA = PFMath::fastCos(angle);
  const float sinA = PFMath::fastSin(angle);

  const float leftTipX = (float)hingeLeft + doorWidth * cosA;
  const float rightTipX = (float)hingeRight - doorWidth * cosA;

  auto drawDoorPanel = [&](float xStart, float xEnd, float tipX, bool isLeft) {
    int x0 = PFMath::clamp(PFMath::ifloor(fminf(xStart, xEnd)), 0, w - 1);
    int x1 = PFMath::clamp(PFMath::ifloor(fmaxf(xStart, xEnd) + 0.999f), 0, w - 1);
    int span = x1 - x0;
    if (span < 1) span = 1;

    bool isBackFace = (cosA < 0.0f);
    float seedVal = isLeft ? 5.7f : 8.9f;

    for (int px = x0; px <= x1; px++) {
      float ox = (float)(px - cx) * invZ + cx;
      float u = (float)(px - x0) / (float)span;

      float splinterBot = (float)innerBottom + 3.5f * z + PFMath::fastSin(ox * 1.8f) * (1.2f * z);
      float splinterTop = (float)innerTop - 2.0f * z + PFMath::fastSin(ox * 1.3f) * (0.8f * z);

      int pyMin = PFMath::clamp(PFMath::ifloor(splinterTop), 0, h - 1);
      int pyMax = PFMath::clamp(PFMath::ifloor(splinterBot + 0.999f), 0, h - 1);

      float baseTone = isBackFace ? 0.40f : 0.36f;
      if (PFMath::ifloor(u * 3.0f) != PFMath::ifloor((u + 0.08f) * 3.0f)) {
        baseTone -= 0.10f;
      }
      if (fabsf((float)px - tipX) < 1.4f * z) {
        baseTone += 0.07f * sinA;
      }

      for (int py = pyMin; py <= pyMax; py++) {
        float oy = (float)(py - cy) * invZ + cy;
        float doorTone = baseTone + woodTexture(ox * 2.0f, oy * 0.8f, seedVal);

        bool isBatten = (oy >= 21.0f && oy <= 24.0f) || (oy >= 40.0f && oy <= 43.0f);
        if (isBatten) {
          doorTone += 0.07f;
          if (oy > 23.0f || oy > 42.0f) doorTone -= 0.10f;
        }

        writePixelValue(px, py, PFMath::clamp(doorTone, 0.10f, 0.48f));
      }
    }
  };

  if (fabsf(leftTipX - (float)hingeLeft) > 0.8f) {
    drawDoorPanel((float)hingeLeft, leftTipX, leftTipX, true);
  }
  if (fabsf(rightTipX - (float)hingeRight) > 0.8f) {
    drawDoorPanel((float)hingeRight, rightTipX, rightTipX, false);
  }

  // Metal hinge pins
  const int hingeY1 = (int)(cy + (18.0f - cy) * z + 0.5f);
  const int hingeY2 = (int)(cy + (45.0f - cy) * z + 0.5f);
  const int hinges[4][2] = {
    {innerLeft - 1, hingeY1}, {innerLeft - 1, hingeY2},
    {innerRight + 1, hingeY1}, {innerRight + 1, hingeY2}
  };
  for (int hg = 0; hg < 4; hg++) {
    int hx = hinges[hg][0];
    int hy = hinges[hg][1];
    writeValue(hx, hy - 1, 0.08f);
    writeValue(hx, hy, 0.46f);
    writeValue(hx, hy + 1, 0.08f);
  }

  PFCanvas::present();
}

} // namespace CHOEGYEONGEUN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
