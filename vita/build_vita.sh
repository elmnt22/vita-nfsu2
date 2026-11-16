#!/usr/bin/env bash
# OpenUG2 — PS Vita build. Produces vita/build/NFSU2Vita.vpk.
#
#   VITASDK=E:/vitasdk ./vita/build_vita.sh
#
# Needs VitaSDK with vitaGL + vitashark + mathneon installed. Install the VPK,
# then copy your own NFSU2 data (TRACKS/, CARS/, GLOBAL/, ...) to
# ux0:data/NFSU2/. libshacccg.suprx must be extracted on the console.
set -euo pipefail

VITASDK="${VITASDK:-E:/vitasdk}"
BIN="$VITASDK/bin"
CC="$BIN/arm-vita-eabi-gcc"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/vita/build"
TITLE="OpenUG2"                    # file names (elf, vpk)
VPK_NAME="NFSU2Vita"                # the release VPK's file name
DISPLAY_TITLE="NFS Underground 2"  # name on the LiveArea / home screen
TITLEID="NFSU20001"
VERSION="00.11"

mkdir -p "$OUT/obj" "$OUT/sce_sys"

# Same embedded debug-shader header the desktop Makefile generates.
GEN="$ROOT/src/generated_world_debug_shaders.h"
if [ ! -f "$GEN" ] || [ "$ROOT/src/world_debug_120.vert" -nt "$GEN" ] || [ "$ROOT/src/world_debug_120.frag" -nt "$GEN" ]; then
    ( cd "$ROOT" &&
      echo "/* AUTO-GENERATED from world_debug_120.vert/.frag by make -- do not edit */" > "$GEN" &&
      xxd -i src/world_debug_120.vert >> "$GEN" &&
      xxd -i src/world_debug_120.frag >> "$GEN" )
fi

CFLAGS=(-O2 -g -std=gnu99 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
        -Wno-misleading-indentation -mcpu=cortex-a9 -mfpu=neon -ftree-vectorize -fno-strict-aliasing
        -fno-math-errno -DN2_GLES "-I$ROOT/vita/include" "-I$ROOT/src")
# -fno-math-errno: sqrtf etc. become a bare vsqrt (no errno call path); results are identical,
# so physics stays bit-exact. Draw-side files (no sim/AI state) also get -O3; physics, AI and
# collision (n2sim, carcoll, airacer, main) keep -O2.
O3_FILES=" src/render.c src/world_instance.c src/world_tiles.c src/world_mesh.c src/acid.c src/aciddraw.c src/carfx.c src/nisdraw.c src/hud.c "
# rebuild everything when the flags change
FLAGSTAMP="$OUT/obj/.flags"
FLAGSTR="${CFLAGS[*]} | O3:$O3_FILES"
if [ ! -f "$FLAGSTAMP" ] || [ "$(cat "$FLAGSTAMP")" != "$FLAGSTR" ]; then
    echo "compiler flags changed: full rebuild"; rm -f "$OUT"/obj/*.o; printf '%s' "$FLAGSTR" > "$FLAGSTAMP"
fi

SRC="src/main.c src/render.c src/physics.c src/n2sim.c src/wcoll.c src/carcoll.c src/airacer.c src/perfparts.c src/airace.c src/racepath.c src/style.c src/ai.c src/audio.c src/resource.c src/world.c
     src/world_instance.c src/world_resident.c src/world_mesh.c src/hud.c src/feng.c src/shop.c src/world_tiles.c src/carfx.c src/chasecam.c src/jumpcam.c src/nis.c src/nisdraw.c src/acid.c src/aciddraw.c src/carsfx.c src/eal3.c src/music.c src/options.c src/profile.c src/pcsave.c src/garage_line.c src/vinyl.c src/partanim.c src/smokeable.c src/skyline.c src/worldanim.c src/career.c src/trophy.c src/movie.c src/mplay.c src/speech.c src/tuning.c src/replay.c src/rain.c src/reverb.c src/savenote.c vita/sdl_vita.c"

OBJS=()
pids=()
# VITA_JOBS caps parallel compiles (default 2: RAM is shared with other sessions' builds; 0 = all at once)
throttle() { local n="${VITA_JOBS:-2}"; [ "$n" -gt 0 ] || return 0
    while [ "$(jobs -rp | wc -l)" -ge "$n" ]; do wait -n || true; done; }
FAILMARK="$OUT/obj/.compile_failed"; rm -f "$FAILMARK"
for s in $SRC; do
    o="$OUT/obj/$(basename "${s%.c}").o"
    OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] ||
        { [ "$s" = vita/sdl_vita.c ] && { [ "$ROOT/vita/vu_shim.inc" -nt "$o" ] || [ "$ROOT/vita/boot_settings.inc" -nt "$o" ]; }; } ||
        [ -n "$(find "$ROOT/src" "$ROOT/vita/include" -name '*.h' -newer "$o" -print -quit)" ]; then
        echo "CC $s"
        throttle
        XF=(); case "$O3_FILES" in *" $s "*) XF=(-O3);; esac
        "$CC" "${CFLAGS[@]}" "${XF[@]}" -c "$ROOT/$s" -o "$o" || touch "$FAILMARK" &
        pids+=($!)
    fi
done
wait || true
[ ! -e "$FAILMARK" ] || { echo "compile failed"; exit 1; }

# vitaGL: vita/lib/libvitaGL.a is upstream cdbba42 built with NO_DEBUG=1
# HAVE_VERTEX_LAYOUT_CACHE=1 plus a small patch (see vita/PORTING_NOTES.md).
# VGL=stock links the VitaSDK-installed library instead.
VGL_DIR=()
if [ "${VGL:-ug2}" != stock ]; then VGL_DIR=("-L$ROOT/vita/lib"); fi
echo "vitaGL: ${VGL:-ug2}"

LIBS="-lvitaGL -lvitashark -lSceShaccCgExt -ltaihen_stub -lSceShaccCg_stub -lmathneon
      -lScePvf_stub -lSceCommonDialog_stub -lSceGxm_stub -lSceDisplay_stub -lSceAppMgr_stub -lSceAppUtil_stub
      -lSceNpTrophy_stub -lSceSysmodule_stub -lSceKernelDmacMgr_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub -lScePower_stub
      -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lstdc++ -lm -lc"

echo "LD $TITLE.elf"
"$CC" -Wl,-q -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=_write_r,--wrap=printf,--wrap=vprintf,--wrap=fprintf,--wrap=vfprintf,--wrap=puts,--wrap=fputs,--wrap=putchar,--wrap=fputc,--wrap=fwrite,--wrap=sceGxmEndScene,--wrap=sceGxmBeginScene,--wrap=sceGxmDisplayQueueAddEntry,--wrap=sceGxmNotificationWait,--wrap=sceGxmFinish "${OBJS[@]}" "${VGL_DIR[@]}" $LIBS -o "$OUT/$TITLE.elf"

"$BIN/vita-elf-create" "$OUT/$TITLE.elf" "$OUT/$TITLE.velf" >/dev/null
"$BIN/vita-make-fself" -c "$OUT/$TITLE.velf" "$OUT/eboot.bin"
"$BIN/vita-mksfoex" -s TITLE_ID="$TITLEID" -s APP_VER="$VERSION" -d ATTRIBUTE2=12 "$DISPLAY_TITLE" "$OUT/sce_sys/param.sfo"

# trophy pack (sce_sys/trophy/NFSU20001_00/TROPHY.TRP, packed below with the rest of sce_sys)
TRP="$ROOT/vita/sce_sys/trophy/${TITLEID}_00/TROPHY.TRP"
if [ ! -f "$TRP" ] || [ "$ROOT/tools/trophy_trp.py" -nt "$TRP" ] || [ "$ROOT/tools/trophies.json" -nt "$TRP" ]; then
    python "$ROOT/tools/trophy_trp.py" --out "$TRP" || echo "trophy_trp failed: no trophy pack"
fi
PACK=(-s "$OUT/sce_sys/param.sfo" -b "$OUT/eboot.bin")
# bubble icon, boot splash (pic0) and LiveArea (livearea/contents/*)
while IFS= read -r f; do
    PACK+=(-a "$f=sce_sys/${f#"$ROOT/vita/sce_sys/"}")
done < <(find "$ROOT/vita/sce_sys" -type f)
# loading-screen controller art: img/loading_controller.png -> assets/loading_controller.raw
python "$ROOT/tools/lsc_png.py" || echo "lsc_png failed: drawn controller used"
# app assets (app0:assets/...): the tile conversion screen background
while IFS= read -r f; do
    PACK+=(-a "$f=assets/${f#"$ROOT/vita/assets/"}")
done < <(find "$ROOT/vita/assets" -type f)
"$BIN/vita-pack-vpk" "${PACK[@]}" "$OUT/$VPK_NAME.vpk"
echo "built $OUT/$VPK_NAME.vpk"
