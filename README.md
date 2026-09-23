# Heat Box — Firmware Riset Breadboard

Firmware ESP32 DevKit V1 (ESP32-WROOM-32, 30 pin) untuk mesin **Heat Box**, alat ukur
kadar air tepung telur.

**Tujuan tahap ini hanya satu:** semua sensor terbaca benar dan stabil di Serial Monitor.
Belum ada LCD, heater, atau kontrol proses.

| Sensor | Fungsi | Library |
|---|---|---|
| MAX31865 + PT100 3-wire | suhu chamber | `lib/RtdSensor` (di atas Adafruit MAX31865) |
| ADS1232 + load cell 300 g | berat sampel | `lib/Ads1232` (driver sendiri) |
| Rotary encoder EC11 | input UI (prioritas rendah) | `lib/EncoderInput` |

---

## Build & upload

```bash
pio run                     # build (default_envs = esp32doit-devkit-v1)
pio run -t upload           # flash
pio device monitor          # serial 115200
```

Platform **dikunci di `espressif32@6.5.0`** (Arduino core 2.0.14). Library composite video
untuk fase berikutnya tidak kompatibel dengan core 3.x, jadi versi ini jangan dinaikkan.

> Catatan tooling: `pio run -v` (verbose) di PlatformIO Core 6.2.0 gagal di langkah
> `firmware.bin` dengan `TypeError: '_Null' and 'str'`. Ini bug di tampilan verbose,
> bukan di kode. Build tanpa `-v` berjalan normal.

---

## Struktur folder

```
Heatbox/
├── platformio.ini
├── README.md                  <- file ini
├── include/
│   ├── pins.h                 <- pin map + konstanta hardware RTD
│   └── app_config.h           <- konstanta aplikasi (report, serial, plausibility)
├── lib/
│   ├── Ads1232/               <- driver load cell (bit-bang, tulis sendiri)
│   │   ├── library.json
│   │   └── src/Ads1232.h, Ads1232.cpp
│   ├── RtdSensor/             <- wrapper MAX31865 non-blocking
│   │   ├── library.json
│   │   └── src/RtdSensor.h, RtdSensor.cpp
│   ├── EncoderInput/          <- encoder EC11 polling
│   │   ├── library.json
│   │   └── src/EncoderInput.h, EncoderInput.cpp
│   └── Heatbox/               <- (lama) algoritma pengeringan fase berikutnya, tidak dipakai
├── src/
│   ├── main.cpp               <- setup, loop, report serial, command serial
│   └── main.md                <- (lama) draft main berbasis lib Heatbox, tidak dikompilasi
└── test/                      <- (lama) unit test native untuk lib Heatbox
```

### Kenapa dipecah ke `lib/` dan `include/`

- **`lib/` berisi driver yang berdiri sendiri.** Tiap driver adalah library PlatformIO
  (punya `library.json`) dan **tidak membaca `pins.h`**. Pin diberikan lewat konstruktor
  dari `main.cpp`, jadi driver bisa dipindah ke board atau project lain tanpa diedit.
- **`include/` berisi konfigurasi milik project**: pin map dan konstanta aplikasi.
  Mengganti wiring atau RREF cukup di satu tempat.
- **`src/main.cpp` hanya berisi perekat**: membuat objek, mencetak report, dan
  menerjemahkan command serial.
- PlatformIO (LDF) hanya meng-compile library yang di-`#include`. Karena itu
  `lib/Heatbox` tidak ikut ter-build walaupun foldernya ada.

Semua kelas memakai pola yang sama: `begin()` sekali di `setup()`, `update()`
non-blocking di tiap `loop()`, lalu data dibaca lewat getter. Di `loop()` **tidak ada
`delay()`**; semua timing memakai `millis()`/`micros()`.

---

## Penjelasan tiap file

### `platformio.ini`

- `[platformio] default_envs` membuat `pio run` hanya mem-build firmware ESP32.
- `[env:esp32doit-devkit-v1]` persis sesuai spesifikasi: platform 6.5.0,
  `-DCORE_DEBUG_LEVEL=0`, dan `lib_deps` Adafruit MAX31865 `^1.6.0` (saat ini
  terpasang 1.6.2 + Adafruit BusIO 1.17.4).
- `[env:native]` adalah env unit test lama untuk `lib/Heatbox`, tidak diubah.

### `include/pins.h`

Satu-satunya tempat alokasi pin.

| Perangkat | Pin |
|---|---|
| MAX31865 (VSPI, hardware SPI) | SCK=18, MISO=19, MOSI=23, **CS=17** |
| ADS1232 | SCLK=4, DOUT/DRDY=16, PDWN=13 |
| Encoder EC11 | A=32, B=33, SW=27 |

Isi lain:
- `RTD_RREF = 430.0f` dan `RTD_RNOMINAL = 100.0f`. **Kalau board memakai resistor
  referensi 400R, cukup ganti `RTD_RREF` di sini.**
- `RTD_WIRES = MAX31865_3WIRE` supaya mode 2/3/4-wire bisa dicoba tanpa mengubah kode lain.
  Di library Adafruit, nilai `2WIRE` dan `4WIRE` sama; bedanya hanya di jumper board.
- **Guard compile-time**: `static_assert` membuat build gagal kalau ada pin yang
  jatuh ke daftar terlarang (0, 1, 2, 3, 5, 6–12, 14, 15, 21, 22, 25, 26, 34–39).

### `include/app_config.h`

Konstanta level aplikasi dalam namespace `app`:
- versi firmware, baud rate, ukuran TX buffer UART (1024 byte, supaya print panjang
  tidak memblok loop)
- periode report 500 ms, ukuran buffer command
- **plausibility check RTD saat boot**: 80–200 Ω dan −50..250 °C
- ambang cek jumper SPEED ADS1232 dari waktu DRDY pertama (200 ms)

Konstanta milik driver (timing datasheet, filter, ambang) ada di header driver masing-masing.

### `lib/Ads1232/src/Ads1232.h` + `Ads1232.cpp` — load cell

Driver bit-bang yang ditulis sendiri berdasarkan **TI SBAS350H (Juni 2025)**, tanpa
library pihak ketiga dan tanpa protokol HX711. Asumsi modul: GAIN=128, SPEED=10 SPS,
A0=GND (AIN1).

**Protokol** (rujukan section ada di komentar `Ads1232.cpp`):

| Aksi | Pulsa SCLK | Sumber |
|---|---|---|
| Baca data | 24 bit, MSB dulu, dibaca setelah falling edge | 7.3.12, Tabel 7-8 (t4 ≤ 50 ns) |
| Paksa DRDY/DOUT HIGH | +1 (total **25**) | 7.3.10, Gambar 7-10 |
| Offset self-calibration | +2 (total **26**); mulai di falling edge ke-26 | 7.4.1, Gambar 7-11 |
| Pilih channel | bukan lewat SCLK, tetapi pin A0 (jumper) | Tabel 7-1 |

- **Data ready** dideteksi dari DOUT yang turun LOW, di-poll di `update()` tanpa delay.
- **Konversi 24-bit ke `int32_t`**: `(code & 0x7FFFFF) - (code & 0x800000)`. Hasilnya
  sama dengan sign-extend bit 23, tanpa cast unsigned→signed yang implementation-defined.
- **Critical section** (`portMUX_TYPE` + `portENTER_CRITICAL`) membungkus shift-out.
  Estimasi worst case 26 pulsa ≈ 0,15 ms. Batas terketat yang relevan adalah SCLK HIGH
  ≥ 12,46 ms yang memicu standby (t10, SPEED=1), jadi masih jauh. Perhitungan lengkap
  ada di komentar `shiftOut()`. Durasi nyata diukur tiap pembacaan (perintah `p`).
- **Power-up sequence** 7.4.5: PDWN LOW (t15) → HIGH (t16) → LOW (t17) → HIGH.
  `reset()` = PDWN LOW 100 µs (min 26 µs) lalu HIGH, non-blocking.
  **4 konversi pertama dibuang** setelah reset (worst case 7.3.7). Konversi pertama
  setelah self-calibration tidak dibuang (7.4.1).
- **Deteksi fault protokol**: setelah pulsa ke-25, DOUT *wajib* HIGH. Kalau masih LOW,
  kemungkinan SCLK tidak diterima chip (wiring atau level logic) → status `DOUTERR`.
- **Filter**: gerbang spike 6σ (median-of-5 sebagai pengganti sampel pencilan) →
  moving average 16. Median **tidak** dipakai sebagai filter permanen karena median
  sliding membuat keluaran berkorelasi sehingga rata-rata 16 sampel tidak memberi
  pengurangan noise √16.
- **Deteksi stabil**: `isStable()` true jika **rentang pergerakan angka yang tampil**
  (maks−min keluaran moving average) selama `kStableWindow` = 32 sampel (3,2 s) di bawah
  `kStableSpanMg` = 5 mg. Sebaran satu sampel mentah sengaja tidak dipakai: besarnya
  tetap ~21 count baik saat diam maupun bergerak, jadi tidak bisa membedakan keduanya.
- **Tare / kalibrasi** berjalan di background: mengumpulkan 64 sampel raw (≈6,4 s), lalu
  hasilnya dilaporkan lewat `takeEvent()`.
- **NVS** (Preferences, namespace `heatbox`, key `ads_offset_d` dan `ads_cpg`) otomatis
  di-load di `begin()`. Offset disimpan `double` (1 count ≈ 0,18 mg, pembulatan merugikan).
  Jika scale belum pernah dikalibrasi → status **UNCALIBRATED**, dan scale default
  diperkirakan dari asumsi load cell 1 mV/V — pada hardware ini sensitivitas
  sebenarnya 0,7682 mV/V, sehingga default meleset 23 %.
- **Diagnostik**: waktu DRDY pertama setelah PDWN (≈402 ms @10 SPS, sekaligus cek jumper
  SPEED), interval DRDY, durasi self-cal (≈801 ms), durasi shift-out, dan jumlah protocol error.

### `lib/RtdSensor/src/RtdSensor.h` + `RtdSensor.cpp` — suhu

Wrapper `Adafruit_MAX31865` dengan konfigurasi `RREF`/`RNOMINAL`/wire-mode dari `pins.h`.

- **Non-blocking**: `readRTD()` milik Adafruit berisi `delay(10)` + `delay(65)`, dan
  pembaca registernya private. Karena itu chip dijalankan dalam **continuous mode**
  (bias + auto-conversion ON, filter 50 Hz), lalu register RTD dibaca langsung lewat
  `Adafruit_SPIDevice` (1 MHz, SPI_MODE1, sama dengan library) setiap 100 ms.
  Konsekuensinya bias selalu menyala, sehingga ada sedikit self-heating dibanding one-shot.
- **Health check `begin()`**: loopback SPI (tulis register threshold lalu baca balik),
  cek register config (`0xD1` untuk 3-wire), satu kali automatic fault-detection cycle,
  lalu tunggu sampel pertama.
- **Ambang fault hardware** dipasang di batas PT100 IEC 60751 (18,52–390,48 Ω), jadi
  hanya RTD open/short yang memicu fault. Nilai yang sekadar tidak wajar (RREF atau
  jumper salah) tetap terbaca dan ditangkap oleh plausibility check di `main.cpp`.
- **Fault register** dibaca setiap sampel dengan `readFault(MAX31865_FAULT_NONE)`.
  Argumen default library (`AUTO`) jangan dipakai saat berjalan, karena menulis ulang
  config dan mematikan auto-conversion. Fault di-decode ke teks oleh `printFault()`:
  RTD HIGH/LOW threshold, REFIN- > 0,85×VBIAS, REFIN- < 0,85×VBIAS (FORCE- open),
  RTDIN- < 0,85×VBIAS (FORCE- open), Under/Over voltage. Register ini tidak punya bit
  "REFIN+"; D5 dan D4 sama-sama mengacu ke REFIN-.
- **Auto clear**: `clearFault()` dipanggil setelah laporan diambil lewat `takeFaultReport()`.
  Fault yang sama dilaporkan ulang paling cepat tiap 5 detik, dan hilangnya fault juga dilaporkan.

### `lib/EncoderInput/src/EncoderInput.h` + `EncoderInput.cpp` — encoder

- **Polling, tanpa `attachInterrupt`**, karena nanti berjalan bersama library video yang
  sensitif timing. Pin A/B/SW memakai `INPUT_PULLUP`.
- **Quadrature 4x**: lookup table 16 entri berindeks `(state_lama << 2) | state_baru`.
  Transisi ilegal (dua bit berubah sekaligus) diabaikan.
- Posisi dihitung dalam **detent** (4 transisi per detent untuk EC11 20/20), dengan
  resync di posisi diam A=B=1 supaya transisi yang terlewat tidak menggeser fase permanen.
  `delta()` = perubahan posisi pada `update()` terakhir.
- **Tombol**: debounce 20 ms, short press dikirim saat tombol dilepas, long press
  (> 800 ms) dikirim saat tombol masih ditahan.

### `src/main.cpp`

- Banner saat boot: versi firmware, tanggal build, seluruh pin map, konfigurasi RTD/ADS.
- **Health check** OK/FAIL untuk MAX31865 dan ADS1232, resistansi RTD mentah,
  plausibility check, dan status kalibrasi. Jika ADS1232 tidak menurunkan DRDY dalam
  2 detik, dicetak peringatan eksplisit tentang wiring dan **level logic**
  (VIH ADS1232 = 0,7×DVDD).
- Report tiap 500 ms dalam mode human readable atau CSV.
- Command serial dibaca per karakter ke buffer (non-blocking).
- Semua pesan non-data diawali `# `, jadi log CSV cukup difilter dengan `grep -v '^#'`.

### File lama (tidak diubah)

- `lib/Heatbox/` berisi algoritma pengeringan (regresi slope, fitting kurva, logika
  berhenti) untuk fase kontrol proses. Tidak di-include, jadi tidak ikut ter-build.
- `test/` berisi unit test native untuk `lib/Heatbox` (`pio test -e native`). Menurut
  `test/README.md`, suite ini baru bisa jalan setelah `HeatboxConfig.h` dan
  `HeatboxMath.h` tidak lagi meng-include `<Arduino.h>`.
- `src/main.md` berisi draft `main` lama dalam bentuk komentar. File `.md` tidak dikompilasi.

---

## Output serial

### Mode human readable (default)

```
[ 12.5s] RTD 27.41 C (R=110.62, fault 0x00) | W 14.238 g STABLE (raw 452331) | ENC 0 btn:-
```

| Status berat | Arti |
|---|---|
| `STABLE` | angka yang tampil bergerak < 5 mg selama 32 sampel (3,2 s) |
| `MOVING` | belum stabil |
| `SETTLE` | setelah reset/self-cal, atau menunggu data pertama |
| `BUSY` | tare/kalibrasi sedang mengumpulkan sampel |
| `CLIP` | raw di kode full-scale (±8388607): overload atau wiring load cell salah |
| `DOUTERR` | DOUT tidak kembali HIGH setelah pulsa ke-25 |
| `NODATA` | tidak ada DRDY selama > 2 s |
| akhiran `UNCAL` | scale belum dikalibrasi |

`btn:` bernilai `-` (idle), `P` (ditekan), `S` (short press), atau `L` (long press).

### Mode CSV (perintah `v`)

Header dicetak sekali saat mode diaktifkan:

```
millis,tempC,rtdOhm,rtdFault,rawCounts,grams,stable,encPos
12500,27.41,110.62,0,452331,14.238,1,0
```

### Command (akhiri dengan Enter)

| Cmd | Fungsi |
|---|---|
| `t` | tare (64 sampel, ≈6,4 s) |
| `c <gram>` | kalibrasi 1 titik dengan anak timbangan, contoh `c 10` |
| `n` | rekam titik nol → baris `CALZERO` |
| `m <mg> <u\|d>` | rekam titik kalibrasi → baris `CALPT`; `u` naik, `d` turun |
| `s <cpg>` | terapkan countsPerGram hasil fit dari PC |
| `z` | offset self-calibration ADS1232 |
| `r` | reset ADS1232 (PDWN cycle) |
| `v` | ganti mode human ↔ CSV |
| `e` | reset posisi encoder |
| `p` | cetak kalibrasi tersimpan + diagnostik ADS1232 |
| `?` | bantuan |

## Empat hal berbeda yang sering tertukar

Hanya dua di antaranya yang benar-benar "kalibrasi".

| Perintah | Tingkat | Yang diperbaiki | Perlu anak timbangan? |
|---|---|---|---|
| `r` | cip | **Bukan kalibrasi.** Reset perangkat keras ADS1232 lewat siklus PDWN, lalu buang 4 konversi. Dipakai kalau cip macet atau muncul `DOUTERR`. | tidak |
| `z` | cip | Offset internal ADC + PGA. Cip mengukur offset-nya sendiri lalu menguranginya (≈801 ms). Cip **tidak tahu apa-apa** soal load cell Anda. | tidak |
| `t` | aplikasi | Titik **nol**: raw saat ini dicatat sebagai acuan kosong. Memperhitungkan berat wadah dan sisa offset. | tidak |
| `c` / `m`+`s` | aplikasi | **Skala**: berapa count per gram. Inilah yang memperbaiki "10 g terbaca 8 g". | ya |

Ketergantungannya berurutan, dan inilah sebabnya urutan tidak boleh dibalik:

```
z  mengubah raw       ->  maka t harus diulang
t  menentukan offset  ->  maka skala harus dihitung SETELAH t
r  mereset cip        ->  maka z dan t diulang (lihat catatan)
```

Catatan: datasheet SBAS350H tidak menyatakan apakah PDWN menghapus hasil *offset
self-calibration*, sehingga mengulang `z` setelah `r` adalah sikap konservatif, bukan
keharusan yang terbukti. Lihat tabel TODO VERIFY di bawah.

### Urutan kalibrasi load cell

**Cepat — satu titik** (cukup untuk pemakaian biasa):

1. Nyalakan board, tunggu pesan `ADS1232 settling selesai`.
2. `z`, tunggu `Self-calibration selesai` (≈801 ms).
3. Kosongkan timbangan, tunggu `STABLE`, lalu `t` (≈6,4 s).
4. Pasang anak timbangan, tunggu `STABLE`, lalu `c <gram>`.
5. `p` untuk memeriksa nilai yang tersimpan.

Kerjakan langkah 3 dan 4 **berdekatan**. Drift nol 14,2 mg/menit berarti jeda satu
menit antara tare dan kalibrasi sudah menyuntikkan kesalahan ~0,14 % pada beban 10 g.

**Teliti — multi-titik** (untuk angka linearitas dan histeresis di laporan):

Jalankan dari panel *Kalibrasi multi-titik* di `tuning_web`, yang mengirim `n` dan
`m` untuk Anda dan menghitung fit-nya. Langkah `z` dan `t` tetap dilakukan lebih
dulu. Prosedur lengkap dan alasan tiap langkah ada di
[tuning_web/README.md](tuning_web/README.md) dan
[SPESIFIKASI-TEKNIS.md](SPESIFIKASI-TEKNIS.md) §7.

Bedanya dengan `c`: multi-titik mengoreksi drift memakai nol sebelum **dan** sesudah
tiap beban, memakai banyak titik sehingga linearitas terukur, dan menghasilkan
residual, histeresis, serta repeatability. `c` hanya memberi satu angka skala.

---

## Checklist verifikasi di hardware

1. **RREF board** 430R atau 400R (cek sablon). Bandingkan `R=` saat boot dengan suhu ruang
   (PT100 @25 °C ≈ 109,7 Ω).
2. **Jumper 3-wire** di board MAX31865 sudah disolder sesuai mode.
3. **DVDD modul ADS1232 = 3,3 V** (VIH = 0,7×DVDD). Kalau DVDD 5 V: SCLK/PDWN dari ESP32
   tidak terbaca dan DOUT 5 V berisiko merusak GPIO16.
4. **Jumper SPEED**: DRDY pertama saat boot ≈ 402 ms, dan `p` menunjukkan interval ≈ 100 ms
   (bukan ≈ 53 ms / 12,5 ms, yang berarti 80 SPS).
5. **GAIN0/GAIN1 = HIGH, A0 = LOW, TEMP = LOW**, dan REFP tersambung ke eksitasi bridge
   (rasiometrik).
6. **Protokol 26 pulsa**: durasi `z` sekitar 801 ms. Kalau hanya ≈100 ms, kalibrasi tidak terpicu.
7. **Durasi shift-out** (`p`) di bawah 1000 µs.
8. **Ambang stabil**: sudah diukur di hardware ini — noise raw 20,9 count RMS (48,7 nV),
   dan angka yang tampil bergerak ~2,6 mg saat benar-benar diam. `kStableSpanMg` = 5 mg
   karena itu tercapai dengan margin. Ukur ulang dengan `p` kalau pengawatan berubah.
9. **Encoder**: arah putar (tukar A/B kalau terbalik), posisi diam = A=B=1, dan
   4 transisi per detent.
10. **Pull-up**: pull-up internal ESP32 ≈ 45 kΩ; tambahkan 10 kΩ eksternal kalau encoder
    atau kabel panjang terlalu berisik.

### TODO VERIFY (detail yang belum terverifikasi 100%)

| Lokasi | Pertanyaan | Referensi |
|---|---|---|
| `Ads1232.cpp`, `reset()` | Apakah data pertama setelah PDWN sudah fully settled? Kalau ya, `kDiscardAfterReset` bisa 0. | SBAS350H 7.4.4, Gambar 7-14, Tabel 7-12 (tidak eksplisit) |
| `Ads1232.cpp`, `reset()` | Apakah PDWN menghapus hasil offset calibration, dan apakah ada auto-cal saat power-up? | SBAS350H 7.4.1, 7.4.4 (tidak disebut) |
| `RtdSensor.h`, `kStartupSettleMs` | Waktu start-up VBIAS dan waktu konversi continuous 50 Hz | Datasheet MAX31865, Electrical Characteristics |
| `RtdSensor.h`, `kFaultCycleTimeoutMs` | Durasi automatic fault-detection cycle | Datasheet MAX31865, register Configuration D3:D2 |
| `RtdSensor.cpp`, `ohmToThresholdRegister()` | Format dan arah pembandingan register High/Low Fault Threshold | Datasheet MAX31865, Fault Threshold registers |
| `RtdSensor.cpp`, `kFaultTexts` | Teks bit D7..D2 Fault Status Register | Datasheet MAX31865, Fault Status register |

Item MAX31865 masih TODO karena datasheet-nya belum bisa diunduh saat kode ditulis.
Isinya saat ini mengikuti source dan contoh resmi library Adafruit.

---

## Referensi

- TI **SBAS350H** — *ADS1232, ADS1234 24-Bit ADC for Bridge Sensors* (rev. Juni 2025)
- Analog Devices / Maxim — *MAX31865 RTD-to-Digital Converter* datasheet
- Adafruit MAX31865 library 1.6.2, Adafruit BusIO 1.17.4
- Arduino-ESP32 core 2.0.14 (`framework-arduinoespressif32@3.20014.231204`)
