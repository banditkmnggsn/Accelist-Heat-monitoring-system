// =====================================================================
//  main.cpp — Heat Box, tahap riset breadboard
//
//  Tujuan tahap ini HANYA: semua sensor terbaca benar dan stabil di
//  Serial Monitor. Belum ada LCD, heater, atau kontrol proses.
//
//  Output:
//    - baris report tiap app::kReportPeriodMs (human readable / CSV)
//    - pesan event/diagnostik selalu diawali "# " supaya mudah dibuang
//      saat log CSV diolah
// =====================================================================

#include <Arduino.h>

#include <Ads1232.h>
#include <EncoderInput.h>
#include <RtdSensor.h>

#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdlib>

#include "app_config.h"
#include "pins.h"

namespace {

enum class ReportMode : uint8_t { Human, Csv };

constexpr const char* kCsvHeader = "millis,tempC,rtdOhm,rtdFault,rawCounts,grams,stable,encPos";

RtdSensor rtd({PIN_MAX_CS, PIN_MAX_SCK, PIN_MAX_MISO, PIN_MAX_MOSI, RTD_WIRES, RTD_RREF, RTD_RNOMINAL});
Ads1232 loadCell({PIN_ADS_SCLK, PIN_ADS_DOUT, PIN_ADS_PDWN});
EncoderInput encoder({PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW});

// Kalibrasi multi-titik: firmware hanya MENGUKUR dan MENCETAK. Penyimpanan,
// fit, dan metrik dikerjakan di PC (tuning_web), sesuai arsitektur proyek.
enum class MeasureKind : uint8_t { None, Zero, Point };
MeasureKind pendingMeasure = MeasureKind::None;
float pendingNominalMg = 0.0f;
char pendingDirection = 'U';

ReportMode reportMode = ReportMode::Human;
uint32_t lastReportMs = 0;
char buttonLatch = '-';  // 'S' / 'L' ditahan sampai tercetak di report
bool adsTimeoutReported = false;
bool autoTarePending = true;  // initial zero setting, dikerjakan sekali per boot
bool settledReported = false;
uint32_t lastRtdCheckMs = 0;

char commandBuffer[app::kCommandBufferSize];
size_t commandLength = 0;
bool commandOverflow = false;

// =====================================================================
//  Util
// =====================================================================
void logLine(const char* format, ...) __attribute__((format(printf, 1, 2)));

void logLine(const char* format, ...) {
    char line[app::kLogBufferSize];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Serial.print("# ");
    Serial.println(line);
}

const char* wireModeText() {
    return (RTD_WIRES == MAX31865_3WIRE) ? "3-wire" : "2/4-wire";
}

float samplesToSeconds(uint32_t samples) {
    return static_cast<float>(samples * Ads1232::kConversionPeriodMs) / app::kMsPerSecond;
}

// =====================================================================
//  Banner & bantuan
// =====================================================================
void printBanner() {
    Serial.println();
    Serial.println("==============================================================");
    Serial.printf(" %s\n", app::kFirmwareName);
    Serial.printf(" Firmware v%s (build %s %s)\n", app::kFirmwareVersion, __DATE__, __TIME__);
    Serial.println("==============================================================");
    Serial.println(" Pin map (ESP32 DevKit V1 / WROOM-32):");
    Serial.printf("   MAX31865 (VSPI) : SCK=%d MISO=%d MOSI=%d CS=%d\n",
                  PIN_MAX_SCK, PIN_MAX_MISO, PIN_MAX_MOSI, PIN_MAX_CS);
    Serial.printf("   ADS1232         : SCLK=%d DOUT/DRDY=%d PDWN=%d\n",
                  PIN_ADS_SCLK, PIN_ADS_DOUT, PIN_ADS_PDWN);
    Serial.printf("   Encoder EC11    : A=%d B=%d SW=%d\n", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
    Serial.println("   Terlarang       : 0,1,2,3,5,6-11,12,15 | video 25,26 | cadangan 14,21,22,34-39");
    Serial.printf(" RTD     : PT%.0f %s, RNOMINAL=%.1f ohm, RREF=%.1f ohm, filter 50 Hz, %" PRIu32 " ms/sampel\n",
                  RTD_RNOMINAL, wireModeText(), RTD_RNOMINAL, RTD_RREF, RtdSensor::kSamplePeriodMs);
    Serial.println(" ADS1232 : asumsi jumper GAIN=128, SPEED=10 SPS, A0=GND (AIN1)");
    Serial.printf("           gerbang spike %.0f sigma (median-%u) -> rata-rata adaptif %u..%u sampel\n",
                  Ads1232::kSpikeGateSigma, static_cast<unsigned>(Ads1232::kMedianSize),
                  static_cast<unsigned>(Ads1232::kAverageMin),
                  static_cast<unsigned>(Ads1232::kAverageMax));
    Serial.printf("           STABLE jika angka bergerak < %.1f mg selama %u sampel (%.1f s)\n",
                  Ads1232::kStableSpanMg, static_cast<unsigned>(Ads1232::kStableWindow),
                  samplesToSeconds(Ads1232::kStableWindow));
    Serial.println("==============================================================");
}

void printHelp() {
    logLine("Perintah (akhiri dengan Enter):");
    logLine("  t          tare (%u sampel, ~%.1f s)", static_cast<unsigned>(Ads1232::kTareSamples),
            samplesToSeconds(Ads1232::kTareSamples));
    logLine("  c <gram>   kalibrasi 1 titik dengan anak timbangan, contoh: c 200");
    logLine("  n          rekam titik NOL (timbangan kosong) -> baris CALZERO");
    logLine("  m <mg> <u|d>  rekam titik kalibrasi -> baris CALPT; u=naik, d=turun");
    logLine("  s <cpg>    set countsPerGram langsung (dipakai hasil fit dari PC)");
    logLine("  z          offset self-calibration ADS1232 (lalu tare ulang)");
    logLine("  r          reset ADS1232 (PDWN cycle)");
    logLine("  v          ganti mode report: human readable <-> CSV");
    logLine("  e          reset posisi encoder ke 0");
    logLine("  p          cetak kalibrasi tersimpan + diagnostik ADS1232");
    logLine("  x          panel diagnostik MAX31865 + fault-detection cycle BARU");
    logLine("  ?          bantuan ini");
}

void printCalibration() {
    logLine("Kalibrasi load cell (NVS):");
    logLine("  offset        = %.2f count%s", loadCell.offsetCounts(),
            loadCell.hasStoredOffset() ? "" : "  (default, belum pernah tare)");
    logLine("  countsPerGram = %.3f%s", loadCell.countsPerGram(),
            loadCell.isCalibrated() ? "" : "  (default perkiraan 1 mV/V)");
    if (!loadCell.isCalibrated()) {
        logLine("  status        = UNCALIBRATED -> tare ('t') lalu 'c <gram>' dengan anak timbangan");
    }
}

void printAdsDiagnostics() {
    logLine("Diagnostik ADS1232:");
    logLine("  DRDY pertama setelah PDWN = %" PRIu32 " ms (t11: ~%" PRIu32 " ms @10 SPS)",
            loadCell.firstDrdyMs(), Ads1232::kWakeupDataReadyMs);
    logLine("  interval DRDY terakhir    = %" PRIu32 " ms (10 SPS = %" PRIu32 " ms)",
            loadCell.lastDrdyIntervalMs(), Ads1232::kConversionPeriodMs);
    logLine("  durasi shift-out terakhir = %" PRIu32 " us (batas desain %" PRIu32 " us)",
            loadCell.lastShiftDurationUs(), Ads1232::kMaxShiftDurationUs);
    if (loadCell.lastSelfCalMs() > 0) {
        logLine("  self-cal terakhir         = %" PRIu32 " ms (t8: ~%" PRIu32 " ms @10 SPS)",
                loadCell.lastSelfCalMs(), Ads1232::kSelfCalNominalMs);
    }
    logLine("  protocol error total      = %" PRIu32, loadCell.protocolErrorCount());
    const float span = loadCell.stableSpanCounts();
    const float threshold = loadCell.stableThresholdCounts();
    if (loadCell.isCalibrated()) {
        const float mgPerCount = 1000.0f / std::fabs(loadCell.countsPerGram());
        logLine("  gerak angka (%u sampel)    = %.2f count = %.3f mg (ambang %.3f mg) -> %s",
                static_cast<unsigned>(Ads1232::kStableWindow), span, span * mgPerCount,
                threshold * mgPerCount, loadCell.isStable() ? "STABLE" : "MOVING");
    } else {
        logLine("  gerak angka (%u sampel)    = %.2f count (ambang %.2f count) -> %s",
                static_cast<unsigned>(Ads1232::kStableWindow), span, threshold,
                loadCell.isStable() ? "STABLE" : "MOVING");
    }
    logLine("  SD window moving average  = %.2f count  (info saja, bukan dasar STABLE)",
            loadCell.stdDevCounts());
    logLine("  window rata-rata adaptif  = %u sampel (%.1f s) dari maks %u",
            static_cast<unsigned>(loadCell.averageWindow()),
            samplesToSeconds(loadCell.averageWindow()),
            static_cast<unsigned>(Ads1232::kAverageMax));

    const float drift = loadCell.driftCountsPerSecond();
    if (loadCell.isCalibrated()) {
        const float mgPerCount = 1000.0f / std::fabs(loadCell.countsPerGram());
        logLine("  LAJU DRIFT                = %+.2f count/s = %+.2f mg/menit",
                drift, drift * 60.0f * mgPerCount);
        logLine("    Ini angka yang menentukan kapan boleh tare, bukan STABLE.");
        logLine("    Di bawah ~%.0f mg/menit berarti sudah setimbang termal.",
                app::kSettledDriftMgPerMin);
    } else {
        logLine("  LAJU DRIFT                = %+.2f count/s", drift);
    }

    const float rawSd = loadCell.rawStdDevCounts();
    const int32_t rawPp = loadCell.rawPeakToPeakCounts();
    if (loadCell.isCalibrated()) {
        const float mgPerCount = 1000.0f / std::fabs(loadCell.countsPerGram());
        logLine("  noise raw (%u sampel, drift dibuang) = SD %.2f count = %.3f mg, p-p %" PRId32 " count = %.3f mg",
                static_cast<unsigned>(Ads1232::kRawRingSize), rawSd, rawSd * mgPerCount, rawPp,
                static_cast<float>(rawPp) * mgPerCount);
    } else {
        logLine("  noise raw (%u sampel, drift dibuang) = SD %.2f count, p-p %" PRId32 " count  (mg: --- UNCALIBRATED)",
                static_cast<unsigned>(Ads1232::kRawRingSize), rawSd, rawPp);
    }
    logLine("  spike ditolak gerbang     = %" PRIu32 " sampel (ambang %.1f sigma)",
            loadCell.spikesRejected(), Ads1232::kSpikeGateSigma);
    if (loadCell.lastShiftDurationUs() > Ads1232::kMaxShiftDurationUs) {
        logLine("  WARNING: shift-out melebihi batas desain, cek beban CPU / interrupt");
    }
}

// =====================================================================
//  Panel diagnostik MAX31865 (perintah 'x')
//
//  Setiap bit dicetak terpisah dengan namanya, TERMASUK yang bernilai 0:
//  justru nilai 0 pada D5/D4/D3 yang sering disalahartikan sebagai "aman",
//  padahal bit itu hanya di-set oleh fault-detection cycle.
// =====================================================================
void printRtdPanel() {
    if (!rtd.spiOk()) {
        logLine("MAX31865: SPI tidak merespons, panel tidak berarti");
        return;
    }

    // Siklus BARU dijalankan lebih dulu; inilah satu-satunya cara bit
    // deteksi FORCE-/RTDIN- terputus benar-benar diperbarui.
    const uint8_t cycleFault = rtd.runFaultDetectionCycle();
    const uint8_t config = rtd.liveConfigRegister();
    const uint16_t reg = rtd.rawRegister();
    const float measured = rtd.ratio();

    logLine("Diagnostik MAX31865:");
    logLine("  register RTD (raw)  = 0x%04X   (MSB 0x%02X, LSB 0x%02X)", reg,
            static_cast<unsigned>(reg >> 8), static_cast<unsigned>(reg & 0xFF));
    logLine("  bit D0 (fault flag) = %u", static_cast<unsigned>(reg & 0x01));
    logLine("  nilai 15-bit        = %u / 32768", static_cast<unsigned>(rtd.rawCode()));
    logLine("  rasio terukur       = %.6f   <- besaran yang benar-benar diukur cip", measured);
    logLine("  resistansi          = %.2f ohm   (RREF di pins.h = %.1f)", rtd.rtdResistance(),
            RTD_RREF);
    logLine("  suhu                = %.2f C", rtd.tempC());

    // Rasio tidak bergantung pada RREF. Jadi kalau resistansi probe sudah
    // diukur multimeter, nilai RREF yang SEHARUSNYA terpasang bisa dihitung.
    if (measured > 0.0f) {
        logLine("  --- petunjuk RREF ---");
        logLine("  rasio = R_probe / R_ref_asli, tidak terpengaruh RTD_RREF.");
        logLine("  Kalau probe terukur X ohm di multimeter, maka R_ref_asli = X / %.6f:", measured);
        const float samples[] = {100.0f, 108.0f, 110.0f};
        for (float probe : samples) {
            logLine("    probe %.0f ohm  ->  R_ref_asli ~ %.1f ohm", probe, probe / measured);
        }
        logLine("  Ukur resistor referensi di board. Kalau BUKAN %.0f ohm, itu penyebabnya,",
                RTD_RREF);
        logLine("  dan RTD_RREF di pins.h harus diganti ke nilai terukur itu.");
    }

    logLine("  config register     = 0x%02X", config);
    logLine("    D7 VBIAS            = %u", static_cast<unsigned>((config >> 7) & 1));
    logLine("    D6 conversion mode  = %u (%s)", static_cast<unsigned>((config >> 6) & 1),
            ((config >> 6) & 1) ? "auto" : "off");
    logLine("    D5 1-shot           = %u", static_cast<unsigned>((config >> 5) & 1));
    logLine("    D4 3-wire           = %u (%s)", static_cast<unsigned>((config >> 4) & 1),
            ((config >> 4) & 1) ? "3-wire" : "2/4-wire");
    logLine("    D3:D2 fault detect  = %u%u", static_cast<unsigned>((config >> 3) & 1),
            static_cast<unsigned>((config >> 2) & 1));
    logLine("    D1 fault clear      = %u", static_cast<unsigned>((config >> 1) & 1));
    logLine("    D0 filter           = %u (%s Hz)", static_cast<unsigned>(config & 1),
            (config & 1) ? "50" : "60");

    logLine("  fault (siklus BARU) = 0x%02X", cycleFault);
    logLine("    D7 RTD High Threshold (RTD/kabel open)  = %u",
            static_cast<unsigned>((cycleFault >> 7) & 1));
    logLine("    D6 RTD Low Threshold (RTD short)        = %u",
            static_cast<unsigned>((cycleFault >> 6) & 1));
    logLine("    D5 REFIN- > 0.85 x VBIAS                = %u",
            static_cast<unsigned>((cycleFault >> 5) & 1));
    logLine("    D4 REFIN- < 0.85 x VBIAS (FORCE- open)  = %u",
            static_cast<unsigned>((cycleFault >> 4) & 1));
    logLine("    D3 RTDIN- < 0.85 x VBIAS (FORCE- open)  = %u",
            static_cast<unsigned>((cycleFault >> 3) & 1));
    logLine("    D2 Over/under voltage                   = %u",
            static_cast<unsigned>((cycleFault >> 2) & 1));
    logLine("  ambang fault        : low = %.2f ohm, high = %.2f ohm", rtd.thresholdLowOhm(),
            rtd.thresholdHighOhm());
    logLine("  initFault (boot)    = 0x%02X", rtd.initFault());

    if (cycleFault != 0) {
        Serial.print("# !! SIKLUS FAULT BARU TIDAK BERSIH: ");
        RtdSensor::printFault(Serial, cycleFault);
        Serial.println();
        logLine("  !! Perbaiki pengawatan dulu; nilai resistansi di atas tidak bisa dipercaya.");
    } else {
        logLine("  Siklus fault bersih: RTD, REFIN-, dan RTDIN- tersambung.");
        logLine("  Berarti error bukan kabel terputus, melainkan NILAI referensi.");
    }
}

// =====================================================================
//  Health check
// =====================================================================
void checkRtdPlausibility() {
    const float ohm = rtd.rtdResistance();
    const float tempC = rtd.tempC();
    if (ohm < app::kRtdPlausibleMinOhm || ohm > app::kRtdPlausibleMaxOhm) {
        logLine("  WARNING: RREF salah atau jumper wire-mode belum benar");
        logLine("           (R=%.2f ohm di luar %.0f..%.0f ohm; RREF=%.1f, %s)", ohm,
                app::kRtdPlausibleMinOhm, app::kRtdPlausibleMaxOhm, RTD_RREF, wireModeText());
    }
    if (tempC < app::kRtdPlausibleMinC || tempC > app::kRtdPlausibleMaxC) {
        logLine("  WARNING: RREF salah atau jumper wire-mode belum benar");
        logLine("           (T=%.2f C di luar %.0f..%.0f C)", tempC,
                app::kRtdPlausibleMinC, app::kRtdPlausibleMaxC);
    }
    // Saturasi tidak tertangkap cek di atas: nilai yang macet di plafon ADC
    // tetap terlihat wajar. Rasio cip yang dilihat, bukan resistansinya,
    // karena rasio tidak bergantung pada RTD_RREF.
    if (rtd.ratio() > app::kRtdRatioWarn) {
        logLine("  WARNING: RTD mendekati saturasi (rasio %.3f dari 1.000)", rtd.ratio());
        logLine("           suhu maks terukur %.1f C; di atas itu angka suhu TIDAK benar",
                rtd.maxMeasurableTempC());
    }
}

void reportRtdHealth(bool ok) {
    if (!rtd.spiOk()) {
        logLine("  MAX31865 : FAIL - SPI tidak merespons (config 0x%02X)", rtd.configRegister());
        logLine("             cek wiring SCK=%d MISO=%d MOSI=%d CS=%d, VIN & GND board",
                PIN_MAX_SCK, PIN_MAX_MISO, PIN_MAX_MOSI, PIN_MAX_CS);
        return;
    }

    if (rtd.initFault() != 0) {
        Serial.printf("#   MAX31865 : FAIL - fault-detection cycle 0x%02X: ", rtd.initFault());
        RtdSensor::printFault(Serial, rtd.initFault());
        Serial.println();
    } else if (!ok) {
        logLine("  MAX31865 : FAIL - belum ada sampel valid (fault 0x%02X)", rtd.fault());
    } else {
        logLine("  MAX31865 : OK (config 0x%02X, %s, continuous 50 Hz)", rtd.configRegister(),
                wireModeText());
    }

    // Resistansi mentah selalu dicetak supaya salah RREF/jumper terlihat
    logLine("  RTD awal : R=%.2f ohm (kode %u/32768), T=%.2f C, fault 0x%02X",
            rtd.rtdResistance(), static_cast<unsigned>(rtd.rawCode()), rtd.tempC(), rtd.fault());
    logLine("  RTD maks : %.1f C (plafon ADC dengan RREF %.1f ohm)", rtd.maxMeasurableTempC(),
            RTD_RREF);
    // Tanpa syarat valid(): valid() mensyaratkan fault == 0, sehingga nilai
    // yang jelas tidak wajar justru lolos tanpa peringatan.
    if (rtd.spiOk()) {
        checkRtdPlausibility();
    }
}

void reportAdsWakeupTiming() {
    if (loadCell.firstDrdyMs() < app::kAdsFastWakeupThresholdMs) {
        logLine("  WARNING: DRDY pertama %" PRIu32 " ms setelah PDWN, terlalu cepat untuk 10 SPS",
                loadCell.firstDrdyMs());
        logLine("           (t11 ~402 ms @10 SPS, ~53 ms @80 SPS). Cek jumper SPEED -> GND.");
    }
}

void reportAdsHealth() {
    switch (loadCell.health()) {
        case Ads1232::Health::Ok:
            logLine("  ADS1232  : OK (DRDY pertama %" PRIu32 " ms setelah PDWN, shift-out %" PRIu32 " us)",
                    loadCell.firstDrdyMs(), loadCell.lastShiftDurationUs());
            reportAdsWakeupTiming();
            return;

        case Ads1232::Health::NoDrdy:
            logLine("  ADS1232  : FAIL - DRDY/DOUT tidak pernah LOW dalam %" PRIu32 " ms",
                    Ads1232::kDrdyTimeoutMs);
            logLine("  !! Kemungkinan penyebab:");
            logLine("  !! 1. Wiring: DOUT->GPIO%d, SCLK->GPIO%d, PDWN->GPIO%d, GND modul = GND ESP32",
                    PIN_ADS_DOUT, PIN_ADS_SCLK, PIN_ADS_PDWN);
            logLine("  !! 2. Level logic: VIH ADS1232 = 0.7 x DVDD. Kalau DVDD modul 5 V, input");
            logLine("  !!    butuh >= 3.5 V -> SCLK/PDWN 3.3 V dari ESP32 tidak terbaca, dan DOUT");
            logLine("  !!    5 V bisa merusak GPIO ESP32. Pakai DVDD 3.3 V atau level shifter.");
            logLine("  !! 3. Modul tanpa supply (AVDD/DVDD) atau PDWN tertahan LOW di modul.");
            return;

        case Ads1232::Health::DoutStuckLow:
            logLine("  ADS1232  : FAIL - DOUT tetap LOW setelah pulsa SCLK ke-25");
            logLine("  !! SCLK tidak diterima chip (wiring GPIO%d / level logic, VIH = 0.7 x DVDD)",
                    PIN_ADS_SCLK);
            logLine("  !! atau DOUT (GPIO%d) short ke GND.", PIN_ADS_DOUT);
            return;

        case Ads1232::Health::NotStarted:
            logLine("  ADS1232  : FAIL - begin() belum dijalankan");
            return;
    }
}

void runHealthCheck() {
    logLine("Health check:");
    const bool rtdOk = rtd.begin();
    reportRtdHealth(rtdOk);

    loadCell.begin();
    reportAdsHealth();
    adsTimeoutReported = (loadCell.health() == Ads1232::Health::NoDrdy);

    encoder.begin();
    logLine("  Encoder  : siap (polling, INPUT_PULLUP)");

    printCalibration();
}

// =====================================================================
//  Hasil pengukuran kalibrasi multi-titik
//
//  Baris CALZERO/CALPT sengaja TIDAK diawali "# " supaya parser di PC
//  bisa membedakannya dari log biasa. Yang dicetak adalah RAW COUNTS,
//  bukan gram, supaya data tetap berguna kalau rumus konversi diganti.
// =====================================================================
void printMeasureResult() {
    const MeasureKind kind = pendingMeasure;
    pendingMeasure = MeasureKind::None;
    if (kind == MeasureKind::None) {
        return;
    }

    if (kind == MeasureKind::Zero) {
        Serial.printf("CALZERO,%" PRIu32 ",%.2f,%.2f,%u,%.2f,%.2f\n", millis(),
                      loadCell.measureMean(), loadCell.measureSd(),
                      static_cast<unsigned>(loadCell.measureSamples()), rtd.rtdResistance(),
                      rtd.tempC());
        return;
    }

    Serial.printf("CALPT,%" PRIu32 ",%.3f,%c,%.2f,%.2f,%u,%.2f,%.2f\n", millis(), pendingNominalMg,
                  pendingDirection, loadCell.measureMean(), loadCell.measureSd(),
                  static_cast<unsigned>(loadCell.measureSamples()), rtd.rtdResistance(),
                  rtd.tempC());
}

// =====================================================================
//  Event dari driver
// =====================================================================
void reportLoadCellEvents() {
    for (Ads1232::Event event = loadCell.takeEvent(); event != Ads1232::Event::None;
         event = loadCell.takeEvent()) {
        switch (event) {
            case Ads1232::Event::ResetDone:
                logLine("ADS1232 settling selesai (%u konversi dibuang), DRDY pertama %" PRIu32 " ms",
                        static_cast<unsigned>(Ads1232::kDiscardAfterReset), loadCell.firstDrdyMs());
                reportAdsWakeupTiming();
                break;
            case Ads1232::Event::TareDone:
                logLine("Tare selesai: offset = %.2f count (tersimpan di NVS)", loadCell.offsetCounts());
                break;
            case Ads1232::Event::CalibrationDone:
                logLine("Kalibrasi selesai: countsPerGram = %.3f (tersimpan di NVS)",
                        loadCell.countsPerGram());
                break;
            case Ads1232::Event::CalibrationFailed:
                logLine("Kalibrasi GAGAL: selisih < %" PRId32 " count dari offset. Beban terpasang? Sudah tare?",
                        Ads1232::kMinCalibrationSpanCounts);
                break;
            case Ads1232::Event::SelfCalDone:
                logLine("Self-calibration selesai dalam %" PRIu32 " ms (t8 ~%" PRIu32 " ms @10 SPS). Tare ulang ('t').",
                        loadCell.lastSelfCalMs(), Ads1232::kSelfCalNominalMs);
                break;
            case Ads1232::Event::SelfCalTimeout:
                logLine("Self-calibration TIMEOUT: DRDY tidak turun dalam %" PRIu32 " ms",
                        Ads1232::kSelfCalTimeoutMs);
                break;
            case Ads1232::Event::MeasureDone:
                printMeasureResult();
                break;
            case Ads1232::Event::JobRejected:
                logLine("ADS1232: perintah ditolak (sedang sibuk / argumen tidak valid)");
                break;
            case Ads1232::Event::JobCanceled:
                logLine("ADS1232: tare/kalibrasi dibatalkan oleh reset");
                break;
            case Ads1232::Event::ProtocolError:
                logLine("ADS1232 PROTOCOL ERROR: DOUT tetap LOW setelah pulsa SCLK terakhir (total %" PRIu32 ")",
                        loadCell.protocolErrorCount());
                break;
            case Ads1232::Event::NvsWriteFailed:
                logLine("ADS1232: GAGAL menyimpan kalibrasi ke NVS");
                break;
            case Ads1232::Event::None:
                break;
        }
    }
}

void reportRtdFaults() {
    uint8_t code = 0;
    if (!rtd.takeFaultReport(code)) {
        return;
    }
    if (code == 0) {
        logLine("RTD fault hilang");
        return;
    }
    Serial.printf("# RTD FAULT 0x%02X: ", code);
    RtdSensor::printFault(Serial, code);
    Serial.println(" -> fault di-clear");
}

void reportAdsTimeout() {
    const bool timedOut = loadCell.drdyTimedOut();
    if (timedOut && !adsTimeoutReported) {
        logLine("WARNING: ADS1232 tidak mengirim DRDY > %" PRIu32 " ms (wiring / supply / level logic?)",
                Ads1232::kDrdyTimeoutMs);
    } else if (!timedOut && adsTimeoutReported) {
        logLine("ADS1232 kembali mengirim data");
    }
    adsTimeoutReported = timedOut;
}

// =====================================================================
//  Initial zero setting — tare otomatis saat alat nyala, seperti timbangan
// =====================================================================
void serviceAutoTare() {
    if (!autoTarePending) {
        return;
    }
    if (loadCell.health() != Ads1232::Health::Ok || loadCell.drdyTimedOut()) {
        autoTarePending = false;  // tidak ada data; biarkan user tare manual
        return;
    }
    if (loadCell.isSettling() || loadCell.isBusy() || std::isnan(loadCell.filteredCounts())) {
        return;  // tunggu data valid mengalir
    }

    // Pengaman: bandingkan dengan offset tersimpan. Tanpa offset tersimpan
    // (boot pertama) tidak ada acuan, jadi tare langsung dijalankan.
    if (loadCell.hasStoredOffset()) {
        const float countsPerGram = std::fabs(loadCell.countsPerGram());
        const double deviation =
            std::fabs(static_cast<double>(loadCell.filteredCounts()) - loadCell.offsetCounts());
        if (deviation > static_cast<double>(app::kInitialZeroRangeGrams * countsPerGram)) {
            logLine("Tare otomatis DIBATALKAN: ada ~%.1f g di timbangan saat nyala (batas %.0f g)",
                    deviation / countsPerGram, app::kInitialZeroRangeGrams);
            logLine("  Kosongkan timbangan lalu jalankan 't'. Offset lama tetap dipakai.");
            autoTarePending = false;
            return;
        }
    }

    loadCell.tare();
    autoTarePending = false;
    logLine("Initial zero setting: tare otomatis saat nyala (%u sampel, ~%.1f s)",
            static_cast<unsigned>(Ads1232::kTareSamples),
            samplesToSeconds(Ads1232::kTareSamples));
    logLine("  Timbangan HARUS kosong sekarang. Kalau ada beban, ia akan ikut dinolkan.");
}

// Pemberitahuan sekali saja: drift sudah turun, inilah saat terbaik tare ulang.
// Sengaja TIDAK tare otomatis — kalau sampel sudah di timbangan, tare akan
// menolkannya.
void serviceSettledNotice() {
    if (settledReported || autoTarePending || !loadCell.isCalibrated()) {
        return;
    }
    const float drift = loadCell.driftCountsPerSecond();
    if (std::isnan(drift)) {
        return;
    }
    const float mgPerMin = std::fabs(drift) * 60.0f * 1000.0f / std::fabs(loadCell.countsPerGram());
    if (mgPerMin > app::kSettledDriftMgPerMin) {
        return;
    }
    settledReported = true;
    logLine("SUDAH SETIMBANG: drift turun ke %.2f mg/menit (ambang %.0f).", mgPerMin,
            app::kSettledDriftMgPerMin);
    logLine("  Inilah saat terbaik menjalankan 't' lagi. Tare saat baru nyala selalu");
    logLine("  meleset karena pemanasan sendiri belum selesai.");
}

void latchButtonEvent() {
    switch (encoder.buttonEvent()) {
        case EncoderInput::ButtonEvent::ShortPress:
            buttonLatch = 'S';
            break;
        case EncoderInput::ButtonEvent::LongPress:
            buttonLatch = 'L';
            break;
        case EncoderInput::ButtonEvent::None:
            break;
    }
}

// '-' idle, 'P' sedang ditekan, 'S' short press, 'L' long press
char takeButtonChar() {
    if (buttonLatch != '-') {
        const char latched = buttonLatch;
        buttonLatch = '-';
        return latched;
    }
    return encoder.isPressed() ? 'P' : '-';
}

// =====================================================================
//  Report
// =====================================================================
const char* loadCellStatusText() {
    if (loadCell.drdyTimedOut()) return "NODATA";
    if (loadCell.doutError()) return "DOUTERR";
    if (loadCell.isSettling()) return "SETTLE";
    if (loadCell.isClipped()) return "CLIP";
    if (loadCell.isBusy()) return "BUSY";
    return loadCell.isStable() ? "STABLE" : "MOVING";
}

void printHumanReport(uint32_t nowMs) {
    char rtdText[64];
    if (rtd.spiOk()) {
        snprintf(rtdText, sizeof(rtdText), "RTD %.2f C (R=%.2f, fault 0x%02X)", rtd.tempC(),
                 rtd.rtdResistance(), rtd.fault());
    } else {
        snprintf(rtdText, sizeof(rtdText), "RTD --- (SPI FAIL)");
    }

    char weightText[64];
    const float grams = loadCell.grams();
    if (std::isnan(grams)) {
        snprintf(weightText, sizeof(weightText), "W --- g %s (raw %" PRId32 ")",
                 loadCellStatusText(), loadCell.raw());
    } else {
        // 4 desimal (0,1 mg) BUKAN klaim akurasi: noise satu baris jauh lebih
        // besar. Digit ini supaya rata-rata ratusan baris di PC (siklus
        // pengeringan) tidak terkunci ke grid 1 mg.
        snprintf(weightText, sizeof(weightText), "W %.4f g %s (raw %" PRId32 ")", grams,
                 loadCellStatusText(), loadCell.raw());
    }

    Serial.printf("[%5.1fs] %s | %s%s | ENC %" PRId32 " btn:%c\n",
                  static_cast<float>(nowMs) / app::kMsPerSecond, rtdText, weightText,
                  loadCell.isCalibrated() ? "" : " UNCAL", encoder.position(), takeButtonChar());
}

void printCsvReport(uint32_t nowMs) {
    takeButtonChar();  // buang latch supaya tidak muncul basi saat kembali ke mode human
    Serial.printf("%" PRIu32 ",%.2f,%.2f,%u,%" PRId32 ",%.4f,%d,%" PRId32 "\n", nowMs,
                  rtd.tempC(), rtd.rtdResistance(), static_cast<unsigned>(rtd.fault()),
                  loadCell.raw(), loadCell.grams(), loadCell.isStable() ? 1 : 0,
                  encoder.position());
}

void serviceReport() {
    const uint32_t nowMs = millis();
    if ((nowMs - lastReportMs) < app::kReportPeriodMs) {
        return;
    }
    lastReportMs += app::kReportPeriodMs;
    if ((nowMs - lastReportMs) >= app::kReportPeriodMs) {
        lastReportMs = nowMs;  // tertinggal jauh (mis. output panjang): sinkron ulang
    }

    if (reportMode == ReportMode::Human) {
        printHumanReport(nowMs);
    } else {
        printCsvReport(nowMs);
    }
}

// =====================================================================
//  Serial command (non-blocking, per karakter)
// =====================================================================
char* skipBlanks(char* text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    return text;
}

void commandTare() {
    if (loadCell.isBusy()) {
        logLine("Tare ditolak: ADS1232 sedang sibuk");
        return;
    }
    if (!loadCell.isStable()) {
        logLine("Peringatan: berat belum STABLE, hasil tare bisa meleset");
    }
    loadCell.tare();
    if (loadCell.isBusy()) {
        logLine("Tare dimulai (%u sampel, ~%.1f s)...", static_cast<unsigned>(Ads1232::kTareSamples),
                samplesToSeconds(Ads1232::kTareSamples));
    }
}

void commandCalibrate(char* args) {
    char* end = nullptr;
    const float knownGrams = strtof(args, &end);
    const bool parsed = (end != args) && (*skipBlanks(end) == '\0');
    if (!parsed || !std::isfinite(knownGrams) || knownGrams <= 0.0f) {
        logLine("Format: c <gram>  (angka > 0), contoh: c 200");
        return;
    }
    if (loadCell.isBusy()) {
        logLine("Kalibrasi ditolak: ADS1232 sedang sibuk");
        return;
    }
    if (!loadCell.hasStoredOffset()) {
        logLine("Peringatan: belum pernah tare, offset masih default. Tare dulu dengan timbangan kosong.");
    }
    if (!loadCell.isStable()) {
        logLine("Peringatan: berat belum STABLE, hasil kalibrasi bisa meleset");
    }
    loadCell.calibrateWith(knownGrams);
    if (loadCell.isBusy()) {
        logLine("Kalibrasi dengan %.3f g dimulai (%u sampel, ~%.1f s)...", knownGrams,
                static_cast<unsigned>(Ads1232::kTareSamples), samplesToSeconds(Ads1232::kTareSamples));
    }
}

void commandMeasureZero() {
    if (loadCell.isBusy()) {
        logLine("Rekam nol ditolak: ADS1232 sedang sibuk");
        return;
    }
    pendingMeasure = MeasureKind::Zero;
    loadCell.measure();
    if (!loadCell.isBusy()) {
        pendingMeasure = MeasureKind::None;
        return;
    }
    logLine("Rekam titik NOL (%u sampel, ~%.1f s)...", static_cast<unsigned>(Ads1232::kTareSamples),
            samplesToSeconds(Ads1232::kTareSamples));
}

// m <mg> <u|d>
void commandMeasurePoint(char* args) {
    char* end = nullptr;
    const float nominalMg = strtof(args, &end);
    if (end == args || !std::isfinite(nominalMg) || nominalMg <= 0.0f) {
        logLine("Format: m <mg> <u|d>  contoh: m 10000 u  (10 g, arah naik)");
        return;
    }
    char* rest = skipBlanks(end);
    const char direction = static_cast<char>(toupper(static_cast<unsigned char>(*rest)));
    if ((direction != 'U' && direction != 'D') || *skipBlanks(rest + 1) != '\0') {
        logLine("Format: m <mg> <u|d>  u = naik (loading), d = turun (unloading)");
        return;
    }
    if (loadCell.isBusy()) {
        logLine("Rekam titik ditolak: ADS1232 sedang sibuk");
        return;
    }

    pendingNominalMg = nominalMg;
    pendingDirection = direction;
    pendingMeasure = MeasureKind::Point;
    loadCell.measure();
    if (!loadCell.isBusy()) {
        pendingMeasure = MeasureKind::None;
        return;
    }
    logLine("Rekam titik %.3f mg arah %c (%u sampel, ~%.1f s)...", nominalMg, direction,
            static_cast<unsigned>(Ads1232::kTareSamples), samplesToSeconds(Ads1232::kTareSamples));
}

// s <countsPerGram> — menerapkan hasil fit yang dihitung di PC
void commandSetScale(char* args) {
    char* end = nullptr;
    const float countsPerGram = strtof(args, &end);
    if (end == args || *skipBlanks(end) != '\0' || !std::isfinite(countsPerGram) ||
        std::fabs(countsPerGram) < Ads1232::kMinAbsCountsPerGram) {
        logLine("Format: s <countsPerGram>  contoh: s 5499.03");
        return;
    }
    loadCell.setScale(countsPerGram);
    logLine("countsPerGram di-set ke %.3f (tersimpan di NVS)", loadCell.countsPerGram());
}

void commandSelfCalibrate() {
    if (loadCell.selfCalibrate()) {
        logLine("Offset self-calibration dimulai pada DRDY berikutnya (~%" PRIu32 " ms)...",
                Ads1232::kSelfCalNominalMs);
    } else {
        logLine("Self-calibration ditolak: ADS1232 sedang sibuk / reset");
    }
}

void commandReset() {
    loadCell.reset();
    logLine("ADS1232 reset (PDWN cycle), menunggu %u konversi settling...",
            static_cast<unsigned>(Ads1232::kDiscardAfterReset));
}

void commandToggleMode() {
    if (reportMode == ReportMode::Human) {
        logLine("Mode report: CSV");
        reportMode = ReportMode::Csv;
        Serial.println(kCsvHeader);
    } else {
        reportMode = ReportMode::Human;
        logLine("Mode report: human readable");
    }
}

void handleCommand(char* line) {
    line = skipBlanks(line);
    if (*line == '\0') {
        return;
    }
    const char command = static_cast<char>(tolower(static_cast<unsigned char>(line[0])));
    char* args = skipBlanks(line + 1);

    if (command == 'c') {
        commandCalibrate(args);
        return;
    }
    if (command == 'm') {
        commandMeasurePoint(args);
        return;
    }
    if (command == 's') {
        commandSetScale(args);
        return;
    }
    if (*args != '\0') {
        logLine("Perintah tidak dikenal: '%s' (ketik ? untuk bantuan)", line);
        return;
    }

    switch (command) {
        case 't':
            commandTare();
            break;
        case 'n':
            commandMeasureZero();
            break;
        case 'z':
            commandSelfCalibrate();
            break;
        case 'r':
            commandReset();
            break;
        case 'v':
            commandToggleMode();
            break;
        case 'e':
            encoder.resetPosition();
            logLine("Posisi encoder direset ke 0");
            break;
        case 'p':
            printCalibration();
            printAdsDiagnostics();
            break;
        case 'x':
            printRtdPanel();
            break;
        case '?':
            printHelp();
            break;
        default:
            logLine("Perintah tidak dikenal: '%s' (ketik ? untuk bantuan)", line);
            break;
    }
}

void pollSerialCommands() {
    while (Serial.available() > 0) {
        const int incoming = Serial.read();
        if (incoming < 0) {
            return;
        }
        const char c = static_cast<char>(incoming);

        if (c == '\n' || c == '\r') {
            if (commandOverflow) {
                logLine("Perintah terlalu panjang (maks %u karakter), diabaikan",
                        static_cast<unsigned>(app::kCommandBufferSize - 1));
            } else if (commandLength > 0) {
                commandBuffer[commandLength] = '\0';
                handleCommand(commandBuffer);
            }
            commandLength = 0;
            commandOverflow = false;
        } else if (commandLength < app::kCommandBufferSize - 1) {
            commandBuffer[commandLength++] = c;
        } else {
            commandOverflow = true;
        }
    }
}

}  // namespace

// =====================================================================
//  setup / loop
// =====================================================================
void setup() {
    Serial.setTxBufferSize(app::kSerialTxBufferSize);  // wajib sebelum begin()
    Serial.begin(app::kSerialBaud);

    printBanner();
    runHealthCheck();
    printHelp();

    lastReportMs = millis();
}

void loop() {
    rtd.update();
    loadCell.update();
    encoder.update();
    latchButtonEvent();

    pollSerialCommands();

    serviceAutoTare();
    serviceSettledNotice();
    reportLoadCellEvents();
    reportRtdFaults();
    reportAdsTimeout();

    // Plausibility check berkala: nilai di luar rentang wajar bisa muncul
    // belakangan, bukan hanya saat boot.
    const uint32_t nowMs = millis();
    if (rtd.spiOk() && (nowMs - lastRtdCheckMs) >= app::kRtdPlausibleCheckMs) {
        lastRtdCheckMs = nowMs;
        checkRtdPlausibility();
    }

    serviceReport();
}
