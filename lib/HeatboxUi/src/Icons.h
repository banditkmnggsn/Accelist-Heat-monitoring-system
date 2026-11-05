#pragma once

// =====================================================================
//  Icons.h — ikon pixel satu warna
//
//  Besar : 16 x 8, diambil piksel-per-piksel dari desain (docs/ui, SET
//          IKON PIXEL). Di desain kotaknya 16x16 tetapi gambarnya hanya
//          8 baris teratas.
//  Kecil : 8 x 8, digambar ulang (desain hanya memperkecil versi besar
//          di browser, tidak ada bitmap 8x8 asli). Dinilai di UJI TAMPILAN.
// =====================================================================

#include <cstdint>

namespace hbui {

enum class Icon : uint8_t {
    Wifi,
    WifiX,
    Clock,
    Door,
    Thermo,
    Scale,
    Heat,
    Warn,
    Check,
    Cross,
    Lock,
    Start,
    Rotary,
    History,
    Gear,
    Service,
    Count,
};

constexpr int16_t kIconLargeW = 16;
constexpr int16_t kIconLargeH = 8;
constexpr int16_t kIconSmallW = 8;
constexpr int16_t kIconSmallH = 8;

// Baris dari atas; bit paling kiri = MSB.
const uint16_t* iconLarge(Icon icon);  // 8 baris x 16 bit
const uint8_t* iconSmall(Icon icon);   // 8 baris x 8 bit
const char* iconName(Icon icon);

}  // namespace hbui
