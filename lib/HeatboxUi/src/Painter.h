#pragma once

// =====================================================================
//  Painter.h — pembungkus Adafruit_GFX untuk aturan design system
//
//  - Semua teks ditulis KAPITAL (sesuai desain) dengan font bawaan 5x7
//    dan cp437(true) supaya simbol ° ± · → √ ◄ ► benar.
//  - Teks dipotong supaya tidak melewati margin kanan (atau lebar yang
//    diberikan), sehingga batas karakter T1 40 / T2 20 / T3 13 / T4 10
//    ditegakkan otomatis.
//  - Tidak memakai fungsi Arduino lain selain Adafruit_GFX, jadi bisa
//    dirender di PC (tools/ui_render) untuk pratinjau PNG.
// =====================================================================

#include <Adafruit_GFX.h>

#include "Icons.h"
#include "Theme.h"

namespace hbui {

class Painter {
public:
    Painter(Adafruit_GFX& gfx, const UiStyle& style);

    Adafruit_GFX& gfx() const { return gfx_; }
    const UiStyle& style() const { return style_; }

    void clear();
    void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c);
    // Bingkai dengan tebal `thick` ke dalam
    void frame(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t thick, uint8_t c);
    void hline(int16_t x, int16_t y, int16_t w, uint8_t thick, uint8_t c);

    // Lebar teks dalam piksel tanpa spasi kosong di karakter terakhir.
    static int16_t textWidth(const char* s, uint8_t size);
    static int16_t charWidth(uint8_t size) { return static_cast<int16_t>(6 * size); }

    // maxW 0 = sampai margin kanan layar. Return x setelah teks.
    int16_t text(int16_t x, int16_t y, const char* s, uint8_t size, uint8_t c, int16_t maxW = 0);
    void textRight(int16_t right, int16_t y, const char* s, uint8_t size, uint8_t c);
    void textCenter(int16_t centerX, int16_t y, const char* s, uint8_t size, uint8_t c);

    void icon(Icon icon, int16_t x, int16_t y, uint8_t c, bool large);

    // Zona PARKING dipaksa putih setelah tiap gambar (aturan mutlak).
    void clearParking();

private:
    Adafruit_GFX& gfx_;
    const UiStyle& style_;
};

}  // namespace hbui
