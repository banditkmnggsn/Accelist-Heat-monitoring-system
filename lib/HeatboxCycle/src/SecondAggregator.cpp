#include "SecondAggregator.h"

#include <cmath>

namespace hbcycle {

namespace {

// Selisih waktu bertanda: tetap benar saat millis() melewati 2^32
// (49,7 hari), selama selisihnya di bawah 24 hari.
int32_t elapsedMs(uint32_t nowMs, uint32_t sinceMs) {
    return static_cast<int32_t>(nowMs - sinceMs);
}

}  // namespace

void SecondAggregator::reset(uint32_t epochMs) {
    epochMs_ = epochMs;
    index_ = 0;
    clearBucket();
}

bool SecondAggregator::poll(uint32_t nowMs, SecondRecord& out) {
    if (elapsedMs(nowMs, bucketStartMs()) < static_cast<int32_t>(kPeriodMs)) {
        return false;
    }

    out.index = index_;
    out.good = good_;
    out.bad = bad_;
    out.valid = (good_ >= kMinSamples) && (bad_ == 0);
    if (good_ > 0) {
        out.rawMean = sumRaw_ / good_;
        out.tSec = static_cast<double>(index_) + sumOffsetMs_ / good_ / 1000.0;
    } else {
        out.rawMean = NAN;
        out.tSec = static_cast<double>(index_) + 0.5;
    }

    ++index_;
    clearBucket();
    return true;
}

void SecondAggregator::addSample(uint32_t ms, int32_t raw, bool ok) {
    const int32_t offset = elapsedMs(ms, bucketStartMs());
    if (offset < 0 || offset >= static_cast<int32_t>(kPeriodMs)) {
        return;
    }
    if (!ok) {
        if (bad_ < UINT8_MAX) {
            ++bad_;
        }
        return;
    }
    if (good_ == UINT8_MAX) {
        return;
    }
    sumRaw_ += static_cast<double>(raw);
    sumOffsetMs_ += static_cast<double>(offset);
    ++good_;
}

uint32_t SecondAggregator::epochMs() const { return epochMs_; }

// Aritmetika uint32 sengaja dibiarkan membungkus, sama seperti millis().
uint32_t SecondAggregator::bucketStartMs() const { return epochMs_ + index_ * kPeriodMs; }

void SecondAggregator::clearBucket() {
    sumRaw_ = 0.0;
    sumOffsetMs_ = 0.0;
    good_ = 0;
    bad_ = 0;
}

}  // namespace hbcycle
