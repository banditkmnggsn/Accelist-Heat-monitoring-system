#include "MinuteBuilder.h"

#include <cmath>

#include "DryingMath.h"

namespace hbcycle {

static_assert(MinuteBuilder::kSecondsPerMinute <= drying::kMaxLevelPoints,
              "satu menit harus muat di buffer robustLevel");

void MinuteBuilder::reset() {
    started_ = false;
    minute_ = 0;
    count_ = 0;
}

bool MinuteBuilder::add(const SecondRecord& second, double massMg, MinuteRecord& out) {
    const uint32_t minute = second.index / kSecondsPerMinute;
    if (!started_) {
        started_ = true;
        minute_ = minute;
    }

    bool closed = false;
    if (minute != minute_) {
        // Detik melompat ke menit lain (seharusnya tidak terjadi: aggregator
        // mengeluarkan setiap detik). Tutup menit berjalan lebih dulu.
        out = close();
        closed = true;
        minute_ = minute;
    }

    if (second.valid && std::isfinite(massMg) && count_ < kSecondsPerMinute) {
        tsMin_[count_] = second.tSec / 60.0;
        massMg_[count_] = massMg;
        ++count_;
    }

    // Detik terakhir menit ini. Kalau panggilan ini sudah menutup menit
    // sebelumnya, penutupan menit ini menunggu detik berikutnya (yang
    // memicu cabang "minute != minute_" di atas).
    if (!closed && second.index % kSecondsPerMinute == kSecondsPerMinute - 1) {
        out = close();
        closed = true;
        minute_ = minute + 1;
    }
    return closed;
}

MinuteRecord MinuteBuilder::close() {
    MinuteRecord record;
    record.index = minute_;
    record.tMidMin = static_cast<double>(minute_) + 0.5;
    record.massMg = NAN;
    record.slopeMgPerMin = NAN;
    record.spreadMg = NAN;
    record.uncertaintyMg = NAN;
    record.secondsValid = count_;
    record.secondsUsed = 0;
    record.valid = false;

    // Garis tetap dihitung walau detiknya kurang, sebagai diagnostik di log;
    // hanya `valid` yang menentukan apakah menit ini boleh dipakai.
    if (count_ >= 2) {
        const drying::Level level = drying::robustLevel(tsMin_, massMg_, count_, record.tMidMin);
        if (level.ok) {
            record.massMg = level.level;
            record.slopeMgPerMin = level.slope;
            record.spreadMg = level.scale;
            record.secondsUsed = static_cast<uint8_t>(level.used);
            record.uncertaintyMg = level.scale / std::sqrt(static_cast<double>(level.used));
            record.valid = (level.used >= kMinUsedSeconds) && (level.scale <= kMaxSpreadMg);
        }
    }

    count_ = 0;
    return record;
}

}  // namespace hbcycle
