#pragma once

// ===== Patternflow pattern =====
// Title:    HEOEUNBIN
// Author:   @engmung
// Date:     2026-09-19
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================
//
// Translated from patterns_js/new/heoeunbin_portrait.js (the portrait
// exhibit file). The gecko is drawn into a 128x64 index buffer exactly as
// the JS does, and the buffer is written rotated counter-clockwise into the
// 64x128 frame: buffer (x, y) -> frame (y, BUF_W-1-x). That orientation is
// the artist's choice (head down, feet to the right) - keep it.
//
// Knob 1: Size     0.6..1.5  (centre 1.05 = whole body, 1.5 = head close-up)
// Knob 2: Backdrop 0.3..1    (black background, no visible flowers -> 5 at centre -> 9;
//                             cream flowers grow along the edges over the full travel)
// Knob 3: Speed input 0.2..1.2 -> actual 0.2..2.4; centre stays 0.7.
// Above centre, a quadratic boost increases both gait and MIDI footfall rate.
// Knob 4: unused
// K1 press: flip walking direction. K2 press: the mouse rider hops on / off.
// Idle return puts back dir = -1, rider = false.
//
// exhibit: update() also raises `stepPulse` in the frame a foot lands, so the
// gallery's Ableton set can hit the footstep sample on the step rather than
// on a clock of its own (features/exhibit/exhibit_pulse.h). It is a flag and
// nothing more - no pixel here depends on it, and nobody has to read it.

#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"
#include "src/core_mem.h"   // exhibit: PFMem, for buf below

namespace HEOEUNBIN {

const char* NAME = "HEOEUNBIN";
const char* const KNOB_LABELS[4] = {"Size", "Backdrop", "Speed", "-"};
constexpr bool ABSOLUTE_READY = true;

// The frame the visitor sees (portrait) ...
constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;
// ... and the drawing buffer, which is NOT the frame: the gecko is drawn
// landscape and rotated on the way out.
constexpr int BUF_W = 128;
constexpr int BUF_H = 64;

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

// The far leg ranks one below the body, so the body gets an outline where it
// overlaps the far leg; the crest has its own rank above the body.
static const uint8_t RANK[12] = {0, 0, 1, 2, 2, 5, 9, 3, 3, 4, 4, 3};

static const uint8_t PALETTE[12][3] = {
  {255, 150, 60},  // C_BG
  {255, 240, 196}, // C_SPARK
  {184, 136, 64},  // C_FARLEG (shaded, ~40% darker than the body)
  {255, 232, 140}, // C_BODY
  {232, 190, 82},  // C_STRIPE
  {255, 250, 228}, // C_EYE
  {0, 0, 0},       // C_INK
  {255, 255, 255}, // C_MOUSE
  {255, 172, 160}, // C_CHEEK
  {255, 160, 40},  // C_FRUIT
  {96, 200, 80},   // C_LEAF
  {255, 232, 140}  // C_CREST
};

// exhibit: 8 KB, PFMem in setup(). The JS also keeps a second 8 KB ink
// buffer; here the outline pass runs row by row with two 128-byte row copies
// on the stack instead, which gives the same pixels.
static uint8_t* buf = nullptr;

// Knob variables (the idle return's rest list). Start values = the centres.
static float sizeVal = 1.05f;
static float backdropVal = 0.65f;
static float speedVal = 0.7f;
static float unusedVal = 0.5f;   // K4: never reaches the pattern

static float gait = 0.0f;     // wraps with fract
static float idle = 0.0f;     // wraps at 20*PI
static float blinkT = 0.0f;   // wraps at 3.7
static float drift = 0.0f;    // wraps at 2960
static float dir = -1.0f;     // K1 press flips
static bool rider = false;    // K2 press toggles

// exhibit: the frames a foot lands in, and nothing else. drawLeg() puts a
// leg's stance - the part of the cycle its foot is on the ground - at the
// start of its phase, so a foot touches down the moment its phase crosses 0:
// the near front and far hind legs are drawn at phase `gait` (bit 1), the
// near hind and far front at `gait + 0.5` (bit 2). update() sets the bits;
// whoever reads them clears them, and nothing here depends on anyone doing
// so. Not a knob and not in the rest list: it lives for one frame.
static uint8_t stepPulse = 0;

static constexpr float IDLE_WRAP = 62.83185307179586f;   // 20*PI: sin(idle*0.6/2.2/0.9) all close
static constexpr float BLINK_WRAP = 3.7f;
static constexpr float DRIFT_WRAP = 2960.0f;              // NOT 1480 (0.55*1480 = 5.5*148)

static constexpr int TAIL_N = 38;
struct TailPoint { float x, y, r; };
static TailPoint pts[TAIL_N];

// Tail curl per segment, 0.08 + 1.15*u^1.2 with u = k/38, and the eyelid
// crests in local coordinates (bases on the eye rim at -135/-90/-45 degrees,
// apexes leaning out -8/0/+8 degrees). Both are constant: generated once by a
// script from the JS formulas (double precision, printed to 9 digits).
static const float TAIL_CURL[TAIL_N] = {
  0.08f, 0.0946203418f, 0.113588725f, 0.134639036f, 0.157166626f, 0.180860596f,
  0.205527542f, 0.231034156f, 0.257282354f, 0.284196614f, 0.311716801f, 0.339793779f,
  0.368386562f, 0.397460391f, 0.426985373f, 0.456935495f, 0.487287896f, 0.518022309f,
  0.549120629f, 0.580566574f, 0.612345417f, 0.644443762f, 0.676849372f, 0.709551014f,
  0.742538339f, 0.775801779f, 0.809332458f, 0.843122118f, 0.877163053f, 0.911448055f,
  0.945970365f, 0.980723631f, 1.01570187f, 1.05089944f, 1.08631101f, 1.12193152f,
  1.15775619f, 1.19378046f,
};
static constexpr float TAIL_INV_N = 1.0f / (float)TAIL_N;

static const float CREST_TRI[3][6] = {   // three corners (x, y)
  {-38.6870058f, -18.0610173f, -44.0610173f, -12.6870058f, -46.3255517f, -19.1052647f},
  {-32.2f, -17.6f, -39.8f, -17.6f, -36.0f, -23.8f},
  {-27.9389827f, -12.6870058f, -33.3129942f, -18.0610173f, -25.6744483f, -19.1052647f},
};
static const float CREST_TIP[3][2] = {   // tip disc centre (radius 1.4)
  {-45.207462f, -18.2627236f},
  {-36.0f, -22.4f},
  {-26.792538f, -18.2627236f},
};

static constexpr float FLOWER_COS[6] = {1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f};
static constexpr float FLOWER_SIN[6] = {0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f};

// Box around every non-background pixel drawn since the flowers: the outline
// pass only looks inside it (everything outside has rank 0 and never inks).
static int boxX0, boxY0, boxX1, boxY1;

static inline int iceil(float x) { return -PFMath::ifloor(-x); }

static inline void mark(int x0, int y0, int x1, int y1) {
  if (x0 < boxX0) boxX0 = x0;
  if (y0 < boxY0) boxY0 = y0;
  if (x1 > boxX1) boxX1 = x1;
  if (y1 > boxY1) boxY1 = y1;
}

// Every row of a disc is one contiguous run: walk in from both ends to the
// first inside pixel, then fill the run. Same pixels as testing each one.
static void disc(float cx, float cy, float r, uint8_t c) {
  float r2 = r * r;
  int y0 = PFMath::ifloor(cy - r); if (y0 < 0) y0 = 0;
  int y1 = iceil(cy + r);          if (y1 > BUF_H - 1) y1 = BUF_H - 1;
  int x0 = PFMath::ifloor(cx - r); if (x0 < 0) x0 = 0;
  int x1 = iceil(cx + r);          if (x1 > BUF_W - 1) x1 = BUF_W - 1;
  if (x0 > x1 || y0 > y1) return;
  mark(x0, y0, x1, y1);
  for (int y = y0; y <= y1; y++) {
    float dy = (float)y + 0.5f - cy;
    float rem = r2 - dy * dy;
    if (rem < 0.0f) continue;
    int xl = x0, xr = x1;
    while (xl <= xr) { float dx = (float)xl + 0.5f - cx; if (dx * dx <= rem) break; xl++; }
    if (xl > xr) continue;
    while (xr > xl) { float dx = (float)xr + 0.5f - cx; if (dx * dx <= rem) break; xr--; }
    memset(buf + y * BUF_W + xl, c, (size_t)(xr - xl + 1));
  }
}

static void ellipse(float cx, float cy, float rx, float ry, uint8_t c) {
  int y0 = PFMath::ifloor(cy - ry); if (y0 < 0) y0 = 0;
  int y1 = iceil(cy + ry);          if (y1 > BUF_H - 1) y1 = BUF_H - 1;
  int x0 = PFMath::ifloor(cx - rx); if (x0 < 0) x0 = 0;
  int x1 = iceil(cx + rx);          if (x1 > BUF_W - 1) x1 = BUF_W - 1;
  if (x0 > x1 || y0 > y1) return;
  mark(x0, y0, x1, y1);
  float irx = 1.0f / rx, iry = 1.0f / ry;
  for (int y = y0; y <= y1; y++) {
    float dy = ((float)y + 0.5f - cy) * iry;
    float rem = 1.0f - dy * dy;
    if (rem < 0.0f) continue;
    int xl = x0, xr = x1;
    while (xl <= xr) { float dx = ((float)xl + 0.5f - cx) * irx; if (dx * dx <= rem) break; xl++; }
    if (xl > xr) continue;
    while (xr > xl) { float dx = ((float)xr + 0.5f - cx) * irx; if (dx * dx <= rem) break; xr--; }
    memset(buf + y * BUF_W + xl, c, (size_t)(xr - xl + 1));
  }
}

static void capsule(float x0, float y0, float x1, float y1, float r0, float r1, uint8_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int n = iceil(len * 1.5f);
  if (n < 1) n = 1;
  float invN = 1.0f / (float)n;
  float dr = r1 - r0;
  for (int i = 0; i <= n; i++) {
    float t = (float)i * invN;
    disc(x0 + dx * t, y0 + dy * t, r0 + dr * t, c);
  }
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint8_t c) {
  float mnx = x0 < x1 ? x0 : x1; if (x2 < mnx) mnx = x2;
  float mxx = x0 > x1 ? x0 : x1; if (x2 > mxx) mxx = x2;
  float mny = y0 < y1 ? y0 : y1; if (y2 < mny) mny = y2;
  float mxy = y0 > y1 ? y0 : y1; if (y2 > mxy) mxy = y2;
  int minX = PFMath::ifloor(mnx); if (minX < 0) minX = 0;
  int maxX = iceil(mxx);          if (maxX > BUF_W - 1) maxX = BUF_W - 1;
  int minY = PFMath::ifloor(mny); if (minY < 0) minY = 0;
  int maxY = iceil(mxy);          if (maxY > BUF_H - 1) maxY = BUF_H - 1;

  float det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
  if (fabsf(det) < 0.0001f) return;
  if (minX > maxX || minY > maxY) return;
  mark(minX, minY, maxX, maxY);
  float inv = 1.0f / det;
  float a0 = (y1 - y2) * inv, b0 = (x2 - x1) * inv;
  float a1 = (y2 - y0) * inv, b1 = (x0 - x2) * inv;
  for (int y = minY; y <= maxY; y++) {
    float py = (float)y + 0.5f - y2;
    float r0 = b0 * py, r1 = b1 * py;
    uint8_t* row = buf + y * BUF_W;
    for (int x = minX; x <= maxX; x++) {
      float px = (float)x + 0.5f - x2;
      float w0 = a0 * px + r0;
      float w1 = a1 * px + r1;
      float w2 = 1.0f - w0 - w1;
      if (w0 >= -0.01f && w1 >= -0.01f && w2 >= -0.01f) row[x] = c;
    }
  }
}

// Horizontal cut with an integer row count per column: stable at any scale.
static void mouthCut(float xa, float ya, float xb, float yb, float ra, float rb, uint8_t c) {
  int x0 = PFMath::ifloor((xa < xb ? xa : xb) + 0.5f);
  int x1 = PFMath::ifloor((xa > xb ? xa : xb) + 0.5f);
  int xs = x0 < 0 ? 0 : x0;
  int xe = x1 > BUF_W - 1 ? BUF_W - 1 : x1;
  float invSpan = (x1 == x0) ? 0.0f : 1.0f / (float)(x1 - x0);
  for (int x = xs; x <= xe; x++) {
    float t = (float)(x - x0) * invSpan;
    if (xa > xb) t = 1.0f - t;
    float yc = ya + (yb - ya) * t;
    int rows = PFMath::ifloor(2.0f * (ra + (rb - ra) * t) + 0.5f);
    if (rows < 1) rows = 1;
    int top = PFMath::ifloor(yc - (float)rows * 0.5f + 0.5f);
    int ys = top < 0 ? 0 : top;
    int ye = top + rows > BUF_H ? BUF_H : top + rows;
    for (int y = ys; y < ye; y++) buf[y * BUF_W + x] = c;
  }
}

static void flower(float cx, float cy, float s, uint8_t c) {
  float pr = s * 0.55f;
  disc(cx, cy, pr, c);
  for (int k = 0; k < 6; k++) disc(cx + FLOWER_COS[k] * s, cy + FLOWER_SIN[k] * s, pr, c);
}

// Local gecko coordinates -> buffer: X(lx) = xA + xB*lx, Y(ly) = yA + yB*ly
// (the JS's baseCx + (focus + (l - focus)*s)*-dir, folded once per frame).
static float xA, xB, yA, yB, sc;

static inline float X(float lx) { return xA + xB * lx; }
static inline float Y(float ly) { return yA + yB * ly; }
static inline void T(float x0, float y0, float x1, float y1, float r0, float r1, uint8_t c) {
  capsule(X(x0), Y(y0), X(x1), Y(y1), r0 * sc, r1 * sc, c);
}
static inline void E(float x0, float y0, float rx, float ry, uint8_t c) {
  ellipse(X(x0), Y(y0), rx * sc, ry * sc, c);
}
static inline void D(float x0, float y0, float r, uint8_t c) {
  disc(X(x0), Y(y0), r * sc, c);
}

static void drawLeg(float hipx, float hipy, float phase, bool isFront, float ground, uint8_t c) {
  const float stride = 8.5f, lift = 4.2f;
  float fx, fy;
  float p = PFMath::fract(phase);
  if (p < 0.62f) {
    float q = p * (1.0f / 0.62f);
    fx = hipx - stride * 0.5f + stride * q;
    fy = ground;
  } else {
    float q2 = (p - 0.62f) * (1.0f / 0.38f);
    fx = hipx + stride * 0.5f - stride * q2;
    fy = ground - lift * PFMath::fastSin(q2 * PI);
  }

  const float L = 8.2f;
  float dx = fx - hipx, dy = fy - hipy;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 2.0f * L - 0.5f) {
    float k = (2.0f * L - 0.5f) / d;
    dx *= k; dy *= k;
    d = 2.0f * L - 0.5f;
  }
  float mx = hipx + dx * 0.5f, my = hipy + dy * 0.5f;
  float hh = L * L - d * d * 0.25f;
  float h = hh > 0.0f ? sqrtf(hh) : 0.0f;
  float invD = 1.0f / d;   // d >= 3 here: the hips sit >= 12 above the ground
  float nx = -dy * invD, ny = dx * invD;
  float bend = isFront ? 1.0f : -1.0f;
  if (nx * bend < 0.0f) { nx = -nx; ny = -ny; }
  float kx = mx + nx * h, ky = my + ny * h;
  float ax = hipx + dx, ay = hipy + dy;

  T(hipx, hipy, kx, ky, 3.3f, 2.8f, c);
  T(kx, ky, ax, ay, 2.8f, 2.4f, c);

  float lifted = fy < ground - 0.5f ? 1.0f : 0.0f;
  T(ax, ay, ax - 4.6f, ay + 0.4f - lifted * 1.2f, 2.0f, 1.3f, c);
  T(ax, ay, ax - 4.0f, ay - 2.8f, 1.8f, 1.2f, c);
  T(ax, ay, ax - 2.6f, ay + 2.8f - lifted * 0.9f, 1.8f, 1.2f, c);
}

void setup() {
  PFMath::buildSinLUT();
  if (!buf) buf = (uint8_t*)PFMem::alloc((size_t)BUF_W * BUF_H);   // exhibit: 8 KB
  gait = 0.0f;
  idle = 0.0f;
  blinkT = 0.0f;
  drift = 0.0f;
  dir = -1.0f;
  rider = false;
  sizeVal = 1.05f;
  backdropVal = 0.65f;
  speedVal = 0.7f;
  unusedVal = 0.5f;
}

static inline float effectiveSpeed() {
  const float boost = speedVal > 0.7f ? speedVal - 0.7f : 0.0f;
  return speedVal + 4.8f * boost * boost;
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &sizeVal, 0.6f, 1.5f, 0.028f);
  PFParams::apply(input, 1, &backdropVal, 0.3f, 1.0f, 0.022f);
  PFParams::apply(input, 2, &speedVal, 0.2f, 1.2f, 0.031f);
  PFParams::apply(input, 3, &unusedVal, 0.0f, 1.0f, 0.03f);

  if (input.btnPressed[0] && !input.paramAbsoluteActive[0] && !input.knobAudioActive[0]) dir = -dir;
  if (input.btnPressed[1] && !input.paramAbsoluteActive[1] && !input.knobAudioActive[1]) rider = !rider;

  // The same advance drives the visible gait and MIDI landing detection.
  // Keep raw speedVal for the knob and idle-return state; map it only here.
  const float speed = effectiveSpeed();
  const float gaitWas = gait;
  const float advance = dt * speed * 1.5f;
  gait = PFMath::fract(gait + advance);
  if (advance > 0.0f) {
    if (PFMath::ifloor(gaitWas + advance) > 0) stepPulse |= 1;
    if (PFMath::ifloor(gaitWas + advance + 0.5f) > PFMath::ifloor(gaitWas + 0.5f)) stepPulse |= 2;
  }
  idle += dt;
  if (idle >= IDLE_WRAP) idle -= IDLE_WRAP;
  blinkT += dt;
  if (blinkT >= BLINK_WRAP) blinkT -= BLINK_WRAP;
  drift += dt * speed * 20.0f;
  if (drift >= DRIFT_WRAP) drift -= DRIFT_WRAP;
}

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);
  if (!buf) {   // allocation failed: a black panel, not a crash
    PFCanvas::clear();
    PFCanvas::present();
    return;
  }

  const float s = sizeVal;
  const float speed = effectiveSpeed();
  const float walk = speed < 1.0f ? speed : 1.0f;
  const float g = gait;

  float sparkAmount = PFMath::clamp01((backdropVal - 0.3f) / 0.7f);
  float bgLevel = PFMath::clamp01(sparkAmount * 2.0f);

  memset(buf, C_BG, (size_t)BUF_W * BUF_H);

  // Background flowers: 1 + ceil(f) of them; the newest starts at the
  // fractional part of f and grows, so none pops in.
  float f = sparkAmount * 8.0f;
  int fi = PFMath::ifloor(f);
  int nSpark = 1 + iceil(f);
  float lastScale = f > (float)fi ? f - (float)fi : 1.0f;
  float allScale = 1.0f + 0.35f * sparkAmount;
  for (int i = 0; i < nSpark; i++) {
    float sx = PFMath::jsMod((float)(i * 41 + 7) + dir * -drift * (0.55f + 0.25f * (float)(i % 3)),
                             (float)(BUF_W + 20));
    if (sx < 0.0f) sx += (float)(BUF_W + 20);
    sx -= 10.0f;
    static const float flowerRows[4] = {7.0f, 56.0f, 15.0f, 49.0f};
    float sy = flowerRows[i % 4];
    float fs = (3.0f + (float)(i % 2) * 0.9f) * allScale;
    if (i == nSpark - 1 && i > 0) fs *= lastScale;
    flower(sx, sy, fs, C_SPARK);
    disc(sx, sy, fs * 0.4f, C_BG);
  }

  // Everything from here on is the gecko: start the outline box.
  boxX0 = BUF_W; boxY0 = BUF_H; boxX1 = -1; boxY1 = -1;

  // Direction flips the margin too (dir = -1 is the original).
  float baseCx = (float)BUF_W * 0.5f - 6.0f * dir + PFMath::fastSin(idle * 0.6f) * 1.5f;
  float bob = -fabsf(PFMath::fastSin(g * TWO_PI)) * 1.2f * walk;
  float baseCy = 27.5f + bob;
  const float ground = 17.0f;

  // Zooming in (s > 1) moves the anchor towards (-30, -17); at or below 1 it stays at (0, 0).
  float zoomFactor = s > 1.0f ? (s - 1.0f) * 2.0f : 0.0f;
  float focusX = -30.0f * zoomFactor;
  float focusY = -17.0f * zoomFactor;
  sc = s;
  xB = -dir * s;
  xA = baseCx - dir * (focusX - focusX * s);
  yB = s;
  yA = baseCy + (focusY - focusY * s);

  const float ex = -36.0f, ey = -10.0f, eyeR = 8.6f;

  // Far front & hind legs
  drawLeg(-27.0f, 3.5f, g + 0.5f, true, ground, C_FARLEG);
  drawLeg(2.0f, 5.0f, g, false, ground, C_FARLEG);

  // Tail
  {
    float tx = 4.0f, ty = 2.0f;
    const float seg = 1.45f;
    float idleSway = PFMath::fastSin(idle * 2.2f) * 0.08f + PFMath::fastSin(idle * 0.9f) * 0.04f;
    float gPhase = g * TWO_PI;
    float waveAmp = 0.22f * walk;
    for (int k = 0; k < TAIL_N; k++) {
      float u = (float)k * TAIL_INV_N;
      float tailWave = PFMath::fastSin(u * 5.2f - gPhase) * waveAmp;
      float a = TAIL_CURL[k] + idleSway * (0.4f + 0.6f * u) + tailWave * u;
      float nx2 = tx + PFMath::fastCos(a) * seg;
      float ny2 = ty + PFMath::fastSin(a) * seg;
      float r = 6.4f * (1.0f - u * 0.82f) + 1.2f;
      T(tx, ty, nx2, ny2, r, r, C_BODY);
      pts[k].x = nx2; pts[k].y = ny2; pts[k].r = r;
      tx = nx2; ty = ny2;
    }
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

  // Near hind & front legs
  drawLeg(2.0f, 5.0f, g + 0.5f, false, ground, C_BODY);
  drawLeg(-27.0f, 3.5f, g, true, ground, C_BODY);

  // Flank stripe dots (bq % 5 < 3 of 16 steps from (-23,-7.5) to (4,-2.8))
  for (int bq = 0; bq < 16; bq++) {
    if (bq % 5 < 3) {
      float bt = (float)bq * (1.0f / 15.0f);
      D(-23.0f + bt * 27.0f, -7.5f + bt * 4.7f, 1.0f, C_STRIPE);
    }
  }

  // Tail stripe dots
  for (int q = 0; q < TAIL_N - 4; q++) {
    if (q % 5 < 3) {
      float pR = pts[q].r;
      float rr = pR * 0.25f;
      D(pts[q].x, pts[q].y - pR * 0.55f, rr < 1.0f ? rr : 1.0f, C_STRIPE);
    }
  }

  // Open mouth: an integer row count per column (2 at the back -> 1 at the front).
  mouthCut(X(-53.0f), Y(1.5f), X(-37.0f), Y(2.0f), 0.9f * s, 0.4f * s, C_BG);

  // Mouse rider
  float mb = 0.0f;
  if (rider) {
    mb = PFMath::fastSin(g * 4.0f * PI + 1.2f) * 0.6f * walk;
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

  // Eyelid crests (before the eye: the eye disc covers where their bases meet)
  for (int ci = 0; ci < 3; ci++) {
    const float* t = CREST_TRI[ci];
    triangle(X(t[0]), Y(t[1]), X(t[2]), Y(t[3]), X(t[4]), Y(t[5]), C_CREST);
    D(CREST_TIP[ci][0], CREST_TIP[ci][1], 1.4f, C_CREST);
  }

  // Eye disc (on its own 3.7 s blink clock)
  bool blink = blinkT > 3.5f && blinkT < 3.66f;
  if (!blink) D(ex, ey, eyeR, C_EYE);

  // ── Outline ink pass ──
  // A ranked pixel next to a lower-ranked one turns to ink, judged on the
  // picture before any ink. Row by row: `prev` keeps the row above as it was,
  // `cur` this row as it was, and the row below is still untouched in buf.
  if (boxX1 >= boxX0) {
    uint8_t prevRow[BUF_W], curRow[BUF_W];
    int bx0 = boxX0, bx1 = boxX1;
    if (boxY0 > 0) memcpy(prevRow, buf + (boxY0 - 1) * BUF_W, BUF_W);
    for (int y = boxY0; y <= boxY1; y++) {
      uint8_t* row = buf + y * BUF_W;
      memcpy(curRow, row, BUF_W);
      const uint8_t* below = (y < BUF_H - 1) ? row + BUF_W : nullptr;
      for (int x = bx0; x <= bx1; x++) {
        uint8_t c = curRow[x];
        uint8_t rk = RANK[c];
        if (rk == 0 || c == C_INK) continue;
        if ((x > 0 && RANK[curRow[x - 1]] < rk) || (x < BUF_W - 1 && RANK[curRow[x + 1]] < rk) ||
            (y > 0 && RANK[prevRow[x]] < rk) || (below && RANK[below[x]] < rk)) {
          row[x] = C_INK;
        }
      }
      memcpy(prevRow, curRow, BUF_W);
    }
  }

  // Ink details
  if (blink) {
    T(ex - 6.0f, ey + 1.0f, ex + 6.0f, ey + 1.0f, 1.1f, 1.1f, C_INK);
  } else {
    // Pupil: a vertical band 40% of the eye's width, only inside the eye
    float pw = eyeR * 0.4f * s, pcx = X(ex), pcy = Y(ey), pr = (eyeR - 0.8f) * s;
    float pr2 = pr * pr;
    int py0 = PFMath::ifloor(pcy - pr); if (py0 < 0) py0 = 0;
    int py1 = iceil(pcy + pr);          if (py1 > BUF_H - 1) py1 = BUF_H - 1;
    int px0 = PFMath::ifloor(pcx - pw); if (px0 < 0) px0 = 0;
    int px1 = iceil(pcx + pw);          if (px1 > BUF_W - 1) px1 = BUF_W - 1;
    for (int py2 = py0; py2 <= py1; py2++) {
      float ddy = (float)py2 + 0.5f - pcy;
      float ddy2 = ddy * ddy;
      uint8_t* row = buf + py2 * BUF_W;
      for (int px2 = px0; px2 <= px1; px2++) {
        float ddx = (float)px2 + 0.5f - pcx;
        if (fabsf(ddx) <= pw && ddx * ddx + ddy2 <= pr2 && row[px2] == C_EYE) row[px2] = C_INK;
      }
    }
  }

  // Ear dot
  D(-24.0f, -4.0f, 1.6f, C_INK);

  // Mouse face
  if (rider) {
    float ex2 = -10.0f, ey2 = -12.5f + mb;
    T(ex2 - 8.5f, ey2 - 1.5f, ex2 - 7.0f, ey2 - 2.6f, 0.55f, 0.55f, C_INK);
    T(ex2 - 7.0f, ey2 - 2.6f, ex2 - 5.5f, ey2 - 1.5f, 0.55f, 0.55f, C_INK);
    T(ex2 - 0.5f, ey2 - 1.5f, ex2 + 1.0f, ey2 - 2.6f, 0.55f, 0.55f, C_INK);
    T(ex2 + 1.0f, ey2 - 2.6f, ex2 + 2.5f, ey2 - 1.5f, 0.55f, 0.55f, C_INK);
    D(ex2 - 12.5f, ey2 + 2.6f, 0.8f, C_INK);
  }

  // ── RGB ──
  // Portrait frame (64x128): the 128x64 buffer turned 90 degrees
  // counter-clockwise, buffer (x, y) -> frame (y, BUF_W-1-x). Head down, feet
  // to the right, as the artist set it - do not change.
  uint8_t col[12][3];
  memcpy(col, PALETTE, sizeof(col));
  for (int ch = 0; ch < 3; ch++) {
    col[C_BG][ch] = (uint8_t)((float)PALETTE[C_BG][ch] * bgLevel + 0.5f);
    col[C_SPARK][ch] = (uint8_t)((float)PALETTE[C_SPARK][ch] * bgLevel + 0.5f);
  }
  for (int y2 = 0; y2 < BUF_H; y2++) {
    const uint8_t* row = buf + y2 * BUF_W;
    for (int x2 = 0; x2 < BUF_W; x2++) {
      const uint8_t* cc = col[row[x2]];
      PFCanvas::setPixel(y2, BUF_W - 1 - x2, cc[0], cc[1], cc[2]);
    }
  }

  PFCanvas::present();
}

} // namespace HEOEUNBIN

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
