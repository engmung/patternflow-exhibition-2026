// Patternflow Ensemble - the node graph, as the console holds it and as it
// goes to the hub.
//
// A graph is nodes and the wires between their sockets:
//
//   { nodes: [{ id, type, x, y, p: { ...the node's own settings and the
//                                     values of its unwired inputs } }],
//     links: [{ a: node id, o: output number, b: node id, i: input number }] }
//
// The hub does not run nodes: it runs a small program of registers and
// instructions, once for every panel, twenty-five times a second
// (core/pfe_graph.h). PFG.compile(graph) is that flattening:
//
//   -> { bytes,            the program, or null
//        errors: [{ node, text }],
//        regs: { node id: [register of each output] },   for showing live values
//        ins:  { node id: [register of each input] },
//        size, registers }
//
// PFG.TYPES is the list of node types: what each is called, its sockets and
// settings, and how it becomes instructions. A new node is one entry here,
// as long as the instructions the hub already knows can say it.
//
// No page here: the console (app.js) draws the graph, Node runs the tests
// (tests/test_graph.js) against the hub's own code.
//
// License: MIT
(function (root) {
  'use strict';

  // The hub's instructions (core/pfe_graph.h, GraphOp) - the same numbers.
  const OP = { END: 0, CONST: 1, CC: 2, TIME: 3, PANEL: 4, KNOB: 5, ADD: 6, SUB: 7, MUL: 8, DIV: 9, MIN: 10, MAX: 11, POW: 12, MOD: 13,
               GT: 14, LT: 15, ABS: 16, NEG: 17, SIN: 18, COS: 19, FLOOR: 20, ROUND: 21, FRACT: 22, SQRT: 23, SIGN: 24, NOISE: 25,
               CLAMP: 26, MIX: 27, SELECT: 28, SMOOTH: 29, PHASE: 30, TARGET: 31, OUT: 32 };
  const PANEL = { id: 0, order: 1, order01: 2, x: 3, y: 4, z: 5, random: 6, count: 7 };
  const MAX_BYTES = 2048, MAX_REGS = 128, MAX_CC = 32, MAX_STATE = 32;

  // ── What a node writes its instructions with ────────────────────────────
  class Emitter {
    constructor() {
      this.code = [];
      this.cc = [];                 // [channel, number, start]
      this.regs = 0;
      this.consts = new Map();
      this.states = 0;
    }
    reg() { return this.regs++; }
    k(value) {                      // a number, once
      const v = Math.fround(Number.isFinite(value) ? value : 0);
      if (this.consts.has(v)) return this.consts.get(v);
      const r = this.reg();
      const b = new Uint8Array(new Float32Array([v]).buffer);
      this.code.push(OP.CONST, r, b[0], b[1], b[2], b[3]);
      this.consts.set(v, r);
      return r;
    }
    op(code, ...operands) {         // an instruction with a result
      const r = this.reg();
      this.code.push(code, r, ...operands);
      if (code === OP.SMOOTH || code === OP.PHASE) this.states++;
      return r;
    }
    controller(channel, number, start) {
      let slot = this.cc.findIndex((c) => c[0] === channel && c[1] === number);
      if (slot < 0) { slot = this.cc.length; this.cc.push([channel, number, start]); }
      return this.op(OP.CC, slot);
    }
    // Arithmetic a node may want without spelling it out.
    add(a, b) { return this.op(OP.ADD, a, b); }
    sub(a, b) { return this.op(OP.SUB, a, b); }
    mul(a, b) { return this.op(OP.MUL, a, b); }
    div(a, b) { return this.op(OP.DIV, a, b); }
    target(ids) { this.code.push(OP.TARGET, ids.length, ...ids); }
    out(reg, what) { this.code.push(OP.OUT, reg, what); }
  }

  // ── A formula: a, b, c, d (the node's inputs), t (seconds), numbers ─────
  const FUNCS = {
    sin: [1, OP.SIN], cos: [1, OP.COS], abs: [1, OP.ABS], floor: [1, OP.FLOOR], round: [1, OP.ROUND], fract: [1, OP.FRACT],
    sqrt: [1, OP.SQRT], sign: [1, OP.SIGN], noise: [1, OP.NOISE],
    min: [2, OP.MIN], max: [2, OP.MAX], pow: [2, OP.POW], mod: [2, OP.MOD],
    clamp: [3, OP.CLAMP], mix: [3, OP.MIX], select: [3, OP.SELECT],
  };
  function formula(text, e, vars) {
    const tokens = String(text).match(/\s*(\d+\.?\d*|\.\d+|[A-Za-z_]\w*|[-+*/%^(),<>])/g);
    const src = String(text).replace(/\s+/g, '');
    if (!src) throw new Error('The expression is empty');
    const list = (tokens || []).map((t) => t.trim());
    if (list.join('') !== src) throw new Error('The expression has a character it cannot use');
    let at = 0;
    const peek = () => list[at];
    const take = (want) => {
      if (want && list[at] !== want) throw new Error(`Expression: expected "${want}"`);
      return list[at++];
    };
    const primary = () => {
      const t = take();
      if (t == null) throw new Error('The expression ends in the middle');
      if (/^[\d.]/.test(t)) return e.k(parseFloat(t));
      if (t === '(') { const r = compare(); take(')'); return r; }
      if (t === '-') return e.op(OP.NEG, unary());
      if (/^[A-Za-z_]/.test(t)) {
        if (peek() === '(') {
          const f = FUNCS[t];
          if (!f) throw new Error(`Expression: there is no function "${t}"`);
          take('(');
          const args = [];
          if (peek() !== ')') { args.push(compare()); while (peek() === ',') { take(','); args.push(compare()); } }
          take(')');
          if (args.length !== f[0]) throw new Error(`Expression: ${t}() takes ${f[0]} value${f[0] > 1 ? 's' : ''}`);
          return e.op(f[1], ...args);
        }
        if (t === 'pi') return e.k(Math.PI);
        if (t === 'tau') return e.k(Math.PI * 2);
        if (Object.prototype.hasOwnProperty.call(vars, t)) return vars[t]();
        throw new Error(`Expression: "${t}" is not known (a, b, c, d, t, pi)`);
      }
      throw new Error(`Expression: "${t}" does not belong there`);
    };
    const unary = () => (peek() === '-' ? (take(), e.op(OP.NEG, unary())) : power());
    const power = () => {
      const base = primary();
      return peek() === '^' ? (take(), e.op(OP.POW, base, unary())) : base;      // right to left
    };
    const product = () => {
      let r = unary();
      for (;;) {
        const t = peek();
        if (t === '*') { take(); r = e.op(OP.MUL, r, unary()); }
        else if (t === '/') { take(); r = e.op(OP.DIV, r, unary()); }
        else if (t === '%') { take(); r = e.op(OP.MOD, r, unary()); }
        else return r;
      }
    };
    const sum = () => {
      let r = product();
      for (;;) {
        const t = peek();
        if (t === '+') { take(); r = e.op(OP.ADD, r, product()); }
        else if (t === '-') { take(); r = e.op(OP.SUB, r, product()); }
        else return r;
      }
    };
    const compare = () => {
      let r = sum();
      for (;;) {
        const t = peek();
        if (t === '>') { take(); r = e.op(OP.GT, r, sum()); }
        else if (t === '<') { take(); r = e.op(OP.LT, r, sum()); }
        else return r;
      }
    };
    const result = compare();
    if (at < list.length) throw new Error(`Expression: "${list[at]}" does not belong there`);
    return result;
  }

  // ── The node types ───────────────────────────────────────────────────────
  // ins: sockets [{ name, def, step }] (a socket with no wire uses the
  //      node's own value for it, p.in<number>, or def)
  // outs: socket names
  // params: settings drawn in the node [{ key, label, kind, ... }]
  // emit(e, node, ins) -> the register of each output
  const num = (v, def) => (Number.isFinite(+v) && v !== '' && v != null ? +v : def);
  const MATH = [
    ['add', 'Add', 2, (e, a, b) => e.add(a, b)],
    ['sub', 'Subtract', 2, (e, a, b) => e.sub(a, b)],
    ['mul', 'Multiply', 2, (e, a, b) => e.mul(a, b)],
    ['div', 'Divide', 2, (e, a, b) => e.div(a, b)],
    ['inv', 'Invert (1 − A)', 1, (e, a) => e.sub(e.k(1), a)],
    ['min', 'Minimum', 2, (e, a, b) => e.op(OP.MIN, a, b)],
    ['max', 'Maximum', 2, (e, a, b) => e.op(OP.MAX, a, b)],
    ['pow', 'Power', 2, (e, a, b) => e.op(OP.POW, a, b)],
    ['mod', 'Modulo', 2, (e, a, b) => e.op(OP.MOD, a, b)],
    ['abs', 'Absolute', 1, (e, a) => e.op(OP.ABS, a)],
    ['gt', 'Greater Than', 2, (e, a, b) => e.op(OP.GT, a, b)],
    ['lt', 'Less Than', 2, (e, a, b) => e.op(OP.LT, a, b)],
    ['sin', 'Sine', 1, (e, a) => e.op(OP.SIN, a)],
    ['cos', 'Cosine', 1, (e, a) => e.op(OP.COS, a)],
    ['floor', 'Floor', 1, (e, a) => e.op(OP.FLOOR, a)],
    ['round', 'Round', 1, (e, a) => e.op(OP.ROUND, a)],
    ['fract', 'Fraction', 1, (e, a) => e.op(OP.FRACT, a)],
    ['sqrt', 'Square Root', 1, (e, a) => e.op(OP.SQRT, a)],
  ];
  const WAVES = [['sine', 'Sine'], ['triangle', 'Triangle'], ['saw', 'Saw'], ['square', 'Square']];

  const TYPES = {
    // ── Input ──
    cc: {
      title: 'MIDI CC', group: 'Input', ins: [], outs: ['Value'],
      params: [{ key: 'label', label: 'Name', kind: 'text', def: '' },
               { key: 'ch', label: 'Channel', kind: 'number', min: 1, max: 16, step: 1, def: 1 },
               { key: 'cc', label: 'CC', kind: 'number', min: 0, max: 127, step: 1, def: 21 },
               { key: 'v', label: '', kind: 'cc', def: 0 }],          // its value, 0..127: a fader to try it by, and where it starts
      emit: (e, n) => [e.controller(clampInt(n.p.ch, 1, 16, 1), clampInt(n.p.cc, 0, 127, 21), clampInt(n.p.v, 0, 127, 0))],
    },
    value: {
      title: 'Value', group: 'Input', ins: [], outs: ['Value'],
      params: [{ key: 'v', label: '', kind: 'number', step: 0.01, def: 0.5 }],
      emit: (e, n) => [e.k(num(n.p.v, 0.5))],
    },
    time: { title: 'Time', group: 'Input', ins: [], outs: ['Seconds'], params: [], emit: (e) => [e.op(OP.TIME)] },
    lfo: {
      title: 'LFO', group: 'Input', outs: ['Value'],
      ins: [{ name: 'Rate (Hz)', def: 0.5, step: 0.05 }, { name: 'Phase', def: 0, step: 0.05 }],
      params: [{ key: 'wave', label: '', kind: 'select', options: WAVES, def: 'sine' }],
      emit: (e, n, [rate, offset]) => {
        const x = e.op(OP.FRACT, e.add(e.op(OP.PHASE, rate), offset));          // 0..1 round and round
        switch (n.p.wave) {
          case 'triangle': return [e.sub(e.k(1), e.op(OP.ABS, e.sub(e.mul(x, e.k(2)), e.k(1))))];
          case 'saw': return [x];
          case 'square': return [e.op(OP.LT, x, e.k(0.5))];
          default: return [e.add(e.k(0.5), e.mul(e.k(0.5), e.op(OP.SIN, e.mul(x, e.k(Math.PI * 2)))))];
        }
      },
    },
    noise: {
      title: 'Noise', group: 'Input', outs: ['Value'],
      ins: [{ name: 'Speed', def: 0.5, step: 0.05 }, { name: 'Offset', def: 0, step: 0.1 }],
      params: [],
      emit: (e, n, [rate, offset]) => [e.op(OP.NOISE, e.add(e.mul(e.op(OP.TIME), rate), offset))],
    },
    panel: {
      title: 'Panel Info', group: 'Input', ins: [], params: [],
      outs: ['Number', 'Order (0–1)', 'X', 'Y', 'Z', 'Random', 'Count'],
      emit: (e) => [PANEL.id, PANEL.order01, PANEL.x, PANEL.y, PANEL.z, PANEL.random, PANEL.count].map((what) => e.op(OP.PANEL, what)),
    },
    knob: {
      title: 'Panel Knobs', group: 'Input', ins: [], params: [], outs: ['K1', 'K2', 'K3', 'K4'],
      emit: (e) => [0, 1, 2, 3].map((k) => e.op(OP.KNOB, k)),
    },
    // ── Converter ──
    math: {
      title: 'Math', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'A', def: 0.5, step: 0.05 }, { name: 'B', def: 0.5, step: 0.05 }],
      params: [{ key: 'op', label: '', kind: 'select', options: MATH.map((m) => [m[0], m[1]]), def: 'mul' }],
      emit: (e, n, [a, b]) => [(MATH.find((m) => m[0] === n.p.op) || MATH[2])[3](e, a, b)],
      uses: (n) => (MATH.find((m) => m[0] === n.p.op) || MATH[2])[2],            // how many of its inputs count
    },
    map: {
      title: 'Map Range', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'Value', def: 0.5, step: 0.05 }, { name: 'From Min', def: 0, step: 0.05 }, { name: 'From Max', def: 1, step: 0.05 },
            { name: 'To Min', def: 0, step: 0.05 }, { name: 'To Max', def: 1, step: 0.05 }],
      params: [{ key: 'clamp', label: 'Clamp', kind: 'check', def: true }],
      emit: (e, n, [v, a, b, c, d]) => {
        let t = e.div(e.sub(v, a), e.sub(b, a));
        if (n.p.clamp !== false) t = e.op(OP.CLAMP, t, e.k(0), e.k(1));
        return [e.op(OP.MIX, c, d, t)];
      },
    },
    clamp: {
      title: 'Clamp', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'Value', def: 0.5, step: 0.05 }, { name: 'Min', def: 0, step: 0.05 }, { name: 'Max', def: 1, step: 0.05 }],
      params: [],
      emit: (e, n, [v, lo, hi]) => [e.op(OP.CLAMP, v, lo, hi)],
    },
    mix: {
      title: 'Mix', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'A', def: 0, step: 0.05 }, { name: 'B', def: 1, step: 0.05 }, { name: 'Factor', def: 0.5, step: 0.05 }],
      params: [],
      emit: (e, n, [a, b, t]) => [e.op(OP.MIX, a, b, t)],
    },
    smooth: {
      title: 'Smooth', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'Value', def: 0.5, step: 0.05 }, { name: 'Time (s)', def: 0.3, step: 0.05 }],
      params: [],
      emit: (e, n, [v, secs]) => [e.op(OP.SMOOTH, v, secs)],
    },
    steps: {
      title: 'Snap', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'Value', def: 0.5, step: 0.05 }, { name: 'Steps', def: 4, step: 1 }],
      params: [],
      emit: (e, n, [v, count]) => {
        const last = e.sub(e.op(OP.MAX, count, e.k(2)), e.k(1));               // 4 steps: 0, 1/3, 2/3, 1
        return [e.div(e.op(OP.ROUND, e.mul(v, last)), last)];
      },
    },
    switch: {
      title: 'Switch', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'Condition (> 0.5)', def: 0, step: 1 }, { name: 'True', def: 1, step: 0.05 }, { name: 'False', def: 0, step: 0.05 }],
      params: [],
      emit: (e, n, [cond, a, b]) => [e.op(OP.SELECT, cond, a, b)],
    },
    expr: {
      title: 'Expression', group: 'Converter', outs: ['Value'],
      ins: [{ name: 'a', def: 0, step: 0.05 }, { name: 'b', def: 0, step: 0.05 }, { name: 'c', def: 0, step: 0.05 }, { name: 'd', def: 0, step: 0.05 }],
      params: [{ key: 'text', label: '', kind: 'formula', def: 'a * 0.5 + 0.25' }],
      emit: (e, n, [a, b, c, d]) => [formula(n.p.text == null ? 'a' : n.p.text, e, { a: () => a, b: () => b, c: () => c, d: () => d, t: () => e.op(OP.TIME) })],
    },
    // ── Output ──
    out: {
      title: 'Panel Output', group: 'Output', outs: [], sink: true,
      ins: [{ name: 'K1', drive: 0 }, { name: 'K2', drive: 1 }, { name: 'K3', drive: 2 }, { name: 'K4', drive: 3 },
            { name: 'Pattern (index)', drive: 4 }, { name: 'Brightness', drive: 5 }],
      params: [{ key: 'to', label: '', kind: 'panels', def: [] }],             // [] : every panel
    },
  };
  function clampInt(v, lo, hi, def) {
    const n = Math.round(num(v, def));
    return Math.min(hi, Math.max(lo, n));
  }

  // A node as it is first put down.
  function create(type, id, x, y) {
    const T = TYPES[type];
    if (!T) return null;
    const p = {};
    for (const q of T.params) p[q.key] = Array.isArray(q.def) ? q.def.slice() : q.def;
    return { id, type, x, y, p };
  }
  // The value an unwired input has.
  function inputValue(node, i) {
    const s = TYPES[node.type].ins[i];
    return num(node.p['in' + i], s.def);
  }

  // ── Graph -> program ─────────────────────────────────────────────────────
  function compile(graph) {
    const errors = [];
    const nodes = new Map();
    for (const n of graph.nodes || []) if (TYPES[n.type]) nodes.set(n.id, n);
    const wired = new Map();                              // "node:input" -> { a, o }
    for (const l of graph.links || []) {
      const from = nodes.get(l.a), to = nodes.get(l.b);
      if (!from || !to) continue;
      if (!(l.o >= 0 && l.o < TYPES[from.type].outs.length) || !(l.i >= 0 && l.i < TYPES[to.type].ins.length)) continue;
      wired.set(to.id + ':' + l.i, { a: l.a, o: l.o });
    }
    const e = new Emitter();
    const regs = {}, ins = {};
    const state = new Map();                              // node id -> 'doing' | 'done'
    const need = (node) => {
      if (state.get(node.id) === 'done') return true;
      if (state.get(node.id) === 'doing') {
        errors.push({ node: node.id, text: 'These nodes are wired in a loop' });
        return false;
      }
      state.set(node.id, 'doing');
      const T = TYPES[node.type];
      const uses = T.uses ? T.uses(node) : T.ins.length;
      const inputs = [];
      for (let i = 0; i < T.ins.length; i++) {
        const w = wired.get(node.id + ':' + i);
        if (w && i < uses) {
          if (!need(nodes.get(w.a))) { state.set(node.id, 'done'); regs[node.id] = regs[node.id] || []; return false; }
          inputs.push(regs[w.a][w.o]);
        } else if (T.sink) inputs.push(-1);               // an output's unwired socket drives nothing
        else inputs.push(e.k(inputValue(node, i)));
      }
      ins[node.id] = inputs;
      if (!T.sink) {
        try { regs[node.id] = T.emit(e, node, inputs); }
        catch (err) { errors.push({ node: node.id, text: err.message }); regs[node.id] = T.outs.map(() => e.k(0)); }
      }
      state.set(node.id, 'done');
      return true;
    };
    const sinks = [...nodes.values()].filter((n) => TYPES[n.type].sink);
    for (const s of sinks) need(s);
    // Then who gets what. Outputs to the same panels share one TARGET.
    let driven = 0;
    const byTarget = new Map();
    for (const s of sinks) {
      const ids = [...new Set((Array.isArray(s.p.to) ? s.p.to : []).map((v) => Math.round(+v)).filter((v) => v >= 1 && v <= 250))].sort((a, b) => a - b);
      const key = ids.join(',');
      if (!byTarget.has(key)) byTarget.set(key, { ids, outs: [] });
      (ins[s.id] || []).forEach((r, i) => { if (r >= 0) byTarget.get(key).outs.push([r, TYPES.out.ins[i].drive]); });
    }
    // Every panel first, then the chosen ones, the smaller group later: what
    // is written later is what a panel takes, so an output for panels 2 and
    // 3 outranks one for every panel whichever node was put down first.
    const groups = [...byTarget.values()].filter((t) => t.outs.length)
      .sort((a, b) => (a.ids.length ? 1 : 0) - (b.ids.length ? 1 : 0) || b.ids.length - a.ids.length);
    for (const t of groups) {
      e.target(t.ids);
      for (const [r, what] of t.outs) { e.out(r, what); driven++; }
    }
    const out = { bytes: null, errors, regs, ins, size: 0, registers: e.regs, driven, controllers: e.cc.map((c) => c.slice(0, 2)) };
    if (errors.length) return out;
    if (!driven) return out;                              // nothing reaches a panel: no program at all
    if (e.regs > MAX_REGS) errors.push({ node: null, text: `Too many values (${e.regs}; the hub holds ${MAX_REGS})` });
    if (e.cc.length > MAX_CC) errors.push({ node: null, text: `Too many MIDI controllers (${e.cc.length}; up to ${MAX_CC})` });
    if (e.states > MAX_STATE) errors.push({ node: null, text: `Too many LFO and Smooth nodes (${e.states}; up to ${MAX_STATE})` });
    const bytes = new Uint8Array(6 + e.cc.length * 3 + e.code.length + 1);
    bytes.set([0x50, 0x46, 0x47, 1, Math.max(1, e.regs), e.cc.length]);
    e.cc.forEach((c, i) => bytes.set(c, 6 + i * 3));
    bytes.set(e.code, 6 + e.cc.length * 3);
    out.size = bytes.length;
    if (bytes.length > MAX_BYTES) errors.push({ node: null, text: `The graph is too large (${bytes.length} bytes; the hub takes ${MAX_BYTES})` });
    if (!errors.length) out.bytes = bytes;
    return out;
  }

  const api = { TYPES, OP, compile, create, inputValue, formula, Emitter, MAX_BYTES, MAX_REGS };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PFG = api;
})(typeof window !== 'undefined' ? window : globalThis);
