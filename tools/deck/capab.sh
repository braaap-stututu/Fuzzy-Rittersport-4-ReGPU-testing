#!/bin/bash
# Alternated capped AI races (the Deck profile's case): capab.sh ROUNDS CAP "label|ENV=v ..." ...   (from run/)
# Per race: fps, the CPU the game + wineserver used over 40 s of the race (% of one core, threads.py), APU power.
cd "$(dirname "$0")/../.."
rounds=$1; cap=$2; shift 2
for r in $(seq 1 "$rounds"); do
  for spec in "$@"; do
    IFS='|' read -r label envs <<< "$spec"
    L="$label-$r"
    ( env $envs BENCH_MAX_FPS=$cap tools/deck/bench.sh "$L" 60 > /dev/null 2>&1 ) &
    B=$!
    sleep 8 # (bench.sh removes the last race's fm2.log after 2 s: its "measuring" line isn't this race's)
    for i in $(seq 1 400); do grep -q 'benchmark: measuring' fm2.log 2>/dev/null && break; sleep 1; done
    sleep 10
    t0=$(date +%T)
    cpu=$(python3 tools/deck/threads.py 40 400 | awk '/^total/ {gsub("%","",$2); print $2}')
    t1=$(date +%T)
    wait $B
    f="out/$L.txt"
    fps="$(grep -o 'avg [0-9.]* fps' "$f" | head -1), $(grep -o '1% low [0-9.]* fps' "$f" | head -1), p99 $(python3 -c "
import sys
r=[l.strip().split(',') for l in open('out/$L.csv')]
lo,hi=[float(x) for x in sys.argv[1:3]]
m=sorted(float(b) for a,b in r if lo<=float(a)<=hi)
print('%.1f ms, frames > 20 ms: %d' % (m[int(len(m)*.99)], sum(x>20 for x in m)))
" $(sed -n 's/^window: \([0-9.]*\) \.\. \([0-9.]*\)$/\1 \2/p' "$f"))"
    sens=$(awk -v a="$t0" -v b="$t1" 'NR>1 && $1 >= a && $1 <= b {n++; m+=$2; u+=$3; w+=$4} END {if (n) printf "GPU %.0f MHz %.0f%% busy, APU %.2f W", m/n, u/n, w/n}' "out/$L-sensors.txt")
    echo "$L: $fps | cpu ${cpu}% of a core | $sens (same 40 s)" | tee -a out/capab.txt
  done
done
