#!/usr/bin/env python3
"""Staff monitor for the Patternflow exhibit: is the chain panel -> Wi-Fi ->
FlowLocal -> USB -> laptop -> Ableton Live working, and if not, which link
broke and what to do - in plain Korean, for on-site helpers - with buttons
that fix the usual cases from this one laptop page.

  python tools/monitor/pf_monitor.py            # the hardware, http://127.0.0.1:8765
  python tools/monitor/pf_monitor.py --open     # ... and open the dashboard window
  python tools/monitor/pf_monitor.py --demo     # no hardware: cycles through every scenario
  python tools/monitor/pf_monitor.py --demo --demo-scenario hotspot_full
  python tools/monitor/pf_monitor.py --list-scenarios

What it reads:
- the "Patternflow Exhibit" USB-MIDI input (FlowLocal): the panel's Program
  Change, CC 24-26 and notes (with their arrival times, to see held-up knob
  messages: KnobBunchDetector), and FlowLocal's status replies and command
  acks (below). Windows MIDI Services makes the port multi-client, so this
  runs next to Live.
- tasklist (is Ableton Live running), GetSystemPowerStatus (charger) and
  powercfg (sleep, hibernate, lid, USB selective suspend and PCI Express
  link state power management, on the charger and on battery: the show must
  go on when the charger is pulled out; the repo's "노트북 전시용 전원 설정.bat"
  sets them all).

What it sends, to the "Patternflow Exhibit" output:
- every 2 s the status request F0 7D 50 46 01 F7. Nothing else on its own.
- one message per press of an operator button, never automatically:
    소리 다시 맞추기      BF 77 7F                    resync (CC 119 ch 16)
    패널 연결 다시 잡기   F0 7D 50 46 10 52 43 F7     FlowLocal ends its RTP sessions
    패널 다시 켜기        F0 7D 50 46 11 52 50 F7     FlowLocal POSTs the panel's /api/wifi/reboot
    FlowLocal 다시 켜기   F0 7D 50 46 12 52 46 F7     FlowLocal acks, then restarts
    무선 채널 바꾸기      F0 7D 50 46 13 43 48 nn F7  flx-mon3: fix channel nn (1..13), acks, restarts
    채널 자동으로         F0 7D 50 46 13 43 48 00 F7  flx-mon3: back to the boot scan's pick, restarts
- the one big button "한 번에 다시 연결하기" (Reconnector): one press runs a
  sequence - FlowLocal answering?, command 12, the panel back (command 11
  if it sits on the hotspot without a session), Live started or resynced,
  a last check - that sends those same messages, each at most twice per
  press, and stops to ask a person for one physical step when it must.
  No Program Change, CC or note ever (the resync is FlowLocal's own CC 119).

PF monitor SysEx v1 (the FlowLocal firmware implements the other side):
  F0 7D 50 46 01 F7                      status request       laptop -> FlowLocal
  F0 7D 50 46 1x <guard> [arg] F7        command (above)      laptop -> FlowLocal
  F0 7D 50 46 02 <key=value;...> F7      status reply         FlowLocal -> laptop
  F0 7D 50 46 03 cmd=1x;ok=0|1;msg=.. F7 command ack          FlowLocal -> laptop
7D is the non-commercial manufacturer ID, 50 46 = "PF". Text is 0x20..0x7E,
at most 240 bytes; unknown keys are ignored (status keys: STATUS_KEYS).
FlowLocal firmware from before the protocol discards all of it and never
answers: the monitor then says the status and the three FlowLocal buttons
need the new FlowLocal firmware, gives the manual steps instead, and keeps
showing the panel's MIDI. That is only said of a FlowLocal never heard on
this laptop: logs/state.json (Memory) remembers across monitor restarts that
FlowLocal answered the protocol, so a silent one later is a stuck one (press
its RST button), as on 2026-09-25 21:12.

Every rule that turns the readings into a colour, a headline, "what to do
now" steps and the button to press is in diagnose(), in priority order, and
test_pf_monitor.py runs each one. --demo feeds the same Tracker and
diagnose() with scripted scenarios (SCENARIOS), button presses included, so
what the dashboard shows in the demo is what it would show for those
readings on the hardware.

State changes and every button press go to logs/YYYY-MM-DD.log (one line
each, local time), and so does the evidence for later: FlowLocal's counters
rj / to / se / gp / ur (and an integer key this monitor does not know yet)
with their raw numbers when they change, the hotspot's channel and flx-mon3's
scan scores, and the time from the panel joining the hotspot to its session
(Tracker._note_evidence, _note_connect). What must survive a monitor restart
goes to logs/state.json.

Needs Python 3.11 and python-rtmidi (pip install python-rtmidi). Without
python-rtmidi the dashboard still runs and says the monitor cannot read MIDI.
"""

import argparse
import collections
import csv
import ctypes
import io
import json
import math
import os
import queue
import re
import socket
import subprocess
import sys
import threading
import time
import traceback
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    import rtmidi
except ImportError:  # the dashboard reports it
    rtmidi = None

HERE = os.path.dirname(os.path.abspath(__file__))
DASHBOARD = os.path.join(HERE, "dashboard.html")
CONFIG = os.path.join(HERE, "config.json")

PORT_NAME = "Patternflow Exhibit"
HTTP_HOST = "127.0.0.1"
HTTP_PORT = 8765

# ---------------------------------------------------------------- timing

POLL_S = 2.0                  # status request interval (the protocol's 2 s)
PORT_CHECK_S = 1.0            # how often the MIDI port list is read
OPEN_RETRY_S = 3.0            # after a failed open
STATUS_STALE_S = 6.0          # no reply for this long (3 polls): FlowLocal is not answering
FL_NO_REPLY_BAD_S = 20.0      # ... and after this long it is red, not a blip
PORT_GRACE_S = 6.0            # a replug or a FlowLocal reset takes a few seconds
LIVE_START_GRACE_S = 150.0    # Live needs 1-2 min after login to open the set
FL_BOOT_GRACE_S = 40.0        # FlowLocal up less than this is starting: hotspot after 12 s,
                              # panel back 13-24 s after a reset (measured 2026-09-19)
PANEL_RECONNECT_GRACE_S = 30.0  # session just dropped, hotspot empty: the panel rejoins by itself
PANEL_JOIN_GRACE_S = 25.0     # stations on the hotspot but no session yet: the panel image of
                              # 2026-09-25 invites 20-23 s after it joined (measured)
HEARD_BAD_MS = 30000          # session up but nothing from it: three AppleMIDI syncs (10 s apart) missed
MIDI_RECENT_S = 180.0         # without status, MIDI this recent shows the panel is through
USB_LOSS_S = 3.0              # FlowLocal got panel MIDI this much later than the laptop's last one
RESYNC_WAIT_S = 1.5           # FlowLocal answers a resync within ~10 ms (measured 2026-09-19)
LATE_ACK_S = 15.0             # an ack this late after the press still counts
FIX_MEMORY_S = 300.0          # a press that did not help is skipped this long when picking the next button
FIX_SHOW_S = 600.0            # a finished press stays on the page this long
STALE_REPLY_S = 9.0           # replies stopped this long while the port stays listed: reopen it (MidiWorker)
STALE_TRIES = 3               # reopens per silence; each keeps its old handles open for good
FL_RESTART_REOPEN_S = 2.0     # FlowLocal acked a restart: reopen the port this much later
FLAP_COUNT = 3                # FlowLocal left USB (or restarted) this often ...
FLAP_WINDOW_S = 120.0         # ... within this long: it keeps restarting - power or cable (2026-09-25 21:02-21:04)
FLAP_NOTE_S = 600.0           # after such a run, a yellow note stays this long after its last restart
SELF_RESTART_NOTE_S = 1800.0  # a restart FlowLocal did by itself (rsn=, bc=) is a yellow note while this recent
LOW_BATTERY_PCT = 30          # on battery: yellow ("the sound goes on"), red below this
MEMORY_SAVE_S = 60.0          # logs/state.json: "FlowLocal last answered" is rewritten at most this often

# Wireless quality: WIFI_POOR (yellow) needs a sign that signals really came
# late - held-up knob messages seen here, or FlowLocal's link numbers jt, jm,
# gp. A weak signal (prs) alone is not that; it is named next to them. Why
# these numbers: README.md.
WEAK_RSSI_DBM = -75           # prs, the panel's station RSSI at FlowLocal (named, never enough alone)
JITTER_WARN_MS = 20           # jt, RFC 3550 interarrival jitter (a few ms on a quiet channel)
JUMP_WARN_MS = 250            # jm, largest transit deviation in 60 s: a hold-up people hear
GAPS_WARN_PER_MIN = 3         # RTP packets lost per minute (gp counter)
BUNCHES_WARN_PER_MIN = 3      # held-up knob messages (KnobBunchDetector) in the last minute
WIFI_SETTLE_S = 20.0          # after a session comes up: its numbers, and what happens meanwhile, do not count
JUMP_SETTLE_S = 60.0          # ... jm covers 60 s, so the session's start stays in it that long

# Held-up knob messages (KnobBunchDetector). The panel paces each knob (CC 24,
# 25, 26 on channel 1) to at most one message per 50 ms, so two CCs of the
# same knob that reach the laptop closer than BUNCH_PAIR_S were held on the way
# (the first at least 38 ms longer than the second) and let go together.
# Bundles that come together on purpose - the panel's hello on a new session
# (PC then K1-K3), FlowLocal's resync answer and USB re-mount re-send (PC, CC
# 118, K1-K3), footstep note on/off pairs - hold one CC per knob at most, and
# the knob CCs right after a PC are FlowLocal's cache, so they never count.
BUNCH_PAIR_S = 0.012
BUNCH_GROUP_S = 0.060         # a pair this soon after the last one is the same hold-up
BUNDLE_AFTER_PC_S = 0.010     # a knob's first CC this soon after a Program Change belongs to its bundle
BUNCH_GAP_MAX_S = 5.0         # the knob's pause before a hold-up is shown up to this long
TIMELINE_S = 15.0             # the dashboard's arrival timeline
ARRIVALS_MAX = 3000

# Evidence for later (the event log): FlowLocal's counters and connections.
CONNECT_SLOW_S = 8.0          # panel on the hotspot (cl 0 -> n) to its session (ps 0 -> n): 1-2 s is
                              # normal, 20-24 s were measured on 2026-09-27; more than this is a warning
GP_NOTE_S = 60.0              # packet gaps (gp) are written at most once per this, summed
NEW_COUNTER_NOTE_S = 60.0     # a counter this monitor does not know yet, likewise

# ---------------------------------------------------------------- protocol

SYSEX_HEAD = (0xF0, 0x7D, 0x50, 0x46)
CMD_STATUS_REQUEST = 0x01
CMD_STATUS_REPLY = 0x02
CMD_ACK = 0x03
CMD_RECONNECT_PANEL = 0x10
CMD_REBOOT_PANEL = 0x11
CMD_REBOOT_FLOWLOCAL = 0x12
CMD_CHANNEL = 0x13            # flx-mon3: one argument byte after the guard (CHANNEL_*)
COMMAND_GUARD = {CMD_RECONNECT_PANEL: (0x52, 0x43),     # 'R' 'C'
                 CMD_REBOOT_PANEL: (0x52, 0x50),        # 'R' 'P'
                 CMD_REBOOT_FLOWLOCAL: (0x52, 0x46),    # 'R' 'F'
                 CMD_CHANNEL: (0x43, 0x48)}             # 'C' 'H'
CHANNEL_AUTO = 0x00           # command 13: back to the boot scan's pick
CHANNEL_NEXT = 0x7F           # command 13: the next of 1/6/11 after the current one (the monitor sends the number)
CHANNEL_CANDIDATES = (1, 6, 11)
STATUS_REQUEST = [0xF0, 0x7D, 0x50, 0x46, CMD_STATUS_REQUEST, 0xF7]
RESYNC_REQUEST = [0xBF, 0x77, 0x7F]
MAX_TEXT = 240                # what a writer may send
MAX_TEXT_READ = 1024          # what this reader accepts before calling it garbage

# key, meaning (Korean, shown in the operator section)
STATUS_KEYS = (
    ("v", "프로토콜 버전"),
    ("fw", "FlowLocal 빌드"),
    ("up", "FlowLocal 켜진 시간 (초)"),
    ("ap", "핫스팟 켜짐 (1 = 켜짐)"),
    ("ch", "핫스팟 무선 채널"),
    ("chm", "채널 정하는 방식 (auto = 켤 때 조용한 채널을 고름, fixed = 펌웨어에 고정, manual = 운영자가 고름)"),
    ("cl", "핫스팟에 붙은 기기 수"),
    ("cm", "핫스팟 최대 기기 수"),
    ("ps", "RTP 세션 수 (패널)"),
    ("pip", "패널 IP"),
    ("prs", "패널 Wi-Fi 신호 세기 (dBm)"),
    ("hd", "마지막 MIDI·하트비트 (ms 전, -1 = 없음)"),
    ("ra", "마지막 MIDI 메시지 (ms 전, -1 = 없음)"),
    ("lp", "마지막 Program Change (-1 = 없음)"),
    ("kn", "K1-K3 마지막 값 (CC 24-26)"),
    ("jt", "RTP 도착 흔들림 (ms, RFC 3550, -1 = 모름)"),
    ("jm", "최근 60초 가장 큰 늦음 (ms, -1 = 모름)"),
    ("rx", "RTP에서 받은 메시지"),
    ("tx", "USB로 쓴 메시지"),
    ("dr", "버린 메시지"),
    ("se", "켜진 뒤 열린 세션"),
    ("to", "시간 초과로 끝난 세션"),
    ("rj", "거절한 초대"),
    ("gp", "RTP 패킷 빠짐"),
    ("rr", "받은 재동기화 요청"),
    ("ud", "버린 USB 입력 패킷"),
    ("sr", "답한 상태 요청"),
    ("hi", "내부 힙 여유 (바이트)"),
    # from firmware flx-mon2 on; flx-mon1 does not send them (the monitor then shows them as unknown)
    ("rsn", "마지막으로 켜진 이유 (poweron = 전원을 넣어 켬, brownout = 전원 부족, panic·wdt = 펌웨어 멈춤)"),
    ("bc", "전원을 넣은 뒤 켜진 횟수 (1 = 그 뒤로 다시 켜진 적 없음)"),
    ("why", "다시 켜기 전에 남긴 이유 (cmd12 = 다시 켜기 버튼, web = 상태 페이지, bridge_hung = MIDI 전달 멈춤, "
            "usb_wedged = USB로 대답이 안 나감)"),
    ("ur", "켜진 뒤 USB 연결을 스스로 다시 맺은 횟수"),
    # from firmware flx-mon3 on
    ("cs", "켤 때 채널 점수 (채널:점수, 낮을수록 조용, 비었으면 검사 안 함)"),
)
COUNTER_KEYS = ("rx", "tx", "dr", "se", "to", "rj", "gp", "rr", "ud", "sr", "hi")
# chm= in Korean
CHANNEL_MODE_WORDS = {"auto": "자동 (켤 때 가장 조용한 채널)", "fixed": "고정 (펌웨어 설정)",
                      "manual": "직접 고름 (운영자가 고정)"}
KNOWN_KEYS = frozenset(k for k, _ in STATUS_KEYS)

# rsn= (FlowLocal's esp_reset_reason() at its last start): Korean words, and the
# kind of cause - "power" (cable, port, charger), "crash" (the firmware stopped;
# tell the operator), None (a normal start or not a fault by itself).
RESET_REASONS = {
    "poweron": ("전원을 넣어 켬", None),
    "ext": ("리셋 신호", None),
    "sw": ("펌웨어가 다시 켬", None),
    "panic": ("펌웨어 오류", "crash"),
    "int_wdt": ("펌웨어 멈춤·인터럽트 감시", "crash"),
    "task_wdt": ("펌웨어 멈춤·작업 감시", "crash"),
    "wdt": ("펌웨어 멈춤·감시 장치", "crash"),
    "deepsleep": ("깊은 잠에서 깸", None),
    "brownout": ("전원 부족", "power"),
    "sdio": ("SDIO 리셋", None),
    "usb": ("USB 리셋", None),
    "jtag": ("JTAG 리셋", None),
    "efuse": ("eFuse 오류", "crash"),
    "pwr_glitch": ("전원 순간 끊김", "power"),
    "cpu_lockup": ("CPU 멈춤", "crash"),
    "unknown": ("알 수 없음", None),
}


def reset_word(rsn):
    """rsn= in Korean; a reason this monitor does not know yet keeps its tag."""
    if rsn is None:
        return "모름"
    return RESET_REASONS[rsn][0] if rsn in RESET_REASONS else "알 수 없음 · %s" % rsn


def reset_kind(rsn):
    return RESET_REASONS[rsn][1] if rsn in RESET_REASONS else None


# why= (flx-mon2, fl_recovery.h noteName): the note FlowLocal left before it
# restarted itself - in Korean, and whether someone asked for the restart
# ("asked": the monitor's command 12, the status page's Reboot) or FlowLocal
# recovered by itself ("crash": tell the operator).
RESTART_NOTES = {
    "cmd12": ("'FlowLocal 다시 켜기' 버튼", "asked"),
    "cmd13": ("'무선 채널 바꾸기' 버튼", "asked"),      # flx-mon3 (the name is a guess; any restart after the press counts as asked)
    "web": ("상태 페이지의 Reboot", "asked"),
    "bridge_hung": ("MIDI 전달이 멈춤", "crash"),
    "usb_wedged": ("USB로 대답이 안 나감", "crash"),
}


def note_word(why):
    return RESTART_NOTES[why][0] if why in RESTART_NOTES else why

# Pattern number = Program Change = Live scene (README section 1). Shown by
# number only: the artists' romanised names were not confirmed, and the panel
# no longer captions them either.
KNOB_CCS = (24, 25, 26)       # K1-K3 position, channel 1
MIRROR_CHANNEL = 16           # CC 118 program mirror / CC 119 resync, FlowLocal's

LEVEL_WORD = {"ok": "정상", "warn": "확인 필요", "bad": "문제", "unknown": "모름"}
WAIT_WORD = "기다리는 중"
SEVERITY = {"ok": 0, "unknown": 0, "warn": 1, "bad": 2}

STEP_PANEL_POWER_CYCLE = "패널 전원을 끄고 10초 뒤 다시 켜요. 첫 그림부터 시작하고 보통 1분 안에 이어져요."
STEP_REPLUG_SHORT = "작은 보드(FlowLocal)를 노트북에서 뽑았다가 5초 뒤 같은 자리에 다시 꽂아요."
# A FlowLocal that sits on USB but answers nothing (2026-09-25 21:03-21:19: its hotspot
# up, its USB side silent) came back with its RST button; a replug does the same.
# The screen's buttons go over that same USB and cannot reach it.
STEP_RST = ("작은 보드(FlowLocal)의 RST 버튼을 한 번 누르세요. 옆의 BOOT 버튼은 누르지 않아요. "
            "30초 안에 이 화면이 '켜지는 중'을 거쳐 초록이 되는지 봐요.")
STEP_REPLUG_TAPED = ("안 되면 작은 보드(FlowLocal) 케이블을 노트북에서 뽑았다가 5초 뒤 같은 테이프 자리에 다시 꽂으세요. "
                     "보드 쪽은 'USB'라고 적힌 잭이에요('COM' 잭이 아니에요).")
STEP_RST_SHORT = ("작은 보드(FlowLocal)의 RST 버튼을 한 번 눌러요(BOOT 말고). "
                  "안 되면 뽑았다가 5초 뒤 같은 테이프 자리에 다시 꽂아요.")
POWER_SCRIPT = "노트북 전시용 전원 설정.bat"
STEP_POWER_SCRIPT = ("(담당자) 루트 폴더(레포 맨 위, '패턴플로우 모니터 열기.bat'이 있는 곳)의 "
                     "'%s'을 한 번 실행해요. 관리자 권한은 필요 없어요." % POWER_SCRIPT)
# A panel that went dark cannot take the reboot command: the power comes first.
STEP_REBOOT_PANEL_IF_ON = ("패널에 그림이 보이는지(전원이 켜져 있는지) 봐요. 그림이 보이면 화면의 [패널 다시 켜기] 버튼을 누르세요. "
                           "그림이 없으면 패널 전원부터 켜요.")

# The operator's single buttons (운영자용 칸, "개별 버튼"). Each sends exactly one
# message per press.
#   gap_s      the same button (or one of its group) again only after this long
#              (FlowLocal takes each command once per 3 s)
#   ack_s      a command's ack must come within this (reboot-panel waits for FlowLocal's
#              HTTP call: 2 s to connect and 2 s for the answer at most)
#   recover_s  after the ack, how long the monitor watches for the panel to come back
#   group      buttons that share gap_s and "needs the new firmware" ("cmd": 10-12, "channel": 13)
# "reconnect" ends the RTP sessions FlowLocal holds, so it only helps while a
# session is up (FlowLocal answers ok=0 "no RTP session to end" otherwise);
# "reboot_panel" needs FlowLocal to know which hotspot device is the panel.
# The channel buttons (flx-mon3) make FlowLocal store the choice and restart:
# USB back in ~2 s, the hotspot after ~13 s, the panel ~2 s later.
STEP_CHANNEL_MANUAL = ("새 FlowLocal 펌웨어(flx-mon3)가 있어야 채널을 고를 수 있어요. 그 전에는 [FlowLocal 다시 켜기]로 "
                       "켤 때 가장 조용한 채널을 다시 고르게 해요.")
CHANNEL_CONFIRM = ("작은 보드가 새 채널로 다시 켜져요. 음악은 계속 나오지만 15초쯤 소리가 패널을 따라가지 않아요 "
                   "(USB는 2초쯤, 핫스팟은 13초쯤 뒤 돌아오고, 패널은 그 뒤 2초쯤에 다시 붙어요).")
ACTIONS = collections.OrderedDict([
    ("resync", {
        "label": "소리 다시 맞추기", "cmd": None, "msg": RESYNC_REQUEST,
        "gap_s": 3.0, "ack_s": None, "recover_s": None,
        "when": "소리가 패널 그림과 다를 때",
        "what": "작은 보드가 패널의 지금 그림과 노브 값을 음악 프로그램에 다시 보내요. 아무것도 끊기지 않아요.",
        "confirm": None, "wait_text": None,
        "manual": "음악 프로그램 장치의 Resync 버튼을 누르거나, 그림 바꾸는 노브를 딸깍 3번 돌려요.",
    }),
    ("reconnect", {
        "label": "패널 연결 다시 잡기", "cmd": CMD_RECONNECT_PANEL,
        "gap_s": 5.0, "ack_s": 2.0, "recover_s": 40.0,
        "when": "패널은 연결돼 있다는데 소리가 패널을 따라오지 않을 때",
        "what": "작은 보드가 패널과의 연결을 한 번 끊어, 패널이 곧바로 새로 붙게 해요. 패널 그림은 그대로예요.",
        "confirm": None, "wait_text": "다시 연결 중… 보통 10초 안에 붙어요",
        "manual": STEP_PANEL_POWER_CYCLE,
    }),
    ("reboot_panel", {
        "label": "패널 다시 켜기", "cmd": CMD_REBOOT_PANEL,
        "gap_s": 15.0, "ack_s": 6.0, "recover_s": 60.0,
        "when": "패널이 핫스팟에 있는데 연결이 안 붙을 때, '패널 연결 다시 잡기'로 안 될 때",
        "what": "작은 보드가 패널에게 다시 켜라고 해요. 패널이 핫스팟에 붙어 있을 때만 돼요.",
        "confirm": "패널 그림이 잠깐 꺼졌다가 첫 그림부터 다시 시작돼요. 30초쯤 걸려요.",
        "wait_text": "패널이 다시 켜지는 중… 보통 30초 안에 붙어요",
        "manual": STEP_PANEL_POWER_CYCLE,
    }),
    ("reboot_flowlocal", {
        "label": "FlowLocal 다시 켜기", "cmd": CMD_REBOOT_FLOWLOCAL,
        "gap_s": 15.0, "ack_s": 2.0, "recover_s": 60.0,
        "when": "핫스팟이 꺼졌을 때, 무선이 계속 나쁠 때 (대답을 멈춘 보드에는 닿지 않아요: 그때는 보드의 RST 버튼)",
        "what": "작은 보드를 다시 켜요. 켜질 때 가장 조용한 무선 채널을 다시 골라요.",
        "confirm": "음악은 계속 나오지만 30초쯤 소리가 패널을 따라가지 않아요. 그 뒤 패널이 저절로 다시 붙어요.",
        "wait_text": "작은 보드가 다시 켜지는 중… 보통 30초 안에 패널까지 붙어요",
        "manual": STEP_RST_SHORT,
    }),
    ("channel_next", {
        "label": "무선 채널 바꾸기", "cmd": CMD_CHANNEL, "group": "channel",
        "gap_s": 30.0, "ack_s": 2.0, "recover_s": 60.0,
        "when": "폰 핫스팟을 끈 뒤에도 무선이 계속 나쁠 때",
        "what": "작은 보드의 무선 채널을 1·6·11 중 다음 것으로 바꾸고 다시 켜요. 고른 채널은 다시 켜져도 그대로예요.",
        "confirm": CHANNEL_CONFIRM,
        "wait_text": "무선 채널을 바꾸고 다시 켜지는 중… 보통 20초 안에 패널까지 붙어요",
        "manual": STEP_CHANNEL_MANUAL,
    }),
    ("channel_auto", {
        "label": "채널 자동으로", "cmd": CMD_CHANNEL, "group": "channel",
        "gap_s": 30.0, "ack_s": 2.0, "recover_s": 60.0,
        "when": "직접 고른 채널을 그만 쓰고 켤 때마다 가장 조용한 채널을 고르게 할 때",
        "what": "작은 보드가 켤 때마다 주변을 살펴 가장 조용한 채널을 고르게 되돌리고 다시 켜요.",
        "confirm": "작은 보드가 다시 켜지며 채널을 새로 골라요. 음악은 계속 나오지만 15초쯤 소리가 패널을 따라가지 않아요.",
        "wait_text": "채널을 자동으로 되돌리고 다시 켜지는 중… 보통 20초 안에 패널까지 붙어요",
        "manual": STEP_CHANNEL_MANUAL,
    }),
])
for _a in ACTIONS.values():
    _a.setdefault("group", "cmd" if _a["cmd"] is not None else None)
    if _a["cmd"] is not None and _a["cmd"] != CMD_CHANNEL:
        _a["msg"] = [0xF0, 0x7D, 0x50, 0x46, _a["cmd"]] + list(COMMAND_GUARD[_a["cmd"]]) + [0xF7]
ACTIONS["channel_auto"]["msg"] = [0xF0, 0x7D, 0x50, 0x46, CMD_CHANNEL, 0x43, 0x48, CHANNEL_AUTO, 0xF7]
ACTIONS["channel_next"]["msg"] = None     # the channel's number, from the status at the press (action_message)
ACTION_BY_CMD = {a["cmd"]: k for k, a in ACTIONS.items() if a["cmd"] is not None and k != "channel_auto"}
# FlowLocal restarts after these (its ack says so); the port leaving then is expected
RESTART_ACTIONS = frozenset(("reboot_flowlocal", "channel_next", "channel_auto"))

# The one big button on the main screen (Reconnector).
RECONNECT_ID = "reconnect_all"
RECONNECT_LABEL = "한 번에 다시 연결하기"


def button_label(action):
    """The name a step puts in [brackets] for this button."""
    if action == RECONNECT_ID:
        return RECONNECT_LABEL
    return ACTIONS[action]["label"]

# Windows: keep tasklist / powercfg from flashing a console under pythonw.
CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0


def fmt_ago(sec):
    """Korean "... ago": 방금, 12초 전, 5분 전."""
    if sec is None:
        return "모름"
    return "방금" if sec < 2 else "%s 전" % fmt_dur(sec)


def pattern_name(n):
    if n is None:
        return None
    return "그림 %d" % n


def fmt_dur(sec):
    """Korean duration: 45초, 5분, 3시간 12분."""
    if sec is None:
        return "모름"
    sec = int(max(0, sec))
    if sec < 60:
        return "%d초" % sec
    if sec < 3600:
        return "%d분" % (sec // 60)
    h, m = sec // 3600, (sec % 3600) // 60
    return "%d시간 %d분" % (h, m) if m else "%d시간" % h


def hexbytes(msg):
    return " ".join("%02X" % b for b in msg)


# ================================================================ SysEx

def build_status_request():
    return list(STATUS_REQUEST)


def build_command(cmd, arg=None):
    """The command message for 0x10 / 0x11 / 0x12, with its guard bytes, and
    0x13 with its argument byte after the guard (build_channel_command)."""
    if cmd == CMD_CHANNEL:
        return build_channel_command(CHANNEL_NEXT if arg is None else arg)
    return [0xF0, 0x7D, 0x50, 0x46, cmd] + list(COMMAND_GUARD[cmd]) + [0xF7]


def build_channel_command(arg):
    """F0 7D 50 46 13 43 48 <arg> F7 (flx-mon3): 0x00 automatic channel,
    1..13 fix that channel, 0x7F the next of 1/6/11 after the current one."""
    if not (arg in (CHANNEL_AUTO, CHANNEL_NEXT) or 1 <= arg <= 13):
        raise ValueError("channel argument %r" % (arg,))
    return [0xF0, 0x7D, 0x50, 0x46, CMD_CHANNEL] + list(COMMAND_GUARD[CMD_CHANNEL]) + [arg, 0xF7]


def next_channel(ch):
    """The next of 1, 6, 11 after channel ch (as FlowLocal's 0x7F): 1 -> 6 -> 11 -> 1,
    any other channel -> the next of them above it. None if ch is unknown."""
    if ch is None:
        return None
    for c in CHANNEL_CANDIDATES:
        if c > ch:
            return c
    return CHANNEL_CANDIDATES[0]


def _build_text(cmd, fields):
    """Like FlowLocal's TextOut: a field that does not fit in MAX_TEXT bytes
    is left out, and so is every field after it."""
    text = b""
    for k, v in fields.items():
        if v is None:
            v = ""
        elif isinstance(v, bool):
            v = "1" if v else "0"
        elif isinstance(v, (list, tuple)):
            v = ",".join("-" if x is None else str(x) for x in v)
        part = ("%s=%s" % (k, v)).encode("ascii")
        joined = text + b";" + part if text else part
        if len(joined) > MAX_TEXT:
            break
        text = joined
    return list(SYSEX_HEAD) + [cmd] + list(text) + [0xF7]


def build_status_reply(fields):
    """The reply FlowLocal would send for these fields (tests and --demo)."""
    return _build_text(CMD_STATUS_REPLY, fields)


def build_ack(cmd, ok, msg=""):
    """The ack FlowLocal would send for a command (tests and --demo)."""
    return _build_text(CMD_ACK, collections.OrderedDict([("cmd", "%02X" % cmd), ("ok", 1 if ok else 0), ("msg", msg)]))


def is_pf_sysex(msg):
    return len(msg) >= 5 and tuple(msg[:4]) == SYSEX_HEAD


def _last_sysex(msg):
    """The part from the last F0 of a complete SysEx (ends in F7), or None.
    If a driver glued an abandoned start (no F7) onto the next message, the
    last F0 wins."""
    if not msg or len(msg) < 6:
        return None
    b = list(msg)
    if b[-1] != 0xF7:
        return None
    try:
        start = len(b) - 1 - b[::-1].index(0xF0)
    except ValueError:
        return None
    return b[start:]


def pf_command(msg):
    """The command byte of a complete PF SysEx, or None."""
    b = _last_sysex(msg)
    if b is None or len(b) < 6 or tuple(b[:4]) != SYSEX_HEAD:
        return None
    return b[4]


def parse_pf_text(msg, cmd):
    """key -> value strings of one complete PF SysEx with this command byte
    whose body is key=value text, or None.

    None for anything else: another SysEx, a reply without F7 (FlowLocal
    abandons a reply the USB FIFO will not take), bytes outside 0x20..0x7E."""
    b = _last_sysex(msg)
    if b is None or len(b) < 6 or tuple(b[:4]) != SYSEX_HEAD or b[4] != cmd:
        return None
    text = b[5:-1]
    if len(text) > MAX_TEXT_READ or any(x < 0x20 or x > 0x7E for x in text):
        return None
    fields = {}
    for part in bytes(text).decode("ascii").split(";"):
        if "=" not in part:
            continue
        k, v = part.split("=", 1)
        k = k.strip()
        if k:
            fields[k] = v.strip()
    return fields


def parse_status_reply(msg):
    return parse_pf_text(msg, CMD_STATUS_REPLY)


def parse_ack(msg):
    return parse_pf_text(msg, CMD_ACK)


_INT = re.compile(r"^-?\d{1,12}$")
_IPV4 = re.compile(r"^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$")
_TAG = re.compile(r"^[!-~]{1,40}$")
_WORD = re.compile(r"^[a-z][a-z0-9_]{0,23}$")


def _to_int(v, lo=None, hi=None):
    if v is None:
        return None
    v = v.strip()
    if not _INT.match(v):
        return None
    n = int(v)
    if (lo is not None and n < lo) or (hi is not None and n > hi):
        return None
    return n


def _parse_knobs(v):
    if not v:
        return None
    parts = v.split(",")
    if len(parts) != 3:
        return None
    return [_to_int(p, 0, 127) for p in parts]


def _parse_scores(v):
    """cs= (flx-mon3): the last boot scan's score per candidate channel,
    "1:120,6:40,11:300" (lower is quieter) -> [[1, 120], [6, 40], [11, 300]].
    Lenient: parts that do not read as channel:score are skipped; None when
    nothing is left (empty: no scan, e.g. a fixed channel)."""
    out = []
    for part in (v or "").split(","):
        if ":" not in part:
            continue
        c, s = part.split(":", 1)
        c, s = _to_int(c, 1, 14), _to_int(s, 0)
        if c is not None and s is not None:
            out.append([c, s])
    return out or None


def interpret_status(raw):
    """Typed fields of a parsed reply. A missing or malformed value is None;
    hd/ra keep -1 for "never", jt/jm turn -1 ("unknown") into None. Unknown
    keys are kept in "extra"."""
    st = {"v": _to_int(raw.get("v"), 0)}
    fw = raw.get("fw") or ""
    st["fw"] = fw if _TAG.match(fw) else None
    st["up"] = _to_int(raw.get("up"), 0)
    ap = _to_int(raw.get("ap"), 0, 1)
    st["ap"] = None if ap is None else bool(ap)
    st["ch"] = _to_int(raw.get("ch"), 1, 14)
    chm = (raw.get("chm") or "").strip().lower()
    st["chm"] = chm if chm in CHANNEL_MODE_WORDS else None
    st["cl"] = _to_int(raw.get("cl"), 0, 255)
    st["cm"] = _to_int(raw.get("cm"), 0, 255)
    st["ps"] = _to_int(raw.get("ps"), 0, 255)
    pip = raw.get("pip") or ""
    m = _IPV4.match(pip)
    st["pip"] = pip if m and all(int(g) <= 255 for g in m.groups()) else None
    st["prs"] = _to_int(raw.get("prs"), -127, 0)
    st["hd"] = _to_int(raw.get("hd"), -1)
    st["ra"] = _to_int(raw.get("ra"), -1)
    lp = _to_int(raw.get("lp"), -1, 127)
    st["lp"] = None if lp in (None, -1) else lp
    st["kn"] = _parse_knobs(raw.get("kn"))
    for k in ("jt", "jm"):
        n = _to_int(raw.get(k), -1, 3600000)
        st[k] = None if n in (None, -1) else n
    for k in COUNTER_KEYS:
        st[k] = _to_int(raw.get(k), 0)
    # flx-mon2: why FlowLocal last started, boots since power-on, USB soft reconnects
    rsn = (raw.get("rsn") or "").strip().lower()
    st["rsn"] = rsn if _WORD.match(rsn) else None
    st["bc"] = _to_int(raw.get("bc"), 0)
    why = (raw.get("why") or "").strip()
    st["why"] = why if _TAG.match(why) else None
    st["ur"] = _to_int(raw.get("ur"), 0)
    # flx-mon3: the boot scan's scores
    st["cs"] = _parse_scores(raw.get("cs"))
    st["extra"] = {k: v for k, v in raw.items() if k not in KNOWN_KEYS}
    return st


_MON_TAG = re.compile(r"^flx-mon(\d{1,3})\b")


def channel_command_support(st, raw=None):
    """Does this FlowLocal take command 13? True from flx-mon3 on (its tag,
    or what only it sends: chm=manual, a cs= key), False for a firmware known
    to be older (flx-mon1, flx-mon2), None when the tag says nothing (then the
    press is allowed and a missing ack tells)."""
    if st is None:
        return None
    if st.get("chm") == "manual" or (raw is not None and "cs" in raw):
        return True
    m = _MON_TAG.match(st.get("fw") or "")
    if m:
        return int(m.group(1)) >= 3
    return None


def scores_text(cs):
    """ "1번 120 · 6번 40 · 11번 300" """
    if not cs:
        return None
    return " · ".join("%d번 %d" % (c, s) for c, s in cs)


def interpret_ack(raw):
    """{"cmd": 0x10..0x12 or None, "ok": bool or None, "msg": short text}."""
    cmd = None
    c = (raw.get("cmd") or "").strip()
    if re.match(r"^[0-9A-Fa-f]{2}$", c) and int(c, 16) in COMMAND_GUARD:
        cmd = int(c, 16)
    ok = _to_int(raw.get("ok"), 0, 1)
    msg = (raw.get("msg") or "").strip()[:80]
    return {"cmd": cmd, "ok": None if ok is None else bool(ok), "msg": msg}


# FlowLocal's ack messages (fl_midi_bridge.h) in plain Korean, and what a
# refusal means for the next step (see Tracker._refused).
ACK_REASONS = (
    (r"^ended \d+ RTP session", "패널과의 연결을 끊었어요. 패널이 곧 다시 붙어요", None),
    (r"^restarting", "작은 보드가 다시 켜져요", None),
    (r"^panel [\d.]+ is rebooting", "패널이 다시 켜져요", None),
    (r"^no RTP session to end", "지금은 끊을 패널 연결이 없어요", "no_session"),
    (r"^no RTP yet", "작은 보드가 아직 켜지는 중이에요", "starting"),
    (r"^too soon", "너무 빨리 다시 눌렀어요", "too_soon"),
    (r"^busy", "앞의 '패널 다시 켜기'가 아직 끝나지 않았어요", "busy"),
    (r"^no panel session and the hotspot is not up", "핫스팟이 아직 안 켜졌어요", "starting"),
    (r"^no panel session and no device", "핫스팟에 패널이 없어요", "absent"),
    (r"^no panel session and \d+ devices", "핫스팟에 기기가 여러 대라 어느 것이 패널인지 몰라요", "ambiguous"),
    (r"^no connection to panel", "패널에 닿지 않아요", "unreachable"),
    (r"^panel [\d.]+ closed the connection", "패널이 요청 도중 연결을 끊었어요", "unreachable"),
    (r"^no HTTP answer from panel", "패널이 대답하지 않아요", "unreachable"),
    (r"^panel [\d.]+ answered HTTP", "패널이 이 요청을 받지 않았어요(패널 펌웨어가 예전 것일 수 있어요)", "unreachable"),
    (r"^could not start", "작은 보드가 요청을 시작하지 못했어요", None),
    # command 13 (flx-mon3); FlowLocal's exact words are not known yet, so loosely
    (r"(?i)^(channel )?auto", "채널을 자동으로 되돌리고 다시 켜져요", None),
    (r"(?i)^channel \d+", "채널을 바꾸고 다시 켜져요", None),
    (r"(?i)^(bad|unknown|invalid) (channel|argument)", "작은 보드가 이 채널을 받지 않았어요", "bad_channel"),
)


def ack_reason(msg):
    """(Korean text or None, kind or None) for an ack's msg=."""
    for pattern, text, kind in ACK_REASONS:
        if re.match(pattern, msg or ""):
            return text, kind
    return None, None


def ack_text(msg):
    """The ack's msg= for people: Korean, with FlowLocal's words after it."""
    ko, _kind = ack_reason(msg)
    if ko and msg:
        return "%s (%s)" % (ko, msg)
    return ko or msg or ""


class BootTracker(object):
    """Notices a FlowLocal reboot from its uptime.

    Each reply implies a boot time (now - up). A reboot moves it later - by
    at least the old uptime when "up" drops, and also when FlowLocal was
    gone longer than it had been up. Comparing each reply with the one
    before (2 s apart) keeps crystal drift between the boards (tens of ppm,
    seconds per day) from ever looking like a reboot."""

    SLACK_S = 5.0

    def __init__(self):
        self.boot_at = None

    def feed(self, up, now):
        if up is None:
            return False
        boot = now - up
        rebooted = self.boot_at is not None and boot - self.boot_at > self.SLACK_S
        self.boot_at = boot
        return rebooted


# ================================================================ arrivals

class ArrivalClock(object):
    """When each message reached the laptop.

    The MIDI thread reads the port every 10 ms, so its read time cannot tell
    four messages 2 ms apart from four that came together. rtmidi gives each
    message the time since the one before (from the driver's timestamps);
    this chains those onto the read time. It re-anchors on the read time
    after a pause (> 1 s), when the chain runs ahead of the read time or far
    behind it, and for good if the deltas look broken: zero for messages that
    were read in different passes, BROKEN_AFTER times within SUSPECT_WINDOW_S.
    Broken deltas do that on every knob turn; a stall of this thread only now
    and then, so those few are forgotten."""

    PAUSE_S = 1.0
    AHEAD_S = 0.02
    BEHIND_S = 5.0
    BROKEN_AFTER = 5
    SUSPECT_WINDOW_S = 60.0

    def __init__(self):
        self.last = None
        self.last_read = None
        self.suspects = collections.deque()
        self.trusted = True

    def reset(self):
        self.last = self.last_read = None

    def feed(self, delta, read_at):
        t = read_at
        if self.trusted and self.last is not None and delta is not None and 0 <= delta <= self.PAUSE_S:
            if delta < 0.0005 and self.last_read is not None and read_at - self.last_read > 0.03:
                self.suspects.append(read_at)
                while read_at - self.suspects[0] > self.SUSPECT_WINDOW_S:
                    self.suspects.popleft()
                if len(self.suspects) >= self.BROKEN_AFTER:
                    self.trusted = False
            else:
                chained = self.last + delta
                if read_at - self.BEHIND_S <= chained <= read_at + self.AHEAD_S:
                    t = min(chained, read_at)
        self.last, self.last_read = t, read_at
        return t


class KnobBunchDetector(object):
    """Held-up knob messages. The panel paces each knob (CC 24-26, channel 1)
    to one CC per 50 ms, so two CCs of the same knob closer than BUNCH_PAIR_S
    were held on the way and let go together. Pairs within BUNCH_GROUP_S of
    the last one are the same hold-up ("bunch").

    Never counted: Program Changes, CC 118, notes, other CCs, and bundles
    with one CC per knob (the panel's hello, FlowLocal's resync answer and
    USB re-mount re-send, all "PC then K1-K3"); a knob's first CC within
    BUNDLE_AFTER_PC_S after a Program Change (FlowLocal's cache in such a
    bundle) neither pairs nor is paired with; a CC that repeats its knob's
    last value (a re-send, not a turn: the panel sends a knob only when it
    changes).

    feed() takes a message's arrival time; entries are the tracker's
    timeline points, [t, kind code, bunched], whose flag it sets."""

    def __init__(self):
        self.last = [None, None, None]      # per knob: [t, value, entry, t before, in a bundle, flagged]
        self.pc_at = None
        self.bundle = set()                 # knobs whose first CC after the last PC is still to come
        self.recent = collections.deque()   # hold-ups: [t first, messages, pause before (s) or None, knobs, t last]
        self.total = 0

    def reset(self):
        """A new port handle: arrival times start over (the counts stay)."""
        self.last = [None, None, None]
        self.pc_at = None
        self.bundle = set()

    def feed(self, t, kind, knob=None, value=None, entry=None):
        """True when this message made or grew a hold-up."""
        if kind == "pc":
            self.pc_at, self.bundle = t, {0, 1, 2}
            return False
        if kind != "knob" or knob not in (0, 1, 2):
            return False
        bundled = (knob in self.bundle and self.pc_at is not None
                   and 0.0 <= t - self.pc_at <= BUNDLE_AFTER_PC_S)
        self.bundle.discard(knob)
        prev = self.last[knob]
        rec = [t, value, entry, None if prev is None else prev[0], bundled, False]
        self.last[knob] = rec
        if bundled or prev is None or prev[4]:
            return False
        pt, pv, pentry, pbefore, _pb, pflagged = prev
        if not (0.0 <= t - pt <= BUNCH_PAIR_S) or value == pv:
            return False
        ev = self.recent[-1] if self.recent else None
        if ev is not None and pt - ev[4] <= BUNCH_GROUP_S:
            ev[1] += 1 if pflagged else 2
            ev[3] = "".join(sorted(set(ev[3]) | {str(knob + 1)}))
            ev[4] = t
        else:
            pause = None if pbefore is None or pt - pbefore > BUNCH_GAP_MAX_S else pt - pbefore
            self.recent.append([pt, 2, pause, str(knob + 1), t])
            self.total += 1
        prev[5] = rec[5] = True
        for e in (pentry, entry):
            if e is not None:
                e[2] = True
        while self.recent and t - self.recent[0][0] > 120.0:
            self.recent.popleft()
        return True

    def count(self, now, window=60.0, since=None):
        """Hold-ups that started in the last `window` s, and not before `since`."""
        lo = now - window if since is None else max(now - window, since)
        return sum(1 for b in self.recent if lo <= b[0] <= now + 0.05)

    def last_bunch(self):
        return self.recent[-1] if self.recent else None


# ================================================================ events

class EventLog(object):
    """Recent events for the dashboard, and one line each in logs/YYYY-MM-DD.log."""

    def __init__(self, log_dir, maxlen=200, wall=time.time):
        self.dir = log_dir
        self.wall = wall
        self.items = collections.deque(maxlen=maxlen)
        self.lock = threading.Lock()
        self.write_error = None
        if log_dir:
            try:
                os.makedirs(log_dir, exist_ok=True)
            except OSError as e:
                self.write_error = str(e)

    def add(self, kind, text, level="info", ui=True):
        ts = self.wall()
        lt = time.localtime(ts)
        entry = {"t": round(ts, 3), "time": time.strftime("%H:%M:%S", lt), "kind": kind,
                 "level": level, "text": text}
        with self.lock:
            if ui:
                self.items.append(entry)
            if self.dir:
                path = os.path.join(self.dir, time.strftime("%Y-%m-%d", lt) + ".log")
                try:
                    with open(path, "a", encoding="utf-8") as f:
                        f.write("%s [%s] %s%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S", lt), kind,
                                                    "" if level == "info" else "(%s) " % LEVEL_WORD.get(level, level),
                                                    text))
                    self.write_error = None
                except OSError as e:
                    self.write_error = str(e)
        return entry

    def recent(self, n=40):
        with self.lock:
            items = list(self.items)[-n:]
        items.reverse()
        return items

    def today_path(self):
        if not self.dir:
            return None
        return os.path.join(self.dir, time.strftime("%Y-%m-%d") + ".log")


# ================================================================ memory

_LOG_NAME = re.compile(r"^\d{4}-\d\d-\d\d\.log$")
# the line the monitor writes on FlowLocal's first status reply (Tracker._on_status)
_LOG_FIRST_REPLY = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) \[status\] (?:\([^)]*\) )?"
                              r"FlowLocal 상태를 받기 시작했어요 \(펌웨어 ([^,\s)]+)")


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


class Memory(object):
    """What the monitor keeps across its own restarts, in logs/state.json:
    that FlowLocal answered the PF monitor protocol on this laptop (so a
    FlowLocal that is silent later is a stuck one, not old firmware), its
    firmware tag and when it last answered, and the last FlowLocal
    self-restart already written to the log (so it is not written again).

    Robust to a bad file: a missing one is a fresh start, one that is not
    JSON (or not an object) is kept as state.json.bad and written anew, a
    field of the wrong type is ignored. It is written whole to
    state.json.tmp and moved over the old one. Without a path it only
    lives in memory (tests, --demo)."""

    VERSION = 1

    def __init__(self, path=None, wall=time.time):
        self.path = path
        self.wall = wall
        self.lock = threading.Lock()
        self.fl_protocol_seen = False
        self.fl_fw = None
        self.fl_seen_at = None            # wall time FlowLocal last answered (kept to MEMORY_SAVE_S)
        self.fl_first_seen_at = None
        self.restart_noted = None         # {"boot_at": wall, "bc": n, "rsn": s} of the last self-restart logged
        self.source = None                # where fl_protocol_seen came from: "file", "logs", "run"
        self.error = None                 # the last problem reading or writing the file (Korean)
        self.error_new = False            # ... not yet written to the event log
        self.dirty = False
        self.urgent = False               # something worth writing at once (first answer, a new firmware)
        self.saved_at = None
        if path:
            self.load()

    def _problem(self, text):
        self.error, self.error_new = text, True

    def load(self):
        """Reads the file. False when it is missing or unusable (then all is as new)."""
        try:
            with open(self.path, encoding="utf-8-sig") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                raise ValueError("맨 바깥이 { } 가 아니에요")
        except FileNotFoundError:
            return False
        except (OSError, ValueError) as e:
            bad = self.path + ".bad"
            try:
                os.replace(self.path, bad)
                kept = " 망가진 파일은 %s로 남겨 뒀어요." % os.path.basename(bad)
            except OSError:
                kept = ""
            self._problem("모니터 기억 파일(state.json)을 읽지 못해서 새로 만들어요 (%s).%s" % (e, kept))
            return False
        with self.lock:
            if data.get("fl_protocol_seen") is True:
                self.fl_protocol_seen = True
                self.source = "file"
            fw = data.get("fl_fw")
            self.fl_fw = fw if isinstance(fw, str) and _TAG.match(fw) else None
            for key in ("fl_seen_at", "fl_first_seen_at"):
                v = data.get(key)
                setattr(self, key, float(v) if _num(v) and v > 0 else None)
            rn = data.get("fl_restart_noted")
            if isinstance(rn, dict) and _num(rn.get("boot_at")):
                bc, rsn = rn.get("bc"), rn.get("rsn")
                self.restart_noted = {"boot_at": float(rn["boot_at"]),
                                      "bc": bc if isinstance(bc, int) and not isinstance(bc, bool) else None,
                                      "rsn": rsn if isinstance(rsn, str) and _WORD.match(rsn) else None}
        return True

    def seed_from_logs(self, log_dir, max_files=31):
        """A FlowLocal that answered before this file existed (or after it was
        lost) is still in the event logs: the newest "FlowLocal 상태를 받기
        시작했어요 (펌웨어 ...)" line. True if one was found."""
        try:
            names = sorted((n for n in os.listdir(log_dir) if _LOG_NAME.match(n)), reverse=True)[:max_files]
        except OSError:
            return False
        for name in names:
            found = None
            try:
                with open(os.path.join(log_dir, name), encoding="utf-8", errors="replace") as f:
                    for line in f:
                        if "받기 시작했어요" in line:
                            m = _LOG_FIRST_REPLY.match(line)
                            if m:
                                found = m
            except OSError:
                continue
            if found is None:
                continue
            try:
                at = time.mktime(time.strptime(found.group(1), "%Y-%m-%d %H:%M:%S"))
            except (ValueError, OverflowError):
                at = None
            fw = found.group(2)
            with self.lock:
                self.fl_protocol_seen = True
                self.source = "logs"
                self.fl_fw = fw if fw != "?" and _TAG.match(fw) else self.fl_fw
                if at is not None:
                    self.fl_seen_at = at
                    if self.fl_first_seen_at is None:
                        self.fl_first_seen_at = at
                self.dirty = self.urgent = True
            return True
        return False

    def remember(self, fw, when):
        """FlowLocal answered the protocol (a status reply): cheap, called for each."""
        with self.lock:
            if not self.fl_protocol_seen or (fw and fw != self.fl_fw):
                self.urgent = True
            if self.source is None:
                self.source = "run"
            self.fl_protocol_seen = True
            if fw:
                self.fl_fw = fw
            if self.fl_first_seen_at is None:
                self.fl_first_seen_at = when
            self.fl_seen_at = when
            self.dirty = True

    def restart_already_noted(self, boot_at, bc, rsn):
        with self.lock:
            rn = self.restart_noted
            return (rn is not None and rn["bc"] == bc and rn["rsn"] == rsn
                    and abs(rn["boot_at"] - boot_at) <= 30.0)

    def note_restart(self, boot_at, bc, rsn):
        with self.lock:
            self.restart_noted = {"boot_at": boot_at, "bc": bc, "rsn": rsn}
            self.dirty = self.urgent = True

    def as_dict(self):
        seen = self.fl_seen_at
        return collections.OrderedDict([
            ("_설명", "패턴플로우 모니터가 다시 켜져도 기억하는 것이에요. 모니터가 고쳐 써요. "
                     "지워도 괜찮아요(그러면 모니터가 logs의 기록에서 다시 찾아요)."),
            ("version", self.VERSION),
            ("fl_protocol_seen", self.fl_protocol_seen),
            ("fl_fw", self.fl_fw),
            ("fl_seen_at", None if seen is None else round(seen, 1)),
            ("fl_seen_local", None if seen is None else time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(seen))),
            ("fl_first_seen_at", None if self.fl_first_seen_at is None else round(self.fl_first_seen_at, 1)),
            ("fl_restart_noted", self.restart_noted),
        ])

    def flush(self, force=False):
        """Writes the file if something changed: at once for the first answer,
        a new firmware or a self-restart, else at most every MEMORY_SAVE_S.
        True when it wrote."""
        with self.lock:
            if not self.path or not self.dirty:
                return False
            now = self.wall()
            if not (force or self.urgent or self.saved_at is None or now - self.saved_at >= MEMORY_SAVE_S):
                return False
            text = json.dumps(self.as_dict(), ensure_ascii=False, indent=2)
            tmp = self.path + ".tmp"
            try:
                os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
                with open(tmp, "w", encoding="utf-8") as f:
                    f.write(text + "\n")
                os.replace(tmp, self.path)
            except OSError as e:
                if self.error != "모니터 기억 파일(state.json)을 쓰지 못했어요: %s" % e:
                    self._problem("모니터 기억 파일(state.json)을 쓰지 못했어요: %s" % e)
                self.saved_at = now           # try again later, not every second
                return False
            self.dirty = self.urgent = False
            self.saved_at = now
            if self.error and self.error.startswith("모니터 기억 파일(state.json)을 쓰지"):
                self.error = None
            return True

    def take_error(self):
        """A problem not yet written to the event log, once."""
        with self.lock:
            if self.error_new:
                self.error_new = False
                return self.error
            return None

    def view(self, now_wall=None):
        now_wall = self.wall() if now_wall is None else now_wall
        with self.lock:
            seen = self.fl_seen_at
            return {
                "seen": self.fl_protocol_seen,
                "fw": self.fl_fw,
                "seen_age": None if seen is None else round(max(0.0, now_wall - seen), 1),
                "seen_local": None if seen is None else time.strftime("%Y-%m-%d %H:%M", time.localtime(seen)),
                "source": self.source,
                "error": self.error,
                "path": self.path,
            }


def open_memory(log_dir, wall=time.time):
    """The monitor's Memory for this log folder: state.json, else the logs."""
    if not log_dir:
        return Memory(None, wall)
    mem = Memory(os.path.join(log_dir, "state.json"), wall)
    if not mem.fl_protocol_seen:
        mem.seed_from_logs(log_dir)
    return mem


# ================================================================ tracker

KIND_CODE = {"pc": "p", "knob": "k", "cc": "c", "note": "n", "note_off": "o", "other": "x"}


def describe_message(msg):
    """(kind, Korean text) for a channel message from the panel, or (None, None)."""
    st = msg[0]
    hi, ch = st & 0xF0, (st & 0x0F) + 1
    d1 = msg[1] if len(msg) > 1 else 0
    d2 = msg[2] if len(msg) > 2 else 0
    if hi == 0xC0:
        return "pc", "패턴 %d" % d1
    if hi == 0xB0:
        if ch == 1 and d1 in KNOB_CCS:
            return "knob", "K%d = %d" % (d1 - 23, d2)
        return "cc", "CC %d = %d (채널 %d)" % (d1, d2, ch)
    if hi == 0x90 and d2 > 0:
        if ch == 10:
            return "note", "발소리 노트 %d" % d1
        if 60 <= d1 <= 62:
            return "note", "K%d 누름" % (d1 - 59)
        return "note", "노트 %d (채널 %d)" % (d1, ch)
    if hi in (0x80, 0x90):
        return "note_off", None
    return "other", None


class Tracker(object):
    """Everything the monitor knows, fed by the MIDI thread and the system
    checks, read by the web server as snapshot(). Takes its clock as an
    argument so the tests and --demo can script time. The fix buttons'
    progress (fix_begin / fix_sent / acks / fix_update) lives here too, so
    it follows the same readings."""

    def __init__(self, events=None, clock=time.monotonic, midi_lib=None, memory=None, keeper=None):
        self.lock = threading.RLock()
        self.clock = clock
        self.events = events if events is not None else EventLog(None)
        self.memory = memory if memory is not None else Memory(None, self.events.wall)
        # only LiveApp gives a keeper that can start Live; the default one never does
        self.keeper = keeper if keeper is not None else LiveKeeper(self.events)
        self.started = clock()
        self.midi_lib = (rtmidi is not None) if midi_lib is None else midi_lib
        # port
        self.port_checked = False
        self.port_present = False
        self.port_changed_at = self.started
        self.in_name = self.out_name = None
        self.matches = 0
        self.in_open = self.out_open = False
        self.in_open_at = self.out_open_at = None
        self.open_error = None
        # status replies
        self.requests = 0
        self.replies = 0
        self.bad_replies = 0
        self.status = None
        self.status_raw = None
        self.status_at = None
        self.status_ever_port = False     # a reply since the port (re)appeared
        self.status_lost_noted = False
        self.old_fw_noted = False
        self.boot = BootTracker()
        self.fl_reboot_at = None
        self.prev_ps = None
        self.prev_cl = None
        self.session_changed_at = None
        self.session_down_at = None
        self.session_up_at = None
        self.stations_since = None        # stations on the hotspot but no session, since
        self.prev_to = None
        self.prev_hd = None
        self.drop_silent = False          # the last session ended after the panel went quiet (timeout)
        self.fl_protocol_seen = False     # a v1 status reply came since the monitor started
        self.fl_restart_ack_at = None     # FlowLocal acked a restart (MidiWorker reopens the port)
        self.fl_restarts = collections.deque()   # FlowLocal left USB or restarted (not for the button), times
        self.port_dropped_at = None       # the port last left the list (not for the button)
        self.flap_noted = False           # "keeps restarting" is in the log for this run of restarts
        self.boot_info = None             # (bc, rsn) of FlowLocal's current start
        self.self_restart = None          # that start was FlowLocal restarting by itself (rsn=, bc=)
        self.prev_ur = None
        self.gp_hist = collections.deque()
        self.restart_asked_at = None      # a button or the one-button run asked FlowLocal to restart
        # evidence for the log (_note_evidence)
        self.prev_counts = {}             # se, to, rj of the last reply
        self.gp_noted = None              # [gp written last, when]
        self.new_counters = {}            # unknown integer keys: {"v": last, "noted": value written, "at": when, "off": not a counter}
        self.chan_key = None              # (boot, ch, chm) of the channel last written to the log
        self.join_at = None               # the panel joined the hotspot (cl 0 -> n), no session yet
        self.connects = collections.deque(maxlen=20)   # [time, s from joining to the session, slow]
        # panel MIDI
        self.midi_at = None
        self.midi_count = 0
        self.midi_since_open = 0
        self.recent = collections.deque()
        self.pc = None
        self.pc_at = None
        self.pc_count = 0
        self.knobs = [None, None, None]
        self.knob_at = [None, None, None]
        self.last_text = None
        self.note_count = 0
        self.arrivals = collections.deque()   # [t, kind code, held up (bunched)]
        self.bunches = KnobBunchDetector()
        self.arrival_source = "read"
        # system
        self.live = None
        self.live_names = []
        self.live_seen = False
        self.live_changed_at = None
        self.live_exe = None              # Live's exe while it ran (the keeper's fallback)
        self.ac = None
        self.battery = None
        self.has_battery = None
        self.low_battery_noted = False
        self.power_items = None
        self.power_checked_at = None
        # buttons
        self.fix = None
        self.fix_seq = 0
        self.fix_pressed = {}             # action -> time of its last press
        self.fix_failed = {}              # action -> (time, result, needs new firmware) of a press that did not help
        self.acks = 0
        self.acks_bad = 0
        self.acks_ok_ever = False         # a well-formed ack came: FlowLocal takes commands
        # the one big button's sequence
        self.reconnect = Reconnector(self)

    # ------------------------------------------------ inputs: port

    def on_ports(self, present, in_name, out_name, matches, now):
        with self.lock:
            first = not self.port_checked
            self.port_checked = True
            if matches > 1 and matches != self.matches:
                self.events.add("port", "이름이 '%s'인 MIDI 포트가 %d개 보여요. 첫째 것을 씁니다." % (PORT_NAME, matches), "warn")
            self.matches = matches
            if present:
                self.in_name, self.out_name = in_name, out_name
            if present != self.port_present:
                self.port_present = present
                self.port_changed_at = now
                f = self.fix
                button = (f is not None and f["action"] in RESTART_ACTIONS and f["phase"] in ("ack", "recover"))
                restarting = button or self.reconnect.expects_port_loss(now)
                if present:
                    self.events.add("port", "FlowLocal 보드가 노트북에 보여요 (%s)" % in_name, "ok")
                else:
                    self._lost(now)
                    if restarting:
                        if button:
                            f["port_lost"] = True
                        self.events.add("port", "FlowLocal 보드가 다시 켜지느라 노트북에서 잠깐 사라졌어요")
                    else:
                        self.events.add("port", "FlowLocal 보드가 노트북에서 사라졌어요 (USB 빠짐 또는 보드 재시작)", "bad")
                        self.port_dropped_at = now
                        self._restarted(now)
                self.fix_update(now)
            elif first and not present:
                self.events.add("port", "FlowLocal 보드가 노트북에 안 보여요", "bad")

    def _restarted(self, now):
        """FlowLocal left USB or started again, not for the button: FLAP_COUNT
        of these within FLAP_WINDOW_S is a board that keeps restarting (a
        loose cable or too little USB power), noted once per run."""
        self.fl_restarts.append(now)
        while self.fl_restarts and now - self.fl_restarts[0] > FLAP_WINDOW_S + FLAP_NOTE_S:
            self.fl_restarts.popleft()
        n = sum(1 for t in self.fl_restarts if now - t <= FLAP_WINDOW_S)
        if n >= FLAP_COUNT and not self.flap_noted:
            self.flap_noted = True
            self.events.add("flowlocal", "FlowLocal이 계속 다시 켜지고 있어요 (%s 안에 %d번 노트북에서 사라졌다 돌아옴) "
                            "- 케이블이 같은 테이프 자리에 끝까지 꽂혔는지, 노트북 충전기가 꽂혔는지 봐요" % (
                                fmt_dur(FLAP_WINDOW_S), n), "bad")

    def _restart_expected(self, boot_at):
        """FlowLocal started again because someone asked: [FlowLocal 다시 켜기],
        a channel button, the one-button run (its command or a person's RST)."""
        if boot_at is None:
            return False
        times = [self.fl_restart_ack_at, self.restart_asked_at] + [self.fix_pressed.get(a) for a in RESTART_ACTIONS]
        for t in times:
            if t is not None and -5.0 <= boot_at - t <= 60.0:
                return True
        return self.reconnect.expects_boot(boot_at)

    def _start_reason(self, st, now, boot_at):
        """rsn= / bc= (flx-mon2) of FlowLocal's current start. A start that
        was FlowLocal restarting by itself - for power, a firmware fault, or
        any start after the first since power-on (bc > 1), but not for the
        [FlowLocal 다시 켜기] button - becomes a yellow note (self_restart)
        and one line in the log, once per start even across monitor
        restarts (Memory). True if that line was written or had been."""
        rsn, bc, why = st["rsn"], st["bc"], st["why"]
        asked = RESTART_NOTES.get(why, (None, None))[1] == "asked"
        kind = reset_kind(rsn) or (RESTART_NOTES[why][1] if why in RESTART_NOTES else None)
        if boot_at is None or asked or not (kind in ("power", "crash") or (bc is not None and bc > 1)):
            self.self_restart = None
            return False
        if rsn in ("sw", None) and self._restart_expected(boot_at):
            self.self_restart = None
            return False
        # a software restart says more through its note ("USB로 대답이 안 나감") than "펌웨어가 다시 켬"
        word = note_word(why) if (why and reset_kind(rsn) is None and why in RESTART_NOTES) else reset_word(rsn)
        self.self_restart = {"rsn": rsn, "bc": bc, "why": why, "kind": kind or "other", "word": word,
                             "boot_at": boot_at}
        wall_boot = self.memory.wall() - (now - boot_at)
        if self.memory.restart_already_noted(wall_boot, bc, rsn):
            return True
        self.memory.note_restart(wall_boot, bc, rsn)
        parts = ["이유: " + word]
        if why and note_word(why) != word:
            parts.append("기록: " + note_word(why))
        if bc is not None and bc > 1:
            parts.append("전원을 넣은 뒤 %d번째 켜짐" % bc)
        parts.append("방금" if now - boot_at < 2 else "%s 전" % fmt_dur(now - boot_at))
        self.events.add("flowlocal", "FlowLocal이 스스로 다시 켜졌어요 (%s)" % ", ".join(parts), "warn")
        return True

    def known_to_answer(self):
        """FlowLocal answered the PF monitor protocol on this laptop: this run or an earlier one."""
        return self.fl_protocol_seen or self.acks_ok_ever or self.memory.fl_protocol_seen

    def _lost(self, now):
        self.in_open = self.out_open = False
        self.in_open_at = self.out_open_at = None
        self.status_ever_port = False
        self.status_lost_noted = False
        self.old_fw_noted = False
        self.midi_since_open = 0
        self.gp_hist.clear()

    def on_opened(self, in_ok, out_ok, error, now):
        with self.lock:
            if out_ok and not self.out_open:
                self.out_open_at = now
            if in_ok and not self.in_open:
                self.in_open_at = now
            if (in_ok and not self.in_open) or (out_ok and not self.out_open):
                if in_ok and out_ok:
                    self.events.add("port", "모니터가 FlowLocal 포트를 열었어요", "ok")
            self.in_open, self.out_open = in_ok, out_ok
            if error and error != self.open_error:
                self.events.add("port", "모니터가 FlowLocal 포트를 열지 못했어요: %s" % error, "warn")
            self.open_error = error

    def on_reopen(self, why, now):
        """MidiWorker set its handles aside while the port stayed listed and
        opens it again at once (on_opened follows). The panel MIDI counts from
        the new handles; what FlowLocal said before stays known."""
        with self.lock:
            self.in_open_at = self.out_open_at = now
            self.midi_since_open = 0
            self.events.add("port", "모니터가 FlowLocal 포트를 새로 열어요 (%s)" % why, "warn")

    def on_request_sent(self, now):
        with self.lock:
            self.requests += 1

    def on_arrival_source(self, source):
        with self.lock:
            if source != self.arrival_source and source == "read":
                self.events.add("midi", "MIDI 도착 시각을 드라이버에서 못 받아서, 읽은 시각으로 몰림을 봐요 (10 ms 정도 덜 정확)", "warn")
            self.arrival_source = source

    # ------------------------------------------------ inputs: MIDI

    def on_midi(self, msg, now, arrival=None):
        if not msg:
            return
        st = msg[0]
        if st == 0xF0:
            self._on_sysex(msg, now)
            return
        if st < 0x80 or st >= 0xF0:
            return
        hi, ch = st & 0xF0, (st & 0x0F) + 1
        # FlowLocal's own program mirror (CC 118 ch 16) follows every PC; it is
        # not a panel message of its own.
        if hi == 0xB0 and ch == MIRROR_CHANNEL and len(msg) > 1 and msg[1] in (118, 119):
            return
        kind, text = describe_message(msg)
        t = now if arrival is None else arrival
        with self.lock:
            self.midi_at = now
            self.midi_count += 1
            self.midi_since_open += 1
            self.recent.append(now)
            if len(self.recent) > 20000:
                self.recent.popleft()
            entry = [t, KIND_CODE.get(kind, "x"), False]
            self.arrivals.append(entry)
            while len(self.arrivals) > ARRIVALS_MAX or (self.arrivals and t - self.arrivals[0][0] > TIMELINE_S + 5.0):
                self.arrivals.popleft()
            if kind == "knob":
                self.bunches.feed(t, kind, msg[1] - KNOB_CCS[0], msg[2] if len(msg) > 2 else None, entry)
            else:
                self.bunches.feed(t, kind)
            if self.midi_since_open == 1:
                self.events.add("midi", "패널 신호가 노트북에 들어오고 있어요 (%s)" % (text or "MIDI"), "ok")
            if kind == "pc":
                n = msg[1]
                self.pc_count += 1
                if n != self.pc:
                    self.events.add("pattern", "패턴 %d" % n, ui=False)
                self.pc, self.pc_at = n, now
                self.fix_update(now)
            elif kind == "knob":
                i = msg[1] - KNOB_CCS[0]
                self.knobs[i], self.knob_at[i] = msg[2], now
            elif kind == "note":
                self.note_count += 1
            if text:
                self.last_text = text

    def _on_sysex(self, msg, now):
        cmd = pf_command(msg)
        if cmd == CMD_STATUS_REPLY:
            raw = parse_status_reply(msg)
            if raw is not None:
                self._on_status(raw, now)
                return
        elif cmd == CMD_ACK:
            raw = parse_ack(msg)
            if raw is not None:
                self._on_ack(interpret_ack(raw), now)
                return
        if is_pf_sysex(msg) and cmd not in (CMD_STATUS_REQUEST,) + tuple(COMMAND_GUARD):
            with self.lock:
                self.bad_replies += 1

    def _on_status(self, raw, now):
        st = interpret_status(raw)
        with self.lock:
            self.replies += 1
            first = not self.status_ever_port
            prev_at = self.status_at
            self.status, self.status_raw, self.status_at = st, raw, now
            self.status_ever_port = True
            if first:
                self.events.add("status", "FlowLocal 상태를 받기 시작했어요 (펌웨어 %s, 켜진 지 %s)" % (
                    st["fw"] or "?", fmt_dur(st["up"])), "ok")
            elif self.status_lost_noted:
                self.events.add("status", "FlowLocal이 다시 대답해요", "ok")
            self.status_lost_noted = False
            if st["v"] is not None and st["v"] >= 1:
                self.fl_protocol_seen = True
                self.memory.remember(st["fw"], self.memory.wall())
            rebooted = self.boot.feed(st["up"], now)
            boot_at = None if st["up"] is None else now - st["up"]
            info = (st["bc"], st["rsn"])
            logged = self._start_reason(st, now, boot_at) if (rebooted or info != self.boot_info) else False
            self.boot_info = info
            if rebooted:
                self.fl_reboot_at = boot_at
                asked = RESTART_NOTES.get(st["why"], (None, None))[1] == "asked"
                expected = asked or self._restart_expected(boot_at)
                if not logged:
                    why = note_word(st["why"]) if asked else (reset_word(st["rsn"]) if st["rsn"] else None)
                    self.events.add("flowlocal", "FlowLocal이 다시 켜졌어요 (켜진 지 %s%s)" % (
                        fmt_dur(st["up"]), ", 이유: " + why if why else ""), "info" if expected else "warn")
                # the same restart as a port drop since the last reply, else it came back
                # between two port-list reads and only its uptime tells
                seen_drop = self.port_dropped_at is not None and (prev_at is None or self.port_dropped_at >= prev_at)
                if not expected and not seen_drop:
                    self._restarted(now)
                self.gp_hist.clear()
                self.prev_to = self.prev_hd = None
                self.drop_silent = False
                self.prev_ur = None
                # a new start: its counters start from 0, its channel is picked again
                self.prev_counts = {}
                self.gp_noted = None
                for c in self.new_counters.values():
                    c["v"] = c["noted"] = None
                self.chan_key = None
                self.join_at = None
            ur = st["ur"]
            if ur is not None and self.prev_ur is not None and ur > self.prev_ur:
                self.events.add("flowlocal", "FlowLocal이 USB 연결을 스스로 다시 맺었어요 (켜진 뒤 %d번, ur %d→%d%s)" % (
                    ur, self.prev_ur, ur, ", 기록: " + st["why"] if st["why"] else ""), "warn")
            if ur is not None:
                self.prev_ur = ur
            ps, cl = st["ps"], st["cl"]
            session_up = False
            if ps is not None:
                if self.prev_ps is None:
                    self.session_changed_at = now
                    if ps > 0:
                        self.session_up_at = now
                elif (ps > 0) != (self.prev_ps > 0):
                    self.session_changed_at = now
                    if ps > 0:
                        session_up = True
                        self.session_down_at = None
                        self.session_up_at = now
                        extra = ", ".join(x for x in (st["pip"], "%d dBm" % st["prs"] if st["prs"] is not None else None) if x)
                        self.events.add("session", "패널이 연결됐어요" + (" (%s)" % extra if extra else ""), "ok")
                        self._note_connect(cl, ps, now)
                    else:
                        self.session_down_at = now
                        self.session_up_at = None
                        # FlowLocal timed the session out, or the panel had been quiet
                        # before it ended: the device still on the hotspot is a panel
                        # that went dark or froze (the soft AP keeps a silent station
                        # for minutes), not one that is joining.
                        timed_out = st["to"] is not None and self.prev_to is not None and st["to"] > self.prev_to
                        self.drop_silent = timed_out or (self.prev_hd is not None and self.prev_hd > HEARD_BAD_MS)
                        self.events.add("session", "패널 연결이 끊겼어요" + (
                            " (그 전에 패널 소식이 끊겼어요)" if self.drop_silent else ""), "bad")
                self.prev_ps = ps
                if ps > 0:
                    self.session_down_at = None
                    self.drop_silent = False
            if cl == 0:
                self.drop_silent = False          # the panel left: a device that joins next is joining
            if ps == 0 and cl:
                if self.stations_since is None:
                    self.stations_since = now
            else:
                self.stations_since = None
            if st["to"] is not None:
                self.prev_to = st["to"]
            if st["hd"] is not None:
                self.prev_hd = st["hd"]
            if cl is not None:
                if self.prev_cl is not None and cl != self.prev_cl:
                    self.events.add("hotspot", "핫스팟에 붙은 기기 %d대 → %d대" % (self.prev_cl, cl))
                # the panel joining the hotspot: the clock for "joined -> session" starts
                if cl == 0:
                    self.join_at = None
                elif self.prev_cl == 0 and not ps:
                    self.join_at = now
                self.prev_cl = cl
            gp = st["gp"]
            if gp is not None:
                if self.gp_hist and gp < self.gp_hist[-1][1]:
                    self.gp_hist.clear()           # counter restarted
                self.gp_hist.append((now, gp))
                while self.gp_hist and now - self.gp_hist[0][0] > 70.0:
                    self.gp_hist.popleft()
            self._note_evidence(st, now, session_up)
            self.fix_update(now)

    def _note_connect(self, cl, ps, now):
        """A session came up (ps 0 -> n): how long after the panel joined the
        hotspot (cl 0 -> n), into the log. Replies come every 2 s, so it is
        known to about 2 s; a join and a session within one reply is "2초 안에".
        1-2 s is normal; 20-24 s was measured on 2026-09-27 (an old panel image,
        or a panel that could not reach FlowLocal): past CONNECT_SLOW_S it is a
        warning."""
        if self.join_at is not None:
            took = now - self.join_at
        elif self.prev_cl == 0 and cl:
            took = 0.0
        else:
            return                        # the join was not seen (the monitor came later)
        self.join_at = None
        slow = took > CONNECT_SLOW_S
        self.connects.append([now, round(took, 1), slow])
        text = "패널이 핫스팟에 붙고 %s 연결됐어요 (cl 0→%d, ps 0→%d, 상태 답 간격 2초)" % (
            "2초 안에" if took < 2.0 else "약 %d초 뒤" % round(took), cl or 0, ps)
        if slow:
            text += " — 느려요: 보통 1–2초예요"
        self.events.add("session", text, "warn" if slow else "info")

    def _note_evidence(self, st, now, session_up=False):
        """FlowLocal's counters into the log when they change, with their raw
        numbers, so an incident can be read afterwards: rj (invitations
        refused), to (sessions timed out), se (sessions opened), gp (packet
        gaps, summed at most once per GP_NOTE_S), the hotspot's channel (after
        each start, with flx-mon3's scan scores cs=, and when it changes), and
        an integer key this monitor does not know (a counter a later firmware
        added, e.g. evicted sessions) when it grows - never used to judge.
        A counter that went down restarted with FlowLocal: no line."""
        prev = self.prev_counts
        for key, text, level in (("rj", "FlowLocal이 패널의 연결 요청(초대)을 거절했어요", "warn"),
                                 ("to", "FlowLocal이 조용해진 패널 연결을 시간 초과로 끝냈어요", "warn"),
                                 ("se", "패널 연결이 새로 열렸어요", "info")):
            v = st.get(key)
            if v is None:
                continue
            p = prev.get(key)
            if p is not None and v > p:
                ui = True
                if key == "se" and session_up and v - p == 1:
                    ui = False            # the "패널이 연결됐어요" line says it; the file keeps the numbers
                    text = "패널 연결이 새로 열렸어요"
                elif key == "se" and not session_up:
                    text = "패널이 연결을 새로 열었어요 (두 상태 답 사이라 끊김이 화면에 안 보였어요)"
                self.events.add("counter", "%s (%s %d→%d)" % (text, key, p, v), level, ui=ui)
            prev[key] = v
        gp = st.get("gp")
        if gp is not None:
            g = self.gp_noted             # [gp last written (or the baseline), when written]
            if g is None or gp < g[0]:
                self.gp_noted = [gp, None]
            elif gp > g[0] and (g[1] is None or now - g[1] >= GP_NOTE_S):
                n = gp - g[0]
                span = None if g[1] is None else now - g[1]
                rate = n if span is None else n * 60.0 / max(span, 60.0)
                level = "warn" if rate >= GAPS_WARN_PER_MIN else "info"
                self.events.add("counter", "무선에서 RTP 패킷이 빠졌어요: %d개%s (gp %d→%d)" % (
                    n, "" if span is None else ", 최근 %s 동안" % fmt_dur(span), g[0], gp), level, ui=level == "warn")
                self.gp_noted = [gp, now]
        for key, v in (st.get("extra") or {}).items():
            if not re.match(r"^[a-z]{1,4}$", key):
                continue
            n = _to_int(v, 0)
            c = self.new_counters.get(key)
            if c is None:
                c = self.new_counters[key] = {"v": None, "noted": None, "at": None, "off": False}
            if c["off"]:
                continue
            if n is None or (c["v"] is not None and n < c["v"]):
                c["off"] = True           # not a number, or it went down without a restart: not a counter
                continue
            if c["v"] is None:
                c["v"] = c["noted"] = n
                continue
            c["v"] = n
            if n > c["noted"] and (c["at"] is None or now - c["at"] >= NEW_COUNTER_NOTE_S):
                self.events.add("counter", "FlowLocal의 새 값 '%s'이(가) 늘었어요 (%s %d→%d, 이 모니터가 아직 모르는 값)" % (
                    key, key, c["noted"], n))
                c["noted"], c["at"] = n, now
        ch, chm = st.get("ch"), st.get("chm")
        if ch is not None:
            key = (ch, chm)
            if self.chan_key is None:
                text = "FlowLocal 핫스팟 채널: %d번 (%s)" % (ch, CHANNEL_MODE_WORDS.get(chm, "정하는 방식 모름"))
                if st.get("cs"):
                    text += ", 켤 때 점수(낮을수록 조용): " + scores_text(st["cs"]) + " (cs=%s)" % ",".join(
                        "%d:%d" % (c, s) for c, s in st["cs"])
                self.events.add("channel", text)
            elif key != self.chan_key:
                old_ch, old_m = self.chan_key
                self.events.add("channel", "핫스팟 채널이 바뀌었어요: %d번 (%s) → %d번 (%s)" % (
                    old_ch, CHANNEL_MODE_WORDS.get(old_m, "?"), ch, CHANNEL_MODE_WORDS.get(chm, "?")))
            self.chan_key = key

    def _on_ack(self, ack, now):
        with self.lock:
            self.acks += 1
            if ack["cmd"] is None or ack["ok"] is None:
                self.acks_bad += 1
                self.events.add("button", "작은 보드가 알 수 없는 답을 보냈어요", "warn")
                return
            self.acks_ok_ever = True
            f = self.fix
            if f is not None and ACTIONS[f["action"]]["cmd"] == ack["cmd"]:
                action = f["action"]          # command 13 serves two buttons
            else:
                action = ACTION_BY_CMD[ack["cmd"]]
            label = ACTIONS[action]["label"]
            if self.reconnect.on_ack(ack, now):
                label = "%s · %s" % (RECONNECT_LABEL, label)
            text = ack_text(ack["msg"])
            self.events.add("button", "작은 보드의 답: '%s' %s%s" % (
                label, "받았어요" if ack["ok"] else "못 해요", " - %s" % text if text else ""),
                "info" if ack["ok"] else "warn")
            if ack["cmd"] in (CMD_REBOOT_FLOWLOCAL, CMD_CHANNEL) and ack["ok"]:
                self.fl_restart_ack_at = now  # FlowLocal restarts now (MidiWorker reopens the port)
            if f is None or f["action"] != action:
                return
            if f["phase"] == "sending":
                # FlowLocal answered before the web request heard that the message
                # went out: it did; fix_sent() then finds the press moved on
                f.update(phase="ack", sent_at=now)
            late = f["phase"] == "done" and f["result"] == "no_ack" and now - f["t0"] <= LATE_ACK_S
            if f["phase"] != "ack" and not late:
                return
            f["ack"] = {"ok": ack["ok"], "msg": ack["msg"], "text": text}
            if ack["ok"]:
                if late:
                    self.fix_failed.pop(action, None)
                f.update(phase="recover", result=None, done_at=None, recover_at=now, level="info",
                         message=ACTIONS[action]["wait_text"], next=None, next_button=None)
            else:
                self._refused(f, now, ack["msg"])

    def check_stale(self, now):
        """Called about once a second: notes replies that stopped or never
        came, and times out the button in progress."""
        with self.lock:
            if self.status_ever_port and not self.status_lost_noted and now - self.status_at > STATUS_STALE_S:
                self.status_lost_noted = True
                self.events.add("status", "FlowLocal 상태 응답이 끊겼어요", "warn")
            if (self.out_open and self.in_open and not self.status_ever_port and not self.old_fw_noted
                    and self.out_open_at is not None and now - self.out_open_at > STATUS_STALE_S
                    and self.requests >= 2):
                self.old_fw_noted = True
                if self.known_to_answer():
                    fw = self.memory.fl_fw
                    self.events.add("status", "FlowLocal이 모니터에 대답하지 않아요 (이 노트북에서 새 펌웨어%s로 대답하던 "
                                    "보드예요. 계속 이러면 RST 버튼을 한 번 눌러요)" % ("(%s)" % fw if fw else ""), "warn")
                else:
                    self.events.add("status", "FlowLocal이 상태 요청에 답하지 않아요 - 예전 펌웨어로 보여요 (패널 신호는 계속 보여 줘요)", "warn")
            if self.flap_noted and (not self.fl_restarts or now - self.fl_restarts[-1] > FLAP_WINDOW_S):
                self.flap_noted = False
                if self.port_present:
                    self.events.add("flowlocal", "FlowLocal이 %s째 다시 켜지지 않고 붙어 있어요" % fmt_dur(FLAP_WINDOW_S), "ok")
            self.fix_update(now)

    # ------------------------------------------------ inputs: system

    def on_live(self, running, names, now, exe=None):
        with self.lock:
            if running is not None and running != self.live:
                if self.live is not None or running:
                    self.events.add("live", "음악 프로그램(Ableton Live)이 %s" % ("켜져 있어요" if running else "꺼졌어요"),
                                    "ok" if running else "bad")
                elif self.live is None and not running:
                    self.events.add("live", "음악 프로그램(Ableton Live)이 아직 안 보여요", "warn")
                self.live_changed_at = now
            self.live = running
            if running:
                self.live_seen = True
                self.live_names = list(names or [])
                if exe:
                    self.live_exe = exe

    def keep_live(self, now, cfg):
        """The Live keeper's step, right after each on_live (Supervisor)."""
        with self.lock:
            fresh = self.status_ever_port and self.status_at is not None and now - self.status_at <= STATUS_STALE_S
            rr = self.status["rr"] if fresh and self.status is not None else None
            pressed = self.fix_pressed.get("resync")
            if pressed is not None and now - pressed < LIVE_LAUNCH_WAIT_S + LIVE_LOAD_S:
                rr = None                 # the resync button moves rr too: no proof the set opened
            self.keeper.step(now, self.live, self.live_seen, live_autorestart_on(cfg), live_set_path(cfg),
                             self.live_exe, rr)

    def live_needs_watch(self):
        """Live is gone or being started: look more often."""
        return self.live is False or self.keeper.phase != "idle"

    def on_power(self, ac, battery, has_battery, now):
        """The charger. Pulled out, the laptop runs on - and so does the show
        (the power settings allow for it): yellow and calm, red only when the
        battery runs low."""
        with self.lock:
            low = battery is not None and battery < LOW_BATTERY_PCT
            pct = battery if battery is not None else "?"
            if ac is not None and self.ac is not None and ac != self.ac:
                if ac:
                    self.events.add("power", "노트북 충전기가 연결됐어요", "ok")
                else:
                    self.events.add("power", "노트북 충전기가 빠졌어요 (배터리 %s%%) - 소리는 계속 나요. 충전기를 다시 꽂아 주세요" % pct,
                                    "bad" if low else "warn")
            elif ac is False and self.ac is None:
                self.events.add("power", "노트북이 배터리로 돌고 있어요 (%s%%) - 소리는 계속 나요. 충전기를 꽂아 주세요" % pct,
                                "bad" if low else "warn")
            if ac is False and low:
                if not self.low_battery_noted and self.ac is False:
                    self.events.add("power", "노트북 배터리가 %d%%까지 내려갔어요 - 충전기를 바로 꽂아 주세요" % battery, "bad")
                self.low_battery_noted = True
            else:
                self.low_battery_noted = False
            self.ac, self.battery, self.has_battery = ac, battery, has_battery

    def on_power_settings(self, items, now):
        with self.lock:
            before = {i["key"]: power_item_problem(i, self.has_battery) for i in (self.power_items or [])}
            for i in items or []:
                ac_bad, dc_bad = power_item_problem(i, self.has_battery)
                was_ac, was_dc = before.get(i["key"], (False, False))
                if (ac_bad and not was_ac) or (dc_bad and not was_dc):
                    self.events.add("power", "노트북 설정: %s - 전시에는 '%s'" % (power_problem_text(i, self.has_battery),
                                                                           i["want_text"]), "warn")
            self.power_items = items
            self.power_checked_at = now

    # ------------------------------------------------ fix buttons

    def _last_press(self, action):
        """The last press of this button; for the channel buttons, of either (they share gap_s)."""
        group = ACTIONS[action]["group"]
        keys = [k for k, a in ACTIONS.items() if a["group"] == group] if group == "channel" else [action]
        times = [self.fix_pressed[k] for k in keys if k in self.fix_pressed]
        return max(times) if times else None

    def action_message(self, action):
        """(the message this press sends, the channel it asks for or None), or
        (None, why) when there is none now: [무선 채널 바꾸기] asks for the next
        of 1/6/11 after the channel FlowLocal reports, by its number."""
        with self.lock:
            if action == "channel_next":
                st = self.status if self.status_ever_port else None
                target = next_channel(st["ch"] if st is not None else None)
                if target is None:
                    return None, "지금 무선 채널을 몰라요"
                return build_channel_command(target), target
            return ACTIONS[action]["msg"], None

    def fix_begin(self, action, now, msg=None, target=None):
        """A button was pressed. None when it may be sent, else (code, message)."""
        a = ACTIONS[action]
        with self.lock:
            self.fix_update(now)
            if self.reconnect.running():
                return "busy", "지금 '%s'이 진행 중이에요. 끝날 때까지 기다려 주세요." % RECONNECT_LABEL
            f = self.fix
            if f is not None and f["phase"] != "done":
                return "busy", "지금 '%s' 버튼의 결과를 기다리는 중이에요. 끝날 때까지 기다려 주세요." % ACTIONS[f["action"]]["label"]
            last = self._last_press(action)
            if last is not None and now - last < a["gap_s"]:
                return "too_soon", "방금 눌렀어요. %d초 뒤에 다시 누를 수 있어요." % max(1, math.ceil(a["gap_s"] - (now - last)))
            if msg is None:
                msg, target = self.action_message(action)
                if msg is None:
                    return "unknown", "보내지 않았어요: %s." % target
            self.fix_pressed[action] = now
            if action in RESTART_ACTIONS:
                self.restart_asked_at = now
            self.fix_seq += 1
            st = self.status if self.status_ever_port else None
            self.fix = {
                "seq": self.fix_seq, "action": action, "t0": now, "sent_at": None, "phase": "sending",
                "result": None, "level": "info", "message": "보내는 중…", "next": None, "next_button": None,
                "ack": None, "recover_at": None, "done_at": None, "saw_down": False, "port_lost": False,
                "base_pc": self.pc_count,
                "base_ps": st["ps"] if st is not None else None,
                "base_se": st["se"] if st is not None else None,
                "base_ch": st["ch"] if st is not None else None,
                "msg": list(msg), "target": target,
            }
            what = " → %d번 채널" % target if target else ""
            self.events.add("button", "'%s' 버튼을 눌렀어요%s (%s)" % (a["label"], what, hexbytes(msg)))
            return None

    def fix_sent(self, ok, now):
        with self.lock:
            f = self.fix
            if f is None or f["phase"] != "sending":
                return
            if not ok:
                self._done(f, now, "send_failed", "warn",
                           "보내지 못했어요. 작은 보드가 노트북에 보이는지 확인하고 다시 눌러 주세요.")
                return
            f["sent_at"] = now
            f["phase"] = "answer" if ACTIONS[f["action"]]["cmd"] is None else "ack"
            f["message"] = "작은 보드의 답을 기다리는 중…"
            self.fix_update(now)

    def _done(self, f, at, result, level, message, next_step=None, next_button=None, failed=None, needs_fw=False):
        """Ends the press. `failed`: it showed that this button does not help
        now, so the next steps skip it for FIX_MEMORY_S (default: failed,
        no_ack, no_answer). A refusal for a passing reason (too soon, busy,
        starting) or a message that never went out is not that."""
        f.update(phase="done", result=result, level=level, message=message, next=next_step,
                 next_button=next_button, done_at=at)
        label = ACTIONS[f["action"]]["label"]
        if failed is None:
            failed = result in ("failed", "no_ack", "no_answer")
        if result in ("ok", "answered"):
            if f["action"] != "resync":
                self.fix_failed.clear()
        elif failed:
            self.fix_failed[f["action"]] = (at, result, needs_fw)
        self.events.add("button", "'%s': %s%s" % (label, message, (" 다음: " + next_step) if next_step else ""), level)

    def _refused(self, f, now, why):
        """FlowLocal answered ok=0: say why in Korean and what to do next."""
        action = f["action"]
        ko, kind = ack_reason(why)
        text = "작은 보드가 못 한다고 답했어요: %s." % (ko or why or "이유 모름")
        nxt, button = ACTIONS[action]["manual"], None
        if kind == "too_soon":
            nxt = "10초 뒤에 다시 눌러 주세요."
        elif kind == "busy":
            nxt = "30초 기다린 뒤 이 화면을 다시 보세요."
        elif kind == "starting":
            nxt = "작은 보드가 켜지는 중이에요. 30초 기다린 뒤 이 화면을 다시 보세요."
        elif kind == "no_session":
            nxt, button = STEP_REBOOT_PANEL_IF_ON, "reboot_panel"
        elif kind == "ambiguous":
            nxt = ("PatternflowLocal에 붙은 폰·노트북의 Wi-Fi를 꺼요(점검용 폰 포함). "
                   "패널만 남으면 이 버튼이 다시 돼요.")
        elif kind == "absent":
            nxt = "패널에 그림이 보이는지(전원이 켜져 있는지) 봐요. " + STEP_PANEL_POWER_CYCLE
        elif kind == "unreachable":
            nxt = STEP_PANEL_POWER_CYCLE
        elif kind == "bad_channel":
            nxt, button = "[채널 자동으로]를 누르거나 담당자에게 알려 주세요.", "channel_auto"
        elif ACTIONS[action]["group"] == "channel":
            nxt = "30초 뒤에 다시 눌러 보세요. 그래도 안 되면 담당자에게 알려 주세요."
        self._done(f, now, "refused", "warn", text, nxt, button,
                   failed=kind in ("no_session", "absent", "unreachable"))

    def fix_update(self, now):
        """Moves the press in progress on: answer / ack / recovery / timeouts.
        A timeout is dated when it fell due, not when it was noticed."""
        with self.lock:
            f = self.fix
            if f is None or f["phase"] in ("done", "sending"):
                return
            a = ACTIONS[f["action"]]
            action = f["action"]
            if f["phase"] == "answer":
                if self.pc_count > f["base_pc"]:
                    n = self.pc
                    self._done(f, now, "answered", "ok",
                               "작은 보드가 패턴 %d번을 음악 프로그램에 다시 보냈어요. 소리가 패널 그림에 맞춰졌을 거예요." % n)
                elif now - f["sent_at"] > RESYNC_WAIT_S:
                    self._done(f, f["sent_at"] + RESYNC_WAIT_S, "no_answer", "warn",
                               "보냈지만 답이 없어요. 작은 보드가 켜진 뒤 패널이 그림을 바꾼 적이 없으면 답이 없어요.",
                               "그림 바꾸는 노브를 딸깍 3번 돌려 보세요.")
                return
            if f["phase"] == "ack":
                if action in RESTART_ACTIONS and f["port_lost"]:
                    # the ack got lost, but the board went away: it is restarting
                    f.update(phase="recover", recover_at=now, message=a["wait_text"])
                elif now - f["sent_at"] > a["ack_s"]:
                    due = f["sent_at"] + a["ack_s"]
                    answering = (self.status_ever_port and self.status_at is not None
                                 and now - self.status_at <= STATUS_STALE_S)
                    if not self.known_to_answer():
                        # FlowLocal never answered anything on this laptop: firmware from before the protocol
                        self._done(f, due, "no_ack", "warn",
                                   "작은 보드가 답하지 않았어요. 이 버튼은 새 FlowLocal 펌웨어가 필요해요.", a["manual"],
                                   needs_fw=True)
                    elif answering and a["group"] == "channel":
                        # it answers the status request but not command 13: a firmware before flx-mon3
                        self._done(f, due, "no_ack", "warn",
                                   "작은 보드가 채널 명령에 답하지 않았어요. 예전 FlowLocal 펌웨어(flx-mon3 전)는 이 명령을 "
                                   "몰라요. 아무것도 바뀌지 않았어요.", a["manual"], needs_fw=True)
                    elif answering:
                        # it still answers the status request: this ack went missing
                        self._done(f, due, "no_ack", "warn",
                                   "작은 보드가 이 버튼에 답하지 않았어요. 상태 답은 오고 있어서, 답이 오다가 사라졌을 수 있어요.",
                                   "30초 기다린 뒤에도 이 화면이 그대로면 한 번 더 눌러 보세요.", failed=False)
                    else:
                        self._done(f, due, "no_ack", "warn",
                                   "작은 보드가 답하지 않았어요. 작은 보드가 멈췄을 수 있어요.", STEP_RST_SHORT)
                    return
                else:
                    return
            # recover
            st = self.status if self.status_ever_port else None
            fresh = (st is not None and self.status_at is not None and self.status_at > f["recover_at"]
                     and now - self.status_at <= STATUS_STALE_S)
            if fresh and st["ps"] == 0:
                f["saw_down"] = True
            if fresh and st["ps"]:
                if action in RESTART_ACTIONS:
                    back = st["up"] is not None and self.status_at - st["up"] > f["t0"] - 1.0
                else:
                    base_se, se = f["base_se"], st["se"]
                    back = (not f["base_ps"] or f["saw_down"]
                            or (se is not None and base_se is not None and se > base_se))
                if back:
                    took = max(1, round(now - f["t0"]))
                    got = st["ch"]
                    if action == "channel_next" and f["target"] and got is not None and got != f["target"]:
                        text = ("작은 보드가 다시 켜지고 패널도 붙었어요 (%d초 걸림). 그런데 채널이 %d번이에요 "
                                "(바꾸려던 채널: %d번)." % (took, got, f["target"]))
                    elif action == "channel_next":
                        text = "무선 채널을 %s번으로 바꿨어요. 작은 보드가 다시 켜지고 패널도 붙었어요 (%d초 걸림)." % (
                            got if got is not None else f["target"], took)
                    elif action == "channel_auto":
                        text = "채널을 자동으로 되돌렸어요 (지금 %s번%s). 작은 보드가 다시 켜지고 패널도 붙었어요 (%d초 걸림)." % (
                            got if got is not None else "?", "" if st["chm"] in (None, "auto") else ", 방식: %s" % st["chm"],
                            took)
                    else:
                        text = {"reconnect": "패널이 다시 연결됐어요 (%d초 걸림).",
                                "reboot_panel": "패널이 다시 켜지고 연결됐어요 (%d초 걸림).",
                                "reboot_flowlocal": "작은 보드가 다시 켜지고 패널도 붙었어요 (%d초 걸림)."}[action] % took
                    self._done(f, now, "ok", "ok", text)
                    return
            if now - f["recover_at"] > a["recover_s"]:
                due = f["recover_at"] + a["recover_s"]
                wait = fmt_dur(a["recover_s"])
                if action == "reconnect":
                    self._done(f, due, "failed", "bad", "%s 안에 다시 붙지 않았어요." % wait,
                               STEP_REBOOT_PANEL_IF_ON, "reboot_panel")
                elif action == "reboot_panel":
                    self._done(f, due, "failed", "bad", "%s 안에 다시 붙지 않았어요." % wait, STEP_PANEL_POWER_CYCLE)
                elif not self.status_ever_port:
                    self._done(f, due, "failed", "bad", "작은 보드가 %s 안에 다시 나타나지 않았어요." % wait, STEP_RST_SHORT)
                else:
                    self._done(f, due, "failed", "bad", "작은 보드는 다시 켜졌지만 패널이 %s 안에 붙지 않았어요." % wait,
                               STEP_REBOOT_PANEL_IF_ON, "reboot_panel")

    def _fix_view(self, now):
        f = self.fix
        if f is None:
            return None
        if f["done_at"] is not None and now - f["done_at"] > FIX_SHOW_S:
            return None
        a = ACTIONS[f["action"]]
        label = a["label"]
        if f.get("target"):
            label += " (%s → %d번)" % ("%s번" % f["base_ch"] if f.get("base_ch") else "?", f["target"])
        return {
            "seq": f["seq"], "action": f["action"], "label": label, "phase": f["phase"],
            "result": f["result"], "level": f["level"], "message": f["message"],
            "next": f["next"], "next_button": f["next_button"],
            "elapsed": round(now - f["t0"], 1),
            "recover_elapsed": None if f["recover_at"] is None else round(now - f["recover_at"], 1),
            "recover_s": a["recover_s"],
            "done_age": None if f["done_at"] is None else round(now - f["done_at"], 1),
            "ack": dict(f["ack"]) if f["ack"] else None,
        }

    # ------------------------------------------------ the one big button (Reconnector)
    # The run's decisions are made under this lock; what it sends goes out
    # through the MIDI thread after the lock is let go (ReconnectRunner), and
    # the outcome comes back through reconnect_result().

    def reconnect_start(self, now):
        with self.lock:
            self.fix_update(now)
            f = self.fix
            if f is not None and f["phase"] != "done":
                return False, "지금 '%s' 버튼의 결과를 기다리는 중이에요" % ACTIONS[f["action"]]["label"]
            return self.reconnect.start(now)

    def reconnect_step(self, now, cfg=None):
        """The run's next move: a list of effects to carry out, ("send", key, msg) / ("launch",)."""
        with self.lock:
            return self.reconnect.step(now, cfg or {})

    def reconnect_result(self, key, ok, now):
        with self.lock:
            self.reconnect.result(key, ok, now)

    def reconnect_cancel(self, now):
        with self.lock:
            return self.reconnect.cancel(now)

    def reconnect_running(self):
        with self.lock:
            return self.reconnect.running()

    def reconnect_launch(self, now, cfg):
        """The run's ("launch",): starts Live on the exhibition set through the keeper."""
        with self.lock:
            ok, how, err = self.keeper.start_now(now, live_set_path(cfg), self.live_exe)
            self.reconnect.result("launch", ok, now, err)
            return ok

    # ------------------------------------------------ output

    def _flap_view(self, now):
        """FlowLocal restarting again and again: "now" while FLAP_COUNT
        restarts fall within the last FLAP_WINDOW_S; "recent" for FLAP_NOTE_S
        after the last restart of such a run."""
        times = [t for t in self.fl_restarts if now - FLAP_WINDOW_S - FLAP_NOTE_S <= t <= now + 0.05]
        in_window = sum(1 for t in times if now - t <= FLAP_WINDOW_S)
        run_end = None
        for j in range(FLAP_COUNT - 1, len(times)):
            if times[j] - times[j - FLAP_COUNT + 1] <= FLAP_WINDOW_S:
                run_end = times[j]
        flapping = in_window >= FLAP_COUNT
        return {
            "now": flapping,
            "recent": not flapping and run_end is not None and now - run_end <= FLAP_NOTE_S,
            "count_window": in_window,
            "count": len(times),
            "last_age": round(now - times[-1], 1) if times else None,
            "run_end_age": None if run_end is None else round(now - run_end, 1),
        }

    def _channel_view(self):
        """The hotspot's channel for the operator section and the channel buttons."""
        st = self.status if self.status_ever_port else None
        if st is None:
            return {"ch": None, "chm": None, "chm_word": None, "cs": None, "cs_text": None, "next": None,
                    "support": None, "fw": None}
        return {"ch": st["ch"], "chm": st["chm"], "chm_word": CHANNEL_MODE_WORDS.get(st["chm"]),
                "cs": st["cs"], "cs_text": scores_text(st["cs"]), "next": next_channel(st["ch"]),
                "support": channel_command_support(st, self.status_raw), "fw": st["fw"]}

    def snapshot(self, now=None):
        now = self.clock() if now is None else now

        def age(t):
            return None if t is None else round(max(0.0, now - t), 1)

        with self.lock:
            self.fix_update(now)
            while self.recent and now - self.recent[0] > 60.0:
                self.recent.popleft()
            st = self.status
            fl_up = None
            if st is not None and st["up"] is not None and self.status_at is not None and self.status_ever_port:
                fl_up = round(st["up"] + (now - self.status_at), 1)
            fresh_st = st if self.status_ever_port else None
            pattern, pattern_age, pattern_src = self.pc, age(self.pc_at), "midi" if self.pc is not None else None
            if pattern is None and fresh_st is not None and fresh_st["lp"] is not None:
                pattern, pattern_src = fresh_st["lp"], "status"
            knobs, knob_ages, knob_src = [], [], []
            for i in range(3):
                if self.knobs[i] is not None:
                    knobs.append(self.knobs[i]); knob_ages.append(age(self.knob_at[i])); knob_src.append("midi")
                elif fresh_st is not None and fresh_st["kn"] and fresh_st["kn"][i] is not None:
                    knobs.append(fresh_st["kn"][i]); knob_ages.append(None); knob_src.append("status")
                else:
                    knobs.append(None); knob_ages.append(None); knob_src.append(None)
            # what happens while a session settles (WIFI_SETTLE_S) is not held against the wireless
            settle_end = None
            if self.session_up_at is not None:
                settle_end = self.session_up_at + WIFI_SETTLE_S
            elif not self.status_ever_port and self.in_open_at is not None:
                settle_end = self.in_open_at + WIFI_SETTLE_S      # no status (old firmware): from the port's open
            gp_min = None
            if self.gp_hist and self.status_ever_port:
                window = [g for t, g in self.gp_hist if now - t <= 60.0 and (settle_end is None or t >= settle_end)]
                if window:
                    gp_min = window[-1] - window[0]
            cut = now - TIMELINE_S
            points = [[round(now - e[0], 3), e[1], 1 if e[2] else 0] for e in self.arrivals if cut <= e[0] <= now + 0.05]
            lb = self.bunches.last_bunch()
            seen_60 = self.bunches.count(now)
            counted_60 = self.bunches.count(now, since=settle_end)
            memory = self.memory.view()
            memory["seen"] = self.known_to_answer()
            if memory["seen"] and memory["source"] is None:
                memory["source"] = "run"
            sr = self.self_restart
            return {
                "monitor_up_s": round(now - self.started, 1),
                "midi_lib": self.midi_lib,
                "port": {
                    "checked": self.port_checked,
                    "present": self.port_present,
                    "changed_age": age(self.port_changed_at),
                    "in_open": self.in_open,
                    "out_open": self.out_open,
                    "in_open_age": age(self.in_open_at),
                    "out_open_age": age(self.out_open_at),
                    "in_name": self.in_name,
                    "out_name": self.out_name,
                    "matches": self.matches,
                    "error": self.open_error,
                },
                "requests": self.requests,
                "replies": self.replies,
                "bad_replies": self.bad_replies,
                "status": st,
                "status_raw": self.status_raw,
                "status_age": age(self.status_at) if self.status_ever_port else None,
                "status_last_age": age(self.status_at),
                "status_ever": self.status_ever_port,
                "fl_up_s": fl_up,
                "fl_reboot_age": age(self.fl_reboot_at),
                "fl_memory": memory,
                "fl_restart": None if sr is None else {
                    "rsn": sr["rsn"], "word": sr["word"], "kind": sr["kind"], "bc": sr["bc"], "why": sr["why"],
                    "age": age(sr["boot_at"])},
                "flap": self._flap_view(now),
                "session": {
                    "up": None if self.prev_ps is None else self.prev_ps > 0,
                    "changed_age": age(self.session_changed_at),
                    "down_age": age(self.session_down_at),
                    "up_age": age(self.session_up_at),
                    "stations_age": age(self.stations_since),
                    "drop_silent": self.drop_silent,
                },
                "wireless": {"gp_min": gp_min,
                             "settle_left": None if settle_end is None or now >= settle_end else round(settle_end - now, 1)},
                "channel": self._channel_view(),
                "connects": [{"age": age(t), "s": s, "slow": slow} for t, s, slow in self.connects],
                "reconnect": self.reconnect.view(now),
                "midi": {
                    "last_age": age(self.midi_at),
                    "count": self.midi_count,
                    "since_open": self.midi_since_open,
                    "per_min": len(self.recent),
                    "last_text": self.last_text,
                    "pattern": pattern,
                    "pattern_name": pattern_name(pattern),
                    "pattern_age": pattern_age if pattern_src == "midi" else None,
                    "pattern_src": pattern_src,
                    "knobs": knobs,
                    "knob_ages": knob_ages,
                    "knob_src": knob_src,
                    "notes": self.note_count,
                },
                "arrivals": {
                    "window_s": TIMELINE_S,
                    "points": points,
                    "bunches_60s": counted_60,            # what counts for WIFI_POOR (after the settle time)
                    "bunches_seen_60s": seen_60,          # ... and those while a session settled
                    "bunches_total": self.bunches.total,
                    "last_bunch": None if lb is None else {
                        "age": age(lb[0]), "size": lb[1], "gap_ms": None if lb[2] is None else int(round(lb[2] * 1000)),
                        "knobs": "·".join("K" + k for k in lb[3])},
                    "source": self.arrival_source,
                },
                "live": {
                    "running": self.live,
                    "names": list(self.live_names),
                    "seen": self.live_seen,
                    "changed_age": age(self.live_changed_at),
                    "exe": self.live_exe,
                },
                "live_keeper": self.keeper.view(now),
                "power": {"ac": self.ac, "battery": self.battery, "has_battery": self.has_battery},
                "power_settings": {"checked_age": age(self.power_checked_at),
                                   "items": [dict(i) for i in (self.power_items or [])]},
                "fix": self._fix_view(now),
                "fix_pressed": {k: age(t) for k, t in self.fix_pressed.items()},
                "fix_failed": {k: {"age": age(t), "result": r, "needs_fw": fw} for k, (t, r, fw) in self.fix_failed.items()
                               if now - t <= FIX_MEMORY_S},
                "acks": {"count": self.acks, "bad": self.acks_bad, "ok_ever": self.acks_ok_ever},
            }


# ================================================================ the one big button

# [한 번에 다시 연결하기]: its timeouts, from what was measured. FlowLocal's
# restart: USB back in ~1.6-1.9 s, the hotspot ~13.3 s after the start, the
# panel on it ~1-2 s later, its session ~0.5-1 s after that. The panel's
# restart: on the hotspot again in ~5-7 s, its session ~1-1.5 s later.
RC_PORT_GRACE_S = 6.0         # the port left less than this ago: a restart may bring it back by itself
RC_OPEN_S = 10.0              # the port is listed but the monitor cannot open it: give up (PORT_BUSY)
RC_SELF_REPAIR_S = 25.0       # FlowLocal silent: flx-mon2 repairs its USB itself (soft reconnect after ~9 s,
                              # a restart after ~21 s) - wait for that before asking for its RST button
RC_FRESH_BOOT_S = 40.0        # FlowLocal started less than this ago: its sessions and channel are new already
RC_ACK_S = 4.0                # command 12's ack or the port leaving, else it is sent once more
RC_BACK_S = 15.0              # after command 12: answering again from a new start
RC_AP_S = 30.0                # ... and its hotspot up
RC_JOIN_S = 25.0              # the panel on the hotspot after it came up
RC_SESSION_S = 10.0           # on the hotspot without a session this long: restart the panel (command 11)
RC_PANEL_ACK_S = 8.0          # command 11's ack (FlowLocal's HTTP call to the panel takes up to 4 s)
RC_PANEL_REBOOT_S = 30.0      # after the panel's restart: its session
RC_HUMAN_JOIN_S = 40.0        # after a person switched the panel off and on: on the hotspot but no session
RC_LIVE_S = 90.0              # Live started (by this run or the keeper): its set opened (FlowLocal's rr rises)
RC_RESYNC_S = 1.5             # the resync's answer (FlowLocal answers in ~10 ms)
RC_VERIFY_S = 12.0            # the last check may take this long to hold (an AppleMIDI sync every 10 s)
RC_HD_FRESH_MS = 15000        # ... the panel heard this recently
RC_HUMAN_S = 300.0            # a person's step is waited for this long
RC_SHOW_S = 600.0             # a finished run stays on the page this long

RC_STEPS = (
    ("usb", "작은 보드(FlowLocal)가 대답하는지 확인"),
    ("restart", "작은 보드 다시 켜기 (핫스팟·무선 채널을 새로)"),
    ("panel", "패널 다시 연결"),
    ("live", "음악 프로그램(Ableton Live) 맞추기"),
    ("verify", "마지막 확인"),
)
RC_PHASE_STEP = {"usb": "usb", "restart": "restart", "restart_ack": "restart", "restart_wait": "restart",
                 "ap": "restart", "join": "panel", "session": "panel", "panel_ack": "panel", "panel_wait": "panel",
                 "live": "live", "live_launch": "live", "live_wait": "live", "resync": "live", "verify": "verify"}
# The one physical step a person may be asked for: (short, what to do). The run
# goes on by itself when it sees it done; the same step twice ends the run.
RC_HUMAN = {
    "plug": ("작은 보드의 USB를 꽂아 주세요",
             "작은 보드(FlowLocal)의 USB 케이블을 노트북의 테이프 표시한 자리에 꽂아 주세요. 보드 쪽은 'USB'라고 "
             "적힌 잭이에요('COM' 잭이 아니에요)."),
    "rst": ("작은 보드의 RST 버튼을 한 번 눌러 주세요",
            "작은 보드(FlowLocal)의 RST 버튼을 한 번 눌러 주세요(옆의 BOOT는 누르지 않아요). 안 되면 USB를 뽑았다가 "
            "5초 뒤 같은 테이프 자리에 꽂아 주세요."),
    "replug": ("작은 보드의 USB를 뽑았다 꽂아 주세요",
               "작은 보드(FlowLocal)의 USB를 노트북에서 뽑았다가 5초 뒤 같은 테이프 자리에 꽂아 주세요."),
    "panel_power": ("패널 전원을 껐다 켜 주세요",
                    "패널 전원을 껐다가 10초 뒤 다시 켜 주세요. 켜면 첫 그림부터 시작해요."),
    "phones": ("PatternflowLocal에 붙은 폰의 Wi-Fi를 꺼 주세요",
               "PatternflowLocal Wi-Fi에 붙은 폰·노트북의 Wi-Fi를 꺼 주세요(점검용 폰 포함). 핫스팟에 패널만 "
               "남아야 해요."),
    "live": ("음악 프로그램(Ableton Live)을 켜 주세요",
             "음악 프로그램(Ableton Live)을 켜 주세요(바탕화면이나 시작 메뉴). 전시 세트가 열릴 때까지 기다려요."),
}
RC_SEND_WORDS = {"cmd12": "작은 보드 다시 켜기", "cmd11": "패널 다시 켜기", "resync": "소리 다시 맞추기"}
RC_ACK_KEYS = {CMD_REBOOT_FLOWLOCAL: "cmd12", CMD_REBOOT_PANEL: "cmd11"}
RC_DONE_TEXT = "다 됐어요 — 패널 노브를 돌려 막대가 움직이는지 보세요"
RECONNECT_WHAT = ("작은 보드(FlowLocal)가 대답하는지 보고 → 작은 보드를 다시 켜 핫스팟과 무선 채널을 새로 하고 → "
                  "패널이 다시 붙기를 기다려요(안 붙으면 패널도 다시 켜요) → 음악 프로그램을 맞추고 → 마지막으로 확인해요.")
RECONNECT_CONFIRM = ("작은 보드를 다시 켜고 패널이 다시 붙을 때까지 30초쯤 걸려요. 음악은 계속 나오지만 그동안 소리가 "
                     "패널을 따라가지 않아요. 사람이 할 일이 생기면 화면이 한 가지만 알려 주고, 하고 나면 알아서 이어서 해요.")


def _secs(s):
    return "%.1f초" % max(0.0, s)


class Reconnector(object):
    """[한 번에 다시 연결하기]: one press brings the chain up in order.

      1 usb      FlowLocal answers? The port missing: ask for the cable. The
                 port there but silent: wait RC_SELF_REPAIR_S (flx-mon2 repairs
                 its USB), then ask for its RST button.
      2 restart  command 12 (every panel session ends, the hotspot and its
                 channel start new) unless FlowLocal started less than
                 RC_FRESH_BOOT_S ago; its new start, then ap=1.
      3 panel    cl >= 1 and ps >= 1. On the hotspot without a session for
                 RC_SESSION_S: command 11 (restart the panel). Two devices:
                 ask for phones off. Never on the hotspot: ask for the panel's
                 power to be switched off and on.
      4 live     Live running: the resync (BF 77 7F). Not running: start the
                 exhibition set (LiveKeeper.start_now; the switch off: ask a
                 person) and wait for FlowLocal's rr to rise (the Live device
                 asks for a resync when the set has loaded).
      5 verify   a fresh reply, ps >= 1, the panel heard lately, Live up.

    A person's step (RC_HUMAN) is asked for at most once per kind; the run
    goes on when it sees it done, and ends if the same step would be needed
    again or nobody does it in RC_HUMAN_S. It is one button press: what it
    sends (commands 12 and 11, the resync, each at most twice) belongs to
    that press, and nothing is sent once it ended or was cancelled.

    It lives in the Tracker and runs under its lock: step() returns effects
    - ("send", key, msg), ("launch",) - that the caller carries out outside
    the lock and reports through result(); acks arrive through on_ack()."""

    def __init__(self, tracker):
        self.tr = tracker
        self.run = None
        self.seq = 0
        self._effects = []
        self._cfg = {}

    # ---------------------------------------------------------------- control

    def running(self):
        return self.run is not None and self.run["result"] is None

    def start(self, now):
        """(True, None), or (False, why) when it cannot start now."""
        if self.running():
            return False, "이미 하는 중이에요. 끝날 때까지 기다려 주세요"
        if not self.tr.midi_lib:
            return False, "모니터가 MIDI를 쓸 수 없어요"
        self.seq += 1
        self.run = {
            "seq": self.seq, "t0": now, "phase": None, "phase_t0": now, "step": None,
            "steps": collections.OrderedDict((k, {"id": k, "label": label, "state": "wait", "t0": None, "t1": None,
                                                  "detail": ""}) for k, label in RC_STEPS),
            "result": None, "message": None, "action": None, "done_at": None,
            "human": None, "asked": [],
            "sent": {}, "sent_at": {}, "tries": {}, "acks": {},
            "boot_at": None, "ap_at": None, "join_at": None, "human_join_at": None,
            "panel_rebooted": False, "rr_base": None, "pc_base": 0, "launch": None, "launch_err": None,
            "live_note": None,
        }
        self._effects = []
        self.tr.events.add("reconnect", "'%s'을 시작해요" % RECONNECT_LABEL)
        self._phase("usb", now)
        return True, None

    def cancel(self, now):
        if not self.running():
            return False
        self._finish(now, "cancelled", "그만했어요. 하던 단계에서 멈췄어요(이미 보낸 것은 되돌리지 않아요).", None, "warn")
        return True

    def step(self, now, cfg=None):
        """One look; returns the effects to carry out."""
        self._effects = []
        self._cfg = cfg or {}
        for _ in range(16):
            if not self.running():
                break
            if not getattr(self, "_p_" + self.run["phase"])(now):
                break
        return self._effects

    def result(self, key, ok, now, err=None):
        """What became of an effect: a message written (or not), Live started (or not)."""
        if not self.running():
            return
        r = self.run
        if key == "launch":
            r["launch"], r["launch_err"] = bool(ok), err
        elif key in r["sent_at"]:
            r["sent"][key] = bool(ok)

    def on_ack(self, ack, now):
        """FlowLocal's ack to a command this run sent: True when it was one."""
        if not self.running():
            return False
        key = RC_ACK_KEYS.get(ack["cmd"])
        r = self.run
        if key is None or key not in r["sent_at"] or key in r["acks"] or now < r["sent_at"][key] - 0.01:
            return False
        r["acks"][key] = {"ok": ack["ok"], "msg": ack["msg"], "at": now}
        return True

    def expects_port_loss(self, now):
        """FlowLocal leaving USB now is this run's doing (its restart, or a
        person's RST, replug or plug)."""
        if not self.running():
            return False
        r = self.run
        h = r["human"]
        return r["phase"] in ("restart_ack", "restart_wait") or (h is not None and h["kind"] in ("rst", "replug", "plug"))

    def expects_boot(self, boot_at):
        """A FlowLocal start at boot_at that this run asked for."""
        if not self.running() or boot_at is None:
            return False
        r = self.run
        t = r["sent_at"].get("cmd12")
        if t is not None and -5.0 <= boot_at - t <= 60.0:
            return True
        h = r["human"]
        return h is not None and h["kind"] in ("rst", "replug", "plug") and boot_at >= h["t0"] - 5.0

    # ---------------------------------------------------------------- readings

    def _fresh(self, now, since=None):
        """FlowLocal's last status if it is fresh (and came after `since`), else None."""
        tr = self.tr
        if not (tr.status_ever_port and tr.status is not None and tr.status_at is not None):
            return None
        if now - tr.status_at > STATUS_STALE_S or (since is not None and tr.status_at < since):
            return None
        return tr.status

    def _boot(self):
        """When the FlowLocal of the last reply started (the monitor's clock)."""
        tr = self.tr
        if not tr.status_ever_port or tr.status is None or tr.status["up"] is None or tr.status_at is None:
            return None
        return tr.status_at - tr.status["up"]

    def _new_boot(self, now, since):
        """A fresh reply from a FlowLocal that started after `since` (the implied
        start is up to 1 s late: up= is whole seconds)."""
        if self._fresh(now, since=since) is None:
            return False
        boot = self._boot()
        if boot is None or boot < since - 1.0:
            return False
        self.run["boot_at"] = boot
        return True

    # ---------------------------------------------------------------- bookkeeping

    def _phase(self, name, now):
        r = self.run
        r["phase"], r["phase_t0"] = name, now
        step = RC_PHASE_STEP[name]
        order = list(r["steps"])
        if r["step"] is not None and order.index(step) < order.index(r["step"]):
            for k in order[order.index(step):]:        # back to an earlier step: it runs again
                r["steps"][k].update(state="wait", t0=None, t1=None, detail="")
        s = r["steps"][step]
        if s["state"] in ("wait", "ok", "skip"):
            s.update(state="run", t0=now if s["state"] == "wait" else s["t0"], t1=None)
        r["step"] = step
        return True

    def _ok(self, step, now, detail, state="ok"):
        r = self.run
        s = r["steps"][step]
        if s["t0"] is None:
            s["t0"] = now
        s.update(state=state, t1=now, detail=detail)
        n = list(r["steps"]).index(step) + 1
        self.tr.events.add("reconnect", "%s %d/%d %s: %s — %s (%s)" % (
            RECONNECT_LABEL, n, len(RC_STEPS), s["label"], "됐어요" if state == "ok" else "건너뜀", detail,
            _secs(now - s["t0"])), "ok" if state == "ok" else "info")

    def _send(self, key, msg, now):
        r = self.run
        r["tries"][key] = r["tries"].get(key, 0) + 1
        r["sent"].pop(key, None)
        r["acks"].pop(key, None)
        r["sent_at"][key] = now
        self._effects.append(("send", key, list(msg)))
        self.tr.events.add("reconnect", "%s: '%s' 보내요 (%s%s)" % (
            RECONNECT_LABEL, RC_SEND_WORDS[key], hexbytes(msg),
            "" if r["tries"][key] == 1 else ", %d번째" % r["tries"][key]))

    def _ask(self, kind, now, resume):
        """Stop for a person's step; a second time for the same kind ends the run."""
        r = self.run
        short, text = RC_HUMAN[kind]
        if kind in r["asked"]:
            return self._fail(now, "%s — 해 보았지만 이어지지 않았어요." % short,
                              "담당자에게 연락해 주세요%s." % _contact_suffix(self._cfg))
        r["asked"].append(kind)
        st = self._fresh(now)
        # the panel's session as it was: after the step only a new one counts (a silent session stays "up")
        r["human"] = {"kind": kind, "short": short, "text": text, "t0": now, "resume": resume,
                      "ps0": st["ps"] if st is not None else 0, "se0": st["se"] if st is not None else None,
                      "saw_down": False}
        r["human_join_at"] = None
        r["phase"], r["phase_t0"] = "human", now
        if kind in ("rst", "replug", "plug"):
            self.tr.restart_asked_at = now            # the start that follows is expected
        n = list(r["steps"]).index(r["step"]) + 1 if r["step"] else 0
        self.tr.events.add("reconnect", "%s %d/%d: 사람이 할 일 — %s" % (RECONNECT_LABEL, n, len(RC_STEPS), text), "warn")
        return True

    def _resumed(self, now, what):
        r = self.run
        h = r["human"]
        r["human"] = None
        self.tr.events.add("reconnect", "%s: %s (%s 뒤) — 이어서 해요" % (RECONNECT_LABEL, what, _secs(now - h["t0"])), "ok")

    def _fail(self, now, message, action):
        r = self.run
        if r["step"] is not None:
            s = r["steps"][r["step"]]
            s.update(state="fail", t1=now, detail=s["detail"] or message)
        self._finish(now, "failed", message, action, "bad")
        return False

    def _finish(self, now, result, message, action, level):
        r = self.run
        r.update(result=result, message=message, action=action, done_at=now, human=None)
        self.tr.events.add("reconnect", "%s: %s%s (모두 %s)" % (
            RECONNECT_LABEL, message, " 할 일: " + action if action else "", _secs(now - r["t0"])), level)

    # ---------------------------------------------------------------- 1 usb

    def _p_usb(self, now):
        tr, r = self.tr, self.run
        if not tr.port_present:
            if tr.port_checked and now - tr.port_changed_at < RC_PORT_GRACE_S:
                return False                          # it just left: a restart may bring it back
            return self._ask("plug", now, "port")
        if not (tr.in_open and tr.out_open):
            if now - r["phase_t0"] < RC_OPEN_S:
                return False                          # the monitor tries again every 3 s
            return self._fail(now, "모니터가 작은 보드와 이야기하지 못해요 (포트를 열 수 없어요).",
                              "노트북을 다시 시작해 주세요 (시작 → 전원 → 다시 시작). 음악 프로그램과 이 화면이 "
                              "저절로 다시 열려요.")
        st = self._fresh(now)
        if st is not None:
            self._ok("usb", now, "대답해요 (펌웨어 %s)" % (st["fw"] or "?"))
            return self._phase("restart", now)
        if now - r["phase_t0"] < RC_SELF_REPAIR_S:
            return False                              # flx-mon2 repairs its own USB meanwhile
        return self._ask("rst", now, "rst")

    # ---------------------------------------------------------------- 2 restart

    def _p_restart(self, now):
        r = self.run
        boot = self._boot()
        if self._fresh(now) is not None and boot is not None and now - boot < RC_FRESH_BOOT_S:
            r["boot_at"] = boot
            r["steps"]["restart"]["detail"] = "방금 켜졌어요 (%d초 전) — 다시 켜지 않아요" % round(now - boot)
            return self._phase("ap", now)
        return self._restart_send(now)

    def _restart_send(self, now):
        self._send("cmd12", build_command(CMD_REBOOT_FLOWLOCAL), now)
        self.tr.restart_asked_at = now
        return self._phase("restart_ack", now)

    def _p_restart_ack(self, now):
        tr, r = self.tr, self.run
        t = r["sent_at"]["cmd12"]
        if self._new_boot(now, t):
            return self._phase("ap", now)             # back already (its ack may have got lost)
        ack = r["acks"].get("cmd12")
        if (ack is not None and ack["ok"]) or tr.port_changed_at >= t:
            return self._phase("restart_wait", now)   # restarting: it said so, or it left USB
        if ack is not None:                           # refused
            if ack_reason(ack["msg"])[1] in ("too_soon", "busy") and r["tries"]["cmd12"] < 2:
                return self._restart_send(now) if now - ack["at"] >= 3.5 else False
            return self._ask("rst", now, "rst")
        if r["sent"].get("cmd12") is False:          # not written (the port went?)
            if r["tries"]["cmd12"] < 2:
                return self._restart_send(now) if now - t >= 1.0 else False
            return self._ask("rst", now, "rst")
        if now - t < RC_ACK_S:
            return False
        if self._fresh(now, since=t + 0.5) is not None:
            # it answered the status request after the command, from the same start: it did not take it
            return self._restart_send(now) if r["tries"]["cmd12"] < 2 else self._ask("rst", now, "rst")
        return self._phase("restart_wait", now)       # silent since the command: it may be restarting

    def _p_restart_wait(self, now):
        r = self.run
        t = r["sent_at"]["cmd12"]
        if self._new_boot(now, t):
            return self._phase("ap", now)
        if now - t < RC_BACK_S:
            return False
        return self._ask("rst", now, "rst")

    def _p_ap(self, now):
        r = self.run
        st = self._fresh(now)
        if st is not None and st["ap"]:
            r["ap_at"] = now
            detail = r["steps"]["restart"]["detail"]
            self._ok("restart", now, (detail + " · " if detail else "") + "핫스팟이 켜졌어요" +
                     (" (채널 %d번)" % st["ch"] if st["ch"] else ""))
            return self._phase("join", now)
        if now - r["phase_t0"] < RC_AP_S:
            return False
        return self._ask("replug", now, "replug")

    # ---------------------------------------------------------------- 3 panel

    def _panel_ok(self, now, detail):
        self._ok("panel", now, detail)
        return self._phase("live", now)

    def _p_join(self, now):
        r = self.run
        st = self._fresh(now)
        if st is not None and st["ps"]:
            return self._panel_ok(now, "패널이 연결됐어요")
        if st is not None and st["cl"]:
            r["join_at"] = now
            return self._phase("session", now)
        if now - (r["ap_at"] if r["ap_at"] is not None else r["phase_t0"]) < RC_JOIN_S:
            return False
        return self._ask("panel_power", now, "panel")

    def _p_session(self, now):
        r = self.run
        st = self._fresh(now)
        if st is not None and st["ps"]:
            return self._panel_ok(now, "패널이 연결됐어요 (핫스팟에 붙고 %d초)" % round(now - r["join_at"]))
        if st is not None and st["cl"] == 0:
            return self._phase("join", now)           # it left again
        if now - r["join_at"] < RC_SESSION_S:
            return False
        if st is not None and st["cl"] is not None and st["cl"] >= 2:
            return self._ask("phones", now, "phones")  # FlowLocal cannot tell which one is the panel
        if not r["panel_rebooted"]:
            return self._panel_reboot(now)
        return self._ask("panel_power", now, "panel")

    def _panel_reboot(self, now):
        self.run["panel_rebooted"] = True
        self._send("cmd11", build_command(CMD_REBOOT_PANEL), now)
        return self._phase("panel_ack", now)

    def _p_panel_ack(self, now):
        r = self.run
        if self._fresh(now) is not None and self.tr.status["ps"]:
            return self._panel_ok(now, "패널이 연결됐어요")
        t = r["sent_at"]["cmd11"]
        ack = r["acks"].get("cmd11")
        if ack is not None and ack["ok"]:
            r["steps"]["panel"]["detail"] = "패널을 다시 켰어요"
            return self._phase("panel_wait", now)
        if ack is not None:
            kind = ack_reason(ack["msg"])[1]
            if kind == "ambiguous":
                return self._ask("phones", now, "phones")
            if kind in ("too_soon", "busy", "starting") and r["tries"]["cmd11"] < 2:
                if now - ack["at"] < 3.5:
                    return False
                self._send("cmd11", build_command(CMD_REBOOT_PANEL), now)
                return self._phase("panel_ack", now)
            return self._ask("panel_power", now, "panel")   # it cannot reach the panel
        if r["sent"].get("cmd11") is False or now - t >= RC_PANEL_ACK_S:
            return self._ask("panel_power", now, "panel")
        return False

    def _p_panel_wait(self, now):
        r = self.run
        st = self._fresh(now, since=r["phase_t0"])
        if st is not None and st["ps"]:
            return self._panel_ok(now, "패널을 다시 켰고 %d초 뒤 연결됐어요" % round(now - r["phase_t0"]))
        if now - r["phase_t0"] < RC_PANEL_REBOOT_S:
            return False
        return self._ask("panel_power", now, "panel")

    # ---------------------------------------------------------------- 4 live

    def _p_live(self, now):
        tr, r = self.tr, self.run
        keeper = tr.keeper
        if tr.live is True:
            if keeper.phase == "loading":
                return self._live_wait(now)           # it just came up: its set is loading
            return self._resync(now)
        if tr.live is None:
            self._ok("live", now, "확인하지 못했어요 (작업 목록을 읽지 못함)", state="skip")
            return self._phase("verify", now)
        if keeper.busy() and live_autorestart_on(self._cfg):
            return self._live_wait(now)               # the keeper is starting it
        if not live_autorestart_on(self._cfg):
            # the operator turned the auto-restart off (to close Live on purpose): a person decides
            return self._ask("live", now, "live")
        r["launch"] = r["launch_err"] = None
        self._effects.append(("launch",))
        return self._phase("live_launch", now)

    def _p_live_launch(self, now):
        r = self.run
        if r["launch"] is None:
            if now - r["phase_t0"] < 5.0:
                return False
            r["launch"], r["launch_err"] = False, "대답 없음"
        if not r["launch"]:
            return self._ask("live", now, "live")
        return self._live_wait(now)

    def _live_wait(self, now):
        st = self._fresh(now)
        self.run["rr_base"] = st["rr"] if st is not None else None
        return self._phase("live_wait", now)

    def _p_live_wait(self, now):
        tr, r = self.tr, self.run
        st = self._fresh(now)
        rr = st["rr"] if st is not None else None
        if rr is not None and (r["rr_base"] is None or rr < r["rr_base"]):
            r["rr_base"] = rr                         # known late, or FlowLocal started again
        elif tr.live and rr is not None and rr > r["rr_base"]:
            self._ok("live", now, "켜지고 전시 세트가 열렸어요")
            return self._phase("verify", now)
        if now - r["phase_t0"] < RC_LIVE_S:
            return False
        if tr.live:
            r["live_note"] = "켜졌지만 세트가 열렸는지는 몰라요"
            return self._resync(now)
        return self._ask("live", now, "live")

    def _resync(self, now):
        tr, r = self.tr, self.run
        tr.fix_pressed["resync"] = now                # the keeper does not take this rr for a set that opened
        r["pc_base"] = tr.pc_count
        self._send("resync", RESYNC_REQUEST, now)
        return self._phase("resync", now)

    def _p_resync(self, now):
        tr, r = self.tr, self.run
        note = r["live_note"] + " · " if r["live_note"] else ""
        if tr.pc_count > r["pc_base"]:
            self._ok("live", now, note + "켜져 있어요 · 소리를 패턴 %s번에 다시 맞췄어요" % tr.pc)
            return self._phase("verify", now)
        if r["sent"].get("resync") is False:
            self._ok("live", now, note + "켜져 있어요 (소리 맞추기는 보내지 못했어요)")
            return self._phase("verify", now)
        if now - r["sent_at"]["resync"] < RC_RESYNC_S:
            return False
        self._ok("live", now, note + "켜져 있어요 · 소리 맞추기를 보냈어요")
        return self._phase("verify", now)

    # ---------------------------------------------------------------- 5 verify

    def _p_verify(self, now):
        tr, r = self.tr, self.run
        st = self._fresh(now)
        problem = None
        if not (tr.port_present and tr.in_open and tr.out_open) or st is None:
            problem = ("작은 보드가 대답하지 않아요", "rst", "rst")
        elif not st["ps"]:
            problem = ("패널 연결이 다시 끊겼어요", "panel_power", "panel")
        elif st["hd"] is None or st["hd"] < 0 or st["hd"] > RC_HD_FRESH_MS:
            problem = ("패널 연결은 있는데 패널 소식이 없어요", "panel_power", "panel")
        elif tr.live is False:
            problem = ("음악 프로그램이 꺼져 있어요", "live", "live")
        if problem is None:
            self._ok("verify", now, "작은 보드 · 패널 · 음악 프로그램이 모두 이어졌어요")
            self._finish(now, "ok", RC_DONE_TEXT, None, "ok")
            return False
        if now - r["phase_t0"] < RC_VERIFY_S:
            return False
        r["steps"]["verify"]["detail"] = problem[0]
        return self._ask(problem[1], now, problem[2])

    # ---------------------------------------------------------------- a person's step

    def _p_human(self, now):
        tr, r = self.tr, self.run
        h = r["human"]
        kind, resume = h["kind"], h["resume"]
        if resume == "port":
            if tr.port_present and tr.in_open and tr.out_open:
                self._resumed(now, "작은 보드가 보여요")
                return self._phase("usb", now)
        elif resume in ("rst", "replug"):
            if self._new_boot(now, h["t0"]):
                self._resumed(now, "작은 보드가 다시 켜졌어요")
                if r["steps"]["usb"]["state"] != "ok":
                    self._ok("usb", now, "대답해요 (다시 켠 뒤)")
                r["steps"]["restart"]["detail"] = "사람이 다시 켰어요 (%s)" % ("RST" if kind == "rst" else "USB 뽑았다 꽂기")
                return self._phase("ap", now)
            if (kind == "rst" and r["steps"]["usb"]["state"] != "ok"
                    and self._fresh(now, since=h["t0"]) is not None):
                # it answers again without a new start: it repaired its own USB
                self._resumed(now, "작은 보드가 다시 대답해요")
                self._ok("usb", now, "다시 대답해요 (스스로 USB를 다시 맺음)")
                return self._phase("restart", now)
        elif resume == "panel":
            st = self._fresh(now, since=h["t0"])
            if st is not None and not st["ps"]:
                h["saw_down"] = True
            if st is not None and st["ps"] and (not h["ps0"] or h["saw_down"] or (
                    h["se0"] is not None and st["se"] is not None and st["se"] > h["se0"])):
                self._resumed(now, "패널이 연결됐어요")
                return self._panel_ok(now, "패널 전원을 껐다 켠 뒤 연결됐어요")
            if st is not None and st["cl"]:
                if r["human_join_at"] is None:
                    r["human_join_at"] = now
                elif now - r["human_join_at"] >= RC_HUMAN_JOIN_S:
                    return self._fail(now, "패널이 핫스팟에는 붙는데 연결을 열지 않아요.",
                                      "담당자에게 연락해 주세요%s. (패널 MIDI 설정 확인이 필요할 수 있어요.)" %
                                      _contact_suffix(self._cfg))
            elif st is not None:
                r["human_join_at"] = None
        elif resume == "phones":
            st = self._fresh(now, since=h["t0"])
            if st is not None and st["ps"]:
                self._resumed(now, "패널이 연결됐어요")
                return self._panel_ok(now, "패널이 연결됐어요")
            if st is not None and st["cl"] == 1:
                self._resumed(now, "핫스팟에 기기가 1대 남았어요")
                r["join_at"] = now
                return self._phase("session", now)
        elif resume == "live":
            if tr.live is True:
                self._resumed(now, "음악 프로그램이 켜졌어요")
                return self._phase("live", now)
        if now - h["t0"] < RC_HUMAN_S:
            return False
        return self._fail(now, "%s — %s 기다렸는데 알아채지 못했어요." % (h["short"], fmt_dur(RC_HUMAN_S)),
                          "다 했으면 [%s]를 다시 눌러 주세요. 그래도 안 되면 담당자에게 연락해 주세요%s." % (
                              RECONNECT_LABEL, _contact_suffix(self._cfg)))

    # ---------------------------------------------------------------- the page

    def view(self, now):
        r = self.run
        if r is None or (r["done_at"] is not None and now - r["done_at"] > RC_SHOW_S):
            return None
        steps = []
        for i, s in enumerate(r["steps"].values()):
            el = None
            if s["t0"] is not None:
                el = (s["t1"] if s["t1"] is not None else now) - s["t0"]
            steps.append({"id": s["id"], "n": i + 1, "label": s["label"], "state": s["state"],
                          "elapsed": None if el is None else round(max(0.0, el), 1), "detail": s["detail"]})
        h, cur = r["human"], r["step"]
        end = r["done_at"] if r["done_at"] is not None else now
        return {
            "seq": r["seq"], "running": r["result"] is None, "result": r["result"], "phase": r["phase"],
            "message": r["message"], "action": r["action"], "elapsed": round(end - r["t0"], 1),
            "done_age": None if r["done_at"] is None else round(now - r["done_at"], 1),
            "step": cur, "step_n": list(r["steps"]).index(cur) + 1 if cur else None,
            "step_label": r["steps"][cur]["label"] if cur else None, "steps_total": len(RC_STEPS),
            "steps": steps,
            "human": None if h is None else {"kind": h["kind"], "short": h["short"], "text": h["text"],
                                             "age": round(now - h["t0"], 1)},
        }


# ================================================================ diagnosis

def _issue(code, level, headline, steps, detail="", button=None, wait=False, op_button=None):
    return {"code": code, "level": level, "headline": headline, "detail": detail, "steps": list(steps),
            "button": button, "wait": wait, "op_button": op_button}


def _contact_suffix(cfg):
    for c in (cfg or {}).get("contacts") or []:
        if not isinstance(c, dict):
            continue
        name = str(c.get("name") or "").strip()
        phone = str(c.get("phone") or "").strip()
        if name or phone:
            role = str(c.get("role") or "").strip()
            return " (%s)" % " ".join(x for x in (role, name, phone) if x)
    return ""


def _cfg_text(cfg, key):
    v = (cfg or {}).get(key)
    return str(v).strip() if v else ""


def _step_call(cfg, why=""):
    return "그래도 안 되면 담당자에게 연락해 주세요%s.%s" % (_contact_suffix(cfg), (" " + why) if why else "")


def _step_replug():
    return ("작은 보드(라벨: FlowLocal)를 노트북의 테이프 표시한 자리에서 뽑았다가, 5초 뒤 같은 자리에 다시 꽂아요. "
            "보드 쪽은 'USB'라고 적힌 잭이에요('COM' 잭이 아니에요).")


STEP_RESTART_LAPTOP = "그래도 그대로면 노트북을 다시 시작해요(시작 → 전원 → 다시 시작). 음악 프로그램과 이 화면이 저절로 다시 열려요."


def _panel_power_step(cfg):
    place = _cfg_text(cfg, "panel_power_place")
    where = "'패널 전원 켜는 곳'(%s)" % place if place else "'패널 전원 켜는 곳'"
    return "패널에 그림이 보이는지(전원이 켜져 있는지) 봐요. 꺼져 있으면 %s에서 켜요." % where


def _btn(action):
    return "[%s]" % ACTIONS[action]["label"]


def staff_power_problems(snap):
    """Power settings that can stop the show: wrong on the charger, or on
    battery (on a laptop that has one - the charger may be pulled out)."""
    has_battery = (snap.get("power") or {}).get("has_battery")
    return [i for i in (snap.get("power_settings") or {}).get("items") or [] if any(power_item_problem(i, has_battery))]


def status_is_fresh(snap):
    age = snap.get("status_age")
    return snap.get("status") is not None and snap.get("status_ever") and age is not None and age <= STATUS_STALE_S


def fl_known_to_answer(snap):
    """FlowLocal answered the PF monitor protocol on this laptop, in this
    monitor run or an earlier one (logs/state.json)."""
    return bool((snap.get("fl_memory") or {}).get("seen")) or bool((snap.get("acks") or {}).get("ok_ever"))


def waiting_for_first_reply(snap):
    """The port is open and asked, and FlowLocal has not answered since it (re)appeared."""
    port = snap["port"]
    return (port["present"] and port["in_open"] and port["out_open"] and not snap["status_ever"]
            and port["out_open_age"] is not None and port["out_open_age"] >= STATUS_STALE_S
            and snap["requests"] >= 2)


def old_firmware_suspected(snap):
    """No answer from a FlowLocal never heard on this laptop: its firmware is
    from before the protocol. One heard before is stuck instead (fl_silence_s)."""
    return waiting_for_first_reply(snap) and not fl_known_to_answer(snap)


def fl_silence_s(snap):
    """How long FlowLocal has answered nothing while its port is open, when
    that says something: since its last reply, or - for a FlowLocal known to
    answer on this laptop that has not answered since its port (re)appeared -
    since the port was opened. None otherwise: fresh replies, a FlowLocal
    never heard here (old firmware), too early to tell."""
    port = snap["port"]
    if not (snap["midi_lib"] and port["present"] and port["in_open"] and port["out_open"]) or status_is_fresh(snap):
        return None
    if snap["status_ever"]:
        return snap["status_age"] or 0.0
    if waiting_for_first_reply(snap) and fl_known_to_answer(snap):
        return port["out_open_age"]
    return None


def fl_stopped(snap):
    """FlowLocal sits on USB but has answered nothing for FL_NO_REPLY_BAD_S: stuck (red)."""
    s = fl_silence_s(snap)
    return s is not None and s >= FL_NO_REPLY_BAD_S


def panel_midi_during_silence(snap, silence):
    """Panel MIDI still reached the laptop after FlowLocal's last answer: its
    USB side still carries the show, only the monitor gets no answer."""
    la = snap["midi"]["last_age"]
    return la is not None and la < silence and la < 60.0


def fl_stopped_detail(snap, silence, full=True):
    """Why a silent FlowLocal is taken for a stuck one (not old firmware)."""
    mem = snap.get("fl_memory") or {}
    if snap["status_ever"]:
        text = "작은 보드가 노트북에는 보이지만 %s째 아무 대답이 없어요." % fmt_dur(silence)
    else:
        text = ("작은 보드가 노트북에는 보이지만, 모니터가 연결한 뒤 %s째 한 번도 대답하지 않아요. "
                "이 노트북에서 새 펌웨어%s로 대답한 적이 있어서%s 예전 펌웨어가 아니라 멈춘 것으로 보여요." % (
                    fmt_dur(silence), "(%s)" % mem["fw"] if mem.get("fw") else "",
                    "(마지막 대답: %s)" % mem["seen_local"] if mem.get("seen_local") else ""))
    if full:
        text += (" USB 쪽이 멈추면 핫스팟은 켜져 있어도 패널 신호가 음악 프로그램에 가지 않아요. "
                 "화면의 버튼도 같은 USB로 가서 멈춘 보드에 닿지 않아요.")
    return text


def buttons_note(snap):
    """A note over the buttons when none of them can help, or None."""
    if fl_stopped(snap):
        return {"level": "bad", "title": "작은 보드가 대답을 멈췄어요.",
                "text": "화면의 버튼은 멈춘 작은 보드에 닿지 않아요. 작은 보드(FlowLocal)의 RST 버튼을 한 번 누르세요"
                        "(BOOT 말고). 안 되면 케이블을 뽑았다가 5초 뒤 같은 테이프 자리에 다시 꽂으세요."}
    return None


def commands_unanswered(snap, group="cmd"):
    """A FlowLocal command of this group ("cmd": 10-12, "channel": 13) went
    unanswered lately while FlowLocal was known not to take it: firmware from
    before the protocol (any command), or before flx-mon3 (the channel). One
    that answered before and then not is stuck or lost an ack (Tracker.fix_update)."""
    return any(v.get("needs_fw") and ACTIONS[k]["group"] == group for k, v in (snap.get("fix_failed") or {}).items())


def reconnect_busy(snap):
    return bool((snap.get("reconnect") or {}).get("running"))


def channel_view(snap):
    return snap.get("channel") or {}


def button_block(snap, action):
    """Why this button cannot do its job right now, or None. Leaves out a
    press in progress and the gap between presses (action_states)."""
    port = snap["port"]
    if not snap["midi_lib"]:
        return "모니터가 MIDI를 쓸 수 없어요"
    if reconnect_busy(snap):
        return "'%s'이 진행 중이에요" % RECONNECT_LABEL
    if not (port["present"] and port["out_open"]):
        return "작은 보드가 노트북에 안 보여요"
    if fl_stopped(snap):
        # every button goes over the USB that stopped answering
        return "작은 보드가 대답을 멈춰서 버튼이 닿지 않아요"
    if ACTIONS[action]["cmd"] is None:
        return None
    if old_firmware_suspected(snap):
        return "새 FlowLocal 펌웨어가 필요해요"
    if ACTIONS[action]["group"] == "channel":
        ch = channel_view(snap)
        if ch.get("support") is False:
            return "새 FlowLocal 펌웨어(flx-mon3)가 필요해요 (지금 %s)" % (ch.get("fw") or "?")
        if not status_is_fresh(snap):
            return "작은 보드의 상태를 기다리는 중이에요"
        if action == "channel_next" and ch.get("next") is None:
            return "지금 무선 채널을 몰라요 (핫스팟이 켜지는 중일 수 있어요)"
        if action == "channel_auto" and ch.get("chm") == "auto":
            return "이미 자동이에요"
        return None
    if action in ("reconnect", "reboot_panel") and status_is_fresh(snap):
        st = snap["status"]
        if st["ap"] is False:
            return "핫스팟이 아직 꺼져 있어요"
        if st["ps"] == 0:
            if action == "reconnect":
                return "지금은 끊을 패널 연결이 없어요"
            if st["cl"] == 0:
                return "패널이 핫스팟에 없어서 닿지 않아요"
            if st["cl"] is not None and st["cl"] >= 2:
                return "핫스팟 기기가 %d대라 어느 것이 패널인지 몰라요" % st["cl"]
    return None


def button_usable(snap, action):
    """Can this button do its job right now? A FlowLocal command that went
    unanswered lately is not offered again for FIX_MEMORY_S."""
    if button_block(snap, action) is not None:
        return False
    return ACTIONS[action]["cmd"] is None or not commands_unanswered(snap, ACTIONS[action]["group"])


def reconnect_state(snap):
    """The one big button as the page shows it: enabled, or why not."""
    fix = snap.get("fix")
    reason = None
    if reconnect_busy(snap):
        reason = "진행 중이에요"
    elif not snap["midi_lib"]:
        reason = "모니터가 MIDI를 쓸 수 없어요"
    elif fix is not None and fix["phase"] != "done":
        reason = "앞에 누른 버튼('%s')을 기다리는 중이에요" % fix["label"]
    elif old_firmware_suspected(snap):
        reason = "작은 보드의 펌웨어가 예전 것이라 이 버튼이 일을 못 해요 (담당자에게 'FlowLocal 펌웨어 업데이트')"
    return {"id": RECONNECT_ID, "label": RECONNECT_LABEL, "enabled": reason is None, "reason": reason,
            "what": RECONNECT_WHAT, "confirm": RECONNECT_CONFIRM}


def one_button_usable(snap):
    return reconnect_state(snap)["enabled"] or reconnect_busy(snap)


def pick_button(snap, ladder):
    """The first button of the ladder that is usable and did not fail lately."""
    failed = snap.get("fix_failed") or {}
    for a in ladder:
        if a not in failed and button_usable(snap, a):
            return a
    return None


STEP_ONE_BUTTON = ("화면의 [%s] 버튼을 누르세요. 작은 보드를 다시 켜고 패널·음악 프로그램까지 차례로 이어요(약 30초). "
                   "사람이 할 일이 생기면 화면이 한 가지만 알려 줘요." % RECONNECT_LABEL)


def _one_step(snap, lead=""):
    """(the step that points at the one button, "reconnect_all"), or (None, None)
    when it cannot run now (a single button's press in progress, old firmware)."""
    if not one_button_usable(snap):
        return None, None
    return lead + STEP_ONE_BUTTON, RECONNECT_ID


def _silent_steps(snap, cfg, lead=""):
    """(steps, button) for a session that is up but silent: the panel's power
    first, then the one button (it restarts FlowLocal, which drops the half-dead
    session and makes the panel join afresh - what RST did on 2026-09-27 when
    ending the session and the panel's restart did not), then the power."""
    one, b = _one_step(snap, lead)
    steps = [_panel_power_step(cfg)]
    if one:
        steps.append(one)
    elif commands_unanswered(snap):
        steps.append("작은 보드가 화면의 버튼에 답하지 않아요(새 FlowLocal 펌웨어가 필요해요).")
    steps.append(("그래도 그대로면 " if b else "") + STEP_PANEL_POWER_CYCLE)
    return steps, b


def _no_session_steps(snap, st, cfg):
    """(steps, button) for devices on the hotspot but no session. The panel's
    power comes first (a panel that lost power stays on FlowLocal's station
    list for a while); then the one button (a new start of FlowLocal and, if
    the panel still sits there without a session, its restart). With two or
    more devices FlowLocal cannot tell which one is the panel: phones off first."""
    cl = st["cl"]
    steps = [_panel_power_step(cfg)]
    if cl is not None and cl >= 2:
        steps.append("PatternflowLocal에 붙은 폰·노트북의 Wi-Fi를 꺼요(점검용 폰 포함). 핫스팟에 패널만 남아야 해요.")
        one, b = _one_step(snap, "1분 기다려요. 그래도 안 붙으면 ")
    else:
        one, b = _one_step(snap, "그림이 보이면 ")
    if one:
        steps.append(one)
    if cl is None or cl < 2:
        steps.append("PatternflowLocal에 붙은 폰이 있으면 그 폰의 Wi-Fi를 꺼요.")
    steps.append("그래도 그대로면 " + STEP_PANEL_POWER_CYCLE)
    return steps, b


def usb_loss_s(snap):
    """How much later FlowLocal got panel MIDI than the laptop's last panel
    MIDI (seconds), when that is more than USB_LOSS_S; else None. Only counts
    MIDI FlowLocal got while this monitor was listening."""
    st = snap["status"]
    if st is None or st["ra"] is None or st["ra"] < 0 or snap["status_age"] is None:
        return None
    in_age = snap["port"]["in_open_age"]
    if in_age is None:
        return None
    fl_age = snap["status_age"] + st["ra"] / 1000.0
    if fl_age > in_age - 2.0:
        return None
    laptop_age = snap["midi"]["last_age"] if snap["midi"]["since_open"] else in_age
    loss = laptop_age - fl_age
    return loss if loss > USB_LOSS_S else None


def wireless_problems(snap, st):
    """(key, Korean text) for each sign that the panel's signals came late:
    held-up knob messages seen here (bunches after the session settled),
    FlowLocal's jitter (jt), its largest late arrival in a minute (jm), lost
    packets (gp). Empty when there is none: a weak signal (prs) alone is no
    such sign, so it is only named next to one (the likely cause).

    Nothing counts in the WIFI_SETTLE_S after a session came up, and neither
    does what happened in them (bunches and gaps are counted from then on:
    Tracker.snapshot); jm covers a minute, so it waits JUMP_SETTLE_S. Bunches
    timed by this monitor's read times (arrival source "read", the driver's
    timestamps looked broken) also catch the monitor's own stalls, so then
    they count only next to a FlowLocal-side sign."""
    out = []
    arr = snap.get("arrivals") or {}
    bunches = arr.get("bunches_60s") or 0
    weak = None
    if st is not None:
        up_age = (snap.get("session") or {}).get("up_age")
        if up_age is None or up_age < WIFI_SETTLE_S:
            return out
        if st["jt"] is not None and st["jt"] >= JITTER_WARN_MS:
            out.append(("jt", "도착 흔들림 %d ms" % st["jt"]))
        if st["jm"] is not None and st["jm"] >= JUMP_WARN_MS and up_age >= JUMP_SETTLE_S:
            out.append(("jm", "최근 1분 가장 큰 늦음 %d ms" % st["jm"]))
        gp = (snap.get("wireless") or {}).get("gp_min")
        if gp is not None and gp >= GAPS_WARN_PER_MIN:
            out.append(("gp", "빠진 패킷 1분에 %d개" % gp))
        if st["prs"] is not None and st["prs"] < WEAK_RSSI_DBM:
            weak = ("prs", "신호 세기 %d dBm (%d보다 약함)" % (st["prs"], WEAK_RSSI_DBM))
    if bunches >= BUNCHES_WARN_PER_MIN and (arr.get("source") != "read" or out):
        out.append(("bunch", "같은 노브 신호가 몰려 옴 1분에 %d번" % bunches))
    if out and weak:
        out.append(weak)
    return out


GRACE_CODES = frozenset(("CHECKING", "PORT_RECONNECTING", "LIVE_STARTING", "FL_STARTING",
                         "PANEL_RECONNECTING", "PANEL_JOINING", "FIX_WAIT", "LIVE_RESTARTING", "RECONNECT_RUNNING"))
# What a button press in progress is expected to disturb: FIX_WAIT stands in for these.
CHAIN_CODES = frozenset(("CHECKING", "PORT_RECONNECTING", "PORT_MISSING", "PORT_BUSY", "FL_NO_REPLY",
                         "FL_OLD_FIRMWARE", "FL_STARTING", "FL_AP_DOWN", "HOTSPOT_FULL", "PANEL_RECONNECTING",
                         "PANEL_NOT_ON_HOTSPOT", "PANEL_JOINING", "PANEL_NO_SESSION", "PANEL_SILENT",
                         "USB_LOSS", "WIFI_POOR"))
# ... and what the one-button run takes care of on top (its Live step)
RECONNECT_CODES = CHAIN_CODES | frozenset(("FIX_WAIT", "LIVE_STARTING", "LIVE_RESTARTING", "LIVE_RESTARTED",
                                           "LIVE_NOT_RUNNING", "LIVE_GAVE_UP"))


def diagnose(snap, cfg=None):
    """One level (ok / warn / bad), one headline, "what to do now" steps and
    the button the steps point to.

    Every rule below that matches adds an issue; the rules are in priority
    order. The headline is the most severe issue, the earliest rule among
    equals; the rest are listed as "also check". Grace periods keep normal
    start-up and reconnects yellow ("기다리는 중") instead of red, and a
    button press in progress stands in for what it disturbs."""
    issues = []

    def add(*a, **k):
        issues.append(_issue(*a, **k))

    port = snap["port"]
    midi = snap["midi"]
    fresh = status_is_fresh(snap)
    st = snap["status"] if fresh else None
    fl_up = snap.get("fl_up_s")
    old_fw = old_firmware_suspected(snap)

    # 1. The monitor itself cannot read MIDI: nothing about the chain is known.
    if not snap["midi_lib"]:
        add("MONITOR_NO_MIDI", "warn", "모니터가 MIDI 신호를 읽을 수 없어요", [
            "소리가 나고, 패널 노브를 돌리면 소리가 바뀌는지 직접 확인해 주세요.",
            "담당자에게 '모니터에 python-rtmidi 설치 필요'라고 알려 주세요.",
        ], "이 노트북에 python-rtmidi가 없어요. 전시(소리)와는 상관없고, 이 화면만 연결 상태를 못 봐요.")

    # 2. FlowLocal's USB-MIDI port is not on the laptop.
    elif not port["checked"]:
        add("CHECKING", "warn", "연결 상태를 확인하는 중이에요…", ["몇 초만 기다려 주세요."], wait=True)
    elif not port["present"]:
        if port["changed_age"] is not None and port["changed_age"] < PORT_GRACE_S \
                and snap["monitor_up_s"] > port["changed_age"] + 0.5:
            add("PORT_RECONNECTING", "warn", "작은 보드(FlowLocal)를 다시 찾는 중이에요…",
                ["몇 초만 기다려 주세요."], "보드가 방금 빠졌거나 다시 켜지는 중일 수 있어요.", wait=True)
        else:
            add("PORT_MISSING", "bad", "작은 보드(FlowLocal)가 노트북에 안 보여요", [
                "작은 보드(라벨: FlowLocal)가 노트북의 테이프 표시한 USB 자리에 꽂혀 있는지 봐요. "
                "보드 쪽은 'USB'라고 적힌 잭이어야 해요('COM' 잭이 아니에요).",
                "꽂혀 있으면 뽑았다가 5초 뒤 같은 자리에 다시 꽂아요. 1분 안에 이 화면이 초록으로 바뀌는지 봐요.",
                STEP_RESTART_LAPTOP,
                _step_call(cfg),
            ], "패널 신호가 음악 프로그램까지 오지 않아요. 음악은 계속 나와도 패널 그림을 따라가지 않아요. "
               "화면의 버튼들도 작은 보드가 보여야 쓸 수 있어요.")

    # 3. The port is there but this monitor could not open it.
    elif not (port["in_open"] and port["out_open"]):
        add("PORT_BUSY", "warn", "모니터가 FlowLocal 신호를 읽지 못하고 있어요", [
            "소리가 나고, 패널 노브를 돌리면 소리가 바뀌는지 직접 확인해 주세요. 그렇다면 전시는 괜찮아요.",
            "모니터가 몇 초마다 다시 시도해요. 잠깐 기다려 주세요.",
            "계속 이러면 담당자에게 알려 주세요. 노트북을 다시 시작하면 풀려요(소리가 잘 나면 전시가 끝난 뒤에).",
        ], "보드는 노트북에 보이지만 이 모니터가 연결을 열지 못했어요" +
            (" (%s)." % port["error"] if port["error"] else "."))

    port_ok = snap["midi_lib"] and port["present"] and port["in_open"] and port["out_open"]

    # 3b. FlowLocal keeps dropping off USB and coming back: a loose cable or too
    # little USB power (2026-09-25 21:02-21:04, four starts in a minute after a replug).
    flap = snap.get("flap") or {}
    if snap["midi_lib"] and (flap.get("now") or flap.get("recent")):
        steps = [
            "작은 보드(FlowLocal) 케이블이 노트북의 테이프 표시한 자리에 끝까지 꽂혀 있는지 봐요. 보드 쪽 'USB' 잭도 "
            "끝까지 들어가 있어야 해요. 헐거우면 뽑았다가 5초 뒤 같은 테이프 자리에 끝까지 꽂아요(다른 자리로 옮기지 않아요).",
            "노트북 충전기가 노트북과 콘센트에 꽂혀 있는지 봐요. 배터리로만 돌면 USB 전원이 약해질 수 있어요.",
            ("2분 동안 이 화면을 지켜봐요. 그래도 " if flap.get("now") else "") +
            "또 다시 켜지면 담당자에게 연락해 주세요%s." % _contact_suffix(cfg),
            STEP_POWER_SCRIPT,
        ]
        if flap.get("now"):
            add("FL_FLAPPING", "bad", "FlowLocal이 계속 다시 켜지고 있어요", steps,
                "작은 보드가 최근 %s 동안 %d번 노트북에서 사라졌다가 돌아왔어요. 케이블이 헐겁거나 USB 전원이 모자랄 때 "
                "이래요. 켜질 때마다 30초쯤 소리가 패널을 따라가지 않아요." % (fmt_dur(FLAP_WINDOW_S), flap["count_window"]))
        else:
            add("FL_FLAPPING", "warn", "FlowLocal이 조금 전 여러 번 다시 켜졌어요", steps,
                "작은 보드가 %s 전까지 짧은 사이에 여러 번(최근 %s 동안 %d번) 노트북에서 사라졌다가 돌아왔어요. "
                "지금은 멈췄지만 케이블이나 전원 때문일 수 있어요." % (
                    fmt_dur(flap["run_end_age"]), fmt_dur(FLAP_WINDOW_S + FLAP_NOTE_S), flap["count"]))

    # 4. FlowLocal's status replies. Silence from a FlowLocal known to answer on
    # this laptop (this run or an earlier one) is a stuck board: its RST button,
    # then a replug. The screen's buttons go over the same USB and cannot reach it.
    silence = fl_silence_s(snap)
    if port_ok and not fresh:
        if silence is not None:
            if silence < FL_NO_REPLY_BAD_S:
                add("FL_NO_REPLY", "warn", "작은 보드(FlowLocal)가 잠깐 대답하지 않아요",
                    ["20초만 기다려 주세요."], "%s째 응답이 없어요. 보통 금방 돌아와요." % fmt_dur(silence), wait=True)
            elif panel_midi_during_silence(snap, silence):
                add("FL_NO_REPLY", "warn", "FlowLocal이 모니터에 대답하지 않아요 — 패널 신호는 들어와요", [
                    "소리가 나고, 패널 노브를 돌리면 소리가 바뀌면 그대로 두세요.",
                    "소리가 패널을 따라가지 않거나, 관람객이 적을 때: " + STEP_RST,
                    STEP_REPLUG_TAPED,
                    _step_call(cfg, "('FlowLocal이 모니터에 대답하지 않아요'라고 알려 주세요.)"),
                ], "%s 패널 신호는 %s에도 노트북에 들어왔어요. 전시는 돌지만 이 화면과 버튼은 작은 보드에 닿지 않아요." % (
                    fl_stopped_detail(snap, silence, full=False), fmt_ago(midi["last_age"])))
            else:
                add("FL_NO_REPLY", "bad", "FlowLocal이 대답을 멈췄어요", [
                    STEP_RST,
                    STEP_REPLUG_TAPED,
                    _step_call(cfg, "('FlowLocal이 대답을 멈췄어요'라고 알려 주세요.)"),
                ], fl_stopped_detail(snap, silence))
        elif old_fw:
            la = midi["last_age"]
            seen = ("마지막 패널 신호: %s." % fmt_ago(la)) if la is not None else \
                "아직 패널 신호가 없어요(아무도 노브를 안 만지면 신호가 없는 게 정상이에요)."
            add("FL_OLD_FIRMWARE", "warn", "FlowLocal 상태를 알 수 없어요 — 새 FlowLocal 펌웨어가 필요해요", [
                "소리가 나고, 패널 노브를 돌리면 소리가 바뀌면 그대로 두세요.",
                "소리가 패널을 따라가지 않으면: " + STEP_PANEL_POWER_CYCLE,
                "그래도 안 되면: " + STEP_REPLUG_SHORT,
                "담당자에게 'FlowLocal 펌웨어 업데이트 필요'라고 알려 주세요.",
            ], "작은 보드의 펌웨어가 상태 알림 기능이 없는 예전 것이에요. 패널이 핫스팟에 붙었는지는 이 화면에 안 나오고, "
               "들어오는 패널 신호만 보여요. '소리 다시 맞추기' 말고 다른 고치기 버튼도 새 펌웨어가 있어야 해요. " + seen)
        else:
            add("CHECKING", "warn", "연결 상태를 확인하는 중이에요…", ["몇 초만 기다려 주세요."], wait=True)

    # 5. What FlowLocal says about the hotspot and the panel.
    if port_ok and st is not None:
        ps, cl, cm, hd = st["ps"], st["cl"], st["cm"], st["hd"]
        starting = fl_up is not None and fl_up < FL_BOOT_GRACE_S
        if starting and (st["ap"] is False or not ps):
            add("FL_STARTING", "warn", "작은 보드(FlowLocal)가 켜지는 중이에요… 보통 30초 안에 패널까지 붙어요", [
                "아무것도 누르지 말고 기다려 주세요.",
            ], "작은 보드가 %s 전에 켜졌어요. 12초쯤 뒤 핫스팟이 뜨고, 패널이 다시 붙어요." % fmt_dur(fl_up), wait=True)
        elif st["ap"] is False:
            one, b = _one_step(snap)
            steps = ([one, "이 화면이 1분 안에 그대로면: " + _step_replug()] if b
                     else [_step_replug(), "1분 기다려요. 패널이 다시 붙으면 이 화면이 초록이 돼요."])
            add("FL_AP_DOWN", "bad", "FlowLocal 핫스팟(PatternflowLocal)이 꺼져 있어요",
                steps + [STEP_RESTART_LAPTOP, _step_call(cfg)], "패널이 붙을 Wi-Fi가 없어요.", button=b,
                op_button=pick_button(snap, ("reboot_flowlocal",)))
        elif ps == 0:
            if cm and cl is not None and cl >= cm:
                add("HOTSPOT_FULL", "bad", "핫스팟 자리가 꽉 찼어요 (%d/%d대) — 패널이 들어올 자리가 없어요" % (cl, cm), [
                    "PatternflowLocal Wi-Fi에 붙은 폰·노트북의 Wi-Fi를 꺼요. 점검용 폰도 점검이 끝났으면 꺼요.",
                    "1분 기다려요. 패널이 자리를 잡으면 이 화면이 초록이 돼요.",
                    _step_call(cfg),
                ], "핫스팟에는 기기가 %d대까지만 붙어요. 패널을 뺀 기기가 자리를 모두 차지하고 있어요." % cm)
            elif cl == 0:
                down = snap["session"]["down_age"]
                if down is not None and down < PANEL_RECONNECT_GRACE_S:
                    add("PANEL_RECONNECTING", "warn", "패널이 다시 붙는 중이에요… 보통 30초 안에 붙어요", [
                        "기다려 주세요. 패널에 그림이 보이는지(전원이 켜져 있는지)만 봐 주세요.",
                    ], "%s 전에 패널 연결이 끊겼어요." % fmt_dur(down), wait=True)
                else:
                    one, b = _one_step(snap, "켜져 있는데 1분이 지나도 그대로면 ")
                    steps = [_panel_power_step(cfg), "켜져 있으면 1분 기다려요."]
                    if one:
                        steps[1] = one
                    steps += ["그래도 그대로면 " + STEP_PANEL_POWER_CYCLE,
                              _step_call(cfg, "(패널에 다른 Wi-Fi가 저장돼 있으면 PatternflowLocal에 못 붙어요.)")]
                    add("PANEL_NOT_ON_HOTSPOT", "bad", "패널이 핫스팟에 없어요", steps,
                        "핫스팟(PatternflowLocal)에 붙은 기기가 하나도 없어요. 패널이 꺼졌거나 Wi-Fi에 못 붙고 있어요.",
                        button=b)
            else:
                since = snap["session"]["stations_age"] if cl else snap["session"]["changed_age"]
                silent = snap["session"].get("drop_silent")
                if not silent and (since is None or since < PANEL_JOIN_GRACE_S):
                    add("PANEL_JOINING", "warn", "패널 연결을 기다리는 중이에요… 보통 20초 안에 붙어요", [
                        "기다려 주세요.",
                    ], "핫스팟에 기기가 붙어 있어요. 패널이 신호 연결을 여는 것을 기다리고 있어요.", wait=True)
                else:
                    steps, b = _no_session_steps(snap, st, cfg)
                    steps.append(_step_call(cfg, "(패널 MIDI 설정 확인이 필요할 수 있어요.)"))
                    if silent:
                        detail = ("패널 소식이 먼저 끊기고, %s 전에 신호 연결도 끝났어요. 패널이 꺼졌거나 멈췄을 수 있어요. "
                                  "핫스팟에는 기기 %s대가 남아 있다고 나오지만, 꺼진 기기도 30초쯤은 목록에 남아요." % (
                                      fmt_dur(snap["session"]["down_age"]), cl if cl is not None else "?"))
                    else:
                        detail = ("핫스팟에 기기 %s대가 붙어 있지만 패널의 신호 연결이 %s째 없어요. "
                                  "붙은 기기가 패널이 아니라 폰일 수도 있어요." % (cl if cl is not None else "?", fmt_dur(since)))
                    op = pick_button(snap, ("reboot_panel",)) if not (cl is not None and cl >= 2) else None
                    add("PANEL_NO_SESSION", "bad", "패널 신호가 끊겼어요 — 패널 전원부터 봐 주세요" if silent
                        else "패널이 핫스팟에는 있는데 신호 연결이 안 돼요", steps, detail, button=b, op_button=op)
        elif ps is not None and ps > 0:
            if hd is not None and hd > HEARD_BAD_MS:
                steps, b = _silent_steps(snap, cfg, "그림이 보이면 ")
                add("PANEL_SILENT", "bad", "패널 신호가 끊겼어요 (끊김 감지)", steps + [_step_call(cfg)],
                    "연결은 남아 있지만 %s째 패널 소식이 없어요. 패널이 꺼졌거나 멈췄거나, 연결이 반쯤 끊겼어요." % fmt_dur(hd / 1000.0),
                    button=b, op_button=pick_button(snap, ("reconnect", "reboot_panel")))

    # 6. Panel MIDI reaches FlowLocal but not the laptop.
    if port_ok and st is not None and st["ps"]:
        loss = usb_loss_s(snap)
        if loss is not None:
            one, b = _one_step(snap, "안 바뀌면: ")
            steps = ["패널 노브를 돌려 소리가 바뀌는지 들어 봐요. 바뀌면 그대로 두세요."]
            steps += [one, "그래도 안 되면: " + _step_replug()] if one else ["안 바뀌면: " + _step_replug()]
            add("USB_LOSS", "warn", "패널 신호가 작은 보드까지는 오는데 노트북에 안 들어와요",
                steps + [STEP_RESTART_LAPTOP, _step_call(cfg)],
                "작은 보드는 %s 전에 패널 신호를 받았는데, 노트북에 들어온 마지막 패널 신호는 그보다 %s 전이에요." % (
                    fmt_dur(snap["status_age"] + st["ra"] / 1000.0), fmt_dur(loss)), button=b)

    # 6b. FlowLocal restarted by itself lately (firmware flx-mon2 on: rsn=, bc=).
    # It is back; what it means depends on why.
    sr = snap.get("fl_restart")
    if port_ok and st is not None and sr and sr.get("age") is not None and sr["age"] < SELF_RESTART_NOTE_S:
        contact = _contact_suffix(cfg)
        extra = ""
        if sr.get("bc") and sr["bc"] > 1:
            extra += " 전원을 넣은 뒤 %d번째 켜진 거예요." % sr["bc"]
        if sr.get("why"):
            extra += " 펌웨어가 남긴 이유: %s." % (
                "%s (%s)" % (note_word(sr["why"]), sr["why"]) if sr["why"] in RESTART_NOTES else sr["why"])
        if sr["kind"] == "power":
            steps = [
                "작은 보드(FlowLocal) 케이블이 노트북의 테이프 표시한 자리에 끝까지 꽂혀 있는지 봐요. 헐거우면 뽑았다가 "
                "5초 뒤 같은 테이프 자리에 끝까지 꽂아요.",
                "노트북 충전기가 꽂혀 있는지 봐요.",
                "담당자에게 'FlowLocal 전원 부족으로 다시 켜짐'이라고 알려 주세요%s." % contact,
            ]
            detail = ("작은 보드가 %s 전에 %s 때문에 다시 켜졌고, 지금은 스스로 회복했어요. "
                      "케이블이 헐겁거나 USB 포트의 전원이 약하면 이래요.%s" % (fmt_dur(sr["age"]), sr["word"], extra))
        elif sr["kind"] == "crash":
            steps = [
                "지금 할 일은 없어요. 작은 보드가 스스로 다시 켜져서 회복했어요.",
                "담당자에게 'FlowLocal이 스스로 다시 켜짐 (이유: %s)'이라고 알려 주세요%s." % (sr["word"], contact),
            ]
            detail = ("작은 보드의 펌웨어가 %s 전에 멈춰서(%s) 스스로 다시 켜졌고, 지금은 회복했어요. "
                      "다시 켜질 때 30초쯤 소리가 패널을 따라가지 않았을 거예요.%s" % (fmt_dur(sr["age"]), sr["word"], extra))
        else:
            steps = [
                "지금 할 일은 없어요. 작은 보드가 스스로 다시 켜져서 회복했어요.",
                "자주 그러면 담당자에게 알려 주세요%s." % contact,
            ]
            detail = "작은 보드가 %s 전에 스스로 다시 켜졌고(이유: %s), 지금은 회복했어요.%s" % (
                fmt_dur(sr["age"]), sr["word"], extra)
        add("FL_SELF_RESTART", "warn", "FlowLocal이 스스로 다시 켜졌어요 (이유: %s)" % sr["word"], steps, detail)

    # 7. Ableton Live, and the monitor starting it again after it quit (LiveKeeper).
    live = snap["live"]
    keep = snap.get("live_keeper") or {}
    button = _cfg_text(cfg, "live_recovery_button")
    recovery = (("음악 프로그램이 열리면서 복구 창이 뜨면 '%s' 버튼을 눌러요." % button) if button
                else "음악 프로그램이 열리면서 복구 창이 뜨면 스태프 안내문에 적힌 버튼을 눌러요.")
    if keep.get("busy") and (keep["phase"] != "gone" or live_autorestart_on(cfg)):
        crash = keep.get("crash")
        gone = "%s 전에 %s" % (fmt_dur(keep.get("gone_age")), "충돌로 꺼졌어요 (Windows 기록: %s)" % (
            crash.get("module") or "응답 없음") if crash else "꺼졌어요")
        if keep["phase"] == "loading":
            head = "음악 프로그램이 다시 켜져서 세트를 여는 중이에요… 1분쯤 걸려요"
            detail = "음악 프로그램이 %s. 모니터가 다시 켰고, 지금 세트를 열고 있어요. 다 열리면 이 안내가 사라져요." % gone
        else:
            head = "음악 프로그램(Ableton Live)이 꺼져서 다시 켜는 중이에요… 1분쯤 걸려요"
            n = (keep.get("starts_window") or 0) + (1 if keep["phase"] == "gone" else 0)
            detail = "음악 프로그램이 %s. 모니터가 전시 세트를 열어 다시 켜요 (%s 안에 %d번째)." % (
                gone, fmt_dur(LIVE_RESTART_WINDOW_S), max(1, n))
        add("LIVE_RESTARTING", "warn", head, [
            "아무것도 누르지 말고 기다려 주세요.",
            recovery,
            "2분이 지나도 소리가 안 나면 담당자에게 연락해 주세요%s." % _contact_suffix(cfg),
        ], detail, wait=True)
    elif live["running"] is False:
        if not live["seen"] and snap["monitor_up_s"] < LIVE_START_GRACE_S:
            add("LIVE_STARTING", "warn", "음악 프로그램(Ableton Live)이 열리는 중이에요…", [
                "기다려 주세요. 노트북을 켠 직후에는 1–2분 걸려요.",
            ], "음악 프로그램이 열리고 소리가 나면 이 화면이 알려 줘요.", wait=True)
        elif keep.get("phase") == "stopped" and keep.get("stop_reason") == "limit":
            add("LIVE_GAVE_UP", "bad", "Live가 계속 꺼져요 — 담당자에게 연락해 주세요", [
                "담당자에게 바로 연락해 주세요%s. '음악 프로그램이 계속 꺼져요'라고 알려 주세요." % _contact_suffix(cfg),
                "기다리는 동안: 노트북을 다시 시작해요(시작 → 전원 → 다시 시작). 음악 프로그램이 열리면 소리가 나는지 봐요.",
                recovery,
            ], "음악 프로그램이 %s 안에 %d번 꺼져서 모니터가 그때마다 다시 켰어요. 더 켜면 또 꺼질 수 있어서 멈췄어요. "
               "노트북이나 오디오 장치에 문제가 있을 수 있어요." % (fmt_dur(LIVE_RESTART_WINDOW_S), keep.get("starts_window") or 0))
        else:
            reason = {"off": " 모니터의 자동 다시 켜기는 꺼져 있어요(운영자용 칸).",
                      "error": " 모니터가 다시 켜지 못했어요: %s." % (keep.get("error") or "?"),
                      "shutdown": " Windows가 꺼지는 중이었어요."}.get(keep.get("stop_reason") or "", "")
            steps = ["노트북을 다시 시작해요(시작 → 전원 → 다시 시작). 음악 프로그램이 저절로 열려요.",
                     recovery,
                     "소리가 나고 이 화면이 초록이 될 때까지 기다려요(1–2분).",
                     _step_call(cfg)]
            b = None
            if live_autorestart_on(cfg) and keep.get("stop_reason") not in ("shutdown", "off"):
                # the one button opens the exhibition set too (the switch off: the operator closed Live on purpose)
                one, b = _one_step(snap)
                if one:
                    steps = [one + " 음악 프로그램(전시 세트)도 켜요.",
                             "그래도 안 켜지면: " + steps[0]] + steps[1:]
            add("LIVE_NOT_RUNNING", "bad", "음악 프로그램(Ableton Live)이 꺼져 있어요", steps,
                "음악 프로그램이 꺼지면 소리가 나지 않아요." + reason, button=b)
    elif (live["running"] and keep.get("done_age") is not None and keep["done_age"] < LIVE_RESTARTED_NOTE_S
          and keep.get("done_confirmed") is False):
        add("LIVE_RESTARTED", "warn", "음악 프로그램이 조금 전 다시 켜졌어요 — 소리가 나는지 봐 주세요", [
            recovery,
            "소리가 나고, 패널 노브를 돌리면 소리가 바뀌면 그대로 두세요.",
            "소리가 안 나면 담당자에게 연락해 주세요%s." % _contact_suffix(cfg),
        ], "모니터가 %s 전에 음악 프로그램을 다시 켰어요(뜨는 데 %s초). 세트가 열렸는지는 이 화면이 확인하지 못했어요 "
           "(세트가 열리면 음악 프로그램이 작은 보드에 보내는 신호가 오지 않았어요)." % (
            fmt_dur(keep["done_age"]), keep.get("done_took")))

    # 8. The charger. The show goes on on battery (the power settings allow for it):
    # calm and yellow, red only when the battery runs low.
    power = snap["power"]
    if power["ac"] is False:
        pct = power["battery"]
        low = pct is not None and pct < LOW_BATTERY_PCT
        dc = [i for i in staff_power_problems(snap) if i.get("dc_ok") is False]
        if low:
            head = "노트북 배터리가 얼마 안 남았어요 (%d%%) — 충전기를 바로 꽂아 주세요" % pct
        else:
            head = ("노트북 충전기가 빠졌어요%s — 소리는 계속 나요. 충전기를 다시 꽂아 주세요" %
                    (" (배터리 %d%%)" % pct if pct is not None else ""))
        detail = ("노트북이 배터리로 돌고 있어서 소리는 계속 나요. 충전기만 다시 꽂으면 돼요. "
                  "배터리가 다 떨어지면 노트북이 꺼져 소리가 멈춰요.")
        if dc:
            detail += (" 지금은 배터리일 때 절전 설정이 켜져 있어서(%s) 오래 두면 노트북이 잠들 수 있어요." %
                       ", ".join(i["label"] for i in dc))
        add("ON_BATTERY", "bad" if low else "warn", head, [
            "노트북 충전기를 다시 꽂아 주세요. 노트북 쪽과 콘센트(멀티탭) 쪽을 둘 다 봐요.",
            "멀티탭 스위치가 켜져 있는지 봐요.",
            "꽂으면 몇 초 안에 이 안내가 사라져요. 그래도 '배터리'로 나오면 담당자에게 연락해 주세요%s." % _contact_suffix(cfg),
        ], detail)

    # 9. Wireless quality: late or bunched signals.
    wl = []
    if port_ok and st is not None and st["ps"]:
        wl = wireless_problems(snap, st)
    elif port_ok and old_fw:
        wl = wireless_problems(snap, None)       # the monitor's own burst count still works
    if wl:
        chm = st["chm"] if st is not None else None
        steps = ["근처에서 켜 둔 폰 핫스팟(스태프 폰 포함)을 꺼요."]
        b = None
        if st is not None and channel_view(snap).get("support") is True and button_usable(snap, "channel_next"):
            # flx-mon3: the channel button, right after the phones (the staff may press it)
            b = "channel_next"
            steps.append("그래도 나쁘면 관람객이 적을 때 화면의 [%s] 버튼을 누르세요. 무선 채널을 1·6·11 중 다음 것으로 "
                         "바꿔요(15초쯤 소리가 패널을 따라가지 않고, 음악은 계속 나와요)." % ACTIONS["channel_next"]["label"])
        elif st is not None and chm not in ("fixed", "manual"):
            one, b = _one_step(snap, "그래도 나쁘면 관람객이 적을 때 ")
            if one:
                steps.append(one + " 작은 보드가 켜질 때 가장 조용한 무선 채널을 다시 골라요.")
        # Moving FlowLocal is the operator's job: staff do not unplug it or move its port (STAFF_CARD.md).
        why = ("무선 채널이 고정돼 있어요. " if chm in ("fixed", "manual") else
               "새 FlowLocal 펌웨어는 켤 때 조용한 무선 채널을 골라요. " if st is None else "")
        steps.append("그래도 나쁘면 담당자에게 '무선 나쁨'이라고 알려 주세요%s. (%s작은 보드를 짧은 USB 연장선으로 "
                     "노트북에서 떼어 높은 곳에 두거나 파란색(USB 3.0) 포트를 피하는 것은 담당자가 해요.)" % (
                         _contact_suffix(cfg), why))
        add("WIFI_POOR", "warn", "무선 상태가 나빠요 — 패널 신호가 늦게 오거나 몰려서 와요", steps,
            "지금: " + ", ".join(t for _k, t in wl) + ". 노브를 돌려도 소리가 늦게 따라오거나 한꺼번에 바뀔 수 있어요. "
            "'자세히'의 '신호 도착' 줄에서 몰려 온 신호(빨강)를 볼 수 있어요.", button=b,
            op_button="channel_next" if b == "channel_next" else pick_button(snap, ("reboot_flowlocal",)))

    # 10. The laptop's power settings, on the charger and on battery.
    problems = staff_power_problems(snap)
    if problems:
        has_battery = power.get("has_battery")
        add("POWER_SETTINGS", "warn", "노트북 절전 설정을 고쳐야 해요 (담당자용)", [
            "지금 당장 할 일은 없어요. 소리는 계속 나요.",
            "담당자에게 '노트북 절전 설정'이라고 알려 주세요.",
            STEP_POWER_SCRIPT,
        ], "지금 설정: " + "; ".join(power_problem_text(i, has_battery) for i in problems) +
            ". 이대로면 노트북이나 작은 보드가 잠들어 소리가 끊길 수 있어요. 충전기가 빠져도 전시가 이어지려면 "
            "배터리일 때도 모두 꺼져 있어야 해요.")

    # A button press in progress: what it disturbs is expected, not a new problem.
    fix = snap.get("fix")
    if fix is not None and fix["phase"] in ("ack", "recover") and fix["action"] != "resync":
        issues = [i for i in issues if i["code"] not in CHAIN_CODES]
        a = ACTIONS[fix["action"]]
        head = a["wait_text"] if fix["phase"] == "recover" else "'%s' — 작은 보드의 답을 기다리는 중…" % a["label"]
        issues.insert(0, _issue("FIX_WAIT", "warn", head, [
            "아무것도 누르지 말고 기다려 주세요.",
            "이 화면이 결과를 알려 줘요 (길어야 %s)." % fmt_dur(a["recover_s"] + a["ack_s"]),
        ], "'%s' 버튼을 %s 전에 눌렀어요." % (fix["label"], fmt_dur(fix["elapsed"])), wait=True))

    # The one big button's run: it takes care of the chain (and Live) now.
    rc = snap.get("reconnect") or {}
    if rc.get("running"):
        issues = [i for i in issues if i["code"] not in RECONNECT_CODES]
        where = "%d/%d단계 %s" % (rc["step_n"] or 1, rc["steps_total"], rc["step_label"] or "")
        h = rc.get("human")
        if h:
            issues.insert(0, _issue("RECONNECT_HUMAN", "warn", "한 가지만 해 주세요: " + h["short"], [
                h["text"],
                "하고 나면 이 화면이 알아채고 알아서 이어서 해요. 다른 버튼은 누르지 않아요.",
            ], "'%s' %s에서 사람의 손이 필요해요." % (RECONNECT_LABEL, where)))
        else:
            issues.insert(0, _issue("RECONNECT_RUNNING", "warn", "한 번에 다시 연결하는 중이에요… (%s)" % where, [
                "아무것도 누르지 말고 기다려 주세요. 보통 30초 안에 끝나요.",
                "단계마다 ✓가 붙어요. 사람이 할 일이 생기면 이 화면이 한 가지만 알려 줘요.",
            ], "음악은 계속 나와요. 작은 보드가 다시 켜지는 동안 20초쯤 소리가 패널을 따라가지 않아요.", wait=True))

    if not issues:
        issues.append(_issue("ALL_OK", "ok", "모두 정상이에요", [
            "스피커 소리는 이 화면이 알 수 없어요. 소리가 들리는지 가끔 들어 봐 주세요.",
            "패널 노브를 돌리면 아래 막대가 따라 움직이는지, 소리가 같이 바뀌는지 가끔 확인해 주세요.",
        ], "패널 신호가 작은 보드를 거쳐 음악 프로그램까지 잘 오고 있어요."))

    order = sorted(range(len(issues)), key=lambda i: (-SEVERITY[issues[i]["level"]], i))
    primary = issues[order[0]]
    others = [issues[i] for i in order[1:]]
    grace = primary["code"] in GRACE_CODES
    # the channel button shows on the main screen only while the wireless is poor
    # (and not on a FlowLocal known to lack command 13)
    channel_button = None
    if any(i["code"] == "WIFI_POOR" for i in issues) and channel_view(snap).get("support") is not False:
        channel_button = "channel_next"
    return {
        "level": primary["level"],
        "level_word": WAIT_WORD if primary["wait"] else LEVEL_WORD[primary["level"]],
        "wait": primary["wait"],
        "code": primary["code"],
        "headline": primary["headline"],
        "detail": primary["detail"],
        "steps": primary["steps"],
        "button": primary["button"],
        "op_button": primary.get("op_button"),
        "channel_button": channel_button,
        "others": others,
        "chain": chain_states(snap, fresh, grace, wl),
    }


def chain_states(snap, fresh, grace=False, wl=None):
    """패널 → Wi-Fi → FlowLocal → USB → 노트북 → Ableton Live → 스피커, each (state, short text)."""
    port = snap["port"]
    st = snap["status"] if fresh else None
    midi_age = snap["midi"]["last_age"]
    fl_up = snap.get("fl_up_s")

    if not snap["midi_lib"]:
        usb = fl = ("unknown", "모니터가 못 읽음")
    elif not port["checked"]:
        usb = fl = ("unknown", "확인 중")
    elif not port["present"]:
        if grace or (port["changed_age"] is not None and port["changed_age"] < PORT_GRACE_S
                     and snap["monitor_up_s"] > port["changed_age"] + 0.5):
            usb, fl = ("warn", "다시 찾는 중"), ("unknown", "다시 찾는 중")
        else:
            usb, fl = ("bad", "안 보임"), ("bad", "노트북에 안 보임")
    else:
        usb = ("ok", "연결됨") if port["in_open"] and port["out_open"] else ("warn", "모니터가 못 엶")
        if usb[0] == "ok" and st is not None and st["ps"] and usb_loss_s(snap) is not None:
            usb = ("warn", "신호가 안 들어옴")
        silence = fl_silence_s(snap)
        if st is not None:
            if st["ap"] is False:
                fl = ("warn", "켜지는 중") if grace or (fl_up is not None and fl_up < FL_BOOT_GRACE_S) else ("bad", "핫스팟 꺼짐")
            else:
                fl = ("ok", "켜진 지 %s" % fmt_dur(fl_up) if fl_up is not None else "응답함")
        elif snap["status_ever"] or silence is not None:
            age = silence if silence is not None else (snap["status_age"] or 0)
            fl = ("warn" if age < FL_NO_REPLY_BAD_S or grace else "bad", "%s째 대답 없음" % fmt_dur(age))
        elif old_firmware_suspected(snap):
            fl = ("unknown", "예전 펌웨어")
        else:
            fl = ("unknown", "확인 중")
    if (snap.get("flap") or {}).get("now") and snap["midi_lib"]:
        fl = ("bad", "계속 다시 켜짐")

    panel = wifi = ("unknown", "모름")
    if st is not None:
        ps, cl, cm, prs, hd = st["ps"], st["cl"], st["cm"], st["prs"], st["hd"]
        ch = ("ch %d · " % st["ch"]) if st["ch"] else ""
        if st["ap"] is False:
            booting = grace or (fl_up is not None and fl_up < FL_BOOT_GRACE_S)
            wifi = ("warn", "켜지는 중") if booting else ("bad", "핫스팟 꺼짐")
        elif ps is not None and ps > 0:
            if wl:
                wifi = ("warn", ch + "나쁨")
            else:
                wifi = ("ok", ch + ("%d dBm" % prs if prs is not None else "연결됨"))
            if hd is not None and hd > HEARD_BAD_MS:
                panel = ("warn", "연결 중") if grace else ("bad", "%s째 소식 없음" % fmt_dur(hd / 1000.0))
            else:
                panel = ("ok", "연결됨")
        else:
            if grace:
                wifi = ("warn", "기다리는 중")
            elif cm and cl is not None and cl >= cm:
                wifi = ("bad", "꽉 참 %d/%d" % (cl, cm))
            elif cl == 0:
                wifi = ("bad", "패널 없음")
            else:
                wifi = ("warn", "기기 %s대 · 패널 대기" % (cl if cl is not None else "?"))
            panel = ("warn", "연결 중") if grace else ("bad", "연결 안 됨")
    elif snap["midi_lib"] and port["present"] and midi_age is not None and midi_age < MIDI_RECENT_S:
        panel = ("ok", "신호 " + fmt_ago(midi_age))
        wifi = ("warn", "몰림 있음") if wl else ("ok", "신호로 확인")
    elif grace and port["present"]:
        panel = wifi = ("warn", "기다리는 중")

    power = snap["power"]
    if power["ac"] is False:
        low = power["battery"] is not None and power["battery"] < LOW_BATTERY_PCT
        laptop = ("bad" if low else "warn", "배터리 %s%%" % (power["battery"] if power["battery"] is not None else "?"))
    elif staff_power_problems(snap):
        laptop = ("warn", "절전 설정 확인")
    elif power["ac"] is True:
        laptop = ("ok", "충전기 연결" if power["has_battery"] else "켜짐")
    else:
        laptop = ("ok", "켜짐")

    live = snap["live"]
    keep = snap.get("live_keeper") or {}
    if keep.get("busy") and keep.get("phase") != "gone":
        livest = ("warn", "다시 켜는 중" if keep["phase"] == "launching" else "세트 여는 중")
    elif live["running"] is True:
        livest = ("ok", "켜짐")
    elif keep.get("phase") == "gone" and keep.get("enabled"):
        livest = ("warn", "꺼짐 · 다시 켤 준비")
    elif live["running"] is False:
        livest = ("warn", "여는 중") if not live["seen"] and snap["monitor_up_s"] < LIVE_START_GRACE_S else ("bad", "꺼짐")
    else:
        livest = ("unknown", "확인 중")

    rows = (
        ("panel", "패널", "node", panel),
        ("wifi", "Wi-Fi", "link", wifi),
        ("flowlocal", "FlowLocal", "node", fl),
        ("usb", "USB", "link", usb),
        ("laptop", "노트북", "node", laptop),
        ("live", "Ableton Live", "node", livest),
        ("speaker", "스피커", "node", ("unknown", "직접 들어 보세요")),
    )
    return [{"id": i, "label": label, "kind": kind, "state": s[0], "state_word": LEVEL_WORD[s[0]], "text": s[1]}
            for i, label, kind, s in rows]


def action_states(snap, recommended=None):
    """The single buttons as the dashboard shows them (운영자용 칸; the channel
    button also on the main screen while the wireless is poor): enabled or
    why not, and whether they need a newer FlowLocal firmware."""
    fix = snap.get("fix")
    busy = fix is not None and fix["phase"] != "done"
    old = old_firmware_suspected(snap)
    pressed = snap.get("fix_pressed") or {}
    ch = channel_view(snap)
    out = []
    for key, a in ACTIONS.items():
        cmd = a["cmd"] is not None
        group = a["group"]
        ages = [pressed[k] for k, b in ACTIONS.items() if k in pressed and pressed[k] is not None and
                (k == key or (group == "channel" and b["group"] == "channel"))]
        age = min(ages) if ages else None
        reason = "진행 중이에요" if busy and fix["action"] == key else button_block(snap, key)
        if reason is not None:
            pass
        elif busy:
            reason = "앞에 누른 버튼을 기다리는 중이에요"
        elif age is not None and age < a["gap_s"]:
            reason = "%d초 뒤에 다시 누를 수 있어요" % max(1, math.ceil(a["gap_s"] - age))
        label_full = a["label"]
        if key == "channel_next" and ch.get("ch") is not None and ch.get("next") is not None:
            label_full = "%s (지금 %d번 → 다음 %d번)" % (a["label"], ch["ch"], ch["next"])
        elif key == "channel_auto" and ch.get("chm"):
            label_full = "%s (지금: %s)" % (a["label"], {"auto": "자동", "fixed": "고정", "manual": "직접 고름"}[ch["chm"]])
        needs_fw = cmd and (old or commands_unanswered(snap, group) or (group == "channel" and ch.get("support") is False))
        out.append({
            "id": key, "label": a["label"], "label_full": label_full, "group": group,
            "when": a["when"], "what": a["what"], "confirm": a["confirm"],
            "manual": a["manual"], "enabled": reason is None, "reason": reason,
            "needs_fw": bool(needs_fw), "running": busy and fix["action"] == key,
            "recommended": key == recommended,
        })
    return out


def action_state(snap, action):
    for s in action_states(snap):
        if s["id"] == action:
            return s
    return None


def build_view(snap, fresh=None, wl=None):
    """The card values the dashboard shows, worked out here so they are tested."""
    if fresh is None:
        fresh = status_is_fresh(snap)
    st = snap["status"] if fresh else None
    midi = snap["midi"]
    old_fw = old_firmware_suspected(snap)
    if old_fw:
        why_unknown = "새 FlowLocal 펌웨어가 필요해요"
    elif fl_stopped(snap):
        why_unknown = "FlowLocal이 대답을 멈췄어요"
    elif snap["status_ever"] or fl_silence_s(snap) is not None:
        why_unknown = "FlowLocal 응답 없음"
    else:
        why_unknown = "확인 중"

    pattern = {"num": midi["pattern"], "name": midi["pattern_name"], "age": midi["pattern_age"],
               "note": ""}
    if midi["pattern"] is None:
        pattern["note"] = "패널이 그림을 바꾸면 여기에 떠요"
    elif midi["pattern_src"] == "status":
        pattern["note"] = "작은 보드가 기억하는 마지막 그림"

    knobs = [{"label": "K%d" % (i + 1), "value": midi["knobs"][i], "age": midi["knob_ages"][i]} for i in range(3)]
    moved = [k["age"] for k in knobs if k["age"] is not None]
    knob_check = {"moved_age": min(moved) if moved else None,
                  "live": bool(moved) and min(moved) < 3.0,
                  "note": "관람객이 노브를 돌리면 막대가 따라 움직여요. 움직이면 패널 → 작은 보드 → 노트북이 이어진 거예요."}

    heard = None
    if st is not None and st["hd"] is not None and st["hd"] >= 0:
        heard = round(st["hd"] / 1000.0 + (snap["status_age"] or 0), 1)
    last = {"age": midi["last_age"], "what": midi["last_text"], "heard_age": heard, "per_min": midi["per_min"],
            "note": "아무도 노브를 안 만지면 신호가 없는 게 정상이에요"}

    arr = snap.get("arrivals") or {}
    if wl is None:
        wl = wireless_problems(snap, st) if (st is not None and st["ps"]) or old_fw else []
    wireless = {"level": "unknown", "bars": 0, "word": "모름", "dbm": None, "ch": None, "chm": None, "cs": None,
                "jt": None, "jm": None, "gp_min": None, "bunches_min": arr.get("bunches_60s", 0),
                "bunches_seen": arr.get("bunches_seen_60s", 0),
                "settle_left": (snap.get("wireless") or {}).get("settle_left"),
                "problems": [t for _k, t in wl], "problem_keys": [k for k, _t in wl], "note": why_unknown}
    if st is not None:
        wireless.update(ch=st["ch"], chm=st["chm"], cs=scores_text(st.get("cs")), jt=st["jt"], jm=st["jm"],
                        gp_min=(snap.get("wireless") or {}).get("gp_min"))
        if st["ps"]:
            d = st["prs"]
            if d is not None:
                bars, word = (4, "좋음") if d >= -60 else (3, "보통") if d >= -67 else (2, "약한 편") if d >= WEAK_RSSI_DBM else (1, "약함")
                wireless.update(dbm=d, bars=bars, word=word)
            else:
                wireless.update(word="연결됨")
            wireless["level"] = "warn" if wl else "ok"
            wireless["note"] = "같은 노브 신호가 몰려 오면 아래 '신호 도착' 줄에 빨갛게 '몰림'으로 표시돼요"
            if wl:
                wireless["word"] = "나쁨"
        else:
            wireless["note"] = "패널이 연결돼 있지 않아요"
    elif old_fw and wl:
        wireless.update(level="warn", word="몰림 있음")

    hotspot = {"cl": None, "cm": None, "level": "unknown", "note": why_unknown}
    if st is not None and st["cl"] is not None:
        full = bool(st["cm"]) and st["cl"] >= st["cm"]
        hotspot = {"cl": st["cl"], "cm": st["cm"], "level": "warn" if full and not st["ps"] else "ok",
                   "note": "꽉 차면 패널이 못 들어와요. 점검용 폰은 끝나면 Wi-Fi를 꺼요."}

    uptime = {"s": snap.get("fl_up_s") if st is not None else None, "fw": st["fw"] if st is not None else None,
              "note": why_unknown if st is None else ""}
    sr = snap.get("fl_restart")
    if st is not None and sr and sr.get("age") is not None and sr["age"] < 3600:
        uptime["note"] = "스스로 다시 켜졌어요 (이유: %s%s)" % (
            sr["word"], ", 전원을 넣은 뒤 %d번째" % sr["bc"] if sr.get("bc") and sr["bc"] > 1 else "")
    elif st is not None and snap.get("fl_reboot_age") is not None and snap["fl_reboot_age"] < 3600:
        uptime["note"] = "%s 전에 다시 켜졌어요" % fmt_dur(snap["fl_reboot_age"])
    return {"pattern": pattern, "knobs": knobs, "knob_check": knob_check, "last": last, "wireless": wireless,
            "hotspot": hotspot, "uptime": uptime}


# ================================================================ system checks

def live_process_rows(tasklist_csv):
    """(image name, PID or None) of each process whose name starts with "Ableton Live",
    from `tasklist /fo csv /nh` output."""
    rows = []
    for row in csv.reader(io.StringIO(tasklist_csv)):
        if row and row[0].lower().startswith("ableton live") and row[0].lower().endswith(".exe"):
            pid = row[1].strip() if len(row) > 1 else ""
            rows.append((row[0], int(pid) if pid.isdigit() else None))
    return rows


def live_processes(tasklist_csv):
    """Image names starting with "Ableton Live" in `tasklist /fo csv /nh` output."""
    return [name for name, _pid in live_process_rows(tasklist_csv)]


_kernel32 = None


def process_path(pid):
    """The exe of one of this user's processes (QueryFullProcessImageNameW), or None."""
    global _kernel32
    if pid is None:
        return None
    try:
        if _kernel32 is None:
            k = ctypes.WinDLL("kernel32")
            k.OpenProcess.restype = ctypes.c_void_p
            k.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
            k.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_wchar_p,
                                                     ctypes.POINTER(ctypes.c_ulong)]
            k.CloseHandle.argtypes = [ctypes.c_void_p]
            _kernel32 = k
        h = _kernel32.OpenProcess(0x1000, False, pid)          # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return None
        try:
            buf = ctypes.create_unicode_buffer(1024)
            n = ctypes.c_ulong(len(buf))
            return buf.value if _kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else None
        finally:
            _kernel32.CloseHandle(h)
    except (AttributeError, OSError, ValueError, TypeError):
        return None


def check_live():
    """(running, names, exe of the first Live process or None), or (None, [], None)
    if tasklist could not run."""
    try:
        r = subprocess.run(["tasklist", "/fo", "csv", "/nh", "/fi", "IMAGENAME eq Ableton*"],
                           capture_output=True, timeout=15, creationflags=CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError):
        return None, [], None
    if r.returncode != 0:
        return None, [], None
    rows = live_process_rows(_decode(r.stdout))
    exe = process_path(rows[0][1]) if rows else None
    return bool(rows), [name for name, _pid in rows], exe


class _PowerStatus(ctypes.Structure):
    _fields_ = [("ACLineStatus", ctypes.c_ubyte), ("BatteryFlag", ctypes.c_ubyte),
                ("BatteryLifePercent", ctypes.c_ubyte), ("SystemStatusFlag", ctypes.c_ubyte),
                ("BatteryLifeTime", ctypes.c_ulong), ("BatteryFullLifeTime", ctypes.c_ulong)]


def power_from_status(ac_line, battery_flag, percent):
    """(on AC, battery %, has battery) from SYSTEM_POWER_STATUS fields; None = unknown."""
    has_battery = None if battery_flag == 255 else not (battery_flag & 128)
    ac = {0: False, 1: True}.get(ac_line)
    if has_battery is False and ac is None:
        ac = True
    pct = percent if percent <= 100 else None
    return ac, pct, has_battery


def check_power():
    try:
        s = _PowerStatus()
        if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(s)):
            return None, None, None
    except (AttributeError, OSError):
        return None, None, None
    return power_from_status(s.ACLineStatus, s.BatteryFlag, s.BatteryLifePercent)


SUB_SLEEP = "238c9fa8-0aad-41ed-83f4-97be242c8f20"          # SUB_SLEEP
SUB_USB = "2a737441-1930-4402-8d77-b2bebba308a3"            # (no alias on every machine)
SUB_BUTTONS = "4f971e89-eebd-4455-a8de-9e59040e7347"        # SUB_BUTTONS
SUB_PCIEXPRESS = "501a4d13-42af-4429-9fd1-a8218c268e20"     # SUB_PCIEXPRESS

# key, subgroup, setting, label, kind. The show needs every one at 0 on the
# charger (AC) and on battery (DC), so that pulling the charger out never stops
# it; "노트북 전시용 전원 설정.bat" (repo root) sets exactly these. By GUID: the
# aliases are not on every machine (USBSELECTIVESUSPEND is not on this laptop),
# and LIDACTION is a hidden setting that only `powercfg /qh` lists.
POWER_CHECKS = (
    ("sleep", SUB_SLEEP, "29f6c1db-86da-48c5-9fdb-f2b67b1f44da", "절전(잠자기)", "timeout"),            # STANDBYIDLE
    ("hibernate", SUB_SLEEP, "9d7815a6-7ee4-497e-8888-515a05f02364", "최대 절전", "timeout"),           # HIBERNATEIDLE
    ("lid", SUB_BUTTONS, "5ca83367-6e45-459f-a27b-476b1d01c936", "덮개를 닫을 때", "lid"),              # LIDACTION
    ("usb_suspend", SUB_USB, "48e6b7a6-50f5-4782-a5d4-53bb8f07e226", "USB 선택적 절전", "onoff"),
    ("aspm", SUB_PCIEXPRESS, "ee12f906-d277-404b-b6da-e5fa1a576df5", "PCI Express 링크 상태 전원 관리", "aspm"),  # ASPM
)
POWER_FIX_TAIL = "powercfg /setactive SCHEME_CURRENT"
LID_ACTIONS = {0: "아무것도 안 함", 1: "절전", 2: "최대 절전", 3: "시스템 종료", 4: "화면 끄기"}
ASPM_LEVELS = {0: "해제", 1: "보통 수준의 절전", 2: "최대 절전"}


def _decode(b):
    for enc in ("oem", "mbcs", "utf-8"):
        try:
            return b.decode(enc, "replace")
        except LookupError:
            continue
    return b.decode("latin-1")


_HEX_LINE = re.compile(r"^(.*?):\s*0x([0-9a-fA-F]{1,8})\s*$", re.M)
_AC_WORD = re.compile(r"(?<![A-Za-z])AC(?![A-Za-z])")
_DC_WORD = re.compile(r"(?<![A-Za-z])DC(?![A-Za-z])")


def parse_powercfg_ac_dc(text, setting_guid):
    """(AC, DC) index of one setting from `powercfg /q[h] SCHEME SUB SETTING`,
    either None if its line is missing; None if the setting is not there.

    The labels are localised ("Current AC Power Setting Index" / "현재 AC 전원
    설정 색인") but keep "AC" and "DC", so this reads the hex values after the
    setting's GUID on lines whose label says AC or DC. Where no label does,
    the block ends with the AC then the DC value (a range setting lists its
    minimum, maximum and increment in hex before them)."""
    if not text:
        return None
    i = text.lower().find(setting_guid.lower())
    if i < 0:
        return None
    lines = _HEX_LINE.findall(text[i:])
    ac = [int(v, 16) for label, v in lines if _AC_WORD.search(label) and not _DC_WORD.search(label)]
    dc = [int(v, 16) for label, v in lines if _DC_WORD.search(label) and not _AC_WORD.search(label)]
    if ac or dc:
        return (ac[0] if ac else None), (dc[0] if dc else None)
    if len(lines) < 2:
        return None
    return int(lines[-2][1], 16), int(lines[-1][1], 16)


def power_value_text(kind, v):
    if v is None:
        return "모름"
    if kind == "timeout":
        return "안 함" if v == 0 else "%s 후" % fmt_dur(v)
    if kind == "onoff":
        return "사용 안 함" if v == 0 else "사용"
    if kind == "lid":
        return LID_ACTIONS.get(v, str(v))
    if kind == "aspm":
        return ASPM_LEVELS.get(v, str(v))
    return str(v)


def power_item(check, acdc):
    """One power setting as the monitor shows it. ok: both sides right (True),
    one side wrong (False), else unknown (None) - a setting this computer
    does not have (no lid) is unknown, not a problem."""
    key, sub, setting, label, kind = check
    ac, dc = acdc if acdc else (None, None)
    ac_ok = None if ac is None else ac == 0
    dc_ok = None if dc is None else dc == 0
    ok = False if (ac_ok is False or dc_ok is False) else (True if (ac_ok and dc_ok) else None)
    fix = []
    if ac_ok is False:
        fix.append("powercfg /setacvalueindex SCHEME_CURRENT %s %s 0" % (sub, setting))
    if dc_ok is False:
        fix.append("powercfg /setdcvalueindex SCHEME_CURRENT %s %s 0" % (sub, setting))
    return {"key": key, "label": label, "kind": kind, "fix": fix,
            "ac": ac, "dc": dc, "ac_ok": ac_ok, "dc_ok": dc_ok, "ok": ok,
            "ac_text": power_value_text(kind, ac), "dc_text": power_value_text(kind, dc),
            "want_text": power_value_text(kind, 0)}


def power_item_problem(item, has_battery=None):
    """(wrong on the charger, wrong on battery) - battery only counts on a
    computer that may have one."""
    return (item.get("ac_ok") is False,
            item.get("dc_ok") is False and has_battery is not False)


def power_problem_text(item, has_battery=None):
    """ "최대 절전 = 5시간 후 (배터리)" """
    ac_bad, dc_bad = power_item_problem(item, has_battery)
    if ac_bad and dc_bad and item["ac_text"] == item["dc_text"]:
        return "%s = %s (전원 연결·배터리)" % (item["label"], item["ac_text"])
    parts = []
    if ac_bad:
        parts.append("%s = %s (전원 연결)" % (item["label"], item["ac_text"]))
    if dc_bad:
        parts.append("%s = %s (배터리)" % (item["label"], item["dc_text"]))
    return ", ".join(parts)


def check_power_settings(run=None):
    """The POWER_CHECKS of the current power plan. `powercfg /qh` first (it
    also lists hidden settings such as the lid), then /q; a setting that
    cannot be read stays unknown."""
    run = run or subprocess.run
    items = []
    for check in POWER_CHECKS:
        acdc = None
        for flag in ("/qh", "/q"):
            try:
                r = run(["powercfg", flag, "SCHEME_CURRENT", check[1], check[2]],
                        capture_output=True, timeout=15, creationflags=CREATE_NO_WINDOW)
            except (OSError, subprocess.SubprocessError):
                break
            acdc = parse_powercfg_ac_dc(_decode(r.stdout), check[2]) if r.returncode == 0 else None
            if acdc:
                break
        items.append(power_item(check, acdc))
    return items


# ================================================================ Live keeper

LIVE_GONE_GRACE_S = 8.0        # Live gone this long (restarted by hand? a tasklist blip?) before it is started again
LIVE_LAUNCH_WAIT_S = 90.0      # after a start, Live's process must show up within this
LIVE_LOAD_S = 60.0             # after it shows up, Live opens the set in about this long
LIVE_RESTART_MAX = 3           # at most this many starts ...
LIVE_RESTART_WINDOW_S = 900.0  # ... within this long, then the monitor stops and calls for the operator
LIVE_RESTARTED_NOTE_S = 300.0  # a start the set did not confirm stays a yellow "listen" note this long
LIVE_CRASH_LOOKBACK_S = 120.0  # a Windows crash record this long before Live vanished counts
LIVE_CHECK_FAST_S = 2.0        # tasklist interval while Live is gone or starting (else 5 s)
DEFAULT_LIVE_SET = "set Project/set.als"
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))


def live_set_path(cfg):
    """config.json "live_set": the exhibition set the monitor opens when it
    starts Live again. Relative to the repo root; empty = set Project/set.als."""
    v = os.path.expandvars(str((cfg or {}).get("live_set") or "").strip()) or DEFAULT_LIVE_SET
    return os.path.normpath(v if os.path.isabs(v) else os.path.join(REPO_ROOT, v))


def live_autorestart_on(cfg):
    """config.json "live_autorestart", on unless it says false."""
    v = (cfg or {}).get("live_autorestart", True)
    return v is not False and str(v).strip().lower() not in ("false", "0", "off", "no", "")


_EVT = "{http://schemas.microsoft.com/win/2004/08/events/event}"


def live_crash_records(xml_text):
    """Ableton Live's crash and hang records in `wevtutil qe Application
    /f:xml` output, in its order: 1000 Application Error, 1001 Windows Error
    Reporting (APPCRASH, AppHang), 1002 Application Hang. Each {"id", "time"
    (UTC), "kind" (crash / hang), "module", "code", "report"}. Only provider
    names, event IDs and data values are read - never the localised text."""
    import xml.etree.ElementTree as ET
    if not xml_text or "<Event" not in xml_text:
        return []
    try:
        root = ET.fromstring("<Events>%s</Events>" % xml_text[xml_text.index("<Event"):])
    except ET.ParseError:
        return []
    out = []
    for ev in root.iter(_EVT + "Event"):
        system = ev.find(_EVT + "System")
        if system is None:
            continue
        try:
            eid = int((system.findtext(_EVT + "EventID") or "").strip())
        except ValueError:
            continue
        tc = system.find(_EVT + "TimeCreated")
        named, plain = {}, []
        for d in ev.iter(_EVT + "Data"):
            text = (d.text or "").strip()
            if d.get("Name"):
                named[d.get("Name")] = text
            else:
                plain.append(text)
        rec = None
        if eid == 1000 and "ableton live" in named.get("AppName", "").lower():
            rec = {"kind": "crash", "module": named.get("ModuleName"), "code": named.get("ExceptionCode"),
                   "report": named.get("IntegratorReportId") or named.get("ReportId")}
        elif eid == 1001 and "ableton live" in named.get("P1", "").lower():
            name = named.get("EventName", "").upper()
            if name.startswith("APPHANG"):
                rec = {"kind": "hang", "module": None, "code": None, "report": named.get("ReportId")}
            elif name in ("APPCRASH", "BEX", "BEX64"):
                rec = {"kind": "crash", "module": named.get("P4"), "code": named.get("P7"),
                       "report": named.get("ReportId")}
        elif eid == 1002 and any("ableton live" in x.lower() for x in plain + list(named.values())):
            rec = {"kind": "hang", "module": None, "code": None, "report": None}
        if rec:
            rec.update(id=eid, time=None if tc is None else tc.get("SystemTime"))
            out.append(rec)
    return out


def pick_crash_record(records):
    """1000 and 1002 are written when it happens; a 1001 report can come hours
    later (2026-09-25 21:50 re-sent the report of the 16:17 crash, ReportId
    dd12d02c...), so it counts only when neither is there."""
    for want in (1000, 1002, 1001):
        for rec in records:
            if rec["id"] == want:
                return rec
    return None


def probe_live_crash(since_s):
    """The Windows record of Ableton Live crashing or hanging in the last since_s, or None."""
    q = ("*[System[(EventID=1000 or EventID=1001 or EventID=1002) and TimeCreated[timediff(@SystemTime) <= %d]]]"
         % int(max(1.0, since_s) * 1000))
    try:
        r = subprocess.run(["wevtutil", "qe", "Application", "/q:" + q, "/f:xml", "/rd:true", "/c:30"],
                           capture_output=True, timeout=15, creationflags=CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    return pick_crash_record(live_crash_records(_decode(r.stdout)))


def launch_live(set_path, exe=None, prefer_exe=False):
    """Starts Live on the exhibition set: the set's file association (the
    .als opens in Live), or Live's exe as it ran, given the set - that one
    first with prefer_exe. (ok, "set" / "exe", error)."""
    if not set_path or not os.path.isfile(set_path):
        return False, None, "전시 세트 파일이 없어요 (%s)" % (set_path or "빈칸")
    err = None
    for how in (("exe", "set") if prefer_exe else ("set", "exe")):
        try:
            if how == "set":
                os.startfile(set_path)
            elif exe and os.path.isfile(exe):
                subprocess.Popen([exe, set_path], cwd=os.path.dirname(exe), close_fds=True)
            else:
                continue
            return True, how, None
        except (OSError, AttributeError) as e:
            err = str(e)
    return False, None, err or "음악 프로그램을 여는 방법을 찾지 못했어요"


def windows_shutting_down():
    try:
        return bool(ctypes.windll.user32.GetSystemMetrics(0x2000))      # SM_SHUTTINGDOWN
    except (AttributeError, OSError):
        return False


def _cannot_launch(set_path, exe=None, prefer_exe=False):
    return False, None, "이 모니터는 음악 프로그램을 켜지 않아요 (테스트·데모)"


def live_gone_text(crash):
    if crash is None:
        return "음악 프로그램(Ableton Live)이 꺼졌어요 (Windows 충돌 기록은 없어요)"
    if crash["kind"] == "hang":
        return "음악 프로그램(Ableton Live)이 멈춰서 꺼졌어요 (Windows 기록: 응답 없음, 이벤트 %d)" % crash["id"]
    return "음악 프로그램(Ableton Live)이 충돌로 꺼졌어요 (Windows 기록: %s, %s, 이벤트 %d)" % (
        crash.get("module") or "?", crash.get("code") or "?", crash["id"])


class LiveKeeper(object):
    """Starts Ableton Live again when it quits during the show (2026-09-25
    21:44 it vanished and stayed closed; the monitor only said so).

    Only after Live was seen running in this monitor run, only while
    config.json "live_autorestart" is on (the operator turns it off before
    closing Live on purpose), only if Live is still gone LIVE_GONE_GRACE_S
    later (someone may have started it by hand) and Windows is not shutting
    down. A start opens the exhibition set ("live_set") by its file
    association; Live's exe as it ran is the fallback, and goes first after a
    start that did not bring Live up. At most LIVE_RESTART_MAX starts in
    LIVE_RESTART_WINDOW_S: then it stops and calls for the operator, until
    Live runs again or the switch is turned off and on. Windows' crash
    records only name the cause in the log - 21:44 left none in time.

    After a crash Live may ask whether to recover the set; FlowLocal's resync
    counter (rr) going up shows the set opened (the Live device asks for a
    resync 0.5 s after the set loads), else the page asks to check."""

    def __init__(self, events, launcher=_cannot_launch, crash_probe=None, shutting_down=None):
        self.events = events
        self.launcher = launcher
        self.crash_probe = crash_probe or (lambda since_s: None)
        self.shutting_down = shutting_down or (lambda: False)
        self.phase = "idle"             # idle, gone, launching, loading, stopped
        self.stop_reason = None         # off, limit, error, shutdown
        self.error = None
        self.gone_at = self.launch_at = self.up_at = None
        self.starts = collections.deque()
        self.no_show = 0                # starts in this episode that did not bring Live up
        self.crash = None
        self.how = None
        self.rr_base = None
        self.done_at = self.done_confirmed = self.done_took = None
        self.enabled = None             # config.json "live_autorestart" at the last step

    def _add(self, text, level="info"):
        self.events.add("live", text, level)

    def busy(self):
        return self.phase in ("gone", "launching", "loading")

    def step(self, now, running, seen, enabled, set_path, exe=None, rr=None):
        """One look at Live, after each tasklist check."""
        self.enabled = enabled
        if running is None:
            return                      # tasklist did not run: nothing is known
        if running:
            if self.phase == "launching":
                self.phase, self.up_at, self.rr_base = "loading", now, rr
                self._add("음악 프로그램이 다시 떴어요 (%d초 걸림). 세트를 여는 중이에요." % round(now - self.launch_at))
            if self.phase == "loading":
                if rr is not None and self.rr_base is not None and rr > self.rr_base:
                    self._finish(now, True)
                elif rr is not None and (self.rr_base is None or rr < self.rr_base):
                    self.rr_base = rr   # FlowLocal's counter showed up late, or FlowLocal restarted
                elif now - self.up_at >= LIVE_LOAD_S:
                    self._finish(now, False)
            elif self.phase in ("gone", "stopped"):
                if self.phase == "stopped" and self.stop_reason in ("limit", "error"):
                    self._add("음악 프로그램이 다시 보여요 (누가 다시 켰어요)", "ok")
                self.phase, self.stop_reason, self.error = "idle", None, None
            return
        if not seen:
            return                      # never ran in this monitor run: the laptop's start-up opens it
        if self.phase in ("idle", "loading"):
            if self.phase == "idle":
                self.no_show, self.crash = 0, None
            self.phase, self.gone_at = "gone", now
            return
        if self.phase == "stopped":
            if not enabled:
                self.stop_reason = "off"            # the switch is also the reset
            elif self.stop_reason == "off" or (self.stop_reason == "shutdown" and not self.shutting_down()):
                self.phase, self.gone_at, self.stop_reason, self.error = "gone", now, None, None
            return
        if self.phase == "launching":
            if now - self.launch_at < LIVE_LAUNCH_WAIT_S:
                return
            self.no_show += 1
            self._add("음악 프로그램이 %d초 안에 뜨지 않았어요" % LIVE_LAUNCH_WAIT_S, "warn")
            self.phase, self.gone_at = "gone", now - LIVE_GONE_GRACE_S
        if self.phase == "gone" and now - self.gone_at >= LIVE_GONE_GRACE_S:
            self._decide(now, enabled, set_path, exe)

    def _decide(self, now, enabled, set_path, exe):
        if not enabled:
            self._stop("off", "자동 다시 켜기가 꺼져 있어서 음악 프로그램을 다시 켜지 않아요", "warn")
            return
        if self.shutting_down():
            self._stop("shutdown", "Windows가 꺼지는 중이라 음악 프로그램을 다시 켜지 않아요")
            return
        while self.starts and now - self.starts[0] > LIVE_RESTART_WINDOW_S:
            self.starts.popleft()
        if len(self.starts) >= LIVE_RESTART_MAX:
            self._stop("limit", "음악 프로그램이 계속 꺼져요 (%s 안에 %d번 다시 켰어요). 더는 다시 켜지 않아요 - 담당자에게 "
                       "연락해 주세요" % (fmt_dur(LIVE_RESTART_WINDOW_S), len(self.starts)), "bad")
            return
        if self.no_show == 0:
            try:
                self.crash = self.crash_probe(now - self.gone_at + LIVE_CRASH_LOOKBACK_S)
            except Exception:
                self.crash = None
            self._add(live_gone_text(self.crash), "bad")
        ok, how, err = self.launcher(set_path, exe, self.no_show > 0)
        if not ok:
            self._stop("error", "음악 프로그램을 다시 켜지 못했어요: %s" % err, "bad", err)
            return
        self.starts.append(now)
        self.phase, self.launch_at, self.how = "launching", now, how
        self._add("음악 프로그램을 다시 켜요 (%s '%s', %s 안에 %d번째)" % (
            "파일 연결로" if how == "set" else "실행 파일로", os.path.basename(set_path),
            fmt_dur(LIVE_RESTART_WINDOW_S), len(self.starts)), "warn")

    def start_now(self, now, set_path, exe=None):
        """A person asked for Live ([한 번에 다시 연결하기]): start the exhibition
        set now, whatever the count of automatic starts (this is not one of
        them). The keeper then follows it like its own start (up, set loading,
        FlowLocal's rr). A start already under way is left to finish.
        (ok, how, error)."""
        if self.phase in ("launching", "loading"):
            return True, self.how, None
        ok, how, err = self.launcher(set_path, exe, False)
        if not ok:
            self._add("음악 프로그램을 켜지 못했어요 ('%s'): %s" % (RECONNECT_LABEL, err), "bad")
            return False, None, err
        self.no_show, self.crash = 0, None
        self.phase, self.launch_at, self.how = "launching", now, how
        self.stop_reason = self.error = None
        if self.gone_at is None:
            self.gone_at = now
        self._add("음악 프로그램을 켜요 ('%s', %s '%s')" % (
            RECONNECT_LABEL, "파일 연결로" if how == "set" else "실행 파일로", os.path.basename(set_path or "")))
        return True, how, None

    def _stop(self, reason, text, level="info", error=None):
        self.phase, self.stop_reason, self.error = "stopped", reason, error
        if text:
            self._add(text, level)

    def _finish(self, now, confirmed):
        """confirmed: the set opened (took = start to set); else only the
        process is known to be up (took = start to process)."""
        self.phase = "idle"
        self.done_at, self.done_confirmed = now, confirmed
        self.done_took = now - self.launch_at if confirmed else self.up_at - self.launch_at
        if confirmed:
            self._add("음악 프로그램이 다시 켜지고 세트가 열렸어요 (%d초 걸림)" % round(self.done_took), "ok")
        else:
            self._add("음악 프로그램이 다시 켜졌어요 (뜨는 데 %d초). 세트가 열렸는지는 몰라요 - 소리가 나는지, 복구 창이 "
                      "떠 있지 않은지 봐 주세요" % round(self.done_took), "ok")

    def view(self, now):
        def age(t):
            return None if t is None else round(max(0.0, now - t), 1)
        active = self.phase != "idle"
        return {
            "phase": self.phase, "busy": self.busy(), "stop_reason": self.stop_reason, "error": self.error,
            "enabled": self.enabled,
            "gone_age": age(self.gone_at) if active else None,
            "launch_age": age(self.launch_at) if self.phase in ("launching", "loading") else None,
            "up_age": age(self.up_at) if self.phase == "loading" else None,
            "starts_window": sum(1 for t in self.starts if now - t <= LIVE_RESTART_WINDOW_S),
            "last_start_age": age(self.starts[-1]) if self.starts else None,
            "how": self.how,
            "done_age": age(self.done_at), "done_confirmed": self.done_confirmed,
            "done_took": None if self.done_took is None else round(self.done_took),
            "crash": None if self.crash is None else {k: self.crash.get(k) for k in ("id", "kind", "module", "code", "time")},
        }


# ================================================================ MIDI thread

def find_port(names, needle):
    """(index, name, number of matches) of the first port whose name contains needle."""
    low = needle.lower()
    matches = [(i, n) for i, n in enumerate(names) if low in n.lower()]
    if not matches:
        return None, None, 0
    return matches[0][0], matches[0][1], len(matches)


class SendJob(object):
    """Runs on the MIDI thread: writes one message, unless the button's
    request gave up waiting first (then nothing is sent)."""

    def __init__(self, msg):
        self.msg = list(msg)
        self.done = threading.Event()
        self.lock = threading.Lock()
        self.cancelled = False
        self.sent = False

    def __call__(self, worker):
        # done is set under the lock: a waiter that timed out either cancels
        # before the send or finds it done, never in between
        with self.lock:
            try:
                if not self.cancelled and worker.mo is not None:
                    self.sent = worker.send(self.msg)
            finally:
                self.done.set()

    def wait(self, timeout):
        """True if it was sent; after a timeout it is cancelled and never sent."""
        if not self.done.wait(timeout):
            with self.lock:
                if not self.done.is_set():
                    self.cancelled = True
                    return False
        return self.sent


class MidiWorker(threading.Thread):
    """Owns the rtmidi ports: reads, writes the status request, follows the
    port going and coming back, and writes the buttons' messages (jobs).

    An input whose USB device vanished is never closed: on Windows closing
    it crashes the process (tools/midi_monitor.py, measured 2026-09-19 on a
    FlowLocal replug). Vanished ports stay in self.vanished and the process
    leaves through os._exit (main). Outputs are treated the same way.

    A replug or a FlowLocal restart can come and go between two port-list
    reads (a USB replug takes about 1 s), and the old handles then hear
    nothing again. So when FlowLocal's replies had been coming on these
    handles and stop for stale_reply_s, or a send fails, while the port is
    still listed, the handles are set aside the same way and the port is
    opened again: at most STALE_TRIES times until a reply comes, since a
    FlowLocal that is really stuck leaves one pair of handles open per try.
    FlowLocal acking its restart does this once, reopen_s after the ack."""

    port_check_s = PORT_CHECK_S
    stale_reply_s = STALE_REPLY_S
    reopen_s = FL_RESTART_REOPEN_S

    def __init__(self, tracker, port_name=PORT_NAME, poll_s=POLL_S):
        super().__init__(name="pf-midi", daemon=True)
        self.tracker = tracker
        self.port_name = port_name
        self.poll_s = poll_s
        self.stop_evt = threading.Event()
        self.jobs = queue.Queue()
        self.vanished = []
        self.mi = self.mo = None
        self.next_open = 0.0
        self.send_error_noted = False
        self.arrival = ArrivalClock()
        self.opened_at = None             # the current input handle's open time
        self.reply_at = None              # FlowLocal's last reply or ack on it
        self.send_failed = False
        self.stale_tries = 0              # reopens since FlowLocal's last reply
        self.gone_at = None               # the port last left the list
        self.restart_seen = None          # the restart ack already acted on

    def run(self):
        try:
            self._run()
        except Exception:
            self.tracker.events.add("error", "MIDI 스레드가 멈췄어요: %s" % traceback.format_exc(limit=4).replace("\n", " | "), "bad")

    def _run(self):
        probe_in, probe_out = rtmidi.MidiIn(), rtmidi.MidiOut()
        next_check = next_request = 0.0
        while not self.stop_evt.is_set():
            got = False
            if self.mi is not None:
                for _ in range(1000):
                    try:
                        item = self.mi.get_message()
                    except Exception:
                        item = None
                    if item is None:
                        break
                    got = True
                    read_at = time.monotonic()
                    msg = item[0]
                    if msg and msg[0] == 0xF0 and pf_command(msg) in (CMD_STATUS_REPLY, CMD_ACK):
                        self.reply_at = read_at
                        self.stale_tries = 0
                    t = self.arrival.feed(item[1], read_at)
                    self.tracker.on_midi(msg, read_at, t)
                if got and not self.arrival.trusted:
                    self.tracker.on_arrival_source("read")
            now = time.monotonic()
            if now >= next_check:
                next_check = now + self.port_check_s
                self._check_ports(probe_in, probe_out, now)
                if self._stale(now):
                    self._check_ports(probe_in, probe_out, now)
            if self.mi is not None and self.mo is not None and now >= next_request:
                next_request = now + self.poll_s
                if self.send(STATUS_REQUEST):
                    self.tracker.on_request_sent(now)
            while True:
                try:
                    job = self.jobs.get_nowait()
                except queue.Empty:
                    break
                job(self)
            if not got:
                self.stop_evt.wait(0.01)
        if not self.vanished:
            for p in (self.mi, self.mo):
                if p is not None:
                    try:
                        p.close_port()
                    except Exception:
                        pass

    def _check_ports(self, probe_in, probe_out, now):
        try:
            ins, outs = probe_in.get_ports(), probe_out.get_ports()
        except Exception:
            ins, outs = [], []
        idx_in, name_in, n_in = find_port(ins, self.port_name)
        idx_out, name_out, _n = find_port(outs, self.port_name)
        if idx_in is None:
            if self.mi is not None or self.mo is not None:
                self.gone_at = now
            self._set_aside()
            self.stale_tries = 0
        elif idx_out is None and self.mo is not None:
            self.vanished.append(self.mo)
            self.mo = None
        self.tracker.on_ports(idx_in is not None, name_in, name_out, n_in, now)
        if idx_in is None or (self.mi is not None and self.mo is not None) or now < self.next_open:
            return
        errors = []
        if self.mi is None:
            mi = rtmidi.MidiIn()
            try:
                mi.ignore_types(sysex=False, timing=True, active_sense=True)
                mi.open_port(idx_in)
                self.mi = mi
                self.opened_at, self.reply_at = now, None
                self.arrival.reset()
                if self.arrival.trusted:
                    self.tracker.on_arrival_source("rtmidi")
            except Exception as e:
                errors.append("입력 %s" % e)
                del mi
        if self.mo is None and idx_out is not None:
            mo = rtmidi.MidiOut()
            try:
                mo.open_port(idx_out)
                self.mo = mo
                self.send_failed = False
            except Exception as e:
                errors.append("출력 %s" % e)
                del mo
        elif idx_out is None:
            errors.append("출력 포트가 목록에 없음")
        if errors:
            self.next_open = now + OPEN_RETRY_S
        self.tracker.on_opened(self.mi is not None, self.mo is not None, "; ".join(errors) or None, now)

    def _set_aside(self):
        """Stops using the handles without closing them (see the class comment)."""
        for attr in ("mi", "mo"):
            p = getattr(self, attr)
            if p is not None:
                self.vanished.append(p)
                setattr(self, attr, None)
        self.reply_at = None

    def _stale(self, now):
        """Sets the handles aside if they look dead while the port is listed;
        True when it did (the caller opens the port again)."""
        if self.mi is None or self.mo is None:
            return False
        why = None
        ack_at = self.tracker.fl_restart_ack_at
        if ack_at is not None and ack_at != self.restart_seen and now - ack_at >= self.reopen_s:
            self.restart_seen = ack_at
            if self.gone_at is None or self.gone_at < ack_at:
                # the port never left the list: FlowLocal came back between two reads
                why, self.stale_tries = "작은 보드가 다시 켜져서", 0
        if why is None and self.stale_tries < STALE_TRIES:
            # after a reopen the new handles must answer too, else it tries again
            ref = self.reply_at if self.reply_at is not None else (self.opened_at if self.stale_tries else None)
            if ref is not None and now - ref > self.stale_reply_s:
                why = "상태 답이 %d초째 없어요" % int(now - ref)
            elif self.send_failed:
                why = "보내기가 실패했어요"
        if why is None:
            return False
        self.stale_tries += 1
        self._set_aside()
        self.next_open = 0.0
        self.tracker.on_reopen(why, now)
        return True

    def send(self, msg):
        try:
            self.mo.send_message(msg)
            self.send_error_noted = False
            self.send_failed = False
            return True
        except Exception as e:
            self.send_failed = True
            if not self.send_error_noted:
                self.send_error_noted = True
                self.tracker.events.add("error", "FlowLocal로 보내지 못했어요: %s" % e, "warn")
            return False

    def request(self, job):
        self.jobs.put(job)

    def stop(self):
        self.stop_evt.set()


# ================================================================ config

CONFIG_DEFAULTS = {
    "contacts": [{"role": "담당자", "name": "", "phone": ""}, {"role": "보조", "name": "", "phone": ""}],
    "panel_power_place": "",
    "check_phone": "",
    "live_recovery_button": "",
    "live_autorestart": True,
    "live_set": DEFAULT_LIVE_SET,
}


class Config(object):
    """config.json, re-read when it changes (edit in Notepad, the page follows).
    The page changes one key only (the Live auto-restart switch, set_value)."""

    def __init__(self, path=CONFIG):
        self.path = path
        self.mtime = None
        self.data = dict(CONFIG_DEFAULTS)
        self.error = None
        self.lock = threading.Lock()

    def get(self):
        with self.lock:
            try:
                mtime = os.path.getmtime(self.path)
            except OSError:
                self.error = None if self.mtime is None else "config.json을 찾을 수 없어요"
                return self.data
            if mtime != self.mtime:
                self.mtime = mtime
                try:
                    with open(self.path, encoding="utf-8-sig") as f:
                        loaded = json.load(f)
                    if not isinstance(loaded, dict):
                        raise ValueError("맨 바깥이 { } 가 아니에요")
                    data = dict(CONFIG_DEFAULTS)
                    data.update({k: v for k, v in loaded.items() if not k.startswith("_")})
                    if not isinstance(data.get("contacts"), list):
                        data["contacts"] = CONFIG_DEFAULTS["contacts"]
                    self.data, self.error = data, None
                except (OSError, ValueError) as e:
                    self.error = "config.json을 읽지 못했어요: %s" % e
            return self.data

    def set_value(self, key, value):
        """Writes one key into config.json and keeps everything else in it as
        it is (the _설명 note, the contacts). A file that is not valid JSON is
        left alone. (ok, error in Korean)."""
        with self.lock:
            try:
                with open(self.path, encoding="utf-8-sig") as f:
                    data = json.load(f, object_pairs_hook=collections.OrderedDict)
                if not isinstance(data, dict):
                    raise ValueError("맨 바깥이 { } 가 아니에요")
            except FileNotFoundError:
                data = collections.OrderedDict()
            except (OSError, ValueError) as e:
                return False, "config.json을 읽지 못해서 바꾸지 않았어요 (메모장으로 고쳐 주세요): %s" % e
            data[key] = value
            tmp = self.path + ".tmp"
            try:
                with open(tmp, "w", encoding="utf-8") as f:
                    json.dump(data, f, ensure_ascii=False, indent=2)
                    f.write("\n")
                os.replace(tmp, self.path)
            except OSError as e:
                return False, "config.json에 쓰지 못했어요: %s" % e
            self.mtime = None             # read again on the next get()
            return True, None


# ================================================================ apps

THRESHOLDS = {
    "status_stale_s": STATUS_STALE_S, "fl_no_reply_bad_s": FL_NO_REPLY_BAD_S, "fl_boot_grace_s": FL_BOOT_GRACE_S,
    "panel_join_grace_s": PANEL_JOIN_GRACE_S, "panel_reconnect_grace_s": PANEL_RECONNECT_GRACE_S,
    "heard_bad_ms": HEARD_BAD_MS, "weak_rssi_dbm": WEAK_RSSI_DBM, "jitter_warn_ms": JITTER_WARN_MS,
    "jump_warn_ms": JUMP_WARN_MS, "gaps_warn_per_min": GAPS_WARN_PER_MIN,
    "bunches_warn_per_min": BUNCHES_WARN_PER_MIN, "bunch_pair_ms": int(BUNCH_PAIR_S * 1000),
    "bunch": "같은 노브(K1–K3) 신호 2개가 %d ms 안에 (패널은 노브마다 50 ms에 1개만 보냄)" % int(BUNCH_PAIR_S * 1000),
    "wifi_settle_s": WIFI_SETTLE_S, "connect_slow_s": CONNECT_SLOW_S,
    "reconnect": [
        ["1 작은 보드 대답", "포트 없으면 USB 꽂기 요청 · 대답 없으면 %d초 기다린 뒤 RST 요청" % RC_SELF_REPAIR_S],
        ["2 작은 보드 다시 켜기", "명령 12 (답 %d초, 새로 켜짐 %d초, 핫스팟 %d초 안) · 켜진 지 %d초 안이면 건너뜀" % (
            RC_ACK_S, RC_BACK_S, RC_AP_S, RC_FRESH_BOOT_S)],
        ["3 패널", "핫스팟에 %d초 안 · 붙고 %d초 넘게 연결 없으면 명령 11 (답 %d초, 연결 %d초 안) · 안 붙으면 패널 전원 요청" % (
            RC_JOIN_S, RC_SESSION_S, RC_PANEL_ACK_S, RC_PANEL_REBOOT_S)],
        ["4 음악 프로그램", "켜져 있으면 소리 다시 맞추기 · 꺼져 있으면 전시 세트를 열고 %d초 안에 세트가 열리기를(rr) 기다림" % RC_LIVE_S],
        ["5 마지막 확인", "%d초 안에: 상태 답, 패널 연결, 패널 소식 %d초 안, 음악 프로그램" % (RC_VERIFY_S, RC_HD_FRESH_MS // 1000)],
        ["사람이 할 일", "같은 일은 한 번만 부탁 · %d분 기다림" % (RC_HUMAN_S // 60)],
    ],
    "flap_count": FLAP_COUNT, "flap_window_s": FLAP_WINDOW_S, "flap_note_s": FLAP_NOTE_S,
    "self_restart_note_s": SELF_RESTART_NOTE_S, "low_battery_pct": LOW_BATTERY_PCT,
    "live_gone_grace_s": LIVE_GONE_GRACE_S, "live_restart_max": LIVE_RESTART_MAX,
    "live_restart_window_s": LIVE_RESTART_WINDOW_S, "live_load_s": LIVE_LOAD_S,
}


def build_state(mode, snap, cfg, cfg_error, events, log_path=None, extra=None):
    fresh = status_is_fresh(snap)
    diag = diagnose(snap, cfg)
    st = snap["status"] if fresh else None
    wl = wireless_problems(snap, st) if (st is not None and st["ps"]) or old_firmware_suspected(snap) else []
    state = {
        "app": "pf-monitor",
        "mode": mode,
        "server_time": time.time(),
        "diagnosis": diag,
        "actions": action_states(snap, diag["op_button"]),
        "reconnect": reconnect_state(snap),
        "view": build_view(snap, fresh, wl),
        "snap": snap,
        "status_fresh": fresh,
        "status_keys": STATUS_KEYS,
        "reset_words": {k: v[0] for k, v in RESET_REASONS.items()},
        "thresholds": THRESHOLDS,
        "power_fix_tail": POWER_FIX_TAIL,
        "power_script": POWER_SCRIPT,
        "fix_note": buttons_note(snap),
        "live_autorestart": {"on": live_autorestart_on(cfg), "set": live_set_path(cfg),
                             "set_exists": os.path.isfile(live_set_path(cfg))},
        "events": events,
        "config": {k: cfg.get(k) for k in CONFIG_DEFAULTS},
        "config_error": cfg_error,
        "log_path": log_path,
    }
    if extra:
        state.update(extra)
    return state


def _refusal(action, reason, manual=None):
    label = ACTIONS[action]["label"]
    text = "보내지 않았어요: %s." % reason
    if manual:
        text += " 대신 손으로: " + manual
    return {"ok": False, "result": "refused", "message": text, "label": label}


class Supervisor(threading.Thread):
    """System checks (Live every 5 s, every 2 s while it is gone or being
    started again; charger every 5 s; powercfg every 60 s), the Live keeper,
    stale-reply notes, button timeouts, logs/state.json, and one log line
    whenever the diagnosis (its code or level) changes."""

    def __init__(self, tracker, config):
        super().__init__(name="pf-supervisor", daemon=True)
        self.tracker = tracker
        self.config = config
        self.stop_evt = threading.Event()
        self.last_code = None

    def run(self):
        tr = self.tracker
        next_live = next_power = next_settings = 0.0
        while True:
            try:
                now = tr.clock()
                cfg = self.config.get()
                if now >= next_live:
                    running, names, exe = check_live()
                    t = tr.clock()
                    tr.on_live(running, names, t, exe)
                    tr.keep_live(t, cfg)
                    next_live = now + (LIVE_CHECK_FAST_S if tr.live_needs_watch() else 5.0)
                if now >= next_power:
                    tr.on_power(*check_power(), tr.clock())
                    next_power = now + 5.0
                if now >= next_settings:
                    tr.on_power_settings(check_power_settings(), tr.clock())
                    next_settings = now + 60.0
                tr.check_stale(tr.clock())
                tr.memory.flush()
                err = tr.memory.take_error()
                if err:
                    tr.events.add("monitor", err, "warn")
                d = diagnose(tr.snapshot(), cfg)
                # the level too: FL_NO_REPLY going from "a moment" to red must reach the log
                # (not the headline: it carries numbers such as the battery %)
                key = (d["code"], d["level"], d["wait"])
                if key != self.last_code:
                    self.last_code = key
                    tr.events.add("diagnosis", "화면 상태: " + d["headline"], d["level"])
            except Exception:
                tr.events.add("error", "점검 스레드 오류: %s" % traceback.format_exc(limit=4).replace("\n", " | "), "warn")
            if self.stop_evt.wait(1.0):
                return

    def stop(self):
        self.stop_evt.set()


class ReconnectRunner(threading.Thread):
    """Moves the one-button run on (Tracker.reconnect_step) four times a
    second while it runs, and carries out what it asks for outside the
    tracker's lock: a message through the MIDI thread (one SendJob, reported
    back), or Live started through the keeper. Idle, it only looks once a
    second (nothing to do: step() returns no effects)."""

    def __init__(self, tracker, config, midi):
        super().__init__(name="pf-reconnect", daemon=True)
        self.tracker = tracker
        self.config = config
        self.midi = midi
        self.stop_evt = threading.Event()
        self.wake_evt = threading.Event()

    def run(self):
        tr = self.tracker
        while not self.stop_evt.is_set():
            try:
                cfg = self.config.get()
                for effect in tr.reconnect_step(tr.clock(), cfg):
                    self.carry_out(effect, cfg)
            except Exception:
                tr.events.add("error", "한 번에 다시 연결하기 오류: %s" % traceback.format_exc(limit=4).replace("\n", " | "), "warn")
            self.wake_evt.wait(0.25 if tr.reconnect_running() else 1.0)
            self.wake_evt.clear()

    def carry_out(self, effect, cfg):
        tr = self.tracker
        if effect[0] == "send":
            ok = False
            if self.midi is not None:
                job = SendJob(effect[2])
                self.midi.request(job)
                ok = job.wait(1.0)
            tr.reconnect_result(effect[1], ok, tr.clock())
        elif effect[0] == "launch":
            tr.reconnect_launch(tr.clock(), cfg)

    def wake(self):
        self.wake_evt.set()

    def stop(self):
        self.stop_evt.set()
        self.wake_evt.set()


class LiveApp(object):
    mode = "live"

    def __init__(self, port_name, log_dir, config, url="http://%s:%d/" % (HTTP_HOST, HTTP_PORT)):
        self.events = EventLog(log_dir)
        self.config = config
        self.url = url
        self.memory = open_memory(log_dir)
        self.keeper = LiveKeeper(self.events, launch_live, probe_live_crash, windows_shutting_down)
        self.tracker = Tracker(self.events, memory=self.memory, keeper=self.keeper)
        self.midi = MidiWorker(self.tracker, port_name) if rtmidi is not None else None
        self.supervisor = Supervisor(self.tracker, config)
        self.runner = ReconnectRunner(self.tracker, config, self.midi)
        self.fix_lock = threading.Lock()

    def start(self):
        self.events.add("monitor", "모니터를 켰어요 (%s)" % self.url)
        err = self.memory.take_error()
        if err:
            self.events.add("monitor", err, "warn")
        mem = self.memory.view()
        if mem["seen"]:
            self.events.add("monitor", "기억: 이 노트북의 FlowLocal은 새 펌웨어(%s)로 대답한 적이 있어요 (마지막 %s, %s)" % (
                mem["fw"] or "?", mem["seen_local"] or "?", "logs의 기록에서 찾음" if mem["source"] == "logs" else "state.json"))
        cfg = self.config.get()
        self.events.add("live", "음악 프로그램 자동 다시 켜기: %s (세트: %s)" % (
            "켜짐" if live_autorestart_on(cfg) else "꺼짐", live_set_path(cfg)))
        if self.midi is None:
            self.events.add("monitor", "python-rtmidi가 없어서 MIDI를 읽지 못해요", "warn")
        else:
            self.midi.start()
        self.supervisor.start()
        self.runner.start()

    def stop(self):
        self.supervisor.stop()
        self.runner.stop()
        self.memory.flush(force=True)
        if self.midi is not None:
            self.midi.stop()
            self.midi.join(2.0)
            if self.midi.vanished:
                return True
        return False

    def set_live_autorestart(self, on, query=None):
        """The operator's switch (운영자용 칸): config.json "live_autorestart"."""
        ok, err = self.config.set_value("live_autorestart", bool(on))
        if not ok:
            self.events.add("live", err, "warn")
            return {"ok": False, "message": err}
        self.events.add("live", "음악 프로그램 자동 다시 켜기를 %s (운영자용 칸)" % ("켰어요" if on else "껐어요"),
                        "info" if on else "warn")
        return {"ok": True, "on": bool(on),
                "message": "자동 다시 켜기를 %s." % ("켰어요" if on else "껐어요. 이제 음악 프로그램을 닫아도 다시 켜지 않아요")}

    def state(self, query):
        cfg = self.config.get()
        return build_state(self.mode, self.tracker.snapshot(), cfg, self.config.error, self.events.recent(40),
                           self.events.today_path())

    def fix(self, action, query=None):
        """One press of a dashboard button: at most one message to FlowLocal.
        Returns at once; the ack and the recovery show up in the state."""
        tr = self.tracker
        with self.fix_lock:
            now = tr.clock()
            s = action_state(tr.snapshot(now), action)
            if not s["enabled"]:
                res = _refusal(action, s["reason"], s["manual"] if s["needs_fw"] else None)
                tr.events.add("button", "'%s' 버튼: %s" % (s["label"], res["message"]), "warn")
                return res
            msg, target = tr.action_message(action)
            if msg is None:
                res = _refusal(action, target)
                tr.events.add("button", "'%s' 버튼: %s" % (s["label"], res["message"]), "warn")
                return res
            refused = tr.fix_begin(action, now, msg, target)
            if refused is not None:
                return {"ok": False, "result": refused[0], "message": refused[1]}
            if self.midi is None:
                tr.fix_sent(False, tr.clock())
                return {"ok": False, "result": "send_failed", "message": "보내지 못했어요."}
            job = SendJob(msg)
            self.midi.request(job)
            sent = job.wait(1.0)
            tr.fix_sent(sent, tr.clock())
            snap = tr.snapshot()
            return {"ok": sent, "result": "sent" if sent else "send_failed",
                    "message": "보냈어요." if sent else "보내지 못했어요. 잠깐 뒤에 다시 눌러 주세요.",
                    "fix": snap["fix"]}

    def reconnect(self, query=None):
        """The one big button: starts the run (ReconnectRunner carries it out)."""
        tr = self.tracker
        with self.fix_lock:
            now = tr.clock()
            s = reconnect_state(tr.snapshot(now))
            if not s["enabled"]:
                tr.events.add("reconnect", "'%s' 버튼: 시작하지 않았어요 (%s)" % (RECONNECT_LABEL, s["reason"]), "warn")
                return {"ok": False, "result": "refused", "message": "시작하지 않았어요: %s." % s["reason"]}
            ok, why = tr.reconnect_start(now)
            if not ok:
                return {"ok": False, "result": "refused", "message": "시작하지 않았어요: %s." % why}
            self.runner.wake()
            return {"ok": True, "result": "started", "message": "시작했어요. 단계마다 이 화면이 알려 줘요."}

    def reconnect_cancel(self, query=None):
        tr = self.tracker
        if tr.reconnect_cancel(tr.clock()):
            return {"ok": True, "message": "그만했어요."}
        return {"ok": False, "message": "하고 있는 것이 없어요."}


# ---------------------------------------------------------------- demo

class FakeClock(object):
    def __init__(self, t=0.0):
        self.t = t

    def __call__(self):
        return self.t


T_END = 100000.0
DEMO_OK_FIELDS = collections.OrderedDict([
    ("v", 1), ("fw", "exhibit-mon1"), ("up", 0), ("ap", 1), ("ch", 1), ("chm", "auto"), ("cl", 2), ("cm", 3),
    ("ps", 1), ("pip", "192.168.66.2"), ("prs", -58), ("hd", 800), ("ra", 1200), ("lp", 3), ("kn", [64, 80, 40]),
    ("jt", 3), ("jm", 18),
    ("rx", 5234), ("tx", 5230), ("dr", 0), ("se", 2), ("to", 0), ("rj", 0), ("gp", 3), ("rr", 4),
    ("ud", 0), ("sr", 2700), ("hi", 182344),
])
# what the fields of a FlowLocal that has just booted look like
FRESH_BOOT = dict(ap=0, ch="", cl=0, ps=0, pip="", prs="", hd=-1, ra=-1, lp=-1, kn=[None] * 3, jt=-1, jm=-1,
                  rx=0, tx=0, dr=0, se=0, to=0, rj=0, gp=0, rr=0, ud=0, sr=1)
NO_PANEL = dict(ps=0, pip="", prs="", hd=-1, ra=-1, jt=-1, jm=-1)


def demo_power_items(bad=()):
    """Power settings for tests and --demo: all right except `bad` - keys
    (then wrong on the charger and on battery) or {key: (ac, dc)}."""
    values = dict(bad) if isinstance(bad, dict) else {}
    items = []
    for check in POWER_CHECKS:
        if check[0] in values:
            acdc = values[check[0]]
        elif check[0] in bad:
            v = 1800 if check[4] == "timeout" else 1
            acdc = (v, v)
        else:
            acdc = (0, 0)
        items.append(power_item(check, acdc))
    return items


# this laptop on 2026-09-25 21:40 (powercfg): hibernate after 5 h on battery,
# the lid sleeps, PCI Express link state power management at maximum savings
LAPTOP_20260925_POWER = {"hibernate": (0, 18000), "lid": (1, 1), "aspm": (2, 2)}
DEMO_LIVE_EXE = r"C:\ProgramData\Ableton\Live 12 Suite\Program\Ableton Live 12 Suite.exe"
# the Application Error record of Live's crash at 2026-09-25 16:17
DEMO_LIVE_CRASH = {"id": 1000, "kind": "crash", "module": "rthdasio64.dll", "code": "c0000005",
                   "time": "2026-09-25T07:17:43.1624750Z", "report": "dd12d02c-97ad-4381-a179-a080418bf191"}


class Script(object):
    """Helpers for the demo scenarios: `at(s)` moves the fake clock to s
    seconds before the scenario's end, the rest feed the tracker like the
    threads do. flow() then plays a button press from that end onwards."""

    def __init__(self, tr, clk, end=T_END):
        self.tr, self.clk, self.end = tr, clk, end
        self.boot = end - 5400.0
        self.last_over = {}

    def at(self, s_before_end):
        self.clk.t = self.end - s_before_end

    def system(self, live=True, ac=True, battery=87, bad_settings=()):
        self.tr.on_live(live, ["Ableton Live 12 Suite.exe"] if live else [], self.clk.t, DEMO_LIVE_EXE if live else None)
        self.tr.on_power(ac, battery, True, self.clk.t)
        self.tr.on_power_settings(demo_power_items(bad_settings), self.clk.t)

    def live(self, running, enabled=True):
        """A tasklist check and the Live keeper's step after it, as the Supervisor does."""
        self.tr.on_live(running, ["Ableton Live 12 Suite.exe"] if running else [], self.clk.t,
                        DEMO_LIVE_EXE if running else None)
        self.tr.keep_live(self.clk.t, {"live_autorestart": enabled, "live_set": DEFAULT_LIVE_SET})

    def lives(self, start, end, running, step=2.0, enabled=True):
        s = start
        while s >= end:
            self.at(s)
            self.live(running, enabled)
            s -= step

    def remember(self, fw, s_before_end):
        """logs/state.json from an earlier monitor run: FlowLocal answered then."""
        mem = self.tr.memory
        mem.remember(fw, mem.wall() - (self.clk.t - (self.end - s_before_end)))
        mem.source = "file"

    def port(self, present=True, open_ok=True):
        self.tr.on_ports(present, "Patternflow Exhibit 0" if present else None,
                         "Patternflow Exhibit 1" if present else None, 1 if present else 0, self.clk.t)
        if present:
            self.tr.on_opened(open_ok, open_ok, None if open_ok else
                              "입력 MidiInWinMM::openPort: error creating Windows MM MIDI input port.", self.clk.t)

    def reply(self, **over):
        f = collections.OrderedDict(DEMO_OK_FIELDS)
        t = self.clk.t
        f["up"] = int(t - self.boot)
        # unless given: FlowLocal's last panel MIDI is the tracker's, and the
        # panel's AppleMIDI sync comes every 10 s
        ra = -1 if self.tr.midi_at is None else int((t - self.tr.midi_at) * 1000)
        f["ra"] = ra
        f["hd"] = min(ra if ra >= 0 else 10 ** 9, int((t % 10.0) * 1000))
        for k, v in over.items():
            f[k] = v(self.clk.t) if callable(v) else v
        self.last_over = dict(over)
        self.tr.on_request_sent(self.clk.t)
        self.tr.on_midi(build_status_reply(f), self.clk.t)

    def replies(self, start, end, step=20.0, **over):
        s = start
        while s >= end:
            self.at(s)
            self.reply(**over)
            s -= step
        self.at(end)
        self.reply(**over)

    def requests(self, start, end, step=2.0):
        s = start
        while s >= end:
            self.at(s)
            self.tr.on_request_sent(self.clk.t)
            s -= step

    def midi(self, *msgs):
        for m in msgs:
            self.tr.on_midi(list(m), self.clk.t)

    def panel_hello(self, pattern=3, knobs=(64, 80, 40)):
        self.midi((0xC0, pattern), (0xB0, 24, knobs[0]), (0xB0, 25, knobs[1]), (0xB0, 26, knobs[2]))

    def flow(self, action, outcome, elapsed, cancel_at=None):
        """The button pressed at the scenario's end and `elapsed` s of what
        follows: outcome "ok" (the panel comes back), "fail" (it does not),
        "no_ack" (FlowLocal takes status requests but not commands) or
        "refused" (FlowLocal says it cannot). The one big button plays out
        against DemoWorld (its outcomes there)."""
        if action == RECONNECT_ID:
            DemoWorld(self, outcome).play(elapsed, cancel_at)
            return
        tr, t0 = self.tr, self.end
        fin = t0 + elapsed

        def go(dt):
            if t0 + dt > fin + 1e-9:
                return False
            self.clk.t = t0 + dt
            return True

        go(0.0)
        if tr.fix_begin(action, self.clk.t) is not None:
            return
        go(0.01)
        tr.fix_sent(True, self.clk.t)
        problem = dict(self.last_over)
        healthy = {"se": 3}
        answering = tr.status_ever_port and tr.status_at is not None and t0 - tr.status_at <= STATUS_STALE_S

        def still():
            # the problem goes on: the same replies, or none if FlowLocal was not answering
            if answering:
                self.reply(**problem)
            else:
                tr.on_request_sent(self.clk.t)

        def each(fn, first=1.0):
            dt = first
            while go(dt):
                fn(dt)
                dt += 2.0

        if action == "resync":
            if go(0.02):
                n = tr.pc if tr.pc is not None else 1
                self.midi((0xC0, n), (0xBF, 118, n), (0xB0, 24, 64), (0xB0, 25, 64), (0xB0, 26, 64))
            each(lambda dt: still(), 2.0)
            return
        cmd = ACTIONS[action]["cmd"]
        target = (tr.fix or {}).get("target")
        # FlowLocal's own ack texts (fl_midi_bridge.h; command 13's are not known yet)
        said = {"reconnect": ("ended 1 RTP session, the panel invites again", "no RTP session to end"),
                "reboot_panel": ("panel 192.168.66.2 is rebooting", "no connection to panel 192.168.66.2"),
                "reboot_flowlocal": ("restarting: USB back in seconds, hotspot in about 13 s",
                                     "too soon: once per 3 s, wait 2 s"),
                "channel_next": ("channel %s saved, restarting" % target, "too soon: once per 3 s, wait 2 s"),
                "channel_auto": ("automatic channel saved, restarting", "too soon: once per 3 s, wait 2 s")}[action]
        if outcome in ("ok", "fail", "refused") and go(1.2 if action == "reboot_panel" else 0.05):
            tr.on_midi(build_ack(cmd, outcome != "refused", said[outcome == "refused"]), self.clk.t)
        if outcome in ("no_ack", "refused"):
            each(lambda dt: still())
            return
        if action in RESTART_ACTIONS:
            # what stays across the restart: the firmware, and the channel the button chose
            keep = {"ch": problem.get("ch", 1)}
            if "fw" in problem:
                keep["fw"] = problem["fw"]
            if action == "channel_next":
                keep.update(ch=target or 6, chm="manual", cs="")
            elif action == "channel_auto":
                keep.update(ch=11, chm="auto", cs="1:240,6:310,11:45")
            elif "cs" in problem:
                keep.update(chm=problem.get("chm", "auto"), cs=problem["cs"])
            booting = dict(FRESH_BOOT, **{k: v for k, v in keep.items() if k != "ch"})
            if go(0.3):
                tr.on_ports(False, None, None, 0, self.clk.t)
            if go(2.5):
                self.port()
            self.boot = t0 + 0.4

            def booted(dt):
                up = dt - 0.4
                if up < 12:
                    self.reply(**booting)
                elif up < 14 or outcome == "fail":
                    self.reply(**dict(booting, ap=1, ch=keep["ch"]))
                elif up < 18:
                    self.reply(**dict(booting, ap=1, ch=keep["ch"], cl=1))
                else:
                    if up - 2 < 18:
                        self.panel_hello(tr.pc if tr.pc is not None else 1)
                    self.reply(se=1, rx=40, tx=40, gp=0, rr=0, sr=int(up / 2), jm=40, **keep)
            each(booted, 3.0)
            return
        # reconnect: FlowLocal ends the session, the panel (still on the hotspot)
        # invites again; reboot_panel: the panel leaves the hotspot, rejoins,
        # then invites. "fail": the panel does not come back.
        ended = dict(problem, ps=0, cl=1, pip="", prs="", hd=-1, jt=-1, jm=-1)
        plan = {
            "reconnect": ((5.0, ended),),
            "reboot_panel": ((8.0, dict(ended, cl=0)), (12.0, ended)),
        }[action]
        back_at = plan[-1][0]

        def after(dt):
            if outcome == "fail":
                self.reply(**(ended if action == "reconnect" else problem))
            elif dt < back_at:
                self.reply(**next(f for t, f in plan if dt < t))
            else:
                if dt - 2.0 < back_at:
                    self.panel_hello(1 if action == "reboot_panel" else (tr.pc if tr.pc is not None else 1),
                                     (64, 64, 64) if action == "reboot_panel" else (64, 80, 40))
                self.reply(**healthy)
        each(after)


DEMO_HUMAN_S = 7.0            # the demo's person does the asked step this long after it shows


class DemoWorld(object):
    """What FlowLocal, the panel, a phone and Live do in the demo (and the
    tests) while the one big button runs: the run's effects are carried out
    on this pretend world, which answers the way the hardware was measured
    to (FlowLocal's restart: USB back after 1.8 s, the hotspot 13.3 s after
    its start, the panel on it 1.5 s later and its session 0.8 s after that;
    the panel's restart: on the hotspot again after 6.5 s, the session 1.3 s
    later). A person does an asked step DEMO_HUMAN_S after it shows.

    outcome:
      ok            everything answers (the happy path, ~18 s)
      rst           FlowLocal is silent; asked for its RST button, the person presses it
      self_repair   FlowLocal is silent for 17 s, then repairs its USB by itself
      plug          FlowLocal is off the laptop; asked for the cable
      panel_reboot  the panel joins the hotspot but opens no session until command 11
      panel_off     the panel never joins; asked to switch it off and on
      phones        the panel and a phone on the hotspot, no session: phones off, then command 11
      live_off      Live is not running: the run starts the set
      panel_dead    the panel never joins, not even after the person's step: the run gives up"""

    PANEL_IP = "192.168.66.2"

    def __init__(self, script, outcome):
        s = self.s = script
        self.tr = s.tr
        self.outcome = outcome
        self.t0 = s.end
        self.q = []
        self.n = 0
        self.present = outcome != "plug"
        self.answering = outcome not in ("rst", "self_repair")
        self.boot = s.boot                        # FlowLocal's start (the script's clock)
        self.ap_at = self.boot + 13.3
        # the panel: "on" joins and invites, "stuck" joins but invites only after a restart,
        # "off" never joins until a person switches it on, "dead" never
        self.panel = {"panel_reboot": "stuck", "phones": "stuck", "panel_off": "off", "panel_dead": "dead"}.get(outcome, "on")
        on_hotspot = self.panel in ("on", "stuck") and outcome not in ("plug",)
        self.joined = on_hotspot
        self.session = self.panel == "on" and outcome not in ("plug",)
        self.phone = outcome == "phones"
        self.live = bool(self.tr.live)
        self.se, self.rr = 2, 4
        self.pc = self.tr.pc if self.tr.pc is not None else 3
        self.human_at = None
        self.changed = False                      # until FlowLocal restarts, it answers as in the scenario
        self.next_reply = self.t0 + 0.5
        self.next_live = self.t0 + 1.0
        self.cfg = {"live_autorestart": True, "live_set": DEFAULT_LIVE_SET}
        if outcome == "self_repair":
            self.later(17.0, self._usb_repaired)

    # ------------------------------------------------ the world's clock

    def later(self, dt, fn, at=None):
        self.n += 1
        self.q.append(((at if at is not None else self.s.clk.t) + dt, self.n, fn))
        self.q.sort()

    def play(self, elapsed, cancel_at=None):
        tr, clk = self.tr, self.s.clk
        clk.t = self.t0
        if not tr.reconnect_start(self.t0)[0]:
            return
        t, fin = self.t0, self.t0 + elapsed
        while t <= fin + 1e-9:
            clk.t = t
            while self.q and self.q[0][0] <= t + 1e-9:
                _t, _n, fn = self.q.pop(0)
                fn()
            if t >= self.next_reply:
                self.next_reply += 2.0
                self._reply()
            if t >= self.next_live:
                self.next_live += 2.0
                self.s.live(self.live)
            self._person(t)
            if cancel_at is not None and t >= self.t0 + cancel_at:
                tr.reconnect_cancel(t)
                cancel_at = None
            for effect in tr.reconnect_step(t, self.cfg):
                self._effect(effect)
            t = round(t + 0.25, 6)
        clk.t = fin
        if cancel_at is not None:
            tr.reconnect_cancel(fin)              # pressed within the last step

    # ------------------------------------------------ FlowLocal, the panel, Live

    def _reply(self):
        s, t = self.s, self.s.clk.t
        tr = self.tr
        if not (self.present and self.answering and tr.port_present and tr.in_open):
            tr.on_request_sent(t)
            return
        s.boot = self.boot
        if not self.changed:
            s.reply(**s.last_over)                # as before the press
            return
        ap = t >= self.ap_at
        cl = (1 if ap and self.joined else 0) + (1 if ap and self.phone else 0)
        sess = ap and self.session
        # with a session, ra= / hd= follow the panel's last MIDI and its 10 s sync (Script.reply)
        quiet = {} if sess else {"hd": -1, "ra": -1}
        s.reply(ap=1 if ap else 0, ch=6 if ap else "", cl=cl, ps=1 if sess else 0,
                pip=self.PANEL_IP if sess else "", prs=-52 if sess else "", jt=3 if sess else -1,
                jm=20 if sess else -1, se=self.se, rr=self.rr, gp=0, to=0, rj=0, lp=self.pc, **quiet)

    def _restart(self, delay_back=1.5):
        """FlowLocal starts again: off USB, back, its hotspot 13.3 s after the start."""
        tr, s = self.tr, self.s
        tr.on_ports(False, None, None, 0, s.clk.t)
        self.changed = True
        self.boot = s.clk.t + 0.1
        self.ap_at = self.boot + 13.3
        self.present = self.answering = True
        self.session = False
        self.joined = False
        self.se = 0
        self.rr = 0
        self.later(delay_back, lambda: s.port())
        if self.panel in ("on", "stuck"):
            self.later(0.0, self._panel_join, at=self.ap_at + 1.5)

    def _panel_join(self):
        self.joined = True
        if self.panel == "on":
            self.later(0.8, self._panel_session)

    def _panel_session(self):
        if not (self.joined and self.s.clk.t >= self.ap_at):
            return
        self.session = True
        self.se += 1
        self.s.panel_hello(self.pc)

    def _usb_repaired(self):
        tr, s = self.tr, self.s
        tr.on_ports(False, None, None, 0, s.clk.t)
        self.answering = True
        self.later(0.6, lambda: s.port())

    def _effect(self, effect):
        tr, s, t = self.tr, self.s, self.s.clk.t
        if effect[0] == "launch":
            tr.reconnect_launch(t, self.cfg)
            self.later(3.0, self._live_up)
            return
        key = effect[1]
        tr.reconnect_result(key, self.present and tr.port_present, t)
        if not (self.present and self.answering):
            return
        if key == "cmd12":
            self.later(0.05, lambda: tr.on_midi(build_ack(CMD_REBOOT_FLOWLOCAL, True,
                                                          "restarting: USB back in seconds, hotspot in about 13 s"),
                                                s.clk.t))
            self.later(0.3, self._restart)
        elif key == "cmd11":
            cl = (1 if self.joined else 0) + (1 if self.phone else 0)
            if cl == 1 and self.joined:
                self.later(1.0, lambda: tr.on_midi(build_ack(CMD_REBOOT_PANEL, True, "panel %s is rebooting" % self.PANEL_IP),
                                                   s.clk.t))
                self.later(2.0, self._panel_leaves)
                self.panel = "on"
                self.later(6.5, self._panel_join)
            else:
                msg = ("no panel session and %d devices on the hotspot" % cl if cl else
                       "no panel session and no device on the hotspot")
                self.later(0.05, lambda: tr.on_midi(build_ack(CMD_REBOOT_PANEL, False, msg), s.clk.t))
        elif key == "resync":
            self.rr += 1
            self.later(0.01, lambda: s.midi((0xC0, self.pc), (0xBF, 118, self.pc), (0xB0, 24, 64), (0xB0, 25, 80),
                                            (0xB0, 26, 40)))

    def _panel_leaves(self):
        self.joined = self.session = False

    def _live_up(self):
        self.live = True
        self.s.live(True)
        self.later(9.0, self._set_loaded)

    def _set_loaded(self):
        self.rr += 1                              # the Live device asks for a resync when its set has loaded

    # ------------------------------------------------ the person

    def _person(self, t):
        rc = self.tr.reconnect.run
        h = rc["human"] if rc is not None and rc["result"] is None else None
        if h is None:
            self.human_at = None
            return
        if self.human_at is None:
            self.human_at = t
        if t - self.human_at < DEMO_HUMAN_S or self.human_at < 0:
            return
        self.human_at = -1.0                      # done once
        kind = h["kind"]
        if kind == "plug":
            self.present = self.answering = self.changed = True
            self.boot = t
            self.ap_at = t + 13.3
            self.s.port()
            if self.panel in ("on", "stuck"):
                self.later(0.0, self._panel_join, at=self.ap_at + 1.5)
        elif kind in ("rst", "replug"):
            self._restart()
        elif kind == "panel_power":
            self.changed = True
            if self.panel != "dead":
                self.panel = "on"
                self.joined = self.session = False
                self.later(max(9.0, self.ap_at - t + 1.5), self._panel_join)
        elif kind == "phones":
            self.phone = False
        elif kind == "live":
            self.later(3.0, self._live_up)


def _sc_all_ok(s):
    s.at(600); s.system(); s.port(); s.panel_hello(1)
    s.replies(598, 60)
    s.at(40); s.midi((0xC0, 3), (0xBF, 118, 3))
    s.replies(38, 0)


def _sc_checking(s):
    s.at(4); s.system(); s.port(); s.requests(4, 1)
    s.at(0)


def _sc_port_missing(s):
    s.at(600); s.system(); s.tr.on_ports(False, None, None, 0, s.clk.t)
    s.at(0)


def _sc_port_reconnecting(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 6)
    s.at(3); s.tr.on_ports(False, None, None, 0, s.clk.t)
    s.at(0)


def _sc_port_busy(s):
    s.at(600); s.system(); s.port(open_ok=False)
    s.at(0)


def _sc_old_firmware(s):
    s.at(600); s.system(); s.port(); s.panel_hello(1)
    s.requests(598, 0)
    s.at(12); s.midi((0xC0, 4), (0xB0, 26, 70))
    s.at(0)


def _sc_fl_no_reply_short(s):
    s.at(600); s.system(); s.port(); s.panel_hello(5)
    s.replies(598, 12)
    s.requests(10, 0)
    s.at(0)


def _sc_fl_no_reply(s):
    s.at(600); s.system(); s.port(); s.panel_hello(5)
    s.replies(598, 50)
    s.requests(48, 0)
    s.at(0)


def _sc_fl_no_reply_acks(s):
    s.at(600); s.system(); s.port(); s.panel_hello(5)
    s.replies(598, 50)
    s.at(500); s.tr.on_midi(build_ack(CMD_RECONNECT_PANEL, True), s.clk.t)   # a button worked earlier today
    s.requests(48, 0)
    s.at(0)


def _sc_fl_starting(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 40)
    s.at(30); s.tr.on_ports(False, None, None, 0, s.clk.t)
    s.at(22); s.port(); s.boot = s.end - 24
    s.replies(20, 12, step=2.0, **FRESH_BOOT)
    s.replies(10, 0, step=2.0, **dict(FRESH_BOOT, ap=1, ch=1, cl=1))


def _sc_fl_ap_down(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 300)
    s.replies(290, 0, **dict(NO_PANEL, ap=0, cl=0))


def _sc_hotspot_full(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 300)
    s.replies(290, 0, **dict(NO_PANEL, cl=3, hd=240000, ra=240000))


def _sc_panel_not_on_hotspot(s):
    s.at(600); s.system(); s.port()
    s.replies(598, 0, **dict(NO_PANEL, cl=0, lp=-1, kn=[None] * 3))


def _sc_panel_reconnecting(s):
    s.at(600); s.system(); s.port(); s.panel_hello(4)
    s.replies(598, 22)
    s.replies(20, 0, step=2.0, **dict(NO_PANEL, cl=0, hd=20000, ra=25000))


def _sc_panel_joining(s):
    s.at(600); s.system(); s.port(); s.panel_hello(4)
    s.replies(598, 60)
    s.replies(58, 22, **dict(NO_PANEL, cl=0, hd=60000, ra=60000))
    s.replies(12, 0, step=2.0, **dict(NO_PANEL, cl=1, hd=80000, ra=80000))


def _sc_two_stations(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 120)
    s.replies(118, 0, **dict(NO_PANEL, cl=2))


def _sc_panel_no_session(s):
    s.at(600); s.system(); s.port()
    s.replies(598, 0, **dict(NO_PANEL, cl=1, lp=-1, kn=[None] * 3))


def _sc_panel_silent(s):
    s.at(600); s.system(); s.port(); s.panel_hello(6)
    s.replies(598, 60)
    s.replies(58, 0, step=2.0, hd=lambda t: int((t - (s.end - 60)) * 1000))


def _sc_panel_off(s):
    # the panel lost power 72 s ago: silent, FlowLocal timed the session out
    # 10 s ago, the dead panel stays on the station list
    s.at(600); s.system(); s.port(); s.panel_hello(6)
    s.replies(598, 80)
    off = s.end - 72
    s.replies(72, 12, step=2.0, hd=lambda t: int((t - off) * 1000))
    s.replies(10, 0, step=2.0, **dict(NO_PANEL, cl=1, to=1))


def _sc_usb_loss(s):
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 60)
    s.replies(58, 0, step=2.0, ra=lambda t: 400 + int(t * 1000) % 1500)


def _sc_live_not_running(s):
    # the operator turned the auto-restart off and closed Live
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 130)
    s.lives(120, 110, False, enabled=False)
    s.replies(108, 0)


def _sc_live_starting(s):
    s.at(40); s.system(live=False); s.port(); s.panel_hello(1)
    s.replies(38, 0)


def _live_downs(s, start, downs, reply_every=20):
    """Every 2 s from `start` s before the end: a tasklist check with the keeper's
    step - Live gone in each (gone, back] of `downs` - and a status reply every
    `reply_every` s and in the last 10 s."""
    for t in range(int(start), -1, -2):
        s.at(t)
        s.live(not any(back < t <= gone for gone, back in downs))
        if t % reply_every == 0 or t < 10:
            s.reply()


def _sc_live_restarting(s):
    # Live vanished 40 s ago (like 2026-09-25 21:44); the monitor started it 32 s ago,
    # it came up 12 s ago and is opening the set
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 42)
    _live_downs(s, 40, [(40, 12)], reply_every=10)


def _sc_live_gave_up(s):
    # three crashes, three starts in 15 minutes; the fourth time the monitor stops
    s.at(1200); s.system(); s.port(); s.panel_hello(3)
    _live_downs(s, 1198, [(700, 682), (450, 432), (200, 182), (60, -1)])


def _sc_live_restarted(s):
    # the monitor started Live again about 4 minutes ago; the set may be behind a recovery dialog
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 332)
    _live_downs(s, 330, [(330, 312)], reply_every=10)


def _sc_on_battery(s):
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 200)
    s.at(180); s.tr.on_power(False, 62, True, s.clk.t)
    s.replies(178, 0)


def _sc_on_battery_low(s):
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 200)
    s.at(180); s.tr.on_power(False, 34, True, s.clk.t)
    s.replies(178, 22)
    s.at(20); s.tr.on_power(False, 22, True, s.clk.t)
    s.replies(18, 0)


def _sc_fl_stuck(s):
    # 2026-09-25 21:12: the monitor was started again while FlowLocal (flx-mon1, answering
    # all afternoon) sat on USB with its USB side silent; its RST button brought it back
    s.remember("flx-mon1", 3500)
    s.at(420); s.system(); s.port()
    s.requests(418, 0)
    s.at(0)


def _sc_fl_flapping(s):
    # 2026-09-25 21:02-21:04: FlowLocal plugged in again, then off USB and back three times in a minute
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 80)
    drops = ((76, 75), (50, 44), (22, 21))
    for i, (gone, back) in enumerate(drops):
        s.at(gone); s.tr.on_ports(False, None, None, 0, s.clk.t)
        s.at(back); s.port(); s.boot = s.end - back + 0.5
        s.replies(back - 1, drops[i + 1][0] + 2 if i + 1 < len(drops) else 0, step=2.0, **FRESH_BOOT)


def _sc_fl_flapped(s):
    # the same, five minutes ago; it has stayed since
    s.at(600); s.system(); s.port(); s.panel_hello(2)
    s.replies(598, 420)
    for gone, back in ((400, 399), (370, 368), (340, 339)):
        s.at(gone); s.tr.on_ports(False, None, None, 0, s.clk.t)
        s.at(back); s.port(); s.boot = s.end - back + 0.5
        s.replies(back - 1, back - 5, step=2.0, **FRESH_BOOT)
    s.at(310); s.panel_hello(2)
    s.replies(308, 0, step=10.0, se=1)


def _sc_fl_self_restart(s):
    # flx-mon2: FlowLocal restarted by itself three minutes ago for want of power
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 200, rsn="poweron", bc=1, ur=0)
    s.at(190); s.tr.on_ports(False, None, None, 0, s.clk.t)
    s.at(188); s.port(); s.boot = s.end - 188.5
    s.replies(187, 170, step=2.0, **dict(FRESH_BOOT, rsn="brownout", bc=2, ur=0))
    s.at(168); s.panel_hello(3)
    s.replies(166, 0, step=10.0, rsn="brownout", bc=2, ur=0, se=1)


def _sc_fl_self_restart_crash(s):
    # flx-mon2: FlowLocal's task watchdog restarted it two minutes ago (its USB bridge hung)
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    s.replies(598, 140, rsn="poweron", bc=1, ur=0)
    s.at(130); s.tr.on_ports(False, None, None, 0, s.clk.t)
    s.at(128); s.port(); s.boot = s.end - 128.5
    s.replies(127, 110, step=2.0, **dict(FRESH_BOOT, rsn="task_wdt", bc=3, why="bridge_hung", ur=0))
    s.at(108); s.panel_hello(3)
    s.replies(106, 0, step=10.0, rsn="task_wdt", bc=3, why="bridge_hung", ur=0, se=1)


def _sc_wifi_poor(s):
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    start = s.end - 600
    s.replies(598, 0, step=10.0, ch=6, prs=-79, jt=34, jm=420, gp=lambda t: 3 + int((t - start) / 12))


def _sc_wifi_poor_mon3(s):
    # FlowLocal flx-mon3: it takes command 13, and says its boot scan's scores
    s.at(600); s.system(); s.port(); s.panel_hello(3)
    start = s.end - 600
    s.replies(598, 0, step=10.0, fw="flx-mon3", ch=6, chm="auto", cs="1:210,6:95,11:380", prs=-68, jt=31, jm=380,
              gp=lambda t: 3 + int((t - start) / 15))


def _sc_live_never_up(s):
    # Live did not come up after the laptop started (the monitor never saw it run)
    s.at(600); s.system(live=False); s.port(); s.panel_hello(3)
    s.replies(598, 0)


def _sc_power_settings(s):
    s.at(600); s.system(bad_settings=LAPTOP_20260925_POWER); s.port(); s.panel_hello(1)
    s.replies(598, 0)


def _sc_several(s):
    s.at(600); s.system(bad_settings=("usb_suspend",)); s.port(); s.panel_hello(3)
    s.replies(598, 200)
    s.at(190); s.tr.on_power(False, 41, True, s.clk.t)
    s.at(160); s.tr.on_live(False, [], s.clk.t)
    s.replies(158, 0, prs=-79)


def _sc_no_midi_lib(s):
    s.tr.midi_lib = False
    s.at(600); s.system()
    s.at(0)


# key, Korean label, expected diagnosis code, script, knob animation
# (False / True / "bunch"), monitor uptime (s), button press (action, outcome, s since)
SCENARIOS = collections.OrderedDict((row[0], row[1:]) for row in (
    ("all_ok", "모두 정상", "ALL_OK", _sc_all_ok, True, 600, None),
    ("checking", "모니터 막 켬 (확인 중)", "CHECKING", _sc_checking, False, 4, None),
    ("port_missing", "FlowLocal 보드 안 보임", "PORT_MISSING", _sc_port_missing, False, 600, None),
    ("port_reconnecting", "FlowLocal 방금 빠짐", "PORT_RECONNECTING", _sc_port_reconnecting, False, 600, None),
    ("port_busy", "모니터가 포트를 못 엶", "PORT_BUSY", _sc_port_busy, False, 600, None),
    ("old_firmware", "예전 FlowLocal 펌웨어", "FL_OLD_FIRMWARE", _sc_old_firmware, True, 600, None),
    ("fl_no_reply_short", "FlowLocal 잠깐 무응답", "FL_NO_REPLY", _sc_fl_no_reply_short, False, 600, None),
    ("fl_no_reply", "FlowLocal 대답 멈춤", "FL_NO_REPLY", _sc_fl_no_reply, False, 600, None),
    ("fl_no_reply_acks", "FlowLocal 대답 멈춤 (버튼이 되던 보드)", "FL_NO_REPLY", _sc_fl_no_reply_acks, False, 600, None),
    ("fl_stuck", "FlowLocal 멈춤 (모니터를 다시 켠 뒤, 9/25 21:12)", "FL_NO_REPLY", _sc_fl_stuck, False, 420, None),
    ("fl_flapping", "FlowLocal 계속 다시 켜짐 (9/25 21:03)", "FL_FLAPPING", _sc_fl_flapping, False, 600, None),
    ("fl_flapped", "FlowLocal 조금 전 여러 번 다시 켜짐", "FL_FLAPPING", _sc_fl_flapped, True, 600, None),
    ("fl_self_restart", "FlowLocal 스스로 다시 켜짐 (전원 부족)", "FL_SELF_RESTART", _sc_fl_self_restart, True, 600, None),
    ("fl_self_restart_crash", "FlowLocal 스스로 다시 켜짐 (펌웨어 멈춤)", "FL_SELF_RESTART", _sc_fl_self_restart_crash,
     True, 600, None),
    ("fl_starting", "FlowLocal 켜지는 중", "FL_STARTING", _sc_fl_starting, False, 600, None),
    ("fl_ap_down", "FlowLocal 핫스팟 꺼짐", "FL_AP_DOWN", _sc_fl_ap_down, False, 600, None),
    ("hotspot_full", "핫스팟 꽉 참", "HOTSPOT_FULL", _sc_hotspot_full, False, 600, None),
    ("panel_not_on_hotspot", "패널이 핫스팟에 없음", "PANEL_NOT_ON_HOTSPOT", _sc_panel_not_on_hotspot, False, 600, None),
    ("panel_reconnecting", "패널 다시 붙는 중", "PANEL_RECONNECTING", _sc_panel_reconnecting, False, 600, None),
    ("panel_joining", "패널 연결 기다림", "PANEL_JOINING", _sc_panel_joining, False, 600, None),
    ("panel_no_session", "패널 신호 연결 안 됨", "PANEL_NO_SESSION", _sc_panel_no_session, False, 600, None),
    ("panel_silent", "패널 신호 끊김 감지", "PANEL_SILENT", _sc_panel_silent, False, 600, None),
    ("panel_off", "패널 꺼짐 (끊김 뒤 연결 끝남)", "PANEL_NO_SESSION", _sc_panel_off, False, 600, None),
    ("usb_loss", "노트북에 신호가 안 들어옴", "USB_LOSS", _sc_usb_loss, False, 600, None),
    ("live_not_running", "음악 프로그램 꺼짐 (자동 다시 켜기 꺼 둠)", "LIVE_NOT_RUNNING", _sc_live_not_running, True, 600,
     None),
    ("live_starting", "음악 프로그램 여는 중", "LIVE_STARTING", _sc_live_starting, True, 40, None),
    ("live_restarting", "음악 프로그램 꺼져서 다시 켜는 중", "LIVE_RESTARTING", _sc_live_restarting, True, 600, None),
    ("live_restarted", "음악 프로그램 조금 전 다시 켜짐", "LIVE_RESTARTED", _sc_live_restarted, True, 600, None),
    ("live_gave_up", "음악 프로그램 계속 꺼짐 (다시 켜기 멈춤)", "LIVE_GAVE_UP", _sc_live_gave_up, False, 1200, None),
    ("on_battery", "충전기 빠짐 (소리는 계속)", "ON_BATTERY", _sc_on_battery, True, 600, None),
    ("on_battery_low", "충전기 빠짐, 배터리 적음", "ON_BATTERY", _sc_on_battery_low, True, 600, None),
    ("wifi_poor", "무선 나쁨 (신호 몰림)", "WIFI_POOR", _sc_wifi_poor, "bunch", 600, None),
    ("power_settings", "절전 설정 문제 (이 노트북 9/25)", "POWER_SETTINGS", _sc_power_settings, True, 600, None),
    ("several", "여러 문제 한꺼번에", "LIVE_NOT_RUNNING", _sc_several, True, 600, None),
    ("no_midi_lib", "python-rtmidi 없음", "MONITOR_NO_MIDI", _sc_no_midi_lib, False, 600, None),
    ("panel_no_session_two", "패널 연결 안 됨 (기기 2대)", "PANEL_NO_SESSION", _sc_two_stations, False, 600, None),
    ("fix_resync", "버튼: 소리 다시 맞추기", "ALL_OK", _sc_all_ok, True, 600, ("resync", "ok", 2.0)),
    ("fix_reconnect_wait", "버튼: 연결 다시 잡는 중", "FIX_WAIT", _sc_panel_silent, False, 600, ("reconnect", "ok", 3.0)),
    ("fix_reconnect_ok", "버튼: 연결 다시 잡기 성공", "ALL_OK", _sc_panel_silent, True, 600, ("reconnect", "ok", 14.0)),
    ("fix_reconnect_failed", "버튼: 연결 다시 잡기 실패", "PANEL_NO_SESSION", _sc_panel_silent, False, 600,
     ("reconnect", "fail", 46.0)),
    ("fix_no_ack", "버튼: 작은 보드가 답 안 함", "PANEL_SILENT", _sc_panel_silent, False, 600, ("reconnect", "no_ack", 6.0)),
    ("fix_reboot_panel_wait", "버튼: 패널 다시 켜는 중", "FIX_WAIT", _sc_panel_no_session, False, 600,
     ("reboot_panel", "ok", 6.0)),
    ("fix_reboot_panel_ok", "버튼: 패널 다시 켜기 성공", "ALL_OK", _sc_panel_no_session, True, 600,
     ("reboot_panel", "ok", 20.0)),
    ("fix_reboot_panel_refused", "버튼: 패널 다시 켜기 거절", "PANEL_NO_SESSION", _sc_panel_no_session, False, 600,
     ("reboot_panel", "refused", 4.0)),
    ("fix_reboot_flowlocal_wait", "버튼: FlowLocal 다시 켜는 중", "FIX_WAIT", _sc_fl_ap_down, False, 600,
     ("reboot_flowlocal", "ok", 10.0)),
    ("fix_reboot_flowlocal_ok", "버튼: FlowLocal 다시 켜기 성공", "ALL_OK", _sc_fl_ap_down, True, 600,
     ("reboot_flowlocal", "ok", 26.0)),
    ("live_never_up", "음악 프로그램이 안 켜짐 (켠 적 없음)", "LIVE_NOT_RUNNING", _sc_live_never_up, True, 600, None),
    ("wifi_poor_mon3", "무선 나쁨 (새 펌웨어: 채널 버튼)", "WIFI_POOR", _sc_wifi_poor_mon3, "bunch", 600, None),
    ("fix_channel_wait", "버튼: 무선 채널 바꾸는 중", "FIX_WAIT", _sc_wifi_poor_mon3, False, 600, ("channel_next", "ok", 8.0)),
    ("fix_channel_ok", "버튼: 무선 채널 바꾸기 성공", "ALL_OK", _sc_wifi_poor_mon3, True, 600, ("channel_next", "ok", 24.0)),
    # the one big button: "loop" plays the run again and again in the demo (3-27 s after the press)
    ("reconnect_run", "한 번에 다시 연결하기 (차례로 보기)", "RECONNECT_RUNNING", _sc_panel_silent, False, 600,
     (RECONNECT_ID, "ok", "loop")),
    ("reconnect_done", "한 번에 다시 연결하기 (다 됨)", "ALL_OK", _sc_panel_silent, True, 600, (RECONNECT_ID, "ok", 30.0)),
    ("reconnect_rst", "한 번에 다시 연결하기 (RST 부탁)", "RECONNECT_HUMAN", _sc_fl_no_reply, False, 600,
     (RECONNECT_ID, "rst", 28.0)),
    ("reconnect_panel_power", "한 번에 다시 연결하기 (패널 전원 부탁)", "RECONNECT_HUMAN", _sc_panel_not_on_hotspot, False,
     600, (RECONNECT_ID, "panel_off", 45.0)),
    ("reconnect_panel_reboot", "한 번에 다시 연결하기 (패널도 다시 켬)", "ALL_OK", _sc_panel_no_session, True, 600,
     (RECONNECT_ID, "panel_reboot", 60.0)),
    ("reconnect_live", "한 번에 다시 연결하기 (음악 프로그램도 켬)", "ALL_OK", _sc_live_never_up, True, 600,
     (RECONNECT_ID, "live_off", 50.0)),
))


def demo_outcome(key, action):
    """What a button pressed in the demo does in this scenario."""
    if action == "resync":
        return "ok"
    if key in ("fl_no_reply", "fl_no_reply_short", "fix_no_ack"):
        return "no_ack"             # FlowLocal takes nothing
    if key in ("fix_reboot_panel_refused", "fix_reconnect_failed"):
        return SCENARIOS[key][5][1]
    return "ok"


def demo_reconnect_outcome(key):
    """What the pretend world (DemoWorld) does when the one big button is
    pressed in this scenario."""
    base = SCENARIOS[key][2]
    if base in (_sc_fl_no_reply, _sc_fl_no_reply_short, _sc_fl_no_reply_acks, _sc_fl_stuck):
        return "rst"
    if base is _sc_port_missing:
        return "plug"
    if base in (_sc_panel_not_on_hotspot, _sc_panel_off):
        return "panel_off"
    if base is _sc_panel_no_session:
        return "panel_reboot"
    if base is _sc_two_stations:
        return "phones"
    if base is _sc_live_never_up:
        return "live_off"
    return "ok"


def demo_arrivals(tr, end, anim_t, seconds, bunch=False):
    """A visitor turning K1-K3: a CC every ~70 ms in 2.8 s gestures with
    1.4 s pauses, over the last `seconds`. With bunch the network holds six
    of them in every gesture and lets them go within 5 ms (two per knob:
    held-up knob messages). Values follow when the panel sent each message,
    arrival times when the laptop got it. Times follow the demo's own clock
    (anim_t) so the timeline scrolls between polls."""
    step = 0.07
    msgs = []
    k0 = max(0, int((anim_t - seconds) / step))
    for k in range(k0, int(anim_t / step) + 1):
        slot = k % 60
        if slot >= 40:
            continue                         # pause between gestures
        w = k * step + ((k * 37) % 11) * 0.002
        if bunch and 12 <= slot <= 17:
            last = k - slot + 17
            w = last * step + ((last * 37) % 11) * 0.002 + (slot - 12) * 0.0015 + 0.03
        if w > anim_t or w < anim_t - seconds:
            continue
        knob = k % 3
        sent = k * step                      # when the panel sent it (a held one arrives later)
        val = int(round(63.5 + 63.5 * math.sin(sent * (0.9, 0.5, 0.31)[knob] + (0.0, 2.0, 4.0)[knob])))
        msgs.append((w, [0xB0, 24 + knob, val]))
    msgs.sort(key=lambda m: m[0])
    for w, m in msgs:
        t = end - (anim_t - w)
        tr.on_midi(m, t, t)


def run_scenario(key, anim_t=None, wall_end=None, press=None):
    """A Tracker fed with one scenario's script (and a button press, the
    scenario's own or `press`); its clock ends at T_END."""
    label, _code, fn, anim, up, flow = SCENARIOS[key]
    if press is not None:
        flow = press
    if flow and flow[2] == "loop":
        # the one big button, played again and again: 3-27 s after the press
        flow = (flow[0], flow[1], 3.0 + ((anim_t or 0.0) % 24.0))
    elapsed = flow[2] if flow else 0.0
    base_end = T_END - elapsed
    clk = FakeClock(base_end - up)
    wall_end = time.time() if wall_end is None else wall_end

    def wall():
        return wall_end - (T_END - clk.t)
    events = EventLog(None, wall=wall)
    # the demo's keeper "starts" Live without starting anything
    keeper = LiveKeeper(events, launcher=lambda set_path, exe=None, prefer_exe=False: (True, "set", None),
                        crash_probe=lambda since_s: dict(DEMO_LIVE_CRASH))
    tr = Tracker(events, clock=clk, midi_lib=True, memory=Memory(None, wall), keeper=keeper)
    tr.arrival_source = "rtmidi"
    events.add("monitor", "데모 화면이에요: %s" % label)
    s = Script(tr, clk, end=base_end)
    fn(s)
    if flow:
        s.flow(*flow)
    clk.t = T_END
    if anim and tr.port_present and tr.prev_ps != 0:
        t_anim = 33.0 if anim_t is None else anim_t
        if anim == "bunch":
            demo_arrivals(tr, T_END, t_anim + 62.0, 62.0, bunch=True)
        elif anim_t is not None:
            demo_arrivals(tr, T_END, t_anim + 20.0, 17.0)
        n = 1 + int(t_anim // 20) % 6
        if anim_t is not None and n != tr.pc and not flow:
            tr.pc, tr.pc_at = n, clk.t - (t_anim % 20)
    clk.t = T_END
    return tr, clk


class DemoApp(object):
    """--demo: the scenarios in turn (or the one picked on the page). A
    button pressed on the page plays out over the next seconds with that
    scenario's outcome; nothing is sent anywhere."""

    mode = "demo"
    PRESS_HOLD_S = 150.0

    def __init__(self, config, pinned=None, cycle_s=7.0):
        self.config = config
        self.pinned = pinned
        self.cycle_s = cycle_s
        self.t0 = time.monotonic()
        self.keys = list(SCENARIOS)
        self.press = None
        self.autorestart = None           # the page's switch in this demo (None: config.json's)
        self.lock = threading.Lock()

    def start(self):
        pass

    def stop(self):
        return False

    def _press_for(self, key):
        p = self.press
        if p is None or p["scenario"] != key:
            return None
        elapsed = time.monotonic() - p["t"]
        if elapsed > self.PRESS_HOLD_S:
            return None
        return (p["action"], p["outcome"], elapsed, p.get("cancel_at"))

    def current(self, query):
        want = ((query or {}).get("scenario") or [None])[0] or self.pinned
        if want in SCENARIOS:
            return want, None
        p = self.press
        if p is not None and time.monotonic() - p["t"] <= self.PRESS_HOLD_S:
            return p["scenario"], None
        elapsed = time.monotonic() - self.t0
        i = int(elapsed // self.cycle_s) % len(self.keys)
        return self.keys[i], round(self.cycle_s - elapsed % self.cycle_s, 1)

    def _cfg(self):
        cfg = self.config.get()
        if self.autorestart is not None:
            cfg = dict(cfg, live_autorestart=self.autorestart)
        return cfg

    def state(self, query):
        key, next_in = self.current(query)
        tr, clk = run_scenario(key, anim_t=time.monotonic() - self.t0, press=self._press_for(key))
        return build_state(self.mode, tr.snapshot(clk.t), self._cfg(), self.config.error, tr.events.recent(40), None, {
            "demo": {"scenario": key, "label": SCENARIOS[key][0], "expect": SCENARIOS[key][1], "next_in": next_in,
                     "scenarios": [[k, v[0]] for k, v in SCENARIOS.items()]},
        })

    def fix(self, action, query=None):
        with self.lock:
            key, _ = self.current(query)
            tr, clk = run_scenario(key, press=self._press_for(key))
            s = action_state(tr.snapshot(clk.t), action)
            if not s["enabled"]:
                return _refusal(action, s["reason"], s["manual"] if s["needs_fw"] else None)
            self.press = {"action": action, "t": time.monotonic(), "scenario": key, "outcome": demo_outcome(key, action)}
            return {"ok": True, "result": "sent", "message": "(데모) 누른 것처럼 흉내 내요. 실제로는 아무것도 보내지 않아요."}

    def reconnect(self, query=None):
        """The one big button in the demo: the scenario's pretend world plays it out."""
        with self.lock:
            key, _ = self.current(query)
            tr, clk = run_scenario(key, press=self._press_for(key))
            s = reconnect_state(tr.snapshot(clk.t))
            if not s["enabled"]:
                return {"ok": False, "result": "refused", "message": "시작하지 않았어요: %s." % s["reason"]}
            self.press = {"action": RECONNECT_ID, "t": time.monotonic(), "scenario": key,
                          "outcome": demo_reconnect_outcome(key)}
            return {"ok": True, "result": "started",
                    "message": "(데모) 누른 것처럼 흉내 내요. 실제로는 아무것도 보내지 않아요."}

    def reconnect_cancel(self, query=None):
        with self.lock:
            p = self.press
            if p is None or p["action"] != RECONNECT_ID or p.get("cancel_at") is not None:
                return {"ok": False, "message": "하고 있는 것이 없어요."}
            p["cancel_at"] = time.monotonic() - p["t"]
            return {"ok": True, "message": "(데모) 그만했어요."}

    def set_live_autorestart(self, on, query=None):
        """The switch, in this demo only (config.json is not written)."""
        self.autorestart = bool(on)
        return {"ok": True, "on": bool(on), "message": "(데모) 자동 다시 켜기를 %s. config.json은 바꾸지 않아요." % (
            "켰어요" if on else "껐어요")}


# ================================================================ HTTP

_FIX_PATH = re.compile(r"^/api/fix/([a-z_]+)$")


class Handler(BaseHTTPRequestHandler):
    server_version = "PFMonitor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quiet; pythonw has no stderr
        pass

    def _host_ok(self):
        # A page elsewhere cannot use DNS rebinding to read or press anything.
        host = (self.headers.get("Host") or "").lower()
        return host in self.server.allowed_hosts

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        if not self._host_ok():
            return self._send(403, {"error": "host"})
        url = urlparse(self.path)
        try:
            if url.path in ("/", "/index.html"):
                with open(DASHBOARD, "rb") as f:
                    return self._send(200, f.read(), "text/html; charset=utf-8")
            if url.path == "/api/state":
                return self._send(200, self.server.app.state(parse_qs(url.query)))
            if url.path == "/api/ping":
                return self._send(200, {"ok": True, "app": "pf-monitor", "mode": self.server.app.mode})
            if url.path == "/favicon.ico":
                return self._send(204, b"", "image/x-icon")
            return self._send(404, {"error": "not found"})
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            return None
        except Exception as e:
            return self._send(500, {"error": str(e), "trace": traceback.format_exc(limit=3)})

    def do_POST(self):
        if not self._host_ok():
            return self._send(403, {"error": "host"})
        url = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(min(length, 4096))
        m = _FIX_PATH.match(url.path)
        switch = url.path == "/api/live_autorestart"
        sequence = url.path in ("/api/reconnect", "/api/reconnect/cancel")
        if not switch and not sequence and (not m or m.group(1) not in ACTIONS):
            return self._send(404, {"error": "not found"})
        # Only the dashboard sets this header; a cross-site form or fetch cannot
        # without a CORS preflight, which this server never answers.
        if self.headers.get("X-PF-Monitor") != "1":
            return self._send(403, {"ok": False, "message": "이 버튼은 모니터 화면에서만 누를 수 있어요."})
        query = parse_qs(url.query)
        try:
            if switch:
                on = (query.get("on") or [""])[0]
                if on not in ("0", "1"):
                    return self._send(400, {"ok": False, "message": "on=0 또는 on=1"})
                return self._send(200, self.server.app.set_live_autorestart(on == "1", query))
            if url.path == "/api/reconnect":
                return self._send(200, self.server.app.reconnect(query))
            if url.path == "/api/reconnect/cancel":
                return self._send(200, self.server.app.reconnect_cancel(query))
            return self._send(200, self.server.app.fix(m.group(1), query))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            return None
        except Exception as e:
            return self._send(500, {"ok": False, "message": "모니터 오류: %s" % e})


class MonitorServer(ThreadingHTTPServer):
    daemon_threads = True
    # Windows lets a second socket bind a port that has SO_REUSEADDR; take the
    # port exclusively so a second monitor fails to start instead.
    allow_reuse_address = False

    def server_bind(self):
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        # HTTPServer.server_bind would call getfqdn(), which can stall on Windows.
        import socketserver
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


def make_server(app, port, host=HTTP_HOST):
    srv = MonitorServer((host, port), Handler)
    srv.app = app
    real = srv.server_address[1]
    srv.allowed_hosts = {"%s:%d" % (h, real) for h in ("127.0.0.1", "localhost")}
    return srv


def ping(url, timeout=1.5):
    try:
        with urllib.request.urlopen(url.rstrip("/") + "/api/ping", timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8")).get("app") == "pf-monitor"
    except Exception:
        return False


def open_dashboard(url):
    """Edge in app mode (a window without tabs or address bar), else the default browser."""
    candidates = []
    for env in ("ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"):
        base = os.environ.get(env)
        if base:
            candidates.append(os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"))
    for exe in candidates:
        if os.path.isfile(exe):
            try:
                subprocess.Popen([exe, "--app=" + url, "--window-size=1280,860"], close_fds=True)
                return True
            except OSError:
                pass
    import webbrowser
    return webbrowser.open(url)


def _fix_std_streams():
    # pythonw has no console: sys.stdout / sys.stderr are None and any print dies.
    for name in ("stdout", "stderr"):
        if getattr(sys, name) is None:
            setattr(sys, name, open(os.devnull, "w", encoding="utf-8"))
        else:
            try:
                getattr(sys, name).reconfigure(errors="replace", line_buffering=True)
            except Exception:
                pass


def main(argv=None):
    ap = argparse.ArgumentParser(description="Patternflow exhibit staff monitor (dashboard on http://127.0.0.1:8765).")
    ap.add_argument("--port-name", default=PORT_NAME, help="substring of the FlowLocal MIDI port name (default: %(default)r)")
    ap.add_argument("--http-port", type=int, default=HTTP_PORT, help="dashboard port (default: %(default)s)")
    ap.add_argument("--open", action="store_true", help="open the dashboard window (Edge app mode, else the default browser)")
    ap.add_argument("--demo", action="store_true", help="no hardware: serve scripted scenarios")
    ap.add_argument("--demo-scenario", help="with --demo: stay on this scenario (see --list-scenarios)")
    ap.add_argument("--demo-cycle", type=float, default=7.0, help="with --demo: seconds per scenario (default: %(default)s)")
    ap.add_argument("--list-scenarios", action="store_true", help="print the demo scenarios and exit")
    ap.add_argument("--seconds", type=float, default=0.0, help="stop after this many seconds (tests)")
    ap.add_argument("--log-dir", default=os.path.join(HERE, "logs"), help="event log folder (default: logs next to this file)")
    args = ap.parse_args(argv)
    _fix_std_streams()

    if args.list_scenarios:
        for k, v in SCENARIOS.items():
            print("%-26s %-22s %s" % (k, v[1], v[0]))
        return 0
    if args.demo_scenario and args.demo_scenario not in SCENARIOS:
        print("unknown scenario %r (see --list-scenarios)" % args.demo_scenario)
        return 2

    url = "http://%s:%d/" % (HTTP_HOST, args.http_port)
    config = Config()
    app = DemoApp(config, args.demo_scenario, args.demo_cycle) if args.demo else LiveApp(args.port_name, args.log_dir, config, url)
    try:
        server = make_server(app, args.http_port)
    except OSError as e:
        if ping(url):
            print("the monitor is already running: %s" % url)
            if args.open:
                open_dashboard(url)
            return 0
        print("cannot listen on %s: %s" % (url, e))
        return 1

    def excepthook(a):
        if hasattr(app, "events"):
            app.events.add("error", "스레드 오류: %s" % "".join(traceback.format_exception(a.exc_type, a.exc_value, a.exc_traceback, limit=4)).replace("\n", " | "), "warn")
    threading.excepthook = excepthook

    app.start()
    print("PF monitor (%s) on %s - Ctrl-C to stop" % (app.mode, url))
    if args.open:
        open_dashboard(url)
    if args.seconds > 0:
        threading.Timer(args.seconds, server.shutdown).start()
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        vanished = app.stop()
        if hasattr(app, "events"):
            app.events.add("monitor", "모니터를 껐어요")
    if vanished:
        # a vanished MIDI port must never be closed (see MidiWorker)
        sys.stdout.flush()
        os._exit(0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
