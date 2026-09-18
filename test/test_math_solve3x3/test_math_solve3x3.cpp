// =====================================================================
//  test_math_solve3x3 — penyelesai sistem linier 3x3 (aturan Cramer)
//
//  Dipakai di dalam fitting kurva: untuk tiap nilai tau yang dicoba,
//  tiga parameter sisanya (W_inf, A, k) diselesaikan lewat fungsi ini.
//  Kalau dia salah, fitting menghasilkan angka yang kelihatan wajar
//  tapi keliru — jenis bug yang paling sulit dilacak.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

using heatbox::detail::solve3x3;

void setUp(void)    { hbInit(); }
void tearDown(void) {}


// --- Matriks diagonal: jawaban bisa dihitung di kepala ----------------
void test_matriks_diagonal(void) {
    const float M[3][3] = { {2,0,0}, {0,3,0}, {0,0,4} };
    const float r[3]    = { 2, 6, 12 };
    float x[3];
    TEST_ASSERT_TRUE(solve3x3(M, r, x));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, x[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, x[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, x[2]);
}

// --- Matriks identitas: keluaran harus sama persis dengan masukan -----
void test_matriks_identitas(void) {
    const float M[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    const float r[3]    = { 7, -3, 0.5f };
    float x[3];
    TEST_ASSERT_TRUE(solve3x3(M, r, x));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  7.0f, x[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -3.0f, x[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f,  0.5f, x[2]);
}

// --- Matriks penuh, semua elemen terisi -------------------------------
//
//  M * [1,2,3] = [14,32,53]. Ini yang menguji seluruh 9 elemen kofaktor;
//  matriks diagonal tidak akan menangkap salah tanda di Cramer.
void test_matriks_penuh(void) {
    const float M[3][3] = { {1,2,3}, {4,5,6}, {7,8,10} };
    const float r[3]    = { 14, 32, 53 };
    float x[3];
    TEST_ASSERT_TRUE(solve3x3(M, r, x));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, x[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.0f, x[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 3.0f, x[2]);
}

// --- Solusi negatif ---------------------------------------------------
//
//  Penting: koefisien k (dekomposisi + creep) SELALU negatif di mesin
//  asli, karena beratnya turun. Salah tanda tidak boleh lolos.
void test_solusi_negatif(void) {
    const float M[3][3] = { {2,1,0}, {1,3,1}, {0,1,2} };
    // M * [-1, 2, -3] = [0, 2, -4]
    const float r[3] = { 0, 2, -4 };
    float x[3];
    TEST_ASSERT_TRUE(solve3x3(M, r, x));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -1.0f, x[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,  2.0f, x[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -3.0f, x[2]);
}

// --- Matriks singular harus DITOLAK, bukan menghasilkan sampah --------
//
//  Baris 2 dan 3 kelipatan baris 1 -> determinan nol.
//  Ini bisa benar-benar terjadi: kalau data yang di-fit terlalu pendek
//  atau terlalu datar, matriks normalnya jadi singular. Fungsi harus
//  mengembalikan false supaya fitting bisa menolak nilai tau itu,
//  bukan meneruskan angka hasil pembagian nol.
void test_matriks_singular_ditolak(void) {
    const float M[3][3] = { {1,2,3}, {2,4,6}, {3,6,9} };
    const float r[3]    = { 1, 2, 3 };
    float x[3];
    TEST_ASSERT_FALSE(solve3x3(M, r, x));
}

// --- Dua baris identik (kasus singular yang lain) ---------------------
void test_baris_identik_ditolak(void) {
    const float M[3][3] = { {1,2,3}, {1,2,3}, {4,5,6} };
    const float r[3]    = { 1, 1, 2 };
    float x[3];
    TEST_ASSERT_FALSE(solve3x3(M, r, x));
}

// --- Matriks simetris positif-definit ---------------------------------
//
//  Bentuk inilah yang sebenarnya muncul dari persamaan normal
//  least-squares. Test ini paling dekat dengan pemakaian aslinya.
void test_matriks_simetris(void) {
    const float M[3][3] = { {4,1,1}, {1,5,2}, {1,2,6} };
    // M * [1,1,1] = [6,8,9]
    const float r[3] = { 6, 8, 9 };
    float x[3];
    TEST_ASSERT_TRUE(solve3x3(M, r, x));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, x[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, x[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, x[2]);
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_matriks_diagonal);
    RUN_TEST(test_matriks_identitas);
    RUN_TEST(test_matriks_penuh);
    RUN_TEST(test_solusi_negatif);
    RUN_TEST(test_matriks_singular_ditolak);
    RUN_TEST(test_baris_identik_ditolak);
    RUN_TEST(test_matriks_simetris);
    return UNITY_END();
}
