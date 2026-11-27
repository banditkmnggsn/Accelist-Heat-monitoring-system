#pragma once

// =====================================================================
//  HeaterControl.h — kontrol suhu lampu halogen (RANCANGAN §4.2, §7)
//
//  - TempController: PI dengan keluaran duty 0..1, dihitung sekali per
//    jendela SSR (2 dtk, time-proportional). Integral hanya aktif di dekat
//    setpoint (pita integral) supaya tidak overshoot saat naik dari dingin,
//    dan tidak menumpuk saat keluaran jenuh (anti-windup).
//  - HeaterSequencer: urutan aman relay pengaman K1 + SSR sesuai catatan
//    skematik Heatlamp no. 6: tutup K1 dulu, baru SSR boleh menyala; saat
//    mati SSR dulu, K1 dilepas sesudahnya. Kontak K1 tidak pernah memutus
//    arus lampu.
//
//  C++ murni; penulisan pin ada di src/ (HeaterDriver).
// =====================================================================

#include <cstdint>

namespace hbcycle {

struct PiParams {
    float kp = 0.05f;             // duty per derajat galat (5 %/C)
    float tiSec = 120.0f;         // waktu integral
    float integralBandC = 8.0f;   // integral hanya dalam +-pita ini
    float maxDuty = 1.0f;
};

class TempController {
public:
    void reset() { integral_ = 0.0f; lastDuty_ = 0.0f; }
    void setParams(const PiParams& params) { params_ = params; }
    const PiParams& params() const { return params_; }

    // dtSec = lama jendela sebelumnya. Return duty 0..maxDuty.
    float update(float setpointC, float tempC, float dtSec);

    float lastDuty() const { return lastDuty_; }

private:
    PiParams params_{};
    float integral_ = 0.0f;  // dalam satuan duty
    float lastDuty_ = 0.0f;
};

class HeaterSequencer {
public:
    static constexpr uint32_t kRelaySettleMs = 150;  // K1 tertutup -> SSR boleh
    static constexpr uint32_t kSsrOffLeadMs = 60;    // SSR mati -> K1 dilepas

    // want = lampu boleh dipakai (fase memanaskan, pintu tertutup, tanpa
    // galat). ssrDemand = keinginan SSR menyala pada saat ini (jendela).
    void update(uint32_t nowMs, bool want, bool ssrDemand);

    bool relay() const { return relay_; }
    bool ssr() const { return ssr_; }

private:
    bool relay_ = false;
    bool ssr_ = false;
    uint32_t relayOnMs_ = 0;
    uint32_t ssrOffMs_ = 0;
};

}  // namespace hbcycle
