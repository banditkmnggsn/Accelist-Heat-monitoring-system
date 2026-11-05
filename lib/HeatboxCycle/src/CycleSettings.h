#pragma once

// =====================================================================
//  CycleSettings.h — pengaturan operator (RANCANGAN §4.1)
//
//  3 profil suhu (pre-heat + heat) + waktu umum (lama pre-heat, waktu
//  minimum/maksimum heat) + profil aktif + suara nyala/mati.
//  C++ murni: validasi dan format blob NVS diuji di PC. Penyimpanan ke
//  NVS (namespace "hbcfg") ada di src/SettingsStore.
// =====================================================================

#include <cstddef>
#include <cstdint>

namespace hbcycle {

constexpr uint8_t kProfileCount = 3;

struct Profile {
    uint8_t preheatC;  // suhu pre-heat, C
    uint8_t heatC;     // suhu heat, C
};

struct GeneralTimes {
    uint8_t preheatMin;  // lama pre-heat, menit
    uint8_t minHeatMin;  // waktu minimum heat, menit
    uint8_t maxHeatMin;  // waktu maksimum heat (timeout), menit
};

struct Settings {
    Profile profiles[kProfileCount];
    GeneralTimes general;
    uint8_t activeProfile;  // 0..kProfileCount-1
    bool soundOn;
};

struct Range {
    uint8_t min;
    uint8_t max;
};

// Batas menu (RANCANGAN §4.1). Langkah ubah 1 (percepatan di UI).
constexpr Range kPreheatCRange    = {50, 150};
constexpr Range kHeatCRange       = {80, 150};
constexpr Range kPreheatMinRange  = {1, 60};
constexpr Range kMinHeatMinRange  = {1, 25};
constexpr Range kMaxHeatMinRange  = {5, 60};

enum class SettingsError : uint8_t {
    None,
    OutOfRange,
    HeatBelowPreheat,  // heat harus >= pre-heat
    MinNotBelowMax,    // waktu min harus < waktu maks
    BadProfileIndex,
};

// Bawaan: semua profil 80/110 C, 10/3/25 menit, profil 1, suara nyala.
Settings defaultSettings();

SettingsError validateProfile(const Profile& profile);
SettingsError validateGeneral(const GeneralTimes& general);
SettingsError validateSettings(const Settings& settings);

// ---- format blob NVS ----
// magic(2) versi(1) aktif(1) suara(1) profil 3x2 umum 3 crc32(4)
constexpr size_t kSettingsBlobSize = 18;

void encodeSettings(const Settings& settings, uint8_t (&out)[kSettingsBlobSize]);

// false kalau ukuran/magic/versi/CRC salah ATAU isinya tidak lolos
// validasi; `out` hanya ditulis kalau true.
bool decodeSettings(const uint8_t* data, size_t length, Settings& out);

uint32_t crc32(const uint8_t* data, size_t length);

}  // namespace hbcycle
