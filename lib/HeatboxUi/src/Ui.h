#pragma once

// =====================================================================
//  Ui.h — manajer layar: tumpukan layar, dialog modal, event input,
//  dan keputusan kapan perlu menggambar ulang.
//
//  Dipanggil dari satu task saja (loop() di core 1). Tidak menggambar
//  sendiri ke video: render() menerima Painter, sehingga sama persis
//  dipakai di ESP32 dan di perender PNG PC (tools/ui_render).
// =====================================================================

#include <InputEvent.h>

#include "Screens.h"

namespace hbui {

class Ui : public Navigator {
public:
    explicit Ui(UiCommands& commands);

    void begin(const hbcycle::Settings& settings, const UiSnapshot& snap);

    void handle(const hbinput::InputEvent& event, const UiSnapshot& snap);
    // Tiap loop: timer layar, dialog yang menutup sendiri, deteksi perubahan.
    void update(const UiSnapshot& snap);

    bool needsRender() const { return dirty_ || animating_; }
    void render(Painter& p, const UiSnapshot& snap);

    // Target & label tombol tahan untuk task input
    HoldSpec holdSpec(const UiSnapshot& snap);

    ScreenId current() const { return stack_[depth_ - 1]; }
    const UiStyle& style() const { return style_; }

    // ---- Navigator ----
    void push(ScreenId id) override;
    void pop() override;
    void goHome() override;
    void replace(ScreenId id) override;
    void openDialog(const DialogSpec& spec) override;
    uint8_t editProfileIndex() const override { return editProfile_; }
    void setEditProfileIndex(uint8_t index) override { editProfile_ = index; }

private:
    static constexpr uint8_t kMaxDepth = 8;

    UiContext context(const UiSnapshot& snap);
    Screen& screen(ScreenId id);
    void settle(const UiSnapshot& snap);  // onEnter untuk layar yang baru tampil
    void handleDialog(const hbinput::InputEvent& event, const UiSnapshot& snap);
    void closeDialog(const UiSnapshot& snap, bool accepted);
    uint32_t viewKey(const UiSnapshot& snap);

    UiCommands& cmd_;
    hbcycle::Settings settings_{};
    UiStyle style_{};

    ScreenId stack_[kMaxDepth] = {ScreenId::Boot};
    uint8_t depth_ = 1;
    bool enterPending_ = true;
    uint8_t editProfile_ = 0;

    bool dialogOpen_ = false;
    DialogSpec dialog_{};
    uint8_t dialogOption_ = 0;

    bool hot_ = false;
    bool dirty_ = true;
    bool animating_ = false;
    uint32_t lastKey_ = 0;
    uint32_t lastRotateMs_ = 0;

    BootScreen boot_;
    SensorErrorScreen sensorError_;
    HomeScreen home_;
    HistoryScreen history_;
    ProfileSelectScreen profileSelect_;
    SettingsMenuScreen settingsMenu_;
    EditProfileScreen editProfileScreen_;
    EditGeneralScreen editGeneral_;
    ServiceMenuScreen serviceMenu_;
    DisplayTestScreen displayTest_;
    InputTestScreen inputTest_;
    SoundTestScreen soundTest_;
};

}  // namespace hbui
