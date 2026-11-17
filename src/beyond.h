/* beyond.h -- the optional "Beyond" world (the World Expansion, BeyondDev/).
 *
 * Dormant unless the player converted their own copy of the map and put the
 * files on the card:
 *   ux0:data/NFSU2/beyond/enable.txt            the switch (any content)
 *   ux0:data/NFSU2/beyond/BEYOND.ug2x           texture pack (BeyondDev converter/ug2x.h)
 *   ux0:data/NFSU2/beyond/minimap.bin           HUD minimap (beyondconv --minimap, hud_set_trackmap_file)
 *   ux0:data/NFSU2/beyond/spawn.txt             "x y heading_deg" per line, first = the Free Run start
 *   ux0:data/NFSU2/cache/tiles/STREAMBEYOND_v6.idx/.pak  the world tiles (the game's own tile format)
 * With all of them there, Quick Race -> Free Run asks "Bayview" or "World
 * Expansion"; the second drives the Beyond world (track STREAMBEYOND).
 * Without them nothing changes. */
#ifndef OPENUG2_BEYOND_H
#define OPENUG2_BEYOND_H
#include <stdint.h>
#include "nfsu2.h"

#define BEYOND_TRACK "STREAMBEYOND"

int beyond_enabled(void);                      /* the switch and every file present (checked once) */
int beyond_is_track(const char *trackname);    /* "STREAMBEYOND" or "BEYOND" */
extern int g_beyond_pick;                      /* Quick Race Free Run: 1 = the player chose the World Expansion */
/* key = FNV-1a 32 of the map's texture name (the tiles' texkey). Fills the
   DXT chain (base level down to 4x4) + a decoded rgb/alpha copy and the draw
   mode fields; 0 when the pack has no such key. Thread safe. */
int beyond_tex_decode(uint32_t key, N2Tex *t);
int beyond_spawn(float xy[2], float *heading_rad);   /* the first spawn.txt line */
/* textures Beyond borrows from the city (its sky): kept as copies taken while
   the city was loaded; beyond_tex_decode falls back to them */
void beyond_keep_tex(uint32_t key, const N2Tex *t);
void beyond_close(void);                       /* the pack's file handle + table (track switch away) */
/* where the race data of region `reg` (a track or a ROUTES stem) lives: the
   Beyond folder (its ROUTESBEYOND/, written by BeyondDev tools/events) for
   Beyond when it is on, else `troot` unchanged */
const char *beyond_troot(const char *troot, const char *reg);
/* its race events (ROUTESBEYOND/events.txt from the BeyondDev tools): ids
   7001+, type as airace.h (AIRACE_CIRCUIT ...), length in metres, a name;
   none unless beyond_enabled() */
int beyond_event_count(void);
int beyond_event_at(int i, int *id, int *type, int *len_m);
int beyond_event_type(int id);                 /* -1: not a Beyond event */
int beyond_event_start(int i, float p[3], float *head);   /* event i's free-roam start (starts.txt); 0: none */
const char *beyond_event_name(int id);         /* NULL: not a Beyond event */
unsigned char *beyond_trackmap(int id, int *w, int *h);   /* its Quick Race map, RGBA (free it); NULL: none */
#endif
