// ===== Patternflow pattern =====
// Title:    CHOEGYEONGEUN
// Author:   @engmung
// Date:     2026-09-18
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

// @ramp oklab 0.035:#cc0000 0.6:#ffea00 0.207:#330000 1:#ffffff 0.497:#cd7918 0.552:#ff5900 0.7:#ff00c8 0.855:#0045e6 0:#000000 0.078:#6b98c2 0.116:#ffc96b 0.152:#c7c7c7
// @knobs DoorOpen=0..1, Zoom=0.8..2.5, FlowerSpeed=0.2..8, Unused=0..1
// @matrix 128x64


















// Knob 1 (DoorOpen): 궤짝 널문 개폐 (0: 완전 밀폐, 1: 와이드 개방) - 묵직한 관성
// Knob 2 (Zoom): 감실 내부 클로즈업 줌 (0.8: 전체 조망, 2.5: 내부 밀착) + 줌 크기 비례 가변 숨쉬기
// Knob 3 (FlowerSpeed): 꽃의 이동 및 자전 회전 속도 (0.2 ~ 8.0 고속 지원)
// Knob 4 (Unused): 미사용

// [컬러램프 대역 분할표]
// 0.00        : 순수 배경 (Pure Black)
// 0.04 ~ 0.16 : [BG Slots] 감실 배면 4개 단색 배경 슬롯 (0.5 이하 대역)
//               Scene 0 (겹매화)   -> BG Tier 0 (0.04)
//               Scene 1 (참나리)   -> BG Tier 1 (0.08)
//               Scene 2 (모란/작약) -> BG Tier 2 (0.12)
//               Scene 3 (벚꽃)     -> BG Tier 3 (0.16)
// 0.22 ~ 0.48 : 궤짝 외부 틀, 널문, 몰딩 (Cabinet Frame & Door)
// 0.51 ~ 0.53 : [Leaf] 잎사귀 (Foliage)
// 0.55 ~ 0.92 : [Color Slots] 꽃 형태별 고유 색상 슬롯
// 0.98 ~ 1.00 : [Core] 꽃술 중심 핵 (White Spark)

const clamp = (x, a, b) => (x < a ? a : (x > b ? b : x));
const flowerHash = n => { const v = Math.sin(n * 127.1 + 31.7) * 43758.5453; return v - Math.floor(v); };

function dampCabinet(p, key, velocityKey, target, response, dt, min, max) {
  const offset = p[key] - target;
  const j = p[velocityKey] + response * offset;
  const decay = Math.exp(-response * dt);
  let next = target + (offset + j * dt) * decay;
  let velocity = (p[velocityKey] - response * j * dt) * decay;

  if (next < min || next > max) {
    next = clamp(next, min, max);
    velocity = 0;
  }
  if (Math.abs(next - target) < 1e-4 && Math.abs(velocity) < 1e-3) {
    next = target;
    velocity = 0;
  }
  p[key] = next;
  p[velocityKey] = velocity;
}

export function setup(params) {
  params.doorOpen = 1;
  params.zoomBase = 1;
  params.zoom = 1;
  params.flowerSpeed = 1;
  params.doorVelocity = 0;
  params.zoomVelocity = 0;
  params.doorResponse = 12;
  params.zoomResponse = 9;

  params.controlsReady = false;
  params.sceneIndex = 0;
  params.canSwitch = false;
  params.time = 0;

  const counts = [6, 5, 6, 7];

  params.scenes = counts.map((count, s) => {
    const flowers = [];

    for (let i = 0; i < count; i++) {
      const seed = i + s * 37;
      const depth = 0.55 + flowerHash(seed + 3) * 0.45;

      const baseRadii = [5.3, 5.5, 5.2, 5.0];
      const randomScale = 1.9 + flowerHash(seed + 16) * 1.0;
      const radius = baseRadii[s] * (0.88 + depth * 0.22) * randomScale;

      const normX = (i + 0.5 + (flowerHash(seed + 1) - 0.5) * 0.5) / count;
      const normY = ((i * 0.618033 + flowerHash(seed + 2) * 0.3) % 1.0);

      const f = {
        baseR: radius,
        invR: 1.0 / radius,
        depth,
        phase: flowerHash(seed + 8) * 6.28318,
        phaseY: flowerHash(seed + 14) * 6.28318,
        turn: flowerHash(seed + 9) * 6.28318,
        turnSpeed: (flowerHash(seed + 10) - 0.5) * (0.35 + depth * 0.45),

        speedMult: 0.8 + flowerHash(seed + 13) * 0.4,
        driftFreq: 0.32 + flowerHash(seed + 21) * 0.25,
        driftAmp: 1.2 + flowerHash(seed + 22) * 1.5,

        progressX: normX,
        progressY: normY,

        x: 44 + normX * 39,
        y: 12 + normY * 39,
        loose: (s === 3 && i % 3 === 0),
        colorTier: s % 5,
        hasLeaf: (i % 2 === 1),
        leafAngle: flowerHash(seed + 15) * 6.28318,
        petals: []
      };

      function whorl(n, r, length, width, phase, pointed) {
        for (let k = 0; k < n; k++) {
          const a = k * 6.28318 / n + phase;
          const variation = 0.95 + 0.1 * flowerHash(seed * 31 + k + r * 17);
          const pLen = r * length * variation;
          const pOff = r * 0.42;
          f.petals.push({
            baseAngle: a,
            cosA: 1,
            sinA: 0,
            offset: pOff,
            length: pLen,
            invSpan: 1.0 / (pLen + pOff),
            width: r * width,
            pointed: pointed,
            shade: flowerHash(k + seed * 7) * 0.015,
            notch: (s === 3 && !f.loose)
          });
        }
      }

      if (s === 0) {
        whorl(5, 1.0, 0.62, 0.38, 0.1, false);
        whorl(5, 0.72, 0.58, 0.36, 0.72, false);
      } else if (s === 1) {
        whorl(3, 1.0, 0.75, 0.28, 0.0, true);
        whorl(3, 0.9, 0.70, 0.28, 1.047, true);
      } else if (s === 2) {
        whorl(8, 1.0, 0.68, 0.42, 0.0, false);
        whorl(8, 0.72, 0.55, 0.38, 0.392, false);
      } else {
        if (f.loose) {
          whorl(1, 1.0, 0.72, 0.44, 0.0, false);
        } else {
          whorl(5, 1.0, 0.66, 0.40, 0.0, false);
        }
      }

      flowers.push(f);
    }
    return flowers.sort((a, b) => a.depth - b.depth);
  });
}

export function update(dt, input, params) {
  dt = Number.isFinite(dt) ? clamp(dt, 0, 0.1) : 0;
  const values = input && input.knobValues;
  const read = (i, fallback) => values && Number.isFinite(values[i]) ? values[i] : fallback;

  const openTarget = clamp(read(0, params.doorOpen), 0, 1);
  const zoomTarget = clamp(read(1, params.zoomBase), 0.8, 2.5);
  const speedTarget = clamp(read(2, params.flowerSpeed), 0.2, 8.0);

  if (!params.controlsReady) {
    params.doorOpen = openTarget;
    params.zoomBase = zoomTarget;
    params.zoom = zoomTarget;
    params.flowerSpeed = speedTarget;
    params.controlsReady = true;
  }

  dampCabinet(params, 'doorOpen', 'doorVelocity', openTarget, params.doorResponse, dt, 0, 1);
  dampCabinet(params, 'zoomBase', 'zoomVelocity', zoomTarget, params.zoomResponse, dt, 0.8, 2.5);
  params.flowerSpeed += (speedTarget - params.flowerSpeed) * (1 - Math.exp(-dt * 5));

  const simDt = dt * params.flowerSpeed;
  params.time += simDt;

  const zoomT = clamp((params.zoomBase - 0.8) / (2.5 - 0.8), 0.0, 1.0);
  const breathAmp = 0.008 + Math.pow(zoomT, 1.4) * 0.047;
  const breath = Math.sin(params.time * 0.95) * breathAmp;
  params.zoom = clamp(params.zoomBase + breath, 0.78, 2.6);

  if (params.doorOpen < 0.05) {
    if (params.canSwitch) {
      params.sceneIndex = (params.sceneIndex + 1) % 4;
      params.canSwitch = false;
    }
  } else if (params.doorOpen > 0.38) {
    params.canSwitch = true;
  }

  const s = params.sceneIndex;
  const t = params.time;

  const xMin = 44, yMin = 12;
  const spanX = 39, spanY = 39;

  const flowers = params.scenes[s];
  const count = flowers.length;

  for (let i = 0; i < count; i++) {
    const f = flowers[i];
    const margin = f.baseR + 6;
    f.turn += simDt * f.turnSpeed * 1.5;

    for (let pIdx = 0; pIdx < f.petals.length; pIdx++) {
      const p = f.petals[pIdx];
      const ang = p.baseAngle + f.turn;
      p.cosA = Math.cos(ang);
      p.sinA = Math.sin(ang);
    }

    if (s === 0) {
      const flowRate = (0.05 * f.speedMult) * simDt;
      f.progressY = (f.progressY - flowRate + 1.0) % 1.0;
      f.x = xMin + f.progressX * spanX + Math.sin(t * f.driftFreq + f.phase) * f.driftAmp;
      f.y = (yMin - margin) + f.progressY * (spanY + margin * 2);
    } else if (s === 1) {
      const flowRate = (0.055 * f.speedMult) * simDt;
      f.progressX = (f.progressX + flowRate) % 1.0;
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2);
      f.y = yMin + f.progressY * spanY + Math.sin(t * f.driftFreq + f.phaseY) * f.driftAmp;
    } else if (s === 2) {
      const orbitSpeed = (0.24 * f.speedMult) * simDt;
      f.phase += orbitSpeed;
      f.phaseY += orbitSpeed * 0.75;
      f.x = 63.5 + Math.sin(f.phase) * (14.0 * (0.7 + f.depth * 0.3));
      f.y = 31.5 + Math.cos(f.phaseY) * (13.0 * (0.7 + f.depth * 0.3));
    } else {
      const flowRateX = (0.045 * f.speedMult) * simDt;
      const flowRateY = (0.07 * f.speedMult) * simDt;
      f.progressX = (f.progressX + flowRateX) % 1.0;
      f.progressY = (f.progressY + flowRateY) % 1.0;
      const sway = f.loose ? 3.2 : 1.8;
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2) + Math.sin(t * 1.2 * f.driftFreq + f.phase) * sway;
      f.y = (yMin - margin) + f.progressY * (spanY + margin * 2);
    }
  }
}

export function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const openK = params.doorOpen;
  const z = params.zoom;
  const s = params.sceneIndex;

  const cx = 63.5;
  const cy = 31.5;
  const invZ = 1.0 / z;

  function woodTexture(ox, oy, seed) {
    return (Math.sin(ox * 1.8 + Math.cos(oy * 0.15 + seed) * 3.2) * 0.5 +
            Math.sin(ox * 3.4 + oy * 0.5 + seed * 2.0) * 0.25) * 0.05;
  }

  // 1. 프레임 바운더리 산출 (빈틈 방지를 위한 정수 버퍼 정합)
  const boxLeft = Math.floor(cx + (38 - cx) * z);
  const boxRight = Math.ceil(cx + (89 - cx) * z);
  const boxTop = Math.floor(cy + (6 - cy) * z);
  const boxBottom = Math.ceil(cy + (57 - cy) * z);

  const innerLeft = Math.round(cx + (44 - cx) * z);
  const innerRight = Math.round(cx + (83 - cx) * z);
  const innerTop = Math.round(cy + (12 - cy) * z);
  const innerBottom = Math.round(cy + (51 - cy) * z);

  // 2. 외부 여백 초기화 (0.00 Black)
  for (let y = 0; y < h; y++) {
    const inBoxY = (y >= boxTop && y <= boxBottom);
    for (let x = 0; x < w; x++) {
      if (!inBoxY || x < boxLeft || x > boxRight) {
        display.setValue(x, y, 0.0);
      }
    }
  }

  // 3. 감실 내부 배면 (빈틈없이 1px 여유 패딩 매핑)
  const yMinInner = clamp(innerTop, 0, h - 1);
  const yMaxInner = clamp(innerBottom, 0, h - 1);
  const xMinInner = clamp(innerLeft, 0, w - 1);
  const xMaxInner = clamp(innerRight, 0, w - 1);

  const bgSlots = [0.04, 0.08, 0.12, 0.16];
  const flatBGTone = bgSlots[s % 4];

  for (let py = yMinInner; py <= yMaxInner; py++) {
    const isTopBottomEdge = (py === innerTop || py === innerBottom);
    for (let px = xMinInner; px <= xMaxInner; px++) {
      if (isTopBottomEdge || px === innerLeft || px === innerRight) {
        display.setValue(px, py, 0.20);
      } else {
        display.setValue(px, py, flatBGTone);
      }
    }
  }

  // 4. 대형 꽃 래스터화
  const tierBases = [0.55, 0.64, 0.74, 0.82, 0.91];
  const flowerList = params.scenes[s];

  for (let i = 0; i < flowerList.length; i++) {
    const f = flowerList[i];
    const baseTone = tierBases[f.colorTier];

    const screenFX = cx + (f.x - cx) * z;
    const screenFY = cy + (f.y - cy) * z;
    const screenR = f.baseR * z * 1.5 + 2;

    const minPX = clamp(Math.floor(screenFX - screenR), xMinInner, xMaxInner);
    const maxPX = clamp(Math.ceil(screenFX + screenR), xMinInner, xMaxInner);
    const minPY = clamp(Math.floor(screenFY - screenR), yMinInner, yMaxInner);
    const maxPY = clamp(Math.ceil(screenFY + screenR), yMinInner, yMaxInner);

    if (minPX > maxPX || minPY > maxPY) continue;

    const hasLeaf = f.hasLeaf;
    const lx = f.x + Math.cos(f.leafAngle) * (f.baseR + 1.4);
    const ly = f.y + Math.sin(f.leafAngle) * (f.baseR + 1.4);

    for (let py = minPY; py <= maxPY; py++) {
      const oy = (py - cy) * invZ + cy;
      const dy = oy - f.y;
      const v = dy * f.invR;

      for (let px = minPX; px <= maxPX; px++) {
        const ox = (px - cx) * invZ + cx;
        const dx = ox - f.x;
        const distSq = dx * dx + dy * dy;

        if (distSq <= 1.8) {
          display.setValue(px, py, 0.99);
          continue;
        }

        if (hasLeaf) {
          const ldx = ox - lx;
          const ldy = (oy - ly) * 1.4;
          if (ldx * ldx + ldy * ldy <= 4.0) {
            display.setValue(px, py, 0.52);
          }
        }

        const u = dx * f.invR;

        for (let pIdx = 0; pIdx < f.petals.length; pIdx++) {
          const p = f.petals[pIdx];
          const nx = u * p.cosA + v * p.sinA;
          const ny = -u * p.sinA + v * p.cosA;

          if (nx > -p.offset && nx < p.length) {
            const normX = (nx + p.offset) * p.invSpan;

            let yBound;
            if (s === 2) {
              yBound = p.width * Math.sin(normX * 3.14159) * (1.1 - normX * 0.3);
            } else if (s === 1) {
              yBound = p.width * Math.sin(normX * 3.14159) * (1.0 - normX * 0.4);
            } else {
              yBound = p.width * Math.sqrt(normX) * (1.0 - normX * 0.95);
            }

            const absNY = Math.abs(ny);
            if (absNY < yBound) {
              if (p.notch && normX > 0.78 && absNY < yBound * 0.35) {
                continue;
              }
              const shading = (1.0 - normX) * 0.03 + p.shade;
              display.setValue(px, py, baseTone + shading);
              break;
            }
          }
        }
      }
    }
  }

  // 5. 감실 기둥 및 인방/문지방 보 (상하 1px 틈새 완전 밀봉)
  const yPostMin = clamp(Math.floor(cy + (2 - cy) * z), 0, h - 1);
  const yPostMax = clamp(Math.ceil(cy + (61 - cy) * z), 0, h - 1);

  for (let py = yPostMin; py <= yPostMax; py++) {
    const oy = (py - cy) * invZ + cy;

    const xL0 = clamp(boxLeft, 0, w - 1);
    const xL1 = clamp(innerLeft - 1, 0, w - 1);
    for (let px = xL0; px <= xL1; px++) {
      const ox = (px - cx) * invZ + cx;
      display.setValue(px, py, clamp(0.35 + woodTexture(ox, oy, 1.2), 0.10, 0.48));
    }

    const xR0 = clamp(innerRight + 1, 0, w - 1);
    const xR1 = clamp(boxRight, 0, w - 1);
    for (let px = xR0; px <= xR1; px++) {
      const ox = (px - cx) * invZ + cx;
      display.setValue(px, py, clamp(0.34 + woodTexture(ox, oy, 2.7), 0.10, 0.48));
    }
  }

  // 상단 인방 보: boxTop ~ innerTop까지 1px 중첩되도록 닫음
  const xBeam0 = clamp(innerLeft, 0, w - 1);
  const xBeam1 = clamp(innerRight, 0, w - 1);
  const yTop0 = clamp(boxTop, 0, h - 1);
  const yTop1 = clamp(innerTop, 0, h - 1);

  for (let py = yTop0; py <= yTop1; py++) {
    const oy = (py - cy) * invZ + cy;
    const isEdge = (py === boxTop);
    for (let px = xBeam0; px <= xBeam1; px++) {
      const ox = (px - cx) * invZ + cx;
      display.setValue(px, py, clamp((isEdge ? 0.46 : 0.36) + woodTexture(ox * 0.5, oy * 1.5, 3.5), 0.12, 0.48));
    }
  }

  // 하단 문지방 보: innerBottom ~ boxBottom까지 1px 중첩되도록 닫음
  const yBot0 = clamp(innerBottom, 0, h - 1);
  const yBot1 = clamp(boxBottom, 0, h - 1);

  for (let py = yBot0; py <= yBot1; py++) {
    const oy = (py - cy) * invZ + cy;
    for (let px = xBeam0; px <= xBeam1; px++) {
      const ox = (px - cx) * invZ + cx;
      display.setValue(px, py, clamp(0.30 + woodTexture(ox * 0.5, oy * 1.5, 4.8), 0.10, 0.46));
    }
  }

  // 기둥 귀퉁이 쐐기 홈
  const jointCorners = [
    [boxLeft + Math.round(1 * z), Math.floor(cy + (3 - cy) * z)],
    [boxRight - Math.round(2 * z), Math.floor(cy + (3 - cy) * z)],
    [boxLeft + Math.round(1 * z), Math.ceil(cy + (60 - cy) * z)],
    [boxRight - Math.round(2 * z), Math.ceil(cy + (60 - cy) * z)]
  ];
  for (let c = 0; c < 4; c++) {
    const jx = jointCorners[c][0];
    const jy = jointCorners[c][1];
    if (jx >= 0 && jx < w && jy >= 0 && jy < h) display.setValue(jx, jy, 0.10);
    if (jx + 1 >= 0 && jx + 1 < w && jy >= 0 && jy < h) display.setValue(jx + 1, jy, 0.10);
  }

  // 6. 여닫이 널문
  const hingeLeft = innerLeft;
  const hingeRight = innerRight;
  const doorWidth = 20.8 * z;

  const angle = openK * 2.89;
  const cosA = Math.cos(angle);
  const sinA = Math.sin(angle);

  const leftTipX = hingeLeft + doorWidth * cosA;
  const rightTipX = hingeRight - doorWidth * cosA;

  function drawDoorPanel(xStart, xEnd, tipX, isLeft) {
    const xMin = clamp(Math.floor(Math.min(xStart, xEnd)), 0, w - 1);
    const xMax = clamp(Math.ceil(Math.max(xStart, xEnd)), 0, w - 1);
    const span = Math.max(1, xMax - xMin);

    for (let px = xMin; px <= xMax; px++) {
      const ox = (px - cx) * invZ + cx;
      const u = (px - xMin) / span;

      const splinterBot = innerBottom + 3.5 * z + Math.sin(ox * 1.8) * (1.2 * z);
      const splinterTop = innerTop - 2.0 * z + Math.sin(ox * 1.3) * (0.8 * z);

      const pyMin = clamp(Math.floor(splinterTop), 0, h - 1);
      const pyMax = clamp(Math.ceil(splinterBot), 0, h - 1);

      for (let py = pyMin; py <= pyMax; py++) {
        const oy = (py - cy) * invZ + cy;
        const isBackFace = (cosA < 0.0);
        let doorTone = (isBackFace ? 0.40 : 0.36) + woodTexture(ox * 2.0, oy * 0.8, isLeft ? 5.7 : 8.9);

        if (Math.floor(u * 3) !== Math.floor((u + 0.08) * 3)) {
          doorTone -= 0.10;
        }

        const isBatten = (oy >= 21.0 && oy <= 24.0) || (oy >= 40.0 && oy <= 43.0);
        if (isBatten) {
          doorTone += 0.07;
          if (oy > 23.0 || oy > 42.0) doorTone -= 0.10;
        }

        if (Math.abs(px - tipX) < 1.4 * z) {
          doorTone += 0.07 * sinA;
        }

        display.setValue(px, py, clamp(doorTone, 0.10, 0.48));
      }
    }
  }

  if (Math.abs(leftTipX - hingeLeft) > 0.8) {
    drawDoorPanel(hingeLeft, leftTipX, leftTipX, true);
  }
  if (Math.abs(rightTipX - hingeRight) > 0.8) {
    drawDoorPanel(hingeRight, rightTipX, rightTipX, false);
  }

  // 철물 경첩 핀
  const hingeY1 = Math.round(cy + (18 - cy) * z);
  const hingeY2 = Math.round(cy + (45 - cy) * z);
  const hinges = [
    [innerLeft - 1, hingeY1], [innerLeft - 1, hingeY2],
    [innerRight + 1, hingeY1], [innerRight + 1, hingeY2]
  ];
  for (let hg = 0; hg < 4; hg++) {
    const hx = hinges[hg][0];
    const hy = hinges[hg][1];
    if (hx >= 0 && hx < w && hy - 1 >= 0 && hy - 1 < h) display.setValue(hx, hy - 1, 0.08);
    if (hx >= 0 && hx < w && hy >= 0 && hy < h) display.setValue(hx, hy, 0.46);
    if (hx >= 0 && hx < w && hy + 1 >= 0 && hy + 1 < h) display.setValue(hx, hy + 1, 0.08);
  }
}

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
