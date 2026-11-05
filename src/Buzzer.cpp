#include "Buzzer.h"

#include <cmath>

#include "pins.h"

// Rangkaian skematik: BC337 low-side, HIGH = bunyi, diam = LOW (R27
// pull-down). Duty 0 LEDC = LOW = diam. Kalau suatu saat dipakai modul
// aktif-LOW, keluaran LEDC harus dibalik dulu; sampai itu dicegah di sini.
static_assert(BUZZER_PASSIVE, "Firmware ini mengasumsikan buzzer pasif (nada dari LEDC)");
static_assert(!BUZZER_ACTIVE_LOW, "Buzzer aktif-LOW belum didukung (LEDC duty 0 akan menyalakannya)");

namespace {

constexpr uint8_t kIdleLevel = LOW;

}  // namespace

void Buzzer::begin(bool enabled) {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, kIdleLevel);
    enabled_.store(enabled);
}

void Buzzer::play(hbui::Sound sound, bool force) {
    if (!force && !enabled_.load()) {
        return;
    }
    const uint8_t index = static_cast<uint8_t>(sound);
    if (index < static_cast<uint8_t>(hbui::Sound::Count)) {
        pending_.store(index);  // suara baru menggantikan yang sedang bunyi
    }
}

void Buzzer::start(uint8_t sound, uint32_t nowMs) {
    current_ = &sounds::kTable[sound];
    step_ = 0;
    stepStartMs_ = nowMs;
    lastFreq_ = 0xFFFF;  // paksa tulis keluaran pertama
}

void Buzzer::output(uint16_t freq) {
    if (freq == lastFreq_) {
        return;
    }
    lastFreq_ = freq;
    if (freq == 0) {
        if (attached_) {
            ledcWrite(kChannel, 0);  // diam di dalam suara: duty 0 = transistor mati
        }
        return;
    }
    if (!attached_) {
        ledcSetup(kChannel, freq, 10);
        ledcAttachPin(PIN_BUZZER, kChannel);
        attached_ = true;
    }
    ledcWriteTone(kChannel, freq);  // duty 50 %
}

void Buzzer::stop() {
    if (attached_) {
        ledcWrite(kChannel, 0);
        ledcDetachPin(PIN_BUZZER);
        attached_ = false;
    }
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, kIdleLevel);
    current_ = nullptr;
    lastFreq_ = 0;
}

void Buzzer::update(uint32_t nowMs) {
    const uint8_t pending = pending_.exchange(kNone);
    if (pending != kNone) {
        start(pending, nowMs);
    }
    if (current_ == nullptr) {
        return;
    }

    // Lewati langkah yang sudah habis
    uint32_t elapsed = nowMs - stepStartMs_;
    while (step_ < current_->count && elapsed >= current_->steps[step_].ms) {
        stepStartMs_ += current_->steps[step_].ms;
        elapsed = nowMs - stepStartMs_;
        ++step_;
    }
    if (step_ >= current_->count) {
        stop();
        return;
    }

    const ToneStep& s = current_->steps[step_];
    uint16_t freq = s.f0;
    if (s.f0 != 0 && s.f1 != s.f0) {
        const float t = static_cast<float>(elapsed) / static_cast<float>(s.ms);
        freq = static_cast<uint16_t>(static_cast<float>(s.f0) *
                                     std::pow(static_cast<float>(s.f1) / static_cast<float>(s.f0), t));
        freq = static_cast<uint16_t>(freq - freq % 8);  // kurangi tulis ulang timer saat glide
    }
    if (s.gateOn > 0 && (elapsed % (s.gateOn + s.gateOff)) >= s.gateOn) {
        freq = 0;
    }
    output(freq);
}
