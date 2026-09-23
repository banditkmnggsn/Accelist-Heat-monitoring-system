#include "Ads1232.h"

#include <Preferences.h>

#include <cmath>

// =====================================================================
//  Ringkasan protokol ADS1232 (TI SBAS350H)
//
//  1. Data ready  (7.3.10, 7.3.12, Gambar 7-9)
//     DRDY/DOUT turun LOW = data baru siap. Pin ini dipoll di update();
//     tidak ada delay/tunggu.
//
//  2. Shift-out  (7.3.10, 7.3.12, Tabel 7-8)
//     Data 24 bit two's complement, MSB dulu. Tiap rising edge SCLK
//     menggeser bit berikutnya ke DOUT; bit baru valid maks t4 = 50 ns
//     setelah rising edge, bit lama bertahan min t5 = 0 ns. Karena itu
//     DOUT dibaca SETELAH falling edge (>= 1 us setelah rising edge).
//     t2 (DRDY LOW -> SCLK pertama) min 0 ns: boleh langsung clock.
//
//  3. Pulsa tambahan setelah 24 bit
//     - Pulsa ke-25: memaksa DRDY/DOUT HIGH sampai data berikutnya siap
//       (7.3.10: "the pin can be forced high with an additional SCLK";
//       Gambar 7-10 "25th SCLK to Force DRDY/DOUT High"). Wajib karena
//       DRDY dipoll: tanpa pulsa ini DOUT tertahan di nilai LSB dan
//       bisa terbaca sebagai "data ready" palsu.
//     - Pulsa ke-26: memulai offset self-calibration (7.4.1: "apply at
//       least two additional SCLKs after retrieving 24 bits of data...
//       The falling edge of the 26th SCLK begins the calibration cycle",
//       Gambar 7-11). Selesai saat DRDY LOW lagi, t8 = 801.02..801.03 ms
//       @10 SPS (Tabel 7-9). Konversi pertama setelah kalibrasi sudah
//       fully settled dan valid (7.4.1) -> tidak ada yang dibuang.
//       Selama kalibrasi SCLK tidak disentuh (7.4.1: "minimize activity
//       on SCLK").
//     - Pemilihan channel TIDAK lewat SCLK: ADS1232 memakai pin A0
//       (Tabel 7-1). Di modul ini A0 di-jumper ke GND (AIN1).
//
//  4. Standby  (7.4.2, Tabel 7-10)
//     SCLK HIGH selama >= t10 memasukkan chip ke standby:
//     t10 = 12.46 ms (SPEED=1) / 99.96 ms (SPEED=0). SCLK idle harus LOW.
//
//  5. Power-down / reset  (7.4.4, 7.4.5, Tabel 7-12, 7-13)
//     PDWN LOW me-reset seluruh sirkuit. Pulsa min t14 = 26 us.
//     Power-up: PDWN LOW (t15 >= 10 us) -> HIGH (t16 >= 26 us)
//     -> LOW (t17 >= 26 us) -> HIGH. Setelah wake-up, DRDY pertama
//     t11 = 401.8 ms @10 SPS / 52.51 ms @80 SPS.
//
//  6. Logic level  (5.5 Electrical Characteristics)
//     VIH min = 0.7 * DVDD. Dengan DVDD 5 V -> 3.5 V, di atas output
//     GPIO ESP32 (3.3 V). Modul harus diberi DVDD 3.3 V atau pakai level
//     shifter (Gambar 8-1 juga memakai DVDD 3 V untuk MCU 3 V).
// =====================================================================

namespace {

constexpr const char* kNvsNamespace = "heatbox";
// Kunci baru: offset kini disimpan sebagai double. Memakai nama lama akan
// bertabrakan tipe dengan nilai int yang sudah ada di NVS dan getDouble()
// akan diam-diam mengembalikan nilai default.
constexpr const char* kNvsKeyOffset = "ads_offset_d";
constexpr const char* kNvsKeyScale = "ads_cpg";

constexpr uint32_t kSignBit24 = 0x800000UL;
constexpr uint32_t kMagnitudeMask24 = 0x7FFFFFUL;

}  // namespace

Ads1232::Ads1232(const Pins& pins) : pins_(pins) {
    portMUX_INITIALIZE(&mux_);
}

// ---------------------------------------------------------------------
//  begin()
// ---------------------------------------------------------------------
bool Ads1232::begin() {
    // SCLK idle LOW. Kalau HIGH terlalu lama chip masuk standby (7.4.2).
    pinMode(pins_.sclk, OUTPUT);
    digitalWrite(pins_.sclk, LOW);

    pinMode(pins_.pdwn, OUTPUT);
    digitalWrite(pins_.pdwn, LOW);

    // DOUT adalah output push-pull dari ADS1232. Pull-up internal hanya
    // supaya pin yang tidak tersambung terbaca HIGH (= "tidak ada DRDY"),
    // bukan LOW acak yang terlihat seperti data ready.
    pinMode(pins_.dout, INPUT_PULLUP);

    loadCalibration();

    // Power-up sequence 7.4.5 / Gambar 7-15. Delay mikrodetik di sini
    // hanya berjalan sekali dari setup().
    delayMicroseconds(kPowerUpLowUs);  // t15
    digitalWrite(pins_.pdwn, HIGH);
    delayMicroseconds(kPdwnPulseUs);   // t16

    state_ = State::Running;  // supaya reset() tidak mengabaikan panggilan
    reset();                  // t17: PDWN LOW, HIGH-nya dilakukan update()

    // Health check: tunggu DRDY pertama. Deteksi tetap lewat update()
    // (poll pin DOUT), bukan delay.
    const uint32_t waitStartMs = millis();
    while (!firstDrdySeen_ && (millis() - waitStartMs) < kDrdyTimeoutMs) {
        update();
    }

    if (!firstDrdySeen_) {
        health_ = Health::NoDrdy;
    } else if (errorStreak_ > 0) {
        health_ = Health::DoutStuckLow;
    } else {
        health_ = Health::Ok;
    }
    return health_ == Health::Ok;
}

// ---------------------------------------------------------------------
//  update() — state machine, non-blocking
// ---------------------------------------------------------------------
void Ads1232::update() {
    switch (state_) {
        case State::Off:
            return;

        case State::PowerDown:
            if ((micros() - stateStartUs_) >= kPdwnPulseUs) {
                digitalWrite(pins_.pdwn, HIGH);
                discardLeft_ = kDiscardAfterReset;
                enterState(State::Settling);
                lastDrdyMs_ = stateStartMs_;
            }
            return;

        case State::Calibrating:
            if (digitalRead(pins_.dout) == LOW) {
                lastSelfCalMs_ = millis() - stateStartMs_;
                enterState(State::Running);
                pushEvent(Event::SelfCalDone);
                handleDataReady();  // data pertama setelah kalibrasi valid (7.4.1)
            } else if ((millis() - stateStartMs_) > kSelfCalTimeoutMs) {
                enterState(State::Running);
                lastDrdyMs_ = stateStartMs_;
                pushEvent(Event::SelfCalTimeout);
            }
            return;

        case State::Settling:
        case State::Running:
            if (!firstDrdySeen_ && (millis() - lastDrdyMs_) < kWakeupGuardMs) {
                return;  // clock chip belum tentu hidup, level DOUT belum berarti
            }
            if (errorStreak_ > 0 && (millis() - lastErrorMs_) < kConversionPeriodMs) {
                return;  // backoff supaya DOUT yang macet LOW tidak di-clock terus-menerus
            }
            if (digitalRead(pins_.dout) == LOW) {
                handleDataReady();
            }
            return;
    }
}

void Ads1232::enterState(State next) {
    state_ = next;
    stateStartMs_ = millis();
    stateStartUs_ = micros();
}

void Ads1232::handleDataReady() {
    const uint32_t nowMs = millis();

    if (!firstDrdySeen_) {
        firstDrdySeen_ = true;
        firstDrdyMs_ = nowMs - lastDrdyMs_;  // lastDrdyMs_ = saat PDWN HIGH
    } else if (intervalValid_) {
        lastDrdyIntervalMs_ = nowMs - lastDrdyMs_;
    }
    lastDrdyMs_ = nowMs;

    const bool startSelfCal = selfCalRequested_;
    uint32_t code = 0;
    const bool released = shiftOut(startSelfCal ? kPulsesSelfCal : kPulsesRead, code);

    if (!released) {
        // Setelah pulsa ke-25 DOUT wajib HIGH (7.3.10). Kalau masih LOW,
        // SCLK tidak diterima chip (wiring / level logic) atau DOUT short.
        ++protocolErrors_;
        if (++errorStreak_ == 1) {
            pushEvent(Event::ProtocolError);
        }
        lastErrorMs_ = nowMs;
        intervalValid_ = false;
        return;
    }
    errorStreak_ = 0;

    if (startSelfCal) {
        selfCalRequested_ = false;
        resetFilters();  // offset internal berubah, buffer lama tidak berlaku
        intervalValid_ = false;
        enterState(State::Calibrating);
        return;
    }

    intervalValid_ = true;

    if (state_ == State::Settling) {
        if (discardLeft_ > 0) {
            --discardLeft_;
        }
        if (discardLeft_ == 0) {
            enterState(State::Running);
            pushEvent(Event::ResetDone);
        }
        return;
    }

    processSample(toSigned24(code));
}

// ---------------------------------------------------------------------
//  shiftOut() — clock `pulses` pulsa, ambil 24 bit pertama
//
//  Anggaran waktu critical section (worst case = 26 pulsa, self-cal):
//    per pulsa : digitalWrite(H) + delayMicroseconds(1)
//                + digitalWrite(L) + digitalRead + delayMicroseconds(1)
//    delayMicroseconds(1) = busy-wait esp_timer resolusi 1 us -> 1..2 us
//    gpio_set_level / gpio_get_level (core 2.0.14) diperkirakan < 0.5 us
//    => per pulsa <= 2*2 + 3*0.5 = 5.5 us
//    => 26 pulsa <= 143 us, + cek DOUT akhir ~0.5 us  => ~0.15 ms
//
//  Dibandingkan dengan batas:
//    - t3 >= 100 ns (Tabel 7-8)      : tiap fase HIGH/LOW >= 1 us   -> OK
//    - t4 <= 50 ns  (Tabel 7-8)      : DOUT dibaca >= 1 us setelah
//                                      rising edge                   -> OK
//    - t10 >= 12.46 ms (SPEED=1) /
//      99.96 ms (SPEED=0) (Tabel 7-10): SCLK HIGH per pulsa <= ~2.5 us,
//                                      bahkan SELURUH burst ~0.15 ms,
//                                      > 80x di bawah batas terketat -> OK
//    - t7 = 12.5 / 100 ms (Tabel 7-8): pembacaan selesai jauh sebelum
//                                      data berikutnya menimpa       -> OK
//    - f(SCLK) <= 5 MHz (5.3)        : periode >= 2 us = <= 500 kHz -> OK
//    - Interrupt watchdog ESP32
//      (CONFIG_ESP_INT_WDT_TIMEOUT_MS
//      = 300 ms di core 2.0.14)      : ~0.15 ms                       -> OK
//
//  kMaxShiftDurationUs (1 ms) adalah batas desain. Durasi nyata diukur
//  tiap baca (lastShiftDurationUs(), perintah 'p') untuk verifikasi.
//
//  Return: true kalau DOUT HIGH setelah pulsa terakhir (protokol benar).
// ---------------------------------------------------------------------
bool Ads1232::shiftOut(uint8_t pulses, uint32_t& code) {
    uint32_t value = 0;
    const uint32_t startUs = micros();

    portENTER_CRITICAL(&mux_);
    for (uint8_t i = 0; i < pulses; ++i) {
        digitalWrite(pins_.sclk, HIGH);
        delayMicroseconds(kSclkHalfPeriodUs);
        digitalWrite(pins_.sclk, LOW);
        if (i < kDataBits) {
            value = (value << 1) | ((digitalRead(pins_.dout) == HIGH) ? 1UL : 0UL);
        }
        delayMicroseconds(kSclkHalfPeriodUs);
    }
    const bool released = (digitalRead(pins_.dout) == HIGH);
    portEXIT_CRITICAL(&mux_);

    lastShiftUs_ = micros() - startUs;
    code = value;
    return released;
}

// Two's complement 24-bit -> int32_t (7.3.9).
// Bit 23 punya bobot -2^23, bit 22..0 bobot positif biasa. Rumus di bawah
// sama dengan sign-extend bit 23 ke bit 31..24, tapi tanpa cast
// unsigned->signed yang implementation-defined.
//   7FFFFFh ->  8388607,  000001h -> 1,  FFFFFFh -> -1,  800000h -> -8388608
int32_t Ads1232::toSigned24(uint32_t code) {
    return static_cast<int32_t>(code & kMagnitudeMask24) -
           static_cast<int32_t>(code & kSignBit24);
}

// ---------------------------------------------------------------------
//  Statistik raw (sebelum filter apa pun)
//
//  Dipakai dua hal: laporan noise yang jujur, dan sigma untuk gerbang
//  spike. Saat beban benar-benar berubah, window ini ikut memuat step
//  sehingga SD-nya melonjak dan gerbang spike otomatis melonggar — itu
//  memang yang diinginkan supaya perubahan asli tidak ikut ditolak.
// ---------------------------------------------------------------------
void Ads1232::updateRawStats(int32_t value) {
    rawRing_[rawIdx_] = value;
    rawIdx_ = static_cast<uint8_t>((rawIdx_ + 1) % kRawRingSize);
    if (rawCount_ < kRawRingSize) {
        ++rawCount_;
    }
    if (rawCount_ < kRawRingSize) {
        rawStdDev_ = NAN;
        rawPeakToPeak_ = 0;
        return;
    }

    double sum = 0.0;
    int32_t lowest = rawRing_[0];
    int32_t highest = rawRing_[0];
    for (uint8_t i = 0; i < kRawRingSize; ++i) {
        sum += static_cast<double>(rawRing_[i]);
        if (rawRing_[i] < lowest) lowest = rawRing_[i];
        if (rawRing_[i] > highest) highest = rawRing_[i];
    }
    const double mean = sum / kRawRingSize;
    double sumSq = 0.0;
    for (uint8_t i = 0; i < kRawRingSize; ++i) {
        const double d = static_cast<double>(rawRing_[i]) - mean;
        sumSq += d * d;
    }
    rawStdDev_ = static_cast<float>(std::sqrt(sumSq / (kRawRingSize - 1)));
    rawPeakToPeak_ = highest - lowest;
}

// ---------------------------------------------------------------------
//  Filter: gerbang spike -> moving average 16
//
//  Sampel masuk apa adanya ke averager supaya rata-rata 16 sampel benar-
//  benar memberi pengurangan noise sqrt(16). Median-of-5 hanya dipakai
//  sebagai pengganti kalau sampel menyimpang > kSpikeGateSigma dari noise
//  raw yang terukur.
// ---------------------------------------------------------------------
void Ads1232::processSample(int32_t value) {
    raw_ = value;
    clipped_ = (value >= kCodeMax) || (value <= kCodeMin);

    medianBuf_[medianIdx_] = value;
    medianIdx_ = static_cast<uint8_t>((medianIdx_ + 1) % kMedianSize);
    if (medianCount_ < kMedianSize) {
        ++medianCount_;
    }
    const int32_t median = medianOfWindow();

    updateRawStats(value);

    int32_t accepted = value;
    if (std::isnan(rawStdDev_)) {
        accepted = median;  // dasar noise belum diketahui: pakai perilaku konservatif
    } else if (std::fabs(static_cast<double>(value) - static_cast<double>(median)) >
               static_cast<double>(kSpikeGateSigma) * static_cast<double>(rawStdDev_)) {
        accepted = median;
        ++spikesRejected_;
    }

    if (avgCount_ == kAverageSize) {
        avgSum_ -= avgBuf_[avgIdx_];
    } else {
        ++avgCount_;
    }
    avgBuf_[avgIdx_] = accepted;
    avgSum_ += accepted;
    avgIdx_ = static_cast<uint8_t>((avgIdx_ + 1) % kAverageSize);
    filtered_ = static_cast<double>(avgSum_) / avgCount_;

    // Rentang pergerakan keluaran: dasar deteksi stabil
    stableRing_[stableIdx_] = filtered_;
    stableIdx_ = static_cast<uint8_t>((stableIdx_ + 1) % kStableWindow);
    if (stableCount_ < kStableWindow) {
        ++stableCount_;
        stableSpan_ = NAN;
    } else {
        double lowest = stableRing_[0];
        double highest = stableRing_[0];
        for (uint8_t i = 1; i < kStableWindow; ++i) {
            if (stableRing_[i] < lowest) lowest = stableRing_[i];
            if (stableRing_[i] > highest) highest = stableRing_[i];
        }
        stableSpan_ = static_cast<float>(highest - lowest);
    }

    if (avgCount_ == kAverageSize) {
        // standar deviasi sampel (n-1) dari isi window moving average
        double sumSq = 0.0;
        for (uint8_t i = 0; i < kAverageSize; ++i) {
            const double d = static_cast<double>(avgBuf_[i]) - filtered_;
            sumSq += d * d;
        }
        stdDev_ = static_cast<float>(std::sqrt(sumSq / (kAverageSize - 1)));
    } else {
        stdDev_ = NAN;
    }

    newSample_ = true;

    if (job_ != Job::None) {
        // raw setelah gerbang spike, bukan hasil median
        if (jobCount_ == 0) {
            jobBase_ = static_cast<double>(accepted);
        }
        const double delta = static_cast<double>(accepted) - jobBase_;
        jobSum_ += delta;
        jobSumSq_ += delta * delta;
        if (++jobCount_ >= kTareSamples) {
            finishJob();
        }
    }
}

int32_t Ads1232::medianOfWindow() const {
    int32_t sorted[kMedianSize];
    for (uint8_t i = 0; i < medianCount_; ++i) {
        sorted[i] = medianBuf_[i];
    }
    // insertion sort, maksimal 5 elemen
    for (uint8_t i = 1; i < medianCount_; ++i) {
        const int32_t key = sorted[i];
        int8_t j = static_cast<int8_t>(i - 1);
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = key;
    }
    return sorted[medianCount_ / 2];
}

void Ads1232::resetFilters() {
    medianIdx_ = 0;
    medianCount_ = 0;
    avgIdx_ = 0;
    avgCount_ = 0;
    avgSum_ = 0;
    filtered_ = NAN;
    stdDev_ = NAN;
    rawIdx_ = 0;
    rawCount_ = 0;
    rawStdDev_ = NAN;
    rawPeakToPeak_ = 0;
    stableIdx_ = 0;
    stableCount_ = 0;
    stableSpan_ = NAN;
    clipped_ = false;
    newSample_ = false;
}

// ---------------------------------------------------------------------
//  reset() — PDWN cycle, non-blocking
//
//  Justifikasi kDiscardAfterReset = 4:
//  - 7.4.4 menyebut power-down me-reset seluruh sirkuit, dan Gambar 7-14
//    memberi t11 = 401.8 ms sampai DRDY pertama. Angka ini sama dengan
//    waktu DRDY ditahan HIGH saat filter settle setelah ganti MUX
//    (7.3.7, Tabel 7-5, t1 = 401 ms), dan untuk kasus MUX datasheet
//    menulis "There is no need to discard any data".
//  - Tetapi 7.4.4 TIDAK menyatakan eksplisit bahwa data pertama setelah
//    PDWN sudah settled. Maka dipakai worst case 7.3.7 untuk perubahan
//    input mendadak: "Discard the first four readings". Biayanya hanya
//    400 ms.
//  TODO VERIFY: apakah data pertama setelah PDWN sudah fully settled?
//    (SBAS350H 7.4.4 / Gambar 7-14 / Tabel 7-12 tidak eksplisit). Cek di
//    hardware: log raw 6 konversi pertama setelah perintah 'r' dengan
//    beban tetap; kalau konversi ke-1 sudah sama dengan ke-5 (dalam
//    batas noise), kDiscardAfterReset boleh diturunkan ke 0.
//  TODO VERIFY: apakah PDWN juga menghapus hasil offset calibration dan
//    apakah chip melakukan offset calibration otomatis saat power-up?
//    SBAS350H 7.4.1 dan 7.4.4 tidak menyebutkannya. Dampaknya: offset
//    tare yang tersimpan di NVS bisa bergeser antar boot kalau 'z' tidak
//    dijalankan. Cek: bandingkan raw timbangan kosong antar beberapa boot
//    dengan dan tanpa 'z'.
// ---------------------------------------------------------------------
void Ads1232::reset() {
    if (state_ == State::Off) {
        return;
    }
    if (job_ != Job::None) {
        job_ = Job::None;
        pushEvent(Event::JobCanceled);
    }
    selfCalRequested_ = false;
    resetFilters();
    firstDrdySeen_ = false;
    intervalValid_ = false;
    errorStreak_ = 0;

    digitalWrite(pins_.sclk, LOW);
    digitalWrite(pins_.pdwn, LOW);
    enterState(State::PowerDown);
}

// ---------------------------------------------------------------------
//  Offset self-calibration
// ---------------------------------------------------------------------
bool Ads1232::selfCalibrate() {
    const bool stateOk = (state_ == State::Running) || (state_ == State::Settling);
    if (!stateOk || job_ != Job::None || selfCalRequested_) {
        return false;
    }
    // 26 pulsa dikirim pada DRDY berikutnya (kalibrasi hanya bisa
    // dimulai setelah 24 bit dibaca, 7.4.1)
    selfCalRequested_ = true;
    return true;
}

// ---------------------------------------------------------------------
//  Tare & kalibrasi (background, hasil lewat event)
// ---------------------------------------------------------------------
bool Ads1232::canStartJob() const {
    return state_ != State::Off && state_ != State::Calibrating &&
           !selfCalRequested_ && job_ == Job::None;
}

void Ads1232::startJob(Job job) {
    job_ = job;
    jobBase_ = 0.0;
    jobSum_ = 0.0;
    jobSumSq_ = 0.0;
    jobCount_ = 0;
}

void Ads1232::tare() {
    if (!canStartJob()) {
        pushEvent(Event::JobRejected);
        return;
    }
    startJob(Job::Tare);
}

void Ads1232::calibrateWith(float knownGrams) {
    if (!std::isfinite(knownGrams) || knownGrams <= 0.0f || !canStartJob()) {
        pushEvent(Event::JobRejected);
        return;
    }
    jobKnownGrams_ = knownGrams;
    startJob(Job::Calibrate);
}

void Ads1232::measure() {
    if (!canStartJob()) {
        pushEvent(Event::JobRejected);
        return;
    }
    measureMean_ = NAN;
    measureSd_ = NAN;
    measureSamples_ = 0;
    startJob(Job::Measure);
}

void Ads1232::finishJob() {
    const double mean = jobBase_ + jobSum_ / jobCount_;
    const Job finished = job_;
    job_ = Job::None;

    if (finished == Job::Measure) {
        const double variance =
            (jobSumSq_ - jobSum_ * jobSum_ / jobCount_) / static_cast<double>(jobCount_ - 1);
        measureMean_ = mean;
        measureSd_ = static_cast<float>(std::sqrt(variance > 0.0 ? variance : 0.0));
        measureSamples_ = jobCount_;
        pushEvent(Event::MeasureDone);
        return;
    }

    if (finished == Job::Tare) {
        offsetCounts_ = mean;  // pecahan count dipertahankan (1 count ~ 0.18 mg)
        hasOffset_ = true;
        pushEvent(saveCalibration() ? Event::TareDone : Event::NvsWriteFailed);
        return;
    }

    const double span = mean - offsetCounts_;
    if (std::fabs(span) < kMinCalibrationSpanCounts) {
        pushEvent(Event::CalibrationFailed);
        return;
    }
    // scale negatif valid (load cell terpasang terbalik); grams() tetap positif
    countsPerGram_ = static_cast<float>(span / static_cast<double>(jobKnownGrams_));
    hasScale_ = true;
    pushEvent(saveCalibration() ? Event::CalibrationDone : Event::NvsWriteFailed);
}

void Ads1232::setScale(float countsPerGram) {
    if (!std::isfinite(countsPerGram) || std::fabs(countsPerGram) < kMinAbsCountsPerGram) {
        pushEvent(Event::JobRejected);
        return;
    }
    countsPerGram_ = countsPerGram;
    hasScale_ = true;
    if (!saveCalibration()) {
        pushEvent(Event::NvsWriteFailed);
    }
}

// ---------------------------------------------------------------------
//  NVS (Preferences, namespace "heatbox")
// ---------------------------------------------------------------------
void Ads1232::loadCalibration() {
    offsetCounts_ = static_cast<double>(kDefaultOffset);
    countsPerGram_ = kDefaultCountsPerGram;
    hasOffset_ = false;
    hasScale_ = false;

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return;  // namespace belum pernah dibuat -> pakai default
    }
    if (prefs.isKey(kNvsKeyOffset)) {
        offsetCounts_ = prefs.getDouble(kNvsKeyOffset, static_cast<double>(kDefaultOffset));
        hasOffset_ = true;
    }
    if (prefs.isKey(kNvsKeyScale)) {
        const float stored = prefs.getFloat(kNvsKeyScale, kDefaultCountsPerGram);
        if (std::isfinite(stored) && std::fabs(stored) >= kMinAbsCountsPerGram) {
            countsPerGram_ = stored;
            hasScale_ = true;
        }
    }
    prefs.end();
}

// Tulis flash beberapa ms; hanya dipanggil saat tare/kalibrasi selesai.
bool Ads1232::saveCalibration() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        return false;
    }
    bool ok = prefs.putDouble(kNvsKeyOffset, offsetCounts_) > 0;
    if (hasScale_) {
        ok = (prefs.putFloat(kNvsKeyScale, countsPerGram_) > 0) && ok;
    }
    prefs.end();
    return ok;
}

// ---------------------------------------------------------------------
//  Event queue
// ---------------------------------------------------------------------
void Ads1232::pushEvent(Event event) {
    if (eventCount_ == kEventQueueSize) {
        // antrian penuh: buang event paling lama
        eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % kEventQueueSize);
        --eventCount_;
    }
    const uint8_t tail = static_cast<uint8_t>((eventHead_ + eventCount_) % kEventQueueSize);
    events_[tail] = event;
    ++eventCount_;
}

Ads1232::Event Ads1232::takeEvent() {
    if (eventCount_ == 0) {
        return Event::None;
    }
    const Event event = events_[eventHead_];
    eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % kEventQueueSize);
    --eventCount_;
    return event;
}

// ---------------------------------------------------------------------
//  Getter
// ---------------------------------------------------------------------
bool Ads1232::hasNewSample() {
    const bool fresh = newSample_;
    newSample_ = false;
    return fresh;
}

int32_t Ads1232::raw() const { return raw_; }

float Ads1232::grams() const {
    if (std::isnan(filtered_)) {
        return NAN;
    }
    return static_cast<float>((filtered_ - offsetCounts_) / static_cast<double>(countsPerGram_));
}

// Ambang dinyatakan dalam mg supaya tetap berarti kalau skala berubah;
// jatuh kembali ke count selama belum ada kalibrasi.
float Ads1232::stableThresholdCounts() const {
    if (!hasScale_) {
        return kStableSpanCounts;
    }
    return kStableSpanMg * std::fabs(countsPerGram_) / 1000.0f;
}

// Stabil = angka yang tampil tidak bergerak lebih dari ambang selama
// kStableWindow sampel terakhir.
bool Ads1232::isStable() const {
    return state_ == State::Running && stableCount_ == kStableWindow &&
           !std::isnan(stableSpan_) && stableSpan_ < stableThresholdCounts();
}

Ads1232::Health Ads1232::health() const { return health_; }
bool Ads1232::isCalibrated() const { return hasScale_; }
bool Ads1232::hasStoredOffset() const { return hasOffset_; }

bool Ads1232::isBusy() const {
    return job_ != Job::None || selfCalRequested_ || state_ == State::Calibrating;
}

bool Ads1232::isSettling() const {
    return state_ == State::PowerDown || state_ == State::Settling ||
           state_ == State::Calibrating;
}

bool Ads1232::isClipped() const { return clipped_; }

bool Ads1232::drdyTimedOut() const {
    const bool waitingData = (state_ == State::Settling) || (state_ == State::Running);
    return waitingData && (millis() - lastDrdyMs_) > kDrdyTimeoutMs;
}

bool Ads1232::doutError() const { return errorStreak_ > 0; }
float Ads1232::countsPerGram() const { return countsPerGram_; }
int32_t Ads1232::offset() const { return static_cast<int32_t>(std::lround(offsetCounts_)); }
double Ads1232::offsetCounts() const { return offsetCounts_; }
float Ads1232::filteredCounts() const { return static_cast<float>(filtered_); }
float Ads1232::stdDevCounts() const { return stdDev_; }
float Ads1232::stableSpanCounts() const { return stableSpan_; }
float Ads1232::rawStdDevCounts() const { return rawStdDev_; }
int32_t Ads1232::rawPeakToPeakCounts() const { return rawPeakToPeak_; }
uint32_t Ads1232::spikesRejected() const { return spikesRejected_; }
double Ads1232::measureMean() const { return measureMean_; }
float Ads1232::measureSd() const { return measureSd_; }
uint8_t Ads1232::measureSamples() const { return measureSamples_; }
uint32_t Ads1232::firstDrdyMs() const { return firstDrdyMs_; }
uint32_t Ads1232::lastDrdyIntervalMs() const { return lastDrdyIntervalMs_; }
uint32_t Ads1232::lastSelfCalMs() const { return lastSelfCalMs_; }
uint32_t Ads1232::lastShiftDurationUs() const { return lastShiftUs_; }
uint32_t Ads1232::protocolErrorCount() const { return protocolErrors_; }
