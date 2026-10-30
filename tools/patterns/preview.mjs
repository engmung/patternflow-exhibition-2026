// Builds patterns/preview.html: the six exhibition patterns running in the
// browser the way the exhibit firmware drives them - K1-K3 as absolute
// positions in 32 detents starting at the middle, K4 stepping the patterns,
// the panel stood on its end (a 128x64 pattern shows turned 90 degrees
// counter-clockwise, as core_canvas.h maps it), the show's JS and the
// original side by side, and every pattern's JS and .h one click from the
// clipboard.
//
//   node tools/patterns/preview.mjs            (from the exhibition_2026 root)
//
// It reads each pattern folder, patterns/<n>_<key>/: <key>.js (what the panel
// shows), <key>.h (what the firmware compiles), an optional
// <key>_landscape.js (a reference version) - and patterns/_original/js/<key>.js.
// Run it again after changing any of them.
//
// The runtime is the web's own PatternRuntime, bundled by esbuild from the
// main repo's web/ (PF_WEB overrides where that is).
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../..");
const patterns = path.join(root, "patterns");
const web = path.resolve(process.env.PF_WEB ?? path.join(root, "../랜딩페이지_opensorce/web"));
const bundle = path.join(patterns, ".pf-runtime.js");

execFileSync(
  path.join(web, "node_modules/.bin/esbuild" + (process.platform === "win32" ? ".cmd" : "")),
  [
    path.join(here, "preview-entry.ts"),
    "--bundle", "--format=iife", "--minify", "--platform=browser",
    "--alias:@=./src", "--tsconfig=./tsconfig.json", `--outfile=${bundle}`, "--log-level=warning",
  ],
  { cwd: web, stdio: "inherit", shell: process.platform === "win32" },
);
const runtime = fs.readFileSync(bundle, "utf8");
fs.rmSync(bundle);

// What each pattern's knob presses do, shown beside the panel.
const PRESS = {
  bakwan: "K1 누르기: 다음 나무",
  choegyeongeun: "K1 누르기: 문이 닫혔다 열리며 다음 꽃 장면 · K1을 끝까지 내렸다 올려도 다음 장면",
  hanyeonghoe: "K1 누르기: 돌풍",
  heoeunbin: "K1 누르기: 방향 바꾸기 · K2 누르기: 쥐 태우기/내리기 · 발이 닿을 때마다 MIDI 노트(채널 10, 36/38)",
  jeongeungyo: "K1 누르기: 흩어졌다 다시 모이기",
  yujin: "K1 누르기: 물고기가 놀라 흩어짐",
};

const read = (p) => (fs.existsSync(p) ? fs.readFileSync(p, "utf8") : null);
// K4 order = the folder numbers (the same order as exhibit_presets.h's rows).
const folders = fs.readdirSync(patterns, { withFileTypes: true })
  .filter((d) => d.isDirectory() && /^\d+_/.test(d.name))
  .map((d) => d.name)
  .sort((a, b) => parseInt(a, 10) - parseInt(b, 10));

const data = folders.map((folder) => {
  const key = folder.replace(/^\d+_/, "");
  const dir = path.join(patterns, folder);
  const versions = [{ id: "show", label: "전시용", file: `${folder}/${key}.js`, code: read(path.join(dir, `${key}.js`)) }];
  const landscape = read(path.join(dir, `${key}_landscape.js`));
  if (landscape) versions.push({ id: "landscape", label: "참고 · 가로 버전", file: `${folder}/${key}_landscape.js`, code: landscape });
  versions.push({ id: "orig", label: "원본", file: `_original/js/${key}.js`, code: read(path.join(patterns, "_original", "js", `${key}.js`)) });
  return {
    key,
    title: key.toUpperCase(),
    press: PRESS[key] ?? "",
    h: read(path.join(dir, `${key}.h`)),
    hFile: `${folder}/${key}.h`,
    versions,
  };
});

const template = fs.readFileSync(path.join(here, "preview.template.html"), "utf8");
const json = JSON.stringify(data).replace(/</g, "\\u003c");
const html = template
  .replace("/*__RUNTIME__*/", () => runtime.replace(/<\/script/gi, "<\\/script"))
  .replace("/*__DATA__*/null", () => json);
const out = path.join(patterns, "preview.html");
fs.writeFileSync(out, html);
console.log(`wrote ${out} (${Math.round(html.length / 1024)} KB)`);
