// ===== Patternflow pattern =====
// Title:    JEONGEUNGYO
// Author:   @engmung
// Date:     2026-09-16
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

// @ramp oklab 0:#000000 0.971:#feeabe 0.519:#e1d2a8 0.309:#000000 0.354:#878787 0.475:#9c9c9c 0.271:#cfc095
// @knobs State=0..0.99, DriftSpeed=0.2..4, RotSpeed=-2..2, Unused=0..1
// @matrix 64x128

// critical knob = State, turning it crosses Free Dispersed Solitude (widely scattered wandering) ↔ Gathered 3x3 Matrix (tight centered stitched assembly)
// Knob 1: State (0.0 = 화면 전체로 격렬하게 유영, 0.25~0.85 구간에서 실선이 하나씩 순차 결속되며 0.99 완성)
// Knob 2: Drift Speed (유영 속도)
// Knob 3: Rotation Speed (화면 전체의 연속 회전 각속도)
// (Knob 4 is removed/unused)

export function setup(params) {
  params.W = 64;
  params.H = 128;
  params.t = 0;
  params.rot = 0;

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
}

export function update(dt, input, params) {
  const safeDt = Math.min(dt || 0.016, 0.05);
  const speed = input.knobValues[1];
  params.t += safeDt * speed;

  const state = Math.min(0.99, Math.max(0.0, input.knobValues[0]));
  params.state = state;

  const rotSpeed = input.knobValues[2];
  params.rot += safeDt * rotSpeed;
  if (params.rot > 6.283185) params.rot -= 6.283185;
  else if (params.rot < -6.283185) params.rot += 6.283185;

  const t = params.t;
  const baseRadius = 9.5;

  for (let i = 0; i < 9; i++) {
    const s = params.nodeSeeds[i];
    const grid = params.gridPos[i];
    const nd = params.nodes[i];

    // 고조파를 합성해 더 빠르고 입체적인 파동 궤적 생성
    const wave1X = Math.cos(s.px * 6.28 + t * s.fx);
    const wave2X = Math.sin(t * (s.fx * 1.8) + s.py * 3.14) * 0.35;
    const driftX = (wave1X + wave2X) * s.ampX;

    const wave1Y = Math.sin(s.py * 6.28 + t * s.fy);
    const wave2Y = Math.cos(t * (s.fy * 1.7) + s.px * 3.14) * 0.35;
    const driftY = (wave1Y + wave2Y) * s.ampY;

    const aloneX = s.aloneX + driftX;
    const aloneY = s.aloneY + driftY;

    // State가 올라갈수록 격자 위치로 감쇠 수렴
    const easeState = state * state * (3.0 - 2.0 * state);
    const cx = aloneX + (grid.x - aloneX) * easeState;
    const cy = aloneY + (grid.y - aloneY) * easeState;

    nd.cx = cx;
    nd.cy = cy;
    nd.curR = baseRadius;

    // 4방향 슬릿 포트 위치 갱신
    const pOffset = baseRadius * 0.82;
    nd.ports[0].x = cx;           nd.ports[0].y = cy - pOffset; // 상
    nd.ports[1].x = cx;           nd.ports[1].y = cy + pOffset; // 하
    nd.ports[2].x = cx - pOffset; nd.ports[2].y = cy;           // 좌
    nd.ports[3].x = cx + pOffset; nd.ports[3].y = cy;           // 우
  }
}

export function draw(display, params, time) {
  const W = display.width;
  const H = display.height;
  const nodes = params.nodes;
  const links = params.links;
  const toneOffsets = params.toneOffsets;
  const state = params.state;
  const t = params.t;

  const stitchW = 1.0;

  const cx0 = W * 0.5;
  const cy0 = H * 0.5;
  const rot = params.rot || 0.0;
  const cosR = Math.cos(rot);
  const sinR = Math.sin(rot);

  // [구역 1] 배경 영역: 0.02 ~ 0.26
  const bgTone = 0.02 + (state / 0.99) * 0.24;

  for (let y = 0; y < H; y++) {
    const vy = y - cy0;
    for (let x = 0; x < W; x++) {
      const ux = x - cx0;

      const rx = cx0 + ux * cosR + vy * sinR;
      const ry = cy0 - ux * sinR + vy * cosR;

      let pixelVal = bgTone;

      // [구역 2] 전경 영역: 0.31 ~ 1.0

      // 1. 단색 바느질 연결선: 0.37
      for (let l = 0; l < links.length; l++) {
        const link = links[l];

        // 중앙 연결 실선 순차 단선
        if (l === 8  && state < 0.25) continue;
        if (l === 9  && state < 0.45) continue;
        if (l === 10 && state < 0.65) continue;
        if (l === 11 && state < 0.85) continue;

        const pA = nodes[link[0]].ports[link[1]];
        const pB = nodes[link[2]].ports[link[3]];

        const abx = pB.x - pA.x;
        const aby = pB.y - pA.y;
        const lenSq = abx * abx + aby * aby;
        if (lenSq < 0.1) continue;

        const tProj = ((rx - pA.x) * abx + (ry - pA.y) * aby) / lenSq;
        if (tProj >= 0.0 && tProj <= 1.0) {
          const px = pA.x + tProj * abx;
          const py = pA.y + tProj * aby;
          const dx = rx - px;
          const dy = ry - py;
          const dist = Math.sqrt(dx * dx + dy * dy);

          if (dist < stitchW) {
            pixelVal = 0.37;
          }
        }
      }

      // 2. 도자 원반 노드 & 슬릿 홈 & 거미줄
      for (let i = 0; i < 9; i++) {
        const nd = nodes[i];
        const dx = rx - nd.cx;
        const dy = ry - nd.cy;
        const distSq = dx * dx + dy * dy;
        const curR = nd.curR;

        if (distSq <= curR * curR) {
          const dist = Math.sqrt(distSq);
          const normDist = dist / curR;

          const baseDome = 0.52 + 0.33 * Math.cos(normDist * 1.5708);
          let diskTone = baseDome + toneOffsets[i];

          // 가장자리 슬릿 홈: 0.32
          for (let k = 0; k < 4; k++) {
            const port = nd.ports[k];
            const px = rx - port.x;
            const py = ry - port.y;

            let dLine = 999.0;
            if (k === 0 || k === 1) {
              const clampedY = Math.max(-2.3, Math.min(2.3, py));
              const ly = py - clampedY;
              dLine = Math.sqrt(px * px + ly * ly);
            } else {
              const clampedX = Math.max(-2.3, Math.min(2.3, px));
              const lx = px - clampedX;
              dLine = Math.sqrt(lx * lx + py * py);
            }

            if (dLine < 1.0) {
              diskTone = 0.32;
            }
          }

          // 4번 원반: 본래 거미줄 패턴 (0.35)
          if (i === 4 && state < 0.85) {
            const webPower = 1.0 - (state / 0.85);
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

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
