# Heat Box — dasbor PC

Web lokal yang membaca report firmware dari ESP32 lewat serial (`115200` baud), menjalankan
siklus pengeringan sampel, dan menyimpan datanya. Semua perhitungan ada di sini; ESP32
hanya mengirim baris teks dan tidak membutuhkan Wi-Fi.

Awalnya folder ini alat tuning sementara. Sejak siklus pengeringan ditambahkan, ia menjadi
antarmuka utama alat — **jangan dihapus**. Rincian desain, bukti, dan keterbatasannya ada di
[CATATAN-SIKLUS-PENGERINGAN.md](../CATATAN-SIKLUS-PENGERINGAN.md).

## Menjalankan

```powershell
python -m pip install pyserial
python tuning_web/server.py
```

Buka http://127.0.0.1:8080. Tutup dengan `Ctrl+C`. Port default `COM3`; kalau berbeda:

```powershell
$env:TUNING_SERIAL_PORT = "COM5"
python tuning_web/server.py
```

Serial Monitor PlatformIO dan proses upload harus ditutup/selesai dulu, karena satu port
serial tidak bisa dipakai dua program sekaligus. Sebaliknya, tutup server ini sebelum
meng-upload firmware.

## Berkas

| Berkas | Isi |
|---|---|
| `server.py` | server HTTP, pembaca serial, endpoint API |
| `cycle.py` | mesin siklus pengeringan: fase, nilai per menit, cut-off, penyimpanan |
| `drying.py` | matematika kurva: garis robust, Theil-Sen, PAVA, fit eksponensial, prediksi |
| `analysis.py` | regresi dan metrik kalibrasi multi-titik |
| `index.html` | halaman dasbor |
| `test_cycle.py` | uji otomatis, termasuk siklus penuh dengan data sintetis |

Uji: `cd tuning_web` lalu `python -m unittest test_cycle -v`.

## Panel "Siklus sampel"

Alur: **Mulai siklus** → tunggu drift nol kecil → **Tare wadah kosong** → masukkan
sampel → **Mulai pemanas** → titik tiap menit → cut-off otomatis saat laju < 10 mg/menit
selama 2 menit → prediksi waktu berhenti saat laju model < 0,1 mg/menit.

Pemanas belum terpasang: **Mulai pemanas** hanya menandai waktu nol. Data tiap siklus ada di
`data/cycle_<tanggal>_<jam>/` (`samples.csv`, `minutes.csv`, `summary.json`), dan bisa
diunduh dari bagian *Berkas data siklus*.

Semua ambang bisa diubah di *Pengaturan siklus* selama pemanasan tidak sedang berjalan.

## Panel "Serial terakhir"

- **Bekukan** menghentikan tampilan sepenuhnya; baris baru tetap dikumpulkan di latar
  belakang dan penghitung menunjukkan berapa yang tertahan, jadi tidak ada data hilang.
- **Filter** menyaring baris berdasarkan potongan teks, misalnya `Tare`.
- **Unduh** menyimpan baris yang sedang tampil (ikut filter) ke berkas `.log`.

Server menyimpan 20000 baris terakhir (`TUNING_LOG_MAX`).

## Bagian "Perawatan & kalibrasi" (terlipat)

Perintah firmware (tare, self-cal, reset ADS1232, diagnostik load cell & RTD, bantuan),
kalibrasi 1 titik, pengaturan tampilan tenang, dan kalibrasi multi-titik.

### Kalibrasi multi-titik

Diulang untuk tiap anak timbangan:

1. Kosongkan timbangan, tekan **Rekam NOL** (perintah `n`).
2. Pilih beban dari tombol preset atau ketik sendiri dalam mg, pilih arah naik/turun.
3. Letakkan beban, tekan **Beban sudah diletakkan** (perintah `m <mg> <u|d>`).
4. Angkat beban, tekan **Rekam NOL** lagi.

Setiap rekaman menunggu **waktu tunggu yang sama** lebih dulu (default 30 detik, dengan
hitung mundur). Creep sesaat setelah beban diletakkan puluhan mg/menit; kalau jedanya
berbeda-beda, creep yang ikut terukur juga berbeda dan muncul sebagai repeatability buruk.

Tiap titik dikoreksi terhadap rata-rata nol **sebelum dan sesudah** pembebanan, yang
membatalkan drift nol. Firmware hanya mencetak baris `CALZERO,` / `CALPT,` berisi **raw
counts**; fit, residual, histeresis, dan repeatability dihitung di PC oleh `analysis.py`.

Kalau semua titik di satu nominal, garis tidak bisa difit, tetapi panel tetap menampilkan
**repeatability** dan membandingkannya dengan noise elektronik: kalau sebaran teramati jauh
lebih besar, penyebabnya mekanis (posisi beban, waktu tunggu).

**Span ditetapkan oleh beban terbesar.** Error skala = drift selama jeda ÷ berat beban,
jadi 500 mg dengan repeatability 4,5 mg memberi 0,9 %, sedangkan 50 g hanya 0,009 %. Beban
kecil gunanya untuk menguji linearitas.

Kolom **± (mg)** dan **rasio S/N** menunjukkan apakah sebuah titik bermakna; S/N di bawah
10 ditandai merah. **Terapkan ke firmware** mengirim `s <countsPerGram>` hasil fit ke NVS.
Tiap sesi server menulis `data/calibration_<tanggal>_<jam>.csv` sendiri.
