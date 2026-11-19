# NFSU2 Vita Prep Tool (developer notes) (Windows)

Was UG2Converter (renamed 2026-11-12). Checks the player's game files,
does the Vita's one-time "OPTIMIZING WORLD DATA" boot step on a PC (about
2 minutes for all six tracks instead of 15-20+ on the console), and puts
the game files + cache on the Vita over FTP or into a folder (USB).

## Game files: the check and what is sent

`converter/game_manifest.inc` lists every file the Vita needs with its size
and CRC32 (names and numbers only, no game data). Regenerate it with

    python tools/gen_prep_manifest.py <NFSU2 v1.2 game folder>

It takes the 12 folders the README lists and leaves out the files the port
never opens: `SOUND/ENGINE/CAR_nn_ENG_MB_EE.ABK` (audio.c loads the `_SPU`
banks). Add a rule there when another file is proven unused (the original
track bundles must stay: the game reads texture pixels from them on demand
even with the compact copies). The data files are all dated 2004, so v1.0
and v1.2 installs carry the same data; only SPEED2.EXE differs (4,800,512
bytes = v1.2), and the port does not use it.

A passing check is remembered (HKCU `Software\NFSU2Vita\PrepTool`,
`CheckedOK` = fingerprint of path + sizes + write times); the next check of
an unchanged folder compares sizes only. Sending lists each remote folder
once (FTP LIST) and skips game files already there at the same size; cache
files always go.

It runs the game's own conversion (`src/world_tiles.c`, `src/resource.c`,
built 32-bit like the Vita, same as the simulator) on the player's NFSU2
folder and writes what the Vita would write to `ux0:data/NFSU2/cache`:

- `cache/tiles/<TRACK>_v5.pak` + `.idx` -- the prebuilt world tiles
- `cache/<TRACK>.BUN.compact` -- the compact bundle copies
- `cache/<TRACK>.BUN.compact.strip` + `cache/<STEM>.focus` -- the 1 MB
  bundle world tiles keep and the track's start, so even the first load
  never reads the 57 MB copy (`converter/conv_extras.c`)
- `cache/mods_<CAR>.bin` -- each player car's parts lists (~330 ms a car
  on the Vita the first time it is loaded); cars with no model in the
  install (vinyl-only folders) are skipped
- `cache/shop_vNN.bin` -- the shop screens and art (`src/shop.c`
  `shop_build_cache`)

for STREAML4RA (city), L4RB/L4RD (drag), L4RC (drift), L4RF (Street X),
L4RG (URL). The output is byte-identical to the simulator's own boot
conversion (checked 2026-10-18; mesh counts match the Vita's logs; strip,
focus, mods and shop files checked identical to the simulator's 2026-10-25).
World detail is set to low, as the Vita's default.

## Use

1. Run `NFSU2VitaPrep.exe` (one file, nothing to install). It finds the NFSU2
   install from the registry; otherwise Browse to the folder with `TRACKS`.
2. Convert.
3. Send to Vita: the Vita's FTP server on port 1337 (VitaShell: SELECT, or
   vitacompanion). Or copy the `cache` folder from "Open output folder" into
   `ux0:data/NFSU2/` by hand.

Command line: `--check GAME`, `--cli GAME OUT` (check + convert, log in
`OUT/convert_log.txt`), `--send IP OUT [GAME]`, `--copy DEST OUT [GAME]`.

World Expansion (`conv_xp.c`, merged from the separate Expansion Prep Tool
11-19): optional Most Wanted / Carbon folders (`--cli GAME OUT [MW|- [CARBON|-]]`).
It checks them against `xp_manifest.inc` (`tools/gen_xp_manifest.py`),
translates each city with `src/xpconv.c` / `xproads.c` into `OUT/stage/TRACKS`,
runs the same world conversion plus the skyline on it, and adds the files to
the send list (stream, companion, caches, minimap, road net, props, events,
then the `.xp` marker and the tile index last). It does not send
`expansion/enable.txt`: the player switches the cities on in the Vita's
Settings.

When sending, the Vita's old `.idx` files are deleted first and each track's
index goes up last, so a cut-off transfer reads as "not converted" (the Vita
then converts that track itself) instead of as a broken set.

Not converted here: shader binaries (GPU-specific, the Vita compiles them),
texclass lists (learned while playing).

shop.c is linked whole for its cache builder; what the rest of the shop
calls (career, profiles, music ...) is stubbed in `conv_stubs.c` and stops
the tool with the name if ever reached.

## Build

    ./converter/build_converter.sh   # -> converter/build/NFSU2VitaPrep.exe

zig cc, SDL2 linked statically (`SDL2_DIR`, default the simulator's). A
`WTILE_VERSION` bump, a `SHOP_CACHE` version bump (shop.c) or a change to the
conversion code needs a rebuild of the tool too, or the Vita ignores the old
files and builds its own. New game symbols the shop starts calling show up
as link errors: add them to `conv_stubs.c`.
