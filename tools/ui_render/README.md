# ui_render — pratinjau PNG layar LCD di PC

Alat pengembang. Kode layar yang **sama** dengan firmware (`lib/HeatboxUi`) digambar ke
`GFXcanvas8` (RGB332) lalu disimpan sebagai PNG 256×240 (`_1x`) dan 768×720 (`_3x`, sama dengan
kanvas desain). Tiap gambar juga dicek otomatis: kalau ada piksel tidak putih di zona PARKING,
program mencetak `PELANGGARAN PARKING`.

Firmware tidak memakai folder ini. `shim/` hanya pengganti minimal `Arduino.h`/`Print.h`
supaya Adafruit GFX bisa dikompilasi dengan g++ biasa.

```bash
git clone --depth 1 --branch 1.13.0 https://github.com/adafruit/Adafruit-GFX-Library.git /tmp/gfx
```

```bash
GFX=/tmp/gfx tools/ui_render/build.sh .pio/ui_render
```

Butuh g++ (Linux / WSL / MSYS2). Skenario layar ada di `ui_render.cpp` (`main`); hasil terakhir
disalin ke `docs/ui/pratinjau/`.
