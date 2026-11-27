#pragma once

// =====================================================================
//  Widgets.h — komponen design system (docs/ui, 04..13)
//  Semua status tampil sebagai teks/ikon + warna, tidak hanya warna,
//  supaya tetap terbaca di monitor hitam-putih.
// =====================================================================

#include <cstddef>

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

// ---------------------------------------------------------------------
//  Komponen layar tes, hasil, riwayat (Prompt 2)
// ---------------------------------------------------------------------

// "MM:SS" dari detik (maks 99:59); detik < 0 -> "--:--"
void formatMinSecTo(float seconds, char* out, size_t size);
template <size_t N>
void formatMinSec(float seconds, char (&out)[N]) {
    static_assert(N >= 6, "buffer MM:SS minimal 6");
    formatMinSecTo(seconds, out, N);
}
// "31/10/26" dan "14:05" (WIB) dari detik UNIX; epoch 0 -> "--/--/--", "--:--"
void formatDate(uint32_t epoch, char (&date)[10], char (&clock)[6]);
// "4.81" / "--.--"
void formatPercentTo(float value, char* out, size_t size);
template <size_t N>
void formatPercent(float value, char (&out)[N]) {
    static_assert(N >= 8, "buffer persen minimal 8");
    formatPercentTo(value, out, N);
}

// Penanda halaman kanan-bawah: kotak kecil per halaman + "n/N"
void drawPageMarker(Painter& p, int16_t y, uint8_t page, uint8_t count);

// Bar progres: label T1 kiri + nilai T1 kanan, lalu bar 12 px.
void drawProgress(Painter& p, int16_t x, int16_t y, int16_t w, const char* label, const char* value,
                  float fraction, uint8_t fill);

// Baris data (label T1 kiri, nilai T2 kanan, garis pemisah abu)
struct DataRow {
    const char* label;
    const char* value;
};
// valueSize kT1 untuk nilai panjang (MAC, IP, nama host)
void drawDataRows(Painter& p, int16_t y, int16_t rowH, const DataRow* rows, uint8_t count,
                  uint8_t valueSize = kT2);

// Template ERROR tanpa hasil (02-11): blok merah + 3 baris T2 + langkah operator
void drawErrorBlock(Painter& p, const char* code, const char* const (&lines)[3], const char* step);

// Grafik per menit fase HEAT (02-08b/d, 02-10.2/6)
struct GraphSpec {
    const hbcycle::CycleResult* result;
    bool temperature;   // false = % susut, true = suhu C
    float xMaxMin;      // sumbu x 0..xMaxMin
    float phaseAMin;    // < 0 = tanpa penanda
    float stopMin;      // < 0 = tanpa penanda
    bool model;         // garis model (Fase B)
};
void drawGraph(Painter& p, int16_t x, int16_t y, int16_t w, int16_t h, const GraphSpec& spec);

// Dialog batal yang terisi selama TAHAN (02-12)
void drawCancelDialog(Painter& p, uint32_t heldMs, uint32_t targetMs);

// ---------------------------------------------------------------------
//  Daftar bergulir (PENGATURAN, OPSI PENGEMBANG, RIWAYAT, daftar WiFi)
// ---------------------------------------------------------------------
struct ListCursor {
    uint8_t cursor = 0;
    uint8_t top = 0;  // baris pertama yang tampil
    // Pindah kursor (berhenti di ujung) dan geser jendela `rows` baris
    void rotate(int8_t steps, uint8_t count, uint8_t rows);
    void clamp(uint8_t count, uint8_t rows);
};
// Rel gulir 3 px di kanan (x 252) kalau count > rows
void drawScrollRail(Painter& p, int16_t y, int16_t h, uint8_t top, uint8_t rows, uint8_t count);

// Baris menu daftar bergulir: rows baris tinggi rowH mulai y, label dari
// fungsi labelOf(i). Baris terkunci digambar abu + ikon gembok.
void drawMenuList(Painter& p, int16_t y, int16_t rowH, uint8_t rows, const ListCursor& list, uint8_t count,
                  const char* (*labelOf)(uint8_t index, const UiContext& ctx), const UiContext& ctx,
                  bool (*lockedOf)(uint8_t index, const UiContext& ctx));

// "12.3456 G" (4 desimal = 0,1 mg) atau "--- G"
void formatGramsTo(float grams, char* out, size_t size);
template <size_t N>
void formatGrams(float grams, char (&out)[N]) {
    static_assert(N >= 12, "buffer gram minimal 12");
    formatGramsTo(grams, out, N);
}
// Teks status timbangan: "STABIL", "BERGERAK", "TIDAK ADA DATA", ...
const char* scaleStatusText(const ScaleInfo& s);

}  // namespace hbui
