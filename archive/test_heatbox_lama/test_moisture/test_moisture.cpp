// =====================================================================
//  test_moisture — perhitungan kadar air + deteksi berat stabil
//
//  Dua fungsi ini yang menghasilkan angka akhir yang dilihat operator.
//  Semua kerja di lapisan bawah bermuara ke sini.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

void setUp(void)    { hbInit(); }
void tearDown(void) {}


// ---------------------------------------------------------------------
//  hbMoisturePct
// ---------------------------------------------------------------------

// --- Kasus dasar ------------------------------------------------------
void test_kadar_air_dasar(void) {
    // 1000 mg -> 250 mg, hilang 750 mg = 75%
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, hbMoisturePct(1000.0f, 250.0f));
}

// --- Nilai realistis tepung telur -------------------------------------
//
//  Sampel 20 g dengan kadar air ~4%: 20000 mg -> 19200 mg.
//  Ini rentang angka yang sebenarnya akan muncul di mesin.
void test_kadar_air_realistis_tepung_telur(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, hbMoisturePct(20000.0f, 19200.0f));
}

// --- Resolusi: beda 15 mg harus terlihat ------------------------------
//
//  Ini menerjemahkan seluruh error budget kita jadi satu assertion.
//  Geseran alat +-15 mg pada sampel 20 g = +-0.075% kadar air.
//  Kalau perhitungannya memakai tipe yang kurang presisi, beda sekecil
//  ini akan hilang.
void test_resolusi_15_mg_terlihat(void) {
    const float a = hbMoisturePct(20000.0f, 19200.0f);
    const float b = hbMoisturePct(20000.0f, 19185.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.075f, b - a);
}

// --- Tidak ada air yang hilang ----------------------------------------
void test_kadar_air_nol(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, hbMoisturePct(20000.0f, 20000.0f));
}

// --- Penjaga pembagian nol --------------------------------------------
//
//  Bisa terjadi kalau operator menekan Start tanpa memasukkan sampel.
//  Harus mengembalikan 0, bukan NaN atau infinity.
void test_penjaga_pembagi_nol(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, hbMoisturePct(0.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, hbMoisturePct(0.0f, 100.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, hbMoisturePct(-50.0f, 10.0f));
}

// --- Berat akhir lebih besar dari awal (hasil negatif) ----------------
//
//  Janggal tapi mungkin: sampel menyerap uap air saat pendinginan, atau
//  ada yang menyentuh pan. Fungsi tidak boleh menyembunyikannya —
//  angka negatif justru sinyal ke operator bahwa ada yang salah.
void test_hasil_negatif_tidak_disembunyikan(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, hbMoisturePct(1000.0f, 1050.0f));
}


// ---------------------------------------------------------------------
//  hbStableWeight
// ---------------------------------------------------------------------

// --- Data konstan harus dinyatakan stabil -----------------------------
void test_stabil_saat_data_konstan(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    hbtest::feedConstant(1000000 + hbtest::mgToCounts(500.0f), STABLE_N + 5);

    float mg = -1.0f;
    TEST_ASSERT_TRUE(hbStableWeight(&mg));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f, mg);
}

// --- Data yang masih bergerak harus DITOLAK ---------------------------
//
//  Ini yang mencegah mesin mencatat berat saat pan masih bergoyang atau
//  operator baru saja menutup pintu chamber.
void test_tidak_stabil_saat_data_bergerak(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    // Turun 500 count/detik = ~24 mg/detik -> standar deviasi jauh di
    // atas ambang STABLE_SD_MG
    hbtest::feedSlope(1000000, -500.0f, STABLE_N + 5, false);

    float mg = -1.0f;
    TEST_ASSERT_FALSE(hbStableWeight(&mg));
}

// --- Data belum cukup banyak harus ditolak ----------------------------
void test_tidak_stabil_saat_data_kurang(void) {
    hbtest::feedConstant(1000000, STABLE_N - 3);

    float mg = -1.0f;
    TEST_ASSERT_FALSE(hbStableWeight(&mg));
}

// --- Keluaran tidak disentuh kalau belum stabil -----------------------
//
//  Kontrak fungsi: kalau return false, *out_mg tidak boleh diubah.
//  Kalau dilanggar, pemanggil bisa memakai nilai setengah jadi.
void test_keluaran_tidak_disentuh_saat_gagal(void) {
    float mg = -12345.0f;
    TEST_ASSERT_FALSE(hbStableWeight(&mg));
    TEST_ASSERT_EQUAL_FLOAT(-12345.0f, mg);
}

// --- Noise kecil masih dianggap stabil --------------------------------
//
//  Ambangnya tidak boleh terlalu ketat, kalau tidak mesin akan menunggu
//  selamanya. Noise +-1 mg harus lolos.
void test_noise_kecil_masih_stabil(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    for (int s = 0; s < STABLE_N + 5; s++) {
        const int32_t v = 1000000 + hbtest::noise(s, (int32_t)COUNTS_PER_MG);
        for (int k = 0; k < ADC_SPS; k++) hbPushRaw(v);
        hbTick1Hz();
    }

    float mg = -1.0f;
    TEST_ASSERT_TRUE(hbStableWeight(&mg));
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 0.0f, mg);
}


// ---------------------------------------------------------------------
//  Integrasi: satu siklus penuh
// ---------------------------------------------------------------------

// --- Dari tare sampai angka kadar air ---------------------------------
//
//  Test terakhir ini menjahit semuanya: tare, timbang awal, keringkan,
//  timbang akhir, hitung. Kalau yang satu ini lulus, rantai lengkapnya
//  tersambung benar.
void test_siklus_penuh_menghasilkan_kadar_air(void) {
    // 1. Tare dengan pan kosong
    hbtest::feedConstant(1000000, 5);
    hbTare();

    // 2. Sampel 20 g masuk
    const int32_t awal = 1000000 + hbtest::mgToCounts(20000.0f);
    hbtest::feedConstant(awal, STABLE_N + 5);
    float w_awal = 0.0f;
    TEST_ASSERT_TRUE(hbStableWeight(&w_awal));
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 20000.0f, w_awal);

    // 3. Keringkan — kehilangan 800 mg (kadar air 4%)
    const int32_t akhir = awal - hbtest::mgToCounts(800.0f);
    hbtest::feedConstant(akhir, STABLE_N + 5);
    float w_akhir = 0.0f;
    TEST_ASSERT_TRUE(hbStableWeight(&w_akhir));

    // 4. Hasil akhir
    const float pct = hbMoisturePct(w_awal, w_akhir);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.0f, pct);
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_kadar_air_dasar);
    RUN_TEST(test_kadar_air_realistis_tepung_telur);
    RUN_TEST(test_resolusi_15_mg_terlihat);
    RUN_TEST(test_kadar_air_nol);
    RUN_TEST(test_penjaga_pembagi_nol);
    RUN_TEST(test_hasil_negatif_tidak_disembunyikan);
    RUN_TEST(test_stabil_saat_data_konstan);
    RUN_TEST(test_tidak_stabil_saat_data_bergerak);
    RUN_TEST(test_tidak_stabil_saat_data_kurang);
    RUN_TEST(test_keluaran_tidak_disentuh_saat_gagal);
    RUN_TEST(test_noise_kecil_masih_stabil);
    RUN_TEST(test_siklus_penuh_menghasilkan_kadar_air);
    return UNITY_END();
}
