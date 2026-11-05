#pragma once

// =====================================================================
//  app_config.h — konstanta level aplikasi (src/)
//  Konstanta milik driver ada di header masing-masing library. Pin dan
//  konstanta hardware ada di pins.h.
// =====================================================================

#include <cstddef>
#include <cstdint>

namespace app {

constexpr const char* kFirmwareName    = "Heat Box - LCD composite + rotary";
constexpr const char* kFirmwareVersion = "0.2.0";

constexpr uint32_t kSerialBaud = 115200;  // harus sama dengan monitor_speed
// TX buffer UART (harus > 128 = FIFO hardware). Tanpa buffer, output
// panjang memblok loop selama UART mengirim dan polling sensor telat.
constexpr size_t kSerialTxBufferSize = 1024;

// Periode baris report lama (human & CSV). Bawaan MATI; perintah 'v'
// memutar mati -> human -> CSV (untuk servis / dasbor arsip).
constexpr uint32_t kReportPeriodMs = 500;
// Ringkasan berkala: noise berat, suhu, heap, fps render, WiFi
constexpr uint32_t kSummaryPeriodMs = 10000;

// Buffer command serial: 1 huruf + argumen + '\0'
constexpr size_t kCommandBufferSize = 32;
// Buffer satu baris pesan log ("# ...")
constexpr size_t kLogBufferSize = 256;  // cukup untuk baris RINGKASAN

constexpr float kMsPerSecond = 1000.0f;

// Cek jumper SPEED ADS1232 dari waktu DRDY pertama setelah PDWN:
// t11 = 401.8 ms @10 SPS vs 52.51 ms @80 SPS (SBAS350H Tabel 7-10/7-12).
// Ambang diletakkan di tengah.
constexpr uint32_t kAdsFastWakeupThresholdMs = 200;

// ---------------------------------------------------------------------
//  Plausibility check RTD saat boot (PT100, rentang kerja heat box).
//  Di luar rentang ini hampir pasti RREF salah atau jumper wire-mode
//  belum benar. PT100: 80 ohm ~ -51 C, 200 ohm ~ +266 C.
// ---------------------------------------------------------------------
constexpr float kRtdPlausibleMinOhm = 80.0f;
constexpr float kRtdPlausibleMaxOhm = 200.0f;
constexpr float kRtdPlausibleMinC   = -50.0f;
constexpr float kRtdPlausibleMaxC   = 250.0f;

// Nilai resistor referensi FISIK di board MAX31865 (kode 431 = 430 ohm).
// Dipakai panel diagnostik 'x' untuk cek kewajaran yang tidak bergantung
// pada RTD_RREF di pins.h (yang sementara berisi faktor koreksi).
constexpr float kRtdRrefPhysicalOhm = 430.0f;

// Ulangi plausibility check berkala, bukan hanya saat boot: nilai bisa
// melenceng setelah chamber panas atau sambungan melemah.
constexpr uint32_t kRtdPlausibleCheckMs = 60000;

// Rasio kode ADC RTD (0..1) di atas ini berarti mendekati saturasi. Nilai
// yang macet di plafon tetap lolos cek rentang di atas, jadi dicek terpisah.
constexpr float kRtdRatioWarn = 0.95f;

// ---------------------------------------------------------------------
//  Initial zero setting: tare otomatis saat alat nyala, seperti timbangan.
//
//  Batas pengaman mengikuti praktik OIML R76 (initial zero-setting range
//  10 % kapasitas): kalau raw saat nyala menyimpang lebih dari ini dari
//  offset yang tersimpan, artinya ada beban di timbangan dan tare
//  otomatis DIBATALKAN supaya beban itu tidak ikut dinolkan.
//
//  Pengaman ini hanya menangkap kasus kasar. Beban kecil yang tertinggal
//  tetap akan ikut dinolkan — sama seperti timbangan mana pun.
// ---------------------------------------------------------------------
constexpr float kInitialZeroRangeGrams = 30.0f;  // 10 % dari kapasitas 300 g

// ---------------------------------------------------------------------
//  Ambang "sudah setimbang". Drift monotonik setelah alat nyala berasal
//  dari pemanasan sendiri dan EMF termoelektrik, bukan noise, sehingga
//  hanya bisa ditunggu — tidak bisa difilter.
//
//  Firmware TIDAK tare ulang otomatis saat ambang ini tercapai: kalau
//  sampel sudah di timbangan, tare akan menolkannya. Yang dilakukan hanya
//  memberi tahu bahwa inilah saat terbaik untuk tare ulang.
// ---------------------------------------------------------------------
constexpr float kSettledDriftMgPerMin = 3.0f;

// =====================================================================
//  Input rotary — SEMUA konstanta waktu & decoder di satu tempat
//  (skema lengkap: docs/ui/KONTROL-ROTARY.md)
// =====================================================================
namespace input {
constexpr uint32_t kPollPeriodMs = 1;  // task input 1 kHz di core 1

// Decoder: transisi quadrature per klik (detent). EC11 20 klik/20 pulsa
// = 4. Kalau 1 klik baru menghasilkan langkah tiap 2 klik, ganti ke 2.
// UJI INPUT menampilkan hitungan raw per klik untuk mengeceknya.
constexpr uint8_t kEncStepsPerDetent = 4;
// true kalau putar kanan ternyata mengurangi nilai (A/B tertukar)
constexpr bool kEncReverse = false;

constexpr uint32_t kButtonDebounceMs = 20;  // di atas RC 1k/100nF board
constexpr uint32_t kDoorDebounceMs   = 30;

constexpr uint32_t kPressMaxMs      = 600;   // lepas sebelum ini = TEKAN
constexpr uint32_t kHoldBarMs       = 600;   // bar progres muncul (= batas TEKAN)
constexpr uint32_t kHoldActionMs    = 2000;  // TAHAN: kembali / mulai / batal edit
constexpr uint32_t kHoldServiceMs   = 5000;  // TAHAN di Beranda: masuk SERVIS
constexpr uint32_t kHoldCancelTestMs = 3000; // batal tes berjalan (dipakai Prompt 2)

// Percepatan saat mengubah nilai: klik yang datang < jendela ini setelah
// klik sebelumnya dianggap putaran cepat -> langkah kAccelStep.
constexpr uint32_t kAccelWindowMs = 45;
constexpr int     kAccelStep     = 5;

// Anomali untuk UJI INPUT: klik berlawanan arah dalam jendela ini
// setelah klik sebelumnya dicurigai langkah mundur palsu.
constexpr uint32_t kReverseSuspectMs = 120;
}  // namespace input

// =====================================================================
//  UI & video
// =====================================================================
namespace ui {
constexpr uint32_t kMinRenderIntervalMs = 100;  // maks ~10 fps
constexpr uint32_t kSnapshotPeriodMs    = 50;
constexpr float kHotBannerOnC  = 50.0f;  // banner "ruang masih panas"
constexpr float kHotBannerOffC = 48.0f;  // histeresis supaya tidak berkedip
constexpr uint32_t kBootNetBudgetMs = 8000;  // boot maks menunggu WiFi + jam
}  // namespace ui

// =====================================================================
//  Task FreeRTOS (pembagian core: lihat RANCANGAN §9)
//    core 0: WiFi (bawaan IDF) + task sensor (ADS1232 critical section
//            hanya mematikan interupsi core 0)
//    core 1: ISR video (begin() dipanggil dari setup/loopTask), task
//            input 1 kHz, loop() = UI
// =====================================================================
namespace task {
constexpr int kSensorCore = 0;
constexpr unsigned kSensorPriority = 4;
constexpr uint32_t kSensorStackBytes = 8192;
constexpr int kInputCore = 1;
constexpr unsigned kInputPriority = 3;  // di atas loop() (prioritas 1)
constexpr uint32_t kInputStackBytes = 4096;
}  // namespace task

}  // namespace app
