#pragma once

// =====================================================================
//  Screens.h — semua layar Prompt 1
//    Boot, SensorError, Home (Beranda), History (Riwayat kosong),
//    ProfileSelect, SettingsMenu, EditProfile, EditGeneral,
//    ServiceMenu, DisplayTest, InputTest, SoundTest
//  Layar tes, hasil, riwayat penuh, dan servis lengkap = Prompt 2.
// =====================================================================

#include "Screen.h"

namespace hbui {

// ---------------------------------------------------------------------
class BootScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    void onHold(UiContext&) override {}
    HoldSpec holdSpec(const UiContext&) const override { return {0, ""}; }
    const char* hint(const UiContext&) const override { return "MENYIAPKAN...  MOHON TUNGGU"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    bool animating(const UiContext&) const override { return true; }

private:
    static constexpr uint8_t kRows = 5;
    CheckState rowState(const UiContext& ctx, uint8_t row) const;

    uint32_t startMs_ = 0;
    uint32_t lastRevealMs_ = 0;
    uint8_t revealed_ = 0;     // jumlah baris yang sudah tampil
    uint32_t doneMs_ = 0;      // semua baris selesai
    bool sensorFail_ = false;
    bool netFail_ = false;
};

// ---------------------------------------------------------------------
class SensorErrorScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override { return "TEKAN:CEK ULANG  TAHAN:BERANDA"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    bool rechecking_ = false;
};

// ---------------------------------------------------------------------
class HomeScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH  TEKAN:OK"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

// ---------------------------------------------------------------------
class HistoryScreen : public Screen {
public:
    const char* hint(const UiContext&) const override { return "TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
};

// ---------------------------------------------------------------------
class ProfileSelectScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    static constexpr uint8_t kBackRow = hbcycle::kProfileCount;
    void makeActive(UiContext& ctx);
    uint8_t cursor_ = 0;
};

// ---------------------------------------------------------------------
class SettingsMenuScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

// ---------------------------------------------------------------------
//  Edit nilai, dipakai EditProfile dan EditGeneral.
//  Model kontrol (beda dari desain, lihat KEPUTUSAN-UI.md):
//    PUTAR pilih kotak -> TEKAN mulai edit -> PUTAR ubah (cepat = 5)
//    -> TEKAN simpan (ditolak: blok kuning + pesan, tetap edit)
//    -> TAHAN batal edit. TAHAN di luar edit = kembali.
// ---------------------------------------------------------------------
struct FieldDef {
    const char* label;
    hbcycle::Range range;
    const char* unit;  // ditampilkan setelah angka, mis. HB_DEG "C"
};

class EditValuesScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

protected:
    virtual const char* title(const UiContext& ctx) const = 0;
    virtual uint8_t fieldCount() const = 0;
    virtual const FieldDef& field(uint8_t index) const = 0;
    virtual uint8_t valueOf(const hbcycle::Settings& s, uint8_t index, const UiContext& ctx) const = 0;
    // Tulis nilai ke kandidat lalu validasi kandidat itu.
    virtual hbcycle::SettingsError apply(hbcycle::Settings& candidate, uint8_t index, uint8_t value,
                                         const UiContext& ctx) const = 0;
    virtual uint8_t valueSize() const = 0;

private:
    uint8_t cursor_ = 0;
    bool editing_ = false;
    uint8_t editValue_ = 0;
    hbcycle::SettingsError rejected_ = hbcycle::SettingsError::None;
};

class EditProfileScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext& ctx) const override;
    uint8_t fieldCount() const override { return 2; }
    const FieldDef& field(uint8_t index) const override;
    uint8_t valueOf(const hbcycle::Settings& s, uint8_t index, const UiContext& ctx) const override;
    hbcycle::SettingsError apply(hbcycle::Settings& candidate, uint8_t index, uint8_t value,
                                 const UiContext& ctx) const override;
    uint8_t valueSize() const override { return kT3; }
};

class EditGeneralScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext&) const override { return "UMUM"; }
    uint8_t fieldCount() const override { return 3; }
    const FieldDef& field(uint8_t index) const override;
    uint8_t valueOf(const hbcycle::Settings& s, uint8_t index, const UiContext& ctx) const override;
    hbcycle::SettingsError apply(hbcycle::Settings& candidate, uint8_t index, uint8_t value,
                                 const UiContext& ctx) const override;
    uint8_t valueSize() const override { return kT2; }
};

// ---------------------------------------------------------------------
class ServiceMenuScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

// ---------------------------------------------------------------------
//  UJI TAMPILAN: halaman diganti dengan PUTAR. Dua halaman terakhir
//  adalah varian yang masih diragukan; TEKAN mengganti varian dan
//  langsung berlaku di semua layar (RAM saja, sampai restart).
// ---------------------------------------------------------------------
class DisplayTestScreen : public Screen {
public:
    void onEnter(UiContext&) override { page_ = 0; }
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    enum Page : uint8_t { Palette, Text, IconsLarge, IconsSmall, Margins, Overscan, VarSelect, VarSecondary, PageCount };
    uint8_t page_ = 0;
};

// ---------------------------------------------------------------------
class InputTestScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR/TEKAN:UJI  TAHAN 2 DTK:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    uint32_t presses_ = 0;
    uint32_t fastClicks_ = 0;
    int32_t startDetents_ = 0;
};

// ---------------------------------------------------------------------
class SoundTestScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:BUNYI TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

}  // namespace hbui
