// =====================================================================
//  ui_render — pratinjau PNG semua layar lib/HeatboxUi di PC
//
//  Kode layar yang SAMA dengan firmware digambar ke GFXcanvas8 (RGB332),
//  lalu disimpan sebagai PNG ukuran asli 256x240 dan 3x (768x720, sama
//  dengan kanvas desain). Dipakai untuk mengecek tata letak, batas
//  karakter, dan zona PARKING tanpa LCD. Cara pakai: tools/ui_render/README.md
// =====================================================================

#include <Adafruit_GFX.h>
#include <Ui.h>
#include <Widgets.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace hbui;

namespace {

// ---- PNG tanpa kompresi (deflate "stored"), cukup untuk pratinjau ----
uint32_t crcTable[256];
void initCrc() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
uint32_t crc(const std::vector<uint8_t>& d, size_t from) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = from; i < d.size(); ++i) c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 3; i >= 0; --i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc(out, start));
}
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + y * w * 3, rgb.begin() + (y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t pos = 0; pos < raw.size(); pos += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + len >= raw.size() ? 1 : 0);
        z.push_back(len & 0xFF); z.push_back(len >> 8);
        z.push_back(~len & 0xFF); z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

// ---- perintah palsu: mencatat saja ----
struct FakeCommands : UiCommands {
    void playSound(Sound, bool) override {}
    bool saveSettings(const hbcycle::Settings&) override { return true; }
    void recheckSensors() override {}
};

UiSnapshot baseSnapshot() {
    UiSnapshot s{};
    s.nowMs = 1000;
    s.scaleCheck = CheckState::Ok;
    s.tempCheck = CheckState::Ok;
    s.scaleOk = true;
    s.tempOk = true;
    s.tempC = 27.4f;
    s.doorClosed = true;
    s.wifi = WifiState::Connected;
    s.rssi = -61;
    s.clockSynced = true;
    s.hour = 14;
    s.minute = 5;
    s.input.stepsPerDetent = 4;
    s.input.doorPinHigh = false;
    return s;
}

struct Harness {
    FakeCommands cmd;
    Ui ui{cmd};
    UiSnapshot snap = baseSnapshot();
    GFXcanvas8 canvas{256, 240};
    std::string outDir;

    void boot(hbcycle::Settings settings = hbcycle::defaultSettings()) { ui.begin(settings, snap); }
    void advance(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 50) {
            snap.nowMs += 50;
            ui.update(snap);
        }
    }
    void home() {
        boot();
        advance(4000);
    }
    void send(hbinput::InputType type, int8_t steps = 0) {
        snap.nowMs += 200;
        ui.handle({type, steps, snap.nowMs}, snap);
        ui.update(snap);
    }
    void rotate(int n) {
        for (int i = 0; i < (n < 0 ? -n : n); ++i) send(hbinput::InputType::Rotate, n < 0 ? -1 : 1);
    }
    void press() { send(hbinput::InputType::Press); }
    void hold() { send(hbinput::InputType::Hold); }

    void shot(const char* name) {
        ui.update(snap);
        Painter p(canvas, ui.style());
        ui.render(p, snap);
        const uint8_t* buf = canvas.getBuffer();
        for (int scale : {1, 3}) {
            const int w = 256 * scale, h = 240 * scale;
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const uint8_t c = buf[(y / scale) * 256 + (x / scale)];
                    uint8_t* px = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                    px[0] = static_cast<uint8_t>(((c >> 5) & 7) * 255 / 7);
                    px[1] = static_cast<uint8_t>(((c >> 2) & 7) * 255 / 7);
                    px[2] = static_cast<uint8_t>((c & 3) * 255 / 3);
                }
            }
            writePng(outDir + "/" + name + (scale == 1 ? "_1x.png" : "_3x.png"), w, h, rgb);
        }
        // Aturan mutlak: zona PARKING putih
        for (int y = layout::kParkY; y < layout::kParkY + layout::kParkH; ++y) {
            for (int x = layout::kParkX; x < 256; ++x) {
                if (buf[y * 256 + x] != color::kBg) {
                    std::printf("PELANGGARAN PARKING di %s (%d,%d)\n", name, x, y);
                    return;
                }
            }
        }
        std::printf("ok %s\n", name);
    }
};

}  // namespace

int main(int argc, char** argv) {
    initCrc();
    const std::string out = argc > 1 ? argv[1] : ".";
    auto make = [&]() {
        Harness* h = new Harness();
        h->outDir = out;
        return h;
    };

    {   // Boot sedang berjalan
        Harness* h = make();
        h->snap.tempCheck = CheckState::Pending;
        h->snap.wifi = WifiState::Connecting;
        h->snap.clockSynced = false;
        h->boot();
        h->advance(1200);
        h->shot("01_boot_memeriksa");
        h->snap.tempCheck = CheckState::Ok;
        h->snap.wifi = WifiState::Connected;
        h->snap.clockSynced = true;
        h->advance(1500);
        h->shot("02_boot_semua_ok");
    }
    {   // Boot offline (WiFi + jam gagal)
        Harness* h = make();
        h->snap.wifi = WifiState::Connecting;
        h->snap.clockSynced = false;
        h->boot();
        h->advance(8600);
        h->shot("03_boot_offline");
        h->advance(3000);
        h->shot("04_beranda_jam_belum_sinkron");
    }
    {   // Boot timbangan gagal -> Error SENSOR
        Harness* h = make();
        h->snap.scaleCheck = CheckState::Fail;
        h->snap.scaleOk = false;
        h->boot();
        h->advance(2000);
        h->shot("05_boot_timbangan_gagal");
        h->advance(2500);
        h->shot("06_error_sensor");
        h->hold();
        h->shot("07_beranda_sensor_gagal");
    }
    {   // Beranda dan variannya
        Harness* h = make();
        h->home();
        h->shot("08_beranda");
        h->snap.doorClosed = false;
        h->shot("09_beranda_pintu_terbuka");
        h->snap.doorClosed = true;
        h->snap.tempC = 64.2f;
        h->shot("10_beranda_masih_panas");
        h->snap.tempC = 27.4f;
        h->snap.holdProgressMs = 3100;
        h->shot("11_beranda_tahan_servis");
    }
    {   // Pilih profil (Beranda > MULAI TES)
        Harness* h = make();
        h->home();
        h->press();
        h->shot("12_pilih_profil");
        h->rotate(1);
        h->press();  // profil 2 jadi aktif
        h->snap.holdProgressMs = 1200;
        h->shot("13_pilih_profil_tahan_mulai");
        h->snap.holdProgressMs = 0;
        h->snap.doorClosed = false;
        h->hold();
        h->shot("14_dialog_pintu_terbuka");
        h->snap.doorClosed = true;
        h->ui.update(h->snap);  // dialog menutup sendiri, tes tidak mulai
        h->hold();
        h->shot("15_dialog_siklus_belum_aktif");
        h->press();
        h->rotate(5);
        h->shot("16_pilih_profil_kursor_kembali");
    }
    {   // Riwayat
        Harness* h = make();
        h->home();
        h->rotate(1);
        h->press();
        h->shot("17_riwayat_kosong");
    }
    {   // Pengaturan: menu, edit profil, ditolak
        Harness* h = make();
        h->home();
        h->rotate(2);
        h->press();
        h->rotate(1);
        h->shot("18_pengaturan_menu");
        h->press();      // PROFIL 2
        h->shot("19_edit_profil_pilih");
        h->press();
        h->rotate(-3);
        h->shot("20_edit_profil_mengedit");
        h->rotate(23);   // pre-heat 100
        h->press();
        h->rotate(1);    // kotak heat
        h->press();
        h->rotate(-25);  // heat 85 < pre-heat 100
        h->press();
        h->shot("21_edit_profil_ditolak");
    }
    {   // Pengaturan: edit umum ditolak
        Harness* h = make();
        h->home();
        h->rotate(2);
        h->press();
        h->rotate(3);
        h->press();      // UMUM
        h->rotate(1);
        h->press();
        h->rotate(30);   // min 25 = maks 25
        h->press();
        h->shot("22_edit_umum_ditolak");
    }
    {   // Pengaturan: reset default + terkunci
        Harness* h = make();
        h->home();
        h->rotate(2);
        h->press();
        h->rotate(5);
        h->press();
        h->shot("23_dialog_reset_default");
        h->rotate(1);
        h->shot("24_dialog_reset_pilih_reset");
        h->hold();
        h->snap.testRunning = true;
        h->shot("25_pengaturan_terkunci");
    }
    {   // Servis
        Harness* h = make();
        h->home();
        h->hold();       // TAHAN 5 dtk di Beranda
        h->shot("26_servis_menu");
        h->press();      // UJI TAMPILAN
        for (int page = 0; page < 8; ++page) {
            char name[40];
            std::snprintf(name, sizeof(name), "%02d_uji_tampilan_%d", 27 + page, page + 1);
            h->shot(name);
            h->rotate(1);
        }
        h->hold();
        h->rotate(1);
        h->press();      // UJI INPUT
        h->snap.input.detents = 12;
        h->snap.input.raw = 48;
        h->snap.input.rawPerClick = 4;
        h->snap.input.lastDir = 1;
        h->snap.input.pressed = true;
        h->snap.input.pressedMs = 840;
        h->snap.input.lastHoldMs = 230;
        h->shot("35_uji_input");
        h->hold();
        h->rotate(1);
        h->press();      // UJI SUARA
        h->rotate(2);
        h->shot("36_uji_suara");
    }
    for (int variant = 1; variant <= 2; ++variant) {  // varian pilihan dipakai di Beranda
        Harness* h = make();
        h->home();
        h->hold();
        h->press();
        h->rotate(6);  // halaman varian pilihan
        for (int i = 0; i < variant; ++i) h->press();
        h->hold();
        h->hold();
        h->shot(variant == 1 ? "37_beranda_varian_B" : "38_beranda_varian_C");
    }
    return 0;
}
