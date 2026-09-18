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

}  // namespace app
