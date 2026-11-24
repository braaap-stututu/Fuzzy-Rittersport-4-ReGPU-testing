#!/bin/sh
# The AI benchmark race, as the PC's work\bench\fm4bench.ps1: boot, press into the first race, the race AI drives the
# player's car (FM4_BENCHMARK), N seconds are measured after GO plus a settle, the game closes itself, then the frame
# times of that window: average, p99, 1% and 0.1% lows.
#
#   tools/deck/bench.sh LABEL [seconds]
#   FM2_RESOLUTION=1280x800 REGPU_SOMETHING=1 tools/deck/bench.sh test1 60
#
# Results: out/LABEL.txt, out/LABEL.csv (frame times), out/LABEL.log, out/LABEL-sensors.txt.
set -e
cd "$(dirname "$0")/../.."
LABEL=${1:-race}
SECS=${2:-60}
mkdir -p out
export FM2_VSYNC=0 FM2_MAX_FPS=1000 REGPU_DEBUG=1 REGPU_DEBUG_PORT=7351 REGPU_VBLANK_HZ=1000 FM4_WHEEL_MODE=off \
    FM4_GRID_TEST=0 FM4_BENCHMARK=$SECS FM4_AI_PLAYER_LOG=1 REGPU_PERF_LITE=1 REGPU_FRAME_TIMES_SWAP="out/$LABEL.csv"
export FM2_RESOLUTION=${FM2_RESOLUTION:-1280x800} FM2_MSAA=${FM2_MSAA:-1}
pkill -x fm4.exe 2>/dev/null || true
sleep 2
rm -f fm2.log "out/$LABEL.csv"
tools/deck/sensors.sh $((SECS + 500)) "out/$LABEL-sensors.txt" &
SENS=$!
./play.sh &
GAME=$!
for i in $(seq 1 120); do [ -f fm2.log ] && break; sleep 1; done
python3 tools/bench/fm4route.py fm2.log 300 'benchmark: GO'
# The game closes itself after "benchmark: done"; give up after the race plus two minutes.
i=0
while kill -0 $GAME 2>/dev/null && [ $i -lt $((SECS + 120)) ]; do sleep 2; i=$((i + 2)); done
pkill -x fm4.exe 2>/dev/null || true
kill $SENS 2>/dev/null || true
cp fm2.log "out/$LABEL.log"
FROM=$(grep 'benchmark: measuring' fm2.log | head -1 | sed 's/^\[ *\([0-9.]*\)\].*/\1/')
TO=$(grep 'benchmark: done' fm2.log | head -1 | sed 's/^\[ *\([0-9.]*\)\].*/\1/')
{
    echo "== $LABEL: $FM2_RESOLUTION msaa $FM2_MSAA, $SECS s, $(date)"
    env | grep -E '^(REGPU|FM4|FM2)_' | sort
    echo "window: $FROM .. $TO"
    grep 'ai player: the player' fm2.log | head -1 || true
    [ -n "$FROM" ] && [ -n "$TO" ] && python3 tools/regpu/frametimes.py "out/$LABEL.csv" "$FROM" "$TO"
    echo "perf lite, median of the race's lines:"
    grep 'perf lite:' fm2.log | tail -40 | sed -n 's/.*GPU \([0-9.]*\) ms\/frame | draw thread \([0-9]*\)% busy.*/\1 \2/p' |
        sort -n | awk '{g[NR]=$1; d[NR]=$2} END {if (NR) printf "GPU %s ms/frame (median), draw thread busy %s%% (of the median-GPU line)\n", g[int((NR+1)/2)], d[int((NR+1)/2)]}'
    echo "sensors while the GPU was 90%+ busy:"
    awk 'NR > 1 && $3 >= 90 {n++; c+=$2; p+=$4; t+=$5} END {if (n) printf "%.0f MHz, %.1f W, %.0f C (%d s)\n", c/n, p/n, t/n, n}' "out/$LABEL-sensors.txt"
} | tee "out/$LABEL.txt"
