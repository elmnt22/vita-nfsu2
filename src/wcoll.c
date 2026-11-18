/* wcoll.c — the retail NFSU2 collision world (see wcoll.h). */
#include "wcoll.h"
#include "resource.h"
#include "SDL.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- retail surface table 0x800ee0 -------------------------------------- */
const WCollSurface wcoll_surfaces[WCOLL_NSURF] = {
    /* name               skidw bumpf bumpa  grip   slide    roll  drag  prio */
    { "NONE",             1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD",             1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_WET",         1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_DRIFT",       1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_SMOKE_1",     1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_SMOKE_2",     1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_SMOKE_3",     1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "ROAD_BRIDGE",      1,    1,    0,     1,     1,       1,    1,    0.1f },
    { "DIRT",             1.4f, 1,    0.05f, 0.97f, 0.957875f, 1,  1,    0.5f },
    { "GRAVEL",           1.4f, 1,    0.02f, 0.99f, 0.9813f, 1,    1,    0.2f },
    { "ROUGH_ROAD",       1,    1,    0.02f, 1,     1,       1.01f, 1,   0.1f },
    { "COBBLESTONE",      1,    3,    0.03f, 1,     1,       1.02f, 1,   0.1f },
    { "STAIRS",           1,    3,    0.048f, 1,    1,       1.02f, 1,   0.1f },
    { "PUDDLE",           1,    1,    0.01f, 0.96f, 0.9513f, 1.1f, 1,    1    },
    { "DEEP_WATER",       1,    1,    0.03f, 0.96f, 0.948f,  1.5f, 1,    1    },
    { "GRASS",            1.4f, 1,    0.05f, 0.96f, 0.948f,  1.05f, 5,   1    },
    { "SIDEWALK",         1,    1.1f, 0.03f, 1,     1,       1,    1,    0.1f },
    { "WOOD",             1,    1,    0.01f, 1,     1,       1,    1,    0.5f },
    { "PLASTIC",          1,    1,    0.01f, 1,     1,       1,    1,    0.5f },
    { "GLASS",            1,    1,    0.01f, 1,     1,       1,    1,    0.5f },
    { "SOLID_WALL",       1,    1,    0.01f, 1,     1,       1,    1,    0.1f },
    { "SEE_THROUGH_WALL", 1,    1,    0.01f, 1,     1,       1,    1,    0.1f },
    { "PLANT",            1,    1,    0,     0.99f, 0.981f,  1,    1,    1    },
    { "POST",             1,    1,    0,     0.99f, 0.981f,  1,    1,    0.2f },
    { "PILLAR",           1,    1,    0,     0.99f, 0.981f,  1,    1,    0.2f },
    { "METAL_GRATE",      1,    1,    0,     0.99f, 0.9813f, 1,    1,    0    },
    { "METAL",            1,    1,    0,     0.99f, 0.981f,  1,    1,    0.2f },
    { "CHAINLINK",        1,    1,    0,     0.99f, 0.981f,  1,    1,    0.2f },
    { "CAR",              1,    1,    0,     0.99f, 0.9813f, 1,    1,    0    },
};

/* 0x5d3ca0: bilinear value noise on a 1/freq lattice, values from 0x800bf8 */
float wcoll_bump(int surface, float x, float y)
{
    static const float T[16] = { -0.5f, -0.2f, 0.5f, 0.2f, -0.4f, -0.1f, 0.2f, 0.5f,
                                 -0.2f, -0.5f, 0.4f, -0.3f, 0.3f, 0.0f, -0.2f, 0.1f };
    if (surface < 0 || surface >= WCOLL_NSURF) surface = 0;
    const WCollSurface *s = &wcoll_surfaces[surface];
    float X = s->bump_freq * x, Y = s->bump_freq * y;
    float fx = (float)(int)X; if (fx > X) fx -= 1.0f;
    float fy = (float)(int)Y; if (fy > Y) fy -= 1.0f;
    int a = (int)fx, b = (int)fy;
    float tx = X - fx, ty = Y - fy;
#define H(i, j) T[(unsigned)(-5 * (i) * (j) - 3 * ((i) + 1) * ((j) + 1)) & 15]
    float v = H(a, b) * (1 - tx) * (1 - ty) + H(a + 1, b) * tx * (1 - ty)
            + H(a, b + 1) * (1 - tx) * ty + H(a + 1, b + 1) * tx * ty;
#undef H
    return v * s->bump_amp;
}

/* ---- loaded data -------------------------------------------------------- */
typedef struct {
    int section;
    int W, H;
    float x0, y0;
    const unsigned char *poly;   /* 32-byte compact polygons (0x34134) */
    int npoly;
    const unsigned char *list;   /* u16 poly, u16 sub-cell mask (0x34136) */
    int nlist;
    const unsigned char *cells;  /* u16 start, u16 count, W*H (0x34137) */
    int ncells;
    float ox, oy;                /* (port) polygon xy origin: 0 in UG2, the 0x341f0 origin in an
                                    expansion city (xpconv.c; Rockport runs past +-4096 m) */
    unsigned char *data;         /* owned chunk payload */
} Pack;

typedef struct {
    int section;
    float bb[4];                 /* min x, min y, max x, max y */
    float *xy;
    int n;
} Bound;

typedef struct {
    uint32_t hash;
    char name[32];
} Group;

#define WC_NSLOT 1040            /* 26 section letters x 20 (UG2) or x 40 (an expansion city) */

/* a drivable section's visible-section record (chunk 0x34153, 0xa4 bytes:
   +8 s16 section, +0xc its boundary (FindBoundary, filled at load), +0x10
   count, +0x14 the visible sections as s16, at most 72) */
typedef struct {
    int section, bnd, n;
    short vis[72];
} VisRec;

struct WColl {
    Pack *packs; int npacks;
    VisRec *vis; int nvis;
    int *vorder;                 /* VisibleSectionManager +0x10: the drivable sections, most recently found first */
    int slot[WC_NSLOT];          /* 0x88f768: pack per section slot (0x5d3c10) */
    Bound *bnd; int nbnd;
    int *order;                  /* the boundary list, most recently found first */
    Group *groups; int ngroups;
    unsigned char *enabled;      /* the world's +0x2f0 list as a flag per group */
    int xp;                      /* (port) an expansion city (MW / Carbon, translated by xpconv.c):
                                    drivable sections x01..x39, each pack numbered as its
                                    section (no x20.. copies), MW visible-section records */
};

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rds16(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static uint32_t str_hash(const char *s)                                 /* 0x43db50 */
{
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}

/* 0x5d3c10: section number -> slot. Sections x20..x39 are the collision
 * copies of drivable sections x00..x19 (offset 0x800dfc = 20 in game). */
static int section_slot(const WColl *w, int s)
{
    if (s == 0) return 0;
    int m = s % 100;
    if (w->xp) { int i = m + (s / 100 - 1) * 40; return m < 40 && i >= 0 && i < WC_NSLOT ? i : -1; }
    if (m >= 20 && m < 40) m -= 20;
    int i = m + (s / 100 - 1) * 20;
    return i >= 0 && i < WC_NSLOT ? i : -1;
}

static unsigned char *read_at(FILE *f, long off, long n)
{
    unsigned char *b = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (!b) return NULL;
    if (fseek(f, off, SEEK_SET) || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); return NULL; }
    return b;
}

static int g_wc_oom;   /* an allocation failed in wcoll_load / load_bounds */
static void load_bounds(WColl *w, const unsigned char *d, long len)
{
    int cap = 0;
    for (long o = 0; o + 8 <= len; ) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        if ((unsigned long)sz > (unsigned long)(len - o - 8)) break;   /* no signed wrap on a huge size */
        long end = o + 8 + (long)sz;
        if (id == 0x34152) {
            long q = o + 8;
            while (q + 0x24 <= end) {
                if (rd32(d + q) == 0x11111111u || rd32(d + q) == 0) { q += 4; continue; }
                int sec = rds16(d + q + 8), n = d[q + 10];   /* +11 = MW flag byte, 0 in UG2 */
                if (n < 1 || q + 0x24 + n * 8 > end) break;
                /* Only drivable sections (x00..x19; x01..x39 in an expansion city) are tracked; the x41..,
                   x98, x99 boundaries are scenery and region outlines. */
                if (sec % 100 < (w->xp ? 40 : 20)) {
                    if (w->nbnd == cap) {
                        int nc = cap ? cap * 2 : 256;
                        Bound *nb = (Bound *)realloc(w->bnd, (size_t)nc * sizeof *w->bnd);
                        if (!nb) { g_wc_oom = 1; break; }
                        w->bnd = nb; cap = nc;
                    }
                    Bound *b = &w->bnd[w->nbnd];
                    b->section = sec; b->n = n;
                    for (int k = 0; k < 4; k++) b->bb[k] = rdf(d + q + 0xc + (k & 1) * 4 + (k >> 1) * 8);
                    b->xy = (float *)malloc((size_t)n * 8);
                    if (!b->xy) { g_wc_oom = 1; break; }
                    w->nbnd++;
                    for (int k = 0; k < 2 * n; k++) b->xy[k] = rdf(d + q + 0x24 + k * 4);
                }
                q += 0x24 + n * 8;
            }
        } else if (id == 0x34153 && !w->vis && w->xp) {
            /* MW's records are not UG2's fixed 0xa4: +0xc s16 section, +0xe u8
               count, +0xf u8 capacity, +0x12 the sections as s16, each record
               padded to 4 bytes */
            int cnt = 0;
            for (long q = o + 8; q + 0x12 <= end; q += (0x12 + 2 * d[q + 0xf] + 3) & ~3L) cnt++;
            w->vis = (VisRec *)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof *w->vis);
            for (long q = o + 8; w->vis && q + 0x12 <= end; q += (0x12 + 2 * d[q + 0xf] + 3) & ~3L) {
                VisRec *v = &w->vis[w->nvis++];
                int n = d[q + 0xe];
                if (q + 0x12 + 2 * n > end) n = 0;
                v->section = rds16(d + q + 0xc); v->bnd = -1;
                v->n = n > 72 ? 72 : n;
                for (int k = 0; k < v->n; k++) v->vis[k] = rds16(d + q + 0x12 + 2 * k);
            }
        } else if (id == 0x34153 && !w->vis) {
            /* VisibleSectionManager::Loader (GC 8020b628): the drivable
               sections' visible-section lists */
            int cnt = (int)(sz / 0xa4);
            w->vis = (VisRec *)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof *w->vis);
            for (int i = 0; w->vis && i < cnt; i++) {
                const unsigned char *r = d + o + 8 + 0xa4 * i;
                VisRec *v = &w->vis[w->nvis++];
                int n = (int)rd32(r + 0x10);
                v->section = rds16(r + 8); v->bnd = -1;
                v->n = n < 0 ? 0 : n > 72 ? 72 : n;
                for (int k = 0; k < v->n; k++) v->vis[k] = rds16(r + 0x14 + 2 * k);
            }
        } else if (id == 0x80034150u) {
            load_bounds(w, d + o + 8, sz);
        } else if (id == 0x3412f) {
            /* barrier groups (0x5e78a0 -> 0x88ff90/94): 0x20-byte records,
               name hash +0, name +9; a polygon's group byte indexes this
               table (index 0 "NONE"). Not the 0x34108 scenery groups, whose
               order differs. */
            int cnt = (int)(sz >> 5);
            free(w->groups);
            w->groups = (Group *)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof *w->groups);
            if (!w->groups) { g_wc_oom = 1; cnt = 0; }
            w->ngroups = cnt;
            for (int i = 0; i < cnt; i++) {
                const unsigned char *r = d + o + 8 + 32 * i;
                w->groups[i].hash = rd32(r);
                memcpy(w->groups[i].name, r + 9, 23); w->groups[i].name[23] = 0;
            }
        }
        o = end;
    }
}

WColl *wcoll_load(const char *stream_bun, const char *track_bun)
{
    FILE *f = fopen(stream_bun, "rb");
    if (!f) return NULL;
    g_wc_oom = 0;
    WColl *w = (WColl *)calloc(1, sizeof *w);
    if (!w) { fclose(f); printf("wcoll: out of memory, no collision for %s\n", stream_bun); return NULL; }
    for (int i = 0; i < WC_NSLOT; i++) w->slot[i] = -1;
    int cap = 0;
    long off = 0;
    unsigned char hdr[8];
    /* the bundle's chunk index (resource.c): only the collision packs are read */
    int nix = 0;
    ResChunk *ix = res_chunk_index(stream_bun, &nix);
    for (int k = 0; ix ? k < nix : 1; k++) {
        uint32_t id, sz;
        if (ix) { id = ix[k].id; sz = ix[k].size; off = (long)ix[k].off; }
        else {
            if (fseek(f, off, SEEK_SET) != 0 || fread(hdr, 1, 8, f) != 8) break;
            id = rd32(hdr); sz = rd32(hdr + 4);
        }
        if (id == 0x80034130u) {
            /* keep the payload's file alignment: the header sub-chunk is
               found by aligning its absolute offset */
            long base = off + 8, pad = base & 15;
            unsigned char *raw = read_at(f, base - pad, (long)sz + pad);
            if (raw && w->npacks == cap) {
                int nc = cap ? cap * 2 : 64;
                Pack *np = (Pack *)realloc(w->packs, (size_t)nc * sizeof *w->packs);
                if (np) { w->packs = np; cap = nc; } else { free(raw); raw = NULL; g_wc_oom = 1; }
            }
            if (raw) {
                Pack *p = &w->packs[w->npacks];
                /* 0x5d8140; raw starts on a 16-byte file boundary */
                memset(p, 0, sizeof *p);
                long o = pad;
                while (o + 8 <= pad + (long)sz) {
                    uint32_t cid = rd32(raw + o), csz = rd32(raw + o + 4);
                    if (o + 8 + (long)csz > pad + (long)sz) break;
                    const unsigned char *b = raw + o + 8;
                    if (cid == 0x34131) {
                        const unsigned char *h = raw + ((o + 0x17) & ~15L);
                        p->section = rd16(h);
                        p->W = rds16(h + 4); p->H = rds16(h + 6);
                        p->x0 = rdf(h + 8); p->y0 = rdf(h + 0xc);
                    } else if (cid == 0x34134) { p->poly = b; p->npoly = (int)(csz >> 5); }
                    else if (cid == 0x34136) { p->list = b; p->nlist = (int)(csz >> 2); }
                    else if (cid == 0x34137) { p->cells = b; p->ncells = (int)(csz >> 2); }
                    else if (cid == 0x341f0 && csz >= 12 && !memcmp(b, "XPC1", 4)) {
                        p->ox = rdf(b + 4); p->oy = rdf(b + 8); w->xp = 1;
                    }
                    o += 8 + csz;
                }
                p->data = raw;
                int s = section_slot(w, p->section);
                if (p->poly && p->list && p->cells && p->W > 0 && p->H > 0 &&
                    p->ncells >= p->W * p->H && s >= 0) {
                    w->slot[s] = w->npacks++;
                } else free(raw);
            }
            LOAD_PUMP();   /* a pack read: the loading screen keeps moving */
        }
        if (!ix) off += 8 + (long)sz;
    }
    free(ix);
    fclose(f);

    f = fopen(track_bun, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        unsigned char *d = n > 0 ? read_at(f, 0, n) : NULL;
        fclose(f);
        if (d) { load_bounds(w, d, n); free(d); }
    }
    if (!w->npacks || !w->nbnd) {
        printf("wcoll: %s: %d collision packs, %d section boundaries -- not used\n",
               stream_bun, w->npacks, w->nbnd);
        wcoll_free(w);
        return NULL;
    }
    w->order = (int *)malloc((size_t)w->nbnd * sizeof *w->order);
    if (!w->order) g_wc_oom = 1;
    else for (int i = 0; i < w->nbnd; i++) w->order[i] = i;
    if (w->nvis) {   /* each record's boundary (FindBoundary(section) at load) */
        w->vorder = (int *)malloc((size_t)w->nvis * sizeof *w->vorder);
        for (int i = 0; i < w->nvis; i++) {
            if (w->vorder) w->vorder[i] = i;
            for (int b = 0; b < w->nbnd && w->vis[i].bnd < 0; b++)
                if (w->bnd[b].section == w->vis[i].section) w->vis[i].bnd = b;
        }
        if (!w->vorder) w->nvis = 0;
    }
    w->enabled = (unsigned char *)calloc((size_t)(w->ngroups > 256 ? w->ngroups : 256), 1);
    /* out of memory part way (a track switch late in a session): no collision
       rather than a half-built one read through NULL every frame (audit 11-17) */
    if (g_wc_oom || !w->enabled) {
        printf("wcoll: out of memory while loading %s, no collision\n", stream_bun);
        wcoll_free(w);
        return NULL;
    }
    printf("wcoll: %d collision packs, %d drivable section boundaries, %d barrier groups\n",
           w->npacks, w->nbnd, w->ngroups);
    return w;
}

/* direct-mapped cache of expanded polygons; the packs never change while a
   WColl lives, so an entry is exact until wcoll_free clears it */
#define PC_BITS 11
typedef struct { const WColl *w; uint32_t key; WCollPoly p; } PolyCache;
static PolyCache g_pc[1 << PC_BITS];
/* the calling thread: on the Vita the user read-only thread register
   (TPIDRURO, one instruction; sceKernelGetThreadId would be a call per
   ground query) */
static inline uintptr_t pc_self(void) {
#if defined(__arm__) && !defined(OPENUG2_SIM_PRE_H)
    uintptr_t v; __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v)); return v;
#else
    return (uintptr_t)SDL_ThreadID();
#endif
}
static uintptr_t g_pc_tid;
void wcoll_cache_thread(void) { g_pc_tid = pc_self(); }

void wcoll_free(WColl *w)
{
    if (!w) return;
    for (int i = 0; i < (1 << PC_BITS); i++) if (g_pc[i].w == w) g_pc[i].w = NULL;
    for (int i = 0; i < w->npacks; i++) free(w->packs[i].data);
    for (int i = 0; i < w->nbnd; i++) free(w->bnd[i].xy);
    free(w->packs); free(w->bnd); free(w->order); free(w->groups); free(w->enabled);
    free(w->vis); free(w->vorder);
    free(w);
}

int wcoll_pack_count(const WColl *w) { return w ? w->npacks : 0; }

void wcoll_ctx_init(WCollCtx *c)
{
    memset(c, 0, sizeof *c);
    c->tp[0] = c->tp[1] = 1e30f;
    c->bnd = -1; c->pack = -1;
}

/* diagnostics: the barrier group name of a polygon id (pack << 16 | poly), "" none */
const char *wcoll_poly_group(const WColl *w, int id)
{
    int pk = id >> 16, pi = id & 0xffff;
    if (!w || pk < 0 || pk >= w->npacks || pi >= w->packs[pk].npoly) return "?";
    int g = w->packs[pk].poly[32 * pi];
    return g > 0 && g < w->ngroups ? w->groups[g].name : "";
}
/* 0x5d80f0: groups are matched by name hash; index 0 is never switched */
int wcoll_enable_group(WColl *w, const char *name)
{
    if (!w) return 0;
    uint32_t h = str_hash(name);
    for (int i = 1; i < w->ngroups; i++)
        if (w->groups[i].hash == h) { w->enabled[i] = 1; return 1; }
    return 0;
}

void wcoll_clear_groups(WColl *w)
{
    if (w && w->enabled) memset(w->enabled, 0, (size_t)(w->ngroups > 256 ? w->ngroups : 256));
}

/* ---- section tracker ---------------------------------------------------- */
static int bbox_has(const Bound *b, const float *p, float m)            /* 0x43d410 */
{
    return p[0] >= b->bb[0] - m && p[0] <= b->bb[2] + m &&
           p[1] >= b->bb[1] - m && p[1] <= b->bb[3] + m;
}

static int bound_inside(const Bound *b, const float *p)                 /* 0x5d6dd0 */
{
    if (!bbox_has(b, p, 0)) return 0;
    float x = p[0], y = p[1];
    int in = 0;
    for (int i = 0, j = b->n - 1; i < b->n; j = i++) {
        float xi = b->xy[2*i], yi = b->xy[2*i+1], xj = b->xy[2*j], yj = b->xy[2*j+1];
        if (((yi <= y && y < yj) || (yj <= y && y < yi)) &&
            x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

/* nearest distance from p to the outline, starting from 'best' (0x5d7270 /
   0x5d70d0 inner loop) */
static float edge_dist(const Bound *b, const float *p, float best)
{
    for (int i = 0; i < b->n; i++) {
        const float *a = &b->xy[2*i], *c = &b->xy[2*((i + 1) % b->n)];
        float rx = p[0] - a[0], ry = p[1] - a[1];
        float ex = c[0] - a[0], ey = c[1] - a[1];
        float l = sqrtf(ex*ex + ey*ey);
        if (l > 0) { ex /= l; ey /= l; }
        float t = rx*ex + ry*ey, q = rx*ey - ex*ry, dd;
        if (t >= 0) dd = t <= l ? fabsf(q) : sqrtf(q*q + (t - l)*(t - l));
        else dd = sqrtf(t*t + q*q);
        if (dd < best) best = dd;
    }
    return best;
}

static float inside_dist(const Bound *b, const float *p)                /* 0x5d7270 */
{
    if (!bound_inside(b, p)) return 0;
    float dx = p[0] - b->xy[0], dy = p[1] - b->xy[1];
    return edge_dist(b, p, sqrtf(dx*dx + dy*dy));
}

static void to_front(WColl *w, int k)
{
    int v = w->order[k];
    memmove(&w->order[1], &w->order[0], (size_t)k * sizeof *w->order);
    w->order[0] = v;
}

/* 0x5db770: the boundary nearest to p (0 when inside one), ties to the lower
   section number */
static int bound_nearest(WColl *w, const float *p, float *dist)
{
    int best = -1; float bd = 9999999.0f;
    for (int k = 0; k < w->nbnd; k++) {
        const Bound *b = &w->bnd[w->order[k]];
        if (bound_inside(b, p)) {
            best = w->order[k]; bd = 0;
            to_front(w, k);
            break;
        }
        float dd = bd;                                                  /* 0x5d70d0 */
        if (bbox_has(b, p, bd)) dd = edge_dist(b, p, bd);
        if (best < 0 || dd < bd || (dd == bd && b->section < w->bnd[best].section)) {
            bd = dd; best = w->order[k];
        }
    }
    if (dist) *dist = bd;
    return best;
}

/* 0x5db850: the boundary containing p, or one within 0.1 m */
static int bound_find(WColl *w, const float *p)
{
    for (int k = 0; k < w->nbnd; k++)
        if (bound_inside(&w->bnd[w->order[k]], p)) {
            int b = w->order[k];
            to_front(w, k);
            return b;
        }
    float d;
    int b = bound_nearest(w, p, &d);
    return d < 0.1f ? b : -1;
}

/* The boundary search reorders w->order (most recently found first), and
   race cars step on several threads (main.c rt_step_batch): one at a time. */
static volatile int g_wc_order_lock;
static int bound_find_locked(WColl *w, const float *p, float *near_dist)
{
    for (int n_ = 0; __sync_lock_test_and_set(&g_wc_order_lock, 1); n_++) if (n_ > 64) SDL_Delay(1);
    int b = bound_find(w, p);
    if (b < 0) bound_nearest(w, p, near_dist);
    __sync_lock_release(&g_wc_order_lock);
    return b;
}

/* VisibleSectionManager::FindDrivableSection(bVector2) 0x5e00d0 (GC
   8020bdc8): the first drivable section (most recently found first) whose
   boundary holds p (0x43d410 box, 0x5d6dd0 polygon) moves to the front;
   else FindClosestBoundary 0x5db770 under 0.1 m (0x784358) gives the
   section by number (FindDrivableSection(int) GC 8020be90); else none. */
int wcoll_visible_sections(WColl *w, const float p[2], const short **list)
{
    if (!w || !w->nvis) return -2;                      /* no section data */
    for (int k = 0; k < w->nvis; k++) {
        const VisRec *v = &w->vis[w->vorder[k]];
        if (v->bnd < 0 || !bound_inside(&w->bnd[v->bnd], p)) continue;
        int i = w->vorder[k];
        memmove(&w->vorder[1], &w->vorder[0], (size_t)k * sizeof *w->vorder);
        w->vorder[0] = i;
        *list = w->vis[i].vis;
        return w->vis[i].n;
    }
    float d = 0.0f;
    for (int n_ = 0; __sync_lock_test_and_set(&g_wc_order_lock, 1); n_++) if (n_ > 64) SDL_Delay(1);
    int b = bound_nearest(w, p, &d);
    __sync_lock_release(&g_wc_order_lock);
    if (b < 0 || !(d < 0.1f)) return -1;
    for (int i = 0; i < w->nvis; i++)
        if (w->vis[i].section == w->bnd[b].section) { *list = w->vis[i].vis; return w->vis[i].n; }
    return -1;
}

/* 0x5dfeb0: re-checks once p has gone 9 m past the safe radius */
static int track(WColl *w, WCollCtx *c, const float *p, float margin)
{
    float dx = p[0] - c->tp[0], dy = p[1] - c->tp[1];
    if (sqrtf(dx*dx + dy*dy) <= margin + c->tr) return 0;
    c->tp[0] = p[0]; c->tp[1] = p[1];
    if (c->bnd >= 0 && bound_inside(&w->bnd[c->bnd], p)) {
        c->tr = inside_dist(&w->bnd[c->bnd], p);
        return 0;
    }
    c->bnd = bound_find_locked(w, p, &c->tr);
    if (c->bnd < 0) return 1;
    c->tr = inside_dist(&w->bnd[c->bnd], p);
    return 1;
}

/* 0x5e68d0: switch the live pack when the tracked section changes */
static void select_pack(WColl *w, WCollCtx *c, const float *p)
{
    if (!(fabsf(p[0] - c->tp[0]) + fabsf(p[1] - c->tp[1]) > 9.0f + c->tr)) return;
    if (!track(w, c, p, 9.0f)) return;
    int sec = c->bnd >= 0 ? w->bnd[c->bnd].section : 0;
    if (c->pack < 0) {
        if (!sec) return;
        int s = section_slot(w, sec);
        if (s < 0 || w->slot[s] < 0) return;
        c->pack = w->slot[s]; c->ncache = 0;
    } else if (w->packs[c->pack].section - (w->xp ? 0 : 20) != sec) {
        int s = section_slot(w, sec);
        c->pack = s >= 0 ? w->slot[s] : -1;
        c->ncache = 0;
    }
}

/* ---- polygons ----------------------------------------------------------- */
typedef WCollPoly Poly;

static void poly_expand(const Pack *k, const unsigned char *q, Poly *o)  /* 0x5d7f10 */
{
    o->id = 0;
    int base = rds16(q + 4);
    for (int i = 0; i < 4; i++) {                                       /* 0x5d2cc0 */
        o->v[i][0] = (float)rds16(q + 8 + 2*i) * 0.125f + k->ox;
        o->v[i][1] = (float)rds16(q + 0x10 + 2*i) * 0.125f + k->oy;
        o->v[i][2] = (float)((base * 256 + rds16(q + 0x18 + 2*i)) * 256) * (1.0f / 65536.0f);
    }
    o->flags = q[3];
    o->surface = (signed char)q[2];
    o->nv = (o->flags & 0x10) ? 4 : 3;
    if (o->flags & 4) {                       /* vertical wall: +-2048 m */
        o->v[0][2] = -2048.0f; o->v[1][2] = 2048.0f;
        o->v[2][2] = 2048.0f;  o->v[3][2] = -2048.0f;
    }
    float a[3], b[3];
    for (int k = 0; k < 3; k++) { a[k] = o->v[1][k] - o->v[0][k]; b[k] = o->v[2][k] - o->v[0][k]; }
    float n[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
    float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
    memcpy(o->n, n, sizeof n);
    o->d = o->v[0][0]*n[0] + o->v[0][1]*n[1] + o->v[0][2]*n[2];
}

/* polygon pi of pack: from the cache on the frame thread (exact: the packs
   never change while the WColl lives), else expanded into tmp. The pointer
   is good until the next poly_at. */
static const Poly *poly_at(const WColl *w, int pack, const unsigned char *q, int pi, int main, Poly *tmp)
{
    if (!main) { poly_expand(&w->packs[pack], q, tmp); return tmp; }
    uint32_t key = ((uint32_t)pack << 16) | (uint32_t)pi;
    PolyCache *e = &g_pc[(key * 2654435761u) >> (32 - PC_BITS)];
    if (e->w != w || e->key != key) { poly_expand(&w->packs[pack], q, &e->p); e->w = w; e->key = key; }
    return &e->p;
}

/* 0x5d3330: the largest edge function, <= 0 inside (counter-clockwise) */
static float poly_edge(const Poly *o, const float *p)
{
    float r = -1.0f;
#define E(A, B) ((p[0] - o->v[A][0]) * (o->v[B][1] - o->v[A][1]) - (o->v[B][0] - o->v[A][0]) * (p[1] - o->v[A][1]))
    float e = E(0, 1);
    if (-1.0f <= e) { r = e; if (0 < e) return e; }
    e = E(1, 2); if (r <= e) r = e;
    if (r <= 0) {
        e = E(3, 0); if (r <= e) r = e;
        if (r <= 0 && o->nv == 4) { e = E(2, 3); if (r <= e) r = e; }
    }
#undef E
    return r;
}

static float poly_z(const Poly *o, const float *p)
{
    return (-1.0f / o->n[2]) * ((o->n[0] * p[0] + o->n[1] * p[1]) - o->d);
}

/* (port) the 2D footprints of a barrier group's polygons, for the GPS route
   search: per polygon its two vertices farthest apart in xy (x0 y0 x1 y1),
   duplicates (the x20..x39 collision copies) dropped */
int wcoll_group_walls(const WColl *w, const char *name, float *seg, int max)
{
    if (!w || !name || !seg || max < 1) return 0;
    uint32_t h = str_hash(name);
    int gi = -1, n = 0;
    for (int i = 1; i < w->ngroups && gi < 0; i++) if (w->groups[i].hash == h) gi = i;
    if (gi < 0) return 0;
    for (int pk = 0; pk < w->npacks; pk++) {
        const Pack *k = &w->packs[pk];
        for (int pi = 0; pi < k->npoly && n < max; pi++) {
            const unsigned char *q = k->poly + 32 * pi;
            if (q[0] != gi) continue;
            Poly o;
            poly_expand(k, q, &o);
            int a = 0, b = 1; float best = -1.0f;
            for (int i = 0; i < o.nv; i++)
                for (int j = i + 1; j < o.nv; j++) {
                    float dx = o.v[j][0] - o.v[i][0], dy = o.v[j][1] - o.v[i][1], d = dx * dx + dy * dy;
                    if (d > best) { best = d; a = i; b = j; }
                }
            float s[4] = { o.v[a][0], o.v[a][1], o.v[b][0], o.v[b][1] };
            int dup = 0;
            for (int i = 0; i < n && !dup; i++) {
                const float *t = seg + 4 * i;
                dup = (t[0] == s[0] && t[1] == s[1] && t[2] == s[2] && t[3] == s[3]) ||
                      (t[0] == s[2] && t[1] == s[3] && t[2] == s[0] && t[3] == s[1]);
            }
            if (!dup) { memcpy(seg + 4 * n, s, sizeof s); n++; }
        }
    }
    return n;
}

static void push_cache(WCollCtx *c, int poly)
{
    int n = c->ncache + 1;
    if (n > 4) n = 4;
    memmove(&c->cache[1], &c->cache[0], (size_t)(n - 1) * sizeof c->cache[0]);
    c->cache[0] = poly;
    c->ncache = n;
}

float wcoll_ground(WColl *w, WCollCtx *c, const float p[3],
                   int *surface, float normal[3], int *found)
{
    if (w) select_pack(w, c, p);
    if (!w || c->pack < 0) {
        if (found) *found = 0;
        if (surface) *surface = 1;
        if (normal) { normal[0] = normal[1] = 0; normal[2] = -1; }
        return p[2];
    }
    const Pack *k = &w->packs[c->pack];
    float limit = p[2] + 1.0f;
    Poly tmp;
    const Poly *op;
    int mainthr = g_pc_tid && pc_self() == g_pc_tid;
    /* the four most recently used polygons first */
    for (int i = 0; i < c->ncache; i++) {
        op = poly_at(w, c->pack, k->poly + 32 * c->cache[i], c->cache[i], mainthr, &tmp);
        if (!(poly_edge(op, p) < 0)) continue;
        float z = poly_z(op, p);
        if (!(op->flags & 1) || (z <= limit && limit - z < 4.0f)) {
            int id = c->cache[i];
            memmove(&c->cache[1], &c->cache[0], (size_t)i * sizeof c->cache[0]);
            c->cache[0] = id;
            if (found) *found = 1;
            if (surface) *surface = op->surface;
            if (normal) memcpy(normal, op->n, 12);
            return z;
        }
    }
    /* the grid cell and its 8 m sub-cell */
    int ix = (int)((p[0] - k->x0) * 0.03125f), iy = (int)((p[1] - k->y0) * 0.03125f);
    float ox = (float)ix * 32.0f + k->x0, oy = (float)iy * 32.0f + k->y0;
    int sx = (int)((p[0] - ox) * 0.125f), sy = (int)((p[1] - oy) * 0.125f);
    uint32_t mask = (1u << (sx & 31)) << ((sy * 4) & 31);
    int cx = ix > 0 ? ix : 0, cy = iy > 0 ? iy : 0;
    if (cx > k->W - 1) cx = k->W - 1;
    if (cy > k->H - 1) cy = k->H - 1;
    const unsigned char *cell = k->cells + 4 * (k->W * cy + cx);
    int start = rd16(cell), end = start + rd16(cell + 2);
    if (end > k->nlist) end = k->nlist;
    int px = (int)((p[0] - k->ox) * 65536.0f), py = (int)((p[1] - k->oy) * 65536.0f);
    int best = -1; float bz = -99999.0f;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int e = start; e < end; e++) {
            const unsigned char *le = k->list + 4 * e;
            if (!(rd16(le + 2) & mask)) continue;
            int pi = rd16(le);
            if (pi >= k->npoly) continue;
            const unsigned char *q = k->poly + 32 * pi;
            if (q[3] & 0xa) break;               /* walls end the ground list */
            int sel = q[1];
            if (px < rds16(q + 8 + (sel & 3) * 2) * 8192) continue;
            if (py < rds16(q + 0x10 + ((sel >> 2) & 3) * 2) * 8192) continue;
            if (px > rds16(q + 8 + ((sel >> 4) & 3) * 2) * 8192) continue;
            if (py > rds16(q + 0x10 + (sel >> 6) * 2) * 8192) continue;
            if (q[0] && (c->nogroups || !w->enabled[q[0]])) continue;   /* barrier group off (0x5d8150: the collider's list) */
            op = poly_at(w, c->pack, q, pi, mainthr, &tmp);
            float ed = poly_edge(op, p);
            if (ed > 0) continue;
            float z = poly_z(op, p);
            if (!(op->flags & 1) && ed < 0) { best = pi; bz = z; break; }
            if (!(z > bz) || !(z <= limit)) continue;
            best = pi; bz = z;
            if (op->flags & 0x40) continue;
            if (limit - z < 4.0f) break;
        }
        limit = FLT_MAX;
    }
    if (best < 0) {
        if (found) *found = 0;
        if (surface) *surface = 1;
        if (normal) { normal[0] = normal[1] = 0; normal[2] = -1; }
        return p[2];
    }
    op = poly_at(w, c->pack, k->poly + 32 * best, best, mainthr, &tmp);
    if (!(op->flags & 0x40)) push_cache(c, best);
    if (found) *found = 1;
    if (surface) *surface = op->surface;
    if (normal) memcpy(normal, op->n, 12);
    return bz;
}

/* 0x5e0d30 + 0x5ae930: the polygons a moving body may touch. Cells under the
   box [mn, mx] (8 m sub-cells), polygon bounds overlapping it, barrier
   groups switched on; ordered as the original creates its TrackPolygon
   bodies (by polygon). */
int wcoll_gather(WColl *w, WCollCtx *c, const float pos[3], const float mn[2], const float mx[2],
                 WCollPoly *out, int max)
{
    if (!w) return 0;
    select_pack(w, c, pos);
    if (c->pack < 0) return 0;
    const Pack *k = &w->packs[c->pack];
    int ids[160], n = 0;
    int x0 = (int)((mn[0] - k->x0) * 0.03125f), x1 = (int)((mx[0] - k->x0) * 0.03125f);
    int y0 = (int)((mn[1] - k->y0) * 0.03125f), y1 = (int)((mx[1] - k->y0) * 0.03125f);
    int wbits = (1 << (1 - (int)((mx[0] - mn[0] + 8.0f) * -0.125f))) - 1;
    int hbits = (1 << (1 - (int)((mx[1] - mn[1] + 8.0f) * -0.125f))) - 1;
    for (int iy = y0; iy <= y1; iy++)
        for (int ix = x0; ix <= x1; ix++) {
            float ox = (float)ix * 32.0f + k->x0, oy = (float)iy * 32.0f + k->y0;
            int lx = (int)((mn[0] - ox) * 0.125f), ly = (int)((mn[1] - oy) * 0.125f);
            unsigned xb = ((unsigned)wbits << (lx < 0 ? 0 : lx & 31)) & 15u;
            unsigned yb = (unsigned)hbits << (ly < 0 ? 0 : ly & 31);
            unsigned mask = 0;
            for (int j = 0; j < 4; j++) if (yb & (1u << j)) mask |= xb << (4 * j);
            int cx = ix > 0 ? ix : 0, cy = iy > 0 ? iy : 0;
            if (cx > k->W - 1) cx = k->W - 1;
            if (cy > k->H - 1) cy = k->H - 1;
            const unsigned char *cell = k->cells + 4 * (k->W * cy + cx);
            int e0 = rd16(cell), e1 = e0 + rd16(cell + 2);
            if (e1 > k->nlist) e1 = k->nlist;
            for (int e = e0; e < e1; e++) {
                const unsigned char *le = k->list + 4 * e;
                if (!(rd16(le + 2) & mask)) continue;
                int pi = rd16(le);
                if (pi >= k->npoly) continue;
                const unsigned char *q = k->poly + 32 * pi;
                int sel = q[1], dup = 0;
                if (rds16(q + 8 + (sel & 3) * 2) * 0.125f + k->ox > mx[0]) continue;
                if (rds16(q + 0x10 + ((sel >> 2) & 3) * 2) * 0.125f + k->oy > mx[1]) continue;
                if (!(mn[0] <= rds16(q + 8 + ((sel >> 4) & 3) * 2) * 0.125f + k->ox)) continue;
                if (!(mn[1] <= rds16(q + 0x10 + (sel >> 6) * 2) * 0.125f + k->oy)) continue;
                if (q[0] && (c->nogroups || !w->enabled[q[0]])) continue;
                for (int i = 0; i < n && !dup; i++) dup = ids[i] == pi;    /* the 0x20 mark */
                if (dup || n >= 160) continue;
                ids[n++] = pi;
            }
        }
    for (int i = 1; i < n; i++)                 /* ascending, as 0x5ae930 sorts */
        for (int j = i; j > 0 && ids[j - 1] > ids[j]; j--) { int t = ids[j]; ids[j] = ids[j - 1]; ids[j - 1] = t; }
    int m = 0;
    int mainthr = g_pc_tid && pc_self() == g_pc_tid;   /* cached polygons: frame thread only (11-15) */
    for (int i = 0; i < n && m < max; i++) {
        if (mainthr) out[m] = *poly_at(w, c->pack, k->poly + 32 * ids[i], ids[i], 1, NULL);
        else poly_expand(k, k->poly + 32 * ids[i], &out[m]);
        out[m].id = (c->pack << 16) | ids[i];
        m++;
    }
    return m;
}
