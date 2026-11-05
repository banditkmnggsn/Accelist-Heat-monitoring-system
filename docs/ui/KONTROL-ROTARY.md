# Kontrol rotary — Heat Box

Satu-satunya input operator adalah rotary encoder EC11 (putar + tekan). Tombol START 22 mm
(GPIO35) belum dipakai. Konstanta ada di `include/app_config.h` (`app::input`), logikanya di
`lib/HeatboxInput` (diuji `test/test_input`).

## Gerakan dasar

| Gerakan | Syarat | Arti umum |
|---|---|---|
| **PUTAR** | satu klik penuh (4 transisi quadrature) | pindah pilihan / pindah halaman / ubah nilai. Kanan = naik/turun ke bawah daftar |
| **TEKAN** | dilepas < **0,6 dtk** | pilih / OK / simpan |
| **TAHAN** | ditahan sampai batas aksi layar | aksi layar (lihat tabel) |

- **Bar progres** muncul di baris petunjuk mulai **0,6 dtk**, berlabel aksinya ("KEMBALI...",
  "MULAI...", "SERVIS...", "BATAL...", "TUTUP...").
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
| Beranda | masuk **SERVIS** (2 dtk tidak berbuat apa-apa) | 5 dtk |
| Pilih Profil (kursor di profil) | **MULAI** tes (simpan profil aktif dulu) | 2 dtk |
| Pilih Profil (kursor di "< KEMBALI") | — | — |
| Edit nilai, sedang mengedit | **BATAL** edit (nilai lama tetap) | 2 dtk |
| Error SENSOR saat boot | ke **BERANDA** (MULAI TES ditolak) | 2 dtk |
| Dialog | **TUTUP** dialog (konfirmasi = BATAL) | 2 dtk |
| Hasil (Prompt 2) | ke Beranda | 2 dtk |
| Tes berjalan (Prompt 2) | dialog batal yang terisi; lepas sebelum penuh = lanjut | 3 dtk |
| Layar lain | **KEMBALI** | 2 dtk |

## Per layar

| Layar | PUTAR | TEKAN | Petunjuk |
|---|---|---|---|
| Beranda | pilih menu | buka menu | `PUTAR:PILIH  TEKAN:OK` |
| Pilih Profil | pilih profil / "< KEMBALI" | jadikan aktif / kembali | `PUTAR:PILIH TEKAN:AKTIFKAN TAHAN:MULAI` |
| Riwayat | — | — | `TAHAN:KEMBALI` |
| Pengaturan | pilih item | buka / ganti suara / dialog reset | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| Edit nilai (pilih) | pilih kotak | mulai edit | `PUTAR:PILIH TEKAN:EDIT TAHAN:KEMBALI` |
| Edit nilai (edit) | ubah nilai | simpan (ditolak = kuning) | `PUTAR:UBAH TEKAN:SIMPAN TAHAN:BATAL` |
| Dialog info | — | tutup | `TEKAN:OK` |
| Dialog konfirmasi | pilih BATAL/RESET | jalankan pilihan | `PUTAR:PILIH  TEKAN:OK  TAHAN:BATAL` |
| Error SENSOR | — | cek ulang | `TEKAN:CEK ULANG  TAHAN:BERANDA` |
| Servis | pilih uji | buka | `PUTAR:PILIH TEKAN:OK TAHAN:KEMBALI` |
| UJI TAMPILAN | ganti halaman | ganti varian (halaman 7–8) | `PUTAR:HALAMAN ...` |
| UJI INPUT | dihitung | dihitung | `PUTAR/TEKAN:UJI  TAHAN 2 DTK:KEMBALI` |
| UJI SUARA | pilih suara | putar suara (walau suara dimatikan) | `PUTAR:PILIH TEKAN:BUNYI TAHAN:KEMBALI` |

## Suara umpan balik

TEKAN = KLIK, TAHAN berhasil = TAHAN SELESAI (dua bip). Layar bisa menimpanya: tersimpan =
KONFIRMASI, ditolak = ERROR, pintu terbuka / terkunci = PERINGATAN. Daftar lengkap di
`src/sounds.h`, bisa diputar satu per satu di SERVIS > UJI SUARA.

## Decoder (tidak ada langkah palsu)

- Tiap transisi sah menambah/mengurangi hitungan; pantulan kontak saling meniadakan.
- Langkah keluar hanya saat kembali ke posisi diam (detent) **dan** hitungan tepat ±4.
  Gerak setengah jalan dibuang tanpa langkah; transisi terlewat membuat klik hilang, bukan
  berbalik arah.
- Kalau ternyata 1 klik = 2 transisi (UJI INPUT: "RAW PER KLIK TERAKHIR" = 2 dan klik baru
  terhitung tiap 2 klik), ubah `kEncStepsPerDetent` ke 2. Kalau putar kanan malah turun, set
  `kEncReverse = true`.
- UJI INPUT menampilkan penghitung anomali: lompatan 2 bit (transisi terlewat), gerak tanpa
  klik (pantulan / getaran saat diam), dan balik arah < 120 ms (dicurigai langkah mundur palsu).
