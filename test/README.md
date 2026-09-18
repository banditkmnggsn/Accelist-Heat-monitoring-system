# Unit test Heat Box

Semua suite di sini berjalan **di PC, tanpa board**. Itu tujuan utama dari
pemisahan `HeatboxMath` sebagai modul murni: algoritmanya bisa dibuktikan
benar sebelum hardware-nya ada.

```bash
pio test -e native                    # semua suite
pio test -e native -f test_math_fit   # satu suite saja
pio test -e native -v                 # verbose, tampilkan tiap assertion
```

## Struktur

Tiap folder `test_*/` adalah **program terpisah** dengan `main()` sendiri.
PlatformIO mengompilasi dan menjalankannya satu per satu, jadi satu suite yang
crash tidak menjatuhkan yang lain.

| Suite | Menguji | Kenapa dipisah |
|---|---|---|
| `test_math_slope` | regresi linier | murni fungsi, tanpa state |
| `test_math_median` | median filter | murni fungsi, tanpa state |
| `test_math_solve3x3` | penyelesai 3×3 | aljabar, dipakai fitting |
| `test_math_fit` | fitting kurva 3 suku | paling rumit, butuh data sintetik |
| `test_pipeline` | ADC → detik → status | punya state, butuh reset tiap test |
| `test_stop_logic` | keputusan berhenti | perilaku produk, siklus panjang |
| `test_moisture` | kadar air + deteksi stabil | API publik |

## Prasyarat di kode library

Dua penyesuaian kecil supaya suite ini bisa jalan:

**1. Config harus bisa di-override dari build flag.**
Di `HeatboxConfig.h`, bungkus tiap makro:

```cpp
#ifndef USE_CURVE_FIT
#define USE_CURVE_FIT 0
#endif
```

Tanpa `#ifndef`, `-D USE_CURVE_FIT=1` di `platformio.ini` akan bentrok. Pola ini
juga membuat library-mu bisa dikonfigurasi pemakai tanpa mengedit source —
praktik yang layak diajarkan.

**2. Lapisan algoritma tidak boleh `#include <Arduino.h>`.**
Ganti dengan `<stdint.h>`, `<math.h>`, `<string.h>`. Kalau `Arduino.h` masih ada,
environment `native` tidak akan bisa dikompilasi.

## Asumsi nama API

Suite ini ditulis mengikuti gaya yang sudah kamu pakai:

```cpp
namespace heatbox { namespace detail {
    float   slopeCountsPerSec(const int32_t *y, int n, float dt);
    int32_t median(const int32_t *src, uint8_t n);
    bool    solve3x3(const float M[3][3], const float r[3], float x[3]);
    HbFit   fitCurve(const int32_t *y, int n, float dt);
}}

void            hbInit();
void            hbPushRaw(int32_t counts);
void            hbTick1Hz();
void            hbTare();
void            hbStartDry();
bool            hbStableWeight(float *out_mg);
float           hbMoisturePct(float w_start_mg, float w_end_mg);
const HbStatus* hbStatus();
```

Kalau nama di kodemu berbeda, sesuaikan — logikanya tetap sama.

## Aturan yang dipegang di seluruh suite

- **Deterministik.** Tidak ada `rand()`. Noise dibangkitkan dari rumus
  berbasis indeks, jadi test yang gagal selalu bisa diulang.
- **Toleransi eksplisit.** Pakai `TEST_ASSERT_FLOAT_WITHIN`, jangan
  `TEST_ASSERT_EQUAL_FLOAT` — float tidak pernah sama persis.
- **`setUp()` mereset state.** Modul ini masih memakai state global, jadi tiap
  test wajib mulai dari nol.
- **Nilai realistis.** Count ADC diuji di orde jutaan, bukan ratusan. Angka
  kecil menyembunyikan bug presisi yang justru ingin kita cegah.
