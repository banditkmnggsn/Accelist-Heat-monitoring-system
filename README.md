# Heat Box — moisture analyzer tepung telur

Alat ukur **kadar air tepung telur** dengan metode *loss on drying*. Sampel 5–10 g
dipanaskan lampu halogen di dalam ruang berinsulasi sambil terus ditimbang, lalu:

```
kadar air (basis basah) = (W0 − Wf) / W0 × 100 %
```

Proyek magang Accelist, saat ini di **tahap breadboard**.

## Cara kerja

1. **Pre-heat** ruang dengan cawan kosong (bawaan 80 °C, 10 menit sejak tombol Start), lalu
   tare dalam kondisi panas.
2. **Sampel masuk.** Tutup dibuka → lampu mati; tutup ditutup → suhu pulih → berat awal **W0** dicatat.
3. **Heat** (bawaan 110 °C). Berat dibaca tiap detik dan diolah menjadi satu titik per menit.
4. Selama laju susut **> 4 mg/menit**, keputusan memakai data load cell. Setelah itu sisa
   kurva **diprediksi** dengan model eksponensial, dan pemanasan berhenti 2 menit setelah
   laju model mencapai **1 mg/menit** (batas 3–25 menit).
5. **Wf** dicatat sebelum lampu mati → kadar air + status **OK / TIMEOUT / ERROR**.

Semua suhu dan batas waktu bisa diatur operator lewat LCD + rotary encoder (ada Reset
Default). Rinciannya ada di [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md).

## Perangkat keras

| Bagian | Komponen |
|---|---|
| Pemroses | ESP32 DevKit V1 (cadangan: ESP32-S3) |
| Berat | load cell 300 g + ADC ADS1232 (driver bit-bang sendiri) |
| Suhu | PT100 + MAX31865 |
| Pemanas | lampu halogen + SSR, limit switch di tutup *(rencana)* |
| Antarmuka | LCD composite + rotary encoder + tombol Start 22 mm *(rencana)*; USB serial ke PC |

## Status

| Bagian | Status |
|---|---|
| Driver load cell, suhu, encoder | ✅ jalan, load cell sudah terkarakterisasi |
| Sensor suhu PT100 | ✅ pengawatan 3-wire benar, RREF di-tuning dengan multimeter ([spesifikasi §8.1](SPESIFIKASI-TEKNIS.md)) |
| Dasbor PC + kalibrasi multi-titik | ✅ jalan |
| Matematika siklus + nilai per menit di firmware (`lib/HeatboxCycle`) | ✅ teruji, cocok dengan versi Python |
| Siklus di firmware, pemanas, log flash, LCD | ⏳ belum dibuat |

## Mulai

```bash
pio run                        # build (platform dikunci espressif32@6.5.0 / core 2.0.14)
pio run -t upload              # flash ke ESP32
pio device monitor             # serial 115200, ketik ? untuk daftar perintah

python -m pip install pyserial
python tuning_web/server.py    # dasbor di http://127.0.0.1:8080 (port COM3; ganti lewat TUNING_SERIAL_PORT)
```

- Serial Monitor dan dasbor tidak bisa dipakai bersamaan; tutup salah satu sebelum
  upload.
- `pio run -v` (verbose) gagal di PlatformIO Core 6.2.0 karena bug tampilan; build tanpa
  `-v` normal.

## Struktur

```
include/          pin (pins.h) dan konstanta aplikasi (app_config.h)
lib/Ads1232/      driver load cell
lib/RtdSensor/    driver sensor suhu
lib/EncoderInput/ rotary encoder
lib/HeatboxCycle/ logika siklus di firmware (dalam pengerjaan)
lib/Heatbox/      versi lama, tidak dipakai
src/main.cpp      perekat: report serial dan perintah
tuning_web/       dasbor PC (Python)
test/             uji unit
```

## Dokumen

| Dokumen | Isi |
|---|---|
| [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md) | Rancangan lengkap: alur, pengaturan bawaan, pengolahan data, status, log, LCD, rencana uji, pertanyaan terbuka |
| [SPESIFIKASI-TEKNIS.md](SPESIFIKASI-TEKNIS.md) | Angka terukur, pin, protokol, kalibrasi, perintah serial, masalah terbuka |
| [tuning_web/README.md](tuning_web/README.md) | Cara memakai dasbor PC |
| [test/README.md](test/README.md) | Cara menjalankan uji |
