// =====================================================================
//  test_stop_logic — keputusan kapan pengeringan dihentikan
//
//  Ini perilaku produk yang sesungguhnya. Matematika boleh sempurna,
//  tapi kalau mesin berhenti di waktu yang salah, hasilnya tetap salah.
//
//  Suite ini yang paling berharga dari semuanya: dia membuktikan jaring
//  pengaman bekerja — satu-satunya hal yang mencegah mesin menggantung
//  selamanya di lantai pabrik.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

void setUp(void)    { hbInit(); }
void tearDown(void) {}

// Siapkan mesin sampai siap mengeringkan.
static void mulaiMengering(int32_t awal) {
    hbtest::feedConstant(awal, 3);
    hbTare();
    hbStartDry();
}


// --- Kurva datar harus memicu berhenti karena kemiringan --------------
//
//  Waktunya deterministik: MIN_DRY_S (holdCnt baru mulai dihitung)
//  + SLOPE_HOLD_S (harus bertahan selama ini) = 180 + 60 = 240 detik.
void test_kurva_datar_berhenti_karena_kemiringan(void) {
    mulaiMengering(2000000);
    hbtest::feedConstant(2000000, MIN_DRY_S + SLOPE_HOLD_S + 30);

    TEST_ASSERT_EQUAL(HB_COOLING, hbStatus()->phase);
    TEST_ASSERT_EQUAL_STRING("laju di bawah ambang", hbStatus()->stopReason);
    TEST_ASSERT_UINT32_WITHIN(3, (uint32_t)(MIN_DRY_S + SLOPE_HOLD_S),
                              hbStatus()->t_dry_s);
}

// --- TIDAK BOLEH berhenti sebelum MIN_DRY_S ---------------------------
//
//  Di menit pertama kurva bisa terlihat landai sesaat — misalnya saat
//  chamber masih memanas dan air belum mulai keluar. Tanpa penjaga ini,
//  mesin bisa berhenti sebelum pengeringan benar-benar dimulai.
void test_tidak_berhenti_sebelum_waktu_minimum(void) {
    mulaiMengering(2000000);
    hbtest::feedConstant(2000000, MIN_DRY_S - 10);

    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);
}

// --- Batas waktu maksimum memaksa berhenti ----------------------------
//
//  INI TEST TERPENTING.
//
//  Kurva tepung telur TIDAK PERNAH benar-benar datar — setelah air habis,
//  protein terus terurai dan berat terus turun pelan. Kriteria kemiringan
//  saja tidak akan pernah terpicu.
//
//  Di sini diumpankan penurunan tetap 50 count/detik (~143 mg/menit),
//  jauh di atas ambang, sehingga HANYA batas waktu yang bisa
//  menghentikannya.
void test_batas_waktu_memaksa_berhenti(void) {
    mulaiMengering(3000000);
    const int dijalankan = hbtest::feedSlope(3000000, -50.0f, MAX_DRY_S + 60);

    TEST_ASSERT_EQUAL(HB_COOLING, hbStatus()->phase);
    TEST_ASSERT_EQUAL_STRING("batas waktu maksimum", hbStatus()->stopReason);
    TEST_ASSERT_TRUE(dijalankan <= MAX_DRY_S + 1);
}

// --- Kemiringan curam tidak boleh memicu berhenti ---------------------
void test_kemiringan_curam_tidak_berhenti(void) {
    mulaiMengering(3000000);
    hbtest::feedSlope(3000000, -50.0f, MIN_DRY_S + SLOPE_HOLD_S + 60);

    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);
}

// --- Penghitung tahan harus RESET kalau kemiringan naik lagi ----------
//
//  Skenario nyata: kurva sempat melandai (air permukaan habis), lalu
//  turun lagi (air dari dalam mulai keluar). Kalau penghitung tidak
//  direset, mesin berhenti terlalu cepat dan sampel masih basah.
void test_penghitung_tahan_direset(void) {
    mulaiMengering(2000000);

    // Fase 1: lewati waktu minimum dengan kemiringan curam
    hbtest::feedSlope(2000000, -50.0f, MIN_DRY_S + 20);
    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);

    // Fase 2: melandai, TAPI belum cukup lama
    const int32_t v1 = 2000000 - 50 * (MIN_DRY_S + 20);
    hbtest::feedConstant(v1, SLOPE_HOLD_S - 20);
    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);

    // Fase 3: curam lagi -> penghitung harus kembali nol
    hbtest::feedSlope(v1, -50.0f, 60);
    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);

    // Fase 4: melandai lagi, kali ini penuh -> baru boleh berhenti
    const int32_t v2 = v1 - 50 * 60;
    hbtest::feedConstant(v2, SLOPE_HOLD_S + 20);
    TEST_ASSERT_EQUAL(HB_COOLING, hbStatus()->phase);
}

// --- Kemiringan POSITIF juga harus dihitung sebagai "tidak datar" -----
//
//  Berat naik itu janggal (biasanya sampel menyerap uap air kembali,
//  atau ada gangguan mekanik), tapi jelas bukan tanda sudah kering.
//  Karena itu implementasinya memakai fabsf().
void test_kemiringan_positif_tidak_dianggap_kering(void) {
    mulaiMengering(1000000);
    hbtest::feedSlope(1000000, +50.0f, MIN_DRY_S + SLOPE_HOLD_S + 60);

    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);
}

// --- Setelah berhenti, status tidak boleh berubah lagi ----------------
void test_status_terkunci_setelah_berhenti(void) {
    mulaiMengering(2000000);
    hbtest::feedConstant(2000000, MIN_DRY_S + SLOPE_HOLD_S + 20);
    TEST_ASSERT_EQUAL(HB_COOLING, hbStatus()->phase);

    const uint32_t t = hbStatus()->t_dry_s;
    hbtest::feedConstant(2000000, 30);            // terus diumpani
    TEST_ASSERT_EQUAL(HB_COOLING, hbStatus()->phase);
    TEST_ASSERT_EQUAL_UINT32(t, hbStatus()->t_dry_s);
}

// --- Alasan berhenti harus terisi, tidak boleh kosong -----------------
//
//  Operator perlu tahu MENGAPA mesin berhenti. "Batas waktu" berarti
//  ada yang perlu diperiksa; "laju di bawah ambang" berarti normal.
void test_alasan_berhenti_selalu_terisi(void) {
    mulaiMengering(2000000);
    hbtest::feedConstant(2000000, MIN_DRY_S + SLOPE_HOLD_S + 20);

    TEST_ASSERT_NOT_NULL(hbStatus()->stopReason);
    TEST_ASSERT_TRUE(hbStatus()->stopReason[0] != '\0');
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_kurva_datar_berhenti_karena_kemiringan);
    RUN_TEST(test_tidak_berhenti_sebelum_waktu_minimum);
    RUN_TEST(test_batas_waktu_memaksa_berhenti);
    RUN_TEST(test_kemiringan_curam_tidak_berhenti);
    RUN_TEST(test_penghitung_tahan_direset);
    RUN_TEST(test_kemiringan_positif_tidak_dianggap_kering);
    RUN_TEST(test_status_terkunci_setelah_berhenti);
    RUN_TEST(test_alasan_berhenti_selalu_terisi);
    return UNITY_END();
}
