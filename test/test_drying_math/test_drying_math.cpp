// =====================================================================
//  test_drying_math — port C++ lib/HeatboxCycle vs tuning_web/drying.py
//
//  Jawaban di golden_cases.h DIHITUNG oleh drying.py (gen_golden.py),
//  implementasi yang sudah teruji di siklus PC. Kalau uji ini lolos,
//  firmware menghasilkan angka yang sama dengan Python untuk nilai per
//  menit, laju, fit kurva, dan prediksi waktu stop.
//
//  Jalan di PC (env native) dan di ESP32 (pio test di board).
// =====================================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <DryingMath.h>
#include <unity.h>

#include <cmath>
#include <cstdio>

#include "golden_cases.h"

namespace {

// Logika dan urutan operasi identik dengan Python, jadi selisih hanya
// berasal dari libm (exp/log) yang berbeda per platform.
constexpr double kExactRelTol = 1e-9;  // garis & median: aritmetika dasar saja
constexpr double kFitRelTol = 1e-6;    // fit: exp/log + iterasi golden-section

// Toleransi relatif terhadap max(1, |expected|): nilai kecil dibandingkan
// secara absolut.
void assertClose(double expected, double actual, double relTol, const char* caseName,
                 const char* field) {
    const double tolerance = relTol * std::fmax(1.0, std::fabs(expected));
    if (std::fabs(actual - expected) <= tolerance) {
        return;
    }
    char message[192];
    snprintf(message, sizeof(message), "%s.%s: diharapkan %.17g, dapat %.17g", caseName, field,
             expected, actual);
    TEST_FAIL_MESSAGE(message);
}

void assertEqualFlag(bool expected, bool actual, const char* caseName, const char* field) {
    if (expected == actual) {
        return;
    }
    char message[128];
    snprintf(message, sizeof(message), "%s.%s: diharapkan %s", caseName, field,
             expected ? "true" : "false");
    TEST_FAIL_MESSAGE(message);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// =====================================================================
//  Cocok angka per angka dengan Python
// =====================================================================
void test_robust_level_cocok_python(void) {
    for (const golden::LevelCase& c : golden::kLevelCases) {
        const drying::Level got = drying::robustLevel(c.ts, c.ys, c.n, c.tRef);
        assertEqualFlag(true, got.ok, c.name, "ok");
        assertClose(c.level, got.level, kExactRelTol, c.name, "level");
        assertClose(c.slope, got.slope, kExactRelTol, c.name, "slope");
        assertClose(c.scale, got.scale, kExactRelTol, c.name, "scale");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(c.used, got.used, c.name);
    }
}

void test_theil_sen_cocok_python(void) {
    for (const golden::SlopeCase& c : golden::kSlopeCases) {
        const drying::Slope got = drying::theilSenSlope(c.xs, c.ys, c.n);
        assertEqualFlag(c.ok, got.ok, c.name, "ok");
        if (c.ok) {
            assertClose(c.slope, got.slope, kExactRelTol, c.name, "slope");
        }
    }
}

void test_fit_dan_prediksi_cocok_python(void) {
    for (const golden::FitCase& c : golden::kFitCases) {
        const drying::ExpFit fit = drying::fitExponential(c.ts, c.ms, c.n, c.withDrift);
        assertEqualFlag(c.ok, fit.ok, c.name, "ok");
        if (c.ok) {
            assertClose(c.t0, fit.t0, kExactRelTol, c.name, "t0");
            assertClose(c.mInf, fit.mInfMg, kFitRelTol, c.name, "mInf");
            assertClose(c.amplitude, fit.amplitudeMg, kFitRelTol, c.name, "amplitude");
            assertClose(c.tau, fit.tauMin, kFitRelTol, c.name, "tau");
            assertClose(c.drift, fit.driftMgPerMin, kFitRelTol, c.name, "drift");
            assertClose(c.rms, fit.rmsMg, kFitRelTol, c.name, "rms");
            assertClose(c.r2, fit.r2, kFitRelTol, c.name, "r2");
            assertEqualFlag(c.tauAtBound, fit.tauAtBound, c.name, "tauAtBound");
        }
        for (const golden::StopCase& s : c.stops) {
            const drying::StopPrediction stop = drying::predictStop(fit, s.target);
            assertEqualFlag(s.ok, stop.ok, c.name, "stop.ok");
            if (s.ok) {
                assertClose(s.tStop, stop.tStopMin, kFitRelTol, c.name, "stop.tStop");
                assertClose(s.massAtStop, stop.massAtStopMg, kFitRelTol, c.name, "stop.massAtStop");
                assertClose(s.remaining, stop.remainingMg, kFitRelTol, c.name, "stop.remaining");
            }
        }
    }
}

// =====================================================================
//  Sifat yang tidak bergantung pada Python
// =====================================================================

// Tisu menekan wadah 10 detik di tengah menit yang massanya turun 40
// mg/menit. Garis robust harus membuang tepat 10 detik itu dan membaca
// nilai sebenarnya di tengah menit, tanpa bias.
void test_robust_level_tisu_tanpa_bias(void) {
    double ts[60];
    double ys[60];
    for (int i = 0; i < 60; ++i) {
        ts[i] = 5.0 + (i + 0.5) / 60.0;
        ys[i] = 5000.0 - 40.0 * ts[i] + ((i >= 20 && i < 30) ? 50.0 : 0.0);
    }
    const drying::Level got = drying::robustLevel(ts, ys, 60, 5.5);
    TEST_ASSERT_TRUE(got.ok);
    TEST_ASSERT_EQUAL_UINT(50, got.used);
    TEST_ASSERT_TRUE(std::fabs(got.level - (5000.0 - 40.0 * 5.5)) < 1e-9);
}

void test_robust_level_tolak_masukan_tidak_sah(void) {
    const double same[] = {1.0, 1.0, 1.0};
    const double ys[] = {5.0, 6.0, 7.0};
    TEST_ASSERT_FALSE(drying::robustLevel(same, ys, 3, 1.0).ok);  // x tidak bervariasi

    double many[drying::kMaxLevelPoints + 1];
    for (size_t i = 0; i <= drying::kMaxLevelPoints; ++i) {
        many[i] = static_cast<double>(i);
    }
    TEST_ASSERT_FALSE(drying::robustLevel(many, many, drying::kMaxLevelPoints + 1, 0.0).ok);
}

// Laju dari eksponensial eksak: garis ln(laju) harus mengembalikan tau
// dan waktu stop analitik.
void test_log_rate_stop_eksponensial_eksak(void) {
    const double amplitude = 600.0;
    const double tau = 10.0;
    const double t0 = 1.0;
    double ts[6];
    double rates[6];
    for (int i = 0; i < 6; ++i) {
        ts[i] = 10.5 + i;
        rates[i] = amplitude / tau * std::exp(-(ts[i] - t0) / tau);
    }
    const drying::LogRateStop got = drying::logRateStop(ts, rates, 6, 1.0);
    TEST_ASSERT_TRUE(got.ok);
    TEST_ASSERT_EQUAL_UINT(6, got.used);
    TEST_ASSERT_TRUE(std::fabs(got.tauMin - tau) < 1e-9);
    const double expected = t0 + tau * std::log(amplitude / (tau * 1.0));
    TEST_ASSERT_TRUE(std::fabs(got.tStopMin - expected) < 1e-9);
}

// Laju <= 0 tidak bisa di-log dan harus dilewati, bukan menggagalkan.
void test_log_rate_stop_lewati_laju_nonpositif(void) {
    const double ts[] = {10.5, 11.5, 12.5, 13.5, 14.5};
    const double rates[] = {8.0, 0.0, 5.0, -1.0, 3.0};
    const drying::LogRateStop got = drying::logRateStop(ts, rates, 5, 1.0);
    TEST_ASSERT_TRUE(got.ok);
    TEST_ASSERT_EQUAL_UINT(3, got.used);
}

// Laju yang naik tidak punya waktu berhenti.
void test_log_rate_stop_laju_naik_ditolak(void) {
    const double ts[] = {10.5, 11.5, 12.5, 13.5};
    const double rates[] = {3.0, 4.0, 5.0, 6.0};
    TEST_ASSERT_FALSE(drying::logRateStop(ts, rates, 4, 1.0).ok);
}

// Dua metode prediksi harus sepakat pada kurva eksponensial bersih:
// inilah dasar kriteria V7 (cross-check M1 vs A1).
void test_fit_dan_log_rate_sepakat(void) {
    const double mInf = 4400.0;
    const double amplitude = 600.0;
    const double tau = 8.0;
    double ts[10];
    double ms[10];
    double rates[10];
    for (int i = 0; i < 10; ++i) {
        ts[i] = 8.5 + i;
        ms[i] = mInf + amplitude * std::exp(-ts[i] / tau);
        rates[i] = amplitude / tau * std::exp(-ts[i] / tau);
    }
    const drying::ExpFit fit = drying::fitExponential(ts, ms, 10);
    const drying::StopPrediction m1 = drying::predictStop(fit, 1.0);
    const drying::LogRateStop a1 = drying::logRateStop(ts, rates, 10, 1.0);
    TEST_ASSERT_TRUE(m1.ok);
    TEST_ASSERT_TRUE(a1.ok);
    TEST_ASSERT_TRUE(std::fabs(m1.tStopMin - a1.tStopMin) < 1e-3);
}

// modelRate harus turunan (negatif) dari modelMass tanpa drift.
void test_model_rate_turunan_model_mass(void) {
    const double ts[] = {3.5, 5.5, 7.5, 9.5, 11.5, 13.5};
    double ms[6];
    for (int i = 0; i < 6; ++i) {
        ms[i] = 4200.0 + 800.0 * std::exp(-ts[i] / 6.0);
    }
    const drying::ExpFit fit = drying::fitExponential(ts, ms, 6);
    TEST_ASSERT_TRUE(fit.ok);
    const double t = 9.0;
    const double h = 1e-3;
    const double numeric = (drying::modelMass(fit, t - h) - drying::modelMass(fit, t + h)) / (2.0 * h);
    TEST_ASSERT_TRUE(std::fabs(numeric - drying::modelRate(fit, t)) < 1e-4);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_robust_level_cocok_python);
    RUN_TEST(test_theil_sen_cocok_python);
    RUN_TEST(test_fit_dan_prediksi_cocok_python);
    RUN_TEST(test_robust_level_tisu_tanpa_bias);
    RUN_TEST(test_robust_level_tolak_masukan_tidak_sah);
    RUN_TEST(test_log_rate_stop_eksponensial_eksak);
    RUN_TEST(test_log_rate_stop_lewati_laju_nonpositif);
    RUN_TEST(test_log_rate_stop_laju_naik_ditolak);
    RUN_TEST(test_fit_dan_log_rate_sepakat);
    RUN_TEST(test_model_rate_turunan_model_mass);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);  // beri waktu monitor serial tersambung
    runAllTests();
}
void loop() {}
#else
int main() { return runAllTests(); }
#endif
