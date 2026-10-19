# OpenUG2 Vita simulator (Windows)

Runs the **Vita build of the game code** on a PC: the same sources compiled
with `__vita__` and `N2_GLES`, the same front end, GLSL ES shaders, uniform /
variant shims (`vita/vu_shim.inc`, shared with `vita/sdl_vita.c`) and the
Vita's 720x408 framebuffer. Only the platform layer differs
(`sim/sim_vita.c` instead of `vita/sdl_vita.c`): real SDL2, an OpenGL ES 2.0
context on the desktop driver, files under a local folder.

It is for checking what a change looks like before a build goes to the
console. It does not reproduce the Vita's speed, memory limits or vitaGL bugs.

## Build

    ./sim/build_sim.sh        # -> sim/build/OpenUG2sim.exe (+ SDL2.dll, .pdb)

32-bit (ILP32 like the Vita), zig's clang (`python -m ziglang cc`), SDL2 MinGW
package in `E:/re-tools/simdeps` (override with `SDL2_DIR`).
`src/world_instance.c` is built at -O0: at -O1/-O2 clang's code crashes in
`world_instance_cache_selftest` (the write after the cache eviction); GCC's
Vita build passes it. Worth a look -- it may be undefined behaviour.

## Folders

`ux0:` / `app0:` map to `<root>/ux0` and `<root>/app0`, root =
`OPENUG2_SIM_ROOT` or `E:/NFS Underground 2 Vita/simroot`. In there,
`ux0/data/OpenUG2/{CARS,GLOBAL,TRACKS,...}` are junctions to the PC game data
(`data/nfsug2`); `log.txt`, `boot.txt`, `garage.txt`, `options.bin` and the
cache are the simulator's own. Screenshots go to `ux0/shots/`.

## Input

Keyboard as the Vita pad (through the same controller bindings): arrows =
D-pad, Z Cross, X Circle, A Square, S Triangle, Q L, W R, Enter Start,
Right Shift Select, J/L left stick, U/O right stick; F12 = screenshot.

## Scripts

`OPENUG2_SIM_SCRIPT=<file>` drives the pad, one command per line:

    wait 12          # seconds (loading runs on threads)
    frames 30
    press START      # held 3 frames; "press CROSS 10" for 10
    hold UP / release UP
    stick -1 0       # left stick x y
    rstick 0 -1      # right stick x y
    hyd 1            # hold Hydraulic Pressurize (1) / Bounce (2), 0 lets go
    shot main_menu   # -> simroot/ux0/shots/main_menu.png
    quit

Examples in `simroot/scripts/`. `OPENUG2_SIM_FAST=1` turns vsync off,
`OPENUG2_SIM_CONSOLE=1` keeps the log on the console. A crash prints the
faulting function and the stack (from the .pdb) to log.txt.

## Differences from the Vita found so far

- Depth: vitaGL makes every depth buffer 32-bit float (DF32M) whatever format
  the game asks for; the simulator remaps depth renderbuffers the same way and
  renders frame 0 into a float-depth buffer (`OPENUG2_SIM_D24=1` turns both off).
  Without it the 16-bit scene target z-fought: dark rims, black specks.
- Still open (not on hardware): a solid black triangle on some car bodies,
  e.g. the Tiburon's door -- dark body sub-meshes (material 0fedee40) drawn
  in front of the paint. `probe X Y` in a script logs which draw paints a pixel.
