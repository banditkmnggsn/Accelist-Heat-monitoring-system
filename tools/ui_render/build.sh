#!/usr/bin/env bash
# Bangun & jalankan perender PNG layar (lihat README.md di folder ini).
#   GFX=/path/ke/Adafruit-GFX-Library (tag 1.13.0) tools/ui_render/build.sh [folder_keluaran]
set -eu
GFX="${GFX:?set GFX ke folder Adafruit-GFX-Library}"
OUT="${1:-.pio/ui_render}"
mkdir -p "$OUT"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
g++ -std=gnu++17 -O1 -Wall -Wextra -DARDUINO=100 \
    -I "$HERE/shim" -I "$GFX" -I "$ROOT/include" \
    -I "$ROOT/lib/HeatboxUi/src" -I "$ROOT/lib/HeatboxCycle/src" -I "$ROOT/lib/HeatboxInput/src" \
    "$HERE/ui_render.cpp" "$ROOT"/lib/HeatboxUi/src/*.cpp "$ROOT/lib/HeatboxCycle/src/CycleSettings.cpp" \
    "$GFX/Adafruit_GFX.cpp" -o "$OUT/ui_render" 2> "$OUT/build.log" || { cat "$OUT/build.log"; exit 1; }
grep -v "Adafruit_GFX.cpp\|glcdfont\|Adafruit-GFX" "$OUT/build.log" | grep -i "warning" || true
"$OUT/ui_render" "$OUT"
