# Unit test Heat Box

Semua suite di sini berjalan **di PC, tanpa board**: logika yang tidak butuh hardware
(`lib/HeatboxCycle`, `lib/HeatboxInput`) ditulis tanpa `Arduino.h` supaya bisa dibuktikan
benar sebelum hardware-nya ada. Tujuh suite lama untuk `lib/Heatbox` diarsipkan bersama
library-nya di `archive/test_heatbox_lama/`.

```bash
pio test -e native                    # semua suite
pio test -e native -f test_input      # satu suite saja
pio test -e native -v                 # verbose, tampilkan tiap assertion
```

## Suite

| Suite | Menguji |
|---|---|
| `test_drying_math` | Port C++ matematika siklus (`DryingMath.*`) terhadap **vektor emas** yang dihitung `archive/tuning_web/drying.py`. Lolos berarti firmware memberi angka yang sama dengan Python |
| `test_cycle_aggregation` | `SecondAggregator` + `MinuteBuilder`: rata-rata per detik, detik tidak sah, loop tertunda, `millis()` membungkus, nilai per menit di tengah menit |
| `test_settings` | `CycleSettings`: nilai bawaan, validasi (heat ≥ pre-heat, min < maks, batas), blob NVS `hbcfg` (CRC, isi rusak ditolak) |
| `test_input` | `lib/HeatboxInput`: decoder rotary tanpa langkah palsu (pantulan, setengah klik, transisi terlewat, mode 2 langkah), debounce, gesture TEKAN/TAHAN/batal-putar |
| `test_cycle` | `CycleController` + `HeaterControl` + `SimModel` (RANCANGAN §12 U1): kontrol PI mencapai 80 °C tanpa overshoot > 3 °C, urutan K1 → SSR, siklus penuh OK (kadar air, Fase A/B, prediksi stop), pintu dibuka saat HEAT → E-TUTUP, batal, sampel ringan → E-SAMPEL, sensor suhu putus → E-SENSOR, SSR macet → E-HEATER, sampel lambat kering → TIMEOUT |
| `test_advanced` | `AdvancedSettings` (opsi pengembang): bawaan = konstanta lama (CycleConfig/PiParams), validasi (target < ambang Fase A, batas), blob NVS (rusak/isi tidak sah ditolak), parameter -> CycleConfig, `setConfig` ditolak saat siklus berjalan, filter TAMPILAN TENANG |

```bash
python test/test_drying_math/gen_golden.py   # hanya kalau drying.py / kasus uji berubah
pio test -e native -f test_drying_math       # butuh gcc di PATH
```

Tanpa PlatformIO, dari root proyek:

```bat
test\run_msvc.bat
```

```bash
UNITY=/path/ke/Unity/src test/run_gcc.sh
```

`run_msvc.bat` untuk MSVC 2019 (mesin pengembangan Windows tanpa gcc), `run_gcc.sh` untuk
g++ (Linux/WSL/MSYS2). Keduanya menjalankan kelima suite; Unity diambil dari
`.pio/libdeps/native/Unity/src` setelah `pio pkg install -e native`, atau dari
https://github.com/ThrowTheSwitch/Unity (tag v2.6.0).

## Aturan yang dipegang di seluruh suite

- **Deterministik.** Tidak ada `rand()`; test yang gagal selalu bisa diulang.
- **Toleransi eksplisit** untuk float.
- **Nilai realistis**: count ADC di orde jutaan, waktu dekat `millis()` yang membungkus.
