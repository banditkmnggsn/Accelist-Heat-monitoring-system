// Layar alur utama: Boot, Error SENSOR, Beranda, Pilih Profil

#include <app_config.h>

#include <cmath>
#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

constexpr uint8_t kDialogDoorOpen = 1;
constexpr uint8_t kDialogSensor = 3;
constexpr uint8_t kDialogInterrupted = 4;

const char* const kBootLabels[5] = {"TIMBANGAN", "SENSOR SUHU", "PINTU", "WIFI", "JAM"};

bool sensorsReady(const UiSnapshot& snap) { return snap.scaleOk && snap.tempOk; }

// Teks gagal sensor untuk banner boot / layar error
const char* sensorFailText(const UiSnapshot& snap) {
    const bool scaleBad = snap.scaleCheck == CheckState::Fail || !snap.scaleOk;
    const bool tempBad = snap.tempCheck == CheckState::Fail || !snap.tempOk;
    if (scaleBad && tempBad) return "TIMBANGAN & SENSOR SUHU GAGAL";
    if (scaleBad) return "TIMBANGAN TIDAK TERBACA";
    return "SENSOR SUHU TIDAK TERBACA";
}

void profileTemps(const hbcycle::Profile& profile, const char* separator, char (&out)[24]) {
    std::snprintf(out, sizeof(out), "%u" HB_DEG "C%s%u" HB_DEG "C", static_cast<unsigned>(profile.preheatC),
                  separator, static_cast<unsigned>(profile.heatC));
}

}  // namespace

// =====================================================================
//  BOOT (02-01a/b/c): baris cek muncul berurutan dengan √ / X
// =====================================================================
void BootScreen::onEnter(UiContext& ctx) {
    startMs_ = ctx.snap.nowMs;
    lastRevealMs_ = ctx.snap.nowMs;
    revealed_ = 0;
    doneMs_ = 0;
    sensorFail_ = false;
    netFail_ = false;
}

CheckState BootScreen::rowState(const UiContext& ctx, uint8_t row) const {
    const UiSnapshot& snap = ctx.snap;
    const bool netTimedOut = (snap.nowMs - startMs_) >= app::ui::kBootNetBudgetMs;
    switch (row) {
        case 0:
            return snap.scaleCheck;
        case 1:
            return snap.tempCheck;
        case 2:
            return CheckState::Ok;  // pin selalu terbaca; keadaannya ditulis di kanan
        case 3:
            if (snap.wifi == WifiState::Connected) return CheckState::Ok;
            if (snap.wifi == WifiState::Disabled || snap.wifi == WifiState::NoCredentials || netTimedOut) {
                return CheckState::Fail;
            }
            return CheckState::Pending;
        default:
            if (snap.clockSynced) return CheckState::Ok;
            if (rowState(ctx, 3) == CheckState::Fail || netTimedOut) return CheckState::Fail;
            return CheckState::Pending;
    }
}

void BootScreen::tick(UiContext& ctx) {
    const uint32_t now = ctx.snap.nowMs;
    constexpr uint32_t kRevealGapMs = 300;
    if (revealed_ < kRows && (now - lastRevealMs_) >= kRevealGapMs &&
        (revealed_ == 0 || rowState(ctx, static_cast<uint8_t>(revealed_ - 1)) != CheckState::Pending)) {
        ++revealed_;
        lastRevealMs_ = now;
    }
    if (doneMs_ == 0 && revealed_ == kRows && rowState(ctx, kRows - 1) != CheckState::Pending) {
        doneMs_ = now;
        sensorFail_ = rowState(ctx, 0) == CheckState::Fail || rowState(ctx, 1) == CheckState::Fail;
        netFail_ = rowState(ctx, 3) == CheckState::Fail || rowState(ctx, 4) == CheckState::Fail;
        if (sensorFail_) {
            ctx.cmd.playSound(Sound::Error, false);
        }
    }
    if (doneMs_ != 0) {
        const uint32_t waitMs = sensorFail_ ? 2000 : (netFail_ ? 2500 : 800);
        if ((now - doneMs_) >= waitMs) {
            if (sensorFail_) {
                ctx.nav.replace(ScreenId::SensorError);
            } else {
                ctx.nav.goHome();
            }
        }
    }
}

void BootScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& snap = ctx.snap;
    p.text(kMargin, 20, "HEAT BOX", kT3, color::kText, kHeaderW);
    char version[40];
    std::snprintf(version, sizeof(version), "FW V%s " HB_DOT " MEMERIKSA SISTEM", app::kFirmwareVersion);
    p.text(18, 46, version, kT1, color::kSubText);

    for (uint8_t row = 0; row < revealed_; ++row) {
        const int16_t y = static_cast<int16_t>(60 + row * 22);
        const CheckState state = rowState(ctx, row);
        drawCheckBox(p, 18, static_cast<int16_t>(y + 4),
                     state == CheckState::Ok     ? CheckMark::Done
                     : state == CheckState::Fail ? CheckMark::Failed
                                                 : CheckMark::Busy);
        p.text(34, static_cast<int16_t>(y + 1), kBootLabels[row], kT2, color::kText, 140);

        char detail[16] = "";
        if (row == 2) {
            std::snprintf(detail, sizeof(detail), "%s", snap.doorClosed ? "TERTUTUP" : "TERBUKA");
        } else if (row == 3) {
            if (snap.wifi == WifiState::Connected) {
                std::snprintf(detail, sizeof(detail), "%d DBM", static_cast<int>(snap.rssi));
            } else if (snap.wifi == WifiState::Disabled) {
                std::snprintf(detail, sizeof(detail), "DIMATIKAN");
            } else if (snap.wifi == WifiState::NoCredentials) {
                std::snprintf(detail, sizeof(detail), "TANPA SSID");
            } else if (state == CheckState::Fail) {
                std::snprintf(detail, sizeof(detail), "PUTUS");
            }
        } else if (row == 4 && state == CheckState::Ok) {
            char clock[8];
            formatClock(snap, clock);
            std::snprintf(detail, sizeof(detail), "%s", clock);
        }
        p.textRight(238, static_cast<int16_t>(y + 5), detail, kT1, color::kSubText);
        p.hline(18, static_cast<int16_t>(y + 20), 220, 1, color::kGrid);
    }

    if (doneMs_ != 0 && sensorFail_) {
        drawBanner(p, BannerKind::Error, sensorFailText(snap));
    } else if (doneMs_ != 0 && netFail_) {
        p.fill(18, 172, 220, 28, color::kWarn);
        p.text(24, 176, "JAM BELUM SINKRON", kT1, color::kText);
        p.text(24, 188, "TES TETAP BISA JALAN", kT1, color::kText);
    }
}

// =====================================================================
//  ERROR SENSOR (template 02-11b)
// =====================================================================
void SensorErrorScreen::onEnter(UiContext&) { rechecking_ = false; }

void SensorErrorScreen::onPress(UiContext& ctx) {
    rechecking_ = true;
    ctx.cmd.recheckSensors();
}

void SensorErrorScreen::onHold(UiContext& ctx) { ctx.nav.goHome(); }

HoldSpec SensorErrorScreen::holdSpec(const UiContext&) const {
    return {app::input::kHoldActionMs, "BERANDA"};
}

void SensorErrorScreen::tick(UiContext& ctx) {
    const UiSnapshot& snap = ctx.snap;
    if (!rechecking_ || snap.scaleCheck == CheckState::Pending || snap.tempCheck == CheckState::Pending) {
        return;
    }
    rechecking_ = false;
    if (snap.scaleCheck == CheckState::Ok && snap.tempCheck == CheckState::Ok) {
        ctx.cmd.playSound(Sound::Confirm, false);
        ctx.nav.goHome();
    } else {
        ctx.cmd.playSound(Sound::Error, false);
    }
}

void SensorErrorScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& snap = ctx.snap;
    drawHeader(p, snap, "SENSOR GAGAL");
    p.fill(kMargin, 48, kContentW, 36, color::kError);
    p.icon(Icon::Cross, 12, 62, color::kBg, true);
    p.text(36, 52, "ERROR", kT2, color::kBg);
    p.text(36, 72, "E-SENSOR", kT1, color::kBg);

    const bool scaleBad = snap.scaleCheck == CheckState::Fail;
    const bool tempBad = snap.tempCheck == CheckState::Fail;
    const char* line1 = (scaleBad && tempBad) ? "TIMBANGAN &" : scaleBad ? "TIMBANGAN" : "SENSOR SUHU";
    const char* line2 = (scaleBad && tempBad) ? "SENSOR SUHU GAGAL." : "TIDAK TERBACA.";
    p.text(12, 96, line1, kT2, color::kText);
    p.text(12, 116, line2, kT2, color::kText);
    p.text(12, 136, "TES TIDAK BISA JALAN", kT2, color::kText);

    p.text(12, 166, "LANGKAH OPERATOR", kT1, color::kSubText);
    p.text(12, 178, "CEK KABEL, LALU TEKAN. KALAU", kT1, color::kText);
    p.text(12, 190, "TETAP GAGAL: HUBUNGI TEKNISI", kT1, color::kText);

    if (rechecking_) {
        drawBanner(p, BannerKind::Warn, "MEMERIKSA ULANG...");
    }
}

// =====================================================================
//  BERANDA (A, A2, 02-02b)
// =====================================================================
void HomeScreen::tick(UiContext& ctx) {
    // Tes terputus (02-01d): dialog sekali setelah boot
    const uint32_t id = ctx.snap.interruptedId;
    if (id != 0 && id != interruptShownId_) {
        interruptShownId_ = id;
        std::snprintf(interruptTitle_, sizeof(interruptTitle_), "TES #%04u TERPUTUS", static_cast<unsigned>(id));
        ctx.cmd.playSound(Sound::Warning, false);
        ctx.nav.openDialog({kDialogInterrupted, DialogKind::Info, interruptTitle_,
                            {"LISTRIK/ALAT MATI SAAT TES.", "HASIL TIDAK VALID, TERCATAT", "DI RIWAYAT SEBAGAI E-RESET."},
                            "TEKAN:OK", {nullptr, nullptr}, false});
    }
}

void HomeScreen::onDialogResult(UiContext& ctx, uint8_t id, bool) {
    if (id == kDialogInterrupted) {
        ctx.cmd.acknowledgeInterrupted();
    }
}

void HomeScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > 2 ? 2 : next));
}

void HomeScreen::onPress(UiContext& ctx) {
    static const ScreenId kTargets[3] = {ScreenId::ProfileSelect, ScreenId::History, ScreenId::SettingsMenu};
    ctx.nav.push(kTargets[cursor_]);
}


void HomeScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& snap = ctx.snap;
    drawHeader(p, snap, "HEAT BOX");
    if (sensorsReady(snap)) {
        drawBadge(p, kMargin, 46, BadgeKind::Ok, "SIAP");
    } else {
        drawBadge(p, kMargin, 46, BadgeKind::Error, "SENSOR GAGAL");
    }

    // Kartu profil aktif 6, 68, 244, 38
    const uint8_t active = ctx.settings.activeProfile;
    p.frame(kMargin, 66, kContentW, 40, 2, color::kText);
    p.text(11, 70, "PROFIL AKTIF", kT1, color::kSubText);
    char name[12];
    std::snprintf(name, sizeof(name), "PROFIL %u", static_cast<unsigned>(active + 1));
    p.text(11, 84, name, kT2, color::kText);
    char temps[24];
    profileTemps(ctx.settings.profiles[active], " " HB_ARROW " ", temps);
    p.textRight(244, 88, temps, kT1, color::kText);

    // Menu 6, 110, 244, 60
    static const char* const kItems[3] = {"MULAI TES", "RIWAYAT", "PENGATURAN"};
    static const Icon kIcons[3] = {Icon::Start, Icon::History, Icon::Gear};
    for (uint8_t i = 0; i < 3; ++i) {
        drawMenuRow(p, static_cast<int16_t>(110 + i * 20), 20, kItems[i], kT2, i == cursor_, kIcons[i], false);
    }

    // Hasil terakhir 6, 180, 244, 30
    p.hline(kMargin, 178, kContentW, 2, color::kGrid);
    p.text(kMargin, 182, "HASIL TERAKHIR", kT1, color::kSubText);
    if (!snap.hasLast) {
        p.text(kMargin, 191, "BELUM ADA TES", kT2, color::kText);
    } else {
        const hbcycle::HistoryEntry& e = snap.last;
        char line[24];
        const char* status = e.status == hbcycle::Status::Ok        ? "OK"
                             : e.status == hbcycle::Status::Timeout ? "TIMEOUT"
                                                                     : hbcycle::reasonCode(e.reason);
        if (e.hasMoisture) {
            std::snprintf(line, sizeof(line), "#%04u %.2f%% %s", static_cast<unsigned>(e.id),
                          static_cast<double>(e.moisturePct), status);
        } else {
            std::snprintf(line, sizeof(line), "#%04u %s", static_cast<unsigned>(e.id), status);
        }
        p.text(kMargin, 191, line, kT2, color::kText);
    }

    if (ctx.hot) {
        char text[32];
        std::snprintf(text, sizeof(text), "RUANG MASIH PANAS " HB_DOT " %ld" HB_DEG "C",
                      static_cast<long>(std::lround(snap.tempC)));
        drawBanner(p, BannerKind::Heat, text);
    }
}

// =====================================================================
//  PILIH PROFIL (02-03)
// =====================================================================
void ProfileSelectScreen::onEnter(UiContext& ctx) { cursor_ = ctx.settings.activeProfile; }

void ProfileSelectScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > kBackRow ? kBackRow : next));
}

void ProfileSelectScreen::makeActive(UiContext& ctx) {
    if (ctx.settings.activeProfile == cursor_) {
        return;
    }
    ctx.settings.activeProfile = cursor_;
    ctx.cmd.saveSettings(ctx.settings);
}

void ProfileSelectScreen::onPress(UiContext& ctx) {
    if (cursor_ == kBackRow) {
        ctx.nav.pop();
        return;
    }
    makeActive(ctx);
    ctx.cmd.playSound(Sound::Confirm, false);
}

void ProfileSelectScreen::onHold(UiContext& ctx) {
    if (cursor_ == kBackRow) {
        return;  // TAHAN di "< KEMBALI" tidak berbuat apa-apa
    }
    makeActive(ctx);
    const UiSnapshot& snap = ctx.snap;
    if (!sensorsReady(snap)) {
        ctx.cmd.playSound(Sound::Error, false);
        ctx.nav.openDialog({kDialogSensor, DialogKind::Info, "SENSOR GAGAL",
                            {"TIMBANGAN / SENSOR SUHU", "TIDAK TERBACA. TES TIDAK", "BISA DIMULAI."},
                            "TEKAN:OK", {nullptr, nullptr}, false});
        return;
    }
    if (!snap.doorClosed) {
        ctx.cmd.playSound(Sound::Warning, false);
        ctx.nav.openDialog({kDialogDoorOpen, DialogKind::Info, "PINTU TERBUKA",
                            {"TUTUP PINTU DULU,", "LALU TAHAN LAGI.", nullptr}, "TEKAN:OK", {nullptr, nullptr},
                            true});
        return;
    }
    // MULAI: siklus diproses task sensor; layar tes mengikuti snap.cycle
    ctx.cmd.startTest(ctx.settings);
    ctx.nav.replace(ScreenId::TestRun);
}

HoldSpec ProfileSelectScreen::holdSpec(const UiContext&) const {
    if (cursor_ == kBackRow) {
        return {0, ""};
    }
    return {app::input::kHoldActionMs, "MULAI"};
}

const char* ProfileSelectScreen::hint(const UiContext&) const {
    return (cursor_ == kBackRow) ? "PUTAR:PILIH  TEKAN:KEMBALI" : "PUTAR:PILIH TEKAN:AKTIFKAN TAHAN:MULAI";
}

void ProfileSelectScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& snap = ctx.snap;
    const hbcycle::Settings& s = ctx.settings;
    drawHeader(p, snap, "PILIH PROFIL");
    drawStepper(p, kStepperY, 0);

    // Daftar 4 baris x 18 px mulai y 76
    for (uint8_t row = 0; row <= kBackRow; ++row) {
        const int16_t y = static_cast<int16_t>(76 + row * 18);
        if (row == kBackRow) {
            drawMenuRow(p, y, 18, "< KEMBALI", kT2, cursor_ == row, Icon::Count, false);
            continue;
        }
        char name[12];
        std::snprintf(name, sizeof(name), "PROFIL %u", static_cast<unsigned>(row + 1));
        const uint8_t ink = drawMenuRow(p, y, 18, name, kT2, cursor_ == row, Icon::Count, false);
        char temps[24];
        profileTemps(s.profiles[row], "/", temps);
        p.textRight(246, static_cast<int16_t>(y + 5), temps, kT1, ink);
        if (row == s.activeProfile) {
            p.text(136, static_cast<int16_t>(y + 5), HB_CHECK "AKTIF", kT1, ink);
        }
    }

    char summary[44];
    std::snprintf(summary, sizeof(summary), "PRE-HEAT %uM " HB_DOT " MIN %uM " HB_DOT " MAKS %uM",
                  static_cast<unsigned>(s.general.preheatMin), static_cast<unsigned>(s.general.minHeatMin),
                  static_cast<unsigned>(s.general.maxHeatMin));
    p.text(kMargin, 152, summary, kT1, color::kSubText);

    // Kesiapan: cawan = pengingat operator (tidak ada sensornya), pintu = saklar
    drawCheckBox(p, kMargin, 166, CheckMark::Empty);
    p.text(22, 167, "CAWAN KOSONG TERPASANG", kT1, color::kText);
    drawCheckBox(p, kMargin, 184, snap.doorClosed ? CheckMark::Done : CheckMark::Failed);
    p.text(22, 185, snap.doorClosed ? "PINTU TERTUTUP" : "PINTU TERBUKA", kT1, color::kText);
}

}  // namespace hbui
