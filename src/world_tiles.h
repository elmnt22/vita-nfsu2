#ifndef OPENUG2_WORLD_TILES_H
#define OPENUG2_WORLD_TILES_H

/* Prebuilt world tiles (PS Vita). The streaming build places, merges and
 * packs a whole 800 m neighbourhood every few seconds; the original game
 * instead streams sections that are ready as they come off the disc. The
 * converter runs the same build once per 512 m cell over the whole city and
 * writes every placed mesh into the 128 m tile its centre lies in, so a
 * later loader only reads the tiles that come into range.
 *
 * Generated on the console from the player's own game files; nothing of the
 * game is shipped. Format version bumps force a reconversion. On the card:
 * <track>_vN.idx (the index) + <track>_vN.pak (every tile file of the track
 * in one pack: a file open costs ~270 ms on the card). Older caches with one
 * <track>_x_y.bin per tile still load, and are packed in the background. */

#define WTILE_SIZE     128.0f
#define WTILE_CELL     4          /* tiles per conversion cell side (512 m) */
#define WTILE_VERSION  7   /* 2: far full models keep their prelight; 3: 1 mm grid deltas + LZ4;
                                    4: road reflection flag per mesh;
                                    5: that flag read with the right bit (0x40);
                                    6: n2_skip_filler keeps a 0x11 first vertex byte (L4RA road hole at -1200,275);
                                    7: 36-byte-vertex solids (0x134900 +0xc & 0x4000: XS_ signs) read at their stride (black triangles) */
#define WTILE_BEYOND_VERSION 6   /* the World Expansion tile set (BeyondDev b6tiles): its own version, see wt_ver */
#define WTILE_DIR      "ux0:data/NFSU2/cache/tiles"

typedef struct {
    volatile int cells_done, cells_total, cells_failed;
    volatile int tiles_written;
    volatile long long bytes, meshes, tris;
    volatile int done, ok;
    volatile unsigned start_ms, now_ms;
} WTileProgress;

/* 1 when a complete tile set of this version exists for the track. */
int world_tiles_ready(const char *trackname);
#include "nfsu2.h"
#include "world_instance.h"
#include <SDL.h>
/* 1 when the world should load from tiles (a tile set exists and
   ux0:data/NFSU2/notileload.txt does not). */
int world_tiles_enabled(const char *trackname);
/* Fills scene (sky + world) and vista for a neighbourhood from the tiles,
   applying the event's scenery selection and the near/far LOD choice.
   Returns the scene mesh count, 0 on failure (caller builds as before). */
int world_tiles_assemble(N2Scene *scene, N2Scene *vista, const char *troot, const char *track,
                         float fx, float fy, float radius, int scenery_event, WInstStats *stats);
/* Where the next build will probably be centred (any thread, per frame):
   the read-ahead fetches tile files around it while the loader is idle. */
void world_tiles_aim(float x, float y);
/* 1: free the read-ahead tiles and decode scratch and pause the read-ahead
   (before a front end over the world); 0: resume. */
void world_tiles_trim(int pause);
/* (Codex CR15) an area assembled with generation gen (WInstStats.tile_vis_gen)
   was thrown away unseen: the fade / LOD history goes back to the area that is
   on screen. A no-op for any other generation or once another assembly began. */
void world_tiles_visibility_undo(unsigned gen);
/* A track switch in the running game: drop everything kept for the old
   track (index, loaded and read-ahead tiles, global tile, scenery
   selection) so the next build reads the new one's. The read-ahead thread
   and a world_tiles_preload for the next track stay. Frame thread, with
   no build running. */
void world_tiles_reset(void);
/* Converts the whole track into ux0:.../tiles/<track>_vN.pak (one file per
   track) + its index; fills progress as it goes (any thread). */
int world_tiles_convert(const char *troot, const char *trackname, WTileProgress *progress);
/* Aim the read-ahead at the next race's start while the player is in the
   menus: the stored bytes of the tiles a build at (x, y) on `track` (any
   track, also not the current one) will read are fetched into a bounded
   cache (16 MB; 8 MB while trimmed) that the next assembly of that track
   takes instead of the card. Same request again: no-op. NULL cancels. Any
   thread; starts the read-ahead thread if needed. */
void world_tiles_preload(const char *track, float x, float y);
/* Packs caches of loose per-tile files (older conversions) into one file per
   track on a low-priority thread that keeps off the card while the world
   streams, then deletes the loose files. Once per run; the first successful
   assembly starts it, calling it earlier is harmless. */
void world_tiles_migrate_start(void);
void world_tiles_drop_old_versions(void);   /* older WTILE_VERSION sets, once the current one is complete */
/* Boot: pack every track's loose tile files on the calling thread, progress
 * in p (cells = tiles copied + files removed); needed = any loose file left;
 * track = the one being packed now (for the screen). */
int world_tiles_migrate_needed(void);
int world_tiles_migrate_sync(WTileProgress *p);
const char *world_tiles_migrate_track(void);
/* Every stand-in (world role 2) at least minh tall in the track's tile set,
   one tile at a time (any thread; skyline.c). Returns tiles read, -1 no set. */
/* GitHub #40: set by worldanim.c while a track's world animations are loaded:
   1 when `solid` (a bin hash) is one of its animated models. The placement of
   such a model (flag 0x10, in no scenery group: the pub's windmill sails,
   cranes, planes, garage doors, spotlights) is the animation's source, drawn
   by worldanim.c -- as static scenery it was a frozen duplicate. */
extern int (*g_wt_anim_solid)(uint32_t solid);
int world_tiles_each_standin(const char *track, float minh,
                             void (*cb)(void *ctx, const N2Mesh *m), void *ctx,
                             SDL_atomic_t *abort_flag);

#endif
