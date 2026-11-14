#!/usr/bin/env python3
"""Do one thing to every panel, over Wi-Fi.

  python ensemble/tools/panels.py scan -o panels.txt        find the panels on this network
  python ensemble/tools/panels.py status                    what each one is running
  python ensemble/tools/panels.py flash firmware.bin        put an image on each (in, or back out)
  python ensemble/tools/panels.py wifi-add "Name" --connect teach each one a Wi-Fi network
  python ensemble/tools/panels.py wifi-forget "Name"
  python ensemble/tools/panels.py reboot
  python ensemble/tools/panels.py patterns                  whose patterns differ from whose
  python ensemble/tools/panels.py patterns-pull <panel> -o pack
                                                            one panel's patterns, in order, to a folder
  python ensemble/tools/panels.py patterns-push --pack pack every panel gets exactly that set and order
  python ensemble/tools/panels.py patterns-push --union     every panel gets every pattern any of them has
  python ensemble/tools/panels.py join firmware.bin --pack pack
                                                            new panels in one go: image, patterns,
                                                            then the hub's Wi-Fi (from hub_secrets.h)

Which panels: addresses on the command line, or --hub <hub address> (the
panels the hub knows; --only 1,3 picks by number), or --file <list>, or
panels.txt in the current folder. An address is an IP or a name, with
:port if it is not 80. A list has one address a line; # starts a comment.

`flash` is the careful one, because an image that does not boot cannot be
taken off again over Wi-Fi:

  - it refuses a file that is not an ESP32-S3 app image, is too big for the
    panel's 3 MB slot, or has no /update in it (the hub's image, say);
  - it skips a panel already running that exact image, so running it again
    after a failure touches only the panels still to do;
  - it does the first panel alone and watches it come back and stay up
    before going on, and stops at the first panel that does not come back.

The ensemble sends a pattern by its number, and a panel numbers its patterns
by the order they sit in (Origin first, then the modules alphabetically or as
/patterns/catalog.txt lists them). Panels holding different sets therefore
show different patterns under one number. `patterns-push` makes every panel
hold the same modules in the same order: it sends what is missing, takes off
what the pack does not have (keeping a copy on this computer first), and
writes catalog.txt so the order is the pack's.

It talks to the panel's own HTTP API (Patternflow docs/rest-api.md): GET
/api/status, POST /update, /api/wifi, /api/patterns. One request at a time
per panel - the panel's server takes a single connection. Python 3.8+,
nothing to install.

Not tried on hardware yet: ensemble/README.md section 8.
"""
import argparse
import concurrent.futures
import getpass
import http.client
import json
import locale
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unicodedata
import urllib.parse
from pathlib import Path

DEFAULT_LIST = "panels.txt"
SLOT_BYTES = 0x300000            # each of the panel's two app slots (partitions/app3M_fat9M_16MB.csv)
CHIP_ESP32S3 = 9                 # esp_image_header_t.chip_id
APP_DESC_MAGIC = bytes.fromhex("3254cdab")   # esp_app_desc_t.magic_word 0xABCD5432, little-endian
WAY_OUT = b"/update/status"      # a route only an image that carries the panel's /update has
UPLOAD_TIMEOUT_S = 180           # what Patternflow's own build.sh allows an upload
REBOOT_WAIT_S = 2.5              # the panel answers the upload, then restarts 1.2 s later
BACK_TIMEOUT_S = 75              # reboot, rejoin Wi-Fi, answer again
SOAK_S = 10                      # how long the first panel has to stay up before the rest follow


# ── Talking to one panel ────────────────────────────────────────────────
def split_address(address):
    """'192.168.0.23', 'patternflow-a1b2.local:8080' -> (host, port)."""
    host, _, port = address.partition(":")
    return host, int(port) if port else 80


def request(address, method, path, body=None, headers=None, timeout=5.0, progress=None):
    """One HTTP request on a connection of its own. Returns (status, body) or (None, why)."""
    host, port = split_address(address)
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.putrequest(method, path)
        conn.putheader("Connection", "close")
        for key, value in (headers or {}).items():
            conn.putheader(key, value)
        if body is not None:
            conn.putheader("Content-Length", str(len(body)))
        conn.endheaders()
        if body is not None:
            step = 16 * 1024
            for start in range(0, len(body), step):
                conn.send(body[start:start + step])
                if progress:
                    progress(min(start + step, len(body)), len(body))
        response = conn.getresponse()
        return response.status, response.read()
    except (OSError, http.client.HTTPException) as error:
        return None, str(error) or error.__class__.__name__
    finally:
        conn.close()


def get_json(address, path, timeout=5.0, tries=1):
    """A JSON object from a panel, or None. With tries > 1 a failed connection
    (not an answer) is tried again after a moment."""
    for attempt in range(tries):
        status, body = request(address, "GET", path, timeout=timeout)
        if status is not None or attempt == tries - 1:
            break
        time.sleep(1.0)
    if status != 200:
        return None
    try:
        value = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        return None
    return value if isinstance(value, dict) else None


def post_form(address, path, fields, timeout=8.0, method="POST", tries=1):
    """A form POST. With tries > 1 it is sent again, after a moment, while the
    connection itself fails (never after the panel answered, even with an error)."""
    body = urllib.parse.urlencode(fields).encode()
    for attempt in range(tries):
        status, reply = request(address, method, path, body=body,
                                headers={"Content-Type": "application/x-www-form-urlencoded"}, timeout=timeout)
        if status is not None:
            break
        if attempt < tries - 1:
            time.sleep(1.5)
    if status is None:
        return False, reply
    text = reply.decode("utf-8", "replace").strip()
    try:
        parsed = json.loads(text)
    except ValueError:
        parsed = {}
    if status == 200 and parsed.get("ok", True) is not False:
        return True, text
    return False, parsed.get("error") or f"HTTP {status} {text[:120]}"


def is_panel(status):
    return bool(status) and "version" in status and ("variant" in status or "panel" in status)


def describe(status):
    """A panel's status as the few words a table has room for."""
    if not status:
        return {"name": "", "image": "", "build": "", "up": "", "note": ""}
    hotspot = status.get("hotspot") or {}
    variant = status.get("variant", "")
    image = " ".join(x for x in (variant, status.get("variantVersion", "")) if x)
    image = f"{image} ({status.get('version', '?')})" if image else str(status.get("version", "?"))
    up = status.get("uptime")
    if isinstance(up, (int, float)):
        up = f"{int(up) // 3600}:{int(up) // 60 % 60:02d}:{int(up) % 60:02d}"
    notes = []
    ensemble = status.get("ensemble")
    if isinstance(ensemble, dict):
        notes.append(f"허브 {ensemble.get('hub') or '없음'}" + (f", {ensemble.get('id')}번" if ensemble.get("id") else ""))
    if status.get("crash"):
        notes.append("crash 기록 있음")
    if status.get("sleep"):
        notes.append("절전")
    return {"name": hotspot.get("ssid", ""), "image": image, "build": status.get("build", ""),
            "up": up or "", "note": ", ".join(notes)}


# ── Which panels ────────────────────────────────────────────────────────
def read_list(path):
    out = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line.split()[0])
    return out


def from_hub(hub, only):
    data = get_json(hub, "/api/panels", timeout=5.0)
    if data is None or not isinstance(data.get("panels"), list):
        raise SystemExit(f"허브 {hub} 가 /api/panels 에 답하지 않습니다. 허브와 같은 네트워크에 있는지, 허브 펌웨어가 이 주소를 가진 판인지 확인하세요.")
    out = []
    for p in data["panels"]:
        if only and p.get("id") not in only:
            continue
        if not p.get("on") or p.get("ip") in (None, "", "0.0.0.0"):
            print(f"  {p.get('id')}번 패널({p.get('uid')})은 지금 허브에 붙어 있지 않아 건너뜁니다.")
            continue
        out.append(p["ip"])
    return out


def targets(args):
    if args.panels:
        found = list(args.panels)
    elif args.hub:
        only = {int(x) for x in args.only.split(",")} if args.only else None
        found = from_hub(args.hub, only)
    elif args.file:
        found = read_list(args.file)
    elif Path(DEFAULT_LIST).exists():
        found = read_list(DEFAULT_LIST)
    else:
        raise SystemExit("패널을 알려 주세요: 주소를 적거나, --hub <허브 주소>, --file <목록>, "
                         f"또는 이 폴더의 {DEFAULT_LIST}.\n먼저 찾으려면:  panels.py scan -o {DEFAULT_LIST}")
    seen, unique = set(), []
    for address in found:
        if address not in seen:
            seen.add(address)
            unique.append(address)
    if not unique:
        raise SystemExit("대상 패널이 없습니다.")
    return unique


def shown_width(text):
    """Columns a string takes on a terminal: a Korean syllable takes two."""
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def table(rows, headers):
    cells = [[str(c) for c in row] for row in [headers] + rows]
    widths = [max(shown_width(row[i]) for row in cells) for i in range(len(headers))]
    for row in cells:
        print("  " + "  ".join(c + " " * (w - shown_width(c)) for c, w in zip(row, widths)).rstrip())


def confirm(question):
    """Ask, and take only a yes. No terminal to ask on (a script, a pipe) is a no."""
    try:
        answer = input(question)
    except EOFError:
        print("\n물어볼 수 없어서 그만둡니다. 묻지 않고 하려면 -y 를 붙이세요.")
        return False
    if answer.strip().lower() in ("y", "yes", "ㅛ"):
        return True
    print("그만둡니다.")
    return False


# ── scan ────────────────────────────────────────────────────────────────
def own_address():
    """This computer's address on the network it would use to reach the LAN."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("10.255.255.255", 1))   # nothing is sent: UDP connect only picks a route
        return probe.getsockname()[0]
    except OSError:
        return None
    finally:
        probe.close()


def cmd_scan(args):
    subnet = args.subnet
    if not subnet:
        mine = own_address()
        if not mine or mine.startswith("127."):
            raise SystemExit("이 컴퓨터의 네트워크 주소를 알 수 없습니다. --subnet 192.168.0 처럼 앞 세 자리를 적어 주세요.")
        subnet = mine.rsplit(".", 1)[0]
    subnet = subnet.rstrip(".")
    port = f":{args.port}" if args.port != 80 else ""
    print(f"{subnet}.1 ~ {subnet}.254 에서 패널을 찾습니다 ...")
    hosts = [f"{subnet}.{n}{port}" for n in range(1, 255)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        answers = list(pool.map(lambda h: get_json(h, "/api/status", timeout=args.timeout), hosts))
    found = [(h, s) for h, s in zip(hosts, answers) if is_panel(s)]
    if not found:
        print("패널을 찾지 못했습니다. 이 컴퓨터가 패널과 같은 Wi-Fi에 있는지, 손님용 네트워크가 아닌지 확인하세요.")
        return 1
    rows = []
    for host, status in found:
        d = describe(status)
        rows.append([host, d["name"], d["image"], d["build"], d["note"]])
    table(rows, ["주소", "이름", "이미지", "빌드", ""])
    print(f"패널 {len(found)}대.")
    if args.output:
        lines = [f"{host}  # {describe(status)['name']} {describe(status)['image']}".rstrip() for host, status in found]
        Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"{args.output} 에 적었습니다.")
    return 0


# ── status ──────────────────────────────────────────────────────────────
def cmd_status(args):
    rows, missing = [], 0
    for address in targets(args):
        status = get_json(address, "/api/status", timeout=args.timeout, tries=3)
        if not is_panel(status):
            rows.append([address, "", "답 없음", "", "", ""])
            missing += 1
            continue
        d = describe(status)
        rows.append([address, d["name"], d["image"], d["build"], d["up"], d["note"]])
    table(rows, ["주소", "이름", "이미지", "빌드", "켜진 지", ""])
    return 1 if missing else 0


# ── flash ───────────────────────────────────────────────────────────────
def inspect_image(data, force):
    """Why this file may not go on a panel, or None. Also the build it will report."""
    problems = []
    if len(data) < 64 * 1024 or data[0] != 0xE9 or data[0x20:0x24] != APP_DESC_MAGIC:
        problems.append("ESP32 앱 이미지가 아닙니다 (firmware.bin 이어야 합니다. bootloader.bin 이나 .zip 은 아닙니다).")
    else:
        chip = int.from_bytes(data[12:14], "little")
        if chip != CHIP_ESP32S3:
            problems.append(f"ESP32-S3용 이미지가 아닙니다 (칩 번호 {chip}).")
        if len(data) > SLOT_BYTES:
            problems.append(f"패널의 앱 자리(3 MB)보다 큽니다: {len(data)} 바이트.")
        if WAY_OUT not in data:
            problems.append("이 이미지에는 /update 가 없습니다. 올리면 Wi-Fi로 되돌릴 길이 없어집니다 (허브 펌웨어를 고른 것은 아닌가요?).")
    build = data[0xB0:0xB4].hex() if len(data) > 0xB4 else ""
    if build == "00000000":
        build = ""   # built without the ELF hash: the panel then reports a hash of its compile time
    if problems and not force:
        return problems, build
    return [], build


def upload(address, data, show):
    boundary = "----pfe" + os.urandom(12).hex()
    head = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n").encode()
    body = head + data + f"\r\n--{boundary}--\r\n".encode()
    last = [-1]

    def progress(sent, total):
        pct = sent * 100 // total
        if show and pct != last[0] and pct % 5 == 0:
            last[0] = pct
            print(f"\r    올리는 중 {pct:3d}%", end="", flush=True)

    status, reply = request(address, "POST", f"/update?size={len(data)}", body=body,
                            headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
                            timeout=UPLOAD_TIMEOUT_S, progress=progress)
    if show:
        print("\r" + " " * 24 + "\r", end="")
    if status == 200:
        return None
    if status is None:
        # The panel can say what it saw of an upload whose connection died.
        after = get_json(address, "/update/status", timeout=4.0) or {}
        detail = after.get("lastError") or f"{after.get('received', '?')}/{after.get('expected', '?')} 바이트"
        return f"올리다 끊김 ({reply}; 패널: {detail})"
    text = reply.decode("utf-8", "replace")
    try:
        text = json.loads(text).get("error", text)
    except ValueError:
        pass
    if status == 403:
        return "패널이 잠겨 있습니다: 패널에서 K2를 길게 눌러 NETWORK, K4를 돌려 UPDATE 화면을 연 뒤 다시 하세요."
    return f"패널이 거절 (HTTP {status}): {text[:160]}"


def wait_back(address, expected_build, old_build):
    """After an upload the panel restarts. Returns (its status, None) or (status or None, problem)."""
    timeout = BACK_TIMEOUT_S
    started = time.monotonic()
    time.sleep(REBOOT_WAIT_S)
    went_away = False
    while time.monotonic() - started < timeout:
        status = get_json(address, "/api/status", timeout=3.0)
        if not is_panel(status) or status.get("busy") == "update":
            went_away = went_away or not is_panel(status)
            time.sleep(1.0)
            continue
        build, uptime = status.get("build", ""), status.get("uptime")
        restarted = went_away or (isinstance(uptime, (int, float)) and uptime < 45)
        if expected_build and build:
            if build == expected_build:
                return status, None
            if restarted:
                return status, f"다시 켜졌지만 다른 이미지입니다 (빌드 {build}, 올린 것은 {expected_build})"
        elif restarted:
            if old_build and build and build == old_build:
                return status, "다시 켜졌지만 이미지가 바뀌지 않았습니다"
            return status, None
        time.sleep(1.0)
    return None, f"{timeout}초 안에 돌아오지 않았습니다"


def stays_up(address, first):
    """The first panel: is it still there, and still the same boot, a little later?"""
    time.sleep(SOAK_S)
    later = get_json(address, "/api/status", timeout=4.0)
    if not is_panel(later):
        return "돌아왔다가 다시 사라졌습니다"
    before, after = first.get("uptime"), later.get("uptime")
    reason = later.get("resetReason")
    if isinstance(before, (int, float)) and isinstance(after, (int, float)) and after < before + SOAK_S / 2:
        return f"켜진 지 {before}초였다가 {after}초가 됐습니다: 혼자 다시 켜지고 있습니다 ({reason or '?'})"
    # After an update the board says it restarted in software. Anything else
    # this soon is the board restarting on its own.
    if reason not in (None, "", "sw", "poweron"):
        return f"스스로 다시 켜졌습니다 ({reason})"
    if later.get("crash") and not first.get("crash"):
        return "crash 기록이 생겼습니다"
    return None


def cmd_flash(args):
    path = Path(args.image)
    if not path.is_file():
        raise SystemExit(f"{path} 가 없습니다.")
    data = path.read_bytes()
    problems, build = inspect_image(data, args.force_image)
    if problems:
        print(f"{path} 는 올리지 않습니다:")
        for problem in problems:
            print("  - " + problem)
        print("그래도 올리려면 --force-image (권하지 않습니다).")
        return 2
    addresses = targets(args)
    print(f"이미지: {path}  {len(data)} 바이트" + (f", 빌드 {build}" if build else ""))

    plan, rows = [], []
    for address in addresses:
        status = get_json(address, "/api/status", timeout=args.timeout, tries=3)
        d = describe(status)
        if not is_panel(status):
            rows.append([address, "", "답 없음", "건너뜀"])
        elif build and status.get("build") == build:
            rows.append([address, d["name"], d["image"], "이미 이 이미지"])
        else:
            rows.append([address, d["name"], d["image"], "올림"])
            plan.append((address, status))
    table(rows, ["주소", "이름", "지금", "할 일"])
    unreachable = sum(1 for r in rows if r[3] == "건너뜀")
    if not plan:
        print("올릴 패널이 없습니다." + (f" (답 없는 패널 {unreachable}대)" if unreachable else ""))
        return 1 if unreachable else 0
    if not args.yes and not confirm(f"패널 {len(plan)}대에 올립니다. 첫 대를 먼저 올려 켜지는지 본 뒤 나머지를 올립니다. 계속할까요? [y/N] "):
        return 1

    done, failed, stopped = [], [], False
    for index, (address, before) in enumerate(plan):
        print(f"[{index + 1}/{len(plan)}] {address}")
        # A running show makes the upload fail without saying so (Patternflow's build.sh does the same).
        request(address, "POST", "/api/shows/control", body=b"op=stop",
                headers={"Content-Type": "application/x-www-form-urlencoded"}, timeout=3.0)
        problem = upload(address, data, show=sys.stdout.isatty())
        came_back = True
        if not problem:
            after, problem = wait_back(address, build, before.get("build", ""))
            came_back = after is not None
            if not problem and index == 0 and len(plan) > 1 and not args.no_soak:
                print(f"    첫 대가 켜졌습니다. {SOAK_S}초 지켜봅니다 ...")
                problem = stays_up(address, after)
                came_back = problem is None
            if not problem:
                d = describe(after)
                print(f"    됐습니다: {d['image']}" + (f"  {d['note']}" if d["note"] else ""))
                done.append(address)
                continue
        print(f"    실패: {problem}")
        failed.append((address, problem))
        if not came_back and not args.keep_going:
            # An image that does not boot cannot be taken off over Wi-Fi.
            # Do not put it on a second panel.
            stopped = True
            break

    left = [a for a, _ in plan[len(done) + len(failed):]]
    print()
    print(f"올림 {len(done)}대, 실패 {len(failed)}대" + (f", 손대지 않음 {len(left)}대" if left else "") +
          (f", 답 없음 {unreachable}대" if unreachable else ""))
    for address, problem in failed:
        print(f"  {address}: {problem}")
    if stopped:
        print("켜지지 않는 패널이 나와서 멈췄습니다. 그 패널은 USB로 다시 구워야 할 수 있습니다 (patternflow.work/flash).")
        print("이미지 탓이 아니라고 확인되면 같은 명령을 다시 돌리세요. 이미 올라간 패널은 건너뜁니다.")
    elif failed:
        print("같은 명령을 다시 돌리면 이미 올라간 패널은 건너뛰고 나머지만 올립니다.")
    return 1 if failed or unreachable else 0


# ── Wi-Fi and reboot ────────────────────────────────────────────────────
def each(args, label, act):
    bad = 0
    for address in targets(args):
        ok, detail = act(address)
        print(f"  {address}: {label if ok else '실패: ' + str(detail)}")
        bad += 0 if ok else 1
    return 1 if bad else 0


def post_retrying(address, path, fields, timeout=8.0):
    """A panel on a busy Wi-Fi now and then drops one request (2026-11-14: one
    timed out, the next was cut). The settings these send are the same
    whichever attempt lands, so a failed connection is tried twice more."""
    return post_form(address, path, fields, timeout=timeout, tries=3)


def move_to(address, ssid, password, connect, timeout=8.0):
    """Store a network on a panel. With connect, also make it the one every boot
    starts from, then switch to it now. Returns (ok, detail).

    connect=1 alone moves the panel for this boot only: the core keeps the
    boot preference (bootIdx) on the network it had, and follows that
    network when the list reorders (core_wifi.h addNetwork), so the next
    restart goes back to it. /api/wifi/boot names the slot, and it has to
    be sent before the switch - after it the panel is on another network."""
    ok, detail = post_retrying(address, "/api/wifi", {"ssid": ssid, "pass": password}, timeout=timeout)
    if not ok or not connect:
        return ok, detail
    saved = get_json(address, "/api/wifi", timeout=timeout) or {}
    names = [n.get("ssid") for n in saved.get("networks") or [] if isinstance(n, dict)]
    if ssid not in names:
        return False, "저장했다는데 패널의 목록에 없습니다"
    ok, detail = post_retrying(address, "/api/wifi/boot", {"bootIdx": names.index(ssid)}, timeout=timeout)
    if not ok:
        return False, f"다음 부팅에 먼저 찾을 네트워크로 정하지 못했습니다: {detail}"
    if saved.get("connected") and saved.get("current") == ssid:
        return True, "already"
    return post_retrying(address, "/api/wifi", {"ssid": ssid, "pass": password, "connect": "1"}, timeout=timeout)


def cmd_wifi_add(args):
    password = args.password
    if password is None:
        password = getpass.getpass(f'"{args.ssid}" 의 비밀번호 (열린 네트워크면 그냥 Enter): ')
    bad = 0
    for address in targets(args):
        ok, detail = move_to(address, args.ssid, password, args.connect)
        if not ok:
            label = "실패: " + str(detail)
        elif not args.connect:
            label = "저장함"
        elif detail == "already":
            label = "이미 그 네트워크에 있음. 다음 부팅에도 먼저 찾게 함"
        else:
            label = "저장하고 옮겨 감. 다음 부팅에도 먼저 찾음"
        print(f"  {address}: {label}")
        bad += 0 if ok else 1
    if args.connect:
        print("패널들이 그 네트워크로 옮겨 갑니다. 이 컴퓨터도 그 네트워크에 있어야 다시 닿습니다.")
    return 1 if bad else 0


def cmd_wifi_forget(args):
    path = "/api/wifi?" + urllib.parse.urlencode({"ssid": args.ssid})

    def forget(address):
        status, reply = request(address, "DELETE", path, timeout=8.0)
        if status == 200:
            return True, ""
        if status == 404:
            return True, ""      # it was not saved there: nothing to forget
        return False, reply if status is None else f"HTTP {status}"

    return each(args, "지움", forget)


def cmd_reboot(args):
    return each(args, "다시 켜는 중", lambda address: post_form(address, "/api/wifi/reboot", {}))


# ── patterns ────────────────────────────────────────────────────────────
# A pack is a folder: <slug>.pfm and its <slug>.json sidecar for every
# module, and catalog.txt, one slug a line in the order the panels are to
# number them. pack.json says where it came from and which presets (the
# patterns compiled into the image, Origin) sit in front of the modules.
CATALOG = "catalog.txt"
PACK_INFO = "pack.json"
RELOAD_TIMEOUT_S = 30            # a batch's rescan and reload, done in the panel's loop()
MODULE_ROOM_SPARE = 2048         # bytes of memory in one piece a panel should have beyond the module it is sent


def pattern_list(address, timeout=6.0):
    """(presets, module slugs, display names), each in the panel's index order; None if it does not answer."""
    data = get_json(address, "/api/patterns", timeout=timeout, tries=3)
    if not data or not isinstance(data.get("patterns"), list):
        return None
    entries = sorted(data["patterns"], key=lambda p: p.get("index", 0))
    presets = [p.get("name", "") for p in entries if not p.get("module")]
    modules = [p["module"] for p in entries if p.get("module")]
    names = {p["module"]: p.get("name", p["module"]) for p in entries if p.get("module")}
    return {"presets": presets, "modules": modules, "names": names,
            "mounted": data.get("mounted", True), "free": data.get("free")}


def read_pack(folder):
    folder = Path(folder)
    if not (folder / CATALOG).is_file():
        raise SystemExit(f"{folder} 에 {CATALOG} 가 없습니다. 패턴 묶음은 patterns-pull 로 만듭니다.")
    order = []
    for line in (folder / CATALOG).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and line not in order:
            order.append(line)
    missing = [slug for slug in order if not (folder / f"{slug}.pfm").is_file()]
    if missing:
        raise SystemExit(f"{folder} 에 이 모듈의 .pfm 이 없습니다: {', '.join(missing)}")
    info = {}
    if (folder / PACK_INFO).is_file():
        try:
            info = json.loads((folder / PACK_INFO).read_text(encoding="utf-8"))
        except ValueError:
            info = {}
    return order, info


def download(address, slug, ext):
    query = urllib.parse.urlencode({"slug": slug, "ext": ext})
    for attempt in range(3):              # a busy Wi-Fi drops one now and then
        status, body = request(address, "GET", f"/api/patterns/file?{query}", timeout=20.0)
        if status is not None:
            break
        time.sleep(1.5)
    return body if status == 200 else None


def save_modules(address, slugs, folder):
    """Copy these modules (.pfm and sidecar) off a panel into folder. Returns the slugs that failed."""
    folder.mkdir(parents=True, exist_ok=True)
    failed = []
    for slug in slugs:
        pfm = download(address, slug, "pfm")
        if pfm is None:
            failed.append(slug)
            continue
        (folder / f"{slug}.pfm").write_bytes(pfm)
        sidecar = download(address, slug, "json")
        if sidecar is not None:
            (folder / f"{slug}.json").write_bytes(sidecar)
    return failed


def make_room(address, size):
    """A panel receives a module into one piece of its memory, and one that
    has been on for a while may have no piece that long left: the upload is
    cut off, every time (2026-11-14, a 53 KB module and 51 KB in one piece,
    on a panel that had spent an hour as its own hotspot). A restart gives
    the memory back. Restarts the panel if it says it lacks the room.
    Returns a problem, or None."""
    before = get_json(address, "/api/status", tries=3) or {}
    largest = before.get("heapLargest")
    if not isinstance(largest, int) or largest >= size + MODULE_ROOM_SPARE:
        return None
    print(f"    가장 큰 모듈({size} 바이트)이 들어갈 메모리가 한 덩어리로 없습니다 (가장 큰 덩어리 {largest} 바이트). "
          "패널을 다시 켭니다 ...")
    post_form(address, "/api/wifi/reboot", {})
    started, gone, now = time.monotonic(), False, None
    while time.monotonic() - started < BACK_TIMEOUT_S:
        time.sleep(1.0)
        now = get_json(address, "/api/status", timeout=2.0)
        if now is None:
            gone = True
        elif gone or now.get("uptime", 0) < before.get("uptime", 0):
            break
        else:
            now = None
    if now is None:
        return f"다시 켠 패널이 {BACK_TIMEOUT_S}초 안에 돌아오지 않았습니다"
    largest = now.get("heapLargest")
    if isinstance(largest, int) and largest < size:
        return f"다시 켠 뒤에도 메모리가 모자랍니다 (한 덩어리 {largest} 바이트, 모듈 {size} 바이트)"
    return None


def put_file(address, name, data, last):
    status, reply = request(address, "PUT", "/api/patterns", body=data,
                            headers={"Content-Type": "application/octet-stream", "X-PF-Name": name,
                                     "X-PF-Last": "1" if last else "0"}, timeout=30.0)
    if status == 200:
        return None
    if status is None:
        return f"{name}: {reply}"
    text = reply.decode("utf-8", "replace")
    try:
        text = json.loads(text).get("error", text)
    except (ValueError, AttributeError):
        pass
    return f"{name}: HTTP {status} {str(text)[:120]}"


def compare(have, order, presets):
    """What stands between a panel's list and the pack's: (same, missing, extra, why)."""
    missing = [s for s in order if s not in have["modules"]]
    extra = [s for s in have["modules"] if s not in order]
    why = []
    if presets is not None and have["presets"] != presets:
        why.append(f"이미지에 든 패턴이 다릅니다 ({', '.join(have['presets']) or '없음'})")
    same = have["modules"] == order and not why
    return same, missing, extra, why


def cmd_patterns(args):
    """Compare: against a pack, or against the first panel that answers."""
    reference, ref_presets, ref_from = None, None, ""
    if args.pack:
        reference, info = read_pack(args.pack)
        ref_presets = info.get("presets")
        ref_from = f"묶음 {args.pack}"
    rows, lists, bad = [], [], 0
    for address in targets(args):
        have = pattern_list(address, timeout=args.timeout)
        if have is None:
            rows.append([address, "", "답 없음", ""])
            bad += 1
            continue
        if reference is None:
            reference, ref_presets, ref_from = have["modules"], have["presets"], address
        same, missing, extra, why = compare(have, reference, ref_presets)
        if same:
            note = "같음"
        else:
            bad += 1
            parts = why[:]
            if missing:
                parts.append(f"없는 것 {len(missing)}")
            if extra:
                parts.append(f"더 있는 것 {len(extra)}")
            if not missing and not extra and not why:
                parts.append("순서가 다름")
            note = "다름: " + ", ".join(parts)
        if not have["mounted"]:
            note += " (패턴 저장소가 안 열림)"
        rows.append([address, len(have["presets"]) + len(have["modules"]), len(have["modules"]), note])
        lists.append((address, have, missing, extra))
    print(f"기준: {ref_from}" + (f", 모듈 {len(reference)}개" if reference is not None else ""))
    table(rows, ["주소", "패턴", "모듈", ""])
    if args.verbose:
        for address, have, missing, extra in lists:
            if missing or extra:
                print(f"\n{address}")
                if missing:
                    print("  없는 것: " + ", ".join(missing))
                if extra:
                    print("  더 있는 것: " + ", ".join(have["names"].get(s, s) for s in extra))
    if bad:
        print("맞추려면:  panels.py patterns-push --pack <묶음>   (묶음은 patterns-pull <패널> -o <폴더>)")
    return 1 if bad else 0


def pull(address, folder, merge=False, allow_empty=False):
    """A pack from one panel. With merge, the panel's modules the pack does not
    have yet are added after the ones it has, and nothing in it is replaced.
    Returns (modules in the pack, modules added, failed slugs) or raises SystemExit."""
    have = pattern_list(address)
    if have is None:
        raise SystemExit(f"{address} 가 /api/patterns 에 답하지 않습니다.")
    if not have["modules"] and not allow_empty:
        raise SystemExit(f"{address} 에는 모듈이 없습니다 (이미지에 든 {', '.join(have['presets'])} 뿐).")
    folder = Path(folder)
    if folder.exists() and any(folder.iterdir()) and not (folder / CATALOG).exists():
        raise SystemExit(f"{folder} 는 비어 있지 않은 다른 폴더입니다. 새 폴더 이름을 주세요.")
    order, info = ([], {})
    if merge and (folder / CATALOG).exists():
        order, info = read_pack(folder)
        if info.get("presets") is not None and info["presets"] != have["presets"]:
            raise SystemExit(f"{address} 는 이미지에 든 패턴이 묶음과 다릅니다 "
                             f"({', '.join(have['presets'])}). 같은 이미지의 패널끼리만 합칩니다.")
    elif folder.exists():
        for old in list(folder.glob("*.pfm")) + list(folder.glob("*.json")):
            old.unlink()
    new = [s for s in have["modules"] if s not in order]
    print(f"{address}: 모듈 {len(have['modules'])}개" + (f", 묶음에 없는 {len(new)}개를 받습니다" if merge else
                                                     f"를 {folder} 로 받습니다") + " ...")
    failed = save_modules(address, new, folder) if new else []
    added = [s for s in new if s not in failed]
    order = order + added
    (folder / CATALOG).write_text("".join(f"{s}\n" for s in order), encoding="utf-8")
    status = get_json(address, "/api/status") or {}
    names = dict(zip(order, info.get("names") or [])) if info else {}
    names.update({s: have["names"].get(s, s) for s in added})
    sources = (info.get("from") or "").split(", ") if info.get("from") else []
    info = {"from": ", ".join(sources + [address]) if merge else address,
            "panel": (status.get("hotspot") or {}).get("ssid", "") if not merge else info.get("panel", ""),
            "version": status.get("version", "") if not info.get("version") else info["version"],
            "presets": have["presets"], "names": [names.get(s, s) for s in order],
            "at": time.strftime("%Y-%m-%d %H:%M:%S")}
    (folder / PACK_INFO).write_text(json.dumps(info, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return len(order), len(added), failed


def cmd_patterns_pull(args):
    count, added, failed = pull(args.source, args.output, merge=args.merge)
    print(f"묶음의 모듈 {count}개" + (f" (새로 {added}개)" if args.merge else "") + "." +
          (f" 받지 못한 것: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


def fresh_storage(address, have):
    """Whether this panel's pattern storage is unmounted because it was never
    formatted - a board straight from the web flasher (2026-11-14: three of
    them refused every module with "filesystem not mounted"). Returns
    (fresh, why) where why is the panel's own reason (fsError)."""
    if have["mounted"]:
        return False, ""
    status = get_json(address, "/api/status", tries=3) or {}
    why = status.get("fsError") or ""
    return why.startswith("not formatted"), why


def format_fresh(address):
    """Format a never-formatted pattern storage, as the panel's own /patterns
    page does (POST /api/patterns/format). There is nothing on it to lose.
    Returns the panel's pattern list once it mounts, or a problem string."""
    for _ in range(5):
        ok, detail = post_form(address, "/api/patterns/format", {}, timeout=120.0, tries=2)
        if ok:
            break
        if "retry" not in str(detail):        # 503 "pattern still loading; retry" passes
            return f"포맷하지 못했습니다: {detail}"
        time.sleep(2.0)
    else:
        return f"포맷하지 못했습니다: {detail}"
    for _ in range(30):
        now = pattern_list(address)
        if now and now["mounted"]:
            return now
        time.sleep(1.0)
    return "포맷했지만 패턴 저장소가 아직 열리지 않습니다"


def push_one(address, folder, order, presets, have, backup_root, resend):
    """Make one panel's modules the pack's. Returns a problem, or None."""
    if not have["mounted"]:
        fresh, why = fresh_storage(address, have)
        if not fresh:
            return f"패턴 저장소가 열리지 않습니다 ({why or '이유를 말하지 않음'}). 패널의 /patterns 화면을 보세요"
        print("    패턴 저장소가 한 번도 포맷되지 않았습니다 (공장 상태, 비어 있음). 포맷합니다 ...")
        got = format_fresh(address)
        if isinstance(got, str):
            return got
        have = got
    _, missing, extra, why = compare(have, order, presets)
    if why:
        return "; ".join(why) + ". 같은 펌웨어 이미지를 먼저 올리세요"
    send = list(order) if resend else missing
    if extra and backup_root is not None:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        name = (get_json(address, "/api/status") or {}).get("hotspot", {}).get("ssid") or address.replace(":", "_")
        where = backup_root / f"{name}-{stamp}"
        failed = save_modules(address, extra, where)
        if failed:
            return f"지울 모듈을 이 컴퓨터에 옮겨 두지 못했습니다 ({', '.join(failed)}). 아무것도 지우지 않았습니다"
        print(f"    지울 {len(extra)}개는 {where} 에 옮겨 뒀습니다")
    if extra:
        status, reply = request(address, "POST", "/api/patterns/delete", body="\n".join(extra).encode(),
                                headers={"Content-Type": "text/plain"}, timeout=20.0)
        if status != 200:
            return f"지우지 못했습니다: {reply if status is None else 'HTTP ' + str(status)}"
    # The largest modules first, while the panel's memory is at its least
    # broken up (make_room): the order they arrive in means nothing, the
    # catalog says which is which number.
    files = []
    for slug in sorted(send, key=lambda s: -(folder / f"{s}.pfm").stat().st_size):
        files.append((f"{slug}.pfm", (folder / f"{slug}.pfm").read_bytes()))
        if (folder / f"{slug}.json").is_file():
            files.append((f"{slug}.json", (folder / f"{slug}.json").read_bytes()))
    if files:
        problem = make_room(address, max(len(data) for _, data in files))
        if problem:
            return problem
    # The running order goes last, and closes the batch: one rescan for all of it.
    files.append((CATALOG, "".join(f"{s}\n" for s in order).encode()))
    for n, (name, data) in enumerate(files):
        problem = put_file(address, name, data, last=(n == len(files) - 1))
        if problem:
            # An abandoned batch restores itself on the panel after 5 s.
            return f"보내다 멈춤 ({n}/{len(files)}): {problem}"
        if sys.stdout.isatty():
            print(f"\r    보내는 중 {n + 1}/{len(files)}", end="", flush=True)
    if sys.stdout.isatty():
        print("\r" + " " * 30 + "\r", end="")
    started = time.monotonic()
    while time.monotonic() - started < RELOAD_TIMEOUT_S:
        time.sleep(1.0)
        now = pattern_list(address)
        if now and now["modules"] == order:
            return None
    now = pattern_list(address)
    if now is None:
        return "보낸 뒤 답이 없습니다"
    return f"보냈지만 목록이 묶음과 다릅니다 (모듈 {len(now['modules'])}개)"


def push(args, addresses, folder):
    folder = Path(folder)
    order, info = read_pack(folder)
    presets = info.get("presets")
    print(f"묶음: {folder}  모듈 {len(order)}개" + (f" ({info.get('from')} 에서)" if info.get("from") else ""))
    plan, rows, unreachable = [], [], 0
    for address in addresses:
        have = pattern_list(address, timeout=args.timeout)
        if have is None:
            rows.append([address, "답 없음", ""])
            unreachable += 1
            continue
        same, missing, extra, why = compare(have, order, presets)
        if same and not args.resend:
            rows.append([address, "이미 같음", ""])
            continue
        todo = why[:] or [f"보냄 {len(order) if args.resend else len(missing)}", f"지움 {len(extra)}"]
        if not why and not missing and not extra:
            todo = ["순서만 맞춤"] if not args.resend else todo
        if not have["mounted"]:
            todo = ["저장소를 처음 포맷"] + todo
        rows.append([address, "맞춤", ", ".join(todo)])
        plan.append((address, have))
    table(rows, ["주소", "할 일", ""])
    if not plan:
        print("맞출 패널이 없습니다." + (f" (답 없는 패널 {unreachable}대)" if unreachable else ""))
        return (1 if unreachable else 0), []
    removing = sum(len(compare(h, order, presets)[2]) for _, h in plan)
    if removing:
        print(f"묶음에 없는 모듈 {removing}개는 패널에서 지웁니다" +
              (". 지우기 전에 이 컴퓨터의 pattern-backup/ 에 옮겨 둡니다." if not args.no_backup else " (옮겨 두지 않음)."))
    if not args.yes and not confirm(f"패널 {len(plan)}대의 패턴을 이 묶음과 같게 맞춥니다. 계속할까요? [y/N] "):
        return 1, []
    backup_root = None if args.no_backup else Path(args.backup)
    done, failed = [], []
    for address, have in plan:
        print(f"  {address}")
        problem = push_one(address, folder, order, presets, have, backup_root, args.resend)
        if problem:
            print(f"    실패: {problem}")
            failed.append(address)
        else:
            print(f"    됐습니다: 모듈 {len(order)}개, 묶음의 순서대로")
            done.append(address)
    print(f"\n맞춤 {len(done)}대, 실패 {len(failed)}대" + (f", 답 없음 {unreachable}대" if unreachable else ""))
    return (1 if failed or unreachable else 0), done


def cmd_patterns_push(args):
    folder = args.pack
    if args.union:
        # Every module any of these panels has, in one pack: the first panel's
        # in its order, then what each next one adds. Nothing gets taken off.
        addresses = targets(args)
        folder = folder or "pack-all"
        if not args.pack and Path(folder).exists():
            # Last time's union: this one is built afresh. Anything else there is not ours.
            if not (Path(folder) / CATALOG).exists():
                raise SystemExit(f"{folder} 는 묶음이 아닌 폴더입니다. --pack 으로 다른 폴더를 주세요.")
            shutil.rmtree(folder)
        print(f"패널 {len(addresses)}대가 가진 패턴을 모두 모아 {folder} 에 묶습니다.")
        reachable = []
        for address in addresses:
            if pattern_list(address, timeout=args.timeout) is None:
                print(f"  {address}: 답 없음, 건너뜀")
                continue
            _, _, failed = pull(address, folder, merge=True, allow_empty=True)
            if failed:
                print(f"받지 못한 모듈이 있어 멈춥니다 ({address}: {', '.join(failed)}). 아무 패널도 건드리지 않았습니다.")
                return 1
            reachable.append(address)
        print()
        code, _ = push(args, reachable, folder)
        return code
    if args.source:
        folder = folder or f"pack-{args.source.replace(':', '_')}"
        count, _, failed = pull(args.source, folder)
        print(f"  모듈 {count}개를 받았습니다.\n")
        if failed:
            print(f"받지 못한 모듈이 있어 멈춥니다: {', '.join(failed)}")
            return 1
    elif not folder:
        raise SystemExit("--pack <묶음 폴더>, --from <기준 패널>, 또는 --union 을 주세요.")
    addresses = [a for a in targets(args) if a != args.source]
    code, _ = push(args, addresses, folder)
    return code


# ── join: new panels in one go ──────────────────────────────────────────
HUB_SECRETS = Path(__file__).resolve().parent.parent / "hub" / "hub_secrets.h"


def hub_network(path=HUB_SECRETS):
    """The network the hub's panels live on, from hub_secrets.h: its router in router mode, else its hotspot."""
    if not Path(path).is_file():
        return None, None
    found = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and parts[0] == "#define" and parts[2].startswith('"'):
            found[parts[1]] = parts[2].split('"')[1]
    if found.get("HUB_STA_SSID"):
        return found["HUB_STA_SSID"], found.get("HUB_STA_PASS", "")
    if found.get("HUB_AP_SSID"):
        return found["HUB_AP_SSID"], found.get("HUB_AP_PASS", "")
    return None, None


def cmd_join(args):
    path = Path(args.image)
    if not path.is_file():
        raise SystemExit(f"{path} 가 없습니다.")
    problems, build = inspect_image(path.read_bytes(), False)
    if problems:
        raise SystemExit(f"{path} 는 올리지 않습니다: " + " ".join(problems))
    ssid, password = args.ssid, args.password
    if not ssid:
        ssid, password = hub_network(args.hub_secrets or HUB_SECRETS)
        if not ssid:
            raise SystemExit(f"허브의 Wi-Fi를 모릅니다: {args.hub_secrets or HUB_SECRETS} 가 없습니다. --ssid 와 --pass 를 주세요.")
    if password is None:
        password = getpass.getpass(f'"{ssid}" 의 비밀번호: ')
    addresses = targets(args)
    print(f"패널 {len(addresses)}대를 허브에 넣습니다: 이미지 → " + ("패턴 → " if args.pack else "") + f"Wi-Fi \"{ssid}\"")
    if not args.yes and not confirm("계속할까요? [y/N] "):
        return 1

    print("\n1. 이미지")
    args.yes = True
    flash_args = argparse.Namespace(**{**vars(args), "panels": addresses, "keep_going": False,
                                       "no_soak": False, "force_image": False})
    cmd_flash(flash_args)
    ready = []
    for address in addresses:
        status = get_json(address, "/api/status", timeout=args.timeout)
        if is_panel(status) and (not build or status.get("build") == build):
            ready.append(address)
        else:
            print(f"  {address}: 이 이미지가 돌고 있지 않아 여기서 뺍니다")
    if not ready:
        print("이미지가 올라간 패널이 없어 멈춥니다.")
        return 1

    if args.pack:
        print("\n2. 패턴")
        push(args, ready, args.pack)
        order, info = read_pack(args.pack)
        lagging = []
        for address in ready:
            have = pattern_list(address, timeout=args.timeout)
            if have is None or not compare(have, order, info.get("presets"))[0]:
                print(f"  {address}: 패턴이 묶음과 달라 허브에 넣지 않습니다. 고친 뒤 같은 명령을 다시 돌리세요")
                lagging.append(address)
        ready = [a for a in ready if a not in lagging]

    print(f"\n{3 if args.pack else 2}. Wi-Fi")
    moved = 0
    for address in ready:
        ok, detail = move_to(address, ssid, password, connect=True)
        if ok and detail == "already":
            print(f"  {address}: 이미 \"{ssid}\" 에 있음 (다음 부팅에도 먼저 찾게 함)")
        else:
            print(f"  {address}: " + ("저장하고 옮겨 감" if ok else f"실패: {detail}"))
        moved += 1 if ok else 0
    print(f"\n허브로 간 패널 {moved}대 / {len(addresses)}대. 콘솔의 지도에 나타나면 끌어서 자리에 놓으세요.")
    return 0 if moved == len(addresses) else 1


# ── --hub-wifi: this computer on the hub's network for one command ──────
# With the hub's own hotspot the panels live on a network the laptop is not
# on (it talks to the hub over USB). --hub-wifi joins that network for the
# length of one command and puts the Wi-Fi back as it was afterwards, so
# "every panel" is one line however many there are. Windows (netsh); the
# network and its password come from hub_secrets.h. Internet, if the laptop
# has a cable, stays on the cable.
HUB_HOTSPOT_ADDRESS = "192.168.66.1"       # hub_config.h HUB_AP_IP
HUB_ROUTER_ADDRESS = "pf-ensemble.local"   # hub_config.h HUB_HOSTNAME, in router mode
WIFI_JOIN_TIMEOUT_S = 40


def netsh(*args):
    run = subprocess.run(["netsh", "wlan", *args], capture_output=True)
    text = run.stdout.decode(locale.getpreferredencoding(False), "replace")
    return run.returncode, text


def wifi_current():
    """The SSID this computer's Wi-Fi is on now, or None."""
    _, text = netsh("show", "interfaces")
    for line in text.splitlines():
        label, sep, value = line.partition(":")
        if sep and label.strip() == "SSID":
            return value.strip() or None
    return None


def wifi_add_profile(ssid, password):
    """A Windows Wi-Fi profile for ssid. Returns whether this call added it (and so should remove it)."""
    added = netsh("show", "profile", f"name={ssid}")[0] != 0
    if added:
        def esc(s):
            return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        xml = ('<?xml version="1.0"?>\n<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">'
               f"<name>{esc(ssid)}</name><SSIDConfig><SSID><name>{esc(ssid)}</name></SSID></SSIDConfig>"
               "<connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security>"
               "<authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption>"
               "<useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType>"
               f"<protected>false</protected><keyMaterial>{esc(password)}</keyMaterial></sharedKey>"
               "</security></MSM></WLANProfile>")
        fd, path = tempfile.mkstemp(suffix=".xml")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(xml)
            code, text = netsh("add", "profile", f"filename={path}", "user=current")
        finally:
            os.unlink(path)            # the password does not stay on disk
        if code != 0:
            raise SystemExit(f'"{ssid}" 의 Wi-Fi 프로필을 만들지 못했습니다: {text.strip()[:200]}')
    return added


def wifi_connect(ssid):
    netsh("connect", f"name={ssid}")
    started = time.monotonic()
    while time.monotonic() - started < WIFI_JOIN_TIMEOUT_S:
        if wifi_current() == ssid:
            return
        time.sleep(1.0)
    raise SystemExit(f'"{ssid}" 에 {WIFI_JOIN_TIMEOUT_S}초 안에 붙지 못했습니다. 허브가 켜져 있는지, 가까이 있는지 보세요.')


def wifi_leave(previous, ssid, added):
    if previous:
        netsh("connect", f"name={previous}")
    if added:
        netsh("delete", "profile", f"name={ssid}")
    if previous:
        started = time.monotonic()
        while time.monotonic() - started < 30 and wifi_current() != previous:
            time.sleep(1.0)
        print(f'이 컴퓨터의 Wi-Fi를 "{previous}" 로 돌려놓았습니다.' if wifi_current() == previous
              else f'"{previous}" 로 돌아가는 중입니다 (Windows가 마저 붙습니다).')


def stay_on_wifi(ssid, stop, every_s=3.0):
    """While stop is not set: back onto ssid whenever Windows has moved off it."""
    while not stop.wait(every_s):
        now = wifi_current()
        if now != ssid and not stop.is_set():
            print(f'\n  (Windows가 Wi-Fi를 "{now}" 로 옮겼습니다. 허브의 "{ssid}" 로 되돌립니다)', flush=True)
            netsh("connect", f"name={ssid}")


def hub_address(path=HUB_SECRETS):
    router = False
    if Path(path).is_file():
        router = any(line.strip().startswith("#define HUB_STA_SSID")
                     for line in Path(path).read_text(encoding="utf-8").splitlines())
    return HUB_ROUTER_ADDRESS if router else HUB_HOTSPOT_ADDRESS


def on_hub_wifi(args, run):
    if not sys.platform.startswith("win"):
        raise SystemExit("--hub-wifi 는 지금 Windows에서만 됩니다. 이 컴퓨터를 허브의 Wi-Fi에 직접 넣고 "
                         f"--hub {HUB_HOTSPOT_ADDRESS} 로 하세요.")
    secrets = getattr(args, "hub_secrets", None) or HUB_SECRETS
    ssid, password = hub_network(secrets)
    if not ssid:
        raise SystemExit(f"허브의 Wi-Fi를 모릅니다: {secrets} 가 없습니다.")
    hub = hub_address(secrets)
    # The panels, unless named: the ones the hub knows.
    if hasattr(args, "hub") and not (args.panels or args.hub or args.file):
        args.hub = hub
    previous = wifi_current()
    if previous == ssid:
        return run(args)
    print(f'이 컴퓨터의 Wi-Fi를 잠시 허브의 "{ssid}" 로 옮깁니다' + (f' (지금 "{previous}")' if previous else "") + " ...")
    added = wifi_add_profile(ssid, password)
    stop = threading.Event()
    try:
        wifi_connect(ssid)
        for _ in range(15):                       # an address from the hub, and the hub answering
            if get_json(hub, "/api/status", timeout=2.0) is not None:
                break
            time.sleep(1.0)
        # Windows leaves a network that has no internet for a saved one that
        # does, minutes into a long run (2026-11-14: halfway through the
        # patterns, every request after that timed out). Keep it here.
        threading.Thread(target=stay_on_wifi, args=(ssid, stop), daemon=True).start()
        return run(args)
    finally:
        stop.set()
        wifi_leave(previous, ssid, added)


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")   # a console that cannot print Korean should not stop the work
    parser = argparse.ArgumentParser(description="패널 여러 대에 같은 일을 Wi-Fi로 합니다.",
                                     epilog="자세한 설명: 이 파일 맨 위, ensemble/README.md 4장.")
    parser.add_argument("--hub-wifi", action="store_true",
                        help="명령 하나 동안 이 컴퓨터를 허브의 Wi-Fi에 넣었다가 돌려놓음 (Windows). "
                             "패널을 적지 않으면 허브가 아는 패널 모두")
    sub = parser.add_subparsers(dest="command", required=True)

    def panel_options(p):
        p.add_argument("panels", nargs="*", help="패널 주소 (IP 또는 이름, 필요하면 :포트)")
        p.add_argument("--hub", help="허브 주소. 허브가 아는 패널에게 합니다")
        p.add_argument("--only", help="--hub 와 함께: 이 번호의 패널만 (예: 1,3,5)")
        p.add_argument("--file", help=f"주소 목록 파일 (없으면 ./{DEFAULT_LIST})")
        p.add_argument("--timeout", type=float, default=4.0, help="패널의 답을 기다리는 초 (기본 4)")

    p = sub.add_parser("scan", help="이 네트워크에서 패널 찾기")
    p.add_argument("--subnet", help="찾을 곳의 앞 세 자리 (예: 192.168.0). 없으면 이 컴퓨터의 네트워크")
    p.add_argument("--port", type=int, default=80)
    p.add_argument("--timeout", type=float, default=1.5)
    p.add_argument("-o", "--output", help="찾은 주소를 적을 파일")
    p.set_defaults(run=cmd_scan)

    p = sub.add_parser("status", help="패널마다 무엇이 돌고 있는지")
    panel_options(p)
    p.set_defaults(run=cmd_status)

    p = sub.add_parser("flash", help="펌웨어 이미지 올리기 (들어갈 때도, 되돌릴 때도)")
    p.add_argument("image", help="firmware.bin")
    panel_options(p)
    p.add_argument("-y", "--yes", action="store_true", help="묻지 않고 진행")
    p.add_argument("--keep-going", action="store_true", help="돌아오지 않는 패널이 있어도 나머지를 계속")
    p.add_argument("--no-soak", action="store_true", help="첫 대를 지켜보는 시간 없이")
    p.add_argument("--force-image", action="store_true", help="이미지 검사를 건너뜀")
    p.set_defaults(run=cmd_flash)

    p = sub.add_parser("wifi-add", help="패널에 Wi-Fi 저장")
    p.add_argument("ssid")
    p.add_argument("--pass", dest="password", help="비밀번호. 없으면 물어봅니다")
    p.add_argument("--connect", action="store_true", help="저장하고 바로 그 네트워크로 옮김")
    panel_options(p)
    p.set_defaults(run=cmd_wifi_add)

    p = sub.add_parser("wifi-forget", help="패널에서 Wi-Fi 지우기")
    p.add_argument("ssid")
    panel_options(p)
    p.set_defaults(run=cmd_wifi_forget)

    p = sub.add_parser("reboot", help="패널 다시 켜기")
    panel_options(p)
    p.set_defaults(run=cmd_reboot)

    def push_options(p):
        p.add_argument("-y", "--yes", action="store_true", help="묻지 않고 진행")
        p.add_argument("--resend", action="store_true", help="이미 있는 모듈도 묶음의 것으로 다시 보냄")
        p.add_argument("--backup", default="pattern-backup", help="지울 모듈을 옮겨 둘 폴더 (기본 ./pattern-backup)")
        p.add_argument("--no-backup", action="store_true", help="지울 모듈을 옮겨 두지 않음")

    p = sub.add_parser("patterns", help="패널마다 패턴이 같은지 (번호가 같은 패턴을 가리키는지)")
    panel_options(p)
    p.add_argument("--pack", help="이 묶음과 비교 (없으면 첫 패널과)")
    p.add_argument("-v", "--verbose", action="store_true", help="무엇이 없고 더 있는지 이름까지")
    p.set_defaults(run=cmd_patterns)

    p = sub.add_parser("patterns-pull", help="한 패널의 패턴을 순서까지 폴더로 받기")
    p.add_argument("source", help="기준 패널 주소")
    p.add_argument("-o", "--output", required=True, help="받을 폴더")
    p.add_argument("--merge", action="store_true", help="폴더에 이미 있는 묶음에 이 패널의 패턴 중 없는 것만 더함")
    p.set_defaults(run=cmd_patterns_pull)

    p = sub.add_parser("patterns-push", help="패널마다 패턴을 묶음과 똑같게 (없는 것 보냄, 더 있는 것 지움, 순서 맞춤)")
    panel_options(p)
    p.add_argument("--pack", help="묶음 폴더 (patterns-pull 로 만든 것)")
    p.add_argument("--from", dest="source", help="묶음 대신 이 패널의 패턴을 받아서 (받은 것은 ./pack-<주소> 에 남음)")
    p.add_argument("--union", action="store_true",
                   help="대상 패널들이 가진 패턴을 모두 모아 모두에게 (아무것도 지우지 않음, 묶음은 ./pack-all)")
    push_options(p)
    p.set_defaults(run=cmd_patterns_push)

    p = sub.add_parser("join", help="새 패널을 한 번에: 이미지 → 패턴 → 허브 Wi-Fi")
    p.add_argument("image", help="ensemble firmware.bin")
    panel_options(p)
    p.add_argument("--pack", help="패턴 묶음 폴더. 없으면 패턴은 건드리지 않음")
    p.add_argument("--ssid", help="허브의 Wi-Fi. 없으면 ensemble/hub/hub_secrets.h 에서 읽음")
    p.add_argument("--pass", dest="password", help="그 비밀번호")
    p.add_argument("--hub-secrets", help="다른 hub_secrets.h")
    push_options(p)
    p.set_defaults(run=cmd_join)

    # Python before 3.12 hands addresses that come after an option ("wifi-add
    # Name --connect 10.0.0.5") to nobody: the list of panels was closed,
    # empty, when the first option began. They are panels; take them as such.
    args, extra = parser.parse_known_args(argv)
    if extra:
        if hasattr(args, "panels") and not any(x.startswith("-") for x in extra):
            args.panels = list(args.panels) + extra
        else:
            parser.error("알 수 없는 인자: " + " ".join(extra))
    if args.hub_wifi:
        return on_hub_wifi(args, args.run)
    return args.run(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n멈췄습니다.")
        sys.exit(130)
