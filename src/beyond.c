/* beyond.c -- the optional Beyond world: its switch, texture pack and start
 * point (see beyond.h). Everything here is unused unless beyond_enabled(). */
#include "beyond.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int g_bx_enabled = -1;
int g_beyond_pick;
static FILE *g_bx_f;
static BxEntry *g_bx_t;
static int g_bx_n, g_bx_tried;
static volatile int g_bx_lock;
static void bx_lock(void) { while (__sync_lock_test_and_set(&g_bx_lock, 1)) {} }
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
                    bx_exists("ux0:data/NFSU2/cache/tiles/STREAMBEYOND_v6.idx") &&
                    bx_exists("ux0:data/NFSU2/cache/tiles/STREAMBEYOND_v6.pak");
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

/* ---- race events (ROUTESBEYOND/events.txt: "id type length_m district n") ---- */
#define BX_MAXEV 128
static struct { int id, type, len, has_start; float start[3], head; char name[40]; } g_bx_ev[BX_MAXEV];
static int g_bx_nev = -1;
static void bx_events_load(void) {
    if (g_bx_nev >= 0) return;
    g_bx_nev = 0;
    if (!beyond_enabled()) return;
    FILE *f = fopen(BEYOND_DIR "/ROUTESBEYOND/events.txt", "r");
    if (!f) return;
    char line[160];
    while (g_bx_nev < BX_MAXEV && fgets(line, sizeof line, f)) {
        int id, type, len, num = 0, used = 0;
        if (sscanf(line, "%d %d %d %n", &id, &type, &len, &used) < 3 || id < 4000 || id > 7999) continue;
        char *nm = line + used, *e = nm + strlen(nm);
        while (e > nm && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char *sp = strrchr(nm, ' ');                     /* the trailing number */
        if (sp && sscanf(sp + 1, "%d", &num) == 1) *sp = 0;
        g_bx_ev[g_bx_nev].id = id; g_bx_ev[g_bx_nev].type = type; g_bx_ev[g_bx_nev].len = len;
        snprintf(g_bx_ev[g_bx_nev].name, sizeof g_bx_ev[g_bx_nev].name, num > 0 ? "%s %d" : "%s", nm, num);
        g_bx_nev++;
    }
    fclose(f);
    int ns = 0;
    if ((f = fopen(BEYOND_DIR "/ROUTESBEYOND/starts.txt", "r")) != NULL) {   /* "id x y z heading_deg" */
        while (fgets(line, sizeof line, f)) {
            int id; float x, y, z, hd;
            if (sscanf(line, "%d %f %f %f %f", &id, &x, &y, &z, &hd) != 5) continue;
            for (int i = 0; i < g_bx_nev; i++)
                if (g_bx_ev[i].id == id) {
                    g_bx_ev[i].start[0] = x; g_bx_ev[i].start[1] = y; g_bx_ev[i].start[2] = z;
                    g_bx_ev[i].head = hd * 3.14159265f / 180.0f; g_bx_ev[i].has_start = 1; ns++;
                }
        }
        fclose(f);
    }
    printf("beyond: %d race events, %d free-roam starts\n", g_bx_nev, ns);
}

/* ---- shops and the garage (ROUTESBEYOND/shops.txt: "type x y z heading_deg") ---- */
#define BX_MAXSHOP 32
static struct { int type; float p[3], head; } g_bx_shop[BX_MAXSHOP];
static int g_bx_nshop = -1;
static void bx_shops_load(void) {
    if (g_bx_nshop >= 0) return;
    g_bx_nshop = 0;
    if (!beyond_enabled()) return;
    FILE *f = fopen(BEYOND_DIR "/ROUTESBEYOND/shops.txt", "r");
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
    printf("beyond: %d shops\n", g_bx_nshop);
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
int beyond_event_count(void) { bx_events_load(); return g_bx_nev; }
int beyond_event_at(int i, int *id, int *type, int *len) {
    bx_events_load();
    if (i < 0 || i >= g_bx_nev) return 0;
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
    bx_events_load();
    if (i < 0 || i >= g_bx_nev || !g_bx_ev[i].has_start) return 0;
    memcpy(p, g_bx_ev[i].start, sizeof g_bx_ev[i].start);
    if (head) *head = g_bx_ev[i].head;
    return 1;
}
int beyond_event_type(int id) { int i = bx_ev_index(id); return i < 0 ? -1 : g_bx_ev[i].type; }
const char *beyond_event_name(int id) { int i = bx_ev_index(id); return i < 0 ? NULL : g_bx_ev[i].name; }
/* the Quick Race track map of a Beyond event: ROUTESBEYOND/TRACKMAP<id>.bin
   (BeyondDev tools/events/trackmaps.py, the minimap's BMAP format), RGBA,
   malloc'd; NULL when none */
unsigned char *beyond_trackmap(int id, int *w, int *h) {
    if (bx_ev_index(id) < 0) return NULL;
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
