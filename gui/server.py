#!/usr/bin/env python3
"""Touchscreen page for Omni Pedal. Python standard library only."""

import json
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE_HOST = "127.0.0.1"
ENGINE_PORT = 8765
PAGE_PORT = 8080

EFFECTS = ["bypass", "overdrive", "distortion", "delay", "chorus", "reverb", "bass"]
KNOBS = ("drive", "tone", "mix", "time", "level")

state = {
    "effect": "bypass",
    "drive": 0.35,
    "tone": 0.65,
    "mix": 0.45,
    "time": 0.35,
    "level": 1.0,
}


def clamp(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if number < 0.0:
        return 0.0
    if number > 1.0:
        return 1.0
    return number


def engine_exchange(line):
    command = line.strip()
    try:
        sock = socket.create_connection((ENGINE_HOST, ENGINE_PORT), 0.4)
    except OSError:
        return None
    try:
        sock.sendall((command + "\n").encode("ascii"))
        sock.settimeout(0.4)
        data = b""
        want_status = command in ("", "status")
        while len(data) < 8192:
            if want_status and data.endswith(b"end\n"):
                break
            if not want_status and (data.startswith(b"ok\n") or data.startswith(b"error ")):
                break
            try:
                chunk = sock.recv(1024)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
        return data.decode("ascii", "replace")
    except OSError:
        return None
    finally:
        sock.close()


def parse_status(text):
    found = {}
    for raw in text.splitlines():
        parts = raw.split()
        if len(parts) != 2:
            continue
        key, value = parts
        if key == "effect" and value in EFFECTS:
            found["effect"] = value
        elif key in KNOBS:
            number = clamp(value)
            if number is not None:
                found[key] = number
    if "effect" not in found:
        return None
    for key in KNOBS:
        if key not in found:
            return None
    return found


def read_engine():
    text = engine_exchange("status")
    if text is None:
        return None
    return parse_status(text)


def apply_local(line):
    parts = line.strip().split()
    if not parts:
        return False
    cmd = parts[0]
    if cmd == "next":
        index = EFFECTS.index(state["effect"]) if state["effect"] in EFFECTS else 0
        state["effect"] = EFFECTS[(index + 1) % len(EFFECTS)]
        return True
    if cmd == "bypass":
        state["effect"] = "bypass"
        return True
    if cmd == "effect" and len(parts) == 2 and parts[1] in EFFECTS:
        state["effect"] = parts[1]
        return True
    if cmd in KNOBS and len(parts) == 2:
        number = clamp(parts[1])
        if number is None:
            return False
        state[cmd] = number
        return True
    return False


def snapshot():
    remote = read_engine()
    if remote is not None:
        remote["connected"] = True
        return remote
    preview = dict(state)
    preview["connected"] = False
    return preview


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if args and isinstance(args[0], str) and args[0].startswith("GET /api/state"):
            return
        super().log_message(fmt, *args)

    def send_json(self, payload, code=200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/state":
            self.send_json(snapshot())
            return
        if path == "/":
            path = "/index.html"
        file_path = (HERE / path.lstrip("/")).resolve()
        if not str(file_path).startswith(str(HERE)) or not file_path.is_file():
            self.send_error(404)
            return
        kind = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
        }.get(file_path.suffix, "application/octet-stream")
        body = file_path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/api/command":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(min(length, 2000))
        try:
            line = json.loads(raw.decode("utf-8")).get("line", "")
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_json({"ok": False, "error": "bad json"}, 400)
            return
        if not isinstance(line, str) or not apply_local(line):
            self.send_json({"ok": False, "error": "bad command"}, 400)
            return
        reply = engine_exchange(line)
        body = snapshot()
        body["ok"] = reply is None or reply.startswith("ok") or "effect " in reply
        self.send_json(body)


def main():
    server = ThreadingHTTPServer((ENGINE_HOST, PAGE_PORT), Handler)
    print("Omni Pedal touchscreen: http://%s:%d" % (ENGINE_HOST, PAGE_PORT))
    print("Audio engine socket: %s:%d" % (ENGINE_HOST, ENGINE_PORT))
    print("On the Pi, open that page in Chromium, or use --kiosk.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
        server.server_close()


if __name__ == "__main__":
    main()
