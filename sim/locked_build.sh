#!/usr/bin/env bash
# Serialises sim builds between parallel agents: one build_sim.sh at a time.
# Usage: SIM_OUT=sim/build_X ./sim/locked_build.sh
# Holds while E:/re-tools/out/.driv3r_hold exists (the coordinator creates it while
# the Driv3r session runs a big build).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="E:/re-tools/out/.simbuild.lock"
while [ -e "E:/re-tools/out/.driv3r_hold" ]; do sleep 20; done
until mkdir "$LOCK" 2>/dev/null; do
    # a lock older than 30 min is stale (crashed agent)
    if [ -n "$(find "$LOCK" -maxdepth 0 -mmin +30 2>/dev/null)" ]; then rmdir "$LOCK"; fi
    sleep 10
done
trap 'rmdir "$LOCK"' EXIT
SIM_JOBS="${SIM_JOBS:-2}" "$ROOT/sim/build_sim.sh" "$@"   # 2 compiles max: RAM is shared with Driv3r
