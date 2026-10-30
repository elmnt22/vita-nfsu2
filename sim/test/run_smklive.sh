#!/usr/bin/env bash
# Builds and runs sim/test/t_smklive.c against the sim objects (run ./sim/build_sim.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/sim/build"
SDL2_DIR="${SDL2_DIR:-E:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32}"
CC=(python -m ziglang cc -target x86-windows-gnu)
CFLAGS=(-O2 -g -std=gnu99 -w -fno-strict-aliasing -DN2_GLES
        -I"$ROOT/sim/include" -I"$SDL2_DIR/include" -I"$ROOT/src" -include "$ROOT/sim/include/sim_pre.h")
OBJS=()
for o in "$OUT"/obj/*.o; do
    case "$(basename "$o")" in main.o) ;; *) OBJS+=("$o");; esac
done
"${CC[@]}" "${CFLAGS[@]}" -c "$ROOT/sim/test/t_smklive.c" -o "$OUT/t_smklive.o"
"${CC[@]}" -g "$OUT/t_smklive.o" "${OBJS[@]}" "$SDL2_DIR/lib/libSDL2.dll.a" -ldbghelp -Wl,--stack,8388608 \
    -o "$OUT/t_smklive.exe"
cd "$OUT" && ./t_smklive.exe "$@"
