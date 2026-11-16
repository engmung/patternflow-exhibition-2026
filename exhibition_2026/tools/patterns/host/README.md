# Host parity and timing harness

This harness checks whether a translated exhibit pattern header
(`patterns/<n>_<key>/<key>.h`) draws the same
frames as the Pattern Lab JS beside it, and it reports how long a frame takes. It needs no
hardware.

```
python tools/patterns/host/pfhost.py run --header <n>.h --ns <NAMESPACE> --js <pattern.js> [--standard] [--script "..."]
python tools/patterns/host/pfhost.py current [bakwan,...]  # each pattern folder's .h vs its .js (all six by default)
python tools/patterns/host/pfhost.py validate-old          # the six first-export headers vs patterns/_original/js
python tools/patterns/host/pfhost.py expand --script "..." # print the per-frame table a script becomes
```

Run it from `exhibition_2026/` (any folder works; the paths are resolved from the
script). Output goes to `tools/patterns/host/out/<name>/`, which git ignores.
You get `<script>.png` (strips) and `summary.json` (per-frame numbers).

## What it does

1. **Native.** `runner.cpp` compiles with the header at `-O2` (`/O2` under MSVC;
   the compiler comes from `compiler_environment()` in the clone's
   `firmware/toolchain/check_module_elf.py`, the same lookup
   `tools/check_exhibit_feature.py` uses). It builds against the **real**
   portable core headers from the clone: `core_math.h`, `core_color.h`,
   `core_noise.h`, `core_tables.h`, `core_mem.h`, `core_params.h` →
   `abi/pf_params.h`, and `core_canvas.h` with its real `setFrame` and rotation.
   It also uses the real `InputFrame` struct, cut out of `core_encoders.h`.
   `stubs/` covers only the hardware: `Arduino.h` (a deterministic clock, an LCG
   `random`), `config.h`, the HUB75 driver, and `core_loop_sync.h`. Builds are
   cached by content under `%TEMP%/pf-host-build/`, so an edited header rebuilds
   by itself.
   - The frame is the one the exhibit build hands a pattern
     (`core_exhibit.h driveLanes` + `patternflow.ino applyLaneMotion`):
     - K1-K3 arrive as lanes, `knobAudioActive = !btnPressed[i]` and
       `knobAudioValue = position`, so `PFParams::apply` gives
       `lo + (hi-lo)*pos`.
     - `knobDeltas` carries the core's lane motion (scale 48), which PFParams
       ignores.
     - K4 never arrives.
     - In the frame where a knob's button is pressed, that knob has no lane, so
       its parameter keeps the previous frame's value.
     - `dt = 1/30` and `now` advances 33 ms per frame.
   - `setup()` runs once. Each frame then runs `update(dt, input)` + `draw()`.
     A preset calls `PFCanvas::present()` itself. The stub driver's
     `blitRGB888` hook reads the frame back at that moment, while `setFrame` is
     still set, and undoes the panel mapping. Captures therefore come out in the
     pattern's **logical orientation**, the same one the JS canvas uses (64×128
     for a portrait pattern). If a header never presents, the harness reads the
     canvas after `draw()` and reports `noPresent > 0` in `summary.json`.
   - **ms/frame** is the median of `update + draw` (present included, the blit is
     a no-op). Setup, capture frames and the first 10 frames are excluded. The
     `centre` script (and any script tagged `bench`) runs another `--bench`
     frames (default 300) at its final positions, which gives a stable median.
2. **JS.** `dump.ts` runs the pattern in the web's own `PatternRuntime`, the same
   runtime Pattern Lab and `tools/patterns/render.ts` use, via the main repo's
   `web/node_modules/tsx` (set `PF_WEB` to point elsewhere).
   - `knobValues = lo + (hi-lo)*pos`, with lo and hi taken from the JS `@knobs`
     line.
   - Presses land on the same frames, and a pressed knob holds its value as it
     does on the device.
   - `time = frame/30`.
   - K4 is set to the value the C++ K4 variable keeps. pfhost reads it out of
     the header: it is the `&var` handed to `PFParams::apply(input, 3, …)`, or
     `var[3]` for a loop. Override it with `k4=` in a script.
   - Alpha is flattened onto black.
3. **Compare.** For each captured frame:
   - **MAD**: mean absolute difference, 0..255, over all channels.
   - **>24**: % of pixels whose worst channel is off by more than 24.
   - **±1px>24**: the same, but a pixel counts as right if any JS pixel within
     one pixel of it matches. This separates edges that moved by a pixel from
     pixels that are actually wrong.
   - **worst**: the frame with the highest MAD.

   The strip has one column per captured frame and three rows: JS, C++, and
   |diff|×4. Each run prints one summary line per script, with C++ and JS
   ms/frame.

## Scripts

Clauses are separated by `|`. Frames count from 1, and 30 frames = 1 s. Give a
script a tag with `tag=...` (`--script` can be repeated).

| clause | meaning |
|---|---|
| `240f` | length. The default is the last frame any clause names. |
| `0.5,0.5,0.5` | K1-K3 positions from frame 1 (default 0.5 each) |
| `1,0.5,0.5@90` | jump at frame 90 |
| `0,0,0->1,1,1@30..120` | linear move over frames 30..120 |
| `k2:0.2@60`, `k2:0->1@30..90` | the same, for one knob |
| `press1@60,90` | K1 pressed (btnPressed + btnHeld) on frames 60 and 90 |
| `hold2@60..90` | K2 goes down at 60 and is held until 90 |
| `capture@1,30,60`, `capture@10..20`, `capture@every15`, `capture@last` | frames to dump. Without this, only the last frame is dumped. |
| `k4=0.8` | JS K4 value (the default is what the C++ keeps) |
| `raw` | do not quantise positions to the 32 detents (quantising is the default, since a hand can only stop on a detent) |

Example:
`--script "turn=180f | 0.5,0.5,0.5 | k1:0.5->1@30..90 | press3@120 | capture@every15"`

**Standard set** (`--standard`, or the default when no `--script` is given):
`centre` (0.5×3, 240 f, plus the bench frames), `min` (0×3), `max` (1×3), `sweep`
(K1, then K2, then K3 each 0→1 over 90 frames) and `press` (K1/K2/K3 pressed at
60/120/180, captured on the frame before, the frame of and the frame after each
press).

To check a new header against its new JS:

```
python tools/patterns/host/pfhost.py current jeongeungyo
```

`validate-old` reads the six headers from clone commit `013fd9b` by default, so
the baseline does not change while the files are being replaced. Use
`--rev WORKTREE` to run the files on disk instead, and `--only a,b` to run a
subset.

The header line also prints a small **lint**:
- `double` types, unsuffixed float literals, and double-libm calls (`sin(`, `pow(`…).
  All of these should be 0.
- Two cost hints: the number of `/` operators (the S3's FPU has no divider, so
  each float divide is a `__divsf3` call) and the number of float libm calls.
  These cost almost nothing on the host but a lot on the device.

## What "good parity" looks like (measured on the old headers vs the original JS)

| pattern | centre MAD / >24 / ±1px | worst script | C++ ms/f (host) | JS ms/f | reading |
|---|---|---|---|---|---|
| HEOEUNBIN | 0.5 / 0.3% / 0.0% | 0.5 MAD | 0.138 (max 0.268) | 0.33 | faithful: rounding only |
| JEONGEUNGYO | 0.2 / 0.1% / 0.0% | min 1.7 MAD, 1.0% | **0.290** | 0.88 | faithful: fast sin/cos edge pixels |
| HANYEONGHOE | 0.02 / 0% / 0% | max 9.1 MAD, 12% | 0.079 | 0.33 | exact at centre/min/press; the cloud layer diverges at K1-K3 = max |
| YUJIN | 2.3 / 2.5% / 0.7% | press 3.3 → 8.5 MAD at f300 | **0.398** | 2.40 | background exact (scattered 1-LSB ramp dots); fish paths drift apart over time |
| BAKWAN | 6.4 / 16% / 11% | sweep 20.5 MAD | 0.099 | 0.97 | translation differs: ground brightness/texture and branch shapes, from frame 1 |
| CHOEGYEONGEUN | 13.5 / 21% / 16% | max 26 MAD, f1 70 MAD | 0.089 | 0.59 | translation differs: the zoom/door state starts elsewhere (f1), and flowers spawn in other places |

How to read these numbers:
- **A faithful translation** reaches MAD < ~1, >24 < ~1%, and **±1px>24 ≈ 0%** on
  every script, and the differences do not grow over time. The residue comes
  from:
  - JS doubles vs device float32;
  - `PFMath::fastSin/fastCos/fastAtan2` LUT approximations, which move an edge
    by one pixel (this is exactly what the ±1px column absorbs);
  - ramp LUT rounding, which shows up as isolated 1-LSB dots in the ×4 diff.
- **Chaotic state** (flocking fish, particles) starts at parity and then drifts,
  because float32 and double disagree and the difference amplifies. The
  signature is a low MAD on early frames that grows with the frame number,
  while static layers stay exact. To check the logic, compare early captures
  (≤ 2 s) or the non-particle layers. Do not expect late frames to match.
- **A real translation error** is visible at `f1`, or it appears only at one knob
  extreme (a mis-mapped range, clamp or smoothing start). The ±1px column stays
  high because the pixels are wrong, not just shifted. The old BAKWAN and
  CHOEGYEONGEUN headers, and HANYEONGHOE at max, are in this class.

## Timing caveats

- Host ms are **relative**. Use them to rank patterns and compare a new header
  with its old one. They are not device ms: the ESP32-S3 runs at 240 MHz with a
  single-precision FPU and **no hardware divide**. Division-heavy and
  libm-heavy code (per-pixel `/`, `expf`, `sqrtf`) is far cheaper on x86 than on
  the device, so the host understates exactly the patterns that are slow there.
  JEONGEUNGYO (per-pixel division per segment) and YUJIN (per-pixel `expf`) are
  the two slowest on the host too. The new headers should come in at or below
  those numbers, and their cost-hint counts should go down.
- PSRAM latency is not modelled: `PFMem` is plain `malloc` here.
- The device only confirms a result through the `/status` frame rate (README.md).
