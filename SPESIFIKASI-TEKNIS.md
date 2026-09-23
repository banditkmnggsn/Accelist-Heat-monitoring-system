# Spesifikasi Teknis — Sistem Akuisisi Sensor Heat Box

**Tahap:** riset breadboard
**Tanggal pengukuran:** 23 September 2026
**Status:** subsistem load cell terkarakterisasi; subsistem RTD belum valid (lihat §8)

Seluruh angka pada dokumen ini adalah hasil pengukuran pada perangkat keras,
bukan nilai datasheet, kecuali dinyatakan lain. Angka datasheet ditandai
dengan rujukan dokumennya.

---

## 1. Ringkasan sistem

Sistem mengakuisisi dua besaran fisis untuk penelitian kurva pengeringan:
massa sampel (load cell 300 g melalui ADC ADS1232) dan suhu ruang pengering
(PT100 melalui MAX31865). Data dikirim sebagai baris teks melalui UART ke PC,
yang menjalankan dasbor web lokal untuk perekaman, perhitungan, dan kalibrasi.

### 1.1 Keputusan arsitektur

| Keputusan | Alasan teknis |
|---|---|
| Dasbor di PC, bukan di ESP32 | Menghindari kontensi CPU. Driver ADS1232 melakukan bit-bang SCLK dengan *critical section*; tumpukan Wi-Fi ESP32 dapat menyela dan merusak timing pembacaan. PC juga menyediakan penyimpanan dan Python tanpa batas memori mikrokontroler. |
| Driver ADS1232 bit-bang manual | Protokol ADS1232 memerlukan pulsa ke-25 dan ke-26 dengan makna khusus (lihat §4.2) yang tidak ditangani pustaka SPI generik. |
| Encoder secara *polling* | Menghindari ISR yang dapat menyela *critical section* bit-bang. |
| Firmware hanya mencetak RAW COUNTS | Data mentah tetap berguna bila rumus konversi diubah kemudian. Perhitungan dilakukan di PC. |

---

## 2. Perangkat keras

### 2.1 Unit pemroses

| Parameter | Nilai |
|---|---|
| Modul | ESP32-WROOM-32 (DevKit V1, 30 pin) |
| Frekuensi inti | 240 MHz |
| RAM | 320 KB |
| Flash | 4 MB |
| Antarmuka PC | UART0, 115200 baud, jembatan USB CH340 |

### 2.2 Rantai pengukuran massa

| Parameter | Nilai | Sumber |
|---|---|---|
| Load cell | Kapasitas 300 g, jembatan regangan penuh | spesifikasi komponen |
| **Sensitivitas terukur** | **0,7682 mV/V** | diturunkan, §6.2 |
| ADC | TI ADS1232, 24 bit, ΔΣ | SBAS350H |
| Penguatan | 128 (GAIN1 = GAIN0 = 1, *jumper*) | SBAS350H Tabel 7-1 |
| Laju cuplik | 10 SPS (SPEED = 0, *jumper*) | SBAS350H Tabel 7-8 |
| Kanal masuk | AINP1 / AINN1 (A0 = GND) | SBAS350H Tabel 7-1 |
| Mode referensi | Rasiometrik — VREF = tegangan eksitasi jembatan | SBAA154 |

### 2.3 Rantai pengukuran suhu

| Parameter | Nilai |
|---|---|
| Sensor | PT100, konfigurasi 3 kawat |
| Pengkondisi | MAX31865 melalui SPI perangkat keras (VSPI) |
| Resistor referensi | 430 Ω (nilai di `pins.h`; **belum diverifikasi fisik**) |
| Penapis notch | 50 Hz |
| Mode konversi | Kontinu, VBIAS menyala terus |

### 2.4 Alokasi pin

| Fungsi | GPIO |
|---|---|
| MAX31865 SCK / MISO / MOSI / CS | 18 / 19 / 23 / 17 |
| ADS1232 SCLK / DOUT-DRDY / PDWN | 4 / 16 / 13 |
| Encoder EC11 A / B / SW | 32 / 33 / 27 |

GPIO25 dan GPIO26 dicadangkan untuk keluaran *composite video* tahap
berikutnya dan tidak dialokasikan. Pin *strapping* (0, 2, 5, 12, 15), UART0
(1, 3), dan flash internal (6–11) dikecualikan. Pembatasan ini ditegakkan saat
kompilasi melalui `static_assert` di `include/pins.h`.

CS MAX31865 sengaja **bukan** GPIO5 meskipun itu SS bawaan VSPI, karena GPIO5
adalah pin *strapping*.

---

## 3. Perangkat lunak

### 3.1 Lingkungan

| Parameter | Nilai |
|---|---|
| Kerangka kerja | PlatformIO, `espressif32@6.5.0`, Arduino core 2.0.14 |
| Penggunaan RAM | 22 516 bait (6,9 % dari 327 680) |
| Penggunaan Flash | 303 133 bait (23,1 % dari 1 310 720) |

Versi *platform* sengaja tidak dinaikkan: pustaka *composite video* untuk tahap
berikutnya tidak kompatibel dengan core 3.x.

### 3.2 Struktur modul

```
include/pins.h            alokasi pin, RTD_RREF, RTD_RNOMINAL, RTD_WIRES
include/app_config.h      konstanta tingkat aplikasi
lib/Ads1232/              driver bit-bang ADS1232
lib/RtdSensor/            pembungkus MAX31865
lib/EncoderInput/         encoder EC11, polling
lib/Heatbox/              matematika kurva pengeringan (belum dipakai main.cpp)
src/main.cpp              loop, report serial, penangan perintah
tuning_web/server.py      server HTTP di PC, pembaca serial
tuning_web/analysis.py    regresi dan metrik kalibrasi
tuning_web/index.html     dasbor peramban
```

---

## 4. Protokol ADS1232

### 4.1 Pewaktuan

Seluruh nilai dari TI SBAS350H untuk osilator internal 4,9152 MHz.

| Simbol | Besaran | Nilai |
|---|---|---|
| t3 | Lebar pulsa SCLK minimum | 100 ns |
| t4 | DOUT valid setelah tepi naik | maks 50 ns |
| t7 | Waktu konversi, SPEED = 0 | 100 ms |
| t8 | Data pertama setelah kalibrasi offset | 801,03 ms |
| t10 | SCLK HIGH memicu *standby* | 99,96 ms |
| t11 | DRDY pertama setelah *wake-up* | 401,8 ms |
| t14 | Lebar pulsa PDWN minimum | 26 µs |

Implementasi memakai setengah periode SCLK 1 µs, sepuluh kali lipat di atas
t3. Anggaran durasi *shift-out* 26 pulsa adalah ±150 µs, lebih dari 600 kali di
bawah t10 dan jauh di bawah batas *interrupt watchdog* ESP32 (300 ms pada core
2.0.14). Durasi sebenarnya diukur setiap pembacaan dan dapat dibaca melalui
perintah `p`.

### 4.2 Makna pulsa tambahan

- **Pulsa ke-25** memaksa DRDY/DOUT ke HIGH sampai data berikutnya siap. Wajib,
  karena DRDY di-*poll*: tanpa pulsa ini DOUT tertahan pada nilai LSB dan dapat
  terbaca sebagai *data ready* palsu (SBAS350H §7.3.10, Gambar 7-10).
- **Pulsa ke-26** memulai *offset self-calibration*; siklus dimulai pada tepi
  turun pulsa tersebut dan selesai saat DRDY kembali LOW (§7.4.1, Gambar 7-11).

Pemilihan kanal **tidak** melalui SCLK, melainkan melalui pin A0 (Tabel 7-1).

### 4.3 Deteksi kesehatan

Driver membedakan tiga kondisi gagal:

| Kondisi | Deteksi |
|---|---|
| `NoDrdy` | DRDY/DOUT tidak pernah LOW dalam 2000 ms |
| `DoutStuckLow` | DOUT tetap LOW setelah pulsa ke-25 — SCLK tidak diterima cip |
| `ProtocolError` | Sama seperti di atas, terjadi saat berjalan; dihitung kumulatif |

Penyebab tersering `DoutStuckLow` adalah ketidaksesuaian level logika: VIH
ADS1232 adalah 0,7 × DVDD. Bila modul diberi DVDD 5 V, masukan memerlukan
≥ 3,5 V, sementara GPIO ESP32 hanya 3,3 V.

---

## 5. Rantai pengolahan sinyal

```
raw 24-bit  ->  gerbang spike 6 sigma  ->  moving average 16  ->  gram
                (pengganti: median-of-5)                          (x - offset) / countsPerGram
```

### 5.1 Gerbang spike

Sampel masuk **apa adanya** ke perata-rata. Median-of-5 hanya menggantikan
sampel bila simpangannya terhadap median melebihi 6 × SD raw terukur.

Alasan: median sliding sebagai penapis permanen membuat keluaran berurutan
saling berkorelasi, sehingga rata-rata 16 sampel **tidak** memberi pengurangan
noise √16. Efisiensi median dari 5 cuplikan Gaussian juga hanya sekitar 70 %
dibanding rata-ratanya. Dengan gerbang spike, kedua kerugian itu hilang pada
kondisi normal, sementara perlindungan terhadap pencilan tetap ada.

SD raw dihitung dari *ring buffer* 64 sampel mentah yang terpisah. Ketika beban
benar-benar berubah, *window* tersebut ikut memuat lonjakan sehingga SD-nya
melonjak dan gerbang melonggar sendiri — perubahan asli tidak ikut ditolak.

### 5.2 Deteksi stabil

Status `STABLE` ditentukan oleh **rentang pergerakan angka yang ditampilkan**:
selisih maksimum-minimum keluaran *moving average* selama 32 sampel (3,2 detik)
harus di bawah 5 mg.

Sebaran satu sampel mentah (*standard deviation*) sengaja **tidak** dipakai.
Besarnya tetap sekitar 20,9 count baik saat timbangan diam maupun bergerak,
sehingga secara prinsip tidak dapat membedakan keduanya. Yang berubah ketika
beban bergerak adalah keluaran rata-ratanya.

### 5.3 Penyimpanan kalibrasi

Disimpan di NVS (`Preferences`, namespace `heatbox`):

| Kunci | Tipe | Isi |
|---|---|---|
| `ads_offset_d` | `double` | Offset tare dalam count, presisi penuh |
| `ads_cpg` | `float` | Faktor skala, count per gram |

Offset disimpan sebagai `double`, bukan bilangan bulat: satu count setara
0,1819 mg, sehingga pembulatan membuang hingga 0,09 mg.

---

## 6. Karakterisasi terukur

Kondisi: 23 September 2026, tahap breadboard, suhu ruang, tanpa pelindung
elektromagnetik, load cell belum terpasang kaku.

### 6.1 Faktor skala

Diukur dengan anak timbangan bersertifikat 10 g.

| Besaran | Nilai |
|---|---|
| Raw tanpa beban | 112 740,4 count |
| Raw dengan beban 10 g | 167 730,2 count |
| Rentang | 54 989,8 count |
| **Faktor skala** | **5499,0 count/gram** |
| Resolusi satu count | 0,1819 mg |

### 6.2 Turunan besaran fisis

| Besaran | Nilai | Cara peroleh |
|---|---|---|
| Skala penuh ADC | ±19,531 mV | 0,5 × VREF / penguatan, VREF = 5 V |
| Satu LSB | 2,328 nV | skala penuh / 2²³ |
| Sensitivitas rantai | 12,803 µV/gram | 5499 × 2,328 nV |
| Keluaran pada 300 g | 3,841 mV | 12,803 µV × 300 |
| **Sensitivitas load cell** | **0,7682 mV/V** | 3,841 mV / 5 V |
| Pemakaian rentang ADC | 19,7 % dari skala penuh positif | 3,841 / 19,531 |

Angka terakhir menjelaskan akar penyebab kesalahan pembacaan 23 % sebelum
kalibrasi: firmware memakai nilai bawaan yang mengasumsikan sensitivitas
1,000 mV/V, sedangkan sensitivitas sebenarnya 0,7682 mV/V. Rasio kedua angka
tersebut, 0,7682, tepat sama dengan faktor kesalahan yang teramati.

### 6.3 Derau

Diukur dari 241 cuplikan selama 120 detik, setelah komponen drift linear
dikurangkan.

| Besaran | Count | Setara massa |
|---|---|---|
| Simpangan baku (RMS) | 20,9 | 3,80 mg |
| Puncak-ke-puncak | 121 | 22,00 mg |
| Derau setara tegangan masukan | — | 48,7 nV RMS |

Sebaran puncak-ke-puncak sebesar 5,8 σ untuk 241 cuplikan sesuai dengan
prediksi distribusi Gaussian (≈ 6 σ), sehingga derau dinilai bersih tanpa
pencilan sistematis.

**Resolusi efektif:**

| Ukuran | Nilai |
|---|---|
| Count pada kapasitas penuh | 1 649 700 |
| Resolusi RMS | 1/78 933 skala penuh (16,27 bit) |
| Resolusi bebas derau (p-p) | 1/13 634 skala penuh (13,73 bit) |

Sebagai pembanding, load cell kelas OIML C3 disertifikasi untuk 3000 divisi.
Kinerja derau rantai elektronik karena itu berada **di atas** kelas load
cell yang dipakai.

### 6.4 Kestabilan nol

| Besaran | Nilai |
|---|---|
| Laju drift | +1,302 count/detik |
| | +14,2 mg/menit |
| | +0,852 g/jam |
| Histeresis setelah beban 10 g diangkat | 153,6 count = 27,9 mg (0,28 % beban) |

Drift adalah sumber ketidakpastian terbesar pada sistem saat ini, melampaui
derau sebesar hampir empat kali lipat per menit. Pengaruhnya terhadap operasi:

| Selang | Pergeseran |
|---|---|
| 3,2 detik (*window* deteksi stabil) | 4,2 count = 0,76 mg |
| 6,4 detik (64 cuplikan tare) | 8,3 count = 1,52 mg |
| 1 menit | 78 count = 14,2 mg |

### 6.5 Ketidakpastian satu titik kalibrasi

Rata-rata 64 cuplikan menekan derau menjadi 20,9/√64 = 2,6 count.

| Besaran | Nilai |
|---|---|
| Ketidakpastian satu titik | ±0,475 mg |
| Anak timbangan minimum untuk S/N = 10 | 4,75 mg |

---

## 7. Metode kalibrasi multi-titik

### 7.1 Prosedur

1. Kosongkan timbangan, rekam titik nol (perintah `n`, 64 cuplikan).
2. Letakkan anak timbangan, rekam titik (perintah `m <mg> <u|d>`, 64 cuplikan).
3. Angkat beban, rekam titik nol lagi.
4. Ulangi untuk setiap nominal, baik arah naik (`u`) maupun turun (`d`).

Langkah 3 bukan formalitas. Setiap titik dikoreksi terhadap **rata-rata nol
sebelum dan sesudah** pembebanan, dan rata-rata itulah yang membatalkan
komponen drift linear selama titik diukur. Tanpa koreksi ini, drift 14,2
mg/menit akan masuk langsung ke hasil.

### 7.2 Format keluaran

Firmware mencetak satu baris per pengukuran, tanpa awalan `# ` agar dapat
dibedakan dari log biasa oleh pengurai di PC:

```
CALZERO,<millis>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
CALPT,<millis>,<nominal_mg>,<U|D>,<mean>,<sd>,<n>,<rtd_ohm>,<rtd_tempC>
```

Nilai yang dicetak adalah **raw counts**, bukan gram. Firmware tidak menyimpan
titik-titik ini; penyimpanan, regresi, dan metrik seluruhnya dikerjakan di PC.

### 7.3 Perhitungan

Regresi kuadrat terkecil orde satu atas pasangan (nominal_mg, count bersih):

```
count_bersih = intercept + slope × nominal_mg
countsPerGram = slope × 1000
```

Metrik yang dihitung:

| Metrik | Definisi |
|---|---|
| Residual | Selisih pembacaan terhadap nominal, dalam mg |
| Nonlinearitas | Residual absolut terbesar / kapasitas × 100 % |
| Histeresis | Selisih pembacaan arah naik dan turun pada nominal sama |
| Repeatability | Simpangan baku antar pengulangan pada nominal dan arah sama |
| Ketidakpastian titik | SD raw / √n, dikonversi ke mg |

### 7.4 Verifikasi algoritma

Diuji terhadap data sintetis dengan jawaban yang diketahui:

| Pengujian | Hasil |
|---|---|
| Rekonstruksi skala tanpa drift | 5499,000000 count/g; residual 2,3 × 10⁻¹³ mg |
| Rekonstruksi skala dengan drift 120 count per langkah | 5499,000000 count/g; residual 2,3 × 10⁻¹³ mg |
| Deteksi histeresis 50 mg yang disengaja | 50,0000 mg |
| Penolakan data tidak cukup (1 titik) | mengembalikan *null* |

Pengujian kedua mengonfirmasi bahwa koreksi nol sebelum-sesudah membatalkan
drift linear secara sempurna.

---

## 8. Keterbatasan dan masalah terbuka

### 8.1 Subsistem RTD belum valid

Pembacaan saat ini 370,07 Ω, diterjemahkan menjadi 781 °C pada suhu ruang.
Rasio yang sebenarnya diukur cip adalah 370/430 = 0,86, sedangkan PT100 pada
suhu ruang seharusnya menghasilkan 110/430 = 0,257 — selisih faktor 3,35.

Tidak ada kombinasi baku PT100 atau PT1000 dengan resistor referensi 430 Ω
atau 4300 Ω yang menghasilkan rasio 0,86. Karena itu penyebabnya lebih mungkin
berupa kesalahan pengawatan atau konfigurasi *jumper* 3 kawat pada modul,
bukan sekadar konstanta yang keliru.

**Catatan penting:** status `fault 0x00` pada log yang sedang berjalan **tidak**
membuktikan pengawatan benar. Bit D5/D4/D3 (deteksi FORCE- terputus) hanya
di-*set* oleh *automatic fault-detection cycle*, yang pada firmware saat ini
hanya dijalankan sekali ketika `begin()`. Selama berjalan, register yang dibaca
adalah nilai *latched* dari siklus tersebut.

Langkah diagnosis yang disarankan, berurutan:
1. Ukur resistansi elemen PT100 langsung dengan multimeter (seharusnya ≈ 108 Ω
   pada suhu ruang).
2. Ukur nilai resistor referensi di papan dan cocokkan dengan `RTD_RREF`.
3. Periksa konfigurasi *jumper* 2/3/4 kawat pada modul.

Koreksi berupa *offset* atau faktor pengali manual **tidak boleh** diterapkan:
kesalahan bersifat multiplikatif dan akan makin melenceng saat ruang panas,
selain menyembunyikan sambungan yang buruk.

### 8.2 Drift nol belum terselesaikan

Drift 14,2 mg/menit membatasi kegunaan sistem untuk pengukuran berdurasi
panjang, yang justru merupakan tujuan penelitian kurva pengeringan. Kandidat
penyebab yang belum diuji:

| Kandidat | Cara uji |
|---|---|
| Referensi ADC tidak rasiometrik | Periksa posisi *jumper* INT/EXT pada modul. Posisi EXT mengambil referensi dari konektor load cell sehingga drift catu daya saling meniadakan; posisi INT mengambil dari catu 5 V analog sehingga drift catu muncul langsung. |
| Pemanasan awal | Ukur ulang laju drift setelah 60 menit menyala; bila meluruh, penyebabnya termal. |
| Gaya termoelektrik pada sambungan breadboard | Satu count setara 2,328 nV, sehingga beda suhu kecil pada sambungan logam tak sejenis sudah cukup berpengaruh. Uji dengan memindahkan rangkaian ke papan solder. |
| Drift offset internal ADC | Jalankan `z` (*offset self-calibration*) berkala dan bandingkan laju drift sebelum dan sesudah. |
| Penyangga load cell belum kaku | Pasang sel pada dudukan kaku dan ukur ulang. |

### 8.3 Batas resolusi yang bersifat fisis

Target resolusi 0,001 g (1 mg) **tidak dapat dicapai** dengan load cell
kapasitas 300 g. Satu miligram setara 1/300 000 kapasitas, sementara resolusi
bebas derau yang terukur adalah 1/13 634.

Kesimpulan ini bersifat perangkat keras, bukan perangkat lunak: rantai
elektronik sudah bekerja pada 1/78 933 (RMS), lebih baik daripada kelas load
cell yang dipakai. Untuk mencapai 1 mg diperlukan load cell berkapasitas 10–30
gram.

### 8.4 Pustaka Heatbox belum tersambung

`lib/Heatbox` memuat `COUNTS_PER_MG 21.0f` yang tidak sesuai dengan faktor
skala terukur (5,499 count/mg). Konstanta ini **tidak** mempengaruhi pembacaan
saat ini karena `src/main.cpp` tidak meng-*include* pustaka tersebut; hanya
berkas uji yang memakainya. Nilai wajib diperbaiki sebelum pustaka
disambungkan ke jalur pengukuran.

### 8.5 Uji unit belum dapat dijalankan

Lingkungan `[env:native]` memerlukan `gcc`/`g++` yang belum terpasang pada
mesin pengembangan, sehingga tujuh berkas uji Unity yang ada belum dapat
dieksekusi.

---

## 9. Antarmuka perintah serial

Semua perintah diakhiri dengan Enter.

| Perintah | Fungsi |
|---|---|
| `t` | Tare, 64 cuplikan (≈ 6,4 detik) |
| `c <gram>` | Kalibrasi satu titik dengan anak timbangan |
| `n` | Rekam titik nol, menghasilkan baris `CALZERO` |
| `m <mg> <u\|d>` | Rekam titik kalibrasi, menghasilkan baris `CALPT` |
| `s <cpg>` | Terapkan faktor skala hasil regresi dari PC |
| `z` | *Offset self-calibration* ADS1232 |
| `r` | Reset ADS1232 (siklus PDWN) |
| `v` | Alih mode laporan: terbaca manusia ↔ CSV |
| `e` | Reset posisi encoder |
| `p` | Cetak kalibrasi tersimpan dan diagnostik |
| `?` | Bantuan |

### 9.1 Format baris laporan

```
[  909.6s] RTD 781.19 C (R=370.07, fault 0x00) | W 0.235 g MOVING (raw 112737) UNCAL | ENC 0 btn:-
```

Penanda `UNCAL` muncul selama faktor skala masih memakai nilai bawaan. Nilai
`raw` adalah cuplikan mentah terakhir tanpa penapisan; nilai `W` dihitung dari
keluaran *moving average*.

Baris diagnostik selalu diawali `# ` agar mudah disaring saat log diolah.
Baris `CALZERO,` dan `CALPT,` sengaja tidak diawali demikian.

---

## 10. Perangkat lunak PC

| Berkas | Fungsi |
|---|---|
| `server.py` | Server HTTP pada 127.0.0.1:8080, pembaca serial, penulis CSV |
| `analysis.py` | Regresi dan metrik kalibrasi; hanya memakai pustaka standar Python |
| `index.html` | Dasbor: metrik langsung, kontrol, log serial, panel kalibrasi |

`analysis.py` sengaja tidak bergantung pada NumPy maupun SciPy agar dapat
dijalankan pada komputer tanpa hak administrator.

Setiap sesi server menulis berkas `data/calibration_<tanggal>_<jam>.csv`
tersendiri dengan kolom:

```
recorded_at, millis, nominal_mg, direction, raw_mean, raw_sd, samples,
rtd_ohm, rtd_temp_c, zero_before, zero_after
```

Berkas ditulis ulang setiap ada perubahan, bukan ditambahkan, karena nilai
`zero_after` sebuah titik baru diketahui setelah titik tersebut tercatat.
Pemisahan berkas per sesi menjamin tidak ada data sesi lain yang tertimpa.

---

## 11. Ringkasan angka kunci

| Besaran | Nilai |
|---|---|
| Faktor skala | 5499,0 count/gram |
| Sensitivitas load cell | 0,7682 mV/V |
| Resolusi satu count | 0,1819 mg |
| Derau RMS | 3,80 mg (20,9 count) |
| Derau puncak-ke-puncak | 22,00 mg (121 count) |
| Resolusi efektif RMS | 1/78 933 skala penuh (16,27 bit) |
| Resolusi bebas derau | 1/13 634 skala penuh (13,73 bit) |
| Drift nol | 14,2 mg/menit |
| Histeresis pada beban 10 g | 27,9 mg (0,28 %) |
| Ketidakpastian titik kalibrasi | ±0,475 mg |
| Ambang deteksi stabil | 5 mg selama 3,2 detik |
