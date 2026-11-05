#include "Widgets.h"

#include <cstdio>

namespace hbui {

namespace {

using namespace layout;

struct SelectColors {
    uint8_t fill;   // kBg = tanpa blok
    uint8_t text;
    uint8_t frame;  // kBg = tanpa bingkai
};

SelectColors selectColors(const UiStyle& style) {
    switch (style.select) {
        case SelectStyle::BlackBlock:
            return {color::kText, color::kBg, color::kBg};
        case SelectStyle::ThickFrame:
            return {color::kBg, color::kText, color::kText};
        case SelectStyle::BlueBlock:
        case SelectStyle::Count:
            break;
    }
    return {color::kAccent, color::kBg, color::kBg};
}

}  // namespace

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
    uint8_t ink = locked ? p.style().secondaryColor() : color::kText;
    if (selected) {
        const SelectColors sc = selectColors(p.style());
        if (sc.fill != color::kBg) {
            p.fill(kMargin, y, kContentW, h, sc.fill);
        }
        if (sc.frame != color::kBg) {
            p.frame(kMargin, y, kContentW, h, 2, sc.frame);
        }
        ink = sc.text;
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
            ink = p.style().secondaryColor();
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
    const uint8_t labelInk = (state == BoxState::Normal) ? p.style().secondaryColor() : color::kText;
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
    const SelectColors sc = selectColors(p.style());
    const int16_t buttonW = (kDialogW - 24) / 2;
    for (uint8_t i = 0; i < 2; ++i) {
        const int16_t bx = static_cast<int16_t>(kDialogX + 8 + i * (buttonW + 8));
        const int16_t by = bottomY + 5;
        uint8_t ink = color::kText;
        if (i == selectedOption) {
            if (sc.fill != color::kBg) p.fill(bx, by, buttonW, 14, sc.fill);
            p.frame(bx, by, buttonW, 14, 2, sc.frame != color::kBg ? sc.frame : sc.fill);
            ink = sc.text;
        } else {
            p.frame(bx, by, buttonW, 14, 1, color::kGrid);
        }
        p.textCenter(static_cast<int16_t>(bx + buttonW / 2), by + 3, spec.options[i], kT1, ink);
    }
}

}  // namespace hbui
