// =====================================================================
//  test_settings — lib/HeatboxCycle/CycleSettings: bawaan, validasi
//  (heat >= pre-heat, min < maks, batas), dan blob NVS (rusak -> ditolak).
// =====================================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <CycleSettings.h>
#include <unity.h>

#include <cstring>

using namespace hbcycle;

void setUp() {}
void tearDown() {}

void test_defaults_are_valid_and_match_design() {
    const Settings s = defaultSettings();
    TEST_ASSERT_EQUAL(SettingsError::None, validateSettings(s));
    for (uint8_t i = 0; i < kProfileCount; ++i) {
        TEST_ASSERT_EQUAL_UINT8(80, s.profiles[i].preheatC);
        TEST_ASSERT_EQUAL_UINT8(110, s.profiles[i].heatC);
    }
    TEST_ASSERT_EQUAL_UINT8(10, s.general.preheatMin);
    TEST_ASSERT_EQUAL_UINT8(3, s.general.minHeatMin);
    TEST_ASSERT_EQUAL_UINT8(25, s.general.maxHeatMin);
    TEST_ASSERT_EQUAL_UINT8(0, s.activeProfile);
    TEST_ASSERT_TRUE(s.soundOn);
}

void test_profile_rules() {
    TEST_ASSERT_EQUAL(SettingsError::None, validateProfile(Profile{50, 80}));
    TEST_ASSERT_EQUAL(SettingsError::None, validateProfile(Profile{150, 150}));
    TEST_ASSERT_EQUAL(SettingsError::None, validateProfile(Profile{110, 110}));  // heat = pre-heat sah
    TEST_ASSERT_EQUAL(SettingsError::HeatBelowPreheat, validateProfile(Profile{120, 110}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateProfile(Profile{49, 110}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateProfile(Profile{80, 79}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateProfile(Profile{80, 151}));
}

void test_general_rules() {
    TEST_ASSERT_EQUAL(SettingsError::None, validateGeneral(GeneralTimes{1, 1, 5}));
    TEST_ASSERT_EQUAL(SettingsError::None, validateGeneral(GeneralTimes{60, 25, 60}));
    TEST_ASSERT_EQUAL(SettingsError::MinNotBelowMax, validateGeneral(GeneralTimes{10, 20, 20}));
    TEST_ASSERT_EQUAL(SettingsError::MinNotBelowMax, validateGeneral(GeneralTimes{10, 25, 5}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateGeneral(GeneralTimes{0, 3, 25}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateGeneral(GeneralTimes{10, 26, 60}));
    TEST_ASSERT_EQUAL(SettingsError::OutOfRange, validateGeneral(GeneralTimes{10, 3, 61}));
}

void test_bad_active_profile_rejected() {
    Settings s = defaultSettings();
    s.activeProfile = 3;
    TEST_ASSERT_EQUAL(SettingsError::BadProfileIndex, validateSettings(s));
}

void test_blob_roundtrip() {
    Settings s = defaultSettings();
    s.profiles[1] = Profile{85, 120};
    s.profiles[2] = Profile{75, 105};
    s.general = GeneralTimes{12, 4, 30};
    s.activeProfile = 2;
    s.soundOn = false;
    uint8_t blob[kSettingsBlobSize];
    encodeSettings(s, blob);
    Settings back = defaultSettings();
    TEST_ASSERT_TRUE(decodeSettings(blob, sizeof(blob), back));
    TEST_ASSERT_EQUAL_UINT8(85, back.profiles[1].preheatC);
    TEST_ASSERT_EQUAL_UINT8(105, back.profiles[2].heatC);
    TEST_ASSERT_EQUAL_UINT8(30, back.general.maxHeatMin);
    TEST_ASSERT_EQUAL_UINT8(2, back.activeProfile);
    TEST_ASSERT_FALSE(back.soundOn);
}

void test_corrupted_blob_rejected_and_output_untouched() {
    uint8_t blob[kSettingsBlobSize];
    encodeSettings(defaultSettings(), blob);
    for (size_t i = 0; i < kSettingsBlobSize; ++i) {
        uint8_t bad[kSettingsBlobSize];
        std::memcpy(bad, blob, sizeof(bad));
        bad[i] ^= 0x40;
        Settings out{};
        out.activeProfile = 99;
        TEST_ASSERT_FALSE(decodeSettings(bad, sizeof(bad), out));
        TEST_ASSERT_EQUAL_UINT8(99, out.activeProfile);
    }
    Settings out{};
    TEST_ASSERT_FALSE(decodeSettings(blob, sizeof(blob) - 1, out));
    TEST_ASSERT_FALSE(decodeSettings(nullptr, sizeof(blob), out));
}

void test_valid_crc_but_invalid_values_rejected() {
    Settings s = defaultSettings();
    s.profiles[0] = Profile{130, 100};  // heat < pre-heat
    uint8_t blob[kSettingsBlobSize];
    encodeSettings(s, blob);  // CRC benar, isi tidak sah
    Settings out{};
    TEST_ASSERT_FALSE(decodeSettings(blob, sizeof(blob), out));
}

void test_crc32_known_value() {
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32(text, sizeof(text)));
}

int runAll() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid_and_match_design);
    RUN_TEST(test_profile_rules);
    RUN_TEST(test_general_rules);
    RUN_TEST(test_bad_active_profile_rejected);
    RUN_TEST(test_blob_roundtrip);
    RUN_TEST(test_corrupted_blob_rejected_and_output_untouched);
    RUN_TEST(test_valid_crc_but_invalid_values_rejected);
    RUN_TEST(test_crc32_known_value);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    runAll();
}
void loop() {}
#else
int main() { return runAll(); }
#endif
