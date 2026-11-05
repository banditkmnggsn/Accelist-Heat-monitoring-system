#pragma once

// =====================================================================
//  InputEvent.h — event dari task input ke UI
// =====================================================================

#include <cstdint>

namespace hbinput {

enum class InputType : uint8_t {
    Rotate,  // steps = +1 (kanan, naik) / -1 (kiri, turun)
    Press,   // TEKAN: dilepas < kPressMaxMs
    Hold,    // TAHAN: target tahan layar aktif tercapai
};

struct InputEvent {
    InputType type;
    int8_t steps;  // hanya untuk Rotate
    uint32_t ms;   // waktu kejadian
};

}  // namespace hbinput
