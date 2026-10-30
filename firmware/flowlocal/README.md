# FlowLocal

A **standalone Patternflow show appliance** built on an ESP32-S3 N16R8.

Lives in its own tree (`../flowlocal` next to the Patternflow repo) so its
PlatformIO platform (pioarduino / Arduino-ESP32 3.x) never overwrites the
Patternflow release toolchain.

It replaces the PC/Mac broker for local or touring performances by acting as:

| Role | Detail |
|---|---|
| **Wi-Fi Access Point** | SSID `PatternflowLocal` / pass `patternflow` (configurable; multi-island OK) |
| **MQTT broker** | Port 1883 (TCP) + 9001 (WebSocket for Director) |
| **Web console** | `http://flowlocal.local` — status, STA, display fields, weather hub, OTA |
| **Media** | **Default:** USB-C stick on **USB** jack. **Alternate:** MicroSD (proven) on SPI 4/5/6/7 |
| **Optional TFT** | 1.8" ST7735 128×160 on the same SPI pins as SD (USB build only) |
| **Weather hub** | STA pulls OpenWeather ~30 min; panels use `GET /api/weather` |

## Kit release (downloadable)

**[FlowLocal Kit releases](https://github.com/SimonePDA/flowlocal/releases)** ship appliance
bins, Patternflow panel bins, and a USB stick image. Start with **`KIT_README.md`**
on the release: **PC once to flash**, then **tablet-only** for the show.

Maintainers rebuild with:

```powershell
python tools/make_flowlocal_kit.py
```

---

## Build the portable case

Step-by-step assembly for the 3D-printed enclosure, 18650 battery, **J5019**
charge+boost module, power switch, and optional ST7735 status display:

| Resource | Description |
|---|---|
| **[`BUILD.md`](BUILD.md)** | Full build guide — BOM, wiring, photos, first power-on |
| [`build_pics/Schematics.png`](build_pics/Schematics.png) | Power + TFT wiring schematic |
| [`3D_STL/`](3D_STL/) | Case bottom and top (`FlowLocal_Case_bottom.stl`, `FlowLocal_TOPCase.stl`) |
| [`build_pics/`](build_pics/) | Reference photos from a completed build |

**Power layout:** case USB-C → J5019 `IN`; 18650 **+** through a switch → `BAT+`;
`VOUT+` → ESP32 **5V**. Switch **on** powers the ESP and connects the cell for
charge+use when USB is plugged in. Switch **off** fully isolates the battery.
Set J5019 **VOUT ≈ 5.15 V** before wiring the board. Close the **USB-OTG**
jumper so the FAT32 stick on the **USB** jack gets VBUS.

---

## Hardware

| Part | Spec |
|---|---|
| ESP32-S3-DevKitC-1 / YD N16R8 | 16 MB flash, 8 MB PSRAM, two USB-C jacks |
| USB-C flash (FAT32) | Jack labelled **USB**, not **COM** (default media) |
| MicroSD SPI (alternate) | CS=4, SCK=5, MOSI=6, MISO=7 — use `-e flowlocal-sd` (no TFT) |
| ST7735 1.8" (optional) | SCK=5 MOSI=6 CS=4 DC=7 RST=8 BL=3, **3.3 V** — USB build only |

**Portable case:** see **[Build the portable case](#build-the-portable-case)** above
([`BUILD.md`](BUILD.md), schematic, STLs, photos).

**Display vs SD:** those SPI pins are shared. Choose **USB + optional TFT** (`flowlocal`) or **MicroSD without TFT** (`flowlocal-sd`). MicroSD path is proven and kept for venues that prefer a card over USB.

Unplugging the TFT does not change FlowLocal behaviour (soft-fail).

On these boards the header marked **5V** is **VIN** (input), not a USB-host supply. The native **USB** jack already carries D−/D+ (GPIO19/20). What it lacks, with the factory **USB-OTG** solder jumper **open**, is **5 V on VBUS** — so a stick plugged in there stays dead.

**USB-OTG jumper closed** (solder blob, or a 2-pin header + removable jumper): VBUS of **COM** and **USB** are tied. Power the board from **COM** (PC or 5 V adapter); the stick on **USB** then gets 5 V. No pigtail.

Leave the jumper **off** when you plug a computer into the native **USB** jack (USB Serial/JTAG / CDC). Do not put a PC on both jacks with the jumper on — that shorts two 5 V sources.

**COM** (CH343) is always the flash/serial port. Closing USB-OTG does not steal that. The native PHY cannot be a mass-storage **host** and a PC gadget at the same time: `-e flowlocal` is stick host; pull the jumper and use device-mode firmware if you need USB-JTAG on that jack.

---

## Getting started

### 1 — Shared AP credentials

`flowlocal_secrets.h` ships with the fixed AP (`PatternflowLocal` / `patternflow`).
That file is **committed on purpose** — every panel must know the same network.
Only fill in optional `FL_STA_*` if you want compile-time upstream Wi‑Fi; otherwise set STA + timezone on the dashboard (NTP runs when STA is up).

Use the same SSID/password in your panels' `patternflow_secrets.h`:

```cpp
#define PF_WIFI_SSID "PatternflowLocal"
#define PF_WIFI_PASS "patternflow"
```

### 2 — Build & flash (USB default)

USB-OTG jumper on; stick on **USB**; flash over **COM**:

```powershell
cd D:\Documenti\Arduino\flowlocal
chcp 65001
$env:PYTHONIOENCODING = "utf-8"
python -m platformio run -e flowlocal -t upload
python -m platformio run -e flowlocal -t uploadfs
```

MicroSD alternate (no TFT): wire CS4 SCK5 MOSI6 MISO7, then `-e flowlocal-sd`.

If upload cannot find the board:

1. Hold **BOOT**, tap **RESET**, release **BOOT**.
2. List ports with `python -m platformio device list`.
3. Upload again with the UART COM port:
   `python -m platformio run -t upload --upload-port COM5`
   (use the port you actually see).

After boot you should see `[FlowLocal] ready.` Serial stays on the **COM** port.

Join Wi-Fi `PatternflowLocal` / `patternflow` and open `http://192.168.66.1`. When the stick is mounted, tap **Open Director**, then **Connect** and **Launch**.

Director: `ws://192.168.66.1:9001` (filled automatically when served from FlowLocal)  
Panels: MQTT host `192.168.66.1`, port `1883`

### 3 — Connect

1. Join the `PatternflowLocal` Wi-Fi from your tablet / laptop.
2. Open `http://192.168.66.1` → **Open Director** → **Connect** → **Launch**.
3. Panels connect to `192.168.66.1:1883` automatically once they are on the same AP.

---

## USB flash payload

The repo already includes a ready-to-copy image at `flash_image/` (payload v2).
Rebuild anytime (needs `../patternflow` or `PATTERNFLOW_ROOT` for release zips):

```sh
cd D:\Documenti\Arduino\flowlocal
python tools/make_flash_image.py
```

Shipped layout (copy these folders to the **root** of a FAT32 stick):

| Path | Contents |
|---|---|
| `patterns/128x64/` | Absolute-ready `.pfm`+`.json` for 128-wide / 64×2 panels |
| `patterns/64x64/` | Absolute-ready pack for single 64×64 panels |
| `patterns/*` | Small flat demo subset for smoke tests |
| `shows/` | Demo `.pfs` tables at top level (also under `shows/demo_sequences/`) |
| `launchpads/` | `flowlocal_starter.json`, `simone_performative_deck.json` (Launch decks) |
| `director/` | Director PWA (also baked into LittleFS) |
| `docs/USB_STICK.txt` | Format / copy instructions |
| `docs/WIFI_PULL.txt` | How panels pull content over Wi-Fi |
| `patternflow-*.zip` | Optional download bundles |

A 32 GB or 64 GB USB-C stick is fine — the payload is well under 50 MB.
The ESP32-S3 USB host stack mounts **FAT32 only**, not exFAT or NTFS.
Windows will usually ship a 64 GB stick as exFAT, and Explorer will refuse
to format anything over 32 GB as FAT32. Reformat it once with a tool that
allows large FAT32 volumes (Rufus, or [fat32format](http://ridgecrop.co.uk/index.htm?guiformat.htm)),
32 KB allocation unit, volume label `FLOWLOCAL`. Then copy the payload.

---

## Pull content from FlowLocal → panel (Wi-Fi)

Both devices join `PatternflowLocal` / `patternflow`. FlowLocal is always
`192.168.66.1`.

### From Director (recommended)

1. Open http://192.168.66.1/director/
2. **Connect** → set **Panel HTTP base** to `http://patternflow.local`
   (or the panel’s LAN IP)
3. **File** tab:
   - **Install used patterns to panel** — panel pulls matching files from
     FlowLocal `patterns/`
   - **Send show to panel** — saves the current `.pfs` to `shows/`, then
     the panel pulls it into FFat `/shows`

### From the panel API

```sh
curl -X POST "http://patternflow.local/api/library/pull" \
  -d "host=192.168.66.1" \
  -d "kind=patterns" \
  -d "names=0515_4.pfm,0515_4.json"

curl -X POST "http://patternflow.local/api/library/pull" \
  -d "host=192.168.66.1" \
  -d "kind=shows" \
  -d "names=demo_ripple_grid.pfs"
```

Browse the stick library:

```text
GET http://192.168.66.1/api/library/patterns
GET http://192.168.66.1/api/library/shows
GET http://192.168.66.1/api/library/launchpads
```

Full notes also live on the stick at `docs/WIFI_PULL.txt`.

---

## AP configuration via web console

`POST /api/ap` with JSON body:

```json
{ "ssid": "PatternflowLocal-B", "pass": "patternflow", "enabled": true,
  "ip": "192.168.67.1", "gw": "192.168.67.1", "sn": "255.255.255.0" }
```

SSID, password, and AP IP persist in NVS. Changing the IP moves the dashboard,
MQTT broker address, and `/api/time` / `/api/weather` for that island.

### Multi-island

1. On each FlowLocal box: set a unique AP SSID and AP IP (dashboard → Access Point).
2. On each panel Wi‑Fi page: **add** that SSID/password to the known-network list
   and select it for boot when that island is in use.
3. On each panel MQTT page: choose **FlowLocal**, set **FlowLocal host** to that
   box’s AP IP (factory default `192.168.66.1`).

Wi‑Fi and MQTT stay separate: the Wi‑Fi list can hold home internet *and* several
show APs; FlowLocal mode only applies MQTT presets and island HTTP behaviours.

---

## Roadmap

- [x] USB OTG mass-storage host (`/usb` mount, hot-plug retry)
- [x] Serve Director PWA from USB flash (`/director/`)
- [x] USB file browser / download endpoint (`/files/`)
- [x] Content library API (`/api/library`) + panel Wi-Fi pull
- [x] Preloaded USB image (`patterns/` / `shows/` / starter deck)
- [x] NTP sync when upstream STA is available (`/api/time`, dashboard STA + TZ)
- [x] OTA web update (`/update`, arm then upload `.bin`)
- [x] Optional ST7735 status display + `/display` field toggles
- [x] Weather hub (`/weather`, STA pull, `GET /api/weather` for panels)
- [x] Runtime AP SSID + IP for multi-island venues
