#include "HardwareSensors.h"

HardwareSensors::HardwareSensors(Ads1232& loadCell, RtdSensor& rtd, DoorClosedFn doorClosed)
    : loadCell_(loadCell), rtd_(rtd), doorClosed_(doorClosed) {}

uint32_t HardwareSensors::nowMs() { return millis(); }

bool HardwareSensors::takeMassSample(hbcycle::MassSample& out) {
    if (!loadCell_.hasNewSample()) {
        return false;
    }
    out.ms = millis();
    out.raw = loadCell_.accepted();
    out.ok = !loadCell_.isClipped();  // kode full-scale = overload / wiring
    return true;
}

hbcycle::ChamberReading HardwareSensors::chamber() {
    hbcycle::ChamberReading reading;
    reading.tempOk = rtd_.valid();
    reading.tempC = reading.tempOk ? rtd_.tempC() : NAN;
    reading.lidClosed = doorClosed_();
    return reading;
}
