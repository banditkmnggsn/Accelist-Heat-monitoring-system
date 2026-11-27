#pragma once

// =====================================================================
//  OtaService.h — update firmware lewat WiFi (ArduinoOTA, keputusan 8.1)
//
//  - Aktif hanya kalau include/secrets.h berisi OTA_PASS tidak kosong
//    (tanpa sandi, siapa pun di jaringan pabrik bisa mengganti firmware).
//  - Upload dari PC: pio run -e ota -t upload (lihat platformio.ini).
//  - Pengaman: OTA hanya dilayani saat siklus TIDAK berjalan; saat mulai
//    menulis, lampu dipaksa mati dan LCD menampilkan layar UPDATE.
//  - update() dari task sensor (core 0). handle() memblok selama transfer;
//    watchdog task di-reset tiap potongan data.
//  Nanti diganti/ditambah update lewat aplikasi IoT.
// =====================================================================

#include <UiModel.h>

class NetClock;

namespace ota_service {

void begin(NetClock& net);
// allowed = false selama siklus berjalan (permintaan upload diabaikan)
void update(bool allowed);
hbui::OtaState state();
uint8_t percent();

}  // namespace ota_service
