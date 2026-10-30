/**
 * Smoke checks against docs/pfst-v2-proposal.md (implementation spec) §5.
 * Run: node --input-type=module director/js/pfst-v2-smoke.js
 */
import {
  normalizePerformance,
  stateAt,
  PFST_VERSION_2
} from "./performance.js";
import { encodeShowTable, decodeShowTable } from "./show-table.js";

function hex(s) {
  return new Uint8Array(
    s
      .trim()
      .split(/\s+/)
      .map((b) => parseInt(b, 16))
  );
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function bytesEqual(a, b, label) {
  assert(a.length === b.length, label + " length " + a.length + " vs " + b.length);
  for (let i = 0; i < a.length; i++) {
    assert(a[i] === b[i], label + " byte " + i + ": " + a[i] + " vs " + b[i]);
  }
}

// ── Vector A: one eased ramp ──────────────────────────────────────────────
const perfA = normalizePerformance({
  version: 2,
  id: "a-ease-ramp",
  title: "a-ease-ramp",
  length: 50,
  loop: false,
  timeline: [
    { t: 0, param: [0, null, null, null], ease: true },
    { t: 3, param: [1000, null, null, null] }
  ]
});
const encA = encodeShowTable(perfA);
assert(encA[4] === 2, "A version");
assert(encA.length === 109, "A size " + encA.length);
// cues at 77 (header 76 + pool 1)
assert(encA[77] === 0 && encA[78] === 0, "A cue0 t");
assert(encA[79] === 0x42, "A cue0 flags PARAM1|EASE");
assert(encA[93] === 0x1e && encA[94] === 0, "A cue1 t=30");
assert(encA[95] === 0x02, "A cue1 flags PARAM1");

const expectA = [
  [0, 0],
  [0.5, 167],
  [1.0, 333],
  [1.5, 500],
  [2.0, 667],
  [2.9, 967],
  [3.0, 1000],
  [4.0, 1000]
];
for (const [t, v] of expectA) {
  const got = stateAt(perfA, t).param[0];
  assert(
    Math.abs(got - v) <= 1,
    "A @" + t + "s expected " + v + " got " + got
  );
}
bytesEqual(encodeShowTable(decodeShowTable(encA)), encA, "A round-trip");
const backA = decodeShowTable(encA);
assert(backA.version === PFST_VERSION_2, "A decode version");
assert(backA.timeline.some((c) => c.ease), "A ease flag");

// ── Vector B: mixed moment (ease + jump at same t) ────────────────────────
const perfB = normalizePerformance({
  version: 2,
  id: "b-mixed",
  title: "b-mixed",
  length: 50,
  timeline: [
    { t: 1.0, param: [0, null, null, null], ease: true },
    { t: 1.0, param: [null, 250, null, null] },
    { t: 2.5, param: [1000, null, null, null] }
  ]
});
// Preserve author order: ease cue before hold at same t (do not re-sort ease last)
assert(perfB.timeline[0].ease === true, "B first cue ease");
assert(perfB.timeline[1].param[1] === 250, "B second cue P2");
const midB = stateAt(perfB, 1.75);
assert(Math.abs(midB.param[0] - 500) <= 1, "B P1 mid " + midB.param[0]);
assert(midB.param[1] === 250, "B P2 holds 250");
assert(stateAt(perfB, 1.0).param[1] === 250, "B P2 at fire");
assert(stateAt(perfB, 3.0).param[1] === 250, "B P2 never eases");
// P2 must never take intermediate values during P1's ramp
for (const t of [1.1, 1.5, 2.0, 2.4]) {
  assert(stateAt(perfB, t).param[1] === 250, "B P2 @" + t);
}
// ── Vector C: chained eases ───────────────────────────────────────────────
const perfC = normalizePerformance({
  version: 2,
  id: "c-chained",
  title: "c-chained",
  length: 50,
  timeline: [
    { t: 0, param: [0, null, null, null], ease: true },
    { t: 2, param: [800, null, null, null], ease: true },
    { t: 4, param: [200, null, null, null] }
  ]
});
assert(Math.abs(stateAt(perfC, 1.0).param[0] - 400) <= 1, "C up mid");
assert(stateAt(perfC, 2.0).param[0] === 800, "C peak");
assert(Math.abs(stateAt(perfC, 3.0).param[0] - 500) <= 1, "C down mid");
assert(stateAt(perfC, 4.0).param[0] === 200, "C end");

// Reject unknown version
let rejected = false;
try {
  decodeShowTable(hex("50 46 53 54 03 00 00 00 00 00 01 00"));
} catch (_) {
  rejected = true;
}
assert(rejected, "reject version 3");

console.log("pfst-v2-smoke OK (spec §5 vectors A/B/C + round-trip)");
