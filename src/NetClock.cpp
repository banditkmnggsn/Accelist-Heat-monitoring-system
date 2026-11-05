#include "NetClock.h"

#include <Arduino.h>

#include <ctime>

#ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
#endif

#if ENABLE_WIFI
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#endif

namespace {

std::atomic<bool> gSynced{false};

#if ENABLE_WIFI
constexpr const char* kTimeZone = "WIB-7";  // UTC+7, tanpa DST
constexpr const char* kNtp1 = "id.pool.ntp.org";
constexpr const char* kNtp2 = "pool.ntp.org";
constexpr uint32_t kPollMs = 500;

void onTimeSync(struct timeval*) { gSynced.store(true); }
#endif

}  // namespace

void NetClock::begin() {
#if ENABLE_WIFI
    static char ssid[33] = "";
    static char pass[65] = "";
    Preferences prefs;
    if (prefs.begin("hbnet", true)) {
        prefs.getString("ssid", ssid, sizeof(ssid));
        prefs.getString("pass", pass, sizeof(pass));
        prefs.end();
    }
    if (ssid[0] == '\0') {
        snprintf(ssid, sizeof(ssid), "%s", WIFI_SSID);
        snprintf(pass, sizeof(pass), "%s", WIFI_PASS);
    }
    if (ssid[0] == '\0') {
        state_.store(hbui::WifiState::NoCredentials);
        return;
    }
    // persistent(false): jangan tulis kredensial ke flash tiap boot
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
    state_.store(hbui::WifiState::Connecting);
#endif
}

void NetClock::update(uint32_t nowMs) {
#if ENABLE_WIFI
    if (state_.load() == hbui::WifiState::NoCredentials || (nowMs - lastPollMs_) < kPollMs) {
        return;
    }
    lastPollMs_ = nowMs;
    const bool up = WiFi.status() == WL_CONNECTED;
    state_.store(up ? hbui::WifiState::Connected : hbui::WifiState::Connecting);
    rssi_.store(up ? static_cast<int8_t>(WiFi.RSSI()) : 0);
    if (up && !sntpStarted_) {
        // SNTP baru dimulai setelah ada jaringan; sinkron ulang tiap jam (bawaan)
        sntp_set_time_sync_notification_cb(onTimeSync);
        configTzTime(kTimeZone, kNtp1, kNtp2);
        sntpStarted_ = true;
    }
#else
    (void)nowMs;
#endif
}

bool NetClock::synced() const { return gSynced.load(); }

bool NetClock::localTime(uint8_t& hour, uint8_t& minute) const {
    if (!gSynced.load()) {
        return false;
    }
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    hour = static_cast<uint8_t>(local.tm_hour);
    minute = static_cast<uint8_t>(local.tm_min);
    return true;
}

const char* NetClock::stateText() const {
    switch (state_.load()) {
        case hbui::WifiState::Disabled: return "MATI(build)";
        case hbui::WifiState::NoCredentials: return "TANPA-SSID";
        case hbui::WifiState::Connecting: return "MENYAMBUNG";
        case hbui::WifiState::Connected: return "OK";
    }
    return "?";
}
