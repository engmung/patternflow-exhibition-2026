// ===== Patternflow pattern =====
// Title:    YUJIN
// Author:   @engmung
// Date:     2026-09-17
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

// @ramp oklab 0:#003d9e 0.421:#b8feff 0.509:#ff0000 0.664:#99ff00 0.984:#ffdd00 1:#000000 0.839:#f000d0 0.47:#fbff00
// @knobs FishCount=4..24, SwimSpeed=7..20, CurrentLoop=0..1, Unused=0..1
// @matrix 64x128


// Knob 1 (FishCount): 화면에 등장하는 물고기 수
// Knob 2 (SwimSpeed): 전체 유영 및 해류 순항 속도 (7..20)
// Knob 3 (CurrentLoop): 0이면 바닥 평온 순항, 올리면 가로 유화 결 전체가 물살을 타고 화면 전체 대회전
// Knob 4 (Unused): 미사용

export function setup(params) {
  params.time = 0;
  params.fishes = [];

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
      orbitRy: 44 + ((i * 5.7) % 8)
    });
  }
}

export function update(dt, input, params) {
  const speedKnob = input ? input.knobValues[1] : 12.0;
  const countKnob = input ? input.knobValues[0] : 16;
  const loopKnob = input ? input.knobValues[2] : 0.0;

  const simDt = Math.min(dt, 0.05) * (speedKnob * 0.15);
  params.time += simDt;
  params.fishCount = Math.min(24, Math.max(4, Math.round(countKnob)));
  params.loop = Math.max(0.0, Math.min(1.0, loopKnob));

  const k = params.loop;
  const t = params.time;
  const cx = 32;
  const cy = 64;

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

    const diff = Math.atan2(Math.sin(targetAngle - f.angle), Math.cos(targetAngle - f.angle));
    f.angle += diff * Math.min(1.0, simDt * 3.6);

    const currentSpeed = f.speedBase * (1.0 + k * 0.18);
    f.x += Math.cos(f.angle) * currentSpeed * simDt;
    f.y += Math.sin(f.angle) * currentSpeed * simDt;

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

  // -----------------------------------------------------------------
  // 1. 유화 배경: 디테일한 다층 도메인 왜곡 + 명확한 수심 분리
  //    (상단 얕은 수면: 0.38 ~ 0.44  vs  하단 깊은 심해: 0.07 ~ 0.12)
  // -----------------------------------------------------------------
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let x0 = x;
      let y0 = y;

      // Knob 3 활성화 시: 바닥의 짙은 심해와 가로 붓결이 해류를 따라 통째로 말려 회전
      if (k > 0.01) {
        const dx = x - cx;
        const dy = (y - cy) * (rx / ry);
        const r = Math.hypot(dx, dy);

        const bandWeight = Math.exp(-Math.pow((r - rx) / 12.0, 2.0));
        const rot = k * (1.75 * bandWeight + 0.3 * (1.0 - Math.min(1.0, r / 42.0)));

        const cosR = Math.cos(-rot);
        const sinR = Math.sin(-rot);

        const rx0 = dx * cosR - dy * sinR;
        const ry0 = dx * sinR + dy * cosR;

        x0 = cx + rx0;
        y0 = cy + ry0 * (ry / rx);
      }

      // [다층 미세 유체 왜곡 (Layered Domain Turbulence)]:
      // 과하지 않게 가로 붓터치를 울퉁불퉁 굴곡시키는 2중 워프 벡터
      const warpA = Math.sin(y0 * 0.16 + t * 1.2 + Math.cos(x0 * 0.12) * 1.6) * 2.2;
      const warpB = Math.cos(x0 * 0.22 - y0 * 0.07 - t * 1.4) * 1.5;
      const microGrain = Math.sin(x0 * 0.45 + y0 * 0.3 - t * 0.8) * 0.7;

      const wx = x0 + warpA + microGrain;
      const wy = y0 + warpB + warpA * 0.4;

      // [가로 유화 결 텍스처 (Horizontal Impasto Striations)]
      const streakRow = Math.floor(wy / 2.8);
      const horizFlow = Math.sin(wx * 0.14 - t * 2.2 + streakRow * 1.9) * 0.035;
      const horizFine = Math.cos(wx * 0.28 - t * 1.3 + wy * 0.08) * 0.02;
      // 나이프로 눌러 바른 듯한 미세 능선
      const knifeChisel = Math.pow(Math.abs(Math.sin(wx * 0.08 + wy * 0.15 - t * 0.6)), 3.0) * 0.025;

      // [수심 경계면]: y ≈ 48 부근에서 울퉁불퉁하지만 명확하게 나뉘는 층위
      const shelfY = 48.0 + Math.sin(wx * 0.11 - t * 1.1) * 3.6 + Math.cos(wx * 0.26) * 1.6;
      const distFromShelf = wy - shelfY;
      // 단차를 시각적으로 또렷하게 가르는 시그모이드 전이
      const depthMask = 1.0 / (1.0 + Math.exp(-distFromShelf * 0.85));

      // 상단 얕은 수면 (0.38 ~ 0.44)
      const shallowTone = 0.40 + horizFlow + horizFine + knifeChisel;
      // 하단 깊은 심해 (0.07 ~ 0.12)
      const abyssTone = 0.09 + horizFlow * 0.5 + horizFine * 0.4;

      let bgVal = shallowTone * (1.0 - depthMask) + abyssTone * depthMask;
      display.setValue(x, y, Math.max(0.06, Math.min(0.44, bgVal)));
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

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
