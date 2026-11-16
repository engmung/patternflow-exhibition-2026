// @matrix 64x128
// @knobs Bloom=2..8.5, CloudSize=0.8..2.6, Wind=0.3..5.5, Unused=0..1
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



// Knob 1 (Bloom): 생성되는 구름 덩어리의 총개수 (최소 2개 ~ 6개, 가운데 5개, 약 62%부터 6개)
// Knob 2 (CloudSize): 큰 구름 덩어리 전체 중심 기준 크기 (0.8배 ~ 2.6배)
// Knob 3 (Wind): 오른쪽으로 바람 타고 흘러가는 속도
// Knob 4 (Unused): 미사용
// K1 누르기: 돌풍 - 구름이 오른쪽으로 확 빨리 밀려가며 옆으로 살짝 늘어나고 몽글거림이 빨라졌다가 약 2.5초에 걸쳐 가라앉는다.
//            다시 누르면 지금 세기에서 이어서 다시 세진다. 꽃 레이어도 같은 순간 바람을 맞는다.

function setup(params) {
  params.wind = 0;
  params.gustTarget = 0; // K1 누르기 돌풍 (대기 복귀 때 0)
  params.gust = 0;       // 돌풍 세기 0..1 (대기 복귀 때 0)
  params.stretch = 1.0;  // update가 매 프레임 gust에서 다시 계산

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
        pulseFreq: 0.7 + hash(s + 6.6) * 0.6,
        p: 0 // 아래에서 phase로 시작
      });
      dabs[dabs.length - 1].p = dabs[dabs.length - 1].phase;
    }
    return { ...pc, dabs };
  });

  // 하강된 구름 높이에 맞춘 버퍼 크기 (최대 70px 높이)
  params.transBuffer = new Float32Array(64 * 72);
  params.toneBuffer = new Float32Array(64 * 72);
  params.countBuffer = new Uint8Array(64 * 72);
}

function update(dt, input, params) {
  // --- K1 누르기: 돌풍 (꽃 레이어와 똑같은 식 - 두 레이어가 같은 input을 받아 같은 값이 된다) ---
  // gustTarget: 누르면 1, 초당 0.45씩 줄어 약 2.2초에 0 (0에서 멈춤 - 누적 없음)
  // gust: gustTarget을 부드럽게 따라가는 실제 세기 0..1 (시정수 약 0.14초 -> 누르는 순간 튀지 않고 빠르게 차오름)
  // 효과 중에 다시 누르면 gustTarget만 1로 돌아가고 gust는 지금 값에서 이어서 올라간다.
  // 펌웨어 3분 대기 복귀: gustTarget = 0, gust = 0
  if (input.btnPressed && input.btnPressed[0]) params.gustTarget = 1.0;
  params.gustTarget = Math.max(0.0, params.gustTarget - dt * 0.45);
  params.gust += (params.gustTarget - params.gust) * Math.min(1.0, dt * 7.0);
  if (params.gustTarget === 0.0 && params.gust < 0.001) params.gust = 0.0;
  const gust = params.gust;

  const speed = input.knobValues[2];
  // 돌풍은 흐름 속도에 더해진다: K3 최소(0.033/s)에서도 확 밀려가고(+0.4/s), 최대(0.6/s)에서도 과하지 않다.
  params.wind += dt * (speed * 0.11 + gust * 0.4);
  // wind는 span(=2.0) 안에서만 쓰인다 -> 2.0에서 정확히 한 바퀴.
  // C++ 번역 시: 이 주기(2.0)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  if (params.wind >= 2.0) params.wind -= 2.0;
  // 덩어리 안 붓자국(dab)마다 위상 p를 따로 누적한다 (pulseFreq가 제각각이라 공용 시간을 쓰면 wrap이 안 맞는다).
  // p는 cos(p), sin(p*1.3), sin(p*1.4)에만 쓰인다 -> 20π에서 정확히 한 바퀴.
  // C++ 번역 시: 이 주기(20π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  const clouds = params.clouds;
  const TWENTY_PI = 20.0 * Math.PI;
  for (let c = 0; c < clouds.length; c++) {
    const dabs = clouds[c].dabs;
    for (let d = 0; d < dabs.length; d++) {
      const dab = dabs[d];
      dab.p += dt * 1.1 * dab.pulseFreq * (1.0 + 2.0 * gust); // 돌풍 때 몽글거림도 빨라진다
      if (dab.p >= TWENTY_PI) dab.p -= TWENTY_PI;
    }
  }
  params.count = input.knobValues[0];
  params.scale = input.knobValues[1];
  params.stretch = 1.0 + 0.3 * gust; // 돌풍에 구름이 가로로 늘어나는 배율 (1 ~ 1.3)
}

function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const count = params.count;
  const scale = params.scale;
  const jitter = 1.0 + (-1.671) * 0.3; // 0.4987
  const wind = params.wind;
  const clouds = params.clouds;
  const stretch = params.stretch; // 돌풍 가로 늘어남 (1 ~ 1.3)

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

      const p = dab.p;
      const driftX = Math.cos(p) * 0.007;
      const driftY = Math.sin(p * 1.3) * 0.004;

      const dabPosX = cx + (dab.ox + driftX) * jitter * scale * stretch;
      const dabPosY = cloud.y + (dab.oy + driftY) * jitter * scale;

      const sizePulse = 1.0 + Math.sin(p * 1.4) * 0.10;
      const rX = dab.rx * sqrtScale * sizePulse * stretch;
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



// Knob 1 (Bloom): 구름 개수와 꽃 크기를 함께 바꾼다. 꽃은 작은 봉오리(0.4)에서 활짝 핀 큰 꽃(1.6)까지 자라고, 줄기도 함께 자라 꽃머리가 올라간다 (가운데 1.0)
// Knob 2 (CloudSize): 줄기의 흔들림 폭 (구름 크기와 함께)
// Knob 3 (Wind): 바람에 줄기와 꽃들이 흔들리는 속도 (구름 흐름과 함께)
// Knob 4 (Unused): 미사용
// K1 누르기: 돌풍 - 줄기와 꽃이 오른쪽(구름이 흘러가는 쪽)으로 확 눕고 더 빠르고 크게 흔들리다가 약 2.5초에 걸쳐 제자리로 돌아온다.
//            다시 누르면 지금 세기에서 이어서 다시 세진다. 구름 레이어도 같은 순간 빨라진다.

function setup(params) {
  params.windTime = 0;
  params.gustTarget = 0; // K1 누르기 돌풍 (대기 복귀 때 0)
  params.gust = 0;       // 돌풍 세기 0..1 (대기 복귀 때 0)
  params.swayAmp = 1.0;  // update가 매 프레임 gust에서 다시 계산
  params.lean = 0.0;
}

function update(dt, input, params) {
  // --- K1 누르기: 돌풍 (구름 레이어와 똑같은 식 - 두 레이어가 같은 input을 받아 같은 값이 된다) ---
  // gustTarget: 누르면 1, 초당 0.45씩 줄어 약 2.2초에 0 (0에서 멈춤 - 누적 없음)
  // gust: gustTarget을 부드럽게 따라가는 실제 세기 0..1 (시정수 약 0.14초 -> 누르는 순간 튀지 않고 빠르게 차오름)
  // 효과 중에 다시 누르면 gustTarget만 1로 돌아가고 gust는 지금 값에서 이어서 올라간다.
  // 펌웨어 3분 대기 복귀: gustTarget = 0, gust = 0
  if (input.btnPressed && input.btnPressed[0]) params.gustTarget = 1.0;
  params.gustTarget = Math.max(0.0, params.gustTarget - dt * 0.45);
  params.gust += (params.gustTarget - params.gust) * Math.min(1.0, dt * 7.0);
  if (params.gustTarget === 0.0 && params.gust < 0.001) params.gust = 0.0;
  const gust = params.gust;

  // 돌풍은 흔들림 속도에 더해진다(곱하지 않음): K3 최소에서도 확 빨라지고, 최대에서도 과하게 떨리지 않는다.
  params.windTime += dt * (input.knobValues[2] * 2.0 + gust * 6.0);
  // windTime은 sin(t), sin(t*1.2), sin(t*1.3)에만 쓰인다 -> 20π에서 정확히 한 바퀴.
  // C++ 번역 시: 이 주기(20π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다
  if (params.windTime >= 20.0 * Math.PI) params.windTime -= 20.0 * Math.PI;

  const rawBloom = input.knobValues[0];
  const tNorm = Math.max(0.0, Math.min(1.0, (rawBloom - 2.0) / 6.5));
  // 꽃 크기: 0.4(작은 봉오리) ~ 1.6(활짝 핀 큰 꽃), 가운데 1.0. 한 칸마다 크기가 눈에 보이게 넓게 잡았다.
  params.bloom = 0.4 + tNorm * 1.2;

  params.sway = input.knobValues[1];

  // 돌풍 모양 (프레임당 한 번): 흔들림 폭 x(1 + 1.5 gust), 오른쪽으로 눕는 정도(px)
  params.swayAmp = 1.0 + 1.5 * gust;
  params.lean = gust * 5.0;
}

function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const bloomK = params.bloom;
  const swayK = params.sway;
  const t = params.windTime;
  const swayAmp = params.swayAmp; // 돌풍 흔들림 배율 (1 ~ 2.5)
  const lean = params.lean;       // 돌풍에 오른쪽으로 눕는 픽셀 (0 ~ 5)

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
    // 너무 작으면 픽셀이 사라지므로 봉오리 크기(0.6)에서 멈춘다
    const s = Math.max(0.6, bloomK * 0.95);

    // 1. 작은 중심 코어
    drawDab(cx, cy, 1.3 * s, 0.8 * s, val);

    // 2. 좌우/상단으로 살짝만 삐져나온 작은 꽃잎 터치들
    drawDab(cx - 1.3 * s, cy - 0.2 * s, 0.8 * s, 0.5 * s, val + 0.05); // 좌 날개
    drawDab(cx + 1.3 * s, cy - 0.2 * s, 0.8 * s, 0.5 * s, val + 0.05); // 우 날개
    drawDab(cx, cy - 0.8 * s, 0.6 * s, 0.6 * s, val + 0.03);           // 위
  }

  // 꽃이 필수록 줄기가 자라 꽃머리가 위로 올라간다 (가운데 0, 봉오리 -4px, 최대 +4px)
  const lift = (bloomK - 1.0) * 7.0;
  const liftBud = (bloomK - 1.0) * 3.5;

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
    const sway = Math.sin(t + i * 1.5) * 1.1 * (1.0 + swayK * 0.4) * swayAmp + lean;
    const stemVal = 0.22 + (i % 3) * 0.03;
    drawBrushLine(fl.rx, 127, fl.fx + sway, fl.fy - lift + 1.5, 0.7, stemVal);
  }

  for (let k = 0; k < purpleFlowers.length; k++) {
    const pf = purpleFlowers[k];
    const swayBud = Math.sin(t * 1.2 + k * 1.8) * 0.9 * (1.0 + swayK * 0.4) * swayAmp + lean * 0.8;
    drawBrushLine(pf.rx, 127, pf.bx + swayBud, pf.by - liftBud + 1.2, 0.5, 0.23);
  }

  // 2. 바닥 풀잎 렌더링
  for (let j = 0; j < grassBlades.length; j++) {
    const gb = grassBlades[j];
    const swayGrass = Math.sin(t * 1.3 + j) * 0.6 * swayAmp + lean * 0.6;
    const bladeVal = 0.06 + ((j * 7) % 11) * 0.01;
    drawBrushLine(gb[0], gb[1], gb[2] + swayGrass, gb[3], 0.7, bladeVal);
  }

  // 3. 아담한 보라색 꽃머리 렌더링
  for (let k = 0; k < purpleFlowers.length; k++) {
    const pf = purpleFlowers[k];
    const swayBud = Math.sin(t * 1.2 + k * 1.8) * 0.9 * (1.0 + swayK * 0.4) * swayAmp + lean * 0.8;
    const hx = pf.bx + swayBud;
    const hy = pf.by - liftBud;
    const baseTone = 0.38 + (k % 4) * 0.03;
    drawCompactPurpleFlower(hx, hy, baseTone);
  }

  // 4. 노란 유채꽃 머리 렌더링
  for (let i = 0; i < flowers.length; i++) {
    const fl = flowers[i];
    const sway = Math.sin(t + i * 1.5) * 1.1 * (1.0 + swayK * 0.4) * swayAmp + lean;
    drawRapeFlowerHead(fl.fx + sway, fl.fy - lift, fl.s, fl.isMain);
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
