#pragma once

// =====================================================================
//  SimModel.h — HeatBoxDummyData: tiruan fisik ruang pemanas + timbangan
//  (RANCANGAN §9, §12 U1)
//
//  Dipakai env PlatformIO "simulasi" (lampu asli tidak pernah dinyalakan)
//  dan uji PC test_cycle. Model sengaja sederhana tetapi cukup untuk
//  menguji logika siklus, kontrol PI, dan semua layar:
//    - suhu ruang: orde satu, lampu nyala/mati (SSR), pintu terbuka
//      mendinginkan lebih cepat; sensor PT100 tertinggal beberapa detik
//    - massa: cawan + sampel kering + air; air menguap eksponensial dengan
//      tau yang turun saat suhu naik; noise putih + drift kecil
//    - sampel otomatis masuk saat pintu dibuka lalu ditutup selama fase
//      MUAT SAMPEL (setLoadArmed)
//  C++ murni: waktu dari pemanggil (boleh dipercepat).
// =====================================================================

#include <cstdint>

#include "SensorInput.h"

namespace hbcycle {

struct SimParams {
    float ambientC = 30.0f;
    float heaterRiseC = 300.0f;   // kenaikan tunak kalau lampu 100 % terus
    float heatTauSec = 240.0f;    // konstanta waktu ruang
    float lidOpenLoss = 5.0f;     // pintu terbuka: pendinginan x ini
    float sensorLagSec = 6.0f;
    float countsPerGram = 5500.0f;
    float panMg = 21000.0f;       // cawan aluminium
    float sampleMg = 5000.0f;     // berat sampel basah
    float waterFraction = 0.05f;  // kadar air sampel
    float dryTauMinAt110 = 4.0f;  // tau pengeringan di 110 C
    float dryTempScaleC = 18.0f;  // tau x e^(-(T-110)/skala)
    float noiseMg = 3.8f;         // SD satu sampel 10 SPS
    float driftMgPerMin = 0.2f;
    uint32_t seed = 12345;
};

class SimModel {
public:
    explicit SimModel(const SimParams& params = SimParams());

    void reset(uint32_t nowMs);
    const SimParams& params() const { return params_; }
    SimParams& params() { return params_; }

    // Masukan
    void setHeater(bool on) { heaterOn_ = on; }
    void setLidClosed(bool closed);
    void setLoadArmed(bool armed) { loadArmed_ = armed; }
    void removeSample() { sampleLoaded_ = false; }
    void setStuckHeater(bool stuck) { stuckHeater_ = stuck; }   // uji: SSR macet nyala
    void setTempFault(bool fault) { tempFault_ = fault; }       // uji: sensor putus

    // Majukan model sampai nowMs (langkah internal 50 ms)
    void advance(uint32_t nowMs);

    // Satu sampel tiap 100 ms waktu simulasi
    bool takeSample(MassSample& out);
    ChamberReading chamber() const;

    float trueTempC() const { return tempC_; }
    float waterMg() const { return waterMg_; }
    bool sampleLoaded() const { return sampleLoaded_; }
    double rawPerMg() const { return params_.countsPerGram / 1000.0; }
    double zeroRaw() const { return 0.0; }

private:
    double gaussian();
    void stepPhysics(float dtSec);

    SimParams params_;
    uint32_t nowMs_ = 0;
    uint32_t nextSampleMs_ = 0;
    uint32_t rng_ = 1;
    float tempC_ = 30.0f;
    float sensorC_ = 30.0f;
    float waterMg_ = 0.0f;
    float driftMg_ = 0.0f;
    bool heaterOn_ = false;
    bool lidClosed_ = true;
    bool loadArmed_ = false;
    bool sampleLoaded_ = false;
    bool stuckHeater_ = false;
    bool tempFault_ = false;
    bool lidWasOpenedArmed_ = false;
};

}  // namespace hbcycle
