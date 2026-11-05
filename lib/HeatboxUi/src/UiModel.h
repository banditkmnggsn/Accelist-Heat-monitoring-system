#pragma once

// =====================================================================
//  UiModel.h — batas antara logika dan tampilan (RANCANGAN §9)
//
//  Layar HANYA membaca UiSnapshot (salinan baca-saja yang disusun src/
//  tiap ~50 ms) dan HANYA bertindak lewat UiCommands. Layar tidak tahu
//  driver sensor, WiFi, NVS, atau buzzer.
// =====================================================================

#include <CycleSettings.h>

#include <cstdint>

namespace hbui {

enum class CheckState : uint8_t { Pending, Ok, Fail };

enum class WifiState : uint8_t {
    Disabled,       // build ENABLE_WIFI=0
    NoCredentials,  // tidak ada SSID di NVS maupun secrets.h
    Connecting,
    Connected,
};

// Semua suara ada di satu tabel: src/sounds.h
enum class Sound : uint8_t {
    Welcome,   // boot: mesin menyala
    Click,     // TEKAN
    HoldDone,  // aksi TAHAN berhasil (dua bip)
    Confirm,   // nilai tersimpan / pilihan diterapkan
    Error,     // ditolak / gagal (nada turun)
    Warning,   // peringatan (nada panjang)
    Finish,    // tes selesai (dipakai Prompt 2; bisa diputar di UJI SUARA)
    Count,
};

// Diagnostik rotary untuk UJI INPUT
struct InputDiag {
    int32_t detents;          // posisi kumulatif (klik)
    int32_t raw;              // transisi quadrature sah, bertanda
    int32_t rawPerClick;      // raw di antara dua klik terakhir (harus = langkah/klik)
    uint32_t invalidJumps;    // dua bit berubah sekaligus
    uint32_t partialReturns;  // gerak tanpa jadi klik (pantulan / diam bergetar)
    uint32_t reverseSuspect;  // klik berlawanan arah sangat cepat setelah klik sebelumnya
    int8_t lastDir;           // +1 kanan, -1 kiri, 0 belum ada
    bool pressed;             // tombol turun (debounce)
    bool cancelled;           // tekanan ini dibatalkan putaran
    uint32_t pressedMs;       // lama tombol ditekan saat ini
    uint32_t lastHoldMs;      // lama tekanan terakhir saat dilepas
    bool doorPinHigh;         // level mentah GPIO pintu
    uint8_t stepsPerDetent;
};

struct UiSnapshot {
    uint32_t nowMs;

    // Cek boot sensor (Pending sampai driver selesai begin()).
    CheckState scaleCheck;
    CheckState tempCheck;
    // Keadaan sekarang
    bool scaleOk;   // ADS1232 mengirim data tanpa galat
    bool tempOk;    // MAX31865 sah dan wajar
    float tempC;    // NAN kalau tidak sah
    bool doorClosed;

    WifiState wifi;
    int8_t rssi;
    bool clockSynced;  // NTP pernah berhasil sejak boot
    uint8_t hour;
    uint8_t minute;

    bool testRunning;  // selalu false sampai siklus ada (Prompt 2)

    uint32_t holdProgressMs;  // 0 = bar tahan tidak tampil
    InputDiag input;
};

class UiCommands {
public:
    // force: tetap bunyi walaupun suara dimatikan di Pengaturan (UJI SUARA)
    virtual void playSound(Sound sound, bool force) = 0;
    // Simpan ke NVS; false kalau gagal menulis
    virtual bool saveSettings(const hbcycle::Settings& settings) = 0;
    // Ulangi cek sensor boot (layar error SENSOR)
    virtual void recheckSensors() = 0;

protected:
    ~UiCommands() = default;
};

}  // namespace hbui
