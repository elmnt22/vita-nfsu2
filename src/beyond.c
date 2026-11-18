/* beyond.c -- the optional Beyond world: its switch, texture pack and start
 * point (see beyond.h). Everything here is unused unless beyond_enabled(). */
#include "beyond.h"
#include "world_tiles.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BEYOND_DIR "ux0:data/NFSU2/beyond"

/* BeyondDev converter/ug2x.h, version 1 */
typedef struct {
    uint32_t key;
    uint16_t w, h;
    uint8_t mips, fmt, alpha, cls;
    uint32_t offset, size, name, hash;
    uint8_t src, pad[3];
} BxEntry;
typedef struct { char magic[4]; uint32_t version, count, table, names, names_bytes, data, flags; } BxHeader;

#define BX_STR2(x) #x
#define BX_STR(x) BX_STR2(x)
static int g_bx_enabled = -1;
int g_beyond_pick;
static FILE *g_bx_f;
static BxEntry *g_bx_t;
static int g_bx_n, g_bx_tried;
static volatile int g_bx_lock;
/* the holder may be reading the file: a waiter backs off after a short spin,
   so a higher-priority thread on the same core cannot starve it (audit 11-17) */
static void bx_lock(void) { for (int n = 0; __sync_lock_test_and_set(&g_bx_lock, 1); n++) if (n > 64) SDL_Delay(1); }
static void bx_unlock(void) { __sync_lock_release(&g_bx_lock); }

static int bx_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int beyond_enabled(void) {
    if (g_bx_enabled < 0) {
        int sw = bx_exists(BEYOND_DIR "/enable.txt");
        int files = bx_exists(BEYOND_DIR "/BEYOND.ug2x") &&
                    bx_exists(WTILE_DIR "/STREAMBEYOND_v" BX_STR(WTILE_BEYOND_VERSION) ".idx") &&
                    bx_exists(WTILE_DIR "/STREAMBEYOND_v" BX_STR(WTILE_BEYOND_VERSION) ".pak");
        g_bx_enabled = sw && files;
        if (sw) printf("beyond: %s\n", files ? "on -- Quick Race Free Run offers the World Expansion"
                                             : "enable.txt found but the converted files are missing; off");
    }
    return g_bx_enabled;
}

int beyond_is_track(const char *t) {
    return t && (!strcmp(t, BEYOND_TRACK) || !strcmp(t, "BEYOND"));
}

/* the table, once (under the lock) */
static int bx_open(void) {
    if (g_bx_t) return 1;
    if (g_bx_tried) return 0;
    g_bx_tried = 1;
    FILE *f = fopen(BEYOND_DIR "/BEYOND.ug2x", "rb");
    BxHeader h;
    if (!f || fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "UG2X", 4) || h.version != 1 ||
        !h.count || h.count > 65536) {
        printf("beyond: BEYOND.ug2x missing or not a version 1 pack\n");
        if (f) fclose(f);
        return 0;
    }
    BxEntry *t = (BxEntry *)malloc(sizeof *t * h.count);
    if (!t || fseek(f, (long)h.table, SEEK_SET) || fread(t, sizeof *t, h.count, f) != h.count) {
        free(t); fclose(f);
        printf("beyond: BEYOND.ug2x table unreadable\n");
        return 0;
    }
    g_bx_f = f; g_bx_t = t; g_bx_n = (int)h.count;
    printf("beyond: texture pack, %d textures\n", g_bx_n);
    return 1;
}

static const BxEntry *bx_find(uint32_t key) {
    int lo = 0, hi = g_bx_n;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_bx_t[m].key < key) lo = m + 1; else hi = m; }
    return lo < g_bx_n && g_bx_t[lo].key == key ? &g_bx_t[lo] : NULL;
}

#define BX_KEEP 8
static struct { uint32_t key; N2Tex t; } g_bx_keep[BX_KEEP];
static int g_bx_nkeep;
static unsigned char *bx_dup(const unsigned char *p, size_t n) {
    if (!p || !n) return NULL;
    unsigned char *q = (unsigned char *)malloc(n);
    if (q) memcpy(q, p, n);
    return q;
}
static void bx_copy(N2Tex *d, const N2Tex *s) {
    size_t px = (size_t)s->w * s->h;
    *d = *s;
    d->rgb = bx_dup(s->rgb, px * 3);
    d->alpha = bx_dup(s->alpha, px);
    d->dxt = bx_dup(s->dxt, s->dxt ? (size_t)s->dxtlen : 0);
}
void beyond_keep_tex(uint32_t key, const N2Tex *t) {
    bx_lock();
    int have = 0;
    for (int i = 0; i < g_bx_nkeep; i++) have |= g_bx_keep[i].key == key;
    if (!have && key && g_bx_nkeep < BX_KEEP && t && (t->rgb || t->dxt)) {
        g_bx_keep[g_bx_nkeep].key = key;
        bx_copy(&g_bx_keep[g_bx_nkeep].t, t);
        g_bx_nkeep++;
        printf("beyond: kept the city's texture %08x (%dx%d) for the sky%s", (unsigned)key, t->w, t->h, "\n");
    }
    bx_unlock();
}
static int bx_keep_get(uint32_t key, N2Tex *t) {
    int ok = 0;
    bx_lock();
    for (int i = 0; i < g_bx_nkeep && !ok; i++)
        if (g_bx_keep[i].key == key) { bx_copy(t, &g_bx_keep[i].t); ok = 1; }
    bx_unlock();
    return ok;
}

int beyond_tex_decode(uint32_t key, N2Tex *t) {
    if (!key) return 0;
    bx_lock();
    BxEntry e; int have = 0;
    unsigned char *dxt = NULL;
    if (bx_open()) {
        const BxEntry *p = bx_find(key);
        if (p && (p->fmt == 1 || p->fmt == 3 || p->fmt == 5) && p->w >= 4 && p->h >= 4 && p->size) {
            e = *p;
            dxt = (unsigned char *)malloc(e.size);
            have = dxt && fseek(g_bx_f, (long)e.offset, SEEK_SET) == 0 && fread(dxt, 1, e.size, g_bx_f) == e.size;
        }
    }
    bx_unlock();
    if (!have) { free(dxt); return bx_keep_get(key, t); }
    size_t px = (size_t)e.w * e.h;
    unsigned char *rgb = (unsigned char *)malloc(px * 3);
    unsigned char *alpha = e.alpha ? (unsigned char *)malloc(px) : NULL;   /* opaque: the alpha bits hold other data */
    if (!rgb || (e.alpha && !alpha)) { free(rgb); free(alpha); free(dxt); return 0; }
    if (e.fmt == 1) n2_dxt1(dxt, e.w, e.h, rgb, alpha);
    else if (e.fmt == 3) n2_dxt3(dxt, e.w, e.h, rgb, alpha);
    else n2_dxt5(dxt, e.w, e.h, rgb, alpha);
    memset(t, 0, sizeof *t);
    t->w = e.w; t->h = e.h; t->rgb = rgb; t->alpha = alpha;
    t->dxt = dxt; t->dxtlen = (int)e.size; t->dxtfmt = e.fmt;
    t->afmt = e.alpha ? e.fmt : 0;
    /* the map's material: 1 alpha test, 2 blend, 3 additive glow */
    if (e.alpha == 1) { t->usage = 1; t->wz = 1; }
    else if (e.alpha == 2) { t->usage = 2; t->blend = 1; t->order = 5; }
    else if (e.alpha == 3) { t->blend = 2; t->order = 6; }
    else t->wz = 1;
    return 1;
}

const char *beyond_troot(const char *troot, const char *reg) {
    return reg && beyond_is_track(reg) && beyond_enabled() ? BEYOND_DIR : troot;
}
int beyond_spawn(float xy[2], float *heading_rad) {
    FILE *f = fopen(BEYOND_DIR "/spawn.txt", "r");
    if (!f) return 0;
    char line[256]; int ok = 0;
    while (!ok && fgets(line, sizeof line, f)) {
        float x, y, hd = 0.0f;
        if (line[0] == '#') continue;
        int n = sscanf(line, "%f %f %f", &x, &y, &hd);
        if (n >= 2 && isfinite(x) && isfinite(y)) {
            xy[0] = x; xy[1] = y;
            if (heading_rad) *heading_rad = n >= 3 ? hd * 3.14159265f / 180.0f : NAN;
            ok = 1;
        }
    }
    fclose(f);
    return ok;
}

void beyond_close(void) {
    bx_lock();
    if (g_bx_f) fclose(g_bx_f);
    free(g_bx_t);
    g_bx_f = NULL; g_bx_t = NULL; g_bx_n = 0; g_bx_tried = 0;
    bx_unlock();
}

/* ---- race events (ROUTESBEYOND/events.txt: "id type length_m district n") ----
   Several sets, each with its stem: the World Expansion's (BEYOND, when enabled) and each installed
   expansion city's (TRACKS/ROUTES<region>/events.txt from xproads/the Expansion Prep Tool: L2RA
   Rockport, L5RA Palmont). The track records take every set (beyond_event_all); the free-roam
   calls (beyond_event_count/_at/_start) see the set beyond_events_select picked (default BEYOND). */
#define BX_MAXEV 512
#define BX_XP_ROUTES "ux0:data/NFSU2/TRACKS"
static struct { int id, type, len, has_start; float start[3], head; char name[40], stem[8]; } g_bx_ev[BX_MAXEV];
static int g_bx_nev = -1;
static short g_bx_view[BX_MAXEV];
static int g_bx_nview = -1;
static char g_bx_sel[8] = "BEYOND";
static int g_bx_nshop;                 /* the selected set's shops (defined below) */
static void bx_events_set(const char *dir, const char *stem) {
    char p[200], line[160];
    snprintf(p, sizeof p, "%s/events.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    int first = g_bx_nev;
    while (g_bx_nev < BX_MAXEV && fgets(line, sizeof line, f)) {
        int id, type, len, num = 0, used = 0;
        if (sscanf(line, "%d %d %d %n", &id, &type, &len, &used) < 3 || id < 4000 || id > 7999) continue;
        char *nm = line + used, *e = nm + strlen(nm);
        while (e > nm && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char *sp = strrchr(nm, ' ');                     /* the trailing number */
        if (sp && sscanf(sp + 1, "%d", &num) == 1) *sp = 0;
        memset(&g_bx_ev[g_bx_nev], 0, sizeof g_bx_ev[g_bx_nev]);
        g_bx_ev[g_bx_nev].id = id; g_bx_ev[g_bx_nev].type = type; g_bx_ev[g_bx_nev].len = len;
        snprintf(g_bx_ev[g_bx_nev].name, sizeof g_bx_ev[g_bx_nev].name, num > 0 ? "%s %d" : "%s", nm, num);
        snprintf(g_bx_ev[g_bx_nev].stem, sizeof g_bx_ev[g_bx_nev].stem, "%s", stem);
        g_bx_nev++;
    }
    fclose(f);
    int ns = 0;
    snprintf(p, sizeof p, "%s/starts.txt", dir);
    if ((f = fopen(p, "r")) != NULL) {   /* "id x y z heading_deg" */
        while (fgets(line, sizeof line, f)) {
            int id; float x, y, z, hd;
            if (sscanf(line, "%d %f %f %f %f", &id, &x, &y, &z, &hd) != 5) continue;
            for (int i = first; i < g_bx_nev; i++)
                if (g_bx_ev[i].id == id) {
                    g_bx_ev[i].start[0] = x; g_bx_ev[i].start[1] = y; g_bx_ev[i].start[2] = z;
                    g_bx_ev[i].head = hd * 3.14159265f / 180.0f; g_bx_ev[i].has_start = 1; ns++;
                }
        }
        fclose(f);
    }
    printf("beyond: %s: %d race events, %d free-roam starts\n", stem, g_bx_nev - first, ns);
}
static void bx_events_load(void) {
    if (g_bx_nev >= 0) return;
    g_bx_nev = 0;
    if (beyond_enabled()) bx_events_set(BEYOND_DIR "/ROUTESBEYOND", "BEYOND");
    static const char *const xp[] = { "L2RA", "L5RA" };
    for (int k = 0; k < 2; k++) {
        char d[160]; snprintf(d, sizeof d, BX_XP_ROUTES "/ROUTES%s", xp[k]);
        bx_events_set(d, xp[k]);
    }
}
static void bx_view(void) {
    bx_events_load();
    if (g_bx_nview >= 0) return;
    g_bx_nview = 0;
    for (int i = 0; i < g_bx_nev; i++) if (!strcmp(g_bx_ev[i].stem, g_bx_sel)) g_bx_view[g_bx_nview++] = (short)i;
}
void beyond_events_select(const char *stem) {
    if (!stem) stem = "BEYOND";
    if (!strcmp(stem, g_bx_sel)) return;
    snprintf(g_bx_sel, sizeof g_bx_sel, "%s", stem);
    g_bx_nview = -1;
    g_bx_nshop = -1;   /* that set's shops */
}
int beyond_event_all(int i, int *id, int *type, int *len, const char **stem) {
    bx_events_load();
    if (i < 0) return g_bx_nev;
    if (i >= g_bx_nev) return 0;
    if (id) *id = g_bx_ev[i].id;
    if (type) *type = g_bx_ev[i].type;
    if (len) *len = g_bx_ev[i].len;
    if (stem) *stem = g_bx_ev[i].stem;
    return 1;
}

/* ---- shops and the garage (ROUTESBEYOND/shops.txt: "type x y z heading_deg") ----
   The selected set's (beyond_events_select): an expansion city's from its
   TRACKS/ROUTES<region>/shops.txt (xpevents.inc: MW's menu gates) */
#define BX_MAXSHOP 32
static struct { int type; float p[3], head; } g_bx_shop[BX_MAXSHOP];
static int g_bx_nshop = -1;
static void bx_shops_load(void) {
    if (g_bx_nshop >= 0) return;
    g_bx_nshop = 0;
    char path[200];
    if (!strcmp(g_bx_sel, "BEYOND")) {
        if (!beyond_enabled()) return;
        snprintf(path, sizeof path, BEYOND_DIR "/ROUTESBEYOND/shops.txt");
    } else snprintf(path, sizeof path, BX_XP_ROUTES "/ROUTES%s/shops.txt", g_bx_sel);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (g_bx_nshop < BX_MAXSHOP && fgets(line, sizeof line, f)) {
        int t; float x, y, z, hd;
        if (sscanf(line, "%d %f %f %f %f", &t, &x, &y, &z, &hd) != 5 || t < 0 || t > 6) continue;
        g_bx_shop[g_bx_nshop].type = t;
        g_bx_shop[g_bx_nshop].p[0] = x; g_bx_shop[g_bx_nshop].p[1] = y; g_bx_shop[g_bx_nshop].p[2] = z;
        g_bx_shop[g_bx_nshop].head = hd * 3.14159265f / 180.0f;
        g_bx_nshop++;
    }
    fclose(f);
    printf("beyond: %s: %d shops\n", g_bx_sel, g_bx_nshop);
}
int beyond_shop_count(void) { bx_shops_load(); return g_bx_nshop; }
int beyond_shop_at(int i, int *type, float p[3], float *head) {
    bx_shops_load();
    if (i < 0 || i >= g_bx_nshop) return 0;
    if (type) *type = g_bx_shop[i].type;
    if (p) memcpy(p, g_bx_shop[i].p, sizeof g_bx_shop[i].p);
    if (head) *head = g_bx_shop[i].head;
    return 1;
}
int beyond_event_count(void) { bx_view(); return g_bx_nview; }
int beyond_event_at(int i, int *id, int *type, int *len) {
    bx_view();
    if (i < 0 || i >= g_bx_nview) return 0;
    i = g_bx_view[i];
    if (id) *id = g_bx_ev[i].id;
    if (type) *type = g_bx_ev[i].type;
    if (len) *len = g_bx_ev[i].len;
    return 1;
}
static int bx_ev_index(int id) {
    if (id < 4000) return -1;
    bx_events_load();
    for (int i = 0; i < g_bx_nev; i++) if (g_bx_ev[i].id == id) return i;
    return -1;
}
int beyond_event_start(int i, float p[3], float *head) {
    bx_view();
    if (i < 0 || i >= g_bx_nview || !g_bx_ev[g_bx_view[i]].has_start) return 0;
    i = g_bx_view[i];
    memcpy(p, g_bx_ev[i].start, sizeof g_bx_ev[i].start);
    if (head) *head = g_bx_ev[i].head;
    return 1;
}
int beyond_event_type(int id) { int i = bx_ev_index(id); return i < 0 ? -1 : g_bx_ev[i].type; }
const char *beyond_event_name(int id) { int i = bx_ev_index(id); return i < 0 ? NULL : g_bx_ev[i].name; }
/* the Quick Race track map of a Beyond event: ROUTESBEYOND/TRACKMAP<id>.bin
   (BeyondDev tools/events/trackmaps.py, the minimap's BMAP format), RGBA,
   malloc'd; NULL when none */
/* an expansion city's event (port): drawn here like trackmaps.py does it -- the city's
   minimap (TRACKS/<region>.MAP) framed on the route, dimmed, the route white with a
   dark edge (ROUTES<region>/trackpoly.bin, xpevents.inc), a green dot at the start */
static float *bx_tp_route(const char *stem, int id, int *n, int *closed)
{
    char p[200]; snprintf(p, sizeof p, BX_XP_ROUTES "/ROUTES%s/trackpoly.bin", stem);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    uint32_t hd[2]; float *pl = NULL;
    if (fread(hd, 4, 2, f) == 2 && hd[0] == 0x4c505458u)
        for (uint32_t i = 0; i < hd[1] && !pl; i++) {
            int32_t eh[3];
            if (fread(eh, 4, 3, f) != 3 || eh[2] < 0 || eh[2] > 100000) break;
            if (eh[0] != id) { if (fseek(f, (long)eh[2] * 8, SEEK_CUR)) break; continue; }
            if (eh[2] >= 2 && (pl = (float *)malloc((size_t)eh[2] * 8)) && fread(pl, 8, (size_t)eh[2], f) != (size_t)eh[2]) { free(pl); pl = NULL; }
            if (pl) { *n = eh[2]; *closed = eh[1]; }
            break;
        }
    fclose(f);
    return pl;
}
static void bx_tp_line(unsigned char *px, int S, float ax, float ay, float bx, float by, float r, const unsigned char rgb[3])
{
    int x0 = (int)floorf(fminf(ax, bx) - r - 1), x1 = (int)ceilf(fmaxf(ax, bx) + r + 1);
    int y0 = (int)floorf(fminf(ay, by) - r - 1), y1 = (int)ceilf(fmaxf(ay, by) + r + 1);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > S - 1) x1 = S - 1; if (y1 > S - 1) y1 = S - 1;
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float qx = x + 0.5f - ax, qy = y + 0.5f - ay;
            float t = l2 > 0 ? (qx * dx + qy * dy) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ex = qx - t * dx, ey = qy - t * dy, d = sqrtf(ex * ex + ey * ey);
            float a = r + 0.5f - d; if (a <= 0) continue; if (a > 1) a = 1;
            unsigned char *o = px + 4 * ((size_t)y * S + x);
            for (int k = 0; k < 3; k++) o[k] = (unsigned char)(o[k] + (rgb[k] - o[k]) * a);
        }
}
static unsigned char *bx_tp_draw(const char *stem, int id, int *w, int *h)
{
    int n = 0, closed = 0;
    float *pl = bx_tp_route(stem, id, &n, &closed);
    if (!pl) return NULL;
    char p[200]; snprintf(p, sizeof p, BX_XP_ROUTES "/%s.MAP", stem);
    FILE *f = fopen(p, "rb");
    unsigned char hd[28]; uint32_t MW = 0, MH = 0; float fl[4] = { 0 };
    unsigned char *mm = NULL;
    if (f && fread(hd, 1, 28, f) == 28 && !memcmp(hd, "BMAP", 4)) {
        memcpy(&MW, hd + 4, 4); memcpy(&MH, hd + 8, 4); memcpy(fl, hd + 12, 16);
        if (MW && MH && MW <= 4096 && MH <= 4096 && fl[2] > 0 && (mm = (unsigned char *)malloc((size_t)MW * MH * 4)) &&
            fread(mm, 4, (size_t)MW * MH, f) != (size_t)MW * MH) { free(mm); mm = NULL; }
    }
    if (f) fclose(f);
    enum { S = 256 };
    unsigned char *px = mm ? (unsigned char *)malloc((size_t)S * S * 4) : NULL;
    if (!px) { free(mm); free(pl); return NULL; }
    float minx = pl[0], maxx = pl[0], miny = pl[1], maxy = pl[1];
    for (int i = 1; i < n; i++) {
        minx = fminf(minx, pl[2 * i]); maxx = fmaxf(maxx, pl[2 * i]);
        miny = fminf(miny, pl[2 * i + 1]); maxy = fmaxf(maxy, pl[2 * i + 1]);
    }
    float span = fmaxf(maxx - minx, maxy - miny) * 1.25f + 200.0f;
    float x0 = (minx + maxx) * 0.5f - span * 0.5f, y0 = (miny + maxy) * 0.5f + span * 0.5f;   /* top-left, y up */
    for (int y = 0; y < S; y++)            /* the minimap's crop, bilinear, dimmed (0.55) */
        for (int x = 0; x < S; x++) {
            float wx = x0 + (x + 0.5f) / S * span, wy = y0 - (y + 0.5f) / S * span;
            float u = (wx - fl[0]) / fl[2] * MW - 0.5f, v = (fl[1] - wy) / fl[2] * MH - 0.5f;
            int iu = (int)floorf(u), iv = (int)floorf(v); float fu = u - iu, fv = v - iv;
            unsigned char *o = px + 4 * ((size_t)y * S + x);
            for (int k = 0; k < 3; k++) {
                float c = 0;
                for (int j = 0; j < 4; j++) {
                    int su = iu + (j & 1), sv = iv + (j >> 1);
                    if (su < 0) su = 0; if (sv < 0) sv = 0; if (su >= (int)MW) su = MW - 1; if (sv >= (int)MH) sv = MH - 1;
                    c += mm[4 * ((size_t)sv * MW + su) + k] * ((j & 1) ? fu : 1 - fu) * ((j >> 1) ? fv : 1 - fv);
                }
                o[k] = (unsigned char)(c * 0.55f);
            }
            o[3] = 255;
        }
    free(mm);
    static const unsigned char dark[3] = { 15, 15, 15 }, white[3] = { 255, 255, 255 }, green[3] = { 120, 230, 60 };
    int ns = closed ? n : n - 1;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ns; i++) {
            int j = (i + 1) % n;
            bx_tp_line(px, S, (pl[2 * i] - x0) / span * S, (y0 - pl[2 * i + 1]) / span * S,
                       (pl[2 * j] - x0) / span * S, (y0 - pl[2 * j + 1]) / span * S, pass ? 0.7f : 1.7f, pass ? white : dark);
        }
    float sx = (pl[0] - x0) / span * S, sy = (y0 - pl[1]) / span * S;
    bx_tp_line(px, S, sx, sy, sx, sy, 5.5f, dark);
    bx_tp_line(px, S, sx, sy, sx, sy, 3.5f, green);
    free(pl);
    *w = S; *h = S;
    return px;
}
unsigned char *beyond_trackmap(int id, int *w, int *h) {
    int ei = bx_ev_index(id);
    if (ei < 0) return NULL;
    if (strcmp(g_bx_ev[ei].stem, "BEYOND")) return bx_tp_draw(g_bx_ev[ei].stem, id, w, h);
    char p[160]; snprintf(p, sizeof p, BEYOND_DIR "/ROUTESBEYOND/TRACKMAP%d.bin", id);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    unsigned char hd[28]; uint32_t W = 0, H = 0;
    unsigned char *px = NULL;
    if (fread(hd, 1, 28, f) == 28 && !memcmp(hd, "BMAP", 4)) {
        memcpy(&W, hd + 4, 4); memcpy(&H, hd + 8, 4);
        if (W && H && W <= 1024 && H <= 1024 && (px = (unsigned char *)malloc((size_t)W * H * 4)) &&
            fread(px, 4, (size_t)W * H, f) != (size_t)W * H) { free(px); px = NULL; }
    }
    fclose(f);
    if (px) { *w = (int)W; *h = (int)H; }
    return px;
}

/* ---- progress (port): beyond/progress.txt, apart from the UG2 save ----
   one line per raced event: "id won best_ticks best_place" (ticks 1/4000 s,
   0 = no time, e.g. a drift) */
#define BX_PROG BEYOND_DIR "/progress.txt"
static struct { int id, won, best, place; } g_bx_prog[BX_MAXEV];
static int g_bx_progn = -1;
static void bx_prog_load(void) {
    if (g_bx_progn >= 0) return;
    g_bx_progn = 0;
    FILE *f = fopen(BX_PROG, "r");
    if (!f) return;
    while (g_bx_progn < BX_MAXEV &&
           fscanf(f, "%d %d %d %d", &g_bx_prog[g_bx_progn].id, &g_bx_prog[g_bx_progn].won,
                  &g_bx_prog[g_bx_progn].best, &g_bx_prog[g_bx_progn].place) == 4)
        g_bx_progn++;
    fclose(f);
}
int beyond_progress(int id, int *won, int *best_ticks, int *best_place) {
    bx_prog_load();
    for (int i = 0; i < g_bx_progn; i++)
        if (g_bx_prog[i].id == id) {
            if (won) *won = g_bx_prog[i].won;
            if (best_ticks) *best_ticks = g_bx_prog[i].best;
            if (best_place) *best_place = g_bx_prog[i].place;
            return 1;
        }
    return 0;
}
int beyond_progress_won_count(void) {
    bx_prog_load();
    int n = 0;
    for (int i = 0; i < g_bx_progn; i++) n += g_bx_prog[i].won != 0;
    return n;
}
int beyond_progress_record(int id, int won, int ticks, int place) {
    if (beyond_event_type(id) < 0) return 0;
    bx_prog_load();
    int i = 0;
    while (i < g_bx_progn && g_bx_prog[i].id != id) i++;
    if (i == g_bx_progn) {
        if (g_bx_progn == BX_MAXEV) return 0;
        g_bx_prog[i].id = id; g_bx_prog[i].won = 0; g_bx_prog[i].best = 0; g_bx_prog[i].place = 0;
        g_bx_progn++;
    }
    if (won) g_bx_prog[i].won = 1;
    if (ticks > 0 && (!g_bx_prog[i].best || ticks < g_bx_prog[i].best)) g_bx_prog[i].best = ticks;
    if (place > 0 && (!g_bx_prog[i].place || place < g_bx_prog[i].place)) g_bx_prog[i].place = place;
    mkdir(BEYOND_DIR, 0777);   /* an expansion-only install has no beyond folder yet */
    const char *tmp = BX_PROG ".tmp";
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    int ok = 1;
    for (int k = 0; k < g_bx_progn; k++)
        if (fprintf(f, "%d %d %d %d\n", g_bx_prog[k].id, g_bx_prog[k].won, g_bx_prog[k].best, g_bx_prog[k].place) < 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (ok) { remove(BX_PROG); ok = rename(tmp, BX_PROG) == 0; }
    return ok;
}
