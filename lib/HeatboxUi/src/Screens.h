#pragma once

// =====================================================================
//  Screens.h — semua layar
//    Operator : Boot, SensorError, Home (Beranda), ProfileSelect,
//               TestRun, Result, TestError, History, HistoryDetail
//    PENGATURAN (ScreensSettings / ScreensNetwork): menu, SET PROFIL,
//               edit profil, UMUM, WIFI & JAM, ketik SSID/sandi, INFO
//    OPSI PENGEMBANG (ScreensDev, tim IT): status sensor, timbangan,
//               kalibrasi, cek timbangan, parameter metode, kontrol
//               pemanas, tampilan, memori log
//  Menu SERVIS lama (uji tampilan/input/suara) sudah dihapus 27 Nov 2026.
// =====================================================================

#include "Screen.h"
#include "Widgets.h"

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
    void tick(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    // TAHAN di Beranda tidak berbuat apa-apa (menu SERVIS dihapus 27 Nov 2026)
    void onHold(UiContext&) override {}
    HoldSpec holdSpec(const UiContext&) const override { return {0, ""}; }
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH  TEKAN:OK"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
    uint32_t interruptShownId_ = 0;
    char interruptTitle_[32] = "";
};

// ---------------------------------------------------------------------
//  RIWAYAT (03-30.1): daftar tes tersimpan, terbaru di atas
// ---------------------------------------------------------------------
class HistoryScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    static constexpr uint8_t kMax = 40;
    static constexpr uint8_t kRows = 6;
    hbcycle::HistoryEntry entries_[kMax] = {};
    uint8_t count_ = 0;
    uint8_t cursor_ = 0;
    uint8_t top_ = 0;
};

// ---------------------------------------------------------------------
//  Halaman hasil, dipakai HASIL (tes baru) dan RIWAYAT #id
// ---------------------------------------------------------------------
constexpr uint8_t kResultPages = 7;
// top = y isi pertama (76 di bawah bar tahap, 62 di riwayat)
void drawResultPage(Painter& p, const UiContext& ctx, const hbcycle::CycleResult& r, uint8_t page, int16_t top);
// Banner catatan hasil (bendera) untuk halaman WAKTU & STATUS; false kalau tidak ada
bool resultNote(const hbcycle::CycleResult& r, char (&out)[40]);

class HistoryDetailScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    const char* hint(const UiContext&) const override { return "PUTAR:HALAMAN  TAHAN:RIWAYAT"; }
    HoldSpec holdSpec(const UiContext& ctx) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    hbcycle::CycleResult result_{};
    bool loaded_ = false;
    uint8_t page_ = 0;
};

// ---------------------------------------------------------------------
//  TES BERJALAN: satu layar mengikuti fase siklus (snap.cycle.phase)
//    PRE-HEAT (02-04), TARE (02-05), MUAT SAMPEL (02-06), PULIH & W0
//    (02-07), PEMANASAN 4 halaman (02-08), STOP (02-09).
//    TAHAN 3 dtk = batal (dialog terisi 02-12). Selesai -> HASIL / ERROR.
// ---------------------------------------------------------------------
class TestRunScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext& ctx) const override;
    const char* hint(const UiContext& ctx) const override;
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    void drawPreheat(Painter& p, const UiContext& ctx) const;
    void drawTare(Painter& p, const UiContext& ctx) const;
    void drawLoad(Painter& p, const UiContext& ctx) const;
    void drawRecover(Painter& p, const UiContext& ctx, bool locked) const;
    void drawHeat(Painter& p, const UiContext& ctx) const;
    void drawStop(Painter& p, const UiContext& ctx) const;

    uint32_t enterMs_ = 0;
    uint32_t heatEnterMs_ = 0;
    hbcycle::Phase lastPhase_ = hbcycle::Phase::Idle;
    uint8_t page_ = 0;
    hbcycle::QuietValue quiet_;
    float shownSampleMg_ = 0.0f;
};

class ResultScreen : public Screen {
public:
    void onEnter(UiContext&) override { page_ = 0; }
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext& ctx) const override;
    const char* hint(const UiContext&) const override { return "PUTAR:HALAMAN  TAHAN:BERANDA"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t page_ = 0;
};

class TestErrorScreen : public Screen {
public:
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext& ctx) const override;
    const char* hint(const UiContext&) const override { return "TEKAN:BERANDA"; }
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

// =====================================================================
//  PENGATURAN
// =====================================================================
class SettingsMenuScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    ListCursor list_;
};

// SET PROFIL: PROFIL 1-3 -> EditProfile
class ProfileListScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:UBAH TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

// ---------------------------------------------------------------------
//  Edit nilai, dipakai profil, UMUM, parameter metode, kontrol pemanas,
//  tampilan. Model kontrol (beda dari desain, KEPUTUSAN-UI 4.11):
//    PUTAR pilih -> TEKAN mulai edit -> PUTAR ubah (cepat = 5 langkah)
//    -> TEKAN simpan (ditolak: blok kuning + pesan, tetap edit)
//    -> TAHAN batal edit. TAHAN di luar edit = kembali.
//  <= 3 nilai: kotak bertumpuk (03-20.2); lebih: baris label|nilai
//  (03-40.6) dengan baris RESET opsional di bawah.
// ---------------------------------------------------------------------
struct FieldDef {
    const char* label;
    int32_t min;
    int32_t max;
    int32_t step;              // langkah per klik (cepat = 5 x)
    uint8_t decimals;          // nilai disimpan x 10^decimals
    const char* unit;          // setelah angka, mis. HB_DEG "C"
    const char* const* names;  // teks per nilai (min..max) atau nullptr
};
void formatField(const FieldDef& f, int32_t value, char (&out)[20]);

class EditValuesScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override;
    void draw(Painter& p, const UiContext& ctx) const override;

protected:
    virtual const char* title(const UiContext& ctx) const = 0;
    virtual uint8_t fieldCount() const = 0;
    virtual const FieldDef& field(uint8_t index) const = 0;
    virtual int32_t valueOf(const UiContext& ctx, uint8_t index) const = 0;
    // Tulis ke salinan, validasi, simpan. nullptr = tersimpan, selain itu
    // pesan penolakan (nilai TIDAK disimpan).
    virtual const char* commit(UiContext& ctx, uint8_t index, int32_t value) = 0;
    virtual uint8_t valueSize() const { return kT2; }  // tata letak kotak
    // Baris "RESET ..." di bawah daftar (nullptr = tidak ada)
    virtual const char* resetLabel() const { return nullptr; }
    virtual void resetValues(UiContext&) {}
    // Catatan T1 di bawah kotak (nullptr = tidak ada)
    virtual const char* note() const { return nullptr; }

private:
    uint8_t rowCount() const { return static_cast<uint8_t>(fieldCount() + (resetLabel() != nullptr ? 1 : 0)); }
    bool onResetRow() const { return resetLabel() != nullptr && cursor_ == fieldCount(); }
    void drawBoxes(Painter& p, const UiContext& ctx) const;
    void drawRows(Painter& p, const UiContext& ctx) const;

    uint8_t cursor_ = 0;
    bool editing_ = false;
    int32_t editValue_ = 0;
    const char* rejected_ = nullptr;
    char dialogTitle_[24] = "";
};

class EditProfileScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext& ctx) const override;
    uint8_t fieldCount() const override { return 2; }
    const FieldDef& field(uint8_t index) const override;
    int32_t valueOf(const UiContext& ctx, uint8_t index) const override;
    const char* commit(UiContext& ctx, uint8_t index, int32_t value) override;
    uint8_t valueSize() const override { return kT3; }
};

class EditGeneralScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext&) const override { return "UMUM"; }
    uint8_t fieldCount() const override { return 3; }
    const FieldDef& field(uint8_t index) const override;
    int32_t valueOf(const UiContext& ctx, uint8_t index) const override;
    const char* commit(UiContext& ctx, uint8_t index, int32_t value) override;
};

// OPSI PENGEMBANG > PARAMETER METODE (03-40.6b)
class EditMethodScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext&) const override { return "PARAMETER"; }
    uint8_t fieldCount() const override { return 6; }
    const FieldDef& field(uint8_t index) const override;
    int32_t valueOf(const UiContext& ctx, uint8_t index) const override;
    const char* commit(UiContext& ctx, uint8_t index, int32_t value) override;
    const char* resetLabel() const override { return "RESET PARAMETER"; }
    void resetValues(UiContext& ctx) override;
};

// OPSI PENGEMBANG > KONTROL PEMANAS (PI lampu, uji U3)
class EditHeaterScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext&) const override { return "PEMANAS"; }
    uint8_t fieldCount() const override { return 4; }
    const FieldDef& field(uint8_t index) const override;
    int32_t valueOf(const UiContext& ctx, uint8_t index) const override;
    const char* commit(UiContext& ctx, uint8_t index, int32_t value) override;
    const char* resetLabel() const override { return "RESET PEMANAS"; }
    void resetValues(UiContext& ctx) override;
};

// OPSI PENGEMBANG > TAMPILAN (03-40.7)
class EditDisplayScreen : public EditValuesScreen {
protected:
    const char* title(const UiContext&) const override { return "TAMPILAN"; }
    uint8_t fieldCount() const override { return 2; }
    const FieldDef& field(uint8_t index) const override;
    int32_t valueOf(const UiContext& ctx, uint8_t index) const override;
    const char* commit(UiContext& ctx, uint8_t index, int32_t value) override;
    const char* note() const override { return "HANYA ANGKA DI LAYAR, DATA ASLI TETAP"; }
};

// INFO PERANGKAT (03-40.10)
class DeviceInfoScreen : public Screen {
public:
    const char* hint(const UiContext&) const override { return "TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;
};

// =====================================================================
//  WIFI & JAM (ScreensNetwork.cpp)
// =====================================================================
class WifiScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
};

class WifiScanScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext& ctx) const override;
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    static constexpr uint8_t kMax = 12;
    static constexpr uint8_t kRows = 6;
    uint8_t rowCount() const { return static_cast<uint8_t>(count_ + 2); }  // + KETIK + PINDAI ULANG
    void startScan(UiContext& ctx);
    WifiNetwork nets_[kMax] = {};
    uint8_t count_ = 0;
    bool waiting_ = false;
    uint8_t seq_ = 0;
    uint32_t requestMs_ = 0;
    ListCursor list_;
};

// Ketik per karakter (keputusan 8.2, disesuaikan rotary):
//   PUTAR = ganti karakter, TEKAN = ganti kelompok (abc/ABC/123/simbol),
//   TAHAN 1 dtk = kunci karakter & lanjut kolom. Di ujung putaran ada
//   HAPUS (hapus karakter terakhir), SELESAI, BATAL (TAHAN untuk memilih).
class TextEntryScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext& ctx) const override;
    const char* hint(const UiContext& ctx) const override;
    void draw(Painter& p, const UiContext& ctx) const override;
    bool animating(const UiContext&) const override { return true; }  // kursor berkedip

private:
    enum Group : uint8_t { Lower, Upper, Digit, Symbol, GroupCount };
    static uint8_t groupSize(uint8_t group);
    char current() const;           // 0 = salah satu aksi
    uint8_t action() const;         // 0 karakter, 1 HAPUS, 2 SELESAI, 3 BATAL
    void finish(UiContext& ctx);

    char text_[65] = "";
    uint8_t length_ = 0;
    uint8_t maxLength_ = 64;
    uint8_t group_ = Lower;
    uint8_t index_ = 0;  // posisi di kelompok (karakter lalu 3 aksi)
    char rejected_[32] = "";
};

// =====================================================================
//  OPSI PENGEMBANG (ScreensDev.cpp)
// =====================================================================
class DevGateScreen : public Screen {
public:
    void onPress(UiContext& ctx) override { ctx.nav.pop(); }
    void onHold(UiContext& ctx) override { ctx.nav.replace(ScreenId::DevMenu); }
    HoldSpec holdSpec(const UiContext&) const override;
    const char* hint(const UiContext&) const override { return "TAHAN 5 DTK:MASUK  TEKAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
};

class DevMenuScreen : public Screen {
public:
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    ListCursor list_;
};

class SensorStatusScreen : public Screen {
public:
    const char* hint(const UiContext&) const override { return "DATA LANGSUNG  TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;
};

class ScaleActionsScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    const char* hint(const UiContext&) const override { return "PUTAR:PILIH TEKAN:JALANKAN TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    uint8_t cursor_ = 0;
    bool waiting_ = false;
    uint8_t seq_ = 0;
    char done_[32] = "";
    uint32_t doneMs_ = 0;
    hbcycle::QuietValue quiet_;
    float shown_ = 0.0f;
};

// KALIBRASI BERAT 1 titik (keputusan 8.3): KOSONGKAN -> ACUAN -> LETAKKAN
// -> HASIL (SIMPAN / BATAL = faktor lama dikembalikan)
class CalibrationScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    void onHold(UiContext& ctx) override;
    HoldSpec holdSpec(const UiContext& ctx) const override;
    const char* hint(const UiContext& ctx) const override;
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    enum Step : uint8_t { Empty, Reference, Load, Result, Failed };
    Step step_ = Empty;
    bool busy_ = false;
    uint8_t seq_ = 0;
    uint8_t refIndex_ = 3;  // 10 g
    float oldCpg_ = 0.0f;
    float newCpg_ = 0.0f;
    uint32_t busySinceMs_ = 0;
    uint8_t choice_ = 1;    // di HASIL: 0 BATAL, 1 SIMPAN
    bool failedOnZero_ = false;
};

// CEK TIMBANGAN (03-40.5): acuan vs terbaca, selisih, OK / PERIKSA
class ScaleCheckScreen : public Screen {
public:
    void onEnter(UiContext& ctx) override;
    void tick(UiContext& ctx) override;
    void onRotate(UiContext& ctx, int8_t steps, bool fast) override;
    void onPress(UiContext& ctx) override;
    const char* hint(const UiContext&) const override { return "PUTAR:ACUAN TEKAN:NOLKAN TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;
    uint32_t liveKey(const UiContext& ctx) const override;

private:
    uint8_t refIndex_ = 3;
    bool taring_ = false;
    uint8_t seq_ = 0;
};

// MEMORI LOG (03-40.8)
class LogMemoryScreen : public Screen {
public:
    void onPress(UiContext& ctx) override;
    void onDialogResult(UiContext& ctx, uint8_t id, bool accepted) override;
    const char* hint(const UiContext&) const override { return "TEKAN:HAPUS SEMUA  TAHAN:KEMBALI"; }
    void draw(Painter& p, const UiContext& ctx) const override;

private:
    bool failed_ = false;
};

// Berat acuan anak timbangan (KALIBRASI & CEK)
constexpr uint8_t kRefCount = 7;
extern const uint8_t kRefGrams[kRefCount];

}  // namespace hbui
