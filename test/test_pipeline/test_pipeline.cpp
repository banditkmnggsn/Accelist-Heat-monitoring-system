// =====================================================================
//  test_pipeline — jalur data ADC -> median -> rata-rata detik -> status
//
//  Menguji fungsi matematika satu per satu itu perlu, tapi belum cukup.
//  Suite ini membuktikan potongan-potongan itu benar-benar tersambung:
//  spike yang masuk lewat hbPushRaw() harus benar-benar tersaring sampai
//  ke angka yang dibaca HMI.
// =====================================================================

#include <unity.h>
#include "hb_test_support.h"

void setUp(void)    { hbInit(); }   // WAJIB — modul ini punya state global
void tearDown(void) {}


// --- Kondisi awal setelah hbInit --------------------------------------
void test_kondisi_awal(void) {
    const HbStatus *s = hbStatus();
    TEST_ASSERT_EQUAL(HB_IDLE, s->phase);
    TEST_ASSERT_EQUAL_STRING("", s->stopReason);
    TEST_ASSERT_EQUAL_UINT32(0, s->t_dry_s);
}

// --- Masukan konstan menghasilkan keluaran konstan --------------------
void test_masukan_konstan(void) {
    hbtest::feedConstant(1500000, 5);
    TEST_ASSERT_EQUAL_INT32(1500000, hbStatus()->raw_now);
}

// --- Tick tanpa sampel tidak boleh mengubah apa-apa -------------------
//
//  Bisa terjadi kalau task ADC tersendat atau board baru menyala.
//  Pembagian dengan nol harus dicegah.
void test_tick_tanpa_sampel_aman(void) {
    hbTick1Hz();
    hbTick1Hz();
    TEST_ASSERT_EQUAL(HB_IDLE, hbStatus()->phase);
}

// --- Spike tunggal harus tersaring habis ------------------------------
//
//  Skenario nyata: SSR heater switching, atau ada yang menyenggol meja.
void test_spike_tunggal_tersaring(void) {
    for (int i = 0; i < 9; i++) hbPushRaw(1000000);
    hbPushRaw(9000000);                       // spike besar di sampel ke-10
    hbTick1Hz();
    TEST_ASSERT_INT32_WITHIN(50, 1000000, hbStatus()->raw_now);
}

// --- Spike di TENGAH detik juga harus tersaring -----------------------
void test_spike_di_tengah_tersaring(void) {
    for (int i = 0; i < 5; i++) hbPushRaw(1000000);
    hbPushRaw(-500000);                       // spike negatif
    for (int i = 0; i < 4; i++) hbPushRaw(1000000);
    hbTick1Hz();
    TEST_ASSERT_INT32_WITHIN(50, 1000000, hbStatus()->raw_now);
}

// --- Rata-rata detik mengikuti tren -----------------------------------
//
//  Toleransinya longgar karena median-of-5 menimbulkan sedikit lag pada
//  data yang naik terus. Itu perilaku yang benar, bukan bug — jadi
//  test-nya menguji arah dan besaran, bukan nilai persis.
void test_rata_rata_mengikuti_tren(void) {
    for (int i = 0; i < 10; i++) hbPushRaw(1000000 + i * 100);
    hbTick1Hz();
    const int32_t v = hbStatus()->raw_now;
    TEST_ASSERT_TRUE(v > 1000000);
    TEST_ASSERT_TRUE(v < 1000900);
}

// --- Tare menggeser titik nol -----------------------------------------
void test_tare_menggeser_nol(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    hbtest::feedConstant(1000000, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, hbStatus()->weight_mg);
}

// --- Berat setelah tare terbaca dalam mg yang benar -------------------
void test_berat_setelah_tare(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    // tambah tepat 500 mg
    hbtest::feedConstant(1000000 + hbtest::mgToCounts(500.0f), 2);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f, hbStatus()->weight_mg);
}

// --- Berat bisa negatif (sampel diangkat sebelum tare ulang) ----------
void test_berat_negatif(void) {
    hbtest::feedConstant(1000000, 3);
    hbTare();
    hbtest::feedConstant(1000000 - hbtest::mgToCounts(200.0f), 2);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -200.0f, hbStatus()->weight_mg);
}

// --- Fase tidak berubah sendiri saat IDLE -----------------------------
//
//  Kemiringan hanya dihitung saat HB_DRYING. Kalau tidak, mesin bisa
//  "berhenti mengeringkan" padahal belum mulai.
void test_tidak_mengering_saat_idle(void) {
    hbtest::feedConstant(1000000, 300);
    TEST_ASSERT_EQUAL(HB_IDLE, hbStatus()->phase);
    TEST_ASSERT_EQUAL_UINT32(0, hbStatus()->t_dry_s);
}

// --- hbStartDry memulai penghitungan waktu ----------------------------
void test_start_dry_mulai_menghitung(void) {
    hbtest::feedConstant(1000000, 3);
    hbStartDry();
    TEST_ASSERT_EQUAL(HB_DRYING, hbStatus()->phase);
    TEST_ASSERT_EQUAL_UINT32(0, hbStatus()->t_dry_s);

    hbtest::feedConstant(1000000, 10);
    TEST_ASSERT_EQUAL_UINT32(10, hbStatus()->t_dry_s);
}

// --- Kemiringan terbaca dalam mg/menit dengan benar -------------------
//
//  Turun 21 count/detik = 1 mg/detik = 60 mg/menit.
void test_kemiringan_dalam_mg_per_menit(void) {
    hbtest::feedConstant(2000000, 2);
    hbStartDry();
    hbtest::feedSlope(2000000, -21.0f, 150, false);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, -60.0f, hbStatus()->slope_mgmin);
}

// --- setUp benar-benar mereset antar test -----------------------------
//
//  Kalau state bocor antar test, kegagalan jadi tergantung urutan
//  eksekusi — bug paling membingungkan untuk dilacak pemula.
void test_state_bersih_setelah_init(void) {
    hbtest::feedConstant(1234567, 5);
    hbStartDry();
    hbtest::feedConstant(1234567, 5);

    hbInit();                                    // seperti yang dilakukan setUp
    TEST_ASSERT_EQUAL(HB_IDLE, hbStatus()->phase);
    TEST_ASSERT_EQUAL_UINT32(0, hbStatus()->t_dry_s);
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_kondisi_awal);
    RUN_TEST(test_masukan_konstan);
    RUN_TEST(test_tick_tanpa_sampel_aman);
    RUN_TEST(test_spike_tunggal_tersaring);
    RUN_TEST(test_spike_di_tengah_tersaring);
    RUN_TEST(test_rata_rata_mengikuti_tren);
    RUN_TEST(test_tare_menggeser_nol);
    RUN_TEST(test_berat_setelah_tare);
    RUN_TEST(test_berat_negatif);
    RUN_TEST(test_tidak_mengering_saat_idle);
    RUN_TEST(test_start_dry_mulai_menghitung);
    RUN_TEST(test_kemiringan_dalam_mg_per_menit);
    RUN_TEST(test_state_bersih_setelah_init);
    return UNITY_END();
}
