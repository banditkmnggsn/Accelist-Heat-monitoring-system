#pragma once

#include <Arduino.h>

// =====================================================================
//  Konfigurasi Heatbox
// =====================================================================

#define COUNTS_PER_MG        21.0f

#define ADC_SPS              10
#define MEDIAN_N             5
#define ANALYSIS_MODE        0
#define BLOCK_S              60
#define REG_WINDOW_S         120
#define REG_WINDOW_BLK       5
#define SLOPE_STOP_MGMIN     2.0f
#define SLOPE_HOLD_S         60
#define MIN_DRY_S            180
#define MAX_DRY_S            1800
#define USE_CURVE_FIT        0
#define FIT_EVERY_S          10
#define FIT_MIN_S            180
#define TAU_MIN_S            30.0f
#define TAU_MAX_S            1800.0f
#define GOLDEN_ITER          25
#define RESID_STOP_MG        10.0f
#define SEC_BUF_MAX          2048
#define BLK_BUF_MAX          (SEC_BUF_MAX / BLOCK_S + 2)
#define STABLE_N             16
#define STABLE_SD_MG         3.0f
