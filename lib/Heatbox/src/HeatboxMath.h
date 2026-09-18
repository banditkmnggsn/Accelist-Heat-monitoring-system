#pragma once

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "HeatboxConfig.h"
#include "HeatboxTypes.h"

namespace heatbox {
namespace detail {

int32_t median(const int32_t *src, uint8_t n);
float slopeCountsPerSec(const int32_t *y, int n, float dt);
bool solve3x3(const float M[3][3], const float r[3], float x[3]);

#if USE_CURVE_FIT
float sseForP(const int32_t *y, int n, float p, float abc[3]);
HbFit fitCurve(const int32_t *y, int n, float dt);
#endif

}  // namespace detail
}  // namespace heatbox
