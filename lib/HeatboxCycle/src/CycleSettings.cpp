#include "CycleSettings.h"

namespace hbcycle {

namespace {

constexpr uint8_t kMagic0 = 'H';
constexpr uint8_t kMagic1 = 'C';
constexpr uint8_t kVersion = 1;
constexpr size_t kCrcOffset = kSettingsBlobSize - 4;

bool inRange(uint8_t value, Range range) { return value >= range.min && value <= range.max; }

}  // namespace

Settings defaultSettings() {
    Settings s{};
    for (uint8_t i = 0; i < kProfileCount; ++i) {
        s.profiles[i] = Profile{80, 110};
    }
    s.general = GeneralTimes{10, 3, 25};
    s.activeProfile = 0;
    s.soundOn = true;
    return s;
}

SettingsError validateProfile(const Profile& profile) {
    if (!inRange(profile.preheatC, kPreheatCRange) || !inRange(profile.heatC, kHeatCRange)) {
        return SettingsError::OutOfRange;
    }
    if (profile.heatC < profile.preheatC) {
        return SettingsError::HeatBelowPreheat;
    }
    return SettingsError::None;
}

SettingsError validateGeneral(const GeneralTimes& general) {
    if (!inRange(general.preheatMin, kPreheatMinRange) ||
        !inRange(general.minHeatMin, kMinHeatMinRange) ||
        !inRange(general.maxHeatMin, kMaxHeatMinRange)) {
        return SettingsError::OutOfRange;
    }
    if (general.minHeatMin >= general.maxHeatMin) {
        return SettingsError::MinNotBelowMax;
    }
    return SettingsError::None;
}

SettingsError validateSettings(const Settings& settings) {
    if (settings.activeProfile >= kProfileCount) {
        return SettingsError::BadProfileIndex;
    }
    for (uint8_t i = 0; i < kProfileCount; ++i) {
        const SettingsError error = validateProfile(settings.profiles[i]);
        if (error != SettingsError::None) {
            return error;
        }
    }
    return validateGeneral(settings.general);
}

uint32_t crc32(const uint8_t* data, size_t length) {
    // CRC-32 IEEE 802.3 (poli 0xEDB88320, terbalik), bitwise: blob kecil.
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void encodeSettings(const Settings& settings, uint8_t (&out)[kSettingsBlobSize]) {
    size_t i = 0;
    out[i++] = kMagic0;
    out[i++] = kMagic1;
    out[i++] = kVersion;
    out[i++] = settings.activeProfile;
    out[i++] = settings.soundOn ? 1 : 0;
    for (uint8_t p = 0; p < kProfileCount; ++p) {
        out[i++] = settings.profiles[p].preheatC;
        out[i++] = settings.profiles[p].heatC;
    }
    out[i++] = settings.general.preheatMin;
    out[i++] = settings.general.minHeatMin;
    out[i++] = settings.general.maxHeatMin;
    const uint32_t crc = crc32(out, kCrcOffset);
    for (int b = 0; b < 4; ++b) {
        out[kCrcOffset + b] = static_cast<uint8_t>(crc >> (8 * b));  // little-endian
    }
}

bool decodeSettings(const uint8_t* data, size_t length, Settings& out) {
    if (data == nullptr || length != kSettingsBlobSize || data[0] != kMagic0 ||
        data[1] != kMagic1 || data[2] != kVersion) {
        return false;
    }
    uint32_t stored = 0;
    for (int b = 0; b < 4; ++b) {
        stored |= static_cast<uint32_t>(data[kCrcOffset + b]) << (8 * b);
    }
    if (stored != crc32(data, kCrcOffset) || data[4] > 1) {
        return false;
    }

    Settings s{};
    size_t i = 3;
    s.activeProfile = data[i++];
    s.soundOn = data[i++] == 1;
    for (uint8_t p = 0; p < kProfileCount; ++p) {
        s.profiles[p].preheatC = data[i++];
        s.profiles[p].heatC = data[i++];
    }
    s.general.preheatMin = data[i++];
    s.general.minHeatMin = data[i++];
    s.general.maxHeatMin = data[i++];
    if (validateSettings(s) != SettingsError::None) {
        return false;
    }
    out = s;
    return true;
}

}  // namespace hbcycle
