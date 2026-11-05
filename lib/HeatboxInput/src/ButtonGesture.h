#pragma once

// =====================================================================
//  ButtonGesture.h — TEKAN / TAHAN dari tombol rotary yang sudah di-debounce
//
//  Aturan (docs/ui/KONTROL-ROTARY.md):
//    - TEKAN : dilepas sebelum pressMaxMs.
//    - TAHAN : masih ditahan saat holdTargetMs tercapai. Aksi terjadi
//              TEPAT di batas itu, lalu tombol harus dilepas dulu sebelum
//              event apa pun berikutnya. Target ditentukan layar aktif
//              (2 dtk kembali/mulai, 5 dtk servis, 0 = tidak ada aksi).
//    - Putar sambil menekan = batal: tidak ada TEKAN maupun TAHAN sampai
//      tombol dilepas (putarannya sendiri diabaikan oleh pemanggil).
//    - Tombol yang sudah ditekan saat start tidak menghasilkan event.
//    - Lepas di antara pressMaxMs dan target = tidak ada event.
// =====================================================================

#include <cstdint>

namespace hbinput {

enum class Gesture : uint8_t { None, Press, Hold };

class ButtonGesture {
public:
    ButtonGesture(uint32_t pressMaxMs, uint32_t holdBarMs)
        : pressMaxMs_(pressMaxMs), holdBarMs_(holdBarMs) {}

    void begin(bool pressed, uint32_t nowMs);

    // 0 = tombol tahan tidak punya aksi di layar ini (bar tidak muncul).
    void setHoldTarget(uint32_t ms) { holdTargetMs_ = ms; }

    // pressed: level setelah debounce. rotated: ada klik putar di tick ini.
    Gesture update(uint32_t nowMs, bool pressed, bool rotated);

    // Tombol sedang turun (termasuk setelah batal / setelah aksi TAHAN).
    // Selama true, pemanggil mengabaikan putaran.
    bool isDown() const { return state_ != State::Idle; }
    bool wasCancelled() const { return state_ == State::Cancelled; }

    // Lama ditahan untuk bar progres. 0 kalau bar tidak boleh tampil:
    // belum mencapai holdBarMs, dibatalkan, sudah beraksi, atau tanpa target.
    uint32_t holdProgressMs(uint32_t nowMs) const;

private:
    enum class State : uint8_t { Idle, Down, Cancelled, Done };

    uint32_t pressMaxMs_;
    uint32_t holdBarMs_;
    uint32_t holdTargetMs_ = 0;
    State state_ = State::Idle;
    uint32_t downMs_ = 0;
};

}  // namespace hbinput
