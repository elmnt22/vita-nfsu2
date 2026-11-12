/* racepath.c — the race route and race progress (see racepath.h).
 * Addresses in comments are SPEED2.EXE v1.2. */
#include "racepath.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rdu16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t  rds16(const unsigned char *p) { return (int16_t)rdu16(p); }
static float    rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* ---- the spline (0x57a300 class, type 2: Catmull-Rom, ends extrapolated) --
   control points P[-1..n] of (x, y, z, arc length); P[-1] and P[n] are the
   extrapolated ends (0x570240) */
typedef struct {
    int n;
    float (*P)[4];             /* P[-1..n] valid */
    float (*mem)[4];
    float len;                 /* +0x14 */
    int dirty;                 /* +0x18 */
} Spl;
static const float BASIS[3][4][4] = {                                   /* 0x865230 */
    { { 0, -0.5f, 1, -0.5f }, { 1, 0, -2.5f, 1.5f }, { 0, 0.5f, 2, -1.5f }, { 0, 0, -0.5f, 0.5f } },
    { { -0.5f, 2, -1.5f, 0 }, { 0, -5, 4.5f, 0 }, { 0.5f, 4, -4.5f, 0 }, { 0, -1, 1.5f, 0 } },
    { { 2, -3, 0, 0 }, { -5, 9, 0, 0 }, { 4, -9, 0, 0 }, { -1, 3, 0, 0 } },
};
static Spl *spl_new(int n)                                              /* 0x57a300 + 0x5701d0 */
{
    Spl *s = (Spl *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->mem = (float (*)[4])calloc((size_t)(n + 3), sizeof *s->mem);
    if (!s->mem) { free(s); return NULL; }
    s->P = s->mem + 1;
    s->n = n;
    s->dirty = 1;
    return s;
}
static void spl_free(Spl *s) { if (s) { free(s->mem); free(s); } }
static void spl_rebuild(Spl *s)                                         /* 0x570240 type 2 */
{
    int n = s->n;
    if (n < 2) return;
    for (int c = 0; c < 4; c++) {
        s->P[-1][c] = s->P[0][c] - (s->P[1][c] - s->P[0][c]);
        s->P[n][c] = s->P[n - 1][c] + (s->P[n - 1][c] - s->P[n - 2][c]);
    }
}
static void spl_ext(const Spl *s, int k, float o[4])                    /* 0x570510, one point */
{
    int n = s->n;
    if (k < -1) {
        for (int c = 0; c < 4; c++) o[c] = s->P[0][c] + (s->P[0][c] - s->P[1][c]) * (float)(-k);
    } else if (n < k) {
        for (int c = 0; c < 4; c++) o[c] = s->P[n - 1][c] + (s->P[n - 1][c] - s->P[n - 2][c]) * (float)(k - n + 1);
    } else {
        for (int c = 0; c < 4; c++) o[c] = s->P[k][c];
    }
}
static void spl_eval(Spl *s, float t, int deriv, float o[4])            /* 0x570640 */
{
    if (s->dirty) { spl_rebuild(s); s->dirty = 0; }
    if (s->n == 0) { o[0] = o[1] = o[2] = o[3] = 0; return; }
    int i = (int)t;
    float fl = (float)i;
    if (fl > t) fl -= 1.0f;
    int seg = (int)fl;
    float f = t - fl;
    float Q[4][4];
    if (seg < 0 || seg >= s->n - 1) for (int j = 0; j < 4; j++) spl_ext(s, seg - 1 + j, Q[j]);
    else for (int j = 0; j < 4; j++) memcpy(Q[j], s->P[seg - 1 + j], sizeof Q[j]);
    float T[4] = { 1.0f, f, f * f, f * f * f }, w[4];                   /* 0x570440 */
    for (int j = 0; j < 4; j++)
        w[j] = BASIS[deriv][j][0] * T[0] + BASIS[deriv][j][1] * T[1] + BASIS[deriv][j][2] * T[2] + BASIS[deriv][j][3] * T[3];
    for (int c = 0; c < 4; c++) o[c] = w[0] * Q[0][c] + w[1] * Q[1][c] + w[2] * Q[2][c] + w[3] * Q[3][c];
}
static void spl_arclen(Spl *s, int steps)                               /* 0x5778d0 */
{
    float acc = 0;
    for (int k = 0; k < s->n; k++) {
        s->P[k][3] = acc;
        s->dirty = 1;
        if ((float)k < (float)(s->n - 1)) {
            float a[4], b[4];
            spl_eval(s, (float)k, 0, a);
            for (int j = 1; j <= steps; j++) {
                spl_eval(s, (float)j / (float)steps + (float)k, 0, b);
                float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
                acc += sqrtf(dx * dx + dy * dy + dz * dz);                /* 0x43ce10 */
                memcpy(a, b, sizeof a);
            }
        }
    }
    s->len = acc;
    spl_rebuild(s);
    s->dirty = 0;
}
static int g_ls_lastit;
static float spl_project(Spl *s, const float p[3], float t, float tol)  /* 0x5774c0 (type 2) */
{
    if (s->dirty) { spl_rebuild(s); s->dirty = 0; }
    if (s->n == 0) return 0;
    float step = 1.0f, ps = 0, pe = 0;
    for (int it = 998; it; it--) {
        g_ls_lastit = 999 - it;
        float a[4], d[4];
        spl_eval(s, t, 0, a);
        spl_eval(s, t, 1, d);
        float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L == 0) return t;
        float e = ((p[0] - a[0]) * d[0] + (p[1] - a[1]) * d[1] + (p[2] - a[2]) * d[2]) / L;
        if (fabsf(e) < tol) return t;
        if (ps != 0 && -((e - pe) / ps) == 0) return t;
        float sv = e / L;
        if (sv <= -step) sv = -step;
        if (step < sv) sv = step;
        t += sv;
        if (sv * ps < 0) step *= 0.5f;
        ps = sv; pe = e;
    }
    return t;
}

/* ---- the road graph ----------------------------------------------------------- */
typedef struct RpSec RpSec;
struct RpSec {
    const unsigned char *raw;  /* the 0xdc-byte record */
    int first, end, cnt;       /* +0x1c +0x1e +0x20 */
    int nlink[2];              /* +0x22 +0x23 */
    struct { RpSec *to; int dir; } link[2][4];   /* +0x24, 8 bytes each */
    float len;                 /* +0x64 */
    float start[2], endp[2];   /* +0x88 +0x90 */
    float bbmin[2], bbmax[2];  /* +0x98 +0xa0 */
    /* the race route's */
    int rev;                   /* +0x68 */
    float splen, arc0;         /* +0x6c +0x70 */
    float pa, pb, scale;       /* +0x74 +0x78 +0x7c */
    float rmin, rmax;          /* +0x80 +0x84 */
    Spl *sp;                   /* +0xa8 */
};
typedef struct {
    float x, y;
    int sec;                   /* +8 */
    unsigned char b10, b11;    /* +0xa +0xb */
    short nb[3];               /* +0xc +0xe +0x10 */
    float prog;                /* +0x14 (rewritten by 0x5d52c0) */
} RpNode;
struct RacePath {
    unsigned char *file;
    int nsec, nnode;
    RpSec *sec;
    RpNode *node;
    const unsigned char *blk; int nblk;          /* chunk 0x3414d (0x24 bytes) */
    const unsigned char *zones; long nzones;     /* chunk 0x3414a (+0x43c / +0x438) */
    float lap, half;                             /* +0x18 +0x1c */
    int circuit, ev, reversed;                   /* +0x24 +0x28 +0x25 */
    int nroute;                                  /* +0x2c */
    struct { int first, count; float len, p0, p1, scale; } route[16];   /* +0x30 */
    RpSec *list[128]; unsigned char dir[128]; int nlist;               /* +0x1b4 +0x3b4 +0x1b0 */
    /* port only: the zones grouped by type (stable: file order kept within
       a type) with their boxes read once, for FindZone's walks */
    struct RpZoneIx { const unsigned char *z; int type; float bb[4]; } *zix;
    int nzix;
};

static void zone_index(RacePath *p);
static int sec_allowed(const RpSec *s, int ev)                          /* 0x5d4f60 */
{
    int a = s->raw[0xac], d = s->raw[0xad];
    if (!a && !d) return 1;
    if (a) {
        for (int i = 0; i < a && i < 7; i++) if (rdu16(s->raw + 0xae + 2 * i) == (unsigned)ev) return 1;
        return 0;
    }
    for (int i = 0; i < d && i < 7; i++) if (rdu16(s->raw + 0xae + 2 * i) == (unsigned)ev) return 0;
    return 1;
}
static int sec_blocked(const RacePath *p, const RpSec *s)
{
    int n = rds16(s->raw + 0xbc);
    for (int i = 0; i < n && i < 15; i++) {
        int k = rds16(s->raw + 0xbe + 2 * i);
        if (k >= 0 && k < p->nblk && p->blk[k * 0x24 + 0x10] != 0) return 1;
    }
    return 0;
}
static int in_box(const float *mn, const float *mx, const float *q, float r)   /* 0x43d410 */
{
    return mn[0] - r <= q[0] && q[0] <= mx[0] + r && mn[1] - r <= q[1] && q[1] <= mx[1] + r;
}
/* 0x5d57a0: the nearest node, over every section (routes 0) or the race
   routes' sections that have a spline (routes 1), within r (-1: any) */
/* OPENUG2_LOCSTAT test aid: locate calls, nearest-node scans (any range /
   128 m), node-loop work, projection iterations; logged every 2000 locates */
static long g_ls_loc, g_ls_nn_any, g_ls_nn_r, g_ls_nn_nodes, g_ls_proj_it, g_ls_noloc;
static int g_ls_on = -1;
static int nearest_node(const RacePath *p, const float *q, int routes, float r)
{
    if (g_ls_on > 0) { if (r == -1.0f) g_ls_nn_any++; else g_ls_nn_r++; }
    float best = r == -1.0f ? 1e8f : r;
    int found = -1;
    if (!routes) {
        for (int i = 0; i < p->nsec; i++) {
            const RpSec *s = &p->sec[i];
            if (!in_box(s->bbmin, s->bbmax, q, best)) continue;
            if (g_ls_on > 0) g_ls_nn_nodes += s->end - s->first;
            for (int k = s->first; k < s->end; k++) {
                float dx = p->node[k].x - q[0], dy = p->node[k].y - q[1], d = sqrtf(dx * dx + dy * dy);
                if (found < 0 || d < best) { best = d; found = k; }
            }
        }
        return found;
    }
    for (int r_ = 0; r_ < p->nroute; r_++)
        for (int j = 0; j < p->route[r_].count; j++) {
            const RpSec *s = p->list[p->route[r_].first + j];
            if (!s->sp || !in_box(s->bbmin, s->bbmax, q, best)) continue;
            if (g_ls_on > 0) g_ls_nn_nodes += s->end - s->first;
            for (int k = s->first; k < s->end; k++) {
                float dx = p->node[k].x - q[0], dy = p->node[k].y - q[1], d = sqrtf(dx * dx + dy * dy);
                if (found < 0 || d < best) { best = d; found = k; }
            }
        }
    return found;
}
/* 0x5d4e30: distance from q to segment a-b; *which = the nearer end */
static float seg_dist(const RpNode *a, const RpNode *b, const float *q, int *which)
{
    float ex = b->x - a->x, ey = b->y - a->y;
    float ax = q[0] - a->x, ay = q[1] - a->y;
    float bx = q[0] - b->x, by = q[1] - b->y;
    float d1 = ex * ax + ey * ay, d2 = -(bx * ex + by * ey);
    if (d1 <= 0) { if (which) *which = 0; return sqrtf(ax * ax + ay * ay); }
    if (d2 <= 0) { if (which) *which = 1; return sqrtf(bx * bx + by * by); }
    if (which) *which = d2 < d1;
    return fabsf(ey * ax - ex * ay) / sqrtf(ey * ey + ex * ex);
}
/* 0x5d5a00: the node whose segment passes nearest (the node or a
   neighbour in its section); returns the distance */
static float node_refine(const RacePath *p, const float *q, int *node)
{
    int k = *node;
    const RpSec *s = &p->sec[p->node[k].sec];
    int res = -1, w;
    float best = 9999999.0f;
    if (k != s->first) {
        float d = seg_dist(&p->node[k - 1], &p->node[k], q, &w);
        if (d < 1e7f) { best = d; res = k - 1 + w; }
    }
    if (k == s->end - 1) { *node = res; return best; }
    float d = seg_dist(&p->node[k], &p->node[k + 1], q, &w);
    if (!(d < best)) { *node = res; return best; }
    *node = k + w;
    return d;
}

float racepath_diff(const RacePath *p, float a, float b)                /* 0x5d5cd0 */
{
    float d = a - b;
    if (p->circuit) {
        if (d < -p->half) return d + p->lap;
        if (p->half < d) d -= p->lap;
    }
    return d;
}

void rploc_init(RpLoc *l)                                               /* 0x5da2f0 */
{
    memset(l, 0, sizeof *l);
    l->node = -1;
    l->dir[0] = 1.0f;
}
static void locate(const RacePath *p, RpLoc *l, const float *q, int hint)   /* 0x5d9e30 */
{
    if (hint < 0) {
        hint = l->node;
        if (hint < 0) hint = nearest_node(p, q, 1, -1.0f);
        else {
            float dx = q[0] - l->pos[0], dy = q[1] - l->pos[1];
            if (128.0f < sqrtf(dx * dx + dy * dy)) hint = nearest_node(p, q, 1, -1.0f);
        }
        if (hint < 0) {
            l->node = -1;
            l->pos[0] = q[0]; l->pos[1] = q[1];
            l->dir[0] = 1.0f; l->dir[1] = 0;
            l->progress = 0; l->frac = 0;
            l->spt[0] = q[0]; l->spt[1] = q[1];
            return;
        }
    }
    float d = node_refine(p, q, &hint);
    if (hint < 0) return;
    const RpNode *h = &p->node[hint];
    if (h->b10 != 0xff) {
        float b11 = (float)h->b11;
        if (d + d < b11) {
            if ((float)h->b10 <= d + d) {
                for (int i = 0; i < 3; i++) {
                    int nb = h->nb[i];
                    if (nb == -1 || nb < 0 || nb >= p->nnode) continue;
                    if (!p->sec[p->node[nb].sec].sp) continue;
                    int m = nb;
                    float d2 = node_refine(p, q, &m);
                    if (m < 0 || p->node[m].b10 == 0xff) continue;
                    float c = (fabsf(racepath_diff(p, h->prog, p->node[m].prog)) - 40.0f) * 0.25f;
                    if (c < 0) c = 0;
                    c += d2 + 1.0f;
                    if (c < d) { d = c; hint = m; h = &p->node[hint]; }
                }
            }
        } else {
            int m = nearest_node(p, q, 1, 128.0f);
            if (m >= 0 && p->node[m].b10 != 0xff) {
                float d2 = node_refine(p, q, &m);
                if (m >= 0) {
                    float c = (fabsf(racepath_diff(p, h->prog, p->node[m].prog)) - 40.0f) * 0.25f;
                    if (c < 0) c = 0;
                    if (d2 + 1.0f + c < d) hint = m;
                }
            }
        }
    }
    const RpSec *s = &p->sec[p->node[hint].sec];
    if (!s->sp) return;
    float t0 = l->t;
    if (hint != l->node || t0 == 0) {
        t0 = (float)(hint - s->first + 1);
        l->node = hint;
    }
    float P3[3] = { q[0], q[1], 0 }, a[4], dv[4];
    float t = spl_project(s->sp, P3, t0, 0.05f);
    if (g_ls_on > 0) g_ls_proj_it += g_ls_lastit;
    spl_eval(s->sp, t, 0, a);
    float sx = a[0], sy = a[1];
    spl_eval(s->sp, t, 1, dv);
    float dl = sqrtf(dv[0] * dv[0] + dv[1] * dv[1]);                    /* 0x43ce50 */
    float dx = 1.0f, dy = 0;
    if (dl != 0) { dx = dv[0] / dl; dy = dv[1] / dl; }
    spl_eval(s->sp, t, 0, a);
    float prog = (a[3] - s->arc0) * s->scale + s->pa;
    if (s->scale < 0) { dx = -dx; dy = -dy; }
    l->t = t;
    l->pos[0] = q[0]; l->pos[1] = q[1];
    l->dir[0] = dx; l->dir[1] = dy;
    l->progress = prog;
    l->frac = prog / p->lap;
    l->spt[0] = sx; l->spt[1] = sy;
}
void racepath_locate(const RacePath *p, RpLoc *l, const float pos[3])   /* 0x5da330 */
{
    if (!p || !p->sec) return;
    if (g_ls_on < 0) g_ls_on = getenv("OPENUG2_LOCSTAT") != NULL;
    if (l->thresh <= fabsf(pos[0] - l->pos[0]) + fabsf(pos[1] - l->pos[1])) {
        locate(p, l, pos, -1);
        if (g_ls_on > 0) {
            if (l->node < 0) g_ls_noloc++;
            if (++g_ls_loc >= 2000) {
                printf("locstat over %ld locates: nearest any %ld, nearest 128 m %ld, nodes scanned %.1f/locate, proj iters %.2f/locate, unlocated %ld (sections %d nodes %d)%c",
                       g_ls_loc, g_ls_nn_any, g_ls_nn_r, (double)g_ls_nn_nodes / g_ls_loc, (double)g_ls_proj_it / g_ls_loc, g_ls_noloc, p->nsec, p->nnode, 10);
                g_ls_loc = g_ls_nn_any = g_ls_nn_r = g_ls_nn_nodes = g_ls_proj_it = g_ls_noloc = 0;
            }
        }
    }
}
static float locate_at(const RacePath *p, const float *q)               /* 0x5ded90 */
{
    RpLoc l;
    rploc_init(&l);
    l.node = -1;
    locate(p, &l, q, -1);
    return l.progress;
}

/* 0x577710: the spline parameter at arc length `arc` (regula falsi over
   [-n, 2n] for a Catmull-Rom spline, to 1 mm, at most 40 steps) */
static float spl_arc_t(Spl *s, float arc)
{
    float lo = (float)-s->n, hi = (float)(2 * s->n), a[4], b[4];
    if (!(lo < hi)) return lo;
    spl_eval(s, lo, 0, a);
    spl_eval(s, hi, 0, b);
    float alo = a[3], ahi = b[3];
    if (!(ahi != alo) || !(alo < arc)) return lo;
    if (ahi <= arc) return hi;
    float t = lo;
    for (int it = 0; it < 40; it++) {                                   /* 0x28 */
        float f = (arc - alo) / (ahi - alo), c[4];
        t = f * hi + (1.0f - f) * lo;
        spl_eval(s, t, 0, c);
        if (fabsf(arc - c[3]) < 0.001f) break;                          /* 0x79dc38 */
        if (arc < c[3]) { ahi = c[3]; hi = t; }
        if (c[3] < arc) { alo = c[3]; lo = t; }
    }
    return t;
}
/* 0x5da240 (via 0x5d5af0): the route's node nearest `prog` -- in the
   section of route `route` whose range holds it best */
static int node_at(const RacePath *p, float prog, int route)
{
    if (route < 0 || route >= p->nroute) return -1;
    RpSec *best = NULL;
    float bd = 9999999.0f;
    for (int i = 0; i < p->route[route].count; i++) {
        RpSec *s = p->list[p->route[route].first + i];
        float a = prog - s->rmax, b = s->rmin - prog;
        if (a < b) a = b;
        if (a < bd) { bd = a; best = s; }
    }
    if (!best) return -1;
    int pick = -1;
    float pd = 0;
    for (int i = best->first; i < best->end; i++) {
        float d = fabsf(racepath_diff(p, p->node[i].prog, prog));
        if (pick < 0 || d < pd) { pick = i; pd = d; }
    }
    return pick;
}
/* 0x5da370: put a locator `prog` metres along the route, `lat` metres right
   of the section's spline (0x5d5c00) */
static void place_at(const RacePath *p, RpLoc *l, float prog, float lat)
{
    int nd = node_at(p, prog, 0);
    if (nd < 0) return;
    const RpSec *s = &p->sec[p->node[nd].sec];
    if (!s->sp || s->scale == 0) return;
    float t = spl_arc_t(s->sp, (prog - s->pa) / s->scale + s->arc0), a[4], d[4];
    spl_eval(s->sp, t, 0, a);
    spl_eval(s->sp, t, 1, d);
    float dl = sqrtf(d[0] * d[0] + d[1] * d[1]), dx = 1.0f, dy = 0;     /* 0x43ce50 */
    if (dl != 0) { dx = d[0] / dl; dy = d[1] / dl; }
    float q[3] = { dy * lat + a[0], -dx * lat + a[1], 0 };
    locate(p, l, q, nd);
}
int racepath_grid(const RacePath *p, int n, float (*pos)[3], float *heading)
{
    if (!p || p->nroute < 1 || n < 1) return 0;
    RpLoc l;
    rploc_init(&l);
    for (float s = 0; s < 100.0f; ) { s += 10.0f; place_at(p, &l, s, 0); }   /* 0x784560 to 0x7a55ac */
    for (int i = 0; i < n; i++) {
        place_at(p, &l, (float)(i / 2) * 17.0f + 100.0f, i % 2 == 0 ? 3.0f : -3.0f);   /* 0x7a55a4 / 0x7a55a8 */
        pos[i][0] = l.pos[0]; pos[i][1] = l.pos[1]; pos[i][2] = 0;
        unsigned short h = (unsigned short)(int)floorf(atan2f(l.dir[1], l.dir[0]) * (32768.0f / 3.14159265f) + 0.5f);   /* 0x43e6d0 */
        heading[i] = (float)h * (6.28318531f / 65536.0f);
    }
    return n;
}

/* ---- building the race route -------------------------------------------------- */
/* 0x5d5090: the section's spline through its nodes; the extra end points
   come from the neighbouring sections (or the section's own ends) */
static void sec_spline(RacePath *p, RpSec *s, const float *pre, const float *post)
{
    if (s->sp) return;
    s->sp = spl_new(s->cnt + 2);
    if (!s->sp) return;
    Spl *sp = s->sp;
    for (int i = s->first; i < s->end; i++) {
        float *P = sp->P[i - s->first + 1];
        P[0] = p->node[i].x; P[1] = p->node[i].y; P[2] = 0; P[3] = 0;
    }
    float a0, a1, b0, b1;
    if (!pre) {
        const RpNode *n0 = &p->node[s->first], *n1 = &p->node[s->first + 1];
        a0 = s->start[0] - (n1->x - n0->x); a1 = s->start[1] - (n1->y - n0->y);
    } else { a0 = pre[0]; a1 = pre[1]; }
    sp->P[0][0] = a0; sp->P[0][1] = a1; sp->P[0][2] = 0; sp->P[0][3] = 0;
    if (!post) {
        const RpNode *n0 = &p->node[s->end - 2], *n1 = &p->node[s->end - 1];
        b0 = s->endp[0] - (n0->x - n1->x); b1 = s->endp[1] - (n0->y - n1->y);
    } else { b0 = post[0]; b1 = post[1]; }
    float *E = sp->P[s->cnt + 1];
    E[0] = b0; E[1] = b1; E[2] = 0; E[3] = 0;
    sp->dirty = 1;
    spl_arclen(sp, 16);
    float a[4], b[4];
    spl_eval(sp, 1.0f, 0, a);
    spl_eval(sp, (float)s->cnt, 0, b);
    s->arc0 = a[3];
    s->splen = b[3] - a[3];
}
/* 0x5d52c0: the section's place along the route, and its nodes' */
static void sec_place(RacePath *p, RpSec *s, int rev, float start, float scale)
{
    float end = scale * s->splen + start;
    s->rev = rev; s->rmin = start; s->rmax = end;
    if (!rev) { s->pb = end; s->pa = start; s->scale = scale; }
    else { s->pa = end; s->pb = start; s->scale = -scale; }
    for (int i = s->first; i < s->end; i++) {
        float a[4];
        spl_eval(s->sp, (float)(i - s->first + 1), 0, a);
        p->node[i].prog = (a[3] - s->arc0) * s->scale + s->pa;
    }
}
/* the travel vector at a section's joining end (backward = towards the
   section the route comes from) */
static void joint_back(const RacePath *p, const RpSec *s, int rev, float *v)
{
    if (!rev) {
        const RpNode *a = &p->node[s->end - 2], *b = &p->node[s->end - 1];
        v[0] = a->x - b->x; v[1] = a->y - b->y;
    } else {
        const RpNode *a = &p->node[s->first], *b = &p->node[s->first + 1];
        v[0] = b->x - a->x; v[1] = b->y - a->y;
    }
}
static void joint_fwd(const RacePath *p, const RpSec *s, int rev, float *v)
{
    if (!rev) {
        const RpNode *a = &p->node[s->first], *b = &p->node[s->first + 1];
        v[0] = b->x - a->x; v[1] = b->y - a->y;
    } else {
        const RpNode *a = &p->node[s->end - 2], *b = &p->node[s->end - 1];
        v[0] = a->x - b->x; v[1] = a->y - b->y;
    }
}
/* 0x5e48b0: (re)build route r's splines and places; returns the last
   section's end progress */
static float build_route(RacePath *p, int r, int closed, float scale)
{
    float last = 0;
    int c0 = p->route[0].count, first = p->route[r].first, cnt = p->route[r].count;
    for (int i = 0; i < cnt; i++) {
        RpSec *s = p->list[first + i];
        int rev = p->dir[first + i];
        spl_free(s->sp); s->sp = NULL;
        float vb[2], vf[2], pre[2], post[2];
        int hb = 0, hf = 0, hpre = 0, hpost = 0;
        if (0 < i || closed) {
            int j = (c0 - 1 + i) % c0;                                  /* (route 0's count, as the game) */
            joint_back(p, p->list[first + j], p->dir[first + j], vb);
            hb = 1;
        }
        if (i < cnt - 1 || closed) {
            int j = (i + 1) % c0;
            joint_fwd(p, p->list[first + j], p->dir[first + j], vf);
            hf = 1;
        }
        if (hb) {
            if (!rev) { pre[0] = vb[0] + s->start[0]; pre[1] = vb[1] + s->start[1]; hpre = 1; }
            else { post[0] = vb[0] + s->endp[0]; post[1] = vb[1] + s->endp[1]; hpost = 1; }
        }
        if (hf) {
            if (!rev) { post[0] = vf[0] + s->endp[0]; post[1] = vf[1] + s->endp[1]; hpost = 1; }
            else { pre[0] = vf[0] + s->start[0]; pre[1] = vf[1] + s->start[1]; hpre = 1; }
        }
        float start = locate_at(p, rev ? s->endp : s->start);
        sec_spline(p, s, hpre ? pre : NULL, hpost ? post : NULL);
        if (!s->sp) return last;
        sec_place(p, s, rev, start, scale);
        last = s->rmax;
    }
    return last;
}

/* 0x5d53d0: depth-first search for the cheapest chain of sections from x
   (entered in direction dir) to the target (or, target NULL, back onto a
   built route further on) */
typedef struct {
    float cost, rejoin, cap;
    int maxdepth;
    RpSec *target;
    int ev;
    float start;
    int n;
    RpSec *list[16];
    unsigned char dir[16];
} PfState;
static int pathfind(RacePath *p, PfState *st, RpSec *x, int dir, int depth)
{
    if (st->n == 16 || !sec_allowed(x, st->ev)) return 0;
    for (int i = 0; i < st->n; i++) if (x == st->list[i] && x != st->target) return 0;
    if (sec_blocked(p, x)) return 0;
    if (!st->target && x->sp) {
        if (dir != x->rev) return 0;
        float pr = dir == 1 ? x->pb : x->pa;
        if (pr == 0) {
            if (!p->circuit) return 0;
            pr = p->lap;
        }
        if (st->start <= pr && 0.5f <= (pr - st->start) / st->cost) { st->rejoin = pr; return 1; }
        return 0;
    }
    st->list[st->n] = x; st->dir[st->n] = (unsigned char)dir; st->n++;
    st->cost += x->len;
    if (st->target && x == st->target) return 1;
    if (!(depth + 1 <= st->maxdepth && st->cost <= st->cap)) return 0;
    PfState best; int found = 0;
    int g = dir == 0;
    for (int k = 0; k < 4; k++) {
        if (k >= x->nlink[g] || !x->link[g][k].to) continue;
        PfState c = *st;
        if (pathfind(p, &c, x->link[g][k].to, x->link[g][k].dir, depth + 1) && (!found || c.cost < best.cost)) {
            best = c;
            best.cap = c.cost;
            st->cap = c.cost;
            found = 1;
        }
    }
    if (found) *st = best;
    return found;
}

static int build(RacePath *p, const unsigned char *evr)                 /* 0x5e5bb0 */
{
    p->lap = 1.0f; p->half = 1.0f;
    p->circuit = evr[3] != 0;
    p->ev = rdu16(evr);
    p->nroute = 0; p->nlist = 0;
    p->route[0].first = 0;
    int n = (signed char)evr[2], first_seg = 1, rev = 0;
    for (int i = 0; i < n - 1; i++) {
        const unsigned char *A, *B;
        if (!p->reversed) { A = evr + 8 + 8 * i; B = A + 8; }
        else { A = evr + 8 * (n - i); B = A - 8; }
        float a[2] = { rdf(A), rdf(A + 4) }, b[2] = { rdf(B), rdf(B + 4) };
        int na = nearest_node(p, a, 0, -1.0f);
        if (na < 0) return 0;
        RpSec *S = &p->sec[p->node[na].sec];
        if (first_seg) {
            first_seg = 0;
            float dx = b[0] - a[0], dy = b[1] - a[1], l = sqrtf(dx * dx + dy * dy);
            float u0 = 1, u1 = 0, w0 = 1, w1 = 0, ex, ey;
            if (l != 0) { u0 = dx / l; u1 = dy / l; }
            if (na - 0 < S->end - 1) { ex = p->node[na + 1].x - p->node[na].x; ey = p->node[na + 1].y - p->node[na].y; }
            else { ex = p->node[na].x - p->node[na - 1].x; ey = p->node[na].y - p->node[na - 1].y; }
            float el = sqrtf(ex * ex + ey * ey);
            if (el != 0) { w0 = ex / el; w1 = ey / el; }
            rev = u0 * w0 + u1 * w1 <= 0;
        } else rev = p->dir[p->nlist - 1];
        int nb = nearest_node(p, b, 0, -1.0f);
        if (nb < 0) return 0;
        PfState st; memset(&st, 0, sizeof st);
        st.ev = p->ev; st.target = &p->sec[p->node[nb].sec]; st.cap = 9999999.0f; st.maxdepth = 10;
        if (!pathfind(p, &st, S, rev, 0)) return 0;
        for (int j = 0; j < st.n; j++) {
            RpSec *x = st.list[j];
            if (p->nlist > 0 && x == p->list[p->nlist - 1]) continue;
            if (p->circuit && p->nlist > 0 && x == p->list[p->route[0].first]) continue;
            for (int q = 0; q < p->nlist; q++) if (x == p->list[q]) return 0;
            if (p->nlist >= 128) return 0;
            p->list[p->nlist] = x; p->dir[p->nlist] = st.dir[j]; p->nlist++;
        }
    }
    p->route[0].count = p->nlist - p->route[0].first;
    p->nroute = 1;
    float lap = build_route(p, 0, p->circuit, 1.0f);
    p->route[0].len = lap; p->route[0].p0 = 0; p->route[0].p1 = lap; p->route[0].scale = 1.0f;
    p->lap = lap; p->half = lap * 0.5f;
    if (evr[4]) {                                                       /* shortcuts */
        for (int r = 0; r < p->nroute; r++)
            for (int j = 0; j < p->route[r].count; j++) {
                RpSec *s = p->list[p->route[r].first + j];
                int g = p->dir[p->route[r].first + j] == 0;
                for (int k = 0; k < 4; k++) {
                    if (15 < p->nroute) break;
                    if (k >= s->nlink[g]) continue;
                    RpSec *L = s->link[g][k].to;
                    int ld = s->link[g][k].dir;
                    if (!L || L->sp) continue;
                    PfState st; memset(&st, 0, sizeof st);
                    st.ev = p->ev; st.target = NULL; st.cap = 9999999.0f; st.maxdepth = 15;
                    float p0 = locate_at(p, ld ? L->endp : L->start);
                    st.start = p0;
                    if (!pathfind(p, &st, L, ld, 0)) continue;
                    int nr = p->nroute;
                    p->route[nr].first = p->nlist;
                    for (int q = 0; q < st.n; q++) {
                        if (nr == 16 || p->nlist == 128) break;
                        p->list[p->nlist] = st.list[q]; p->dir[p->nlist] = st.dir[q]; p->nlist++;
                    }
                    p->route[nr].count = p->nlist - p->route[nr].first;
                    p->nroute = nr + 1;
                    float end = build_route(p, nr, 0, 1.0f);
                    p->route[nr].len = end - p0;
                    p->route[nr].p0 = p0;
                    p->route[nr].p1 = st.rejoin;
                    p->route[nr].scale = (st.rejoin - p0) / (end - p0);
                    build_route(p, nr, 0, p->route[nr].scale);
                }
            }
    }
    return 1;
}

static RacePath *load(const char *path, int event, int reversed, int zones_only);
RacePath *racepath_load(const char *path, int event, int reversed) { return load(path, event, reversed, 0); }
RacePath *racepath_load_zones(const char *path) { return load(path, 0, 0, 1); }
static RacePath *load(const char *path, int event, int reversed, int zones_only)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    RacePath *p = (RacePath *)calloc(1, sizeof *p);
    unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (!p || !d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); free(p); return NULL; }
    fclose(f);
    p->file = d;
    p->reversed = reversed;
    const unsigned char *secs = NULL, *nodes = NULL, *evs = NULL;
    int nev = 0;
    long stack[8]; int depth = 0;
    long o = 0, end = len;
    while (o + 8 <= end) {                                              /* 0x5d9a90 */
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) { stack[depth++] = end; end = next; o = body; continue; }
        if (id == 0x34148) { nodes = d + body; p->nnode = (int)(sz / 0x18); }
        else if (id == 0x34149) { secs = d + body; p->nsec = (int)(sz / 0xdc); }
        else if (id == 0x3414c) { evs = d + body; nev = (int)(sz / 0x110); }
        else if (id == 0x3414d) { p->blk = d + body; p->nblk = (int)(sz / 0x24); }
        else if (id == 0x3414a) { p->zones = d + body; p->nzones = (long)sz; }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack[--depth]; }
    }
    zone_index(p);
    if (zones_only) {                       /* free roam: no route (0x5d9a60 ClearConnectionTree) */
        if (!p->zones) { racepath_free(p); return NULL; }
        return p;
    }
    const unsigned char *evr = NULL;
    for (int i = 0; i < nev; i++) if (rdu16(evs + i * 0x110) == (unsigned)event) { evr = evs + i * 0x110; break; }
    if (!secs || !nodes || !evr || p->nsec <= 0 || p->nnode <= 0) { racepath_free(p); return NULL; }
    p->sec = (RpSec *)calloc((size_t)p->nsec, sizeof *p->sec);
    p->node = (RpNode *)calloc((size_t)p->nnode, sizeof *p->node);
    if (!p->sec || !p->node) { racepath_free(p); return NULL; }
    for (int i = 0; i < p->nnode; i++) {
        const unsigned char *r = nodes + i * 0x18;
        RpNode *n = &p->node[i];
        n->x = rdf(r); n->y = rdf(r + 4);
        n->sec = rdu16(r + 8);
        n->b10 = r[0xa]; n->b11 = r[0xb];
        n->nb[0] = rds16(r + 0xc); n->nb[1] = rds16(r + 0xe); n->nb[2] = rds16(r + 0x10);
        n->prog = rdf(r + 0x14);
        if (n->sec >= p->nsec) n->sec = 0;
    }
    for (int i = 0; i < p->nsec; i++) {
        const unsigned char *r = secs + i * 0xdc;
        RpSec *s = &p->sec[i];
        s->raw = r;
        s->first = rds16(r + 0x1c); s->end = rds16(r + 0x1e); s->cnt = rds16(r + 0x20);
        if (s->first < 0) s->first = 0;
        if (s->end > p->nnode) s->end = p->nnode;
        s->len = rdf(r + 0x64);
        s->start[0] = rdf(r + 0x88); s->start[1] = rdf(r + 0x8c);
        s->endp[0] = rdf(r + 0x90); s->endp[1] = rdf(r + 0x94);
        s->bbmin[0] = rdf(r + 0x98); s->bbmin[1] = rdf(r + 0x9c);
        s->bbmax[0] = rdf(r + 0xa0); s->bbmax[1] = rdf(r + 0xa4);
        for (int g = 0; g < 2; g++) {                                   /* 0x5d5630 */
            s->nlink[g] = r[0x22 + g] > 4 ? 4 : r[0x22 + g];
            for (int k = 0; k < s->nlink[g]; k++) {
                const unsigned char *lk = r + 0x24 + (g * 4 + k) * 8;
                int to = rdu16(lk);
                s->link[g][k].to = to < p->nsec ? &p->sec[to] : NULL;
                s->link[g][k].dir = lk[2];
            }
        }
    }
    if (!build(p, evr) || !(p->lap > 1.0f)) { racepath_free(p); return NULL; }
    return p;
}
void racepath_free(RacePath *p)
{
    if (!p) return;
    if (p->sec) for (int i = 0; i < p->nsec; i++) spl_free(p->sec[i].sp);
    free(p->sec); free(p->node); free(p->zix); free(p->file); free(p);
}
float racepath_lap(const RacePath *p) { return p ? p->lap : 0; }
int racepath_circuit(const RacePath *p) { return p ? p->circuit : 0; }
int racepath_routes(const RacePath *p) { return p ? p->nroute : 0; }
int racepath_sections(const RacePath *p, int r) { return p && r >= 0 && r < p->nroute ? p->route[r].count : 0; }

/* 0x43d890: the point inside the polygon (crossing test, (x, y) pairs) */
static int poly_in(const float q[2], const unsigned char *pts, int n)
{
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = rdf(pts + i * 8), yi = rdf(pts + i * 8 + 4), xj = rdf(pts + j * 8), yj = rdf(pts + j * 8 + 4);
        if (((yi <= q[1] && q[1] < yj) || (yj <= q[1] && q[1] < yi)) &&
            q[0] < (xj - xi) * (q[1] - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
static int zix_cmp(const void *a, const void *b)
{
    const struct RpZoneIx *x = (const struct RpZoneIx *)a, *y = (const struct RpZoneIx *)b;
    if (x->type != y->type) return x->type < y->type ? -1 : 1;
    return x->z < y->z ? -1 : x->z > y->z;                 /* file order within a type */
}
static void zone_index(RacePath *p)
{
    if (!p->zones) return;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_NOZIX")) return;   /* simulator A/B: the plain walks */
#endif
    int n = 0;
    for (const unsigned char *z = racepath_zone_next(p, NULL); z; z = racepath_zone_next(p, z)) n++;
    p->zix = n ? (struct RpZoneIx *)malloc(sizeof *p->zix * (size_t)n) : NULL;
    if (!p->zix) return;
    for (const unsigned char *z = racepath_zone_next(p, NULL); z && p->nzix < n; z = racepath_zone_next(p, z)) {
        struct RpZoneIx *e = &p->zix[p->nzix++];
        e->z = z; e->type = (int)rd32(z);
        e->bb[0] = rdf(z + 0x20); e->bb[1] = rdf(z + 0x24); e->bb[2] = rdf(z + 0x28); e->bb[3] = rdf(z + 0x2c);
    }
    qsort(p->zix, (size_t)p->nzix, sizeof *p->zix, zix_cmp);
}
/* the first index entry of `type` (n = past the end when none) */
static int zix_first(const RacePath *p, int type)
{
    int lo = 0, hi = p->nzix;
    while (lo < hi) { int m = (lo + hi) / 2; if (p->zix[m].type < type) lo = m + 1; else hi = m; }
    return lo;
}
const unsigned char *racepath_zone_at(const RacePath *p, const float pos[2], int type, const unsigned char *after)
{
    if (!p || !p->zones) return NULL;
    if (!p->zix) {                                         /* no index (out of memory): the plain walk */
        for (const unsigned char *z = after ? racepath_zone_next(p, after) : racepath_zone_next(p, NULL); z; z = racepath_zone_next(p, z))
            if ((int)rd32(z) == type && racepath_zone_holds(z, pos)) return z;
        return NULL;
    }
    int i = zix_first(p, type);
    if (after) while (i < p->nzix && p->zix[i].type == type && p->zix[i].z <= after) i++;
    for (; i < p->nzix && p->zix[i].type == type; i++) {
        const struct RpZoneIx *e = &p->zix[i];
        if (e->bb[0] <= pos[0] && pos[0] <= e->bb[2] && e->bb[1] <= pos[1] && pos[1] <= e->bb[3] &&
            poly_in(pos, e->z + 0x44, (int16_t)(e->z[0x40] | e->z[0x41] << 8))) return e->z;
    }
    return NULL;
}
const unsigned char *racepath_zone(const RacePath *p, const float pos[2], int type)
{
    if (!p || !p->zones) return NULL;
    if (p->zix) {
        const unsigned char *r = racepath_zone_at(p, pos, type, NULL);
#ifdef OPENUG2_SIM_PRE_H
        static int chk = -1;
        if (chk < 0) chk = getenv("OPENUG2_ZONECHECK") != NULL;
        if (chk) {   /* simulator test aid: the index against the plain walk */
            static long calls, diffs;
            const unsigned char *w = NULL;
            for (long o = 0; o + 0x44 <= p->nzones && !w; ) {
                const unsigned char *z = p->zones + o;
                int sz = (int16_t)(z[0x42] | z[0x43] << 8);
                if (sz <= 0) break;
                if ((int)rd32(z) == type && racepath_zone_holds(z, pos)) w = z;
                o += sz;
            }
            if (w != r) diffs++;
            if (++calls % 20000 == 0) printf("zonecheck: %ld lookups, %ld differences%c", calls, diffs, 10);
        }
#endif
        return r;
    }
    for (long o = 0; o + 0x44 <= p->nzones; ) {
        const unsigned char *z = p->zones + o;
        int sz = (int16_t)(z[0x42] | z[0x43] << 8);
        if (sz <= 0) break;
        if ((int)rd32(z) == type && rdf(z + 0x20) <= pos[0] && pos[0] <= rdf(z + 0x28) &&
            rdf(z + 0x24) <= pos[1] && pos[1] <= rdf(z + 0x2c) &&                /* 0x43d410 */
            poly_in(pos, z + 0x44, (int16_t)(z[0x40] | z[0x41] << 8))) return z;
        o += sz;
    }
    return NULL;
}
const unsigned char *racepath_zone_next(const RacePath *p, const unsigned char *prev)
{
    if (!p || !p->zones) return NULL;
    long o = 0;
    if (prev) {
        int sz = (int16_t)(prev[0x42] | prev[0x43] << 8);
        if (sz <= 0) return NULL;
        o = (long)(prev - p->zones) + sz;
    }
    return o >= 0 && o + 0x44 <= p->nzones && (int16_t)(p->zones[o + 0x42] | p->zones[o + 0x43] << 8) > 0
           ? p->zones + o : NULL;
}
int racepath_zone_holds(const unsigned char *z, const float pos[2])
{
    return z && rdf(z + 0x20) <= pos[0] && pos[0] <= rdf(z + 0x28) &&
           rdf(z + 0x24) <= pos[1] && pos[1] <= rdf(z + 0x2c) &&
           poly_in(pos, z + 0x44, (int16_t)(z[0x40] | z[0x41] << 8));
}

/* ---- checkpoints, laps and standings ------------------------------------------ */
static unsigned rp_hash(const char *s)                                  /* 0x43db50 */
{
    unsigned h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}
/* 0x5d8660(type, k) / the marker with index sel (sel 0: the first) */
static int marker_pos(const unsigned char *d, long len, int event, const char *type, int sel, float pos[3])
{
    unsigned want = rp_hash(type);
    int found = 0;
    long stack[8]; int depth = 0;
    long o = 0, end = len;
    while (o + 8 <= end) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) { stack[depth++] = end; end = next; o = body; continue; }
        if (id == 0x34146) {
            for (long s = (body + 15) & ~15L; s + 48 <= next; s += 48) {
                const unsigned char *m = d + s;
                if (rd32(m + 8) != want || (int)rd32(m + 0x24) != event) continue;
                if (!found || (sel != 0 && (int)rd32(m + 0xc) == sel)) {
                    pos[0] = rdf(m + 0x10); pos[1] = rdf(m + 0x14); pos[2] = rdf(m + 0x18);
                    found = 1;
                }
            }
        }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack[--depth]; }
    }
    return found;
}
/* 0x5ee4f0: a checkpoint at the marker (its route percent) or at def */
static int add_cp(RaceCps *c, const RacePath *p, const unsigned char *d, long len, int event,
                  const char *type, float def, int sel, float *out)
{
    float pos[3];
    if (out) out[0] = out[1] = out[2] = 0;
    if (100.0f < def) def -= 100.0f;
    if (c->n >= 8) return -1;
    if (d && marker_pos(d, len, event, type, sel, pos)) {
        if (out) { out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2]; }
        RpLoc l; rploc_init(&l);
        locate(p, &l, pos, -1);
        c->cp[c->n] = l.frac * 100.0f;
        return c->n++;
    }
    if (0 <= def) { c->cp[c->n] = def; return c->n++; }
    return -1;
}
void racecps_init(RaceCps *c, const RacePath *p, const char *path, int event, int reversed, int sprint, int laps)
{
    memset(c, 0, sizeof *c);
    c->sprint = sprint; c->laps = laps; c->circuit = racepath_circuit(p);
    unsigned char *d = NULL; long len = 0;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (f) {
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
        if (d && fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); d = NULL; }
        fclose(f);
    }
    int s1, s2;                                                         /* 0x5ee620 */
    if (!reversed) {
        c->start = add_cp(c, p, d, len, event, "ForwardStartLine", 5.0f, 0, c->start_pos);
        if (c->start < 0) c->start = 0;
        s1 = add_cp(c, p, d, len, event, "SplitTimeBoundary1", c->cp[c->start] + 33.0f, 0, NULL);
        s2 = add_cp(c, p, d, len, event, "SplitTimeBoundary2", c->cp[c->start] + 66.0f, 0, NULL);
        add_cp(c, p, d, len, event, "SplitTimeBoundary3", -1.0f, 0, NULL);
        c->finish = sprint ? add_cp(c, p, d, len, event, "ForwardFinishLine", c->cp[c->start] + 95.0f, 0, NULL) : c->start;
    } else {
        c->start = add_cp(c, p, d, len, event, "BackwardStartLine", 5.0f, 0, c->start_pos);
        if (c->start < 0) c->start = 0;
        add_cp(c, p, d, len, event, "SplitTimeBoundary3", -1.0f, 0, NULL);
        s1 = add_cp(c, p, d, len, event, "SplitTimeBoundary2", c->cp[c->start] + 33.0f, 0, NULL);
        s2 = add_cp(c, p, d, len, event, "SplitTimeBoundary1", c->cp[c->start] + 66.0f, 0, NULL);
        c->finish = sprint ? add_cp(c, p, d, len, event, "BackwardFinishLine", c->cp[c->start] + 95.0f, 0, NULL) : c->start;
    }
    if (c->finish < 0) c->finish = c->start;
    if (sprint) {
        float a = c->cp[c->start] + 33.0f, b = c->cp[c->start] + 66.0f;
        if (100.0f < a) a -= 100.0f;
        if (100.0f < b) b -= 100.0f;
        if (s1 >= 0 && a == c->cp[s1]) c->cp[s1] = (c->cp[c->finish] - c->cp[c->start]) * 0.33f + c->cp[c->start];
        if (s2 >= 0 && b == c->cp[s2]) c->cp[s2] = (c->cp[c->finish] - c->cp[c->start]) * 0.66f + c->cp[c->start];
    }
    free(d);
}
static float wrap100(const RaceCps *c, float a, float b)               /* 0x5d5d10 */
{
    float d = a - b;
    if (c->circuit) {
        if (d < -50.0f) return d + 100.0f;
        if (50.0f < d) d -= 100.0f;
    }
    return d;
}
static float lap_frac(const RaceCps *c, float pct);
float racecps_lap_frac(const RaceCps *c, float pct) { return lap_frac(c, pct); }
int racepath_node_sec(const RacePath *p, int node)
{
    return p && node >= 0 && node < p->nnode ? (int)p->node[node].sec : -1;
}
static float lap_frac(const RaceCps *c, float pct)                      /* 0x5ee9b0 */
{
    float f = c->cp[c->finish], s = c->cp[c->start];
    if (f != s) {
        float v = (pct - s) / (f - s);
        if (1.0f < v) return v - 1.0f;
        return v;
    }
    float v = wrap100(c, pct, f);
    if (v < 0) v += 100.0f;
    return v * 0.01f;
}
void race_entry_reset(RaceEntry *e)
{
    memset(e, 0, sizeof *e);
    e->lap = -1; e->cp = -1; e->cpmax = -1; e->maxlap = 0;
    for (int i = 0; i < 5; i++) e->best[i] = e->pbest[i] = 0x7fffffff;
}
void race_entry_go(RaceEntry *e, const RaceCps *c, float frac, int now)
{
    e->cpt[0] = now;
    e->cpmax = 0;
    if (c->sprint) { e->lap = 0; e->cp = 0; }
    e->pct = frac * 100.0f;
}
int race_entry_update(RaceEntry *e, const RaceCps *c, float frac, int now)
{
    float pct = frac * 100.0f;
    int fin = 0;
    e->newlap = e->newcp = 0;
    for (int i = 0; i < c->n; i++) {
        float dn = wrap100(c, pct, c->cp[i]), dp = wrap100(c, e->pct, c->cp[i]);   /* 0x5ee150 */
        if (25.0f < fabsf(dn)) continue;
        int fwd;
        if (dp < 0 && 0 <= dn) fwd = 1;
        else if (0 <= dp && dn < 0) fwd = 0;
        else continue;
        if (!fwd) {
            if (i == c->finish) {
                e->lap--;
                if (e->lap < e->maxlap - 1) e->lap = e->maxlap - 1;
            }
        } else {
            if (i == c->finish) {
                e->lap++;
                if (e->maxlap < e->lap) {
                    e->maxlap = e->lap;
                    e->newlap = 1;
                    if (e->maxlap == c->laps && !e->place) fin = 1;
                }
            }
            int k = i + e->lap * 5;
            e->cp = k;
            if (e->cpmax < k) {
                e->cpmax = k;
                e->newcp = 1;
                if (i < 5) {
                    if (e->cpt[0] != 0 && e->cpt[0] != 0x7fffffff) {
                        e->psplit[i] = e->split[i];
                        e->split[i] = now - e->cpt[0];
                        e->pbest[i] = e->best[i];
                        if (e->split[i] < e->best[i]) e->best[i] = e->split[i];
                        if (i == c->finish && 0 <= e->lap && e->lap < 21) {
                            e->lapt[e->lap] = e->split[i];
                            e->lapc[e->lap] = e->lapt[e->lap];
                            if (0 < e->lap) e->lapc[e->lap] += e->lapc[e->lap - 1];
                        }
                    }
                    e->cpt[i] = now;
                }
            }
        }
    }
    e->dist = (float)e->lap + lap_frac(c, pct);
    e->pct = pct;
    return fin;
}
void race_standings(RaceEntry **e, int n)                               /* 0x5eec80 */
{
    int swapped;
    do {
        swapped = 0;
        for (int i = 0; i + 1 < n; i++) {
            RaceEntry *a = e[i], *b = e[i + 1];
            int behind;                                                 /* 0x5eec00 */
            if (a->place && b->place) behind = b->place < a->place;
            else behind = a->dist < b->dist;
            if (behind) { e[i] = b; e[i + 1] = a; swapped = 1; }
        }
    } while (swapped);
    for (int i = 0; i < n; i++) e[i]->pos = i + 1;
}
