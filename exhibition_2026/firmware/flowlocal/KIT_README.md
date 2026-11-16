# FlowLocal Kit — start here

This release is a **complete show kit**: FlowLocal ESP32 + Patternflow panel
firmware (**3.9.5 Performance P5.5** — playlist up to **64**, MQTT Publisher
param/sequence mirror, NETWORK CLK + SELECT Sequence, stable web console) + USB stick files. After the devices are flashed
once, you run the show from a **tablet** (no PC broker).

| Phase | What you need |
|---|---|
| **Once** | A PC (or any machine that can flash ESP32) + this release |
| **Every show** | Tablet or phone on FlowLocal Wi‑Fi + browser |

---

## Quick path (single island)

Do these in order. Details for each step are below.

1. **Hardware** — Close the **USB-OTG** jumper. Power FlowLocal from **COM**
   (or the portable case 5.15 V rail — see [`BUILD.md`](BUILD.md)). Stick goes in **USB** (not COM).
2. **USB stick** — Format FAT32, copy the stick image onto it.
3. **Flash FlowLocal** — Write `flowlocal.bin` + `flowlocal-littlefs.bin` over **COM**.
4. **Flash each panel** — Write the matching Patternflow `.bin` (serial or OTA).
5. **Configure each panel** — Wi‑Fi `PatternflowLocal` / `patternflow`, MQTT mode **Director**,
   preset **FlowLocal show AP**, host `192.168.66.1` → Connect to Director.
6. **Tablet** — Join that Wi‑Fi → open `http://192.168.66.1` → **Open Director** → **Launch**.
   Use **← FlowLocal** in the Director header to return to the appliance dashboard.

If something fails, jump to [Troubleshooting](#troubleshooting).

---

## What to download from this release

GitHub lists files **flat** at the bottom of the release. Same bits also sit
inside `flowlocal-kit-*.zip` under folders. Either works.

| Download this | Use it for |
|---|---|
| `KIT_README.md` | This guide |
| `BUILD.md` | Portable case assembly (BOM, wiring, photos, STLs) |
| `flowlocal.bin` | FlowLocal appliance firmware (app) |
| `flowlocal-littlefs.bin` | FlowLocal web UI / Director (must flash too) |
| `flowlocal-sd.bin` | Optional: MicroSD build (no USB stick, no TFT) |
| `patternflow-firmware.bin` | Panel firmware — **128×64** (and usual single-panel) |
| `patternflow-firmware64x2.bin` | Panel firmware — **two 64×64** daisy-chained |
| `usb-flash-image.zip` | Everything for the FAT32 stick |
| `flowlocal-kit-*.zip` | All of the above in one archive (`firmware/`, `panels/`, …) |

Inside the kit zip the same files are named:

- `firmware/flowlocal.bin`, `firmware/flowlocal-littlefs.bin`, …
- `panels/patternflow-firmware.bin`, `panels/patternflow-firmware64x2.bin`
- `usb-flash-image.zip`

---

## Critical hardware notes (read before plugging cables)

Boards: **ESP32-S3 N16R8** with **two** USB-C jacks labelled **COM** and **USB**.

| Jack | Job in this kit |
|---|---|
| **COM** | Flash + serial + **power** while a stick is attached |
| **USB** | Stick host only (mass storage). **Not** a PC data cable |

### USB-OTG jumper (bridge)

Factory boards often ship with this jumper **open**. Then the **USB** jack has
data lines but **no 5 V**, so the stick never mounts.

**Close the USB-OTG jumper** (solder blob, or header + removable jumper) so
COM and USB share VBUS. Then: power from **COM**, stick in **USB**.

**Warnings**

- Do **not** plug a PC into **both** jacks with the jumper closed (shorts 5 V).
- With kit firmware, the **USB** jack is a **host** for the stick — you cannot
  use that same jack as USB-Serial to a PC at the same time.
- Leave the jumper **off** only if you need native USB device-mode on **USB**
  (not this kit’s normal show setup).

### Stick format

- **FAT32 only** (not exFAT / NTFS). Label e.g. `FLOWLOCAL`.
- Large sticks: Rufus or [guiformat](http://ridgecrop.co.uk/index.htm?guiformat.htm).
- Unzip `usb-flash-image.zip` and copy its **contents** to the **root** of the
  stick so you see folders named `patterns/`, `shows/`, `launchpads/`, `director/`.
  Do not nest them under an extra folder.

---

## 1 — Flash FlowLocal (the new ESP32)

1. OTG jumper closed; stick prepared and plugged into **USB**; PC on **COM**.
2. Flash **both** images (app alone is not enough — without LittleFS, Director is missing):

| File | Flash offset |
|---|---|
| `flowlocal.bin` | `0x10000` |
| `flowlocal-littlefs.bin` | `0xC10000` |

Example (`esptool`, change `COMx`):

```text
esptool.py --chip esp32s3 --port COMx write_flash ^
  0x10000  flowlocal.bin ^
  0xC10000 flowlocal-littlefs.bin
```

Or from a clone of this repo with PlatformIO:

```powershell
python -m platformio run -e flowlocal -t upload
python -m platformio run -e flowlocal -t uploadfs
```

3. Open serial on **COM** (115200). You want media mounted and `[FlowLocal] ready.`

**MicroSD instead of USB stick:** use `flowlocal-sd.bin`, wire CS=4 SCK=5 MOSI=6
MISO=7, no TFT on those pins. OTG jumper not required for SD.

---

## 2 — Flash Patternflow (each LED panel)

Pick the bin for your geometry:

| Panel | File |
|---|---|
| 128×64 (stock / banner-style) | `patternflow-firmware.bin` |
| Two 64×64 daisy-chained | `patternflow-firmware64x2.bin` |

**First flash on a blank panel** usually needs USB/serial (or your vendor’s
flash tool), same as any ESP32 project. After that you can use the panel’s
web **OTA** (`/update`) when it is on Wi‑Fi.

These bins include **MQTT → Director** with a **FlowLocal show AP** preset
(required for the tablet show).

---

## 3 — Tell each panel to use FlowLocal

On the panel’s web console (home Wi‑Fi is fine for this step, or the show AP):

1. **Wi‑Fi** — add network  
   - SSID: `PatternflowLocal`  
   - Password: `patternflow`  
   Join that network when you perform.
2. **MQTT** — select **Director**  
   - Preset: **FlowLocal show AP**  
   - Host: `192.168.66.1` (factory AP IP)  
   - **Connect to Director**
3. When you go home, switch MQTT back to **Normal** (or Director → **PC broker**
   on your LAN). Leaving a FlowLocal preset selected on home Wi‑Fi makes the
   panel probe a missing `.66.1` and feel sluggish.

Optional compile-time Wi‑Fi in `patternflow_secrets.h`:

```cpp
#define PF_WIFI_SSID "PatternflowLocal"
#define PF_WIFI_PASS "patternflow"
```

---

## 4 — Run the show from a tablet (no PC)

1. Power FlowLocal from **COM**; stick in **USB**.
2. On the tablet, join Wi‑Fi **PatternflowLocal** / **patternflow**.
3. Browser → `http://192.168.66.1` (or `http://flowlocal.local`).
4. Tap **Open Director** → **Connect** (WebSocket is already
   `ws://192.168.66.1:9001`) → open **Launch**.
5. **Load example** for *Simone Performative Deck*, or open
   `simone_performative_deck.json` / `flowlocal_starter.json` from the stick.
6. Panels on the same AP, with MQTT **Director** + FlowLocal preset, follow Launch pads.
   Tap **← FlowLocal** in Director to return to the appliance dashboard.

To copy demos onto a panel’s own storage: Director **File** tab, or see
`docs/WIFI_PULL.txt` on the stick.

**Sequences on the panel:** upload/pull `.pfs` → panel **/show** → tick →
**Start**. Hold **K4** for SELECT; turn **K3** for Normal ↔ Sequence.

---

## 5 — Several FlowLocal boxes (multi-island)

Each box needs its **own AP SSID** and **own AP IP** (dashboard → Access Point).

| Island | Example SSID | Example AP IP |
|---|---|---|
| A | `PatternflowLocal` | `192.168.66.1` |
| B | `PatternflowLocal-B` | `192.168.67.1` |

On each panel: store every island in the Wi‑Fi list; set MQTT **Director** host
to that island’s IP with a FlowLocal preset. Director on island B:
`http://192.168.67.1/director/` · `ws://192.168.67.1:9001`.

---

## Optional TFT

USB build only: 1.8" ST7735, SCK=5 MOSI=6 CS=4 DC=7 RST=8 BL=3 @ **3.3 V**.
Same pins as the SD alternate — choose **USB + TFT** or **SD**, not both.
This firmware uses the **green-tab** window offset (common 128×160 clones).
No display? Soft-fail; the appliance still works.

## Portable case (18650 + J5019)

Illustrated build (BOM, switch on battery +, J5019 @ **5.15 V**, STLs, photos):
**[`BUILD.md`](https://github.com/SimonePDA/flowlocal/blob/main/BUILD.md)**
in the repo. Schematic:
[`build_pics/Schematics.png`](https://github.com/SimonePDA/flowlocal/blob/main/build_pics/Schematics.png).
STLs are in [`3D_STL/`](https://github.com/SimonePDA/flowlocal/tree/main/3D_STL).
The kit zip also includes `BUILD.md`, the schematic, and the STLs.

---

## Troubleshooting

| Symptom | What to check |
|---|---|
| Stick never mounts / `devices=0` | OTG closed? Stick in **USB**? Power from **COM**? FAT32? Power-cycle cold. |
| Director Connect stays disconnected (no stick) | Update `flowlocal.bin` — older builds blocked MQTT while retrying USB mount. Broker works without media; stick is only for shows/decks. |
| No Wi‑Fi named PatternflowLocal | FlowLocal app flashed? Serial say ready? |
| Director missing / blank page | Flash **littlefs** as well as `flowlocal.bin`. |
| Tablet can’t open 192.168.66.1 | Tablet joined **PatternflowLocal**? |
| Panel ignores Launch | Panel on show AP? MQTT = **Director** + FlowLocal preset? Host IP correct? |
| Panel sluggish at home | MQTT still on a FlowLocal preset → switch to **Normal** or **PC broker**. |
| Windows can’t see the stick while FlowLocal runs | Normal: host mode owns **USB**. Unplug stick to edit on a PC. |
| Confused by file names | Loose release assets are flat; kit zip uses `firmware/` and `panels/` folders — same files. |

---

## Maintainers — rebuild this kit

```powershell
cd D:\Documenti\Arduino\flowlocal
python tools/make_flowlocal_kit.py
# → dist/flowlocal-kit-*.zip
```

Needs a sibling `../patternflow` tree with `dist/firmware.bin` and
`dist/firmware64x2.bin` already built on the **Patternflow** PlatformIO
toolchain (`espressif32@7.0.1`), not FlowLocal’s pioarduino toolchain.
