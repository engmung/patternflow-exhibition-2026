#pragma once

// ===== Patternflow pattern =====
// Title:    HEOEUNBIN
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

namespace HEOEUNBIN {

const char* NAME = "HEOEUNBIN";
const char* const KNOB_LABELS[4] = {"Speed", "Backdrop", "Size", "-"};
constexpr bool ABSOLUTE_READY = true;

static constexpr uint8_t C_BG = 0;
static constexpr uint8_t C_SPARK = 1;
static constexpr uint8_t C_FARLEG = 2;
static constexpr uint8_t C_BODY = 3;
static constexpr uint8_t C_STRIPE = 4;
static constexpr uint8_t C_EYE = 5;
static constexpr uint8_t C_INK = 6;
static constexpr uint8_t C_MOUSE = 7;
static constexpr uint8_t C_CHEEK = 8;
static constexpr uint8_t C_FRUIT = 9;
static constexpr uint8_t C_LEAF = 10;
static constexpr uint8_t C_CREST = 11;

static const uint8_t RANK[12] = {0, 0, 2, 2, 2, 5, 9, 3, 3, 4, 4, 3};

static const uint8_t PALETTE[12][3] = {
  {255, 150, 60},  // C_BG
  {255, 214, 110}, // C_SPARK
  {236, 208, 120}, // C_FARLEG
  {255, 232, 140}, // C_BODY
  {232, 190, 82},  // C_STRIPE
  {255, 250, 228}, // C_EYE
  {0, 0, 0},        // C_INK
  {255, 255, 255}, // C_MOUSE
  {255, 172, 160}, // C_CHEEK
  {255, 160, 40},  // C_FRUIT
  {96, 200, 80},   // C_LEAF
  {255, 232, 140}  // C_CREST
};

static uint8_t buf[PANEL_RES_W * PANEL_RES_H];
static uint8_t ink[PANEL_RES_W * PANEL_RES_H];

static float speedVal = 0.502f;
static float backdropVal = 1.0f;
static float sizeVal = 1.5f;
static float unusedVal = 1.5f;

static float gait = 0.0f;
static float idle = 0.0f;
static float drift = 0.0f;
static float dir = -1.0f;
static bool rider = false;

struct TailPoint {
  float x;
  float y;
  float r;
};

static TailPoint pts[38];

static void disc(float cx, float cy, float r, uint8_t c) {
  float r2 = r * r;
  int y0 = PFMath::clamp(PFMath::ifloor(cy - r), 0, PANEL_RES_H - 1);
  int y1 = PFMath::clamp(PFMath::ifloor(cy + r + 0.999f), 0, PANEL_RES_H - 1);
  int x0 = PFMath::clamp(PFMath::ifloor(cx - r), 0, PANEL_RES_W - 1);
  int x1 = PFMath::clamp(PFMath::ifloor(cx + r + 0.999f), 0, PANEL_RES_W - 1);

  for (int y = y0; y <= y1; y++) {
    float dy = (float)y + 0.5f - cy;
    float dy2 = dy * dy;
    int rowIdx = y * PANEL_RES_W;
    for (int x = x0; x <= x1; x++) {
      float dx = (float)x + 0.5f - cx;
      if (dx * dx + dy2 <= r2) {
        buf[rowIdx + x] = c;
      }
    }
  }
}

static void ellipse(float cx, float cy, float rx, float ry, uint8_t c) {
  if (rx <= 0.001f || ry <= 0.001f) return;
  float invRx = 1.0f / rx;
  float invRy = 1.0f / ry;

  int y0 = PFMath::clamp(PFMath::ifloor(cy - ry), 0, PANEL_RES_H - 1);
  int y1 = PFMath::clamp(PFMath::ifloor(cy + ry + 0.999f), 0, PANEL_RES_H - 1);
  int x0 = PFMath::clamp(PFMath::ifloor(cx - rx), 0, PANEL_RES_W - 1);
  int x1 = PFMath::clamp(PFMath::ifloor(cx + rx + 0.999f), 0, PANEL_RES_W - 1);

  for (int y = y0; y <= y1; y++) {
    float dy = ((float)y + 0.5f - cy) * invRy;
    float dy2 = dy * dy;
    if (dy2 > 1.0f) continue;
    int rowIdx = y * PANEL_RES_W;
    for (int x = x0; x <= x1; x++) {
      float dx = ((float)x + 0.5f - cx) * invRx;
      if (dx * dx + dy2 <= 1.0f) {
        buf[rowIdx + x] = c;
      }
    }
  }
}

static void capsule(float x0, float y0, float x1, float y1, float r0, float r1, uint8_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int n = (int)(len * 1.5f + 0.999f);
  if (n < 1) n = 1;
  float invN = 1.0f / (float)n;
  for (int i = 0; i <= n; i++) {
    float t = (float)i * invN;
    disc(x0 + dx * t, y0 + dy * t, r0 + (r1 - r0) * t, c);
  }
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint8_t c) {
  int minX = PFMath::clamp(PFMath::ifloor(fminf(x0, fminf(x1, x2))), 0, PANEL_RES_W - 1);
  int maxX = PFMath::clamp(PFMath::ifloor(fmaxf(x0, fmaxf(x1, x2)) + 0.999f), 0, PANEL_RES_W - 1);
  int minY = PFMath::clamp(PFMath::ifloor(fminf(y0, fminf(y1, y2))), 0, PANEL_RES_H - 1);
  int maxY = PFMath::clamp(PFMath::ifloor(fmaxf(y0, fmaxf(y1, y2)) + 0.999f), 0, PANEL_RES_H - 1);

  float det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
  if (fabsf(det) < 0.0001f) return;
  float invDet = 1.0f / det;

  for (int y = minY; y <= maxY; y++) {
    float py = (float)y + 0.5f;
    int rowIdx = y * PANEL_RES_W;
    for (int x = minX; x <= maxX; x++) {
      float px = (float)x + 0.5f;
      float w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) * invDet;
      float w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) * invDet;
      float w2 = 1.0f - w0 - w1;
      if (w0 >= -0.01f && w1 >= -0.01f && w2 >= -0.01f) {
        buf[rowIdx + x] = c;
      }
    }
  }
}

static void mouthCut(float xa, float ya, float xb, float yb, float ra, float rb, uint8_t c) {
  int x0 = (int)(fminf(xa, xb) + 0.5f);
  int x1 = (int)(fmaxf(xa, xb) + 0.5f);
  int minCol = PFMath::clamp(x0, 0, PANEL_RES_W - 1);
  int maxCol = PFMath::clamp(x1, 0, PANEL_RES_W - 1);
  float spanX = (float)(x1 - x0);

  for (int x = minCol; x <= maxCol; x++) {
    float t = (spanX < 0.0001f) ? 0.0f : ((float)(x - x0) / spanX);
    if (xa > xb) t = 1.0f - t;
    float yc = ya + (yb - ya) * t;
    int rows = (int)(2.0f * (ra + (rb - ra) * t) + 0.5f);
    if (rows < 1) rows = 1;
    int top = (int)(yc - (float)rows * 0.5f + 0.5f);
    int yStart = PFMath::clamp(top, 0, PANEL_RES_H - 1);
    int yEnd = PFMath::clamp(top + rows, 0, PANEL_RES_H);
    for (int y = yStart; y < yEnd; y++) {
      buf[y * PANEL_RES_W + x] = c;
    }
  }
}

static void flower(float cx, float cy, float s, uint8_t c) {
  disc(cx, cy, s * 0.55f, c);
  for (int k = 0; k < 6; k++) {
    float a = (float)k * (PI / 3.0f);
    disc(cx + PFMath::fastCos(a) * s, cy + PFMath::fastSin(a) * s, s * 0.55f, c);
  }
}

static void drawLeg(float hipx, float hipy, float phase, bool isFront, float ground, uint8_t c,
                    float baseCx, float baseCy, float focusX, float focusY, float s, float currentDir) {
  auto transformX = [&](float lx) -> float {
    return baseCx + (focusX + (lx - focusX) * s) * -currentDir;
  };
  auto transformY = [&](float ly) -> float {
    return baseCy + (focusY + (ly - focusY) * s);
  };
  auto T = [&](float x0, float y0, float x1, float y1, float r0, float r1) {
    capsule(transformX(x0), transformY(y0), transformX(x1), transformY(y1), r0 * s, r1 * s, c);
  };

  float stride = 8.5f, lift = 4.2f;
  float fx, fy;
  float p = PFMath::fract(phase);
  if (p < 0.62f) {
    float q = p / 0.62f;
    fx = hipx - stride * 0.5f + stride * q;
    fy = ground;
  } else {
    float q2 = (p - 0.62f) / 0.38f;
    fx = hipx + stride * 0.5f - stride * q2;
    fy = ground - lift * PFMath::fastSin(q2 * PI);
  }

  float L = 8.2f;
  float dx = fx - hipx, dy = fy - hipy;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 2.0f * L - 0.5f) {
    float k = (2.0f * L - 0.5f) / d;
    dx *= k;
    dy *= k;
    d = 2.0f * L - 0.5f;
  }
  float mx = hipx + dx * 0.5f, my = hipy + dy * 0.5f;
  float h = sqrtf(fmaxf(0.0f, L * L - d * d * 0.25f));
  float nx = (d > 0.0001f) ? (-dy / d) : 0.0f;
  float ny = (d > 0.0001f) ? (dx / d) : 1.0f;
  float bend = isFront ? 1.0f : -1.0f;
  if (nx * bend < 0.0f) { nx = -nx; ny = -ny; }
  float kx = mx + nx * h, ky = my + ny * h;
  float ax = hipx + dx, ay = hipy + dy;

  T(hipx, hipy, kx, ky, 3.3f, 2.8f);
  T(kx, ky, ax, ay, 2.8f, 2.4f);

  float lifted = (fy < ground - 0.5f) ? 1.0f : 0.0f;
  T(ax, ay, ax - 4.6f, ay + 0.4f - lifted * 1.2f, 2.0f, 1.3f);
  T(ax, ay, ax - 4.0f, ay - 2.8f, 1.8f, 1.2f);
  T(ax, ay, ax - 2.6f, ay + 2.8f - lifted * 0.9f, 1.8f, 1.2f);
}

void setup() {
  PFMath::buildSinLUT();
  gait = 0.0f;
  idle = 0.0f;
  drift = 0.0f;
  dir = -1.0f;
  rider = false;
  speedVal = 0.502f;
  backdropVal = 1.0f;
  sizeVal = 1.5f;
  unusedVal = 1.5f;
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &speedVal, 0.0f, 2.0f, 0.042f);
  PFParams::apply(input, 1, &backdropVal, 0.0f, 1.0f, 0.021f);
  PFParams::apply(input, 2, &sizeVal, 0.6f, 1.5f, 0.019f);
  PFParams::apply(input, 3, &unusedVal, 0.5f, 1.5f, 0.021f);

  if (input.btnPressed[0] && !input.paramAbsoluteActive[0] && !input.knobAudioActive[0]) {
    dir = -dir;
  }
  if (input.btnPressed[1] && !input.paramAbsoluteActive[1] && !input.knobAudioActive[1]) {
    rider = !rider;
  }

  gait = PFMath::fract(gait + dt * speedVal * 1.5f);
  idle += dt;
  if (idle > 370.0f * TWO_PI) idle -= 370.0f * TWO_PI; // Seamless multi-period wrap
  drift += dt * speedVal * 20.0f;
  if (drift > 1480.0f) drift -= 1480.0f; // Multiplier wrap for drift % (W + 20)
}

void draw() {
  float s = sizeVal;
  float currentDir = dir;
  float walk = fminf(1.0f, speedVal);
  float currentGait = gait;
  float currentIdle = idle;

  float bgLevel = fminf(1.0f, backdropVal * 2.0f);
  float sparkAmount = fmaxf(0.0f, backdropVal - 0.5f) * 2.0f;

  memset(buf, C_BG, sizeof(buf));

  // Background sparkles / flowers
  int nSpark = 1 + (int)(sparkAmount * 5.0f + 0.5f);
  for (int i = 0; i < nSpark; i++) {
    float driftTerm = currentDir * -drift * (0.55f + 0.25f * (float)(i % 3));
    float rawSx = (float)(i * 41 + 7) + driftTerm;
    float sx = PFMath::jsMod(rawSx, (float)(PANEL_RES_W + 20));
    if (sx < 0.0f) sx += (float)(PANEL_RES_W + 20);
    sx -= 10.0f;
    float sy = 8.0f + (float)((i * 23 + 11) % 48);
    flower(sx, sy, 2.4f + (float)(i % 2) * 0.8f, C_SPARK);
  }

  // Gecko base positioning
  float baseCx = (float)PANEL_RES_W * 0.5f + 6.0f + PFMath::fastSin(currentIdle * 0.6f) * 1.5f;
  float bob = -fabsf(PFMath::fastSin(currentGait * TWO_PI)) * 1.2f * walk;
  float baseCy = 27.5f + bob;
  float ground = 17.0f;

  float zoomFactor = fmaxf(0.0f, (s - 1.0f) / 0.5f);
  float focusX = -30.0f * zoomFactor;
  float focusY = -17.0f * zoomFactor;

  auto X = [&](float lx) -> float {
    return baseCx + (focusX + (lx - focusX) * s) * -currentDir;
  };
  auto Y = [&](float ly) -> float {
    return baseCy + (focusY + (ly - focusY) * s);
  };

  auto T = [&](float x0, float y0, float x1, float y1, float r0, float r1, uint8_t c) {
    capsule(X(x0), Y(y0), X(x1), Y(y1), r0 * s, r1 * s, c);
  };
  auto E = [&](float x0, float y0, float rx, float ry, uint8_t c) {
    ellipse(X(x0), Y(y0), rx * s, ry * s, c);
  };
  auto D = [&](float x0, float y0, float r, uint8_t c) {
    disc(X(x0), Y(y0), r * s, c);
  };
  auto TRI = [&](float x0, float y0, float x1, float y1, float x2, float y2, uint8_t c) {
    triangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c);
  };

  float ex = -36.0f, ey = -10.0f;
  float eyeR = 8.6f;

  // Far legs
  drawLeg(-27.0f, 3.5f, currentGait + 0.5f, true, ground, C_FARLEG, baseCx, baseCy, focusX, focusY, s, currentDir);
  drawLeg(2.0f, 5.0f, currentGait, false, ground, C_FARLEG, baseCx, baseCy, focusX, focusY, s, currentDir);

  // Tail
  float tx = 4.0f, ty = 2.0f;
  int N = 38;
  float seg = 1.45f;
  float idleSway = PFMath::fastSin(currentIdle * 2.2f) * 0.08f + PFMath::fastSin(currentIdle * 0.9f) * 0.04f;
  for (int k = 0; k < N; k++) {
    float u = (float)k / (float)N;
    float curl = 0.08f + 1.15f * PFMath::fastPow(u, 1.2f);
    float tailWave = PFMath::fastSin(u * 5.2f - currentGait * TWO_PI) * 0.22f * walk;
    float a = curl + idleSway * (0.4f + 0.6f * u) + tailWave * u;

    float nx2 = tx + PFMath::fastCos(a) * seg;
    float ny2 = ty + PFMath::fastSin(a) * seg;
    float r = 6.4f * (1.0f - u * 0.82f) + 1.2f;
    T(tx, ty, nx2, ny2, r, r, C_BODY);
    pts[k] = {nx2, ny2, r};
    tx = nx2;
    ty = ny2;
  }

  // Torso
  T(-29.0f, 0.0f, 5.0f, 2.0f, 11.4f, 7.5f, C_BODY);

  // Skull
  E(-35.0f, -4.0f, 15.5f, 12.0f, C_BODY);

  // Upper snout
  T(-35.0f, -3.5f, -57.5f, -3.0f, 10.5f, 2.4f, C_BODY);
  D(-57.5f, -3.0f, 2.4f, C_BODY);

  // Lower jaw
  T(-31.0f, 3.0f, -49.5f, 3.2f, 4.8f, 1.8f, C_BODY);
  D(-49.5f, 3.2f, 1.8f, C_BODY);

  // Near legs
  drawLeg(2.0f, 5.0f, currentGait + 0.5f, false, ground, C_BODY, baseCx, baseCy, focusX, focusY, s, currentDir);
  drawLeg(-27.0f, 3.5f, currentGait, true, ground, C_BODY, baseCx, baseCy, focusX, focusY, s, currentDir);

  // Body flank stripe dots
  int bodySteps = 16;
  for (int bq = 0; bq < bodySteps; bq++) {
    if (bq % 5 < 3) {
      float bt = (float)bq / (float)(bodySteps - 1);
      float bx = -23.0f + bt * 27.0f;
      float by = -7.5f + bt * 4.7f;
      D(bx, by, 1.0f, C_STRIPE);
    }
  }

  // Tail stripe dots
  for (int q = 0; q < N - 4; q++) {
    if (q % 5 < 3) {
      float pR = pts[q].r;
      D(pts[q].x, pts[q].y - pR * 0.55f, fminf(1.0f, pR * 0.25f), C_STRIPE);
    }
  }

  // Mouth cut
  mouthCut(X(-53.0f), Y(1.5f), X(-37.0f), Y(2.0f), 0.9f * s, 0.4f * s, C_BG);

  // Mouse rider
  if (rider) {
    float mb = PFMath::fastSin(currentGait * 4.0f * PI + 1.2f) * 0.6f * walk;
    float mx0 = -10.0f, my0 = -12.5f + mb;
    D(mx0 - 7.5f, my0 - 5.5f, 3.6f, C_MOUSE);
    D(mx0 + 6.5f, my0 - 5.5f, 3.6f, C_MOUSE);
    E(mx0, my0, 9.2f, 6.6f, C_MOUSE);
    E(mx0 - 8.5f, my0 + 2.0f, 4.4f, 3.0f, C_MOUSE);
    E(mx0 + 1.0f, my0 - 7.2f, 3.0f, 2.7f, C_FRUIT);
    D(mx0 + 1.5f, my0 - 9.9f, 0.9f, C_LEAF);
    D(mx0 - 6.0f, my0 + 1.0f, 1.6f, C_CHEEK);
    D(mx0 + 4.5f, my0 + 2.0f, 1.6f, C_CHEEK);
  }

  // Eyelid crests
  float crestBase = 7.6f, crestH = 6.2f, crestTipR = 1.4f;
  const float crestAngles[3] = {-135.0f * DEG_TO_RAD, -90.0f * DEG_TO_RAD, -45.0f * DEG_TO_RAD};
  const float crestLean[3] = {-143.0f * DEG_TO_RAD, -90.0f * DEG_TO_RAD, -37.0f * DEG_TO_RAD};
  for (int ci = 0; ci < 3; ci++) {
    float ca = crestAngles[ci];
    float la = crestLean[ci];
    float ux = PFMath::fastCos(ca), uy = PFMath::fastSin(ca);
    float vx = -uy, vy = ux;
    float bx0 = ex + ux * (eyeR - 1.0f), by0 = ey + uy * (eyeR - 1.0f);
    float apx = bx0 + PFMath::fastCos(la) * crestH, apy = by0 + PFMath::fastSin(la) * crestH;
    TRI(bx0 + vx * crestBase * 0.5f, by0 + vy * crestBase * 0.5f,
        bx0 - vx * crestBase * 0.5f, by0 - vy * crestBase * 0.5f,
        apx, apy, C_CREST);
    D(apx - PFMath::fastCos(la) * crestTipR, apy - PFMath::fastSin(la) * crestTipR, crestTipR, C_CREST);
  }

  // Eye disc & blink
  float blinkT = PFMath::jsMod(currentIdle, 3.7f);
  bool blink = (blinkT > 3.5f && blinkT < 3.66f);
  if (!blink) D(ex, ey, eyeR, C_EYE);

  // ── Outline ink pass ──
  for (int y = 0; y < PANEL_RES_H; y++) {
    int rowIdx = y * PANEL_RES_W;
    for (int x = 0; x < PANEL_RES_W; x++) {
      int idx = rowIdx + x;
      uint8_t c = buf[idx];
      uint8_t rk = RANK[c];
      ink[idx] = 0;
      if (rk == 0 || c == C_INK) continue;
      if ((x > 0 && RANK[buf[idx - 1]] < rk) ||
          (x < PANEL_RES_W - 1 && RANK[buf[idx + 1]] < rk) ||
          (y > 0 && RANK[buf[idx - PANEL_RES_W]] < rk) ||
          (y < PANEL_RES_H - 1 && RANK[buf[idx + PANEL_RES_W]] < rk)) {
        ink[idx] = 1;
      }
    }
  }
  for (int j = 0; j < PANEL_RES_W * PANEL_RES_H; j++) {
    if (ink[j]) buf[j] = C_INK;
  }

  // Ink details
  if (blink) {
    T(ex - 6.0f, ey + 1.0f, ex + 6.0f, ey + 1.0f, 1.1f, 1.1f, C_INK);
  } else {
    // Pupil
    float pw = eyeR * 0.4f * s;
    float pcx = X(ex), pcy = Y(ey);
    float pr = (eyeR - 0.8f) * s;
    float pr2 = pr * pr;
    int py0 = PFMath::clamp(PFMath::ifloor(pcy - pr), 0, PANEL_RES_H - 1);
    int py1 = PFMath::clamp(PFMath::ifloor(pcy + pr + 0.999f), 0, PANEL_RES_H - 1);
    int px0 = PFMath::clamp(PFMath::ifloor(pcx - pw), 0, PANEL_RES_W - 1);
    int px1 = PFMath::clamp(PFMath::ifloor(pcx + pw + 0.999f), 0, PANEL_RES_W - 1);

    for (int py2 = py0; py2 <= py1; py2++) {
      float ddy = (float)py2 + 0.5f - pcy;
      float ddy2 = ddy * ddy;
      int rIdx = py2 * PANEL_RES_W;
      for (int px2 = px0; px2 <= px1; px2++) {
        float ddx = (float)px2 + 0.5f - pcx;
        if (fabsf(ddx) <= pw && (ddx * ddx + ddy2 <= pr2) && buf[rIdx + px2] == C_EYE) {
          buf[rIdx + px2] = C_INK;
        }
      }
    }
  }

  // Ear dot
  D(-24.0f, -4.0f, 1.6f, C_INK);

  // Mouse face details
  if (rider) {
    float mb2 = PFMath::fastSin(currentGait * 4.0f * PI + 1.2f) * 0.6f * walk;
    float ex2 = -10.0f, ey2 = -12.5f + mb2;
    T(ex2 - 8.5f, ey2 - 1.5f, ex2 - 7.0f, ey2 - 2.6f, 0.55f, 0.55f, C_INK);
    T(ex2 - 7.0f, ey2 - 2.6f, ex2 - 5.5f, ey2 - 1.5f, 0.55f, 0.55f, C_INK);
    T(ex2 - 0.5f, ey2 - 1.5f, ex2 + 1.0f, ey2 - 2.6f, 0.55f, 0.55f, C_INK);
    T(ex2 + 1.0f, ey2 - 2.6f, ex2 + 2.5f, ey2 - 1.5f, 0.55f, 0.55f, C_INK);
    D(ex2 - 12.5f, ey2 + 2.6f, 0.8f, C_INK);
  }

  // ── RGB rendering pass ──
  uint8_t bgR = (uint8_t)(PALETTE[C_BG][0] * bgLevel + 0.5f);
  uint8_t bgG = (uint8_t)(PALETTE[C_BG][1] * bgLevel + 0.5f);
  uint8_t bgB = (uint8_t)(PALETTE[C_BG][2] * bgLevel + 0.5f);

  uint8_t spR = (uint8_t)(PALETTE[C_SPARK][0] * bgLevel + 0.5f);
  uint8_t spG = (uint8_t)(PALETTE[C_SPARK][1] * bgLevel + 0.5f);
  uint8_t spB = (uint8_t)(PALETTE[C_SPARK][2] * bgLevel + 0.5f);

  for (int y2 = 0; y2 < PANEL_RES_H; y2++) {
    int rowIdx = y2 * PANEL_RES_W;
    for (int x2 = 0; x2 < PANEL_RES_W; x2++) {
      uint8_t cc = buf[rowIdx + x2];
      if (cc == C_BG) {
        PFCanvas::setPixel(x2, y2, bgR, bgG, bgB);
      } else if (cc == C_SPARK) {
        PFCanvas::setPixel(x2, y2, spR, spG, spB);
      } else {
        PFCanvas::setPixel(x2, y2, PALETTE[cc][0], PALETTE[cc][1], PALETTE[cc][2]);
      }
    }
  }

  PFCanvas::present();
}

} // namespace HEOEUNBIN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
