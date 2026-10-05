#pragma once

// =====================================================================
//  DryingMath.h — matematika kurva pengeringan untuk siklus di firmware
//
//  Port langsung dari tuning_web/drying.py, yang sudah teruji di PC.
//  Nama fungsi dan URUTAN OPERASI sengaja sama, supaya hasilnya bisa
//  dicocokkan angka per angka dengan vektor emas dari Python
//  (test/test_drying_math). Kalau rumus di sini diubah, ubah juga di
//  drying.py lalu bangkitkan ulang vektornya.
//
//  C++ murni: TIDAK meng-include Arduino.h, supaya bisa diuji di PC.
//
//  Satuan: waktu dalam MENIT, massa dalam MILIGRAM. "Laju" selalu laju
//  PENYUSUTAN: positif = massa berkurang.
//
//  Tanpa alokasi dinamis: buffer kerja ada di stack dengan kapasitas tetap
//  di bawah. Fungsi mengembalikan ok = false kalau n melebihinya.
// =====================================================================

#include <cstddef>

namespace drying {

// ---------------------------------------------------------------------
//  Kapasitas buffer kerja
// ---------------------------------------------------------------------
constexpr size_t kMaxLevelPoints = 64;     // robustLevel: 60 nilai per detik satu menit
constexpr size_t kMaxTheilSenPoints = 16;  // theilSenSlope: jendela laju 5 menit
constexpr size_t kMaxFitPoints = 64;       // fitExponential/logRateStop: titik per menit
constexpr size_t kMaxGridSize = 64;        // FitOptions::gridSize

struct Line {
    bool ok;           // false: kurang dari 2 titik atau x tidak bervariasi
    double slope;
    double intercept;  // nilai garis di x = 0
};

// Garis least squares (drying.ols_line).
Line olsLine(const double* xs, const double* ys, size_t n);

struct Level {
    bool ok;
    double level;  // nilai garis robust di tRef
    double slope;  // kemiringan garis robust
    double scale;  // sebaran robust setara SD (1,4826 x MAD residual)
    size_t used;   // banyak titik yang dipakai garis akhir
};

// Nilai di tRef dari garis lurus yang tahan gangguan (drying.robust_level).
//
// Garis LS -> buang titik yang menyimpang > 3 x sebaran robust -> garis
// dihitung ulang (2 putaran) -> dibaca di tRef. Dipakai untuk nilai per
// menit, W0 (ekstrapolasi balik), dan Wf. Garis, bukan rata-rata, karena
// massa sedang turun di dalam jendela: rata-rata menjadi bias kalau
// sebagian titik dibuang secara tidak simetris.
//
// Beda dengan Python: kalau x tidak bervariasi, Python jatuh ke IQM;
// di sini ok = false. Data per detik selalu punya waktu yang berbeda.
Level robustLevel(const double* ts, const double* ys, size_t n, double tRef);

struct Slope {
    bool ok;  // false: tidak ada pasangan dengan x berbeda, atau n terlalu besar
    double slope;
};

// Median semua kemiringan antar-pasangan titik (estimator Theil-Sen,
// drying.theil_sen_slope). Satu menit yang terganggu tidak bisa menyeret
// hasilnya; regresi least squares bisa sampai membalik tanda.
Slope theilSenSlope(const double* xs, const double* ys, size_t n);

// Parameter pencarian tau. Nilai bawaan SAMA dengan drying.fit_exponential;
// siklus boleh memakai nilai lain (mis. batas tau 0,3..30 menit).
struct FitOptions {
    size_t gridSize = 48;          // grid logaritmik sebelum golden-section
    int goldenIterations = 40;
    double tauMinMin = 0.2;
    double tauMaxMin = 600.0;
    double tauMaxPerDuration = 20.0;  // tau >> rentang data = lengkungan tidak teramati
};

struct ExpFit {
    bool ok;
    bool withDrift;
    double t0;             // waktu titik pertama
    double mInfMg;         // massa asimtot
    double amplitudeMg;    // A
    double tauMin;
    double driftMgPerMin;  // 0 kalau tanpa suku drift
    double rmsMg;
    double r2;
    size_t nPoints;
    double tFirst;
    double tLast;
    bool tauAtBound;  // minimum grid di ujung: tau tidak terdefinisi
};

// Fit m(t) = m_inf + A * exp(-(t - t0) / tau) [+ k * (t - t0)]
// (drying.fit_exponential).
//
// Untuk tau tetap model ini linear dalam koefisiennya, jadi koefisien
// diselesaikan least squares (2x2, atau 3x3 dengan drift). Tau dicari
// dengan grid logaritmik lalu golden-section di sekitar minimum; grid
// lebih dulu supaya tidak terjebak minimum lokal.
ExpFit fitExponential(const double* ts, const double* ms, size_t n, bool withDrift = false,
                      const FitOptions& options = FitOptions());

double modelMass(const ExpFit& fit, double t, bool includeDrift = true);

// Laju penyusutan dari komponen pengeringan saja (tanpa drift), mg/menit.
double modelRate(const ExpFit& fit, double t);

struct StopPrediction {
    bool ok;  // false: fit gagal, A <= 0, tau <= 0, atau target <= 0
    double tStopMin;
    double massAtStopMg;  // tanpa suku drift
    double remainingMg;   // sisa air menurut model = tau x target
};

// Kapan laju model turun ke targetRate (drying.predict_stop):
//   t_stop = t0 + tau * ln(A / (tau * target))
// Kalau laju awal model sudah <= target, t_stop = t0.
StopPrediction predictStop(const ExpFit& fit, double targetRateMgPerMin);

struct LogRateStop {
    bool ok;  // false: < 3 laju positif, atau laju tidak menurun
    double tauMin;
    double tStopMin;
    size_t used;  // banyak laju positif yang dipakai
};

// Cross-check sederhana untuk fitExponential (tidak ada padanannya di
// drying.py). Untuk model eksponensial, ln(laju) linear terhadap waktu:
//   ln r = ln(A / tau) - (t - t0) / tau
// Garis LS pada (t, ln r) memberi tau = -1 / kemiringan dan waktu saat
// laju = target secara tertutup. Laju <= 0 tidak bisa di-log dan dilewati.
// `ts` adalah waktu yang diwakili tiap laju (tengah jendela Theil-Sen).
LogRateStop logRateStop(const double* ts, const double* rates, size_t n,
                        double targetRateMgPerMin);

}  // namespace drying
