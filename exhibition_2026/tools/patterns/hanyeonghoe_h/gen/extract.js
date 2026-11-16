// extract.js <project.json> <sources.sha256> <outdir> [--print-hashes]
//
// Writes each code layer's JavaScript out of the project JSON as
// <outdir>/layer_<name>.js, then checks it against the hash the hand translation
// was written for (units/sources.sha256). Any mismatch, a missing layer or an
// unrecorded code layer is a hard failure: the hand units would silently draw the
// old layer. --print-hashes lists the JSON's current hashes and exits without
// checking (for updating sources.sha256 after a hand update).
"use strict";
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const args = process.argv.slice(2);
const printOnly = args.includes("--print-hashes");
const [projectPath, hashPath, outDir] = args.filter((a) => !a.startsWith("--"));

const project = JSON.parse(fs.readFileSync(projectPath, "utf8"));
const codeLayers = project.layers.filter((l) => l.type === "code");
const sha = (s) => crypto.createHash("sha256").update(s, "utf8").digest("hex");

if (printOnly) {
  for (const l of codeLayers) console.log(`${l.name}\t${sha(l.code)}`);
  process.exit(0);
}

const recorded = new Map(); // layer name -> { unit, hash }
for (const line of fs.readFileSync(hashPath, "utf8").split(/\r?\n/)) {
  const t = line.trim();
  if (!t || t.startsWith("#")) continue;
  const [unit, layer, hash] = t.split(/\s+/);
  recorded.set(layer, { unit, hash });
}

fs.mkdirSync(outDir, { recursive: true });
const errors = [];
for (const l of codeLayers) {
  const file = path.join(outDir, `layer_${l.name}.js`);
  fs.writeFileSync(file, l.code);
  const rec = recorded.get(l.name);
  const h = sha(l.code);
  if (!rec) {
    errors.push(`code layer "${l.name}" has no hand translation recorded in ${hashPath}`);
  } else if (rec.hash !== h) {
    errors.push(
      `layer "${l.name}" JS changed since units/${rec.unit} was translated\n` +
        `      recorded ${rec.hash}\n      now      ${h}\n` +
        `      -> update units/${rec.unit} by hand (diff the old and new layer JS), then the hash`,
    );
  }
  recorded.delete(l.name);
}
for (const [name, rec] of recorded) errors.push(`units/${rec.unit} names layer "${name}", which the project no longer has`);

if (errors.length) {
  console.error("\nhanyeonghoe_h: REFUSING TO BUILD — the hand translations are stale:\n");
  for (const e of errors) console.error("  - " + e);
  console.error("");
  process.exit(1);
}
console.log(`layer JS matches the hand translations (${codeLayers.map((l) => l.name).join(", ")})`);
