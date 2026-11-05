#pragma once

// =====================================================================
//  Debouncer.h — level digital dianggap berubah setelah stabil sekian ms
//  Dipakai tombol rotary dan saklar pintu. Aman terhadap millis() yang
//  membungkus (selisih unsigned).
// =====================================================================

#include <cstdint>

namespace hbinput {

class Debouncer {
public:
    explicit Debouncer(uint32_t stableMs) : stableMs_(stableMs) {}

    void reset(bool level, uint32_t nowMs) {
        raw_ = level;
        state_ = level;
        changedMs_ = nowMs;
    }

    // Return level setelah debounce.
    bool update(bool raw, uint32_t nowMs) {
        if (raw != raw_) {
            raw_ = raw;
            changedMs_ = nowMs;
        }
        if (raw_ != state_ && (nowMs - changedMs_) >= stableMs_) {
            state_ = raw_;
        }
        return state_;
    }

    bool state() const { return state_; }

private:
    uint32_t stableMs_;
    bool raw_ = false;
    bool state_ = false;
    uint32_t changedMs_ = 0;
};

}  // namespace hbinput
