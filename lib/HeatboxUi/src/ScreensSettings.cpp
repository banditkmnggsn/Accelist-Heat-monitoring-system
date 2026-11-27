// PENGATURAN (03-20.x): menu, SET PROFIL, edit nilai (profil, umum,
// parameter metode, kontrol pemanas, tampilan), reset default, INFO PERANGKAT

#include <app_config.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

constexpr uint8_t kDialogReset = 10;
constexpr uint8_t kDialogResetFields = 11;

enum MenuItem : uint8_t { SetProfile, General, SoundToggle, WifiClock, Info, Reset, Developer, ItemCount };
constexpr uint8_t kMenuRows = 6;
constexpr int16_t kRowH = 25;  // 6 baris dari y 48 sampai 198

const char* menuLabel(uint8_t i, const UiContext& ctx) {
    switch (i) {
        case SetProfile: return "SET PROFIL";
        case General: return "UMUM";
        case SoundToggle: return ctx.settings.soundOn ? "SUARA: NYALA" : "SUARA: MATI";
        case WifiClock: return "WIFI & JAM";
        case Info: return "INFO PERANGKAT";
        case Reset: return "RESET DEFAULT";
        default: return "OPSI PENGEMBANG";
    }
}

// Selama tes hanya INFO PERANGKAT yang boleh dibuka
bool menuLocked(uint8_t i, const UiContext& ctx) { return ctx.snap.testRunning && i != Info; }

const char* settingsErrorText(hbcycle::SettingsError error) {
    switch (error) {
        case hbcycle::SettingsError::HeatBelowPreheat: return "HEAT HARUS " HB_GE " PRE-HEAT";
        case hbcycle::SettingsError::MinNotBelowMax: return "MIN HARUS < MAKS";
        case hbcycle::SettingsError::OutOfRange: return "NILAI DI LUAR BATAS";
        case hbcycle::SettingsError::BadProfileIndex:
        case hbcycle::SettingsError::None: break;
    }
    return "NILAI DITOLAK";
}

const char* advancedErrorText(hbcycle::AdvancedError error) {
    switch (error) {
        case hbcycle::AdvancedError::TargetNotBelowPhaseA: return "TARGET HARUS < AMBANG FASE A";
        case hbcycle::AdvancedError::OutOfRange: return "NILAI DI LUAR BATAS";
        case hbcycle::AdvancedError::None: break;
    }
    return "NILAI DITOLAK";
}

// Simpan salinan pengaturan tim IT; nullptr = tersimpan
const char* saveAdvanced(UiContext& ctx, const hbcycle::AdvancedSettings& candidate, hbcycle::AdvancedError error) {
    if (error != hbcycle::AdvancedError::None) {
        return advancedErrorText(error);
    }
    if (!ctx.cmd.saveAdvanced(candidate)) {
        return "GAGAL MENYIMPAN (NVS)";
    }
    ctx.advanced = candidate;
    return nullptr;
}

const char* const kOnOff[2] = {"MATI", "NYALA"};

const FieldDef kProfileFields[2] = {
    {"SUHU PRE-HEAT " HB_DOT " 50-150", hbcycle::kPreheatCRange.min, hbcycle::kPreheatCRange.max, 1, 0, HB_DEG "C",
     nullptr},
    {"SUHU HEAT " HB_DOT " 80-150", hbcycle::kHeatCRange.min, hbcycle::kHeatCRange.max, 1, 0, HB_DEG "C", nullptr},
};

const FieldDef kGeneralFields[3] = {
    {"LAMA PRE-HEAT " HB_DOT " 1-60", hbcycle::kPreheatMinRange.min, hbcycle::kPreheatMinRange.max, 1, 0, " MNT",
     nullptr},
    {"WAKTU MIN " HB_DOT " 1-25", hbcycle::kMinHeatMinRange.min, hbcycle::kMinHeatMinRange.max, 1, 0, " MNT",
     nullptr},
    {"WAKTU MAKS " HB_DOT " 5-60", hbcycle::kMaxHeatMinRange.min, hbcycle::kMaxHeatMinRange.max, 1, 0, " MNT",
     nullptr},
};

const FieldDef kMethodFields[6] = {
    {"AMBANG FASE A", hbcycle::kPhaseARate10Range.min, hbcycle::kPhaseARate10Range.max, 1, 1, " MG/MNT", nullptr},
    {"KONFIRMASI", hbcycle::kPhaseAConfirmRange.min, hbcycle::kPhaseAConfirmRange.max, 1, 0, " MNT", nullptr},
    {"TARGET LAJU", hbcycle::kTargetRate10Range.min, hbcycle::kTargetRate10Range.max, 1, 1, " MG/MNT", nullptr},
    {"TAMBAHAN STOP", hbcycle::kStopExtraRange.min, hbcycle::kStopExtraRange.max, 1, 0, " MNT", nullptr},
    {"TARE STABIL", hbcycle::kTareStableRange.min, hbcycle::kTareStableRange.max, 5, 0, " DTK", nullptr},
    {"DARURAT: SP +", hbcycle::kOverTempRange.min, hbcycle::kOverTempRange.max, 1, 0, HB_DEG "C", nullptr},
};

const FieldDef kHeaterFields[4] = {
    {"KP (DUTY/" HB_DEG "C)", hbcycle::kKp1000Range.min, hbcycle::kKp1000Range.max, 1, 3, "", nullptr},
    {"TI INTEGRAL", hbcycle::kTiRange.min, hbcycle::kTiRange.max, 5, 0, " DTK", nullptr},
    {"PITA INTEGRAL " HB_PM, hbcycle::kBandRange.min, hbcycle::kBandRange.max, 1, 0, HB_DEG "C", nullptr},
    {"MAKS DAYA LAMPU", hbcycle::kMaxDutyRange.min, hbcycle::kMaxDutyRange.max, 5, 0, "%", nullptr},
};

const FieldDef kDisplayFields[2] = {
    {"TAMPILAN TENANG " HB_DOT " ANGKA BERAT DIAM", 0, 1, 1, 0, "", kOnOff},
    {"DEAD BAND " HB_DOT " 1-20", hbcycle::kDeadbandRange.min, hbcycle::kDeadbandRange.max, 1, 0, " MG", nullptr},
};

// Angka saja (tanpa satuan)
void formatNumber(const FieldDef& f, int32_t value, char (&out)[20]) {
    if (f.names != nullptr) {
        std::snprintf(out, sizeof(out), "%s", f.names[value - f.min]);
        return;
    }
    if (f.decimals == 0) {
        std::snprintf(out, sizeof(out), "%ld", static_cast<long>(value));
        return;
    }
    // 1-3 desimal (nilai menu selalu >= 0)
    static const char* const kFormats[3] = {"%ld.%01ld", "%ld.%02ld", "%ld.%03ld"};
    const uint8_t d = f.decimals > 3 ? 3 : f.decimals;
    const int32_t scale = d == 1 ? 10 : (d == 2 ? 100 : 1000);
    const long whole = static_cast<long>(value / scale) % 1000000L;
    const long frac = static_cast<long>(value % scale);
    std::snprintf(out, sizeof(out), kFormats[d - 1], whole, frac);
}

}  // namespace

void formatField(const FieldDef& f, int32_t value, char (&out)[20]) {
    char number[20];
    formatNumber(f, value, number);
    std::snprintf(out, sizeof(out), "%s%s", number, f.names != nullptr ? "" : f.unit);
}

// =====================================================================
//  PENGATURAN · MENU (03-20.1) + TERKUNCI (03-20.5) + RESET (03-20.4)
// =====================================================================
void SettingsMenuScreen::onRotate(UiContext&, int8_t steps, bool) { list_.rotate(steps, ItemCount, kMenuRows); }

void SettingsMenuScreen::onPress(UiContext& ctx) {
    if (menuLocked(list_.cursor, ctx)) {
        ctx.cmd.playSound(Sound::Warning, false);  // terkunci selama tes
        return;
    }
    switch (list_.cursor) {
        case SetProfile: ctx.nav.push(ScreenId::ProfileList); break;
        case General: ctx.nav.push(ScreenId::EditGeneral); break;
        case SoundToggle:
            ctx.settings.soundOn = !ctx.settings.soundOn;
            ctx.cmd.saveSettings(ctx.settings);
            ctx.cmd.playSound(Sound::Confirm, true);
            break;
        case WifiClock: ctx.nav.push(ScreenId::Wifi); break;
        case Info: ctx.nav.push(ScreenId::DeviceInfo); break;
        case Reset:
            ctx.nav.openDialog({kDialogReset, DialogKind::Confirm, "RESET DEFAULT?",
                                {"SEMUA PROFIL 80/110" HB_DEG "C,", "WAKTU 10/3/25 MENIT.",
                                 "KALIBRASI & WIFI TIDAK BERUBAH."},
                                nullptr, {"BATAL", "RESET"}, false});
            break;
        default: ctx.nav.push(ScreenId::DevGate); break;
    }
}

void SettingsMenuScreen::onDialogResult(UiContext& ctx, uint8_t id, bool accepted) {
    if (id != kDialogReset || !accepted) {
        return;
    }
    const bool soundOn = ctx.settings.soundOn;  // suara bukan parameter proses
    ctx.settings = hbcycle::defaultSettings();
    ctx.settings.soundOn = soundOn;
    ctx.cmd.saveSettings(ctx.settings);
    ctx.cmd.playSound(Sound::Confirm, false);
}

void SettingsMenuScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "PENGATURAN");
    drawMenuList(p, kContentY, kRowH, kMenuRows, list_, ItemCount, menuLabel, ctx, menuLocked);
    if (ctx.snap.testRunning) {
        drawBanner(p, BannerKind::Warn, "TERKUNCI SAAT TES");
    }
}

// =====================================================================
//  SET PROFIL: PROFIL 1-3
// =====================================================================
void ProfileListScreen::onEnter(UiContext& ctx) { cursor_ = ctx.nav.editProfileIndex() % hbcycle::kProfileCount; }

void ProfileListScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    const int last = hbcycle::kProfileCount - 1;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > last ? last : next));
}

void ProfileListScreen::onPress(UiContext& ctx) {
    ctx.nav.setEditProfileIndex(cursor_);
    ctx.nav.push(ScreenId::EditProfile);
}

void ProfileListScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "SET PROFIL");
    for (uint8_t i = 0; i < hbcycle::kProfileCount; ++i) {
        const int16_t y = static_cast<int16_t>(kContentY + i * 30);
        char name[12];
        std::snprintf(name, sizeof(name), "PROFIL %u", static_cast<unsigned>(i + 1));
        const uint8_t ink = drawMenuRow(p, y, 28, name, kT2, i == cursor_, Icon::Count, false);
        char temps[16];
        const hbcycle::Profile& pr = ctx.settings.profiles[i];
        std::snprintf(temps, sizeof(temps), "%u/%u" HB_DEG "C", static_cast<unsigned>(pr.preheatC),
                      static_cast<unsigned>(pr.heatC));
        p.textRight(244, static_cast<int16_t>(y + 10), temps, kT1, ink);
        if (i == ctx.settings.activeProfile) {
            p.text(136, static_cast<int16_t>(y + 10), HB_CHECK "AKTIF", kT1, ink);
        }
        if (i != cursor_) {
            p.hline(kMargin, static_cast<int16_t>(y + 28), kContentW, 1, color::kGrid);
        }
    }
    p.text(kMargin, 148, "PRE-HEAT = SUHU AWAL RUANG,", kT1, color::kSubText);
    p.text(kMargin, 160, "HEAT = SUHU PENGERINGAN.", kT1, color::kSubText);
    p.text(kMargin, 178, "PROFIL AKTIF DIPILIH DI MULAI TES.", kT1, color::kSubText);
}

// =====================================================================
//  EDIT NILAI (03-20.2a/b, 03-20.3a/b, 03-40.6b, 03-40.7)
// =====================================================================
void EditValuesScreen::onEnter(UiContext&) {
    cursor_ = 0;
    editing_ = false;
    rejected_ = nullptr;
}

void EditValuesScreen::onRotate(UiContext&, int8_t steps, bool fast) {
    if (!editing_) {
        const int next = static_cast<int>(cursor_) + steps;
        const int last = rowCount() - 1;
        cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > last ? last : next));
        return;
    }
    const FieldDef& f = field(cursor_);
    const int32_t delta = steps * f.step * (fast ? app::input::kAccelStep : 1);
    int32_t value = editValue_ + delta;
    value = value < f.min ? f.min : (value > f.max ? f.max : value);
    editValue_ = value;
    rejected_ = nullptr;  // nilai berubah: coba lagi
}

void EditValuesScreen::onPress(UiContext& ctx) {
    if (onResetRow()) {
        std::snprintf(dialogTitle_, sizeof(dialogTitle_), "%s?", resetLabel());
        ctx.nav.openDialog({kDialogResetFields, DialogKind::Confirm, dialogTitle_,
                            {"SEMUA NILAI DI LAYAR INI", "KEMBALI KE BAWAAN.", "BERLAKU UNTUK TES BERIKUTNYA."},
                            nullptr, {"BATAL", "RESET"}, false});
        return;
    }
    if (!editing_) {
        editing_ = true;
        editValue_ = valueOf(ctx, cursor_);
        rejected_ = nullptr;
        return;
    }
    const char* error = commit(ctx, cursor_, editValue_);
    if (error != nullptr) {
        rejected_ = error;  // nilai TIDAK disimpan sampai sah
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    editing_ = false;
    ctx.cmd.playSound(Sound::Confirm, false);
}

void EditValuesScreen::onHold(UiContext& ctx) {
    if (editing_) {
        editing_ = false;  // batal: nilai lama tetap
        rejected_ = nullptr;
        return;
    }
    ctx.nav.pop();
}

void EditValuesScreen::onDialogResult(UiContext& ctx, uint8_t id, bool accepted) {
    if (id == kDialogResetFields && accepted) {
        resetValues(ctx);
        ctx.cmd.playSound(Sound::Confirm, false);
    }
}

HoldSpec EditValuesScreen::holdSpec(const UiContext&) const {
    return {app::input::kHoldActionMs, editing_ ? "BATAL" : "KEMBALI"};
}

const char* EditValuesScreen::hint(const UiContext&) const {
    if (editing_) return "PUTAR:UBAH TEKAN:SIMPAN TAHAN:BATAL";
    return onResetRow() ? "PUTAR:PILIH TEKAN:RESET TAHAN:KEMBALI" : "PUTAR:PILIH TEKAN:EDIT TAHAN:KEMBALI";
}

void EditValuesScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, title(ctx));
    if (rowCount() <= 3) {
        drawBoxes(p, ctx);
    } else {
        drawRows(p, ctx);
    }
    if (rejected_ != nullptr) {
        drawBanner(p, BannerKind::Warn, rejected_);
    }
}

void EditValuesScreen::drawBoxes(Painter& p, const UiContext& ctx) const {
    const uint8_t n = fieldCount();
    constexpr int16_t kGap = 4;
    const int16_t area = note() != nullptr ? 130 : 150;
    const int16_t boxH = static_cast<int16_t>((area - (n - 1) * kGap) / n);
    for (uint8_t i = 0; i < n; ++i) {
        const FieldDef& f = field(i);
        const bool editingThis = editing_ && i == cursor_;
        char text[20];
        formatField(f, editingThis ? editValue_ : valueOf(ctx, i), text);
        BoxState state = BoxState::Normal;
        if (i == cursor_) {
            state = !editing_ ? BoxState::Selected : (rejected_ != nullptr ? BoxState::Rejected : BoxState::Editing);
        }
        drawValueBox(p, kMargin, static_cast<int16_t>(kContentY + i * (boxH + kGap)), kContentW, boxH, f.label, text,
                     valueSize(), state);
    }
    if (note() != nullptr) {
        p.text(kMargin, static_cast<int16_t>(kContentY + area + 6), note(), kT1, color::kSubText);
    }
}

void EditValuesScreen::drawRows(Painter& p, const UiContext& ctx) const {
    constexpr int16_t kH = 21;
    for (uint8_t i = 0; i < rowCount(); ++i) {
        const int16_t y = static_cast<int16_t>(kContentY + i * kH);
        const bool selected = i == cursor_;
        if (i == fieldCount()) {  // baris RESET
            drawMenuRow(p, y, kH - 2, resetLabel(), kT1, selected, Icon::Count, false);
            continue;
        }
        const FieldDef& f = field(i);
        const bool editingThis = editing_ && selected;
        const bool rejectedThis = editingThis && rejected_ != nullptr;
        if (rejectedThis) {
            p.fill(kMargin, y, kContentW, kH - 2, color::kWarn);
            p.frame(kMargin, y, kContentW, kH - 2, 2, color::kText);
        } else if (editingThis) {
            p.frame(kMargin, y, kContentW, kH - 2, 3, color::kAccent);
        } else if (selected) {
            p.frame(kMargin, y, kContentW, kH - 2, 2, color::kText);
        } else {
            p.hline(kMargin, static_cast<int16_t>(y + kH - 2), kContentW, 1, color::kGrid);
        }
        char label[28];
        std::snprintf(label, sizeof(label), "%s%s", selected && !editing_ ? "> " : "", f.label);
        p.text(12, static_cast<int16_t>(y + 6), label, kT1, color::kText, 112);

        // nilai: angka T2 + satuan T1, rata kanan; saat edit diapit ◄ ►
        char number[20];
        formatNumber(f, editingThis ? editValue_ : valueOf(ctx, i), number);
        const char* unit = f.names != nullptr ? "" : f.unit;
        int16_t right = 244;
        if (editingThis) {
            p.textRight(right, static_cast<int16_t>(y + 6), rejectedThis ? "!" : HB_RIGHT, kT1, color::kText);
            right = static_cast<int16_t>(right - 10);
        }
        p.textRight(right, static_cast<int16_t>(y + 6), unit, kT1, color::kText);
        right = static_cast<int16_t>(right - Painter::textWidth(unit, kT1) - (unit[0] != '\0' ? 1 : 0));
        p.textRight(right, static_cast<int16_t>(y + 2), number, kT2, color::kText);
        if (editingThis) {
            const int16_t left = static_cast<int16_t>(right - Painter::textWidth(number, kT2) - 10);
            p.text(left, static_cast<int16_t>(y + 6), HB_LEFT, kT1, color::kText);
        }
    }
}

// ---- profil ----
const char* EditProfileScreen::title(const UiContext& ctx) const {
    static const char* const kTitles[hbcycle::kProfileCount] = {"PROFIL 1", "PROFIL 2", "PROFIL 3"};
    return kTitles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
}

const FieldDef& EditProfileScreen::field(uint8_t index) const { return kProfileFields[index % 2]; }

int32_t EditProfileScreen::valueOf(const UiContext& ctx, uint8_t index) const {
    const hbcycle::Profile& profile = ctx.settings.profiles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
    return index == 0 ? profile.preheatC : profile.heatC;
}

const char* EditProfileScreen::commit(UiContext& ctx, uint8_t index, int32_t value) {
    hbcycle::Settings candidate = ctx.settings;
    hbcycle::Profile& profile = candidate.profiles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
    (index == 0 ? profile.preheatC : profile.heatC) = static_cast<uint8_t>(value);
    const hbcycle::SettingsError error = hbcycle::validateProfile(profile);
    if (error != hbcycle::SettingsError::None) {
        return settingsErrorText(error);
    }
    ctx.settings = candidate;
    ctx.cmd.saveSettings(ctx.settings);
    return nullptr;
}

// ---- umum ----
const FieldDef& EditGeneralScreen::field(uint8_t index) const { return kGeneralFields[index % 3]; }

int32_t EditGeneralScreen::valueOf(const UiContext& ctx, uint8_t index) const {
    const hbcycle::GeneralTimes& g = ctx.settings.general;
    return index == 0 ? g.preheatMin : (index == 1 ? g.minHeatMin : g.maxHeatMin);
}

const char* EditGeneralScreen::commit(UiContext& ctx, uint8_t index, int32_t value) {
    hbcycle::Settings candidate = ctx.settings;
    hbcycle::GeneralTimes& g = candidate.general;
    (index == 0 ? g.preheatMin : (index == 1 ? g.minHeatMin : g.maxHeatMin)) = static_cast<uint8_t>(value);
    const hbcycle::SettingsError error = hbcycle::validateGeneral(g);
    if (error != hbcycle::SettingsError::None) {
        return settingsErrorText(error);
    }
    ctx.settings = candidate;
    ctx.cmd.saveSettings(ctx.settings);
    return nullptr;
}

// ---- parameter metode ----
const FieldDef& EditMethodScreen::field(uint8_t index) const { return kMethodFields[index % 6]; }

int32_t EditMethodScreen::valueOf(const UiContext& ctx, uint8_t index) const {
    const hbcycle::MethodParams& m = ctx.advanced.method;
    const uint8_t values[6] = {m.phaseARate10, m.phaseAConfirm, m.targetRate10,
                               m.stopExtraMin, m.tareStableSec, m.overTempMarginC};
    return values[index % 6];
}

const char* EditMethodScreen::commit(UiContext& ctx, uint8_t index, int32_t value) {
    hbcycle::AdvancedSettings candidate = ctx.advanced;
    hbcycle::MethodParams& m = candidate.method;
    uint8_t* const targets[6] = {&m.phaseARate10, &m.phaseAConfirm, &m.targetRate10,
                                 &m.stopExtraMin, &m.tareStableSec, &m.overTempMarginC};
    *targets[index % 6] = static_cast<uint8_t>(value);
    return saveAdvanced(ctx, candidate, hbcycle::validateMethod(m));
}

void EditMethodScreen::resetValues(UiContext& ctx) {
    hbcycle::AdvancedSettings candidate = ctx.advanced;
    candidate.method = hbcycle::defaultAdvanced().method;
    saveAdvanced(ctx, candidate, hbcycle::AdvancedError::None);
}

// ---- kontrol pemanas ----
const FieldDef& EditHeaterScreen::field(uint8_t index) const { return kHeaterFields[index % 4]; }

int32_t EditHeaterScreen::valueOf(const UiContext& ctx, uint8_t index) const {
    const hbcycle::HeaterParams& h = ctx.advanced.heater;
    const int32_t values[4] = {h.kp1000, h.tiSec, h.bandC, h.maxDutyPct};
    return values[index % 4];
}

const char* EditHeaterScreen::commit(UiContext& ctx, uint8_t index, int32_t value) {
    hbcycle::AdvancedSettings candidate = ctx.advanced;
    hbcycle::HeaterParams& h = candidate.heater;
    switch (index % 4) {
        case 0: h.kp1000 = static_cast<uint16_t>(value); break;
        case 1: h.tiSec = static_cast<uint16_t>(value); break;
        case 2: h.bandC = static_cast<uint8_t>(value); break;
        default: h.maxDutyPct = static_cast<uint8_t>(value); break;
    }
    return saveAdvanced(ctx, candidate, hbcycle::validateHeater(h));
}

void EditHeaterScreen::resetValues(UiContext& ctx) {
    hbcycle::AdvancedSettings candidate = ctx.advanced;
    candidate.heater = hbcycle::defaultAdvanced().heater;
    saveAdvanced(ctx, candidate, hbcycle::AdvancedError::None);
}

// ---- tampilan ----
const FieldDef& EditDisplayScreen::field(uint8_t index) const { return kDisplayFields[index % 2]; }

int32_t EditDisplayScreen::valueOf(const UiContext& ctx, uint8_t index) const {
    const hbcycle::DisplayParams& d = ctx.advanced.display;
    return index == 0 ? (d.quiet ? 1 : 0) : d.deadbandMg;
}

const char* EditDisplayScreen::commit(UiContext& ctx, uint8_t index, int32_t value) {
    hbcycle::AdvancedSettings candidate = ctx.advanced;
    if (index == 0) {
        candidate.display.quiet = value != 0;
    } else {
        candidate.display.deadbandMg = static_cast<uint8_t>(value);
    }
    return saveAdvanced(ctx, candidate, hbcycle::validateDisplay(candidate.display));
}

// =====================================================================
//  INFO PERANGKAT (03-40.10)
// =====================================================================
uint32_t DeviceInfoScreen::liveKey(const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    uint32_t k = s.device.totalTests * 31u + static_cast<uint32_t>(s.net.ota) * 7u;
    for (const char* c = s.net.ip; *c != '\0'; ++c) k = k * 33u + static_cast<uint8_t>(*c);
    k = k * 33u + static_cast<uint32_t>(std::lround(s.scale.countsPerGram));
    return k;
}

void DeviceInfoScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    drawHeader(p, s, "INFO");
    char fw[16];
    std::snprintf(fw, sizeof(fw), "V%s", app::kFirmwareVersion);
    char total[12];
    std::snprintf(total, sizeof(total), "%lu", static_cast<unsigned long>(s.device.totalTests));
    const DataRow big[3] = {{"FIRMWARE", fw}, {"ID ALAT", s.device.id}, {"TOTAL TES", total}};
    drawDataRows(p, kContentY, 21, big, 3);

    char host[32];
    std::snprintf(host, sizeof(host), "%s.LOCAL", s.net.host);
    const char* ota = "MATI (OTA_PASS KOSONG)";
    switch (s.net.ota) {
        case OtaState::Waiting: ota = "MENUNGGU WIFI"; break;
        case OtaState::Ready: ota = "SIAP"; break;
        case OtaState::Updating: ota = "SEDANG UPDATE"; break;
        case OtaState::Failed: ota = "UPDATE TERAKHIR GAGAL"; break;
        case OtaState::Off: break;
    }
    char cal[28];
    std::snprintf(cal, sizeof(cal), "%.2f CNT/G%s", static_cast<double>(s.scale.countsPerGram),
                  s.scale.calibrated ? "" : " (PERKIRAAN)");
    const DataRow small[5] = {
        {"MAC", s.device.mac},
        {"ALAMAT IP", s.net.ip[0] != '\0' ? s.net.ip : "-"},
        {"NAMA OTA", s.net.host[0] != '\0' ? host : "-"},
        {"OTA (UPDATE WIFI)", ota},
        {"KALIBRASI", cal},
    };
    drawDataRows(p, static_cast<int16_t>(kContentY + 3 * 21), 18, small, 5, kT1);
}

}  // namespace hbui
