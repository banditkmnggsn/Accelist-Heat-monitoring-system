#pragma once

// =====================================================================
//  NetClock.h — WiFi STA + jam NTP (WIB) + layar WIFI & JAM, non-blocking
//
//  Kredensial: NVS namespace "hbnet" (ssid, pass) kalau ada, kalau tidak
//  include/secrets.h (WIFI_SSID, WIFI_PASS). Diganti dari LCD lewat
//  PENGATURAN > WIFI & JAM: SSID/sandi baru DICOBA dulu dan baru disimpan
//  ke NVS setelah tersambung; kalau gagal, WiFi lama dipakai lagi.
//  Build flag ENABLE_WIFI=0: WiFi tidak di-link sama sekali.
//
//  Thread: begin() dari setup(); update() dari task sensor (core 0) dan
//  HANYA di situ API WiFi dipanggil. Permintaan UI (core 1) disimpan dulu
//  (request*) lalu dikerjakan di update(). Data bersama dijaga portMUX.
// =====================================================================

#include <Arduino.h>
#include <UiModel.h>

#include <atomic>
#include <cstdint>

class NetClock {
public:
    void begin();
    void update(uint32_t nowMs);

    hbui::WifiState state() const { return state_.load(); }
    int8_t rssi() const { return rssi_.load(); }
    bool synced() const;
    // Jam lokal WIB; false sebelum jam pernah sinkron
    bool localTime(uint8_t& hour, uint8_t& minute) const;
    const char* stateText() const;
    const char* hostname() const { return host_; }  // "heatbox-8b102e"
    bool connected() const { return state_.load() == hbui::WifiState::Connected; }

    // ---- UI (core 1) ----
    void info(hbui::NetInfo& out);  // tanpa kolom OTA
    void deviceIds(char (&mac)[18], char (&id)[12]) const;
    void requestScan() { scanRequested_.store(true); }
    uint8_t scanResults(hbui::WifiNetwork* out, uint8_t max);
    void requestConnect(const char* ssid, const char* pass);
    void requestReconnect() { reconnectRequested_.store(true); }
    void requestSync() { syncRequested_.store(true); }

private:
    struct Credentials {
        char ssid[33];
        char pass[65];
    };
    static constexpr uint8_t kMaxNetworks = 12;

    void startScan();
    void pollScan();
    void startTrial(uint32_t nowMs);
    void pollTrial(uint32_t nowMs);
    void connect(const Credentials& c);
    void publishInfo(bool up);

    std::atomic<hbui::WifiState> state_{hbui::WifiState::Disabled};
    std::atomic<int8_t> rssi_{0};
    uint32_t lastPollMs_ = 0;
    bool sntpStarted_ = false;
    char host_[24] = "heatbox";
    uint8_t mac_[6] = {};

    // Kredensial aktif dan percobaan (task sensor saja)
    Credentials current_{};
    Credentials trial_{};
    bool trying_ = false;
    uint32_t trialStartMs_ = 0;
    bool resumeAfterScan_ = false;
    bool scanning_ = false;

    // Permintaan UI
    std::atomic<bool> scanRequested_{false};
    std::atomic<bool> connectRequested_{false};
    std::atomic<bool> reconnectRequested_{false};
    std::atomic<bool> syncRequested_{false};

    // Bersama UI <-> task sensor
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    Credentials requested_{};
    hbui::NetInfo info_{};
    hbui::WifiNetwork networks_[kMaxNetworks] = {};
    uint8_t networkCount_ = 0;
};
