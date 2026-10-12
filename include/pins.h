#pragma once

// =====================================================================
//  pins.h — pin map & konstanta hardware Heat Box (tahap breadboard)
//  Board : ESP32 DevKit V1 (ESP32-WROOM-32, 30 pin)
//
//  SATU-SATUNYA tempat alokasi pin. Library di lib/ tidak membaca file
//  ini; pin diteruskan dari main.cpp lewat konstruktor.
//
//  PIN TERLARANG (jangan dialokasikan untuk apa pun):
//    GPIO25, GPIO26          : cadangan output composite video
//                              (ESP_8_BIT_composite)
//    GPIO0, 2, 5, 12, 15     : strapping
//    GPIO1, 3                : UART0 (Serial monitor)
//    GPIO6..11               : SPI flash internal modul
//    GPIO14, 21, 22, 34..39  : cadangan fase berikutnya
//  Dicek saat compile oleh static_assert di bagian bawah file.
// =====================================================================

#include <Adafruit_MAX31865.h>  // max31865_numwires_t

// ---------------------------------------------------------------------
//  MAX31865 (PT100) — hardware SPI, bus VSPI
//  CS sengaja BUKAN GPIO5 (SS default VSPI) karena GPIO5 pin strapping.
// ---------------------------------------------------------------------
#define PIN_MAX_SCK   18
#define PIN_MAX_MISO  19
#define PIN_MAX_MOSI  23
#define PIN_MAX_CS    17

// ---------------------------------------------------------------------
//  ADS1232 (load cell) — bit-bang
// ---------------------------------------------------------------------
#define PIN_ADS_SCLK  4
#define PIN_ADS_DOUT  16   // DRDY/DOUT (dual function)
#define PIN_ADS_PDWN  13

// ---------------------------------------------------------------------
//  Rotary encoder EC11 — polling, INPUT_PULLUP
// ---------------------------------------------------------------------
#define PIN_ENC_A     32
#define PIN_ENC_B     33
#define PIN_ENC_SW    27

// ---------------------------------------------------------------------
//  Konstanta hardware RTD
//  RTD_RREF = nilai SEBENARNYA resistor referensi di board (nominal 430
//  ohm, kode 431). Tuning 12 Okt 2026 dengan multimeter sebagai acuan:
//    probe merah-biru 110,8 ohm - pasangan merah 0,9 ohm = 109,9 ohm
//    (3-wire membuang hambatan kabel), rasio cip 0,2595 -> 423,5 ohm;
//    cocok dengan resistor terukur langsung 423,12 ohm.
//  JANGAN diubah untuk menutupi bacaan yang salah: kalau rasio cip tidak
//  cocok dengan PT100, penyebabnya pengawatan atau jumper 2/3/4-wire
//  (perintah 'x' menampilkan cek kewajarannya). Ubah hanya dari acuan fisik.
// ---------------------------------------------------------------------
constexpr float RTD_RREF     = 423.5f;
constexpr float RTD_RNOMINAL = 100.0f;  // PT100

// Mode kabel RTD. Pilihan: MAX31865_2WIRE, MAX31865_3WIRE, MAX31865_4WIRE.
// Catatan: di library Adafruit, 2WIRE dan 4WIRE bernilai sama (bit 3-wire
// = 0). Bedanya hanya di jumper/solder pad board, jadi jumper wajib
// disesuaikan juga saat mengganti nilai ini.
constexpr max31865_numwires_t RTD_WIRES = MAX31865_3WIRE;

// ---------------------------------------------------------------------
//  Guard compile-time: pin terpakai tidak boleh masuk daftar terlarang
// ---------------------------------------------------------------------
namespace pins_detail {
constexpr bool isForbiddenGpio(int gpio) {
    return gpio == 0 || gpio == 1 || gpio == 2 || gpio == 3 || gpio == 5 ||
           (gpio >= 6 && gpio <= 12) || gpio == 14 || gpio == 15 ||
           gpio == 21 || gpio == 22 || gpio == 25 || gpio == 26 ||
           (gpio >= 34 && gpio <= 39);
}
}  // namespace pins_detail

static_assert(!pins_detail::isForbiddenGpio(PIN_MAX_SCK),  "PIN_MAX_SCK memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_MAX_MISO), "PIN_MAX_MISO memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_MAX_MOSI), "PIN_MAX_MOSI memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_MAX_CS),   "PIN_MAX_CS memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ADS_SCLK), "PIN_ADS_SCLK memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ADS_DOUT), "PIN_ADS_DOUT memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ADS_PDWN), "PIN_ADS_PDWN memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ENC_A),    "PIN_ENC_A memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ENC_B),    "PIN_ENC_B memakai GPIO terlarang");
static_assert(!pins_detail::isForbiddenGpio(PIN_ENC_SW),   "PIN_ENC_SW memakai GPIO terlarang");
