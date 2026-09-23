#pragma once

// =====================================================================
//  Ads1232.h — driver bit-bang ADS1232 untuk load cell
//
//  Referensi: TI SBAS350H "ADS1232, ADS1234" (rev. Juni 2025).
//  Semua nomor section/tabel di file ini mengacu ke revisi tersebut.
//
//  Asumsi modul (di-jumper, tidak dikendalikan firmware):
//    GAIN1=GAIN0=1 -> gain 128, SPEED=0 -> 10 SPS, A0=0 -> AIN1, TEMP=0.
//
//  Pola pemakaian:
//    begin() sekali di setup(), update() di setiap loop() (non-blocking),
//    lalu baca lewat getter. Tare/kalibrasi berjalan di background dan
//    hasilnya dilaporkan lewat takeEvent().
// =====================================================================

#include <Arduino.h>

class Ads1232 {
public:
    struct Pins {
        uint8_t sclk;
        uint8_t dout;  // DRDY/DOUT
        uint8_t pdwn;
    };

    enum class Health : uint8_t {
        NotStarted,
        Ok,
        NoDrdy,        // DRDY/DOUT tidak pernah LOW dalam kDrdyTimeoutMs
        DoutStuckLow,  // DOUT tidak naik setelah pulsa ke-25 -> SCLK tidak diterima chip
    };

    enum class Event : uint8_t {
        None,
        ResetDone,          // settling setelah PDWN selesai, data valid mengalir
        TareDone,
        CalibrationDone,
        CalibrationFailed,  // selisih count terlalu kecil (beban tidak terdeteksi)
        SelfCalDone,
        SelfCalTimeout,
        MeasureDone,        // pengukuran raw selesai, hasil di measureMean()/measureSd()
        JobRejected,        // perintah ditolak (sedang sibuk / argumen tidak valid)
        JobCanceled,        // tare/kalibrasi dibatalkan oleh reset()
        ProtocolError,      // DOUT tidak dilepas HIGH setelah pulsa terakhir
        NvsWriteFailed,
    };

    // -----------------------------------------------------------------
    //  Timing dari datasheet (nilai untuk fCLK 4.9152 MHz / osc internal,
    //  toleransi osc internal +-3%)
    // -----------------------------------------------------------------
    // t7, Tabel 7-8: conversion time, SPEED=0 (10 SPS)
    static constexpr uint32_t kConversionPeriodMs = 100;
    // t14 (Tabel 7-12), t16/t17 (Tabel 7-13): pulsa PDWN min 26 us. Pakai margin ~4x.
    static constexpr uint32_t kPdwnPulseUs = 100;
    // t15 (Tabel 7-13): PDWN LOW setelah supply stabil min 10 us. Pakai margin 10x.
    static constexpr uint32_t kPowerUpLowUs = 100;
    // t3 (Tabel 7-8): lebar pulsa SCLK HIGH/LOW min 100 ns -> 1 us sudah 10x lipat.
    static constexpr uint32_t kSclkHalfPeriodUs = 1;
    // t8 (Tabel 7-9): data pertama setelah offset calibration, SPEED=0 = 801.03 ms max
    static constexpr uint32_t kSelfCalNominalMs = 801;
    static constexpr uint32_t kSelfCalTimeoutMs = 2000;
    // Batas diagnostik: DRDY tidak turun selama ini -> dianggap tidak ada data
    static constexpr uint32_t kDrdyTimeoutMs = 2000;
    // t11 (Tabel 7-10/7-12): data ready setelah wake-up, SPEED=0 = 401.8 ms,
    // SPEED=1 = 52.51 ms. Dipakai untuk mengecek jumper SPEED saat boot.
    static constexpr uint32_t kWakeupDataReadyMs = 402;
    // DOUT diabaikan sebentar setelah PDWN HIGH: lebih lama dari wake-up
    // clock t13 (maks 5.6 ms, crystal, Tabel 7-12), jauh lebih cepat dari
    // DRDY pertama (t11 >= 52.51 ms).
    static constexpr uint32_t kWakeupGuardMs = 10;
    // Batas desain durasi critical section shift-out (lihat Ads1232.cpp)
    static constexpr uint32_t kMaxShiftDurationUs = 1000;

    // -----------------------------------------------------------------
    //  Protokol serial (lihat komentar lengkap di Ads1232.cpp)
    // -----------------------------------------------------------------
    static constexpr uint8_t kDataBits = 24;
    static constexpr uint8_t kPulsesRead = 25;     // 7.3.10 / Gambar 7-10
    static constexpr uint8_t kPulsesSelfCal = 26;  // 7.4.1 / Gambar 7-11

    // Konversi yang dibuang setelah reset PDWN. Lihat justifikasi +
    // TODO VERIFY di Ads1232.cpp (7.3.7 vs 7.4.4).
    static constexpr uint8_t kDiscardAfterReset = 4;

    // Kode output 24-bit two's complement (7.3.9, Tabel 7-7): clip di sini
    static constexpr int32_t kCodeMax = 8388607;   // 7FFFFFh
    static constexpr int32_t kCodeMin = -8388608;  // 800000h

    // -----------------------------------------------------------------
    //  Filter & deteksi stabil
    //
    //  Median-of-5 TIDAK dipakai sebagai filter permanen: median sliding
    //  membuat output berurutan berkorelasi, sehingga rata-rata 16 sampel
    //  tidak memberi pengurangan noise sqrt(16). Median hanya dipakai
    //  sebagai pengganti saat sampel dinilai spike (lihat processSample).
    // -----------------------------------------------------------------
    static constexpr uint8_t kMedianSize = 5;
    static constexpr uint8_t kAverageSize = 16;
    static constexpr uint8_t kRawRingSize = 64;  // dasar noise raw & gerbang spike
    static constexpr float kSpikeGateSigma = 6.0f;

    // Ambang isStable(). Ini sebaran SATU sampel di dalam window, bukan
    // ketidakpastian rata-ratanya: rata-rata 16 sampel independen ~4x lebih
    // baik dari angka ini. Diukur di hardware 2025-09: SD raw 20.9 count
    // (3.8 mg pada 5499 count/g), jadi ambang lama 3.0 count mustahil
    // tercapai dan status STABLE tidak pernah muncul.
    static constexpr float kStableStdDevMg = 6.0f;
    static constexpr float kStableStdDevCounts = 35.0f;  // dipakai saat belum terkalibrasi

    // -----------------------------------------------------------------
    //  Tare & kalibrasi
    // -----------------------------------------------------------------
    static constexpr uint8_t kTareSamples = 64;               // tare & calibrateWith()
    static constexpr int32_t kMinCalibrationSpanCounts = 1000; // beban kalibrasi minimal (count)
    static constexpr float kMinAbsCountsPerGram = 0.001f;     // guard scale nol / rusak
    static constexpr int32_t kDefaultOffset = 0;

    // Scale default kalau NVS kosong (status UNCALIBRATED). Hanya perkiraan:
    //   LSB = 0.5*VREF/Gain / (2^23-1)          (7.3.9)
    //   Vin = S[mV/V] * Vexc * W/Wmax, VREF = Vexc (rasiometrik, Gambar 8-1)
    //   counts/g = S/1000 * Gain * (2^23-1) / (0.5 * Wmax)
    // S = 1.0 mV/V adalah ASUMSI; cek datasheet load cell 300 g.
    static constexpr float kAssumedSensitivityMvPerV = 1.0f;
    static constexpr float kAssumedCapacityGrams = 300.0f;
    static constexpr float kAssumedGain = 128.0f;
    static constexpr float kDefaultCountsPerGram =
        (kAssumedSensitivityMvPerV / 1000.0f) * kAssumedGain * static_cast<float>(kCodeMax) /
        (0.5f * kAssumedCapacityGrams);

    static constexpr uint8_t kEventQueueSize = 4;

    explicit Ads1232(const Pins& pins);

    // Konfigurasi pin, load kalibrasi dari NVS, power-up sequence (7.4.5),
    // lalu tunggu DRDY pertama maks kDrdyTimeoutMs. Hanya dipanggil dari setup().
    // true = chip merespons dan protokol baca benar.
    bool begin();

    void update();

    // true sekali per sampel baru (flag dikosongkan saat dibaca)
    bool hasNewSample();

    int32_t raw() const;    // raw count terakhir, belum difilter
    float grams() const;    // (moving average - offset) / countsPerGram, NAN kalau belum ada data

    void tare();                           // rata-rata kTareSamples sampel -> offset (background)
    void setScale(float countsPerGram);    // langsung disimpan ke NVS
    void calibrateWith(float knownGrams);  // pakai anak timbangan (background)
    // Kumpulkan kTareSamples raw TANPA mengubah kalibrasi. Hasil lewat
    // Event::MeasureDone; dipakai kalibrasi multi-titik yang fit-nya di PC.
    void measure();
    bool selfCalibrate();                  // offset self-calibration ADS1232, false kalau ditolak
    bool isStable() const;

    // PDWN LOW >= kPdwnPulseUs lalu HIGH (non-blocking), lalu buang
    // kDiscardAfterReset konversi.
    void reset();

    Event takeEvent();

    // ---- status & diagnostik ----
    Health health() const;
    bool isCalibrated() const;     // scale berasal dari NVS / kalibrasi, bukan default
    bool hasStoredOffset() const;
    bool isBusy() const;           // tare/kalibrasi/self-cal sedang berjalan
    bool isSettling() const;       // power-down, settling, atau self-cal
    bool isClipped() const;        // raw terakhir di kode full-scale
    bool drdyTimedOut() const;
    bool doutError() const;        // protocol error terakhir belum pulih
    float countsPerGram() const;
    int32_t offset() const;          // pembulatan untuk tampilan; grams() pakai presisi penuh
    double offsetCounts() const;
    float filteredCounts() const;
    // Noise SETELAH filter. Dipakai isStable(); JANGAN dipakai untuk laporan
    // spesifikasi — untuk itu pakai rawStdDevCounts().
    float stdDevCounts() const;    // NAN sampai window moving average penuh
    float rawStdDevCounts() const;      // noise sebenarnya, sebelum filter
    int32_t rawPeakToPeakCounts() const;
    uint32_t spikesRejected() const;
    float stableThresholdCounts() const;
    double measureMean() const;      // valid setelah Event::MeasureDone
    float measureSd() const;
    uint8_t measureSamples() const;
    uint32_t firstDrdyMs() const;  // PDWN HIGH -> DRDY pertama
    uint32_t lastDrdyIntervalMs() const;
    uint32_t lastSelfCalMs() const;
    uint32_t lastShiftDurationUs() const;
    uint32_t protocolErrorCount() const;

private:
    enum class State : uint8_t { Off, PowerDown, Settling, Running, Calibrating };
    enum class Job : uint8_t { None, Tare, Calibrate, Measure };

    void enterState(State next);
    void handleDataReady();
    bool shiftOut(uint8_t pulses, uint32_t& code);
    void processSample(int32_t value);
    void updateRawStats(int32_t value);
    void resetFilters();
    int32_t medianOfWindow() const;
    bool canStartJob() const;
    void startJob(Job job);
    void finishJob();
    void loadCalibration();
    bool saveCalibration();
    void pushEvent(Event event);

    static int32_t toSigned24(uint32_t code);

    Pins pins_;
    portMUX_TYPE mux_;

    State state_ = State::Off;
    Health health_ = Health::NotStarted;
    uint32_t stateStartMs_ = 0;
    uint32_t stateStartUs_ = 0;
    uint8_t discardLeft_ = 0;
    bool selfCalRequested_ = false;

    // diagnostik timing
    bool firstDrdySeen_ = false;
    uint32_t firstDrdyMs_ = 0;
    uint32_t lastDrdyMs_ = 0;
    bool intervalValid_ = false;
    uint32_t lastDrdyIntervalMs_ = 0;
    uint32_t lastSelfCalMs_ = 0;
    uint32_t lastShiftUs_ = 0;
    uint32_t protocolErrors_ = 0;
    uint32_t errorStreak_ = 0;
    uint32_t lastErrorMs_ = 0;

    // data & filter
    int32_t raw_ = 0;
    bool clipped_ = false;
    bool newSample_ = false;
    int32_t medianBuf_[kMedianSize] = {};
    uint8_t medianIdx_ = 0;
    uint8_t medianCount_ = 0;
    int32_t avgBuf_[kAverageSize] = {};
    uint8_t avgIdx_ = 0;
    uint8_t avgCount_ = 0;
    int64_t avgSum_ = 0;
    double filtered_ = NAN;
    float stdDev_ = NAN;
    int32_t rawRing_[kRawRingSize] = {};
    uint8_t rawIdx_ = 0;
    uint8_t rawCount_ = 0;
    float rawStdDev_ = NAN;
    int32_t rawPeakToPeak_ = 0;
    uint32_t spikesRejected_ = 0;

    // kalibrasi
    double offsetCounts_ = static_cast<double>(kDefaultOffset);
    float countsPerGram_ = kDefaultCountsPerGram;
    bool hasScale_ = false;
    bool hasOffset_ = false;

    // job background. Penjumlahan relatif terhadap jobBase_ (sampel pertama)
    // supaya varians tidak hilang presisi terhadap mean yang besar.
    Job job_ = Job::None;
    double jobBase_ = 0.0;
    double jobSum_ = 0.0;
    double jobSumSq_ = 0.0;
    uint8_t jobCount_ = 0;
    float jobKnownGrams_ = 0.0f;
    double measureMean_ = NAN;
    float measureSd_ = NAN;
    uint8_t measureSamples_ = 0;

    // antrian event
    Event events_[kEventQueueSize] = {};
    uint8_t eventHead_ = 0;
    uint8_t eventCount_ = 0;
};
