#pragma once

// =====================================================================
//  Theme.h — design system (docs/ui, halaman DESIGN SYSTEM)
//
//  Kanvas asli 256 x 240 (desain 768 x 720 = 3x). Koordinat di sini
//  piksel asli. Keputusan yang menyimpang dari desain: docs/ui/KEPUTUSAN-UI.md
// =====================================================================

#include <cstdint>

namespace hbui {

// ---- palet RGB332 (RRRGGGBB) ----
namespace color {
constexpr uint8_t kBg        = 0xFF;  // putih
constexpr uint8_t kText      = 0x00;  // hitam
constexpr uint8_t kSecondary = 0x49;  // abu tua
constexpr uint8_t kGrid      = 0x92;  // abu muda (garis, kotak "belum")
constexpr uint8_t kAccent    = 0x02;  // biru
constexpr uint8_t kOk        = 0x10;  // hijau
constexpr uint8_t kWarn      = 0xFC;  // kuning: HANYA blok berisi teks hitam
constexpr uint8_t kError     = 0xA0;  // merah
constexpr uint8_t kHeat      = 0xF0;  // oranye
}  // namespace color

// ---- ukuran teks: font 5x7 Adafruit GFX (sel 6x8) x setTextSize ----
constexpr uint8_t kT1 = 1;  // 6x8   label kecil
constexpr uint8_t kT2 = 2;  // 12x16 teks penting
constexpr uint8_t kT3 = 3;  // 18x24 nilai
constexpr uint8_t kT4 = 4;  // 24x32 angka besar

// ---- tata letak ----
namespace layout {
constexpr int16_t kW = 256;
constexpr int16_t kH = 240;
constexpr int16_t kMargin = 6;
constexpr int16_t kContentW = kW - 2 * kMargin;  // 244 -> T1 40, T2 20, T3 13, T4 10 karakter
constexpr int16_t kRight = kW - kMargin;         // 250

// Zona PARKING: monitor menimpa tulisan di sini, WAJIB putih di semua layar.
constexpr int16_t kParkX = 156;
constexpr int16_t kParkY = 0;
constexpr int16_t kParkW = kW - kParkX;  // 100
constexpr int16_t kParkH = 43;           // y 0..42

// Header hanya di kiri (x 6..150)
constexpr int16_t kHeaderW = 144;
constexpr int16_t kStatusY = 16;  // ikon WiFi + jam + ikon pintu (monitor memotong +-12 baris atas)
constexpr int16_t kTitleY = 28;   // judul T2, y 28..43

constexpr int16_t kContentY = 48;     // isi mulai di bawah zona PARKING
constexpr int16_t kBannerY = 206;     // banner peringatan, tinggi 12
constexpr int16_t kBannerH = 12;
constexpr int16_t kHintRuleY = 219;   // garis abu 2 px di atas petunjuk
constexpr int16_t kHintY = 222;       // baris petunjuk T1, y 222..234
constexpr int16_t kHintH = 12;

// Dialog (desain 18, 62, 220, 116; dilebarkan 16 px supaya judul T2
// sampai 18 karakter muat, mis. "SIKLUS BELUM AKTIF")
constexpr int16_t kDialogX = 10;
constexpr int16_t kDialogY = 62;
constexpr int16_t kDialogW = 236;
constexpr int16_t kDialogH = 116;

// Bar tahap tes: 5 kotak 48 px + 4 celah 1 px = 244 px
constexpr int16_t kStepperY = 44;
constexpr int16_t kStepperH = 28;
constexpr int16_t kStepBoxW = 48;
}  // namespace layout

// ---- simbol cp437 (Adafruit GFX dengan cp437(true)) ----
// Pakai penggabungan literal: "80" HB_DEG "C", bukan "80\xF8C".
#define HB_DEG    "\xF8"  // °
#define HB_PM     "\xF1"  // ±
#define HB_DOT    "\xFA"  // ·
#define HB_ARROW  "\x1A"  // →
#define HB_CHECK  "\xFB"  // √ (dipakai sebagai ✓)
#define HB_GE     "\xF2"  // ≥
#define HB_LEFT   "\x11"  // ◄
#define HB_RIGHT  "\x10"  // ►

#define HB_OHM    "\xEA"  // Ω

// Gaya tetap (pilihan uji LCD 6 Nov 2026, varian lain sudah dihapus):
//   pilihan aktif = BINGKAI TEBAL 2 px + ">" ; teks sekunder = HITAM
//   (teks abu dan blok biru kurang jelas di monitor ini).
namespace color {
constexpr uint8_t kSubText = kText;
}  // namespace color

}  // namespace hbui
