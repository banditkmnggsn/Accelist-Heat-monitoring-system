#include "RtdSensor.h"

#include <SPI.h>

#include <cmath>

namespace {

struct FaultText {
    uint8_t mask;
    const char* text;
};

// Arti bit Fault Status Register MAX31865 (teks sama dengan contoh resmi
// library Adafruit). Tidak ada bit "REFIN+": D5 dan D4 sama-sama tentang
// REFIN-. Nama makro Adafruit REFINLOW (D5) / REFINHIGH (D4) tidak
// mencerminkan arti bitnya, jadi pegang teks di bawah.
// TODO VERIFY: cocokkan teks D7..D2 dengan tabel Fault Status Register di
//   datasheet MAX31865 (datasheet belum bisa diunduh saat kode ditulis).
constexpr FaultText kFaultTexts[] = {
    {MAX31865_FAULT_HIGHTHRESH, "RTD HIGH threshold (kemungkinan RTD open)"},
    {MAX31865_FAULT_LOWTHRESH, "RTD LOW threshold (kemungkinan RTD short)"},
    {MAX31865_FAULT_REFINLOW, "REFIN- > 0.85 x VBIAS"},
    {MAX31865_FAULT_REFINHIGH, "REFIN- < 0.85 x VBIAS (FORCE- open)"},
    {MAX31865_FAULT_RTDINLOW, "RTDIN- < 0.85 x VBIAS (FORCE- open)"},
    {MAX31865_FAULT_OVUV, "Under/Over voltage"},
};

constexpr uint8_t kUndefinedFaultBits = 0x03;  // D1, D0

}  // namespace

RtdSensor::RtdSensor(const Config& cfg)
    : cfg_(cfg),
      max_(static_cast<int8_t>(cfg.csPin), &SPI),
      spi_(static_cast<int8_t>(cfg.csPin), kSpiClockHz, SPI_BITORDER_MSBFIRST, SPI_MODE1, &SPI) {}

bool RtdSensor::begin() {
    // Pin VSPI eksplisit. ss = -1: CS dikendalikan Adafruit_SPIDevice di
    // cfg_.csPin, GPIO5 (SS default VSPI, pin strapping) tidak disentuh.
    // SPI.begin() tanpa argumen di dalam library tidak mengubah pin lagi
    // karena bus sudah aktif.
    SPI.begin(static_cast<int8_t>(cfg_.sckPin), static_cast<int8_t>(cfg_.misoPin),
              static_cast<int8_t>(cfg_.mosiPin), -1);

    max_.begin(cfg_.wires);  // wire mode, bias OFF, auto OFF, clear fault
    spi_.begin();
    max_.enable50Hz(true);   // notch hanya diubah saat auto-conversion OFF

    // Loopback SPI: tulis ambang fault lalu baca balik. MISO putus/short
    // terbaca 0x0000 atau 0xFFFF, keduanya bukan nilai yang ditulis.
    const uint16_t lower = ohmToThresholdRegister(kFaultLowRatio * cfg_.rnominalOhm);
    const uint16_t upper = ohmToThresholdRegister(kFaultHighRatio * cfg_.rnominalOhm);
    max_.setThresholds(lower, upper);
    spiOk_ = (max_.getLowerThreshold() == lower) && (max_.getUpperThreshold() == upper);
    if (!spiOk_) {
        return false;
    }

    // Automatic fault-detection cycle, hanya saat boot (bias ON, auto OFF).
    // Saat berjalan readFault() WAJIB dipanggil dengan MAX31865_FAULT_NONE:
    // argumen default (AUTO) menulis ulang config dan mematikan
    // auto-conversion.
    max_.readFault(MAX31865_FAULT_AUTO);
    const uint32_t cycleStartMs = millis();
    while ((readConfig() & kFaultCycleMask) != 0 &&
           (millis() - cycleStartMs) < kFaultCycleTimeoutMs) {
        // bit D3:D2 kembali 00 saat cycle selesai
    }
    initFault_ = max_.readFault(MAX31865_FAULT_NONE);
    max_.clearFault();

    max_.enableBias(true);   // continuous mode butuh VBIAS menyala terus
    max_.autoConvert(true);

    config_ = readConfig();
    uint8_t expected = MAX31865_CONFIG_BIAS | MAX31865_CONFIG_MODEAUTO | MAX31865_CONFIG_FILT50HZ;
    if (cfg_.wires == MAX31865_3WIRE) {
        expected |= MAX31865_CONFIG_3WIRE;
    }
    spiOk_ = (config_ & kConfigCheckMask) == expected;
    if (!spiOk_) {
        return false;
    }

    // Tunggu sampel pertama (setup saja). Pembacaan tetap lewat update().
    lastSampleMs_ = millis();
    waitMs_ = kStartupSettleMs;
    const uint32_t waitStartMs = millis();
    while (sampleCount_ == 0 && (millis() - waitStartMs) < kFirstSampleTimeoutMs) {
        update();
    }
    return valid() && initFault_ == 0;
}

void RtdSensor::update() {
    if (!spiOk_) {
        return;
    }
    const uint32_t nowMs = millis();
    if ((nowMs - lastSampleMs_) < waitMs_) {
        return;
    }
    lastSampleMs_ = nowMs;
    waitMs_ = kSamplePeriodMs;
    readSample();
}

void RtdSensor::readSample() {
    // Continuous mode: register RTD berisi hasil konversi terakhir,
    // cukup dibaca tanpa trigger 1-shot.
    uint8_t buffer[2] = {0, 0};
    readRegisters(MAX31865_RTDMSB_REG, buffer, sizeof(buffer));
    const uint16_t reg = static_cast<uint16_t>((buffer[0] << 8) | buffer[1]);
    rawCode_ = static_cast<uint16_t>(reg >> 1);  // D0 = flag fault

    resistance_ = (static_cast<float>(rawCode_) / kAdcFullScale) * cfg_.rrefOhm;
    tempC_ = max_.calculateTemperature(rawCode_, cfg_.rnominalOhm, cfg_.rrefOhm);

    // FAULT_NONE: hanya baca register status, tanpa fault-detection cycle
    fault_ = max_.readFault(MAX31865_FAULT_NONE);
    ++sampleCount_;

    if (reportPending_) {
        return;  // laporan sebelumnya belum diambil, fault chip belum di-clear
    }
    const bool changed = (fault_ != reportedFault_);
    const bool repeatDue = (fault_ != 0) && ((millis() - lastReportMs_) >= kFaultRepeatMs);
    if (changed || repeatDue) {
        reportPending_ = true;
    } else if (fault_ != 0) {
        // Fault yang sama sudah dilaporkan: clear agar hilangnya fault
        // terdeteksi di sampel berikutnya (status bit di chip latched).
        max_.clearFault();
    }
}

bool RtdSensor::takeFaultReport(uint8_t& faultCode) {
    if (!reportPending_) {
        return false;
    }
    reportPending_ = false;
    faultCode = fault_;
    reportedFault_ = fault_;
    lastReportMs_ = millis();
    if (faultCode != 0) {
        max_.clearFault();  // auto clear setelah dilaporkan
    }
    return true;
}

bool RtdSensor::readRegisters(uint8_t address, uint8_t* buffer, size_t length) {
    const uint8_t readAddress = address & kReadAddressMask;
    return spi_.write_then_read(&readAddress, 1, buffer, length);
}

uint8_t RtdSensor::readConfig() {
    uint8_t value = 0;
    readRegisters(MAX31865_CONFIG_REG, &value, 1);
    return value;
}

// Register threshold 16 bit dengan format sama seperti register RTD
// (kode 15 bit rata kiri, D0 tidak dipakai).
// TODO VERIFY: format dan arah pembandingan register High/Low Fault
//   Threshold di datasheet MAX31865.
uint16_t RtdSensor::ohmToThresholdRegister(float ohm) const {
    float code = (ohm / cfg_.rrefOhm) * kAdcFullScale;
    if (code < 0.0f) {
        code = 0.0f;
    }
    if (code > static_cast<float>(kAdcMaxCode)) {
        code = static_cast<float>(kAdcMaxCode);
    }
    const uint16_t code15 = static_cast<uint16_t>(std::lround(code));
    return static_cast<uint16_t>(code15 << 1);
}

void RtdSensor::printFault(Print& out, uint8_t faultCode) {
    if (faultCode == 0) {
        out.print("tidak ada fault");
        return;
    }
    bool first = true;
    for (const FaultText& item : kFaultTexts) {
        if ((faultCode & item.mask) == 0) {
            continue;
        }
        if (!first) {
            out.print(", ");
        }
        out.print(item.text);
        first = false;
    }
    if ((faultCode & kUndefinedFaultBits) != 0) {
        if (!first) {
            out.print(", ");
        }
        out.print("bit D1/D0 (tidak terdefinisi)");
    }
}

float RtdSensor::tempC() const { return tempC_; }
float RtdSensor::rtdResistance() const { return resistance_; }
uint8_t RtdSensor::fault() const { return fault_; }
bool RtdSensor::valid() const { return spiOk_ && sampleCount_ > 0 && fault_ == 0; }
bool RtdSensor::spiOk() const { return spiOk_; }
uint8_t RtdSensor::initFault() const { return initFault_; }
uint8_t RtdSensor::configRegister() const { return config_; }
uint16_t RtdSensor::rawCode() const { return rawCode_; }
