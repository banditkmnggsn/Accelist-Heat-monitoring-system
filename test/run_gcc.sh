#!/usr/bin/env bash
# =====================================================================
#  Jalankan suite uji C++ murni di PC memakai g++ (tanpa PlatformIO).
#  Dari root proyek:   UNITY=/path/ke/Unity/src test/run_gcc.sh
#  Bawaan UNITY = .pio/libdeps/native/Unity/src (setelah `pio pkg install -e native`).
#  Hasil build di .pio/gcc. Exit code 1 kalau ada yang gagal.
# =====================================================================
set -u
UNITY="${UNITY:-.pio/libdeps/native/Unity/src}"
if [ ! -f "$UNITY/unity.c" ]; then
    echo "Unity tidak ditemukan di $UNITY (set variabel UNITY)"; exit 1
fi
OUT=.pio/gcc
mkdir -p "$OUT"
LIBS="lib/HeatboxCycle/src lib/HeatboxInput/src"
INC=""; SRC=""
for d in $LIBS; do INC="$INC -I $d"; SRC="$SRC $d/*.cpp"; done
FAILED=0
for S in test_drying_math test_cycle_aggregation test_settings test_input test_cycle test_advanced; do
    echo "=== $S"
    if ! g++ -std=gnu++17 -Wall -Wextra -Werror -O1 $INC -I "$UNITY" \
            "test/$S/$S.cpp" $SRC "$UNITY/unity.c" -o "$OUT/$S" 2> "$OUT/$S.build.log"; then
        cat "$OUT/$S.build.log"; FAILED=1; continue
    fi
    "$OUT/$S" | tail -n 4 || FAILED=1
done
[ $FAILED -eq 0 ] && echo "SEMUA SUITE LOLOS" || echo "ADA SUITE YANG GAGAL"
exit $FAILED
