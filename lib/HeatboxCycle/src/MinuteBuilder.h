#pragma once

// =====================================================================
//  MinuteBuilder.h — 60 nilai per detik -> satu titik per menit
//
//  Nilai menit = garis robust (drying::robustLevel) lewat detik-detik sah
//  menit itu, dibaca TEPAT di tengah menit. Garis, bukan rata-rata, karena
//  massa sedang turun di dalam menit: rata-rata menjadi bias kalau sebagian
//  detik dibuang secara tidak simetris (sentuhan, getaran).
//
//  Menit ke-k mencakup detik 60k..60k+59 dari SecondAggregator dengan epoch
//  yang sama, jadi waktu tengahnya k + 0,5 menit sejak epoch.
// =====================================================================

#include <cstdint>

#include "SecondAggregator.h"

namespace hbcycle {

struct MinuteRecord {
    uint32_t index;        // menit ke-k sejak epoch
    double tMidMin;        // k + 0,5
    double massMg;         // nilai garis robust di tengah menit; NAN kalau < 2 detik sah
    double slopeMgPerMin;  // kemiringan di dalam menit (diagnostik)
    double spreadMg;       // sebaran robust setara SD
    double uncertaintyMg;  // spreadMg / sqrt(secondsUsed)
    uint8_t secondsValid;  // detik sah yang masuk
    uint8_t secondsUsed;   // detik yang dipakai garis akhir
    bool valid;            // secondsUsed >= kMinUsedSeconds dan spreadMg <= kMaxSpreadMg
};

class MinuteBuilder {
public:
    static constexpr uint8_t kSecondsPerMinute = 60;
    // Sampai 1/3 menit boleh terganggu. Lebih dari itu, garisnya tidak
    // lagi mewakili menit tersebut.
    static constexpr uint8_t kMinUsedSeconds = 40;
    // Noise nilai per detik ~1-2 mg. Menit yang memuat perubahan beban
    // (beban diletakkan/diangkat, tutup dibuka) punya sebaran ratusan sampai
    // ribuan mg -- di log 5 Okt 2026: 643 dan 4545 mg -- dan tidak boleh
    // menjadi titik kurva. 10 mg ~ 7x noise per detik; disetel ulang setelah
    // log 10 g dianalisis.
    static constexpr double kMaxSpreadMg = 10.0;

    void reset();

    // Satu detik dari SecondAggregator. massMg hanya dipakai kalau
    // second.valid. true kalau satu menit selesai; hasilnya di `out`.
    bool add(const SecondRecord& second, double massMg, MinuteRecord& out);

private:
    MinuteRecord close();

    bool started_ = false;
    uint32_t minute_ = 0;
    double tsMin_[kSecondsPerMinute] = {};
    double massMg_[kSecondsPerMinute] = {};
    uint8_t count_ = 0;
};

}  // namespace hbcycle
