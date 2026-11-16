// dabs.js <layer_Cloud.js>
// Runs the Cloud layer's own setup() and prints its DABS table as C++ float
// literals (the block that replaces @@DABS@@ in units/L1.tpl). The JS builds the
// dabs from a double-precision sine hash that float32 cannot reproduce, so the
// finished table is baked. Every literal must round-trip to the same float32.
"use strict";
const fs = require("fs");
const code = fs.readFileSync(process.argv[2], "utf8").replace(/\bexport\s+function\b/g, "function");
const setup = new Function(code + "\nreturn setup;")();
const params = {};
setup(params);
const f = (v) => {
  const x = Math.fround(v);
  let s = x.toPrecision(9);
  if (Number(s) === 0) s = "0.0";
  if (!/[.e]/.test(s)) s += ".0";
  if (Math.fround(Number(s)) !== x) throw new Error("no round trip " + v);
  return s + "f";
};
const out = [];
out.push("  // ox, oy, rx, ry, phase, pulseFreq — 16 per cloud, from the JS setup() run in double precision");
out.push(`  static const Dab DABS[L1_CLOUD_N * L1_DAB_N] = {`);
params.clouds.forEach((c, ci) => {
  out.push(`    // cloud ${ci} (seed ${c.seed})`);
  for (const d of c.dabs) out.push(`    { ${[d.ox, d.oy, d.rx, d.ry, d.phase, d.pulseFreq].map(f).join(", ")} },`);
});
out.push("  };");
console.log(out.join("\n"));
