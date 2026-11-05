// Layar SERVIS sementara (Prompt 1): menu, UJI TAMPILAN, UJI INPUT, UJI SUARA.
// Menu servis lengkap (sensor, timbangan, kalibrasi, ...) = Prompt 2.

#include <app_config.h>

#include <cstdio>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

int clampIndex(int value, int last) { return value < 0 ? 0 : (value > last ? last : value); }

const char* const kSoundNames[static_cast<uint8_t>(Sound::Count)] = {
    "BOOT", "KLIK", "TAHAN SELESAI", "KONFIRMASI", "ERROR", "PERINGATAN", "SELESAI",
};

const char* const kPageNames[] = {
    "PALET & KOMPONEN", "TEKS T1-T4", "IKON 16X8", "IKON 8X8",
    "MARGIN & PARKING", "OVERSCAN", "VARIAN: PILIHAN", "VARIAN: TEKS ABU",
};

const char* const kSelectNames[] = {"A: BLOK BIRU", "B: BLOK HITAM", "C: BINGKAI TEBAL"};

void pageLabel(Painter& p, uint8_t page, uint8_t pageCount, int16_t x, int16_t y) {
    char text[32];
    std::snprintf(text, sizeof(text), "%u/%u %s", static_cast<unsigned>(page + 1),
                  static_cast<unsigned>(pageCount), kPageNames[page]);
    p.text(x, y, text, kT1, p.style().secondaryColor());
}

void iconGrid(Painter& p, bool large) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Icon::Count); ++i) {
        const int16_t x = static_cast<int16_t>(kMargin + (i % 4) * 61);
        const int16_t y = static_cast<int16_t>(52 + (i / 4) * 38);
        const Icon icon = static_cast<Icon>(i);
        const int16_t w = large ? kIconLargeW : kIconSmallW;
        p.icon(icon, static_cast<int16_t>(x + 30 - w / 2), y, color::kText, large);
        p.textCenter(static_cast<int16_t>(x + 30), static_cast<int16_t>(y + 14), iconName(icon), kT1,
                     color::kText);
    }
}

}  // namespace

// =====================================================================
//  MENU SERVIS (tahan 5 dtk di Beranda, langsung ke sini)
// =====================================================================
void ServiceMenuScreen::onRotate(UiContext&, int8_t steps, bool) {
    cursor_ = static_cast<uint8_t>(clampIndex(cursor_ + steps, 2));
}

void ServiceMenuScreen::onPress(UiContext& ctx) {
    static const ScreenId kTargets[3] = {ScreenId::DisplayTest, ScreenId::InputTest, ScreenId::SoundTest};
    ctx.nav.push(kTargets[cursor_]);
}

void ServiceMenuScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "SERVIS");
    p.icon(Icon::Service, 128, 24, color::kText, true);
    p.text(kMargin, 48, "KHUSUS TEKNISI", kT1, p.style().secondaryColor());
    static const char* const kItems[3] = {"UJI TAMPILAN", "UJI INPUT", "UJI SUARA"};
    for (uint8_t i = 0; i < 3; ++i) {
        drawMenuRow(p, static_cast<int16_t>(62 + i * 24), 22, kItems[i], kT2, i == cursor_, Icon::Count, false);
    }
    p.text(kMargin, 140, "MENU SERVIS LENGKAP (SENSOR,", kT1, p.style().secondaryColor());
    p.text(kMargin, 150, "TIMBANGAN, KALIBRASI) DI TAHAP BERIKUT", kT1, p.style().secondaryColor());
}

// =====================================================================
//  UJI TAMPILAN
// =====================================================================
void DisplayTestScreen::onRotate(UiContext&, int8_t steps, bool) {
    page_ = static_cast<uint8_t>(clampIndex(page_ + steps, PageCount - 1));
}

void DisplayTestScreen::onPress(UiContext& ctx) {
    if (page_ == VarSelect) {
        ctx.style.select = static_cast<SelectStyle>((static_cast<uint8_t>(ctx.style.select) + 1) %
                                                    static_cast<uint8_t>(SelectStyle::Count));
    } else if (page_ == VarSecondary) {
        ctx.style.secondary = static_cast<SecondaryStyle>((static_cast<uint8_t>(ctx.style.secondary) + 1) %
                                                          static_cast<uint8_t>(SecondaryStyle::Count));
    }
}

const char* DisplayTestScreen::hint(const UiContext&) const {
    if (page_ == Overscan) {
        return nullptr;  // pola sampai ke tepi; TAHAN tetap kembali
    }
    if (page_ == VarSelect || page_ == VarSecondary) {
        return "PUTAR:HALAMAN  TEKAN:GANTI VARIAN";
    }
    return "PUTAR:HALAMAN  TAHAN:KEMBALI";
}

void DisplayTestScreen::draw(Painter& p, const UiContext& ctx) const {
    if (page_ != Margins && page_ != Overscan) {
        drawHeader(p, ctx.snap, "UJI TAMPILAN");
        pageLabel(p, page_, PageCount, kMargin, 38);
    }
    switch (page_) {
        case Palette: {
            static const uint8_t kColors[9] = {color::kBg,   color::kText, color::kSecondary,
                                               color::kGrid, color::kAccent, color::kOk,
                                               color::kWarn, color::kError, color::kHeat};
            static const char* const kNames[9] = {"PUTIH", "HITAM",   "SEKUNDER", "GRID", "AKSEN",
                                                  "OK",    "WASPADA", "ERROR",    "PANAS"};
            for (uint8_t i = 0; i < 9; ++i) {
                const int16_t x = static_cast<int16_t>(kMargin + (i % 3) * 82);
                const int16_t y = static_cast<int16_t>(50 + (i / 3) * 28);
                p.fill(x, y, 22, 18, kColors[i]);
                p.frame(x, y, 22, 18, 1, color::kText);
                p.text(static_cast<int16_t>(x + 26), static_cast<int16_t>(y + 1), kNames[i], kT1, color::kText, 54);
                char hex[8];
                std::snprintf(hex, sizeof(hex), "0X%02X", static_cast<unsigned>(kColors[i]));
                p.text(static_cast<int16_t>(x + 26), static_cast<int16_t>(y + 10), hex, kT1,
                       p.style().secondaryColor());
            }
            drawBadge(p, kMargin, 136, BadgeKind::Ok, "OK");
            drawBadge(p, 44, 136, BadgeKind::Warn, "TIMEOUT");
            drawBadge(p, 112, 136, BadgeKind::Error, "ERROR");
            drawBadge(p, 174, 136, BadgeKind::Heat, "PANAS");
            drawStepper(p, 156, 2);
            p.text(kMargin, 188, "BANNER (Y 206-218):", kT1, p.style().secondaryColor());
            drawBanner(p, BannerKind::Warn, "PERINGATAN: CEK SAMPEL");
            break;
        }
        case Text:
            p.text(kMargin, 48, "T1 6X8 " HB_DOT " MAKS 40 KARAKTER", kT1, color::kText);
            p.text(kMargin, 58, "1234567890123456789012345678901234567890", kT1, color::kText);
            p.text(kMargin, 68, "T1 ABU: PRE-HEAT 10M " HB_DOT " MAKS 25M", kT1, color::kSecondary);
            p.text(kMargin, 80, "T2 " HB_PM "06:08", kT2, color::kText);
            p.text(kMargin, 98, "12345678901234567890", kT2, color::kText);
            p.text(kMargin, 118, "110" HB_DEG "C", kT3, color::kText);
            p.text(110, 118, "T3", kT1, color::kSecondary);
            p.text(kMargin, 146, "4.81 %", kT4, color::kText);
            p.text(160, 146, "T4", kT1, color::kSecondary);
            p.text(kMargin, 184, "SIMBOL: " HB_DEG " " HB_PM " " HB_DOT " " HB_ARROW " " HB_CHECK " " HB_LEFT
                   " " HB_RIGHT " " HB_GE,
                   kT1, color::kText);
            p.text(kMargin, 196, "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789", kT1, color::kText);
            break;
        case IconsLarge:
            iconGrid(p, true);
            break;
        case IconsSmall:
            iconGrid(p, false);
            break;
        case Margins:
            p.frame(kMargin, kMargin, kContentW, kH - 2 * kMargin, 1, color::kText);
            // Garis batas zona PARKING digambar TEPAT DI LUAR zona (x 155, y 43)
            p.fill(kParkX - 1, 0, 1, kParkH + 1, color::kError);
            p.fill(kParkX - 1, kParkH, kW - kParkX + 1, 1, color::kError);
            p.text(12, 12, "UJI TAMPILAN", kT2, color::kText, kHeaderW);
            pageLabel(p, page_, PageCount, 12, 50);
            p.text(12, 70, "BINGKAI HITAM = MARGIN AMAN 6 PX", kT1, color::kText);
            p.text(12, 82, "GARIS MERAH = BATAS ZONA PARKING", kT1, color::kText);
            p.text(12, 94, "(X 156-255, Y 0-42) HARUS PUTIH", kT1, color::kText);
            p.text(12, 112, "CEK: SISI BINGKAI TERLIHAT? (ATAS", kT1, color::kText);
            p.text(12, 124, "     KANAN SENGAJA HILANG DI ZONA)", kT1, color::kText);
            p.text(12, 142, "CEK: TULISAN PARKING MONITOR", kT1, color::kText);
            p.text(12, 154, "     DI DALAM ZONA MERAH?", kT1, color::kText);
            break;
        case Overscan: {
            static const uint8_t kFrames[8] = {color::kError, color::kWarn, color::kOk,     color::kGrid,
                                               color::kAccent, color::kHeat, color::kSecondary, color::kText};
            for (uint8_t i = 0; i < 8; ++i) {
                const int16_t inset = static_cast<int16_t>(i * 4);
                p.frame(inset, inset, static_cast<int16_t>(kW - 2 * inset), static_cast<int16_t>(kH - 2 * inset), 2,
                        kFrames[i]);
            }
            pageLabel(p, page_, PageCount, 40, 60);
            p.text(40, 76, "BINGKAI TIAP 4 PX DARI TEPI:", kT1, color::kText);
            p.text(40, 88, "0 MERAH  4 KUNING  8 HIJAU", kT1, color::kText);
            p.text(40, 100, "12 ABU  16 BIRU  20 ORANYE", kT1, color::kText);
            p.text(40, 112, "24 ABU TUA  28 HITAM", kT1, color::kText);
            p.text(40, 130, "ZONA PARKING DIKOSONGKAN.", kT1, color::kText);
            p.text(40, 150, "TAHAN 2 DTK: KEMBALI", kT1, color::kText);
            break;
        }
        case VarSelect:
            for (uint8_t i = 0; i < static_cast<uint8_t>(SelectStyle::Count); ++i) {
                UiStyle variant = ctx.style;
                variant.select = static_cast<SelectStyle>(i);
                Painter sample(p.gfx(), variant);
                const int16_t y = static_cast<int16_t>(52 + i * 46);
                const bool active = ctx.style.select == variant.select;
                char label[32];
                std::snprintf(label, sizeof(label), "%s%s", kSelectNames[i], active ? "  <- AKTIF" : "");
                p.text(kMargin, y, label, kT1, color::kText);
                drawMenuRow(sample, static_cast<int16_t>(y + 10), 20, "MULAI TES", kT2, true, Icon::Start, false);
                drawMenuRow(sample, static_cast<int16_t>(y + 30), 12, "RIWAYAT", kT1, false, Icon::Count, false);
            }
            break;
        case VarSecondary: {
            static const uint8_t kInks[2] = {color::kSecondary, color::kText};
            static const char* const kLabels[2] = {"A: ABU TUA 0X49", "B: HITAM"};
            for (uint8_t i = 0; i < 2; ++i) {
                const int16_t y = static_cast<int16_t>(54 + i * 70);
                const bool active = static_cast<uint8_t>(ctx.style.secondary) == i;
                char label[32];
                std::snprintf(label, sizeof(label), "%s%s", kLabels[i], active ? "  <- AKTIF" : "");
                p.text(kMargin, y, label, kT1, color::kText);
                p.frame(kMargin, static_cast<int16_t>(y + 10), kContentW, 52, 1, color::kGrid);
                p.text(12, static_cast<int16_t>(y + 16), "PROFIL AKTIF", kT1, kInks[i]);
                p.text(12, static_cast<int16_t>(y + 28), "PRE-HEAT 10M " HB_DOT " MIN 3M " HB_DOT " MAKS 25M",
                       kT1, kInks[i]);
                p.text(12, static_cast<int16_t>(y + 40), "HASIL TERAKHIR " HB_DOT " KHUSUS TEKNISI", kT1, kInks[i]);
            }
            break;
        }
        default:
            break;
    }
}

// =====================================================================
//  UJI INPUT
// =====================================================================
void InputTestScreen::onEnter(UiContext& ctx) {
    presses_ = 0;
    fastClicks_ = 0;
    startDetents_ = ctx.snap.input.detents;
}

void InputTestScreen::onRotate(UiContext&, int8_t, bool fast) {
    if (fast) {
        ++fastClicks_;
    }
}

void InputTestScreen::onPress(UiContext&) { ++presses_; }

uint32_t InputTestScreen::liveKey(const UiContext& ctx) const {
    const InputDiag& d = ctx.snap.input;
    uint32_t key = static_cast<uint32_t>(d.raw) * 31u + d.invalidJumps * 7u + d.partialReturns * 13u +
                   d.reverseSuspect * 17u + d.lastHoldMs + (d.pressedMs / 100u) * 3u;
    key ^= (d.pressed ? 0x10000u : 0u) | (d.cancelled ? 0x20000u : 0u) | (d.doorPinHigh ? 0x40000u : 0u);
    return key;
}

void InputTestScreen::draw(Painter& p, const UiContext& ctx) const {
    const InputDiag& d = ctx.snap.input;
    drawHeader(p, ctx.snap, "UJI INPUT");
    char text[40];

    p.text(kMargin, 46, "KLIK (POSISI)", kT1, p.style().secondaryColor());
    std::snprintf(text, sizeof(text), "%ld", static_cast<long>(d.detents - startDetents_));
    p.text(kMargin, 56, text, kT3, color::kText, 110);
    p.text(130, 46, "ARAH TERAKHIR", kT1, p.style().secondaryColor());
    p.text(130, 58, d.lastDir > 0 ? "KANAN +" : (d.lastDir < 0 ? "KIRI -" : "-"), kT2, color::kText);

    struct Row {
        const char* label;
        long value;
    };
    const Row rows[] = {
        {"RAW (TRANSISI SAH)", static_cast<long>(d.raw)},
        {"RAW PER KLIK TERAKHIR", static_cast<long>(d.rawPerClick)},
        {"LOMPAT 2 BIT (TERLEWAT)", static_cast<long>(d.invalidJumps)},
        {"GERAK TANPA KLIK", static_cast<long>(d.partialReturns)},
        {"BALIK ARAH CEPAT", static_cast<long>(d.reverseSuspect)},
        {"PUTARAN CEPAT (LANGKAH 5)", static_cast<long>(fastClicks_)},
        {"JUMLAH TEKAN", static_cast<long>(presses_)},
        {"LAMA TEKAN TERAKHIR (MS)", static_cast<long>(d.lastHoldMs)},
    };
    int16_t y = 86;
    for (const Row& row : rows) {
        p.text(kMargin, y, row.label, kT1, color::kText);
        std::snprintf(text, sizeof(text), "%ld", row.value);
        p.textRight(kRight, y, text, kT1, color::kText);
        y = static_cast<int16_t>(y + 11);
    }
    std::snprintf(text, sizeof(text), "TARGET RAW/KLIK = %u", static_cast<unsigned>(d.stepsPerDetent));
    p.text(kMargin, y, text, kT1, p.style().secondaryColor());
    y = static_cast<int16_t>(y + 11);

    if (d.cancelled) {
        std::snprintf(text, sizeof(text), "TOMBOL: BATAL (DIPUTAR)");
    } else if (d.pressed) {
        std::snprintf(text, sizeof(text), "TOMBOL: DITEKAN %lu MS", static_cast<unsigned long>(d.pressedMs));
    } else {
        std::snprintf(text, sizeof(text), "TOMBOL: LEPAS");
    }
    p.text(kMargin, y, text, kT1, color::kText);
    y = static_cast<int16_t>(y + 11);
    p.text(kMargin, y, d.doorPinHigh ? "PINTU GPIO34: HIGH = TERBUKA" : "PINTU GPIO34: LOW = TERTUTUP", kT1,
           color::kText);
}

// =====================================================================
//  UJI SUARA
// =====================================================================
void SoundTestScreen::onRotate(UiContext&, int8_t steps, bool) {
    cursor_ = static_cast<uint8_t>(clampIndex(cursor_ + steps, static_cast<int>(Sound::Count) - 1));
}

void SoundTestScreen::onPress(UiContext& ctx) {
    // Menimpa klik bawaan; tetap bunyi walau suara dimatikan di Pengaturan
    ctx.cmd.playSound(static_cast<Sound>(cursor_), true);
}

void SoundTestScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "UJI SUARA");
    for (uint8_t i = 0; i < static_cast<uint8_t>(Sound::Count); ++i) {
        drawMenuRow(p, static_cast<int16_t>(kContentY + i * 22), 20, kSoundNames[i], kT2, i == cursor_,
                    Icon::Count, false);
    }
    if (!ctx.settings.soundOn) {
        drawBanner(p, BannerKind::Warn, "SUARA MATI " HB_DOT " UJI TETAP BUNYI");
    }
}

}  // namespace hbui
