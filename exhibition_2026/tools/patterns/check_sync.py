#!/usr/bin/env python3
"""Has any pattern's JS changed since its .h was last made from it?

Each folder in patterns/ holds a pattern's JS (what Pattern Lab and the
simulator run) and its .h (what the panel runs, translated from that JS).
patterns/sync.json remembers the fingerprints of both at the moment they were
last proven to draw the same picture (tools/patterns/host/pfhost.py current).
This compares the files on disk with those fingerprints - no compiler, under
a second - so a JS that was changed and never translated cannot slip into a
firmware build unnoticed.

    python tools/patterns/check_sync.py            # report; exit 1 if anything is out of step
    python tools/patterns/check_sync.py --accept bakwan[,yujin|all]
        # record the current files as in step. Only after
        #   python tools/patterns/host/pfhost.py current bakwan
        # says OK (same picture).

Fingerprints ignore CR/LF differences, so a checkout on another computer
(git turns LF into CRLF on Windows for some files) does not count as a change.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATTERNS = ROOT / "patterns"
STATE = PATTERNS / "sync.json"


def fingerprint(path: Path) -> str | None:
    if not path.exists():
        return None
    data = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def folders() -> list[Path]:
    return sorted(p for p in PATTERNS.iterdir() if p.is_dir() and p.name[:1].isdigit())


def sources(folder: Path) -> dict[str, Path]:
    """The files a pattern's .h is made from, plus the .h itself."""
    key = folder.name.split("_", 1)[1]
    files = {"js": folder / f"{key}.js", "h": folder / f"{key}.h"}
    project = folder / f"{key}.project.json"
    if project.exists():  # a layered composition: the project is its source
        files["project"] = project
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--accept", help="comma-separated keys (bakwan,...) or 'all'")
    a = ap.parse_args()

    state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {}

    if a.accept:
        wanted = None if a.accept == "all" else set(a.accept.split(","))
        for folder in folders():
            key = folder.name.split("_", 1)[1]
            if wanted is not None and key not in wanted:
                continue
            state[folder.name] = {kind: fingerprint(p) for kind, p in sources(folder).items()}
            print(f"recorded {folder.name}")
        STATE.write_text(json.dumps(state, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        return 0

    bad = 0
    for folder in folders():
        files = sources(folder)
        known = state.get(folder.name, {})
        changed = [kind for kind, p in files.items() if fingerprint(p) != known.get(kind)]
        missing = [kind for kind, p in files.items() if not p.exists()]
        if missing:
            verdict = f"MISSING {', '.join(missing)}"
        elif not known:
            verdict = "never recorded - run pfhost current, then --accept"
        elif not changed:
            verdict = "in step"
        elif "h" not in changed:
            verdict = "JS changed, .h not redone yet - translate it again"
        elif changed == ["h"]:
            verdict = ".h changed - prove it with pfhost current, then --accept"
        else:
            verdict = "JS and .h both changed - prove with pfhost current, then --accept"
        if verdict != "in step":
            bad += 1
        print(f"  {folder.name:18s} {verdict}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
