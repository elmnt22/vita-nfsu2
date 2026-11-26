/* xpcity.c -- the expansion cities on the console, from the games' raw files.
 *
 * Rockport (Most Wanted 2005) and Palmont (Carbon): the player copies the games'
 * raw files to ux0:data/NFSU2/expansion/<mw|carbon>/ (TRACKS/ + GLOBAL/) and
 * nothing else -- no PC tool (11-25). The retail streamer (rstream.c) draws the
 * raw stream, each section translated as it loads; the first boot into the city
 * makes the small files (companion, collision, map, roads, races, props) here,
 * on the game's loading screen. The old way stays readable: a city translated
 * into UG2's format (xpconv.c) by the Expansion Prep Tool, with its marker
 * (TRACKS/<stream>.xp) and tiles, is still used when its raw files are not there. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <sys/stat.h>
#include <SDL.h>
#include "xpconv.h"
#include "xpcity.h"
#include "skyline.h"
#include "beyond.h"
#include "world_tiles.h"
#include "rstream.h"

void vita_mark(const char *fmt, ...);
void vita_fast_clocks(void);
void (*xpcity_screen)(const char *line1, const char *line2, const char *line3);
void (*xpcity_screen_done)(void);
static int g_drew;   /* the loading screen shows this city's lines (put back at the end) */
static void city_screen(const char *l1, const char *l2, const char *l3)
{
    if (!xpcity_screen) return;
    xpcity_screen(l1, l2, l3); g_drew = 1;
}
static void city_screen_done(void)
{
    if (g_drew && xpcity_screen_done) xpcity_screen_done();
    g_drew = 0;
}

#define XP_RAW_ROOT "ux0:data/NFSU2/expansion"
#define XP_SWITCH   XP_RAW_ROOT "/enable.txt"   /* the switch (Settings ticked on, or the old Prep Tool) */
#define XP_OFF      XP_RAW_ROOT "/off.txt"      /* Settings ticked off: dropped-in raw files stay hidden */
#define XP_CACHE    "ux0:data/NFSU2/cache"

static const struct { const char *bun, *companion, *dir, *name; int game; } k_city[] = {
    { "STREAML2RA", "L2RA", "mw",     "ROCKPORT", XP_GAME_MW },
    { "STREAML5RA", "L5RA", "carbon", "PALMONT",  XP_GAME_CARBON },
};
#define NCITY ((int)(sizeof k_city / sizeof *k_city))

static int city_of(const char *track) {
    if (!track) return -1;
    for (int c = 0; c < NCITY; c++)
        if (!strncmp(track, k_city[c].bun, strlen(k_city[c].bun))) return c;
    return -1;
}
int xpcity_is_city(const char *track) { return city_of(track) >= 0; }
int xpcity_default_focus(const char *track, float xy[2], float *heading_deg) {
    /* Palmont: road-net node 1871 on the downtown freeway, facing along it to node 1872
       (port choice, checked in the sim); Rockport keeps its authored focus */
    if (city_of(track) != 1) return 0;
    xy[0] = 3614.6f; xy[1] = 415.4f;
    if (heading_deg) *heading_deg = -30.0f;
    return 1;
}
/* start point when no focus cache exists: the authored focus the sim derived
   (Rockport 204.6,-143.3; Palmont 706.7,339.7 -- Palmont's free roam then
   moves to xpcity_default_focus) */
int xpcity_fixed_focus(const char *track, float xy[2]) {
    int c = city_of(track);
    if (c == 0) { xy[0] = 204.65f; xy[1] = -143.31f; return 1; }
    if (c == 1) { xy[0] = 706.73f; xy[1] = 339.70f; return 1; }
    return 0;
}
const char *xpcity_region(const char *track) { int c = city_of(track); return c >= 0 ? k_city[c].bun + 6 : NULL; }

static char g_bar_region[16] = "";   /* the race barriers' region (read below); "" = read again */

static struct { volatile long long done, total; volatile int finished, rc; const char *in, *out; int game; } g_job;
static int job_progress(void *ud, long long done, long long total) {
    (void)ud; g_job.done = done; g_job.total = total; return 0;
}
static int g_job_lean;
static int job_thread(void *ud) {
    (void)ud;
    XpStats st;
    if (g_job_lean) {
        g_job.rc = xp_lean(g_job.in, g_job.out, g_job.game, job_progress, NULL, &st);
        vita_mark("xpcity: lean file %s rc %d: %.1f MB read, %.1f MB written, %ld collision packs (%ld polygons), %ld lamp flares",
                  g_job.out, g_job.rc, st.in_bytes / 1048576.0, st.out_bytes / 1048576.0, st.cpacks, st.cpolys, st.lights);
        g_job.finished = 1;
        return 0;
    }
    g_job.rc = xp_convert(g_job.in, g_job.out, g_job.game, job_progress, NULL, &st);
    vita_mark("xpcity: translated %s rc %d: %.1f -> %.1f MB, %ld solids, %ld bad, %ld instances",
              g_job.in, g_job.rc, st.in_bytes / 1048576.0, st.out_bytes / 1048576.0, st.solids, st.bad, st.insts);
    g_job.finished = 1;
    return 0;
}
static int copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"); if (!a) return 0;
    FILE *b = fopen(to, "wb"); if (!b) { fclose(a); return 0; }
    static char buf[256 << 10]; size_t n; int ok = 1;
    while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
    fclose(a);
    if (fclose(b) != 0) ok = 0;
    if (!ok) remove(to);
    return ok;
}

/* the minimap (BMAP, TRACKS/<companion>.MAP): from the PC tool, or made here
   from the raw TRACKS/<companion>/MINI_MAP.BIN (a second; no map = no radar) */
static char g_map[1024];
static void make_map(int c, const char *troot)
{
    char raw[256];
    snprintf(g_map, sizeof g_map, "%s/%s.MAP", troot, k_city[c].companion);
    char sea[1100]; snprintf(sea, sizeof sea, "%s/%s.SEA", troot, k_city[c].companion);
    char area0[1100]; snprintf(area0, sizeof area0, "%s/%s_A0.MAP", troot, k_city[c].companion);
    if (xp_file_size(g_map) > 0 && (k_city[c].game != XP_GAME_CARBON || (xp_file_size(sea) > 0 && xp_file_size(area0) > 0))) return;   /* Carbon: + its sea mask, its areas' maps (11-19) */
    snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s/MINI_MAP.BIN", k_city[c].dir, k_city[c].companion);
    if (k_city[c].game == XP_GAME_CARBON)   /* no map art for the city: drawn from its road collision */
        snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    int rc = xp_file_size(raw) > 0 ? xp_minimap(raw, g_map, k_city[c].game) : XP_ERR_OPEN_IN;
    vita_mark("xpcity: minimap %s: %s", g_map, rc == XP_OK ? "made" : "none (no raw MINI_MAP.BIN / stream)");
    if (rc == XP_OK) {   /* a race map drawn on the old one: gone (xpcity_race_map makes it again) */
        char side[1100], op[1100]; int old = -1;
        snprintf(side, sizeof side, "%s/%s_RACE.txt", troot, k_city[c].companion);
        FILE *sf = fopen(side, "r");
        if (sf) { if (fscanf(sf, "%d", &old) != 1) old = -1; fclose(sf); }
        if (old >= 0) { snprintf(op, sizeof op, "%s/%s_RACE%d.MAP", troot, k_city[c].companion, old); remove(op); }
    }
}
/* the free-roam road net (TRACKS/ROUTES<companion>/RoutesFreeRoam.bin): from the PC tool, or
   made here from the companion bundle's road network (xproads.c, well under a second) */
static void make_routes(int c, const char *troot)
{
    char dir[256], out[300], bun[256];
    snprintf(dir, sizeof dir, "%s/ROUTES%s", troot, k_city[c].companion);
    snprintf(out, sizeof out, "%s/RoutesFreeRoam.bin", dir);
    if (xp_file_size(out) > 0) return;
    mkdir(dir, 0777);
    snprintf(bun, sizeof bun, "%s/%s.BUN", troot, k_city[c].companion);
    XpRoadStats st;
    int rc = xp_routes(bun, out, k_city[c].game, &st);
    vita_mark("xpcity: road net %s: %s (%ld sections, %ld links)", out, rc == XP_OK ? "made" : "failed", st.sections, st.links);
}
/* the city's race events (xp_events): once, from the raw MW files (gameplay.bin, the
   companion, the stream's barrier walls); the Prep Tool sends them ready made */
/* xp_events' route search on several cores (xp_parallel): fn(arg, 1..n-1) on threads, 0 here */
typedef struct { void (*fn)(void *, int); void *arg; int wi; } XpPar;
static int xp_par_thread(void *ud) { XpPar *p = (XpPar *)ud; p->fn(p->arg, p->wi); return 0; }
static void xp_par_sdl(void (*fn)(void *, int), void *arg, int n)
{
    XpPar P[8]; SDL_Thread *t[8] = {0};
    if (n > 8) n = 8;
    for (int i = 1; i < n; i++) { P[i].fn = fn; P[i].arg = arg; P[i].wi = i; t[i] = SDL_CreateThread(xp_par_thread, "xpev", &P[i]); }
    fn(arg, 0);
    for (int i = 1; i < n; i++) {
        if (t[i]) SDL_WaitThread(t[i], NULL);
        else fn(arg, i);   /* no thread: that worker's share here (the jobs are taken from one counter) */
    }
}
static void make_events(int c, const char *troot)
{
    char dir[256], ev[300], gp[256], rawc[256], raws[256];
    snprintf(dir, sizeof dir, "%s/ROUTES%s", troot, k_city[c].companion);
    snprintf(ev, sizeof ev, "%s/shops.txt", dir);                    /* a set from before the shops / track maps: made again */
    int have_shops = xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/trackpoly.bin", dir);
    have_shops = have_shops && xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/names.txt", dir);                    /* + the race names (11-19: MW's race maps) */
    have_shops = have_shops && xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/mmroute.bin", dir);                  /* + Carbon's minimap routes (11-19) */
    have_shops = have_shops && xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/events.txt", dir);
    int have = have_shops && xp_file_size(ev) > 0;
    if (have && xp_events_current(dir)) return;
    snprintf(gp, sizeof gp, XP_RAW_ROOT "/%s/GLOBAL/gameplay.bin", k_city[c].dir);
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(raws, sizeof raws, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    if (xp_file_size(gp) <= 0 || xp_file_size(rawc) <= 0 || xp_file_size(raws) <= 0) {
        if (have) vita_mark("xpcity: %s race events are an older set (no raw files to make them again: copy them to expansion/)", k_city[c].name);
        else vita_mark("xpcity: no race events for %s (no raw gameplay.bin / %s / %s)", k_city[c].name, k_city[c].companion, k_city[c].bun);
        return;
    }
    if (have) remove(ev);   /* WW2: an older set -- made again (a failed run leaves none, as before) */
    mkdir(dir, 0777);
    Uint32 t0 = SDL_GetTicks();   /* (the bar: prep_step) */
    XpEventStats st;
    xp_ticks = SDL_GetTicks;   /* its step times in the log */
    xp_parallel = xp_par_sdl; xp_workers = 3;   /* 11-24: the route search on the Vita's three cores */
    /* event ids: Rockport 7301 on, Palmont 7601 on (beyond.c takes 4000-7999) */
    int rc = xp_events(gp, rawc, raws, dir, k_city[c].game, k_city[c].name[0] == 'R' ? "Rockport" : "Palmont",
                       k_city[c].game == XP_GAME_CARBON ? 7601 : 7301, &st);
    if (rc != XP_OK) remove(ev);
    else { beyond_events_reload(); g_bar_region[0] = 0; }   /* XF2: the new set (and barriers), not none until a restart */
    vita_mark("xpcity: race events %s: %s in %.1f s (%ld races -> %ld events: %ld circuits %ld sprints %ld drags, %ld skipped, "
              "%ld dropped; barriers %ld on %ld off)", dir, rc == XP_OK ? "made" : "FAILED", (SDL_GetTicks() - t0) / 1000.0,
              st.races, st.events, st.circuits, st.sprints, st.drags, st.skipped, st.dropped, st.barriers_on, st.barriers_off);
}
/* the city's smackable props -> TRACKS/XPSMK<companion>.BUN (xpprops.inc), from the raw files */
static void make_props(int c, const char *troot)
{
    char out[300], rawc[256], raws[256];
    snprintf(out, sizeof out, "%s/XPSMK%s.BUN", troot, k_city[c].companion);
    if (xp_file_size(out) > 0) return;
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(raws, sizeof raws, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    if (xp_file_size(rawc) <= 0 || xp_file_size(raws) <= 0) {
        vita_mark("xpcity: no props for %s (no raw %s / %s)", k_city[c].name, k_city[c].companion, k_city[c].bun);
        return;
    }
    Uint32 t0 = SDL_GetTicks();   /* (the bar: prep_step) */
    XpPropStats st;
    int rc = xp_props(rawc, raws, out, k_city[c].game, &st);
    vita_mark("xpcity: props %s: %s in %.1f s (%ld MW smackables -> %ld props, %ld infos, %ld packs; scenery: %ld other kinds, "
              "%ld too big, %ld no model, %ld bad)", out, rc == XP_OK ? "made" : "FAILED", (SDL_GetTicks() - t0) / 1000.0,
              st.mw, st.props, st.infos, st.packs, st.other_kind, st.too_big, st.no_model, st.bad);
}
/* 11-24: the first boot's steps on the game's loading screen, with how far along they are
   all together: each step on a worker while this thread draws; a..b is the step's share
   (the Vita's times, hwlog96: reading 22 s, map 2, road net 10, races 90, props 41) and
   xp_frac how far it is. No time left shown (not always right, user 11-24). A step that
   is done already (every later boot) shows nothing. */
typedef struct { void (*fn)(int, const char *); int c; const char *troot; volatile int done; } PrepJob;
static int prep_thread(void *ud) { PrepJob *j = (PrepJob *)ud; j->fn(j->c, j->troot); j->done = 1; return 0; }
static const char *g_prep_city = "";
static void prep_bar(const char *line, float frac)
{
    char a[64], b[64];
    snprintf(a, sizeof a, "PREPARING %s", g_prep_city);
    snprintf(b, sizeof b, "%d%%  -  ONE TIME ONLY", (int)(frac * 100.0f));
    city_screen(a, line, b);
}
static void prep_step(void (*fn)(int, const char *), int c, const char *troot, const char *line, float a, float b, int live)
{
    if (!xpcity_screen) { fn(c, troot); return; }
    static PrepJob j;
    j.fn = fn; j.c = c; j.troot = troot; j.done = 0;
    xp_frac = 0.0f;
    g_prep_city = k_city[c].name;
    SDL_Thread *t = SDL_CreateThread(prep_thread, "xpprep", &j);
    if (!t) { fn(c, troot); return; }
    Uint32 t0 = SDL_GetTicks();
    while (!j.done) {
        if (g_drew || SDL_GetTicks() - t0 > 300) {   /* not for a step with nothing to do */
            float f = live ? xp_frac : 0.0f; if (f < 0) f = 0; if (f > 1) f = 1;
            prep_bar(line, a + (b - a) * f);
        }
        SDL_Delay(100);
    }
    SDL_WaitThread(t, NULL);
    if (g_drew) prep_bar(line, b);
}
static void prep_rest(int c, const char *troot)
{
    prep_step(make_map, c, troot, "DRAWING THE MAP", 0.13f, 0.145f, 0);
    prep_step(make_routes, c, troot, "BUILDING THE ROADS", 0.145f, 0.21f, 0);
    prep_step(make_events, c, troot, "BUILDING THE RACES", 0.21f, 0.755f, 1);
    prep_step(make_props, c, troot, "PLACING THE PROPS", 0.755f, 1.0f, 1);
}
const char *xpcity_map(const char *track)
{
    int c = city_of(track);
    return c >= 0 && g_map[0] && strstr(g_map, k_city[c].companion) ? g_map : NULL;
}

/* the map holding (x, y): the city's, or one of the areas apart from it (Carbon's west
   district, canyons, drift tracks: <map>_A<k>.MAP, xpconv minimap_carbon), read once */
static int map_box(const char *path, float box[3])   /* x0, y0 (top), size */
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    uint8_t h[28]; int ok = fread(h, 1, 28, f) == 28 && !memcmp(h, "BMAP", 4);
    fclose(f);
    if (ok) memcpy(box, h + 12, 12);
    return ok && box[2] > 0;
}
const char *xpcity_map_at(const char *track, float x, float y)
{
    const char *m = xpcity_map(track);
    if (!m) return NULL;
    static char base[1024], out[1100]; static int na = -1, last = -1; static float mb[3], ab[64][3];
    if (na < 0 || strcmp(base, m)) {
        snprintf(base, sizeof base, "%s", m); na = 0;
        if (!map_box(m, mb)) mb[2] = 0;
        const char *dot = strrchr(m, '.'); int bl = dot ? (int)(dot - m) : (int)strlen(m);
        for (int a = 0; a < 64; a++) {
            snprintf(out, sizeof out, "%.*s_A%d.MAP", bl, m, a);
            if (!map_box(out, ab[a])) break;
            na = a + 1;
        }
        vita_mark("xpcity: %s + %d area maps", m, na); last = -1;
    }
    const char *dot0 = strrchr(m, '.'); int bl0 = dot0 ? (int)(dot0 - m) : (int)strlen(m);
    if (last >= 0 && last < na && x >= ab[last][0] && x <= ab[last][0] + ab[last][2] && y <= ab[last][1] && y >= ab[last][1] - ab[last][2]) {
        snprintf(out, sizeof out, "%.*s_A%d.MAP", bl0, m, last); return out;   /* still in the area's (no flicking at an edge) */
    }
    const float in = 100.0f;   /* well inside, not at the very edge */
    if (mb[2] > 0 && x >= mb[0] + in && x <= mb[0] + mb[2] - in && y <= mb[1] - in && y >= mb[1] - mb[2] + in) { last = -1; return m; }
    int best = -1; float bd = 1e30f;
    for (int a = 0; a < na; a++) {
        if (x < ab[a][0] || x > ab[a][0] + ab[a][2] || y > ab[a][1] || y < ab[a][1] - ab[a][2]) continue;
        float cx = ab[a][0] + ab[a][2] * 0.5f, cy = ab[a][1] - ab[a][2] * 0.5f, d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (d < bd) { bd = d; best = a; }
    }
    last = best;
    if (best < 0) return m;
    snprintf(out, sizeof out, "%.*s_A%d.MAP", bl0, m, best);
    return out;
}

/* MW's race map (11-19): Most Wanted shows a race on a map of its own,
   TRACKS/L2RA/MINI_MAP_<race>.BIN (its race 1.2.3: MINI_MAP_1_2_3.BIN): the city map
   with that race's route drawn in white and its start in green (64 tiles, as
   MINI_MAP.BIN). The race names are in ROUTES<companion>/names.txt (xpevents.inc).
   The file comes from the PC tool (TRACKS/RACEMAPS<companion>/) or the raw install;
   it is made into <companion>_RACE<id>.MAP once, the one before it removed.
   Carbon has no such maps (its route is drawn from its road net). */
/* Carbon's minimap route for a race (xpevents.inc mr_race: its own search, made by the
   converter into ROUTES<companion>/mmroute.bin): x y pairs, runs apart by a far point.
   NULL for MW (its race maps carry theirs) or none. The buffer stays until the next race. */
const float *xpcity_race_route(const char *track, int event_id, int *n_out)
{
    static float *buf; static int bn, bev = -1, bc = -1;
    *n_out = 0;
    int c = city_of(track);
    if (c < 0 || k_city[c].game != XP_GAME_CARBON || !g_map[0]) return NULL;
    if (event_id == bev && c == bc) { *n_out = bn; return bn > 1 ? buf : NULL; }
    free(buf); buf = NULL; bn = 0; bev = event_id; bc = c;
    char troot[1024]; snprintf(troot, sizeof troot, "%s", g_map);
    char *sl = strrchr(troot, '/'); if (!sl) return NULL;
    *sl = 0;
    char p[1100]; snprintf(p, sizeof p, "%s/ROUTES%s/mmroute.bin", troot, k_city[c].companion);
    FILE *f = fopen(p, "rb");
    if (!f) { vita_mark("xpcity: no minimap routes (%s)", p); return NULL; }
    uint32_t hd[2];
    if (fread(hd, 4, 2, f) == 2 && hd[0] == 0x524d4d58u) {
        for (uint32_t i = 0; i < hd[1]; i++) {
            int32_t eh[2];
            if (fread(eh, 4, 2, f) != 2 || eh[1] < 0 || eh[1] > 1000000) break;
            if (eh[0] != event_id) { if (fseek(f, (long)eh[1] * 8, SEEK_CUR)) break; continue; }
            buf = malloc((size_t)eh[1] * 8);
            if (buf && fread(buf, 8, (size_t)eh[1], f) == (size_t)eh[1]) bn = eh[1];
            else { free(buf); buf = NULL; }
            break;
        }
    }
    fclose(f);
    vita_mark("xpcity: minimap route for %d: %d points", event_id, bn);
    *n_out = bn;
    return bn > 1 ? buf : NULL;
}

/* Carbon's race map (11-19, user: "look like Bayview or Rockport"): the city map with the
   race's route (xpcity_race_route) drawn into it as MW's race maps have theirs -- a white line
   ~2.6 px wide in a dark edge, the start a green bar across the road (MINI_MAP_7_2_2.BIN:
   white 251, edge ~(8,28,16), start (57,165,74) ~20 x 2 px) -- instead of over it. */
static void rm_line(uint8_t *img, int W, float ax, float ay, float bx, float by, float rad, const uint8_t col[3])
{
    int x0 = (int)floorf(fminf(ax, bx) - rad - 1), x1 = (int)ceilf(fmaxf(ax, bx) + rad + 1);
    int y0 = (int)floorf(fminf(ay, by) - rad - 1), y1 = (int)ceilf(fmaxf(ay, by) + rad + 1);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > W - 1) y1 = W - 1;
    float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f - ax, py = y + 0.5f - ay, t = l2 > 0 ? (px * ex + py * ey) / l2 : 0;
        if (t < 0) t = 0; if (t > 1) t = 1;
        float dx = px - ex * t, dy = py - ey * t, d = sqrtf(dx * dx + dy * dy), cov = rad + 0.5f - d;
        if (cov <= 0) continue;
        if (cov > 1) cov = 1;
        uint8_t *q = img + ((size_t)y * W + x) * 4;
        for (int k = 0; k < 3; k++) q[k] = (uint8_t)(q[k] + (col[k] - q[k]) * cov + 0.5f);
        if (q[3] < 255) q[3] = (uint8_t)(q[3] + (255 - q[3]) * cov + 0.5f);
    }
}
static int bake_route_map(int c, const char *troot, int event_id, const char *dst)
{
    int n = 0; const float *pt = xpcity_race_route(k_city[c].bun, event_id, &n);
    if (!pt || n < 2) return 0;
    float st[4]; int have_st = 0;   /* the start: starts.txt "id x y z heading" */
    {
        char p[1100]; snprintf(p, sizeof p, "%s/ROUTES%s/starts.txt", troot, k_city[c].companion);
        FILE *sf = fopen(p, "r"); int id; float x, y, z, h;
        while (sf && fscanf(sf, "%d %f %f %f %f", &id, &x, &y, &z, &h) == 5) if (id == event_id) { st[0] = x; st[1] = y; st[3] = h; have_st = 1; break; }
        if (sf) fclose(sf);
    }
    if (!have_st) { st[0] = pt[0]; st[1] = pt[1]; st[3] = 0; }
    const char *base = xpcity_map_at(k_city[c].bun, st[0], st[1]);   /* the city's, or the area's it is in */
    if (!base) return 0;
    long long len = xp_file_size(base);
    FILE *f = fopen(base, "rb");
    if (!f || len < 28) { if (f) fclose(f); return 0; }
    uint8_t *d = malloc((size_t)len);
    int ok = d && fread(d, 1, (size_t)len, f) == (size_t)len; fclose(f);
    uint32_t W = 0, H = 0; float fl[4];
    if (ok) { memcpy(&W, d + 4, 4); memcpy(&H, d + 8, 4); memcpy(fl, d + 12, 16); }
    if (!ok || memcmp(d, "BMAP", 4) || !W || W != H || W > 4096 || 28 + (long long)W * W * 4 > len || !(fl[2] > 0)) { free(d); return 0; }
    uint8_t *img = d + 28;
    const float sc = (float)W / fl[2];
    static const uint8_t edge[3] = { 8, 28, 16 }, white[3] = { 251, 251, 251 }, green[3] = { 57, 165, 74 };
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i + 1 < n; i++) {
            const float *a = pt + 2 * i, *b = a + 2;
            if (a[0] > 1e8f || b[0] > 1e8f) continue;   /* the runs' break */
            float gx = b[0] - a[0], gy = b[1] - a[1];
            if (gx * gx + gy * gy > 80.0f * 80.0f) continue;
            rm_line(img, (int)W, (a[0] - fl[0]) * sc, (fl[1] - a[1]) * sc, (b[0] - fl[0]) * sc, (fl[1] - b[1]) * sc,
                    pass ? 1.3f : 2.4f, pass ? white : edge);
        }
    if (have_st) {   /* the start: a bar across the road */
        float px = (st[0] - fl[0]) * sc, py = (fl[1] - st[1]) * sc, r = st[3] * 3.14159265f / 180.0f;
        float ux = -sinf(r), uy = -cosf(r);   /* across the heading (map y runs down) */
        for (int pass = 0; pass < 2; pass++)
            rm_line(img, (int)W, px - ux * 10, py - uy * 10, px + ux * 10, py + uy * 10, pass ? 1.0f : 2.0f, pass ? green : edge);
    }
    f = fopen(dst, "wb");
    ok = f && fwrite(d, 1, (size_t)len, f) == (size_t)len;
    if (f && fclose(f)) ok = 0;
    if (!ok) remove(dst);
    free(d);
    return ok;
}

const char *xpcity_race_map(const char *track, int event_id);
void xpcity_race_map_prepare(const char *region, int event_id)
{
    if (!g_map[0] || !region) return;
    for (int c = 0; c < NCITY; c++)
        if (!strcmp(region, k_city[c].companion) && strstr(g_map, k_city[c].companion)) { (void)xpcity_race_map(k_city[c].bun, event_id); return; }
}
const char *xpcity_race_map(const char *track, int event_id)
{
    static char out[1100]; static int out_ev = -1;
    static int bad_ev = -1;
    int c = city_of(track);
    if (c < 0 || !g_map[0] || !strstr(g_map, k_city[c].companion)) return NULL;
    if (event_id == out_ev && out[0]) return out;
    if (event_id == bad_ev) return NULL;
    char troot[1024]; snprintf(troot, sizeof troot, "%s", g_map);
    char *sl = strrchr(troot, '/'); if (!sl) return NULL;
    *sl = 0;
    char dst[1100];
    if (k_city[c].game == XP_GAME_CARBON) {   /* its route drawn into the city map (bake_route_map) */
        bad_ev = event_id;
        snprintf(dst, sizeof dst, "%s/%s_RACE%d.MAP", troot, k_city[c].companion, event_id);
        if (xp_file_size(dst) <= 0) {
            Uint32 t0 = SDL_GetTicks();
            int okb = bake_route_map(c, troot, event_id, dst);
            vita_mark("xpcity: race map %s: %s in %u ms", dst, okb ? "route drawn in" : "FAILED (no route?)", (unsigned)(SDL_GetTicks() - t0));
            if (!okb) return NULL;
        }
        goto keep;
    }
    char p[1100], name[64] = "";
    snprintf(p, sizeof p, "%s/ROUTES%s/names.txt", troot, k_city[c].companion);
    FILE *f = fopen(p, "r");
    if (f) {
        int id; char nm[64];
        while (fscanf(f, "%d %63s", &id, nm) == 2) if (id == event_id) { snprintf(name, sizeof name, "%s", nm); break; }
        fclose(f);
    }
    bad_ev = event_id;
    if (!name[0]) { vita_mark("xpcity: race %d has no name (%s): no race map", event_id, p); return NULL; }
    {   /* "7_2_2_r_sprint" -> "7_2_2_R": the leading numbers, _R for the reverse ('r') */
        char b[64] = ""; size_t bl = 0; int rev = 0, nums = 1;
        for (char *tok = strtok(name, "._"); tok; tok = strtok(NULL, "._")) {
            int num = tok[0] >= '0' && tok[0] <= '9';
            if (num && nums && bl + strlen(tok) + 2 < sizeof b) bl += (size_t)snprintf(b + bl, sizeof b - bl, "%s%s", bl ? "_" : "", tok);
            else { nums = 0; if (!strcmp(tok, "r") || !strcmp(tok, "R")) rev = 1; }
        }
        snprintf(name, sizeof name, "%s%s", b, rev ? "_R" : "");
    }
    snprintf(dst, sizeof dst, "%s/%s_RACE%d.MAP", troot, k_city[c].companion, event_id);
    if (xp_file_size(dst) <= 0) {
        char src[1100];
        snprintf(src, sizeof src, "%s/RACEMAPS%s/MINI_MAP_%s.BIN", troot, k_city[c].companion, name);
        if (xp_file_size(src) <= 0) snprintf(src, sizeof src, XP_RAW_ROOT "/%s/TRACKS/%s/MINI_MAP_%s.BIN", k_city[c].dir, k_city[c].companion, name);
        size_t nl = strlen(name);
        if (xp_file_size(src) <= 0 && nl > 2 && !strcmp(name + nl - 2, "_R")) {   /* no reverse map: the race's own */
            name[nl - 2] = 0;
            snprintf(src, sizeof src, "%s/RACEMAPS%s/MINI_MAP_%s.BIN", troot, k_city[c].companion, name);
            if (xp_file_size(src) <= 0) snprintf(src, sizeof src, XP_RAW_ROOT "/%s/TRACKS/%s/MINI_MAP_%s.BIN", k_city[c].dir, k_city[c].companion, name);
        }
        if (xp_file_size(src) <= 0) { vita_mark("xpcity: no race map for %d (MINI_MAP_%s.BIN)", event_id, name); return NULL; }
        Uint32 t0 = SDL_GetTicks();
        int rc = xp_minimap(src, dst, k_city[c].game);
        vita_mark("xpcity: race map %s from %s: %s in %u ms", dst, src, rc == XP_OK ? "made" : "FAILED", (unsigned)(SDL_GetTicks() - t0));
        if (rc != XP_OK) return NULL;
    }
keep:
    {   /* one at a time (4 MB each): <companion>_RACE.txt holds the id of the one on the card */
        char side[1100]; snprintf(side, sizeof side, "%s/%s_RACE.txt", troot, k_city[c].companion);
        FILE *sf = fopen(side, "r"); int old = -1;
        if (sf) { if (fscanf(sf, "%d", &old) != 1) old = -1; fclose(sf); }
        if (old >= 0 && old != event_id) {
            char op[1100]; snprintf(op, sizeof op, "%s/%s_RACE%d.MAP", troot, k_city[c].companion, old); remove(op);
        }
        if (old != event_id && (sf = fopen(side, "w"))) { fprintf(sf, "%d\n", event_id); fclose(sf); }
    }
    snprintf(out, sizeof out, "%s", dst); out_ev = event_id; bad_ev = -1;
    return out;
}

static void files_notice(int c);   /* the file check (below) */
static int g_failed[NCITY];   /* XF3: a translation that failed this session */
static int g_known[NCITY], g_ok[NCITY];   /* xpcity_installed: the files do not change while the game runs */
/* ---- the retail streamer's way (11-24): no translated stream, no tiles ----
   Rockport is drawn by the retail streamer (rstream.c) straight from the raw MW
   stream, each section translated as it loads (xp_section). What stays on the
   card: the translated companion (TRACKS/L2RA.BUN, 1.4 MB, its tables + UG2
   scenery groups) and STREAML2RA.XPL (collision packs + night lamp flares, xp_lean),
   both made once from the raw files. Palmont: the translated stream + tiles still. */
static int g_lean_known[NCITY], g_lean_on[NCITY];
static int lean_city(int c)
{
    if (!g_lean_known[c]) {
        char a[256], b[256];
        snprintf(a, sizeof a, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
        snprintf(b, sizeof b, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
        g_lean_on[c] = xpcity_enabled() && rstream_enabled() &&
                       xp_file_size(a) > 0 && xp_file_size(b) > 0;
        g_lean_known[c] = 1;
        printf("xpcity: %s %s%c", k_city[c].name, g_lean_on[c] ? "streams from its raw files (retail streamer, no tiles)"
                                                             : "uses its translated stream + tiles", 10);
    }
    return g_lean_on[c];
}
int xpcity_lean(const char *track) { int c = city_of(track); return c >= 0 && lean_city(c); }
int xpcity_lean_game(const char *track) { int c = city_of(track); return c >= 0 && lean_city(c) ? k_city[c].game : 0; }
int xpcity_stream_path(const char *track, char *out, size_t n)
{
    int c = city_of(track);
    if (c < 0 || !lean_city(c)) return 0;
    snprintf(out, n, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    return 1;
}
int xpcity_coll_path(const char *troot, const char *track, char *out, size_t n)
{
    int c = city_of(track);
    if (c < 0 || !lean_city(c)) return 0;
    snprintf(out, n, "%s/%s.XPL", troot, k_city[c].bun);
    return 1;
}
/* the lean file's night lamp flare packs (0x80135100), read once per city */
unsigned char *xpcity_lamps(const char *troot, const char *track, long *len)
{
    static unsigned char *buf; static long blen; static char have[300];
    *len = 0;
    char p[300];
    if (!xpcity_coll_path(troot, track, p, sizeof p)) return NULL;
    if (strcmp(have, p)) {
        free(buf); buf = NULL; blen = 0; snprintf(have, sizeof have, "%s", p);
        FILE *f = fopen(p, "rb");
        long cap = 0; unsigned char h[8];
        while (f && fread(h, 1, 8, f) == 8) {
            uint32_t id, sz; memcpy(&id, h, 4); memcpy(&sz, h + 4, 4);
            if (id != 0x80135100u) { if (fseek(f, (long)sz, SEEK_CUR)) break; continue; }
            if (blen + 8 + (long)sz > cap) {
                long nc = cap ? cap * 2 : 256 << 10; while (nc < blen + 8 + (long)sz) nc *= 2;
                unsigned char *nb = realloc(buf, (size_t)nc); if (!nb) break;
                buf = nb; cap = nc;
            }
            memcpy(buf + blen, h, 8);
            if (fread(buf + blen + 8, 1, sz, f) != sz) break;
            blen += 8 + (long)sz;
        }
        if (f) fclose(f);
    }
    *len = blen;
    return buf;
}
static int lean_marker_ok(const char *mark, long long raw_size)
{
    FILE *f = fopen(mark, "r"); if (!f) return 0;
    int v = 0, g = 0; long long s = -1;
    int ok = fscanf(f, "xplean %d %d %lld", &v, &g, &s) == 3 && v == XP_LEAN_VERSION && s == raw_size;
    fclose(f);
    return ok;
}
static int lean_prepare(int c, const char *troot)
{
    char raw[256], rawc[256], outc[1024], lean[1024], mark[1024], p[1100];
    snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(outc, sizeof outc, "%s/%s.BUN", troot, k_city[c].companion);
    snprintf(lean, sizeof lean, "%s/%s.XPL", troot, k_city[c].bun);
    snprintf(mark, sizeof mark, "%s/%s.lean", troot, k_city[c].bun);
    long long raw_size = xp_file_size(raw);
    {   /* the old translated stream, its tiles and caches: not used any more (keeptiles.txt keeps them) */
        FILE *k = fopen("ux0:data/NFSU2/keeptiles.txt", "rb");
        if (k) fclose(k);
        else {
            snprintf(p, sizeof p, "%s/%s.BUN", troot, k_city[c].bun);
            if (xp_file_size(p) > 0) { remove(p); vita_mark("xpcity: old translated stream %s deleted", p); }
            snprintf(p, sizeof p, "%s/%s.xp", troot, k_city[c].bun); remove(p);
            snprintf(p, sizeof p, XP_CACHE "/%s.BUN.idx", k_city[c].bun); remove(p);
            skyline_forget(k_city[c].bun);
            world_tiles_drop_track(k_city[c].bun);
        }
    }
    if (xp_file_size(outc) > 0 && xp_file_size(lean) > 0 && lean_marker_ok(mark, raw_size)) {
        prep_rest(c, troot);   /* each made already, or a newer version */
        city_screen_done();
        return 1;
    }
    if (g_failed[c]) { vita_mark("xpcity: %s failed to prepare this session; not again until a restart", k_city[c].name); return 0; }
    vita_mark("xpcity: preparing %s for the retail streamer from %s (%lld bytes)", k_city[c].name, raw, raw_size);
    files_notice(c);
    remove(mark);
    snprintf(p, sizeof p, XP_CACHE "/%s.XPL.idx", k_city[c].bun); remove(p);
    g_prep_city = k_city[c].name;
    memset((void *)&g_job, 0, sizeof g_job);
    g_job.in = raw; g_job.out = lean; g_job.game = k_city[c].game; g_job_lean = 1;
    SDL_Thread *t = SDL_CreateThread(job_thread, "xpcity", NULL);
    Uint32 t0 = SDL_GetTicks();
    char title[64]; snprintf(title, sizeof title, "PREPARING %s", k_city[c].name);
    prep_bar("READING THE CITY", 0.0f);
    while (t && !g_job.finished) {
        float frac = g_job.total > 0 ? (float)g_job.done / (float)g_job.total : 0.0f;
        prep_bar("READING THE CITY", 0.13f * frac);
        SDL_Delay(100);
    }
    if (t) SDL_WaitThread(t, NULL);
    else job_thread(NULL);
    g_job_lean = 0;
    int ok = g_job.rc == XP_OK && xp_companion(rawc, outc, k_city[c].game) == XP_OK;
    if (ok) {
        FILE *m = fopen(mark, "w");
        ok = m && fprintf(m, "xplean %d %d %lld\n", XP_LEAN_VERSION, k_city[c].game, raw_size) > 0;
        if (m && fclose(m)) ok = 0;
    }
    if (ok) prep_rest(c, troot);
    vita_mark("xpcity: %s %s in %.1f s", k_city[c].name, ok ? "prepared" : "FAILED", (SDL_GetTicks() - t0) / 1000.0);
    if (!ok) {
        remove(mark);
        g_failed[c] = 1; g_known[c] = 0;
        Uint32 e0 = SDL_GetTicks();
        while (xpcity_screen && SDL_GetTicks() - e0 < 5000) {
            city_screen(title, "COULD NOT PREPARE THE CITY", "SEE THE LOG. BACK TO BAYVIEW.");
            SDL_Delay(100);
        }
    }
    city_screen_done();
    return ok;
}

int xpcity_prepare(const char *troot, const char *track) {
    int c = city_of(track);
    if (c < 0) return 1;
    if (!xpcity_enabled()) { vita_mark("xpcity: %s asked for without " XP_SWITCH, k_city[c].name); return 0; }
    if (lean_city(c)) return lean_prepare(c, troot);
    char raw[256], rawc[256], out[1024], outc[1024], mark[1024];
    snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(out, sizeof out, "%s/%s.BUN", troot, k_city[c].bun);
    snprintf(outc, sizeof outc, "%s/%s.BUN", troot, k_city[c].companion);
    snprintf(mark, sizeof mark, "%s/%s.xp", troot, k_city[c].bun);
    long long raw_size = xp_file_size(raw);
    int have = xp_file_size(out) > 0 && xp_file_size(outc) > 0;
    /* sent from the PC tool (no raw files here), or translated before from these raw files */
    if (have && xp_marker_ok(mark, k_city[c].game, raw_size)) { prep_rest(c, troot); city_screen_done(); return 1; }
    if (raw_size <= 0 || xp_file_size(rawc) <= 0) {
        vita_mark("xpcity: %s is not installed (no translated city, no raw files in " XP_RAW_ROOT "/%s)",
                  k_city[c].name, k_city[c].dir);
        return 0;
    }
    if (g_failed[c]) { vita_mark("xpcity: %s failed to translate this session; not again until a restart", k_city[c].name); return 0; }
    vita_mark("xpcity: translating %s from %s (%lld bytes)", k_city[c].name, raw, raw_size);
    files_notice(c);
    remove(mark);
    /* everything built from an older translation goes with it */
    char p[256];
    snprintf(p, sizeof p, XP_CACHE "/%s.BUN.compact", k_city[c].bun); remove(p);
    snprintf(p, sizeof p, XP_CACHE "/%s.BUN.compact.strip", k_city[c].bun); remove(p);
    snprintf(p, sizeof p, XP_CACHE "/%s.focus", k_city[c].bun + 6); remove(p);
    snprintf(p, sizeof p, XP_CACHE "/tiles/%s_v%d.idx", k_city[c].bun, WTILE_VERSION); remove(p);
    snprintf(p, sizeof p, XP_CACHE "/tiles/%s_v%d.pak", k_city[c].bun, WTILE_VERSION); remove(p);
    skyline_forget(k_city[c].bun);
    snprintf(p, sizeof p, "%s/ROUTES%s/RoutesFreeRoam.bin", troot, k_city[c].companion); remove(p);   /* road net: rebuilt below */
    snprintf(p, sizeof p, "%s/ROUTES%s/events.txt", troot, k_city[c].companion); remove(p);       /* race events too */
    snprintf(p, sizeof p, "%s/XPSMK%s.BUN", troot, k_city[c].companion); remove(p);              /* the props */
    {   /* XF3 11-22: the minimap and its parts were kept from the old translation (make_map skips a map that is there) */
        char q[1100];
        snprintf(q, sizeof q, "%s/%s.MAP", troot, k_city[c].companion); remove(q);
        snprintf(q, sizeof q, "%s/%s.SEA", troot, k_city[c].companion); remove(q);
        for (int a = 0; a < 8; a++) { snprintf(q, sizeof q, "%s/%s_A%d.MAP", troot, k_city[c].companion, a); remove(q); }
    }
    memset((void *)&g_job, 0, sizeof g_job);
    g_job.in = raw; g_job.out = out; g_job.game = k_city[c].game;
    SDL_Thread *t = SDL_CreateThread(job_thread, "xpcity", NULL);
    Uint32 t0 = SDL_GetTicks();
    char title[64]; snprintf(title, sizeof title, "PREPARING %s", k_city[c].name);
    while (t && !g_job.finished) {
        float frac = g_job.total > 0 ? (float)g_job.done / (float)g_job.total : 0.0f;
        char a[96]; snprintf(a, sizeof a, "%d%%  -  ONE TIME ONLY", (int)(frac * 100.0f));
        city_screen(title, "TRANSLATING THE CITY", a);
        SDL_Delay(100);
    }
    if (t) SDL_WaitThread(t, NULL);
    else job_thread(NULL);
    int ok = g_job.rc == XP_OK && xp_companion(rawc, outc, k_city[c].game) == XP_OK && xp_marker_write(mark, k_city[c].game, raw_size);
    if (ok) prep_rest(c, troot);
    vita_mark("xpcity: %s %s in %.1f s", k_city[c].name, ok ? "translated" : "FAILED", (SDL_GetTicks() - t0) / 1000.0);
    if (!ok) {
        remove(mark);
        g_failed[c] = 1; g_known[c] = 0;   /* XF3: the picker stops offering it; no rerun on every pick */
        Uint32 e0 = SDL_GetTicks();
        while (xpcity_screen && SDL_GetTicks() - e0 < 5000) {
            city_screen(title, "COULD NOT TRANSLATE THE CITY", "SEE THE LOG. BACK TO BAYVIEW.");
            SDL_Delay(100);
        }
    }
    city_screen_done();
    return ok;
}

/* the Free Run world picker (shop.c): 0 Bayview, 1 Rockport, 2 Palmont */
int g_xpcity_pick;
/* raw files dropped in for city c: its stream + companion in expansion/<mw|carbon>/TRACKS */
static int raw_present(int c)
{
    char a[256], b[256];
    snprintf(a, sizeof a, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(b, sizeof b, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    return xp_file_size(a) > 0 && xp_file_size(b) > 0;
}
/* on: the switch (expansion/enable.txt: Settings, or the old Prep Tool), or (11-25, no PC
   tool) a city's raw files dropped in -- the player copies them and nothing else --
   unless Settings ticked the World Expansion off (expansion/off.txt) */
int xpcity_enabled(void)
{
    static int known, on;
    if (!known) {
        int sw = xp_file_size(XP_SWITCH) >= 0, raw = 0;
        for (int c = 0; c < NCITY; c++) raw |= raw_present(c) << c;
        on = sw || (raw && xp_file_size(XP_OFF) < 0);
        known = 1;
        printf("xpcity: %s%s%s\n", !on ? (raw ? "off (ticked off in Settings: " XP_OFF ")" : "off (no raw files in " XP_RAW_ROOT ", no enable.txt)")
                                       : sw ? "enable.txt found" : "raw files found",
               raw & 1 ? " -- Rockport files in" : "", raw & 2 ? " -- Palmont files in" : "");
    }
    return on;
}

/* ---- the file check (was the Prep Tool's): the files each city reads, as in the
   games the expansion was made with (attributes.bin: never read here). Every size is checked; the small files' CRC32
   too (~5 MB); the streams by size only (reading 0.5-1.2 GB just for this would add
   a minute to the first boot). A different file is logged and shown on the loading
   screen, then the prep goes on (a different release may still work). */
static const struct { int city; const char *rel; long long size; unsigned crc; int opt; } k_files[] = {
    { 0, "TRACKS/STREAML2RA.BUN", 532663556LL, 0xbd947661u, 0 },
    { 0, "TRACKS/L2RA.BUN", 1413824LL, 0x75079449u, 0 },
    { 0, "TRACKS/L2RA/MINI_MAP.BIN", 221492LL, 0x93886852u, 1 },
    { 0, "GLOBAL/gameplay.bin", 2105216LL, 0xc5153d5au, 1 },
    { 1, "TRACKS/STREAML5RA.BUN", 1238368256LL, 0x964f0bedu, 0 },
    { 1, "TRACKS/L5RA.BUN", 1068712LL, 0xf1d4c3ceu, 0 },
    { 1, "GLOBAL/gameplay.bin", 1638336LL, 0x5f351271u, 1 },
};
static unsigned file_crc(const char *p)
{
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    static unsigned char buf[64 << 10]; static unsigned tab[256];
    if (!tab[1]) for (unsigned i = 0; i < 256; i++) {   /* the zip CRC-32, as the Prep Tool's */
        unsigned k = i; for (int b = 0; b < 8; b++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1; tab[i] = k; }
    unsigned c = 0xffffffffu; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) for (size_t i = 0; i < n; i++) c = tab[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    fclose(f);
    return c ^ 0xffffffffu;
}
/* 0 all match; else the number of missing/different files, the first in why */
static int files_check(int c, char *why, size_t n)
{
    int bad = 0; why[0] = 0;
    for (int i = 0; i < (int)(sizeof k_files / sizeof *k_files); i++) {
        if (k_files[i].city != c) continue;
        char p[300]; snprintf(p, sizeof p, XP_RAW_ROOT "/%s/%s", k_city[c].dir, k_files[i].rel);
        long long sz = xp_file_size(p);
        const char *w = sz < 0 ? "missing" : sz != k_files[i].size ? "a different size"
                      : k_files[i].size < (64LL << 20) && file_crc(p) != k_files[i].crc ? "different" : NULL;
        if (!w) continue;
        vita_mark("xpcity: %s file check: %s is %s%s", k_city[c].name, k_files[i].rel, w,
                  sz < 0 && k_files[i].opt ? " (optional: that part is left out)" : "");
        if (!bad++) {
            const char *nm = strrchr(k_files[i].rel, '/');
            snprintf(why, n, "%s IS %s", nm ? nm + 1 : k_files[i].rel, w);
            for (char *s = why; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
        }
    }
    if (!bad) vita_mark("xpcity: %s file check: all files match", k_city[c].name);
    return bad;
}
/* shown once per city on its first prep: a few seconds, then the prep goes on */
static void files_notice(int c)
{
    char why[96];
    if (!files_check(c, why, sizeof why) || !xpcity_screen) return;
    char t[64]; snprintf(t, sizeof t, "PREPARING %s", k_city[c].name);
    Uint32 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 4000) {
        city_screen(t, why, "NOT THE EXPECTED GAME VERSION -- TRYING ANYWAY");
        SDL_Delay(100);
    }
}
/* 1 usable (translated, or raw files to translate), 2 translated but too old for this
   build and no raw files to translate again (V3: "run the Prep Tool again"), 0 none */
static int city_ok(int c)
{
    if (!xpcity_enabled() || g_failed[c]) return 0;
    char a[256], b[256], m[256];
    snprintf(a, sizeof a, "ux0:data/NFSU2/TRACKS/%s.BUN", k_city[c].bun);
    snprintf(b, sizeof b, "ux0:data/NFSU2/TRACKS/%s.BUN", k_city[c].companion);
    snprintf(m, sizeof m, "ux0:data/NFSU2/TRACKS/%s.xp", k_city[c].bun);
    int have = xp_file_size(a) > 0 && xp_file_size(b) > 0;
    if (have && xp_marker_ok(m, k_city[c].game, -1)) return 1;   /* translated */
    snprintf(a, sizeof a, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(b, sizeof b, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    if (xp_file_size(a) > 0 && xp_file_size(b) > 0) return 1;   /* raw: translated on the way in */
    if (have && xp_file_size(m) > 0) {
        printf("xpcity: %s is translated for an older build (%s); its raw files are needed%c", k_city[c].name, m, 10);
        return 2;
    }
    return 0;
}
static int city_state(int pick)
{
    int c = pick - 1;
    if (c < 0 || c >= NCITY) return pick == 0;
    if (!g_known[c]) { g_ok[c] = city_ok(c); g_known[c] = 1; }
    return g_ok[c];
}
int xpcity_installed(int pick) { return city_state(pick) != 0; }
int xpcity_outdated(int pick) { return city_state(pick) == 2; }
const char *xpcity_track(int pick) { return pick >= 1 && pick <= NCITY ? k_city[pick - 1].bun : NULL; }

/* ---- race barriers (MW BARRIER_SPLINE_<n> groups; xpconv xp_companion) ----
   TRACKS/ROUTES<region>/barriers.txt (mkevents): "<event> NAME NAME ...", a leading
   '*' = the barrier the other way round (MW flips its artwork: its arrows point
   the other way; 11-19 HW: without it every arrow pointed right). */
#include "wcoll.h"
#include "world_scenery.h"
typedef struct { int ev, first, n; } XpBarEv;
static XpBarEv *g_bar_ev; static int g_bar_nev;
static char (*g_bar_nm)[24]; static int g_bar_nnm;
static unsigned char *g_bar_flip;   /* per name: '*' */
static const XpBarEv *bar_find(int event)
{
    for (int i = 0; i < g_bar_nev; i++) if (g_bar_ev[i].ev == event) return &g_bar_ev[i];
    return NULL;
}
static int xpbar_on(const char *name, int event)
{
    const XpBarEv *e = bar_find(event);
    if (!e) return -1;
    if (!name) return 1;
    for (int i = 0; i < e->n; i++) if (!strcmp(g_bar_nm[e->first + i], name)) return g_bar_flip[e->first + i] ? 2 : 1;
    return 0;
}
void xpcity_barriers_select(const char *troot, const char *track)
{
    const char *reg = xpcity_is_city(track) ? xpcity_region(track) : NULL;
    if (!reg) { g_wg_xp_barrier_on = NULL; g_bar_region[0] = 0; return; }
    g_wg_xp_barrier_on = xpbar_on;
    if (!strcmp(reg, g_bar_region)) return;
    snprintf(g_bar_region, sizeof g_bar_region, "%s", reg);
    free(g_bar_ev); free(g_bar_nm); free(g_bar_flip); g_bar_ev = NULL; g_bar_nm = NULL; g_bar_flip = NULL; g_bar_nev = g_bar_nnm = 0;
    char p[1024]; snprintf(p, sizeof p, "%s/ROUTES%s/barriers.txt", troot, reg);
    FILE *f = fopen(p, "r");
    if (!f) { printf("xpcity: no race barriers (%s)\n", p); return; }
    int cev = 0, cnm = 0; char tok[64];
    while (fscanf(f, "%63s", tok) == 1) cnm++;
    rewind(f);
    char line[4096];
    while (fgets(line, sizeof line, f)) cev++;
    g_bar_ev = calloc((size_t)cev + 1, sizeof *g_bar_ev); g_bar_nm = calloc((size_t)cnm + 1, sizeof *g_bar_nm);
    g_bar_flip = calloc((size_t)cnm + 1, 1);
    if (!g_bar_ev || !g_bar_nm || !g_bar_flip) { fclose(f); free(g_bar_ev); free(g_bar_nm); free(g_bar_flip); g_bar_ev = NULL; g_bar_nm = NULL; g_bar_flip = NULL; return; }
    rewind(f);
    while (fgets(line, sizeof line, f)) {
        char *s = strtok(line, " \t\r\n");
        if (!s) continue;
        XpBarEv *e = &g_bar_ev[g_bar_nev++];
        e->ev = atoi(s); e->first = g_bar_nnm; e->n = 0;
        while ((s = strtok(NULL, " \t\r\n")) && g_bar_nnm < cnm) {
            int flip = *s == '*';
            if (flip) s++;
            g_bar_flip[g_bar_nnm] = (unsigned char)flip;
            snprintf(g_bar_nm[g_bar_nnm++], 24, "%s", s); e->n++;
        }
    }
    fclose(f);
    printf("xpcity: race barriers for %d events (%d groups) from %s\n", g_bar_nev, g_bar_nnm, p);
}
int xpcity_barriers_enable(WColl *w, int event)
{
    const XpBarEv *e = g_wg_xp_barrier_on ? bar_find(event) : NULL;
    int n = 0;
    for (int i = 0; e && i < e->n; i++) n += wcoll_enable_group(w, g_bar_nm[e->first + i]);
    return n;
}
