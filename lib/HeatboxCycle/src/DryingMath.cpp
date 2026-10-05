#include "DryingMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drying {

namespace {

// robustLevel (nilai bawaan drying.robust_level)
constexpr int kRobustPasses = 2;
constexpr double kRobustRejectK = 3.0;
constexpr double kRobustScaleFloorMg = 0.1;
constexpr double kMadToSd = 1.4826;  // MAD -> SD untuk noise Gaussian

// Pivot di bawah ini (relatif terhadap elemen matriks terbesar) = singular
constexpr double kSingularRelative = 1e-12;
constexpr size_t kMaxColumns = 3;  // 1, exp, dan (opsional) drift

constexpr size_t kMaxSlopes = kMaxTheilSenPoints * (kMaxTheilSenPoints - 1) / 2;
constexpr size_t kMaxOlsPoints = (kMaxLevelPoints > kMaxFitPoints) ? kMaxLevelPoints : kMaxFitPoints;
static_assert(kMaxOlsPoints <= 256, "indeks titik disimpan sebagai uint8_t");

// Median; urutan isi `values` berubah. n harus > 0.
double medianInPlace(double* values, size_t n) {
    std::sort(values, values + n);
    const size_t mid = n / 2;
    return (n % 2 != 0) ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

// ols_line atas sebagian titik (indeks naik), urutan penjumlahan sama
// dengan Python supaya hasilnya identik.
Line olsIndexed(const double* xs, const double* ys, const uint8_t* index, size_t n) {
    Line line = {false, NAN, NAN};
    if (n < 2) {
        return line;
    }
    double sumX = 0.0;
    double sumY = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sumX += xs[index[i]];
        sumY += ys[index[i]];
    }
    const double meanX = sumX / static_cast<double>(n);
    const double meanY = sumY / static_cast<double>(n);
    double sxx = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double dx = xs[index[i]] - meanX;
        sxx += dx * dx;
    }
    if (sxx == 0.0) {
        return line;
    }
    double sxy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sxy += (xs[index[i]] - meanX) * (ys[index[i]] - meanY);
    }
    line.ok = true;
    line.slope = sxy / sxx;
    line.intercept = meanY - line.slope * meanX;
    return line;
}

// Eliminasi Gauss dengan pivot parsial (drying._solve). false kalau
// (hampir) singular.
bool solve(const double matrix[kMaxColumns][kMaxColumns], const double rhs[kMaxColumns], size_t n,
           double solution[kMaxColumns]) {
    double rows[kMaxColumns][kMaxColumns + 1];
    double scale = 0.0;
    for (size_t r = 0; r < n; ++r) {
        for (size_t c = 0; c < n; ++c) {
            rows[r][c] = matrix[r][c];
            scale = std::max(scale, std::fabs(matrix[r][c]));
        }
        rows[r][n] = rhs[r];
    }
    if (scale == 0.0) {
        scale = 1.0;
    }

    for (size_t col = 0; col < n; ++col) {
        size_t pivot = col;
        for (size_t r = col + 1; r < n; ++r) {
            if (std::fabs(rows[r][col]) > std::fabs(rows[pivot][col])) {
                pivot = r;
            }
        }
        if (std::fabs(rows[pivot][col]) < scale * kSingularRelative) {
            return false;
        }
        if (pivot != col) {
            for (size_t c = 0; c <= n; ++c) {
                std::swap(rows[col][c], rows[pivot][c]);
            }
        }
        for (size_t r = col + 1; r < n; ++r) {
            const double factor = rows[r][col] / rows[col][col];
            for (size_t c = col; c <= n; ++c) {
                rows[r][c] -= factor * rows[col][c];
            }
        }
    }

    for (size_t k = n; k-- > 0;) {
        double known = 0.0;
        for (size_t c = k + 1; c < n; ++c) {
            known += rows[k][c] * solution[c];
        }
        solution[k] = (rows[k][n] - known) / rows[k][k];
    }
    return true;
}

// SSE model untuk p = tau / durasi tetap (drying._sse_for_p). Model
// linear dalam koefisiennya. Waktu sudah dinormalkan ke s = (t - t0) /
// durasi dalam [0, 1] supaya matriks normal berkondisi baik.
double sseForP(const double* ss, const double* ys, size_t n, double p, bool withDrift,
               double coef[kMaxColumns]) {
    const size_t k = withDrift ? 3 : 2;
    double decay[kMaxFitPoints];
    for (size_t r = 0; r < n; ++r) {
        decay[r] = std::exp(-ss[r] / p);
    }
    // Kolom 0 = 1, kolom 1 = exp(-s / p), kolom 2 = s
    auto column = [&](size_t i, size_t r) -> double {
        return (i == 0) ? 1.0 : (i == 1) ? decay[r] : ss[r];
    };

    double ata[kMaxColumns][kMaxColumns];
    double aty[kMaxColumns];
    for (size_t i = 0; i < k; ++i) {
        for (size_t j = 0; j < k; ++j) {
            double sum = 0.0;
            for (size_t r = 0; r < n; ++r) {
                sum += column(i, r) * column(j, r);
            }
            ata[i][j] = sum;
        }
        double sum = 0.0;
        for (size_t r = 0; r < n; ++r) {
            sum += column(i, r) * ys[r];
        }
        aty[i] = sum;
    }
    if (!solve(ata, aty, k, coef)) {
        return INFINITY;
    }

    double sse = 0.0;
    for (size_t r = 0; r < n; ++r) {
        double predicted = 0.0;
        for (size_t i = 0; i < k; ++i) {
            predicted += coef[i] * column(i, r);
        }
        const double residual = ys[r] - predicted;
        sse += residual * residual;
    }
    return sse;
}

}  // namespace

// ---------------------------------------------------------------------
//  Garis
// ---------------------------------------------------------------------
Line olsLine(const double* xs, const double* ys, size_t n) {
    if (n > kMaxOlsPoints) {
        return Line{false, NAN, NAN};
    }
    uint8_t index[kMaxOlsPoints];
    for (size_t i = 0; i < n; ++i) {
        index[i] = static_cast<uint8_t>(i);
    }
    return olsIndexed(xs, ys, index, n);
}

Level robustLevel(const double* ts, const double* ys, size_t n, double tRef) {
    Level out = {false, NAN, NAN, NAN, 0};
    if (n == 0 || n > kMaxLevelPoints) {
        return out;
    }

    uint8_t candidate[kMaxLevelPoints];
    for (size_t i = 0; i < n; ++i) {
        candidate[i] = static_cast<uint8_t>(i);
    }
    Line line = olsIndexed(ts, ys, candidate, n);
    if (!line.ok) {
        return out;
    }

    double residuals[kMaxLevelPoints];
    double work[kMaxLevelPoints];
    size_t used = n;
    double scale = NAN;
    for (int pass = 0; pass < kRobustPasses; ++pass) {
        // Residual dihitung terhadap SEMUA titik, termasuk yang dibuang di
        // putaran sebelumnya: titik yang ternyata wajar bisa masuk lagi.
        for (size_t i = 0; i < n; ++i) {
            residuals[i] = ys[i] - (line.intercept + line.slope * ts[i]);
            work[i] = residuals[i];
        }
        const double center = medianInPlace(work, n);
        for (size_t i = 0; i < n; ++i) {
            work[i] = std::fabs(residuals[i] - center);
        }
        scale = std::max(kRobustScaleFloorMg, kMadToSd * medianInPlace(work, n));

        size_t kept = 0;
        for (size_t i = 0; i < n; ++i) {
            if (std::fabs(residuals[i] - center) <= kRobustRejectK * scale) {
                candidate[kept++] = static_cast<uint8_t>(i);
            }
        }
        const Line refit = olsIndexed(ts, ys, candidate, kept);
        if (kept < n / 2 || !refit.ok) {
            break;  // terlalu banyak yang dibuang: pertahankan garis sebelumnya
        }
        used = kept;
        line = refit;
    }

    out.ok = true;
    out.level = line.intercept + line.slope * tRef;
    out.slope = line.slope;
    out.scale = scale;
    out.used = used;
    return out;
}

// ---------------------------------------------------------------------
//  Laju
// ---------------------------------------------------------------------
Slope theilSenSlope(const double* xs, const double* ys, size_t n) {
    Slope out = {false, NAN};
    if (n > kMaxTheilSenPoints) {
        return out;
    }
    double slopes[kMaxSlopes];
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const double dx = xs[j] - xs[i];
            if (dx != 0.0) {
                slopes[count++] = (ys[j] - ys[i]) / dx;
            }
        }
    }
    if (count == 0) {
        return out;
    }
    out.ok = true;
    out.slope = medianInPlace(slopes, count);
    return out;
}

// ---------------------------------------------------------------------
//  Fit eksponensial
// ---------------------------------------------------------------------
ExpFit fitExponential(const double* ts, const double* ms, size_t n, bool withDrift,
                      const FitOptions& options) {
    ExpFit fit = {false, withDrift, NAN, NAN, NAN, NAN, NAN, NAN, NAN, n, NAN, NAN, false};
    const size_t gridSize = options.gridSize;
    if (n < (withDrift ? 5u : 4u) || n > kMaxFitPoints || gridSize < 3 || gridSize > kMaxGridSize) {
        return fit;
    }
    const double t0 = ts[0];
    const double duration = ts[n - 1] - t0;
    if (duration <= 0.0) {
        return fit;
    }
    double ss[kMaxFitPoints];
    for (size_t r = 0; r < n; ++r) {
        ss[r] = (ts[r] - t0) / duration;
    }

    const double upper = std::min(options.tauMaxMin, options.tauMaxPerDuration * duration);
    const double lo = std::log(options.tauMinMin / duration);
    const double hi = std::log(upper / duration);
    if (hi <= lo) {
        return fit;
    }

    double coef[kMaxColumns];
    double grid[kMaxGridSize];
    double sses[kMaxGridSize];
    size_t best = 0;
    for (size_t i = 0; i < gridSize; ++i) {
        grid[i] = lo + (hi - lo) * static_cast<double>(i) / static_cast<double>(gridSize - 1);
        sses[i] = sseForP(ss, ms, n, std::exp(grid[i]), withDrift, coef);
        if (sses[i] < sses[best]) {
            best = i;  // minimum PERTAMA, sama dengan min() Python
        }
    }
    if (std::isinf(sses[best])) {
        return fit;
    }

    // Golden-section di antara tetangga grid terbaik, dalam ln(p)
    double a = grid[best > 0 ? best - 1 : 0];
    double b = grid[std::min(gridSize - 1, best + 1)];
    const double ratio = (std::sqrt(5.0) - 1.0) / 2.0;
    double c = b - ratio * (b - a);
    double d = a + ratio * (b - a);
    double fc = sseForP(ss, ms, n, std::exp(c), withDrift, coef);
    double fd = sseForP(ss, ms, n, std::exp(d), withDrift, coef);
    for (int i = 0; i < options.goldenIterations; ++i) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - ratio * (b - a);
            fc = sseForP(ss, ms, n, std::exp(c), withDrift, coef);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + ratio * (b - a);
            fd = sseForP(ss, ms, n, std::exp(d), withDrift, coef);
        }
    }

    const double p = std::exp((a + b) / 2.0);
    const double sse = sseForP(ss, ms, n, p, withDrift, coef);
    if (std::isinf(sse)) {
        return fit;
    }

    double sum = 0.0;
    for (size_t r = 0; r < n; ++r) {
        sum += ms[r];
    }
    const double mean = sum / static_cast<double>(n);
    double sst = 0.0;
    for (size_t r = 0; r < n; ++r) {
        sst += (ms[r] - mean) * (ms[r] - mean);
    }

    fit.ok = true;
    fit.t0 = t0;
    fit.mInfMg = coef[0];
    fit.amplitudeMg = coef[1];
    fit.tauMin = p * duration;
    fit.driftMgPerMin = withDrift ? coef[2] / duration : 0.0;
    fit.rmsMg = std::sqrt(sse / static_cast<double>(n));
    fit.r2 = (sst > 0.0) ? 1.0 - sse / sst : 1.0;
    fit.tFirst = ts[0];
    fit.tLast = ts[n - 1];
    fit.tauAtBound = (best == 0) || (best == gridSize - 1);
    return fit;
}

double modelMass(const ExpFit& fit, double t, bool includeDrift) {
    const double s = t - fit.t0;
    double mass = fit.mInfMg + fit.amplitudeMg * std::exp(-s / fit.tauMin);
    if (includeDrift) {
        mass += fit.driftMgPerMin * s;
    }
    return mass;
}

double modelRate(const ExpFit& fit, double t) {
    const double s = t - fit.t0;
    return fit.amplitudeMg / fit.tauMin * std::exp(-s / fit.tauMin);
}

StopPrediction predictStop(const ExpFit& fit, double targetRateMgPerMin) {
    StopPrediction out = {false, NAN, NAN, NAN};
    if (!fit.ok || fit.amplitudeMg <= 0.0 || fit.tauMin <= 0.0 || targetRateMgPerMin <= 0.0) {
        return out;
    }
    const double initialRate = fit.amplitudeMg / fit.tauMin;
    const double tStop = (initialRate <= targetRateMgPerMin)
                             ? fit.t0
                             : fit.t0 + fit.tauMin * std::log(initialRate / targetRateMgPerMin);
    const double remaining = fit.amplitudeMg * std::exp(-(tStop - fit.t0) / fit.tauMin);
    out.ok = true;
    out.tStopMin = tStop;
    out.massAtStopMg = fit.mInfMg + remaining;
    out.remainingMg = remaining;
    return out;
}

// ---------------------------------------------------------------------
//  Cross-check: garis ln(laju)
// ---------------------------------------------------------------------
LogRateStop logRateStop(const double* ts, const double* rates, size_t n,
                        double targetRateMgPerMin) {
    LogRateStop out = {false, NAN, NAN, 0};
    if (n > kMaxFitPoints || targetRateMgPerMin <= 0.0) {
        return out;
    }
    double xs[kMaxFitPoints];
    double ys[kMaxFitPoints];
    size_t used = 0;
    for (size_t i = 0; i < n; ++i) {
        if (rates[i] > 0.0 && std::isfinite(rates[i])) {
            xs[used] = ts[i];
            ys[used] = std::log(rates[i]);
            ++used;
        }
    }
    out.used = used;
    if (used < 3) {
        return out;
    }
    const Line line = olsLine(xs, ys, used);
    if (!line.ok || line.slope >= 0.0) {
        return out;  // laju tidak menurun: tidak ada waktu berhenti yang berarti
    }
    out.ok = true;
    out.tauMin = -1.0 / line.slope;
    out.tStopMin = (std::log(targetRateMgPerMin) - line.intercept) / line.slope;
    return out;
}

}  // namespace drying
