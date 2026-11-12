# OpenUG2 → PS Vita: porting notes

Status and working knowledge for the Vita port. Last updated 2026-10-05.

## Build and run

- Toolchain: native-Windows VitaSDK at `E:\vitasdk` (vitaGL with its GLSL
  translator, vitashark, mathneon). No SDL2, zlib or make needed.
- Build from `OpenUG2/`: `VITASDK=E:/vitasdk ./vita/build_vita.sh`
  → `vita/build/NFSU2Vita.vpk` (title ID `NFSU20001`).
- Packaging: `ATTRIBUTE2=12` (extended memory, ~356 MB user RAM), plain
  `vita-make-fself -c`. Signing with `-a 0x2800000000000001` killed the app
  before `main()` on hardware.
- Console setup: game data in `ux0:data/NFSU2/` (TRACKS, CARS, GLOBAL,
  SOUND, ...); decrypted `libshacccg.suprx` in `ur0:data/` (ShaRKF00D).
- Bubble icon: `vita/sce_sys/icon0.png` (128×128, 8-bit palette).

## What the port changes

- `vita/sdl_vita.c` + `vita/include/{SDL.h,SDL_opengles2.h,zlib.h}`: the
  SDL2 subset the engine uses, implemented on vitaGL / SceCtrl / SceAudio /
  SceKernel. Buttons are surfaced as the desktop keys (table at the top of
  `sdl_vita.c`); left stick steers analog.
- `glGetUniformfv` (missing in vitaGL) answered from a shadow cache of
  `glUniform*` writes; only element 0 of array uploads is stored.
- `src/main.c` (`#ifdef __vita__`): default args (data path, 960×544, HUD),
  smaller open-world ring (450 m resident / 300 m draw), boot markers.
- Memory: `world.c` lends the loaded STREAM bundle to `world_instance.c`
  (no second 113 MB copy); instance prototypes are parsed lazily; the
  startup spawn lookup hands its bundle to the world load (`res_keep` /
  `res_take`); `Paths*.bin` found by one directory listing instead of
  4000 `fopen` probes; big files read with `read()` in 4 MB chunks.
- Shaders: vitaGL set to `VGL_MODE_SHADER_PAIR` so compile errors surface
  (POSTPONED reports success and crashes inside `glLinkProgram`). Fixed
  for Cg: no variable-indexed uniform arrays, no `x`/`y` name clashes, no
  varying named `distance`. A failing shader is replaced by flat magenta.

## Diagnostics (device and Vita3K)

- `ux0:data/NFSU2/boot.txt`: timestamped milestones with heap use and,
  after GL init, vitaGL free VRAM / RAM pool (`vita_mark`).
- `ux0:data/NFSU2/log.txt`: all engine `printf`/`fprintf`/`puts`/`fwrite`
  output, written immediately (linker `--wrap`).
- `oom.txt`: failed allocations with size and caller.
- `shader_fail_N.txt`: compiler messages and full source of a failed shader.
- Crash dumps: `python E:/armtest/vita-parse-core/main.py <psp2dmp>
  vita/build/OpenUG2.elf` with `E:/vitasdk/bin` on PATH; the ELF must be
  the one that was installed.
- Vita3K (`D:\Users\Desktop\Vita3K`, `-r NFSU20001`) is good for load and
  heap checks; it compiles the big world shader very slowly and returns
  garbage from `vglMemFree`.

## Current status (2026-10-05)

Playable free roam with seamless streaming, ~15 fps on hardware.

- Seamless streaming: compact pinned track bundle (57 MB in RAM, texture
  pixels read from the card, cached under ux0:data/NFSU2/cache/); 350 m
  residents rebuilt every 80 m with a 3 s lookahead; the worker thread does
  the build, validation, collision, mesh packing and texture decoding; the
  render thread only uploads (budgets rise near the resident edge).
- Rendering: lite world/sky shader, trimmed main shader, frustum cull, fog
  at 300 m, 3D at 720x408 upscaled with a native-res HUD, no vsync wait.
- Physics catch-up (up to 3 ticks per frame), tick-scaled chase camera,
  16 m ground triangle grid, retail-style street minimap.
- Test toggle: Select+Triangle cycles GPU experiment modes (log.txt).

## Next steps

1. Confirm the texture-predecode build keeps up at high speed.
2. Compact vertex format; DXT textures (vitaGL compressed-mip bug).
3. Render scale / detail settings file; frontend menu; remove diagnostics.

## Performance follow-up (2026-10-05, pending hardware test)

The supplied capture has 944 frames. Excluding startup frame 0: mean frame
time 60.85 ms, median 50.02 ms, p95 83.36 ms, maximum 382.61 ms. Resident
finish peaks are 219-252 ms. GPU timer queries are unavailable, and the CSV's
`cpu_player_sim` section includes resident finishing, so it is not a pure
physics measurement. `oom.txt` appends across launches; the supplied copy is
older than this run.

This follow-up changes:

- Ordinary/baseline residents no longer upload backdrop vista geometry that
  those tiers never draw. Full-tier and explicit resident audits retain it.
- Candidate support checks use the candidate's own ground grid, without
  activating it or scanning the whole scene. A startup regression compares
  indexed and brute-force results for stacked decks and checks active-grid
  isolation, using both fine and coarse indexes.
- Vita ordinary batch uploads yield between batches after 6 ms normally or
  12 ms with the existing urgent quota. These are soft budgets: a single GL
  upload, texture/sky work, or finalization can still exceed them. Synchronous
  startup remains unbounded. Compare overall build-to-activation latency as
  well as peak frame time; finer slicing may extend upload latency.
- Packed vertex staging is released as each mesh is copied into its batch,
  rather than freeing all staging on the final frame. Cancellation safely
  retains ownership of unconsumed entries. The packing loop also polls abort
  requests every 32 meshes.

The safety boundary and input bindings are unchanged. Select + D-pad Up/Down
belongs to the user's overclock overlay; Select + Triangle is the engine's
graphics experiment toggle.

Validation: VitaSDK compilation/link/package and diff whitespace check.
Desktop SDL/OpenGL regression execution is unavailable in this environment
(no desktop make/compiler/SDL setup; WSL access denied). Updated desktop
fixtures cover vista gating and packed/direct batch byte equality, but have
not been executed here. Startup regression execution and real performance
remain to be checked on hardware. The SDK build emits existing warnings in
asset/path handling; it is not warning-free.

Test the same fast route and west-to-south turn, then collect `boot.txt`,
`log.txt`, and `perf.csv`. Keep the matching `vita/build/OpenUG2.elf` for any
crash dump. Compare resident peak steps, complete streaming latency, safety
net messages, heap peaks, and GPU pool headroom against the supplied capture.

## Second streaming follow-up (2026-10-05, pending hardware test)

The next device capture confirms smaller resident finishing peaks (about
101-109 ms), but dense resident jobs still take 7.22-8.91 s from start to
activation. Mean CPU frame time excluding startup is 57.55 ms, versus
60.85 ms previously; routes differ, so this is not a controlled FPS benchmark.
There are no safety-net messages in this capture. The final southbound job
is still preparing when the supplied logs end. The old `oom.txt` is unchanged.

Changes for the next build:

- Lookahead starts at 7 s and follows a smoothed measurement of complete
  start-to-activation latency (clamped to 3-10 s). Prediction is capped at
  220 m, derived from resident radius minus safety margin and cell size.
  The previous implementation actually used 3 s / 120 m despite a stale
  comment saying 200 m. This aims farther ahead at speed while retaining
  overlap for candidate support checks. Existing turn cancellation remains.
- Batch upload soft budgets increase to 8 ms normally and 24 ms near the
  resident edge. This corrects the first follow-up's overly fine slicing,
  which reduced stalls but increased the number of upload frames.
- Deduplication verifies the filtered survivors in the existing sorted order
  before compaction, avoiding a second bbox pass and full sort. Exact content
  comparison and survivor order are unchanged. A startup self-test verifies
  duplicate removal, distinct prelight preservation, and region range remaps.
- `log.txt` uses one append handle, with a lightweight mutex to serialize
  loader/render writes. Each fragment still goes straight to `sceIoWrite`,
  without a new stdio buffer. Normal process exit closes the handle; mutex
  setup failure falls back to the previous per-write open/close path. Check
  that logs remain readable after exiting the app or recovering from a crash.

`log.txt` now reports `stream latency ...; lookahead ...` on activation.
Validate on the same fast route and the southbound turn, comparing complete
streaming latency and missing scenery as well as finishing peaks. Faster
arrival is not yet demonstrated on hardware. Vita compilation/packaging and
diff checks are the local verification; desktop runtime tests remain unavailable.

## Third streaming follow-up (2026-10-05, pending hardware test)

The latest run reaches generation 12. Complete jobs range from 4.82-8.33 s;
finishing peaks range from 70-134 ms. No new OOM or safety-net messages are
present. CPU mean frame time excluding startup is 60.05 ms across 1603 frames;
the longer, different route prevents attributing an FPS change to this build.

Remaining waste is visible in the logs: five aborted jobs, one discarded CPU
result and one discarded partial GPU upload. At 133.86 s the `(160,-400)`
candidate is discarded after almost six seconds of preparation/upload because
the prediction changed to `(160,-320)`. That is only one cell farther along
the same trajectory. Later two jobs are aborted consecutively on bends.

The next build changes only streaming retention/cancellation policy:

- A prepared or uploading candidate can finish if it covers the current car
  within radius minus safety margin (300 m) and is closer than the active
  resident to either the car or the predicted position. The existing 400 m²
  improvement threshold remains. Actual ground support is still checked before
  upload and activation; out-of-reach candidates are rejected.
- Early cancellation requires the target to be more than 120 degrees behind
  the velocity, rather than merely past 90 degrees. The existing 100 m distance,
  8 m/s speed and 4.5 s age limits remain. This avoids cancelling work for
  a nearly perpendicular target as the car follows a bend.
- Startup and the desktop resident test call a synthetic regression covering
  the recorded partial-upload case, an unhelpful/out-of-range replacement,
  near-perpendicular travel, true reverse direction and low-speed/nearby cases.

Vita build and diff checks are the local validation. Synthetic startup tests
and real behavior still require device execution. Compare discard/abort counts
and how long sections remain missing on the same winding route. The tradeoff
is that a build which becomes unhelpful on a moderate turn may now finish
instead of being cancelled immediately.

## Authored low-detail scenery experiment (2026-10-05)

The latest hardware capture remains around 14.8-15.4 FPS at the later checkpoints.
This build tests the game's authored simpler models rather than decimating roads.
On Vita, low world detail is the default. Put the word `full` in
`ux0:data/NFSU2/world_detail.txt` and restart to compare original detail with
exactly the same executable; delete the file or use `low` to restore low detail.
Desktop builds retain the original default. Set detail before loading and never
change it while a streaming worker runs.

For each placement, the builder selects a cheaper model only from its three
authored model keys. Counts come from the production parser's valid submeshes.
Only non-ground, non-vista OTHER-category alternatives with matching scenery
classification qualify. If no valid cheaper alternative loads, retain the original.
Original meshes are kept as collision-only; alternatives are render-only. World
batch upload, normal/vertex packing and texture requests skip collision-only
meshes. Wall collection and camera clipping skip render-only meshes. Dedup
compares mesh roles so it cannot merge collision and rendering ownership.
Roads and terrain remain unchanged. Keeping original CPU geometry while loading
additional simpler models can increase memory and preparation time; measure both.
Visual differences, prelighting and collision silhouettes require device review.

`boot.txt` now reports detail mode, replacements, saved source triangles, render
triangles and collision-only mesh counts for each neighborhood before dedup.
`tools/world_lod_audit.cjs` can inspect a STREAM bundle using Node. Its L4RA audit
found 3,104,192 original versus 1,266,568 lowest raw instanced source triangles
(59.2% reduction across all referenced placements). This is an asset estimate,
not the runtime reduction: it excludes uninstanced ground, ignores visibility,
and does not apply production compatibility checks. It cannot predict FPS.

Local validation: Vita cross-build, production and instance fixture syntax checks,
and git diff whitespace checks. The fixture covers original collision/render
roles, cheaper authored selection, full mode and ground alternative exclusion.
Desktop runtime tests cannot run in this Windows setup (desktop toolchain/SDL
unavailable); hardware performance and visuals remain untested. Earlier private
fixture calls were updated for lazy parser/radius signatures during syntax checks.
`vita/build/OpenUG2-full.vpk` and `.elf` preserve the prior full-detail build as
an additional rollback. The usual `OpenUG2.vpk` is the low-detail experiment.
Compare the same route and overclock settings; send all four hardware captures.

## Streaming priority follow-up (2026-10-05, after low-detail device run)

The device confirmed low detail: approximately 50-59% fewer render triangles
before dedup, with frame 600 at 17.2 FPS. This did not solve streaming. Seven
completed background builds took 8.33, 8.83, 9.02, 8.78, 6.87, 7.45 and 7.95 s.
The safety barrier fired at 112.53 s; the needed area activated at 113.39 s.
World GPU batch uploads usually took about 0.4-0.5 s (one took 1.28 s), while
object placement alone took roughly 2.5-3.4 s. Heap checkpoints reached about
182 MB, and active-area VRAM remained available. `oom.txt` was unchanged.

The next build addresses CPU preparation and loaded coverage:
- Build a 32,768-slot model-key index (~128 KB for the observed 10,698-model
  library) once and retain it with cached library metadata. Both ordinary model
  resolution and low-detail alternatives use it instead of repeatedly scanning
  the whole library. Hash collisions use linear probing; duplicate keys retain
  the first prototype exactly as the previous resolver did. Allocation failure
  uses the original linear lookup. Name fallback and missing-key policy remain.
- Increase Vita resident radius from 350 to 400 m, retaining 220 m draw radius,
  50 m safety margin and 80 m targeting cells. This actually loads more world,
  including scenery collisions and ground. Ground's existing extra 60 m margin
  remains. The barrier consequently moves from 390 to 440 m, within loaded
  ground coverage, and maximum prediction lead rises from 220 to 270 m.

This is one larger resident and one incoming replacement, not several additional
full sections. Lower GPU geometry provides headroom, but original collision CPU
geometry and textures remain; do not extrapolate triangle savings into identical
VRAM savings. The larger area may increase memory and build work. Device testing
must confirm the indexed lookups compensate and whether walls become rarer.

Validation: Vita package build, production/fixture cross-compiler syntax checks,
and whitespace checks. Added synthetic fixture coverage for hash collisions,
zero/missing/max keys, first duplicate ownership and authored LOD fallback.
Desktop fixtures cannot execute here because a native desktop compiler and SDL
are unavailable. New hardware performance remains untested. Install the usual
`vita/build/NFSU2Vita.vpk`; low world detail remains the default. Compare the fast
route with the same overclock settings and capture all four diagnostic files.

## Reuse placed bounds and omit collision prelighting (2026-10-05)

The latest device run recorded no safety-net events and activated 13 replacements
in 4.9-7.72 s (mean 6.91 s). Object placement was roughly one second after the
model-key index change. Peak live heap checkpoints were 224 MB, not 240 MB:
the boot banner's reserved heap size is not a usage measurement. Keep 400 m
coverage and target CPU preparation/memory before increasing it further.

The next build computes exact min/max XYZ while placing and validating transformed
vertices. A validity flag identifies this checked world geometry. Dedup uses the
same bounds (converted to its alternating min/max format), the XY mesh grid uses
its XY bounds, wall collection reuses XYZ bounds, and resident validation checks
bounds and indices rather than rechecking all known-finite placed positions.
Uncached geometry retains the original scans. Terrain's existing 5 cm downward
shift updates cached Z bounds; future world vertex edits must invalidate/update
the cache. Collision-only originals no longer copy vertex prelight colours.
Renderable meshes keep their colours; collision positions and indices remain.
Colour differences no longer distinguish otherwise identical collision-only
meshes during dedup, removing physically redundant copies when applicable.

The bounds metadata costs 24 bytes plus a validity flag/padding per mesh. Omitted
collision colours save four bytes per original collision vertex that had colours;
net savings and preparation speed need measurement on the Vita. Added fixture
checks for translated/rotated bounds, colour ownership and cached/uncached wall
collection equivalence. Production and fixture syntax checks and Vita build are
local validation; desktop runtime fixtures remain unavailable on this machine.
Use the usual `vita/build/NFSU2Vita.vpk` and send the same fast-route captures.

## Single-level native world DXT experiment (2026-10-05)

Native world DXT is now the next device experiment. Upstream allocator reference:
https://github.com/Rinnegatamante/vitaGL/blob/master/source/utils/gpu_utils.c
The installed SDK provides a static vitaGL library, not its source. Upstream code
shows mip > 0 can grow/reallocate compressed storage and manually copy old data
when realloc fails. This is consistent with the earlier recorded allocator crash,
but is not proof of the installed library's exact fault. Do not claim it fixed.

Vita compressed uploads now submit exactly one level with GL_LINEAR min/mag,
without compressed mip growth, mip generation or sub-block tail uploads. This
applies to car compressed uploads too. The plan accepts DXT1/3/5, power-of-two
sizes from 4 through 2048, and complete payload sizes. For textures at least
64 in both dimensions it selects the authored half-resolution mip. If that mip
is missing, dimensions are unsuitable, or RGB has already been halved, use the
existing decoded path. Explicit bounds checking prevents truncated chain reads.
Desktop compressed mip handling remains unchanged. Base allocation failure or
GL error returns failure; the world binder retries decoded RGB where possible.
Hardware faults cannot be caught this way.

World texture decode retains raw compressed data when the native mode is active.
Worker classification/noise rejection still uses the existing CPU decode. Once
mode and noise checks finish, an eligible texture discards RGB/alpha staging,
skips RGB box-halving and stages only DXT. This reduces staging lifetime and
GPU texture footprint but does NOT yet eliminate CPU DXT decoding. Do not port
Switch timing estimates directly to Vita. Without distant mips the test can
shimmer/alias more; inspect fences/trees/decals and transparency carefully.

Default: native single-level DXT where supported. For comparison/recovery put
`decoded` in `ux0:data/NFSU2/texture_mode.txt` and restart. Delete that file to
restore native mode. This setting affects world DXT; car upload uses the safe
single-level function independently. Low scenery and 400 m coverage remain.
Startup logs `world textures: DXT single level` or `decoded RGB`. Worker logs
DXT staged counts and decoded staging bytes released. Failed DXT API uploads
log decoded fallback status.

Local validation: Vita cross-build, production syntax and whitespace checks.
A GL-free startup test checks rectangular mip offsets/lengths, short payloads,
DXT1/3/5 formats, tiny/NPOT/oversized dimensions and previously halved RGB.
These assertions will execute on next Vita boot; native upload stability and
performance remain untested. The usual `vita/build/NFSU2Vita.vpk` is the experiment.

## Fixed GPU tiles and shared resident buffers (2026-10-05)

The supplied capture contains the older generic `texture path: S3TC direct` line,
but neither `world textures: DXT single level` nor `DXT staged`. It cannot verify
native world DXT. The generic extension line also appeared with world DXT disabled.
The capture does show 27 post-startup frames over 150 ms (peak 439.55 ms), average
cpu_total 55.51 ms, cpu_swap 18.40 ms, and heap checkpoints up to 214 MB. These
support investigating resident replacement work. Do not claim DXT caused the gains
or that textures cost only 16-109 ms: worker predecode checkpoints in this capture
include 0.2-1.41 s intervals. GPU upload/CPU preparation require separate timing.

This build is the first incremental GPU stage, not a completed CPU tile loader:
- Vita ordinary world batches use fixed absolute 128 m XY tile coordinates,
  including negative coordinates, instead of 256 m cells relative to the current
  scene minimum. Material grouping and 16-bit vertex ceilings still apply.
- Complete batch vertex/index payloads are fingerprinted with two 64-bit accumulators
  plus vertex/index counts. Matching live payloads reuse VBO/IBO storage; rendering
  metadata, source mesh mapping and collision indices remain per-resident. This is
  a non-cryptographic content signature; the negligible collision risk is confined
  to rendered GPU contents, not collision/ground queries. Different UVs, normals,
  prelighting, positions or indices produce different signatures.
- Each owning N2Batch holds a shared-buffer reference. Normal release, sliced
  retirement, candidate cancellation and failure use the same release helper.
  Last reference removes the cache entry and frees the buffers. Cache owns no
  extra GPU reference, so it does not retain obsolete tiles indefinitely. All
  cache and GL operations occur on the frame thread; worker CPU scenes do not
  borrow active scene pointers. Allocation failure uses independent buffers.
- Logs report `tile GPU reuse` batch counts and KB retained without upload.
  CPU packing and validation still happen for the complete resident, including
  staging used to establish payload identity. Hashing has a CPU cost and smaller
  tiles may increase batch count: measure device FPS/latency rather than assume
  70-80% savings or 1-2 s loads. CPU tile reuse remains subsequent work.
- Lookahead now immediately tracks a slower observed build and decays toward
  shorter times gradually, still clamped to the existing range and distance cap.
  A behind-target running worker can be aborted at any age when its candidate is
  unhelpful by the existing coverage/improvement predicate. Useful overlapping
  work is retained. Completed jobs remain immune to abort at the job API.

The native DXT single-level experiment remains in this executable; no larger
coverage is enabled. Build marker: `build: fixed tile GPU reuse + DXT single-level
experiment`. Startup also runs a small real-GL ownership regression: adding a
far-away tile retains both earlier buffers, cancelling a partial upload releases
its reference, retiring the first owner preserves the second, and changing UVs
forces new contents. It checks the GPU buffer sizes and final cache cleanup.
`tile GPU reuse selftest passed` confirms this executed on-device. Previous
GL-free DXT policy tests still run; native mode marker must also be present.

Local validation: production cross-compiler syntax checks, Vita package build,
and diff whitespace check. Device-only GL selftest and performance are not yet
executed. Install the newly built usual `vita/build/NFSU2Vita.vpk`, keep overclock
settings/route consistent, and capture all four diagnostic files.

### Startup crash correction (2026-10-05)
The device log stopped at the new GPU ownership selftest assertion (first upload
failed, second produced three batches). The dump main-thread PC maps to newlib
_kill_r, consistent with the assertion abort. Installed libvitaGL disassembly
confirms glPixelStorei accepts GL_UNPACK_ROW_LENGTH only; main was issuing
GL_PACK_ALIGNMENT and leaving GL_INVALID_ENUM before the test. Restrict that
pixel-alignment call to desktop, drain and log pre-existing GL errors before the
selftest, and log its two upload counts. Actual upload and ownership assertions
remain enabled. Syntax checks and package build are local validation; startup
and streaming reuse still require a new Vita run.

### Retained-buffer upload scheduling (2026-10-05)
Latest device run passed GPU reuse startup checks and activated 13 generations.
First update retained 1607 batches / 15743 KB, uploading 3449 KB, yet world upload
spanned 3.56 s. Completed update latency ranged 3.29-8.09 s. Sampled heap peaked
216 MB, free VRAM bottomed at 37 MB; perf excluding frame 0 averaged 61.70 ms,
41 frames exceeded 150 ms. No safety-net marker appeared. Six completed CPU
results and one partial GPU upload were discarded as obsolete.

Vita upload slices now count only newly uploaded batches against the batch-count
allowance. Shared-buffer hits still undergo packing/hash verification and count
against the unchanged 8 ms normal / 24 ms urgent elapsed-time budget. Desktop
counting is unchanged. Startup fixture now includes three retained tiles and two
new tiles to exercise cancellation after the first new upload, including release
of both shared references and new buffers. CPU tile ownership is still future
work; this package targets the artificial scheduling delay only. Local syntax,
whitespace and package checks pass; device checks and latency remain unverified.

### CPU scenery tile reuse, first stage (2026-10-05)
Scenery placements in pinned STREAM bundles are now cached by immutable source
record, assigned to fixed 128 m tiles. Incoming residents borrow the cached
placed geometry, skipping prototype resolution/parsing, authored LOD selection,
vertex transforms, bounds accumulation and vertex/index allocations for hits.
Per-resident mesh metadata remains separate; draw classification does not mutate
cached metadata. Only all-OTHER placement outputs are admitted: original ground,
vistas, mixed categories and the terrain height bias remain on existing paths.
Collision-only authored LOD originals are retained with their render counterpart.
Selection and source-record range filtering still run for every build, so partial
tiles and event visibility retain the existing coverage semantics.

A shared geometry owner uses atomic references across worker, active resident,
candidate, retirement and cache. World dedup, failure cleanup and sliced retirement
release references; arrays free only when the final owner leaves. Worker-packed
vertices are stored in that owner and borrowed by overlapping residents, avoiding
recomputed normals/colour packing. Batch emission frees only independently owned
staging, preserving cached packed data until its geometry owner retires.

Cache admission is limited to 24 MB including metadata, geometry and reserved
packed bytes (36 bytes per drawable vertex). Tiles outside radius + 256 m are
evicted; bundle/detail/event changes reset the cache. Unpinned bundles bypass it.
Failure to allocate cache metadata falls back to ordinary placement ownership.
Startup ownership fixture uses production lookup/eviction to check both resident
retirement while a tile remains and tile eviction while a resident remains. The
GL startup fixture also checks repeated packed borrowing and final owner release.
New boot counters: 'CPU tiles: ... placements reused ... built ... cache reserved'
and 'worker: ... packed meshes reused'. Build marker is 'CPU scenery tile reuse
+ shared packing + DXT single-level'. Ground placement, grids, collision wall
collection, batch concatenation/fingerprinting and per-resident metadata still
rebuild; this is not the completed ground/collision tile conversion.

Local validation: production cross-compiler syntax checks, instance fixture syntax,
whitespace check and Vita package build. Device startup fixtures and performance
require a new hardware run. Previous scheduling-only device run passed startup
but took 6.22-9.74 s per completed update (about 16 FPS excluding frame 0); no
speed improvement was demonstrated. Measure this stage from the new counters
and timings before expanding tile ownership further.

### Nearby scenery cache priority (2026-10-05)
The first CPU reuse run passed startup and activated 17 generations. Reuse was
real (1872 placements on first update, up to 2798 packed meshes later), but cache
reservations reached 24 MB immediately. Completed latency was 4.06-8.12 s;
first world upload still took 3.55 s. Perf excluding frame 0 averaged 54.32 ms
(18.41 FPS), 54 frames exceeded 150 ms. Route/scene differences prevent attributing
that FPS change to cache alone. Heap peaked 222/240 MB, VRAM free minimum 37 MB.

At build start, retained cache entries are sorted farthest-first using distance
from the new focus to their fixed tile centre. Full-cache admission can now
evict untouched farther entries for a materially closer incoming tile (squared
distance improvement exceeds 128*128). Entries used during the current build
are protected. New entries are not in that build's victim list, avoiding repeated
replacement of newly admitted data. The reservation cap remains 24 MB. A failed
victim-list allocation simply retains the previous no-replacement admission
behavior. Eviction releases the cache reference, preserving outstanding resident
references. Boot logs now count distant placements evicted.

Startup regression simulates full reservations using small actual payloads,
checks that a closer admission evicts the farther entry, that a farther request
cannot evict a closer remaining entry, and that an existing resident stays alive.
Ground sharing remains deferred: the current post-placement terrain height bias
mutates vertices and must be applied exactly once before sharing. This package
changes scenery cache policy only. Production and instance-fixture syntax checks,
whitespace check and package build passed; new device checks remain unexecuted.

### Shared ground prototypes (2026-10-05)
Ground now participates in the same fixed-tile CPU cache and 24 MB combined
reservation budget as scenery. Cache initialization/invalidation/eviction moves
before ground placement. Ground identities are immutable pinned prototype header
addresses, tagged separately from scenery record identities. Only complete
ROAD/TERRAIN output sets are cached; partial sets remain uncached. Cache hits
check each ground mesh's XY bounds against the current radius, so a previously
complete prototype may emit only the subset now in range without omitting meshes
when a later focus moves. Hits skip prototype parse, transforms, vertex/index
allocation and, once prepared, normal/vertex packing. Vistas and instanced ground
remain on existing placement paths. Grids and collision wall collection rebuild.

Terrain height normalization has an explicit per-mesh flag and helper: the
existing -0.05 m adjustment is applied once before ground enters shared storage.
All scene terrain is normalized before dedup, ensuring cached and fresh meshes
compare in the same coordinates. The later existing normalization pass is now
idempotent. Bounds Z is adjusted together with vertex Z; ROAD is unchanged.
Startup checks cover repeated normalization plus cached ground selection from
two bounds at different focuses, reference cleanup and own-matrix statistics.
New log counter: 'CPU tiles: ... ground meshes reused'. Build marker includes
'CPU scenery + ground reuse'. Production and instance fixture syntax checks,
whitespace check and Vita package build are local validation; device startup
checks and performance need another run.

Smaller world files remain a plausible memory/startup improvement. The loader
already stores a compact 57 MB bundle, excluding texture pixels and reading those
from the original card file on demand. Further format work should target unused
geometry/metadata or preprocessed tiles, rather than assume compression alone
will shorten streaming builds. Current resident updates use the pinned bundle
and spend most of their time constructing resident data and scheduling uploads.
Original game files and compact on-card cache format are unchanged by this build.

### Balanced cache and earlier upload urgency (2026-10-05)
Both changes are in one package. Ground reservations are capped at 8 MB and
scenery at 16 MB, within the same 24 MB total. Category accounting is updated on
admission, eviction and reset. Each category has its own cursor through the
farthest-first victim snapshot; category stored in the snapshot is checked before
accessing a possibly freed entry from the other category. Eviction cannot consume
the other category's reservation. Startup cache regression uses category limits
and checks that a full scenery reservation leaves ground's budget available and
that over-limit ground admission fails.

Normal Vita world upload slice elapsed budget rises from 8 to 12 ms; the existing
urgent 24 ms budget stays unchanged. Urgency now considers two seconds of travel
at the current speed in addition to distance from active centre, allowing fast
driving to increase upload throughput before crossing the old 280 m trigger.
Distance plus speed is deliberately conservative across turns. Actual uploads
still obey batch-count limits and elapsed time is checked between full batches;
shared GPU hits consume time but not new-upload count. Synchronous startup is
unchanged. This may trade slightly higher normal frame times for shorter resident
activation latency; hardware logs are needed before claiming a speedup.

Prior ground-sharing device run passed startup, reused 1423 of 1503 ground meshes
on first update and shortened placement ~1.01 to 0.71 s. Scenery reuse fell from
1859 to 939 placements while sharing the unpartitioned cache. Completed latency
remained 6.99-8.19 s. Safety net fired at 90.30 s, next activation 90.59 s. Heap
peaked 219 MB, VRAM free minimum 38 MB; ~18.99 FPS excluding startup. Ground plus
scenery admission now logs each category's reserved KB. Local syntax, fixture
syntax, whitespace check and package build are validation; new behavior has not
run on Vita. Build marker: balanced CPU tile cache + predictive upload urgency.

### Worker batch planning and mesh fingerprints (2026-10-05)
Worker packing now computes a two-word fingerprint over each mesh's packed
vertices and original indices. Shared geometry retains this fingerprint beside
its packed vertices, so hits do not rehash that payload. A batch fingerprint
combines ordered mesh fingerprints and mesh vertex/index lengths; lengths and
order define concatenation and index offsets. As before, the GPU cache uses two
64-bit noncryptographic hashes plus total vertex/index counts; collisions remain
a very small theoretical visual risk. Byte-identical batches with different mesh
boundaries may no longer share; source grouping remains deterministic.

Worker creates the upload job's sorted fixed-tile/material runs using logical
texture keys and CPU-known texture resolution/modes. Only small run/hash metadata
is added; no full duplicate consolidated vertex buffer. The render thread checks
all members against actual resolved GPU textures and material groups before
using the prepared runs. A mismatch or worker allocation/packing failure uses
the existing grouping path. Both paths use the same fingerprint scheme. GL
allocation, buffer lookup and reference changes remain on the render thread.
GPU reuse hits with accepted plans now skip vertex concatenation, temporary
vertex/index allocation, index remapping and full payload hashing. New uploads
still concatenate vertices/indices on the render thread. Audits use the existing
packing path so payload reports remain available. Plans cancel cooperatively
between runs; zero-GPU worker plans are released safely on abort/discard.

New boot/log markers: 'worker: batch plan prepared/fallback in ... ms',
'worker batch plan: ... prepared runs accepted', and explicit mismatch fallback.
Existing cache split, upload budgets and predictive urgency remain unchanged.
Startup ownership test compares prepared and ordinary paths for identical GPU
handles and complete batch metadata, tests changed UVs, rejects wrong material
mode and texture-group mismatch, and releases rejected CPU plans without GL
owners. Syntax and fixture syntax checks, whitespace check and Vita package build
passed locally; startup regression and performance need another hardware run.
Build marker: worker batch plans + fingerprints + balanced CPU tile cache.

Development order: establish reliable fast driving, adequate memory headroom and
acceptable loading/hitches before adding main menu and other front-end features.
Menus add UI/testing work without addressing streaming latency and should follow
stabilization of this core gameplay loop.

### Persistent boot diagnostics (2026-10-05)
Follow-up after worker plans: the first device upload improved from 2.36 to
0.53 s and overall activation from 6.52 to 5.12 s. Plans were accepted throughout,
17 generations activated without a safety-net marker; heap peak 219 MB, free
VRAM minimum 37 MB. Busy areas still showed 5-7 s loads. Timing markers themselves
still opened and closed boot.txt for every line, unlike the already persistent
stdout log handle. That adds repeated card operations to worker and activation
paths; its magnitude needs direct measurement rather than guessing from marker
spacing.

Boot probe now establishes one append descriptor and a lightweight mutex.
Markers retain direct sceIoWrite (no user-space buffering) and all timestamps,
heap/VRAM sampling and text. Concurrent marker lines are serialized; partial
writes retry remaining bytes. A failed initial open retries on a later marker.
If mutex creation fails, markers retain the old open/write/close behavior.
Normal exit closes the descriptor and deletes the mutex. Texture/geometry,
coverage, cache quotas and upload budgets are unchanged.

After each worker plan, log.txt reports 'boot diagnostics: ... markers, ... ms
total, ... ms disk writes (cumulative)'. Total includes formatting, heap/VRAM
sampling, lock wait and writes; disk timing includes open retries when necessary.
These are cumulative sums across calls/threads, not wall-clock time to subtract
blindly from build latency. Compare successive snapshots to assess diagnostic
cost. No new startup assertion is introduced for this reversible logging change.
Production syntax, whitespace and package checks passed; hardware latency and
crash-log persistence still need confirmation. Also fixed desktop conditional
compilation of the worker-only CPU texture information helper.

### Traffic lane-probe reuse at fixed resolution (2026-10-05)
First steady-FPS experiment; rendering remains 720x408. Traffic previously called
traffic_lane_tangent for its steering lookahead every physics tick, probing ground
at the target and candidate lane offsets. Vita now reuses only that lane offset
for at most five subsequent ticks. The fresh path target and heading are still
computed every tick. Refresh immediately on loaded scene mesh identity change,
target segment change, 2 m target movement, 0.5 m height change, 0.1 rad heading
change, invalid coordinates or an uninitialized cache. Heading comparison handles
angle wrap. Reused offsets are clamped to current interpolated authored road width.
Suspension, forces, steering response, collisions and car motion continue running
on the existing schedule; no cars are removed and there is no render-scale change.
Junction/segment-end checks and race-circuit AI are unchanged. Desktop retains the
old lane-probe path. Respawn initialization clears the route cache with other route
state.

Startup policy checks cover cold initialization, unchanged target, scene change,
segment transition, movement, height, heading, expired age, NaN and angle wrap.
Hardware logs add 'AI lane targeting: ... probes, ... reused (cumulative)' at
profiler frames 59, 659, etc. Build marker identifies AI lane probe reuse.
Compare cpu_ai_step and steady FPS at the same idle location, with unchanged
resolution/traffic/clock settings. Gains are unverified; rendering and GPU wait
remain separate work even if this reduces lane-targeting CPU cost. Local syntax,
whitespace and Vita package checks are validation; hardware driving behavior and
startup policy checks need a new Vita run.

### Car-relative chase camera (2026-10-05)
Latest lane-cache run reused about 82% of probes; sub-100 ms frames still
averaged 8.11 ms AI and 52.05 ms total. No clear steady-FPS win established.
User reports periodic car judder predating that change, possibly since the
catch-up camera fix. Chase smoothing previously chased the final car position
with a gain depending on the integer physics tick count, causing the trailing
gap to vary as tick counts alternate. Vita normal chase now translates camera
and look target by the player's XY displacement before easing the chase offset.
Height/suspension, heading-related swing, collision clipping, physics tick
schedule and 720x408 rendering remain unchanged. Previous follow state is reset
when entering menu, static/capture or orbit modes so transitions cannot reuse
stale displacement. Desktop behavior is unchanged. This addresses a plausible
camera cause; streaming hitches and uneven rendered frame intervals can still
cause visible judder. Hardware straight-line/corner/bump testing is required.

### Chase camera experiment reverted (2026-10-05)
Hardware feedback: car-relative camera felt much too close and worse, with
reported worse performance. Reverted the entire camera translation experiment;
restored previous tick-scaled eye and look-target easing. Removing longitudinal
lag changed the effective viewing distance significantly, so that approach is
rejected. No confirmed diagnosis of the original judder or measured cause of the
reported FPS regression. AI lane reuse and 720x408 remain. Build marker identifies
camera rollback. Investigate judder from restored baseline rather than changing
the established camera framing again.

### Bounding-box world visibility and chase distance (2026-10-05)
User confirms rollback improved judder but parked camera still too close.
Vita default chase distance increased from 4 to 6 metres; existing smoothing,
height and tick scheduling retained. Desktop remains 4 metres.
World batches now use six unnormalized view-projection planes computed once per
frame and positive-vertex AABB rejection, replacing loose enclosing spheres.
A batch is rejected only when its entire bounding box is outside one plane,
with 1 cm world-space tolerance. This is conservative for existing batch bounds
and should skip more off-screen long/flat batches without reducing visible
geometry, resolution (720x408), view distance or collision coverage. Opaque,
cutout and deferred world batches share the check. Vehicle sphere and vista
policies unchanged. Startup checks cover inside, all six outside planes,
plane-touching boxes and a box containing the frustum. FPS benefit unverified;
compare draws, cpu_world_draw, cpu_swap and total at the same location/camera.

### Chase distance adjustment (2026-10-05)
User found 6 m somewhat too far while driving. Set Vita default to 5 m, midway
between original 4 m and tested 6 m; retain chase smoothing and world AABB
visibility improvement. Latest 6 m/culling logs: sub-100 ms frames averaged
41.48 ms, world draw 9.13 ms, swap 10.75 ms, 480.5 draw calls; whole run excluding
startup averaged 46.76 ms (21.4 FPS). Runs differ in route/duration; evidence
supports a rendering improvement but is not a controlled benchmark.

### Background boot diagnostics (2026-10-05)
Target activation stalls while retaining accepted 5 m chase and 720x408 render.
Resident finish calls emitted several synchronous boot markers, each paying
memory-card write time. Latest logs show ~7 seconds cumulative boot disk writes.
Boot logging now switches after SDL initialization to a fixed 128 x 320-byte
queue (~41 KB) drained by a dedicated 64 KB-stack thread. Producers capture the
original timestamp, heap/VRAM sample and formatted message, enqueue under a
separate short-held mutex, and never acquire the disk-write mutex. Queue-full
markers are dropped and counted rather than blocking driving; diagnostics report
pending/dropped counts. Startup markers remain synchronous. Thread/mutex startup
failure retains synchronous fallback. Partial disk writes retry; shutdown drains
and joins before closing the file. An abrupt crash can lose queued tail markers;
oom.txt and stdout/log.txt paths remain synchronous and unchanged.
Marker timing now measures producer formatting/sampling/enqueue, disk timing
measures background writes separately, so these two cumulative values no longer
represent the same synchronous call interval. Heap/VRAM sampling cost remains.
Potential remaining stalls include texture-to-mesh lookup and sky/glow upload;
next hardware logs should identify how much disappears once boot writes leave
the main thread. No measured latency benefit is claimed before a Vita test.

### Async log.txt / perf.csv writes (2026-10-05)
perf.csv analysis of the 19:07 run: stream hitches were not world work. Every
printf went to log.txt as a synchronous sceIoWrite (~20 ms each on the card,
matching boot.txt's own 22 ms/write average). Stream activation prints ~7
lines in one frame (100-160 ms player_sim spikes, two per generation: texture
finalisation + activation); the 60-frame "lap ms/frame" print showed as
65-225 ms cpu_swap spikes on frames 59, 179, 359 ... log.txt now goes through a
64 KB ring drained by a "log-writer" thread (contiguous runs, many lines per
write); a full ring makes the producer wait (order kept, no drops). perf.csv
gets a 16 KB stdio buffer whose flushes take the same path (vita_async_file).
Queued tail is lost on an abrupt crash; oom.txt stays synchronous. boot.txt
marker "async log build"; log line "log queue: background, N card writes, N
producer waits".
Also: AI traffic respawn (every 30 frames, 50-150 ms when a slot is empty)
now applies the pure filters first, sorts survivors by score and runs the
ground/clearance probes nearest-first, stopping at the first accept (same
winner as the full scan). Resident mesh->texture lookup uses a sorted key
index instead of meshes x textures linear scans.

### mallinfo() out of gameplay paths (2026-10-05)
The async-log run (19:20) removed the AI spawn and lap-print spikes but section
switches still peaked at 85-134 ms. "boot diagnostics" showed ~48 ms of
producer time per queued marker: vita_mark called mallinfo(), which walks the
heap under the malloc lock. ~5 main-thread markers per stream generation (two
frames) explained the spikes; the worker's ~15 markers also held the malloc
lock against the main thread. Queued (gameplay) markers now print "brk N MB" =
sbrk(0) above the heap start (high-water mark, O(1)); startup markers keep the
mallinfo "heap" value. The 600-frame "vita: frame" line and the 4 MB ALLOC
trace also use the break. vglMemFree is a counter read (sceClibMspaceMallocStats)
and is kept. boot.txt marker "async log + brk markers build".

### Hitch profiler (2026-10-05)
brk-marker run (19:27): user reports clearly smoother section loads; perf.csv
still shows two ~90-125 ms player_sim frames per stream generation. Added
hitch_begin/hitch_mark/hitch_report (world_resident.c): the frame thread's
player_sim span is split by labelled marks (retire, stream policy, job take,
finish-step internals fin.*, activation act.*, physics ticks ...) and any
frame >= 40 ms logs one "hitch N ms: label=ms ..." line (labels < 1 ms
omitted, "rest" = after the last mark). Build marker "hitch profiler build".

### vglMemFree out of gameplay markers (2026-10-05)
Hitch profiler run (19:36): the two section-switch frames were mostly boot
markers (fin.vmark_tex/sky, fin.vmark_batches, fin.ground_check, act.vmark;
stream_tail/stream_policy frames are the "build start" marker). Per-marker
cost grew steadily from ~8 ms to ~46 ms over 16 generations: vglMemFree
(sceClibMspaceMallocStats) walks vitaGL's pool, which fragments as resident
generations come and go. Queued markers no longer sample VRAM/RAM pools; the
"vita: frame" line reports VRAM only up to frame 60. Real work left per switch
frame: tex_bind 5-10, tex_lookup 6-9, sky/glow+plan 6-11, batch_upload 5-26,
free_normals 4-6, wall_index 4-6 ms. Separate ~40-50 ms "retire" frame per
generation now split into ret.batches / ret.meshes / ret.free marks.
Build marker "no-vram markers build".

### GPU timeline, shader variants, shader cache, multicore AI (2026-10-06)

Measured with a GXM timeline (sdl_vita.c wraps sceGxmEndScene and friends; a
watcher thread timestamps vitaGL's query fence) written to perf.csv as gx_*
columns. The game was GPU-bound: GPU busy for the whole frame, the CPU
blocked in sceGxmDisplayQueueAddEntry.

- Upscale blit used the full main fragment program (8 ms of GPU); it now has
  a plain copy shader.
- The player car alone cost ~22 ms of GPU, almost all per-pixel shading in
  the uber fragment program (the car is only 6473 triangles). The main FS is
  now split into ten generated variants (lit with spec/env/fresnel bits,
  textured unlit, flat unlit; constant pow() as multiplies). vu_select() in
  sdl_vita.c picks one before every draw from the cached uniform values and
  re-sends only uniforms that changed. Car ~6 ms, HUD ~1 ms; looks identical.
- Linked programs are cached with glGetProgramBinary under
  ux0:data/NFSU2/cache/prog_<hash>.bin: shader setup at boot 8.4 s -> 75 ms.
- vita_parallel_for(): two workers pinned to cores 1 and 2 (priority above
  the streaming worker), main thread pinned to core 0. Traffic cars step in
  parallel (each car only touches itself and read-only world data); AI step
  11 -> 5 ms.
- Select+Triangle A/B modes: 0 normal, 1 shader variants off, 2 AI on one
  thread.

Result, driving with traffic: 35 ms/frame (~28 fps), from ~50 ms; parked
37 ms from 64 ms. GPU (~35 ms) is the limit again; the world is ~20 ms of it
plus ~12 ms of CPU for ~500 draws, so world batching is next.

### Best Buy advertisement texture (2026-10-06)

World material inventories and texture binding now include the optional
GLOBAL/GlobalTexturesNA.bin (GlobalTexturesROW.bin if NA is absent). The
44 KB archive supplies ADS_BESTBUY_BBH_A, previously replaced with a sibling
material on the storefront and separate sign. Existing sources retain
precedence. Vita caches source bytes, stages the logo through worker predecode,
and reuses its existing GPU texture cache across residents. There is no added
per-frame loading or lookup work. Production VPK build verified; appearance
and performance have not yet been checked on hardware.

The first ad-pack build still substituted a window texture: the generic
TPK inventory mistook GLOBALTEXTURESNA's pack metadata for a texture name
and skipped the actual logo. Inventory now reads 0x33310004 rows directly
(124-byte rows, name at +12, hash at +36). A startup self-test reproduces
that layout and boot.txt reports "regional ads: 1 keys; Best Buy inventoried"
when the source is available. Retail inventory audit also found 84 omitted
unique keys in STREAML4RA, 75 in STREAML4RD and 22 in InGameCommon; those
counts describe missing inventory entries, not confirmed visible failures.

## Patched vitaGL (vita/lib/libvitaGL.a)

The VitaSDK-installed vitaGL (Aug 2026) calls the GXM shader patcher on every
draw. vita/lib/libvitaGL.a is upstream vitaGL cdbba42 (source copy in
E:/armtest/deps/vitaGL_ug2) built like `make NO_DEBUG=1
HAVE_VERTEX_LAYOUT_CACHE=1`, with three changes in source/custom_shaders.c and
source/vgl.c:

- glVertexAttribPointer no longer invalidates the vertex layout cache when an
  attribute only switches to a different VBO (every world batch has its own
  VBO, so stock upstream missed the cache on every draw). A switch between VBO
  and client memory still invalidates it.
- vertex_layout_lookup*/vertex_layout_vbos_ok: a cached *packed* layout is only
  reused while every VBO-backed attribute shares the first attribute's VBO,
  the same rule the uncached path uses to decide packing.
- `int vgl_ug2_build` marks the build; shader binaries are cached as
  cache/prog_u_*.bin under it so the two libraries never share binaries.
- vglSetShaderAssociationPath is a no-op (installed vitaShaRK lacks the call;
  Razor only).

- vglCompressedTexImageChainUG2 (textures.c, gpu_alloc_compressed_chain_ug2 in
  utils/gpu_utils.c): uploads a whole DXT mip chain in one allocation and
  swizzles every level on the CPU. Upstream's per-level glCompressedTexImage2D
  reallocs and memcpys the buffer per level while the previous level's
  sceGxmTransferCopy can still be in flight, then frees the old buffer under
  it. render.c declares it weak, so the stock library still links and falls
  back to one level. World DXT textures use it with LINEAR_MIPMAP_NEAREST,
  chain stopped at 4x4; ux0:data/NFSU2/nomips.txt turns it off.

`VGL=stock ./vita/build_vita.sh` links the installed library instead.
boot.txt's "boot logging" line names which one is in the build.

## Car Shop (src/shop.c)

The garage's CAR SHOP row opens the game's own customization screens:
UI_ChooseCustomizeCategory / UI_ChoosePerformanceCategory /
UI_GenericParts_Browser / UI_ChooseRimBrand / UI_Rims_Browser from
FRONTEND/FrontB.lzc, drawn with their own textures and the front-end font
(GLOBAL/GLOBALA.BUN). Menu order, icons and the icon-scroller constants come
from SPEED2.EXE; text from LANGUAGES/English.bin; part names, levels and
model links from the GLOBALB car part database (0x80034602, layout per
SpeedReflect's Nikki); prices from the GLOBALB career unlock table (0x34A1C).
First use writes ux0:data/NFSU2/cache/shop_v2.bin. Fitted parts and rims
are saved per car in ux0:data/NFSU2/garage.txt and restored on load.
Working: Body Shop bumpers, skirts, spoilers, hoods, roof scoops, lights,
exhaust tips, rims. Locked (shown as the game shows locked items): mirrors,
carbon fibre, wide body, Performance, Graphics and Car Specialties.
