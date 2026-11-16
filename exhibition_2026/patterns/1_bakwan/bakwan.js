// @ramp linear 0.046:#748a9a 1:#614700 0.105:#0095c7 0.141:#34180e 0.304:#a35914 0.5:#401212 0.52:#000000 0.833:#f0a400
// @knobs Foliage=0.25..1, Wind=-0.75..3.25, Speed=0.35..1.65, -
// @matrix 64x128


// Knob 1 (Foliage): Canopy cluster fullness, carpet coverage, and falling leaf count (centre 0.625)
// Knob 2 (Wind): Wind direction and strength (-0.75 = light left, 0 = calm "breathing", centre +1.25 = moderate right, +3.25 = strong right)
// Knob 3 (Speed): Tempo of sway, flutter, flicker and falling leaves (0.35x ~ 1.65x, centre 1.0x)
// Knob 4: unused
// K1 press: the next tree from a short curated list (TREE_SEEDS, cycling; the first is the author's 3831).
//   The new canopy shakes and sheds a short shower of leaves (~2.5 s) so the change reads as intentional.
//   K1 누르기: 다음 나무(목록 순환) + 새 나무가 흔들리며 잎이 한 번 우수수 떨어짐(약 2.5초). 다시 누르면 다음 나무로 넘어가고 잎비가 다시 시작됨.
// K2/K3 press: unused
// Idle-return reset (firmware): params.treeIndex = 0, params.showerT = SHOWER_DUR.
//   The tree is rebuilt on the next update() because treeIndex != builtIndex - do not rebuild in the reset itself.
//   대기 복귀 때: treeIndex = 0, showerT = SHOWER_DUR 로만 되돌리면 다음 update()가 첫 나무를 다시 짓는다.

// Curated tree seeds (complete, balanced, not clipped, clearly different), ordered for variety:
// 3831 tall crown (author) / 102 low round / 137 slender spire / 7 low and broad / 95 tall twin crown / 33 even trio / 43 big and full
const TREE_SEEDS = [3831, 102, 137, 7, 95, 33, 43];

// Leaf shower after a press
const SHOWER_COUNT = 48;   // fixed pool, independent of Foliage so it reads at every K1 position
const SHOWER_DUR = 2.6;    // seconds of real time (not scaled by Speed) - the response always takes the same time

function buildTree(seedInt, params) {
  let s = (Math.imul(seedInt ^ 0x6C62272E, 0x5DEECE66) + 0xB) & 0x7FFFFFFF;
  function rnd() {
    s = (Math.imul(s, 0x41C64E6D) + 0x3039) & 0x7FFFFFFF;
    return s / 2147483647;
  }

  // Independent random scales for each of the 3 trees (0.60x to 1.0x)
  const sc0 = 0.60 + rnd() * 0.40;
  const sc1 = 0.60 + rnd() * 0.40;
  const sc2 = 0.60 + rnd() * 0.40;
  params.scales = [sc0, sc1, sc2];

  const lean = (rnd() - 0.5) * 4.0;
  const clusterWidth = 18.0 + rnd() * 6.0;
  const centerX = 32.0 + (rnd() - 0.5) * 4.0;

  const t0BaseX = centerX - clusterWidth * (0.45 + rnd() * 0.1);
  const t1BaseX = centerX + (rnd() - 0.5) * 2.5;
  const t2BaseX = centerX + clusterWidth * (0.45 + rnd() * 0.1);

  // Height and thickness scaled toward bottom ground anchor point (yBase ~ 104..106)
  const t0H = (46 + rnd() * 8) * sc0;
  const t1H = (56 + rnd() * 8) * sc1;
  const t2H = (46 + rnd() * 8) * sc2;

  const t0TopY = 104 - t0H;
  const t1TopY = 106 - t1H;
  const t2TopY = 104 - t2H;

  params.trunks = [
    { x0: t0BaseX, y0: 104, x1: t0BaseX + (lean - 2) * sc0, y1: t0TopY, wBase: (11.5 + rnd() * 2.0) * sc0, wTop: (8.0 + rnd() * 1.5) * sc0, seed: rnd() * 8, sc: sc0 },
    { x0: t1BaseX, y0: 106, x1: t1BaseX + lean * sc1,       y1: t1TopY, wBase: (14.0 + rnd() * 2.5) * sc1, wTop: (9.5 + rnd() * 2.0) * sc1, seed: rnd() * 8, sc: sc1 },
    { x0: t2BaseX, y0: 104, x1: t2BaseX + (lean + 2) * sc2, y1: t2TopY, wBase: (11.5 + rnd() * 2.0) * sc2, wTop: (8.0 + rnd() * 1.5) * sc2, seed: rnd() * 8, sc: sc2 }
  ];

  function getTrunkX(tIdx, y) {
    const tr = params.trunks[tIdx];
    const f = Math.max(0.0, Math.min(1.0, (tr.y0 - y) / (tr.y0 - tr.y1)));
    return tr.x0 * (1.0 - f) + tr.x1 * f;
  }

  params.boughs = [];
  function addBough(p0, p1, p2, rad, seed, ownerScale) {
    params.boughs.push({ p0, p1, p2, rad: rad * ownerScale, seed });
  }

  // 1. Primary lateral wing boughs (scaled with Trunk 0 and Trunk 2)
  const leftReachX = Math.max(1, t0BaseX - (26 + rnd() * 6) * sc0);
  const rightReachX = Math.min(62, t2BaseX + (26 + rnd() * 6) * sc2);
  const leftMidY = 104 - 28 * sc0;
  const rightMidY = 104 - 28 * sc2;

  addBough([getTrunkX(0, 104 - 26 * sc0), 104 - 26 * sc0], [t0BaseX - 12 * sc0, leftMidY], [leftReachX, leftMidY - 3 + rnd() * 5], 3.4, rnd() * 6, sc0);
  addBough([getTrunkX(2, 104 - 26 * sc2), 104 - 26 * sc2], [t2BaseX + 12 * sc2, rightMidY], [rightReachX, rightMidY - 3 + rnd() * 5], 3.4, rnd() * 6, sc2);

  const scMid = (sc0 + sc2) * 0.5;
  const bridgeY1 = 104 - 38 * scMid;
  const bridgeY2 = 104 - 34 * scMid;
  addBough([getTrunkX(0, bridgeY1 + 1), bridgeY1 + 1], [centerX, bridgeY1], [getTrunkX(2, bridgeY1 - 1), bridgeY1 - 1], 3.0, rnd() * 6, scMid);
  addBough([getTrunkX(2, bridgeY2 + 1), bridgeY2 + 1], [centerX, bridgeY2], [getTrunkX(0, bridgeY2 - 1), bridgeY2 - 1], 2.8, rnd() * 6, scMid);

  const mLeftX = Math.max(2, t0BaseX - (22 + rnd() * 5) * sc0);
  const mRightX = Math.min(61, t2BaseX + (22 + rnd() * 5) * sc2);
  const mLeftY = 104 - 44 * sc0;
  const mRightY = 104 - 44 * sc2;
  addBough([getTrunkX(0, 104 - 40 * sc0), 104 - 40 * sc0], [t0BaseX - 10 * sc0, mLeftY + 6], [mLeftX, mLeftY], 2.9, rnd() * 6, sc0);
  addBough([getTrunkX(2, 104 - 40 * sc2), 104 - 40 * sc2], [t2BaseX + 10 * sc2, mRightY + 6], [mRightX, mRightY], 2.9, rnd() * 6, sc2);

  const crownForks = [
    { tid: 0, dx: -12, dy: -24, rad: 2.5, sc: sc0 },
    { tid: 1, dx: -8,  dy: -28, rad: 2.4, sc: sc1 },
    { tid: 1, dx: 8,   dy: -28, rad: 2.4, sc: sc1 },
    { tid: 2, dx: 12,  dy: -24, rad: 2.5, sc: sc2 }
  ];

  for (let c = 0; c < crownForks.length; c++) {
    const cf = crownForks[c];
    const tr = params.trunks[cf.tid];
    const endX = tr.x1 + cf.dx * cf.sc + (rnd() - 0.5) * 4;
    const endY = tr.y1 + cf.dy * cf.sc + (rnd() - 0.5) * 4;
    const midX = tr.x1 * 0.5 + endX * 0.5 + (rnd() - 0.5) * 3;
    const midY = tr.y1 * 0.5 + endY * 0.5;
    addBough([tr.x1, tr.y1], [midX, midY], [endX, endY], cf.rad, rnd() * 6, cf.sc);
  }

  // 2. Trunk lateral side shoots
  for (let tid = 0; tid < 3; tid++) {
    const tr = params.trunks[tid];
    const sc = params.scales[tid];
    for (let step = 1; step <= 3; step++) {
      const frac = step / 4.0;
      const cy = tr.y0 - (tr.y0 - tr.y1) * frac;
      const cx = getTrunkX(tid, cy);
      const dir = (tid === 0) ? -1 : (tid === 2 ? 1 : (rnd() > 0.5 ? 1 : -1));
      const drop = (rnd() > 0.45) ? -5 * sc : 3 * sc;
      const reach = (5 + rnd() * 4) * sc;
      addBough([cx, cy], [cx + dir * (reach * 0.5), cy + drop * 0.5], [cx + dir * reach, cy + drop], 1.6, rnd() * 6, sc);
    }
  }

  // 3. Mid-bough lateral twigs
  const primaryCount = params.boughs.length;
  for (let i = 0; i < primaryCount; i++) {
    const b = params.boughs[i];
    if (b.rad < 1.4) continue;

    for (let u of [0.35, 0.70]) {
      const ox = (1 - u) * (1 - u) * b.p0[0] + 2 * (1 - u) * u * b.p1[0] + u * u * b.p2[0];
      const oy = (1 - u) * (1 - u) * b.p0[1] + 2 * (1 - u) * u * b.p1[1] + u * u * b.p2[1];

      const tx = b.p2[0] - b.p0[0];
      const ty = b.p2[1] - b.p0[1];
      const len = Math.hypot(tx, ty) + 0.1;
      const normX = tx / len;
      const normY = ty / len;

      const crossDir = (u === 0.35) ? 1 : -1;
      const reach = (4 + rnd() * 4) * (b.rad / 3.0);
      const px = -normY * crossDir * reach;
      const py = normX * crossDir * reach - 1.5;

      addBough([ox, oy], [ox + px * 0.5, oy + py * 0.5], [ox + px, oy + py], b.rad * 0.5, rnd() * 6, 1.0);
    }
  }

  // 4. Ground roots (Dynamic Count: 2~3, Scaled, Length reduced)
  params.roots = [];
  function addProceduralRoot(p0, ownerScale, dir, bias, lenFactor) {
    const rx = p0[0];
    const ry = p0[1];
    // Length is reduced: P2 Y is 112~118 (was 118+), and scaled
    const baseLen = 14 + rnd() * 6;
    const finalLen = baseLen * ownerScale * lenFactor;

    // Spread bias: roots can go inwards (-bias) or outwards (+bias)
    const midSpread = (8 + rnd() * 6) * dir * bias;
    const endSpread = (16 + rnd() * 8) * dir * bias;

    const p1 = [rx + midSpread * ownerScale, ry + (finalLen * 0.4)];
    const p2 = [rx + endSpread * ownerScale, ry + finalLen]; // Final ground touch
    const rad = (3.2 + rnd() * 0.5) * ownerScale * lenFactor; // Slightly thinner if short

    params.roots.push({ p0, p1, p2, rad });
  }

  for (let tid = 0; tid < 3; tid++) {
    const tr = params.trunks[tid];
    const sc = tr.sc;
    const groundY = tr.y0 - 6; // Base connection slightly up from actual ground

    // Random Root Count: 2 or 3
    const rootCount = (rnd() > 0.45) ? 3 : 2;

    // Synthesis logic per trunk location
    if (tid === 0) { // LEFT Trunk
      // Main outer root
      addProceduralRoot([tr.x0 - 2*sc, groundY], sc, -1, 1.0, 1.0); // Bias outward
      // Inner central root
      addProceduralRoot([tr.x0 + 1*sc, groundY + 1], sc, 1, 0.4, 0.9); // Bias slightly inward, shorter
      if (rootCount === 3) {
        // Extra central one
        addProceduralRoot([tr.x0 - 0.5*sc, groundY + 0.5], sc, -1, 0.3, 0.95);
      }
    } else if (tid === 1) { // MIDDLE Trunk
      // Spreads both ways
      addProceduralRoot([tr.x0 - 1.5*sc, groundY], sc, -1, 0.8, 1.0); // Spread left
      addProceduralRoot([tr.x0 + 1.5*sc, groundY], sc, 1, 0.8, 1.0);  // Spread right
      if (rootCount === 3) {
        // One straight down or slightly inward
        const centralDir = (rnd() > 0.5) ? 1 : -1;
        addProceduralRoot([tr.x0, groundY + 0.5], sc, centralDir, 0.2, 0.95);
      }
    } else { // RIGHT Trunk
      // Main outer root
      addProceduralRoot([tr.x0 + 2*sc, groundY], sc, 1, 1.0, 1.0); // Bias outward
      // Inner central root
      addProceduralRoot([tr.x0 - 1*sc, groundY + 1], sc, -1, 0.4, 0.9); // Bias slightly inward, shorter
      if (rootCount === 3) {
        addProceduralRoot([tr.x0 + 0.5*sc, groundY + 0.5], sc, 1, 0.3, 0.95);
      }
    }
  }

  // 5. Canopy foliage clusters
  params.branchLeaves = [];
  let leafAttempts = 0;
  while (params.branchLeaves.length < 280 && leafAttempts < 1400) {
    leafAttempts++;
    const b = params.boughs[Math.floor(rnd() * params.boughs.length)];
    const u = 0.15 + 0.85 * rnd();
    const bx = (1 - u) * (1 - u) * b.p0[0] + 2 * (1 - u) * u * b.p1[0] + u * u * b.p2[0];
    const by = (1 - u) * (1 - u) * b.p0[1] + 2 * (1 - u) * u * b.p1[1] + u * u * b.p2[1];

    const upperFactor = Math.max(0.0, Math.min(1.0, (82.0 - by) / 60.0));
    const keepProb = Math.pow(upperFactor, 1.6);

    if (rnd() < keepProb) {
      const jx = Math.sin(leafAttempts * 4.3 + rnd()) * (4.0 + (leafAttempts % 4));
      const jy = Math.cos(leafAttempts * 3.7 + rnd()) * (3.0 + (leafAttempts % 4));
      params.branchLeaves.push({
        x: bx + jx,
        y: by + jy,
        size: 1.2 + (params.branchLeaves.length % 3) * 0.35,
        seed: leafAttempts * 0.73 + rnd() * 2.0,
        tone: 0.84 + ((params.branchLeaves.length % 6) / 6) * 0.14
      });
    }
  }

  // 6. Leaves resting on roots (Dynamic, dependent on generated roots)
  params.rootLeaves = [];
  for (let r = 0; r < params.roots.length; r++) {
    const rt = params.roots[r];
    // Number of leaves depends on root thickness/length
    const leavesPerRoot = 8 + Math.floor(rt.rad * 2.0);
    for (let k = 0; k < leavesPerRoot; k++) {
      const u = 0.2 + 0.8 * (k / (leavesPerRoot - 1.0));
      const rx = (1 - u) * (1 - u) * rt.p0[0] + 2 * (1 - u) * u * rt.p1[0] + u * u * rt.p2[0];
      const ry = (1 - u) * (1 - u) * rt.p0[1] + 2 * (1 - u) * u * rt.p1[1] + u * u * rt.p2[1];
      const offX = (rnd() - 0.5) * (rt.rad * 1.5);
      const offY = (rnd() - 0.5) * (rt.rad * 1.3);
      params.rootLeaves.push({
        x: rx + offX,
        y: ry + offY,
        size: 1.2 + rnd() * 0.4,
        seed: (r * 14 + k) * 0.61,
        tone: 0.84 + rnd() * 0.14
      });
    }
  }

  // 7. Drifting falling leaves
  params.drifters = [];
  for (let k = 0; k < 60; k++) {
    const hostLeaf = params.branchLeaves[(k * 7 + Math.floor(rnd() * 5)) % params.branchLeaves.length];
    params.drifters.push({
      seed: k * 1.73 + rnd(),
      originX: hostLeaf.x,
      originY: hostLeaf.y,
      fallRate: 42.0 + (k % 5) * 9.0,
      phaseOffset: rnd() * 100.0,
      wobbleSeed: rnd() * 5.0
    });
  }

  params.neighborDrifters = [];
  for (let j = 0; j < 40; j++) {
    params.neighborDrifters.push({
      seed: j * 2.37 + rnd(),
      startY: 4 + (j * 17) % 65,
      fallRate: 45.0 + (j % 5) * 10.0,
      phaseOffset: rnd() * 100.0
    });
  }
}

// Build the tree for params.treeIndex and everything derived from it (drifter progress and cycle times).
// C++ 번역 시: 나무를 바꿀 때는 반드시 이 함수 하나로 - buildTree 다음에 낙엽 진행도/주기를 같이 다시 계산해야 한다
// C++ 번역 시: 배열은 setup에서 최대 크기로 한 번만 잡고(boughs 64, roots 9, branchLeaves 280, rootLeaves 144,
//   drifters 60, neighborDrifters 40) 누를 때는 개수만 0으로 되돌려 제자리에서 다시 채울 것 - 누를 때마다 new/vector
//   재할당 금지(며칠 동안 힙이 조각난다). 계산은 float(powf/sinf)로 - 누르는 프레임 한 번에 powf 약 1400번이 돈다
function rebuildTree(params) {
  buildTree(TREE_SEEDS[params.treeIndex], params);
  params.builtIndex = params.treeIndex;

  // Per-leaf fall progress in [0,1): same value the old ((t + phaseOffset) % cycleTime) / cycleTime gave at t = 0
  for (let d = 0; d < params.drifters.length; d++) {
    const df = params.drifters[d];
    df.cycleTime = (118.0 - df.originY) / df.fallRate;
    df.prog = (df.phaseOffset % df.cycleTime) / df.cycleTime;
  }
  for (let j = 0; j < params.neighborDrifters.length; j++) {
    const nd = params.neighborDrifters[j];
    nd.cycleTime = (120.0 - nd.startY) / nd.fallRate;
    nd.prog = (nd.phaseOffset % nd.cycleTime) / nd.cycleTime;
  }
}

export function setup(params) {
  params.time = 0;

  // K1 press state (the idle return resets exactly these two)
  params.treeIndex = 0;          // which TREE_SEEDS entry is shown
  params.showerT = SHOWER_DUR;   // seconds since the last press; >= SHOWER_DUR means no shower
  params.builtIndex = -1;
  rebuildTree(params);

  // Shower leaves: fixed per-leaf constants, made once. Their start points are taken from the canopy at each press.
  params.shower = [];
  for (let k = 0; k < SHOWER_COUNT; k++) {
    const a = k * 0.61803398875;
    const f = a - Math.floor(a);                  // evenly spread 0..1, no random needed
    params.shower.push({
      x: 0, y: 0,
      delay: f * 0.35,                             // leaves let go over the first 0.35 s
      land: 100.0 + (k % 5) * 3.0,                  // lands on the leaf carpet (y 100..112) and joins it
      grav: 36.0 + ((k * 5) % 9) * 4.0,             // px/s^2, 36..68: every leaf lands before SHOWER_DUR, even from the top
      wob: k * 1.37,
      rad: 1.0 + (k % 3) * 0.25
    });
  }
}

export function update(dt, input, params) {
  params.foliage = input.knobValues[0];
  params.windDir = input.knobValues[1];
  const speed = input.knobValues[2];

  // K1 press: next tree, then the shower starts from the new canopy (pressing again re-triggers it)
  if (input.btnPressed && input.btnPressed[0]) {
    params.treeIndex = (params.treeIndex + 1) % TREE_SEEDS.length;
    params.showerT = 0.0;
  }
  if (params.treeIndex !== params.builtIndex) {
    rebuildTree(params);
    const L = params.branchLeaves;
    for (let k = 0; k < SHOWER_COUNT; k++) {
      const lf = L[(k * 37 + 11) % L.length];
      params.shower[k].x = lf.x;
      params.shower[k].y = lf.y;
    }
  }
  // Clamped, never grows past SHOWER_DUR (panel runs for days)
  // C++ 번역 시: showerT는 SHOWER_DUR에서 멈춘다 - 계속 더하지 말 것
  if (params.showerT < SHOWER_DUR) params.showerT = Math.min(SHOWER_DUR, params.showerT + dt);
  const e = 1.0 - params.showerT / SHOWER_DUR;
  params.shake = e * e;          // canopy shake envelope 1 -> 0, smooth at the end

  // Every use of params.time is sin/cos(t * f + c) with f in {1.8, 2.5, 3.0, 3.5, 3.8, 4.0}: all multiples of 0.1, so 20*PI is an exact period.
  // C++ 번역 시: 이 주기(20π)를 그대로 유지 - 다른 값으로 wrap하면 화면이 튄다 (float32에서도 시간이 커지지 않게)
  const TIME_WRAP = 20.0 * Math.PI;
  params.time += dt * speed;
  if (params.time >= TIME_WRAP) params.time -= TIME_WRAP;

  // Falling leaves: each leaf has its own progress, advanced by dt * speed / cycleTime and wrapped at exactly 1 cycle.
  // C++ 번역 시: 잎마다 진행도를 따로 두고 정확히 1에서 wrap할 것 - 공유 시간 % cycleTime으로 되돌리면 시간이 커질수록 깨진다
  for (let d = 0; d < params.drifters.length; d++) {
    const df = params.drifters[d];
    df.prog += dt * speed / df.cycleTime;
    df.prog -= Math.floor(df.prog);
  }
  for (let j = 0; j < params.neighborDrifters.length; j++) {
    const nd = params.neighborDrifters[j];
    nd.prog += dt * speed / nd.cycleTime;
    nd.prog -= Math.floor(nd.prog);
  }
}

export function draw(display, params, time) {
  const w = display.width;
  const h = display.height;
  const t = params.time;
  const foliage = params.foliage;
  const windDir = params.windDir;
  const horizonY = 82;
  const shake = params.shake;

  function getTreeSway(y) {
    const heightRatio = Math.max(0.0, Math.min(1.0, (110.0 - y) / 110.0));
    const gentleBreathe = Math.sin(t * 1.8 - y * 0.03) * 0.45;
    return (windDir * 1.2 + gentleBreathe * Math.max(Math.abs(windDir), 0.8)) * Math.pow(heightRatio, 1.7);
  }

  // 1. Pale overcast sky
  for (let y = 0; y < horizonY; y++) {
    const skyTone = 0.03 + (y / horizonY) * 0.06;
    for (let x = 0; x < w; x++) {
      display.setValue(x, y, skyTone);
    }
  }

  // 2. High-detail impasto golden leaf carpet
  for (let y = horizonY; y < h; y++) {
    const depth = (y - horizonY) / (h - horizonY);
    const mound = Math.sin(y * 0.16 + Math.cos(y * 0.09)) * 2.5;

    for (let x = 0; x < w; x++) {
      const p1 = Math.sin(x * 2.1 + y * 2.7 + mound);
      const p2 = Math.cos(x * 3.7 - y * 1.9 + mound * 0.5);
      const microGlint = Math.sin(x * 5.3 + y * 4.9);
      const pointillism = Math.abs(p1 * p2);

      const baseGround = 0.72 + depth * 0.10;
      const lightTilt = (p1 - p2) * 0.06;
      let flakeVal = 0.82 + depth * 0.12 + lightTilt;

      if (microGlint > 0.75) flakeVal += 0.08;

      const coverage = 0.28 + depth * 0.52 * foliage;
      if (pointillism < coverage) {
        display.setValue(x, y, Math.min(0.98, Math.max(0.74, flakeVal)));
      } else {
        display.setValue(x, y, baseGround);
      }
    }
  }

  // 3. Tree trunks (each individually scaled)
  for (let tid = 0; tid < params.trunks.length; tid++) {
    const tr = params.trunks[tid];
    const steps = Math.floor(tr.y0 - tr.y1);

    for (let i = 0; i <= steps; i++) {
      const frac = i / steps;
      const cy = tr.y0 - frac * (tr.y0 - tr.y1);
      const halfW = (tr.wBase * (1.0 - frac) + tr.wTop * frac) * 0.5;

      const swayOffset = getTreeSway(cy) * 0.75 * tr.sc;
      const organicContour = Math.sin(cy * 0.22 + tr.seed * 3.0) * 0.5;
      const cx = (tr.x0 * (1.0 - frac) + tr.x1 * frac) + swayOffset + organicContour;

      const minX = Math.floor(cx - halfW);
      const maxX = Math.ceil(cx + halfW);

      for (let px = minX; px <= maxX; px++) {
        const u = (px - (cx - halfW)) / (halfW * 2.0);
        if (u >= 0.0 && u <= 1.0) {
          const directionalLight = (1.0 - u);
          const macroShadow = 1.0 - (px / w) * 0.24;
          const cylinder = Math.sin(u * Math.PI);

          const warpY = cy * 0.24 + Math.sin(px * 0.45 + tr.seed) * 1.6;
          const warpX = u * 13.0 + Math.sin(cy * 0.18 + px * 0.35) * 2.2;
          const barkGrain = Math.sin(warpX) * 0.045 + Math.cos(warpY * 1.8 + warpX * 0.6) * 0.035;

          let tone = (0.24 + cylinder * 0.10 + directionalLight * 0.18) * macroShadow + barkGrain;
          tone = Math.max(0.20, Math.min(0.48, tone));
          display.setValue(px, cy, tone);
        }
      }
    }
  }

  // 4. Roots
  for (let r = 0; r < params.roots.length; r++) {
    const rt = params.roots[r];
    const rSteps = 24;
    for (let s = 0; s <= rSteps; s++) {
      const u = s / rSteps;
      const rx = (1 - u) * (1 - u) * rt.p0[0] + 2 * (1 - u) * u * rt.p1[0] + u * u * rt.p2[0];
      const ry = (1 - u) * (1 - u) * rt.p0[1] + 2 * (1 - u) * u * rt.p1[1] + u * u * rt.p2[1];
      const curR = rt.rad * (1.0 - u * 0.50);
      const rLim = Math.ceil(curR);

      for (let dy = -rLim; dy <= rLim; dy++) {
        for (let dx = -rLim; dx <= rLim; dx++) {
          if (dx * dx + dy * dy <= curR * curR) {
            const sideFactor = 0.5 - (dx / curR) * 0.45;
            let rootVal = (0.26 + sideFactor * 0.18) * (1.0 - (rx / w) * 0.20);
            rootVal = Math.max(0.20, Math.min(0.46, rootVal));
            const px = Math.floor(rx + dx);
            const py = Math.floor(ry + dy);
            if (px >= 0 && px < w && py >= 0 && py < h) {
              display.setValue(px, py, rootVal);
            }
          }
        }
      }
    }
  }

  // 5. Boughs
  for (let b = 0; b < params.boughs.length; b++) {
    const bg = params.boughs[b];
    const bSteps = 34;

    for (let s = 0; s <= bSteps; s++) {
      const u = s / bSteps;
      const bx = (1 - u) * (1 - u) * bg.p0[0] + 2 * (1 - u) * u * bg.p1[0] + u * u * bg.p2[0];
      const by = (1 - u) * (1 - u) * bg.p0[1] + 2 * (1 - u) * u * bg.p1[1] + u * u * bg.p2[1];

      const swayOffset = getTreeSway(by);
      const px = bx + swayOffset;
      const py = by;
      const curRad = Math.max(0.7, bg.rad * (1.0 - u * 0.55));
      const radCeil = Math.ceil(curRad);
      const radSq = curRad * curRad;
      const invRad = 1.0 / curRad;
      const macroFalloff = 1.0 - (px / w) * 0.22;

      for (let dy = -radCeil; dy <= radCeil; dy++) {
        for (let dx = -radCeil; dx <= radCeil; dx++) {
          if (dx * dx + dy * dy <= radSq) {
            const lightVec = (-dx * 0.7 - dy * 0.7) * invRad;
            const reliefShade = 0.31 + lightVec * 0.12;
            let limbVal = reliefShade * macroFalloff;
            limbVal = Math.max(0.20, Math.min(0.48, limbVal));

            const ix = Math.floor(px + dx);
            const iy = Math.floor(py + dy);
            if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
              display.setValue(ix, iy, limbVal);
            }
          }
        }
      }
    }
  }

  function drawChunkyLeaf(cx, cy, rad, tone) {
    const rCeil = Math.ceil(rad);
    for (let dy = -rCeil; dy <= rCeil; dy++) {
      for (let dx = -rCeil; dx <= rCeil; dx++) {
        if (dx * dx + dy * dy <= rad * rad) {
          const px = Math.floor(cx + dx);
          const py = Math.floor(cy + dy);
          if (px >= 0 && px < w && py >= 0 && py < h) {
            const core = (dx === 0 && dy === 0) ? 0.04 : 0.0;
            display.setValue(px, py, Math.min(0.98, Math.max(0.82, tone + core)));
          }
        }
      }
    }
  }

  // 6. Root leaves
  if (params.rootLeaves) {
    const rootLeafCount = Math.floor(params.rootLeaves.length * foliage);
    for (let i = 0; i < rootLeafCount; i++) {
      const rLeaf = params.rootLeaves[i];
      drawChunkyLeaf(rLeaf.x, rLeaf.y, rLeaf.size, rLeaf.tone);
    }
  }

  // 7. Canopy foliage clusters
  const leafCount = Math.floor(params.branchLeaves.length * foliage);
  for (let i = 0; i < leafCount; i++) {
    const lf = params.branchLeaves[i];
    const swayOffset = getTreeSway(lf.y);
    let flutter = Math.sin(t * 2.5 + lf.seed * 2.0) * 0.45;
    if (shake > 0.0) flutter += Math.sin(params.showerT * 19.0 + lf.seed * 2.0) * 1.4 * shake;   // press: canopy shakes, fading out
    const lx = lf.x + swayOffset + flutter;
    const ly = lf.y;

    const flicker = 0.90 + 0.10 * Math.sin(t * 1.8 + lf.seed * 2.0);
    const val = lf.tone * flicker;
    drawChunkyLeaf(lx, ly, lf.size, val);
  }

  // 8. Native falling leaves
  const activeDrifterCount = Math.floor(params.drifters.length * foliage);
  for (let d = 0; d < activeDrifterCount; d++) {
    const df = params.drifters[d];
    const fallDistance = 118.0 - df.originY;
    const prog = df.prog;
    const ly = df.originY + prog * fallDistance;

    if (ly >= df.originY && ly < 122) {
      const windPush = prog * windDir * 28.0;
      const flutter = Math.sin(prog * Math.PI * 4.0 + df.wobbleSeed) * 2.2;
      let lx = df.originX + windPush + flutter;

      lx = ((lx % w) + w) % w;

      const birthFade = Math.min(1.0, prog * 8.0);
      const groundFade = Math.max(0.0, 1.0 - Math.max(0.0, ly - 98.0) / 22.0);
      const alpha = birthFade * groundFade;

      if (alpha > 0.15) {
        const tumble = Math.abs(Math.sin(t * 3.5 + df.seed * 2.5));
        const flakeRad = 0.8 + tumble * 0.6;
        const shimmer = 0.88 + 0.12 * Math.sin(t * 4.0 + df.seed * 3.0);
        drawChunkyLeaf(lx, ly, flakeRad, shimmer);
      }
    }
  }

  // 9. Neighboring tree leaves
  if (Math.abs(windDir) > 0.2) {
    const activeNeighborCount = Math.floor(params.neighborDrifters.length * foliage);
    for (let j = 0; j < activeNeighborCount; j++) {
      const nd = params.neighborDrifters[j];
      const fallDistance = 120.0 - nd.startY;
      const prog = nd.prog;
      const ly = nd.startY + prog * fallDistance;

      if (ly < 122) {
        const startX = (windDir > 0) ? -10.0 : (w + 10.0);
        const windPush = prog * windDir * 38.0;
        const flutter = Math.sin(prog * Math.PI * 5.0 + nd.seed) * 2.0;
        const lx = startX + windPush + flutter;

        if (lx >= -2 && lx < w + 2) {
          const tumble = Math.abs(Math.cos(t * 3.0 + nd.seed * 2.0));
          const flakeRad = 0.7 + tumble * 0.55;
          const shimmer = 0.86 + 0.14 * Math.sin(t * 3.8 + nd.seed);
          drawChunkyLeaf(lx, ly, flakeRad, shimmer);
        }
      }
    }
  }

  // 10. Leaf shower after a K1 press (per leaf, not per pixel)
  if (params.showerT < SHOWER_DUR) {
    for (let k = 0; k < SHOWER_COUNT; k++) {
      const sl = params.shower[k];
      const st = params.showerT - sl.delay;
      if (st <= 0.0) continue;
      const ly = sl.y + 0.5 * sl.grav * st * st + 6.0 * st;
      if (ly >= sl.land) continue;
      const lx = sl.x + windDir * 7.0 * st + Math.sin(st * 7.0 + sl.wob) * 1.6;
      const tumble = Math.abs(Math.sin(st * 9.0 + sl.wob));
      drawChunkyLeaf(lx, ly, sl.rad + tumble * 0.45, 0.83);   // 0.83 = the ramp's brightest gold
    }
  }
}
