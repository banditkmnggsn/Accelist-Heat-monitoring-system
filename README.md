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
| LCD + rotary (Prompt 1): boot, beranda, pilih profil, pengaturan, servis uji tampilan/input/suara; WiFi + jam NTP; buzzer | 🧪 ter-build, menunggu uji di LCD fisik |
| Siklus di firmware, pemanas, log flash, layar tes/hasil/riwayat, servis lengkap, OTA | ⏳ Prompt 2 |
| Dasbor PC | 📦 diarsipkan (`archive/tuning_web`) |

## Mulai

```bash
pio run                        # build (platform dikunci espressif32@6.5.0 / core 2.0.14)
pio run -t upload              # flash ke ESP32 (COM5; tutup Serial Monitor dulu)
pio device monitor             # serial 115200, ketik ? untuk daftar perintah
pio test -e native             # unit test di PC (butuh gcc)
```

- **WiFi:** salin `include/secrets.example.h` menjadi `include/secrets.h` lalu isi SSID dan
  sandi. Tanpa file itu build tetap jalan, WiFi dilewati.
- **Build flag** di `platformio.ini`: `VIDEO_PAL=0/1` (NTSC/PAL), `ENABLE_WIFI=1/0`.
- Serial: report lama 500 ms **mati** bawaan (`v` untuk menyalakan); tiap 10 detik muncul baris
  `# RINGKASAN` (noise berat, suhu, heap, fps, WiFi).
- `pio run -v` (verbose) gagal di PlatformIO Core 6.2.0 karena bug tampilan; build tanpa
  `-v` normal.

## Kontrol

Hanya rotary: **PUTAR** pilih/ubah, **TEKAN** OK, **TAHAN 2 dtk** kembali (di Pilih Profil:
mulai), **TAHAN 5 dtk di Beranda** = menu servis. Lengkapnya di
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
src/Buzzer, sounds.h  buzzer pasif LEDC + tabel suara
src/NetClock        WiFi STA + NTP WIB
src/SettingsStore   pengaturan di NVS "hbcfg"
test/               uji unit (PC)
tools/ui_render/    alat dev: render PNG semua layar di PC
docs/ui/            desain, keputusan UI, kontrol rotary, pratinjau layar
docs/hardware/      skematik
archive/            semua yang tidak dipakai lagi (lihat archive/README.md)
```

## Dokumen

| Dokumen | Isi |
|---|---|
| [docs/KONTEKS-SINKRON.md](docs/KONTEKS-SINKRON.md) | Konteks lengkap sesi pengembangan (status, aturan, keputusan, lingkup Prompt 2) — dibaca dulu oleh sesi Claude baru |
| [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md) | Rancangan lengkap: alur, pengaturan bawaan, pengolahan data, status, log, LCD, rencana uji, pertanyaan terbuka |
| [SPESIFIKASI-TEKNIS.md](SPESIFIKASI-TEKNIS.md) | Angka terukur, pin, protokol, kalibrasi, perintah serial, masalah terbuka |
| [docs/ui/KEPUTUSAN-UI.md](docs/ui/KEPUTUSAN-UI.md) | Keputusan tampilan, terutama yang menyimpang dari desain |
| [docs/ui/KONTROL-ROTARY.md](docs/ui/KONTROL-ROTARY.md) | Skema kontrol rotary per layar |
| [test/README.md](test/README.md) | Cara menjalankan uji |
| [archive/README.md](archive/README.md) | Isi arsip dan cara memulihkannya |
