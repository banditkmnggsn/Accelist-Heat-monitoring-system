#pragma once

// =====================================================================
//  CycleController.h — siklus pengeringan di firmware
//  (RANCANGAN-SIKLUS-PENGERINGAN.md §3 alur, §4.2 parameter, §5 olah
//  data, §7 status)
//
//  PRE-HEAT -> TARE -> MUAT SAMPEL -> PULIH + W0 -> HEAT (Fase A/B) ->
//  STOP -> HASIL, atau ERROR dari fase mana pun.
//
//  C++ murni: tidak tahu pin, LCD, atau LittleFS. Masukan lewat step()
//  (sampel berat 10 SPS + keadaan ruang), keluaran lewat relayOn()/ssrOn()
//  (lampu), view() (LCD), dan CycleSink (logger). Waktu = nowMs dari
//  SensorInput, jadi simulasi boleh berjalan lebih cepat dari waktu nyata.
// =====================================================================

#include <cstdint>

#include "CycleSettings.h"
#include "CycleTypes.h"
#include "DryingMath.h"
#include "HeaterControl.h"
#include "MinuteBuilder.h"
#include "SecondAggregator.h"
#include "SensorInput.h"

namespace hbcycle {

// Parameter metode (RANCANGAN §4.2). Bukan menu operator.
struct CycleConfig {
    float bandC = 2.0f;                // pita "suhu tercapai" +-
    uint32_t heatBandHoldSec = 60;     // Fase A: suhu heat sudah di pita >= ini
    float phaseARateMgMin = 4.0f;      // ambang Fase A
    uint8_t phaseAConfirm = 3;         // evaluasi menit berturut-turut
    float targetRateMgMin = 1.0f;      // target laju prediksi
    float stopExtraMin = 2.0f;         // stop = t* + ini
    float shrinkMinRateMgMin = 6.0f;   // "penyusutan nyata" (1,5 x ambang)
    uint8_t minFitPoints = 5;          // V1

    float tareDriftMaxMgMin = 0.5f;    // stabil untuk tare
    uint32_t tareStableSec = 60;       // tare otomatis setelah stabil selama ini
    uint32_t tareTimeoutSec = 600;     // ERROR (PREHEAT)
    uint32_t tareWarnSec = 480;        // banner "TARE BELUM STABIL"
    uint32_t preheatExtraSec = 300;    // pre-heat + ini belum di pita -> ERROR (HEATER)

    uint32_t w0BandSec = 30;           // suhu pulih dalam pita >= ini
    float w0CalmMaxMg = 5.0f;          // sebaran robust 30 dtk terakhir
    uint32_t w0TimeoutSec = 300;       // ERROR (SAMPEL)
    float sampleMinMg = 4000.0f;
    float sampleMaxMg = 12000.0f;
    float sampleIdealMinMg = 5000.0f;
    float sampleIdealMaxMg = 10000.0f;
    float loadDetectMg = 1000.0f;      // ada beban di cawan

    float overTempMarginC = 15.0f;     // suhu > setpoint + ini -> ERROR (HEATER)
    float overTempAbsC = 165.0f;
    uint32_t tempFaultMs = 3000;       // sensor suhu tidak sah selama ini -> ERROR (SENSOR)
    uint32_t scaleFaultMs = 5000;      // timbangan tidak sehat selama ini
    uint32_t invalidSecondsFault = 10; // detik berat tidak sah berturut-turut (pintu tertutup)

    uint32_t windowMs = 2000;          // jendela SSR time-proportional
    uint32_t stoppingShowSec = 3;      // layar STOP
    uint32_t coolingLogSec = 180;      // log lanjut setelah stop
    float tauMinMin = 0.3f;
    float tauMaxMin = 30.0f;
};

struct StartParams {
    Settings settings;
    uint32_t testId;
    uint32_t startEpoch;      // 0 = jam belum sinkron
    double zeroRaw;           // offset timbangan saat ini (nol sementara sebelum tare)
    float countsPerGram;      // kalibrasi timbangan (boleh negatif)
    const char* firmware;
};

// Satu detik untuk log per detik (RANCANGAN §10)
struct SecondLog {
    uint32_t index;   // detik sejak MULAI
    double rawMean;   // NAN kalau tidak ada sampel baik
    bool valid;
    float tempC;
    float dutyPct;
    Phase phase;
    bool lidOpen;
};

class CycleSink {
public:
    virtual void onStarted(const CycleResult& header) = 0;
    virtual void onSecond(const SecondLog& second) = 0;
    virtual void onFinished(const CycleResult& result) = 0;  // hasil final (sekali)
    virtual void onLogClosed(uint32_t testId) = 0;           // log pendinginan selesai

protected:
    ~CycleSink() = default;
};

class CycleController {
public:
    explicit CycleController(const CycleConfig& config = CycleConfig());

    void setSink(CycleSink* sink) { sink_ = sink; }
    void setPiParams(const PiParams& params) { pi_.setParams(params); }
    const PiParams& piParams() const { return pi_.params(); }
    const CycleConfig& config() const { return config_; }
    // Parameter metode baru (OPSI PENGEMBANG). false = ditolak karena siklus
    // sedang berjalan; tes yang berjalan selalu memakai parameter saat MULAI.
    bool setConfig(const CycleConfig& config);

    // ---- perintah operator ----
    // false kalau siklus sedang berjalan atau pengaturan tidak sah
    bool start(const StartParams& params, uint32_t nowMs);
    void requestTare() { tareRequested_ = true; }  // TEKAN di layar TARE
    void cancel();                                  // TAHAN 3 dtk: ERROR (BATAL)
    void acknowledge();                             // Done/Failed -> Idle

    // Dipanggil sesering mungkin (task sensor, ~1 kHz). sample = nullptr
    // kalau tidak ada sampel baru. scaleHealthy = ADS1232 tanpa galat.
    void step(uint32_t nowMs, const MassSample* sample, const ChamberReading& chamber, bool scaleHealthy);

    // ---- keluaran ----
    bool relayOn() const { return sequencer_.relay(); }
    bool ssrOn() const { return sequencer_.ssr(); }
    bool running() const;  // Preheat..Stopping
    const CycleView& view() const { return view_; }

private:
    static constexpr uint16_t kRing = 192;  // detik terakhir yang disimpan

    struct RingSecond {
        uint32_t index;
        double raw;
        bool valid;
    };
    struct MinuteInfo {
        double tMid;  // menit sejak t = 0
        double massMg;
        double uncertaintyMg;
        bool valid;
        bool hasRate;
        double rateT;
        double rate;  // mg/menit, positif = menyusut
    };

    void onSecond(const SecondRecord& second, const ChamberReading& chamber);
    void controlHeater(uint32_t nowMs, const ChamberReading& chamber);
    bool heatingAllowed(const ChamberReading& chamber) const;
    void checkSafety(uint32_t nowMs, const ChamberReading& chamber, bool scaleHealthy);

    void secondPreheat(const ChamberReading& chamber, bool inBand);
    void secondTare(const ChamberReading& chamber);
    void secondLoad(const ChamberReading& chamber);
    void secondRecover(const ChamberReading& chamber, bool inBand);
    void secondHeat(const SecondRecord& second, const ChamberReading& chamber, bool inBand);
    void onMinute(const MinuteRecord& minute);
    void evaluatePhaseA(uint32_t k);
    void runFit(uint32_t k);

    void enter(Phase phase);
    void startHeat(uint32_t t0Index);
    void finish();                 // stop normal (t_stop / waktu maksimum)
    void fail(Reason reason);      // ERROR tanpa kadar air
    void closeResult();            // CRC + sink
    void stopHeater();

    double massMg(double raw) const;
    // Rata-rata massa n detik sah terakhir; NAN kalau tidak ada
    double recentMeanMg(uint8_t seconds) const;
    // Garis robust lewat `seconds` detik terakhir (massa mg), dibaca di detik terakhir
    drying::Level recentLevel(uint8_t seconds, bool useRaw) const;
    float driftMgPerMin() const;
    double fitWindowStart() const;

    CycleConfig config_;
    CycleSink* sink_ = nullptr;
    TempController pi_;
    HeaterSequencer sequencer_;
    SecondAggregator seconds_;
    MinuteBuilder minutes_;

    CycleView view_{};
    StartParams params_{};
    double zeroRaw_ = 0.0;
    float cpg_ = 1.0f;

    // waktu
    uint32_t lastNowMs_ = 0;
    uint32_t secondIndex_ = 0;   // detik terakhir yang diproses
    uint32_t t0Index_ = 0;       // detik pertama fase HEAT
    uint32_t windowStartMs_ = 0;
    float windowDuty_ = 0.0f;
    float lastTempC_ = 0.0f;
    uint32_t tempBadSinceMs_ = 0;
    bool tempBad_ = false;
    uint32_t scaleBadSinceMs_ = 0;
    bool scaleBad_ = false;
    uint32_t invalidSecondsRun_ = 0;

    bool tareRequested_ = false;
    int32_t closedSinceSec_ = -1;  // MUAT/PULIH: detik pintu ditutup
    uint32_t phaseStartSec_ = 0;

    // HEAT
    MinuteInfo mins_[kMaxMinutes] = {};
    uint32_t minuteCount_ = 0;
    uint8_t belowCount_ = 0;
    drying::ExpFit fit_{};
    bool fitOk_ = false;
    double tempSum_ = 0.0;
    uint32_t tempCount_ = 0;
    double dutySum_ = 0.0;
    uint32_t dutyCount_ = 0;
    double minuteTempSum_ = 0.0;
    uint32_t minuteTempCount_ = 0;
    float stopAtSec_ = -1.0f;  // detik HEAT saat stop (OK)

    // log
    bool logging_ = false;
    uint32_t stopSecondIndex_ = 0;
    bool finishedSent_ = false;

    RingSecond ring_[kRing] = {};
    uint16_t ringHead_ = 0;   // posisi tulis berikutnya
    uint16_t ringCount_ = 0;
};

}  // namespace hbcycle
