#pragma once

// =====================================================================
//  pins.h — pin map & konstanta hardware Heat Box
//  Board : ESP32 DevKit V1 (ESP32-WROOM-32, 30 pin, dual core)
//  Acuan : docs/hardware/Schematic_HMA-CONTROL_2026-11-05.pdf (sheet 1-5)
//          dan Schematic_HMA-POWER_2026-11-05.pdf. Breadboard = skematik.
//
//  SATU-SATUNYA tempat alokasi pin. Library di lib/ tidak membaca file
//  ini; pin diteruskan dari src/ lewat konstruktor.
//
//  Aturan GPIO (ditegakkan static_assert di bagian bawah):
//    GPIO1, 3       : UART0 (Serial monitor)        -> dilarang
//    GPIO6..11      : SPI flash internal modul      -> dilarang
//    GPIO0, 2, 15   : strapping                      -> dilarang
//    GPIO5, 12      : strapping, HANYA untuk DS18B20 (pull-up 4k7 = level
//                     boot benar) dan buzzer (pull-down R27 10k, aktif HIGH)
//    GPIO34..39     : input saja, tanpa pull-up internal -> bukan output
//
//  PERINGATAN I2C: pin I2C bawaan Arduino ESP32 adalah SDA=21, SCL=22,
//  yaitu relay pengaman K1 dan SSR pemanas. JANGAN pernah memanggil
//  Wire.begin() (juga tidak lewat library lain) — itu bisa menyalakan
//  relay/SSR.
// =====================================================================

#include <Adafruit_MAX31865.h>  // max31865_numwires_t
#include <Arduino.h>           // LOW / HIGH

// ---------------------------------------------------------------------
//  MAX31865 (PT100) — hardware SPI, bus VSPI
//  CS sengaja BUKAN GPIO5 (SS default VSPI) karena GPIO5 pin strapping.
// ---------------------------------------------------------------------
#define PIN_MAX_SCK   18
#define PIN_MAX_MISO  19
#define PIN_MAX_MOSI  23
#define PIN_MAX_CS    17

// ---------------------------------------------------------------------
//  ADS1232 (load cell) — bit-bang. R19/R22 100k menahan SCLK/PDWN LOW
//  saat boot.
// ---------------------------------------------------------------------
#define PIN_ADS_SCLK  4
#define PIN_ADS_DOUT  16   // DRDY/DOUT (dual function)
#define PIN_ADS_PDWN  13

// ---------------------------------------------------------------------
//  Rotary encoder EC11 (J3) — polling 1 kHz, bukan interrupt.
//  Tiap jalur: R seri 1k + pull-up 10k ke 3V3 + 100 nF ke GND (~1 ms).
//  Konstanta decoder (langkah per klik, arah) ada di app_config.h.
// ---------------------------------------------------------------------
#define PIN_ENC_A     32   // CLK
#define PIN_ENC_B     33   // DT
#define PIN_ENC_SW    27   // tombol, aktif LOW

// ---------------------------------------------------------------------
//  HMI lain
// ---------------------------------------------------------------------
// Composite video: ESP_8_BIT memakai DAC1 yang secara hardware HANYA ada
// di GPIO25 (langsung ke RCA J9, tanpa R seri). Konstanta ini dokumentasi;
// library tidak menerima nomor pin.
#define PIN_VIDEO     25

// Limit switch pintu (J4): input saja, pull-up R8 10k eksternal, R10 1k
// seri, C17 100 nF; kontak ke GND. Saklar tertekan (pintu tertutup) = LOW.
// Kabel lepas terbaca HIGH = "terbuka", jadi aman (fail-safe).
#define PIN_DOOR              34
#define PIN_DOOR_ACTIVE_LEVEL LOW   // level saat pintu TERTUTUP

// Tombol START 22 mm (J2): pull-up R4 10k eksternal. Belum dipakai;
// pin hanya dipesan supaya tidak dialokasikan untuk hal lain.
#define PIN_START_BTN 35

// Buzzer BZ1 5 V PASIF lewat Q3 BC337 low-side (sheet 5): HIGH = bunyi.
// R27 10k menahan GPIO12 LOW saat boot (strapping MTDI wajib LOW, kalau
// tidak flash diberi 1,8 V dan ESP32 gagal boot). D3 1N4002 = flyback.
#define PIN_BUZZER          12
#define BUZZER_ACTIVE_LOW   0   // 0: HIGH = transistor ON (sesuai skematik)
#define BUZZER_PASSIVE      1   // 1: nada dari PWM LEDC (buzzer magnetik pasif)

// ---------------------------------------------------------------------
//  Pemanas (board Power, sheet Heatlamp) — BC337 low-side, HIGH = NYALA.
//  R16/R17 10k menahan basis LOW. Firmware memaksa OUTPUT LOW di baris
//  pertama setup(); di tahap ini TIDAK ADA kode yang menulis HIGH.
// ---------------------------------------------------------------------
#define PIN_HL_CONTROLLER 22   // SSR lampu (J7)
#define PIN_HL_SAFETY     21   // relay pengaman K1 (memutus L dan N)

// DS18B20 suhu load cell (J5), 1-Wire, pull-up R12 4k7. Belum dipakai.
#define PIN_DS18B20   5

// ---------------------------------------------------------------------
//  Konstanta hardware RTD
//  RTD_RREF = nilai SEBENARNYA resistor referensi di board (nominal 430
//  ohm, kode 431). 12 Okt 2026: dipakai nilai terukur langsung dengan
//  multimeter, 423,12 ohm (cek silang: probe merah-biru 110,8 ohm -
//  pasangan merah 0,9 ohm = 109,9 ohm, rasio cip 0,2595 -> 423,5 ohm).
//  JANGAN diubah untuk menutupi bacaan yang salah: kalau rasio cip tidak
//  cocok dengan PT100, penyebabnya pengawatan atau jumper 2/3/4-wire
//  (perintah 'x' menampilkan cek kewajarannya). Ubah hanya dari acuan fisik.
//
//  RTD_OFFSET_C = koreksi tetap, sebagian (12 Okt 2026, keputusan pemilik).
//  PT100 konsisten lebih tinggi 2,4..2,6 C dari sensor pembanding ruang
//  (25,4 vs 23; 28,6 vs 26); dipilih -1 C, jadi sisa selisih ~+1,5 C.
//  Multimeter genggam (+-0,5..1 %, setara +-1,4..2,9 C) tidak cukup akurat
//  untuk memutus selisih ini lewat RREF. Kalau penyebab aslinya RREF,
//  selisih di 110 C ~+3,2 C sebelum offset (~+2,2 C sesudah).
//  Ganti dengan RREF dari uji titik es (0 C = 100,00 ohm) bila dilakukan.
// ---------------------------------------------------------------------
constexpr float RTD_RREF     = 423.12f;  // ohm
constexpr float RTD_RNOMINAL = 100.0f;  // PT100
constexpr float RTD_OFFSET_C = -1.5f;   // C, ditambahkan ke suhu terhitung

// Mode kabel RTD. Pilihan: MAX31865_2WIRE, MAX31865_3WIRE, MAX31865_4WIRE.
// Catatan: di library Adafruit, 2WIRE dan 4WIRE bernilai sama (bit 3-wire
// = 0). Bedanya hanya di jumper/solder pad board, jadi jumper wajib
// disesuaikan juga saat mengganti nilai ini.
constexpr max31865_numwires_t RTD_WIRES = MAX31865_3WIRE;

// ---------------------------------------------------------------------
//  Guard compile-time
// ---------------------------------------------------------------------
namespace pins_detail {
constexpr bool exists(int gpio) {
    return gpio >= 0 && gpio <= 39 && gpio != 20 && gpio != 24 && !(gpio >= 28 && gpio <= 31);
}
constexpr bool isUartOrFlash(int gpio) { return gpio == 1 || gpio == 3 || (gpio >= 6 && gpio <= 11); }
constexpr bool isStrapping(int gpio) {
    return gpio == 0 || gpio == 2 || gpio == 5 || gpio == 12 || gpio == 15;
}
constexpr bool isInputOnly(int gpio) { return gpio >= 34 && gpio <= 39; }
// Pin strapping hanya boleh dipakai fungsi yang rangkaiannya menjamin
// level boot benar (lihat komentar di atas).
constexpr bool usable(int gpio, bool strappingApproved) {
    return exists(gpio) && !isUartOrFlash(gpio) && (!isStrapping(gpio) || strappingApproved);
}
constexpr int kAllPins[] = {PIN_MAX_SCK,   PIN_MAX_MISO,  PIN_MAX_MOSI,      PIN_MAX_CS,
                            PIN_ADS_SCLK,  PIN_ADS_DOUT,  PIN_ADS_PDWN,      PIN_ENC_A,
                            PIN_ENC_B,     PIN_ENC_SW,    PIN_VIDEO,         PIN_DOOR,
                            PIN_START_BTN, PIN_BUZZER,    PIN_HL_CONTROLLER, PIN_HL_SAFETY,
                            PIN_DS18B20};
constexpr int kPinCount = static_cast<int>(sizeof(kAllPins) / sizeof(kAllPins[0]));
// Rekursif karena core Arduino 2.0.14 dikompilasi C++11 (constexpr tanpa loop)
constexpr bool uniqueFrom(int i, int j) {
    return i >= kPinCount ? true
           : j >= kPinCount ? uniqueFrom(i + 1, i + 2)
           : (kAllPins[i] != kAllPins[j] && uniqueFrom(i, j + 1));
}
constexpr bool allUnique() { return uniqueFrom(0, 1); }
}  // namespace pins_detail

static_assert(pins_detail::allUnique(), "Dua fungsi memakai GPIO yang sama");
static_assert(pins_detail::usable(PIN_MAX_SCK, false),  "PIN_MAX_SCK memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_MAX_MISO, false), "PIN_MAX_MISO memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_MAX_MOSI, false), "PIN_MAX_MOSI memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_MAX_CS, false),   "PIN_MAX_CS memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ADS_SCLK, false), "PIN_ADS_SCLK memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ADS_DOUT, false), "PIN_ADS_DOUT memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ADS_PDWN, false), "PIN_ADS_PDWN memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ENC_A, false),    "PIN_ENC_A memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ENC_B, false),    "PIN_ENC_B memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_ENC_SW, false),   "PIN_ENC_SW memakai GPIO terlarang");
static_assert(PIN_VIDEO == 25, "Composite video ESP_8_BIT hanya bisa di DAC1 = GPIO25");
static_assert(pins_detail::usable(PIN_DOOR, false),      "PIN_DOOR memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_START_BTN, false), "PIN_START_BTN memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_BUZZER, PIN_BUZZER == 12 && !BUZZER_ACTIVE_LOW),
              "PIN_BUZZER terlarang (GPIO12 hanya boleh untuk buzzer aktif-HIGH dengan pull-down)");
static_assert(pins_detail::usable(PIN_HL_CONTROLLER, false), "PIN_HL_CONTROLLER memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_HL_SAFETY, false),     "PIN_HL_SAFETY memakai GPIO terlarang");
static_assert(pins_detail::usable(PIN_DS18B20, PIN_DS18B20 == 5), "PIN_DS18B20 memakai GPIO terlarang");
static_assert(!pins_detail::isInputOnly(PIN_BUZZER) && !pins_detail::isInputOnly(PIN_HL_CONTROLLER) &&
                  !pins_detail::isInputOnly(PIN_HL_SAFETY) && !pins_detail::isInputOnly(PIN_ADS_SCLK) &&
                  !pins_detail::isInputOnly(PIN_ADS_PDWN) && !pins_detail::isInputOnly(PIN_MAX_CS),
              "Pin output tidak boleh di GPIO34..39 (input saja)");
