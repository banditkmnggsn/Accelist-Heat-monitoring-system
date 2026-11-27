#include "HeaterControl.h"

#include <cmath>

namespace hbcycle {

float TempController::update(float setpointC, float tempC, float dtSec) {
    if (!std::isfinite(tempC) || !std::isfinite(setpointC) || dtSec <= 0.0f) {
        lastDuty_ = 0.0f;
        return 0.0f;
    }
    const float error = setpointC - tempC;
    const float proportional = params_.kp * error;

    // Integral hanya dekat setpoint; di luar pita dibiarkan (tidak direset)
    // supaya keluaran tidak melonjak saat suhu turun sebentar (pintu dibuka).
    if (std::fabs(error) <= params_.integralBandC && params_.tiSec > 0.0f) {
        const float candidate = integral_ + params_.kp * error * dtSec / params_.tiSec;
        const float unclamped = proportional + candidate;
        // Anti-windup: jangan menambah integral ke arah yang sudah jenuh.
        const bool saturatedHigh = unclamped > params_.maxDuty && error > 0.0f;
        const bool saturatedLow = unclamped < 0.0f && error < 0.0f;
        if (!saturatedHigh && !saturatedLow) {
            integral_ = candidate;
        }
    }
    if (integral_ < 0.0f) integral_ = 0.0f;
    if (integral_ > params_.maxDuty) integral_ = params_.maxDuty;

    float duty = proportional + integral_;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > params_.maxDuty) duty = params_.maxDuty;
    lastDuty_ = duty;
    return duty;
}

void HeaterSequencer::update(uint32_t nowMs, bool want, bool ssrDemand) {
    if (!want) {
        if (ssr_) {
            ssr_ = false;
            ssrOffMs_ = nowMs;
        }
        if (relay_ && (nowMs - ssrOffMs_) >= kSsrOffLeadMs) {
            relay_ = false;
        }
        return;
    }
    if (!relay_) {
        relay_ = true;
        relayOnMs_ = nowMs;
    }
    const bool settled = (nowMs - relayOnMs_) >= kRelaySettleMs;
    const bool next = ssrDemand && settled;
    if (ssr_ && !next) {
        ssrOffMs_ = nowMs;
    }
    ssr_ = next;
}

}  // namespace hbcycle
