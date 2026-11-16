// Patternflow Ensemble - tests for console/graph.js: a node graph compiled
// here, run by the hub's own code.
//
//   node ensemble/tests/test_graph.js
//
// The hub and the panels are the console's demo module (console/pfe_sim.wasm.js,
// built from ensemble/core/ by tools/build_wasm.py): the same program a real
// hub runs, with pretend panels around it.
//
// License: MIT
'use strict';
const fs = require('fs');
const path = require('path');
const PFG = require('../console/graph.js');

let checks = 0, failed = 0;
function check(ok, what) {
  checks++;
  if (!ok) { failed++; console.log('  FAIL ' + what); }
}

// ── An installation ────────────────────────────────────────────────────────
function loadSim() {
  const text = fs.readFileSync(path.join(__dirname, '..', 'console', 'pfe_sim.wasm.js'), 'utf8');
  const b64 = /PFE_SIM_WASM\s*=\s*"([^"]+)"/.exec(text)[1];
  return new WebAssembly.Instance(new WebAssembly.Module(Buffer.from(b64, 'base64')), {}).exports;
}
const x = loadSim();
let now = 5000;
const enc = new TextEncoder(), dec = new TextDecoder();
function cmd(obj) {
  const bytes = enc.encode(JSON.stringify(obj));
  new Uint8Array(x.memory.buffer, x.pfe_io(), bytes.length).set(bytes);
  x.pfe_command(bytes.length);
}
function lines() {
  const n = x.pfe_out_len();
  const text = n ? dec.decode(new Uint8Array(x.memory.buffer, x.pfe_out(), n)) : '';
  x.pfe_out_clear();
  return text.split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l); } catch (_) { return {}; } });
}
function run(ms) {
  for (let left = ms; left > 0; left -= 20) { now += Math.min(20, left); x.pfe_tick(now); }
}
function boot(panels) {
  x.pfe_init(panels, 12345, now);
  x.pfe_net(2, 1, 0);
  cmd({ c: 'hello' });
  cmd({ c: 'cfg', link: ['solo', 'solo', 'solo', 'solo'], plink: 'solo', k4: false });
  run(1500);
  lines();
}
const knob = (panel, k) => Math.round(x.pfe_lane(panel, k) * 1000);
const midi = (ch, cc, v) => cmd({ c: 'midi', ch, cc, v });
// A graph to the hub, the way the console sends it. -> the hub's complaint, or ''.
function send(graph) {
  const made = PFG.compile(graph);
  if (made.errors.length) return 'compile: ' + made.errors.map((e) => e.text).join('; ');
  lines();
  if (!made.bytes) { cmd({ c: 'graph', op: 'clear' }); return ''; }
  cmd({ c: 'graph', op: 'begin', len: made.bytes.length });
  for (let at = 0; at < made.bytes.length; at += 240) cmd({ c: 'graph', op: 'data', d: Buffer.from(made.bytes.subarray(at, at + 240)).toString('base64') });
  cmd({ c: 'graph', op: 'end' });
  cmd({ c: 'graph', op: 'on' });
  const err = lines().find((l) => l.t === 'err');
  return err ? err.m : '';
}
// Building graphs by hand.
let ids = 0;
function g() { return { nodes: [], links: [] }; }
function node(graph, type, p = {}) {
  const n = PFG.create(type, 'n' + (++ids), 0, 0);
  Object.assign(n.p, p);
  graph.nodes.push(n);
  return n;
}
function wire(graph, from, o, to, i) { graph.links.push({ a: from.id, o, b: to.id, i }); }
const KN = { K1: 0, K2: 1, K3: 2, K4: 3, PATTERN: 4, BRIGHT: 5 };

boot(4);

// ── One controller, every panel ────────────────────────────────────────────
{
  const G = g();
  const cc = node(G, 'cc', { ch: 5, cc: 21, v: 0 });
  const out = node(G, 'out');
  wire(G, cc, 0, out, KN.K1);
  check(send(G) === '', 'a controller wired to every panel is taken by the hub');
  midi(5, 21, 127);
  run(600);
  check([0, 1, 2, 3].every((p) => knob(p, 0) === 1000), 'the controller at the top: every K1 at the top');
  midi(5, 21, 32);
  run(600);
  check([0, 1, 2, 3].every((p) => knob(p, 0) === Math.round(32 / 127 * 1000)), 'and where it is turned to');
  check([0, 1, 2, 3].every((p) => knob(p, 1) === 500), 'a knob nothing is wired to is left alone');
}

// ── The same signal, shaped differently for different panels ──────────────
{
  const G = g();
  const cc = node(G, 'cc', { ch: 1, cc: 7, v: 0 });
  const map = node(G, 'map', { in1: 0, in2: 1, in3: 0.2, in4: 0.6 });       // 0..1 -> 0.2..0.6
  const inv = node(G, 'math', { op: 'inv' });
  const a = node(G, 'out', { to: [1, 2] });
  const b = node(G, 'out', { to: [3] });
  wire(G, cc, 0, map, 0);
  wire(G, cc, 0, inv, 0);
  wire(G, map, 0, a, KN.K2);
  wire(G, inv, 0, b, KN.K2);
  check(send(G) === '', 'one controller to two groups of panels');
  midi(1, 7, 127);
  run(600);
  check(knob(0, 1) === 600 && knob(1, 1) === 600, 'panels 1 and 2: the range it was mapped to');
  check(knob(2, 1) === 0, 'panel 3: the same signal turned over');
  check(knob(3, 1) === 500, 'panel 4: named by nobody, untouched');
  midi(1, 7, 0);
  run(600);
  check(knob(0, 1) === 200 && knob(2, 1) === 1000, 'and at the other end');
}

// ── Each node type, against the arithmetic it stands for ───────────────────
function oneValue(type, p, inputs = {}) {                  // the node's output on K1 of every panel
  const G = g();
  const n = node(G, type, p);
  for (const [i, v] of Object.entries(inputs)) n.p['in' + i] = v;
  const out = node(G, 'out');
  wire(G, n, 0, out, KN.K1);
  const problem = send(G);
  if (problem) return problem;
  run(500);
  return knob(1, 0);
}
{
  const near = (got, want, what) => check(typeof got === 'number' && Math.abs(got - Math.round(want * 1000)) <= 2, `${what}: ${want} (got ${got})`);
  near(oneValue('value', { v: 0.37 }), 0.37, 'a value');
  near(oneValue('math', { op: 'add' }, { 0: 0.2, 1: 0.3 }), 0.5, 'add');
  near(oneValue('math', { op: 'sub' }, { 0: 0.9, 1: 0.3 }), 0.6, 'subtract');
  near(oneValue('math', { op: 'mul' }, { 0: 0.5, 1: 0.5 }), 0.25, 'multiply');
  near(oneValue('math', { op: 'div' }, { 0: 0.2, 1: 0.5 }), 0.4, 'divide');
  near(oneValue('math', { op: 'div' }, { 0: 0.2, 1: 0 }), 0, 'divide by nothing is nothing');
  near(oneValue('math', { op: 'inv' }, { 0: 0.3 }), 0.7, 'turn over');
  near(oneValue('math', { op: 'min' }, { 0: 0.3, 1: 0.8 }), 0.3, 'the smaller');
  near(oneValue('math', { op: 'max' }, { 0: 0.3, 1: 0.8 }), 0.8, 'the larger');
  near(oneValue('math', { op: 'pow' }, { 0: 0.5, 1: 2 }), 0.25, 'power');
  near(oneValue('math', { op: 'mod' }, { 0: 1.3, 1: 1 }), 0.3, 'remainder');
  near(oneValue('math', { op: 'abs' }, { 0: -0.4 }), 0.4, 'absolute');
  near(oneValue('math', { op: 'gt' }, { 0: 0.6, 1: 0.5 }), 1, 'greater');
  near(oneValue('math', { op: 'lt' }, { 0: 0.6, 1: 0.5 }), 0, 'less');
  near(oneValue('math', { op: 'sin' }, { 0: Math.PI / 6 }), 0.5, 'sine');
  near(oneValue('math', { op: 'fract' }, { 0: 2.75 }), 0.75, 'fraction');
  near(oneValue('math', { op: 'sqrt' }, { 0: 0.49 }), 0.7, 'square root');
  near(oneValue('map', { clamp: true }, { 0: 0.5, 1: 0, 2: 1, 3: 0.2, 4: 0.4 }), 0.3, 'a range mapped');
  near(oneValue('map', { clamp: true }, { 0: 2, 1: 0, 2: 1, 3: 0.2, 4: 0.4 }), 0.4, 'and held inside it');
  near(oneValue('map', { clamp: false }, { 0: 2, 1: 0, 2: 1, 3: 0.2, 4: 0.4 }), 0.6, 'or let past it');
  near(oneValue('map', { clamp: true }, { 0: 0.25, 1: 1, 2: 0, 3: 0, 4: 1 }), 0.75, 'a range mapped backwards');
  near(oneValue('clamp', {}, { 0: 0.9, 1: 0.2, 2: 0.6 }), 0.6, 'kept between');
  near(oneValue('mix', {}, { 0: 0.2, 1: 0.8, 2: 0.25 }), 0.35, 'mixed');
  near(oneValue('steps', {}, { 0: 0.4, 1: 4 }), 1 / 3, 'four steps');
  near(oneValue('steps', {}, { 0: 0.95, 1: 4 }), 1, 'the top step');
  near(oneValue('switch', {}, { 0: 1, 1: 0.7, 2: 0.2 }), 0.7, 'a switch, on');
  near(oneValue('switch', {}, { 0: 0, 1: 0.7, 2: 0.2 }), 0.2, 'a switch, off');
  near(oneValue('smooth', {}, { 0: 0.8, 1: 0.2 }), 0.8, 'smoothed: where its input is');
  for (const [text, want] of [
    ['a * 0.5 + 0.25', 0.2 * 0.5 + 0.25], ['(1 + 2) * 3 / 10', 0.9], ['1 + 2 * 3 / 10', 1], ['2 ^ 3 / 10', 0.8], ['2 ^ -1', 0.5],
    ['-(-0.3)', 0.3], ['7 % 4 / 10', 0.3], ['(3 > 2) * 0.6', 0.6], ['(3 < 2) + 0.1', 0.1], ['sin(pi / 2) * 0.5', 0.5],
    ['floor(2.7) / 10', 0.2], ['min(a, 0.9) + max(0.1, b)', 0.2 + 0.3], ['sqrt(0.25)', 0.5], ['clamp(2, 0, 1) * 0.5', 0.5],
    ['mix(0, 1, 0.25)', 0.25], ['select(a > b, 0.9, 0.4)', 0.4], ['abs(a - b)', 0.1], ['fract(2.75)', 0.75], ['1 - a - b', 0.5],
    ['.5', 0.5], ['cos(0) / 4', 0.25], ['round(0.6) / 2', 0.5], ['pow(a, 2) * 10', 0.4], ['mod(1.3, 1)', 0.3], ['tau / 10', 0.628],
  ]) near(oneValue('expr', { text }, { 0: 0.2, 1: 0.3 }), want, `formula "${text}"`);
  for (const text of ['a +', 'foo(1)', '1 2', 'x', 'sin(1, 2)', '(1', 'a $ b', ''])
    check(String(oneValue('expr', { text })).startsWith('compile:'), `formula "${text}" is refused with a reason`);
}

// ── A panel's own number, place, and knobs ─────────────────────────────────
{
  const G = g();
  const p = node(G, 'panel');
  const out = node(G, 'out');
  wire(G, p, 1, out, KN.K1);                                // its order, 0..1
  wire(G, p, 5, out, KN.K3);                                // its own random number
  const div = node(G, 'math', { op: 'div', in1: 4 });
  wire(G, p, 0, div, 0);                                    // its number, over 4
  wire(G, div, 0, out, KN.K2);
  check(send(G) === '', 'a graph that asks each panel who it is');
  run(600);
  check(knob(0, 0) === 0 && knob(1, 0) === 333 && knob(2, 0) === 667 && knob(3, 0) === 1000, 'each panel its place in the order');
  check(knob(0, 1) === 250 && knob(3, 1) === 1000, 'each panel its number');
  check(new Set([0, 1, 2, 3].map((q) => knob(q, 2))).size === 4, 'each panel a number of its own');
}
{
  // K1 of each panel, by hand, drives its K2 the other way.
  const G = g();
  const k = node(G, 'knob');
  const inv = node(G, 'math', { op: 'inv' });
  const out = node(G, 'out');
  wire(G, k, 0, inv, 0);
  wire(G, inv, 0, out, KN.K2);
  check(send(G) === '', 'a panel knob as an input');
  run(600);
  x.pfe_hand(2, 0, 5);
  run(900);
  check(knob(2, 0) !== knob(0, 0) && knob(2, 1) === 1000 - knob(2, 0) && knob(0, 1) === 1000 - knob(0, 0), 'each panel follows its own hand');
}

// ── Things that move by themselves ─────────────────────────────────────────
{
  const G = g();
  const lfo = node(G, 'lfo', { wave: 'saw', in0: 1, in1: 0 });            // once a second
  const p = node(G, 'panel');
  const spread = node(G, 'lfo', { wave: 'sine', in0: 0.5 });              // every panel a different phase
  const out = node(G, 'out');
  wire(G, lfo, 0, out, KN.K1);
  wire(G, p, 1, spread, 1);
  wire(G, spread, 0, out, KN.K2);
  check(send(G) === '', 'LFOs');
  const seen = [], phases = [];
  for (let i = 0; i < 25; i++) { run(80); seen.push(knob(0, 0)); phases.push([0, 1, 2, 3].map((q) => knob(q, 1))); }
  check(Math.min(...seen) < 150 && Math.max(...seen) > 850, 'a saw goes from the bottom to the top');
  check(seen.some((v, i) => i && v < seen[i - 1] - 400), 'and falls back');
  check(phases.some((row) => Math.max(...row) - Math.min(...row) > 400), 'panels offset by their order are in different places of the wave');
  for (const wave of ['sine', 'triangle', 'square']) {
    const H = g();
    const l = node(H, 'lfo', { wave, in0: 2 });
    const o = node(H, 'out');
    wire(H, l, 0, o, KN.K1);
    check(send(H) === '', wave);
    const vals = [];
    for (let i = 0; i < 20; i++) { run(60); vals.push(knob(0, 0)); }
    check(Math.min(...vals) >= 0 && Math.max(...vals) <= 1000 && Math.max(...vals) - Math.min(...vals) > 500, `${wave} swings between 0 and 1`);
  }
  const N = g();
  const nz = node(N, 'noise', { in0: 2 });
  const o = node(N, 'out');
  wire(N, nz, 0, o, KN.K1);
  check(send(N) === '', 'noise');
  const vals = [];
  for (let i = 0; i < 30; i++) { run(100); vals.push(knob(0, 0)); }
  check(new Set(vals).size > 10 && vals.every((v) => v >= 0 && v <= 1000), 'noise wanders, inside 0..1');
}

// ── The pattern and the brightness ─────────────────────────────────────────
{
  const G = g();
  const cc = node(G, 'cc', { ch: 2, cc: 1, v: 0 });
  const map = node(G, 'map', { in3: 0, in4: 4 });                          // 0..1 -> pattern 0..4
  const out = node(G, 'out', { to: [2] });
  wire(G, cc, 0, map, 0);
  wire(G, map, 0, out, KN.PATTERN);
  wire(G, cc, 0, out, KN.BRIGHT);
  check(send(G) === '', 'a controller choosing the pattern and the brightness of one panel');
  midi(2, 1, 127);
  run(1500);
  check(x.pfe_pattern(1) === 4 && x.pfe_pattern(0) === 0, 'panel 2 on pattern 4, the others where they were');
  midi(2, 1, 64);
  run(1500);
  check(x.pfe_pattern(1) === 2, 'half way: pattern 2');
}

// ── What cannot be, and what need not be ───────────────────────────────────
{
  const G = g();
  const a = node(G, 'math', { op: 'add' }), b = node(G, 'math', { op: 'add' });
  const out = node(G, 'out');
  wire(G, a, 0, b, 0);
  wire(G, b, 0, a, 0);
  wire(G, b, 0, out, KN.K1);
  const made = PFG.compile(G);
  check(made.bytes === null && made.errors.length > 0 && made.errors[0].text.includes('loop'), 'nodes wired in a circle are refused, and said so');

  const idle = g();
  node(idle, 'cc');
  node(idle, 'math');
  check(PFG.compile(idle).bytes === null && PFG.compile(idle).errors.length === 0, 'nodes that reach no panel are no program, and no error');
  const unwired = g();
  node(unwired, 'out');
  check(PFG.compile(unwired).bytes === null, 'an output with nothing wired to it drives nothing');
  check(send(idle) === '', 'and clear the hub');
  midi(5, 21, 0);
  const before = knob(0, 0);
  run(500);
  check(knob(0, 0) === before, 'after which no controller moves anything');

  // Only what is wired to a panel is in the program, and a number is in it once.
  const lean = g();
  const v1 = node(lean, 'value', { v: 0.5 }), v2 = node(lean, 'value', { v: 0.5 });
  const add = node(lean, 'math', { op: 'add' });
  node(lean, 'lfo');                                          // wired to nothing
  const o = node(lean, 'out');
  wire(lean, v1, 0, add, 0);
  wire(lean, v2, 0, add, 1);
  wire(lean, add, 0, o, KN.K1);
  const made2 = PFG.compile(lean);
  check(made2.registers === 2 && made2.size === 6 + 6 + 4 + 2 + 3 + 1, `only what reaches a panel is compiled (${made2.registers} registers, ${made2.size} bytes)`);
  check(made2.regs[add.id].length === 1 && made2.ins[add.id].length === 2 && made2.ins[o.id][0] === made2.regs[add.id][0] && made2.ins[o.id][1] === -1,
        'and the compiler says which register is which socket');
  // A second input a one-input operation does not use is not compiled either.
  const one = g();
  const big = node(one, 'expr', { text: 'a + b + c + d' });
  const sq = node(one, 'math', { op: 'abs' });
  const o2 = node(one, 'out');
  wire(one, big, 0, sq, 1);                                   // into B, which "absolute" ignores
  wire(one, sq, 0, o2, KN.K1);
  check(PFG.compile(one).regs[big.id] === undefined, 'an input the operation does not read pulls nothing in');

  // Too much of anything is said, not sent.
  const many = g();
  const sink = node(many, 'out');
  let last = node(many, 'lfo');
  for (let i = 0; i < 40; i++) {
    const s = node(many, 'smooth');
    wire(many, last, 0, s, 0);
    last = s;
  }
  wire(many, last, 0, sink, KN.K1);
  const made3 = PFG.compile(many);
  check(made3.bytes === null && made3.errors.some((e) => e.text.includes('Smooth')), 'more nodes that remember than the hub has room for');
}

// ── What the console watches ───────────────────────────────────────────────
{
  const G = g();
  const cc = node(G, 'cc', { ch: 3, cc: 40, v: 0 });
  const half = node(G, 'math', { op: 'mul', in1: 0.5 });
  const out = node(G, 'out');
  wire(G, cc, 0, half, 0);
  wire(G, half, 0, out, KN.K1);
  const made = PFG.compile(G);
  check(send(G) === '', 'a graph to watch');
  cmd({ c: 'mon', on: true, panel: '#1' });
  midi(3, 40, 127);
  run(700);
  const seen = lines();
  check(seen.some((l) => l.t === 'mi' && l.ch === 3 && l.cc === 40 && l.v === 127), 'the controller is reported as it comes in');
  const nv = seen.filter((l) => l.t === 'nv').pop();
  check(nv && nv.p === 1 && nv.v[made.regs[cc.id][0]] === 1 && nv.v[made.regs[half.id][0]] === 0.5, 'and each node\'s value, by the register the compiler gave it');
  cmd({ c: 'mon', on: false });
  cmd({ c: 'graph', op: 'get' });
  const info = lines().find((l) => l.t === 'graph');
  check(info && info.on === 1 && info.len === made.size && JSON.stringify(info.cc) === '[[3,40]]', 'the hub says what it is running');
}

console.log(`${failed ? 'FAIL' : 'PASS'}  ${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
