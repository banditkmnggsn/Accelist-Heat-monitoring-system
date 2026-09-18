// =====================================================================
//  test_math_slope — regresi linier jendela bergerak
//
//  Fungsi ini fondasi seluruh deteksi titik berhenti. Kalau dia salah,
//  mesin berhenti di waktu yang keliru dan semua hasil ikut salah.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

using heatbox::detail::slopeCountsPerSec;

void setUp(void)    { hbInit(); }
void tearDown(void) {}


// --- Kasus dasar: garis lurus naik ------------------------------------
void test_garis_lurus_naik(void) {
    const int32_t y[] = {100, 110, 120, 130, 140};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, slopeCountsPerSec(y, 5, 1.0f));
}

// --- Garis lurus TURUN ------------------------------------------------
//
//  Ini arah yang sebenarnya terjadi di mesin: pengeringan = berat berkurang.
//  Menguji arah positif saja berarti menguji kasus yang tidak pernah ada.
void test_garis_lurus_turun(void) {
    const int32_t y[] = {140, 130, 120, 110, 100};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, slopeCountsPerSec(y, 5, 1.0f));
}

// --- Data datar harus memberi kemiringan tepat nol --------------------
//
//  Ini juga menguji secara tidak langsung bahwa sum(u_i) = 0 benar-benar
//  eksak. Kalau pemusatan sumbu-x salah, nilai offset akan bocor ke sini.
void test_data_datar_kemiringan_nol(void) {
    int32_t y[120];
    for (int i = 0; i < 120; i++) y[i] = 1500000;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, slopeCountsPerSec(y, 120, 1.0f));
}

// --- PENJAGA DESAIN: presisi di count besar ---------------------------
//
//  Count asli itu jutaan, bukan ratusan. Kalau suatu hari ada yang
//  mengganti akumulator int64 jadi float "biar ringan", test ini yang
//  akan menangkapnya. Test bukan cuma membuktikan kode benar hari ini —
//  dia menjaga keputusan desain supaya tidak hilang saat refactor.
void test_presisi_di_count_besar(void) {
    int32_t y[120];
    hbtest::makeRamp(y, 120, 2000000, 21);          // 21 count/detik = 1 mg/detik
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 21.0f, slopeCountsPerSec(y, 120, 1.0f));
}

// --- Kemiringan sangat kecil, di ambang deteksi -----------------------
//
//  2 mg/menit = 0.7 count/detik. Inilah beda antara "berhenti" dan
//  "lanjut", jadi harus terbaca dengan benar.
void test_kemiringan_kecil_di_ambang(void) {
    int32_t y[120];
    for (int i = 0; i < 120; i++)
        y[i] = 1000000 - (int32_t)(0.7f * (float)i);
    const float s = slopeCountsPerSec(y, 120, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -0.7f, s);
}

// --- Noise tidak boleh menggeser kemiringan ---------------------------
//
//  Inilah inti dari averaging: satu pembacaan boleh meleset jauh, tapi
//  kemiringan dari 120 titik tetap akurat.
void test_kemiringan_tahan_noise(void) {
    int32_t y[120];
    for (int i = 0; i < 120; i++)
        y[i] = 1000000 - (int32_t)(21.0f * (float)i) + hbtest::noise(i, 100);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -21.0f, slopeCountsPerSec(y, 120, 1.0f));
}

// --- dt selain 1 detik (dipakai ANALYSIS_MODE 1, blok per menit) ------
void test_dt_blok_60_detik(void) {
    // Naik 600 count per blok, blok = 60 detik  ->  10 count/detik
    const int32_t y[] = {1000000, 1000600, 1001200, 1001800, 1002400};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, slopeCountsPerSec(y, 5, 60.0f));
}

// --- Penjaga: titik terlalu sedikit -----------------------------------
void test_titik_kurang_dari_tiga(void) {
    const int32_t y[] = {100, 110};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, slopeCountsPerSec(y, 2, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, slopeCountsPerSec(y, 0, 1.0f));
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_garis_lurus_naik);
    RUN_TEST(test_garis_lurus_turun);
    RUN_TEST(test_data_datar_kemiringan_nol);
    RUN_TEST(test_presisi_di_count_besar);
    RUN_TEST(test_kemiringan_kecil_di_ambang);
    RUN_TEST(test_kemiringan_tahan_noise);
    RUN_TEST(test_dt_blok_60_detik);
    RUN_TEST(test_titik_kurang_dari_tiga);
    return UNITY_END();
}
