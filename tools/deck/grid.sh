#!/bin/sh
# The frozen-grid test, as the PC's work\bench\fm4grid.ps1: boot, drive to the first race's grid (World Tour, first
# event), freeze the world there, settle, then print the fps for N one-second rounds and keep the perf lite lines.
# Stays running afterwards when KEEP=1 (for live A/B: python3 tools/perf/ab.py ab REGPU_NAME 5 4 switch).
#
#   tools/deck/grid.sh LABEL [settle s] [rounds]
#   FM2_RESOLUTION=1280x800 FM2_MSAA=1 REGPU_SOMETHING=1 tools/deck/grid.sh test1
#
# Results: out/LABEL.txt (fps rounds, the last perf lite lines, sensors), out/LABEL.log (the game's log).
set -e
cd "$(dirname "$0")/../.."
LABEL=${1:-grid}
SETTLE=${2:-40}
ROUNDS=${3:-6}
mkdir -p out
export FM2_VSYNC=0 FM2_MAX_FPS=1000 REGPU_DEBUG=1 REGPU_DEBUG_PORT=7351 REGPU_VBLANK_HZ=1000 \
    FM4_GRID_TEST=1 FM4_GRID_FREEZE_AT=9.0 FM4_WHEEL_MODE=off REGPU_PERF_LITE=1
export FM2_RESOLUTION=${FM2_RESOLUTION:-1280x800} FM2_MSAA=${FM2_MSAA:-1}
pkill -x fm4.exe 2>/dev/null || true
sleep 2
rm -f fm2.log
tools/deck/sensors.sh $((SETTLE + ROUNDS + 400)) "out/$LABEL-sensors.txt" &
SENS=$!
./play.sh &
for i in $(seq 1 120); do [ -f fm2.log ] && break; sleep 1; done
python3 tools/perf/route.py fm2.log 400
sleep "$SETTLE"
{
    echo "== $LABEL: $FM2_RESOLUTION msaa $FM2_MSAA, $(date)"
    env | grep -E '^(REGPU|FM4|FM2)_' | sort
    for i in $(seq 1 "$ROUNDS"); do python3 tools/perf/ab.py fps 1 | tail -1; done
    grep 'perf lite:' fm2.log | tail -5
    echo "sensors (last 10 s):"; tail -10 "out/$LABEL-sensors.txt"
} | tee "out/$LABEL.txt"
cp fm2.log "out/$LABEL.log"
if [ "$KEEP" != "1" ]; then
    python3 tools/regpu/regpuctl.py quit > /dev/null 2>&1 || true
    sleep 3; pkill -x fm4.exe 2>/dev/null || true
    kill $SENS 2>/dev/null || true
fi
