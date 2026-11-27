#pragma once

// =====================================================================
//  AdvancedSettings.h — pengaturan tim IT (PENGATURAN > OPSI PENGEMBANG)
//
//  Tiga kelompok, disimpan sebagai SATU blob ber-CRC di NVS "hbcfg"
//  kunci "adv" (src/SettingsStore), terpisah dari pengaturan operator:
//    - MethodParams : parameter metode ukur (03-40.6), masuk CycleConfig
//    - HeaterParams : kontrol PI lampu (uji U3 di alat asli)
//    - DisplayParams: filter angka berat di layar saja (03-40.7)
//  Angka pecahan disimpan sebagai bilangan bulat x10 / x1000 supaya blob
//  pasti sama di semua platform. C++ murni: diuji di PC (test_advanced).
// =====================================================================

#include <cstddef>
#include <cstdint>

#include "CycleController.h"
#include "HeaterControl.h"

namespace hbcycle {

struct MethodParams {
    uint8_t phaseARate10;     // ambang Fase A, 0,1 mg/menit (40 = 4,0)
    uint8_t phaseAConfirm;    // menit berturut-turut di bawah ambang
    uint8_t targetRate10;     // target laju prediksi, 0,1 mg/menit (10 = 1,0)
    uint8_t stopExtraMin;     // stop = t* + ini (menit)
    uint8_t tareStableSec;    // tare otomatis setelah stabil selama ini
    uint8_t overTempMarginC;  // darurat suhu: setpoint + ini
};

struct HeaterParams {
    uint16_t kp1000;     // Kp x1000 (50 = 0,050 duty per C)
    uint16_t tiSec;      // waktu integral, detik
    uint8_t bandC;       // integral aktif hanya dalam +- pita ini
    uint8_t maxDutyPct;  // batas daya lampu, %
};

struct DisplayParams {
    bool quiet;          // TAMPILAN TENANG: angka berat tidak bergetar
    uint8_t deadbandMg;  // angka baru berganti kalau berubah >= ini
};

struct AdvancedSettings {
    MethodParams method;
    HeaterParams heater;
    DisplayParams display;
};

// Batas menu (bilangan bulat yang disimpan)
struct Range16 {
    uint16_t min;
    uint16_t max;
};
constexpr Range16 kPhaseARate10Range = {10, 100};   // 1,0..10,0 mg/mnt
constexpr Range16 kPhaseAConfirmRange = {1, 10};
constexpr Range16 kTargetRate10Range = {2, 50};     // 0,2..5,0 mg/mnt
constexpr Range16 kStopExtraRange = {0, 10};
constexpr Range16 kTareStableRange = {20, 180};
constexpr Range16 kOverTempRange = {5, 30};
constexpr Range16 kKp1000Range = {5, 500};          // 0,005..0,500
constexpr Range16 kTiRange = {10, 900};
constexpr Range16 kBandRange = {1, 30};
constexpr Range16 kMaxDutyRange = {20, 100};
constexpr Range16 kDeadbandRange = {1, 20};

enum class AdvancedError : uint8_t {
    None,
    OutOfRange,
    TargetNotBelowPhaseA,  // target laju harus < ambang Fase A
};

// Bawaan = nilai RANCANGAN / app_config sebelum menu ini ada.
AdvancedSettings defaultAdvanced();

AdvancedError validateMethod(const MethodParams& m);
AdvancedError validateHeater(const HeaterParams& h);
AdvancedError validateDisplay(const DisplayParams& d);
AdvancedError validateAdvanced(const AdvancedSettings& a);

// Parameter metode -> CycleConfig (nilai lain di `base` dibiarkan)
CycleConfig applyMethod(const MethodParams& m, CycleConfig base = CycleConfig());
PiParams toPiParams(const HeaterParams& h);

// ---- blob NVS: magic(2) versi(1) isi(15) cadangan(1) crc32(4) ----
constexpr size_t kAdvancedBlobSize = 22;
void encodeAdvanced(const AdvancedSettings& a, uint8_t (&out)[kAdvancedBlobSize]);
// false kalau ukuran/magic/versi/CRC salah atau isi tidak sah; out tidak ditulis
bool decodeAdvanced(const uint8_t* data, size_t length, AdvancedSettings& out);

// ---------------------------------------------------------------------
//  Filter tampilan berat (TAMPILAN TENANG). Hanya angka di layar: data
//  siklus, log, dan hasil memakai nilai asli.
// ---------------------------------------------------------------------
class QuietValue {
public:
    // Kembalikan nilai yang ditampilkan. NAN masuk = NAN keluar (reset).
    float update(float value, const DisplayParams& d);
    void reset();

private:
    float shown_ = 0.0f;
    bool has_ = false;
};

}  // namespace hbcycle
