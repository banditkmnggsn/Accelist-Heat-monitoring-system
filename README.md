# Heat Box — moisture analyzer tepung telur

Alat ukur **kadar air tepung telur** dengan metode *loss on drying*. Sampel 5–10 g
dipanaskan lampu halogen di dalam ruang berinsulasi sambil terus ditimbang, lalu:

```
kadar air (basis basah) = (W0 − Wf) / W0 × 100 %
```

Proyek magang Accelist, saat ini di **tahap breadboard**.

## Cara kerja

1. **Pre-heat** ruang dengan cawan kosong (bawaan 80 °C, 10 menit sejak tes dimulai), lalu
   tare dalam kondisi panas.
2. **Sampel masuk.** Tutup dibuka → lampu mati; tutup ditutup → suhu pulih → berat awal **W0** dicatat.
3. **Heat** (bawaan 110 °C). Berat dibaca tiap detik dan diolah menjadi satu titik per menit.
4. Selama laju susut **> 4 mg/menit**, keputusan memakai data load cell. Setelah itu sisa
   kurva **diprediksi** dengan model eksponensial, dan pemanasan berhenti 2 menit setelah
   laju model mencapai **1 mg/menit** (batas 3–25 menit).
5. **Wf** dicatat sebelum lampu mati → kadar air + status **OK / TIMEOUT / ERROR**.

Semua suhu dan batas waktu bisa diatur operator lewat LCD + rotary encoder (ada Reset
Default). Alat berdiri sendiri; tidak ada lagi dasbor PC. Rinciannya ada di [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md).

## Perangkat keras

| Bagian | Komponen |
|---|---|
| Pemroses | ESP32 DevKit V1 / ESP32-WROOM-32, dual core (cadangan: ESP32-S3) |
| Berat | load cell 300 g + ADC ADS1232 (driver bit-bang sendiri) |
| Suhu | PT100 + MAX31865 |
| Pemanas | lampu halogen + SSR (GPIO22) + relay pengaman K1 (GPIO21) — **belum dinyalakan firmware** |
| Antarmuka | LCD composite 256×240 (GPIO25) + rotary encoder; limit switch pintu (GPIO34); buzzer pasif (GPIO12); tombol Start 22 mm (GPIO35, belum dipakai) |
| Skematik | [`docs/hardware/`](docs/hardware/) — breadboard dirangkai sama persis dengan skematik |

## Status

| Bagian | Status |
|---|---|
| Driver load cell, suhu | ✅ jalan, load cell sudah terkarakterisasi |
| Sensor suhu PT100 | ✅ pengawatan 3-wire benar, RREF dari multimeter + offset −1,5 °C ([spesifikasi §8.1](SPESIFIKASI-TEKNIS.md)) |
| Matematika siklus + nilai per menit di firmware (`lib/HeatboxCycle`) | ✅ teruji, cocok dengan versi Python |
| LCD + rotary (Prompt 1): boot, beranda, pilih profil, pengaturan; WiFi + jam NTP; buzzer | ✅ diuji di LCD fisik 6 Nov 2026 |
| Prompt 2: siklus di firmware (PRE-HEAT .. HASIL), kontrol lampu PI + K1/SSR, log LittleFS + riwayat, layar tes/hasil/error/riwayat, env `simulasi` | 🧪 ter-build + 58 uji PC + simulasi; menunggu uji di alat (lampu) |
| Pengaturan lengkap (27 Nov 2026): SET PROFIL, WIFI & JAM (ganti SSID/sandi di LCD), INFO PERANGKAT, OPSI PENGEMBANG (status sensor, timbangan, kalibrasi, cek, parameter metode, kontrol pemanas, tampilan, memori log), OTA. Menu SERVIS lama dihapus | 🧪 ter-build + 64 uji PC + pratinjau; menunggu uji di alat |
| Aplikasi HP (Flutter) | ⏳ belum dimulai |
| Dasbor PC | 📦 diarsipkan (`archive/tuning_web`) |

## Mulai

```bash
pio run                        # build (platform dikunci espressif32@6.5.0 / core 2.0.14)
pio run -t upload              # flash ke ESP32 (COM5; tutup Serial Monitor dulu)
pio run -e simulasi -t upload  # sama, tetapi sensor & lampu DITIRU (lampu asli tidak menyala)
pio device monitor             # serial 115200, ketik ? untuk daftar perintah
pio test -e native             # unit test di PC (butuh gcc)
```

- **WiFi:** bisa diatur langsung di LCD (PENGATURAN > WIFI & JAM > GANTI WIFI; tersimpan di
  NVS setelah berhasil tersambung). `include/secrets.h` (salinan `secrets.example.h`) hanya
  cadangan kalau NVS kosong, dan tempat `OTA_PASS`.
- **OTA (update lewat WiFi):** isi `OTA_PASS` di `include/secrets.h`, upload sekali lewat USB.
  Setelah itu `pio run -e ota -t upload --upload-port <NAMA OTA>.local` dengan variabel
  lingkungan `HEATBOX_OTA_PASS` = sandi itu. Nama/IP ada di PENGATURAN > INFO PERANGKAT.
  OTA ditolak selama tes berjalan.
- **Build flag** di `platformio.ini`: `VIDEO_PAL=0/1` (NTSC/PAL), `ENABLE_WIFI=1/0`.
- Serial: report lama 500 ms **mati** bawaan (`v` untuk menyalakan); tiap 10 detik muncul baris
  `# RINGKASAN` (noise berat, suhu, heap, fps, WiFi).
- `pio run -v` (verbose) gagal di PlatformIO Core 6.2.0 karena bug tampilan; build tanpa
  `-v` normal.

## Kontrol

Hanya rotary: **PUTAR** pilih/ubah, **TEKAN** OK, **TAHAN 2 dtk** kembali (di Pilih Profil:
mulai). Menu tim IT: PENGATURAN > OPSI PENGEMBANG, lalu **TAHAN 5 dtk**. Lengkapnya di
[docs/ui/KONTROL-ROTARY.md](docs/ui/KONTROL-ROTARY.md).

## Struktur

```
include/            pin (pins.h), konstanta aplikasi (app_config.h), contoh secrets
lib/Ads1232/        driver load cell
lib/RtdSensor/      driver sensor suhu
lib/HeatboxCycle/   logika siklus + pengaturan (C++ murni, diuji di PC)
lib/HeatboxInput/   decoder rotary + gesture TEKAN/TAHAN (C++ murni, diuji di PC)
lib/HeatboxUi/      tampilan LCD: tema, ikon, widget, layar, manajer layar
src/main.cpp        perekat: video, UI loop (core 1)
src/SensorService   task sensor + perintah serial + ringkasan (core 0)
src/InputTask       polling rotary/pintu 1 kHz + buzzer (core 1)
src/Buzzer, sounds.h  buzzer (aktif, digerakkan LEDC) + tabel suara
src/NetClock        WiFi STA + NTP WIB + pindai/ganti WiFi dari LCD
src/OtaService      update firmware lewat WiFi (ArduinoOTA)
src/CycleService    siklus + pin lampu (task sensor), env simulasi
src/TestLog         log tes di LittleFS + riwayat
src/SettingsStore   pengaturan operator + opsi pengembang di NVS "hbcfg"
test/               uji unit (PC)
tools/ui_render/    alat dev: render PNG semua layar di PC
docs/ui/            desain, keputusan UI, kontrol rotary, pratinjau layar
docs/hardware/      skematik
archive/            semua yang tidak dipakai lagi (lihat archive/README.md)
```

## Dokumen

| Dokumen | Isi |
|---|---|
| [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md) | Rancangan lengkap: alur, pengaturan bawaan, pengolahan data, status, log, LCD, rencana uji, pertanyaan terbuka |
| [SPESIFIKASI-TEKNIS.md](SPESIFIKASI-TEKNIS.md) | Angka terukur, pin, protokol, kalibrasi, perintah serial, masalah terbuka |
| [docs/ui/KEPUTUSAN-UI.md](docs/ui/KEPUTUSAN-UI.md) | Keputusan tampilan, terutama yang menyimpang dari desain |
| [docs/ui/KONTROL-ROTARY.md](docs/ui/KONTROL-ROTARY.md) | Skema kontrol rotary per layar |
| [test/README.md](test/README.md) | Cara menjalankan uji |
| [archive/README.md](archive/README.md) | Isi arsip dan cara memulihkannya |
