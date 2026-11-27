#include "AdvancedSettings.h"

#include <cmath>

#include "CycleSettings.h"  // crc32

namespace hbcycle {

namespace {

constexpr uint8_t kMagic0 = 'H';
constexpr uint8_t kMagic1 = 'A';
constexpr uint8_t kVersion = 1;
constexpr size_t kCrcOffset = kAdvancedBlobSize - 4;

bool in(uint32_t v, Range16 r) { return v >= r.min && v <= r.max; }

void put16(uint8_t* out, size_t& i, uint16_t v) {
    out[i++] = static_cast<uint8_t>(v & 0xFF);
    out[i++] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* data, size_t& i) {
    const uint16_t v = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
    i += 2;
    return v;
}

}  // namespace

AdvancedSettings defaultAdvanced() {
    const CycleConfig c;
    const PiParams pi;
    AdvancedSettings a{};
    a.method.phaseARate10 = static_cast<uint8_t>(std::lround(c.phaseARateMgMin * 10.0f));
    a.method.phaseAConfirm = c.phaseAConfirm;
    a.method.targetRate10 = static_cast<uint8_t>(std::lround(c.targetRateMgMin * 10.0f));
    a.method.stopExtraMin = static_cast<uint8_t>(std::lround(c.stopExtraMin));
    a.method.tareStableSec = static_cast<uint8_t>(c.tareStableSec);
    a.method.overTempMarginC = static_cast<uint8_t>(std::lround(c.overTempMarginC));
    a.heater.kp1000 = static_cast<uint16_t>(std::lround(pi.kp * 1000.0f));
    a.heater.tiSec = static_cast<uint16_t>(std::lround(pi.tiSec));
    a.heater.bandC = static_cast<uint8_t>(std::lround(pi.integralBandC));
    a.heater.maxDutyPct = static_cast<uint8_t>(std::lround(pi.maxDuty * 100.0f));
    a.display.quiet = true;
    a.display.deadbandMg = 3;
    return a;
}

AdvancedError validateMethod(const MethodParams& m) {
    if (!in(m.phaseARate10, kPhaseARate10Range) || !in(m.phaseAConfirm, kPhaseAConfirmRange) ||
        !in(m.targetRate10, kTargetRate10Range) || !in(m.stopExtraMin, kStopExtraRange) ||
        !in(m.tareStableSec, kTareStableRange) || !in(m.overTempMarginC, kOverTempRange)) {
        return AdvancedError::OutOfRange;
    }
    if (m.targetRate10 >= m.phaseARate10) {
        return AdvancedError::TargetNotBelowPhaseA;
    }
    return AdvancedError::None;
}

AdvancedError validateHeater(const HeaterParams& h) {
    if (!in(h.kp1000, kKp1000Range) || !in(h.tiSec, kTiRange) || !in(h.bandC, kBandRange) ||
        !in(h.maxDutyPct, kMaxDutyRange)) {
        return AdvancedError::OutOfRange;
    }
    return AdvancedError::None;
}

AdvancedError validateDisplay(const DisplayParams& d) {
    return in(d.deadbandMg, kDeadbandRange) ? AdvancedError::None : AdvancedError::OutOfRange;
}

AdvancedError validateAdvanced(const AdvancedSettings& a) {
    AdvancedError e = validateMethod(a.method);
    if (e == AdvancedError::None) e = validateHeater(a.heater);
    if (e == AdvancedError::None) e = validateDisplay(a.display);
    return e;
}

CycleConfig applyMethod(const MethodParams& m, CycleConfig base) {
    base.phaseARateMgMin = static_cast<float>(m.phaseARate10) / 10.0f;
    // "penyusutan nyata" tetap 1,5 x ambang (RANCANGAN §6.1)
    base.shrinkMinRateMgMin = base.phaseARateMgMin * 1.5f;
    base.phaseAConfirm = m.phaseAConfirm;
    base.targetRateMgMin = static_cast<float>(m.targetRate10) / 10.0f;
    base.stopExtraMin = static_cast<float>(m.stopExtraMin);
    base.tareStableSec = m.tareStableSec;
    base.overTempMarginC = static_cast<float>(m.overTempMarginC);
    return base;
}

PiParams toPiParams(const HeaterParams& h) {
    PiParams p;
    p.kp = static_cast<float>(h.kp1000) / 1000.0f;
    p.tiSec = static_cast<float>(h.tiSec);
    p.integralBandC = static_cast<float>(h.bandC);
    p.maxDuty = static_cast<float>(h.maxDutyPct) / 100.0f;
    return p;
}

void encodeAdvanced(const AdvancedSettings& a, uint8_t (&out)[kAdvancedBlobSize]) {
    size_t i = 0;
    out[i++] = kMagic0;
    out[i++] = kMagic1;
    out[i++] = kVersion;
    out[i++] = a.method.phaseARate10;
    out[i++] = a.method.phaseAConfirm;
    out[i++] = a.method.targetRate10;
    out[i++] = a.method.stopExtraMin;
    out[i++] = a.method.tareStableSec;
    out[i++] = a.method.overTempMarginC;
    put16(out, i, a.heater.kp1000);
    put16(out, i, a.heater.tiSec);
    out[i++] = a.heater.bandC;
    out[i++] = a.heater.maxDutyPct;
    out[i++] = a.display.quiet ? 1 : 0;
    out[i++] = a.display.deadbandMg;
    out[i++] = 0;  // cadangan
    const uint32_t crc = crc32(out, kCrcOffset);
    for (int b = 0; b < 4; ++b) {
        out[kCrcOffset + b] = static_cast<uint8_t>(crc >> (8 * b));
    }
}

bool decodeAdvanced(const uint8_t* data, size_t length, AdvancedSettings& out) {
    if (data == nullptr || length != kAdvancedBlobSize || data[0] != kMagic0 || data[1] != kMagic1 ||
        data[2] != kVersion) {
        return false;
    }
    uint32_t stored = 0;
    for (int b = 0; b < 4; ++b) {
        stored |= static_cast<uint32_t>(data[kCrcOffset + b]) << (8 * b);
    }
    if (stored != crc32(data, kCrcOffset)) {
        return false;
    }
    AdvancedSettings a{};
    size_t i = 3;
    a.method.phaseARate10 = data[i++];
    a.method.phaseAConfirm = data[i++];
    a.method.targetRate10 = data[i++];
    a.method.stopExtraMin = data[i++];
    a.method.tareStableSec = data[i++];
    a.method.overTempMarginC = data[i++];
    a.heater.kp1000 = get16(data, i);
    a.heater.tiSec = get16(data, i);
    a.heater.bandC = data[i++];
    a.heater.maxDutyPct = data[i++];
    if (data[i] > 1) {
        return false;
    }
    a.display.quiet = data[i++] == 1;
    a.display.deadbandMg = data[i++];
    if (validateAdvanced(a) != AdvancedError::None) {
        return false;
    }
    out = a;
    return true;
}

// ---------------------------------------------------------------------
float QuietValue::update(float value, const DisplayParams& d) {
    if (!std::isfinite(value)) {
        has_ = false;
        return value;
    }
    if (!d.quiet || !has_ || std::fabs(value - shown_) >= static_cast<float>(d.deadbandMg)) {
        shown_ = value;
        has_ = true;
    }
    return shown_;
}

void QuietValue::reset() { has_ = false; }

}  // namespace hbcycle
