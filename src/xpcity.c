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
#include <math.h>
#include <stdint.h>
#include <sys/stat.h>
#include <SDL.h>
#include "xpconv.h"
#include "xpcity.h"
#include "skyline.h"
#include "beyond.h"
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

static char g_bar_region[16] = "";   /* the race barriers' region (read below); "" = read again */
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
    else { beyond_events_reload(); g_bar_region[0] = 0; }   /* XF2: the new set (and barriers), not none until a restart */
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

static int g_failed[NCITY];   /* XF3: a translation that failed this session */
static int g_known[NCITY], g_ok[NCITY];   /* xpcity_installed: the files do not change while the game runs */
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
    if (g_failed[c]) { vita_mark("xpcity: %s failed to translate this session; not again until a restart", k_city[c].name); return 0; }
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
    {   /* XF3 11-22: the minimap and its parts were kept from the old translation (make_map skips a map that is there) */
        char q[1100];
        snprintf(q, sizeof q, "%s/%s.MAP", troot, k_city[c].companion); remove(q);
        snprintf(q, sizeof q, "%s/%s.SEA", troot, k_city[c].companion); remove(q);
        for (int a = 0; a < 8; a++) { snprintf(q, sizeof q, "%s/%s_A%d.MAP", troot, k_city[c].companion, a); remove(q); }
    }
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
        g_failed[c] = 1; g_known[c] = 0;   /* XF3: the picker stops offering it; no rerun on every pick */
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
        printf("xpcity: %s is translated for an older build (%s); the Prep Tool has to send it again%c", k_city[c].name, m, 10);
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
