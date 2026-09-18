// =====================================================================
//  test_math_median — filter median
//
//  Tugasnya cuma satu: membuang spike dari getaran, dentuman, atau
//  gangguan listrik saat SSR heater switching. Tapi kalau dia salah,
//  spike-nya lolos dan merusak rata-rata detik itu.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

using heatbox::detail::median;

void setUp(void)    { hbInit(); }
void tearDown(void) {}


// --- Kasus dasar ------------------------------------------------------
void test_nilai_acak_sederhana(void) {
    const int32_t y[] = {9, 3, 7, 5, 1};        // urut: 1 3 5 7 9
    TEST_ASSERT_EQUAL_INT32(5, median(y, 5));
}

// --- Median TIDAK BOLEH mengubah urutan array asli --------------------
//
//  Implementasi menyalin ke buffer lokal sebelum sorting. Kalau suatu
//  saat ada yang "mengoptimalkan" dengan sorting in-place, buffer
//  melingkar di hbPushRaw akan rusak diam-diam.
void test_array_asli_tidak_berubah(void) {
    int32_t y[] = {9, 3, 7, 5, 1};
    median(y, 5);
    TEST_ASSERT_EQUAL_INT32(9, y[0]);
    TEST_ASSERT_EQUAL_INT32(3, y[1]);
    TEST_ASSERT_EQUAL_INT32(7, y[2]);
    TEST_ASSERT_EQUAL_INT32(5, y[3]);
    TEST_ASSERT_EQUAL_INT32(1, y[4]);
}

// --- Satu spike besar harus dibuang -----------------------------------
void test_membuang_satu_spike(void) {
    const int32_t y[] = {1000, 1000, 999999, 1000, 1000};
    TEST_ASSERT_EQUAL_INT32(1000, median(y, 5));
}

// --- Dua spike masih tertahan (batas kemampuan median-of-5) -----------
//
//  Median-of-N tahan sampai (N-1)/2 pencilan. Untuk N=5 berarti 2.
//  Test ini mendokumentasikan batas itu secara eksplisit.
void test_dua_spike_masih_tertahan(void) {
    const int32_t y[] = {999999, 1000, 1000, 1000, -999999};
    TEST_ASSERT_EQUAL_INT32(1000, median(y, 5));
}

// --- Buffer belum penuh (kondisi awal setelah hbInit) -----------------
//
//  Di detik pertama, hbPushRaw baru mengisi 1-4 dari 5 slot. Ini jalur
//  kode yang berbeda dan gampang terlewat.
void test_buffer_belum_penuh(void) {
    const int32_t y[] = {50, 10, 30, 20, 40};
    TEST_ASSERT_EQUAL_INT32(50, median(y, 1));   // {50}
    TEST_ASSERT_EQUAL_INT32(50, median(y, 2));   // {10,50}      -> index 1
    TEST_ASSERT_EQUAL_INT32(30, median(y, 3));   // {10,30,50}   -> index 1
    TEST_ASSERT_EQUAL_INT32(30, median(y, 4));   // {10,20,30,50}-> index 2
    TEST_ASSERT_EQUAL_INT32(30, median(y, 5));   // {10,20,30,40,50}
}

// --- Nilai negatif dan count besar ------------------------------------
void test_nilai_negatif_dan_besar(void) {
    const int32_t y1[] = {-500, -100, -300, -200, -400};
    TEST_ASSERT_EQUAL_INT32(-300, median(y1, 5));

    const int32_t y2[] = {2000005, 2000001, 2000003, 2000002, 2000004};
    TEST_ASSERT_EQUAL_INT32(2000003, median(y2, 5));
}

// --- Semua nilai sama -------------------------------------------------
void test_semua_sama(void) {
    const int32_t y[] = {1234567, 1234567, 1234567, 1234567, 1234567};
    TEST_ASSERT_EQUAL_INT32(1234567, median(y, 5));
}

// --- Sudah terurut, dan terurut terbalik ------------------------------
//
//  Dua kasus terburuk untuk insertion sort. Bukan soal kecepatan di sini,
//  tapi memastikan tidak ada off-by-one di batas loop.
void test_sudah_terurut_dan_terbalik(void) {
    const int32_t naik[]  = {1, 2, 3, 4, 5};
    const int32_t turun[] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32(3, median(naik, 5));
    TEST_ASSERT_EQUAL_INT32(3, median(turun, 5));
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_nilai_acak_sederhana);
    RUN_TEST(test_array_asli_tidak_berubah);
    RUN_TEST(test_membuang_satu_spike);
    RUN_TEST(test_dua_spike_masih_tertahan);
    RUN_TEST(test_buffer_belum_penuh);
    RUN_TEST(test_nilai_negatif_dan_besar);
    RUN_TEST(test_semua_sama);
    RUN_TEST(test_sudah_terurut_dan_terbalik);
    return UNITY_END();
}
