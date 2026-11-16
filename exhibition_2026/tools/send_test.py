#!/usr/bin/env python3
"""Send test MIDI (Program Change, CC sweeps, a panel-like demo) to an output port.

For testing the PF Exhibit Scenes device and the Ctrl-M mappings in Live
without the panel: make a loopMIDI port (e.g. "PF Test"), switch its INPUT
on in Live (Track + Remote), and send to it from here.

  python tools/send_test.py --list
  python tools/send_test.py --port "PF Test" pc 3
  python tools/send_test.py --port "PF Test" pc 3 --mirror    # + CC 118 ch16, as FlowLocal sends
  python tools/send_test.py --port "PF Test" pc 0 1 2 3 4 5 6 --interval 4
  python tools/send_test.py --port "PF Test" altcc 2          # CC 118 = 2 on ch16 (BF 76 02)
  python tools/send_test.py --port "PF Test" sweep --cc 24 25 26
  python tools/send_test.py --port "PF Test" demo --hold 8
  python tools/send_test.py --port "PF Test" raw C0 05
  python tools/send_test.py --dry-run demo           # print only, no port
  python tools/send_test.py --port "Patternflow Exhibit" --allow-device resync
                                                     # ask FlowLocal itself (Live closed)

Message shapes match what the exhibit panel sends (docs/midi-spec.md in the
patternflow clone): CC 24-26 = K1-K3 as an absolute 0-127 position, at most
one message per knob every 50 ms; Program Change n = pattern index n
(Origin 0, the six artists 1-6, boot and idle return on 1); notes 60-62 =
K1-K3 presses. K4 sends no CC and no note. Channel 1 unless --channel.

FlowLocal's two channel-16 messages ignore --channel:
  resync     Control Change 119 = 127 on channel 16 (BF 77 7F), the request
             that makes FlowLocal send the last Program Change again. The
             PF Exhibit Scenes device sends it on load and from its Resync
             button; FlowLocal discards every other message from the laptop.
  altcc N    Control Change 118 = N on channel 16 (BF 76 N), the program
             mirror FlowLocal sends right after every Program Change. The
             device's Alt CC reads it as Program Change N. "pc --mirror"
             sends it after each Program Change, the way FlowLocal does.

Ports that can reach hardware are refused unless --allow-device: names
containing "patternflow" or "flowlocal", and rtpMIDI session ports (named
after this computer by default) - an RTP session can reach a real panel.

Needs python-rtmidi (pip install python-rtmidi).
"""

import argparse
import socket
import sys
import time

try:
    import rtmidi
except ImportError:  # pragma: no cover - reported to the user
    print("python-rtmidi is not installed: pip install python-rtmidi")
    sys.exit(2)

PANEL_MIN_INTERVAL_S = 0.05   # the panel paces each knob to 20 msg/s

# FlowLocal's contract (flowlocal: src/fl_midi_bridge.h, EXHIBIT.md)
FLOWLOCAL_CHANNEL = 16
RESYNC_CC = 119               # laptop -> FlowLocal: value > 0 = send the last program again
RESYNC_VALUE = 127
PROGRAM_MIRROR_CC = 118       # FlowLocal -> laptop: value = program, after every Program Change


def looks_like_device(port_name):
    """True for ports that can reach hardware: anything named Patternflow, and
    rtpMIDI sessions, which Windows' rtpMIDI names after the computer by default."""
    low = port_name.lower()
    if "patternflow" in low or "flowlocal" in low:
        return True
    host = socket.gethostname().lower()
    return bool(host) and host in low


class Sender(object):
    def __init__(self, port_needle, channel, dry_run, allow_device):
        self.channel = channel - 1
        self.dry_run = dry_run
        self.t0 = time.perf_counter()
        self.count = 0
        self.out = None
        self.name = "(dry run)"
        if dry_run:
            return
        out = rtmidi.MidiOut()
        names = out.get_ports()
        low = port_needle.lower()
        matches = [(i, n) for i, n in enumerate(names) if low in n.lower()]
        if not matches:
            raise SystemExit("no MIDI output matches %r. Outputs: %s"
                             % (port_needle, ", ".join(names) or "(none)"))
        idx, name = matches[0]
        if len(matches) > 1:
            print("note: %d outputs match %r, using the first: %s"
                  % (len(matches), port_needle, ", ".join(n for _, n in matches)))
        if looks_like_device(name) and not allow_device:
            raise SystemExit("refusing to send to %r: it looks like a real device or an rtpMIDI "
                             "session (which can reach a panel). Use a loopMIDI port, or pass "
                             "--allow-device if you really mean it." % name)
        try:
            out.open_port(idx)
        except rtmidi.RtMidiError as e:
            raise SystemExit("could not open %r: %s" % (name, e))
        self.out = out
        self.name = name

    def send(self, msg, label):
        self.count += 1
        print("[%8.3fs] %-34s | %s" % (time.perf_counter() - self.t0, label,
                                       " ".join("%02X" % b for b in msg)))
        if self.out is not None:
            self.out.send_message(msg)

    def pc(self, program):
        self.send([0xC0 | self.channel, program & 0x7F], "PC %d (scene %d, top = 0)" % (program, program))

    def cc(self, number, value):
        self.send([0xB0 | self.channel, number & 0x7F, value & 0x7F], "CC %d = %d" % (number, value))

    def flowlocal_cc(self, number, value, label):
        """A Control Change on FlowLocal's channel 16, whatever --channel says."""
        self.send([0xB0 | (FLOWLOCAL_CHANNEL - 1), number & 0x7F, value & 0x7F], label)

    def resync(self):
        self.flowlocal_cc(RESYNC_CC, RESYNC_VALUE, "resync request (CC %d = %d ch%d)"
                          % (RESYNC_CC, RESYNC_VALUE, FLOWLOCAL_CHANNEL))

    def mirror(self, program):
        self.flowlocal_cc(PROGRAM_MIRROR_CC, program, "CC %d = %d ch%d (program mirror)"
                          % (PROGRAM_MIRROR_CC, program, FLOWLOCAL_CHANNEL))

    def note(self, number, velocity, length):
        self.send([0x90 | self.channel, number & 0x7F, velocity & 0x7F], "NoteOn %d vel %d" % (number, velocity))
        self.sleep(length)
        self.send([0x80 | self.channel, number & 0x7F, 0], "NoteOff %d" % number)

    def sleep(self, seconds):
        if not self.dry_run and seconds > 0:
            time.sleep(seconds)

    def close(self):
        if self.out is not None:
            time.sleep(0.05)   # let the last message leave before closing
            self.out.close_port()
        print("sent %d message(s) to %s" % (self.count, self.name))


def sweep_values(step):
    """64 -> 127 -> 0 -> 64, like a knob turned right, left, and back to centre."""
    vals = list(range(64, 128, step))
    if vals[-1] != 127:
        vals.append(127)
    down = list(range(127, -1, -step))
    if down[-1] != 0:
        down.append(0)
    vals += down[1:]
    back = list(range(0, 65, step))
    if back[-1] != 64:
        back.append(64)
    vals += back[1:]
    return vals


def run_sweep(sender, ccs, step, interval, cycles):
    for _ in range(cycles):
        for v in sweep_values(step):
            for c in ccs:
                sender.cc(c, v)
            sender.sleep(interval)


def parse_byte(text):
    t = text.strip().lower()
    if t.startswith("0x"):
        t = t[2:]
    value = int(t, 16)
    if not 0 <= value <= 255:
        raise argparse.ArgumentTypeError("byte out of range: %s" % text)
    return value


def int_range(lo, hi):
    def conv(text):
        v = int(text)
        if not lo <= v <= hi:
            raise argparse.ArgumentTypeError("%d is outside %d..%d" % (v, lo, hi))
        return v
    return conv


def main():
    ap = argparse.ArgumentParser(
        description="Send test Program Change / CC / notes to a MIDI output port (e.g. a loopMIDI port).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("\n\n", 1)[1])
    ap.add_argument("--list", action="store_true", help="list MIDI output ports and exit")
    ap.add_argument("--port", help="substring of the output port name, case-insensitive")
    ap.add_argument("--channel", type=int_range(1, 16), default=1, help="MIDI channel 1-16 (default 1)")
    ap.add_argument("--dry-run", action="store_true", help="print the messages without opening a port")
    ap.add_argument("--allow-device", action="store_true",
                    help="allow a port that looks like hardware or an rtpMIDI session")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("pc", help="send Program Change(s)")
    p.add_argument("programs", nargs="+", type=int_range(0, 127))
    p.add_argument("--interval", type=float, default=2.0, help="seconds between programs (default 2)")
    p.add_argument("--mirror", action="store_true",
                   help="follow each one with CC 118 = program on channel 16, as FlowLocal does")

    sub.add_parser("resync", help="send the resync request to FlowLocal: CC 119 = 127 on channel 16 (BF 77 7F)")

    p = sub.add_parser("altcc", help="send CC 118 = N on channel 16 (BF 76 N), FlowLocal's program mirror")
    p.add_argument("program", type=int_range(0, 127))

    p = sub.add_parser("cc", help="send one CC value")
    p.add_argument("number", type=int_range(0, 127))
    p.add_argument("value", type=int_range(0, 127))

    p = sub.add_parser("sweep", help="sweep CCs 64 -> 127 -> 0 -> 64")
    p.add_argument("--cc", type=int_range(0, 127), nargs="+", default=[24, 25, 26],
                   help="CC numbers (default 24 25 26 = K1-K3)")
    p.add_argument("--step", type=int_range(1, 127), default=2, help="value step (default 2)")
    p.add_argument("--interval", type=float, default=PANEL_MIN_INTERVAL_S,
                   help="seconds between values (default 0.05, the panel's pace)")
    p.add_argument("--cycles", type=int_range(1, 1000), default=1)

    p = sub.add_parser("note", help="send a note on/off (the exhibit panel's K1-K3 presses are 60-62)")
    p.add_argument("number", type=int_range(0, 127))
    p.add_argument("--velocity", type=int_range(1, 127), default=127)
    p.add_argument("--length", type=float, default=0.2)

    p = sub.add_parser("raw", help="send raw bytes in hex, e.g. raw C0 05")
    p.add_argument("bytes", nargs="+", type=parse_byte)

    p = sub.add_parser("demo", help="panel-like session: boot pattern, K4 through the rest with knob sweeps, wrap back")
    p.add_argument("--first", type=int_range(0, 127), default=1,
                   help="PC of the first exhibit pattern, which is also the boot/idle one (default 1; Origin is 0)")
    p.add_argument("--patterns", type=int_range(1, 128), default=6,
                   help="how many patterns K4 cycles through (default 6 artists)")
    p.add_argument("--hold", type=float, default=6.0, help="seconds on each pattern (default 6)")

    args = ap.parse_args()

    try:
        sys.stdout.reconfigure(errors="replace", line_buffering=True)
    except Exception:
        pass

    if args.list:
        mo = rtmidi.MidiOut()
        print("MIDI API: %s" % rtmidi.get_api_display_name(mo.get_current_api()))
        print("Outputs:")
        names = mo.get_ports()
        for i, n in enumerate(names):
            print("  [%d] %s" % (i, n))
        if not names:
            print("  (none)")
        return 0

    if not args.cmd:
        ap.print_usage()
        print("choose a command (pc, cc, sweep, note, raw, demo, resync, altcc), or --list")
        return 2
    if not args.port and not args.dry_run:
        print("--port is required (or --dry-run). See --list.")
        return 2

    sender = Sender(args.port or "", args.channel, args.dry_run, args.allow_device)
    if args.cmd in ("resync", "altcc"):
        print("sending to: %s, channel %d (fixed for %s)" % (sender.name, FLOWLOCAL_CHANNEL, args.cmd))
    else:
        print("sending to: %s, channel %d" % (sender.name, args.channel))
    try:
        if args.cmd == "pc":
            for i, prog in enumerate(args.programs):
                if i:
                    sender.sleep(args.interval)
                sender.pc(prog)
                if args.mirror:
                    sender.mirror(prog)
        elif args.cmd == "resync":
            sender.resync()
        elif args.cmd == "altcc":
            sender.mirror(args.program)
        elif args.cmd == "cc":
            sender.cc(args.number, args.value)
        elif args.cmd == "sweep":
            run_sweep(sender, args.cc, args.step, args.interval, args.cycles)
        elif args.cmd == "note":
            sender.note(args.number, args.velocity, args.length)
        elif args.cmd == "raw":
            if args.bytes[0] < 0x80:
                print("warning: first byte %02X is not a status byte; the driver may drop it" % args.bytes[0])
            sender.send(list(args.bytes), "raw")
        elif args.cmd == "demo":
            last = args.first + args.patterns - 1
            if last > 127:
                print("--first %d with --patterns %d goes past program 127" % (args.first, args.patterns))
                return 2
            progs = list(range(args.first, last + 1))
            sender.pc(progs[0])                       # the panel boots here
            sender.sleep(min(args.hold, 2.0))
            for prog in progs[1:] + progs[:1]:        # K4 turns, wrapping round
                sender.pc(prog)
                started = time.perf_counter()
                run_sweep(sender, [24, 25, 26], 8, PANEL_MIN_INTERVAL_S, 1)
                rest = args.hold - (time.perf_counter() - started)
                sender.sleep(rest)
    except KeyboardInterrupt:
        print("stopped")
    finally:
        sender.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
