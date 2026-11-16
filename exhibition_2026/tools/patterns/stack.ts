// The @stack line of a layered Pattern Lab composition, outside the browser.
//
// A layered pattern published from Pattern Lab is the flattened JS plus one
// `// @stack v1 d:<base64 deflate-raw project JSON>` line. Pasting it back into
// Pattern Lab restores the LAYERS from that line and ignores the flattened
// code above it - so an edit has to go into the project, and both the
// flattened code and the line are regenerated from it, exactly as the lab's
// own publish does (flattenLayers + serializeProject).
//
//   cd <main repo>/web
//   NODE_PATH=$PWD/node_modules npx tsx --tsconfig ./tsconfig.json <this> decode <pattern.js> <project.json>
//   ... <this> verify <pattern.js>          regenerate from the line, compare with the file's code
//   ... <this> build <project.json> <out.js>   flattened code + fresh @stack line

import fs from "node:fs";
import zlib from "node:zlib";
import { flattenLayers } from "@/lib/lab/flatten";
import { deserializeProject, serializeProject } from "@/lib/lab/serialize";

const STACK_LINE_RE = /^[ \t]*\/\/[ \t]*@stack[ \t]+v1[ \t]+(d|r):([A-Za-z0-9+/=]+)[ \t]*$/m;

function decodeLine(code: string): string {
  const m = code.match(STACK_LINE_RE);
  if (!m) throw new Error("no @stack line");
  const bytes = Buffer.from(m[2], "base64");
  return (m[1] === "d" ? zlib.inflateRawSync(bytes) : bytes).toString("utf8");
}

function flatten(json: string): { flat: string; stackLine: string } {
  const project = deserializeProject(json);
  if (!project) throw new Error("project JSON did not deserialize");
  const flat = flattenLayers(project.layers, project.matrix, {
    labels: project.knobLabels,
    ranges: project.ranges,
  });
  const serialized = serializeProject({ ...project, editOf: null });
  if (!serialized) throw new Error("project did not serialize");
  const payload = zlib.deflateRawSync(Buffer.from(serialized, "utf8"));
  return { flat, stackLine: `// @stack v1 d:${payload.toString("base64")}` };
}

/** The flattened body of a published file: from @matrix/@knobs down to just before @stack. */
function body(code: string): string {
  const start = code.search(/^\/\/ @(matrix|knobs) /m);
  const end = code.search(STACK_LINE_RE);
  return code.slice(start, end).replace(/\s+$/, "");
}

const [cmd, a, b] = process.argv.slice(2);
if (cmd === "decode") {
  const json = decodeLine(fs.readFileSync(a, "utf8"));
  fs.writeFileSync(b, JSON.stringify(JSON.parse(json), null, 1));
  console.log(`decoded ${a} -> ${b}`);
} else if (cmd === "verify") {
  const code = fs.readFileSync(a, "utf8");
  const { flat } = flatten(decodeLine(code));
  // Trailing spaces on a line are not content (chat and editors drop them).
  const norm = (t: string) => t.replace(/[ \t]+$/gm, "");
  const want = norm(body(code));
  const got = norm(flat.replace(/\s+$/, ""));
  if (want === got) {
    console.log(`OK: the flattened code in ${a} is exactly what its @stack line regenerates (${got.length} chars)`);
  } else {
    let i = 0;
    while (i < want.length && want[i] === got[i]) i++;
    console.log(`MISMATCH at char ${i} (file ${want.length}, regenerated ${got.length})`);
    console.log("file: " + JSON.stringify(want.slice(Math.max(0, i - 80), i + 80)));
    console.log("regen:" + JSON.stringify(got.slice(Math.max(0, i - 80), i + 80)));
    process.exit(1);
  }
} else if (cmd === "build") {
  const { flat, stackLine } = flatten(fs.readFileSync(a, "utf8"));
  fs.writeFileSync(b, `${flat}\n\n${stackLine}\n`);
  console.log(`built ${b}: ${flat.length} chars of code + ${stackLine.length} chars of @stack`);
} else {
  console.error("usage: stack.ts decode|verify|build ...");
  process.exit(2);
}
