#pragma once

// =====================================================================
//  SensorInput.h — sumber data sensor untuk logika siklus
//
//  Logika siklus hanya melihat antarmuka ini, tidak tahu apakah datanya
//  dari hardware (HardwareSensors di src/) atau dari simulasi
//  (HeatBoxDummyData). Dengan begitu simulasi ikut menguji logika siklus,
//  dan waktu simulasi boleh berjalan lebih cepat dari waktu nyata.
// =====================================================================

#include <cstdint>

namespace hbcycle {

// Satu sampel berat dari ADC (10 SPS).
struct MassSample {
    uint32_t ms;  // waktu sampel menurut nowMs()
    int32_t raw;  // raw count SETELAH gerbang spike driver
    bool ok;      // false: sampel tidak boleh dipakai (mis. clip)
};

// Keadaan ruang pemanas saat ini.
struct ChamberReading {
    float tempC;     // NAN kalau tidak sah
    bool tempOk;     // sensor sehat dan bacaan wajar
    bool lidClosed;  // true kalau saklar tutup belum terpasang
};

class SensorInput {
public:
    virtual ~SensorInput() {}

    virtual uint32_t nowMs() = 0;

    // true sekali per sampel baru; isi `out`.
    virtual bool takeMassSample(MassSample& out) = 0;

    virtual ChamberReading chamber() = 0;
};

}  // namespace hbcycle
