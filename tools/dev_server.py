#!/usr/bin/env python3
"""Serves the web pages on the PC with sample data, for working on layouts.

    python3 tools/dev_server.py [--port 8000] [--problems]

Then open http://localhost:8000/ (or /?layout=a..d, /config).
The sample /data.json comes from the real Station code (build/tests/wx_sample),
so it has exactly the fields the firmware sends. Settings changes are kept in
memory only.
"""
import argparse
import json
import os
import subprocess
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
SAMPLE_BIN = os.path.join(ROOT, "build", "tests", "wx_sample")

SETTINGS = {
    "ssid": "Home Net", "has_pass": True, "hostname": "weather",
    "tz": "CST6CDT,M3.2.0,M11.1.0", "lat": "40.00000", "lon": "-100.00000", "elev_cm": 68800,
    "temp_unit": "F", "press_unit": "inHg", "wind_unit": "mph", "rain_unit": "in",
    "layout": "auto", "day_layout": "B", "night_layout": "D",
    "rain_um": 279, "wind_mmps": 667, "vane_offset": 0, "has_pin": False,
}


def sample(problems: bool) -> dict:
    if not os.path.exists(SAMPLE_BIN):
        sys.exit("build the tests first: cmake -S tests -B build/tests && cmake --build build/tests")
    out = subprocess.run([SAMPLE_BIN] + (["problems"] if problems else []),
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)


class Handler(BaseHTTPRequestHandler):
    problems = False

    def send(self, code, body, ctype):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def file(self, name, ctype):
        with open(os.path.join(WEB, name), "rb") as f:
            self.send(200, f.read(), ctype)

    def data(self):
        d = sample(self.problems)
        u = d["units"]
        u.update({"t": SETTINGS["temp_unit"], "p": SETTINGS["press_unit"],
                  "w": SETTINGS["wind_unit"], "r": SETTINGS["rain_unit"]})
        if SETTINGS["layout"] != "auto":
            d["layout"] = SETTINGS["layout"]
        return d

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/":
            letter = q.get("layout", [None])[0] or self.data()["layout"]
            return self.file("layout_%s.html" % letter.lower(), "text/html; charset=utf-8")
        if url.path == "/app.js":
            return self.file("app.js", "application/javascript")
        if url.path == "/config":
            return self.file("config.html", "text/html; charset=utf-8")
        if url.path == "/data.json":
            return self.send(200, json.dumps(self.data()), "application/json")
        if url.path == "/api/config":
            return self.send(200, json.dumps(SETTINGS), "application/json")
        if url.path == "/api/scan":
            nets = [{"ssid": "Home Net", "rssi": -48, "auth": 4}, {"ssid": "Neighbor", "rssi": -80, "auth": 4},
                    {"ssid": "Guest", "rssi": -67, "auth": 0}]
            return self.send(200, json.dumps({"scanning": False, "nets": nets}), "application/json")
        self.send(404, "Not found", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        form = urllib.parse.parse_qs(self.rfile.read(n).decode(), keep_blank_values=True)
        if self.path == "/api/config":
            for k, v in form.items():
                if k in SETTINGS:
                    SETTINGS[k] = v[0]
            return self.send(200, json.dumps({"ok": True, "wifi_changed": False, "hostname": "weather"}),
                             "application/json")
        if self.path in ("/api/scan", "/api/reboot", "/api/reset"):
            return self.send(200, '{"ok":true}', "application/json")
        self.send(404, "Not found", "text/plain")

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--problems", action="store_true", help="show the problem status bar")
    a = ap.parse_args()
    Handler.problems = a.problems
    print("serving on http://localhost:%d/" % a.port)
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
