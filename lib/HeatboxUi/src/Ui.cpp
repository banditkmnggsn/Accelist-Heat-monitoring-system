#include "Ui.h"

#include <app_config.h>

#include <cmath>
#include <cstdio>

#include "Widgets.h"

namespace hbui {

namespace {

// FNV-1a sederhana untuk mendeteksi perubahan tampilan
struct KeyHash {
    uint32_t h = 2166136261u;
    void add(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xFFu;
            h *= 16777619u;
        }
    }
};

}  // namespace

Ui::Ui(UiCommands& commands) : cmd_(commands) {}

void Ui::begin(const hbcycle::Settings& settings, const hbcycle::AdvancedSettings& advanced, const UiSnapshot& snap) {
    settings_ = settings;
    advanced_ = advanced;
    depth_ = 1;
    stack_[0] = ScreenId::Boot;
    enterPending_ = true;
    settle(snap);
    dirty_ = true;
}

Screen& Ui::screen(ScreenId id) {
    switch (id) {
        case ScreenId::Boot: return boot_;
        case ScreenId::SensorError: return sensorError_;
        case ScreenId::Home: return home_;
        case ScreenId::History: return history_;
        case ScreenId::ProfileSelect: return profileSelect_;
        case ScreenId::SettingsMenu: return settingsMenu_;
        case ScreenId::ProfileList: return profileList_;
        case ScreenId::EditProfile: return editProfileScreen_;
        case ScreenId::EditGeneral: return editGeneral_;
        case ScreenId::Wifi: return wifi_;
        case ScreenId::WifiScan: return wifiScan_;
        case ScreenId::TextEntry: return textEntry_;
        case ScreenId::DeviceInfo: return deviceInfo_;
        case ScreenId::DevGate: return devGate_;
        case ScreenId::DevMenu: return devMenu_;
        case ScreenId::SensorStatus: return sensorStatus_;
        case ScreenId::ScaleActions: return scaleActions_;
        case ScreenId::Calibration: return calibration_;
        case ScreenId::ScaleCheck: return scaleCheck_;
        case ScreenId::EditMethod: return editMethod_;
        case ScreenId::EditHeater: return editHeater_;
        case ScreenId::EditDisplay: return editDisplay_;
        case ScreenId::LogMemory: return logMemory_;
        case ScreenId::TestRun: return testRun_;
        case ScreenId::Result: return result_;
        case ScreenId::TestError: return testError_;
        case ScreenId::HistoryDetail: return historyDetail_;
        case ScreenId::Count: break;
    }
    return home_;
}

UiContext Ui::context(const UiSnapshot& snap) {
    return UiContext{snap, settings_, advanced_, cmd_, *this, hot_};
}

void Ui::settle(const UiSnapshot& snap) {
    // onEnter bisa langsung berpindah lagi; batasi supaya tidak berputar.
    for (uint8_t guard = 0; enterPending_ && guard < 4; ++guard) {
        enterPending_ = false;
        UiContext ctx = context(snap);
        screen(current()).onEnter(ctx);
    }
    dirty_ = true;
}

// ---- Navigator ----
void Ui::push(ScreenId id) {
    if (depth_ < kMaxDepth) {
        stack_[depth_++] = id;
        enterPending_ = true;
    }
}

void Ui::pop() {
    if (depth_ > 1) {
        --depth_;
        enterPending_ = true;
    }
}

void Ui::popTo(ScreenId id) {
    for (uint8_t d = depth_; d > 0; --d) {
        if (stack_[d - 1] == id) {
            if (d != depth_) {
                depth_ = d;
                enterPending_ = true;
            }
            return;
        }
    }
}

void Ui::goHome() {
    depth_ = 1;
    stack_[0] = ScreenId::Home;
    enterPending_ = true;
}

void Ui::replace(ScreenId id) {
    stack_[depth_ - 1] = id;
    enterPending_ = true;
}

void Ui::openDialog(const DialogSpec& spec) {
    dialog_ = spec;
    dialogOpen_ = true;
    dialogOption_ = 0;  // pilihan awal selalu yang aman (batal)
    dirty_ = true;
}

void Ui::closeDialog(const UiSnapshot& snap, bool accepted) {
    dialogOpen_ = false;
    dirty_ = true;
    UiContext ctx = context(snap);
    screen(current()).onDialogResult(ctx, dialog_.id, accepted);
    settle(snap);
}

// ---- input ----
void Ui::handleDialog(const hbinput::InputEvent& event, const UiSnapshot& snap) {
    switch (event.type) {
        case hbinput::InputType::Rotate:
            if (dialog_.kind == DialogKind::Confirm) {
                dialogOption_ = (event.steps > 0) ? 1 : 0;
            }
            break;
        case hbinput::InputType::Press:
            cmd_.playSound(Sound::Click, false);
            closeDialog(snap, dialog_.kind == DialogKind::Info || dialogOption_ == 1);
            break;
        case hbinput::InputType::Hold:
            cmd_.playSound(Sound::HoldDone, false);
            closeDialog(snap, false);
            break;
    }
}

void Ui::handle(const hbinput::InputEvent& event, const UiSnapshot& snap) {
    dirty_ = true;
    if (snap.net.ota == OtaState::Updating) {
        return;  // update firmware: semua input diabaikan
    }
    if (dialogOpen_) {
        handleDialog(event, snap);
        return;
    }
    UiContext ctx = context(snap);
    Screen& s = screen(current());
    switch (event.type) {
        case hbinput::InputType::Rotate: {
            const bool fast = (event.ms - lastRotateMs_) < app::input::kAccelWindowMs;
            lastRotateMs_ = event.ms;
            s.onRotate(ctx, event.steps, fast);
            break;
        }
        case hbinput::InputType::Press:
            // Suara bawaan; layar boleh menimpanya dengan suara lain.
            cmd_.playSound(Sound::Click, false);
            s.onPress(ctx);
            break;
        case hbinput::InputType::Hold:
            cmd_.playSound(Sound::HoldDone, false);
            s.onHold(ctx);
            break;
    }
    settle(snap);
}

HoldSpec Ui::holdSpec(const UiSnapshot& snap) {
    if (snap.net.ota == OtaState::Updating) {
        return {0, ""};
    }
    if (dialogOpen_) {
        return {app::input::kHoldActionMs, "TUTUP"};
    }
    UiContext ctx = context(snap);
    return screen(current()).holdSpec(ctx);
}

void Ui::update(const UiSnapshot& snap) {
    // Banner panas dengan histeresis
    if (snap.tempOk && !std::isnan(snap.tempC)) {
        if (!hot_ && snap.tempC > app::ui::kHotBannerOnC) hot_ = true;
        if (hot_ && snap.tempC < app::ui::kHotBannerOffC) hot_ = false;
    } else {
        hot_ = false;
    }

    if (dialogOpen_ && dialog_.closeWhenDoorClosed && snap.doorClosed) {
        // Pintu sudah ditutup: dialog hilang sendiri, tes TIDAK mulai otomatis.
        dialogOpen_ = false;
        dirty_ = true;
    }

    UiContext ctx = context(snap);
    screen(current()).tick(ctx);
    settle(snap);
    animating_ = !dialogOpen_ && screen(current()).animating(context(snap));

    const uint32_t key = viewKey(snap);
    if (key != lastKey_) {
        lastKey_ = key;
        dirty_ = true;
    }
}

uint32_t Ui::viewKey(const UiSnapshot& snap) {
    KeyHash k;
    k.add(static_cast<uint32_t>(current()) | (static_cast<uint32_t>(depth_) << 8) |
          (dialogOpen_ ? 1u << 16 : 0u) | (hot_ ? 1u << 17 : 0u) | (snap.doorClosed ? 1u << 18 : 0u) |
          (snap.clockSynced ? 1u << 19 : 0u) | (snap.scaleOk ? 1u << 20 : 0u) |
          (snap.tempOk ? 1u << 21 : 0u) | (snap.testRunning ? 1u << 22 : 0u));
    k.add(static_cast<uint32_t>(snap.wifi) | (static_cast<uint32_t>(snap.scaleCheck) << 8) |
          (static_cast<uint32_t>(snap.tempCheck) << 16));
    k.add(static_cast<uint32_t>(snap.hour) * 60u + snap.minute);
    k.add(hot_ ? static_cast<uint32_t>(std::lround(snap.tempC)) : 0u);
    k.add(snap.holdProgressMs / 50u);  // bar tahan ~20 langkah per detik
    k.add(static_cast<uint32_t>(snap.net.ota) | (static_cast<uint32_t>(snap.net.otaPct) << 8));
    UiContext ctx = context(snap);
    k.add(screen(current()).liveKey(ctx));
    return k.h;
}

void Ui::drawOta(Painter& p, const UiSnapshot& snap) const {
    // Layar penuh selama firmware baru ditulis (OTA dari PlatformIO)
    p.clear();
    p.text(layout::kMargin, layout::kTitleY, "UPDATE", kT2, color::kText);
    p.fill(layout::kMargin, 60, layout::kContentW, 40, color::kWarn);
    p.icon(Icon::Warn, 12, 76, color::kText, true);
    p.text(36, 66, "JANGAN MATIKAN", kT2, color::kText);
    p.text(36, 86, "ALAT SAMPAI SELESAI", kT1, color::kText);
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(snap.net.otaPct));
    drawProgress(p, layout::kMargin, 120, layout::kContentW, "FIRMWARE BARU LEWAT WIFI", pct,
                 static_cast<float>(snap.net.otaPct) / 100.0f, color::kAccent);
    p.text(layout::kMargin, 156, "ALAT MULAI ULANG SENDIRI SETELAH", kT1, color::kText);
    p.text(layout::kMargin, 168, "UPDATE SELESAI. LAMPU MATI.", kT1, color::kText);
    drawHint(p, "TOMBOL TIDAK AKTIF SELAMA UPDATE", 0, {0, ""});
    p.clearParking();
}

void Ui::render(Painter& p, const UiSnapshot& snap) {
    if (snap.net.ota == OtaState::Updating) {
        drawOta(p, snap);
        dirty_ = false;
        return;
    }
    UiContext ctx = context(snap);
    const Screen& s = screen(current());
    p.clear();
    s.draw(p, ctx);
    const HoldSpec hold = holdSpec(snap);
    if (dialogOpen_) {
        drawDialog(p, dialog_, dialogOption_);
        const char* hint = (dialog_.kind == DialogKind::Confirm) ? "PUTAR:PILIH  TEKAN:OK  TAHAN:BATAL"
                                                                  : dialog_.hint;
        drawHint(p, hint, snap.holdProgressMs, hold);
    } else {
        const char* hint = s.hint(ctx);
        if (hint != nullptr) {
            drawHint(p, hint, snap.holdProgressMs, hold);
        }
    }
    p.clearParking();
    dirty_ = false;
}

}  // namespace hbui
