#pragma once

// =====================================================================
//  HardwareSensors.h — SensorInput dari driver hardware sungguhan
//
//  Hanya membaca driver; update() driver tetap dipanggil task sensor.
//  Status pintu (limit switch GPIO34) dibaca task input lewat fungsi
//  yang diberikan pemanggil.
// =====================================================================

#include <Ads1232.h>
#include <RtdSensor.h>
#include <SensorInput.h>

class HardwareSensors : public hbcycle::SensorInput {
public:
    using DoorClosedFn = bool (*)();

    HardwareSensors(Ads1232& loadCell, RtdSensor& rtd, DoorClosedFn doorClosed);

    uint32_t nowMs() override;
    bool takeMassSample(hbcycle::MassSample& out) override;
    hbcycle::ChamberReading chamber() override;

private:
    Ads1232& loadCell_;
    RtdSensor& rtd_;
    DoorClosedFn doorClosed_;
};
