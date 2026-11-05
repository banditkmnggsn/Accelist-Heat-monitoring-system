#pragma once

// =====================================================================
//  SettingsStore.h — pengaturan operator di NVS namespace "hbcfg"
//  (terpisah dari kalibrasi load cell di "heatbox"). Satu blob ber-CRC;
//  isi rusak / tidak sah dikembalikan ke bawaan lalu ditulis ulang.
// =====================================================================

#include <CycleSettings.h>

class SettingsStore {
public:
    // repaired = true kalau NVS kosong/rusak dan bawaan baru ditulis.
    hbcycle::Settings load(bool& repaired);
    bool save(const hbcycle::Settings& settings);
};
