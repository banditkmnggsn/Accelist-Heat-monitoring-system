#pragma once

// =====================================================================
//  secrets.example.h — CONTOH. Salin menjadi include/secrets.h lalu isi.
//  include/secrets.h ada di .gitignore: jangan pernah di-commit.
//
//  Kredensial WiFi di NVS (namespace "hbnet") diprioritaskan; file ini
//  dipakai kalau NVS kosong. Tanpa secrets.h build tetap jalan, WiFi
//  saja yang dilewati (boot: WiFi "TANPA SSID").
// =====================================================================

#define WIFI_SSID "nama-wifi"
#define WIFI_PASS "kata-sandi-wifi"
// Sandi update firmware lewat WiFi (OTA). KOSONG = OTA mati. Isi lalu
// upload sekali lewat USB; setelah itu bisa: set HEATBOX_OTA_PASS=<sandi>
// dan pio run -e ota -t upload (lihat platformio.ini).
#define OTA_PASS  ""
