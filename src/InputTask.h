#pragma once

// =====================================================================
//  InputTask.h — task polling 1 kHz di core 1 (BUKAN interrupt)
//
//  Membaca rotary (A/B/SW), saklar pintu, dan menjalankan sequencer
//  buzzer. Event (PUTAR/TEKAN/TAHAN) dikirim lewat antrian ke UI.
//  Interrupt GPIO tidak dipakai supaya tidak berebut dengan ISR video
//  di core yang sama; polling 1 kHz + RC 1 ms di board sudah cukup.
// =====================================================================

#include <Arduino.h>
#include <ButtonGesture.h>
#include <Debouncer.h>
#include <InputEvent.h>
#include <QuadratureDecoder.h>
#include <UiModel.h>

#include <atomic>

#include "Buzzer.h"

class InputTask {
public:
    InputTask();

    // Konfigurasi pin, mulai task. Buzzer ikut diinisialisasi (pin LOW).
    void begin(bool soundOn);

    // ---- untuk UI (loop, core 1) ----
    bool nextEvent(hbinput::InputEvent& out);
    void setHoldTarget(uint32_t ms) { holdTarget_.store(ms); }
    uint32_t holdProgressMs() const { return holdProgress_.load(); }
    bool doorClosed() const { return doorClosed_.load(); }
    Buzzer& buzzer() { return buzzer_; }

    // ---- untuk report serial lama (core 0) ----
    int32_t position() const { return detents_.load(); }
    void resetPosition() { detents_.store(0); }
    // '-' diam, 'P' sedang ditekan, 'S' TEKAN, 'L' TAHAN (sekali per event)
    char takeButtonChar();

private:
    static void entry(void* self);
    void run();
    void poll(uint32_t nowMs);
    uint8_t readAB() const;

    QueueHandle_t queue_ = nullptr;
    Buzzer buzzer_;
    hbinput::QuadratureDecoder decoder_;
    hbinput::Debouncer button_;
    hbinput::Debouncer door_;
    hbinput::ButtonGesture gesture_;

    std::atomic<uint32_t> holdTarget_{0};
    std::atomic<uint32_t> holdProgress_{0};
    std::atomic<bool> doorClosed_{false};
    std::atomic<int32_t> detents_{0};
    std::atomic<char> buttonLatch_{'-'};
    std::atomic<bool> buttonDown_{false};
};
