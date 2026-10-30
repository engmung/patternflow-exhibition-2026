#!/usr/bin/env python3
"""Check a built FlowLocal exhibit image before it goes near the venue.

    python tools/check_exhibit_image.py [path/to/firmware.bin]

Default path: $PLATFORMIO_BUILD_DIR/exhibit/firmware.bin, else
.pio/build/exhibit/firmware.bin.

Passes when the image
  - carries the exhibit hotspot password (control: the scan can see strings),
  - does NOT carry the touring kit's public password "patternflow" as a
    NUL-terminated string (every lowercase "patternflow" byte run is listed
    with context either way),
  - names the USB-MIDI port "Patternflow Exhibit" and the RTP session
    "FlowLocal",
  - carries the laptop contract as the status page states it: the resync
    request "CC 119 ch 16 value>0" and the program mirror
    "CC 118 ch 16 value=program" (with its log line and counter), and the
    knob re-send "CC 24-26 ch 1 value=last" (with its log line, the cached
    values and the counter), and the PF monitor's status request
    "F0 7D 50 46 01 F7" (with its log line and reply counter), its commands'
    log line and the panel reboot path "/api/wifi/reboot",
  - carries the automatic channel's scan log line and ap_channel, and the RTP
    timing fields (rtp_jitter_ms, rtp_spread_ms) in /api/status,
  - carries the self-recovery: the PF monitor tag flx-mon3, the boot line and
    the bridge watchdog's and USB watch's log lines, the restart fields in
    /api/status (reset_reason, usb_reconnects) and the notes the status reply
    reports as why= (bridge_hung, usb_wedged, cmd13),
  - carries the RTP session handling of flx-mon3: its log lines (never
    refused, the list-full eviction, a repeated invitation answered, a silent
    session ended) and /api/status's rtp_evicted and rtp_retries_answered,
  - carries the PF monitor's command 13 (Wi-Fi channel): the commands log
    line naming it, its NVS namespace, the manual channel's boot log line and
    ap_channel_choice in /api/status,
  - carries the panel address (panel_ip) and the status page's links to the
    panel's own pages (/, /midi, /knobs, /api/exhibit?caption=0 and =1), and
    the hotspot station list (ap_stations) the page offers with no session,
  - states recovery time only as "usually within a minute (not yet measured
    on hardware)", with none of the times the status page used to give,
  - carries none of the regular build's broker / USB-host / SD / weather /
    Director markers.

Run it on the regular `flowlocal` image as a negative control: it must FAIL
there (public password present, broker markers present).
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

EXHIBIT_PASS = b"YOUR_PF_WIFI_PASS"
PUBLIC_PASS = b"patternflow"
RECOVERY_WORDING = b"usually within a minute (not yet measured on hardware)"

MUST_HAVE = {
    EXHIBIT_PASS: "exhibit AP password",
    b"PatternflowLocal\x00": "AP SSID",
    b"Patternflow Exhibit\x00": "USB-MIDI product / port name",
    b"FlowLocal\x00": "RTP-MIDI session name",
    b"[midi] rtp listener": "bridge log marker",
    b"CC 119 ch 16 value>0\x00": "resync request contract (status field text)",
    b"CC 118 ch 16 value=program\x00": "program mirror contract (status field text)",
    b"[midi] program mirror: CC ": "program mirror log line",
    b"program_mirrors\x00": "program mirror counter in /api/status",
    b"CC 24-26 ch 1 value=last\x00": "knob re-send contract (status field text)",
    b"[midi] knob re-send: CC ": "knob re-send log line",
    b"knob_values\x00": "cached knob values in /api/status",
    b"knob_resends\x00": "knob re-send counter in /api/status",
    b"F0 7D 50 46 01 F7\x00": "PF monitor status request (status field text)",
    b"[midi] pf monitor: status request ": "PF monitor log line",
    b"status_replies\x00": "PF monitor reply counter in /api/status",
    b"[midi] pf monitor: command ": "PF monitor command log line",
    b"/api/wifi/reboot\x00": "panel reboot path (PF monitor command 11)",
    b"[FlWifi] channel scan": "automatic channel scan log line",
    b"ap_channel\x00": "AP channel in /api/status",
    b"rtp_jitter_ms\x00": "RTP jitter in /api/status",
    b"rtp_spread_ms\x00": "RTP transit spread in /api/status",
    b"flx-mon3\x00": "PF monitor fw tag with the restart keys, cs= and command 13",
    b"[midi] rtp sessions: never refused": "RTP session handling log line",
    b"[midi] rtp session list full: ended ": "eviction instead of NO log line",
    b"[midi] rtp invitation repeated by ": "repeated invitation answered log line",
    b"[midi] rtp session of ": "silent session ended log line",
    b"rtp_evicted\x00": "evicted sessions in /api/status",
    b"rtp_retries_answered\x00": "repeated invitations answered in /api/status",
    b"13 Wi-Fi channel (kept in NVS, then a restart)": "PF monitor command 13 in the commands log line",
    b"fl_exhibit\x00": "NVS namespace of command 13's channel",
    b"chosen from the laptop (PF monitor command 13, NVS)": "manual channel boot log line",
    b"ap_channel_choice\x00": "command 13's stored channel in /api/status",
    b"cmd13\x00": "restart note cmd13 (why=)",
    b"[FlowLocal] boot ": "boot line (boots since power-on, reset reason)",
    b"[midi] bridge watchdog: ": "bridge watchdog log line",
    b"[midi] usb watch: ": "USB watch (soft reconnect) log line",
    b"reset_reason\x00": "reset reason in /api/status",
    b"usb_reconnects\x00": "USB soft reconnects in /api/status",
    b"bridge_hung\x00": "restart note bridge_hung (why=)",
    b"usb_wedged\x00": "restart note usb_wedged (why=)",
    b"panel_ip\x00": "panel address in /api/status",
    b"ap_stations\x00": "hotspot stations in /api/status (panel candidates with no session)",
    b"http://'+s+'/midi'": "MIDI link for each hotspot station with no session",
    b"'Panel: '": "panel address on the status page",
    b"b+'/'": "link to the panel console",
    b"b+'/midi'": "link to the panel MIDI page",
    b"b+'/knobs'": "link to the panel knobs page",
    b"b+'/api/exhibit?caption=0'": "link turning panel captions off",
    b"b+'/api/exhibit?caption=1'": "link turning panel captions on",
    RECOVERY_WORDING: "recovery-time wording",
}

MUST_NOT_HAVE = {
    b"[broker] PicoMQTT": "MQTT broker",
    b"[usb] host ready": "USB-host stick (EspUsbHost)",
    b"[sd] ": "SPI MicroSD",
    b"[tft] ": "ST7735 status display",
    b"[wx] ": "weather hub",
    b"/api/library": "USB/SD content library",
    b"Director not installed": "LittleFS Director",
    b"[FlWifi] STA connecting": "upstream Wi-Fi (STA)",
    b"well under a minute": "old recovery claim (reboot dialog)",
    b"within about 40 seconds": "old recovery claim (panel not connected hint)",
    b"wait about 10 seconds": "old recovery claim (stale session hint)",
}


def default_path() -> Path:
    build = os.environ.get("PLATFORMIO_BUILD_DIR")
    if build:
        return Path(build) / "exhibit" / "firmware.bin"
    return Path(__file__).resolve().parents[1] / ".pio" / "build" / "exhibit" / "firmware.bin"


def context(data: bytes, at: int, n: int) -> str:
    lo, hi = max(0, at - 16), min(len(data), at + n + 16)
    return data[lo:hi].decode("latin-1").encode("unicode_escape").decode("ascii")


def find_all(data: bytes, needle: bytes) -> list[int]:
    out, i = [], data.find(needle)
    while i >= 0:
        out.append(i)
        i = data.find(needle, i + 1)
    return out


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_path()
    data = path.read_bytes()
    print(f"image: {path}  ({len(data)} bytes)")
    failures = 0

    for needle, what in MUST_HAVE.items():
        hits = find_all(data, needle)
        ok = bool(hits)
        failures += not ok
        print(f"  [{'ok' if ok else 'FAIL'}] has {what}: {len(hits)} hit(s)")

    public_hits = find_all(data, PUBLIC_PASS)
    terminated = [i for i in public_hits if data[i + len(PUBLIC_PASS):i + len(PUBLIC_PASS) + 1] == b"\x00"]
    ok = not terminated
    failures += not ok
    print(f"  [{'ok' if ok else 'FAIL'}] public password \"patternflow\" as a C string: "
          f"{len(terminated)} hit(s)")
    for i in public_hits:
        tag = "C-STRING" if i in terminated else "inside  "
        print(f"      {tag} 0x{i:06x}: {context(data, i, len(PUBLIC_PASS))}")

    for needle, what in MUST_NOT_HAVE.items():
        hits = find_all(data, needle)
        ok = not hits
        failures += not ok
        print(f"  [{'ok' if ok else 'FAIL'}] no {what}: {len(hits)} hit(s)")

    print("PASS" if failures == 0 else f"FAIL ({failures} check(s))")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
