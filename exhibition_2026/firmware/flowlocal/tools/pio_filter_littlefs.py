# PlatformIO pre-script: pack LittleFS from data/ without PC-only Director extras.
Import("env")

from pathlib import Path
import shutil

SKIP_DIR_PARTS = {"tools", "mosquitto", "__pycache__"}
SKIP_NAMES = {".DS_Store", "Thumbs.db", "pfst-v2-smoke.js"}

src = Path(env["PROJECT_DIR"]) / "data"
dst = Path(env["PROJECT_DIR"]) / ".pio" / "littlefs_data"

if dst.exists():
    shutil.rmtree(dst)
dst.mkdir(parents=True)

for path in src.rglob("*"):
    if not path.is_file():
        continue
    rel = path.relative_to(src)
    if any(part in SKIP_DIR_PARTS for part in rel.parts):
        continue
    if path.name in SKIP_NAMES or path.suffix == ".pyc":
        continue
    out = dst / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, out)

env["PROJECT_DATA_DIR"] = str(dst)
print(f"[littlefs] data dir → {dst}")
