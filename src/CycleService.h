#pragma once

// =====================================================================
//  CycleService.h — siklus pengeringan di task sensor (core 0)
//
//  - step() dipanggil task sensor tiap iterasi (~1 kHz) dengan sampel
//    berat baru dan keadaan ruang. Di sini juga pin lampu ditulis:
//    relay K1 (GPIO21) dan SSR (GPIO22), urutan aman dari HeaterSequencer.
//  - Build HB_SIMULASI=1: sensor & lampu ditiru SimModel (waktu dipercepat
//    HB_SIM_SPEED kali). Pin lampu TIDAK PERNAH ditulis HIGH. Pintu tetap
//    dari limit switch asli.
//  - UI (core 1) hanya memanggil fungsi request*() dan copyView().
// =====================================================================

#include <AdvancedSettings.h>
#include <CycleController.h>
#include <CycleSettings.h>
#include <UiModel.h>

class NetClock;
class TestLog;

namespace cycle_service {

// Dari setup(): pin lampu dipaksa mati, logger sudah begin(). advanced =
// parameter metode + PI tersimpan (NVS "hbcfg"/"adv").
void begin(TestLog& log, NetClock& net, const hbcycle::AdvancedSettings& advanced);

// ---- task sensor ----
// sample = nullptr kalau tidak ada sampel baru. zeroRaw/cpg = kalibrasi
// timbangan saat ini (dipakai saat MULAI).
void step(uint32_t nowMs, const hbcycle::MassSample* sample, const hbcycle::ChamberReading& chamber,
          bool scaleHealthy, double zeroRaw, float countsPerGram);
bool running();  // siklus sedang memanaskan/menimbang (blokir tare/kalibrasi & OTA)
void forceHeaterOff();  // OTA mulai menulis firmware (task sensor)
// Perintah serial 'y' (status / tuning PI). args tanpa huruf perintah.
void command(char* args);
void printHelp();

// Simulasi: status sensor untuk UI (boot check, beranda)
bool simulation();
float simTempC();

// ---- UI ----
// Parameter baru dari OPSI PENGEMBANG; diterapkan saat tidak ada tes berjalan
void setAdvanced(const hbcycle::AdvancedSettings& advanced);
void requestStart(const hbcycle::Settings& settings);
void requestTare();
void requestCancel();
void requestAcknowledge();
void copyView(hbcycle::CycleView& out);
bool startPending();

}  // namespace cycle_service
