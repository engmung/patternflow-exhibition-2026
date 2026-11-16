"""Bake YUJIN's sigmoid table: sigTab[i] = 1.0f / (1.0f + expf(-a)), a = -8 + i/8, i = 0..129.

Two independent float32 computations, which must agree bit for bit:
  1. the exact C++ expression from setup(), compiled with the harness compiler
     (the same one pfhost builds the runner with);
  2. mpmath: expf correctly rounded to float32, then the float32 add and divide.
Prints the C initializer (shortest round-trip decimals) for yujin.h SIG_TAB.
Run: python tools/patterns/host/bake_yujin_sigtab.py
"""
import os, struct, subprocess, sys
from pathlib import Path
import numpy as np
import mpmath

HERE = Path(__file__).resolve().parent / "out" / "yujin_sigtab"
HERE.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, r"G:/2026_G/26_1/패턴플로우/exhibition_2026/tools/patterns/host")
import pfhost  # noqa: E402

N = 130
cpp = r'''
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
int main() {
  for (int i = 0; i < 130; i++) {
    volatile float lo = -8.0f, sc = 8.0f;
    const float a = lo + (float)i / sc;
    const float v = 1.0f / (1.0f + expf(-a));
    uint32_t u; memcpy(&u, &v, 4);
    printf("%08x\n", (unsigned)u);
  }
  return 0;
}
'''
w = HERE / "gen"
w.mkdir(exist_ok=True)
(w / "g.cpp").write_text(cpp)
compiler, env = pfhost.compiler_environment()
exe = w / "g.exe"
if Path(compiler).stem.lower() == "cl":
    cmd = [compiler, "/nologo", "/O2", "g.cpp", "/Fe:" + str(exe)]
else:
    cmd = [compiler, "-O2", "g.cpp", "-o", str(exe)]
r = subprocess.run(cmd, cwd=w, env=env, capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
host = [int(l, 16) for l in subprocess.run([str(exe)], capture_output=True, text=True).stdout.split()]
assert len(host) == N

mpmath.mp.prec = 200
ref = []
for i in range(N):
    a = np.float32(-8.0) + np.float32(i) / np.float32(8.0)
    ex = mpmath.exp(-mpmath.mpf(float(a)))  # 200-bit exp, then to float32 via double
    e2 = np.float32(float(ex))
    v = np.float32(1.0) / (np.float32(1.0) + e2)
    ref.append(struct.unpack("<I", struct.pack("<f", float(v)))[0])

diff = [i for i in range(N) if host[i] != ref[i]]
print(f"// host compiler vs correctly-rounded float32: {N - len(diff)}/{N} identical; differing: {diff}", file=sys.stderr)

lines = []
for i in range(0, N, 4):
    parts = []
    for j in range(i, min(i + 4, N)):
        f = struct.unpack("<f", struct.pack("<I", host[j]))[0]
        t = f"{f:.9g}"
        assert np.float32(float(t)) == np.float32(f) and float(np.float32(float(t))) == f
        if "e" not in t and "." not in t: t += ".0"
        parts.append(f"{t}f")
    lines.append("  " + ", ".join(parts) + ",")
out = "\n".join(lines)
(HERE / "sig_tab.inc").write_text(out + "\n")
print(out)
