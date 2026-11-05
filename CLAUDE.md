# CLAUDE.md — Heat Box firmware

Konteks lengkap proyek dan sesi sebelumnya: **docs/KONTEKS-SINKRON.md** (baca dulu).

## Cara kerja dengan pemilik (Stephen)
- Lapor dalam Bahasa Indonesia, singkat, langsung ke inti; istilah Inggris diberi arti singkat.
- Hal kecil: putuskan sendiri dan catat di `docs/ui/KEPUTUSAN-UI.md` (keputusan + alasan).
- BERHENTI dan tanya untuk hal besar: mengubah pin, menghapus fitur, menyentuh driver sensor
  (`lib/Ads1232`, `lib/RtdSensor`) atau kalibrasi (NVS `heatbox`, `RTD_RREF`, `RTD_OFFSET_C`,
  `RTD_WIRES`), mengganti library, apa pun yang menyalakan pemanas.
- Commit per fase; push hanya kalau diminta.
- Tidak ada dead code di luar `archive/`; berkas tak terpakai di-`git mv` ke `archive/` dan
  dicatat di `archive/README.md`. Kode untuk Prompt 2 (HeatboxCycle, driver, kalibrasi) bukan
  dead code.

## Aturan keras
- Platform `espressif32@6.5.0` (Arduino core 2.0.14, C++11) DIKUNCI.
- GPIO22 (SSR) dan GPIO21 (relay K1) selalu OUTPUT LOW sampai siklus pemanas disetujui.
- `Wire.begin()` dilarang (I2C bawaan = 21/22).
- GPIO12 (buzzer) strapping: wajib LOW saat boot dan setelah tiap suara.
- Zona PARKING LCD x 156–255, y 0–42 selalu putih di semua layar.
- Jangan commit `include/secrets.h`; jangan tulis password di kode/dokumen.

## Perintah
- Build/upload: `pio run`, `pio run -t upload` (COM5), `pio device monitor` (115200).
- Unit test: `pio test -e native` atau `test\run_msvc.bat` / `test/run_gcc.sh`.
- Pratinjau layar: `tools/ui_render/README.md`.
