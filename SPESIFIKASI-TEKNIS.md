# Spesifikasi Teknis — Heat Box

**Tahap:** breadboard · **Pengukuran:** 23 September 2026 · **Diperbarui:** 5 Oktober 2026

Angka di dokumen ini hasil pengukuran, kecuali diberi rujukan datasheet. Rancangan siklus,
pengaturan, dan tampilan ada di [RANCANGAN-SIKLUS-PENGERINGAN.md](RANCANGAN-SIKLUS-PENGERINGAN.md).

---

## 1. Perangkat keras

| Bagian | Spesifikasi |
|---|---|
| Pemroses | ESP32-WROOM-32 (DevKit V1, 30 pin), 240 MHz, 320 KB RAM, 4 MB flash; USB CH340, UART0 115200 baud. Cadangan: ESP32-S3 (tanpa DAC, jadi composite video tidak bisa) |
| Load cell | 300 g, jembatan penuh, sensitivitas **terukur 0,7682 mV/V** (§5.2) |
| ADC berat | TI ADS1232 24 bit ΔΣ, gain 128, 10 SPS, kanal AIN1, referensi rasiometrik (semua lewat jumper) |
| Sensor suhu | PT100 3 kawat + MAX31865 (VSPI), filter 50 Hz, mode kontinu. Resistor referensi di board berkode 431 (430 Ω); multimeter membaca 423,12 Ω, ditambah offset −1 °C ke sensor pembanding ruang (nilai sebenarnya dipastikan lewat titik es, §8.1) |
| Input | Rotary encoder EC11 + tombol (polling) |
| Rencana | Lampu halogen + SSR, limit switch tutup, tombol Start industri 22 mm, LCD composite |

---

## 2. Pin

| Fungsi | GPIO |
|---|---|
| MAX31865 SCK / MISO / MOSI / CS | 18 / 19 / 23 / 17 |
| ADS1232 SCLK / DOUT-DRDY / PDWN | 4 / 16 / 13 |
| Encoder A / B / SW | 32 / 33 / 27 |
| *Rencana:* composite video / SSR / saklar tutup / tombol Start | 25 / 22 / 34 / 35 |

- **Dilarang:** strapping (0, 2, 5, 12, 15), UART0 (1, 3), flash (6–11). Larangan ini
  ditegakkan `static_assert` di `include/pins.h`.
- **Cadangan:** 14, 21, 22, 25, 26, 34–39.
- CS MAX31865 sengaja bukan GPIO5 (strapping).

---

## 3. Perangkat lunak

| | |
|---|---|
| Kerangka | PlatformIO `espressif32@6.5.0`, Arduino core 2.0.14. **Dikunci**: library composite video tidak kompatibel dengan core 3.x |
| Pemakaian (5 Okt 2026) | RAM 23 996 B (7,3 %), Flash 313 185 B (23,9 %) |

```
include/pins.h, app_config.h   pin, konstanta hardware dan aplikasi
lib/Ads1232/                   driver bit-bang ADS1232
lib/RtdSensor/                 pembungkus MAX31865 non-blocking
lib/EncoderInput/              encoder EC11
lib/HeatboxCycle/              siklus di firmware: matematika, nilai per detik/per menit
lib/Heatbox/                   versi lama, tidak dipakai, akan diganti HeatboxCycle
src/main.cpp                   loop, report serial, perintah serial
src/HardwareSensors.*          sumber data sensor hardware untuk siklus
tuning_web/                    dasbor PC (Python stdlib + pyserial)
```

---

## 4. Firmware sekarang

### 4.1 Protokol ADS1232 (TI SBAS350H)

| Simbol | Besaran | Nilai |
|---|---|---|
| t3 | Lebar pulsa SCLK minimum | 100 ns (dipakai 1 µs) |
| t7 | Waktu konversi, 10 SPS | 100 ms |
| t8 | Data pertama setelah self-calibration | 801 ms |
| t10 | SCLK HIGH memicu standby | 99,96 ms |
| t11 | DRDY pertama setelah wake-up | 401,8 ms |
| t14 | Pulsa PDWN minimum | 26 µs |

- **Pulsa ke-25** memaksa DOUT HIGH; tanpa pulsa ini DOUT bisa terbaca sebagai *data ready*
  palsu.
- **Pulsa ke-26** memulai offset self-calibration.
- Shift-out berjalan dalam critical section ±150 µs, diukur tiap baca (perintah `p`).
- Kanal dipilih lewat pin A0, bukan SCLK.
- **Galat yang dideteksi:** `NoDrdy` (DRDY tidak turun dalam 2 detik), `DoutStuckLow`
  (DOUT tetap LOW setelah pulsa ke-25; biasanya level logika: VIH = 0,7 × DVDD, jadi DVDD
  harus 3,3 V).

### 4.2 Pengolahan berat di firmware

```
raw 24 bit → gerbang spike 6σ (pengganti: median-of-5) → rata-rata adaptif s.d. 128 sampel → gram
```

- **Rata-rata adaptif:** jendelanya tumbuh 1 sampel per konversi sampai 128 (tenang). Saat
  beban berubah > 6σ, rata-rata dimulai ulang dari sampel itu, jadi angka mengikuti beban
  baru dalam ±0,5 detik.
- **σ "tenang"** untuk deteksi perubahan hanya diperbarui kalau 64 sampel terakhir bebas
  step dan spike. Perbaikan 5 Okt 2026: sebelumnya σ ini membengkak setelah beban berubah,
  sehingga perubahan berikutnya tidak terdeteksi dan angka meluruh pelan sampai ±10 detik.
- **STABLE:** rentang keluaran rata-rata 16 sampel selama 32 sampel (3,2 detik) < 5 mg.
  Keluar dari STABLE butuh 1,5 × ambang.
- **Tare otomatis saat nyala:** hanya kalau beda dari offset tersimpan < 30 g. Pesan
  "SUDAH SETIMBANG" muncul sekali saat drift < 3 mg/menit.
- **NVS** (`Preferences`, namespace `heatbox`): `ads_offset_d` (double, count) dan `ads_cpg`
  (float, count/gram).
- Siklus baru tidak memakai angka `W` ini; siklus memakai rata-rata per detik dari sampel
  lolos gerbang (rancangan §5).

### 4.3 Suhu

- MAX31865 kontinu, register RTD dibaca langsung tiap 100 ms (non-blocking).
- Ambang fault di batas PT100 (18,52–390,48 Ω).
- Cek kewajaran saat boot dan tiap 60 detik (80–200 Ω, −50..250 °C).
- Peringatan saturasi saat rasio ADC > 0,95.

---

## 5. Karakterisasi terukur (23 September 2026)

Kondisi: breadboard, suhu ruang, tanpa pelindung, load cell belum terpasang kaku.

### 5.1 Skala (anak timbangan 10 g)

| Besaran | Nilai |
|---|---|
| Raw tanpa beban / dengan 10 g | 112 740,4 / 167 730,2 count |
| **Faktor skala** | **5499,0 count/gram** |
| Satu count | 0,1819 mg |

### 5.2 Turunan

| Besaran | Nilai |
|---|---|
| Skala penuh ADC (VREF 5 V, gain 128) | ±19,531 mV; 1 LSB = 2,328 nV |
| Keluaran pada 300 g | 3,841 mV → **0,7682 mV/V** |
| Pemakaian rentang ADC | 19,7 % |

Bawaan firmware mengasumsikan 1,000 mV/V. Itulah sebab bacaan meleset 23 % sebelum
kalibrasi.

### 5.3 Derau (241 sampel, 120 detik, drift linear dibuang)

| Besaran | Nilai |
|---|---|
| RMS | 20,9 count = **3,80 mg** (48,7 nV) |
| Puncak-ke-puncak | 121 count = 22,0 mg (5,8σ, sesuai Gaussian) |
| Resolusi efektif RMS / bebas derau | 1/78 933 (16,27 bit) / 1/13 634 (13,73 bit) |

SD 64 sampel di CSV kalibrasi sesudahnya: 17,7–33,3 count (3,2–6,1 mg).

### 5.4 Kestabilan nol

| Besaran | Nilai |
|---|---|
| Drift setelah nyala | +14,2 mg/menit (+0,85 g/jam) |
| Setelah ±2–4 jam | menetap di sekitar +0,3 g |
| Histeresis setelah 10 g diangkat | 27,9 mg (0,28 % beban) |
| Creep sesaat setelah 500 mg diletakkan | +30 mg/menit |

Drift adalah sumber ketidakpastian terbesar.

---

## 6. Kalibrasi load cell

| Perintah | Yang diperbaiki | Perlu anak timbangan |
|---|---|---|
| `r` | **Bukan kalibrasi:** reset ADS1232 (PDWN), buang 4 konversi | tidak |
| `z` | Offset internal ADC (≈ 801 ms); cip tidak tahu soal load cell | tidak |
| `t` | Titik nol (64 sampel ≈ 6,4 detik) | tidak |
| `c <gram>` atau `n` / `m` + `s` | **Skala** count/gram | ya |

Urutannya tidak boleh dibalik: `z` mengubah raw (ulangi `t`), dan skala dihitung setelah `t`.

**Cepat (1 titik):** tunggu "ADS1232 settling selesai" → `z` → kosongkan, tunggu STABLE,
`t` → pasang anak timbangan terbesar, tunggu STABLE, `c <gram>` → cek dengan `p`. Kerjakan
`t` dan `c` berdekatan: drift 14 mg/menit menambah error ~0,14 % per menit jeda pada beban
10 g.

**Teliti (multi-titik, dari dasbor):** nol → beban → nol lagi untuk tiap nominal (naik dan
turun), dengan waktu tunggu yang sama sebelum tiap rekaman (bawaan 30 detik).
- **Koreksi drift:** tiap titik dikoreksi dengan rata-rata nol sebelum dan sesudahnya, yang
  membatalkan drift linear.
- **Hitungan di PC:** regresi `count_bersih = intercept + slope × mg`, lalu
  `countsPerGram = slope × 1000`, ditambah residual, nonlinearitas, histeresis, dan
  repeatability.
- **Span ditentukan beban terbesar**; beban kecil gunanya menguji linearitas.

```
CALZERO,<millis>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
CALPT,<millis>,<nominal_mg>,<U|D>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
```

Nilainya raw count. Fit dan penyimpanan dikerjakan di PC (`tuning_web/analysis.py`).
Ketidakpastian satu titik ±0,475 mg; beban minimum untuk S/N 10 adalah 4,75 mg.

---

## 7. Antarmuka serial (115200 baud, akhiri dengan Enter)

| Perintah | Fungsi |
|---|---|
| `t` | Tare |
| `c <gram>` | Kalibrasi 1 titik |
| `n` / `m <mg> <u\|d>` | Rekam nol / titik kalibrasi → baris `CALZERO` / `CALPT` |
| `s <cpg>` | Terapkan count/gram hasil fit PC |
| `z` / `r` | Self-calibration / reset ADS1232 |
| `v` | Mode report human ↔ CSV |
| `e` | Reset posisi encoder |
| `p` | Kalibrasi tersimpan + diagnostik load cell (drift, noise, window) |
| `x` | Diagnostik MAX31865 + fault-detection cycle baru |
| `?` | Bantuan |

**Report tiap 500 ms** (format ini di-parse dasbor; jangan diubah):
```
[ 12.5s] RTD 27.41 C (R=110.62, fault 0x00) | W 14.2381 g STABLE (raw 452331) | ENC 0 btn:-
```

| Status berat | Arti |
|---|---|
| STABLE / MOVING | bergerak < 5 mg dalam 3,2 detik / belum |
| SETTLE | setelah reset/self-cal, menunggu data |
| BUSY | tare/kalibrasi sedang berjalan |
| CLIP | raw di skala penuh (overload/wiring) |
| DOUTERR | DOUT tidak kembali HIGH setelah pulsa ke-25 |
| NODATA | tidak ada DRDY > 2 detik |
| akhiran UNCAL | skala belum dikalibrasi |

- `W` dicetak 4 desimal supaya rata-rata di PC tidak terkunci ke grid 1 mg; ini bukan klaim
  akurasi.
- **Tiap 60 detik sejak boot** firmware mencetak nilai per menit (rancangan §5):
  `MNT,<menit>,<massa_mg>,<kemiringan_mg_per_menit>,<sebaran_mg>,<ketidakpastian_mg>,<detik_dipakai>,<detik_sah>,<sah 1|0>`.
  Massanya relatif terhadap offset tare tersimpan, jadi menit yang memuat tare tidak berarti.
- Pesan non-data diawali `# `. Baris `CALZERO,` / `CALPT,` sengaja tanpa awalan.
- Mode CSV (`v`): `millis,tempC,rtdOhm,rtdFault,rawCounts,grams,stable,encPos`.

---

## 8. Masalah terbuka

### 8.1 RTD: pengawatan sudah benar (12 Okt 2026), titik es belum dicek
- **Masalahnya dulu:** rasio terukur 0,868 (seharusnya ≈ 0,26), terbaca ±373 Ω / ±780 °C.
  Pernah ditutupi dengan `RTD_RREF = 128.4`, yang membuat cip saturasi di 73,5 °C.
- **Penyebab:** jumper "24 · 3" di atas Rref masih di posisi 24. Pada mode 3-wire, FORCE2
  harus tersambung ke FORCE+ (datasheet MAX31865, "3-wire sensor connection").
- **Konfigurasi yang benar** ([panduan Adafruit](https://learn.adafruit.com/adafruit-max31865-rtd-pt100-amplifier/rtd-wiring-config)):
  - pasangan kabel merah (0,9 Ω) ke F+ dan RTD+;
  - kabel biru ke F− / RTD−;
  - jumper "2/3 Wire" disolder;
  - trace "24" dipotong dan pad tengah disolder ke "3";
  - firmware tetap 3-wire.
- **Hasil:** rasio 0,2595; siklus fault bersih.
- **Tuning RREF (12 Okt 2026), multimeter sebagai acuan utama:**
  - resistor referensi terukur langsung 423,12 Ω → **`RTD_RREF = 423.12f`**;
  - cek silang: probe merah–biru 110,8 Ω − pasangan merah 0,9 Ω = 109,9 Ω (3-wire membuang
    hambatan kabel), 109,9 / rasio 0,2595 = 423,5 Ω; selisihnya hanya 0,25 °C;
  - bacaan saat tuning: 109,9 Ω = 25,4 °C (dengan 430 terbaca 29,8 °C); batas ukur ±965 °C.
- **Offset −1 °C (12 Okt 2026, keputusan pemilik) → `RTD_OFFSET_C = -1.0f`:**
  - PT100 konsisten lebih tinggi dari sensor pembanding ruang: 25,4 vs 23 °C (+2,4) dan
    28,6 vs 26 °C (+2,6). Selisih yang tetap berarti kesalahan kalibrasi, bukan noise;
  - pemanasan probe oleh arus bias sudah disingkirkan: saat bias baru hidup bacaan datar
    28,62–28,65 °C selama 70 detik;
  - multimeter genggam (±0,5–1 % ≈ ±1,4–2,9 °C) tidak bisa memutus selisih ini lewat RREF;
  - dipilih koreksi sebagian −1 °C, sisa selisih ±1,5 °C. Offset ditambahkan ke suhu
    (`RtdSensor`); resistansi yang dilaporkan tetap nilai terukur;
  - kalau penyebab aslinya RREF, selisih di 110 °C ±3,2 °C sebelum offset (±2,2 °C sesudah).
    Acuan yang lebih kuat adalah titik es (0 °C = 100,00 Ω), yang menggantikan offset ini.
- `fault 0x00` di report **tidak** membuktikan pengawatan benar: bit deteksi kabel putus
  hanya diperbarui oleh fault-detection cycle. Untuk menjalankan siklus baru, pakai `x`.

### 8.2 Drift nol

| Kandidat | Cara uji |
|---|---|
| Referensi ADC tidak rasiometrik | cek jumper INT/EXT modul |
| Pemanasan awal | ukur ulang setelah 60 menit menyala |
| EMF termal sambungan breadboard | pindah ke papan solder |
| Offset internal ADC | `z` berkala, bandingkan laju drift |
| Dudukan load cell belum kaku | pasang kaku, ukur ulang |

**Temuan 5 Oktober 2026** (log singkat dengan beban 10 g, uptime 2–5 menit, dihitung dari
raw count):

| Kejadian | Pergeseran nol | Penyebab paling mungkin |
|---|---|---|
| Beban 10 g diangkat, pertama | +0,4 mg | kembali ke nol |
| Beban 10 g diangkat, kedua | +22 mg, naik ke +57 mg dalam ±100 detik (±26 mg/menit, melambat) | belum bisa dibedakan: pemanasan awal (uptime < 5 menit), dudukan load cell, atau EMF termal sambungan |
| Jumper disentuh/dikencangkan | −86 mg seketika; noise 3,6 → 10 mg | sambungan breadboard: resistansi kontak + EMF termal (1 µV ≈ 430 count ≈ 78 mg) |

- **Sambungan breadboard terbukti berpengaruh**, dan pindah ke PCB menghilangkan bagian itu.
- **Pemanasan awal dan dudukan load cell tetap harus diuji terpisah** (log panjang U2).
- **Angka W yang lambat kembali ke nol** di log yang sama (±9 detik setelah beban diangkat)
  bukan masalah hardware. Itu bug filter firmware dan sudah diperbaiki (§4.2).

### 8.3 Batas resolusi fisis
- 1 mg absolut = 1/300 000 kapasitas, sedangkan resolusi bebas derau 1/13 634. Itu batas load
  cell 300 g, bukan perangkat lunak; untuk 1 mg absolut perlu load cell 10–30 g.
- Ini berbeda dengan **resolusi laju**: perubahan berat per menit bisa diukur jauh lebih
  halus setelah dirata-rata (rancangan §5).

### 8.4 Uji unit
- **Tidak ada gcc** di mesin pengembangan, jadi `pio test -e native` belum bisa dipakai.
- **Suite siklus firmware** (`test_drying_math`, `test_cycle_aggregation`) jalan lewat
  MSVC 2019: `test\run_msvc.bat`.
- **Tujuh suite lama untuk `lib/Heatbox`** belum bisa jalan.
- **Uji di board** (`pio test -e esp32doit-devkit-v1`) saat ini gagal karena Adafruit BusIO
  dari `lib_deps` tidak menemukan `Wire.h`.

### 8.5 Belum terverifikasi terhadap datasheet

| Lokasi | Pertanyaan | Rujukan |
|---|---|---|
| `Ads1232.cpp`, `reset()` | Apakah data pertama setelah PDWN sudah settled? Kalau ya, `kDiscardAfterReset` bisa 0 | SBAS350H 7.4.4, Gambar 7-14, Tabel 7-12 |
| `Ads1232.cpp`, `reset()` | Apakah PDWN menghapus hasil offset calibration; ada auto-cal saat power-up? | SBAS350H 7.4.1, 7.4.4 |
| `RtdSensor.h`, `kStartupSettleMs` | Waktu start-up VBIAS dan waktu konversi kontinu 50 Hz | MAX31865, Electrical Characteristics |
| `RtdSensor.h`, `kFaultCycleTimeoutMs` | Durasi automatic fault-detection cycle | MAX31865, register Configuration D3:D2 |
| `RtdSensor.cpp`, `ohmToThresholdRegister()` | Format dan arah register High/Low Fault Threshold | MAX31865, Fault Threshold registers |
| `RtdSensor.cpp`, `kFaultTexts` | Teks bit D7..D2 Fault Status Register | MAX31865, Fault Status register |

---

## 9. Dasbor PC (`tuning_web/`)

| Berkas | Fungsi |
|---|---|
| `server.py` | HTTP 127.0.0.1:8080, pembaca serial, API |
| `analysis.py` | regresi dan metrik kalibrasi (stdlib saja) |
| `drying.py`, `cycle.py` | siklus versi PC; acuan uji untuk port firmware |
| `index.html` | dasbor |

- Tiap sesi server menulis `data/calibration_<tanggal>_<jam>.csv` (kolom `recorded_at,
  millis, nominal_mg, direction, raw_mean, raw_sd, samples, rtd_ohm, rtd_temp_c,
  zero_before, zero_after`).
- Siklus PC menyimpan data ke `data/cycle_<tanggal>_<jam>/`.

---

## 10. Angka kunci

| Besaran | Nilai |
|---|---|
| Faktor skala | 5499,0 count/gram (0,1819 mg/count) |
| Sensitivitas load cell | 0,7682 mV/V |
| Derau RMS / p-p | 3,80 mg / 22,0 mg |
| Drift nol setelah nyala | 14,2 mg/menit |
| Histeresis pada 10 g | 27,9 mg |
| Ketidakpastian titik kalibrasi | ±0,475 mg |
| Ambang STABLE | 5 mg dalam 3,2 detik |
| Batas ukur suhu (RREF 423,12) | ±965 °C (PT100 kelas B sampai 850 °C) |

## 11. Ide Tambahan
- calibration tetep di pakai nanti di user interface dan masuk ke SOP
- ada lampu indikator 1 lampu warna bisa berubah, terdiri dari preparation alat orange sudah siap jika buru buru, hijau sudah oke banget, siap tare dan calibration setelah itu dengan batu timbangan 10g, lampu untuk proses juga
- 

