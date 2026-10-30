#!/usr/bin/env python3
"""Offline tests for pf_monitor.py (stdlib unittest, no hardware, no MIDI port).

  python tools/monitor/test_pf_monitor.py
  python -m unittest discover -s tools/monitor -p "test_*.py"

Covers the PF monitor SysEx v1 build/parse (status replies and command acks,
including truncated and garbage ones, the reply as FlowLocal's firmware
writes it, flx-mon2's rsn / bc / why / ur, flx-mon3's command 13, chm=manual
and cs=), FlowLocal reboot (uptime drop) detection, the arrival clock and the
held-up knob detector (bundles are not bunching; two same-knob CCs 3 ms apart
are), the wireless rule and its settle time, the evidence written to the log
(counters, channel, joined-to-session time), the channel buttons, the one big
button's run as a state machine with injected clock, status, port and ack
events (every branch) and against the demo's pretend world, every diagnosis
rule and its grace period, logs/state.json (fresh, corrupt, remembered,
seeded from the logs), a stuck FlowLocal against old firmware, FlowLocal
restarting again and again, the fix buttons (rate limit, ack, recovery,
refusals, timeouts, one message per press), the powercfg parsing on this
laptop's real Korean answers (AC and DC: sleep, hibernate, lid, USB
selective suspend, PCIe ASPM), the one-click power settings file (read,
never run), the Live keeper against injected process lists, a fake
launcher and real Windows crash records (it never stops or starts Live),
tasklist / power-status parsing, the MIDI thread against a fake rtmidi
(sends nothing but the status request on its own, never closes a vanished
port), the demo scenarios and the HTTP server.
"""

import http.client
import json
import os
import re
import sys
import threading
import time
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pf_monitor as m  # noqa: E402


def reply_bytes(**over):
    f = dict(m.DEMO_OK_FIELDS)
    f["up"] = 100
    f.update(over)
    return m.build_status_reply(f)


def parse(msg):
    raw = m.parse_status_reply(msg)
    return None if raw is None else m.interpret_status(raw)


def sysex(kind, text):
    return [0xF0, 0x7D, 0x50, 0x46, kind] + list(text.encode("ascii")) + [0xF7]


# A reply exactly as fl_pf_monitor.h formatStatus() writes one for a FlowLocal
# in its boot silence (unknown values empty or -1) and one with a panel.
FW_BOOTING = ("v=1;fw=exhibit-mon1;up=4;ap=0;ch=;chm=auto;cl=0;cm=3;ps=0;pip=;prs=;hd=-1;ra=-1;lp=-1;"
              "kn=-,-,-;jt=-1;jm=-1;rx=0;tx=0;dr=0;se=0;to=0;rj=0;gp=0;rr=0;ud=0;sr=2;hi=201532")
FW_RUNNING = ("v=1;fw=exhibit-mon1;up=5400;ap=1;ch=1;chm=auto;cl=1;cm=3;ps=1;pip=192.168.66.2;prs=-61;"
              "hd=812;ra=4410;lp=3;kn=64,-,40;jt=4;jm=37;rx=5234;tx=5230;dr=0;se=2;to=0;rj=0;gp=3;rr=4;"
              "ud=0;sr=2700;hi=182344")


class SysExTests(unittest.TestCase):
    def test_requests_are_exactly_the_contract(self):
        self.assertEqual(m.build_status_request(), [0xF0, 0x7D, 0x50, 0x46, 0x01, 0xF7])
        self.assertEqual(m.RESYNC_REQUEST, [0xBF, 0x77, 0x7F])
        self.assertEqual(m.build_command(0x10), [0xF0, 0x7D, 0x50, 0x46, 0x10, 0x52, 0x43, 0xF7])
        self.assertEqual(m.build_command(0x11), [0xF0, 0x7D, 0x50, 0x46, 0x11, 0x52, 0x50, 0xF7])
        self.assertEqual(m.build_command(0x12), [0xF0, 0x7D, 0x50, 0x46, 0x12, 0x52, 0x46, 0xF7])
        self.assertEqual({k: a["msg"] for k, a in m.ACTIONS.items()}, {
            "resync": [0xBF, 0x77, 0x7F],
            "reconnect": m.build_command(0x10),
            "reboot_panel": m.build_command(0x11),
            "reboot_flowlocal": m.build_command(0x12),
            "channel_next": None,                                   # the number comes from the status at the press
            "channel_auto": [0xF0, 0x7D, 0x50, 0x46, 0x13, 0x43, 0x48, 0x00, 0xF7],
        })

    def test_channel_command_is_the_flx_mon3_contract(self):
        # F0 7D 50 46 13 'C' 'H' <arg> F7: 0 automatic, 1..13 that channel, 7F the next of 1/6/11
        self.assertEqual(m.build_channel_command(11), [0xF0, 0x7D, 0x50, 0x46, 0x13, 0x43, 0x48, 0x0B, 0xF7])
        self.assertEqual(m.build_channel_command(0x00)[-2], 0x00)
        self.assertEqual(m.build_channel_command(0x7F)[-2], 0x7F)
        self.assertEqual(m.build_command(0x13, 6), m.build_channel_command(6))
        for bad in (-1, 14, 0x7E, 200):
            with self.assertRaises(ValueError):
                m.build_channel_command(bad)
        self.assertEqual([m.next_channel(c) for c in (1, 6, 11, 3, 13, 14, None)], [6, 11, 1, 6, 1, 1, None])
        # every message the monitor can write stays inside FlowLocal's 8-byte SysEx buffer (RX_MAX)
        self.assertLessEqual(len(m.build_channel_command(13)) - 2, 8)

    def test_full_reply(self):
        st = parse(reply_bytes())
        self.assertEqual(st["v"], 1)
        self.assertEqual(st["fw"], "exhibit-mon1")
        self.assertEqual(st["up"], 100)
        self.assertIs(st["ap"], True)
        self.assertEqual((st["ch"], st["chm"]), (1, "auto"))
        self.assertEqual((st["cl"], st["cm"], st["ps"]), (2, 3, 1))
        self.assertEqual(st["pip"], "192.168.66.2")
        self.assertEqual(st["prs"], -58)
        self.assertEqual((st["hd"], st["ra"]), (800, 1200))
        self.assertEqual(st["lp"], 3)
        self.assertEqual(st["kn"], [64, 80, 40])
        self.assertEqual((st["jt"], st["jm"]), (3, 18))
        for k in m.COUNTER_KEYS:
            self.assertIsInstance(st[k], int, k)
        self.assertEqual(st["extra"], {})

    def test_replies_as_the_firmware_writes_them(self):
        st = parse(sysex(0x02, FW_BOOTING))
        self.assertIs(st["ap"], False)
        self.assertIsNone(st["ch"])
        self.assertEqual((st["cl"], st["ps"], st["hd"], st["ra"]), (0, 0, -1, -1))
        self.assertIsNone(st["pip"])
        self.assertIsNone(st["prs"])
        self.assertIsNone(st["lp"])
        self.assertEqual(st["kn"], [None, None, None])
        self.assertIsNone(st["jt"])
        self.assertIsNone(st["jm"])
        st = parse(sysex(0x02, FW_RUNNING))
        self.assertEqual((st["ps"], st["pip"], st["prs"], st["kn"]), (1, "192.168.66.2", -61, [64, None, 40]))
        self.assertEqual((st["jt"], st["jm"], st["gp"], st["hi"]), (4, 37, 3, 182344))
        self.assertLessEqual(len(FW_RUNNING), m.MAX_TEXT)

    def test_builder_drops_whole_fields_past_240_bytes_like_the_firmware(self):
        msg = reply_bytes(up=4294967, rx=4294967295, tx=4294967295, dr=4294967295, se=4294967295,
                          to=4294967295, rj=4294967295, gp=4294967295, rr=4294967295, ud=4294967295,
                          sr=4294967295, hi=4294967295, pip="192.168.166.254", prs=-100,
                          hd=4294967295, ra=4294967295)
        text = bytes(msg[5:-1]).decode("ascii")
        self.assertLessEqual(len(text), m.MAX_TEXT)
        self.assertTrue(all("=" in part for part in text.split(";")))
        st = parse(msg)
        self.assertEqual(st["v"], 1)                 # the first fields are always there
        self.assertEqual(st["rx"], 4294967295)
        self.assertIsNone(st["hi"])                  # the last ones were left out
        self.assertIsNone(st["sr"])

    def test_unknown_keys_are_kept_aside_and_ignored(self):
        st = parse(reply_bytes(zz="7", v2x="hello"))
        self.assertEqual(st["extra"], {"zz": "7", "v2x": "hello"})
        self.assertEqual(st["cl"], 2)

    def test_empty_and_missing_values(self):
        st = parse(m.build_status_reply({"v": 1, "pip": "", "prs": "", "hd": -1, "ra": -1, "lp": -1,
                                         "kn": [None, 5, None]}))
        self.assertIsNone(st["pip"])
        self.assertIsNone(st["prs"])
        self.assertEqual(st["hd"], -1)
        self.assertIsNone(st["lp"])
        self.assertEqual(st["kn"], [None, 5, None])
        self.assertIsNone(st["cl"])      # key not sent at all
        self.assertIsNone(st["up"])

    def test_empty_reply_is_a_reply(self):
        self.assertEqual(m.parse_status_reply([0xF0, 0x7D, 0x50, 0x46, 0x02, 0xF7]), {})

    def test_truncated_replies(self):
        full = reply_bytes()
        self.assertIsNone(m.parse_status_reply(full[:-1]))          # no F7: abandoned reply
        self.assertIsNone(m.parse_status_reply(full[:20]))
        self.assertIsNone(m.parse_status_reply(full[:5]))
        self.assertIsNone(m.parse_status_reply([]))
        self.assertIsNone(m.parse_status_reply(None))
        self.assertIsNone(m.parse_status_reply([0xF0, 0xF7]))

    def test_other_sysex(self):
        body = list(b"v=1;cl=2") + [0xF7]
        self.assertIsNone(m.parse_status_reply([0xF0, 0x7E, 0x50, 0x46, 0x02] + body))   # manufacturer
        self.assertIsNone(m.parse_status_reply([0xF0, 0x7D, 0x50, 0x47, 0x02] + body))   # signature
        self.assertIsNone(m.parse_status_reply([0xF0, 0x7D, 0x50, 0x46, 0x03] + body))   # an ack
        self.assertIsNone(m.parse_status_reply(m.build_status_request()))                # our own request
        self.assertIsNone(m.parse_status_reply([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7]))     # identity request

    def test_bytes_outside_printable_ascii(self):
        good = reply_bytes()
        for bad in (0x80, 0x7F, 0x1F, 0x00, 0x0A):
            msg = good[:10] + [bad] + good[10:]
            self.assertIsNone(m.parse_status_reply(msg), hex(bad))

    def test_glued_abandoned_start_uses_the_last_reply(self):
        abandoned = [0xF0, 0x7D, 0x50, 0x46, 0x02] + list(b"v=1;up=5;c")
        st = parse(abandoned + reply_bytes(cl=1))
        self.assertEqual(st["cl"], 1)

    def test_too_long_is_garbage(self):
        msg = [0xF0, 0x7D, 0x50, 0x46, 0x02] + [0x41] * (m.MAX_TEXT_READ + 1) + [0xF7]
        self.assertIsNone(m.parse_status_reply(msg))

    def test_garbage_values(self):
        raw = m.parse_status_reply([0xF0, 0x7D, 0x50, 0x46, 0x02] + list(
            b"v=x;up=abc;ap=2;ch=15;chm=maybe;cl=-1;cm=999;ps=1.5;pip=999.1.1.1;prs=5;hd=-5;ra=;lp=200;kn=1,2;"
            b"fw=a b;jt=x;jm=-7;rx=12e3") + [0xF7])
        st = m.interpret_status(raw)
        for k in ("v", "up", "ap", "ch", "chm", "cl", "cm", "ps", "pip", "prs", "hd", "ra", "lp", "kn", "fw",
                  "jt", "jm", "rx"):
            self.assertIsNone(st[k], k)
        self.assertEqual(m._parse_knobs("1,x,300"), [1, None, None])
        self.assertEqual(m._parse_knobs("-,-,-"), [None, None, None])

    def test_messy_separators(self):
        raw = m.parse_status_reply([0xF0, 0x7D, 0x50, 0x46, 0x02] + list(b";;junk;=x; cl = 2 ;v=1;") + [0xF7])
        self.assertEqual(raw, {"cl": "2", "v": "1"})
        self.assertEqual(m.interpret_status(raw)["cl"], 2)

    def test_flx_mon3_keys(self):
        # chm may be auto | fixed | manual; cs= the boot scan's scores, read leniently
        st = parse(reply_bytes(fw="flx-mon3", ch=11, chm="manual", cs="1:210,6:95,11:380"))
        self.assertEqual((st["ch"], st["chm"], st["cs"]), (11, "manual", [[1, 210], [6, 95], [11, 380]]))
        self.assertEqual(st["extra"], {})
        for text, want in (("", None), ("1:5", [[1, 5]]), ("1:x,6:40,junk,11:", [[6, 40]]), ("0:5,15:3", None),
                           ("6:-4", None), (" 1 : 7 , 6:8", [[1, 7], [6, 8]])):
            self.assertEqual(m._parse_scores(text), want, text)
        self.assertIsNone(parse(reply_bytes(chm="sometimes"))["chm"])
        self.assertEqual(m.scores_text([[1, 210], [6, 95]]), "1번 210 · 6번 95")
        # a key flx-mon3 may add (e.g. evicted sessions) under a name this monitor does not know: kept aside
        st = parse(reply_bytes(ev="3", xs="a,b"))
        self.assertEqual(st["extra"], {"ev": "3", "xs": "a,b"})

    def test_channel_command_support_from_the_reply(self):
        def support(**over):
            raw = m.parse_status_reply(reply_bytes(**over))
            return m.channel_command_support(m.interpret_status(raw), raw)
        self.assertIs(support(fw="flx-mon2"), False)
        self.assertIs(support(fw="flx-mon1"), False)
        self.assertIs(support(fw="flx-mon3"), True)
        self.assertIs(support(fw="flx-mon12"), True)
        self.assertIs(support(fw="flx-mon2", chm="manual"), True)       # only command 13 sets it
        self.assertIs(support(fw="flx-mon2", cs=""), True)              # only flx-mon3 sends the key
        self.assertIsNone(support(fw="exhibit-mon1"))                   # the tag says nothing
        self.assertIsNone(m.channel_command_support(None))


class AckTests(unittest.TestCase):
    def ack(self, text):
        raw = m.parse_ack(sysex(0x03, text))
        return None if raw is None else m.interpret_ack(raw)

    def test_acks_as_the_firmware_writes_them(self):
        self.assertEqual(self.ack("cmd=10;ok=1;msg=ended 1 RTP session, the panel invites again"),
                         {"cmd": 0x10, "ok": True, "msg": "ended 1 RTP session, the panel invites again"})
        self.assertEqual(self.ack("cmd=11;ok=0;msg=no panel session and 2 devices on the hotspot")["ok"], False)
        self.assertEqual(self.ack("cmd=12;ok=1;msg=restarting: USB back in seconds, hotspot in about 13 s")["cmd"], 0x12)
        self.assertEqual(self.ack("cmd=10;ok=0;msg=")["msg"], "")

    def test_builder_matches(self):
        self.assertEqual(m.build_ack(0x11, False, "no connection to panel 192.168.66.2"),
                         sysex(0x03, "cmd=11;ok=0;msg=no connection to panel 192.168.66.2"))

    def test_channel_acks(self):
        # flx-mon3's command 13 is acked like the others (its words are not known yet)
        a = self.ack("cmd=13;ok=1;msg=channel 11 saved, restarting")
        self.assertEqual((a["cmd"], a["ok"]), (0x13, True))
        self.assertEqual(m.ack_reason("channel 11 saved, restarting")[0], "채널을 바꾸고 다시 켜져요")
        self.assertEqual(m.ack_reason("automatic channel saved, restarting")[0], "채널을 자동으로 되돌리고 다시 켜져요")
        self.assertEqual(m.ack_reason("bad channel 14")[1], "bad_channel")

    def test_bad_acks(self):
        for text in ("cmd=14;ok=1;msg=", "cmd=1G;ok=1", "cmd=10;ok=2", "cmd=10", "ok=1", ""):
            a = self.ack(text)
            self.assertTrue(a["cmd"] is None or a["ok"] is None, text)
        self.assertIsNone(m.parse_ack(sysex(0x03, "cmd=10;ok=1")[:-1]))       # abandoned: no F7
        self.assertIsNone(m.parse_ack(sysex(0x02, "cmd=10;ok=1")))            # a status reply
        self.assertIsNone(m.parse_ack(sysex(0x03, "cmd=10;ok=1;msg=caf\x7f")))

    def test_every_firmware_message_has_korean(self):
        for msg, kind in (("ended 2 RTP sessions, the panel invites again", None),
                          ("restarting: USB back in seconds, hotspot in about 13 s", None),
                          ("panel 192.168.66.3 is rebooting", None),
                          ("no RTP session to end", "no_session"),
                          ("no RTP yet: the hotspot is still starting", "starting"),
                          ("too soon: once per 3 s, wait 2 s", "too_soon"),
                          ("busy: the last reboot request is still running", "busy"),
                          ("no panel session and the hotspot is not up", "starting"),
                          ("no panel session and no device on the hotspot", "absent"),
                          ("no panel session and 2 devices on the hotspot", "ambiguous"),
                          ("no connection to panel 192.168.66.2", "unreachable"),
                          ("no HTTP answer from panel 192.168.66.2", "unreachable"),
                          ("panel 192.168.66.2 answered HTTP 404", "unreachable"),
                          ("could not start the request (no memory)", None)):
            ko, k = m.ack_reason(msg)
            self.assertTrue(ko, msg)
            self.assertEqual(k, kind, msg)
            self.assertIn(msg, m.ack_text(msg))
        self.assertEqual(m.ack_reason("something new"), (None, None))
        self.assertEqual(m.ack_text("something new"), "something new")


class BootTrackerTests(unittest.TestCase):
    def test_steady_uptime_is_not_a_reboot(self):
        b = m.BootTracker()
        self.assertFalse(b.feed(100, 1000.0))
        for i in range(1, 200):
            # integer seconds and reply jitter move the implied boot by up to ~1 s
            self.assertFalse(b.feed(100 + 2 * i, 1000.0 + 2 * i + (0.9 if i % 3 else 0.0)))

    def test_uptime_drop(self):
        b = m.BootTracker()
        b.feed(5000, 1000.0)
        self.assertTrue(b.feed(3, 1010.0))
        self.assertFalse(b.feed(5, 1012.0))

    def test_gone_longer_than_it_was_up(self):
        # up=10 at t=1000, gone 60 s, back with up=50: "up" grew, but it rebooted
        b = m.BootTracker()
        b.feed(10, 1000.0)
        self.assertTrue(b.feed(50, 1070.0))

    def test_slow_drift_over_days_is_not_a_reboot(self):
        b = m.BootTracker()
        t, up = 0.0, 0.0
        b.feed(0, t)
        for _ in range(3 * 86400 // 2):
            t += 2.0
            up += 2.0 * (1 + 40e-6)        # FlowLocal's crystal 40 ppm fast
            self.assertFalse(b.feed(int(up), t))

    def test_missing_uptime(self):
        self.assertFalse(m.BootTracker().feed(None, 5.0))


class ArrivalTests(unittest.TestCase):
    def test_chains_driver_deltas_onto_the_read_time(self):
        c = m.ArrivalClock()
        self.assertEqual(c.feed(0.0, 10.000), 10.000)
        # four messages 2 ms apart, all read in one pass 8 ms after the first
        self.assertAlmostEqual(c.feed(0.002, 10.008), 10.002)
        self.assertAlmostEqual(c.feed(0.002, 10.008), 10.004)
        self.assertAlmostEqual(c.feed(0.002, 10.008), 10.006)

    def test_reanchors_after_a_pause_and_when_ahead(self):
        c = m.ArrivalClock()
        c.feed(0.0, 10.0)
        self.assertEqual(c.feed(3.0, 13.0), 13.0)             # longer than PAUSE_S
        self.assertEqual(c.feed(0.5, 13.01), 13.01)           # would run 0.49 s ahead of the read

    def test_broken_deltas_are_given_up(self):
        c = m.ArrivalClock()
        t = 10.0
        c.feed(0.0, t)
        for _ in range(m.ArrivalClock.BROKEN_AFTER):
            t += 0.05
            c.feed(0.0, t)                                    # zero deltas across passes
        self.assertFalse(c.trusted)
        self.assertEqual(c.feed(0.002, t + 0.05), t + 0.05)

    def test_a_stall_now_and_then_is_forgotten(self):
        # one zero delta across passes every few minutes (this thread held up
        # once), real deltas in between: the driver's times stay in use
        c = m.ArrivalClock()
        t = 10.0
        c.feed(0.0, t)
        for _ in range(4 * m.ArrivalClock.BROKEN_AFTER):
            t += 0.05
            c.feed(0.0, t)
            for _ in range(20):
                t += 0.07
                c.feed(0.07, t)
            t += 100.0
            c.feed(0.9, t)                                    # a pause: re-anchored
        self.assertTrue(c.trusted)


def knob(n, value):
    """K1..K3 = CC 24..26 on channel 1."""
    return [0xB0, 23 + n, value]


class BunchTests(unittest.TestCase):
    """KnobBunchDetector through the Tracker, as the MIDI thread feeds it:
    two CCs of the same knob closer than 12 ms were held on the way (the
    panel paces each knob to one per 50 ms); bundles never are."""

    def setUp(self):
        self.clk = m.FakeClock(100.0)
        self.tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True)

    def play(self, timed):
        for t, msg in timed:
            self.tr.on_midi(list(msg), t, t)
        self.clk.t = max(t for t, _ in timed) + 0.5
        return self.tr.snapshot()["arrivals"]

    def turning(self, n, start, count, pace=0.050, value=40, jitter=()):
        """Knob n turned steadily: one CC per `pace` (the panel's pacing), values rising."""
        return [(start + i * pace + (jitter[i % len(jitter)] if jitter else 0.0), knob(n, value + i)) for i in range(count)]

    def test_a_session_join_bundle_is_not_bunching(self):
        # on every session join the panel re-sends PROGRAM, FlowLocal adds its CC 118 mirror,
        # then CC 24/25/26 - in one go, after a silence
        hello = [(100.300, [0xC0, 3]), (100.3004, [0xBF, 118, 3]), (100.301, knob(1, 64)),
                 (100.3015, knob(2, 80)), (100.302, knob(3, 40))]
        a = self.play(self.turning(1, 100.0, 5) + hello + [(101.0 + d, msg) for d, msg in
                                                           ((0.0, [0xC0, 4]), (0.0004, [0xBF, 118, 4]),
                                                            (0.001, knob(1, 70)), (0.0012, knob(2, 70)),
                                                            (0.0014, knob(3, 70)))])
        self.assertEqual((a["bunches_seen_60s"], a["bunches_total"]), (0, 0))
        self.assertFalse(any(p[2] for p in a["points"]))

    def test_a_resync_answer_is_not_bunching(self):
        # a visitor turns K1 (one CC per 50 ms) when FlowLocal answers a resync: PC, CC 118 and its
        # cached K1-K3 land 2 ms after the panel's K1 - the cache repeats that K1's value - and the
        # panel's next K1 comes 3 ms after the bundle
        turn = self.turning(1, 100.0, 6)                  # K1 = 40..45, the last at 100.25
        answer = [(100.252, [0xC0, 5]), (100.2522, [0xBF, 118, 5]), (100.2525, knob(1, 45)),
                  (100.2527, knob(2, 64)), (100.253, knob(3, 64))]
        a = self.play(turn + answer + [(100.256, knob(1, 46)), (100.306, knob(1, 47))])
        self.assertEqual(a["bunches_total"], 0)
        # a USB re-mount re-send is the same bundle (PC then K1-K3), and so is the panel's hello
        # repeating a value: nothing
        a = self.play([(101.0, [0xC0, 5]), (101.0003, knob(1, 47)), (101.0005, knob(2, 64)), (101.0007, knob(3, 64))])
        self.assertEqual(a["bunches_total"], 0)

    def test_fast_turning_of_one_knob_at_50_ms_pacing_is_not_bunching(self):
        jitter = (0.0, 0.004, -0.003, 0.006, -0.005, 0.002)   # Wi-Fi's usual few ms
        a = self.play(self.turning(1, 100.0, 60, jitter=jitter))
        self.assertEqual(a["bunches_total"], 0)
        # all three knobs at once, each at its own 50 ms pace, 17 ms apart from each other
        three = self.turning(1, 104.0, 40) + self.turning(2, 104.017, 40) + self.turning(3, 104.034, 40)
        a = self.play(sorted(three))
        self.assertEqual(a["bunches_total"], 0)
        # footsteps: note on / off pairs together, and other CCs, never count
        notes = [(106.0 + i * 0.2 + d, msg) for i in range(10) for d, msg in
                 ((0.0, [0x99, 36, 100]), (0.001, [0x89, 36, 0]), (0.002, [0xB1, 7, i]), (0.003, [0xB1, 7, i + 1]))]
        a = self.play(notes)
        self.assertEqual(a["bunches_total"], 0)

    def test_two_same_knob_ccs_3_ms_apart_after_a_300_ms_gap_is_bunching(self):
        a = self.play(self.turning(1, 100.0, 3) + [(100.400, knob(1, 60)), (100.403, knob(1, 61))])
        self.assertEqual((a["bunches_seen_60s"], a["bunches_total"]), (1, 1))
        lb = a["last_bunch"]
        self.assertEqual((lb["size"], lb["gap_ms"], lb["knobs"]), (2, 300, "K1"))
        self.assertEqual([p[2] for p in a["points"]], [0, 0, 0, 1, 1])      # only those two are red

    def test_a_longer_hold_is_one_bunch(self):
        # K1 and K2 turning; the network holds 400 ms of them and lets six go within 8 ms
        held = [(100.600, knob(1, 50)), (100.6015, knob(2, 50)), (100.603, knob(1, 51)), (100.6045, knob(2, 51)),
                (100.606, knob(1, 52)), (100.608, knob(2, 52))]
        a = self.play(self.turning(1, 100.0, 4) + self.turning(2, 100.02, 4) + held + self.turning(1, 100.7, 3, value=60))
        self.assertEqual(a["bunches_total"], 1)
        self.assertEqual((a["last_bunch"]["size"], a["last_bunch"]["knobs"]), (6, "K1·K2"))
        self.assertEqual(sum(p[2] for p in a["points"]), 6)

    def test_counted_from_the_settle_time(self):
        d = m.KnobBunchDetector()
        for t in (10.0, 10.003, 30.0, 30.004):
            d.feed(t, "knob", 0, int(t * 1000) % 127)
        self.assertEqual(d.count(40.0), 2)
        self.assertEqual(d.count(40.0, since=20.0), 1)
        self.assertEqual(d.count(200.0), 0)                  # out of the last minute


class Clocked(object):
    def setUp(self):
        self.clk = m.FakeClock(1000.0)
        self.tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True)

    def adv(self, s):
        self.clk.t += s

    def port_up(self):
        self.tr.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, self.clk.t)
        self.tr.on_opened(True, True, None, self.clk.t)

    def system_ok(self):
        self.tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t)
        self.tr.on_power(True, 90, True, self.clk.t)
        self.tr.on_power_settings(m.demo_power_items(), self.clk.t)

    def reply(self, up=None, **over):
        # unless given, FlowLocal's last panel MIDI (ra=) is the laptop's last one
        if "ra" not in over:
            over["ra"] = -1 if self.tr.midi_at is None else int((self.clk.t - self.tr.midi_at) * 1000)
        self.tr.on_request_sent(self.clk.t)
        self.tr.on_midi(reply_bytes(up=up if up is not None else int(self.clk.t), **over), self.clk.t)

    def ack(self, cmd, ok, msg=""):
        self.tr.on_midi(m.build_ack(cmd, ok, msg), self.clk.t)

    def snap(self):
        return self.tr.snapshot()

    def diag(self, cfg=None):
        return m.diagnose(self.tr.snapshot(), cfg or m.CONFIG_DEFAULTS)

    def action(self, key):
        return m.action_state(self.tr.snapshot(), key)

    def events(self, kind=None):
        return [e["text"] for e in self.tr.events.recent(200) if kind is None or e["kind"] == kind]

    def healthy(self, settle=0.0):
        self.system_ok()
        self.port_up()
        self.adv(1)
        self.reply()
        if settle:
            steps = int(settle // 2)
            for _ in range(steps):
                self.adv(2)
                self.reply()


class TrackerTests(Clocked, unittest.TestCase):
    def test_panel_messages(self):
        self.port_up()
        self.tr.on_midi([0xC0, 4], self.clk.t)
        self.tr.on_midi([0xBF, 118, 4], self.clk.t)     # FlowLocal's mirror: not panel activity
        self.tr.on_midi([0xB0, 24, 10], self.clk.t)
        self.tr.on_midi([0xB0, 26, 127], self.clk.t)
        self.tr.on_midi([0xB1, 25, 99], self.clk.t)     # CC 25 on channel 2: not K2
        self.tr.on_midi([0x99, 36, 100], self.clk.t)    # footstep
        self.tr.on_midi([0x89, 36, 0], self.clk.t)
        self.tr.on_midi([0xF8], self.clk.t)             # clock (filtered by rtmidi anyway)
        s = self.snap()
        self.assertEqual(s["midi"]["pattern"], 4)
        self.assertEqual(s["midi"]["pattern_name"], "그림 4")
        self.assertEqual(s["midi"]["knobs"], [10, None, 127])
        self.assertEqual(s["midi"]["notes"], 1)
        self.assertEqual(s["midi"]["count"], 6)
        self.assertEqual(s["midi"]["last_text"], "발소리 노트 36")
        self.assertEqual(self.tr.pc_count, 1)

    def test_pattern_names(self):
        self.assertEqual([m.pattern_name(i) for i in range(7)], ["그림 %d" % i for i in range(7)])
        self.assertEqual(m.pattern_name(9), "그림 9")
        self.assertIsNone(m.pattern_name(None))

    def test_pattern_and_knobs_fall_back_to_status(self):
        self.port_up()
        self.reply(lp=5, kn=[1, None, 3])
        s = self.snap()
        self.assertEqual((s["midi"]["pattern"], s["midi"]["pattern_src"]), (5, "status"))
        self.assertEqual(s["midi"]["knobs"], [1, None, 3])
        self.tr.on_midi([0xC0, 2], self.clk.t)
        self.assertEqual(self.snap()["midi"]["pattern"], 2)

    def test_session_up_and_down(self):
        self.port_up()
        self.reply(ps=0, cl=0, pip="", prs="")
        self.assertFalse(self.snap()["session"]["up"])
        self.assertIsNone(self.snap()["session"]["down_age"])   # unknown when it went down
        self.adv(2); self.reply(ps=1)
        self.assertTrue(self.snap()["session"]["up"])
        self.adv(2); self.reply(ps=0, cl=0)
        self.adv(3)
        self.assertEqual(self.snap()["session"]["down_age"], 3.0)
        texts = self.events("session")
        self.assertIn("패널 연결이 끊겼어요", texts)
        self.assertTrue(any(t.startswith("패널이 연결됐어요 (192.168.66.2, -58 dBm)") for t in texts))

    def test_reboot_event_from_the_uptime_drop(self):
        self.port_up()
        self.reply(up=4000)
        self.adv(2); self.reply(up=4002)
        self.adv(30); self.reply(up=10)
        self.assertEqual(len(self.events("flowlocal")), 1)
        self.assertEqual(self.snap()["fl_reboot_age"], 10.0)
        self.assertEqual(m.build_view(self.snap())["uptime"]["note"], "10초 전에 다시 켜졌어요")

    def test_port_loss_resets_the_reply_state(self):
        self.port_up()
        self.reply()
        self.assertTrue(self.snap()["status_ever"])
        self.adv(1)
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        s = self.snap()
        self.assertFalse(s["status_ever"])
        self.assertFalse(s["port"]["in_open"])
        self.assertIsNone(s["status_age"])
        self.assertIn("FlowLocal 보드가 노트북에서 사라졌어요 (USB 빠짐 또는 보드 재시작)", self.events("port"))

    def test_old_firmware_and_lost_replies_are_noted_once(self):
        self.port_up()
        for _ in range(6):
            self.adv(2); self.tr.on_request_sent(self.clk.t); self.tr.check_stale(self.clk.t)
        self.assertEqual(sum("예전 펌웨어" in t for t in self.events("status")), 1)
        self.reply()
        for _ in range(6):
            self.adv(2); self.tr.check_stale(self.clk.t)
        self.assertEqual(self.events("status").count("FlowLocal 상태 응답이 끊겼어요"), 1)
        self.reply()
        self.assertIn("FlowLocal이 다시 대답해요", self.events("status"))

    def test_malformed_pf_sysex_is_counted(self):
        self.port_up()
        self.tr.on_midi(reply_bytes()[:-1] + [0x80, 0xF7], self.clk.t)
        self.tr.on_midi(m.build_status_request(), self.clk.t)   # an echo of a request is not a bad reply
        self.tr.on_midi(m.build_command(0x10), self.clk.t)      # nor of a command
        self.tr.on_midi([0xF0, 0x7E, 0x7F, 0x06, 0x02, 0xF7], self.clk.t)
        self.tr.on_midi(sysex(0x03, "cmd=99;ok=1"), self.clk.t)
        s = self.snap()
        self.assertEqual((s["replies"], s["bad_replies"]), (0, 1))
        self.assertEqual((s["acks"]["count"], s["acks"]["bad"], s["acks"]["ok_ever"]), (1, 1, False))

    def test_power_events(self):
        self.tr.on_power(True, 90, True, self.clk.t)
        self.tr.on_power(False, 88, True, self.clk.t)
        self.tr.on_power(True, 88, True, self.clk.t)
        self.assertEqual(self.events("power"), [
            "노트북 충전기가 연결됐어요",
            "노트북 충전기가 빠졌어요 (배터리 88%) - 소리는 계속 나요. 충전기를 다시 꽂아 주세요"])
        levels = [e["level"] for e in self.tr.events.recent(10) if e["kind"] == "power"]
        self.assertEqual(levels, ["ok", "warn"])                  # calm: yellow, not red

    def test_battery_running_low_is_noted_once(self):
        self.tr.on_power(True, 90, True, self.clk.t)
        self.tr.on_power(False, 35, True, self.clk.t)
        self.tr.on_power(False, 29, True, self.clk.t)
        self.tr.on_power(False, 28, True, self.clk.t)
        texts = self.events("power")
        self.assertEqual(sum("29%까지 내려갔어요" in t for t in texts), 1)
        self.assertFalse(any("28%" in t for t in texts))
        self.tr.on_power(True, 28, True, self.clk.t)
        self.assertIn("노트북 충전기가 연결됐어요", self.events("power"))

    def test_live_events(self):
        self.tr.on_live(False, [], self.clk.t)
        self.tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t)
        self.tr.on_live(False, [], self.clk.t)
        self.assertEqual(self.events("live"), ["음악 프로그램(Ableton Live)이 꺼졌어요",
                                               "음악 프로그램(Ableton Live)이 켜져 있어요",
                                               "음악 프로그램(Ableton Live)이 아직 안 보여요"])

    def test_event_log_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            log = m.EventLog(d)
            log.add("port", "테스트 한 줄", "bad")
            with open(log.today_path(), encoding="utf-8") as f:
                line = f.read()
        self.assertRegex(line, r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \[port\] \(문제\) 테스트 한 줄\n$")


# ------------------------------------------------ diagnosis: every rule

class DiagnosisTests(Clocked, unittest.TestCase):
    def test_all_ok(self):
        self.healthy()
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["button"]), ("ALL_OK", "ok", None))
        self.assertEqual({c["state"] for c in d["chain"][:6]}, {"ok"})

    def test_no_midi_library(self):
        self.tr.midi_lib = False
        self.system_ok()
        d = self.diag()
        self.assertEqual(d["code"], "MONITOR_NO_MIDI")
        self.assertEqual(d["others"], [])
        self.assertTrue(all(not a["enabled"] for a in m.action_states(self.snap())))

    def test_checking_then_old_firmware(self):
        self.system_ok()
        self.port_up()
        self.adv(2); self.tr.on_request_sent(self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["wait"]), ("CHECKING", True))
        self.adv(2); self.tr.on_request_sent(self.clk.t)
        self.adv(2.5); self.tr.on_request_sent(self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_OLD_FIRMWARE", "warn"))
        self.assertIn("아직 패널 신호가 없어요", d["detail"])
        self.tr.on_midi([0xB0, 25, 70], self.clk.t)
        d = self.diag()
        self.assertIn("마지막 패널 신호: 방금", d["detail"])
        self.assertEqual(d["chain"][0]["state"], "ok")      # the panel's MIDI arrives
        self.assertEqual(d["chain"][2]["text"], "예전 펌웨어")

    def test_old_firmware_buttons(self):
        self.system_ok(); self.port_up()
        for _ in range(4):
            self.adv(2); self.tr.on_request_sent(self.clk.t)
        states = {a["id"]: a for a in m.action_states(self.snap())}
        self.assertTrue(states["resync"]["enabled"])
        for k in ("reconnect", "reboot_panel", "reboot_flowlocal"):
            self.assertFalse(states[k]["enabled"], k)
            self.assertTrue(states[k]["needs_fw"], k)
            self.assertEqual(states[k]["reason"], "새 FlowLocal 펌웨어가 필요해요")

    def test_old_firmware_does_not_hide_a_red_problem(self):
        self.system_ok()
        self.port_up()
        for _ in range(4):
            self.adv(2); self.tr.on_request_sent(self.clk.t)
        self.tr.on_power(False, 20, True, self.clk.t)             # on battery and low: red
        d = self.diag()
        self.assertEqual(d["code"], "ON_BATTERY")
        self.assertEqual([o["code"] for o in d["others"]], ["FL_OLD_FIRMWARE"])

    def test_port_missing_right_away_at_start_and_grace_after_a_blip(self):
        self.system_ok()
        self.assertEqual(self.diag()["code"], "CHECKING")          # before the first port check
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("PORT_MISSING", "bad"))
        joined = " ".join(d["steps"])
        for words in ("테이프", "뽑았다가", "노트북을 다시 시작"):
            self.assertIn(words, joined)
        self.adv(1); self.port_up(); self.adv(10); self.reply()
        self.adv(1); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(3)
        self.assertEqual(self.diag()["code"], "PORT_RECONNECTING")
        self.adv(3.5)
        self.assertEqual(self.diag()["code"], "PORT_MISSING")

    def test_port_busy(self):
        self.system_ok()
        self.tr.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, self.clk.t)
        self.tr.on_opened(False, True, "입력 error", self.clk.t)
        d = self.diag()
        self.assertEqual(d["code"], "PORT_BUSY")
        self.assertIn("입력 error", d["detail"])

    def assert_stopped_steps(self, d):
        """FlowLocal stopped answering: RST (not BOOT), then a replug at the taped port, then the contact."""
        self.assertEqual((d["code"], d["level"], d["button"]), ("FL_NO_REPLY", "bad", None))
        self.assertEqual(d["headline"], "FlowLocal이 대답을 멈췄어요")
        self.assertEqual(len(d["steps"]), 3)
        self.assertIn("RST 버튼을 한 번 누르세요", d["steps"][0])
        self.assertIn("BOOT 버튼은 누르지 않아요", d["steps"][0])
        self.assertIn("뽑았다가 5초 뒤 같은 테이프 자리에", d["steps"][1])
        self.assertIn("담당자", d["steps"][2])
        joined = " ".join([d["headline"], d["detail"]] + d["steps"])
        self.assertNotIn("예전 펌웨어로 보여요", joined)
        self.assertNotIn("펌웨어가 필요", joined)
        self.assertNotIn("[", joined)                             # no screen button: they cannot reach it

    def test_replies_stop(self):
        self.healthy()
        self.adv(7)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["wait"]), ("FL_NO_REPLY", "warn", True))
        self.assertTrue(self.action("reboot_flowlocal")["enabled"])      # a blip: the buttons stay
        self.adv(24)
        d = self.diag()
        self.assert_stopped_steps(d)
        self.assertIn("31초째 아무 대답이 없어요", d["detail"])
        self.assertEqual(d["chain"][2]["state"], "bad")
        self.reply()
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_replies_stop_after_buttons_worked(self):
        # the buttons worked earlier, but they go over the USB that stopped answering
        self.healthy()
        self.ack(0x10, True, "ended 1 RTP session, the panel invites again")
        self.adv(31)
        d = self.diag()
        self.assert_stopped_steps(d)
        for a in m.action_states(self.snap(), d["button"]):
            self.assertEqual((a["enabled"], a["reason"], a["needs_fw"]),
                             (False, "작은 보드가 대답을 멈춰서 버튼이 닿지 않아요", False), a["id"])
        note = m.buttons_note(self.snap())
        self.assertEqual(note["level"], "bad")
        self.assertIn("RST", note["text"])

    def test_starting_then_ap_down(self):
        self.system_ok(); self.port_up()
        self.reply(up=5, ap=0, cl=0, ps=0)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["wait"]), ("FL_STARTING", "warn", True))
        self.adv(20); self.reply(up=25, ap=1, cl=0, ps=0)
        self.assertEqual(self.diag()["code"], "FL_STARTING")       # hotspot up, the panel on its way
        self.adv(20); self.reply(up=45, ap=0, cl=0, ps=0)
        d = self.diag()
        # the main screen points at the one button; the operator's single button is still named
        self.assertEqual((d["code"], d["level"], d["button"], d["op_button"]),
                         ("FL_AP_DOWN", "bad", "reconnect_all", "reboot_flowlocal"))
        self.assertIn("[한 번에 다시 연결하기]", d["steps"][0])

    def test_hotspot_full(self):
        self.system_ok(); self.port_up()
        self.reply(cl=3, cm=3, ps=0)
        d = self.diag()
        self.assertEqual(d["code"], "HOTSPOT_FULL")
        self.assertIn("3/3", d["headline"])
        self.assertIn("Wi-Fi를 꺼요", d["steps"][0])
        self.assertEqual(d["chain"][1]["state"], "bad")

    def test_full_hotspot_with_the_panel_on_it_is_fine(self):
        self.system_ok(); self.port_up()
        self.reply(cl=3, cm=3, ps=1)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_session_drop_grace_then_not_on_hotspot(self):
        self.healthy()
        self.adv(2); self.reply(ps=0, cl=0)
        self.assertEqual(self.diag()["code"], "PANEL_RECONNECTING")
        for _ in range(16):
            self.adv(2); self.reply(ps=0, cl=0)
        d = self.diag()
        self.assertEqual((d["code"], d["button"], d["op_button"]), ("PANEL_NOT_ON_HOTSPOT", "reconnect_all", None))
        self.assertIn("전원", d["steps"][0])
        self.assertIn("[한 번에 다시 연결하기]", d["steps"][1])       # after the power, not before
        # the reboot command cannot reach a panel that is not on the hotspot
        a = self.action("reboot_panel")
        self.assertEqual((a["enabled"], a["reason"]), (False, "패널이 핫스팟에 없어서 닿지 않아요"))

    def test_stations_without_session(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=1)
        self.assertEqual(self.diag()["code"], "PANEL_JOINING")
        for _ in range(12):
            self.adv(2); self.reply(ps=0, cl=1)
        self.assertEqual(self.diag()["code"], "PANEL_JOINING")     # 24 s: still the panel's first invitation
        for _ in range(18):
            self.adv(2); self.reply(ps=0, cl=1)
        d = self.diag()
        self.assertEqual((d["code"], d["button"], d["op_button"]), ("PANEL_NO_SESSION", "reconnect_all", "reboot_panel"))
        self.assertIn("1분째", d["detail"])
        self.assertIn("전원", d["steps"][0])                     # a panel that is off cannot take the command
        self.assertIn("그림이 보이면 화면의 [한 번에 다시 연결하기]", d["steps"][1])
        # no session: nothing for "reconnect" to end, FlowLocal would say so
        a = self.action("reconnect")
        self.assertEqual((a["enabled"], a["reason"]), (False, "지금은 끊을 패널 연결이 없어요"))
        self.assertTrue(next(x for x in m.action_states(self.snap(), d["op_button"])
                             if x["id"] == "reboot_panel")["recommended"])

    def test_panel_switched_off(self):
        # measured order: the panel goes quiet, FlowLocal times the session out
        # after ~60 s, but the soft AP keeps the dead station for minutes
        self.healthy()
        off = self.clk.t
        for _ in range(20):
            self.adv(2); self.reply(hd=int((self.clk.t - off) * 1000))
        self.assertEqual(self.diag()["code"], "PANEL_SILENT")
        self.adv(2); self.reply(ps=0, cl=1, to=1, pip="", prs="", hd=-1)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["button"], d["op_button"]),
                         ("PANEL_NO_SESSION", "bad", "reconnect_all", "reboot_panel"))
        self.assertIn("전원", d["headline"])
        self.assertIn("전원", d["steps"][0])
        self.assertIn("그림이 보이면", d["steps"][1])
        self.assertIn("패널 소식이 먼저 끊기고", d["detail"])
        self.assertEqual(d["chain"][0]["state"], "bad")
        # the dead station finally expires; a device that joins after that is joining
        for _ in range(3):
            self.adv(2); self.reply(ps=0, cl=0, to=1)
        self.adv(2); self.reply(ps=0, cl=1, to=1)
        self.assertEqual(self.diag()["code"], "PANEL_JOINING")

    def test_session_ended_by_the_panel_is_joining(self):
        # a session that ends while the panel was heard (it re-invites, or reconnect
        # was pressed on a healthy session): the usual wait
        self.healthy()
        self.adv(2); self.reply(ps=0, cl=1, to=0)
        self.assertEqual(self.diag()["code"], "PANEL_JOINING")
        self.assertFalse(self.snap()["session"]["drop_silent"])

    def test_two_stations_without_session(self):
        self.system_ok(); self.port_up()
        for _ in range(20):
            self.adv(2); self.reply(ps=0, cl=2)
        d = self.diag()
        self.assertEqual((d["code"], d["button"], d["op_button"]), ("PANEL_NO_SESSION", "reconnect_all", None))
        self.assertIn("폰·노트북의 Wi-Fi를 꺼요", d["steps"][1])
        self.assertIn("1분 기다려요. 그래도 안 붙으면 화면의 [한 번에 다시 연결하기]", d["steps"][2])
        a = self.action("reboot_panel")
        self.assertEqual((a["enabled"], a["reason"]), (False, "핫스팟 기기가 2대라 어느 것이 패널인지 몰라요"))

    def test_heard(self):
        self.healthy()
        self.reply(hd=30000)
        self.assertEqual(self.diag()["code"], "ALL_OK")
        self.reply(hd=30001)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["button"], d["op_button"]),
                         ("PANEL_SILENT", "bad", "reconnect_all", "reconnect"))
        self.assertIn("전원", d["steps"][0])
        self.assertIn("그림이 보이면 화면의 [한 번에 다시 연결하기]", d["steps"][1])
        self.assertIn("패널 전원을 끄고", d["steps"][2])
        self.reply(hd=-1)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_usb_loss(self):
        self.healthy()
        self.adv(40)
        self.reply(ra=500)     # FlowLocal got panel MIDI 0.5 s ago, the laptop none since the port opened
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("USB_LOSS", "warn"))
        self.assertEqual(d["chain"][3]["text"], "신호가 안 들어옴")
        self.tr.on_midi([0xB0, 24, 3], self.clk.t)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_live_grace_then_not_running(self):
        self.tr.on_power(True, 90, True, self.clk.t)
        self.port_up(); self.reply()
        self.tr.on_live(False, [], self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["wait"]), ("LIVE_STARTING", True))
        self.adv(m.LIVE_START_GRACE_S)
        self.reply()
        self.assertEqual(self.diag()["code"], "LIVE_NOT_RUNNING")

    def test_live_closing_after_it_ran_is_red_at_once(self):
        self.healthy()
        self.tr.on_live(False, [], self.clk.t)
        self.assertEqual((self.diag()["code"], self.diag()["level"]), ("LIVE_NOT_RUNNING", "bad"))

    def test_live_check_unknown_is_not_an_alarm(self):
        self.healthy()
        self.tr.live = None
        d = self.diag()
        self.assertEqual(d["code"], "ALL_OK")
        self.assertEqual(d["chain"][5]["state"], "unknown")

    def test_battery(self):
        # the show goes on on battery: yellow and calm, red only when it runs low
        self.healthy()
        self.tr.on_power(False, 30, True, self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("ON_BATTERY", "warn"))
        self.assertIn("30%", d["headline"])
        self.assertIn("소리는 계속 나요. 충전기를 다시 꽂아 주세요", d["headline"])
        self.assertIn("충전기", d["steps"][0])
        self.assertEqual(d["chain"][4]["state"], "warn")
        self.tr.on_power(False, 29, True, self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("ON_BATTERY", "bad"))
        self.assertIn("29%", d["headline"])
        self.assertEqual(d["chain"][4]["state"], "bad")

    def test_battery_with_sleep_on_battery_says_so(self):
        self.healthy()
        self.tr.on_power_settings(m.demo_power_items({"hibernate": (0, 18000)}), self.clk.t)
        self.tr.on_power(False, 70, True, self.clk.t)
        d = self.diag()
        self.assertEqual(d["code"], "ON_BATTERY")
        self.assertIn("배터리일 때 절전 설정이 켜져 있어서(최대 절전)", d["detail"])
        self.assertEqual([o["code"] for o in d["others"]], ["POWER_SETTINGS"])

    def wifi(self, **over):
        self.healthy(settle=m.JUMP_SETTLE_S + 2)
        self.reply(**over)
        return self.diag()

    def test_wireless_thresholds(self):
        base = dict(prs=-75, jt=m.JITTER_WARN_MS - 1, jm=m.JUMP_WARN_MS - 1)
        self.assertEqual(self.wifi(**base)["code"], "ALL_OK")
        for key, over in (("jt", dict(jt=m.JITTER_WARN_MS)), ("jm", dict(jm=m.JUMP_WARN_MS))):
            with self.subTest(key):
                self.setUp()
                d = self.wifi(**dict(base, **over))
                self.assertEqual((d["code"], d["level"]), ("WIFI_POOR", "warn"))
                self.assertEqual(m.wireless_problems(self.snap(), self.tr.status)[0][0], key)
                self.assertEqual(d["chain"][1]["state"], "warn")
                joined = " ".join(d["steps"])
                self.assertIn("핫스팟", d["steps"][0])
                # a firmware that may not know command 13: the one button (the channel is picked again at start)
                self.assertIn("[한 번에 다시 연결하기]", d["steps"][1])
                self.assertIn("가장 조용한 무선 채널", d["steps"][1])
                # moving FlowLocal is for the operator: staff do not unplug it (STAFF_CARD.md)
                self.assertIn("담당자에게", d["steps"][-1])
                for words in ("USB 연장선", "파란색(USB 3.0)"):
                    self.assertIn(words, d["steps"][-1])
                self.assertNotIn("뽑", joined)

    def test_a_weak_signal_alone_is_not_poor_wireless(self):
        # nothing came late: the show is fine, however weak the signal looks
        d = self.wifi(prs=-82)
        self.assertEqual(d["code"], "ALL_OK")
        self.assertEqual(m.build_view(self.snap())["wireless"]["word"], "약함")    # the card still says so
        self.reply(prs=-82, jt=m.JITTER_WARN_MS + 5)
        d = self.diag()
        self.assertEqual(d["code"], "WIFI_POOR")
        self.assertEqual([k for k, _t in m.wireless_problems(self.snap(), self.tr.status)], ["jt", "prs"])
        self.assertIn("신호 세기 -82 dBm", d["detail"])

    def test_packet_gaps_per_minute(self):
        self.healthy(settle=10)
        gp = 3
        for _ in range(31):
            self.adv(2); gp += 0.06; self.reply(gp=int(gp))
        self.assertEqual(self.diag()["code"], "ALL_OK")           # under 2 a minute
        for _ in range(10):
            self.adv(2); gp += 0.5; self.reply(gp=int(gp))
        d = self.diag()
        self.assertEqual(d["code"], "WIFI_POOR")
        self.assertIn("빠진 패킷", d["detail"])

    def bunches(self, n=m.BUNCHES_WARN_PER_MIN, ago=30.0):
        """n held-up knob messages in the last minute: K1 paused 300 ms, then two CCs 3 ms apart."""
        t = self.clk.t - ago
        for i in range(n):
            g = t + i * 5
            for tt, v in ((g, 10 + i), (g + 0.3, 30 + i), (g + 0.303, 50 + i)):
                self.tr.on_midi(knob(1, v), tt, tt)

    def test_bunching_makes_the_wireless_poor(self):
        self.healthy(settle=m.WIFI_SETTLE_S + 40)
        self.tr.on_arrival_source("rtmidi")
        self.bunches(m.BUNCHES_WARN_PER_MIN - 1)
        self.assertEqual(self.diag()["code"], "ALL_OK")
        self.bunches(1, ago=10.0)
        d = self.diag()
        self.assertEqual(d["code"], "WIFI_POOR")
        self.assertIn("같은 노브 신호가 몰려 옴 1분에 %d번" % m.BUNCHES_WARN_PER_MIN, d["detail"])

    def test_bunching_timed_by_read_times_needs_a_flowlocal_number_too(self):
        # read times also catch this monitor's own stalls
        self.healthy(settle=m.JUMP_SETTLE_S + 2)
        self.tr.on_arrival_source("read")
        self.bunches()
        self.assertEqual(self.diag()["code"], "ALL_OK")
        self.reply(jt=m.JITTER_WARN_MS)
        d = self.diag()
        self.assertEqual(d["code"], "WIFI_POOR")
        self.assertIn("같은 노브 신호가 몰려 옴", d["detail"])

    def test_what_happens_while_a_session_settles_does_not_count(self):
        # held-up messages and lost packets in the 20 s after a session came up are not held against
        # the wireless when those 20 s are over (2026-09-26 00:24:13: 21 s after the session)
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=1, gp=10)
        self.adv(2); self.reply(ps=1, gp=10)
        self.tr.on_arrival_source("rtmidi")
        for i in range(4):
            self.adv(3)
            for tt, v in ((self.clk.t, 10 + i), (self.clk.t + 0.3, 30 + i), (self.clk.t + 0.303, 50 + i)):
                self.tr.on_midi(knob(1, v), tt, tt)
            self.reply(ps=1, gp=10 + 2 * (i + 1))
        for _ in range(5):
            self.adv(2); self.reply(ps=1, gp=18)
        s = self.snap()
        self.assertGreaterEqual(s["session"]["up_age"], m.WIFI_SETTLE_S)
        self.assertEqual((s["arrivals"]["bunches_seen_60s"], s["arrivals"]["bunches_60s"]), (4, 0))
        self.assertEqual(s["wireless"]["gp_min"], 0)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_the_bundles_after_a_connection_are_not_bunching(self):
        # 2026-09-26 00:22:54-00:24:13: right after connecting, a person turned the pattern knob
        # and the knobs: PC bundles, FlowLocal's resends, footsteps - never WIFI_POOR
        self.healthy(settle=m.WIFI_SETTLE_S + 2)
        self.tr.on_arrival_source("rtmidi")
        t = self.clk.t
        for i in range(40):                                    # a new pattern every ~0.8 s for 30 s
            g = t + i * 0.8
            for d, msg in ((0.0, [0xC0, i % 6 + 1]), (0.0003, [0xBF, 118, i % 6 + 1]), (0.0008, knob(1, 64)),
                           (0.001, knob(2, 64)), (0.0012, knob(3, 64)), (0.3, [0x99, 36, 90]), (0.301, [0x89, 36, 0])):
                self.tr.on_midi(msg, g + d, g + d)
            for k in range(6):                                  # and K1 turned in between, at its pace
                tt = g + 0.4 + k * 0.05
                self.tr.on_midi(knob(1, 70 + k), tt, tt)
        self.clk.t = t + 33
        self.reply()
        self.assertEqual(self.snap()["arrivals"]["bunches_total"], 0)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_wireless_numbers_settle_after_a_session_starts(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=1)
        self.adv(2); self.reply(ps=1, prs=-80, jt=40, jm=900)
        self.assertEqual(self.diag()["code"], "ALL_OK")           # just connected
        for _ in range(11):
            self.adv(2); self.reply(ps=1, prs=-80, jt=40, jm=900)
        d = self.diag()
        self.assertEqual(d["code"], "WIFI_POOR")
        self.assertNotIn("늦음", d["detail"])                     # jm still covers the session's start

    def test_fixed_channel_step(self):
        for chm in ("fixed", "manual"):
            with self.subTest(chm):
                self.setUp()
                d = self.wifi(jt=40, chm=chm)
                self.assertIn("무선 채널이 고정돼 있어요", d["steps"][-1])
                self.assertNotIn("[한 번에 다시 연결하기]", " ".join(d["steps"]))   # a restart keeps the channel

    def test_poor_wireless_offers_the_channel_button_after_the_phones(self):
        d = self.wifi(jt=40, fw="flx-mon3", ch=6, chm="auto", cs="1:210,6:95,11:380")
        self.assertEqual((d["code"], d["button"], d["channel_button"]), ("WIFI_POOR", "channel_next", "channel_next"))
        self.assertIn("폰 핫스팟", d["steps"][0])
        self.assertIn("[무선 채널 바꾸기]", d["steps"][1])
        self.assertIn("1·6·11 중 다음", d["steps"][1])
        a = self.action("channel_next")
        self.assertEqual((a["enabled"], a["label_full"]), (True, "무선 채널 바꾸기 (지금 6번 → 다음 11번)"))
        # flx-mon2 does not know command 13: no channel button on the main screen, the one button instead
        self.setUp()
        d = self.wifi(jt=40, fw="flx-mon2", ch=6, chm="auto")
        self.assertEqual((d["button"], d["channel_button"]), ("reconnect_all", None))
        # healthy wireless: no channel button on the main screen
        self.setUp()
        self.assertIsNone(self.wifi(fw="flx-mon3", ch=6)["channel_button"])

    def test_power_settings_on_the_charger_and_on_battery(self):
        self.healthy()
        self.tr.on_power_settings(m.demo_power_items(("lid",)), self.clk.t)
        d = self.diag()
        self.assertEqual(d["code"], "POWER_SETTINGS")       # the lid counts now: a closed lid must not stop the show
        self.assertIn("덮개를 닫을 때 = 절전 (전원 연결·배터리)", d["detail"])
        self.assertIn("노트북 전시용 전원 설정.bat", d["steps"][-1])
        self.assertIn("관리자 권한은 필요 없어요", d["steps"][-1])
        self.tr.on_power_settings(m.demo_power_items({"usb_suspend": (1, 0)}), self.clk.t)
        self.assertIn("USB 선택적 절전 = 사용 (전원 연결)", self.diag()["detail"])
        # wrong on battery only: the charger may be pulled out
        self.tr.on_power_settings(m.demo_power_items(m.LAPTOP_20260925_POWER), self.clk.t)
        d = self.diag()
        self.assertEqual(d["code"], "POWER_SETTINGS")
        for words in ("최대 절전 = 5시간 후 (배터리)", "PCI Express 링크 상태 전원 관리 = 최대 절전 (전원 연결·배터리)"):
            self.assertIn(words, d["detail"])
        self.assertEqual(d["chain"][4]["state"], "warn")
        # a computer without a battery: only the charger side counts
        self.tr.on_power(True, None, False, self.clk.t)
        self.tr.on_power_settings(m.demo_power_items({"hibernate": (0, 18000)}), self.clk.t)
        self.assertEqual(self.diag()["code"], "ALL_OK")
        self.tr.on_power(True, 90, True, self.clk.t)
        # unreadable (no lid, no ASPM setting) is not a problem
        self.tr.on_power_settings([m.power_item(c, None) for c in m.POWER_CHECKS], self.clk.t)
        self.assertEqual(self.diag()["code"], "ALL_OK")
        self.tr.on_power_settings(m.demo_power_items(), self.clk.t)
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_power_setting_events_name_the_side(self):
        self.healthy()
        self.tr.on_power_settings(m.demo_power_items({"hibernate": (0, 18000)}), self.clk.t)
        self.tr.on_power_settings(m.demo_power_items({"hibernate": (0, 18000)}), self.clk.t)   # same: no new line
        self.tr.on_power_settings(m.demo_power_items({"hibernate": (1800, 18000)}), self.clk.t)
        texts = self.events("power")                              # newest first
        self.assertEqual(texts, ["노트북 설정: 최대 절전 = 30분 후 (전원 연결), 최대 절전 = 5시간 후 (배터리) - 전시에는 '안 함'",
                                 "노트북 설정: 최대 절전 = 5시간 후 (배터리) - 전시에는 '안 함'"])

    def test_contact_and_staff_card_blanks(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=0)
        plain = self.diag()
        self.assertIn("그래도 안 되면 담당자에게 연락해 주세요.", " ".join(plain["steps"]))
        cfg = dict(m.CONFIG_DEFAULTS, contacts=[{"role": "담당자", "name": "홍길동", "phone": "010-0000-0000"}],
                   panel_power_place="책상 왼쪽 멀티탭", live_recovery_button="No")
        d = self.diag(cfg)
        joined = " ".join(d["steps"])
        self.assertIn("(담당자 홍길동 010-0000-0000)", joined)
        self.assertIn("'패널 전원 켜는 곳'(책상 왼쪽 멀티탭)", joined)
        self.tr.on_live(False, [], self.clk.t)
        d = self.diag(cfg)
        self.assertIn("'No' 버튼", " ".join(s for o in [d] + d["others"] for s in o["steps"]))

    def test_primary_is_most_severe_then_earliest_rule(self):
        self.healthy(settle=m.JUMP_SETTLE_S + 2)
        self.reply(jt=40)
        self.tr.on_power(False, 70, True, self.clk.t)
        self.tr.on_live(False, [], self.clk.t)
        d = self.diag()
        self.assertEqual(d["code"], "LIVE_NOT_RUNNING")
        self.assertEqual([o["code"] for o in d["others"]], ["ON_BATTERY", "WIFI_POOR"])

    def test_every_step_names_only_real_buttons(self):
        labels = {a["label"] for a in m.ACTIONS.values()} | {m.RECONNECT_LABEL}
        singles = {a["label"] for a in m.ACTIONS.values()} - {m.ACTIONS["channel_next"]["label"]}
        for key in m.SCENARIOS:
            tr, clk = m.run_scenario(key)
            snap = tr.snapshot(clk.t)
            d = m.diagnose(snap, m.CONFIG_DEFAULTS)
            main = d["steps"] + [s for o in d["others"] for s in o["steps"]]
            texts = list(main)
            fix = snap["fix"]
            if fix and fix["next"]:
                texts.append(fix["next"])
            if snap["reconnect"] and snap["reconnect"]["action"]:
                texts.append(snap["reconnect"]["action"])
            for t in texts:
                for name in re.findall(r"\[([^\]]+)\]", t):
                    self.assertIn(name, labels, (key, t))
            # the main screen points at the one button (and the channel button); the single
            # buttons live in the operator section
            for t in main:
                for name in re.findall(r"\[([^\]]+)\]", t):
                    self.assertNotIn(name, singles, (key, t))
            if d["button"]:
                self.assertIn("[%s]" % m.button_label(d["button"]), " ".join(d["steps"]), key)


# ------------------------------------------------ the fix buttons

class FixTests(Clocked, unittest.TestCase):
    def press(self, action):
        r = self.tr.fix_begin(action, self.clk.t)
        if r is None:
            self.adv(0.01)
            self.tr.fix_sent(True, self.clk.t)
        return r

    def fix(self):
        return self.snap()["fix"]

    def no_session(self, seconds):
        for _ in range(int(seconds // 2)):
            self.adv(2); self.reply(ps=0, cl=1)

    def silent_session(self):
        self.healthy()
        self.adv(2); self.reply(hd=45000)
        self.assertEqual(self.diag()["code"], "PANEL_SILENT")

    def test_rate_limit_and_busy(self):
        self.healthy()
        self.assertIsNone(self.press("resync"))
        self.assertEqual(self.tr.fix_begin("resync", self.clk.t)[0], "busy")   # waiting for the answer
        self.adv(2)
        self.assertEqual(self.fix()["result"], "no_answer")
        self.assertEqual(self.tr.fix_begin("resync", self.clk.t)[0], "too_soon")
        self.assertIn("초 뒤에", self.tr.fix_begin("resync", self.clk.t)[1])
        self.adv(m.ACTIONS["resync"]["gap_s"])
        self.assertIsNone(self.press("resync"))
        a = self.action("resync")
        self.assertEqual((a["enabled"], a["running"], a["reason"]), (False, True, "진행 중이에요"))
        self.assertEqual(self.action("reboot_flowlocal")["reason"], "앞에 누른 버튼을 기다리는 중이에요")

    def test_resync_answered(self):
        self.healthy()
        self.press("resync")
        self.adv(0.01)
        self.tr.on_midi([0xC0, 5], self.clk.t)
        f = self.fix()
        self.assertEqual((f["phase"], f["result"], f["level"]), ("done", "answered", "ok"))
        self.assertIn("패턴 5번을", f["message"])

    def test_reconnect_ok(self):
        self.silent_session()
        self.press("reconnect")
        self.assertEqual(self.fix()["phase"], "ack")
        self.assertEqual(self.diag()["code"], "FIX_WAIT")
        self.adv(0.05); self.ack(0x10, True, "ended 1 RTP session, the panel invites again")
        f = self.fix()
        self.assertEqual((f["phase"], f["message"]), ("recover", "다시 연결 중… 보통 10초 안에 붙어요"))
        self.assertIn("패널과의 연결을 끊었어요", f["ack"]["text"])
        d = self.diag()
        self.assertEqual((d["code"], d["headline"]), ("FIX_WAIT", "다시 연결 중… 보통 10초 안에 붙어요"))
        self.adv(2); self.reply(ps=0, cl=1)
        self.assertEqual(self.diag()["code"], "FIX_WAIT")         # the drop is expected, not a new alarm
        self.adv(3); self.reply(ps=1, se=3)
        f = self.fix()
        self.assertEqual((f["result"], f["level"]), ("ok", "ok"))
        self.assertIn("5초 걸림", f["message"])
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_reconnect_back_without_a_visible_drop(self):
        # the panel re-invited between two polls: only se shows it
        self.silent_session()
        self.press("reconnect")
        self.adv(0.05); self.ack(0x10, True)
        self.adv(2); self.reply(ps=1, se=3)
        self.assertEqual(self.fix()["result"], "ok")

    def test_reconnect_does_not_come_back(self):
        self.silent_session()
        self.press("reconnect")
        self.adv(0.05); self.ack(0x10, True)
        for _ in range(21):
            self.adv(2); self.reply(ps=0, cl=1)
        f = self.fix()
        self.assertEqual((f["result"], f["level"], f["next_button"]), ("failed", "bad", "reboot_panel"))
        self.assertLess(f["next"].index("전원"), f["next"].index("[패널 다시 켜기]"))
        d = self.diag()
        self.assertEqual((d["code"], d["button"], d["op_button"]), ("PANEL_NO_SESSION", "reconnect_all", "reboot_panel"))
        self.assertIn("전원", d["steps"][0])

    def test_no_ack_from_a_flowlocal_that_never_answered_means_old_firmware(self):
        # the port just opened, FlowLocal has answered nothing yet
        self.system_ok(); self.port_up()
        self.adv(1); self.tr.on_request_sent(self.clk.t)
        self.press("reboot_flowlocal")
        self.adv(m.ACTIONS["reboot_flowlocal"]["ack_s"] + 0.5)
        f = self.fix()
        self.assertEqual((f["result"], f["level"]), ("no_ack", "warn"))
        self.assertIn("새 FlowLocal 펌웨어", f["message"])
        for k in ("reconnect", "reboot_panel", "reboot_flowlocal"):
            self.assertTrue(self.action(k)["needs_fw"], k)

    def test_no_ack_while_status_replies_come_is_a_lost_ack_and_a_late_ack_still_counts(self):
        self.silent_session()
        self.press("reconnect")
        self.adv(m.ACTIONS["reconnect"]["ack_s"] + 0.5)
        f = self.fix()
        self.assertEqual((f["result"], f["level"]), ("no_ack", "warn"))
        self.assertNotIn("펌웨어", f["message"])
        self.assertIn("한 번 더", f["next"])
        self.assertFalse(any(a["needs_fw"] for a in m.action_states(self.snap())))
        self.assertNotIn("reconnect", self.snap()["fix_failed"])   # it may well work next time
        self.adv(3); self.ack(0x10, True)                         # it came after all
        self.assertEqual(self.fix()["phase"], "recover")

    def test_no_ack_from_a_flowlocal_that_stopped_answering(self):
        # buttons worked earlier; in a short silence (the buttons still open) the restart
        # goes unanswered too: stuck, not old firmware - its RST button next
        self.healthy()
        self.ack(0x10, True, "ended 1 RTP session, the panel invites again")
        self.adv(8)
        self.assertTrue(self.action("reboot_flowlocal")["enabled"])
        self.press("reboot_flowlocal")
        self.adv(m.ACTIONS["reboot_flowlocal"]["ack_s"] + 0.5)
        f = self.fix()
        self.assertEqual(f["result"], "no_ack")
        self.assertIn("멈췄을 수 있어요", f["message"])
        self.assertIn("RST", f["next"])
        self.assertIn("뽑았다가", f["next"])
        self.assertFalse(any(a["needs_fw"] for a in m.action_states(self.snap())))
        self.adv(20)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["button"]), ("FL_NO_REPLY", "bad", None))
        self.assertIn("RST", d["steps"][0])
        self.assertFalse(self.action("reboot_flowlocal")["enabled"])

    def test_passing_refusals_do_not_count_as_failed(self):
        self.system_ok(); self.port_up()
        for _ in range(20):
            self.adv(2); self.reply(ps=0, cl=1)
        for msg in ("busy: the last reboot request is still running", "no RTP yet: the hotspot is still starting",
                    "too soon: once per 3 s, wait 2 s"):
            with self.subTest(msg):
                self.no_session(20)
                self.press("reboot_panel")
                self.adv(0.05); self.ack(0x11, False, msg)
                self.assertEqual(self.fix()["result"], "refused")
                self.assertNotIn("reboot_panel", self.snap()["fix_failed"])
                self.assertEqual(self.diag()["op_button"], "reboot_panel")
        self.no_session(20)
        self.assertIsNone(self.tr.fix_begin("reboot_panel", self.clk.t))
        self.tr.fix_sent(False, self.clk.t)                                  # never went out
        self.assertEqual(self.snap()["fix_failed"], {})
        self.no_session(20)
        self.press("reboot_panel")
        self.adv(1.2); self.ack(0x11, False, "no connection to panel 192.168.66.2")
        self.assertIn("reboot_panel", self.snap()["fix_failed"])             # this one did not help
        self.assertIsNone(self.diag()["op_button"])
        self.assertEqual(self.diag()["button"], "reconnect_all")             # the one button still can

    def test_an_ack_before_the_send_is_confirmed_still_counts(self):
        # the MIDI thread reads FlowLocal's ack before the web request calls fix_sent()
        self.silent_session()
        self.assertIsNone(self.tr.fix_begin("reconnect", self.clk.t))
        self.adv(0.004); self.ack(0x10, True, "ended 1 RTP session, the panel invites again")
        self.adv(0.002); self.tr.fix_sent(True, self.clk.t)
        f = self.fix()
        self.assertEqual((f["phase"], f["result"]), ("recover", None))
        self.adv(m.ACTIONS["reconnect"]["ack_s"] + 1)
        self.assertEqual(self.fix()["phase"], "recover")
        self.assertFalse(any(a["needs_fw"] for a in m.action_states(self.snap())))

    def test_refusals_say_why_and_what_next(self):
        self.silent_session()
        self.press("reconnect")
        self.adv(0.05); self.ack(0x10, False, "no RTP session to end")
        f = self.fix()
        self.assertEqual((f["result"], f["next_button"]), ("refused", "reboot_panel"))
        self.assertIn("끊을 패널 연결이 없어요", f["message"])
        self.assertEqual(f["ack"]["msg"], "no RTP session to end")
        self.adv(20)
        self.press("reboot_panel")
        self.adv(1.2); self.ack(0x11, False, "no panel session and 2 devices on the hotspot")
        f = self.fix()
        self.assertEqual(f["result"], "refused")
        self.assertIn("Wi-Fi를 꺼요", f["next"])
        self.adv(20)
        self.press("reboot_panel")
        self.adv(1.2); self.ack(0x11, False, "panel 192.168.66.2 answered HTTP 404")
        self.assertIn("패널 전원을 끄고", self.fix()["next"])
        self.adv(20)
        self.press("reboot_flowlocal")
        self.adv(0.05); self.ack(0x12, False, "too soon: once per 3 s, wait 2 s")
        self.assertIn("10초 뒤", self.fix()["next"])

    def test_reboot_panel_ok(self):
        self.system_ok(); self.port_up()
        for _ in range(20):
            self.adv(2); self.reply(ps=0, cl=1, se=2)
        self.assertEqual(self.diag()["op_button"], "reboot_panel")
        self.press("reboot_panel")
        self.adv(1.5); self.ack(0x11, True, "panel 192.168.66.2 is rebooting")
        self.assertEqual(self.fix()["phase"], "recover")
        self.adv(2); self.reply(ps=0, cl=0)
        self.assertEqual(self.diag()["code"], "FIX_WAIT")
        self.adv(8); self.reply(ps=1, se=3)
        self.assertEqual(self.fix()["result"], "ok")
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_reboot_panel_times_out(self):
        self.system_ok(); self.port_up()
        for _ in range(20):
            self.adv(2); self.reply(ps=0, cl=1)
        self.press("reboot_panel")
        self.adv(1.5); self.ack(0x11, True, "panel 192.168.66.2 is rebooting")
        for _ in range(31):
            self.adv(2); self.reply(ps=0, cl=1)
        f = self.fix()
        self.assertEqual((f["result"], f["level"]), ("failed", "bad"))
        self.assertEqual(f["next"], m.STEP_PANEL_POWER_CYCLE)
        self.assertEqual(self.diag()["op_button"], None)          # the reboot failed lately: not offered again

    def test_reboot_flowlocal_ok(self):
        self.healthy()
        self.press("reboot_flowlocal")
        self.adv(0.05); self.ack(0x12, True, "restarting: USB back in seconds, hotspot in about 13 s")
        self.adv(0.3); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.assertEqual(self.diag()["code"], "FIX_WAIT")         # the port going away is expected
        self.assertTrue(any("다시 켜지느라" in t for t in self.events("port")))
        self.adv(2.5); self.port_up()
        self.adv(2); self.reply(up=4, ap=0, cl=0, ps=0)
        self.assertEqual(self.diag()["code"], "FIX_WAIT")
        self.adv(16); self.reply(up=20, ps=1, se=1)
        f = self.fix()
        self.assertEqual(f["result"], "ok")
        self.assertIsNotNone(self.snap()["fl_reboot_age"])

    def test_reboot_flowlocal_ack_lost_but_the_board_went_away(self):
        self.healthy()
        self.press("reboot_flowlocal")
        self.adv(0.3); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.assertEqual(self.fix()["phase"], "recover")

    def test_send_failure(self):
        self.healthy()
        self.assertIsNone(self.tr.fix_begin("reconnect", self.clk.t))
        self.tr.fix_sent(False, self.clk.t)
        self.assertEqual(self.fix()["result"], "send_failed")

    def test_button_block_reasons(self):
        self.system_ok()
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.assertEqual(self.action("resync")["reason"], "작은 보드가 노트북에 안 보여요")
        self.port_up()
        self.reply(up=5, ap=0, cl=0, ps=0)
        self.assertEqual(self.action("reboot_panel")["reason"], "핫스팟이 아직 꺼져 있어요")
        self.assertTrue(self.action("reboot_flowlocal")["enabled"])
        self.assertTrue(self.action("resync")["enabled"])


class LiveAppTests(Clocked, unittest.TestCase):
    """LiveApp.fix against a fake MIDI thread: one message per accepted press."""

    class FakeWorker(object):
        def __init__(self):
            self.mo = object()
            self.sent = []

        def send(self, msg):
            self.sent.append(list(msg))
            return True

        def request(self, job):
            job(self)

    def app(self):
        app = m.LiveApp.__new__(m.LiveApp)
        app.tracker, app.events = self.tr, self.tr.events
        app.midi = self.FakeWorker()
        app.fix_lock = threading.Lock()
        return app

    def test_one_message_per_press_and_nothing_when_refused(self):
        self.healthy()
        self.reply(hd=45000)
        app = self.app()
        res = app.fix("reconnect")
        self.assertEqual((res["ok"], res["result"]), (True, "sent"))
        self.assertEqual(app.midi.sent, [m.build_command(0x10)])
        res = app.fix("reconnect")                                 # waiting for the ack
        self.assertFalse(res["ok"])
        self.assertEqual(app.midi.sent, [m.build_command(0x10)])
        self.adv(0.05); self.ack(0x10, False, "no RTP session to end")
        res = app.fix("reconnect")                                 # within the button's gap
        self.assertEqual(res["result"], "refused")
        self.adv(10)
        res = app.fix("resync")
        self.assertTrue(res["ok"])
        self.assertEqual(app.midi.sent, [m.build_command(0x10), m.RESYNC_REQUEST])
        self.assertTrue(any("'패널 연결 다시 잡기' 버튼을 눌렀어요" in t for t in self.events("button")))

    def test_disabled_button_sends_nothing(self):
        self.system_ok(); self.port_up()
        for _ in range(4):
            self.adv(2); self.tr.on_request_sent(self.clk.t)       # old firmware
        app = self.app()
        res = app.fix("reboot_flowlocal")
        self.assertEqual(res["result"], "refused")
        self.assertIn("새 FlowLocal 펌웨어", res["message"])
        self.assertIn("대신 손으로", res["message"])
        self.assertEqual(app.midi.sent, [])


# ------------------------------------------------ views, scenarios

class ViewTests(unittest.TestCase):
    def view(self, key):
        tr, clk = m.run_scenario(key)
        return m.build_view(tr.snapshot(clk.t))

    def test_all_ok_cards(self):
        v = self.view("all_ok")
        w = v["wireless"]
        self.assertEqual((w["bars"], w["word"], w["dbm"], w["ch"], w["chm"], w["level"]), (4, "좋음", -58, 1, "auto", "ok"))
        self.assertEqual((v["hotspot"]["cl"], v["hotspot"]["cm"], v["hotspot"]["level"]), (2, 3, "ok"))
        self.assertEqual(v["pattern"]["name"], "그림 3")
        self.assertEqual(v["uptime"]["fw"], "exhibit-mon1")
        self.assertIsNotNone(v["last"]["heard_age"])

    def test_old_firmware_cards_say_why(self):
        v = self.view("old_firmware")
        self.assertEqual(v["wireless"]["note"], "새 FlowLocal 펌웨어가 필요해요")
        self.assertIsNone(v["hotspot"]["cl"])
        self.assertIsNone(v["uptime"]["s"])
        self.assertEqual(v["pattern"]["name"], "그림 4")

    def test_full_hotspot_card(self):
        v = self.view("hotspot_full")
        self.assertEqual(v["hotspot"]["level"], "warn")
        self.assertEqual(v["wireless"]["note"], "패널이 연결돼 있지 않아요")

    def test_wifi_poor_card(self):
        w = self.view("wifi_poor")["wireless"]
        self.assertEqual((w["level"], w["word"]), ("warn", "나쁨"))
        self.assertEqual(set(w["problem_keys"]), {"prs", "jt", "jm", "gp", "bunch"})
        self.assertGreaterEqual(w["bunches_min"], m.BUNCHES_WARN_PER_MIN)
        w = self.view("wifi_poor_mon3")["wireless"]
        self.assertEqual((w["ch"], w["chm"], w["cs"]), (6, "auto", "1번 210 · 6번 95 · 11번 380"))

    def test_knob_check_card(self):
        v = self.view("all_ok")
        self.assertEqual(len(v["knobs"]), 3)
        self.assertFalse(v["knob_check"]["live"])                # nobody turned a knob in the last seconds
        tr, clk = m.run_scenario("all_ok", anim_t=40.0)          # the demo's visitor turns them
        v = m.build_view(tr.snapshot(clk.t))
        self.assertTrue(v["knob_check"]["live"])

    def test_rssi_words(self):
        for dbm, bars, word in ((-50, 4, "좋음"), (-60, 4, "좋음"), (-61, 3, "보통"), (-68, 2, "약한 편"),
                                (-75, 2, "약한 편"), (-76, 1, "약함")):
            tr = m.run_scenario("all_ok")[0]
            tr.status["prs"] = dbm
            v = m.build_view(tr.snapshot(m.T_END), wl=[])
            self.assertEqual((v["wireless"]["bars"], v["wireless"]["word"]), (bars, word), dbm)


class ScenarioTests(unittest.TestCase):
    def test_every_scenario_gives_its_rule(self):
        for key, (label, code, _fn, _anim, _up, _flow) in m.SCENARIOS.items():
            with self.subTest(key):
                tr, clk = m.run_scenario(key, anim_t=33.0)
                snap = tr.snapshot(clk.t)
                d = m.diagnose(snap, m.CONFIG_DEFAULTS)
                self.assertEqual(d["code"], code)
                self.assertTrue(d["headline"])
                self.assertTrue(d["steps"])
                self.assertTrue(all(s.strip() for s in d["steps"]))
                self.assertEqual([c["id"] for c in d["chain"]],
                                 ["panel", "wifi", "flowlocal", "usb", "laptop", "live", "speaker"])
                self.assertEqual(d["chain"][-1]["state"], "unknown")
                json.dumps(m.build_state("demo", snap, m.CONFIG_DEFAULTS, None, tr.events.recent()))

    def test_button_flows(self):
        want = {"fix_resync": ("done", "answered"), "fix_reconnect_wait": ("recover", None),
                "fix_reconnect_ok": ("done", "ok"), "fix_reconnect_failed": ("done", "failed"),
                "fix_no_ack": ("done", "no_ack"), "fix_reboot_panel_wait": ("recover", None),
                "fix_reboot_panel_ok": ("done", "ok"), "fix_reboot_panel_refused": ("done", "refused"),
                "fix_reboot_flowlocal_wait": ("recover", None), "fix_reboot_flowlocal_ok": ("done", "ok"),
                "fix_channel_wait": ("recover", None), "fix_channel_ok": ("done", "ok")}
        runs = {"reconnect_run": None, "reconnect_done": "ok", "reconnect_rst": None, "reconnect_panel_power": None,
                "reconnect_panel_reboot": "ok", "reconnect_live": "ok"}
        self.assertEqual(set(want) | set(runs), {k for k, v in m.SCENARIOS.items() if v[5]})
        for key, (phase, result) in want.items():
            with self.subTest(key):
                tr, clk = m.run_scenario(key)
                f = tr.snapshot(clk.t)["fix"]
                self.assertEqual((f["phase"], f["result"]), (phase, result))
        for key, result in runs.items():
            with self.subTest(key):
                tr, clk = m.run_scenario(key, anim_t=33.0)
                rc = tr.snapshot(clk.t)["reconnect"]
                self.assertEqual(rc["result"], result)
        tr, clk = m.run_scenario("fix_channel_ok")
        self.assertIn("무선 채널을 11번으로 바꿨어요", tr.snapshot(clk.t)["fix"]["message"])

    def test_every_rule_has_a_scenario(self):
        import inspect
        src = inspect.getsource(m.diagnose)
        codes = set(re.findall(r'(?:add|_issue)\("([A-Z_]+)"', src))
        self.assertGreater(len(codes), 20)
        covered = {v[1] for v in m.SCENARIOS.values()}
        self.assertEqual(codes - covered, set())

    def test_demo_press_plays_out(self):
        app = m.DemoApp(m.Config(os.path.join(os.path.dirname(os.path.abspath(__file__)), "no-such-config.json")),
                        pinned="panel_silent")
        res = app.fix("reconnect", {})
        self.assertTrue(res["ok"])
        st = app.state({})
        self.assertEqual(st["diagnosis"]["code"], "FIX_WAIT")
        self.assertEqual(st["snap"]["fix"]["action"], "reconnect")
        res = app.fix("reboot_flowlocal", {})                      # busy
        self.assertFalse(res["ok"])
        # a disabled button in a scenario is refused like on the hardware
        app2 = m.DemoApp(app.config, pinned="old_firmware")
        self.assertEqual(app2.fix("reconnect", {})["result"], "refused")


POWERCFG_EN = """Power Scheme GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (Balanced)
  GUID Alias: SCHEME_BALANCED
  Subgroup GUID: 238c9fa8-0aad-41ed-83f4-97be242c8f20  (Sleep)
    GUID Alias: SUB_SLEEP
    Power Setting GUID: 29f6c1db-86da-48c5-9fdb-f2b67b1f44da  (Sleep after)
      GUID Alias: STANDBYIDLE
      Minimum Possible Setting: 0x00000000
      Maximum Possible Setting: 0xffffffff
      Possible Settings increment: 0x00000001
      Possible Settings units: Seconds
    Current AC Power Setting Index: 0x00000708
    Current DC Power Setting Index: 0x00000384
"""

POWERCFG_KO = """전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)
  GUID 별칭: SCHEME_BALANCED
  하위 그룹 GUID: 2a737441-1930-4402-8d77-b2bebba308a3  (USB 설정)
    전원 설정 GUID: 48e6b7a6-50f5-4782-a5d4-53bb8f07e226  (USB 선택적 절전 모드 설정)
      가능한 설정 색인: 000
      가능한 설정 이름: 사용 안 함
      가능한 설정 색인: 001
      가능한 설정 이름: 사용
    현재 AC 전원 설정 색인: 0x00000001
    현재 DC 전원 설정 색인: 0x00000000

"""

TASKLIST = '''"AbletonAudioCpl.exe","20256","Console","1","N/A"
"Ableton Live 12 Suite.exe","10220","Console","1","411,588 K"
"Ableton Web Connector.exe","12024","Console","1","1,244 K"
"Ableton Index.exe","17580","Console","1","884 K"
'''

# `powercfg /qh SCHEME_CURRENT <subgroup GUID> <setting GUID>` on the exhibit laptop
# (Korean Windows 11, 2026-09-25 ~21:45), exactly as it came (OEM codepage, CRLF).
PCFG_STANDBY = ('전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
                '  하위 그룹 GUID: 238c9fa8-0aad-41ed-83f4-97be242c8f20  (절전)\r\n    GUID 별칭: SUB_SLEEP\r\n'
                '    전원 설정 GUID: 29f6c1db-86da-48c5-9fdb-f2b67b1f44da  (다음 시간 후 절전 모드로 전환)\r\n'
                '      GUID 별칭: STANDBYIDLE\r\n      가능한 최소 설정: 0x00000000\r\n      가능한 최대 설정: 0xffffffff\r\n'
                '      가능한 설정 증가: 0x00000001\r\n      가능한 설정 단위: 초\r\n    현재 AC 전원 설정 색인: 0x00000000\r\n'
                '    현재 DC 전원 설정 색인: 0x00000000\r\n\r\n')
PCFG_HIBERNATE = ('전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
                  '  하위 그룹 GUID: 238c9fa8-0aad-41ed-83f4-97be242c8f20  (절전)\r\n    GUID 별칭: SUB_SLEEP\r\n'
                  '    전원 설정 GUID: 9d7815a6-7ee4-497e-8888-515a05f02364  (다음 시간 이후에 최대 절전 모드로 전환)\r\n'
                  '      GUID 별칭: HIBERNATEIDLE\r\n      가능한 최소 설정: 0x00000000\r\n      가능한 최대 설정: 0xffffffff\r\n'
                  '      가능한 설정 증가: 0x00000001\r\n      가능한 설정 단위: 초\r\n    현재 AC 전원 설정 색인: 0x00000000\r\n'
                  '    현재 DC 전원 설정 색인: 0x00004650\r\n\r\n')
PCFG_LID_QH = ('전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
               '  하위 그룹 GUID: 4f971e89-eebd-4455-a8de-9e59040e7347  (전원 단추와 덮개)\r\n    GUID 별칭: SUB_BUTTONS\r\n'
               '    전원 설정 GUID: 5ca83367-6e45-459f-a27b-476b1d01c936  (덮개 닫기 동작)\r\n      GUID 별칭: LIDACTION\r\n'
               '      가능한 설정 색인: 000\r\n      가능한 설정 이름: 아무 것도 안 함\r\n      가능한 설정 색인: 001\r\n'
               '      가능한 설정 이름: 절전\r\n      가능한 설정 색인: 002\r\n      가능한 설정 이름: 최대 절전 모드\r\n'
               '      가능한 설정 색인: 003\r\n      가능한 설정 이름: 시스템 종료\r\n    현재 AC 전원 설정 색인: 0x00000001\r\n'
               '    현재 DC 전원 설정 색인: 0x00000001\r\n\r\n')
# the lid is a hidden setting: plain /q lists only the scheme (and exits 0)
PCFG_LID_Q = '전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
PCFG_USB = ('전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
            '  하위 그룹 GUID: 2a737441-1930-4402-8d77-b2bebba308a3  (USB 설정)\r\n'
            '    전원 설정 GUID: 48e6b7a6-50f5-4782-a5d4-53bb8f07e226  (USB 선택적 절전 모드 설정)\r\n'
            '      가능한 설정 색인: 000\r\n      가능한 설정 이름: 사용 안 함\r\n      가능한 설정 색인: 001\r\n'
            '      가능한 설정 이름: 사용 가능\r\n    현재 AC 전원 설정 색인: 0x00000000\r\n    현재 DC 전원 설정 색인: 0x00000000\r\n\r\n')
PCFG_ASPM = ('전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)\r\n  GUID 별칭: SCHEME_BALANCED\r\n'
             '  하위 그룹 GUID: 501a4d13-42af-4429-9fd1-a8218c268e20  (PCI Express)\r\n    GUID 별칭: SUB_PCIEXPRESS\r\n'
             '    전원 설정 GUID: ee12f906-d277-404b-b6da-e5fa1a576df5  (링크 상태 전원 관리)\r\n      GUID 별칭: ASPM\r\n'
             '      가능한 설정 색인: 000\r\n      가능한 설정 이름: 해제\r\n      가능한 설정 색인: 001\r\n'
             '      가능한 설정 이름: 보통 수준의 절전\r\n      가능한 설정 색인: 002\r\n      가능한 설정 이름: 최대 절전\r\n'
             '    현재 AC 전원 설정 색인: 0x00000002\r\n    현재 DC 전원 설정 색인: 0x00000002\r\n\r\n')
PCFG_BAD_PARAM = '매개 변수가 잘못되었습니다. 도움말을 보려면 "/?"를 입력하십시오.\r\n'

# `wevtutil qe Application /f:xml` on the exhibit laptop, 2026-09-25: Live's crash at
# 16:17 local (Application Error 1000), and the three WER 1001 records written at 21:50,
# the APPCRASH one being that 16:17 report sent late (same ReportId). Paths shortened.
WEV_1000 = ("<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Application Error'"
            " Guid='{a0e9b465-b939-57d7-b27d-95d8e925ff57}'/><EventID>1000</EventID><Version>0</Version><Level>2</Level>"
            "<Task>100</Task><Opcode>0</Opcode><Keywords>0x8000000000000000</Keywords><TimeCreated SystemTime="
            "'2026-09-25T07:17:43.1624750Z'/><EventRecordID>19601</EventRecordID><Correlation/><Execution ProcessID='8236'"
            " ThreadID='13380'/><Channel>Application</Channel><Computer>lshnote2</Computer><Security UserID="
            "'S-1-5-21-1745693637-2169050152-285897543-1001'/></System><EventData><Data Name='AppName'>Ableton Live 12 "
            "Suite.exe</Data><Data Name='AppVersion'>1.0.0.1</Data><Data Name='AppTimeStamp'>69cb157c</Data><Data Name="
            "'ModuleName'>rthdasio64.dll</Data><Data Name='ModuleVersion'>3.1.9.1</Data><Data Name='ModuleTimeStamp'>"
            "632d62f7</Data><Data Name='ExceptionCode'>c0000005</Data><Data Name='FaultingOffset'>0000000000003075</Data>"
            "<Data Name='ProcessId'>0x4e54</Data><Data Name='ProcessCreationTime'>0x1dd4cbb0d8940b2</Data><Data Name="
            "'AppPath'>C:\\ProgramData\\Ableton\\Live 12 Suite\\Program\\Ableton Live 12 Suite.exe</Data><Data Name="
            "'ModulePath'>c:\\windows\\system32\\rthdasio64.dll</Data><Data Name='IntegratorReportId'>dd12d02c-97ad-4381-"
            "a179-a080418bf191</Data><Data Name='PackageFullName'></Data><Data Name='PackageRelativeAppId'></Data>"
            "</EventData></Event>")
WEV_1001 = (
    "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Windows Error Rep"
    "orting' Guid='{0ead09bd-2157-539a-8d6d-c87f95b64d70}'/><EventID>1001</EventID><Version>0</Version><Level>4</Le"
    "vel><Task>0</Task><Opcode>0</Opcode><Keywords>0x8000000000000000</Keywords><TimeCreated SystemTime='2026-09-25"
    "T12:50:32.1443476Z'/><EventRecordID>19677</EventRecordID><Correlation/><Execution ProcessID='7520' ThreadID='2"
    "2688'/><Channel>Application</Channel><Computer>lshnote2</Computer><Security UserID='S-1-5-18'/></System><Event"
    "Data><Data Name='Bucket'>1660022046131342402</Data><Data Name='BucketType'>4</Data><Data Name='EventName'>APPC"
    "RASH</Data><Data Name='Response'>사용할 수 없음</Data><Data Name='CabId'>0</Data><Data Name='P1'>Ableton Live 12 Sui"
    "te.exe</Data><Data Name='P2'>1.0.0.1</Data><Data Name='P3'>69cb157c</Data><Data Name='P4'>rthdasio64.dll</Data"
    "><Data Name='P5'>3.1.9.1</Data><Data Name='P6'>632d62f7</Data><Data Name='P7'>c0000005</Data><Data Name='P8'>0"
    "000000000003075</Data><Data Name='P9'></Data><Data Name='P10'></Data><Data Name='AttachedFiles'>...</Data><Dat"
    "a Name='StorePath'>...</Data><Data Name='AnalysisSymbol'></Data><Data Name='Rechecking'>0</Data><Data Name='Re"
    "portId'>dd12d02c-97ad-4381-a179-a080418bf191</Data><Data Name='ReportStatus'>268959844</Data><Data Name='Hashe"
    "dBucket'>81a3c456f8db6b24570995417af9cc42</Data><Data Name='CabGuid'>0</Data></EventData></Event><Event xmlns="
    "'http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Windows Error Reporting' Guid="
    "'{0ead09bd-2157-539a-8d6d-c87f95b64d70}'/><EventID>1001</EventID><Version>0</Version><Level>4</Level><Task>0</"
    "Task><Opcode>0</Opcode><Keywords>0x8000000000000000</Keywords><TimeCreated SystemTime='2026-09-25T12:50:27.287"
    "3172Z'/><EventRecordID>19676</EventRecordID><Correlation/><Execution ProcessID='7520' ThreadID='22688'/><Chann"
    "el>Application</Channel><Computer>lshnote2</Computer><Security UserID='S-1-5-18'/></System><EventData><Data Na"
    "me='Bucket'></Data><Data Name='BucketType'>0</Data><Data Name='EventName'>AppTermFailureEvent</Data><Data Name"
    "='Response'>사용할 수 없음</Data><Data Name='CabId'>0</Data><Data Name='P1'></Data><Data Name='P2'></Data><Data Name"
    "='P3'></Data><Data Name='P4'></Data><Data Name='P5'></Data><Data Name='P6'></Data><Data Name='P7'></Data><Data"
    " Name='P8'></Data><Data Name='P9'></Data><Data Name='P10'></Data><Data Name='AttachedFiles'>...</Data><Data Na"
    "me='StorePath'>...</Data><Data Name='AnalysisSymbol'></Data><Data Name='Rechecking'>0</Data><Data Name='Report"
    "Id'>3e35d4da-5ba2-4c13-b03e-4a37c2b4c0c8</Data><Data Name='ReportStatus'>2051</Data><Data Name='HashedBucket'>"
    "</Data><Data Name='CabGuid'>0</Data></EventData></Event><Event xmlns='http://schemas.microsoft.com/win/2004/08"
    "/events/event'><System><Provider Name='Windows Error Reporting' Guid='{0ead09bd-2157-539a-8d6d-c87f95b64d70}'/"
    '><EventID>1001</EventID><Version>0</Version><Level>4</Level><Task>0</Task><Opcode>0</Opcode><Keywords>0x800000'
    "0000000000</Keywords><TimeCreated SystemTime='2026-09-25T12:50:27.1558809Z'/><EventRecordID>19675</EventRecord"
    "ID><Correlation/><Execution ProcessID='7520' ThreadID='22688'/><Channel>Application</Channel><Computer>lshnote"
    "2</Computer><Security UserID='S-1-5-18'/></System><EventData><Data Name='Bucket'></Data><Data Name='BucketType"
    "'>0</Data><Data Name='EventName'>LiveKernelEvent</Data><Data Name='Response'>사용할 수 없음</Data><Data Name='CabId'"
    ">0</Data><Data Name='P1'>193</Data><Data Name='P2'>815</Data><Data Name='P3'>2</Data><Data Name='P4'>ffffaa83d"
    "40ec0c0</Data><Data Name='P5'>0</Data><Data Name='P6'>10_0_26100</Data><Data Name='P7'>0_0</Data><Data Name='P"
    "8'>768_1</Data><Data Name='P9'></Data><Data Name='P10'></Data><Data Name='AttachedFiles'>...</Data><Data Name="
    "'StorePath'>...</Data><Data Name='AnalysisSymbol'></Data><Data Name='Rechecking'>0</Data><Data Name='ReportId'"
    ">bdf41b99-eb4d-4009-b484-697ae8ad07b1</Data><Data Name='ReportStatus'>2051</Data><Data Name='HashedBucket'></D"
    "ata><Data Name='CabGuid'>0</Data></EventData></Event>"
)


class SystemParsingTests(unittest.TestCase):
    def test_powercfg_range_setting_english(self):
        self.assertEqual(m.parse_powercfg_ac_dc(POWERCFG_EN, "29f6c1db-86da-48c5-9fdb-f2b67b1f44da"), (1800, 900))

    def test_powercfg_index_setting_korean(self):
        self.assertEqual(m.parse_powercfg_ac_dc(POWERCFG_KO, "48E6B7A6-50F5-4782-A5D4-53BB8F07E226"), (1, 0))

    def test_powercfg_other_output(self):
        self.assertIsNone(m.parse_powercfg_ac_dc("지정한 전원 구성표, 하위 그룹 또는 설정이 없습니다.", "48e6b7a6-50f5-4782-a5d4-53bb8f07e226"))
        self.assertIsNone(m.parse_powercfg_ac_dc(POWERCFG_KO, "29f6c1db-86da-48c5-9fdb-f2b67b1f44da"))
        # a cut-off answer: the AC line is there, the DC one is not
        self.assertEqual(m.parse_powercfg_ac_dc(POWERCFG_KO.split("    현재 DC")[0], "48e6b7a6-50f5-4782-a5d4-53bb8f07e226"),
                         (1, None))
        self.assertIsNone(m.parse_powercfg_ac_dc("", "x"))

    def test_power_items(self):
        checks = {c[0]: c for c in m.POWER_CHECKS}
        self.assertEqual(list(checks), ["sleep", "hibernate", "lid", "usb_suspend", "aspm"])
        i = m.power_item(checks["sleep"], (1800, 900))
        self.assertEqual((i["ok"], i["ac_ok"], i["dc_ok"], i["ac_text"], i["dc_text"], i["want_text"]),
                         (False, False, False, "30분 후", "15분 후", "안 함"))
        self.assertEqual(i["fix"], ["powercfg /setacvalueindex SCHEME_CURRENT %s %s 0" % checks["sleep"][1:3],
                                    "powercfg /setdcvalueindex SCHEME_CURRENT %s %s 0" % checks["sleep"][1:3]])
        u = m.power_item(checks["usb_suspend"], (0, 1))           # the old check looked at AC only
        self.assertEqual((u["ok"], u["ac_ok"], u["dc_ok"], u["dc_text"]), (False, True, False, "사용"))
        self.assertEqual(u["fix"], ["powercfg /setdcvalueindex SCHEME_CURRENT %s %s 0" % checks["usb_suspend"][1:3]])
        self.assertEqual(m.power_item(checks["usb_suspend"], (0, 0))["ok"], True)
        self.assertEqual(m.power_item(checks["lid"], (1, 1))["ac_text"], "절전")
        self.assertEqual(m.power_item(checks["aspm"], (2, 1))["dc_text"], "보통 수준의 절전")
        self.assertEqual(m.power_item(checks["aspm"], (0, 0))["want_text"], "해제")
        self.assertIsNone(m.power_item(checks["lid"], None)["ok"])
        self.assertIsNone(m.power_item(checks["lid"], (0, None))["ok"])      # half known and right: unknown
        self.assertFalse(m.power_item(checks["lid"], (None, 3))["ok"])       # half known and wrong: wrong

    def test_tasklist(self):
        self.assertEqual(m.live_processes(TASKLIST), ["Ableton Live 12 Suite.exe"])
        self.assertEqual(m.live_processes("정보: 지정된 조건에 맞는 작업이 실행되고 있지 않습니다.\r\n"), [])
        self.assertEqual(m.live_processes('"Ableton Live 11 Lite.exe","1","Console","1","1 K"'), ["Ableton Live 11 Lite.exe"])

    def test_power_status(self):
        self.assertEqual(m.power_from_status(1, 1, 79), (True, 79, True))
        self.assertEqual(m.power_from_status(0, 2, 30), (False, 30, True))
        self.assertEqual(m.power_from_status(255, 128, 255), (True, None, False))   # desktop, no battery
        self.assertEqual(m.power_from_status(255, 255, 255), (None, None, None))

    def test_durations(self):
        self.assertEqual([m.fmt_dur(x) for x in (0, 59, 60, 3599, 3600, 3660, None)],
                         ["0초", "59초", "1분", "59분", "1시간", "1시간 1분", "모름"])
        self.assertEqual([m.fmt_ago(x) for x in (0.5, 5, 125)], ["방금", "5초 전", "2분 전"])


# ------------------------------------------------ the MIDI thread against a fake rtmidi

class FakePort(object):
    registry = None

    def __init__(self):
        self.opened = None
        self.sent = []
        self.closed = False
        FakePort.registry["objects"].append(self)

    def get_ports(self):
        return list(self.registry["ports"])

    def open_port(self, idx):
        if self.registry.get("fail_open"):
            raise RuntimeError("busy")
        self.opened = idx

    def close_port(self):
        self.closed = True

    def ignore_types(self, sysex=True, timing=True, active_sense=True):
        self.ignored = (sysex, timing, active_sense)

    def get_message(self):
        if self.registry["incoming"] and self.opened is not None and not isinstance(self, FakeOut):
            return self.registry["incoming"].pop(0), 0.0
        return None

    def send_message(self, msg):
        if self.registry.get("fail_send"):
            raise RuntimeError("device gone")
        self.sent.append(list(msg))
        self.registry["sent"].append(list(msg))
        if self.registry.get("answer") and list(msg) == m.STATUS_REQUEST:
            self.registry["incoming"].append(reply_bytes(up=int(time.monotonic())))


class FakeIn(FakePort):
    pass


class FakeOut(FakePort):
    pass


class MidiWorkerTests(unittest.TestCase):
    def setUp(self):
        self.reg = {"ports": ["Patternflow Exhibit 0"], "objects": [], "sent": [], "incoming": []}
        FakePort.registry = self.reg
        self.saved = m.rtmidi
        m.rtmidi = types.SimpleNamespace(MidiIn=FakeIn, MidiOut=FakeOut)
        self.tr = m.Tracker(m.EventLog(None), midi_lib=True)
        self.w = m.MidiWorker(self.tr, poll_s=0.05)

    def tearDown(self):
        self.w.stop()
        if self.w.ident is not None:
            self.w.join(2)
        m.rtmidi = self.saved

    def wait(self, cond, timeout=3.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if cond():
                return True
            time.sleep(0.01)
        return False

    def test_sends_only_status_requests_and_reads_replies(self):
        self.reg["incoming"] = [[0xC0, 2], reply_bytes(ps=1), m.build_ack(0x10, True)]
        self.w.start()
        self.assertTrue(self.wait(lambda: len(self.reg["sent"]) >= 3))
        self.assertTrue(self.wait(lambda: self.tr.replies == 1 and self.tr.acks == 1))
        self.assertTrue(all(msg == m.STATUS_REQUEST for msg in self.reg["sent"]))
        ins = [o for o in self.reg["objects"] if isinstance(o, FakeIn) and o.opened is not None]
        self.assertEqual(ins[0].ignored, (False, True, True))
        self.assertEqual(self.tr.pc, 2)

    def test_a_job_sends_one_message(self):
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.out_open))
        job = m.SendJob(m.build_command(0x12))
        self.w.request(job)
        self.assertTrue(job.wait(2.0))
        self.assertEqual(self.reg["sent"].count(m.build_command(0x12)), 1)
        self.assertTrue(all(msg in (m.STATUS_REQUEST, m.build_command(0x12)) for msg in self.reg["sent"]))

    def test_a_cancelled_job_sends_nothing(self):
        job = m.SendJob(m.build_command(0x11))
        self.assertFalse(job.wait(0.01))               # nobody ran it: cancelled
        worker = types.SimpleNamespace(mo=object(), send=lambda msg: self.fail("sent after cancel"))
        job(worker)
        self.assertFalse(job.sent)

    def test_a_job_sent_at_the_deadline_is_reported_sent(self):
        # the wait times out while the MIDI thread is sending: it went out
        job = m.SendJob(m.build_command(0x10))
        started = threading.Event()

        def slow_send(msg):
            started.set()
            time.sleep(0.2)
            return True
        runner = threading.Thread(target=job, args=(types.SimpleNamespace(mo=object(), send=slow_send),))
        runner.start()
        self.assertTrue(started.wait(1.0))
        self.assertTrue(job.wait(0.05))
        runner.join(2)
        self.assertFalse(job.cancelled)

    def test_vanished_ports_are_never_closed(self):
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.in_open and self.tr.out_open))
        opened = [o for o in self.reg["objects"] if o.opened is not None]
        self.reg["ports"] = []
        self.assertTrue(self.wait(lambda: not self.tr.port_present))
        self.assertEqual(len(self.w.vanished), 2)
        self.reg["ports"] = ["Patternflow Exhibit 0"]
        self.assertTrue(self.wait(lambda: self.tr.in_open and self.tr.out_open))
        self.w.stop(); self.w.join(2)
        self.assertFalse(any(o.closed for o in opened))

    def test_replies_that_stop_while_the_port_stays_listed_reopen_it(self):
        # FlowLocal went away and came back between two port-list reads: the old
        # handle hears nothing any more
        self.reg["answer"] = True
        self.w.port_check_s, self.w.stale_reply_s = 0.05, 0.3
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.replies >= 2))
        first = self.w.mi
        self.reg["answer"] = False
        self.assertTrue(self.wait(lambda: self.w.mi is not None and self.w.mi is not first))
        self.assertIn(first, self.w.vanished)
        self.assertFalse(first.closed)
        self.assertTrue(self.tr.port_present and self.tr.in_open and self.tr.out_open)
        self.assertTrue(any("새로 열어요" in e["text"] for e in self.tr.events.recent(50)))
        # still silent: a few more tries, then it leaves the port alone
        self.assertTrue(self.wait(lambda: self.w.stale_tries == m.STALE_TRIES))
        n = len(self.w.vanished)
        time.sleep(0.8)
        self.assertEqual(len(self.w.vanished), n)
        self.assertEqual(n, 2 * m.STALE_TRIES)
        self.reg["answer"] = True                                  # answering again: the count starts over
        self.assertTrue(self.wait(lambda: self.w.stale_tries == 0))

    def test_no_reopen_for_a_flowlocal_that_never_answered(self):
        # old firmware: silence tells nothing about the handle
        self.w.port_check_s, self.w.stale_reply_s = 0.05, 0.1
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.in_open))
        time.sleep(0.5)
        self.assertEqual(self.w.vanished, [])

    def test_a_flowlocal_restart_ack_reopens_the_port_once(self):
        self.reg["answer"] = True
        self.w.port_check_s, self.w.reopen_s = 0.05, 0.2
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.replies >= 1))
        first = self.w.mi
        self.reg["incoming"].append(m.build_ack(0x12, True, "restarting: USB back in seconds, hotspot in about 13 s"))
        self.assertTrue(self.wait(lambda: self.w.mi is not None and self.w.mi is not first))
        second = self.w.mi
        time.sleep(0.5)
        self.assertIs(self.w.mi, second)
        self.assertEqual(len(self.w.vanished), 2)

    def test_a_failed_send_reopens_the_port(self):
        self.w.port_check_s = 0.05
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.out_open))
        first = self.w.mo
        self.reg["fail_send"] = True
        self.assertTrue(self.wait(lambda: self.w.mo is not None and self.w.mo is not first))
        self.assertIn(first, self.w.vanished)

    def test_open_failure_is_reported_and_retried(self):
        self.reg["fail_open"] = True
        self.w.start()
        self.assertTrue(self.wait(lambda: self.tr.open_error is not None))
        self.assertEqual(m.diagnose(self.tr.snapshot())["code"], "PORT_BUSY")
        self.reg["fail_open"] = False
        self.assertTrue(self.wait(lambda: self.tr.in_open and self.tr.out_open, timeout=m.OPEN_RETRY_S + 3))


# ------------------------------------------------ HTTP (demo mode)

class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = m.DemoApp(m.Config(os.path.join(os.path.dirname(os.path.abspath(__file__)), "no-such-config.json")))
        cls.srv = m.make_server(cls.app, 0)
        cls.port = cls.srv.server_address[1]
        cls.t = threading.Thread(target=cls.srv.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        cls.t.start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()

    def req(self, method, path, headers=None, host=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        h = dict(headers or {})
        if host:
            h["Host"] = host
        c.request(method, path, headers=h)
        r = c.getresponse()
        body = r.read()
        c.close()
        return r.status, body

    def test_state_and_page(self):
        st, body = self.req("GET", "/api/state?scenario=hotspot_full")
        self.assertEqual(st, 200)
        d = json.loads(body.decode("utf-8"))
        self.assertEqual(d["diagnosis"]["code"], "HOTSPOT_FULL")
        self.assertEqual(d["demo"]["scenario"], "hotspot_full")
        self.assertEqual([a["id"] for a in d["actions"]], list(m.ACTIONS))
        st, body = self.req("GET", "/")
        self.assertEqual(st, 200)
        page = body.decode("utf-8")
        self.assertIn("패턴플로우 전시 모니터", page)
        self.assertNotRegex(page, r"(src|href)=\"https?://")          # nothing from the internet
        self.assertEqual(self.req("GET", "/api/ping")[0], 200)
        self.assertEqual(self.req("GET", "/nope")[0], 404)

    def test_cycles_without_a_pin(self):
        d = json.loads(self.req("GET", "/api/state")[1].decode("utf-8"))
        self.assertIn(d["demo"]["scenario"], m.SCENARIOS)
        self.assertIsNotNone(d["demo"]["next_in"])

    def test_fix_needs_the_dashboard_header(self):
        self.assertEqual(self.req("POST", "/api/fix/resync?scenario=all_ok")[0], 403)
        self.assertEqual(self.req("POST", "/api/fix/format_disk", {"X-PF-Monitor": "1"})[0], 404)
        self.assertEqual(self.req("GET", "/api/fix/resync")[0], 404)
        st, body = self.req("POST", "/api/fix/resync?scenario=all_ok", {"X-PF-Monitor": "1"})
        self.assertEqual(st, 200)
        self.assertTrue(json.loads(body.decode("utf-8"))["ok"])

    def test_foreign_host_header_is_refused(self):
        self.assertEqual(self.req("GET", "/api/state", host="evil.example:8765")[0], 403)
        self.assertEqual(self.req("POST", "/api/fix/resync", {"X-PF-Monitor": "1"}, host="evil.example")[0], 403)
        self.assertEqual(self.req("GET", "/api/state", host="localhost:%d" % self.port)[0], 200)

    def test_second_server_on_the_same_port_fails(self):
        with self.assertRaises(OSError):
            m.make_server(self.app, self.port)


class ConfigTests(unittest.TestCase):
    def test_repo_config_parses(self):
        c = m.Config()
        data = c.get()
        self.assertIsNone(c.error)
        self.assertEqual(len(data["contacts"]), 2)
        self.assertNotIn("_설명", data)

    def test_broken_config_keeps_defaults(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "config.json")
            with open(p, "w", encoding="utf-8") as f:
                f.write("{ not json")
            c = m.Config(p)
            self.assertEqual(c.get()["contacts"], m.CONFIG_DEFAULTS["contacts"])
            self.assertIn("config.json", c.error)


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class SupervisorTests(Clocked, unittest.TestCase):
    """The Supervisor thread with its system checks faked (no tasklist, powercfg or wevtutil)."""

    def test_the_log_follows_a_change_of_level_too(self):
        from unittest import mock
        self.healthy()
        cfg = types.SimpleNamespace(get=lambda: dict(m.CONFIG_DEFAULTS))
        lines = lambda: [e["text"] for e in self.tr.events.recent(200) if e["kind"] == "diagnosis"]

        def wait_for(cond, timeout=5.0):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if cond():
                    return True
                time.sleep(0.05)
            return False
        with mock.patch.object(m, "check_live", lambda: (True, ["Ableton Live 12 Suite.exe"], None)), \
                mock.patch.object(m, "check_power", lambda: (True, 90, True)), \
                mock.patch.object(m, "check_power_settings", lambda run=None: m.demo_power_items()):
            sup = m.Supervisor(self.tr, cfg)
            sup.start()
            try:
                self.assertTrue(wait_for(lambda: "화면 상태: 모두 정상이에요" in lines()))
                self.adv(8)                                       # FlowLocal quiet: a moment
                self.assertTrue(wait_for(lambda: "화면 상태: 작은 보드(FlowLocal)가 잠깐 대답하지 않아요" in lines()))
                self.adv(15)                                      # still quiet: red, same code
                self.assertTrue(wait_for(lambda: "화면 상태: FlowLocal이 대답을 멈췄어요" in lines()))
            finally:
                sup.stop()
                sup.join(3)
        self.assertEqual(len(lines()), 3)


# ------------------------------------------------ memory: logs/state.json

class MemoryTests(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.path = os.path.join(self.dir, "state.json")
        self.now = [1790000000.0]

    def tearDown(self):
        self.tmp.cleanup()

    def mem(self):
        return m.Memory(self.path, wall=lambda: self.now[0])

    def log(self, name, text):
        with open(os.path.join(self.dir, name), "w", encoding="utf-8") as f:
            f.write(text)

    def test_fresh(self):
        mem = self.mem()
        self.assertEqual((mem.fl_protocol_seen, mem.fl_fw, mem.error), (False, None, None))
        self.assertFalse(mem.flush())                             # nothing to write
        self.assertFalse(os.path.exists(self.path))
        self.assertFalse(m.open_memory(self.dir).fl_protocol_seen)   # no logs either
        self.assertFalse(m.open_memory(None).fl_protocol_seen)

    def test_remembered_across_a_restart(self):
        mem = self.mem()
        mem.remember("flx-mon1", self.now[0])
        self.assertTrue(mem.flush())                              # the first answer is written at once
        with open(self.path, encoding="utf-8") as f:
            data = json.load(f)
        self.assertEqual((data["version"], data["fl_protocol_seen"], data["fl_fw"]), (1, True, "flx-mon1"))
        self.assertEqual(data["fl_seen_at"], self.now[0])
        self.assertRegex(data["fl_seen_local"], r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d$")
        self.assertIn("_설명", data)
        self.assertFalse(os.path.exists(self.path + ".tmp"))
        again = self.mem()
        self.assertEqual((again.fl_protocol_seen, again.fl_fw, again.source, again.fl_seen_at),
                         (True, "flx-mon1", "file", self.now[0]))
        self.assertTrue(again.view()["seen"])

    def test_writes_are_spaced_but_news_is_written_at_once(self):
        mem = self.mem()
        mem.remember("flx-mon1", self.now[0])
        mem.flush()
        self.now[0] += 10
        mem.remember("flx-mon1", self.now[0])
        self.assertFalse(mem.flush())                             # only the time moved: once a minute
        self.now[0] += m.MEMORY_SAVE_S
        self.assertTrue(mem.flush())
        self.now[0] += 1
        mem.remember("flx-mon2", self.now[0])                     # a new firmware
        self.assertTrue(mem.flush())
        self.assertEqual(self.mem().fl_fw, "flx-mon2")
        self.now[0] += 1
        mem.note_restart(self.now[0] - 5, 2, "brownout")          # a self-restart logged
        self.assertTrue(mem.flush())
        self.assertTrue(self.mem().restart_already_noted(self.now[0] - 20, 2, "brownout"))
        self.assertFalse(self.mem().restart_already_noted(self.now[0] - 60, 2, "brownout"))
        self.assertFalse(self.mem().restart_already_noted(self.now[0] - 5, 3, "brownout"))

    def test_a_corrupt_file_is_kept_aside_and_replaced(self):
        with open(self.path, "w", encoding="utf-8") as f:
            f.write('{"fl_protocol_seen": tru')
        mem = self.mem()
        self.assertFalse(mem.fl_protocol_seen)
        self.assertTrue(os.path.exists(self.path + ".bad"))
        err = mem.take_error()
        self.assertIn("state.json", err)
        self.assertIn("state.json.bad", err)
        self.assertIsNone(mem.take_error())                       # said once
        mem.remember("flx-mon1", self.now[0])
        mem.flush()
        self.assertTrue(self.mem().fl_protocol_seen)

    def test_wrong_shapes_are_ignored(self):
        for text in ('[1, 2]', '"yes"', '\x00\x01',
                     '{"fl_protocol_seen": "yes", "fl_fw": 5, "fl_seen_at": "x", "fl_restart_noted": [1]}',
                     '{"fl_protocol_seen": 1, "fl_fw": "a b", "fl_seen_at": -5, "fl_restart_noted": {"boot_at": "x"}}'):
            with self.subTest(text):
                with open(self.path, "w", encoding="utf-8") as f:
                    f.write(text)
                mem = self.mem()
                self.assertEqual((mem.fl_protocol_seen, mem.fl_fw, mem.fl_seen_at, mem.restart_noted),
                                 (False, None, None, None))
                if os.path.exists(self.path + ".bad"):
                    os.remove(self.path + ".bad")

    def test_seeded_from_the_logs(self):
        # no state.json yet (the monitor of 2026-09-25 had none): the event logs know
        self.log("2026-09-24.log", "2026-09-24 10:00:00 [status] (정상) FlowLocal 상태를 받기 시작했어요 (펌웨어 exhibit-mon1, 켜진 지 3초)\n")
        self.log("2026-09-25.log",
                 "2026-09-25 19:04:18 [status] (정상) FlowLocal 상태를 받기 시작했어요 (펌웨어 flx-mon1, 켜진 지 1분)\n"
                 "2026-09-25 21:12:51 [status] (확인 필요) FlowLocal이 상태 요청에 답하지 않아요 - 예전 펌웨어로 보여요 (패널 신호는 계속 보여 줘요)\n"
                 "2026-09-25 21:19:31 [status] (정상) FlowLocal 상태를 받기 시작했어요 (펌웨어 flx-mon1, 켜진 지 2초)\n"
                 "2026-09-25 21:20:32 [pattern] 패턴 6 YUJIN\n")
        self.log("notes.txt", "2026-09-26 10:00:00 [status] (정상) FlowLocal 상태를 받기 시작했어요 (펌웨어 other, 켜진 지 2초)\n")
        mem = m.open_memory(self.dir, wall=lambda: self.now[0])
        self.assertEqual((mem.fl_protocol_seen, mem.fl_fw, mem.source), (True, "flx-mon1", "logs"))
        self.assertEqual(time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(mem.fl_seen_at)), "2026-09-25 21:19:31")
        self.assertTrue(mem.flush())                              # and written down for next time
        self.assertEqual(self.mem().source, "file")

    def test_logs_without_a_reply_seed_nothing(self):
        self.log("2026-09-25.log", "2026-09-25 21:12:51 [status] (확인 필요) FlowLocal이 상태 요청에 답하지 않아요 - 예전 펌웨어로 보여요\n")
        self.assertFalse(m.open_memory(self.dir).fl_protocol_seen)
        self.log("2026-09-26.log", "2026-09-26 09:00:00 [status] (정상) FlowLocal 상태를 받기 시작했어요 (펌웨어 ?, 켜진 지 2초)\n")
        mem = m.open_memory(self.dir)
        self.assertEqual((mem.fl_protocol_seen, mem.fl_fw), (True, None))   # a reply without a tag still counts

    def test_a_file_that_cannot_be_written_is_said_once(self):
        os.makedirs(self.path + ".tmp")                           # something in the way of the temporary file
        mem = self.mem()
        mem.remember("flx-mon1", self.now[0])
        self.assertFalse(mem.flush())
        self.assertIn("쓰지 못했어요", mem.take_error())
        self.now[0] += m.MEMORY_SAVE_S + 1
        self.assertFalse(mem.flush())
        self.assertIsNone(mem.take_error())
        self.assertTrue(mem.fl_protocol_seen)                     # this run still knows


# ------------------------------------------------ a stuck FlowLocal versus old firmware

class StuckFlowLocalTests(Clocked, unittest.TestCase):
    """2026-09-25 21:12: the monitor started again while FlowLocal (flx-mon1, answering
    all afternoon) sat on USB with its USB side silent. Heard on this laptop before:
    stuck, press RST. Never heard here: old firmware."""

    def silent_start(self, remembered):
        if remembered:
            self.tr.memory.remember("flx-mon1", self.tr.memory.wall() - 3600)
        self.system_ok()
        self.port_up()
        for _ in range(4):
            self.adv(2); self.tr.on_request_sent(self.clk.t); self.tr.check_stale(self.clk.t)

    def test_never_heard_here_is_old_firmware(self):
        self.silent_start(False)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_OLD_FIRMWARE", "warn"))
        self.assertTrue(any("예전 펌웨어로 보여요" in t for t in self.events("status")))
        self.adv(60); self.tr.on_request_sent(self.clk.t)
        self.assertEqual(self.diag()["code"], "FL_OLD_FIRMWARE")
        self.assertTrue(self.action("reboot_flowlocal")["needs_fw"])
        self.assertIsNone(m.buttons_note(self.snap()))

    def test_heard_before_is_stuck(self):
        self.silent_start(True)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["wait"]), ("FL_NO_REPLY", "warn", True))   # 8 s: a moment
        texts = self.events("status")
        self.assertFalse(any("예전 펌웨어" in t for t in texts))
        self.assertTrue(any("새 펌웨어(flx-mon1)로 대답하던 보드예요" in t for t in texts))
        self.adv(13); self.tr.on_request_sent(self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"], d["button"]), ("FL_NO_REPLY", "bad", None))
        self.assertEqual(d["headline"], "FlowLocal이 대답을 멈췄어요")
        self.assertIn("RST 버튼을 한 번 누르세요", d["steps"][0])
        self.assertIn("BOOT 버튼은 누르지 않아요", d["steps"][0])
        self.assertIn("5초 뒤 같은 테이프 자리에", d["steps"][1])
        self.assertIn("담당자", d["steps"][2])
        self.assertIn("새 펌웨어(flx-mon1)로 대답한 적이 있어서", d["detail"])
        self.assertIn("멈춘 것으로 보여요", d["detail"])
        self.assertNotIn("예전 펌웨어로 보여요", d["detail"])
        self.assertEqual(d["chain"][2]["text"], "21초째 대답 없음")
        states = m.action_states(self.snap())
        self.assertTrue(all(not a["enabled"] and not a["needs_fw"] for a in states))
        self.assertEqual(m.build_view(self.snap())["uptime"]["note"], "FlowLocal이 대답을 멈췄어요")
        self.assertFalse(m.old_firmware_suspected(self.snap()))
        # RST: the port goes and comes back, FlowLocal answers
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(1); self.port_up(); self.adv(1)
        self.reply(up=2, ap=0, cl=0, ps=0)
        self.assertEqual(self.diag()["code"], "FL_STARTING")

    def test_heard_in_this_run_then_plugged_in_again_and_silent(self):
        self.healthy()
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(3); self.port_up()
        for _ in range(12):
            self.adv(2); self.tr.on_request_sent(self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_NO_REPLY", "bad"))
        self.assertIn("모니터가 연결한 뒤 24초째 한 번도", d["detail"])

    def test_panel_midi_still_coming_is_yellow(self):
        self.healthy()
        self.adv(25)
        self.tr.on_midi([0xB0, 24, 10], self.clk.t)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_NO_REPLY", "warn"))
        self.assertIn("패널 신호는 들어와요", d["headline"])
        self.assertIn("RST", d["steps"][1])

    def test_a_reply_is_remembered(self):
        self.assertFalse(self.tr.memory.fl_protocol_seen)
        self.assertFalse(self.snap()["fl_memory"]["seen"])
        self.healthy()
        self.assertEqual((self.tr.memory.fl_protocol_seen, self.tr.memory.fl_fw), (True, "exhibit-mon1"))
        mem = self.snap()["fl_memory"]
        self.assertEqual((mem["seen"], mem["source"], mem["fw"]), (True, "run", "exhibit-mon1"))

    def test_a_button_unanswered_by_a_remembered_flowlocal_is_not_old_firmware(self):
        self.tr.memory.remember("flx-mon1", self.tr.memory.wall())
        self.system_ok(); self.port_up()
        self.adv(1); self.tr.on_request_sent(self.clk.t)
        self.assertIsNone(self.tr.fix_begin("reboot_flowlocal", self.clk.t))
        self.adv(0.01); self.tr.fix_sent(True, self.clk.t)
        self.adv(m.ACTIONS["reboot_flowlocal"]["ack_s"] + 0.5)
        f = self.snap()["fix"]
        self.assertEqual(f["result"], "no_ack")
        self.assertNotIn("펌웨어", f["message"])
        self.assertIn("RST", f["next"])
        self.assertFalse(any(a["needs_fw"] for a in m.action_states(self.snap())))


# ------------------------------------------------ FlowLocal restarting again and again

class FlappingTests(Clocked, unittest.TestCase):
    def drop(self, gone_s=1.0):
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(gone_s)
        self.port_up()

    def flap_lines(self):
        return [t for t in self.events("flowlocal") if "계속 다시 켜지고" in t]

    def test_three_restarts_in_two_minutes(self):
        # 2026-09-25 21:02:56, 21:03:26, 21:03:51
        self.healthy()
        self.drop(); self.adv(29)
        self.drop(); self.adv(24)
        self.assertEqual(self.snap()["flap"]["count_window"], 2)
        self.assertNotEqual(self.diag()["code"], "FL_FLAPPING")
        self.drop()
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_FLAPPING", "bad"))
        self.assertEqual(d["headline"], "FlowLocal이 계속 다시 켜지고 있어요")
        joined = " ".join(d["steps"])
        for words in ("같은 테이프 자리", "끝까지", "노트북 충전기", "노트북 전시용 전원 설정.bat", "(담당자)", "담당자에게 연락"):
            self.assertIn(words, joined)
        self.assertIn("3번", d["detail"])
        self.assertEqual(d["chain"][2]["text"], "계속 다시 켜짐")
        self.assertEqual(len(self.flap_lines()), 1)
        self.drop()                                               # a fourth: no second line
        self.assertEqual(len(self.flap_lines()), 1)

    def test_while_it_is_gone_it_does_not_just_wait(self):
        self.healthy()
        for _ in range(3):
            self.drop(); self.adv(10)
        self.tr.on_ports(False, None, None, 0, self.clk.t)       # gone again, 2 s: "다시 찾는 중" would be a wait
        self.adv(2)
        self.assertEqual(self.diag()["code"], "FL_FLAPPING")

    def test_spread_out_restarts_are_not_flapping(self):
        self.healthy()
        for _ in range(3):
            self.drop(); self.adv(61)
        f = self.snap()["flap"]
        self.assertEqual((f["now"], f["recent"]), (False, False))
        self.assertEqual(self.flap_lines(), [])

    def test_after_the_run_a_yellow_note_then_nothing(self):
        self.healthy()
        for _ in range(3):
            self.drop(); self.adv(10)
        self.assertTrue(self.snap()["flap"]["now"])
        self.adv(m.FLAP_WINDOW_S)
        for _ in range(3):
            self.adv(2); self.reply()
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_FLAPPING", "warn"))
        self.assertEqual(d["headline"], "FlowLocal이 조금 전 여러 번 다시 켜졌어요")
        self.tr.check_stale(self.clk.t)
        self.assertTrue(any("다시 켜지지 않고 붙어 있어요" in t for t in self.events("flowlocal")))
        self.adv(m.FLAP_NOTE_S)
        self.reply()
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_the_restart_button_is_not_counted(self):
        self.healthy()
        for _ in range(3):
            self.assertIsNone(self.tr.fix_begin("reboot_flowlocal", self.clk.t))
            self.adv(0.01); self.tr.fix_sent(True, self.clk.t)
            self.adv(0.05); self.ack(0x12, True, "restarting: USB back in seconds, hotspot in about 13 s")
            self.adv(0.3); self.drop(2.5)
            self.adv(2); self.reply(up=2)
            self.assertEqual(self.snap()["fix"]["result"], "ok")
            self.adv(16)
        self.assertEqual(self.snap()["flap"]["count"], 0)
        self.assertEqual(self.flap_lines(), [])

    def test_a_restart_seen_only_in_the_uptime_counts(self):
        # FlowLocal came back between two port-list reads: only its uptime says so
        self.healthy()
        for _ in range(3):
            self.adv(20); self.reply(up=1)
        self.assertTrue(self.snap()["flap"]["now"])
        self.assertEqual(self.diag()["code"], "FL_FLAPPING")

    def test_a_new_run_after_a_quiet_spell_is_logged_again(self):
        self.healthy()
        for _ in range(3):
            self.drop(); self.adv(5)
        self.adv(m.FLAP_WINDOW_S + 1)
        self.tr.check_stale(self.clk.t)
        for _ in range(3):
            self.drop(); self.adv(5)
        self.assertEqual(len(self.flap_lines()), 2)


# ------------------------------------------------ flx-mon2: why FlowLocal started

class RestartReasonTests(Clocked, unittest.TestCase):
    def test_parsing(self):
        st = parse(reply_bytes(rsn="brownout", bc=3, why="bridge_hung", ur=2))
        self.assertEqual((st["rsn"], st["bc"], st["why"], st["ur"]), ("brownout", 3, "bridge_hung", 2))
        self.assertEqual(st["extra"], {})
        st = parse(sysex(0x02, FW_RUNNING))                      # flx-mon1 has none of them
        self.assertEqual((st["rsn"], st["bc"], st["why"], st["ur"]), (None, None, None, None))
        st = parse(reply_bytes(rsn="Brown Out!", bc="-1", why="a b", ur="x"))
        self.assertEqual((st["rsn"], st["bc"], st["why"], st["ur"]), (None, None, None, None))
        st = parse(reply_bytes(rsn="newreason", zz="1"))        # a reason from a later firmware, an unknown key
        self.assertEqual((st["rsn"], st["extra"]), ("newreason", {"zz": "1"}))
        self.assertEqual(m.reset_word("newreason"), "알 수 없음 · newreason")
        self.assertEqual(m.reset_word("brownout"), "전원 부족")
        self.assertEqual(set(m.RESET_REASONS), {"poweron", "ext", "sw", "panic", "int_wdt", "task_wdt", "wdt", "deepsleep",
                                                "brownout", "sdio", "usb", "jtag", "efuse", "pwr_glitch", "cpu_lockup",
                                                "unknown"})
        self.assertEqual({k for k, (_w, kind) in m.RESET_REASONS.items() if kind == "power"}, {"brownout", "pwr_glitch"})
        self.assertIn("rsn", {k for k, _ in m.STATUS_KEYS})       # shown in the operator section

    def start(self, up, **over):
        """FlowLocal back from a restart `up` s ago."""
        self.boot = self.clk.t - up
        self.reply(up=up, **over)

    def replies(self, seconds, **over):
        for _ in range(int(seconds // 2)):
            self.adv(2); self.reply(up=int(self.clk.t - self.boot), **over)

    def test_a_normal_start_says_nothing(self):
        self.system_ok(); self.port_up()
        self.start(5000, rsn="poweron", bc=1, ur=0)
        self.replies(10, rsn="poweron", bc=1, ur=0)
        self.assertEqual(self.events("flowlocal"), [])
        self.assertIsNone(self.snap()["fl_restart"])
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_flx_mon1_restarts_are_still_noticed_without_a_reason(self):
        self.healthy()
        self.adv(30); self.start(3)
        self.assertEqual([t for t in self.events("flowlocal") if "다시 켜졌어요" in t], ["FlowLocal이 다시 켜졌어요 (켜진 지 3초)"])
        self.assertIsNone(self.snap()["fl_restart"])

    def test_brownout(self):
        self.healthy()
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(2); self.port_up(); self.adv(1)
        self.start(2, rsn="brownout", bc=2, ur=0, ap=0, cl=0, ps=0)
        texts = self.events("flowlocal")
        self.assertIn("FlowLocal이 스스로 다시 켜졌어요 (이유: 전원 부족, 전원을 넣은 뒤 2번째 켜짐, 2초 전)", texts)
        self.assertFalse(any(t.startswith("FlowLocal이 다시 켜졌어요") for t in texts))   # one line, not two
        d = self.diag()
        self.assertEqual(d["code"], "FL_STARTING")
        self.assertIn("FL_SELF_RESTART", [o["code"] for o in d["others"]])
        self.replies(50, rsn="brownout", bc=2, ur=0)
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("FL_SELF_RESTART", "warn"))
        self.assertEqual(d["headline"], "FlowLocal이 스스로 다시 켜졌어요 (이유: 전원 부족)")
        self.assertIn("테이프", d["steps"][0])
        self.assertIn("충전기", d["steps"][1])
        self.assertIn("담당자", d["steps"][2])
        self.assertIn("스스로 회복했어요", d["detail"])
        self.assertIn("전원을 넣은 뒤 2번째", m.build_view(self.snap())["uptime"]["note"])
        self.adv(m.SELF_RESTART_NOTE_S)
        self.replies(4, rsn="brownout", bc=2, ur=0)
        self.assertEqual(self.diag()["code"], "ALL_OK")         # the note is over; the log keeps it

    def test_watchdog_restart(self):
        self.healthy()
        self.adv(30); self.start(2, rsn="task_wdt", bc=3, why="bridge_hung")
        self.assertIn("FlowLocal이 스스로 다시 켜졌어요 (이유: 펌웨어 멈춤·작업 감시, 기록: MIDI 전달이 멈춤, 전원을 넣은 뒤 3번째 켜짐, 2초 전)",
                      self.events("flowlocal"))
        self.replies(50, rsn="task_wdt", bc=3, why="bridge_hung")
        d = self.diag()
        self.assertEqual(d["code"], "FL_SELF_RESTART")
        self.assertIn("지금 할 일은 없어요", d["steps"][0])
        self.assertIn("담당자에게", d["steps"][1])
        self.assertIn("bridge_hung", d["detail"])

    def test_a_restart_after_power_on_counts_by_bc_alone(self):
        self.healthy()
        self.adv(30); self.start(2, rsn="sw", bc=2)
        self.assertTrue(any("스스로 다시 켜졌어요 (이유: 펌웨어가 다시 켬" in t for t in self.events("flowlocal")))
        self.assertEqual(self.snap()["fl_restart"]["kind"], "other")

    def test_a_software_restart_is_named_by_its_note(self):
        # flx-mon2 restarts itself when no status reply gets out (fl_recovery.h UsbWatch)
        self.healthy()
        self.adv(30); self.start(2, rsn="sw", bc=2, why="usb_wedged")
        self.assertTrue(any("스스로 다시 켜졌어요 (이유: USB로 대답이 안 나감" in t for t in self.events("flowlocal")))
        self.replies(50, rsn="sw", bc=2, why="usb_wedged")
        d = self.diag()
        self.assertEqual((d["code"], d["headline"]), ("FL_SELF_RESTART", "FlowLocal이 스스로 다시 켜졌어요 (이유: USB로 대답이 안 나감)"))
        self.assertIn("지금 할 일은 없어요", d["steps"][0])
        self.assertIn("USB로 대답이 안 나감 (usb_wedged)", d["detail"])

    def test_a_restart_someone_asked_for_is_not_by_itself(self):
        # why=web: the status page's Reboot; why=cmd12: the monitor's command, even when
        # this monitor run did not see the press
        for why, word in (("web", "상태 페이지의 Reboot"), ("cmd12", "'FlowLocal 다시 켜기' 버튼")):
            with self.subTest(why):
                self.setUp()
                self.healthy()
                self.adv(30); self.start(3, rsn="sw", bc=2, why=why)
                texts = self.events("flowlocal")
                self.assertFalse(any("스스로" in t for t in texts))
                self.assertIn("FlowLocal이 다시 켜졌어요 (켜진 지 3초, 이유: %s)" % word, texts)
                self.assertIsNone(self.snap()["fl_restart"])
                self.assertEqual(self.snap()["flap"]["count"], 0)

    def test_a_restart_by_the_button_is_not_by_itself(self):
        self.healthy()
        self.assertIsNone(self.tr.fix_begin("reboot_flowlocal", self.clk.t))
        self.adv(0.01); self.tr.fix_sent(True, self.clk.t)
        self.adv(0.05); self.ack(0x12, True, "restarting: USB back in seconds, hotspot in about 13 s")
        self.adv(0.3); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(2.5); self.port_up()
        self.adv(1); self.start(3, rsn="sw", bc=2)
        texts = self.events("flowlocal")
        self.assertFalse(any("스스로" in t for t in texts))
        self.assertIn("FlowLocal이 다시 켜졌어요 (켜진 지 3초, 이유: 펌웨어가 다시 켬)", texts)
        self.assertIsNone(self.snap()["fl_restart"])

    def test_usb_reconnects(self):
        self.system_ok(); self.port_up()
        self.start(100, ur=0)
        self.adv(2); self.reply(up=102, ur=1, why="bridge_hung")
        self.assertIn("FlowLocal이 USB 연결을 스스로 다시 맺었어요 (켜진 뒤 1번, ur 0→1, 기록: bridge_hung)",
                      self.events("flowlocal"))
        self.adv(2); self.reply(up=104, ur=1)
        self.assertEqual(len([t for t in self.events("flowlocal") if "USB 연결을" in t]), 1)

    def test_logged_once_across_monitor_restarts(self):
        self.system_ok(); self.port_up()
        self.start(600, rsn="brownout", bc=2)
        self.assertEqual(sum("스스로" in t for t in self.events("flowlocal")), 1)
        tr2 = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True, memory=self.tr.memory)
        tr2.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, self.clk.t)
        tr2.on_opened(True, True, None, self.clk.t)
        self.adv(30)
        tr2.on_midi(reply_bytes(up=630, rsn="brownout", bc=2), self.clk.t)
        self.assertFalse(any("스스로" in e["text"] for e in tr2.events.recent(50)))
        self.assertIsNotNone(tr2.snapshot()["fl_restart"])      # still a note: it was ten minutes ago


# ------------------------------------------------ power settings: this laptop's real answers

class PowerSettingsTests(unittest.TestCase):
    GUIDS = {"sleep": "29f6c1db-86da-48c5-9fdb-f2b67b1f44da", "hibernate": "9d7815a6-7ee4-497e-8888-515a05f02364",
             "lid": "5ca83367-6e45-459f-a27b-476b1d01c936", "usb_suspend": "48e6b7a6-50f5-4782-a5d4-53bb8f07e226",
             "aspm": "ee12f906-d277-404b-b6da-e5fa1a576df5"}

    def test_the_checks_are_these_five_by_guid(self):
        self.assertEqual({c[0]: c[2] for c in m.POWER_CHECKS}, self.GUIDS)
        self.assertEqual({c[0]: c[1] for c in m.POWER_CHECKS}["aspm"], "501a4d13-42af-4429-9fd1-a8218c268e20")

    def test_real_korean_answers(self):
        cases = ((PCFG_STANDBY, "sleep", (0, 0)), (PCFG_HIBERNATE, "hibernate", (0, 18000)),
                 (PCFG_LID_QH, "lid", (1, 1)), (PCFG_USB, "usb_suspend", (0, 0)), (PCFG_ASPM, "aspm", (2, 2)))
        for text, key, want in cases:
            with self.subTest(key):
                guid = self.GUIDS[key]
                self.assertEqual(m.parse_powercfg_ac_dc(text, guid), want)
                self.assertEqual(m.parse_powercfg_ac_dc(text, guid.upper()), want)
                raw = text.encode("cp949")                        # what powercfg writes (OEM codepage 949)
                self.assertEqual(m.parse_powercfg_ac_dc(m._decode(raw), guid), want)
                # a machine with another codepage garbles the Korean, not the GUIDs and numbers
                self.assertEqual(m.parse_powercfg_ac_dc(raw.decode("latin-1"), guid), want)
        self.assertIsNone(m.parse_powercfg_ac_dc(PCFG_LID_Q, self.GUIDS["lid"]))      # hidden: /q lists nothing
        self.assertIsNone(m.parse_powercfg_ac_dc(PCFG_BAD_PARAM, self.GUIDS["aspm"]))

    @staticmethod
    def answer(text, rc=0):
        return types.SimpleNamespace(returncode=rc, stdout=text.encode("cp949"), stderr=b"")

    def test_check_power_settings_on_this_laptop(self):
        by_guid = {"29f6c1db": PCFG_STANDBY, "9d7815a6": PCFG_HIBERNATE, "48e6b7a6": PCFG_USB, "ee12f906": PCFG_ASPM}
        calls = []

        def run(args, **kw):
            calls.append(list(args))
            if args[4].startswith("5ca83367"):
                return self.answer(PCFG_LID_QH if args[1] == "/qh" else PCFG_LID_Q)
            return self.answer(by_guid[args[4][:8]])
        items = {i["key"]: i for i in m.check_power_settings(run)}
        self.assertEqual({k: (i["ac"], i["dc"]) for k, i in items.items()},
                         {"sleep": (0, 0), "hibernate": (0, 18000), "lid": (1, 1), "usb_suspend": (0, 0), "aspm": (2, 2)})
        self.assertEqual({k for k, i in items.items() if i["ok"] is False}, {"hibernate", "lid", "aspm"})
        self.assertEqual({k for k, i in items.items() if i["dc_ok"] is False}, {"hibernate", "lid", "aspm"})
        self.assertEqual(items["hibernate"]["dc_text"], "5시간 후")
        self.assertEqual(items["aspm"]["ac_text"], "최대 절전")
        self.assertTrue(all(c[:3] == ["powercfg", "/qh", "SCHEME_CURRENT"] for c in calls))
        self.assertEqual(len(calls), 5)                           # /qh answered each: /q was not needed
        snap = {"power": {"has_battery": True}, "power_settings": {"items": list(items.values())}}
        self.assertEqual([i["key"] for i in m.staff_power_problems(snap)], ["hibernate", "lid", "aspm"])

    def test_check_power_settings_copes(self):
        def run(args, **kw):
            if args[1] == "/qh":
                return types.SimpleNamespace(returncode=1, stdout=b"", stderr=PCFG_BAD_PARAM.encode("cp949"))
            if args[4].startswith("5ca83367"):
                return self.answer(PCFG_LID_Q)                    # hidden and /qh failed: unknown
            if args[4].startswith("ee12f906"):
                raise OSError("powercfg is missing")
            return self.answer(PCFG_STANDBY)
        items = {i["key"]: i for i in m.check_power_settings(run)}
        self.assertEqual((items["sleep"]["ac"], items["sleep"]["dc"], items["sleep"]["ok"]), (0, 0, True))
        for key in ("hibernate", "lid", "usb_suspend", "aspm"):
            self.assertIsNone(items[key]["ok"], key)
            self.assertEqual(items[key]["ac_text"], "모름")
        snap = {"power": {"has_battery": True}, "power_settings": {"items": list(items.values())}}
        self.assertEqual(m.staff_power_problems(snap), [])        # unknown is not a problem

    def test_problem_text(self):
        i = m.power_item(m.POWER_CHECKS[1], (0, 18000))
        self.assertEqual(m.power_problem_text(i), "최대 절전 = 5시간 후 (배터리)")
        self.assertEqual(m.power_problem_text(i, has_battery=False), "")
        self.assertEqual(m.power_item_problem(i, False), (False, False))
        j = m.power_item(m.POWER_CHECKS[4], (2, 2))
        self.assertEqual(m.power_problem_text(j), "PCI Express 링크 상태 전원 관리 = 최대 절전 (전원 연결·배터리)")


# ------------------------------------------------ the one-click power settings file

class PowerScriptTests(unittest.TestCase):
    """노트북 전시용 전원 설정.bat at the repo root. Read, never run."""

    PATH = os.path.join(REPO, m.POWER_SCRIPT)

    def test_the_file(self):
        with open(self.PATH, "rb") as f:
            raw = f.read()
        self.assertTrue(all(b < 128 for b in raw))                # cmd reads it in the OEM codepage
        self.assertEqual(raw.count(b"\n"), raw.count(b"\r\n"))    # CRLF: cmd mis-reads labels in LF files
        text = raw.decode("ascii").replace("\r\n", "\n")
        names = dict(re.findall(r'^set "([A-Z_]+)=([0-9a-f-]{36})"$', text, re.M))
        applied = {(names[s], names[g]) for s, g in re.findall(r'^call :apply "[^"]+" %([A-Z_]+)% %([A-Z_]+)%$', text, re.M)}
        self.assertEqual(applied, {(c[1], c[2]) for c in m.POWER_CHECKS})     # exactly what the monitor checks
        shown = {(names[s], names[g]) for s, g in re.findall(r'^call :show "[^"]+" %([A-Z_]+)% %([A-Z_]+)%$', text, re.M)}
        self.assertEqual(shown, applied)
        lines = text.split("\n")
        for want in ("powercfg /setacvalueindex SCHEME_CURRENT %2 %3 0", "powercfg /setdcvalueindex SCHEME_CURRENT %2 %3 0",
                     "powercfg /setactive SCHEME_CURRENT", "pause", "exit /b 0"):
            self.assertIn(want, lines)
        self.assertLess(lines.index("powercfg /setactive SCHEME_CURRENT"), lines.index("pause"))
        # the display stays as it is; nothing that needs an administrator
        for never in ("/hibernate", "monitor-timeout", "VIDEOIDLE", "3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e",
                      "/setsecuritydescriptor", "/change", "reg add"):
            self.assertNotIn(never, text)
        self.assertIn("Run as administrator", text)               # said, if a computer refuses


# ------------------------------------------------ Live: started again after it quit

class FakeLauncher(object):
    def __init__(self):
        self.calls = []
        self.results = []

    def __call__(self, set_path, exe=None, prefer_exe=False):
        self.calls.append((set_path, exe, prefer_exe))
        return self.results.pop(0) if self.results else (True, "exe" if prefer_exe else "set", None)


class LiveKeeperTests(Clocked, unittest.TestCase):
    """LiveKeeper against injected process lists and a fake launcher: no
    Live is ever stopped or started by these tests."""

    CFG = dict(m.CONFIG_DEFAULTS, live_recovery_button="No")

    def setUp(self):
        super().setUp()
        self.launcher = FakeLauncher()
        self.crash = None
        self.down = False
        self.tr.keeper = m.LiveKeeper(self.tr.events, self.launcher, lambda since_s: self.crash, lambda: self.down)
        self.healthy()

    def look(self, running, cfg=None):
        """A tasklist check and the keeper's step, as the Supervisor does."""
        self.tr.on_live(running, ["Ableton Live 12 Suite.exe"] if running else [], self.clk.t,
                        m.DEMO_LIVE_EXE if running else None)
        self.tr.keep_live(self.clk.t, cfg or self.CFG)

    def keeper(self):
        return self.snap()["live_keeper"]

    def crash_and_start(self):
        self.look(False)
        self.adv(9); self.reply(); self.look(False)

    def come_up_and_load(self, rr=None):
        self.adv(10); self.reply(); self.look(True)
        for _ in range(31):
            self.adv(2); self.reply(**({} if rr is None else {"rr": rr})); self.look(True)

    def test_a_crash_then_a_start_after_the_grace(self):
        self.crash = dict(m.DEMO_LIVE_CRASH)
        self.look(True)                                           # Live running: its exe is noted
        self.look(False)
        self.assertEqual(self.keeper()["phase"], "gone")
        d = self.diag(self.CFG)
        self.assertEqual((d["code"], d["wait"]), ("LIVE_RESTARTING", True))
        self.assertEqual(d["headline"], "음악 프로그램(Ableton Live)이 꺼져서 다시 켜는 중이에요… 1분쯤 걸려요")
        self.assertIn("'No' 버튼", d["steps"][1])
        self.adv(6); self.look(False)
        self.assertEqual(self.launcher.calls, [])                # still within 8 s: maybe started by hand
        self.adv(2.5); self.look(False)
        self.assertEqual(len(self.launcher.calls), 1)
        set_path, exe, prefer = self.launcher.calls[0]
        self.assertEqual(set_path, os.path.normpath(os.path.join(REPO, "set Project", "set.als")))
        self.assertEqual((exe, prefer), (m.DEMO_LIVE_EXE, False))
        texts = self.events("live")
        self.assertIn("음악 프로그램(Ableton Live)이 충돌로 꺼졌어요 (Windows 기록: rthdasio64.dll, c0000005, 이벤트 1000)", texts)
        self.assertIn("음악 프로그램을 다시 켜요 (파일 연결로 'set.als', 15분 안에 1번째)", texts)
        self.assertEqual(self.keeper()["phase"], "launching")
        self.adv(10); self.reply(); self.look(True)
        self.assertEqual(self.keeper()["phase"], "loading")
        d = self.diag(self.CFG)
        self.assertEqual(d["code"], "LIVE_RESTARTING")
        self.assertIn("세트를 여는 중", d["headline"])
        self.assertEqual(d["chain"][5]["text"], "세트 여는 중")

    def test_no_crash_record_still_starts_it(self):
        # 2026-09-25 21:44: Live vanished and Windows wrote nothing
        self.crash_and_start()
        self.assertEqual(len(self.launcher.calls), 1)
        self.assertIn("음악 프로그램(Ableton Live)이 꺼졌어요 (Windows 충돌 기록은 없어요)", self.events("live"))

    def test_back_by_hand_within_the_grace(self):
        self.look(False)
        self.adv(4); self.look(True)
        self.adv(10); self.reply(); self.look(True)
        self.assertEqual(self.launcher.calls, [])
        self.assertEqual(self.keeper()["phase"], "idle")
        self.assertEqual(self.diag(self.CFG)["code"], "ALL_OK")

    def test_the_set_opening_shows_in_the_resync_counter(self):
        self.crash_and_start()
        self.adv(12); self.reply(); self.look(True)              # up: the set is loading (rr 4)
        self.adv(3); self.reply(rr=5); self.look(True)           # the Live device asked FlowLocal for a resync
        k = self.keeper()
        self.assertEqual((k["phase"], k["done_confirmed"]), ("idle", True))
        self.assertIn("음악 프로그램이 다시 켜지고 세트가 열렸어요 (15초 걸림)", self.events("live"))   # start to set
        self.assertEqual(self.diag(self.CFG)["code"], "ALL_OK")  # no "listen" note: the set is open

    def test_an_unconfirmed_start_is_a_yellow_note(self):
        self.crash_and_start()
        self.come_up_and_load()
        k = self.keeper()
        self.assertEqual((k["phase"], k["done_confirmed"], k["done_took"]), ("idle", False, 10))
        d = self.diag(self.CFG)
        self.assertEqual((d["code"], d["level"]), ("LIVE_RESTARTED", "warn"))
        self.assertIn("'No' 버튼", d["steps"][0])
        self.adv(m.LIVE_RESTARTED_NOTE_S); self.reply(); self.look(True)
        self.assertEqual(self.diag(self.CFG)["code"], "ALL_OK")

    def test_the_resync_button_is_no_proof_of_the_set(self):
        self.crash_and_start()
        self.adv(10); self.reply(); self.look(True)
        self.assertIsNone(self.tr.fix_begin("resync", self.clk.t))
        self.tr.fix_sent(True, self.clk.t)
        self.adv(2); self.reply(rr=5); self.look(True)
        self.assertEqual(self.keeper()["phase"], "loading")

    def test_switch_off_means_never(self):
        off = dict(self.CFG, live_autorestart=False)
        self.look(False, off)
        self.assertEqual(self.diag(off)["code"], "LIVE_NOT_RUNNING")   # not "restarting": it will not
        self.adv(9); self.look(False, off)
        self.adv(120); self.reply(); self.look(False, off)
        self.assertEqual(self.launcher.calls, [])
        k = self.keeper()
        self.assertEqual((k["phase"], k["stop_reason"]), ("stopped", "off"))
        d = self.diag(off)
        self.assertEqual((d["code"], d["level"]), ("LIVE_NOT_RUNNING", "bad"))
        self.assertIn("자동 다시 켜기는 꺼져 있어요", d["detail"])
        self.assertIn("자동 다시 켜기가 꺼져 있어서 음악 프로그램을 다시 켜지 않아요", self.events("live"))
        self.assertEqual(d["chain"][5]["text"], "꺼짐")
        # turned on again while Live is gone: started after the grace
        self.adv(2); self.look(False)
        self.assertEqual(self.launcher.calls, [])
        self.adv(9); self.look(False)
        self.assertEqual(len(self.launcher.calls), 1)

    def test_at_most_three_starts_in_fifteen_minutes(self):
        for _ in range(3):
            self.crash_and_start()
            self.come_up_and_load()
            self.adv(60)
        self.assertEqual(len(self.launcher.calls), 3)
        self.crash_and_start()                                    # the fourth time
        self.assertEqual(len(self.launcher.calls), 3)
        k = self.keeper()
        self.assertEqual((k["phase"], k["stop_reason"], k["starts_window"]), ("stopped", "limit", 3))
        self.reply()
        d = self.diag(self.CFG)
        self.assertEqual((d["code"], d["level"]), ("LIVE_GAVE_UP", "bad"))
        self.assertEqual(d["headline"], "Live가 계속 꺼져요 — 담당자에게 연락해 주세요")
        self.assertTrue(any(t.startswith("음악 프로그램이 계속 꺼져요 (15분 안에 3번 다시 켰어요)") for t in self.events("live")))
        self.adv(120); self.reply(); self.look(False)
        self.assertEqual(len(self.launcher.calls), 3)            # it stays stopped
        self.look(True)                                           # someone started it by hand
        self.assertEqual(self.keeper()["phase"], "idle")
        self.assertIn("음악 프로그램이 다시 보여요 (누가 다시 켰어요)", self.events("live"))
        self.adv(m.LIVE_RESTART_WINDOW_S)
        self.crash_and_start()                                    # a new quarter of an hour
        self.assertEqual(len(self.launcher.calls), 4)

    def test_a_start_that_fails_stops_and_says_why(self):
        self.launcher.results = [(False, None, "전시 세트 파일이 없어요 (X)")]
        self.crash_and_start()
        k = self.keeper()
        self.assertEqual((k["phase"], k["stop_reason"]), ("stopped", "error"))
        d = self.diag(self.CFG)
        self.assertEqual(d["code"], "LIVE_NOT_RUNNING")
        self.assertIn("전시 세트 파일이 없어요", d["detail"])
        self.adv(60); self.reply(); self.look(False)
        self.assertEqual(len(self.launcher.calls), 1)            # not again on its own

    def test_no_show_tries_the_exe(self):
        self.crash_and_start()
        for _ in range(46):
            self.adv(2); self.look(False)                         # 92 s: Live never came up
        self.assertEqual([c[2] for c in self.launcher.calls], [False, True])
        self.assertIn("음악 프로그램이 90초 안에 뜨지 않았어요", self.events("live"))
        self.assertEqual(self.keeper()["starts_window"], 2)

    def test_never_seen_is_left_to_the_laptop(self):
        tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True,
                       keeper=m.LiveKeeper(m.EventLog(None), self.launcher))
        for _ in range(10):
            tr.on_live(False, [], self.clk.t); tr.keep_live(self.clk.t, self.CFG); self.adv(2)
        self.assertEqual(self.launcher.calls, [])
        self.assertEqual(tr.keeper.phase, "idle")

    def test_not_while_windows_shuts_down(self):
        self.down = True
        self.crash_and_start()
        self.assertEqual(self.launcher.calls, [])
        self.assertEqual(self.keeper()["stop_reason"], "shutdown")
        self.down = False                                         # the shutdown was called off
        self.adv(2); self.look(False)
        self.adv(9); self.look(False)
        self.assertEqual(len(self.launcher.calls), 1)

    def test_the_default_keeper_starts_nothing(self):
        tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True)
        tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t)
        for _ in range(6):
            self.adv(2); tr.on_live(False, [], self.clk.t); tr.keep_live(self.clk.t, self.CFG)
        k = tr.snapshot()["live_keeper"]
        self.assertEqual((k["phase"], k["stop_reason"]), ("stopped", "error"))

    def test_the_exe_is_remembered_while_live_runs(self):
        self.look(True)
        self.assertEqual(self.snap()["live"]["exe"], m.DEMO_LIVE_EXE)
        self.tr.on_live(False, [], self.clk.t, None)
        self.assertEqual(self.snap()["live"]["exe"], m.DEMO_LIVE_EXE)


class LiveHelperTests(unittest.TestCase):
    def test_crash_records_as_windows_wrote_them(self):
        recs = m.live_crash_records(WEV_1001 + WEV_1000)
        self.assertEqual([(r["id"], r["kind"], r["module"], r["code"]) for r in recs],
                         [(1001, "crash", "rthdasio64.dll", "c0000005"), (1000, "crash", "rthdasio64.dll", "c0000005")])
        # the 21:50 WER report is the 16:17 crash sent late
        self.assertEqual(recs[0]["report"], recs[1]["report"])
        self.assertEqual(recs[1]["time"], "2026-09-25T07:17:43.1624750Z")
        self.assertEqual(m.pick_crash_record(recs)["id"], 1000)
        self.assertEqual(m.pick_crash_record(m.live_crash_records(WEV_1001))["id"], 1001)
        raw = (WEV_1001 + WEV_1000).encode("cp949")               # wevtutil writes the OEM codepage
        self.assertEqual(len(m.live_crash_records(m._decode(raw))), 2)
        self.assertEqual(len(m.live_crash_records(raw.decode("latin-1"))), 2)

    def test_other_records_and_broken_text(self):
        self.assertEqual(m.live_crash_records(WEV_1000.replace("Ableton Live 12 Suite.exe", "svchost.exe")), [])
        self.assertEqual(m.live_crash_records(WEV_1000[:300]), [])
        self.assertEqual(m.live_crash_records(""), [])
        self.assertIsNone(m.pick_crash_record([]))
        hang = ("<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Application "
                "Hang'/><EventID>1002</EventID><TimeCreated SystemTime='2026-09-25T13:00:00.0Z'/></System><EventData>"
                "<Data>Ableton Live 12 Suite.exe</Data><Data>1.0.0.1</Data></EventData></Event>")
        recs = m.live_crash_records(hang)
        self.assertEqual([(r["id"], r["kind"]) for r in recs], [(1002, "hang")])
        self.assertIn("멈춰서", m.live_gone_text(recs[0]))
        self.assertIn("기록은 없어요", m.live_gone_text(None))

    def test_settings(self):
        self.assertTrue(m.live_autorestart_on({}))
        self.assertTrue(m.live_autorestart_on({"live_autorestart": True}))
        for off in (False, "false", "False", "0", "off", 0):
            self.assertFalse(m.live_autorestart_on({"live_autorestart": off}), off)
        self.assertEqual(m.live_set_path({}), os.path.normpath(os.path.join(REPO, "set Project", "set.als")))
        self.assertEqual(m.live_set_path({"live_set": ""}), m.live_set_path({}))
        self.assertEqual(m.live_set_path({"live_set": r"D:\Exhibit\PF.als"}), os.path.normpath(r"D:\Exhibit\PF.als"))
        self.assertTrue(os.path.isfile(m.live_set_path(m.Config().get())))   # the repo's config opens the repo's set

    def test_tasklist_pids_and_paths(self):
        self.assertEqual(m.live_process_rows(TASKLIST), [("Ableton Live 12 Suite.exe", 10220)])
        self.assertIsNone(m.process_path(None))
        if os.name == "nt":
            self.assertEqual(os.path.normcase(m.process_path(os.getpid())), os.path.normcase(sys.executable))


class LaunchLiveTests(unittest.TestCase):
    """launch_live with os.startfile and subprocess.Popen replaced: nothing is started."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.set = os.path.join(self.tmp.name, "set.als")
        self.exe = os.path.join(self.tmp.name, "Ableton Live 12 Suite.exe")
        for p in (self.set, self.exe):
            open(p, "wb").close()
        self.calls = []

    def tearDown(self):
        self.tmp.cleanup()

    def launch(self, startfile_ok=True, **kw):
        from unittest import mock

        def startfile(p):
            self.calls.append(("startfile", p))
            if not startfile_ok:
                raise OSError("no application is associated")

        def popen(args, **k):
            self.calls.append(("popen", list(args)))
            return types.SimpleNamespace(pid=1)
        with mock.patch.object(m.os, "startfile", startfile, create=True), mock.patch.object(m.subprocess, "Popen", popen):
            return m.launch_live(**kw)

    def test_the_set_by_its_file_association(self):
        self.assertEqual(self.launch(set_path=self.set, exe=self.exe), (True, "set", None))
        self.assertEqual(self.calls, [("startfile", self.set)])

    def test_the_exe_when_the_association_fails(self):
        self.assertEqual(self.launch(startfile_ok=False, set_path=self.set, exe=self.exe), (True, "exe", None))
        self.assertEqual(self.calls, [("startfile", self.set), ("popen", [self.exe, self.set])])

    def test_the_exe_first_when_asked(self):
        self.assertEqual(self.launch(set_path=self.set, exe=self.exe, prefer_exe=True), (True, "exe", None))
        self.assertEqual(self.calls, [("popen", [self.exe, self.set])])

    def test_nothing_works(self):
        ok, how, err = self.launch(startfile_ok=False, set_path=self.set, exe=None)
        self.assertEqual((ok, how), (False, None))
        self.assertIn("associated", err)
        ok, how, err = self.launch(set_path=os.path.join(self.tmp.name, "missing.als"), exe=self.exe)
        self.assertEqual((ok, how), (False, None))
        self.assertIn("전시 세트 파일이 없어요", err)
        self.assertEqual(self.calls, [("startfile", self.set)])  # nothing tried for a set that is not there


class ConfigWriteTests(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "config.json")
        with open(m.CONFIG, encoding="utf-8") as src, open(self.path, "w", encoding="utf-8") as dst:
            dst.write(src.read())

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_switch_keeps_the_rest(self):
        c = m.Config(self.path)
        self.assertIs(c.get()["live_autorestart"], True)
        self.assertEqual(c.set_value("live_autorestart", False), (True, None))
        self.assertIs(c.get()["live_autorestart"], False)
        with open(self.path, encoding="utf-8") as f:
            data = json.load(f)
        self.assertEqual(list(data)[0], "_설명")
        self.assertEqual(len(data["contacts"]), 2)
        self.assertEqual(data["live_set"], "set Project/set.als")
        self.assertFalse(os.path.exists(self.path + ".tmp"))

    def test_a_broken_file_is_left_alone(self):
        with open(self.path, "w", encoding="utf-8") as f:
            f.write("{ not json")
        ok, err = m.Config(self.path).set_value("live_autorestart", False)
        self.assertFalse(ok)
        self.assertIn("메모장", err)
        with open(self.path, encoding="utf-8") as f:
            self.assertEqual(f.read(), "{ not json")

    def test_live_app_switch(self):
        app = m.LiveApp.__new__(m.LiveApp)
        app.config, app.events = m.Config(self.path), m.EventLog(None)
        res = app.set_live_autorestart(False)
        self.assertEqual((res["ok"], res["on"]), (True, False))
        self.assertIs(app.config.get()["live_autorestart"], False)
        self.assertIn("자동 다시 켜기를 껐어요", app.events.recent(1)[0]["text"])
        self.assertTrue(app.set_live_autorestart(True)["on"])


class SwitchHttpTests(unittest.TestCase):
    """/api/live_autorestart on a demo server (the demo never writes config.json)."""

    setUpClass = classmethod(HttpTests.setUpClass.__func__)      # its own demo server
    tearDownClass = classmethod(HttpTests.tearDownClass.__func__)
    req = HttpTests.req

    def test_the_switch(self):
        self.assertEqual(self.req("POST", "/api/live_autorestart?on=0")[0], 403)                 # not from the page
        self.assertEqual(self.req("POST", "/api/live_autorestart?on=2", {"X-PF-Monitor": "1"})[0], 400)
        self.assertEqual(self.req("POST", "/api/live_autorestart?on=0", {"X-PF-Monitor": "1"}, host="evil.example")[0], 403)
        st, body = self.req("POST", "/api/live_autorestart?on=0", {"X-PF-Monitor": "1"})
        self.assertEqual(st, 200)
        self.assertIs(json.loads(body.decode("utf-8"))["on"], False)
        d = json.loads(self.req("GET", "/api/state?scenario=all_ok")[1].decode("utf-8"))
        self.assertIs(d["live_autorestart"]["on"], False)
        self.assertTrue(d["live_autorestart"]["set_exists"])
        self.req("POST", "/api/live_autorestart?on=1", {"X-PF-Monitor": "1"})
        d = json.loads(self.req("GET", "/api/state?scenario=live_restarting")[1].decode("utf-8"))
        self.assertEqual(d["diagnosis"]["code"], "LIVE_RESTARTING")
        self.assertEqual(d["snap"]["live_keeper"]["phase"], "loading")
        with open(m.CONFIG, encoding="utf-8") as f:
            self.assertIs(json.load(f)["live_autorestart"], True)   # untouched by the demo


# ------------------------------------------------ evidence for next time: counters and connections in the log

class EvidenceTests(Clocked, unittest.TestCase):
    """Lines in logs/YYYY-MM-DD.log (the dashboard shows only some of them)."""

    def setUp(self):
        super().setUp()
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.tr.events = m.EventLog(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def lines(self, kind=None):
        path = self.tr.events.today_path()
        if not os.path.exists(path):
            return []
        with open(path, encoding="utf-8") as f:
            rows = [re.match(r"^\S+ \S+ \[(\w+)\] (?:\([^)]*\) )?(.*)$", line.rstrip("\n")) for line in f]
        return [r.group(2) for r in rows if r and (kind is None or r.group(1) == kind)]

    def test_counters_are_written_with_their_numbers(self):
        self.healthy()
        self.adv(2); self.reply(rj=1, to=0)
        self.adv(2); self.reply(rj=1, to=1, se=3)
        texts = self.lines("counter")
        self.assertIn("FlowLocal이 패널의 연결 요청(초대)을 거절했어요 (rj 0→1)", texts)
        self.assertIn("FlowLocal이 조용해진 패널 연결을 시간 초과로 끝냈어요 (to 0→1)", texts)
        self.assertIn("패널이 연결을 새로 열었어요 (두 상태 답 사이라 끊김이 화면에 안 보였어요) (se 2→3)", texts)
        levels = {e["text"][:20]: e["level"] for e in self.tr.events.recent(50) if e["kind"] == "counter"}
        self.assertIn("warn", levels.values())
        # the same numbers again: nothing new; a FlowLocal restart (counters from 0): nothing either
        n = len(self.lines("counter"))
        self.adv(2); self.reply(rj=1, to=1, se=3)
        self.adv(30); self.reply(up=2, rj=0, to=0, se=0, ap=0, cl=0, ps=0)
        self.adv(2); self.reply(up=4, rj=0, to=0, se=0, ap=0, cl=0, ps=0)
        self.assertEqual(len(self.lines("counter")), n)

    def test_a_visible_new_session_keeps_se_in_the_file(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=1, se=2)
        self.adv(2); self.reply(ps=1, cl=1, se=3)
        self.assertIn("패널 연결이 새로 열렸어요 (se 2→3)", self.lines("counter"))          # in the file ...
        self.assertFalse(any(e["kind"] == "counter" for e in self.tr.events.recent(50)))   # ... "패널이 연결됐어요" on the page

    def test_packet_gaps_are_summed_once_a_minute(self):
        self.healthy()
        gp = 3
        for i in range(40):                                          # 80 s, a gap every 4 s
            self.adv(2); gp += i % 2; self.reply(gp=gp)
        texts = [t for t in self.lines("counter") if "gp" in t]
        self.assertEqual(len(texts), 2)                              # the first gap at once, then the next minute's
        self.assertEqual(texts[0], "무선에서 RTP 패킷이 빠졌어요: 1개 (gp 3→4)")
        self.assertRegex(texts[1], r"^무선에서 RTP 패킷이 빠졌어요: \d+개, 최근 1분 동안 \(gp 4→\d+\)$")

    def test_a_counter_this_monitor_does_not_know_yet(self):
        # flx-mon3 may add e.g. a counter of evicted sessions: written when it grows, never judged
        self.healthy()
        self.adv(2); self.reply(ev=0)
        self.adv(2); self.reply(ev=1)
        self.adv(2); self.reply(ev=2)                                # within a minute: summed later
        self.assertEqual([t for t in self.lines("counter") if "'ev'" in t],
                         ["FlowLocal의 새 값 'ev'이(가) 늘었어요 (ev 0→1, 이 모니터가 아직 모르는 값)"])
        self.adv(61); self.reply(ev=2)
        self.assertIn("FlowLocal의 새 값 'ev'이(가) 늘었어요 (ev 1→2, 이 모니터가 아직 모르는 값)", self.lines("counter"))
        # one that goes down without a restart is no counter: never written again
        self.adv(2); self.reply(ev=0)
        self.adv(70); self.reply(ev=9)
        self.assertFalse(any("ev 0→9" in t or "ev 2→9" in t for t in self.lines("counter")))
        self.assertEqual(self.diag()["code"], "ALL_OK")

    def test_the_channel_after_each_start_and_when_it_changes(self):
        self.healthy()
        self.adv(2); self.reply(fw="flx-mon3", ch=6, chm="auto", cs="1:210,6:95,11:380")
        self.adv(30); self.reply(up=3, fw="flx-mon3", ap=0, ch="", cl=0, ps=0, cs="1:210,6:95,11:380")
        self.adv(12); self.reply(up=15, fw="flx-mon3", ch=11, chm="manual", cl=0, ps=0, cs="")
        self.adv(2); self.reply(up=17, fw="flx-mon3", ch=6, chm="auto", cs="1:40,6:20,11:90")
        texts = self.lines("channel")
        self.assertEqual(texts[0], "FlowLocal 핫스팟 채널: 1번 (자동 (켤 때 가장 조용한 채널))")
        self.assertIn("핫스팟 채널이 바뀌었어요: 1번 (자동 (켤 때 가장 조용한 채널)) → 6번 (자동 (켤 때 가장 조용한 채널))", texts)
        self.assertIn("FlowLocal 핫스팟 채널: 11번 (직접 고름 (운영자가 고정))", texts)          # after the restart
        self.assertIn("핫스팟 채널이 바뀌었어요: 11번 (직접 고름 (운영자가 고정)) → 6번 (자동 (켤 때 가장 조용한 채널))", texts)
        self.adv(40); self.reply(up=3, fw="flx-mon3", ap=1, ch=6, chm="auto", cs="1:40,6:20,11:90")
        self.assertEqual(self.lines("channel")[-1],
                         "FlowLocal 핫스팟 채널: 6번 (자동 (켤 때 가장 조용한 채널)), 켤 때 점수(낮을수록 조용): "
                         "1번 40 · 6번 20 · 11번 90 (cs=1:40,6:20,11:90)")

    def test_joined_to_session_time_is_written_and_a_slow_one_warned(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=0)
        self.adv(2); self.reply(ps=0, cl=1)                           # joined
        self.adv(2); self.reply(ps=1, cl=1)                           # a session 2 s later: normal
        self.adv(2); self.reply(ps=0, cl=0)
        self.adv(2); self.reply(ps=0, cl=1)                           # joined again
        for _ in range(10):
            self.adv(2); self.reply(ps=0, cl=1)                       # 20 s without a session (2026-09-27)
        self.adv(2); self.reply(ps=1, cl=1)
        self.adv(2); self.reply(ps=0, cl=0)
        self.adv(2); self.reply(ps=1, cl=1)                           # joined and invited between two replies
        rows = [(e["text"], e["level"]) for e in reversed(self.tr.events.recent(100)) if "핫스팟에 붙고" in e["text"]]
        self.assertEqual(rows, [
            ("패널이 핫스팟에 붙고 약 2초 뒤 연결됐어요 (cl 0→1, ps 0→1, 상태 답 간격 2초)", "info"),
            ("패널이 핫스팟에 붙고 약 22초 뒤 연결됐어요 (cl 0→1, ps 0→1, 상태 답 간격 2초) — 느려요: 보통 1–2초예요", "warn"),
            ("패널이 핫스팟에 붙고 2초 안에 연결됐어요 (cl 0→1, ps 0→1, 상태 답 간격 2초)", "info"),
        ])
        self.assertEqual([(c["s"], c["slow"]) for c in self.snap()["connects"]], [(2.0, False), (22.0, True), (0.0, False)])
        self.assertGreater(22.0, m.CONNECT_SLOW_S)

    def test_a_join_the_monitor_did_not_see_is_not_timed(self):
        self.system_ok(); self.port_up()
        self.reply(ps=0, cl=1)                                        # already on the hotspot when the monitor came
        self.adv(2); self.reply(ps=1, cl=1)
        self.assertFalse(any("핫스팟에 붙고" in t for t in self.lines("session")))


# ------------------------------------------------ the channel buttons (flx-mon3 command 13)

class ChannelButtonTests(Clocked, unittest.TestCase):
    MON3 = dict(fw="flx-mon3", ch=6, chm="auto", cs="1:210,6:95,11:380")

    def mon3(self, **over):
        self.reply(**dict(self.MON3, **over))

    def ready(self):
        self.system_ok(); self.port_up()
        self.adv(1); self.mon3()

    def press(self, action):
        r = self.tr.fix_begin(action, self.clk.t)
        if r is None:
            self.adv(0.01)
            self.tr.fix_sent(True, self.clk.t)
        return r

    def test_old_firmware_cannot(self):
        self.system_ok(); self.port_up()
        self.adv(1); self.reply(fw="flx-mon2", ch=6, chm="auto")
        for key in ("channel_next", "channel_auto"):
            a = self.action(key)
            self.assertEqual((a["enabled"], a["needs_fw"]), (False, True), key)
            self.assertEqual(a["reason"], "새 FlowLocal 펌웨어(flx-mon3)가 필요해요 (지금 flx-mon2)")
        self.assertTrue(self.action("reboot_flowlocal")["enabled"])     # the others are not held up

    def test_labels_and_blocks(self):
        self.ready()
        a = self.action("channel_next")
        self.assertEqual((a["enabled"], a["label_full"]), (True, "무선 채널 바꾸기 (지금 6번 → 다음 11번)"))
        self.assertIn("15초쯤", a["confirm"])                            # the confirmation says the pause
        b = self.action("channel_auto")
        self.assertEqual((b["enabled"], b["reason"]), (False, "이미 자동이에요"))
        self.adv(2); self.mon3(ch=11, chm="manual")
        self.assertEqual(self.action("channel_next")["label_full"], "무선 채널 바꾸기 (지금 11번 → 다음 1번)")
        self.assertTrue(self.action("channel_auto")["enabled"])
        self.adv(2); self.mon3(ap=0, ch="", cl=0, ps=0)                  # the hotspot is starting: no channel yet
        self.assertEqual(self.action("channel_next")["reason"], "지금 무선 채널을 몰라요 (핫스팟이 켜지는 중일 수 있어요)")

    def test_next_channel_flow(self):
        self.ready()
        self.assertIsNone(self.press("channel_next"))
        f = self.snap()["fix"]
        self.assertEqual((f["phase"], f["label"]), ("ack", "무선 채널 바꾸기 (6번 → 11번)"))
        self.assertTrue(any("F0 7D 50 46 13 43 48 0B F7" in t for t in self.events("button")))
        self.assertEqual(self.diag()["code"], "FIX_WAIT")
        self.adv(0.05); self.ack(0x13, True, "channel 11 saved, restarting")
        self.assertEqual(self.snap()["fix"]["phase"], "recover")
        self.assertIsNotNone(self.tr.fl_restart_ack_at)                 # the MIDI thread reopens the port
        self.adv(0.3); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.assertTrue(any("다시 켜지느라" in t for t in self.events("port")))
        self.assertEqual(self.diag()["code"], "FIX_WAIT")
        self.adv(1.6); self.port_up()
        self.adv(2); self.mon3(up=2, ap=0, ch="", cl=0, ps=0, chm="manual", cs="")
        self.adv(12); self.mon3(up=14, ap=1, ch=11, cl=1, ps=0, chm="manual", cs="")
        self.adv(2); self.mon3(up=16, ch=11, ps=1, chm="manual", cs="", se=1)
        f = self.snap()["fix"]
        self.assertEqual((f["result"], f["level"]), ("ok", "ok"))
        self.assertIn("무선 채널을 11번으로 바꿨어요", f["message"])
        # the restart was asked for: no "by itself" note, no flapping count
        self.assertIsNone(self.snap()["fl_restart"])
        self.assertEqual(self.snap()["flap"]["count"], 0)
        self.assertIn("FlowLocal 핫스팟 채널: 11번 (직접 고름 (운영자가 고정))", self.events("channel"))
        # the two buttons share their 30 s
        r = self.tr.fix_begin("channel_auto", self.clk.t)
        self.assertEqual(r[0], "too_soon")
        self.assertIn("초 뒤에 다시", self.action("channel_auto")["reason"])
        self.adv(m.ACTIONS["channel_auto"]["gap_s"])
        self.mon3(up=50, ch=11, chm="manual", cs="")
        self.assertTrue(self.action("channel_auto")["enabled"])

    def test_a_different_channel_after_the_restart_is_said(self):
        self.ready()
        self.press("channel_next")
        self.adv(0.05); self.ack(0x13, True, "channel 11 saved, restarting")
        self.adv(0.3); self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(1.6); self.port_up()
        self.adv(14); self.mon3(up=14, ch=6, ps=1)
        self.assertIn("그런데 채널이 6번이에요 (바꾸려던 채널: 11번)", self.snap()["fix"]["message"])

    def test_no_ack_from_a_firmware_before_flx_mon3(self):
        # a tag that says nothing: the press is allowed; the status answers, command 13 does not
        self.system_ok(); self.port_up()
        self.adv(1); self.reply(ch=6, chm="auto")
        self.assertTrue(self.action("channel_next")["enabled"])
        self.press("channel_next")
        self.adv(m.ACTIONS["channel_next"]["ack_s"] + 0.5); self.reply(ch=6)
        f = self.snap()["fix"]
        self.assertEqual((f["result"], f["level"]), ("no_ack", "warn"))
        self.assertIn("flx-mon3 전", f["message"])
        self.assertIn("아무것도 바뀌지 않았어요", f["message"])
        states = {a["id"]: a for a in m.action_states(self.snap())}
        self.assertTrue(states["channel_next"]["needs_fw"])
        self.assertTrue(states["channel_auto"]["needs_fw"])
        self.assertFalse(states["reboot_flowlocal"]["needs_fw"])          # only command 13 is unknown to it
        self.assertTrue(states["reboot_flowlocal"]["enabled"])
        self.assertTrue(m.button_usable(self.snap(), "reboot_flowlocal"))
        self.assertFalse(m.button_usable(self.snap(), "channel_auto"))

    def test_live_app_sends_one_channel_message_per_press(self):
        self.ready()
        app = m.LiveApp.__new__(m.LiveApp)
        app.tracker, app.events = self.tr, self.tr.events
        app.midi = LiveAppTests.FakeWorker()
        app.fix_lock = threading.Lock()
        res = app.fix("channel_next")
        self.assertEqual((res["ok"], res["result"]), (True, "sent"))
        self.assertEqual(app.midi.sent, [m.build_channel_command(11)])
        res = app.fix("channel_auto")                                      # waiting for the first one's ack
        self.assertFalse(res["ok"])
        self.assertEqual(app.midi.sent, [m.build_channel_command(11)])


# ------------------------------------------------ the one big button (Reconnector) as a state machine

class ReconnectTests(Clocked, unittest.TestCase):
    """[한 번에 다시 연결하기] with its readings and events injected: a pretend
    FlowLocal (self.w) answers the status request every 2 s, and what the run
    asks for - ("send", ...) and ("launch",) - is looked at and answered by hand."""

    CFG = dict(m.CONFIG_DEFAULTS)

    def setUp(self):
        super().setUp()
        self.launcher = FakeLauncher()
        self.tr.keeper = m.LiveKeeper(self.tr.events, self.launcher)
        self.sent = []
        self.w = {"ap": 1, "cl": 1, "ps": 1, "hd": 800, "se": 2, "rr": 4}
        self.boot = self.clk.t - 5000.0
        self.answering = True
        self.next_reply = self.clk.t
        self.cfg = dict(self.CFG)

    # ---- the pretend world

    def say(self):
        w = self.w
        quiet = {} if w["ps"] else {"pip": "", "prs": "", "hd": -1, "ra": -1}
        over = dict(ap=w["ap"], ch=6 if w["ap"] else "", cl=w["cl"], ps=w["ps"], se=w["se"], rr=w["rr"])
        if w["ps"]:
            over["hd"] = w["hd"]
        over.update(quiet)
        self.reply(up=int(self.clk.t - self.boot), **over)

    def step(self):
        effects = self.tr.reconnect_step(self.clk.t, self.cfg)
        for e in effects:
            if e[0] == "send":
                self.sent.append(e[1])
                self.tr.reconnect_result(e[1], True, self.clk.t)
            else:
                self.sent.append("launch")
                self.tr.reconnect_launch(self.clk.t, self.cfg)
        return [e[1] if e[0] == "send" else e[0] for e in effects]

    def run_for(self, seconds, dt=0.25):
        """`seconds` pass: a reply every 2 s while FlowLocal answers on an open port, the run looked at every dt."""
        effects = []
        end = self.clk.t + seconds - 1e-9
        while self.clk.t < end:
            self.adv(dt)
            if (self.answering and self.tr.port_present and self.tr.in_open
                    and self.clk.t >= self.next_reply - 1e-9):
                self.next_reply = self.clk.t + 2.0
                self.say()
            effects += self.step()
        return effects

    def restart(self, ack=True):
        """FlowLocal after command 12 (or RST): off USB, back 1.6 s later, a new start without a hotspot."""
        if ack:
            self.adv(0.05); self.ack(0x12, True, "restarting: USB back in seconds, hotspot in about 13 s"); self.step()
        self.adv(0.25); self.tr.on_ports(False, None, None, 0, self.clk.t); self.step()
        self.boot = self.clk.t + 0.1
        self.w.update(ap=0, cl=0, ps=0, se=0, rr=0)
        self.answering = True
        self.run_for(1.6)
        self.port_up()
        self.next_reply = self.clk.t

    def hotspot_and_panel(self, join_after=1.5, session_after=0.8):
        """The hotspot 13.3 s after the start, the panel on it, its session (with its hello)."""
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1)
        self.run_for(join_after)
        self.w.update(cl=1)
        self.run_for(session_after)
        self.session_up()

    def session_up(self):
        self.w.update(ps=1, cl=max(self.w["cl"], 1), se=self.w["se"] + 1, hd=300)
        self.tr.on_midi([0xC0, 3], self.clk.t)
        for n in (1, 2, 3):
            self.tr.on_midi(knob(n, 64), self.clk.t)

    def rc(self):
        return self.snap()["reconnect"]

    def states(self):
        return {s["id"]: s["state"] for s in self.rc()["steps"]}

    def begin(self):
        ok, why = self.tr.reconnect_start(self.clk.t)
        self.assertTrue(ok, why)
        return self.step()

    def answer_resync(self):
        self.adv(0.01)
        self.tr.on_midi([0xC0, 3], self.clk.t)
        self.tr.on_midi([0xBF, 118, 3], self.clk.t)

    # ---- the paths

    def test_happy_path(self):
        self.healthy(settle=10)
        self.next_reply = self.clk.t + 2.0
        self.assertEqual(self.begin(), ["cmd12"])
        self.assertEqual(self.states(), {"usb": "ok", "restart": "run", "panel": "wait", "live": "wait", "verify": "wait"})
        d = self.diag()
        self.assertEqual((d["code"], d["wait"], d["level"]), ("RECONNECT_RUNNING", True, "warn"))
        self.assertIn("2/5단계", d["headline"])
        # every single button waits while it runs; the run cannot start twice
        self.assertEqual(self.action("reboot_flowlocal")["reason"], "'한 번에 다시 연결하기'이 진행 중이에요")
        self.assertEqual(self.tr.reconnect_start(self.clk.t)[0], False)
        self.assertEqual(self.tr.fix_begin("resync", self.clk.t)[0], "busy")
        self.restart()
        self.assertTrue(any("다시 켜지느라" in t for t in self.events("port")))    # not "사라졌어요 (문제)"
        self.assertEqual(self.run_for(3), [])
        self.assertEqual(self.rc()["phase"], "ap")                   # its new start was seen
        self.hotspot_and_panel()
        effects = self.run_for(2.5)
        self.assertEqual(effects, ["resync"])
        self.answer_resync()
        self.run_for(0.5)
        rc = self.rc()
        self.assertEqual((rc["result"], rc["message"]), ("ok", m.RC_DONE_TEXT))
        self.assertEqual(set(self.states().values()), {"ok"})
        self.assertLess(rc["elapsed"], 25)
        # what it sent: command 12 once and the resync once - nothing else
        self.assertEqual(self.sent, ["cmd12", "resync"])
        # the restart it asked for is no self-restart and no flapping
        self.assertIsNone(self.snap()["fl_restart"])
        self.assertEqual(self.snap()["flap"]["count"], 0)
        self.assertEqual(self.diag()["code"], "ALL_OK")
        log = " | ".join(self.events("reconnect"))
        for words in ("1/5 작은 보드(FlowLocal)가 대답하는지 확인: 됐어요", "2/5 작은 보드 다시 켜기", "3/5 패널 다시 연결",
                      "4/5 음악 프로그램", "5/5 마지막 확인", "'작은 보드 다시 켜기' 보내요 (F0 7D 50 46 12 52 46 F7)"):
            self.assertIn(words, log)
        self.assertEqual(self.step(), [])                            # finished: it sends nothing more

    def test_the_port_missing_asks_for_the_cable_and_goes_on(self):
        self.system_ok()
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(30)
        self.begin()
        rc = self.rc()
        self.assertEqual(rc["human"]["kind"], "plug")
        d = self.diag()
        self.assertEqual((d["code"], d["level"]), ("RECONNECT_HUMAN", "warn"))
        self.assertIn("USB", d["headline"])
        self.assertIn("테이프", d["steps"][0])
        self.assertEqual(self.run_for(20), [])
        # plugged in: FlowLocal starts from power (just started: no command 12 needed)
        self.boot = self.clk.t
        self.w.update(ap=0, cl=0, ps=0, se=0, rr=0)
        self.port_up(); self.next_reply = self.clk.t
        self.run_for(3)
        self.assertIsNone(self.rc()["human"])
        self.assertEqual(self.states()["usb"], "ok")
        self.assertIn("다시 켜지 않아요", self.rc()["steps"][1]["detail"])
        self.hotspot_and_panel()
        self.run_for(2.5)
        self.answer_resync()
        self.run_for(0.5)
        self.assertEqual(self.rc()["result"], "ok")
        self.assertEqual(self.sent, ["resync"])

    def test_a_silent_flowlocal_that_repairs_itself(self):
        self.healthy()
        self.answering = False
        self.adv(8)
        self.begin()
        self.assertEqual(self.run_for(15), [])                       # flx-mon2 repairs its USB meanwhile
        self.assertEqual(self.states()["usb"], "run")
        self.assertIsNone(self.rc()["human"])
        # its soft reconnect: the port goes and comes back, replies again from the same start
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.run_for(0.6)
        self.port_up(); self.answering = True; self.next_reply = self.clk.t
        effects = self.run_for(2)
        self.assertEqual(effects, ["cmd12"])
        self.assertIsNone(self.rc()["human"])
        self.assertEqual(self.states()["usb"], "ok")

    def test_a_silent_flowlocal_needs_its_rst_button(self):
        self.healthy()
        self.answering = False
        self.adv(8)
        self.begin()
        self.run_for(m.RC_SELF_REPAIR_S + 1)
        rc = self.rc()
        self.assertEqual(rc["human"]["kind"], "rst")
        d = self.diag()
        self.assertEqual(d["code"], "RECONNECT_HUMAN")
        self.assertEqual(d["headline"], "한 가지만 해 주세요: 작은 보드의 RST 버튼을 한 번 눌러 주세요")
        self.assertIn("BOOT는 누르지 않아요", d["steps"][0])
        self.assertIn("알아서 이어서", d["steps"][1])
        self.assertEqual(self.sent, [])                              # nothing reaches a board that does not answer
        self.run_for(5)
        # RST: the port goes, comes back, a new start
        self.restart(ack=False)
        self.assertEqual(self.run_for(3), [])
        self.assertIsNone(self.rc()["human"])
        self.assertEqual(self.states()["usb"], "ok")
        self.assertIn("RST", self.rc()["steps"][1]["detail"])
        self.assertEqual(self.snap()["flap"]["count"], 0)            # the person's restart is expected too
        self.hotspot_and_panel()
        self.run_for(2.5)
        self.answer_resync()
        self.run_for(0.5)
        self.assertEqual(self.rc()["result"], "ok")
        self.assertEqual(self.sent, ["resync"])                      # no command 12: RST did it

    def test_the_panel_never_joins_asks_for_its_power(self):
        self.healthy()
        self.assertEqual(self.begin(), ["cmd12"])
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1)
        self.run_for(m.RC_JOIN_S + 3)
        rc = self.rc()
        self.assertEqual((rc["human"]["kind"], rc["step"]), ("panel_power", "panel"))
        self.assertEqual(self.diag()["headline"], "한 가지만 해 주세요: 패널 전원을 껐다 켜 주세요")
        self.run_for(10)                                              # the person switches it off and on
        self.w.update(cl=1)
        self.run_for(4)
        self.session_up()
        effects = self.run_for(2.5)
        self.assertEqual(effects, ["resync"])
        self.assertEqual(self.states()["panel"], "ok")
        self.assertEqual(self.sent, ["cmd12", "resync"])             # no command 11: nobody to reach

    def test_the_panel_joins_without_a_session_so_it_is_restarted(self):
        self.healthy()
        self.begin()
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1)
        self.run_for(1.5)
        self.w.update(cl=1)
        self.assertEqual(self.run_for(m.RC_SESSION_S + 2.5), ["cmd11"])
        self.assertIn("F0 7D 50 46 11 52 50 F7", " ".join(self.events("reconnect")))
        self.adv(1.0); self.ack(0x11, True, "panel 192.168.66.2 is rebooting"); self.step()
        self.assertEqual(self.rc()["phase"], "panel_wait")
        self.w.update(cl=0)
        self.run_for(5.5)
        self.w.update(cl=1)
        self.run_for(1.3)
        self.session_up()
        self.assertEqual(self.run_for(2.5), ["resync"])
        self.answer_resync()
        self.run_for(0.5)
        rc = self.rc()
        self.assertEqual(rc["result"], "ok")
        self.assertIn("패널을 다시 켰고", rc["steps"][2]["detail"])
        self.assertEqual(self.sent, ["cmd12", "cmd11", "resync"])

    def test_a_panel_the_reboot_cannot_reach_asks_for_its_power(self):
        self.healthy()
        self.begin()
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1, cl=1)
        self.run_for(m.RC_SESSION_S + 2.5)
        self.adv(2.0); self.ack(0x11, False, "no connection to panel 192.168.66.2"); self.step()
        self.assertEqual(self.rc()["human"]["kind"], "panel_power")

    def test_two_devices_ask_for_the_phones_then_restart_the_panel(self):
        self.healthy()
        self.begin()
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1, cl=2)
        self.run_for(m.RC_SESSION_S + 2.5)
        self.assertEqual(self.rc()["human"]["kind"], "phones")
        self.assertNotIn("cmd11", self.sent)                         # FlowLocal could not tell which is the panel
        self.w.update(cl=1)                                           # phones off
        self.assertEqual(self.run_for(m.RC_SESSION_S + 3), ["cmd11"])

    def test_live_off_is_started_with_the_set(self):
        self.tr.on_power(True, 90, True, self.clk.t)
        self.tr.on_power_settings(m.demo_power_items(), self.clk.t)
        self.tr.on_live(False, [], self.clk.t)                       # never seen running
        self.port_up(); self.adv(1); self.say()
        self.boot = self.clk.t - 5000.0
        self.begin()
        self.restart()
        self.hotspot_and_panel()
        effects = self.run_for(2.5)
        self.assertEqual(effects, ["launch"])
        self.assertEqual(len(self.launcher.calls), 1)
        self.assertEqual(self.launcher.calls[0][0], os.path.normpath(os.path.join(REPO, "set Project", "set.als")))
        self.assertEqual(self.tr.keeper.phase, "launching")
        self.run_for(3)
        self.tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t, m.DEMO_LIVE_EXE)
        self.tr.keep_live(self.clk.t, self.cfg)
        self.run_for(8)
        self.assertEqual(self.states()["live"], "run")
        self.w["rr"] += 1                                             # the Live device asked for a resync: the set is open
        self.run_for(2.5)
        rc = self.rc()
        self.assertEqual((rc["result"], rc["steps"][3]["detail"]), ("ok", "켜지고 전시 세트가 열렸어요"))
        self.assertEqual(self.sent, ["cmd12", "launch"])

    def test_live_off_with_the_switch_off_asks_a_person(self):
        self.cfg = dict(self.CFG, live_autorestart=False)
        self.tr.on_power(True, 90, True, self.clk.t)
        self.tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t)
        self.port_up(); self.adv(1); self.say()
        self.tr.on_live(False, [], self.clk.t)                       # the operator closed it
        self.begin()
        self.restart()
        self.hotspot_and_panel()
        self.run_for(2.5)
        self.assertEqual(self.rc()["human"]["kind"], "live")
        self.assertEqual(self.launcher.calls, [])
        self.tr.on_live(True, ["Ableton Live 12 Suite.exe"], self.clk.t)
        self.assertEqual(self.run_for(1), ["resync"])

    def test_a_lost_ack_does_not_restart_it_twice(self):
        self.healthy()
        self.next_reply = self.clk.t + 2.0
        self.begin()
        # no ack; FlowLocal went silent at once and came back between two port-list reads
        self.answering = False
        self.run_for(m.RC_ACK_S + 1)
        self.assertEqual(self.rc()["phase"], "restart_wait")
        self.boot = self.clk.t - 1.0
        self.w.update(ap=0, cl=0, ps=0, se=0, rr=0)
        self.answering = True; self.next_reply = self.clk.t
        self.run_for(3)
        self.assertEqual(self.rc()["phase"], "ap")
        self.assertEqual(self.sent, ["cmd12"])

    def test_a_command_it_does_not_take_is_sent_once_more_then_rst(self):
        self.healthy()
        self.next_reply = self.clk.t + 2.0
        self.begin()
        self.run_for(m.RC_ACK_S + 2)                                  # it keeps answering from the same start
        self.assertEqual(self.sent, ["cmd12", "cmd12"])
        self.run_for(m.RC_ACK_S + 2)
        self.assertEqual(self.sent, ["cmd12", "cmd12"])
        self.assertEqual(self.rc()["human"]["kind"], "rst")

    def test_a_fresh_flowlocal_is_not_restarted_again(self):
        self.system_ok(); self.port_up()
        self.boot = self.clk.t - 10.0
        self.w.update(ap=0, cl=0, ps=0)
        self.adv(1); self.say()
        self.begin()
        self.assertNotIn("cmd12", self.sent)
        self.assertEqual(self.rc()["phase"], "ap")

    def test_cancel(self):
        self.healthy()
        self.begin()
        self.assertTrue(self.tr.reconnect_cancel(self.clk.t))
        rc = self.rc()
        self.assertEqual((rc["result"], rc["running"]), ("cancelled", False))
        self.assertEqual(self.run_for(30), [])                       # nothing more is sent
        self.assertFalse(self.tr.reconnect_cancel(self.clk.t))
        self.assertTrue(self.action("reboot_flowlocal")["enabled"])

    def test_a_panel_that_joins_but_never_opens_a_session_after_the_step(self):
        self.healthy()
        self.begin()
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1)
        self.run_for(m.RC_JOIN_S + 3)
        self.assertEqual(self.rc()["human"]["kind"], "panel_power")
        # the panel joins but never opens a session, even after the person's step
        self.w.update(cl=1)
        self.run_for(m.RC_HUMAN_JOIN_S + 3)
        rc = self.rc()
        self.assertEqual(rc["result"], "failed")
        self.assertIn("핫스팟에는 붙는데 연결을 열지 않아요", rc["message"])
        self.assertIn("담당자", rc["action"])
        self.assertEqual(self.states()["panel"], "fail")

    def test_the_same_step_twice_ends_the_run(self):
        self.healthy()
        self.begin()
        self.restart()
        self.run_for(max(0.0, self.boot + 13.3 - self.clk.t))
        self.w.update(ap=1)
        self.run_for(m.RC_JOIN_S + 3)
        self.assertEqual(self.rc()["human"]["kind"], "panel_power")
        self.w.update(cl=1)
        self.run_for(3)
        self.session_up()
        self.w.update(hd=40000)                                       # a session, but the panel stays quiet
        self.run_for(2.5 + m.RC_RESYNC_S + m.RC_VERIFY_S + 2)
        rc = self.rc()
        self.assertEqual(rc["result"], "failed")
        self.assertEqual(rc["message"], "패널 전원을 껐다 켜 주세요 — 해 보았지만 이어지지 않았어요.")
        self.assertIn("담당자", rc["action"])
        self.assertEqual(self.states()["verify"], "fail")

    def test_nobody_does_the_step(self):
        self.system_ok()
        self.tr.on_ports(False, None, None, 0, self.clk.t)
        self.adv(30)
        self.begin()
        self.run_for(m.RC_HUMAN_S + 1, dt=1.0)
        rc = self.rc()
        self.assertEqual(rc["result"], "failed")
        self.assertIn("5분 기다렸는데", rc["message"])
        self.assertIn("[한 번에 다시 연결하기]를 다시 눌러", rc["action"])

    def test_a_port_the_monitor_cannot_open(self):
        self.system_ok()
        self.tr.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, self.clk.t)
        self.tr.on_opened(False, True, "입력 error", self.clk.t)
        self.begin()
        self.run_for(m.RC_OPEN_S + 1)
        rc = self.rc()
        self.assertEqual(rc["result"], "failed")
        self.assertIn("노트북을 다시 시작", rc["action"])

    def test_the_last_check_asks_for_the_panel_when_it_went_quiet(self):
        self.healthy()
        self.begin()
        self.restart()
        self.hotspot_and_panel()
        self.w.update(hd=40000)                                       # a session, but the panel went quiet
        self.run_for(2.5 + m.RC_RESYNC_S + m.RC_VERIFY_S + 1)
        rc = self.rc()
        self.assertEqual((rc["step"], rc["human"]["kind"]), ("verify", "panel_power"))
        self.assertEqual(rc["steps"][4]["detail"], "패널 연결은 있는데 패널 소식이 없어요")
        # switched off and on: the panel step runs again, then the check
        self.w.update(ps=0, cl=0)
        self.run_for(6)
        self.w.update(cl=1)
        self.run_for(2)
        self.session_up()
        self.run_for(2.5 + m.RC_RESYNC_S + 1)
        rc = self.rc()
        self.assertEqual(rc["result"], "ok")
        self.assertEqual(self.sent, ["cmd12", "resync", "resync"])

    def test_when_it_may_start(self):
        self.healthy()
        self.assertTrue(m.reconnect_state(self.snap())["enabled"])
        self.assertIsNone(self.tr.fix_begin("reconnect", self.clk.t))    # a single button's press first
        s = m.reconnect_state(self.snap())
        self.assertEqual(s["enabled"], False)
        self.assertIn("앞에 누른 버튼", s["reason"])
        self.assertEqual(self.tr.reconnect_start(self.clk.t)[0], False)
        tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=False)
        self.assertFalse(m.reconnect_state(tr.snapshot())["enabled"])
        self.assertEqual(tr.reconnect_start(self.clk.t), (False, "모니터가 MIDI를 쓸 수 없어요"))
        # old firmware (never answered on this laptop): it cannot do its job
        tr = m.Tracker(m.EventLog(None), clock=self.clk, midi_lib=True)
        tr.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, self.clk.t)
        tr.on_opened(True, True, None, self.clk.t)
        for _ in range(4):
            self.adv(2); tr.on_request_sent(self.clk.t)
        self.assertIn("펌웨어", m.reconnect_state(tr.snapshot())["reason"])

    def test_nothing_is_sent_without_a_press(self):
        self.healthy()
        for _ in range(40):
            self.adv(1)
            self.assertEqual(self.tr.reconnect_step(self.clk.t, self.cfg), [])
        self.assertIsNone(self.snap()["reconnect"])


class ReconnectDemoTests(unittest.TestCase):
    """Every outcome of the demo's pretend world, played to the end."""

    def test_every_outcome(self):
        cases = (("panel_silent", "ok", 40, "ok"), ("fl_no_reply", "rst", 70, "ok"),
                 ("fl_no_reply", "self_repair", 60, "ok"), ("port_missing", "plug", 60, "ok"),
                 ("panel_no_session", "panel_reboot", 60, "ok"), ("panel_not_on_hotspot", "panel_off", 90, "ok"),
                 ("panel_no_session_two", "phones", 90, "ok"), ("live_never_up", "live_off", 70, "ok"),
                 ("panel_not_on_hotspot", "panel_dead", 360, "failed"))
        for key, outcome, elapsed, result in cases:
            with self.subTest(outcome):
                tr, clk = m.run_scenario(key, press=(m.RECONNECT_ID, outcome, float(elapsed)))
                snap = tr.snapshot(clk.t)
                self.assertEqual(snap["reconnect"]["result"], result)
                if result == "ok":
                    self.assertEqual(m.diagnose(snap, m.CONFIG_DEFAULTS)["code"], "ALL_OK")
                json.dumps(m.build_state("demo", snap, m.CONFIG_DEFAULTS, None, tr.events.recent()))

    def test_the_demo_page_plays_a_press_and_a_cancel(self):
        app = m.DemoApp(m.Config(os.path.join(os.path.dirname(os.path.abspath(__file__)), "no-such-config.json")),
                        pinned="panel_silent")
        res = app.reconnect({})
        self.assertTrue(res["ok"])
        st = app.state({})
        self.assertEqual(st["diagnosis"]["code"], "RECONNECT_RUNNING")
        self.assertFalse(st["reconnect"]["enabled"])
        self.assertEqual(app.reconnect({})["result"], "refused")          # not twice
        self.assertTrue(app.reconnect_cancel({})["ok"])
        self.assertEqual(app.state({})["snap"]["reconnect"]["result"], "cancelled")
        app2 = m.DemoApp(app.config, pinned="old_firmware")
        self.assertEqual(app2.reconnect({})["result"], "refused")


class ReconnectHttpTests(unittest.TestCase):
    setUpClass = classmethod(HttpTests.setUpClass.__func__)
    tearDownClass = classmethod(HttpTests.tearDownClass.__func__)
    req = HttpTests.req

    def test_the_one_button_needs_the_dashboard_header(self):
        self.assertEqual(self.req("POST", "/api/reconnect?scenario=panel_silent")[0], 403)
        self.assertEqual(self.req("GET", "/api/reconnect")[0], 404)
        st, body = self.req("POST", "/api/reconnect?scenario=panel_silent", {"X-PF-Monitor": "1"})
        self.assertEqual(st, 200)
        self.assertTrue(json.loads(body.decode("utf-8"))["ok"])
        d = json.loads(self.req("GET", "/api/state?scenario=panel_silent")[1].decode("utf-8"))
        self.assertEqual(d["diagnosis"]["code"], "RECONNECT_RUNNING")
        self.assertEqual([s["id"] for s in d["snap"]["reconnect"]["steps"]], ["usb", "restart", "panel", "live", "verify"])
        st, body = self.req("POST", "/api/reconnect/cancel?scenario=panel_silent", {"X-PF-Monitor": "1"})
        self.assertTrue(json.loads(body.decode("utf-8"))["ok"])
        self.assertEqual(self.req("POST", "/api/fix/channel_next?scenario=wifi_poor_mon3", {"X-PF-Monitor": "1"})[0], 200)


class ReconnectRunnerTests(unittest.TestCase):
    """The LiveApp's thread that carries the run out: one SendJob per message, through the MIDI thread."""

    def test_messages_go_through_the_midi_thread(self):
        clk = m.FakeClock(1000.0)
        tr = m.Tracker(m.EventLog(None), clock=clk, midi_lib=True)
        tr.on_ports(True, "Patternflow Exhibit 0", "Patternflow Exhibit 1", 1, clk.t)
        tr.on_opened(True, True, None, clk.t)
        tr.on_midi(reply_bytes(up=5000), clk.t)
        worker = LiveAppTests.FakeWorker()
        runner = m.ReconnectRunner(tr, types.SimpleNamespace(get=lambda: dict(m.CONFIG_DEFAULTS)), worker)
        self.assertTrue(tr.reconnect_start(clk.t)[0])
        for e in tr.reconnect_step(clk.t, m.CONFIG_DEFAULTS):
            runner.carry_out(e, m.CONFIG_DEFAULTS)
        self.assertEqual(worker.sent, [m.build_command(0x12)])
        self.assertTrue(tr.reconnect.run["sent"]["cmd12"])
        runner_without_midi = m.ReconnectRunner(tr, None, None)
        runner_without_midi.carry_out(("send", "cmd12", m.build_command(0x12)), {})
        self.assertFalse(tr.reconnect.run["sent"]["cmd12"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
