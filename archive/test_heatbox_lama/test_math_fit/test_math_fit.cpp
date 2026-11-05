// =====================================================================
//  test_math_fit — fitting kurva 3 suku (variable projection)
//
//  Ini kode paling rumit di seluruh project: golden-section search atas
//  tau, dengan penyelesaian 3x3 di setiap langkah.
//
//  TEKNIKNYA: bangkitkan kurva dari parameter yang SUDAH KITA TENTUKAN,
//  lalu cek apakah fitting bisa menemukannya kembali. Karena kita tahu
//  jawaban benarnya, kita bisa mengukur akurasinya — bukan cuma
//  memastikan "tidak crash".
//
//  Model:  W(t) = W_inf + A * exp(-t/tau) + k * t
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

#if !USE_CURVE_FIT
#  error "Suite ini butuh USE_CURVE_FIT=1. Lihat test/README.md."
#endif

using heatbox::detail::fitCurve;

static int32_t g_curve[1200];

void setUp(void)    { hbInit(); }
void tearDown(void) {}


// --- Kurva bersih tanpa noise: fitting harus nyaris sempurna ----------
void test_kurva_bersih_parameter_ditemukan(void) {
    const int   N    = 900;          // 15 menit @ 1 Hz
    const float Winf = 400000.0f;    // ~19 g
    const float A    = 21000.0f;     // ~1 g air
    const float tau  = 300.0f;       // 5 menit
    const float k    = -0.5f;        // dekomposisi + creep

    hbtest::makeDryingCurve(g_curve, N, Winf, A, tau, k, 0);
    HbFit f = fitCurve(g_curve, N, 1.0f);

    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(  10.0f,  tau,  f.tau_s);
    TEST_ASSERT_FLOAT_WITHIN( 600.0f,  A,    f.A);
    TEST_ASSERT_FLOAT_WITHIN(   0.05f, k,    f.k_cps);
    TEST_ASSERT_FLOAT_WITHIN(1000.0f,  Winf, f.W_inf);
}

// --- Dengan noise realistis (+-100 count ~ +-5 mg) --------------------
//
//  Toleransinya lebih longgar, dan memang seharusnya begitu. Test yang
//  menuntut presisi sempurna di data ber-noise itu test yang salah.
void test_kurva_ber_noise(void) {
    const int   N    = 900;
    const float tau  = 300.0f;
    const float A    = 21000.0f;
    const float k    = -0.5f;

    hbtest::makeDryingCurve(g_curve, N, 400000.0f, A, tau, k, 100);
    HbFit f = fitCurve(g_curve, N, 1.0f);

    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(  30.0f, tau, f.tau_s);
    TEST_ASSERT_FLOAT_WITHIN(1500.0f, A,   f.A);
    TEST_ASSERT_FLOAT_WITHIN(   0.2f, k,   f.k_cps);
}

// --- Pengeringan cepat (tau kecil) ------------------------------------
void test_tau_kecil(void) {
    const float tau = 60.0f;
    hbtest::makeDryingCurve(g_curve, 600, 400000.0f, 21000.0f, tau, -0.5f, 50);
    HbFit f = fitCurve(g_curve, 600, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(10.0f, tau, f.tau_s);
}

// --- Pengeringan lambat (tau besar, mendekati batas atas pencarian) ---
void test_tau_besar(void) {
    const float tau = 900.0f;
    hbtest::makeDryingCurve(g_curve, 1200, 400000.0f, 21000.0f, tau, -0.5f, 50);
    HbFit f = fitCurve(g_curve, 1200, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(150.0f, tau, f.tau_s);   // tau besar memang
                                                      // lebih sulit dipisahkan
                                                      // dari suku linier
}

// --- Suku linier harus terpisah dari suku eksponensial ----------------
//
//  INI TEST TERPENTING DI SUITE INI.
//
//  Inti seluruh model 3 suku: memisahkan "air yang keluar" (eksponensial)
//  dari "bahan yang terurai + load cell yang creep" (linier). Kalau
//  keduanya tertukar, hasil kadar airnya salah dan tidak ada yang sadar.
//
//  Di sini k dibuat besar (-3 count/detik) supaya kalau fitting menelan
//  suku linier ke dalam eksponensial, tau-nya akan meleset jauh.
void test_suku_linier_terpisah_dari_eksponensial(void) {
    const float tau = 300.0f;
    const float k   = -3.0f;
    hbtest::makeDryingCurve(g_curve, 900, 400000.0f, 21000.0f, tau, k, 50);
    HbFit f = fitCurve(g_curve, 900, 1.0f);

    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(40.0f, tau, f.tau_s);
    TEST_ASSERT_FLOAT_WITHIN( 0.4f, k,   f.k_cps);
}

// --- Tanpa suku linier sama sekali (k = 0) ----------------------------
//
//  Kasus batas: fitting tidak boleh "mengarang" kemiringan linier
//  padahal tidak ada.
void test_tanpa_suku_linier(void) {
    hbtest::makeDryingCurve(g_curve, 900, 400000.0f, 21000.0f, 300.0f, 0.0f, 50);
    HbFit f = fitCurve(g_curve, 900, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 0.0f, f.k_cps);
}

// --- Sisa air yang belum keluar ---------------------------------------
//
//  resid_mg = |A * exp(-t_sekarang/tau)| / COUNTS_PER_MG
//  Untuk N=900, tau=300: exp(-3) = 0.0498
//  -> 21000 * 0.0498 = 1046 count = ~50 mg
void test_sisa_air_dihitung_benar(void) {
    hbtest::makeDryingCurve(g_curve, 900, 400000.0f, 21000.0f, 300.0f, -0.5f, 0);
    HbFit f = fitCurve(g_curve, 900, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    const float harusnya = 21000.0f * expf(-3.0f) / COUNTS_PER_MG;
    TEST_ASSERT_FLOAT_WITHIN(15.0f, harusnya, f.resid_mg);
}

// --- Perkiraan waktu berhenti masuk akal ------------------------------
//
//  t_stop = tau * ln(A/ambang). Harus lebih besar dari waktu sekarang
//  selama sisa airnya masih di atas ambang.
void test_perkiraan_waktu_berhenti(void) {
    hbtest::makeDryingCurve(g_curve, 600, 400000.0f, 21000.0f, 300.0f, -0.5f, 0);
    HbFit f = fitCurve(g_curve, 600, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_TRUE(f.t_stop_s > 0.0f);
    TEST_ASSERT_TRUE(f.t_stop_s < 3600.0f);
}

// --- Kualitas fitting: RMSE harus kecil untuk data bersih -------------
void test_rmse_kecil_untuk_data_bersih(void) {
    hbtest::makeDryingCurve(g_curve, 900, 400000.0f, 21000.0f, 300.0f, -0.5f, 0);
    HbFit f = fitCurve(g_curve, 900, 1.0f);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_TRUE(f.rmse_mg < 1.0f);
}

// --- Data terlalu sedikit harus ditolak, bukan crash ------------------
void test_data_terlalu_sedikit(void) {
    const int32_t y[] = {1000000, 999000};
    HbFit f = fitCurve(y, 2, 1.0f);
    TEST_ASSERT_FALSE(f.valid);
}

// --- Data datar sempurna tidak boleh membuat fitting meledak ----------
//
//  Kasus patologis: tidak ada informasi untuk menentukan tau. Fitting
//  boleh memberi jawaban apa saja, tapi TIDAK BOLEH menghasilkan NaN
//  atau infinity yang kemudian menyebar ke seluruh perhitungan.
void test_data_datar_tidak_menghasilkan_nan(void) {
    for (int i = 0; i < 900; i++) g_curve[i] = 400000;
    HbFit f = fitCurve(g_curve, 900, 1.0f);
    if (f.valid) {
        TEST_ASSERT_FALSE(isnan(f.tau_s));
        TEST_ASSERT_FALSE(isnan(f.W_inf));
        TEST_ASSERT_FALSE(isnan(f.A));
        TEST_ASSERT_FALSE(isnan(f.k_cps));
        TEST_ASSERT_FALSE(isinf(f.tau_s));
    }
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_kurva_bersih_parameter_ditemukan);
    RUN_TEST(test_kurva_ber_noise);
    RUN_TEST(test_tau_kecil);
    RUN_TEST(test_tau_besar);
    RUN_TEST(test_suku_linier_terpisah_dari_eksponensial);
    RUN_TEST(test_tanpa_suku_linier);
    RUN_TEST(test_sisa_air_dihitung_benar);
    RUN_TEST(test_perkiraan_waktu_berhenti);
    RUN_TEST(test_rmse_kecil_untuk_data_bersih);
    RUN_TEST(test_data_terlalu_sedikit);
    RUN_TEST(test_data_datar_tidak_menghasilkan_nan);
    return UNITY_END();
}
