#!/usr/bin/env bash
# OpenUG2 -- Windows simulator of the Vita build (sim/sim_vita.c).
#
#   ./sim/build_sim.sh            -> sim/build/OpenUG2sim.exe (+ SDL2.dll)
#
# 32-bit (ILP32, like the Vita) with zig's clang (python -m ziglang). The game
# sources compile exactly as for the Vita: __vita__ and N2_GLES on, the Vita
# GL shims in front of every GL call. SDL2 = the official MinGW package
# (SDL2_DIR, default E:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${SIM_OUT:-$ROOT/sim/build}"
SDL2_DIR="${SDL2_DIR:-E:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32}"
CC=(python -m ziglang cc -target x86-windows-gnu)
mkdir -p "$OUT/obj"

GEN="$ROOT/src/generated_world_debug_shaders.h"
[ -f "$GEN" ] || { echo "missing $GEN (run the Vita build once)"; exit 1; }

CFLAGS=(-O2 -g -fno-omit-frame-pointer -std=gnu99 -w -fno-strict-aliasing -DN2_GLES
        -I"$ROOT/sim/include" -I"$SDL2_DIR/include" -I"$ROOT/src")
GAME_FLAGS=(-include "$ROOT/sim/include/sim_pre.h")

SRC="src/main.c src/render.c src/physics.c src/n2sim.c src/wcoll.c src/carcoll.c src/airacer.c src/perfparts.c src/airace.c src/racepath.c src/style.c src/ai.c src/audio.c src/resource.c src/world.c
     src/world_instance.c src/world_resident.c src/world_mesh.c src/hud.c src/feng.c src/shop.c src/world_tiles.c src/carfx.c src/chasecam.c src/jumpcam.c src/nis.c src/nisdraw.c src/acid.c src/aciddraw.c src/carsfx.c src/eal3.c src/music.c src/options.c src/profile.c src/pcsave.c src/garage_line.c src/vinyl.c src/partanim.c src/smokeable.c src/worldanim.c src/career.c src/movie.c src/mplay.c src/speech.c src/tuning.c src/replay.c src/rain.c src/reverb.c"

OBJS=(); pids=()
# SIM_JOBS=N caps parallel compiles (0/unset = all at once) -- share RAM with other sessions' builds
throttle() { local n="${SIM_JOBS:-0}"; [ "$n" -gt 0 ] || return 0
    while [ "$(jobs -rp | wc -l)" -ge "$n" ]; do wait -n || true; done; }
FAILMARK="$OUT/obj/.compile_failed"; rm -f "$FAILMARK"
for s in $SRC; do
    o="$OUT/obj/$(basename "${s%.c}").o"; OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ -n "$(find "$ROOT/src" "$ROOT/sim/include" -name '*.h' -newer "$o" -print -quit)" ]; then
        echo "CC $s"
        opt=(); case "$s" in src/world_instance.c) opt=(-O0);; esac   # see sim/README: -O2 trips its self-test
        throttle
        "${CC[@]}" "${CFLAGS[@]}" "${opt[@]}" "${GAME_FLAGS[@]}" -c "$ROOT/$s" -o "$o" || touch "$FAILMARK" &
        pids+=($!)
    fi
done
for s in sim/sim_vita.c sim/sim_gl.c sim/sim_heap.c; do
    o="$OUT/obj/$(basename "${s%.c}").o"; OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ "$ROOT/vita/vu_shim.inc" -nt "$o" ] || [ -n "$(find "$ROOT/sim/include" -name '*.h' -newer "$o" -print -quit)" ]; then
        echo "CC $s"
        throttle
        "${CC[@]}" "${CFLAGS[@]}" -c "$ROOT/$s" -o "$o" || touch "$FAILMARK" &
        pids+=($!)
    fi
done
wait || true
[ ! -e "$FAILMARK" ] || { echo "compile failed"; exit 1; }

echo "LD OpenUG2sim.exe"
"${CC[@]}" -g "${OBJS[@]}" "$SDL2_DIR/lib/libSDL2.dll.a" -ldbghelp -Wl,--stack,8388608 -o "$OUT/OpenUG2sim.exe"
cp -u "$SDL2_DIR/bin/SDL2.dll" "$OUT/"
echo "built $OUT/OpenUG2sim.exe"
