# Patternflow Director (P4)

Framework-free static PWA for **local** performance authoring. Publishes the same MQTT topics as the panel firmware (`pattern`, `param/1..4`, `message`, retained `snapshot`).

## Quick start

Director uses the **bundled Python broker** (TCP 1883 for the panel, WebSocket 9001 for this page):

```powershell
cd D:\Documenti\Arduino\patternflow\director\tools
python -m pip install -r requirements.txt
python run_local_broker.py
```

Leave that window open. If port 1883 is busy, stop the other MQTT process first.

In a second terminal, serve the PWA:

```powershell
cd D:\Documenti\Arduino\patternflow\director
python -m http.server 8080
```

Open http://127.0.0.1:8080/

- **Setup** shows this PC’s LAN IP while the broker is running (copy it).
- **Connect** → `ws://127.0.0.1:9001`, prefix `patternflow`, empty user/password.
- On the panel `/mqtt`: **Director** mode → paste that IP → Connect to Director.

## Features

- Setup: Python broker status + live LAN IP
- Connect to `ws://127.0.0.1:9001`. Load patterns from a panel only when you ask (not on connect)
- Timeline: pattern, four absolute params `0..1000`, message; play / pause / seek / loop
- **PFST v2** by default: 0.1 s cue grid + per-cue **Ease** (linear lerp to the next param cue). v1 `.pfs` still open; File tab can upgrade / compress staircases
- Live sliders for immediate preview
- Open / Save a `.pfs` table (same file the panel plays on `/show`)
- Launch: 4×4 clip grid (Snapshots / K1–K4 / Message), BPM quantize, local deck JSON
- Example Launch deck: `examples/simone_performative_deck.json` (Load example on the Launch tab)
- Send timeline-used patterns to the panel (`/patterns` preselect + Download ZIP)
- `js/patterns-catalog.json` — local cache of official pattern knobs
- `demos/` — four 30-second `.pfs` tables (0515-4, 0531, Ripple Grid, 0601)
- `demos/all_patterns/` — one demo `.pfs` per shipped pattern

## Package for GitHub Release

From the repo root:

```bash
python tools/make_director_zip.py
# → patternflow-director-p4.zip
```

## Local-only policy

Director is for the **local Python broker**. Do not point it at the public Home Assistant broker for authoring.
