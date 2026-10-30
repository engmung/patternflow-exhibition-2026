#pragma once

// ===== Patternflow pattern =====
// Title:    YUJIN
// Author:   @engmung
// Date:     2026-09-17
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

namespace YUJIN {

const char* NAME = "YUJIN";
const char* const KNOB_LABELS[4] = {"FishCount", "SwimSpeed", "CurrentLoop", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;

static const uint8_t RAMP_LUT[256][3] = {
  {0,61,158},{1,63,159},{2,65,160},{4,67,161},{5,69,162},{7,71,163},{9,72,164},{11,74,165},
  {13,76,166},{15,78,167},{16,80,168},{18,82,169},{20,84,170},{22,85,171},{23,87,172},{25,89,173},
  {27,91,174},{29,93,175},{30,94,176},{32,96,177},{34,98,178},{35,100,179},{37,101,180},{39,103,181},
  {40,105,182},{42,107,183},{44,109,184},{45,110,185},{47,112,185},{48,114,186},{50,116,187},{52,117,188},
  {53,119,189},{55,121,190},{57,123,191},{58,124,192},{60,126,193},{62,128,194},{63,130,195},{65,131,196},
  {67,133,197},{68,135,198},{70,137,199},{72,138,200},{73,140,200},{75,142,201},{77,144,202},{78,146,203},
  {80,147,204},{82,149,205},{83,151,206},{85,153,207},{87,154,208},{88,156,209},{90,158,210},{92,160,210},
  {93,161,211},{95,163,212},{97,165,213},{98,167,214},{100,168,215},{102,170,216},{104,172,217},{105,174,218},
  {107,176,218},{109,177,219},{110,179,220},{112,181,221},{114,183,222},{116,184,223},{117,186,224},{119,188,225},
  {121,190,225},{123,192,226},{124,193,227},{126,195,228},{128,197,229},{130,199,230},{131,201,231},{133,202,232},
  {135,204,232},{137,206,233},{138,208,234},{140,210,235},{142,211,236},{144,213,237},{145,215,237},{147,217,238},
  {149,219,239},{151,220,240},{153,222,241},{154,224,242},{156,226,242},{158,228,243},{160,230,244},{162,231,245},
  {163,233,246},{165,235,247},{167,237,247},{169,239,248},{171,240,249},{172,242,250},{174,244,251},{176,246,251},
  {178,248,252},{180,250,253},{182,252,254},{183,253,255},{188,254,247},{193,255,235},{200,255,223},{207,255,212},
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

struct Fish {
  int id;
  int type;
  float x;
  float y;
  float baseY;
  float angle;
  float speedBase;
  float len;
  float height;
  float tone;
  float wag;
  float phase;
  float orbitRx;
  float orbitRy;
};

static constexpr int MAX_FISH_COUNT = 24;
static Fish fishes[MAX_FISH_COUNT];

static float param_fishCount = 24.0f;
static float param_swimSpeed = 20.0f;
static float param_currentLoop = 0.0f;
static float param_unused = 0.114f;

static float simTime = 0.0f;
static float* valField = nullptr;

void setup() {
  PFMath::buildSinLUT();

  if (!valField) {
    valField = PFMem::allocFloats(FRAME_W * FRAME_H);
  }

  simTime = 0.0f;

  const float tones[4] = {0.98f, 0.88f, 0.78f, 0.65f};

  for (int i = 0; i < MAX_FISH_COUNT; i++) {
    int type = i % 4;
    float len = 11.0f;
    float height = 3.2f;

    if (type == 0) {
      len = 13.0f + (float)(i % 3) * 1.8f;
      height = 4.3f + (float)(i % 2) * 0.6f;
    } else if (type == 1) {
      len = 11.0f + (float)(i % 2) * 2.0f;
      height = 3.3f;
    } else if (type == 2) {
      len = 7.2f + (float)(i % 3) * 1.2f;
      height = 2.4f + (float)(i % 2) * 0.5f;
    } else {
      len = 13.5f + (float)(i % 2) * 2.0f;
      height = 2.5f;
    }

    float depthBias = PFMath::fastPow((float)i / 23.0f, 2.6f);
    float bottomY = 122.0f - depthBias * 64.0f;

    fishes[i].id = i;
    fishes[i].type = type;
    fishes[i].x = -18.0f + PFMath::jsMod((float)i * 19.3f, 98.0f);
    fishes[i].y = bottomY;
    fishes[i].baseY = bottomY;
    fishes[i].angle = PI;
    fishes[i].speedBase = 12.0f + PFMath::jsMod((float)i * 3.7f, 8.5f);
    fishes[i].len = len;
    fishes[i].height = height;
    fishes[i].tone = tones[i % 4];
    fishes[i].wag = (float)i * 1.5f;
    fishes[i].phase = (float)i * 2.2f;
    fishes[i].orbitRx = 19.0f + PFMath::jsMod((float)i * 4.3f, 7.0f);
    fishes[i].orbitRy = 44.0f + PFMath::jsMod((float)i * 5.7f, 8.0f);
  }
}

void update(float dt, const InputFrame& input) {
  if (!valField) return;

  PFParams::apply(input, 0, &param_fishCount, 4.0f, 24.0f, 0.417f);
  PFParams::apply(input, 1, &param_swimSpeed, 7.0f, 20.0f, 0.271f);
  PFParams::apply(input, 2, &param_currentLoop, 0.0f, 1.0f, 0.021f);
  PFParams::apply(input, 3, &param_unused, 0.0f, 1.0f, 0.021f);

  float safeDt = dt;
  if (safeDt <= 0.0f) safeDt = 0.016f;
  else if (safeDt > 0.05f) safeDt = 0.05f;

  float simDt = safeDt * (param_swimSpeed * 0.15f);
  simTime += simDt;

  constexpr float TIME_PERIOD = TWO_PI * 100.0f;
  if (simTime >= TIME_PERIOD) {
    simTime = PFMath::jsMod(simTime, TIME_PERIOD);
  }

  int activeCount = (int)(param_fishCount + 0.5f);
  activeCount = PFMath::clamp(activeCount, 4, 24);
  float k = PFMath::clamp01(param_currentLoop);
  float t = simTime;

  constexpr float cx = 32.0f;
  constexpr float cy = 64.0f;

  for (int i = 0; i < activeCount; i++) {
    Fish& f = fishes[i];
    float targetAngle = PI;

    if (k > 0.01f) {
      float nx = (f.x - cx) / f.orbitRx;
      float ny = (f.y - cy) / f.orbitRy;
      float dist = sqrtf(nx * nx + ny * ny);
      float phi = PFMath::fastAtan2(ny, nx);

      float tx = -PFMath::fastSin(phi) * f.orbitRx;
      float ty = PFMath::fastCos(phi) * f.orbitRy;

      float trackErr = 1.0f - dist;
      float attractX = nx * f.orbitRx * trackErr * 1.35f;
      float attractY = ny * f.orbitRy * trackErr * 1.35f;

      float loopAngle = PFMath::fastAtan2(ty + attractY, tx + attractX);

      float cosB = -1.0f * (1.0f - k) + PFMath::fastCos(loopAngle) * k;
      float sinB = 0.0f * (1.0f - k) + PFMath::fastSin(loopAngle) * k;
      targetAngle = PFMath::fastAtan2(sinB, cosB);
    } else {
      targetAngle += PFMath::fastSin(t * 1.2f + f.phase) * 0.05f;
    }

    float diff = PFMath::fastAtan2(PFMath::fastSin(targetAngle - f.angle), PFMath::fastCos(targetAngle - f.angle));
    float step = simDt * 3.6f;
    if (step > 1.0f) step = 1.0f;
    f.angle += diff * step;

    float currentSpeed = f.speedBase * (1.0f + k * 0.18f);
    f.x += PFMath::fastCos(f.angle) * currentSpeed * simDt;
    f.y += PFMath::fastSin(f.angle) * currentSpeed * simDt;

    if (k < 0.15f) {
      if (f.x < -24.0f) {
        f.x = 88.0f;
        f.y = f.baseY + PFMath::fastSin(t + (float)f.id) * 3.5f;
        f.angle = PI;
      }
    } else {
      if (f.x < -28.0f) f.x = 90.0f;
      if (f.x > 90.0f) f.x = -28.0f;
      if (f.y < -28.0f) f.y = 154.0f;
      if (f.y > 154.0f) f.y = -28.0f;
    }

    f.wag += simDt * (2.8f + currentSpeed * 0.08f);
    if (f.wag >= TWO_PI * 10.0f) {
      f.wag = PFMath::jsMod(f.wag, TWO_PI * 10.0f);
    }
  }
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);

  if (!valField) {
    PFCanvas::present();
    return;
  }

  const float t = simTime;
  int activeCount = (int)(param_fishCount + 0.5f);
  activeCount = PFMath::clamp(activeCount, 4, 24);
  const float k = PFMath::clamp01(param_currentLoop);

  constexpr float cx = 32.0f;
  constexpr float cy = 64.0f;
  constexpr float rx = 20.0f;
  constexpr float ry = 46.0f;
  constexpr float aspectY = rx / ry;
  constexpr float invAspectY = ry / rx;

  for (int y = 0; y < FRAME_H; y++) {
    int rowOffset = y * FRAME_W;
    float fy = (float)y;

    for (int x = 0; x < FRAME_W; x++) {
      float fx = (float)x;
      float x0 = fx;
      float y0 = fy;

      if (k > 0.01f) {
        float dx = fx - cx;
        float dy = (fy - cy) * aspectY;
        float r = sqrtf(dx * dx + dy * dy);

        float rDiff = (r - rx) * (1.0f / 12.0f);
        float bandWeight = expf(-(rDiff * rDiff));
        float rNorm = r * (1.0f / 42.0f);
        if (rNorm > 1.0f) rNorm = 1.0f;
        float rot = k * (1.75f * bandWeight + 0.3f * (1.0f - rNorm));

        float cosR = PFMath::fastCos(-rot);
        float sinR = PFMath::fastSin(-rot);

        float rx0 = dx * cosR - dy * sinR;
        float ry0 = dx * sinR + dy * cosR;

        x0 = cx + rx0;
        y0 = cy + ry0 * invAspectY;
      }

      float warpA = PFMath::fastSin(y0 * 0.16f + t * 1.2f + PFMath::fastCos(x0 * 0.12f) * 1.6f) * 2.2f;
      float warpB = PFMath::fastCos(x0 * 0.22f - y0 * 0.07f - t * 1.4f) * 1.5f;
      float microGrain = PFMath::fastSin(x0 * 0.45f + y0 * 0.3f - t * 0.8f) * 0.7f;

      float wx = x0 + warpA + microGrain;
      float wy = y0 + warpB + warpA * 0.4f;

      float streakRow = PFMath::floorF(wy * (1.0f / 2.8f));
      float horizFlow = PFMath::fastSin(wx * 0.14f - t * 2.2f + streakRow * 1.9f) * 0.035f;
      float horizFine = PFMath::fastCos(wx * 0.28f - t * 1.3f + wy * 0.08f) * 0.02f;

      float knifeBase = fabsf(PFMath::fastSin(wx * 0.08f + wy * 0.15f - t * 0.6f));
      float knifeChisel = (knifeBase * knifeBase * knifeBase) * 0.025f;

      float shelfY = 48.0f + PFMath::fastSin(wx * 0.11f - t * 1.1f) * 3.6f + PFMath::fastCos(wx * 0.26f) * 1.6f;
      float distFromShelf = wy - shelfY;

      float depthMask = 1.0f / (1.0f + expf(-distFromShelf * 0.85f));

      float shallowTone = 0.40f + horizFlow + horizFine + knifeChisel;
      float abyssTone = 0.09f + horizFlow * 0.5f + horizFine * 0.4f;

      float bgVal = shallowTone * (1.0f - depthMask) + abyssTone * depthMask;
      valField[rowOffset + x] = PFMath::clamp(bgVal, 0.06f, 0.44f);
    }
  }

  for (int i = 0; i < activeCount; i++) {
    const Fish& f = fishes[i];
    float fx = f.x;
    float fy = f.y;
    float fa = f.angle;
    float fTone = f.tone;

    float dirX = PFMath::fastCos(fa);
    float dirY = PFMath::fastSin(fa);
    float normX = -dirY;
    float normY = dirX;

    int steps = (int)ceilf(f.len * 1.5f);
    if (steps < 1) steps = 1;
    float invSteps = 1.0f / (float)steps;
    bool eyeDrawn = false;

    for (int s = 0; s <= steps; s++) {
      float q = (float)s * invSteps;
      float distBack = q * f.len;

      float wagPow = PFMath::fastPow(q, 1.8f);
      float wag = PFMath::fastSin(f.wag - q * 2.5f) * wagPow * 0.55f;

      float pxCenter = fx - dirX * distBack + normX * wag;
      float pyCenter = fy - dirY * distBack + normY * wag;

      float hUp = 0.0f;
      float hDown = 0.0f;
      bool isTail = false;
      bool isFork = false;

      if (f.type == 0) {
        if (q < 0.72f) {
          float prof = PFMath::fastSin((q / 0.72f) * PI);
          hUp = prof * (f.height * 0.58f);
          hDown = prof * (f.height * 0.42f);
        } else {
          float tq = (q - 0.72f) / 0.28f;
          hUp = (0.18f + tq * 0.26f) * f.height;
          hDown = (0.18f + tq * 0.26f) * f.height;
          isTail = true;
        }
      } else if (f.type == 1) {
        if (q < 0.70f) {
          float prof = PFMath::fastSin((q / 0.70f) * PI);
          hUp = prof * (f.height * 0.48f);
          hDown = prof * (f.height * 0.52f);
        } else {
          float tq = (q - 0.70f) / 0.30f;
          hUp = (0.16f + tq * 0.42f) * f.height;
          hDown = (0.16f + tq * 0.42f) * f.height;
          isTail = true;
          isFork = (tq > 0.42f);
        }
      } else if (f.type == 2) {
        if (q < 0.76f) {
          float prof = PFMath::fastSin((q / 0.76f) * PI);
          hUp = prof * (f.height * 0.38f);
          hDown = prof * (f.height * 0.62f);
        } else {
          float tq = (q - 0.76f) / 0.24f;
          float leaf = PFMath::fastSin(tq * PI) * 0.32f + 0.14f;
          hUp = leaf * f.height;
          hDown = leaf * f.height;
          isTail = true;
        }
      } else {
        if (q < 0.80f) {
          float prof = PFMath::fastSin((q / 0.80f) * PI * 0.95f);
          hUp = prof * (f.height * 0.48f);
          hDown = prof * (f.height * 0.52f);
        } else {
          float tq = (q - 0.80f) / 0.20f;
          float taper = 1.0f - tq * 0.85f;
          if (taper < 0.12f) taper = 0.12f;
          taper *= 0.35f;
          hUp = taper * f.height;
          hDown = taper * f.height;
          isTail = true;
        }
      }

      int topY = (int)ceilf(hUp);
      int botY = (int)ceilf(hDown);
      float forkGap = (float)topY * 0.42f;

      for (int d = -botY; d <= topY; d++) {
        float fd = (float)d;
        if (isFork && fabsf(fd) < forkGap) continue;

        int px = PFMath::ifloor(pxCenter + normX * fd);
        int py = PFMath::ifloor(pyCenter + normY * fd);

        if (px >= 0 && px < FRAME_W && py >= 0 && py < FRAME_H) {
          float val = isTail ? (fTone * 0.90f) : fTone;
          if (fd < -(float)botY * 0.65f) val -= 0.04f;

          val = PFMath::clamp(val, 0.64f, 1.0f);
          valField[py * FRAME_W + px] = val;
        }
      }

      if (!eyeDrawn && q >= 0.16f && q <= 0.22f) {
        float eyeOffset = hUp * 0.55f;
        int ex = PFMath::ifloor(pxCenter + normX * eyeOffset);
        int ey = PFMath::ifloor(pyCenter + normY * eyeOffset);

        if (ex >= 0 && ex < FRAME_W && ey >= 0 && ey < FRAME_H) {
          valField[ey * FRAME_W + ex] = 1.0f;
          eyeDrawn = true;
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

} // namespace YUJIN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
