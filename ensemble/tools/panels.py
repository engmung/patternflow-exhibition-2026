#!/usr/bin/env python3
"""Do one thing to every panel, over Wi-Fi.

  python ensemble/tools/panels.py scan -o panels.txt        find the panels on this network
  python ensemble/tools/panels.py status                    what each one is running
  python ensemble/tools/panels.py flash firmware.bin        put an image on each (in, or back out)
  python ensemble/tools/panels.py wifi-add "Name" --connect teach each one a Wi-Fi network
  python ensemble/tools/panels.py wifi-forget "Name"
  python ensemble/tools/panels.py reboot

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

It talks to the panel's own HTTP API (Patternflow docs/rest-api.md): GET
/api/status, POST /update, /api/wifi. One request at a time per panel - the
panel's server takes a single connection. Python 3.8+, nothing to install.

Not tried on hardware yet: ensemble/README.md section 8.
"""
import argparse
import concurrent.futures
import getpass
import http.client
import json
import os
import socket
import sys
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


def get_json(address, path, timeout=5.0):
    status, body = request(address, "GET", path, timeout=timeout)
    if status != 200:
        return None
    try:
        value = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        return None
    return value if isinstance(value, dict) else None


def post_form(address, path, fields, timeout=8.0, method="POST"):
    body = urllib.parse.urlencode(fields).encode()
    status, reply = request(address, method, path, body=body,
                            headers={"Content-Type": "application/x-www-form-urlencoded"}, timeout=timeout)
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
        status = get_json(address, "/api/status", timeout=args.timeout)
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
        status = get_json(address, "/api/status", timeout=args.timeout)
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
    if not args.yes:
        answer = input(f"패널 {len(plan)}대에 올립니다. 첫 대를 먼저 올려 켜지는지 본 뒤 나머지를 올립니다. 계속할까요? [y/N] ")
        if answer.strip().lower() not in ("y", "yes", "ㅛ"):
            print("그만둡니다.")
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


def cmd_wifi_add(args):
    password = args.password
    if password is None:
        password = getpass.getpass(f'"{args.ssid}" 의 비밀번호 (열린 네트워크면 그냥 Enter): ')
    fields = {"ssid": args.ssid, "pass": password}
    if args.connect:
        fields["connect"] = "1"
    code = each(args, "저장하고 옮겨 감" if args.connect else "저장함",
                lambda address: post_form(address, "/api/wifi", fields))
    if args.connect:
        print("패널들이 그 네트워크로 옮겨 갑니다. 이 컴퓨터도 그 네트워크에 있어야 다시 닿습니다.")
    return code


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


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")   # a console that cannot print Korean should not stop the work
    parser = argparse.ArgumentParser(description="패널 여러 대에 같은 일을 Wi-Fi로 합니다.",
                                     epilog="자세한 설명: 이 파일 맨 위, ensemble/README.md 4장.")
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

    args = parser.parse_args(argv)
    return args.run(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n멈췄습니다.")
        sys.exit(130)
