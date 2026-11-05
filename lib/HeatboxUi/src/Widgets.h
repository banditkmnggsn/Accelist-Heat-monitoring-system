#pragma once

// =====================================================================
//  Widgets.h — komponen design system (docs/ui, 04..13)
//  Semua status tampil sebagai teks/ikon + warna, tidak hanya warna,
//  supaya tetap terbaca di monitor hitam-putih.
// =====================================================================

#include "Painter.h"
#include "Screen.h"
#include "UiModel.h"

namespace hbui {

// Header kiri (x 6..150): ikon WiFi, jam ("*--:--" sebelum NTP), ikon
// pintu HANYA saat terbuka, lalu judul T2 di y 20.
void drawHeader(Painter& p, const UiSnapshot& snap, const char* title);

// Baris petunjuk y 222..234 dengan garis abu di atasnya. Saat tombol
// ditahan >= 0,6 dtk, petunjuk diganti bar progres berlabel aksi.
void drawHint(Painter& p, const char* hint, uint32_t holdProgressMs, const HoldSpec& hold);

enum class BannerKind : uint8_t { Warn, Heat, Error };
// Banner y 206..218. Kuning & oranye: teks hitam; merah: teks putih.
void drawBanner(Painter& p, BannerKind kind, const char* text);

// Baris menu. selected memakai gaya pilihan aktif (UiStyle).
// iconOrNone = Icon::Count kalau tanpa ikon. Return warna tinta yang
// dipakai, untuk teks tambahan di baris yang sama.
uint8_t drawMenuRow(Painter& p, int16_t y, int16_t h, const char* label, uint8_t size, bool selected,
                 Icon iconOrNone, bool locked);

// Badge kecil (desain 07): blok warna + ikon 8x8 + teks putih/hitam.
enum class BadgeKind : uint8_t { Ok, Warn, Error, Heat };
void drawBadge(Painter& p, int16_t x, int16_t y, BadgeKind kind, const char* text);

// Bar tahap tes 244 px: 5 kotak 48 px, label PRE/TARE/ISI/PANAS/HASIL.
// Selesai = blok hitam teks putih + √; sekarang = blok biru, garis 2 px;
// belum = garis abu 1 px, teks abu. Dapat dibedakan dalam hitam-putih.
void drawStepper(Painter& p, int16_t y, uint8_t currentStep);

enum class BoxState : uint8_t { Normal, Selected, Editing, Rejected };
// Kotak nilai (desain 10): label T1, nilai, ◄ ► saat diedit, "!" saat ditolak.
void drawValueBox(Painter& p, int16_t x, int16_t y, int16_t w, int16_t h, const char* label,
                  const char* value, uint8_t valueSize, BoxState state);

// Kotak cek 10x10 (desain 11) + teks T1.
enum class CheckMark : uint8_t { Empty, Busy, Done, Failed };
void drawCheckBox(Painter& p, int16_t x, int16_t y, CheckMark mark);

// Dialog modal (desain 12) di 18, 62, 220, 116.
void drawDialog(Painter& p, const DialogSpec& spec, uint8_t selectedOption);

// "HH:MM", atau "*--:--" sebelum jam pernah sinkron
void formatClock(const UiSnapshot& snap, char (&out)[8]);

}  // namespace hbui
