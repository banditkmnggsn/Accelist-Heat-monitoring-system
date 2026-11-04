// rotary_test.cpp - Tes rotary encoder (modul EC11 + PCB) untuk ESP32 DevKit V1.
// Hanya aktif di env [rotary_test] (build_flags -DROTARY_TEST).
// Untuk env lain seluruh isi file ini tidak ikut di-compile.
//
// Jalur sinyal SAMA PERSIS dengan schematic Main (J3 Rotary EC11 XH 5P):
//   J3.5 CLK -> R5  1k -> ENC_A  -> GPIO32 (D32)   pull-up R3  10k ke 3V3, C14 100n ke GND
//   J3.4 DT  -> R9  1k -> ENC_B  -> GPIO33 (D33)   pull-up R7  10k ke 3V3, C16 100n ke GND
//   J3.3 SW  -> R13 1k -> ENC_SW -> GPIO27 (D27)   pull-up R11 10k ke 3V3, C18 100n ke GND
//   J3.2 +   -> 3V3
//   J3.1 GND -> GND
// LCD composite tetap di GPIO25 (J9 VIDEO), jadi tes ini sekalian mengecek
// encoder (polling) berjalan bareng library video tanpa saling ganggu.
//
// Encoder dibaca dengan POLLING tiap 1 ms di task terpisah (bukan interrupt),
// sesuai keputusan proyek: hindari ISR yang bentrok dengan timing library video.
#ifdef ROTARY_TEST

#include <Arduino.h>
#include <ESP_8_BIT_GFX.h>

static const int PIN_ENC_A  = 32;  // CLK
static const int PIN_ENC_B  = 33;  // DT
static const int PIN_ENC_SW = 27;  // SW (aktif LOW)

// Transisi per klik (detent). EC11 umumnya 4. Kalau 1 klik terbaca 2 langkah, ganti ke 2.
#define STEPS_PER_DETENT 4

ESP_8_BIT_GFX video(true, 8);  // NTSC (sudah terbukti cocok di tes LCD), warna 8 bit

static const uint8_t C_BLACK = 0x00, C_WHITE = 0xFF, C_RED = 0xE0, C_YELLOW = 0xFC,
                     C_GREEN = 0x1C, C_CYAN = 0x1F, C_GREY = 0x92;

// Tabel decode quadrature: index = (stateLama << 2) | stateBaru, state = (A << 1) | B
static const int8_t QDEC[16] = {0, -1, 1, 0,
                                1, 0, 0, -1,
                                -1, 0, 0, 1,
                                0, 1, -1, 0};

volatile int32_t  rawCount   = 0;   // jumlah transisi valid (+ / -)
volatile uint32_t errCount   = 0;   // transisi loncat (A & B berubah bersamaan) = noise / terlalu cepat
volatile uint32_t pressCount = 0;   // jumlah tombol ditekan
volatile bool     swPressed  = false;
volatile int8_t   lastDir    = 0;   // +1 / -1
volatile uint8_t  pinState   = 0;   // bit2 = SW, bit1 = A, bit0 = B (nilai mentah)

static void encoderTask(void *) {
  uint8_t prev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  uint8_t swRaw = digitalRead(PIN_ENC_SW);
  uint8_t swStable = swRaw;
  uint32_t swChangedAt = millis();

  for (;;) {
    uint8_t a = digitalRead(PIN_ENC_A);
    uint8_t b = digitalRead(PIN_ENC_B);
    uint8_t cur = (a << 1) | b;
    if (cur != prev) {
      int8_t d = QDEC[(prev << 2) | cur];
      if (d == 0) {
        errCount++;
      } else {
        rawCount += d;
        lastDir = d;
      }
      prev = cur;
    }

    // Debounce tombol 20 ms (di atas filter RC 1k/100n di board)
    uint8_t sw = digitalRead(PIN_ENC_SW);
    if (sw != swRaw) {
      swRaw = sw;
      swChangedAt = millis();
    }
    if (sw != swStable && (millis() - swChangedAt) >= 20) {
      swStable = sw;
      swPressed = (swStable == LOW);
      if (swPressed) pressCount++;
    }

    pinState = (sw << 2) | (a << 1) | b;
    vTaskDelay(1);  // polling 1 ms
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ROTARY TEST start (A=GPIO32, B=GPIO33, SW=GPIO27)");

  // Pull-up eksternal 10k sudah ada di board/modul. Pull-up internal ikut dinyalakan
  // supaya tes di breadboard tetap aman kalau resistor eksternal belum terpasang.
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  video.begin();
  video.copyAfterSwap = true;

  // Task encoder di core 0, loop (gambar LCD) tetap di core 1
  xTaskCreatePinnedToCore(encoderTask, "enc", 2048, nullptr, 2, nullptr, 0);
}

void loop() {
  static int32_t lastRawPrinted = 0;
  static uint32_t lastPressPrinted = 0;
  static uint32_t lastErrPrinted = 0;

  int32_t raw = rawCount;
  int32_t pos = raw / STEPS_PER_DETENT;
  uint32_t press = pressCount;
  uint32_t err = errCount;
  bool down = swPressed;
  int8_t dir = lastDir;
  uint8_t ps = pinState;

  // ---- Serial: hanya cetak kalau ada perubahan ----
  if (raw != lastRawPrinted || press != lastPressPrinted || err != lastErrPrinted) {
    Serial.printf("pos=%ld raw=%ld arah=%s tombol=%s tekan=%lu error=%lu | A=%d B=%d SW=%d\n",
                  (long)pos, (long)raw, dir > 0 ? "CW" : (dir < 0 ? "CCW" : "-"),
                  down ? "DITEKAN" : "lepas", (unsigned long)press, (unsigned long)err,
                  (ps >> 1) & 1, ps & 1, (ps >> 2) & 1);
    lastRawPrinted = raw;
    lastPressPrinted = press;
    lastErrPrinted = err;
  }

  // ---- LCD ----
  const int W = video.width();   // 256
  video.waitForFrame();
  video.fillScreen(C_BLACK);
  video.drawRect(8, 8, W - 16, video.height() - 16, C_GREY);
  video.setTextWrap(false);

  video.setTextColor(C_WHITE);
  video.setTextSize(2);
  video.setCursor(56, 20);
  video.print("ROTARY TEST");

  video.setTextSize(1);
  video.setTextColor(C_GREY);
  video.setCursor(40, 42);
  video.print("A=GPIO32 B=GPIO33 SW=GPIO27");

  // Posisi (klik)
  video.setTextColor(C_WHITE);
  video.setCursor(40, 62);
  video.print("Posisi (klik):");
  video.setTextSize(3);
  video.setTextColor(C_YELLOW);
  video.setCursor(40, 74);
  video.print(pos);

  // Bar posisi: tengah = 0, 1 klik = 4 px
  int cx = W / 2;
  int bx = cx + constrain((int)pos * 4, -100, 100);
  video.drawFastHLine(28, 112, W - 56, C_GREY);
  video.drawFastVLine(cx, 106, 13, C_GREY);
  video.fillRect(bx - 3, 104, 7, 17, C_CYAN);

  video.setTextSize(1);
  video.setTextColor(C_WHITE);
  video.setCursor(40, 132);
  video.print("Arah terakhir: ");
  video.setTextColor(dir > 0 ? C_GREEN : (dir < 0 ? C_RED : C_GREY));
  video.print(dir > 0 ? "CW (kanan)" : (dir < 0 ? "CCW (kiri)" : "-"));

  video.setTextColor(C_WHITE);
  video.setCursor(40, 146);
  video.print("Raw: ");
  video.print(raw);
  video.print("   Error: ");
  video.setTextColor(err ? C_RED : C_WHITE);
  video.print(err);

  // Tombol
  video.setTextColor(C_WHITE);
  video.setCursor(40, 162);
  video.print("Tombol: ");
  if (down) {
    video.fillRect(88, 158, 60, 15, C_GREEN);
    video.setTextColor(C_BLACK);
    video.setCursor(94, 162);
    video.print("DITEKAN");
  } else {
    video.setTextColor(C_GREY);
    video.print("lepas");
  }
  video.setTextColor(C_WHITE);
  video.setCursor(40, 178);
  video.print("Jumlah tekan: ");
  video.print(press);

  // Level mentah pin (1 = HIGH, 0 = LOW)
  video.setTextColor(C_GREY);
  video.setCursor(40, 196);
  video.print("Pin: A=");
  video.print((ps >> 1) & 1);
  video.print(" B=");
  video.print(ps & 1);
  video.print(" SW=");
  video.print((ps >> 2) & 1);
}

#endif  // ROTARY_TEST