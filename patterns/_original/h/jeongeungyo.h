#pragma once

// ===== Patternflow pattern =====
// Title:    JEONGEUNGYO
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

namespace JEONGEUNGYO {

const char* NAME = "JEONGEUNGYO";
const char* const KNOB_LABELS[4] = {"State", "DriftSpeed", "RotSpeed", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;

// Stops 0.000:#000000, 0.271:#cfc095, 0.309:#000000, 0.354:#878787, 0.475:#9c9c9c, 0.519:#e1d2a8, 0.971:#feeabe; mode oklab; wrap off.
static const uint8_t RAMP_LUT[256][3] = {
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{1,1,0},{1,1,1},{2,2,1},
  {3,3,2},{5,4,2},{6,5,3},{8,7,4},{11,9,5},{13,11,7},{16,14,8},{19,16,10},
  {21,19,12},{24,22,14},{27,24,16},{30,27,18},{33,30,21},{36,32,23},{39,35,25},{42,38,27},
  {45,41,29},{48,44,32},{51,47,34},{54,50,36},{57,53,39},{61,56,41},{64,59,43},{67,62,46},
  {70,65,48},{74,68,51},{77,71,53},{80,74,56},{84,77,58},{87,80,61},{91,84,63},{94,87,66},
  {98,90,68},{101,93,71},{105,97,73},{108,100,76},{112,103,79},{115,106,81},{119,110,84},{122,113,87},
  {126,117,89},{130,120,92},{133,123,95},{137,127,97},{141,130,100},{145,134,103},{148,137,106},{152,141,109},
  {156,144,111},{160,148,114},{164,151,117},{167,155,120},{171,159,123},{175,162,125},{179,166,128},{183,170,131},
  {187,173,134},{191,177,137},{195,180,140},{199,184,143},{203,188,146},{207,192,149},{182,168,130},{154,143,110},
  {128,118,91},{102,94,72},{78,71,54},{54,50,36},{33,30,20},{13,11,6},{1,1,1},{0,0,0},
  {1,1,1},{6,6,6},{16,16,16},{28,28,28},{41,41,41},{55,55,55},{69,69,69},{84,84,84},
  {99,99,99},{115,115,115},{131,131,131},{135,135,135},{136,136,136},{137,137,137},{138,138,138},{138,138,138},
  {139,139,139},{140,140,140},{140,140,140},{141,141,141},{142,142,142},{142,142,142},{143,143,143},{144,144,144},
  {144,144,144},{145,145,145},{146,146,146},{146,146,146},{147,147,147},{148,148,148},{148,148,148},{149,149,149},
  {150,150,150},{150,150,150},{151,151,151},{152,152,152},{152,152,152},{153,153,153},{154,154,154},{155,155,155},
  {155,155,155},{156,156,156},{161,160,157},{167,165,158},{173,170,160},{179,174,161},{185,179,162},{192,184,163},
  {198,189,164},{204,194,165},{210,199,166},{216,203,167},{223,208,168},{225,210,168},{225,210,168},{226,211,169},
  {226,211,169},{226,211,169},{226,211,169},{227,211,169},{227,212,169},{227,212,170},{227,212,170},{228,212,170},
  {228,212,170},{228,213,170},{228,213,171},{229,213,171},{229,213,171},{229,213,171},{229,214,171},{230,214,172},
  {230,214,172},{230,214,172},{230,214,172},{231,215,172},{231,215,172},{231,215,173},{231,215,173},{232,216,173},
  {232,216,173},{232,216,173},{232,216,174},{233,216,174},{233,217,174},{233,217,174},{233,217,174},{234,217,175},
  {234,217,175},{234,218,175},{234,218,175},{235,218,175},{235,218,176},{235,218,176},{235,219,176},{236,219,176},
  {236,219,176},{236,219,176},{236,219,177},{237,220,177},{237,220,177},{237,220,177},{237,220,177},{238,220,178},
  {238,221,178},{238,221,178},{238,221,178},{239,221,178},{239,222,179},{239,222,179},{239,222,179},{240,222,179},
  {240,222,179},{240,223,180},{240,223,180},{241,223,180},{241,223,180},{241,223,180},{241,224,180},{242,224,181},
  {242,224,181},{242,224,181},{242,224,181},{243,225,181},{243,225,182},{243,225,182},{243,225,182},{244,225,182},
  {244,226,182},{244,226,183},{244,226,183},{245,226,183},{245,227,183},{245,227,183},{245,227,184},{246,227,184},
  {246,227,184},{246,228,184},{246,228,184},{247,228,184},{247,228,185},{247,228,185},{248,229,185},{248,229,185},
  {248,229,185},{248,229,186},{249,229,186},{249,230,186},{249,230,186},{249,230,186},{250,230,187},{250,231,187},
  {250,231,187},{250,231,187},{251,231,187},{251,231,188},{251,232,188},{251,232,188},{252,232,188},{252,232,188},
  {252,232,189},{252,233,189},{253,233,189},{253,233,189},{253,233,189},{253,233,189},{254,234,190},{254,234,190},
  {254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},
};

struct Vec2 {
  float x;
  float y;
};

struct NodeSeed {
  float aloneX;
  float aloneY;
  float px;
  float py;
  float fx;
  float fy;
  float ampX;
  float ampY;
};

struct LinkDef {
  uint8_t nA;
  uint8_t pA;
  uint8_t nB;
  uint8_t pB;
};

struct NodeState {
  float cx;
  float cy;
  float curR;
  Vec2 ports[4];
};

static const Vec2 GRID_POS[9] = {
  { 13.5f, 45.5f }, { 32.0f, 45.5f }, { 50.5f, 45.5f },
  { 13.5f, 64.0f }, { 32.0f, 64.0f }, { 50.5f, 64.0f },
  { 13.5f, 82.5f }, { 32.0f, 82.5f }, { 50.5f, 82.5f }
};

static const NodeSeed NODE_SEEDS[9] = {
  { 16.0f,  22.0f, 0.14f, 0.19f, 1.25f, 1.60f, 13.0f, 17.0f },
  { 32.0f,  18.0f, 0.82f, 0.35f, 1.45f, 1.10f, 15.0f, 14.0f },
  { 48.0f,  24.0f, 0.41f, 0.77f, 1.10f, 1.50f, 13.0f, 17.0f },
  { 12.0f,  62.0f, 0.63f, 0.21f, 1.65f, 1.30f, 12.0f, 19.0f },
  { 32.0f,  64.0f, 0.00f, 0.00f, 0.95f, 1.15f,  9.0f, 11.0f },
  { 52.0f,  66.0f, 0.29f, 0.84f, 1.30f, 1.65f, 12.0f, 19.0f },
  { 16.0f, 106.0f, 0.75f, 0.12f, 1.40f, 1.15f, 13.0f, 17.0f },
  { 32.0f, 110.0f, 0.38f, 0.66f, 1.15f, 1.45f, 15.0f, 14.0f },
  { 48.0f, 104.0f, 0.91f, 0.47f, 1.60f, 1.30f, 13.0f, 17.0f }
};

static const float TONE_OFFSETS[9] = {
  -0.03f,  0.03f, -0.02f,
   0.04f,  0.00f, -0.04f,
   0.02f, -0.03f,  0.03f
};

static const LinkDef LINKS[12] = {
  {0, 3, 1, 2}, {1, 3, 2, 2},
  {6, 3, 7, 2}, {7, 3, 8, 2},
  {0, 1, 3, 0}, {2, 1, 5, 0},
  {3, 1, 6, 0}, {5, 1, 8, 0},
  {3, 3, 4, 2},
  {1, 1, 4, 0},
  {4, 3, 5, 2},
  {4, 1, 7, 0}
};

static float paramState = 0.0f;
static float paramDriftSpeed = 2.0f;
static float paramRotSpeed = 2.0f;
static float paramUnused = 1.0f;

static float animT = 0.0f;
static float animRot = 0.0f;
static NodeState nodes[9];

void setup() {
  PFMath::buildSinLUT();
  animT = 0.0f;
  animRot = 0.0f;
  for (int i = 0; i < 9; i++) {
    nodes[i].curR = 9.5f;
  }
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &paramState, 0.0f, 0.99f, 0.021f);
  PFParams::apply(input, 1, &paramDriftSpeed, 0.2f, 4.0f, 0.079f);
  PFParams::apply(input, 2, &paramRotSpeed, -2.0f, 2.0f, 0.083f);
  PFParams::apply(input, 3, &paramUnused, 0.0f, 1.0f, 0.021f);

  float safeDt = dt;
  if (safeDt <= 0.0f) safeDt = 0.016f;
  if (safeDt > 0.05f) safeDt = 0.05f;

  animT += safeDt * paramDriftSpeed;
  constexpr float TIME_PERIOD = 200.0f * PI;
  if (animT > TIME_PERIOD) animT -= TIME_PERIOD;

  animRot += safeDt * paramRotSpeed;
  if (animRot > TWO_PI) animRot -= TWO_PI;
  else if (animRot < -TWO_PI) animRot += TWO_PI;

  const float state = PFMath::clamp(paramState, 0.0f, 0.99f);
  const float easeState = state * state * (3.0f - 2.0f * state);
  constexpr float BASE_RADIUS = 9.5f;
  constexpr float P_OFFSET = BASE_RADIUS * 0.82f;

  for (int i = 0; i < 9; i++) {
    const NodeSeed& s = NODE_SEEDS[i];
    const Vec2& grid = GRID_POS[i];

    float wave1X = PFMath::fastCos(s.px * TWO_PI + animT * s.fx);
    float wave2X = PFMath::fastSin(animT * (s.fx * 1.8f) + s.py * PI) * 0.35f;
    float driftX = (wave1X + wave2X) * s.ampX;

    float wave1Y = PFMath::fastSin(s.py * TWO_PI + animT * s.fy);
    float wave2Y = PFMath::fastCos(animT * (s.fy * 1.7f) + s.px * PI) * 0.35f;
    float driftY = (wave1Y + wave2Y) * s.ampY;

    float aloneX = s.aloneX + driftX;
    float aloneY = s.aloneY + driftY;

    float cx = aloneX + (grid.x - aloneX) * easeState;
    float cy = aloneY + (grid.y - aloneY) * easeState;

    nodes[i].cx = cx;
    nodes[i].cy = cy;
    nodes[i].curR = BASE_RADIUS;

    nodes[i].ports[0].x = cx;
    nodes[i].ports[0].y = cy - P_OFFSET;
    nodes[i].ports[1].x = cx;
    nodes[i].ports[1].y = cy + P_OFFSET;
    nodes[i].ports[2].x = cx - P_OFFSET;
    nodes[i].ports[2].y = cy;
    nodes[i].ports[3].x = cx + P_OFFSET;
    nodes[i].ports[3].y = cy;
  }
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);

  constexpr float CX0 = FRAME_W * 0.5f;
  constexpr float CY0 = FRAME_H * 0.5f;
  constexpr float STITCH_W_SQ = 1.0f;
  constexpr float INV_CUR_R = 1.0f / 9.5f;
  constexpr float CUR_R_SQ = 9.5f * 9.5f;

  const float state = PFMath::clamp(paramState, 0.0f, 0.99f);
  const float bgTone = 0.02f + (state / 0.99f) * 0.24f;
  const float cosR = PFMath::fastCos(animRot);
  const float sinR = PFMath::fastSin(animRot);
  const bool drawWeb = (state < 0.85f);
  const float webPower = drawWeb ? (1.0f - (state / 0.85f)) : 0.0f;
  const float webWeight = webPower * 0.75f;
  const float webOneMinusWeight = 1.0f - webWeight;

  for (int y = 0; y < FRAME_H; y++) {
    const float vy = (float)y - CY0;
    const float baseRx = CX0 + vy * sinR;
    const float baseRy = CY0 + vy * cosR;

    for (int x = 0; x < FRAME_W; x++) {
      const float ux = (float)x - CX0;
      const float rx = baseRx + ux * cosR;
      const float ry = baseRy - ux * sinR;

      float pixelVal = bgTone;

      // 1. 단색 바느질 연결선: 0.37
      for (int l = 0; l < 12; l++) {
        if (l == 8  && state < 0.25f) continue;
        if (l == 9  && state < 0.45f) continue;
        if (l == 10 && state < 0.65f) continue;
        if (l == 11 && state < 0.85f) continue;

        const LinkDef& link = LINKS[l];
        const Vec2& pA = nodes[link.nA].ports[link.pA];
        const Vec2& pB = nodes[link.nB].ports[link.pB];

        const float abx = pB.x - pA.x;
        const float aby = pB.y - pA.y;
        const float lenSq = abx * abx + aby * aby;
        if (lenSq < 0.1f) continue;

        const float tProj = ((rx - pA.x) * abx + (ry - pA.y) * aby) / lenSq;
        if (tProj >= 0.0f && tProj <= 1.0f) {
          const float px = pA.x + tProj * abx;
          const float py = pA.y + tProj * aby;
          const float dx = rx - px;
          const float dy = ry - py;
          const float distSq = dx * dx + dy * dy;

          if (distSq < STITCH_W_SQ) {
            pixelVal = 0.37f;
          }
        }
      }

      // 2. 도자 원반 노드 & 슬릿 홈 & 거미줄
      for (int i = 0; i < 9; i++) {
        const NodeState& nd = nodes[i];
        const float dx = rx - nd.cx;
        const float dy = ry - nd.cy;
        const float distSq = dx * dx + dy * dy;

        if (distSq <= CUR_R_SQ) {
          const float dist = sqrtf(distSq);
          const float normDist = dist * INV_CUR_R;

          const float baseDome = 0.52f + 0.33f * PFMath::fastCos(normDist * HALF_PI);
          float diskTone = baseDome + TONE_OFFSETS[i];

          // 가장자리 슬릿 홈: 0.32
          for (int k = 0; k < 4; k++) {
            const Vec2& port = nd.ports[k];
            const float px = rx - port.x;
            const float py = ry - port.y;

            float dLineSq = 999.0f;
            if (k == 0 || k == 1) {
              const float clampedY = PFMath::clamp(py, -2.3f, 2.3f);
              const float ly = py - clampedY;
              dLineSq = px * px + ly * ly;
            } else {
              const float clampedX = PFMath::clamp(px, -2.3f, 2.3f);
              const float lx = px - clampedX;
              dLineSq = lx * lx + py * py;
            }

            if (dLineSq < 1.0f) {
              diskTone = 0.32f;
            }
          }

          // 4번 원반: 본래 거미줄 패턴 (0.35)
          if (i == 4 && drawWeb) {
            const float angle = PFMath::fastAtan2(dy, dx);
            const float spokes = fabsf(PFMath::fastSin(angle * 5.0f));
            const float rings = fabsf(PFMath::fastSin(dist * 1.2f - animT * 0.7f));

            if (spokes > 0.88f || rings > 0.82f) {
              diskTone = diskTone * webOneMinusWeight + 0.35f * webWeight;
            }
          }

          if (diskTone > pixelVal) {
            pixelVal = diskTone;
          }
        }
      }

      float vClamped = PFMath::clamp01(pixelVal);
      int li = (int)(vClamped * 255.0f + 0.5f);
      if (li > 255) li = 255;
      PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
    }
  }

  PFCanvas::present();
}

} // namespace JEONGEUNGYO

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
