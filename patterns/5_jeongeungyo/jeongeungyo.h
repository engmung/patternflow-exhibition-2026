#pragma once

// ===== Patternflow pattern =====
// Title:    JEONGEUNGYO
// Author:   @engmung
// Date:     2026-09-19
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================
//
// exhibit: patterns_js/new/jeongeungyo.js 의 번역. 그림은 JS 와 같고, 픽셀 계산만 바꿨다.
//   - JS 는 픽셀마다 링크 12개(나눗셈 포함)와 원반 9개를 전부 검사한다. 여기서는 행마다
//     "이 행에서 이 링크/원반이 걸칠 수 있는 x 구간"을 먼저 구하고(회전 좌표가 x, y 에 대해
//     선형이라 구간은 곱셈 몇 번), 그 구간(+-1px 여유) 안의 픽셀만 JS 와 같은 식으로 검사한다.
//     나머지 픽셀은 바탕색 그대로.
//   - 원반 돔 cos(normDist * 1.5708) 은 u = distSq/R^2 의 6차 급수로(sqrt 없음) 바꿨다.
//     이것은 근사다: u <= 1 에서 오차 < 1e-8 이라 화면에는 차이가 없다.
//   - 거미줄 살 |sin(5*atan2)| 는 (dx + i dy)^5 의 허수부로(각도 없음), 고리
//     |sin(a)| > 0.82 는 a/pi 의 소수부 구간 비교로 바꿨다. 이 둘은 근사가 아니라
//     같은 식이다.

#include <Arduino.h>
#include "config.h"
#include "src/core_display.h"
#include "src/core_encoders.h"
#include "src/core_canvas.h"
#include "src/core_params.h"
#include "src/core_math.h"

namespace JEONGEUNGYO {

const char* NAME = "JEONGEUNGYO";
const char* const KNOB_LABELS[4] = {"State", "Rotation", "DriftSpeed", "-"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FRAME_W = 64;
constexpr int FRAME_H = 128;

// @ramp oklab 0:#000000 0.271:#cfc095 0.309:#000000 0.354:#878787 0.475:#9c9c9c 0.519:#e1d2a8 0.971:#feeabe
// (buildRampLUTRGBA 로 생성, i/255 샘플)
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
  {254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190},{254,234,190}
};

struct NodeSeed {
  float aloneX, aloneY, px, py, fx, fy, ampX, ampY;
};

struct LinkDef {
  uint8_t nA, pA, nB, pB;
};

struct NodeState {
  float cx, cy;
  float portX[4], portY[4];
};

// 프레임마다 미리 계산하는 링크 선분 (시작점, 방향, 길이², 1/길이², 활성 여부)
struct Seg {
  bool on;
  float ax, ay, abx, aby, lenSq, inv;
};

// State = 0.99 일 때 모이는 화면 중앙 3x3 격자 타깃 좌표 (중심 32, 64)
static const float GRID_X[9] = { 13.5f, 32.0f, 50.5f, 13.5f, 32.0f, 50.5f, 13.5f, 32.0f, 50.5f };
static const float GRID_Y[9] = { 45.5f, 45.5f, 45.5f, 64.0f, 64.0f, 64.0f, 82.5f, 82.5f, 82.5f };

// State = 0.0 일 때 화폭 전체를 누비는 분산 앵커 및 진폭/주파수 시드
static const NodeSeed NODE_SEEDS[9] = {
  { 16.0f,  22.0f, 0.14f, 0.19f, 1.25f, 1.60f, 13.0f, 17.0f },
  { 32.0f,  18.0f, 0.82f, 0.35f, 1.45f, 1.10f, 15.0f, 14.0f },
  { 48.0f,  24.0f, 0.41f, 0.77f, 1.10f, 1.50f, 13.0f, 17.0f },
  { 12.0f,  62.0f, 0.63f, 0.21f, 1.65f, 1.30f, 12.0f, 19.0f },
  { 32.0f,  64.0f, 0.00f, 0.00f, 0.95f, 1.15f,  9.0f, 11.0f }, // 4번 중앙 거미줄 노드
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
  // 수평 외곽 링크
  {0, 3, 1, 2}, {1, 3, 2, 2},
  {6, 3, 7, 2}, {7, 3, 8, 2},
  // 수직 외곽 링크
  {0, 1, 3, 0}, {2, 1, 5, 0},
  {3, 1, 6, 0}, {5, 1, 8, 0},
  // 중앙(4번) 연결 4대 실선
  {3, 3, 4, 2}, // 중앙-좌 (State < 0.25에서 끊김)
  {1, 1, 4, 0}, // 중앙-상 (State < 0.45에서 끊김)
  {4, 3, 5, 2}, // 중앙-우 (State < 0.65에서 끊김)
  {4, 1, 7, 0}  // 중앙-하 (State < 0.85에서 끊김)
};

constexpr float BASE_RADIUS = 9.5f;
constexpr float R_SQ = BASE_RADIUS * BASE_RADIUS;
constexpr float P_OFFSET = BASE_RADIUS * 0.82f;
constexpr float BG_CAP = 0.18f;
constexpr float JS_PI = 3.14159265358979f;
// C++ 번역 시: 이 주기(400π, 2π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
// float32 로는 400π/2π 가 정확히 안 맞아서 hi + lo 두 조각으로 뺀다 (JS 의 double 주기와 같은 값).
// hi = float32(400π), lo = 400π - hi (2π 도 같은 방식, python 으로 계산)
constexpr float T_WRAP = 1256.63708f;
constexpr float T_WRAP_LO = -2.35250209e-05f;
constexpr float R_WRAP = 6.28318548f;
constexpr float R_WRAP_LO = -1.74845553e-07f;

// 보정 합(Kahan): float32 누적기가 수 분 동안 JS(double) 보다 조금씩 밀리지 않게.
// 프레임당 몇 번뿐인 덧셈이라 비용은 없다.
static inline void kahanAdd(float& sum, float& comp, float x) {
  const float y = x - comp;
  const float t = sum + y;
  comp = (t - sum) - y;
  sum = t;
}

// 원반 돔 cos(normDist * 1.5708), normDist = sqrt(u), u = distSq / R^2.
// cos(k*sqrt(u)) = sum (-1)^n (k^2 u)^n / (2n)! : sqrt 없이 u 의 다항식. n = 6 까지면 u <= 1 에서 오차 < 1e-8.
constexpr float DOME_Z = 1.5708f * 1.5708f;
constexpr float DOME_C1 = -DOME_Z / 2.0f;
constexpr float DOME_C2 = DOME_Z * DOME_Z / 24.0f;
constexpr float DOME_C3 = -DOME_Z * DOME_Z * DOME_Z / 720.0f;
constexpr float DOME_C4 = DOME_Z * DOME_Z * DOME_Z * DOME_Z / 40320.0f;
constexpr float DOME_C5 = -DOME_Z * DOME_Z * DOME_Z * DOME_Z * DOME_Z / 3628800.0f;
constexpr float DOME_C6 = DOME_Z * DOME_Z * DOME_Z * DOME_Z * DOME_Z * DOME_Z / 479001600.0f;
constexpr float INV_R_SQ = 1.0f / R_SQ;

// 노브 (K1-K3 은 PFParams::apply 로 lo + (hi-lo)*위치). K4 는 없음.
static float paramState = 0.495f;     // K1 State 0 ~ 0.99
static float paramRotation = 0.6f;    // K2 Rotation -0.9 ~ 2.1 (rad/s)
static float paramDriftSpeed = 2.0f;  // K3 DriftSpeed 0.4 ~ 3.6

static float animT = 0.0f;    // [0, 400π)
static float animRot = 0.0f;  // [0, 2π)
static float animTComp = 0.0f;    // animT 의 Kahan 보정값
static float animRotComp = 0.0f;  // animRot 의 Kahan 보정값
// K1 누르기 봉투: pressEnv 0..1 (원시값), pressRising = 오르는 중. 대기 복귀 때 0 / false 로 되돌린다.
static float pressEnv = 0.0f;
static bool pressRising = false;
static float stateNow = 0.5f;      // 누르기로 흩어진 만큼 뺀 실제 모임 상태
static float knobStateNow = 0.5f;  // 바탕 밝기용 (누르기와 무관)
static float ringAlpha = 0.0f;     // asin(0.82) / π

static NodeState nodes[9];
static Seg segs[12];

void setup() {
  animT = 0.0f;
  animRot = 0.0f;
  animTComp = 0.0f;
  animRotComp = 0.0f;
  pressEnv = 0.0f;
  pressRising = false;
  stateNow = 0.5f;
  knobStateNow = 0.5f;
  ringAlpha = asinf(0.82f) / JS_PI;
  for (int i = 0; i < 9; i++) {
    nodes[i].cx = GRID_X[i];
    nodes[i].cy = GRID_Y[i];
    for (int k = 0; k < 4; k++) { nodes[i].portX[k] = GRID_X[i]; nodes[i].portY[k] = GRID_Y[i]; }
  }
  for (int l = 0; l < 12; l++) segs[l].on = false;
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &paramState, 0.0f, 0.99f, 0.02f);
  PFParams::apply(input, 1, &paramRotation, -0.9f, 2.1f, 0.06f);
  PFParams::apply(input, 2, &paramDriftSpeed, 0.4f, 3.6f, 0.064f);

  // JS: Math.min(dt || 0.016, 0.05)
  float safeDt = (dt > 0.0f) ? dt : 0.016f;
  if (safeDt > 0.05f) safeDt = 0.05f;
  const float speed = paramDriftSpeed;

  // K1 누르기 봉투 (오름 0.15초, 내림 2.2초, 0..1 로 잘라 누적되지 않음)
  if (input.btnPressed[0]) pressRising = true;
  float pe = pressEnv;
  if (pressRising) {
    pe += safeDt / 0.15f;
    if (pe >= 1.0f) { pe = 1.0f; pressRising = false; }
  } else {
    pe -= safeDt / 2.2f;
    if (pe < 0.0f) pe = 0.0f;
  }
  pressEnv = pe;
  const float e = pe * pe * (3.0f - 2.0f * pe); // 부드러운 모양 (0, 1 양끝에서 기울기 0)

  // e 가 있는 동안 유영 위상을 더 빨리 돌린다 (최소 +1.2, 느릴수록 최대 +4.0)
  kahanAdd(animT, animTComp, safeDt * (speed + e * fmaxf(1.2f, 4.4f - speed)));
  // animT 는 cos/sin(t*f) 에만 쓰인다. 400π 가 f 전부에 정확히 맞는 가장 짧은 주기.
  if (animT >= T_WRAP) { kahanAdd(animT, animTComp, -T_WRAP); kahanAdd(animT, animTComp, -T_WRAP_LO); }
  else if (animT < 0.0f) { kahanAdd(animT, animTComp, T_WRAP); kahanAdd(animT, animTComp, T_WRAP_LO); }

  float knobState = paramState;
  if (knobState < 0.0f) knobState = 0.0f;
  if (knobState > 0.99f) knobState = 0.99f;
  knobStateNow = knobState;
  const float state = knobState * (1.0f - e);
  stateNow = state;

  kahanAdd(animRot, animRotComp, safeDt * paramRotation);
  if (animRot >= R_WRAP) { kahanAdd(animRot, animRotComp, -R_WRAP); kahanAdd(animRot, animRotComp, -R_WRAP_LO); }
  else if (animRot < 0.0f) { kahanAdd(animRot, animRotComp, R_WRAP); kahanAdd(animRot, animRotComp, R_WRAP_LO); }

  const float t = animT;
  // 누르기 동안 유영 폭도 잠깐 넓어진다 (최대 1.3배)
  const float ampBoost = 1.0f + 0.3f * e;
  const float easeState = state * state * (3.0f - 2.0f * state);

  for (int i = 0; i < 9; i++) {
    const NodeSeed& s = NODE_SEEDS[i];
    NodeState& nd = nodes[i];

    // 고조파를 합성한 궤적 (프레임당 36회라 정확한 cosf/sinf)
    const float wave1X = cosf(s.px * 6.28f + t * s.fx);
    const float wave2X = sinf(t * (s.fx * 1.8f) + s.py * 3.14f) * 0.35f;
    const float driftX = (wave1X + wave2X) * s.ampX * ampBoost;

    const float wave1Y = sinf(s.py * 6.28f + t * s.fy);
    const float wave2Y = cosf(t * (s.fy * 1.7f) + s.px * 3.14f) * 0.35f;
    const float driftY = (wave1Y + wave2Y) * s.ampY * ampBoost;

    const float aloneX = s.aloneX + driftX;
    const float aloneY = s.aloneY + driftY;

    // State가 올라갈수록 격자 위치로 감쇠 수렴 (+ 완성된 격자도 ±0.6px 숨쉬기)
    const float cx = aloneX + (GRID_X[i] - aloneX) * easeState + driftX * 0.03f * easeState;
    const float cy = aloneY + (GRID_Y[i] - aloneY) * easeState + driftY * 0.03f * easeState;

    nd.cx = cx;
    nd.cy = cy;
    // 4방향 슬릿 포트: 상, 하, 좌, 우
    nd.portX[0] = cx;            nd.portY[0] = cy - P_OFFSET;
    nd.portX[1] = cx;            nd.portY[1] = cy + P_OFFSET;
    nd.portX[2] = cx - P_OFFSET; nd.portY[2] = cy;
    nd.portX[3] = cx + P_OFFSET; nd.portY[3] = cy;
  }

  for (int l = 0; l < 12; l++) {
    const LinkDef& link = LINKS[l];
    Seg& sg = segs[l];

    // 중앙 연결 실선 순차 단선
    bool on = true;
    if (l == 8  && state < 0.25f) on = false;
    if (l == 9  && state < 0.45f) on = false;
    if (l == 10 && state < 0.65f) on = false;
    if (l == 11 && state < 0.85f) on = false;

    const float ax = nodes[link.nA].portX[link.pA];
    const float ay = nodes[link.nA].portY[link.pA];
    const float abx = nodes[link.nB].portX[link.pB] - ax;
    const float aby = nodes[link.nB].portY[link.pB] - ay;
    const float lenSq = abx * abx + aby * aby;
    if (lenSq < 0.1f) on = false;

    sg.on = on;
    if (!on) continue;
    sg.ax = ax; sg.ay = ay;
    sg.abx = abx; sg.aby = aby;
    sg.lenSq = lenSq;
    sg.inv = 1.0f / lenSq;
  }
}

// 링크 하나를 행 단위로 훑기 위한 선형식. 픽셀 (x, y) 에서
//   tProj = t0 + x*tx + y*ty,  cross(q, ab) = c0 + x*cx + y*cy,  선까지 거리 = |cross| / len.
struct LinkRow {
  const Seg* sg;
  float t0, tx, ty, c0, cx, cy, len;
  float invTx, invCx;
  bool useTx, useCx;
};

// 원반 하나: 행 y 에서 원반 중심까지의 수직 거리 h = h0 + y, 행 위 중심 x = xc (프레임 상수).
struct NodeRow {
  float h0, xc;
  int y0, y1;
};

void draw() {
  PFCanvas::setFrame(FRAME_W, FRAME_H);

  constexpr float CX0 = FRAME_W * 0.5f;
  constexpr float CY0 = FRAME_H * 0.5f;

  const float cosR = cosf(animRot);
  const float sinR = sinf(animRot);
  const float state = stateNow;
  const float t = animT;

  // [구역 1] 배경: 0.02 ~ BG_CAP, 노브 State 를 따른다 (누르기로 깜박이지 않게)
  float bgTone = 0.02f + (knobStateNow / 0.99f) * 0.24f;
  if (bgTone > BG_CAP) bgTone = BG_CAP;
  const bool webOn = state < 0.85f;
  const float webW = (1.0f - (state / 0.85f)) * 0.75f;
  const float webKeep = 1.0f - webW;
  const float webAdd = 0.35f * webW;

  // 고리 위상: |sin(dist*1.2 - t*0.7)| > 0.82  <=>  frac((dist*1.2 - t*0.7)/π) ∈ (α, 1-α)
  float ringOff = t * 0.7f * (1.0f / JS_PI);
  ringOff -= floorf(ringOff);
  constexpr float RING_K = 1.2f / JS_PI;
  const float ringLo = ringAlpha;
  const float ringHi = 1.0f - ringAlpha;

  // 회전 좌표: 픽셀 (x, y) -> P00 + x*D + y*E, D = (cosR, -sinR), E = (sinR, cosR)
  const float p00x = CX0 - CX0 * cosR - CY0 * sinR;
  const float p00y = CY0 + CX0 * sinR - CY0 * cosR;

  LinkRow lrs[12];
  int nLinks = 0;
  for (int l = 0; l < 12; l++) {
    const Seg& sg = segs[l];
    if (!sg.on) continue;
    LinkRow& r = lrs[nLinks++];
    const float wx = p00x - sg.ax;
    const float wy = p00y - sg.ay;
    r.sg = &sg;
    r.t0 = (wx * sg.abx + wy * sg.aby) * sg.inv;
    r.tx = (cosR * sg.abx - sinR * sg.aby) * sg.inv;
    r.ty = (sinR * sg.abx + cosR * sg.aby) * sg.inv;
    r.c0 = wx * sg.aby - wy * sg.abx;
    r.cx = cosR * sg.aby + sinR * sg.abx;
    r.cy = sinR * sg.aby - cosR * sg.abx;
    r.len = sqrtf(sg.lenSq);
    // 기울기가 거의 0 이면 행 전체에서 64*1e-4 < 0.01 만큼만 변한다 -> 여유 0.01 로 판정
    r.useTx = fabsf(r.tx) > 1e-4f;
    r.useCx = fabsf(r.cx) > 1e-4f;
    r.invTx = r.useTx ? 1.0f / r.tx : 0.0f;
    r.invCx = r.useCx ? 1.0f / r.cx : 0.0f;
  }

  NodeRow nrs[9];
  for (int i = 0; i < 9; i++) {
    const float wx = p00x - nodes[i].cx;
    const float wy = p00y - nodes[i].cy;
    NodeRow& r = nrs[i];
    r.xc = -(cosR * wx - sinR * wy);
    r.h0 = sinR * wx + cosR * wy;
    // |h0 + y| <= R 인 행만 (+-1행 여유, 판정은 픽셀에서 JS 식 그대로)
    int y0 = (int)floorf(-r.h0 - BASE_RADIUS) - 1;
    int y1 = (int)ceilf(-r.h0 + BASE_RADIUS) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > FRAME_H - 1) y1 = FRAME_H - 1;
    r.y0 = y0;
    r.y1 = y1;
  }

  int bgIdx = (int)(bgTone * 255.0f + 0.5f);
  const uint8_t bgR = RAMP_LUT[bgIdx][0];
  const uint8_t bgG = RAMP_LUT[bgIdx][1];
  const uint8_t bgB = RAMP_LUT[bgIdx][2];

  float row[FRAME_W];

  for (int y = 0; y < FRAME_H; y++) {
    const float vy = (float)y - CY0;
    const float baseRx = CX0 + vy * sinR;
    const float baseRy = CY0 + vy * cosR;
    const float fy = (float)y;
    int touchLo = FRAME_W, touchHi = -1;
    for (int x = 0; x < FRAME_W; x++) row[x] = bgTone;

    // [구역 2] 1. 단색 바느질 연결선: 0.37
    for (int n = 0; n < nLinks; n++) {
      const LinkRow& r = lrs[n];
      const float tr = r.t0 + fy * r.ty;
      const float cr = r.c0 + fy * r.cy;
      float xmin = -2.0f, xmax = (float)FRAME_W + 1.0f;
      if (r.useTx) {
        float a = -tr * r.invTx, b = (1.0f - tr) * r.invTx;
        if (a > b) { const float s = a; a = b; b = s; }
        if (a > xmin) xmin = a;
        if (b < xmax) xmax = b;
      } else if (tr < -0.01f || tr > 1.01f) {
        continue;
      }
      if (r.useCx) {
        float a = (-r.len - cr) * r.invCx, b = (r.len - cr) * r.invCx;
        if (a > b) { const float s = a; a = b; b = s; }
        if (a > xmin) xmin = a;
        if (b < xmax) xmax = b;
      } else if (fabsf(cr) > r.len + 0.01f) {
        continue;
      }
      if (xmin > xmax) continue;
      int x0 = (int)floorf(xmin) - 1;
      int x1 = (int)ceilf(xmax) + 1;
      if (x0 < 0) x0 = 0;
      if (x1 > FRAME_W - 1) x1 = FRAME_W - 1;
      const Seg& sg = *r.sg;
      for (int x = x0; x <= x1; x++) {
        const float ux = (float)x - CX0;
        const float qx = baseRx + ux * cosR - sg.ax;
        const float qy = baseRy - ux * sinR - sg.ay;
        const float tProj = (qx * sg.abx + qy * sg.aby) * sg.inv;
        if (tProj >= 0.0f && tProj <= 1.0f) {
          const float dx = qx - tProj * sg.abx;
          const float dy = qy - tProj * sg.aby;
          if (dx * dx + dy * dy < 1.0f) {
            row[x] = 0.37f;
            if (x < touchLo) touchLo = x;
            if (x > touchHi) touchHi = x;
          }
        }
      }
    }

    // 2. 도자 원반 노드 & 슬릿 홈 & 거미줄
    for (int i = 0; i < 9; i++) {
      const NodeRow& r = nrs[i];
      if (y < r.y0 || y > r.y1) continue;
      const float h = r.h0 + fy;
      const float d2 = R_SQ - h * h;
      const float half = d2 > 0.0f ? sqrtf(d2) : 0.0f;
      int x0 = (int)floorf(r.xc - half) - 1;
      int x1 = (int)ceilf(r.xc + half) + 1;
      if (x0 < 0) x0 = 0;
      if (x1 > FRAME_W - 1) x1 = FRAME_W - 1;
      if (x0 > x1) continue;

      const NodeState& nd = nodes[i];
      const float tone0 = TONE_OFFSETS[i];
      for (int x = x0; x <= x1; x++) {
        const float ux = (float)x - CX0;
        const float dx = baseRx + ux * cosR - nd.cx;
        const float dy = baseRy - ux * sinR - nd.cy;
        const float distSq = dx * dx + dy * dy;
        if (distSq > R_SQ) continue;

        const float u = distSq * INV_R_SQ;
        const float dome = 1.0f + u * (DOME_C1 + u * (DOME_C2 + u * (DOME_C3 + u * (DOME_C4 + u * (DOME_C5 + u * DOME_C6)))));
        float diskTone = 0.52f + 0.33f * dome + tone0;

        // 가장자리 슬릿 홈: 0.32. 상/하 홈은 |dx| < 1, 좌/우 홈은 |dy| < 1 일 때만 닿을 수 있다.
        const float adx = fabsf(dx), ady = fabsf(dy);
        bool slit = false;
        if (adx < 1.0f) {
          for (int k = 0; k < 2; k++) {
            const float px = dx;
            const float py = (baseRy - ux * sinR) - nd.portY[k];
            const float cl = py > 2.3f ? 2.3f : (py < -2.3f ? -2.3f : py);
            const float ly = py - cl;
            if (px * px + ly * ly < 1.0f) { slit = true; break; }
          }
        }
        if (!slit && ady < 1.0f) {
          for (int k = 2; k < 4; k++) {
            const float px = (baseRx + ux * cosR) - nd.portX[k];
            const float py = dy;
            const float cl = px > 2.3f ? 2.3f : (px < -2.3f ? -2.3f : px);
            const float lx = px - cl;
            if (lx * lx + py * py < 1.0f) { slit = true; break; }
          }
        }

        if (slit) diskTone = 0.32f;

        // 4번 원반: 거미줄 (0.35)
        if (i == 4 && webOn) {
          const float dist = sqrtf(distSq);
          // |sin(5θ)| = |Im((dx + i dy)^5)| / dist^5
          const float dx2 = dx * dx, dy2 = dy * dy;
          const float im5 = dy * (5.0f * dx2 * dx2 - 10.0f * dx2 * dy2 + dy2 * dy2);
          const bool spokes = fabsf(im5) > 0.88f * distSq * distSq * dist;
          float ru = dist * RING_K - ringOff;
          ru -= floorf(ru);
          const bool rings = ru > ringLo && ru < ringHi;
          if (spokes || rings) {
            diskTone = diskTone * webKeep + webAdd;
          }
        }

        if (diskTone > row[x]) row[x] = diskTone;
        if (x < touchLo) touchLo = x;
        if (x > touchHi) touchHi = x;
      }
    }

    // 행 출력: 건드린 구간만 값->색, 나머지는 바탕색
    for (int x = 0; x < FRAME_W; x++) {
      if (x < touchLo || x > touchHi) {
        PFCanvas::setPixel(x, y, bgR, bgG, bgB);
      } else {
        int li = (int)(row[x] * 255.0f + 0.5f);
        if (li < 0) li = 0;
        if (li > 255) li = 255;
        PFCanvas::setPixel(x, y, RAMP_LUT[li][0], RAMP_LUT[li][1], RAMP_LUT[li][2]);
      }
    }
  }

  PFCanvas::present();
}

} // namespace JEONGEUNGYO

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
