#!/usr/bin/env python3
"""Host test of ensemble/tools/panels.py against stand-in panels.

Each stand-in is a small HTTP server that answers the routes the tool uses
the way Patternflow's docs/rest-api.md and src/core_web_update.h say a panel
does: one connection at a time, /api/status, POST /update (multipart, with
?size=), a restart 1.2 s after a good upload, /api/wifi. They are not
panels. What this proves is the tool's own behaviour: what it refuses to
send, what it skips, when it stops.

  python ensemble/tests/test_panels_tool.py
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import panels  # noqa: E402

# The tool's waits, shortened: the stand-ins restart in a second.
panels.REBOOT_WAIT_S = 0.4
panels.BACK_TIMEOUT_S = 6
panels.SOAK_S = 2

checks = failed = 0


def check(cond, what):
    global checks, failed
    checks += 1
    if not cond:
        failed += 1
        print(f"  FAIL {what}")


def image(build="beef1234", variant="ensemble", chip=9, size=200_000, way_out=True, magic=0xE9):
    """Bytes shaped like an ESP32-S3 app image: the fields the tool reads, and filler."""
    data = bytearray(os.urandom(size))
    data[0] = magic
    data[12:14] = chip.to_bytes(2, "little")
    data[0x20:0x24] = bytes.fromhex("3254cdab")
    data[0xB0:0xB4] = bytes.fromhex(build)
    data[0x100:0x100 + 64] = (f"PFVARIANT:{variant};".encode()).ljust(64, b"\0")
    marker = b"/update/status"
    start = 0x200
    if way_out:
        data[start:start + len(marker)] = marker
    else:
        while marker in data:      # random filler will not hold it, but be exact
            i = data.index(marker)
            data[i] = 0
    return bytes(data)


class Panel:
    """One stand-in panel."""

    def __init__(self, bind="127.0.0.1", build="aaaa0001", variant="core", reports_build=True, **quirks):
        self.build, self.variant, self.reports_build = build, variant, reports_build
        self.quirks = quirks          # fail_once, never_back, crash_loop, keeps_old, locked
        self.boot = time.monotonic() - 500
        self.down_from = self.down_until = None
        self.next = None              # (build, variant) to boot into
        self.uploads = 0
        self.wifi = {}
        self.reset_reason = "poweron"
        self.requests = 0
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def reply(self, code, obj):
                body = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(body)
                self.close_connection = True

            def handle_any(self):
                owner.requests += 1
                owner.tick()
                if owner.is_down():
                    self.close_connection = True      # a board that is restarting answers nothing
                    return
                length = int(self.headers.get("Content-Length") or 0)
                body = self.rfile.read(length) if length else b""
                url = urllib.parse.urlsplit(self.path)
                query = urllib.parse.parse_qs(url.query)
                owner.route(self, self.command, url.path, query, body)

            do_GET = do_POST = do_DELETE = do_PUT = handle_any

        self.server = HTTPServer((bind, 0), Handler)      # one request at a time, like the panel
        self.address = f"{bind}:{self.server.server_address[1]}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def is_down(self):
        now = time.monotonic()
        return self.down_from is not None and now >= self.down_from and (self.down_until is None or now < self.down_until)

    def tick(self):
        now = time.monotonic()
        if self.down_until is not None and now >= self.down_until:
            self.boot, self.down_from, self.down_until = self.down_until, None, None
            if self.next:
                self.build, self.variant = self.next
                self.next = None
            self.reset_reason = "sw"
        if self.quirks.get("crash_loop") and self.variant == "ensemble" and now - self.boot > 0.9:
            self.boot = now
            self.reset_reason = "panic"

    def restart(self, into=None, forever=False):
        now = time.monotonic()
        self.down_from = now + 0.3
        self.down_until = None if forever else now + 1.3
        self.next = into

    def route(self, h, method, path, query, body):
        if path == "/api/status" and method == "GET":
            status = {"version": "3.12.0", "variant": self.variant, "variantVersion": "v0.1.0" if self.variant != "core" else "",
                      "uptime": int(time.monotonic() - self.boot), "resetReason": self.reset_reason, "panel": "128x64",
                      "busy": "", "hotspot": {"ssid": "patternflow-" + self.address[-4:]}}
            if self.reports_build:
                status["build"] = self.build
            if self.variant == "ensemble":
                status["ensemble"] = {"uid": "246f28" + self.address[-6:].replace(":", "0"), "linked": True, "hub": "192.168.0.9", "id": 3}
            return h.reply(200, status)
        if path == "/update/status":
            return h.reply(200, {"armed": True, "busy": False, "version": "3.12.0", "lastError": "flash write failed",
                                 "lastRejected": False, "lastOk": False, "received": 1234, "expected": 5678, "attempts": self.uploads})
        if path == "/update" and method == "POST":
            ctype = h.headers.get("Content-Type", "")
            if "multipart/form-data" not in ctype or "boundary=" not in ctype:
                return h.reply(400, {"error": "no firmware image in the request"})
            boundary = ctype.split("boundary=", 1)[1].encode()
            if len(boundary) > 70:
                return h.reply(400, {"error": "boundary too long"})
            head_end = body.index(b"\r\n\r\n") + 4
            tail = body.rindex(b"\r\n--" + boundary + b"--")
            part_head, data = body[:head_end], body[head_end:tail]
            if b'name="firmware"' not in part_head or int(query.get("size", ["0"])[0]) != len(data):
                return h.reply(400, {"error": "no firmware image in the request"})
            self.uploads += 1
            if self.quirks.get("locked"):
                return h.reply(403, {"error": "locked - on the device: hold K2 for NETWORK, then turn K4 for UPDATE"})
            if self.quirks.get("fail_once"):
                self.quirks["fail_once"] = False
                return h.reply(500, {"error": "flash write failed"})
            if data[0] != 0xE9:
                return h.reply(500, {"error": "not an image"})
            variant = data[0x100:0x140].split(b";")[0].decode().replace("PFVARIANT:", "")
            into = None if self.quirks.get("keeps_old") else (data[0xB0:0xB4].hex(), variant)
            h.reply(200, {"ok": True})
            self.restart(into, forever=bool(self.quirks.get("never_back")))
            return None
        if path == "/api/wifi" and method == "POST":
            form = urllib.parse.parse_qs(body.decode())
            fields = {k: v[0] for k, v in {**query, **form}.items()}
            if not fields.get("ssid"):
                return h.reply(400, {"ok": False, "error": "ssid missing"})
            self.wifi[fields["ssid"]] = {"pass": fields.get("pass", ""), "connect": fields.get("connect") == "1"}
            return h.reply(200, {"ok": True})
        if path == "/api/wifi" and method == "DELETE":
            ssid = query.get("ssid", [""])[0]
            if ssid in self.wifi:
                del self.wifi[ssid]
                return h.reply(200, {"ok": True})
            return h.reply(404, {"ok": False, "error": "not saved"})
        if path == "/api/wifi/reboot" and method == "POST":
            h.reply(200, {"ok": True})
            self.restart()
            return None
        return h.reply(404, {"error": "no such route"})


class Hub:
    def __init__(self, rows):
        body = json.dumps({"panels": rows}).encode()

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                ok = self.path == "/api/panels"
                self.send_response(200 if ok else 404)
                self.send_header("Content-Length", str(len(body) if ok else 0))
                self.end_headers()
                if ok:
                    self.wfile.write(body)

        self.server = HTTPServer(("127.0.0.1", 0), Handler)
        self.address = f"127.0.0.1:{self.server.server_address[1]}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()


def run(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            code = panels.main(list(argv))
        except SystemExit as stop:
            code = stop.code if isinstance(stop.code, int) else 1
            if not isinstance(stop.code, int) and stop.code:
                print(stop.code)
    return code, out.getvalue()


def main():
    tmp = Path(tempfile.mkdtemp())
    good = tmp / "firmware.bin"
    good.write_bytes(image())
    stock = tmp / "stock.bin"
    stock.write_bytes(image(build="0c0ffee0", variant="core"))

    # ── The image is looked at before anything is sent ──────────────────
    one = Panel()
    cases = [
        ("the hub's image (no /update in it)", image(way_out=False), "/update"),
        ("an image for another chip", image(chip=5), "ESP32-S3"),
        ("something that is not an image", os.urandom(100_000), "앱 이미지가 아닙니다"),
        ("a bootloader-sized file", image(size=20_000), "앱 이미지가 아닙니다"),
        ("an image bigger than the slot", image(size=0x300000 + 16), "3 MB"),
    ]
    for name, data, expect in cases:
        bad = tmp / "bad.bin"
        bad.write_bytes(data)
        code, out = run("flash", str(bad), one.address, "-y")
        check(code == 2 and expect in out, f"refuses {name}: {out.strip()[:120]}")
    check(one.uploads == 0 and one.requests == 0, "a refused image reaches no panel")
    code, out = run("flash", str(tmp / "missing.bin"), one.address, "-y")
    check(code != 0 and "없습니다" in out, "a missing file is said")

    # ── Three panels in, then again: nothing to do ──────────────────────
    a, b, c = Panel(), Panel(), Panel()
    listing = tmp / "panels.txt"
    listing.write_text(f"# the studio\n{a.address}  # by the door\n\n{b.address}\n{c.address}\n{a.address}\n", encoding="utf-8")
    code, out = run("flash", str(good), "--file", str(listing), "-y")
    check(code == 0, f"three panels flashed: exit {code}\n{out}")
    check("올림 3대, 실패 0대" in out, "the summary counts three")
    check("지켜봅니다" in out, "the first panel is watched before the rest")
    check(all(p.build == "beef1234" and p.variant == "ensemble" for p in (a, b, c)), "each runs the new image")
    check([p.uploads for p in (a, b, c)] == [1, 1, 1], "each took one upload, the repeated address once")
    code, out = run("flash", str(good), "--file", str(listing), "-y")
    check(code == 0 and "올릴 패널이 없습니다" in out and out.count("이미 이 이미지") == 3, "a second run skips all three")
    check([p.uploads for p in (a, b, c)] == [1, 1, 1], "and uploads nothing")
    code, out = run("status", "--file", str(listing))
    check(code == 0 and out.count("ensemble v0.1.0 (3.12.0)") == 3 and "허브 192.168.0.9, 3번" in out, "status shows them")

    # ── And back out, by way of the hub's list ──────────────────────────
    hub = Hub([{"id": 1, "uid": "x", "on": 1, "ip": a.address}, {"id": 2, "uid": "y", "on": 1, "ip": b.address},
               {"id": 3, "uid": "z", "on": 0, "ip": c.address}])
    code, out = run("flash", str(stock), "--hub", hub.address, "-y")
    check(code == 0 and "3번 패널(z)은 지금 허브에 붙어 있지 않아" in out, "the hub's list, without the one that is away")
    check(a.variant == "core" and b.variant == "core" and c.variant == "ensemble", "two are back on the stock image")
    code, out = run("flash", str(good), "--hub", hub.address, "--only", "2", "-y")
    check(code == 0 and b.variant == "ensemble" and a.variant == "core", "--only picks by number")
    code, out = run("status", "--hub", "127.0.0.1:9")
    check(code != 0 and "/api/panels" in out, "a hub that does not answer is said")

    # ── One upload fails: the rest go on, and a rerun does only that one ─
    d, e, f = Panel(), Panel(fail_once=True), Panel()
    trio = [d.address, e.address, f.address]
    code, out = run("flash", str(good), *trio, "-y")
    check(code == 1 and "올림 2대, 실패 1대" in out and "flash write failed" in out, f"one failure is reported\n{out}")
    check(d.build == f.build == "beef1234" and e.build == "aaaa0001", "the other two went on")
    code, out = run("flash", str(good), *trio, "-y")
    check(code == 0 and "올림 1대" in out and out.count("이미 이 이미지") == 2, "the rerun does the one left")
    check([p.uploads for p in (d, e, f)] == [1, 2, 1], "and only that one is touched again")

    # ── A panel that does not come back stops everything ────────────────
    g, h, i = Panel(never_back=True), Panel(), Panel()
    code, out = run("flash", str(good), g.address, h.address, i.address, "-y")
    check(code == 1 and "돌아오지 않았습니다" in out and "멈췄습니다" in out, f"stops at a panel that stays gone\n{out}")
    check("손대지 않음 2대" in out and h.uploads == 0 and i.uploads == 0, "the image went on no second panel")
    code, out = run("status", g.address, h.address)
    check(code == 1 and "답 없음" in out, "status says which one does not answer")

    # ── A first panel that restarts on its own stops it too ─────────────
    j, k = Panel(crash_loop=True), Panel()
    code, out = run("flash", str(good), j.address, k.address, "-y")
    check(code == 1 and ("혼자 다시 켜지고" in out or "스스로 다시 켜졌습니다" in out), f"a crash loop is caught while watching\n{out}")
    check(k.uploads == 0, "and the second panel is left alone")

    # ── Came back, but on the old image ─────────────────────────────────
    m, n = Panel(keeps_old=True), Panel()
    code, out = run("flash", str(good), m.address, n.address, "-y")
    check(code == 1 and "다른 이미지" in out and n.build == "beef1234", "an image that did not take is a failure, not a stop")

    # ── A locked panel, and one that never reports a build ──────────────
    locked = Panel(locked=True)
    code, out = run("flash", str(good), locked.address, "-y")
    check(code == 1 and "UPDATE 화면" in out, "a locked panel says how to unlock it")
    old = Panel(reports_build=False)
    code, out = run("flash", str(good), old.address, "-y")
    check(code == 0 and old.build == "beef1234", "a panel that reports no build is judged by its restart")
    nohash = tmp / "nohash.bin"
    nohash.write_bytes(image(build="00000000"))
    fresh = Panel()
    code, out = run("flash", str(nohash), fresh.address, "-y")
    check(code == 0 and fresh.uploads == 1, "an image without the ELF hash still goes on")

    # ── Without -y it asks, and no is no ────────────────────────────────
    ask = Panel()
    real_input = panels.input if hasattr(panels, "input") else None
    panels.input = lambda prompt="": "n"
    code, out = run("flash", str(good), ask.address)
    check(code == 1 and ask.uploads == 0 and "그만둡니다" in out, "answering no uploads nothing")
    if real_input:
        panels.input = real_input
    else:
        del panels.input

    # ── Wi-Fi ───────────────────────────────────────────────────────────
    p1, p2 = Panel(), Panel()
    code, out = run("wifi-add", "Patternflow Ensemble", "--pass", "s3cret & more", "--connect", p1.address, p2.address)
    check(code == 0 and all(p.wifi.get("Patternflow Ensemble") == {"pass": "s3cret & more", "connect": True} for p in (p1, p2)),
          f"wifi-add stores the network and switches\n{out}")
    code, out = run("wifi-add", "Studio", "--pass", "x", p1.address)
    check(code == 0 and p1.wifi["Studio"]["connect"] is False, "without --connect it only stores")
    code, out = run("wifi-forget", "Patternflow Ensemble", p1.address, p2.address)
    check(code == 0 and "Patternflow Ensemble" not in p1.wifi and "Patternflow Ensemble" not in p2.wifi, "wifi-forget removes it")
    code, out = run("wifi-forget", "Never Saved", p1.address)
    check(code == 0, "forgetting what was never saved is not an error")
    code, out = run("wifi-add", "Studio", "--pass", "x", "127.0.0.1:9")
    check(code == 1 and "실패" in out, "a panel that is not there is a failure")
    code, out = run("reboot", p2.address)
    check(code == 0, "reboot is acknowledged")
    time.sleep(0.5)
    check(panels.get_json(p2.address, "/api/status", timeout=1.0) is None, "and the panel goes away for a moment")

    # ── No panels named ─────────────────────────────────────────────────
    here = os.getcwd()
    os.chdir(tempfile.mkdtemp())      # a folder with no panels.txt in it
    code, out = run("status")
    check(code != 0 and "scan" in out, "with nothing named it says how to find panels")
    os.chdir(here)

    # ── scan (loopback has every 127.0.0.x only on Linux) ───────────────
    if sys.platform.startswith("linux"):
        s1 = Panel(bind="127.0.0.7")
        port = s1.address.split(":")[1]
        found = tmp / "found.txt"
        code, out = run("scan", "--subnet", "127.0.0", "--port", port, "--timeout", "1", "-o", str(found))
        check(code == 0 and "패널 1대" in out and f"127.0.0.7:{port}" in out, f"scan finds the one panel\n{out}")
        check(panels.read_list(found) == [f"127.0.0.7:{port}"], "and writes a list the other commands read")
        code, out = run("scan", "--subnet", "127.0.1", "--port", port, "--timeout", "0.5")
        check(code == 1 and "찾지 못했습니다" in out, "an empty network is said")

    print(f"{'FAIL' if failed else 'PASS'}  {checks} checks, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
