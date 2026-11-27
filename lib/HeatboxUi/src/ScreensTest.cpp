// Layar alur tes: TES BERJALAN (PRE-HEAT .. STOP), HASIL 7 halaman, ERROR

#include <app_config.h>

#include <cmath>
#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;
using hbcycle::Phase;

namespace {

struct Hash {
    uint32_t h = 2166136261u;
    void add(int32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (static_cast<uint32_t>(v) >> (8 * i)) & 0xFFu;
            h *= 16777619u;
        }
    }
    void addf(float v, float scale) { add(std::isfinite(v) ? static_cast<int32_t>(std::lround(v * scale)) : -999999); }
};

bool cancellable(Phase phase) {
    return phase == Phase::Idle || phase == Phase::Preheat || phase == Phase::Tare || phase == Phase::Load ||
           phase == Phase::Recover || phase == Phase::Heat;
}

// "T3/T4 angka" + satuan kecil di belakangnya, sejajar bawah
void numberWithUnit(Painter& p, int16_t x, int16_t y, const char* number, uint8_t size, const char* unit,
                    uint8_t unitSize) {
    const int16_t after = p.text(x, y, number, size, color::kText);
    p.text(static_cast<int16_t>(after + 6), static_cast<int16_t>(y + 8 * size - 8 * unitSize), unit, unitSize,
           color::kText);
}

// Metrik ringkas: garis atas 2 px, label T1, nilai T2
void metric(Painter& p, int16_t x, int16_t y, int16_t w, const char* label, const char* value) {
    p.hline(x, y, w, 2, color::kText);
    p.text(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 4), label, kT1, color::kSubText,
           static_cast<int16_t>(w - 4));
    p.text(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 15), value, kT2, color::kText,
           static_cast<int16_t>(w - 4));
}

float graphMaxMin(float minutes) {
    float m = std::ceil(minutes / 5.0f) * 5.0f;
    return m < 10.0f ? 10.0f : m;
}

// Blok status hasil (OK / OK ! / BELUM KERING / ERROR ...)
void statusBlock(Painter& p, int16_t x, int16_t y, const hbcycle::CycleResult& r) {
    uint8_t fill = color::kOk;
    uint8_t ink = color::kBg;
    Icon icon = Icon::Check;
    char text[20];
    const bool warn = (r.flags & (hbcycle::flag::kModelDeviates | hbcycle::flag::kWeightOutside |
                                  hbcycle::flag::kMethodsDisagree)) != 0;
    switch (r.status) {
        case hbcycle::Status::Ok:
            std::snprintf(text, sizeof(text), warn ? "OK !" : "OK");
            if (warn) {
                fill = color::kWarn;
                ink = color::kText;
                icon = Icon::Warn;
            }
            break;
        case hbcycle::Status::Timeout:
            std::snprintf(text, sizeof(text), "BELUM KERING");
            fill = color::kWarn;
            ink = color::kText;
            icon = Icon::Clock;
            break;
        default:
            std::snprintf(text, sizeof(text), "ERROR %s", hbcycle::reasonCode(r.reason) + 2);  // tanpa "E-"
            fill = color::kError;
            icon = Icon::Cross;
            break;
    }
    const int16_t w = static_cast<int16_t>(8 + kIconSmallW + 6 + Painter::textWidth(text, kT2) + 8);
    p.fill(x, y, w, 22, fill);
    p.icon(icon, static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 7), ink, false);
    p.text(static_cast<int16_t>(x + 6 + kIconSmallW + 6), static_cast<int16_t>(y + 3), text, kT2, ink);
}

const char* const kErrorTexts[][4] = {
    // baris 1, baris 2, baris 3, langkah operator
    {"PINTU DIBUKA SAAT", "PEMANASAN.", "AMBIL SAMPEL BARU.", "KEMBALI DAN ULANGI TES"},     // Lid
    {"TIMBANGAN/SENSOR", "SUHU TIDAK TERBACA.", "PEMANAS DIMATIKAN.", "HUBUNGI TEKNISI"},   // Sensor
    {"SUHU TIDAK WAJAR.", "PEMANAS DIMATIKAN.", "PERIKSA LAMPU/SSR.", "HUBUNGI TEKNISI"},    // Heater
    {"TIMBANGAN TIDAK", "STABIL UNTUK TARE.", "CEK ANGIN/GETARAN.", "KEMBALI DAN ULANGI TES"},  // Preheat
    {"BERAT SAMPEL DI", "LUAR 4-12 G ATAU", "TIDAK STABIL.", "KEMBALI DAN ULANGI TES"},     // Sample
    {"TES DIBATALKAN", "OPERATOR.", "SAMPEL TIDAK SAH.", "KEMBALI DAN ULANGI TES"},          // Cancel
    {"LISTRIK/ALAT MATI", "SAAT TES.", "HASIL TIDAK VALID.", "KEMBALI DAN ULANGI TES"},      // Reset
    {"DATA BERAT TERLALU", "BANYAK TERGANGGU.", "HASIL TIDAK ADA.", "KEMBALI DAN ULANGI TES"},  // Data
};

uint8_t errorTextIndex(hbcycle::Reason reason) {
    switch (reason) {
        case hbcycle::Reason::Lid: return 0;
        case hbcycle::Reason::Sensor: return 1;
        case hbcycle::Reason::Heater: return 2;
        case hbcycle::Reason::Preheat: return 3;
        case hbcycle::Reason::Sample: return 4;
        case hbcycle::Reason::Cancel: return 5;
        case hbcycle::Reason::Reset: return 6;
        default: return 7;
    }
}

}  // namespace

// =====================================================================
//  TES BERJALAN
// =====================================================================
void TestRunScreen::onEnter(UiContext& ctx) {
    enterMs_ = ctx.snap.nowMs;
    heatEnterMs_ = 0;
    lastPhase_ = ctx.snap.cycle.phase;
    page_ = 0;
}

void TestRunScreen::onRotate(UiContext& ctx, int8_t steps, bool) {
    if (ctx.snap.cycle.phase != Phase::Heat) return;
    const int next = static_cast<int>(page_) + steps;
    page_ = static_cast<uint8_t>(next < 0 ? 0 : (next > 3 ? 3 : next));
}

void TestRunScreen::onPress(UiContext& ctx) {
    if (ctx.snap.cycle.phase == Phase::Tare) {
        if (ctx.snap.cycle.lidClosed) {
            ctx.cmd.tareNow();
            ctx.cmd.playSound(Sound::Confirm, false);
        } else {
            ctx.cmd.playSound(Sound::Warning, false);
        }
    }
}

void TestRunScreen::onHold(UiContext& ctx) {
    if (cancellable(ctx.snap.cycle.phase)) {
        ctx.cmd.cancelTest();
    }
}

void TestRunScreen::tick(UiContext& ctx) {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    // TAMPILAN TENANG: angka berat sampel di layar tidak bergetar (data asli tetap)
    shownSampleMg_ = quiet_.update(v.sampleMg, ctx.advanced.display);
    if (v.phase != lastPhase_) {
        if (v.phase == Phase::Load) ctx.cmd.playSound(Sound::Warning, false);  // operator: masukkan sampel
        if (v.phase == Phase::Tare) ctx.cmd.playSound(Sound::HoldDone, false);
        if (v.phase == Phase::Heat) {
            ctx.cmd.playSound(Sound::Confirm, false);
            heatEnterMs_ = ctx.snap.nowMs;
        }
        lastPhase_ = v.phase;
    }
    if (v.phase == Phase::Done) {
        ctx.cmd.playSound(Sound::Finish, false);
        ctx.nav.replace(ScreenId::Result);
    } else if (v.phase == Phase::Failed) {
        ctx.cmd.playSound(Sound::Error, false);
        ctx.nav.replace(ScreenId::TestError);
    } else if (v.phase == Phase::Idle && !ctx.snap.startPending && (ctx.snap.nowMs - enterMs_) > 2500) {
        ctx.cmd.playSound(Sound::Error, false);  // MULAI ditolak (mis. pengaturan rusak)
        ctx.nav.goHome();
    }
}

HoldSpec TestRunScreen::holdSpec(const UiContext& ctx) const {
    if (cancellable(ctx.snap.cycle.phase)) {
        return {app::input::kHoldCancelTestMs, "BATAL"};
    }
    return {0, ""};
}

const char* TestRunScreen::hint(const UiContext& ctx) const {
    switch (ctx.snap.cycle.phase) {
        case Phase::Tare: return "TEKAN:TARE SEKARANG  TAHAN:BATAL";
        case Phase::Heat: return "PUTAR:HALAMAN  TAHAN 3 DTK:BATAL";
        case Phase::Stopping:
        case Phase::Done:
        case Phase::Failed: return "MOHON TUNGGU " HB_DOT " HASIL OTOMATIS";
        default: return "TAHAN 3 DTK:BATAL";
    }
}

uint32_t TestRunScreen::liveKey(const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    Hash k;
    k.add(static_cast<int32_t>(v.phase) | (page_ << 8) | (v.lidClosed ? 1 << 12 : 0) |
          (v.preheatWaiting ? 1 << 13 : 0) | (v.stable ? 1 << 14 : 0) | (v.weightCalm ? 1 << 15 : 0) |
          (v.phaseB ? 1 << 16 : 0) | (v.fitFailed ? 1 << 17 : 0) | (ctx.snap.startPending ? 1 << 18 : 0));
    k.add(static_cast<int32_t>(v.preheatElapsedSec + v.tareSec * 3 + v.stableSec * 7 + v.inBandSec * 11 +
                               v.recoverSec * 13 + v.heatSec * 17 + v.stoppingSec * 19));
    k.addf(v.tempC, 10.0f);
    k.addf(v.panMg, 10.0f);
    k.addf(shownSampleMg_, 1.0f);
    k.addf(v.currentMg, 10.0f);
    k.addf(v.rateMgMin, 10.0f);
    k.add(v.result.minuteCount);
    k.add(heatEnterMs_ != 0 && (ctx.snap.nowMs - heatEnterMs_) < 2500 ? 1 : 0);
    return k.h;
}

void TestRunScreen::draw(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    switch (v.phase) {
        case Phase::Tare: drawTare(p, ctx); break;
        case Phase::Load: drawLoad(p, ctx); break;
        case Phase::Recover: drawRecover(p, ctx, false); break;
        case Phase::Heat:
            if (heatEnterMs_ != 0 && (ctx.snap.nowMs - heatEnterMs_) < 2500) {
                drawRecover(p, ctx, true);  // 02-07b: W0 terkunci, memulai pemanasan
            } else {
                drawHeat(p, ctx);
            }
            break;
        case Phase::Stopping:
        case Phase::Done:
        case Phase::Failed: drawStop(p, ctx); break;
        default: drawPreheat(p, ctx); break;
    }
    const HoldSpec hold = holdSpec(ctx);
    if (ctx.snap.holdProgressMs > 0 && hold.ms > 0) {
        drawCancelDialog(p, ctx.snap.holdProgressMs, hold.ms);
    }
}

// ---- PRE-HEAT (02-04a/b/c) ----
void TestRunScreen::drawPreheat(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    drawHeader(p, ctx.snap, "PRE-HEAT");
    drawStepper(p, kStepperY, 0);
    const hbcycle::Profile& prof = ctx.settings.profiles[ctx.settings.activeProfile];
    const float target = v.phase == Phase::Idle ? static_cast<float>(prof.preheatC) : v.setpointC;

    p.text(kMargin, 76, "SUHU / TARGET", kT1, color::kSubText);
    char temp[12];
    char unit[16];
    if (std::isfinite(ctx.snap.tempC)) {
        std::snprintf(temp, sizeof(temp), "%.1f", static_cast<double>(ctx.snap.tempC));
    } else {
        std::snprintf(temp, sizeof(temp), "--.-");
    }
    std::snprintf(unit, sizeof(unit), "/ %d" HB_DEG "C", static_cast<int>(std::lround(target)));
    numberWithUnit(p, kMargin, 86, temp, kT4, unit, kT2);

    float fraction = 0.0f;
    const float start = std::isfinite(v.preheatStartC) ? v.preheatStartC : ctx.snap.tempC;
    if (std::isfinite(ctx.snap.tempC) && std::isfinite(start) && target - start > 1.0f) {
        fraction = (ctx.snap.tempC - start) / (target - start);
    }
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(fraction * 100.0f));
    drawProgress(p, kMargin, 126, kContentW, "SUHU", pct, fraction, color::kAccent);

    const uint32_t total = v.phase == Phase::Idle ? ctx.settings.general.preheatMin * 60u : v.preheatTotalSec;
    const uint32_t left = v.preheatElapsedSec >= total ? 0 : total - v.preheatElapsedSec;
    char remain[8];
    char length[8];
    formatMinSec(static_cast<float>(left), remain);
    formatMinSec(static_cast<float>(total), length);
    p.hline(kMargin, 156, 116, 2, color::kText);
    p.text(kMargin, 160, "SISA PRE-HEAT", kT1, color::kSubText);
    p.text(kMargin, 172, remain, kT2, color::kText);
    p.hline(130, 156, 120, 2, color::kText);
    p.text(132, 160, "LAMA", kT1, color::kSubText);
    p.text(132, 172, length, kT2, color::kText);

    if (v.preheatPaused || !ctx.snap.doorClosed) {
        drawBanner(p, BannerKind::Warn, "PINTU TERBUKA - TIMER DIJEDA");
    } else if (v.preheatWaiting) {
        drawBanner(p, BannerKind::Warn, "MENUNGGU SUHU " HB_PM "2" HB_DEG "C");
    }
}

// ---- TARE (02-05a/b) ----
void TestRunScreen::drawTare(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    drawHeader(p, ctx.snap, "TARE");
    drawStepper(p, kStepperY, 1);
    p.text(kMargin, 78, "BERAT CAWAN", kT1, color::kSubText);
    char weight[16];
    if (std::isfinite(v.panMg)) {
        std::snprintf(weight, sizeof(weight), "%.4f G", static_cast<double>(v.panMg / 1000.0f));
    } else {
        std::snprintf(weight, sizeof(weight), "--.---- G");
    }
    p.text(kMargin, 88, weight, kT3, color::kText);

    char drift[24];
    if (std::isfinite(v.driftMgMin)) {
        std::snprintf(drift, sizeof(drift), "GERAK %.1f MG/MNT", static_cast<double>(std::fabs(v.driftMgMin)));
    } else {
        std::snprintf(drift, sizeof(drift), "GERAK --  (MENGUKUR)");
    }
    p.text(kMargin, 124, drift, kT1, color::kText);
    const char* badge = v.stable ? "STABIL" : "MASIH BERGERAK";
    const int16_t badgeW = static_cast<int16_t>(3 + kIconSmallW + 4 + Painter::textWidth(badge, kT1) + 4);
    drawBadge(p, static_cast<int16_t>(kRight - badgeW), 121, v.stable ? BadgeKind::Ok : BadgeKind::Warn, badge);

    char held[8];
    formatMinSec(static_cast<float>(v.stableSec), held);
    drawProgress(p, kMargin, 146, kContentW, "TARE OTOMATIS (STABIL 1 MNT)", held,
                 static_cast<float>(v.stableSec) / 60.0f, color::kText);

    char waited[8];
    formatMinSec(static_cast<float>(v.tareSec), waited);
    char line[40];
    std::snprintf(line, sizeof(line), "MENUNGGU %s " HB_DOT " BATAS %u MNT", waited,
                  static_cast<unsigned>(v.tareLimitSec / 60u));
    p.text(kMargin, 178, line, kT1, color::kSubText);

    if (!v.lidClosed) {
        drawBanner(p, BannerKind::Warn, "TUTUP PINTU UNTUK TARE");
    } else if (v.tareLimitSec > 0 && v.tareSec + 120 >= v.tareLimitSec) {
        char text[32];
        std::snprintf(text, sizeof(text), "TARE BELUM STABIL %u MNT", static_cast<unsigned>(v.tareSec / 60u));
        drawBanner(p, BannerKind::Warn, text);
    }
}

// ---- MUAT SAMPEL (02-06a/b/c) ----
void TestRunScreen::drawLoad(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    drawHeader(p, ctx.snap, "MUAT SAMPEL");
    drawStepper(p, kStepperY, 2);

    static const char* const kSteps[3] = {"1 BUKA PINTU", "2 MASUKKAN 5-10 G", "3 TUTUP PINTU"};
    static const Icon kIcons[3] = {Icon::Door, Icon::Scale, Icon::Door};
    const uint8_t current = !v.lidOpenedAfterTare ? 0 : (!v.lidClosed ? 1 : 2);
    for (uint8_t i = 0; i < 3; ++i) {
        const int16_t y = static_cast<int16_t>(76 + i * 24);
        if (i == current) {
            p.frame(kMargin, y, kContentW, 22, 2, color::kText);
        } else {
            p.hline(kMargin, static_cast<int16_t>(y + 21), kContentW, 1, color::kGrid);
        }
        p.icon(kIcons[i], 12, static_cast<int16_t>(y + 7), color::kText, true);
        p.text(34, static_cast<int16_t>(y + 3), kSteps[i], kT2, color::kText);
    }

    p.frame(kMargin, 152, 100, 36, 2, color::kText);
    p.icon(Icon::Door, 12, 157, color::kText, false);
    p.text(24, 157, "PINTU", kT1, color::kText);
    p.text(12, 169, v.lidClosed ? "TUTUP" : "TERBUKA", kT2, color::kText, 92);

    p.frame(112, 152, 138, 36, 2, color::kText);
    p.text(118, 157, "BERAT " HB_DOT " 4-12 G", kT1, color::kText);
    char weight[16];
    if (std::isfinite(shownSampleMg_)) {
        std::snprintf(weight, sizeof(weight), "%.3f G", static_cast<double>(shownSampleMg_ / 1000.0f));
    } else {
        std::snprintf(weight, sizeof(weight), "-.--- G");
    }
    p.text(118, 169, weight, kT2, color::kText, 128);
    p.text(kMargin, 193, "PEMANAS MATI SAAT PINTU DIBUKA", kT1, color::kSubText);

    const float m = v.sampleMg;
    if (std::isfinite(m) && m >= 1000.0f) {
        if (m < 4000.0f) {
            drawBanner(p, BannerKind::Error, "TERLALU RINGAN (MIN 4 G)");
        } else if (m > 12000.0f) {
            drawBanner(p, BannerKind::Error, "TERLALU BERAT (MAKS 12 G)");
        } else if (m < 5000.0f || m > 10000.0f) {
            drawBanner(p, BannerKind::Warn, "DI LUAR ANJURAN 5-10 G");
        }
    }
}

// ---- PULIH & W0 (02-07a/b) ----
void TestRunScreen::drawRecover(Painter& p, const UiContext& ctx, bool locked) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    drawHeader(p, ctx.snap, "PULIH & W0");
    drawStepper(p, kStepperY, 2);
    const bool tempDone = locked || v.inBandSec >= 30;
    char line[24];
    std::snprintf(line, sizeof(line), "SUHU %u" HB_DEG "C", static_cast<unsigned>(v.result.preheatC));
    drawCheckBox(p, 12, 85, tempDone ? CheckMark::Done : CheckMark::Busy);
    p.text(28, 82, line, kT2, color::kText);
    char count[16];
    std::snprintf(count, sizeof(count), "%u/30 DTK", static_cast<unsigned>(locked ? 30 : (v.inBandSec > 30 ? 30 : v.inBandSec)));
    p.textRight(244, 86, count, kT1, color::kText);
    p.hline(12, 102, 232, 1, color::kGrid);
    drawCheckBox(p, 12, 113, (locked || v.weightCalm) ? CheckMark::Done : CheckMark::Busy);
    p.text(28, 110, "BERAT STABIL", kT2, color::kText);
    p.textRight(244, 114, (locked || v.weightCalm) ? "TENANG" : "BERGERAK", kT1, color::kText);
    p.hline(12, 130, 232, 1, color::kGrid);

    if (locked) {
        p.text(12, 146, "BERAT AWAL", kT1, color::kSubText);
        char w0[24];
        std::snprintf(w0, sizeof(w0), "W0 = %.4f G", static_cast<double>(v.result.w0Mg / 1000.0f));
        p.text(12, 156, w0, kT3, color::kText);
        p.text(12, 186, "MEMULAI PEMANASAN...", kT1, color::kText);
        return;
    }
    char waited[16];
    std::snprintf(waited, sizeof(waited), "%u DTK", static_cast<unsigned>(v.recoverSec));
    drawProgress(p, 12, 142, 232, "MENUNGGU STABIL", waited, static_cast<float>(v.inBandSec) / 30.0f,
                 color::kAccent);
    char weight[24];
    if (std::isfinite(shownSampleMg_)) {
        std::snprintf(weight, sizeof(weight), "SAMPEL %.3f G", static_cast<double>(shownSampleMg_ / 1000.0f));
        p.text(12, 176, weight, kT1, color::kText);
    }
}

// ---- PEMANASAN 4 halaman (02-08a/b/c/d) ----
void TestRunScreen::drawHeat(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    const hbcycle::CycleResult& r = v.result;
    static const char* const kTitles[4] = {"PEMANASAN", "GRAFIK", "BERAT", "GRAFIK SUHU"};
    drawHeader(p, ctx.snap, kTitles[page_]);
    drawStepper(p, kStepperY, 3);
    char a[24];
    char b[24];

    if (page_ == 0) {
        p.text(kMargin, 76, "WAKTU BERJALAN", kT1, color::kSubText);
        formatMinSec(static_cast<float>(v.heatSec), a);
        p.text(kMargin, 86, a, kT4, color::kText);
        std::snprintf(b, sizeof(b), "MAKS %u MNT", static_cast<unsigned>(r.maxHeatMin));
        p.textRight(kRight, 90, b, kT1, color::kSubText);
        std::snprintf(b, sizeof(b), "MIN %u MNT", static_cast<unsigned>(r.minHeatMin));
        p.textRight(kRight, 102, b, kT1, color::kSubText);

        if (std::isfinite(v.tempC)) {
            std::snprintf(a, sizeof(a), "%.1f/%u" HB_DEG "C", static_cast<double>(v.tempC), static_cast<unsigned>(r.heatC));
        } else {
            std::snprintf(a, sizeof(a), "--/%u" HB_DEG "C", static_cast<unsigned>(r.heatC));
        }
        metric(p, kMargin, 122, 160, "SUHU", a);
        char pct[12];
        formatPercent(v.lossPct, pct);
        std::snprintf(a, sizeof(a), "%s%%", pct);
        metric(p, 170, 122, 80, "SUSUT", a);
        if (std::isfinite(v.rateMgMin)) {
            std::snprintf(a, sizeof(a), "%.1f MG/MNT", static_cast<double>(v.rateMgMin));
        } else {
            std::snprintf(a, sizeof(a), "-- MG/MNT");
        }
        metric(p, kMargin, 158, 138, "LAJU " HB_DOT " AMBANG 4", a);

        p.hline(150, 158, 100, 2, color::kText);
        if (v.fitFailed) {
            p.text(152, 162, "FIT GAGAL", kT1, color::kText);
            p.text(152, 174, "LANJUT MAKS", kT1, color::kText);
        } else if (v.phaseB) {
            p.text(152, 162, "FASE B PREDIKSI", kT1, color::kText);
            p.text(152, 176, "STOP", kT1, color::kSubText);
            if (v.predictBeyondMax) {
                std::snprintf(a, sizeof(a), "MAKS");
            } else {
                const float left = r.tStopSec - static_cast<float>(v.heatSec);
                formatMinSec(left > 0.0f ? left : 0.0f, a);
            }
            p.textRight(kRight, 172, a, kT2, color::kText);
        } else {
            p.text(152, 162, "FASE A", kT1, color::kText);
            p.text(152, 174, "MENGUKUR", kT1, color::kText);
            p.text(152, 186, "STOP --:--", kT1, color::kSubText);
        }
    } else if (page_ == 1 || page_ == 3) {
        GraphSpec g{};
        g.result = &r;
        g.temperature = page_ == 3;
        const float stopMin = (v.phaseB && !v.predictBeyondMax) ? r.tStopSec / 60.0f : -1.0f;
        g.xMaxMin = graphMaxMin(std::fmax(static_cast<float>(v.heatSec) / 60.0f + 1.0f, stopMin));
        g.phaseAMin = r.phaseASec >= 0.0f ? r.phaseASec / 60.0f : -1.0f;
        g.stopMin = stopMin;
        g.model = v.phaseB;
        drawGraph(p, 10, 76, 236, page_ == 3 ? 108 : 116, g);
        if (page_ == 3) {
            std::snprintf(a, sizeof(a), "%+.1f", std::isfinite(r.overshootC) ? static_cast<double>(r.overshootC) : 0.0);
            char line[64];
            std::snprintf(line, sizeof(line), "DAYA %d%% " HB_DOT " OVERSHOOT %s" HB_DEG "C",
                          static_cast<int>(std::lround(v.dutyPct)), a);
            p.text(10, 187, line, kT1, color::kText);
        }
    } else {
        char w0[16];
        char now[16];
        char lost[16];
        char peak[16];
        char valid[16];
        std::snprintf(w0, sizeof(w0), "%.4f G", static_cast<double>(r.w0Mg / 1000.0f));
        if (std::isfinite(v.currentMg)) {
            std::snprintf(now, sizeof(now), "%.4f G", static_cast<double>(v.currentMg / 1000.0f));
            std::snprintf(lost, sizeof(lost), "%.1f MG", static_cast<double>(r.w0Mg - v.currentMg));
        } else {
            std::snprintf(now, sizeof(now), "-");
            std::snprintf(lost, sizeof(lost), "-");
        }
        std::snprintf(peak, sizeof(peak), "%.1f MG/M", static_cast<double>(r.peakRateMgMin));
        std::snprintf(valid, sizeof(valid), "%u / %u", static_cast<unsigned>(r.validMinutes),
                      static_cast<unsigned>(r.minuteCount));
        const DataRow rows[5] = {{"W0", w0}, {"SEKARANG", now}, {"HILANG", lost}, {"LAJU PUNCAK", peak},
                                 {"MENIT SAH", valid}};
        drawDataRows(p, 76, 22, rows, 5);
    }
    drawPageMarker(p, 196, page_, 4);
}

// ---- STOP (02-09) ----
void TestRunScreen::drawStop(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleView& v = ctx.snap.cycle;
    drawHeader(p, ctx.snap, "STOP");
    drawStepper(p, kStepperY, 4);
    p.frame(28, 88, 200, 80, 2, color::kText);
    p.icon(Icon::Clock, 36, 108, color::kText, true);
    p.text(58, 104, "MENGUNCI BERAT", kT2, color::kText);
    p.text(58, 124, "AKHIR...", kT2, color::kText);
    char wf[16];
    if (std::isfinite(v.result.wfMg)) {
        std::snprintf(wf, sizeof(wf), "%.4f G", static_cast<double>(v.result.wfMg / 1000.0f));
    } else {
        std::snprintf(wf, sizeof(wf), "-");
    }
    drawProgress(p, 28, 176, 200, "WF", wf, static_cast<float>(v.stoppingSec + 1) / 3.0f, color::kText);
}

// =====================================================================
//  HASIL (02-10) — juga dipakai RIWAYAT #id
// =====================================================================
bool resultNote(const hbcycle::CycleResult& r, char (&out)[40]) {
    if (r.flags & hbcycle::flag::kModelDeviates) {
        std::snprintf(out, sizeof(out), "MODEL MENYIMPANG %.1f MG", static_cast<double>(std::fabs(r.wfModelDevMg)));
    } else if (r.flags & hbcycle::flag::kSmallShrink) {
        std::snprintf(out, sizeof(out), "PENYUSUTAN SANGAT KECIL");
    } else if (r.flags & hbcycle::flag::kWeightOutside) {
        std::snprintf(out, sizeof(out), "SAMPEL DI LUAR ANJURAN 5-10 G");
    } else if (r.flags & hbcycle::flag::kMethodsDisagree) {
        std::snprintf(out, sizeof(out), "DUA METODE PREDIKSI BERBEDA");
    } else {
        return false;
    }
    return true;
}

void drawResultPage(Painter& p, const UiContext& ctx, const hbcycle::CycleResult& r, uint8_t page, int16_t top) {
    (void)ctx;
    char a[24];
    char b[24];
    char c[24];
    char d[24];
    char e[24];
    switch (page) {
        case 0: {
            p.text(kMargin, top, "KADAR AIR", kT2, color::kText);
            char number[12];
            if (r.hasMoisture) {
                formatPercent(r.moisturePct, number);
            } else {
                std::snprintf(number, sizeof(number), "--.--");
            }
            std::snprintf(a, sizeof(a), "%s %%", number);
            const int16_t numberEnd = p.text(kMargin, static_cast<int16_t>(top + 18), a, kT4, color::kText);
            if (r.hasMoisture && !r.validated) {
                p.fill(kMargin, static_cast<int16_t>(top + 32), static_cast<int16_t>(numberEnd - kMargin), 3,
                       color::kText);  // dicoret: tidak tervalidasi
            }
            p.text(kMargin, static_cast<int16_t>(top + 54), "BASIS BASAH", kT1, color::kSubText);
            statusBlock(p, kMargin, static_cast<int16_t>(top + 68), r);
            // Durasi di kanan baris "KADAR AIR" (blok status bisa selebar 180 px)
            formatMinSec(r.heatSec, a);
            p.textRight(kRight, top, a, kT2, color::kText);
            p.textRight(static_cast<int16_t>(kRight - Painter::textWidth(a, kT2) - 6), static_cast<int16_t>(top + 6),
                        "DURASI", kT1, color::kSubText);
            if (r.hasMoisture && !r.validated) {
                p.text(kMargin, static_cast<int16_t>(top + 96), "TIDAK TERVALIDASI", kT1, color::kText);
            }
            break;
        }
        case 1:
        case 5: {
            GraphSpec g{};
            g.result = &r;
            g.temperature = page == 5;
            const float stopMin = r.heatSec > 0.0f ? r.heatSec / 60.0f : -1.0f;
            g.xMaxMin = graphMaxMin(stopMin > 0.0f ? stopMin : 1.0f);
            g.phaseAMin = r.phaseASec >= 0.0f ? r.phaseASec / 60.0f : -1.0f;
            g.stopMin = stopMin;
            g.model = std::isfinite(r.tauMin);
            const int16_t h = static_cast<int16_t>((page == 5 ? 172 : 190) - top);
            drawGraph(p, 10, top, 236, h, g);
            if (page == 5) {
                std::snprintf(a, sizeof(a), "%.1f", std::isfinite(r.tempAvgC) ? static_cast<double>(r.tempAvgC) : 0.0);
                std::snprintf(b, sizeof(b), "%+.1f", std::isfinite(r.overshootC) ? static_cast<double>(r.overshootC) : 0.0);
                char line[80];
                std::snprintf(line, sizeof(line), "RATA %s" HB_DEG "C " HB_DOT " OVER %s" HB_DEG "C " HB_DOT " TARGET %u", a, b,
                              static_cast<unsigned>(r.heatC));
                p.text(10, 176, line, kT1, color::kText);
            }
            break;
        }
        case 2: {
            std::snprintf(a, sizeof(a), "%.4f G", static_cast<double>(r.w0Mg / 1000.0f));
            if (std::isfinite(r.wfMg)) {
                std::snprintf(b, sizeof(b), "%.4f G", static_cast<double>(r.wfMg / 1000.0f));
                std::snprintf(c, sizeof(c), "%.1f MG", static_cast<double>(r.w0Mg - r.wfMg));
            } else {
                std::snprintf(b, sizeof(b), "-");
                std::snprintf(c, sizeof(c), "-");
            }
            if (std::isfinite(r.dryBasisPct)) {
                std::snprintf(d, sizeof(d), "%.2f %%", static_cast<double>(r.dryBasisPct));
            } else {
                std::snprintf(d, sizeof(d), "-");
            }
            if (!std::isfinite(r.w0Mg)) std::snprintf(a, sizeof(a), "-");
            const DataRow rows[4] = {{"W0", a}, {"WF", b}, {"HILANG", c}, {"BASIS KERING", d}};
            drawDataRows(p, top, 26, rows, 4);
            break;
        }
        case 3: {
            formatMinSec(r.heatSec, a);
            formatMinSec(r.phaseASec, b);
            formatMinSec(r.tStarSec, c);
            formatMinSec(r.tStopSec, d);
            std::snprintf(e, sizeof(e), "%s", hbcycle::reasonCode(r.reason));
            const DataRow rows[5] = {{"DURASI", a}, {"FASE A", b}, {"PREDIKSI T*", c}, {"RENCANA STOP", d},
                                     {"ALASAN", e}};
            drawDataRows(p, top, 22, rows, 5);
            break;
        }
        case 4: {
            const bool fit = std::isfinite(r.tauMin);
            std::snprintf(a, sizeof(a), fit ? "%.1f MNT" : "-", static_cast<double>(r.tauMin));
            std::snprintf(b, sizeof(b), fit ? "%.3f" : "-", static_cast<double>(r.r2));
            std::snprintf(c, sizeof(c), fit ? "%.2f MG" : "-", static_cast<double>(r.rmsMg));
            std::snprintf(d, sizeof(d), fit ? "%.2f %%" : "-", static_cast<double>(r.asymptotePct));
            if (std::isfinite(r.wfModelDevMg)) {
                std::snprintf(e, sizeof(e), "%+.1f MG", static_cast<double>(r.wfModelDevMg));
            } else {
                std::snprintf(e, sizeof(e), "-");
            }
            const DataRow rows[5] = {{"TAU", a}, {"R\xFD", b}, {"RMS", c}, {"ASIMTOT", d}, {"WF - MODEL", e}};
            drawDataRows(p, top, 22, rows, 5);
            break;
        }
        default: {
            std::snprintf(a, sizeof(a), "#%04u", static_cast<unsigned>(r.id));
            char date[10];
            char clock[6];
            formatDate(r.startEpoch, date, clock);
            std::snprintf(b, sizeof(b), "%u " HB_DOT " %u/%u" HB_DEG "C", static_cast<unsigned>(r.profileIndex + 1),
                          static_cast<unsigned>(r.preheatC), static_cast<unsigned>(r.heatC));
            std::snprintf(d, sizeof(d), "FW %s", r.firmware);
            const DataRow rows[5] = {{"ID TES", a}, {"TANGGAL", date}, {"JAM", clock}, {"PROFIL", b}, {"VERSI", d}};
            drawDataRows(p, top, 22, rows, 5);
            break;
        }
    }
}

void ResultScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(page_) + steps;
    page_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= kResultPages ? kResultPages - 1 : next));
}

void ResultScreen::onHold(UiContext& ctx) {
    ctx.cmd.acknowledgeResult();
    ctx.nav.goHome();
}

HoldSpec ResultScreen::holdSpec(const UiContext&) const { return {app::input::kHoldActionMs, "BERANDA"}; }

void ResultScreen::draw(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleResult& r = ctx.snap.cycle.result;
    static const char* const kTitles[kResultPages] = {"", "GRAFIK", "HASIL BERAT", "WAKTU STATUS", "MODEL",
                                                       "HASIL SUHU", "INFO TES"};
    char title[16];
    if (page_ == 0) {
        std::snprintf(title, sizeof(title), "HASIL #%04u", static_cast<unsigned>(r.id));
    } else {
        std::snprintf(title, sizeof(title), "%s", kTitles[page_]);
    }
    drawHeader(p, ctx.snap, title);
    drawStepper(p, kStepperY, 4);
    drawResultPage(p, ctx, r, page_, 76);
    drawPageMarker(p, 192, page_, kResultPages);
    char note[40];
    if (page_ == 3 && resultNote(r, note)) {
        drawBanner(p, BannerKind::Warn, note);
    } else if (ctx.hot) {
        char text[32];
        std::snprintf(text, sizeof(text), "PANAS " HB_DOT " RUANG %ld" HB_DEG "C",
                      static_cast<long>(std::lround(ctx.snap.tempC)));
        drawBanner(p, BannerKind::Heat, text);
    }
}

// =====================================================================
//  ERROR tanpa hasil (02-11)
// =====================================================================
void TestErrorScreen::onPress(UiContext& ctx) {
    ctx.cmd.acknowledgeResult();
    ctx.nav.goHome();
}

void TestErrorScreen::onHold(UiContext& ctx) { onPress(ctx); }

HoldSpec TestErrorScreen::holdSpec(const UiContext&) const { return {app::input::kHoldActionMs, "BERANDA"}; }

void TestErrorScreen::draw(Painter& p, const UiContext& ctx) const {
    const hbcycle::CycleResult& r = ctx.snap.cycle.result;
    drawHeader(p, ctx.snap, "TES GAGAL");
    const char* const* texts = kErrorTexts[errorTextIndex(r.reason)];
    const char* const lines[3] = {texts[0], texts[1], texts[2]};
    drawErrorBlock(p, hbcycle::reasonCode(r.reason), lines, texts[3]);
    char id[12];
    std::snprintf(id, sizeof(id), "TES #%04u", static_cast<unsigned>(r.id));
    p.textRight(244, 72, id, kT1, color::kBg);
    if (ctx.hot) {
        char text[32];
        std::snprintf(text, sizeof(text), "PANAS " HB_DOT " RUANG %ld" HB_DEG "C",
                      static_cast<long>(std::lround(ctx.snap.tempC)));
        drawBanner(p, BannerKind::Heat, text);
    }
}

}  // namespace hbui
