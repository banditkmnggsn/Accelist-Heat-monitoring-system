#include "QuadratureDecoder.h"

namespace hbinput {

namespace {

// Indeks = (state_lama << 2) | state_baru, state = (A << 1) | B.
// +1 untuk urutan 00->10->11->01->00 (A mendahului B), -1 kebalikannya,
// 0 untuk "tidak berubah" atau lompatan dua bit.
constexpr int8_t kTransition[16] = {
    0,  -1, +1, 0,   // dari 00
    +1, 0,  0,  -1,  // dari 01
    -1, 0,  0,  +1,  // dari 10
    0,  +1, -1, 0,   // dari 11
};

}  // namespace

QuadratureDecoder::QuadratureDecoder(uint8_t stepsPerDetent, bool reverse)
    : steps_(stepsPerDetent == 2 ? 2 : 4), reverse_(reverse) {}

void QuadratureDecoder::reset(uint8_t ab) {
    prev_ = static_cast<uint8_t>(ab & 0x03);
    accum_ = 0;
}

bool QuadratureDecoder::isDetent(uint8_t ab) const {
    return ab == kDetentState || (steps_ == 2 && ab == 0x00);
}

int8_t QuadratureDecoder::update(uint8_t ab) {
    ab = static_cast<uint8_t>(ab & 0x03);
    if (ab == prev_) {
        return 0;
    }
    const int8_t delta = kTransition[(prev_ << 2) | ab];
    prev_ = ab;
    if (delta == 0) {
        ++invalid_;
    } else {
        raw_ += delta;
        accum_ = static_cast<int8_t>(accum_ + delta);
    }
    if (!isDetent(ab)) {
        return 0;
    }

    int8_t step = 0;
    if (accum_ == static_cast<int8_t>(steps_)) {
        step = 1;
    } else if (accum_ == -static_cast<int8_t>(steps_)) {
        step = -1;
    } else if (accum_ != 0 || delta == 0) {
        ++partial_;
    }
    accum_ = 0;
    return reverse_ ? static_cast<int8_t>(-step) : step;
}

}  // namespace hbinput
