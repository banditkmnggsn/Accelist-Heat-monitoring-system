#pragma once

// =====================================================================
//  SecondAggregator.h — sampel ADC 10 SPS -> satu nilai per detik
//
//  Nilai per detik = rata-rata sampel yang LOLOS gerbang spike driver.
//  Sengaja bukan angka W firmware: rata-rata bergerak W membuat nilai
//  berurutan saling berkorelasi dan tertunda, sehingga statistik per menit
//  (garis robust, laju) menjadi terlalu optimistis.
//
//  Detik ditutup menurut jam, bukan jumlah sampel: osilator ADS1232 +-3 %
//  memberi 9..11 sampel per detik. Detik tanpa cukup sampel tetap
//  dikeluarkan sebagai tidak sah, supaya celah data terlihat di hilir.
//
//  Pemakaian di loop():
//      while (agg.poll(now, rec)) { ...pakai rec... }
//      if (sampel baru) agg.addSample(sample.ms, sample.raw, sample.ok);
//  poll() harus dipanggil lebih dulu supaya sampel masuk ke detik yang benar.
// =====================================================================

#include <cstdint>

namespace hbcycle {

struct SecondRecord {
    uint32_t index;  // detik ke-n sejak reset(): mencakup [n, n+1) detik
    double tSec;     // rata-rata waktu sampel baik, detik sejak reset(); n + 0,5 kalau kosong
    double rawMean;  // rata-rata raw count sampel baik; NAN kalau tidak ada
    uint8_t good;    // banyak sampel baik
    uint8_t bad;     // banyak sampel buruk (mis. clip)
    bool valid;      // good >= kMinSamples dan bad == 0
};

class SecondAggregator {
public:
    static constexpr uint32_t kPeriodMs = 1000;
    // Minimal separuh dari ~10 sampel per detik. Kurang dari ini berarti
    // ADC macet/reset di tengah detik, dan rata-ratanya tidak mewakili.
    static constexpr uint8_t kMinSamples = 5;

    // Mulai hitungan detik dari epochMs (detik ke-0 = [epoch, epoch + 1 s)).
    void reset(uint32_t epochMs);

    // true kalau detik yang sedang diisi sudah lewat; record-nya di `out`.
    // Kalau loop tertunda beberapa detik, tiap panggilan mengeluarkan satu
    // detik (yang terlewat keluar sebagai tidak sah), jadi panggil dalam while.
    bool poll(uint32_t nowMs, SecondRecord& out);

    // Sampel di luar detik yang sedang diisi diabaikan.
    void addSample(uint32_t ms, int32_t raw, bool ok);

    uint32_t epochMs() const;

private:
    uint32_t bucketStartMs() const;
    void clearBucket();

    uint32_t epochMs_ = 0;
    uint32_t index_ = 0;  // detik yang sedang diisi
    double sumRaw_ = 0.0;
    double sumOffsetMs_ = 0.0;  // jumlah (waktu sampel - awal detik)
    uint8_t good_ = 0;
    uint8_t bad_ = 0;
};

}  // namespace hbcycle
