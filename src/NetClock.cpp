#include "NetClock.h"

#include <esp_mac.h>

#include <cstring>
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
std::atomic<uint32_t> gLastSync{0};
std::atomic<uint8_t> gDisconnectReason{0};

#if ENABLE_WIFI
constexpr const char* kTimeZone = "WIB-7";  // UTC+7, tanpa DST
constexpr const char* kNtp1 = "id.pool.ntp.org";
constexpr const char* kNtp2 = "pool.ntp.org";
constexpr const char* kNvs = "hbnet";
constexpr uint32_t kPollMs = 500;
constexpr uint32_t kTrialTimeoutMs = 20000;  // SSID/sandi baru harus tersambung dalam ini

// Alasan putus dari ESP-IDF (esp_wifi_types.h)
constexpr uint8_t kReasonAuthExpire = 2;
constexpr uint8_t kReason4WayTimeout = 15;
constexpr uint8_t kReasonNoApFound = 201;
constexpr uint8_t kReasonAuthFail = 202;
constexpr uint8_t kReasonHandshakeTimeout = 204;

void onTimeSync(struct timeval*) {
    gSynced.store(true);
    gLastSync.store(static_cast<uint32_t>(time(nullptr)));
}

void onDisconnect(WiFiEvent_t, WiFiEventInfo_t info) { gDisconnectReason.store(info.wifi_sta_disconnected.reason); }
#endif

void copyText(char* out, size_t size, const char* in) {
    std::strncpy(out, in != nullptr ? in : "", size - 1);
    out[size - 1] = '\0';
}

}  // namespace

void NetClock::begin() {
    esp_read_mac(mac_, ESP_MAC_WIFI_STA);
    snprintf(host_, sizeof(host_), "heatbox-%02x%02x%02x", mac_[3], mac_[4], mac_[5]);
    portENTER_CRITICAL(&lock_);
    copyText(info_.host, sizeof(info_.host), host_);
    portEXIT_CRITICAL(&lock_);
#if ENABLE_WIFI
    Preferences prefs;
    if (prefs.begin(kNvs, true)) {
        prefs.getString("ssid", current_.ssid, sizeof(current_.ssid));
        prefs.getString("pass", current_.pass, sizeof(current_.pass));
        prefs.end();
    }
    if (current_.ssid[0] == '\0') {
        copyText(current_.ssid, sizeof(current_.ssid), WIFI_SSID);
        copyText(current_.pass, sizeof(current_.pass), WIFI_PASS);
    }
    // persistent(false): jangan tulis kredensial ke flash IDF tiap boot
    WiFi.persistent(false);
    WiFi.setHostname(host_);
    WiFi.mode(WIFI_STA);  // selalu STA: pindai tetap bisa walau belum ada SSID
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (current_.ssid[0] == '\0') {
        state_.store(hbui::WifiState::NoCredentials);
    } else {
        connect(current_);
    }
    publishInfo(false);
#endif
}

void NetClock::connect(const Credentials& c) {
#if ENABLE_WIFI
    gDisconnectReason.store(0);
    WiFi.begin(c.ssid, c.pass);
    state_.store(hbui::WifiState::Connecting);
#else
    (void)c;
#endif
}

void NetClock::update(uint32_t nowMs) {
#if ENABLE_WIFI
    if (connectRequested_.exchange(false)) {
        startTrial(nowMs);
    }
    if (reconnectRequested_.exchange(false) && !trying_ && current_.ssid[0] != '\0') {
        WiFi.disconnect();
        connect(current_);
    }
    if (scanRequested_.exchange(false) && !scanning_) {
        startScan();
    }
    if (scanning_) {
        pollScan();
    }
    if ((nowMs - lastPollMs_) < kPollMs) {
        return;
    }
    lastPollMs_ = nowMs;
    if (trying_) {
        pollTrial(nowMs);
    }

    const bool up = WiFi.status() == WL_CONNECTED;
    if (!trying_ && current_.ssid[0] == '\0') {
        state_.store(hbui::WifiState::NoCredentials);
    } else {
        state_.store(up ? hbui::WifiState::Connected : hbui::WifiState::Connecting);
    }
    rssi_.store(up ? static_cast<int8_t>(WiFi.RSSI()) : 0);
    if (up && !sntpStarted_) {
        // SNTP baru dimulai setelah ada jaringan; sinkron ulang tiap jam (bawaan)
        sntp_set_time_sync_notification_cb(onTimeSync);
        configTzTime(kTimeZone, kNtp1, kNtp2);
        sntpStarted_ = true;
    }
    if (syncRequested_.exchange(false) && up) {
        configTzTime(kTimeZone, kNtp1, kNtp2);  // mulai ulang SNTP = sinkron sekarang
    }
    publishInfo(up);
#else
    (void)nowMs;
#endif
}

void NetClock::publishInfo(bool up) {
#if ENABLE_WIFI
    char ip[16] = "";
    if (up) {
        const IPAddress a = WiFi.localIP();
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    }
    const Credentials& shown = trying_ ? trial_ : current_;
    portENTER_CRITICAL(&lock_);
    copyText(info_.ssid, sizeof(info_.ssid), shown.ssid);
    copyText(info_.ip, sizeof(info_.ip), ip);
    info_.trying = trying_;
    info_.scanning = scanning_;
    portEXIT_CRITICAL(&lock_);
#else
    (void)up;
#endif
}

// ---- percobaan SSID/sandi baru ----
void NetClock::requestConnect(const char* ssid, const char* pass) {
    portENTER_CRITICAL(&lock_);
    copyText(requested_.ssid, sizeof(requested_.ssid), ssid);
    copyText(requested_.pass, sizeof(requested_.pass), pass);
    info_.lastFail = hbui::NetFail::None;
    info_.trying = true;  // langsung terlihat di layar sebelum task sensor memproses
    portEXIT_CRITICAL(&lock_);
    connectRequested_.store(true);
}

void NetClock::startTrial(uint32_t nowMs) {
#if ENABLE_WIFI
    portENTER_CRITICAL(&lock_);
    trial_ = requested_;
    portEXIT_CRITICAL(&lock_);
    WiFi.disconnect();
    trying_ = true;
    trialStartMs_ = nowMs;
    connect(trial_);
    Serial.printf("# WiFi: mencoba SSID \"%s\"\n", trial_.ssid);
#else
    (void)nowMs;
#endif
}

void NetClock::pollTrial(uint32_t nowMs) {
#if ENABLE_WIFI
    hbui::NetFail fail = hbui::NetFail::None;
    if (WiFi.status() == WL_CONNECTED) {
        current_ = trial_;
        Preferences prefs;
        if (prefs.begin(kNvs, false)) {
            prefs.putString("ssid", current_.ssid);
            prefs.putString("pass", current_.pass);
            prefs.end();
        }
        trying_ = false;
        Serial.printf("# WiFi: tersambung ke \"%s\", disimpan ke NVS\n", current_.ssid);
        return;
    }
    const uint8_t reason = gDisconnectReason.load();
    if (reason == kReasonAuthFail || reason == kReason4WayTimeout || reason == kReasonHandshakeTimeout ||
        reason == kReasonAuthExpire) {
        fail = hbui::NetFail::WrongPassword;
    } else if (reason == kReasonNoApFound && (nowMs - trialStartMs_) > 8000) {
        fail = hbui::NetFail::NotFound;  // beri waktu beberapa kali pindai kanal
    } else if ((nowMs - trialStartMs_) > kTrialTimeoutMs) {
        fail = hbui::NetFail::Timeout;
    }
    if (fail == hbui::NetFail::None) {
        return;
    }
    trying_ = false;
    portENTER_CRITICAL(&lock_);
    info_.lastFail = fail;
    portEXIT_CRITICAL(&lock_);
    Serial.printf("# WiFi: \"%s\" GAGAL (alasan %u), kembali ke WiFi lama\n", trial_.ssid,
                  static_cast<unsigned>(reason));
    WiFi.disconnect();
    if (current_.ssid[0] != '\0') {
        connect(current_);
    }
#else
    (void)nowMs;
#endif
}

// ---- pindai ----
void NetClock::startScan() {
#if ENABLE_WIFI
    // ESP-IDF menolak pindai saat STA sedang mencoba tersambung: hentikan
    // dulu, lalu sambung lagi ke WiFi aktif setelah pindai selesai
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        trying_ = false;
        resumeAfterScan_ = current_.ssid[0] != '\0';
    }
    WiFi.scanDelete();
    scanning_ = WiFi.scanNetworks(true /*async*/, false /*tanpa SSID tersembunyi*/) == WIFI_SCAN_RUNNING;
    if (!scanning_) {
        portENTER_CRITICAL(&lock_);
        networkCount_ = 0;
        ++info_.scanSeq;
        portEXIT_CRITICAL(&lock_);
    }
    portENTER_CRITICAL(&lock_);
    info_.scanning = scanning_;
    portEXIT_CRITICAL(&lock_);
#endif
}

void NetClock::pollScan() {
#if ENABLE_WIFI
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        return;
    }
    hbui::WifiNetwork found[kMaxNetworks] = {};
    uint8_t count = 0;
    for (int16_t i = 0; i < n; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(i));
        // satu baris per SSID (yang terkuat), urut RSSI menurun
        int8_t existing = -1;
        for (uint8_t k = 0; k < count; ++k) {
            if (strcmp(found[k].ssid, ssid.c_str()) == 0) existing = static_cast<int8_t>(k);
        }
        if (existing >= 0) {
            if (rssi <= found[existing].rssi) continue;
            for (uint8_t k = static_cast<uint8_t>(existing); k + 1 < count; ++k) found[k] = found[k + 1];
            --count;
        }
        uint8_t pos = count;
        while (pos > 0 && found[pos - 1].rssi < rssi) --pos;
        if (pos >= kMaxNetworks) continue;
        const uint8_t last = count < kMaxNetworks ? count : kMaxNetworks - 1;
        for (uint8_t k = last; k > pos; --k) found[k] = found[k - 1];
        copyText(found[pos].ssid, sizeof(found[pos].ssid), ssid.c_str());
        found[pos].rssi = rssi;
        found[pos].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        if (count < kMaxNetworks) ++count;
    }
    WiFi.scanDelete();
    scanning_ = false;
    portENTER_CRITICAL(&lock_);
    std::memcpy(networks_, found, sizeof(found));
    networkCount_ = count;
    ++info_.scanSeq;
    info_.scanning = false;
    portEXIT_CRITICAL(&lock_);
    if (resumeAfterScan_) {
        resumeAfterScan_ = false;
        connect(current_);
    }
#endif
}

uint8_t NetClock::scanResults(hbui::WifiNetwork* out, uint8_t max) {
    portENTER_CRITICAL(&lock_);
    const uint8_t n = networkCount_ < max ? networkCount_ : max;
    std::memcpy(out, networks_, n * sizeof(hbui::WifiNetwork));
    portEXIT_CRITICAL(&lock_);
    return n;
}

void NetClock::info(hbui::NetInfo& out) {
    portENTER_CRITICAL(&lock_);
    out = info_;
    portEXIT_CRITICAL(&lock_);
    out.lastSyncEpoch = gLastSync.load();
    out.nowEpoch = gSynced.load() ? static_cast<uint32_t>(time(nullptr)) : 0;
}

void NetClock::deviceIds(char (&mac)[18], char (&id)[12]) const {
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
    snprintf(id, sizeof(id), "HBX-%02X%02X%02X", mac_[3], mac_[4], mac_[5]);
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
