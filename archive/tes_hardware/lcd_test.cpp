// lcd_test.cpp - Tes LCD composite video untuk ESP32 DevKit V1 (output GPIO25 / DAC1).
// Hanya aktif di env [lcd_test] (build_flags -DLCD_TEST).
// Untuk env utama (esp32doit-devkit-v1) seluruh isi file ini tidak ikut di-compile.
// Dibuat 28 Okt 2026 untuk tes pengenalan LCD; boleh dihapus kalau tes selesai.
#ifdef LCD_TEST

#include <Arduino.h>
#include <ESP_8_BIT_GFX.h>

// true = NTSC, false = PAL. Ganti ke false kalau gambar tidak sinkron / bergulir.
#define VIDEO_NTSC true

ESP_8_BIT_GFX video(VIDEO_NTSC, 8);  // warna 8 bit (RGB332), output selalu di GPIO25

// Warna RGB332
static const uint8_t C_BLACK = 0x00;
static const uint8_t C_WHITE = 0xFF;
static const uint8_t C_RED = 0xE0;
static const uint8_t C_YELLOW = 0xFC;
static const uint8_t C_GREEN = 0x1C;
static const uint8_t C_CYAN = 0x1F;
static const uint8_t C_BLUE = 0x03;
static const uint8_t C_MAGENTA = 0xE3;
static const uint8_t C_GREY = 0x92;

// Warna bingkai dari luar ke dalam (inset 0, 4, 8, ... 28 px)
static const uint8_t frameColor[8] = {C_RED, C_YELLOW, C_GREEN, C_CYAN,
                                      C_BLUE, C_MAGENTA, C_WHITE, C_GREY};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("LCD TEST start (GPIO25)");
  video.begin();
  video.copyAfterSwap = true;
}

void loop() {
  static uint32_t lastPrint = 0;
  const int W = video.width();   // 256
  const int H = video.height();  // 240
  uint32_t sec = millis() / 1000;

  video.waitForFrame();
  video.fillScreen(C_BLACK);

  // Bingkai konsentris tiap 4 px -> untuk mengukur area yang benar-benar terlihat (overscan)
  for (int i = 0; i < 8; i++) {
    int m = i * 4;
    video.drawRect(m, m, W - 2 * m, H - 2 * m, frameColor[i]);
  }

  video.setTextWrap(false);
  video.setTextColor(C_WHITE);

  video.setTextSize(2);
  video.setCursor(80, 40);
  video.print("LCD TEST");

  video.setTextSize(1);
  video.setCursor(65, 62);
  video.print("ESP32 DevKitV1 GPIO25");
  video.setCursor(56, 74);
  video.print(VIDEO_NTSC ? "NTSC" : "PAL");
  video.print("  ");
  video.print(W);
  video.print("x");
  video.print(H);
  video.print(" px");

  // Color bar
  for (int i = 0; i < 8; i++) {
    video.fillRect(48 + i * 20, 90, 20, 28, frameColor[i]);
  }

  // Counter detik (kalau angka naik = tidak freeze)
  video.setTextSize(2);
  video.setCursor(56, 126);
  video.print("Detik ");
  video.print(sec);

  // Keterangan bingkai
  video.setTextSize(1);
  video.setCursor(40, 152);
  video.print("Bingkai luar->dalam (px):");
  video.setCursor(40, 164);
  video.print("0 Merah  4 Kuning  8 Hijau");
  video.setCursor(40, 176);
  video.print("12 Cyan  16 Biru  20 Ungu");
  video.setCursor(40, 188);
  video.print("24 Putih  28 Abu");

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print("LCD TEST jalan, detik=");
    Serial.println(sec);
  }
}

#endif  // LCD_TEST
