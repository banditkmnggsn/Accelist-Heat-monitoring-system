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
    // ---- PENGATURAN (03-20) ----
    SettingsMenu,
    ProfileList,    // SET PROFIL: daftar profil 1-3
    EditProfile,
    EditGeneral,
    Wifi,           // WIFI & JAM (03-40.9)
    WifiScan,       // daftar SSID hasil pindai
    TextEntry,      // ketik SSID / sandi per karakter (keputusan 8.2)
    DeviceInfo,     // INFO PERANGKAT (03-40.10)
    // ---- PENGATURAN > OPSI PENGEMBANG (tim IT; dulu menu SERVIS 03-40) ----
    DevGate,        // masuk: TAHAN 5 dtk
    DevMenu,
    SensorStatus,   // 03-40.2
    ScaleActions,   // 03-40.3
    Calibration,    // 03-40.4 (1 titik, keputusan 8.3)
    ScaleCheck,     // 03-40.5
    EditMethod,     // 03-40.6
    EditHeater,     // kontrol PI lampu (uji U3)
    EditDisplay,    // 03-40.7
    LogMemory,      // 03-40.8
    // ---- tes ----
    TestRun,        // PRE-HEAT .. STOP (02-04 .. 02-09, 02-12)
    Result,         // HASIL 7 halaman (02-10)
    TestError,      // ERROR tanpa hasil (02-11)
    HistoryDetail,  // RIWAYAT #id 7 halaman (03-30.2-8)
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

// Isian layar WiFi yang sedang disusun (pindai -> ketik -> sambung)
struct WifiDraft {
    char ssid[33];
    char pass[65];
    bool forPassword;  // TextEntry mengisi sandi (true) atau SSID (false)
    bool freshScan;    // WifiScan memindai ulang saat dibuka dari WIFI & JAM
};

class Navigator {
public:
    virtual void push(ScreenId id) = 0;
    virtual void pop() = 0;
    // Kembali sampai layar `id` di atas tumpukan (tidak berbuat apa-apa kalau tidak ada)
    virtual void popTo(ScreenId id) = 0;
    virtual void goHome() = 0;          // tumpukan dikosongkan, Beranda
    virtual void replace(ScreenId id) = 0;
    virtual void openDialog(const DialogSpec& spec) = 0;
    virtual uint8_t editProfileIndex() const = 0;
    virtual void setEditProfileIndex(uint8_t index) = 0;
    virtual uint32_t selectedTest() const = 0;  // RIWAYAT -> detail
    virtual void setSelectedTest(uint32_t id) = 0;
    virtual WifiDraft& wifiDraft() = 0;

protected:
    ~Navigator() = default;
};

struct UiContext {
    const UiSnapshot& snap;
    hbcycle::Settings& settings;          // salinan kerja; disimpan lewat cmd.saveSettings
    hbcycle::AdvancedSettings& advanced;  // salinan kerja; disimpan lewat cmd.saveAdvanced
    UiCommands& cmd;
    Navigator& nav;
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
