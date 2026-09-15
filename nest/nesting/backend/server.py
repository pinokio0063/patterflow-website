#!/usr/bin/env python3
"""HTTP wrapper around Sparrow temp.exe for nest.patternflow.fit.

POST /simulate with the same job JSON as Custom Nest. Converts outlines to
Sparrow input (points), runs temp.exe, returns fabric meters + SVG.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(ROOT, "..", "..")))
from job_queue import JobQueue  # noqa: E402

ENGINE = os.path.join(ROOT, "temp.exe")
RUNTIME = os.path.join(ROOT, "runtime")
PORT = int(os.environ.get("PF_NESTING_PORT", "9786"))
HOST = os.environ.get("PF_NESTING_HOST", "127.0.0.1")
PT_PER_IN = 72.0
IN_TO_M = 0.0254
# Browser/Cloudflare wait ~100s; keep engine under that.
MAX_TIME_SEC = 90
IO_OVERHEAD_SEC = 4

QUEUE = JobQueue()


def normalize_size(raw):
    if not raw:
        return ""
    s = str(raw).upper().strip()
    s = s.replace("XXXXXL", "5XL").replace("XXXXL", "4XL")
    s = s.replace("XXXL", "3XL").replace("XXL", "2XL")
    return s


def normalize_slv(raw):
    if not raw:
        return ""
    slv = str(raw).upper().strip()
    if slv.startswith("H"):
        return "HAF"
    if slv.startswith("F"):
        return "FULL"
    return slv


def part_dim(chart, size, key):
    row = chart.get(size) if isinstance(chart, dict) else None
    if not isinstance(row, dict):
        return 0.0, 0.0
    part = row.get(key)
    if not isinstance(part, dict):
        return 0.0, 0.0
    try:
        w = float(part.get("width") or 0)
        h = float(part.get("height") or 0)
    except (TypeError, ValueError):
        return 0.0, 0.0
    if not (w > 0 and h > 0):
        return 0.0, 0.0
    return w, h


def bbox_pts(points):
    xs = [p["x"] for p in points]
    ys = [p["y"] for p in points]
    return min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)


def read_master(master):
    if not isinstance(master, dict):
        return []
    raw = master.get("points") or []
    out = []
    for p in raw:
        try:
            out.append({"x": float(p["x"]), "y": float(p["y"])})
        except (TypeError, ValueError, KeyError):
            continue
    return out if len(out) >= 3 else []


def grade_to_points(master_pts, target_w_in, target_h_in):
    """Fit SVG-unit outline to chart inches, then convert to Sparrow points."""
    if not master_pts or target_w_in <= 0 or target_h_in <= 0:
        return []
    min_x, min_y, bw, bh = bbox_pts(master_pts)
    if bw <= 1e-12 or bh <= 1e-12:
        return []
    sx = target_w_in / bw
    sy = target_h_in / bh
    poly = []
    for p in master_pts:
        x_in = (p["x"] - min_x) * sx
        y_in = (p["y"] - min_y) * sy
        poly.append([round(x_in * PT_PER_IN, 4), round(y_in * PT_PER_IN, 4)])
    return poly


def inflate_polygon(poly, gap_pts):
    """Outward offset from centroid so minGap matches the Illustrator extension."""
    if gap_pts <= 0 or len(poly) < 3:
        return poly
    cx = sum(p[0] for p in poly) / len(poly)
    cy = sum(p[1] for p in poly) / len(poly)
    inflate = gap_pts / 2.0
    out = []
    for x, y in poly:
        dx = x - cx
        dy = y - cy
        length = (dx * dx + dy * dy) ** 0.5 or 1.0
        out.append([
            round(x + (dx / length) * inflate, 4),
            round(y + (dy / length) * inflate, 4),
        ])
    return out


def sparrow_item(item_id, poly):
    return {
        "id": item_id,
        "demand": 1,
        "allowed_orientations": [270, 90],
        "shape": {"type": "simple_polygon", "data": poly},
    }


def build_sparrow_input(body):
    dia = float(body.get("dia") or 63)
    if not (dia > 0):
        dia = 63.0
    min_gap = float(body.get("minGap") or 0)
    if min_gap < 0:
        min_gap = 0.0
    sleeve_key = str(body.get("sleeveKey") or "short_slv_with_rib")
    rib = "without_rib" if "without" in sleeve_key else "with_rib"
    haf_key = "short_slv_" + rib
    full_key = "long_slv_" + rib
    chart = body.get("chart") if isinstance(body.get("chart"), dict) else {}
    masters = body.get("masters") if isinstance(body.get("masters"), dict) else {}
    font_m = read_master(masters.get("font"))
    back_m = read_master(masters.get("back"))
    slv_m = read_master(masters.get("sleeve"))
    slv_long = read_master(masters.get("sleeveLong"))
    if not font_m or not back_m:
        raise ValueError("Load FRONT and BACK SVG first.")
    job = body.get("job") if isinstance(body.get("job"), list) else []
    gap_pts = min_gap * PT_PER_IN
    items = []
    next_id = 0
    for row in job:
        size = normalize_size((row or {}).get("SIZE"))
        slv = normalize_slv((row or {}).get("SLV"))
        if not size or slv not in ("HAF", "FULL"):
            continue
        tw, th = part_dim(chart, size, "FONT_BACK")
        if tw <= 0:
            continue
        font_poly = inflate_polygon(grade_to_points(font_m, tw, th), gap_pts)
        back_poly = inflate_polygon(grade_to_points(back_m, tw, th), gap_pts)
        if len(font_poly) < 3 or len(back_poly) < 3:
            continue
        items.append(sparrow_item(next_id, font_poly))
        next_id += 1
        items.append(sparrow_item(next_id, back_poly))
        next_id += 1
        is_full = slv == "FULL"
        src = slv_long if is_full else slv_m
        sw, sh = part_dim(chart, size, full_key if is_full else haf_key)
        slv_poly = inflate_polygon(grade_to_points(src, sw, sh), gap_pts) if src else []
        if len(slv_poly) >= 3:
            # Two sleeves per garment, same as Custom Nest.
            items.append(sparrow_item(next_id, slv_poly))
            next_id += 1
            items.append(sparrow_item(next_id, slv_poly))
            next_id += 1
    if not items:
        raise ValueError("No nestable pieces from the current job / size chart.")
    return {
        "name": "patternflow_nest",
        "strip_height": round(dia * PT_PER_IN, 4),
        "items": items,
    }


def fabric_meters(svg_text):
    used_pts = None
    m = re.search(r"x_max:\s*([\d.]+)", svg_text or "")
    if m:
        used_pts = float(m.group(1))
    if used_pts is None:
        m = re.search(r'h:\s*[\d.]+\s*\|\s*w:\s*([\d.]+)', svg_text or "", re.I)
        if m:
            used_pts = float(m.group(1))
    if used_pts is None:
        m = re.search(r"<path d=\"M0,0 L([\d.]+),0 L", svg_text or "")
        if m:
            used_pts = float(m.group(1))
    if not used_pts or used_pts <= 0:
        return None
    return round((used_pts / PT_PER_IN) * IN_TO_M, 4)


def svg_to_inches(svg_text):
    """Scale Sparrow point SVG to inches so the web viewer (12px/in) matches Custom Nest."""
    text = svg_text or ""
    match = re.match(r"(?is)\s*(<svg\b[^>]*>)(.*)(</svg>\s*)\s*$", text)
    if not match:
        return text
    open_tag, inner, close_tag = match.group(1), match.group(2), match.group(3)
    vb = re.search(r'viewBox\s*=\s*["\']([^"\']+)["\']', open_tag, re.I)
    if not vb:
        return text
    parts = vb.group(1).replace(",", " ").split()
    if len(parts) != 4:
        return text
    try:
        nums = [float(p) / PT_PER_IN for p in parts]
    except ValueError:
        return text
    new_vb = " ".join("%.4f" % n for n in nums)
    new_open = re.sub(
        r'viewBox\s*=\s*["\'][^"\']+["\']',
        'viewBox="%s"' % new_vb,
        open_tag,
        count=1,
        flags=re.I,
    )
    return "%s<g transform=\"scale(%.10f)\">%s</g>%s" % (
        new_open, 1.0 / PT_PER_IN, inner, close_tag
    )


def fail_payload(error, elapsed_ms=0):
    return {
        "ok": False,
        "error": error,
        "fabricMeters": None,
        "svg": "",
        "elapsedMs": elapsed_ms,
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        ticket = (parse_qs(parsed.query).get("ticket") or [""])[0]
        if path == "/queue":
            snap = QUEUE.snapshot(ticket)
            snap["engine"] = "sparrow"
            self._json(200, snap)
            return
        if path in ("/", "/health", "/engine", "/simulate"):
            snap = QUEUE.snapshot(ticket)
            snap.update({
                "engine": "sparrow",
                "exe": os.path.isfile(ENGINE),
                "hint": "POST /simulate with job JSON",
            })
            self._json(200, snap)
            return
        self._json(404, fail_payload("GET /health, /queue or POST /simulate only"))

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/simulate":
            self._json(404, fail_payload("POST /simulate only"))
            return
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        if not os.path.isfile(ENGINE):
            self._json(503, fail_payload(
                "temp.exe missing. Put it in nest\\nesting\\backend\\temp.exe"
            ))
            return
        try:
            body = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            self._json(400, fail_payload("Invalid JSON body"))
            return
        try:
            sparrow = build_sparrow_input(body if isinstance(body, dict) else {})
        except ValueError as e:
            self._json(400, fail_payload(str(e)))
            return
        except Exception as e:
            self._json(400, fail_payload("Could not build Sparrow input: %s" % e))
            return
        time_sec = int(body.get("timeSec") or 30)
        if time_sec < 3:
            time_sec = 3
        if time_sec > MAX_TIME_SEC:
            time_sec = MAX_TIME_SEC
        cli_time = max(3, time_sec - IO_OVERHEAD_SEC)
        http_limit = time_sec + 20
        ticket = QUEUE.acquire(body.get("queueTicket") if isinstance(body, dict) else None)
        job_dir = os.path.join(RUNTIME, "jobs", "".join(
            ch if ch.isalnum() or ch in "-_" else "-" for ch in ticket
        )[:80] or "job")
        output_dir = os.path.join(job_dir, "output")
        input_path = os.path.join(job_dir, "input.json")
        output_svg = os.path.join(output_dir, "final_patternflow_nest.svg")
        output_json = os.path.join(output_dir, "final_patternflow_nest.json")
        svg_raw = ""
        t0 = time.time()
        try:
            os.makedirs(output_dir, exist_ok=True)
            with open(input_path, "w", encoding="utf-8") as fh:
                json.dump(sparrow, fh)
            proc = subprocess.run(
                [ENGINE, "-i", input_path, "-t", str(cli_time), "-x"],
                cwd=job_dir,
                capture_output=True,
                timeout=http_limit,
            )
            if not os.path.isfile(output_svg):
                elapsed = int((time.time() - t0) * 1000)
                err = (proc.stderr or proc.stdout or b"").decode("utf-8", "replace").strip()
                hint = err.splitlines()[-1] if err else ("exit %s" % proc.returncode)
                self._json(500, fail_payload("temp.exe produced no SVG (%s)" % hint, elapsed))
                return
            with open(output_svg, "r", encoding="utf-8") as fh:
                svg_raw = fh.read()
        except subprocess.TimeoutExpired:
            elapsed = int((time.time() - t0) * 1000)
            self._json(504, fail_payload("Sparrow nest timed out (%ss)." % time_sec, elapsed))
            return
        except OSError as e:
            self._json(500, fail_payload("Could not start temp.exe: %s" % e))
            return
        finally:
            QUEUE.release(ticket)
            try:
                shutil.rmtree(job_dir, ignore_errors=True)
            except Exception:
                pass
        if not svg_raw:
            return
        elapsed = int((time.time() - t0) * 1000)
        meters = fabric_meters(svg_raw)
        self._json(200, {
            "ok": True,
            "fabricMeters": meters,
            "svg": svg_to_inches(svg_raw),
            "elapsedMs": elapsed,
            "itemCount": len(sparrow["items"]),
            "density": None,
        })

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _json(self, code, obj):
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self._cors()
        self.end_headers()
        self.wfile.write(payload)


def main():
    os.makedirs(os.path.join(RUNTIME, "jobs"), exist_ok=True)
    if not os.path.isfile(ENGINE):
        print("WARNING: temp.exe not found in nest\\nesting\\backend\\")
    print("PatternFlow Nesting (Sparrow)  http://%s:%s/  (temp.exe)" % (HOST, PORT))
    try:
        httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError as e:
        print("ERROR: port %s is busy. Close the old nesting window and try again." % PORT)
        print(e)
        sys.exit(1)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
