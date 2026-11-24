#!/bin/bash
# CPU profile of the frozen grid (SteamOS ptrace_scope=1: perf starts grid.sh as its child; host perf from
# ReGPU-V/work/tools/perf-host through its own loader). Records PSECS seconds once the grid is frozen and settled.
#   fm4profile.sh <label> [perf seconds] [VAR=value ...]     (PROFILE=race: the AI benchmark race instead of the grid)
set -u
LABEL="${1:?label}"; PSECS="${2:-15}"; shift; [ $# -gt 0 ] && shift
D="$(dirname "$(readlink -f "$0")")"; RUN="$(cd "$D/../.." && pwd)"
P="/home/deck/claude projects/ReGPU-V/work/tools/perf-host"
OUT="$RUN/out/profile-$LABEL"; rm -rf "$OUT"; mkdir -p "$OUT"; mkfifo "$OUT/ctl" "$OUT/ack"
if [ "${PROFILE:-grid}" = race ]; then CMD=("$D/bench.sh" "prof-$LABEL" 60); else CMD=("$D/grid.sh" "prof-$LABEL" 20 $((PSECS + 15))); fi
env "$@" "$P/ld.so" --library-path "$P/lib" "$P/perf" record -F 1999 -D -1 --control "fifo:$OUT/ctl,$OUT/ack" -o "$OUT/perf.data" -- \
    "${CMD[@]}" > "$OUT/grid.txt" 2>&1 &
BPID=$!
exec 3<>"$OUT/ctl" 4<>"$OUT/ack"
sleep 20
WANT='grid test: frozen'; [ "${PROFILE:-grid}" = race ] && WANT='benchmark: measuring'
for i in $(seq 1 400); do grep -q "$WANT" "$RUN/fm2.log" 2>/dev/null && break; sleep 1; done
[ "${PROFILE:-grid}" = race ] && sleep 10 || sleep 25
echo enable >&3; read -t 5 -u 4 a
sleep "$PSECS"
echo disable >&3; read -t 5 -u 4 a
wait $BPID
"$P/ld.so" --library-path "$P/lib" "$P/perf" script -i "$OUT/perf.data" -F tid,comm,ip,dso > "$OUT/samples.txt" 2>/dev/null
distrobox enter regpu -- python3 -I "$D/fm4profile.py" "$OUT/samples.txt" "$RUN/fm4.exe" > "$OUT/report.txt"
head -150 "$OUT/report.txt"
