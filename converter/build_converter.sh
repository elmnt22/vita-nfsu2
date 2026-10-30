#!/usr/bin/env bash
# OpenUG2 World Data Converter -- Windows tool that runs the Vita's one-time
# world tile conversion on a PC (converter/ug2conv.c).
#
#   ./converter/build_converter.sh   -> converter/build/UG2Converter.exe
#
# The game's world sources are compiled exactly as for the simulator (32-bit
# ILP32 like the Vita, __vita__ + N2_GLES, sim/include/sim_pre.h), so the
# files it writes are the ones the console writes. SDL2 is linked statically:
# one exe, nothing to install.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/converter/build"
SDL2_DIR="${SDL2_DIR:-E:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32}"
CC=(python -m ziglang cc -target x86-windows-gnu)
mkdir -p "$OUT/obj"

CFLAGS=(-O2 -g -std=gnu99 -w -fno-strict-aliasing -DN2_GLES
        -I"$ROOT/sim/include" -I"$SDL2_DIR/include" -I"$ROOT/src")
GAME_FLAGS=(-include "$ROOT/sim/include/sim_pre.h")

# world conversion + what it reaches (render/physics/wcoll for mesh packing
# and the collision copy the tiles carry)
SRC="src/world_tiles.c src/world_instance.c src/world.c src/resource.c src/world_mesh.c src/world_resident.c
     src/render.c src/physics.c src/wcoll.c src/shop.c src/feng.c converter/conv_extras.c"

OBJS=(); pids=()
for s in $SRC; do
    o="$OUT/obj/$(basename "${s%.c}").o"; OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ -n "$(find "$ROOT/src" "$ROOT/sim/include" -name '*.h' -newer "$o" -print -quit)" ]; then
        echo "CC $s"
        opt=(); case "$s" in src/world_instance.c) opt=(-O0);; esac   # see sim/README: clang -O2 trips its self-test
        "${CC[@]}" "${CFLAGS[@]}" "${opt[@]}" "${GAME_FLAGS[@]}" -c "$ROOT/$s" -o "$o" &
        pids+=($!)
    fi
done
for s in converter/conv_platform.c converter/conv_stubs.c sim/sim_gl.c converter/ug2conv.c; do
    o="$OUT/obj/$(basename "${s%.c}").o"; OBJS+=("$o")
    extra=(); case "$s" in converter/ug2conv.c) extra=(-I"$ROOT/converter");; esac
    echo "CC $s"
    "${CC[@]}" "${CFLAGS[@]}" "${extra[@]}" -c "$ROOT/$s" -o "$o" &
    pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
[ $fail = 0 ] || { echo "compile failed"; exit 1; }

echo "LD UG2Converter.exe"
"${CC[@]}" -g "${OBJS[@]}" "$ROOT/converter/ug2conv.rc" "$SDL2_DIR/lib/libSDL2.a" \
    -lwininet -lcomctl32 -lshell32 -lole32 -loleaut32 -luuid -ladvapi32 -lgdi32 -luser32 \
    -lwinmm -limm32 -lsetupapi -lversion -lcfgmgr32 \
    -Wl,--subsystem,windows -Wl,--stack,8388608 -o "$OUT/UG2Converter.exe"
echo "built $OUT/UG2Converter.exe"
