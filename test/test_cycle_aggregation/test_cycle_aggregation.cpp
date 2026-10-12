// =====================================================================
//  test_cycle_aggregation — sampel 10 SPS -> per detik -> per menit
//
//  Menguji SecondAggregator dan MinuteBuilder (lib/HeatboxCycle): rata-rata
//  per detik, detik tidak sah, loop yang tertunda, millis() yang
//  membungkus, dan nilai per menit yang dibaca tepat di tengah menit.
//
//  Jalan di PC (env native / MSVC) dan di ESP32.
// =====================================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <MinuteBuilder.h>
#include <SecondAggregator.h>
#include <unity.h>

#include <cmath>
#include <cstdio>

using hbcycle::MinuteBuilder;
using hbcycle::MinuteRecord;
using hbcycle::SecondAggregator;
using hbcycle::SecondRecord;

namespace {

void assertNear(double expected, double actual, double tolerance, const char* what) {
    if (std::fabs(actual - expected) <= tolerance) {
        return;
    }
    char message[160];
    snprintf(message, sizeof(message), "%s: diharapkan %.9f, dapat %.9f", what, expected, actual);
    TEST_FAIL_MESSAGE(message);
}

// Detik sah buatan untuk MinuteBuilder, waktu di tengah detik.
SecondRecord validSecond(uint32_t index) {
    SecondRecord second;
    second.index = index;
    second.tSec = index + 0.5;
    second.rawMean = 0.0;
    second.good = 10;
    second.bad = 0;
    second.valid = true;
    return second;
}

SecondRecord invalidSecond(uint32_t index) {
    SecondRecord second = validSecond(index);
    second.good = 0;
    second.valid = false;
    second.rawMean = NAN;
    return second;
}

// Massa turun 40 mg/menit dari 5000 mg.
double fallingMass(double tSec) { return 5000.0 - 40.0 * tSec / 60.0; }

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// =====================================================================
//  SecondAggregator
// =====================================================================
void test_detik_rata_rata_sampel_baik(void) {
    SecondAggregator agg;
    agg.reset(1000);
    for (int i = 0; i < 10; ++i) {
        agg.addSample(1000 + 50 + 100 * i, 1000 + i, true);
    }
    SecondRecord second;
    TEST_ASSERT_FALSE(agg.poll(1999, second));  // detik belum lewat
    TEST_ASSERT_TRUE(agg.poll(2000, second));
    TEST_ASSERT_EQUAL_UINT32(0, second.index);
    TEST_ASSERT_TRUE(second.valid);
    TEST_ASSERT_EQUAL_UINT8(10, second.good);
    assertNear(1004.5, second.rawMean, 1e-12, "rawMean");
    assertNear(0.5, second.tSec, 1e-12, "tSec = rata-rata waktu sampel");
    TEST_ASSERT_FALSE(agg.poll(2000, second));
}

void test_detik_kurang_sampel_tidak_sah(void) {
    SecondAggregator agg;
    agg.reset(0);
    for (int i = 0; i < SecondAggregator::kMinSamples - 1; ++i) {
        agg.addSample(100 * i, 500, true);
    }
    SecondRecord second;
    TEST_ASSERT_TRUE(agg.poll(1000, second));
    TEST_ASSERT_FALSE(second.valid);
    assertNear(500.0, second.rawMean, 1e-12, "rawMean tetap dihitung");
}

// Satu sampel clip (overload / wiring) membuat seluruh detik dicurigai.
void test_sampel_buruk_membuat_detik_tidak_sah(void) {
    SecondAggregator agg;
    agg.reset(0);
    for (int i = 0; i < 10; ++i) {
        agg.addSample(100 * i, 500, i != 3);
    }
    SecondRecord second;
    TEST_ASSERT_TRUE(agg.poll(1000, second));
    TEST_ASSERT_FALSE(second.valid);
    TEST_ASSERT_EQUAL_UINT8(9, second.good);
    TEST_ASSERT_EQUAL_UINT8(1, second.bad);
}

// Loop tertunda 2,5 detik: detik yang terlewat keluar satu per panggilan,
// berurutan, sebagai tidak sah. Tidak ada detik yang hilang dari hitungan.
void test_loop_tertunda_mengeluarkan_detik_kosong(void) {
    SecondAggregator agg;
    agg.reset(0);
    for (int i = 0; i < 10; ++i) {
        agg.addSample(100 * i, 700, true);
    }
    SecondRecord second;
    TEST_ASSERT_TRUE(agg.poll(3500, second));
    TEST_ASSERT_EQUAL_UINT32(0, second.index);
    TEST_ASSERT_TRUE(second.valid);
    TEST_ASSERT_TRUE(agg.poll(3500, second));
    TEST_ASSERT_EQUAL_UINT32(1, second.index);
    TEST_ASSERT_FALSE(second.valid);
    TEST_ASSERT_TRUE(std::isnan(second.rawMean));
    assertNear(1.5, second.tSec, 1e-12, "tSec detik kosong");
    TEST_ASSERT_TRUE(agg.poll(3500, second));
    TEST_ASSERT_EQUAL_UINT32(2, second.index);
    TEST_ASSERT_FALSE(agg.poll(3500, second));
}

// millis() melewati 2^32 setelah 49,7 hari menyala.
void test_millis_membungkus(void) {
    SecondAggregator agg;
    const uint32_t epoch = 0xFFFFFE00u;  // 512 ms sebelum membungkus
    agg.reset(epoch);
    for (uint32_t i = 0; i < 10; ++i) {
        agg.addSample(epoch + 100 * i, 42, true);
    }
    SecondRecord second;
    TEST_ASSERT_FALSE(agg.poll(epoch + 999, second));
    TEST_ASSERT_TRUE(agg.poll(epoch + 1000, second));
    TEST_ASSERT_TRUE(second.valid);
    TEST_ASSERT_EQUAL_UINT8(10, second.good);
}

void test_sampel_di_luar_detik_diabaikan(void) {
    SecondAggregator agg;
    agg.reset(5000);
    agg.addSample(4999, 1, true);  // sebelum epoch
    agg.addSample(6000, 1, true);  // milik detik berikutnya, poll() belum dipanggil
    SecondRecord second;
    TEST_ASSERT_TRUE(agg.poll(6000, second));
    TEST_ASSERT_EQUAL_UINT8(0, second.good);
}

// =====================================================================
//  MinuteBuilder
// =====================================================================
void test_menit_dibaca_di_tengah_menit(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    int closed = 0;
    for (uint32_t i = 0; i < 120; ++i) {
        const SecondRecord second = validSecond(i);
        if (builder.add(second, fallingMass(second.tSec), minute)) {
            ++closed;
            TEST_ASSERT_EQUAL_UINT32(i / 60, minute.index);
            assertNear(minute.index + 0.5, minute.tMidMin, 1e-12, "tMidMin");
            assertNear(fallingMass(minute.tMidMin * 60.0), minute.massMg, 1e-9, "massMg");
            assertNear(-40.0, minute.slopeMgPerMin, 1e-9, "slope");
            TEST_ASSERT_TRUE(minute.valid);
            TEST_ASSERT_EQUAL_UINT8(60, minute.secondsUsed);
        }
    }
    TEST_ASSERT_EQUAL_INT(2, closed);
}

// Lebih dari 1/3 menit hilang: nilai tetap dicatat untuk log, tapi menit
// tidak boleh dipakai.
void test_menit_kurang_detik_tidak_sah(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    bool closed = false;
    for (uint32_t i = 0; i < 60; ++i) {
        const SecondRecord second = (i % 2 == 0) ? validSecond(i) : invalidSecond(i);
        closed = builder.add(second, fallingMass(second.tSec), minute);
    }
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_EQUAL_UINT8(30, minute.secondsValid);
    TEST_ASSERT_FALSE(minute.valid);
    TEST_ASSERT_TRUE(std::isfinite(minute.massMg));
}

// Tisu menekan wadah 10 detik saat massa turun 40 mg/menit: 10 detik itu
// dibuang dan nilai menit tidak bias.
void test_menit_gangguan_10_detik_tidak_bias(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    bool closed = false;
    for (uint32_t i = 0; i < 60; ++i) {
        const SecondRecord second = validSecond(i);
        const double disturbance = (i >= 20 && i < 30) ? 50.0 : 0.0;
        closed = builder.add(second, fallingMass(second.tSec) + disturbance, minute);
    }
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_EQUAL_UINT8(50, minute.secondsUsed);
    TEST_ASSERT_TRUE(minute.valid);
    assertNear(fallingMass(30.0), minute.massMg, 1e-9, "massMg");
}

// Beban 10 g diletakkan di tengah menit (kasus nyata di log 5 Okt 2026,
// yang dulu lolos sebagai menit sah dengan sebaran 643 mg).
void test_menit_dengan_perubahan_beban_tidak_sah(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    bool closed = false;
    for (uint32_t i = 0; i < 60; ++i) {
        closed = builder.add(validSecond(i), (i >= 30) ? 10000.0 : 0.0, minute);
    }
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_TRUE(minute.spreadMg > MinuteBuilder::kMaxSpreadMg);
    TEST_ASSERT_FALSE(minute.valid);
}

// Noise biasa ±1,5 mg per detik tidak boleh membuat menit ditolak.
void test_menit_noise_biasa_tetap_sah(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    bool closed = false;
    for (uint32_t i = 0; i < 60; ++i) {
        const SecondRecord second = validSecond(i);
        closed = builder.add(second, fallingMass(second.tSec) + ((i % 2 == 0) ? 1.5 : -1.5), minute);
    }
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_TRUE(minute.valid);
    assertNear(fallingMass(30.0), minute.massMg, 0.5, "massMg");
}

void test_detik_melompat_menutup_menit(void) {
    MinuteBuilder builder;
    builder.reset();
    MinuteRecord minute;
    for (uint32_t i = 0; i < 30; ++i) {
        TEST_ASSERT_FALSE(builder.add(validSecond(i), 1000.0, minute));
    }
    TEST_ASSERT_TRUE(builder.add(validSecond(65), 1000.0, minute));
    TEST_ASSERT_EQUAL_UINT32(0, minute.index);
    TEST_ASSERT_EQUAL_UINT8(30, minute.secondsValid);
}

// =====================================================================
//  Alur penuh: sampel 10 SPS -> detik -> menit
// =====================================================================

// Osilator ADS1232 meleset 3 % (periode 97 ms, 10-11 sampel per detik) dan
// epoch tidak sejajar dengan sampel. Massa turun linear, jadi nilai menit
// harus tepat sama dengan massa sebenarnya di tengah menit.
void test_alur_penuh_osilator_meleset(void) {
    const double zeroRaw = 145883.0;
    const double countsPerGram = 5499.0;
    const uint32_t epoch = 12345;

    SecondAggregator agg;
    MinuteBuilder builder;
    agg.reset(epoch);
    builder.reset();

    int minutes = 0;
    SecondRecord second;
    MinuteRecord minute;
    for (uint32_t ms = epoch + 40; ms < epoch + 3 * 60000 + 100; ms += 97) {
        const double tSec = (ms - epoch) / 1000.0;
        const double raw = zeroRaw + fallingMass(tSec) / 1000.0 * countsPerGram;
        while (agg.poll(ms, second)) {
            TEST_ASSERT_TRUE(second.valid);
            const double massMg = (second.rawMean - zeroRaw) / countsPerGram * 1000.0;
            if (builder.add(second, massMg, minute)) {
                ++minutes;
                TEST_ASSERT_TRUE(minute.valid);
                // Raw dibulatkan ke count bulat: 1 count = 0,18 mg, jadi
                // pembulatan memberi galat jauh di bawah 0,1 mg per menit.
                assertNear(fallingMass(minute.tMidMin * 60.0), minute.massMg, 0.1, "massMg");
            }
        }
        agg.addSample(ms, static_cast<int32_t>(std::lround(raw)), true);
    }
    TEST_ASSERT_EQUAL_INT(3, minutes);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_detik_rata_rata_sampel_baik);
    RUN_TEST(test_detik_kurang_sampel_tidak_sah);
    RUN_TEST(test_sampel_buruk_membuat_detik_tidak_sah);
    RUN_TEST(test_loop_tertunda_mengeluarkan_detik_kosong);
    RUN_TEST(test_millis_membungkus);
    RUN_TEST(test_sampel_di_luar_detik_diabaikan);
    RUN_TEST(test_menit_dibaca_di_tengah_menit);
    RUN_TEST(test_menit_kurang_detik_tidak_sah);
    RUN_TEST(test_menit_gangguan_10_detik_tidak_bias);
    RUN_TEST(test_menit_dengan_perubahan_beban_tidak_sah);
    RUN_TEST(test_menit_noise_biasa_tetap_sah);
    RUN_TEST(test_detik_melompat_menutup_menit);
    RUN_TEST(test_alur_penuh_osilator_meleset);
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
