# Exhibition pattern knobs · presses

> **English translation.** The Korean original is [`PATTERN_KNOBS.md`](PATTERN_KNOBS.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/`, the `patternflow/` and `flowlocal/` clones and the audio samples are not included, and the FlowLocal firmware source is not public — the [front page](README.md) says what to do instead. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](README.ko.md), translated in [`docs/setup-notes.en.md`](docs/setup-notes.en.md).

What came out of changing the six patterns' JS to fit absolute knobs, and what to take care of when moving that JS into a .h.

## Files

Each pattern has one folder (`patterns/<number>_<name>/`) with the JS and the .h side by side. How to copy and update them: [`patterns/README.en.md`](patterns/README.en.md).

| Pattern | Folder | Notes |
|---|---|---|
| BAKWAN | `patterns/1_bakwan/` | |
| CHOEGYEONGEUN | `patterns/2_choegyeongeun/` | Redrawn in portrait. `choegyeongeun_landscape.js` (landscape) is for reference only |
| HANYEONGHOE | `patterns/3_hanyeonghoe/` | Layer composite. Paste the JS in full, down to the last `// @stack` line. The `.h` is generated from `hanyeonghoe.project.json` |
| HEOEUNBIN | `patterns/4_heoeunbin/` | Portrait file; the direction you see on the panel is the same as the original (head at the bottom). `heoeunbin_landscape.js` (landscape) also looks exactly the same on the panel |
| JEONGEUNGYO | `patterns/5_jeongeungyo/` | |
| YUJIN | `patterns/6_yujin/` | |

- The originals (the JS as it was handed to us, and the first exported .h) are in `patterns/_original/`.
- The preview is `patterns/preview.html`. Like the exhibition firmware it has 32 steps and starts in the middle; K4 moves to the next pattern, and you can view each pattern side by side with its original. It has **JS 복사** / **.h 복사** ("Copy JS" / "Copy .h") buttons. To rebuild it, run `node tools/patterns/preview.mjs`.
- The copyright header and footer were taken out. The community adds them back.

## Uploading to Pattern Lab or the community

1. Paste the pattern folder's `<name>.js` into Pattern Lab in full (or use **JS 복사** ("Copy JS") in `patterns/preview.html`). For HANYEONGHOE the layers are restored from the `@stack` line.
2. Pattern Lab knobs start from their old values. To see the exhibition's first screen, set the three knobs to the middle. HANYEONGHOE is saved with them in the middle.
3. If you also upload the .h to the community, use `<name>.h` from the same folder.

To put JS you changed in Pattern Lab into the exhibition, overwrite `<name>.js` in that folder and rebuild the .h ([`patterns/README.en.md`](patterns/README.en.md), the section on changing a pattern).

## How the knobs work (exhibition firmware)

- K1–K3 are each a single position (0–100%, 32 steps). They go back to the middle at power-on and at the 3-minute idle return, and they keep their position when you change pattern.
- The value a pattern receives is that position's share of the way from the `@knobs` minimum to the maximum. The default value and step size in the code are ignored. A knob that needs a curve takes `0..1` and bends it inside the code.
- The sound gets the same position (CC 24/25/26 = position × 127). Presses send notes (K1 = 60, K2 = 61, K3 = 62). When a pattern marks a moment, one more note goes out in that frame (channel 10. Right now the only one is HEOEUNBIN's footsteps — [4. HEOEUNBIN](#4-heoeunbin) below).
- The roles are the same in all six patterns: K1 = amount, K2 = colour/space, K3 = speed.

## Final knobs per pattern

"Middle" means all three knobs at 50% — the screen a visitor sees first.

### 1. BAKWAN
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | Foliage | 0.25 ~ 1 | 0.625 (artist's value 0.631) | sparse branches ↔ dense gold |
| K2 | Wind | -0.75 ~ 3.25 | +1.25 (artist's value), wind blowing right | light wind blowing left (no wind at step 6) ↔ gale |
| K3 | Speed | 0.35 ~ 1.65 | 1.0× (original tempo) | slower ↔ faster |

- **K1 press:** next tree. It cycles through 7 seeds (3831 → 102 → 137 → 7 → 95 → 33 → 43). On each change the new tree sways and leaves rain down for about 2.5 seconds. The first tree is the artist's seed 3831.
- **Colour:** settled on the `@ramp` in the code we were given (orange-brown at the bottom) (2026-09-19, confirmed by the user). The two stops in the old .h (`0.281:#5d2619 0.683:#fff700`, lemon yellow) are not used.

### 2. CHOEGYEONGEUN (portrait version)
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | DoorOpen | 0 ~ 1 (curve) | open 0.8, the doors swung open to both sides | fully sealed (no light leaking through the middle) ↔ wide open |
| K2 | Zoom | 0 ~ 1 → 0.7 ~ 2.2 | 0.95, the whole cabinet | small ↔ into the niche |
| K3 | FlowerSpeed | 0 ~ 1 → 0.35 ~ 8 | about 3.7 | slower ↔ faster |

- **Flower colours:** four scenes over the dark niche: coral → gold → pink → sky blue. The leaves were split out as green.
- **K1 press:** the doors close by themselves, then open on the next flower scene. Exactly one scene per press. Another press while the doors are closing merges into that same close, and presses in the 0.25 s right after a change are ignored.
- **Changing with the knob:** close it all the way (CC 0) and open it again for the next scene. It re-arms once opened to step 5 or above, about CC 20 or above.
- **To catch this in Ableton:** use "K1 (CC 24) held at a value of 24 or more for about 0.3 s, then a value of 0" as the rule. The doors have inertia, so the panel moves on about 0.4 s after the value reaches 0. A change made by pressing shows up as note 60.

### 3. HANYEONGHOE
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | Bloom | 2 ~ 8.5 | 5 clouds, flower 1.0 | 2 clouds, small bud (0.4) ↔ 6 clouds, big flower in full bloom (1.6); the stems too, ±4px |
| K2 | CloudSize | 0.8 ~ 2.6 | 1.7 (cloud size and how far the stems sway) | small ↔ large |
| K3 | Wind | 0.3 ~ 5.5 | 2.9 | slow wind ↔ strong wind |

- **K1 press:** a gust. The clouds rush off, the flowers and grass bend far over, and it dies down in about 2.5 s.
- Originally there was a bug where the clouds did not show at all in the middle (cloud size was 0 when K3 ≤ 0). It is fixed now.

### 4. HEOEUNBIN
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | Size | 0.6 ~ 1.5 | 1.05, the whole body visible | small ↔ close-up of the head |
| K2 | Backdrop | 0.3 ~ 1 | solid orange + 5 cream flowers | black (flowers off) ↔ orange, 9 flowers |
| K3 | Speed | input 0.2 ~ 1.2 → actual 0.2 ~ 2.4 | 0.7 | slow (still walking) ↔ fast |

- **K1 press:** turns the walking direction around. **K2 press:** gives the mouse a ride, or lets it off. Both are the original features, unchanged.
- **Footstep notes (2026-09-20):** in every frame where a foot touches the ground, the panel sends one MIDI note. Channel **10**, velocity 100, note-off after 40 ms. **36** (**C1** in Live) = near front foot + far hind foot; **38** (**D1**) = near hind foot + far front foot. The two alternate. The walk clock turns `1.5 × Speed` times per second and the feet touch down twice per turn, so **footsteps per second = 3 × Speed**, and this is read from the very clock that draws the picture. So the sound does not drift away from the picture even while K3 is being turned. Live-side setup: [`ableton/README.en.md`](ableton/README.en.md) §4, the footstep notes (HEOEUNBIN) part.

| K3 | Speed | Footsteps per second | Time between steps | Time between the same note | As quarter notes |
|---|---|---|---|---|---|
| Fully left (0%) | 0.2 | 0.6 | 1.67 s | 3.33 s | **36 BPM** |
| Middle (50%, at power-on and idle return) | 0.7 | 2.1 | 476 ms | 952 ms | **126 BPM** |
| Fully right (100%) | 2.4 | 7.2 | 139 ms | 278 ms | **432 BPM** |

- The actual speed is `input + 4.8 × max(0, input - 0.7)²`. The lowest and middle speeds are unchanged; only the top is twice what it was. The knob's CC range is still 0–127, and it speeds up in the upper half.
- Counting one footstep as one quarter note, **BPM = 180 × actual Speed**. Below the middle each step is 5.625 BPM; above it the BPM rises along the curve. The picture and the MIDI use the same walk clock.
- Turning around (K1 press) does not stop the walk, so the footsteps keep coming. They stop when you go to another pattern with K4 or when the panel sleeps, and any note still held down is released. Turning off the panel's `/midi` silences them along with the knob CCs.
- The far-side legs were changed to a dark shadow colour, with an outline where they meet the body, so the alternating front and back legs are clearly told apart.
- The portrait file is pixel-for-pixel the same as the way the original appeared on the panel. The head is at the bottom; a check found all 450 frames identical. The only advantage of the portrait file is that Pattern Lab and the community show it the same way the panel does.

### 5. JEONGEUNGYO
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | State | 0 ~ 0.99 | half gathered | scattered ↔ 3x3 complete (background brightness capped) |
| K2 | Rotation | -0.9 ~ 2.1 | 0.6 rad/s (about one turn in 10 seconds) | turning the other way ↔ fast (no step where it stops exactly) |
| K3 | DriftSpeed | 0.4 ~ 3.6 | 2.0 (artist's value) | slower ↔ faster |

- **K1 press:** the discs suddenly scatter, then gather back into the grid in about 2.5–3 s.
- It was the heaviest of the six patterns, so the per-pixel maths was made lighter. It draws the same picture more cheaply: measured on a PC, one frame went from about 1.1 ms to 0.55 ms.

### 6. YUJIN
| | Name | Range | Middle | Ends (min ↔ max) |
|---|---|---|---|---|
| K1 | FishCount | 4 ~ 24 | 14 fish | 4 ↔ 24 |
| K2 | CurrentLoop | 0 ~ 1 (curve) | 0.625, swimming in circles, riding the current | calm cruising ↔ a whirlpool over the whole screen |
| K3 | SwimSpeed | 6 ~ 24 | 15 | slower ↔ faster |

- **K1 press:** only the fish close to one point near the middle of the school (about 12px, fading out to 28px) glide smoothly aside (up to 1.5×, without whipping round backwards), then return to the current within 1–1.5 s. Fish further away carry on as they were. Pressing again and again does not make them go wild.
- At the original middle (0.5) the fish bunched into one lump and swam in and out of the screen, so the middle was moved to 0.625.

## Translating to .h and putting it in (my notes)

- **We do the translation ourselves.** The six .h files were not exported from Pattern Lab; they are direct ports of the JS in the same folder (2026-09-19, from firmware clone `a5b5db9` on). Since 2026-09-24 the .h files live in this repo's pattern folders, not in the firmware clone, and the firmware compiles those files directly (the include in `exhibit_presets.h` is `../../../../../patterns/...`).
  - HANYEONGHOE's .h is rebuilt by `tools/patterns/hanyeonghoe_h/` from `patterns/3_hanyeonghoe/hanyeonghoe.project.json` (`build.sh`). If a layer's JS changes, the build stops and tells you which hand translation needs fixing. The procedure is in that folder's README.
  - The host harness proves it is the same picture as the JS: `python tools/patterns/host/pfhost.py current [name]` (default set centre/min/max/sweep/press, no hardware needed). When it says OK, record it with `python tools/patterns/check_sync.py --accept <name>`, and before flashing check that `python tools/patterns/check_sync.py` shows everything `in step`.
- **State to put back at idle return** (`PF_EXHIBIT_REST_VARS`; carry the names over to match the translation):
  - BAKWAN: `treeIndex = 0`, `showerT = SHOWER_DUR`. The tree is rebuilt by the next update.
  - CHOEGYEONGEUN: `pressClosing = false`, `pressPeek = 0`, `pressCool = 0`, `sceneIndex = 0`, `canSwitch = false`
  - HANYEONGHOE: `gustTarget = 0`, `gust = 0` in both layers
  - HEOEUNBIN: `dir = -1`, `rider = false`. The knob variables' defaults are now 0.7 / 0.65 / 1.05; the old .h values (0.502 / 1.0 / 1.5) must not be used.
  - JEONGEUNGYO: `pressEnv = 0`, `pressRising = false`
  - YUJIN: `startleS = 0`, `startleE = 0`, `calmS = 0`, `startleN = 0`, and `fleeR = 0` for each fish
- **Time wrap:** keep the periods written in the code (20π, 400π, 2π, 1, 2960 etc.) exactly the same in the translation. Wrapping at any other value makes the picture jump. They are marked with comments in Korean.
- **Memory:** allocate arrays over 1 KB, such as YUJIN's `swirlW` (32 KB), with PFMem. BAKWAN's tree arrays are allocated once in setup at their maximum size and refilled in place on each press.
- **Checks on real hardware:**
  - fps: open the status page on a phone.
  - BAKWAN: any stutter in the frame of a press.
  - Each pattern: how the middle and both ends look.
