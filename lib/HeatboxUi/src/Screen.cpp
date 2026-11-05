#include "Screen.h"

#include <app_config.h>

namespace hbui {

// Bawaan semua layar: TAHAN 2 dtk = KEMBALI
HoldSpec Screen::holdSpec(const UiContext&) const { return {app::input::kHoldActionMs, "KEMBALI"}; }

}  // namespace hbui
