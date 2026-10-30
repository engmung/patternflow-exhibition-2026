// @ramp oklab 0:#00103f 0.24:#2f9be6 0.421:#b8feff 0.509:#ff0000 0.664:#99ff00 0.984:#ffdd00 1:#000000 0.839:#f000d0 0.47:#fbff00
// @knobs FishCount=4..24, CurrentLoop=0..1, SwimSpeed=6..24, -
// @matrix 64x128


// Knob 1 (FishCount): 화면에 등장하는 물고기 수 (4..24, 가운데 14)
// Knob 2 (CurrentLoop): 0이면 바닥 평온 순항, 올리면 가로 유화 결 전체가 물살을 타고 화면 전체 대회전
//   위치 p를 loop = p * (1.5 - 0.5 * p)로 휘어 씀 (가운데 0.625, 양 끝 0과 1). 약 0.2초에 걸쳐 부드럽게 따라감
// Knob 3 (SwimSpeed): 전체 유영 및 해류 순항 속도 (6..24, 가운데 15)
// Knob 4: 미사용 (패턴 전환 전용)
// K1 누르기: 물고기 놀람(잔잔하게) - 누를 때마다 떼 한가운데 근처의 물고기 하나(화면 안 떼의 중심 + 누른 횟수로
//   정해지는 어긋남에 가장 가까운 물고기)의 코앞에서 놀람이 난다. 그 점에서 12px 안의 물고기는 누른 순간 정한 방향
//   (점에서 멀어지는 쪽, 벽이 가까우면 벽을 따라 안쪽으로 비스듬히)으로 옆으로 비켜 미끄러지고, 28px까지 부드럽게
//   옅어져 그 밖의 물고기는 그대로 헤엄친다. 봉투 e는 startleS^2(1.3초에 걸쳐 1 -> 0)를 약 0.15초 늦게 따라가
//   0에서 스르르 올라(누른 뒤 약 0.2초에 최고 약 0.7) 1~1.5초 사이에 잦아든다. 목표 방향은 물살에서 최대 약 90도까지만
//   돌리고(뒤로 휙 돌지 않음), 몸은 도는 반경을 넓게 제한해 천천히 돌며, 속도는 최대 1.5배(SwimSpeed 15 넘으면 그만큼
//   덜). 봉투가 줄면 원래 물살로 합류한다. 벽 앞에서는 비켜나기를 풀고(물살 대회전에서는 반응한 물고기를 벽 안쪽으로
//   살짝 밀어) 벽에 줄지어 붙지 않는다. 화면 밖에서 재진입을 기다리는 물고기는 놀라지 않는다. 가로 순항(K2 아래쪽)
//   에서는 약 6초에 걸쳐 제 수심으로 돌아간다.
//   다시 누르면 새 점에서 다시 놀라되 봉투는 지금 높이에서 이어 오른다(연타해도 툭툭 끊기거나 날뛰지 않음). K2/K3 누르기는 미사용
// 대기 복귀(3분) 때 되돌릴 상태: startleS = 0, startleE = 0, calmS = 0, startleN = 0, 물고기별 fleeR = 0
//   (startleX/Y와 물고기별 fleeA/fleeK는 누를 때 새로 정해지고, startleE = 0이면 쓰이지 않는다.
//    fleeR도 startleE = 0이면 쓰이지 않지만, 0으로 되돌려 두면 대기 복귀 직후 첫 누름이 깨끗이 시작된다)

export function setup(params) {
  params.time = 0;
  params.fishes = [];

  // K1 누르기(놀람) 상태
  params.startleS = 0; // 놀람 진행도 1 -> 0 (1.3초)
  params.startleE = 0; // 놀람 봉투: startleS^2를 약 0.15초 늦게 부드럽게 따라감 (0에서 스르르 오르고, 다시 눌러도 끊기지 않게)
  params.calmS = 0; // 놀란 뒤 제 수심으로 돌아가는 힘 1 -> 0 (약 6초)
  params.startleN = 0; // 누른 횟수 (0..63에서 돈다) - 놀람 지점을 고르는 데만 쓴다
  params.startleX = 32; // 놀람 지점 (누를 때 정해짐, 물고기별 fleeA를 정하는 데만 쓰고 기록해 둔다)
  params.startleY = 64;

  // 물살 회전 가중치는 픽셀 위치에만 의존 → 한 번만 계산해 표로 저장
  // (rot = k * swirlW[i], 원본의 bandWeight + 반경 항과 동일)
  // C++ 번역 시: 64x128 float 표 = 32 KB → PFMem(PSRAM)으로 할당할 것
  const W = 64;
  const H = 128;
  const cx = 32;
  const cy = 64;
  const rx = 20.0;
  const ry = 46.0;
  params.swirlW = new Float32Array(W * H);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const dx = x - cx;
      const dy = (y - cy) * (rx / ry);
      const r = Math.hypot(dx, dy);
      const u = (r - rx) / 12.0;
      const bandWeight = Math.exp(-(u * u));
      params.swirlW[y * W + x] = 1.75 * bandWeight + 0.3 * (1.0 - Math.min(1.0, r / 42.0));
    }
  }

  const tones = [0.98, 0.88, 0.78, 0.65]; // 원작 컬러 대응 고유 톤

  for (let i = 0; i < 24; i++) {
    const type = i % 4;
    let len = 11;
    let height = 3.2;

    if (type === 0) {
      // 대형 마름모 체형 (등이 솟고 꼬리가 단정한 원작 중심 어종)
      len = 13 + (i % 3) * 1.8;
      height = 4.3 + (i % 2) * 0.6;
    } else if (type === 1) {
      // V자 갈래 꼬리형 (유선형 몸체 + V자 포크 테일)
      len = 11 + (i % 2) * 2.0;
      height = 3.3;
    } else if (type === 2) {
      // 아담한 송사리형 (둥근 배 + 잎사귀형 유선 꼬리)
      len = 7.2 + (i % 3) * 1.2;
      height = 2.4 + (i % 2) * 0.5;
    } else {
      // 슬림 피라미형 (길고 유연한 바디 + 뾰족하게 빠지는 유선 꼬리)
      len = 13.5 + (i % 2) * 2.0;
      height = 2.5;
    }

    // 개체의 80% 이상이 화면 맨 아래쪽 심해(y: 92 ~ 124)에 조밀하게 집중
    const depthBias = Math.pow(i / 23.0, 2.6);
    const bottomY = 122 - depthBias * 64;

    params.fishes.push({
      id: i,
      type: type,
      x: -18 + ((i * 19.3) % 98),
      y: bottomY,
      baseY: bottomY,
      angle: Math.PI,
      speedBase: 12.0 + ((i * 3.7) % 8.5),
      len: len,
      height: height,
      tone: tones[i % tones.length],
      wag: i * 1.5,
      phase: i * 2.2,
      orbitRx: 19 + ((i * 4.3) % 7),
      orbitRy: 44 + ((i * 5.7) % 8),
      fleeA: 0, // 놀람: 누른 순간 정한 달아나는 방향 (라디안)
      fleeK: 0, // 놀람: 이 물고기가 비켜나는 세기 0..1 (누를 때 정하고, 벽 앞에서 줄기만 한다)
      fleeR: 0 // 놀람: 이번 놀람에 반응한 물고기면 > 0 (몸을 천천히 돌리는 제한을 봉투가 끝날 때까지 유지)
    });
  }
}

// (x, y)에서 (ux, uy) 방향으로 화면 가장자리까지의 거리 (화면 안 점만 넘길 것)
function rayToWall(x, y, ux, uy) {
  let hit = 1000.0;
  if (ux < -0.02) hit = x / -ux;
  else if (ux > 0.02) hit = (63.0 - x) / ux;
  if (uy < -0.02) hit = Math.min(hit, y / -uy);
  else if (uy > 0.02) hit = Math.min(hit, (127.0 - y) / uy);
  return hit;
}

export function update(dt, input, params) {
  const countKnob = input ? input.knobValues[0] : 14;
  const loopKnob = input ? input.knobValues[1] : 0.5;
  const speedKnob = input ? input.knobValues[2] : 15.0;

  const simDt = Math.min(dt, 0.05) * (speedKnob * 0.15);
  params.time += simDt;
  // params.time은 sin/cos(t * f + c)로만 쓰이고 f가 모두 0.1의 배수(1.2, 1.4, 0.8, 2.2, 1.3, 0.6, 1.1, 1.0)라 20π에서 정확히 이어진다.
  // C++ 번역 시: 이 주기(20π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  const TIME_WRAP = 20.0 * Math.PI;
  if (params.time >= TIME_WRAP) params.time -= TIME_WRAP;
  params.fishCount = Math.min(24, Math.max(4, Math.round(countKnob)));

  // 위치를 휘어 가운데를 0.625로 (물고기가 소용돌이를 타고 화면 안에 머무는 구간)
  const p = Math.max(0.0, Math.min(1.0, loopKnob));
  const loopTarget = p * (1.5 - 0.5 * p);
  // 한 칸 돌릴 때 유화 결이 툭 튀지 않도록 약 0.2초에 걸쳐 따라감
  if (params.loopS === undefined) params.loopS = loopTarget;
  params.loopS += (loopTarget - params.loopS) * Math.min(1.0, dt * 6.0);
  params.loop = params.loopS;

  const k = params.loop;
  const t = params.time;
  const cx = 32;
  const cy = 64;

  // K1 누르기: 물고기 놀람. 누른 그 프레임부터 e = 1
  const dtc = Math.min(dt, 0.05);
  if (input && input.btnPressed && input.btnPressed[0]) {
    const n = params.startleN;
    // 놀람 지점: 화면 안 물고기 떼의 중심 + 누를 때마다 다른 어긋남(황금비 수열, x ±12, y ±16)에
    // 가장 가까운 물고기를 골라 그 코앞(머리 쪽 6px) → 그 물고기는 뒤돌아 달아나고 이웃들은 부채꼴로 흩어진다
    // (물살 대회전에서는 떼의 중심이 빈 물 한가운데라, 중심에서 터지면 모두 벽 쪽으로만 달아난다)
    // 화면에 물고기가 없으면 화면 가운데 기준
    let gx = 0.0;
    let gy = 0.0;
    let gn = 0;
    for (let i = 0; i < params.fishCount; i++) {
      const g = params.fishes[i];
      if (g.x >= 0 && g.x < 64 && g.y >= 0 && g.y < 128) {
        gx += g.x;
        gy += g.y;
        gn++;
      }
    }
    if (gn > 0) {
      gx /= gn;
      gy /= gn;
    } else {
      gx = cx;
      gy = cy;
    }
    const u = 0.37 + n * 0.6180339887;
    const v = 0.71 + n * 0.7548776662;
    let qx = gx + ((u - Math.floor(u)) - 0.5) * 24.0;
    let qy = gy + ((v - Math.floor(v)) - 0.5) * 32.0;
    let best = -1;
    let bestD = 1.0e9;
    for (let i = 0; i < params.fishCount; i++) {
      const g = params.fishes[i];
      if (g.x >= 0 && g.x < 64 && g.y >= 0 && g.y < 128) {
        const d2 = (g.x - qx) * (g.x - qx) + (g.y - qy) * (g.y - qy);
        if (d2 < bestD) {
          bestD = d2;
          best = i;
        }
      }
    }
    if (best >= 0) {
      const g = params.fishes[best];
      qx = g.x + Math.cos(g.angle) * 6.0;
      qy = g.y + Math.sin(g.angle) * 6.0;
    }
    params.startleX = qx;
    params.startleY = qy;
    // 물고기마다 비켜날 방향과 세기를 누른 순간에 정한다 (위치를 따라 매 프레임 다시 재면, 세기가 옅어지는
    // 경계에서 물살과 맞서 떼가 한데 뭉쳐 맴돈다). 세기: 지점에서 12px 안은 1, 28px에서 0으로 부드럽게
    // (smoothstep) - 먼 물고기는 그대로 헤엄친다. 화면 밖 물고기는 0 (재진입 시점 그대로)
    for (let i = 0; i < params.fishes.length; i++) {
      const g = params.fishes[i];
      if (i < params.fishCount && g.x >= 0 && g.x < 64 && g.y >= 0 && g.y < 128) {
        const ax = g.x - qx;
        const ay = g.y - qy;
        const aLen = Math.sqrt(ax * ax + ay * ay + 1.0);
        let ux = ax / aLen;
        let uy = ay / aLen;
        // 달아나는 방향으로 20px 안에 벽이 있으면: 벽을 파고드는 성분을 빼고 벽을 따라 가되 안쪽으로 비스듬히
        // (벽을 거의 정면으로 향하면 지금 헤엄치던 쪽으로 벽을 따라). 벽에 부딪혀 멈추지 않고 옆으로 빠져나간다
        let hit = 1000.0;
        let wnx = 0.0; // 부딪힐 벽의 바깥 방향
        let wny = 0.0;
        if (ux < -0.02) {
          hit = g.x / -ux;
          wnx = -1.0;
        } else if (ux > 0.02) {
          hit = (63.0 - g.x) / ux;
          wnx = 1.0;
        }
        if (uy < -0.02 && g.y / -uy < hit) {
          hit = g.y / -uy;
          wnx = 0.0;
          wny = -1.0;
        } else if (uy > 0.02 && (127.0 - g.y) / uy < hit) {
          hit = (127.0 - g.y) / uy;
          wnx = 0.0;
          wny = 1.0;
        }
        if (hit < 20.0) {
          const un = ux * wnx + uy * wny;
          let tx = ux - un * wnx;
          let ty = uy - un * wny;
          let tl = Math.sqrt(tx * tx + ty * ty);
          if (tl < 0.3) {
            const along = -Math.cos(g.angle) * wny + Math.sin(g.angle) * wnx;
            tx = along >= 0.0 ? -wny : wny;
            ty = along >= 0.0 ? wnx : -wnx;
            tl = 1.0;
          }
          ux = tx / tl - 0.35 * wnx;
          uy = ty / tl - 0.35 * wny;
        }
        g.fleeA = Math.atan2(uy, ux);
        // 비켜날 방향은 지금 머리 방향에서 90도 안으로 접는다 (뒤쪽이면 같은 편 옆으로). 거의 정반대면 화면 가운데
        // 쪽 옆으로 → 목표가 왼쪽·오른쪽 사이를 오가며 지그재그로 떨지 않고, 뒤돌지 않고 옆으로 미끄러진다
        const rel = Math.atan2(Math.sin(g.fleeA - g.angle), Math.cos(g.fleeA - g.angle));
        if (Math.abs(rel) > 0.5 * Math.PI) {
          let side = rel >= 0.0 ? 1.0 : -1.0;
          if (Math.abs(Math.sin(rel)) < 0.25) {
            side = Math.cos(g.angle) * (cy - g.y) - Math.sin(g.angle) * (cx - g.x) >= 0.0 ? 1.0 : -1.0;
          }
          g.fleeA = g.angle + side * 0.5 * Math.PI;
        }
        const kk = Math.max(0.0, Math.min(1.0, (28.0 - aLen) / 16.0));
        g.fleeK = kk * kk * (3.0 - 2.0 * kk);
      } else {
        g.fleeK = 0.0;
      }
      // 연달아 누를 때 앞선 놀람에 반응하던 물고기는 이번에 멀어도 봉투가 끝날 때까지 천천히 돈다
      // (제한이 갑자기 풀려 목표 쪽으로 휙 꺾이지 않게)
      g.fleeR = Math.max(g.fleeK, params.startleE > 0.0 ? g.fleeR : 0.0);
    }
    params.startleN = (n + 1) % 64;
    params.startleS = 1.0;
    params.calmS = 1.0;
  }
  const sS = params.startleS;
  // 봉투 e: startleS^2(끝은 기울기 0으로 부드럽게 0에 닿음)를 약 0.15초 늦게 따라간다 → 누르면 0에서 스르르 올라
  // 누른 뒤 약 0.2초에 최고(약 0.7), 1.3초 무렵 거의 0. 다시 누르면 지금 높이에서 이어 오른다 (툭 끊기지 않게).
  // 거의 0이 되면 정확히 0으로 내려 놓는다 → 누르지 않은 평소 모습은 그대로
  let e = params.startleE + (sS * sS - params.startleE) * Math.min(1.0, dtc * 7.0);
  if (sS === 0.0 && e < 0.002) e = 0.0;
  params.startleE = e;
  params.startleS = Math.max(0.0, sS - dtc / 1.3);
  const calm = params.calmS;
  params.calmS = Math.max(0.0, calm - dtc / 6.0);
  // 빨라지는 몫: 최대 0.5 (1.5배). SwimSpeed가 15를 넘으면 그만큼 줄인다 → 빠른 노브에서도 더 멀리 튀어 나가지 않게
  const boostMax = 0.5 * Math.min(1.0, 15.0 / speedKnob);

  for (let i = 0; i < params.fishCount; i++) {
    const f = params.fishes[i];
    let targetAngle = Math.PI;

    if (k > 0.01) {
      // 타원형 해류 궤도 추종
      const nx = (f.x - cx) / f.orbitRx;
      const ny = (f.y - cy) / f.orbitRy;
      const dist = Math.hypot(nx, ny);
      const phi = Math.atan2(ny, nx);

      const tx = -Math.sin(phi) * f.orbitRx;
      const ty = Math.cos(phi) * f.orbitRy;

      const trackErr = 1.0 - dist;
      const attractX = nx * f.orbitRx * trackErr * 1.35;
      const attractY = ny * f.orbitRy * trackErr * 1.35;

      const loopAngle = Math.atan2(ty + attractY, tx + attractX);

      const cosB = Math.cos(Math.PI) * (1.0 - k) + Math.cos(loopAngle) * k;
      const sinB = Math.sin(Math.PI) * (1.0 - k) + Math.sin(loopAngle) * k;
      targetAngle = Math.atan2(sinB, cosB);
    } else {
      targetAngle += Math.sin(t * 1.2 + f.phase) * 0.05;
    }

    // 놀란 뒤: 가로 순항 구간(k < 0.15)에서는 제 수심(baseY)에서 6px 넘게 벗어난 만큼만 되돌아간다
    // (calm이 0이면 이 항은 0 - 누르지 않은 평소 모습은 그대로)
    if (calm > 0.0 && k < 0.15) {
      const dev = f.y - f.baseY;
      const excess = dev > 6.0 ? dev - 6.0 : (dev < -6.0 ? dev + 6.0 : 0.0);
      targetAngle += Math.max(-0.7, Math.min(0.7, excess * 0.04)) * calm;
    }

    // 놀람: 누른 순간 정한 방향(fleeA) 쪽으로 목표 방향을 돌린다. 각도 차이(±π로 감싼 차이)를 ±1.6 rad(약 90도)로
    // 잘라 섞는다 → 지점이 코앞이어도 뒤로 휙 돌지 않고 옆으로 비켜 미끄러지고, 봉투가 줄면 원래 물살로 돌아온다
    const onScreen = f.x >= 0.0 && f.x < 64.0 && f.y >= 0.0 && f.y < 128.0;
    const curA = targetAngle; // 놀람을 섞기 전 물살 방향
    let eF = 0.0; // 이 물고기가 받는 봉투 (놀라지 않는 물고기는 0)
    const startled = e > 0.0 && f.fleeR > 0.0; // 이번 놀람에 반응한 물고기 (천천히 돌기·벽 안쪽 밀기)
    let boost = 0.0; // 이 물고기의 빨라지는 몫
    if (e > 0.0 && f.fleeK > 0.0) {
      if (onScreen) {
        // 달아나는 방향으로 벽까지 남은 거리: 24px부터 줄어 6px에서 0. 한 번 줄면 다시 늘지 않는다
        // → 벽 앞에서 달아나기를 풀고 원래 물살로 돌아선다 (벽에 한 줄로 늘어서거나 벽 앞에서 맴돌지 않게)
        const hit = rayToWall(f.x, f.y, Math.cos(f.fleeA), Math.sin(f.fleeA));
        f.fleeK = Math.min(f.fleeK, Math.max(0.0, Math.min(1.0, (hit - 6.0) / 18.0)));
      }
      eF = e;
      let dF = Math.atan2(Math.sin(f.fleeA - targetAngle), Math.cos(f.fleeA - targetAngle));
      dF = Math.max(-1.6, Math.min(1.6, dF));
      targetAngle += e * f.fleeK * dF;
      boost = boostMax * e * f.fleeK;
    }

    // 놀람 중 물살 대회전(k >= 0.15)에서는 반응한 물고기를 가장자리 10px 안에서 안쪽으로 민다.
    // 단 물살이 그 벽 밖으로 흘러 나가는 곳(왼쪽 출구 등)에서는 밀지 않는다 - 물살과 맞서 벽에 붙지 않게
    if (startled && k >= 0.15 && onScreen) {
      const nx = f.x < 10.0 ? (10.0 - f.x) / 10.0 : (f.x > 53.0 ? (53.0 - f.x) / 10.0 : 0.0);
      const ny = f.y < 10.0 ? (10.0 - f.y) / 10.0 : (f.y > 117.0 ? (117.0 - f.y) / 10.0 : 0.0);
      if (nx !== 0.0 || ny !== 0.0) {
        const nl = Math.sqrt(nx * nx + ny * ny);
        const inward = (Math.cos(curA) * nx + Math.sin(curA) * ny) / nl; // 물살이 안쪽으로 향하는 정도 -1..1
        const w = e * Math.min(1.0, nl) * Math.max(0.0, Math.min(1.0, 1.0 + 2.5 * inward));
        const inA = Math.atan2(ny, nx);
        targetAngle += w * Math.atan2(Math.sin(inA - targetAngle), Math.cos(inA - targetAngle));
      }
    }

    let diff = Math.atan2(Math.sin(targetAngle - f.angle), Math.cos(targetAngle - f.angle));
    let turn = diff * Math.min(1.0, simDt * 3.6);
    if (startled) {
      // 놀라서 비켜날 때는 몸을 천천히 돌린다: 한 프레임 최대 simDt * (2 + 4(1 - e)) rad. 헤엄 시간(simDt) 기준이라
      // 도는 반경이 SwimSpeed와 무관하게 일정하다 (가운데 노브·봉투 최고 무렵 초당 약 7 rad, 한 프레임 약 14도). 빠른 물살 대회전에서도
      // 궤도를 따라 돌 만큼은 되어 연타해도 물고기가 화면 밖으로 새지 않는다. e가 0으로 가면 제한이 거의 풀려 평소대로
      const maxTurn = simDt * (2.0 + 4.0 * (1.0 - e));
      turn = Math.max(-maxTurn, Math.min(maxTurn, turn));
    }
    f.angle += turn;
    // f.angle은 cos/sin/atan2(sin, cos)로만 쓰이므로 2π 단위 wrap은 정확하다.
    // C++ 번역 시: 이 wrap(±π, 2π)을 그대로 유지 - 며칠 켜 두어도 각도가 무한히 커지지 않게
    if (f.angle > Math.PI) f.angle -= 2.0 * Math.PI;
    else if (f.angle < -Math.PI) f.angle += 2.0 * Math.PI;

    const hx = Math.cos(f.angle);
    const hy = Math.sin(f.angle);
    if (boost > 0.0 && onScreen) {
      // 머리가 향한 쪽 벽까지 14px 안이면 빨라지는 몫을 줄여 2px에서 0 → 빠른 물고기가 벽으로 튀어 박히지 않게
      boost *= Math.max(0.0, Math.min(1.0, (rayToWall(f.x, f.y, hx, hy) - 2.0) / 12.0));
    }
    const currentSpeed = f.speedBase * (1.0 + k * 0.18) * (1.0 + boost);
    f.x += hx * currentSpeed * simDt;
    f.y += hy * currentSpeed * simDt;

    // 경계 처리: 화면 밖(-24)으로 완전히 벗어난 뒤 반대편 밖(88)에서 자연 재진입
    if (k < 0.15) {
      if (f.x < -24) {
        f.x = 88;
        f.y = f.baseY + Math.sin(t + f.id) * 3.5;
        f.angle = Math.PI;
      }
    } else {
      if (f.x < -28) f.x = 90;
      if (f.x > 90) f.x = -28;
      if (f.y < -28) f.y = 154;
      if (f.y > 154) f.y = -28;
    }

    f.wag += simDt * (2.8 + currentSpeed * 0.08);
    // f.wag은 sin(f.wag - ...)로만 쓰인다. C++ 번역 시: 이 주기(2π)를 그대로 유지 - 다른 값으로 wrap하면 꼬리가 튄다
    if (f.wag >= 2.0 * Math.PI) f.wag -= 2.0 * Math.PI;
  }
}

export function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const t = params.time;
  const count = params.fishCount;
  const k = params.loop;

  const cx = 32;
  const cy = 64;
  const rx = 20.0;
  const ry = 46.0;
  const swirlW = params.swirlW;

  // 프레임마다 한 번만 계산하는 시간 항
  const tA = t * 1.2;
  const tB = t * 1.4;
  const tG = t * 0.8;
  const tH = t * 2.2;
  const tF = t * 1.3;
  const tK = t * 0.6;
  const tS = t * 1.1;

  // -----------------------------------------------------------------
  // 1. 유화 배경: 디테일한 다층 도메인 왜곡 + 명확한 수심 분리
  //    (상단 얕은 수면: 0.16 ~ 0.32  vs  하단 깊은 심해: 0.00 ~ 0.04)
  // -----------------------------------------------------------------
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let x0 = x;
      let y0 = y;

      // Knob 2 활성화 시: 바닥의 짙은 심해와 가로 붓결이 해류를 따라 통째로 말려 회전
      if (k > 0.01) {
        const dx = x - cx;
        const dy = (y - cy) * (rx / ry);
        const rot = k * swirlW[y * w + x];

        const cosR = Math.cos(-rot);
        const sinR = Math.sin(-rot);

        const rx0 = dx * cosR - dy * sinR;
        const ry0 = dx * sinR + dy * cosR;

        x0 = cx + rx0;
        y0 = cy + ry0 * (ry / rx);
      }

      // [다층 미세 유체 왜곡 (Layered Domain Turbulence)]:
      // 과하지 않게 가로 붓터치를 울퉁불퉁 굴곡시키는 2중 워프 벡터
      const warpA = Math.sin(y0 * 0.16 + tA + Math.cos(x0 * 0.12) * 1.6) * 2.2;
      const warpB = Math.cos(x0 * 0.22 - y0 * 0.07 - tB) * 1.5;
      const microGrain = Math.sin(x0 * 0.45 + y0 * 0.3 - tG) * 0.7;

      const wx = x0 + warpA + microGrain;
      const wy = y0 + warpB + warpA * 0.4;

      // [가로 유화 결 텍스처 (Horizontal Impasto Striations)]
      const streakRow = Math.floor(wy / 2.8);
      const horizFlow = Math.sin(wx * 0.14 - tH + streakRow * 1.9) * 0.035;
      const horizFine = Math.cos(wx * 0.28 - tF + wy * 0.08) * 0.02;
      // 나이프로 눌러 바른 듯한 미세 능선
      const kc = Math.abs(Math.sin(wx * 0.08 + wy * 0.15 - tK));
      const knifeChisel = kc * kc * kc * 0.025;

      // [수심 경계면]: y ≈ 48 부근에서 울퉁불퉁하지만 명확하게 나뉘는 층위
      const shelfY = 48.0 + Math.sin(wx * 0.11 - tS) * 3.6 + Math.cos(wx * 0.26) * 1.6;
      const distFromShelf = wy - shelfY;
      // 단차를 시각적으로 또렷하게 가르는 시그모이드 전이
      const depthMask = 1.0 / (1.0 + Math.exp(-distFromShelf * 0.85));

      // 상단 얕은 수면 (0.24 중심, 램프의 채도 높은 밝은 파랑 #2f9be6).
      // 전시 수정: 예전 0.40(연한 하늘색)은 노랑·연두 물고기가 묻혔고, LED에서는 어두운 파랑끼리 구분이 안 돼
      // 램프 아래쪽(0~0.421)을 새로 잡고(남색 -> 밝은 파랑) 수면을 0.24로 낮춤. 물고기 색(0.64~1.0)은 그대로
      const shallowTone = 0.24 + horizFlow + horizFine + knifeChisel;
      // 하단 깊은 심해 (0.01 중심, 램프 맨 아래 거의 검은 남색 #00103f)
      const abyssTone = 0.01 + horizFlow * 0.5 + horizFine * 0.4;

      let bgVal = shallowTone * (1.0 - depthMask) + abyssTone * depthMask;
      display.setValue(x, y, Math.max(0.0, Math.min(0.32, bgVal)));
    }
  }

  // -----------------------------------------------------------------
  // 2. 물고기: 4가지 유기적 체형 렌더링 (0.64 ~ 1.00 strictly)
  // -----------------------------------------------------------------
  for (let i = 0; i < count; i++) {
    const f = params.fishes[i];
    const fx = f.x;
    const fy = f.y;
    const fa = f.angle;
    const fTone = f.tone;

    const dirX = Math.cos(fa);
    const dirY = Math.sin(fa);
    const normX = -dirY;
    const normY = dirX;

    const steps = Math.ceil(f.len * 1.5);
    let eyeDrawn = false;

    for (let s = 0; s <= steps; s++) {
      const q = s / steps;
      const distBack = q * f.len;

      // 자연스러운 글라이딩 꼬리 파동
      const wag = Math.sin(f.wag - q * 2.5) * Math.pow(q, 1.8) * 0.55;

      const pxCenter = fx - dirX * distBack + normX * wag;
      const pyCenter = fy - dirY * distBack + normY * wag;

      let hUp = 0.0;
      let hDown = 0.0;
      let isTail = false;
      let isFork = false;

      if (f.type === 0) {
        // [Type 0: 대형 마름모]
        if (q < 0.72) {
          const prof = Math.sin((q / 0.72) * Math.PI);
          hUp = prof * (f.height * 0.58);
          hDown = prof * (f.height * 0.42);
        } else {
          const tq = (q - 0.72) / 0.28;
          hUp = (0.18 + tq * 0.26) * f.height;
          hDown = (0.18 + tq * 0.26) * f.height;
          isTail = true;
        }
      } else if (f.type === 1) {
        // [Type 1: 유선형 V자 갈래 꼬리]
        if (q < 0.70) {
          const prof = Math.sin((q / 0.70) * Math.PI);
          hUp = prof * (f.height * 0.48);
          hDown = prof * (f.height * 0.52);
        } else {
          const tq = (q - 0.70) / 0.30;
          hUp = (0.16 + tq * 0.42) * f.height;
          hDown = (0.16 + tq * 0.42) * f.height;
          isTail = true;
          isFork = tq > 0.42;
        }
      } else if (f.type === 2) {
        // [Type 2: 통통한 송사리]
        if (q < 0.76) {
          const prof = Math.sin((q / 0.76) * Math.PI);
          hUp = prof * (f.height * 0.38);
          hDown = prof * (f.height * 0.62);
        } else {
          const tq = (q - 0.76) / 0.24;
          const leaf = Math.sin(tq * Math.PI) * 0.32 + 0.14;
          hUp = leaf * f.height;
          hDown = leaf * f.height;
          isTail = true;
        }
      } else {
        // [Type 3: 슬림 피라미]
        if (q < 0.80) {
          const prof = Math.sin((q / 0.80) * Math.PI * 0.95);
          hUp = prof * (f.height * 0.48);
          hDown = prof * (f.height * 0.52);
        } else {
          const tq = (q - 0.80) / 0.20;
          const taper = Math.max(0.12, (1.0 - tq * 0.85)) * 0.35;
          hUp = taper * f.height;
          hDown = taper * f.height;
          isTail = true;
        }
      }

      const topY = Math.ceil(hUp);
      const botY = Math.ceil(hDown);

      for (let d = -botY; d <= topY; d++) {
        if (isFork && Math.abs(d) < topY * 0.42) continue;

        const px = Math.floor(pxCenter + normX * d);
        const py = Math.floor(pyCenter + normY * d);

        if (px >= 0 && px < w && py >= 0 && py < h) {
          let val = isTail ? fTone * 0.90 : fTone;
          if (d < -botY * 0.65) val -= 0.04;

          display.setValue(px, py, Math.max(0.64, Math.min(1.0, val)));
        }
      }

      // 측면 등 위쪽 단 1개의 눈동자
      if (!eyeDrawn && q >= 0.16 && q <= 0.22) {
        const eyeOffset = hUp * 0.55;
        const ex = Math.floor(pxCenter + normX * eyeOffset);
        const ey = Math.floor(pyCenter + normY * eyeOffset);

        if (ex >= 0 && ex < w && ey >= 0 && ey < h) {
          display.setValue(ex, ey, 1.0);
          eyeDrawn = true;
        }
      }
    }
  }
}
