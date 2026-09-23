import csv
import json
import os
import re
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import serial

import analysis


ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data"
WEB_PORT = int(os.environ.get("TUNING_WEB_PORT", "8080"))
SERIAL_PORT = os.environ.get("TUNING_SERIAL_PORT", "COM5")
SERIAL_BAUD = 115200
LOG_MAX = int(os.environ.get("TUNING_LOG_MAX", "20000"))

REPORT_RE = re.compile(
    r"^\[\s*(?P<seconds>[0-9.]+)s\]\s+"
    r"RTD\s+(?P<temp>[-+0-9.]+)\s+C\s+\(R=(?P<ohm>[-+0-9.]+),\s+fault\s+0x(?P<fault>[0-9A-Fa-f]+)\)\s+\|\s+"
    r"W\s+(?P<grams>[-+0-9.]+|---)\s+g\s+(?P<status>[A-Z]+)\s+\(raw\s+(?P<raw>-?[0-9]+)\)(?:\s+UNCAL)?\s+\|\s+"
    r"ENC\s+(?P<encoder>-?[0-9]+)\s+btn:(?P<button>.)$"
)

# CALZERO,<millis>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
CALZERO_RE = re.compile(
    r"^CALZERO,(?P<millis>\d+),(?P<mean>[-+0-9.]+),(?P<sd>[-+0-9.]+),(?P<samples>\d+),"
    r"(?P<ohm>[-+0-9.]+),(?P<temp>[-+0-9.]+)$"
)
# CALPT,<millis>,<nominal_mg>,<U|D>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
CALPT_RE = re.compile(
    r"^CALPT,(?P<millis>\d+),(?P<nominal>[-+0-9.]+),(?P<direction>[UD]),(?P<mean>[-+0-9.]+),"
    r"(?P<sd>[-+0-9.]+),(?P<samples>\d+),(?P<ohm>[-+0-9.]+),(?P<temp>[-+0-9.]+)$"
)

CSV_COLUMNS = [
    "recorded_at", "millis", "nominal_mg", "direction", "raw_mean", "raw_sd", "samples",
    "rtd_ohm", "rtd_temp_c", "zero_before", "zero_after",
]


class SerialState:
    def __init__(self):
        self.lock = threading.Lock()
        self.serial = None
        self.connected = False
        self.last_data = None
        self.last_line = ""
        self.log = []
        # Nomor urut absolut baris pertama di self.log. Klien memakainya untuk
        # mengambil hanya baris baru, sehingga log bisa dibekukan tanpa kehilangan data.
        self.log_first_seq = 0
        self.cal_points = []
        self.pending_zero = None  # nol terakhir, dipakai sebagai zero_before titik berikutnya

    def snapshot(self, since):
        with self.lock:
            next_seq = self.log_first_seq + len(self.log)
            start = 0 if since < self.log_first_seq else min(len(self.log), since - self.log_first_seq)
            return {
                "port": SERIAL_PORT,
                "baud": SERIAL_BAUD,
                "connected": self.connected,
                "lastDataAge": None
                if self.last_data is None
                else round(time.time() - self.last_data, 1),
                "data": self.data,
                "lastLine": self.last_line,
                "log": self.log[start:],
                "logFirstSeq": self.log_first_seq + start,
                "logNextSeq": next_seq,
                "logDropped": max(0, self.log_first_seq - since) if since > 0 else 0,
            }

    @property
    def data(self):
        return getattr(self, "_data", None)

    def append_log(self, line):
        self.last_line = line
        self.log.append(line)
        if len(self.log) > LOG_MAX:
            dropped = len(self.log) - LOG_MAX
            del self.log[:dropped]
            self.log_first_seq += dropped

    def record_zero(self, mean):
        """Nol berlaku sebagai zero_after titik terakhir dan zero_before titik berikutnya.

        Memakai keduanya membatalkan drift linear selama titik diukur, yang pada
        hardware ini jauh lebih besar dari noise.
        """
        self.pending_zero = mean
        if self.cal_points and self.cal_points[-1]["zero_after"] is None:
            self.cal_points[-1]["zero_after"] = mean

    def record_point(self, point):
        point["zero_before"] = self.pending_zero
        point["zero_after"] = None
        self.cal_points.append(point)

    def clear_calibration(self):
        with self.lock:
            self.cal_points = []
            self.pending_zero = None

    def calibration_snapshot(self):
        with self.lock:
            points = [dict(p) for p in self.cal_points]
        fit = analysis.fit_linear(points)
        stats = analysis.metrics(points, fit)
        rows = analysis.residuals(points, fit) if fit else []
        return {
            "points": points,
            "rows": rows,
            "fit": fit,
            "metrics": stats,
            "report": analysis.report_text(fit, stats, rows),
            "pendingZero": self.pending_zero,
        }

    def send(self, command):
        with self.lock:
            if not self.serial or not self.serial.is_open:
                return False
            self.serial.write((command.strip() + "\n").encode("ascii"))
            return True


state = SerialState()
SESSION_CSV = DATA_DIR / f"calibration_{datetime.now():%Y%m%d_%H%M%S}.csv"


def write_calibration_csv(points):
    """Tulis ulang seluruh berkas sesi.

    Ditulis ulang, bukan di-append, karena zero_after sebuah titik baru
    diketahui setelah titik itu tercatat. Tiap sesi server memakai berkas
    sendiri, jadi tidak ada data sesi lain yang tertimpa.
    """
    DATA_DIR.mkdir(exist_ok=True)
    with SESSION_CSV.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for point in points:
            writer.writerow({column: point.get(column, "") for column in CSV_COLUMNS})


def handle_line(line):
    """Satu baris dari firmware. Dipisah dari loop serial supaya bisa diuji."""
    calibration_changed = False
    with state.lock:
        state.append_log(line)

        zero = CALZERO_RE.match(line)
        if zero:
            state.record_zero(float(zero.group("mean")))
            calibration_changed = True

        point = CALPT_RE.match(line)
        if point:
            state.record_point({
                "recorded_at": datetime.now().isoformat(timespec="seconds"),
                "millis": int(point.group("millis")),
                "nominal_mg": float(point.group("nominal")),
                "direction": point.group("direction"),
                "raw_mean": float(point.group("mean")),
                "raw_sd": float(point.group("sd")),
                "samples": int(point.group("samples")),
                "rtd_ohm": float(point.group("ohm")),
                "rtd_temp_c": float(point.group("temp")),
            })
            calibration_changed = True

        snapshot = [dict(p) for p in state.cal_points] if calibration_changed else None

        match = REPORT_RE.match(line)
        if match:
            values = match.groupdict()
            state._data = {
                "seconds": float(values["seconds"]),
                "tempC": float(values["temp"]),
                "rtdOhm": float(values["ohm"]),
                "rtdFault": int(values["fault"], 16),
                "grams": None if values["grams"] == "---" else float(values["grams"]),
                "status": values["status"],
                "rawCounts": int(values["raw"]),
                "encoder": int(values["encoder"]),
                "button": values["button"],
            }
            state.last_data = time.time()

    if snapshot is not None:
        write_calibration_csv(snapshot)  # di luar lock: menulis berkas lambat


def serial_worker():
    while True:
        try:
            connection = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
            with state.lock:
                state.serial = connection
                state.connected = True
                state.append_log(f"Connected to {SERIAL_PORT} @ {SERIAL_BAUD}")

            while connection.is_open:
                raw = connection.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    handle_line(line)
        except (OSError, serial.SerialException) as error:
            with state.lock:
                state.serial = None
                state.connected = False
                state.append_log(f"Serial unavailable: {error}")
            time.sleep(2)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format_string, *args):
        return

    def send_bytes(self, content_type, body, status=200):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        if path == "/api/state":
            try:
                since = int(parse_qs(parsed.query).get("since", ["0"])[0])
            except ValueError:
                since = 0
            body = json.dumps(state.snapshot(since)).encode("utf-8")
            self.send_bytes("application/json; charset=utf-8", body)
            return
        if path == "/api/calibration":
            body = json.dumps(state.calibration_snapshot()).encode("utf-8")
            self.send_bytes("application/json; charset=utf-8", body)
            return
        if path == "/api/calibration.csv":
            if not SESSION_CSV.exists():
                write_calibration_csv([])
            self.send_bytes("text/csv; charset=utf-8", SESSION_CSV.read_bytes())
            return
        if path in ("/", "/index.html"):
            self.send_bytes("text/html; charset=utf-8", (ROOT / "index.html").read_bytes())
            return
        self.send_bytes("text/plain; charset=utf-8", b"Not found", 404)

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/calibration/clear":
            state.clear_calibration()
            write_calibration_csv([])
            self.send_bytes("application/json; charset=utf-8", b'{"ok":true}')
            return
        if path != "/api/command":
            self.send_bytes("text/plain; charset=utf-8", b"Not found", 404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length))
            command = str(payload.get("command", ""))
        except (ValueError, json.JSONDecodeError):
            self.send_bytes("application/json; charset=utf-8", b'{"ok":false}', 400)
            return
        number = r"[0-9]+(?:\.[0-9]+)?"
        allowed = (
            rf"[?tzrvepn]"
            rf"|c\s+{number}"
            rf"|s\s+{number}"
            rf"|m\s+{number}\s+[udUD]"
        )
        if not re.fullmatch(allowed, command):
            self.send_bytes("application/json; charset=utf-8", b'{"ok":false}', 400)
            return
        ok = state.send(command)
        self.send_bytes("application/json; charset=utf-8", json.dumps({"ok": ok}).encode("utf-8"), 200 if ok else 503)


if __name__ == "__main__":
    threading.Thread(target=serial_worker, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", WEB_PORT), Handler)
    print(f"Tuning web: http://127.0.0.1:{WEB_PORT}")
    print(f"Serial: {SERIAL_PORT} @ {SERIAL_BAUD}. Set TUNING_SERIAL_PORT to override.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()