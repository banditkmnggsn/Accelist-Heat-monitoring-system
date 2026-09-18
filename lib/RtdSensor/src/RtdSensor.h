#pragma once

// =====================================================================
//  RtdSensor.h — wrapper MAX31865 (PT100) non-blocking
//
//  Konfigurasi & fault memakai Adafruit_MAX31865 (lib_deps). Register RTD
//  dibaca langsung lewat Adafruit_SPIDevice karena di library v1.6.2
//  satu-satunya pembaca RTD adalah readRTD(), yang berisi delay(10) +
//  delay(65) dan register reader-nya private.
//
//  Mode: continuous (auto-conversion), filter 50 Hz, sampel tiap
//  kSamplePeriodMs (10 Hz) berbasis millis().
// =====================================================================

#include <Adafruit_MAX31865.h>
#include <Adafruit_SPIDevice.h>
#include <Arduino.h>

class RtdSensor {
public:
    struct Config {
        uint8_t csPin;
        uint8_t sckPin;
        uint8_t misoPin;
        uint8_t mosiPin;
        max31865_numwires_t wires;
        float rrefOhm;
        float rnominalOhm;
    };

    // -----------------------------------------------------------------
    //  Timing
    // -----------------------------------------------------------------
    static constexpr uint32_t kSamplePeriodMs = 100;  // target refresh 10 Hz
    // Jeda sebelum sampel pertama setelah bias + auto-conversion aktif:
    // > bias settling 10 ms (nilai yang dipakai readRTD() Adafruit)
    // + > 1 konversi (1-shot 50 Hz = 62.5 ms, continuous lebih cepat).
    // TODO VERIFY: waktu start-up VBIAS dan waktu konversi continuous
    //   mode 50 Hz di datasheet MAX31865 (Electrical Characteristics).
    static constexpr uint32_t kStartupSettleMs = 100;
    static constexpr uint32_t kFirstSampleTimeoutMs = 500;
    // Fault-detection cycle otomatis (hanya saat begin): tunggu bit
    // D3:D2 register config kembali 00.
    // TODO VERIFY: durasi automatic fault detection di datasheet MAX31865
    //   (Adafruit hanya memberi delay(1) sebelum membaca status).
    static constexpr uint32_t kFaultCycleTimeoutMs = 100;
    // Fault yang sama dilaporkan ulang paling cepat tiap interval ini
    static constexpr uint32_t kFaultRepeatMs = 5000;

    // -----------------------------------------------------------------
    //  SPI & register (setting sama dengan Adafruit_MAX31865)
    // -----------------------------------------------------------------
    static constexpr uint32_t kSpiClockHz = 1000000;
    static constexpr uint8_t kReadAddressMask = 0x7F;
    static constexpr uint8_t kFaultCycleMask = 0x0C;   // config D3:D2
    static constexpr uint8_t kConfigCheckMask = 0xD1;  // D7 bias, D6 auto, D4 3-wire, D0 50 Hz
    static constexpr float kAdcFullScale = 32768.0f;   // 2^15, RTD code 15 bit
    static constexpr uint16_t kAdcMaxCode = 0x7FFF;

    // -----------------------------------------------------------------
    //  Ambang fault hardware (register High/Low Fault Threshold).
    //  Dipasang di batas rentang PT100 IEC 60751 (-200..850 C) supaya
    //  hanya RTD open/short yang memicu fault. Nilai yang sekadar tidak
    //  wajar (RREF / jumper salah) tetap terbaca dan ditangkap oleh
    //  plausibility check di main.cpp.
    //    PT100 @ -200 C = 18.52 ohm  -> 0.1852 x RNOMINAL
    //    PT100 @  850 C = 390.48 ohm -> 3.9048 x RNOMINAL
    // -----------------------------------------------------------------
    static constexpr float kFaultLowRatio = 0.1852f;
    static constexpr float kFaultHighRatio = 3.9048f;

    explicit RtdSensor(const Config& cfg);

    // Inisialisasi SPI & chip, cek register (loopback threshold + config),
    // jalankan automatic fault-detection cycle, aktifkan continuous mode,
    // lalu tunggu sampel pertama. Hanya dipanggil dari setup().
    bool begin();

    void update();

    float tempC() const;
    float rtdResistance() const;
    uint8_t fault() const;  // isi Fault Status Register sampel terakhir
    bool valid() const;     // SPI OK, sudah ada sampel, dan tidak ada fault

    // Laporan fault untuk dicetak aplikasi. Return true sekali per
    // perubahan kode fault (termasuk kembali ke 0) atau tiap
    // kFaultRepeatMs selama fault bertahan. Fault di chip di-clear
    // otomatis setelah laporan diambil.
    bool takeFaultReport(uint8_t& faultCode);

    // ---- diagnostik ----
    bool spiOk() const;
    uint8_t initFault() const;       // hasil fault-detection cycle saat begin()
    uint8_t configRegister() const;  // register config setelah begin()
    uint16_t rawCode() const;        // kode RTD 15 bit terakhir

    // Decode bit Fault Status Register ke teks, dipisah koma
    static void printFault(Print& out, uint8_t faultCode);

private:
    bool readRegisters(uint8_t address, uint8_t* buffer, size_t length);
    uint8_t readConfig();
    uint16_t ohmToThresholdRegister(float ohm) const;
    void readSample();

    Config cfg_;
    Adafruit_MAX31865 max_;
    Adafruit_SPIDevice spi_;

    bool spiOk_ = false;
    uint8_t initFault_ = 0;
    uint8_t config_ = 0;
    uint32_t lastSampleMs_ = 0;
    uint32_t waitMs_ = kStartupSettleMs;  // jeda ke sampel berikutnya
    uint32_t sampleCount_ = 0;

    uint16_t rawCode_ = 0;
    float resistance_ = NAN;
    float tempC_ = NAN;
    uint8_t fault_ = 0;

    bool reportPending_ = false;
    uint8_t reportedFault_ = 0;
    uint32_t lastReportMs_ = 0;
};
