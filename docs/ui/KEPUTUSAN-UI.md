# Keputusan UI — LCD composite 256×240

Catatan keputusan yang diambil saat implementasi, terutama yang **menyimpang dari desain**
(`LCD_UI_Design.html` / `LCD_UI_design.pdf`). Aturan prompt menang atas desain kalau bentrok.
Pratinjau semua layar: [`pratinjau/`](pratinjau/) (dirender dari kode yang sama, `tools/ui_render`).

| Penanda | Arti |
|---|---|
| **[SETUJU]** | sudah disetujui pemilik proyek (jawaban FASE 0, 4–5 Nov 2026) |
| **[SENDIRI]** | keputusan kecil implementasi; boleh diubah |
| **[UJI]** | dulu diragukan dan diuji di monitor lewat SERVIS > UJI TAMPILAN (layar itu dihapus 27 Nov 2026, lihat §10) |

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
| 1.8 | Header turun: status (ikon WiFi + jam + pintu) y 6 → **16**, judul y 20 → **28**; boot "HEAT BOX" y 18 → 20; UJI TAMPILAN judul "TAMPILAN n/8" (label halaman y 38 dihapus), hal. 5 judul y 12 → 22 **[SETUJU]** | Uji LCD 6 & 26 Nov 2026: monitor memotong ±10–12 baris atas, jam tidak terlihat. Kiri/kanan/bawah utuh. Semua layar baru mengikuti batas ini (isi mulai y ≥ 44) |

## 2. Ikon

| # | Keputusan | Alasan |
|---|---|---|
| 2.1 | Ikon besar **16×8** diambil piksel-per-piksel dari desain **[SETUJU]** | Di desain kotaknya 16×16 tetapi gambarnya hanya 8 baris teratas |
| 2.2 | Ikon kecil **8×8 digambar ulang** **[UJI]** | Desain tidak punya bitmap 8×8 asli (hanya diperkecil browser). Dinilai di UJI TAMPILAN halaman 4 |
| 2.3 | Ikon START dipertahankan (dipakai di menu MULAI TES sebagai tanda "putar/mulai") **[SENDIRI]** | Tombol START fisik belum dipakai; ikonnya tetap berguna |
| 2.4 | Ikon 16×8 **WIFI-X, GEAR, SERVIS digambar ulang** (busur WiFi + X, roda gigi 8×8, kunci pas) **[SENDIRI]** | Bitmap desain aslinya memang tidak terbaca (SERVIS berbentuk X seperti SILANG); dikeluhkan di uji LCD 6 Nov |

## 3. Komponen

| # | Keputusan | Alasan |
|---|---|---|
| 3.1 | Item terpilih: **bingkai tebal + ">"** (bawaan sejak 26 Nov; dipilih Stephen di uji LCD 6 Nov) **[SETUJU]** | Teks putih di atas biru bisa kabur di composite (bandwidth warna). Varian B blok hitam, C bingkai tebal; pilih di UJI TAMPILAN halaman 7 (berlaku langsung sampai restart) |
| 3.2 | Teks sekunder **hitam** (bawaan sejak 26 Nov; abu 0x49 terlalu pudar di monitor) **[SETUJU]** | Bisa terlalu pudar di T1; varian hitam di UJI TAMPILAN halaman 8 |
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
| 4.10 | **Pengaturan**: menu PROFIL 1/2/3, UMUM, **SUARA: NYALA/MATI**, RESET DEFAULT **[SENDIRI, diizinkan]** — diganti §10.2 | Toggle suara diusulkan prompt; disimpan di blob hbcfg yang sama |
| 4.11 | **Edit nilai** (beda dari desain): PUTAR pilih kotak → TEKAN mulai edit → PUTAR ubah (putar cepat = langkah 5) → TEKAN simpan → TAHAN 2 dtk batal edit; TAHAN di luar edit = kembali **[SETUJU]** | Desain hanya "PUTAR:ubah TEKAN:simpan LAMA:batal" untuk dua/tiga nilai sekaligus |
| 4.12 | Edit profil: **dua kotak bertumpuk selebar layar** (desain berdampingan 116 px) **[SETUJU]** | Label "SUHU PRE-HEAT · 50-150" (132 px) + nilai T3 + ◄► tidak muat di 116 px |
| 4.13 | Nilai ditolak → kotak kuning + "!" + banner kuning ("HEAT HARUS ≥ PRE-HEAT", "MIN HARUS < MAKS"); nilai tidak tersimpan; tetap di mode edit **[SETUJU]** | Nilai di luar batas dicegah dengan membatasi putaran di min/maks |
| 4.14 | Reset default: dialog konfirmasi BATAL/RESET (pilihan awal BATAL); semua profil 80/110 °C, 10/3/25 mnt, profil aktif 1; **kalibrasi dan setelan suara tidak disentuh** **[SENDIRI]** | Suara bukan parameter proses |
| 4.15 | Varian terkunci (tes berjalan): semua item bergembok + banner "TERKUNCI SAAT TES"; TEKAN = bunyi peringatan. Logika siap, belum bisa terjadi | `testRunning` selalu false sampai Prompt 2 |
| 4.16 | ~~Servis: TAHAN 5 dtk di Beranda langsung ke menu~~ — **dihapus 27 Nov 2026** (§10.1) | — |
| 4.17 | ~~UJI TAMPILAN 8 halaman~~ — **dihapus 27 Nov 2026** setelah uji LCD selesai (hasilnya: 1.8, 2.4, 3.1, 3.2) | — |

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
| 6.5 | 26 Nov 2026: buzzer yang terpasang **aktif**. Tetap digerakkan LEDC (warna nada masih bisa diatur sedikit). BOOT, KONFIRMASI, ERROR dibuat ulang sebagai **bip putus-putus nada datar** (glide terdengar jelek di buzzer aktif); PERINGATAN, SELESAI, TAHAN SELESAI, KLIK tetap **[SETUJU]** | Penilaian Stephen di UJI SUARA |

## 7. Arsitektur & lain-lain

| # | Keputusan | Alasan |
|---|---|---|
| 7.1 | Core 1: ISR video, task input (prioritas 3), UI di `loop()`. Core 0: task sensor (prioritas 4) + WiFi **[SETUJU]** | Critical section ADS1232 ±150 µs hanya mematikan interupsi core 0; ISR video punya 2 buffer baris (63,5 µs/baris NTSC) |
| 7.2 | Logika tidak tahu tampilan: layar membaca `UiSnapshot` dan bertindak lewat `UiCommands` | RANCANGAN §9 |
| 7.3 | FASE 2 dan 3 di-commit bersama | Kerangka layar (`Ui`, `Screen`) dan layar-layarnya saling bergantung; commit terpisah tidak bisa di-build |
| 7.4 | Versi library dikunci ke versi yang terbit sebelum tes LCD 28 Okt 2026: ESP_8_BIT 1.3.2, Adafruit GFX 1.13.0, BusIO 1.17.4 | `pio pkg list` tidak bisa dijalankan di sesi cloud; **cek sekali saat build pertama di PC** |
| 7.5 | Native unit test tanpa `USE_CURVE_FIT` / `test/support` (milik suite lama yang diarsipkan) | — |

## 8. Disepakati 5 Nov 2026 (dikerjakan 27 Nov 2026, lihat §10)

| # | Topik | Keputusan pemilik (5 Nov 2026) |
|---|---|---|
| 8.1 | OTA | ArduinoOTA dengan password dulu (password di `include/secrets.h` → `OTA_PASS`, tidak di-commit). Nanti diganti lewat aplikasi IoT |
| 8.2 | Input WiFi | Daftar SSID hasil scan, lalu password diketik **satu karakter per kolom** (bukan papan huruf penuh): karakter aktif berkedip; PUTAR = ganti karakter urut; TEKAN 1× = ganti a-z ↔ A-Z; TEKAN 2× = angka & simbol; TAHAN di karakter = kunci karakter itu, lanjut ke kolom berikutnya. Disimpan di NVS `hbnet` |
| 8.3 | Servis timbangan | Kalibrasi di layar servis menggantikan dasbor, **versi minimal dulu** (disetujui): STATUS SENSOR (berat, suhu, STABLE/MOVING live), TARE, KALIBRASI 1 TITIK (acuan 1–100 g, tunggu STABLE, simpan ke NVS `heatbox`), SELF-CAL ADS1232, RESET ADS1232. Multi-titik lengkap (03-40.4) belakangan |

## 9. Prompt 2 — alur tes, hasil, riwayat (26 Nov 2026)

| # | Keputusan | Alasan |
|---|---|---|
| 9.1 | **Rotary saja**; tombol START (J2) tidak dipasang. Mulai = TAHAN 2 dtk di Pilih Profil, batal = TAHAN 3 dtk di layar tes (dialog "BATALKAN TES?" terisi, 02-12) **[SETUJU]** | Petunjuk desain "TAHAN START 3d" diganti "TAHAN 3 DTK:BATAL" |
| 9.2 | Satu layar **TES BERJALAN** mengikuti fase siklus: PRE-HEAT (02-04), TARE (02-05), MUAT SAMPEL (02-06), PULIH & W0 (02-07), PEMANASAN 4 halaman (02-08), STOP (02-09); selesai otomatis ke HASIL atau TES GAGAL **[SENDIRI]** | Logika tidak tahu tampilan; layar hanya membaca `snap.cycle` |
| 9.3 | Bar tahap tetap PRE/TARE/ISI/PANAS/HASIL (keputusan 3.3); MUAT & PULIH = "ISI" | Label desain "PREHEAT/SAMPEL" tidak muat di 48 px |
| 9.4 | TARE: TEKAN = tare sekarang (hanya kalau pintu tertutup); tare otomatis setelah stabil 1 mnt (gerak ≤ 0,5 mg/mnt dari garis robust 3 mnt); batas tunggu 10 mnt → E-PREHEAT; banner peringatan 2 mnt sebelum batas **[SENDIRI]** | RANCANGAN §4.2 |
| 9.5 | MUAT SAMPEL: langkah aktif dibingkai; kotak pintu "TUTUP/TERBUKA" (TERTUTUP tidak muat T2 di 100 px); banner merah <4 g / >12 g, kuning di luar 5–10 g. Pintu ditutup dengan berat 4–12 g → PULIH **[SENDIRI]** | — |
| 9.6 | PULIH: checklist suhu (n/30 dtk) + berat tenang; setelah W0 terkunci layar 02-07b tampil 2,5 dtk sebelum halaman pemanasan **[SENDIRI]** | Desain 02-07b |
| 9.7 | HASIL 7 halaman: UTAMA, GRAFIK, BERAT, WAKTU & STATUS, MODEL, SUHU, INFO. DURASI dipindah ke kanan baris "KADAR AIR" (blok status bisa selebar 180 px: "BELUM KERING", "ERROR TUTUP"); INFO 5 baris (PROFIL "1 · 80/110°C") **[SENDIRI]** | Tidak tumpang-tindih dengan penanda halaman |
| 9.8 | Status: OK (hijau), OK ! (kuning, ada bendera), BELUM KERING (TIMEOUT), ERROR FIT/DATA/WF (angka dicoret + "TIDAK TERVALIDASI"). ERROR tanpa kadar air → layar TES GAGAL (02-11) dengan kode E-TUTUP / E-SENSOR / E-HEATER / E-SAMPEL / E-BATAL / E-PREHEAT / E-DATA / E-RESET **[SENDIRI]** | RANCANGAN §7. E-PREHEAT, E-DATA, E-RESET tidak ada di desain; memakai template yang sama |
| 9.9 | Grafik: % susut per menit = titik 4×4, model = garis biru (putus-putus setelah data terakhir), suhu = garis 2 px + target putus-putus; penanda A (label di bawah) dan STOP (label di atas) **[SENDIRI]** | Label tidak saling menimpa walau berdekatan |
| 9.10 | RIWAYAT: 6 baris per layar (id, tanggal/jam, kadar air T2, badge status), rel gulir, "n/40 TES TERSIMPAN"; TEKAN buka detail 7 halaman (sama dengan HASIL) **[SENDIRI]** | 03-30 |
| 9.11 | Tes terputus (alat mati di tengah tes): dialog "TES #id TERPUTUS" muncul di Beranda setelah boot (bukan di layar boot); tes tercatat E-RESET di riwayat **[SENDIRI]** | 02-01d; boot tetap ringkas |
| 9.12 | Beranda "HASIL TERAKHIR": "#0042 4.81% OK" / "#0041 E-TUTUP" dari log flash | — |
| 9.13 | Build `simulasi`: tanda merah "SIM" di baris status semua layar **[SENDIRI]** | Supaya tidak tertukar dengan alat asli |

## 10. Pengaturan lengkap & opsi pengembang (27 Nov 2026)

| # | Keputusan | Alasan |
|---|---|---|
| 10.1 | Menu **SERVIS dihapus total** (UJI TAMPILAN/INPUT/SUARA, TAHAN 5 dtk di Beranda). TAHAN di Beranda tidak berbuat apa-apa **[SETUJU]** | Permintaan Stephen: uji LCD sudah selesai, hemat flash |
| 10.2 | PENGATURAN = SET PROFIL (grup PROFIL 1–3), UMUM, SUARA, WIFI & JAM, INFO PERANGKAT, RESET DEFAULT, OPSI PENGEMBANG; daftar bergulir 6 baris + rel gulir **[SETUJU]** | Permintaan Stephen; profil digrup supaya menu tidak penuh |
| 10.3 | Selama tes: semua item terkunci kecuali INFO PERANGKAT **[SENDIRI]** | Ganti WiFi/kalibrasi di tengah tes mengganggu siklus |
| 10.4 | **OPSI PENGEMBANG** (pengganti isi menu servis 03-40, untuk tim IT, bukan tim gudang): layar masuk "KHUSUS TIM IT", **TAHAN 5 dtk = masuk**, TEKAN = kembali **[SENDIRI]** | Seperti "opsi pengembang" di HP: terlihat tapi tidak bisa terbuka tidak sengaja |
| 10.5 | Isi opsi pengembang: STATUS SENSOR (03-40.2), TIMBANGAN (03-40.3), KALIBRASI BERAT, CEK TIMBANGAN (03-40.5), PARAMETER METODE (03-40.6), **KONTROL PEMANAS** (baru: Kp, Ti, pita integral, maks daya), TAMPILAN (03-40.7), MEMORI LOG (03-40.8) **[SETUJU + SENDIRI]** | Kontrol pemanas ditambah supaya PI hasil uji U3 bisa disimpan permanen tanpa ubah kode |
| 10.6 | KALIBRASI **1 titik** (keputusan 8.3), 4 langkah: KOSONGKAN (rekam nol = tare 64 sampel) → BERAT ACUAN (1·2·5·10·20·50·100 g) → LETAKKAN (rekam) → HASIL (faktor lama/baru, perubahan %, cek sekarang) dengan **BATAL = faktor lama dikembalikan** / SIMPAN. TEKAN hanya diterima saat STABIL **[SENDIRI]** | Desain 03-40.4 multi-titik + S/N = belakangan |
| 10.7 | CEK TIMBANGAN: TEKAN = NOLKAN (tare); toleransi **5 mg + 0,01 % acuan** (10 g → ±6 mg). Status OK / PERIKSA / TUNGGU STABIL / LETAKKAN BEBAN **[SENDIRI]** | Desain "TEKAN:ulang" kurang jelas; toleransi belum ditentukan — sesuaikan dengan kelas anak timbangan (`app::ui::kCheckTol*`) |
| 10.8 | PARAMETER METODE & KONTROL PEMANAS: dialog PERINGATAN dulu (03-40.6a); 6/4 nilai sebagai baris label│nilai + baris RESET; disimpan di NVS `hbcfg`/`adv` (blob ber-CRC, `AdvancedSettings`); berlaku untuk **tes berikutnya**; "penyusutan nyata" tetap 1,5 × ambang Fase A **[SENDIRI]** | Tes yang berjalan tidak boleh berubah metode di tengah jalan |
| 10.9 | TAMPILAN: TAMPILAN TENANG nyala/mati + DEAD BAND 1–20 mg (bawaan nyala, 3 mg). Hanya angka berat di layar MUAT SAMPEL, PULIH, dan TIMBANGAN; STATUS SENSOR, KALIBRASI, CEK tetap angka asli. "Abaikan gerak < 2 mg" di desain dilebur ke dead band **[SENDIRI]** | Dua parameter itu tumpang-tindih |
| 10.10 | MEMORI LOG: n/40 tes, ruang bebas, HAPUS SEMUA (dialog konfirmasi, bukan tahan 3 dtk); nomor tes tetap berlanjut; tes yang sedang dicatat tidak ikut dihapus **[SENDIRI]** | Konsisten dengan dialog lain |
| 10.11 | WIFI & JAM (03-40.9): status, SSID, sinyal, IP, jam + sinkron terakhir; aksi GANTI WIFI, HUBUNGKAN ULANG, SINKRON JAM **[SETUJU]** | — |
| 10.12 | GANTI WIFI: pindai → daftar SSID (gembok = bersandi, bar sinyal), KETIK NAMA WIFI (SSID tersembunyi), PINDAI ULANG. SSID/sandi baru **dicoba dulu**; disimpan ke NVS `hbnet` hanya kalau tersambung (≤ 20 dtk), kalau gagal WiFi lama dipakai lagi + banner SANDI SALAH / TIDAK DITEMUKAN **[SENDIRI]** | Salah ketik sandi tidak boleh membuat alat putus dari jaringan |
| 10.13 | Ketik per karakter (8.2, disesuaikan): PUTAR = karakter, **TEKAN = ganti jenis** abc → ABC → 123 → simbol (bukan TEKAN 1×/2×), **TAHAN 1 dtk = kunci**; di ujung putaran ada DEL / OK / X (TAHAN untuk menjalankan). Huruf kecil tampil apa adanya (`Painter::textExact`); sandi tampil terbuka; sandi WPA 1–7 karakter ditolak **[SENDIRI]** | TEKAN 2× butuh menunda TEKAN 1× (terasa lambat); semua aksi tetap bisa dengan rotary saja |
| 10.14 | INFO PERANGKAT (03-40.10): firmware, ID alat (HBX- + 3 bait MAC), total tes, MAC, IP, nama OTA, status OTA, faktor kalibrasi **[SETUJU]** | "About" yang diminta Stephen |
| 10.15 | **OTA** (keputusan 8.1): ArduinoOTA, aktif hanya kalau `OTA_PASS` diisi; dilayani hanya saat siklus tidak berjalan; saat menulis lampu dipaksa mati dan layar penuh **UPDATE** (progres %, tombol tidak aktif) **[SETUJU + SENDIRI]** | Update dari aplikasi HP menyusul |
| 10.16 | Gaya UI dikunci: bingkai tebal + teks sekunder hitam; kode varian (`UiStyle`) dihapus **[SENDIRI]** | Tidak ada lagi layar untuk menggantinya |

