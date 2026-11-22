/* xpconv -- Most Wanted (2005) / Carbon world streams -> Underground 2's format.
   Shared by the Expansion Prep Tool (PC) and the game's on-device conversion. */
#ifndef XPCONV_H
#define XPCONV_H
/* bumped whenever the translated output changes: an older marker = translate again */
/* the oldest translation each game still takes (V4 11-22: 12 changed only the Rockport relight) */
#define XP_MIN_MW     12
#define XP_MIN_CARBON 11
#define XP_VERSION 12   /* 12: brighter Rockport night relight, clamped (11-19), 11: Carbon vertex orders for effects 5/25/26 (11-19, rainbow building fronts), 10: per-submesh vertex compaction (11-18), 9: caches rebuilt for TRN_PANO as backdrop (11-18), 8: chunks padded to 4 bytes (11-18: odd index counts misaligned ~11k objects per city), 2: collision packs (11-17), 3: night prelight + lamp flares (11-18), 4: endless walls fitted to the ground (11-18), 5: road curve tangents (11-18, RoutesFreeRoam rebuilt), 6: race barrier groups (11-18: polygon group ids, companion group tables), 7: zero-padded barrier names */
enum { XP_GAME_MW = 1, XP_GAME_CARBON = 2 };
enum { XP_OK = 0, XP_ERR_OPEN_IN, XP_ERR_OPEN_OUT, XP_ERR_READ, XP_ERR_WRITE,
       XP_ERR_FORMAT, XP_ERR_MEMORY, XP_ERR_CANCELLED };
typedef struct {
    long solids, groups, verts, tris, dropped, infos, insts, streams_multi, bad;
    long effects[32];
    long lights;   /* generated night lamp flares */
    long cpacks, cpolys, ctris, cwalls, cgrouped, crot, cempty, coverflow, ctall;   /* collision (ctall: endless walls fitted) */
    long long in_bytes, out_bytes;
    long relaid;   /* Carbon vertices moved from their other orders */
} XpStats;
/* return nonzero to cancel */
typedef int (*XpProgress)(void *ud, long long done, long long total);
int xp_convert(const char *in_path, const char *out_path, int game,
               XpProgress progress, void *ud, XpStats *stats);
/* The marker next to a translated stream (STREAMxxx.xp): "xpconv VERSION GAME SOURCE_SIZE".
   xp_marker_ok: 1 when it is there and matches this translator + source (source_size < 0:
   any source, for a city sent from the PC tool without its raw files). */
int xp_marker_write(const char *marker_path, int game, long long source_size);
int xp_marker_ok(const char *marker_path, int game, long long source_size);
long long xp_file_size(const char *path);
/* The city's minimap (MW: TRACKS/L2RA/MINI_MAP.BIN) -> the game's BMAP file
   (hud_set_trackmap_file): 1024 px RGBA + its world window. */
int xp_minimap(const char *mini_map_bin, const char *out_path, int game);
/* The city's free-roam road net (xproads.c): MW L2RA.BUN chunk 0x3b800 -> RoutesFreeRoam.bin
   for TRACKS/ROUTES<region> (airacer.c ainet_load). */
typedef struct { long lanes, dead, sections, points, links, dropped; } XpRoadStats;
int xp_routes(const char *companion_bun, const char *out_path, int game, XpRoadStats *stats);
/* The city's companion stream (MW TRACKS/L2RA.BUN): copied with MW's scenery group
   tables rewritten as UG2's (placements 0x34107, groups 0x34108, collision groups 0x3412f). */
int xp_companion(const char *in_path, const char *out_path, int game);
/* The city's race events (xpevents.inc, port of ExpansionDev/events/mkevents.py): MW
   gameplay.bin races routed over the road net -> out_dir/ Paths<id>.bin, Routes<id>F.bin,
   events.txt, starts.txt, barriers.txt; ids from first_id, city names the event labels.
   Needs the raw MW files: gameplay.bin, the companion (L2RA.BUN) and the stream
   (STREAML2RA.BUN, for the race barrier walls). */
typedef struct { long races, events, circuits, sprints, drags, drifts, fallback, through, skipped, dropped,
                 barriers_on, barriers_off, barrier_segs, shops; } XpEventStats;
int xp_events(const char *gameplay_bin, const char *raw_companion, const char *raw_stream,
              const char *out_dir, int game, const char *city, int first_id, XpEventStats *stats);

/* The city's smackable props -> smokeables (xpprops.inc): one bundle with hulls, infos,
   placements and spawner packs, read by smk_load in place of the city's own (11-18). */
typedef struct { long mw, props, infos, packs, other_kind, no_model, too_big, bad; } XpPropStats;
int xp_props(const char *raw_companion, const char *raw_stream, const char *out_path, int game, XpPropStats *stats);
#endif
