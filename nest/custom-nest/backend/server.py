#!/usr/bin/env python3
"""JSON API for nest.exe (C++ engine). No static files — tunnel must not leak the exe."""
import json
import os
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(ROOT, "..", "..")))
from job_queue import JobQueue  # noqa: E402

ENGINE = os.path.join(ROOT, "engine", "nest.exe")
PORT = int(os.environ.get("PF_NEST_PORT", "9785"))
HOST = os.environ.get("PF_NEST_HOST", "127.0.0.1")
# Cloudflare edge ~100s; stay under that on the public tunnel.
NEST_OFF_TIMEOUT_S = 90

QUEUE = JobQueue()


def nest_timeout_sec(body):
    return NEST_OFF_TIMEOUT_S


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        ticket = (parse_qs(parsed.query).get("ticket") or [""])[0]
        if path == "/queue":
            self._json(200, json.dumps(QUEUE.snapshot(ticket)).encode("utf-8"))
            return
        if path in ("/", "/engine", "/health", "/simulate"):
            status = {
                "ok": True,
                "engine": "cpp",
                "exe": os.path.isfile(ENGINE),
                "hint": "POST /simulate with job JSON"
            }
            status.update(QUEUE.snapshot(ticket))
            self._json(200, json.dumps(status).encode("utf-8"))
            return
        self._json(404, json.dumps({
            "ok": False, "error": "GET /health, /queue or POST /simulate only",
            "pages": [], "job": {"rows": []}, "elapsedMs": 0
        }).encode("utf-8"))

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/simulate":
            self._json(404, json.dumps({
                "ok": False, "error": "POST /simulate only",
                "pages": [], "job": {"rows": []}, "elapsedMs": 0
            }).encode("utf-8"))
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b"{}"
        if not os.path.isfile(ENGINE):
            payload = json.dumps({
                "ok": False,
                "error": "C++ engine missing. Run engine\\build.bat then start.bat again.",
                "pages": [],
                "job": {"rows": []},
                "elapsedMs": 0
            }).encode("utf-8")
            self._json(503, payload)
            return
        ticket = ""
        try:
            parsed_body = json.loads(body.decode("utf-8") if body else "{}")
            if isinstance(parsed_body, dict):
                ticket = str(parsed_body.get("queueTicket") or "")
        except Exception:
            ticket = ""
        ticket = QUEUE.acquire(ticket)
        limit = nest_timeout_sec(body)
        try:
            try:
                proc = subprocess.run(
                    [ENGINE],
                    input=body,
                    capture_output=True,
                    timeout=limit,
                    cwd=os.path.join(ROOT, "engine"),
                )
            except subprocess.TimeoutExpired:
                sec = int(limit or NEST_OFF_TIMEOUT_S)
                self._json(504, json.dumps({
                    "ok": False, "error": "C++ nest timed out (%ss)." % sec,
                    "pages": [], "job": {"rows": []}, "elapsedMs": sec * 1000
                }).encode("utf-8"))
                return
            out = proc.stdout if proc.stdout else proc.stderr
            if not out:
                out = json.dumps({
                    "ok": False,
                    "error": "nest.exe returned no JSON (exit %s)" % proc.returncode,
                    "pages": [],
                    "job": {"rows": []},
                    "elapsedMs": 0
                }).encode("utf-8")
            self._json(200 if proc.returncode in (0, 2) else 500, out)
        finally:
            QUEUE.release(ticket)

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _json(self, code, payload):
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self._cors()
        self.end_headers()
        self.wfile.write(payload)

def main():
    os.chdir(ROOT)
    if not os.path.isfile(ENGINE):
        print("WARNING: engine\\nest.exe not found. Run engine\\build.bat")
    print("PatternFlow Custom Nest  http://%s:%s/  (C++ engine)" % (HOST, PORT))
    try:
        httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError as e:
        print("ERROR: port %s is busy. Close the old server window and run start.bat again." % PORT)
        print(e)
        sys.exit(1)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
