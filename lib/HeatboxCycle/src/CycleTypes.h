#pragma once

// =====================================================================
//  CycleTypes.h — tipe data siklus pengeringan yang dibagi ke UI, logger,
//  dan uji PC (RANCANGAN-SIKLUS-PENGERINGAN.md §3, §7, §11)
//
//  C++ murni (tanpa Arduino.h). CycleResult sengaja POD berukuran tetap:
//  disimpan apa adanya ke LittleFS (berkas .res) dan dibaca lagi oleh
//  layar Riwayat, jadi urutan/ukuran field = format berkas. Kalau diubah,
//  naikkan kResultVersion.
// =====================================================================

#include <cstddef>
#include <cstdint>

namespace hbcycle {

// Fase siklus (RANCANGAN §3). Done/Failed = siklus selesai, menunggu
// operator kembali ke Beranda.
enum class Phase : uint8_t {
    Idle,
    Preheat,   // ruang (cawan kosong) dipanaskan ke suhu pre-heat
    Tare,      // menunggu berat stabil 1 menit -> tare otomatis / TEKAN
    Load,      // operator membuka pintu, memasukkan sampel, menutup
    Recover,   // suhu pulih ke pre-heat + berat tenang -> W0
    Heat,      // t = 0: suhu heat, Fase A -> Fase B
    Stopping,  // Wf dikunci, lampu mati, layar STOP sebentar
    Done,      // hasil (OK / TIMEOUT / ERROR dengan kadar air)
    Failed,    // ERROR tanpa kadar air (tutup, sensor, pemanas, ...)
};

enum class Status : uint8_t { None, Running, Ok, Timeout, Error };

// Kode alasan (RANCANGAN §7). Ditampilkan di HASIL > WAKTU & STATUS dan
// di layar ERROR.
enum class Reason : uint8_t {
    None,
    RateReached,       // OK: laju model mencapai target, stop t* + 2
    MaxTime,           // TIMEOUT: Fase A tidak tercapai sampai waktu maksimum
    SmallShrink,       // TIMEOUT: penyusutan tidak pernah >= syarat
    PredictBeyondMax,  // TIMEOUT: t* + 2 melewati waktu maksimum
    Fit,               // ERROR (FIT): fit tidak layak, kadar air tidak tervalidasi
    Data,              // ERROR (DATA): menit sah tidak cukup
    Wf,                // ERROR (WF): Wf menyimpang dan data akhir terganggu
    Lid,               // ERROR (TUTUP): pintu dibuka saat HEAT
    Sensor,            // ERROR (SENSOR)
    Heater,            // ERROR (HEATER): suhu berlebih / tidak tercapai
    Preheat,           // ERROR (PREHEAT): tare tidak stabil sampai batas tunggu
    Sample,            // ERROR (SAMPEL): berat di luar 4-12 g / W0 tidak tercapai
    Cancel,            // ERROR (BATAL): dibatalkan operator
    Reset,             // ERROR (RESET): alat mati/restart di tengah tes
};

// Bendera peringatan (bitmask), hasil tetap berlaku
namespace flag {
constexpr uint16_t kModelDeviates = 1u << 0;   // Wf menyimpang dari model (data mulus)
constexpr uint16_t kWeightOutside = 1u << 1;   // W0 di luar anjuran 5-10 g
constexpr uint16_t kSmallShrink = 1u << 2;     // penyusutan sangat kecil
constexpr uint16_t kMethodsDisagree = 1u << 3; // V7: t* model vs A1 berbeda jauh
constexpr uint16_t kFitTried = 1u << 4;        // fit dicoba (info), mis. saat TIMEOUT
constexpr uint16_t kNoClock = 1u << 5;         // jam belum sinkron saat tes mulai
}  // namespace flag

// Satu titik per menit fase HEAT (grafik & riwayat)
struct MinutePoint {
    float massMg;  // nilai garis robust di tengah menit; NAN = menit tidak sah
    float tempC;   // rata-rata suhu menit itu; NAN kalau tidak ada
};

constexpr uint32_t kResultMagic = 0x48425231;  // "HBR1"
constexpr uint16_t kResultVersion = 1;
constexpr uint8_t kMaxMinutes = 64;  // waktu maksimum heat 60 mnt + cadangan

struct CycleResult {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;  // flag::*
    uint32_t id;
    uint32_t startEpoch;  // detik UNIX saat MULAI; 0 = jam belum sinkron
    char firmware[12];

    // Pengaturan yang dipakai
    uint8_t profileIndex;
    uint8_t preheatC;
    uint8_t heatC;
    uint8_t preheatMin;
    uint8_t minHeatMin;
    uint8_t maxHeatMin;
    Status status;
    Reason reason;

    // Berat & kadar air
    uint8_t hasMoisture;  // 1 = kadar air dihitung (OK, TIMEOUT, ERROR FIT/DATA/WF)
    uint8_t validated;    // 1 = OK atau TIMEOUT (bukan "tidak tervalidasi")
    uint8_t validMinutes;
    uint8_t minuteCount;  // isi minutes[]
    float w0Mg;
    float wfMg;
    float moisturePct;  // basis basah
    float dryBasisPct;

    // Waktu (detik sejak t = 0); < 0 = tidak ada
    float heatSec;     // durasi fase HEAT sampai stop
    float phaseASec;   // Fase A terpicu
    float tStarSec;    // prediksi laju = target
    float tStopSec;    // rencana stop (t* + 2); stop aktual = heatSec
    float heatReachedSec;

    // Model (Fase B)
    float tauMin;
    float mInfMg;
    float amplitudeMg;
    float fitT0Min;
    float rmsMg;
    float r2;
    float asymptotePct;  // (W0 - m_inf) / W0
    float wfModelDevMg;  // Wf - model di t_stop; NAN kalau tidak ada model
    float peakRateMgMin;

    // Suhu fase HEAT
    float tempAvgC;   // rata-rata setelah suhu heat tercapai
    float overshootC; // maks (suhu - target) setelah tercapai
    float dutyAvgPct; // rata-rata daya lampu fase HEAT

    // Konversi raw -> mg untuk log per detik: (raw - zeroRaw) / cpg x 1000
    double zeroRaw;  // nol siklus (setelah TARE)
    float countsPerGram;
    uint32_t secondsLogged;  // panjang log per detik (.sec)

    MinutePoint minutes[kMaxMinutes];
    uint32_t crc;  // crc32 semua bait sebelum field ini
};

// Ringkasan untuk daftar Riwayat & "hasil terakhir" Beranda
struct HistoryEntry {
    uint32_t id;
    uint32_t startEpoch;
    float moisturePct;
    Status status;
    Reason reason;
    uint8_t hasMoisture;
    uint8_t validated;
    uint16_t flags;
};

// Keadaan siklus yang ditampilkan LCD (salinan baca-saja, disusun task
// sensor). Semua waktu dalam detik waktu siklus.
struct CycleView {
    Phase phase;
    uint32_t testId;

    // Suhu & lampu
    float tempC;  // NAN kalau tidak sah
    float setpointC;
    float dutyPct;  // daya lampu jendela berjalan, 0..100
    bool lidClosed;
    bool heaterOn;  // relay K1 tertutup (lampu boleh menyala)

    // PRE-HEAT (02-04)
    uint32_t preheatElapsedSec;
    uint32_t preheatTotalSec;
    bool preheatWaiting;  // waktu habis, suhu belum dalam pita
    bool preheatPaused;   // pintu terbuka -> timer berhenti
    float preheatStartC;  // suhu saat MULAI (untuk persen progres)

    // TARE (02-05)
    float panMg;       // berat relatif nol timbangan saat ini
    float driftMgMin;  // NAN sampai data cukup
    bool stable;
    uint32_t stableSec;   // lama stabil berturut-turut
    uint32_t tareSec;     // lama di fase TARE
    uint32_t tareLimitSec;

    // MUAT SAMPEL (02-06)
    float sampleMg;  // NAN sebelum ada bacaan setelah tare
    bool lidOpenedAfterTare;

    // PULIH & W0 (02-07)
    uint32_t inBandSec;  // suhu dalam pita berturut-turut
    bool weightCalm;
    uint32_t recoverSec;

    // HEAT (02-08)
    uint32_t heatSec;
    float currentMg;  // berat sekarang relatif nol siklus
    float lossPct;    // (W0 - M) / W0
    float rateMgMin;  // laju Theil-Sen; NAN kalau belum ada
    bool phaseB;      // prediksi aktif
    bool fitFailed;   // fit tidak layak -> lanjut ke waktu maksimum
    bool predictBeyondMax;
    uint32_t stoppingSec;  // STOP: detik sejak stop

    // Data tes berjalan / hasil akhir (diisi bertahap)
    CycleResult result;
};

const char* phaseName(Phase phase);
const char* reasonCode(Reason reason);  // "E-TUTUP", "LAJU < 4", ...

}  // namespace hbcycle
