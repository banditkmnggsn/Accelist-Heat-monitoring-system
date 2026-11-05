// =====================================================================
//  hb_test_support.h
//  Helper bersama untuk semua suite test. Header-only supaya tidak ada
//  urusan linking antar suite.
//
//  Ditaruh di test/support/ dan di-include lewat  -I test/support
// =====================================================================

#pragma once

#include <stdint.h>
#include <math.h>

extern "C++" {
#include "Heatbox.h"
}

namespace hbtest {

// ---------------------------------------------------------------------
//  Noise deterministik.
//
//  JANGAN pakai rand(). Test yang memakai angka acak suatu hari akan gagal
//  dan kamu tidak bisa mereproduksinya. Rumus berbasis indeks memberi
//  sebaran yang cukup mirip noise, tapi selalu sama setiap kali dijalankan.
// ---------------------------------------------------------------------
inline int32_t noise(int i, int32_t amp) {
    if (amp <= 0) return 0;
    return (int32_t)(((i * 37 + 11) % (2 * amp + 1)) - amp);
}

// ---------------------------------------------------------------------
//  Bangkitkan kurva pengeringan sintetik:
//      W(t) = Winf + A * exp(-t / tau) + k * t   (+ noise)
//
//  Karena parameternya kita yang tentukan, kita TAHU jawaban benarnya.
//  Fitting yang baik harus bisa menemukannya kembali.
// ---------------------------------------------------------------------
inline void makeDryingCurve(int32_t *out, int n,
                            float Winf, float A, float tau, float k,
                            int32_t noiseAmp = 0) {
    for (int i = 0; i < n; i++) {
        const float t = (float)i;
        const float w = Winf + A * expf(-t / tau) + k * t;
        out[i] = (int32_t)(w + 0.5f) + noise(i, noiseAmp);
    }
}

// ---------------------------------------------------------------------
//  Garis lurus: y[i] = start + i * step
// ---------------------------------------------------------------------
inline void makeRamp(int32_t *out, int n, int32_t start, int32_t step) {
    for (int i = 0; i < n; i++) out[i] = start + (int32_t)i * step;
}

// ---------------------------------------------------------------------
//  Umpankan satu nilai konstan selama `seconds` detik penuh, lalu
//  jalankan hbTick1Hz() tiap detiknya.
// ---------------------------------------------------------------------
inline void feedConstant(int32_t counts, int seconds) {
    for (int s = 0; s < seconds; s++) {
        for (int k = 0; k < ADC_SPS; k++) hbPushRaw(counts);
        hbTick1Hz();
    }
}

// ---------------------------------------------------------------------
//  Umpankan kemiringan tetap (count per detik) selama `seconds` detik.
//  Berhenti lebih awal kalau fase berubah dari HB_DRYING.
//  Return: berapa detik yang benar-benar dijalankan.
// ---------------------------------------------------------------------
inline int feedSlope(int32_t start, float countsPerSec, int seconds,
                     bool stopWhenPhaseChanges = true) {
    for (int s = 0; s < seconds; s++) {
        const int32_t v = start + (int32_t)(countsPerSec * (float)s);
        for (int k = 0; k < ADC_SPS; k++) hbPushRaw(v);
        hbTick1Hz();
        if (stopWhenPhaseChanges && hbStatus()->phase != HB_DRYING) return s + 1;
    }
    return seconds;
}

// ---------------------------------------------------------------------
//  Konversi bantu, biar angka di test bisa ditulis dalam mg.
// ---------------------------------------------------------------------
inline int32_t mgToCounts(float mg)     { return (int32_t)(mg * COUNTS_PER_MG); }
inline float   countsToMg(int32_t c)    { return (float)c / COUNTS_PER_MG; }

}  // namespace hbtest
