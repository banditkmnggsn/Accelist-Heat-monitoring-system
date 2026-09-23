# Prompt untuk AI code di VS Code — Heat Box, perbaikan akurasi sensor

> Copy seluruh isi file ini ke AI code di VS Code (Copilot / Claude Code / Cursor),
> dengan folder project `Accelist-Heat-monitoring-system` terbuka.

---

## Konteks proyek

Firmware ESP32 (PlatformIO, `espressif32@6.5.0` / Arduino core 2.0.14 — **JANGAN
dinaikkan**, library composite video fase berikutnya tidak kompatibel core 3.x).

Struktur yang sudah ada dan harus dipertahankan:

```
include/pins.h            alokasi pin + RTD_RREF, RTD_RNOMINAL, RTD_WIRES
include/app_config.h      konstanta level aplikasi
lib/Ads1232/              driver bit-bang ADS1232 (load cell 300 g, gain 128, 10 SPS)
lib/RtdSensor/            wrapper MAX31865 (PT100 3-wire) di atas Adafruit_MAX31865
lib/EncoderInput/         encoder EC11, polling
lib/Heatbox/              matematika kurva pengeringan (median, slope, curve fit)
src/main.cpp              loop(), report serial, command handler 1 huruf
tuning_web/server.py      HTTP server DI PC, baca serial + kirim command
tuning_web/index.html     dashboard di browser PC
```

Arsitektur yang WAJIB dipertahankan:

- **Dashboard tetap di PC (`tuning_web/`), TIDAK dipindah ke ESP32.**
  Jangan tambahkan WiFi, WebServer, LittleFS, atau penyimpanan CSV di firmware.
  ESP32 hanya mengirim baris teks lewat serial; PC yang menyimpan, menghitung,
  dan menggambar. Alasan: PC punya disk, Python, dan tidak ada risiko WiFi
  merebut CPU saat bit-bang SCLK ADS1232.
- Driver ADS1232 tetap bit-bang manual, **bukan** library pihak ketiga.
- Encoder tetap polling, bukan interrupt.
- Kalibrasi load cell tetap disimpan di NVS (`Preferences`, namespace `heatbox`).

---

## LARANGAN KERAS

1. **DILARANG menambahkan offset, faktor koreksi, atau fudge factor manual pada
   pembacaan suhu RTD** untuk "membetulkan" hasil. Error RTD saat ini bersifat
   multiplikatif (~3,4×) dan akan makin melenceng saat chamber panas. Koreksi
   manual juga akan menyembunyikan sambungan yang buruk. Yang boleh diubah hanya
   `RTD_RREF` di `pins.h`, dan HANYA ke nilai hasil pengukuran fisik resistor
   referensi di board.
2. Jangan mengubah pin map di `pins.h`. GPIO25/26 dicadangkan untuk composite
   video dan tidak boleh dipakai.
3. Jangan menaikkan versi platform di `platformio.ini`.
4. Jangan menghapus komentar referensi datasheet (nomor section SBAS350H) yang
   sudah ada di `Ads1232.cpp` / `Ads1232.h` — itu dokumentasi yang dipakai untuk
   laporan magang.

---

## TUGAS 1 — Perbaiki bug yang sudah teridentifikasi

### 1.1 `stdDevCounts()` melaporkan noise yang terlalu optimistis

Di `lib/Ads1232/src/Ads1232.cpp`, `processSample()` menghitung `stdDev_` dari isi
`avgBuf_`, padahal `avgBuf_` berisi **hasil median-of-5**, bukan sampel mentah.
Akibatnya angka noise yang dilaporkan 2–3× lebih baik dari kenyataan.

Perbaiki:
- Tambahkan ring buffer terpisah berisi **raw counts** (`int32_t rawRing_[64]`).
- Tambahkan getter `float rawStdDevCounts() const` dan
  `int32_t rawPeakToPeakCounts() const` yang dihitung dari ring raw itu.
- `stdDevCounts()` yang lama tetap ada (dipakai `isStable()`), tapi beri komentar
  bahwa nilainya adalah noise **setelah** filter, dan jangan dipakai untuk
  laporan spesifikasi.

### 1.2 Median-of-5 sliding membuang resolusi

Median dari 5 sampel Gaussian hanya punya efisiensi ~70% dibanding mean-nya, dan
median sliding membuat output berurutan berkorelasi sehingga `kAverageSize = 16`
TIDAK memberi pengurangan noise √16.

Ubah tahap median jadi **spike gate**, bukan filter permanen:
- Hitung median-of-5 seperti sekarang, tapi jangan langsung dipakai.
- Jika `abs(raw - median) > kSpikeGateSigma * rawStdDev` (pakai
  `kSpikeGateSigma = 6.0f`, konstanta baru di header), masukkan `median` ke
  averager. Kalau tidak, masukkan `raw`.
- Kalau `rawStdDev` belum valid (ring belum penuh), pakai `median` seperti
  perilaku lama.
- Tambahkan penghitung `uint32_t spikesRejected_` + getter, supaya bisa dilihat
  seberapa sering gate aktif.

### 1.3 Dua sumber kebenaran untuk skala (BUG NYATA)

`lib/Heatbox/src/HeatboxConfig.h` punya `#define COUNTS_PER_MG 21.0f`
(= 21000 counts/g), sementara `Ads1232::kDefaultCountsPerGram` ≈ 7158 counts/g
dan nilai sebenarnya dari hardware ≈ 5000 counts/g. Semua angka mg yang dicetak
`HeatboxMath.cpp` (`rmse_mg`, `resid_mg`, `STABLE_SD_MG`) karena itu salah ~4×.

Perbaiki:
- Hapus `#define COUNTS_PER_MG` dari `HeatboxConfig.h`.
- Ganti jadi variabel runtime yang di-set dari `loadCell.countsPerGram() / 1000.0f`
  saat kalibrasi berubah. Buat setter di namespace `heatbox`, misal
  `heatbox::setCountsPerMg(float)`, dan panggil dari `main.cpp` setiap kali
  event `CalibrationDone` / saat boot setelah `loadCalibration()`.
- Kalau skala belum dikalibrasi, fungsi-fungsi yang melaporkan mg harus
  mengembalikan `NAN`, bukan angka palsu.

### 1.4 Offset tare dibulatkan ke integer

`finishJob()` memakai `std::lround(mean)` untuk `offset_`. Simpan sebagai `double`
(`offsetCounts_`) supaya tidak kehilangan pecahan count (~0,2 mg). NVS tetap bisa
menyimpannya lewat `putFloat`/`putDouble`. Getter `offset()` boleh tetap
mengembalikan pembulatan untuk tampilan, tapi `grams()` harus memakai nilai
presisi penuh.

### 1.5 Tare & kalibrasi memakai sampel yang sudah difilter

`finishJob()` merata-rata `median`, bukan raw. Ubah `jobSum_` supaya mengakumulasi
**raw counts** (setelah spike gate 1.2), dan naikkan `kTareSamples` dari 32 jadi
**64** (6,4 detik @10 SPS). Noise tare turun ~√2 dan biayanya cuma waktu.

### 1.6 Ambang stabil dinyatakan dalam count, bukan mg

`kStableStdDevCounts = 3.0f` tidak bertahan kalau skala berubah. Ganti jadi
`kStableStdDevMg = 0.6f` dan konversi ke count memakai `countsPerGram_` saat
runtime. Kalau belum terkalibrasi, jatuh kembali ke ambang count yang lama.

### 1.7 RTD tidak dirata-rata sama sekali

`RtdSensor::readSample()` memakai `rawCode_` langsung. 1 LSB = 0,0131 Ω ≈ 0,034 °C,
dan log menunjukkan wobble ~1,5 LSB. Tambahkan moving average 16 sampel atas
`rawCode_` (buffer `uint16_t`, sum `uint32_t`), lalu hitung `resistance_` dan
`tempC_` dari hasil rata-ratanya. Simpan juga `rawCode_` yang belum dirata-rata
dan ekspos lewat getter baru `rawCodeInstant()` untuk panel diagnostik.

**Jangan** mengubah `RTD_RREF` atau menambahkan koreksi suhu apa pun (lihat
LARANGAN KERAS #1).

---

## TUGAS 2 — Sistem kalibrasi multi-titik

Tujuan: mengumpulkan data kalibrasi mentah yang tetap berguna walaupun rumus
konversi diganti di kemudian hari. Karena itu yang disimpan adalah **RAW COUNTS**,
bukan gram hasil konversi.

### 2.1 Firmware — perintah baru di `src/main.cpp`

Tambahkan perintah serial berikut (ikuti gaya command 1 huruf yang sudah ada,
dan tambahkan ke `printHelp()`):

| Perintah | Arti |
|---|---|
| `m <mg> <u\|d>` | Rekam satu titik kalibrasi. `<mg>` = nilai nominal anak timbangan dalam **miligram**. `<u>` = arah naik (loading), `<d>` = arah turun (unloading). |
| `n` | Rekam titik nol (raw tanpa beban). Dipakai sebagai "raw nol sebelum" titik berikutnya. |
| `d` | Jalankan diagnostik noise (lihat Tugas 3). |
| `x` | Cetak panel diagnostik RTD lengkap (lihat Tugas 3). |

Perilaku `m <mg> <u|d>`:
1. Tolak kalau `loadCell.isBusy()` atau belum pernah ada perintah `n`.
2. Kumpulkan **64 sampel raw** (6,4 detik), hitung mean dan standar deviasi.
3. Setelah selesai, rekam ulang raw nol (64 sampel lagi, setelah beban diangkat
   — minta user menekan Enter dulu, atau sediakan perintah `n` terpisah yang
   dipanggil user; pilih yang paling sederhana dan dokumentasikan di help).
4. Cetak SATU baris dengan prefix `CALPT,` berisi, dipisah koma:

```
CALPT,<millis>,<nominal_mg>,<arah:U|D>,<raw_mean>,<raw_sd>,<raw_zero_before>,<raw_zero_after>,<rtd_ohm>,<rtd_tempC>,<n_samples>
```

Contoh:
```
CALPT,318420,10000.000,U,50231.44,2.13,124.87,126.02,110.31,26.74,64
```

Catatan format:
- `raw_mean`, `raw_sd`, `raw_zero_*` dicetak dengan 2 desimal (mereka hasil
  rata-rata, jadi pecahan count bermakna).
- Baris `CALPT,` harus **tidak** diawali `# ` supaya parser PC bisa
  membedakannya dari log biasa.
- Firmware TIDAK menyimpan titik-titik ini. Firmware hanya mencetak. Penyimpanan
  dan perhitungan fit dilakukan di PC.

### 2.2 PC — `tuning_web/server.py`

Tambahkan:
- Regex kedua untuk menangkap baris `CALPT,...` dan mem-parse-nya jadi dict.
- Setiap titik di-append ke file CSV `tuning_web/data/calibration_<YYYYMMDD>.csv`
  dengan header kolom yang sama persis. **Append, jangan pernah overwrite.**
- Endpoint `GET /api/calibration` → seluruh isi CSV hari ini sebagai JSON.
- Endpoint `GET /api/calibration.csv` → download file CSV mentah.
- Endpoint `POST /api/command` sudah ada; perluas regex validasinya supaya
  menerima `m <angka> <u|d>`, `n`, `d`, `x`.

### 2.3 PC — perhitungan fit dan metrik (`tuning_web/analysis.py`, file baru)

Semua perhitungan di Python, memakai hanya stdlib (jangan tambah dependensi
numpy/scipy — proyek ini dijalankan di laptop kantor tanpa hak admin; kalau
numpy sudah terpasang boleh dipakai, tapi sediakan fallback murni Python).

Fungsi yang harus ada:

1. `fit_linear(points)` → least-squares orde 1 pada
   `(raw_mean - raw_zero_avg)` vs `nominal_mg`, mengembalikan slope
   (counts/mg), intercept, dan R².
2. `fit_quadratic(points)` → least-squares orde 2, opsional, dengan flag.
3. `residuals(points, fit)` → residual **tiap titik dalam miligram**
   (bukan counts, bukan %FS).
4. `metrics(points, fit)` → dict berisi:
   - `max_residual_mg` — residual absolut terbesar
   - `rms_residual_mg` — akar rata-rata kuadrat residual
   - `nonlinearity_pct_fs` — `max_residual_mg / kapasitas_mg * 100`
   - `hysteresis_mg` — untuk tiap nilai nominal yang punya titik `U` dan `D`,
     selisih pembacaannya; laporkan yang terbesar
   - `creep_mg` — dihitung dari perintah terpisah (lihat 2.4)
   - `repeatability_mg` — SD antar pengulangan pada nominal yang sama
5. `report_text(metrics)` → ringkasan teks siap ditempel ke laporan magang,
   Bahasa Indonesia formal, satu paragraf + tabel residual per titik.

Tampilkan semua angka ini di `index.html` dalam satu panel "Kalibrasi", dengan
tabel per titik (nominal mg, arah, raw, residual mg) dan tombol download CSV.

### 2.4 Uji creep

Tambahkan perintah firmware `k` yang: mencetak baris `CREEP,<millis>,<raw_mean>`
setiap 10 detik selama 30 menit, lalu berhenti sendiri. Di PC, simpan ke
`tuning_web/data/creep_<timestamp>.csv` dan hitung
`creep_mg = (raw_akhir - raw_awal) / counts_per_mg`, ditampilkan sebagai grafik
raw vs waktu.

---

## TUGAS 3 — Diagnostik

### 3.1 Statistik noise (`d`)

Perintah `d` mengumpulkan **200 sampel raw berturut-turut** (20 detik @10 SPS),
lalu mencetak:

```
# Noise (200 sampel raw, tanpa filter):
#   SD          = 1.84 count = 0.368 mg
#   peak-to-peak= 9 count    = 1.800 mg
#   mean        = 124.31 count
#   spike gate  = 0 sampel ditolak
#   catatan     : mg dihitung dari countsPerGram = 5010.79 (TERKALIBRASI)
```

Kalau belum terkalibrasi, kolom mg harus ditulis `--- (UNCALIBRATED)`, **bukan**
angka dari skala default.

### 3.2 Panel RTD (`x`)

Perintah `x` mencetak:

```
# Diagnostik MAX31865:
#   register RTD (raw)  = 0x6E23   (MSB 0x6E, LSB 0x23)
#   bit D0 (fault flag) = 0
#   nilai 15-bit        = 14097 / 32768
#   resistansi          = 185.00 ohm   (RREF = 430.0)
#   suhu                = 224.31 C
#   config register     = 0xD1
#     D7 VBIAS            = 1
#     D6 conversion mode  = 1 (auto)
#     D5 1-shot           = 0
#     D4 3-wire           = 1
#     D3:D2 fault detect  = 00
#     D1 fault clear      = 0
#     D0 filter           = 1 (50 Hz)
#   fault register      = 0x00
#     D7 RTD High Threshold (RTD/kabel open)        = 0
#     D6 RTD Low Threshold (RTD short)              = 0
#     D5 REFIN- > 0.85 x VBIAS                      = 0
#     D4 REFIN- < 0.85 x VBIAS (FORCE- open)        = 0
#     D3 RTDIN- < 0.85 x VBIAS (FORCE- open)        = 0
#     D2 Over/under voltage                         = 0
#   ambang fault        : low = 18.52 ohm, high = 390.48 ohm
#   initFault (boot)    = 0x00
```

Tampilkan **setiap bit secara terpisah dengan namanya**, termasuk yang bernilai 0
— justru nilai 0 pada D4/D3 yang sering disalahartikan sebagai "aman".

### 3.3 Fault-detection cycle on demand (PENTING)

Ini temuan kunci: bit D5/D4/D3 (deteksi FORCE- open) **hanya di-set oleh
automatic fault-detection cycle**, yang di firmware sekarang cuma dijalankan
sekali saat `RtdSensor::begin()`. Selama running, `readFault(MAX31865_FAULT_NONE)`
cuma membaca register yang sudah latched. Jadi `fault 0x00` di log berjalan
**tidak membuktikan wiring benar**.

Tambahkan:
- Method `uint8_t RtdSensor::runFaultDetectionCycle()` yang menjalankan
  `max_.readFault(MAX31865_FAULT_AUTO)`, menunggu bit D3:D2 config kembali `00`
  (timeout `kFaultCycleTimeoutMs`), membaca hasilnya, lalu **memulihkan
  auto-convert + bias** seperti semula (cycle mematikan auto-conversion).
- Perintah `x` menjalankan cycle ini dulu, baru mencetak panelnya, dan panelnya
  menampilkan baris terpisah `fault (hasil cycle baru) = 0x..`.
- Tambahkan peringatan di output kalau hasil cycle ≠ 0.

### 3.4 Plausibility warning harus selalu tampil

Di `src/main.cpp`, `checkRtdPlausibility()` sekarang hanya dipanggil kalau
`rtd.valid()`, dan `valid()` mensyaratkan `fault_ == 0`. Panggil tanpa syarat
selama `rtd.spiOk()`, supaya nilai di luar 80–200 Ω selalu memicu WARNING.
Tambahkan juga pemanggilan berkala (1×/menit) di `loop()`, bukan hanya saat boot.

---

## TUGAS 4 — Kompensasi suhu untuk zero drift (opsional, kerjakan terakhir)

Buat opsional di balik `#define ENABLE_TEMP_COMPENSATION 0` di `app_config.h`,
default **mati**.

Kalau diaktifkan:
- Firmware mencetak baris `TCPT,<millis>,<raw_mean>,<rtd_tempC>` setiap 30 detik.
- PC menyimpan ke `tuning_web/data/tempcomp_<timestamp>.csv`.
- `analysis.py` memfit `raw_zero = a + b * T` dan melaporkan `b` (counts/°C),
  R², dan berapa mg drift per °C.
- **Jangan terapkan koreksinya ke pembacaan sebelum R² > 0,8 pada data nyata.**
  Kalau diterapkan, koefisiennya disimpan di NVS (key `tc_a`, `tc_b`) dan
  dipakai di `grams()`. Tampilkan status "TEMPCOMP ON/OFF" di baris report.

Catatan: ini hanya berguna kalau PT100 merasakan suhu **badan load cell**, bukan
suhu chamber. Kalau load cell ada di luar chamber, tandai di kode bahwa fitur ini
butuh sensor suhu kedua di dekat load cell.

---

## TUGAS 5 — Test

Project sudah punya `[env:native]` dengan Unity. Tambahkan unit test untuk:
- `Ads1232::toSigned24()`: `0x7FFFFF → 8388607`, `0x000001 → 1`,
  `0xFFFFFF → -1`, `0x800000 → -8388608`, `0x000000 → 0`.
- Spike gate: deret dengan satu outlier 100σ harus menghasilkan output yang
  mendekati mean deret tanpa outlier.
- `fit_linear` di Python: data sintetis dengan slope diketahui harus
  menghasilkan slope yang sama dalam 1e-6, dan residual nol.
- `metrics()`: hysteresis dan max residual pada data sintetis yang sudah
  diketahui jawabannya.

Jalankan `pio test -e native` dan pastikan lulus sebelum selesai.

---

## Urutan pengerjaan yang diminta

1. Tugas 1.3 (bug `COUNTS_PER_MG`) — ini bug nyata yang merusak semua laporan mg.
2. Tugas 3.2 + 3.3 (panel RTD + fault cycle on demand) — dibutuhkan untuk
   mendiagnosa masalah PT100 yang sedang berjalan.
3. Tugas 1.1, 1.2, 1.4, 1.5, 1.6 (akurasi load cell).
4. Tugas 2 (kalibrasi multi-titik).
5. Tugas 3.1, 3.4.
6. Tugas 1.7 (rata-rata RTD).
7. Tugas 5 (test).
8. Tugas 4 hanya kalau sisa waktu.

Setelah tiap tugas, jalankan `pio run` dan pastikan compile bersih tanpa warning
baru. Jangan gabung semua perubahan jadi satu commit besar — satu commit per
nomor tugas, dengan pesan commit Bahasa Indonesia yang menyebut nomor tugasnya.
