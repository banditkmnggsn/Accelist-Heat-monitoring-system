// =====================================================================
//  main.cpp — Heat Box: perekat tipis
//
//  Pembagian core (RANCANGAN §9):
//    core 1: ISR video composite (begin() dipanggil di sini, dari
//            loopTask), task input 1 kHz (rotary, pintu, buzzer), dan
//            loop() = UI (gambar ulang hanya saat ada perubahan, maks ~10 fps)
//    core 0: WiFi (bawaan ESP-IDF) + task sensor (ADS1232, MAX31865,
//            perintah serial, ringkasan 10 dtk)
//  Pemanas (SSR GPIO22, relay K1 GPIO21) dipaksa LOW di baris pertama
//  setup() dan tidak pernah dinyalakan di firmware ini.
// =====================================================================

#include <Arduino.h>
#include <ESP_8_BIT_GFX.h>
#include <Ui.h>

#include <atomic>

#include "InputTask.h"
#include "NetClock.h"
#include "SensorService.h"
#include "SettingsStore.h"
#include "app_config.h"
#include "pins.h"

#ifndef VIDEO_PAL
#define VIDEO_PAL 0
#endif

namespace {

ESP_8_BIT_GFX video(VIDEO_PAL == 0, 8);  // true = NTSC, warna 8 bit RGB332, GPIO25
InputTask input;
NetClock net;
SettingsStore settingsStore;
std::atomic<uint32_t> renderCount{0};

// UI -> logika. Layar tidak tahu NVS, buzzer, atau driver.
class AppCommands : public hbui::UiCommands {
public:
    void playSound(hbui::Sound sound, bool force) override { input.buzzer().play(sound, force); }
    bool saveSettings(const hbcycle::Settings& settings) override {
        input.buzzer().setEnabled(settings.soundOn);
        return settingsStore.save(settings);
    }
    void recheckSensors() override { sensor_service::requestRecheck(); }
};

AppCommands commands;
hbui::Ui ui(commands);
hbui::UiSnapshot snapshot{};
uint32_t lastSnapshotMs = 0;
uint32_t lastRenderMs = 0;

void refreshSnapshot(uint32_t nowMs) {
    const sensor_service::Status sensors = sensor_service::status();
    snapshot.nowMs = nowMs;
    snapshot.scaleCheck = sensors.scaleCheck;
    snapshot.tempCheck = sensors.tempCheck;
    snapshot.scaleOk = sensors.scaleOk;
    snapshot.tempOk = sensors.tempOk;
    snapshot.tempC = sensors.tempC;
    snapshot.doorClosed = input.doorClosed();
    snapshot.wifi = net.state();
    snapshot.rssi = net.rssi();
    snapshot.clockSynced = net.localTime(snapshot.hour, snapshot.minute);
    snapshot.testRunning = false;  // siklus dibuat di Prompt 2
    snapshot.holdProgressMs = input.holdProgressMs();
    snapshot.input = input.diag();
}

}  // namespace

void setup() {
    // Pemanas MATI sedini mungkin (R16/R17 juga menahan basis LOW).
    pinMode(PIN_HL_CONTROLLER, OUTPUT);
    digitalWrite(PIN_HL_CONTROLLER, LOW);
    pinMode(PIN_HL_SAFETY, OUTPUT);
    digitalWrite(PIN_HL_SAFETY, LOW);

    Serial.setTxBufferSize(app::kSerialTxBufferSize);  // wajib sebelum begin()
    Serial.begin(app::kSerialBaud);
    Serial.println();
    Serial.printf("# %s v%s (build %s %s)\n", app::kFirmwareName, app::kFirmwareVersion, __DATE__, __TIME__);
    Serial.printf("# ESP32 %u core, loop/UI di core %d\n", static_cast<unsigned>(ESP.getChipCores()),
                  static_cast<int>(xPortGetCoreID()));

    bool settingsRepaired = false;
    const hbcycle::Settings settings = settingsStore.load(settingsRepaired);
    if (settingsRepaired) {
        Serial.println("# Pengaturan (NVS hbcfg) kosong/rusak -> bawaan ditulis ulang");
    }

    input.begin(settings.soundOn);  // buzzer: GPIO12 LOW dulu, baru task input jalan

    const uint32_t heapBefore = ESP.getFreeHeap();
    video.begin();             // ISR video di core ini (core 1)
    video.copyAfterSwap = false;  // tiap render menggambar layar penuh
    net.begin();
    Serial.printf("# Video %s 256x240: framebuffer %u B. Heap bebas setelah video + WiFi: %u B (blok terbesar %u B)\n",
                  VIDEO_PAL ? "PAL" : "NTSC", static_cast<unsigned>(heapBefore - ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));

    sensor_service::start(input, net, renderCount);

    refreshSnapshot(millis());
    ui.begin(settings, snapshot);
    input.buzzer().play(hbui::Sound::Welcome, false);
}

void loop() {
    const uint32_t nowMs = millis();
    if ((nowMs - lastSnapshotMs) >= app::ui::kSnapshotPeriodMs) {
        lastSnapshotMs = nowMs;
        refreshSnapshot(nowMs);
    }

    hbinput::InputEvent event;
    while (input.nextEvent(event)) {
        refreshSnapshot(millis());
        ui.handle(event, snapshot);
    }
    ui.update(snapshot);
    input.setHoldTarget(ui.holdSpec(snapshot).ms);

    if (ui.needsRender() && (nowMs - lastRenderMs) >= app::ui::kMinRenderIntervalMs) {
        lastRenderMs = nowMs;
        hbui::Painter painter(video, ui.style());
        ui.render(painter, snapshot);
        video.waitForFrame();  // tukar buffer saat vsync (tidur, bukan busy-wait)
        renderCount.fetch_add(1);
    } else {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
