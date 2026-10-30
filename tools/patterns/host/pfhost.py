"""Host parity + timing harness for the exhibit's pattern headers (no hardware).

Proves that a translated pattern header (patterns/<n>_<key>/<key>.h) draws
what the JS beside it draws, and times it. See README.md.

    python tools/patterns/host/pfhost.py current [bakwan,yujin,...]  (each folder's .h vs its .js)
    python tools/patterns/host/pfhost.py run --header H.h --ns NS --js P.js [--standard | --script "..."]
    python tools/patterns/host/pfhost.py validate-old            (six old headers vs patterns/_original/js)
    python tools/patterns/host/pfhost.py expand --script "..."   (print the expanded per-frame script)

One run = one script, played twice:
  native  the header compiled at -O2 against the REAL portable core headers
          (core_math, core_color, core_noise, core_tables, core_mem,
          core_params/abi/pf_params, core_canvas's frame mapping) with the
          hardware stubbed (stubs/), fed exactly the frame the exhibit build
          feeds it (K1-K3 as lanes -> PFParams::apply = lo + (hi-lo)*pos).
  JS      the pattern in the web's PatternRuntime (dump.ts), knobValues =
          lo + (hi-lo)*pos from the @knobs line, the same presses.
then compared frame by frame (MAD, % pixels off by > 24, the same allowing a
1-pixel shift) and written as a JS | C++ | diff x4 strip under out/.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
EXH = HERE.parents[2]                                   # exhibition_2026/
REPO = EXH / "patternflow"                              # the firmware clone
SKETCH = REPO / "firmware" / "patternflow"
EXHIBIT_PATTERNS = SKETCH / "features" / "exhibit" / "patterns"
WEB = Path(os.environ.get("PF_WEB", EXH.parent / "랜딩페이지_opensorce" / "web"))
OUT = HERE / "out"
sys.path.insert(0, str(REPO / "firmware" / "toolchain"))
from check_module_elf import compiler_environment as _compiler_environment  # noqa: E402

_COMPILER: tuple[str, dict] | None = None


def compiler_environment() -> tuple[str, dict]:
    """check_module_elf's lookup, once per process, with vcvars' chatter muted."""
    global _COMPILER
    if _COMPILER is None:
        sys.stderr.flush()
        saved = os.dup(2)
        try:
            with open(os.devnull, "w") as null:
                os.dup2(null.fileno(), 2)
                _COMPILER = _compiler_environment()
        finally:
            os.dup2(saved, 2)
            os.close(saved)
    return _COMPILER

# The commit that put the six current (LLM-translated) headers into the
# clone. validate-old reads them from here, so the baseline stays the same
# while the files on disk are being replaced.
OLD_REV = "013fd9b"

# name -> (header file, namespace, original JS)
SIX = [
    ("bakwan", "BAKWAN", "bakwan.js"),
    ("choegyeongeun", "CHOEGYEONGEUN", "choegyeongeun.js"),
    ("hanyeonghoe", "LabStack_HANYEONGHOE", "hanyeonghoe.js"),
    ("heoeunbin", "HEOEUNBIN", "heoeunbin.js"),
    ("jeongeungyo", "JEONGEUNGYO", "jeongeungyo.js"),
    ("yujin", "YUJIN", "yujin.js"),
]

CORE_HEADERS = ("core_math.h", "core_color.h", "core_noise.h", "core_tables.h",
                "core_mem.h", "core_params.h", "core_canvas.h")

# ── Scripts ──────────────────────────────────────────────────────────────
# Clauses separated by "|", frames 1-based, 30 frames = 1 s:
#   240f                        run 240 frames (default: the last frame named)
#   0.5,0.5,0.5                 K1-K3 positions (0..1) from frame 1
#   1,0.5,0.5@90                jump to these positions at frame 90
#   0,0,0->1,1,1@30..120        linear move over frames 30..120
#   k2:0.2@60  k2:0->1@30..90   the same for one knob
#   press1@60,90                K1 press (btnPressed + btnHeld) on frames 60, 90
#   hold2@60..90                K2 down at 60 (btnPressed), held to 90
#   capture@1,30,60  capture@every30  capture@last
#   k4=0.8                      the JS K4 value (default: what the C++ keeps)
#   raw                         do not quantise positions to detents
# Positions are quantised to the exhibit's 32 detents (a hand can only land
# there) unless "raw" is given.
STANDARD = {
    "centre": "240f | 0.5,0.5,0.5 | capture@1,15,30,60,120,180,240",
    "min": "240f | 0,0,0 | capture@1,30,120,240",
    "max": "240f | 1,1,1 | capture@1,30,120,240",
    "sweep": ("270f | 0,0,0 | k1:0->1@1..90 | k2:0->1@91..180 | k3:0->1@181..270"
              " | capture@30,60,90,120,150,180,210,240,270"),
    "press": ("300f | 0.5,0.5,0.5 | press1@60 | press2@120 | press3@180"
              " | capture@59,60,61,75,120,121,150,180,181,210,300"),
}
BENCH_EXTRA = 300   # extra frames at the centre script's end for ms/frame


def knob_detents() -> int:
    cfg = (SKETCH / "features" / "exhibit" / "exhibit_config.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+PF_EXHIBIT_KNOB_DETENTS\s+(\d+)", cfg)
    return int(m.group(1)) if m else 32


def parse_script(text: str) -> dict:
    detents = knob_detents()
    clauses = [c.strip() for c in text.split("|") if c.strip()]
    n = None
    start = [0.5, 0.5, 0.5]
    events = []          # (frame_from, frame_to, knob, v_from, v_to)
    press: dict[int, int] = {}
    held: dict[int, int] = {}
    captures: set[int] = set()
    capture_every = None
    capture_last = False
    k4 = None
    raw = False
    last_named = 1

    def rng(s: str) -> tuple[int, int]:
        if ".." in s:
            a, b = s.split("..")
            return int(a), int(b)
        return int(s), int(s)

    for c in clauses:
        if m := re.fullmatch(r"(\d+)f", c):
            n = int(m.group(1))
        elif c == "raw":
            raw = True
        elif m := re.fullmatch(r"k4=([-\d.eE+]+)", c):
            k4 = float(m.group(1))
        elif m := re.fullmatch(r"capture@(.+)", c):
            spec = m.group(1)
            if spec == "last":
                capture_last = True
            elif spec.startswith("every"):
                capture_every = int(spec[5:])
            else:
                for part in spec.split(","):
                    a, b = rng(part)
                    captures.update(range(a, b + 1))
                    last_named = max(last_named, b)
        elif m := re.fullmatch(r"press([123])@(.+)", c):
            k = int(m.group(1)) - 1
            for part in m.group(2).split(","):
                f = int(part)
                press[f] = press.get(f, 0) | (1 << k)
                held[f] = held.get(f, 0) | (1 << k)
                last_named = max(last_named, f)
        elif m := re.fullmatch(r"hold([123])@(\d+)\.\.(\d+)", c):
            k = int(m.group(1)) - 1
            a, b = int(m.group(2)), int(m.group(3))
            press[a] = press.get(a, 0) | (1 << k)
            for f in range(a, b + 1):
                held[f] = held.get(f, 0) | (1 << k)
            last_named = max(last_named, b)
        elif m := re.fullmatch(r"k([123]):([-\d.]+)(?:->([-\d.]+))?(?:@(\d+(?:\.\.\d+)?))?", c):
            k = int(m.group(1)) - 1
            v0 = float(m.group(2))
            v1 = float(m.group(3)) if m.group(3) else v0
            a, b = rng(m.group(4)) if m.group(4) else (1, 1)
            events.append((a, b, k, v0, v1))
            last_named = max(last_named, b)
        elif m := re.fullmatch(r"([-\d.,]+)(?:->([-\d.,]+))?(?:@(\d+(?:\.\.\d+)?))?", c):
            v0 = [float(x) for x in m.group(1).split(",")]
            v1 = [float(x) for x in m.group(2).split(",")] if m.group(2) else v0
            if len(v0) != 3 or len(v1) != 3:
                raise SystemExit(f"script: '{c}' needs three positions K1,K2,K3")
            if m.group(3) is None and m.group(2) is None:
                start = v0
                continue
            a, b = rng(m.group(3)) if m.group(3) else (1, 1)
            for k in range(3):
                events.append((a, b, k, v0[k], v1[k]))
            last_named = max(last_named, b)
        else:
            raise SystemExit(f"script: cannot read clause '{c}'")
    if n is None:
        n = last_named
    pos = list(start)
    steps = []
    for f in range(1, n + 1):
        for a, b, k, v0, v1 in events:
            if a <= f <= b:
                pos[k] = v0 + (v1 - v0) * ((f - a) / (b - a) if b > a else 1.0)
        q = [min(1.0, max(0.0, p)) for p in pos]
        if not raw:
            q = [round(p * detents) / detents for p in q]
        cap = f in captures or (capture_every and f % capture_every == 0) or (capture_last and f == n)
        steps.append((q[0], q[1], q[2], press.get(f, 0), held.get(f, 0), 1 if cap else 0))
    if not any(s[5] for s in steps):
        steps[-1] = steps[-1][:5] + (1,)
    return {"n": n, "steps": steps, "k4": k4, "text": text}


def expanded(script: dict) -> str:
    lines = [f"frames {script['n']}"]
    lines += [f"{a:.6f} {b:.6f} {c:.6f} {p} {h} {cap}" for a, b, c, p, h, cap in script["steps"]]
    return "\n".join(lines) + "\n"


def captured_frames(script: dict) -> list[int]:
    return [i + 1 for i, s in enumerate(script["steps"]) if s[5]]


# ── Native build ─────────────────────────────────────────────────────────
def cut_struct(text: str, name: str) -> str:
    m = re.search(r"^struct " + name + r" \{.*?^\};", text, re.S | re.M)
    if not m:
        raise SystemExit(f"struct {name} not found in core_encoders.h")
    return m.group(0)


def lane_scale() -> str:
    net = (SKETCH / "net_config.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+PF_LANE_MOTION_SCALE\s+([\d.]+f?)", net)
    return m.group(1) if m else "48.0f"


def k4_variable(header: str, ns: str) -> str | None:
    body = strip_comments(header)
    m = re.search(r"PFParams::apply\w*\(\s*input\s*,\s*3\s*,\s*&\s*([\w.\[\]]+)", body)
    if m:
        return f"{ns}::{m.group(1)}"
    m = re.search(r"PFParams::apply\w*\(\s*input\s*,\s*(\w+)\s*,\s*&\s*(\w+)\[\s*\1\s*\]", body)
    if m:
        return f"{ns}::{m.group(2)}[3]"
    return None


def build_native(header: bytes, ns: str) -> tuple[Path, Path]:
    """Compile runner.cpp + the header; cached by content. Returns (exe, dir)."""
    enc = (SKETCH / "src" / "core_encoders.h").read_text(encoding="utf-8")
    encoders = "#pragma once\n#include <Arduino.h>\n" + cut_struct(enc, "InputFrame") + "\n"
    k4 = k4_variable(header.decode("utf-8", "replace"), ns)
    cfg = (f"#pragma once\n#define PF_NS {ns}\n#define PF_LANE_SCALE {lane_scale()}\n"
           + (f"#define PF_K4_VAR {k4}\n" if k4 else ""))
    files: dict[str, bytes] = {
        "pattern.h": header,
        "runner_config.h": cfg.encode(),
        "runner.cpp": (HERE / "runner.cpp").read_bytes(),
        "src/core_encoders.h": encoders.encode(),
        "abi/pf_params.h": (SKETCH / "abi" / "pf_params.h").read_bytes(),
    }
    for name in CORE_HEADERS:
        files[f"src/{name}"] = (SKETCH / "src" / name).read_bytes()
    for p in (HERE / "stubs").rglob("*.h"):
        files[p.relative_to(HERE / "stubs").as_posix()] = p.read_bytes()
    compiler, env = compiler_environment()
    h = hashlib.sha1(compiler.encode())
    for k in sorted(files):
        h.update(k.encode() + b"\0" + files[k] + b"\0")
    w = Path(tempfile.gettempdir()) / "pf-host-build" / f"{ns}-{h.hexdigest()[:12]}"
    exe = w / ("runner.exe" if os.name == "nt" else "runner")
    if exe.is_file():
        return exe, w
    w.mkdir(parents=True, exist_ok=True)
    for k, v in files.items():
        (w / k).parent.mkdir(parents=True, exist_ok=True)
        (w / k).write_bytes(v)
    if Path(compiler).stem.lower() == "cl":
        cmd = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/utf-8", "/D_USE_MATH_DEFINES",
               "/wd4244", "/wd4305", f"/I{w}", "runner.cpp", "/Fe:" + str(exe)]
    else:
        cmd = [compiler, "-std=c++20", "-O2", f"-I{w}", "runner.cpp", "-o", str(exe)]
    r = subprocess.run(cmd, cwd=w, env=env, capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        errs = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower()]
        print("\n".join(errs[:40]) or (r.stdout + r.stderr)[-4000:])
        raise SystemExit(f"native build of {ns} failed (work dir {w})")
    return exe, w


def run_native(exe: Path, w: Path, script: dict, tag: str, bench: int) -> tuple[dict, np.ndarray]:
    d = w / f"run_{tag}"
    d.mkdir(exist_ok=True)
    (d / "script.txt").write_text(expanded(script), encoding="ascii")
    _, env = compiler_environment()
    r = subprocess.run([str(exe), "--bench", str(bench)], cwd=d, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"native runner failed ({r.returncode}): {r.stderr[-2000:]}")
    info = json.loads(r.stdout.strip().splitlines()[-1])
    raw = np.frombuffer((d / "frames.rgb").read_bytes(), dtype=np.uint8)
    frames = raw.reshape(info["captured"], info["h"], info["w"], 3)
    return info, frames


# ── JS side ──────────────────────────────────────────────────────────────
def run_js(js: Path, script: dict, k4: float | None, tag: str, w: Path) -> tuple[dict, np.ndarray]:
    d = w / f"js_{tag}"
    d.mkdir(parents=True, exist_ok=True)
    sp = d / "script.txt"
    sp.write_text(expanded(script), encoding="ascii")
    out = d / "frames.rgb"
    tsx = WEB / "node_modules" / "tsx" / "dist" / "cli.mjs"
    node = shutil.which("node")
    if not node or not tsx.is_file():
        raise SystemExit(f"need node and {tsx} (the main repo's web/ toolchain; set PF_WEB)")
    cmd = [node, str(tsx), "--tsconfig", "./tsconfig.json", str(HERE / "dump.ts"), str(js), str(sp), str(out)]
    if k4 is not None:
        cmd += ["--k4", repr(float(k4))]
    env = dict(os.environ, NODE_PATH=str(WEB / "node_modules"))
    r = subprocess.run(cmd, cwd=WEB, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    lines = [l for l in r.stdout.strip().splitlines() if l.startswith("{")]
    if r.returncode != 0 or not lines:
        raise SystemExit(f"JS dump failed: {r.stdout[-1500:]} {r.stderr[-1500:]}")
    info = json.loads(lines[-1])
    if info.get("error"):
        print(f"  JS error: {info['error']}")
    raw = np.frombuffer(out.read_bytes(), dtype=np.uint8)
    return info, raw.reshape(info["captured"], info["h"], info["w"], 3)


# ── Compare ──────────────────────────────────────────────────────────────
THRESH = 24


def frame_metrics(a: np.ndarray, b: np.ndarray) -> dict:
    ai = a.astype(np.int16)
    bi = b.astype(np.int16)
    diff = np.abs(ai - bi)
    mad = float(diff.mean())
    off = float((diff.max(axis=2) > THRESH).mean() * 100)
    # Shift-tolerant: a C++ pixel counts as right if any JS pixel within one
    # pixel of it is within THRESH on every channel (edges one pixel over).
    h, w, _ = ai.shape
    pad = np.pad(ai, ((1, 1), (1, 1), (0, 0)), mode="edge")
    best = None
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            nb = pad[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]
            m = np.abs(nb - bi).max(axis=2)
            best = m if best is None else np.minimum(best, m)
    off1 = float((best > THRESH).mean() * 100)
    lum = lambda x: float((0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]).mean())
    return {"mad": round(mad, 3), "off": round(off, 3), "off1px": round(off1, 3),
            "lumJs": round(lum(ai), 2), "lumCpp": round(lum(bi), 2)}


def font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def strip(js: np.ndarray, cpp: np.ndarray, frames: list[int], mets: list[dict], title: str, path: Path) -> None:
    n, h, w, _ = js.shape
    s = max(1, 384 // max(w, h))
    cw, ch = w * s, h * s
    lab = 30
    gap = 4
    left = 44
    top = 26
    W = left + n * (cw + gap)
    H = top + 3 * (ch + lab + gap)
    img = Image.new("RGB", (W, H), (16, 16, 16))
    dr = ImageDraw.Draw(img)
    f12, f14 = font(12), font(14)
    dr.text((6, 5), title, fill=(230, 230, 230), font=f14)
    rows = [("JS", js), ("C++", cpp), ("diff", np.clip(np.abs(js.astype(np.int16) - cpp.astype(np.int16)) * 4, 0, 255).astype(np.uint8))]
    for r, (name, arr) in enumerate(rows):
        y0 = top + r * (ch + lab + gap)
        dr.text((4, y0 + lab + ch // 2 - 8), name, fill=(200, 200, 200), font=f14)
        for i in range(n):
            x0 = left + i * (cw + gap)
            m = mets[i]
            if r == 0:
                txt = f"f{frames[i]}  t={frames[i] / 30:.2f}s"
            elif r == 1:
                txt = f"MAD {m['mad']:.1f}  >24 {m['off']:.1f}%"
            else:
                txt = f"x4  +-1px>24 {m['off1px']:.1f}%"
            dr.rectangle([x0, y0, x0 + cw - 1, y0 + lab - 2], fill=(32, 32, 32))
            dr.text((x0 + 3, y0 + 7), txt, fill=(225, 225, 225), font=f12)
            img.paste(Image.fromarray(arr[i]).resize((cw, ch), Image.NEAREST), (x0, y0 + lab))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


# ── Static look at the header ────────────────────────────────────────────
def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


def lint(header: str) -> dict:
    """Double-precision traps that cost real time on the ESP32-S3 (single-
    precision FPU only): double types, un-suffixed float literals, and C libm
    calls that take double (sin, pow, ...) rather than sinf/powf/PFMath."""
    body = strip_comments(header)
    body_no_tables = re.sub(r"=\s*\{[^{}]*\}", "= {}", body)   # data tables are const-folded
    return {
        "double": len(re.findall(r"\bdouble\b", body)),
        "literals": len(re.findall(r"(?<![\w.])\d+\.\d*(?:[eE][-+]?\d+)?(?![\w.])", body_no_tables))
        + len(re.findall(r"(?<![\w.])\.\d+(?:[eE][-+]?\d+)?(?![\w.])", body_no_tables)),
        "libm_double": len(re.findall(
            r"(?<![\w:.])(sin|cos|tan|asin|acos|atan|atan2|pow|exp|log|log2|sqrt|floor|ceil|fmod|fabs|round|hypot)\s*\(",
            body)),
        # Not wrong, but what costs on the S3 and is cheap on the host: its FPU
        # has no divider (every float '/' is a __divsf3 call) and libm's
        # expf/powf/logf/sinf/... are software. Counted, not judged.
        "div": len(re.findall(r"[\w)\]]\s*/\s*[\w(]", body)),
        "libm_float": len(re.findall(
            r"(?<![\w:.])(sinf|cosf|tanf|atan2f|atanf|asinf|acosf|powf|expf|logf|log2f|sqrtf|fmodf|hypotf)\s*\(",
            body)),
    }


# ── Driver ───────────────────────────────────────────────────────────────
def run_pattern(label: str, header: bytes, ns: str, js: Path, scripts: dict[str, str], bench: int) -> dict:
    exe, w = build_native(header, ns)
    lt = lint(header.decode("utf-8", "replace"))
    results = {"pattern": label, "ns": ns, "js": str(js), "lint": lt, "scripts": {}}
    print(f"{label} ({ns}) vs {js.name}   lint: double={lt['double']} unsuffixed-literals={lt['literals']} "
          f"double-libm-calls={lt['libm_double']}   (cost hints: '/' {lt['div']}, float libm calls {lt['libm_float']})")
    for tag, text in scripts.items():
        sc = parse_script(text)
        cinfo, cfr = run_native(exe, w, sc, tag, bench if tag in ("centre", "bench") else 0)
        k4 = sc["k4"] if sc["k4"] is not None else (cinfo["k4"] if cinfo["k4Known"] else None)
        jinfo, jfr = run_js(js, sc, k4, tag, w)
        frames = captured_frames(sc)
        if (jinfo["w"], jinfo["h"]) != (cinfo["w"], cinfo["h"]):
            print(f"  {tag}: SIZE MISMATCH JS {jinfo['w']}x{jinfo['h']} vs C++ {cinfo['w']}x{cinfo['h']}")
            results["scripts"][tag] = {"error": "size mismatch", "js": jinfo, "cpp": cinfo}
            continue
        mets = [frame_metrics(jfr[i], cfr[i]) for i in range(len(frames))]
        worst = max(range(len(mets)), key=lambda i: mets[i]["mad"])
        summ = {
            "frames": frames,
            "per_frame": [dict(frame=f, **m) for f, m in zip(frames, mets)],
            "mad_mean": round(float(np.mean([m["mad"] for m in mets])), 3),
            "off_mean": round(float(np.mean([m["off"] for m in mets])), 3),
            "off1px_mean": round(float(np.mean([m["off1px"] for m in mets])), 3),
            "worst": {"frame": frames[worst], **mets[worst]},
            "cpp": cinfo, "js": {k: v for k, v in jinfo.items() if k != "ranges"},
            "k4": k4,
        }
        results["scripts"][tag] = summ
        png = OUT / label / f"{tag}.png"
        strip(jfr, cfr, frames, mets, f"{label} [{tag}]  {text}   (k4={k4})", png)
        wm = summ["worst"]
        print(f"  {tag:7s} MAD {summ['mad_mean']:6.2f}  >24 {summ['off_mean']:5.1f}%  "
              f"+-1px>24 {summ['off1px_mean']:5.1f}%  worst f{wm['frame']} MAD {wm['mad']:.1f} >24 {wm['off']:.1f}%  "
              f"| C++ {cinfo['medianMs']:.3f} ms/f (p90 {cinfo['p90Ms']:.3f})  JS {jinfo['medianMs']:.3f} ms/f  "
              f"-> {png.relative_to(EXH).as_posix()}")
    (OUT / label).mkdir(parents=True, exist_ok=True)
    (OUT / label / "summary.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    return results


def header_bytes(path: str | None, rev: str | None, name: str) -> bytes:
    if path:
        return Path(path).read_bytes()
    rel = f"firmware/patternflow/features/exhibit/patterns/{name}.h"
    if rev and rev.upper() != "WORKTREE":
        return subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{rel}"], capture_output=True,
                              check=True).stdout
    return (REPO / rel).read_bytes()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="one header vs one JS pattern")
    r.add_argument("--header", required=True)
    r.add_argument("--ns", required=True)
    r.add_argument("--js", required=True)
    r.add_argument("--name", help="label for out/<name>/ (default: header file stem)")
    r.add_argument("--script", action="append", help="a script (repeatable); tag=... prefix names it")
    r.add_argument("--standard", action="store_true", help="the standard set (default when no --script)")
    r.add_argument("--bench", type=int, default=BENCH_EXTRA, help="extra frames timed after the centre script")
    c = sub.add_parser("current", help="each pattern folder's .h vs the .js beside it (all six by default)")
    c.add_argument("only", nargs="?", help="comma-separated names, e.g. bakwan,yujin")
    c.add_argument("--bench", type=int, default=BENCH_EXTRA)
    v = sub.add_parser("validate-old", help="the six first-export headers vs patterns/_original/js")
    v.add_argument("--rev", default=OLD_REV, help=f"git rev of the headers in the clone (default {OLD_REV}; WORKTREE = files on disk)")
    v.add_argument("--only", help="comma-separated names")
    v.add_argument("--bench", type=int, default=BENCH_EXTRA)
    e = sub.add_parser("expand", help="print the expanded script")
    e.add_argument("--script", required=True)
    a = ap.parse_args()

    if a.cmd == "expand":
        sys.stdout.write(expanded(parse_script(a.script)))
        return 0
    if a.cmd == "run":
        scripts: dict[str, str] = {}
        for i, s in enumerate(a.script or []):
            m = re.match(r"(\w+)=(.*)", s)
            scripts[m.group(1) if m else f"s{i + 1}"] = m.group(2) if m else s
        if a.standard or not scripts:
            scripts = {**STANDARD, **scripts}
        label = a.name or Path(a.header).stem
        run_pattern(label, Path(a.header).read_bytes(), a.ns, Path(a.js).resolve(), scripts, a.bench)
        return 0
    if a.cmd == "current":
        only = set(a.only.split(",")) if a.only else None
        rows = []
        for name, ns, js in SIX:
            if only and name not in only:
                continue
            folder = next((EXH / "patterns").glob(f"*_{name}"))
            rows.append(run_pattern(name, (folder / f"{name}.h").read_bytes(), ns,
                                    folder / js, STANDARD, a.bench))
        print()
        print("summary (each folder's .h vs its .js; C++ ms = centre median, host):")
        for res in rows:
            sc = res["scripts"]
            worst = max(sc[t]["mad_mean"] for t in sc if "mad_mean" in sc[t])
            verdict = "OK (same picture)" if worst < 1.0 else "DIFFERS - the .h likely needs redoing"
            print(f"  {res['pattern']:14s} worst MAD {worst:5.2f}  C++ {sc['centre']['cpp']['medianMs']:.3f} ms/f  {verdict}")
        return 0
    if a.cmd == "validate-old":
        only = set(a.only.split(",")) if a.only else None
        rows = []
        for name, ns, js in SIX:
            if only and name not in only:
                continue
            res = run_pattern(f"old_{name}", header_bytes(None, a.rev, name), ns,
                              EXH / "patterns" / "_original" / "js" / js, STANDARD, a.bench)
            rows.append(res)
        print("\nsummary (old headers vs original JS; C++ ms = centre median incl. bench frames, host -O2):")
        for res in rows:
            sc = res["scripts"]
            mads = " ".join(f"{t}={sc[t]['mad_mean']:.1f}/{sc[t]['off_mean']:.0f}%" for t in sc if "mad_mean" in sc[t])
            print(f"  {res['pattern']:18s} C++ {sc['centre']['cpp']['medianMs']:.3f} ms/f  "
                  f"JS {sc['centre']['js']['medianMs']:.3f} ms/f   MAD/>24: {mads}")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
