#pragma once

// =====================================================================
//  HardwareSensors.h — SensorInput dari driver hardware sungguhan
//
//  Hanya membaca driver; update() driver tetap dipanggil di loop().
//  Saklar tutup belum terpasang, jadi tutup selalu dilaporkan tertutup.
// =====================================================================

#include <Ads1232.h>
#include <RtdSensor.h>
#include <SensorInput.h>

class HardwareSensors : public hbcycle::SensorInput {
public:
    HardwareSensors(Ads1232& loadCell, RtdSensor& rtd);

    uint32_t nowMs() override;
    bool takeMassSample(hbcycle::MassSample& out) override;
    hbcycle::ChamberReading chamber() override;

private:
    Ads1232& loadCell_;
    RtdSensor& rtd_;
};
