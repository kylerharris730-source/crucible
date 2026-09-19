#!/usr/bin/env bash
# =============================================================================
# lagbench.sh -- does lava dropped into water still hold 60 fps?
#
#     mingw32-make lagbench
#     bash scripts/lagbench.sh [threads]      threads defaults to 8
#
# Runs tools/steamprof.cpp's three lava-into-water scenes in a generated world
# with lighting on -- the blob it has always had, and the sheet across the
# whole basin that was reported as lagging, at two thicknesses -- and fails if
# any scene misses 16.6 ms on more than 1% of its frames.
#
# A benchmark, not a test, and kept out of run_tests.sh on purpose: frame time
# depends on the machine and on what else it is doing, and a suite that fails
# because a browser was open is a suite people learn to ignore. The census
# steamprof prints after each scene is the other half of the result -- a
# change that made a scene fast by boiling less water shows up there.
#
# The game frame also pays for the panel, the player and creatures, none of
# which is in here, so treat the numbers as the sim's share of the frame, and
# the 1% line as a floor rather than a promise.
# =============================================================================
set -u
cd "$(dirname "$0")/.."

THREADS=${1:-8}
OUT=artifacts/steamprof.exe
mkdir -p artifacts
SRCS=$(ls src/*.cpp | grep -v '/main\.cpp$')
echo "building steamprof..."
# shellcheck disable=SC2086
if ! g++ -std=c++11 -O3 -Isrc -DCINDERLIFT_BUILD_ID='"lagbench"' \
        -DCINDERLIFT_VERSION='"lagbench"' tools/steamprof.cpp $SRCS -o "$OUT" \
        -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32 >artifacts/lagbench_build.log 2>&1; then
    echo "steamprof did not build -- see artifacts/lagbench_build.log"
    exit 2
fi

fail=0
run() {   # run <label> <args...>
    local label="$1"; shift
    local log
    log=$("$OUT" "$THREADS" "$@")
    local stats over census
    stats=$(echo "$log" | grep -A1 "whole transient" | tr '\n' ' ' | sed 's/  */ /g;s/whole transient //')
    over=$(echo "$stats" | sed -n 's/.*over 16.6ms \([0-9.]*\)%.*/\1/p')
    census=$(echo "$log" | grep "steam " | sed 's/  */ /g')
    local verdict=ok
    if awk -v o="$over" 'BEGIN { exit !(o > 1.0) }'; then verdict=FAIL; fail=1; fi
    printf '  %-5s %-22s %s\n' "$verdict" "$label" "$stats"
    printf '        %-22s %s\n' "" "$census"
}

echo "lava into water, $THREADS threads, lighting on (ms per frame):"
run "blob"
run "sheet, 15 thick"      layer thick=15
run "sheet, 30 thick"      layer thick=30

if [ $fail -ne 0 ]; then
    echo "LAG: a scene missed 60 fps on more than 1% of frames"
    exit 1
fi
echo "PASS"
