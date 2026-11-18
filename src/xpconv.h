/* xpconv -- Most Wanted (2005) / Carbon world streams -> Underground 2's format.
   Shared by the Expansion Prep Tool (PC) and the game's on-device conversion. */
#ifndef XPCONV_H
#define XPCONV_H
/* bumped whenever the translated output changes: an older marker = translate again */
#define XP_VERSION 1
enum { XP_GAME_MW = 1, XP_GAME_CARBON = 2 };
enum { XP_OK = 0, XP_ERR_OPEN_IN, XP_ERR_OPEN_OUT, XP_ERR_READ, XP_ERR_WRITE,
       XP_ERR_FORMAT, XP_ERR_MEMORY, XP_ERR_CANCELLED };
typedef struct {
    long solids, groups, verts, tris, dropped, infos, insts, streams_multi, bad;
    long effects[32];
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
#endif
