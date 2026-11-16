"""
Patternflow Director — local MQTT broker (TCP 1883 + WebSockets 9001).

Why: official Windows Mosquitto builds often ship without WebSockets, and
browsers cannot speak raw MQTT TCP. This broker covers both the panel (1883)
and the Director PWA (ws://127.0.0.1:9001) with no Docker.

Also serves http://127.0.0.1:19001/lan so Director can show this PC’s LAN IP.

Install once:
  python -m pip install -r requirements.txt

Run (leave the window open):
  python run_local_broker.py
"""
from __future__ import annotations

import asyncio
import json
import logging
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from amqtt.broker import Broker
except ImportError as exc:
    raise SystemExit(
        "amqtt is not installed.\n"
        "  python -m pip install -r requirements.txt\n"
        f"({exc})"
    ) from exc

CONFIG = {
    "listeners": {
        "default": {
            "type": "tcp",
            "bind": "0.0.0.0:1883",
        },
        "ws-mqtt": {
            "type": "ws",
            "bind": "0.0.0.0:9001",
        },
    },
    "sys_interval": 0,
    "topic-check": {
        "enabled": False,
    },
}

LAN_HTTP_PORT = 19001

logging.basicConfig(
    level=logging.INFO,
    format="[broker] %(levelname)s %(message)s",
)


def lan_ipv4s() -> list[str]:
    ips: list[str] = []

    def add(ip: str) -> None:
        if (
            ip
            and not ip.startswith("127.")
            and not ip.startswith("169.254.")
            and ip not in ips
        ):
            ips.append(ip)

    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        add(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            add(info[4][0])
    except OSError:
        pass

    return ips


class LanHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        return

    def _send(self, code: int, body: dict) -> None:
        payload = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] not in ("/", "/lan"):
            self._send(404, {"ok": False, "error": "not found"})
            return
        ips = lan_ipv4s()
        self._send(200, {"ok": True, "ip": ips[0] if ips else "", "ips": ips})


def start_lan_http() -> ThreadingHTTPServer:
    server = ThreadingHTTPServer(("127.0.0.1", LAN_HTTP_PORT), LanHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


async def main() -> None:
    httpd = start_lan_http()
    broker = Broker(CONFIG)
    await broker.start()
    ips = lan_ipv4s()
    shown = ips[0] if ips else "192.168.x.x"
    print("Patternflow local broker running (LAN-trust / anonymous)")
    print(f"  This PC LAN IP:  {shown}" + (f"  (also: {', '.join(ips[1:])})" if len(ips) > 1 else ""))
    print("  Panels:          TCP  mqtt://%s:1883" % shown)
    print("  Director:        WS   ws://127.0.0.1:9001")
    print("  IP helper:       http://127.0.0.1:%d/lan" % LAN_HTTP_PORT)
    print("On the panel: MQTT -> Director -> paste %s -> Connect to Director" % shown)
    print("Ctrl+C to stop.")
    try:
        while True:
            await asyncio.sleep(3600)
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        httpd.shutdown()
        await broker.shutdown()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nstopped.")
