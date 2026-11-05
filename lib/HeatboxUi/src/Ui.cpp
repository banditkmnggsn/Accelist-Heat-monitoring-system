#include "Ui.h"

#include <app_config.h>

#include <cmath>

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

void Ui::begin(const hbcycle::Settings& settings, const UiSnapshot& snap) {
    settings_ = settings;
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
        case ScreenId::EditProfile: return editProfileScreen_;
        case ScreenId::EditGeneral: return editGeneral_;
        case ScreenId::ServiceMenu: return serviceMenu_;
        case ScreenId::DisplayTest: return displayTest_;
        case ScreenId::InputTest: return inputTest_;
        case ScreenId::SoundTest: return soundTest_;
        case ScreenId::Count: break;
    }
    return home_;
}

UiContext Ui::context(const UiSnapshot& snap) {
    return UiContext{snap, settings_, cmd_, *this, style_, hot_};
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
    UiContext ctx = context(snap);
    k.add(screen(current()).liveKey(ctx));
    return k.h;
}

void Ui::render(Painter& p, const UiSnapshot& snap) {
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
