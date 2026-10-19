/* wcoll.c — the retail NFSU2 collision world (see wcoll.h). */
#include "wcoll.h"

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

#define WC_NSLOT 520             /* 26 section letters x 20 */

struct WColl {
    Pack *packs; int npacks;
    int slot[WC_NSLOT];          /* 0x88f768: pack per section slot (0x5d3c10) */
    Bound *bnd; int nbnd;
    int *order;                  /* the boundary list, most recently found first */
    Group *groups; int ngroups;
    unsigned char *enabled;      /* the world's +0x2f0 list as a flag per group */
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
static int section_slot(int s)
{
    if (s == 0) return 0;
    int m = s % 100;
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

static void load_bounds(WColl *w, const unsigned char *d, long len)
{
    int cap = 0;
    for (long o = 0; o + 8 <= len; ) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        long end = o + 8 + (long)sz;
        if (end > len) break;
        if (id == 0x34152) {
            long q = o + 8;
            while (q + 0x24 <= end) {
                if (rd32(d + q) == 0x11111111u || rd32(d + q) == 0) { q += 4; continue; }
                int sec = rds16(d + q + 8), n = rds16(d + q + 10);
                if (n < 1 || q + 0x24 + n * 8 > end) break;
                /* Only drivable sections (x00..x19) are tracked; the x41..,
                   x98, x99 boundaries are scenery and region outlines. */
                if (sec % 100 < 20) {
                    if (w->nbnd == cap) {
                        cap = cap ? cap * 2 : 256;
                        w->bnd = (Bound *)realloc(w->bnd, (size_t)cap * sizeof *w->bnd);
                    }
                    Bound *b = &w->bnd[w->nbnd++];
                    b->section = sec; b->n = n;
                    for (int k = 0; k < 4; k++) b->bb[k] = rdf(d + q + 0xc + (k & 1) * 4 + (k >> 1) * 8);
                    b->xy = (float *)malloc((size_t)n * 8);
                    for (int k = 0; k < 2 * n; k++) b->xy[k] = rdf(d + q + 0x24 + k * 4);
                }
                q += 0x24 + n * 8;
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
    WColl *w = (WColl *)calloc(1, sizeof *w);
    for (int i = 0; i < WC_NSLOT; i++) w->slot[i] = -1;
    int cap = 0;
    long off = 0;
    unsigned char hdr[8];
    while (fseek(f, off, SEEK_SET) == 0 && fread(hdr, 1, 8, f) == 8) {
        uint32_t id = rd32(hdr), sz = rd32(hdr + 4);
        if (id == 0x80034130u) {
            /* keep the payload's file alignment: the header sub-chunk is
               found by aligning its absolute offset */
            long base = off + 8, pad = base & 15;
            unsigned char *raw = read_at(f, base - pad, (long)sz + pad);
            if (raw) {
                if (w->npacks == cap) {
                    cap = cap ? cap * 2 : 64;
                    w->packs = (Pack *)realloc(w->packs, (size_t)cap * sizeof *w->packs);
                }
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
                    o += 8 + csz;
                }
                p->data = raw;
                int s = section_slot(p->section);
                if (p->poly && p->list && p->cells && p->W > 0 && p->H > 0 &&
                    p->ncells >= p->W * p->H && s >= 0) {
                    w->slot[s] = w->npacks++;
                } else free(raw);
            }
        }
        off += 8 + (long)sz;
    }
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
    for (int i = 0; i < w->nbnd; i++) w->order[i] = i;
    w->enabled = (unsigned char *)calloc((size_t)(w->ngroups > 256 ? w->ngroups : 256), 1);
    printf("wcoll: %d collision packs, %d drivable section boundaries, %d barrier groups\n",
           w->npacks, w->nbnd, w->ngroups);
    return w;
}

void wcoll_free(WColl *w)
{
    if (!w) return;
    for (int i = 0; i < w->npacks; i++) free(w->packs[i].data);
    for (int i = 0; i < w->nbnd; i++) free(w->bnd[i].xy);
    free(w->packs); free(w->bnd); free(w->order); free(w->groups); free(w->enabled);
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
    while (__sync_lock_test_and_set(&g_wc_order_lock, 1)) { }
    int b = bound_find(w, p);
    if (b < 0) bound_nearest(w, p, near_dist);
    __sync_lock_release(&g_wc_order_lock);
    return b;
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
        int s = section_slot(sec);
        if (s < 0 || w->slot[s] < 0) return;
        c->pack = w->slot[s]; c->ncache = 0;
    } else if (w->packs[c->pack].section - 20 != sec) {
        int s = section_slot(sec);
        c->pack = s >= 0 ? w->slot[s] : -1;
        c->ncache = 0;
    }
}

/* ---- polygons ----------------------------------------------------------- */
typedef WCollPoly Poly;

static void poly_expand(const unsigned char *q, Poly *o)                /* 0x5d7f10 */
{
    o->id = 0;
    int base = rds16(q + 4);
    for (int i = 0; i < 4; i++) {                                       /* 0x5d2cc0 */
        o->v[i][0] = (float)rds16(q + 8 + 2*i) * 0.125f;
        o->v[i][1] = (float)rds16(q + 0x10 + 2*i) * 0.125f;
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
    Poly o;
    /* the four most recently used polygons first */
    for (int i = 0; i < c->ncache; i++) {
        poly_expand(k->poly + 32 * c->cache[i], &o);
        if (!(poly_edge(&o, p) < 0)) continue;
        float z = poly_z(&o, p);
        if (!(o.flags & 1) || (z <= limit && limit - z < 4.0f)) {
            int id = c->cache[i];
            memmove(&c->cache[1], &c->cache[0], (size_t)i * sizeof c->cache[0]);
            c->cache[0] = id;
            if (found) *found = 1;
            if (surface) *surface = o.surface;
            if (normal) memcpy(normal, o.n, 12);
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
    int px = (int)(p[0] * 65536.0f), py = (int)(p[1] * 65536.0f);
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
            poly_expand(q, &o);
            float ed = poly_edge(&o, p);
            if (ed > 0) continue;
            float z = poly_z(&o, p);
            if (!(o.flags & 1) && ed < 0) { best = pi; bz = z; break; }
            if (!(z > bz) || !(z <= limit)) continue;
            best = pi; bz = z;
            if (o.flags & 0x40) continue;
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
    poly_expand(k->poly + 32 * best, &o);
    if (!(o.flags & 0x40)) push_cache(c, best);
    if (found) *found = 1;
    if (surface) *surface = o.surface;
    if (normal) memcpy(normal, o.n, 12);
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
                if (rds16(q + 8 + (sel & 3) * 2) * 0.125f > mx[0]) continue;
                if (rds16(q + 0x10 + ((sel >> 2) & 3) * 2) * 0.125f > mx[1]) continue;
                if (!(mn[0] <= rds16(q + 8 + ((sel >> 4) & 3) * 2) * 0.125f)) continue;
                if (!(mn[1] <= rds16(q + 0x10 + (sel >> 6) * 2) * 0.125f)) continue;
                if (q[0] && (c->nogroups || !w->enabled[q[0]])) continue;
                for (int i = 0; i < n && !dup; i++) dup = ids[i] == pi;    /* the 0x20 mark */
                if (dup || n >= 160) continue;
                ids[n++] = pi;
            }
        }
    for (int i = 1; i < n; i++)                 /* ascending, as 0x5ae930 sorts */
        for (int j = i; j > 0 && ids[j - 1] > ids[j]; j--) { int t = ids[j]; ids[j] = ids[j - 1]; ids[j - 1] = t; }
    int m = 0;
    for (int i = 0; i < n && m < max; i++) {
        poly_expand(k->poly + 32 * ids[i], &out[m]);
        out[m].id = (c->pack << 16) | ids[i];
        m++;
    }
    return m;
}
