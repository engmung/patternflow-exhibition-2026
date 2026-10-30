#!/usr/bin/env python3
"""Assemble the default USB-flash payload for FlowLocal.

Copy the *contents* of flash_image/ to a FAT32 USB-C stick (or MicroSD).
The stick is the canonical library FlowLocal serves over Wi-Fi:

  patterns/     .pfm + .json modules (preloaded demos)
  shows/        .pfs show tables (preloaded demos)
  launchpads/   Director deck JSON (starter deck)
  director/     optional USB copy of the PWA (LittleFS also has one)
  docs/         stick + Wi-Fi pull instructions

Director on this image uses broker 192.168.66.1 (not the PC Python broker).
"""
from __future__ import annotations

import json
import os
import shutil
import zipfile
from pathlib import Path

FLOWLOCAL = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("PATTERNFLOW_ROOT", FLOWLOCAL.parent / "patternflow")).resolve()
OUT = FLOWLOCAL / "flash_image"

# Prefer the FlowLocal-baked Director (already flowlocal-aware). Fall back to
# Patternflow's director/ when rebuilding from a fresh clone.
DIRECTOR_SRC_CANDIDATES = (
    FLOWLOCAL / "data" / "director",
    ROOT / "director",
)
DEMOS_SRC_CANDIDATES = (
    FLOWLOCAL / "data" / "director" / "demos",
    ROOT / "director" / "demos",
)

RELEASE_FILES = (
    "patternflow-director-p5.zip",
    "patternflow-director-p4.zip",
    "patternflow-demo-sequences.zip",
    "patternflow-demo-patterns.zip",
)

SKIP_NAMES = {".DS_Store", "Thumbs.db", "pfst-v2-smoke.js"}
SKIP_DIR_PARTS = {"tools", "mosquitto"}


def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def first_existing(paths: tuple[Path, ...]) -> Path:
    for p in paths:
        if p.is_dir():
            return p
    raise SystemExit(
        "missing director/demos sources; expected one of:\n  "
        + "\n  ".join(str(p) for p in paths)
    )


def copy_tree(src: Path, dst: Path) -> int:
    count = 0
    for path in sorted(src.rglob("*")):
        if not path.is_file():
            continue
        if path.name in SKIP_NAMES:
            continue
        rel = path.relative_to(src)
        if any(part in SKIP_DIR_PARTS for part in rel.parts):
            continue
        out = dst / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)
        count += 1
    return count


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def find_release(name: str) -> Path | None:
    for base in (ROOT, FLOWLOCAL / "flash_image", FLOWLOCAL):
        cand = base / name
        if cand.is_file():
            return cand
    return None


def extract_patterns(zip_path: Path, dest: Path) -> int:
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    with zipfile.ZipFile(zip_path) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            name = Path(info.filename).name
            if name.startswith(".") or name in SKIP_NAMES:
                continue
            lower = name.lower()
            if not (lower.endswith(".pfm") or lower.endswith(".json")):
                continue
            target = dest / name
            with zf.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out)
            count += 1
    return count


def write_starter_deck(path: Path) -> None:
    """Minimal Launch deck that references the four shipped demo patterns."""
    deck = {
        "version": 1,
        "id": "flowlocal_starter",
        "title": "FlowLocal Starter",
        "bpm": 120,
        "quantize": "off",
        "layers": {
            "snapshot": [
                {
                    "kind": "snapshot",
                    "pattern": "0515-4",
                    "param": [500, 500, 500, 500],
                    "message": "",
                    "label": "0515-4",
                },
                {
                    "kind": "snapshot",
                    "pattern": "0531",
                    "param": [500, 500, 500, 500],
                    "message": "",
                    "label": "0531",
                },
                {
                    "kind": "snapshot",
                    "pattern": "0601",
                    "param": [500, 500, 500, 500],
                    "message": "",
                    "label": "0601",
                },
                {
                    "kind": "snapshot",
                    "pattern": "Ripple Grid",
                    "param": [500, 500, 500, 500],
                    "message": "",
                    "label": "Ripple Grid",
                },
            ]
            + [{"kind": "empty"} for _ in range(12)],
            "param0": [{"kind": "empty"} for _ in range(16)],
            "param1": [{"kind": "empty"} for _ in range(16)],
            "param2": [{"kind": "empty"} for _ in range(16)],
            "param3": [{"kind": "empty"} for _ in range(16)],
            "message": [{"kind": "empty"} for _ in range(16)],
        },
    }
    write_text(path, json.dumps(deck, indent=2) + "\n")


def main() -> int:
    director_src = first_existing(DIRECTOR_SRC_CANDIDATES)
    demos_src = first_existing(DEMOS_SRC_CANDIDATES)

    # Preserve release zips if they already live under flash_image/ before wipe.
    cached_zips: dict[str, Path] = {}
    for name in RELEASE_FILES:
        existing = find_release(name)
        if existing and existing.parent == OUT:
            tmp = FLOWLOCAL / ".flash_image_cache" / name
            tmp.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(existing, tmp)
            cached_zips[name] = tmp

    reset_dir(OUT)

    director_count = copy_tree(director_src, OUT / "director")
    write_text(
        OUT / "director" / "js" / "runtime.js",
        "\n".join(
            [
                "/* FlowLocal appliance — not the PC Python broker. */",
                "window.PF_RUNTIME = {",
                "  flowlocal: true,",
                '  wsUrl: "ws://192.168.66.1:9001"',
                "};",
                "",
            ]
        ),
    )
    write_text(
        OUT / "director" / "OPEN.txt",
        "\n".join(
            [
                "Do not double-click index.html on the PC.",
                "Join Wi-Fi PatternflowLocal, then open:",
                "  http://192.168.66.1/director/",
                "Connect is already set to ws://192.168.66.1:9001",
                "",
            ]
        ),
    )

    # Library layout FlowLocal /api/library expects.
    # Absolute-ready Director packs live under patterns/128x64 and patterns/64x64.
    shows_out = OUT / "shows"
    patterns_out = OUT / "patterns"
    decks_out = OUT / "launchpads"
    shows_out.mkdir(parents=True, exist_ok=True)
    patterns_out.mkdir(parents=True, exist_ok=True)
    decks_out.mkdir(parents=True, exist_ok=True)

    show_count = 0
    for pfs in sorted(demos_src.glob("*.pfs")):
        shutil.copy2(pfs, shows_out / pfs.name)
        show_count += 1
    copy_tree(demos_src, shows_out / "demo_sequences")

    pattern_count = 0
    for geom in ("128x64", "64x64"):
        src = ROOT / "data" / geom / "patterns"
        if not src.is_dir():
            print(f"warning: missing {src}")
            continue
        dest = patterns_out / geom
        dest.mkdir(parents=True, exist_ok=True)
        for path in sorted(src.iterdir()):
            if not path.is_file():
                continue
            if path.suffix.lower() not in (".pfm", ".json"):
                continue
            if path.name in SKIP_NAMES:
                continue
            shutil.copy2(path, dest / path.name)
            pattern_count += 1

    # Keep a tiny flat demo set at patterns/ root for quick smoke tests.
    demos_flat = ("0515_4", "0531", "0601", "ripple_grid")
    for stem in demos_flat:
        for ext in (".pfm", ".json"):
            src = patterns_out / "128x64" / (stem + ext)
            if src.is_file():
                shutil.copy2(src, patterns_out / (stem + ext))

    write_starter_deck(decks_out / "flowlocal_starter.json")

    # Ready-to-perform Launch deck (also under director/examples for Load example).
    for deck_src in (
        ROOT / "director" / "examples" / "simone_performative_deck.json",
        FLOWLOCAL / "data" / "director" / "examples" / "simone_performative_deck.json",
        FLOWLOCAL / "flash_image" / "launchpads" / "simone_performative_deck.json",
    ):
        if deck_src.is_file():
            shutil.copy2(deck_src, decks_out / "simone_performative_deck.json")
            break

    # One demo .pfs per pattern (panel Sequences playlist demos).
    all_pat = ROOT / "director" / "demos" / "all_patterns"
    if all_pat.is_dir():
        dest = shows_out / "all_pattern_demos"
        dest.mkdir(parents=True, exist_ok=True)
        for pfs in sorted(all_pat.glob("*.pfs")):
            shutil.copy2(pfs, dest / pfs.name)
            show_count += 1

    for name in RELEASE_FILES:
        src = find_release(name) or cached_zips.get(name)
        if src is None or not src.is_file():
            print(f"warning: missing release zip {name}")
            continue
        shutil.copy2(src, OUT / name)

    cache = FLOWLOCAL / ".flash_image_cache"
    if cache.is_dir():
        shutil.rmtree(cache)

    docs = OUT / "docs"
    write_text(
        docs / "README.txt",
        "\n".join(
            [
                "FlowLocal USB flash payload",
                "",
                "Join Wi-Fi PatternflowLocal / patternflow",
                "Director:  http://192.168.66.1/director/",
                "Console:   http://192.168.66.1/",
                "",
                "Library folders (served by /api/library):",
                "  patterns/128x64/  absolute-ready modules for 128-wide panels",
                "  patterns/64x64/   absolute-ready modules for 64x64 panels",
                "  patterns/*.pfm    small flat demo subset (smoke tests)",
                "  shows/            .pfs demo shows",
                "  launchpads/       starter Launch deck",
                "",
                "See WIFI_PULL.txt for installing content onto a panel over Wi-Fi.",
                "See USB_STICK.txt for formatting and copying this image.",
                "",
            ]
        ),
    )
    write_text(
        docs / "USB_STICK.txt",
        "\n".join(
            [
                "Preparing the FlowLocal USB-C (or MicroSD) image",
                "================================================",
                "",
                "1. Format the stick as FAT32 only (not exFAT / NTFS).",
                "   Label: FLOWLOCAL. 32 KB allocation unit is fine.",
                "   Large sticks: use Rufus or guiformat (fat32format).",
                "",
                "2. Copy EVERYTHING inside flash_image/ to the ROOT of the stick",
                "   so you see folders named director/, patterns/, shows/, …",
                "   Do not nest them under another folder.",
                "",
                "3. Hardware",
                "   - USB-C stick: jack labelled USB (not COM).",
                "     Close the USB-OTG jumper; power from COM.",
                "   - MicroSD: CS=4 SCK=5 MOSI=6 MISO=7, build -e flowlocal-sd.",
                "",
                "4. Flash FlowLocal firmware over COM, then reboot with the",
                "   stick inserted. Serial should show media mounted.",
                "",
                "5. Join PatternflowLocal / patternflow and open",
                "   http://192.168.66.1/director/",
                "",
                "Rebuild this folder anytime:",
                "  python tools/make_flash_image.py",
                "",
            ]
        ),
    )
    write_text(
        docs / "WIFI_PULL.txt",
        "\n".join(
            [
                "Pull files from FlowLocal to a Patternflow panel (Wi-Fi)",
                "=======================================================",
                "",
                "Both devices must be on PatternflowLocal (pass: patternflow).",
                "FlowLocal is always 192.168.66.1. The panel keeps its own IP",
                "(often patternflow.local).",
                "",
                "A) From FlowLocal Director (easiest)",
                "------------------------------------",
                "1. Tablet/PC joins PatternflowLocal.",
                "2. Open http://192.168.66.1/director/",
                "3. Connect tab:",
                "     WebSocket  ws://192.168.66.1:9001",
                "     Panel HTTP base  http://patternflow.local",
                "       (or http://<panel-lan-ip> if mDNS fails)",
                "4. File tab:",
                "   - Install used patterns to panel",
                "       → panel POSTs /api/library/pull for matching .pfm/.json",
                "         from FlowLocal patterns/128x64/ (preferred) or 64x64/",
                "   - Send show to panel",
                "       → saves current .pfs to FlowLocal shows/, then panel pulls it",
                "",
                "Patterns live under patterns/128x64/ and patterns/64x64/",
                "(absolute-ready for Director MQTT 0..1000). Shows under shows/.",
                "",
                "B) From the panel (curl / script)",
                "---------------------------------",
                "On any machine that can reach the panel:",
                "",
                "  curl -X POST \"http://patternflow.local/api/library/pull\" \\",
                "    -d \"host=192.168.66.1\" \\",
                "    -d \"kind=patterns\" \\",
                "    -d \"names=128x64/0515_4.pfm,128x64/0515_4.json\"",
                "",
                "  curl -X POST \"http://patternflow.local/api/library/pull\" \\",
                "    -d \"host=192.168.66.1\" \\",
                "    -d \"kind=shows\" \\",
                "    -d \"names=demo_ripple_grid.pfs\"",
                "",
                "The panel downloads from FlowLocal, validates, and stores on FFat",
                "(/patterns or /shows) using the file basename. Patterns trigger a",
                "deferred module reload.",
                "",
                "C) Browse the library on FlowLocal",
                "----------------------------------",
                "  GET http://192.168.66.1/api/library",
                "  GET http://192.168.66.1/api/library/patterns",
                "  GET http://192.168.66.1/api/library/shows",
                "  GET http://192.168.66.1/api/library/launchpads",
                "  GET http://192.168.66.1/api/library/patterns/file?name=128x64/0531.pfm",
                "",
                "Panel firmware must include POST /api/library/pull",
                "(Patternflow with FlowLocal sync / performance-director tree).",
                "",
            ]
        ),
    )

    manifest = {
        "flowlocal_payload_version": 3,
        "director_files": director_count,
        "show_files": show_count,
        "pattern_files": pattern_count,
        "pattern_geometries": ["128x64", "64x64"],
        "launchpads": ["flowlocal_starter.json"],
        "release_files": [n for n in RELEASE_FILES if (OUT / n).is_file()],
        "docs": ["README.txt", "USB_STICK.txt", "WIFI_PULL.txt"],
    }
    write_text(OUT / "manifest.json", json.dumps(manifest, indent=2) + "\n")

    print(f"wrote {OUT}")
    print(f"  director={director_count} shows={show_count} patterns={pattern_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
