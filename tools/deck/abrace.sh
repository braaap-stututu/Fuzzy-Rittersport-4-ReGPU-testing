#!/bin/bash
# Alternated AI benchmark races: abrace.sh ROUNDS SECONDS "label|dir|ENV=v ..." ...   (dir: release or run)
R="/home/deck/claude projects/fm4deck/FM4-Vulkan-WIP"; N="/home/deck/claude projects/fm4deck/run"
rounds=$1; secs=$2; shift 2
for r in $(seq 1 "$rounds"); do
  for spec in "$@"; do
    IFS='|' read -r label dir envs <<< "$spec"
    case "$dir" in release) D="$R";; run) D="$N";; *) D="/home/deck/claude projects/fm4deck/$dir";; esac
    L="$label-$r"
    ( cd "$D" && env $envs tools/deck/bench.sh "$L" "$secs" > /dev/null 2>&1 )
    f="$D/out/$L.txt"
    s=$(grep -E "avg|1% low|frames >" "$f" | tr '\n' ' ' | tr -s ' ' | cut -c1-200)
    g=$(grep "GPU .* ms/frame (median)" "$f" | sed 's/ (of the median-GPU line)//')
    sens=$(grep -A1 "sensors while" "$f" | tail -1)
    echo "$L: $s | $g | $sens" | tee -a "$N/out/abrace.txt"
  done
done
