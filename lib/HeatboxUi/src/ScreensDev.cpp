// PENGATURAN > OPSI PENGEMBANG (tim IT; isi menu SERVIS lama 03-40):
// status sensor, timbangan, kalibrasi berat, cek timbangan, parameter
// metode, kontrol pemanas, tampilan, memori log

#include <app_config.h>

#include <cmath>
#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

const uint8_t kRefGrams[kRefCount] = {1, 2, 5, 10, 20, 50, 100};

namespace {

constexpr uint8_t kDialogWarnMethod = 20;
constexpr uint8_t kDialogWarnHeater = 21;
constexpr uint8_t kDialogScaleJob = 22;
constexpr uint8_t kDialogClearLogs = 23;

enum DevItem : uint8_t {
    SensorItem,
    ScaleItem,
    CalibrationItem,
    CheckItem,
    MethodItem,
    HeaterItem,
    DisplayItem,
    MemoryItem,
    DevItemCount,
};
constexpr uint8_t kDevRows = 6;

const char* devLabel(uint8_t i, const UiContext&) {
    static const char* const kLabels[DevItemCount] = {"STATUS SENSOR",    "TIMBANGAN",       "KALIBRASI BERAT",
                                                      "CEK TIMBANGAN",    "PARAMETER METODE", "KONTROL PEMANAS",
                                                      "TAMPILAN",         "MEMORI LOG"};
    return kLabels[i % DevItemCount];
}

bool devLocked(uint8_t, const UiContext& ctx) { return ctx.snap.testRunning; }

uint32_t hashf(uint32_t k, float v, float scale) {
    const uint32_t q = std::isfinite(v) ? static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * scale))) : 0xFFFFu;
    return k * 31u + q;
}

void formatRef(uint8_t index, char (&out)[8]) {
    std::snprintf(out, sizeof(out), "%u G", static_cast<unsigned>(kRefGrams[index % kRefCount]));
}

float tolMg(float refMg) { return app::ui::kCheckTolBaseMg + refMg * app::ui::kCheckTolFraction; }

// Baris berat langsung: "BERAT 0.0027 G" + status STABIL/BERGERAK
void drawLiveWeight(Painter& p, int16_t y, float grams, const ScaleInfo& s) {
    char g[16];
    formatGrams(grams, g);
    p.text(kMargin, y, "BERAT LANGSUNG", kT1, color::kSubText);
    p.text(kMargin, static_cast<int16_t>(y + 10), g, kT2, color::kText);
    p.textRight(kRight, static_cast<int16_t>(y + 14), scaleStatusText(s), kT1, color::kText);
}

const char* jobName(ScaleJob job) {
    switch (job) {
        case ScaleJob::Tare: return "TARE";
        case ScaleJob::SelfCal: return "SELF-CAL ADS1232";
        case ScaleJob::Reset: return "RESET ADS1232";
        case ScaleJob::Calibrate: return "KALIBRASI";
        case ScaleJob::None: break;
    }
    return "-";
}

}  // namespace

// =====================================================================
//  MASUK (dulu SERVIS · MASUK 03-40.0)
// =====================================================================
HoldSpec DevGateScreen::holdSpec(const UiContext&) const { return {app::input::kHoldDevMs, "MASUK"}; }

void DevGateScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "PENGEMBANG");
    p.textCenter(128, 66, "KHUSUS TIM IT", kT2, color::kText);
    p.textCenter(128, 100, "KALIBRASI, CEK TIMBANGAN, PARAMETER", kT1, color::kText);
    p.textCenter(128, 112, "METODE DAN KONTROL PEMANAS.", kT1, color::kText);
    p.fill(kMargin, 132, kContentW, 30, color::kWarn);
    p.icon(Icon::Warn, 12, 143, color::kText, false);
    p.text(26, 137, "SALAH UBAH = HASIL UKUR SALAH.", kT1, color::kText);
    p.text(26, 149, "TIM GUDANG CUKUP TEKAN UNTUK KEMBALI.", kT1, color::kText);
    p.textCenter(128, 178, "TAHAN ROTARY 5 DETIK UNTUK MASUK", kT1, color::kSubText);
}

// =====================================================================
//  MENU OPSI PENGEMBANG (dulu SERVIS · MENU 03-40.1a/b)
// =====================================================================
void DevMenuScreen::onRotate(UiContext&, int8_t steps, bool) { list_.rotate(steps, DevItemCount, kDevRows); }

void DevMenuScreen::onPress(UiContext& ctx) {
    if (devLocked(list_.cursor, ctx)) {
        ctx.cmd.playSound(Sound::Warning, false);
        return;
    }
    switch (list_.cursor) {
        case SensorItem: ctx.nav.push(ScreenId::SensorStatus); break;
        case ScaleItem: ctx.nav.push(ScreenId::ScaleActions); break;
        case CalibrationItem: ctx.nav.push(ScreenId::Calibration); break;
        case CheckItem: ctx.nav.push(ScreenId::ScaleCheck); break;
        case MethodItem:
        case HeaterItem:
            // 03-40.6a: peringatan sebelum masuk
            ctx.cmd.playSound(Sound::Warning, false);
            ctx.nav.openDialog({list_.cursor == MethodItem ? kDialogWarnMethod : kDialogWarnHeater,
                                DialogKind::Confirm, "PERINGATAN",
                                {list_.cursor == MethodItem ? "MENGUBAH CARA UKUR." : "MENGUBAH KONTROL LAMPU.",
                                 "BERLAKU UNTUK TES BERIKUTNYA.", "LANJUT?"},
                                nullptr, {"BATAL", "LANJUT"}, false});
            break;
        case DisplayItem: ctx.nav.push(ScreenId::EditDisplay); break;
        default: ctx.nav.push(ScreenId::LogMemory); break;
    }
}

void DevMenuScreen::onDialogResult(UiContext& ctx, uint8_t id, bool accepted) {
    if (!accepted) return;
    if (id == kDialogWarnMethod) ctx.nav.push(ScreenId::EditMethod);
    if (id == kDialogWarnHeater) ctx.nav.push(ScreenId::EditHeater);
}

void DevMenuScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "PENGEMBANG");
    drawMenuList(p, kContentY, 25, kDevRows, list_, DevItemCount, devLabel, ctx, devLocked);
    if (ctx.snap.testRunning) {
        drawBanner(p, BannerKind::Warn, "TERKUNCI SAAT TES");
    }
}

// =====================================================================
//  STATUS SENSOR (03-40.2) — data diperbarui langsung
// =====================================================================
uint32_t SensorStatusScreen::liveKey(const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    uint32_t k = hashf(0, s.scale.grams, 10000.0f);
    k = k * 31u + static_cast<uint32_t>(s.scale.raw);
    k = k * 31u + static_cast<uint32_t>(s.scale.status) + (s.scale.stable ? 64u : 0u);
    k = hashf(k, s.tempC, 100.0f);
    k = hashf(k, s.rtd.ohm, 100.0f);
    return k * 31u + s.rtd.fault + (s.rtd.spiOk ? 512u : 0u);
}

void SensorStatusScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    drawHeader(p, s, "SENSOR");
    char grams[16];
    formatGrams(s.scale.grams, grams);
    char raw[16];
    std::snprintf(raw, sizeof(raw), "%ld", static_cast<long>(s.scale.raw));
    char temp[16];
    char ohm[16];
    char fault[8];
    if (s.rtd.spiOk && std::isfinite(s.rtd.ohm)) {
        std::snprintf(temp, sizeof(temp), "%.2f" HB_DEG "C", static_cast<double>(s.tempC));
        std::snprintf(ohm, sizeof(ohm), "%.2f " HB_OHM, static_cast<double>(s.rtd.ohm));
        std::snprintf(fault, sizeof(fault), "0X%02X", static_cast<unsigned>(s.rtd.fault));
    } else {
        std::snprintf(temp, sizeof(temp), "SPI GAGAL");
        std::snprintf(ohm, sizeof(ohm), "-");
        std::snprintf(fault, sizeof(fault), "-");
    }
    const DataRow rows[7] = {
        {"BERAT ASLI", grams},
        {"RAW ADS1232", raw},
        {"STATUS TIMBANGAN", scaleStatusText(s.scale)},
        {"SUHU PT100", temp},
        {"RESISTANSI", ohm},
        {"FAULT MAX31865", fault},
        {"PINTU", s.doorClosed ? "TERTUTUP" : "TERBUKA"},
    };
    drawDataRows(p, kContentY, 21, rows, 7);
}

// =====================================================================
//  TIMBANGAN · AKSI (03-40.3a-e)
// =====================================================================
void ScaleActionsScreen::onEnter(UiContext&) {
    waiting_ = false;
    done_[0] = '\0';
    quiet_.reset();
}

void ScaleActionsScreen::tick(UiContext& ctx) {
    shown_ = quiet_.update(ctx.snap.scale.grams, ctx.advanced.display);
    if (!waiting_ || ctx.snap.scale.jobSeq == seq_) return;
    waiting_ = false;
    const JobResult r = ctx.snap.scale.jobResult;
    const char* name = jobName(ctx.snap.scale.job);
    std::snprintf(done_, sizeof(done_), "%s %s", name,
                  r == JobResult::Done ? "SELESAI" : (r == JobResult::Rejected ? "DITOLAK" : "GAGAL"));
    doneMs_ = ctx.snap.nowMs;
    ctx.cmd.playSound(r == JobResult::Done ? Sound::Finish : Sound::Error, false);
}

void ScaleActionsScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next > 2 ? 2 : next));
}

void ScaleActionsScreen::onPress(UiContext& ctx) {
    if (waiting_) {
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    static const char* const kWhy[3][2] = {
        {"TARE", "TIMBANGAN/CAWAN HARUS KOSONG."},
        {"SELF-CAL ADS1232", "OFFSET ADC DIKALIBRASI, LALU TARE."},
        {"RESET ADS1232", "ADC DIMATIKAN LALU DINYALAKAN."},
    };
    ctx.nav.openDialog({kDialogScaleJob, DialogKind::Confirm, "JALANKAN?", {kWhy[cursor_][0], kWhy[cursor_][1], nullptr},
                        nullptr, {"BATAL", "YA"}, false});
}

void ScaleActionsScreen::onDialogResult(UiContext& ctx, uint8_t id, bool accepted) {
    if (id != kDialogScaleJob || !accepted) return;
    static const ScaleJob kJobs[3] = {ScaleJob::Tare, ScaleJob::SelfCal, ScaleJob::Reset};
    seq_ = ctx.snap.scale.jobSeq;
    waiting_ = true;
    done_[0] = '\0';
    ctx.cmd.scaleJob(kJobs[cursor_], 0.0f);
}

uint32_t ScaleActionsScreen::liveKey(const UiContext& ctx) const {
    uint32_t k = hashf(0, shown_, 10000.0f);
    k = k * 31u + static_cast<uint32_t>(ctx.snap.scale.status) + (ctx.snap.scale.stable ? 64u : 0u);
    k = k * 31u + cursor_ + (waiting_ ? 8u : 0u) + (done_[0] != '\0' ? 16u : 0u);
    return k * 31u + ((ctx.snap.nowMs - doneMs_) < 4000 ? 1u : 0u);
}

void ScaleActionsScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "TIMBANGAN");
    static const char* const kItems[3] = {"TARE", "SELF-CAL ADS1232", "RESET ADS1232"};
    for (uint8_t i = 0; i < 3; ++i) {
        const int16_t y = static_cast<int16_t>(52 + i * 26);
        drawMenuRow(p, y, 24, kItems[i], kT2, i == cursor_, Icon::Count, false);
        if (i != cursor_) p.hline(kMargin, static_cast<int16_t>(y + 24), kContentW, 1, color::kGrid);
    }
    drawLiveWeight(p, 140, shown_, ctx.snap.scale);
    if (waiting_) {
        drawBanner(p, BannerKind::Warn, "SEDANG BERJALAN... JANGAN SENTUH");
    } else if (done_[0] != '\0' && (ctx.snap.nowMs - doneMs_) < 4000) {
        drawBadge(p, kMargin, 176,
                  ctx.snap.scale.jobResult == JobResult::Done ? BadgeKind::Ok : BadgeKind::Error, done_);
    }
}

// =====================================================================
//  KALIBRASI BERAT 1 titik (03-40.4, versi minimal keputusan 8.3)
// =====================================================================
void CalibrationScreen::onEnter(UiContext&) {
    step_ = Empty;
    busy_ = false;
    choice_ = 1;
}

void CalibrationScreen::tick(UiContext& ctx) {
    if (!busy_ || ctx.snap.scale.jobSeq == seq_) return;
    busy_ = false;
    const bool ok = ctx.snap.scale.jobResult == JobResult::Done;
    failedOnZero_ = step_ == Empty;
    if (step_ == Empty) {
        step_ = ok ? Reference : Failed;
    } else if (step_ == Load) {
        newCpg_ = ctx.snap.scale.countsPerGram;
        step_ = ok ? Result : Failed;
        choice_ = 1;
    }
    ctx.cmd.playSound(ok ? Sound::Finish : Sound::Error, false);
}

void CalibrationScreen::onRotate(UiContext&, int8_t steps, bool) {
    if (step_ == Reference) {
        const int next = static_cast<int>(refIndex_) + steps;
        refIndex_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= kRefCount ? kRefCount - 1 : next));
    } else if (step_ == Result) {
        choice_ = steps > 0 ? 1 : 0;
    }
}

void CalibrationScreen::onPress(UiContext& ctx) {
    if (busy_) return;
    const ScaleInfo& s = ctx.snap.scale;
    switch (step_) {
        case Empty:
        case Load:
            if (s.status != ScaleStatus::Ok || !s.stable) {
                ctx.cmd.playSound(Sound::Error, false);  // tunggu STABIL
                return;
            }
            seq_ = s.jobSeq;
            busy_ = true;
            busySinceMs_ = ctx.snap.nowMs;
            if (step_ == Empty) {
                ctx.cmd.scaleJob(ScaleJob::Tare, 0.0f);
            } else {
                oldCpg_ = s.countsPerGram;
                ctx.cmd.scaleJob(ScaleJob::Calibrate, static_cast<float>(kRefGrams[refIndex_]));
            }
            break;
        case Reference:
            step_ = Load;
            break;
        case Result:
            if (choice_ == 0) {
                ctx.cmd.scaleRestore(oldCpg_);  // BATAL: faktor lama kembali
            }
            ctx.cmd.playSound(Sound::Confirm, false);
            ctx.nav.pop();
            break;
        case Failed:
            step_ = Empty;
            break;
    }
}

void CalibrationScreen::onHold(UiContext& ctx) {
    if (step_ == Result) {
        ctx.cmd.scaleRestore(oldCpg_);  // keluar tanpa SIMPAN = batal
    }
    ctx.nav.pop();
}

HoldSpec CalibrationScreen::holdSpec(const UiContext&) const {
    if (busy_) return {0, ""};  // tunggu perekaman selesai
    return {app::input::kHoldActionMs, "BATAL"};
}

const char* CalibrationScreen::hint(const UiContext&) const {
    if (busy_) return "MEREKAM... JANGAN SENTUH TIMBANGAN";
    switch (step_) {
        case Empty: return "TEKAN:REKAM NOL  TAHAN:BATAL";
        case Reference: return "PUTAR:PILIH TEKAN:LANJUT TAHAN:BATAL";
        case Load: return "TEKAN:REKAM  TAHAN:BATAL";
        case Result: return "PUTAR:PILIH  TEKAN:OK  TAHAN:BATAL";
        case Failed: break;
    }
    return "TEKAN:ULANGI  TAHAN:KELUAR";
}

uint32_t CalibrationScreen::liveKey(const UiContext& ctx) const {
    uint32_t k = static_cast<uint32_t>(step_) | (busy_ ? 16u : 0u) | (static_cast<uint32_t>(refIndex_) << 5) |
                 (static_cast<uint32_t>(choice_) << 9);
    k = hashf(k, ctx.snap.scale.grams, 10000.0f);
    k = k * 31u + (ctx.snap.scale.stable ? 1u : 0u) + static_cast<uint32_t>(ctx.snap.scale.status) * 2u;
    return busy_ ? k * 31u + (ctx.snap.nowMs - busySinceMs_) / 250u : k;
}

void CalibrationScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& snap = ctx.snap;
    const ScaleInfo& s = snap.scale;
    drawHeader(p, snap, "KALIBRASI");
    static const uint8_t kStepNo[4] = {1, 2, 3, 4};
    char stepText[16];
    if (step_ == Failed) {
        std::snprintf(stepText, sizeof(stepText), "GAGAL");
    } else {
        std::snprintf(stepText, sizeof(stepText), "LANGKAH %u/4", static_cast<unsigned>(kStepNo[step_]));
    }
    p.text(kMargin, 46, stepText, kT1, color::kSubText);
    char ref[8];
    formatRef(refIndex_, ref);

    switch (step_) {
        case Empty:
            p.text(12, 62, "KOSONGKAN TIMBANGAN", kT2, color::kText);
            p.text(12, 82, "CAWAN BOLEH TETAP TERPASANG,", kT1, color::kText);
            p.text(12, 94, "ISINYA DIKOSONGKAN. PINTU DITUTUP.", kT1, color::kText);
            drawLiveWeight(p, 112, s.grams, s);
            break;
        case Reference:
            p.text(12, 62, "BERAT ACUAN", kT2, color::kText);
            drawValueBox(p, 12, 82, 232, 44, "ANAK TIMBANGAN", ref, kT3, BoxState::Editing);
            p.text(12, 134, "1" HB_DOT "2" HB_DOT "5" HB_DOT "10" HB_DOT "20" HB_DOT "50" HB_DOT "100 G", kT1,
                   color::kText);
            p.text(12, 148, "PAKAI BEBAN TERBESAR YANG ADA", kT1, color::kSubText);
            p.text(12, 160, "(SKALA LEBIH TEPAT)", kT1, color::kSubText);
            break;
        case Load: {
            char line[24];
            std::snprintf(line, sizeof(line), "LETAKKAN %s", ref);
            p.text(12, 62, line, kT2, color::kText);
            p.text(12, 82, "TARUH DI TENGAH CAWAN, TUTUP PINTU,", kT1, color::kText);
            p.text(12, 94, "TUNGGU STABIL LALU TEKAN.", kT1, color::kText);
            drawLiveWeight(p, 112, s.grams, s);
            break;
        }
        case Result: {
            const float refMg = static_cast<float>(kRefGrams[refIndex_]) * 1000.0f;
            const float diffMg = std::isfinite(s.grams) ? s.grams * 1000.0f - refMg : NAN;
            char oldF[20];
            char newF[20];
            char change[20];
            char diff[20];
            std::snprintf(oldF, sizeof(oldF), "%.2f", static_cast<double>(oldCpg_));
            std::snprintf(newF, sizeof(newF), "%.2f", static_cast<double>(newCpg_));
            const float pct = oldCpg_ != 0.0f ? (newCpg_ - oldCpg_) / std::fabs(oldCpg_) * 100.0f : NAN;
            std::snprintf(change, sizeof(change), "%+.2f %%", static_cast<double>(pct));
            if (std::isfinite(diffMg)) {
                std::snprintf(diff, sizeof(diff), "%+.1f MG", static_cast<double>(diffMg));
            } else {
                std::snprintf(diff, sizeof(diff), "-");
            }
            const DataRow rows[4] = {
                {"FAKTOR LAMA CNT/G", oldF}, {"FAKTOR BARU CNT/G", newF}, {"PERUBAHAN", change}, {"CEK SEKARANG", diff}};
            drawDataRows(p, 58, 22, rows, 4);
            static const char* const kChoices[2] = {"BATAL", "SIMPAN"};
            for (uint8_t i = 0; i < 2; ++i) {
                const int16_t bx = static_cast<int16_t>(kMargin + i * 124);
                p.frame(bx, 156, 120, 26, i == choice_ ? 2 : 1, i == choice_ ? color::kText : color::kGrid);
                p.textCenter(static_cast<int16_t>(bx + 60), 161, kChoices[i], kT2, color::kText);
            }
            break;
        }
        case Failed:
            drawBadge(p, 12, 62, BadgeKind::Error, "KALIBRASI GAGAL");
            p.text(12, 84, failedOnZero_ ? "REKAM NOL GAGAL:" : "BEBAN TERLALU KECIL", kT2, color::kText);
            p.text(12, 104, failedOnZero_ ? "TIMBANGAN SIBUK." : "ATAU TIMBANGAN SIBUK.", kT2, color::kText);
            p.text(12, 130, "FAKTOR LAMA TETAP DIPAKAI.", kT1, color::kText);
            p.text(12, 142, "CEK BEBAN TERPASANG, LALU ULANGI.", kT1, color::kText);
            break;
    }

    if (busy_) {
        const uint32_t elapsed = snap.nowMs - busySinceMs_;
        char left[12];
        const uint32_t remain = elapsed >= app::ui::kScaleJobMs ? 0 : (app::ui::kScaleJobMs - elapsed + 999) / 1000;
        std::snprintf(left, sizeof(left), "%lu DTK", static_cast<unsigned long>(remain));
        drawProgress(p, 12, 152, 232, step_ == Empty ? "MEREKAM NOL..." : "MEREKAM BEBAN...", left,
                     static_cast<float>(elapsed) / static_cast<float>(app::ui::kScaleJobMs), color::kAccent);
    } else if ((step_ == Empty || step_ == Load) && !(s.status == ScaleStatus::Ok && s.stable)) {
        drawBanner(p, BannerKind::Warn, "TUNGGU STABIL SEBELUM MENEKAN");
    } else if (step_ == Result && !s.calibrated) {
        drawBanner(p, BannerKind::Warn, "FAKTOR BELUM TERSIMPAN");
    }
}

// =====================================================================
//  CEK TIMBANGAN (03-40.5a/b)
// =====================================================================
void ScaleCheckScreen::onEnter(UiContext&) { taring_ = false; }

void ScaleCheckScreen::tick(UiContext& ctx) {
    if (taring_ && ctx.snap.scale.jobSeq != seq_) {
        taring_ = false;
        ctx.cmd.playSound(ctx.snap.scale.jobResult == JobResult::Done ? Sound::Confirm : Sound::Error, false);
    }
}

void ScaleCheckScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(refIndex_) + steps;
    refIndex_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= kRefCount ? kRefCount - 1 : next));
}

void ScaleCheckScreen::onPress(UiContext& ctx) {
    if (taring_) return;
    seq_ = ctx.snap.scale.jobSeq;
    taring_ = true;
    ctx.cmd.scaleJob(ScaleJob::Tare, 0.0f);  // NOLKAN sebelum anak timbangan diletakkan
}

uint32_t ScaleCheckScreen::liveKey(const UiContext& ctx) const {
    uint32_t k = hashf(refIndex_ + (taring_ ? 64u : 0u), ctx.snap.scale.grams, 10000.0f);
    return k * 31u + (ctx.snap.scale.stable ? 1u : 0u) + static_cast<uint32_t>(ctx.snap.scale.status) * 2u;
}

void ScaleCheckScreen::draw(Painter& p, const UiContext& ctx) const {
    const ScaleInfo& s = ctx.snap.scale;
    drawHeader(p, ctx.snap, "CEK BERAT");
    const float refMg = static_cast<float>(kRefGrams[refIndex_]) * 1000.0f;
    const float tol = tolMg(refMg);
    p.text(kMargin, 50, "1. KOSONGKAN, TEKAN = NOLKAN", kT1, color::kText);
    p.text(kMargin, 62, "2. LETAKKAN ANAK TIMBANGAN", kT1, color::kText);
    char tolText[28];
    std::snprintf(tolText, sizeof(tolText), "TOLERANSI " HB_PM "%.1f MG", static_cast<double>(tol));
    p.text(kMargin, 76, tolText, kT1, color::kSubText);

    char refNum[8];
    std::snprintf(refNum, sizeof(refNum), "%u", static_cast<unsigned>(kRefGrams[refIndex_]));
    drawValueBox(p, kMargin, 92, 92, 62, "ACUAN (G)", refNum, kT2, BoxState::Editing);

    p.frame(108, 92, 142, 62, 2, color::kText);
    p.text(114, 97, "TERBACA", kT1, color::kSubText);
    char g[16];
    formatGrams(s.grams, g);
    p.text(114, 112, g, kT2, color::kText, 132);
    const float diffMg = std::isfinite(s.grams) ? s.grams * 1000.0f - refMg : NAN;
    char diff[24];
    if (std::isfinite(diffMg)) {
        std::snprintf(diff, sizeof(diff), "SELISIH %+.1f MG", static_cast<double>(diffMg));
    } else {
        std::snprintf(diff, sizeof(diff), "SELISIH -");
    }
    p.text(114, 136, diff, kT1, color::kText, 132);

    if (taring_) {
        drawBadge(p, 108, 162, BadgeKind::Warn, "MENOLKAN...");
    } else if (s.status != ScaleStatus::Ok || !s.stable) {
        drawBadge(p, 108, 162, BadgeKind::Warn, "TUNGGU STABIL");
    } else if (!(s.grams * 1000.0f > refMg * 0.5f)) {
        drawBadge(p, 108, 162, BadgeKind::Warn, "LETAKKAN BEBAN");
    } else if (std::fabs(diffMg) <= tol) {
        drawBadge(p, 108, 162, BadgeKind::Ok, "OK");
    } else {
        drawBadge(p, 108, 162, BadgeKind::Error, "PERIKSA");
    }
    if (!s.calibrated) {
        drawBanner(p, BannerKind::Error, "BELUM DIKALIBRASI: FAKTOR PERKIRAAN");
    }
}

// =====================================================================
//  MEMORI LOG (03-40.8a/b)
// =====================================================================
void LogMemoryScreen::onPress(UiContext& ctx) {
    if (ctx.snap.historyCount == 0) {
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    static char line[32];
    std::snprintf(line, sizeof(line), "%u TES AKAN DIHAPUS.", static_cast<unsigned>(ctx.snap.historyCount));
    ctx.cmd.playSound(Sound::Warning, false);
    ctx.nav.openDialog({kDialogClearLogs, DialogKind::Confirm, "HAPUS SEMUA?",
                        {line, "TIDAK BISA DIBATALKAN.", "NOMOR TES TETAP BERLANJUT."}, nullptr, {"BATAL", "HAPUS"},
                        false});
}

void LogMemoryScreen::onDialogResult(UiContext& ctx, uint8_t id, bool accepted) {
    if (id != kDialogClearLogs || !accepted) return;
    failed_ = !ctx.cmd.clearLogs();
    ctx.cmd.playSound(failed_ ? Sound::Error : Sound::Confirm, false);
}

void LogMemoryScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    drawHeader(p, s, "MEMORI LOG");
    char count[16];
    std::snprintf(count, sizeof(count), "%u/%u TES", static_cast<unsigned>(s.historyCount),
                  static_cast<unsigned>(s.historyCapacity));
    p.text(kMargin, 54, count, kT3, color::kText);
    char free[40];
    std::snprintf(free, sizeof(free), "BEBAS %lu KB DARI %lu KB", static_cast<unsigned long>(s.device.logFreeKb),
                  static_cast<unsigned long>(s.device.logTotalKb));
    p.text(kMargin, 84, free, kT1, color::kText);
    p.text(kMargin, 96, "TES TERTUA DIHAPUS OTOMATIS SAAT PENUH.", kT1, color::kSubText);

    drawMenuRow(p, 116, 26, "HAPUS SEMUA LOG", kT2, true, Icon::Count, s.historyCount == 0);

    p.text(kMargin, 156, "AMBIL LOG LEWAT USB (SERIAL 115200):", kT1, color::kText);
    p.text(kMargin, 168, "l = DAFTAR TES, d <ID> = ISI TES", kT1, color::kText);
    if (failed_) {
        drawBanner(p, BannerKind::Error, "GAGAL MENGHAPUS SEBAGIAN LOG");
    }
}

}  // namespace hbui
