#pragma once
// Shim minimal supaya Adafruit GFX + lib/HeatboxUi bisa dikompilasi di PC.
// HANYA untuk tools/ui_render; tidak dipakai firmware.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Print.h"

typedef bool boolean;
#ifndef PROGMEM
#define PROGMEM
#endif
using std::max;
using std::min;
static inline float radians(float deg) { return deg * 0.017453292519943295f; }
