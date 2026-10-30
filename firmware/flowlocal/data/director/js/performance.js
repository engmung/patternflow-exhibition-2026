/**
 * In-memory performance model (timeline cues).
 * On disk the Director Open/Saves PFST (.pfs) — see show-table.js.
 *
 * PFST v2 (docs/pfst-v2-proposal.md): times on a 0.1 s grid, optional per-cue
 * `ease` so param channels lerp to their next cue. New shows default to v2;
 * v1 files still open and re-save byte-compatibly.
 */

export const PFST_VERSION = 1;
export const PFST_VERSION_2 = 2;

/** Snap cue times: whole seconds (v1) or 0.1 s (v2). */
export function quantizeTime(t, version = PFST_VERSION_2) {
  const raw = Number(t);
  if (!Number.isFinite(raw) || raw < 0) return 0;
  if (Number(version) === PFST_VERSION_2) {
    return Math.round(raw * 10) / 10;
  }
  return Math.round(raw);
}

export function emptyPerformance() {
  return {
    version: PFST_VERSION_2,
    id: "untitled-" + new Date().toISOString().slice(0, 10),
    title: "Untitled",
    utcStart: "",
    channel: 0,
    length: 60,
    loop: false,
    patternsZip: "",
    patternsZipSha256: "",
    required: ["Origin"],
    timeline: [
      { t: 0, pattern: "Origin" },
      { t: 0, param: [500, 500, 500, 500] }
    ]
  };
}

export function normalizePerformance(raw) {
  const base = emptyPerformance();
  if (!raw || typeof raw !== "object") return base;
  const out = { ...base, ...raw };
  out.version =
    Number(out.version) === PFST_VERSION_2 ? PFST_VERSION_2 : PFST_VERSION;
  const deciseconds = out.version === PFST_VERSION_2;
  out.id = String(out.id || base.id);
  out.title = String(out.title || base.title);
  out.utcStart = out.utcStart == null ? "" : String(out.utcStart);
  out.channel = Number(out.channel) || 0;
  out.length = deciseconds
    ? Math.max(0.1, Math.round((Number(out.length) || 60) * 10) / 10)
    : Math.max(1, Math.round(Number(out.length) || 60));
  out.loop = !!out.loop;
  out.patternsZip = out.patternsZip == null ? "" : String(out.patternsZip);
  out.patternsZipSha256 =
    out.patternsZipSha256 == null ? "" : String(out.patternsZipSha256);
  out.required = Array.isArray(out.required)
    ? out.required.map(String)
    : base.required;
  out.timeline = Array.isArray(out.timeline)
    ? out.timeline
        .map((cue) => normalizeCue(cue, out.version))
        .filter(Boolean)
        .sort(compareCues)
    : base.timeline;
  return out;
}

function cueKindOrder(cue) {
  if (cue.pattern != null) return 0;
  if (cue.param) return 1;
  if (cue.message != null) return 2;
  return 3;
}

export function compareCues(a, b) {
  if (a.t !== b.t) return a.t - b.t;
  // Same-t cues keep table order by kind (pattern → param → message). Ease
  // vs hold at one tick must stay as separate cues; do not reorder them —
  // the player fires in file order (spec §2.3 / vector B).
  return cueKindOrder(a) - cueKindOrder(b);
}

/**
 * Parse param field into length-4 sparse array (null = not set on this cue).
 * Accepts [n,n,n,n], sparse array with nulls, or { "1": n, "2": n } (1-based).
 */
export function parseParamField(raw) {
  const out = [null, null, null, null];
  if (Array.isArray(raw)) {
    for (let i = 0; i < 4 && i < raw.length; i++) {
      if (raw[i] == null || raw[i] === "") continue;
      out[i] = clamp1000(raw[i]);
    }
  } else if (raw && typeof raw === "object") {
    for (const [k, v] of Object.entries(raw)) {
      if (v == null || v === "") continue;
      let i = Number(k);
      if (!Number.isFinite(i)) continue;
      if (i >= 1 && i <= 4) i -= 1;
      else if (i < 0 || i > 3) continue;
      out[i] = clamp1000(v);
    }
  } else {
    return null;
  }
  if (out.every((x) => x == null)) return null;
  return out;
}

/** Merge sparse patches; values from `patch` win when non-null. */
export function mergeParamSparse(base, patch) {
  const out = [null, null, null, null];
  for (let i = 0; i < 4; i++) {
    if (patch && patch[i] != null) out[i] = clamp1000(patch[i]);
    else if (base && base[i] != null) out[i] = clamp1000(base[i]);
  }
  return out.every((x) => x == null) ? null : out;
}

/** JSON-facing param: full array if all set, else 1-based object. */
export function serializeParam(param) {
  if (!param) return undefined;
  const dense = param.every((v) => v != null);
  if (dense) return param.map((n) => clamp1000(n));
  const obj = {};
  for (let i = 0; i < 4; i++) {
    if (param[i] != null) obj[String(i + 1)] = clamp1000(param[i]);
  }
  return obj;
}

export function serializePerformance(perf) {
  const p = normalizePerformance(perf);
  return {
    version: p.version,
    id: p.id,
    title: p.title,
    utcStart: p.utcStart,
    channel: p.channel,
    length: p.length,
    loop: p.loop,
    patternsZip: p.patternsZip,
    patternsZipSha256: p.patternsZipSha256,
    required: p.required,
    timeline: (p.timeline || []).map((cue) => {
      const out = { t: cue.t };
      if (cue.pattern != null) out.pattern = cue.pattern;
      if (cue.message != null) out.message = cue.message;
      if (cue.ease === true) out.ease = true;
      if (cue.param) {
        out.param = serializeParam(cue.param);
      }
      return out;
    })
  };
}

function normalizeCue(cue, version) {
  if (!cue || typeof cue !== "object") return null;
  const t = quantizeTime(cue.t, version);
  const out = { t };
  if (cue.pattern != null && String(cue.pattern).length) {
    out.pattern = String(cue.pattern);
  }
  const param = parseParamField(cue.param);
  if (param) {
    out.param = param;
  }
  if (cue.message != null) {
    out.message = String(cue.message);
  }
  if (version === PFST_VERSION_2 && cue.ease === true && out.param) {
    out.ease = true;
  }
  if (out.pattern == null && out.param == null && out.message == null) {
    return null;
  }
  return out;
}

export function clamp1000(n) {
  let v = Math.round(Number(n));
  if (!Number.isFinite(v)) v = 500;
  if (v < 0) v = 0;
  if (v > 1000) v = 1000;
  return v;
}

/**
 * Display names actually planted on the timeline (not the Required shortlist).
 */
export function timelinePatternNames(perf) {
  const seen = Object.create(null);
  const out = [];
  for (const cue of perf.timeline || []) {
    const name = String(cue.pattern || "").trim();
    if (!name) continue;
    const key = name.toLowerCase();
    if (seen[key]) continue;
    seen[key] = true;
    out.push(name);
  }
  return out;
}

/**
 * Timeline extent in seconds. Authoring length wins; never shorter than last cue.
 */
export function durationOf(perf) {
  let maxCue = 0;
  for (const c of perf.timeline || []) {
    if (c.t > maxCue) maxCue = c.t;
  }
  const len = Math.max(
    0.1,
    Number(perf.length) || 60
  );
  return Math.max(len, maxCue);
}

/** Clamp a time into [0, duration] on the show's grid. */
export function clampToDuration(perf, t) {
  const d = durationOf(perf);
  const n = quantizeTime(t, perf.version);
  if (n > d) return quantizeTime(d, perf.version);
  return n;
}

/**
 * Last defined value for a channel at time t.
 * PFST v2 EASE (§3.3): for each channel an eased cue sets, lerp from that
 * cue's value to that channel's next cue — arrive exactly when the next
 * cue fires. Endpoint rule: at/after t1 the terminating cue owns the value
 * (no interpolate-to-end); same-t next → no segment; no later cue → hold.
 */
function paramChannelAt(cues, channel, t) {
  let prev = null;
  let next = null;
  for (const c of cues) {
    if (!c.param || c.param[channel] == null) continue;
    if (c.t <= t) prev = c;
    else {
      next = c;
      break;
    }
  }
  if (!prev) {
    for (const c of cues) {
      if (c.param && c.param[channel] != null) return c.param[channel];
    }
    return 500;
  }
  const from = prev.param[channel];
  // Strictly between prev and next, and only if prev armed ease with a
  // later (not same-t) cue that sets this channel.
  if (
    prev.ease &&
    next &&
    next.t > prev.t &&
    t > prev.t &&
    t < next.t
  ) {
    const u = (t - prev.t) / (next.t - prev.t);
    const to = next.param[channel];
    // Device: long(v0 + (v1-v0)*u + 0.5f) — half-up for positive wire values.
    return clamp1000(from + (to - from) * u);
  }
  return from;
}

/**
 * Composed snapshot at time t: fold all cues with cue.t <= t.
 * Pattern/message last-write-wins; params hold or ease per cue flag.
 */
export function stateAt(perf, t) {
  const cues = perf.timeline || [];
  let pattern = "";
  let message = "";

  for (const c of cues) {
    if (c.t > t) break;
    if (c.pattern != null) pattern = c.pattern;
    if (c.message != null) message = c.message;
  }

  const param = [
    paramChannelAt(cues, 0, t),
    paramChannelAt(cues, 1, t),
    paramChannelAt(cues, 2, t),
    paramChannelAt(cues, 3, t)
  ];

  return { pattern, message, param, t };
}

/** Short label for cue list / chips. */
export function formatParamSparse(param) {
  if (!param) return "";
  const parts = [];
  for (let i = 0; i < 4; i++) {
    if (param[i] != null) parts.push("P" + (i + 1) + "=" + param[i]);
  }
  return parts.join(" ");
}

/**
 * Promote a v1 (or mixed) performance to v2 authoring: 0.1 s grid, keep holds.
 * Does not invent ease flags — mark those in the cue editor.
 */
export function upgradePerformanceToV2(perf) {
  const raw = serializePerformance(perf);
  raw.version = PFST_VERSION_2;
  return normalizePerformance(raw);
}

/**
 * Collapse dense same-channel staircases into sparse eased endpoints.
 * Useful when importing an old v1 bake (one cue per second). Linear pieces
 * only — bezier authoring stays in Pattern Lab.
 */
export function compressStaircaseToEase(perf) {
  const p = upgradePerformanceToV2(perf);
  const cues = p.timeline || [];
  const keep = [];

  // Pass 1: collect per-channel keyframes (t, v) in order.
  const lanes = [[], [], [], []];
  for (const cue of cues) {
    if (!cue.param) continue;
    for (let ch = 0; ch < 4; ch++) {
      if (cue.param[ch] == null) continue;
      lanes[ch].push({ t: cue.t, v: cue.param[ch] });
    }
  }

  // Pass 2: for each lane, drop intermediate collinear samples on a 1 Hz
  // staircase (Δt≈1, constant Δv). Keep endpoints; ease the starts.
  const easedPoints = new Map(); // key `${t10}:${ch}` → { t, ch, v, ease }
  for (let ch = 0; ch < 4; ch++) {
    const pts = lanes[ch];
    if (!pts.length) continue;
    let i = 0;
    while (i < pts.length) {
      let j = i + 1;
      while (
        j < pts.length &&
        Math.abs(pts[j].t - pts[j - 1].t - 1) < 0.05 &&
        pts[j].v !== pts[j - 1].v
      ) {
        // Extend while roughly 1 Hz and still moving (any direction).
        const dv0 = pts[i + 1] ? pts[i + 1].v - pts[i].v : 0;
        const dv = pts[j].v - pts[j - 1].v;
        if (j > i + 1 && Math.sign(dv) !== Math.sign(dv0) && dv !== 0) break;
        j++;
      }
      if (j - i >= 3) {
        // Long staircase → start (ease) + end (hold).
        const a = pts[i];
        const b = pts[j - 1];
        easedPoints.set(Math.round(a.t * 10) + ":" + ch, {
          t: a.t,
          ch,
          v: a.v,
          ease: true
        });
        easedPoints.set(Math.round(b.t * 10) + ":" + ch, {
          t: b.t,
          ch,
          v: b.v,
          ease: false
        });
        i = j;
      } else {
        for (let k = i; k < j; k++) {
          easedPoints.set(Math.round(pts[k].t * 10) + ":" + ch, {
            t: pts[k].t,
            ch,
            v: pts[k].v,
            ease: false
          });
        }
        i = j;
      }
    }
  }

  // Rebuild: keep pattern/message cues; rebuild param cues from easedPoints.
  for (const cue of cues) {
    if (cue.pattern != null || cue.message != null) {
      const copy = { t: cue.t };
      if (cue.pattern != null) copy.pattern = cue.pattern;
      if (cue.message != null) copy.message = cue.message;
      // Drop dense params from hybrid cues — they get rebuilt below.
      keep.push(copy);
    }
  }

  const byKey = new Map(); // `${t10}:${ease}` → sparse param cue
  for (const pt of easedPoints.values()) {
    const key = Math.round(pt.t * 10) + ":" + (pt.ease ? 1 : 0);
    let e = byKey.get(key);
    if (!e) {
      e = {
        t: quantizeTime(pt.t, PFST_VERSION_2),
        param: [null, null, null, null],
        ease: pt.ease
      };
      byKey.set(key, e);
    }
    e.param[pt.ch] = pt.v;
  }
  for (const e of byKey.values()) {
    if (e.ease) keep.push({ t: e.t, param: e.param, ease: true });
    else keep.push({ t: e.t, param: e.param });
  }

  p.timeline = keep
    .map((c) => normalizeCue(c, PFST_VERSION_2))
    .filter(Boolean)
    .sort(compareCues);
  return p;
}
