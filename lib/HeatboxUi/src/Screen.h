#pragma once

// =====================================================================
//  Screen.h — kerangka layar: id, konteks, dialog, dan kelas dasar
//
//  Kontrol (docs/ui/KONTROL-ROTARY.md):
//    PUTAR = pindah pilihan / ubah nilai, TEKAN = pilih / OK / simpan,
//    TAHAN = aksi layar (holdSpec): bawaan 2 dtk KEMBALI.
// =====================================================================

#include <CycleSettings.h>

#include "Painter.h"
#include "UiModel.h"

namespace hbui {

enum class ScreenId : uint8_t {
    Boot,
    SensorError,
    Home,
    History,
    ProfileSelect,
    SettingsMenu,
    EditProfile,
    EditGeneral,
    ServiceMenu,
    DisplayTest,
    InputTest,
    SoundTest,
    Count,
};

// Aksi tombol tahan pada layar aktif. ms = 0: tidak ada aksi, bar tidak tampil.
struct HoldSpec {
    uint32_t ms;
    const char* label;  // ditampilkan di bar progres, mis. "KEMBALI..."
};

enum class DialogKind : uint8_t { Info, Confirm };

struct DialogSpec {
    uint8_t id;  // dikembalikan ke layar lewat onDialogResult
    DialogKind kind;
    const char* title;
    const char* lines[3];  // isi T1, boleh nullptr
    const char* hint;      // Info: petunjuk tutup; Confirm: diabaikan
    const char* options[2];  // Confirm: {batal, setuju}; pilihan awal = batal
    bool closeWhenDoorClosed;
};

class Navigator {
public:
    virtual void push(ScreenId id) = 0;
    virtual void pop() = 0;
    virtual void goHome() = 0;          // tumpukan dikosongkan, Beranda
    virtual void replace(ScreenId id) = 0;
    virtual void openDialog(const DialogSpec& spec) = 0;
    virtual uint8_t editProfileIndex() const = 0;
    virtual void setEditProfileIndex(uint8_t index) = 0;

protected:
    ~Navigator() = default;
};

struct UiContext {
    const UiSnapshot& snap;
    hbcycle::Settings& settings;  // salinan kerja; disimpan lewat cmd.saveSettings
    UiCommands& cmd;
    Navigator& nav;
    UiStyle& style;
    bool hot;  // suhu di atas ambang banner panas (dengan histeresis)
};

class Screen {
public:
    virtual ~Screen() {}

    virtual void onEnter(UiContext&) {}
    virtual void onRotate(UiContext&, int8_t /*steps*/, bool /*fast*/) {}
    virtual void onPress(UiContext&) {}
    virtual void onHold(UiContext& ctx) { ctx.nav.pop(); }
    virtual void onDialogResult(UiContext&, uint8_t /*id*/, bool /*accepted*/) {}
    // Dipanggil tiap loop UI: timer, perpindahan otomatis.
    virtual void tick(UiContext&) {}

    virtual HoldSpec holdSpec(const UiContext& ctx) const;
    virtual const char* hint(const UiContext& ctx) const = 0;
    virtual void draw(Painter& p, const UiContext& ctx) const = 0;

    // Nilai yang kalau berubah membuat layar perlu digambar ulang (di luar
    // header/banner yang dicek Ui sendiri). Bawaan: tidak ada.
    virtual uint32_t liveKey(const UiContext&) const { return 0; }
    // true = gambar ulang tiap kesempatan (animasi), tetap maks ~10 fps.
    virtual bool animating(const UiContext&) const { return false; }
};

}  // namespace hbui
