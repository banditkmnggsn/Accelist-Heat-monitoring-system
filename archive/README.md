# Arsip

Semua yang **tidak dipakai** firmware maupun unit test aktif dipindah ke sini (5 Nov 2026),
supaya isi repo di luar folder ini hanya kode yang terpakai. Tidak ada yang dihapus.
PlatformIO tidak mengompilasi apa pun dari folder ini.

| Folder | Isi | Kenapa diarsipkan |
|---|---|---|
| `tuning_web/` | Dasbor PC (Python) | Alat berdiri sendiri dengan LCD. `drying.py`/`cycle.py` tetap acuan uji port C++ (`test/test_drying_math/gen_golden.py`) |
| `lib_Heatbox/` | Library siklus versi lama | Diganti `lib/HeatboxCycle` (RANCANGAN §9) |
| `test_heatbox_lama/` | 7 suite uji + `support/` untuk `lib_Heatbox` | Bergantung pada `lib_Heatbox`; belum pernah bisa jalan |
| `lib_EncoderInput/` | Driver rotary lama (polling, short/long press) | Diganti `lib/HeatboxInput` (decoder tanpa false trigger + gesture TEKAN/TAHAN, ada unit test) |
| `tes_hardware/` | `lcd_test.cpp`, `rotary_test.cpp` | Tes pengenalan LCD dan rotary 28 Okt–4 Nov 2026 sudah selesai; fungsinya kini ada di menu SERVIS (UJI TAMPILAN, UJI INPUT) |

Untuk menjalankan ulang tes hardware lama: salin file ke `src/`, lalu tambahkan env lamanya
(`build_flags = -DLCD_TEST` atau `-DROTARY_TEST`, `build_src_filter = -<*> +<nama_file.cpp>`,
`lib_deps` library video) ke `platformio.ini`.
