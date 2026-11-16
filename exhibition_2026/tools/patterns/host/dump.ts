// JS side of the host parity harness (tools/patterns/host/pfhost.py drives it).
//
// Runs a Pattern Lab JS pattern through the web's own PatternRuntime (the code
// the Pattern Lab preview uses, as tools/patterns/render.ts does) over an
// EXPANDED script - the same file the native runner reads, one line per frame:
//
//   frames N
//   p1 p2 p3 pressMask heldMask capture      (x N)
//
// and writes the captured frames as raw RGB888 (alpha flattened onto black,
// the way the panel shows an unwritten pixel), in the pattern's own canvas
// orientation. Prints one JSON line.
//
// Knobs as the exhibit firmware gives them: knobValues[i] = lo + (hi - lo) * p
// with lo/hi from the @knobs line; in the frame a knob's button is PRESSED the
// firmware drops that knob's lane, so PFParams leaves the parameter at the
// previous frame's value - mirrored here by holding knobValues[i] for that
// frame. K4 is a constant (--k4, which pfhost.py reads out of the C++ header:
// K4 never reaches an exhibit pattern). dt = 1/30, time = frame / 30.
//
//   tools/patterns/run.sh host/dump <pattern.js> <script.txt> <out.rgb> [--k4 v]

import fs from "node:fs";
import { PatternRuntime, type ColorRamp, type PatternInput } from "@/lib/pattern/harness";
import { parseKnobsAnnotation } from "@/lib/pattern/knobs";
import { parseMatrixAnnotation } from "@/lib/pattern/matrix";
import { parseRampAnnotation } from "@/lib/pattern/ramp";
import { hexToRgb } from "@/lib/pattern/color";

const [file, scriptPath, outPath] = process.argv.slice(2);
if (!file || !scriptPath || !outPath) {
  console.error("usage: dump.ts <pattern.js> <script.txt> <out.rgb> [--k4 value]");
  process.exit(2);
}
const k4Arg = process.argv.indexOf("--k4");

const code = fs.readFileSync(file, "utf8");
const matrix = parseMatrixAnnotation(code) ?? { width: 128, height: 64 };
const knobs = parseKnobsAnnotation(code) ?? [null, null, null, null];
const rampAnn = parseRampAnnotation(code);
const ramp: ColorRamp | null = rampAnn
  ? {
      mode: rampAnn.mode,
      wrap: rampAnn.wrap,
      stops: rampAnn.stops.map((s) => ({ position: s.position, color: hexToRgb(s.color) })),
    }
  : null;
const ranges: Array<[number, number]> = knobs.map((k) => (k ? [k.min, k.max] : [0, 1]));
const k4 = k4Arg >= 0 ? Number(process.argv[k4Arg + 1]) : ranges[3][0];

const lines = fs.readFileSync(scriptPath, "utf8").split(/\r?\n/).filter((l) => l.trim());
const n = Number(lines[0].split(/\s+/)[1]);
const steps = lines.slice(1, n + 1).map((l) => {
  const v = l.trim().split(/\s+/).map(Number);
  return { pos: [v[0], v[1], v[2]], press: v[3], held: v[4], capture: v[5] };
});

const rt = new PatternRuntime(matrix.width, matrix.height);
if (ramp) rt.setRamp(ramp);
rt.recolor = Boolean(rampAnn?.recolor && ramp);
const t0 = performance.now();
const load = rt.loadCode(code);
const setupMs = performance.now() - t0;
if (!load.ok) {
  console.log(JSON.stringify({ error: load.error }));
  process.exit(1);
}

const w = matrix.width;
const h = matrix.height;
const out = fs.openSync(outPath, "w");
const rgb = Buffer.alloc(w * h * 3);
const ms: number[] = [];
let captured = 0;
let error: string | undefined;
const lastValues = [0, 1, 2].map((i) => ranges[i][0] + (ranges[i][1] - ranges[i][0]) * steps[0].pos[i]);

for (let f = 1; f <= n; f++) {
  const s = steps[f - 1];
  const pressed = [0, 1, 2, 3].map((i) => i < 3 && ((s.press >> i) & 1) === 1);
  const held = [0, 1, 2, 3].map((i) => i < 3 && ((s.held >> i) & 1) === 1);
  const values = [0, 1, 2].map((i) => {
    if (pressed[i]) return lastValues[i];
    const v = ranges[i][0] + (ranges[i][1] - ranges[i][0]) * s.pos[i];
    lastValues[i] = v;
    return v;
  });
  values.push(k4);
  const norm = values.map((v, i) => (ranges[i][1] !== ranges[i][0] ? (v - ranges[i][0]) / (ranges[i][1] - ranges[i][0]) : 0));
  const input: PatternInput = {
    knobDeltas: [0, 0, 0, 0],
    knobValues: values,
    knobNormalized: norm,
    knobRanges: ranges,
    btnPressed: pressed,
    btnHeld: held,
  };
  const a = performance.now();
  const res = rt.renderFrame(1 / 30, f / 30, input);
  ms.push(performance.now() - a);
  if (!res.ok && !error) error = `frame ${f}: ${res.error}`;
  if (s.capture) {
    const d = rt.data;
    for (let p = 0, q = 0; p < d.length; p += 4, q += 3) {
      const al = d[p + 3];
      rgb[q] = Math.round((d[p] * al) / 255);
      rgb[q + 1] = Math.round((d[p + 1] * al) / 255);
      rgb[q + 2] = Math.round((d[p + 2] * al) / 255);
    }
    fs.writeSync(out, rgb);
    captured++;
  }
}
fs.closeSync(out);
const steady = ms.slice(ms.length > 20 ? 10 : 0).sort((x, y) => x - y);
console.log(
  JSON.stringify({
    w,
    h,
    captured,
    frames: n,
    k4,
    ranges,
    ramp: rampAnn ? rampAnn.mode : null,
    setupMs: Math.round(setupMs * 1000) / 1000,
    medianMs: steady.length ? Math.round(steady[Math.floor(steady.length / 2)] * 10000) / 10000 : 0,
    error,
  }),
);
