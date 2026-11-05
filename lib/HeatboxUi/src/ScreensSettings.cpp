// Layar Pengaturan (03-20.x): menu, edit profil, edit umum, reset default

#include <app_config.h>

#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

constexpr uint8_t kDialogReset = 10;

enum MenuItem : uint8_t { Profile1, Profile2, Profile3, General, SoundToggle, Reset, ItemCount };
constexpr int16_t kRowH = 25;  // 6 baris dari y 48 sampai 198

const char* errorText(hbcycle::SettingsError error) {
    switch (error) {
        case hbcycle::SettingsError::HeatBelowPreheat: return "HEAT HARUS " HB_GE " PRE-HEAT";
        case hbcycle::SettingsError::MinNotBelowMax: return "MIN HARUS < MAKS";
        case hbcycle::SettingsError::OutOfRange: return "NILAI DI LUAR BATAS";
        case hbcycle::SettingsError::BadProfileIndex:
        case hbcycle::SettingsError::None: break;
    }
    return "NILAI DITOLAK";
}

const FieldDef kProfileFields[2] = {
    {"SUHU PRE-HEAT " HB_DOT " 50-150", hbcycle::kPreheatCRange, HB_DEG "C"},
    {"SUHU HEAT " HB_DOT " 80-150", hbcycle::kHeatCRange, HB_DEG "C"},
};

const FieldDef kGeneralFields[3] = {
    {"LAMA PRE-HEAT " HB_DOT " 1-60", hbcycle::kPreheatMinRange, " MNT"},
    {"WAKTU MIN " HB_DOT " 1-25", hbcycle::kMinHeatMinRange, " MNT"},
    {"WAKTU MAKS " HB_DOT " 5-60", hbcycle::kMaxHeatMinRange, " MNT"},
};

}  // namespace

// =====================================================================
//  PENGATURAN · MENU (03-20.1) + TERKUNCI (03-20.5) + RESET (03-20.4)
// =====================================================================
void SettingsMenuScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= ItemCount ? ItemCount - 1 : next));
}

void SettingsMenuScreen::onPress(UiContext& ctx) {
    if (ctx.snap.testRunning) {
        ctx.cmd.playSound(Sound::Warning, false);  // terkunci selama tes
        return;
    }
    switch (cursor_) {
        case Profile1:
        case Profile2:
        case Profile3:
            ctx.nav.setEditProfileIndex(cursor_);
            ctx.nav.push(ScreenId::EditProfile);
            break;
        case General:
            ctx.nav.push(ScreenId::EditGeneral);
            break;
        case SoundToggle:
            ctx.settings.soundOn = !ctx.settings.soundOn;
            ctx.cmd.saveSettings(ctx.settings);
            ctx.cmd.playSound(Sound::Confirm, true);
            break;
        default:
            ctx.nav.openDialog({kDialogReset, DialogKind::Confirm, "RESET DEFAULT?",
                                {"SEMUA PROFIL 80/110" HB_DEG "C,", "WAKTU 10/3/25 MENIT.",
                                 "KALIBRASI TIDAK BERUBAH."},
                                nullptr, {"BATAL", "RESET"}, false});
            break;
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
    const bool locked = ctx.snap.testRunning;
    for (uint8_t i = 0; i < ItemCount; ++i) {
        char label[24];
        if (i <= Profile3) {
            std::snprintf(label, sizeof(label), "PROFIL %u", static_cast<unsigned>(i + 1));
        } else if (i == General) {
            std::snprintf(label, sizeof(label), "UMUM");
        } else if (i == SoundToggle) {
            std::snprintf(label, sizeof(label), "SUARA: %s", ctx.settings.soundOn ? "NYALA" : "MATI");
        } else {
            std::snprintf(label, sizeof(label), "RESET DEFAULT");
        }
        const int16_t y = static_cast<int16_t>(kContentY + i * kRowH);
        drawMenuRow(p, y, static_cast<int16_t>(kRowH - 2), label, kT2, i == cursor_, Icon::Count, locked);
        if (i != cursor_) {
            p.hline(kMargin, static_cast<int16_t>(y + kRowH - 2), kContentW, 1, color::kGrid);
        }
    }
    if (locked) {
        drawBanner(p, BannerKind::Warn, "TERKUNCI SAAT TES");
    }
}

// =====================================================================
//  EDIT NILAI (03-20.2a/b, 03-20.3a/b)
// =====================================================================
void EditValuesScreen::onEnter(UiContext&) {
    cursor_ = 0;
    editing_ = false;
    rejected_ = hbcycle::SettingsError::None;
}

void EditValuesScreen::onRotate(UiContext&, int8_t steps, bool fast) {
    if (!editing_) {
        const int next = static_cast<int>(cursor_) + steps;
        const int last = fieldCount() - 1;
        cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > last ? last : next));
        return;
    }
    const FieldDef& f = field(cursor_);
    const int delta = steps * (fast ? app::input::kAccelStep : 1);
    int value = static_cast<int>(editValue_) + delta;
    value = value < f.range.min ? f.range.min : (value > f.range.max ? f.range.max : value);
    editValue_ = static_cast<uint8_t>(value);
    rejected_ = hbcycle::SettingsError::None;  // nilai berubah: coba lagi
}

void EditValuesScreen::onPress(UiContext& ctx) {
    if (!editing_) {
        editing_ = true;
        editValue_ = valueOf(ctx.settings, cursor_, ctx);
        rejected_ = hbcycle::SettingsError::None;
        return;
    }
    hbcycle::Settings candidate = ctx.settings;
    const hbcycle::SettingsError error = apply(candidate, cursor_, editValue_, ctx);
    if (error != hbcycle::SettingsError::None) {
        rejected_ = error;  // nilai TIDAK disimpan sampai sah
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    ctx.settings = candidate;
    ctx.cmd.saveSettings(ctx.settings);
    editing_ = false;
    ctx.cmd.playSound(Sound::Confirm, false);
}

void EditValuesScreen::onHold(UiContext& ctx) {
    if (editing_) {
        editing_ = false;  // batal: nilai lama tetap
        rejected_ = hbcycle::SettingsError::None;
        return;
    }
    ctx.nav.pop();
}

HoldSpec EditValuesScreen::holdSpec(const UiContext&) const {
    return {app::input::kHoldActionMs, editing_ ? "BATAL" : "KEMBALI"};
}

const char* EditValuesScreen::hint(const UiContext&) const {
    return editing_ ? "PUTAR:UBAH TEKAN:SIMPAN TAHAN:BATAL" : "PUTAR:PILIH TEKAN:EDIT TAHAN:KEMBALI";
}

void EditValuesScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, title(ctx));
    const uint8_t n = fieldCount();
    constexpr int16_t kGap = 4;
    const int16_t boxH = static_cast<int16_t>((150 - (n - 1) * kGap) / n);
    for (uint8_t i = 0; i < n; ++i) {
        const FieldDef& f = field(i);
        const bool editingThis = editing_ && i == cursor_;
        const uint8_t value = editingThis ? editValue_ : valueOf(ctx.settings, i, ctx);
        char text[16];
        std::snprintf(text, sizeof(text), "%u%s", static_cast<unsigned>(value), f.unit);
        BoxState state = BoxState::Normal;
        if (i == cursor_) {
            state = !editing_ ? BoxState::Selected
                    : (rejected_ != hbcycle::SettingsError::None ? BoxState::Rejected : BoxState::Editing);
        }
        drawValueBox(p, kMargin, static_cast<int16_t>(kContentY + i * (boxH + kGap)), kContentW, boxH, f.label,
                     text, valueSize(), state);
    }
    if (rejected_ != hbcycle::SettingsError::None) {
        drawBanner(p, BannerKind::Warn, errorText(rejected_));
    }
}

// ---- profil ----
const char* EditProfileScreen::title(const UiContext& ctx) const {
    static const char* const kTitles[hbcycle::kProfileCount] = {"PROFIL 1", "PROFIL 2", "PROFIL 3"};
    return kTitles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
}

const FieldDef& EditProfileScreen::field(uint8_t index) const { return kProfileFields[index % 2]; }

uint8_t EditProfileScreen::valueOf(const hbcycle::Settings& s, uint8_t index, const UiContext& ctx) const {
    const hbcycle::Profile& profile = s.profiles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
    return index == 0 ? profile.preheatC : profile.heatC;
}

hbcycle::SettingsError EditProfileScreen::apply(hbcycle::Settings& candidate, uint8_t index, uint8_t value,
                                                const UiContext& ctx) const {
    hbcycle::Profile& profile = candidate.profiles[ctx.nav.editProfileIndex() % hbcycle::kProfileCount];
    (index == 0 ? profile.preheatC : profile.heatC) = value;
    return hbcycle::validateProfile(profile);
}

// ---- umum ----
const FieldDef& EditGeneralScreen::field(uint8_t index) const { return kGeneralFields[index % 3]; }

uint8_t EditGeneralScreen::valueOf(const hbcycle::Settings& s, uint8_t index, const UiContext&) const {
    return index == 0 ? s.general.preheatMin : (index == 1 ? s.general.minHeatMin : s.general.maxHeatMin);
}

hbcycle::SettingsError EditGeneralScreen::apply(hbcycle::Settings& candidate, uint8_t index, uint8_t value,
                                                const UiContext&) const {
    (index == 0 ? candidate.general.preheatMin
                : (index == 1 ? candidate.general.minHeatMin : candidate.general.maxHeatMin)) = value;
    return hbcycle::validateGeneral(candidate.general);
}

}  // namespace hbui
