#!/usr/bin/env python3
"""Host test for the exhibit MIDI bridge's re-send logic (no hardware).

    python tools/check_exhibit_bridge.py

Compiles the REAL src/fl_midi_bridge.h with the first C++ compiler it finds
($CXX, c++, g++, clang++, cl, or MSVC Build Tools through vswhere), with:
  - its AppleMIDI session specialisations cut out (the block from the
    BEGIN_APPLEMIDI_NAMESPACE line to the END_APPLEMIDI_NAMESPACE line: they
    need the library's internals, and the session test below has them);
  - stand-ins for Arduino, FreeRTOS, Wi-Fi, UDP, NVS, TinyUSB and USBMIDI in
    tools/tests/exhibit_stubs.h, and for the AppleMIDI / MIDI Library front
    ends in tools/tests/exhibit_bridge_test.cpp;
  - the real ArduinoJson from .pio/libdeps/exhibit (run
    `python -m platformio pkg install -e exhibit` once if it is missing).

Then the RTP session test (tools/tests/exhibit_session_test.cpp): the WHOLE
src/fl_midi_bridge.h, specialisations included, compiled into the real
AppleMIDI 3.5.0 and MIDI Library 5.0.2 from .pio/libdeps/exhibit over the
same stand-ins (UDP a model socket). A scripted panel invites, repeats its
invitation, heartbeats, sends MIDI, goes silent, says BY; the test checks
what FlowLocal answers (OK / BY / CK1, never NO) and its bookkeeping: a
session joins; the same SSRC and token again gets the same OK again, byte for
byte, only until the data handshake, and never makes a second session; a new
token or a rebooted panel replaces its session without a BY; a full list ends
the session heard from least recently (a BY only to another address) instead
of refusing; a session unheard for 25 s ends with a BY (none when another
session is on its address) while heartbeats or MIDI keep it; the stranger BY
stays quiet for 1 s after any BY of FlowLocal's own; the panel's own BY and
"reconnect panel" through the real library.

The driver runs the bridge task's real loop one pass at a time against a
model USB host (a 16-packet FIFO the laptop drains every 1 ms frame while it
reads) and checks, packet by packet, what the laptop reads:
  - the panel's messages pass through unchanged; a pattern switch from the
    panel sends its Program Change and CC 118 mirror, and no knob values;
  - a resync request (CC 119 ch 16), the status page button, USB (re)mount
    and resume, and a dropped Program Change each send the last Program
    Change, its mirror, then CC 24 / 25 / 26 on channel 1 with the last
    values the panel sent (the newest, even if their own forward failed);
  - a knob the panel has not sent is skipped, and with no Program Change
    seen nothing is re-sent;
  - only CC 24-26 on channel 1 are kept (not CC 23 / 27 / 118 / 119, not
    channel 2 or 16);
  - a fresh Program Change from the panel stands in for an owed one, and the
    knob values still follow it; a mirror owed after a forwarded program
    goes out alone;
  - a re-send cut short by a full FIFO resumes in order (nothing ahead of an
    owed program or mirror) and does not repeat what already went out;
  - requests closer than 250 ms count once; other laptop messages are
    discarded;
  - /api/status's knob_resend, knob_values and knob_resends;
  - the PF monitor: a status request (F0 7D 50 46 01 F7) gets one whole
    reply F0 7D 50 46 02 <text> F7 with nothing between, after the panel's
    messages and after an owed re-send; its fields; at most one reply per
    200 ms; a reply nobody reads is given up after 20 ms; other SysEx and a
    request cut short are discarded and counted; the resync request works as
    before around them; pollHotspot's RSSI and channel lookup; the replies
    through the hotspot's boot silence (the task runs before begin(), RTP is
    not read until then); the /api/status fields;
  - the PF monitor's commands: reconnect panel (a BY to every session, the
    ack with their count, no BY of FlowLocal's own for the panel's packets
    under way), reboot panel (its own task, the POST it sends, the panel's
    address from the session or the one station with a lease, the ack for
    HTTP 200 / another code / silence / no connection / busy), reboot
    FlowLocal (the ack, then the restart 300 ms after it, also when the ack
    is given up), Wi-Fi channel (13 'C' 'H' <c>: stored in NVS, the ack, the
    restart with why=cmd13; auto, a channel, the next of 1 / 6 / 11 from the
    hotspot's channel; refused with nothing stored before the hotspot is up,
    when NVS fails, on a build with a fixed channel, and for an argument no
    channel can be, before the 3 s gate); each at most once per 3 s; wrong
    guards discarded; acks before a reply and behind an owed re-send; chm=
    and cs= from the channel plan setup() publishes;
  - jt= / jm= from RTP timestamps through noteRtpTiming();
  - self-recovery: rsn= / bc= / why= / ur= and /api/status's "restart"; the
    task watchdog subscribed once and fed every pass; loop()'s heartbeat check
    (quiet while the task passes, a restart 3 s after it stopped, why=
    bridge_hung on the next boot, refused after 3 automatic restarts since
    power-on and given back by one); the task watchdog's hook; the USB watch
    against a model laptop (stops reading, a missed resume, a bus reset that
    helps or not, a laptop that does not take the device back, nobody asking).

Before those it builds and runs three tests of pure headers (no stand-ins):
tools/tests/pf_monitor_test.cpp against src/fl_pf_monitor.h (the SysEx
assembler with the status request, the commands' guards and command 13's
argument, the command gate, the reply and ack text: key order, unknown values,
chm=manual, cs=, a month's values whole, the widest values within 240 bytes
and cut only between fields, the restart keys and cs last, the shortest text,
and their USB-MIDI packets for every text length),
tools/tests/link_quality_test.cpp against src/fl_rtp_jitter.h and
src/fl_ap_channel.h (RFC 3550 jitter, the 60 s transit spread, clock wraps,
the boot scan's channel scores and choice, the channel modes and command 13's
"next"), and tools/tests/recovery_test.cpp against src/fl_recovery.h (reset
reason names, the RTC boot record, the heartbeat watch and the USB watch,
millisecond by millisecond, across wraps).

The bridge test is built and run four times: as configured, with FL_EXHIBIT_KNOB_RESEND=0
(the re-send must vanish and everything else stay as it was), with
FL_EXHIBIT_PROGRAM_CC=0 (knob values right after the Program Change), and
with FL_EXHIBIT_AP_CHANNEL=6 (chm=fixed, command 13 refused). Then the
compile-time guards: a knob range holding CC 118 or CC 119, channel 16, or
more than 8 numbers must not compile, each with its own message, and neither
must self-recovery timings that do not fit together (a heartbeat limit as long
as the task watchdog's, a single request as evidence, reconnects closer than
one takes) or a session timeout no longer than two of the panel's heartbeats.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "src" / "fl_midi_bridge.h"
# The headers the bridge includes from src/: pure C++, and the NVS store
# (Preferences, a stand-in in exhibit_stubs.h).
HEADERS = [ROOT / "src" / name for name in ("fl_pf_monitor.h", "fl_rtp_jitter.h", "fl_ap_channel.h",
                                            "fl_recovery.h", "fl_channel_store.h")]
TESTS = ROOT / "tools" / "tests"
DRIVER = TESTS / "exhibit_bridge_test.cpp"
SESSION_DRIVER = TESTS / "exhibit_session_test.cpp"
SHARED_STUBS = TESTS / "exhibit_stubs.h"
PURE_TESTS = [
    ("PF monitor protocol (src/fl_pf_monitor.h alone)", TESTS / "pf_monitor_test.cpp"),
    ("link quality (src/fl_rtp_jitter.h, src/fl_ap_channel.h)", TESTS / "link_quality_test.cpp"),
    ("self-recovery decisions (src/fl_recovery.h alone)", TESTS / "recovery_test.cpp"),
]
# Empty headers standing in for the platform's; exhibit_stubs.h has what they
# would declare. The session test uses the real AppleMIDI.h (and MIDI.h), and
# AppleMIDI.h wants <IPAddress.h>.
STUBS = ("Arduino.h", "WiFi.h", "WiFiUdp.h", "USB.h", "USBMIDI.h", "tusb.h", "AppleMIDI.h",
         "flowlocal_config.h", "esp_timer.h", "esp_heap_caps.h", "esp_wifi.h",
         "esp_wifi_ap_get_sta_list.h", "esp_attr.h", "esp_system.h", "esp_task_wdt.h", "Preferences.h")
SESSION_STUBS = tuple(name for name in STUBS if name != "AppleMIDI.h") + ("IPAddress.h",)

VARIANTS = [
    ("as configured", []),
    ("knob re-send off", ["FL_EXHIBIT_KNOB_RESEND=0"]),
    ("program mirror off", ["FL_EXHIBIT_PROGRAM_CC=0"]),
    ("fixed channel", ["FL_EXHIBIT_AP_CHANNEL=6"]),
]

MUST_NOT_BUILD = [
    (["FL_EXHIBIT_KNOB_CC_FIRST=118", "FL_EXHIBIT_KNOB_CC_LAST=118"],
     "must not contain the program mirror's control number"),
    (["FL_EXHIBIT_KNOB_CC_FIRST=119", "FL_EXHIBIT_KNOB_CC_LAST=119"],
     "must not contain the resync request's control number"),
    (["FL_EXHIBIT_KNOB_CC_CHANNEL=16"],
     "must not be the resync request's or the program mirror's channel"),
    (["FL_EXHIBIT_KNOB_CC_FIRST=20", "FL_EXHIBIT_KNOB_CC_LAST=28"],
     "must be 1 to 8 control numbers"),
    (["FL_EXHIBIT_BRIDGE_STALL_MS=5000"],
     "shorter than the task watchdog's timeout"),
    (["FL_EXHIBIT_USB_ASK_MIN=1"],
     "USB watch: at least 2 requests"),
    (["FL_EXHIBIT_USB_RECONNECT_MIN_MS=5000"],
     "a reconnect must be over before the next may start"),
    (["FL_EXHIBIT_SESSION_TIMEOUT_MS=20000"],
     "FL_EXHIBIT_SESSION_TIMEOUT_MS must be 21000..61000"),
    (["FL_EXHIBIT_SESSION_TIMEOUT_MS=90000"],
     "FL_EXHIBIT_SESSION_TIMEOUT_MS must be 21000..61000"),
]


def compiler_environment() -> tuple[str, dict[str, str]]:
    """Same search as patternflow's firmware/toolchain/check_module_elf.py."""
    env = dict(os.environ)
    for name in (os.environ.get("CXX"), "c++", "g++", "clang++", "cl"):
        if name and (compiler := shutil.which(name)):
            return compiler, env
    if os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / (
            "Microsoft Visual Studio/Installer/vswhere.exe"
        )
        if vswhere.is_file():
            install = subprocess.check_output([
                str(vswhere), "-latest", "-products", "*", "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
            ], text=True).strip()
            if install:
                setup = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
                # cmd does not understand subprocess's quoting of a list
                # argument; the path comes from the local VS installer.
                output = subprocess.check_output(
                    f'cmd /d /u /s /c "call "{setup}" >nul && set"',
                    encoding="utf-16-le", timeout=60,
                )
                updates = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
                env = {key.upper(): value for key, value in env.items()}
                env.update({key.upper(): updates.get(key.upper(), value)
                            for key, value in updates.items()})
                compiler = shutil.which("cl", path=env.get("PATH"))
                if compiler:
                    return compiler, env
    raise SystemExit("No C++17 compiler found; set CXX or install GCC/Clang/MSVC Build Tools.")


def library_dir(name: str, header: str) -> Path:
    """A library env:exhibit pins (platformio.ini), as `pio pkg install` put it."""
    candidates = []
    if os.environ.get("PLATFORMIO_LIBDEPS_DIR"):
        candidates.append(Path(os.environ["PLATFORMIO_LIBDEPS_DIR"]) / "exhibit" / name / "src")
    candidates.append(ROOT / ".pio" / "libdeps" / "exhibit" / name / "src")
    for c in candidates:
        if (c / header).is_file():
            return c
    raise SystemExit(f"{name} not found under .pio/libdeps/exhibit: run "
                     "`python -m platformio pkg install -e exhibit` once.")


def bridge_without_sessions() -> str:
    lines = BRIDGE.read_text(encoding="utf-8").splitlines()
    begins = [i for i, line in enumerate(lines) if line.strip() == "BEGIN_APPLEMIDI_NAMESPACE"]
    ends = [i for i, line in enumerate(lines) if line.strip() == "END_APPLEMIDI_NAMESPACE"]
    if len(begins) != 1 or len(ends) != 1 or ends[0] < begins[0]:
        raise SystemExit(f"{BRIDGE}: expected one BEGIN_/END_APPLEMIDI_NAMESPACE block "
                         f"(found {len(begins)} / {len(ends)}); update this script")
    kept = lines[:begins[0]] + ["// (AppleMIDI session specialisations cut by check_exhibit_bridge.py)"] \
        + lines[ends[0] + 1:]
    return "\n".join(kept) + "\n"


def command(compiler: str, work: Path, libs: list[Path], defines: list[str], out: Path | None,
            driver: Path = DRIVER, packed_refs: bool = False) -> list[str]:
    """Build work/<driver>; libs are third-party include dirs (their warnings are not ours).

    packed_refs: the code binds references to members of packed structs, as
    AppleMIDI 3.5.0 does with the SSRC of the messages it parses
    (getParticipantBySSRC(invitation.ssrc), and the same in the members the
    bridge copies). A packed local may sit at any address on the PC, and the
    undefined-behaviour sanitizer zig's c++ builds in (debug) stops at the
    first misaligned one; only that check is turned off. On the ESP32-S3,
    where such a load would fault, the firmware has run this code since
    2026-09-19.
    """
    source = work / driver.name
    if Path(compiler).stem.lower() == "cl":
        cmd = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
               # the bridge is device code built with -Wall -Wextra; MSVC's
               # narrowing and constant-condition warnings are not its contract
               "/wd4244", "/wd4127", f"/I{work}", "/external:W0"]
        cmd += [f"/external:I{lib}" for lib in libs]
        cmd += [f"/D{d}" for d in defines]
        cmd += [str(source)] + ([f"/Fe:{out}", f"/Fo:{out.with_suffix('.obj')}"] if out else ["/Zs"])
    else:
        cmd = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror", f"-I{work}"]
        if packed_refs:
            cmd += ["-fno-sanitize=alignment"]
        for lib in libs:
            cmd += ["-isystem", str(lib)]
        cmd += [f"-D{d}" for d in defines]
        # Compile only (not -fsyntax-only, which zig's c++ refuses next to its own -c).
        cmd += [str(source)] + (["-o", str(out)] if out else ["-c", "-o", str(work / "guard.o")])
    return cmd


def build_and_run(compiler: str, env: dict[str, str], work: Path, libs: list[Path], defines: list[str],
                  driver: Path, name: str, label: str, packed_refs: bool = False) -> bool:
    exe = work / (name + (".exe" if os.name == "nt" else ""))
    built = subprocess.run(command(compiler, work, libs, defines, exe, driver, packed_refs), cwd=work, env=env,
                           capture_output=True, text=True, errors="replace")
    if built.returncode != 0:
        print(built.stdout + built.stderr)
        print(f"   FAIL: {label} did not build")
        return False
    ran = subprocess.run([str(exe)], cwd=work, env=env, capture_output=True, text=True, errors="replace")
    print(ran.stdout.rstrip())
    if ran.stderr.strip():
        print(ran.stderr.rstrip())
    if ran.returncode != 0:
        print(f"   FAIL: {label} (exit {ran.returncode})")
        return False
    return True


def main() -> int:
    compiler, env = compiler_environment()
    ajson = library_dir("ArduinoJson", "ArduinoJson.h")
    applemidi = library_dir("AppleMIDI", "AppleMIDI.h")
    midilib = library_dir("MIDI Library", "MIDI.h")
    print(f"compiler: {compiler}")
    failures = 0
    with tempfile.TemporaryDirectory(prefix="flowlocal-bridge-") as directory:
        work = Path(directory)
        # The bridge test: the header without its session specialisations.
        (work / "src").mkdir()
        (work / "src" / "fl_midi_bridge.h").write_text(bridge_without_sessions(), encoding="utf-8")
        for header in HEADERS:
            shutil.copyfile(header, work / "src" / header.name)
        for name in STUBS:
            (work / name).write_text(f"// {name}: supplied by {SHARED_STUBS.name}\n", encoding="utf-8")
        shutil.copyfile(DRIVER, work / DRIVER.name)
        shutil.copyfile(SHARED_STUBS, work / SHARED_STUBS.name)
        # The session test: the whole header, the real AppleMIDI library.
        session = work / "session"
        (session / "src").mkdir(parents=True)
        shutil.copyfile(BRIDGE, session / "src" / BRIDGE.name)
        for header in HEADERS:
            shutil.copyfile(header, session / "src" / header.name)
        for name in SESSION_STUBS:
            (session / name).write_text(f"// {name}: supplied by {SHARED_STUBS.name}\n", encoding="utf-8")
        shutil.copyfile(SESSION_DRIVER, session / SESSION_DRIVER.name)
        shutil.copyfile(SHARED_STUBS, session / SHARED_STUBS.name)

        for label, test in PURE_TESTS:
            shutil.copyfile(test, work / test.name)
            print(f"== {label}")
            if not build_and_run(compiler, env, work, [ajson], [], test, test.stem, test.name):
                failures += 1

        for index, (label, defines) in enumerate(VARIANTS):
            print(f"== {label} {' '.join(defines)}".rstrip())
            if not build_and_run(compiler, env, work, [ajson], defines, DRIVER, f"bridge_test_{index}", label):
                failures += 1

        print("== RTP sessions (the whole src/fl_midi_bridge.h in the real AppleMIDI 3.5.0)")
        if not build_and_run(compiler, env, session, [ajson, applemidi, midilib], [], SESSION_DRIVER,
                             "session_test", SESSION_DRIVER.name, packed_refs=True):
            failures += 1

        print("== compile-time guards")
        for defines, message in MUST_NOT_BUILD:
            built = subprocess.run(command(compiler, work, [ajson], defines, None), cwd=work, env=env,
                                   capture_output=True, text=True, errors="replace")
            text = built.stdout + built.stderr
            ok = built.returncode != 0 and message in text
            failures += not ok
            print(f"  [{'ok' if ok else 'FAIL'}] {' '.join(defines)} refused: \"{message}\"")
            if not ok:
                print(text)

    print("PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
