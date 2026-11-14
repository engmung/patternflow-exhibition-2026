#!/usr/bin/env python3
"""Run the Ensemble host tests. No board, no Patternflow checkout needed.

  python ensemble/tests/run_tests.py
  python ensemble/tests/run_tests.py --patternflow ../patternflow

What runs:
  core      tests/test_core.cpp: the protocol, the panel engine and the hub,
            through whole simulated installations (lossy air, power cuts)
  panel     tests/test_panel_glue.cpp: the panel feature's device side,
            compiled against stand-ins for the Patternflow headers
  tool      tests/test_panels_tool.py: tools/panels.py against stand-in
            panels (what it refuses to send, what it skips, when it stops)
  console   the page the hub serves (hub/src/console_page.h) is up to date
  --patternflow <checkout>
            the stand-in for features/pf_feature.h still lists the hooks in
            the real header's order (a descriptor is positional, so a hook
            added upstream in the middle would wire every later one wrong)

Needs a C++17 compiler: g++ or clang++ on the PATH, or  python -m pip install ziglang
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAGS = ["-std=c++17", "-O1", "-Wall", "-Wextra"]


def compiler():
    for name in ("g++", "clang++", "c++"):
        path = shutil.which(name)
        if path:
            # The sanitizers catch what a passing test hides; not every
            # Windows toolchain ships their runtime.
            extra = [] if sys.platform.startswith("win") else ["-fsanitize=address,undefined", "-g"]
            return [path, *extra]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "c++"]
    except ImportError:
        return None


def build_and_run(cc, tmp, name, source, includes):
    exe = Path(tmp) / (name + (".exe" if sys.platform.startswith("win") else ""))
    cmd = [*cc, *FLAGS, *[f"-I{i}" for i in includes], "-o", str(exe), str(source)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode != 0:
        print(f"{name}: did not compile\n{built.stderr}")
        return False
    if built.stderr.strip():
        print(built.stderr.strip())
    ran = subprocess.run([str(exe)], capture_output=True, text=True)
    out = (ran.stdout + ran.stderr).strip()
    print(f"{name}: {out.splitlines()[-1] if out else 'no output'}")
    if ran.returncode != 0 and out:
        print("\n".join("  " + line for line in out.splitlines()[:-1]))
    return ran.returncode == 0


def panel_tree(tmp):
    """The stand-in Patternflow tree with the feature installed into it."""
    tree = Path(tmp) / "patternflow"
    shutil.copytree(ROOT / "tests" / "panel_stub", tree)
    dest = tree / "features" / "ensemble"
    shutil.copytree(ROOT / "panel" / "features" / "ensemble", dest)
    # The panel gets three of the core's headers (panel/install.py); the
    # test also wants the hub on the other end of the wire.
    for header in (ROOT / "core").glob("pfe_*.h"):
        shutil.copy(header, dest / header.name)
    return tree


def descriptor_fields(text):
    body = text[text.index("struct PFFeature {") + len("struct PFFeature {"):]
    body = body[:body.index("\n};")]
    body = re.sub(r"//[^\n]*", "", body)
    names = []
    for statement in body.split(";"):
        m = re.search(r"\(\*\s*(\w+)\s*\)", statement) or re.search(r"(\w+)\s*$", statement.strip())
        if m and statement.strip():
            names.append(m.group(1))
    return names


def check_descriptor(checkout):
    real = Path(checkout) / "firmware" / "patternflow" / "features" / "pf_feature.h"
    if not real.exists():
        print(f"descriptor: no {real}")
        return False
    want = descriptor_fields(real.read_text(encoding="utf-8"))
    have = descriptor_fields((ROOT / "tests" / "panel_stub" / "features" / "pf_feature.h").read_text(encoding="utf-8"))
    if want == have:
        print(f"descriptor: PASS  the {len(have)} hooks are in the checkout's order")
        return True
    print("descriptor: FAIL  features/pf_feature.h in the checkout no longer matches the stand-in")
    print(f"  checkout: {want}\n  stand-in: {have}")
    print("  Bring tests/panel_stub/features/pf_feature.h and panel/features/ensemble/feature_ensemble.h along.")
    return False


def main():
    args = sys.argv[1:]
    checkout = None
    if args[:1] == ["--patternflow"] and len(args) == 2:
        checkout = args[1]
    elif args:
        print(__doc__)
        return 2
    cc = compiler()
    if not cc:
        print("No C++ compiler found. Install g++ or clang++, or: python -m pip install ziglang")
        return 1
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        ok &= build_and_run(cc, tmp, "core", ROOT / "tests" / "test_core.cpp", [])
        tree = panel_tree(tmp)
        ok &= build_and_run(cc, tmp, "panel", ROOT / "tests" / "test_panel_glue.cpp", [tree])
    tool = subprocess.run([sys.executable, str(ROOT / "tests" / "test_panels_tool.py")], capture_output=True, text=True)
    out = (tool.stdout + tool.stderr).strip()
    print("tool: " + (out.splitlines()[-1] if out else "no output"))
    if tool.returncode != 0 and out:
        print("\n".join("  " + line for line in out.splitlines()[:-1]))
    ok &= tool.returncode == 0
    console = subprocess.run([sys.executable, str(ROOT / "tools" / "build_console.py"), "--check"],
                             capture_output=True, text=True)
    print("console: " + (console.stdout + console.stderr).strip())
    ok &= console.returncode == 0
    if checkout:
        ok &= check_descriptor(checkout)
    print("ALL PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
