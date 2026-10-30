// 참고용 가로 버전 - 전시 패널에는 choegyeongeun_portrait 사용
// @ramp oklab 0.035:#cc0000 0.6:#ffea00 0.207:#330000 1:#ffffff 0.497:#cd7918 0.552:#ff5900 0.7:#ff00c8 0.855:#0045e6 0:#000000 0.078:#6b98c2 0.116:#ffc96b 0.152:#c7c7c7
// @knobs DoorOpen=0..1, Zoom=0..1, FlowerSpeed=0..1, -
// @matrix 128x64

// Knob 1 (DoorOpen): 궤짝 널문 개폐 (0: 완전 밀폐, 1: 와이드 개방) - 묵직한 관성
//                    열림 = 3.0p - 3.6p^2 + 1.6p^3 (가운데 0.5 → 0.80, 문짝이 양옆으로 젖혀져 감실이 드러남)
//                    완전히 닫았다(위치 0 = CC 0) 다시 열면(5칸째 = 위치 5/32, CC 약 20 이상) 다음 꽃 장면으로 넘어감
//                    닫혀 있을 때는 문틈으로 감실 빛이 숨쉬듯 새어 나오고, 뒤를 지나는 꽃빛이 그 위에 비침
//                    (문짝이 실제로 겹쳐 있는 동안 계속 켜져 있다가, 틈이 벌어지면서 사라짐)
// Knob 2 (Zoom): 감실 줌, 위치 0..1 → 배율 0.8 ~ 2.4 지수 곡선 (가운데 1.15 = 궤짝 전체가 크게)
//                + 줌 크기 비례 가변 숨쉬기
// Knob 3 (FlowerSpeed): 꽃의 이동 및 자전 회전 속도, 위치 0..1 → 0.35 ~ 8.0 지수 곡선 (가운데 약 3.7)
// K1 누르기: 문이 저절로 닫혔다가(약 0.4초) 다음 꽃 장면으로 다시 열림 - 누를 때마다 정확히 한 장면
//            (닫는 도중에 또 누르면 그 닫힘에 합쳐짐, 노브로 문을 닫아 둔 상태면 바로 한 장면 넘기고
//             문이 잠깐 빼꼼 열렸다가(약 2초) 다시 닫혀 새 장면을 보여 줌)
//            대기 복귀 때 되돌릴 상태: pressClosing = false, pressPeek = 0, pressCool = 0, sceneIndex = 0, canSwitch = false
// Knob 4: 미사용 (패턴 전환)

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
  // K1 누르기 상태 (대기 복귀 때 이 값으로 되돌림)
  params.pressClosing = false; // 누르기로 문을 닫는 중 (닫히면 한 장면 넘기고 false)
  params.pressCool = 0;        // 장면을 넘긴 직후 누르기를 잠깐 무시하는 시간(초)
  params.pressPeek = 0;        // 장면을 넘긴 뒤 문이 빼꼼 열리는 정도 1 → 0 (지수 감쇠, 아주 작아지면 0으로)
  // 누적 위상은 전부 정확한 주기로 감는다 (며칠 켜 두어도 값이 커지지 않고, 감는 순간 화면이 튀지 않음)
  params.breathPhase = 0; // 숨쉬기 sin(breathPhase), 2π로 감음
  params.leakPhase = 0;   // 빛샘 숨쉬기 sin(leakPhase), 실제 시간 × 1.7, 2π로 감음
  // 문짝 나뭇결의 행별 항 (프레임마다 다시 채움, 각 512 B)
  params.doorRowWarp = new Float32Array(128);
  params.doorRowSinB = new Float32Array(128);
  params.doorRowCosB = new Float32Array(128);

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
        driftPhase: 0, // 흔들림 위상: simDt × driftFreq 누적, 10π로 감음 (장면 3은 1.2배로 씀)
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

  // 노브 위치(0..1) → 실제 값. 위치는 32칸, 가운데 0.5에서 시작.
  const openPos = clamp(read(0, 1), 0, 1);
  const zoomPos = clamp(read(1, 0.5), 0, 1);
  const speedPos = clamp(read(2, 0.5), 0, 1);
  // 문: 반쯤 돌렸을 때 이미 문짝이 양옆으로 젖혀지도록 완만한 곡선
  //     3.0p - 3.6p^2 + 1.6p^3 → 0 / 0.80 / 1 (단조 증가), 닫힘(0.05 미만)은 맨 아래 칸에서만
  const openTarget = openPos * (3.0 - openPos * (3.6 - openPos * 1.6));
  // 줌: 0.8 + 0.1361*(e^(2.546p) - 1) → 0.8 / 1.15 / 2.4
  const zoomTarget = 0.8 + 0.1361 * (Math.exp(2.546 * zoomPos) - 1);
  // 속도: 0.35*e^(3.129*g), g = p*(2.5 - p*(2.5 - p)) → 0.35 / 3.66 / 8.0
  //       g는 단조 증가(기울기 2.5 → 최소 0.42 → 0.5), 꺾임 없음
  const speedG = speedPos * (2.5 - speedPos * (2.5 - speedPos));
  const speedTarget = 0.35 * Math.exp(3.129 * speedG);

  if (!params.controlsReady) {
    params.doorOpen = openTarget;
    params.zoomBase = zoomTarget;
    params.zoom = zoomTarget;
    params.flowerSpeed = speedTarget;
    params.controlsReady = true;
  }

  // K1 누르기: 누르면 문 목표를 0(닫힘)으로 덮어씀 → 기존 감쇠가 닫는 움직임을 그대로 그림
  //   닫는 도중에 또 누르면 같은 닫힘에 합쳐짐 (한 번 닫힐 때 한 장면만)
  // 막 넘긴 직후(0.25초)에는 누르기를 무시한다 - 연타하면 한 프레임짜리 장면이 생기므로
  params.pressCool = Math.max(0, (params.pressCool || 0) - dt);
  if (input && input.btnPressed && input.btnPressed[0] && params.pressCool === 0) params.pressClosing = true;
  // 빼꼼 열림: 장면을 넘긴 뒤 약 0.4초 0.34까지 유지하다 약 2초에 걸쳐 0으로 (노브가 더 열려 있으면 노브가 이김)
  params.pressPeek *= Math.exp(-dt * 1.2);
  if (params.pressPeek < 0.002) params.pressPeek = 0;
  // 노브가 빼꼼 열림보다 더 열려 있으면 빼꼼은 거기서 끝 - 그 뒤 노브로 닫을 때 문이 멈칫하지 않게
  if (openTarget >= 0.34 * Math.min(1, params.pressPeek * 1.6)) params.pressPeek = 0;
  let doorTarget = openTarget;
  if (params.pressClosing) doorTarget = 0;
  else if (params.pressPeek > 0) doorTarget = Math.max(openTarget, 0.34 * Math.min(1, params.pressPeek * 1.6));

  dampCabinet(params, 'doorOpen', 'doorVelocity', doorTarget, params.doorResponse, dt, 0, 1);
  dampCabinet(params, 'zoomBase', 'zoomVelocity', zoomTarget, params.zoomResponse, dt, 0.8, 2.4);
  params.flowerSpeed += (speedTarget - params.flowerSpeed) * (1 - Math.exp(-dt * 5));

  const simDt = dt * params.flowerSpeed;
  // C++ 번역 시: 이 주기(2π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  params.breathPhase += simDt * 0.95;
  if (params.breathPhase >= 6.2831853) params.breathPhase -= 6.2831853;
  // 빛샘은 꽃 속도와 무관한 실제 시간으로 숨쉰다 (속도 최소에서도 닫힌 궤짝이 움직이도록)
  // C++ 번역 시: 이 주기(2π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  params.leakPhase += dt * 1.7;
  if (params.leakPhase >= 6.2831853) params.leakPhase -= 6.2831853;

  const zoomT = clamp((params.zoomBase - 0.8) / (2.4 - 0.8), 0.0, 1.0);
  const breathAmp = 0.008 + Math.pow(zoomT, 1.4) * 0.047;
  const breath = Math.sin(params.breathPhase) * breathAmp;
  params.zoom = clamp(params.zoomBase + breath, 0.78, 2.5);

  if (params.pressClosing) {
    // 누르기로 닫는 중: 눈에 보이게 닫히면(0.05 미만) 정확히 한 장면 넘기고 덮어쓰기를 풀어 노브 위치로 다시 열림
    //   canSwitch도 내려서 같은 닫힘으로 노브 쪽 전환이 한 번 더 일어나지 않게 함
    if (params.doorOpen < 0.05) {
      params.sceneIndex = (params.sceneIndex + 1) % 4;
      params.canSwitch = false;
      params.pressClosing = false;
      params.pressPeek = 1;
      params.pressCool = 0.25;
    }
  } else if (params.doorOpen < 0.05) {
    if (params.canSwitch) {
      params.sceneIndex = (params.sceneIndex + 1) % 4;
      params.canSwitch = false;
    }
  } else if (params.doorOpen > 0.38) {
    params.canSwitch = true;
  }

  const s = params.sceneIndex;


  const xMin = 44, yMin = 12;
  const spanX = 39, spanY = 39;

  const flowers = params.scenes[s];
  const count = flowers.length;

  for (let i = 0; i < count; i++) {
    const f = flowers[i];
    const margin = f.baseR + 6;
    // C++ 번역 시: turn / driftPhase / (장면 2의) phase, phaseY 는 아래 주기 그대로 감을 것 - 다른 값이면 화면이 튄다
    f.turn += simDt * f.turnSpeed * 1.5; // 각도로만 쓰임 → 2π
    if (f.turn >= 6.2831853) f.turn -= 6.2831853;
    else if (f.turn < 0) f.turn += 6.2831853;
    f.driftPhase += simDt * f.driftFreq; // sin(driftPhase), sin(1.2*driftPhase) 둘 다 정확 → 10π
    if (f.driftPhase >= 31.415927) f.driftPhase -= 31.415927;

    for (let pIdx = 0; pIdx < f.petals.length; pIdx++) {
      const p = f.petals[pIdx];
      const ang = p.baseAngle + f.turn;
      p.cosA = Math.cos(ang);
      p.sinA = Math.sin(ang);
    }

    if (s === 0) {
      const flowRate = (0.05 * f.speedMult) * simDt;
      f.progressY = (f.progressY - flowRate + 1.0) % 1.0;
      f.x = xMin + f.progressX * spanX + Math.sin(f.driftPhase + f.phase) * f.driftAmp;
      f.y = (yMin - margin) + f.progressY * (spanY + margin * 2);
    } else if (s === 1) {
      const flowRate = (0.055 * f.speedMult) * simDt;
      f.progressX = (f.progressX + flowRate) % 1.0;
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2);
      f.y = yMin + f.progressY * spanY + Math.sin(f.driftPhase + f.phaseY) * f.driftAmp;
    } else if (s === 2) {
      const orbitSpeed = (0.24 * f.speedMult) * simDt;
      f.phase += orbitSpeed;         // sin/cos 로만 쓰임 → 2π
      f.phaseY += orbitSpeed * 0.75;
      if (f.phase >= 6.2831853) f.phase -= 6.2831853;
      if (f.phaseY >= 6.2831853) f.phaseY -= 6.2831853;
      f.x = 63.5 + Math.sin(f.phase) * (14.0 * (0.7 + f.depth * 0.3));
      f.y = 31.5 + Math.cos(f.phaseY) * (13.0 * (0.7 + f.depth * 0.3));
    } else {
      const flowRateX = (0.045 * f.speedMult) * simDt;
      const flowRateY = (0.07 * f.speedMult) * simDt;
      f.progressX = (f.progressX + flowRateX) % 1.0;
      f.progressY = (f.progressY + flowRateY) % 1.0;
      const sway = f.loose ? 3.2 : 1.8;
      f.x = (xMin - margin) + f.progressX * (spanX + margin * 2) + Math.sin(1.2 * f.driftPhase + f.phase) * sway;
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
    const seed = isLeft ? 5.7 : 8.9;
    const baseTone = (cosA < 0.0) ? 0.40 : 0.36; // 뒷면이 보이면 조금 밝게

    // 나뭇결 woodTexture(ox*2, oy*0.8, seed)를 행(row) 항과 열(column) 항으로 나눠 미리 계산
    //   sin(a2 + b) = sin(a2)cos(b) + cos(a2)sin(b) 이라 값은 픽셀마다 계산하던 것과 같음
    //   (픽셀당 삼각함수 3개 → 1개)
    const rowWarp = params.doorRowWarp;
    const rowSinB = params.doorRowSinB;
    const rowCosB = params.doorRowCosB;
    const rMin = clamp(Math.floor(innerTop - 2.8 * z), 0, h - 1);
    const rMax = clamp(Math.ceil(innerBottom + 4.7 * z), 0, h - 1);
    for (let py = rMin; py <= rMax; py++) {
      const oy = (py - cy) * invZ + cy;
      rowWarp[py] = Math.cos(oy * 0.8 * 0.15 + seed) * 3.2;
      const b = oy * 0.8 * 0.5;
      rowSinB[py] = Math.sin(b);
      rowCosB[py] = Math.cos(b);
    }

    for (let px = xMin; px <= xMax; px++) {
      const ox = (px - cx) * invZ + cx;
      const u = (px - xMin) / span;
      const colA1 = ox * 2.0 * 1.8;
      const colA2 = ox * 2.0 * 3.4 + seed * 2.0;
      const colS2 = Math.sin(colA2) * 0.25;
      const colC2 = Math.cos(colA2) * 0.25;
      const isGroove = Math.floor(u * 3) !== Math.floor((u + 0.08) * 3);
      const isTip = Math.abs(px - tipX) < 1.4 * z;

      const splinterBot = innerBottom + 3.5 * z + Math.sin(ox * 1.8) * (1.2 * z);
      const splinterTop = innerTop - 2.0 * z + Math.sin(ox * 1.3) * (0.8 * z);

      const pyMin = clamp(Math.floor(splinterTop), 0, h - 1);
      const pyMax = clamp(Math.ceil(splinterBot), 0, h - 1);

      for (let py = pyMin; py <= pyMax; py++) {
        const oy = (py - cy) * invZ + cy;
        let doorTone = baseTone +
          (Math.sin(colA1 + rowWarp[py]) * 0.5 + colS2 * rowCosB[py] + colC2 * rowSinB[py]) * 0.05;

        if (isGroove) {
          doorTone -= 0.10;
        }

        const isBatten = (oy >= 21.0 && oy <= 24.0) || (oy >= 40.0 && oy <= 43.0);
        if (isBatten) {
          doorTone += 0.07;
          if (oy > 23.0 || oy > 42.0) doorTone -= 0.10;
        }

        if (isTip) {
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

  // 7. 문틈 빛샘: 닫힌 문의 이음매로 감실 빛이 숨쉬듯 새어 나오고, 뒤를 지나는 꽃빛이 그 위에 비침
  //    문짝이 실제로 겹친 만큼(닫힘에서 약 2.6z px) 켜져 있다가, 두 문 끝이 약 3 px 벌어질 때까지 사라짐
  //    (그때쯤엔 진짜 틈으로 감실이 보이므로 닫힌 것처럼 보이는 칸은 모두 빛샘이 켜져 있다)
  const leak = clamp((leftTipX - rightTipX + 3.0) / (2.6 * z + 3.0), 0, 1);
  if (leak > 0) {
    const seamX = clamp(Math.floor((leftTipX + rightTipX) * 0.5), 0, w - 1);
    const seamOX = (seamX - cx) * invZ + cx;
    const leakWave = Math.sin(params.leakPhase);
    const leakWave2 = Math.cos(params.leakPhase);
    // 꽃빛 반경(꽃 크기의 0.35~0.75배)도 숨쉼: 꽃 중심 위쪽은 sin, 아래쪽은 cos
    //   (큰 꽃이 이음매에 걸쳐 있어도 가장자리가 늘 움직이고 바탕 빛이 사이사이 보임)
    const glowUp = leak * (0.55 + 0.2 * leakWave);
    const glowDown = leak * (0.55 + 0.2 * leakWave2);
    const lyMin = clamp(innerTop + 1, 0, h - 1);
    const lyMax = clamp(innerBottom - 1, 0, h - 1);
    // 꺼지지 않는 바탕: 이음매 가운데를 중심으로 감실 배면색(조금 밝게)이 안쪽 높이의
    //   (0.35 + 0.25 sin) 만큼 늘었다 줄었다 함 → 속도 최소에서도 닫힌 궤짝이 늘 움직임
    //   위 끝은 sin, 아래 끝은 cos(1/4 주기 늦게)로 움직여 한쪽 끝이 멈칫할 때도 다른 끝은 움직인다
    const runScale = 0.5 * (innerBottom - innerTop) * Math.min(1, leak * 2);
    const runUp = (0.35 + 0.25 * leakWave) * runScale;
    const runDown = (0.35 + 0.25 * leakWave2) * runScale;
    const runMid = (innerTop + innerBottom) * 0.5;
    // 램프에서 각 배면색과 같은 색조로 조금 더 밝은 자리 (OKLab 밝기 기준: 0.04→0.045, 0.08→0.085, 0.12 그대로, 0.16→0.155)
    const leakSlots = [0.045, 0.085, 0.12, 0.155];
    const runTone = leakSlots[s % 4];
    const ryMin = clamp(Math.round(runMid - runUp), lyMin, lyMax);
    const ryMax = clamp(Math.round(runMid + runDown), lyMin, lyMax);
    for (let py = ryMin; py <= ryMax; py++) display.setValue(seamX, py, runTone);
    for (let py = lyMin; py <= lyMax; py++) {
      const oy = (py - cy) * invZ + cy;
      let tone = -1;
      for (let i = 0; i < flowerList.length; i++) {
        const f = flowerList[i];
        const dx = seamOX - f.x;
        const dy = oy - f.y;
        const d2 = dx * dx + dy * dy;
        const rr = f.baseR * (dy < 0 ? glowUp : glowDown);
        if (d2 <= 1.8) {
          tone = 0.99;
        } else if (d2 < rr * rr) {
          tone = tierBases[f.colorTier] + 0.02;
        }
      }
      if (tone > 0) display.setValue(seamX, py, tone);
    }
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
