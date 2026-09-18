#pragma once

#include "HeatboxTypes.h"

void hbInit();
void hbPushRaw(int32_t counts);
void hbTare();
void hbStartDry();
void hbTick1Hz();
bool hbStableWeight(float *out_mg);
float hbMoisturePct(float w_start_mg, float w_end_mg);
const HbStatus* hbStatus();
