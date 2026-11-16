// Headless renderer for Pattern Lab JS patterns, for checking knob work
// without a browser or a panel.
//
// It runs the pattern through the web's own PatternRuntime (the same code the
// Pattern Lab preview uses), drives K1-K3 the way the exhibit firmware does -
// one absolute position 0..1 per knob, value = min + (max - min) * position,
// ranges from the @knobs line - and writes a contact sheet PNG plus metrics.
//
// Run from the main repo's web/ directory (it resolves "@/..." through
// web/tsconfig.json):
//
//   cd <main repo>/web
//   npx tsx <exhibition_2026>/tools/patterns/render.ts <pattern.js> --out <dir>
//       [--configs "0.5,0.5,0.5;0,0.5,0.5;1,0.5,0.5"]   positions K1,K2,K3
//       [--times 1,3,6]        seconds at which to capture a frame
//       [--sweep]              also sweep each knob 0..1 (others at 0.5)
//       [--turn]               turn each knob detent by detent, report jumps
//       [--scale 4] [--name label] [--fps 30]
//
// Metrics per config (in <dir>/<name>.metrics.json):
//   meanLum   mean luminance 0..1 of the last frame
//   litFrac   fraction of pixels brighter than 0.06
//   motion    mean |frame(t) - frame(t - 1/fps)| over the last second, 0..1
//   nonFinite setValue/setPixel calls that received NaN/Infinity (whole run)
//   msFrame   mean ms per update+draw in Node (a relative cost figure only)

import fs from "node:fs";
import path from "node:path";
import sharp from "sharp";
import {
  PatternRuntime,
  type ColorRamp,
  type PatternInput,
} from "@/lib/pattern/harness";
import { parseKnobsAnnotation } from "@/lib/pattern/knobs";
import { parseMatrixAnnotation } from "@/lib/pattern/matrix";
import { parseRampAnnotation } from "@/lib/pattern/ramp";
import { hexToRgb } from "@/lib/pattern/color";

type Metrics = {
  config: number[];
  values: number[];
  meanLum: number;
  litFrac: number;
  motion: number;
  nonFinite: number;
  msFrame: number;
  error?: string;
};

function arg(name: string, fallback?: string): string | undefined {
  const i = process.argv.indexOf(`--${name}`);
  if (i < 0) return fallback;
  return process.argv[i + 1];
}

const file = process.argv[2];
if (!file || file.startsWith("--")) {
  console.error("usage: render.ts <pattern.js> --out <dir> [--configs ...] [--times ...] [--sweep]");
  process.exit(2);
}
const outDir = arg("out", path.join(path.dirname(file), "renders"))!;
const name = arg("name", path.basename(file).replace(/\.js$/, ""))!;
const scale = Number(arg("scale", "4"));
const fps = Number(arg("fps", "30"));
const times = (arg("times", "1,3,6") as string).split(",").map(Number);
const configs = (arg("configs", "0.5,0.5,0.5") as string)
  .split(";")
  .map((c) => c.split(",").map(Number));
const doSweep = process.argv.includes("--sweep");

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

function valuesFor(pos: number[]): number[] {
  return [0, 1, 2, 3].map((i) => {
    const r = ranges[i];
    const p = i < 3 ? Math.max(0, Math.min(1, pos[i] ?? 0.5)) : 0;
    return r[0] + (r[1] - r[0]) * p;
  });
}

function lum(data: Uint8ClampedArray, i: number) {
  return (0.2126 * data[i] + 0.7152 * data[i + 1] + 0.0722 * data[i + 2]) / 255;
}

/** Run one config; returns frames captured at `captureAt` seconds and metrics. */
function run(pos: number[], captureAt: number[]) {
  const rt = new PatternRuntime(matrix.width, matrix.height);
  if (ramp) rt.setRamp(ramp);
  rt.recolor = Boolean(rampAnn?.recolor && ramp);
  let nonFinite = 0;
  // Count non-finite writes by wrapping the runtime's display.
  const disp = (rt as unknown as { display: { setValue: Function; setPixel: Function } }).display;
  const sv = disp.setValue;
  const sp = disp.setPixel;
  disp.setValue = (x: number, y: number, v: number) => {
    if (!Number.isFinite(v)) nonFinite++;
    return sv(x, y, v);
  };
  disp.setPixel = (x: number, y: number, r: unknown, g?: number, b?: number) => {
    if (Array.isArray(r)) {
      if (!r.every((c) => Number.isFinite(c))) nonFinite++;
    } else if (![r, g, b].every((c) => Number.isFinite(c as number))) nonFinite++;
    return sp(x, y, r, g, b);
  };
  const load = rt.loadCode(code);
  const values = valuesFor(pos);
  const metrics: Metrics = {
    config: pos,
    values: values.map((v) => Math.round(v * 1000) / 1000),
    meanLum: 0,
    litFrac: 0,
    motion: 0,
    nonFinite: 0,
    msFrame: 0,
  };
  const frames: Uint8ClampedArray[] = [];
  if (!load.ok) {
    metrics.error = load.error;
    return { frames, metrics };
  }
  const total = Math.max(...captureAt);
  const frameCount = Math.round(total * fps);
  const captureFrames = new Set(captureAt.map((t) => Math.round(t * fps)));
  let prev: Uint8ClampedArray | null = null;
  let motionSum = 0;
  let motionN = 0;
  let ms = 0;
  for (let f = 1; f <= frameCount; f++) {
    const input: PatternInput = {
      knobDeltas: [0, 0, 0, 0],
      knobValues: values,
      knobNormalized: [pos[0] ?? 0.5, pos[1] ?? 0.5, pos[2] ?? 0.5, 0],
      knobRanges: ranges,
      btnPressed: [false, false, false, false],
      btnHeld: [false, false, false, false],
    };
    const t0 = performance.now();
    const res = rt.renderFrame(1 / fps, f / fps, input);
    ms += performance.now() - t0;
    if (!res.ok) {
      metrics.error = res.error;
      break;
    }
    const cur = rt.data;
    if (prev && f > frameCount - fps) {
      let d = 0;
      for (let i = 0; i < cur.length; i += 4) d += Math.abs(lum(cur, i) - lum(prev, i));
      motionSum += d / (cur.length / 4);
      motionN++;
    }
    if (f > frameCount - fps - 1) prev = new Uint8ClampedArray(cur);
    if (captureFrames.has(f)) frames.push(new Uint8ClampedArray(cur));
  }
  const last = rt.data;
  let sum = 0;
  let lit = 0;
  for (let i = 0; i < last.length; i += 4) {
    const l = lum(last, i);
    sum += l;
    if (l > 0.06) lit++;
  }
  const n = last.length / 4;
  metrics.meanLum = Math.round((sum / n) * 1000) / 1000;
  metrics.litFrac = Math.round((lit / n) * 1000) / 1000;
  metrics.motion = motionN ? Math.round((motionSum / motionN) * 10000) / 10000 : 0;
  metrics.nonFinite = nonFinite;
  metrics.msFrame = Math.round((ms / frameCount) * 100) / 100;
  return { frames, metrics };
}

async function cell(frame: Uint8ClampedArray | undefined, label: string) {
  const w = matrix.width * scale;
  const h = matrix.height * scale;
  const labelH = 28;
  const base = frame
    ? await sharp(Buffer.from(frame.buffer, frame.byteOffset, frame.byteLength), {
        raw: { width: matrix.width, height: matrix.height, channels: 4 },
      })
        .flatten({ background: "#000000" })
        .resize(w, h, { kernel: "nearest" })
        .png()
        .toBuffer()
    : await sharp({ create: { width: w, height: h, channels: 3, background: "#400000" } }).png().toBuffer();
  const svg = `<svg width="${w}" height="${labelH}"><rect width="100%" height="100%" fill="#202020"/><text x="4" y="19" font-family="monospace" font-size="14" fill="#e0e0e0">${label
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")}</text></svg>`;
  return sharp({ create: { width: w + 4, height: h + labelH + 4, channels: 3, background: "#101010" } })
    .composite([
      { input: Buffer.from(svg), top: 0, left: 0 },
      { input: base, top: labelH, left: 0 },
    ])
    .png()
    .toBuffer();
}

async function sheet(rows: { label: string; frames: (Uint8ClampedArray | undefined)[]; colLabels: string[] }[], out: string) {
  const cells: Buffer[][] = [];
  for (const row of rows) {
    const rc: Buffer[] = [];
    for (let c = 0; c < row.frames.length; c++) rc.push(await cell(row.frames[c], `${row.label} ${row.colLabels[c]}`));
    cells.push(rc);
  }
  const cw = matrix.width * scale + 4;
  const ch = matrix.height * scale + 28 + 4;
  const cols = Math.max(...cells.map((r) => r.length));
  const composite: sharp.OverlayOptions[] = [];
  cells.forEach((r, ri) => r.forEach((b, ci) => composite.push({ input: b, top: ri * ch, left: ci * cw })));
  await sharp({ create: { width: cols * cw, height: cells.length * ch, channels: 3, background: "#000000" } })
    .composite(composite)
    .png()
    .toFile(out);
}

async function main() {
  fs.mkdirSync(outDir, { recursive: true });
  const all: Metrics[] = [];
  const rows = [];
  for (const cfg of configs) {
    const { frames, metrics } = run(cfg, times);
    all.push(metrics);
    rows.push({
      label: `[${cfg.map((p) => p.toFixed(2)).join(",")}]`,
      frames: times.map((_, i) => frames[i]),
      colLabels: times.map((t) => `t${t}`),
    });
  }
  await sheet(rows, path.join(outDir, `${name}.png`));

  const sweep: Record<string, Metrics[]> = {};
  if (doSweep) {
    const positions = [0, 2, 4, 8, 12, 16, 20, 24, 28, 30, 32].map((d) => d / 32);
    for (let k = 0; k < 3; k++) {
      const label = knobs[k]?.name ?? `K${k + 1}`;
      sweep[label] = [];
      const frames: Uint8ClampedArray[] = [];
      for (const p of positions) {
        const cfg = [0.5, 0.5, 0.5];
        cfg[k] = p;
        const tEnd = Math.max(...times);
        const { frames: fr, metrics } = run(cfg, [tEnd]);
        sweep[label].push(metrics);
        frames.push(fr[0]);
      }
      await sheet(
        [{ label: `K${k + 1} ${label}`, frames, colLabels: positions.map((p) => `${Math.round(p * 100)}%`) }],
        path.join(outDir, `${name}.sweep-k${k + 1}.png`),
      );
    }
  }

  // --turn: a visitor turning each knob one detent every 3 frames from the
  // middle to the top, down to the bottom and back to the middle. A knob that
  // jumps (a rebuilt scene, a time base that is multiplied instead of
  // accumulated) shows as a per-frame change far above the steady motion.
  const turn: Record<string, { steadyMax: number; turnMax: number; turnP99: number; worstAtPos: number }> = {};
  if (process.argv.includes("--turn")) {
    for (let k = 0; k < 3; k++) {
      const label = knobs[k]?.name ?? `K${k + 1}`;
      const script: number[] = [];
      const hold = (p: number, frames: number) => { for (let i = 0; i < frames; i++) script.push(p); };
      hold(0.5, fps * 2);
      for (let d = 16; d <= 32; d++) hold(d / 32, 3);
      for (let d = 31; d >= 0; d--) hold(d / 32, 3);
      for (let d = 1; d <= 16; d++) hold(d / 32, 3);
      hold(0.5, fps);
      const rt = new PatternRuntime(matrix.width, matrix.height);
      if (ramp) rt.setRamp(ramp);
      rt.recolor = Boolean(rampAnn?.recolor && ramp);
      rt.loadCode(code);
      let prev: Uint8ClampedArray | null = null;
      const diffs: number[] = [];
      let err: string | undefined;
      script.forEach((p, f) => {
        const pos = [0.5, 0.5, 0.5];
        pos[k] = p;
        const res = rt.renderFrame(1 / fps, (f + 1) / fps, {
          knobDeltas: [0, 0, 0, 0],
          knobValues: valuesFor(pos),
          knobNormalized: [pos[0], pos[1], pos[2], 0],
          knobRanges: ranges,
          btnPressed: [false, false, false, false],
          btnHeld: [false, false, false, false],
        });
        if (!res.ok && !err) err = res.error;
        const cur = rt.data;
        if (prev) {
          let d = 0;
          for (let i = 0; i < cur.length; i += 4) d += Math.abs(lum(cur, i) - lum(prev, i));
          diffs.push(d / (cur.length / 4));
        } else diffs.push(0);
        prev = new Uint8ClampedArray(cur);
      });
      const steady = diffs.slice(fps, fps * 2);
      const turning = diffs.slice(fps * 2, diffs.length - fps);
      const sorted = [...turning].sort((a, b) => a - b);
      const worst = turning.indexOf(Math.max(...turning));
      turn[label] = {
        steadyMax: Math.round(Math.max(...steady) * 10000) / 10000,
        turnMax: Math.round(Math.max(...turning) * 10000) / 10000,
        turnP99: Math.round(sorted[Math.floor(sorted.length * 0.99)] * 10000) / 10000,
        worstAtPos: script[fps * 2 + worst],
      };
      if (err) (turn[label] as Record<string, unknown>).error = err;
    }
  }

  fs.writeFileSync(
    path.join(outDir, `${name}.metrics.json`),
    JSON.stringify({ file, matrix, knobs, configs: all, sweep, turn }, null, 1),
  );
  const fmt = (m: Metrics) =>
    `[${m.config.map((p) => p.toFixed(2)).join(",")}] vals=${m.values.slice(0, 3).join(",")} lum=${m.meanLum} lit=${m.litFrac} motion=${m.motion} nonFinite=${m.nonFinite} ms=${m.msFrame}${m.error ? " ERROR " + m.error : ""}`;
  console.log(`${name}: ${matrix.width}x${matrix.height} knobs=${JSON.stringify(knobs)}`);
  all.forEach((m) => console.log("  " + fmt(m)));
  for (const [k, list] of Object.entries(sweep)) {
    console.log(`  sweep ${k}:`);
    list.forEach((m) => console.log("    " + fmt(m)));
  }
  for (const [k, r] of Object.entries(turn)) {
    console.log(`  turn ${k}: steadyMax=${r.steadyMax} turnMax=${r.turnMax} turnP99=${r.turnP99} worstAtPos=${r.worstAtPos}`);
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
