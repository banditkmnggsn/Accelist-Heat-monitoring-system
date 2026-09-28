"""Siklus pengeringan satu sampel: tare -> isi -> pemanasan -> cut-off -> prediksi.

Berjalan di PC, bukan di ESP32, sesuai arsitektur proyek: firmware hanya
mengirim baris report, PC yang menyimpan, menghitung, dan memutuskan.

Alur fase:
  idle     -> prepare : operator menekan "Mulai siklus"
  prepare  -> taring  : operator menekan "Tare" dengan wadah kosong terpasang
  taring   -> load    : firmware melaporkan "Tare selesai"
  load     -> heating : operator menekan "Mulai pemanas"; berat awal m0 dicatat
  heating  -> cutoff  : laju penyusutan < ambang load cell selama beberapa menit
  cutoff   -> done    : waktu prediksi tercapai (laju model < target)
  (aktif)  -> aborted : operator membatalkan, ESP32 restart, atau data berhenti

Setelah cut-off, load cell tidak lagi dipakai untuk kurva: fit dibekukan dan
waktu berhenti diekstrapolasi dari model. Menit sesudah cut-off tetap direkam
sebagai data validasi — selisihnya terhadap model menunjukkan seberapa jauh
ekstrapolasi bisa dipercaya.
"""

import csv
import json
import math
import threading
from collections import deque
from dataclasses import asdict, dataclass, fields
from datetime import datetime

import drying

EXPECTED_PER_MIN = 120      # baris report tiap 500 ms (app::kReportPeriodMs)
MIN_SAMPLES_PER_MIN = 40    # di bawah ini menit dianggap "data kurang"
GAP_ABORT_S = 120.0
TARE_TIMEOUT_S = 20.0
RECENT_KEEP_S = 900.0
READINESS_WINDOW_S = 180.0
CREEP_WINDOW_S = 120.0
REBOOT_BACKSTEP_S = 5.0

# Status firmware yang nilai beratnya tidak boleh dipakai
BAD_STATUS = {"NODATA", "DOUTERR", "SETTLE", "CLIP", "BUSY"}

PHASE_LABELS = {
    "idle": "Belum mulai",
    "prepare": "Persiapan: tare wadah kosong",
    "taring": "Tare berjalan",
    "load": "Isi sampel",
    "heating": "Pemanasan",
    "cutoff": "Cut-off: menunggu waktu prediksi",
    "done": "Selesai",
    "aborted": "Dibatalkan",
}
ACTIVE_PHASES = {"prepare", "taring", "load", "heating", "cutoff"}

MINUTE_COLUMNS = [
    "index", "t_mid_min", "n", "n_used", "mass_mg", "spread_mg", "temp_c", "rate_mg_min",
    "valid", "after_cutoff", "flags", "fit_residual_mg",
]
SAMPLE_COLUMNS = ["wall_time", "phase", "t_rel_s", "fw_seconds", "grams", "raw", "status", "temp_c"]


@dataclass
class Settings:
    cutoff_rate_mg_min: float = 10.0   # di bawah ini load cell tidak dipercaya lagi
    target_rate_mg_min: float = 0.1    # pemanasan berhenti saat laju model di bawah ini
    rate_window_min: int = 5           # jendela laju, sama dengan REG_WINDOW_BLK lib/Heatbox
    cutoff_confirm_min: int = 2        # menit berturut-turut di bawah ambang
    min_heat_min: int = 3              # MIN_DRY_S lib/Heatbox
    max_heat_min: int = 180
    rise_tolerance_mg: float = 3.0     # kenaikan per menit di atas ini = anomali
    m0_window_s: float = 30.0
    min_sample_mg: float = 50.0
    ready_drift_mg_min: float = 3.0    # sama dengan app::kSettledDriftMgPerMin
    model: str = "exp"                 # "exp" atau "exp_drift"


def _round(value, digits):
    return None if value is None else round(value, digits)


class DryingCycle:
    def __init__(self, data_dir, send):
        self.lock = threading.RLock()
        self.data_dir = data_dir
        self.send = send
        self.settings = Settings()
        self.recent = deque()          # sampel 15 menit terakhir, terisi di fase apa pun
        self.last_fw_seconds = None
        self.rtd_saturation_seen = False
        self._samples_handle = None
        self._samples_writer = None
        self._clear()

    # ------------------------------------------------------------------
    #  Status dasar
    # ------------------------------------------------------------------
    def _clear(self):
        self._close_samples()
        self.phase = "idle"
        self.message = "Tekan Mulai siklus untuk memulai satu sampel."
        self.warnings = []
        self.events = []
        self.cycle_id = None
        self.folder = None
        self.m0_mg = None
        self.t_heat = None
        self.minutes = []
        self.cur_index = None
        self.cur_samples = []
        self.peak_rate = None
        self.below_count = 0
        self.fit = None
        self.fit_alt = None
        self.fit_final = False
        self.prediction = None
        self.prediction_alt = None
        self.confidence = []
        self.cutoff_min = None
        self.fit_start_min = None
        self.tare_requested_at = None
        self.last_sample_t = None
        self.end_reason = None

    def _event(self, now, text):
        elapsed = None if self.t_heat is None else round((now - self.t_heat) / 60.0, 2)
        self.events.append({
            "wall_time": datetime.now().isoformat(timespec="seconds"),
            "elapsed_min": elapsed,
            "text": text,
        })

    def _warn(self, text):
        if text not in self.warnings:
            self.warnings.append(text)

    # ------------------------------------------------------------------
    #  Masukan dari server.py
    # ------------------------------------------------------------------
    def feed(self, sample):
        """Satu baris report. `sample` berisi t (detik monotonik PC), fw_seconds,
        grams, raw, status, temp_c, uncal."""
        with self.lock:
            now = sample["t"]
            if self.last_fw_seconds is not None and \
                    sample["fw_seconds"] < self.last_fw_seconds - REBOOT_BACKSTEP_S:
                self._on_reboot(now)
            self.last_fw_seconds = sample["fw_seconds"]

            usable = sample["grams"] is not None and sample["status"] not in BAD_STATUS
            if usable:
                self.recent.append(sample)
            while self.recent and now - self.recent[0]["t"] > RECENT_KEEP_S:
                self.recent.popleft()

            if self.phase not in ACTIVE_PHASES:
                return

            if self.last_sample_t is not None and now - self.last_sample_t > GAP_ABORT_S:
                self._abort(now, "Data dari ESP32 terhenti lebih dari 2 menit.")
                return
            self.last_sample_t = now
            self._write_sample(sample)

            if sample["uncal"]:
                self._warn("Load cell masih UNCAL: skala belum dikalibrasi, semua angka massa salah.")
            if self.phase == "taring" and now - self.tare_requested_at > TARE_TIMEOUT_S:
                self.phase = "prepare"
                self.message = "Firmware tidak mengonfirmasi tare dalam 20 detik. Tekan Tare lagi."
            if self.phase in ("heating", "cutoff"):
                self._feed_heating(sample, usable, now)

    def on_log_line(self, line, now):
        with self.lock:
            if "Firmware v" in line or "Initial zero setting" in line:
                self._on_reboot(now)
                return
            if "RTD mendekati saturasi" in line:
                self.rtd_saturation_seen = True
                if self.phase in ACTIVE_PHASES:
                    self._warn("Sensor suhu mendekati saturasi: di atas batas ini suhu yang tampil TIDAK "
                               "benar. RTD_RREF terlalu kecil untuk suhu pemanasan.")
            if self.phase != "taring":
                return
            if "Tare selesai" in line:
                self.phase = "load"
                self.message = ("Tare selesai. Masukkan sampel, tunggu angkanya diam, "
                                "lalu tekan Mulai pemanas.")
                self._event(now, "Tare wadah kosong selesai")
            elif "Tare ditolak" in line:
                self.phase = "prepare"
                self.message = "Firmware menolak tare (ADS1232 sedang sibuk). Tunggu sebentar lalu coba lagi."

    def _on_reboot(self, now):
        # Setelah restart, firmware menjalankan tare otomatis. Kalau sampel sudah
        # di wadah, tare itu menolkannya — seluruh siklus tidak bisa dipercaya lagi.
        if self.phase in ACTIVE_PHASES:
            self._abort(now, "ESP32 restart di tengah siklus. Tare otomatis saat nyala kemungkinan "
                             "sudah menolkan sampel, jadi data sesudahnya tidak valid.")

    # ------------------------------------------------------------------
    #  Aksi operator
    # ------------------------------------------------------------------
    def action(self, name, now, new_settings=None):
        with self.lock:
            if new_settings:
                if self.phase in ("heating", "cutoff"):
                    return False, "Pengaturan tidak bisa diubah saat pemanasan berjalan."
                self._apply_settings(new_settings)

            if name == "settings":
                return True, "Pengaturan disimpan."
            if name == "start":
                if self.phase in ACTIVE_PHASES:
                    return False, "Siklus sedang berjalan."
                self._begin(now)
                return True, self.message
            if name == "tare":
                if self.phase not in ("prepare", "load"):
                    return False, "Tare hanya bisa dilakukan sebelum pemanasan."
                if not self.send("t"):
                    return False, "Serial belum terhubung."
                self.phase = "taring"
                self.tare_requested_at = now
                self.message = "Tare berjalan (64 sampel, sekitar 6,4 detik). Jangan sentuh wadah."
                return True, self.message
            if name == "heat":
                if self.phase != "load":
                    return False, "Tare wadah kosong dulu sebelum memulai pemanas."
                return self._start_heating(now)
            if name == "stop":
                if self.phase not in ("heating", "cutoff"):
                    return False, "Tidak ada pemanasan yang berjalan."
                self._finish(now, "Dihentikan operator.")
                return True, self.message
            if name == "abort":
                if self.phase not in ACTIVE_PHASES:
                    return False, "Tidak ada siklus aktif."
                self._abort(now, "Dibatalkan operator.")
                return True, self.message
            if name == "reset":
                if self.phase in ACTIVE_PHASES:
                    return False, "Batalkan siklus dulu."
                self._clear()
                return True, self.message
            return False, f"Aksi tidak dikenal: {name}"

    def _apply_settings(self, values):
        allowed = {f.name: f.type for f in fields(Settings)}
        for key, value in values.items():
            if key not in allowed:
                continue
            if key == "model":
                if value in ("exp", "exp_drift"):
                    self.settings.model = value
                continue
            number = float(value)
            if not math.isfinite(number) or number <= 0:
                continue
            is_int = allowed[key] in (int, "int")
            setattr(self.settings, key, int(round(number)) if is_int else number)

    def _begin(self, now):
        self._clear()
        stamp = datetime.now()
        self.cycle_id = stamp.strftime("%Y%m%d_%H%M%S")
        self.folder = self.data_dir / f"cycle_{self.cycle_id}"
        self.folder.mkdir(parents=True, exist_ok=True)
        self._samples_handle = (self.folder / "samples.csv").open("w", newline="", encoding="utf-8")
        self._samples_writer = csv.writer(self._samples_handle)
        self._samples_writer.writerow(SAMPLE_COLUMNS)
        self.phase = "prepare"
        self.message = ("Pasang wadah KOSONG. Tunggu drift nol turun di bawah "
                        f"{self.settings.ready_drift_mg_min:g} mg/menit, lalu tekan Tare.")
        self.last_sample_t = now
        self._event(now, f"Siklus {self.cycle_id} dimulai")
        if self.rtd_saturation_seen:
            self._warn("Firmware sudah melaporkan sensor suhu mendekati saturasi sebelum siklus ini.")
        self._persist()

    def _start_heating(self, now):
        s = self.settings
        window = [x for x in self.recent if now - x["t"] <= s.m0_window_s]
        expected = s.m0_window_s * EXPECTED_PER_MIN / 60.0
        if len(window) < 0.6 * expected:
            return False, "Data berat belum cukup untuk mencatat berat awal. Tunggu beberapa detik."
        m0 = drying.interquartile_mean([x["grams"] * 1000.0 for x in window])
        if m0 < s.min_sample_mg:
            return False, (f"Berat sampel {m0:.1f} mg di bawah minimum {s.min_sample_mg:g} mg. "
                           "Sampel sudah dimasukkan?")

        creep = self._slope_mg_per_min(CREEP_WINDOW_S, now)
        if creep is not None and abs(creep) > s.cutoff_rate_mg_min / 2:
            self._warn(f"Saat pemanas mulai, berat masih bergerak {creep:+.1f} mg/menit (creep). "
                       "Menit-menit awal akan bias; tunggu lebih lama sebelum memanaskan.")

        self.m0_mg = m0
        self.t_heat = now
        self.cur_index = 0
        self.cur_samples = []
        self.phase = "heating"
        self.message = "Pemanasan berjalan. Titik kurva muncul tiap 1 menit."
        self._event(now, f"Pemanas mulai. Berat awal m0 = {m0:.1f} mg")
        # TODO(heater): kirim perintah pemanas ON ke firmware setelah hardware ada.
        self._persist()
        return True, self.message

    # ------------------------------------------------------------------
    #  Pemanasan: agregasi per menit
    # ------------------------------------------------------------------
    def _feed_heating(self, sample, usable, now):
        index = int((now - self.t_heat) // 60)
        while index > self.cur_index:
            self._finalize_minute(self.cur_index, self.cur_samples, now)
            self.cur_samples = []
            self.cur_index += 1
            if self.phase not in ("heating", "cutoff"):
                return
        if usable:
            self.cur_samples.append(sample)
        if (self.phase == "cutoff" and self.prediction
                and (now - self.t_heat) / 60.0 >= self.prediction["t_stop_min"]):
            self._finish(now, "Waktu prediksi tercapai: laju penyusutan model di bawah target.")

    def _finalize_minute(self, index, samples, now):
        s = self.settings
        flags = []
        mass = spread = temp = None
        used = 0
        if len(samples) >= MIN_SAMPLES_PER_MIN:
            grams = [x["grams"] * 1000.0 for x in samples]
            ts = [(x["t"] - self.t_heat) / 60.0 for x in samples]
            mass, _, spread, used = drying.robust_level(ts, grams, index + 0.5)
            temps = [x["temp_c"] for x in samples if x["temp_c"] is not None]
            temp = sum(temps) / len(temps) if temps else None
        else:
            flags.append("data kurang")

        previous = next((m["mass_mg"] for m in reversed(self.minutes) if m["mass_mg"] is not None),
                        self.m0_mg)
        if mass is not None and previous is not None and mass - previous > s.rise_tolerance_mg:
            flags.append("naik")

        record = {
            "index": index,
            "t_mid_min": index + 0.5,
            "n": len(samples),
            "n_used": used,
            "mass_mg": mass,
            "spread_mg": spread,
            "temp_c": temp,
            "rate_mg_min": None,
            "valid": mass is not None and not flags,
            "after_cutoff": self.phase == "cutoff",
            "flags": flags,
            "fit_residual_mg": None,
        }
        self.minutes.append(record)

        if record["after_cutoff"]:
            if self.fit and mass is not None:
                record["fit_residual_mg"] = mass - drying.model_mass(self.fit, record["t_mid_min"])
        else:
            record["rate_mg_min"] = self._rate_at(index)
            self._update_heating(record, now)
        self._persist()

    def _rate_at(self, index):
        lo = index - self.settings.rate_window_min + 1
        pts = [m for m in self.minutes if m["valid"] and not m["after_cutoff"] and lo <= m["index"] <= index]
        if len(pts) < 3:
            return None
        slope = drying.theil_sen_slope([m["t_mid_min"] for m in pts], [m["mass_mg"] for m in pts])
        return None if slope is None else -slope

    def _update_heating(self, record, now):
        s = self.settings
        rate = record["rate_mg_min"]
        elapsed = record["index"] + 1
        if rate is not None and (self.peak_rate is None or rate > self.peak_rate):
            self.peak_rate = rate

        self._update_fit(final=False)

        if elapsed >= s.max_heat_min:
            self._cutoff(now, elapsed, "Batas waktu maksimum tercapai sebelum laju turun di bawah ambang.")
            return
        if rate is None or elapsed < s.min_heat_min:
            return
        if self.peak_rate is None or self.peak_rate < s.cutoff_rate_mg_min:
            # Laju belum pernah melewati ambang: di awal pemanasan atau sampel sudah kering.
            # Cut-off di sini akan terpicu seketika, jadi ditahan.
            self.below_count = 0
            self.message = (f"Laju penyusutan belum pernah melewati {s.cutoff_rate_mg_min:g} mg/menit. "
                            "Cut-off ditahan sampai penyusutan benar-benar terjadi.")
            return
        if rate < s.cutoff_rate_mg_min:
            self.below_count += 1
            self.message = (f"Laju {rate:.1f} mg/menit di bawah ambang "
                            f"({self.below_count}/{s.cutoff_confirm_min} menit).")
            if self.below_count >= s.cutoff_confirm_min:
                self._cutoff(now, elapsed,
                             f"Laju penyusutan di bawah {s.cutoff_rate_mg_min:g} mg/menit selama "
                             f"{s.cutoff_confirm_min} menit berturut-turut.")
        else:
            self.below_count = 0
            self.message = f"Pemanasan berjalan. Laju penyusutan {rate:.1f} mg/menit."

    # ------------------------------------------------------------------
    #  Fit dan prediksi
    # ------------------------------------------------------------------
    def _fit_points(self):
        """Titik untuk fit: periode laju menurun saja.

        Kurva pengeringan klasik punya tiga bagian: pemanasan awal (laju naik),
        laju konstan, lalu laju menurun. Model eksponensial hanya berlaku untuk
        bagian terakhir, jadi fit dimulai dari menit terakhir yang lajunya masih
        >= 90 % puncak. Laju di menit k dihitung dari jendela k-4..k, sehingga
        awal fit digeser mundur setengah jendela.
        """
        valid = [m for m in self.minutes if m["valid"] and not m["after_cutoff"]]
        if self.peak_rate is None or self.peak_rate <= 0:
            return valid
        near_peak = [m["index"] for m in valid
                     if m["rate_mg_min"] is not None and m["rate_mg_min"] >= 0.9 * self.peak_rate]
        plateau_end = max(near_peak) if near_peak else 0
        start = max(0, plateau_end - self.settings.rate_window_min // 2)
        window = [m for m in valid if m["index"] >= start]
        return window if len(window) >= 4 else valid

    def _update_fit(self, final):
        s = self.settings
        points = self._fit_points()
        rates = [m["rate_mg_min"] for m in self.minutes if m["rate_mg_min"] is not None]
        latest_rate = rates[-1] if rates else None
        # Fit sementara baru berarti setelah laju jelas turun dari puncaknya
        falling = (self.peak_rate is not None and latest_rate is not None
                   and latest_rate <= 0.8 * self.peak_rate)
        if len(points) < 4 or (not final and not falling):
            if not final:
                self.fit = self.fit_alt = self.prediction = self.prediction_alt = None
                self.confidence = []
            return

        ts = [m["t_mid_min"] for m in points]
        ms = [m["mass_mg"] for m in points]
        use_drift = s.model == "exp_drift"
        self.fit = drying.fit_exponential(ts, ms, with_drift=use_drift)
        self.fit_alt = drying.fit_exponential(ts, ms, with_drift=not use_drift)
        self.fit_start_min = ts[0]
        self.prediction = drying.predict_stop(self.fit, s.target_rate_mg_min)
        self.prediction_alt = drying.predict_stop(self.fit_alt, s.target_rate_mg_min)
        self.confidence = self._confidence_notes(ts[-1])

    def _confidence_notes(self, t_last):
        notes = []
        fit, pred = self.fit, self.prediction
        if fit is None:
            return ["Fit gagal: data belum membentuk kurva yang bisa dimodelkan."]
        if fit["amplitude_mg"] <= 0:
            notes.append("Model tidak menunjukkan penyusutan (amplitudo <= 0).")
        if fit["tau_at_bound"]:
            notes.append("Tau menempel di batas pencarian: kurva belum cukup melengkung, prediksi tidak "
                         "dapat dipercaya.")
        if fit["r2"] < 0.98:
            notes.append(f"R2 {fit['r2']:.4f}: data tidak mengikuti model eksponensial dengan baik.")
        if pred and t_last > fit["t0"]:
            reach = (pred["t_stop_min"] - t_last) / (t_last - fit["t0"])
            if reach > 3:
                notes.append(f"Prediksi mengekstrapolasi {reach:.1f}x lebih jauh dari rentang data yang difit.")
        alt = self.prediction_alt
        if pred and alt and pred["t_stop_min"] > 0:
            gap = abs(alt["t_stop_min"] - pred["t_stop_min"]) / pred["t_stop_min"]
            if gap > 0.3:
                notes.append(f"Model dengan dan tanpa drift berbeda {gap * 100:.0f} % pada waktu berhenti: "
                             "ketidakpastian prediksi besar.")
        return notes

    def _cutoff(self, now, elapsed, reason):
        self.cutoff_min = elapsed
        if self.peak_rate is None or self.peak_rate < self.settings.cutoff_rate_mg_min:
            # Tanpa penyusutan berarti, fit hanya menangkap noise dan tetap
            # menghasilkan "waktu berhenti" yang terlihat meyakinkan. Lebih
            # jujur tidak memberi prediksi sama sekali.
            self.fit = self.fit_alt = self.prediction = self.prediction_alt = None
            self.confidence = []
            self._finish(now, f"{reason} Penyusutan tidak pernah melewati "
                              f"{self.settings.cutoff_rate_mg_min:g} mg/menit, jadi tidak ada prediksi.")
            return
        self._update_fit(final=True)
        self.fit_final = True
        self.phase = "cutoff"
        self._event(now, f"Cut-off di menit {elapsed}: {reason}")
        if self.prediction is None:
            self._finish(now, f"{reason} Fit gagal, tidak ada prediksi waktu berhenti.")
        elif self.prediction["t_stop_min"] <= elapsed:
            self._finish(now, f"{reason} Laju model sudah di bawah target saat cut-off.")
        elif elapsed >= self.settings.max_heat_min:
            self._finish(now, reason)
        else:
            self.message = (f"Cut-off di menit {elapsed}. Load cell tidak dipakai lagi untuk kurva; "
                            f"prediksi berhenti di menit {self.prediction['t_stop_min']:.1f}.")

    # ------------------------------------------------------------------
    #  Akhir siklus
    # ------------------------------------------------------------------
    def _finish(self, now, reason):
        self.phase = "done"
        self.end_reason = reason
        self.message = f"Selesai: {reason}"
        self._event(now, f"Selesai: {reason}")
        # TODO(heater): kirim perintah pemanas OFF ke firmware setelah hardware ada.
        self._persist()
        self._close_samples()

    def _abort(self, now, reason):
        self.phase = "aborted"
        self.end_reason = reason
        self.message = f"Dibatalkan: {reason}"
        self._event(now, f"Dibatalkan: {reason}")
        self._persist()
        self._close_samples()

    # ------------------------------------------------------------------
    #  Penyimpanan
    # ------------------------------------------------------------------
    def _write_sample(self, sample):
        if self._samples_writer is None:
            return
        t_rel = None if self.t_heat is None else round(sample["t"] - self.t_heat, 2)
        self._samples_writer.writerow([
            datetime.now().isoformat(timespec="milliseconds"), self.phase, t_rel,
            sample["fw_seconds"], sample["grams"], sample["raw"], sample["status"], sample["temp_c"],
        ])

    def _close_samples(self):
        if self._samples_handle is not None:
            self._samples_handle.close()
        self._samples_handle = None
        self._samples_writer = None

    def _persist(self):
        if self.folder is None:
            return
        if self._samples_handle is not None:
            self._samples_handle.flush()
        with (self.folder / "minutes.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=MINUTE_COLUMNS)
            writer.writeheader()
            for minute in self.minutes:
                row = dict(minute)
                row["flags"] = ";".join(minute["flags"])
                writer.writerow(row)
        summary = self._summary()
        summary["settings"] = asdict(self.settings)
        summary["events"] = self.events
        (self.folder / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    # ------------------------------------------------------------------
    #  Ringkasan untuk web
    # ------------------------------------------------------------------
    def _slope_mg_per_min(self, window_s, now):
        pts = [x for x in self.recent if now - x["t"] <= window_s]
        if len(pts) < 0.5 * window_s * EXPECTED_PER_MIN / 60.0:
            return None
        line = drying.ols_line([x["t"] / 60.0 for x in pts], [x["grams"] * 1000.0 for x in pts])
        return None if line is None else line[0]

    def _summary(self):
        s = self.settings
        latest = next((m for m in reversed(self.minutes) if m["mass_mg"] is not None), None)
        latest_rate = next((m["rate_mg_min"] for m in reversed(self.minutes)
                            if m["rate_mg_min"] is not None), None)
        result = {
            "cycleId": self.cycle_id,
            "phase": self.phase,
            "endReason": self.end_reason,
            "m0Mg": _round(self.m0_mg, 2),
            "latestMassMg": _round(latest["mass_mg"], 2) if latest else None,
            "latestRateMgMin": _round(latest_rate, 3),
            "peakRateMgMin": _round(self.peak_rate, 3),
            "cutoffMin": self.cutoff_min,
            "fitStartMin": self.fit_start_min,
            "fitFinal": self.fit_final,
            "fit": self.fit,
            "fitAlt": self.fit_alt,
            "prediction": self.prediction,
            "predictionAlt": self.prediction_alt,
            "confidence": self.confidence,
            "warnings": self.warnings,
        }
        if self.m0_mg and latest:
            loss = self.m0_mg - latest["mass_mg"]
            result["lossMg"] = round(loss, 2)
            result["lossPct"] = round(loss / self.m0_mg * 100.0, 3)
        if self.m0_mg and self.prediction:
            at_stop = self.prediction["mass_at_stop_mg"]
            result["moistureAtStopPct"] = round((self.m0_mg - at_stop) / self.m0_mg * 100.0, 3)
            result["moistureAsymptoticPct"] = round((self.m0_mg - self.fit["m_inf_mg"]) / self.m0_mg * 100.0, 3)
        validation = [m["fit_residual_mg"] for m in self.minutes if m["fit_residual_mg"] is not None]
        if validation:
            result["validationRmsMg"] = round(math.sqrt(sum(v * v for v in validation) / len(validation)), 3)
            result["validationPoints"] = len(validation)
        result["cutoffRateMgMin"] = s.cutoff_rate_mg_min
        result["targetRateMgMin"] = s.target_rate_mg_min
        return result

    def snapshot(self, now):
        with self.lock:
            s = self.settings
            drift = self._slope_mg_per_min(READINESS_WINDOW_S, now)
            creep = self._slope_mg_per_min(CREEP_WINDOW_S, now)
            valid = [m for m in self.minutes if m["valid"] and not m["after_cutoff"]]
            monotone = drying.isotonic_nonincreasing([m["mass_mg"] for m in valid])
            live = self.recent[-1] if self.recent else None
            elapsed = None if self.t_heat is None else (now - self.t_heat) / 60.0
            countdown = None
            if self.phase == "cutoff" and self.prediction and elapsed is not None:
                countdown = max(0.0, self.prediction["t_stop_min"] - elapsed)

            snap = self._summary()
            snap.update({
                "phaseLabel": PHASE_LABELS[self.phase],
                "message": self.message,
                "settings": asdict(s),
                "elapsedMin": _round(elapsed, 3),
                "countdownMin": _round(countdown, 3),
                "currentMinuteSamples": len(self.cur_samples) if self.phase in ("heating", "cutoff") else None,
                "zeroDriftMgMin": _round(drift, 2),
                "zeroReady": drift is not None and abs(drift) <= s.ready_drift_mg_min,
                "creepMgMin": _round(creep, 2),
                "liveMg": _round(live["grams"] * 1000.0, 2) if live else None,
                "minutes": self.minutes,
                "monotone": [{"t_mid_min": m["t_mid_min"], "mass_mg": round(v, 3)} for m, v in zip(valid, monotone)],
                "events": self.events[-20:],
                "files": None if self.folder is None else ["samples.csv", "minutes.csv", "summary.json"],
                "rtdSaturationSeen": self.rtd_saturation_seen,
            })
            return snap

    def file_path(self, name):
        if self.folder is None or name not in ("samples.csv", "minutes.csv", "summary.json"):
            return None
        path = self.folder / name
        return path if path.exists() else None
