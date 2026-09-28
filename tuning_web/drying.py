"""Matematika kurva pengeringan untuk siklus sampel.

Hanya stdlib (sama seperti analysis.py) supaya jalan di laptop tanpa hak admin.

Satuan di seluruh modul: waktu dalam MENIT sejak pemanas mulai, massa dalam
MILIGRAM. "Laju" selalu berarti laju PENYUSUTAN: positif = massa berkurang.

Algoritma ini melanjutkan rencana awal di lib/Heatbox (firmware) yang belum
pernah disambungkan ke main.cpp: blok 60 detik, regresi atas 5 blok untuk
laju, dan fit eksponensial dengan golden-section search atas tau — termasuk
varian dengan suku drift linear.
"""

import math


def median(values):
    ordered = sorted(values)
    n = len(ordered)
    if n == 0:
        return None
    mid = n // 2
    return ordered[mid] if n % 2 else (ordered[mid - 1] + ordered[mid]) / 2.0


def interquartile_mean(values):
    """Rata-rata separuh nilai di tengah (IQM).

    Seperti median, sampai 25 % sampel di tiap sisi boleh rusak (tangan
    menyentuh wadah, tisu menekan) tanpa menggeser hasil. Berbeda dengan
    median, IQM tetap merata-rata banyak sampel sehingga tidak terkunci ke
    grid resolusi baris report.
    """
    ordered = sorted(values)
    n = len(ordered)
    if n == 0:
        return None
    if n < 4:
        return sum(ordered) / n
    cut = n // 4
    core = ordered[cut:n - cut]
    return sum(core) / len(core)


def ols_line(xs, ys):
    """(kemiringan, intersep) least squares; None kalau x tidak bervariasi."""
    n = len(xs)
    if n < 2:
        return None
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    if sxx == 0:
        return None
    sxy = sum((xs[i] - mean_x) * (ys[i] - mean_y) for i in range(n))
    slope = sxy / sxx
    return slope, mean_y - slope * mean_x


def robust_level(ts, ys, t_ref, passes=2, k=3.0, scale_floor=0.1):
    """Nilai di `t_ref` dari garis lurus yang tahan gangguan, plus sebarannya.

    Dipakai untuk nilai per menit. IQM saja tidak cukup saat massa sedang
    turun: memotong 25 % nilai tertinggi ikut membuang sampel awal menit
    (yang tertinggi karena belum menyusut), sehingga pusat waktunya bergeser
    dan nilainya bias. Pada 40 mg/menit dengan gangguan 10 detik biasnya
    3,4 mg.

    Caranya: garis least squares -> buang sampel yang menyimpang lebih dari
    k x sebaran robust -> garis dihitung ulang -> dibaca di t_ref.
    Mengembalikan (level, kemiringan, sebaran setara SD, banyak sampel dipakai).
    """
    n = len(ts)
    line = ols_line(ts, ys)
    if line is None:
        return interquartile_mean(ys), 0.0, None, n
    keep = list(range(n))
    scale = None
    for _ in range(passes):
        residuals = [ys[i] - (line[1] + line[0] * ts[i]) for i in range(n)]
        center = median(residuals)
        scale = max(scale_floor, 1.4826 * median([abs(r - center) for r in residuals]))
        candidate = [i for i in range(n) if abs(residuals[i] - center) <= k * scale]
        refit = ols_line([ts[i] for i in candidate], [ys[i] for i in candidate])
        if len(candidate) < n // 2 or refit is None:
            break
        keep, line = candidate, refit
    return line[1] + line[0] * t_ref, line[0], scale, len(keep)


def theil_sen_slope(xs, ys):
    """Median dari semua kemiringan antar-pasangan titik (estimator Theil-Sen).

    Dipakai untuk laju karena satu menit yang terganggu tidak bisa menyeret
    hasilnya; regresi least squares di lib/Heatbox tidak punya sifat ini.
    """
    slopes = []
    for i in range(len(xs)):
        for j in range(i + 1, len(xs)):
            dx = xs[j] - xs[i]
            if dx != 0:
                slopes.append((ys[j] - ys[i]) / dx)
    return median(slopes) if slopes else None


def isotonic_nonincreasing(values):
    """Proyeksi least squares ke deret yang tidak pernah naik (algoritma PAVA).

    Inilah cara yang benar menerapkan aturan "berat tidak mungkin naik".
    Cara naif — mengambil minimum berjalan — bias ke bawah: ia mengunci ke
    simpangan noise yang paling rendah, sehingga penyusutan terlihat terus
    bertambah walaupun tidak ada yang menguap.
    """
    blocks = []  # [jumlah, banyak]
    for value in values:
        blocks.append([value, 1])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] < blocks[-1][0] / blocks[-1][1]:
            total, count = blocks.pop()
            blocks[-1][0] += total
            blocks[-1][1] += count
    out = []
    for total, count in blocks:
        out.extend([total / count] * count)
    return out


def _solve(matrix, rhs):
    """Eliminasi Gauss dengan pivot parsial; None kalau (hampir) singular."""
    n = len(rhs)
    rows = [list(matrix[i]) + [rhs[i]] for i in range(n)]
    scale = max(abs(v) for row in matrix for v in row) or 1.0
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(rows[r][col]))
        if abs(rows[pivot][col]) < scale * 1e-12:
            return None
        rows[col], rows[pivot] = rows[pivot], rows[col]
        for r in range(col + 1, n):
            factor = rows[r][col] / rows[col][col]
            for c in range(col, n + 1):
                rows[r][c] -= factor * rows[col][c]
    solution = [0.0] * n
    for r in range(n - 1, -1, -1):
        known = sum(rows[r][c] * solution[c] for c in range(r + 1, n))
        solution[r] = (rows[r][n] - known) / rows[r][r]
    return solution


def _sse_for_p(ss, ys, p, with_drift):
    """SSE model untuk p = tau / durasi tetap. Model linear dalam koefisiennya.

    Waktu dinormalkan ke s = (t - t0) / durasi dalam [0, 1] seperti di
    lib/Heatbox, supaya matriks normal tetap berkondisi baik.
    """
    columns = [[1.0] * len(ss), [math.exp(-s / p) for s in ss]]
    if with_drift:
        columns.append(list(ss))
    k = len(columns)
    n = len(ss)
    ata = [[sum(columns[i][r] * columns[j][r] for r in range(n)) for j in range(k)] for i in range(k)]
    aty = [sum(columns[i][r] * ys[r] for r in range(n)) for i in range(k)]
    coef = _solve(ata, aty)
    if coef is None:
        return math.inf, None
    sse = 0.0
    for r in range(n):
        predicted = sum(coef[i] * columns[i][r] for i in range(k))
        sse += (ys[r] - predicted) ** 2
    return sse, coef


def fit_exponential(ts, ms, with_drift=False, tau_min=0.2, tau_max=600.0):
    """Fit m(t) = m_inf + A * exp(-(t - t0) / tau) [+ k * (t - t0)].

    Untuk tau tetap model ini linear, jadi koefisiennya diselesaikan least
    squares; tau dicari dengan grid logaritmik lalu golden-section di sekitar
    minimum. Grid lebih dulu dipakai supaya tidak terjebak minimum lokal.

    Suku k (varian "dengan drift") menyerap drift/creep instrumen yang linear.
    Laju PENGERINGAN yang dilaporkan hanya berasal dari suku eksponensial.
    """
    n = len(ts)
    if n < (5 if with_drift else 4):
        return None
    t0 = ts[0]
    duration = ts[-1] - t0
    if duration <= 0:
        return None
    ss = [(t - t0) / duration for t in ts]

    # Tau jauh lebih panjang dari data berarti lengkungan kurva tidak teramati
    upper = min(tau_max, 20.0 * duration)
    lo = math.log(tau_min / duration)
    hi = math.log(upper / duration)
    if hi <= lo:
        return None

    grid_size = 48
    grid = [lo + (hi - lo) * i / (grid_size - 1) for i in range(grid_size)]
    sses = [_sse_for_p(ss, ms, math.exp(lp), with_drift)[0] for lp in grid]
    best = min(range(grid_size), key=lambda i: sses[i])
    if math.isinf(sses[best]):
        return None

    a = grid[max(0, best - 1)]
    b = grid[min(grid_size - 1, best + 1)]
    ratio = (math.sqrt(5.0) - 1.0) / 2.0
    c = b - ratio * (b - a)
    d = a + ratio * (b - a)
    fc = _sse_for_p(ss, ms, math.exp(c), with_drift)[0]
    fd = _sse_for_p(ss, ms, math.exp(d), with_drift)[0]
    for _ in range(40):
        if fc < fd:
            b, d, fd = d, c, fc
            c = b - ratio * (b - a)
            fc = _sse_for_p(ss, ms, math.exp(c), with_drift)[0]
        else:
            a, c, fc = c, d, fd
            d = a + ratio * (b - a)
            fd = _sse_for_p(ss, ms, math.exp(d), with_drift)[0]

    p = math.exp((a + b) / 2.0)
    sse, coef = _sse_for_p(ss, ms, p, with_drift)
    if coef is None:
        return None

    mean = sum(ms) / n
    sst = sum((m - mean) ** 2 for m in ms)
    return {
        "model": "exp_drift" if with_drift else "exp",
        "t0": t0,
        "m_inf_mg": coef[0],
        "amplitude_mg": coef[1],
        "tau_min": p * duration,
        "drift_mg_per_min": coef[2] / duration if with_drift else 0.0,
        "rms_mg": math.sqrt(sse / n),
        "r2": 1.0 - sse / sst if sst > 0 else 1.0,
        "n_points": n,
        "t_first": ts[0],
        "t_last": ts[-1],
        "tau_at_bound": best in (0, grid_size - 1),
    }


def model_mass(fit, t, include_drift=True):
    s = t - fit["t0"]
    mass = fit["m_inf_mg"] + fit["amplitude_mg"] * math.exp(-s / fit["tau_min"])
    if include_drift:
        mass += fit["drift_mg_per_min"] * s
    return mass


def model_rate(fit, t):
    """Laju penyusutan dari komponen pengeringan saja (tanpa drift), mg/menit."""
    s = t - fit["t0"]
    return fit["amplitude_mg"] / fit["tau_min"] * math.exp(-s / fit["tau_min"])


def predict_stop(fit, target_rate):
    """Kapan laju penyusutan model turun ke `target_rate`.

    Dari r(t) = (A / tau) * exp(-(t - t0) / tau):
        t_stop = t0 + tau * ln(A / (tau * target))
    Sisa air yang belum menguap saat itu = tau * target, jadi berhenti di
    0,1 mg/menit dengan tau 20 menit meninggalkan 2 mg di sampel.
    """
    if fit is None:
        return None
    amplitude, tau = fit["amplitude_mg"], fit["tau_min"]
    if amplitude <= 0 or tau <= 0 or target_rate <= 0:
        return None
    initial_rate = amplitude / tau
    if initial_rate <= target_rate:
        t_stop = fit["t0"]
    else:
        t_stop = fit["t0"] + tau * math.log(initial_rate / target_rate)
    remaining = amplitude * math.exp(-(t_stop - fit["t0"]) / tau)
    return {
        "t_stop_min": t_stop,
        "mass_at_stop_mg": fit["m_inf_mg"] + remaining,
        "remaining_mg": remaining,
    }
