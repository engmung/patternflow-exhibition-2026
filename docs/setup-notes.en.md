# Install and operations notes — Patternflow exhibition 2026

> **English translation.** The Korean original is [`README.ko.md`](../README.ko.md). It was written for the crew of the 2026 exhibition *Anbu* in Seoul and describes that installation as it was — the exhibition laptop's paths, COM ports and dates are kept as facts. In this public copy the hotspot password reads `YOUR_HOTSPOT_PASSWORD`; the prebuilt firmware images, `flasher.html`, `backups/`, the `patternflow/` and `flowlocal/` clones and the audio samples are not included, and the FlowLocal firmware source is not public — the [front page](../README.md) says what to do instead. Where the original says "the root `README.md`", it means the Korean notes now in [`README.ko.md`](../README.ko.md), translated in [`docs/setup-notes.en.md`](setup-notes.en.md) (this file). The English [front page](../README.md) is the short guide; these notes are the full detail.

At a paid exhibition opening around 2026-10-01, visitors touch one Patternflow panel, and the sound comes from Ableton Live on a laptop under the desk. This folder collects what that installation uses: the two firmwares (panel and FlowLocal), the Live device and the check tools.

> **This document contains the hotspot password.** This repository is pushed only to a private GitHub repository (`engmung/patternflow-exhibition-2026`). Do not make it public, and do not copy it anywhere else to share it.

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
python tools/check_exhibit_image.py             # must print PASS
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
   - `[midi] pf monitor: status request F0 7D 50 46 01 F7 answered … (v1, fw=flx-mon2, …)` and `[midi] pf monitor: commands 10 reconnect panel, 11 reboot panel …, 12 reboot FlowLocal …` (if these two lines are missing, it is an old image that cannot answer the monitor)
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
