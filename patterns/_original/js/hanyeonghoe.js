// ===== Patternflow pattern =====
// Title:    HANYEONGHOE
// Author:   @engmung
// Date:     2026-09-16
// SPDX-License-Identifier: CC-BY-SA-4.0
// ===============================

// @matrix 64x128
// @knobs BloomSize=0.5..6, WindSpeed=0.2..5, StemSway=-2..2, Unused=0..1
// Patternflow layer-stack composition — flattened by Pattern Lab.
// Layers (bottom → top):
//   1. Pixel 1 (pixel, normal, 100%)
//   2. Cloud (code, normal, 100%)
//   3. flower (code, normal, 100%)
const __W = 64, __H = 128;
const __stack = [];

function __b64(s, n) {
  const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const out = new Uint8ClampedArray(n);
  let o = 0, buf = 0, bits = 0;
  for (let i = 0; i < s.length && o < n; i++) {
    const v = A.indexOf(s[i]);
    if (v < 0) continue;
    buf = (buf << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out[o++] = (buf >> bits) & 255; }
  }
  return out;
}

// Run-length coded RGBA: base64 of {count-1, r, g, b, a} tuples.
function __rle(s, n) {
  const t = __b64(s, (s.length * 3) >> 2), out = new Uint8ClampedArray(n);
  let o = 0;
  for (let i = 0; i + 4 < t.length && o < n; i += 5) {
    for (let k = t[i] + 1; k > 0 && o < n; k--) {
      out[o++] = t[i + 1]; out[o++] = t[i + 2]; out[o++] = t[i + 3]; out[o++] = t[i + 4];
    }
  }
  return out;
}

function __mkDisplay(L) {
  return {
    width: __W, height: __H,
    setPixel(x, y, r, g, b) {
      const xi = x | 0, yi = y | 0;
      if (xi < 0 || xi >= __W || yi < 0 || yi >= __H) return;
      const i = (yi * __W + xi) * 4, B = L.buf;
      if (Array.isArray(r)) { B[i] = r[0] || 0; B[i + 1] = r[1] || 0; B[i + 2] = r[2] || 0; }
      else { B[i] = r; B[i + 1] = g || 0; B[i + 2] = b || 0; }
      B[i + 3] = 255;
    },
    setValue(x, y, v) {
      const xi = x | 0, yi = y | 0;
      if (xi < 0 || xi >= __W || yi < 0 || yi >= __H) return;
      v = Number.isFinite(v) ? (v < 0 ? 0 : v > 1 ? 1 : v) : 0;
      const i = (yi * __W + xi) * 4, B = L.buf, T = L.lut;
      if (T) {
        const li = Math.round(v * 255) * 4;
        B[i] = T[li]; B[i + 1] = T[li + 1]; B[i + 2] = T[li + 2]; B[i + 3] = T[li + 3];
      } else {
        const byte = Math.round(v * 255);
        B[i] = byte; B[i + 1] = byte; B[i + 2] = byte; B[i + 3] = 255;
      }
    },
  };
}

// ── pixel layer: Pixel 1 ──
__stack.push({
  kind: "pixel", opacity: 1, blend: "normal", role: "paint", maskInvert: false,
  buf: __rle("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BRSoAP8AEY8A/wQJTQD/AgQkAP8VChwH/xMGMwD/BAlNAP8ADXAA/wUUqAD/ABGPAP8ECU0A/wIEJAD/DwocB/8ABCQA/wUKHAf/AQQkAP8BBjMA/wAEJAD/AwYzAP8ACU0A/wsGMwD/AwlNAP8FFKgA/wQJTQD/AQQkAP8VChwH/wYEJAD/AQYzAP8ABCQA/wIGMwD/AglNAP8MBjMA/wIUqAD/AA1wAP8FCU0A/wAEJAD/AgocB/8ABCQA/xMKHAf/BAQkAP8BBjMA/wEEJAD/AQYzAP8ABCQA/wIGMwD/AAQkAP8CCU0A/wkGMwD/CQlNAP8AChwH/wAEJAD/AAocB/8ABCQA/xQKHAf/AwQkAP8HBjMA/wEJTQD/AAQkAP8ACU0A/wIEJAD/AQlNAP8HBjMA/wgJTQD/GAocB/8ABCQA/wAGMwD/AQQkAP8GBjMA/wAEJAD/AgYzAP8CCU0A/wMEJAD/AglNAP8DBjMA/wkJTQD/AAocB/8BBCQA/xcKHAf/AAQkAP8JBjMA/wAEJAD/AgYzAP8ABCQA/wIJTQD/AgQkAP8ABjMA/wEJTQD/AQYzAP8ICU0A/wIEJAD/EwocB/8BBCQA/wIKHAf/AgQkAP8ABjMA/wAEJAD/CwYzAP8ABCQA/xEJTQD/AQQkAP8UChwH/wgEJAD/AAYzAP8ABCQA/wYGMwD/AAlNAP8JBjMA/wkJTQD/AwQkAP8VChwH/woEJAD/AgYzAP8BCU0A/wIGMwD/AAlNAP8HBjMA/wgJTQD/AgQkAP8MChwH/wEEJAD/CQocB/8JBCQA/wMGMwD/AglNAP8EBjMA/wIJTQD/AgYzAP8GCU0A/wMEJAD/CwocB/8CBCQA/wUKHAf/AAQkAP8BChwH/wcEJAD/AgYzAP8ACU0A/wUGMwD/AQlNAP8DBjMA/wUJTQD/AQYzAP8BCU0A/wQEJAD/DAocB/8BBCQA/wYKHAf/AAQkAP8BChwH/wMEJAD/AAYzAP8DBCQA/wAGMwD/AAlNAP8BBjMA/wAJTQD/BgYzAP8ACU0A/wQGMwD/AAlNAP8DBjMA/wEJTQD/AwQkAP8IChwH/wYEJAD/BAocB/8BBCQA/wEGMwD/AAocB/8IBCQA/wEGMwD/AAlNAP8FBjMA/wAJTQD/BAYzAP8CCU0A/wEGMwD/BAlNAP8CBCQA/wgKHAf/BgQkAP8FChwH/wMEJAD/AAYzAP8JBCQA/wgGMwD/AglNAP8DBjMA/wIJTQD/AQYzAP8CCU0A/wEEJAD/CQocB/8EBCQA/wUKHAf/AAQkAP8ABjMA/wQEJAD/AgYzAP8BBCQA/wAGMwD/AwQkAP8JBjMA/wQJTQD/BgYzAP8BCU0A/wAEJAD/BAocB/8ABCQA/wQKHAf/AwQkAP8AChwH/wcEJAD/AQYzAP8DBCQA/xAGMwD/AQlNAP8BBjMA/wcJTQD/AQYzAP8ACU0A/wUKHAf/CAQkAP8AChwH/wQGMwD/AwQkAP8EBjMA/wEEJAD/BQYzAP8ACU0A/wsGMwD/AQlNAP8CBjMA/wQJTQD/AQYzAP8ACU0A/wAEJAD/AwocB/8IBCQA/wAKHAf/AQQkAP8LBjMA/wIEJAD/BgYzAP8BCU0A/w0GMwD/AAlNAP8ABjMA/wIJTQD/AgYzAP8HChwH/wgEJAD/DAYzAP8JBCQA/wEGMwD/AAlNAP8LBjMA/wAJTQD/AgYzAP8DCU0A/wAGMwD/AAlNAP8ABCQA/wMKHAf/AAQkAP8AChwH/wgEJAD/DgYzAP8HBCQA/wIAJB7/AQYzAP8BCU0A/wkGMwD/AAlNAP8GBjMA/wIKHAf/AAQkAP8BChwH/wIEJAD/AAocB/8EBCQA/w4GMwD/AQQkAP8CBjMA/wIAJB7/AAQkAP8GACQe/wEJTQD/DwYzAP8BChwH/wAEJAD/AAocB/8CBCQA/wIKHAf/AgYzAP8ABCQA/wIGMwD/AQocB/8KBjMA/wMEJAD/AQYzAP8BBCQA/wMAJB7/AQQkAP8FACQe/wAEJAD/DQYzAP8BBCQA/wAKHAf/AAQkAP8DChwH/wYGMwD/AgocB/8FBjMA/wEEJAD/AAYzAP8HBCQA/wIGMwD/AAQkAP8BACQe/wQEJAD/BAAkHv8CBCQA/wcGMwD/AgQkAP8DChwH/wYGMwD/AQocB/8DBCQA/wIGMwD/BAQkAP8ABjMA/wsEJAD/AQYzAP8CACQe/wQEJAD/AwAkHv8FBCQA/wIGMwD/AAQkAP8EChwH/wMGMwD/CQQkAP8ABjMA/wUEJAD/BgYzAP8FBCQA/wIAJB7/AgQkAP8BACQe/wUEJAD/AAYzAP8JBCQA/wIKHAf/AQYzAP8KBCQA/wEGMwD/BQQkAP8LBjMA/wEAJB7/AwQkAP8BACQe/wIEJAD/AgAkHv8EBCQA/wMGMwD/BAQkAP8BChwH/wEGMwD/BQQkAP8AChwH/wQGMwD/BQQkAP8FBjMA/wkEJAD/AwAkHv8BBCQA/wEAJB7/AwQkAP8BACQe/wQEJAD/BQYzAP8ABCQA/wAKHAf/AAYzAP8DBCQA/wIKHAf/AwYzAP8GBCQA/wMGMwD/EQQkAP8FACQe/wMEJAD/AQAkHv8EBCQA/wYGMwD/AgQkAP8CChwH/wEGMwD/BwQkAP8EBjMA/wUEJAD/BAAkHv8MBCQA/wAGMwD/BAAkHv8FBCQA/wkGMwD/AQocB/8IBCQA/wAGMwD/CAAkHv8ABCQA/wYAJB7/AwQkAP8CACQe/wcEJAD/AAYzAP8EACQe/wQEJAD/BAYzAP8JBCQA/wQGMwD/AAQkAP8aACQe/wcEJAD/AQYzAP8DACQe/w0EJAD/BAYzAP8DBCQA/xwAJB7/BwQkAP8CBjMA/wAEJAD/BAAkHv8CBCQA/wcGMwD/AgQkAP8jACQe/wcEJAD/BAYzAP8ABCQA/wgGMwD/AgQkAP8pACQe/wUEJAD/CgYzAP8ABCQA/y8AJB7/BQQkAP8EBjMA/zYAJB7/BQQkAP8qACQe/wAEJAD/BAAkHv8BBCQA/wkAJB7/AgQkAP8yACQe/wEEJAD/CQAkHv8ABCQA/z8AJB7/AAQkAP8+ACQe/wAEJAD/AQAkHv8BBCQA/yYAJB7/AgQkAP8NACQe/wMEJAD/PQAkHv8ABCQA/7sAJB7/BgQkAP86ACQe/wEEJAD/fwAkHv8ABCQA/zsAJB7/AgQkAP8=", __W * __H * 4),
});

// ── code layer: Cloud ──
(function () {
  // Shadow the wrapper's own exports: a layer that defines no setup/update
  // must yield undefined here, not capture the OUTER setup (infinite loop).
  var setup, update, draw;



// Knob 1 (CloudCount): 생성되는 구름 덩어리의 총개수/확률 (1개 ~ 6개)
// Knob 2 (Speed): 오른쪽으로 바람 타고 흘러가는 속도
// Knob 3 (CloudScale): 큰 구름 덩어리 전체 중심 기준 크기 (0.5배 ~ 2.5배)
// Knob 4 (Unused): 미사용

function setup(params) {
  params.wind = 0;
  params.breathe = 0;

  function hash(n) {
    const s = Math.sin(n * 127.1) * 43758.5453;
    return s - Math.floor(s);
  }

  // 전체적으로 Y 좌표를 하강시키고, 덩어리별 고유 밝기/투과 범위를 다르게 설정
  // baseTone: 해당 덩어리의 최종 밝기 한도 (0.65 ~ 0.95)
  // alphaRate: 물감 도포 농도 (0.42 ~ 0.62)
  const parentClouds = [
    { x: -0.15, y: 0.17, rx: 0.22, ry: 0.09, seed: 15.2, threshold: 1.0, baseTone: 0.95, alphaRate: 0.62 }, // 두껍고 쨍한 흰 구름
    { x: 0.15,  y: 0.24, rx: 0.28, ry: 0.11, seed: 48.7, threshold: 2.0, baseTone: 0.72, alphaRate: 0.44 }, // 옅고 부드러운 구름
    { x: 0.45,  y: 0.16, rx: 0.25, ry: 0.10, seed: 82.1, threshold: 3.0, baseTone: 0.86, alphaRate: 0.55 }, // 중간 톤 구름
    { x: 0.75,  y: 0.25, rx: 0.30, ry: 0.12, seed: 33.9, threshold: 4.0, baseTone: 0.92, alphaRate: 0.60 }, // 도톰한 큰 구름
    { x: 1.05,  y: 0.18, rx: 0.24, ry: 0.09, seed: 61.3, threshold: 5.0, baseTone: 0.68, alphaRate: 0.42 }, // 은은하게 비치는 구름
    { x: 1.35,  y: 0.23, rx: 0.27, ry: 0.11, seed: 94.8, threshold: 6.0, baseTone: 0.82, alphaRate: 0.50 }  // 자연스러운 중간 구름
  ];

  params.clouds = parentClouds.map((pc) => {
    const dabs = [];
    for (let i = 0; i < 16; i++) {
      const s = pc.seed + i * 7.31;
      const ang = hash(s) * Math.PI * 2.0;
      const rad = Math.sqrt(hash(s + 1.1));

      const coreBoost = 1.35 - rad * 0.35;
      const baseRx = (0.075 + hash(s + 2.2) * 0.035) * coreBoost;
      const baseRy = (0.022 + hash(s + 3.3) * 0.012) * coreBoost;

      dabs.push({
        ox: Math.cos(ang) * rad * pc.rx * 0.85,
        oy: Math.sin(ang) * rad * pc.ry * 0.85,
        rx: baseRx,
        ry: baseRy,
        phase: hash(s + 5.5) * Math.PI * 2.0,
        pulseFreq: 0.7 + hash(s + 6.6) * 0.6
      });
    }
    return { ...pc, dabs };
  });

  // 하강된 구름 높이에 맞춘 버퍼 크기 (최대 70px 높이)
  params.transBuffer = new Float32Array(64 * 72);
  params.toneBuffer = new Float32Array(64 * 72);
  params.countBuffer = new Uint8Array(64 * 72);
}

function update(dt, input, params) {
  const speed = input.knobValues[1];
  params.wind += dt * speed * 0.11;
  params.breathe += dt * 1.1;
  params.count = input.knobValues[0];
  params.scale = input.knobValues[2];
}

function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const count = params.count;
  const scale = params.scale;
  const jitter = 1.0 + (-1.671) * 0.3; // 0.4987
  const wind = params.wind;
  const breathe = params.breathe;
  const clouds = params.clouds;

  // 구름이 내려앉은 만큼 최대 Y 영역을 0.52(약 66px)로 확장
  const maxCloudY = Math.min(h, Math.floor(h * 0.52));
  const bufferSize = w * maxCloudY;

  const trans = params.transBuffer;
  const toneBuf = params.toneBuffer;
  const counts = params.countBuffer;

  // 버퍼 초기화
  for (let i = 0; i < bufferSize; i++) {
    trans[i] = 1.0;
    toneBuf[i] = 0.0;
    counts[i] = 0;
  }

  // 화면 밖 충분한 마진으로 래핑 순환
  const minX = -0.50;
  const maxX = 1.50;
  const span = maxX - minX;

  const sqrtScale = Math.sqrt(scale);
  const invW = 1.0 / (w - 1);
  const invH = 1.0 / (h - 1);

  for (let c = 0; c < clouds.length; c++) {
    const cloud = clouds[c];
    if (cloud.threshold > count) continue;

    const rawX = cloud.x + wind;
    let cx = ((rawX - minX) % span);
    if (cx < 0) cx += span;
    cx += minX;

    for (let d = 0; d < cloud.dabs.length; d++) {
      const dab = cloud.dabs[d];

      const p = dab.phase + breathe * dab.pulseFreq;
      const driftX = Math.cos(p) * 0.007;
      const driftY = Math.sin(p * 1.3) * 0.004;

      const dabPosX = cx + (dab.ox + driftX) * jitter * scale;
      const dabPosY = cloud.y + (dab.oy + driftY) * jitter * scale;

      const sizePulse = 1.0 + Math.sin(p * 1.4) * 0.10;
      const rX = dab.rx * sqrtScale * sizePulse;
      const rY = dab.ry * sqrtScale * sizePulse;

      const minPxX = Math.floor((dabPosX - rX) * w);
      const maxPxX = Math.ceil((dabPosX + rX) * w);
      const minPxY = Math.floor((dabPosY - rY) * h);
      const maxPxY = Math.ceil((dabPosY + rY) * h);

      if (maxPxX < 0 || minPxX >= w || maxPxY < 0 || minPxY >= maxCloudY) {
        continue;
      }

      const clampMinX = Math.max(0, minPxX);
      const clampMaxX = Math.min(w - 1, maxPxX);
      const clampMinY = Math.max(0, minPxY);
      const clampMaxY = Math.min(maxCloudY - 1, maxPxY);

      const invRxSq = 1.0 / (rX * rX);
      const invRySq = 1.0 / (rY * rY);
      const alphaRate = cloud.alphaRate;
      const baseTone = cloud.baseTone;

      for (let py = clampMinY; py <= clampMaxY; py++) {
        const ny = py * invH;
        const dy = ny - dabPosY;
        const dySqNorm = (dy * dy) * invRySq;
        if (dySqNorm >= 1.0) continue;

        const rowOffset = py * w;

        for (let px = clampMinX; px <= clampMaxX; px++) {
          const nx = px * invW;
          const dx = nx - dabPosX;
          const distNorm = (dx * dx) * invRxSq + dySqNorm;

          if (distNorm < 1.0) {
            const idx = rowOffset + px;
            const alpha = (1.0 - distNorm) * alphaRate;
            trans[idx] *= (1.0 - alpha);
            
            // 해당 픽셀에 닿은 큰 구름의 밝기 톤 누적 가중치 반영
            if (baseTone > toneBuf[idx]) {
              toneBuf[idx] = baseTone;
            }

            if (counts[idx] < 8) counts[idx]++;
          }
        }
      }
    }
  }

  // 최종 픽셀 렌더링
  for (let py = 0; py < maxCloudY; py++) {
    const ny = py * invH;
    const rowOffset = py * w;

    // 하강된 구름 고도에 맞추어 0.40 이후부터 산과 자연스럽게 오버랩 감쇄
    let falloff = 1.0;
    if (ny > 0.40) {
      falloff = Math.max(0.0, 1.0 - (ny - 0.40) * 8.33); // 1.0 / 0.12 ≈ 8.33
    }

    for (let px = 0; px < w; px++) {
      const idx = rowOffset + px;
      const cNum = counts[idx];

      if (cNum > 0) {
        const densityVal = 1.0 - trans[idx];
        const targetTone = toneBuf[idx];
        const layerSheen = cNum * 0.025;

        // 구름 덩어리별 targetTone(0.68~0.95)에 따라 각 구름의 고유 밝기 대역 형성
        let v = ((densityVal * targetTone) + layerSheen) * falloff;
        v = v > 1.0 ? 1.0 : (v < 0.0 ? 0.0 : v);
        display.setValue(px, py, v);
      } else {
        display.setValue(px, py, 0.0);
      }
    }
  }

  // 하단 0.0 클리어
  for (let py = maxCloudY; py < h; py++) {
    for (let px = 0; px < w; px++) {
      display.setValue(px, py, 0.0);
    }
  }
}
  __stack.push({
    kind: "code", opacity: 1, blend: "normal", recolor: false, role: "paint", maskInvert: false,
    lut: [0,0,0,0,18,18,18,53,63,63,63,105,114,114,114,158,169,169,169,211,218,218,218,255,213,213,213,255,208,208,208,255,203,203,203,255,199,199,199,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,194,194,194,255,195,195,195,255,195,195,195,255,195,195,195,255,196,196,196,255,196,196,196,255,196,196,196,255,197,197,197,255,197,197,197,255,198,198,198,255,198,198,198,255,198,198,198,255,199,199,199,255,199,199,199,255,199,199,199,255,200,200,200,255,200,200,200,255,200,200,200,255,201,201,201,255,201,201,201,255,201,201,201,255,202,202,202,255,202,202,202,255,203,203,203,255,203,203,203,255,203,203,203,255,204,204,204,255,204,204,204,255,204,204,204,255,205,205,205,255,205,205,205,255,205,205,205,255,206,206,206,255,206,206,206,255,207,207,207,255,207,207,207,255,207,207,207,255,208,208,208,255,208,208,208,255,208,208,208,255,209,209,209,255,209,209,209,255,209,209,209,255,210,210,210,255,210,210,210,255,211,211,211,255,211,211,211,255,211,211,211,255,212,212,212,255,212,212,212,255,212,212,212,255,213,213,213,255,213,213,213,255,213,213,213,255,214,214,214,255,214,214,214,255,215,215,215,255,215,215,215,255,215,215,215,255,216,216,216,255,216,216,216,255,216,216,216,255,217,217,217,255,217,217,217,255,218,218,218,255,218,218,218,255,218,218,218,255,219,219,219,255,219,219,219,255,219,219,219,255,220,220,220,255,220,220,220,255,220,220,220,255,221,221,221,255,221,221,221,255,222,222,222,255,222,222,222,255,222,222,222,255,223,223,223,255,223,223,223,255,223,223,223,255,224,224,224,255,224,224,224,255,224,224,224,255,224,224,224,255,224,224,224,255,224,224,224,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,225,225,225,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,226,226,226,255,227,227,227,255,227,227,227,255,227,227,227,255,227,227,227,255,227,227,227,255,227,227,227,255,227,227,227,255,228,228,228,255,228,228,228,255,229,229,229,255,229,229,229,255,230,230,230,255,230,230,230,255,230,230,230,255,231,231,231,255,231,231,231,255,232,232,232,255,232,232,232,255,233,233,233,255,233,233,233,255,233,233,233,255,234,234,234,255,234,234,234,255,235,235,235,255,235,235,235,255,236,236,236,255,236,236,236,255,236,236,236,255,237,237,237,255,237,237,237,255,238,238,238,255,238,238,238,255,239,239,239,255,239,239,239,255,239,239,239,255,240,240,240,255,240,240,240,255,241,241,241,255,241,241,241,255,242,242,242,255,242,242,242,255,242,242,242,255,243,243,243,255,243,243,243,255,244,244,244,255,244,244,244,255,245,245,245,255,245,245,245,255,245,245,245,255,246,246,246,255,246,246,246,255,247,247,247,255,247,247,247,255,248,248,248,255,248,248,248,255,248,248,248,255,249,249,249,255,249,249,249,255,250,250,250,255,250,250,250,255,251,251,251,255,251,251,251,255,252,252,252,255,252,252,252,255,252,252,252,255,253,253,253,255,253,253,253,255,254,254,254,255,254,254,254,255,255,255,255,255,255,255,255,255],
    buf: new Uint8ClampedArray(__W * __H * 4),
    params: {},
    fSetup: typeof setup === "function" ? setup : null,
    fUpdate: typeof update === "function" ? update : null,
    fDraw: typeof draw === "function" ? draw : null,
  });
})();

// ── code layer: flower ──
(function () {
  // Shadow the wrapper's own exports: a layer that defines no setup/update
  // must yield undefined here, not capture the OUTER setup (infinite loop).
  var setup, update, draw;



// Knob 1 (BloomSize): 노란 유채꽃 및 보라색 꽃머리 볼륨 연동
// Knob 2 (WindSpeed): 바람에 줄기와 꽃들이 흔들리는 속도
// Knob 3 (StemSway): 바람의 방향 및 줄기의 휨 편향
// Knob 4 (Unused): 미사용

function setup(params) {
  params.windTime = 0;
}

function update(dt, input, params) {
  params.windTime += dt * input.knobValues[1] * 2.0;

  const rawBloom = input.knobValues[0];
  const tNorm = Math.max(0.0, Math.min(1.0, (rawBloom - 0.5) / 5.5));
  params.bloom = 0.65 + tNorm * 0.35;

  params.sway = input.knobValues[2];
}

function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const bloomK = params.bloom;
  const swayK = params.sway;
  const t = params.windTime;

  // 배경 0.0 투과 초기화
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      display.setValue(x, y, 0.0);
    }
  }

  function plot(x, y, v) {
    const px = Math.floor(x);
    const py = Math.floor(y);
    if (px >= 0 && px < w && py >= 0 && py < h) {
      display.setValue(px, py, v);
    }
  }

  function drawBrushLine(x0, y0, x1, y1, radius, val) {
    const dist = Math.hypot(x1 - x0, y1 - y0);
    const steps = Math.max(1, Math.floor(dist * 2.0));
    for (let i = 0; i <= steps; i++) {
      const frac = i / steps;
      const cx = x0 + (x1 - x0) * frac;
      const cy = y0 + (y1 - y0) * frac;
      const r = Math.floor(radius);
      for (let dy = -r; dy <= r; dy++) {
        for (let dx = -r; dx <= r; dx++) {
          if (dx * dx + dy * dy <= radius * radius) {
            plot(cx + dx, cy + dy, val);
          }
        }
      }
    }
  }

  function drawDab(cx, cy, rx, ry, val) {
    const startX = Math.floor(cx - rx);
    const endX = Math.ceil(cx + rx);
    const startY = Math.floor(cy - ry);
    const endY = Math.ceil(cy + ry);

    for (let py = startY; py <= endY; py++) {
      for (let px = startX; px <= endX; px++) {
        const dx = (px - cx) / rx;
        const dy = (py - cy) / ry;
        if (dx * dx + dy * dy <= 1.0) {
          plot(px, py, val);
        }
      }
    }
  }

  // --- 왕관형 노란 유채꽃 머리 ---
  function drawRapeFlowerHead(cx, cy, scale, isMain) {
    const s = scale * bloomK;
    const coreVal = isMain ? 0.88 : 0.65;
    const petalVal = isMain ? 0.98 : 0.72;
    const calyxVal = 0.32;

    drawDab(cx, cy + 1.2 * s, 1.6 * s, 0.8 * s, calyxVal);
    drawDab(cx, cy + 0.3 * s, 1.8 * s, 1.2 * s, coreVal);

    const petals = [
      [0.0, -1.8, 1.1, 1.4],
      [-1.4, -1.4, 1.0, 1.3],
      [1.4, -1.4, 1.0, 1.3],
      [-2.4, -0.4, 1.1, 1.1],
      [2.4, -0.4, 1.1, 1.1]
    ];

    for (let i = 0; i < petals.length; i++) {
      const p = petals[i];
      drawDab(cx + p[0] * s, cy + p[1] * s, p[2] * s, p[3] * s, petalVal);
    }
  }

  // --- 아담한 크기의 보라색 잔꽃 (가로 폭 약 3~4픽셀로 축소 안정화) ---
  function drawCompactPurpleFlower(cx, cy, val) {
    const s = bloomK * 0.95;

    // 1. 작은 중심 코어
    drawDab(cx, cy, 1.3 * s, 0.8 * s, val);

    // 2. 좌우/상단으로 살짝만 삐져나온 작은 꽃잎 터치들
    drawDab(cx - 1.3 * s, cy - 0.2 * s, 0.8 * s, 0.5 * s, val + 0.05); // 좌 날개
    drawDab(cx + 1.3 * s, cy - 0.2 * s, 0.8 * s, 0.5 * s, val + 0.05); // 우 날개
    drawDab(cx, cy - 0.8 * s, 0.6 * s, 0.6 * s, val + 0.03);           // 위
  }

  const flowers = [
    { rx: 5,  fx: 5,  fy: 88,  s: 1.1, isMain: false },
    { rx: 7,  fx: 6,  fy: 78,  s: 1.0, isMain: false },
    { rx: 9,  fx: 9,  fy: 80,  s: 1.2, isMain: true },
    { rx: 11, fx: 11, fy: 91,  s: 1.1, isMain: false },
    { rx: 15, fx: 15, fy: 76,  s: 1.3, isMain: true },
    { rx: 17, fx: 17, fy: 86,  s: 1.1, isMain: false },
    { rx: 17, fx: 16, fy: 97,  s: 1.1, isMain: false },
    { rx: 20, fx: 20, fy: 89,  s: 1.1, isMain: false },
    { rx: 22, fx: 22, fy: 68,  s: 1.6, isMain: true },
    { rx: 27, fx: 27, fy: 100, s: 1.2, isMain: false },
    { rx: 28, fx: 28, fy: 76,  s: 1.4, isMain: true },
    { rx: 32, fx: 33, fy: 80,  s: 1.3, isMain: true },
    { rx: 42, fx: 43, fy: 116, s: 1.2, isMain: false },
    { rx: 55, fx: 56, fy: 120, s: 1.2, isMain: false }
  ];

  const purpleFlowers = [
    { rx: 14, bx: 14, by: 106 },
    { rx: 16, bx: 16, by: 104 },
    { rx: 22, bx: 22, by: 103 },
    { rx: 24, bx: 24, by: 101 },
    { rx: 25, bx: 25, by: 110 },
    { rx: 35, bx: 35, by: 112 },
    { rx: 37, bx: 37, by: 104 },
    { rx: 38, bx: 38, by: 113 },
    { rx: 43, bx: 43, by: 105 },
    { rx: 45, bx: 45, by: 99 },
    { rx: 49, bx: 49, by: 108 },
    { rx: 51, bx: 51, by: 103 },
    { rx: 59, bx: 59, by: 114 }
  ];

  const grassBlades = [
    [2, 127, 8, 108],  [4, 127, 3, 101],  [6, 127, 12, 105], [8, 127, 5, 111],
    [12, 127, 16, 103],[14, 127, 9, 107], [18, 127, 24, 110],[20, 127, 16, 113],
    [24, 127, 26, 104],[26, 127, 21, 109],[30, 127, 36, 110],[34, 127, 30, 105],
    [38, 127, 44, 104],[40, 127, 37, 113],[44, 127, 48, 101],[46, 127, 42, 110],
    [50, 127, 56, 107],[52, 127, 47, 112],[58, 127, 62, 113],[60, 127, 54, 109]
  ];

  // 1. 줄기들 바닥에 렌더링
  for (let i = 0; i < flowers.length; i++) {
    const fl = flowers[i];
    const sway = Math.sin(t + i * 1.5) * 1.1 * (1.0 + swayK * 0.4);
    const stemVal = 0.22 + (i % 3) * 0.03;
    drawBrushLine(fl.rx, 127, fl.fx + sway, fl.fy + 1.5, 0.7, stemVal);
  }

  for (let k = 0; k < purpleFlowers.length; k++) {
    const pf = purpleFlowers[k];
    const swayBud = Math.sin(t * 1.2 + k * 1.8) * 0.9 * (1.0 + swayK * 0.4);
    drawBrushLine(pf.rx, 127, pf.bx + swayBud, pf.by + 1.2, 0.5, 0.23);
  }

  // 2. 바닥 풀잎 렌더링
  for (let j = 0; j < grassBlades.length; j++) {
    const gb = grassBlades[j];
    const swayGrass = Math.sin(t * 1.3 + j) * 0.6;
    const bladeVal = 0.06 + ((j * 7) % 11) * 0.01;
    drawBrushLine(gb[0], gb[1], gb[2] + swayGrass, gb[3], 0.7, bladeVal);
  }

  // 3. 아담한 보라색 꽃머리 렌더링
  for (let k = 0; k < purpleFlowers.length; k++) {
    const pf = purpleFlowers[k];
    const swayBud = Math.sin(t * 1.2 + k * 1.8) * 0.9 * (1.0 + swayK * 0.4);
    const hx = pf.bx + swayBud;
    const hy = pf.by;
    const baseTone = 0.38 + (k % 4) * 0.03;
    drawCompactPurpleFlower(hx, hy, baseTone);
  }

  // 4. 노란 유채꽃 머리 렌더링
  for (let i = 0; i < flowers.length; i++) {
    const fl = flowers[i];
    const sway = Math.sin(t + i * 1.5) * 1.1 * (1.0 + swayK * 0.4);
    drawRapeFlowerHead(fl.fx + sway, fl.fy, fl.s, fl.isMain);
  }
}
  __stack.push({
    kind: "code", opacity: 1, blend: "normal", recolor: false, role: "paint", maskInvert: false,
    lut: [0,0,0,0,0,50,6,53,0,99,12,105,0,149,17,158,0,198,23,211,0,239,28,255,0,236,29,255,0,232,30,255,0,228,30,255,0,225,31,255,0,221,32,255,0,217,32,255,0,214,33,255,0,210,34,255,0,206,35,255,0,203,35,255,0,199,36,255,0,195,37,255,0,192,37,255,0,188,38,255,0,184,39,255,0,181,40,255,0,177,40,255,0,173,41,255,0,170,42,255,0,166,43,255,0,162,43,255,0,159,44,255,0,155,45,255,0,151,45,255,0,148,46,255,0,144,47,255,0,140,48,255,0,137,48,255,0,133,49,255,0,129,50,255,0,126,50,255,0,122,51,255,0,118,52,255,0,115,53,255,0,111,53,255,0,107,54,255,0,104,55,255,0,100,55,255,0,96,56,255,0,92,57,255,1,94,58,255,3,96,59,255,4,98,59,255,5,100,60,255,7,102,61,255,8,105,62,255,9,107,63,255,11,109,63,255,12,111,64,255,14,113,65,255,15,115,66,255,16,117,67,255,18,119,67,255,19,122,68,255,20,124,69,255,22,126,70,255,23,128,71,255,24,130,72,255,26,132,72,255,27,134,73,255,29,136,74,255,30,139,75,255,31,141,76,255,33,143,76,255,34,145,77,255,35,147,78,255,37,149,79,255,38,151,80,255,40,154,81,255,41,156,81,255,42,158,82,255,44,160,83,255,45,162,84,255,46,164,85,255,48,166,85,255,49,168,86,255,50,171,87,255,52,173,88,255,66,162,95,255,82,150,102,255,97,137,110,255,113,125,118,255,129,112,126,255,145,100,133,255,161,88,141,255,169,81,145,255,169,82,146,255,170,82,146,255,170,83,146,255,170,84,147,255,171,84,147,255,171,85,148,255,171,85,148,255,172,86,148,255,172,87,149,255,172,87,149,255,173,88,149,255,173,88,150,255,173,89,150,255,174,90,151,255,174,90,151,255,174,91,151,255,174,91,152,255,175,92,152,255,175,93,153,255,175,93,153,255,176,94,153,255,176,94,154,255,176,95,154,255,177,96,155,255,177,96,155,255,177,97,155,255,178,97,156,255,178,98,156,255,178,99,156,255,178,99,157,255,179,100,157,255,179,100,158,255,179,101,158,255,180,102,158,255,180,102,159,255,180,103,159,255,181,103,160,255,181,104,160,255,192,126,154,255,202,149,149,255,213,172,143,255,224,194,137,255,235,217,132,255,246,240,126,255,253,255,122,255,252,254,121,255,252,254,120,255,251,254,119,255,251,253,118,255,251,253,118,255,250,252,117,255,250,252,116,255,249,251,115,255,249,251,114,255,248,250,114,255,248,250,113,255,247,250,112,255,247,249,111,255,246,249,110,255,246,248,110,255,246,248,109,255,245,247,108,255,245,247,107,255,244,246,106,255,244,246,106,255,243,246,105,255,243,245,104,255,242,245,103,255,242,244,102,255,241,244,102,255,241,243,101,255,240,243,100,255,240,243,99,255,240,242,98,255,239,242,97,255,239,241,97,255,238,241,96,255,238,240,95,255,237,240,94,255,237,239,93,255,236,239,93,255,236,239,92,255,235,238,91,255,235,238,90,255,235,237,89,255,234,237,89,255,234,236,88,255,233,236,87,255,233,236,86,255,232,235,85,255,232,235,85,255,231,234,84,255,231,234,83,255,230,233,82,255,230,233,81,255,230,232,80,255,229,232,80,255,229,232,79,255,228,231,78,255,228,231,77,255,227,230,76,255,231,235,62,255,241,244,32,255,250,254,2,255,251,255,0,255,251,254,0,255,251,254,0,255,251,254,0,255,251,253,0,255,251,253,0,255,251,253,0,255,252,252,0,255,252,252,0,255,252,251,0,255,252,251,0,255,252,251,0,255,252,250,0,255,252,250,0,255,252,250,0,255,252,249,0,255,252,249,0,255,252,249,0,255,252,248,0,255,252,248,0,255,252,247,0,255,253,247,0,255,253,247,0,255,253,246,0,255,253,246,0,255,253,246,0,255,253,245,0,255,253,245,0,255,253,245,0,255,253,244,0,255,253,244,0,255,253,244,0,255,253,243,0,255,253,243,0,255,253,242,0,255,253,242,0,255,254,242,0,255,254,241,0,255,254,241,0,255,254,241,0,255,254,240,0,255,254,240,0,255,254,240,0,255,254,239,0,255,254,239,0,255,254,238,0,255,254,238,0,255,254,238,0,255,254,237,0,255,254,237,0,255,254,237,0,255,255,236,0,255,255,236,0,255,255,236,0,255,255,235,0,255,255,235,0,255,255,235,0,255,255,234,0,255,255,234,0,255,255,234,0,255],
    buf: new Uint8ClampedArray(__W * __H * 4),
    params: {},
    fSetup: typeof setup === "function" ? setup : null,
    fUpdate: typeof update === "function" ? update : null,
    fDraw: typeof draw === "function" ? draw : null,
  });
})();

// Mask grouping: a mask layer paints nothing — it gates the nearest paint
// layer BELOW it (stacked masks intersect). Binary threshold: pixel-art masks
// switch on where alpha >= 0.5, code masks where luminance × alpha >= 0.5.
const __groups = [];
{
  let last = null;
  for (const L of __stack) {
    if (L.role === "mask") { if (last) last.masks.push(L); continue; }
    L.masks = [];
    last = L;
    if (L.opacity > 0) __groups.push(L);
  }
}

function __maskOn(M, j) {
  const B = M.buf;
  let on;
  if (M.kind === "pixel") on = B[j + 3] >= 128;
  else on = (0.2126 * B[j] + 0.7152 * B[j + 1] + 0.0722 * B[j + 2]) * (B[j + 3] / 255) >= 127.5;
  return M.maskInvert ? !on : on;
}

// One shared knob set: every layer receives the same input, exactly like
// patterns running on the device's four physical encoders.
function __mirror(L) {
  const inp = L.lastInput;
  if (!inp) return;
  L.params.knobValues = inp.knobValues;
  L.params.knobNormalized = inp.knobNormalized;
  L.params.knobDeltas = inp.knobDeltas;
  L.params.knobRanges = inp.knobRanges;
  L.params.btnPressed = inp.btnPressed;
  L.params.btnHeld = inp.btnHeld;
}

export function setup(params) {
  for (const L of __stack) {
    if (L.kind !== "code") continue;
    L.display = __mkDisplay(L);
    if (L.fSetup) L.fSetup(L.params);
  }
}

export function update(dt, input, params) {
  for (const L of __stack) {
    if (L.kind !== "code") continue;
    L.lastInput = input;
    __mirror(L);
    if (L.fUpdate) L.fUpdate(dt, input, L.params);
  }
}

export function draw(display, params, time) {
  for (const L of __stack) {
    if (L.kind !== "code" || !L.fDraw) continue;
    L.buf.fill(0);
    __mirror(L);
    L.fDraw(L.display, L.params, time);
    if (L.recolor && L.lut) {
      const B = L.buf, T = L.lut;
      for (let i = 0; i < B.length; i += 4) {
        if (B[i + 3] === 0) continue;
        const lum = Math.round(0.2126 * B[i] + 0.7152 * B[i + 1] + 0.0722 * B[i + 2]) * 4;
        B[i] = T[lum]; B[i + 1] = T[lum + 1]; B[i + 2] = T[lum + 2]; B[i + 3] = T[lum + 3];
      }
    }
  }

  // Composite bottom → top over the panel's opaque black.
  const count = __W * __H;
  for (let i = 0; i < count; i++) {
    const j = i * 4;
    let r = 0, g = 0, b = 0;
    for (const L of __groups) {
      let hidden = false;
      for (let m = 0; m < L.masks.length; m++) {
        if (!__maskOn(L.masks[m], j)) { hidden = true; break; }
      }
      if (hidden) continue;
      const B = L.buf;
      const a = (B[j + 3] / 255) * L.opacity;
      if (a <= 0) continue;
      const sr = B[j], sg = B[j + 1], sb = B[j + 2], inv = 1 - a;
      if (L.blend === "add") { r += sr * a; g += sg * a; b += sb * a; }
      else if (L.blend === "multiply") {
        r = r * inv + ((r * sr) / 255) * a;
        g = g * inv + ((g * sg) / 255) * a;
        b = b * inv + ((b * sb) / 255) * a;
      } else if (L.blend === "screen") {
        r = r * inv + (255 - ((255 - r) * (255 - sr)) / 255) * a;
        g = g * inv + (255 - ((255 - g) * (255 - sg)) / 255) * a;
        b = b * inv + (255 - ((255 - b) * (255 - sb)) / 255) * a;
      } else {
        r = r * inv + sr * a;
        g = g * inv + sg * a;
        b = b * inv + sb * a;
      }
    }
    display.setPixel(i % __W, (i / __W) | 0, r, g, b);
  }
}

// @stack v1 d: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

// ── Made with Patternflow Community · https://patternflow.work/community ──
// Shared under CC-BY-SA-4.0. Attribution is part of this licence —
// please keep this notice and the author credit above when you reuse,
// remix, or redistribute this pattern. Do not delete it.
