# Keputusan UI — LCD composite 256×240 (Prompt 1)

Catatan keputusan yang diambil saat implementasi, terutama yang **menyimpang dari desain**
(`LCD_UI_Design.html` / `LCD_UI_design.pdf`). Aturan prompt menang atas desain kalau bentrok.
Pratinjau semua layar: [`pratinjau/`](pratinjau/) (dirender dari kode yang sama, `tools/ui_render`).

| Penanda | Arti |
|---|---|
| **[SETUJU]** | sudah disetujui pemilik proyek (jawaban FASE 0, 4–5 Nov 2026) |
| **[SENDIRI]** | keputusan kecil implementasi; boleh diubah |
| **[UJI]** | masih diragukan; diuji di monitor lewat SERVIS > UJI TAMPILAN |

## 1. Kanvas, teks, warna

| # | Keputusan | Alasan |
|---|---|---|
| 1.1 | Batas karakter **T1 40, T2 20, T3 13, T4 10** (bukan 42/21/14/10) **[SETUJU]** | 42/21/14/10 hanya pas untuk lebar penuh 256 px; dengan margin 6 px lebar isi 244 px. `Painter::text()` memotong otomatis di margin kanan |
| 1.2 | Semua teks **KAPITAL** **[SETUJU]** | PDF desain menampilkan kapital; huruf kecil 5×7 lebih kabur di composite |
| 1.3 | Simbol dari cp437 (`cp437(true)`): ° ± · → √(✓) ◄ ► ≥ **[SENDIRI]** | Sudah ada di font bawaan; ✕ tidak ada → ikon silang / "X"; ‹ → "<" |
| 1.4 | Palet sama persis dengan desain (dicek: 0xFF 0x00 0x49 0x92 0x02 0x10 0xFC 0xA0 0xF0) | — |
| 1.5 | Zona PARKING x 156–255, y 0–42 **dipaksa putih setelah setiap gambar** (`Painter::clearParking`) **[SENDIRI]** | Aturan mutlak; perender PC juga memeriksa tiap piksel zona di semua layar |
| 1.6 | Garis zona PARKING di UJI TAMPILAN digambar **tepat di luar** zona (x 155, y 43) **[SETUJU]** | Zona tetap putih |
| 1.7 | Gambar ulang hanya saat tampilan berubah (hash isi layar), maks ~10 fps; tiap render menggambar layar penuh lalu `waitForFrame()`; `copyAfterSwap = false` **[SENDIRI]** | Double buffer tanpa memcpy 60 KB per frame |

## 2. Ikon

| # | Keputusan | Alasan |
|---|---|---|
| 2.1 | Ikon besar **16×8** diambil piksel-per-piksel dari desain **[SETUJU]** | Di desain kotaknya 16×16 tetapi gambarnya hanya 8 baris teratas |
| 2.2 | Ikon kecil **8×8 digambar ulang** **[UJI]** | Desain tidak punya bitmap 8×8 asli (hanya diperkecil browser). Dinilai di UJI TAMPILAN halaman 4 |
| 2.3 | Ikon START dipertahankan (dipakai di menu MULAI TES sebagai tanda "putar/mulai") **[SENDIRI]** | Tombol START fisik belum dipakai; ikonnya tetap berguna |

## 3. Komponen

| # | Keputusan | Alasan |
|---|---|---|
| 3.1 | Item terpilih: **blok biru teks putih + ">"** (bawaan) **[UJI]** | Teks putih di atas biru bisa kabur di composite (bandwidth warna). Varian B blok hitam, C bingkai tebal; pilih di UJI TAMPILAN halaman 7 (berlaku langsung sampai restart) |
| 3.2 | Teks sekunder abu 0x49 (bawaan) **[UJI]** | Bisa terlalu pudar di T1; varian hitam di UJI TAMPILAN halaman 8 |
| 3.3 | Bar tahap tes **244 px = 5 kotak 48 px + 4 celah 1 px**, tinggi 28, label PRE/TARE/ISI/PANAS/HASIL **[SETUJU]** | Desain hanya ±82 px sehingga label terpotong. Selesai = blok hitam + √, sekarang = blok biru + bingkai hitam 2 px, belum = garis abu 1 px → beda tanpa warna |
| 3.4 | Dialog dilebarkan ke **x 10, lebar 236** (desain 18, 220) **[SENDIRI]** | Judul T2 sampai 18 karakter muat ("SIKLUS BELUM AKTIF"). Tetap di bawah zona PARKING (y ≥ 62) |
| 3.5 | Kotak cek 10×10: centang/silang digambar per piksel 6×6 di dalam bingkai **[SENDIRI]** | Karakter √ 5×7 terlalu sesak di dalam kotak |
| 3.6 | Badge, banner, kotak nilai: status selalu teks/ikon + warna; kuning hanya sebagai blok berisi teks hitam | Aturan desain |
| 3.7 | Banner "RUANG MASIH PANAS" muncul > 50 °C, hilang < 48 °C **[SETUJU]** | Histeresis supaya tidak berkedip |

## 4. Layar

| # | Keputusan | Alasan |
|---|---|---|
| 4.1 | **Boot**: baris cek muncul berurutan (min 300 ms per baris); pintu selalu ✓ dengan keadaan di kanan (TERTUTUP/TERBUKA); WiFi dan jam diberi batas **8 dtk** sejak boot **[SENDIRI]** | Pin pintu selalu terbaca; boot tidak boleh macet menunggu WiFi |
| 4.2 | Boot gagal sensor → banner merah 2 dtk → layar **SENSOR GAGAL** (template 02-11b, judul "SENSOR GAGAL" bukan "TES GAGAL"): **TEKAN = cek ulang, TAHAN 2 dtk = Beranda** (MULAI TES ditolak dialog) **[SETUJU]** | Menu servis tetap bisa dipakai walau sensor gagal |
| 4.3 | Catatan offline (WiFi/jam gagal) = blok kuning 2 baris "JAM BELUM SINKRON / TES TETAP BISA JALAN", 2,5 dtk, lalu Beranda **[SETUJU]** | — |
| 4.4 | Jam: `*--:--` sampai NTP pernah sinkron sejak boot; setelah itu jam berjalan tanpa `*` walau WiFi putus **[SENDIRI]** | Tanpa RTC, jam sebelum NTP tidak diketahui |
| 4.5 | **Beranda**: badge SIAP (atau SENSOR GAGAL), kartu profil aktif, menu MULAI TES/RIWAYAT/PENGATURAN, "HASIL TERAKHIR: BELUM ADA TES" (logger = Prompt 2) | — |
| 4.6 | **Pilih Profil**: daftar 4 baris 18 px (PROFIL 1–3 + "< KEMBALI"); profil aktif ditandai "√AKTIF"; ringkasan T1; checklist **cawan = kotak kosong** (pengingat, tidak ada sensornya), **pintu = dari saklar** **[SENDIRI]** | Desain 66 px tidak muat 4 baris; desain mencentang cawan padahal tidak terukur |
| 4.7 | Pilih Profil: **TEKAN = jadikan aktif (simpan NVS)**, **TAHAN 2 dtk = simpan + coba mulai**, di "< KEMBALI": TEKAN = kembali, TAHAN tidak berbuat apa-apa (bar tidak muncul) **[SETUJU]** | — |
| 4.8 | Mulai saat pintu terbuka → dialog PINTU TERBUKA ("TUTUP PINTU DULU, LALU TAHAN LAGI"); dialog **menutup sendiri** saat pintu ditutup, **tes tidak mulai otomatis** **[SETUJU]** | — |
| 4.9 | Mulai saat pintu tertutup → dialog "SIKLUS BELUM AKTIF / SIKLUS PENGERINGAN DIBUAT DI TAHAP BERIKUT" | Siklus = Prompt 2 |
| 4.10 | **Pengaturan**: menu PROFIL 1/2/3, UMUM, **SUARA: NYALA/MATI**, RESET DEFAULT **[SENDIRI, diizinkan]** | Toggle suara diusulkan prompt; disimpan di blob hbcfg yang sama |
| 4.11 | **Edit nilai** (beda dari desain): PUTAR pilih kotak → TEKAN mulai edit → PUTAR ubah (putar cepat = langkah 5) → TEKAN simpan → TAHAN 2 dtk batal edit; TAHAN di luar edit = kembali **[SETUJU]** | Desain hanya "PUTAR:ubah TEKAN:simpan LAMA:batal" untuk dua/tiga nilai sekaligus |
| 4.12 | Edit profil: **dua kotak bertumpuk selebar layar** (desain berdampingan 116 px) **[SETUJU]** | Label "SUHU PRE-HEAT · 50-150" (132 px) + nilai T3 + ◄► tidak muat di 116 px |
| 4.13 | Nilai ditolak → kotak kuning + "!" + banner kuning ("HEAT HARUS ≥ PRE-HEAT", "MIN HARUS < MAKS"); nilai tidak tersimpan; tetap di mode edit **[SETUJU]** | Nilai di luar batas dicegah dengan membatasi putaran di min/maks |
| 4.14 | Reset default: dialog konfirmasi BATAL/RESET (pilihan awal BATAL); semua profil 80/110 °C, 10/3/25 mnt, profil aktif 1; **kalibrasi dan setelan suara tidak disentuh** **[SENDIRI]** | Suara bukan parameter proses |
| 4.15 | Varian terkunci (tes berjalan): semua item bergembok + banner "TERKUNCI SAAT TES"; TEKAN = bunyi peringatan. Logika siap, belum bisa terjadi | `testRunning` selalu false sampai Prompt 2 |
| 4.16 | **Servis**: TAHAN 5 dtk di Beranda **langsung ke menu** (tanpa layar 03-40.0) **[SETUJU]**; isi sementara UJI TAMPILAN, UJI INPUT, UJI SUARA | Menu servis lengkap = Prompt 2 |
| 4.17 | UJI TAMPILAN 8 halaman: palet + badge + bar tahap + banner, teks T1–T4, ikon 16×8, ikon 8×8, margin & PARKING, overscan, varian pilihan, varian teks abu. Varian **terarah**: hanya 2 hal yang masih diragukan (3.1, 3.2) + ikon 8×8 **[SETUJU]** | — |
| 4.18 | Halaman overscan tanpa baris petunjuk (pola sampai ke tepi); TAHAN tetap kembali | — |

## 5. Kontrol

Skema lengkap: [KONTROL-ROTARY.md](KONTROL-ROTARY.md). Semua konstanta waktu di
`include/app_config.h` (`app::input`).

| # | Keputusan | Alasan |
|---|---|---|
| 5.1 | TEKAN < 0,6 dtk; bar progres mulai **0,6 dtk** (sama dengan batas TEKAN) **[SETUJU]** | "Bar terlihat" berarti TEKAN sudah tidak mungkin terjadi |
| 5.2 | Aksi TAHAN tepat di batasnya (2 dtk / 5 dtk servis / 3 dtk batal tes di Prompt 2), lalu tombol harus dilepas dulu | — |
| 5.3 | Putar sambil menekan = **batal keduanya** (putaran diabaikan, tidak ada TEKAN/TAHAN setelah dilepas) **[SETUJU]** | — |
| 5.4 | Decoder: langkah keluar hanya saat klik penuh selesai (4 transisi), gerak setengah dibuang tanpa langkah **[SENDIRI]** | Putar kanan tidak pernah memberi langkah mundur; diam tidak pernah memberi langkah |
| 5.5 | Suara bawaan: TEKAN = klik, TAHAN berhasil = dua bip; layar boleh menimpa (konfirmasi, error, peringatan) **[SENDIRI]** | Satu tempat di `Ui::handle` |

## 6. Suara

| # | Keputusan | Alasan |
|---|---|---|
| 6.1 | Buzzer **pasif** lewat LEDC (bukan `tone()`), sequencer non-blocking di task input 1 kHz; semua suara di satu tabel `src/sounds.h` **[SETUJU]** | — |
| 6.2 | Langkah suara = nada datar, glide eksponensial, atau nada terpotong (gate) → suara tidak datar **[SETUJU]** | Teknik terinspirasi efek suara OttoDIY; semua melodi karya sendiri, tidak ada kode/nilai yang disalin |
| 6.3 | Setelah tiap suara LEDC dilepas dan GPIO12 dipaksa OUTPUT LOW; pin LOW sebelum suara pertama **[SETUJU]** | Strapping MTDI |
| 6.4 | Suara baru menggantikan suara yang sedang bunyi | Respons input tidak tertunda |

## 7. Arsitektur & lain-lain

| # | Keputusan | Alasan |
|---|---|---|
| 7.1 | Core 1: ISR video, task input (prioritas 3), UI di `loop()`. Core 0: task sensor (prioritas 4) + WiFi **[SETUJU]** | Critical section ADS1232 ±150 µs hanya mematikan interupsi core 0; ISR video punya 2 buffer baris (63,5 µs/baris NTSC) |
| 7.2 | Logika tidak tahu tampilan: layar membaca `UiSnapshot` dan bertindak lewat `UiCommands` | RANCANGAN §9 |
| 7.3 | FASE 2 dan 3 di-commit bersama | Kerangka layar (`Ui`, `Screen`) dan layar-layarnya saling bergantung; commit terpisah tidak bisa di-build |
| 7.4 | Versi library dikunci ke versi yang terbit sebelum tes LCD 28 Okt 2026: ESP_8_BIT 1.3.2, Adafruit GFX 1.13.0, BusIO 1.17.4 | `pio pkg list` tidak bisa dijalankan di sesi cloud; **cek sekali saat build pertama di PC** |
| 7.5 | Native unit test tanpa `USE_CURVE_FIT` / `test/support` (milik suite lama yang diarsipkan) | — |

## 8. Disepakati untuk Prompt 2 (belum dikerjakan)

| # | Topik | Keputusan pemilik (5 Nov 2026) |
|---|---|---|
| 8.1 | OTA | ArduinoOTA dengan password dulu (password di `include/secrets.h` → `OTA_PASS`, tidak di-commit). Nanti diganti lewat aplikasi IoT |
| 8.2 | Input WiFi | Daftar SSID hasil scan, lalu password diketik **satu karakter per kolom** (bukan papan huruf penuh): karakter aktif berkedip; PUTAR = ganti karakter urut; TEKAN 1× = ganti a-z ↔ A-Z; TEKAN 2× = angka & simbol; TAHAN di karakter = kunci karakter itu, lanjut ke kolom berikutnya. Disimpan di NVS `hbnet` |
| 8.3 | Servis timbangan | Kalibrasi di layar servis menggantikan dasbor, tetapi **versi minimal dulu**: TARE, KALIBRASI 1 TITIK, dan beberapa yang penting saja. Multi-titik lengkap (03-40.4) belakangan |
