"""Bangkitkan vektor emas test_drying_math dari tuning_web/drying.py.

Port C++ (lib/HeatboxCycle/src/DryingMath.cpp) harus memberi angka yang sama
dengan implementasi Python yang sudah teruji di siklus PC. Skrip ini
menjalankan drying.py pada kasus uji di bawah lalu menulis masukan beserta
jawabannya ke golden_cases.h.

Jalankan dari root proyek setiap kali drying.py atau kasus di bawah berubah:
    python test/test_drying_math/gen_golden.py

golden_cases.h ikut di-commit, jadi uji C++ tidak butuh Python.
Noise dibangkitkan dengan seed tetap; nilainya disalin apa adanya ke header,
sehingga C++ tidak perlu meniru generator acak Python.
"""

import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tuning_web"))

import drying  # noqa: E402

TARGETS = (1.0, 0.1)  # target laju siklus baru dan siklus PC lama, mg/menit


def lit(value):
    """Literal double C++ yang kembali persis ke nilai Python (repr = round-trip)."""
    text = repr(float(value))
    if text in ("inf", "-inf", "nan"):
        raise ValueError(f"nilai tidak bisa ditulis sebagai literal: {text}")
    return text


def array(name, values):
    body = ",\n    ".join(", ".join(lit(v) for v in values[i:i + 4]) for i in range(0, len(values), 4))
    return f"static const double {name}[] = {{\n    {body},\n}};\n"


def boolean(value):
    return "true" if value else "false"


# ---------------------------------------------------------------------
#  Kasus robust_level: 60 nilai per detik dalam satu menit
# ---------------------------------------------------------------------
def per_second_minute(index):
    """Waktu (menit) tengah tiap detik dalam menit ke-`index`."""
    return [index + (i + 0.5) / 60.0 for i in range(60)]


def level_cases():
    cases = []

    ts = per_second_minute(5)
    cases.append(("garis_turun_bersih", ts, [5000.0 - 40.0 * t for t in ts], 5.5))

    rng = random.Random(11)
    cases.append(("noise_gauss", ts, [4800.0 - 20.0 * t + rng.gauss(0.0, 1.5) for t in ts], 5.5))

    rng = random.Random(12)
    cases.append(("tisu_10_detik", ts,
                  [5000.0 - 40.0 * t + rng.gauss(0.0, 1.2) + (50.0 if 20 <= i < 30 else 0.0)
                   for i, t in enumerate(ts)], 5.5))

    rng = random.Random(13)
    spikes = {7: 200.0, 41: -150.0}
    cases.append(("dua_spike", ts,
                  [4500.0 - 10.0 * t + rng.gauss(0.0, 1.2) + spikes.get(i, 0.0) for i, t in enumerate(ts)],
                  5.5))

    rng = random.Random(14)
    cases.append(("langkah_tengah_menit", ts,
                  [3000.0 + rng.gauss(0.0, 1.2) + (300.0 if i >= 36 else 0.0) for i, t in enumerate(ts)],
                  5.5))

    # W0: 45 detik setelah 10 detik settling, diekstrapolasi balik ke t = 0
    rng = random.Random(15)
    ts_w0 = [(10 + i) / 60.0 for i in range(45)]
    cases.append(("ekstrapolasi_w0", ts_w0, [5200.0 - 15.0 * t + rng.gauss(0.0, 1.2) for t in ts_w0], 0.0))

    rng = random.Random(16)
    ts_short = [12.0 + i / 60.0 for i in range(7)]
    cases.append(("titik_sedikit", ts_short, [4000.0 - 5.0 * t + rng.gauss(0.0, 1.0) for t in ts_short], 12.05))
    return cases


# ---------------------------------------------------------------------
#  Kasus theil_sen_slope: jendela laju 5 menit
# ---------------------------------------------------------------------
def slope_cases():
    rng = random.Random(21)
    xs_noise = [10.5 + i for i in range(5)]
    rng2 = random.Random(22)
    xs_gap = [10.5, 11.5, 13.5, 14.5]
    return [
        ("satu_menit_rusak", [0.0, 1.0, 2.0, 3.0, 4.0], [100.0, 90.0, 80.0, 250.0, 60.0]),
        ("noise_5_menit", xs_noise, [4500.0 - 6.0 * x + rng.gauss(0.0, 0.4) for x in xs_noise]),
        ("menit_hilang", xs_gap, [4300.0 - 4.0 * x + rng2.gauss(0.0, 0.4) for x in xs_gap]),
        ("x_sama", [1.0, 1.0, 1.0], [5.0, 6.0, 7.0]),
    ]


# ---------------------------------------------------------------------
#  Kasus fit_exponential + predict_stop
# ---------------------------------------------------------------------
def fit_cases():
    cases = []
    ts = [i + 0.5 for i in range(3, 25)]
    cases.append(("eksp_bersih", ts, [4200.0 + 800.0 * math.exp(-t / 12.0) for t in ts], False))

    rng = random.Random(5)
    drifted = [4200.0 + 800.0 * math.exp(-t / 12.0) + 2.0 * (t - ts[0]) + rng.gauss(0.0, 0.5) for t in ts]
    cases.append(("eksp_drift_tanpa_suku", ts, drifted, False))
    cases.append(("eksp_drift_dengan_suku", ts, drifted, True))

    rng = random.Random(31)
    ts_cycle = [i + 0.5 for i in range(8, 18)]
    cases.append(("siklus_tipikal", ts_cycle,
                  [4400.0 + 600.0 * math.exp(-(t - 1.0) / 10.0) + rng.gauss(0.0, 0.3) for t in ts_cycle], False))

    rng = random.Random(32)
    ts_line = [i + 0.5 for i in range(10, 16)]
    cases.append(("hampir_lurus", ts_line, [5000.0 - 3.0 * t + rng.gauss(0.0, 0.05) for t in ts_line], False))

    ts_small = [i + 0.5 for i in range(10, 17)]
    cases.append(("amplitudo_kecil", ts_small,
                  [4400.0 + 3.0 * math.exp(-(t - 10.0) / 10.0) for t in ts_small], False))

    cases.append(("massa_naik", ts_small, [4400.0 - 50.0 * math.exp(-(t - 10.0) / 4.0) for t in ts_small], False))
    cases.append(("titik_kurang", [10.5, 11.5, 12.5], [5000.0, 4990.0, 4985.0], False))
    cases.append(("drift_titik_kurang", [10.5, 11.5, 12.5, 13.5], [5000.0, 4990.0, 4985.0, 4982.0], True))
    return cases


# ---------------------------------------------------------------------
#  Penulisan header
# ---------------------------------------------------------------------
def main():
    out = [
        "#pragma once\n",
        "// =====================================================================\n",
        "//  golden_cases.h — DIBANGKITKAN OTOMATIS oleh gen_golden.py dari\n",
        "//  tuning_web/drying.py. JANGAN diedit manual; jalankan ulang skripnya.\n",
        "// =====================================================================\n\n",
        "#include <cstddef>\n\n",
        "namespace golden {\n\n",
        "struct LevelCase {\n    const char* name;\n    size_t n;\n    const double* ts;\n"
        "    const double* ys;\n    double tRef;\n    double level;\n    double slope;\n"
        "    double scale;\n    size_t used;\n};\n\n",
        "struct SlopeCase {\n    const char* name;\n    size_t n;\n    const double* xs;\n"
        "    const double* ys;\n    bool ok;\n    double slope;\n};\n\n",
        "struct StopCase {\n    bool ok;\n    double target;\n    double tStop;\n    double massAtStop;\n"
        "    double remaining;\n};\n\n",
        "struct FitCase {\n    const char* name;\n    size_t n;\n    const double* ts;\n    const double* ms;\n"
        "    bool withDrift;\n    bool ok;\n    double t0;\n    double mInf;\n    double amplitude;\n"
        "    double tau;\n    double drift;\n    double rms;\n    double r2;\n    bool tauAtBound;\n"
        f"    StopCase stops[{len(TARGETS)}];\n}};\n\n",
    ]

    rows = []
    for k, (name, ts, ys, t_ref) in enumerate(level_cases()):
        out.append(array(f"kLevel{k}Ts", ts))
        out.append(array(f"kLevel{k}Ys", ys))
        level, slope, scale, used = drying.robust_level(ts, ys, t_ref)
        rows.append(f'    {{"{name}", {len(ts)}, kLevel{k}Ts, kLevel{k}Ys, {lit(t_ref)}, {lit(level)}, '
                    f'{lit(slope)}, {lit(scale)}, {used}}},\n')
    out.append("\nstatic const LevelCase kLevelCases[] = {\n" + "".join(rows) + "};\n\n")

    rows = []
    for k, (name, xs, ys) in enumerate(slope_cases()):
        out.append(array(f"kSlope{k}Xs", xs))
        out.append(array(f"kSlope{k}Ys", ys))
        slope = drying.theil_sen_slope(xs, ys)
        rows.append(f'    {{"{name}", {len(xs)}, kSlope{k}Xs, kSlope{k}Ys, {boolean(slope is not None)}, '
                    f'{lit(slope if slope is not None else 0.0)}}},\n')
    out.append("\nstatic const SlopeCase kSlopeCases[] = {\n" + "".join(rows) + "};\n\n")

    rows = []
    for k, (name, ts, ms, with_drift) in enumerate(fit_cases()):
        out.append(array(f"kFit{k}Ts", ts))
        out.append(array(f"kFit{k}Ms", ms))
        fit = drying.fit_exponential(ts, ms, with_drift=with_drift)
        stops = []
        for target in TARGETS:
            stop = drying.predict_stop(fit, target)
            if stop is None:
                stops.append(f"{{false, {lit(target)}, 0.0, 0.0, 0.0}}")
            else:
                stops.append(f"{{true, {lit(target)}, {lit(stop['t_stop_min'])}, "
                             f"{lit(stop['mass_at_stop_mg'])}, {lit(stop['remaining_mg'])}}}")
        if fit is None:
            values = "false, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false"
        else:
            values = (f"true, {lit(fit['t0'])}, {lit(fit['m_inf_mg'])}, {lit(fit['amplitude_mg'])}, "
                      f"{lit(fit['tau_min'])}, {lit(fit['drift_mg_per_min'])}, {lit(fit['rms_mg'])}, "
                      f"{lit(fit['r2'])}, {boolean(fit['tau_at_bound'])}")
        rows.append(f'    {{"{name}", {len(ts)}, kFit{k}Ts, kFit{k}Ms, {boolean(with_drift)}, {values},\n'
                    f'     {{{", ".join(stops)}}}}},\n')
    out.append("\nstatic const FitCase kFitCases[] = {\n" + "".join(rows) + "};\n\n")

    out.append("}  // namespace golden\n")
    target = HERE / "golden_cases.h"
    target.write_text("".join(out), encoding="utf-8", newline="\n")
    print(f"Ditulis: {target}")


if __name__ == "__main__":
    main()
