// scaffold <project.json> <outdir>   -> scaffold.h, unit prompts
// assemble <outdir> <out.h>          -> scaffold + L*.cpp units
import fs from "node:fs";
import path from "node:path";
import { deserializeProject } from "@/lib/lab/serialize";
import { buildHExport, assembleH } from "@/lib/lab/hExport";

const [cmd, a, b] = process.argv.slice(2);
if (cmd === "scaffold") {
  const project = deserializeProject(fs.readFileSync(a, "utf8"));
  if (!project) throw new Error("bad project");
  const ex = buildHExport({
    name: project.name ?? "HANYEONGHOE",
    matrix: project.matrix,
    layers: project.layers,
    knobs: project.knobs,
    ranges: project.ranges,
    knobLabels: project.knobLabels,
  });
  fs.mkdirSync(b, { recursive: true });
  fs.writeFileSync(path.join(b, "scaffold.h"), ex.scaffold);
  for (const u of ex.units) fs.writeFileSync(path.join(b, `prompt_L${u.index}.md`), u.prompt);
  console.log(JSON.stringify({ ns: ex.namespaceName, stack: ex.stackNames, units: ex.units.map((u) => [u.index, u.name, u.usesValueField]) }));
} else if (cmd === "assemble") {
  const scaffold = fs.readFileSync(path.join(a, "scaffold.h"), "utf8");
  const units: Record<number, string> = {};
  for (const f of fs.readdirSync(a)) {
    const m = f.match(/^L(\d+)\.cpp$/);
    if (m) units[Number(m[1])] = fs.readFileSync(path.join(a, f), "utf8");
  }
  fs.writeFileSync(b, assembleH(scaffold, units));
  console.log("assembled", Object.keys(units));
}
