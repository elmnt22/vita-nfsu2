#!/usr/bin/env bash
# Builds and runs sim/test/t_unlock_oracle.c (shop.c included, career.c fresh, the rest from the sim objects).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/${SIM_OUT:-sim/build}"
SDL2_DIR="${SDL2_DIR:-E:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32}"
CC=(python -m ziglang cc -target x86-windows-gnu)
CFLAGS=(${SHOP_COPY:+-DSHOP_COPY=\"$SHOP_COPY\"} -O1 -g -std=gnu99 -w -fno-strict-aliasing -DN2_GLES
        -I"$ROOT/sim/include" -I"$SDL2_DIR/include" -I"$ROOT/src" -include "$ROOT/sim/include/sim_pre.h")
OBJS=()
for o in "$OUT"/obj/*.o; do
    case "$(basename "$o")" in main.o|career.o|shop.o) ;; *) OBJS+=("$o");; esac
done
"${CC[@]}" "${CFLAGS[@]}" -c "$ROOT/src/career.c" -o "$OUT/t_unlock_career.o"
"${CC[@]}" "${CFLAGS[@]}" -c "$ROOT/sim/test/t_unlock_oracle.c" -o "$OUT/t_unlock.o"
"${CC[@]}" -g "$OUT/t_unlock.o" "$OUT/t_unlock_career.o" "${OBJS[@]}" "$SDL2_DIR/lib/libSDL2.dll.a" -ldbghelp -lopengl32 \
    -Wl,--stack,8388608 -o "$OUT/t_unlock.exe"
cd "$OUT" && ./t_unlock.exe "$@"
