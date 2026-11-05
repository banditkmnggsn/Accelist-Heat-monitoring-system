#pragma once

// =====================================================================
//  Buzzer.h — sequencer nada non-blocking untuk buzzer pasif (GPIO12)
//
//  - Nada dari LEDC (timer PWM sendiri, clock APB). Tidak memakai I2S,
//    DAC, maupun APLL milik library video, jadi tidak bentrok.
//  - play() boleh dipanggil dari task UI; update() dipanggil task input
//    (1 kHz, core 1). Tidak ada delay(); tone() Arduino tidak dipakai.
//  - Setelah tiap suara: LEDC dilepas dan pin dipaksa OUTPUT LOW
//    (GPIO12 = strapping, wajib LOW; transistor mati).
// =====================================================================

#include <Arduino.h>
#include <UiModel.h>

#include <atomic>

#include "sounds.h"

class Buzzer {
public:
    // Pin OUTPUT LOW sebelum suara apa pun.
    void begin(bool enabled);
    void setEnabled(bool enabled) { enabled_.store(enabled); }

    // Thread-safe. force = bunyi walau suara dimatikan (UJI SUARA).
    void play(hbui::Sound sound, bool force);

    // Dipanggil tiap ~1 ms dari task input.
    void update(uint32_t nowMs);

private:
    static constexpr uint8_t kChannel = 0;   // LEDC kanal 0 (timer 0)
    static constexpr uint8_t kNone = 0xFF;

    void start(uint8_t sound, uint32_t nowMs);
    void output(uint16_t freq);
    void stop();

    std::atomic<bool> enabled_{true};
    std::atomic<uint8_t> pending_{kNone};

    // Hanya disentuh task input
    const SoundDef* current_ = nullptr;
    uint8_t step_ = 0;
    uint32_t stepStartMs_ = 0;
    uint16_t lastFreq_ = 0;
    bool attached_ = false;
};
