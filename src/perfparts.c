/* perfparts.c — the retail NFSU2 performance parts (see perfparts.h).
 * Addresses in comments are SPEED2.EXE v1.2. */
#include "perfparts.h"
#include "n2sim.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- GLOBALB data ----------------------------------------------------------- */
/* a package (0x17c bytes, 0x500100(cat, level)): +0 category, +4 level,
   +8 part count, +0xc parts of 0x5c bytes: +0 index, +0x30 component
   values (-1 none), +0x40 five parts it replaces, +0x54 two prerequisites */
#define PKG_SIZE  0x17c
#define PART_SIZE 0x5c
static const unsigned char *g_pkg; static int g_npkg;
static const unsigned char *g_part[PERF_NPARTS];        /* 0x837b90[index] */
static const unsigned char *g_combo[64]; static int g_ncombo[64], g_ntype;   /* 0x86b0c8 */
static float g_lim[30];                                  /* 0x34a32 (0x86b3d0) */
static int g_ready;
static unsigned char *g_copy; static long g_copylen;

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t  rds32(const unsigned char *p) { return (int32_t)rd32(p); }
static float    rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static int pkg_cat(const unsigned char *k) { return rds32(k); }
static int pkg_lvl(const unsigned char *k) { return rds32(k + 4); }
static int pkg_n(const unsigned char *k) { int n = rds32(k + 8); return n < 0 ? 0 : n > 4 ? 4 : n; }
static const unsigned char *pkg_part(const unsigned char *k, int j) { return k + 0xc + j * PART_SIZE; }
static int part_idx(const unsigned char *p) { return rds32(p); }
static float part_val(const unsigned char *p, int k) { return rdf(p + 0x30 + 4 * k); }

static const unsigned char *pkg_find(int cat, int lvl)                  /* 0x500100 */
{
    for (int i = 0; i < g_npkg; i++) {
        const unsigned char *k = g_pkg + i * PKG_SIZE;
        if (pkg_cat(k) == cat && pkg_lvl(k) == lvl) return k;
    }
    return NULL;
}

int perf_init(const unsigned char *g, long len)
{
    free(g_copy); g_copy = NULL;
    g_npkg = 0; g_ntype = 0; g_ready = 0;
    memset(g_part, 0, sizeof g_part);
    /* keep the chunks we point into */
    g_copy = (unsigned char *)malloc((size_t)len);
    if (!g_copy) return 0;
    memcpy(g_copy, g, (size_t)len); g_copylen = len;
    g = g_copy;
    int got = 0;
    /* walk the chunk tree (containers have the top bit set) */
    long stack_end[8]; int depth = 0;
    long o = 0, end = len;
    stack_end[0] = end;
    while (o + 8 <= end) {
        uint32_t id = rd32(g + o), sz = rd32(g + o + 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) {
            stack_end[++depth] = end; end = next; o = body;
            continue;
        }
        if (id == 0x34a15 && !g_npkg && sz % PKG_SIZE == 0) {          /* loaded once (0x8641f4) */
            g_pkg = g + body; g_npkg = (int)(sz / PKG_SIZE); got |= 1;
        } else if (id == 0x34a31 && g_ntype < 64 && sz >= 4) {          /* loader 0x599440 */
            g_ncombo[g_ntype] = (int)rd32(g + body);
            g_combo[g_ntype] = g + body + 4;
            if ((long)g_ncombo[g_ntype] * 16 + 4 > (long)sz) g_ncombo[g_ntype] = (int)((sz - 4) / 16);
            g_ntype++; got |= 2;
        } else if (id == 0x34a32 && sz >= sizeof g_lim) {
            for (int i = 0; i < 30; i++) g_lim[i] = rdf(g + body + 4 * i);
            got |= 4;
        }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack_end[depth--]; }
    }
    for (int i = 0; i < g_npkg; i++) {
        const unsigned char *k = g_pkg + i * PKG_SIZE;
        for (int j = 0; j < pkg_n(k); j++) {
            int idx = part_idx(pkg_part(k, j));
            if (idx >= 0 && idx < PERF_NPARTS) g_part[idx] = pkg_part(k, j);
        }
    }
    g_ready = got == 7;
    return g_ready;
}
int perf_ready(void) { return g_ready; }
float perf_rating_min(void) { return g_lim[10]; }
float perf_rating_max(void) { return g_lim[11]; }

/* ---- fitting ---------------------------------------------------------------- */
void perf_clear(PerfParts *pp) { memset(pp, 0, sizeof *pp); }

/* 0x5a6320: fit (on) or remove (off) one part */
static int part_set(PerfParts *pp, const unsigned char *p, int on, int force)
{
    int idx = part_idx(p);
    if (idx < 0 || idx >= PERF_NPARTS) return 0;
    if (pp->fit[idx]) {
        if (!on) { pp->fit[idx] = 0; return 1; }
        return 0;
    }
    if (!on) return 1;
    int n = 1;
    for (int i = 0; i < 2; i++) {                                       /* prerequisites +0x54 */
        int q = rds32(p + 0x54 + 4 * i);
        if (q == -1 || (q >= 0 && q < PERF_NPARTS && pp->fit[q])) continue;
        if (!force) return 0;
        if (q >= 0 && q < PERF_NPARTS && g_part[q]) n += part_set(pp, g_part[q], 1, force);   /* 0x599680 */
    }
    for (int i = 0; i < 5; i++) {                                       /* replaces +0x40 */
        int q = rds32(p + 0x40 + 4 * i);
        if (q != -1 && q >= 0 && q < PERF_NPARTS && g_part[q]) part_set(pp, g_part[q], 0, 0);
    }
    pp->fit[idx] = 1;
    return n;
}
void perf_fit_level(PerfParts *pp, int cat, int level)                  /* 0x5a64a0 */
{
    for (int i = 0; i < g_npkg; i++) {                                  /* 0x5a6450 -> 0x5a6400(pkg, 0) */
        const unsigned char *k = g_pkg + i * PKG_SIZE;
        if (pkg_cat(k) != cat) continue;
        for (int j = 0; j < pkg_n(k); j++) part_set(pp, pkg_part(k, j), 0, 0);
    }
    for (int lvl = 1; lvl <= level; lvl++) {
        const unsigned char *k = pkg_find(cat, lvl);
        if (!k) continue;
        for (int j = 0; j < pkg_n(k); j++) part_set(pp, pkg_part(k, j), 1, 0);
    }
}

int perf_pkg_parts(int cat, int level, int idx[4], unsigned int name[4], int price[4])
{
    const unsigned char *k = pkg_find(cat, level);
    if (!k) return 0;
    int n = pkg_n(k);
    for (int j = 0; j < n; j++) {
        const unsigned char *p = pkg_part(k, j);
        if (idx) idx[j] = part_idx(p);
        if (name) name[j] = rd32(p + 4);
        if (price) price[j] = rds32(p + 8);
    }
    return n;
}
void perf_fit_pkg(PerfParts *pp, int cat, int level)                     /* 0x521340 */
{
    const unsigned char *k = pkg_find(cat, level);
    for (int j = 0; k && j < pkg_n(k); j++) part_set(pp, pkg_part(k, j), 1, 0);
}
void perf_fit_part(PerfParts *pp, int idx)                               /* 0x5213d0 */
{
    if (idx >= 0 && idx < PERF_NPARTS && g_part[idx]) part_set(pp, g_part[idx], 1, 0);
}
void perf_power(const unsigned char *built, float raw[2], float norm[2])  /* 0x5b5810 / 0x5b56e0 */
{
    const unsigned char *P = built + 0x110;
    float x0 = rdf(P + 0x1f0), x1 = rdf(P + 0x1f8), d[9];
    for (int i = 0; i < 9; i++) d[i] = rdf(P + 0x200 + 4 * i);
    float inv = 8.0f / (x1 - x0), tq = 0, pw = 0;
    for (float r = x0; r < x1; r += 50.0f) {
        float t = (r - x0) * inv, v;                                    /* 0x5764d0 */
        int i = (int)t;
        if (i < 0 || t < 0) v = d[0];
        else if (i >= 8) v = d[8];
        else v = (1.0f - (t - (float)i)) * d[i] + (t - (float)i) * d[i + 1];
        if (tq < v) tq = v;
        float p = v * 1000.0f * r * 0.000104712f;
        if (pw < p) pw = p;
    }
    raw[0] = tq; raw[1] = pw;
    for (int k = 0; k < 2; k++) {
        float lo = g_lim[4 + 2 * k], hi = g_lim[5 + 2 * k], v = raw[k];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        norm[k] = hi != lo ? (v - lo) / (hi - lo) : 0.0f;
    }
}
void perf_unfit_cat(PerfParts *pp, int cat)                              /* 0x521a20 */
{
    for (int i = 0; i < g_npkg; i++) {
        const unsigned char *k = g_pkg + i * PKG_SIZE;
        if (pkg_cat(k) != cat) continue;
        for (int j = 0; j < pkg_n(k); j++) part_set(pp, pkg_part(k, j), 0, 0);
    }
}

/* 0x5000a0: the package a part belongs to */
static const unsigned char *pkg_of(int idx)
{
    for (int i = 0; i < g_npkg; i++) {
        const unsigned char *k = g_pkg + i * PKG_SIZE;
        for (int j = 0; j < pkg_n(k); j++) if (part_idx(pkg_part(k, j)) == idx) return k;
    }
    return NULL;
}
static int fitted(const PerfParts *pp, int idx) { return idx >= 0 && idx < PERF_NPARTS && pp->fit[idx]; }   /* 0x599610 */
int perf_part_fitted(const PerfParts *pp, int idx) { return fitted(pp, idx); }
/* 0x521c10: of the parts this one replaces (+0x40, the ones not -1), the
   first fitted one decides: worse when its package's level is higher */
static int part_worse(const PerfParts *pp, const unsigned char *p)
{
    const unsigned char *own = pkg_of(part_idx(p));
    int n = 0;
    for (int i = 0; i < 5; i++) if (rds32(p + 0x40 + 4 * i) != -1) n++;
    for (int i = 0; i < n; i++) {
        int q = rds32(p + 0x40 + 4 * i);
        if (!fitted(pp, q)) continue;
        const unsigned char *k = pkg_of(q);
        return own && k && pkg_lvl(own) < pkg_lvl(k);
    }
    return 0;
}
int perf_part_worse(const PerfParts *pp, int idx)
{
    return idx >= 0 && idx < PERF_NPARTS && g_part[idx] && part_worse(pp, g_part[idx]);
}
int perf_pkg_fitted(const PerfParts *pp, int cat, int level)             /* 0x521b90 */
{
    const unsigned char *k = pkg_find(cat, level);
    if (!k) return 0;
    for (int j = 0; j < pkg_n(k); j++) if (!fitted(pp, part_idx(pkg_part(k, j)))) return 0;
    return 1;
}
int perf_pkg_worse(const PerfParts *pp, int cat, int level)              /* 0x5303c0 */
{
    const unsigned char *k = pkg_find(cat, level);
    int n = 0;
    for (int j = 0; k && j < pkg_n(k); j++) n += part_worse(pp, pkg_part(k, j));
    return n;
}

/* 0x5a6580: a category's component fractions out[0..n) (the fitted parts'
   values summed over the three packages, clamped 0..1 when their average
   leaves 0..1), level[cat] = average x 3; returns the average */
static float cat_frac(PerfParts *pp, int cat, int n, float *out)
{
    float loc[4];
    if (!n || !out) { n = (cat == 1 || cat == 5) ? 3 : cat == 9 ? 2 : 1; out = loc; }
    for (int k = 0; k < n; k++) out[k] = 0;
    for (int lvl = 1; lvl < 4; lvl++) {
        const unsigned char *pk = pkg_find(cat, lvl);
        if (!pk) continue;
        for (int k = 0; k < n; k++)                                      /* 0x5a61a0 */
            for (int j = 0; j < pkg_n(pk); j++) {
                const unsigned char *p = pkg_part(pk, j);
                int idx = part_idx(p);
                if (idx >= 0 && idx < PERF_NPARTS && pp->fit[idx] && part_val(p, k) != -1.0f)
                    out[k] += part_val(p, k);
            }
    }
    float a = 0;
    for (int k = 0; k < n; k++) a += out[k];
    a *= 1.0f / (float)n;
    if (a < 0 || 1.0f < a) {
        a = 0;
        for (int k = 0; k < n; k++) {
            out[k] = out[k] < 0 ? 0 : out[k] > 1.0f ? 1.0f : out[k];
            a += out[k];
        }
        a *= 1.0f / (float)n;
    }
    a = a < 0 ? 0 : a > 1.0f ? 1.0f : a;
    pp->level[cat] = a * 3.0f;
    return a;
}
int perf_cat_level(const PerfParts *pp, int cat)                         /* 0x5995e0 */
{
    if (cat < 0 || cat >= PERF_NCAT) return 0;
    PerfParts t = *pp;
    cat_frac(&t, cat, 0, NULL);
    return (int)(t.level[cat] + 0.5f);                                  /* fadd 0.5, ftol */
}

/* ---- the rebuild (0x5ac4f0) ------------------------------------------------- */
#define SF(off) rdf(S + (off))
#define OF(off) (*(float *)(O + (off)))
static float lerpf(float a, float b, float t) { return (b - a) * t + a; }
static int ftol_round(float x) { return (int)(x + 0.5f); }               /* fadd 0.5, 0x75bed4 */

void perf_build(const unsigned char *rec, PerfParts *pp, unsigned char *out)
{
    memcpy(out, rec, N2_REC_SIZE);
    const unsigned char *S = rec + 0x110;   /* stock P and the upgrade targets after it */
    unsigned char *O = out + 0x110;         /* the working P (0x2b0 bytes) */
    float c[4], f;
    if (!g_ready) return;
    memset(pp->level, 0, sizeof pp->level);
    /* 0x5a6b50 engine (cat 0): power, rev limit, mass */
    f = cat_frac(pp, 0, 1, c);
    if (f != 0) {
        OF(0) += SF(0x344) * f;
        OF(0x160) = lerpf(OF(0x160), SF(0x348), f);
        OF(0x110) *= (SF(0x340) - 1.0f) * f + 1.0f;
    }
    /* 0x5a6bd0 transmission (cat 1): gearbox set by level, final drive, ... */
    f = cat_frac(pp, 1, 3, c);
    if (f != 0) {
        int lvl = ftol_round(c[2] * 3.0f);              /* the gearbox part (3rd component) */
        if (0 < lvl && lvl < 4) {
            const unsigned char *set = S + 0x310 + lvl * 0x40;
            unsigned char *setw = out + 0x110 + 0x310 + lvl * 0x40;
            memcpy(O + 0x1b8, set + 8, 4);
            memcpy(O + 0x1bc, set + 0xc, 4);
            memcpy(O + 0x1c0, set + 0x10, 4);
            memcpy(O + 0x1b0, set, 4);
            memcpy(O + 0x1b4, set + 4, 4);
            memcpy(O + 0x1d0, set + 0x20, 0x20);
            int32_t n = 0;
            for (int i = 2; i < 8; i++) { if (rdf(set + 0x28 + (i - 2) * 4) == 0) break; n++; }
            memcpy(setw + 0x18, &n, 4);                                 /* written back into the table */
            memcpy(O + 0x1c8, &n, 4);
        }
        OF(0x1cc) = lerpf(OF(0x1cc), SF(0x3ec), c[0]);
        OF(0x1c4) = lerpf(OF(0x1c4), SF(0x3e4), c[0]);
        OF(0x1fc) += SF(0x41c) * c[1];
    }
    /* nitrous (cat 2): bottle towards the target, the boost set */
    f = cat_frac(pp, 2, 1, c);
    if (f != 0) {
        OF(0x2a0) = lerpf(OF(0x2a0), SF(0x580), c[0]);
        OF(0x2ac) = SF(0x58c);
    }
    /* 0x5a6d00 (cat 3): torque curve additions */
    f = cat_frac(pp, 3, 1, c);
    if (f != 0) {
        for (int k = 0; k < 9; k++) OF(0x200 + 4 * k) += SF(0x420 + 4 * k) * f;
        for (int k = 0; k < 3; k++) OF(0x248 + 4 * k) += SF(0x444 + 4 * k) * f;
    }
    /* 0x5a6e30 turbo (cat 4): the boost curve towards the target and into
       the torque curve -- always, so a stock turbo car gets its stock boost */
    f = cat_frac(pp, 4, 1, c);
    for (int k = 0; k < 9; k++) {
        OF(0x224 + 4 * k) = lerpf(OF(0x224 + 4 * k), SF(0x510 + 4 * k), f);
        OF(0x200 + 4 * k) += OF(0x224 + 4 * k);
    }
    /* 0x5a6fa0 suspension (cat 5): springs/dampers front and rear, ride
       height, steering */
    f = cat_frac(pp, 5, 3, c);
    if (f != 0) {
        for (int side = 0; side < 2; side++) {
            const unsigned char *src = S + (side ? 0x5e0 : 0x5c0);
            int b = side * 0x20;
            OF(0x18c + b) = lerpf(OF(0x18c + b), rdf(src + 0x1c), c[0]);
            OF(0x170 + b) = lerpf(OF(0x170 + b), rdf(src), c[0]);
            OF(0x174 + b) = lerpf(OF(0x174 + b), rdf(src + 4), c[0]);
            OF(0x184 + b) = lerpf(OF(0x184 + b), rdf(src + 0x14), c[0]);
            OF(0x178 + b) = lerpf(OF(0x178 + b), rdf(src + 8), c[0]);
            OF(0x188 + b) = lerpf(OF(0x188 + b), rdf(src + 0x18), c[0]);
            OF(0x17c + b) = lerpf(OF(0x17c + b), rdf(src + 0xc), c[0]);
            OF(0x180 + b) = lerpf(OF(0x180 + b), rdf(src + 0x10), c[1]);
        }
        OF(0x18) += c[0] * SF(0x604);
        OF(0x48) += c[0] * SF(0x604);
        OF(0x78) += c[0] * SF(0x608);
        OF(0xa8) += c[0] * SF(0x608);
        OF(8) -= SF(0x600) * c[0];
        OF(0x270) = lerpf(OF(0x270), SF(0x5bc), c[2]);
    }
    /* 0x5a7140 brakes (cat 6) */
    f = cat_frac(pp, 6, 1, c);
    if (f != 0) {
        OF(0x27c) = lerpf(OF(0x27c), SF(0x5b8), f);
        OF(0x278) = lerpf(OF(0x278), SF(0x5b4), f);
        OF(0x274) = lerpf(OF(0x274), SF(0x5b0), f);
    }
    /* 0x5a71c0 (cat 7): idle/redline/limit set by level, torque additions */
    f = cat_frac(pp, 7, 1, c);
    if (pp->fit[0x2d] || pp->fit[0x2e] || pp->fit[0x33]) OF(0x290) = 0;
    if (f != 0) {
        int lvl = ftol_round(pp->level[7]);
        if (0 < lvl && lvl < 4) {
            const unsigned char *set = S + 0x410 + lvl * 0x40;
            OF(0x1f0) += rdf(set);
            OF(0x1f4) += rdf(set + 4);
            OF(0x1f8) += rdf(set + 8);
        }
        for (int k = 0; k < 9; k++) OF(0x200 + 4 * k) += SF(0x4e0 + 4 * k) * f;
        for (int k = 0; k < 3; k++) OF(0x248 + 4 * k) += SF(0x504 + 4 * k) * f;
    }
    /* 0x5a7360 tyres (cat 8) */
    f = cat_frac(pp, 8, 1, c);
    if (f != 0) {
        static const int OO[10] = { 0xd8, 0xd4, 0xd0, 0xe0, 0xdc, 0xf8, 0xf4, 0xf0, 0x100, 0xfc };
        static const int SS[10] = { 0x548, 0x544, 0x540, 0x550, 0x54c, 0x568, 0x564, 0x560, 0x570, 0x56c };
        for (int i = 0; i < 10; i++) OF(OO[i]) = lerpf(OF(OO[i]), SF(SS[i]), f);
    }
    /* (0x5a6880, the tyre randomiser, is off: 0x7fc290 = 1) */
    /* 0x5a74a0 (cat 9) */
    f = cat_frac(pp, 9, 2, c);
    if (f != 0) {
        OF(0x264) += (SF(0x590) + SF(0x5a0)) * c[0];
        OF(0x268) += (SF(0x594) + SF(0x5a4)) * c[1];
        OF(0x26c) += (SF(0x598) + SF(0x5a8)) * f;
    }
    /* inertia from the (possibly lighter) mass: 0x43c780 identity, box
       moments m (b^2 + c^2) / 12 */
    {
        float m = OF(0x110), x = OF(0x114), y = OF(0x118), z = OF(0x11c);
        float *I = &OF(0x120);
        for (int i = 0; i < 16; i++) I[i] = (i % 5) == 0 ? 1.0f : 0.0f;
        I[0] = (z * z + y * y) * m * 0.0833333f;
        I[5] = (z * z + x * x) * m * 0.0833333f;
        I[10] = (x * x + y * y) * m * 0.0833333f;
    }
    /* 0x5997e0 (visual parts' physics: kits, hoods, spoilers) adds nothing
       without visual parts -- the opponents' stock look */
}
#undef SF
#undef OF

/* ---- ratings ----------------------------------------------------------------- */
static int no_ground(void *u, float x, float y, float zr, float *z, float n[3], int *s)
{
    (void)u; (void)x; (void)y; (void)zr; (void)z; (void)n; (void)s;
    return 0;
}
float perf_rating(const unsigned char *built)
{
    if (rds32(built + 0x844) != 0) return 0;                            /* 0x5b4dc0: not rated */
    N2Sim *s = n2sim_create(built, no_ground, NULL);
    if (!s) return 0;
    float r = n2sim_accel_rating(s);
    n2sim_destroy(s);
    return (float)(int)r;                                               /* stored as an int (+8) */
}
float perf_level(float rating)                                          /* 0x5b6990(1, -1) */
{
    return (rating - g_lim[10]) / (g_lim[11] - g_lim[10]) * 10.0f;
}
int perf_class(float rating)
{
    static const float THR[4] = { 0, 273, 401, 500 };                   /* 0x7a1a84 */
    for (int i = 3; i >= 0; i--) if (THR[i] < rating) return 3 - i;
    return 3;
}
float perf_handling(const unsigned char *built)                         /* 0x5b5490 */
{
    static const float W[9] = { 0.14f, 0.1f, 0.15f, 0.15f, 0.1f, 0.15f, 0.15f, 0.08f, 0.08f };   /* 0x7a1a94 */
    const unsigned char *P = built + 0x110;
    const float *L = g_lim;
#define PP(o) rdf(P + (o))
    float im = 1.0f / PP(0x110), v[9];
    v[0] = ((PP(0xe0) + PP(0xdc) + PP(0x100) + PP(0xfc)) * 0.5f * im * 0.5f - L[14]) / (L[15] - L[14]);
    v[1] = ((PP(0x274) * 0.65f + PP(0x278) * 0.35f) * im - L[16]) / (L[17] - L[16]);
    v[2] = ((PP(0x174) * PP(0x170) + PP(0x180) + PP(0x194) * PP(0x190) + PP(0x1a0)) * 0.5f * im * 0.5f - L[12])
           / (L[13] - L[12]);
    v[3] = 1.0f - (PP(0x110) - L[18]) / (L[19] - L[18]);
    v[5] = 1.0f - (PP(8) - L[20]) / (L[21] - L[20]);
    v[6] = 1.0f - (PP(0x160) - L[22]) / (L[23] - L[22]);
    v[7] = (PP(0x264) - L[24]) / (L[25] - L[24]);
    v[8] = (PP(0x268) - L[26]) / (L[27] - L[26]);
    float r = PP(0x1c0);
    v[4] = r <= 0.6f ? (r + 0.2f) * 1.25f : (1.0f - r) * 2.5f;
#undef PP
    float h = 0;
    for (int i = 0; i < 9; i++) h += W[i] * v[i];
    if (!(0.03f < h)) return 0.03f;
    return h > 1.0f ? 1.0f : h;
}

static float clamp_stat(float v) { return !(0.03f < v) ? 0.03f : v > 1.0f ? 1.0f : v; }

/* The car select's sliders (0x497cd0) from a built record. The physics
   record P is the PerfConfig + 0x30 (0x5b5930 reads the drag at +0x290 /
   +0x140, n2sim_accel_rating at P+0x260 / P+0x110). */
void perf_car_stats(const unsigned char *built, float out[3])
{
    out[0] = out[1] = out[2] = 0.03f;
    if (rds32(built + 0x844) != 0) return;                              /* not rated */
    N2Sim *s = n2sim_create(built, no_ground, NULL);
    if (!s) return;
    const float *L = g_lim;
    float d64[64], x1 = n2sim_accel_curve(s, d64);
    /* 0x5b5930: the speed where the drag meets the +0x55c acceleration
       curve, capped by the limiter (+0x2c0 mph x 0.447027) */
    float v = 0;
    if (x1 > 0) {
        float inv = 63.0f / x1;
        for (;;) {
            float t = v * inv; int i = (int)t;
            float f = i >= 63 ? d64[63] : (1.0f - (t - (float)i)) * d64[i] + (t - (float)i) * d64[i + 1];
            if (!((v * n2sim_param(s, 0x260) * v) / n2sim_param(s, 0x110) <= f)) break;
            v += 1.34108f;
            if (!(v < x1)) break;
        }
    }
    float lim = n2sim_param(s, 0x290);
    if (0.0f < lim && lim * 0.447027f < v) v = lim * 0.447027f;
    /* the nitrous term both share: P+0x2ac x P+0x2a0 over GLOBALB 0x34a32 [8], [9] */
    float nos = (n2sim_param(s, 0x2ac) * n2sim_param(s, 0x2a0) - L[8]) / (L[9] - L[8]);
    /* 0x5b53a0: 0.82 x the acceleration rating (0x59a350) over [2], [3], 0.1 x
       nitrous, 0.08 x a third of round(P+0x2b4) */
    float acc = (1.0f - (0.08f + 0.1f)) * ((n2sim_accel_rating(s) - L[2]) / (L[3] - L[2]))
              + nos * 0.1f + (float)(int)(n2sim_param(s, 0x2b4) + 0.5f) * 0.333333f * 0.08f;
    /* 0x5b6a20: 0.085 x nitrous + 0.915 x the top speed over [0], [1] */
    float top = nos * 0.085f + (1.0f - 0.085f) * ((v - L[0]) / (L[1] - L[0]));
    out[0] = clamp_stat(acc);
    out[1] = clamp_stat(top);
    out[2] = perf_handling(built);                                      /* 0x5b5490 */
    n2sim_destroy(s);
}

void perf_fit_rating(int type, const unsigned char *rec, float target, float handling,
                     PerfParts *pp, unsigned char *out)                 /* 0x5b6ad0 */
{
    if (type < 0 || type >= g_ntype || g_ncombo[type] <= 0) { perf_clear(pp); perf_build(rec, pp, out); return; }
    const unsigned char *E = g_combo[type];
    int n = g_ncombo[type];
    for (int pass = 0; pass < 2; pass++) {
        const unsigned char *pick = E;
        if (n != 1) {
            const unsigned char *a = E, *b = E;
            int i = 0;
            do {
                a = E + i * 16; b = a + 16;
                if (target < rdf(b)) break;
                i++;
            } while (i < n - 1);
            pick = fabsf(rdf(a) - target) < fabsf(rdf(b) - target) ? a : b;
        }
        for (int i = 0; i < g_npkg; i++) {                              /* every listed part off */
            const unsigned char *k = g_pkg + i * PKG_SIZE;
            for (int j = 0; j < pkg_n(k); j++) {
                int idx = part_idx(pkg_part(k, j));
                if (idx >= 0 && idx < PERF_NPARTS) pp->fit[idx] = 0;
            }
        }
        for (int i = 0; i < 0x44; i++)
            if ((pick[5 + (i >> 3)] >> (7 - (i & 7))) & 1) pp->fit[i] = 1;
        perf_build(rec, pp, out);
        if (handling < 0 || pass == 1) return;
        target = (handling - perf_handling(out)) * 150.0f + target;     /* 0x7a1a7c */
    }
}

/* ---- race-type physics changes (0x5b4dc0 tail) ---------------------------- */
static void wrf(unsigned char *p, float f) { memcpy(p, &f, 4); }
/* 0x7fc2e4 + 0x144 k: drift tables, 22 floats {level 0 values .. level 3} */
static const float DRIFT_T[3][22] = {
    { 0, 0.15f, 1, 1, 0.95f, 1, 0, 0.95f, 0.95f, 1, 1,  0, 0.15f, 1, 1, 0.95f, 1, 0, 0.9f, 0.9f, 0.9f, 0.9f },
    { 0, 0,     1, 1, 0.95f, 1, 0, 1,     1,     1, 1,  0, 0,     1, 1, 0.9f,  1, 0, 1,    1,    1.1f, 1.1f },
    { 0, -0.15f,1, 1, 0.95f, 1, 0, 1,     1,     1, 1,  0, -0.15f,1, 1, 0.95f, 1, 0, 1,    1,    1,    1    },
};
/* 0x5a7530 */
/* 0x7fc394 + 0x144 k: drag tables (all three the same in the retail EXE) */
static const float DRAG_T[3][22] = {
    { 0, 0, 1.2f, 1.2f, 0.9f, 0.95f, 0, 1, 1, 1, 1,  0, 0, 1.4f, 1.4f, 0.9f, 0.95f, 0, 1, 1, 1, 1 },
    { 0, 0, 1.2f, 1.2f, 0.9f, 0.95f, 0, 1, 1, 1, 1,  0, 0, 1.4f, 1.4f, 0.9f, 0.95f, 0, 1, 1, 1, 1 },
    { 0, 0, 1.2f, 1.2f, 0.9f, 0.95f, 0, 1, 1, 1, 1,  0, 0, 1.4f, 1.4f, 0.9f, 0.95f, 0, 1, 1, 1, 1 },
};
/* 0x7fc33c + 0x144 k: street X tables */
static const float STREETX_T[3][22] = {
    { 0, 0, 1.1f, 1.1f, 0.92f, 1, 0.02f, 1.08f, 1.1f, 1.1f, 1.09f,  0, 0, 1.12f, 1.4f, 0.88f, 0.95f, 0.02f, 1.1f, 1.12f, 1.15f, 1.1f },
    { -0.015f, 0, 1.07f, 1.4f, 0.88f, 0.985f, 0.02f, 1.12f, 1.1f, 1.35f, 1.12f,  -0.01f, 0, 1.15f, 1.3f, 0.95f, 0.95f, 0.03f, 1.15f, 1.075f, 1.3f, 1.075f },
    { 0, 0, 1.13f, 1.35f, 0.9f, 1, 0.02f, 1.06f, 1.1f, 1.1f, 1.1f,  0, 0, 1.3f, 1.6f, 0.8f, 0.9f, 0, 1.15f, 1.15f, 1.14f, 1.1f },
};
static void race_mods(unsigned char *P, const float *lvl, const float *T)
{
#define PFL(o) rdf(P + (o))
#define PSET(o, v) wrf(P + (o), (v))
    const float third = 0.333333343f;                                   /* 0x786348 */
    float f = lvl[9] * third;
    PSET(0x264, ((T[13] - T[2]) * f + T[2]) * PFL(0x264));
    PSET(0x268, ((T[14] - T[3]) * f + T[3]) * PFL(0x268));
    f = lvl[0] * third;
    PSET(0x160, ((T[15] - T[4]) * f + T[4]) * PFL(0x160));
    PSET(0x110, ((T[16] - T[5]) * f + T[5]) * PFL(0x110));
    PSET(8, PFL(8) - (lvl[5] * third * (T[17] - T[6]) + T[6]));
    f = lvl[8] * third;
    for (int i = 0; i < 2; i++) {
        int t = 0xd0 + i * 0x20;
        PSET(t + 0xc, ((T[i + 18] - T[i + 7]) * f + T[i + 7]) * PFL(t + 0xc));
        PSET(t + 0x10, ((T[i + 20] - T[i + 9]) * f + T[i + 9]) * PFL(t + 0x10));
    }
    PSET(0x27c, lvl[6] * third * (T[11] - T[0]) + PFL(0x27c) + T[0]);
    PSET(0x1c0, lvl[1] * third * (T[12] - T[1]) + T[1] + PFL(0x1c0));
    float v = PFL(8);
    PSET(8, v < 0 ? 0 : v);
    v = PFL(0x27c);
    PSET(0x27c, v <= 0 ? 0 : (v <= 1.0f ? v : 1.0f));
    /* 0x43c780 then the box inertia from the dimensions (P+0x114..) */
    for (int i = 0; i < 16; i++) PSET(0x120 + 4 * i, i % 5 == 0 ? 1.0f : 0);   /* 0x43c780 identity */
    float x2 = PFL(0x114) * PFL(0x114), y2 = PFL(0x118) * PFL(0x118), z2 = PFL(0x11c) * PFL(0x11c), m = PFL(0x110);
    PSET(0x120, (z2 + y2) * m * 0.0833333358f);                        /* 0x7875b0 */
    PSET(0x134, (z2 + x2) * m * 0.0833333358f);
    PSET(0x148, (y2 + x2) * m * 0.0833333358f);
#undef PFL
#undef PSET
}
void perf_race_mods(unsigned char *built, const PerfParts *pp, int mode)
{
    unsigned char *P = built + 0x110;
    if (mode == PERF_MODE_DRIFT) {                                      /* 0x5a7750 */
        float rear = rdf(P + 0x1c0);
        int k = !(rear < 0.2f);
        if (0.8f < rear) k = 2;
        race_mods(P, pp->level, DRIFT_T[k]);
        wrf(P + 0x2a0, 0);
    } else if (mode == PERF_MODE_DRAG || mode == PERF_MODE_STREETX || mode == PERF_MODE_STREETX2) {
        /* 0x5a7830 drag / 0x5a77c0 street X (modes 5 and 10): the same
           pick by rear weight; street X also clears +0x2a0 */
        float rear = rdf(P + 0x1c0);
        int k = !(rear < 0.2f);
        if (0.8f < rear) k = 2;
        race_mods(P, pp->level, mode == PERF_MODE_DRAG ? DRAG_T[k] : STREETX_T[k]);
        if (mode != PERF_MODE_DRAG) wrf(P + 0x2a0, 0);
    }
}
