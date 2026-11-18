/* xpconv -- Most Wanted (2005) / Carbon world streams -> Underground 2's format.
   Shared by the Expansion Prep Tool (PC) and the game's on-device conversion. */
#ifndef XPCONV_H
#define XPCONV_H
/* bumped whenever the translated output changes: an older marker = translate again */
#define XP_VERSION 4   /* 2: collision packs (11-17), 3: night prelight + lamp flares (11-18), 4: endless walls fitted to the ground (11-18) */
enum { XP_GAME_MW = 1, XP_GAME_CARBON = 2 };
enum { XP_OK = 0, XP_ERR_OPEN_IN, XP_ERR_OPEN_OUT, XP_ERR_READ, XP_ERR_WRITE,
       XP_ERR_FORMAT, XP_ERR_MEMORY, XP_ERR_CANCELLED };
typedef struct {
    long solids, groups, verts, tris, dropped, infos, insts, streams_multi, bad;
    long effects[32];
    long lights;   /* generated night lamp flares */
    long cpacks, cpolys, ctris, cwalls, cgrouped, crot, cempty, coverflow, ctall;   /* collision (ctall: endless walls fitted) */
    long long in_bytes, out_bytes;
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
#endif
