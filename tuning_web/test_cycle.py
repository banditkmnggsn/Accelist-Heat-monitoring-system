"""Uji matematika pengeringan dan siklus lengkap tanpa hardware.

Jalankan dari folder tuning_web:
    python -m unittest test_cycle -v

Siklus diuji lewat server.handle_line, jalur yang sama persis dengan baris
serial asli, dengan jam palsu 2 baris per detik.
"""

import json
import math
import random
import tempfile
import unittest
from pathlib import Path

import drying
import server
from cycle import DryingCycle


def report_line(fw_seconds, grams, status="STABLE", temp=23.6, uncal=False):
    return (f"[{fw_seconds:6.1f}s] RTD {temp:.2f} C (R=109.19, fault 0x00) | "
            f"W {grams:.4f} g {status} (raw 148141){' UNCAL' if uncal else ''} | ENC 0 btn:-")


class Rig:
    """Firmware palsu + jam palsu yang memberi makan server.handle_line."""

    def __init__(self, folder, seed=7):
        self.sent = []
        self.cycle = DryingCycle(Path(folder), self._send)
        server.drying_cycle = self.cycle
        self.now = 1000.0
        self.fw = 50.0
        self.rng = random.Random(seed)

    def _send(self, command):
        self.sent.append(command)
        return True

    def run(self, seconds, mass_mg, noise_mg=0.5):
        for _ in range(int(round(seconds / 0.5))):
            grams = (mass_mg(self.now) + self.rng.gauss(0.0, noise_mg)) / 1000.0
            server.handle_line(report_line(self.fw, grams), self.now)
            self.now += 0.5
            self.fw += 0.5

    def log(self, text):
        server.handle_line(text, self.now)

    def act(self, name, settings=None):
        return self.cycle.action(name, self.now, settings)

    def prepare_and_load(self, m0_mg):
        """Nol -> Mulai siklus -> Tare -> sampel masuk, siap dipanaskan."""
        self.run(200, lambda t: 0.0)
        self.assertion(self.act("start"))
        self.assertion(self.act("tare"))
        self.run(3, lambda t: 0.0)
        self.log("# Tare selesai: offset = 114725.36 count (tersimpan di NVS)")
        self.run(60, lambda t: m0_mg)

    @staticmethod
    def assertion(result):
        ok, message = result
        if not ok:
            raise AssertionError(message)


class DryingMathTest(unittest.TestCase):
    def test_interquartile_mean_ignores_one_sided_disturbance(self):
        values = [1000.0] * 100 + [1060.0] * 20   # tangan menekan wadah 1/6 menit
        self.assertAlmostEqual(drying.interquartile_mean(values), 1000.0)
        self.assertGreater(sum(values) / len(values), 1009.0)

    def test_robust_level_unbiased_on_falling_signal_with_disturbance(self):
        # 40 mg/menit turun, tisu menekan +50 mg selama detik 20..30
        ts = [i / 120.0 for i in range(120)]
        clean = [5000.0 - 40.0 * t for t in ts]
        disturbed = [v + (50.0 if 20 <= i / 2 < 30 else 0.0) for i, v in enumerate(clean)]
        truth = 5000.0 - 40.0 * 0.5
        level, slope, _, used = drying.robust_level(ts, disturbed, 0.5)
        self.assertAlmostEqual(level, truth, delta=0.05)
        self.assertAlmostEqual(slope, -40.0, delta=0.1)
        self.assertEqual(used, 100)                        # 20 sampel gangguan dibuang
        naive = drying.interquartile_mean(disturbed)
        self.assertGreater(abs(naive - truth), 3.0)        # cara lama bias ~3,4 mg

    def test_theil_sen_survives_one_bad_minute(self):
        xs, ys = [0, 1, 2, 3, 4], [100, 90, 80, 250, 60]
        self.assertAlmostEqual(-drying.theil_sen_slope(xs, ys), 10.0)
        self.assertLess(-drying.ols_line(xs, ys)[0], 0.0)   # least squares terbalik tanda

    def test_isotonic_is_nonincreasing_least_squares(self):
        self.assertEqual(drying.isotonic_nonincreasing([5, 3, 4, 2]), [5, 3.5, 3.5, 2])
        rng = random.Random(3)
        noise = [1000 + rng.gauss(0, 0.5) for _ in range(60)]
        fitted = drying.isotonic_nonincreasing(noise)
        self.assertTrue(all(a >= b for a, b in zip(fitted, fitted[1:])))
        running_min, current = [], math.inf
        for value in noise:
            current = min(current, value)
            running_min.append(current)
        # Minimum berjalan menciptakan penyusutan palsu dari noise murni; PAVA jauh lebih kecil
        self.assertGreater(abs(sum(running_min) / 60 - 1000), 4 * abs(sum(fitted) / 60 - 1000))

    def test_exponential_fit_recovers_known_curve(self):
        ts = [i + 0.5 for i in range(3, 25)]
        ms = [4200 + 800 * math.exp(-t / 12.0) for t in ts]
        fit = drying.fit_exponential(ts, ms)
        self.assertAlmostEqual(fit["tau_min"], 12.0, places=3)
        self.assertAlmostEqual(fit["m_inf_mg"], 4200.0, places=2)
        stop = drying.predict_stop(fit, 0.1)
        self.assertAlmostEqual(stop["t_stop_min"], 12.0 * math.log(800 / 1.2), places=2)
        self.assertAlmostEqual(stop["remaining_mg"], 1.2, places=3)   # tau * target

    def test_drift_model_corrects_linear_creep(self):
        rng = random.Random(5)
        ts = [i + 0.5 for i in range(3, 25)]
        ms = [4200 + 800 * math.exp(-t / 12.0) + 2.0 * (t - ts[0]) + rng.gauss(0, 0.5) for t in ts]
        true_stop = 12.0 * math.log(800 / 1.2)
        plain = drying.predict_stop(drying.fit_exponential(ts, ms), 0.1)["t_stop_min"]
        drift = drying.predict_stop(drying.fit_exponential(ts, ms, with_drift=True), 0.1)["t_stop_min"]
        self.assertGreater(abs(plain - true_stop), 3 * abs(drift - true_stop))


class CycleTest(unittest.TestCase):
    M_INF, AMP, TAU, START = 4400.0, 600.0, 10.0, 1.0

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.rig = Rig(self.tmp.name)

    def tearDown(self):
        self.rig.cycle._close_samples()
        self.tmp.cleanup()

    def true_mass(self, minutes):
        return self.M_INF + self.AMP * math.exp(-max(0.0, minutes - self.START) / self.TAU)

    def test_full_cycle_predicts_stop_time(self):
        rig = self.rig
        rig.prepare_and_load(self.M_INF + self.AMP)
        self.assertEqual(rig.sent, ["t"])
        rig.assertion(rig.act("heat"))
        t_heat = rig.now

        def mass(now):
            minutes = (now - t_heat) / 60.0
            value = self.true_mass(minutes)
            if 5 <= minutes < 6 and 20 <= (now - t_heat) % 60 < 30:
                value += 50.0          # tisu menekan wadah 10 detik
            if 8 <= minutes < 9:
                value += 45.0          # satu menit berat "naik" yang mustahil
            return value

        while rig.cycle.phase in ("heating", "cutoff") and rig.now - t_heat < 100 * 60:
            rig.run(30, mass)

        snap = rig.cycle.snapshot(rig.now)
        true_stop = self.START + self.TAU * math.log(self.AMP / (self.TAU * 0.1))
        self.assertEqual(snap["phase"], "done", snap["message"])
        self.assertIn("Waktu prediksi tercapai", snap["endReason"])
        self.assertTrue(18 <= snap["cutoffMin"] <= 26, snap["cutoffMin"])
        self.assertAlmostEqual(snap["fit"]["tau_min"], self.TAU, delta=0.5)
        self.assertAlmostEqual(snap["prediction"]["t_stop_min"], true_stop, delta=0.05 * true_stop)
        self.assertAlmostEqual(snap["m0Mg"], self.M_INF + self.AMP, delta=1.0)

        minute5 = next(m for m in snap["minutes"] if m["index"] == 5)
        self.assertTrue(minute5["valid"], "tekanan tisu 10 detik harus ditolak IQM, bukan menit dibuang")
        self.assertAlmostEqual(minute5["mass_mg"], self.true_mass(5.5), delta=2.0)
        minute8 = next(m for m in snap["minutes"] if m["index"] == 8)
        self.assertIn("naik", minute8["flags"])
        self.assertFalse(minute8["valid"])

        monotone = [p["mass_mg"] for p in snap["monotone"]]
        self.assertTrue(all(a >= b for a, b in zip(monotone, monotone[1:])))
        self.assertLess(snap["validationRmsMg"], 1.5)

        folder = rig.cycle.folder
        summary = json.loads((folder / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["phase"], "done")
        minutes_csv = (folder / "minutes.csv").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(minutes_csv) - 1, len(snap["minutes"]))
        self.assertGreater(len((folder / "samples.csv").read_text(encoding="utf-8").splitlines()), 7000)

    def test_esp32_restart_aborts_cycle(self):
        rig = self.rig
        rig.prepare_and_load(5000.0)
        rig.assertion(rig.act("heat"))
        rig.run(120, lambda t: 4990.0)
        rig.fw = 3.0                    # uptime firmware mundur: ESP32 restart
        rig.run(1, lambda t: 0.0)
        self.assertEqual(rig.cycle.phase, "aborted")
        self.assertIn("restart", rig.cycle.end_reason)

    def test_firmware_banner_aborts_cycle(self):
        rig = self.rig
        rig.prepare_and_load(5000.0)
        rig.log(" Firmware v0.1.0 (build Sep 28 2026 10:00:00)")
        self.assertEqual(rig.cycle.phase, "aborted")

    def test_sample_that_never_dries_stops_at_time_limit(self):
        rig = self.rig
        rig.prepare_and_load(5000.0)
        rig.assertion(rig.act("heat", {"max_heat_min": 6}))
        rig.run(8 * 60, lambda t: 5000.0)
        self.assertEqual(rig.cycle.phase, "done")
        self.assertIn("Batas waktu maksimum", rig.cycle.end_reason)
        self.assertIn("tidak ada prediksi", rig.cycle.end_reason)
        self.assertIsNone(rig.cycle.prediction, "fit terhadap noise murni tidak boleh jadi prediksi")

    def test_heating_rejected_without_sample(self):
        rig = self.rig
        rig.prepare_and_load(10.0)
        ok, message = rig.act("heat")
        self.assertFalse(ok)
        self.assertIn("minimum", message)
        self.assertEqual(rig.cycle.phase, "load")

    def test_heating_rejected_before_tare(self):
        rig = self.rig
        rig.run(60, lambda t: 5000.0)
        rig.assertion(rig.act("start"))
        ok, _ = rig.act("heat")
        self.assertFalse(ok)

    def test_tare_timeout_returns_to_prepare(self):
        rig = self.rig
        rig.run(60, lambda t: 0.0)
        rig.assertion(rig.act("start"))
        rig.assertion(rig.act("tare"))
        rig.run(25, lambda t: 0.0)
        self.assertEqual(rig.cycle.phase, "prepare")


if __name__ == "__main__":
    unittest.main()
