# Rancangan Siklus Pengeringan — Heat Box

**Versi:** 5 Oktober 2026
**Status:** rancangan disetujui. Implementasi baru langkah 1 (matematika kurva di firmware,
teruji). Siklus di firmware, pemanas, logger, dan LCD belum dibuat (§13).

Ini satu-satunya dokumen panjang di proyek: alur operator, pengaturan dan nilai bawaan,
pengolahan data, penanganan drift, status hasil, log, tampilan LCD, dan rencana uji.
Pengenalan singkat ada di [README.md](README.md). Angka terukur, pin, dan protokol ada di
[SPESIFIKASI-TEKNIS.md](SPESIFIKASI-TEKNIS.md).

| Penanda | Arti |
|---|---|
| **[KUNCI]** | sudah diputuskan pemilik proyek |
| **[USUL]** | usulan rancangan; dipakai sebagai bawaan sampai dikonfirmasi |
| **[BUKA]** | belum diputuskan, lihat §14 |

---

## 1. Ringkasan

1. **Metode:** *loss on drying*. Kadar air basis basah = (W0 − Wf) / W0 × 100 %. Tidak ada
   sensor kelembapan; yang diukur hanya berat dan suhu.
2. **Alat berdiri sendiri [KUNCI]:** siklus, pengolahan data, dan log berjalan di ESP32,
   dioperasikan lewat LCD + rotary encoder. PC hanya opsional: melihat estimasi waktu stop
   dan mengambil log.
3. **Batas akurasi adalah drift, bukan noise.** Setelah data per detik dirata-rata dengan
   benar, noise titik per menit ~0,15–0,25 mg dan noise laju ~0,05–0,17 mg/menit. Drift load
   cell (~1 mg/menit) tidak hilang dengan dirata-rata. "±0,003 g" adalah akurasi absolut
   satu bacaan, bukan resolusi laju.
4. **Dua fase [KUNCI]:** Fase A memakai laju terukur sampai ≤ 4 mg/menit selama 3 menit
   berturut-turut. Fase B memprediksi kapan laju mencapai 1 mg/menit dengan model
   eksponensial, lalu berhenti 2 menit sesudahnya.
5. **Semua batas suhu dan waktu bisa diatur operator [KUNCI]**, punya nilai bawaan, dan
   ada menu Reset Default (§4).

---

## 2. Konsep alat (sketsa, belum final)

| Bagian | Isi |
|---|---|
| Badan | ±30 × 20 cm, tinggi dasar ±5 cm. LCD dan rotary encoder di panel depan |
| Ruang pemanas | ±15 × 15 cm di atas badan, tinggi ruang dalam ±9 cm. Dibuka dengan tuas (tutup berengsel) |
| Tutup | lampu halogen + sensor suhu |
| Sensor suhu | **PT100 + MAX31865 [KUNCI]** (sketsa lama menyebut thermocouple) |
| Tombol Start | **push button industri 22 mm, terpisah, di samping LCD [KUNCI]** |
| Dasar ruang | cawan sampel di atas platform timbangan (load cell 300 g) |
| Dinding, dalam → luar | stainless steel → rockwool → aluminium bubble wrap → light steel / PETG |
| Wadah sampel | aluminium weighing dish |
| Saklar tutup | limit switch: lampu **mati** saat tutup dibuka **[KUNCI]** |

**Catatan rancangan [USUL]:**
- **Saklar tutup memutus daya lampu secara hardware** (seri dengan lampu/SSR), bukan hanya
  dibaca firmware. Firmware tetap membaca status tutup lewat GPIO (§9).
- **Posisi sensor suhu.** Di sketsa sensor ada di tutup, dekat lampu. Sensor yang terkena
  radiasi lampu langsung membaca lebih tinggi dari udara di sekitar sampel, sehingga
  kontrol menjaga *sensor*, bukan *sampel*, di setpoint. Sebaiknya sensor setinggi cawan dan
  terlindung dari radiasi langsung.
- **Jalur panas ke load cell.** Lantai ruang ada tepat di atas load cell; panas yang
  merambat ke sana menjadi drift termal (§6). Perlu celah udara atau insulasi di bawah
  platform timbangan.
- **PETG** mulai melunak sekitar 80 °C. Permukaan luar harus diukur setelah alat dirakit
  dan dijalankan di 150 °C (batas atas pengaturan).

**PCB (JLCPCB 2 layer FR4, modul di socket) [KUNCI]** — breadboard terbukti menggeser nol
sampai puluhan mg saat jumper disentuh (SPESIFIKASI §8.2). Pedoman untuk bagian load cell
**[USUL]**:
- **Sambungan kabel load cell.** Empat kabel (E+, E−, S+, S−) masuk ke terminal sekrup atau
  konektor berkunci (mis. JST-XH) tepat di sebelah modul ADS1232, bukan header dupont.
- **Kontak S+/S− berdampingan.** Sinyalnya hanya mikrovolt, jadi tiap kontak tambahan adalah
  termokopel kecil. Kontak S+ dan S− dibuat berdampingan supaya suhunya sama dan EMF-nya
  saling meniadakan.
- **Routing.** S+/S− sebagai pasangan pendek dan simetris; E+/E− juga berpasangan.
- **Ground.** Lapisan bawah dijadikan ground plane utuh; jalur analog dijauhkan dari ESP32,
  SSR, dan kabel lampu AC.
- **Socket modul.** Untuk daya dan digital tidak masalah; pastikan header rapat dan tidak
  goyang.
- **Kabel load cell** berpelindung (shield ke GND di satu sisi) dan diikat supaya tidak
  bergerak.
- **Posisi PCB analog** jauh dari ruang pemanas, karena beda suhu di atas papan juga
  menimbulkan EMF termal.
- **Yang TIDAK diperbaiki PCB:** pemanasan awal elektronik, creep/histeresis load cell,
  dudukan mekanik, dan panas ruang yang merambat ke load cell. Ini tetap diuji lewat U2 dan
  blank run.

---

## 3. Alur operator dan fase siklus

| # | Fase | Masuk saat | Yang dilakukan alat | Ke |
|---|---|---|---|---|
| 1 | **SIAP** | alat menyala | Layar utama: Mulai / Hasil terakhir / Pengaturan | 2 |
| 2 | **PRE-HEAT** | operator menekan tombol Start | Lampu nyala, ruang (cawan kosong) dikontrol ke suhu pre-heat. **Lama pre-heat dihitung sejak tombol Start ditekan [KUNCI].** Kalau waktunya habis tetapi suhu belum masuk pita ±2 °C, alat menunggu sampai masuk **[USUL]**. Log per detik mulai | 3 |
| 3 | **TARE** | lama pre-heat habis | Tombol TARE tampil dan bisa ditekan kapan saja. Kalau berat stabil terus-menerus 1 menit, tare otomatis **[KUNCI]**. Nol disimpan di RAM, bukan NVS **[USUL]** | 4 |
| 4 | **MUAT SAMPEL** | tare selesai | LCD meminta: buka tutup, masukkan 5–10 g sampel, tutup. Tutup dibuka → lampu mati; tutup ditutup → lampu nyala lagi dan kontrol kembali ke suhu pre-heat **[KUNCI]** | 5 |
| 5 | **PULIH + W0** | tutup ditutup, ada beban | Tunggu suhu kembali ke setpoint pre-heat dan berat stabil, lalu W0 dicatat **[KUNCI]** (detail §5.3) | 6 |
| 6 | **HEAT** | langsung setelah W0 | Setpoint naik ke suhu heat. **t = 0 tepat di saat ini [KUNCI]**: LCD mulai menghitung detik dan menampilkan parameter. Fase A → Fase B (§5) | 7 |
| 7 | **STOP** | t ≥ t_stop (OK), t ≥ waktu maksimum (TIMEOUT), atau galat (ERROR) | Wf dikunci dari bacaan load cell saat lampu masih menyala, lalu lampu mati, lalu kadar air dihitung **[KUNCI]** | 8 |
| 8 | **PENDINGINAN + HASIL** | setelah stop | Lampu mati, layar hasil (§11). Log lanjut 3 menit **[USUL]**. Peringatan PANAS sampai suhu < 50 °C **[USUL]** | 1 |

**Keputusan yang terkait alur:**
- **Pre-heat tidak dihitung** dalam batas minimum/maksimum. Batas itu dihitung dari t = 0
  di awal fase HEAT **[KUNCI]**.
- **Tidak ada kalibrasi offset sensor suhu di dalam siklus [KUNCI].** Sensor suhu di-tuning
  sekali saat perakitan, lalu dianggap tetap (`RTD_RREF` + `RTD_OFFSET_C` di `pins.h`,
  SPESIFIKASI §8.1).
- **Tahap "80 °C selama X menit setelah sampel masuk"** dari rancangan awal **dihapus
  [KUNCI]**. Pre-heat hanya terjadi sebelum sampel masuk (dihitung dari tombol Start).
  Setelah sampel masuk, suhu turun karena tutup baru dibuka. Alat menunggu suhu kembali ke
  setpoint pre-heat, mencatat W0, lalu langsung naik ke suhu heat.
- **Kenapa W0 menunggu suhu pulih [KUNCI]:** saat tutup dibuka panas langsung keluar.
  Menunggu pulih membuat kondisi termal W0 sama dengan saat tare (suhu pre-heat, lampu
  menyala), sehingga pergeseran bacaan karena panas dan konveksi tidak masuk ke W0.
- **Konsekuensi yang perlu diukur [USUL]:** air yang menguap selama pemulihan tidak ikut
  terhitung, sehingga kadar air sedikit terlalu rendah. Contohnya 5 mg/menit × 2 menit =
  10 mg = 0,2 % pada sampel 5 g. Log per detik berisi berat sejak tutup ditutup, jadi
  besarnya bisa diukur di uji air yang diketahui (U6, §12). Kalau ternyata signifikan,
  W0 dipindah ke saat tutup ditutup.

---

## 4. Pengaturan dan nilai bawaan

### 4.1 Pengaturan operator — menu LCD, disimpan di NVS **[KUNCI]**

| Pengaturan | Bawaan | Batas bawah | Batas atas |
|---|---|---|---|
| Suhu pre-heat | **80 °C** | 50 °C | 150 °C |
| Lama pre-heat | **10 menit** | 1 menit **[USUL]** | 60 menit **[USUL]** |
| Suhu heat | **110 °C** | 80 °C | 150 °C |
| Waktu minimum heat | **3 menit** | 1 menit **[USUL]** | 25 menit **[USUL]** |
| Waktu maksimum heat (timeout) | **25 menit** | 5 menit **[USUL]** | 60 menit **[USUL]** |

- "EEPROM" di ESP32 adalah **NVS** (`Preferences`). Pengaturan disimpan di namespace sendiri
  (`hbcfg`), terpisah dari kalibrasi load cell (`heatbox`) **[USUL]**.
- Menu **"Reset Default"** mengembalikan kelima nilai di atas setelah konfirmasi. Menu ini
  **tidak** menyentuh kalibrasi load cell **[KUNCI fungsi, USUL nama]**.
- Validasi **[USUL]**:
  - suhu heat ≥ suhu pre-heat;
  - waktu minimum < waktu maksimum;
  - nilai di luar batas ditolak di menu;
  - isi NVS yang rusak dikembalikan ke bawaan;
  - pengaturan terkunci selama siklus berjalan.

### 4.2 Parameter metode — tetap di firmware, bukan menu operator

Parameter ini menentukan *cara mengukur*, jadi tidak ditaruh di menu operator. Nilainya
hanya diubah lewat serial atau mode servis **[USUL]**.

| Parameter | Nilai | Status |
|---|---|---|
| Ambang Fase A | laju ≤ 4 mg/menit | KUNCI |
| Konfirmasi Fase A | 3 menit berturut-turut | KUNCI |
| Target laju untuk prediksi | 1 mg/menit | KUNCI |
| Tambahan setelah t* | 2 menit | KUNCI |
| Pembacaan load cell dan log | tiap 1 detik | KUNCI |
| Tare otomatis | stabil terus-menerus 1 menit | KUNCI |
| Kriteria "stabil" untuk tare | laju drift nol (garis robust 3 menit terakhir) ≤ 0,5 mg/menit | USUL |
| Batas tunggu tare | 10 menit setelah pre-heat habis → ERROR (PREHEAT) | USUL |
| Suhu pre-heat belum masuk pita | lama pre-heat + 5 menit → ERROR (HEATER) | USUL |
| Pita "suhu tercapai" | ±2 °C dari setpoint | USUL |
| Kriteria W0 | suhu dalam pita ≥ 30 detik **dan** status berat STABLE | USUL |
| W0 | garis robust 30 detik terakhir, dibaca di t = 0 | USUL |
| Batas tunggu W0 | 5 menit setelah tutup ditutup → ERROR (SAMPEL) | USUL |
| Rentang berat sampel | 4–12 g sah; di luar 5–10 g diberi bendera peringatan | USUL |
| Wf | garis robust 60 detik terakhir, lampu masih menyala | USUL |
| Syarat "penyusutan nyata" | laju puncak sejak t = 0 ≥ 6 mg/menit (1,5 × ambang) | USUL |
| Menit sah | ≥ 40 dari 60 detik dipakai dan sebaran robust ≤ 10 mg | USUL |
| Toleransi berat naik antar menit | 3 mg | USUL |
| Jendela laju | 5 menit (Theil–Sen) | USUL |
| Batas τ fit | 0,3–30 menit | USUL |
| Jendela SSR | 2 detik, time-proportional | USUL |
| Batas suhu darurat | > setpoint + 15 °C atau > 165 °C → lampu mati, ERROR | USUL |

---

## 5. Pengolahan data

Satuan: massa mg, waktu menit sejak t = 0 (awal HEAT).

Angka noise di bawah adalah perkiraan *white noise*, dari SPESIFIKASI §6 (20,9 count =
3,8 mg per sampel mentah) dan CSV kalibrasi (SD 64 sampel 3,2–6,1 mg). Log 10 g yang akan
direkam (§12) memberi angka sebenarnya, termasuk noise frekuensi rendah yang tidak turun
saat dirata-rata.

### 5.1 Rantai pengolahan

```
ADC 10 SPS → gerbang spike 6σ (sudah ada di driver)
           → rata-rata per detik (sampel lolos gerbang)
           → nilai per menit (garis robust)
           → laju (Theil–Sen 5 menit)
           → Fase A → fit eksponensial → t_stop
```

| Tahap | Cara | Alasan |
|---|---|---|
| Per detik | Rata-rata sampel lolos-gerbang dalam detik itu (biasanya 10, bisa 9–11 karena osilator ADC ±3 %). Detik tidak sah kalau < 5 sampel atau status CLIP/NODATA/DOUTERR | Rata-rata adalah estimator paling efisien untuk white noise; spike sudah dibuang di driver. Angka `W` di report **tidak** dipakai karena rata-rata bergeraknya membuat data saling berkorelasi dan tertunda |
| Per menit | Lewat 60 nilai per detik: garis least squares → buang titik yang menyimpang > 3 × (1,4826 × MAD residual) → hitung ulang (2 putaran) → **baca garis di tengah menit** | Massa sedang turun di dalam menit itu. Rata-rata atau median menjadi bias kalau sebagian detik dibuang secara tidak simetris: di uji Python, IQM bias 3,4 mg sedangkan garis robust 0,15 mg (laju 40 mg/menit, tisu menekan 10 detik) |
| Menit sah | ≥ 40/60 detik dipakai, sebaran robust ≤ 10 mg, dan berat tidak naik > 3 mg dari menit sebelumnya | Sampai 1/3 menit boleh terganggu. Menit yang memuat perubahan beban punya sebaran ratusan mg (log 5 Okt 2026: 643 dan 4545 mg). Berat yang naik secara fisik tidak mungkin terjadi |
| Laju | Theil–Sen atas 5 menit sah terakhir (median dari 10 kemiringan antar-pasangan). Butuh ≥ 4 menit sah dari 5; kalau tidak, laju "tidak ada" dan penghitung 3 menit di-reset | Satu menit rusak tidak bisa menyeret hasil. Pada data yang sama, regresi biasa membalik tanda (−8 vs +10 mg/menit) |

**Perkiraan ketidakpastian:**

| Besaran | Rumus | Nilai |
|---|---|---|
| Per detik | σ_raw / √10 | 1,2–1,9 mg |
| Per menit | σ₁ₛ / √60 | 0,15–0,25 mg (angka perencanaan 0,5 mg) |
| Laju (5 titik) | σ_menit / √10 | 0,05–0,17 mg/menit |
| Drift nol untuk tare (garis 3 menit) | σ₁ₛ / √135 menit² | 0,10–0,16 mg/menit |

Ambang Fase A 4 mg/menit karena itu ≥ 24 kali di atas noise laju. Ancamannya adalah
drift, bukan noise.

Jendela SSR 2 detik membagi habis 60 detik, jadi tiap menit memuat jumlah siklus nyala/mati
lampu yang sama dan gangguan konveksi dari lampu ter-rata-rata sama.

### 5.2 Fase A — syarat terpicu

Fase A terpicu kalau **semua** syarat terpenuhi:
1. t ≥ waktu minimum heat (bawaan 3 menit);
2. suhu ruang sudah di pita suhu heat selama ≥ 1 menit;
3. ada ≥ 5 menit sah di jendela fit (§5.4);
4. laju puncak sejak t = 0 pernah ≥ 6 mg/menit (penyusutan benar-benar terjadi);
5. laju ≤ 4 mg/menit pada 3 evaluasi menit berturut-turut.

Syarat 2–4 mencegah trigger palsu di awal, saat laju masih kecil karena sampel belum
panas. Lag jendela laju (2 menit) membuat Fase A terpicu sedikit lebih lambat, tidak pernah
lebih cepat. Syarat "3 berturut-turut" dengan jendela yang tumpang-tindih terutama
melindungi dari satu menit yang terganggu.

### 5.3 W0 dan Wf

- **W0:** garis robust 30 detik terakhir, dibaca di t = 0, diambil saat suhu sudah pulih
  ≥ 30 detik dan status berat STABLE. Ketidakpastian ≈ 0,4–0,7 mg.
- **Wf:** garis robust 60 detik terakhir sebelum lampu dimatikan, dibaca di t_stop.
  Ketidakpastian ≈ 0,2–0,5 mg. Hasilnya lebih tenang dan tidak bias dibanding rata-rata
  beberapa detik, karena massa masih turun ~1 mg/menit. Kondisi konveksinya (lampu menyala)
  juga sama dengan data sebelumnya dan dengan blank run.
- Kalau koreksi blank (§6) tersedia, W0 dan Wf dikoreksi dengan kurva blank pada waktu
  masing-masing.

### 5.4 Model kurva untuk Fase B

**Dasar fisika:** pada periode laju menurun, penguapan dikendalikan difusi air di dalam
lapisan sampel. Solusi hukum Fick untuk lapisan setebal L adalah deret eksponensial.
Setelah beberapa menit, suku pertamanya mendominasi, sehingga massa mendekati satu
eksponensial (model Lewis / Henderson–Pabis).

```
m(t) = m∞ + A·e^{−(t−t0)/τ}
r(t) = −dm/dt = (A/τ)·e^{−(t−t0)/τ}               laju penyusutan
t*   = t0 + τ·ln(A / (τ·r*)),  r* = 1 mg/menit    (kalau A/τ ≤ r*: t* = t0)
t_stop = t* + 2 menit
sisa air saat stop = τ·r*·e^{−2/τ}                (τ = 5 → 3,4 mg; τ = 10 → 8,2 mg)
waktu dari Fase A ke t* = τ·ln 4 ≈ 1,39·τ
τ = 4L² / (π²·D)  →  τ ∝ L²
```

**τ ∝ L² penting untuk operasi:** sampel 10 g di cawan yang sama membuat lapisan 2 kali lebih
tebal, sehingga τ sekitar **4 kali lebih lama**. Berat sampel dan cara meratakannya harus
konsisten.

**Fitting di ESP32** (`lib/HeatboxCycle/src/DryingMath.cpp`, port `tuning_web/drying.py`):
- Untuk τ tetap, model linear dalam (m∞, A), jadi cukup persamaan normal 2×2.
- τ dicari dengan grid logaritmik lalu golden-section. Grid lebih dulu supaya tidak
  terjebak minimum lokal.
- Akumulator `double`, karena massa ~5000–10000 mg dan butuh resolusi 0,01 mg.
- Perkiraan biaya ~25 ms sekali per menit; harus diukur di board.

**Cross-check (A1):** garis least squares pada (t, ln laju) memberi τ = −1/kemiringan dan
t* secara tertutup. Lebih sederhana tetapi lebih bising karena memakai laju turunan.

| Alternatif yang ditolak | Alasan |
|---|---|
| Ekstrapolasi laju secara linear | Laju turun eksponensial, jadi target tercapai terlalu cepat → stop dini → sampel belum kering |
| Page `exp(−(t/τ)ⁿ)`, model dua suku | 4–5 parameter dari ~10 titik membuat ekstrapolasi tidak stabil. Jarak ekstrapolasi hanya ~1,4τ, jadi satu eksponensial cukup |
| Eksponensial + suku drift linear | Suku linear tertukar dengan pengeringan lambat; dalam 10–20 menit keduanya tidak bisa dibedakan |

**Jendela fit:** mulai = max(saat suhu heat tercapai + 1 menit, awal periode laju menurun);
akhir = saat Fase A terpicu. "Awal periode laju menurun" = menit terakhir yang lajunya ≥ 90 %
puncak. Data sebelum suhu heat tercapai tidak dipakai karena berisi pemanasan sampel dan
kenaikan 80 → 110 °C; satu eksponensial yang melintasi patahan itu membiaskan τ.

### 5.5 Kriteria fit layak — dicek otomatis oleh firmware

Fit **selalu** menghasilkan angka, bahkan untuk data rusak. Di uji Python, sampel yang tidak
mengering tetap memberi "waktu berhenti" yang terlihat meyakinkan. Karena itu hasilnya
dicek dengan aturan sederhana (rule-based) yang ringan untuk ESP32:

| Kode | Lolos kalau | Alasan |
|---|---|---|
| V1 | ≥ 5 titik sah di jendela fit | derajat bebas (3 parameter + 2) |
| V2 | A > 0; 0,3 ≤ τ ≤ 30 menit; τ tidak menempel di batas pencarian | τ menempel di batas = kurva belum melengkung |
| V3 | m∞ < massa terakhir; kadar air asimtot (W0 − m∞)/W0 dalam 0–25 % | masuk akal secara fisika |
| V4 | RMS residual ≤ max(3 × median ketidakpastian menit, 1,0 mg) | lebih jujur dari R² |
| V5 | laju model di titik terakhir dalam ±30 % dari laju terukur | model cocok dengan data terbaru |
| V6 | (t* − t_akhir) ≤ 2 × (t_akhir − t_mulai) | ekstrapolasi tidak terlalu jauh |
| V7 | \|t* model − t* A1\| ≤ max(2 menit, 25 %) | dua metode sepakat; gagal V7 saja hanya memberi bendera |

Kalau perangkat keras dan prosedur sudah benar, cek ini lolos diam-diam di hampir setiap
uji. Fungsinya jaring pengaman untuk kasus yang tidak normal: tutup dibuka, sampel tumpah,
sampel hampir kering, gangguan mekanis, atau kurva yang tidak eksponensial.

Selama Fase B, menit baru tetap dihitung dan dibandingkan dengan model (residu validasi).
Ini **tidak** mengubah waktu stop **[KUNCI]**, tetapi dipakai untuk bendera peringatan.

### 5.6 Anggaran ketidakpastian kadar air (sampel 5 g; untuk 10 g bagi dua)

| Sumber | Besar | Pengaruh ke kadar air |
|---|---|---|
| Noise W0 | 0,4–0,7 mg | 0,008–0,014 % |
| Noise Wf | 0,2–0,5 mg | 0,004–0,010 % |
| Error skala (span) | — | batal sendiri, karena kadar air adalah rasio |
| Error offset tare | — | batal di W0 − Wf |
| **Drift tanpa koreksi**, ~20 menit × 0,5–1,25 mg/menit | 10–25 mg | **0,2–0,5 % ← dominan** |
| Gaya apung (udara 80 → 110 °C: 1,000 → 0,921 mg/cm³; volume serbuk ~10–29 cm³) | +0,8–2,3 mg, sistematik | −0,02–0,05 % |
| Sisa air di endpoint (τ = 5–10) | 3,4–8,2 mg, sistematik | 0,07–0,16 % |
| Penguapan selama pemulihan sebelum W0 | belum diukur | sistematik, diukur di U6 |
| Creep saat sampel dimuat | belum diketahui (breadboard: +30 mg/menit sesaat setelah 500 mg) | perlu dudukan kaku |

---

## 6. Penanganan drift

**Komponen drift:**
1. Pemanasan sendiri elektronik + EMF termal: 14,2 mg/menit di awal nyala, menuju setimbang
   +0,3 g dalam ~2–4 jam.
2. Load cell ikut panas karena ruang dan lampu (besarnya belum diketahui, kemungkinan
   dominan).
3. Creep di bawah beban.
4. Gaya apung dan konveksi (bergantung duty lampu dan kenaikan suhu ke heat).

| # | Strategi | Status |
|---|---|---|
| S1 | Tare panas tepat sebelum sampel masuk | KUNCI |
| S2 | Tare hanya setelah drift nol stabil (§4.2): lama pre-heat ditambah gerbang drift | KUNCI (angka USUL) |
| S3 | **Blank run dengan beban inert** setara sampel (anak timbangan 5/10 g), ≥ 3–5 kali di hari berbeda → kurva rata-rata d̄(t) → m_koreksi(t) = m(t) − d̄(t), termasuk W0 dan Wf. Wadah kosong saja tidak menangkap creep beban | KUNCI (blank run), USUL (pakai beban) |
| S4 | **Uji keberulangan blank** sebagai syarat pakai: SD antar-run massa semu di menit 25 ≤ 1 mg, SD laju drift di jendela Fase A ≤ 0,3 mg/menit (angka awal). Kalau gagal, solusinya hardware (insulasi load cell, dudukan kaku), bukan koreksi | USUL |
| S5 | Uji "nol sesudah" untuk validasi: setelah tes, angkat sampel dan rekam nol lagi → drift run itu terukur langsung | USUL, khusus tuning |
| S6 | Sensor suhu di badan load cell; drift dimodelkan dari suhu load cell | USUL, fase 2 |

**Akibat pengaturan yang bisa diubah:** kurva blank hanya berlaku untuk pengaturan suhu dan
waktu yang dipakai saat merekamnya. Kurva disimpan bersama pengaturannya. Kalau pengaturan
siklus berbeda, siklus berjalan **tanpa koreksi blank** dan diberi bendera **[USUL]**.

**Kesimpulan:** blank run perlu, tetapi baru cukup kalau lolos S4. Karena itu urutan uji
(§12) menempatkan uji keberulangan blank **sebelum** sampel nyata.

**Bonus diagnostik:** saat lampu dimatikan di akhir, lompatan bacaan load cell mengukur
besar gaya konveksi/termal selama pemanasan. Itulah alasan log diteruskan 3 menit setelah
stop.

---

## 7. Status hasil dan kasus khusus

Status tetap tiga **[KUNCI]**: **OK / TIMEOUT / ERROR**. Ditambah **kode alasan** dan
**bendera peringatan** (mis. "OK ⚠ model menyimpang") supaya informasi tidak hilang
**[USUL]**.

| Kasus | Deteksi | Tindakan | Status |
|---|---|---|---|
| Laju kecil di awal (sampel belum panas) | — | Fase A ditahan oleh syarat §5.2 | — |
| Fase A tidak tercapai sampai waktu maksimum | t ≥ maks | Stop, Wf terukur, fit dicoba sebagai info | **TIMEOUT** (label "belum kering") |
| Penyusutan tidak pernah ≥ 6 mg/menit | laju puncak < syarat | Lanjut sampai waktu maksimum | **TIMEOUT** + bendera "penyusutan sangat kecil" |
| t* + 2 melewati waktu maksimum | saat Fase A | Lanjut sampai maksimum lalu stop; prediksi ditampilkan sebagai info | **TIMEOUT** |
| Fit tidak layak (V1–V6) | saat Fase A | Lampu lanjut sampai waktu maksimum **[USUL]**, Wf terukur | **ERROR (FIT)**, kadar air tampil berlabel "tidak tervalidasi" **[KUNCI]** |
| Spike / getaran singkat | gerbang spike + garis robust | Menit tetap sah kalau ≥ 40/60 detik dipakai | — |
| Gangguan panjang | menit tidak sah berulang | Menit dikeluarkan; kalau jendela fit tidak mencapai 5 titik sampai waktu maksimum | **ERROR (DATA)** |
| Tutup dibuka saat PRE-HEAT/TARE | limit switch | Lampu mati (hardware); waktu pre-heat berhenti; tare tidak bisa selama terbuka | — |
| Tutup dibuka saat HEAT | limit switch | Lampu mati; siklus dihentikan | **ERROR (TUTUP)**, tanpa kadar air |
| Wf menyimpang dari model | \|Wf − m(t_stop)\| > max(3 mg, 3σ prediksi) | Cek 3 menit terakhir: mulus = model meleset (Wf tetap sah); terganggu = Wf rusak | mulus → **OK + bendera**; terganggu → **ERROR (WF)** |
| Berat sampel di luar rentang | W0 < 4 g atau > 12 g | Tidak masuk HEAT | **ERROR (SAMPEL)** |
| Pre-heat / tare tidak siap | batas tunggu §4.2 | Lampu mati | **ERROR (PREHEAT)** atau **(HEATER)** |
| Sensor suhu fault/tidak wajar; load cell NODATA/DOUTERR/CLIP | status driver | **Lampu mati seketika** | **ERROR (SENSOR)** |
| Suhu berlebih / lampu tidak merespons | kontrol suhu | **Lampu mati seketika** | **ERROR (HEATER)** |
| ESP32 reset di tengah siklus | saat boot: log berstatus BERJALAN | Lampu mati (pin bawaan LOW), log ditandai | **ERROR (RESET)** |
| Dibatalkan operator | tombol | Lampu mati | **ERROR (BATAL)** |

**Keamanan [USUL]:**
- Tambahkan **pengaman suhu hardware** independen (thermal fuse/termostat bimetal seri
  dengan lampu). SSR yang rusak umumnya macet *nyala*, dan firmware tidak bisa menolong
  kalau itu terjadi.
- Input SSR diberi pull-down eksternal supaya lampu mati saat boot, reset, atau flashing.

---

## 8. Kritik metode proposal dan catatan diskusi

**Metode di proposal:** regresi 60 detik pada data per detik, ambang 1 mg/menit, 3 evaluasi
berturut-turut.
- **Terlalu bising untuk ambangnya.** SE kemiringan 60 detik = σ₁ₛ/√5 menit² ≈
  0,54–0,85 mg/menit, jadi S/N pada ambang 1 mg/menit hanya 1,2–1,9 dan keputusan akan
  berkedip.
- **Syarat 3 evaluasi tidak banyak menolong.** Kalau dievaluasi tiap detik, jendela-jendelanya
  hampir identik.
- **Data per detik dari `W` firmware berkorelasi**, sehingga SE sebenarnya lebih besar dari
  rumus di atas.
- **Cacat mendasar:** drift ~1 mg/menit sama besar dengan ambangnya. Alur A/B (4 mg/menit
  terukur, lalu ekstrapolasi ke 1 mg/menit) memperbaiki persis masalah ini.
- **Wf panas:** prinsipnya benar (tepung telur higroskopis, menimbang setelah dingin malah
  menyerap air). Tetapi tare dilakukan di suhu pre-heat dan Wf di suhu heat, jadi selisih
  termal dan gaya apung harus dikoreksi dengan blank di pengaturan yang sama.
- **Target 1 mg/menit** setara alat komersial (Mettler HB43: 1 mg per 20–180 detik).

**Catatan diskusi sebelumnya:**

| Catatan | Penilaian |
|---|---|
| Asumsi "semua massa hilang = air"; Maillard menambah air + volatil (hasil terlalu tinggi), kerak menjebak air (terlalu rendah) | Benar secara prinsip, besarnya belum diketahui. Maillard butuh gula pereduksi, dan banyak tepung telur komersial sudah *desugared* (cek ke pabrik). Lipid telur hampir tidak menguap di 110 °C. Kerak dikurangi lapisan tipis dan tahap pre-heat yang lebih rendah. Dikuantifikasi lewat perbandingan dengan oven (U8) dan uji ulang residu (U9) |
| Endpoint 1 mg/menit = sampel tidak 100 % kering | Benar. Sisa air 3,4–8,2 mg (0,07–0,16 % pada 5 g), sistematik dan konsisten kalau τ konsisten, sehingga terserap faktor koreksi. Kadar air asimtot (W0 − m∞)/W0 ditampilkan sebagai info |
| ±0,003 g di W0 dan Wf → ±0,085 % pada 5 g | Aritmetikanya benar (√2·3/5000), tetapi premisnya tidak tepat: error skala batal dalam rasio, dan setelah pengolahan per menit noise-nya 0,2–0,7 mg (±0,01–0,02 %). Kesimpulan "drift dominan" benar |
| Basis basah vs basis kering | Konfirmasi ke pabrik **[BUKA]**. Basis basah tampil utama; basis kering = MC/(1 − MC) di screen detail |
| Koreksi MC_terkoreksi = a·MC + b per pengaturan, divalidasi terhadap oven | Setuju. Syaratnya: ≥ 10–15 sampel yang mencakup rentang kadar air (kalau rentangnya sempit, pakai b saja), duplo di kedua metode, laporan bias + SD selisih (Bland–Altman), validasi ulang tiap kali hardware atau pengaturan berubah |

---

## 9. Arsitektur firmware

```
SensorInput (antarmuka)              HeaterOutput (antarmuka)
 ├─ HardwareSensors (Ads1232 + RtdSensor + saklar tutup)   ├─ SsrHeater (time-proportional 2 detik)
 └─ HeatBoxDummyData (simulasi fisik)                       └─ DummyHeater (menggerakkan model termal simulasi)
                 │
                 ▼
     lib/HeatboxCycle (C++ murni, tanpa Arduino.h → bisa diuji di PC)
       DryingMath (selesai) · SecondAggregator · MinuteBuilder · RateEstimator
       DryingFit · DriftProfile · TempController (PI) · CycleController (fase + status)
                 │
                 ▼
     HeatBoxDataSource (antarmuka baca-saja: fase, berat, laju, prediksi, hasil, titik menit)
       ├─ SerialReporter (report lama TIDAK diubah + baris CYC,...)
       ├─ LcdView (layar + menu)
       └─ TestLogger (LittleFS)
```

- **Satu sumber data untuk semua tampilan.** Serial, dashboard, dan LCD membaca snapshot yang
  sama dari `HeatBoxDataSource`; logika siklus tidak tahu tampilan apa yang dipakai.
- **`HeatBoxDummyData` ada di sisi masukan**: meniru sensor + pemanas (model termal, kurva
  pengeringan, noise, drift, creep, spike, tutup dibuka). Dengan begitu simulasi ikut
  menguji logika siklus, bukan hanya tampilan **[USUL]**.
- **Nol siklus berbasis raw count di RAM:** massa_mg = (raw − nol_raw) / cpg × 1000, dengan
  cpg dari NVS. `Ads1232` butuh aksesor baru untuk nilai sampel *lolos gerbang*; `raw()`
  sekarang mengembalikan nilai sebelum gerbang.
- **`lib/Heatbox` lama diganti** oleh `lib/HeatboxCycle`, bukan diperbaiki.

**Board [KUNCI]:** ESP32 DevKit V1. Kalau pin, flash, atau batas lain tidak cukup, pindah
ke **ESP32-S3** sebagai cadangan. Catatan: ESP32-S3 **tidak punya DAC**, sedangkan
composite video (`ESP_8_BIT_GFX`) memakai DAC di GPIO25. Pindah ke S3 berarti tampilan juga
diganti, mis. LCD RGB 480×272 seperti di proyek uji `LCD_testing`. Karena logika dipisah
dari tampilan, yang diganti hanya `LcdView`.

**Pin tambahan [USUL]** (sekarang masuk daftar "cadangan" di `include/pins.h`):

| Fungsi | GPIO | Catatan |
|---|---|---|
| SSR lampu | 22 (atau 21) | pull-down eksternal |
| Saklar tutup | 34 | input-only, pull-up eksternal |
| Tombol Start 22 mm | 35 | input-only; pull-up eksternal 10 kΩ + RC debounce karena kabelnya ke panel |
| Composite video | 25 | DAC1 |
| Sensor suhu load cell (opsional) | 21 | 1-Wire |
| **Hindari** | 14 | mengeluarkan PWM saat boot → lampu berkedip |

**Serial dan dashboard [USUL]:**
- Format report lama dan baris `CALZERO`/`CALPT` dipertahankan, karena dashboard
  bergantung padanya.
- Baris baru `CYC,...` (tanpa `# `) membawa fase dan **estimasi waktu stop**. Setelah LCD
  ada, dasbor cukup menampilkan estimasi waktu stop dan mengambil log **[KUNCI]**.
- **Sebelum LCD ada, dasbor menjadi panel operator sementara**: tombol Start / Tare / Batal,
  fase, status, dan estimasi stop. Siklus penuh diuji dari dasbor dulu; rotary, LCD, dan
  tombol Start dikerjakan bersamaan belakangan **[KUNCI]**.
- Perintah baru memakai huruf yang belum dipakai: `l` daftar log, `d <id>` ambil log,
  `y ...` kontrol siklus.
- Panel siklus PC (`tuning_web/cycle.py`) dipensiunkan dari UI karena tombolnya bentrok
  dengan siklus firmware. Kodenya disimpan sebagai acuan uji.

---

## 10. Log di flash (LittleFS)

**Kapasitas:** partisi data bawaan ±1,4 MB. LittleFS sudah bawaan core 2.0.14; tidak
perlu ganti ESP32.

**Format biner [USUL]:**
- Record per detik, 8 bait:
  - `int32` raw count rata-rata × 10 (resolusi 0,018 mg; raw dipertahankan supaya data
    tetap berguna kalau rumus konversi berubah)
  - `int16` suhu × 100
  - `uint8` duty lampu (0,5 %)
  - `uint8` bendera: fase, detik tidak sah, spike, tutup terbuka
- Header 128 bait: ID tes, pengaturan, cpg, nol raw, versi firmware, waktu kalau ada.
- Tabel per menit 16 bait/menit, hasil 128 bait (W0, Wf, kadar air, status, kode, t_A, t*,
  t_stop, parameter fit, koreksi blank, CRC32).

| | Ukuran |
|---|---|
| Satu tes (pre-heat 10 + tare + muat + heat ≤ 25 + 3 menit setelah stop) | ±45 menit ≈ 22 KB |
| Batas berkas per tes | 60 menit ≈ 29 KB |
| Kapasitas | ±40 tes |
| Mode tuning opsional (raw 10 SPS penuh) | 72 KB per 30 menit |

**Manajemen [USUL]:**
- Nama berkas `/log/t00042.bin`, ID dari penghitung di NVS.
- Rotasi: sebelum tes baru, hapus yang tertua sampai jumlah ≤ 40 dan ruang bebas ≥ 128 KB.
- Ditulis sekali per menit (buffer RAM 480 bait), jadi maksimal 60 detik data hilang kalau
  daya putus. Header ditulis di awal dengan status BERJALAN; saat boot, log yang belum
  selesai ditandai ERROR (RESET).
- Keausan flash: ~15 blok per tes tersebar oleh wear leveling di ~350 blok, endurance
  100 rb siklus. Dengan 20 tes/hari umurnya > 50 tahun.
- **Risiko yang harus diverifikasi:** menulis atau menghapus flash menghentikan cache CPU
  sebentar, sehingga satu sampel ADS bisa terlewat atau satu frame video bergetar. Karena
  itu penulisan dibatasi di pergantian menit.

**Mengambil log ke PC [USUL]:**
- Perintah `d <id>` mengalirkan CSV teks: `LOGH,` (header), `LOG,` (per detik), `LOGM,`
  (per menit), `LOGE,<n>,<crc32>` (penutup). Butuh ±6–11 detik per tes di 115200 baud.
- Dashboard diberi tombol "Ambil log tes" yang menyimpan ke `tuning_web/data/test_<id>.csv`
  dan memeriksa jumlah baris + CRC.

---

## 11. Tampilan LCD dan menu

**Layar:** composite video 256×240 lewat `ESP_8_BIT_GFX` (sudah dicoba di proyek "Heatbox
LCD"). Jenis LCD belum final.

**Navigasi [KUNCI]:** siklus dimulai dengan tombol Start terpisah. Putar rotary untuk pindah
screen; screen utama tampil pertama. **[USUL]:** tekan rotary singkat untuk kembali ke screen
utama, tekan lama untuk membuka menu.

**Screen hasil:**

| # | Screen | Isi |
|---|---|---|
| 1 | **Utama** (pertama) | **KADAR AIR** angka besar (mis. `4.83 %`), "basis basah", status OK/TIMEOUT/ERROR (+ ⚠ bila ada bendera), durasi, ID tes |
| 2 | Grafik penyusutan | % susut per menit + kurva fit + ekstrapolasi + penanda (suhu heat tercapai, Fase A, stop) |
| 3 | Berat | W0, Wf, massa hilang (mg), koreksi blank, kadar air basis kering |
| 4 | Waktu & status | durasi heat, waktu Fase A, t* prediksi, stop aktual, kode alasan, daftar peringatan |
| 5 | Fit & prediksi | τ, kadar air asimtot, RMS, selisih model vs A1, residu Wf vs model |
| 6 | Suhu | kurva suhu vs setpoint, overshoot maksimum, rata-rata di suhu heat |
| 7 | Info | ID log, versi firmware, petunjuk ambil log lewat USB |

**Saat siklus berjalan:** fase, waktu berjalan sejak t = 0, % susut langsung, laju, suhu,
hitung mundur estimasi stop. Saat PRE-HEAT: suhu dan sisa waktu pre-heat. Saat TARE: status
stabil + tombol TARE.

**Menu pengaturan:** lima pengaturan §4.1 (putar = ubah nilai, tekan = simpan) dan
**Reset Default** dengan konfirmasi.

```
┌──────────────────────────────┐   ┌──────────────────────────────┐
│ HASIL #0042              1/7 │   │ PENYUSUTAN %             2/7 │
│          KADAR AIR           │   │ 5┤                 · · ×     │
│          4.83 %              │   │  │          ● ● ● ●          │
│        basis basah           │   │  │     ● ● ●                 │
│                              │   │  │  ● ●                      │
│  [ OK ]        durasi 18:42  │   │ 0┼●──┬────┬────┬────┬──      │
│  ● ○ ○ ○ ○ ○ ○               │   │  0   5   10   15   20 menit  │
└──────────────────────────────┘   │    ▲110   ▲A    ▲stop        │
                                   └──────────────────────────────┘
```

- **Grafik:** sumbu-Y memakai % susut = (W0 − M)/W0 × 100, sehingga nilai akhirnya sama dengan
  angka di screen utama. ≤ 25–30 titik di area ~220 px masih terbaca.
- **Batasan composite:** garis minimal 2 px dengan warna kontras tinggi; angka penting minimal
  teks ukuran 2 (12×16 px). Teks ukuran 1 kemungkinan kabur dan perlu diuji di monitornya.
- **NTSC vs PAL** ditentukan dari parameter konstruktor library (Indonesia umumnya PAL).
- **RAM:** framebuffer composite ±120 KB (perlu diverifikasi); firmware sekarang memakai 23 KB.

---

## 12. Rencana uji

| # | Uji | Diukur | Lolos (angka awal) |
|---|---|---|---|
| U0 | Unit test port C++ vs Python | selisih angka | ≤ 1e-6 relatif — **selesai** |
| U1 | Simulasi `HeatBoxDummyData`, semua skenario §7 | t_stop, kadar air, status | t_stop meleset ≤ max(1 menit, 10 %), kadar air ≤ 0,02 % abs, status benar, trigger palsu < 1 % |
| U2 | **Baseline load cell (log 10 g)** | σ per detik, deviasi Allan, σ per menit, drift | σ per menit ≤ 0,5 mg |
| U3 | Pemanas + suhu, ruang kosong (butuh sensor suhu benar) | waktu naik, overshoot, kestabilan, interlock | overshoot ≤ 3 °C, stabil ±1 °C, galat mematikan lampu |
| U4 | Blank cawan kosong ×3 | drift nol sepanjang siklus | data untuk S2 |
| U5 | **Blank beban inert ×3–5, hari berbeda** | d̄(t), keberulangan | S4. **Kalau gagal: perbaiki hardware dulu** |
| U6 | Air yang diketahui: pembawa inert + 300–500 mg air ditimbang | recovery = susut / air; penguapan selama pemulihan; t* vs laju aktual | recovery 98–102 % |
| U7 | Sampel nyata, satu batch, ≥ 5 ulangan (5 g dan 10 g) | repeatability | SD ≤ 0,1 % abs |
| U8 | Dibandingkan oven acuan, ≥ 10–15 sampel | a, b, Bland–Altman | disepakati dengan atasan |
| U9 | Uji ulang residu kering | susut semu = volatil non-air + drift | mengukur efek Maillard/volatil |

**Log 10 g untuk U2** (sedang ditunggu):
1. Nyalakan dari dingin. Rekam lewat `pio device monitor -b 115200 -f log2file`, atau lewat
   dashboard lalu **Unduh** (untuk > 2,7 jam set `TUNING_LOG_MAX=60000`).
2. Biarkan kosong 15 menit, `t`, letakkan 10 g, lalu jangan disentuh minimal 2 jam
   (4 jam lebih baik).
3. Angkat beban dan rekam nol 10 menit.
4. Catat suhu ruang di awal dan akhir.

Mode report biasa sudah cukup: kolom `raw` adalah sampel mentah tiap 0,5 detik.

---

## 13. Status implementasi

| Langkah | Isi | Status |
|---|---|---|
| 0 | Analisis log 10 g → finalkan σ dan ambang | menunggu log |
| 1 | `lib/HeatboxCycle/src/DryingMath.*`: nilai per menit, Theil–Sen, fit eksponensial, prediksi stop, cross-check A1 | **selesai** — 10 uji cocok dengan `drying.py` (≤ 1e-6), uji mutasi menangkap salah port, terkompilasi untuk ESP32 tanpa warning (`test/README.md`) |
| 2 | `SensorInput`, `HardwareSensors` (sampel lolos-gerbang dari `Ads1232::accepted()`), `SecondAggregator`, `MinuteBuilder`; baris `MNT` tiap menit sejak boot | **selesai** — 13 uji lolos (`test\run_msvc.bat`), firmware ter-build untuk ESP32 tanpa warning baru (RAM 7,3 %). Ikut diperbaiki: filter angka W yang lambat setelah beban berubah (SPESIFIKASI §4.2) |
| 3 | `HeatBoxDummyData`: mode simulasi penuh (berat + suhu + pemanas) dan mode hybrid (load cell asli, suhu dan pemanas disimulasikan) untuk uji penguapan di suhu ruang sebelum pemanas dan sensor suhu siap **[USUL]** | belum |
| 4 | `CycleController` + `HeatBoxDataSource` + baris `CYC`; dasbor menjadi panel operator sementara (Start / Tare / Batal, fase, status, estimasi stop) | belum |
| 5 | Logger LittleFS + ambil log | belum |
| 6 | Pemanas: SSR, kontrol PI, interlock, saklar tutup | belum |
| 7 | Blank run + koreksi drift | belum |
| 8 | LCD: screen live, screen hasil, menu pengaturan | belum |
| 9 | Uji hardware U2–U9, tuning, pembaruan dokumen | belum |

Setelah langkah 4, siklus penuh dijalankan beberapa kali dari dasbor. Rotary, LCD, dan tombol
Start (langkah 8) baru dikerjakan setelah siklusnya terasa benar **[KUNCI]**.

---

## 14. Pertanyaan terbuka

Semua pertanyaan yang memblokir sudah dijawab (5 Oktober 2026). Sisanya bisa dijawab
sambil jalan:

1. Batas bawah/atas lama pre-heat dan waktu minimum/maksimum (usulan di §4.1).
2. Perkiraan kadar air tepung telur; produk *desugared* atau tidak; basis basah dari pabrik?
3. Diameter cawan (menentukan tebal lapisan, jadi τ dan lama tes).
4. Perlu RTC supaya hasil dan log bertanggal tanpa PC?
5. Uji unit: pasang gcc untuk `pio test -e native`, atau cukup MSVC + uji di ESP32?
6. Akses ke oven acuan untuk menentukan a dan b?
7. Pengaman suhu hardware (thermal fuse) bisa ditambahkan?
8. Kalau fit tidak layak, apakah lampu memang lanjut sampai waktu maksimum (usulan bawaan),
   atau langsung berhenti?

---

## 15. Riwayat

- **28 September 2026:** siklus versi PC (`tuning_web/cycle.py`, `drying.py`) — cut-off
  10 mg/menit, target 0,1 mg/menit, tanpa pemanas, 13 uji lolos. Masih ada sebagai alat
  bantu transisi dan sebagai implementasi acuan untuk uji port C++.
- **5 Oktober 2026:** rancangan siklus di firmware disetujui. Keputusan alur, pengaturan,
  board, dan sensor suhu dari pemilik proyek dicatat di dokumen ini. Langkah 1 selesai.
