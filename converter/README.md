# UG2Converter (developer notes) (Windows)

Does the Vita's one-time "OPTIMIZING WORLD DATA" boot step on a PC: about
2 minutes for all six tracks instead of 15-20+ on the console.

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

1. Run `UG2Converter.exe` (one file, nothing to install). It finds the NFSU2
   install from the registry; otherwise Browse to the folder with `TRACKS`.
2. Convert.
3. Send to Vita: the Vita's FTP server on port 1337 (VitaShell: SELECT, or
   vitacompanion). Or copy the `cache` folder from "Open output folder" into
   `ux0:data/NFSU2/` by hand.

Command line: `UG2Converter.exe --cli GAME OUT` (convert, log in
`OUT/convert_log.txt`), `UG2Converter.exe --send IP OUT`.

When sending, the Vita's old `.idx` files are deleted first and each track's
index goes up last, so a cut-off transfer reads as "not converted" (the Vita
then converts that track itself) instead of as a broken set.

Not converted here: shader binaries (GPU-specific, the Vita compiles them),
texclass lists (learned while playing).

shop.c is linked whole for its cache builder; what the rest of the shop
calls (career, profiles, music ...) is stubbed in `conv_stubs.c` and stops
the tool with the name if ever reached.

## Build

    ./converter/build_converter.sh   # -> converter/build/UG2Converter.exe

zig cc, SDL2 linked statically (`SDL2_DIR`, default the simulator's). A
`WTILE_VERSION` bump, a `SHOP_CACHE` version bump (shop.c) or a change to the
conversion code needs a rebuild of the tool too, or the Vita ignores the old
files and builds its own. New game symbols the shop starts calling show up
as link errors: add them to `conv_stubs.c`.
