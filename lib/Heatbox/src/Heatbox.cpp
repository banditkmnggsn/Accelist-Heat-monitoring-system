#include "Heatbox.h"

static int32_t _medBuf[MEDIAN_N];
static uint8_t _medCount = 0, _medIdx = 0;

static int64_t _secAcc = 0;
static uint16_t _secCnt = 0;

static int32_t _secBuf[SEC_BUF_MAX];
static uint16_t _secN = 0;

#if ANALYSIS_MODE == 1
static int32_t _blkBuf[BLK_BUF_MAX];
static uint16_t _blkN = 0;
static int64_t _blkAcc = 0;
static uint16_t _blkCnt = 0;
#endif

static int32_t _tareCounts = 0;
static uint16_t _holdCnt = 0;
static HbStatus _st;

static inline float _mg(int32_t counts) {
    return (float)(counts - _tareCounts) / COUNTS_PER_MG;
}

void hbInit() {
    _medCount = _medIdx = 0;
    _secAcc = 0; _secCnt = 0; _secN = 0;
#if ANALYSIS_MODE == 1
    _blkN = 0; _blkAcc = 0; _blkCnt = 0;
#endif
    _tareCounts = 0; _holdCnt = 0;
    memset(&_st, 0, sizeof(_st));
    _st.phase = HB_IDLE;
    _st.stopReason = "";
}

void hbPushRaw(int32_t counts) {
#if MEDIAN_N > 1
    _medBuf[_medIdx] = counts;
    _medIdx = (uint8_t)((_medIdx + 1) % MEDIAN_N);
    if (_medCount < MEDIAN_N) _medCount++;
    int32_t v = heatbox::detail::median(_medBuf, _medCount);
#else
    int32_t v = counts;
#endif
    _secAcc += v;
    _secCnt++;
}

void hbTare() {
    if (_secN) _tareCounts = _secBuf[_secN - 1];
}

void hbStartDry() {
    _secN = 0;
    _holdCnt = 0;
    _st.t_dry_s = 0;
    _st.phase = HB_DRYING;
}

void hbTick1Hz() {
    if (_secCnt == 0) return;
    int32_t sec = (int32_t)(_secAcc / (int64_t)_secCnt);
    _secAcc = 0; _secCnt = 0;

    if (_secN < SEC_BUF_MAX) _secBuf[_secN++] = sec;
    else {
        _st.phase = HB_ERROR;
        _st.stopReason = "buffer penuh";
        return;
    }

    _st.raw_now = sec;
    _st.weight_mg = _mg(sec);

#if ANALYSIS_MODE == 1
    _blkAcc += sec;
    _blkCnt++;
    if (_blkCnt >= BLOCK_S) {
        if (_blkN < BLK_BUF_MAX) _blkBuf[_blkN++] = (int32_t)(_blkAcc / _blkCnt);
        _blkAcc = 0;
        _blkCnt = 0;
    }
#endif

    if (_st.phase != HB_DRYING) return;
    _st.t_dry_s++;

#if ANALYSIS_MODE == 0
    int n = (_secN < REG_WINDOW_S) ? _secN : REG_WINDOW_S;
    const int32_t *win = &_secBuf[_secN - n];
    float dt = 1.0f;
#else
    int n = (_blkN < REG_WINDOW_BLK) ? _blkN : REG_WINDOW_BLK;
    const int32_t *win = (_blkN >= (uint16_t)n) ? &_blkBuf[_blkN - n] : _blkBuf;
    float dt = (float)BLOCK_S;
#endif

    float slope_cps = heatbox::detail::slopeCountsPerSec(win, n, dt);
    _st.slope_mgmin = (slope_cps / COUNTS_PER_MG) * 60.0f;

#if USE_CURVE_FIT
    if (_st.t_dry_s >= (uint32_t)FIT_MIN_S && (_st.t_dry_s % FIT_EVERY_S) == 0) {
        _st.fit = heatbox::detail::fitCurve(_secBuf, _secN, 1.0f);
    }
#endif

    if (_st.t_dry_s >= (uint32_t)MAX_DRY_S) {
        _st.phase = HB_COOLING;
        _st.stopReason = "batas waktu maksimum";
        return;
    }
    if (_st.t_dry_s < (uint32_t)MIN_DRY_S) return;

#if USE_CURVE_FIT
    if (_st.fit.valid && _st.fit.resid_mg < RESID_STOP_MG) {
        _st.phase = HB_COOLING;
        _st.stopReason = "sisa air < ambang (fit)";
        return;
    }
#endif

    if (fabsf(_st.slope_mgmin) < SLOPE_STOP_MGMIN) {
        if (++_holdCnt >= (uint16_t)SLOPE_HOLD_S) {
            _st.phase = HB_COOLING;
            _st.stopReason = "laju di bawah ambang";
        }
    } else {
        _holdCnt = 0;
    }
}

bool hbStableWeight(float *out_mg) {
    if (_secN < STABLE_N) return false;
    const int32_t *p = &_secBuf[_secN - STABLE_N];

    float mean = 0.0f;
    for (int i = 0; i < STABLE_N; i++) mean += (float)p[i];
    mean /= (float)STABLE_N;

    float var = 0.0f;
    for (int i = 0; i < STABLE_N; i++) {
        float d = (float)p[i] - mean;
        var += d * d;
    }
    float sd_mg = sqrtf(var / (float)(STABLE_N - 1)) / COUNTS_PER_MG;
    if (sd_mg > STABLE_SD_MG) return false;

    *out_mg = (mean - (float)_tareCounts) / COUNTS_PER_MG;
    return true;
}

float hbMoisturePct(float w_start_mg, float w_end_mg) {
    if (w_start_mg <= 0.0f) return 0.0f;
    return (w_start_mg - w_end_mg) / w_start_mg * 100.0f;
}

const HbStatus* hbStatus() {
    return &_st;
}
