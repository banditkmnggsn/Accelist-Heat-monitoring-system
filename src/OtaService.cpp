#include "OtaService.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include <atomic>

#include "CycleService.h"
#include "NetClock.h"

#ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
#endif

#if ENABLE_WIFI
#include <ArduinoOTA.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif
#ifndef OTA_PASS
#define OTA_PASS ""
#endif

namespace {

NetClock* net = nullptr;
std::atomic<hbui::OtaState> otaState{hbui::OtaState::Off};
std::atomic<uint8_t> otaPct{0};
bool started = false;

#if ENABLE_WIFI
void startOta() {
    ArduinoOTA.setHostname(net->hostname());
    ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.onStart([]() {
        cycle_service::forceHeaterOff();
        otaPct.store(0);
        otaState.store(hbui::OtaState::Updating);
        Serial.println("# OTA: mulai menulis firmware baru (lampu dipaksa mati)");
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        esp_task_wdt_reset();  // transfer memblok task sensor
        if (total > 0) otaPct.store(static_cast<uint8_t>(static_cast<uint64_t>(done) * 100u / total));
    });
    ArduinoOTA.onEnd([]() {
        otaPct.store(100);
        Serial.println("# OTA: selesai, mulai ulang");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaState.store(hbui::OtaState::Failed);
        Serial.printf("# OTA: GAGAL (kode %u)\n", static_cast<unsigned>(error));
    });
    ArduinoOTA.begin();
    otaState.store(hbui::OtaState::Ready);
    Serial.printf("# OTA: siap, host %s.local (pio run -e ota -t upload)\n", net->hostname());
}
#endif

}  // namespace

namespace ota_service {

void begin(NetClock& clock) {
    net = &clock;
#if ENABLE_WIFI
    if (OTA_PASS[0] != '\0') {
        otaState.store(hbui::OtaState::Waiting);
    } else {
        Serial.println("# OTA: MATI (OTA_PASS kosong di include/secrets.h)");
    }
#endif
}

void update(bool allowed) {
#if ENABLE_WIFI
    const hbui::OtaState s = otaState.load();
    if (s == hbui::OtaState::Off) {
        return;
    }
    if (!started) {
        if (net->connected()) {
            startOta();  // mDNS + UDP baru bisa dibuka setelah ada jaringan
            started = true;
        }
        return;
    }
    if (allowed) {
        ArduinoOTA.handle();
    }
#else
    (void)allowed;
#endif
}

hbui::OtaState state() { return otaState.load(); }
uint8_t percent() { return otaPct.load(); }

}  // namespace ota_service
