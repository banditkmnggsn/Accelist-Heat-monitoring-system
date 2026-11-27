// RIWAYAT: daftar tes tersimpan (03-30.1) dan detail 7 halaman (03-30.2-8)

#include <app_config.h>

#include <cmath>
#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

void miniStatus(Painter& p, int16_t right, int16_t y, const hbcycle::HistoryEntry& e) {
    const char* text = "ERROR";
    BadgeKind kind = BadgeKind::Error;
    if (e.status == hbcycle::Status::Ok) {
        const bool warn = (e.flags & (hbcycle::flag::kModelDeviates | hbcycle::flag::kWeightOutside |
                                      hbcycle::flag::kMethodsDisagree)) != 0;
        text = warn ? "OK !" : "OK";
        kind = warn ? BadgeKind::Warn : BadgeKind::Ok;
    } else if (e.status == hbcycle::Status::Timeout) {
        text = "TIMEOUT";
        kind = BadgeKind::Warn;
    }
    const int16_t w = static_cast<int16_t>(3 + kIconSmallW + 4 + Painter::textWidth(text, kT1) + 4);
    drawBadge(p, static_cast<int16_t>(right - w), y, kind, text);
}

}  // namespace

// =====================================================================
//  DAFTAR
// =====================================================================
void HistoryScreen::onEnter(UiContext& ctx) {
    count_ = ctx.cmd.historyList(entries_, kMax);
    if (cursor_ >= count_) cursor_ = 0;
    if (top_ > cursor_) top_ = cursor_;
}

void HistoryScreen::onRotate(UiContext&, int8_t steps, bool) {
    if (count_ == 0) return;
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= count_ ? count_ - 1 : next));
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + kRows) top_ = static_cast<uint8_t>(cursor_ - kRows + 1);
}

void HistoryScreen::onPress(UiContext& ctx) {
    if (count_ == 0) return;
    ctx.nav.setSelectedTest(entries_[cursor_].id);
    ctx.nav.push(ScreenId::HistoryDetail);
}

const char* HistoryScreen::hint(const UiContext&) const {
    return count_ == 0 ? "TAHAN:KEMBALI" : "PUTAR:PILIH TEKAN:BUKA TAHAN:KEMBALI";
}

void HistoryScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "RIWAYAT");
    if (count_ == 0) {
        p.icon(Icon::History, 120, 90, color::kText, true);
        p.textCenter(128, 110, "BELUM ADA TES", kT2, color::kText);
        p.textCenter(128, 134, "HASIL TES AKAN MUNCUL DI SINI", kT1, color::kSubText);
        return;
    }
    for (uint8_t row = 0; row < kRows && top_ + row < count_; ++row) {
        const uint8_t i = static_cast<uint8_t>(top_ + row);
        const hbcycle::HistoryEntry& e = entries_[i];
        const int16_t y = static_cast<int16_t>(48 + row * 25);
        if (i == cursor_) {
            p.frame(kMargin, y, 238, 24, 2, color::kText);
        } else {
            p.hline(kMargin, static_cast<int16_t>(y + 24), 238, 1, color::kGrid);
        }
        char id[8];
        std::snprintf(id, sizeof(id), "#%04u", static_cast<unsigned>(e.id));
        p.text(11, static_cast<int16_t>(y + 4), id, kT1, color::kText);
        char date[10];
        char clock[6];
        formatDate(e.startEpoch, date, clock);
        date[5] = '\0';  // "31/10"
        char when[16];
        std::snprintf(when, sizeof(when), "%s %s", date, clock);
        p.text(11, static_cast<int16_t>(y + 14), when, kT1, color::kSubText);
        char value[12];
        if (e.hasMoisture) {
            formatPercent(e.moisturePct, value);
        } else {
            std::snprintf(value, sizeof(value), "%s", hbcycle::reasonCode(e.reason));
        }
        char shown[16];
        std::snprintf(shown, sizeof(shown), e.hasMoisture ? "%s%%" : "%s", value);
        p.text(82, static_cast<int16_t>(y + 4), shown, kT2, color::kText, 100);
        miniStatus(p, 240, static_cast<int16_t>(y + 5), e);
    }
    // Rel gulir
    if (count_ > kRows) {
        p.fill(247, 48, 3, 150, color::kGrid);
        const int16_t thumbH = static_cast<int16_t>(150 * kRows / count_);
        const int16_t thumbY = static_cast<int16_t>(48 + (150 - thumbH) * top_ / (count_ - kRows));
        p.fill(247, thumbY, 3, thumbH, color::kText);
    }
    char footer[32];
    std::snprintf(footer, sizeof(footer), "%u/%u TES TERSIMPAN", static_cast<unsigned>(ctx.snap.historyCount),
                  static_cast<unsigned>(ctx.snap.historyCapacity));
    p.text(kMargin, 202, footer, kT1, color::kSubText);
}

// =====================================================================
//  DETAIL
// =====================================================================
void HistoryDetailScreen::onEnter(UiContext& ctx) {
    page_ = 0;
    loaded_ = ctx.cmd.loadResult(ctx.nav.selectedTest(), result_);
}

void HistoryDetailScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(page_) + steps;
    page_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= kResultPages ? kResultPages - 1 : next));
}

HoldSpec HistoryDetailScreen::holdSpec(const UiContext&) const { return {app::input::kHoldActionMs, "KEMBALI"}; }

void HistoryDetailScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "RIWAYAT");
    char id[8];
    std::snprintf(id, sizeof(id), "#%04u", static_cast<unsigned>(ctx.nav.selectedTest()));
    p.text(116, kStatusY, id, kT1, color::kText);
    if (!loaded_) {
        p.textCenter(128, 110, "DATA TIDAK TERBACA", kT2, color::kText);
        return;
    }
    static const char* const kNames[kResultPages] = {"UTAMA", "GRAFIK", "BERAT", "WAKTU", "MODEL", "SUHU", "INFO"};
    char date[10];
    char clock[6];
    formatDate(result_.startEpoch, date, clock);
    char sub[40];
    std::snprintf(sub, sizeof(sub), "%s " HB_DOT " %s %s", kNames[page_], date, clock);
    p.text(kMargin, 48, sub, kT1, color::kSubText);
    drawResultPage(p, ctx, result_, page_, 62);
    drawPageMarker(p, 196, page_, kResultPages);
    char note[40];
    if (page_ == 3 && resultNote(result_, note)) {
        drawBanner(p, BannerKind::Warn, note);
    }
}

}  // namespace hbui
