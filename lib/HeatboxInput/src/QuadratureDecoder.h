#pragma once

// =====================================================================
//  QuadratureDecoder.h — decoder rotary EC11 tanpa false trigger
//
//  Masukan: state pin (A << 1) | B setelah pull-up (diam/detent = 11).
//  Keluaran: +1 / -1 HANYA saat satu klik penuh selesai, 0 selain itu.
//
//  Kenapa tidak ada langkah palsu:
//    - Tiap transisi sah (satu bit berubah) menambah/mengurangi hitungan
//      quadrature. Pantulan kontak (11->01->11) saling meniadakan.
//    - Langkah dikeluarkan hanya saat state kembali ke detent DAN hitungan
//      tepat mencapai +-langkahPerKlik. Gerak setengah jalan yang kembali
//      ke detent dibuang tanpa langkah (dulu dibulatkan, itu yang bisa
//      memberi langkah salah arah).
//    - Lompatan dua bit (transisi terlewat) tidak dihitung; klik itu
//      hilang, tetapi tidak pernah berubah arah.
//  Akibatnya putar kanan tidak pernah menghasilkan -1, dan diam tidak
//  pernah menghasilkan langkah.
// =====================================================================

#include <cstdint>

namespace hbinput {

class QuadratureDecoder {
public:
    static constexpr uint8_t kDetentState = 0x03;  // A=1, B=1 (pull-up)

    // stepsPerDetent: 4 (EC11 20 klik/20 pulsa, detent di 11) atau
    // 2 (setengah siklus per klik, detent di 11 dan 00).
    // reverse: balik arah kalau A/B tertukar di pengawatan.
    QuadratureDecoder(uint8_t stepsPerDetent, bool reverse);

    void reset(uint8_t ab);

    // Panggil tiap polling. Return +1, -1, atau 0.
    int8_t update(uint8_t ab);

    // ---- diagnostik ----
    int32_t rawCount() const { return raw_; }           // transisi sah bertanda
    uint32_t invalidJumps() const { return invalid_; }  // dua bit berubah sekaligus
    // Gerak yang kembali ke detent tanpa menjadi satu klik (pantulan,
    // goyangan, atau transisi terlewat).
    uint32_t partialReturns() const { return partial_; }
    uint8_t stepsPerDetent() const { return steps_; }

private:
    bool isDetent(uint8_t ab) const;

    uint8_t steps_;
    bool reverse_;
    uint8_t prev_ = kDetentState;
    int8_t accum_ = 0;
    int32_t raw_ = 0;
    uint32_t invalid_ = 0;
    uint32_t partial_ = 0;
};

}  // namespace hbinput
