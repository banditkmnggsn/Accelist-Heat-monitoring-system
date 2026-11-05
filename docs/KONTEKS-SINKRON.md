# Konteks sinkron — Heat Box firmware (dari sesi cloud 4–5 Nov 2026)

Dokumen ini memindahkan SEMUA konteks sesi Claude Code cloud (Prompt 1) ke sesi lokal baru.
Baca seluruhnya sebelum bekerja. Dokumen pendukung: `README.md`,
`RANCANGAN-SIKLUS-PENGERINGAN.md`, `SPESIFIKASI-TEKNIS.md`, `docs/ui/KEPUTUSAN-UI.md`,
`docs/ui/KONTROL-ROTARY.md`, `archive/README.md`, `test/README.md`.

---

## 1. Pemilik, bahasa, cara kerja

- Pemilik: **Stephen** (magang Accelist). Alat: **Heat Box**, moisture analyzer tepung telur
  (loss on drying): load cell 300 g + ADS1232, PT100 + MAX31865, lampu lewat SSR.
- Laporan ke Stephen: **Bahasa Indonesia, singkat, langsung ke inti; istilah Inggris diberi arti
  singkat.**
- Pola kerja yang disepakati:
  - FASE 0 = baca semua, lapor, tanya; BERHENTI sampai dijawab. Lalu fase-fase kerja.
  - Hal kecil: putuskan sendiri, catat di `docs/ui/KEPUTUSAN-UI.md` (keputusan + alasan).
  - Hal besar = BERHENTI dan tanya: mengubah pin, menghapus fitur, menyentuh driver sensor atau
    kalibrasi, mengganti library, apa pun yang menyalakan pemanas.
  - Commit per fase. **Push hanya kalau Stephen minta** (di sesi cloud ia mengizinkan push ke
    `claude/amazing-meitner-45ct0g`).
- Aturan arsip / dead code (dari Stephen):
  - Di luar `archive/` hanya kode yang dipakai. Berkas tak terpakai → `git mv` utuh ke
    `archive/` (+ catat di `archive/README.md`: isi, alasan, cara memulihkan).
  - Di dalam berkas yang dipertahankan: hapus fungsi tak terpakai, cabang `#if` lama, kode
    dikomentari.
  - PENGECUALIAN (bukan dead code, untuk Prompt 2): `lib/HeatboxCycle` (DryingMath,
    SecondAggregator, MinuteBuilder, SensorInput, CycleSettings), `lib/Ads1232`,
    `lib/RtdSensor`, kalibrasi NVS, dan unit test-nya.
- Varian di UJI TAMPILAN harus **terarah**: hanya hal yang benar-benar masih diragukan, jangan
  memasukkan varian yang sudah jelas tidak cocok.

---

## 2. Status saat sesi cloud berakhir

- Prompt 1 (fondasi LCD + rotary + layar dasar) **SELESAI di kode**, **BELUM diuji di LCD fisik**.
- Branch GitHub: `claude/amazing-meitner-45ct0g` (repo `banditkmnggsn/Accelist-Heat-monitoring-system`).
  Di PC disarankan diambil sebagai branch lokal `lcd-ui`:
  ```
  git fetch origin claude/amazing-meitner-45ct0g
  git switch -c lcd-ui FETCH_HEAD
  ```
- Commit: `b526575` FASE 1 (rapikan repo) → `95a5114` FASE 2+3 (fondasi + layar) → `ef8aff1`
  FASE 4 (dokumen + pratinjau) → `73d1843` + commit berikutnya (catatan keputusan Prompt 2) →
  commit dokumen ini.
- **Build sungguhan `pio run` BELUM PERNAH dijalankan.** Di cloud registry PlatformIO diblokir;
  kode di-compile + link dengan toolchain Xtensa + recipe resmi Arduino-ESP32 2.0.14:
  0 error, 0 warning dari kode proyek (`-Wall -Wextra`).
  - WiFi ON: RAM statis 50 956 B (15,6 %), flash 841 513 B (64,2 %).
  - WiFi OFF + PAL: RAM 28 512 B, flash 395 217 B.
  - Framebuffer video 2 × 61 440 B diambil dari heap saat jalan (belum terukur heap bebasnya).
- Unit test PC: 4 suite, 49 tes, lolos (g++ `-Wall -Wextra -Werror`).
- Langkah berikut Stephen: build + upload di VS Code lewat agent, jalankan checklist LCD,
  hasilkan `docs/ui/HASIL-UJI-PROMPT1.md`. Prompt agent-nya ada di **Lampiran A**.

---

## 3. Hardware (acuan: `docs/hardware/*.pdf`; breadboard = skematik, pin sama persis)

| Fungsi | GPIO | Rangkaian / catatan |
|---|---|---|
| MAX31865 SCK/MISO/MOSI/CS | 18/19/23/17 | VSPI; Rref modul 430 Ω, firmware pakai 423,12 Ω; 3-wire |
| ADS1232 SCLK/DOUT/PDWN | 4/16/13 | R seri 47 Ω; R19/R22 100k pull-down SCLK/PDWN; AVDD 5V_A dari L7805 (12 V) |
| Rotary A(CLK)/B(DT)/SW | 32/33/27 | J3; tiap jalur R seri 1k + pull-up 10k + 100 nF (~1 ms) |
| Video composite | 25 | DAC1 langsung ke RCA (J9), tanpa R. Library hanya pakai DAC1; GPIO26 bebas |
| Limit switch pintu | 34 | input-only, pull-up R8 10k eksternal; kontak ke GND; **LOW = tertutup** (`PIN_DOOR_ACTIVE_LEVEL`); kabel lepas = terbaca terbuka (fail-safe). Di breadboard: saklar biasa |
| Tombol START | 35 | pull-up R4 10k + RC. **Didefinisikan saja, belum dipakai** |
| Buzzer BZ1 | 12 | **PASIF (PWM LEDC)**, 5 V, BC337 low-side, HIGH = bunyi, R26 1k basis, **R27 10k pull-down** (GPIO12 = strapping MTDI, wajib LOW saat boot), D3 1N4002 flyback, C30 100 nF. **Tidak ada resistor seri ke BZ1** (sudah dilaporkan sebagai risiko) |
| SSR lampu (HLController) | 22 | BC337 low-side di board Power, pull-down R16. **Selalu OUTPUT LOW** |
| Relay pengaman K1 (HLSafety) | 21 | BC337, pull-down R17. **Selalu OUTPUT LOW**. Urutan nanti: tutup K1 dulu, baru SSR |
| DS18B20 | 5 | pull-up R12 4k7; belum dipakai |
| Ekstensi J12 | 2, 14, 15, 26, 36, 39 | tidak dipakai |

- Pin I2C bawaan Arduino = 21/22 = relay & SSR → **`Wire.begin()` dilarang di mana pun**.
- Dengan daya USB saja: timbangan tidak jalan (5V_A dari 12 V), boot masuk layar error SENSOR.
  LCD juga butuh 12V_D.
- `RTD_OFFSET_C = -1.5f` di kode (dokumen .md sudah disamakan ke −1,5). Komentar di `pins.h`
  masih menyebut −1 °C; **kode tidak boleh diubah tanpa izin** (Stephen hanya minta dokumen
  disesuaikan).
- Catatan skematik: judul sheet 5 "Buzzer 3,3 V" padahal BZ1 5 V (salah tulis dokumen).
- ESP32-WROOM-32 = dual core (Stephen sempat bertanya; jawab: ya).

---

## 4. Arsitektur firmware (Prompt 1)

Platform DIKUNCI `espressif32@6.5.0` (Arduino core 2.0.14, **C++11**: constexpr tanpa loop,
dsb.). Jangan dinaikkan (library video tidak jalan di core 3.x).

| Core | Isi | Alasan |
|---|---|---|
| 1 | ISR video `ESP_8_BIT_GFX` (`video.begin()` dari `setup()`/loopTask), task input 1 kHz (prio 3, `src/InputTask`: rotary, pintu, buzzer), `loop()` = UI | ISR video punya hanya 2 buffer baris (63,5 µs/baris NTSC) |
| 0 | WiFi (ESP-IDF) + task sensor (prio 4, `src/SensorService`: ADS1232, MAX31865, agregasi MNT, auto-tare boot, parser serial, ringkasan 10 dtk, `NetClock::update`) | ADS1232 shift-out memakai critical section ±150 µs yang mematikan interupsi core pemanggil |

Berkas:
```
include/pins.h          semua pin + static_assert (unik, strapping hanya 5/12, output bukan 34-39)
include/app_config.h    konstanta app; namespace app::input (SEMUA waktu input), app::ui, app::task
include/secrets.example.h  contoh; include/secrets.h (gitignored): WIFI_SSID, WIFI_PASS, OTA_PASS
lib/Ads1232, lib/RtdSensor   driver — TIDAK DIUBAH
lib/HeatboxCycle        DryingMath, SecondAggregator, MinuteBuilder, SensorInput (tidak diubah,
                        kecuali komentar lidClosed) + CycleSettings (baru)
lib/HeatboxInput        QuadratureDecoder, Debouncer, ButtonGesture, InputEvent (C++ murni)
lib/HeatboxUi           UiModel (UiSnapshot/UiCommands/Sound), Theme, Icons, Painter, Widgets,
                        Screen, Screens(.h + ScreensMain/Settings/Service.cpp), Ui (manajer)
src/main.cpp            setup/loop tipis: pemanas LOW, settings, input, video, WiFi, task sensor, UI
src/SensorService.*     logika main.cpp lama dipindah apa adanya + status untuk UI + RINGKASAN
src/InputTask.*         polling 1 kHz, antrian event, diag UJI INPUT, buzzer.update
src/Buzzer.*, sounds.h  sequencer LEDC non-blocking; SEMUA suara di satu tabel sounds.h
src/NetClock.*          WiFi STA + SNTP WIB (TZ "WIB-7", id.pool.ntp.org, pool.ntp.org)
src/SettingsStore.*     NVS "hbcfg" kunci "cfg" (blob 18 B + CRC32)
src/HardwareSensors.*   SensorInput hardware; lidClosed dari saklar pintu
tools/ui_render/        alat dev: render PNG semua layar di PC (Adafruit GFX + shim)
test/                   test_drying_math, test_cycle_aggregation, test_settings, test_input;
                        run_msvc.bat (Windows MSVC), run_gcc.sh (g++)
docs/ui/                desain (HTML+PDF), KEPUTUSAN-UI.md, KONTROL-ROTARY.md, pratinjau/ (38 PNG)
docs/hardware/          skematik CONTROL & POWER
archive/                tuning_web (acuan drying.py/cycle.py), lib_Heatbox + 7 suite lama,
                        lib_EncoderInput, tes_hardware (lcd_test.cpp, rotary_test.cpp)
```

Prinsip (RANCANGAN §9): logika tidak tahu tampilan. Layar hanya membaca `hbui::UiSnapshot`
(disusun di `loop()` tiap 50 ms) dan bertindak lewat `hbui::UiCommands` (playSound,
saveSettings, recheckSensors). Semua akses driver hanya dari task sensor.

### 4.1 platformio.ini
- Env: `esp32doit-devkit-v1` (utama) + `native` (unit test). Env lcd_test/rotary_test dihapus
  (berkasnya di `archive/tes_hardware/`).
- `upload_port = COM5`, `monitor_port = COM5`, 115200.
- build_flags: `-DCORE_DEBUG_LEVEL=0 -DVIDEO_PAL=0 -DENABLE_WIFI=1`.
- lib_deps: `adafruit/Adafruit MAX31865 library@^1.6.0`,
  `roger-random/ESP_8_BIT Color Composite Video Library@1.3.2`,
  `adafruit/Adafruit GFX Library@1.13.0`, `adafruit/Adafruit BusIO@1.17.4`.
  Versi ditebak dari tanggal rilis sebelum tes LCD 28 Okt 2026 (GFX 1.13.0 rilis 20 Okt) —
  **wajib dicek dengan `pio pkg list` saat build pertama di PC**.

### 4.2 Input (docs/ui/KONTROL-ROTARY.md)
- Polling 1 kHz, BUKAN interrupt. Decoder: langkah keluar hanya saat kembali ke detent DAN
  hitungan tepat ±`kEncStepsPerDetent` (4); gerak setengah dibuang (tidak pernah langkah mundur
  palsu); lompatan 2 bit diabaikan. `kEncReverse` untuk membalik arah. Kanan = naik.
- Waktu (`app::input`): TEKAN < 600 ms; bar progres mulai 600 ms; `kHoldActionMs` 2000;
  `kHoldServiceMs` 5000; `kHoldCancelTestMs` 3000 (Prompt 2); debounce tombol 20 ms, pintu
  30 ms; percepatan: klik < 45 ms → langkah 5; anomali "balik arah cepat" < 120 ms.
- Gesture: TAHAN beraksi tepat di target layar aktif lalu tombol harus dilepas dulu; dilepas
  antara 0,6 dtk dan target = tidak ada apa-apa; putar sambil menekan = batal keduanya;
  tombol ditekan saat boot tidak menghasilkan event. Target TAHAN dikirim UI ke task input
  tiap loop (`ui.holdSpec()`).
- TAHAN per layar: Beranda 5 dtk = SERVIS (2 dtk tidak apa-apa); Pilih Profil 2 dtk = MULAI
  (di "< KEMBALI" tidak ada aksi); edit nilai sedang mengedit = BATAL; error SENSOR =
  BERANDA; dialog = TUTUP/batal; lainnya KEMBALI. Hasil (Prompt 2) = Beranda; tes berjalan
  (Prompt 2) = dialog batal terisi 3 dtk.
- Suara bawaan di `Ui::handle`: TEKAN = KLIK, TAHAN = TAHAN SELESAI; layar boleh menimpa.

### 4.3 Suara (`src/sounds.h`)
- Langkah `{f0, f1, ms, gateOn, gateOff}`: f0=0 diam, f0≠f1 glide eksponensial, gate = nada
  terpotong. 7 suara: Welcome (rotor naik + 3 nada), Click, HoldDone (2 bip), Confirm, Error
  (sapuan turun), Warning (dua-ton ~1 dtk), Finish (untuk Prompt 2).
- Karya sendiri; teknik glide terinspirasi OttoDIY (tidak ada kode/nilai disalin).
- LEDC kanal 0 (tidak bentrok I2S/DAC/APLL video). Setelah tiap suara LEDC dilepas dan GPIO12
  dipaksa OUTPUT LOW. Buzzer aktif-LOW dicegah `static_assert`.
- Setelan SUARA nyala/mati di Pengaturan; UJI SUARA memaksa bunyi.

### 4.4 Pengaturan (`lib/HeatboxCycle/CycleSettings`)
- 3 profil {pre-heat °C 50–150, heat °C 80–150, heat ≥ pre-heat}; umum {lama pre-heat 1–60,
  min 1–25, maks 5–60 mnt, min < maks}; profil aktif; suara.
- Bawaan: semua profil 80/110, 10/3/25 mnt, profil 1, suara nyala. Reset default tidak
  menyentuh kalibrasi maupun setelan suara.
- NVS "hbcfg"/"cfg": magic 'H''C', versi 1, aktif, suara, 3×2 profil, 3 umum, CRC32 LE. Rusak /
  tidak sah → bawaan + ditulis ulang.

### 4.5 Serial
- Parser lama tetap: `t c n m s z r v e p x ?`. Report 500 ms **mati bawaan**; `v` = mati →
  human → CSV → mati. MNT tiap menit, CALZERO/CALPT, pesan `# ` tetap. Auto-tare saat nyala tetap.
- Tiap 10 dtk:
  `# RINGKASAN t=120s | berat sd 3.52 mg (19.4 cnt) p-p 18.1 mg spike +0 | suhu 27.41 C | heap 141000 B (min 139000) | render 1.2 fps NTSC | WiFi OK -61 dBm, jam 14:05`
  (sd = SD 64 sampel raw tanpa drift, sama dengan perintah `p`; acuan lama 3,2–6,1 mg).
- Boot mencetak jumlah core, core task, heap bebas setelah video + WiFi.

---

## 5. UI (lib/HeatboxUi) — aturan yang wajib dijaga

- Kanvas 256×240 (desain 768×720 = 3×). Background semua layar PUTIH. Margin 6 px.
- **Zona PARKING x 156–255, y 0–42 WAJIB putih di semua layar** (monitor menimpa tulisan di
  sana). Ditegakkan `Painter::clearParking()` setiap render + dicek per piksel oleh
  `tools/ui_render`.
- Header hanya di kiri (x 6–150): ikon WiFi 8×8, jam T1 (`*--:--` sebelum NTP pernah sinkron),
  ikon pintu 8×8 HANYA saat pintu terbuka, judul T2 di y 20. Jangan tulis nama sensor/hardware.
- Baris petunjuk y 222–234 (T1) dengan garis abu di atasnya; saat TAHAN ≥ 0,6 dtk diganti bar
  progres berlabel aksi ("KEMBALI...", "MULAI...", "SERVIS...", "BATAL...", "TUTUP...").
- Banner y 206–218 (kuning hanya blok berteks hitam; oranye panas; merah error teks putih).
- Font bawaan Adafruit 5×7 (sel 6×8), `cp437(true)`; T1–T4 = setTextSize 1–4; batas karakter
  dalam margin: **T1 40, T2 20, T3 13, T4 10** (Painter memotong otomatis). Semua teks KAPITAL.
  Simbol: ° ± · → √ ◄ ► ≥ (`HB_DEG` dst. di Theme.h; pakai penggabungan literal).
- Palet RGB332: BG 0xFF, teks 0x00, sekunder 0x49, grid 0x92, aksen biru 0x02, OK 0x10,
  waspada 0xFC, error 0xA0, panas 0xF0. Status selalu teks + ikon, tidak hanya warna.
- Ikon besar 16×8 dari desain; ikon kecil 8×8 digambar ulang (dinilai di UJI TAMPILAN).
- Bar tahap tes: 244 px = 5 kotak 48 px + celah 1 px, tinggi 28, PRE/TARE/ISI/PANAS/HASIL;
  selesai = blok hitam + √, sekarang = blok biru + bingkai 2 px, belum = garis abu.
- Render hanya saat tampilan berubah (hash), maks ~10 fps, layar penuh tiap render,
  `copyAfterSwap = false`, lalu `waitForFrame()`.
- Layar Prompt 1: Boot (cek berurutan; gagal timbangan/suhu → banner merah → SENSOR GAGAL;
  WiFi/jam gagal → catatan kuning lalu Beranda; batas jaringan 8 dtk), SensorError (TEKAN cek
  ulang, TAHAN Beranda), Beranda (badge SIAP/SENSOR GAGAL, kartu profil aktif, menu MULAI
  TES/RIWAYAT/PENGATURAN, "BELUM ADA TES", banner panas > 50 °C / hilang < 48 °C), Riwayat
  kosong, Pilih Profil (TEKAN = jadikan aktif, TAHAN = simpan + coba mulai: sensor gagal →
  dialog; pintu terbuka → dialog yang menutup sendiri saat pintu ditutup TANPA mulai otomatis;
  pintu tertutup → dialog "SIKLUS BELUM AKTIF"), Pengaturan (PROFIL 1–3, UMUM, SUARA, RESET
  DEFAULT + konfirmasi, varian terkunci saat tes), Edit nilai (PUTAR pilih kotak → TEKAN edit
  → PUTAR ubah → TEKAN simpan; ditolak = kotak kuning + banner, tetap edit; TAHAN = batal;
  kotak bertumpuk selebar layar), Servis (UJI TAMPILAN 8 halaman, UJI INPUT, UJI SUARA).
- Varian yang masih diuji (RAM saja sampai restart): gaya pilihan A blok biru (bawaan) / B blok
  hitam / C bingkai tebal; teks sekunder abu 0x49 (bawaan) / hitam; ikon 8×8. Setelah Stephen
  memilih, jadikan bawaan di kode dan hapus varian yang tidak dipakai.
- Semua penyimpangan dari desain: `docs/ui/KEPUTUSAN-UI.md`.

Render pratinjau di PC:
```
git clone --depth 1 --branch 1.13.0 https://github.com/adafruit/Adafruit-GFX-Library.git <gfx>
GFX=<gfx> tools/ui_render/build.sh .pio/ui_render     # butuh g++ (WSL/MSYS2)
```

---

## 6. Keputusan & jawaban Stephen (ringkas, berlaku)

1. Batas karakter 40/20/13/10; semua teks kapital; ikon 8×8 digambar ulang; edit profil
   bertumpuk; tampilan boleh disesuaikan / disingkat / pakai ikon asal fungsi tetap.
2. Pembagian core seperti §4 — disetujui.
3. Hardware breadboard = skematik persis; buzzer pasif PWM di GPIO12; pintu GPIO34 LOW =
   tertutup (saklar biasa di breadboard); rotary bawaan 4 transisi/klik, tuning bebas asal
   responsif, tidak ada trigger sendiri, kanan selalu menambah.
4. HeatboxInput menggantikan EncoderInput (diarsipkan); bar 0,6 dtk; putar sambil tekan = batal.
5. Pilih Profil, Edit nilai, Error SENSOR, Servis langsung dari TAHAN 5 dtk — seperti §5.
6. Serial: parser lama tetap, report 500 ms mati, ringkasan 10 dtk wajib.
7. Desain disalin ke `docs/ui/`, skematik ke `docs/hardware/`; render PNG di PC diizinkan.
8. lib/Heatbox + 7 suite lama → archive; tuning_web → archive/tuning_web (acuan uji port C++).
9. **OTA (Prompt 2): ArduinoOTA dengan password dulu**; password di `include/secrets.h`
   (`OTA_PASS`), JANGAN ditulis di kode/dokumen/commit. Nanti diganti lewat aplikasi IoT.
10. **Input WiFi (Prompt 2):** daftar SSID hasil scan, lalu password diketik **satu karakter per
    kolom** (bukan papan huruf penuh): karakter aktif berkedip; PUTAR = ganti karakter urut
    a–z; TEKAN 1× = ganti ke A–Z, TEKAN 1× lagi = kembali a–z; TEKAN 2× = angka & simbol;
    TAHAN di karakter = kunci, lanjut ke kolom berikutnya. Simpan di NVS "hbnet" (ssid, pass).
    **Belum dijawab Stephen** (usulan Claude): TAHAN kunci 0,8 dtk khusus layar ini; item
    "← HAPUS", "√ SELESAI", "X BATAL" di ujung daftar putar; tekan 1× diproses setelah jeda
    ±0,35 dtk untuk membedakan dari 2×; simbol `0-9 . - _ @ ! # $ % & * + = ? /` + spasi.
11. **Servis timbangan minimal (Prompt 2), DISETUJUI:** STATUS SENSOR (berat, suhu,
    STABLE/MOVING live), TARE, KALIBRASI 1 TITIK (acuan 1–100 g, tunggu STABLE, simpan ke NVS
    "heatbox"), SELF-CAL ADS1232, RESET ADS1232. Multi-titik lengkap (desain 03-40.4) belakangan;
    kalibrasi di layar menggantikan dasbor.

---

## 7. Risiko / hal terbuka yang sudah dilaporkan

- Build `pio run` pertama di PC bisa memunculkan beda LDF/versi library → perbaiki minimal.
- Buzzer tanpa resistor seri; 5V_D dipakai bersama → perhatikan noise ADS saat buzzer bunyi.
- Noise ADS dengan WiFi ON vs OFF belum diukur (bandingkan RINGKASAN dengan SPESIFIKASI §5.3).
- Heap bebas setelah video + WiFi belum terukur (dicetak saat boot).
- Langkah per klik rotary (4 atau 2) belum terverifikasi (UJI INPUT "RAW PER KLIK TERAKHIR").
- Dialog sedikit menutupi bar tahap di Pilih Profil (y 62 vs bar sampai y 72) — kosmetik.
- NTSC vs PAL belum diputuskan (foto LCD lama tampak pudar).

---

## 8. Lingkup Prompt 2 (BELUM dikerjakan)

Dari prompt awal + RANCANGAN §13 (langkah 3–7, 8b):
- Siklus pengeringan di firmware (`CycleController`, `HeatBoxDataSource`/snapshot penuh, baris
  CYC), kemungkinan `HeatBoxDummyData` untuk simulasi.
- Pemanas: SSR time-proportional 2 dtk, kontrol PI, interlock (pintu, suhu darurat), urutan K1
  dulu baru SSR. **Hal besar → tanya Stephen dulu.**
- Layar tes berjalan (02-04 … 02-09), dialog batal 3 dtk (02-12), hasil + 7 sub-layar (02-10.x),
  error (02-11a–e), boot "tes terputus" (02-01d).
- Logger LittleFS + riwayat (03-30.x), "hasil terakhir" di Beranda.
- Servis: menu timbangan minimal (§6 no. 11) + yang lain sesuai kebutuhan.
- Input WiFi lewat rotary (§6 no. 10), OTA ArduinoOTA (§6 no. 9).
- Terapkan hasil uji Prompt 1 (varian terpilih, NTSC/PAL, langkah per klik, suara).
- Input untuk Prompt 2 = laporan `docs/ui/HASIL-UJI-PROMPT1.md` dari agent VS Code (Lampiran A).

---

## Lampiran A — Prompt untuk agent VS Code (build, upload, uji LCD)

```text
# PERAN
Kamu agent coding di VS Code (Windows) untuk firmware "Heat Box" (ESP32 DevKit V1,
PlatformIO). Tugasmu: mengambil kode Prompt 1 dari GitHub, build, upload ke board lewat
COM5, lalu memandu saya (Stephen) menjalankan tes di LCD fisik dan mengumpulkan hasilnya
menjadi satu laporan. Saya ada di depan alat. Lapor ke saya dalam Bahasa Indonesia,
singkat, langsung ke inti. Baca dulu docs/KONTEKS-SINKRON.md.

# ATURAN KERAS (jangan dilanggar)
- JANGAN mengubah: pin di include/pins.h, driver lib/Ads1232 dan lib/RtdSensor, konstanta
  RTD_RREF / RTD_OFFSET_C / RTD_WIRES, kalibrasi NVS (namespace "heatbox"), versi platform
  espressif32@6.5.0.
- JANGAN membuat kode apa pun yang menulis HIGH ke GPIO21 (relay K1) atau GPIO22 (SSR
  pemanas). Keduanya harus tetap LOW.
- JANGAN memanggil Wire.begin() (pin I2C bawaan = 21/22 = relay & SSR).
- JANGAN commit include/secrets.h dan JANGAN menampilkan isinya di log/laporan.
- JANGAN push ke GitHub kecuali saya minta.
- Kalau build gagal: perbaiki SEMINIMAL mungkin, hanya yang membuat build gagal, lalu
  laporkan apa yang diubah dan kenapa. Kalau perbaikannya menyentuh hal di daftar atas,
  BERHENTI dan tanya saya.
- Jangan mengerjakan fitur baru (siklus, OTA, input WiFi, servis timbangan = Prompt 2).

# LANGKAH 1 — Ambil kode
  git status   (kalau ada perubahan lokal yang belum disimpan, tanya saya dulu)
  git fetch origin claude/amazing-meitner-45ct0g
  git switch -c lcd-ui FETCH_HEAD   (kalau lcd-ui sudah ada: git switch lcd-ui lalu
                                     git merge --ff-only FETCH_HEAD)
  git log --oneline -8   (harus ada commit "FASE 1", "FASE 2+3", "FASE 4")

# LANGKAH 2 — Kredensial
Tanyakan ke saya SSID WiFi, password WiFi, dan password OTA. Salin
include/secrets.example.h menjadi include/secrets.h dan isi WIFI_SSID, WIFI_PASS,
OTA_PASS. Pastikan `git status` TIDAK menampilkan include/secrets.h.

# LANGKAH 3 — Build
  pio run -e esp32doit-devkit-v1
- Catat RAM dan Flash (acuan: RAM ~51 KB / 15,6 %, flash ~842 KB / 64 %).
- Catat SEMUA warning dari src/, lib/, include/ (abaikan .pio/libdeps). Acuan: 0.
- `pio pkg list -e esp32doit-devkit-v1`: harus ESP_8_BIT 1.3.2, Adafruit GFX 1.13.0,
  Adafruit BusIO 1.17.4, Adafruit MAX31865 1.6.x. Kalau berbeda, laporkan (jangan diubah).
- Unit test: `pio test -e native`; kalau gagal karena tidak ada gcc, `test\run_msvc.bat`
  (butuh MSVC + `pio pkg install -e native` sekali). Acuan: 4 suite, 49 tes.

# LANGKAH 4 — Upload
- Tutup semua Serial Monitor di COM5.
- Ingatkan saya: board harus diberi catu 12 V dari board Power (USB saja = timbangan
  tidak jalan, boot masuk error SENSOR).
  pio run -e esp32doit-devkit-v1 -t upload
  pio device monitor -f log2file

# LANGKAH 5 — Cek serial saat boot
Catat: "# ESP32 2 core, loop/UI di core 1", "Task sensor jalan di core 0", "Heap bebas
setelah video + WiFi", hasil health check ADS1232/MAX31865 + WARNING, dan minimal 6 baris
"# RINGKASAN" (1 menit) dengan timbangan KOSONG dan tidak disentuh.

# LANGKAH 6 — Pandu saya menjalankan checklist LCD (satu per satu, catat jawaban apa adanya)
TAHAN 5 dtk di Beranda = menu SERVIS.
 1. Boot: baris Timbangan, Sensor suhu, Pintu, WiFi, Jam muncul berurutan dengan √/X?
    Ada suara "mesin menyala"?
 2. Warna & ketajaman (NTSC): UJI TAMPILAN hal. 1 (palet) dan 2 (teks). T1 terbaca? Warna
    mana yang pudar/salah?
 3. UJI TAMPILAN hal. 5: bingkai margin 6 px terlihat? Tulisan "PARKING" monitor jatuh di
    dalam garis merah? Hal. 6: bingkai overscan terluar mana yang masih terlihat?
 4. Hal. 4 (ikon 8x8): ikon mana yang tidak jelas?
 5. Hal. 7 (varian pilihan A/B/C) dan 8 (teks abu vs hitam): TEKAN untuk ganti, lihat juga
    di Beranda. Mana yang paling enak?
 6. UJI INPUT: 1 klik pelan → "RAW PER KLIK TERAKHIR" 4 atau 2? Putar kanan 20 klik pelan
    + 20 cepat → naik terus, "KANAN +"? Diam 30 dtk → anomali tetap 0? Tekan singkat,
    tahan (bar di 0,6 dtk), putar sambil menekan ("BATAL (DIPUTAR)"). Saklar pintu →
    "PINTU GPIO34" berubah LOW/HIGH?
 7. TEKAN buka menu, TAHAN 2 dtk kembali, TAHAN 2 dtk di Beranda tidak apa-apa, TAHAN 5 dtk
    di Beranda masuk SERVIS.
 8. Pintu: buka → ikon pintu di header. MULAI TES > tahan 2 dtk → dialog PINTU TERBUKA;
    tutup → dialog hilang sendiri, tes TIDAK mulai; tahan lagi → "SIKLUS BELUM AKTIF".
 9. Pengaturan: heat < pre-heat DITOLAK (kuning), min ≥ maks ditolak, SUARA nyala/mati,
    RESET DEFAULT. Restart board: nilai tersimpan?
10. UJI SUARA: ketujuh suara — mana yang enak/tidak, terlalu pelan/keras?
11. Jam di header sesuai WIB? Tanda * sebelum sinkron?
12. Kalau bisa, panaskan PT100 > 50 °C (tangan/hair dryer, JANGAN pemanas alat) → banner
    oranye "RUANG MASIH PANAS".

# LANGKAH 7 — Uji perbandingan (ubah HANYA build_flags, upload, lalu kembalikan)
 a. -DVIDEO_PAL=1: ulangi checklist 2–3. NTSC atau PAL lebih baik?
 b. -DENABLE_WIFI=0 (NTSC): 6 baris RINGKASAN timbangan kosong lagi; video lebih stabil?
Kembalikan ke VIDEO_PAL=0, ENABLE_WIFI=1 (atau pilihan saya — tanya dulu).

# LANGKAH 8 — Laporan (simpan juga sebagai docs/ui/HASIL-UJI-PROMPT1.md)
1. Build: hasil, RAM, flash, warning, versi library, unit test, perubahan kode (+ alasan).
2. Serial boot: core, heap bebas, health check.
3. Tabel noise sd berat (rata/min/maks) WiFi ON vs OFF vs acuan SPESIFIKASI §5.3.
4. Checklist 1–12 + NTSC/PAL: OK / masalah + keterangan.
5. Pilihan saya: varian A/B/C, teks abu/hitam, ikon 8x8 yang diubah, NTSC/PAL, langkah
   per klik, suara yang diubah.
6. Error / perilaku aneh + potongan log.
Commit di branch lcd-ui. Jangan push kecuali saya minta.
```

---

## Lampiran B — Prompt pembuka untuk chat lokal baru

```text
Lanjutkan proyek Heat Box. Baca dulu docs/KONTEKS-SINKRON.md sampai habis (konteks lengkap
sesi cloud Prompt 1), lalu CLAUDE.md, docs/ui/KEPUTUSAN-UI.md, dan docs/ui/KONTROL-ROTARY.md.
Setelah itu ringkas dalam 5 poin apa yang kamu pahami (status, aturan, lingkup Prompt 2) dan
tunggu instruksi saya. Jangan mengubah berkas apa pun dulu.
```
