#!/bin/bash
# Alternated frozen-grid boots: abgrid.sh ROUNDS "label|dir|ENV=v ENV2=v" ...   (dir: release or run)
# prints one line per boot: label, median fps of the rounds, last perf lite line's GPU ms + draw thread %, mean sensors
R="/home/deck/claude projects/fm4deck/FM4-Vulkan-WIP"; N="/home/deck/claude projects/fm4deck/run"
rounds=$1; shift
for r in $(seq 1 "$rounds"); do
  for spec in "$@"; do
    IFS='|' read -r label dir envs <<< "$spec"
    [ "$dir" = release ] && D="$R" || D="$N"
    L="$label-$r"
    ( cd "$D" && env $envs tools/deck/grid.sh "$L" 30 8 > /dev/null 2>&1 )
    f="$D/out/$L.txt"
    fps=$(grep -E '^[0-9.]+ fps$' "$f" | awk '{print $1}' | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    pl=$(grep 'perf lite: [0-9]' "$f" | tail -3 | sed -n 's/.*GPU \([0-9.]*\) ms\/frame | draw thread \([0-9]*\)%.*/\1 \2/p' | awk '{g+=$1; d+=$2; n++} END {if (n) printf "GPU %.2f ms, draw %d%%", g/n, d/n}')
    sens=$(tail -25 "$D/out/$L-sensors.txt" | awk '$2 ~ /^[0-9]+$/ && $3 >= 50 {n++; c+=$2; p+=$4; t+=$5; u+=$6} END {if (n) printf "%.0f MHz %.1f W %.0f C cpu %.0f MHz", c/n, p/n, t/n, u/n}')
    echo "$L: $fps fps | $pl | $sens" | tee -a "$N/out/abgrid.txt"
  done
done
