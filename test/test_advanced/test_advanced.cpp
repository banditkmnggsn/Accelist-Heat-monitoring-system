// =====================================================================
//  test_advanced — lib/HeatboxCycle/AdvancedSettings: bawaan sama dengan
//  CycleConfig/PiParams lama, validasi, blob NVS, konversi ke CycleConfig,
//  dan filter TAMPILAN TENANG.
// =====================================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <AdvancedSettings.h>
#include <unity.h>

#include <cmath>

using namespace hbcycle;

void setUp() {}
void tearDown() {}

void test_defaults_match_previous_constants() {
    const AdvancedSettings a = defaultAdvanced();
    TEST_ASSERT_EQUAL(AdvancedError::None, validateAdvanced(a));
    TEST_ASSERT_EQUAL_UINT8(40, a.method.phaseARate10);
    TEST_ASSERT_EQUAL_UINT8(3, a.method.phaseAConfirm);
    TEST_ASSERT_EQUAL_UINT8(10, a.method.targetRate10);
    TEST_ASSERT_EQUAL_UINT8(2, a.method.stopExtraMin);
    TEST_ASSERT_EQUAL_UINT8(60, a.method.tareStableSec);
    TEST_ASSERT_EQUAL_UINT8(15, a.method.overTempMarginC);
    TEST_ASSERT_EQUAL_UINT16(50, a.heater.kp1000);
    TEST_ASSERT_EQUAL_UINT16(120, a.heater.tiSec);
    TEST_ASSERT_EQUAL_UINT8(8, a.heater.bandC);
    TEST_ASSERT_EQUAL_UINT8(100, a.heater.maxDutyPct);
    TEST_ASSERT_TRUE(a.display.quiet);
    TEST_ASSERT_EQUAL_UINT8(3, a.display.deadbandMg);

    // Bawaan diterapkan = CycleConfig bawaan (tidak mengubah metode)
    const CycleConfig base;
    const CycleConfig c = applyMethod(a.method);
    TEST_ASSERT_EQUAL_FLOAT(base.phaseARateMgMin, c.phaseARateMgMin);
    TEST_ASSERT_EQUAL_FLOAT(base.shrinkMinRateMgMin, c.shrinkMinRateMgMin);
    TEST_ASSERT_EQUAL_FLOAT(base.targetRateMgMin, c.targetRateMgMin);
    TEST_ASSERT_EQUAL_FLOAT(base.stopExtraMin, c.stopExtraMin);
    TEST_ASSERT_EQUAL_UINT32(base.tareStableSec, c.tareStableSec);
    TEST_ASSERT_EQUAL_FLOAT(base.overTempMarginC, c.overTempMarginC);
    const PiParams pi = toPiParams(a.heater);
    const PiParams piBase;
    TEST_ASSERT_EQUAL_FLOAT(piBase.kp, pi.kp);
    TEST_ASSERT_EQUAL_FLOAT(piBase.tiSec, pi.tiSec);
    TEST_ASSERT_EQUAL_FLOAT(piBase.integralBandC, pi.integralBandC);
    TEST_ASSERT_EQUAL_FLOAT(piBase.maxDuty, pi.maxDuty);
}

void test_validation_rules() {
    AdvancedSettings a = defaultAdvanced();
    a.method.targetRate10 = a.method.phaseARate10;
    TEST_ASSERT_EQUAL(AdvancedError::TargetNotBelowPhaseA, validateMethod(a.method));
    a = defaultAdvanced();
    a.method.phaseARate10 = 9;
    TEST_ASSERT_EQUAL(AdvancedError::OutOfRange, validateMethod(a.method));
    a = defaultAdvanced();
    a.heater.kp1000 = 501;
    TEST_ASSERT_EQUAL(AdvancedError::OutOfRange, validateHeater(a.heater));
    a = defaultAdvanced();
    a.display.deadbandMg = 0;
    TEST_ASSERT_EQUAL(AdvancedError::OutOfRange, validateDisplay(a.display));
}

void test_blob_roundtrip_and_corruption() {
    AdvancedSettings a = defaultAdvanced();
    a.method.phaseARate10 = 55;
    a.method.targetRate10 = 8;
    a.heater.kp1000 = 321;
    a.heater.tiSec = 600;
    a.display.quiet = false;
    a.display.deadbandMg = 7;
    uint8_t blob[kAdvancedBlobSize];
    encodeAdvanced(a, blob);
    AdvancedSettings back{};
    TEST_ASSERT_TRUE(decodeAdvanced(blob, sizeof(blob), back));
    TEST_ASSERT_EQUAL_UINT8(55, back.method.phaseARate10);
    TEST_ASSERT_EQUAL_UINT8(8, back.method.targetRate10);
    TEST_ASSERT_EQUAL_UINT16(321, back.heater.kp1000);
    TEST_ASSERT_EQUAL_UINT16(600, back.heater.tiSec);
    TEST_ASSERT_FALSE(back.display.quiet);
    TEST_ASSERT_EQUAL_UINT8(7, back.display.deadbandMg);

    for (size_t i = 0; i < sizeof(blob); ++i) {
        uint8_t bad[kAdvancedBlobSize];
        for (size_t k = 0; k < sizeof(blob); ++k) bad[k] = blob[k];
        bad[i] ^= 0x01;
        AdvancedSettings untouched = defaultAdvanced();
        TEST_ASSERT_FALSE(decodeAdvanced(bad, sizeof(bad), untouched));
        TEST_ASSERT_EQUAL_UINT8(40, untouched.method.phaseARate10);  // tidak ditulis
    }
    TEST_ASSERT_FALSE(decodeAdvanced(blob, sizeof(blob) - 1, back));
    TEST_ASSERT_FALSE(decodeAdvanced(nullptr, sizeof(blob), back));

    // Isi tidak sah dengan CRC benar tetap ditolak
    AdvancedSettings invalid = defaultAdvanced();
    invalid.method.targetRate10 = 60;
    encodeAdvanced(invalid, blob);
    TEST_ASSERT_FALSE(decodeAdvanced(blob, sizeof(blob), back));
}

void test_apply_method_changes_config() {
    MethodParams m = defaultAdvanced().method;
    m.phaseARate10 = 60;
    m.phaseAConfirm = 5;
    m.targetRate10 = 5;
    m.stopExtraMin = 0;
    m.tareStableSec = 90;
    m.overTempMarginC = 20;
    const CycleConfig c = applyMethod(m);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, c.phaseARateMgMin);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, c.shrinkMinRateMgMin);
    TEST_ASSERT_EQUAL_UINT8(5, c.phaseAConfirm);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, c.targetRateMgMin);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.stopExtraMin);
    TEST_ASSERT_EQUAL_UINT32(90, c.tareStableSec);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, c.overTempMarginC);
    TEST_ASSERT_EQUAL_FLOAT(165.0f, c.overTempAbsC);  // tidak disentuh
}

void test_set_config_refused_while_running() {
    CycleController ctl;
    CycleConfig c;
    c.phaseARateMgMin = 7.0f;
    TEST_ASSERT_TRUE(ctl.setConfig(c));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, ctl.config().phaseARateMgMin);

    StartParams sp{};
    sp.settings = defaultSettings();
    sp.testId = 1;
    sp.zeroRaw = 0.0;
    sp.countsPerGram = 5500.0f;
    sp.firmware = "t";
    TEST_ASSERT_TRUE(ctl.start(sp, 1000));
    c.phaseARateMgMin = 2.0f;
    TEST_ASSERT_FALSE(ctl.setConfig(c));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, ctl.config().phaseARateMgMin);
}

void test_quiet_value() {
    DisplayParams d{true, 3};
    QuietValue q;
    TEST_ASSERT_EQUAL_FLOAT(5000.0f, q.update(5000.0f, d));
    TEST_ASSERT_EQUAL_FLOAT(5000.0f, q.update(5002.9f, d));  // di dalam dead band
    TEST_ASSERT_EQUAL_FLOAT(5000.0f, q.update(4997.5f, d));
    TEST_ASSERT_EQUAL_FLOAT(5003.0f, q.update(5003.0f, d));  // >= dead band: berganti
    TEST_ASSERT_TRUE(std::isnan(q.update(NAN, d)));
    TEST_ASSERT_EQUAL_FLOAT(5001.0f, q.update(5001.0f, d));  // setelah NAN mulai baru
    d.quiet = false;
    TEST_ASSERT_EQUAL_FLOAT(5001.4f, q.update(5001.4f, d));  // filter mati
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_previous_constants);
    RUN_TEST(test_validation_rules);
    RUN_TEST(test_blob_roundtrip_and_corruption);
    RUN_TEST(test_apply_method_changes_config);
    RUN_TEST(test_set_config_refused_while_running);
    RUN_TEST(test_quiet_value);
    return UNITY_END();
}
