# Catatan implementasi — siklus pengeringan sampel

**Tanggal:** 28 September 2026
**Status:** perangkat lunak selesai dan teruji dengan data sintetis; belum diuji dengan
sampel nyata; pemanas belum terpasang.

Dokumen ini mencatat apa yang dibangun, kenapa tiap metode dipilih, apa buktinya, apa
yang belum beres, dan kesalahan yang saya buat selama prosesnya.

---

## 1. Apa yang sekarang bisa dilakukan

Alat ini sekarang bekerja seperti **moisture analyzer**: menimbang sampel tepung selama
dipanaskan, mencatat susutnya tiap menit, lalu memutuskan kapan pemanasan berhenti.

Konsepnya sama dengan moisture analyzer halogen komersial, yang berhenti otomatis saat
susut kurang dari 1 mg per 20–180 detik ([manual Mettler Toledo HB43-S][hb43]). Bedanya ada
di satu ide Anda: load cell di alat ini hanya dipercaya sampai laju **10 mg/menit**. Di
bawah itu drift dan creep sebanding dengan sinyalnya, jadi sisa kurva **diekstrapolasi
dari model**, bukan diukur, sampai laju model mencapai **0,1 mg/menit**.

Semua perhitungan berjalan di PC (`tuning_web/`), sesuai arsitektur proyek: ESP32 hanya
mengirim baris teks.

---

## 2. Alur operator

Halaman `http://127.0.0.1:8080`, panel **Siklus sampel**:

| Langkah | Tombol | Yang terjadi |
|---|---|---|
| 0 | — | Alat dinyalakan. Firmware men-tare otomatis sekali saat boot (sudah ada sebelumnya). |
| 1 | **Mulai siklus** | Folder data baru dibuat. Halaman menampilkan **drift nol** dalam mg/menit. |
| 2 | **Tare wadah kosong** | Wadah kosong terpasang. Tekan saat drift nol sudah di bawah ambang (3 mg/menit). |
| 3 | — | Masukkan sampel. Halaman menampilkan berat langsung dan gerak creep-nya. |
| 4 | **Mulai pemanas** | Berat awal **m0** dicatat (rata-rata robust 30 detik terakhir). Waktu nol dimulai. |
| 5 | otomatis | Tiap menit satu titik kurva, laju penyusutan, dan prediksi sementara. |
| 6 | otomatis | **Cut-off**: laju < 10 mg/menit selama 2 menit berturut-turut. Fit dibekukan. |
| 7 | otomatis | **Selesai** saat waktu prediksi tercapai. Menit sesudah cut-off tetap direkam sebagai validasi. |

Tombol **Hentikan** dan **Batalkan** tersedia di tiap fase aktif. Semua fitur tuning lama
(tare, self-cal, reset ADS, diagnostik, kalibrasi 1 titik dan multi-titik, tampilan
tenang) dipindah ke bagian **Perawatan & kalibrasi** yang terlipat di bawah — tidak ada
yang dihapus kecuali tombol *Reset encoder*, karena encoder belum dipakai di alur ini.

### Tentang pertanyaan Anda soal tare

> "Kalau di-tare nanti bakal balik lagi ke sekitar nilai itu?"

Tergantung **kapan** tare-nya. Pengamatan Anda: setelah ~2 jam, nol menetap di sekitar
+0,3 g dan diam di situ. Itu kesetimbangan termal.

- **Tare sebelum setimbang** → angka akan merayap lagi menuju kesetimbangan (+0,3 g).
- **Tare setelah setimbang** → angka tetap dekat nol.

Tare tidak menghentikan drift; ia hanya memindahkan titik nol. Karena itu langkah 2 di
atas menampilkan drift nol secara langsung: tekan Tare saat angkanya sudah kecil. Untuk
pemakaian harian, paling praktis alat dibiarkan menyala (standby) supaya selalu setimbang.

---

## 3. Langkah implementasi

Dikerjakan dalam empat langkah; tiap langkah diuji sebelum langkah berikutnya dibangun di
atasnya.

### Langkah 1 — Matematika kurva (`tuning_web/drying.py`, baru)

Fungsi murni tanpa hardware: nilai per menit, laju, aturan "tidak boleh naik", fit
eksponensial, dan prediksi. Hanya pustaka standar Python, seperti `analysis.py`.

Desainnya **melanjutkan rencana awal di `lib/Heatbox`** yang belum pernah disambungkan ke
`main.cpp`: blok 60 detik, regresi 5 blok untuk laju, fit eksponensial dengan
golden-section search, termasuk varian dengan suku drift linear.

### Langkah 2 — Mesin siklus (`tuning_web/cycle.py`, baru) + `server.py`

Fase, agregasi per menit, aturan cut-off, pengaman, dan penyimpanan data. Setiap baris
report dari firmware diteruskan ke mesin ini oleh `server.handle_line`.

### Langkah 3 — Firmware (`src/main.cpp`, `lib/RtdSensor`, `include/app_config.h`)

Dua perubahan kecil:

1. **Berat dicetak 4 desimal** (0,1 mg). Ini **bukan** klaim akurasi — noise satu baris
   jauh lebih besar. Gunanya supaya rata-rata ratusan baris di PC tidak terkunci ke grid 1
   mg. Halaman web tetap menampilkan 3 desimal.
2. **Plafon sensor suhu ditampilkan dan diawasi.** Saat boot dicetak
   `RTD maks : X C`, dan tiap menit dicek apakah rasio ADC RTD di atas 0,95. Lihat §6.1
   untuk alasannya.

**Perlu di-upload.** Tanpa upload, halaman tetap bekerja (dengan resolusi 1 mg), tetapi
peringatan plafon suhu tidak muncul.

### Langkah 4 — Halaman web (`tuning_web/index.html`, ditata ulang)

Panel siklus di tengah, dua grafik (berat per menit, laju penyusutan skala log), tabel per
menit sebagai padanan grafik, berkas data, dan pengaturan. Grafik digambar dengan SVG tanpa
pustaka luar supaya jalan tanpa internet. Warna seri divalidasi terhadap permukaan panel
yang sebenarnya untuk buta warna (semua pasangan lolos).

---

## 4. Metode — dan kenapa yang ini

### 4.1 Nilai per menit: garis robust dibaca di tengah menit

Tiap menit berisi ~120 baris report. Nilainya dihitung begini: tarik garis lurus
(least squares) lewat sampel menit itu → buang sampel yang menyimpang lebih dari 3× sebaran
robust → hitung ulang garisnya → **baca garis itu tepat di tengah menit**.

Kenapa tidak median atau rata-rata saja? Karena massa **sedang turun** di dalam menit itu.
Versi pertama saya memakai IQM (rata-rata separuh nilai tengah). Pada laju 40 mg/menit
dengan tisu menekan wadah 10 detik, IQM ikut membuang sampel awal menit — yang tertinggi
karena belum menyusut — sehingga pusat waktunya bergeser 5 detik dan hasilnya **bias 3,4
mg**. Garis robust memberi 0,15 mg. Tes otomatis menangkap ini sebelum kodenya dipakai.

### 4.2 Laju penyusutan: Theil-Sen atas 5 menit

Median dari semua kemiringan antar-pasangan titik dalam 5 menit terakhir. Satu menit yang
terganggu tidak bisa menyeret hasilnya. Contoh uji: data dengan satu menit rusak, regresi
biasa memberi laju **−8 mg/menit (tanda terbalik)**, Theil-Sen memberi **10,0** yang benar.

### 4.3 Aturan "berat tidak mungkin naik"

Dua bagian:

1. **Menit yang naik lebih dari 3 mg** dibanding menit sebelumnya ditandai `naik` dan
   **dikeluarkan** dari laju dan fit. Tampil sebagai tanda silang merah di grafik.
2. **Garis "terkoreksi"** di grafik adalah proyeksi least squares ke deret yang tidak
   pernah naik (algoritma PAVA).

Kenapa bukan "ambil nilai minimum sejauh ini"? Karena itu bias ke bawah: ia mengunci ke
simpangan noise yang paling rendah. Pada noise murni tanpa ada yang menguap, cara itu
menciptakan susut palsu **1,28 mg** dalam 60 menit; PAVA hanya 0,24 mg.

Garis terkoreksi **hanya untuk tampilan**. Laju dan fit memakai nilai per menit asli,
karena PAVA meratakan ekor kurva dan akan membuat laju terlihat turun lebih cepat — memicu
cut-off terlalu dini.

### 4.4 Cut-off

Cut-off terjadi bila **semua** terpenuhi:

- pemanasan sudah berjalan minimal 3 menit;
- laju pernah melewati 10 mg/menit (penyusutan benar-benar terjadi);
- laju di bawah 10 mg/menit selama 2 menit berturut-turut.

Syarat kedua penting. Tanpanya, di menit-menit awal — atau untuk sampel yang sudah kering —
laju yang kecil akan langsung memicu cut-off. Kalau penyusutan tidak pernah melewati
ambang sampai batas waktu maksimum, siklus selesai **tanpa prediksi**: fit terhadap noise
murni tetap menghasilkan "waktu berhenti" yang terlihat meyakinkan, dan itu lebih
berbahaya daripada tidak ada angka sama sekali.

### 4.5 Fit dan prediksi

Model: `m(t) = m_inf + A·exp(−(t − t0)/tau)`, opsional ditambah `k·(t − t0)` untuk drift.

- **Hanya periode laju menurun yang difit.** Kurva pengeringan punya pemanasan awal, laju
  konstan, lalu laju menurun; model eksponensial hanya berlaku untuk yang terakhir. Fit
  dimulai dari menit terakhir yang lajunya masih ≥ 90 % puncak.
- **Pencarian tau:** grid logaritmik 48 titik lalu golden-section di sekitar minimum. Grid
  lebih dulu supaya tidak terjebak minimum lokal.
- **Waktu berhenti:** `t_stop = t0 + tau · ln(A / (tau · target))`.
- **Sisa air saat berhenti = tau × target.** Dengan tau 20 menit dan target 0,1 mg/menit,
  masih ada 2 mg air di sampel saat alat berhenti. Itu sifat kriteria laju, bukan error.

Model **dengan drift** menganggap suku linear sebagai drift alat, dan hanya suku
eksponensial yang dihitung sebagai pengeringan. Ini asumsi: suku linear yang sama juga
bisa berasal dari pengeringan laju-konstan yang lambat. Karena itu keduanya selalu
dihitung, dan halaman memperingatkan bila waktu berhentinya berbeda lebih dari 30 %.

### 4.6 Peringatan keyakinan

Halaman menampilkan peringatan bila: tau menempel di batas pencarian (kurva belum cukup
melengkung), R² < 0,98, prediksi mengekstrapolasi lebih dari 3× rentang data yang difit,
atau dua model berbeda lebih dari 30 %.

---

## 5. Bukti

Uji otomatis: `cd tuning_web && python -m unittest test_cycle -v` — **13 tes, semua lolos.**

Uji paling penting mensimulasikan satu siklus penuh lewat `server.handle_line`, jalur yang
sama persis dengan data serial asli, 2 baris per detik, dengan tisu menekan wadah 10
detik di menit 5 dan satu menit "naik" +45 mg di menit 8:

| Besaran | Hasil | Jawaban benar |
|---|---|---|
| Waktu berhenti | menit **64,98** | 64,97 |
| Tau | 10,001 menit | 10 |
| Menit 5 (tisu 10 detik) | 4782,73 mg, 100/120 sampel dipakai | 4782,58 mg |
| Menit 8 (naik +45 mg) | ditandai `naik`, dikeluarkan | — |
| Validasi 41 menit setelah cut-off | RMS 0,06 mg | — |
| Kadar air saat berhenti | 11,980 % | 11,980 % |

Juga diuji: ESP32 restart di tengah siklus, sampel yang tidak pernah mengering, pemanasan
tanpa sampel, pemanasan sebelum tare, tare yang tidak dikonfirmasi firmware, dan server
HTTP sungguhan (halaman, API, berkas, path traversal ditolak).

> **Penting:** data sintetis ini **tidak punya creep dan drift termal** — padahal di
> hardware Anda itulah musuh utamanya. Tes ini membuktikan **logikanya** benar. Akurasi
> di dunia nyata baru akan terbukti dari simulasi air Anda.

---

## 6. Keterbatasan yang harus Anda ketahui

### 6.1 Sensor suhu buta di atas ~73 °C — HARUS dibereskan sebelum pemanas dipasang

`RTD_RREF` di `include/pins.h` sekarang **128,4**. Resistor referensi di board Anda
terukur **424 Ω** (kode `431`). Angka 128,4 adalah faktor koreksi untuk kesalahan
pengawatan 3-wire yang belum diperbaiki.

Akibatnya cip saturasi saat resistansi probe mencapai 128,4 Ω, yaitu **~73,5 °C**. Di atas
itu suhu yang tampil macet, **tanpa fault apa pun**. Metode standar kadar air tepung
(AACC 44-15.02, identik dengan AOAC 925.10) memakai **130 °C selama 60 menit**
([BAKERpedia][bakerpedia], [ASBE][asbe]). Oven akan mencapai suhu kerjanya lama setelah
sensornya berhenti melapor.

Firmware sekarang mencetak plafon ini saat boot dan memperingatkan saat mendekatinya; siklus
di web ikut menampilkan peringatan itu. Tapi obatnya tetap: kerjakan jumper 3-wire
(pasangan kawat ~2 Ω ke **F+ dan RTD+**, kawat tunggal ke **F− atau RTD−**, potong trace
tipis jumper kanan lalu solder tutup; solder tutup jumper kiri — [panduan Adafruit][ada]),
lalu kembalikan `RTD_RREF = 430.0f`.

### 6.2 Creep dan drift membuat susut terlihat lebih kecil

Creep di bawah beban **naik** (terukur +30 mg/menit sesaat setelah 500 mg diletakkan), dan
drift pemanasan alat juga naik. Keduanya bergerak berlawanan arah dengan susut, jadi
laju yang terukur lebih kecil dari laju sebenarnya — dan cut-off bisa terpicu lebih
cepat.

Model "eksponensial + drift" menyerap bagian yang linear. Cara yang benar secara ilmiah
adalah **blank run**: jalankan siklus yang sama dengan beban inert (anak timbangan seberat
sampel) dan pemanas menyala, lalu kurangkan kurvanya. Ini praktik standar di
termogravimetri. Belum diimplementasikan.

### 6.3 Pemanas akan membuat berat tampak naik sesaat

Saat pemanas menyala, udara di atas wadah memuai dan naik. Gaya apung dan arus konveksi
itu akan menggeser pembacaan — di termogravimetri efek ini disebut koreksi *buoyancy*.
Menit-menit awal pemanasan mungkin ditandai `naik`. Ini belum bisa diuji tanpa pemanas.

### 6.4 ESP32 restart = siklus dibatalkan

Setelah restart, firmware men-tare otomatis. Kalau sampel sudah di wadah, tare itu
menolkannya. Mesin siklus mendeteksi restart (uptime mundur, atau baris banner firmware)
dan langsung membatalkan siklus, supaya tidak ada data sesudahnya yang dianggap sah.

### 6.5 Pemanas belum dikendalikan

**Mulai pemanas** hanya menandai waktu nol, dan **Selesai** belum mematikan apa pun. Ada
dua penanda `TODO(heater)` di `cycle.py` untuk perintah ON/OFF setelah hardware ada.

### 6.6 Target 0,1 mg/menit lebih ketat dari alat komersial

Pengaturan paling ketat di moisture analyzer Mettler Toledo adalah 1 mg per 180 detik =
**0,33 mg/menit**. Target 0,1 mg/menit tiga kali lebih ketat, dan di alat ini dicapai lewat
ekstrapolasi, bukan pengukuran. Mungkin tetap tepat untuk tujuan Anda, tetapi layak
dipertimbangkan ulang bersama pembimbing. Nilainya bisa diubah di **Pengaturan siklus**.

### 6.7 Tidak saya lihat di browser

Saya tidak punya akses browser di sesi ini. Sintaks JavaScript, setiap ID elemen, endpoint
HTTP, dan kesamaan rumus model antara JavaScript dan Python sudah dicek, tetapi **tata
letak visualnya belum pernah saya lihat**. Mohon laporkan kalau ada yang tampil aneh.

---

## 7. Cara simulasi dengan air

1. Pasang wadah kecil (cawan, kertas timah). **Mulai siklus**, tunggu drift nol kecil,
   **Tare wadah kosong**.
2. Masukkan tisu basah atau sedikit air — beberapa gram sudah cukup. Tunggu angkanya
   tenang, lalu **Mulai pemanas**.
3. Kurangi beratnya:
   - **Paling mirip pengeringan asli:** biarkan menguap sendiri (air hangat lebih cepat).
     Penguapan alami melambat dengan sendirinya, mirip periode laju menurun.
   - **Dengan tisu:** serap sedikit-sedikit dan merata. Tekanan ke wadah ditolak otomatis
     selama totalnya di bawah ~25 detik per menit (terukur: meleset < 0,1 mg); di 30 detik
     — separuh menit — tekanan ikut terhitung. Angka `W` firmware adalah rata-rata bergerak
     yang melebarkan tiap tekanan hingga ~13 detik, jadi dalam praktik usahakan tiap sentuhan
     di bawah 10 detik.
4. Amati titik per menit, laju yang turun, cut-off di 10 mg/menit, lalu prediksi.
5. **Biarkan berjalan sampai selesai** walaupun cut-off sudah lewat. Menit sesudah cut-off
   adalah data validasi: kolom *residu model* menunjukkan seberapa jauh prediksi meleset
   dari kenyataan. Itu angka yang paling berharga untuk laporan.

Data tiap siklus tersimpan di `tuning_web/data/cycle_<tanggal>_<jam>/`:

| Berkas | Isi |
|---|---|
| `samples.csv` | setiap baris report selama siklus (2 per detik) |
| `minutes.csv` | nilai per menit, laju, sebaran, status, residu model |
| `summary.json` | pengaturan, m0, cut-off, fit, prediksi, peringatan, kejadian |

---

## 8. Tentang kalibrasi 500 mg yang Anda kirim

Empat titik, semuanya 500 mg. Dari situ **tidak bisa** dibuat garis kalibrasi (butuh
minimal dua nominal berbeda), jadi panel dulu hanya berkata "butuh minimal 2 titik". Yang
bisa diukur adalah **repeatability**, dan sekarang panel menampilkannya:

- Repeatability **SD 4,45 mg** (0,89 % dari 500 mg).
- Noise elektronik hanya menjelaskan **4,9 dari 24,8 count** — sebaran teramati **5,1×
  lebih besar**. Penyebabnya mekanis, bukan elektronik.
- Tersangka utama: **waktu tunggu yang berbeda-beda** sebelum menekan tombol. Creep sesaat
  setelah beban diletakkan puluhan mg/menit, jadi selisih beberapa detik saja sudah
  beberapa mg. Tersangka kedua: posisi beban di wadah tidak sama.
- Nol bergeser +3,6 mg selama tes.

Dua perbaikan di panel kalibrasi: **waktu tunggu seragam** sebelum setiap rekaman (default
30 detik, dengan hitung mundur), dan pembacaan repeatability walau hanya ada satu nominal.

Dan pengingat yang sama seperti sebelumnya: dengan repeatability 4,45 mg, kalibrasi span
memakai 500 mg memberi error skala **0,89 %**; memakai **50 g** hanya **0,009 %**. Span
ditetapkan beban terbesar; beban kecil untuk menguji linearitas.

(CSV itu dari sesi lama: RTD masih terbaca 371 Ω / 785 °C.)

---

## 9. Koreksi dan permintaan maaf

Kesalahan saya yang perlu Anda ketahui, supaya tidak ada yang tersembunyi.

**Di pekerjaan ini, tertangkap sebelum dipakai:**

- Nilai per menit versi pertama (IQM) bias 3,4 mg saat massa sedang turun (§4.1).
- Sampel yang tidak mengering tetap menghasilkan "prediksi" dari fit terhadap noise
  (§4.4). Sekarang tidak ada prediksi dalam kasus itu.
- Dua bug grafik di tinjauan ulang: garis ekstrapolasi bisa tergambar keluar area plot,
  dan tooltip tertutup setiap kali tinggi panel berubah.

**Dari sesi sebelumnya, dan berdampak pada yang Anda alami:**

- **"Drift ngebut di dekat nol"** yang Anda amati kemungkinan besar berasal dari *tampilan
  tenang* buatan saya: ia menahan angka lalu melepasnya sekaligus, sehingga drift pelan
  terlihat seperti lompatan. Angka "asli" di bawahnya bergerak mulus. Penjelasan ini
  sekarang tertulis di panel pengaturannya.
- **Tare otomatis saat nyala** yang saya buat menjalankan tare di saat drift paling
  besar. Alur siklus sekarang meminta tare ulang oleh operator setelah drift nol turun.
- **Instruksi pengawatan 3-wire yang pertama saya berikan terbalik.** Yang benar ada di
  §6.1, dari panduan Adafruit.
- Prediksi saya bahwa resistor referensi Anda ~124 Ω **salah** — Anda mengukurnya 424 Ω.
- Angka histeresis 91 mg saya ambil saat nol **masih pulih**, bukan nilai akhirnya.
- Statistik noise saya sempat ikut menghitung drift sebagai noise; deteksi perubahan beban
  sempat terlambat 6,5 detik; ambang "beban terlalu kecil" sempat saya pasang 50 mg padahal
  hitungannya 4,75 mg. Semuanya sudah diperbaiki.

---

## 10. Asumsi yang saya ambil — koreksi kalau salah

- m0 = rata-rata robust **30 detik terakhir** sebelum pemanas mulai.
- Cut-off butuh **2 menit berturut-turut** di bawah ambang, minimal 3 menit pemanasan.
- Toleransi "naik" **3 mg** per menit. Noise nilai per menit sekitar 0,5 mg, jadi ini ~6σ.
- Batas pemanasan maksimum **180 menit**.
- Siklus berhenti sendiri saat waktu prediksi tercapai.
- Model default **eksponensial tanpa drift**; yang dengan drift ditampilkan sebagai
  pembanding.

Semua angka ini bisa diubah di **Pengaturan siklus** tanpa mengubah kode.

---

## 11. Berkas yang berubah

| Berkas | Perubahan |
|---|---|
| `tuning_web/drying.py` | **baru** — matematika kurva |
| `tuning_web/cycle.py` | **baru** — mesin siklus dan penyimpanan |
| `tuning_web/test_cycle.py` | **baru** — 13 uji otomatis |
| `tuning_web/server.py` | sambungan siklus, endpoint `/api/cycle`, flag `UNCAL`, repeatability |
| `tuning_web/analysis.py` | `repeatability_summary()` |
| `tuning_web/index.html` | ditata ulang: siklus di tengah, perawatan terlipat, grafik |
| `src/main.cpp` | berat 4 desimal; plafon RTD saat boot; peringatan saturasi |
| `lib/RtdSensor/src/RtdSensor.*` | `maxMeasurableTempC()` |
| `include/app_config.h` | `kRtdRatioWarn` |

[hb43]: https://www.mt.com/dam/product_organizations/laboratory_weighing/moisture/products/hb43_s/documentation/en/HB43-S_OI_en_11780961A.pdf
[bakerpedia]: https://bakerpedia.com/processes/moisture-in-flour/
[asbe]: https://asbe.org/article/moisture-in-flour/
[ada]: https://learn.adafruit.com/adafruit-max31865-rtd-pt100-amplifier/rtd-wiring-config
