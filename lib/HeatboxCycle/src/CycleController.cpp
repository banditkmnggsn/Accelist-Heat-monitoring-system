#include "CycleController.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace hbcycle {

namespace {

constexpr float kNan = NAN;

bool heatingPhase(Phase p) {
    return p == Phase::Preheat || p == Phase::Tare || p == Phase::Load || p == Phase::Recover ||
           p == Phase::Heat;
}

double maxOf(double a, double b) { return a > b ? a : b; }

}  // namespace

CycleController::CycleController(const CycleConfig& config) : config_(config) {
    view_.phase = Phase::Idle;
    view_.tempC = kNan;
    view_.setpointC = kNan;
    view_.result.status = Status::None;
}

bool CycleController::setConfig(const CycleConfig& config) {
    if (running()) {
        return false;
    }
    config_ = config;
    return true;
}

bool CycleController::running() const {
    return heatingPhase(view_.phase) || view_.phase == Phase::Stopping;
}

// =====================================================================
//  Perintah
// =====================================================================
bool CycleController::start(const StartParams& params, uint32_t nowMs) {
    if (running() || validateSettings(params.settings) != SettingsError::None ||
        !std::isfinite(params.countsPerGram) || params.countsPerGram == 0.0f || !std::isfinite(params.zeroRaw)) {
        return false;
    }
    if (logging_ && sink_ != nullptr) {
        sink_->onLogClosed(view_.testId);  // log pendinginan tes sebelumnya dipotong
    }
    params_ = params;
    zeroRaw_ = params.zeroRaw;
    cpg_ = params.countsPerGram;

    const Settings& s = params.settings;
    const Profile& profile = s.profiles[s.activeProfile];

    const float tempNow = view_.tempC;
    const bool lidNow = view_.lidClosed;
    view_ = CycleView{};
    view_.testId = params.testId;
    view_.tempC = tempNow;
    view_.lidClosed = lidNow;
    view_.preheatTotalSec = static_cast<uint32_t>(s.general.preheatMin) * 60u;
    view_.preheatStartC = tempNow;
    view_.tareLimitSec = config_.tareTimeoutSec;
    view_.driftMgMin = kNan;
    view_.panMg = kNan;
    view_.sampleMg = kNan;
    view_.currentMg = kNan;
    view_.lossPct = kNan;
    view_.rateMgMin = kNan;

    CycleResult& r = view_.result;
    std::memset(&r, 0, sizeof(r));
    r.magic = kResultMagic;
    r.version = kResultVersion;
    r.id = params.testId;
    r.startEpoch = params.startEpoch;
    if (params.firmware != nullptr) {
        std::strncpy(r.firmware, params.firmware, sizeof(r.firmware) - 1);
    }
    r.profileIndex = s.activeProfile;
    r.preheatC = profile.preheatC;
    r.heatC = profile.heatC;
    r.preheatMin = s.general.preheatMin;
    r.minHeatMin = s.general.minHeatMin;
    r.maxHeatMin = s.general.maxHeatMin;
    r.status = Status::Running;
    r.reason = Reason::None;
    if (params.startEpoch == 0) r.flags |= flag::kNoClock;
    r.zeroRaw = params.zeroRaw;
    r.countsPerGram = params.countsPerGram;
    r.w0Mg = r.wfMg = r.moisturePct = r.dryBasisPct = kNan;
    r.phaseASec = r.tStarSec = r.tStopSec = r.heatReachedSec = -1.0f;
    r.tauMin = r.mInfMg = r.amplitudeMg = r.fitT0Min = r.rmsMg = r.r2 = r.asymptotePct = kNan;
    r.wfModelDevMg = r.tempAvgC = r.overshootC = r.dutyAvgPct = kNan;
    for (uint8_t i = 0; i < kMaxMinutes; ++i) {
        r.minutes[i].massMg = kNan;
        r.minutes[i].tempC = kNan;
    }

    seconds_.reset(nowMs);
    minutes_.reset();
    ringHead_ = 0;
    ringCount_ = 0;
    secondIndex_ = 0;
    t0Index_ = 0;
    pi_.reset();
    windowStartMs_ = nowMs;
    windowDuty_ = 0.0f;
    tempBad_ = false;
    scaleBad_ = false;
    invalidSecondsRun_ = 0;
    tareRequested_ = false;
    closedSinceSec_ = -1;
    minuteCount_ = 0;
    belowCount_ = 0;
    fitOk_ = false;
    stopAtSec_ = -1.0f;
    finishedSent_ = false;
    logging_ = true;

    enter(Phase::Preheat);
    if (sink_ != nullptr) {
        sink_->onStarted(r);
    }
    return true;
}

void CycleController::cancel() {
    if (heatingPhase(view_.phase)) {
        fail(Reason::Cancel);
    }
}

void CycleController::acknowledge() {
    if (view_.phase == Phase::Done || view_.phase == Phase::Failed) {
        view_.phase = Phase::Idle;  // hasil tetap di view_.result; log pendinginan tetap jalan
    }
}

// =====================================================================
//  Loop
// =====================================================================
void CycleController::step(uint32_t nowMs, const MassSample* sample, const ChamberReading& chamber,
                           bool scaleHealthy) {
    lastNowMs_ = nowMs;
    view_.tempC = chamber.tempOk ? chamber.tempC : kNan;
    view_.lidClosed = chamber.lidClosed;

    if (logging_) {
        SecondRecord rec;
        while (seconds_.poll(nowMs, rec)) {
            onSecond(rec, chamber);
            if (!logging_) break;
        }
        if (sample != nullptr && logging_) {
            seconds_.addSample(sample->ms, sample->raw, sample->ok);
        }
    }
    if (heatingPhase(view_.phase)) {
        checkSafety(nowMs, chamber, scaleHealthy);
    }
    controlHeater(nowMs, chamber);
}

bool CycleController::heatingAllowed(const ChamberReading& chamber) const {
    return heatingPhase(view_.phase) && chamber.lidClosed && chamber.tempOk && std::isfinite(chamber.tempC);
}

void CycleController::controlHeater(uint32_t nowMs, const ChamberReading& chamber) {
    const bool allowed = heatingAllowed(chamber);
    if ((nowMs - windowStartMs_) >= config_.windowMs) {
        const float dtSec = static_cast<float>(nowMs - windowStartMs_) / 1000.0f;
        windowStartMs_ = nowMs;
        windowDuty_ = allowed ? pi_.update(view_.setpointC, chamber.tempC, dtSec) : 0.0f;
        if (view_.phase == Phase::Heat) {
            dutySum_ += windowDuty_ * 100.0;
            ++dutyCount_;
        }
    }
    const float onMs = windowDuty_ * static_cast<float>(config_.windowMs);
    const bool demand = allowed && static_cast<float>(nowMs - windowStartMs_) < onMs;
    sequencer_.update(nowMs, allowed, demand);
    view_.dutyPct = allowed ? windowDuty_ * 100.0f : 0.0f;
    view_.heaterOn = sequencer_.relay();
}

void CycleController::checkSafety(uint32_t nowMs, const ChamberReading& chamber, bool scaleHealthy) {
    const bool tempValid = chamber.tempOk && std::isfinite(chamber.tempC);
    if (!tempValid) {
        if (!tempBad_) {
            tempBad_ = true;
            tempBadSinceMs_ = nowMs;
        } else if ((nowMs - tempBadSinceMs_) >= config_.tempFaultMs) {
            fail(Reason::Sensor);
            return;
        }
    } else {
        tempBad_ = false;
        const float sp = view_.setpointC;
        if (chamber.tempC > sp + config_.overTempMarginC || chamber.tempC > config_.overTempAbsC) {
            fail(Reason::Heater);  // lampu mati seketika (fase berubah)
            return;
        }
    }
    if (!scaleHealthy) {
        if (!scaleBad_) {
            scaleBad_ = true;
            scaleBadSinceMs_ = nowMs;
        } else if ((nowMs - scaleBadSinceMs_) >= config_.scaleFaultMs) {
            fail(Reason::Sensor);
        }
    } else {
        scaleBad_ = false;
    }
}

// =====================================================================
//  Per detik
// =====================================================================
void CycleController::onSecond(const SecondRecord& second, const ChamberReading& chamber) {
    secondIndex_ = second.index;
    RingSecond& slot = ring_[ringHead_];
    slot.index = second.index;
    slot.raw = second.rawMean;
    slot.valid = second.valid && std::isfinite(second.rawMean);
    ringHead_ = static_cast<uint16_t>((ringHead_ + 1) % kRing);
    if (ringCount_ < kRing) ++ringCount_;

    const bool tempValid = chamber.tempOk && std::isfinite(chamber.tempC);
    const float temp = tempValid ? chamber.tempC : kNan;
    if (sink_ != nullptr) {
        SecondLog log{second.index, second.rawMean, slot.valid, temp, view_.dutyPct, view_.phase, !chamber.lidClosed};
        sink_->onSecond(log);
    }

    if (!running()) {
        // Pendinginan: log diteruskan sampai coolingLogSec setelah stop
        if (second.index >= stopSecondIndex_ + config_.coolingLogSec) {
            logging_ = false;
            if (sink_ != nullptr) sink_->onLogClosed(view_.testId);
        }
        return;
    }

    // Berat tidak sah berturut-turut saat pintu tertutup = timbangan bermasalah
    const bool weighing = view_.phase == Phase::Tare || view_.phase == Phase::Recover || view_.phase == Phase::Heat;
    if (weighing && chamber.lidClosed) {
        invalidSecondsRun_ = slot.valid ? 0 : invalidSecondsRun_ + 1;
        if (invalidSecondsRun_ >= config_.invalidSecondsFault) {
            fail(Reason::Sensor);
            return;
        }
    } else {
        invalidSecondsRun_ = 0;
    }

    const bool inBand = tempValid && std::fabs(temp - view_.setpointC) <= config_.bandC;
    switch (view_.phase) {
        case Phase::Preheat: secondPreheat(chamber, inBand); break;
        case Phase::Tare: secondTare(chamber); break;
        case Phase::Load: secondLoad(chamber); break;
        case Phase::Recover: secondRecover(chamber, inBand); break;
        case Phase::Heat: secondHeat(second, chamber, inBand); break;
        case Phase::Stopping:
            ++view_.stoppingSec;
            if (view_.stoppingSec >= config_.stoppingShowSec) {
                enter(Phase::Done);
            }
            break;
        default: break;
    }
}

void CycleController::secondPreheat(const ChamberReading& chamber, bool inBand) {
    if (chamber.lidClosed) {
        ++view_.preheatElapsedSec;  // lama pre-heat dihitung hanya saat pintu tertutup
    }
    view_.preheatPaused = !chamber.lidClosed;
    const bool timeUp = view_.preheatElapsedSec >= view_.preheatTotalSec;
    view_.preheatWaiting = timeUp && !inBand;
    if (timeUp && inBand) {
        enter(Phase::Tare);
    } else if (view_.preheatElapsedSec >= view_.preheatTotalSec + config_.preheatExtraSec) {
        fail(Reason::Heater);  // suhu pre-heat tidak tercapai
    }
}

void CycleController::secondTare(const ChamberReading& chamber) {
    ++view_.tareSec;
    view_.panMg = static_cast<float>(recentMeanMg(5));
    const float drift = driftMgPerMin();
    view_.driftMgMin = drift;
    const bool stable = chamber.lidClosed && std::isfinite(drift) && std::fabs(drift) <= config_.tareDriftMaxMgMin;
    view_.stable = stable;
    view_.stableSec = stable ? view_.stableSec + 1 : 0;

    bool doTare = view_.stableSec >= config_.tareStableSec;
    if (tareRequested_) {
        tareRequested_ = false;
        if (chamber.lidClosed) doTare = true;
    }
    if (doTare) {
        const drying::Level level = recentLevel(30, true);
        double zero = level.ok ? level.level : NAN;
        if (!std::isfinite(zero)) {
            // data terlalu sedikit untuk garis: rata-rata raw detik sah terakhir
            double sum = 0.0;
            uint8_t n = 0;
            for (uint16_t i = 0; i < ringCount_ && i < 10; ++i) {
                const RingSecond& s = ring_[(ringHead_ + kRing - 1 - i) % kRing];
                if (s.valid) {
                    sum += s.raw;
                    ++n;
                }
            }
            zero = n > 0 ? sum / n : NAN;
        }
        if (std::isfinite(zero)) {
            zeroRaw_ = zero;
            view_.result.zeroRaw = zero;
            enter(Phase::Load);
            return;
        }
    }
    if (view_.tareSec >= config_.tareTimeoutSec) {
        fail(Reason::Preheat);
    }
}

void CycleController::secondLoad(const ChamberReading& chamber) {
    if (!chamber.lidClosed) {
        view_.lidOpenedAfterTare = true;
        closedSinceSec_ = -1;
    }
    view_.sampleMg = static_cast<float>(recentMeanMg(3));
    if (chamber.lidClosed && view_.lidOpenedAfterTare) {
        if (closedSinceSec_ < 0) closedSinceSec_ = static_cast<int32_t>(secondIndex_);
        const double m = view_.sampleMg;
        if (std::isfinite(m) && m >= config_.sampleMinMg && m <= config_.sampleMaxMg) {
            enter(Phase::Recover);
            return;
        }
        if (secondIndex_ - static_cast<uint32_t>(closedSinceSec_) >= config_.w0TimeoutSec) {
            fail(Reason::Sample);
        }
    }
}

void CycleController::secondRecover(const ChamberReading& chamber, bool inBand) {
    if (!chamber.lidClosed) {
        enter(Phase::Load);  // pintu dibuka lagi: kembali ke MUAT SAMPEL
        closedSinceSec_ = -1;
        return;
    }
    view_.recoverSec = secondIndex_ - static_cast<uint32_t>(closedSinceSec_);
    view_.inBandSec = inBand ? view_.inBandSec + 1 : 0;
    const drying::Level level = recentLevel(30, false);
    const bool calm = level.ok && level.used >= 25 && level.scale <= config_.w0CalmMaxMg;
    view_.weightCalm = calm;
    view_.sampleMg = static_cast<float>(recentMeanMg(3));

    if (view_.inBandSec >= config_.w0BandSec && calm) {
        const double w0 = level.level;
        if (w0 < config_.sampleMinMg || w0 > config_.sampleMaxMg) {
            fail(Reason::Sample);
            return;
        }
        if (w0 < config_.sampleIdealMinMg || w0 > config_.sampleIdealMaxMg) {
            view_.result.flags |= flag::kWeightOutside;
        }
        view_.result.w0Mg = static_cast<float>(w0);
        startHeat(secondIndex_ + 1);  // t = 0 tepat setelah detik ini
        return;
    }
    if (view_.recoverSec >= config_.w0TimeoutSec) {
        fail(Reason::Sample);
    }
}

void CycleController::startHeat(uint32_t t0Index) {
    t0Index_ = t0Index;
    minutes_.reset();
    minuteCount_ = 0;
    belowCount_ = 0;
    fitOk_ = false;
    stopAtSec_ = -1.0f;
    tempSum_ = 0.0;
    tempCount_ = 0;
    dutySum_ = 0.0;
    dutyCount_ = 0;
    minuteTempSum_ = 0.0;
    minuteTempCount_ = 0;
    enter(Phase::Heat);
}

void CycleController::secondHeat(const SecondRecord& second, const ChamberReading& chamber, bool inBand) {
    if (second.index < t0Index_) {
        return;
    }
    if (!chamber.lidClosed) {
        fail(Reason::Lid);
        return;
    }
    CycleResult& r = view_.result;
    const uint32_t hs = second.index - t0Index_;  // detik ke-hs sejak t = 0
    view_.heatSec = hs + 1;
    r.heatSec = static_cast<float>(hs + 1);

    if (chamber.tempOk && std::isfinite(chamber.tempC)) {
        if (inBand && r.heatReachedSec < 0.0f) {
            r.heatReachedSec = static_cast<float>(hs);
        }
        if (r.heatReachedSec >= 0.0f) {
            tempSum_ += chamber.tempC;
            ++tempCount_;
            const float over = chamber.tempC - static_cast<float>(r.heatC);
            if (!std::isfinite(r.overshootC) || over > r.overshootC) r.overshootC = over;
        }
        minuteTempSum_ += chamber.tempC;
        ++minuteTempCount_;
    }

    SecondRecord shifted = second;
    shifted.index = hs;
    shifted.tSec = second.tSec - static_cast<double>(t0Index_);
    const double mass = (second.valid && std::isfinite(second.rawMean)) ? massMg(second.rawMean) : NAN;
    MinuteRecord minute;
    if (minutes_.add(shifted, mass, minute)) {
        onMinute(minute);
        if (!heatingPhase(view_.phase)) return;
    }

    view_.currentMg = static_cast<float>(recentMeanMg(5));
    if (std::isfinite(view_.currentMg) && r.w0Mg > 0.0f) {
        view_.lossPct = (r.w0Mg - view_.currentMg) / r.w0Mg * 100.0f;
    }

    const float heatSec = static_cast<float>(hs + 1);
    if (stopAtSec_ >= 0.0f && heatSec >= stopAtSec_) {
        finish();
    } else if (heatSec >= static_cast<float>(r.maxHeatMin) * 60.0f) {
        finish();
    }
}

void CycleController::onMinute(const MinuteRecord& minute) {
    const uint32_t k = minuteCount_;
    if (k >= kMaxMinutes) {
        return;
    }
    CycleResult& r = view_.result;
    MinuteInfo& mi = mins_[k];
    mi.tMid = minute.tMidMin;
    mi.massMg = minute.massMg;
    mi.uncertaintyMg = minute.uncertaintyMg;
    mi.valid = minute.valid && std::isfinite(minute.massMg);
    mi.hasRate = false;
    mi.rateT = 0.0;
    mi.rate = 0.0;

    r.minutes[k].massMg = mi.valid ? static_cast<float>(minute.massMg) : kNan;
    r.minutes[k].tempC = minuteTempCount_ > 0 ? static_cast<float>(minuteTempSum_ / minuteTempCount_) : kNan;
    minuteTempSum_ = 0.0;
    minuteTempCount_ = 0;
    minuteCount_ = k + 1;
    r.minuteCount = static_cast<uint8_t>(minuteCount_);
    if (mi.valid) ++r.validMinutes;

    // Laju: Theil-Sen atas 5 menit terakhir, butuh >= 4 menit sah (§5.1)
    double xs[5];
    double ys[5];
    size_t n = 0;
    for (uint32_t i = (k >= 4 ? k - 4 : 0); i <= k; ++i) {
        if (mins_[i].valid) {
            xs[n] = mins_[i].tMid;
            ys[n] = mins_[i].massMg;
            ++n;
        }
    }
    if (n >= 4) {
        const drying::Slope slope = drying::theilSenSlope(xs, ys, n);
        if (slope.ok) {
            double tSum = 0.0;
            for (size_t i = 0; i < n; ++i) tSum += xs[i];
            mi.hasRate = true;
            mi.rate = -slope.slope;
            mi.rateT = tSum / static_cast<double>(n);
            if (static_cast<float>(mi.rate) > r.peakRateMgMin) r.peakRateMgMin = static_cast<float>(mi.rate);
        }
    }
    view_.rateMgMin = mi.hasRate ? static_cast<float>(mi.rate) : kNan;

    if (!view_.phaseB && !view_.fitFailed) {
        evaluatePhaseA(k);
    }
}

double CycleController::fitWindowStart() const {
    const CycleResult& r = view_.result;
    double start = r.heatReachedSec >= 0.0f ? r.heatReachedSec / 60.0 + 1.0 : 1e9;
    // awal periode laju menurun = menit terakhir yang lajunya >= 90 % puncak
    double fall = 0.0;
    for (uint32_t i = 0; i < minuteCount_; ++i) {
        if (mins_[i].hasRate && mins_[i].rate >= 0.9 * r.peakRateMgMin) fall = mins_[i].rateT;
    }
    return maxOf(start, fall);
}

void CycleController::evaluatePhaseA(uint32_t k) {
    const MinuteInfo& mi = mins_[k];
    const CycleResult& r = view_.result;
    if (mi.hasRate && mi.rate <= config_.phaseARateMgMin) {
        ++belowCount_;
    } else {
        belowCount_ = 0;
    }
    const float heatSec = static_cast<float>(view_.heatSec);
    const bool c1 = heatSec >= static_cast<float>(r.minHeatMin) * 60.0f;
    const bool c2 = r.heatReachedSec >= 0.0f &&
                    (heatSec - 1.0f - r.heatReachedSec) >= static_cast<float>(config_.heatBandHoldSec);
    const bool c4 = r.peakRateMgMin >= config_.shrinkMinRateMgMin;
    const double start = fitWindowStart();
    uint32_t points = 0;
    for (uint32_t i = 0; i <= k; ++i) {
        if (mins_[i].valid && mins_[i].tMid >= start) ++points;
    }
    const bool c3 = points >= config_.minFitPoints;
    if (c1 && c2 && c3 && c4 && belowCount_ >= config_.phaseAConfirm) {
        view_.result.phaseASec = heatSec;
        runFit(k);
    }
}

void CycleController::runFit(uint32_t k) {
    CycleResult& r = view_.result;
    const double start = fitWindowStart();
    double ts[drying::kMaxFitPoints];
    double ms[drying::kMaxFitPoints];
    double unc[drying::kMaxFitPoints];
    double rateTs[drying::kMaxFitPoints];
    double rates[drying::kMaxFitPoints];
    size_t n = 0;
    size_t nr = 0;
    for (uint32_t i = 0; i <= k && n < drying::kMaxFitPoints; ++i) {
        if (mins_[i].valid && mins_[i].tMid >= start) {
            ts[n] = mins_[i].tMid;
            ms[n] = mins_[i].massMg;
            unc[n] = mins_[i].uncertaintyMg;
            ++n;
        }
        if (mins_[i].hasRate && mins_[i].rateT >= start) {
            rateTs[nr] = mins_[i].rateT;
            rates[nr] = mins_[i].rate;
            ++nr;
        }
    }
    r.flags |= flag::kFitTried;

    drying::FitOptions options;
    options.tauMinMin = config_.tauMinMin;
    options.tauMaxMin = config_.tauMaxMin;
    fit_ = drying::fitExponential(ts, ms, n, false, options);

    const bool v1 = n >= config_.minFitPoints;
    const bool v2 = fit_.ok && fit_.amplitudeMg > 0.0 && fit_.tauMin >= config_.tauMinMin &&
                    fit_.tauMin <= config_.tauMaxMin && !fit_.tauAtBound;
    bool v3 = false;
    bool v4 = false;
    bool v5 = false;
    bool v6 = false;
    double tStar = NAN;
    if (v2 && n > 0) {
        r.tauMin = static_cast<float>(fit_.tauMin);
        r.mInfMg = static_cast<float>(fit_.mInfMg);
        r.amplitudeMg = static_cast<float>(fit_.amplitudeMg);
        r.fitT0Min = static_cast<float>(fit_.t0);
        r.rmsMg = static_cast<float>(fit_.rmsMg);
        r.r2 = static_cast<float>(fit_.r2);
        const double w0 = r.w0Mg;
        const double asym = (w0 - fit_.mInfMg) / w0 * 100.0;
        r.asymptotePct = static_cast<float>(asym);
        v3 = fit_.mInfMg < ms[n - 1] && asym >= 0.0 && asym <= 25.0;

        // V4: RMS residual <= max(3 x median ketidakpastian menit, 1 mg)
        double sorted[drying::kMaxFitPoints];
        size_t m = 0;
        for (size_t i = 0; i < n; ++i) {
            if (std::isfinite(unc[i])) sorted[m++] = unc[i];
        }
        for (size_t i = 1; i < m; ++i) {  // insertion sort, n <= 64
            const double v = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j - 1] > v) {
                sorted[j] = sorted[j - 1];
                --j;
            }
            sorted[j] = v;
        }
        const double medUnc = m == 0 ? 0.0 : (m % 2 ? sorted[m / 2] : 0.5 * (sorted[m / 2 - 1] + sorted[m / 2]));
        v4 = fit_.rmsMg <= maxOf(3.0 * medUnc, 1.0);

        // V5: laju model cocok dengan laju terukur terbaru (+-30 %). Laju
        // Theil-Sen mewakili TENGAH jendela 5 menit, jadi model dibaca di
        // waktu yang sama (bukan di titik terakhir, yang 2 menit lebih
        // lambat dan lajunya sudah lebih kecil).
        const double measured = mins_[k].hasRate ? mins_[k].rate : NAN;
        const double model = mins_[k].hasRate ? drying::modelRate(fit_, mins_[k].rateT) : NAN;
        v5 = std::isfinite(measured) && measured > 0.0 && std::fabs(model - measured) <= 0.3 * measured;

        // V6: ekstrapolasi tidak terlalu jauh
        const drying::StopPrediction pred = drying::predictStop(fit_, config_.targetRateMgMin);
        if (pred.ok) {
            tStar = pred.tStopMin;
            v6 = (tStar - ts[n - 1]) <= 2.0 * (ts[n - 1] - ts[0]);
        }
        // V7 (bendera saja): cross-check garis ln(laju)
        const drying::LogRateStop a1 = drying::logRateStop(rateTs, rates, nr, config_.targetRateMgMin);
        if (a1.ok && std::isfinite(tStar) &&
            std::fabs(tStar - a1.tStopMin) > maxOf(2.0, 0.25 * tStar)) {
            r.flags |= flag::kMethodsDisagree;
        }
    }

    if (v1 && v2 && v3 && v4 && v5 && v6) {
        fitOk_ = true;
        view_.phaseB = true;
        r.tStarSec = static_cast<float>(tStar * 60.0);
        const float tStop = static_cast<float>((tStar + config_.stopExtraMin) * 60.0);
        r.tStopSec = tStop;
        const float maxSec = static_cast<float>(r.maxHeatMin) * 60.0f;
        if (tStop >= maxSec) {
            view_.predictBeyondMax = true;  // lanjut ke waktu maksimum -> TIMEOUT
        } else {
            const float now = static_cast<float>(view_.heatSec);
            stopAtSec_ = tStop > now ? tStop : now;
        }
    } else {
        view_.fitFailed = true;  // lampu lanjut sampai waktu maksimum -> ERROR (FIT)
    }
}

// =====================================================================
//  Akhir siklus
// =====================================================================
void CycleController::finish() {
    CycleResult& r = view_.result;
    const drying::Level wf = recentLevel(60, false);
    if (!wf.ok || r.w0Mg <= 0.0f) {
        fail(Reason::Data);
        return;
    }
    const bool byPrediction = stopAtSec_ >= 0.0f && r.heatSec >= stopAtSec_;
    r.wfMg = static_cast<float>(wf.level);
    r.moisturePct = (r.w0Mg - r.wfMg) / r.w0Mg * 100.0f;
    r.dryBasisPct = r.moisturePct < 100.0f ? r.moisturePct / (100.0f - r.moisturePct) * 100.0f : kNan;
    r.hasMoisture = 1;
    r.validated = 1;

    if (byPrediction) {
        r.status = Status::Ok;
        r.reason = Reason::RateReached;
    } else if (view_.fitFailed) {
        r.status = Status::Error;
        r.reason = Reason::Fit;
        r.validated = 0;
    } else if (view_.predictBeyondMax) {
        r.status = Status::Timeout;
        r.reason = Reason::PredictBeyondMax;
    } else if (r.peakRateMgMin < config_.shrinkMinRateMgMin) {
        r.status = Status::Timeout;
        r.reason = Reason::SmallShrink;
        r.flags |= flag::kSmallShrink;
    } else {
        // Menit sah setelah suhu heat tercapai: kurang dari syarat fit = gangguan panjang
        uint32_t usable = 0;
        const double from = r.heatReachedSec >= 0.0f ? r.heatReachedSec / 60.0 + 1.0 : 1e9;
        for (uint32_t i = 0; i < minuteCount_; ++i) {
            if (mins_[i].valid && mins_[i].tMid >= from) ++usable;
        }
        if (usable < config_.minFitPoints) {
            r.status = Status::Error;
            r.reason = Reason::Data;
            r.validated = 0;
        } else {
            r.status = Status::Timeout;
            r.reason = Reason::MaxTime;
        }
    }

    if (fitOk_) {
        const double dev = r.wfMg - drying::modelMass(fit_, r.heatSec / 60.0, true);
        r.wfModelDevMg = static_cast<float>(dev);
        const double limit = maxOf(3.0, 3.0 * fit_.rmsMg);
        if (r.status == Status::Ok && std::fabs(dev) > limit) {
            // Mulus (3 menit terakhir sah) = model meleset, Wf tetap sah
            bool smooth = minuteCount_ >= 3;
            for (uint32_t i = minuteCount_ >= 3 ? minuteCount_ - 3 : 0; i < minuteCount_; ++i) {
                smooth = smooth && mins_[i].valid;
            }
            if (smooth) {
                r.flags |= flag::kModelDeviates;
            } else {
                r.status = Status::Error;
                r.reason = Reason::Wf;
                r.validated = 0;
            }
        }
    }
    if (tempCount_ > 0) r.tempAvgC = static_cast<float>(tempSum_ / tempCount_);
    if (dutyCount_ > 0) r.dutyAvgPct = static_cast<float>(dutySum_ / dutyCount_);

    stopSecondIndex_ = secondIndex_;
    enter(Phase::Stopping);
    closeResult();
}

void CycleController::fail(Reason reason) {
    if (!running() || view_.phase == Phase::Stopping) {
        return;
    }
    CycleResult& r = view_.result;
    r.status = Status::Error;
    r.reason = reason;
    r.hasMoisture = 0;
    r.validated = 0;
    if (tempCount_ > 0) r.tempAvgC = static_cast<float>(tempSum_ / tempCount_);
    stopSecondIndex_ = secondIndex_;
    enter(Phase::Failed);
    closeResult();
}

void CycleController::closeResult() {
    CycleResult& r = view_.result;
    r.secondsLogged = secondIndex_ + 1;
    r.crc = crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(CycleResult, crc));
    if (sink_ != nullptr && !finishedSent_) {
        sink_->onFinished(r);
    }
    finishedSent_ = true;
}

void CycleController::enter(Phase phase) {
    view_.phase = phase;
    phaseStartSec_ = secondIndex_;
    const CycleResult& r = view_.result;
    switch (phase) {
        case Phase::Preheat:
        case Phase::Tare:
        case Phase::Load:
        case Phase::Recover:
            view_.setpointC = r.preheatC;
            break;
        case Phase::Heat:
            view_.setpointC = r.heatC;
            break;
        default:
            view_.setpointC = kNan;
            break;
    }
    switch (phase) {
        case Phase::Tare:
            view_.tareSec = 0;
            view_.stableSec = 0;
            view_.stable = false;
            view_.driftMgMin = kNan;
            tareRequested_ = false;
            break;
        case Phase::Load:
            view_.sampleMg = kNan;
            break;
        case Phase::Recover:
            view_.inBandSec = 0;
            view_.weightCalm = false;
            view_.recoverSec = 0;
            break;
        case Phase::Stopping:
            view_.stoppingSec = 0;
            break;
        default:
            break;
    }
}

// =====================================================================
//  Bantuan data
// =====================================================================
double CycleController::massMg(double raw) const {
    return (raw - zeroRaw_) / static_cast<double>(cpg_) * 1000.0;
}

double CycleController::recentMeanMg(uint8_t seconds) const {
    double sum = 0.0;
    uint8_t n = 0;
    for (uint16_t i = 0; i < ringCount_ && i < seconds; ++i) {
        const RingSecond& s = ring_[(ringHead_ + kRing - 1 - i) % kRing];
        if (s.valid) {
            sum += massMg(s.raw);
            ++n;
        }
    }
    return n > 0 ? sum / n : NAN;
}

drying::Level CycleController::recentLevel(uint8_t seconds, bool useRaw) const {
    double ts[drying::kMaxLevelPoints];
    double ys[drying::kMaxLevelPoints];
    size_t n = 0;
    if (seconds > drying::kMaxLevelPoints) seconds = static_cast<uint8_t>(drying::kMaxLevelPoints);
    double tRef = 0.0;
    for (uint16_t i = 0; i < ringCount_ && i < seconds; ++i) {
        const RingSecond& s = ring_[(ringHead_ + kRing - 1 - i) % kRing];
        if (i == 0) tRef = static_cast<double>(s.index);
        if (s.valid) {
            ts[n] = static_cast<double>(s.index);
            ys[n] = useRaw ? s.raw : massMg(s.raw);
            ++n;
        }
    }
    if (n < 3) {
        drying::Level none{};
        none.ok = false;
        return none;
    }
    return drying::robustLevel(ts, ys, n, tRef);
}

float CycleController::driftMgPerMin() const {
    // Laju drift nol: garis robust lewat blok rata-rata 5 dtk dalam 3 menit
    // terakhir (RANCANGAN §4.2). Blok 5 dtk supaya 36 titik muat di buffer
    // robustLevel dan noise tiap titik turun ~sqrt(5).
    constexpr uint8_t kBlock = 5;
    constexpr uint8_t kMaxBlocks = 36;
    double ts[kMaxBlocks];
    double ys[kMaxBlocks];
    size_t n = 0;
    uint16_t i = 0;
    for (uint8_t b = 0; b < kMaxBlocks && i + kBlock <= ringCount_; ++b, i = static_cast<uint16_t>(i + kBlock)) {
        double tSum = 0.0;
        double mSum = 0.0;
        uint8_t good = 0;
        for (uint8_t j = 0; j < kBlock; ++j) {
            const RingSecond& s = ring_[(ringHead_ + kRing - 1 - (i + j)) % kRing];
            if (s.valid) {
                tSum += static_cast<double>(s.index);
                mSum += massMg(s.raw);
                ++good;
            }
        }
        if (good >= 3) {
            ts[n] = tSum / good / 60.0;
            ys[n] = mSum / good;
            ++n;
        }
    }
    if (n < 12) {
        return kNan;  // < 1 menit data
    }
    const drying::Level level = drying::robustLevel(ts, ys, n, ts[0]);
    return level.ok ? static_cast<float>(level.slope) : kNan;
}

// =====================================================================
const char* phaseName(Phase phase) {
    switch (phase) {
        case Phase::Idle: return "SIAP";
        case Phase::Preheat: return "PRE-HEAT";
        case Phase::Tare: return "TARE";
        case Phase::Load: return "MUAT";
        case Phase::Recover: return "PULIH";
        case Phase::Heat: return "HEAT";
        case Phase::Stopping: return "STOP";
        case Phase::Done: return "HASIL";
        case Phase::Failed: return "ERROR";
    }
    return "?";
}

const char* reasonCode(Reason reason) {
    switch (reason) {
        case Reason::None: return "-";
        case Reason::RateReached: return "LAJU < 1";
        case Reason::MaxTime: return "WAKTU MAKS";
        case Reason::SmallShrink: return "SUSUT KECIL";
        case Reason::PredictBeyondMax: return "PREDIKSI > MAKS";
        case Reason::Fit: return "E-FIT";
        case Reason::Data: return "E-DATA";
        case Reason::Wf: return "E-WF";
        case Reason::Lid: return "E-TUTUP";
        case Reason::Sensor: return "E-SENSOR";
        case Reason::Heater: return "E-HEATER";
        case Reason::Preheat: return "E-PREHEAT";
        case Reason::Sample: return "E-SAMPEL";
        case Reason::Cancel: return "E-BATAL";
        case Reason::Reset: return "E-RESET";
    }
    return "?";
}

}  // namespace hbcycle
