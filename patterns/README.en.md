# Exhibition patterns

> **English translation.** The Korean original is [`README.md`](README.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/` and the project's copies of the audio samples are not included — build from source as the [front page](../README.md) shows. The original's `patternflow/` (the panel firmware clone) is what you get by applying [`firmware/panel-exhibit-2026.patch`](../firmware/panel-exhibit-2026.patch) to Patternflow `4d264d5`, inside this repo; its `flowlocal/` (the FlowLocal firmware clone) is [`firmware/flowlocal/`](../firmware/flowlocal/) here, so `flowlocal/EXHIBIT.md` means `firmware/flowlocal/EXHIBIT.md`. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](../README.ko.md), translated in [`docs/setup-notes.en.md`](../docs/setup-notes.en.md).

One folder per pattern. The number is the K4 order (pattern 1 is the one the panel shows when it is switched on).

| Folder | JS (Pattern Lab · simulator) | .h (panel firmware) | Also in the folder |
|---|---|---|---|
| `1_bakwan/` | `bakwan.js` | `bakwan.h` | |
| `2_choegyeongeun/` | `choegyeongeun.js` | `choegyeongeun.h` | `choegyeongeun_landscape.js` (landscape version, for reference) |
| `3_hanyeonghoe/` | `hanyeonghoe.js` | `hanyeonghoe.h` | `hanyeonghoe.project.json` (the layer source) |
| `4_heoeunbin/` | `heoeunbin.js` | `heoeunbin.h` | `heoeunbin_landscape.js` (landscape version, for reference) |
| `5_jeongeungyo/` | `jeongeungyo.js` | `jeongeungyo.h` | |
| `6_yujin/` | `yujin.js` | `yujin.h` | |

- **The panel compiles the `.h` files here exactly as they are.** There is no copy inside the firmware clone. What you change here is the firmware.
- `_original/`: the originals as first received. `js/` is the JS pasted in on 2026-09-19; `h/` is the .h exported from Pattern Lab at that time. They are kept for comparison and are not edited.
- `preview.html`: the exhibition simulator. Open it in a browser to run the patterns, and take the code straight from it with the **JS 복사** ("Copy JS") / **.h 복사** ("Copy .h") buttons.
- `sync.json`: the fingerprints from the last time the JS and the .h were confirmed to draw the same picture. The "Check" step below uses it.

## Copying and using them

- **To post in Pattern Lab or the community:** copy the whole `<name>.js` from the folder and paste it. For HANYEONGHOE, paste everything down to the last `// @stack` line, or the layers will not come back.
- **When you need the .h:** use `<name>.h` in the same folder.
- Instead of opening the files, you can also use the copy buttons in `preview.html`.

## When you have changed a pattern

1. **You changed the JS** (you pasted in a fix made in Pattern Lab, or edited it yourself) → overwrite `<name>.js` in that folder. For HANYEONGHOE you must also update `hanyeonghoe.project.json` (it is inside the `@stack` line of the code pasted from Pattern Lab; get it out with `tools/patterns/run.sh stack decode`).
2. **Make the .h again.** Ask Claude: "make the `<name>` .h again". *(Translator's note: the creator used an AI coding assistant for this step. Without one, port the JS to the `.h` by hand, following [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md) "Translating to .h and putting it in", then run the check in step 3.)* HANYEONGHOE has a generator (`tools/patterns/hanyeonghoe_h/build.sh`; if the layer code has changed, it stops and tells you to fix the hand-translated parts).
3. **Check** (from the folder above this one, `exhibition_2026`):
   ```bash
   python tools/patterns/host/pfhost.py current <name>        # does the .h draw the same picture as the JS? (it must say OK)
   python tools/patterns/check_sync.py --accept <name>        # only when it said OK: record the current state
   node tools/patterns/preview.mjs                           # rebuild the simulator
   ```
4. Building and flashing the panel firmware is in [§4 of the setup notes](../docs/setup-notes.en.md#4-building-and-flashing-the-firmware). Before flashing, run this once:
   ```bash
   python tools/patterns/check_sync.py     # everything must be "in step"
   ```
   If you changed only the JS and did not redo the .h, this prints `JS changed, .h not redone yet`.
5. `git add patterns && git commit && git push`. If you changed only patterns, you do not need to remake the firmware backup bundle (the .h files are in this repo).

## Good to know

- `.js`, `.h` and `.json` files keep LF line endings on whatever computer you check them out (`.gitattributes`). So what you copy is identical to the original, byte for byte.
- The simulator, the render tool and the host comparison (`pfhost.py`) use the Pattern Lab runtime from the main repo (`../랜딩페이지_opensorce/web`; the folder name **랜딩페이지** means "landing page"). On another computer, clone the main repo next to this one and run `npm install` once in its `web/`. If you only copy and paste, you do not need any of this (`preview.html` opens as it is).
- The knob, press and idle-return rules are in [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md) at the repo root.
