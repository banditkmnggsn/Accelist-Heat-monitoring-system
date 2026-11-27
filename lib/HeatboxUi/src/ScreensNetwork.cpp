// PENGATURAN > WIFI & JAM (03-40.9a/b), pindai SSID, ketik SSID/sandi

#include <app_config.h>

#include <cstdio>
#include <cstring>

#include "Screens.h"
#include "Widgets.h"

namespace hbui {

using namespace layout;

namespace {

enum WifiAction : uint8_t { ChangeWifi, Reconnect, SyncClock, ActionCount };
constexpr int16_t kActionY = 122;
constexpr int16_t kActionH = 24;

const char* failText(NetFail fail) {
    switch (fail) {
        case NetFail::WrongPassword: return "GAGAL: SANDI SALAH. WIFI LAMA DIPAKAI";
        case NetFail::NotFound: return "GAGAL: WIFI TIDAK DITEMUKAN";
        case NetFail::Timeout: return "GAGAL: TIDAK TERSAMBUNG. WIFI LAMA";
        case NetFail::None: break;
    }
    return nullptr;
}

uint32_t hashText(uint32_t k, const char* s) {
    for (; *s != '\0'; ++s) k = k * 33u + static_cast<uint8_t>(*s);
    return k;
}

// Bar sinyal 4 batang (kiri bawah x,y+8), dari RSSI
void drawSignal(Painter& p, int16_t x, int16_t y, int8_t rssi, uint8_t ink) {
    const uint8_t level = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
    for (uint8_t i = 0; i < 4; ++i) {
        const int16_t h = static_cast<int16_t>(2 + i * 2);
        const int16_t bx = static_cast<int16_t>(x + i * 4);
        if (i < level) {
            p.fill(bx, static_cast<int16_t>(y + 8 - h), 3, h, ink);
        } else {
            p.fill(bx, static_cast<int16_t>(y + 7), 3, 1, color::kGrid);
        }
    }
}

// ---- kelompok karakter untuk ketik ----
const char kLower[] = "abcdefghijklmnopqrstuvwxyz";
const char kUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char kDigit[] = "0123456789";
const char kSymbol[] = " .-_@!#$%&*+=?/:;,'\"()[]{}<>^~`|\\";
const char* const kGroups[4] = {kLower, kUpper, kDigit, kSymbol};
const char* const kGroupNames[4] = {"abc", "ABC", "123", "#+!"};
constexpr uint8_t kActions = 3;  // HAPUS, SELESAI, BATAL
const char* const kActionShort[kActions] = {"DEL", "OK", "X"};

}  // namespace

// =====================================================================
//  WIFI & JAM
// =====================================================================
void WifiScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int next = static_cast<int>(cursor_) + steps;
    cursor_ = static_cast<uint8_t>(next < 0 ? 0 : (next >= ActionCount ? ActionCount - 1 : next));
}

void WifiScreen::onPress(UiContext& ctx) {
    const UiSnapshot& s = ctx.snap;
    if (s.wifi == WifiState::Disabled || s.net.trying) {
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    switch (cursor_) {
        case ChangeWifi:
            ctx.nav.wifiDraft().freshScan = true;
            ctx.nav.push(ScreenId::WifiScan);
            break;
        case Reconnect:
            if (s.wifi == WifiState::NoCredentials) {
                ctx.cmd.playSound(Sound::Error, false);
                return;
            }
            ctx.cmd.wifiReconnect();
            ctx.cmd.playSound(Sound::Confirm, false);
            break;
        default:
            if (s.wifi != WifiState::Connected) {
                ctx.cmd.playSound(Sound::Error, false);  // NTP butuh internet
                return;
            }
            ctx.cmd.clockSync();
            ctx.cmd.playSound(Sound::Confirm, false);
            break;
    }
}

uint32_t WifiScreen::liveKey(const UiContext& ctx) const {
    const NetInfo& n = ctx.snap.net;
    uint32_t k = static_cast<uint32_t>(ctx.snap.wifi) | (n.trying ? 1u << 4 : 0u) |
                 (static_cast<uint32_t>(n.lastFail) << 5) | (static_cast<uint32_t>(cursor_) << 8);
    k = k * 31u + static_cast<uint32_t>(ctx.snap.rssi);
    k = k * 31u + n.nowEpoch / 60u + n.lastSyncEpoch;
    return hashText(hashText(k, n.ssid), n.ip);
}

void WifiScreen::draw(Painter& p, const UiContext& ctx) const {
    const UiSnapshot& s = ctx.snap;
    const NetInfo& n = s.net;
    drawHeader(p, s, "WIFI & JAM");

    // Status sambungan 03-40.9a/b
    if (s.wifi == WifiState::Disabled) {
        drawBadge(p, kMargin, 46, BadgeKind::Error, "WIFI MATI (BUILD)");
    } else if (n.trying) {
        drawBadge(p, kMargin, 46, BadgeKind::Warn, "MENCOBA WIFI BARU...");
    } else if (s.wifi == WifiState::Connected) {
        drawBadge(p, kMargin, 46, BadgeKind::Ok, "TERHUBUNG");
    } else if (s.wifi == WifiState::NoCredentials) {
        drawBadge(p, kMargin, 46, BadgeKind::Error, "BELUM ADA WIFI");
    } else {
        drawBadge(p, kMargin, 46, BadgeKind::Warn, "MENYAMBUNG...");
    }
    p.textExact(kMargin, 64, n.ssid[0] != '\0' ? n.ssid : "-", kT2, color::kText);

    char line[44];
    if (s.wifi == WifiState::Connected) {
        std::snprintf(line, sizeof(line), "SINYAL %d DBM  IP %s", static_cast<int>(s.rssi), n.ip);
    } else {
        std::snprintf(line, sizeof(line), "SINYAL -  IP -");
    }
    p.text(kMargin, 84, line, kT1, color::kText);

    char date[10];
    char clock[6];
    if (n.nowEpoch != 0) {
        formatDate(n.nowEpoch, date, clock);
        std::snprintf(line, sizeof(line), "JAM %s %s", date, clock);
    } else {
        std::snprintf(line, sizeof(line), "JAM BELUM SINKRON (*)");
    }
    p.text(kMargin, 96, line, kT1, color::kText);
    if (n.lastSyncEpoch != 0) {
        formatDate(n.lastSyncEpoch, date, clock);
        std::snprintf(line, sizeof(line), "SINKRON TERAKHIR %s", clock);
    } else {
        std::snprintf(line, sizeof(line), "SINKRON TERAKHIR -");
    }
    p.text(kMargin, 108, line, kT1, color::kSubText);

    static const char* const kLabels[ActionCount] = {"GANTI WIFI", "HUBUNGKAN ULANG", "SINKRON JAM"};
    const bool locked = s.wifi == WifiState::Disabled || n.trying;
    for (uint8_t i = 0; i < ActionCount; ++i) {
        const int16_t y = static_cast<int16_t>(kActionY + i * kActionH);
        drawMenuRow(p, y, kActionH - 2, kLabels[i], kT2, i == cursor_, Icon::Count, locked);
        if (i != cursor_) p.hline(kMargin, static_cast<int16_t>(y + kActionH - 2), kContentW, 1, color::kGrid);
    }

    const char* fail = failText(n.lastFail);
    if (n.trying) {
        drawBanner(p, BannerKind::Warn, "MENUNGGU JAWABAN ACCESS POINT...");
    } else if (fail != nullptr) {
        drawBanner(p, BannerKind::Error, fail);
    } else if (!s.clockSynced) {
        drawBanner(p, BannerKind::Warn, "JAM MEMAKAI NILAI BAWAAN (*)");
    }
}

// =====================================================================
//  PINDAI SSID
// =====================================================================
void WifiScanScreen::startScan(UiContext& ctx) {
    seq_ = ctx.snap.net.scanSeq;
    requestMs_ = ctx.snap.nowMs;
    waiting_ = true;
    ctx.cmd.wifiScan();
}

void WifiScanScreen::onEnter(UiContext& ctx) {
    // Dibuka dari WIFI & JAM: pindai baru. Kembali dari layar ketik: daftar tetap.
    WifiDraft& d = ctx.nav.wifiDraft();
    if (d.freshScan) {
        d.freshScan = false;
        count_ = 0;
        list_ = ListCursor{};
        startScan(ctx);
    }
}

void WifiScanScreen::tick(UiContext& ctx) {
    if (!waiting_) return;
    // Selesai saat penghitung pindai naik (atau batas 15 dtk: daftar lama/kosong)
    if (ctx.snap.net.scanSeq != seq_ || (ctx.snap.nowMs - requestMs_) > 15000) {
        waiting_ = false;
        count_ = ctx.cmd.wifiResults(nets_, kMax);
        list_ = ListCursor{};
    }
}

void WifiScanScreen::onRotate(UiContext&, int8_t steps, bool) {
    if (!waiting_) list_.rotate(steps, rowCount(), kRows);
}

void WifiScanScreen::onPress(UiContext& ctx) {
    if (waiting_) return;
    WifiDraft& d = ctx.nav.wifiDraft();
    if (list_.cursor < count_) {
        const WifiNetwork& n = nets_[list_.cursor];
        std::snprintf(d.ssid, sizeof(d.ssid), "%s", n.ssid);
        d.pass[0] = '\0';
        if (!n.secure) {
            ctx.cmd.wifiConnect(d.ssid, "");  // WiFi terbuka: langsung coba
            ctx.cmd.playSound(Sound::Confirm, false);
            ctx.nav.popTo(ScreenId::Wifi);
            return;
        }
        d.forPassword = true;
        ctx.nav.push(ScreenId::TextEntry);
    } else if (list_.cursor == count_) {
        d.ssid[0] = '\0';
        d.pass[0] = '\0';
        d.forPassword = false;  // SSID tersembunyi: ketik nama dulu
        ctx.nav.push(ScreenId::TextEntry);
    } else {
        startScan(ctx);
    }
}

const char* WifiScanScreen::hint(const UiContext&) const {
    return waiting_ ? "MEMINDAI...  TAHAN:KEMBALI" : "PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI";
}

uint32_t WifiScanScreen::liveKey(const UiContext& ctx) const {
    return (waiting_ ? 1u : 0u) | (static_cast<uint32_t>(count_) << 1) | (static_cast<uint32_t>(list_.cursor) << 8) |
           ((ctx.snap.nowMs / 500u) % 2u) << 16;
}

void WifiScanScreen::draw(Painter& p, const UiContext& ctx) const {
    drawHeader(p, ctx.snap, "PILIH WIFI");
    if (waiting_) {
        p.icon(Icon::Wifi, 120, 96, color::kText, true);
        p.textCenter(128, 114, (ctx.snap.nowMs / 500u) % 2u ? "MEMINDAI..." : "MEMINDAI.. ", kT2, color::kText);
        p.textCenter(128, 138, "MENCARI JARINGAN DI SEKITAR", kT1, color::kSubText);
        return;
    }
    constexpr int16_t kH = 25;
    for (uint8_t row = 0; row < kRows && list_.top + row < rowCount(); ++row) {
        const uint8_t i = static_cast<uint8_t>(list_.top + row);
        const int16_t y = static_cast<int16_t>(kContentY + row * kH);
        const bool selected = i == list_.cursor;
        if (i >= count_) {
            drawMenuRow(p, y, kH - 2, i == count_ ? "KETIK NAMA WIFI" : "PINDAI ULANG", kT2, selected, Icon::Count,
                        false);
        } else {
            const WifiNetwork& n = nets_[i];
            if (selected) {
                p.frame(kMargin, y, kContentW, kH - 2, 2, color::kText);
                p.text(kMargin + 4, static_cast<int16_t>(y + 4), ">", kT2, color::kText);
            }
            p.textExact(kMargin + 18, static_cast<int16_t>(y + 4), n.ssid, kT2, color::kText, 180);
            if (n.secure) p.icon(Icon::Lock, 214, static_cast<int16_t>(y + 7), color::kText, false);
            drawSignal(p, 228, static_cast<int16_t>(y + 7), n.rssi, color::kText);
        }
        if (!selected) p.hline(kMargin, static_cast<int16_t>(y + kH - 2), kContentW, 1, color::kGrid);
    }
    drawScrollRail(p, kContentY, static_cast<int16_t>(kRows * kH - 2), list_.top, kRows, rowCount());
    if (count_ == 0) {
        drawBanner(p, BannerKind::Warn, "TIDAK ADA WIFI TERLIHAT");
    }
}

// =====================================================================
//  KETIK SSID / SANDI per karakter
// =====================================================================
uint8_t TextEntryScreen::groupSize(uint8_t group) {
    return static_cast<uint8_t>(std::strlen(kGroups[group % GroupCount]) + kActions);
}

uint8_t TextEntryScreen::action() const {
    const uint8_t chars = static_cast<uint8_t>(groupSize(group_) - kActions);
    return index_ < chars ? 0 : static_cast<uint8_t>(index_ - chars + 1);
}

char TextEntryScreen::current() const { return action() == 0 ? kGroups[group_][index_] : '\0'; }

void TextEntryScreen::onEnter(UiContext& ctx) {
    const WifiDraft& d = ctx.nav.wifiDraft();
    maxLength_ = d.forPassword ? 63 : 32;
    text_[0] = '\0';
    length_ = 0;
    group_ = Lower;
    index_ = 0;
    rejected_[0] = '\0';
}

void TextEntryScreen::onRotate(UiContext&, int8_t steps, bool) {
    const int n = groupSize(group_);
    int next = (static_cast<int>(index_) + steps) % n;
    if (next < 0) next += n;
    index_ = static_cast<uint8_t>(next);
    rejected_[0] = '\0';
}

void TextEntryScreen::onPress(UiContext&) {
    group_ = static_cast<uint8_t>((group_ + 1) % GroupCount);
    index_ = 0;
    rejected_[0] = '\0';
}

void TextEntryScreen::onHold(UiContext& ctx) {
    rejected_[0] = '\0';
    switch (action()) {
        case 0:
            if (length_ >= maxLength_) {
                std::snprintf(rejected_, sizeof(rejected_), "MAKSIMUM %u KARAKTER", static_cast<unsigned>(maxLength_));
                ctx.cmd.playSound(Sound::Error, false);
                return;
            }
            text_[length_++] = current();
            text_[length_] = '\0';
            break;
        case 1:
            if (length_ > 0) text_[--length_] = '\0';
            break;
        case 2:
            finish(ctx);
            break;
        default:
            ctx.nav.pop();
            break;
    }
}

void TextEntryScreen::finish(UiContext& ctx) {
    WifiDraft& d = ctx.nav.wifiDraft();
    if (!d.forPassword) {
        if (length_ == 0) {
            std::snprintf(rejected_, sizeof(rejected_), "NAMA WIFI MASIH KOSONG");
            ctx.cmd.playSound(Sound::Error, false);
            return;
        }
        std::snprintf(d.ssid, sizeof(d.ssid), "%.32s", text_);
        d.forPassword = true;
        ctx.nav.replace(ScreenId::TextEntry);  // lanjut ketik sandi (kosong = WiFi terbuka)
        return;
    }
    if (length_ > 0 && length_ < 8) {
        std::snprintf(rejected_, sizeof(rejected_), "SANDI WPA MINIMAL 8 KARAKTER");
        ctx.cmd.playSound(Sound::Error, false);
        return;
    }
    std::snprintf(d.pass, sizeof(d.pass), "%.63s", text_);
    ctx.cmd.wifiConnect(d.ssid, d.pass);
    ctx.cmd.playSound(Sound::Confirm, false);
    ctx.nav.popTo(ScreenId::Wifi);
}

HoldSpec TextEntryScreen::holdSpec(const UiContext&) const {
    static const char* const kLabels[4] = {"KUNCI", "HAPUS", "SELESAI", "BATAL"};
    return {app::input::kHoldCharMs, kLabels[action()]};
}

const char* TextEntryScreen::hint(const UiContext&) const {
    return action() == 0 ? "PUTAR:HURUF TEKAN:JENIS TAHAN:KUNCI" : "PUTAR:PILIH  TAHAN:JALANKAN";
}

void TextEntryScreen::draw(Painter& p, const UiContext& ctx) const {
    const WifiDraft& d = ctx.nav.wifiDraft();
    drawHeader(p, ctx.snap, d.forPassword ? "SANDI WIFI" : "NAMA WIFI");
    if (d.forPassword) {
        p.text(kMargin, 46, "WIFI:", kT1, color::kSubText);
        p.textExact(38, 46, d.ssid, kT1, color::kText);
    } else {
        p.text(kMargin, 46, "KETIK NAMA (SSID) PERSIS SAMA", kT1, color::kSubText);
    }
    char count[12];
    std::snprintf(count, sizeof(count), "%u/%u", static_cast<unsigned>(length_), static_cast<unsigned>(maxLength_));
    p.textRight(kRight, 46, count, kT1, color::kText);

    // Kolom teks: 17 karakter terakhir + kolom aktif berkedip
    p.frame(kMargin, 58, kContentW, 26, 2, color::kText);
    constexpr uint8_t kVisible = 17;
    const char* tail = text_ + (length_ > kVisible ? length_ - kVisible : 0);
    const int16_t end = p.textExact(12, 63, tail, kT2, color::kText);
    const bool blinkOn = (ctx.snap.nowMs / 400u) % 2u == 0;
    const char c = current();
    if (c != '\0') {
        const char one[2] = {c == ' ' ? '_' : c, '\0'};
        if (blinkOn) {
            p.fill(static_cast<int16_t>(end - 1), 61, 13, 20, color::kAccent);
            p.textExact(end, 63, one, kT2, color::kBg);
        } else {
            p.textExact(end, 63, one, kT2, color::kText);
        }
    } else if (blinkOn) {
        p.fill(end, 79, 10, 2, color::kText);  // aksi dipilih: kursor garis
    }

    // Jenis karakter (TEKAN berganti)
    for (uint8_t g = 0; g < GroupCount; ++g) {
        const int16_t x = static_cast<int16_t>(kMargin + g * 61);
        if (g == group_) {
            p.fill(x, 90, 58, 16, color::kText);
            p.textExact(static_cast<int16_t>(x + 11), 94, kGroupNames[g], kT1, color::kBg);
            p.text(static_cast<int16_t>(x + 31), 94, "<", kT1, color::kBg);
        } else {
            p.frame(x, 90, 58, 16, 1, color::kGrid);
            p.textExact(static_cast<int16_t>(x + 11), 94, kGroupNames[g], kT1, color::kText);
        }
    }

    // Pilihan putar: 3 kiri, aktif, 3 kanan
    const int n = groupSize(group_);
    const uint8_t chars = static_cast<uint8_t>(n - kActions);
    for (int off = -3; off <= 3; ++off) {
        int i = (static_cast<int>(index_) + off) % n;
        if (i < 0) i += n;
        char label[4];
        if (i < chars) {
            label[0] = kGroups[group_][i] == ' ' ? '_' : kGroups[group_][i];
            label[1] = '\0';
        } else {
            std::snprintf(label, sizeof(label), "%s", kActionShort[i - chars]);
        }
        const int16_t cx = static_cast<int16_t>(128 + off * 34);
        if (off == 0) {
            p.frame(static_cast<int16_t>(cx - 17), 112, 34, 34, 3, color::kAccent);
            if (i < chars) {
                const int16_t w = Painter::textWidth(label, kT3);
                p.textExact(static_cast<int16_t>(cx - w / 2), 118, label, kT3, color::kText);
            } else {
                p.textCenter(cx, 125, label, std::strlen(label) > 2 ? kT1 : kT2, color::kText);
            }
        } else if (i < chars) {
            const int16_t w = Painter::textWidth(label, kT2);
            p.textExact(static_cast<int16_t>(cx - w / 2), 121, label, kT2, color::kSubText);
        } else {
            p.textCenter(cx, 125, label, kT1, color::kSubText);  // DEL / OK / X
        }
    }

    char what[40];
    if (c != '\0') {
        if (c == ' ') {
            std::snprintf(what, sizeof(what), "TAHAN: KUNCI SPASI");
        } else {
            std::snprintf(what, sizeof(what), "TAHAN: KUNCI '%c'", c);
        }
        const int16_t w = Painter::textWidth(what, kT1);
        p.textExact(static_cast<int16_t>(128 - w / 2), 156, what, kT1, color::kText);
    } else {
        static const char* const kWhat[kActions] = {"TAHAN: HAPUS KARAKTER TERAKHIR", "TAHAN: SELESAI & SAMBUNGKAN",
                                                    "TAHAN: BATAL, KEMBALI"};
        const char* text = kWhat[action() - 1];
        if (action() == 2 && !d.forPassword) text = "TAHAN: SELESAI, LANJUT KE SANDI";
        p.textCenter(128, 156, text, kT1, color::kText);
    }
    p.text(kMargin, 172, "KIRI DARI HURUF PERTAMA: DEL / OK / X", kT1, color::kSubText);
    p.text(kMargin, 184, d.forPassword ? "SANDI KOSONG = WIFI TANPA SANDI" : "BEDA HURUF BESAR/KECIL", kT1,
           color::kSubText);
    if (rejected_[0] != '\0') {
        drawBanner(p, BannerKind::Warn, rejected_);
    }
}

}  // namespace hbui
