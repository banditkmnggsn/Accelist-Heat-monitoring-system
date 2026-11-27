#pragma once

// =====================================================================
//  SensorService.h — task sensor di core 0 (lihat SensorService.cpp)
// =====================================================================

#include <UiModel.h>

#include <atomic>
#include <cstdint>

class InputTask;
class NetClock;
class TestLog;

namespace sensor_service {

struct Status {
    hbui::CheckState scaleCheck;  // hasil cek boot / cek ulang
    hbui::CheckState tempCheck;
    bool scaleOk;  // ADS1232 mengirim data tanpa galat
    bool tempOk;   // MAX31865 sah dan wajar
    float tempC;   // NAN kalau tidak sah
};

// Mulai task sensor di core 0. renders = penghitung frame UI (untuk fps).
void start(InputTask& input, NetClock& net, TestLog& log, const std::atomic<uint32_t>& renders);

Status status();

// Ulangi cek sensor (layar error SENSOR, TEKAN = cek ulang)
void requestRecheck();

// ---- OPSI PENGEMBANG (UI, core 1) ----
// Detail timbangan & PT100 (diperbarui tiap ~50 ms)
void detail(hbui::ScaleInfo& scale, hbui::RtdInfo& rtd);
// Aksi timbangan: hasil di ScaleInfo.jobSeq/jobResult. Ditolak saat siklus berjalan.
void requestScaleJob(hbui::ScaleJob job, float grams);
// Batal kalibrasi: faktor countsPerGram lama ditulis lagi ke NVS
void requestScaleRestore(float countsPerGram);

}  // namespace sensor_service
