"""Perhitungan kalibrasi multi-titik load cell.

Hanya memakai stdlib supaya bisa jalan di laptop tanpa hak admin.
Semua masukan adalah RAW COUNTS dari firmware; konversi ke mg dilakukan di
sini, sehingga data mentah tetap berguna kalau rumusnya diganti nanti.
"""

import math

CAPACITY_MG = 300_000.0  # load cell 300 g


def zero_reference(point):
    """Nol yang dipakai untuk satu titik.

    Kalau ada nol sebelum DAN sesudah, rata-ratanya dipakai: itu membatalkan
    drift linear selama titik diukur, yang pada hardware ini ~14 mg/menit dan
    jauh lebih besar dari noise.
    """
    before, after = point.get("zero_before"), point.get("zero_after")
    if before is not None and after is not None:
        return (before + after) / 2.0
    return before if before is not None else after


def net_counts(point):
    zero = zero_reference(point)
    return None if zero is None else point["raw_mean"] - zero


def usable_points(points):
    return [p for p in points if net_counts(p) is not None]


def fit_linear(points):
    """Least-squares net_counts = intercept + slope * nominal_mg."""
    usable = usable_points(points)
    if len(usable) < 2:
        return None

    xs = [p["nominal_mg"] for p in usable]
    ys = [net_counts(p) for p in usable]
    n = len(xs)
    mean_x, mean_y = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    if sxx == 0.0:
        return None  # semua titik pada nominal yang sama

    slope = sum((xs[i] - mean_x) * (ys[i] - mean_y) for i in range(n)) / sxx
    intercept = mean_y - slope * mean_x
    if slope == 0.0:
        return None

    ss_tot = sum((y - mean_y) ** 2 for y in ys)
    ss_res = sum((ys[i] - (intercept + slope * xs[i])) ** 2 for i in range(n))
    r2 = 1.0 if ss_tot == 0.0 else 1.0 - ss_res / ss_tot

    return {
        "slope_counts_per_mg": slope,
        "intercept_counts": intercept,
        "counts_per_gram": slope * 1000.0,
        "r2": r2,
        "n_points": n,
    }


def measured_mg(point, fit):
    """Pembacaan satu titik dalam mg menurut fit."""
    net = net_counts(point)
    if net is None or fit is None:
        return None
    return (net - fit["intercept_counts"]) / fit["slope_counts_per_mg"]


def uncertainty_mg(point, fit):
    """Ketidakpastian statistik titik ini: SD rata-rata, dikonversi ke mg.

    Ini yang menentukan apakah sebuah anak timbangan cukup besar untuk
    terukur. Kalau nominal_mg tidak jauh lebih besar dari angka ini, titik
    tersebut praktis hanya noise.
    """
    if fit is None or not point.get("samples"):
        return None
    standard_error = point["raw_sd"] / math.sqrt(point["samples"])
    return abs(standard_error / fit["slope_counts_per_mg"])


def residuals(points, fit):
    out = []
    for point in usable_points(points):
        reading = measured_mg(point, fit)
        out.append({
            "nominal_mg": point["nominal_mg"],
            "direction": point["direction"],
            "raw_mean": point["raw_mean"],
            "raw_sd": point["raw_sd"],
            "net_counts": net_counts(point),
            "samples": point.get("samples"),
            "measured_mg": reading,
            "residual_mg": None if reading is None else reading - point["nominal_mg"],
            "uncertainty_mg": uncertainty_mg(point, fit),
            "millis": point.get("millis"),
        })
    return out


def _stdev(values):
    if len(values) < 2:
        return None
    mean = sum(values) / len(values)
    return math.sqrt(sum((v - mean) ** 2 for v in values) / (len(values) - 1))


def repeatability_summary(points):
    """Sebaran antar pengulangan per nominal, tanpa perlu fit garis.

    Titik-titik pada SATU nominal tidak bisa menghasilkan garis kalibrasi,
    tetapi tetap menjawab pertanyaan penting: seberapa sama hasilnya kalau
    beban yang sama diletakkan berulang kali. `expected_sd_counts` adalah
    perkiraan sebaran yang bisa dijelaskan noise elektronik saja (rata-rata
    64 sampel titik ditambah nol acuannya); kalau sebaran teramati jauh lebih
    besar, penyebabnya mekanis — posisi beban di wadah atau creep yang
    berbeda karena waktu tunggu yang berbeda.
    """
    groups = {}
    for point in usable_points(points):
        groups.setdefault((point["nominal_mg"], point["direction"]), []).append(point)
    out = []
    for (nominal, direction), group in sorted(groups.items()):
        nets = [net_counts(p) for p in group]
        mean = sum(nets) / len(nets)
        cpg = mean / nominal * 1000.0 if nominal else None
        expected = []
        for p in group:
            point_se = p["raw_sd"] / math.sqrt(p["samples"])
            zero_share = 0.5 if p.get("zero_before") is not None and p.get("zero_after") is not None else 1.0
            expected.append(point_se * math.sqrt(1.0 + zero_share))
        spread = _stdev(nets)
        out.append({
            "nominal_mg": nominal,
            "direction": direction,
            "n": len(nets),
            "mean_net_counts": mean,
            "counts_per_gram": cpg,
            "cpg_min": min(nets) / nominal * 1000.0 if nominal else None,
            "cpg_max": max(nets) / nominal * 1000.0 if nominal else None,
            "sd_counts": spread,
            "sd_mg": None if spread is None or not cpg else spread / cpg * 1000.0,
            "expected_sd_counts": sum(expected) / len(expected),
        })
    return out


def metrics(points, fit):
    if fit is None:
        return None

    rows = residuals(points, fit)
    errors = [abs(r["residual_mg"]) for r in rows if r["residual_mg"] is not None]
    if not errors:
        return None

    max_residual = max(errors)
    rms_residual = math.sqrt(sum(e * e for e in errors) / len(errors))

    # Histeresis: selisih pembacaan pada nominal yang punya titik naik & turun
    by_nominal = {}
    for row in rows:
        by_nominal.setdefault(row["nominal_mg"], {}).setdefault(row["direction"], []).append(
            row["measured_mg"])
    hysteresis = None
    for directions in by_nominal.values():
        if "U" in directions and "D" in directions:
            up = sum(directions["U"]) / len(directions["U"])
            down = sum(directions["D"]) / len(directions["D"])
            gap = abs(down - up)
            hysteresis = gap if hysteresis is None else max(hysteresis, gap)

    # Repeatability: SD antar pengulangan pada nominal + arah yang sama
    repeatability = None
    for directions in by_nominal.values():
        for readings in directions.values():
            spread = _stdev(readings)
            if spread is not None:
                repeatability = spread if repeatability is None else max(repeatability, spread)

    return {
        "max_residual_mg": max_residual,
        "rms_residual_mg": rms_residual,
        "nonlinearity_pct_fs": max_residual / CAPACITY_MG * 100.0,
        "hysteresis_mg": hysteresis,
        "repeatability_mg": repeatability,
        "n_points": len(rows),
    }


def report_text(fit, stats, rows):
    """Ringkasan Bahasa Indonesia formal, siap ditempel ke laporan magang."""
    if fit is None or stats is None:
        return "Belum ada cukup titik kalibrasi untuk dilaporkan (minimal dua titik)."

    samples = rows[0].get("samples") or 0
    lines = [
        "Kalibrasi load cell dilakukan dengan metode multi-titik menggunakan anak "
        "timbangan bersertifikat. Setiap titik diukur dari {} sampel mentah dan "
        "dikoreksi terhadap pembacaan nol sebelum serta sesudah pembebanan untuk "
        "membatalkan drift. Regresi linear atas {} titik menghasilkan faktor skala "
        "{:.3f} count/gram dengan koefisien determinasi R² = {:.6f}. "
        "Residual terbesar adalah {:.3f} mg dan residual RMS {:.3f} mg, setara "
        "nonlinearitas {:.4f} % dari kapasitas penuh.".format(
            samples, fit["n_points"], fit["counts_per_gram"], fit["r2"],
            stats["max_residual_mg"], stats["rms_residual_mg"],
            stats["nonlinearity_pct_fs"]),
    ]
    if stats["hysteresis_mg"] is not None:
        lines.append("Histeresis maksimum antara arah pembebanan naik dan turun "
                     "tercatat {:.3f} mg.".format(stats["hysteresis_mg"]))
    if stats["repeatability_mg"] is not None:
        lines.append("Repeatability terburuk antar pengulangan pada nominal yang sama "
                     "adalah {:.3f} mg.".format(stats["repeatability_mg"]))

    lines.append("")
    lines.append("{:>12} {:>5} {:>14} {:>14} {:>13} {:>13}".format(
        "nominal(mg)", "arah", "raw bersih", "terukur(mg)", "residual(mg)", "+/-(mg)"))
    for row in sorted(rows, key=lambda r: (r["nominal_mg"], r["direction"])):
        lines.append("{:>12.3f} {:>5} {:>14.2f} {:>14.3f} {:>13.3f} {:>13.3f}".format(
            row["nominal_mg"], row["direction"], row["net_counts"], row["measured_mg"],
            row["residual_mg"], row["uncertainty_mg"] or float("nan")))
    return "\n".join(lines)
