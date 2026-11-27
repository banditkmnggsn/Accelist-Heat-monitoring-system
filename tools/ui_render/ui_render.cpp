// =====================================================================
//  ui_render — pratinjau PNG semua layar lib/HeatboxUi di PC
//
//  Kode layar yang SAMA dengan firmware digambar ke GFXcanvas8 (RGB332),
//  lalu disimpan sebagai PNG ukuran asli 256x240 dan 3x (768x720, sama
//  dengan kanvas desain). Dipakai untuk mengecek tata letak, batas
//  karakter, dan zona PARKING tanpa LCD. Cara pakai: tools/ui_render/README.md
// =====================================================================

#include <Adafruit_GFX.h>
#include <CycleController.h>
#include <SimModel.h>
#include <Ui.h>
#include <Widgets.h>

#include <functional>
#include <string>

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
    bool saveAdvanced(const hbcycle::AdvancedSettings&) override { return true; }
    void recheckSensors() override {}
    void startTest(const hbcycle::Settings&) override { started = true; }
    void tareNow() override {}
    void cancelTest() override {}
    void acknowledgeResult() override {}
    void acknowledgeInterrupted() override {}
    uint8_t historyList(hbcycle::HistoryEntry* out, uint8_t max) override {
        uint8_t n = 0;
        for (; n < max && n < history.size(); ++n) out[n] = history[n];
        return n;
    }
    bool loadResult(uint32_t, hbcycle::CycleResult& out) override {
        if (stored == nullptr) return false;
        out = *stored;
        return true;
    }
    bool clearLogs() override { return true; }
    void wifiScan() override { scans++; }
    uint8_t wifiResults(WifiNetwork* out, uint8_t max) override {
        uint8_t n = 0;
        for (; n < max && n < nets.size(); ++n) out[n] = nets[n];
        return n;
    }
    void wifiConnect(const char* ssid, const char* pass) override {
        lastSsid = ssid;
        lastPass = pass;
    }
    void wifiReconnect() override {}
    void clockSync() override {}
    void scaleJob(ScaleJob job, float grams) override {
        lastJob = job;
        lastGrams = grams;
    }
    void scaleRestore(float) override {}
    bool started = false;
    int scans = 0;
    std::vector<hbcycle::HistoryEntry> history;
    std::vector<WifiNetwork> nets;
    const hbcycle::CycleResult* stored = nullptr;
    std::string lastSsid, lastPass;
    ScaleJob lastJob = ScaleJob::None;
    float lastGrams = 0.0f;
};

// Siklus sungguhan (CycleController + SimModel) untuk mengisi layar tes
struct SimRun {
    hbcycle::SimModel sim;
    hbcycle::CycleController ctl;
    uint32_t t = 0;
    bool lid = true;
    bool loadDone = false;
    uint32_t openAt = 0;
    bool autoLoad = true;

    explicit SimRun(const hbcycle::SimParams& params = hbcycle::SimParams()) : sim(params) {
        sim.reset(0);
        hbcycle::StartParams sp{};
        sp.settings = hbcycle::defaultSettings();
        sp.settings.general.preheatMin = 3;
        sp.testId = 42;
        sp.startEpoch = 1793952300;  // 6 Nov 2026 14:05 WIB
        sp.zeroRaw = params.panMg * sim.rawPerMg();
        sp.countsPerGram = params.countsPerGram;
        sp.firmware = "0.3.0";
        // beri sensor 1 dtk dulu supaya suhu awal terbaca
        for (; t < 1000; t += 10) step();
        ctl.start(sp, t);
    }
    void step() {
        sim.advance(t);
        const hbcycle::Phase phase = ctl.view().phase;
        sim.setLoadArmed(phase == hbcycle::Phase::Load);
        if (autoLoad && phase == hbcycle::Phase::Load && !loadDone) {
            if (openAt == 0) {
                openAt = t;
                lid = false;
            } else if (t - openAt >= 15000) {
                lid = true;
                loadDone = true;
            }
        }
        sim.setLidClosed(lid);
        hbcycle::MassSample s;
        const bool fresh = sim.takeSample(s);
        ctl.step(t, fresh ? &s : nullptr, sim.chamber(), true);
        sim.setHeater(ctl.ssrOn());
    }
    void until(const std::function<bool(const hbcycle::CycleView&)>& pred, uint32_t maxMs = 90u * 60u * 1000u) {
        const uint32_t end = t + maxMs;
        for (; t < end && !pred(ctl.view()); t += 10) step();
    }
    void run(uint32_t ms) {
        const uint32_t end = t + ms;
        for (; t < end; t += 10) step();
    }
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
    std::snprintf(s.net.ssid, sizeof(s.net.ssid), "%s", "PABRIK-01");
    std::snprintf(s.net.ip, sizeof(s.net.ip), "%s", "192.168.1.37");
    std::snprintf(s.net.host, sizeof(s.net.host), "%s", "heatbox-8b102e");
    s.net.nowEpoch = 1793948700u;       // 6 Nov 2026 14:05 WIB
    s.net.lastSyncEpoch = 1793948280u;  // 13:58
    s.net.ota = OtaState::Ready;
    s.scale.grams = 0.0027f;
    s.scale.raw = 8345112;
    s.scale.status = ScaleStatus::Ok;
    s.scale.stable = true;
    s.scale.calibrated = true;
    s.scale.countsPerGram = 5499.03f;
    s.rtd.spiOk = true;
    s.rtd.ohm = 110.82f;
    std::snprintf(s.device.mac, sizeof(s.device.mac), "%s", "A4:CF:12:8B:10:2E");
    std::snprintf(s.device.id, sizeof(s.device.id), "%s", "HBX-8B102E");
    s.device.totalTests = 42;
    s.device.logFreeKb = 412;
    s.device.logTotalKb = 1408;
    s.historyCapacity = 40;
    return s;
}

struct Harness {
    FakeCommands cmd;
    Ui ui{cmd};
    UiSnapshot snap = baseSnapshot();
    GFXcanvas8 canvas{256, 240};
    std::string outDir;

    void boot(hbcycle::Settings settings = hbcycle::defaultSettings()) {
        ui.begin(settings, hbcycle::defaultAdvanced(), snap);
    }
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
    void show(const hbcycle::CycleView& v) {
        snap.cycle = v;
        snap.tempC = v.tempC;
        snap.doorClosed = v.lidClosed;
        snap.startPending = false;
        const hbcycle::Phase ph = v.phase;
        snap.testRunning = ph != hbcycle::Phase::Idle && ph != hbcycle::Phase::Done && ph != hbcycle::Phase::Failed;
        ui.update(snap);
    }
    // Beranda -> Pilih Profil -> TAHAN = mulai tes
    void startTest() {
        home();
        press();
        hold();
        snap.startPending = true;
        ui.update(snap);
    }

    void shot(const char* name) {
        ui.update(snap);
        Painter p(canvas);
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
    // Beranda -> PENGATURAN (baris ke-3)
    auto settings = [&](Harness* h) {
        h->home();
        h->rotate(2);
        h->press();
    };
    {   // Pengaturan: menu, SET PROFIL, edit profil, ditolak
        Harness* h = make();
        settings(h);
        h->shot("18_pengaturan_menu");
        h->press();      // SET PROFIL
        h->rotate(1);
        h->shot("19_set_profil");
        h->press();      // PROFIL 2
        h->shot("20_edit_profil_pilih");
        h->press();
        h->rotate(-3);
        h->shot("21_edit_profil_mengedit");
        h->rotate(23);   // pre-heat 100
        h->press();
        h->rotate(1);    // kotak heat
        h->press();
        h->rotate(-25);  // heat 85 < pre-heat 100
        h->press();
        h->shot("22_edit_profil_ditolak");
    }
    {   // Pengaturan: edit umum ditolak
        Harness* h = make();
        settings(h);
        h->rotate(1);
        h->press();      // UMUM
        h->rotate(1);
        h->press();
        h->rotate(30);   // min 25 = maks 25
        h->press();
        h->shot("23_edit_umum_ditolak");
    }
    {   // Pengaturan: reset default, terkunci, bagian bawah menu
        Harness* h = make();
        settings(h);
        h->rotate(5);
        h->press();
        h->rotate(1);
        h->shot("24_dialog_reset_default");
        h->hold();
        h->rotate(1);
        h->shot("26_pengaturan_menu_bawah");
        h->snap.testRunning = true;
        h->rotate(-6);
        h->shot("25_pengaturan_terkunci");
    }
    {   // WIFI & JAM, pindai, ketik sandi
        Harness* h = make();
        settings(h);
        h->rotate(3);
        h->press();      // WIFI & JAM
        h->shot("27_wifi_jam_online");
        h->snap.wifi = WifiState::Connecting;
        h->snap.clockSynced = false;
        h->snap.net.nowEpoch = 0;
        h->snap.net.ip[0] = '\0';
        h->snap.net.lastFail = NetFail::WrongPassword;
        h->shot("28_wifi_jam_gagal");
        h->snap.wifi = WifiState::Connected;
        h->snap.clockSynced = true;
        h->snap.net.nowEpoch = 1793948700u;
        std::snprintf(h->snap.net.ip, sizeof(h->snap.net.ip), "%s", "192.168.1.37");
        h->snap.net.lastFail = NetFail::None;
        h->press();      // GANTI WIFI
        h->shot("29_wifi_memindai");
        const WifiNetwork nets[6] = {{"PABRIK-01", -48, true},   {"Gudang_Lt2", -63, true}, {"QA-Lab", -70, true},
                                     {"TAMU-ACCELIST", -74, false}, {"Printer-HP-4F", -81, true}, {"wifi_kantin", -88, true}};
        for (const WifiNetwork& n : nets) h->cmd.nets.push_back(n);
        h->snap.net.scanSeq++;
        h->ui.update(h->snap);
        h->rotate(1);
        h->shot("30_wifi_daftar");
        h->press();      // Gudang_Lt2 (bersandi)
        h->rotate(15); h->hold();   // p
        h->rotate(-15); h->hold();  // a
        h->rotate(1); h->hold();    // b
        h->rotate(16);              // r (belum dikunci)
        h->shot("31_ketik_sandi");
        h->press();
        h->press();      // 123
        h->rotate(2);
        h->shot("32_ketik_sandi_angka");
        h->rotate(-4);   // OK (putar kiri dari '0': X, OK)
        h->shot("33_ketik_pilih_selesai");
        h->hold();       // 3 karakter: ditolak (min 8)
        h->shot("34_ketik_sandi_terlalu_pendek");
        h->rotate(1);    // BATAL
        h->hold();       // kembali ke daftar
        h->rotate(5);    // KETIK NAMA WIFI
        h->press();
        h->rotate(-20); h->hold();  // g
        h->shot("35_ketik_nama_wifi");
        h->hold();
        h->hold();
        h->snap.net.trying = true;
        std::snprintf(h->snap.net.ssid, sizeof(h->snap.net.ssid), "%s", "Gudang_Lt2");
        h->ui.popTo(ScreenId::Wifi);
        h->ui.update(h->snap);
        h->shot("36_wifi_mencoba");
    }
    {   // INFO PERANGKAT
        Harness* h = make();
        settings(h);
        h->rotate(4);
        h->press();
        h->shot("37_info_perangkat");
    }
    {   // OPSI PENGEMBANG
        Harness* h = make();
        settings(h);
        h->rotate(6);
        h->press();
        h->snap.holdProgressMs = 3100;
        h->shot("38_opsi_pengembang_masuk");
        h->snap.holdProgressMs = 0;
        h->hold();
        h->shot("79_opsi_pengembang_menu");
        h->press();
        h->snap.tempC = 27.81f;
        h->shot("80_status_sensor");
        h->hold();
        h->rotate(1);
        h->press();      // TIMBANGAN
        h->shot("81_timbangan_aksi");
        h->press();
        h->rotate(1);
        h->shot("82_timbangan_konfirmasi");
        h->press();
        h->shot("83_timbangan_berjalan");
        h->snap.scale.job = ScaleJob::Tare;
        h->snap.scale.jobResult = JobResult::Done;
        h->snap.scale.jobSeq++;
        h->ui.update(h->snap);
        h->shot("84_timbangan_selesai");
        h->hold();
        h->rotate(1);
        h->press();      // KALIBRASI
        h->shot("85_kalibrasi_kosongkan");
        h->press();
        h->advance(2400);
        h->shot("86_kalibrasi_rekam_nol");
        h->snap.scale.jobSeq++;
        h->ui.update(h->snap);
        h->rotate(1);    // 20 g
        h->shot("87_kalibrasi_acuan");
        h->press();
        h->snap.scale.grams = 19.9861f;
        h->shot("88_kalibrasi_letakkan");
        h->press();
        h->snap.scale.job = ScaleJob::Calibrate;
        h->snap.scale.countsPerGram = 5512.41f;
        h->snap.scale.grams = 20.0004f;
        h->snap.scale.jobSeq++;
        h->ui.update(h->snap);
        h->shot("89_kalibrasi_hasil");
        h->press();      // SIMPAN
        h->press();      // kalibrasi lagi: rekam nol gagal
        h->press();
        h->snap.scale.jobResult = JobResult::Failed;
        h->snap.scale.jobSeq++;
        h->ui.update(h->snap);
        h->shot("90_kalibrasi_gagal");
        h->snap.scale.jobResult = JobResult::Done;
        h->hold();
        h->rotate(1);
        h->press();      // CEK TIMBANGAN
        h->snap.scale.grams = 10.0021f;
        h->shot("91_cek_ok");
        h->snap.scale.grams = 10.0123f;
        h->shot("92_cek_periksa");
        h->hold();
        h->rotate(1);
        h->press();      // PARAMETER METODE: peringatan
        h->rotate(1);
        h->shot("93_parameter_peringatan");
        h->press();
        h->shot("94_parameter_metode");
        h->rotate(2);
        h->press();
        h->rotate(40);   // target 5,0 >= ambang 4,0
        h->press();
        h->shot("95_parameter_ditolak");
        h->hold();
        h->hold();
        h->rotate(1);
        h->press();      // KONTROL PEMANAS
        h->rotate(1);
        h->press();
        h->press();      // edit Kp
        h->rotate(3);
        h->shot("96_kontrol_pemanas_edit");
        h->hold();
        h->hold();
        h->rotate(1);
        h->press();      // TAMPILAN
        h->shot("97_tampilan");
        h->hold();
        h->rotate(1);
        h->press();      // MEMORI LOG
        h->snap.historyCount = 38;
        h->shot("98_memori_log");
        h->press();
        h->rotate(1);
        h->shot("99_memori_hapus");
    }
    {   // Update firmware lewat WiFi (OTA)
        Harness* h = make();
        h->home();
        h->snap.net.ota = OtaState::Updating;
        h->snap.net.otaPct = 45;
        h->shot("100_update_firmware");
    }

    // =================================================================
    //  Prompt 2: alur tes (data dari siklus simulasi sungguhan)
    // =================================================================
    static hbcycle::CycleResult okResult{};
    {   // Normal: PRE-HEAT .. HASIL
        Harness* h = make();
        hbcycle::SimParams normal;
        normal.sampleMg = 6000.0f;  // di dalam anjuran 5-10 g
        SimRun run(normal);
        h->startTest();
        h->snap.cycle.phase = hbcycle::Phase::Idle;
        h->shot("39_tes_mulai");
        run.until([](const hbcycle::CycleView& v) { return v.preheatElapsedSec >= 70; });
        h->show(run.ctl.view());
        h->shot("40_preheat");
        hbcycle::CycleView v = run.ctl.view();
        v.preheatElapsedSec = v.preheatTotalSec;
        v.preheatWaiting = true;
        v.tempC = 77.1f;
        h->show(v);
        h->shot("41_preheat_menunggu");
        v = run.ctl.view();
        v.lidClosed = false;
        v.preheatPaused = true;
        h->show(v);
        h->shot("42_preheat_dijeda");
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Tare && v.tareSec >= 40; });
        h->show(run.ctl.view());
        h->shot("43_tare");
        v = run.ctl.view();
        v.tareSec = 500;
        v.stable = false;
        v.stableSec = 0;
        v.driftMgMin = 1.8f;
        h->show(v);
        h->shot("44_tare_belum_stabil");
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Load; });
        h->show(run.ctl.view());
        h->shot("45_muat_buka_pintu");
        run.run(8000);
        h->show(run.ctl.view());
        h->shot("46_muat_pintu_terbuka");
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Recover && v.inBandSec >= 12; });
        h->show(run.ctl.view());
        h->shot("47_pulih_w0");
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Heat; });
        h->show(run.ctl.view());
        h->shot("48_w0_terkunci");
        h->advance(3000);
        run.until([](const hbcycle::CycleView& v) { return v.heatSec >= 8 * 60 + 24; });
        h->show(run.ctl.view());
        h->shot("49_pemanasan_fase_a");
        run.until([](const hbcycle::CycleView& v) { return v.phaseB || v.phase != hbcycle::Phase::Heat; });
        run.run(30000);
        h->show(run.ctl.view());
        h->shot("50_pemanasan_fase_b");
        h->rotate(1);
        h->shot("51_pemanasan_grafik");
        h->rotate(1);
        h->shot("52_pemanasan_berat");
        h->rotate(1);
        h->shot("53_pemanasan_suhu");
        h->rotate(-3);
        h->snap.holdProgressMs = 900;
        h->shot("54_dialog_batal_30");
        h->snap.holdProgressMs = 2700;
        h->shot("55_dialog_batal_90");
        h->snap.holdProgressMs = 0;
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Stopping; });
        h->show(run.ctl.view());
        h->shot("56_stop");
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Done; });
        h->show(run.ctl.view());
        okResult = run.ctl.view().result;
        static const char* const kPages[7] = {"utama", "grafik", "berat", "waktu", "model", "suhu", "info"};
        for (int page = 0; page < 7; ++page) {
            char name[40];
            std::snprintf(name, sizeof(name), "%02d_hasil_%d_%s", 57 + page, page + 1, kPages[page]);
            h->shot(name);
            h->rotate(1);
        }
        h->rotate(-7);
        h->snap.tempC = 68.0f;
        h->snap.cycle.tempC = 68.0f;
        h->ui.update(h->snap);
        h->shot("64_hasil_masih_panas");
    }
    {   // Muat sampel: ringan / berat di luar anjuran
        for (int variant = 0; variant < 2; ++variant) {
            hbcycle::SimParams params;
            params.sampleMg = variant == 0 ? 3200.0f : 11000.0f;
            SimRun run(params);
            Harness* h = make();
            h->startTest();
            run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Load; });
            run.run(25000);
            h->show(run.ctl.view());
            h->shot(variant == 0 ? "65_muat_terlalu_ringan" : "66_pulih_sampel_11g");
        }
    }
    {   // Hasil TIMEOUT (sampel lambat kering) dan ERROR FIT
        hbcycle::SimParams params;
        params.dryTauMinAt110 = 40.0f;
        SimRun run(params);
        Harness* h = make();
        h->startTest();
        run.until([](const hbcycle::CycleView& v) { return v.phase == hbcycle::Phase::Done; });
        h->show(run.ctl.view());
        h->shot("67_hasil_timeout");
        Harness* f = make();
        f->startTest();
        hbcycle::CycleView v = run.ctl.view();
        v.result = okResult;
        v.result.status = hbcycle::Status::Error;
        v.result.reason = hbcycle::Reason::Fit;
        v.result.validated = 0;
        f->show(v);
        f->shot("68_hasil_error_fit");
        f->rotate(3);
        v.result.status = hbcycle::Status::Ok;
        v.result.reason = hbcycle::Reason::RateReached;
        v.result.validated = 1;
        v.result.flags |= hbcycle::flag::kModelDeviates;
        v.result.wfModelDevMg = -3.4f;
        f->show(v);
        f->shot("69_hasil_waktu_catatan");
    }
    {   // ERROR tanpa hasil: pintu, sensor, batal
        const hbcycle::Reason reasons[3] = {hbcycle::Reason::Lid, hbcycle::Reason::Sensor, hbcycle::Reason::Cancel};
        const char* names[3] = {"70_error_tutup", "71_error_sensor", "72_error_batal"};
        for (int i = 0; i < 3; ++i) {
            Harness* h = make();
            h->startTest();
            hbcycle::CycleView v{};
            v.phase = hbcycle::Phase::Failed;
            v.lidClosed = true;
            v.tempC = 96.0f;
            v.result = okResult;
            v.result.status = hbcycle::Status::Error;
            v.result.reason = reasons[i];
            v.result.hasMoisture = 0;
            h->show(v);
            h->shot(names[i]);
        }
    }
    {   // Riwayat: daftar + detail, Beranda dengan hasil terakhir, tes terputus
        Harness* h = make();
        const float mc[6] = {4.81f, 4.92f, 4.40f, NAN, 4.77f, 5.02f};
        const hbcycle::Status st[6] = {hbcycle::Status::Ok, hbcycle::Status::Ok, hbcycle::Status::Timeout,
                                       hbcycle::Status::Error, hbcycle::Status::Ok, hbcycle::Status::Ok};
        for (int i = 0; i < 8; ++i) {
            hbcycle::HistoryEntry e{};
            e.id = static_cast<uint32_t>(42 - i);
            e.startEpoch = i == 4 ? 0 : 1793952300u - static_cast<uint32_t>(i) * 15000u;
            e.status = st[i % 6];
            e.reason = e.status == hbcycle::Status::Error ? hbcycle::Reason::Lid : hbcycle::Reason::RateReached;
            e.hasMoisture = e.status == hbcycle::Status::Error ? 0 : 1;
            e.validated = e.hasMoisture;
            e.moisturePct = mc[i % 6];
            h->cmd.history.push_back(e);
        }
        h->cmd.stored = &okResult;
        h->snap.hasLast = true;
        h->snap.last = h->cmd.history[0];
        h->snap.historyCount = 38;
        h->snap.historyCapacity = 40;
        h->home();
        h->shot("73_beranda_hasil_terakhir");
        h->rotate(1);
        h->press();
        h->rotate(1);
        h->shot("74_riwayat_daftar");
        h->press();
        h->shot("75_riwayat_detail_utama");
        h->rotate(1);
        h->shot("76_riwayat_detail_grafik");
        h->rotate(1);
        h->shot("77_riwayat_detail_berat");
    }
    {   // Tes terputus saat boot + header simulasi
        Harness* h = make();
        h->snap.interruptedId = 41;
        h->snap.simulation = true;
        h->home();
        h->shot("78_dialog_tes_terputus");
    }
    return 0;
}
