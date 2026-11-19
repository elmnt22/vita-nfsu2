/* carcoll.c — the retail NFSU2 car-vs-world collision manager (see carcoll.h). */
#include "carcoll.h"
#include "acid.h"

#include <math.h>
#include "SDL.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPOLY 48            /* TrackPolygon bodies a car may have (0x5ae930) */

struct CarColl {
    int    nv;
    float (*hv)[3];           /* hull vertices, model space */
    float  com[3];            /* centre of mass the bounds were built for */
    float  rad;               /* +0x1c bounding radius */
    float  below, above;      /* +0xd0 / +0xd4 extents under / over the centre of mass */
    float  centre[3];         /* +0x110 centre of the hull's bounds, body frame */
    /* pose the step started from (+0x70 matrix, +0xb0 position, +0xc0 velocity, +0x6c span) */
    float  ppos[3], pR[16], pv[3], span;
    char   name[48];
};

static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void  cross3(float *o, const float *a, const float *b)          /* 0x43d1e0 */
{
    float x = a[1]*b[2] - a[2]*b[1], y = a[2]*b[0] - a[0]*b[2], z = a[0]*b[1] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static void  rowmul(float *o, const float *v, const float *m)          /* 0x441000 */
{
    float x = v[0]*m[0] + v[1]*m[4] + v[2]*m[8];
    float y = v[0]*m[1] + v[1]*m[5] + v[2]*m[9];
    float z = v[0]*m[2] + v[1]*m[6] + v[2]*m[10];
    o[0] = x; o[1] = y; o[2] = z;
}
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* ---- the hull ------------------------------------------------------------ */
/* 0x5a1750: chunk 0x80034020 holds 0x39200 headers (name at +0x1c of the
   16-byte aligned body), each followed by its 0x39201 vertices (0x30 bytes,
   position at +0x10) and 0x39202 neighbour lists. */
/* one 0x80034020 chunk's body d (sz bytes, file offset base): the named
   hull's vertices into c */
static void hull_scan(CarColl *c, const unsigned char *d, long sz, long base, const char *want)
{
    for (long o = 0; o + 8 <= sz; ) {
        uint32_t cid = rd32(d + o), csz = rd32(d + o + 4);
        long end = o + 8 + (long)csz;
        if (end > sz) break;
        if (cid == 0x39200) {
            long ha = ((base + o + 0x17) & ~15L) - base;
            char nm[33]; memcpy(nm, d + ha + 0x1c, 32); nm[32] = 0;
            if (!strcmp(nm, want) && end + 8 <= sz && rd32(d + end) == 0x39201) {
                long vs = end, vsz = rd32(d + vs + 4);
                long va = ((base + vs + 0x17) & ~15L) - base;
                int n = (int)((vs + 8 + vsz - va) / 48);
                if (vsz > sz - vs - 8) n = 0;   /* vertices past the chunk: damaged (audit 11-17) */
                if (n > 0 && n < 4096) {
                    c->hv = (float (*)[3])malloc((size_t)n * sizeof *c->hv);
                    if (!c->hv) return;   /* out of memory: the box stand-in */
                    for (int i = 0; i < n; i++)
                        for (int k = 0; k < 3; k++)
                            c->hv[i][k] = rdf(d + va + i * 48 + 0x10 + k * 4);
                    c->nv = n;
                }
                return;
            }
        }
        o = end;
    }
}
static CarColl *hull_finish(CarColl *c, const char *car)
{
    if (!c->nv) {
        /* 0x5aec20 fallback: a box from (-2.2165,-0.971,-0.2) to (2.165,0.971,0.624) */
        static const float lo[3] = { -2.2165f, -0.971f, -0.2f }, hi[3] = { 2.165f, 0.971f, 0.624f };
        c->hv = (float (*)[3])malloc(8 * sizeof *c->hv);
        if (!c->hv) { free(c); printf("carcoll: %s out of memory, no hull\n", car); return NULL; }
        for (int i = 0; i < 8; i++)
            for (int k = 0; k < 3; k++) c->hv[i][k] = (i >> k & 1) ? hi[k] : lo[k];
        c->nv = 8;
        snprintf(c->name, sizeof c->name, "box");
    }
    c->com[0] = 1e30f;
    printf("carcoll: %s collision hull %s, %d vertices\n", car, c->name, c->nv);
    return c;
}
/* RideInfo 0x61c320: the hull is the fitted body part's "CV" attribute --
   in the parts database a body kit (group 5) names <CAR>_CV and a wide body
   kit KITW0n (group 6, +0x5a8 with byte 5 >> 5 > 0, ride +0x83e set by
   default) <CAR>_W0n_CV; without it the car's own (0x610310) */
static void hull_name(char *want, size_t n, const char *car, int wide)
{
    if (wide > 1) snprintf(want, n, "%s_W%02d_CV", car, wide - 1);
    else snprintf(want, n, "%s_CV", car);
}
CarColl *carcoll_load_kit(const char *geometry_bin, const char *car, int wide)
{
    if (wide > 1) {
        char want[48];
        hull_name(want, sizeof want, car, wide);
        FILE *f = fopen(geometry_bin, "rb");
        CarColl *c = (CarColl *)calloc(1, sizeof *c);
        if (!c) { if (f) fclose(f); return NULL; }
        snprintf(c->name, sizeof c->name, "%s", want);
        long off = 0;
        unsigned char h[8];
        while (f && fseek(f, off, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8 && !c->nv) {
            uint32_t id = rd32(h), sz = rd32(h + 4);
            if (id == 0x80034020u && sz < (32u << 20)) {
                unsigned char *d = (unsigned char *)malloc(sz);
                if (d && fread(d, 1, sz, f) == sz) hull_scan(c, d, (long)sz, off + 8, want);
                free(d);
            }
            off += 8 + (long)sz;
        }
        if (f) fclose(f);
        if (c->nv) return hull_finish(c, car);
        free(c);
    }
    return carcoll_load(geometry_bin, car);
}
CarColl *carcoll_load_mem_kit(const unsigned char *geo, long len, const char *car, int wide)
{
    if (wide > 1) {
        char want[48];
        hull_name(want, sizeof want, car, wide);
        CarColl *c = carcoll_load_named(geo, len, want);
        if (c) { printf("carcoll: %s collision hull %s, %d vertices\n", car, c->name, c->nv); return c; }
    }
    return carcoll_load_mem(geo, len, car);
}
CarColl *carcoll_load(const char *geometry_bin, const char *car)
{
    char want[48];
    snprintf(want, sizeof want, "%s_CV", car);
    FILE *f = fopen(geometry_bin, "rb");
    CarColl *c = (CarColl *)calloc(1, sizeof *c);
    if (!c) { if (f) fclose(f); return NULL; }
    snprintf(c->name, sizeof c->name, "%s", want);
    if (f) {
        long off = 0;
        unsigned char h[8];
        while (fseek(f, off, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8 && !c->nv) {
            uint32_t id = rd32(h), sz = rd32(h + 4);
            if (id == 0x80034020u && sz < (32u << 20)) {
                unsigned char *d = (unsigned char *)malloc(sz);
                if (d && fread(d, 1, sz, f) == sz) hull_scan(c, d, (long)sz, off + 8, want);
                free(d);
            }
            off += 8 + (long)sz;
        }
        fclose(f);
    }
    return hull_finish(c, car);
}
/* The same from GEOMETRY.BIN already in memory (a car load has it): the
   file read twice cost a car switch a few hundred ms on the card. */
CarColl *carcoll_load_mem(const unsigned char *geo, long len, const char *car)
{
    char want[48];
    snprintf(want, sizeof want, "%s_CV", car);
    CarColl *c = (CarColl *)calloc(1, sizeof *c);
    if (!c) return NULL;
    snprintf(c->name, sizeof c->name, "%s", want);
    for (long off = 0; geo && off + 8 <= len && !c->nv; ) {
        uint32_t id = rd32(geo + off), sz = rd32(geo + off + 4);
        if ((long)sz > len - off - 8) break;
        if (id == 0x80034020u) hull_scan(c, geo + off + 8, (long)sz, off + 8, want);
        off += 8 + (long)sz;
    }
    return hull_finish(c, car);
}

CarColl *carcoll_load_named(const unsigned char *bun, long len, const char *name)
{
    CarColl *c = (CarColl *)calloc(1, sizeof *c);
    if (!c) return NULL;
    snprintf(c->name, sizeof c->name, "%s", name);
    for (long off = 0; bun && off + 8 <= len && !c->nv; ) {
        uint32_t id = rd32(bun + off), sz = rd32(bun + off + 4);
        if ((long)sz > len - off - 8) break;
        if (id == 0x80034020u) hull_scan(c, bun + off + 8, (long)sz, off + 8, name);
        off += 8 + (long)sz;
    }
    if (!c->nv) { free(c); return NULL; }
    c->com[0] = 1e30f;
    return c;
}

CarColl *carcoll_clone(const CarColl *src)
{
    if (!src) return NULL;
    CarColl *c = (CarColl *)malloc(sizeof *c);
    if (!c) return NULL;
    *c = *src;
    c->hv = NULL;
    if (src->nv > 0 && src->hv) {
        c->hv = (float (*)[3])malloc((size_t)src->nv * sizeof *c->hv);
        if (!c->hv) { free(c); return NULL; }
        memcpy(c->hv, src->hv, (size_t)src->nv * sizeof *c->hv);
    }
    return c;
}
void carcoll_free(CarColl *c)
{
    if (!c) return;
    free(c->hv);
    free(c);
}

int carcoll_vertex_count(const CarColl *c) { return c ? c->nv : 0; }
float carcoll_radius(const CarColl *c) { return c ? c->rad : 0.0f; }

/* 0x5ae520: bounds of the hull about the centre of mass */
static void hull_bounds(CarColl *c, const float com[3])
{
    if (c->com[0] == com[0] && c->com[1] == com[1] && c->com[2] == com[2]) return;
    memcpy(c->com, com, 12);
    float r3 = 0, zmax = 0, zmin = 0, lo[3], hi[3];
    for (int i = 0; i < c->nv; i++) {
        float v[3] = { c->hv[i][0] - com[0], c->hv[i][1] - com[1], c->hv[i][2] - com[2] };
        float l = sqrtf(dot3(v, v));
        if (l > r3) r3 = l;
        if (v[2] > zmax) zmax = v[2]; else if (v[2] < zmin) zmin = v[2];
        for (int k = 0; k < 3; k++) {
            if (!i || v[k] < lo[k]) lo[k] = v[k];
            if (!i || v[k] > hi[k]) hi[k] = v[k];
        }
    }
    c->rad = r3;
    c->below = 0.05f - zmin;
    c->above = 0.05f + zmax;
    for (int k = 0; k < 3; k++) c->centre[k] = (lo[k] + hi[k]) * 0.5f;   /* 0x5a19f0 */
}

/* ---- GJK distance (0x5b3390; simplex 0x595040/0x598130/0x598240) --------
   A car's hull at a pose against either a polygon as a thin face (type 2
   shape, 0x5a2060, extrusion off for cars) or another car's hull. Johnson's
   sub-algorithm on the simplex determinants, as the original's distance
   routine. */
typedef struct {
    const CarColl *c;
    float pos[3], R[16];
    const WCollPoly *p;       /* B is this polygon ... */
    int prism;                /* +0x1d0: the polygon as a 1 m prism (0x592f00) */
    const CarColl *c2;        /* ... or this hull, when p is NULL */
    float pos2[3], R2[16];
} Pair;

static int hull_support(const CarColl *c, const float *pos, const float *R, const float *d, float *o)  /* 0x5afba0 */
{
    float db[3] = { dot3(R, d), dot3(R + 4, d), dot3(R + 8, d) };
    int best = 0; float bd = -9.16e36f;
    for (int i = 0; i < c->nv; i++) {
        float v = (c->hv[i][0] - c->com[0]) * db[0] + (c->hv[i][1] - c->com[1]) * db[1] +
                  (c->hv[i][2] - c->com[2]) * db[2];
        if (v > bd) { bd = v; best = i; }
    }
    float v[3] = { c->hv[best][0] - c->com[0], c->hv[best][1] - c->com[1], c->hv[best][2] - c->com[2] };
    rowmul(o, v, R);
    o[0] += pos[0]; o[1] += pos[1]; o[2] += pos[2];
    return best;
}
/* a hull vertex by its index (0x5a1970), placed */
static void hull_vertex(const CarColl *c, const float *pos, const float *R, int i, float *o)
{
    float v[3] = { c->hv[i][0] - c->com[0], c->hv[i][1] - c->com[1], c->hv[i][2] - c->com[2] };
    rowmul(o, v, R);
    o[0] += pos[0]; o[1] += pos[1]; o[2] += pos[2];
}
static int sup_hull(const Pair *q, const float *d, float *o) { return hull_support(q->c, q->pos, q->R, d, o); }

/* 0x5a1a90 builds a TrackPolygon's shape with a copy of its vertices pushed
   DAT_7a1878 = 1 m behind the face (v - n); 0x5a2060 uses that half only
   while +0x1d0 is set, which 0x5b4be0 does for the contact time and the
   push-out. The rest of the time the polygon is a thin face. */
#define PRISM_DEPTH 1.0f
static int sup_poly(const Pair *q, const float *d, float *o)           /* 0x5a2060 */
{
    const WCollPoly *p = q->p;
    int best = 0; float bd = -9.16e36f;
    for (int i = 0; i < p->nv; i++) {
        float v = dot3(p->v[i], d);
        if (v > bd) { bd = v; best = i; }
    }
    memcpy(o, p->v[best], 12);
    if (q->prism && dot3(p->n, d) < 0) {       /* the back copy reaches further */
        float back = -PRISM_DEPTH * dot3(p->n, d);
        if (back > 0) {
            for (int k = 0; k < 3; k++) o[k] -= PRISM_DEPTH * p->n[k];
            return best + p->nv;                /* the back copies follow the face's vertices */
        }
    }
    return best;
}
static int sup_b(const Pair *q, const float *d, float *o)
{
    if (q->p) return sup_poly(q, d, o);
    return hull_support(q->c2, q->pos2, q->R2, d, o);
}
/* B's vertex by its index */
static void vert_b(const Pair *q, int i, float *o)
{
    if (q->p) {
        const WCollPoly *p = q->p;
        int f = i % p->nv;
        memcpy(o, p->v[f], 12);
        if (i >= p->nv) for (int k = 0; k < 3; k++) o[k] -= PRISM_DEPTH * p->n[k];
    } else hull_vertex(q->c2, q->pos2, q->R2, i, o);
}

/* The points are relative to the car (0x598620 puts the other shape in the
   car's frame), in single precision like the original. */
/* The distance routine's simplex block (pair +0x40): the support points
   picked so far on either shape, their indices, and the closest points. */
typedef struct {
    float a[4][4];            /* +0x00 points of A */
    float b[4][4];            /* +0x40 points of B */
    float wa[4], wb[4];       /* +0x80 / +0x90 closest points */
    float v[4];               /* +0xa0 */
    int   ia[4], na;          /* +0xb0 indices of A's points, +0xc0 count */
    int   ib[4], nb;          /* +0xc4 / +0xd4 */
    int   warm;               /* +0xd8 points from the last call reused */
} GBlk;

static float fdot(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void  fsub(float *o, const float *a, const float *b) { o[0] = a[0]-b[0]; o[1] = a[1]-b[1]; o[2] = a[2]-b[2]; }
static void  fcross(float *o, const float *a, const float *b)        /* 0x43d1e0 */
{
    float x = b[2]*a[1] - b[1]*a[2], y = b[0]*a[2] - b[2]*a[0], z = b[1]*a[0] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static void  fnorm(float *o, const float *a)                         /* 0x43cf10 */
{
    float l = sqrtf(fdot(a, a));
    if (l != 0.0f) { float k = 1.0f / l; o[0] = a[0]*k; o[1] = a[1]*k; o[2] = a[2]*k; }
    else { o[0] = 1.0f; o[1] = 0.0f; o[2] = 0.0f; }
}
static void  fmad(float *o, const float *p, const float *d, float t) /* 0x43d0c0 */
{
    o[0] = t*d[0] + p[0]; o[1] = t*d[1] + p[1]; o[2] = t*d[2] + p[2];
}
static float fd2(const float *a, const float *b)
{
    float d[3]; fsub(d, b, a); return fdot(d, d);
}
/* the furthest of A's points along n (first one wins a tie) */
static float gmaxa(const GBlk *g, const float *n, int *idx)
{
    float m = fdot(n, g->a[0]); *idx = 0;
    for (int i = 1; i < g->na; i++) { float f = fdot(n, g->a[i]); if (m < f) { m = f; *idx = i; } }
    return m;
}
static float gminb(const GBlk *g, const float *n, int *idx)
{
    float m = fdot(n, g->b[0]); *idx = 0;
    for (int i = 1; i < g->nb; i++) { float f = fdot(n, g->b[i]); if (f < m) { m = f; *idx = i; } }
    return m;
}
/* keep only the listed points (indices ascending) */
static void gkeepa(GBlk *g, const int *k, int n)
{
    for (int m = 0; m < n; m++) { g->ia[m] = g->ia[k[m]]; memcpy(g->a[m], g->a[k[m]], 12); }
    g->na = n;
}
static void gkeepb(GBlk *g, const int *k, int n)
{
    for (int m = 0; m < n; m++) { g->ib[m] = g->ib[k[m]]; memcpy(g->b[m], g->b[k[m]], 12); }
    g->nb = n;
}
/* an edge's points kept by the parameter of its closest point */
static int edge_keep(float t, int i, int j, int *k)
{
    if (t == 0.0f) { k[0] = i; return 1; }
    if (t == 1.0f) { k[0] = j; return 1; }
    k[0] = i; k[1] = j; return 2;
}

/* 0x595040: the closest features of the two point sets. Every pair of
   features (a point / edge / triangle of one against an edge / point of the
   other, or edge against edge) gives a direction; it counts only when it
   separates the sets (A's furthest point along it short of B's nearest) and
   the closest points lie on the features. When the furthest points of the
   sets along it are the feature's own, that feature is the answer at once;
   otherwise the nearest of the candidates. The sets shrink to the answer's
   features; out = the direction, the closest points in wa / wb. Returns the
   squared distance, 0 when nothing separates them (overlap). */
static float gjk_feats(GBlk *g, float *out)
{
    const int na = g->na, nb = g->nb;
    float best = 3.402823e+38f;
    int found = 0, bna = 0, bnb = 0, bia[4] = { 0 }, bib[4] = { 0 };
    float bwa[3] = { 0 }, bwb[3] = { 0 }, bout[3] = { 0 };
    int k[3];
#define CANDIDATE(D2) do { found = 1; best = (D2); memcpy(bwa, g->wa, 12); memcpy(bwb, g->wb, 12); \
                           memcpy(bout, out, 12); } while (0)

    if (na == 1 && nb == 1) {
        memcpy(g->wa, g->a[0], 12); memcpy(g->wb, g->b[0], 12);
        fsub(out, g->wb, g->wa);
        return fdot(out, out);
    }
    if (na == 1) {                                       /* A's point against B's edges */
        for (int i = 0; i < nb - 1; i++) {
            float d0[3]; fsub(d0, g->b[i], g->a[0]);
            for (int j = i + 1; j < nb; j++) {
                float e[3]; fsub(e, g->b[j], g->b[i]);
                float t = -fdot(e, d0) / fdot(e, e);
                if (!(t < 0.0f)) {                 /* x87: NaN goes this way */
                    if (!(t > 1.0f)) { fcross(out, d0, e); fcross(out, e, out); }
                    else { t = 1.0f; fsub(out, g->b[j], g->a[0]); }
                } else { t = 0.0f; fsub(out, g->b[i], g->a[0]); }
                int ix; float mb = gminb(g, out, &ix);
                if (!(fdot(out, g->a[0]) < mb)) continue;
                memcpy(g->wa, g->a[0], 12);
                fmad(g->wb, g->b[i], e, t);
                float d2 = fd2(g->wa, g->wb);
                if (ix == i || ix == j) { gkeepb(g, k, edge_keep(t, i, j, k)); return d2; }
                if (d2 < best) {
                    CANDIDATE(d2);
                    bna = 1; bia[0] = 0;
                    bnb = edge_keep(t, i, j, bib);
                }
            }
        }
    } else if (nb == 1) {                                /* A's edges against B's point */
        for (int i = 0; i < na - 1; i++) {
            float d0[3]; fsub(d0, g->b[0], g->a[i]);
            for (int j = i + 1; j < na; j++) {
                float e[3]; fsub(e, g->a[j], g->a[i]);
                float t = fdot(e, d0) / fdot(e, e);
                if (!(t < 0.0f)) {                 /* x87: NaN goes this way */
                    if (!(t > 1.0f)) { fcross(out, d0, e); fcross(out, e, out); }
                    else { t = 1.0f; fsub(out, g->b[0], g->a[j]); }
                } else { t = 0.0f; fsub(out, g->b[0], g->a[i]); }
                int ix; float ma = gmaxa(g, out, &ix);
                if (!(ma < fdot(g->b[0], out))) continue;
                fmad(g->wa, g->a[i], e, t);
                memcpy(g->wb, g->b[0], 12);
                float d2 = fd2(g->wa, g->wb);
                if (ix == i || ix == j) { gkeepa(g, k, edge_keep(t, i, j, k)); return d2; }
                if (d2 < best) {
                    CANDIDATE(d2);
                    bna = edge_keep(t, i, j, bia);
                    bnb = 1; bib[0] = 0;
                }
            }
        }
    } else {                                             /* edge against edge */
        for (int i = 0; i < na - 1; i++)
        for (int j = i + 1; j < na; j++) {
            float e1[3]; fsub(e1, g->a[j], g->a[i]);
            float l1 = fdot(e1, e1);
            for (int kk = 0; kk < nb - 1; kk++) {
                float d[3]; fsub(d, g->b[kk], g->a[i]);
                float de1 = fdot(e1, d);
                for (int l = kk + 1; l < nb; l++) {
                    float e2[3]; fsub(e2, g->b[l], g->b[kk]);
                    float l2 = fdot(e2, e2), e12 = fdot(e1, e2), de2 = fdot(e2, d);
                    float den = l1 * l2 - e12 * e12;
                    float s = de1 * l2 - e12 * de2, u;
                    if (!(fabsf(den) > 1e-10f)) s = 0.0f < s * den ? 1.0f : 0.0f;
                    else {
                        s = s / den;
                        if (!(s < 0.0f)) { if (1.0f < s) s = 1.0f; }
                        else s = 0.0f;
                    }
                    u = (e12 * s - de2) / l2;
                    float pa[3], pb[3], w[3];
                    if (!(u > 0.0f) || !(u < 1.0f)) {
                        u = u <= 0.0f ? 0.0f : 1.0f;
                        float s2 = (e12 * u + de1) / l1;
                        if (!(s2 > 0.0f) || !(s2 < 1.0f)) {
                            s = s2 <= 0.0f ? 0.0f : 1.0f;
                            fmad(pa, g->a[i], e1, s);
                            fmad(pb, g->b[kk], e2, u);
                            fsub(out, pb, pa);
                        } else {
                            fmad(pb, g->b[kk], e2, u);
                            fsub(w, pb, g->a[i]);
                            fcross(out, w, e1); fcross(out, e1, out);
                            s = s2;
                        }
                    } else if (!(s > 0.0f) || !(s < 1.0f)) {
                        fmad(pa, g->a[i], e1, s);
                        fsub(w, g->b[kk], pa);
                        fcross(out, w, e2); fcross(out, e2, out);
                    } else {                             /* both inside: the edges' normal, or
                                                            the better of the two in-plane ones */
                        fcross(out, e1, e2); fnorm(out, out);
                        if (fdot(d, out) < 0.0f) { out[0] = -out[0]; out[1] = -out[1]; out[2] = -out[2]; }
                        float n2[3], n3[3];
                        fmad(pa, g->a[i], e1, s);
                        fsub(w, g->b[kk], pa);
                        fcross(n2, w, e2); fcross(n2, e2, n2);
                        fmad(pb, g->b[kk], e2, u);
                        fsub(w, pb, g->a[i]);
                        fcross(n3, w, e1); fcross(n3, e1, n3);
                        const float *dirs[2] = { n2, n3 };
                        float x, y, lo_a, lo_b, gap;
                        x = fdot(out, g->a[j]); y = fdot(g->a[i], out); lo_a = y < x ? y : x;
                        x = fdot(out, g->b[l]); y = fdot(g->b[kk], out); lo_b = y < x ? y : x;
                        gap = lo_b - lo_a;
                        for (int m = 0; m < 2; m++) {
                            const float *n = dirs[m];
                            x = fdot(n, g->a[j]); y = fdot(n, g->a[i]); lo_a = y < x ? y : x;
                            x = fdot(n, g->b[l]); y = fdot(n, g->b[kk]); lo_b = y < x ? y : x;
                            if (gap < lo_b - lo_a) { memcpy(out, n, 12); gap = lo_b - lo_a; }
                        }
                    }
                    int xa, xb;
                    float ma = gmaxa(g, out, &xa), mb = gminb(g, out, &xb);
                    if (!(ma < mb)) continue;
                    fmad(g->wa, g->a[i], e1, s);
                    fmad(g->wb, g->b[kk], e2, u);
                    float d2 = fd2(g->wa, g->wb);
                    if ((xa == i || xa == j) && (xb == kk || xb == l)) {
                        gkeepa(g, k, edge_keep(s, i, j, k));
                        gkeepb(g, k, edge_keep(u, kk, l, k));
                        return d2;
                    }
                    if (d2 < best) {
                        CANDIDATE(d2);
                        bna = edge_keep(s, i, j, bia);
                        bnb = edge_keep(u, kk, l, bib);
                    }
                }
            }
        }
    }
    if (na >= 3) {                                       /* A's triangles against B's points */
        for (int i = 0; i < na - 2; i++)
        for (int j = i + 1; j < na - 1; j++)
        for (int kk = j + 1; kk < na; kk++) {
            float e1[3], e2[3], n[3];
            fsub(e1, g->a[j], g->a[i]); fsub(e2, g->a[kk], g->a[i]);
            fcross(n, e1, e2); fnorm(n, n);
            float d[3]; fsub(d, g->b[0], g->a[i]);
            if (!(fdot(d, n) < 0.0f)) memcpy(out, n, 12);
            else { out[0] = -n[0]; out[1] = -n[1]; out[2] = -n[2]; }
            float top = fdot(g->a[i], out), ma = top;
            int xa = i, rem = 6 - i - j - kk;
            if (rem < na) { float f = fdot(out, g->a[rem]); if (top < f) { xa = rem; ma = f; } }
            int xb; float mb = gminb(g, out, &xb);
            if (!(ma < mb)) continue;
            const float *p = g->b[xb];
            const float *tri[3] = { g->a[i], g->a[j], g->a[kk] };
            int in = 1;
            for (int m = 0; m < 3 && in; m++) {
                float w[3], e[3], c[3];
                fsub(w, p, tri[m]); fsub(e, tri[(m + 1) % 3], tri[m]);
                fcross(c, n, e);
                in = 0.0f < fdot(c, w);
            }
            if (!in) continue;
            float gap = mb - top, off[3] = { gap * out[0], gap * out[1], gap * out[2] };
            fsub(g->wa, p, off);
            memcpy(g->wb, p, 12);
            float d2 = fdot(off, off);
            if (xa == i) {
                int ka[3] = { i, j, kk };
                gkeepa(g, ka, 3);
                gkeepb(g, &xb, 1);
                return d2;
            }
            if (d2 < best) {
                CANDIDATE(d2);
                bna = 3; bia[0] = i; bia[1] = j; bia[2] = kk;
                bnb = 1; bib[0] = xb;
            }
        }
    }
    if (nb >= 3) {                                       /* B's triangles against A's points */
        for (int i = 0; i < nb - 2; i++)
        for (int j = i + 1; j < nb - 1; j++)
        for (int kk = j + 1; kk < nb; kk++) {
            float e1[3], e2[3], n[3];
            fsub(e1, g->b[j], g->b[i]); fsub(e2, g->b[kk], g->b[i]);
            fcross(n, e1, e2); fnorm(n, n);
            float d[3]; fsub(d, g->b[i], g->a[0]);
            if (!(fdot(d, n) < 0.0f)) memcpy(out, n, 12);
            else { out[0] = -n[0]; out[1] = -n[1]; out[2] = -n[2]; }
            float bot = fdot(g->b[i], out), mb = bot;
            int xb = i, rem = 6 - i - j - kk;
            if (rem < nb) { float f = fdot(out, g->b[rem]); if (f < bot) { xb = rem; mb = f; } }
            int xa; float ma = gmaxa(g, out, &xa);
            if (!(ma < mb)) continue;
            const float *p = g->a[xa];
            const float *tri[3] = { g->b[i], g->b[j], g->b[kk] };
            int in = 1;
            for (int m = 0; m < 3 && in; m++) {
                float w[3], e[3], c[3];
                fsub(w, p, tri[m]); fsub(e, tri[(m + 1) % 3], tri[m]);
                fcross(c, n, e);
                in = 0.0f < fdot(c, w);
            }
            if (!in) continue;
            float gap = bot - ma, off[3] = { gap * out[0], gap * out[1], gap * out[2] };
            memcpy(g->wa, p, 12);
            g->wb[0] = off[0] + p[0]; g->wb[1] = off[1] + p[1]; g->wb[2] = off[2] + p[2];
            float d2 = fdot(off, off);
            if (xb == i) {
                int kb[3] = { i, j, kk };
                gkeepa(g, &xa, 1);
                gkeepb(g, kb, 3);
                return d2;
            }
            if (d2 < best) {
                CANDIDATE(d2);
                bna = 1; bia[0] = xa;
                bnb = 3; bib[0] = i; bib[1] = j; bib[2] = kk;
            }
        }
    }
#undef CANDIDATE
    if (!found) return 0.0f;
    memcpy(g->wa, bwa, 12); memcpy(g->wb, bwb, 12); memcpy(out, bout, 12);
    gkeepa(g, bia, bna);
    gkeepb(g, bib, bnb);
    return best;
}

/* 0x5b3390: the distance between the two shapes. Cold (+0x118 clear): one
   support point of each, found along the line between their first vertices;
   warm: the points of the last call again, at the shapes' new poses. Then
   add the supports along the closest direction until neither shape gives a
   new point or the distance stops shrinking (the block of the last better
   step is kept, 0x598130 / 0x598240). 0 when the shapes touch (under 1e-5
   squared) or overlap. wa / wb: the closest points. */
static float gjk(const Pair *q, GBlk *g, float *wa, float *wb)
{
    const float *o = q->pos;
    float pa[3], pb[3], out[3], d2;
    int ia, ib;
#define REL(P) do { (P)[0] -= o[0]; (P)[1] -= o[1]; (P)[2] -= o[2]; } while (0)
#define ABS(P) do { (P)[0] += o[0]; (P)[1] += o[1]; (P)[2] += o[2]; } while (0)
    if (!g->warm) {
        float a0[3], b0[3], dir[3], nd[3];
        hull_vertex(q->c, q->pos, q->R, 0, a0);       /* 0x5aad20(0): the first vertices */
        vert_b(q, 0, b0);
        fsub(dir, b0, a0);
        nd[0] = -dir[0]; nd[1] = -dir[1]; nd[2] = -dir[2];
        ia = sup_hull(q, dir, pa); REL(pa);
        ib = sup_b(q, nd, pb); REL(pb);
        memcpy(g->a[0], pa, 12); memcpy(g->b[0], pb, 12);
        g->ia[0] = ia; g->ib[0] = ib; g->na = g->nb = 1;
        memcpy(g->wa, pa, 12); memcpy(g->wb, pb, 12);
        fsub(out, g->wb, g->wa);
        d2 = fdot(out, out);
        if (!(d2 > 1e-5f)) goto touch;
        g->warm = 1;
    } else {
        for (int m = 0; m < g->na; m++) { hull_vertex(q->c, q->pos, q->R, g->ia[m], g->a[m]); REL(g->a[m]); }
        for (int m = 0; m < g->nb; m++) { vert_b(q, g->ib[m], g->b[m]); REL(g->b[m]); }
        d2 = gjk_feats(g, out);
        if (!(d2 > 1e-5f)) goto touch;
    }
    {
        GBlk keep = *g;
        for (int it = 0; it < 64; it++) {               /* cap: a NaN distance never improves */
            float nout[3] = { -out[0], -out[1], -out[2] };
            float sa[3], sb[3];
            ia = sup_hull(q, out, sa); REL(sa);
            ib = sup_b(q, nout, sb); REL(sb);
            int newa = 1, newb = 1;
            for (int m = 0; m < g->na; m++) if (g->ia[m] == ia) newa = 0;
            if (newa && g->na < 4) { g->ia[g->na] = ia; memcpy(g->a[g->na], sa, 12); g->na++; }
            for (int m = 0; m < g->nb; m++) if (g->ib[m] == ib) newb = 0;
            if (newb && g->nb < 4) { g->ib[g->nb] = ib; memcpy(g->b[g->nb], sb, 12); g->nb++; }
            if (!newa && !newb) break;                  /* nothing new: this is it */
            float nd2 = gjk_feats(g, out);
            if (nd2 == 0.0f) { d2 = 0.0f; goto touch; }  /* overlap */
            if (!(nd2 < d2)) { *g = keep; break; }      /* no better (or NaN): the last block */
            keep = *g;
            d2 = nd2;
        }
        memcpy(wa, g->wa, 12); memcpy(wb, g->wb, 12); ABS(wa); ABS(wb);
        return sqrtf(d2);
    }
touch:
    memcpy(wa, g->wa, 12); memcpy(wb, g->wb, 12); ABS(wa); ABS(wb);
    return 0.0f;
#undef REL
#undef ABS
}

/* ---- the manager --------------------------------------------------------- */
#define MAXPAIR 512
typedef struct {
    int   a, b;               /* cars; b < 0: car a against its polygon */
    int   poly;               /* index into car a's polygons */
    int   count;              /* +0x240 resolutions this frame */
    GBlk  g;                  /* +0x40 the distance routine's points */
    int   prism;              /* +0x221 the polygon's prism state the points are for */
    int   tested;             /* the distance below is from this run */
    float dist;               /* +0x10 the last distance test's result (t = 0) */
    int   touch;              /* +0x244 a contact this run (distance <= 0.001) */
} CPair;
typedef struct {
    int   pair;
    float t;                  /* time back from the end of the step */
    float sep[3];             /* push-out of a stuck contact (pair +0x20) */
    float wa[3], wb[3];       /* witness points of the last distance test */
} Contact;
typedef struct {
    CarCollCar *cars; int n;
    WColl *w;
    WCollPoly (*poly)[MAXPOLY];
    int *np;
    CPair *pairs; int npairs;
    const float (*pos)[3];    /* each body's centre, read once per run (11-15) */
} World;

static N2Body body_of(const CarCollCar *k)
{
    N2Body b;
    if (k->ops) k->ops->body(k->obj, &b); else n2sim_body(k->s, &b);
    return b;
}

/* pose at time t back from now (0x593280): linear between the start and the
   end of the step, matrix element by element */
static void pose_at(const CarColl *c, const N2Body *b, float t, float *pos, float *R)
{
    if (c->span < 1e-6f) { memcpy(pos, b->pos, 12); memcpy(R, b->R, 64); return; }
    float f = (c->span - t) / c->span;
    for (int k = 0; k < 3; k++) pos[k] = c->ppos[k] + (b->pos[k] - c->ppos[k]) * f;
    for (int k = 0; k < 16; k++) R[k] = c->pR[k] + (b->R[k] - c->pR[k]) * f;
}

/* distance of a pair at time t back; shiftA / shiftB move the shapes */
static float test_pair(const World *W, CPair *p, float t, const float *shiftA,
                       const float *shiftB, int prism, float *wa, float *wb)
{
    const CarCollCar *A = &W->cars[p->a];
    N2Body ba = body_of(A);
    Pair q;
    memset(&q, 0, sizeof q);
    q.c = A->c;
    pose_at(A->c, &ba, t, q.pos, q.R);
    if (shiftA) for (int k = 0; k < 3; k++) q.pos[k] -= shiftA[k];
    if (p->b < 0) { q.p = &W->poly[p->a][p->poly]; q.prism = prism; }
    else {
        const CarCollCar *B = &W->cars[p->b];
        N2Body bb = body_of(B);
        q.c2 = B->c;
        pose_at(B->c, &bb, t, q.pos2, q.R2);
        if (shiftB) for (int k = 0; k < 3; k++) q.pos2[k] += shiftB[k];
    }
    /* 0x5b3d20: a polygon that turned to or from a prism starts the points over */
    if (p->b < 0 && p->prism != prism) { p->prism = prism; p->g.warm = 0; }
    return gjk(&q, &p->g, wa, wb);
}

/* the polygon's normal, turned toward the car's bounds centre at the start
   of the step (0x593df0 with the collidable's +0x100) */
static void poly_side(const CarColl *c, const WCollPoly *p, float *n)
{
    float ref[3], cen[3] = { 0, 0, 0 };
    rowmul(ref, c->centre, c->pR);
    for (int k = 0; k < 3; k++) ref[k] += c->ppos[k];
    for (int i = 0; i < p->nv; i++) for (int k = 0; k < 3; k++) cen[k] += p->v[i][k] / (float)p->nv;
    float s = (ref[0] - cen[0]) * p->n[0] + (ref[1] - cen[1]) * p->n[1] + (ref[2] - cen[2]) * p->n[2];
    for (int k = 0; k < 3; k++) n[k] = s < 0 ? -p->n[k] : p->n[k];
}

/* 0x5b42f0: a contact that penetrates all the way back: push the shapes
   apart in growing steps (0.005, then +0.01 a step up to 0.1), the vertical
   part held within +-0.15 m. Against a polygon along its normal, the car
   only; car against car along the line between them, A back and B on. */
static void push_out(const World *W, CPair *p, float t, Contact *k)
{
    const CarCollCar *A = &W->cars[p->a];
    float N[3];
    if (p->b < 0) {
        float ns[3];
        poly_side(A->c, &W->poly[p->a][p->poly], ns);
        N[0] = -ns[0]; N[1] = -ns[1]; N[2] = -ns[2];      /* car -> polygon */
    } else {
        N2Body ba = body_of(A), bb = body_of(&W->cars[p->b]);
        float pa[3], pb[3], R[16];
        pose_at(A->c, &ba, t, pa, R);
        pose_at(W->cars[p->b].c, &bb, t, pb, R);
        for (int i = 0; i < 3; i++) N[i] = pb[i] - pa[i];
        float l = sqrtf(dot3(N, N));
        if (l > 0) { N[0] /= l; N[1] /= l; N[2] /= l; }
    }
    float s = 0, step = 0.005f;
    for (int it = 0; it < 20; it++) {
        s += step;
        if (step < 0.1f) step += 0.01f;
        float d[3] = { N[0] * s, N[1] * s, N[2] * s };
        if (d[2] <= -0.15f) d[2] = -0.15f;
        if (d[2] > 0.15f) d[2] = 0.15f;
        float dist = test_pair(W, p, t, d, p->b < 0 ? NULL : d, 1, k->wa, k->wb);
        memcpy(k->sep, d, 12);
        if (!(dist <= 0.001f && dist <= 1e-5f)) break;
    }
}

/* 0x5b5050 narrowphase + 0x5b4be0 contact time; 1 if touching */
static int narrow(World *W, int pi, Contact *k)
{
    CPair *p = &W->pairs[pi];
    const CarCollCar *A = &W->cars[p->a];
    memset(k, 0, sizeof *k);
    k->pair = pi;
    float dist = test_pair(W, p, 0, NULL, NULL, 0, k->wa, k->wb);
    p->tested = 1; p->dist = dist;
    if (!(dist <= 0.001f)) return 0;
    p->touch = 1;
    /* +0x140 crash time: walls (0x592ed0), and both cars of a car contact */
    if (p->b < 0) { if ((W->poly[p->a][p->poly].flags & 2) && !A->ops) n2sim_collision(A->s); }
    else {
        if (!A->ops) n2sim_collision(A->s);
        if (!W->cars[p->b].ops) n2sim_collision(W->cars[p->b].s);
    }
    if (!(dist <= 1e-5f)) return 1;                       /* touching now: t = 0 */
    float span = A->c->span;
    if (p->b >= 0 && W->cars[p->b].c->span < span) span = W->cars[p->b].c->span;
    float lo = 0, hi = span, best = -1, mid = 0;
    for (int it = 0; it < 8; it++) {
        mid = (hi - lo) * 0.5f + lo;
        float d = test_pair(W, p, mid, NULL, NULL, 1, k->wa, k->wb);
        if (!(d <= 1e-5f)) {
            if (d <= 0.001f) { k->t = mid; return 1; }
            hi = mid;
            if (best == -1 || mid < best) best = mid;
        } else lo = mid;
    }
    if (best <= 0) { push_out(W, p, mid, k); k->t = mid; }
    else k->t = best;
    return 1;
}

/* 0x593290 / vt+0x30: back to the contact time -- pose, and momentum from
   the velocity then (0x593590) -- moved by sep (subtracted for A) */
static void rewind_car(CarCollCar *K, float t, const float *sep, float sgn)
{
    CarColl *c = K->c;
    N2Body b = body_of(K);
    float pos[3], R[16], P[3];
    pose_at(c, &b, t, pos, R);
    float f = c->span < 1e-6f ? 1.0f : (c->span - t) / c->span;
    for (int i = 0; i < 3; i++) {
        float v = c->pv[i] + (b.v[i] - c->pv[i]) * f;
        P[i] = v * b.mass;
        pos[i] += sgn * sep[i];
    }
    if (K->ops) K->ops->pose(K->obj, pos, R, P); else n2sim_body_pose(K->s, pos, R, P);
}
/* 0x5a9f10 / vt+0x2c: the car runs again for the time it was put back */
static void advance_car(CarCollCar *K, float t)
{
    CarColl *c = K->c;
    N2Body b = body_of(K);
    memcpy(c->ppos, b.pos, 12); memcpy(c->pR, b.R, 64); memcpy(c->pv, b.v, 12);
    c->span = t;
    if (t > 0) { if (K->ops) K->ops->advance(K->obj, t); else n2sim_step(K->s, t); }
}
/* 0x593a40: inverse inertia in use x 0.4 (x 1 in crash mode, car +0x38 == 1) */
static void inv_inertia_arm(const N2Body *b, const float *r, const float *N, float *out, float k)
{
    float rn[3], irn[3];
    cross3(rn, r, N);
    rowmul(irn, rn, b->Iinv);
    irn[0] *= k; irn[1] *= k; irn[2] *= k;
    cross3(out, irn, r);
}
/* 0x593a00: the solver treats a car as at least 1.85 t */
static float solver_invmass(const N2Body *b) { return b->invmass > 1.0f / 1.85f ? 1.0f / 1.85f : b->invmass; }
/* a prop's GetMassInverse is its own (0 while anchored, smokeable.c) */
static float body_invmass(const CarCollCar *K, const N2Body *b) { return K->ops ? b->invmass : solver_invmass(b); }
/* 0x5a0b00: impulse on a car, scaled as if it weighed at least 1.85 t; the
   angular part x 0.4, whole in crash mode (car +0x38 == 1) */
static float car_ang(const CarCollCar *K) { return K->ops || n2sim_dampen(K->s) == 1 ? 1.0f : 0.4f; }
static void apply_car(CarCollCar *K, const float *I, const float *r)
{
    if (K->ops) { K->ops->impulse(K->obj, I, r); return; }
    N2Body b = body_of(K);
    float sc = 1.0f;
    if (b.invmass > 1.0f / 1.85f) sc = (1.0f / 1.85f) / b.invmass;
    float J[3] = { I[0] * sc, I[1] * sc, I[2] * sc };
    acid_car_impulse(K->s, J, r);                                    /* AddDamage 0x593bf0 (acid.c) */
    n2sim_body_impulse(K->s, J, r, car_ang(K));
}

static void norm3(float *v)                                          /* 0x43cf10 */
{
    float l = sqrtf(dot3(v, v));
    if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
/* the world random 0x5ea4e0 (*0x890080), here the port's own */
static unsigned wild_rng = 0x2545f491u;
static unsigned wild_rand(unsigned n) { wild_rng = wild_rng * 1103515245u + 12345u; return (wild_rng >> 16) % n; }

/* Collision::CheckForWildCollision 0x59fb70, from AddImpulse 0x5a9620 with
   the impulse on A (I), both bodies cars: an impulse of 30 t m/s or more
   (|I| capped at 40) puts a car not yet crashing into crash mode
   (SetDampenMode 1, contact event +0x10) and kicks it: a head-on hit
   (impulse against the travel, cos < -0.97) gets a flat sideways shove of
   |cos| x 0.2 x J, and every hit a lift of |cos| x 0.1 x J x 1.1 (1.2 in a
   drag race), capped at 4.1 x that factor, at the contact moved in the
   car's plane: head-on randomly mirrored front / back (x -1.1) with the
   side offset x 0.2 past 0.13 m; otherwise the side offset x 1.19 kept
   within half the front track x 1.19. Returns a bit per car that crashed
   (the drag race totalling and the jump cam are the caller's). */
static int wild_collision(CarCollCar *A, CarCollCar *B, const float *pt, const float *I)
{
    if (A->ops || !B || B->ops) return 0;
    float J = sqrtf(dot3(I, I));
    if (!(J < 40.0f)) J = 40.0f;                                     /* 0x7a1858 */
    if (J < 30.0f) return 0;                                         /* 0x7a1868 */
    int fired = 0;
    for (int k = 0; k < 2; k++) {
        CarCollCar *K = k ? B : A;
        if (n2sim_dampen(K->s) == 1) continue;
        fired |= 1 << k;
        n2sim_set_dampen(K->s, 1);
        N2Body b = body_of(K);
        float r[3] = { pt[0] - b.pos[0], pt[1] - b.pos[1], pt[2] - b.pos[2] };
        float n[3] = { k ? -I[0] : I[0], k ? -I[1] : I[1], k ? -I[2] : I[2] };
        float f[3] = { b.v[0], b.v[1], b.v[2] };
        norm3(n); norm3(f);
        float c = dot3(n, f);
        int head = c < -0.97f;                                       /* 0x7a1854 */
        if (head) {
            float t[3] = { n[0] - f[0] * c, n[1] - f[1] * c, n[2] - f[2] * c };
            norm3(t);
            float q = -(fabsf(c) * 0.2f * J);                        /* 0x7a1838 */
            t[0] *= q; t[1] *= q; t[2] = 0;
            apply_car(K, t, r);
        }
        b = body_of(K);
        r[0] = pt[0] - b.pos[0]; r[1] = pt[1] - b.pos[1]; r[2] = pt[2] - b.pos[2];
        float rl[3] = { dot3(r, b.R), dot3(r, b.R + 4), 0 };
        if (head) {
            if (wild_rand(100) >= 50) rl[0] = -(rl[0] * 1.1f);       /* 0x7a184c */
            if (fabsf(rl[1]) > 0.13f) rl[1] *= 0.2f;                 /* 0x7a1850, 0x784554 */
        } else {
            float w = fabsf(n2sim_param(K->s, 0x14) - n2sim_param(K->s, 0x44)) * 1.19f * 0.5f;
            rl[1] *= 1.19f;                                          /* 0x7a1844 */
            if (rl[1] > w) rl[1] = w;
            else if (rl[1] < -w) rl[1] = -w;
            rl[0] *= 1.0f;                                           /* 0x7a1848 */
        }
        for (int i = 0; i < 3; i++) r[i] = rl[0] * b.R[i] + rl[1] * b.R[4 + i] + rl[2] * b.R[8 + i];
        float kd = n2sim_drag(K->s) ? 1.2f : 1.1f, lim = 4.1f * kd;  /* 0x7a1860 / 0x7a185c, 0x7a183c */
        float z = fabsf(c) * 0.1f * J * kd;                          /* 0x7a1840 */
        if (z > lim) z = lim; else if (z < -lim) z = -lim;
        printf("carcoll: crash mode (impulse %.1f, cos %.2f%s, lift %.2f)\n", J, c, head ? " head-on" : "", z);
        float up[3] = { 0, 0, z };
        apply_car(K, up, r);
    }
    return fired;
}

/* The Collision objects (0x5a9430), for each body's count (CollisionBody
   +0x18 on PC, GC +0x16: BeginCollision 0x5a0590 inc, EndCollision 0x5929c0
   dec). CollisionImpulse 0x5af0e0 makes one per pair (witness +0x244, made at
   0x5af11c) and AddImpulse 0x5a9620 stamps it with WorldTimer (+0xc, at
   0x5a9684); Collision::Update 0x592bd0 ends it once 0.3 s (0x7fbee0) have
   passed since (DoTimestepCollisions, GC 80210b64), and a witness with a
   Collision never expires (HasExpired GC 801ad444). One made without an
   impulse is stamped 0 and ends in the same step, so only impulses count.
   Keyed by the two bodies (a polygon by its id: each track polygon is its own
   body in the original). */
#define MAXCOLL 128
typedef struct { const void *a, *b; int poly; float t; } BodyColl;
static BodyColl g_coll[MAXCOLL];
static int g_ncoll;
static float g_coll_now;
static const void *body_key(const CarCollCar *k) { return k->ops ? k->obj : (const void *)k->s; }
static void coll_impulse(const CarCollCar *A, const CarCollCar *B, int poly)
{
    const void *a = body_key(A), *b = B ? body_key(B) : NULL;
    int i;
    for (i = 0; i < g_ncoll; i++)
        if (g_coll[i].poly == poly && ((g_coll[i].a == a && g_coll[i].b == b) || (b && g_coll[i].a == b && g_coll[i].b == a)))
            break;
    if (i == g_ncoll) {
        if (g_ncoll >= MAXCOLL) return;
        g_coll[g_ncoll++] = (BodyColl){ a, b, poly, 0 };
    }
    g_coll[i].t = g_coll_now;
}
void carcoll_world_time(float dt)
{
    g_coll_now += dt;
    int keep = 0;
    for (int i = 0; i < g_ncoll; i++)
        if (g_coll_now - g_coll[i].t < 0.3f) g_coll[keep++] = g_coll[i];
    g_ncoll = keep;
}
int carcoll_body_collisions(const void *body)
{
    static int off = -1;
    int n = 0;
    if (off < 0) off = getenv("OPENUG2_NOBODYCOLL") != NULL;   /* test aid: the count stays 0 */
    if (!body || off) return 0;
    for (int i = 0; i < g_ncoll; i++)
        if ((g_coll[i].a == body || g_coll[i].b == body) && g_coll_now - g_coll[i].t < 0.3f) n++;
    return n;
}

/* 0x5b2610 + 0x5af0e0 for one contact; 1 when an impulse was applied (the
   bodies were closing) */
static int resolve(World *W, const Contact *k, CarCollHit *hit, CarCollWake wake, void *user)
{
    CPair *p = &W->pairs[k->pair];
    CarCollCar *A = &W->cars[p->a], *B = p->b >= 0 ? &W->cars[p->b] : NULL;
    const WCollPoly *poly = B ? NULL : &W->poly[p->a][p->poly];
    /* vt+0x48 (0x593940) on both, each with the other: a car touched by a
       human player's car may switch to the full simulation */
    if (wake && !A->ops && (!B || !B->ops)) {
        wake(user, p->a, B ? p->b : -1);
        if (B) wake(user, p->b, p->a);
    }
    rewind_car(A, k->t, k->sep, -1.0f);
    if (B) rewind_car(B, k->t, k->sep, 1.0f);
    N2Body ba = body_of(A), bb;
    if (B) bb = body_of(B);

    /* 0x594000: contact point midway between the witnesses; the normal
       from the polygon's side (car -> polygon), or between two cars from
       witness A to witness B (centre to centre if they coincide) */
    float pt[3], N[3];
    for (int i = 0; i < 3; i++) pt[i] = (k->wa[i] + k->wb[i]) * 0.5f;
    if (!B) {
        float ns[3];
        poly_side(A->c, poly, ns);
        N[0] = -ns[0]; N[1] = -ns[1]; N[2] = -ns[2];
    } else {
        for (int i = 0; i < 3; i++) N[i] = k->wb[i] - k->wa[i];
        float l = sqrtf(dot3(N, N));
        if (l < 1e-6f) { for (int i = 0; i < 3; i++) N[i] = bb.pos[i] - ba.pos[i]; l = sqrtf(dot3(N, N)); }
        if (l > 0) { N[0] /= l; N[1] /= l; N[2] /= l; }
    }
    float rA[3] = { pt[0] - ba.pos[0], pt[1] - ba.pos[1], pt[2] - ba.pos[2] }, rB[3] = { 0, 0, 0 }, wr[3];
    float va[3], vb[3] = { 0, 0, 0 };
    cross3(wr, ba.w, rA);
    for (int i = 0; i < 3; i++) va[i] = ba.v[i] + wr[i];
    if (B) {
        for (int i = 0; i < 3; i++) rB[i] = pt[i] - bb.pos[i];
        cross3(wr, bb.w, rB);
        for (int i = 0; i < 3; i++) vb[i] = bb.v[i] + wr[i];
    }
    float dv[3] = { vb[0] - va[0], vb[1] - va[1], vb[2] - va[2] };
    float vn = dot3(dv, N);
    int applied = 0;
    if (vn < 0) {
        float vt[3] = { dv[0] - N[0] * vn, dv[1] - N[1] * vn, dv[2] - N[2] * vn };
        float e, mu, invm = body_invmass(A, &ba), K, t3[3];
        float ka = car_ang(A);
        if (!B) {
            if (A->ops) {                       /* a prop: the max with the polygon's 0.001 (0x5941c0) */
                e = A->ops->bounce > 0.001f ? A->ops->bounce : 0.001f;
                mu = A->ops->friction > 0.001f ? A->ops->friction : 0.001f;
            } else {
                float anz = fabsf(poly->n[2]);
                e = anz < 0.1f ? 0.05f : 0.1f;                      /* 0x5a0c90 */
                if (anz > 0.7f) {                                   /* 0x5a0be0 */
                    mu = ((float)(4 - ba.wheels_down) * 0.17f + 1.0f) * 0.3f;
                    if (mu > 1.0f) mu = 1.0f;
                } else mu = anz < 0.1f ? 0.3f * 0.3f : 0.3f;
            }
            inv_inertia_arm(&ba, rA, N, t3, ka);
            K = invm + dot3(t3, N);
        } else {
            float invmb = body_invmass(B, &bb), t4[3];
            /* a car's own against another body is 0.1 / 0.3; the max of both sides */
            float ea = A->ops ? A->ops->bounce : 0.1f, eb = B->ops ? B->ops->bounce : 0.1f;
            float ma = A->ops ? A->ops->friction : 0.3f, mb = B->ops ? B->ops->friction : 0.3f;
            e = ea > eb ? ea : eb; mu = ma > mb ? ma : mb;
            inv_inertia_arm(&ba, rA, N, t3, ka);
            inv_inertia_arm(&bb, rB, N, t4, car_ang(B));
            K = invm + invmb + dot3(t3, N) + dot3(t4, N);
            if (invmb > invm) invm = invmb;
        }
        if (!(K > 1e-9f) || !(invm > 0)) {     /* two immovable bodies */
            advance_car(A, k->t);
            if (B) advance_car(B, k->t);
            return 0;
        }
        float j = -(e + 1.0f) * vn / K, jmin = 0.3f / invm;        /* 0x7fbf24 */
        if (j < jmin) j = jmin;
        /* vt+0x4c ConsiderImpulse on both (a car's is 1): the normal impulse
           is scaled by the smaller (an anchored prop breaking loose) */
        if (A->ops || (B && B->ops)) {
            float Jn[3] = { N[0] * j, N[1] * j, N[2] * j }, mJn[3] = { -Jn[0], -Jn[1], -Jn[2] };
            float sb = B && B->ops ? fabsf(B->ops->consider(B->obj, Jn, rB)) : 1.0f;
            float sa = A->ops ? fabsf(A->ops->consider(A->obj, mJn, rA)) : 1.0f;
            j *= sa < sb ? sa : sb;
        }
        float lt = sqrtf(dot3(vt, vt)), fr = mu * j;
        if (lt < 1.0f) fr = fr / 1.0f * lt;                        /* 0x7fbf30 */
        float Ft[3] = { 0, 0, 0 };
        if (lt > 1e-5f) { float q = -1.0f / lt * fr; Ft[0] = vt[0] * q; Ft[1] = vt[1] * q; Ft[2] = vt[2] * q; }
        float I[3] = { N[0] * j + Ft[0], N[1] * j + Ft[1], N[2] * j + Ft[2] };
        /* 0x5a9620 effects (sparks, sounds) are the caller's, from the hits;
           its CheckForWildCollision here */
        float mI[3] = { -I[0], -I[1], -I[2] };
        int wild = wild_collision(A, B, pt, mI);
        if (wild) {
            /* contact event +0x10: a car on all four wheels takes the
               impulse at an arm without its up part (car +0xb0) */
            CarCollCar *Ks[2] = { A, B };
            float *rs[2] = { rA, rB };
            for (int q = 0; q < 2; q++) {
                N2Body cb = body_of(Ks[q]);
                if (cb.wheels_down != 4) continue;
                float u = dot3(rs[q], cb.R + 8);
                for (int i = 0; i < 3; i++) rs[q][i] -= cb.R[8 + i] * u;
            }
        }
        if (B) apply_car(B, I, rB);
        apply_car(A, mI, rA);
        if (A->ops) A->ops->touch(A->obj, B && !B->ops);
        if (B && B->ops) B->ops->touch(B->obj, !A->ops);
        if (hit) {
            memcpy(hit->point, pt, 12);
            hit->impulse[0] = -I[0]; hit->impulse[1] = -I[1]; hit->impulse[2] = -I[2];
            hit->slide = lt;
            memcpy(hit->normal, N, 12);
            hit->surface = B ? -1 : poly->surface;
            hit->wall = B ? 0 : (poly->flags & 2) != 0;
            hit->car = p->a; hit->other = p->b;
            hit->poly = B ? -1 : poly->id;
            hit->poly_nz = B ? 0 : poly->n[2];
            hit->wild = wild;
        }
        coll_impulse(A, B, B ? -1 : poly->id);              /* Collision::AddImpulse 0x5a9620 */
        applied = 1;
    }
    advance_car(A, k->t);
    if (B) advance_car(B, k->t);
    return applied;
}

void carcoll_begin(CarColl *c, const N2Sim *s, float dt)
{
    N2Body b;
    n2sim_body(s, &b);
    hull_bounds(c, b.com);
    memcpy(c->ppos, b.pos, 12); memcpy(c->pR, b.R, 64); memcpy(c->pv, b.v, 12);
    c->span = dt * n2sim_time_scale();                                 /* the world step n2sim_step takes */
}

void carcoll_begin_body(CarColl *c, const N2Body *b, float dt)
{
    hull_bounds(c, b->com);
    memcpy(c->ppos, b->pos, 12); memcpy(c->pR, b->R, 64); memcpy(c->pv, b->v, 12);
    c->span = dt;
}

/* 0x5937b0: is the polygon within reach of the car's bounds? */
static int near_poly(const CarColl *c, const N2Body *b, const WCollPoly *p)
{
    float cz = b->R[8] * p->n[0] + b->R[9] * p->n[1] + b->R[10] * p->n[2];
    if (cz > 1.0f) cz = 1.0f;
    float h = dot3(b->pos, p->n) - p->d;
    float ext = cz * h < 0 ? c->above : c->below;
    float reach = sqrtf(1.0f - cz * cz) * c->rad + fabsf(cz) * ext;
    if (!(fabsf(h) < reach)) return 0;
    /* 0x5d3450: the centre of mass within the bounding radius of the face */
    float q[3] = { b->pos[0] - p->n[0] * h, b->pos[1] - p->n[1] * h, b->pos[2] - p->n[2] * h };
    float best = 1e30f;
    int inside = 1;
    for (int i = 0; i < p->nv; i++) {
        const float *a = p->v[i], *e = p->v[(i + 1) % p->nv];
        float ab[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] }, aq[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] }, x[3];
        cross3(x, ab, aq);
        if (dot3(x, p->n) < 0) inside = 0;
        float l2 = dot3(ab, ab), u = l2 > 0 ? dot3(aq, ab) / l2 : 0;
        if (u < 0) u = 0; else if (u > 1) u = 1;
        float d[3] = { aq[0] - ab[0] * u, aq[1] - ab[1] * u, aq[2] - ab[2] * u };
        float dd = dot3(d, d);
        if (dd < best) best = dd;
    }
    float dist = inside ? fabsf(h) : sqrtf(best + h * h);
    return dist <= c->rad;
}

/* 0x5b23b0 for one car: its polygons (0x5ae930, box 0x5a9de0) and the cars
   whose boxes overlap its own; new pairs only */
static Uint64 g_bp_world;   /* broadphase census: the world-polygon part */
static void broadphase(World *W, int ci)
{
    CarCollCar *K = &W->cars[ci];
    N2Body b = body_of(K);
    CarColl *c = K->c;
    int sleeper = K->ops && K->ops->asleep;
    Uint64 bw0 = SDL_GetPerformanceCounter();
    if (W->w && K->wc && !sleeper) {
        float mn[2] = { b.pos[0] - c->rad, b.pos[1] - c->rad }, mx[2] = { b.pos[0] + c->rad, b.pos[1] + c->rad };
        static WCollPoly cand[160];
        int n = wcoll_gather(W->w, K->wc, b.pos, mn, mx, cand, 160);
        for (int i = 0; i < n && W->np[ci] < MAXPOLY; i++) {
            int have = 0;
            for (int j = 0; j < W->np[ci] && !have; j++) have = W->poly[ci][j].id == cand[i].id;
            if (have || !near_poly(c, &b, &cand[i])) continue;
            if (W->npairs >= MAXPAIR) break;
            W->poly[ci][W->np[ci]] = cand[i];
            W->pairs[W->npairs++] = (CPair){ ci, -1, W->np[ci], 0 };
            W->np[ci]++;
        }
    }
    g_bp_world += SDL_GetPerformanceCounter() - bw0;
    /* car against car: 0x593980 both ways -- neither switched off (+0x17),
       both colliding with cars (+0x190, set by 0x5aec20) */
    if (K->nocollide) return;
    for (int o = 0; o < W->n; o++) {
        if (o == ci || W->cars[o].nocollide) continue;
        if (sleeper && W->cars[o].ops && W->cars[o].ops->asleep) continue;
        if ((K->noprops && W->cars[o].ops) || (K->ops && W->cars[o].noprops)) continue;
        /* the box test first (port order only: both tests just skip, and the
           pair-list scan below is the costly one with many cars) */
        /* positions read once per run (11-15, PERF_PLAN P5): a whole body
           copy for every other car, both ways, ~380 a run with 20 bodies */
        const float *op = W->pos[o];
        float r = c->rad + W->cars[o].c->rad;
        if (fabsf(op[0] - b.pos[0]) > r || fabsf(op[1] - b.pos[1]) > r) continue;
        int have = 0;
        for (int j = 0; j < W->npairs && !have; j++)
            have = W->pairs[j].b >= 0 && ((W->pairs[j].a == ci && W->pairs[j].b == o) || (W->pairs[j].a == o && W->pairs[j].b == ci));
        if (have) continue;
        if (W->npairs >= MAXPAIR) break;
        W->pairs[W->npairs++] = (CPair){ ci < o ? ci : o, ci < o ? o : ci, -1, 0 };
    }
}

static void insert_contact(Contact *list, int *nl, const Contact *k)
{
    int at = 0;                                            /* earliest first (0x43bee0) */
    while (at < *nl && k->t <= list[at].t) at++;
    memmove(&list[at + 1], &list[at], (size_t)(*nl - at) * sizeof *list);
    list[at] = *k; (*nl)++;
}

static WCollPoly g_polys[CARCOLL_MAXCARS][MAXPOLY];
static CPair g_pairs[MAXPAIR];
static int g_npairs;

int carcoll_pairs(int (*out)[3], int max)
{
    int n = 0;
    for (int i = 0; i < g_npairs && n < max; i++, n++) {
        out[n][0] = g_pairs[i].a; out[n][1] = g_pairs[i].b;
        out[n][2] = g_pairs[i].b < 0 ? g_polys[g_pairs[i].a][g_pairs[i].poly].id : -1;
    }
    return n;
}

int carcoll_car_pairs(int (*ab)[2], float *dist, int *touch, int max)
{
    int n = 0;
    for (int i = 0; i < g_npairs && n < max; i++) {
        const CPair *p = &g_pairs[i];
        if (p->b < 0 || !p->tested) continue;
        ab[n][0] = p->a; ab[n][1] = p->b; dist[n] = p->dist; touch[n] = p->touch; n++;
    }
    return n;
}

static int g_prop_world_cap = 1000;
void carcoll_set_prop_world_cap(int cap) { g_prop_world_cap = cap; }

int carcoll_run_all(CarCollCar *cars, int n, WColl *w, CarCollHit *hits, int maxhits,
                    CarCollWake wake, void *user)
{
    g_npairs = 0;
    if (!cars || n <= 0) return 0;
    WCollPoly (*polys)[MAXPOLY] = g_polys;
    static int np[CARCOLL_MAXCARS];
    CPair *pairs = g_pairs;
    static Contact list[MAXPAIR];
    if (n > CARCOLL_MAXCARS) n = CARCOLL_MAXCARS;
    World W = { cars, n, w, polys, np, pairs, 0 };
    int nl = 0, done = 0;
    for (int i = 0; i < n; i++) np[i] = 0;
    /* PERF_PLAN P5 census: where a contacts pass goes (logged every 1200 runs) */
    static double tb, tn, tr, tw; static long runs, pairs_sum, iters_sum;
    Uint64 c0 = SDL_GetPerformanceCounter();
    static float bpos[CARCOLL_MAXCARS][3];
    for (int i = 0; i < n; i++) { N2Body b = body_of(&cars[i]); memcpy(bpos[i], b.pos, sizeof bpos[i]); }
    W.pos = (const float (*)[3])bpos;
    for (int i = 0; i < n; i++) broadphase(&W, i);
    Uint64 c1 = SDL_GetPerformanceCounter();
    for (int i = 0; i < W.npairs; i++) {
        Contact k;
        if (narrow(&W, i, &k)) insert_contact(list, &nl, &k);
    }
    Uint64 c2 = SDL_GetPerformanceCounter();
    int iters = 0, it_world = 0, it_prop = 0, it_car = 0, it_pw = 0;
    for (int iter = 0; nl > 0 && iter < 20; iter++) {     /* DAT_802080 */
        iters++;
        Contact k = list[0];
        CPair *p = &pairs[k.pair];
        if (p->b < 0 && cars[p->a].ops && it_pw >= g_prop_world_cap) {
            /* a prop against the world past the cap: left as it is this tick
               (it rests a little inside; the next tick tries again) */
            for (int i = 1; i < nl; i++) list[i - 1] = list[i];
            nl--; iters--; continue;
        }
        if (p->b < 0 && cars[p->a].ops) it_pw++;
        if (p->b < 0) it_world++; else if (cars[p->a].ops || cars[p->b].ops) it_prop++; else it_car++;
        if (resolve(&W, &k, done < maxhits && hits ? &hits[done] : NULL, wake, user))
            done++;
        p->count++;
        /* 0x5af090: drop the contacts of the moved cars, look for new
           neighbours (0x5b23b0) and test their pairs again, under 2 tries */
        int ma = p->a, mb = p->b;
        int keep = 0;
        for (int i = 1; i < nl; i++) {
            const CPair *q = &pairs[list[i].pair];
            int touches = q->a == ma || q->b == ma || (mb >= 0 && (q->a == mb || q->b == mb));
            if (!touches) list[keep++] = list[i];
        }
        nl = keep;
        int first = W.npairs;
        {   N2Body rb = body_of(&cars[ma]); memcpy(bpos[ma], rb.pos, sizeof bpos[ma]);   /* the moved bodies */
            if (mb >= 0) { rb = body_of(&cars[mb]); memcpy(bpos[mb], rb.pos, sizeof bpos[mb]); } }
        broadphase(&W, ma);
        if (mb >= 0) broadphase(&W, mb);
        for (int i = 0; i < W.npairs; i++) {
            const CPair *q = &pairs[i];
            int touches = q->a == ma || q->b == ma || (mb >= 0 && (q->a == mb || q->b == mb));
            if (!touches || q->count >= 2) continue;       /* DAT_802084 */
            Contact c2;
            if (narrow(&W, i, &c2)) insert_contact(list, &nl, &c2);
        }
        (void)first;
    }
    {   double f = 1000.0 / (double)SDL_GetPerformanceFrequency();
        Uint64 c3 = SDL_GetPerformanceCounter();
        double run_world = (double)g_bp_world * f;   /* this run's world gathers (the worst-run line) */
        tw += run_world; g_bp_world = 0;
        tb += (double)(c1 - c0) * f; tn += (double)(c2 - c1) * f; tr += (double)(c3 - c2) * f;
        pairs_sum += W.npairs; iters_sum += iters;
        {   /* the worst run of the window and what its resolves were against */
            static double wms, wbp, wnr, wrs, wwg; static int wit, ww, wp, wc, wn, wpairs, wpolys;
            double ms = (double)(c3 - c0) * f;
            if (ms > wms) {
                int polys = 0; for (int i = 0; i < n; i++) polys += np[i];
                wms = ms; wit = iters; ww = it_world; wp = it_prop; wc = it_car; wn = n; wpairs = W.npairs; wpolys = polys;
                wbp = (double)(c1 - c0) * f; wnr = (double)(c2 - c1) * f; wrs = (double)(c3 - c2) * f; wwg = run_world;
            }
            if (n > 1 && runs + 1 >= 1200 && wms > 0) {
                printf("contacts worst run: %.2f ms, %d resolves (%d world, %d prop, %d car), %d pairs, %d bodies, %d world polys"
                       " | first broadphase %.2f narrow %.2f resolve loop %.2f, world gathers %.2f ms\n",
                       wms, wit, ww, wp, wc, wpairs, wn, wpolys, wbp, wnr, wrs, wwg);
                wms = 0;
            }
        }
        if (n > 1 && ++runs >= 1200) {
            printf("contacts over %ld runs: broadphase %.3f (world %.3f) narrow %.3f resolve %.3f ms/run, %.1f pairs, %.2f resolves (%d bodies)\n",
                   runs, tb / runs, tw / runs, tn / runs, tr / runs, (double)pairs_sum / runs, (double)iters_sum / runs, n);
            tb = tn = tr = tw = 0; runs = pairs_sum = iters_sum = 0;
        }
    }
    g_npairs = W.npairs;
    return done;                                           /* impulses applied */
}

int carcoll_run(CarColl *c, N2Sim *s, WColl *w, WCollCtx *wc, CarCollHit *hits, int maxhits)
{
    if (!c || !w) return 0;
    CarCollCar one = { c, s, wc, 0, 0 };
    return carcoll_run_all(&one, 1, w, hits, maxhits, NULL, NULL);
}
