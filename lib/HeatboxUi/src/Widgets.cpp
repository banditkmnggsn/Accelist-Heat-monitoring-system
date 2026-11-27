#include "Widgets.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace hbui {

using namespace layout;

void formatClock(const UiSnapshot& snap, char (&out)[8]) {
    if (!snap.clockSynced) {
        std::snprintf(out, sizeof(out), "*--:--");
        return;
    }
    std::snprintf(out, sizeof(out), "%02u:%02u", static_cast<unsigned>(snap.hour),
                  static_cast<unsigned>(snap.minute));
}

void drawHeader(Painter& p, const UiSnapshot& snap, const char* title) {
    const bool wifiUp = snap.wifi == WifiState::Connected;
    p.icon(wifiUp ? Icon::Wifi : Icon::WifiX, kMargin, kStatusY, color::kText, false);
    char clock[8];
    formatClock(snap, clock);
    p.text(18, kStatusY, clock, kT1, color::kText, 48);
    if (!snap.doorClosed) {
        p.icon(Icon::Door, 72, kStatusY, color::kText, false);
    }
    if (snap.simulation) {
        p.text(88, kStatusY, "SIM", kT1, color::kError);
    }
    if (title != nullptr) {
        p.text(kMargin, kTitleY, title, kT2, color::kText, kHeaderW);
    }
}

void drawHint(Painter& p, const char* hint, uint32_t holdProgressMs, const HoldSpec& hold) {
    p.hline(kMargin, kHintRuleY, kContentW, 2, color::kGrid);
    const int16_t textY = kHintY + 2;
    if (holdProgressMs == 0 || hold.ms == 0) {
        p.text(kMargin, textY, hint, kT1, color::kText);
        return;
    }
    char label[24];
    std::snprintf(label, sizeof(label), "%s...", hold.label);
    const int16_t barX = static_cast<int16_t>(p.text(kMargin, textY, label, kT1, color::kText) + 4);
    const int16_t barW = static_cast<int16_t>(kRight - barX);
    p.frame(barX, kHintY, barW, kHintH, 1, color::kText);
    const uint32_t clamped = holdProgressMs > hold.ms ? hold.ms : holdProgressMs;
    const int16_t fillW = static_cast<int16_t>((static_cast<int32_t>(barW - 4) * clamped) / hold.ms);
    p.fill(static_cast<int16_t>(barX + 2), kHintY + 2, fillW, kHintH - 4, color::kAccent);
}

void drawBanner(Painter& p, BannerKind kind, const char* text) {
    uint8_t fill = color::kWarn;
    uint8_t ink = color::kText;
    Icon icon = Icon::Warn;
    if (kind == BannerKind::Heat) {
        fill = color::kHeat;
        icon = Icon::Heat;
    } else if (kind == BannerKind::Error) {
        fill = color::kError;
        ink = color::kBg;
        icon = Icon::Cross;
    }
    p.fill(kMargin, kBannerY, kContentW, kBannerH, fill);
    p.icon(icon, kMargin + 3, kBannerY + 2, ink, false);
    p.text(kMargin + 16, kBannerY + 2, text, kT1, ink);
}

uint8_t drawMenuRow(Painter& p, int16_t y, int16_t h, const char* label, uint8_t size, bool selected,
                 Icon iconOrNone, bool locked) {
    uint8_t ink = locked ? color::kSubText : color::kText;
    if (selected) {
        // Pilihan aktif = bingkai tebal 2 px + ">" (Theme.h)
        p.frame(kMargin, y, kContentW, h, 2, color::kText);
        ink = color::kText;
        p.text(kMargin + 4, static_cast<int16_t>(y + (h - 8 * size) / 2), ">", size, ink);
    }
    int16_t textX = static_cast<int16_t>(kMargin + 6 + Painter::charWidth(size));
    if (iconOrNone != Icon::Count) {
        p.icon(iconOrNone, textX, static_cast<int16_t>(y + (h - kIconLargeH) / 2), ink, true);
        textX = static_cast<int16_t>(textX + kIconLargeW + 6);
    }
    const int16_t lockW = locked ? 14 : 0;
    p.text(textX, static_cast<int16_t>(y + (h - 8 * size) / 2), label, size, ink,
           static_cast<int16_t>(kRight - textX - lockW));
    if (locked) {
        p.icon(Icon::Lock, kRight - 11, static_cast<int16_t>(y + (h - kIconSmallH) / 2), ink, false);
    }
    return ink;
}

void drawBadge(Painter& p, int16_t x, int16_t y, BadgeKind kind, const char* text) {
    uint8_t fill = color::kOk;
    uint8_t ink = color::kBg;
    Icon icon = Icon::Check;
    switch (kind) {
        case BadgeKind::Ok:
            break;
        case BadgeKind::Warn:
            fill = color::kWarn;
            ink = color::kText;
            icon = Icon::Warn;
            break;
        case BadgeKind::Error:
            fill = color::kError;
            icon = Icon::Cross;
            break;
        case BadgeKind::Heat:
            fill = color::kHeat;
            ink = color::kText;
            icon = Icon::Heat;
            break;
    }
    const int16_t w = static_cast<int16_t>(3 + kIconSmallW + 4 + Painter::textWidth(text, kT1) + 4);
    p.fill(x, y, w, 14, fill);
    p.icon(icon, x + 3, y + 3, ink, false);
    p.text(static_cast<int16_t>(x + 3 + kIconSmallW + 4), y + 3, text, kT1, ink);
}

void drawStepper(Painter& p, int16_t y, uint8_t currentStep) {
    static const char* const kLabels[5] = {"PRE", "TARE", "ISI", "PANAS", "HASIL"};
    char label[8];
    for (uint8_t i = 0; i < 5; ++i) {
        const int16_t x = static_cast<int16_t>(kMargin + i * (kStepBoxW + 1));
        uint8_t ink = color::kText;
        if (i < currentStep) {
            p.fill(x, y, kStepBoxW, kStepperH, color::kText);
            ink = color::kBg;
            std::snprintf(label, sizeof(label), HB_CHECK "%s", kLabels[i]);
        } else if (i == currentStep) {
            p.fill(x, y, kStepBoxW, kStepperH, color::kAccent);
            p.frame(x, y, kStepBoxW, kStepperH, 2, color::kText);
            ink = color::kBg;
            std::snprintf(label, sizeof(label), "%s", kLabels[i]);
        } else {
            p.frame(x, y, kStepBoxW, kStepperH, 1, color::kGrid);
            ink = color::kSubText;
            std::snprintf(label, sizeof(label), "%s", kLabels[i]);
        }
        p.textCenter(static_cast<int16_t>(x + kStepBoxW / 2), static_cast<int16_t>(y + (kStepperH - 8) / 2),
                     label, kT1, ink);
    }
}

void drawValueBox(Painter& p, int16_t x, int16_t y, int16_t w, int16_t h, const char* label,
                  const char* value, uint8_t valueSize, BoxState state) {
    switch (state) {
        case BoxState::Normal:
            p.frame(x, y, w, h, 1, color::kGrid);
            break;
        case BoxState::Selected:
            p.frame(x, y, w, h, 2, color::kText);
            break;
        case BoxState::Editing:
            p.frame(x, y, w, h, 3, color::kAccent);
            break;
        case BoxState::Rejected:
            p.fill(x, y, w, h, color::kWarn);
            p.frame(x, y, w, h, 2, color::kText);
            break;
    }
    const uint8_t labelInk = (state == BoxState::Normal) ? color::kSubText : color::kText;
    char shownLabel[40];
    std::snprintf(shownLabel, sizeof(shownLabel), "%s%s", state == BoxState::Selected ? "> " : "", label);
    p.text(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 5), shownLabel, kT1, labelInk,
           static_cast<int16_t>(w - 12));
    const int16_t valueY = static_cast<int16_t>(y + 5 + 8 + (h - 13 - 8 * valueSize) / 2);
    const char* marker = (state == BoxState::Editing) ? HB_LEFT " " HB_RIGHT
                         : (state == BoxState::Rejected) ? "!" : "";
    const int16_t markerW = Painter::textWidth(marker, kT2);
    p.text(static_cast<int16_t>(x + 6), valueY, value, valueSize, color::kText,
           static_cast<int16_t>(w - 18 - markerW));
    if (markerW > 0) {
        p.textRight(static_cast<int16_t>(x + w - 7), static_cast<int16_t>(valueY + (8 * valueSize - 16) / 2),
                    marker, kT2, color::kText);
    }
}

void drawCheckBox(Painter& p, int16_t x, int16_t y, CheckMark mark) {
    // Isi 6x6 di dalam bingkai 2 px; digambar per piksel supaya tidak sesak
    static const uint8_t kTick[6] = {0x01, 0x03, 0x26, 0x34, 0x18, 0x08};
    static const uint8_t kCross[6] = {0x21, 0x12, 0x0C, 0x0C, 0x12, 0x21};
    p.frame(x, y, 10, 10, 2, color::kText);
    const uint8_t* rows = nullptr;
    uint8_t ink = color::kText;
    switch (mark) {
        case CheckMark::Empty:
            return;
        case CheckMark::Busy:
            p.fill(x + 3, y + 4, 4, 2, color::kSecondary);
            return;
        case CheckMark::Done:
            rows = kTick;
            ink = color::kOk;
            break;
        case CheckMark::Failed:
            rows = kCross;
            ink = color::kError;
            break;
    }
    for (int16_t r = 0; r < 6; ++r) {
        for (int16_t b = 0; b < 6; ++b) {
            if (rows[r] & (0x20u >> b)) {
                p.fill(static_cast<int16_t>(x + 2 + b), static_cast<int16_t>(y + 2 + r), 1, 1, ink);
            }
        }
    }
}

void drawDialog(Painter& p, const DialogSpec& spec, uint8_t selectedOption) {
    p.fill(kDialogX, kDialogY, kDialogW, kDialogH, color::kBg);
    p.frame(kDialogX, kDialogY, kDialogW, kDialogH, 2, color::kText);
    p.text(kDialogX + 8, kDialogY + 8, spec.title, kT2, color::kText, kDialogW - 16);
    p.hline(kDialogX + 2, kDialogY + 28, kDialogW - 4, 1, color::kGrid);
    for (uint8_t i = 0; i < 3; ++i) {
        if (spec.lines[i] != nullptr) {
            p.text(kDialogX + 8, static_cast<int16_t>(kDialogY + 36 + i * 13), spec.lines[i], kT1,
                   color::kText, kDialogW - 16);
        }
    }
    const int16_t bottomY = kDialogY + kDialogH - 24;
    p.hline(kDialogX + 2, bottomY, kDialogW - 4, 1, color::kGrid);
    if (spec.kind == DialogKind::Info) {
        p.text(kDialogX + 8, bottomY + 8, spec.hint, kT1, color::kText, kDialogW - 16);
        return;
    }
    const int16_t buttonW = (kDialogW - 24) / 2;
    for (uint8_t i = 0; i < 2; ++i) {
        const int16_t bx = static_cast<int16_t>(kDialogX + 8 + i * (buttonW + 8));
        const int16_t by = bottomY + 5;
        if (i == selectedOption) {
            p.frame(bx, by, buttonW, 14, 2, color::kText);
        } else {
            p.frame(bx, by, buttonW, 14, 1, color::kGrid);
        }
        p.textCenter(static_cast<int16_t>(bx + buttonW / 2), by + 3, spec.options[i], kT1, color::kText);
    }
}

// =====================================================================
//  Komponen layar tes, hasil, riwayat
// =====================================================================
void formatMinSecTo(float seconds, char* out, size_t size) {
    if (!(seconds >= 0.0f)) {
        std::snprintf(out, size, "--:--");
        return;
    }
    unsigned total = static_cast<unsigned>(seconds);
    if (total > 99u * 60u + 59u) total = 99u * 60u + 59u;
    std::snprintf(out, size, "%02u:%02u", total / 60u, total % 60u);
}

void formatDate(uint32_t epoch, char (&date)[10], char (&clock)[6]) {
    if (epoch == 0) {
        std::snprintf(date, sizeof(date), "--/--/--");
        std::snprintf(clock, sizeof(clock), "--:--");
        return;
    }
    const time_t t = static_cast<time_t>(epoch) + 7 * 3600;  // WIB = UTC+7
    struct tm tmv;
    gmtime_r(&t, &tmv);
    std::snprintf(date, sizeof(date), "%02u/%02u/%02u", static_cast<unsigned>(tmv.tm_mday) % 100u,
                  static_cast<unsigned>(tmv.tm_mon + 1) % 100u, static_cast<unsigned>(tmv.tm_year) % 100u);
    std::snprintf(clock, sizeof(clock), "%02u:%02u", static_cast<unsigned>(tmv.tm_hour) % 100u,
                  static_cast<unsigned>(tmv.tm_min) % 100u);
}

void formatPercentTo(float value, char* out, size_t size) {
    if (!std::isfinite(value)) {
        std::snprintf(out, size, "--.--");
        return;
    }
    std::snprintf(out, size, "%.2f", static_cast<double>(value));
}

void drawPageMarker(Painter& p, int16_t y, uint8_t page, uint8_t count) {
    char text[8];
    std::snprintf(text, sizeof(text), "%u/%u", static_cast<unsigned>(page + 1), static_cast<unsigned>(count));
    const int16_t textW = Painter::textWidth(text, kT1);
    p.textRight(kRight, static_cast<int16_t>(y + 3), text, kT1, color::kText);
    int16_t x = static_cast<int16_t>(kRight - textW - 4 - count * 8 + 2);
    for (uint8_t i = 0; i < count; ++i, x = static_cast<int16_t>(x + 8)) {
        if (i == page) {
            p.fill(x, static_cast<int16_t>(y + 3), 6, 7, color::kText);
        } else {
            p.frame(x, static_cast<int16_t>(y + 3), 6, 7, 1, color::kText);
        }
    }
}

void drawProgress(Painter& p, int16_t x, int16_t y, int16_t w, const char* label, const char* value,
                  float fraction, uint8_t fill) {
    p.text(x, y, label, kT1, color::kText, static_cast<int16_t>(w - Painter::textWidth(value, kT1) - 6));
    p.textRight(static_cast<int16_t>(x + w), y, value, kT1, color::kText);
    p.frame(x, static_cast<int16_t>(y + 10), w, 12, 2, color::kText);
    if (!(fraction > 0.0f)) return;
    if (fraction > 1.0f) fraction = 1.0f;
    const int16_t fw = static_cast<int16_t>(static_cast<float>(w - 4) * fraction);
    p.fill(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 12), fw, 8, fill);
}

void drawDataRows(Painter& p, int16_t y, int16_t rowH, const DataRow* rows, uint8_t count, uint8_t valueSize) {
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t ry = static_cast<int16_t>(y + i * rowH);
        p.text(kMargin, static_cast<int16_t>(ry + (rowH - 8) / 2 - 1), rows[i].label, kT1, color::kText, 110);
        p.textRight(kRight, static_cast<int16_t>(ry + (rowH - 8 * valueSize) / 2 - 1), rows[i].value, valueSize,
                    color::kText);
        p.hline(kMargin, static_cast<int16_t>(ry + rowH - 1), kContentW, 1, color::kGrid);
    }
}

void drawErrorBlock(Painter& p, const char* code, const char* const (&lines)[3], const char* step) {
    p.fill(kMargin, 48, kContentW, 36, color::kError);
    p.icon(Icon::Cross, 12, 62, color::kBg, true);
    p.text(36, 52, "ERROR", kT2, color::kBg);
    p.text(36, 72, code, kT1, color::kBg);
    for (uint8_t i = 0; i < 3; ++i) {
        if (lines[i] != nullptr) {
            p.text(12, static_cast<int16_t>(96 + i * 20), lines[i], kT2, color::kText);
        }
    }
    p.text(12, 166, "LANGKAH OPERATOR", kT1, color::kSubText);
    p.text(12, 178, step, kT1, color::kText);
}

namespace {

void dashedV(Painter& p, int16_t x, int16_t y0, int16_t y1, uint8_t c) {
    for (int16_t y = y0; y < y1; y = static_cast<int16_t>(y + 5)) {
        p.fill(x, y, 1, (y1 - y) < 3 ? static_cast<int16_t>(y1 - y) : 3, c);
    }
}

void dashedH(Painter& p, int16_t x0, int16_t x1, int16_t y, uint8_t c) {
    for (int16_t x = x0; x < x1; x = static_cast<int16_t>(x + 6)) {
        p.fill(x, y, (x1 - x) < 3 ? static_cast<int16_t>(x1 - x) : 3, 1, c);
    }
}

}  // namespace

void drawGraph(Painter& p, int16_t x, int16_t y, int16_t w, int16_t h, const GraphSpec& spec) {
    const hbcycle::CycleResult& r = *spec.result;
    const float w0 = r.w0Mg;
    const uint8_t n = r.minuteCount;

    // Nilai titik per menit
    auto valueAt = [&](uint8_t k) -> float {
        if (spec.temperature) return r.minutes[k].tempC;
        const float m = r.minutes[k].massMg;
        return (std::isfinite(m) && w0 > 0.0f) ? (w0 - m) / w0 * 100.0f : NAN;
    };
    float lo = spec.temperature ? static_cast<float>(r.heatC) - 30.0f : 0.0f;
    float hi = spec.temperature ? static_cast<float>(r.heatC) + 4.0f : 1.0f;
    for (uint8_t k = 0; k < n; ++k) {
        const float v = valueAt(k);
        if (!std::isfinite(v)) continue;
        if (v > hi) hi = v;
        if (v < lo) lo = v;
    }
    if (spec.temperature) {
        lo = std::floor(lo / 10.0f) * 10.0f;
        hi = std::ceil(hi / 2.0f) * 2.0f;
    } else {
        hi = std::ceil(hi);
    }
    if (hi - lo < 1.0f) hi = lo + 1.0f;

    // Area plot
    const int16_t px = static_cast<int16_t>(x + 22);
    const int16_t py = static_cast<int16_t>(y + 12);
    const int16_t pw = static_cast<int16_t>(w - 26);
    const int16_t ph = static_cast<int16_t>(h - 24);
    const float xMax = spec.xMaxMin > 1.0f ? spec.xMaxMin : 1.0f;
    auto X = [&](float t) {
        const float c = t < 0.0f ? 0.0f : (t > xMax ? xMax : t);
        return static_cast<int16_t>(px + c / xMax * pw);
    };
    auto Y = [&](float v) {
        float f = (v - lo) / (hi - lo);
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        return static_cast<int16_t>(py + ph - f * ph);
    };

    // Sumbu + label
    p.text(x, y, spec.temperature ? HB_DEG "C" : "% SUSUT", kT1, color::kText);
    p.fill(px, py, 1, static_cast<int16_t>(ph + 1), color::kText);
    p.fill(px, static_cast<int16_t>(py + ph), static_cast<int16_t>(pw + 1), 1, color::kText);
    char label[8];
    std::snprintf(label, sizeof(label), "%d", static_cast<int>(std::lround(hi)));
    p.textRight(static_cast<int16_t>(px - 3), py, label, kT1, color::kText);
    std::snprintf(label, sizeof(label), "%d", static_cast<int>(std::lround(lo)));
    p.textRight(static_cast<int16_t>(px - 3), static_cast<int16_t>(py + ph - 7), label, kT1, color::kText);
    p.text(px, static_cast<int16_t>(py + ph + 4), "0", kT1, color::kText);
    std::snprintf(label, sizeof(label), "%dMNT", static_cast<int>(std::lround(xMax)));
    p.textRight(static_cast<int16_t>(px + pw), static_cast<int16_t>(py + ph + 4), label, kT1, color::kText);

    // Garis target suhu
    if (spec.temperature) {
        dashedH(p, static_cast<int16_t>(px + 1), static_cast<int16_t>(px + pw), Y(static_cast<float>(r.heatC)),
                color::kSubText);
    }
    // Penanda Fase A dan STOP
    // Label A di bawah plot, STOP di atas, supaya tidak saling menimpa
    // walaupun keduanya berdekatan.
    if (spec.phaseAMin >= 0.0f) {
        const int16_t mx = X(spec.phaseAMin);
        dashedV(p, mx, py, static_cast<int16_t>(py + ph), color::kText);
        p.textRight(static_cast<int16_t>(mx - 2), static_cast<int16_t>(py + ph - 9), "A", kT1, color::kText);
    }
    if (spec.stopMin >= 0.0f) {
        const int16_t mx = X(spec.stopMin);
        dashedV(p, mx, py, static_cast<int16_t>(py + ph), color::kText);
        if (mx + 2 + Painter::textWidth("STOP", kT1) <= px + pw) {
            p.text(static_cast<int16_t>(mx + 2), py, "STOP", kT1, color::kText);
        } else {
            p.textRight(static_cast<int16_t>(mx - 2), py, "STOP", kT1, color::kText);
        }
    }

    // Model (% susut): garis biru dari awal jendela fit; putus-putus setelah data
    if (!spec.temperature && spec.model && std::isfinite(r.tauMin) && r.tauMin > 0.0f && w0 > 0.0f) {
        const float tLast = n > 0 ? static_cast<float>(n) - 0.5f : 0.0f;
        const float tEnd = spec.stopMin > tLast ? spec.stopMin : tLast;
        int16_t lastX = -1;
        int16_t lastY = -1;
        for (int16_t sx = X(r.fitT0Min); sx <= X(tEnd); ++sx) {
            const float t = (static_cast<float>(sx - px) / pw) * xMax;
            const float m = r.mInfMg + r.amplitudeMg * std::exp(-(t - r.fitT0Min) / r.tauMin);
            const int16_t sy = Y((w0 - m) / w0 * 100.0f);
            const bool dashed = t > tLast && ((sx / 3) % 2 == 1);
            if (lastX >= 0 && !dashed) {
                p.gfx().drawLine(lastX, lastY, sx, sy, color::kAccent);
                p.gfx().drawLine(lastX, static_cast<int16_t>(lastY + 1), sx, static_cast<int16_t>(sy + 1),
                                 color::kAccent);
            }
            lastX = sx;
            lastY = sy;
        }
    }

    // Data: suhu = garis 2 px; susut = titik 4x4
    int16_t prevX = -1;
    int16_t prevY = -1;
    for (uint8_t k = 0; k < n; ++k) {
        const float v = valueAt(k);
        if (!std::isfinite(v)) {
            prevX = -1;
            continue;
        }
        const int16_t dx = X(static_cast<float>(k) + 0.5f);
        const int16_t dy = Y(v);
        if (spec.temperature) {
            if (prevX >= 0) {
                p.gfx().drawLine(prevX, prevY, dx, dy, color::kText);
                p.gfx().drawLine(prevX, static_cast<int16_t>(prevY + 1), dx, static_cast<int16_t>(dy + 1),
                                 color::kText);
            } else {
                p.fill(dx, dy, 2, 2, color::kText);
            }
        } else {
            p.fill(static_cast<int16_t>(dx - 2), static_cast<int16_t>(dy - 2), 4, 4, color::kText);
        }
        prevX = dx;
        prevY = dy;
    }
}

void drawCancelDialog(Painter& p, uint32_t heldMs, uint32_t targetMs) {
    const int16_t x = 18;
    const int16_t y = 76;
    const int16_t w = 220;
    const int16_t h = 112;
    p.fill(x, y, w, h, color::kBg);
    p.frame(x, y, w, h, 2, color::kText);
    p.text(static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 10), "BATALKAN TES?", kT2, color::kText);
    p.text(static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 34), "TETAP TAHAN TOMBOL ROTARY", kT1,
           color::kText);
    float fraction = targetMs > 0 ? static_cast<float>(heldMs) / static_cast<float>(targetMs) : 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(fraction * 100.0f));
    char label[20];
    std::snprintf(label, sizeof(label), "TAHAN %u DTK", static_cast<unsigned>((targetMs + 500) / 1000));
    drawProgress(p, static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 52), static_cast<int16_t>(w - 20), label,
                 pct, fraction, color::kWarn);
    p.text(static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 90), "LEPAS: LANJUT TES", kT1, color::kText);
}

// =====================================================================
//  Daftar bergulir
// =====================================================================
void ListCursor::rotate(int8_t steps, uint8_t count, uint8_t rows) {
    if (count == 0) {
        cursor = 0;
        top = 0;
        return;
    }
    const int next = static_cast<int>(cursor) + steps;
    cursor = static_cast<uint8_t>(next < 0 ? 0 : (next >= count ? count - 1 : next));
    clamp(count, rows);
}

void ListCursor::clamp(uint8_t count, uint8_t rows) {
    if (count == 0) {
        cursor = 0;
        top = 0;
        return;
    }
    if (cursor >= count) cursor = static_cast<uint8_t>(count - 1);
    if (cursor < top) top = cursor;
    if (cursor >= top + rows) top = static_cast<uint8_t>(cursor - rows + 1);
    if (count <= rows) top = 0;
}

void drawScrollRail(Painter& p, int16_t y, int16_t h, uint8_t top, uint8_t rows, uint8_t count) {
    if (count <= rows) return;
    p.fill(252, y, 3, h, color::kGrid);
    const int16_t thumbH = static_cast<int16_t>(h * rows / count);
    const int16_t thumbY = static_cast<int16_t>(y + (h - thumbH) * top / (count - rows));
    p.fill(252, thumbY, 3, thumbH, color::kText);
}

void drawMenuList(Painter& p, int16_t y, int16_t rowH, uint8_t rows, const ListCursor& list, uint8_t count,
                  const char* (*labelOf)(uint8_t index, const UiContext& ctx), const UiContext& ctx,
                  bool (*lockedOf)(uint8_t index, const UiContext& ctx)) {
    for (uint8_t row = 0; row < rows && list.top + row < count; ++row) {
        const uint8_t i = static_cast<uint8_t>(list.top + row);
        const int16_t ry = static_cast<int16_t>(y + row * rowH);
        const bool locked = lockedOf != nullptr && lockedOf(i, ctx);
        drawMenuRow(p, ry, static_cast<int16_t>(rowH - 2), labelOf(i, ctx), kT2, i == list.cursor, Icon::Count,
                    locked);
        if (i != list.cursor) {
            p.hline(kMargin, static_cast<int16_t>(ry + rowH - 2), kContentW, 1, color::kGrid);
        }
    }
    drawScrollRail(p, y, static_cast<int16_t>(rows * rowH - 2), list.top, rows, count);
}

void formatGramsTo(float grams, char* out, size_t size) {
    if (!std::isfinite(grams)) {
        std::snprintf(out, size, "--- G");
        return;
    }
    std::snprintf(out, size, "%.4f G", static_cast<double>(grams));
}

const char* scaleStatusText(const ScaleInfo& s) {
    switch (s.status) {
        case ScaleStatus::NoData: return "TIDAK ADA DATA";
        case ScaleStatus::DoutError: return "GALAT DATA";
        case ScaleStatus::Settling: return "MENYIAPKAN";
        case ScaleStatus::Clipped: return "DI LUAR RENTANG";
        case ScaleStatus::Busy: return "SIBUK";
        case ScaleStatus::Ok: break;
    }
    return s.stable ? "STABIL" : "BERGERAK";
}

}  // namespace hbui
