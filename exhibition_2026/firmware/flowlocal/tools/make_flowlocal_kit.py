#!/usr/bin/env python3
"""Assemble a complete FlowLocal kit zip for GitHub Releases.

Layout inside the zip:

  KIT_README.md
  firmware/flowlocal.bin
  firmware/flowlocal-littlefs.bin
  firmware/flowlocal-sd.bin          (if built)
  panels/patternflow-firmware.bin
  panels/patternflow-firmware64x2.bin
  usb-flash-image.zip                (FAT32 stick payload)

Requires ../patternflow (or PATTERNFLOW_ROOT) with dist/*.bin already built.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import zipfile
from datetime import date
from pathlib import Path

FLOWLOCAL = Path(__file__).resolve().parents[1]
ROOT_PF = Path(os.environ.get("PATTERNFLOW_ROOT", FLOWLOCAL.parent / "patternflow")).resolve()
DIST = FLOWLOCAL / "dist"
KIT_STAGING = DIST / "kit_staging"
OUT_NAME = f"flowlocal-kit-{date.today().isoformat()}.zip"


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=str(cwd))


def ensure_patternflow_bins() -> tuple[Path, Path]:
    a = ROOT_PF / "dist" / "firmware.bin"
    b = ROOT_PF / "dist" / "firmware64x2.bin"
    if not a.is_file() or not b.is_file():
        raise SystemExit(
            f"missing Patternflow OTA bins under {ROOT_PF / 'dist'}\n"
            "Build there first:\n"
            "  python -m platformio run -e firmware -e firmware64x2\n"
            "  copy .pio/build/.../firmware.bin into dist/"
        )
    return a, b


def build_flowlocal() -> tuple[Path, Path, Path | None]:
    # Always rebuild app so release bins match current sources (PIO can
    # otherwise leave a stale firmware.bin after FS-only builds).
    run(
        [sys.executable, "-m", "platformio", "run", "-e", "flowlocal", "-t", "clean"],
        FLOWLOCAL,
    )
    run(
        [sys.executable, "-m", "platformio", "run", "-e", "flowlocal", "-t", "buildfs"],
        FLOWLOCAL,
    )
    run(
        [sys.executable, "-m", "platformio", "run", "-e", "flowlocal"],
        FLOWLOCAL,
    )
    app = FLOWLOCAL / ".pio" / "build" / "flowlocal" / "firmware.bin"
    fs = FLOWLOCAL / ".pio" / "build" / "flowlocal" / "littlefs.bin"
    if not fs.is_file():
        # Older PlatformIO names
        alt = FLOWLOCAL / ".pio" / "build" / "flowlocal" / "spiffs.bin"
        fs = alt if alt.is_file() else fs
    if not app.is_file() or not fs.is_file():
        raise SystemExit(f"FlowLocal build missing bins (app={app.exists()} fs={fs.exists()})")

    sd: Path | None = None
    try:
        run(
            [sys.executable, "-m", "platformio", "run", "-e", "flowlocal-sd"],
            FLOWLOCAL,
        )
        cand = FLOWLOCAL / ".pio" / "build" / "flowlocal-sd" / "firmware.bin"
        if cand.is_file():
            sd = cand
    except subprocess.CalledProcessError as err:
        print(f"warning: flowlocal-sd build failed ({err}); kit continues without it")

    return app, fs, sd


def refresh_flash_image() -> Path:
    run([sys.executable, str(FLOWLOCAL / "tools" / "make_flash_image.py")], FLOWLOCAL)
    return FLOWLOCAL / "flash_image"


def zip_dir(src: Path, zip_path: Path, arc_prefix: str = "") -> None:
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(src.rglob("*")):
            if not path.is_file():
                continue
            if path.name in {".DS_Store", "Thumbs.db"}:
                continue
            rel = path.relative_to(src).as_posix()
            arc = f"{arc_prefix}/{rel}" if arc_prefix else rel
            zf.write(path, arcname=arc)


def main() -> int:
    pf_fw, pf_64 = ensure_patternflow_bins()
    app, fs, sd = build_flowlocal()
    flash = refresh_flash_image()

    if KIT_STAGING.exists():
        shutil.rmtree(KIT_STAGING)
    KIT_STAGING.mkdir(parents=True)

    fw_dir = KIT_STAGING / "firmware"
    panels = KIT_STAGING / "panels"
    fw_dir.mkdir()
    panels.mkdir()

    shutil.copy2(FLOWLOCAL / "KIT_README.md", KIT_STAGING / "KIT_README.md")
    build_md = FLOWLOCAL / "BUILD.md"
    if build_md.is_file():
        shutil.copy2(build_md, KIT_STAGING / "BUILD.md")
    schematic = FLOWLOCAL / "build_pics" / "Schematics.png"
    if schematic.is_file():
        pics_dir = KIT_STAGING / "build_pics"
        pics_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(schematic, pics_dir / "Schematics.png")
    stl_dir = FLOWLOCAL / "3D_STL"
    if stl_dir.is_dir():
        dest_stl = KIT_STAGING / "3D_STL"
        dest_stl.mkdir(parents=True, exist_ok=True)
        for stl in sorted(stl_dir.glob("*.stl")):
            shutil.copy2(stl, dest_stl / stl.name)
    shutil.copy2(app, fw_dir / "flowlocal.bin")
    shutil.copy2(fs, fw_dir / "flowlocal-littlefs.bin")
    if sd is not None:
        shutil.copy2(sd, fw_dir / "flowlocal-sd.bin")
    shutil.copy2(pf_fw, panels / "patternflow-firmware.bin")
    shutil.copy2(pf_64, panels / "patternflow-firmware64x2.bin")

    image_zip = KIT_STAGING / "usb-flash-image.zip"
    zip_dir(flash, image_zip)

    DIST.mkdir(parents=True, exist_ok=True)
    out = DIST / OUT_NAME
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(KIT_STAGING.rglob("*")):
            if not path.is_file():
                continue
            zf.write(path, arcname=path.relative_to(KIT_STAGING).as_posix())

    # Keep unpacked copies for attach-as-separate-assets if desired.
    shutil.copy2(app, DIST / "flowlocal.bin")
    shutil.copy2(fs, DIST / "flowlocal-littlefs.bin")
    if sd is not None:
        shutil.copy2(sd, DIST / "flowlocal-sd.bin")
    shutil.copy2(pf_fw, DIST / "patternflow-firmware.bin")
    shutil.copy2(pf_64, DIST / "patternflow-firmware64x2.bin")
    shutil.copy2(image_zip, DIST / "usb-flash-image.zip")
    shutil.copy2(FLOWLOCAL / "KIT_README.md", DIST / "KIT_README.md")
    if build_md.is_file():
        shutil.copy2(build_md, DIST / "BUILD.md")

    print(f"wrote {out} ({out.stat().st_size} bytes)")
    print(f"staging {KIT_STAGING}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
