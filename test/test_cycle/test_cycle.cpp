// =====================================================================
//  test_cycle — siklus penuh CycleController + SimModel (RANCANGAN §12 U1)
//
//  Tiap skenario menjalankan siklus dalam waktu simulasi (langkah 10 ms),
//  dengan "operator" tiruan: membuka pintu di fase MUAT SAMPEL, dsb.
// =====================================================================

#include <CycleController.h>
#include <HeaterControl.h>
#include <SimModel.h>
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace hbcycle;

namespace {

struct RecordingSink : CycleSink {
    int started = 0;
    int finished = 0;
    int closed = 0;
    uint32_t seconds = 0;
    CycleResult last{};
    void onStarted(const CycleResult&) override { ++started; }
    void onSecond(const SecondLog&) override { ++seconds; }
    void onFinished(const CycleResult& r) override {
        ++finished;
        last = r;
    }
    void onLogClosed(uint32_t) override { ++closed; }
};

struct Run {
    Phase endPhase = Phase::Idle;
    CycleResult result{};
    float maxTempHeat = -1000.0f;     // suhu sensor maks setelah suhu heat tercapai
    float maxTempPreheat = -1000.0f;  // suhu sensor maks di fase TARE/MUAT/PULIH
    bool ssrWithoutRelay = false;
    bool heaterOnWithLidOpen = false;
    uint32_t endMs = 0;
    RecordingSink sink;
};

enum class Scenario { Normal, LidDuringHeat, CancelPreheat, LightSample, TempFault, StuckHeater, SlowDrying };

Settings testSettings() {
    Settings s = defaultSettings();
    s.general.preheatMin = 3;  // pre-heat lebih pendek supaya uji cepat
    return s;
}

void runScenario(Scenario scenario, Run& run, SimParams simParams = SimParams()) {
    SimModel sim(simParams);
    CycleController ctl;
    ctl.setSink(&run.sink);
    sim.reset(0);

    StartParams sp{};
    sp.settings = testSettings();
    sp.testId = 42;
    sp.startEpoch = 0;
    sp.zeroRaw = simParams.panMg * sim.rawPerMg();  // nol boot: cawan sudah di timbangan
    sp.countsPerGram = simParams.countsPerGram;
    sp.firmware = "TEST";

    bool lidClosed = true;
    uint32_t loadOpenedAt = 0;
    bool loadDone = false;
    bool cancelSent = false;
    bool lidHeatOpened = false;
    uint32_t openSince = 0;
    const uint32_t limitMs = 75u * 60u * 1000u;

    for (uint32_t t = 0; t < limitMs; t += 10) {
        sim.advance(t);
        const Phase phase = ctl.view().phase;

        if (t == 1000) {
            TEST_ASSERT_TRUE(ctl.start(sp, t));
        }
        // ---- operator tiruan ----
        sim.setLoadArmed(phase == Phase::Load);
        if (phase == Phase::Load && !loadDone) {
            if (loadOpenedAt == 0) {
                loadOpenedAt = t;
                lidClosed = false;
            } else if (t - loadOpenedAt >= 15000) {
                lidClosed = true;
                loadDone = true;
            }
        }
        if (scenario == Scenario::CancelPreheat && phase == Phase::Preheat && t > 60000 && !cancelSent) {
            ctl.cancel();
            cancelSent = true;
        }
        if (scenario == Scenario::LidDuringHeat && phase == Phase::Heat && ctl.view().heatSec > 240 && !lidHeatOpened) {
            lidClosed = false;
            lidHeatOpened = true;
        }
        if (scenario == Scenario::TempFault && phase == Phase::Tare) {
            sim.setTempFault(true);
        }
        if (scenario == Scenario::StuckHeater && phase == Phase::Tare) {
            sim.setStuckHeater(true);
        }
        sim.setLidClosed(lidClosed);

        MassSample sample;
        const bool fresh = sim.takeSample(sample);
        const ChamberReading chamber = sim.chamber();
        ctl.step(t, fresh ? &sample : nullptr, chamber, true);
        sim.setHeater(ctl.ssrOn());

        if (ctl.ssrOn() && !ctl.relayOn()) run.ssrWithoutRelay = true;
        if (!chamber.lidClosed) {
            if (openSince == 0) openSince = t;
            // relay boleh tertinggal <= 60 ms setelah pintu terbuka
            if (ctl.relayOn() && t - openSince > 100) run.heaterOnWithLidOpen = true;
        } else {
            openSince = 0;
        }
        const float sensor = chamber.tempC;
        if (phase == Phase::Heat && ctl.view().result.heatReachedSec >= 0.0f && sensor > run.maxTempHeat) {
            run.maxTempHeat = sensor;
        }
        if ((phase == Phase::Tare || phase == Phase::Load || phase == Phase::Recover) && sensor > run.maxTempPreheat) {
            run.maxTempPreheat = sensor;
        }
        if (phase == Phase::Done || phase == Phase::Failed) {
            run.endPhase = phase;
            run.result = ctl.view().result;
            run.endMs = t;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(run.ssrWithoutRelay, "SSR menyala tanpa relay K1");
}

void printResult(const char* name, const Run& run) {
    const CycleResult& r = run.result;
    std::printf("  [%s] fase=%s status=%d alasan=%s W0=%.1f Wf=%.1f MC=%.3f%% heat=%.0fs A=%.0fs t*=%.0fs tau=%.2f "
                "rms=%.2f menit=%u/%u Tmaks heat=%.2f pre=%.2f flags=%x\n",
                name, phaseName(run.endPhase), static_cast<int>(r.status), reasonCode(r.reason), r.w0Mg, r.wfMg,
                r.moisturePct, r.heatSec, r.phaseASec, r.tStarSec, r.tauMin, r.rmsMg,
                static_cast<unsigned>(r.validMinutes), static_cast<unsigned>(r.minuteCount), run.maxTempHeat,
                run.maxTempPreheat, static_cast<unsigned>(r.flags));
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------
void test_temp_controller_reaches_setpoint_without_big_overshoot() {
    SimModel sim;
    sim.reset(0);
    TempController pi;
    HeaterSequencer seq;
    float duty = 0.0f;
    uint32_t windowStart = 0;
    float maxTemp = 0.0f;
    float lastTemp = 0.0f;
    for (uint32_t t = 0; t < 20u * 60u * 1000u; t += 10) {
        sim.advance(t);
        const float temp = sim.chamber().tempC;
        if (t - windowStart >= 2000) {
            duty = pi.update(80.0f, temp, (t - windowStart) / 1000.0f);
            windowStart = t;
        }
        seq.update(t, true, (t - windowStart) < duty * 2000.0f);
        sim.setHeater(seq.ssr());
        if (temp > maxTemp) maxTemp = temp;
        lastTemp = temp;
    }
    std::printf("  PI 80 C: maks %.2f, akhir %.2f\n", maxTemp, lastTemp);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, lastTemp);
    TEST_ASSERT_TRUE(maxTemp < 83.0f);  // overshoot <= 3 C (RANCANGAN U3)
}

void test_sequencer_relay_before_ssr_and_ssr_off_first() {
    HeaterSequencer seq;
    seq.update(0, true, true);
    TEST_ASSERT_TRUE(seq.relay());
    TEST_ASSERT_FALSE(seq.ssr());  // belum settle
    seq.update(HeaterSequencer::kRelaySettleMs, true, true);
    TEST_ASSERT_TRUE(seq.ssr());
    seq.update(1000, false, true);
    TEST_ASSERT_FALSE(seq.ssr());
    TEST_ASSERT_TRUE(seq.relay());  // relay menunggu SSR mati dulu
    seq.update(1000 + HeaterSequencer::kSsrOffLeadMs, false, false);
    TEST_ASSERT_FALSE(seq.relay());
}

void test_cycle_normal_ok() {
    Run run;
    runScenario(Scenario::Normal, run);
    printResult("normal", run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(run.result.status));
    TEST_ASSERT_EQUAL(1, run.result.hasMoisture);
    // Air 5 % dari 5 g. Sebagian kecil menguap saat PULIH (sebelum W0) dan
    // sisa air di endpoint -> hasil sedikit di bawah 5 %.
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 4.7f, run.result.moisturePct);
    TEST_ASSERT_TRUE(run.result.phaseASec > 0.0f);
    TEST_ASSERT_TRUE(run.result.heatSec < run.result.maxHeatMin * 60.0f);
    TEST_ASSERT_TRUE(run.maxTempHeat < 110.0f + 3.0f);
    TEST_ASSERT_TRUE(run.maxTempPreheat < 80.0f + 3.0f);
    TEST_ASSERT_FALSE(run.heaterOnWithLidOpen);
    TEST_ASSERT_EQUAL(1, run.sink.started);
    TEST_ASSERT_EQUAL(1, run.sink.finished);
    TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(run.sink.last.status));
    TEST_ASSERT_TRUE(run.result.minuteCount >= 5);
}

void test_cycle_lid_open_during_heat_is_error() {
    Run run;
    runScenario(Scenario::LidDuringHeat, run);
    printResult("pintu", run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Lid), static_cast<int>(run.result.reason));
    TEST_ASSERT_EQUAL(0, run.result.hasMoisture);
}

void test_cycle_cancel() {
    Run run;
    runScenario(Scenario::CancelPreheat, run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Cancel), static_cast<int>(run.result.reason));
}

void test_cycle_light_sample_is_error_sample() {
    SimParams p;
    p.sampleMg = 2000.0f;
    Run run;
    runScenario(Scenario::LightSample, run, p);
    printResult("ringan", run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Sample), static_cast<int>(run.result.reason));
}

void test_cycle_temp_fault_is_error_sensor() {
    Run run;
    runScenario(Scenario::TempFault, run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Sensor), static_cast<int>(run.result.reason));
}

void test_cycle_stuck_heater_is_error_heater() {
    Run run;
    runScenario(Scenario::StuckHeater, run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Failed), static_cast<int>(run.endPhase));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Heater), static_cast<int>(run.result.reason));
}

void test_cycle_slow_drying_is_timeout() {
    SimParams p;
    p.dryTauMinAt110 = 40.0f;  // terlalu lambat untuk selesai dalam 25 menit
    Run run;
    runScenario(Scenario::SlowDrying, run, p);
    printResult("lambat", run);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Done), static_cast<int>(run.endPhase));
    TEST_ASSERT_TRUE(run.result.status == Status::Timeout || run.result.status == Status::Error);
    TEST_ASSERT_EQUAL(1, run.result.hasMoisture);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f * 60.0f, run.result.heatSec);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temp_controller_reaches_setpoint_without_big_overshoot);
    RUN_TEST(test_sequencer_relay_before_ssr_and_ssr_off_first);
    RUN_TEST(test_cycle_normal_ok);
    RUN_TEST(test_cycle_lid_open_during_heat_is_error);
    RUN_TEST(test_cycle_cancel);
    RUN_TEST(test_cycle_light_sample_is_error_sample);
    RUN_TEST(test_cycle_temp_fault_is_error_sensor);
    RUN_TEST(test_cycle_stuck_heater_is_error_heater);
    RUN_TEST(test_cycle_slow_drying_is_timeout);
    return UNITY_END();
}
