#pragma once

// =====================================================================
//  UiModel.h — batas antara logika dan tampilan (RANCANGAN §9)
//
//  Layar HANYA membaca UiSnapshot (salinan baca-saja yang disusun src/
//  tiap ~50 ms) dan HANYA bertindak lewat UiCommands. Layar tidak tahu
//  driver sensor, WiFi, NVS, atau buzzer.
// =====================================================================

#include <AdvancedSettings.h>
#include <CycleSettings.h>
#include <CycleTypes.h>

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
    Error,     // ditolak / gagal (bip putus-putus)
    Warning,   // peringatan (nada panjang)
    Finish,    // tes selesai
    Count,
};

// ---- WiFi & jam (PENGATURAN > WIFI & JAM) ----
enum class NetFail : uint8_t {
    None,
    WrongPassword,  // ditolak access point (sandi salah)
    NotFound,       // SSID tidak terlihat
    Timeout,        // tidak tersambung dalam batas waktu
};

enum class OtaState : uint8_t {
    Off,       // tanpa OTA_PASS / build tanpa WiFi
    Waiting,   // OTA_PASS ada, menunggu WiFi
    Ready,     // menerima upload dari PlatformIO (env "ota")
    Updating,  // sedang menulis firmware baru
    Failed,    // upload terakhir gagal
};

struct NetInfo {
    char ssid[33];          // SSID yang dipakai ("" = belum ada)
    char ip[16];            // "" kalau belum tersambung
    char host[24];          // nama OTA/mDNS, mis. "heatbox-1a2b"
    uint32_t nowEpoch;      // jam sekarang (UNIX), 0 sebelum sinkron
    uint32_t lastSyncEpoch; // sinkron NTP terakhir (0 = belum pernah)
    bool trying;            // sedang mencoba SSID/sandi baru
    NetFail lastFail;       // hasil percobaan terakhir
    bool scanning;
    uint8_t scanSeq;        // naik setiap pemindaian selesai
    OtaState ota;
    uint8_t otaPct;
};

struct WifiNetwork {
    char ssid[33];
    int8_t rssi;
    bool secure;
};

// ---- timbangan & sensor suhu (OPSI PENGEMBANG) ----
enum class ScaleStatus : uint8_t { Ok, NoData, DoutError, Settling, Clipped, Busy };
enum class ScaleJob : uint8_t { None, Tare, SelfCal, Reset, Calibrate };
enum class JobResult : uint8_t { None, Done, Failed, Rejected };

struct ScaleInfo {
    float grams;          // berat asli (filter driver), NAN kalau belum ada
    int32_t raw;          // kode ADC terakhir
    ScaleStatus status;
    bool stable;
    bool calibrated;      // faktor dari kalibrasi, bukan perkiraan
    float countsPerGram;
    // Aksi terakhir yang diminta UI: jobSeq naik setiap aksi selesai
    ScaleJob job;
    JobResult jobResult;
    uint8_t jobSeq;
};

struct RtdInfo {
    bool spiOk;
    float ohm;
    uint8_t fault;
};

struct DeviceInfo {
    char mac[18];
    char id[12];          // "HBX-1A2B3C" dari MAC
    uint32_t totalTests;  // jumlah tes yang pernah dimulai
    uint32_t logFreeKb;
    uint32_t logTotalKb;
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

    bool testRunning;  // siklus memanaskan/menimbang (PRE-HEAT..STOP)
    hbcycle::CycleView cycle;  // keadaan siklus (fase Idle = tidak ada tes)
    bool startPending;         // MULAI sudah diminta, belum diproses task sensor

    // Log tes (LittleFS)
    bool hasLast;                  // ada hasil tersimpan
    hbcycle::HistoryEntry last;    // hasil terakhir (Beranda)
    uint8_t historyCount;
    uint8_t historyCapacity;
    uint32_t interruptedId;        // tes terputus saat boot (0 = tidak ada)
    bool simulation;               // build simulasi: sensor & lampu tiruan

    NetInfo net;
    ScaleInfo scale;
    RtdInfo rtd;
    DeviceInfo device;

    uint32_t holdProgressMs;  // 0 = bar tahan tidak tampil
};

class UiCommands {
public:
    // force: tetap bunyi walaupun suara dimatikan di Pengaturan
    virtual void playSound(Sound sound, bool force) = 0;
    // Simpan ke NVS; false kalau gagal menulis
    virtual bool saveSettings(const hbcycle::Settings& settings) = 0;
    // Pengaturan tim IT; berlaku untuk tes BERIKUTNYA
    virtual bool saveAdvanced(const hbcycle::AdvancedSettings& advanced) = 0;
    // Ulangi cek sensor boot (layar error SENSOR)
    virtual void recheckSensors() = 0;

    // ---- siklus (diproses task sensor; hasil terlihat di snap.cycle) ----
    virtual void startTest(const hbcycle::Settings& settings) = 0;
    virtual void tareNow() = 0;
    virtual void cancelTest() = 0;
    virtual void acknowledgeResult() = 0;  // HASIL/ERROR -> Beranda
    virtual void acknowledgeInterrupted() = 0;

    // ---- riwayat ----
    // Terbaru lebih dulu; return jumlah yang diisi
    virtual uint8_t historyList(hbcycle::HistoryEntry* out, uint8_t max) = 0;
    virtual bool loadResult(uint32_t id, hbcycle::CycleResult& out) = 0;
    virtual bool clearLogs() = 0;  // hapus semua log tes (MEMORI LOG)

    // ---- WiFi & jam (diproses task sensor; hasil di snap.net) ----
    virtual void wifiScan() = 0;
    virtual uint8_t wifiResults(WifiNetwork* out, uint8_t max) = 0;  // RSSI terkuat dulu
    // Coba SSID/sandi baru. Disimpan ke NVS hanya kalau tersambung; kalau
    // gagal, WiFi lama dipakai lagi dan snap.net.lastFail diisi.
    virtual void wifiConnect(const char* ssid, const char* pass) = 0;
    virtual void wifiReconnect() = 0;
    virtual void clockSync() = 0;

    // ---- timbangan (diproses task sensor; hasil di snap.scale) ----
    virtual void scaleJob(ScaleJob job, float grams) = 0;  // grams hanya untuk Calibrate
    virtual void scaleRestore(float countsPerGram) = 0;    // batal kalibrasi: faktor lama

protected:
    ~UiCommands() = default;
};

}  // namespace hbui
