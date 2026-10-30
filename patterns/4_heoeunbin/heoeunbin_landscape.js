// 참고용 가로 버전 - 전시 패널에는 heoeunbin_portrait 사용
// @knobs Size=0.6..1.5, Backdrop=0.3..1, Speed=0.2..1.2, -
// @matrix 128x64
//

//
// Knob 1: Size     - gecko scale (가운데 1.05 = 온몸이 보임, 1.5 = 머리 클로즈업)
// Knob 2: Backdrop - 아래 절반: 0.3 어두운 주황 → 가운데(0.65) 꽉 찬 주황 + 꽃 1송이
//                    위 절반: 꽃이 하나씩 자라나며 늘어나고(최대 6송이) 모두 조금씩 커진다
// Knob 3: Speed    - walking pace (최소 0.2에서도 걷는다, 가운데 0.7, 최대 1.2)
// Knob 4: (unused)
// Button 1 (tap): flip walking direction
// Button 2 (tap): the mouse rider hops on / off
// 대기 복귀 때 되돌릴 상태: params.dir = -1, params.rider = false

var W = 128, H = 64;

var C_BG = 0, C_SPARK = 1, C_FARLEG = 2, C_BODY = 3, C_STRIPE = 4, C_EYE = 5, C_INK = 6,
    C_MOUSE = 7, C_CHEEK = 8, C_FRUIT = 9, C_LEAF = 10, C_CREST = 11;
// 볏(C_CREST)은 몸(2)보다 위, 눈(5)보다 아래 계층: 머리 위에 겹쳐도 검은 윤곽선이 생기고
// 눈 테두리 선은 볏 밑변 위로 그대로 이어진다.
// 먼 다리(C_FARLEG)는 몸(2)보다 한 단계 낮은 1: 몸이 먼 다리 위에 겹치는 곳에도 검은 윤곽선이 생긴다.
var RANK = [0, 0, 1, 2, 2, 5, 9, 3, 3, 4, 4, 3];

var PALETTE = [
  [255, 150, 60],  // bg orange
  [255, 214, 110], // sparkle
  [184, 136, 64],  // far leg - 그늘진 먼 다리: 몸색보다 약 40% 어둡게 (걸을 때 앞뒤 다리가 확실히 갈린다)
  [255, 232, 140], // body
  [232, 190, 82],  // flank stripe
  [255, 250, 228], // eye
  [0, 0, 0],       // ink
  [255, 255, 255], // mouse
  [255, 172, 160], // cheek
  [255, 160, 40],  // orange
  [96, 200, 80],   // leaf
  [255, 232, 140], // crest (body colour, own outline)
];

export function setup(params) {
  params.buf = new Uint8Array(W * H);
  params.ink = new Uint8Array(W * H);
  params.gait = 0;
  params.idle = 0;
  params.blinkT = 0;
  params.drift = 0;
  params.dir = -1;
  params.rider = false;
  params.speed = 0.7;
  params.backdrop = 0.65;
  params.size = 1.05;
}

export function update(dt, input, params) {
  var v = input.knobValues;
  params.size = v[0];
  params.backdrop = v[1];
  params.speed = v[2];
  if (input.btnPressed[0]) params.dir = -params.dir;
  if (input.btnPressed[1]) params.rider = !params.rider;
  params.gait += dt * params.speed * 1.5;
  params.gait -= Math.floor(params.gait);
  // idle은 sin(idle*0.6)·sin(idle*2.2)·sin(idle*0.9)에만 쓰인다: 셋 다 20π에서 정확히 한 바퀴의 정수배.
  // C++ 번역 시: 이 주기(20π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다.
  params.idle += dt;
  params.idle -= Math.floor(params.idle / (20 * Math.PI)) * (20 * Math.PI);
  // 눈 깜박임은 자기 시계(3.7초 주기)로 돈다. C++ 번역 시: 3.7에서 wrap하는 것을 그대로 유지.
  params.blinkT = (params.blinkT + dt) % 3.7;
  params.drift += dt * params.speed * 20;
  // 꽃 세 속도(0.55·0.8·1.05배)가 모두 148px 주기로 딱 맞아떨어지는 2960에서 되감는다:
  // 그림은 그대로이고, 오래 켜 두어도 float 정밀도가 떨어지지 않는다.
  // C++ 번역 시: 2960을 그대로 유지 - 2960은 정확하지만 1480은 아니다(0.55*1480 = 5.5*148 → 꽃이 74px 튄다).
  params.drift -= Math.floor(params.drift / 2960) * 2960;
}

function disc(buf, cx, cy, r, c) {
  var r2 = r * r;
  var y0 = Math.max(0, Math.floor(cy - r)), y1 = Math.min(H - 1, Math.ceil(cy + r));
  var x0 = Math.max(0, Math.floor(cx - r)), x1 = Math.min(W - 1, Math.ceil(cx + r));
  for (var y = y0; y <= y1; y++) {
    var dy = y + 0.5 - cy;
    for (var x = x0; x <= x1; x++) {
      var dx = x + 0.5 - cx;
      if (dx * dx + dy * dy <= r2) buf[y * W + x] = c;
    }
  }
}

function ellipse(buf, cx, cy, rx, ry, c) {
  var y0 = Math.max(0, Math.floor(cy - ry)), y1 = Math.min(H - 1, Math.ceil(cy + ry));
  var x0 = Math.max(0, Math.floor(cx - rx)), x1 = Math.min(W - 1, Math.ceil(cx + rx));
  for (var y = y0; y <= y1; y++) {
    var dy = (y + 0.5 - cy) / ry;
    for (var x = x0; x <= x1; x++) {
      var dx = (x + 0.5 - cx) / rx;
      if (dx * dx + dy * dy <= 1) buf[y * W + x] = c;
    }
  }
}

function capsule(buf, x0, y0, x1, y1, r0, r1, c) {
  var dx = x1 - x0, dy = y1 - y0;
  var len = Math.sqrt(dx * dx + dy * dy);
  var n = Math.max(1, Math.ceil(len * 1.5));
  for (var i = 0; i <= n; i++) {
    var t = i / n;
    disc(buf, x0 + dx * t, y0 + dy * t, r0 + (r1 - r0) * t, c);
  }
}

function triangle(buf, x0, y0, x1, y1, x2, y2, c) {
  var minX = Math.max(0, Math.floor(Math.min(x0, x1, x2)));
  var maxX = Math.min(W - 1, Math.ceil(Math.max(x0, x1, x2)));
  var minY = Math.max(0, Math.floor(Math.min(y0, y1, y2)));
  var maxY = Math.min(H - 1, Math.ceil(Math.max(y0, y1, y2)));

  var det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
  if (Math.abs(det) < 0.0001) return;

  for (var y = minY; y <= maxY; y++) {
    var py = y + 0.5;
    for (var x = minX; x <= maxX; x++) {
      var px = x + 0.5;
      var w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det;
      var w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det;
      var w2 = 1.0 - w0 - w1;
      if (w0 >= -0.01 && w1 >= -0.01 && w2 >= -0.01) {
        buf[y * W + x] = c;
      }
    }
  }
}

// Horizontal cut with an integer row count per column: stable at any scale.
function mouthCut(buf, xa, ya, xb, yb, ra, rb, c) {
  var x0 = Math.round(Math.min(xa, xb)), x1 = Math.round(Math.max(xa, xb));
  for (var x = Math.max(0, x0); x <= Math.min(W - 1, x1); x++) {
    var t = x1 === x0 ? 0 : (x - x0) / (x1 - x0);
    if (xa > xb) t = 1 - t;
    var yc = ya + (yb - ya) * t;
    var rows = Math.max(1, Math.round(2 * (ra + (rb - ra) * t)));
    var top = Math.round(yc - rows / 2);
    for (var y = Math.max(0, top); y < Math.min(H, top + rows); y++) buf[y * W + x] = c;
  }
}

function flower(buf, cx, cy, s, c) {
  disc(buf, cx, cy, s * 0.55, c);
  for (var k = 0; k < 6; k++) {
    var a = k * Math.PI / 3;
    disc(buf, cx + Math.cos(a) * s, cy + Math.sin(a) * s, s * 0.55, c);
  }
}

function drawLeg(buf, T, hipx, hipy, phase, isFront, ground, c) {
  var stride = 8.5, lift = 4.2;
  var fx, fy;
  var p = phase - Math.floor(phase);
  if (p < 0.62) {
    var q = p / 0.62;
    fx = hipx - stride / 2 + stride * q;
    fy = ground;
  } else {
    var q2 = (p - 0.62) / 0.38;
    fx = hipx + stride / 2 - stride * q2;
    fy = ground - lift * Math.sin(q2 * Math.PI);
  }

  var L = 8.2;
  var dx = fx - hipx, dy = fy - hipy;
  var d = Math.sqrt(dx * dx + dy * dy);
  if (d > 2 * L - 0.5) { var k = (2 * L - 0.5) / d; dx *= k; dy *= k; d = 2 * L - 0.5; }
  var mx = hipx + dx / 2, my = hipy + dy / 2;
  var h = Math.sqrt(Math.max(0, L * L - d * d / 4));
  var nx = -dy / d, ny = dx / d;
  var bend = isFront ? 1 : -1;
  if (nx * bend < 0) { nx = -nx; ny = -ny; }
  var kx = mx + nx * h, ky = my + ny * h;
  var ax = hipx + dx, ay = hipy + dy;

  T(buf, hipx, hipy, kx, ky, 3.3, 2.8, c);
  T(buf, kx, ky, ax, ay, 2.8, 2.4, c);

  var lifted = fy < ground - 0.5 ? 1 : 0;
  T(buf, ax, ay, ax - 4.6, ay + 0.4 - lifted * 1.2, 2.0, 1.3, c);
  T(buf, ax, ay, ax - 4.0, ay - 2.8, 1.8, 1.2, c);
  T(buf, ax, ay, ax - 2.6, ay + 2.8 - lifted * 0.9, 1.8, 1.2, c);
}

export function draw(display, params, time) {
  var buf = params.buf;
  var ink = params.ink;
  var s = params.size;
  var dir = params.dir;
  var walk = Math.min(1, params.speed);
  var gait = params.gait;
  var idle = params.idle;

  var colors = PALETTE;
  // Backdrop 아래 절반(0.3..0.65): 어두운 주황 → 꽉 찬 주황 (가운데에서 정확히 끝난다)
  var bgLevel = 0.6 + 0.4 * Math.min(1, Math.max(0, (params.backdrop - 0.3) / 0.35));
  // 위 절반(0.65..1): 꽃이 자라난다. 가운데 = 꽃 1송이(원작 가운데와 같다)
  var sparkAmount = Math.min(1, Math.max(0, (params.backdrop - 0.65) / 0.35));

  buf.fill(C_BG);

  // 배경 꽃 장식
  // 꽃 수 = 1 + ceil(f). 마지막(새로 생긴) 꽃은 f의 소수부만큼 작게 시작해 커지므로 튀지 않는다.
  var f = sparkAmount * 5;
  var fi = Math.floor(f);
  var nSpark = 1 + Math.ceil(f);
  var lastScale = f > fi ? f - fi : 1;
  var allScale = 1 + 0.6 * sparkAmount;
  for (var i = 0; i < nSpark; i++) {
    var sx = (i * 41 + 7 + dir * -params.drift * (0.55 + 0.25 * (i % 3))) % (W + 20);
    if (sx < 0) sx += W + 20;
    sx -= 10;
    var sy = 8 + ((i * 23 + 11) % 48);
    var fs = (2.4 + (i % 2) * 0.8) * allScale;
    if (i === nSpark - 1 && i > 0) fs *= lastScale;
    flower(buf, sx, sy, fs, C_SPARK);
  }

  // 게코 기본 위치
  // 방향을 뒤집으면 여백도 뒤집는다 (dir = -1에서는 원작과 같고, K1 누른 뒤에도 주둥이가 화면 밖으로 안 나간다)
  var baseCx = W / 2 - 6 * dir + Math.sin(idle * 0.6) * 1.5;
  var bob = -Math.abs(Math.sin(gait * 2 * Math.PI)) * 1.2 * walk;
  var baseCy = 27.5 + bob;
  var ground = 17;

  // 줌인 시에만(-30, -17)로 앵커 이동, 1.0 이하(줌아웃/기본)에서는 (0, 0) 중심 유지
  var zoomFactor = Math.max(0, (s - 1.0) / 0.5); // s: 1.0 -> 0.0, s: 1.5 -> 1.0
  var focusX = -30.0 * zoomFactor;
  var focusY = -17.0 * zoomFactor;

  function X(lx) {
    return baseCx + (focusX + (lx - focusX) * s) * -dir;
  }
  function Y(ly) {
    return baseCy + (focusY + (ly - focusY) * s);
  }

  function T(b, x0, y0, x1, y1, r0, r1, c) {
    capsule(b, X(x0), Y(y0), X(x1), Y(y1), r0 * s, r1 * s, c);
  }
  function E(b, x0, y0, rx, ry, c) { ellipse(b, X(x0), Y(y0), rx * s, ry * s, c); }
  function D(b, x0, y0, r, c) { disc(b, X(x0), Y(y0), r * s, c); }
  function TRI(b, x0, y0, x1, y1, x2, y2, c) {
    triangle(b, X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c);
  }

  // 눈 위치 및 크기
  var ex = -36, ey = -10.0;
  var eyeR = 8.6;

  // 먼 앞다리 & 뒷다리
  drawLeg(buf, T, -27, 3.5, gait + 0.5, true, ground, C_FARLEG);
  drawLeg(buf, T, 2, 5, gait, false, ground, C_FARLEG);

  // 꼬리
  var tx = 4, ty = 2;
  var N = 38, seg = 1.45;
  var idleSway = Math.sin(idle * 2.2) * 0.08 + Math.sin(idle * 0.9) * 0.04;
  var pts = [];
  for (var k = 0; k < N; k++) {
    var u = k / N;
    var curl = 0.08 + 1.15 * Math.pow(u, 1.2);
    var tailWave = Math.sin(u * 5.2 - gait * 2 * Math.PI) * 0.22 * walk;
    var a = curl + idleSway * (0.4 + 0.6 * u) + tailWave * u;

    var nx2 = tx + Math.cos(a) * seg;
    var ny2 = ty + Math.sin(a) * seg;
    var r = 6.4 * (1 - u * 0.82) + 1.2;
    T(buf, tx, ty, nx2, ny2, r, r, C_BODY);
    pts.push([nx2, ny2, r]);
    tx = nx2; ty = ny2;
  }

  // 몸통
  T(buf, -29, 0, 5, 2, 11.4, 7.5, C_BODY);

  // 두개골
  E(buf, -35, -4.0, 15.5, 12.0, C_BODY);

  // 윗주둥이
  T(buf, -35, -3.5, -57.5, -3.0, 10.5, 2.4, C_BODY);
  D(buf, -57.5, -3.0, 2.4, C_BODY);

  // 아랫입
  T(buf, -31, 3.0, -49.5, 3.2, 4.8, 1.8, C_BODY);
  D(buf, -49.5, 3.2, 1.8, C_BODY);

  // 가까운 뒷다리 & 앞다리
  drawLeg(buf, T, 2, 5, gait + 0.5, false, ground, C_BODY);
  drawLeg(buf, T, -27, 3.5, gait, true, ground, C_BODY);

  // 몸통 점선
  var bodySteps = 16;
  for (var bq = 0; bq < bodySteps; bq++) {
    if (bq % 5 < 3) {
      var bt = bq / (bodySteps - 1);
      var bx = -23 + bt * (4 - (-23));
      var by = -7.5 + bt * (-2.8 - (-7.5));
      D(buf, bx, by, 1.0, C_STRIPE);
    }
  }

  // 꼬리 점선
  for (var q = 0; q < pts.length - 4; q++) {
    if (q % 5 < 3) {
      var pR = pts[q][2];
      D(buf, pts[q][0], pts[q][1] - pR * 0.55, Math.min(1.0, pR * 0.25), C_STRIPE);
    }
  }

  // 벌려진 입 틈새: 캡슐로 파면 줌아웃에서 반지름이 반 픽셀 밑으로 떨어져 틈이 생겼다 사라졌다 한다.
  // 대신 화면 컬럼마다 파낼 줄 수를 정수로 정해서(뒤 2줄 → 앞 1줄, 최소 1줄) 크기가 항상 같게 한다.
  mouthCut(buf, X(-53), Y(1.5), X(-37), Y(2.0), 0.9 * s, 0.4 * s, C_BG);

  // 쥐 탑승자
  if (params.rider) {
    var mb = Math.sin(gait * 4 * Math.PI + 1.2) * 0.6 * walk;
    var mx0 = -10, my0 = -12.5 + mb;
    D(buf, mx0 - 7.5, my0 - 5.5, 3.6, C_MOUSE);
    D(buf, mx0 + 6.5, my0 - 5.5, 3.6, C_MOUSE);
    E(buf, mx0, my0, 9.2, 6.6, C_MOUSE);
    E(buf, mx0 - 8.5, my0 + 2, 4.4, 3, C_MOUSE);
    E(buf, mx0 + 1, my0 - 7.2, 3, 2.7, C_FRUIT);
    D(buf, mx0 + 1.5, my0 - 9.9, 0.9, C_LEAF);
    D(buf, mx0 - 6, my0 + 1, 1.6, C_CHEEK);
    D(buf, mx0 + 4.5, my0 + 2, 1.6, C_CHEEK);
  }

  // 눈꺼풀 볏 3개: 가운데는 수직, 양쪽은 ±45°로 대칭. 밑변은 눈 테두리 안쪽(−1)에 물려 있다.
  // 눈보다 먼저 그리므로 이웃한 밑변끼리 겹치는 부분은 눈 원판이 덮어 버리고, 눈 밖으로 나온 부분만 보인다
  // (밑변 모서리는 눈 중심에서 8.5 — 눈 반지름 8.6 안이라 어느 배율에서도 볏끼리 붙지 않는다).
  // 자기 계층이라 머리 위에서도 윤곽선이 나고, 눈 테두리 선은 볏 밑변 위로 그대로 이어진다.
  var crestBase = 7.6, crestH = 6.2, crestTipR = 1.4;
  var crestAngles = [-135, -90, -45];     // 밑변 중심이 놓이는 각도 (도, y가 아래인 화면 좌표)
  var crestLean = [-8, 0, 8];           // 꼭짓점이 바깥으로 벌어지는 각
  for (var ci = 0; ci < 3; ci++) {
    var ca = crestAngles[ci] * Math.PI / 180;
    var la = (crestAngles[ci] + crestLean[ci]) * Math.PI / 180;
    var ux = Math.cos(ca), uy = Math.sin(ca);        // 눈 중심 → 밑변 중심
    var vx = -uy, vy = ux;                           // 밑변 방향
    var bx0 = ex + ux * (eyeR - 1.0), by0 = ey + uy * (eyeR - 1.0);
    var apx = bx0 + Math.cos(la) * crestH, apy = by0 + Math.sin(la) * crestH;
    TRI(buf, bx0 + vx * crestBase / 2, by0 + vy * crestBase / 2,
             bx0 - vx * crestBase / 2, by0 - vy * crestBase / 2,
             apx, apy, C_CREST);
    D(buf, apx - Math.cos(la) * crestTipR, apy - Math.sin(la) * crestTipR, crestTipR, C_CREST);
  }

  // 눈 디스크
  var blinkT = params.blinkT;
  var blink = blinkT > 3.5 && blinkT < 3.66;
  if (!blink) D(buf, ex, ey, eyeR, C_EYE);

  // ── 외곽선 잉크 패스 ──
  for (var y = 0; y < H; y++) {
    for (var x = 0; x < W; x++) {
      var i2 = y * W + x;
      var c = buf[i2];
      var rk = RANK[c];
      ink[i2] = 0;
      if (rk === 0 || c === C_INK) continue;
      if ((x > 0 && RANK[buf[i2 - 1]] < rk) || (x < W - 1 && RANK[buf[i2 + 1]] < rk) ||
          (y > 0 && RANK[buf[i2 - W]] < rk) || (y < H - 1 && RANK[buf[i2 + W]] < rk)) {
        ink[i2] = 1;
      }
    }
  }
  for (var j = 0; j < W * H; j++) if (ink[j]) buf[j] = C_INK;

  // 잉크 디테일
  if (blink) {
    T(buf, ex - 6.0, ey + 1, ex + 6.0, ey + 1, 1.1, 1.1, C_INK);
  } else {
    // 동공: 눈 지름의 40% 폭 세로 띠, 눈 원 안쪽만 칠한다
    var pw = eyeR * 0.4 * s, pcx = X(ex), pcy = Y(ey), pr = (eyeR - 0.8) * s;
    for (var py2 = Math.max(0, Math.floor(pcy - pr)); py2 <= Math.min(H - 1, Math.ceil(pcy + pr)); py2++) {
      for (var px2 = Math.max(0, Math.floor(pcx - pw)); px2 <= Math.min(W - 1, Math.ceil(pcx + pw)); px2++) {
        var ddx = px2 + 0.5 - pcx, ddy = py2 + 0.5 - pcy;
        if (Math.abs(ddx) <= pw && ddx * ddx + ddy * ddy <= pr * pr && buf[py2 * W + px2] === C_EYE) buf[py2 * W + px2] = C_INK;
      }
    }
  }

  // 검은 점(귀) 위치
  D(buf, -24, -4.0, 1.6, C_INK);

  if (params.rider) {
    var mb2 = Math.sin(gait * 4 * Math.PI + 1.2) * 0.6 * walk;
    var ex2 = -10, ey2 = -12.5 + mb2;
    T(buf, ex2 - 8.5, ey2 - 1.5, ex2 - 7, ey2 - 2.6, 0.55, 0.55, C_INK);
    T(buf, ex2 - 7, ey2 - 2.6, ex2 - 5.5, ey2 - 1.5, 0.55, 0.55, C_INK);
    T(buf, ex2 - 0.5, ey2 - 1.5, ex2 + 1, ey2 - 2.6, 0.55, 0.55, C_INK);
    T(buf, ex2 + 1, ey2 - 2.6, ex2 + 2.5, ey2 - 1.5, 0.55, 0.55, C_INK);
    D(buf, ex2 - 12.5, ey2 + 2.6, 0.8, C_INK);
  }

  // ── RGB 렌더링 ──
  var bg = colors[C_BG];
  var bgR = bg[0] * bgLevel, bgG = bg[1] * bgLevel, bgB = bg[2] * bgLevel;
  var sp = colors[C_SPARK];
  var spR = sp[0] * bgLevel, spG = sp[1] * bgLevel, spB = sp[2] * bgLevel;
  for (var y2 = 0; y2 < H; y2++) {
    for (var x2 = 0; x2 < W; x2++) {
      var cc = buf[y2 * W + x2];
      if (cc === C_BG) display.setPixel(x2, y2, bgR, bgG, bgB);
      else if (cc === C_SPARK) display.setPixel(x2, y2, spR, spG, spB);
      else { var col = colors[cc]; display.setPixel(x2, y2, col[0], col[1], col[2]); }
    }
  }
}

