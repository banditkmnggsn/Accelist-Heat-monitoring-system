#pragma once

#include <Arduino.h>

// =====================================================================
//  Enumerasi dan struktur data publik
// =====================================================================

enum HbPhase : uint8_t {
    HB_IDLE = 0,
    HB_TARE,
    HB_WEIGH_START,
    HB_DRYING,
    HB_COOLING,
    HB_WEIGH_END,
    HB_DONE,
    HB_ERROR
};

struct HbFit {
    bool  valid;
    float W_inf;
    float A;
    float tau_s;
    float k_cps;
    float resid_mg;
    float t_stop_s;
    float rmse_mg;
};

struct HbStatus {
    HbPhase phase;
    uint32_t t_dry_s;
    int32_t  raw_now;
    float    weight_mg;
    float    slope_mgmin;
    float    W_start_mg;
    float    W_end_mg;
    float    moisture_pct;
    HbFit    fit;
    const char *stopReason;
};
