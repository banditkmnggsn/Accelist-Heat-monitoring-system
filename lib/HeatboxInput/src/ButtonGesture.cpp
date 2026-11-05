#include "ButtonGesture.h"

namespace hbinput {

void ButtonGesture::begin(bool pressed, uint32_t nowMs) {
    // Ditekan saat start: tunggu dilepas tanpa menghasilkan event.
    state_ = pressed ? State::Done : State::Idle;
    downMs_ = nowMs;
}

Gesture ButtonGesture::update(uint32_t nowMs, bool pressed, bool rotated) {
    switch (state_) {
        case State::Idle:
            if (pressed) {
                state_ = rotated ? State::Cancelled : State::Down;
                downMs_ = nowMs;
            }
            return Gesture::None;

        case State::Down: {
            const uint32_t heldMs = nowMs - downMs_;
            if (rotated) {
                state_ = State::Cancelled;
                return Gesture::None;
            }
            if (!pressed) {
                state_ = State::Idle;
                return (heldMs < pressMaxMs_) ? Gesture::Press : Gesture::None;
            }
            if (holdTargetMs_ > 0 && heldMs >= holdTargetMs_) {
                state_ = State::Done;
                return Gesture::Hold;
            }
            return Gesture::None;
        }

        case State::Cancelled:
        case State::Done:
            if (!pressed) {
                state_ = State::Idle;
            }
            return Gesture::None;
    }
    return Gesture::None;
}

uint32_t ButtonGesture::holdProgressMs(uint32_t nowMs) const {
    if (state_ != State::Down || holdTargetMs_ == 0) {
        return 0;
    }
    const uint32_t heldMs = nowMs - downMs_;
    return (heldMs >= holdBarMs_) ? heldMs : 0;
}

}  // namespace hbinput
