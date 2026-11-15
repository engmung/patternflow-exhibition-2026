// Patternflow Ensemble - a performance into a show, and a show into another.
//
// A show is a .pfs table (Patternflow docs/pfst-v2-spec.md): a timeline of
// cues, each setting a pattern and any of four knob channels (0..1000) at a
// time in tenths of a second, optionally easing a channel towards its next
// cue. A panel's player loops one by itself.
//
// Here a show is held as what it means rather than as its cues:
//
//   { title, id, length, loop,
//     tracks: four lists of { t, v, ease }   a channel's points, in time order
//     events: [{ t, pattern, message }] }    what happens at a moment
//
// (`ease`: from this point the channel moves in a line to its next one;
// without it the channel stays here until the next one.)
//
//   PFS.recorder()            takes a panel's samples as they come
//   PFS.build(recording)      a recording as a show's bytes, thinned to fit
//   PFS.read(bytes)           a table -> a show            (null: not one)
//   PFS.write(show)           a show  -> a table's bytes   (throws if it cannot fit)
//   PFS.edit(show, changes)   another show: cut, stretched, its knobs
//                             crossed or turned over, its patterns replaced
//   PFS.at(show, t)           what a player shows at a time: the tests' judge
//
// No page here: the console (app.js) feeds it, Node runs its tests
// (tests/test_pfs.js).
//
// License: MIT
(function (root) {
  'use strict';

  const HEADER = 76, CUE = 16, MAX_CUES = 256, MAX_POOL = 4096, NONE = 0xFFFF;
  const F_PATTERN = 1, F_PARAM = 2, F_MESSAGE = 32, F_EASE = 64;      // F_PARAM << channel
  const CHANNELS = 4, VALUE_MAX = 1000;
  const MAX_TICKS = 6000;                              // ten minutes: a loop, not an evening
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

  // ── Taking the samples ───────────────────────────────────────────────────
  // Times are in ticks (tenths of a second) from the start. A knob is where
  // it was until the report that says otherwise, so a change after a pause
  // is written as "still there a tick ago, here now": the move takes the
  // tick it took, and not the whole pause.
  function recorder() {
    const tracks = [[], [], [], []];
    const patterns = [];
    const last = [null, null, null, null];
    let lastPattern = null;
    return {
      tracks, patterns,
      // knobs: four positions 0..1000. pattern: the name a player finds it by ('' or null: not known).
      sample(t, knobs, pattern) {
        t = clamp(Math.round(t), 0, MAX_TICKS);
        for (let k = 0; k < CHANNELS; k++) {
          const v = clamp(Math.round(knobs[k]), 0, VALUE_MAX);
          if (v === last[k]) continue;
          const track = tracks[k];
          const prev = track[track.length - 1];
          if (prev && prev.t === t) prev.v = v;                    // twice in one tick: the later one
          else {
            if (prev && t - prev.t > 1) track.push({ t: t - 1, v: prev.v });
            track.push({ t, v });
          }
          last[k] = v;
        }
        if (pattern && pattern !== lastPattern) {
          const prev = patterns[patterns.length - 1];
          if (prev && prev.t === t) prev.name = pattern; else patterns.push({ t, name: pattern });
          lastPattern = pattern;
        }
      },
    };
  }

  // ── Fewer points, the same curve ─────────────────────────────────────────
  // The points a line through the others would miss by more than `tol`
  // (measured in value, at each point's own time). The first and the last
  // always stay.
  function simplify(points, tol) {
    if (points.length <= 2) return points.slice();
    const keep = new Array(points.length).fill(false);
    keep[0] = keep[points.length - 1] = true;
    const stack = [[0, points.length - 1]];
    while (stack.length) {
      const [a, b] = stack.pop();
      const A = points[a], B = points[b];
      let worst = -1, at = -1;
      for (let i = a + 1; i < b; i++) {
        const line = A.v + (B.v - A.v) * (points[i].t - A.t) / (B.t - A.t);
        const miss = Math.abs(points[i].v - line);
        if (miss > worst) { worst = miss; at = i; }
      }
      if (worst > tol) { keep[at] = true; stack.push([a, at], [at, b]); }
    }
    return points.filter((_, i) => keep[i]);
  }

  // ── A show's cues ────────────────────────────────────────────────────────
  // At each moment: one cue that eases and one that does not, each with the
  // points there that ask for it. (A channel's last point may ask for either:
  // nothing follows it, so it holds whatever the flag says.)
  function cuesOf(show) {
    const moments = new Map();                          // t -> { ease: cue, plain: cue }
    const at = (t) => {
      if (!moments.has(t)) moments.set(t, { ease: null, plain: null });
      return moments.get(t);
    };
    const cue = (t, flags) => ({ t, flags, pattern: null, message: null, param: [0, 0, 0, 0] });
    show.tracks.forEach((track, k) => {
      for (const point of track) {
        const m = at(point.t);
        const which = point.ease ? 'ease' : 'plain';
        if (!m[which]) m[which] = cue(point.t, point.ease ? F_EASE : 0);
        m[which].flags |= F_PARAM << k;
        m[which].param[k] = point.v;
      }
    });
    for (const e of show.events || []) {
      const m = at(e.t);
      if (!m.plain && !m.ease) m.plain = cue(e.t, 0);
      const c = m.plain || m.ease;
      if (e.pattern) { c.flags |= F_PATTERN; c.pattern = e.pattern; }
      if (e.message) { c.flags |= F_MESSAGE; c.message = e.message; }
    }
    const cues = [];
    for (const t of [...moments.keys()].sort((a, b) => a - b)) {
      const m = moments.get(t);
      // The one that eases first: a plain cue after it at the same moment ends nothing of it.
      if (m.ease) cues.push(m.ease);
      if (m.plain && (m.plain.flags & ~F_EASE)) cues.push(m.plain);
    }
    return cues;
  }

  // A show -> the table's bytes.
  function write(show) {
    const cues = cuesOf(show);
    const pool = [0];                                   // offset 0 is the empty string
    const offsets = new Map();
    const enc = new TextEncoder();
    const offsetOf = (text) => {
      if (!offsets.has(text)) {
        offsets.set(text, pool.length);
        for (const b of enc.encode(text)) pool.push(b);
        pool.push(0);
      }
      return offsets.get(text);
    };
    const packed = cues.map((c) => ({ ...c, patternOff: c.pattern ? offsetOf(c.pattern) : NONE, messageOff: c.message ? offsetOf(c.message) : NONE }));
    if (cues.length > MAX_CUES) throw new Error(`큐가 ${cues.length}개입니다. 쇼 하나에는 ${MAX_CUES}개까지 들어갑니다`);
    if (pool.length > MAX_POOL) throw new Error('패턴 이름이 너무 많습니다');
    const length = Math.round(show.length);
    if (!(length >= 1 && length <= 0xFFFF)) throw new Error('길이가 범위를 벗어났습니다');

    const out = new Uint8Array(HEADER + pool.length + cues.length * CUE);
    const view = new DataView(out.buffer);
    out.set([0x50, 0x46, 0x53, 0x54, 2, show.loop === false ? 0 : 1]);   // "PFST", version 2, flags
    view.setUint16(6, length, true);
    view.setUint16(8, cues.length, true);
    view.setUint16(10, pool.length, true);
    out.set(enc.encode(show.title || '').subarray(0, 32), 12);
    out.set(enc.encode(show.id || '').subarray(0, 32), 44);
    out.set(pool, HEADER);
    packed.forEach((c, i) => {
      const at = HEADER + pool.length + i * CUE;
      view.setUint16(at, c.t, true);
      out[at + 2] = c.flags;
      view.setUint16(at + 4, c.patternOff, true);
      for (let k = 0; k < CHANNELS; k++) view.setUint16(at + 6 + k * 2, c.param[k], true);
      view.setUint16(at + 14, c.messageOff, true);
    });
    return out;
  }
  const cueCount = (show) => cuesOf(show).length;

  // A recording -> { bytes, cues, tolerance, show }. The curve is thinned
  // until the table holds it: first barely (under a hundredth of a knob's
  // travel), then as much as it takes.
  function build({ title, id, length, loop = true, tracks, patterns }) {
    length = clamp(Math.round(length), 10, MAX_TICKS);
    const within = (list) => list.filter((p) => p.t <= length);
    // The table opens by saying everything: where each knob is, and the pattern.
    const whole = tracks.map((track) => {
      const t = within(track);
      return t.length ? t : [{ t: 0, v: VALUE_MAX / 2 }];
    });
    const events = within(patterns || []).map((p) => ({ t: p.t, pattern: p.name }));
    for (let tol = 8; ; tol *= 1.6) {
      const show = { title, id, length, loop, events,
                     tracks: whole.map((track) => simplify(track, tol).map((p) => ({ t: p.t, v: p.v, ease: true }))) };
      const cues = cueCount(show);
      if (cues <= MAX_CUES) return { bytes: write(show), cues, tolerance: tol, show };
      if (tol > VALUE_MAX) throw new Error('이 녹화는 쇼 하나에 담기에 너무 깁니다');
    }
  }

  // ── Reading a table ──────────────────────────────────────────────────────
  function read(bytes) {
    if (!bytes || bytes.length < HEADER || String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) !== 'PFST') return null;
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const version = bytes[4];
    if (version !== 1 && version !== 2) return null;
    const count = view.getUint16(8, true), poolBytes = view.getUint16(10, true);
    if (count > MAX_CUES || poolBytes > MAX_POOL || bytes.length < HEADER + poolBytes + count * CUE) return null;
    const dec = new TextDecoder();
    const text = (at, max) => { let end = at; while (end < at + max && bytes[end]) end++; return dec.decode(bytes.subarray(at, end)); };
    const str = (off) => (off === NONE || off >= poolBytes ? null : text(HEADER + off, poolBytes - off) || null);
    const scale = version === 2 ? 1 : 10;               // a v1 table counts seconds
    const show = { title: text(12, 32), id: text(44, 32), length: view.getUint16(6, true) * scale, loop: !!(bytes[5] & 1),
                   tracks: [[], [], [], []], events: [], cues: count };
    for (let i = 0; i < count; i++) {
      const at = HEADER + poolBytes + i * CUE;
      const t = view.getUint16(at, true) * scale, flags = bytes[at + 2];
      for (let k = 0; k < CHANNELS; k++) {
        if (!(flags & (F_PARAM << k))) continue;
        const track = show.tracks[k];
        const point = { t, v: clamp(view.getUint16(at + 6 + k * 2, true), 0, VALUE_MAX), ease: version === 2 && !!(flags & F_EASE) };
        // Twice at one moment: the first has no time to be anything, the later one is what holds.
        if (track.length && track[track.length - 1].t === t) track[track.length - 1] = point; else track.push(point);
      }
      const pattern = flags & F_PATTERN ? str(view.getUint16(at + 4, true)) : null;
      const message = flags & F_MESSAGE ? str(view.getUint16(at + 14, true)) : null;
      if (pattern || message) show.events.push({ t, pattern, message });
    }
    return show;
  }

  // A channel at a time: null before its first point.
  function valueAt(track, t) {
    let i = -1;
    while (i + 1 < track.length && track[i + 1].t <= t) i++;
    if (i < 0) return null;
    const p = track[i], next = track[i + 1];
    if (p.ease && next && next.t > p.t && t > p.t) return Math.floor(p.v + (next.v - p.v) * (t - p.t) / (next.t - p.t) + 0.5);
    return p.v;
  }

  // What a player shows at `t` ticks into a show: { pattern, param[4] }.
  function at(show, t) {
    const now = show.loop ? t % show.length : t;
    let pattern = null;
    for (const e of show.events) if (e.t <= now && e.pattern) pattern = e.pattern;
    return { pattern, param: show.tracks.map((track) => valueAt(track, now)) };
  }

  // The names of the patterns a show switches to, in the order it first does.
  function patternsOf(show) {
    const out = [];
    for (const e of show.events) if (e.pattern && !out.includes(e.pattern)) out.push(e.pattern);
    return out;
  }

  // ── Another show from one ────────────────────────────────────────────────
  // changes (all optional), applied in this order:
  //   start, end   ticks of the show to keep (the rest is cut away)
  //   length       ticks the kept part is to last: longer is slower
  //   map          for each knob, the channel it takes (0..3), or -1: the show leaves that knob alone
  //   invert       for each knob, whether it is turned over (1000 - value)
  //   patterns     { name: another name, or '' for "do not switch" }
  //   pattern      one pattern for the whole show (it opens on it and switches to no other),
  //                or '' for none: the knobs alone, on whatever the panel is running
  //   title, id, loop
  function edit(show, changes = {}) {
    const start = clamp(Math.round(changes.start || 0), 0, show.length - 1);
    const end = clamp(Math.round(changes.end == null ? show.length : changes.end), start + 1, show.length);
    const span = end - start;
    const length = clamp(Math.round(changes.length || span), 1, 0xFFFF);
    const scale = (t) => clamp(Math.round((t - start) * length / span), 0, length);

    const cut = show.tracks.map((track) => {
      const out = [];
      const first = valueAt(track, start);
      if (first != null) {
        // Where the channel is when the cut begins, and still on its way if it was.
        let i = -1;
        while (i + 1 < track.length && track[i + 1].t <= start) i++;
        const next = track[i + 1];
        // (A point that sits exactly there is taken as it is.)
        out.push({ t: start, v: first, ease: track[i].t === start ? track[i].ease : !!(track[i].ease && next && next.t > start) });
      }
      for (let i = 0; i < track.length; i++) {
        const p = track[i];
        if (p.t <= start || p.t > end) continue;
        out.push({ t: p.t, v: p.v, ease: p.ease });
      }
      // On its way past the end: it arrives where it would have been by then.
      const last = out[out.length - 1];
      if (last && last.ease && last.t < end) {
        const beyond = track.find((p) => p.t > end);
        if (beyond) out.push({ t: end, v: valueAt(track, end), ease: false });
      }
      return out;
    });

    const map = changes.map || [0, 1, 2, 3];
    const invert = changes.invert || [false, false, false, false];
    const tracks = [0, 1, 2, 3].map((k) => {
      const from = map[k];
      if (!(from >= 0 && from < CHANNELS)) return [];
      const out = [];
      for (const p of cut[from]) {
        const point = { t: scale(p.t), v: invert[k] ? VALUE_MAX - p.v : p.v, ease: p.ease };
        if (out.length && out[out.length - 1].t === point.t) out[out.length - 1] = point; else out.push(point);
      }
      return out;
    });

    const swap = changes.patterns || {};
    const named = (name) => (name != null && Object.prototype.hasOwnProperty.call(swap, name) ? swap[name] || null : name);
    const events = [];
    // The pattern in force when the cut begins opens the new show.
    let opening = null;
    for (const e of show.events) if (e.t <= start && e.pattern) opening = e.pattern;
    if (named(opening)) events.push({ t: 0, pattern: named(opening), message: null });
    for (const e of show.events) {
      if (e.t <= start || e.t >= end) continue;         // one at the very end would never be reached
      const pattern = named(e.pattern);
      if (pattern || e.message) events.push({ t: scale(e.t), pattern, message: e.message || null });
    }
    if (changes.pattern != null) {
      const banners = events.filter((e) => e.message).map((e) => ({ t: e.t, pattern: null, message: e.message }));
      events.length = 0;
      if (changes.pattern) events.push({ t: 0, pattern: changes.pattern, message: null });
      events.push(...banners);
    }
    return { title: changes.title != null ? changes.title : show.title, id: changes.id != null ? changes.id : show.id,
             length, loop: changes.loop != null ? changes.loop : show.loop, tracks, events };
  }

  const api = { recorder, simplify, build, read, write, edit, at, valueAt, patternsOf, cueCount, MAX_CUES, MAX_TICKS, VALUE_MAX };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PFS = api;
})(typeof window !== 'undefined' ? window : globalThis);
