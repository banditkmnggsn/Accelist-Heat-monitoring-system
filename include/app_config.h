#pragma once

// =====================================================================
//  app_config.h — konstanta level aplikasi (main.cpp)
//  Konstanta milik driver ada di header masing-masing library.
// =====================================================================

#include <cstddef>
#include <cstdint>

namespace app {

constexpr const char* kFirmwareName    = "Heat Box - riset breadboard sensor";
constexpr const char* kFirmwareVersion = "0.1.0";

constexpr uint32_t kSerialBaud = 115200;  // harus sama dengan monitor_speed
// TX buffer UART (harus > 128 = FIFO hardware). Tanpa buffer, output
// panjang memblok loop selama UART mengirim dan polling sensor telat.
constexpr size_t kSerialTxBufferSize = 1024;

// Periode baris report (human & CSV)
constexpr uint32_t kReportPeriodMs = 500;

// Buffer command serial: 1 huruf + argumen + '\0'
constexpr size_t kCommandBufferSize = 32;
// Buffer satu baris pesan log ("# ...")
constexpr size_t kLogBufferSize = 192;

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

// Ulangi plausibility check berkala, bukan hanya saat boot: nilai bisa
// melenceng setelah chamber panas atau sambungan melemah.
constexpr uint32_t kRtdPlausibleCheckMs = 60000;

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

}  // namespace app
