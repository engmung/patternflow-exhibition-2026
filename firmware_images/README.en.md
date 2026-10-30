# Ready-to-flash firmware images

> **English translation.** The Korean original is [`README.md`](README.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/` and the project's copies of the audio samples are not included — build from source as the [front page](../README.md) shows. The original's `patternflow/` (the panel firmware clone) is what you get by applying [`firmware/panel-exhibit-2026.patch`](../firmware/panel-exhibit-2026.patch) to Patternflow `4d264d5`, inside this repo; its `flowlocal/` (the FlowLocal firmware clone) is [`firmware/flowlocal/`](../firmware/flowlocal/) here, so `flowlocal/EXHIBIT.md` means `firmware/flowlocal/EXHIBIT.md`. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](../README.ko.md), translated in [`docs/setup-notes.en.md`](../docs/setup-notes.en.md).

This folder alone, with no build environment, lets you put both boards back to their current state. Use it when a board has to be flashed again from another computer (the laptop).

**The easiest way:** open `flasher.html` at the top of the repo in Chrome or Edge (double-click it, or drag it onto a browser window). Plug in the board, press the button on the page, and pick the port in the window that pops up. That is all. The images in this folder are packed inside that file, so you need neither internet nor Python. It reads the board's MAC address to tell the exhibition panel, FlowLocal and the development board apart, and it will not flash the wrong board. FlowLocal can be flashed through its right-hand (COM) jack, or through the left-hand (USB) jack that is plugged into the laptop (the left jack first needs the button sequence under "FlowLocal" below). There is also a slot for uploading a `.bin` you built yourself (for experiments).

The commands below do the same job from the command line (for when the browser does not work).

| File | Board | Built from | Notes |
|---|---|---|---|
| `panel/firmware.bin` | Panel DevKit | patternflow `exhibit-2026` `006e9c1` + this repo's `patterns/` (`7a8bae3`) | **Built 2026-09-30**, 1,171,296 B, sha256 `502a3c11…`. Connects only to `PatternflowLocal`; invitation host and MIDI-on are fixed; no captions; when an invitation is refused, it invites again after 1 s; detects a dead session within 12 s; Wi-Fi self-recovery in 30 s; new water color for YUJIN. Flashed to the panel on 2026-09-30 (COM10, settings kept) |
| `panel/bootloader.bin`, `panel/partitions.bin`, `panel/boot_app0.bin` | Panel DevKit | Same as on 2026-09-24 (unchanged) | Only needed when you erase everything and flash from scratch |
| `flowlocal/firmware.factory.bin` | FlowLocal | flowlocal `exhibit-2026` `c6360dc` (firmware tag `flx-mon3`) | **Built 2026-09-30**, 1,236,352 B, sha256 `4ecbcca0…`. Adds to `flx-mon2`: never refuses the panel's invitation (it drops the session that has been quiet the longest instead); ends a session that has been quiet for 25 s; answers a resent identical invitation with the same OK; the monitor's channel command (0x13) and channel scores (`cs=`). Flashed to FlowLocal on 2026-09-30 (USB jack, download mode, RST after flashing) |
| `flowlocal/firmware.bin` | FlowLocal | Same build | 1,170,816 B, sha256 `bcfe677e…`. App only. Normally you use the factory image |

- To check that the files arrived intact: in this folder, `sha256sum -c SHA256SUMS`
- Both images contain the exhibition hotspot password. **Keep this repo private.** *(Translator's note: in this public copy the images are not included, for that reason; build your own with your own password.)*

## Preparation (once)

```bash
pip install esptool
```

On Korean-language Windows, esptool 5.x crashes partway through because of the characters in its progress bar. Put `PYTHONUTF8=1 PYTHONIOENCODING=utf-8` in front of the command (the commands below already have it).

**Always give `--port`.** This laptop has the panel DevKit and FlowLocal plugged in at the same time. Without a port, esptool flashes whichever board it finds on its own. Look up the port number in Device Manager (Ports (COM & LPT)).

## Panel — keep the settings, app only (the usual case)

Take the DevKit off the panel board and plug it into the PC by the DevKit's **own USB jack**. You do not need to press any buttons. `COM10` below is this laptop's port (on the old desktop it was `COM17`).

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 --baud 921600 \
  write-flash 0xe000 panel/boot_app0.bin 0x10000 panel/firmware.bin
```

Stored values such as brightness and the caption setting stay as they are. Do not leave out `boot_app0.bin` (a board that has ever received an over-the-air (OTA) update boots from the second slot, and the old app comes up). The panel was flashed this way on 2026-09-25. The new image does not read any other Wi-Fi network or `/midi` host stored on the panel, so you do not need to erase it because of those.

## Panel — erase everything and start from scratch

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 erase-flash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM10 --baud 921600 \
  write-flash 0x0 panel/bootloader.bin 0x8000 panel/partitions.bin 0xe000 panel/boot_app0.bin 0x10000 panel/firmware.bin
```

Erasing resets the panel's settings, such as brightness, to their defaults. After flashing, set them again in the web console.

## FlowLocal

It has two jacks. Whichever one you use, the MIDI port `Patternflow Exhibit` disappears while flashing, so Live and the monitor lose FlowLocal. Do not do this while the show is open.

- **Left jack (USB)** — flash through the same jack that is plugged into the laptop during the show. In the current installation this is the only jack you can use. Normally this jack is a MIDI device and has no port for flashing, so first put the board into download mode: **hold BOOT, press and release RST once, then release BOOT.** A new COM port appears in Device Manager (`COM11` on this laptop on 2026-09-25). Put that number in `--port` below.
- **Right jack (COM, CH343)** — works without the buttons. In this installation you cannot plug anything into this jack.

```bash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM11 erase-flash
PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m esptool --chip esp32s3 --port COM11 --baud 921600 \
  write-flash 0x0 flowlocal/firmware.factory.bin
```

- **If you flashed through the left jack, press RST once when it finishes** (do not press BOOT). Download mode entered with the buttons stays on even after esptool prints `Hard resetting via RTS pin` at the end and restarts the board (this happened on 2026-09-25). Press RST and the new firmware starts; a few seconds later the MIDI port appears on the laptop again.
- On a board that already runs the exhibition FlowLocal firmware, you can skip `erase-flash` and run only `write-flash 0x0 flowlocal/firmware.factory.bin` (that is how it was flashed on 2026-09-25). A board that ran the regular FlowLocal firmware must be erased first (its old AP settings stay in NVS).

FlowLocal has its hotspot settings built into the firmware, so erasing loses nothing. The hotspot comes up 12 seconds after power-on (it waits on purpose, so that the panel lets go of its old connection). At the end of those 12 seconds it scans the Wi-Fi around it and picks one of channels 1, 6 and 11.

## When you have made new images

If you changed the firmware and rebuilt it, replace the files in this folder as well and regenerate `SHA256SUMS` (in this folder: `sha256sum panel/*.bin flowlocal/*.bin > SHA256SUMS`). On this laptop the build outputs are `C:/Users/user/pfx-exhibit-build/firmware/firmware.bin` (panel), and `C:/Users/user/flx-pio/build/exhibit/firmware.bin` and `firmware.factory.bin` (FlowLocal). Then rebuild `flasher.html` too (from the top of the repo, `node tools/flasher/build.mjs`; the first time, run `cd tools/flasher && npm install`). It refuses to build if the checksums do not match. For how to build, see [§4 of the setup notes](../docs/setup-notes.en.md#4-building-and-flashing-the-firmware) and `backups/README.md`.
