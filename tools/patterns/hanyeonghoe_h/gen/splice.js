// splice.js unit <L1.tpl> <dabs.inc> <out L1.cpp>      — put the baked dab table into L1
// splice.js finish <assembled.h> <author.txt> <footer.txt> <out.h> — add the author block and footer
"use strict";
const fs = require("fs");
const [cmd, ...a] = process.argv.slice(2);
const read = (p) => fs.readFileSync(p, "utf8");
function once(s, anchor, repl) {
  const i = s.indexOf(anchor);
  if (i < 0 || s.indexOf(anchor, i + 1) >= 0) throw new Error("anchor not unique: " + JSON.stringify(anchor));
  return s.slice(0, i) + repl + s.slice(i + anchor.length);
}
if (cmd === "unit") {
  const [tpl, inc, out] = a;
  fs.writeFileSync(out, once(read(tpl), "@@DABS@@\n", read(inc)));
} else if (cmd === "finish") {
  const [src, head, foot, out] = a;
  let h = once(read(src), "#pragma once\n", "#pragma once\n\n" + read(head));
  h += "\n\n" + read(foot);
  fs.writeFileSync(out, h);
} else {
  throw new Error("usage: splice.js unit|finish ...");
}
