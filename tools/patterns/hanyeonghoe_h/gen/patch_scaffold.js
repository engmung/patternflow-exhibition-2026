// exhibit: bit-identical speedups to the generated scaffold's pipeline.
// Every edit is an exact string replacement that must match once.
const fs = require("fs");
const [file] = process.argv.slice(2);
let h = fs.readFileSync(file, "utf8");
function rep(a, b) {
  const i = h.indexOf(a);
  if (i < 0 || h.indexOf(a, i + 1) >= 0) throw new Error("patch anchor not unique: " + a.slice(0, 80));
  h = h.slice(0, i) + b + h.slice(i + a.length);
}

rep(
  "static uint8_t* layerBuf[LAYER_N] = {nullptr, nullptr, nullptr};",
  `static uint8_t* layerBuf[LAYER_N] = {nullptr, nullptr, nullptr};
// exhibit: alpha255[i] = i / 255.0f, the composite's per-layer alpha. Each '/' is a
// __divsf3 call on the S3 (no hardware divide): three per pixel, 24,576 per frame. The
// table holds the very same quotients, so the picture is bit-identical. 1 KB -> PFMem.
static float* alpha255 = nullptr;`,
);

rep(
  `    if (!layerBuf[i]) layerBuf[i] = (uint8_t*)PFMem::alloc((size_t)PIX_N * 4);
  for (int i = 0; i < LAYER_N; i++)
    if (!layerBuf[i]) return; // allocation failed → draw() degrades to blank`,
  `    if (!layerBuf[i]) layerBuf[i] = (uint8_t*)PFMem::alloc((size_t)PIX_N * 4);
  for (int i = 0; i < LAYER_N; i++)
    if (!layerBuf[i]) return; // allocation failed → draw() degrades to blank
  if (!alpha255) alpha255 = PFMem::allocFloats(256);
  if (!alpha255) return;
  for (int i = 0; i < 256; i++) alpha255[i] = (float)i / 255.0f;`,
);

rep(
  `    if (!layerBuf[i]) { PFCanvas::present(); return; }
`,
  `    if (!layerBuf[i]) { PFCanvas::present(); return; }
  if (!alpha255) { PFCanvas::present(); return; }
`,
);

// L1 only ever writes rows 0 .. L1_ROWS-1 (PFMem zeroed the rest), so only those are cleared.
rep(
  "  memset(layerBuf[1], 0, (size_t)PIX_N * 4);",
  `  // exhibit: the cloud layer never writes below row L1::L1_ROWS (66) and PFMem zeroed
  // the buffer, so only its rows are cleared: 16.9 KB of PSRAM writes instead of 32.8.
  memset(layerBuf[1], 0, (size_t)L1::L1_ROWS * FRAME_W * 4);`,
);

rep(
  "        const float a = (p[3] / 255.0f) * G_OPACITY[gi];",
  "        const float a = alpha255[p[3]] * G_OPACITY[gi]; // exhibit: was (p[3] / 255.0f)",
);

fs.writeFileSync(file, h);
