#pragma once

// =====================================================================
//  sounds.h — SEMUA suara buzzer di satu tabel, supaya mudah di-tweak
//
//  Buzzer BZ1 AKTIF lewat BC337, digerakkan PWM LEDC (26 Nov 2026).
//  Di buzzer aktif glide (sapuan nada) terdengar jelek; yang terdengar
//  baik adalah nada datar 1300-2400 Hz yang dipotong on/off. Karena itu
//  BOOT, KONFIRMASI, dan ERROR dibuat ulang sebagai bip putus-putus.
//  PERINGATAN, SELESAI, TAHAN SELESAI, dan KLIK tetap (sudah disukai).
//  Satu langkah = {f0, f1, ms, gateOn, gateOff}:
//    f0 = 0          : diam selama ms
//    f0 == f1        : nada datar
//    f0 != f1        : glide eksponensial f0 -> f1 (sapuan naik/turun)
//    gateOn/gateOff  : kalau > 0, nada dipotong-potong on/off (ms) di
//                      dalam langkah itu -> tekstur "bergetar/berdengung"
//  Resonansi buzzer 12 mm umumnya ~2-4 kHz: di sana paling keras.
//
//  Semua melodi karya sendiri untuk Heat Box. Teknik glide + potongan
//  nada terinspirasi efek suara robot OttoDIY (github.com/OttoDIY/
//  OttoDIYLib); tidak ada kode maupun nilai yang disalin.
// =====================================================================

#include <UiModel.h>

#include <cstdint>

struct ToneStep {
    uint16_t f0;
    uint16_t f1;
    uint16_t ms;
    uint8_t gateOn;
    uint8_t gateOff;
};

struct SoundDef {
    const ToneStep* steps;
    uint8_t count;
};

namespace sounds {

// BOOT: "bip-bip-biiip" naik, nada datar (buzzer aktif)
constexpr ToneStep kWelcome[] = {
    {1300, 1300, 80, 0, 0},
    {0, 0, 60, 0, 0},
    {1750, 1750, 80, 0, 0},
    {0, 0, 60, 0, 0},
    {2400, 2400, 260, 0, 0},
};

// KLIK (TEKAN): kicauan sangat pendek naik
constexpr ToneStep kClick[] = {
    {2700, 3300, 14, 0, 0},
};

// TAHAN SELESAI: dua bip, yang kedua lebih tinggi
constexpr ToneStep kHoldDone[] = {
    {1800, 1800, 55, 0, 0},
    {0, 0, 45, 0, 0},
    {2400, 2400, 75, 0, 0},
};

// KONFIRMASI (tersimpan): tiga ketukan cepat "tit-tit-tit", nada sama
constexpr ToneStep kConfirm[] = {
    {2100, 2100, 40, 0, 0},
    {0, 0, 35, 0, 0},
    {2100, 2100, 40, 0, 0},
    {0, 0, 35, 0, 0},
    {2100, 2100, 60, 0, 0},
};

// ERROR (ditolak/gagal): tiga bip panjang rendah putus-putus
constexpr ToneStep kError[] = {
    {1300, 1300, 170, 0, 0},
    {0, 0, 110, 0, 0},
    {1300, 1300, 170, 0, 0},
    {0, 0, 110, 0, 0},
    {1300, 1300, 260, 0, 0},
};

// PERINGATAN: nada panjang dua-ton bergantian (~1 dtk)
constexpr ToneStep kWarning[] = {
    {1300, 1300, 160, 0, 0}, {1750, 1750, 160, 0, 0},
    {1300, 1300, 160, 0, 0}, {1750, 1750, 160, 0, 0},
    {1300, 1300, 380, 0, 0},
};

// SELESAI (tes selesai, Prompt 2): arpeggio ceria + trill penutup
constexpr ToneStep kFinish[] = {
    {1047, 1047, 75, 0, 0}, {1319, 1319, 75, 0, 0}, {1568, 1568, 75, 0, 0},
    {2093, 2093, 110, 0, 0}, {0, 0, 50, 0, 0},
    {1760, 2349, 90, 0, 0}, {2349, 2349, 240, 30, 12},
};

#define HB_SOUND(arr) {arr, static_cast<uint8_t>(sizeof(arr) / sizeof(arr[0]))}

// Urutan HARUS sama dengan enum hbui::Sound
constexpr SoundDef kTable[] = {
    HB_SOUND(kWelcome), HB_SOUND(kClick),   HB_SOUND(kHoldDone), HB_SOUND(kConfirm),
    HB_SOUND(kError),   HB_SOUND(kWarning), HB_SOUND(kFinish),
};

#undef HB_SOUND

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(hbui::Sound::Count),
              "sounds::kTable harus berisi satu entri per hbui::Sound");

}  // namespace sounds
