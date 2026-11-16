// @ramp oklab 0:#000000 0.971:#feeabe 0.519:#e1d2a8 0.309:#000000 0.354:#878787 0.475:#9c9c9c 0.271:#cfc095
// @knobs State=0..0.99, Rotation=-0.9..2.1, DriftSpeed=0.4..3.6, -
// @matrix 64x128

// critical knob = State, turning it crosses Free Dispersed Solitude (widely scattered wandering) ↔ Gathered 3x3 Matrix (tight centered stitched assembly)
// Knob 1: State (0.0 = 화면 전체로 격렬하게 유영, 0.25~0.85 구간에서 실선이 하나씩 순차 결속되며 0.99 완성)
// Knob 2: Rotation (화면 전체의 연속 회전 각속도, 가운데 0.6 rad/s · 왼쪽으로 돌리면 느려지다 반대로 돈다, 정확히 0 에 서는 칸은 없음)
// Knob 3: Drift Speed (유영 속도)
// K1 누르기: 원반들이 흩어졌다가 다시 모인다. 누르는 순간 봉투 e 가 0.15초 만에 1 로 오르고 약 2.2초에 걸쳐 0 으로 내려온다(격자가 다 모이기까지 약 2.5~3초).
//   실제로 쓰는 모임 상태 = State * (1 - e): 실선이 순서대로 끊겼다가 다시 이어지고, 거미줄이 돌아왔다가 사라지고,
//   원반이 유영 자리로 날아갔다가 격자로 돌아온다. 바탕 밝기는 노브(State)를 그대로 따른다(화면 전체가 깜박이지 않게).
//   e 가 있는 동안 유영 위상도 더 빨리 흐른다(State 0 에서 이미 흩어져 있어도 누르면 한바탕 휘돈다).
//   흩어지는 중에 또 누르면 지금 e 값에서 다시 1 로 올라간다(튀지 않음). 대기 복귀 때 pressEnv = 0, pressRising = false.
// (Knob 4 is removed/unused)

export function setup(params) {
  params.W = 64;
  params.H = 128;
  params.t = 0;
  params.rot = 0;
  // K1 누르기 봉투: pressEnv 0..1 (원시값), pressRising = 오르는 중. 대기 복귀 때 0 / false 로 되돌린다.
  params.pressEnv = 0;
  params.pressRising = false;
  params.state = 0.5;
  params.knobState = 0.5;

  // State = 0.99 일 때 모이는 화면 중앙 3x3 격자 타깃 좌표 (중심 32, 64)
  params.gridPos = [
    { x: 13.5, y: 45.5 }, { x: 32.0, y: 45.5 }, { x: 50.5, y: 45.5 }, // 0, 1, 2
    { x: 13.5, y: 64.0 }, { x: 32.0, y: 64.0 }, { x: 50.5, y: 64.0 }, // 3, 4(중앙), 5
    { x: 13.5, y: 82.5 }, { x: 32.0, y: 82.5 }, { x: 50.5, y: 82.5 }  // 6, 7, 8
  ];

  // State = 0.0 일 때 화폭 전체를 누비는 분산 앵커 및 대폭 확대된 진폭/주파수 시드
  params.nodeSeeds = [
    { aloneX: 16, aloneY: 22,  px: 0.14, py: 0.19, fx: 1.25, fy: 1.60, ampX: 13.0, ampY: 17.0 },
    { aloneX: 32, aloneY: 18,  px: 0.82, py: 0.35, fx: 1.45, fy: 1.10, ampX: 15.0, ampY: 14.0 },
    { aloneX: 48, aloneY: 24,  px: 0.41, py: 0.77, fx: 1.10, fy: 1.50, ampX: 13.0, ampY: 17.0 },
    { aloneX: 12, aloneY: 62,  px: 0.63, py: 0.21, fx: 1.65, fy: 1.30, ampX: 12.0, ampY: 19.0 },
    { aloneX: 32, aloneY: 64,  px: 0.00, py: 0.00, fx: 0.95, fy: 1.15, ampX: 9.0,  ampY: 11.0 }, // 4번 중앙 거미줄 노드
    { aloneX: 52, aloneY: 66,  px: 0.29, py: 0.84, fx: 1.30, fy: 1.65, ampX: 12.0, ampY: 19.0 },
    { aloneX: 16, aloneY: 106, px: 0.75, py: 0.12, fx: 1.40, fy: 1.15, ampX: 13.0, ampY: 17.0 },
    { aloneX: 32, aloneY: 110, px: 0.38, py: 0.66, fx: 1.15, fy: 1.45, ampX: 15.0, ampY: 14.0 },
    { aloneX: 48, aloneY: 104, px: 0.91, py: 0.47, fx: 1.60, fy: 1.30, ampX: 13.0, ampY: 17.0 }
  ];

  params.toneOffsets = [
    -0.03,  0.03, -0.02,
     0.04,  0.00, -0.04,
     0.02, -0.03,  0.03
  ];

  params.nodes = new Array(9);
  for (let i = 0; i < 9; i++) {
    params.nodes[i] = {
      cx: 0, cy: 0, curR: 9.5,
      // 프레임마다 미리 계산하는 원반 경계 상자
      minX: 0, maxX: 0, minY: 0, maxY: 0,
      ports: [ { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 } ]
    };
  }

  params.links = [
    // 수평 외곽 링크
    [0, 3, 1, 2], [1, 3, 2, 2],
    [6, 3, 7, 2], [7, 3, 8, 2],
    // 수직 외곽 링크
    [0, 1, 3, 0], [2, 1, 5, 0],
    [3, 1, 6, 0], [5, 1, 8, 0],

    // 중앙(4번) 연결 4대 실선
    [3, 3, 4, 2], // 중앙-좌 (State < 0.25에서 끊김)
    [1, 1, 4, 0], // 중앙-상 (State < 0.45에서 끊김)
    [4, 3, 5, 2], // 중앙-우 (State < 0.65에서 끊김)
    [4, 1, 7, 0]  // 중앙-하 (State < 0.85에서 끊김)
  ];

  // 프레임마다 미리 계산하는 링크 선분 데이터 (시작점, 방향, 1/길이², 활성 여부, 1px 키운 경계 상자)
  params.seg = new Array(12);
  for (let l = 0; l < 12; l++) {
    params.seg[l] = { on: false, ax: 0, ay: 0, abx: 0, aby: 0, inv: 0, minX: 0, maxX: 0, minY: 0, maxY: 0 };
  }
}

export function update(dt, input, params) {
  const safeDt = Math.min(dt || 0.016, 0.05);
  const speed = input.knobValues[2];

  // K1 누르기 봉투 (오름 0.15초, 내림 2.2초, 0..1 로 잘라 누적되지 않음)
  if (input.btnPressed && input.btnPressed[0]) params.pressRising = true;
  let pe = params.pressEnv;
  if (params.pressRising) {
    pe += safeDt / 0.15;
    if (pe >= 1.0) { pe = 1.0; params.pressRising = false; }
  } else {
    pe -= safeDt / 2.2;
    if (pe < 0.0) pe = 0.0;
  }
  params.pressEnv = pe;
  const e = pe * pe * (3.0 - 2.0 * pe); // 부드러운 모양 (0, 1 양끝에서 기울기 0)

  // e 가 있는 동안 유영 위상을 더 빨리 돌린다. 더하는 양은 느릴수록 크고(최소 +4.1) 빠를수록 작다(최대 +1.2)
  //   - 이미 빠른 유영에 쌓이면 원반이 한 프레임에 지름 이상 건너뛰어 순간이동처럼 보인다
  params.t += safeDt * (speed + e * Math.max(1.2, 4.4 - speed));
  // params.t 는 cos/sin(t*f) 에만 쓰인다. f = fx, fy, fx*1.8, fy*1.7, 0.7 — 전부 0.005 의 배수이고
  // 1.955(=391/200)·2.805·2.465 때문에 400π 가 정확히 맞는 가장 짧은 주기다.
  // C++ 번역 시: 이 주기(400π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  const T_WRAP = 400.0 * Math.PI;
  if (params.t >= T_WRAP) params.t -= T_WRAP;
  else if (params.t < 0.0) params.t += T_WRAP;

  const knobState = Math.min(0.99, Math.max(0.0, input.knobValues[0]));
  params.knobState = knobState; // 바탕 밝기용 (누르기와 무관)
  const state = knobState * (1.0 - e); // 누르기로 흩어진 만큼 뺀 실제 모임 상태
  params.state = state;

  const rotSpeed = input.knobValues[1];
  params.rot += safeDt * rotSpeed;
  // rot 은 cos/sin(rot) 에만 쓰인다.
  // C++ 번역 시: 이 주기(2π)를 그대로 유지 - 6.283185 같은 근사값으로 wrap하면 화면이 튄다
  const R_WRAP = Math.PI * 2.0;
  if (params.rot >= R_WRAP) params.rot -= R_WRAP;
  else if (params.rot < 0.0) params.rot += R_WRAP;

  const t = params.t;
  const baseRadius = 9.5;
  // 누르기 동안 유영 폭도 잠깐 넓어진다 (최대 1.3배): 흩어진 상태(State 0)에서도 원반이 바깥으로 튀었다 돌아온다
  const ampBoost = 1.0 + 0.3 * e;

  for (let i = 0; i < 9; i++) {
    const s = params.nodeSeeds[i];
    const grid = params.gridPos[i];
    const nd = params.nodes[i];

    // 고조파를 합성해 더 빠르고 입체적인 파동 궤적 생성
    const wave1X = Math.cos(s.px * 6.28 + t * s.fx);
    const wave2X = Math.sin(t * (s.fx * 1.8) + s.py * 3.14) * 0.35;
    const driftX = (wave1X + wave2X) * s.ampX * ampBoost;

    const wave1Y = Math.sin(s.py * 6.28 + t * s.fy);
    const wave2Y = Math.cos(t * (s.fy * 1.7) + s.px * 3.14) * 0.35;
    const driftY = (wave1Y + wave2Y) * s.ampY * ampBoost;

    const aloneX = s.aloneX + driftX;
    const aloneY = s.aloneY + driftY;

    // State가 올라갈수록 격자 위치로 감쇠 수렴
    // (+ drift * 0.03 * easeState: 완성된 격자도 제자리에서 ±0.6px 정도 숨쉬어서 정지 화면이 되지 않게.
    //  State 0 에서는 0 이라 흩어진 모습은 그대로)
    const easeState = state * state * (3.0 - 2.0 * state);
    const cx = aloneX + (grid.x - aloneX) * easeState + driftX * 0.03 * easeState;
    const cy = aloneY + (grid.y - aloneY) * easeState + driftY * 0.03 * easeState;

    nd.cx = cx;
    nd.cy = cy;
    nd.curR = baseRadius;
    nd.minX = cx - baseRadius; nd.maxX = cx + baseRadius;
    nd.minY = cy - baseRadius; nd.maxY = cy + baseRadius;

    // 4방향 슬릿 포트 위치 갱신
    const pOffset = baseRadius * 0.82;
    nd.ports[0].x = cx;           nd.ports[0].y = cy - pOffset; // 상
    nd.ports[1].x = cx;           nd.ports[1].y = cy + pOffset; // 하
    nd.ports[2].x = cx - pOffset; nd.ports[2].y = cy;           // 좌
    nd.ports[3].x = cx + pOffset; nd.ports[3].y = cy;           // 우
  }

  // 링크 선분 미리 계산 (픽셀마다 나눗셈 대신 1/길이² 곱셈)
  for (let l = 0; l < 12; l++) {
    const link = params.links[l];
    const sg = params.seg[l];

    // 중앙 연결 실선 순차 단선
    let on = true;
    if (l === 8  && state < 0.25) on = false;
    if (l === 9  && state < 0.45) on = false;
    if (l === 10 && state < 0.65) on = false;
    if (l === 11 && state < 0.85) on = false;

    const pA = params.nodes[link[0]].ports[link[1]];
    const pB = params.nodes[link[2]].ports[link[3]];
    const abx = pB.x - pA.x;
    const aby = pB.y - pA.y;
    const lenSq = abx * abx + aby * aby;
    if (lenSq < 0.1) on = false;

    sg.on = on;
    if (!on) continue;
    sg.ax = pA.x; sg.ay = pA.y;
    sg.abx = abx; sg.aby = aby;
    sg.inv = 1.0 / lenSq;
    // 실선 폭(1px)만큼 키운 경계 상자
    sg.minX = Math.min(pA.x, pB.x) - 1.0; sg.maxX = Math.max(pA.x, pB.x) + 1.0;
    sg.minY = Math.min(pA.y, pB.y) - 1.0; sg.maxY = Math.max(pA.y, pB.y) + 1.0;
  }
}

export function draw(display, params, time) {
  const W = display.width;
  const H = display.height;
  const nodes = params.nodes;
  const seg = params.seg;
  const toneOffsets = params.toneOffsets;
  const state = params.state;
  const t = params.t;

  const cx0 = W * 0.5;
  const cy0 = H * 0.5;
  const rot = params.rot || 0.0;
  const cosR = Math.cos(rot);
  const sinR = Math.sin(rot);

  // [구역 1] 배경 영역: 0.02 ~ BG_CAP (State 끝에서 화면 전체가 밝은 베이지로 뜨지 않게 상한)
  const BG_CAP = 0.18;
  // 바탕은 노브 State 를 따른다 (누르기로 흩어져도 바탕은 그대로 - 화면 전체가 깜박이지 않게)
  const bgTone = Math.min(BG_CAP, 0.02 + (params.knobState / 0.99) * 0.24);
  const webOn = state < 0.85;
  const webPower = 1.0 - (state / 0.85);

  for (let y = 0; y < H; y++) {
    const vy = y - cy0;
    for (let x = 0; x < W; x++) {
      const ux = x - cx0;

      const rx = cx0 + ux * cosR + vy * sinR;
      const ry = cy0 - ux * sinR + vy * cosR;

      let pixelVal = bgTone;

      // [구역 2] 전경 영역: 0.31 ~ 1.0

      // 1. 단색 바느질 연결선: 0.37 (경계 상자 밖이면 건너뜀, 거리는 제곱으로 비교)
      for (let l = 0; l < 12; l++) {
        const sg = seg[l];
        if (!sg.on) continue;
        if (rx < sg.minX || rx > sg.maxX || ry < sg.minY || ry > sg.maxY) continue;

        const qx = rx - sg.ax;
        const qy = ry - sg.ay;
        const tProj = (qx * sg.abx + qy * sg.aby) * sg.inv;
        if (tProj >= 0.0 && tProj <= 1.0) {
          const dx = qx - tProj * sg.abx;
          const dy = qy - tProj * sg.aby;
          if (dx * dx + dy * dy < 1.0) {
            pixelVal = 0.37;
            break;
          }
        }
      }

      // 2. 도자 원반 노드 & 슬릿 홈 & 거미줄
      for (let i = 0; i < 9; i++) {
        const nd = nodes[i];
        if (rx < nd.minX || rx > nd.maxX || ry < nd.minY || ry > nd.maxY) continue;
        const dx = rx - nd.cx;
        const dy = ry - nd.cy;
        const distSq = dx * dx + dy * dy;
        const curR = nd.curR;

        if (distSq <= curR * curR) {
          const dist = Math.sqrt(distSq);
          const normDist = dist / curR;

          const baseDome = 0.52 + 0.33 * Math.cos(normDist * 1.5708);
          let diskTone = baseDome + toneOffsets[i];

          // 가장자리 슬릿 홈: 0.32 (거리 제곱 < 1)
          for (let k = 0; k < 4; k++) {
            const port = nd.ports[k];
            const px = rx - port.x;
            const py = ry - port.y;

            let dSq;
            if (k === 0 || k === 1) {
              const ly = py - Math.max(-2.3, Math.min(2.3, py));
              dSq = px * px + ly * ly;
            } else {
              const lx = px - Math.max(-2.3, Math.min(2.3, px));
              dSq = lx * lx + py * py;
            }

            if (dSq < 1.0) {
              diskTone = 0.32;
              break;
            }
          }

          // 4번 원반: 본래 거미줄 패턴 (0.35)
          if (i === 4 && webOn) {
            const angle = Math.atan2(dy, dx);
            const spokes = Math.abs(Math.sin(angle * 5.0));
            const rings = Math.abs(Math.sin(dist * 1.2 - t * 0.7));

            if (spokes > 0.88 || rings > 0.82) {
              diskTone = diskTone * (1.0 - webPower * 0.75) + 0.35 * (webPower * 0.75);
            }
          }

          if (diskTone > pixelVal) {
            pixelVal = diskTone;
          }
        }
      }

      display.setValue(x, y, pixelVal);
    }
  }
}
