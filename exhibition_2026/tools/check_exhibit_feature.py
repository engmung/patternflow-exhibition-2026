"""Host test for the Patternflow exhibit feature's frame logic (no hardware).

    python tools/check_exhibit_feature.py
    python tools/check_exhibit_feature.py --only logic|real|midi   (one program)

Needs a C++20 compiler: $CXX, g++, clang++ or MSVC, or failing those the
ziglang wheel (`pip install ziglang`), which host_compiler() runs as
`python -m ziglang c++`.

Compiles the REAL features/exhibit/ headers (core_exhibit.h,
core_exhibit_http.h, exhibit_config.h, feature_exhibit.h, and
exhibit_presets.h's lists) against the REAL Button and InputFrame structs cut
out of src/core_encoders.h and the real pf_feature.h / feature_presets.h, with
the panel, the fonts, NVS, the web server and the pattern bodies stubbed. A
small driver replays the sketch's frame order
(loop hook -> readInputFrame [pressed(), fillInput] -> clicked() -> sleep
request -> onSleep -> wake -> longPressed() x4 / UPDATE card -> takePattern ->
drawOverlay) and asserts:

  - boot lands on the default exhibit pattern (and captions it when the build's
    PF_EXHIBIT_CAPTION is on; the exhibit bundle sets it off); rows hidden
  - K4: 3 detents per step, wrap, reversal cancels, full lap is a no-op,
    entry from outside the range, K4 never reaches the frame
  - the menu lock: a 1.5 s hold on any button never fires longPressed() and
    never becomes a click, while btnPressed/btnHeld still reach K1-K3
  - the UPDATE card: K4 press = click (its exit), knob turns do not reach it
  - idle return once per idle period, re-armed by a hand, restarted by a
    console pick, clears leftover K4 detents, puts every knob variable back
  - sleep/wake: turns ignored while asleep and for the wake guard; no K4
    press leaks in the frame after a wake
  - caption: GFX text wrap off while measuring and printing, and back on;
    word-wrapped lines that fit the portrait panel; not over chrome; the
    runtime switch (/api/exhibit, NVS)
  - the registry range check refuses a list that is not the tail
  - K1-K3 knob positions: integrated from the hand's detents and clamped at
    0..PF_EXHIBIT_KNOB_DETENTS, the detents taken out of the frame, the
    positions carried as lanes on every frame (not on a knob in the frame
    its button goes down), kept across a K4 switch, not moved while asleep
    or while the UPDATE card is up, back at the starts on the idle return
  - appendStatus JSON (knobPos, knobDetents)

The stub font model follows lib/Adafruit_GFX: the classic 5x7 advances 6 px
and, while wrap is on, getTextBounds()/print() break to a new row whenever
the next glyph would pass the screen's right edge.

Before compiling, it also reads each pattern header exhibit_presets.h
includes and checks that every variable the pattern hands to PFParams::apply*
is in PF_EXHIBIT_REST_VARS, and that every listed variable belongs to a row.
A knob loop - `PFParams::apply(input, i, &knobVal[i], ...)` - counts as every
element of knobVal, sized from its declaration, and is listed element by
element (V(Ns::knobVal[0]) ...). The stub patterns carry the NAME each header
declares, so a row with an empty title is laid out with the real name. It
fails a pattern that reads input.knobDeltas or input.knobs itself: in this
build the shared positions reach a pattern only as lanes (PFParams::apply).
knobDeltas carries the core's lane motion instead (applyLaneMotion, about 1.5
clicks per detent and a jump after a press) and knobs the raw hardware count,
so such a pattern's picture would drift from CC 24-26.

Then two more programs.

REAL PATTERNS + MIDI. The six real pattern headers, unmodified, through the
real exhibit_presets.h, with the real exhibit feature, the real midi mapping
(features/midi/core_midi.h, built with the exhibit bundle's own overrides.h,
so PF_MIDI_OUT_LANE_ABSOLUTE as the bundle sets it), the real absolute bus
(src/core_bus.h), the real PFParams (abi/pf_params.h) and the sketch's real
applyLaneMotion, cut out of patternflow.ino. Only the panel, the canvas, the
fonts, NVS and the web server are stubbed. The driver replays the sketch's
frame: loop hook, readInputFrame (physical detents, bus release, buttons,
exhibit fillInput, midi fillInput, fillAbsolute), onSleep, applyLaneMotion,
noteFinalFrame, midi observeFrame, takePattern, the pattern's own update().
It keeps its own model of each knob (detents, clamped) and, after every
frame, compares each K1-K3 variable the running pattern hands to
PFParams::apply with lo + (hi - lo) x position - lo and hi as written in the
pattern's source - and every CC 24-26 sent with round(position x 127):
  - all six patterns, random turns including past both ends
  - a K4 switch keeps the positions (the new pattern's first update already
    matches) and moves no CC; the Program Change follows
  - the frame of a button press has no lane on that knob (HEOEUNBIN's K1/K2
    toggles still work), and the parameter catches up on the next frame
  - pacing: >= PF_MIDI_OUT_MIN_INTERVAL_MS between CCs of one knob, only on
    change, the last value always sent; never CC 27
  - the idle return: the default pattern's first frame is already at the
    start positions, the CCs go back to them, HEOEUNBIN's toggles come back
  - a host arriving hears the Program Change and then CC 24, 25, 26
  - the footfalls: HEOEUNBIN flags the frame a pair of feet lands in and the
    exhibit sends a note for it from drawOverlay, in that same frame. Checked
    against the pattern's own gait, read before and after every frame - a
    landing is the frame the phase passes 0 (note A) or 0.5 (note B), the
    pairs alternate, and the rate is 3 x Speed a second at both ends of K3
    and at its centre. Each note is released PF_EXHIBIT_PULSE_MS later;
    nothing sounds while the panel sleeps, while another pattern is on
    screen, or while MIDI is switched off, and nothing is ever left down.
It also prints, per pattern, what each K1-K3 variable starts at in its code
and what the start positions give it now.

Before that it checks the composition: firmware/bundles/exhibit/features_local.h
must name a PF_EXHIBIT_PULSE_SINK that goes out through PatternflowMidi::emit
and honours the /midi switch, because the exhibit feature names no other
feature and this test carries its own copy of that wiring.

MIDI UNCHANGED WITHOUT THE FLAG. core_midi.h as this branch had it before
PF_MIDI_OUT_LANE_ABSOLUTE existed (git show BASELINE_COMMIT, below) and the
current one built without the flag, side by side in one program, fed the
same random frames - hands, lanes (the audio edition's microphone), the
absolute bus, buttons, inbound MIDI, pattern changes, arrivals, setting
changes - must send the same bytes. Twice: with PF_MIDI_OUT_LANE_MOTION 0
(every edition) and 1.

The pattern set is whatever exhibit_presets.h lists at the time, so rerun it
after swapping in the artists' patterns.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parent / "patternflow"
SKETCH = REPO / "firmware" / "patternflow"
EXHIBIT = SKETCH / "features" / "exhibit"
sys.path.insert(0, str(REPO / "firmware" / "toolchain"))
from check_module_elf import compiler_environment  # noqa: E402


def host_compiler() -> tuple[list[str], dict]:
    """The C++ compiler, as the command that starts it: whatever the panel's
    own host tests find ($CXX, c++, g++, clang++, MSVC), else the Clang the
    ziglang wheel carries (`pip install ziglang`), run as
    `python -m ziglang c++` - a laptop with no toolchain but Python."""
    try:
        compiler, env = compiler_environment()
        return [compiler], env
    except SystemExit:
        try:
            import ziglang  # noqa: F401
        except ImportError:
            raise SystemExit("No C++ compiler found: set CXX, install GCC/Clang/MSVC Build Tools, "
                             "or `pip install ziglang`") from None
        return [sys.executable, "-m", "ziglang", "c++"], dict(os.environ)


def cut_struct(text: str, name: str) -> str:
    m = re.search(r"^struct " + name + r" \{.*?^\};", text, re.S | re.M)
    if not m:
        raise SystemExit(f"struct {name} not found in core_encoders.h - update this test")
    return m.group(0)


def define_block(text: str, name: str) -> str:
    m = re.search(r"^#define " + name + r"\(\w\)(?:.*\\\n)*.*$", text, re.M)
    if not m:
        raise SystemExit(f"#define {name} not found in exhibit_presets.h")
    return m.group(0)


def rest_entries(presets: str) -> list[tuple[str, str]]:
    # Ns::var, Ns::params.field, Ns::knobVal[2], Ns::Inner::var (a nested namespace)
    return re.findall(r"V\(\s*(\w+)::((?:\w+::)*[\w.]+(?:\[\d+\])?)\s*\)", define_block(presets, "PF_EXHIBIT_REST_VARS"))


def pattern_headers(presets: str) -> dict[str, tuple[Path, str]]:
    """Namespace -> (header, text) for every header exhibit_presets.h includes."""
    headers: dict[str, tuple[Path, str]] = {}
    for inc in re.findall(r'^#include "([^"]+)"', presets, re.M):
        path = (EXHIBIT / inc).resolve()
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"namespace\s+(\w+)\s*\{", text):
            headers.setdefault(m.group(1), (path, text))
    return headers


def pattern_name(ns: str, headers: dict[str, tuple[Path, str]]) -> str:
    """The NAME the pattern's header gives it (what an empty title captions)."""
    if ns in headers:
        text = headers[ns][1]
        start = re.search(r"namespace\s+" + ns + r"\s*\{", text)
        m = re.search(r'const\s+char\s*\*\s*(?:const\s+)?NAME\s*=\s*"([^"\\]*)"', text[start.end():] if start else text)
        if m:
            return m.group(1)
    return ns


def array_size(text: str, name: str) -> int | None:
    """N from the declaration `name[N]` (a literal, or a constant defined in the file)."""
    m = re.search(r"\b(?:float|int|bool|u?int\d+_t)\s+" + name + r"\s*\[\s*(\w+)\s*\]\s*[=;]", text)
    if not m:
        return None
    if m.group(1).isdigit():
        return int(m.group(1))
    c = re.search(r"\b(?:constexpr|const)\s+\w+\s+" + m.group(1) + r"\s*=\s*(\d+)\s*;", text)
    return int(c.group(1)) if c else None


def knob_targets(text: str, path: Path, ns: str) -> set[str]:
    """What the pattern hands to PFParams::apply*: `&knob`, `&params.speed`,
    `&knobVal[2]`, and - from a loop over the knobs, `apply(input, i,
    &knobVal[i], ...)` - every element of knobVal, sized from its declaration."""
    used: set[str] = set()
    for var, idx in re.findall(r"PFParams::apply\w*\s*\(\s*\w+\s*,\s*\w+\s*,\s*&\s*([\w.]+)(?:\s*\[\s*(\w+)\s*\])?", text):
        if not idx:
            used.add(var)
        elif idx.isdigit():
            used.add(f"{var}[{idx}]")
        else:
            n = array_size(text, var)
            if n is None:
                print(f"note: {ns} ({path.name}) hands {var}[{idx}] to PFParams, but the size of {var} "
                      "was not found - check its elements by hand")
                used.add(f"{var}[0]")
            else:
                used.update(f"{var}[{k}]" for k in range(n))
    return used


def check_rest_list(rows: list[str], rest: list[tuple[str, str]],
                    headers: dict[str, tuple[Path, str]]) -> list[str]:
    """Every PFParams knob of every row's pattern is in the rest list."""
    problems = []
    for ns in rows:
        if ns not in headers:
            problems.append(f"{ns}: no header included by exhibit_presets.h opens namespace {ns}")
            continue
        path, text = headers[ns]
        used = knob_targets(text, path, ns)
        listed = {v for n, v in rest if n == ns}
        if not used:
            print(f"note: {ns} ({path.name}) calls no PFParams::apply* - check its knob variables by hand")
        for v in sorted(used - listed):
            problems.append(f"{ns}::{v} is a knob ({path.name} hands it to PFParams) "
                            f"but is not in PF_EXHIBIT_REST_VARS")
        for v in sorted(listed - used):
            if used:
                print(f"note: {ns}::{v} is in PF_EXHIBIT_REST_VARS but {path.name} does not hand it to PFParams")
    for n, v in rest:
        if n not in rows:
            problems.append(f"PF_EXHIBIT_REST_VARS names {n}::{v}, but {n} is not a row of PF_EXHIBIT_LIST")
    return problems


# The commit on the exhibit-2026 branch just before PF_MIDI_OUT_LANE_ABSOLUTE:
# its core_midi.h is what every other edition must still send, byte for byte.
BASELINE_COMMIT = "013fd9b"


def split_args(text: str) -> list[str]:
    """Top-level comma split of a call's argument list."""
    args, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def apply_calls(text: str) -> list[tuple[str, str, str, str, str]]:
    """(helper, knob, variable, lo, hi) for every PFParams::apply* call, as written."""
    calls = []
    for m in re.finditer(r"PFParams::(apply\w*)\s*\(", text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
        args = split_args(text[m.end():i - 1])
        if len(args) >= 5 and args[2].startswith("&"):
            calls.append((m.group(1), args[1], args[2][1:].strip(), args[3], args[4]))
    return calls


def check_knob_reads(rows: list[str], headers: dict[str, tuple[Path, str]]) -> list[str]:
    """A pattern that reads the frame's detents or click counts itself does
    not get the shared positions: the exhibit hands those on as lanes, and
    what is left in knobDeltas is the core's lane motion (knobs: the raw
    hardware count)."""
    problems = []
    for ns in rows:
        if ns not in headers:
            continue
        path, text = headers[ns]
        code = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
        for field in ("knobDeltas", "knobs"):
            if re.search(r"\.\s*" + field + r"\s*\[", code):
                problems.append(f"{ns} ({path.name}) reads input.{field} itself. In the exhibit the shared "
                                f"knob positions reach a pattern only through PFParams::apply; {field} carries "
                                f"something else (knobDeltas: the core's lane motion, ~1.5 clicks per detent and "
                                f"a jump after a press; knobs: the raw hardware count), so that pattern's "
                                f"picture would drift from CC 24-26 - convert it to PFParams::apply, or accept "
                                f"it and drop this check")
    return problems


def check_pulse_sink() -> list[str]:
    """The exhibit raises the footfall notes and knows nothing about sending
    one; the composition names the wire. Program 2 below carries its own copy
    of that wiring, so this checks the bundle really says the same thing - a
    sink that goes out through the midi feature's own emit() and that honours
    the /midi page's switch. Without it the test would be proving its own
    three lines rather than the firmware's."""
    local = REPO / "firmware" / "bundles" / "exhibit" / "features_local.h"
    text = local.read_text(encoding="utf-8", errors="replace")
    name = re.search(r"^#define\s+PF_EXHIBIT_PULSE_SINK\s+(\w+)\s*$", text, re.M)
    if not name:
        return [f"{local.name} defines no PF_EXHIBIT_PULSE_SINK - the exhibit's footfall notes "
                f"would compile away and the panel would send nothing"]
    body = re.search(r"^static void " + name.group(1) + r"\((.*?)\)\s*\{(.*?)^\}", text, re.S | re.M)
    if not body:
        return [f"{local.name}: PF_EXHIBIT_PULSE_SINK names {name.group(1)}, which is not a "
                f"function in that file"]
    if "PatternflowMidi::emit(" not in body.group(2):
        return [f"{local.name}: {name.group(1)} does not go out through PatternflowMidi::emit - "
                f"the footfall notes would not share the session, the tx count or the transports "
                f"with the knob CCs and the button notes"]
    if "runtimeEnabled" not in body.group(2):
        return [f"{local.name}: {name.group(1)} does not check PatternflowMidi::runtimeEnabled - "
                f"emit() does not check it either (observeFrame does), so MIDI switched off on "
                f"/midi would silence everything except the footsteps"]
    return []


def cut_apply_lane_motion(ino: str) -> str:
    """The sketch's applyLaneMotion (and its hands-off constant), verbatim."""
    start = ino.find("constexpr uint32_t LANE_HANDS_OFF_MS")
    fn = ino.find("void applyLaneMotion(", start)
    if start < 0 or fn < 0:
        raise SystemExit("applyLaneMotion not found in patternflow.ino - update this test")
    i = ino.index("{", fn)
    depth = 0
    while True:
        depth += {"{": 1, "}": -1}.get(ino[i], 0)
        i += 1
        if depth == 0:
            return ino[start:i]


def lane_motion_scale(net_config: str) -> str:
    vals = re.findall(r"#define PF_LANE_MOTION_SCALE ([0-9.]+f?)", net_config)
    if not vals:
        raise SystemExit("PF_LANE_MOTION_SCALE default not found in net_config.h - update this test")
    return vals[-1]


ARDUINO_H = r"""#pragma once
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
// What the Arduino core defines that the patterns and the sketch use.
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#ifdef _MSC_VER
// GCC's __atomic_* builtins, which src/core_bus.h uses (the midi test has the
// same stand-in): single-threaded here, so plain operations are exact.
#define __ATOMIC_RELAXED 0
template<class T, class V> T __atomic_fetch_add(T* p, V v, int) { T o = *p; *p = (T)(o + (T)v); return o; }
template<class T, class V> T __atomic_add_fetch(T* p, V v, int) { *p = (T)(*p + (T)v); return *p; }
template<class T, class V> T __atomic_fetch_sub(T* p, V v, int) { T o = *p; *p = (T)(o - (T)v); return o; }
template<class T, class V> T __atomic_exchange_n(T* p, V v, int) { T o = *p; *p = (T)v; return o; }
template<class T, class V> void __atomic_store_n(T* p, V v, int) { *p = (T)v; }
#endif
inline uint32_t g_now = 1000;
inline uint32_t millis() { return g_now; }
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT_PULLUP = 2;
inline int g_pin[64];
inline int digitalRead(int pin) { return g_pin[pin]; }
inline void pinMode(int, int) {}
struct SerialStub {
  int printf(const char* f, ...) { va_list a; va_start(a, f); int n = vprintf(f, a); va_end(a); return n; }
  void println(const char* s) { puts(s); }
};
inline SerialStub Serial;
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String& operator+=(const char* c) { s += c; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(int v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned v) { s += std::to_string(v); return *this; }
  String& operator+=(long v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s += std::to_string(v); return *this; }
  bool operator==(const char* c) const { return s == c; }
  bool operator!=(const char* c) const { return s != c; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
};
"""

PREFERENCES_H = r"""#pragma once
#include <Arduino.h>
inline std::map<std::string, std::map<std::string, int>> g_nvs;
inline bool g_nvsFailWrites = false;
class Preferences {
  std::string ns_;
  bool ro_ = true;
 public:
  bool begin(const char* n, bool readOnly = false) {
    if (readOnly && !g_nvs.count(n)) return false;   // like nvs_open on a missing namespace
    if (!readOnly && g_nvsFailWrites) return false;
    ns_ = n; ro_ = readOnly;
    if (!readOnly) g_nvs[n];
    return true;
  }
  void end() {}
  bool isKey(const char* k) { return g_nvs[ns_].count(k) > 0; }
  bool getBool(const char* k, bool d) { auto& m = g_nvs[ns_]; auto it = m.find(k); return it == m.end() ? d : it->second != 0; }
  size_t putBool(const char* k, bool v) { if (ro_) return 0; g_nvs[ns_][k] = v ? 1 : 0; return 1; }
  int getInt(const char* k, int d) { auto& m = g_nvs[ns_]; auto it = m.find(k); return it == m.end() ? d : it->second; }
  size_t putInt(const char* k, int v) { if (ro_) return 0; g_nvs[ns_][k] = v; return 4; }
  bool remove(const char* k) { if (ro_) return false; return g_nvs[ns_].erase(k) > 0; }
};
"""

SLEEP_H = r"""#pragma once
namespace PatternflowSleep {
inline bool g_sleeping = false;   // the core's state; the driver flips it where the sketch does
inline bool isSleeping() { return g_sleeping; }
}
"""

HTTP_H = r"""#pragma once
#include <Arduino.h>
#include <functional>
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct WebServer {
  struct Route { std::string path; HTTPMethod method; std::function<void()> fn; };
  std::vector<Route> routes;
  std::map<std::string, std::string> args;
  int code = 0;
  std::string body;
  void on(const char* p, HTTPMethod m, std::function<void()> fn) { routes.push_back({p, m, fn}); }
  bool hasArg(const char* n) { return args.count(n) > 0; }
  String arg(const char* n) { auto it = args.find(n); return it == args.end() ? String() : String(it->second.c_str()); }
  void send(int c, const char*, const String& b) { code = c; body = b.s; }
  void send(int c, const char*, const char* b) { code = c; body = b; }
};
namespace PatternflowHttp {
inline WebServer srv;
inline WebServer& server() { return srv; }
inline bool begun = false;
inline void begin() { begun = true; }
}
"""

UI_TEXT_H = r"""#pragma once
#include <Arduino.h>
// Adafruit_GFX, as far as text layout goes (lib/Adafruit_GFX/Adafruit_GFX.cpp).
struct FakeDisplay {
  uint8_t rot = 0;
  bool wrap = true;            // the library's constructor default
  uint8_t getRotation() const { return rot; }
  void setRotation(uint8_t r) { rot = r; }
  void setTextWrap(bool w) { wrap = w; }
  int width() const { return (rot % 2) ? 64 : 128; }
  int height() const { return (rot % 2) ? 128 : 64; }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
};
inline FakeDisplay g_display;
inline FakeDisplay* dma_display = &g_display;
inline int uiLinePitch() { return 7; }
inline int uiSelectLinePitch() { return 9; }
struct DrawnLine { std::string text; int y; int h; uint16_t color; int rot; bool wrapOn; bool breaks; int width; int font; };
inline std::vector<DrawnLine> g_lines;
namespace PatternflowUiText {
inline int g_font = 0;         // 1 = name font (classic 5x7), 2 = chrome font (TomThumb-like)
inline void useNameFont() { g_font = 1; }
inline void useChromeFont() { g_font = 2; }
inline void useDefaultFont() { g_font = 0; }
inline int advance() { return g_font == 2 ? 4 : 6; }
inline int glyphW() { return g_font == 2 ? 3 : 6; }     // what the wrap test adds to x
inline int rowH() { return g_font == 2 ? 6 : 8; }
// getTextBounds from x = 0: rows break where charBounds() breaks them.
inline void boundsWith(void (*choose)(), const char* t, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
  choose();
  const int W = dma_display->width();
  int x = 0, rows = 0, maxx = -1;
  for (const char* p = t; *p; ++p) {
    if (rows == 0) rows = 1;
    if (dma_display->wrap && x + glyphW() > W) { x = 0; rows++; }
    const int right = x + glyphW() - 1;
    if (right > maxx) maxx = right;
    x += advance();
  }
  *x1 = 0; *y1 = 0;
  *w = (uint16_t)(maxx + 1);
  *h = (uint16_t)(rows * rowH());
}
// drawLine centres by the measured width, then print() - which breaks the
// line again, at the screen edge, if wrap is on.
inline void drawLine(void (*choose)(), const char* t, int y, uint16_t c) {
  int16_t x1, y1; uint16_t w, h;
  boundsWith(choose, t, &x1, &y1, &w, &h);
  const int W = dma_display->width();
  int x = (W - (int)w) / 2;
  bool breaks = false;
  for (const char* p = t; *p; ++p) {
    if (dma_display->wrap && x + glyphW() > W) { breaks = true; x = 0; }
    x += advance();
  }
  g_lines.push_back({t, y, (int)h, c, dma_display->getRotation(), dma_display->wrap, breaks, (int)w, g_font});
}
}  // namespace PatternflowUiText
"""

REGISTRY_H = r"""#pragma once
#include "src/core_encoders.h"
struct PatternEntry {
  const char* name;
  const char* const* knobLabels;
  void (*setup)();
  void (*update)(float dt, const InputFrame& input);
  void (*draw)();
  const char* modulePath;
  bool hidden;
  bool absoluteReady;
};
#define PATTERN_ENTRY(ns) \
  { ns::NAME, ns::KNOB_LABELS, ns::setup, ns::update, ns::draw, nullptr, false, ns::ABSOLUTE_READY }
#define PATTERN_ENTRY_HIDDEN(ns) \
  { ns::NAME, ns::KNOB_LABELS, ns::setup, ns::update, ns::draw, nullptr, true, ns::ABSOLUTE_READY }
inline int g_setupCalls[64];
namespace Origin {
const char* NAME = "Origin";
const char* const KNOB_LABELS[4] = {"a", "b", "c", "d"};
constexpr bool ABSOLUTE_READY = true;
void setup() { g_setupCalls[63]++; }
void update(float, const InputFrame&) {}
void draw() {}
}
#include "features/feature_presets.h"
PatternEntry presetPatterns[] = {
  PATTERN_ENTRY(Origin),
  PF_FEATURE_PRESETS
};
const int NUM_PRESETS = sizeof(presetPatterns) / sizeof(presetPatterns[0]);
"""

# The exhibit bundle's own overrides.h, as config.h includes it on the device:
# the preset seam, the midi settings, and anything set there for the exhibit.
CONFIG_H = r"""#pragma once
#define PANEL_RES_W 128
#define PANEL_RES_H 64
#include "overrides.h"
"""

CANVAS_H = r"""#pragma once
#include <Arduino.h>
#include "config.h"
// What the patterns call; the pixels go nowhere.
namespace PFCanvas {
constexpr int W = PANEL_RES_W;
constexpr int H = PANEL_RES_H;
inline void clear() {}
inline void resetFrame() {}
inline void setFrame(int, int) {}
inline void setPixel(int, int, uint8_t, uint8_t, uint8_t) {}
inline void present() {}
}
"""

TEST_CPP = r"""
#include <cassert>
#include "Arduino.h"
#include "features/exhibit/feature_exhibit.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int cur = 4;          // the sketch's currentPatternIdx (as if restored from NVS)
static bool updateCard = false;   // updateShowing: the only chrome the lock leaves reachable
static bool updateBusy = false;
static int sleepRequest = -1;     // the console's /api/display sleep, applied where the sketch does
static long clicks[4];
static Button* btns[4] = {&btn1, &btn2, &btn3, &btn4};
static const PFFeature& D = PFFeatureExhibit::descriptor;
namespace PX = PatternflowExhibit;
using PatternflowSleep::g_sleeping;

struct Result {
  InputFrame in{};
  bool longPress[4]{};
  bool clicked[4]{};
  bool switched = false;
  int to = -1;
  bool woke = false;
};

static Result frame(int d0, int d1, int d2, int d3, uint32_t advance = 16) {
  Result r;
  const int d[4] = {d0, d1, d2, d3};
  g_now += advance;
  PFFeatureFrame f{0.016f, "x", true, updateCard, cur, 0};
  D.loop(f);
  InputFrame in{};
  in.now = g_now;
  bool phys = false;
  for (int i = 0; i < 4; i++) { clicks[i] += d[i]; in.knobs[i] = clicks[i]; in.knobDeltas[i] = d[i]; if (d[i]) phys = true; }
  if (phys && !updateCard) D.onUserInput();
  for (int i = 0; i < 4; i++) { in.btnPressed[i] = btns[i]->pressed(); in.btnHeld[i] = btns[i]->isDown(); }
  D.fillInput(in);
  for (int i = 0; i < 4; i++) r.clicked[i] = btns[i]->clicked();
  if (sleepRequest >= 0) { g_sleeping = sleepRequest == 1; sleepRequest = -1; }
  D.onSleep(g_sleeping);
  r.in = in;
  if (g_sleeping) {
    for (int i = 0; i < 4; i++) if (d[i] != 0 || in.btnPressed[i]) r.woke = true;
    if (r.woke) g_sleeping = false;   // wake(), then the sketch returns for this frame
    return r;
  }
  if (updateCard) {
    if (r.clicked[3] && !updateBusy) updateCard = false;
    return r;   // the card swallows the frame; no long-press checks while it is up
  }
  for (int i = 0; i < 4; i++) r.longPress[i] = btns[i]->longPressed(1000);
  int idx;
  if (D.takePattern(&idx)) { cur = idx; r.switched = true; r.to = idx; }
  g_lines.clear();
  PFFeatureFrame f2{0.016f, "x", true, updateCard, cur, 0};
  if (D.drawOverlay) D.drawOverlay(f2);
  return r;
}

// Registry row idx's caption is on the portrait panel: its first line opens
// the artist in orange, or - on a row with no artist - the title in white
// (the pattern's NAME when the title is empty too).
static bool drewCaption(int idx) {
  const PFExhibitPresets::Caption& c = PFExhibitPresets::CAPTIONS[idx - PatternflowExhibit::firstIdx];
  const bool byArtist = c.artist[0] != 0;
  const std::string lead = byArtist ? c.artist : (c.title[0] ? c.title : presetPatterns[idx].name);
  const uint16_t color = byArtist ? dma_display->color565(232, 85, 46) : dma_display->color565(255, 255, 255);
  if (g_lines.empty()) return false;
  const DrawnLine& l = g_lines[0];
  return l.rot == 1 && l.color == color && !l.text.empty() && lead.compare(0, l.text.size(), l.text) == 0;
}

// The lines, read in order, spell text exactly: each break falls on a run of
// spaces, or - only in the compact font, which layoutLines falls back to when
// one word is wider than the panel - inside a word. Nothing left over.
static bool joinsTo(const std::vector<const DrawnLine*>& lines, const std::string& text) {
  size_t pos = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    const DrawnLine& l = *lines[i];
    if (i > 0) {
      size_t skip = pos;
      while (skip < text.size() && text[skip] == ' ') skip++;
      if (skip == pos && l.font != 2) return false;   // a word cut in the name font
      pos = skip;
    }
    if (l.text.empty() || text.compare(pos, l.text.size(), l.text) != 0) return false;
    pos += l.text.size();
  }
  return pos == text.size();
}

static void setPin(int b, bool down) { g_pin[btns[b]->pin] = down ? LOW : HIGH; }

// K1-K3 as the frame carries them on: no detents, and each knob's position
// (a, b, c detents from the low end) as its lane - except on a knob whose
// button went down in this frame, which has no lane that frame. K4 never has
// one. The feature's own count agrees.
static bool knobsAre(const Result& r, int a, int b, int c) {
  const int want[3] = {a, b, c};
  const double n = PF_EXHIBIT_KNOB_DETENTS;
  for (int i = 0; i < 3; i++) {
    if (r.in.knobDeltas[i] != 0) { printf("  K%d: detents %d in the frame\n", i + 1, r.in.knobDeltas[i]); return false; }
    if (r.in.knobAudioActive[i] == r.in.btnPressed[i]) { printf("  K%d: lane %d with press %d\n", i + 1, r.in.knobAudioActive[i], r.in.btnPressed[i]); return false; }
    if (std::fabs(r.in.knobAudioValue[i] - want[i] / n) > 1e-6) { printf("  K%d: lane %.5f, want %.5f\n", i + 1, r.in.knobAudioValue[i], want[i] / n); return false; }
    if (PatternflowExhibit::knobDetent[i].load() != want[i]) { printf("  K%d: detent %d, want %d\n", i + 1, PatternflowExhibit::knobDetent[i].load(), want[i]); return false; }
  }
  return !r.in.knobAudioActive[3];
}

static int clampDetent(int v) { return v < 0 ? 0 : (v > PF_EXHIBIT_KNOB_DETENTS ? PF_EXHIBIT_KNOB_DETENTS : v); }

static std::string api(const char* caption) {
  WebServer& s = PatternflowHttp::server();
  s.args.clear();
  if (caption) s.args["caption"] = caption;
  for (auto& rt : s.routes) if (rt.path == "/api/exhibit" && rt.method == HTTP_GET) { rt.fn(); return s.body; }
  return "no route";
}

static float restVar(int i) { return *(float*)PFExhibitPresets::REST_VARS[i].addr; }

int main() {
  for (auto& p : g_pin) p = HIGH;
  for (int i = 0; i < 4; i++) btns[i]->begin(10 + i);
  const int N = PFExhibitPresets::COUNT;
  CHECK(N >= 2);
  CHECK(NUM_PRESETS == N + 1);

  // The range check refuses a list whose tail is out of order.
  std::swap(presetPatterns[1], presetPatterns[2]);
  D.setup();
  CHECK(!PX::rangeOk);
  std::swap(presetPatterns[1], presetPatterns[2]);
  D.setup();
  CHECK(PX::rangeOk);
  const int first = PX::firstIdx;
  const int last = first + N - 1;
  const int def = PX::defaultIdx;
  CHECK(first == 1);
  CHECK(def == first + PF_EXHIBIT_DEFAULT_SLOT);
  // Hidden rows: the core flashes no NAME over the caption for a feature's pick.
  for (int i = first; i <= last; i++) CHECK(presetPatterns[i].hidden);
  CHECK(!presetPatterns[0].hidden);
  CHECK(PX::captionsEnabled.load() == (PF_EXHIBIT_CAPTION != 0));
  // The build's caption default (the exhibit ships with none): a switch draws
  // the caption, or nothing at all.
  const bool capOn = PF_EXHIBIT_CAPTION != 0;
  D.onNetwork();
  CHECK(PatternflowHttp::begun);

  // Boot: restored pattern replaced by the default on the first frame, captioned.
  cur = last;
  const float restBefore0 = PFExhibitPresets::REST_COUNT ? restVar(0) : 0.0f;
  Result r = frame(0, 0, 0, 0);
  CHECK(PX::restTaken);
  std::vector<float> restOrig;
  for (int i = 0; i < PFExhibitPresets::REST_COUNT; i++) restOrig.push_back(restVar(i));
  CHECK(r.switched && cur == def);
  CHECK(capOn ? drewCaption(def) : g_lines.empty());
  r = frame(0, 0, 0, 0);
  CHECK(!r.switched);
  // K1-K3 start where exhibit_config.h / overrides.h put them, rounded to a
  // detent, and are lanes from the first frame on.
  const int NK = PF_EXHIBIT_KNOB_DETENTS;
  const int S[3] = {(int)std::lround(PF_EXHIBIT_KNOB_START_1 * NK), (int)std::lround(PF_EXHIBIT_KNOB_START_2 * NK),
                    (int)std::lround(PF_EXHIBIT_KNOB_START_3 * NK)};
  CHECK(knobsAre(r, S[0], S[1], S[2]));

  // K4: three detents per step; K4 never in the frame; K1 moves its position
  // and its detents do not stay in the frame.
  r = frame(5, 0, 0, 2);
  CHECK(!r.switched && r.in.knobDeltas[3] == 0 && knobsAre(r, clampDetent(S[0] + 5), S[1], S[2]));
  r = frame(0, 0, 0, 1);
  CHECK(r.switched && cur == def + 1);
  CHECK(capOn ? drewCaption(cur) : g_lines.empty());
  // Reversal cancels the remainder.
  frame(0, 0, 0, 2); r = frame(0, 0, 0, -2);
  CHECK(!r.switched);
  r = frame(0, 0, 0, 1);
  CHECK(!r.switched && PX::accum == 1);   // one detent waiting
  // Backward and wrap (from a clean remainder).
  frame(0, 0, 0, -1);
  CHECK(PX::accum == 0);
  cur = first; frame(0, 0, 0, -1); frame(0, 0, 0, -1); r = frame(0, 0, 0, -1);
  CHECK(r.switched && cur == last);
  r = frame(0, 0, 0, 3);
  CHECK(r.switched && cur == first);
  // A fast turn: 3 steps in one frame; a full lap in one frame changes nothing.
  r = frame(0, 0, 0, 9);
  CHECK(r.switched && cur == first + (3 % N));
  { const int before = cur; r = frame(0, 0, 0, 3 * N); CHECK(!r.switched && cur == before); }
  // Entering from outside the range (Origin, a module).
  cur = 0; r = frame(0, 0, 0, 3);
  CHECK(r.switched && cur == first);
  cur = 0; r = frame(0, 0, 0, -3);
  CHECK(r.switched && cur == last);
  cur = 0; r = frame(0, 0, 0, 6);
  CHECK(r.switched && cur == first + 1);

  // K1-K3: one position each, 0..NK detents, integrated from the hand here
  // and carried as a lane. The model k[] is this test's, from the turns it makes.
  {
    int k[3] = {clampDetent(S[0] + 5), S[1], S[2]};
    auto turn = [&](int a, int b, int c) { k[0] = clampDetent(k[0] + a); k[1] = clampDetent(k[1] + b); k[2] = clampDetent(k[2] + c); };
    // Past both ends in one frame: clamped. The first detent back moves at once.
    r = frame(3 * NK, -3 * NK, 0, 0); turn(3 * NK, -3 * NK, 0);
    CHECK(k[0] == NK && k[1] == 0 && knobsAre(r, k[0], k[1], k[2]));
    r = frame(-1, 1, 0, 0); turn(-1, 1, 0);
    CHECK(k[0] == NK - 1 && k[1] == 1 && knobsAre(r, k[0], k[1], k[2]));
    r = frame(1, -1, 0, 0); turn(1, -1, 0);
    CHECK(knobsAre(r, k[0], k[1], k[2]));
    // Many small turns, both ways, all three: the count is exact.
    for (int t = 0; t < 400; t++) {
      const int a = (t * 7) % 5 - 2, b = (t * 3) % 7 - 3, c = (t % 3) - 1;
      r = frame(a, b, c, 0); turn(a, b, c);
      CHECK(knobsAre(r, k[0], k[1], k[2]));
    }
    // Back to the middle-ish, then a K4 switch: the next pattern gets the same
    // positions in the very frame it is switched to, and after it.
    r = frame(NK / 4 - k[0], 3 * NK / 4 - k[1], NK / 2 - k[2], 0); turn(NK / 4 - k[0], 3 * NK / 4 - k[1], NK / 2 - k[2]);
    CHECK(knobsAre(r, k[0], k[1], k[2]));
    for (int s = 0; s < N + 1; s++) {
      const int was = cur;
      r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP);
      CHECK(r.switched && cur != was && knobsAre(r, k[0], k[1], k[2]));
      r = frame(0, 0, 0, 0);
      CHECK(knobsAre(r, k[0], k[1], k[2]));
    }
    // And the next turn goes on from there.
    r = frame(1, -1, 1, 0); turn(1, -1, 1);
    CHECK(knobsAre(r, k[0], k[1], k[2]));
    // A press: that knob has no lane in the frame of the press - patterns
    // act on a press only when the knob is not remote-driven - and a turn
    // in that same frame still counts. Held on, the lane is back.
    for (int b = 0; b < 3; b++) {
      setPin(b, true);
      int d[3] = {0, 0, 0};
      d[b] = 2;
      r = frame(d[0], d[1], d[2], 0, 100); turn(d[0], d[1], d[2]);
      CHECK(r.in.btnPressed[b] && r.in.btnHeld[b] && !r.in.knobAudioActive[b]);
      CHECK(knobsAre(r, k[0], k[1], k[2]));
      r = frame(0, 0, 0, 0);
      CHECK(!r.in.btnPressed[b] && r.in.btnHeld[b] && r.in.knobAudioActive[b] && knobsAre(r, k[0], k[1], k[2]));
      setPin(b, false);
      r = frame(0, 0, 0, 0, 100);
      CHECK(knobsAre(r, k[0], k[1], k[2]));
    }
  }

  // Menu lock: a 1.5 s hold on each button never fires longPressed() and
  // never ends as a click; K1-K3 presses still reach the frame, K4's does not.
  for (int b = 0; b < 4; b++) {
    setPin(b, true);
    bool anyLong = false, sawPress = false, sawHeld = false;
    for (int t = 0; t < 100; t++) {
      r = frame(0, 0, 0, 0);
      anyLong |= r.longPress[b];
      sawPress |= r.in.btnPressed[b];
      sawHeld |= r.in.btnHeld[b];
    }
    setPin(b, false);
    r = frame(0, 0, 0, 0, 100);
    bool click = r.clicked[b];
    r = frame(0, 0, 0, 0);
    click |= r.clicked[b];
    CHECK(!anyLong);
    CHECK(!click);
    if (b < 3) { CHECK(sawPress && sawHeld); } else { CHECK(!sawPress && !sawHeld); }
  }
  // Control: the same hold on a Button nobody locks does fire (the harness can see it).
  {
    Button ctrl; ctrl.begin(40);
    g_pin[40] = LOW;
    bool fired = false;
    for (int t = 0; t < 100; t++) { g_now += 16; ctrl.pressed(); fired |= ctrl.longPressed(1000); }
    g_pin[40] = HIGH;
    CHECK(fired);
  }

  // The UPDATE card a failed console upload leaves up: turns do not reach it
  // (so its idle timer runs), K4 is not a pattern switch, and a K4 press is
  // the click that closes it - but not while the upload is still busy.
  {
    const int before = cur;
    const int k0 = PX::knobDetent[0].load(), k1 = PX::knobDetent[1].load(), k2 = PX::knobDetent[2].load();
    updateCard = true; updateBusy = true;
    frame(0, 0, 0, 0);
    r = frame(4, 2, 0, 6, 100);
    CHECK(r.in.knobDeltas[0] == 0 && r.in.knobDeltas[1] == 0 && r.in.knobDeltas[3] == 0);
    CHECK(knobsAre(r, k0, k1, k2));   // the card owns the knobs: the positions do not move
    CHECK(PX::accum == 0 && cur == before);
    setPin(3, true); r = frame(0, 0, 0, 0, 100);
    CHECK(r.clicked[3] && !r.in.btnPressed[3] && updateCard);   // busy: the core keeps the card
    setPin(3, false); frame(0, 0, 0, 0, 100);
    updateBusy = false;
    setPin(3, true); r = frame(0, 0, 0, 0, 100);
    CHECK(r.clicked[3] && !updateCard);                          // failed upload: K4 = EXIT
    setPin(3, false); r = frame(0, 0, 0, 0, 100);
    CHECK(!r.clicked[3] && !r.switched && cur == before);        // the release is not a second click
  }

  // Idle return: nothing at 179 s, back to the default at 180 s, once, with
  // every knob variable put back and the last visitor's K4 detents dropped.
  cur = def + 1; frame(1, 5, -5, 0);  // a hand, then silence
  CHECK(PX::knobDetent[0].load() != S[0] && PX::knobDetent[1].load() != S[1] && PX::knobDetent[2].load() != S[2]);
  r = frame(0, 0, 0, 2);              // two detents left on K4
  CHECK(!r.switched && PX::accum == 2);
  for (int i = 0; i < PFExhibitPresets::REST_COUNT; i++) *(float*)PFExhibitPresets::REST_VARS[i].addr += 7.0f;
  bool early = false;
  for (int s = 0; s < 178; s++) early |= frame(0, 0, 0, 0, 1000).switched;
  CHECK(!early && cur == def + 1);
  CHECK(PFExhibitPresets::REST_COUNT == 0 || restVar(0) == restBefore0 + 7.0f);
  bool returned = false;
  for (int s = 0; s < 4; s++) {
    r = frame(0, 0, 0, 0, 1000);
    // The positions are back at their starts IN the frame that switches:
    // the default pattern's first frame back already runs on them.
    if (r.switched) { returned = true; CHECK(r.to == def && knobsAre(r, S[0], S[1], S[2])); }
  }
  CHECK(returned && cur == def);
  CHECK(PX::accum == 0);
  CHECK(PFExhibitPresets::REST_COUNT == 0 || restVar(0) == restBefore0);
  for (int i = 0; i < PFExhibitPresets::REST_COUNT; i++) CHECK(restVar(i) == restOrig[i]);
  // Spent: a quiet panel is left alone after that.
  bool again = false;
  for (int s = 0; s < 400; s++) again |= frame(0, 0, 0, 0, 1000).switched;
  CHECK(!again && cur == def);
  // A console pick (a change nobody here handed over) restarts the clock:
  // picked 170 s into a quiet spell, it gets its own 180 s.
  frame(0, 0, 1, 0);
  for (int s = 0; s < 170; s++) frame(0, 0, 0, 0, 1000);
  cur = def + 2;                      // /api/patterns/select
  again = false;
  for (int s = 0; s < 175; s++) again |= frame(0, 0, 0, 0, 1000).switched;
  CHECK(!again && cur == def + 2);
  for (int s = 0; s < 10; s++) again |= frame(0, 0, 0, 0, 1000).switched;
  CHECK(again && cur == def);
  // Already on the default when the idle comes: no switch, but the knobs go
  // back to their starts all the same (the K3 turn above moved one).
  frame(2, 0, 0, 0);
  CHECK(PX::knobDetent[0].load() == clampDetent(S[0] + 2));
  again = false;
  for (int s = 0; s < 185; s++) { r = frame(0, 0, 0, 0, 1000); again |= r.switched; }
  CHECK(!again && cur == def && knobsAre(r, S[0], S[1], S[2]));

  // Every caption in the list: GFX text wrap off while it is laid out and
  // printed, back on afterwards; every line fits the 64 px portrait width
  // without print() breaking it, sits on the 128 px panel, and the words
  // come out whole and in order - the artist in orange (no line at all when
  // the row has no artist), then the title in white, or the pattern's NAME
  // when the title is empty. "(small)" marks a line in the compact font.
  for (int s = 0; s < N; s++) {
    const PFExhibitPresets::Caption& c = PFExhibitPresets::CAPTIONS[s];
    g_lines.clear();
    PX::drawCaption(s, first + s);
    CHECK(g_lines.size() >= (c.artist[0] ? 2u : 1u));
    CHECK(g_display.getRotation() == 0);
    CHECK(g_display.wrap);
    int minY = 1000, maxY = -1;
    std::vector<const DrawnLine*> part[2];   // [0] artist (orange), [1] title (white)
    for (auto& l : g_lines) {
      CHECK(l.rot == 1);
      CHECK(!l.wrapOn && !l.breaks);
      CHECK(l.width <= 60);
      CHECK(l.y - 2 >= 0 && l.y + l.h <= 128);
      minY = l.y < minY ? l.y : minY; maxY = l.y > maxY ? l.y : maxY;
      const bool white = l.color == dma_display->color565(255, 255, 255);
      CHECK(white || l.color == dma_display->color565(232, 85, 46));
      CHECK(white || part[1].empty());          // the artist sits above the title
      part[white ? 1 : 0].push_back(&l);
    }
    std::string lines;
    for (auto& l : g_lines) {
      lines += " ["; lines += l.text; lines += "]@"; lines += std::to_string(l.y);
      if (l.font == 2) lines += "(small)";
    }
    printf("caption %d: %zu lines, y %d..%d:%s\n", s, g_lines.size(), minY, maxY, lines.c_str());
    CHECK(joinsTo(part[0], c.artist));
    CHECK(joinsTo(part[1], c.title[0] ? c.title : presetPatterns[first + s].name));
  }

  // Caption timing and chrome, with captions on whatever the default.
  PX::captionsEnabled.store(true);
  cur = def; r = frame(0, 0, 0, 3);
  CHECK(r.switched && drewCaption(cur));
  CHECK(g_display.getRotation() == 0 && g_display.wrap);
  {
    PFFeatureFrame chromeFrame{0.016f, "x", true, true, cur, 0};
    g_lines.clear();
    D.drawOverlay(chromeFrame);
    CHECK(g_lines.empty());   // not over the device's own screens
  }
  updateCard = true; updateBusy = true; r = frame(0, 0, 0, 0);
  updateCard = false; updateBusy = false;
  g_lines.clear();
  r = frame(0, 0, 0, 0, (uint32_t)PF_EXHIBIT_CAPTION_MS);
  CHECK(g_lines.empty());
  PX::captionsEnabled.store(capOn);

  // The runtime switch: /api/exhibit?caption=0|1|default, remembered in NVS.
  {
    std::string b = api(nullptr);
    CHECK(b.find("\"caption\":true") != std::string::npos || PF_EXHIBIT_CAPTION == 0);
    CHECK(b.find("\"captionStored\":false") != std::string::npos);
    b = api("0");
    CHECK(PatternflowHttp::server().code == 200 && !PX::captionsEnabled.load());
    CHECK(g_nvs["pf_exhibit"]["caption"] == 0);
    r = frame(0, 0, 0, 3);
    CHECK(r.switched && g_lines.empty());
    // A reboot keeps it.
    PX::captionsEnabled.store(true); PX::captionStored.store(false);
    PX::loadCaptionSetting();
    CHECK(!PX::captionsEnabled.load() && PX::captionStored.load());
    b = api("1");
    CHECK(PX::captionsEnabled.load() && b.find("\"captionStored\":true") != std::string::npos);
    r = frame(0, 0, 0, 3);
    CHECK(r.switched && drewCaption(cur));
    b = api("default");
    CHECK(PX::captionsEnabled.load() == (PF_EXHIBIT_CAPTION != 0) && !g_nvs["pf_exhibit"].count("caption"));
    b = api("maybe");
    CHECK(PatternflowHttp::server().code == 400);
    g_nvsFailWrites = true;
    b = api("0");
    CHECK(PatternflowHttp::server().code == 500 && PX::captionsEnabled.load() == (PF_EXHIBIT_CAPTION != 0));
    g_nvsFailWrites = false;
  }

  // Sleep: turns do nothing, K4's press stays in the frame (it wakes the panel).
  {
    const int before = cur;
    sleepRequest = 1;
    r = frame(0, 0, 0, 0);
    CHECK(g_sleeping);
    r = frame(0, 0, 0, 6, 100);
    CHECK(r.woke && !g_sleeping);      // a turn woke it; the turn switched nothing
    CHECK(cur == before);
    r = frame(0, 0, 0, 3);             // the frame after the wake: onSleep(false) arms the guard
    CHECK(!r.switched && PX::wakeGuard);
    r = frame(0, 0, 0, 3);             // inside the wake guard
    CHECK(!r.switched && cur == before);
    r = frame(0, 0, 0, 3, (uint32_t)PF_EXHIBIT_WAKE_GUARD_MS + 10);
    CHECK(r.switched);
    sleepRequest = 1; frame(0, 0, 0, 0, 100);
    setPin(3, true);
    r = frame(0, 0, 0, 0, 100);
    CHECK(r.in.btnPressed[3] && r.woke);   // the press reached the wake check
    setPin(3, false); frame(0, 0, 0, 0, 100);
    frame(0, 0, 0, 0, 100);
    // The frame right after a wake: the feature has not heard onSleep(false)
    // yet, but no K4 press may leak out (it would be a note-on 63 with no
    // note-off) and no K4 turn may count.
    sleepRequest = 1; frame(0, 0, 0, 0, 100);
    const int k0 = PX::knobDetent[0].load(), k1 = PX::knobDetent[1].load(), k2 = PX::knobDetent[2].load();
    r = frame(2, 0, 0, 0, 100);        // K1 turn wakes it
    CHECK(r.woke && !g_sleeping && PX::sleeping);
    CHECK(knobsAre(r, k0, k1, k2));    // the turn that woke it is spent on waking: K1 did not move
    const int here = cur;
    setPin(3, true);
    r = frame(1, 0, 0, 3, 100);        // the frame after the wake: K1 counts again, K4 does not
    CHECK(!r.in.btnPressed[3] && !r.in.btnHeld[3] && r.in.knobDeltas[3] == 0);
    CHECK(!r.switched && cur == here && PX::accum == 0);
    CHECK(knobsAre(r, clampDetent(k0 + 1), k1, k2));
    setPin(3, false); frame(0, 0, 0, 0, 100);
  }

  // Status JSON, including an idle stamp newer than the clock read.
  {
    String json;
    D.appendStatus(json);
    CHECK(json.s.find(",\"exhibit\":{\"rangeOk\":true") == 0);
    CHECK(json.s.find("\"menusLocked\":true") != std::string::npos);
    CHECK(json.s.find("\"restVars\":" + std::to_string(PFExhibitPresets::REST_COUNT)) != std::string::npos);
    CHECK(json.s.back() == '}');
    // The knob positions, last: 0..1 with four decimals, and the detent count.
    char kp[128];
    snprintf(kp, sizeof(kp), ",\"knobDetents\":%d,\"knobPos\":[%.4f,%.4f,%.4f]}", NK,
             PX::knobDetent[0].load() / (double)NK, PX::knobDetent[1].load() / (double)NK,
             PX::knobDetent[2].load() / (double)NK);
    const std::string tail = kp;
    CHECK(json.s.size() > tail.size() && json.s.compare(json.s.size() - tail.size(), tail.size(), tail) == 0);
    printf("%s\n", json.c_str());
    PX::lastInputMs.store(g_now + 5);
    String j2;
    D.appendStatus(j2);
    CHECK(j2.s.find("\"idleMs\":0,") != std::string::npos);
  }
  CHECK(D.shortName == nullptr);

  printf("PASS: exhibit feature - %d patterns at %d..%d (hidden), %d rest variables, boot default, K4 steps/wrap/entry, "
         "menu lock on all four buttons, UPDATE card exit, idle return + rest + console pick, sleep and wake edge, "
         "captions wrap-off and runtime switch, K1-K3 positions (%d detents: clamp, lanes, press frame, K4 switch, "
         "sleep, UPDATE card, idle return), status\n", N, first, last, PFExhibitPresets::REST_COUNT, NK);
  return 0;
}
"""


# ── Program 2: the real patterns, the real midi mapping, the sketch's frame ──
REAL_TEST_CPP = r"""
#include <cassert>
#include <random>
#include "Arduino.h"
// The composition's overrides, first and for the same reason the firmware
// reaches them first (patternflow.ino includes config.h long before
// features.h): every #ifndef default in the tree - PF_MIDI_OUT_LANE_ABSOLUTE
// above all - has to see them before the file it belongs to is compiled.
#include "config.h"
#include "features/midi/core_midi.h"

// The composition's own wiring, as firmware/bundles/exhibit/features_local.h
// writes it: the exhibit knows how to raise a note and nothing about how to
// send one, so the image that carries both features names the sink. The
// static check in check_exhibit_feature.py keeps this the same three lines as
// the bundle's.
static void pfExhibitPulseToMidi(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!PatternflowMidi::runtimeEnabled) return;
  PatternflowMidi::emit(status, d1, d2);
}
#define PF_EXHIBIT_PULSE_SINK pfExhibitPulseToMidi

#include "features/exhibit/feature_exhibit.h"   // the exhibit, and through the registry the six patterns

// The sketch's lane step, verbatim from patternflow.ino.
#define PF_LANE_MOTION_SCALE @LANE_SCALE@
@APPLY_LANE_MOTION@

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static const double NK = PF_EXHIBIT_KNOB_DETENTS;
static int K[3];              // this test's model of each knob: detents from the low end
static int bad = 0;           // mismatches found by the generated pattern checks
static int checked = 0;

// A K1-K3 variable against lo + (hi - lo) x position, in double, with lo and
// hi exactly as the pattern's source hands them to PFParams::apply.
static void pfExpect(float actual, double lo, double hi, double pos, const char* ns, const char* var, int knob) {
  checked++;
  const double want = lo + (hi - lo) * pos;
  const double tol = 1e-5 * (std::fabs(hi - lo) > 1.0 ? std::fabs(hi - lo) : 1.0);
  if (!(std::fabs((double)actual - want) <= tol)) {
    if (bad < 12) printf("  %s::%s (K%d): %.6f, want %.6f at position %.5f\n", ns, var, knob + 1, actual, want, pos);
    bad++;
  }
}
static void pfReport(float now, double lo, double hi, double pos, const char* ns, const char* var, int knob) {
  printf("  %-22s K%d %-18s code starts %10.3f  opens at %10.3f  (%g..%g, position %.3f)\n", ns, knob + 1, var,
         now, lo + (hi - lo) * pos, lo, hi, pos);
}

@GENERATED@

static void (*const CHECKS[])(const double*) = {@CHECK_TABLE@};
static void (*const REPORTS[])(const double*) = {@REPORT_TABLE@};

struct Msg { uint32_t at; uint8_t s, a, b; };
static std::vector<Msg> out;
static void sink(uint8_t s, uint8_t a, uint8_t b) { out.push_back({g_now, s, a, b}); }

// The footfall notes, on their own channel: everything the knobs, the buttons
// and the pattern picker send is on PF_MIDI_CHANNEL, so a message is one or
// the other and the checks below can tell them apart by the status byte.
namespace PP = PatternflowExhibitPulse;
static bool isPulse(const Msg& m) { return m.s == PP::NOTE_ON || m.s == PP::NOTE_OFF; }
static int nonPulseFrom(size_t from, const Msg** last = nullptr) {
  int n = 0;
  for (size_t m = from; m < out.size(); m++) {
    if (isPulse(out[m])) continue;
    if (last) *last = &out[m];
    n++;
  }
  return n;
}

static int cur = 0;
static long clicks[4];
static Button* btns[4] = {&btn1, &btn2, &btn3, &btn4};
static const PFFeature& E = PFFeatureExhibit::descriptor;
namespace PX = PatternflowExhibit;

struct Result { InputFrame seen{}; bool switched = false; size_t outFrom = 0; };

// One frame, in the sketch's order (patternflow.ino loop()): no screens can
// open (the menu lock), so the mode is RUNNING and the lanes are enabled.
static Result frame(int d0, int d1, int d2, int d3, uint32_t adv = 16) {
  Result r;
  const int d[4] = {d0, d1, d2, d3};
  // Everything this frame sends, from the first hook on: the feature's loop
  // hook releases footfall notes that are due, before the pattern or midi
  // have said anything.
  r.outFrom = out.size();
  g_now += adv;
  const float dt = adv / 1000.0f;
  PFFeatureFrame f{dt, "x", true, false, cur, 0};
  E.loop(f);
  InputFrame in{};
  in.now = g_now;
  bool phys = false;
  for (int i = 0; i < 4; i++) {
    clicks[i] += d[i];
    in.knobs[i] = clicks[i];
    in.knobDeltas[i] = d[i];
    if (d[i] != 0) { PatternflowBus::releaseAbsolute(i); phys = true; }
  }
  if (phys) E.onUserInput();
  for (int i = 0; i < 4; i++) { in.btnPressed[i] = btns[i]->pressed(); in.btnHeld[i] = btns[i]->isDown(); }
  E.fillInput(in);
  PatternflowMidi::fillInput(in);
  PatternflowBus::fillAbsolute(in);
  for (int i = 0; i < 4; i++) btns[i]->clicked();
  E.onSleep(PatternflowSleep::g_sleeping);
  if (PatternflowSleep::g_sleeping) {
    // Where the sketch's loop() returns while the panel sleeps: the input
    // frame has been read and filled, and nothing below runs - no lanes, no
    // observeFrame, no pattern, no overlays. The feature's loop hook above
    // did run, which is what still releases a note that was down.
    r.seen = in;
    return r;
  }
  applyLaneMotion(in, true);
  PatternflowBus::noteFinalFrame(in);
  PatternflowMidi::observeFrame(in, cur);
  int idx;
  if (E.takePattern(&idx) || PatternflowMidi::takePattern(&idx)) { cur = idx; r.switched = true; }
  presetPatterns[cur].update(dt, in);
  // ...and the picture goes on the panel, and the overlays run on top of it -
  // where the exhibit sounds whatever this update() flagged (drawOverlay,
  // core_exhibit.h). Same frame, a few microseconds after the pattern drew.
  g_lines.clear();
  {
    PFFeatureFrame drawn{dt, "x", true, false, cur, 0};
    E.drawOverlay(drawn);
  }
  r.seen = in;
  return r;
}

static void turn(int a, int b, int c) {
  const int d[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    K[i] += d[i];
    if (K[i] < 0) K[i] = 0;
    if (K[i] > (int)NK) K[i] = (int)NK;
  }
}
static int ccOf(int i) { return (int)std::floor(K[i] * 127.0 / NK + 0.5); }

// Everything this frame must be, given the model: the running pattern's K1-K3
// variables, the lanes it was handed, and every CC it sent. `stale` marks a
// knob whose parameter is allowed to still hold `prev` (the frame of a press).
static int checkFrame(const Result& r, int stale = -1, int prevDetent = 0) {
  double pos[3] = {K[0] / NK, K[1] / NK, K[2] / NK};
  for (int i = 0; i < 3; i++) {
    if (r.seen.btnPressed[i]) {
      CHECK(!r.seen.knobAudioActive[i]);
    } else {
      CHECK(r.seen.knobAudioActive[i] && std::fabs(r.seen.knobAudioValue[i] - pos[i]) < 1e-6);
    }
  }
  CHECK(!r.seen.knobAudioActive[3] && r.seen.knobDeltas[3] == 0);
  if (stale >= 0) pos[stale] = prevDetent / NK;
  const int slot = cur - PX::firstIdx;
  CHECK(slot >= 0 && slot < PFExhibitPresets::COUNT);
  const int before = bad;
  CHECKS[slot](pos);
  if (bad != before) { printf("  ...on %s at t=%u\n", presetPatterns[cur].name, (unsigned)g_now); return 1; }
  for (size_t m = r.outFrom; m < out.size(); m++) {
    const Msg& x = out[m];
    if ((x.s & 0xF0) != 0xB0) continue;
    CHECK(x.s == 0xB0 && x.a >= 24 && x.a <= 26);          // channel 1, K1-K3 only: never CC 27
    if (x.b != ccOf(x.a - 24)) { printf("  CC %d = %d, want %d\n", x.a, x.b, ccOf(x.a - 24)); return 1; }
  }
  return 0;
}

// Per knob: CCs at least the pacer's interval apart and each one a change.
static int checkPacing(size_t from) {
  for (int cc = 24; cc <= 26; cc++) {
    int last = -1; uint32_t at = 0;
    for (size_t m = from; m < out.size(); m++) {
      if (out[m].s != 0xB0 || out[m].a != cc) continue;
      if (last >= 0) {
        CHECK(out[m].at - at >= (uint32_t)PF_MIDI_OUT_MIN_INTERVAL_MS);
        CHECK(out[m].b != last);
      }
      last = out[m].b; at = out[m].at;
    }
  }
  return 0;
}

// The last value the host heard for each knob is the model's.
static int checkTrailing() {
  for (int i = 0; i < 3; i++) {
    int last = 64;   // what the host is assumed to have before anything is sent
    for (auto& m : out) if (m.s == 0xB0 && m.a == 24 + i) last = m.b;
    if (last != ccOf(i)) { printf("  K%d: host has %d, want %d\n", i + 1, last, ccOf(i)); return 1; }
  }
  return 0;
}

static void setPin(int b, bool down) { g_pin[btns[b]->pin] = down ? LOW : HIGH; }

int main() {
  static_assert(PF_MIDI_OUT_LANE_ABSOLUTE == 1, "the exhibit bundle's overrides.h must set PF_MIDI_OUT_LANE_ABSOLUTE 1");
  for (auto& p : g_pin) p = HIGH;
  for (int i = 0; i < 4; i++) btns[i]->begin(10 + i);
  // The sketch's setup(): features first, then every compiled-in pattern's setup().
  E.setup();
  CHECK(PX::rangeOk);
  PatternflowMidi::registerSink(sink);
  for (int i = 0; i < NUM_PRESETS; i++) presetPatterns[i].setup();
  const int first = PX::firstIdx, def = PX::defaultIdx, NP = PFExhibitPresets::COUNT;
  K[0] = (int)std::lround(PF_EXHIBIT_KNOB_START_1 * NK);
  K[1] = (int)std::lround(PF_EXHIBIT_KNOB_START_2 * NK);
  K[2] = (int)std::lround(PF_EXHIBIT_KNOB_START_3 * NK);
  const int S[3] = {K[0], K[1], K[2]};
  {
    const double pos[3] = {K[0] / NK, K[1] / NK, K[2] / NK};
    printf("what each pattern opens on (K1-K3 at the start positions %.3f %.3f %.3f):\n", pos[0], pos[1], pos[2]);
    for (int s = 0; s < NP; s++) REPORTS[s](pos);
  }
@HEOEUNBIN_REST@

  // Boot on a restored pattern: the default comes up, already at the start positions.
  cur = first + NP - 1;
  Result r = frame(0, 0, 0, 0);
  CHECK(r.switched && cur == def);
  CHECK(checkFrame(r) == 0);
  r = frame(0, 0, 0, 0);
  CHECK(checkFrame(r) == 0);
  CHECK(out.size() >= 1 && out.back().s == 0xC0 && out.back().a == def);   // the Program Change, a frame later

  // Every pattern: random turns, ends included, then a K4 switch.
  std::mt19937 rng(20260919);
  int frames = 0;
  const size_t outStart = out.size();
  for (int s = 0; s < NP; s++) {
    for (int t = 0; t < 150; t++) {
      int d[3];
      for (int i = 0; i < 3; i++) {
        const unsigned u = rng() % 100;
        d[i] = u < 45 ? 0 : u < 95 ? (int)(rng() % 5) - 2 : ((rng() & 1) ? 1 : -1) * ((int)NK + 3);
      }
      const uint32_t adv = 5 + rng() % 36;
      r = frame(d[0], d[1], d[2], 0, adv); turn(d[0], d[1], d[2]); frames++;
      CHECK(checkFrame(r) == 0);
    }
    // Quiet: the trailing values land.
    for (int t = 0; t < 5; t++) { r = frame(0, 0, 0, 0, 20); CHECK(checkFrame(r) == 0); }
    CHECK(checkTrailing() == 0);
    // The switch: the next pattern, the same positions, in the switching
    // frame itself; no CC, because no knob moved; its Program Change next.
    const int was = cur;
    const size_t mark = out.size();
    r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP);
    CHECK(r.switched && cur == PX::stepFrom(was, 1));
    CHECK(checkFrame(r) == 0);
    r = frame(0, 0, 0, 0);
    CHECK(checkFrame(r) == 0);
    // Nothing but the Program Change - footfall notes aside, which are the
    // pattern that is now on screen walking and have their own checks below.
    for (size_t m = mark; m < out.size(); m++) CHECK(out[m].s != 0xB0);
    const Msg* lastPC = nullptr;
    CHECK(nonPulseFrom(mark, &lastPC) == 1 && lastPC->s == 0xC0 && lastPC->a == cur);
  }
  CHECK(cur == def);
  CHECK(checkPacing(outStart) == 0);
  int ccs = 0;
  for (size_t m = outStart; m < out.size(); m++) ccs += out[m].s == 0xB0;
  CHECK(ccs > 100);

  // A press, on every pattern and every knob, with a turn in the same frame:
  // no lane on that knob for that frame (the pattern sees a hand's press),
  // the parameter holds its last value for that frame and lands on the new
  // position the next; a note-on, and no CC for that knob in that frame.
  for (int s = 0; s < NP; s++) {
    for (int b = 0; b < 3; b++) {
      const int prev = K[b];
      int d[3] = {0, 0, 0};
      d[b] = K[b] >= (int)NK - 2 ? -2 : 2;
      setPin(b, true);
      r = frame(d[0], d[1], d[2], 0, 100); turn(d[0], d[1], d[2]);
      CHECK(r.seen.btnPressed[b] && !r.seen.knobAudioActive[b]);
      CHECK(checkFrame(r, b, prev) == 0);
      bool noteOn = false;
      for (size_t m = r.outFrom; m < out.size(); m++) {
        CHECK(!(out[m].s == 0xB0 && out[m].a == 24 + b));
        noteOn |= out[m].s == 0x90 && out[m].a == 60 + b;
      }
      CHECK(noteOn);
      r = frame(0, 0, 0, 0, 60);
      CHECK(checkFrame(r) == 0);
      setPin(b, false);
      r = frame(0, 0, 0, 0, 100);
      CHECK(checkFrame(r) == 0);
    }
    r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP);
    CHECK(r.switched && checkFrame(r) == 0);
  }
  CHECK(checkTrailing() == 0);
@HEOEUNBIN_PRESS@

  // A host arrives (the RTP session joins, FlowLocal restarts): the Program
  // Change, then CC 24, 25, 26 at their positions - nothing else, in that order.
  {
    r = frame(3, -2, 1, 0); turn(3, -2, 1);
    CHECK(checkFrame(r) == 0);
    for (int t = 0; t < 4; t++) { r = frame(0, 0, 0, 0, 20); CHECK(checkFrame(r) == 0); }
    PatternflowMidi::requestStateResend();
    r = frame(0, 0, 0, 0);
    CHECK(checkFrame(r) == 0);
    // In order, ignoring the footfalls of whichever pattern is walking.
    std::vector<Msg> said;
    for (size_t m = r.outFrom; m < out.size(); m++) if (!isPulse(out[m])) said.push_back(out[m]);
    CHECK(said.size() == 4);
    CHECK(said[0].s == 0xC0 && said[0].a == cur);
    for (int i = 0; i < 3; i++) CHECK(said[1 + i].s == 0xB0 && said[1 + i].a == 24 + i && said[1 + i].b == ccOf(i));
    r = frame(0, 0, 0, 0, 100);
    CHECK(nonPulseFrom(r.outFrom) == 0);
  }

  // The idle return, from another pattern with the knobs away from their
  // starts: the frame that switches back already hands the default pattern
  // the start positions, and the CCs follow them there.
  {
    r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP);
    CHECK(r.switched && cur != def);
    int d[3];
    for (int i = 0; i < 3; i++) d[i] = (K[i] == S[i]) ? (S[i] > 2 ? -2 : 2) : 0;
    r = frame(d[0], d[1], d[2], 0); turn(d[0], d[1], d[2]);
    CHECK(checkFrame(r) == 0 && K[0] != S[0] && K[1] != S[1] && K[2] != S[2]);
@HEOEUNBIN_TOGGLE@
    bool back = false;
    for (int t = 0; t < 200 && !back; t++) {
      r = frame(0, 0, 0, 0, 1000);
      if (r.switched) {
        back = true;
        CHECK(cur == def);
        for (int i = 0; i < 3; i++) K[i] = S[i];
        CHECK(checkFrame(r) == 0);
        for (int i = 0; i < 3; i++) {   // the start CCs, in the same frame
          bool sent = false;
          for (size_t m = r.outFrom; m < out.size(); m++) sent |= out[m].s == 0xB0 && out[m].a == 24 + i;
          CHECK(sent);
        }
      } else {
        CHECK(checkFrame(r) == 0);
      }
    }
    CHECK(back);
    r = frame(0, 0, 0, 0);
    CHECK(checkFrame(r) == 0);
    CHECK(out.back().s == 0xC0 && out.back().a == def);
    CHECK(checkTrailing() == 0);
@HEOEUNBIN_RESTORED@
  }
@HEOEUNBIN_PULSE@
  // Whatever else the run sent, every message on the pulse channel is one of
  // the two notes, and nothing is left down.
  {
    int ons = 0, offs = 0;
    for (auto& m : out) {
      if (!isPulse(m)) continue;
      CHECK(m.a == PF_EXHIBIT_PULSE_NOTE_A || m.a == PF_EXHIBIT_PULSE_NOTE_B);
      if (m.s == PP::NOTE_ON) { CHECK(m.b == PF_EXHIBIT_PULSE_VELOCITY); ons++; }
      else { CHECK(m.b == 0); offs++; }
    }
    CHECK(ons == offs);
    printf("footfalls: %d notes on MIDI channel %d (%d / %d), velocity %d, %d ms\n", ons,
           (int)PF_EXHIBIT_PULSE_CHANNEL, (int)PF_EXHIBIT_PULSE_NOTE_A, (int)PF_EXHIBIT_PULSE_NOTE_B,
           (int)PF_EXHIBIT_PULSE_VELOCITY, (int)PF_EXHIBIT_PULSE_MS);
  }

  CHECK(bad == 0);
  printf("PASS: real patterns + midi - %d patterns, %d random frames, %d variable checks, %d CCs (all = round(position x 127), "
         "paced), K4 switch keeps positions, press frames, host arrival, idle return, footfalls on the "
         "frame the feet land\n", NP, frames, checked, ccs);
  return 0;
}
"""


# The footfall checks, spliced into program 2 when exhibit_presets.h really
# has a PF_EXHIBIT_PULSE row for HEOEUNBIN::stepPulse. @SLOT@ is that
# pattern's place in PF_EXHIBIT_LIST.
PULSE_CHECKS_CPP = r"""
  // ── The gecko's footfalls ─────────────────────────────────────────────
  //
  // HEOEUNBIN raises a bit in the frame a pair of feet lands and the exhibit
  // turns it into a note on PF_EXHIBIT_PULSE_CHANNEL from drawOverlay, in
  // that same frame. Nothing here trusts the pattern's arithmetic to a
  // second copy of it: every frame's gait is read out of the pattern before
  // and after, and a landing is the frame that phase passes 0 (the near
  // front and far hind legs, note A) or 0.5 (the other pair, note B).
  {
    const int heoPulseIdx = first + @SLOT@;
    while (cur != heoPulseIdx) { r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP); CHECK(checkFrame(r) == 0); }
    for (int t = 0; t < 4; t++) { r = frame(0, 0, 0, 0, 20); CHECK(checkFrame(r) == 0); }

    // Drain any active notes by observing note-off messages. A fixed quiet
    // window may never occur once the walking rate is increased.
    auto settle = [&]() -> int {
      bool active[128] = {};
      for (const auto& m : out) if (isPulse(m)) active[m.a] = m.s == PP::NOTE_ON;
      for (int t = 0; t < 600; t++) {
        if (!active[PF_EXHIBIT_PULSE_NOTE_A] && !active[PF_EXHIBIT_PULSE_NOTE_B]) return 0;
        const size_t m0 = out.size();
        r = frame(0, 0, 0, 0, 16);
        CHECK(checkFrame(r) == 0);
        for (size_t m = m0; m < out.size(); m++)
          if (isPulse(out[m])) active[out[m].a] = out[m].s == PP::NOTE_ON;
      }
      CHECK(!active[PF_EXHIBIT_PULSE_NOTE_A] && !active[PF_EXHIBIT_PULSE_NOTE_B]);
      return 0;
    };

    // K3 positions: both ends, centre and upper midpoint. Raw input stays
    // 0.2..1.2; actual speed reaches 2.4 at the top. The
    // gait turns dt x Speed x 1.5 a frame and each turn lands two pairs - so
    // the landings come 3 x Speed a second.
    const double lo = 0.2, hi = 1.2;
    struct Span { int detent; const char* what; double speed; };
    const Span spans[4] = {{0, "low end", 0.2}, {(int)NK / 2, "centre", 0.7},
                           {(int)NK * 3 / 4, "upper mid", 1.25}, {(int)NK, "high end", 2.4}};
    const uint32_t ADV = 16;
    for (auto& sp : spans) {
      const int d = sp.detent - K[2];
      r = frame(0, 0, d, 0); turn(0, 0, d);
      CHECK(checkFrame(r) == 0);
      const double pos = K[2] / NK;
      const double rawSpeed = lo + (hi - lo) * pos;
      const double speed = sp.speed;
      CHECK(std::fabs((double)HEOEUNBIN::speedVal - rawSpeed) < 1e-5);
      CHECK(speed * 1.5 * ADV / 1000.0 < 0.5);   // at most one landing a frame, either pair

      const int FRAMES = 600;
      int landings = 0, a = 0, b = 0, last = -1;
      const uint32_t t0 = g_now;
      const size_t from = out.size();
      for (int t = 0; t < FRAMES; t++) {
        const float g0 = HEOEUNBIN::gait;
        const size_t mark = out.size();
        r = frame(0, 0, 0, 0, ADV);
        CHECK(checkFrame(r) == 0);
        const float g1 = HEOEUNBIN::gait;
        const bool wrapped = g1 < g0;                              // the phase passed 0
        const bool half = !wrapped && g0 < 0.5f && g1 >= 0.5f;     // ...or 0.5
        int gotA = 0, gotB = 0;
        for (size_t m = mark; m < out.size(); m++) {
          if (out[m].s != PP::NOTE_ON) continue;
          if (out[m].a == PF_EXHIBIT_PULSE_NOTE_A) gotA++;
          else if (out[m].a == PF_EXHIBIT_PULSE_NOTE_B) gotB++;
        }
        if (gotA != (wrapped ? 1 : 0) || gotB != (half ? 1 : 0)) {
          printf("  frame %d at the %s: gait %.6f -> %.6f, sent A=%d B=%d, want A=%d B=%d\n", t, sp.what,
                 (double)g0, (double)g1, gotA, gotB, wrapped ? 1 : 0, half ? 1 : 0);
          return 1;
        }
        if (gotA) { CHECK(last != 0); last = 0; a++; }   // the pairs alternate
        if (gotB) { CHECK(last != 1); last = 1; b++; }
        landings += gotA + gotB;
      }
      const double seconds = (g_now - t0) / 1000.0;
      CHECK(settle() == 0);
      printf("  K3 at the %-8s (position %.3f): Speed %.3f, %d landings in %.2f s = %.3f/s "
             "(3 x Speed = %.3f/s)\n", sp.what, pos, speed, landings, seconds, landings / seconds,
             3.0 * speed);
      CHECK(a > 0 && b > 0 && a - b <= 1 && b - a <= 1);
      CHECK(std::fabs(landings - 3.0 * speed * seconds) <= 1.0);

      // Every note is released PF_EXHIBIT_PULSE_MS later, on the first frame
      // after that, and nothing strikes the same note twice without one.
      for (size_t m = from; m < out.size(); m++) {
        if (out[m].s != PP::NOTE_ON) continue;
        bool released = false;
        for (size_t n = m + 1; n < out.size(); n++) {
          if (!isPulse(out[n]) || out[n].a != out[m].a) continue;
          CHECK(out[n].s == PP::NOTE_OFF);
          CHECK(out[n].at >= out[m].at + (uint32_t)PF_EXHIBIT_PULSE_MS);
          CHECK(out[n].at <= out[m].at + (uint32_t)PF_EXHIBIT_PULSE_MS + ADV);
          released = true;
          break;
        }
        CHECK(released);
      }
    }

    // Asleep: the panel draws nothing, so nothing sounds - and whatever was
    // down when it went comes up at once instead of hanging for the night.
    {
      size_t mark = out.size();
      PatternflowSleep::g_sleeping = true;
      for (int t = 0; t < 40; t++) r = frame(0, 0, 0, 0, 16);
      for (size_t m = mark; m < out.size(); m++) CHECK(out[m].s != PP::NOTE_ON);
      CHECK(HEOEUNBIN::stepPulse == 0);
      PatternflowSleep::g_sleeping = false;
      bool sounded = false;
      for (int t = 0; t < 300 && !sounded; t++) {
        mark = out.size();
        r = frame(0, 0, 0, 0, 16);
        for (size_t m = mark; m < out.size(); m++) sounded |= out[m].s == PP::NOTE_ON;
      }
      CHECK(sounded);   // awake and walking again
      // Past the wake guard, so the K4 turn below is a pattern switch again.
      r = frame(0, 0, 0, 0, (uint32_t)PF_EXHIBIT_WAKE_GUARD_MS + 10);
      CHECK(settle() == 0);
    }

    // Another pattern on screen: a bit HEOEUNBIN left raised is dropped, not
    // sounded under somebody else's picture.
    {
      r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP);
      CHECK(r.switched && cur != heoPulseIdx && checkFrame(r) == 0);
      HEOEUNBIN::stepPulse = 3;
      const size_t mark = out.size();
      for (int t = 0; t < 30; t++) { r = frame(0, 0, 0, 0, 16); CHECK(checkFrame(r) == 0); }
      for (size_t m = mark; m < out.size(); m++) CHECK(!isPulse(out[m]));
      CHECK(HEOEUNBIN::stepPulse == 0);
      while (cur != heoPulseIdx) { r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP); CHECK(checkFrame(r) == 0); }
    }

    // MIDI switched off: the pattern goes on flagging and the panel goes on
    // drawing, and not a byte leaves. With PF_MIDI_SESSION_LOCKED (overrides.h)
    // nothing can switch it off - not /midi (setRuntimeEnabled refuses) and not
    // an "off" saved in NVS by an earlier build (loadSettings does not read
    // it) - so the switch is then set directly: the sink must still honour it.
    {
      CHECK(settle() == 0);
#if PF_MIDI_SESSION_LOCKED
      g_nvs["pf_midi"]["on"] = 0;
      PatternflowMidi::loadSettings();
      CHECK(PatternflowMidi::runtimeEnabled);
      CHECK(!PatternflowMidi::setRuntimeEnabled(false) && PatternflowMidi::runtimeEnabled);
      CHECK(g_nvs["pf_midi"]["on"] == 0);   // the refusal wrote nothing
      g_nvs.erase("pf_midi");
      PatternflowMidi::runtimeEnabled = false;
#else
      PatternflowMidi::setRuntimeEnabled(false);
#endif
      size_t mark = out.size();
      for (int t = 0; t < 200; t++) { r = frame(0, 0, 0, 0, 16); CHECK(checkFrame(r) == 0); }
      for (size_t m = mark; m < out.size(); m++) CHECK(!isPulse(out[m]));
#if PF_MIDI_SESSION_LOCKED
      PatternflowMidi::runtimeEnabled = true;
#else
      PatternflowMidi::setRuntimeEnabled(true);
#endif
      bool sounded = false;
      for (int t = 0; t < 300 && !sounded; t++) {
        mark = out.size();
        r = frame(0, 0, 0, 0, 16);
        for (size_t m = mark; m < out.size(); m++) sounded |= out[m].s == PP::NOTE_ON;
      }
      CHECK(sounded);
      CHECK(settle() == 0);
    }
  }
"""


# ── Program 3: the midi mapping without the flag, against the baseline ──────
DIFF_TEST_CPP = r"""
#include <algorithm>
#include <random>
#include "Arduino.h"
#include "Preferences.h"
#include "src/core_encoders.h"
#define PatternflowMidi PatternflowMidiBefore
#include "features/midi/core_midi_before.h"
#undef PatternflowMidi
#include "features/midi/core_midi.h"

static_assert(PF_MIDI_OUT_LANE_ABSOLUTE == 0, "this program compares the build without the flag");

struct Msg { uint8_t s, a, b; bool operator==(const Msg& o) const { return s == o.s && a == o.a && b == o.b; } };
static std::vector<Msg> A, B;
static void sinkA(uint8_t s, uint8_t a, uint8_t b) { A.push_back({s, a, b}); }
static void sinkB(uint8_t s, uint8_t a, uint8_t b) { B.push_back({s, a, b}); }

static bool sameFrame(const InputFrame& x, const InputFrame& y) {
  for (int i = 0; i < 4; i++) {
    if (x.knobs[i] != y.knobs[i] || x.knobDeltas[i] != y.knobDeltas[i] || x.btnPressed[i] != y.btnPressed[i] ||
        x.btnHeld[i] != y.btnHeld[i] || x.knobAudioActive[i] != y.knobAudioActive[i] ||
        x.knobAudioValue[i] != y.knobAudioValue[i] || x.paramAbsoluteActive[i] != y.paramAbsoluteActive[i] ||
        x.paramAbsolute[i] != y.paramAbsolute[i])
      return false;
  }
  return x.now == y.now;
}

namespace Old = PatternflowMidiBefore;
namespace New = PatternflowMidi;

int main() {
  Old::registerSink(sinkA);
  New::registerSink(sinkB);
  std::mt19937 rng(20260919);
  auto chance = [&](unsigned perMille) { return rng() % 1000 < perMille; };
  bool lane[4] = {}, held[4] = {};
  float laneV[4] = {0.5f, 0.5f, 0.5f, 0.5f};
  int pattern = 1;
  long msgs = 0, ccs = 0, laneFrames = 0, handCcs = 0;
  const int FRAMES = 300000;
  for (int t = 0; t < FRAMES; t++) {
    g_now += rng() % 41;
    // Settings changed on /midi, on both sides alike.
    if (chance(3)) { const bool on = rng() & 1; Old::setRuntimeEnabled(on); New::setRuntimeEnabled(on); }
    if (chance(4)) { const bool abs = (rng() % 4) != 0; Old::setOutAbsolute(abs); New::setOutAbsolute(abs); }
    if (chance(4)) { const int k = (int)(rng() % 5) - 1, m = 1 + (int)(rng() % 8); Old::setOutMultiplier(k, m); New::setOutMultiplier(k, m); }
    if (chance(3)) { const int k = (int)(rng() % 5) - 1, v = 1 + (int)(rng() % 16); Old::setOutDivisor(k, v); New::setOutDivisor(k, v); }
    // A host arrives.
    if (chance(6)) { Old::requestStateResend(); New::requestStateResend(); }
    // Inbound MIDI, on both sides alike (all of it goes through the same handlers).
    if (chance(60)) {
      const uint8_t ch = (rng() % 8) ? 1 : 2;
      switch (rng() % 5) {
        case 0: { const uint8_t cc = (uint8_t)(20 + rng() % 8), v = (uint8_t)(rng() % 128); Old::onControlChange(ch, cc, v); New::onControlChange(ch, cc, v); break; }
        case 1: { const uint8_t cc = (rng() & 1) ? 121 : 7, v = 0; Old::onControlChange(ch, cc, v); New::onControlChange(ch, cc, v); break; }
        case 2: { const uint8_t n = (uint8_t)(58 + rng() % 8), v = (uint8_t)(rng() % 3 ? 100 : 0); Old::onNoteOn(ch, n, v); New::onNoteOn(ch, n, v); break; }
        case 3: { const uint8_t n = (uint8_t)(58 + rng() % 8); Old::onNoteOff(ch, n, 0); New::onNoteOff(ch, n, 0); break; }
        default: { const uint8_t p = (uint8_t)(rng() % 8); Old::onProgramChange(ch, p); New::onProgramChange(ch, p); break; }
      }
    }
    if (chance(8)) pattern = (int)(rng() % 136) - 2;
    // The frame as the sketch hands it over: hands, lanes with the motion
    // applyLaneMotion writes for them, an absolute hold now and then, buttons.
    InputFrame base{};
    base.now = g_now;
    bool anyLane = false;
    for (int i = 0; i < 4; i++) {
      if (chance(15)) lane[i] = !lane[i];
      if (lane[i]) {
        laneV[i] += ((int)(rng() % 201) - 100) / 1000.0f;
        if (laneV[i] < 0.0f) laneV[i] = 0.0f;
        if (laneV[i] > 1.0f) laneV[i] = 1.0f;
        base.knobAudioActive[i] = true;
        base.knobAudioValue[i] = laneV[i];
        base.knobDeltas[i] = (int)(rng() % 7) - 3;
        anyLane = true;
      } else if (chance(300)) {
        base.knobDeltas[i] = (int)(rng() % 9) - 4;
      }
      if (chance(20)) { base.paramAbsoluteActive[i] = true; base.paramAbsolute[i] = (uint16_t)(rng() % 1001); base.knobAudioActive[i] = false; }
      const bool was = held[i];
      if (chance(25)) held[i] = !held[i];
      base.btnHeld[i] = held[i];
      base.btnPressed[i] = held[i] && !was;
      base.knobs[i] = (long)(rng() % 1000);
    }
    laneFrames += anyLane;
    InputFrame fa = base, fb = base;
    Old::fillInput(fa);
    New::fillInput(fb);
    if (!sameFrame(fa, fb)) { printf("FAIL frame %d: fillInput differs\n", t); return 1; }
    const size_t a0 = A.size(), b0 = B.size();
    Old::observeFrame(fa, pattern);
    New::observeFrame(fb, pattern);
    const bool same = A.size() - a0 == B.size() - b0 && std::equal(A.begin() + a0, A.end(), B.begin() + b0);
    if (!same) {
      printf("FAIL frame %d: the midi output differs from %s\n  before:", t, "@BASELINE@");
      for (size_t m = a0; m < A.size(); m++) printf(" %02X %02X %02X |", A[m].s, A[m].a, A[m].b);
      printf("\n  now:   ");
      for (size_t m = b0; m < B.size(); m++) printf(" %02X %02X %02X |", B[m].s, B[m].a, B[m].b);
      printf("\n");
      return 1;
    }
    for (size_t m = b0; m < B.size(); m++) {
      msgs++;
      if ((B[m].s & 0xF0) == 0xB0) { ccs++; if (!fb.knobAudioActive[B[m].a - 24]) handCcs++; }
    }
    int ia = -1, ib = -1;
    const bool ta = Old::takePattern(&ia), tb = New::takePattern(&ib);
    if (ta != tb || ia != ib) { printf("FAIL frame %d: takePattern differs\n", t); return 1; }
  }
  if (msgs < 10000 || ccs < 1000 || laneFrames < FRAMES / 4) { printf("FAIL: too little traffic to mean anything\n"); return 1; }
  printf("PASS: midi without PF_MIDI_OUT_LANE_ABSOLUTE (PF_MIDI_OUT_LANE_MOTION %d) sends what %s sent - %d frames, "
         "%ld with a lane, %ld messages, %ld CCs\n", PF_MIDI_OUT_LANE_MOTION, "@BASELINE@", FRAMES, laneFrames, msgs, ccs);
  (void)handCcs;
  return 0;
}
"""


def generated_checks(rows: list[str], headers: dict[str, tuple[Path, str]]) -> tuple[str, list[str]]:
    """Per row: pfCheckKnobs(pos) and pfReportStart(pos), inside the pattern's
    own namespace so lo/hi/variables read exactly as the source wrote them."""
    code, notes = [], []
    for ns in rows:
        path, text = headers[ns]
        checks, reports = [], []
        code_only = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
        for helper, knob, var, lo, hi in apply_calls(code_only):
            if helper != "apply":
                notes.append(f"note: {ns} hands {var} to PFParams::{helper} - not compared (only apply is)")
                continue
            label = var.replace('"', "")
            if knob.isdigit():
                if int(knob) > 2:
                    continue
                checks.append(f'pfExpect({var}, (double)({lo}), (double)({hi}), pos[{knob}], "{ns}", "{label}", {knob});')
                reports.append(f'pfReport({var}, (double)({lo}), (double)({hi}), pos[{knob}], "{ns}", "{label}", {knob});')
            else:
                checks.append(f"for (int {knob} = 0; {knob} < 3; ++{knob}) "
                              f'pfExpect({var}, (double)({lo}), (double)({hi}), pos[{knob}], "{ns}", "{label}", {knob});')
                reports.append(f"for (int {knob} = 0; {knob} < 3; ++{knob}) "
                               f'pfReport({var}, (double)({lo}), (double)({hi}), pos[{knob}], "{ns}", "{label}", {knob});')
        if not checks:
            notes.append(f"note: {ns} hands no K1-K3 variable to PFParams::apply - nothing of it is compared")
        code.append(f"namespace {ns} {{\ninline void pfCheckKnobs(const double* pos) {{\n  " + "\n  ".join(checks)
                    + "\n}\ninline void pfReportStart(const double* pos) {\n  " + "\n  ".join(reports) + "\n}\n}")
    return "\n".join(code), notes


def compile_and_run(compiler: list[str], env: dict, w: Path, name: str, defines: tuple[str, ...] = ()) -> int:
    exe = w / (name + ".exe" if Path(compiler[0]).suffix.lower() == ".exe" else name)
    src = w / (name + ".cpp")
    if Path(compiler[0]).stem.lower() == "cl":
        # M_PI_2 and friends, which src/core_math.h uses, need this under MSVC.
        cmd = [*compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W3", "/utf-8", "/D_USE_MATH_DEFINES",
               *[f"/D{d}" for d in defines],
               f"/I{w}", f"/Fo:{w / (name + '.obj')}", str(src), f"/Fe:{exe}"]
    else:
        cmd = [*compiler, "-std=c++20", "-O2", "-Wall", *[f"-D{d}" for d in defines], f"-I{w}", str(src), "-o", str(exe)]
        if compiler[1:3] == ["-m", "ziglang"]:
            # zig's Clang defines NDEBUG from -O1 up, and on Windows it targets
            # MinGW, whose math.h hides M_PI_2 under -std=c++20 as MSVC's does.
            cmd += ["-UNDEBUG", "-D_USE_MATH_DEFINES"]
    built = subprocess.run(cmd, cwd=w, env=env, capture_output=True, text=True, errors="replace")
    if built.returncode != 0:
        print(built.stdout, built.stderr)
        print(f"FAIL: {name}.cpp did not compile")
        return 1
    return subprocess.run([str(exe)], cwd=w, env=env).returncode


def main() -> int:
    # --only logic|real|midi runs one of the three programs (the static checks always run).
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv[:-1] else None

    def want(program: str) -> bool:
        return only is None or only == program

    enc = (SKETCH / "src" / "core_encoders.h").read_text(encoding="utf-8")
    presets = (EXHIBIT / "exhibit_presets.h").read_text(encoding="utf-8")
    names = re.findall(r"^\s*X\((\w+),", presets, re.M)
    if not names:
        raise SystemExit("no X(...) rows found in exhibit_presets.h")
    rest = rest_entries(presets)
    headers = pattern_headers(presets)
    problems = (check_rest_list(names, rest, headers) + check_knob_reads(names, headers)
                + check_pulse_sink())
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1

    compiler, env = host_compiler()
    overrides =(REPO / "firmware" / "bundles" / "exhibit" / "overrides.h").read_bytes()
    encoders = ("#pragma once\n#include <Arduino.h>\n" + cut_struct(enc, "Button") + "\n"
                + cut_struct(enc, "InputFrame") + "\nButton btn1, btn2, btn3, btn4;\n")

    def common(w: Path) -> None:
        (w / "src").mkdir()
        (w / "abi").mkdir()
        (w / "features" / "exhibit").mkdir(parents=True)
        (w / "features" / "midi").mkdir(parents=True)
        (w / "Arduino.h").write_text(ARDUINO_H, encoding="utf-8")
        (w / "Preferences.h").write_text(PREFERENCES_H, encoding="utf-8")
        (w / "config.h").write_text(CONFIG_H, encoding="utf-8")
        (w / "overrides.h").write_bytes(overrides)
        (w / "pattern_registry.h").write_text(REGISTRY_H, encoding="utf-8")
        (w / "src" / "core_encoders.h").write_text(encoders, encoding="utf-8")
        (w / "src" / "core_ui_text.h").write_text(UI_TEXT_H, encoding="utf-8")
        (w / "src" / "core_sleep.h").write_text(SLEEP_H, encoding="utf-8")
        (w / "src" / "core_http.h").write_text(HTTP_H, encoding="utf-8")
        for name in ("pf_feature.h", "feature_presets.h"):
            (w / "features" / name).write_bytes((SKETCH / "features" / name).read_bytes())
        for name in ("core_exhibit.h", "core_exhibit_http.h", "exhibit_config.h", "exhibit_pulse.h",
                     "feature_exhibit.h"):
            (w / "features" / "exhibit" / name).write_bytes((EXHIBIT / name).read_bytes())

    # ── Program 1: the exhibit's frame logic, stub patterns ──
    fakes = []
    counter = 0
    for i, ns in enumerate(names):
        plain = []
        structs: dict[str, list[str]] = {}
        arrays: dict[str, dict[int, str]] = {}
        nested: dict[str, list[str]] = {}
        for n, v in rest:
            if n != ns:
                continue
            counter += 1
            if "::" in v:   # a plain variable in a nested namespace (a layer unit's own state)
                inner, var = v.rsplit("::", 1)
                nested.setdefault(inner, []).append(f"float {var} = {counter}.25f;")
                continue
            element = re.fullmatch(r"(\w+)\[(\d+)\]", v)
            if element:
                arrays.setdefault(element.group(1), {})[int(element.group(2))] = f"{counter}.25f"
            elif "." in v:
                top, field = v.split(".", 1)
                structs.setdefault(top, []).append(f"float {field} = {counter}.25f;")
            else:
                plain.append(f"float {v} = {counter}.25f;")
        body = " ".join(plain) + " " + " ".join(
            f"struct {{ {' '.join(fields)} }} {top};" for top, fields in structs.items())
        body += " " + " ".join(
            f"float {name}[{max(vals) + 1}] = {{{', '.join(vals.get(k, '0.0f') for k in range(max(vals) + 1))}}};"
            for name, vals in arrays.items())
        for inner, decls in nested.items():
            opened = inner.split("::")
            body += " " + " ".join(f"namespace {x} {{" for x in opened) + " " + " ".join(decls) + " " + "}" * len(opened)
        # The header's own NAME, so each caption is laid out as the panel will show it.
        fakes.append(
            f'namespace {ns} {{ const char* NAME = "{pattern_name(ns, headers)}"; '
            f'const char* const KNOB_LABELS[4] = {{"a","b","c","d"}}; '
            f"constexpr bool ABSOLUTE_READY = true; void setup() {{ g_setupCalls[{i}]++; }} "
            f"void update(float, const InputFrame&) {{}} void draw() {{}} {body} }}"
        )
    body = re.sub(r'^#include "[^"]*presets/[^"]*".*$', "", presets, flags=re.M)
    # The pattern headers (exhibition_2026/patterns/<n>_<key>/<key>.h, included
    # by a path relative to exhibit_presets.h) are replaced by the fakes above.
    body = re.sub(r'^#include "[^"]*patterns/[^"]*".*$', "", body, flags=re.M)
    body = body.replace("#define PF_EXHIBIT_PRESETS_H", "#define PF_EXHIBIT_PRESETS_H\n" + "\n".join(fakes), 1)

    with tempfile.TemporaryDirectory(prefix="pf-exhibit-") as d:
        w = Path(d)
        common(w)
        (w / "features" / "exhibit" / "exhibit_presets.h").write_text(body, encoding="utf-8")
        (w / "test.cpp").write_text(TEST_CPP, encoding="utf-8")
        rc = compile_and_run(compiler, env, w, "test") if want("logic") else 0
    if rc:
        return rc

    # ── Program 2: the real patterns and the real midi mapping ──
    generated, notes = generated_checks(names, headers)
    for n in notes:
        print(n)
    heo = ("HEOEUNBIN" in names and "HEOEUNBIN" in headers
           and re.search(r"\bstatic\s+float\s+dir\b", headers["HEOEUNBIN"][1])
           and re.search(r"\bstatic\s+bool\s+rider\b", headers["HEOEUNBIN"][1]))
    if heo:
        slot = names.index("HEOEUNBIN")
        heo_rest = (f"  const float heoDir0 = HEOEUNBIN::dir;\n  const bool heoRider0 = HEOEUNBIN::rider;\n"
                    f"  const int heoSlot = {slot};\n")
        heo_press = (
            "  // HEOEUNBIN's own presses: K1 turns the walker round, K2 puts the rider on\n"
            "  // or off - both gated on the knob not being remote-driven.\n"
            "  while (cur != first + heoSlot) { r = frame(0, 0, 0, PF_EXHIBIT_DETENTS_PER_STEP); CHECK(checkFrame(r) == 0); }\n"
            "  for (int b = 0; b < 2; b++) {\n"
            "    const float dir0 = HEOEUNBIN::dir;\n"
            "    const bool rider0 = HEOEUNBIN::rider;\n"
            "    setPin(b, true);\n"
            "    r = frame(0, 0, 0, 0, 100);\n"
            "    CHECK(checkFrame(r) == 0);\n"
            "    if (b == 0) { CHECK(HEOEUNBIN::dir == -dir0 && HEOEUNBIN::rider == rider0); }\n"
            "    else { CHECK(HEOEUNBIN::rider == !rider0 && HEOEUNBIN::dir == dir0); }\n"
            "    r = frame(0, 0, 0, 0, 100);   // held: no second toggle\n"
            "    CHECK(checkFrame(r) == 0);\n"
            "    CHECK(HEOEUNBIN::dir == (b == 0 ? -dir0 : dir0) && HEOEUNBIN::rider == (b == 1 ? !rider0 : rider0));\n"
            "    setPin(b, false);\n"
            "    r = frame(0, 0, 0, 0, 100);\n"
            "    CHECK(checkFrame(r) == 0);\n"
            "  }\n")
        heo_toggle = ("    // The last visitor turned HEOEUNBIN's walker round and put the rider on.\n"
                      "    HEOEUNBIN::dir = -heoDir0;\n    HEOEUNBIN::rider = !heoRider0;\n")
        heo_restored = "    CHECK(HEOEUNBIN::dir == heoDir0 && HEOEUNBIN::rider == heoRider0);   // put back\n"
    else:
        print("note: no HEOEUNBIN row with dir/rider - its press and restore checks are skipped")
        heo_rest = heo_press = heo_toggle = heo_restored = ""
    # The footfalls: a PF_EXHIBIT_PULSE row for HEOEUNBIN::stepPulse, and the
    # pattern really declaring it. Both, or the checks below mean nothing.
    pulse_rows = re.findall(r"^\s*P\((\w+),\s*([\w:\[\]]+)\s*,", presets, re.M)
    heo_pulse = ("HEOEUNBIN" in names and "HEOEUNBIN" in headers
                 and re.search(r"\buint8_t\s+stepPulse\b", headers["HEOEUNBIN"][1])
                 and any(ns == "HEOEUNBIN" and flag == "HEOEUNBIN::stepPulse" for ns, flag in pulse_rows))
    if heo_pulse:
        heo_pulse_block = PULSE_CHECKS_CPP.replace("@SLOT@", str(names.index("HEOEUNBIN")))
    else:
        print("note: no PF_EXHIBIT_PULSE row for HEOEUNBIN::stepPulse - the footfall checks are skipped")
        heo_pulse_block = ""
    ino = (SKETCH / "patternflow.ino").read_text(encoding="utf-8")
    net_config = (SKETCH / "net_config.h").read_text(encoding="utf-8")
    real = (REAL_TEST_CPP
            .replace("@LANE_SCALE@", lane_motion_scale(net_config))
            .replace("@APPLY_LANE_MOTION@", cut_apply_lane_motion(ino))
            .replace("@GENERATED@", generated)
            .replace("@CHECK_TABLE@", ", ".join(f"{ns}::pfCheckKnobs" for ns in names))
            .replace("@REPORT_TABLE@", ", ".join(f"{ns}::pfReportStart" for ns in names))
            .replace("@HEOEUNBIN_REST@", heo_rest)
            .replace("@HEOEUNBIN_PRESS@", heo_press)
            .replace("@HEOEUNBIN_TOGGLE@", heo_toggle)
            .replace("@HEOEUNBIN_RESTORED@", heo_restored)
            .replace("@HEOEUNBIN_PULSE@", heo_pulse_block))
    with tempfile.TemporaryDirectory(prefix="pf-exhibit-real-") as d:
        w = Path(d)
        common(w)
        (w / "src" / "core_display.h").write_text('#pragma once\n#include "config.h"\n', encoding="utf-8")
        (w / "src" / "core_canvas.h").write_text(CANVAS_H, encoding="utf-8")
        for name in ("core_params.h", "core_math.h", "core_color.h", "core_noise.h", "core_mem.h", "core_bus.h"):
            (w / "src" / name).write_bytes((SKETCH / "src" / name).read_bytes())
        (w / "abi" / "pf_params.h").write_bytes((SKETCH / "abi" / "pf_params.h").read_bytes())
        # The real headers live outside the clone (exhibition_2026/patterns/),
        # included by a relative path that does not exist in this scratch tree:
        # copy each one in and point the presets file's include at the copy.
        (w / "features" / "exhibit" / "patterns").mkdir()
        presets_text = (EXHIBIT / "exhibit_presets.h").read_text(encoding="utf-8")
        def local_include(m: "re.Match[str]") -> str:
            src = (EXHIBIT / m.group(1)).resolve()
            (w / "features" / "exhibit" / "patterns" / src.name).write_bytes(src.read_bytes())
            return f'#include "patterns/{src.name}"'
        presets_text = re.sub(r'^#include "([^"]*patterns/[^"]*)"', local_include, presets_text, flags=re.M)
        (w / "features" / "exhibit" / "exhibit_presets.h").write_text(presets_text, encoding="utf-8")
        for name in ("core_midi.h", "midi_queue.h"):
            (w / "features" / "midi" / name).write_bytes((SKETCH / "features" / "midi" / name).read_bytes())
        (w / "real.cpp").write_text(real, encoding="utf-8")
        rc = compile_and_run(compiler, env, w, "real") if want("real") else 0
    if rc:
        return rc

    # ── Program 3: the midi mapping without the flag, against the baseline ──
    try:
        before = subprocess.run(["git", "-C", str(REPO), "show",
                                 f"{BASELINE_COMMIT}:firmware/patternflow/features/midi/core_midi.h"],
                                capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"FAIL: cannot read core_midi.h at {BASELINE_COMMIT} from {REPO} ({e}) - the byte-for-byte "
              "comparison of every other edition's MIDI output needs it")
        return 1
    diff = DIFF_TEST_CPP.replace("@BASELINE@", BASELINE_COMMIT)
    for motion in ("0", "1") if want("midi") else ():
        with tempfile.TemporaryDirectory(prefix="pf-exhibit-midi-") as d:
            w = Path(d)
            common(w)
            # No overrides.h here: the other editions do not carry the exhibit's.
            (w / "config.h").write_text("#pragma once\n", encoding="utf-8")
            (w / "src" / "core_bus.h").write_bytes((SKETCH / "src" / "core_bus.h").read_bytes())
            for name in ("core_midi.h", "midi_queue.h"):
                (w / "features" / "midi" / name).write_bytes((SKETCH / "features" / "midi" / name).read_bytes())
            (w / "features" / "midi" / "core_midi_before.h").write_bytes(before)
            (w / "diff.cpp").write_text(diff, encoding="utf-8")
            rc = compile_and_run(compiler, env, w, "diff", (f"PF_MIDI_OUT_LANE_MOTION={motion}",))
        if rc:
            return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
