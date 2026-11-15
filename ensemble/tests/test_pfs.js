// Patternflow Ensemble - tests for console/pfs.js (a performance into a show, a show into another).
//
//   node ensemble/tests/test_pfs.js [<path to a Patternflow checkout>]
//
// With a checkout, the tables it carries as test vectors (docs/pfst-v2-vectors)
// are read, played, and written back byte for byte.
//
// License: MIT
'use strict';
const fs = require('fs');
const path = require('path');
const PFS = require('../console/pfs.js');

let checks = 0, failed = 0;
function check(ok, what) {
  checks++;
  if (!ok) { failed++; console.log('  FAIL ' + what); }
}
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const ramp = (points) => points.map(([t, v], i) => ({ t, v, ease: i + 1 < points.length }));

// ── The format, against the vectors Patternflow publishes ──────────────────
const checkout = process.argv[2];
const vectors = checkout ? path.join(checkout, 'docs', 'pfst-v2-vectors') : null;
if (vectors && fs.existsSync(vectors)) {
  const bytesA = new Uint8Array(fs.readFileSync(path.join(vectors, 'a-ease-ramp.pfs')));
  const a = PFS.read(bytesA);
  check(a && !a.loop && a.length === 500 && a.cues === 2 && a.tracks[0].length === 2, 'vector A reads');
  // spec §5: channel 1 from 0 to 1000 between 0.0 s and 3.0 s
  for (const [t, v] of [[0, 0], [5, 167], [10, 333], [15, 500], [20, 667], [29, 967], [30, 1000], [40, 1000]])
    check(PFS.at(a, t).param[0] === v, `vector A at ${t / 10}s is ${v} (got ${PFS.at(a, t).param[0]})`);
  // Every vector, read and written again: the same bytes.
  for (const name of ['a-ease-ramp.pfs', 'b-mixed-moment.pfs', 'c-chained.pfs']) {
    const bytes = new Uint8Array(fs.readFileSync(path.join(vectors, name)));
    const show = PFS.read(bytes);
    check(show != null, name + ' reads');
    check(same([...PFS.write(show)], [...bytes]), name + ' written back byte for byte');
  }
  const b = PFS.read(new Uint8Array(fs.readFileSync(path.join(vectors, 'b-mixed-moment.pfs'))));
  check(PFS.at(b, 10).param[1] === 250 && PFS.at(b, 10).param[0] === 0 && PFS.at(b, 25).param[0] === 1000 && PFS.at(b, 17).param[0] > 0,
        'vector B: one channel eases while another cuts, at the same moment');
} else {
  console.log('  (no Patternflow checkout given: the published vectors were not read)');
}

// ── Taking samples ─────────────────────────────────────────────────────────
{
  const r = PFS.recorder();
  r.sample(0, [500, 500, 500, 500], 'Origin');
  r.sample(0.4, [500, 500, 500, 500], 'Origin');                 // nothing new
  check(r.tracks[0].length === 1 && r.patterns.length === 1, 'a still panel is one point a knob');
  r.sample(50, [531, 500, 500, 500], 'Origin');                  // a detent, after five seconds
  check(same(r.tracks[0].map((p) => p.t), [0, 49, 50]) && r.tracks[0][1].v === 500, 'a move after a pause starts a tick before');
  r.sample(51, [562, 500, 500, 500], 'Origin');
  r.sample(51, [594, 500, 500, 500], 'Origin');                  // twice in a tick: the later one
  check(r.tracks[0].length === 4 && r.tracks[0][3].v === 594, 'two reports in one tick are one point');
  r.sample(80, [594, 500, 500, 500], 'patchbay');
  check(r.patterns.length === 2 && r.patterns[1].t === 80 && r.patterns[1].name === 'patchbay', 'a pattern change is an event');
  r.sample(90, [594, 500, 500, 500], '');                         // a pattern nobody could name: not an event
  check(r.patterns.length === 2, 'an unnamed pattern is left out');
  r.sample(95, [1200, -5, 500, 500], 'patchbay');
  check(r.tracks[0][r.tracks[0].length - 1].v === 1000 && r.tracks[1][r.tracks[1].length - 1].v === 0, 'values stay in 0..1000');
}

// ── Thinning ───────────────────────────────────────────────────────────────
{
  const line = [];
  for (let t = 0; t <= 100; t++) line.push({ t, v: t * 10 });
  check(PFS.simplify(line, 8).length === 2, 'a straight ramp is its two ends');
  const corner = line.map((p) => ({ t: p.t, v: p.t <= 50 ? p.t * 20 : 1000 }));
  const thin = PFS.simplify(corner, 8);
  check(thin.length === 3 && thin[1].t === 50, 'a corner keeps its corner');
  check(PFS.simplify([{ t: 0, v: 1 }], 8).length === 1 && PFS.simplify([], 8).length === 0, 'one point, or none');
}

// ── A whole recording: what the player shows is what was played ────────────
function worstMiss(show, tracks, length) {
  let worst = 0;
  tracks.forEach((track, k) => {
    for (let t = 0; t < length; t++) {
      let i = 0;
      while (i + 1 < track.length && track[i + 1].t <= t) i++;
      const a = track[i], b = track[i + 1];
      const want = b && b.t > a.t ? a.v + (b.v - a.v) * (t - a.t) / (b.t - a.t) : a.v;
      worst = Math.max(worst, Math.abs(PFS.at(show, t).param[k] - want));
    }
  });
  return worst;
}
let played;                       // the recording below, as a show: the edits further down start from it
{
  // Twenty seconds: K1 turned by hand in steps, K2 swept, K3 untouched, a pattern change.
  const r = PFS.recorder();
  let k1 = 500;
  for (let t = 0; t <= 200; t++) {
    if (t > 20 && t < 60 && t % 2 === 0) k1 = Math.min(1000, k1 + 31);
    if (t > 120 && t < 150) k1 = Math.max(0, k1 - 31);
    const k2 = Math.round(500 + 400 * Math.sin(t / 12));
    r.sample(t, [k1, k2, 250, 500], t < 100 ? 'Origin' : 'patchbay');
  }
  const made = PFS.build({ title: 'REC test', id: 'rec_test', length: 200, tracks: r.tracks, patterns: r.patterns });
  const show = PFS.read(made.bytes);
  played = show;
  check(show && show.loop && show.length === 200 && show.title === 'REC test' && show.id === 'rec_test', 'a recording becomes a looping table');
  check(show.cues === made.cues && made.cues <= PFS.MAX_CUES, 'within the cue limit');
  check(made.bytes.length === 76 + 1 + 'Origin'.length + 1 + 'patchbay'.length + 1 + made.cues * 16, 'header, pool and cues, nothing else');
  check(show.tracks.every((t) => t.length >= 1 && t[0].t === 0) && show.events[0].t === 0 && show.events[0].pattern === 'Origin', 'it opens by saying everything');
  const miss = worstMiss(show, r.tracks, 200);
  check(miss <= made.tolerance + 1, `the player is within ${made.tolerance} of what was played (worst ${miss})`);
  check(PFS.at(show, 50).pattern === 'Origin' && PFS.at(show, 150).pattern === 'patchbay', 'the pattern changes when it did');
  check(PFS.at(show, 205).param[2] === 250 && PFS.at(show, 200 + 50).pattern === 'Origin', 'and it loops');
  check(made.cues < 120, `a played knob does not take a cue a sample (${made.cues} cues for 200 ticks)`);
  check(same(PFS.patternsOf(show), ['Origin', 'patchbay']), 'the patterns it switches to, in order');
  check(same([...PFS.write(PFS.read(made.bytes))], [...made.bytes]), 'read and written again: the same bytes');
}
{
  // Ten busy minutes on every knob: it still fits, at a coarser curve.
  const r = PFS.recorder();
  for (let t = 0; t <= 6000; t++) {
    r.sample(t, [0, 1, 2, 3].map((k) => Math.round(500 + 450 * Math.sin(t / (5 + k * 3) + k))), 'Origin');
  }
  const made = PFS.build({ title: 'REC long', id: 'rec_long', length: 6000, tracks: r.tracks, patterns: r.patterns });
  check(made.cues <= PFS.MAX_CUES && made.tolerance > 8, `a long busy recording is thinned to fit (${made.cues} cues, tolerance ${Math.round(made.tolerance)})`);
  check(PFS.read(made.bytes).length === 6000, 'at its full length');
}
{
  // Nothing moved at all, and a recording cut short of a second.
  const r = PFS.recorder();
  r.sample(0, [300, 400, 500, 600], 'Origin');
  const made = PFS.build({ title: 'REC still', id: 'rec_still', length: 3, tracks: r.tracks, patterns: r.patterns });
  const show = PFS.read(made.bytes);
  check(show.cues === 1 && show.length === 10, 'a still second is one cue, one second long');
  check(same(PFS.at(show, 7).param, [300, 400, 500, 600]), 'holding what it was');
  const empty = PFS.build({ title: 'x', id: 'x', length: 50, tracks: [[], [], [], []], patterns: [] });
  check(PFS.read(empty.bytes).cues === 1, 'a recording with no samples still makes a table');
  check(PFS.read(new Uint8Array(10)) === null && PFS.read(new Uint8Array(200)) === null, 'what is not a table reads as none');
}

// ── Another show from one ──────────────────────────────────────────────────
{
  const show = played;
  const every = (fn) => { for (let t = 0; t < show.length; t++) if (!fn(t)) return false; return true; };
  // No changes: the same show.
  const copy = PFS.edit(show);
  check(every((t) => same(PFS.at(copy, t).param, PFS.at(show, t).param) && PFS.at(copy, t).pattern === PFS.at(show, t).pattern), 'edited with nothing: the same show');
  check(same([...PFS.write(copy)], [...PFS.write(show)]), 'and the same bytes');

  // Knobs 1 and 3 change places.
  const crossed = PFS.edit(show, { map: [2, 1, 0, 3] });
  check(every((t) => {
    const a = PFS.at(show, t).param, b = PFS.at(crossed, t).param;
    return b[0] === a[2] && b[2] === a[0] && b[1] === a[1] && b[3] === a[3];
  }), 'knobs 1 and 3 change places, the others stay');
  // One knob takes another's curve while the other keeps it; one is left alone; one is turned over.
  const odd = PFS.edit(show, { map: [1, 1, -1, 3], invert: [false, true, false, false] });
  check(every((t) => {
    const a = PFS.at(show, t).param, b = PFS.at(odd, t).param;
    return b[0] === a[1] && Math.abs(b[1] - (1000 - a[1])) <= 1 && b[2] === null && b[3] === a[3];   // a half rounds one way
  }), 'a knob copied, a knob turned over, a knob the show leaves alone');
  check(PFS.read(PFS.write(odd)).tracks[2].length === 0, 'a knob left alone is in no cue');

  // Twice as long is half as fast; half as long, twice.
  const slow = PFS.edit(show, { length: 400 });
  check(slow.length === 400 && [0, 30, 77, 150, 199].every((t) => {
    const a = PFS.at(show, t).param, b = PFS.at(slow, t * 2).param;
    return a.every((v, k) => Math.abs(v - b[k]) <= 1);
  }) && PFS.at(slow, 250).pattern === 'patchbay' && PFS.at(slow, 150).pattern === 'Origin', 'stretched to twice the length: the same moves at half the speed');
  const fast = PFS.edit(show, { length: 100 });
  check(fast.length === 100 && PFS.at(fast, 30).pattern === 'Origin' && PFS.at(fast, 60).pattern === 'patchbay' &&
        Math.abs(PFS.at(fast, 20).param[0] - PFS.at(show, 40).param[0]) <= 31, 'squeezed to half');
  check(PFS.cueCount(fast) <= PFS.cueCount(show), 'which needs no more cues');

  // A piece cut out of the middle: it opens with what was there, and ends where the move had got to.
  const piece = PFS.edit(show, { start: 30, end: 130 });
  check(piece.length === 100, 'a cut is as long as what was kept');
  check([0, 1, 25, 60, 99].every((t) => {
    const a = PFS.at(show, t + 30), b = PFS.at(piece, t);
    return a.pattern === b.pattern && a.param.every((v, k) => Math.abs(v - b.param[k]) <= 1);
  }), 'and plays what the show played there');
  check(PFS.at(piece, 0).pattern === 'Origin' && PFS.at(piece, 80).pattern === 'patchbay', 'with the pattern it was on, and the change inside it');
  const tail = PFS.edit(show, { start: 150 });
  check(tail.length === 50 && PFS.at(tail, 0).pattern === 'patchbay' && tail.events.length === 1, 'cut after the change: it opens on the later pattern');
  // Cut, then stretched.
  const both = PFS.edit(show, { start: 30, end: 130, length: 300 });
  check(both.length === 300 && Math.abs(PFS.at(both, 150).param[1] - PFS.at(show, 80).param[1]) <= 1, 'cut and stretched together');

  // Another pattern, or none.
  const other = PFS.edit(show, { patterns: { Origin: 'midnight_clocktower' } });
  check(PFS.at(other, 10).pattern === 'midnight_clocktower' && PFS.at(other, 150).pattern === 'patchbay', 'one pattern replaced by another');
  const bare = PFS.edit(show, { patterns: { Origin: '', patchbay: '' } });
  check(bare.events.length === 0 && PFS.at(bare, 150).pattern === null && every((t) => same(PFS.at(bare, t).param, PFS.at(show, t).param)),
        'no pattern at all: the knobs alone, on whatever is running');
  check(PFS.read(PFS.write(bare)).events.length === 0, 'and no pattern in the table');
  const onOne = PFS.edit(show, { pattern: 'slow_orbit' });
  check(onOne.events.length === 1 && PFS.at(onOne, 0).pattern === 'slow_orbit' && PFS.at(onOne, 150).pattern === 'slow_orbit' &&
        every((t) => same(PFS.at(onOne, t).param, PFS.at(show, t).param)), 'the whole show on one other pattern');
  const onNone = PFS.edit(show, { pattern: '' });
  check(onNone.events.length === 0 && PFS.read(PFS.write(onNone)).events.length === 0, 'or on none');
  const named = PFS.edit(show, { title: 'Take two', id: 'rec_two', loop: false });
  const back = PFS.read(PFS.write(named));
  check(back.title === 'Take two' && back.id === 'rec_two' && back.loop === false, 'a new name, and no loop');

  // Everything at once survives the table.
  const all = PFS.edit(show, { start: 20, end: 180, length: 240, map: [2, 0, 1, -1], invert: [true, false, false, false], patterns: { patchbay: 'slow_orbit' } });
  const again = PFS.read(PFS.write(all));
  check([0, 13, 99, 200, 239].every((t) => same(PFS.at(again, t).param, PFS.at(all, t).param) && PFS.at(again, t).pattern === PFS.at(all, t).pattern),
        'every change at once, written and read back');
  check(PFS.cueCount(all) <= PFS.MAX_CUES, 'within the cue limit');
}
{
  // A show that steps (no ease) keeps its steps through an edit and the table.
  const steps = { title: 's', id: 's', length: 100, loop: true, events: [{ t: 0, pattern: 'Origin', message: null }],
                  tracks: [[{ t: 0, v: 100, ease: false }, { t: 50, v: 900, ease: false }], ramp([[0, 0], [100, 1000]]), [], []] };
  const back = PFS.read(PFS.write(steps));
  check(PFS.at(back, 49).param[0] === 100 && PFS.at(back, 50).param[0] === 900 && PFS.at(back, 49).param[1] === 490, 'a step beside a ramp, each as it is');
  const cut = PFS.edit(steps, { start: 25, end: 75 });
  check(PFS.at(cut, 0).param[0] === 100 && PFS.at(cut, 24).param[0] === 100 && PFS.at(cut, 25).param[0] === 900, 'a step inside a cut is still a step');
  check(PFS.at(cut, 0).param[1] === 250 && PFS.at(cut, 49).param[1] === 740 && cut.tracks[1][cut.tracks[1].length - 1].v === 750, 'a ramp cut at both ends keeps its slope');
  let threw = false;
  try { PFS.write({ title: '', id: '', length: 5000, loop: true, events: [], tracks: [Array.from({ length: 300 }, (_, i) => ({ t: i * 10, v: i % 2 ? 0 : 1000, ease: false })), [], [], []] }); }
  catch (_) { threw = true; }
  check(threw, 'more cues than a table holds is refused, not cut short');
}

console.log(`${failed ? 'FAIL' : 'PASS'}  ${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
