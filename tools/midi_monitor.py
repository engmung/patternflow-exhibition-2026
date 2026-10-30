#!/usr/bin/env python3
"""Watch a MIDI input port: every message decoded, timestamped, with a running rate.

Made for the exhibition chain  panel -> FlowLocal -> USB -> laptop: it opens
the input whose name contains "Patternflow Exhibit" (or --port) and prints
what the panel sends - CC 24/25/26 (K1-K3), Program Change (pattern: Origin
0, artists 1-6), notes 60-62 (K1-K3 presses). The exhibit panel sends
nothing for K4 but the Program Change; CC 27 / note 63 are still labelled
in case another build is on the other end. Right after every Program Change
FlowLocal sends CC 118 on channel 16 with the same number (the program
mirror); CC 119 on channel 16 is the resync request the Live device sends
the other way, labelled for a port that carries it.

  python tools/midi_monitor.py --list
  python tools/midi_monitor.py                      # "Patternflow Exhibit"
  python tools/midi_monitor.py --port "PF Test"     # a loopMIDI port
  python tools/midi_monitor.py --seconds 30 --raw

Windows: a USB MIDI port can usually be open in ONE program at a time.
Close Ableton Live (or untick the port in Live's MIDI settings) before
running this, or it cannot open the port - and Live cannot while this runs.

Needs python-rtmidi (pip install python-rtmidi). Ctrl-C prints a summary.
"""

import argparse
import collections
import os
import sys
import time

try:
    import rtmidi
except ImportError:  # pragma: no cover - reported to the user
    print("python-rtmidi is not installed: pip install python-rtmidi")
    sys.exit(2)

DEFAULT_PORT = "Patternflow Exhibit"
RATE_WINDOW_S = 1.0
PORT_CHECK_S = 2.0
SILENCE_NOTE_S = 10.0

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    # 60 = C4, the convention docs/midi-spec.md uses (notes 60-63 = C4-D#4)
    return "%s%d" % (NOTE_NAMES[n % 12], n // 12 - 1)


def panel_hint_cc(cc, ch=1):
    if ch == 16 and cc == 118:
        return " (program mirror from FlowLocal)"
    if ch == 16 and cc == 119:
        return " (resync request to FlowLocal)"
    if 24 <= cc <= 27:
        return " (K%d)" % (cc - 23)
    if 20 <= cc <= 23:
        return " (K%d abs in)" % (cc - 19)
    return ""


def panel_hint_note(n):
    if 60 <= n <= 63:
        return " (button K%d)" % (n - 59)
    return ""


def decode(msg):
    """Return (kind, text) for one complete MIDI message (list of ints)."""
    if not msg:
        return "empty", "(empty)"
    st = msg[0]
    d1 = msg[1] if len(msg) > 1 else 0
    d2 = msg[2] if len(msg) > 2 else 0
    if st < 0x80:
        return "data", "stray data byte(s) %s" % hexbytes(msg)
    if st < 0xF0:
        hi = st & 0xF0
        ch = (st & 0x0F) + 1
        if hi == 0x90 and d2 > 0:
            return "note_on", "NoteOn   ch%-2d %3d %-4s vel %3d%s" % (ch, d1, note_name(d1), d2, panel_hint_note(d1))
        if hi == 0x80 or hi == 0x90:
            return "note_off", "NoteOff  ch%-2d %3d %-4s%s" % (ch, d1, note_name(d1), panel_hint_note(d1))
        if hi == 0xA0:
            return "poly_at", "PolyAT   ch%-2d %3d = %3d" % (ch, d1, d2)
        if hi == 0xB0:
            return "cc", "CC       ch%-2d #%-3d = %3d%s" % (ch, d1, d2, panel_hint_cc(d1, ch))
        if hi == 0xC0:
            return "pc", "PROGRAM  ch%-2d %3d   (pattern %d -> Live scene %d, top scene = 0)" % (ch, d1, d1, d1)
        if hi == 0xD0:
            return "chan_at", "ChanAT   ch%-2d %3d" % (ch, d1)
        if hi == 0xE0:
            return "pitch_bend", "PitchBnd ch%-2d %+5d" % (ch, ((d2 << 7) | d1) - 8192)
    names = {
        0xF0: "SysEx (%d bytes)" % len(msg), 0xF1: "MTC quarter frame", 0xF2: "Song position",
        0xF3: "Song select %d" % d1, 0xF6: "Tune request", 0xF8: "Clock", 0xFA: "Start",
        0xFB: "Continue", 0xFC: "Stop", 0xFE: "Active sensing", 0xFF: "Reset",
    }
    kind = "realtime" if st >= 0xF8 else "system"
    return kind, names.get(st, "System 0x%02X" % st)


def hexbytes(msg):
    return " ".join("%02X" % b for b in msg)


def list_ports():
    mi = rtmidi.MidiIn()
    mo = rtmidi.MidiOut()
    api = rtmidi.get_api_display_name(mi.get_current_api())
    print("MIDI API: %s" % api)
    print("Inputs:")
    ins = mi.get_ports()
    for i, name in enumerate(ins):
        print("  [%d] %s" % (i, name))
    if not ins:
        print("  (none)")
    print("Outputs:")
    outs = mo.get_ports()
    for i, name in enumerate(outs):
        print("  [%d] %s" % (i, name))
    if not outs:
        print("  (none)")
    del mi, mo


def find_port(names, needle):
    """First port whose name contains needle (case-insensitive) -> (index, name, all matches)."""
    low = needle.lower()
    matches = [(i, n) for i, n in enumerate(names) if low in n.lower()]
    if not matches:
        return None, None, []
    return matches[0][0], matches[0][1], matches


class Stats(object):
    def __init__(self):
        self.t0 = time.perf_counter()
        self.recent = collections.deque()
        self.kinds = collections.Counter()
        self.ccs = collections.Counter()
        self.programs = []
        self.total = 0
        self.peak_rate = 0.0
        self.last_t = None

    def add(self, now, kind, msg):
        self.total += 1
        self.kinds[kind] += 1
        if kind == "cc":
            self.ccs[msg[1]] += 1
        if kind == "pc":
            self.programs.append(msg[1])
        self.recent.append(now)
        while self.recent and now - self.recent[0] > RATE_WINDOW_S:
            self.recent.popleft()
        rate = len(self.recent) / RATE_WINDOW_S
        self.peak_rate = max(self.peak_rate, rate)
        gap = None if self.last_t is None else now - self.last_t
        self.last_t = now
        return rate, gap

    def summary(self):
        dur = time.perf_counter() - self.t0
        lines = ["", "---- %d messages in %.1f s, peak %.0f msg/s ----" % (self.total, dur, self.peak_rate)]
        for kind, n in sorted(self.kinds.items()):
            lines.append("  %-10s %d" % (kind, n))
        if self.ccs:
            lines.append("  CC numbers: " + ", ".join("#%d x%d" % (c, n) for c, n in sorted(self.ccs.items())))
        if self.programs:
            shown = self.programs[-20:]
            lines.append("  programs (last %d): %s" % (len(shown), " ".join(str(p) for p in shown)))
        return "\n".join(lines)


def open_input(needle, show_clock):
    mi = rtmidi.MidiIn()
    names = mi.get_ports()
    idx, name, matches = find_port(names, needle)
    if idx is None:
        del mi
        return None, None, names
    if len(matches) > 1:
        print("note: %d inputs match %r, using the first: %s" % (len(matches), needle, ", ".join(n for _, n in matches)))
    # ignore_types(sysex, timing, active_sense): True = drop
    mi.ignore_types(True, not show_clock, not show_clock)
    mi.open_port(idx)
    return mi, name, names


def main():
    ap = argparse.ArgumentParser(
        description="Print decoded MIDI from an input port with timestamps and a running rate.",
        epilog="Windows: close Ableton Live first - a USB MIDI input usually opens in one program at a time.")
    ap.add_argument("--list", action="store_true", help="list MIDI input and output ports and exit")
    ap.add_argument("--port", default=DEFAULT_PORT,
                    help="substring of the input port name, case-insensitive (default: %(default)r)")
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="stop after this many seconds (default: run until Ctrl-C)")
    ap.add_argument("--raw", action="store_true", help="also print the raw bytes in hex")
    ap.add_argument("--clock", action="store_true", help="show MIDI clock and active sensing (hidden by default)")
    ap.add_argument("--no-reconnect", action="store_true",
                    help="exit when the port disappears instead of waiting for it to come back")
    args = ap.parse_args()

    try:
        sys.stdout.reconfigure(errors="replace", line_buffering=True)
    except Exception:
        pass

    if args.list:
        list_ports()
        return 0

    stats = Stats()
    try:
        mi, name, names = open_input(args.port, args.clock)
    except rtmidi.RtMidiError as e:
        print("could not open a port matching %r: %s" % (args.port, e))
        print("Is Ableton Live (or another program) holding it? Close it and retry.")
        return 1
    if mi is None:
        print("no MIDI input matches %r. Inputs: %s" % (args.port, ", ".join(names) or "(none)"))
        if args.no_reconnect:
            return 1
        print("waiting for it to appear (Ctrl-C to stop)...")
    else:
        print("listening on: %s   (Ctrl-C to stop)" % name)

    deadline = time.perf_counter() + args.seconds if args.seconds > 0 else None
    next_port_check = time.perf_counter() + PORT_CHECK_S
    silence_from = time.perf_counter()
    silence_noted = False
    open_error_noted = False
    probe = rtmidi.MidiIn()
    # An input whose USB device vanished is never closed or destroyed: on
    # Windows, closing it crashes the process (measured 2026-09-19 on a
    # FlowLocal replug - the monitor died silently and missed the port
    # coming back). They are kept here and the process leaves via os._exit.
    vanished = []

    try:
        while True:
            now = time.perf_counter()
            if deadline is not None and now >= deadline:
                break

            got = False
            if mi is not None:
                while True:
                    item = mi.get_message()
                    if item is None:
                        break
                    got = True
                    msg, _delta = item
                    now = time.perf_counter()
                    kind, text = decode(msg)
                    rate, gap = stats.add(now, kind, msg)
                    gap_txt = "     " if gap is None else ("%4.0fms" % (gap * 1000) if gap < 10 else " >10s")
                    line = "%s [%9.3fs %s %5.1f/s] %s" % (
                        time.strftime("%H:%M:%S"), now - stats.t0, gap_txt, rate, text)
                    if args.raw:
                        line += "   | " + hexbytes(msg)
                    print(line)
                if got:
                    silence_from = now
                    silence_noted = False
                elif not silence_noted and now - silence_from >= SILENCE_NOTE_S:
                    print("%s ... nothing for %.0f s (port still open)" % (time.strftime("%H:%M:%S"), SILENCE_NOTE_S))
                    silence_noted = True

            if now >= next_port_check:
                next_port_check = now + (PORT_CHECK_S if mi is not None else 1.0)
                current = probe.get_ports()
                if mi is not None:
                    idx, _n, _m = find_port(current, args.port)
                    if idx is None:
                        print("%s !! port disappeared (%s)" % (time.strftime("%H:%M:%S"), name))
                        vanished.append(mi)
                        mi = None
                        open_error_noted = False
                        if args.no_reconnect:
                            return 1
                        print("waiting for it to come back...")
                else:
                    idx, _n, _m = find_port(current, args.port)
                    if idx is not None:
                        try:
                            mi, name, _names = open_input(args.port, args.clock)
                            if mi is not None:
                                print("%s .. port back, listening on: %s" % (time.strftime("%H:%M:%S"), name))
                                silence_from = time.perf_counter()
                                silence_noted = False
                        except rtmidi.RtMidiError as e:
                            if not open_error_noted:
                                print("port is listed but will not open (%s) - another program holding it?" % e)
                                open_error_noted = True

            if not got:
                time.sleep(0.001)
    except KeyboardInterrupt:
        pass
    finally:
        if mi is not None and not vanished:
            mi.close_port()
        print(stats.summary())
        if vanished:
            sys.stdout.flush()
            os._exit(0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
