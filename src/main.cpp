// =====================================================================
//  main.cpp — Heat Box: perekat tipis
//
//  Pembagian core (RANCANGAN §9):
//    core 1: ISR video composite (begin() dipanggil di sini, dari
//            loopTask), task input 1 kHz (rotary, pintu, buzzer), dan
//            loop() = UI (gambar ulang hanya saat ada perubahan, maks ~10 fps)
//    core 0: WiFi (bawaan ESP-IDF) + task sensor (ADS1232, MAX31865,
//            siklus, perintah serial, ringkasan 10 dtk, WiFi/NTP, OTA)
//  Pemanas (SSR GPIO22, relay K1 GPIO21) dipaksa LOW di baris pertama
//  setup(); setelah itu HANYA ditulis oleh siklus (src/CycleService, task
//  sensor). Build simulasi (env "simulasi") tidak pernah menyalakannya.
// =====================================================================

#include <Arduino.h>
#include <ESP_8_BIT_GFX.h>
#include <Ui.h>

#include <atomic>

#include "CycleService.h"
#include "InputTask.h"
#include "NetClock.h"
#include "OtaService.h"
#include "SensorService.h"
#include "SettingsStore.h"
#include "TestLog.h"
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
TestLog testLog;
std::atomic<uint32_t> renderCount{0};

// UI -> logika. Layar tidak tahu NVS, buzzer, atau driver.
class AppCommands : public hbui::UiCommands {
public:
    void playSound(hbui::Sound sound, bool force) override { input.buzzer().play(sound, force); }
    bool saveSettings(const hbcycle::Settings& settings) override {
        input.buzzer().setEnabled(settings.soundOn);
        return settingsStore.save(settings);
    }
    bool saveAdvanced(const hbcycle::AdvancedSettings& advanced) override {
        cycle_service::setAdvanced(advanced);  // berlaku untuk tes berikutnya
        return settingsStore.saveAdvanced(advanced);
    }
    void recheckSensors() override { sensor_service::requestRecheck(); }
    void startTest(const hbcycle::Settings& settings) override { cycle_service::requestStart(settings); }
    void tareNow() override { cycle_service::requestTare(); }
    void cancelTest() override { cycle_service::requestCancel(); }
    void acknowledgeResult() override { cycle_service::requestAcknowledge(); }
    void acknowledgeInterrupted() override { testLog.clearInterrupted(); }
    uint8_t historyList(hbcycle::HistoryEntry* out, uint8_t max) override { return testLog.list(out, max); }
    bool loadResult(uint32_t id, hbcycle::CycleResult& out) override { return testLog.load(id, out); }
    bool clearLogs() override { return testLog.clearAll(); }
    void wifiScan() override { net.requestScan(); }
    uint8_t wifiResults(hbui::WifiNetwork* out, uint8_t max) override { return net.scanResults(out, max); }
    void wifiConnect(const char* ssid, const char* pass) override { net.requestConnect(ssid, pass); }
    void wifiReconnect() override { net.requestReconnect(); }
    void clockSync() override { net.requestSync(); }
    void scaleJob(hbui::ScaleJob job, float grams) override { sensor_service::requestScaleJob(job, grams); }
    void scaleRestore(float countsPerGram) override { sensor_service::requestScaleRestore(countsPerGram); }
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
    cycle_service::copyView(snapshot.cycle);
    const hbcycle::Phase phase = snapshot.cycle.phase;
    snapshot.testRunning = phase != hbcycle::Phase::Idle && phase != hbcycle::Phase::Done &&
                           phase != hbcycle::Phase::Failed;
    snapshot.startPending = cycle_service::startPending();
    snapshot.hasLast = testLog.last(snapshot.last);
    snapshot.historyCount = testLog.count();
    snapshot.historyCapacity = TestLog::kMaxTests;
    snapshot.interruptedId = testLog.interruptedId();
    snapshot.simulation = cycle_service::simulation();
    net.info(snapshot.net);
    snapshot.net.ota = ota_service::state();
    snapshot.net.otaPct = ota_service::percent();
    sensor_service::detail(snapshot.scale, snapshot.rtd);
    snapshot.device.totalTests = testLog.totalTests();
    snapshot.device.logFreeKb = testLog.freeKb();
    snapshot.device.logTotalKb = testLog.totalKb();
    snapshot.holdProgressMs = input.holdProgressMs();
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
    bool advancedRepaired = false;
    const hbcycle::AdvancedSettings advanced = settingsStore.loadAdvanced(advancedRepaired);
    if (advancedRepaired) {
        Serial.println("# Opsi pengembang (NVS hbcfg/adv) kosong/rusak -> bawaan ditulis ulang");
    }

    input.begin(settings.soundOn);  // buzzer: GPIO12 LOW dulu, baru task input jalan

    // Log tes di flash. Boot pertama memformat partisi (beberapa detik).
    const uint32_t fsStart = millis();
    if (testLog.begin()) {
        Serial.printf("# LittleFS: %u tes tersimpan (%lu ms)%s\n", static_cast<unsigned>(testLog.count()),
                      static_cast<unsigned long>(millis() - fsStart),
                      testLog.interruptedId() != 0 ? ", ADA TES TERPUTUS" : "");
    } else {
        Serial.println("# LittleFS GAGAL di-mount: hasil tes tidak disimpan");
    }
    cycle_service::begin(testLog, net, advanced);

    const uint32_t heapBefore = ESP.getFreeHeap();
    video.begin();             // ISR video di core ini (core 1)
    video.copyAfterSwap = false;  // tiap render menggambar layar penuh
    net.begin();
    net.deviceIds(snapshot.device.mac, snapshot.device.id);  // tetap selama alat nyala
    ota_service::begin(net);
    Serial.printf("# Video %s 256x240: framebuffer %u B. Heap bebas setelah video + WiFi: %u B (blok terbesar %u B)\n",
                  VIDEO_PAL ? "PAL" : "NTSC", static_cast<unsigned>(heapBefore - ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));

    sensor_service::start(input, net, testLog, renderCount);

    refreshSnapshot(millis());
    ui.begin(settings, advanced, snapshot);
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
        hbui::Painter painter(video);
        ui.render(painter, snapshot);
        video.waitForFrame();  // tukar buffer saat vsync (tidur, bukan busy-wait)
        renderCount.fetch_add(1);
    } else {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
