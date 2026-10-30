#!/usr/bin/env bash
# Builds sim/test/t_movie.c + src/movie.c (standalone, no sim objects) and
# decodes movies. Usage:
#   run_movie.sh [file.vp6 [outdir [frames...]]]   decode, time, dump frames/WAV
#   CMP=1 run_movie.sh file.vp6                     bit-exact check vs ffmpeg (PC only)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/sim/build"
mkdir -p "$OUT"
CC=(python -m ziglang cc -target x86-windows-gnu)
"${CC[@]}" -O2 -std=gnu99 -Wall -Wno-unused-function -I"$ROOT/src" \
    "$ROOT/sim/test/t_movie.c" "$ROOT/src/movie.c" -o "$OUT/t_movie.exe"
MOV="${1:-$ROOT/../data/nfsug2/MOVIES/Scene01.vp6}"
if [ -n "${CMP:-}" ]; then
    python "$ROOT/sim/test/movie_cmp.py" "$OUT/t_movie.exe" "$MOV"
    exit
fi
DIR="${2:-${TMPDIR:-/tmp}/movie}"
mkdir -p "$DIR"
shift 2 2>/dev/null || true
"$OUT/t_movie.exe" "$MOV" "$DIR" "$@"
