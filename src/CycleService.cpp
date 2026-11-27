#include "CycleService.h"

#include <Arduino.h>
#include <SimModel.h>

#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "NetClock.h"
#include "TestLog.h"
#include "app_config.h"
#include "pins.h"

#ifndef HB_SIMULASI
#define HB_SIMULASI 0
#endif
#ifndef HB_SIM_SPEED
#define HB_SIM_SPEED 10
#endif

namespace {

hbcycle::CycleController controller;
TestLog* testLog = nullptr;
NetClock* net = nullptr;

portMUX_TYPE viewLock = portMUX_INITIALIZER_UNLOCKED;
hbcycle::CycleView sharedView{};  // salinan untuk UI, dijaga viewLock
hbcycle::Settings pendingSettings{};
std::atomic<bool> startRequested{false};
std::atomic<bool> tareRequested{false};
std::atomic<bool> cancelRequested{false};
std::atomic<bool> ackRequested{false};
std::atomic<float> simTemp{NAN};
hbcycle::AdvancedSettings pendingAdvanced{};  // dijaga viewLock
std::atomic<bool> advancedPending{false};

uint32_t lastPublishMs = 0;
uint32_t lastCycMs = 0;
bool relayWritten = false;
bool ssrWritten = false;

#if HB_SIMULASI
hbcycle::SimModel sim;
// Waktu simulasi dipercepat. uint32 ms x 10 meluap setelah ~5 hari nyala.
uint32_t simNow(uint32_t realMs) { return realMs * static_cast<uint32_t>(HB_SIM_SPEED); }
#endif

void writeHeater(bool relay, bool ssr) {
    if (!relay) ssr = false;  // pengaman kedua: SSR tidak pernah tanpa K1
#if HB_SIMULASI
    // Simulasi: lampu asli TIDAK PERNAH dinyalakan.
    (void)relay;
    (void)ssr;
    relayWritten = false;
    ssrWritten = false;
#else
    if (ssr != ssrWritten && !ssr) {
        digitalWrite(PIN_HL_CONTROLLER, LOW);  // matikan SSR lebih dulu
        ssrWritten = false;
    }
    if (relay != relayWritten) {
        digitalWrite(PIN_HL_SAFETY, relay ? HIGH : LOW);
        relayWritten = relay;
    }
    if (ssr != ssrWritten && ssr) {
        digitalWrite(PIN_HL_CONTROLLER, HIGH);
        ssrWritten = true;
    }
#endif
}

// Parameter metode + PI dari OPSI PENGEMBANG (hanya saat siklus tidak berjalan)
void applyAdvanced(const hbcycle::AdvancedSettings& a) {
    controller.setConfig(hbcycle::applyMethod(a.method));
    controller.setPiParams(hbcycle::toPiParams(a.heater));
    Serial.printf("# Parameter: fase A %.1f mg/mnt x%u, target %.1f, stop +%u mnt, tare %u dtk, darurat +%u C |"
                  " PI kp %.3f ti %u dtk pita %u C maks %u %%\n",
                  a.method.phaseARate10 / 10.0, static_cast<unsigned>(a.method.phaseAConfirm),
                  a.method.targetRate10 / 10.0, static_cast<unsigned>(a.method.stopExtraMin),
                  static_cast<unsigned>(a.method.tareStableSec), static_cast<unsigned>(a.method.overTempMarginC),
                  a.heater.kp1000 / 1000.0, static_cast<unsigned>(a.heater.tiSec),
                  static_cast<unsigned>(a.heater.bandC), static_cast<unsigned>(a.heater.maxDutyPct));
}

void processRequests(uint32_t cycleNowMs, double zeroRaw, float countsPerGram) {
    if (advancedPending.load() && !controller.running()) {
        hbcycle::AdvancedSettings a;
        portENTER_CRITICAL(&viewLock);
        a = pendingAdvanced;
        portEXIT_CRITICAL(&viewLock);
        advancedPending.store(false);
        applyAdvanced(a);
    }
    if (startRequested.load()) {
        hbcycle::StartParams params{};
        portENTER_CRITICAL(&viewLock);
        params.settings = pendingSettings;
        portEXIT_CRITICAL(&viewLock);
        params.testId = testLog != nullptr ? testLog->nextId() : 1;
        params.startEpoch = (net != nullptr && net->synced()) ? static_cast<uint32_t>(time(nullptr)) : 0;
        params.firmware = app::kFirmwareVersion;
#if HB_SIMULASI
        sim.removeSample();
        params.zeroRaw = sim.params().panMg * sim.rawPerMg();  // cawan sudah dinolkan saat boot
        params.countsPerGram = sim.params().countsPerGram;
#else
        params.zeroRaw = zeroRaw;
        params.countsPerGram = countsPerGram;
#endif
        const bool ok = controller.start(params, cycleNowMs);
        Serial.printf("# SIKLUS %s: tes #%" PRIu32 " profil %u (%u/%u C), pre-heat %u mnt, min %u maks %u mnt%s\n",
                      ok ? "MULAI" : "DITOLAK", params.testId,
                      static_cast<unsigned>(params.settings.activeProfile + 1),
                      static_cast<unsigned>(params.settings.profiles[params.settings.activeProfile].preheatC),
                      static_cast<unsigned>(params.settings.profiles[params.settings.activeProfile].heatC),
                      static_cast<unsigned>(params.settings.general.preheatMin),
                      static_cast<unsigned>(params.settings.general.minHeatMin),
                      static_cast<unsigned>(params.settings.general.maxHeatMin), HB_SIMULASI ? " [SIMULASI]" : "");
        startRequested.store(false);
    }
    if (tareRequested.exchange(false)) controller.requestTare();
    if (cancelRequested.exchange(false)) controller.cancel();
    if (ackRequested.exchange(false)) controller.acknowledge();
#if !HB_SIMULASI
    (void)zeroRaw;
    (void)countsPerGram;
#endif
}

void printCycLine(uint32_t cycleNowMs) {
    // Baris CYC (tanpa "# ") tiap 1 dtk waktu siklus selama siklus berjalan:
    //   CYC,<fase>,<suhu C>,<setpoint C>,<duty %>,<berat mg>,<laju mg/mnt>,
    //       <detik heat>,<t_stop dtk>,<pintu 1=tutup>,<relay>,<ssr>
    if (!controller.running() || (cycleNowMs - lastCycMs) < 1000) {
        return;
    }
    lastCycMs = cycleNowMs;
    const hbcycle::CycleView& v = controller.view();
    float mass = v.currentMg;
    if (v.phase == hbcycle::Phase::Tare) mass = v.panMg;
    if (v.phase == hbcycle::Phase::Load || v.phase == hbcycle::Phase::Recover) mass = v.sampleMg;
    Serial.printf("CYC,%s,%.2f,%.0f,%.1f,%.1f,%.2f,%" PRIu32 ",%.0f,%d,%d,%d\n", hbcycle::phaseName(v.phase),
                  v.tempC, v.setpointC, v.dutyPct, mass, v.rateMgMin, v.heatSec, v.result.tStopSec,
                  v.lidClosed ? 1 : 0, controller.relayOn() ? 1 : 0, controller.ssrOn() ? 1 : 0);
}

}  // namespace

namespace cycle_service {

void begin(TestLog& log, NetClock& clock, const hbcycle::AdvancedSettings& advanced) {
    testLog = &log;
    net = &clock;
    pinMode(PIN_HL_CONTROLLER, OUTPUT);
    digitalWrite(PIN_HL_CONTROLLER, LOW);
    pinMode(PIN_HL_SAFETY, OUTPUT);
    digitalWrite(PIN_HL_SAFETY, LOW);

    applyAdvanced(advanced);
    controller.setSink(&log);
#if HB_SIMULASI
    sim.reset(simNow(millis()));
#endif
    sharedView = controller.view();
}

void step(uint32_t nowMs, const hbcycle::MassSample* sample, const hbcycle::ChamberReading& chamber,
          bool scaleHealthy, double zeroRaw, float countsPerGram) {
#if HB_SIMULASI
    (void)sample;
    (void)scaleHealthy;
    const uint32_t now = simNow(nowMs);
    sim.setLidClosed(chamber.lidClosed);  // pintu dari limit switch asli
    sim.setLoadArmed(controller.view().phase == hbcycle::Phase::Load);
    sim.advance(now);
    hbcycle::MassSample simSample;
    while (sim.takeSample(simSample)) {
        controller.step(simSample.ms, &simSample, sim.chamber(), true);
    }
    controller.step(now, nullptr, sim.chamber(), true);
    sim.setHeater(controller.ssrOn());
    simTemp.store(sim.chamber().tempC);
#else
    const uint32_t now = nowMs;
    controller.step(now, sample, chamber, scaleHealthy);
#endif
    processRequests(now, zeroRaw, countsPerGram);
    writeHeater(controller.relayOn(), controller.ssrOn());
    printCycLine(now);

    if ((nowMs - lastPublishMs) >= 50) {
        lastPublishMs = nowMs;
        portENTER_CRITICAL(&viewLock);
        sharedView = controller.view();
        portEXIT_CRITICAL(&viewLock);
    }
}

bool running() { return controller.running(); }

void forceHeaterOff() { writeHeater(false, false); }

void setAdvanced(const hbcycle::AdvancedSettings& advanced) {
    portENTER_CRITICAL(&viewLock);
    pendingAdvanced = advanced;
    portEXIT_CRITICAL(&viewLock);
    advancedPending.store(true);
}

bool simulation() { return HB_SIMULASI != 0; }
float simTempC() { return simTemp.load(); }

void printHelp() {
    Serial.println("#   y          status siklus + parameter PI lampu");
    Serial.println("#   y kp <x>   ubah Kp (duty per C), y ti <dtk>, y band <C> (RAM, hilang saat restart;");
    Serial.println("#              simpan permanen di LCD: PENGATURAN > OPSI PENGEMBANG > KONTROL PEMANAS)");
    Serial.println("#   l          daftar log tes di flash;  d <id>  ambil log tes (CSV LOGH/LOG/LOGM/LOGE)");
}

void command(char* args) {
    while (*args == ' ') ++args;
    hbcycle::PiParams pi = controller.piParams();
    if (*args != '\0') {
        char* value = args;
        while (*value != '\0' && *value != ' ') ++value;
        while (*value == ' ') *value++ = '\0';
        const float v = static_cast<float>(atof(value));
        if (strcmp(args, "kp") == 0 && v > 0.0f && v <= 1.0f) {
            pi.kp = v;
        } else if (strcmp(args, "ti") == 0 && v >= 5.0f && v <= 2000.0f) {
            pi.tiSec = v;
        } else if (strcmp(args, "band") == 0 && v >= 0.5f && v <= 50.0f) {
            pi.integralBandC = v;
        } else {
            Serial.println("# y: pakai 'y', 'y kp 0.05', 'y ti 120', atau 'y band 8'");
            return;
        }
        controller.setPiParams(pi);
    }
    const hbcycle::CycleView& v = controller.view();
    Serial.printf("# SIKLUS fase %s tes #%" PRIu32 " | suhu %.2f C setpoint %.0f C duty %.1f %% | relay %d ssr %d"
                  " | PI kp %.3f ti %.0f dtk pita %.1f C%s\n",
                  hbcycle::phaseName(v.phase), v.testId, v.tempC, v.setpointC, v.dutyPct,
                  controller.relayOn() ? 1 : 0, controller.ssrOn() ? 1 : 0, pi.kp, pi.tiSec, pi.integralBandC,
                  HB_SIMULASI ? " [SIMULASI]" : "");
}

// ---- UI ----
void requestStart(const hbcycle::Settings& settings) {
    portENTER_CRITICAL(&viewLock);
    pendingSettings = settings;
    portEXIT_CRITICAL(&viewLock);
    startRequested.store(true);
}

void requestTare() { tareRequested.store(true); }
void requestCancel() { cancelRequested.store(true); }
void requestAcknowledge() { ackRequested.store(true); }
bool startPending() { return startRequested.load(); }

void copyView(hbcycle::CycleView& out) {
    portENTER_CRITICAL(&viewLock);
    out = sharedView;
    portEXIT_CRITICAL(&viewLock);
}

}  // namespace cycle_service
