/* conv_xp.h -- the Prep Tool's World Expansion step (conv_xp.c): Rockport from
 * Need for Speed Most Wanted (2005), Palmont from Need for Speed Carbon. */
#ifndef CONV_XP_H
#define CONV_XP_H
#include <windows.h>
#include "world_tiles.h"

#define XPP_NCITY 2                 /* 0 = Rockport (Most Wanted), 1 = Palmont (Carbon) */
#define XPP_FILES_PER_CITY (12 + 400)

typedef struct {
    volatile int checking;
    volatile long long chk_bytes, chk_total;
    volatile int cur, ncur, todo, phase;   /* phase 1 = translating, 2 = world tiles */
    volatile long long xp_done, xp_total;
    WTileProgress prog;
    volatile DWORD start;
    char msg[512], extra[128];
} XppJob;

const char *xpp_city_name(int c);              /* "Rockport" */
const char *xpp_game_name(int c);              /* "Need for Speed Most Wanted" */
const char *xpp_bun(int c);                    /* "STREAML2RA" */
void xpp_set_out(const char *out);             /* the Prep Tool's output folder */
void xpp_set_dir(int c, const char *dir);      /* "" = leave the city out */
int  xpp_any_dir(void);
int  xpp_dir_fix(int c, char *dir);            /* 1 = holds the city's TRACKS (dir trimmed to the game folder) */
int  xpp_dir_detect(int c, char *out, size_t cap);   /* from the installer's registry entry */
int  xpp_city_done(int c);                     /* converted and ready to send */
/* 1 = every file of the picked games matches, 0 = some don't, -1 = no game picked */
int  xpp_check(volatile long long *bytes, volatile long long *total, char *msg, size_t cap);
/* converts every picked city not done yet; 1 = ok. ask_on_bad: ask (owner's message
   box) before going on with files that don't match */
int  xpp_convert(XppJob *job, int ask_on_bad, HWND owner);
/* adds each ready city's files (local path, Vita path) in send order; add returns 1
   when it took the file (it exists). Returns how many were taken. */
typedef int (*XppSendFn)(void *ud, const char *local, const char *remote);
int  xpp_send_files(XppSendFn add, void *ud, int cap);
#endif
