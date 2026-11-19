/* xpcity.c -- the expansion cities' one-time translation on the console.
 *
 * Rockport (Most Wanted 2005) and Palmont (Carbon) are read as UG2 tracks
 * STREAML2RA / STREAML5RA once their world stream is translated into UG2's
 * format (xpconv.c). The NFSU2 Expansion Prep Tool does that on a PC and sends
 * the result with its marker (TRACKS/<stream>.xp). Without the tool the player
 * copies the game's raw files to ux0:data/NFSU2/expansion/<mw|carbon>/TRACKS
 * and the first boot into the city translates them here, behind a progress
 * screen (slower, same output); the boot's tile conversion then builds the
 * rest exactly as it does for a Bayview track. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <SDL.h>
#include "xpconv.h"
#include "xpcity.h"
#include "skyline.h"
#include "world_tiles.h"

void vita_mark(const char *fmt, ...);
void vita_fast_clocks(void);
void vita_progress_screen(const char *title, const char *line1, const char *line2, float frac);
void vita_progress_screen_done(void);

#define XP_RAW_ROOT "ux0:data/NFSU2/expansion"
#define XP_SWITCH   XP_RAW_ROOT "/enable.txt"   /* the switch: the cities need it AND their files */
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

static int g_window;
int xpcity_window_made(void) { return g_window; }

static struct { volatile long long done, total; volatile int finished, rc; const char *in, *out; int game; } g_job;
static int job_progress(void *ud, long long done, long long total) {
    (void)ud; g_job.done = done; g_job.total = total; return 0;
}
static int job_thread(void *ud) {
    (void)ud;
    XpStats st;
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
    if (xp_file_size(g_map) > 0) return;
    snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s/MINI_MAP.BIN", k_city[c].dir, k_city[c].companion);
    if (k_city[c].game == XP_GAME_CARBON)   /* no map art for the city: drawn from its road collision */
        snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    int rc = xp_file_size(raw) > 0 ? xp_minimap(raw, g_map, k_city[c].game) : XP_ERR_OPEN_IN;
    vita_mark("xpcity: minimap %s: %s", g_map, rc == XP_OK ? "made" : "none (no raw MINI_MAP.BIN / stream)");
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
static void make_events(int c, const char *troot)
{
    char dir[256], ev[300], gp[256], rawc[256], raws[256];
    snprintf(dir, sizeof dir, "%s/ROUTES%s", troot, k_city[c].companion);
    snprintf(ev, sizeof ev, "%s/shops.txt", dir);                    /* a set from before the shops / track maps: made again */
    int have_shops = xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/trackpoly.bin", dir);
    have_shops = have_shops && xp_file_size(ev) > 0;
    snprintf(ev, sizeof ev, "%s/events.txt", dir);
    if (have_shops && xp_file_size(ev) > 0) return;
    snprintf(gp, sizeof gp, XP_RAW_ROOT "/%s/GLOBAL/gameplay.bin", k_city[c].dir);
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(raws, sizeof raws, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    if (xp_file_size(gp) <= 0 || xp_file_size(rawc) <= 0 || xp_file_size(raws) <= 0) {
        vita_mark("xpcity: no race events for %s (no raw gameplay.bin / %s / %s)", k_city[c].name, k_city[c].companion, k_city[c].bun);
        return;
    }
    mkdir(dir, 0777);
    char title[64]; snprintf(title, sizeof title, "PREPARING %s", k_city[c].name);
    if (g_window) vita_progress_screen(title, "BUILDING THE RACES", "ONE TIME ONLY", 0.0f);
    Uint32 t0 = SDL_GetTicks();
    XpEventStats st;
    /* event ids: Rockport 7301 on, Palmont 7601 on (beyond.c takes 4000-7999) */
    int rc = xp_events(gp, rawc, raws, dir, k_city[c].game, k_city[c].name[0] == 'R' ? "Rockport" : "Palmont",
                       k_city[c].game == XP_GAME_CARBON ? 7601 : 7301, &st);
    if (rc != XP_OK) remove(ev);
    vita_mark("xpcity: race events %s: %s in %.1f s (%ld MW races -> %ld events: %ld circuits %ld sprints %ld drags, %ld skipped, "
              "%ld dropped; barriers %ld on %ld off)", dir, rc == XP_OK ? "made" : "FAILED", (SDL_GetTicks() - t0) / 1000.0,
              st.races, st.events, st.circuits, st.sprints, st.drags, st.skipped, st.dropped, st.barriers_on, st.barriers_off);
    if (g_window) vita_progress_screen_done();
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
    char title[64]; snprintf(title, sizeof title, "PREPARING %s", k_city[c].name);
    if (g_window) vita_progress_screen(title, "PLACING THE PROPS", "ONE TIME ONLY", 0.0f);
    Uint32 t0 = SDL_GetTicks();
    XpPropStats st;
    int rc = xp_props(rawc, raws, out, k_city[c].game, &st);
    vita_mark("xpcity: props %s: %s in %.1f s (%ld MW smackables -> %ld props, %ld infos, %ld packs; scenery: %ld other kinds, "
              "%ld too big, %ld no model, %ld bad)", out, rc == XP_OK ? "made" : "FAILED", (SDL_GetTicks() - t0) / 1000.0,
              st.mw, st.props, st.infos, st.packs, st.other_kind, st.too_big, st.no_model, st.bad);
    if (g_window) vita_progress_screen_done();
}
const char *xpcity_map(const char *track)
{
    int c = city_of(track);
    return c >= 0 && g_map[0] && strstr(g_map, k_city[c].companion) ? g_map : NULL;
}

int xpcity_prepare(const char *troot, const char *track) {
    int c = city_of(track);
    if (c < 0) return 1;
    if (!xpcity_enabled()) { vita_mark("xpcity: %s asked for without " XP_SWITCH, k_city[c].name); return 0; }
    char raw[256], rawc[256], out[1024], outc[1024], mark[1024];
    snprintf(raw, sizeof raw, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(rawc, sizeof rawc, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    snprintf(out, sizeof out, "%s/%s.BUN", troot, k_city[c].bun);
    snprintf(outc, sizeof outc, "%s/%s.BUN", troot, k_city[c].companion);
    snprintf(mark, sizeof mark, "%s/%s.xp", troot, k_city[c].bun);
    long long raw_size = xp_file_size(raw);
    int have = xp_file_size(out) > 0 && xp_file_size(outc) > 0;
    /* sent from the PC tool (no raw files here), or translated before from these raw files */
    if (have && xp_marker_ok(mark, k_city[c].game, raw_size)) { make_map(c, troot); make_routes(c, troot); make_events(c, troot); make_props(c, troot); return 1; }
    if (raw_size <= 0 || xp_file_size(rawc) <= 0) {
        vita_mark("xpcity: %s is not installed (no translated city, no raw files in " XP_RAW_ROOT "/%s)",
                  k_city[c].name, k_city[c].dir);
        return 0;
    }
    vita_mark("xpcity: translating %s from %s (%lld bytes)", k_city[c].name, raw, raw_size);
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
    if (!g_window) {   /* graphics up early: the world load's own screens come later */
        vita_fast_clocks();
        SDL_Window *w = SDL_CreateWindow("OpenUG2", 0, 0, 960, 544, SDL_WINDOW_OPENGL);
        SDL_GL_CreateContext(w);
        g_window = 1;
    }
    memset((void *)&g_job, 0, sizeof g_job);
    g_job.in = raw; g_job.out = out; g_job.game = k_city[c].game;
    SDL_Thread *t = SDL_CreateThread(job_thread, "xpcity", NULL);
    Uint32 t0 = SDL_GetTicks();
    char title[64]; snprintf(title, sizeof title, "PREPARING %s", k_city[c].name);
    while (t && !g_job.finished) {
        float frac = g_job.total > 0 ? (float)g_job.done / (float)g_job.total : 0.0f;
        char a[96]; Uint32 ms = SDL_GetTicks() - t0;
        if (frac > 0.05f) snprintf(a, sizeof a, "%d%%  -  ABOUT %d MIN LEFT", (int)(frac * 100.0f),
                                   (int)(ms / frac * (1.0f - frac) / 60000.0f + 0.999f));
        else snprintf(a, sizeof a, "%d%%  -  ONE TIME ONLY", (int)(frac * 100.0f));
        vita_progress_screen(title, "TRANSLATING THE CITY", a, frac);
        SDL_Delay(100);
    }
    if (t) SDL_WaitThread(t, NULL);
    else job_thread(NULL);
    int ok = g_job.rc == XP_OK && xp_companion(rawc, outc, k_city[c].game) == XP_OK && xp_marker_write(mark, k_city[c].game, raw_size);
    if (ok) { make_map(c, troot); make_routes(c, troot); make_events(c, troot); make_props(c, troot); }
    vita_mark("xpcity: %s %s in %.1f s", k_city[c].name, ok ? "translated" : "FAILED", (SDL_GetTicks() - t0) / 1000.0);
    if (!ok) {
        remove(mark);
        Uint32 e0 = SDL_GetTicks();
        while (SDL_GetTicks() - e0 < 5000) {
            vita_progress_screen(title, "COULD NOT TRANSLATE THE CITY", "SEE THE LOG. BACK TO BAYVIEW.", 0.0f);
            SDL_Delay(100);
        }
    }
    vita_progress_screen_done();
    return ok;
}

/* the Free Run world picker (shop.c): 0 Bayview, 1 Rockport, 2 Palmont */
int g_xpcity_pick;
/* the switch (as the World Expansion's beyond/enable.txt): the cities need it AND their files */
int xpcity_enabled(void)
{
    static int known, on;
    if (!known) {
        on = xp_file_size(XP_SWITCH) >= 0;
        known = 1;
        printf("xpcity: %s\n", on ? "enable.txt found -- Rockport / Palmont on where installed" : "off (no " XP_SWITCH ")");
    }
    return on;
}
static int city_ok(int c)
{
    if (!xpcity_enabled()) return 0;
    char a[256], b[256], m[256];
    snprintf(a, sizeof a, "ux0:data/NFSU2/TRACKS/%s.BUN", k_city[c].bun);
    snprintf(b, sizeof b, "ux0:data/NFSU2/TRACKS/%s.BUN", k_city[c].companion);
    snprintf(m, sizeof m, "ux0:data/NFSU2/TRACKS/%s.xp", k_city[c].bun);
    if (xp_file_size(a) > 0 && xp_file_size(b) > 0 && xp_marker_ok(m, k_city[c].game, -1)) return 1;   /* translated */
    snprintf(a, sizeof a, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].bun);
    snprintf(b, sizeof b, XP_RAW_ROOT "/%s/TRACKS/%s.BUN", k_city[c].dir, k_city[c].companion);
    return xp_file_size(a) > 0 && xp_file_size(b) > 0;   /* raw: translated on the way in */
}
int xpcity_installed(int pick)
{
    static int known[NCITY], ok[NCITY];   /* the files do not change while the game runs */
    int c = pick - 1;
    if (c < 0 || c >= NCITY) return pick == 0;
    if (!known[c]) { ok[c] = city_ok(c); known[c] = 1; }
    return ok[c];
}
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
static char g_bar_region[16] = "";
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
