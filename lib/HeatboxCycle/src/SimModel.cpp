#include "SimModel.h"

#include <cmath>

namespace hbcycle {

SimModel::SimModel(const SimParams& params) : params_(params) {}

void SimModel::reset(uint32_t nowMs) {
    nowMs_ = nowMs;
    nextSampleMs_ = nowMs + 100;
    rng_ = params_.seed == 0 ? 1 : params_.seed;
    tempC_ = params_.ambientC;
    sensorC_ = params_.ambientC;
    waterMg_ = 0.0f;
    driftMg_ = 0.0f;
    sampleLoaded_ = false;
    lidWasOpenedArmed_ = false;
}

void SimModel::setLidClosed(bool closed) {
    if (!closed && loadArmed_) {
        lidWasOpenedArmed_ = true;
    }
    if (closed && !lidClosed_ && lidWasOpenedArmed_ && loadArmed_ && !sampleLoaded_) {
        sampleLoaded_ = true;  // operator memasukkan sampel saat pintu terbuka
        waterMg_ = params_.sampleMg * params_.waterFraction;
        lidWasOpenedArmed_ = false;
    }
    lidClosed_ = closed;
}

double SimModel::gaussian() {
    // Box-Muller dari LCG 32-bit (cukup untuk noise simulasi)
    auto next = [this]() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return (static_cast<double>(rng_ >> 8) + 0.5) / 16777216.0;
    };
    const double u1 = next();
    const double u2 = next();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

void SimModel::stepPhysics(float dtSec) {
    const bool lampOn = (heaterOn_ || stuckHeater_) && (lidClosed_ || stuckHeater_);
    const float power = lampOn ? 1.0f : 0.0f;
    const float loss = (tempC_ - params_.ambientC) * (lidClosed_ ? 1.0f : params_.lidOpenLoss);
    tempC_ += (params_.heaterRiseC * power - loss) * dtSec / params_.heatTauSec;
    sensorC_ += (tempC_ - sensorC_) * dtSec / params_.sensorLagSec;

    if (sampleLoaded_ && waterMg_ > 0.0f && tempC_ > 45.0f) {
        const float tauMin = params_.dryTauMinAt110 * std::exp((110.0f - tempC_) / params_.dryTempScaleC);
        waterMg_ -= waterMg_ * dtSec / (tauMin * 60.0f);
        if (waterMg_ < 0.0f) waterMg_ = 0.0f;
    }
    driftMg_ += params_.driftMgPerMin * dtSec / 60.0f;
}

void SimModel::advance(uint32_t nowMs) {
    while (static_cast<int32_t>(nowMs - nowMs_) >= 50) {
        stepPhysics(0.05f);
        nowMs_ += 50;
    }
}

bool SimModel::takeSample(MassSample& out) {
    if (static_cast<int32_t>(nowMs_ - nextSampleMs_) < 0) {
        return false;
    }
    nextSampleMs_ += 100;
    double mass = params_.panMg + driftMg_;
    if (sampleLoaded_) {
        mass += params_.sampleMg * (1.0f - params_.waterFraction) + waterMg_;
    }
    mass += gaussian() * params_.noiseMg;
    out.ms = nowMs_;
    out.raw = static_cast<int32_t>(std::lround(mass * rawPerMg()));
    out.ok = true;
    return true;
}

ChamberReading SimModel::chamber() const {
    ChamberReading reading;
    reading.tempOk = !tempFault_;
    reading.tempC = tempFault_ ? NAN : sensorC_;
    reading.lidClosed = lidClosed_;
    return reading;
}

}  // namespace hbcycle
