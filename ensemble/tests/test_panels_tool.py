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
        # Patterns: the presets compiled into the image, then the modules on
        # FATFS - alphabetical, or as catalog.txt lists them (pattern_registry.h).
        self.presets = ["Origin"]
        self.modules, self.sidecars, self.catalog = {}, {}, None
        self.puts = []
        self.fs_error = ""            # "not formatted": a board fresh from the flasher
        self.formats = 0
        # The longest piece of free memory, as /api/status says it; None: the
        # image does not say. A module longer than that is cut off mid-upload.
        # A restart puts it back to what a fresh panel has.
        self.heap_largest = quirks.get("heap_largest")
        self.restarts = 0
        # Saved networks as core_wifi.h keeps them: a new or re-sent one goes
        # to the front, and the boot preference follows the network it named,
        # not the slot. connect=1 switches now and leaves the preference alone.
        self.networks, self.boot_idx, self.current = ["Studio"], 0, "Studio"
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
            self.current = self.networks[self.boot_idx] if self.networks else None   # a boot starts from bootIdx
        if self.quirks.get("crash_loop") and self.variant == "ensemble" and now - self.boot > 0.9:
            self.boot = now
            self.reset_reason = "panic"

    def restart(self, into=None, forever=False):
        self.restarts += 1
        if self.heap_largest is not None:
            self.heap_largest = 61428
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
            status["fsMounted"] = not self.fs_error
            status["fsError"] = self.fs_error
            if self.heap_largest is not None:
                status["heapLargest"] = self.heap_largest
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
            ssid = fields["ssid"]
            self.wifi[ssid] = {"pass": fields.get("pass", ""), "connect": fields.get("connect") == "1"}
            preferred = self.networks[self.boot_idx] if self.networks else None
            if ssid in self.networks:
                self.networks.remove(ssid)
            self.networks.insert(0, ssid)
            del self.networks[5:]
            self.boot_idx = self.networks.index(preferred) if preferred in self.networks else 0
            if fields.get("connect") == "1":
                self.current = ssid
            return h.reply(200, {"ok": True})
        if path == "/api/wifi" and method == "GET":
            return h.reply(200, {"max": 5, "connected": self.current is not None, "current": self.current or "",
                                 "bootIdx": self.boot_idx, "networks": [{"ssid": s} for s in self.networks]})
        if path == "/api/wifi/boot" and method == "POST":
            form = urllib.parse.parse_qs(body.decode())
            idx = int((form.get("bootIdx") or query.get("bootIdx") or ["-1"])[0])
            if not 0 <= idx < len(self.networks):
                return h.reply(400, {"ok": False, "error": "invalid boot index"})
            self.boot_idx = idx
            return h.reply(200, {"ok": True, "bootIdx": idx, "reboot": True})
        if path == "/api/wifi" and method == "DELETE":
            ssid = query.get("ssid", [""])[0]
            if ssid in self.wifi:
                del self.wifi[ssid]
                preferred = self.networks[self.boot_idx] if self.networks else None
                if ssid in self.networks:
                    self.networks.remove(ssid)
                self.boot_idx = self.networks.index(preferred) if preferred in self.networks else 0
                return h.reply(200, {"ok": True})
            return h.reply(404, {"ok": False, "error": "not saved"})
        if path == "/api/wifi/reboot" and method == "POST":
            h.reply(200, {"ok": True})
            self.restart()
            return None
        if path == "/api/patterns" and method == "GET":
            order = []
            for slug in self.catalog or []:
                if slug in self.modules and slug not in order:
                    order.append(slug)
            order += [s for s in sorted(self.modules) if s not in order]
            listing = [{"index": i, "name": n, "module": None} for i, n in enumerate(self.presets)]
            listing += [{"index": len(self.presets) + i, "name": s.replace("_", " ").title(), "module": s}
                        for i, s in enumerate(order)]
            return h.reply(200, {"active": 0, "presets": len(self.presets), "mounted": not self.fs_error,
                                 "free": 9_000_000, "patterns": listing})
        if path == "/api/patterns/format" and method == "POST":
            self.formats += 1
            if self.fs_error.startswith("not formatted"):
                self.fs_error = ""
                self.modules, self.sidecars, self.catalog = {}, {}, None
                return h.reply(200, {"ok": True})
            return h.reply(500, {"ok": False, "error": self.fs_error or "format failed"})
        if path == "/api/patterns" and method == "PUT" and self.fs_error:
            return h.reply(400, {"ok": False, "error": "filesystem not mounted"})
        if path == "/api/patterns/file" and method == "GET":
            slug, ext = query.get("slug", [""])[0], query.get("ext", ["pfm"])[0]
            data = (self.modules if ext == "pfm" else self.sidecars).get(slug)
            if data is None:
                return h.reply(404, {"error": "not found"})
            h.send_response(200)
            h.send_header("Content-Length", str(len(data)))
            h.send_header("Connection", "close")
            h.end_headers()
            h.wfile.write(data)
            h.close_connection = True
            return None
        if path == "/api/patterns" and method == "PUT":
            name = h.headers.get("X-PF-Name", "")
            if self.heap_largest is not None and len(body) > self.heap_largest:
                h.close_connection = True         # no room for it in one piece: the panel drops the upload
                return None
            self.puts.append(name)
            if name == "catalog.txt":
                self.catalog = [line.strip() for line in body.decode().splitlines() if line.strip()]
            elif name.endswith(".pfm"):
                self.modules[name[:-4]] = body
            elif name.endswith(".json"):
                self.sidecars[name[:-5]] = body
            else:
                return h.reply(400, {"ok": False, "error": "only .pfm, .json or catalog.txt accepted"})
            return h.reply(200, {"ok": True, "slug": name.rsplit(".", 1)[0], "bytes": len(body)})
        if path == "/api/patterns/delete" and method == "POST":
            slugs = [line.strip() for line in body.decode().splitlines() if line.strip()]
            if slugs == ["*"]:
                slugs = list(self.modules)
            removed = 0
            for slug in slugs:
                removed += 1 if self.modules.pop(slug, None) is not None else 0
                self.sidecars.pop(slug, None)
            return h.reply(200, {"ok": True, "removed": removed, "missing": len(slugs) - removed})
        return h.reply(404, {"error": "no such route"})

    def stock(self, *slugs):
        for slug in slugs:
            self.modules[slug] = f"PFM:{slug}".encode()
            self.sidecars[slug] = json.dumps({"name": slug.title(), "slug": slug}).encode()
        return self

    def listed(self):
        """The module slugs in the panel's index order, as /api/patterns gives them."""
        data = panels.pattern_list(self.address)
        return data["modules"] if data else None


class Hub:
    def __init__(self, rows):
        body = json.dumps({"panels": rows}).encode()

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                reply = body if self.path == "/api/panels" else b'{"fw":"0.1.0"}' if self.path == "/api/status" else None
                self.send_response(200 if reply else 404)
                self.send_header("Content-Length", str(len(reply) if reply else 0))
                self.end_headers()
                if reply:
                    self.wfile.write(reply)

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

    # --connect has to outlast a restart (2026-11-14: a panel restarted from
    # the console went back to the studio Wi-Fi, because connect=1 alone
    # leaves the boot preference where it was).
    roam = Panel()
    code, out = run("wifi-add", "PatternflowEnsemble", roam.address, "--pass", "pw", "--connect")
    check(code == 0 and roam.current == "PatternflowEnsemble", f"--connect moves the panel now\n{out}")
    check(roam.networks[roam.boot_idx] == "PatternflowEnsemble", "and makes the next boot start there")
    run("reboot", roam.address)
    time.sleep(1.8)
    wifi_now = panels.get_json(roam.address, "/api/wifi", timeout=1.0) or {}
    check(wifi_now.get("current") == "PatternflowEnsemble", f"after a restart it is still there: {wifi_now}")
    code, out = run("wifi-add", "Venue", roam.address, "--pass", "pw")
    check(code == 0 and roam.networks[roam.boot_idx] == "PatternflowEnsemble" and roam.current == "PatternflowEnsemble",
          "storing another network without --connect changes neither")
    code, out = run("wifi-add", "PatternflowEnsemble", roam.address, "--pass", "pw", "--connect")
    check(code == 0 and "이미 그 네트워크에 있음" in out, f"run again where it already is, it only fixes the preference\n{out}")

    # ── Addresses after an option are still panels (Python before 3.12) ─
    q1 = Panel()
    code, out = run("wifi-add", "Late", "--connect", "--pass", "pw", q1.address)
    check(code == 0 and "Late" in q1.wifi, f"an address after the options is a panel\n{out}")
    code, out = run("wifi-add", "Late", "--pass", "pw", q1.address, "--bogus")
    check(code != 0, "an unknown option is still refused")
    eof = Panel()
    panels.input = lambda prompt="": (_ for _ in ()).throw(EOFError())
    code, out = run("flash", str(good), eof.address)
    check(code == 1 and eof.uploads == 0 and "-y" in out, f"no terminal to ask on is a no, said plainly\n{out}")
    del panels.input

    # ── Patterns: pull a pack, push it, and the numbers mean the same ────
    ref = Panel().stock("aurora", "bloom", "comet", "drift")
    ref.catalog = ["drift", "aurora"]                       # drift first, then aurora, then the rest by name
    other = Panel().stock("aurora", "comet", "xylo")
    bare = Panel()                                          # Origin only
    check(ref.listed() == ["drift", "aurora", "bloom", "comet"], "the stand-in orders like the panel")
    code, out = run("patterns", ref.address, other.address, bare.address)
    check(code == 1 and out.count("다름") == 2 and "같음" in out, f"patterns says who differs\n{out}")
    code, out = run("patterns", ref.address, other.address, "-v")
    check("없는 것: drift, bloom" in out and "Xylo" in out, f"-v names what is missing and what is extra\n{out}")

    pack = tmp / "pack"
    code, out = run("patterns-pull", ref.address, "-o", str(pack))
    check(code == 0 and "모듈 4개" in out, f"pull takes the four modules\n{out}")
    check((pack / "catalog.txt").read_text().split() == ["drift", "aurora", "bloom", "comet"], "in the panel's order")
    check(sorted(x.name for x in pack.glob("*.pfm")) == ["aurora.pfm", "bloom.pfm", "comet.pfm", "drift.pfm"]
          and (pack / "drift.json").exists(), "each .pfm with its sidecar")
    info = json.loads((pack / "pack.json").read_text(encoding="utf-8"))
    check(info["presets"] == ["Origin"] and info["from"] == ref.address, "pack.json says where from and the presets")
    code, out = run("patterns-pull", bare.address, "-o", str(tmp / "nothing"))
    check(code != 0 and "모듈이 없습니다" in out, "a panel with no modules makes no pack")

    backup = tmp / "backup"
    code, out = run("patterns-push", "--pack", str(pack), other.address, bare.address, "-y", "--backup", str(backup))
    check(code == 0 and "맞춤 2대, 실패 0대" in out, f"push makes two panels match\n{out}")
    check(other.listed() == bare.listed() == ref.listed(), "every number now means the same pattern")
    check("xylo" not in other.modules and list(backup.glob("*/xylo.pfm")), "the extra one is off the panel, and kept here")
    check(sorted(p for p in other.puts if p.endswith(".pfm")) == ["bloom.pfm", "drift.pfm"] and other.puts[-1] == "catalog.txt",
          f"only the missing modules travel, the order last: {other.puts}")
    check(other.sidecars.get("drift") == ref.sidecars["drift"], "sidecars go with them")
    sent = len(other.puts)
    code, out = run("patterns-push", "--pack", str(pack), other.address, "-y")
    check(code == 0 and "이미 같음" in out and len(other.puts) == sent, "a second push sends nothing")
    code, out = run("patterns", "--pack", str(pack), ref.address, other.address, bare.address)
    check(code == 0 and out.count("같음") == 3, f"and patterns agrees\n{out}")

    # A panel whose memory is too broken up for the largest module (it has
    # been on a while) would cut that upload off every time. It is restarted
    # first, and then takes everything; one with room is left alone.
    biggest = max((pack / f"{s}.pfm").stat().st_size for s in ("drift", "aurora", "bloom", "comet"))
    tight = Panel(heap_largest=biggest - 1)
    roomy = Panel(heap_largest=biggest + 50_000)
    code, out = run("patterns-push", "--pack", str(pack), tight.address, roomy.address, "-y")
    check(code == 0 and "맞춤 2대, 실패 0대" in out and "다시 켭니다" in out, f"a panel short of memory is restarted and then filled\n{out}")
    check(tight.restarts == 1 and roomy.restarts == 0 and tight.listed() == roomy.listed() == ref.listed(),
          f"only the one that needed it: {tight.restarts}, {roomy.restarts}")
    sizes = [len(tight.modules[p[:-4]]) for p in tight.puts if p.endswith(".pfm")]
    check(sizes == sorted(sizes, reverse=True) and tight.puts[-1] == "catalog.txt", f"the largest modules go first: {sizes}")

    reorder = Panel().stock("aurora", "bloom", "comet", "drift")
    code, out = run("patterns-push", "--pack", str(pack), reorder.address, "-y")
    check(code == 0 and "순서만 맞춤" in out and reorder.puts == ["catalog.txt"], f"same set, other order: only the order goes\n{out}")
    check(reorder.listed() == ref.listed(), "and the order is the pack's")

    other_image = Panel().stock("aurora")
    other_image.presets = ["Origin", "Clock Face"]
    code, out = run("patterns-push", "--pack", str(pack), other_image.address, "-y")
    check(code == 1 and "이미지에 든 패턴이 다릅니다" in out and other_image.puts == [],
          f"a panel whose image holds other presets is left alone\n{out}")

    keep = Panel().stock("zed")
    panels.input = lambda prompt="": "n"
    code, out = run("patterns-push", "--pack", str(pack), keep.address)        # no -y, and the answer is no
    del panels.input
    check(code == 1 and "zed" in keep.modules and keep.puts == [], "without a yes nothing is removed")

    here = os.getcwd()
    os.chdir(tmp)
    fresh_pair = [Panel(), Panel().stock("comet")]
    code, out = run("patterns-push", "--from", ref.address, ref.address, *[p.address for p in fresh_pair], "-y")
    os.chdir(here)
    check(code == 0 and all(p.listed() == ref.listed() for p in fresh_pair) and ref.puts == [],
          f"--from copies one panel's set to the others and leaves the source alone\n{out}")

    # ── A board fresh from the flasher: its storage is formatted first ───
    fresh_board = Panel()
    fresh_board.fs_error = "not formatted"
    broken = Panel().stock("aurora")
    broken.fs_error = "boot sector looks valid but does not mount"
    code, out = run("patterns-push", "--pack", str(pack), fresh_board.address, broken.address, "-y")
    check(fresh_board.formats == 1 and fresh_board.listed() == ref.listed() and "저장소를 처음 포맷" in out,
          f"a never-formatted storage is formatted, then filled\n{out}")
    check(broken.formats == 0 and broken.puts == [] and "boot sector looks valid" in out and code == 1,
          f"a storage that will not mount for another reason is left alone\n{out}")

    # ── Every pattern any panel has, on every panel ─────────────────────
    u1 = Panel().stock("aurora", "comet")
    u1.catalog = ["comet", "aurora"]
    u2 = Panel().stock("aurora", "zinc", "basalt")
    u3 = Panel()                                            # Origin only
    os.chdir(tmp)
    code, out = run("patterns-push", "--union", u1.address, u2.address, u3.address, "-y")
    os.chdir(here)
    union = ["comet", "aurora", "basalt", "zinc"]           # the first panel's order, then what the next adds, by name
    check(code == 0 and all(p.listed() == union for p in (u1, u2, u3)), f"--union gives everyone everything\n{out}")
    check("지웁니다" not in out and all(set(p.modules) == set(union) for p in (u1, u2, u3)), "and takes nothing off")
    check((tmp / "pack-all" / "catalog.txt").read_text().split() == union, "the union pack is kept")
    check(u2.modules["zinc"] == b"PFM:zinc" and u1.modules["zinc"] == b"PFM:zinc", "each module travels as it was")
    os.chdir(tmp)
    code, out = run("patterns-push", "--union", u1.address, u2.address, u3.address, "-y")
    os.chdir(here)
    check(code == 0 and out.count("이미 같음") == 3, f"run again, nothing to do\n{out}")
    merged = tmp / "merged"
    run("patterns-pull", u1.address, "-o", str(merged))
    extra = Panel().stock("aurora", "quartz")
    code, out = run("patterns-pull", extra.address, "-o", str(merged), "--merge")
    check(code == 0 and (merged / "catalog.txt").read_text().split() == union + ["quartz"],
          f"--merge adds only what the pack lacks, after it\n{out}")
    odd = Panel().stock("quartz")
    odd.presets = ["Origin", "Clock Face"]
    code, out = run("patterns-pull", odd.address, "-o", str(merged), "--merge")
    check(code != 0 and "이미지에 든 패턴이 묶음과 다릅니다" in out, "a panel of another image is not merged")

    # ── --hub-wifi: on the hub's network for one command, then back ─────
    calls = []
    saved = (panels.wifi_current, panels.wifi_add_profile, panels.wifi_connect, panels.wifi_leave,
             panels.HUB_SECRETS, panels.HUB_HOTSPOT_ADDRESS, sys.platform)
    where = {"now": "Studio"}
    panels.wifi_current = lambda: where["now"]
    panels.wifi_add_profile = lambda ssid, pw: calls.append(("add", ssid, pw)) or True
    def connect(ssid):
        calls.append(("connect", ssid))
        where["now"] = ssid
    panels.wifi_connect = connect
    def leave(previous, ssid, added):
        calls.append(("leave", previous, ssid, added))
        where["now"] = previous
    panels.wifi_leave = leave
    hw1, hw2 = Panel().stock("aurora"), Panel().stock("aurora", "comet")
    hub_w = Hub([{"id": 1, "uid": "a", "on": 1, "ip": hw1.address}, {"id": 2, "uid": "b", "on": 1, "ip": hw2.address}])
    wsecrets = tmp / "hub_secrets_w.h"
    wsecrets.write_text('#define HUB_AP_SSID "PatternflowEnsemble"\n#define HUB_AP_PASS "hub-pass-1"\n', encoding="utf-8")
    panels.HUB_SECRETS = wsecrets
    panels.HUB_HOTSPOT_ADDRESS = hub_w.address
    sys.platform = "win32"
    try:
        os.chdir(tmp)
        code, out = run("--hub-wifi", "patterns-push", "--union", "-y")
        os.chdir(here)
        check(code == 0 and hw1.listed() == hw2.listed() == ["aurora", "comet"],
              f"--hub-wifi: every panel the hub knows, without naming one\n{out}")
        check(calls[0] == ("add", "PatternflowEnsemble", "hub-pass-1") and calls[1] == ("connect", "PatternflowEnsemble")
              and calls[-1] == ("leave", "Studio", "PatternflowEnsemble", True) and where["now"] == "Studio",
              f"joins the hub's Wi-Fi from hub_secrets.h and goes back: {calls}")
        calls.clear()
        def broken(ssid):
            raise SystemExit("no hub")
        panels.wifi_connect = broken
        code, out = run("--hub-wifi", "status")
        check(code != 0 and calls and calls[-1][0] == "leave" and where["now"] == "Studio",
              f"when the hub's Wi-Fi is not there, it still goes back: {calls}")
        sys.platform = "linux"
        code, out = run("--hub-wifi", "status")
        check(code != 0 and "Windows" in out, "elsewhere it says how to do it by hand")
    finally:
        (panels.wifi_current, panels.wifi_add_profile, panels.wifi_connect, panels.wifi_leave,
         panels.HUB_SECRETS, panels.HUB_HOTSPOT_ADDRESS, sys.platform) = saved

    # ── join: image, patterns, then the hub's Wi-Fi ─────────────────────
    secrets = tmp / "hub_secrets.h"
    secrets.write_text('#define HUB_AP_SSID "PatternflowEnsemble"\n#define HUB_AP_PASS "hub-pass-1"\n'
                       '// #define HUB_STA_SSID "venue-router"\n// #define HUB_STA_PASS "x"\n', encoding="utf-8")
    check(panels.hub_network(secrets) == ("PatternflowEnsemble", "hub-pass-1"), "hotspot mode: the hotspot")
    router = tmp / "router.h"
    router.write_text('#define HUB_AP_SSID "PatternflowEnsemble"\n#define HUB_AP_PASS "hub-pass-1"\n'
                      '#define HUB_STA_SSID "venue-router"\n#define HUB_STA_PASS "venue-pass"\n', encoding="utf-8")
    check(panels.hub_network(router) == ("venue-router", "venue-pass"), "router mode: the router")

    n1, n2 = Panel().stock("xylo"), Panel()
    code, out = run("join", str(good), n1.address, n2.address, "--pack", str(pack), "--hub-secrets", str(secrets),
                    "-y", "--backup", str(backup))
    check(code == 0 and "허브로 간 패널 2대 / 2대" in out, f"join brings two panels in\n{out}")
    check(all(p.variant == "ensemble" and p.listed() == ref.listed() for p in (n1, n2)), "each on the image, with the pack")
    check(all(p.wifi.get("PatternflowEnsemble") == {"pass": "hub-pass-1", "connect": True} for p in (n1, n2)),
          "and told to move to the hub's network")
    uploads = (n1.uploads, n2.uploads)
    code, out = run("join", str(good), n1.address, "--pack", str(pack), "--ssid", "PatternflowEnsemble", "--pass", "p", "-y")
    check(code == 0 and n1.uploads == uploads[0] and "이미 같음" in out, f"a second join redoes nothing it needs not\n{out}")

    stuck = Panel(never_back=True)
    code, out = run("join", str(good), stuck.address, "--pack", str(pack), "--ssid", "S", "--pass", "p", "-y")
    check(code == 1 and stuck.wifi == {} and "멈춥니다" in out, f"a panel that did not come back is not moved\n{out}")

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
