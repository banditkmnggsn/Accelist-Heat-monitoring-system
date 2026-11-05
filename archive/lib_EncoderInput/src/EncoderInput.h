#pragma once

// =====================================================================
//  EncoderInput.h — rotary encoder EC11 + push button, POLLING
//
//  Sengaja tanpa attachInterrupt: nanti berjalan bersama library video
//  composite yang sensitif timing. update() harus dipanggil sesering
//  mungkin (tiap loop) supaya tidak ada transisi A/B yang terlewat.
// =====================================================================

#include <Arduino.h>

class EncoderInput {
public:
    struct Pins {
        uint8_t a;
        uint8_t b;
        uint8_t sw;
    };

    enum class ButtonEvent : uint8_t { None, ShortPress, LongPress };

    // EC11 20 detent / 20 pulsa: 1 detent = 1 siklus quadrature = 4 transisi
    static constexpr int8_t kStepsPerDetent = 4;
    // Posisi diam (detent) dengan pull-up: A=1, B=1
    static constexpr uint8_t kDetentState = 0x03;
    // Resync di detent: sisa >= setengah detent dihitung satu langkah
    static constexpr int8_t kHalfDetentSteps = 2;

    static constexpr uint32_t kButtonDebounceMs = 20;
    static constexpr uint32_t kLongPressMs = 800;  // long press: ditahan > 800 ms

    explicit EncoderInput(const Pins& pins);

    void begin();
    void update();

    int32_t position() const;  // dalam detent
    int32_t delta() const;     // perubahan posisi pada update() terakhir
    void resetPosition();

    bool isPressed() const;              // status tombol setelah debounce
    ButtonEvent buttonEvent() const;     // event dari update() terakhir

private:
    uint8_t readAB() const;
    void updateRotation();
    void updateButton(uint32_t nowMs);

    Pins pins_;

    uint8_t prevState_ = kDetentState;
    int8_t subSteps_ = 0;
    int32_t position_ = 0;
    int32_t delta_ = 0;

    bool rawPressed_ = false;
    bool pressed_ = false;
    bool longFired_ = false;
    uint32_t rawChangeMs_ = 0;
    uint32_t pressStartMs_ = 0;
    ButtonEvent event_ = ButtonEvent::None;
};
