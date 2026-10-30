# Install and operations notes — Patternflow exhibition 2026

> **English translation.** The Korean original is [`README.ko.md`](../README.ko.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/` and the project's copies of the audio samples are not included — build from source as the [front page](../README.md) shows. The original's `patternflow/` (the panel firmware clone) is what you get by applying [`firmware/panel-exhibit-2026.patch`](../firmware/panel-exhibit-2026.patch) to Patternflow `4d264d5`, inside this repo; its `flowlocal/` (the FlowLocal firmware clone) is [`firmware/flowlocal/`](../firmware/flowlocal/) here, so `flowlocal/EXHIBIT.md` means `firmware/flowlocal/EXHIBIT.md`. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](../README.ko.md), translated in [`docs/setup-notes.en.md`](setup-notes.en.md) (this file). The English [front page](../README.md) is the short guide; these notes are the full detail.

At a paid exhibition opening around 2026-10-01, visitors touch one Patternflow panel, and the sound comes from Ableton Live on a laptop under the desk. This folder collects what that installation uses: the two firmwares (panel and FlowLocal), the Live device and the check tools. *(Translator's note: written before the opening. The show *Anbu* ran 3–9 October 2026 and was free; see the [front page](../README.md#credits).)*

> **This document contains the hotspot password.** This repository is pushed only to a private GitHub repository (`engmung/patternflow-exhibition-2026`). Do not make it public, and do not copy it anywhere else to share it. *(Translator's note: no longer true: this copy is public, with the password replaced by a placeholder.)*

Reference commits (updated 2026-09-25). The `exhibit-2026` branches of the two firmware clones are in the bundles in `backups/` (to restore them, see `backups/README.md`).

| Repository | Branch | Commit |
|---|---|---|
| `patternflow/` (panel firmware) | `exhibit-2026` | `006e9c1` (2026-09-30): re-invite 1 s after a refusal, detect a dead session in 12 s. Below it, `7350445` no captions; `2ccb652`: joins only `PatternflowLocal`, invitation host fixed at 192.168.66.1 and MIDI fixed on, fast re-invite, Wi-Fi self-heal, transmit power 19.5 dBm ([§3](#3-network)). Below that, `0e1d957` refresh rate exactly 300 Hz, `497eb4a` pattern .h files read from this repo's `patterns/`, `235f06a`·`871ab33` HEOEUNBIN footstep notes, `fba3e53` fixes from the pattern review, `a5b5db9` the six artist patterns re-translated, `1d37256` K1–K3 knob position shared, `ab5bcab` MIDI `PF_MIDI_OUT_LANE_ABSOLUTE`, `382147e` v3.10.3 core |
| `flowlocal/` (FlowLocal firmware) | `exhibit-2026` | `c6360dc` (2026-09-30, `flx-mon3`): no refusals, ends a session after 25 s of silence, channel commands. Below it, `22e6c2d` (2026-09-25, `flx-mon2`): answers the laptop monitor with its status over USB SysEx and takes its commands; at power-on picks the quietest of channels 1, 6 and 11; drops a device that has been silent for 30 s from the hotspot; measures RTP jitter; recovers by itself from a stalled bridge or a blocked USB (reconnects USB, and if that fails, restarts — up to 3 times after power-on) and reports the restart reason (`rsn`, `bc`, `why`, `ur`). Below that, `f9dcd9d` docs, `0c05e79` when re-sending a PC, also appends the panel's last CC 24–26 values, `ebcbe08` hotspot starts 12 s late at power-on, `5bcce79` CC 118 program mirror and the panel address on the status page |
| this folder | `main` | 2026-09-25 commits: laptop monitor `tools/monitor/`, `패턴플로우 모니터 열기.bat` ("Open Patternflow Monitor"), `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition"), new images (`firmware_images/`, `flasher.html`), bundles, these documents. Below that, `3ef4033` |

What is on the boards now (2026-09-30): panel `502a3c11…` (patternflow `006e9c1`), FlowLocal `4ecbcca0…` (`flx-mon3`, flowlocal `c6360dc`). Both are the same as `firmware_images/` and `flasher.html` (see the [flashing log](#flashing-log) at the end).

At first (2026-09-17) all of this was built without hardware. From 2026-09-18 the two boards and Live ran on real hardware and were fixed there. What was seen on real hardware is in the [flashing log](#flashing-log) and the measurement logs at the end; what has not been seen yet is in [§8](#8-not-yet-confirmed-on-hardware).

- Once, when the installation is finished: **[§7 "Pre-show checklist"](#pre-show-checklist)**
- Watching status and fixing things during the show: the laptop's **Patternflow Monitor** (double-click `패턴플로우 모니터 열기.bat` ("Open Patternflow Monitor") at the top of the repo; it is explained in [`tools/monitor/README.en.md`](../tools/monitor/README.en.md))
- What to give the on-site staff: **[`STAFF_CARD.en.md`](../STAFF_CARD.en.md)** (a one-page guide; fill in the blanks and print it)

---

## 1. At a glance

### Signal path

```
 A visitor turns a knob
        │
        ▼
 ┌─ Patternflow panel ─────────────────────────────────────────┐
 │ ESP32-S3, 128×64 LED.  Power: J4 screw terminal, 5 V only   │
 │ K4 = switch pattern,  K1–K3 = pattern parameters + MIDI CC  │
 └─────────────────────────────────────────────────────────────┘
        │  joins the Wi-Fi "PatternflowLocal"
        │  RTP-MIDI (UDP 5004 control / 5005 data)
        │  the panel invites 192.168.66.1
        ▼
 ┌─ FlowLocal ─────────────────────────────────────────────────┐
 │ ESP32-S3-DevKitC-1 N16R8.  Power: laptop USB                │
 │ hotspot + RTP-MIDI → USB-MIDI relay                         │
 │ answers the laptop monitor's status requests and            │
 │ commands (SysEx)                                            │
 └─────────────────────────────────────────────────────────────┘
        │  the DevKit's "USB" jack → laptop  (not the COM jack)
        │  USB-MIDI port name: "Patternflow Exhibit"
        ▼
 ┌─ Laptop (Windows 11) ───────────────────────────────────────┐
 │ Ableton Live 12 Suite + Max 9                               │
 │ Max for Live device "PF Exhibit Scenes"                     │
 │   Program Change n → fire scene n                           │
 │   CC 24/25/26 → parameters mapped with Ctrl-M               │
 │ Patternflow Monitor (tools/monitor, the staff screen)       │
 │   asks FlowLocal for status over the same port              │
 └─────────────────────────────────────────────────────────────┘
        │  laptop audio output
        ▼
     Speakers
```

**Only two things travel the other way, from the laptop.**

- **The Live device's resync request** (CC 119 = 127 on channel 16, bytes `BF 77 7F`). The device sends it once by itself 0.5 s after the set opens, and again whenever you press the **Resync** button. FlowLocal answers that request by sending the last PC and the panel's K1–K3 values again (see below).
- **Patternflow Monitor's SysEx** (`F0 7D 50 46 …`, [`tools/monitor/README.en.md`](../tools/monitor/README.en.md)). Every 2 seconds it asks for status. It sends a command only when a fix button is pressed, one command per press: **패널 연결 다시 잡기** ("Reconnect the panel"), **패널 다시 켜기** ("Restart the panel") or **FlowLocal 다시 켜기** ("Restart FlowLocal"). FlowLocal answers in SysEx. Live also receives these answers, but the device throws them all away ([`ableton/README.en.md`](../ableton/README.en.md) §3).

FlowLocal throws away everything else that comes from the laptop. It passes no MIDI at all on to the panel. The monitor's **Restart the panel** is the only thing FlowLocal passes on to the panel, as an HTTP request (`POST /api/wifi/reboot`). If Live's request does not reach FlowLocal, the fallback is the monitor's **소리 다시 맞추기** ("Re-sync the sound") button (it sends the same `BF 77 7F` without going through Live). After that comes the **Send current pattern to Live** button on the FlowLocal status page, pressed from the operator's phone ([§6](#6-ableton-live)).

The laptop needs no rtpMIDI driver and no Wi-Fi connection. The laptop joins the hotspot only for an over-the-air (OTA) update of the panel, and leaves right after ([§3](#3-network)).

Every time FlowLocal passes on or re-sends a PC, it appends **CC 118 = the same number** right after it (channel 16, bytes `BF 76 n`). This is the program mirror. When Live does not pass the PC on to the device, the device's **Alt CC** reads this instead.

When FlowLocal **re-sends** a PC (the four cases at the end of [§6](#6-ableton-live)), it also appends, after the mirror, the **CC 24/25/26** (channel 1) that the panel sent last (flowlocal `0c05e79`). So even when Live is reopened or the USB reconnects, Live gets the knob positions along with the pattern. Knobs the panel has not sent since FlowLocal powered on are skipped. If FlowLocal has never received a PC, it re-sends nothing. The `aa3f12d` flashed on 2026-09-18 did not have this feature; it has been on the board since 2026-09-24 ([flashing log](#flashing-log)).

### What each box does

| Box | What it does | What it does not do |
|---|---|---|
| Panel | Holds the six artist patterns inside the firmware (no module loading). Every 3 detents (clicks) of K4 go to the next pattern; no captions are shown on a switch (build default is off since 2026-09-25, because the English names had not been confirmed with the artists; how to turn them on: [§5](#5-replacing-an-artists-pattern)). K1–K3 each remember one position per knob, and that position sets both the picture (in every pattern) and the sound (CC 24–26) (see "Knob position" below). If nobody touches it for 3 minutes, it returns to the default pattern (pattern 1) and puts the knob positions back to the start (centre). On boot it is always pattern 1. All long-press menus are locked. Joins only `PatternflowLocal`, and 1 s after joining it invites 192.168.66.1 ([§3](#3-network)) | Brightness control (only from the web console); blocking incoming MIDI (it assumes FlowLocal passes nothing to the panel); joining other Wi-Fi networks saved on the panel; changing the invitation host or turning MIDI off on `/midi` (locked in the exhibition build, [§3](#3-network)) |
| FlowLocal | Hotspot `PatternflowLocal` (192.168.66.1; at power-on it takes the quietest of channels 1, 6 and 11). Accepts the panel's RTP-MIDI session and passes CC, PC and notes unchanged to USB-MIDI. Appends a CC 118 (channel 16) mirror to every PC, and answers the laptop's resync request by sending the last PC and the last CC 24–26 values again. Answers the laptop monitor's SysEx with its status and takes its commands. Status page `http://192.168.66.1/` | USB stick, SD, TFT, MQTT, internet connection. Passing MIDI from the laptop to the panel (apart from the resync request and the monitor's SysEx, it throws away everything that comes from the laptop) |
| Laptop / Live | When PC n arrives, fires scene n; the K1–K3 CCs move parameters. Sends FlowLocal a resync request when the set opens and when the Resync button is pressed. Patternflow Monitor shows the signal path in Korean and has fix buttons (the staff screen, on this laptop only) | Sending MIDI to the panel |

### MIDI the panel sends (channel 1, footstep notes on channel 10)

| Message | When |
|---|---|
| Program Change n | Every time the pattern changes (K4, idle return, boot, console). When an RTP session newly joins, the current PC once more |
| CC 24 / 25 / 26 | The **position** of K1 / K2 / K3. 0–127 absolute = round(position × 127). It is the very position the pattern uses to draw. 64 (centre) at power-on and on idle return; 32 detents from end to end (about one and a half turns); sent only when the value changes, at most 1 per knob per 50 ms. When an RTP session newly joins, the three values once more, right after the PC (FlowLocal also appends the last values every time it re-sends a PC) |
| Note 60 / 61 / 62 | K1 / K2 / K3 pressed (velocity 127); note-off on release |
| Note 36 / 38 (**channel 10**) | While HEOEUNBIN (pattern 4) walks: one note for each frame in which a foot touches the ground, the two alternating (velocity 100, note-off 40 ms later). That is 3 × Speed per second, so the rate follows K3 when you turn it. Other patterns do not send them. Numbers: [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md), HEOEUNBIN; the Live-side track: [`ableton/README.en.md`](../ableton/README.en.md) §4 "Footstep notes" |
| (none) | K4 sends no MIDI at all (no CC 27, no note 63) |

### Knob position — everyone sees the same picture and hears the same sound

For K1–K3 the panel remembers **one position** (0–1) per knob. One detent moves it by 1/32, and it stops at both ends. That single position goes, unchanged, to two places.

- **Picture**: the current pattern's parameter = the low end of that parameter's range + position × the range. For example, a parameter whose range is 0.2–1.0 is 0.6 at position 0.5. The step size (`step`) and the start value in the pattern code are no longer used.
- **Sound**: CC 24/25/26 = round(position × 127). The centre (0.5) is 64.

That is why the mismatches that used to happen are gone.

| Situation | Before (two separate calculations) | Now (one position) |
|---|---|---|
| Knob turned all the way | The pattern went from its own start value to the end of its own range (about 48 detents end to end); the CC reached the end 16 detents from 64; so they stopped in different places | 32 detents end to end; picture and sound stop together, in the same place |
| Pattern switch with K4 | The new pattern took the value it had before; the CC stayed where it was | The new pattern appears as it looks at the same knob position, and the CC does not move |
| 3-minute idle return | Only the pattern values went back to the start; the CC kept the previous visitor's value | The positions go back to the centre, and each knob that was not at the centre sends CC 64 |
| RTP session joins again | Only the PC was re-sent | The current CC 24–26 values too, right after the PC |
| Only Live restarted, USB reconnected | FlowLocal re-sent only the PC | FlowLocal also sends the last CC 24–26 values after the PC |

Good to know:

- **Every pattern starts at the knobs' centre.** It opens with the look at the middle of its range, not with the start value written in the pattern code. What changes in each pattern is in the table on how each pattern opens at a knob position, in [§5](#5-replacing-an-artists-pattern). The start position can be changed per knob (`PF_EXHIBIT_KNOB_START_1/2/3`, end of [§5](#5-replacing-an-artists-pattern)), but it applies the same way to all six patterns.
- **Direction** is set in only two places: inside the pattern (by the artist, as the meaning of the parameter) and in the Live mapping (Min > Max). The panel has no direction flip per knob or per pattern. Flipping per pattern would make the same knob's CC jump on a K4 switch.
- **The Sensitivity (×4 etc.) and Knob value / Relative steps on the panel's `/midi` page are not used for K1–K3 in this build.** The panel sends the position as it is. It does not matter if the saved values differ.
- During the one frame (1/60 s) in which a button is pressed, that knob's position is not passed to the pattern. The patterns are written to "ignore a press on a knob that is moved remotely"; without this, HEOEUNBIN's K1 (turn the walking direction) and K2 (the rider) presses would not work. Whatever was turned during that same frame is applied in the next frame.

### Pattern number = Program Change = Live scene

Live scenes are counted with **the top one as 0** (the device fires `live_set scenes n`). The scene numbers shown in Live's window start from 1, which is confusing, so give the scenes names.

| Pattern number = PC | Which scene, from the top | Pattern | When it comes |
|---|---|---|---|
| 0 | first (scene 0) | Origin | Only when chosen on the web console. K4 never goes there |
| 1 | second (scene 1) | artist 1 BAKWAN = **default pattern** | boot, 3-minute idle return, K4 |
| 2–6 | third–seventh | artists 2–6: CHOEGYEONGEUN, HANYEONGHOE, HEOEUNBIN, JEONGEUNGYO, YUJIN (in this order; table in [§5](#5-replacing-an-artists-pattern)) | K4 (cycles 1 → 2 → … → 6 → 1) |

When the panel is on Origin, or on another pattern chosen from the console, turning K4 forward goes to pattern 1 and turning it back goes to pattern 6. Pressing K4 normally does nothing. One exception: if a firmware upload failed and the UPDATE/FAILED card is on the screen, press K4 once to close that card.

### Folders

```
패턴플로우서울역/                 this repository ("Patternflow Seoul Station"; branch main, private on GitHub. On the old desktop: exhibition_2026/)
├── README.md                     this document
├── STAFF_CARD.md                 one-page staff guide (for printing)
├── PATTERN_KNOBS.md              knob tables of the artist patterns
├── 패턴플로우 모니터 열기.bat    ("Open Patternflow Monitor") double-click: starts the laptop monitor and opens its window (calls tools/monitor/start_monitor.bat)
├── flasher.html                  flashing tool, opened in Chrome or Edge. Contains the images in firmware_images/ (§4)
├── firmware_images/              ready-to-flash panel and FlowLocal images and SHA256SUMS (firmware_images/README.md)
├── backups/                      exhibit-2026 bundles of the two clones, and patternflow_secrets.h (backups/README.md)
├── patterns/                     JS and .h of the six artist patterns (§5)
├── set Project/                  the exhibition Live set (ableton/README.md)
├── .gitignore                    leaves out patternflow/, flowlocal/, .ap_pass_tmp, etc.
├── patternflow/                  Patternflow clone. branch exhibit-2026. push URL deliberately blocked (DISABLED)
│   └── firmware/
│       ├── patternflow/features/exhibit/   exhibition features (pattern list, captions, K4, idle return, K1–K3 knob position)
│       ├── patternflow/features/midi/      MIDI features (PC re-send, invitation host, re-invite and Wi-Fi self-heal, knob position as CC)
│       └── bundles/exhibit/                exhibition build configuration (features_local.h, overrides.h)
├── flowlocal/                    clone of the personal FlowLocal fork (engmung/flowlocal_lsh). branch exhibit-2026
│   ├── EXHIBIT.md                description of the exhibition build (in English)
│   └── src/                      fl_midi_bridge.h, fl_pf_monitor.h (monitor SysEx), fl_wifi.h, fl_ap_channel.h (channel choice),
│                                 fl_rtp_jitter.h (jitter), fl_web_exhibit.h, main_exhibit.cpp
├── ableton/
│   ├── README.md                 Live set, device, Windows kiosk setup (the detailed version)
│   └── pf-exhibit-scenes/        device patch (.maxpat) and logic (.js)
└── tools/
    ├── monitor/                  laptop monitor: pf_monitor.py, dashboard.html, config.json, logs/ (tools/monitor/README.md)
    ├── flasher/                  script that builds flasher.html (node tools/flasher/build.mjs)
    ├── patterns/                 pattern host harness and tools (§5)
    ├── check_exhibit_feature.py  host test of the exhibition features (caption layout, K4, idle return, menu lock, knob position, real patterns + MIDI)
    ├── midi_monitor.py           shows incoming MIDI
    ├── send_test.py              sends fake PC/CC (loopMIDI test)
    ├── test_pf_exhibit_scenes.js offline test of the Live device's js
    └── check_maxpat.py           .maxpat structure check
```

**No pushing.** Push URLs are blocked in both clones: every push line of `git remote -v` is `DISABLED-private-exhibition-clone`.

- `patternflow/`: `origin` = engmung/Patternflow
- `flowlocal/`: `origin` = engmung/flowlocal_lsh and `upstream` = SimonePDA/flowlocal — both

The `exhibit-2026` branches of the two clones exist only for this exhibition. On the `flowlocal/` side, the hotspot password is committed in `flowlocal_secrets.h`, `EXHIBIT.md` and `tools/check_exhibit_image.py` (on the panel side, the password is only in `patternflow_secrets.h`, which the clone keeps out of git; a copy is in this folder's `backups/`). In either clone, do not restore the push URL, do not change the remotes, and never push `exhibit-2026`. The two clones are backed up as bundles in this folder's `backups/`. This folder is pushed only to one private GitHub repository (`origin`) (`backups/README.md`).

---

## 2. Hardware connections

### Panel

- **Power goes in only through the J4 screw terminal (5 V). Do not power it through USB-C.** This is a fixed rule of the project. The v3.9 board has no USB-C input at all; on a v3.0 board, leave `USB1`/`R1`/`R2` unpopulated and power it only through J4. A v3.0 board powered through USB-C ran fine for 20–30 minutes and then smoke came out of the connector (#221). "I ran it for a bit and it was fine" is not evidence.
- The USB port of the ESP32-S3 DevKit plugged into the panel is for flashing and logs. Plug nothing into it during the show.

### FlowLocal (ESP32-S3-DevKitC-1 N16R8)

| DevKit jack | Use |
|---|---|
| **USB** (native USB, left) | **Plug it into the laptop.** It powers the board and is the USB-MIDI device "Patternflow Exhibit" (manufacturer Patternflow, VID:PID 303A:1001). The laptop monitor's SysEx also travels over this cable. Normally there is no serial port on it. **In download mode you can flash through this jack too**: hold BOOT, press and release RST once, then release BOOT, and a new COM port appears; after flashing, press RST once ([§4.5](#45-flowlocal--flash)) |
| **COM** (USB-UART, right) | Flashing and serial log (115200). Flashes without pressing buttons. Leave it empty during the show. In the current installation (this laptop) nothing can be plugged into this jack, so flashing goes through the USB jack, and you watch the monitor and the status page instead of the serial log |

- **Leave the USB-OTG solder jumper open** (do not bridge it).
- Do not attach a USB stick, SD card, TFT or battery case.
- Pick one USB port on the laptop and mark it with tape. If you plug into a different port, Windows can treat it as a new device and the port name can change to something like `2- Patternflow Exhibit`; then you have to turn it on again in Live's settings. When the monitor tells you to unplug and replug, plug it back into the same port after 5 seconds.
- On 2026-09-25, the first time it was plugged in, Windows once saw FlowLocal as "Unknown USB Device (Device Descriptor Request Failed)". Unplugging and replugging fixed it, and it did not come back (suspect the cable or the port).

### Laptop and sound

- Connect the speakers to the laptop's audio output. In Live, Settings → Audio, fix the output device.
- **Live audio runs through FlexASIO** (since 2026-09-25). Driver Type `ASIO`, Audio Device `FlexASIO`, Sample Rate 48000. The FlexASIO settings are in `C:\Users\user\FlexASIO.toml` (WASAPI shared mode, buffer 480 samples = 10 ms, input off). Measured output latency 22.0 ms (26.7 ms including the buffer). If it crackles, raise `bufferSizeSamples` in that file to 960. Deleting the file returns FlexASIO to its defaults.
  - **Do not use Realtek ASIO.** On 2026-09-25 at 15:52 and at 16:17, Live crashed with c0000005 inside `rthdasio64.dll` (3.1.9.1) (Windows Application log). MME/DirectX was stable, but its latency was 96 ms.
  - **Do not use Voicemeeter, and turn off its autostart** (Task Manager → Startup apps). If it starts at login, it grabs the Realtek device first.
  - **Turn off Live's automatic updates** (Live → Settings → Licenses/Maintenance → Get Updates). On 2026-09-25 alone, Live went 12.3.7 → 12.3.8 → 12.4 → 12.4.1, and each version creates its own settings folder (`%APPDATA%\Ableton\Live 12.x.y\Preferences`).
- Keep the laptop on its charger at all times. To turn off sleep, USB power saving and the rest, run `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition") at the top of the repo once. It needs no administrator rights. On both AC power and battery it sets: sleep and hibernate never, closing the lid does nothing, USB selective suspend disabled, PCI Express Link State Power Management off. Patternflow Monitor checks these settings every 60 seconds and, if they are on, warns in yellow with **노트북 절전 설정을 고쳐야 해요** ("The laptop's power-saving settings need fixing"). On 2026-09-25 this laptop had USB selective suspend set to "Enabled" (measurement log).

---

## 3. Network

| Item | Value |
|---|---|
| SSID | `PatternflowLocal` |
| Password | `YOUR_HOTSPOT_PASSWORD` (WPA2) |
| FlowLocal IP | `192.168.66.1` (255.255.255.0) |
| Channel | At every power-on it picks the quietest of 1, 6 and 11 by itself (20 MHz wide). If the scan fails, 6 |
| Simultaneous connections | Up to 3: the panel, the operator's phone, one spare (the laptop during an OTA update of the panel). A device with no signal for 30 seconds is dropped |
| RTP-MIDI | FlowLocal is the listening side. Session name `FlowLocal`, UDP 5004/5005 |
| Laptop monitor | Asks FlowLocal over the USB cable (SysEx), not over the hotspot. It does not take a hotspot slot |

- The old default password `patternflow` does not work in this build (the FlowLocal build refuses to compile with that password).
- The hotspot name, password and IP are built into the FlowLocal firmware and cannot be changed from the status page.
- **The channel is chosen once, at power-on.** After power-on FlowLocal stays off the air for 12 seconds (see the 2026-09-19 measurement log at the end). In the last 3 seconds of that it scans the surrounding Wi-Fi once and opens the hotspot on the least crowded of 1, 6 and 11 (`FL_EXHIBIT_AP_CHANNEL 0` = auto; put `1`–`13` to fix it to that channel). Even if more Wi-Fi networks appear around it during the show, the channel stays the same until the next power-on. To make it choose again, use the monitor's **한 번에 다시 연결하기** ("Reconnect everything") (it restarts FlowLocal). If the automatically chosen channel turns out to be bad in practice (the monitor shows **무선 상태가 나빠요** ("The Wi-Fi is in bad shape")), use **무선 채널 바꾸기** ("Change the Wi-Fi channel") in the operators' section to move to the next channel (1→6→11). That choice is saved in FlowLocal and used at the next power-on too; pressing **채널 자동으로** ("Channel back to auto") makes it automatic again (from FlowLocal `flx-mon3`; status `chm=manual`). The scores from the power-on scan appear in the status as `cs=` (in units of 100; lower = quieter). The chosen channel appears on the monitor's wireless card, as **Channel** on the status page, and in the log `[FlWifi] channel scores ch1 …, ch6 …, ch11 … -> ch N (auto)`. On 2026-09-25, at this site, it chose channel 1 (measurement log).
- **The 3-slot limit stays, and no slot is reserved for the panel.** If two phones are connected, the panel cannot get in. A device that leaves range or loses power without saying goodbye is dropped by FlowLocal after 30 seconds without any signal (`FL_EXHIBIT_AP_INACTIVE_S`; older firmware used the driver default of 300 seconds; not measured on real hardware). A live panel sends a signal every 10 seconds, so it is not pushed out. When your check is done, turn off the phone's Wi-Fi. When the hotspot is full, the monitor shows **핫스팟 자리가 꽉 찼어요** ("The hotspot is full") and the status page shows `Hotspot full`.

### The laptop does not join the hotspot

- The laptop and FlowLocal are connected only by one USB cable. MIDI and power both go through that cable.
- The **only** time the laptop joins `PatternflowLocal` is **when re-flashing the panel over the air ([§4.2](#42-panel--flash))**. Before joining, make sure nothing but the operator's phone is on the hotspot.
- **As soon as flashing is done, disconnect and delete the network.** Windows Settings → Network & internet → Wi-Fi → Manage known networks → `PatternflowLocal` → **Forget**. If you only disconnect and leave it saved, "Connect automatically" makes the laptop rejoin every time it starts. Then the laptop takes one of the 3 slots, and with just one more phone the panel cannot get in.

### How the panel knows its Wi-Fi

**The exhibition panel firmware joins only `PatternflowLocal`, compiled in** (`PF_WIFI_BUILTIN_ONLY 1`, `patternflow/firmware/bundles/exhibit/overrides.h`, since 2026-09-25).

- The name and password are `PF_WIFI_SSID "PatternflowLocal"` and `PF_WIFI_PASS` in `patternflow/firmware/patternflow/patternflow_secrets.h` (a file kept out of git; a copy is `backups/patternflow_secrets.h`). **Building without this file deliberately fails to compile** (`PF_WIFI_BUILTIN_ONLY needs the network's PF_WIFI_SSID in patternflow_secrets.h`). This is so that a panel that looks only for a placeholder SSID, as happened before, cannot be built.
- Networks saved on the panel (NVS `pf_wifi`, saved with the console's `/wifi` or with Improv) are not tried, neither at boot nor when reconnecting. If any remain, the boot log prints `[WiFi] built-in network only: N saved network(s) not tried`.
- The console's `/wifi` page shows `This firmware joins only PatternflowLocal, compiled in.` at the top, and saved networks show as `not used`. There is no **Add a network** and no **Boot network**. **forget** works (it only tidies the list; the behaviour does not change). Adding or choosing a network through `/api/wifi` gives 403, and Improv (entering Wi-Fi over serial) is refused with "unable to connect".
- The transmit power is 19.5 dBm (the radio chip's default). The core caps it at 13 dBm as a margin for EU conformity tests, but at the exhibition the panel's radio carries both the invitations and the MIDI, so the cap was lifted (`PF_WIFI_TX_POWER`). The actual value is `txDbm` in the panel's `/api/status`. On 2026-09-25 the panel's signal strength measured at FlowLocal was −37 to −43 dBm.
- **The old rule is still in the other editions (default, audio, etc.) and in the exhibition images up to 2026-09-24**: if the panel has even one saved network, it does not try the compiled-in one at all. That was the "won't connect" of 2026-09-25: a panel with one home Wi-Fi saved did not try `PatternflowLocal` and kept inviting 192.168.66.1 on the home Wi-Fi (measurement log). Flash a panel that runs an old image with the new image, as in [§4](#4-building-and-flashing-the-firmware).

### How the panel knows where to send MIDI

- The exhibition build is compiled with `PF_MIDI_DEFAULT_HOST "192.168.66.1"` and `PF_MIDI_SESSION_LOCKED 1`. **It always invites 192.168.66.1, and MIDI is always on.** This cannot be changed on `/midi`.
  - The **Computer to invite** field on the `/midi` page is fixed, and there are no **Save**, **Use this computer** or **Clear** buttons. The **MIDI on this panel** switch is locked too. Below it the page shows `This firmware always calls the computer above and keeps MIDI on: neither can be changed from this page.` Sending MIDI-off or another host (including an empty one) to `/api/midi` gives 403.
  - A host or "off" that an older build saved in NVS (`pf_midi`) is not read. If another host is saved, the boot log prints `[MIDI] host fixed at 192.168.66.1 by this build; the one saved on /midi ("…") is not used`.
  - Absolute/relative mode and the per-knob sensitivity (×4) are still saved the moment you press them, but they are not used for K1–K3 in this build ([§1 "Knob position"](#knob-position--everyone-sees-the-same-picture-and-hears-the-same-sound)).
- The panel does the inviting; FlowLocal only listens. The timings are set by `overrides.h` (old values in brackets).
  - The first invitation goes out **1 s after** joining Wi-Fi (20 s. The 20 s gave a computer's rtpMIDI time to reconnect by itself, which FlowLocal does not need).
  - It sends one invitation **3 times** at 1 s intervals; if there is no answer it gives up and **1 s later** sends a new invitation (13 sends, then a 20 s cycle). FlowLocal does not answer repeats of an invitation it has already answered, so if that answer is lost over the air, only a new invitation gets through. The time lost that way went from 20 s down to about 4 s.
  - When a session ends (including when FlowLocal ends it — that is what the monitor's **패널 연결 다시 잡기** ("Reconnect the panel") does), it invites again **1 s later** (20 s cycle).
  - When Wi-Fi drops and comes back (the hotspot also goes off when FlowLocal restarts), it ends any leftover session at once and invites 1 s later. While Wi-Fi is down it sends no invitations (from the final image `07b4c70b…`).
  - **Wi-Fi self-heal**: if Wi-Fi is connected but 60 seconds pass with no session, and in that time 2 or more invitations went unanswered, the panel drops Wi-Fi and reconnects (at most once every 2 minutes). This is for the case where FlowLocal restarted too fast for the panel to notice. Log: `[MIDI] … invitations to 192.168.66.1 unanswered in … s on a Wi-Fi link that is up - reconnecting the Wi-Fi (at most every 120 s)`. In the panel's `/api/status`: `midi.wifiHeals` (normally 0) and `midi.invitesUnanswered`.
- Measured on 2026-09-25 (panel `12bc93a1…`, FlowLocal `flx-mon1`): after the panel was restarted with the monitor's **패널 다시 켜기** ("Restart the panel"), MIDI came back 4.7 s later. After FlowLocal was powered on, about 14.7 s (the hotspot came up at 12 s, the session 2.7 s after that). With the old image, it took 24 s from panel restart to MIDI.

### Opening the panel's pages on a phone — finding the panel's address

The operator uses this to open the panel's pages (brightness, knob direction, captions) on the check phone. During the show, the laptop monitor comes first for watching status and fixing things ([§7](#7-daily-operation)). The FlowLocal status page tells you the panel's address.

1. Connect the operator's phone to `PatternflowLocal` and open `http://192.168.66.1/`.
2. Look at the **Panel** section.
   - **`Panel: 192.168.66.x`** — the address from which the panel opened its MIDI session. The links below it open in a new tab.
     - **Console** `http://<panel>/` — the panel console home (brightness)
     - **MIDI** `/midi`, **Knobs** `/knobs`
     - **Captions off** `/api/exhibit?caption=0`, **Captions on** `/api/exhibit?caption=1`
     - There is no `/wifi` link. Add `wifi` to the end of the Console address to open it.
   - **`Panel: unknown`** — there is no MIDI session, so FlowLocal does not know which device is the panel. Instead, the other devices on the hotspot (not the phone viewing the page) are listed with **Console** and **MIDI** links. It is a list of guesses, one of which is probably the panel; with 3 slots there are at most two. The exhibition image cannot turn MIDI off or change the host, so either the panel has just powered on or its invitation answer was lost over the air (the panel will invite again soon). If the list is empty, the panel is off or could not join the hotspot (see "[How the panel knows its Wi-Fi](#how-the-panel-knows-its-wi-fi)" above).
3. After opening a link, press nothing on the MIDI page. The host and MIDI-on are locked, and the sliders are saved the moment you press them but are not used for K1–K3 in this build.

- `http://patternflow.local/` may also work, but whether `.local` opens on a phone has not been checked.
- When there is no status page, the panel's address is also in the panel's serial log, in the line `[OTA] Ready — hostname "patternflow.local", IP ...`.
- Whether a phone on the hotspot can actually reach the panel's pages, and whether the panel shows up in the `Panel: unknown` list, has not been confirmed on real hardware ([§8](#8-not-yet-confirmed-on-hardware)).

---

## 4. Building and flashing the firmware

### Know this first

- The repository path contains Korean characters (on the exhibition laptop, `C:/Users/user/Desktop/작업자료/2026/패턴플로우서울역/` — "work materials" / "Patternflow Seoul Station"), so the xtensa linker cannot write its output files. **Always give the build folder an ASCII path.** On this laptop it is under `C:/Users/user/` (it is in the commands below). On the old desktop, the folder of the same name was under `C:/Users/lsh/`.
- In both firmwares **the hotspot password goes into the image as plain text.** Do not share or distribute the `.bin` files in these folders:
  - `C:/Users/user/pfx-exhibit-build/` (panel)
  - `…-editions/` folders such as `C:/Users/user/pfx-all-build-editions/` (the default/audio/performance/clock/midi images were also built with the secrets file in place)
  - `C:/Users/user/flx-pio/build/exhibit/` (FlowLocal)
- Build one at a time. In an environment where one Bash call is cut off after 10 minutes, run the build in the background.

### 4.1 Panel — build

In Git Bash:

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
PF_BUILD_DIR=C:/Users/user/pfx-exhibit-build ./firmware/bundles/build.sh exhibit
```

- Result: `C:/Users/user/pfx-exhibit-build/firmware/firmware.bin` (as of 2026-09-25: 1,170,496 B, sha256 07b4c70b…; the same as `firmware_images/panel/firmware.bin`), with `bootloader.bin` and `partitions.bin` in the same folder.
- At the end it prints `NOTE: built WITH patternflow_secrets.h`. Without the secrets file, the exhibition build now stops at compile time ([§3 "How the panel knows its Wi-Fi"](#how-the-panel-knows-its-wi-fi)).
- **Do not run `pio run -t upload` directly in `firmware/patternflow`.** `build.sh` copies the exhibition configuration files in for the build and removes them when it is done. If you call PlatformIO directly, you get the plain firmware without the features.

Checks after the build (in the panel clone, then in this folder):

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
ZIGCXX=C:/Users/user/pfx-tools/zig-c++.exe        # this laptop: no g++ or MSVC, so zig is used (see below)
python firmware/toolchain/check_boundaries.py     # boundary holds
CXX=$ZIGCXX python firmware/toolchain/check_midi.py      # three PASS lines (default, knob position, host/MIDI lock)
CXX=$ZIGCXX python firmware/toolchain/check_network.py   # two network lines (default, PF_WIFI_BUILTIN_ONLY)
grep -caF "PatternflowLocal" C:/Users/user/pfx-exhibit-build/firmware/firmware.bin   # 1 or more
grep -caF "[EXHIBIT]"        C:/Users/user/pfx-exhibit-build/firmware/firmware.bin   # 1 or more
cd ..
python tools/check_exhibit_feature.py             # four PASS lines. Run it without CXX
```

C++ compiler: `check_exhibit_feature.py` looks for `CXX`, g++, clang++ and MSVC, in that order. If it finds none, it automatically uses zig's Clang (installed with `pip install ziglang`) through `python -m ziglang c++` (C++20; it also adds the `-UNDEBUG -D_USE_MATH_DEFINES` that zig needs). **If you run it with `CXX` set to the zig-c++.exe below, the real program fails to compile at `M_PI_2`** (confirmed 2026-09-25). So run this script without `CXX`. The panel clone's `check_midi.py` and `check_network.py` do not know the ziglang fallback, so give them a compiler executable in `CXX`. This laptop has `C:/Users/user/pfx-tools/zig-c++.exe`, which calls zig c++ with `-UNDEBUG` (its source, `zig_cxx.c`, is next to it; the path of the `zig.exe` installed with `pip install ziglang` is hard-coded in it). On 2026-09-25 all the checks above passed this way.

`check_exhibit_feature.py` compiles and runs three programs. To run only one, use `--only logic|real|midi`.

- **logic** — the frame logic of the exhibition features (captions, K4, idle return, menu lock, knob position). The patterns are fakes.
- **real** — takes **the six real artist patterns**, the real MIDI code and the sketch's `applyLaneMotion` as they are, and imitates the frame order. In every frame it checks that the current pattern's K1–K3 variables equal "low end of the range + position × range", and that the CC 24–26 sent equal round(position × 127). It also prints a table of what each pattern opens with at the knobs' centre ([§5](#5-replacing-an-artists-pattern)).
- **midi** — checks that the MIDI output of builds without `PF_MIDI_OUT_LANE_ABSOLUTE` (the audio edition etc.) is byte-for-byte the same as before this change (`013fd9b`). This comparison reads the git history of the `patternflow/` clone.

If you changed the firmware core (`patternflow.ino`, `src/`), build every configuration. On 2026-09-25 this was built separately, so as not to overwrite the exhibition build folder, with `PF_BUILD_DIR=C:/Users/user/pfx-all-build ./firmware/bundles/build.sh all` (all five editions succeeded, in about 7 minutes on this laptop; the edition images are in `C:/Users/user/pfx-all-build-editions/`). If you ran `all` with the exhibition build folder, build the exhibition image again afterwards (`all` overwrites the same build folder). On the old desktop it took about 27 minutes.

### 4.2 Panel — flash

**To flash without building**, open `flasher.html` at the top of the repo in Chrome or Edge and press the button (plug in the board, then pick the port in the window that pops up). The images from `firmware_images/` are inside it. Right now it holds the 2026-09-25 build (panel `07b4c70b…`, FlowLocal `b48b0ee8…` = `flx-mon2`). For the command-line way, see [`firmware_images/README.en.md`](../firmware_images/README.en.md).

**Flashing over USB.** As in BUILD_GUIDE §8, take the ESP32-S3 DevKit off the panel board and plug the DevKit's **own USB jack** (native USB) into the PC. It flashes without pressing buttons. Check the port number in Device Manager (`COM10` on this laptop, `COM17` on the old desktop). **Always give `--port`.** FlowLocal is also plugged into this laptop, so without a port esptool flashes whichever board it finds by itself.

To keep the settings and replace only the app (usually this one; since 2026-09-19 every USB flash was done this way):

```bash
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8     # so esptool 5.x does not crash on the progress-bar characters in a Korean console
ESPTOOL=C:/Users/user/.platformio/penv/Scripts/esptool.exe
B=C:/Users/user/pfx-exhibit-build/firmware
BOOT_APP0=C:/Users/user/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin

"$ESPTOOL" --chip esp32s3 --port COM10 --baud 921600 write-flash 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
```

**The first time, or when you want to clear the saved settings: erase, then flash.**

```bash
"$ESPTOOL" --chip esp32s3 --port COM10 erase-flash
"$ESPTOOL" --chip esp32s3 --port COM10 --baud 921600 write-flash \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
```

- `erase-flash` erases everything: the saved Wi-Fi list, the `/midi` settings, the caption choice, panel settings such as brightness and knob direction, and even uploaded `.pfm` modules (FATFS). The exhibition needs no modules. Set the brightness again on the web console after flashing. The exhibition image does not read the saved Wi-Fi or the `/midi` host ([§3](#3-network)), so you do not need to erase because of those.
- To keep the modules and erase only the settings (NVS), use `"$ESPTOOL" --chip esp32s3 --port COM10 erase-region 0x9000 0x5000` instead of `erase-flash`, then the same `write-flash`.
- Do not leave out `boot_app0.bin` at `0xe000`. A board that has ever received an OTA update boots from the second app slot, so if you write only at `0x10000`, the old app keeps coming up.
- The offsets match the partition table (`partitions/app3M_fat9M_16MB.csv`). On 2026-09-18 the panel was flashed with a full erase + the four files ([flashing log](#flashing-log)).
- The development board `.119` (patternflow-044c) stays in download mode after every reset and power-up because of a capacitor on the BOOT pin (log of 2026-09-12). Do not use it as the exhibition panel until the capacitor is removed.

**To update a panel that already runs the exhibition firmware (to replace an artist pattern, etc.): over the air.** The saved settings stay as they are. Do it from a computer on the same Wi-Fi as the panel.

- On site, connect the laptop to `PatternflowLocal` **briefly**. It takes one of the hotspot's 3 slots, so first take everything except the operator's phone off the hotspot.
- `<panel-IP>` is the `Panel: 192.168.66.x` on the FlowLocal status page ([§3 "Opening the panel's pages on a phone"](#opening-the-panels-pages-on-a-phone--finding-the-panels-address)).
- After flashing, the panel reboots. MIDI comes back a few seconds later (measured 2026-09-25: 4.7 s when it was rebooted with the monitor's **패널 다시 켜기** ("Restart the panel")).
- **When you are done, disconnect the laptop from `PatternflowLocal` and delete the network** ([§3 "The laptop does not join the hotspot"](#the-laptop-does-not-join-the-hotspot)).

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/patternflow
PF_BUILD_DIR=C:/Users/user/pfx-exhibit-build ./firmware/bundles/build.sh exhibit flash <panel-IP>
```

On a computer that cannot run `build.sh`, copy the built file over and run:

```bash
F=firmware.bin
curl -F "firmware=@$F" "http://<panel-IP>/update?size=$(stat -c%s "$F")"
```

If the answer is `{"ok":true}`, the panel reboots by itself. If the upload fails, an `UPDATE / FAILED ... K4 = EXIT` card stays on the panel. **Press K4 once to close it** (or power-cycle; otherwise it closes by itself after 10 minutes).

### 4.3 Panel — check it worked

1. **Serial log** — the panel firmware's serial output comes out of the DevKit's **USB (native) jack** (115200; COM10 on this laptop). On boot:
   - `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions off`
   - `[WiFi] connecting to "PatternflowLocal" (built-in creds, non-blocking)...`. If the panel has networks saved from before, `[WiFi] built-in network only: N saved network(s) not tried` is printed too (harmless)
   - `[OTA] Ready — hostname "patternflow.local", IP 192.168.66.x`. If the IP is not 192.168.66.x, the panel has joined something other than the hotspot (an old image; [§3](#3-network))
   - `[MIDI] rtp listening on ...`
   - Within a few seconds of joining the hotspot, `[MIDI] rtp session joined by "..." (1)`
   - `[MIDI] rtp invitation went unanswered - not a session (0)`: one invitation was sent 3 times and got no answer. The session count does not change, and the panel sends a new invitation 1 s later. This can appear once or so when an invitation answer is lost over the air. If it keeps repeating, FlowLocal is not answering RTP, and after 60 seconds the panel reconnects its Wi-Fi (`… reconnecting the Wi-Fi`, [§3 "Wi-Fi self-heal"](#how-the-panel-knows-where-to-send-midi)).
   - `[MIDI] no rtp session left: rtp list cleared, inviting 192.168.66.1 in 1000 ms`: the session ended, so it invites again 1 s later (this appears after the monitor's **패널 연결 다시 잡기** ("Reconnect the panel")). `[MIDI] Wi-Fi back: rtp list cleared, inviting 192.168.66.1 in 1000 ms`: Wi-Fi came back, so it invites again (after FlowLocal restarted).
   - `[MIDI] rtp session "..." reported joining again - counted once (1)` means the same session was reported twice and was counted once ([§8, "fixed but not yet confirmed on hardware"](#8-not-yet-confirmed-on-hardware)). `rtp session list was full` must never appear at the exhibition.
   - If you see `[EXHIBIT] ERROR: ...`, the pattern list does not match the end of the registry. K4 switching is then turned off.
2. **Screen** — on boot, pattern 1 appears. No captions (artist names) appear (build default is off).
3. **`http://<panel>/api/status`**:
   - `variant` = `"exhibit"`, `variantVersion` = `"v0.1.0"`, `"exhibit"` in `caps`
   - `ssid` = `"PatternflowLocal"`, `ip`, `txDbm` = 19.5 (transmit power)
   - `exhibit` block: `rangeOk: true`, `first: 1`, `count: 6`, `defaultIdx: 1`, `menusLocked: true`, `captions`, `restVars: 42`, `switches`, `idleReturns`, `idleMs`, `knobDetents: 32`, `knobPos` (K1–K3 positions 0–1; `[0.5000,0.5000,0.5000]` right after power-on and after an idle return)
   - `midi` block: `runtime: true`, `host: "192.168.66.1"`, `rtpPeers: 1`, **`outLaneAbs: true`** (a build that sends K1–K3 as the knob position itself; if this line is missing, it is not the exhibition image), **`locked: true`** (an image from 2026-09-25 or later, with the invitation host and MIDI-on locked), `invitesUnanswered` (invitations given up since boot), `wifiHeals` (times the Wi-Fi was reconnected by self-heal, normally 0). `outMode` and `outMul` are not used for K1–K3 in this build, so their values do not matter
   - `heapInternal`, `heapLargest` (free internal RAM. There is no reference value from real hardware yet; write it down the first time you power it on)
4. The Frame rate on **`http://<panel>/status`** (green at 45 fps or more).

### 4.4 FlowLocal — build

```bash
cd /c/Users/user/Desktop/작업자료/2026/패턴플로우서울역/flowlocal
export PLATFORMIO_PLATFORMS_DIR=C:/Users/user/flx-pio/platforms
export PLATFORMIO_PACKAGES_DIR=C:/Users/user/flx-pio/packages
export PLATFORMIO_BUILD_DIR=C:/Users/user/flx-pio/build
python -m platformio pkg install -e exhibit     # first time only
python -m platformio run -e exhibit
python tools/check_exhibit_image.py             # must print PASS  (public copy: with your own password the 'exhibit AP password' line reads FAIL — see the front page)
python tools/check_exhibit_bridge.py            # host test, PASS (includes the monitor SysEx)
```

- Result: `C:/Users/user/flx-pio/build/exhibit/firmware.bin` (as of the second build of 2026-09-25, `flx-mon2`: 1,166,528 B, sha256 6445a6b5…) and `firmware.factory.bin` (1,232,064 B, sha256 b48b0ee8…, the combined image written at 0x0). Both are the same as in `firmware_images/flowlocal/`.
- On the first build, the `idf_tools.py` that pioarduino uses to install the compiler does not run in Git Bash (`MSys/Mingw is not supported`). The first time, build from PowerShell or cmd, or use the method in the "Build" section of `flowlocal/EXHIBIT.md`.
- `check_exhibit_image.py` passes only if the new password, SSID and port name, and traces of the monitor SysEx and of the channel choice, are in the image, and the string `patternflow` and traces of the normal build's broker, stick, SD and TFT are not. For the compiler of `check_exhibit_bridge.py`, see "Host test" in `flowlocal/EXHIBIT.md` (if you have none, `pip install ziglang`).
- **Do not add `lib_ignore` to any env in this project.** Do not build two envs at the same time. pioarduino rewrites a shared framework build script and restores it only when linking succeeds, so a single failure breaks the normal `flowlocal` build (this actually happened).

### 4.5 FlowLocal — flash

To flash without building, use `flasher.html` (both jacks work). On the command line there are two ways.

- **USB jack (as it is, plugged into the laptop)** — the method used in the current installation. Normally this jack is a MIDI device and has no port for flashing, so first put the board into download mode: **hold BOOT, press and release RST once, then release BOOT.** A new COM port appears in Device Manager (`COM11` on this laptop on 2026-09-25). Meanwhile the MIDI port `Patternflow Exhibit` disappears, so Live and the monitor lose FlowLocal. Do not do this during the show.
- **COM jack (USB-UART)** — works without pressing buttons (the USB jack can stay plugged into the laptop, but only when the OTG jumper is open). In the current installation nothing can be plugged into this jack.

**Always give `--port`.** The panel DevKit (COM10) is plugged in too, so without a port esptool flashes whichever board it finds by itself. Check the new port number in Device Manager and put it in (`COM11` below). Use one of the two:

```bash
# A. PlatformIO: erases everything and writes bootloader, partitions, boot_app0, app
python -m platformio run -e exhibit -t erase_upload --upload-port COM11

# B. esptool only
export PYTHONUTF8=1 PYTHONIOENCODING=utf-8
ESPTOOL=C:/Users/user/.platformio/penv/Scripts/esptool.exe
"$ESPTOOL" --chip esp32s3 --port COM11 erase-flash
"$ESPTOOL" --chip esp32s3 --port COM11 --baud 921600 write-flash 0x0 C:/Users/user/flx-pio/build/exhibit/firmware.factory.bin
```

- **If you flashed through the USB jack, press RST once when it is done.** Download mode entered with the buttons stays on, even though esptool prints `Hard resetting via RTS pin` at the end and restarts the board (this happened on 2026-09-25). Press RST and the new firmware comes up; a few seconds later the MIDI port comes back, and 12 seconds later the hotspot comes up.
- Run A in a shell where the three `export` lines from [§4.4](#44-flowlocal--build) are in effect.
- Why erase: to get rid of old AP settings (NVS) and of a boot slot changed by a browser update. A board that already runs the exhibition FlowLocal firmware can just get `write-flash 0x0 …factory.bin` without erasing (that is how it was flashed on 2026-09-25; factory.bin contains `boot_app0`).
- On the first boot after erasing, the driver's default open Wi-Fi `ESP_xxxxxx` shows for about 1 second and then changes to `PatternflowLocal`. From the second boot on it comes up as `PatternflowLocal` right away.
- There is no LittleFS upload (`uploadfs`) step.

### 4.6 FlowLocal — check it worked

1. **Serial log** (COM jack, 115200. The current installation cannot use the COM jack, so use steps 2–4 and the monitor's **운영자용 자세히 보기** ("Details for operators") instead):
   - `[midi] usb device "Patternflow Exhibit" started (native USB jack)`
   - `[FlWifi] AP starts in 12 s so a panel from before this boot lets go`
   - About 9 seconds later, `[FlWifi] channel scan: N networks in … ms (…)` and `[FlWifi] channel scores ch1 …, ch6 …, ch11 … -> ch N (auto)`
   - `[FlWifi] AP "PatternflowLocal" on 192.168.66.1 chN, max 3 stations` (the channel just chosen), `[FlWifi] AP drops a station after 30 s of silence`
   - `[midi] rtp listener "FlowLocal" on 192.168.66.1:5004 (data 5005), ssrc XXXXXXXX` (the SSRC is fixed per board; it stays the same after a reboot)
   - `[midi] program mirror: CC 118 ch 16 after every Program Change` (if this line is missing or says `off`, the build has the Alt CC emergency path turned off)
   - `[midi] knob re-send: CC 24-26 ch 1 after every re-sent Program Change` (if this line is missing, it is an old image that does not append the knob values when it re-sends a PC; from `0c05e79`)
   - `[midi] pf monitor: status request F0 7D 50 46 01 F7 answered … (v1, fw=flx-mon2, …)` *(Translator's note: the source in `firmware/flowlocal/` reports `flx-mon3`.)* and `[midi] pf monitor: commands 10 reconnect panel, 11 reboot panel …, 12 reboot FlowLocal …` (if these two lines are missing, it is an old image that cannot answer the monitor)
   - When the panel joins, `[midi] rtp session joined by "..."`
2. **Laptop** — `Patternflow Exhibit` is in the list of MIDI ports (`python tools/midi_monitor.py --list`). If you see an old name, remove the device in Device Manager and plug it in again. If it shows up as "Unknown USB Device (Device Descriptor Request Failed)", unplug it and plug it back into the same port ([§2](#2-hardware-connections)). Patternflow Monitor's **최근 기록** ("Recent log") shows `FlowLocal 상태를 받기 시작했어요 (펌웨어 flx-mon1, …)` ("Started receiving FlowLocal's status (firmware flx-mon1, …)"), and **Details for operators** shows the channel (`ch`, `chm=auto`) and the panel's signal strength (`prs`).
3. **Status page `http://192.168.66.1/`** (on the operator's phone; or `http://flowlocal.local/`):
   - `Everything connected` at the top
   - `Panel session (Wi-Fi)` = connected, `Panel last heard` within 15 seconds (the panel sends a signal every 10 seconds; confirmed 2026-09-25)
   - Under Hotspot, `Channel` = the chosen channel, such as `1 (auto, least busy of 1/6/11 at boot)`, and `Wi-Fi timing (jitter / worst, last minute)`
   - `Laptop (USB jack)` = connected as "Patternflow Exhibit"
   - Turning K4 changes `Current pattern (program)` (`none yet` if the panel has not sent a PC yet)
   - `Program mirror to Live` is not `off`, and its count of sent messages goes up every time K4 is turned
   - `Knobs K1–K3 (re-sent with the pattern)` shows the values the panel sent last, such as `CC 24-26 ch 1: 64 / 64 / 64, 0 re-sent`, and they change when K1–K3 are turned (`–` for a knob the panel has not sent yet)
   - In `In / out / dropped`, dropped does not go up
   - The **Panel** section shows `Panel: 192.168.66.x` and five links (Console, MIDI, Knobs, Captions off, Captions on). **Console** opens the panel console on the phone
4. **`GET http://192.168.66.1/api/status`** — `build: "exhibit"`, `ap_ssid`, `ap_clients` / `ap_max_clients` (3), `panel_ip` (`null` when there is no session), `ap_stations` (list of addresses of devices on the hotspot), `viewer_ip`, and under `midi`: `peers`, `usb_mounted`, `rx`, `tx`, `dropped`, `last_program`, `last_rx_age_ms`, `last_heard_age_ms`, `rtp_gaps`, `resync_requests`, `web_resends`, `usb_in_discarded`, `program_mirror`, `program_mirrors`, `knob_resend`, `knob_values` (the CC 24–26 the panel sent last; `null` if none yet), `knob_resends`, `rtp_replaced`, `rtp_unknown`, `rtp_bye_sent`. From `flx-mon1` on there are also, at the top level, `ap_channel`, `ap_channel_mode` (`"auto"`) and `ap_channel_scan` (scan result and score per channel), and under `midi` the monitor's `status_requests`, `status_replies`, `status_abandoned` (should not go up), `monitor_commands`, `monitor_commands_refused`, `panel_reboots`, plus `rtp_jitter_ms` and `rtp_spread_ms` (the monitor's `jt` and `jm`).
5. **The `Send current pattern to Live` button** — pressing it increases `web_resends`, and Live receives the same PC and CC 118 once more, followed by the last CC 24–26 values (answer: `Sending pattern n and 3 knob positions to Live.`). If the panel has not sent a PC since FlowLocal started, `No pattern yet` appears.

---
## 5. Replacing an artist's pattern

*(Translator's note: in this chapter and in [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md), the Korean word 작가 ('artist') is also used for whoever wrote a pattern's code. 'The artist's value' means the value in the pattern as it was first received; where the notes ask 'the artist' to change ranges or code, read it as the pattern's author. The pattern files name `@engmung` as their author.)*

The panel now holds the six artists' patterns. On 2026-09-19 **we re-translated them ourselves** (from firmware clone commit `a5b5db9`). They are not Pattern Lab exports: we took the JS, changed the knobs to suit the exhibition, and ported that JS to C++. Since 2026-09-24 the JS and the .h sit side by side in this repo's `patterns/<number>_<name>/`, and the firmware compiles the .h straight from that folder (there is no copy inside the clone). How to copy and update them: [`patterns/README.en.md`](../patterns/README.en.md). What was changed: [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md). Whether the C++ draws the same picture as the JS: the host harness `tools/patterns/host/` (`pfhost.py run`, no hardware needed) shows it. HANYEONGHOE's .h is not edited by hand: `tools/patterns/hanyeonghoe_h/build.sh` regenerates it from `patterns/3_hanyeonghoe/hanyeonghoe.project.json`. The Pattern Lab export `.h` files we first received are only kept, in `patterns/_original/h/`; they do not go into the firmware. Places where memory was converted carry an `exhibit:` comment, and the artist/licence header and footer are unchanged. The original description of the replacement procedure is in the header comment of `patternflow/firmware/patternflow/features/exhibit/exhibit_presets.h` (steps 1–7). All paths below are relative to `patternflow/firmware/patternflow/`.

| Order = PC | File | namespace | Caption now | Drawing orientation |
|---|---|---|---|---|
| 1 (default) | `patterns/1_bakwan/bakwan.h` | `BAKWAN` | BAKWAN | portrait 64×128 |
| 2 | `patterns/2_choegyeongeun/choegyeongeun.h` | `CHOEGYEONGEUN` | CHOEGYEONGEUN (small font) | portrait 64×128 (redrawn as a portrait version) |
| 3 | `patterns/3_hanyeonghoe/hanyeonghoe.h` | `LabStack_HANYEONGHOE` | HANYEONGHOE (small font) | portrait 64×128 |
| 4 | `patterns/4_heoeunbin/heoeunbin.h` | `HEOEUNBIN` | HEOEUNBIN | portrait 64×128 (head at the bottom, the same way the original appeared on the panel) |
| 5 | `patterns/5_jeongeungyo/jeongeungyo.h` | `JEONGEUNGYO` | JEONGEUNGYO (small font) | portrait 64×128 |
| 6 | `patterns/6_yujin/yujin.h` | `YUJIN` | YUJIN | portrait 64×128 |

**For now a caption is just the name, on one line.** The files we received have only the artist's name (the Title field holds the artist's name) and no work title, so all six rows are `X(namespace, "", "")`. The artist line (orange) does not appear; the pattern's `NAME` appears as one line of white text. CHOEGYEONGEUN (13), HANYEONGHOE (11) and JEONGEUNGYO (11) are longer than 10 characters, so each becomes one line in the small font. When titles arrive, fill in both fields: `X(YUJIN, "YUJIN", "<title>")` (step 4). If you fill in only the title, the caption shows only the title and the name is dropped. The romanisation of the names is exactly as written in the files.

*(Translator's note: a pre-show to-do list; its outcome is not recorded in these notes.)* To check with the artists (based on the knob table in [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md); seen in the code and the host harness, not on real hardware):

- **Orientation** — All six are drawn portrait (64×128), so their "up" is the same as the captions'. CHOEGYEONGEUN was redrawn as a portrait version (`choegyeongeun_portrait.js`). *(Translator's note: in this repo the portrait version is `patterns/2_choegyeongeun/choegyeongeun.js`.)* HEOEUNBIN has its head at the bottom, the same way the original appeared on the panel (the orientation the artist chose). Ask the artists whether they previewed their work in this orientation.
- **Knob ends** (step 6) — For all six the roles are K1 = amount, K2 = colour/space, K3 = speed.
  - BAKWAN: K1 sparse branches ↔ dense gold; K2 weak wind to the left (no wind at the 6th step) ↔ gale; K3 slow ↔ fast.
  - CHOEGYEONGEUN: with K1 at 0 the doors close (only the light through the gap breathes), and the picture is almost still. The work moves on to the next scene when you close the doors and open them again, so the range must not be narrowed. K2 small ↔ into the niche; K3 slow ↔ fast.
  - HANYEONGHOE: K1 2 clouds and small buds (0.4) ↔ 6 clouds and big flowers in full bloom (1.6), and the stems change by ±4 px as well. K2 small clouds ↔ big clouds; K3 gentle wind ↔ strong wind. Unlike the earlier file, no end makes the cloud layer disappear (fixed).
  - HEOEUNBIN: K1 small ↔ close-up of the head; K2 dark orange ↔ 6 flowers; K3 still walks even at its lowest.
  - JEONGEUNGYO: K1 scattered ↔ 3x3 complete; K2 turns backwards ↔ fast (no step makes it stop exactly); K3 slow ↔ fast.
  - YUJIN: K1 4 ↔ 24 fish; K2 calm cruising ↔ a whirlpool over the whole screen; K3 slow ↔ fast.
  - The 3-minute idle return resets all of them. In the code there is no end where the screen goes fully dark or freezes (CHOEGYEONGEUN's closed doors come closest).
- **How it opens at the knob centre** — the table below. When the knobs were changed, the centre of each range was set to be the first picture the artist wants (BAKWAN keeps the artist's values as they were; YUJIN K2 is 0.625 so the fish do not clump). Ask the artists whether this first look is right.
- **K4** — None of the six uses its fourth parameter in the picture. Nothing to do.

#### How each pattern opens at the knob positions

K1–K3 go into every pattern as **the same knob position** ([§1 "Knob position"](#1-at-a-glance)). Parameter = low end of the range + position × range, and at power-on and at idle return the position is the centre (0.5). So a pattern opens with the look of the centre of its range, not with the start value written in its code. In the current files the start values in the code are also set to the centre, so the two are the same. `python tools/check_exhibit_feature.py --only real` prints the numbers in this table as `opens at` (2026-09-19, for the current files). The descriptions in brackets are copied from [`PATTERN_KNOBS.en.md`](../PATTERN_KNOBS.en.md) and were not seen on real hardware.

| Pattern | Knob | Parameter | Range | Centre (the value it opens with now) |
|---|---|---|---|---|
| BAKWAN | K1 | Foliage | 0.25–1 | 0.625 (artist's value 0.631) |
| | K2 | Wind | −0.75–3.25 | 1.25 (artist's value, wind to the right) |
| | K3 | Speed | 0.35–1.65 | 1.0 (original tempo) |
| CHOEGYEONGEUN | K1 | DoorOpen | 0–1 (curve) | 0.5 (opening 0.8, the doors swung open to both sides) |
| | K2 | Zoom | 0–1 → 0.7–2.2 | 0.5 (0.95, the whole chest) |
| | K3 | FlowerSpeed | 0–1 → 0.35–8 | 0.5 (about 3.7) |
| HANYEONGHOE | K1 | Bloom | 2–8.5 | 5.25 (5 clouds, flowers 1.0) |
| | K2 | CloudSize | 0.8–2.6 | 1.7 (cloud size and the width of the stems' sway) |
| | K3 | Wind | 0.3–5.5 | 2.9 |
| HEOEUNBIN | K1 | Size | 0.6–1.5 | 1.05 (whole body visible) |
| | K2 | Backdrop | 0.3–1 | 0.65 (solid orange + 1 flower) |
| | K3 | Speed | input 0.2–1.2 → actual 0.2–2.4 | 0.7 |
| JEONGEUNGYO | K1 | State | 0–0.99 | 0.495 (half gathered) |
| | K2 | Rotation | −0.9–2.1 | 0.6 (rad/s, about one turn every 10 s) |
| | K3 | DriftSpeed | 0.4–3.6 | 2.0 (artist's value) |
| YUJIN | K1 | FishCount | 4–24 | 14 fish |
| | K2 | CurrentLoop | 0–1 (curve) | 0.5 (0.625, swimming in circles with the current) |
| | K3 | SwimSpeed | 6–24 | 15 |

There are two ways to change this.

- **The artist moves the range**: change the low and high values in `PFParams::apply(input, i, &value, low, high, step)` so that the first look they want lands at the centre of the range, and export again. This applies to that pattern only. The step size (the last argument) is not used in this build.
- **Change the start position**: put a line such as `#define PF_EXHIBIT_KNOB_START_3 0.75f` in `firmware/bundles/exhibit/overrides.h` and rebuild. It applies to K3 of all six patterns, and the starting CC on the sound side changes with it (0.75 = CC 95). It is rounded to whole steps.

**Do not swap low and high to reverse the direction.** `PFParams::apply` clamps to [low, high] in the order written, so if you swap them the value always sticks to the first argument and the knob does nothing. The direction is set either by the artist, through what the parameter means (e.g. `1 - value` in the code), or in the Live mapping with Min > Max.

#### Replacement steps

1. **Where the file goes** — Put the `.h` in this repo at `patterns/<number>_<name>/<name>.h` (with the JS in the same folder), and point the include line in the firmware clone's `features/exhibit/exhibit_presets.h` at that path (`#include "../../../../../patterns/<number>_<name>/<name>.h"`, which points outside the clone). **Do not put it in `presets/`.** `check_presets.py` rejects a header that has no matching web-side file. Leave the include lines inside the file as they were exported, and keep the artist/licence header.
2. **The namespace must be unique in the whole tree** — `grep -rn "namespace <Name>" firmware/patternflow` must find only that one file. If it is a default name such as `UntitledPattern`, rename it in that file only, e.g. to `ExhibitKimMinji`.
3. **Memory** — Turn every `static` array of 1 KB or more (e.g. `static float trail[128 * 64]` = 32 KB) into a pointer, and allocate it once with `PFMem` in `setup()`. `update()`/`draw()` return straight away if the pointer is null. `static const` tables live in flash and are fine. The `setup()` of all 6 patterns runs once at boot, so the buffers of patterns that are not on screen hold memory the whole time too.
   ```cpp
   static float* trail = nullptr;
   void setup() { if (!trail) trail = PFMem::allocFloats(PANEL_RES_W * PANEL_RES_H); }
   ```
   - For an array of structs: `static Fish* fishes` + `(Fish*)PFMem::alloc(sizeof(Fish) * 24)`. For a 2-D table `static float lut[4][256]`, change it to `static float (*lut)[256]`, and not one `lut[i][j]` in the file has to change.
   - **Before you change an array, search for `sizeof(array)`.** Once it is a pointer, `sizeof` gives 4, so `memset(buf, 0, sizeof(buf))` clears only 4 bytes — and it still compiles (HEOEUNBIN had this).
   - If PFMem is not included in the file, add `#include "src/core_mem.h"`.
4. **Change the list row** — In `PF_EXHIBIT_LIST` in `exhibit_presets.h`, change the row, e.g. to `X(ExhibitKimMinji, "KIM MINJI", "Tidal Memory")`, and add `#include "patterns/<artist>.h"` above it.
   - **Captions are ASCII only** (Hangul and other non-ASCII characters show as `?`).
   - On the portrait panel a line holds 10 characters. The title wraps at spaces onto at most 3 lines, the artist onto at most 2. If there is **a single word longer than 10 characters**, that whole block switches to the small font and breaks in the middle of the word. Artist names and title words are best kept to 10 characters or fewer.
   - If the title is `""`, the pattern's `NAME` is used. If the artist is `""`, there is no artist line.
5. **Knob variables to reset at idle return** — In `PF_EXHIBIT_REST_VARS` in the same file, replace the old pattern's `V(...)` entries with the new pattern's. These are the variables that `update()` passes to `PFParams::apply` / `applyInt` / `applyUnit` (usually the 4 globals near the top of the file). Example: `V(ExhibitKimMinji::knob1) V(ExhibitKimMinji::knob2) ...`. Struct fields work too; for a pattern that loops over its knobs as `knobVal[i]`, list the array elements one by one (`V(Ns::knobVal[0])` … `[3]`). Add state that buttons switch on and off (bool, int) too, if the next visitor should not inherit it (HEOEUNBIN's `dir`, `rider`). Do not list time accumulators (`timeAcc`, `t`). Variables declared inside a function cannot be listed. (The K1–K3 variables themselves are overwritten on the next frame anyway, because idle return moves the knob positions back to the centre. What the list really resets is the rest, such as button state. The test requires the K1–K3 variables, so keep them in the list too.)
6. **What to ask the artist** — That no end of K1–K3 makes the motion stop or the screen go dark (e.g. a speed range should not start at 0). Until the idle return, the next visitor gets the state the previous one left. **K1–K3 open at the centre of their range, not at the start value in the code** (see ["How each pattern opens at the knob positions"](#how-each-pattern-opens-at-the-knob-positions) above). Ask the artist to set low and high so that the first look they want to show is at the centre of the range. Do not swap low and high to reverse the direction (the knob stops working). If a pattern reads `input.knobDeltas` or `input.knobs` directly, it does not get the knob position: `knobDeltas` carries clicks that the core makes from changes in the position (about 1.5 per step, and they jump all at once on the frame after a press), and `knobs` carries the encoder's raw value. Either way the picture drifts away from CC 24–26, so ask the artist to switch to `PFParams::apply` (the step 8 test reports it as a failure). **The K4 value is not passed to the pattern** (K4 switches patterns). The pattern's fourth parameter stays at its start value in the code, so ask the artist to make the value they want to show the start value.
7. **Remove the old pattern's include** — Delete any `#include` that no row uses any more.
8. **Build and check** — The build in [§4.1](#41-panel--build), `check_boundaries.py`, and `python tools/check_exhibit_feature.py` from the root of this repo. This test fails if a `PFParams::apply*` variable is missing from `PF_EXHIBIT_REST_VARS`, and it prints how the 6 captions sit on the panel (small-font lines are marked `(small)`). This is where you see how many lines each caption takes and whether it dropped to the small font. It actually compiles the new pattern file, checks that the K1–K3 variables follow the knob position correctly, and prints the table of what each pattern opens with at the centre (["How each pattern opens at the knob positions"](#how-each-pattern-opens-at-the-knob-positions)). It fails if a pattern reads `input.knobDeltas`/`input.knobs` directly.
9. **Flash and check on real hardware** — An over-the-air (OTA) update as in [§4.2](#42-panel--flash) is enough.
   - Serial: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions off`
   - `/api/status`: `exhibit.rangeOk: true`; `exhibit.restVars` equals the number of entries in the new list (42 now); `heapInternal`/`heapLargest` have not dropped much compared with before the change
   - Frame rate on `/status`: switch through the six patterns one by one with K4 and check that each is near 45 fps. Discuss a slow pattern with its artist.
   - The captions are readable on the LEDs, and no knob end makes the picture stop or go dark
10. **Live side** — Rename the scene to the new artist and title. If you changed the number of patterns, change the number of scenes to match.

Turning captions on and off needs no build: use the **Captions off** / **Captions on** links in the Panel box of the FlowLocal status page, or, from a phone, `http://<panel>/api/exhibit?caption=0` (off), `?caption=1` (on), `?caption=default` (build default = on). *(Translator's note: the exhibition bundle in the patch sets `PF_EXHIBIT_CAPTION 0`, so the build default is **off**, as §7 says.)* The choice is stored on the panel, survives reboots and OTA updates, and is cleared only by `erase-flash`. The other tunables (idle time `PF_EXHIBIT_IDLE_RETURN_MS` 180000, K4 steps per pattern `PF_EXHIBIT_DETENTS_PER_STEP` 3, caption time `PF_EXHIBIT_CAPTION_MS` 2500, caption orientation `PF_EXHIBIT_CAPTION_ROTATION` 1 = portrait, default pattern `PF_EXHIBIT_DEFAULT_SLOT` 0, K1–K3 steps from end to end `PF_EXHIBIT_KNOB_DETENTS` 32, K1–K3 start positions `PF_EXHIBIT_KNOB_START_1`/`_2`/`_3` 0.5f) are set with a `#define` in `firmware/bundles/exhibit/overrides.h`, followed by a rebuild. If you change the default pattern, change Start PC in the Live device to match. If you change the number of steps, picture and sound change together (both use the same position). `PF_MIDI_OUT_LANE_ABSOLUTE 1` (same file) is the line that makes K1–K3 send their CC as the knob position, so do not remove it. Do not remove `PF_WIFI_BUILTIN_ONLY`, `PF_WIFI_TX_POWER`, `PF_MIDI_SESSION_LOCKED` or the invite timing lines (`PF_MIDI_FIRST_INVITE_DELAY_MS`, `PF_MIDI_INVITE_ATTEMPTS`, `PF_MIDI_REINVITE_AFTER_END_MS`, `PF_MIDI_REINVITE_ON_REJOIN`, `PF_MIDI_WIFI_HEAL_MS`) in the same file either ([§3](#3-network)).

---

## 6. Ableton Live

The detailed procedure is in **[`ableton/README.en.md`](../ableton/README.en.md)**. Only the things you must do:

1. **Build the device** — *(Translator's note: not needed with the shared set: it already loads a built, frozen device, with the js inside.)*  Copy `pf_exhibit_scenes.js` into the folder where the device will live → in Live, add a Max MIDI Effect and open the editor → **before pasting anything**, save it into that folder as `PF Exhibit Scenes.amxd` → copy the contents of the `.maxpat`, **delete** the default contents, and paste → Open in Presentation → save → Freeze, then save again. When the status line reads `ready - scenes 0-6`, it is done.
2. **Live Settings → Link, Tempo & MIDI**
   - In `Patternflow Exhibit`: **Track ON, Remote ON**
   - Out `Patternflow Exhibit`: **Track ON, Remote OFF**. Track is the path by which the device's resync request goes out to FlowLocal (with it off, this port does not appear in the control track's MIDI To list). With Remote on, mapped parameter values go back out as CCs.
   - **Sync OFF** on both ports
   - Takeover Mode: **None**. The panel's CC 24–26 is always the knob position itself, so only with None does the same knob position always give the same sound. Why, and how it differs from Value Scaling: [`ableton/README.en.md`](../ableton/README.en.md) §2
3. **Control track** — *(Translator's note: in the shared set the control track is `1-MIDI`, saved with MIDI From Patternflow Exhibit / Ch. 1 — change it to All Channels; and scene track `4.도마뱀` plays the footstep notes itself (All Ins / Ch. 10, Monitor In), instead of a separate track.)* One MIDI track. MIDI From `Patternflow Exhibit` / **All Channels** (set to Ch. 1, it filters out the CC 118 mirror on channel 16), MIDI To `Patternflow Exhibit` / **Ch. 16**, Monitor **In**. **Only one** device on it, `PF Exhibit Scenes`; no instrument, no clips (with an instrument after the device, the request does not reach FlowLocal). Other instrument tracks: MIDI From **No Input**. To use the K1–K3 press sounds, set up a **separate track** with MIDI From `Patternflow Exhibit` / **Ch. 1**, Monitor In, and put an instrument on it. To use HEOEUNBIN's footsteps (notes 36/38), add one more on **Ch. 10**, Monitor In, and load samples into C1 and D1 of a Drum Rack ([`ableton/README.en.md`](../ableton/README.en.md) §4 "Footstep notes").
4. **Scenes** — From the top: scene 0 = Origin (a quiet or empty scene), scene 1 = artist 1 (default), scenes 2–6 = artists 2–6. Name them (order and names as in the table in [§5](#5-replacing-an-artists-pattern): BAKWAN, CHOEGYEONGEUN, HANYEONGHOE, HEOEUNBIN, JEONGEUNGYO, YUJIN). Global Launch Quantization **None** (the scene changes the moment K4 turns; the creator's decision, 2026-09-19. If it cuts off too abruptly, use 1/16 or 1/8 — [`ableton/README.en.md`](../ableton/README.en.md) §3).
5. **Device settings** — Save the exhibition set with **Autostart ON** (1.5 s after the set opens, it fires the Start PC's scene and starts the transport). Start PC = 1, Offset = 0, Enable ON, **Thru OFF**, **Alt CC OFF** (turn it on if the check below shows that Live does not pass PCs through).
   - **Resync happens by itself.** When the set opens, the device sends a resync request to FlowLocal 0.5 s later. When FlowLocal re-sends the last PC (with CC 118 and the last CC 24–26 values), the device fires that scene, and the mapped parameters move to the panel's knob positions (with Takeover None). Autostart keeps that scene. So even when only Live restarts, it starts from the panel's pattern and knob positions. If the panel has not sent a PC since FlowLocal powered up, there is no answer, and Autostart fires the Start PC's scene.
   - By hand at the laptop: the device's **Resync** button. It sends the same request once.
   - **Fallbacks**: the laptop monitor's **소리 다시 맞추기** ("Re-sync the sound") (it sends the same request over USB without going through Live; this is the button the staff card [`STAFF_CARD.en.md`](../STAFF_CARD.en.md) uses), or **Send current pattern to Live** on the FlowLocal status page, from the operator's phone. Both work even when Live's request does not reach FlowLocal (wrong control track MIDI To, or Track off on the Out port).
6. **K1–K3 mapping (Ctrl-M)** — CC 24/25/26. The value is the knob position itself ([§1 "Knob position"](#1-at-a-glance)). Set things up so that **the centre of the mapped range (CC 64) is the starting sound**. The panel sends CC 64 at power-on (right after the PC, once the session joins) and at the 3-minute idle return (only for knobs that were not at the centre). Only with Takeover None (item 2 above) does this CC move the parameter to that spot. Do not let scene tempo or clip automation move a mapped parameter (if K1 is mapped to tempo, do not use scene tempo). If something like that moves the parameter, the same knob position gives a different sound. **Choose Min/Max so that both ends sound fine.** Until the idle return, the end the previous visitor left (e.g. a filter fully closed) stays as it is. Reverse the direction with Min > Max.
7. **Kiosk** — Auto-login; in Startup, the Live executable + a shortcut to the `.als`; sleep, hibernate, lid action and USB selective suspend off; USB root hub power saving off; Windows Update paused; always on the charger. Do **one rehearsal that starts with the power unplugged**, and write on the staff card what to press in the recovery dialog after Live has been force-closed.

FlowLocal re-sends the last PC in four cases: when it could not deliver that PC over USB; when USB reconnects or the laptop wakes; when a resync request comes in (from the Live device, or the monitor's **Re-sync the sound**); and when the status page button is pressed. In every case the CC 118 mirror follows, and after it the CC 24/25/26 the panel last sent (only for knobs the panel has sent).

Check first, without hardware: create a loopMIDI port `PF Test`, send `python tools/send_test.py --port "PF Test" note 60` and `... pc 1`, then press the device's `dump`. *(Translator's note: `dump` is a message box in the patch: click the device's **Edit** button, switch Presentation mode off in Max (View → Presentation), click `dump`, and read Window → Max Console.)* If the Max Console shows `bytes=8 programs=1`, Live passes PCs through to the device. If it shows `bytes=6 programs=0`, it does not. In that case **turn on the device's Alt CC switch and save the set. That is all you need to do.** Leave the number box at its default, 118, and do not change the panel or FlowLocal firmware. Alt CC reads the CC 118 that FlowLocal adds after every PC, whatever its channel (because of a Cycling '74 forum report that Live merges track input onto channel 1 before handing it on; not yet confirmed on real hardware). Check that `... altcc 1` fires scene 1 ([`ableton/README.en.md`](../ableton/README.en.md) §9 item 1). To see whether the resync request leaves Live, use [`ableton/README.en.md`](../ableton/README.en.md) §5 item 6.

---

## 7. Daily operation

### Pre-show checklist

Do this once at installation, and again every time the panel is reflashed or swapped for another panel. Panel-side checks are written **only here** ([`ableton/README.en.md`](../ableton/README.en.md) points here).

**Patternflow Monitor** (laptop, [`tools/monitor/README.en.md`](../tools/monitor/README.en.md))

- [ ] Do the "first time only" steps: `python -m pip install python-rtmidi`; `tools/monitor/install_startup.bat` (it puts a **패턴플로우 모니터** ("Patternflow Monitor") shortcut in Startup and on the desktop. The monitor also opens without it, from the `.bat` at the top of the repo, but the staff card and the monitor screen point at the desktop shortcut, so do it on the exhibition laptop); in `tools/monitor/config.json`, the contacts, where the panel's power is switched on, and the button to press in Live's recovery window (the same values as the blanks in [`STAFF_CARD.en.md`](../STAFF_CARD.en.md)).
- [ ] When the laptop restarts, the monitor window opens by itself. `패턴플로우 모니터 열기.bat` ("Open Patternflow Monitor") at the top of the repo does the same.
- [ ] At the top, green **모두 정상이에요** ("All good"), and no red box among the seven boxes of the signal path (panel → Wi-Fi → FlowLocal → USB → laptop → Ableton Live → speakers).
- [ ] **운영자용 자세히 보기** ("Details for operators"): FlowLocal firmware `flx-mon2` *(Translator's note: `flx-mon3` for the source in this repo)* (the restart reason `rsn` and the boot count `bc` are shown), channel (`chm=auto`), panel signal strength `prs` stronger than −75 dBm. No laptop power setting that is wrong for the exhibition (if there is one, run `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition") at the top of the repo once). The **Live auto-restart** switch is on (if Live closes, the exhibition set is reopened 8 s later; up to 3 times in 15 minutes). Live's audio is FlexASIO ([§2 "The laptop and sound"](#2-hardware-connections)).
- [ ] At the rehearsal, press the fix buttons one by one and check that **됐어요** ("Done") appears. Measured on 2026-09-25: **패널 연결 다시 잡기** ("Reconnect the panel") MIDI back in 1.0 s, **패널 다시 켜기** ("Restart the panel") 4.7 s, **FlowLocal 다시 켜기** ("Restart FlowLocal") about 14 s after FlowLocal comes up.

Connect the operator's phone to `PatternflowLocal` and open the panel pages through the links in the **Panel** box of `http://192.168.66.1/` ([§3 "Opening the panel pages from a phone"](#3-network)). When you are done, turn off the phone's Wi-Fi.

**Panel MIDI page** — **MIDI** in the Panel box (`http://<panel>/midi`). Do not press anything.

- [ ] The top reads **Connected to FlowLocal**.
- [ ] **Computer to invite** is fixed at `192.168.66.1` (no Save, Use this computer or Clear buttons), and below it is the note `This firmware always calls the computer above and keeps MIDI on`. If the note is missing, the image is not one from 2026-09-25 or later, with the lock in it. Flash a new image as in [§4](#4-building-and-flashing-the-firmware).
- You do not need to check **Knob value / Relative steps** and **Sensitivity**. In this build K1–K3 send the knob position as it is ([§1 "Knob position"](#1-at-a-glance)) and do not use these two settings. Do not press them either (they are saved the moment you press).
- [ ] Add `api/status` after the Console address (`http://<panel>/api/status`) and check that `"midi"` contains **`"outLaneAbs":true`** and **`"locked":true`**. If not, it is not the exhibition image ([§4.3](#43-panel--check-it-worked)).

**Panel knobs page** — **Knobs** in the Panel box (`http://<panel>/knobs`)

- [ ] All four knobs: **Direction = Normal** (not Reversed), **Edges per click = 4 (most encoders)**. Both are the defaults.
- [ ] Turning each knob one step clockwise **raises Position by 1**.
- [ ] If non-default values are saved, set them back to the defaults, **Save**, and turn again (saving does not move the knob position). If one step at the defaults is not 1 (it goes down, rises by 2, or gives 1 per two steps), that knob has a different encoder. Only then, adjust that knob as the note at the bottom of the page says, and write down what you changed.

**Panel Wi-Fi page** — `wifi` after the Console address (`http://<panel>/wifi`)

- [ ] The **Network** box at the top says `This firmware joins only PatternflowLocal, compiled in.` If this box is missing, it is an older image (in which a saved network blocks the compiled one; [§3](#3-network)). Flash a new image as in [§4](#4-building-and-flashing-the-firmware).
- Saved networks are shown as `not used` and are not used. You can delete them (**forget**) or leave them. There is no **Add a network** and no **Boot network**.

**Captions**

- [ ] Decide whether to have them on or off (the build default is off; decided on 2026-09-25: the artists' English names were not confirmed with them, so names are not shown). Press **Captions off** or **Captions on** in the Panel box, and `"caption"` in the reply changes to `false` / `true`. The choice is stored on the panel and survives reboots and OTA updates. Turn K4 three steps to check.

**Brightness**

- [ ] With the venue lighting as it will really be, set it with the **Brightness** slider in **Console** in the Panel box (the panel console's home page). Let go and wait a moment, and it is saved on the panel. Higher brightness also draws more power (J4).

**Other items (their procedures are in other sections)**

- [ ] FlowLocal status page: `Everything connected`, `Program mirror to Live` not `off`, `Panel: 192.168.66.x`, `Channel` is `auto` ([§4.6](#46-flowlocal--check-it-worked))
- [ ] Live: the list in [§6](#6-ableton-live), and the **Alt CC** switch set according to the result of the check whether Live passes PCs through to the device (end of [§6](#6-ableton-live))
- [ ] Laptop: the kiosk checklist in [`ableton/README.en.md`](../ableton/README.en.md) §7 (the monitor re-checks sleep and USB selective suspend from that list every 60 s). **`PatternflowLocal` is not among the laptop's known Wi-Fi networks** ([§3](#3-network))
- [ ] A `FlowLocal` label on the FlowLocal board, and a tape mark at its USB port on the laptop ([§2](#2-hardware-connections)). [`STAFF_CARD.en.md`](../STAFF_CARD.en.md) was written around this mark
- [ ] Fill in the blanks in [`STAFF_CARD.en.md`](../STAFF_CARD.en.md) (contacts, the phone for checks, where the panel's power is switched on, where the knob that changes the picture = K4 is, the button to press in Live's recovery window) and print it. Write the same values in `tools/monitor/config.json`. Save the `PatternflowLocal` password on the phone for checks; do not write it on the card

### Opening order (laptop → Live → panel)

- [ ] Check that the laptop charger is connected; check speaker power and volume
- [ ] FlowLocal is plugged into the laptop's **marked USB port**, through the DevKit's **USB** socket
- [ ] Turn on the laptop → auto-login → wait until the Live set opens. The **패턴플로우 모니터** ("Patternflow Monitor") window also opens by itself (if it does not, use **Patternflow Monitor** on the desktop, or `패턴플로우 모니터 열기.bat` ("Open Patternflow Monitor") at the top of the repo)
- [ ] The lower line of the device on the Live control track shows `ready - scenes 0-6` *(Translator's note: `0-7` with the shared 8-scene set)* for a moment, then changes to `fired HH:MM:SS`; the upper line shows `autostart PC 1 -> 1: ...`, and there is sound
- [ ] Switch on the panel's power (the supply on J4). Pattern 1 appears (no caption)
- [ ] Wait for the monitor to turn green, **모두 정상이에요** ("All good"). FlowLocal opens the hotspot 12 s after it powers up, and the panel joins the hotspot and connects within a few seconds (measured on 2026-09-25: about 14.7 s after FlowLocal powers up, 4.7 s after the panel restarts). If it is still yellow or red after a minute, follow the monitor's **지금 할 일** ("What to do now")
- [ ] Turn K4 three steps → the pattern changes, the Live scene changes, and the monitor's current picture (number) changes
- [ ] Turn K1, K2, K3 → pattern and sound change together, the monitor's knob cards follow, and the **신호 도착** ("Signal arrival") bars run on evenly
- [ ] (Only when checking with the operator's phone) connect the phone to `PatternflowLocal` and look at `Everything connected` on `http://192.168.66.1/` and at the brightness (the **Console** link in the Panel box → Brightness). When done, turn off the phone's Wi-Fi (to free a slot on the hotspot)

### Closing order

- [ ] Switch off the panel's power
- [ ] Quit Live → shut down the laptop (FlowLocal powers off too when the laptop's USB power is cut)
- [ ] If the panel runs on a power bank, charge it

### When something goes wrong

**First, look at 패턴플로우 모니터 ("Patternflow Monitor") on the laptop.** The title at the top and the red box in the signal path tell you which link is broken, and **지금 할 일** ("What to do now") appears. To fix things, press only the one big button, **한 번에 다시 연결하기** ("Reconnect everything"). It goes through these in order: check FlowLocal → restart FlowLocal (it picks a channel again) → connect the panel (if it will not join, restart the panel remotely) → check Live (if it is closed, open the set; if it is open, re-sync the sound) → check. Manual steps (RST, re-plugging, panel power) it asks for one at a time, and it carries on once it detects that you did them. The individual buttons (**소리 다시 맞추기** "Re-sync the sound", **패널 연결 다시 잡기** "Reconnect the panel", **패널 다시 켜기** "Restart the panel", **FlowLocal 다시 켜기** "Restart FlowLocal", **무선 채널 바꾸기** "Change the Wi-Fi channel") are in the operators' section, **운영자용 자세히 보기** ("Details for operators"). The rules it judges by, and the buttons: [`tools/monitor/README.en.md`](../tools/monitor/README.en.md). The exhibition (the sound) keeps running even when the monitor is closed. The table below lists the causes the monitor points to, and the things the monitor does not know about (Live settings, the panel's screen). Looking at `http://192.168.66.1/` from the operator's phone is for when the monitor is not enough.

| Symptom | Cause | Fix |
|---|---|---|
| No monitor window, or **모니터 프로그램이 꺼졌어요** ("The monitor program is closed") | The window was closed, or the monitor did not start | Double-click **Patternflow Monitor** on the desktop or `패턴플로우 모니터 열기.bat` ("Open Patternflow Monitor") at the top of the repo. For anything else: [`tools/monitor/README.en.md`](../tools/monitor/README.en.md) "When the monitor itself misbehaves" |
| No sound at all; the device's lower line shows `ready …` or `fired HH:MM:SS` | Transport stopped, the audio output device changed, speakers | Check that Live is playing; Live Settings → Audio output device; speaker power and volume |
| No sound, and the device stays at `waiting for Live` | The device cannot find the js | Check the Max Console; put the js next to the `.amxd`, or Freeze ([`ableton/README.en.md`](../ableton/README.en.md) §1) |
| Live is not open / a recovery dialog | Restart after a crash | Press the button noted at the rehearsal (the blank in [`STAFF_CARD.en.md`](../STAFF_CARD.en.md)). If that does not work, restart the laptop and wait until Live has reopened and the device's lower line shows `ready …` or `fired HH:MM:SS` |
| Turning K4 changes the pattern but not the scene — the monitor is red with **패널이 핫스팟에 없어요** ("The panel is not on the hotspot"), **패널이 핫스팟에는 있는데 신호 연결이 안 돼요** ("The panel is on the hotspot but the signal connection is not up") or **패널 신호가 끊겼어요** ("The panel's signal was lost") (status page: `Panel not connected`) | The panel cannot get a session with FlowLocal: the panel has no power or was switched on only a moment ago, the hotspot is full, or the reply to the invitation got lost over the air. The exhibition image never fails to join because of another saved Wi-Fi or the `/midi` host ([§3](#3-network)) | First check that there is a picture on the panel (power). Then the monitor's **Reconnect everything**. If the panel is on the hotspot but not connected, the monitor restarts the panel remotely; if it is not on the hotspot, the monitor tells you to switch the panel's power off and on. If the hotspot is full, turn off phone Wi-Fi; if the laptop is on the hotspot, take it off ([§3](#3-network)) |
| Same symptom, and the monitor is red with **작은 보드(FlowLocal)가 노트북에 안 보여요** ("The small board (FlowLocal) is not visible to the laptop") (status page: `Laptop (USB jack)` = not connected) | FlowLocal plugged in by its COM socket, a different USB port, laptop USB power saving, cable | Unplug the DevKit's **USB** socket and plug it back into the marked port 5 s later. If Windows reports it as "Unknown USB Device", do it once more ([§2](#2-hardware-connections)). USB power-saving settings ([`ableton/README.en.md`](../ableton/README.en.md) §7) |
| The monitor is red with **작은 보드(FlowLocal)가 대답하지 않아요** ("The small board (FlowLocal) is not answering"), or yellow with **FlowLocal 상태를 알 수 없어요 — 새 FlowLocal 펌웨어가 필요해요** ("FlowLocal's status is unknown — new FlowLocal firmware is needed") | Red: FlowLocal has hung. Yellow: old FlowLocal firmware that cannot answer the monitor (before `flx-mon1`) | Red: **Reconnect everything**. It waits 25 s for FlowLocal to repair its USB by itself, and if that fails it asks for one thing only: RST (or unplug, wait 5 s, and plug into the same port). Yellow: flash a new image as in [§4.5](#45-flowlocal--flash) (until then the monitor shows only the panel's signals) |
| The monitor is yellow with **노트북 절전 설정을 고쳐야 해요** ("The laptop's power-saving settings need fixing") | On mains power, sleep, hibernate or USB selective suspend is on (on this laptop on 2026-09-25: USB selective suspend = Enabled) | Run `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition") at the top of the repo once (no admin rights needed; at the end it shows whether each setting's AC/DC value is 0x00000000). The monitor turns green within a minute |
| The monitor is yellow with **무선 상태가 나빠요** ("The wireless is in bad shape") | One of panel signal strength, jitter, lateness, lost packets or bunching is past its threshold ([`tools/monitor/README.en.md`](../tools/monitor/README.en.md) "Thresholds") | Turn off phone hotspots, and the Wi-Fi of phones not in use. If it goes on, use **Change the Wi-Fi channel** on the screen to move to the next channel (1·6·11) (FlowLocal `flx-mon3` and later; for about 15 s the sound does not follow the panel). With older firmware, **Reconnect everything** (FlowLocal picks a channel again as it powers up). If it still goes on, move FlowLocal away from the laptop on a USB extension cable and put it somewhere high (plug it into the same taped port) |
| The monitor's current picture changes but the Live scene stays the same (and the status page's Current pattern agrees with the monitor) | Live-side settings | Live In port Track ON; control track MIDI From (All Channels) / Monitor In; device Enable. If the status line says `missing`, check the number of scenes and Offset. If the editor's `dump` shows `programs=0` but `altccIn` rises, turn on the device's **Alt CC** ([`ableton/README.en.md`](../ableton/README.en.md) §8) |
| Scenes change one off | Scene numbers counted from 1 | The top scene is number 0 (Origin). Order the scenes as in the table in [§1](#1-at-a-glance) |
| After restarting only Live, sound and pattern do not match | The device's resync request got no answer, so Autostart fired scene 1. Either the request did not reach FlowLocal (control track MIDI To, Track on Live's Out port), or the panel has not sent a PC since FlowLocal powered up (status page Current pattern is `none yet`; `lp` in the monitor's Details for operators is `-1`) | At the laptop, the device's **Resync** button. If nothing changes (the request does not reach FlowLocal), the monitor's **Re-sync the sound** (it does not go through Live), then **Send current pattern to Live** on the status page from the operator's phone. If it is `none yet`, turn K4 once. To find the cause: [`ableton/README.en.md`](../ableton/README.en.md) §8 |
| Turning K1–K3 does not change the sound | No mapping, In Remote OFF, Takeover set to Pick-Up | Check the Ctrl-M mapping, In Remote ON, Takeover None. (Relative steps and sensitivity on the panel's `/midi` are not the cause in this build) |
| After a visitor leaves, the sound stays in an odd state (filter closed, etc.) | Until the 3-minute idle return, the knob positions the previous visitor left stay as they are | After 3 minutes the panel returns the knobs to the centre and sends CC 64. Narrow the mapping's Min/Max so that it sounds fine even before then |
| Picture and sound go separate ways (only one of them stops at a knob end, the sound jumps after K4, etc.) | Not the exhibition image (`midi.outLaneAbs` missing from `/api/status`), or the knob sliders on the console home were moved (the sliders move only the picture), or the Live-side mapping or Takeover | Check `/api/status` ([§4.3](#43-panel--check-it-worked)). If a console slider was used, turning that knob one step brings them back together (if the slider was moved in the last 5 s, after those 5 s). In Live: Takeover None (with Value Scaling, a parameter changed on the Live side stays out of line until the knob reaches an end), and check that scene tempo or clip automation does not move that parameter |
| Turning K4 does not change the pattern | It changes once every 3 steps. For 1 s right after waking, K4 is ignored | Turn 3 steps or more. If it still does not work, check `exhibit.rangeOk` in `/api/status` (false means a build problem) |
| An `UPDATE` / `FAILED` / `K4 = EXIT` card on the panel | A firmware upload from the console failed | Press K4 once. Or power-cycle |
| The panel is black and does not react to the knobs | Panel off (sleep) may have been pressed in the console, or power | Press any button. If that does not work, check the J4 power, then power-cycle |
| The panel's picture is frozen / the console will not open | Cause unknown (no record on real hardware) | Power-cycle the panel. If you can, first write down `heapInternal` and `uptime` from `/api/status` |
| The hotspot `PatternflowLocal` is not visible — the monitor shows **작은 보드(FlowLocal)가 켜지는 중이에요** ("The small board (FlowLocal) is starting up") or **FlowLocal 핫스팟(PatternflowLocal)이 꺼져 있어요** ("FlowLocal's hotspot (PatternflowLocal) is off") | Less than 12 s since FlowLocal powered up (it stays off the air on purpose), FlowLocal has no power (laptop off, or USB power cut by sleep), FlowLocal firmware is not the exhibition build | If it is starting up, wait. If it is off, **Reconnect everything**; if that does not work, unplug the USB and plug it back into the same port 5 s later, and turn the laptop on. If needed, reflash as in [§4.5](#45-flowlocal--flash) |
| A phone will not join the hotspot | 3 devices are already connected (also check that the laptop is not connected), or a device that left still holds its slot (FlowLocal drops it after 30 s of silence) | Turn off the other phones' Wi-Fi; delete `PatternflowLocal` on the laptop ([§3](#3-network)). If that does not work, wait about 30 s and try again. Password `YOUR_HOTSPOT_PASSWORD` |
| MIDI stops after FlowLocal restarts (monitor button, Reboot on the status page, update, USB unplugged) | FlowLocal deliberately keeps the hotspot off for 12 s after it powers up. When the hotspot comes up, the panel rejoins the Wi-Fi and invites 1 s later | Wait. It joins about 15 s after FlowLocal powers up (measured on 2026-09-25: 14–14.7 s). If it has not joined after a minute, use **Restart the panel** as the monitor says, or power-cycle the panel |
| MIDI stops after the panel's power is switched off and on | It takes a while for the panel to join the Wi-Fi, invite, and get the session up | Wait. It joins in a few seconds (measured on 2026-09-25: 4.7 s with the monitor's Restart the panel), and once joined the panel re-sends the current PC and the K1–K3 values |
| `Patternflow Exhibit` is not in Live's port list / an old name shows | Plugged in by the COM socket, a different USB port, Windows remembers an old name, Windows reports "Unknown USB Device" | Check the USB socket and the marked port. Unplug and plug back into the same port 5 s later. If it is the old name, remove the device in Device Manager and plug it back in |
| `midi_monitor.py` cannot open the port | Live holds the port alone (old MIDI driver) | On this laptop, Windows MIDI Services let several programs share a port, so Live and Patternflow Monitor open it together (`midi_monitor.py` uses the same python-rtmidi). If it will not open on another laptop, close Live and use the monitor; when done, close the monitor and start Live |
| Notes are heard twice | Two press-sound tracks, or that track also receives through All Ins + Arm | Only one press-sound track, with MIDI From `Patternflow Exhibit` / Ch. 1 ([`ableton/README.en.md`](../ableton/README.en.md) §4 "Press sounds") |
| `midi.usb_in_discarded` in FlowLocal's `/api/status` keeps rising | The laptop sends something besides requests: the default midiin/midiout were not deleted when the device was built, device Thru is on (the monitor's SysEx replies go back too), Remote or Sync is on for Live's Out port. The monitor's status requests and commands are not counted here | Not a fault, since FlowLocal discards it. Turn those off as in [`ableton/README.en.md`](../ableton/README.en.md) §1 item 5 and §8 |
| Captions (artist names) appear / you want them on | The build default is off. Someone pressed Captions on, and "on" was stored on the panel | Off: **Captions off** in the Panel box of the status page (= `http://<panel>/api/exhibit?caption=0`). On: **Captions on** (`?caption=1`). To clear the stored choice and go back to the build default (off): `?caption=default` |
| The ← / → buttons on the console home do not step through the exhibition patterns | The exhibition patterns are registered as hidden entries (to stop the name showing in the centre) | Pick from the console's pattern **list**. A pattern picked in the console also returns to pattern 1 after 3 minutes |

---

## 8. Not yet confirmed on hardware

On 2026-09-17 we only built, checked strings in the images, ran host tests and read code, all without hardware. From 2026-09-18 we ran both boards and Live on real hardware (see the flashing log and the field measurements at the end). Below, **confirmed** means seen on real hardware; everything else is still open. If an earlier item fails, the later ones mean nothing.

1. **FlowLocal USB-MIDI** — **Confirmed** (2026-09-19 desktop, 2026-09-25 laptop): Windows 11 sees it as `Patternflow Exhibit`, Live and Patternflow Monitor share the port, and it runs on the laptop's USB power with the OTG jumper open. Still open: whether the "Unknown USB Device (Device Descriptor Request Failed)" seen once at the first connection on 2026-09-25 comes back (cable or port suspected). If the same board ever ran old firmware, Windows may remember the old name (VID:PID 303A:1001).
2. **Whether Live passes Program Change through to the device** — **Confirmed** (2026-09-19, ["Live check"](#live-check-2026-09-19-live-12-suite-on-that-desktop) at the end): it does, so Alt CC is off. **Resync** button → FlowLocal's reply → scene fired also worked. Still open: whether the status line for `altcc 1` shows `ch1` or `ch16`, the byte order through the device's `deferlow`, the status line reset on load, saving and restoring the settings, the "already on" check (`playing_slot_index` and so on), and that Live does not switch scenes wrongly when it receives FlowLocal's SysEx replies while the monitor is running (confirmed only by Node test 26; [`ableton/README.en.md`](../ableton/README.en.md) §3).
3. **Whether the panel joins the hotspot and invites FlowLocal** — **Confirmed** (2026-09-19, 2026-09-24, 2026-09-25): as soon as the session joins, `PROGRAM 1`, the CC 118 mirror and CC 24–26 = 64 reach the laptop. On 2026-09-25 we ran into a failure to join caused by the old rule (a saved Wi-Fi blocks the compiled one), and removed that rule in the exhibition image ([§3](#3-network)). With the new image: 4.7 s from panel restart to MIDI, about 14.7 s from FlowLocal power-up. Still open: whether the same numbers come after flashing the final image `07b4c70b…`, whether the locked `/wifi` and `/midi` pages look on a phone as [§3](#3-network) describes, and the Improv refusal.
4. **Handling the panel** — that none of the 4 long-press menus opens, how K4's 3 steps feel, that the captions are readable on the LEDs and in the right place (caption layout was checked only on the host, with the real Adafruit_GFX font code; with several lines, the lower line's background covers the bottom pixel row of the line above), and that the hidden registration keeps the centre name display from appearing.
5. **Idle return in real time** — that it goes back to pattern 1 once after 180 s, whether resetting the knob values looks jumpy to a viewer, whether a pattern picked in the console goes back after 180 s, and that Live's parameters stay where they are.
6. **Recovery scenarios** — **Confirmed** (2026-09-25, once each with the monitor buttons): MIDI 1.0 s after **패널 연결 다시 잡기** ("Reconnect the panel"), 4.7 s after **패널 다시 켜기** ("Restart the panel"), about 14 s after FlowLocal comes up for **FlowLocal 다시 켜기** ("Restart FlowLocal") (12 s of which is the deliberate quiet time). Still open: that there is no second drop when the panel's power is switched off and on by hand; that it behaves the same over many repeats; whether, when only Live restarts, the device's resync request makes it start at the panel's scene (whether `midi.resync_requests` in FlowLocal's `/api/status` goes up by 1 every time the set opens); whether USB power holds through laptop sleep and wake. FlowLocal's session repair code has only been checked by reading the AppleMIDI 3.5.0 source and following it by hand.
7. **Whether Windows reads and discards USB-MIDI while Live has the port closed** — if so, the PCs sent during that time are lost. When Live reopens, the device's request gets them back; if not, set things right with the Resync button, the monitor's **소리 다시 맞추기** ("Re-sync the sound"), the status page button, or K4.
8. **The hotspot's 3-device limit and phone access** — whether `Panel: 192.168.66.x` on the FlowLocal status page matches the panel's address; whether its links (Console, MIDI, Knobs, Captions) open the panel pages on a phone inside the hotspot; whether the panel appears in the list (`ap_stations`) under `Panel: unknown` when there is no session; whether `patternflow.local` opens on a phone; `Hotspot full` and the monitor's **핫스팟 자리가 꽉 찼어요** ("The hotspot is full") when 3 devices are connected; and whether a device that lost power really drops off after 30 s (`FL_EXHIBIT_AP_INACTIVE_S`).
9. **Turning captions on and off with `/api/exhibit`, and storing the choice**; closing the UPDATE card of a failed upload with K4.
10. **After the artist patterns went in**: free internal heap while running (`heapInternal`, `heapLargest`) and fps per pattern. There is no running baseline for the placeholder state (it was not measured when flashing on 2026-09-18).
    - Static DRAM: six-artist build 145,816 B, placeholder build at the same commit 143,688 B (+2,128 B), default firmware 141,424 B. With the files as received, another 57,656 B would have been added; the conversion in step 3 ([§5](#replacement-steps)) moved that to PFMem. The largest static variable left in each pattern: BAKWAN `neighborDrifters` 640 B, HEOEUNBIN `pts` 456 B, JEONGEUNGYO `nodes` 396 B. None is over 1 KB.
    - PSRAM: at boot the six `setup()` calls take about 253 KB in total from PFMem (HANYEONGHOE 138,792 B, BAKWAN 45,968 B, YUJIN 34,112 B, CHOEGYEONGEUN 24,256 B, HEOEUNBIN 16,384 B, JEONGEUNGYO 0). On a board where PSRAM is not detected, this falls to the internal heap, most of it fails, and those patterns show a black screen. Check whether `heapInternal` right after boot dropped by this much.
    - fps: for the new headers (after `a5b5db9`) there are only host (PC) numbers — measure on the panel with `/status`. One frame on the host (at the centre): YUJIN 0.22 ms, HANYEONGHOE and HEOEUNBIN 0.09 ms, BAKWAN 0.06 ms, CHOEGYEONGEUN 0.05 ms, JEONGEUNGYO 0.04 ms. These numbers cannot be converted to the panel's speed, so use only their order. Below 20 fps, JEONGEUNGYO's motion itself also slows down (`dt` is clamped to 0.05 s). If a pattern is slow, talk with its artist.
11. **Windows kiosk** — a rehearsal that starts with the power unplugged, the crash recovery dialog, the power settings (on 2026-09-25 the monitor caught this laptop's USB selective suspend, battery hibernate after 5 hours, lid close = sleep, and PCI Express maximum power saving; fix them with `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition") at the top of the repo), and the startup shortcut paths (Live and Patternflow Monitor).
12. **When one reply to a session invitation is lost over Wi-Fi** — the panel sends that invitation 3 times (about 4 s), gives up, and invites afresh 1 s later, so it joins a few seconds late (older images: more than 20 s; deliberately not fixed on the FlowLocal side). The log should show `rtp invitation went unanswered`, and the session count should stay the same. Not provoked on purpose.
13. **FlowLocal's first boot** — an open `ESP_xxxxxx` network is visible for about 1 s right after erasing.
14. **Panel power** — there is no measurement anywhere of whether the J4 supply (a power bank or similar) lasts through a full day of operation (an item added while writing this document).
15. **Shared knob position** ([§1 "Knob position"](#1-at-a-glance), added on 2026-09-19) — the only thing confirmed on real hardware is that CC 24–26 = 64 arrive after `PROGRAM 1` when the session joins (2026-09-20, 2026-09-25) and when FlowLocal answers a resync request (2026-09-24). With `midi_monitor.py` running:
    - Turn K1 all the way: at the moment the picture stops, does CC 24 also stop at 0 or 127? Turn one step back: do both move at once?
    - When K4 changes the pattern, is not a single CC 24–26 line printed, only `PROGRAM`? Does the new pattern appear with the look of the same knob position?
    - 3-minute idle return: first move K1–K3 off the centre and set K4 to a pattern other than 1, then wait 3 minutes. Is CC = 64 printed for each knob that was off the centre, followed on the next frame by `PROGRAM 1`? (If the knobs are already at the centre and the pattern is 1, it is normal that nothing is printed: only CCs whose value changed and a PC whose pattern changed go out)
    - When FlowLocal restarts and the session rejoins, are the current values of CC 24, 25, 26 printed right after `PROGRAM n`? (Confirmed on 2026-09-25 with the centre value 64; not yet with the knobs moved)
    - When FlowLocal re-sends (the device's Resync, the monitor's **Re-sync the sound**, **Send current pattern to Live** on the status page, USB unplugged and plugged in), are `PROGRAM n`, `CC ch16 #118` and then CC 24, 25, 26 printed? (The value 64 was confirmed on 2026-09-24 with `BF 77 7F`.) FlowLocal must be flashed with `0c05e79` or later (`aa3f12d`, flashed on 2026-09-18, does not have this)
    - After moving a mapped parameter in Live with the mouse, turn the knob one step: with Takeover None, does the parameter go straight to the panel position, and is that jump acceptable to hear? When only Live restarts, do the parameters go to the panel position with the CCs FlowLocal re-sends? ([`ableton/README.en.md`](../ableton/README.en.md) §9 item 11)
    - On HEOEUNBIN, do the K1 press (direction) and the K2 press (rider) still work?
    - Do 32 steps from end to end feel right in the hand (`PF_EXHIBIT_KNOB_DETENTS`), and does the look at the centre (the table in [§5](#how-each-pattern-opens-at-the-knob-positions)) match what the artists intend — especially HANYEONGHOE (no clouds) and JEONGEUNGYO (no rotation)?

16. **Footstep notes** (added on 2026-09-20) — **Confirmed** (2026-09-20, flashing log): on HEOEUNBIN, `NoteOn 36` and `38` alternate on channel 10, 476 ms apart at K3's centre (473 ms calculated), note-off 40 ms later, no notes left hanging, and other patterns send none. Still open: the rate at both ends of K3 (0.6 / 7.2 per second), whether the note comes at the same moment the foot lands on the LEDs, and whether the notes stop on a K4 switch and when the panel goes to sleep. The Live side: [`ableton/README.en.md`](../ableton/README.en.md) §9 item 12.
17. **Signals arriving in bunches while a knob is turned** (added on 2026-09-25) — the "feeling that piled-up signals come over one at a time" has not yet been measured while turning the knobs. The monitor now measures it. Keep turning K1–K3 and check that the **신호 도착** ("Signal arrival") bars run on evenly, that **몰림 n개** ("n bunched") does not appear more than 3 times a minute, and that the jitter FlowLocal measures, `jt`, is below 20 ms and the worst lateness in a minute, `jm`, below 250 ms ([`tools/monitor/README.en.md`](../tools/monitor/README.en.md) "Thresholds"). For `jt` and `jm` there are only host-test values so far. Measure on channel 1 (the channel chosen on 2026-09-25), and measure again at the venue.
18. **New features of 2026-09-25 not yet seen on hardware** — the panel's Wi-Fi self-heal (60 s, `midi.wifiHeals`), the monitor's guidance for a panel that has lost power, how long the channel scan takes and what it hears at the venue (the COM socket log cannot be seen there, so look at `ap_channel_scan` in FlowLocal's `/api/status`), and the guidance when a monitor button is refused (when the panel is not on the hotspot, when several devices are on the hotspot).

Fixed, but not confirmed on hardware:

- **The panel counting an RTP session twice** (panel `ba29402`, `198abaa`). Before, if the library reported a connection of the same session twice, the panel counted two sessions. Then the count did not go back to 0 after the session ended, and the panel might not invite FlowLocal again until it rebooted (the "MIDI never joins" row of the old §7 table). Now:
  - Sessions are counted by SSRC. A second report of the same session counts once (log `reported joining again - counted once`), and the PC is not re-sent at that point.
  - The end report of an invitation the library gave up on is not treated as the end of a session (log `rtp invitation went unanswered - not a session`).
  - While an earlier invitation may still be open, no second invitation is sent to the same address.
  - MIDI output is not tied to the session count. The session count is used only for the status display and to decide whether to invite again.
  - What has been checked: the host tests (`check_midi.py`), the build, and on 2026-09-25 one Restart the panel and one Restart FlowLocal from the monitor, after which it rejoined (4.7 s, about 14 s). On real hardware: power-cycle the panel and reboot FlowLocal several times each, and check that `midi.rtpPeers` in the panel's `/api/status` never goes above 1, that it rejoins within those times after every drop, and that `rtp session list was full` does not appear on serial.

Known and left as is (not faults, but what remains by design):

- The panel does not block incoming MIDI. It relies on FlowLocal not passing anything from the laptop (resync requests included) on to the panel. Anything that sends a PC to the panel can select any pattern number.
- Cases where the shared knob position breaks (none of them happen in exhibition operation: FlowLocal passes nothing to the panel, and the console is not used during the show):
  - The knob slider on the console home (`pN` of `/api/params`) and MIDI CC 20–22 coming into the panel (absolute) hold **only the picture** of that knob at that value and leave the sound (CC) as it is. Turning that knob one step by hand releases it. If the held value (slider, CC 20–22) last changed less than 5 s ago, picture and sound both stay still until those 5 s have passed, then go to the knob position together.
  - When `dN` of `/api/params` or CC 24–26 (relative) comes into the panel, for 5 s the pattern follows those clicks in its own way for that knob. During those 5 s, a visitor turning that knob moves **neither the picture nor the sound** (the position does move by the amount turned). After 5 s, picture and sound go to the knob position together. `dN` also moves the CC during that time (×4 per click; relative if Relative is saved on `/midi`). Incoming CC 24–26 sends no CC out (the panel does not send back the MIDI it receives).
  - Anything that reads knob clicks directly does not get the knob position. In this build, the core's calibration card (`/api/display?screen=N`, the address the website's panel tuner uses) moves K1 (screen) and K2 (White Lv) by 1–2 per step, stops at the ends of the shared position, and jumps all at once on the frame after K1–K3 are pressed (with K2 at an end, White Lv is about 192). Adjust the card with `screen=` and `level=` of `/api/display`. The same goes for old `.pfm` modules picked in the console that read clicks directly. None of the six artist patterns is affected (they all use `PFParams::apply`).
- For the one frame (1/60 s) in which a button is pressed, that knob's position does not reach the pattern ([§1 "Knob position"](#1-at-a-glance)). Whatever is turned while pressing shows one frame late. In a pattern where a press resets a parameter (Origin), the reset shows for only one frame and the knob position wins. Of the six artist patterns, HEOEUNBIN is the one that uses presses, and it changes state that is not a parameter (direction, rider), so it is not affected.
- The pattern chosen at a pattern switch, an idle return or boot is written to NVS 3 s later. At the scale of an exhibition, flash wear is not a problem.
- A pattern's time accumulator (its clock) keeps running through the idle return.
- The core's centre name display (`src/core_ui_text.h`) also breaks names longer than 10 characters in the middle of a word in portrait. In the exhibition it shows only when a pattern is picked from the console. Left as a separate issue to fix in the main repository.

## Flashing log

*(Translator's note: rows are kept in the original's order, which is not strictly by date; the last image on both boards was the 2026-09-30 one (panel `502a3c11…`, FlowLocal `4ecbcca0…`, `flx-mon3`). The `firmware_images/…` files named here are not in the public copy.)*

| Date | Board | What | Result |
|---|---|---|---|
| 2026-09-18 | Panel DevKit, MAC `44:bd:8d:75:e3:54` (COM17, taken out of the panel) | Full erase + panel exhibit `382147e` (bootloader/partitions/boot_app0/firmware) | Hash verified. Boot log: `[EXHIBIT] 6 patterns at 1..6, default 1 (Lissajous Weave), captions on`, `connecting to "PatternflowLocal" (built-in creds)`. FFat reads `not formatted - presets only` because it had just been erased (not needed for the exhibition; if you want, format it from `/patterns` in the console) |
| 2026-09-18 | FlowLocal DevKit, MAC `14:c1:9f:32:29:6c` (COM11 = CH343 COM socket) | Full erase + `firmware.factory.bin` @0x0 (flowlocal `aa3f12d`, code identical to `5bcce79`, image check PASS) | Hash verified. Boot log: `usb device "Patternflow Exhibit" started`, `AP "PatternflowLocal" on 192.168.66.1 ch6, max 3 stations`, `rtp listener "FlowLocal" … ssrc 5b4ddcda`, `program mirror: CC 118 ch 16`, `exhibit ready`. `No core dump partition found` is a harmless line that appears because the partition table has no core dump partition |
| 2026-09-19 | Panel DevKit `44:bd:8d:75:e3:54` (COM17) | Image with the six artist patterns `013fd9b` (boot_app0 + firmware only, settings and FFat kept, sha256 575244c6…) | Hash verified. Boot log: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions on`; the memory of the earlier temporary pattern was gone, so it started Origin → BAKWAN. fps not measured yet (this desktop has no Wi-Fi) |
| 2026-09-19 (night) | Panel DevKit `44:bd:8d:75:e3:54` (COM17) | Image with the six re-translated patterns `fba3e53` (boot_app0 + firmware only, settings and FFat kept, 1,167,952 B, sha256 95de6f07…) | Hash verified. Boot log: `[EXHIBIT] 6 patterns at 1..6, default 1 (BAKWAN), captions on`, no hangs or errors (`nvs_open NOT_FOUND` and FFat -1 as before). esptool 5.2 crashes on the progress-bar characters in a Korean-locale console (cp949) — flash with `PYTHONUTF8=1 PYTHONIOENCODING=utf-8` in front of the command. fps not measured yet |
| 2026-09-20 | Panel DevKit `44:bd:8d:75:e3:54` (COM17) | Footstep notes image `235f06a` (boot_app0 + firmware, 1,168,608 B, sha256 bd70a7b2…) | 14 s after boot, `PROGRAM 1` + CC 24/25/26 = 64. After switching to HEOEUNBIN, notes 36/38 alternated on channel 10, 476 ms apart at K3's centre (473 ms calculated, 0.6% off), note-off 40 ms later, no notes left hanging. None from the other patterns. Log in `C:/Users/lsh/pfx-logs/footsteps.log` |
| 2026-09-24 | Panel DevKit `44:bd:8d:75:e3:54` (COM17) | **300 Hz** image `0e1d957` (`firmware_images/panel/`, boot_app0 + firmware, 1,168,944 B, sha256 94633a5a…) | Hash verified. Joined the hotspot (192.168.66.2), no errors. Only the one 300 Hz commit was taken from main v3.10.4 |
| 2026-09-24 | FlowLocal `14:c1:9f:32:29:6c` (COM11 = the right-hand COM socket) | Full erase + `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`0c05e79` = `f9dcd9d`, knob value re-send) | Hash verified. Panel RTP session connected. After moving to the left-hand socket and sending a resync (BF 77 7F), `PROGRAM 1` + the CC118 mirror + **CC 24/25/26 = 64** came back (all 3 times). The `wifi:CCMP mgmt frame ... non-zero reserved bit` in the log was also there with the old firmware on 2026-09-19, so it is not a problem |
| 2026-09-25 | Panel DevKit `44:bd:8d:75:e3:54` (this laptop's COM10, the DevKit's own USB) | 2026-09-25 interim image (boot_app0 + firmware, settings kept, 1,170,496 B, sha256 12bc93a1…): `0e1d957` + joins only `PatternflowLocal`, invite host and MIDI-on locked, first invite after 1 s, 3 attempts, re-invite 1 s after a session ends, re-invite when Wi-Fi comes back, Wi-Fi self-heal, 19.5 dBm | Hash verified. The numbers after it joined are in ["Field measurements (2026-09-25)"](#field-measurements-2026-09-25-laptop-panel-devkit--flowlocal) below |
| 2026-09-25 | Panel DevKit `44:bd:8d:75:e3:54` (COM10) | Final image `07b4c70b…` (`firmware_images/panel/firmware.bin`, 1,170,496 B, boot_app0 + firmware, settings kept). The same code as the image above, with only "do not invite while Wi-Fi is down" added | Hash verified. About 1 s after it came back up, `PROGRAM 1` (BAKWAN) reached the laptop (monitor log 19:12:47 → 19:12:48). Monitor "모두 정상" ("All good") |
| 2026-09-25 | FlowLocal `14:c1:9f:32:29:6c` (USB socket; shows up on this laptop as COM11 in BOOT+RST download mode) | `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`flx-mon1`, 1,227,168 B, sha256 3e3b7e9d…), not erased | Hash verified. After esptool printed `Hard resetting via RTS pin` the board stayed in download mode; pressing RST brought it up with the new firmware. At power-up it chose channel 1 by itself, and the monitor started receiving its status (`펌웨어 flx-mon1`, "firmware flx-mon1") |
| 2026-09-30 | Panel DevKit (COM10) + FlowLocal (USB socket, download mode) | Panel `502a3c11…` (patternflow `006e9c1`: re-invite 1 s after a refusal, dead session detected in 12 s, no captions, new colours for YUJIN), FlowLocal `4ecbcca0…` (`flx-mon3`, flowlocal `c6360dc`) | Hashes verified; FlowLocal flashed, then RST. Measured: reconnect the panel 15/15 in 1.5 s, panel reboot 8/8 (0.8–1.3 s after joining the hotspot), FlowLocal reboot 6/6 (15–16.5 s), channel command 3/3, recovery from rapid repeated commands ≤1.8 s (0 refusals, timeouts or lost packets), the monitor's **한 번에 다시 연결하기** ("Reconnect everything") 2/2 (86 s with Live closed, 17 s with Live open). Example channel scores: 1:93, 6:186, 11:119 |
| 2026-09-25 | Panel DevKit `44:bd:8d:75:e3:54` | **Waiting to be flashed**: image without captions `2471b15b…` (`firmware_images/panel/firmware.bin`, 1,170,480 B, patternflow `7350445`). How to flash it: [§4.2](#42-panel--flash) (COM10, boot_app0 + firmware, settings kept) | — |
| 2026-09-25 | FlowLocal `14:c1:9f:32:29:6c` (USB socket, download mode COM11) | `firmware_images/flowlocal/firmware.factory.bin` @0x0 (`flx-mon2`, 1,232,064 B, sha256 b48b0ee8…), not erased: self-recovery (USB reconnect, restart) and the restart reason | Hash verified. Again it needed RST to come up. `fw=flx-mon2`, `rsn=poweron` (RST shows up as power-on), `bc=1`, `ur=0`. Channel 1. With the panel switched on, the session came 2 s after it joined the hotspot |

## Field measurements (2026-09-19, desktop, panel DevKit + FlowLocal)

| Situation | Result |
|---|---|
| Panel reboot (FlowLocal left on) | Joined the hotspot at 3.2 s → MIDI connected at 23.2 s; at that moment `PROGRAM 1` + `CC 118=1` arrived at the computer |
| FlowLocal quick restart (before the fix, before `ebcbe08`) | The panel did not notice the drop and held on to the old connection → **MIDI dead for more than 2 minutes** (cleared only by rebooting the panel) |
| FlowLocal off for 15 s | The panel detected the drop at 8.5 s → rejoined 0.2 s after the hotspot came up → MIDI back 1 s later |
| FlowLocal quick restart (after the fix: hotspot 12 s late at power-up) | Reset: drop detected at 8.5 s → rejoined at 13.4 s. USB unplugged and plugged back in (about 1 s): detected at 8.5 s → rejoined at 16 s → MIDI back at 17 s. USB unplugged for about 6 s and plugged back in: `PROGRAM 1` arrived at the computer 22 s later |
| Resync request `BF 77 7F` (computer → FlowLocal) | Reply `PROGRAM n` + `CC 118=n` within 10 ms |

Cause and fix: if FlowLocal comes back up within 1 s with the same SSID and BSSID, the panel does not notice the loss of beacons (about 8 s), and because of protected management frames (PMF) it also ignores the new AP's disconnect, so it keeps holding on to the dead connection. On neither side can PMF be switched off in the configuration (`capable` is a deprecated option), so FlowLocal was made to stay off the air for 12 s after it powers up (`FL_EXHIBIT_AP_START_DELAY_MS`, flowlocal `ebcbe08`). The USB MIDI port comes up at once during that time.

Fixed at the same time: `tools/midi_monitor.py` died silently on Windows while closing the port when the USB MIDI device disappeared. Now an input that has disappeared is not closed but left alone, and on exit the script leaves with `os._exit`. After the fix, an unplug and replug was logged all the way through `port disappeared` → `port back` → `PROGRAM 1`.

### Live check (2026-09-19, Live 12 Suite on that desktop)

- **Live passes Program Change from the track input through to the Max for Live device's `[midiin]`.** The device's upper line showed `PC 6 ch1 -> 6: …` and the scene fired (the PC line, not the CC 118 line). So **Alt CC stays off**. It is kept only as an emergency path.
- The device's **Resync** → FlowLocal's reply (PC) → scene fired: works. The MIDI To path is confirmed too.
- Why it did not work at first (this actually happened): ① `Ch. 16` was put in the **MIDI From** channel box (→ the PCs on channel 1 were filtered out) ② Monitor was set to **Auto** (unless the track is armed, the input does not reach the device) ③ MIDI To was **No Output** (the request did not go out to FlowLocal). Correct values: From = Patternflow Exhibit / **All Channels**, Monitor = **In**, To = Patternflow Exhibit / **Ch. 16**.

## Field measurements (2026-09-25, laptop, panel DevKit + FlowLocal)

With panel `12bc93a1…` and FlowLocal `flx-mon1` (`3e3b7e9d…`). "Old image" means the images flashed on 2026-09-24 (panel `0e1d957`, FlowLocal `0c05e79`).

| Situation | Result |
|---|---|
| Cause of "will not join" | Only the home Wi-Fi (`KT_GiGA_E368`) was saved on the panel. Because of the old rule that a saved network stops the compiled one from being tried, the panel never once tried `PatternflowLocal` and kept inviting 192.168.66.1 on the home Wi-Fi (nothing but `rtp invitation went unanswered`, over and over). Cleared by deleting the saved network by hand. With the exhibition image this can no longer happen ([§3](#3-network)) |
| Wi-Fi at this spot (laptop scan) | The home router and its mesh on channel 9 (signal about 60%), a neighbour on channel 8. FlowLocal was on channel 6 (overlapping). The new FlowLocal picked channel 1 by itself at power-up |
| Panel signal strength (seen from FlowLocal) | −37 to −43 dBm |
| Old image: panel reboot → MIDI | 24 s (almost the same as the 23.2 s on 2026-09-19; the old panel made its first invitation 20 s after joining the Wi-Fi) |
| New image: FlowLocal power-up → first MIDI | The hotspot came up at 12 s, and 2.7 s later the panel session and the first MIDI (`PROGRAM 1`, CC 118 mirror, CC 24–26 = 64). About 14.7 s after FlowLocal got power |
| Monitor **패널 연결 다시 잡기** ("Reconnect the panel") | Reply at once; MIDI back after 1.0 s |
| Monitor **패널 다시 켜기** ("Restart the panel") | Reply in 0.04 s, the panel reboots, MIDI back after 4.7 s |
| Monitor **FlowLocal 다시 켜기** ("Restart FlowLocal") | FlowLocal reboots; MIDI back about 14 s after it comes up (12 s of which is the deliberate quiet time) |
| Panel signal (heartbeat) | Reaches FlowLocal every 10 s |
| First USB connection | Once, Windows reported FlowLocal as "Unknown USB Device (Device Descriptor Request Failed)". Unplugging and plugging it back in fixed it, and it did not happen again. Cable or port suspected |
| Laptop power settings | The monitor caught "USB selective suspend = Enabled" (both AC and DC turned off at 19:15). It then also caught battery hibernate after 5 hours, lid close = sleep, and PCI Express maximum power saving. `노트북 전시용 전원 설정.bat` ("Laptop power settings for the exhibition") at the top of the repo fixes them all at once |
| FlowLocal hang (old `flx-mon1`) | After it was plugged back in at 21:02 it restarted itself 4 times in 80 s (Windows and Live logs), and after that gave no answer over USB (the hotspot stayed up). RST cleared it. Plugged back in at 22:09, and from 22:10:42 the same state went on for an hour. The cause was not pinned down (thought to be one of: a missed USB suspend, a blocked USB endpoint, unstable power). In this state `flx-mon2` reconnects USB or restarts by itself, and records the reason |
| Live crashes | At 15:52 and 16:17, c0000005 inside `rthdasio64.dll` (Realtek ASIO). Switched to FlexASIO ([§2 "The laptop and sound"](#2-hardware-connections)) |
| New panel connection (`flx-mon2`) | 23:10:01 joined the hotspot → 23:10:03 session and `PROGRAM 1`. Signal −17 dBm |

Summary: when only the panel restarts, the time to MIDI went down from 24 s to 4.7 s. When FlowLocal restarts, most of the time is the deliberate 12 s of quiet (2026-09-19, ["Cause and fix"](#field-measurements-2026-09-19-desktop-panel-devkit--flowlocal)).
