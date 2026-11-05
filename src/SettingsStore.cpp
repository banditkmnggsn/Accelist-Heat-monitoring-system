#include "SettingsStore.h"

#include <Preferences.h>

namespace {

constexpr const char* kNamespace = "hbcfg";
constexpr const char* kKey = "cfg";

}  // namespace

hbcycle::Settings SettingsStore::load(bool& repaired) {
    hbcycle::Settings settings = hbcycle::defaultSettings();
    uint8_t blob[hbcycle::kSettingsBlobSize] = {};
    size_t length = 0;
    Preferences prefs;
    if (prefs.begin(kNamespace, true)) {
        if (prefs.isKey(kKey)) {
            length = prefs.getBytes(kKey, blob, sizeof(blob));
        }
        prefs.end();
    }
    repaired = !hbcycle::decodeSettings(blob, length, settings);
    if (repaired) {
        settings = hbcycle::defaultSettings();
        save(settings);
    }
    return settings;
}

bool SettingsStore::save(const hbcycle::Settings& settings) {
    uint8_t blob[hbcycle::kSettingsBlobSize];
    hbcycle::encodeSettings(settings, blob);
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    const bool ok = prefs.putBytes(kKey, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    return ok;
}
