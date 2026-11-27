# Kontrol rotary — Heat Box

Satu-satunya input operator adalah rotary encoder EC11 (putar + tekan). Tombol START 22 mm
(J2, GPIO35) **tidak dipasang (NC)** — keputusan 26 Nov 2026: rotary sudah cukup, mulai dan
batal tes memakai TAHAN rotary. Pin tetap dipesan di `pins.h`. Konstanta ada di `include/app_config.h` (`app::input`), logikanya di
`lib/HeatboxInput` (diuji `test/test_input`).

## Gerakan dasar

| Gerakan | Syarat | Arti umum |
|---|---|---|
| **PUTAR** | satu klik penuh (4 transisi quadrature) | pindah pilihan / pindah halaman / ubah nilai. Kanan = naik/turun ke bawah daftar |
| **TEKAN** | dilepas < **0,6 dtk** | pilih / OK / simpan |
| **TAHAN** | ditahan sampai batas aksi layar | aksi layar (lihat tabel) |

- **Bar progres** muncul di baris petunjuk mulai **0,6 dtk**, berlabel aksinya ("KEMBALI...",
  "MULAI...", "MASUK...", "BATAL...", "KUNCI...", "TUTUP...").
- Aksi terjadi **tepat** saat batas tercapai. Setelah itu tombol harus dilepas dulu; melepas
  tidak menghasilkan TEKAN.
- Dilepas antara 0,6 dtk dan batas aksi = **tidak terjadi apa-apa**.
- **Putar sambil menekan = batal**: putaran diabaikan dan tekanan itu tidak menghasilkan TEKAN
  maupun TAHAN.
- Layar tanpa aksi tahan (Boot, "< KEMBALI" di Pilih Profil): bar tidak muncul.
- Putar cepat (klik < 45 ms) saat mengubah nilai = langkah 5.

## Aksi TAHAN per layar

| Layar | TAHAN | Batas |
|---|---|---|
| Beranda | — (menu SERVIS dihapus 27 Nov 2026; bar tidak muncul) | — |
| OPSI PENGEMBANG (layar masuk) | **MASUK** ke menu tim IT | 5 dtk |
| Ketik SSID / sandi | **KUNCI** karakter / jalankan DEL, OK, X | 1 dtk |
| Kalibrasi, saat merekam | — (tunggu selesai) | — |
| Kalibrasi, selain itu | **BATAL** (di HASIL: faktor lama dikembalikan) | 2 dtk |
| Pilih Profil (kursor di profil) | **MULAI** tes (simpan profil aktif dulu) | 2 dtk |
| Pilih Profil (kursor di "< KEMBALI") | — | — |
| Edit nilai, sedang mengedit | **BATAL** edit (nilai lama tetap) | 2 dtk |
| Error SENSOR saat boot | ke **BERANDA** (MULAI TES ditolak) | 2 dtk |
| Dialog | **TUTUP** dialog (konfirmasi = BATAL) | 2 dtk |
| Hasil | ke Beranda | 2 dtk |
| Error tes (TES GAGAL) | ke Beranda (TEKAN juga) | 2 dtk |
| Tes berjalan (PRE-HEAT .. PEMANASAN) | dialog "BATALKAN TES?" terisi; lepas sebelum penuh = lanjut; penuh = E-BATAL | 3 dtk |
| STOP | — (bar tidak muncul) | — |
| Riwayat #id | kembali ke daftar | 2 dtk |
| Layar lain | **KEMBALI** | 2 dtk |

## Per layar

| Layar | PUTAR | TEKAN | Petunjuk |
|---|---|---|---|
| Beranda | pilih menu | buka menu | `PUTAR:PILIH  TEKAN:OK` |
| Pilih Profil | pilih profil / "< KEMBALI" | jadikan aktif / kembali | `PUTAR:PILIH TEKAN:AKTIFKAN TAHAN:MULAI` |
| Riwayat (daftar) | pilih tes | buka detail | `PUTAR:PILIH TEKAN:BUKA TAHAN:KEMBALI` |
| Riwayat #id | ganti halaman (7) | — | `PUTAR:HALAMAN  TAHAN:RIWAYAT` |
| PRE-HEAT / MUAT / PULIH | — | — | `TAHAN 3 DTK:BATAL` |
| TARE | — | **tare sekarang** (pintu harus tertutup) | `TEKAN:TARE SEKARANG  TAHAN:BATAL` |
| PEMANASAN | ganti halaman (4) | — | `PUTAR:HALAMAN  TAHAN 3 DTK:BATAL` |
| HASIL | ganti halaman (7) | — | `PUTAR:HALAMAN  TAHAN:BERANDA` |
| TES GAGAL | — | ke Beranda | `TEKAN:BERANDA` |
| Pengaturan | pilih item | buka / ganti suara / dialog reset | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| Edit nilai (pilih) | pilih kotak | mulai edit | `PUTAR:PILIH TEKAN:EDIT TAHAN:KEMBALI` |
| Edit nilai (edit) | ubah nilai | simpan (ditolak = kuning) | `PUTAR:UBAH TEKAN:SIMPAN TAHAN:BATAL` |
| Dialog info | — | tutup | `TEKAN:OK` |
| Dialog konfirmasi | pilih BATAL/RESET | jalankan pilihan | `PUTAR:PILIH  TEKAN:OK  TAHAN:BATAL` |
| Error SENSOR | — | cek ulang | `TEKAN:CEK ULANG  TAHAN:BERANDA` |
| Pengaturan | pilih item (6 baris, bergulir) | buka / ganti suara / dialog reset | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| SET PROFIL | pilih profil 1–3 | edit profil | `PUTAR:PILIH TEKAN:UBAH TAHAN:KEMBALI` |
| WIFI & JAM | GANTI WIFI / HUBUNGKAN ULANG / SINKRON JAM | jalankan | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| Pilih WiFi | pilih SSID / KETIK NAMA WIFI / PINDAI ULANG | pilih (bersandi -> ketik sandi) | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| Ketik SSID / sandi | ganti karakter; ke kiri dari huruf pertama: DEL, OK, X | ganti jenis abc / ABC / 123 / simbol | `PUTAR:HURUF TEKAN:JENIS TAHAN:KUNCI` |
| INFO PERANGKAT | — | — | `TAHAN:KEMBALI` |
| OPSI PENGEMBANG (masuk) | — | kembali | `TAHAN 5 DTK:MASUK  TEKAN:KEMBALI` |
| Menu pengembang | pilih item (8, bergulir) | buka (PARAMETER & PEMANAS: dialog peringatan dulu) | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| TIMBANGAN | TARE / SELF-CAL / RESET | dialog JALANKAN? | `PUTAR:PILIH TEKAN:JALANKAN TAHAN:KEMBALI` |
| KALIBRASI | pilih berat acuan (langkah 2), BATAL/SIMPAN (langkah 4) | rekam nol / lanjut / rekam beban / OK | sesuai langkah |
| CEK TIMBANGAN | pilih berat acuan | nolkan (tare) | `PUTAR:ACUAN TEKAN:NOLKAN TAHAN:KEMBALI` |
| MEMORI LOG | — | dialog HAPUS SEMUA | `TEKAN:HAPUS SEMUA  TAHAN:KEMBALI` |
| UPDATE (OTA) | semua tombol diabaikan | — | `TOMBOL TIDAK AKTIF SELAMA UPDATE` |

## Suara umpan balik

TEKAN = KLIK, TAHAN berhasil = TAHAN SELESAI (dua bip). Layar bisa menimpanya: tersimpan =
KONFIRMASI, ditolak = ERROR, pintu terbuka / terkunci = PERINGATAN. Siklus: masuk TARE = TAHAN
SELESAI, masuk MUAT SAMPEL = PERINGATAN (operator diminta bertindak), mulai PEMANASAN =
KONFIRMASI, hasil = SELESAI, tes gagal = ERROR. Daftar lengkap di `src/sounds.h` (layar UJI
SUARA sudah dihapus bersama menu SERVIS). Buzzer yang terpasang **aktif** (26 Nov 2026); BOOT,
KONFIRMASI, dan ERROR dibuat ulang sebagai bip putus-putus nada datar.

## Decoder (tidak ada langkah palsu)

- Tiap transisi sah menambah/mengurangi hitungan; pantulan kontak saling meniadakan.
- Langkah keluar hanya saat kembali ke posisi diam (detent) **dan** hitungan tepat ±4.
  Gerak setengah jalan dibuang tanpa langkah; transisi terlewat membuat klik hilang, bukan
  berbalik arah.
- Kalau ternyata 1 klik = 2 transisi (klik baru terhitung tiap 2 klik), ubah
  `kEncStepsPerDetent` ke 2. Kalau putar kanan malah turun, set `kEncReverse = true`.
  Uji input 6 Nov 2026: 1 klik = 4 transisi, arah benar.
