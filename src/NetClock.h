#pragma once

// =====================================================================
//  NetClock.h — WiFi STA + jam NTP (WIB), non-blocking
//
//  Kredensial: NVS namespace "hbnet" (ssid, pass) kalau ada, kalau tidak
//  include/secrets.h (WIFI_SSID, WIFI_PASS). Layar input WiFi = Prompt 2.
//  Build flag ENABLE_WIFI=0: WiFi tidak di-link, status selalu Disabled.
//  begin() dari setup(); update() dari task sensor (core 0).
// =====================================================================

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

private:
    std::atomic<hbui::WifiState> state_{hbui::WifiState::Disabled};
    std::atomic<int8_t> rssi_{0};
    uint32_t lastPollMs_ = 0;
    bool sntpStarted_ = false;
};
