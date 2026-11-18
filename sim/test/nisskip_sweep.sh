#!/usr/bin/env bash
# Sweep: every race intro skipped 1 s in, per-car watch log after the hand-off.
# Type-1 intros on sprint 4121 (L4RA), type-2 on 4605 (L4RF). Output: logs/build/nisskip/<i>.txt
# Usage (from OpenUG2 root or worktree): ./sim/test/nisskip_sweep.sh [first] [last]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SR="E:/NFS Underground 2 Vita/simroot"
R="$SR/ux0/data/NFSU2"
OUT="E:/NFS Underground 2 Vita/logs/build/nisskip"; mkdir -p "$OUT"
printf 'wait 22\nquit\n' > "$SR/scripts/wait22.txt"
TYPE2=" 18 19 20 21 22 23 24 25 28 29 36 37 39 40 "
cd "$ROOT/sim/build" || exit 1
for i in $(seq "${1:-0}" "${2:-41}"); do
    case " 26 27 31 32 33 34 35 " in *" $i "*) continue;; esac     # types 3-5 (drag / 5-6 racers): not here
    case "$TYPE2" in *" $i "*) ev="STREAML4RF 4605";; *) ev="STREAML4RA 4121";; esac
    echo "$ev" > "$R/venue.txt"
    OPENUG2_SIM_NISIDX=$i OPENUG2_SIM_NISSKIP=1 OPENUG2_SIM_SCRIPT="$SR/scripts/wait22.txt" \
        timeout 120 ./OpenUG2sim.exe >/dev/null 2>&1
    grep -E "venue boot|nis: |pre-race scene|^watch|nis event" "$R/log.txt" > "$OUT/$i.txt"
    bad=$(grep -c "^watch.*!" "$OUT/$i.txt")
    echo "$i $(grep -o 'IntroNis[0-9]* v[0-9]' "$OUT/$i.txt" | head -1) on ${ev#* }: odd rows $bad"
done
