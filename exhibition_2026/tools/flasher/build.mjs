// Builds flasher.html at the exhibition repo root: one offline page that
// flashes the exhibit panel and FlowLocal from a browser (Chrome / Edge, Web
// Serial) - esptool-js bundled in, and the images from firmware_images/
// embedded, so a double-clicked file is all it takes.
//
//   cd tools/flasher && npm install      (once)
//   node tools/flasher/build.mjs         (from the exhibition_2026 root, after firmware_images/ changes)
//
// The page carries the exhibit images, and so the hotspot password: keep it in
// this private repository.
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../..");
const images = path.join(root, "firmware_images");

const bundled = await build({
  entryPoints: [path.join(here, "entry.js")],
  bundle: true,
  format: "iife",
  minify: true,
  platform: "browser",
  // crypto-js probes require("crypto") inside a try; the browser has no such module.
  external: ["crypto"],
  write: false,
  logLevel: "warning",
});
const lib = bundled.outputFiles[0].text;

function git(args) {
  try {
    return execFileSync("git", ["-C", root, ...args], { encoding: "utf8" }).trim();
  } catch {
    return "";
  }
}

const FILES = {
  panel_bootloader: "panel/bootloader.bin",
  panel_partitions: "panel/partitions.bin",
  panel_boot_app0: "panel/boot_app0.bin",
  panel_firmware: "panel/firmware.bin",
  flowlocal_factory: "flowlocal/firmware.factory.bin",
};

const data = { images: {}, built: "" };
for (const [key, rel] of Object.entries(FILES)) {
  const file = path.join(images, rel);
  const bytes = fs.readFileSync(file);
  data.images[key] = {
    name: rel,
    size: bytes.length,
    sha256: crypto.createHash("sha256").update(bytes).digest("hex"),
    date: git(["log", "-1", "--format=%cs", "--", `firmware_images/${rel}`]) || "(커밋 전)",
    b64: bytes.toString("base64"),
  };
}
const head = git(["rev-parse", "--short", "HEAD"]);
const now = new Date();
const p2 = (n) => String(n).padStart(2, "0");
data.built = `${now.getFullYear()}-${p2(now.getMonth() + 1)}-${p2(now.getDate())} ${p2(now.getHours())}:${p2(now.getMinutes())}, 레포 ${head || "?"}`;

// The SHA256SUMS beside the images must agree with what goes into the page.
const sums = fs.readFileSync(path.join(images, "SHA256SUMS"), "utf8");
for (const [key, rel] of Object.entries(FILES)) {
  const line = sums.split(/\r?\n/).find((l) => l.endsWith(rel));
  if (!line || !line.startsWith(data.images[key].sha256)) {
    throw new Error(`${rel}: not what firmware_images/SHA256SUMS says - update the sums first`);
  }
}

const template = fs.readFileSync(path.join(here, "flasher.template.html"), "utf8");
const json = JSON.stringify(data).replace(/</g, "\\u003c");
const html = template
  .replace("/*__LIB__*/", () => lib.replace(/<\/script/gi, "<\\/script"))
  .replace("/*__DATA__*/null", () => json);
const out = path.join(root, "flasher.html");
fs.writeFileSync(out, html);
console.log(`wrote ${out} (${(html.length / 1048576).toFixed(1)} MB)`);
for (const [key, img] of Object.entries(data.images)) {
  console.log(`  ${img.name.padEnd(32)} ${String(img.size).padStart(9)} B  sha ${img.sha256.slice(0, 12)}  ${img.date}`);
}
