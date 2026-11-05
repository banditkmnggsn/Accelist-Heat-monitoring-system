#include "EncoderInput.h"

namespace {

// Lookup transisi quadrature 4x. Indeks = (state_lama << 2) | state_baru,
// state = (A << 1) | B. +1 untuk urutan 00->10->11->01 (A mendahului B),
// -1 untuk arah sebaliknya, 0 untuk "tidak berubah" atau transisi ilegal
// (dua bit berubah sekaligus = bounce/terlewat, diabaikan).
constexpr int8_t kTransitionTable[16] = {
    0,  -1, +1, 0,   // dari 00
    +1, 0,  0,  -1,  // dari 01
    -1, 0,  0,  +1,  // dari 10
    0,  +1, -1, 0,   // dari 11
};

}  // namespace

EncoderInput::EncoderInput(const Pins& pins) : pins_(pins) {}

void EncoderInput::begin() {
    pinMode(pins_.a, INPUT_PULLUP);
    pinMode(pins_.b, INPUT_PULLUP);
    pinMode(pins_.sw, INPUT_PULLUP);

    prevState_ = readAB();
    subSteps_ = 0;
    position_ = 0;
    delta_ = 0;

    rawPressed_ = (digitalRead(pins_.sw) == LOW);
    pressed_ = rawPressed_;
    longFired_ = pressed_;  // tombol sudah ditekan saat boot: jangan hasilkan event
    rawChangeMs_ = millis();
    pressStartMs_ = rawChangeMs_;
    event_ = ButtonEvent::None;
}

void EncoderInput::update() {
    const int32_t before = position_;
    updateRotation();
    delta_ = position_ - before;

    updateButton(millis());
}

uint8_t EncoderInput::readAB() const {
    const uint8_t a = (digitalRead(pins_.a) == HIGH) ? 1 : 0;
    const uint8_t b = (digitalRead(pins_.b) == HIGH) ? 1 : 0;
    return static_cast<uint8_t>((a << 1) | b);
}

void EncoderInput::updateRotation() {
    const uint8_t state = readAB();
    if (state == prevState_) {
        return;
    }
    subSteps_ = static_cast<int8_t>(subSteps_ + kTransitionTable[(prevState_ << 2) | state]);
    prevState_ = state;

    if (subSteps_ >= kStepsPerDetent) {
        ++position_;
        subSteps_ = 0;
    } else if (subSteps_ <= -kStepsPerDetent) {
        --position_;
        subSteps_ = 0;
    } else if (state == kDetentState) {
        // Kembali ke detent dengan hitungan tidak genap (ada transisi
        // terlewat). Resync supaya fase tidak bergeser permanen.
        if (subSteps_ >= kHalfDetentSteps) {
            ++position_;
        } else if (subSteps_ <= -kHalfDetentSteps) {
            --position_;
        }
        subSteps_ = 0;
    }
}

void EncoderInput::updateButton(uint32_t nowMs) {
    event_ = ButtonEvent::None;

    const bool raw = (digitalRead(pins_.sw) == LOW);  // aktif LOW
    if (raw != rawPressed_) {
        rawPressed_ = raw;
        rawChangeMs_ = nowMs;
    }

    if (raw != pressed_ && (nowMs - rawChangeMs_) >= kButtonDebounceMs) {
        pressed_ = raw;
        if (pressed_) {
            pressStartMs_ = nowMs;
            longFired_ = false;
        } else if (!longFired_) {
            event_ = ButtonEvent::ShortPress;
        }
    }

    if (pressed_ && !longFired_ && (nowMs - pressStartMs_) > kLongPressMs) {
        longFired_ = true;
        event_ = ButtonEvent::LongPress;  // dikirim saat masih ditahan
    }
}

int32_t EncoderInput::position() const { return position_; }
int32_t EncoderInput::delta() const { return delta_; }

void EncoderInput::resetPosition() {
    position_ = 0;
    subSteps_ = 0;
    delta_ = 0;
}

bool EncoderInput::isPressed() const { return pressed_; }
EncoderInput::ButtonEvent EncoderInput::buttonEvent() const { return event_; }
