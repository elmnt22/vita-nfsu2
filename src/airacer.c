/* airacer.c — the retail NFSU2 AI racer (see airacer.h). */
#include <stdarg.h>
#include "airacer.h"
#include "world_resident.h"
#include "wcoll.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stage timing of the racers' ticks (log "ai prof") ---- */
extern uint64_t SDL_GetPerformanceCounter(void);
extern uint64_t SDL_GetPerformanceFrequency(void);
enum { AP_FRAME, AP_THINK, AP_TH_PATH, AP_TH_CHOOSE, AP_TH_BEGIN, AP_TH_APPLY, AP_FR_PATH, AP_FR_APPLY, AP_DRIVER, AP_N };
static const char *const ap_name[AP_N] = { "frame", "think", "think.path", "think.choose", "think.begin", "think.apply",
                                           "tick.path", "tick.apply", "driver" };
/* think.begin by the action's type (ACT_* / TR_*): which manoeuvre's plan is slow */
enum { AP_TYPES = 24 };
static double apt_sum[AP_TYPES], apt_max[AP_TYPES]; static long apt_cnt[AP_TYPES];
static double ap_sum[AP_N], ap_max[AP_N]; static long ap_cnt[AP_N], ap_frames;
static void ap_add(int k, uint64_t t0)
{
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    ap_sum[k] += ms; ap_cnt[k]++;
    if (ms > ap_max[k]) ap_max[k] = ms;
}
static void ap_report(void)
{
    if (++ap_frames < 3600) return;
    char line[512]; int len = snprintf(line, sizeof line, "ai prof over %ld racer ticks (ms: calls mean max):", ap_frames);
    for (int k = 0; k < AP_N; k++)
        if (ap_cnt[k]) len += snprintf(line + len, sizeof line - (size_t)len, " %s %ld %.3f %.2f",
                                       ap_name[k], ap_cnt[k], ap_sum[k] / ap_cnt[k], ap_max[k]);
    printf("%s\n", line);
    len = snprintf(line, sizeof line, "ai prof think.begin by action (type calls mean max):");
    for (int k = 0; k < AP_TYPES; k++)
        if (apt_cnt[k] && len < (int)sizeof line - 40)
            len += snprintf(line + len, sizeof line - (size_t)len, " %d %ld %.3f %.2f",
                            k, apt_cnt[k], apt_sum[k] / apt_cnt[k], apt_max[k]);
    printf("%s\n", line);
    memset(apt_sum, 0, sizeof apt_sum); memset(apt_max, 0, sizeof apt_max); memset(apt_cnt, 0, sizeof apt_cnt);
    memset(ap_sum, 0, sizeof ap_sum); memset(ap_max, 0, sizeof ap_max); memset(ap_cnt, 0, sizeof ap_cnt); ap_frames = 0;
}

/* ---- small math (0x43c..., 0x574...) ------------------------------------- */
typedef struct { float x, y; } V2;

static float v2len(float x, float y) { return sqrtf(x * x + y * y); }
/* 0x43ce50: unit vector, (1,0) for a zero vector */
static void v2norm(float *o, const float *v)
{
    float l = v2len(v[0], v[1]);
    if (l == 0) { o[0] = 1; o[1] = 0; return; }
    float k = 1.0f / l, x = v[0], y = v[1];
    o[0] = k * x; o[1] = k * y;
}
/* 0x43ceb0: v scaled to length len, (1,0) for a zero vector */
static void v2setlen(float *o, const float *v, float len)
{
    float k = v2len(v[0], v[1]) / len;
    if (k == 0) { o[0] = 1; o[1] = 0; return; }
    k = 1.0f / k;
    float x = v[0], y = v[1];
    o[0] = k * x; o[1] = k * y;
}

/* 0x574830..0x574c30: a 2-D cubic Hermite curve, one cubic per axis
   ({p0, t0, p1, t1, a, b, c, d}: x at +0, y at +0x30) */
typedef struct { float p0, t0, p1, t1, a, b, c, d; } Herm1;
typedef struct { Herm1 x, y; } Herm2;
static void herm1_solve(Herm1 *h)                                       /* 0x574830 */
{
    float D = h->p1 - h->p0;
    h->c = h->t0; h->d = h->p0;
    h->a = (h->t0 + h->t1) - (D + D);
    h->b = (D * 3.0f - h->t1) - (h->t0 + h->t0);
}
static void herm2_set(Herm2 *h, const float *p0, const float *t0, const float *p1, const float *t1)
{
    h->x.p0 = p0[0]; h->y.p0 = p0[1];                                   /* 0x574b10 */
    h->x.t0 = t0[0]; h->y.t0 = t0[1];                                   /* 0x574b50 */
    h->x.p1 = p1[0]; h->y.p1 = p1[1];                                   /* 0x574b90 */
    h->x.t1 = t1[0]; h->y.t1 = t1[1];                                   /* 0x574bd0 */
    herm1_solve(&h->x); herm1_solve(&h->y);
}
static void herm2_at(const Herm2 *h, float t, float *o)                 /* 0x574c30 */
{
    o[0] = ((h->x.a * t + h->x.b) * t + h->x.c) * t + h->x.d;
    o[1] = ((h->y.a * t + h->y.b) * t + h->y.c) * t + h->y.d;
}

/* ---- road network --------------------------------------------------------- */
#define SEC_MAX 2048
struct AiNet {
    unsigned char *data; long size;
    const unsigned char *sec[SEC_MAX];      /* 0x88a334: section by id (flags & 0x7ff) */
    int nsec;
    const unsigned char *links; int nlinks; /* 0x883db0 / 0x883db4 */
    const unsigned char *sectors; int nsectors; /* 0x883dbc / 0x883dc0 */
    const unsigned char *x34124; int n34124;    /* 0x883dc4 / 0x883dc8 */
    const unsigned char *order[SEC_MAX]; int norder;   /* 0x88d21c / 0x88d218: the sections in load order */
    /* port-only search index (not in the original): the links' progress
       values ascending with their link numbers, for human_linkgates */
    float *lk_x; int *lk_i; int lk_n;
};

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rdu16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t  rds16(const unsigned char *p) { return (int16_t)rdu16(p); }
static float    rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* the links' progress (+0x0, rdf) sorted, NaNs left out (no window test passes them) */
static const float *lk_sort_x;
static int lk_cmp(const void *a, const void *b)
{
    float x = lk_sort_x[*(const int *)a], y = lk_sort_x[*(const int *)b];
    return x < y ? -1 : x > y ? 1 : *(const int *)a - *(const int *)b;
}
static void ainet_index_links(AiNet *net)
{
    net->lk_n = 0;
    if (net->nlinks <= 0) return;
    float *all = (float *)malloc((size_t)net->nlinks * sizeof *all);
    int *ord = (int *)malloc((size_t)net->nlinks * sizeof *ord);
    net->lk_x = (float *)malloc((size_t)net->nlinks * sizeof *net->lk_x);
    net->lk_i = ord;
    if (!all || !ord || !net->lk_x) { free(all); free(ord); free(net->lk_x); net->lk_x = NULL; net->lk_i = NULL; return; }
    int m = 0;
    for (int i = 0; i < net->nlinks; i++) {
        all[i] = rdf(net->links + i * 0x20);
        if (all[i] == all[i]) ord[m++] = i;
    }
    lk_sort_x = all;
    qsort(ord, (size_t)m, sizeof *ord, lk_cmp);
    for (int k = 0; k < m; k++) net->lk_x[k] = all[ord[k]];
    net->lk_n = m;
    free(all);
}

AiNet *ainet_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    AiNet *net = (AiNet *)calloc(1, sizeof *net);
    net->data = (unsigned char *)malloc((size_t)n + 16);
    if (!net->data || fread(net->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(net->data); free(net); return NULL;
    }
    fclose(f);
    net->size = n;
    for (long o = 0; o + 8 <= n; ) {                                   /* 0x5deb80 */
        uint32_t id = rd32(net->data + o), sz = rd32(net->data + o + 4);
        const unsigned char *c = net->data + o + 8;
        if (o + 8 + (long)sz > n) break;
        if (id == 0x34121) {                                            /* 0x5de1c0 */
            for (const unsigned char *s = c; s + 0x8c <= c + sz; ) {
                int np = rds16(s + 0x34);
                unsigned sid = rdu16(s + 10);
                if (sid < SEC_MAX) net->sec[sid] = s;
                if (net->norder < SEC_MAX) net->order[net->norder++] = s;
                net->nsec++;
                s += 0x8c + np * 0x38;
            }
        } else if (id == 0x34122) {
            net->links = c + 4;
            net->nlinks = (int)((sz - 4) >> 5);
        } else if (id == 0x34123) {
            net->sectors = c; net->nsectors = (int)(sz / 0x114);
        } else if (id == 0x34124) {
            net->x34124 = c; net->n34124 = (int)(sz / 0x108);
        }
        o += 8 + (long)sz;
    }
    ainet_index_links(net);
    return net;
}
void ainet_free(AiNet *n) { if (n) { free(n->lk_x); free(n->lk_i); free(n->data); free(n); } }
/* the furthest route distance of any point (a circuit's lap) */
float ainet_route_length(const AiNet *n)
{
    float m = 0;
    for (int s = 0; s < SEC_MAX; s++) {
        const unsigned char *sc = n->sec[s];
        if (!sc) continue;
        for (int i = 0; i < rds16(sc + 0x34); i++) { float d = rdf(sc + 0x8c + i * 0x38 + 8); if (d > m) m = d; }
    }
    return m;
}
int  ainet_sections(const AiNet *n) { return n ? n->nsec : 0; }

/* A route point, 0x38 bytes: +0 x, +4 y, +8 distance along the route, +0xc
   flags (bits 0-10 section id, 19-24 sector, 0x3f none), +0x10 u16/256
   length to the next point, +0x12 s16/8 height, +0x14/+0x16 u16/256 half
   widths left/right, +0x18/+0x1a s16/32767 unit direction, +0x1c/+0x1e
   links (0xffff none), +0x20/+0x21 s8/128 camber and grade, +0x22.. s8/128
   lane offsets (x the wider half width; lanes 0/1 the racing line forward /
   backward, 2..5 and 6..9 the traffic lanes), +0x2c.. u8 lane speeds m/s. */
typedef const unsigned char Pt;
#define DIRK (1.0f / 32767.0f)          /* 0x81e7fc / 0x88ea70 */
static float pt_x(Pt *p) { return rdf(p); }
static float pt_y(Pt *p) { return rdf(p + 4); }
static uint32_t pt_flags(Pt *p) { return rd32(p + 0xc); }
static float pt_seglen(Pt *p) { return (float)rdu16(p + 0x10) * (1.0f / 256.0f); }
static float pt_z(Pt *p) { return (float)rds16(p + 0x12) * 0.125f; }
static float pt_wl(Pt *p) { return (float)rdu16(p + 0x14) * (1.0f / 256.0f); }
static float pt_wr(Pt *p) { return (float)rdu16(p + 0x16) * (1.0f / 256.0f); }
static float pt_dx(Pt *p) { return (float)rds16(p + 0x18) * DIRK; }
static float pt_dy(Pt *p) { return (float)rds16(p + 0x1a) * DIRK; }
static float pt_camber(Pt *p) { return (float)(signed char)p[0x20] * (1.0f / 128.0f); }
static float pt_grade(Pt *p) { return (float)(signed char)p[0x21] * (1.0f / 128.0f); }
static float pt_lanefrac(Pt *p, int lane) { return (float)(signed char)p[0x22 + lane] * (1.0f / 128.0f); }
static float pt_lanespeed(Pt *p, int lane) { return (float)p[0x2c + lane]; }

static const unsigned char *sec_of(const AiNet *n, Pt *p) { return n->sec[pt_flags(p) & 0x7ff]; }
static int sec_npts(const unsigned char *s) { return rds16(s + 0x34); }
static Pt *sec_pt(const unsigned char *s, int i) { return s + 0x8c + i * 0x38; }
static int pt_index(const AiNet *n, Pt *p)                              /* 0x5d89b0 */
{
    if (!p) return (int)((0 - 0x8c) / 0x38);
    return (int)((p - sec_of(n, p)) - 0x8c) / 0x38;
}
static Pt *pt_prev(const AiNet *n, Pt *p)                               /* 0x5d89f0 */
{
    if (!p) return NULL;
    int i = pt_index(n, p);
    return i ? sec_of(n, p) + i * 0x38 + 0x54 : NULL;
}
static Pt *pt_at(const AiNet *n, int sec, int idx)                      /* inline (FuturePoint +0x5c/+0x5e) */
{
    if (sec == -1 || idx == -1) return NULL;
    if (sec < 0) return NULL;             /* the original adds no section: an invalid address */
    const unsigned char *s = n->sec[sec];
    return s ? sec_pt(s, idx) : NULL;
}
/* 0x401c40: a lane's offset from the centre line, + right */
static float pt_laneoff(Pt *p, int lane)
{
    float r = pt_wr(p), l = pt_wl(p);
    if (r < l) r = l;
    return pt_lanefrac(p, lane) * r;
}
/* 0x401aa0: the travel direction backward over the segment before p */
static void pt_backdir(const AiNet *n, Pt *p, float *o)
{
    Pt *q = pt_prev(n, p);
    if (!q) q = p;
    o[0] = -pt_dx(q); o[1] = -pt_dy(q);
}
static float pt_prevseglen(const AiNet *n, Pt *p)                      /* 0x40ccc0 / 0x40d200 */
{
    Pt *q = pt_prev(n, p);
    return pt_seglen(q ? q : p);
}

/* links, 32 bytes: +4/+6 point index at either end, +8/+10 section ids,
   +0xc flags (nibble 0 kind: 3 = reached from its far end; bit 18 the
   direction it may be taken in, 21, 23 flips the direction of travel, 26
   a junction crossing) */
typedef const unsigned char Lk;
static uint32_t lk_flags(Lk *l) { return rd32(l + 0xc); }
static int pt_nlinks(const AiNet *n, Pt *p)                             /* 0x5d4380 */
{
    int c = 0;
    for (int k = 0; k < 2; k++) { unsigned v = rdu16(p + 0x1c + 2 * k); if (v != 0xffff && (int)v < n->nlinks) c++; }
    return c;
}
static Lk *pt_link(const AiNet *n, Pt *p, int which)                    /* 0x5d43c0 */
{
    int c = 0;
    for (int k = 0; k < 2; k++) {
        unsigned v = rdu16(p + 0x1c + 2 * k);
        if (v != 0xffff && (int)v < n->nlinks) { if (c == which) return n->links + v * 0x20; c++; }
    }
    return NULL;
}
static int lk_index(const AiNet *n, Lk *l) { return (int)((l - n->links) >> 5); }
static const unsigned char *lk_sec(const AiNet *n, Lk *l)               /* 0x5d86a0 */
{
    int s = (lk_flags(l) & 0xf) == 3 ? rds16(l + 10) : rds16(l + 8);
    return s >= 0 && s < SEC_MAX ? n->sec[s] : NULL;
}
static Pt *lk_pt(const AiNet *n, Lk *l)                                 /* 0x5d86d0 */
{
    int i, s;
    if ((lk_flags(l) & 0xf) == 3) { i = rds16(l + 6); s = rds16(l + 10); }
    else { i = rds16(l + 4); s = rds16(l + 8); }
    if (s < 0 || s >= SEC_MAX || !n->sec[s]) return NULL;
    return sec_pt(n->sec[s], i);
}

/* ---- FuturePoint (0x78 bytes, pool 0x81cbb4; ctor 0x411b50 / 0x40ad20) ----
   One point of the path ahead: a route point, the distance to it along the
   path from the car, the lateral shift of the racing line (lane 0) and of
   the traffic lanes 2..5 at it, their lateral limits and speeds. Two
   vtables: 0x784a48 walks the route points forward, 0x784aa0 backward. */
typedef struct FList FList;
typedef struct FPoint {
    int   back;                /* vtable 0x784aa0 (vt+4 returns 1) */
    float shift[5];            /* +0x04 lane 0, +0x08..+0x14 lanes 2..5 */
    float lo[5];               /* +0x18 lane 0, +0x1c.. lanes 2..5 (0x41fd50) */
    float hi[5];               /* +0x2c, +0x30.. */
    float speed[5];            /* +0x40 lane 0, +0x44.. lanes 2..5 (-1 unset) */
    float dist;                /* +0x54 */
    float off;                 /* +0x58 */
    short sec, pt;             /* +0x5c +0x5e */
    Lk   *link;                /* +0x60 the link taken to reach it */
    FList *list;               /* +0x64 */
    struct FPoint *prev, *next;/* +0x68 +0x6c */
    int   x70;                 /* +0x70 */
    unsigned char interp;      /* +0x74 an in-between point (0x418da0) */
} FPoint;

/* lane slot of a FuturePoint field: lane 0 -> [0], lanes 2..5 -> [1..4] */
static int lslot(int lane) { return lane == 0 ? 0 : (lane >= 2 && lane <= 5) ? lane - 1 : -1; }
static float fp_shift(const FPoint *f, int lane) { int s = lslot(lane); return s < 0 ? 0 : f->shift[s]; }
static float fp_speed(const FPoint *f, int lane) { int s = lslot(lane); return s < 0 ? 0 : f->speed[s]; }   /* 0x40afc0 */

/* Recycled fixed-size blocks for the objects the racers make and drop every
   tick (path points, actions). hwlog28: each calloc / free of them on the
   frame thread could wait up to ~10 ms on the heap lock while a streaming
   worker held it; a pool never touches the heap after its first chunks. The
   objects are the same, zeroed as calloc gave them. */
typedef struct PoolFree { struct PoolFree *next; } PoolFree;
typedef struct { size_t size; PoolFree *free; volatile int lock; } Pool;
static void *pool_take(Pool *pl)
{
    while (__sync_lock_test_and_set(&pl->lock, 1)) { }
    if (!pl->free) {                                                    /* a chunk of 64 more */
        size_t sz = (pl->size + 15) & ~(size_t)15;
        unsigned char *c = (unsigned char *)malloc(sz * 64);
        if (c) for (int i = 63; i >= 0; i--) { PoolFree *b = (PoolFree *)(c + sz * (size_t)i); b->next = pl->free; pl->free = b; }
    }
    PoolFree *b = pl->free;
    if (b) pl->free = b->next;
    __sync_lock_release(&pl->lock);
    if (b) memset(b, 0, pl->size);
    return b;
}
static void pool_give(Pool *pl, void *p)
{
    if (!p) return;
    while (__sync_lock_test_and_set(&pl->lock, 1)) { }
    ((PoolFree *)p)->next = pl->free; pl->free = (PoolFree *)p;
    __sync_lock_release(&pl->lock);
}
static Pool g_fp_pool = { sizeof(FPoint), NULL, 0 };

static FPoint *fp_new(int back)                                         /* 0x411b50 */
{
    FPoint *f = (FPoint *)pool_take(&g_fp_pool);
    if (!f) return NULL;
    f->back = back;
    f->sec = f->pt = -1;                                                /* 0x40ad20 */
    for (int i = 0; i < 5; i++) f->speed[i] = -1.0f;                   /* 0x783c60 */
    return f;
}
static void fp_free(FPoint *f) { pool_give(&g_fp_pool, f); }           /* 0x40d270 */

/* vt+8: which of the point's lane bytes a lane uses (backward: 0 -> 1,
   2..5 -> 6..9) */
static int fp_lanemap(const FPoint *f, int lane)                        /* 0x40d080 / 0x40d3a0 */
{
    if (!f->back) return lane;
    if (lane == 0) return 1;
    if (lane >= 2 && lane < 6) return lane + 4;
    return lane;
}
/* vt+0xc / vt+0x10: the road edges left / right of the lane line */
static float fp_left(const AiNet *n, const FPoint *f, int lane)         /* 0x40b2a0 / 0x40b840 */
{
    Pt *p = pt_at(n, f->sec, f->pt);
    float s = fp_shift(f, lane);
    if (!f->back) return -pt_wl(p) - (pt_laneoff(p, lane) + s);
    return -pt_wr(p) - (pt_laneoff(p, fp_lanemap(f, lane)) + s);
}
static float fp_right(const AiNet *n, const FPoint *f, int lane)        /* 0x40b1f0 / 0x40b780 */
{
    Pt *p = pt_at(n, f->sec, f->pt);
    float s = fp_shift(f, lane);
    if (!f->back) return pt_wr(p) - (pt_laneoff(p, lane) + s);
    return pt_wl(p) - (pt_laneoff(p, fp_lanemap(f, lane)) + s);
}
/* vt+0x1c: the direction of travel at the point; vt+0x20 the other way */
static void fp_dir(const AiNet *n, const FPoint *f, Pt *p, float *o)    /* 0x40d2c0 / 0x40d090 */
{
    if (!f->back) { o[0] = pt_dx(p); o[1] = pt_dy(p); }
    else pt_backdir(n, p, o);
}
static void fp_odir(const AiNet *n, const FPoint *f, Pt *p, float *o)
{
    if (f->back) { o[0] = pt_dx(p); o[1] = pt_dy(p); }
    else pt_backdir(n, p, o);
}
/* vt+0x24 grade along the travel */
static float fp_grade(const AiNet *n, const FPoint *f, Pt *p)           /* 0x40d0b0 / 0x40d300 */
{
    if (!f->back) return pt_grade(p);
    Pt *q = pt_prev(n, p);
    return -pt_grade(q ? q : p);
}
/* vt+0x28 / vt+0x2c: how far q lies along the travel past the point */
static float fp_along(const AiNet *n, const FPoint *f, Pt *p, const float *q)   /* 0x40d160 / 0x40d3d0 */
{
    float d[2];
    fp_dir(n, f, p, d);
    return (q[0] - pt_x(p)) * d[0] + (q[1] - pt_y(p)) * d[1];
}
/* vt+0x30 / vt+0x34: how far q lies right of the point */
static float fp_lat(const AiNet *n, const FPoint *f, Pt *p, const float *q)     /* 0x40d1b0 / 0x40d410 */
{
    float d[2];
    fp_dir(n, f, p, d);
    return (q[0] - pt_x(p)) * d[1] - (q[1] - pt_y(p)) * d[0];
}
/* vt+0x38: the 3-D point along / lateral from p on the road surface */
static void fp_pos3(const AiNet *n, const FPoint *f, Pt *p, float *o, float along, float lat)  /* 0x5dd360 / 0x5dd490 */
{
    float d[2], lv[3], av[3];
    fp_dir(n, f, p, d);
    lv[0] = d[1]; lv[1] = -d[0];
    av[0] = d[0]; av[1] = d[1];
    if (!f->back) { lv[2] = pt_camber(p); av[2] = pt_grade(p); }
    else { lv[2] = -pt_camber(p); av[2] = fp_grade(n, f, p); }
    float b[3] = { pt_x(p), pt_y(p), pt_z(p) };
    for (int k = 0; k < 3; k++) o[k] = b[k] + lv[k] * lat;              /* 0x43d0c0 */
    for (int k = 0; k < 3; k++) o[k] = o[k] + av[k] * along;
}
/* vt+0x40: length of the segment the travel takes from p; vt+0x44 the one
   it came over */
static float fp_seglen(const AiNet *n, const FPoint *f, Pt *p)          /* 0x40d110 / 0x40d200 */
{
    return f->back ? pt_prevseglen(n, p) : pt_seglen(p);
}
static float fp_prevseglen(const AiNet *n, const FPoint *f, Pt *p)
{
    return f->back ? pt_seglen(p) : pt_prevseglen(n, p);
}
/* vt+0x48: a lane's speed byte at p */
static float fp_lanespeed(const FPoint *f, Pt *p, int lane)             /* 0x40d130 / 0x40d450 */
{
    return pt_lanespeed(p, fp_lanemap(f, lane));
}
/* vt+0x14: path length from this point to the next */
static float fp_tonext(const AiNet *n, const FPoint *f)                 /* 0x40aff0 */
{
    if (f->next) return f->next->dist - f->dist;
    Pt *p = pt_at(n, f->sec, f->pt);
    return p ? fp_seglen(n, f, p) - f->off : 0;
}
/* vt+0x18: path length from the previous point to this one */
static float fp_fromprev(const AiNet *n, const FPoint *f)               /* 0x40b050 */
{
    if (f->prev) return fp_tonext(n, f->prev);
    Pt *p = pt_at(n, f->sec, f->pt);
    return p ? fp_prevseglen(n, f, p) + f->off : 0;
}
/* vt+0x4c: keep each lane's shift at least 1 m inside the road edges */
static void fp_clampshift(const AiNet *n, FPoint *f)                    /* 0x40b350 / 0x40b900 */
{
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) return;
    static const int LANES[5] = { 0, 2, 3, 4, 5 };
    for (int i = 0; i < 5; i++) {
        float a = f->back ? pt_wr(p) : pt_wl(p), b = f->back ? pt_wl(p) : pt_wr(p);
        float m = a < b ? b : a;
        float fr = pt_lanefrac(p, fp_lanemap(f, LANES[i]));
        float lo = 1.0f - (fr * m + a), hi = (b - fr * m) - 1.0f;      /* 0x783cd0 / 0x783cd4 */
        float s = f->shift[i];
        if (s <= lo) s = lo;
        if (hi < s) s = hi;
        f->shift[i] = s;
    }
}
/* 0x40ae30: where q lies along the path (in the list's distances) */
static float fp_pathpos(const AiNet *n, const FPoint *f, const float *q)
{
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) return 0;
    return (f->dist - f->off) + fp_along(n, f, p, q);
}
/* 0x4122c0: the point `along` metres along the path (list distance), `lat`
   metres right of lane's line (lane -1: lat from the centre line) */
static void fp_point(const AiNet *n, const FPoint *f, float *o, float along, float lat, int lane)
{
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) { o[0] = o[1] = o[2] = 0; return; }
    float a = along + f->off - f->dist, l = lat;
    if (lane != -1) l = pt_laneoff(p, fp_lanemap(f, lane)) + fp_shift(f, lane) + lat;
    fp_pos3(n, f, p, o, a, l);
}

/* ---- FuturePoint list (ctrl +0x370) -------------------------------------- */
#define FL_MAX 40
typedef struct {               /* junction turn record, 0x80 bytes at L +0xd4 */
    float a[2], b[2], c[2], d[2], e[2];   /* +0x00 .. +0x24 */
    float s[5];                /* +0x28 .. +0x38 path distances */
    float lanepos[4][2];       /* +0x3c .. +0x58 */
    float far;                 /* +0x5c where the turn starts along the old road */
    float vcorner;             /* +0x60 */
    float w[4];                /* +0x64 .. +0x70 (6.0) */
    signed char idx;           /* +0x74 */
    signed char ix[8];         /* +0x75 .. +0x7c */
    signed char turn;          /* +0x7d */
} Junc;
struct FList {
    int    n;                  /* +0x04 */
    FPoint *p[FL_MAX];         /* +0x08 */
    void  *xa8;                /* +0xa8 */
    char   xac;                /* +0xac */
    signed char cur, cur2;     /* +0xad +0xae */
    char   rebuild;            /* +0xaf */
    float  lanemin[5];         /* +0xb0 .. +0xc0 */
    int    sector_at;          /* +0xc4 */
    char   bit21;              /* +0xc8 */
    int    kind9_at;           /* +0xcc */
    int    nj;                 /* +0xd0 */
    Junc   j[5];               /* +0xd4 */
    float  jstart;             /* +0x354 */
};

static void fl_reset(FList *L)                                          /* 0x411be0 */
{
    L->n = 0;
    L->cur = L->cur2 = -1;
    L->nj = 0;
    L->jstart = -99999.0f;
    for (int i = 0; i < FL_MAX; i++) if (L->p[i]) { fp_free(L->p[i]); L->p[i] = NULL; }
    L->sector_at = -1; L->kind9_at = -1; L->bit21 = 0;
    for (int i = 0; i < 5; i++) L->lanemin[i] = 99999.0f;
}
static int fl_append(FList *L, FPoint *f)                               /* 0x40a5d0 */
{
    if (L->n >= FL_MAX) { fp_free(f); return 0; }
    L->p[L->n] = f;
    f->list = L;
    if (L->n > 0) { f->prev = L->p[L->n - 1]; L->p[L->n - 1]->next = f; }
    L->n++;
    return 1;
}
/* 0x418a50: first point of a fresh list at a route point */
static void fl_start(FList *L, int sec, int idx, const float *shift0, const float *shifts,
                     float dist, float off, Lk *link, int back)
{
    if (L->n >= FL_MAX) return;
    FPoint *f = fp_new(back);
    f->off = off; f->dist = dist; f->link = link;
    f->sec = (short)sec; f->pt = (short)idx;
    f->shift[0] = *shift0;
    for (int i = 0; i < 4; i++) f->shift[1 + i] = shifts[i];
    fl_append(L, f);
}
/* 0x4198c0: the list point whose lane line passes closest to q (plus 10 x
   the height difference), searched from index `from` in steps that grow
   with the distance; then, flag 1: moved on while it lies less than `slack`
   ahead of q; flag 2: moved back while it lies more than `slack` ahead.
   -1 when that runs off the list. */
static int fl_closest(const AiNet *n, const FList *L, const float *q, int flags, int lane, int from, float slack)
{
    int best = -1;
    float bestd = 0;
    if (from < 1) from = 0;
    for (int i = from; i < L->n; ) {
        const FPoint *f = L->p[i];
        float lp[3];
        fp_point(n, f, lp, f->dist, 0, lane);
        float dx = q[0] - lp[0], dy = q[1] - lp[1];
        float d = sqrtf(dx * dx + dy * dy);
        Pt *p = pt_at(n, f->sec, f->pt);
        float dz = fabsf((p ? pt_z(p) : 0) - q[2]);
        float c = 0 < d ? d : 0;
        if (99999.0f < c) c = 99999.0f;
        c = 10.0f * dz + c;                                             /* 0x783cc4 */
        if (c < bestd || best == -1) { bestd = c; best = i; }
        int st = -2 - (int)(-0.02f * (0 < d ? (d < 99999.0f ? d : 99999.0f) : 0));   /* 0x7852ac */
        if (st < 1) st = 0;
        i = i + st + 1;
    }
    if (best == -1) return -1;
    for (;;) {
        const FPoint *f = L->p[best];
        float pos = 0;
        Pt *p = pt_at(n, f->sec, f->pt);
        if (p) pos = fp_along(n, f, p, q) + (f->dist - f->off);
        float ahead = f->dist - pos;
        if ((flags & 1) && ahead < slack) best++;
        else if ((flags & 2) && ahead > slack) best--;
        else return best;
        int hi = L->n - 1, c = best < 1 ? 0 : best;
        if (c > hi) c = hi;
        if (best != c) return -1;
        if (best == -1) return -1;
    }
}

/* 0x579f20: a running average over the last n values */
#define RING_MAX 16
typedef struct { unsigned char n, count, idx; float sum, avg; float d[RING_MAX]; } Ring;
static void ring_init(Ring *r, int n) { memset(r, 0, sizeof *r); r->n = (unsigned char)n; }
static void ring_push(Ring *r, float v)                                 /* 0x576970 */
{
    if (r->count < r->n) r->count++;
    r->sum = v + r->sum;
    r->sum = r->sum - r->d[r->idx];
    r->d[r->idx] = v;
    r->idx++;
    r->avg = r->sum / (float)r->count;
    if (r->n <= r->idx) r->idx = 0;
}
static void ring_fill(Ring *r, float v)                                 /* 0x5769d0 */
{
    for (int i = 0; i < r->n; i++) r->d[i] = v;
    r->count = 0;
    r->sum = (float)r->n * v;
}

/* ---- the racer and its world --------------------------------------------- */
/* what the AI reads of a car (car +0x40 is its car state) */
typedef struct {
    float pos[3];              /* car +0x60: rigid body position */
    float vel[3];              /* +0x70 */
    float fwd[3], left[3], up[3]; /* +0x90 +0xa0 +0xb0 */
    unsigned short heading;    /* +0x40c */
    float speed, absv;         /* +0x42c forward speed, +0x430 |v| */
    int   wheels;              /* +0x424 */
    unsigned char contact[4];  /* +0x390 */
    float acc[3];              /* +0x80 acceleration (car state +0x40) */
    int   mover;               /* +0x28: 1 full simulation, 3 Mellovator */
    int   ctrl_type;           /* +0x24: 3 AI racer */
    float rear_frac;           /* physics P +0x1c0: rear share of the drive */
    float steer_gain;          /* physics P +0x270 */
    float slip[4][2];          /* car state +0x374: each wheel's slip pair */
    int   nos_level;           /* 0x59fa30: nitrous stage burning, 0 none */
} AiCarView;

/* a contact the car is in (the collision pair list 0x86b490, record
   +8 first / +0xc last touch, +0x30 the point) */
typedef struct {
    int   other;               /* who (the caller's id) */
    int   car;                 /* the other side is a car (0x592c60) */
    int   wall;                /* 0x5a0160: the other side a TrackPolygon (type 3) with |normal z| < 0.1 */
    float j;                   /* pair +0x60: the sum of the impulses' sizes (0x5a9620, t m/s) */
    float start, last;         /* seconds */
    float pt[3];
    float other_absv;          /* the other car's |v| */
} AiContact;

/* one entry of the racers-around table, 0x30 bytes at DriveInfo +8 */
typedef struct {
    float rv[2];               /* +0x00 velocity relative to the car's: across, along the path */
    float ext[2];              /* +0x08 half extents across / along */
    float pathpos;             /* +0x10 position along the car's path */
    float lat[5];              /* +0x14 lateral off lane 0, +0x18.. lanes 2..5 */
    int   idx;                 /* +0x28 nearest list point */
    short car;                 /* +0x2c */
    int   self;
} RacerEnt;

typedef struct AiRacer AiRacer;
typedef struct AiWorld AiWorld;

/* the car's performance (PerfConfig): class 0..3 from the rating, and the
   acceleration curve the planner reads (0x5b5380, 64 points over 0..vmax) */
typedef struct { int klass; float vmax; float acc[64]; } AiPerf;

/* the driving plan (0x1cc bytes): up to 20 waypoints on the lane line, an
   action builds it (action +0x20), the driver keeps a copy (driver +0x10) */
#define PLAN_MAX 20
typedef struct {
    signed char n;             /* +0x00 */
    char  near;                /* +0x01 the first point was taken close to the car */
    float p[PLAN_MAX][3];      /* +0x10, 16-byte stride */
    short sec[PLAN_MAX];       /* +0x150 */
    short pt[PLAN_MAX];        /* +0x178 */
    unsigned char lane[PLAN_MAX]; /* +0x1a0 */
    signed char cur;           /* +0x1b4 the next waypoint ahead */
    float cur_time;            /* +0x1b8 when cur was found */
    float x1bc;                /* +0x1bc */
    float time;                /* +0x1c0 */
    float len;                 /* +0x1c4 */
    char  x1c8;                /* +0x1c8 */
    char  valid;               /* +0x1c9 */
} AiPlan;

/* a lane state (0x10 bytes, ctrl +0x360): the lane (-1 none, 0 the racing
   line, 2..5 traffic lanes), the offset from its line the plan moves to,
   at `rate` metres across per metre (999: at once), and when it was set */
typedef struct { int lane; float off, rate, stamp; } AiLane;

/* an action (base 0x4234a0 / 0x40c960; vtable 0x785660). The goal "Win road
   race" (vtable 0x784768) picks one with its chooser 0x42d0f0 */
enum {
    ACT_PASS = 0, ACT_AVOID = 1, ACT_BLOCK = 4, ACT_JUSTDRIVE = 6, ACT_UNSTUCK = 9,
    ACT_HIT = 11, ACT_ACCIDENT = 13, ACT_STOP = 16, ACT_CHOOSE = 0x16,
    ACT_HITDRAG = 12, ACT_HITWALL = 15     /* IJustGotHitDragRace (vt 0x78588c), HitWallDragRace (vt 0x7858d4) */
};
/* the controller's goal (+0x6fc), by its 0x42aa20 number: 2 "Win drag race"
   (vt 0x7847d0), 3 "Steer player in drag race" (vt 0x784830) */
enum { GOAL_ROAD_RACE = 1, GOAL_DRAG_WIN = 2, GOAL_DRAG_PLAYER = 3, GOAL_TRAFFIC = 7, GOAL_PARK = 11 };
/* free roam's encounter racers (aiencounter.c): 5 ChaseCar, 6 GetAwayFromCar
   (the Outrun), 9 OffsetPursuit (vt 0x784bf8) and its action DriveAtOffset
   (type 0x13, vt 0x785a18) */
enum { GOAL_CHASE = 5, GOAL_GETAWAY = 6, GOAL_PURSUIT = 9 };
enum { ACT_DRIVEOFFSET = 0x13 };
static int goal_drag(int g) { return g == GOAL_DRAG_WIN || g == GOAL_DRAG_PLAYER; }
/* a traffic controller's actions (aitraffic.c): AvoidAvoidableAsTraffic,
   DriveTrafficLane, ObeyTrafficIntersections */
enum { TR_AVOID = 2, TR_DRIVE = 7, TR_OBEY = 17 };
typedef struct AiAction {
    int   type;                /* +0x204 */
    int   prio;                /* +0x200 */
    char  limited;             /* +0x04 has a time limit */
    float start, dur;          /* +0x08 / +0x0c */
    int   status;              /* +0x10: 1/2 done tests, 3 timed out */
    AiPlan plan;               /* +0x20 */
    float speed, x1f4, x1f8;   /* +0x1f0 / +0x1f4 / +0x1f8 for the driver */
    char  use_cap;             /* +0x1fc the goal's capped speed instead of +0x1f0 */
    char  go;                  /* +0x1fd */
    char  catchup;             /* +0x1fe the catch-up may act */
    char  nitrous;             /* +0x1ff start the nitrous */
    int   target;              /* +0x218 the other car (its index), -1 none */
    float t210, t214, t218, t21c, t220, t224, t228, t22c;   /* per action, see the ctors */
    char  flag;                /* GetUnstuck +0x218, AccidentRecovery +0x214 */
    int   isec;                /* ObeyTrafficIntersections +0x210: the intersection, -1 none */
    char  stopped;             /* ObeyTrafficIntersections +0x214 */
} AiAction;

struct AiRacer {
    AiWorld *w;
    int   index;               /* car +8 */
    int   human;               /* racer info +4 == 1 */
    int   skill;               /* controller vt+0x78 (0..9), -2 human */
    AiCarView car;
    AiPerf perf;
    int   suv;                 /* car record +0x88a */
    float halfwidth;           /* 0x60c1a0 */
    float progress;            /* car +0x8fc, distance along the race route */
    float acc_now;             /* 0x4038d0 */
    /* controller (0x420500 / 0x4260b0) */
    Pt   *rpt;                 /* +0xc current route point */
    int   rdir;                /* +0x10 travelling forward along it */
    uint32_t linkmem[16];      /* +0x14: bit0 valid, bit3 decision, bits 4-16 link */
    /* DriveInfo, +0x54 */
    int   nent;                /* +0x58 */
    RacerEnt ent[16];          /* +0x5c */
    signed char own;           /* +0x35c this car's entry */
    AiLane ls;                 /* +0x360 the lane state */
    FList L;                   /* +0x370 */
    short rsec;                /* +0x6ec */
    int   last_kind2_link;     /* +0x750 */
    float link_time;           /* +0x754 */
    int   owner_kind;          /* car +0x14 -> +4: 1 local human, 2 AI */
    int   in_race;             /* car +0x1c race entry present */
    char  force_replan;        /* ctrl +0x774 */
    AiAction *act;             /* +0x6f8 the current action */
    int   goal;                /* +0x6fc: GOAL_ROAD_RACE ("Win road race", 0x42c060) or GOAL_PARK (0x42b330) */
    char  car_player;          /* car +0x18: a human player's car (a finisher's car taken over, 0x5eba30(3)) */
    int   carno;               /* car +10: a human-owned car thinks when frame % 8 == it (0x42abd0) */
    AiPlan dplan;              /* driver +0x10 */
    AiContact contact[8]; int ncontact;   /* car +0x3c collidable, +0x18 count */
    struct AiDriver *drv;      /* ctrl +8 */
    int   slot;                /* place in the AI manager's think list (0x890080 +0x1c) */
    float think_time;          /* +0x778 last think */
    float think_left;          /* +0x6f0 */
    float rand_time, rand_off; /* +0x700 / +0x704 the skill's speed jitter */
    float catchup;             /* +0x770: the catch-up on the goal speed */
    float car_catchup;         /* car +0x468 = max(1, +0x770) */
    float catch_time;          /* +0x72c last computed */
    int   catch_target;        /* +0x748 the human it measures against (slot, -1) */
    char  allow_catchup;       /* +0x744 */
    float phys_time;           /* +0x734 full simulation for this long */
    float far_hold;            /* +0x730 no far mover for this long (2 s after a touch in it, 1 s after a jump ahead) */
    char  far;                 /* the car rides mover 4 (0x5f46a0(4) from the think) */
    char  far_repath;          /* 0x5918a0 asked: hand the plan to mover 4 */
    float drag260;             /* physics P +0x260 (0x4038d0's drag) */
    char  finished;            /* +0x710 */
    char  stop_why;            /* DIAG: why +0x710 was set (log only) */
    char  entry_done;          /* race entry +0x10: the race manager finished the car (0x5ee250) */
    char  at_end;              /* +0x77c */
    char  want_reset;          /* car +0x96c: put me back on the road */
    int   id;                  /* car +10: the racer id (AI 100 + i) */
    float unstuck_time;        /* +0x708 the last GetUnstuck */
    int   approve_car;         /* +0x798 the last car approved (vt+0x9c), -1 */
    float approve_time;        /* +0x79c */
    float nos_start, nos_stop; /* +0x7a8 / +0x7ac */
    float nos_straight;        /* +0x7b0 on a straight since, 0 not */
    char  nos_burn;            /* the car's nitrous burning (car +0x420 set) */
    float reset_time;          /* car +0x9c0 the last time it was put down, -1 */
    float horn_t, horn_on, horn_off, horn_on2;   /* car +0x92c +0x930 +0x938 +0x934 */
    char  horn_two, horn_flash, horn_x928;       /* car +0x926 +0x927 +0x928 */
    int   lap_seen;            /* +0x714 */
    int   block;               /* racer info +0x902: the race table's block level */
    float half_len;            /* the collision box's half length */
    float mass, aero, grip;    /* physics P +0x110, +0x264 + +0x268, (+0xdc + +0xfc) / 2 (0x40dee0) */
    float rpm, band_lo, band_hi;   /* car +0x440 and its gear's shift band (0x402590) */
    int   gear;                /* car +0x434 */
    float nos_frac, nos_cap;   /* the bottle (0x5925b0, seconds) */
    float race_dist;           /* race entry +0x14 */
    float race_rate;           /* race entry +0x18: laps a second, smoothed */
    float prog_next, stall_next;   /* +0x7a0 / +0x738 the 1.5 s samplers */
    Ring  prog5, prog10;       /* +0x7a4 / +0x73c progress along the route, m/s */
    /* a human player's controller (type 1, 0x4266d0): its path tracker and
       the player object's pace ring (0x602650) */
    float lat_prev, lat_rate;  /* +0x3d8 / +0x3dc the lateral offset and its rate */
    Pt   *rpt_prev;            /* +0x3e0 */
    Ring  pace;                /* player +0xa0: |v| over the line's speed, each second */
    float pace_timer, pace_max;/* player +0xa4 / +0xa8 */
    /* a traffic car's controller (TrafficController 0x426580, owner kind 3,
       aitraffic.c) */
    char  traffic;
    int   tidx;                /* its place in the AI manager's traffic list (0x890080 +0x1c) */
    float vcap;                /* +0x79c the speed cap (24.5865 m/s) */
    char  dead;                /* +0x77d switched off (0x4020d0) */
    int   tmover;              /* the mover it set (0x5f46a0): 0 none, 2 TrafficMover, 3 Mellovator */
    char  placed;              /* a new pose for the caller (0x42c670 / 0x42c950) */
    float place_pos[3], place_head, place_speed;
    float tele_time;           /* +0x7a4 */
    /* a drag race: the start delay (+0x75c, 0 unset) and the human's
       DragController (type 2, ctor 0x4299c0, vtable 0x785518, aidrag.c) */
    float start_delay;
    char  dctl;                /* the car's controller is a DragController */
    int   dl_cmd;              /* +0x7b4: 0 left held, 1 right held, 2 none */
    int   dl_lo, dl_hi;        /* +0x7b8 / +0x7bc the lanes left behind (0xff none) */
    char  dl_moved;            /* +0x7c0 */
    int   dl_x7c4;             /* +0x7c4 */
    int   dl_near, dl_got, dl_fsec, dl_fpt;   /* diagnostics: the last lane move (not in the original) */
    float dl_prog;             /* +0x7cc the progress when the move started */
    char  dl_edge[2];          /* +0x790 right / +0x791 left: the last press state */
    /* a free roam encounter racer (aiencounter.c) */
    char  off;                 /* Deactivated (0x5f6e20): out of the world */
    char  poof;                /* car +0x998 poofed; +0x974 / +0x978 its blink */
    float poof_t, poof_dur;
    char  hilite;              /* car FX +0x208c: the challenge highlight */
    /* the GPS list's controller (aigps): the solved path it follows (RealController +0x3f0) */
    const struct PfNode *gps;
    int   op_target;           /* OffsetPursuit +0x18: the target (seen slot) */
    float op_long, op_lat, op_until;   /* +0x1c / +0x20 / +0x24 */
    int   dam_mode;            /* DriveAtOffset +0x21c */
    int   ch_target;           /* ChaseCar / GetAwayFromCar: the other car (seen slot) */
};
/* the RandomEncounterManager (Race +0xc40): its state, the
   RandomEncounterState's (0x85ef70, res), three slots {spawn time, lane
   state, selected, car}; the Outrun's order by gap (seen slots) */
typedef struct {               /* a RacingCar (0x1f0) in the Outrun */
    int   car;                 /* +0 the car (its index) */
    float gap;                 /* +0x24 behind the leader */
    float t0;                  /* +0x30 the getaway's start */
    int   place, reason;       /* +0xc / +0x10 (6 won, 7 lost) */
    int   ftime;               /* +0x198 the time at the finish, 1/4000 s */
    char  human;
} AiRc;
typedef struct {
    int state, res;
    struct { float time; int state; char sel; AiRacer *car; } s[3];
    int n;
    AiRacer *cand;             /* player +0x250 */
    int order[8], norder;      /* the racing cars' indices by standing (Race +0xbd0) */
    char request;              /* Race +0xc30 RequestGetAway */
    char kmh;                  /* 0x83aa1a == 1 */
    AiRc rc[4]; int nrc, nfin; /* Race +0x30 / +0x24 / +0x28 */
    int srt[4];                /* rc by standing */
    float fin_t;               /* the human's finish (-1 none) */
    int result, nresult;       /* the last Outrun's end for the caller (6 / 7) */
} AiRem;

/* an ExtrapolatedState (list 0x8a2ca4): a car the AI can see */
typedef struct {
    const AiCarView *v;        /* +0xc position, +0x10 velocity, +0x18 forward */
    float half[2];             /* half length / width (0x5aacd0) */
    float acc;                 /* 0x60be30: acceleration along its velocity */
    float progress;            /* car +0x8fc */
    int   index;               /* +8 */
    int   active;              /* car +0x550 and not +0x998 */
    float race_dist;           /* race entry +0x14 */
    int   race_pos;            /* race entry +0xa */
    int   human;               /* car +0x18 */
    int   kind;                /* racer info +4: 1 human, 2 AI racer, 3 traffic */
    int   in_race;             /* car +0x1c */
    const AiPlan *plan;        /* its driver's plan (car +0x30 +0x10), NULL none */
    AiRacer *racer;
    int   wrong_way;           /* a human's player +0x248 */
} AiSeen;

struct AiWorld {
    const AiNet *net;
    AiSeen seen[24]; int nseen;
    int   circuit; float track_len, track_half;   /* 0x88f298 +0x24 / +0x18 / +0x1c */
    float time;                /* 0x86518c / 4000 */
    float race_time;           /* racemgr +0x14 / 4000 */
    int   drag, drift, streetx, x89010c;  /* 0x89e7d8 0x89e7d9 0x89e7e3 0x89010c */
    int   x890104, x890108, x890114;      /* race-type flags */
    int   mode;                /* 0x89e7b0 race mode (0 free roam, 7 outrun) */
    float shortcut_time;       /* race record (0x883dac) +0xe4: -1 none */
    const unsigned char *blob; /* GLOBALB 0x34b00 AI tuning (0x81cb60) */
    unsigned rng;
    int   race_state;          /* *0x890118: 3 = the countdown */
    float race_start;          /* 0x890118 +4 (seconds), -1 unset */
    int   difficulty;          /* 0x89e804 (race setup +0x7d) */
    int   race_active;         /* 0x5eeef0 */
    unsigned frame;            /* 0x7fb724 */
    int   nai;                 /* AI racers in the think list */
    int   event;               /* 0x89e7a0 */
    float dt;                  /* 0x865154 the frame */
    float time_prev;
    float cam[4][3]; int ncam; /* the views' eyes (0x5f1160) */
    AiCarView view[24];
    /* the catch-up's race setup (0x4172d0 / 0x4084a0) */
    int   catchup_opt, traffic_level, laps, sprint, humans;
    float catchup_weight;
    int   reversed;            /* 0x89e7a4: the race runs the route backward */
    /* the race's traffic (aitraffic.c) */
    AiRacer *tr[16]; int ntr;  /* the AI manager's traffic cars (0x890080 +0x18 .. +0x10) */
    const unsigned char *trec; /* the race's track record (0x883dac, 0x128 bytes) */
    float start_prog;          /* the start line's progress (race state +0xbf8[+0xbf6] % of the lap) */
    const unsigned char *(*zone)(void *user, const float pos[2]);   /* Paths zone of type 16 (0x5d9bf0) */
    void *zone_user;
    int   tdiff;               /* 0x89e7f4 */
    float tele_period;         /* TrafficTeleporter +0 */
    int   tele_rr;             /* +4 the round-robin index */
    char  tele_on;             /* +8 */
    int   tele_rot;            /* +0xc the search rotation */
    int   tele_points;         /* +0x14 the route points with forward lanes */
    float go_in;               /* 0x5ef2e0: seconds to GO (the countdown), negative after */
    AiRem rem;                 /* free roam's random encounters (aiencounter.c) */
};
/* 0x88f2be: a race route is loaded (route points' distances are race progress) */
static int world_race_route(const AiWorld *w) { return w->mode != 0 && w->mode != 7; }

/* 0x5ea500 -> 0x43c170: the game's random in [0, x) */
static float ai_rand(AiWorld *w, float x)
{
    unsigned v = w->rng % 0x7fffffffu, t = w->rng ^ 0x1d872b41u, u = t >> 5 ^ t;
    w->rng = u << 27 ^ u ^ t;
    return (float)(int)v * 4.656612873077393e-10f * x;
}
static const float *blob_f(const AiWorld *w, int off) { return (const float *)(w->blob + off); }

/* 0x5d4650 / 0x5d4600: the braking and cornering grip the planner assumes,
   by performance class (an SUV record has its own) */
static float car_brake_decel(const AiRacer *r)
{
    static const float N[4] = { 12.0f, 10.6f, 9.3f, 8.0f }, S[4] = { 10.0f, 9.0f, 8.0f, 6.0f };
    return r->suv ? S[r->perf.klass & 3] : N[r->perf.klass & 3];
}
static float car_corner_g(const AiRacer *r)
{
    static const float N[4] = { 3.7f, 3.4f, 3.1f, 2.8f }, S[4] = { 3.0f, 3.0f, 3.0f, 2.0f };
    return r->suv ? S[r->perf.klass & 3] : N[r->perf.klass & 3];
}
static float car_accel_at(const AiRacer *r, float v)                    /* 0x5b5380 + 0x5764d0 */
{
    const AiPerf *p = &r->perf;
    if (!(p->vmax > 0)) return 0;
    float t = v * (63.0f / p->vmax);
    int i = (int)t;
    if (i < 0 || t < 0) return p->acc[0];
    if (i >= 63) return p->acc[63];
    float f = t - (float)i;
    return (1.0f - f) * p->acc[i] + f * p->acc[i + 1];
}
/* 0x5d46a0: the speed a corner of curvature k allows, at most vmax */
static float corner_speed(float g, float k, float vmax)
{
    float m = (g * 9.8f) / (vmax * vmax);
    if (m <= fabsf(k)) m = fabsf(k);
    return sqrtf((g * 9.8f) / m);
}

/* ---- the controller's route choices -------------------------------------- */
/* ctrl vt+0x44: a link the car must always take -- an AI racer's goal
   (vt+0x2c, 0x5dcbe0(0)): kind 2; a human's controller (0x40cae0,
   0x5dcbe0(2)): kinds 2 and 8 */
static int traffic_take_link(AiRacer *r, Lk *l);
static int getaway_take_link(AiRacer *r, Lk *l);                       /* aiencounter.c */
static void traffic_lane_gone(AiRacer *r);
static void traffic_lanes(AiRacer *r);
static void traffic_tick(AiRacer *r, float dt);
static void traffic_goal_speed(AiRacer *r);
static void tr_exec(AiRacer *r, AiAction *a);
static int  tr_done(AiRacer *r, AiAction *a);
static int  tr_counts(AiRacer *r, int car);
/* aidrag.c */
static int  drag_take_link(AiRacer *r, Lk *l);
static int  drag_lane_steps(const AiRacer *r, const FPoint *f, int from, int to);
static void drag_why(const AiRacer *r, const char *fmt, ...);
static AiAction *drag_choose(AiRacer *r, int cur, int prio);
static int  drag_start(AiRacer *r);
static void drag_ctl_tick(AiRacer *r);
static void hitwall_begin(AiRacer *r, AiAction *a);
static void hitdrag_begin(AiRacer *r, AiAction *a);
static int  wall_contact(const AiRacer *r);
static int  goal_laneclass(const AiRacer *r);
static int  traffic_avoid(AiRacer *r, AiLane *ls, AiPlan *P, float horizon, int low_first);
static int link_always(const AiRacer *r, Lk *l)
{
    if (r->goal == GOAL_TRAFFIC) return (lk_flags(l) >> 4 & 0xf) == 2;   /* 0x40cb70: 0x5dcbe0(1) */
    int k = lk_flags(l) & 0xf;
    if (r->goal == GOAL_DRAG_PLAYER) return k == 2 || k == 8;           /* 0x5dcbe0(2) */
    return k == 2 || (r->human && k == 8);
}
/* goal vt+0x28, 0x413cc0: does the racer take this branch? (Park,
   0x41bda0 = 0x5dcbe0(0): only a link it must take, kind 2) */
static int goal_take_link(AiRacer *r, Lk *l)
{
    AiWorld *w = r->w;
    uint32_t f = lk_flags(l);
    int kind = f & 0xf;
    if (r->goal == GOAL_PARK || r->goal == GOAL_PURSUIT || r->goal == GOAL_CHASE) return kind == 2;   /* 0x41bda0 */
    if (r->goal == GOAL_GETAWAY) return getaway_take_link(r, l);       /* 0x41bd00 -> 0x413f50 */
    if (r->goal == GOAL_TRAFFIC) return traffic_take_link(r, l);       /* 0x41bd60 -> 0x413bb0 */
    if (r->goal == GOAL_DRAG_PLAYER) return drag_take_link(r, l);      /* 0x414070 */
    const unsigned char *ts = lk_sec(w->net, l);
    if (((kind == 2 || kind == 8) && r->human) || kind == 2) return 1;
    if (ts && (rd32(ts + 0x70) >> 10 & 1)) return 0;
    if (kind == 5 || kind == 10 || kind == 7 || (f >> 20 & 1)) return 0;
    if (w->shortcut_time == -1.0f) {
        /* 0x5eeff0(car): TODO -- not decoded yet; the original returns
           "no" here when it holds */
    } else if (!(w->shortcut_time < w->race_time)) return 0;
    if (kind != 4) {
        int k = r->skill < 1 ? 0 : r->skill > 9 ? 9 : r->skill;
        float x = ai_rand(w, 1.0f);
        const float *rec = blob_f(w, 0x524 + k * 0x68);
        if (x < rec[0]) return kind == 1;
        if (!(x < rec[1]) && x < rec[2]) return 0;
        return kind == 0;
    }
    /* kind 4: 0x4018f0 (controller flag) -> 0x40da10, else a coin */
    float p = (f & 0x8000000) ? 0.333f : 0.5f;
    return ai_rand(w, 1.0f) < p;
}
/* goal vt+0x30, 0x41bd20: is a branch decision remembered? (Park,
   0x41bdd0: kind 9 too) */
static int goal_remember_link(const AiRacer *r, Lk *l)
{
    int k = lk_flags(l) & 0xf;
    if (r->goal == GOAL_PARK || r->goal == GOAL_PURSUIT || r->goal == GOAL_CHASE)
        return !(k == 7 || k == 5 || k == 10 || k == 2);
    if (r->goal == GOAL_DRAG_PLAYER) return !(k == 4 || k == 7 || k == 5 || k == 10 || k == 2 || k == 9);   /* 0x41bcb0 */
    if (r->goal == GOAL_TRAFFIC) {                                      /* 0x41bd70: by nibble 1 */
        int k1 = lk_flags(l) >> 4 & 0xf;
        return !(k1 == 7 || k1 == 5 || k1 == 10 || k1 == 2);
    }
    return !(k == 7 || k == 5 || k == 10 || k == 2 || k == 9);
}
/* PathfindingNode (0xc0 bytes, pool 0x81cbb8; aigps below) */
typedef struct PfNode {
    Pt   *start, *cur;         /* +0 / +4 the start and current route points */
    float g, h;                /* +8 metres travelled, +0xc 2-D distance left (99999 dead, 0 at the goal) */
    float target[3];           /* +0x10 */
    struct { Pt *pt; unsigned char dir; unsigned short dec; } succ[3];   /* +0x20, 8 bytes each (+0x26 link, 0xffff none) */
    unsigned short dec[64];    /* +0x38 signpost decisions: bits 0-12 link, 13 taken */
    signed char nsucc, ndec;   /* +0xb8 / +0xb9 */
    char  atgoal;              /* +0xba */
    char  startdir, dir;       /* +0xbb / +0xbc: 1 along the points' order */
} PfNode;
/* vt+0x64, a human's 0x418960 GetSignpostDecisionFromPathfinding: Always
   taken, None / Don't take / Never not, else the GPS path's decision for
   the link (none: not taken) */
static int gps_decision(const AiRacer *r, Lk *l)
{
    int k = lk_flags(l) & 0xf;
    if (k == 2) return 1;
    if (k == 7 || k == 5 || k == 10 || !r->gps) return 0;
    unsigned li = (unsigned)lk_index(r->w->net, l) & 0xffff;
    for (int i = 0; i < r->gps->ndec; i++)
        if ((unsigned)(r->gps->dec[i] & 0x1fff) == li) return r->gps->dec[i] >> 13 & 1;
    return 0;
}
/* 0x41cfa0: the controller decides a branch, remembering the answer per link
   (16 slots at +0x14) while the goal wants it remembered */
static int ctrl_choose_link(AiRacer *r, Lk *l, int dir, int traffic)
{
    const AiNet *n = r->w->net;
    uint32_t f = lk_flags(l);
    unsigned li = (unsigned)lk_index(n, l);
    if ((dir == 0) != (int)(f >> 18 & 1) || (f & 0xf) == 3) return 0;
    if (traffic) return r->human ? gps_decision(r, l) : 0;   /* vt+0x64: a racer 0x800428ac no; a human 0x418960 */
    if (r->human) {
        /* the human's controller: every decision remembered (vt+0x5c
           0x403210), taken when always or of kind 0 (vt+0x54 0x418910) */
        for (int i = 0; i < 16; i++)
            if ((r->linkmem[i] & 1) && (li & 0xffff) == (r->linkmem[i] >> 4 & 0x1fff))
                return r->linkmem[i] >> 3 & 1;
        int k = f & 0xf;
        int take = link_always(r, l) || (k != 5 && k != 10 && k != 7 && k == 0);
        int slot = -1;
        for (int i = 0; i < 16; i++) if (!(r->linkmem[i] & 1)) { slot = i; break; }
        if (slot == -1) slot = 0;
        r->linkmem[slot] = ((li & 0x1fff) << 1 | (unsigned)(take & 1)) << 3 | (r->linkmem[slot] & 0xfffe0003u) | 3;
        return take;
    }
    int mem = goal_remember_link(r, l);
    if (mem)
        for (int i = 0; i < 16; i++)
            if ((r->linkmem[i] & 1) && (li & 0xffff) == (r->linkmem[i] >> 4 & 0x1fff))
                return r->linkmem[i] >> 3 & 1;
    int take = goal_take_link(r, l);
    if (take && (f >> 16 & 3) == 2 && !r->traffic) r->last_kind2_link = (int)(li & 0xffff);   /* vt+0x58 0x410580 (0x7e95c4) */
    if (mem) {
        int slot = -1;
        for (int i = 0; i < 16; i++) if (!(r->linkmem[i] & 1)) { slot = i; break; }
        if (slot == -1) slot = 0;                 /* full: the original resets the planner */
        r->linkmem[slot] = ((li & 0x1fff) << 1 | (unsigned)(take & 1)) << 3 | (r->linkmem[slot] & 0xfffe0003u) | 3;
    }
    return take;
}

/* 0x41ec20: one step along the route from *pt: take a link if one is open
   and chosen, else the next point in the direction of travel. */
static int route_step(AiRacer *r, FList *L, const unsigned char **sec, Pt **pt, int *idx, Lk **link, int *dir)
{
    const AiNet *n = r->w->net;
    int nl = pt_nlinks(n, *pt);
    for (int k = 0; k < nl; k++) {
        Lk *l = pt_link(n, *pt, k);
        uint32_t f = lk_flags(l);
#ifdef AIR_TRACE
        printf("    at sec %d pt %d dir %d: link %d kind %u bit18 %u bit26 %u%c", rdu16(*sec + 10), *idx, *dir, lk_index(n, l), f & 0xf, f >> 18 & 1, f >> 26 & 1, 10);
#endif
        if ((f & 0xf) == 3 && (int)(f >> 18 & 1) != *dir) { *link = NULL; return 0; }
        FPoint *last = L->n < 1 ? NULL : L->p[L->n - 1];
        if ((int)(f >> 18 & 1) != *dir &&
            (link_always(r, l) ||                                     /* ctrl vt+0x44 */
             ((!(f >> 26 & 1) || L->xac || (last && 30.0f < last->dist - L->jstart)) &&
              ctrl_choose_link(r, l, *dir, L->xac)))) {
            *sec = lk_sec(n, l);
            *pt = lk_pt(n, l);
            *idx = (int)((*pt - *sec) - 0x8c) / 0x38;
            *dir ^= (int)(f >> 23 & 1);
            *link = l;
#ifdef AIR_TRACE
            printf("    link %d kind %u flags %08x -> sec %d pt %d dir %d%c", lk_index(n, l), f & 0xf, f, rdu16(*sec + 10), *idx, *dir, 10);
#endif
            if (!(f >> 26 & 1)) return 1;
            L->jstart = last ? last->dist : 0;
            return 1;
        }
    }
    *link = NULL;
    int ok;
    if (*dir == 0) { *idx -= 1; ok = *idx >= 0; }
    else { *idx += 1; ok = *idx < sec_npts(*sec); }
    if (!ok) return 0;
    *pt = sec_pt(*sec, *idx);
    return 1;
}

static int racer_lane(const AiRacer *r);   /* ctrl vt+0x1c (0x402170): the lane state's lane */

/* 0x418c50: a new point at a road end with no sector (flags 19-24 all set,
   no first link) on another section is only kept when it lies more than
   5 m (10 in a drag race) past the last point along the lane. */
static int fl_validate(const AiNet *n, AiRacer *r, const FList *L, const FPoint *f)
{
    if (L->n < 1) return 1;
    Pt *p = pt_at(n, f->sec, f->pt);
    const FPoint *last = L->p[L->n - 1];
    if (p && rdu16(p + 0x1c) == 0xffff && (pt_flags(p) & 0x1f80000) == 0x1f80000 && f->sec != last->sec) {
        int lane = racer_lane(r);
        if (lane < 1) lane = 0;
        float q[3];
        fp_point(n, f, q, f->dist, 0, lane);
        float gap = fp_pathpos(n, last, q) - last->dist;
        if (gap <= (r->w->drag ? 10.0f : 5.0f)) return 0;               /* 0x783c6c / 0x783c70 */
    }
    return 1;
}
/* 0x418da0: a stretch longer than 16 m up to the new point gets points
   every 8 m on the last point's segment, flagged +0x74 */
static void fl_interp(FList *L, const FPoint *f)
{
    if (L->n < 1) return;
    const FPoint *last = L->p[L->n - 1];
    int k = (int)((f->dist - last->dist) / 8.0f) - 1;                   /* 0x783c68 */
    short sec = last->sec, pt = last->pt;
    float d0 = last->dist, o0 = last->off;
    int back = last->back;
    float a = 0;
    for (int i = 0; i < k; i++) {
        if (L->n >= FL_MAX) return;
        a += 8.0f;
        FPoint *g = fp_new(back);
        g->sec = sec; g->pt = pt;
        g->off = a + o0; g->dist = d0 + a;
        fl_append(L, g);
        L->p[L->n - 1]->interp = 1;
    }
}

/* 0x41ee20: a junction crossing (a bit-26 link). The turn is cut where the
   new road's nearer edge lies along the old one plus the tightest lane
   radius (each lane's distance to the inner edge / sqrt 2 - 1.2, at most
   sqrt(12.5)); old points past that go, the old road is filled in 3 m steps
   up to it, the new point is pushed along its road until it is clear of the
   corner, and the turn is recorded (5 at most). */
static void fl_junction(AiRacer *r, FList *L, FPoint *nw)
{
    const AiNet *n = r->w->net;
    FPoint *last = L->p[L->n - 1];
    float A[3], B[3], C[3], D[3];
    fp_point(n, last, A, last->dist, fp_left(n, last, 2), 2);
    fp_point(n, last, B, last->dist, fp_right(n, last, 2), 2);
    fp_point(n, nw, C, nw->dist, fp_left(n, nw, 2), 2);
    fp_point(n, nw, D, nw->dist, fp_right(n, nw, 2), 2);
    float pA = fp_pathpos(n, nw, A), pB = fp_pathpos(n, nw, B);
    float pC = fp_pathpos(n, last, C), pD = fp_pathpos(n, last, D);
    float far = pC >= pD ? pD : pC;
    float r0 = sqrtf(5.0f * 5.0f * 0.5f), minr = 99999.0f;               /* 0x783c80 */
    if (!(99999.0f < r0)) minr = r0;
    float arr[4];
    for (int k = 0; k < 4; k++) {
        float e = pA <= pB ? fp_right(n, last, 2 + k) : fp_left(n, last, 2 + k);
        arr[k] = sqrtf(fabsf(e) * fabsf(e) * 0.5f) + -1.2f;              /* 0x783c84 */
        if (!(minr < arr[k])) minr = arr[k];
    }
    float sturn = minr + far;
    while (sturn - 3.0f < last->dist) {                                 /* 0x783c88 */
        FPoint *pv = last->prev;
        if (pv) pv->next = NULL;
        fp_free(last);
        L->p[--L->n] = NULL;
        last = pv;
        if (!last) { fl_append(L, nw); return; }
    }
    while (sturn - 6.0f > last->dist && L->n < FL_MAX) {
        float s0 = last->shift[0], s4[4] = { last->shift[1], last->shift[2], last->shift[3], last->shift[4] };
        fl_start(L, last->sec, last->pt, &s0, s4, last->dist + 3.0f, last->off + 3.0f, NULL, last->back);
        last = L->p[L->n - 1];
        if (!last) break;
    }
    int side = pA > pB;
    float sentry = r0 + far, E0[3], P[4][3];
    fp_point(n, last, E0, sentry, side ? fp_left(n, last, 0) + r0 : fp_right(n, last, 0) - r0, 0);
    for (int k = 0; k < 4; k++)
        fp_point(n, last, P[k], far + arr[k], side ? fp_left(n, last, 2 + k) + arr[k] : fp_right(n, last, 2 + k) - arr[k], 2 + k);
    const float *ref = E0;
    if (!side) {
        int lane = -1;                                                  /* 0x40b0b0: outermost lane the road has */
        const unsigned char *s = n->sec[nw->sec];
        if (s) for (int l = 5; l >= 2 && lane == -1; l--) if (s[0x82 + fp_lanemap(nw, l)]) lane = l;
        if (lane != -1) ref = P[lane - 2];
    }
    Pt *p = pt_at(n, nw->sec, nw->pt);
    float x = (p ? fp_along(n, nw, p, ref) + (nw->dist - nw->off) : 0) - nw->dist + 3.0f;
    while (p && fp_seglen(n, nw, p) < x) {
        const unsigned char *sec = n->sec[nw->sec];
        Pt *q = p;
        int idx = pt_index(n, q), dir = !nw->back;
        Lk *lk = NULL;
        if (!route_step(r, L, &sec, &q, &idx, &lk, &dir)) break;
        if (dir != !nw->back) { int pdir = dir; fp_free(nw); nw = fp_new(pdir == 0); }
        nw->dist = nw->off = 0;
        nw->sec = (short)rdu16(sec + 10); nw->pt = (short)pt_index(n, q);
        nw->link = lk;
        p = pt_at(n, nw->sec, nw->pt);
        x = (p ? fp_along(n, nw, p, ref) + (nw->dist - nw->off) : 0) - nw->dist + 3.0f;
    }
    nw->off = x;
    nw->dist = 3.0f + sturn;
    fl_append(L, nw);
    if (L->nj < 5) {
        Junc *j = &L->j[L->nj];
        j->idx = (signed char)(L->n - 2);
        j->far = far;
        j->turn = (signed char)side;
        j->c[0] = E0[0]; j->c[1] = E0[1];
        j->s[2] = sentry;
        for (int k = 0; k < 4; k++) { j->lanepos[k][0] = P[k][0]; j->lanepos[k][1] = P[k][1]; }
        L->nj++;
    }
    /* else 0x41a090, which computes and discards */
}

/* 0x422220: extend the list along the route until it reaches `upto` metres
   (and has 10 points), at most 40 points and 100 steps */
static void fl_extend(AiRacer *r, FList *L, float upto)
{
    const AiNet *n = r->w->net;
    FPoint *f = L->p[L->n - 1];
    const unsigned char *sec = (f->sec >= 0 && f->pt != -1) ? n->sec[f->sec] : NULL;
    Pt *pt = pt_at(n, f->sec, f->pt), *prevpt = pt;
    int dir = !f->back, idx = f->pt;
    float total = f->dist, minend = 0;
    float sh[5];
    memcpy(sh, f->shift, sizeof sh);
    Lk *link = NULL;
    for (int steps = 0; ; ) {
        if ((total > upto && L->n > 9 && minend <= total) || L->n > 39) return;
        int wasback = dir == 0;
        if (!route_step(r, L, &sec, &pt, &idx, &link, &dir)) return;
        float seg;
        if (!link) {
            if (L->n < 1) {
                if (wasback) { Pt *q = pt_prev(n, prevpt); seg = pt_seglen(q ? q : prevpt); }
                else seg = pt_seglen(prevpt);
            } else {
                FPoint *l2 = L->p[L->n - 1];
                seg = fp_pathpos(n, l2, (const float *)pt) - l2->dist;
            }
            const unsigned char *ps = sec_of(n, pt);
            int bk = dir == 0;
            if (ps && (ps[0x74 + bk * 8] || ps[0x75 + bk * 8])) {        /* 0x40aa20: lane 0 shift fades */
                float rate = (r->w->streetx || r->w->x89010c) ? 0.2f : 0.04f;   /* 0x783cc0 / 0x783cbc */
                if (sh[0] <= 0) { sh[0] = rate * seg + sh[0]; if (!(sh[0] < 0)) sh[0] = 0; }
                else { sh[0] = sh[0] - rate * seg; if (!(0 < sh[0])) sh[0] = 0; }
            }
        } else if (!(lk_flags(link) >> 26 & 1)) {
            float d[2];
            if (!wasback) { d[0] = pt_dx(prevpt); d[1] = pt_dy(prevpt); } else pt_backdir(n, prevpt, d);   /* 0x40cbc0 */
            seg = (pt_x(pt) - pt_x(prevpt)) * d[0] + (pt_y(pt) - pt_y(prevpt)) * d[1];
            if (!r->w->drag) {
                while (seg < 3.0f) {                                    /* 0x783c7c */
                    Lk *l2;
                    if (!route_step(r, L, &sec, &pt, &idx, &l2, &dir)) return;
                    seg = (pt_x(pt) - pt_x(prevpt)) * d[0] + (pt_y(pt) - pt_y(prevpt)) * d[1];
                    if (++steps > 100) return;
                }
            } else {
                while (seg < 3.0f && L->n >= 1) {
                    if (L->n < 2) { seg = 0; total = 0; }
                    else {
                        float g = L->p[L->n - 1]->dist - L->p[L->n - 2]->dist;
                        seg += g; total -= g;
                    }
                    fp_free(L->p[L->n - 1]);
                    L->p[--L->n] = NULL;
                    if (L->n > 0) L->p[L->n - 1]->next = NULL;
                }
            }
        } else {
            for (int i = 0; i < 5; i++) sh[i] = 0;
            seg = v2len(pt_x(prevpt) - pt_x(pt), pt_y(prevpt) - pt_y(pt));
        }
        FPoint *g = fp_new(dir == 0);
        g->sec = (short)rdu16(sec + 10);
        g->pt = (short)pt_index(n, pt);
        g->link = link;
        g->dist = seg + total;
        memcpy(g->shift, sh, sizeof sh);
        g->off = 0;
        fp_clampshift(n, g);
        if (!link || !(lk_flags(link) >> 26 & 1)) {
            if (!fl_validate(n, r, L, g)) fp_free(g);
            else {
                fl_interp(L, g);
                fl_append(L, g);
                total = seg + total;
            }
        } else {
            fl_junction(r, L, g);
            FPoint *e = L->p[L->n - 1];
            sec = (e->sec >= 0 && e->pt != -1) ? n->sec[e->sec] : NULL;
            pt = pt_at(n, e->sec, e->pt);
            idx = e->pt;
            dir = !e->back;
            total = seg + e->dist;
            minend = total;
        }
        if (++steps > 100) return;
        prevpt = pt;
    }
}

/* 0x419670: the first k points are passed: drop them, re-base distances
   and the junction records on the new first point */
static void fl_flags(const AiNet *n, FList *L);
static void fl_dropfront(const AiNet *n, FList *L, int k)
{
    if (k >= L->n) return;
    float d0 = L->p[k]->dist;
    for (int i = 0; i < k; i++) fp_free(L->p[i]);
    for (int i = 0; i < L->n - k; i++) { L->p[i] = L->p[i + k]; L->p[i]->dist -= d0; }
    L->n -= k;
    for (int i = 0; i < k; i++) L->p[L->n + i] = NULL;
    if (!L->p[0]) { L->cur = L->cur2 = -1; L->nj = 0; }
    else {
        L->p[0]->prev = NULL;
        L->jstart -= d0;
        int c = L->cur - k; L->cur = (signed char)(c < 1 ? 0 : c);
        c = L->cur2 - k; L->cur2 = (signed char)(c < 1 ? 0 : c);
        if (L->jstart < 0) L->jstart = -99999.0f;
        for (int j = 0; j < L->nj; j++) {
            Junc *J = &L->j[j];
            J->idx = (signed char)(J->idx - k);
            J->far -= d0;
            for (int i = 0; i < 5; i++) J->s[i] -= d0;
            for (int i = 0; i < 4; i++) {
                int a = J->ix[i] - k, b = J->ix[4 + i] - k, hi = L->n - 1;
                if (a < 1) a = 0;
                if (a > hi) a = hi;
                if (b < 1) b = 0;
                if (b > hi) b = hi;
                J->ix[i] = (signed char)a; J->ix[4 + i] = (signed char)b;
            }
        }
        for (int j = 0; j < L->nj; ) {
            if (L->j[j].idx < 0) {
                for (int m = j; m < L->nj - 1; m++) L->j[m] = L->j[m + 1];
                L->nj--;
            } else j++;
        }
    }
    fl_flags(n, L);
}
/* 0x40a620: points at the end with no length to their next go */
static void fl_trim(FList *L)
{
    for (int i = L->n - 1; i >= 0; i--) {
        FPoint *f = L->p[i];
        if (fabsf(f->off) < 0.001f) break;
        fp_free(f);
        L->p[i] = NULL;
        if (i > 0) L->p[i - 1]->next = NULL;
        L->n--;
    }
    for (int j = 0; j < L->nj; ) {
        if (L->j[j].idx < L->n) j++;
        else {
            for (int m = j; m < L->nj - 1; m++) L->j[m] = L->j[m + 1];
            L->nj--;
        }
    }
}
/* 0x40a6e0: speeds limited from point `from` on (fwd: by the car's
   acceleration from the previous point; back: by its braking towards the
   next), lane speeds by 0.2 m/s per metre */
static void fl_propagate(const AiRacer *r, FList *L, int from, int fwd)
{
    int ref = fwd ? from - 1 : from + 1, hi = L->n - 1;
    if (ref < 1) ref = 0;
    if (ref > hi) ref = hi;
    float a = car_brake_decel(r);
    for (int i = from; i >= 0 && i < L->n; i += fwd ? 1 : -1) {
        FPoint *c = L->p[i], *q = L->p[ref];
        float vr = q->speed[0], d = c->dist - q->dist;
        float k = fwd ? car_accel_at(r, vr) : -a;
        float v = sqrtf(k * d + k * d + vr * vr);
        if (c->speed[0] < v) v = c->speed[0];
        c->speed[0] = v;
        float rate = fwd ? 0.2f : -0.2f;                                /* 0x784554 / 0x7846ec */
        for (int s = 1; s < 5; s++) {
            float lv = rate * d + q->speed[s];
            if (c->speed[s] < lv) lv = c->speed[s];
            c->speed[s] = lv;
        }
        ref = i;
    }
}
/* 0x40a8c0: new points take their lane speeds from the route, then the
   acceleration pass */
static void fl_speeds(const AiNet *n, const AiRacer *r, FList *L)
{
    int first = L->n;
    for (int i = 0; i < L->n; i++)
        if (fabsf(L->p[i]->speed[0] - -1.0f) < 0.001f) { first = i; break; }
    for (int i = first; i < L->n; i++) {
        FPoint *f = L->p[i];
        Pt *p = pt_at(n, f->sec, f->pt);
        if (!p) continue;
        f->speed[0] = fp_lanespeed(f, p, 0);
        for (int s = 0; s < 4; s++) f->speed[1 + s] = fp_lanespeed(f, p, 2 + s);
    }
    fl_propagate(r, L, first, 1);
}
/* 0x5d8920 / 0x410bf0: is the point in a sector both of whose sections
   are flagged (+0x70 bit 6)? */
static int pt_insector(const AiNet *n, Pt *p)
{
    unsigned s = pt_flags(p) >> 19 & 0x3f;
    if (s == 0x3f || (int)s >= n->nsectors) return 0;
    const unsigned char *sec = n->sectors + s * 0x114;
    for (int k = 0; k < 2; k++) {
        int id = rds16(sec + 4 + 2 * k);
        if (id < 0 || id >= SEC_MAX || !n->sec[id] || !(rd32(n->sec[id] + 0x70) >> 6 & 1)) return 0;
    }
    return 1;
}
/* 0x411ce0: where the racing line / each lane stops (the section has none
   for this direction): the distance of the point before */
static void fl_lanecut(const AiNet *n, FList *L, const FPoint *f)
{
    const unsigned char *s = (f->sec >= 0 && f->pt != -1) ? n->sec[f->sec] : NULL;
    if (!s) return;
    float d = f->prev ? f->prev->dist : 0;
    int bk = f->back ? 1 : 0;
    if (!s[0x74 + bk * 8] && !s[0x75 + bk * 8] && !(L->lanemin[0] < d)) L->lanemin[0] = d;
    for (int l = 2; l < 6; l++)
        if (!s[0x82 + fp_lanemap(f, l)] && !(L->lanemin[l - 1] < d)) L->lanemin[l - 1] = d;
}
/* 0x418ae0: lane cut-offs and route markers from the current point on */
static void fl_flags(const AiNet *n, FList *L)
{
    L->sector_at = -1; L->kind9_at = -1; L->bit21 = 0;
    for (int i = 0; i < 5; i++) L->lanemin[i] = 99999.0f;
    int from = L->cur < 1 ? 0 : L->cur;
    for (int i = from; i < L->n; i++) {
        const FPoint *f = L->p[i];
        Pt *p = pt_at(n, f->sec, f->pt);
        if (i == from || f->sec != L->p[i - 1]->sec) fl_lanecut(n, L, f);
        if (!p) continue;
        if (pt_insector(n, p) && L->sector_at == -1) L->sector_at = i;
        int nl = pt_nlinks(n, p);
        for (int k = 0; k < nl; k++) {
            uint32_t lf = lk_flags(pt_link(n, p, k));
            if ((lf & 0xf) == 9 && L->kind9_at == -1) L->kind9_at = i;
            if ((lf >> 21 & 1) && !L->bit21) L->bit21 = 1;
        }
    }
}

/* 0x4123c0: how far q lies right of a lane's line at the point */
static float fp_latlane(const AiNet *n, const FPoint *f, const float *q, int lane)
{
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) return 0;
    return fp_lat(n, f, p, q) - (pt_laneoff(p, fp_lanemap(f, lane)) + fp_shift(f, lane));
}
/* vt+0x1c / vt+0x20 through the point (0x40add0 style: (0,1) without one) */
static void fp_dirv(const AiNet *n, const FPoint *f, float *o, int opposite)
{
    Pt *p = pt_at(n, f->sec, f->pt);
    if (!p) { o[0] = 0; o[1] = 1; return; }
    if (opposite) fp_odir(n, f, p, o); else fp_dir(n, f, p, o);
}

/* 0x41a180: the in-between points of each stretch (+0x74) get their lane
   shifts from a Hermite curve between the stretch's real end points, with
   tangents from the neighbouring real points (0.75 x the stretch length) */
static void fl_smooth(const AiNet *n, FList *L, int from)
{
    int hi = L->n - 1;
    if (from < 1) from = 0;
    if (from > hi) from = hi;
    for (int cnt = 0; from >= 0 && cnt < 2; from--) if (!L->p[from]->interp) cnt++;
    if (from < 1) from = 0;
    while (from < L->n) {
        int a = from;
        while (a < L->n && L->p[a]->interp) a++;
        int b = a + 1;
        while (b < L->n && L->p[b]->interp) b++;
        if (a < L->n && b < L->n && b - a > 1) {
            FPoint *A = L->p[a], *B = L->p[b];
            float d0 = A->dist, len = B->dist - d0, tl = 0.75f * len;   /* 0x783c90 */
            float pa[3], pb[3], la[4][3], lb[4][3], t0[2], t1[2], lt0[4][2], lt1[4][2];
            fp_point(n, A, pa, d0, 0, 0);
            fp_point(n, B, pb, B->dist, 0, 0);
            for (int k = 0; k < 4; k++) {
                fp_point(n, A, la[k], A->dist, 0, 2 + k);
                fp_point(n, B, lb[k], B->dist, 0, 2 + k);
            }
            int c = b + 1;
            while (c < L->n && L->p[c]->interp) c++;
            if (a - 1 < 0) {
                float d[2];
                fp_dirv(n, A, d, 1);
                v2setlen(t0, d, tl);
                for (int k = 0; k < 4; k++) { fp_dirv(n, A, d, 1); v2setlen(lt0[k], d, tl); }
            } else {
                FPoint *P = L->p[a - 1];
                float q[3];
                fp_point(n, P, q, P->dist, 0, 0);
                t0[0] = pa[0] - q[0]; t0[1] = pa[1] - q[1];
                v2setlen(t0, t0, tl);
                for (int k = 0; k < 4; k++) {
                    fp_point(n, P, q, P->dist, 0, 2 + k);
                    lt0[k][0] = la[k][0] - q[0]; lt0[k][1] = la[k][1] - q[1];
                    v2setlen(lt0[k], lt0[k], tl);
                }
            }
            if (c < L->n) {
                FPoint *C = L->p[c];
                float q[3];
                fp_point(n, C, q, C->dist, 0, 0);
                t1[0] = q[0] - pb[0]; t1[1] = q[1] - pb[1];
                v2setlen(t1, t1, tl);
                for (int k = 0; k < 4; k++) {
                    fp_point(n, C, q, C->dist, 0, 2 + k);
                    lt1[k][0] = q[0] - lb[k][0]; lt1[k][1] = q[1] - lb[k][1];
                    v2setlen(lt1[k], lt1[k], tl);
                }
            } else {
                float d[2];
                fp_dirv(n, B, d, 0);
                v2setlen(t1, d, tl);
                for (int k = 0; k < 4; k++) { fp_dirv(n, B, d, 0); v2setlen(lt1[k], d, tl); }
            }
            Herm2 h;
            herm2_set(&h, pa, t0, pb, t1);
            for (int i = a + 1; i <= b - 1; i++) {
                FPoint *f = L->p[i];
                float q[3];
                herm2_at(&h, (f->dist - d0) * (1.0f / len), q);
                f->shift[0] = fp_latlane(n, f, q, 0) + f->shift[0];
            }
            for (int k = 0; k < 4; k++) {
                herm2_set(&h, la[k], lt0[k], lb[k], lt1[k]);
                for (int i = a + 1; i <= b - 1; i++) {
                    FPoint *f = L->p[i];
                    float q[3];
                    herm2_at(&h, (f->dist - d0) * (1.0f / len), q);
                    f->shift[1 + k] = fp_latlane(n, f, q, 2 + k) + f->shift[1 + k];
                }
            }
        }
        from = b;
    }
}

/* 0x419e10: how much the lane bends over the list from `from` (from the
   halfway point, or within `upto` metres): the sine between the first and
   second half's directions, + when it turns right */
static float fl_bend(const AiNet *n, const FList *L, int lane, int from, float upto)
{
    int cnt = L->n - from;
    if (L->n == 0 || cnt < 2) return 0;
    const FPoint *A = L->p[from];
    int mid = -1, end = -1;
    if (upto >= 0) {
        int i;
        for (i = from; i < L->n; i++) if (upto * 0.5f <= L->p[i]->dist - A->dist) break;
        mid = i < L->n ? i : -1;
        for (i = mid + 1; i < L->n; i++) if (upto <= L->p[i]->dist - A->dist) break;
        end = (mid != -1 && i < L->n) ? i : -1;
    }
    if (end == -1) { mid = cnt / 2 + from; end = L->n - 1; }
    float a[3], m[3], e[3];
    fp_point(n, A, a, A->dist, 0, lane);
    fp_point(n, L->p[mid], m, L->p[mid]->dist, 0, lane);
    fp_point(n, L->p[end], e, L->p[end]->dist, 0, lane);
    float v1[2] = { a[0] - m[0], a[1] - m[1] }, v2[2] = { m[0] - e[0], m[1] - e[1] };
    v2norm(v1, v1); v2norm(v2, v2);
    float z = v1[0] * v2[1] - v1[1] * v2[0];
    float s = fabsf(z);
    return 0 <= z ? -s : s;
}
/* 0x41fd50: each point's lateral limits per lane, the road edges */
static void fl_bounds(const AiNet *n, AiRacer *r, FList *L)
{
    if (!L->n) return;
    float bend = 0;
    int i0 = fl_closest(n, L, r->car.pos, 0, 0, 0, 0);
    if (i0 != -1) bend = fl_bend(n, L, 0, i0, -1.0f);
    FPoint *f = L->p[L->n - 1];
    f->lo[0] = fp_left(n, f, 0); f->hi[0] = fp_right(n, f, 0);
    for (int k = 0; k < 4; k++) { f->lo[1 + k] = fp_left(n, f, 2 + k); f->hi[1 + k] = fp_right(n, f, 2 + k); }
    for (int i = L->n - 2; i >= 0; i--) {
        FPoint *c = L->p[i], *x = L->p[i + 1];
        float g = (x->dist - c->dist) * 99999.0f;
        int fl = bend <= -0.12f, fr = !(bend < 0.12f);                  /* 0x7854e0 / 0x785368 */
        float l0 = fp_left(n, c, 0), r0 = fp_right(n, c, 0), w = r0 - l0;
        if (l0 > 1.0f && !(w < 99999.0f)) l0 = 0;
        if (r0 < -1.0f && !(w < 99999.0f)) r0 = 0;
        float gl = fl ? 3.0f * g : g, gr = fr ? 3.0f * g : g;          /* 0x783ca8 */
        float lo = x->lo[0] - gr;
        c->lo[0] = l0 <= lo ? lo : l0;
        float hi = x->hi[0] + gl;
        c->hi[0] = r0 < hi ? r0 : hi;
        for (int k = 0; k < 4; k++) {
            float lk = fp_left(n, c, 2 + k), rk = fp_right(n, c, 2 + k);
            lo = x->lo[1 + k] - g;
            c->lo[1 + k] = lk <= lo ? lo : lk;
            hi = x->hi[1 + k] + g;
            c->hi[1 + k] = rk < hi ? rk : hi;
        }
    }
}

/* 0x41f8e0 (mode 1): a turn's approach (off < 0) or exit (off > 0) points:
   halfway, a lane-0 point held 8 m in from the outside (at most 5 m inside
   the edge), and at |off| the plain lane-0 line; with the list index span
   each covers */
static void fl_turnend(const AiNet *n, FList *L, int j, float off)
{
    Junc *J = &L->j[j];
    int pos = off > 0, turn = J->turn != 0;
    int idx = J->idx + (pos ? 1 : 0), hi = L->n - 1;
    int c = idx < 1 ? 0 : idx;
    if (c > hi) c = hi;
    if (idx != c) return;
    float t1 = off + J->far, th = off * 0.5f + J->far;
    int ih = idx, ifu, A, B, C, D;
    FPoint *f = L->p[ih], *f2;
    if (pos) {
        if (f->dist < th) while (ih < hi) { f = L->p[++ih]; if (!(f->dist < th)) break; }
        ifu = ih; f2 = f;
        if (f2->dist < t1) while (ifu < hi) { f2 = L->p[++ifu]; if (!(f2->dist < t1)) break; }
        A = idx;
        B = ih - 1 > idx ? ih - 1 : idx;
        C = ih;
        D = ifu - 1 > ih ? ifu - 1 : ih;
    } else {
        if (f->dist > th) while (ih > 0) { f = L->p[--ih]; if (f->dist <= th) break; }
        ifu = ih; f2 = f;
        if (f2->dist > t1) while (ifu > 0) { f2 = L->p[--ifu]; if (f2->dist <= t1) break; }
        A = ifu + 1 < ih ? ifu + 1 : ih;
        B = ih;
        C = ih + 1 < idx ? ih + 1 : idx;
        D = idx;
    }
#define CL(v) ((v) < 1 ? 0 : (v) > hi ? hi : (v))
    A = CL(A); B = CL(B); C = CL(C); D = CL(D);
#undef CL
    float lat;
    if (turn) { lat = 8.0f - f->shift[0]; float b = fp_right(n, f, 0) - 5.0f; if (b < lat) lat = b; }   /* 0x783c9c / 0x783c98 */
    else { lat = -8.0f - f->shift[0]; float b = fp_left(n, f, 0) + 5.0f; if (!(b <= lat)) lat = b; }
    float ph[3], pf[3];
    fp_point(n, f, ph, th, lat, 0);
    fp_point(n, f2, pf, t1, -f2->shift[0], 0);
    if (pos) {
        J->d[0] = ph[0]; J->d[1] = ph[1]; J->e[0] = pf[0]; J->e[1] = pf[1];
        J->ix[2] = (signed char)A; J->ix[3] = (signed char)C; J->ix[6] = (signed char)B; J->ix[7] = (signed char)D;
        J->s[3] = th; J->s[4] = t1;
    } else {
        J->b[0] = ph[0]; J->b[1] = ph[1]; J->a[0] = pf[0]; J->a[1] = pf[1];
        J->ix[0] = (signed char)A; J->ix[1] = (signed char)C; J->ix[4] = (signed char)B; J->ix[5] = (signed char)D;
        J->s[1] = th; J->s[0] = t1;
    }
}
/* 0x419000: the racing line through a turn, four Hermite pieces a-b-c-d-e
   over the recorded index spans */
static void fl_turnline(const AiNet *n, FList *L, int j)
{
    Junc *J = &L->j[j];
    const float *sp[4] = { J->a, J->b, J->c, J->d }, *ep[4] = { J->b, J->c, J->d, J->e };
    float len[4], tl[4], st[4], mt[4][2];
    for (int k = 0; k < 4; k++) {
        len[k] = J->s[k + 1] - J->s[k];
        tl[k] = len[k] * 0.75f;                                         /* 0x783c90 */
        if (tl[k] <= 1.0f) tl[k] = 1.0f;
    }
    st[0] = 0.1f * tl[0]; st[1] = tl[1]; st[2] = tl[2]; st[3] = tl[3];  /* 0x783c94 */
    for (int k = 0; k < 3; k++) {
        mt[k][0] = ep[k + 1][0] - ep[k][0]; mt[k][1] = ep[k + 1][1] - ep[k][1];
        v2setlen(mt[k], mt[k], tl[k]);
    }
    int e3 = J->ix[7], hi2 = L->n - 2, c3 = e3 < 1 ? 0 : e3;
    if (c3 > hi2) c3 = hi2;
    if (e3 == c3) {
        FPoint *q = L->p[e3 + 1];
        float p[3];
        fp_point(n, q, p, q->dist, 0, 0);
        mt[3][0] = p[0] - J->e[0]; mt[3][1] = p[1] - J->e[1];
    } else {
        FPoint *q = L->p[L->n - 1];
        float d[2];
        fp_dirv(n, q, d, 0);
        mt[3][0] = d[0]; mt[3][1] = d[1];
    }
    v2setlen(mt[3], mt[3], 0.1f * tl[3]);
    for (int k = 0; k < 4; k++) {
        if (!(0.1f < len[k])) continue;
        int a = J->ix[k], b = J->ix[4 + k], hi = L->n - 1;
        int ca = a < 1 ? 0 : a > hi ? hi : a, cb = b < 1 ? 0 : b > hi ? hi : b;
        if (a != ca || b != cb) continue;
        float t0[2];
        if (a < 1) fp_dirv(n, L->p[0], t0, 1);
        else {
            FPoint *q = L->p[a - 1];
            float p[3];
            fp_point(n, q, p, q->dist, 0, 0);
            t0[0] = sp[k][0] - p[0]; t0[1] = sp[k][1] - p[1];
        }
        v2setlen(t0, t0, st[k]);
        Herm2 h;
        herm2_set(&h, sp[k], t0, ep[k], mt[k]);
        for (int i = a; i <= b; i++) {
            FPoint *f = L->p[i];
            float q[3];
            herm2_at(&h, (f->dist - J->s[k]) * (1.0f / len[k]), q);
            f->shift[0] = fp_latlane(n, f, q, 0) + f->shift[0];
        }
    }
}
/* 0x422780: each recorded turn: approach and exit, the racing line through
   it, the corner speed (lateral grip over the curvature of b-c-d, 60 m/s
   at most) and 6 m/s in the traffic lanes, braked to before and
   accelerated from after */
static float curvature3(const float *p1, const float *p2, const float *p3);
static void fl_turns(const AiNet *n, AiRacer *r, FList *L)
{
    float a = car_brake_decel(r), g = car_corner_g(r);
    for (int j = 0; j < L->nj; j++) {
        Junc *J = &L->j[j];
        for (int k = 0; k < 8; k++) J->ix[k] = -1;
        J->s[0] = J->s[1] = J->s[3] = J->s[4] = -1.0f;
        fl_turnend(n, L, j, -75.0f);                                   /* 0x783c74 */
        fl_turnend(n, L, j, 75.0f);
        /* mode 2 with +-10 m (0x783c78) computes and discards */
        fl_turnline(n, L, j);
        if (L->n - 2 < J->idx) J->vcorner = 60.0f;                      /* 0x783c8c */
        else J->vcorner = corner_speed(g, curvature3(J->b, J->c, J->d), 60.0f);
        for (int k = 0; k < 4; k++) J->w[k] = 6.0f;
        int i = J->idx;
        FPoint *f1 = i == -1 ? NULL : L->p[i], *f2 = i + 1 == -1 ? NULL : L->p[i + 1];
        float d1 = f1 ? f1->dist - J->far : 0, d2 = f2 ? f2->dist - J->far : 0;
        float vc = J->vcorner;
        float v1 = sqrtf(vc * vc - (d1 * a + d1 * a));
        float v2 = sqrtf(car_accel_at(r, vc) * d2 + car_accel_at(r, vc) * d2 + vc * vc);
        if (f1 && !(f1->speed[0] < v1)) f1->speed[0] = v1;
        if (f2 && !(f2->speed[0] < v2)) f2->speed[0] = v2;
        float l1 = d1 * -0.2f, l2 = d2 * 0.2f;
        for (int k = 0; k < 4; k++) {
            if (f1 && !(f1->speed[1 + k] < l1 + J->w[k])) f1->speed[1 + k] = l1 + J->w[k];
            if (f2 && !(f2->speed[1 + k] < l2 + J->w[k])) f2->speed[1 + k] = l2 + J->w[k];
        }
        fl_propagate(r, L, i - 1, 0);
        fl_propagate(r, L, i + 2, 1);
    }
}
/* 0x5d46e0: where the line a1-a2 meets b1-b2 (0 when parallel) */
static int lines_meet(const float *a1, const float *a2, const float *b1, const float *b2, float *o)
{
    float d = (a2[0] - a1[0]) * (b2[1] - b1[1]) - (a2[1] - a1[1]) * (b2[0] - b1[0]);
    if (d == 0) return 0;
    float t = ((a1[1] - b1[1]) * (b2[0] - b1[0]) - (a1[0] - b1[0]) * (b2[1] - b1[1])) / d;
    o[0] = a1[0] + (a2[0] - a1[0]) * t; o[1] = a1[1] + (a2[1] - a1[1]) * t;
    return 1;
}
/* 0x5d4770: curvature of the circle through three points, + turning left */
/* 0x5d4770: signed curvature of the circle through three points (its
   centre into c) */
static float curvature3c(const float *p1, const float *p2, const float *p3, float *cout)
{
    float u[2] = { p1[0] - p2[0], p1[1] - p2[1] }, w[2] = { p3[0] - p2[0], p3[1] - p2[1] };
    float m1[2] = { p2[0] + u[0] * 0.5f, p2[1] + u[1] * 0.5f }, m2[2] = { p2[0] + w[0] * 0.5f, p2[1] + w[1] * 0.5f };
    float e1[2] = { u[1] + m1[0], m1[1] - u[0] }, e2[2] = { w[1] + m2[0], m2[1] - w[0] }, c[2];
    if (!lines_meet(m1, e1, m2, e2, c)) return 0;
    if (cout) { cout[0] = c[0]; cout[1] = c[1]; }
    float rr = v2len(p2[0] - c[0], p2[1] - c[1]);
    if (rr <= 0.001f) rr = 0.001f;
    float k = 1.0f / rr;
    if (w[1] * u[0] - w[0] * u[1] < 0) k = -k;
    return k;
}
static float curvature3(const float *p1, const float *p2, const float *p3) { return curvature3c(p1, p2, p3, NULL); }


/* ---- the path update (once per think) ------------------------------------ */
/* 0x40ab10: the list point whose distance is nearest `d` (flag 1: only
   points beyond d + slack, 2: only up to d - slack), from index `from` */
static int fl_nearest_dist(const FList *L, float d, unsigned flags, int from, float slack)
{
    int best = -1; float bd = 99999.0f;
    for (int i = from; i < L->n; i++) {
        float x = L->p[i]->dist;
        if ((flags & 1) && !(d + slack < x)) continue;
        if ((flags & 2) && !(x <= d - slack)) continue;
        float e = fabsf(x - d);
        if (best == -1 || e < bd) { best = i; bd = e; }
    }
    return best;
}
/* 0x419b20: the car's place on the list: index, path position, lateral
   off the lane */
static int fl_locate(const AiNet *n, const FList *L, const float *q, float *pathpos, float *lat, int lane)
{
    int i = fl_closest(n, L, q, 0, lane, 0, 0);
    if (i == -1) { *pathpos = *lat = 0; return -1; }
    *pathpos = fp_pathpos(n, L->p[i], q);
    *lat = fp_latlane(n, L->p[i], q, lane);
    return i;
}
/* 0x40ada0: how far a lateral position lies outside the lane's road edges */
static float fp_outside(const AiNet *n, const FPoint *f, float lat, int lane)
{
    if (0 < lat) return lat - fp_right(n, f, lane);
    return fp_left(n, f, lane) - lat;
}
/* 0x401b70 / 0x401bc0: lateral distance past a route point's edges */
static float pt_outside(Pt *p, float lat, int back)
{
    if (!back) { if (0 < lat) return lat - pt_wr(p); return -(pt_wl(p) + lat); }
    if (lat < 0) return -(pt_wr(p) + lat);
    return lat - pt_wl(p);
}

static int racer_lane(const AiRacer *r) { return r->ls.lane; }             /* 0x402170 */

/* vt+0x18, 0x413540: the controller's current route point */
static void ctrl_setpoint(AiRacer *r, Pt *p)
{
    const AiNet *n = r->w->net;
    if (!r->human) r->rsec = (r->rpt && sec_of(n, r->rpt)) ? (short)rdu16(sec_of(n, r->rpt) + 10) : -1;
    r->rpt = p;                         /* (a human's 0x40a1a0 also stamps +0x54 / +0x5c, unread here) */
}
/* vt+0x48, 0x4105b0 (a human's 0x411720: the direction only): a link the
   car has passed */
static void ctrl_passlink(AiRacer *r, Lk *l)
{
    const AiNet *n = r->w->net;
    uint32_t f = lk_flags(l);
    if (r->human || r->traffic) { r->rdir ^= (int)(f >> 23 & 1); return; }
    if ((f >> 16 & 3) == 1) r->link_time = r->w->time;                 /* 0x7e95c4 */
    if ((lk_index(n, l) & 0xffff) == r->last_kind2_link) r->last_kind2_link = -1;
    r->rdir ^= (int)(f >> 23 & 1);
}
/* 0x413590: find the route point under the car: the nearest racing-route
   point (flag 0x800) of a section whose distance range holds the car's
   progress (then of any section), not on the section it was on */
static void ctrl_findroad(AiRacer *r, int force)
{
    const AiNet *n = r->w->net;
    const unsigned char *cur = r->rpt ? sec_of(n, r->rpt) : NULL;
    Pt *best = NULL;
    for (int pass = 0; pass < 2 && !best; pass++) {
        float bd = 999999.0f;                                           /* 0x783498 */
        int ns = r->traffic ? n->norder : SEC_MAX;
        for (int s = 0; s < ns; s++) {
            const unsigned char *sec = r->traffic ? n->order[s] : n->sec[s];
            if (!sec) continue;
            if (r->traffic) {                                           /* 0x5d4480(ctrl +0x10) */
                int has = 0, o = r->rdir ? 0 : 4;
                for (int k = 0; k < 4; k++) if (sec[0x84 + o + k]) has = 1;
                if (!has) continue;
            }
            if (pass == 0) {                                            /* 0x5d44c0 */
                float a = rdf(sec + 0x38), b = rdf(sec + 0x3c), x = r->progress;
                int in = a < b ? !(x < a) && x < b : !(x < a) || x < b;
                if (!in) continue;
            }
            /* 0x5e2e40: the section's nearest point, flags 4: racing route */
            float mn[2] = { rdf(sec + 0x60), rdf(sec + 0x64) }, mx[2] = { rdf(sec + 0x68), rdf(sec + 0x6c) };
            const float *q = r->car.pos;
            if (q[0] < mn[0] - bd || q[0] > mx[0] + bd || q[1] < mn[1] - bd || q[1] > mx[1] + bd) continue;
            int np = sec_npts(sec);
            Pt *pick = NULL; float pd = bd;
            for (int i = 0; i < np; i++) {
                Pt *p = sec_pt(sec, i);
                float dx = pt_x(p) - q[0], dy = pt_y(p) - q[1], dz = pt_z(p) - q[2];
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d < pd && (pt_flags(p) & (r->traffic ? 0x2000000u : 0x800u))) { pd = d; pick = p; }
                int st = -2 - (int)(-0.02f * d);
                if (st > 0) i += st;
            }
            if (!pick) continue;
            float lat = (q[0] - pt_x(pick)) * pt_dy(pick) - (q[1] - pt_y(pick)) * pt_dx(pick);   /* 0x40cd00 */
            float off = 0 < lat ? lat - pt_wr(pick) : -(pt_wl(pick) + lat);
            float al = (q[0] - pt_x(pick)) * pt_dx(pick) + (q[1] - pt_y(pick)) * pt_dy(pick);   /* 0x40ce90 */
            float alo = 0 < al ? al - pt_seglen(pick) : -(pt_prevseglen(n, pick) + al);
            int outside = 0 < off || 0 < alo;
            if (((force && pass > 0) || !outside) && pd < bd && sec != cur) { bd = pd; best = pick; }
        }
    }
    ctrl_setpoint(r, best);
}
/* vt+0x30, 0x42cdc0: the car lost its road: forget the plan and the branch
   choices, find the road again and the direction along it */
static void ctrl_lostroad(AiRacer *r, int force)
{
    const AiNet *n = r->w->net;
    Pt *old = r->rpt;
    float od[2] = { 0, 0 };
    if (old) {
        if (!r->rdir) pt_backdir(n, old, od); else { od[0] = pt_dx(old); od[1] = pt_dy(old); }
        const float *v = r->car.absv < 1.0f ? r->car.fwd : r->car.vel;
        float l = v2len(v[0], v[1]);
        if (0.1f < l) { od[0] += v[0] * (0.1f / l); od[1] += v[1] * (0.1f / l); }   /* 0x78351c */
    }
    r->rpt = NULL;
    fl_reset(&r->L);
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    ctrl_findroad(r, force);
    /* kind 1 (0x413860): lane from the section */
    r->last_kind2_link = -1;
    if (r->traffic) {                                                   /* lane class 2: 0x4233e0; owner kind 3 keeps its way */
        traffic_lanes(r);
        r->dplan.valid = 0;
        return;
    }
    if (!old || !r->rpt) {
        if (r->rpt) {
            const float *v = 1.0f < r->car.absv ? r->car.vel : r->car.fwd;
            r->rdir = 0 < pt_dx(r->rpt) * v[0] + pt_dy(r->rpt) * v[1];
        }
    } else
        r->rdir = !(pt_dx(r->rpt) * od[0] + od[1] * pt_dy(r->rpt) <= 0);
    r->dplan.valid = 0;                                                 /* driver +0x1d9 */
}

/* ---- the racers around (0x6148e0) ---------------------------------------- */
static float progress_diff(const AiWorld *w, float a, float b)          /* 0x5d5cd0 */
{
    float d = a - b;
    if (w->circuit) {
        if (d < -w->track_half) return d + w->track_len;
        if (w->track_half < d) d -= w->track_len;
    }
    return d;
}
static float lookahead(const AiRacer *r)                                /* 0x404910 */
{
    float t = (r->traffic ? 4.0f : 3.0f) * r->car.absv;                                       /* 0x408240 */
    return t <= 125.0f ? 125.0f : t;                                    /* 0x7e8e60 */
}
static void racer_table(AiRacer *r)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    FList *L = &r->L;
    r->nent = 0; r->own = -1;
    if (L->n < 1) return;
    float look = lookahead(r), acc = r->acc_now;
    float sp, sl;
    int si = fl_locate(n, L, r->car.pos, &sp, &sl, r->ls.lane);           /* 0x419bc0 */
    float d0[2];
    fp_dirv(n, L->p[si < 0 ? 0 : si], d0, 0);
    float hd = atan2f(d0[1], d0[0]);
    float c = cosf(1.5707963f - hd), s = sinf(1.5707963f - hd);        /* rotation by 0x4000 - heading */
    float sv[2] = { c * r->car.vel[0] - s * r->car.vel[1], s * r->car.vel[0] + c * r->car.vel[1] };
    RacerEnt tmp[16]; int nt = 0, cap = 15;
    for (int k = 0; k < w->nseen; k++) {
        const AiSeen *o = &w->seen[k];
        int self = o->racer == r;
        if (self) cap = 16;
        if (!(o->active || self) || nt >= cap) continue;
        float dx = o->v->pos[0] - r->car.pos[0], dy = o->v->pos[1] - r->car.pos[1], dz = o->v->pos[2] - r->car.pos[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float vx = o->v->vel[0] - r->car.vel[0], vy = o->v->vel[1] - r->car.vel[1], vz = o->v->vel[2] - r->car.vel[2];
        float rel = sqrtf(vx * vx + vy * vy + vz * vz);
        if (r->car.fwd[0] * o->v->fwd[0] + r->car.fwd[1] * o->v->fwd[1] < 0) rel = -rel;
        float pd = progress_diff(w, o->progress, r->progress);
        int near = dist < look;
        if (!near) {                                                    /* 0x403a40 */
            float as = r->car.acc[0] * r->car.fwd[0] + r->car.acc[1] * r->car.fwd[1] + r->car.acc[2] * r->car.fwd[2];
            float dv = (r->car.fwd[0] * o->v->fwd[0] + r->car.fwd[1] * o->v->fwd[1] <= 0) ? o->acc + as : as - o->acc;
            near = dist < -((3.0f * dv * 0.5f + rel) * 3.0f);
            (void)acc;
        }
        if (!near || !(fabsf(pd) <= look * 3.0f)) continue;
        RacerEnt *e = &tmp[nt++];
        float lat;
        e->idx = fl_locate(n, L, o->v->pos, &e->pathpos, &lat, 0);
        int fi = e->idx < 0 ? 0 : e->idx;
        e->lat[0] = fp_latlane(n, L->p[fi], o->v->pos, 0);
        for (int l = 0; l < 4; l++) e->lat[1 + l] = fp_latlane(n, L->p[fi], o->v->pos, 2 + l);
        float od[2];
        fp_dirv(n, L->p[fi], od, 0);
        float ohd = atan2f(od[1], od[0]);
        float c2 = cosf(1.5707963f - ohd), s2 = sinf(1.5707963f - ohd);
        float ov[2] = { c2 * o->v->vel[0] - s2 * o->v->vel[1], s2 * o->v->vel[0] + c2 * o->v->vel[1] };
        e->rv[0] = ov[0] - sv[0]; e->rv[1] = ov[1] - sv[1];
        float ch = cosf(atan2f(o->v->fwd[1], o->v->fwd[0]) - (float)r->car.heading * (6.2831853f / 65536.0f));
        float cc = ch * ch;
        e->ext[0] = cc * o->half[1] + o->half[0] * (1.0f - cc);
        e->ext[1] = (1.0f - cc) * o->half[1] + cc * o->half[0];
        e->car = (short)o->index;
        e->self = self;
    }
    /* sorted by signed squared distance on the path, farthest ahead first */
    float key[16]; int ord[16];
    for (int i = 0; i < nt; i++) {
        float dpp = tmp[i].pathpos - sp;
        int lane = r->ls.lane;
        float lt = lane == 0 ? tmp[i].lat[0] : (lane >= 2 && lane < 6) ? tmp[i].lat[lane - 1] : 0;
        key[i] = dpp * dpp + (lt - sl) * (lt - sl);
        if (dpp < 0) key[i] = -key[i];
        ord[i] = i;
    }
    for (int pass = nt; pass > 0; pass--)
        for (int i = 0; i < nt - 1; i++)
            if (key[i] < key[i + 1]) {
                float t = key[i]; key[i] = key[i + 1]; key[i + 1] = t;
                int o = ord[i]; ord[i] = ord[i + 1]; ord[i + 1] = o;
            }
    for (int i = 0; i < nt; i++) {
        r->ent[i] = tmp[ord[i]];
        if (r->ent[i].self) r->own = (signed char)i;
    }
    r->nent = nt;
}

/* 0x428030: drop the points the car has passed, extend ahead to `look`
   metres, then the speed, smoothing, turn, marker and limit passes */
static void fl_rebuild(AiRacer *r, int at, Pt *p, int back, float look)
{
    const AiNet *n = r->w->net;
    FList *L = &r->L;
    if (!p) return;
    float from = 0;
    if (!L->rebuild && !r->w->drag) {
        if (at == -1) {
            int lane = r->ls.lane < 1 ? 0 : r->ls.lane;
            at = fl_closest(n, L, r->car.pos, 0, lane, L->cur, 0);
        }
        if (at == -1) fl_reset(L);
        else {
            FPoint *f = L->p[at];
            L->cur = (signed char)at;
            /* vt+0x28: an AI racer (0x570a60) keeps 20 points behind (10 in
               free roam and outrun), a human's controller (0x404800) none */
            int keep = (r->human || r->traffic) ? 0 : (r->w->mode == 0 || r->w->mode == 7) ? 10 : 20;
            int d = at - keep;
            fl_dropfront(n, L, d < 1 ? 0 : d);
            from = f->dist;
            if (!L->xac && f->back != back) {
                for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;     /* 0x404950 */
                /* 0x40f750: on a race route, back to the section's way */
                if (world_race_route(r->w) && r->rpt && sec_of(n, r->rpt))
                    r->rdir = !(rd32(sec_of(n, r->rpt) + 0x70) >> 1 & 1);
                fl_reset(L);
            }
        }
    } else { fl_reset(L); L->rebuild = 0; }
    fl_trim(L);
    int n0 = L->n;
    if (n0 == 0) {                                                      /* 0x426810 */
        const unsigned char *s = sec_of(n, p);
        float z = 0, z4[4] = { 0, 0, 0, 0 };
        fl_start(L, (int)(pt_flags(p) & 0x7ff), pt_index(n, p), &z, z4, 0, 0, NULL, back);
        (void)s;
        L->cur = 0;
        fl_extend(r, L, look);
    } else fl_extend(r, L, from + look);
    fl_speeds(n, r, L);
    fl_smooth(n, L, n0 - 1);
    fl_turns(n, r, L);
    fl_flags(n, L);
    fl_bounds(n, r, L);
    if (L->n == 0) { L->cur = L->cur2 = -1; return; }
    if (L->cur == -1) L->cur = (signed char)fl_closest(n, L, r->car.pos, 0, 0, 0, 0);
    int c = L->cur < 1 ? 0 : L->cur;
    if (c > L->n - 1) c = L->n - 1;
    L->cur = L->cur2 = (signed char)c;
}
/* 0x428720: place the car on its path, re-plan ahead, refresh the racers
   around. 0 when the path is lost or the car is past its end. */
static int racer_updatepath(AiRacer *r)
{
    const AiNet *n = r->w->net;
    FList *L = &r->L;
    int on = 0, at = -1, ok = 1;
    float width = r->halfwidth + r->halfwidth;
    int lane = r->ls.lane == -1 ? 0 : r->ls.lane;
    if (L->n == 0) {
        Pt *p = r->rpt;
        if (p) {
            float dx = r->car.pos[0] - pt_x(p), dy = r->car.pos[1] - pt_y(p);
            float al = pt_dx(p) * dx + dy * pt_dy(p);
            float lat = pt_dy(p) * dx - dy * pt_dx(p);
            if (pt_outside(p, lat, 0) < width) {
                float lo = -pt_prevseglen(n, p), hi = pt_seglen(p);
                float cl = al < lo ? lo : al > hi ? hi : al;           /* 0x401090 */
                if (cl == al) on = 1;
            }
        }
    } else {
        float pp, lat;
        fl_locate(n, L, r->car.pos, &pp, &lat, lane);
        at = fl_nearest_dist(L, pp, 0, 0, 0);
        if (at != -1) {
            FPoint *f = L->p[at];
            float first = L->n ? L->p[0]->dist : 0, last = L->n ? L->p[L->n - 1]->dist : 0;
            if (first - 50.0f <= pp && pp < last && fp_outside(n, f, lat, lane) < width) {   /* 0x785b80 */
                ctrl_setpoint(r, pt_at(n, f->sec, f->pt));
                /* 0x4120c0: links passed on the way */
                int a = L->cur2, b = at;
                int lo = a <= b ? a : b, hi = a <= b ? b : a;
                for (int i = lo + 1; i <= hi && i < L->n; i++) if (L->p[i]->link) ctrl_passlink(r, L->p[i]->link);
                r->rdir = !f->back;
                on = 1;
            }
        }
    }
    if (!on) { at = -1; ctrl_lostroad(r, 1); }
    if (!r->rpt) { fl_reset(L); ok = 0; goto done; }
    /* vt+0x2c > width -> vt+0x90, a no-op for a racer */
    fl_rebuild(r, at, r->rpt, !r->rdir, lookahead(r));
    if (L->cur == -1) fl_reset(L);
    /* the lane gone from the path (lanes 2..9): the nearest that is there */
    if (L->cur != -1 && L->n) traffic_lane_gone(r);
done:
    if (L->n == 0) { r->nent = 0; r->own = -1; return 0; }
    racer_table(r);
    float mine = r->own >= 0 ? r->ent[r->own].pathpos : 0;
    if (L->p[L->n - 1]->dist < mine) return 0;
    return ok;
}

/* ---- the driving plan (actions build it, the driver follows it) ---------- */
static float d3(const float *a, const float *b)                         /* 0x43ce10 */
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}
static void plan_length(AiPlan *P)                                      /* 0x405270 */
{
    P->len = 0;
    for (int i = 0; i < P->n - 1; i++) P->len = d3(P->p[i], P->p[i + 1]) + P->len;
}
/* 0x4036e0: q lies ahead of the car (along its velocity, its nose when
   slower than 1 m/s) by more than cos = thr */
static int car_ahead(const AiCarView *c, const float *q, float thr)
{
    float d[2] = { c->vel[0], c->vel[1] }, v[2] = { q[0] - c->pos[0], q[1] - c->pos[1] };
    if (c->absv < 1.0f) { d[0] = c->fwd[0]; d[1] = c->fwd[1]; }
    v2norm(d, d);
    v2norm(v, v);
    return thr < d[1] * v[1] + d[0] * v[0];
}
/* 0x404cd0: append a waypoint; with `check` refused when closer than 1 m
   to the last or (within 20 m) turning back on it. The first one must lie
   ahead of the car or over 20 m away. */
static int plan_add(AiPlan *P, const float *q, int sec, int pt, int lane, int check, const AiCarView *car)
{
    int n = P->n, ok = 1;
    float lim = 20.0f;                                                  /* 0x7837bc */
    if (n > 19) return 0;
    if (n < 1) {
        if (car) {
            float dx = q[0] - car->pos[0], dy = q[1] - car->pos[1];
            float d = sqrtf(dx * dx + dy * dy);
            if (P->near) lim = 8.0f;                                    /* 0x7837c0 */
            if (1.0f <= d && (car_ahead(car, q, 0) || lim < d)) { P->near = 0; goto add; }
            ok = 0;
        }
        P->near = 0;
        if (!ok) return 0;
    } else {
        const float *l = P->p[n - 1];
        float d = sqrtf((q[0] - l[0]) * (q[0] - l[0]) + (q[1] - l[1]) * (q[1] - l[1]));
        ok = !(d < 1.0f);                                               /* 0x7837b8 */
        if (check) {
            if (!ok) return 0;
            if (d < 20.0f) {
                const float *ref = NULL;
                if (n < 2) { if (car) ref = car->pos; } else ref = P->p[n - 2];
                if (ref) {
                    float a[2] = { l[0] - ref[0], l[1] - ref[1] }, b[2] = { q[0] - l[0], q[1] - l[1] };
                    if (b[0] * a[0] + b[1] * a[1] < 0) return 0;
                }
            }
        }
        if (!ok) return 0;
    }
add:
    P->p[n][0] = q[0]; P->p[n][1] = q[1]; P->p[n][2] = q[2];
    P->sec[n] = (short)sec; P->pt[n] = (short)pt; P->lane[n] = (unsigned char)lane;
    P->cur_time = 0; P->x1bc = 0;
    if (n > 0) P->len = d3(q, P->p[n - 1]) + P->len;
    P->n = (signed char)(n + 1);
    return ok;
}
/* 0x404ef0: the first waypoint ahead of pos along dir (once per tick) */
static int plan_cur(AiPlan *P, float now, const float *pos, const float *dir)
{
    if (now == P->cur_time) return P->cur;
    if (P->n < 1) { P->cur = 0; P->cur_time = 0; return P->cur; }
    int pick = P->n - 1;
    float dn[2] = { dir[0], dir[1] };
    v2norm(dn, dn);
    for (int i = 0; i < P->n; i++) {
        float v[2] = { P->p[i][0] - pos[0], P->p[i][1] - pos[1] }, u[2];
        v2norm(u, v);
        if (0.0f < u[0] * dn[0] + u[1] * dn[1] && 0.0f < v[1] * v[1] + v[0] * v[0]) { pick = i; break; }   /* 0x7837c4 / 0x827a20 */
    }
    P->cur = (signed char)pick;
    P->cur_time = now;
    return P->cur;
}
/* 0x40f7a0: drop the first k waypoints */
static void plan_drop(AiPlan *P, int k, int relen)
{
    if (k >= P->n) return;
    for (int i = 0; i < P->n - k; i++) {
        memcpy(P->p[i], P->p[i + k], sizeof P->p[i]);
        P->sec[i] = P->sec[i + k]; P->pt[i] = P->pt[i + k]; P->lane[i] = P->lane[i + k];
    }
    P->n = (signed char)(P->n - k);
    P->cur_time = 0; P->x1bc = 0;
    if (relen) plan_length(P);
    int c = P->cur - k;
    P->cur = (signed char)(c < 1 ? 0 : c);
}
/* 0x415590: the waypoint `dist` metres on from the car along the plan */
static int plan_reach(AiPlan *P, const AiCarView *c, float now, float dist)
{
    int i = plan_cur(P, now, c->pos, c->vel);
    float dx = P->p[i][0] - c->pos[0], dy = P->p[i][1] - c->pos[1];
    float d = sqrtf(dx * dx + dy * dy);
    while (d < dist) {
        if (P->n <= i) return i;
        if (i + 1 >= PLAN_MAX) return i + 1;
        dx = P->p[i + 1][0] - P->p[i][0]; dy = P->p[i + 1][1] - P->p[i][1];
        d = sqrtf(dx * dx + dy * dy) + d;
        i++;
    }
    return i;
}
/* 0x41d160: keep `dist` metres of the plan ahead of the car (nothing
   without a car or a distance) */
static void plan_keep(AiPlan *P, const AiCarView *c, float now, float dist)
{
    if (P->n != 0) {
        if (fabsf(dist) > 0.001f && !(dist < 0) && P->valid && c) {
            int i = 0;
            for (; i < P->n; i++)
                if (0.0f < (P->p[i][0] - c->pos[0]) * c->vel[0] + (P->p[i][1] - c->pos[1]) * c->vel[1]) break;
            if (i < P->n) {
                if (0 < i) plan_drop(P, i, 0);
                int k = plan_reach(P, c, now, dist) + 1;
                P->n = (signed char)(P->n <= k ? P->n : k);
                plan_length(P);
            } else { P->n = 0; P->len = 0; }
            goto out;
        }
        P->n = 0;
    }
    P->len = 0;
    P->time = now;
    P->valid = 0;
out:
    P->x1c8 = 0; P->near = 0; P->cur_time = 0; P->x1bc = 0;
}
/* 0x60bfa0: an entry's lateral offset from a lane's line */
static float ent_lat(const RacerEnt *e, int lane)
{
    if (lane == 0) return e->lat[0];
    if (lane > 1 && lane < 6) return e->lat[lane - 1];
    return 0;
}
/* 0x415b70: a traffic lane the section lacks: the nearest one it has
   (within 3 lanes), the offset carried over from the last waypoint */
static void plan_lanecheck(const AiNet *n, int *lane, float *off, const FPoint *f, const AiPlan *P)
{
    int l = *lane;
    float o = *off;
    if (1 < l && l < 6) {
        const unsigned char *s = (f->sec >= 0 && f->pt != -1) ? n->sec[f->sec] : NULL;
        int m = f->back ? l + 4 : l;
        if (s && !s[0x82 + m]) {
            for (int k = 1; k < 4; k++) {
                int a = l + k, b = l - k;
                if (1 < a && a < 6 && s[0x82 + (f->back ? a + 4 : a)]) {
                    if (0 < P->n) { *lane = a; *off = fp_latlane(n, f, P->p[P->n - 1], a) + o; return; }
                    l = a; break;
                }
                if (1 < b && b < 6 && s[0x82 + (f->back ? b + 4 : b)]) {
                    if (0 < P->n) { *lane = b; *off = fp_latlane(n, f, P->p[P->n - 1], b) + o; return; }
                    l = b; break;
                }
            }
        }
    }
    *lane = l; *off = o;
}
static int world_driftlike(const AiWorld *w)
{
    return !w->drag && !w->x890108 && (w->x890114 || w->drift || w->x890104);
}
/* 0x40fc80: the offset at the next point: `rate` metres across per metre
   towards the target, inside the lane's limits less `margin` (and never
   off the lane line's side of the limits) */
static float plan_latstep(const AiNet *n, const AiRacer *r, float cur, int lane, float rate, float target,
                          const FPoint *f, float margin)
{
    float step = fp_fromprev(n, f), v;
    if (!(cur < target)) { v = cur - step * rate; if (!(target < v)) v = target; }
    else { v = step * rate + cur; if (!(v < target)) v = target; }
    int s = lslot(lane);
    float lo = (s < 0 ? 0 : f->lo[s]) + margin, hi = (s < 0 ? 0 : f->hi[s]) - margin;
    if (world_driftlike(r->w) && r->in_race) {
        if (hi < lo) lo = hi = (hi + lo) * 0.5f;
    } else {
        if (!(lo < 0)) lo = 0;
        if (hi <= 0) hi = 0;
    }
    if (v <= lo) v = lo;
    if (hi < v) v = hi;
    return v;
}
/* 0x411c80: the list point on route point (sec, pt) */
static int fl_find(const FList *L, int sec, int pt)
{
    for (int i = 0; i < L->n; i++) if (L->p[i]->sec == sec && L->p[i]->pt == pt) return i;
    return -1;
}
/* 0x41d530: extend the plan along the path list: each point's offset from
   the lane line moves towards `target` */
static void plan_fill(AiRacer *r, AiPlan *P, float margin, int lane, float target, float rate)
{
    const AiNet *n = r->w->net;
    FList *L = &r->L;
    if (L->n == 0 || P->n >= PLAN_MAX || r->own < 0) return;
    const RacerEnt *me = &r->ent[r->own];
    int i;
    float cur;
    if (P->n == 0) {
        i = me->idx + 1;
        if (L->n - 1 <= i) i = L->n - 1;
        cur = ent_lat(me, lane);
    } else {
        const float *q = P->p[P->n - 1];
        i = fl_closest(n, L, q, 1, lane, 0, 0.5f);                      /* 0x783808 */
        int j = fl_closest(n, L, q, 0, lane, i - 1 < 1 ? 0 : i - 1, 0);
        if (i < 0) return;
        cur = fp_latlane(n, L->p[j < 0 ? 0 : j], q, lane);
    }
    float half = me->ext[0];
    int drift = world_driftlike(r->w) && r->in_race;
    for (; i < L->n; i++) {
        FPoint *f = L->p[i];
        if (!drift) plan_lanecheck(n, &lane, &cur, f, P);
        float v = plan_latstep(n, r, cur, lane, rate, target, f, half + margin);
        float sg = !(cur < target) ? -rate : rate;
        if (fabsf(v - target) > 0.001f && (v - cur) * sg < 0) rate = 0;
        cur = v;
        if (P->n < PLAN_MAX) {                                          /* 0x415d50 */
            float q[3];
            fp_point(n, f, q, f->dist, v, lane);
            plan_add(P, q, f->sec, f->pt, lane, 1, &r->car);
        }
    }
}
/* 0x41bfe0: plan afresh when the car is off the driver's plan sideways
   (more than 1.5 m, 3 in drag, 4.5 in drift-type races, and more than
   its distance down the road), the lane state jumps, or there is none */
static int racer_needreplan(AiRacer *r, const AiLane *ls)
{
    const AiNet *n = r->w->net;
    const AiPlan *P = &r->dplan;
    int lane = ls->lane;
    float lat = 0, pp = 0;
    int c = P->cur;
    if (0 <= c && c < P->n && P->sec[c] != -1 && P->pt[c] != -1) {
        int i = fl_find(&r->L, P->sec[c], P->pt[c]);
        if (i != -1) {
            lat = fp_latlane(n, r->L.p[i], P->p[c], lane);
            pp = fp_pathpos(n, r->L.p[i], P->p[c]);
        }
    }
    const RacerEnt *me = &r->ent[r->own < 0 ? 0 : r->own];
    float dl = fabsf(ent_lat(me, lane) - lat), dp = pp - me->pathpos, thr;
    const AiWorld *w = r->w;
    if (r->owner_kind == 3) thr = 10.0f;                                /* 0x783548 */
    else if (w->drift || w->x890104) thr = 4.5f;                        /* 0x783544 */
    else if (w->drag || w->x890108) thr = 3.0f;                         /* 0x783540 */
    else thr = 1.5f;                                                    /* 0x7e8d94 */
    int re = (thr < dl && dp < dl) || !(fabsf(ls->rate - 999.0f) >= 0.001f) || P->n == 0;
    return r->force_replan || re;
}
/* 0x420d40: the action's plan for the lane state: the driver's plan kept
   for half the speed in metres (30 m at least; a quarter of it in drag),
   or afresh, then extended along the path */
static void plan_build(AiRacer *r, const AiLane *ls, AiPlan *P)
{
    const AiWorld *w = r->w;
    float margin = fabsf(ls->rate - 999.0f) > 0.001f ? 1.0f : 0.0f;   /* 0x7837ec */
    if (world_driftlike(w) && r->in_race) margin = 5.0f;
    if (!racer_needreplan(r, ls)) {
        float keep = 0;
        if (r->dplan.valid) {
            *P = r->dplan;                                              /* 0x401d40 */
            int c = plan_cur(P, w->time, r->car.pos, r->car.vel);
            plan_drop(P, c, 1);
            keep = 0.5f * r->car.absv;                                  /* 0x7837e0 */
            if (keep <= 30.0f) keep = 30.0f;                            /* 0x7837e8 */
            if (w->drag || w->x890108) keep = 0.25f * r->car.absv;      /* 0x7837e4 */
        }
        plan_keep(P, &r->car, w->time, keep);
    } else plan_keep(P, NULL, w->time, 0);
    plan_fill(r, P, margin, ls->lane, ls->off, ls->rate);
    P->valid = 1;
}
/* ---- the race driver's tools (0x57xxxx) ----------------------------------- */
typedef struct { int n; float x0, x1, inv; const float *d; } ACurve;   /* 0x5764d0 */
static float acurve(const ACurve *c, float x)
{
    float t = (x - c->x0) * c->inv;
    int i = (int)t;
    if (i < 0 || !(0 < t)) return c->d[0];
    if (i >= c->n - 1) return c->d[c->n - 1];
    if (t < (float)i) i--;
    float f = t - (float)i;
    return (1.0f - f) * c->d[i] + f * c->d[i + 1];
}
static float agraph(const float *p, int n, float x)                     /* 0x576790 */
{
    if (n > 1) {
        if (x <= p[0]) return p[1];
        if (p[2*n-2] <= x) return p[2*n-1];
        for (int i = 0; i < n - 1; i++)
            if (p[2*i] <= x && x < p[2*i+2]) {
                float dx = p[2*i+2] - p[2*i];
                if (fabsf(dx) <= 1e-6f) return (p[2*i+3] - p[2*i+1]) * 0.5f + p[2*i+1];
                return (x - p[2*i]) / dx * (p[2*i+3] - p[2*i+1]) + p[2*i+1];
            }
    }
    return p[1];
}
#define U16RAD_F 9.58738e-5f                                            /* 0x784240 */
#define DEG_F    0.0174532924f                                          /* 0x784244 */
#define RADDEG_F 57.2957764f                                            /* 0x78424c */
/* 0x43e6d0: the 16-bit angle of the vector (x, y) */
static unsigned short ang16(float x, float y)
{
    return (unsigned short)(int)floorf(atan2f(y, x) * (32768.0f / 3.14159265f) + 0.5f);
}
static float wrap_pi(float a)
{
    if (a > 3.14159274f) return a - 6.28318548f;                        /* 0x784490 / 0x78448c */
    if (a < -3.14159274f) return a + 6.28318548f;
    return a;
}
static float sin16f(float x) { return sinf((float)(unsigned short)(int)(x * 10430.3779f) * U16RAD_F); }   /* 0x43c350 */

/* 0x579f80: the same over a time window (n = window x rate + 0.5 slots) */
typedef struct { Ring r; float window; int head; float t[RING_MAX]; } TRing;
static void tring_init(TRing *q, float window, float rate)
{
    int n = (int)(window * rate + 0.5f);
    if (n > RING_MAX) n = RING_MAX;
    ring_init(&q->r, n);
    q->window = window; q->head = 0;
    memset(q->t, 0, sizeof q->t);
}
static void tring_push(TRing *q, float v, float t)                      /* 0x576aa0 */
{
    Ring *r = &q->r;
    int i = r->idx;
    if (r->d[i] != 0 || q->t[i] != 0) r->sum = r->sum - r->d[i];
    else r->count++;
    r->sum = v + r->sum;
    r->d[i] = v;
    q->t[i] = t;
    while (q->window < t - q->t[q->head]) {
        if (0 < q->t[q->head]) {
            r->sum = r->sum - r->d[q->head];
            r->d[q->head] = 0; q->t[q->head] = 0;
            r->count--;
        }
        if (r->n <= ++q->head) q->head = 0;
    }
    r->idx++;
    r->avg = r->sum / (float)r->count;
    if (r->n <= r->idx) r->idx = 0;
}
/* 0x4016b0: a signal's history: the time steps, the trapezoid integral
   steps and the derivative, each averaged over its own count */
typedef struct { Ring dt, in, de; float rate, last, prev; } Hist;
static void hist_init(Hist *h, int a, int b)
{
    ring_init(&h->dt, a); ring_init(&h->in, a); ring_init(&h->de, b);
    h->rate = 30.0f; h->last = h->prev = 0;
}
static void hist_push(Hist *h, float x, float dt, int nod, int noi)    /* 0x576bb0 */
{
    h->prev = h->last;
    float d = x - h->prev;
    h->last = x;
    float in = noi ? 0 : (0.5f * d + h->prev) * dt;
    float de = nod ? 0 : d / dt;
    ring_push(&h->dt, dt);
    ring_push(&h->in, in);
    ring_push(&h->de, de);
}
static void hist_reset(Hist *h, float v)                                /* 0x57a010 */
{
    h->last = 0; h->prev = v;
    ring_fill(&h->in, v);
    h->prev = v; h->last = 0;
    ring_fill(&h->de, v);
}
static void hist_reset_d(Hist *h, float v)                              /* 0x576c40 */
{
    h->last = 0; h->prev = v;
    ring_fill(&h->de, v);
}
static float hist_p(const Hist *h) { return h->last; }                  /* +0x7c */
static float hist_i(const Hist *h)                                      /* +0x30 x count / (+8 x +0x78) */
{
    if (!h->in.count) return 0;
    return h->in.sum * (float)h->in.count / (h->dt.sum * h->rate);
}
static float hist_d(const Hist *h) { return h->de.avg; }                /* +0x5c */

/* 0x409b50: an adaptive PID controller: three gains, each nudged (MIT
   rule) towards making `x` zero, one gain per 0.2 s in turn */
typedef struct {
    int   mode;                /* +0x1c adaptation rule (0..5) */
    float period;              /* +0x20 */
    float inlo[3], inhi[3];    /* +0x04.. input clamps (pairs) */
    float glo[3], ghi[3];      /* +0x24.. gain limits (pairs) */
    float thr[3];              /* +0x3c */
    float rate[3];             /* +0x48 */
    float k;                   /* +0x54 */
    float g[3];                /* +0x58 */
    TRing step[3];             /* +0x64 */
    float lastx;               /* +0x70 */
    TRing dx;                  /* +0x74 */
    int   simple;              /* vtable 0x784614 (inputs from a Hist) else 0x78463c */
    float in[3];               /* complicated: +0xa8 */
    Hist  h;                   /* simple: +0xa8 */
} APid;
static void apid_init(APid *c, int mode, float period)
{
    memset(c, 0, sizeof *c);
    tring_init(&c->dx, period, 45.0f);
    c->mode = mode; c->period = 0.2f; c->k = 1.0f;                       /* 0x783bf8 / 0x783c10 */
    for (int i = 0; i < 3; i++) {
        tring_init(&c->step[i], period, 45.0f);
        c->ghi[i] = 1.0f; c->glo[i] = 0;                                /* 0x783c04 / 0x783c00 */
        c->thr[i] = 0.01f;                                              /* 0x783bfc */
        c->inlo[i] = -99999.0f; c->inhi[i] = 99999.0f;                  /* 0x783c08 / 0x783c0c */
        c->rate[i] = 5e-05f;                                            /* 0x783c14 */
        c->g[i] = 0;
    }
}
static float apid_in(const APid *c, int k)                              /* vt+8: 0x40a0a0 / 0x40a030 */
{
    if (!c->simple) return c->in[k];
    if (k == 0) return hist_p(&c->h);
    if (k == 1) return hist_i(&c->h);
    if (k == 2) return hist_d(&c->h);
    return 0;
}
static float apid_sens(const APid *c, float a)                          /* 0x409ea0 */
{
    float f = c->dx.r.avg;
    if (fabsf(a) > 1e-09f) {
        float v = f / a;
        if (v <= -1000.0f) v = -1000.0f;                                /* 0x783c18 */
        if (1000.0f < v) v = 1000.0f;
        return v;
    }
    if (fabsf(f) < 0.001f) return 0;
    return 0 < f ? 1.0f : -1.0f;
}
static float sgn0(float x) { return x == 0 ? 0 : 0 < x ? 1.0f : -1.0f; }   /* 0x409f40 */
static float apid_rule(const APid *c, int k, float x, float aux)        /* 0x411390 */
{
    float r = c->rate[k], s = apid_sens(c, c->step[k].r.avg);
    switch (c->mode) {
    case 0: return -(s * r * x);
    case 1: return -(s * r * x) / (s * s + c->k);
    case 2: return -(sgn0(x) * s * r);
    case 3: return -(sgn0(s) * r * x);
    case 4: return -(sgn0(x) * sgn0(s) * r);
    case 5: return -(r * x * aux);
    }
    return 0;
}
static void apid_update(APid *c, float x, float dt, float aux, float now)   /* 0x4182c0 */
{
    int k = (int)(now / c->period) % 3;
    float st = 0;
    if (c->thr[k] <= fabsf(apid_in(c, k))) {
        st = apid_rule(c, k, x, aux);
        float g = st * dt + c->g[k];
        if (!(g <= c->glo[k])) ; else g = c->glo[k];
        if (c->ghi[k] < g) g = c->ghi[k];
        c->g[k] = g;
    }
    tring_push(&c->step[k], st, now);
    for (int i = 0; i < 3; i++) if (i != k) tring_push(&c->step[i], 0, now);
    float d = 0.001f < dt ? dt : 0.001f;
    tring_push(&c->dx, (x - c->lastx) / d, now);
    c->lastx = x;
}
static float apid_out(const APid *c)                                    /* 0x409da0 */
{
    float o = 0;
    for (int k = 0; k < 3; k++) {
        float v = apid_in(c, k);
        if (v <= c->inlo[k]) v = c->inlo[k];
        if (c->inhi[k] < v) v = c->inhi[k];
        o = c->g[k] * v + o;
    }
    return o;
}

/* ---- the race driver (0x425ab0, vtable 0x784eb0, 0x460 bytes) ----------- */
typedef struct AiDriver {
    unsigned short steer;      /* +0x1e0 wheel angle, 16-bit */
    int   gear;                /* +0x1e4 gear request: -1 automatic, 0 reverse, 1 neutral */
    float gas, hand, brake;    /* +0x20c +0x210 +0x214 */
    float tgt[2];              /* +0x224 the point it steers for */
    float maxgas;              /* +0x22c */
    float vtarget;             /* +0x230 */
    float x234, x238;          /* +0x234 (counts down) +0x238 */
    char  go;                  /* +0x23c */
    float hb_time;             /* +0x240 */
    char  fwd;                 /* +0x244 */
    float x248;                /* +0x248 */
    char  stop;                /* +0x24c */
    int   cmd;                 /* +0x250 filter resets */
    char  no_tc;               /* +0x254 */
    float air_since;           /* +0x258, -1 unset */
    char  nos;                 /* +0x25c */
    float vprev;               /* +0x260 */
    Hist  f[6];                /* +0x264 body, offset, heading, velocity, slip fwd, slip lat */
    APid  sp;                  /* +0x280 steering */
    APid  vp;                  /* +0x334 speed above 30 m/s */
    /* the drift driver (0x425e40, vtable 0x784f40, 0x250 bytes) in its
       place when 0x4260b0 picks it: same base (+0x1e0 .. +0x214, +0x224) */
    int   drift;
    float dd_speed;            /* +0x22c target speed (vt+0x38 0x405b60) */
    float dd_pslip;            /* +0x230 slide angle last frame */
    float dd_delta;            /* +0x238 the slide angle it wants */
    char  dd_stop;             /* +0x23c (vt+0x1c 0x405b10) */
    char  clutch_kick;         /* drivetrain +0x74 it sets (physics mover only) */
} AiDriver;

static const float LOOK_D[10] = { 22.8971996f, 22.8971996f, 24, 27.8999996f, 33, 39, 45.7999992f, 49.4000015f, 53.0999985f, 55.9000015f };
static const ACurve LOOK = { 10, 0, 160, 0.0562499985f, LOOK_D };       /* 0x7e9d94 */
static const float LOOKC_M_D[2] = { 0.600000024f, 1.10000002f }, LOOKC_P_D[2] = { 1.29999995f, 1.10000002f };
static const ACurve LOOKC_M = { 2, 0, 3, 0.333333343f, LOOKC_M_D };     /* 0x7e9de4 */
static const ACurve LOOKC_P = { 2, 0, 3, 0.333333343f, LOOKC_P_D };     /* 0x7e9df8 */
static const float G0_D[10] = { 0.328000009f, 0.219999999f, 0.148000002f, 0.115000002f, 0.0900000036f, 0.074000001f, 0.057f, 0.0489999987f, 0.0430000015f, 0.0399999991f };
static const float G1_D[10] = { 0.209999993f, 0.244000003f, 0.26699999f, 0.289999992f, 0.305000007f, 0.32100001f, 0.328000009f, 0.335999995f, 0.340999991f, 0.344000012f };
static const float G2_D[10] = { 0, 0.0758289993f, 0.0758289993f, 0.0738260001f, 0.0604030006f, 0.046980001f, 0.046980001f, 0.0402680002f, 0.0402680002f, 0.0402680002f };
static const ACurve G0 = { 10, 0, 160, 0.0562499985f, G0_D };           /* 0x7e9d44 */
static const ACurve G1 = { 10, 0, 160, 0.0562499985f, G1_D };           /* 0x7e9d80 */
static const ACurve G2 = { 10, 0, 160, 0.0562499985f, G2_D };           /* 0x7e9d58 (0x7e9d6c the same) */
static const float MAXST_D[10] = { 45, 30, 5.5f, 5, 3, 2, 2, 2, 2, 2 };
static const ACurve MAXST = { 10, 0, 160, 0.0562499985f, MAXST_D };     /* 0x7e9e0c */
static const float STR_E_D[20] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
static const ACurve STR_E = { 20, 0, 95, 0.200000003f, STR_E_D };       /* 0x7e9e34 */
static const float STR_V_D[10] = { 4, 3, 2, 1, 0.449999988f, 0.140000001f, 0.100000001f, 0.100000001f, 0.100000001f, 0.100000001f };
static const ACurve STR_V = { 10, 0, 160, 0.0562499985f, STR_V_D };     /* 0x7e9e20 */
static const float PEDAL_D[10] = { 30, 15, 7.5f, 6, 6, 6, 6, 6, 6, 6 };
static const ACurve PEDAL = { 10, 0, 160, 0.0562499985f, PEDAL_D };     /* 0x7e9d10 */
static const float BLEND_G[4] = { 5, 0, 10, 1 };                        /* 0x81e7e4 */
static const float ADAPT_G[20] = { 0,0, 1,-0.5f, 3,-1.5f, 5,-2.5f, 10,-5, 50,-5, 60,-5, 70,-5, 80,-5, 90,-5 };   /* 0x827a18 */
static const float EXCESS_G[18] = { -20,100, -10,10, -5,7, -2,3, 0,0, 2,0, 10,-3, 20,-10, 30,-100 };            /* 0x81e7f4 */
static const float KP_ACC[7] = { 0.100000001f, 0.100000001f, 0.100000001f, 0.100000001f, 0.00499999989f, 0.100000001f, 0.100000001f };
static const float KI_ACC[7] = { 0.0199999996f, 0.0199999996f, 0.0199999996f, 0.0199999996f, 0.00499999989f, 0.0199999996f, 0.0199999996f };
static const float KD_ACC[7] = { 0.0199999996f, 0.0199999996f, 0.00999999978f, 0.00999999978f, 0.00999999978f, 0.00999999978f, 0.00999999978f };
static const float KP_DEC[7] = { 0.100000001f, 0.100000001f, 0.100000001f, 0.100000001f, 0.00499999989f, 0.100000001f, 0.100000001f };
static const float KI_DEC[7] = { 0.0199999996f, 0.0199999996f, 0.0199999996f, 0.0199999996f, 0, 0.0199999996f, 0.0199999996f };
static const float KD_DEC[7] = { 0.0199999996f, 0.0199999996f, 0.00999999978f, 0.00999999978f, 0.00800000038f, 0.00999999978f, 0.00999999978f };
static const float SKP[7] = { 0.899999976f, 1, 0.800000012f, 0.699999988f, 0.850000024f, 0.5f, 0 };       /* 0x78395c */
static const float SKI[7] = { 0.200000003f, 0, 0.100000001f, 1.41999996f, 0.790000021f, 1, 0 };           /* 0x783978 */
static const float SKD[7] = { 0.300000012f, 0.0799999982f, 0.180000007f, 0, 0.119999997f, 0, 0 };         /* 0x783994 */
static const float LOOKK[7] = { 0.550000012f, 0.400000006f, 0.349999994f, 0.550000012f, 0.899999976f, 0.400000006f, 0 };   /* 0x7839b0 */
static const float LOOKMIN[7] = { 15, 15, 15, 5, 9, 15, 0 };                                              /* 0x7839cc */
static const float MAXGAS[4] = { 0.930000007f, 0.970000029f, 1, 1 };                                      /* 0x783a28 */
/* steering PID gain sets {rate x3, min/max x3, airborne caps x3}: road,
   full simulation, full simulation SUV, street X (0x78382c..0x7838dc) */
typedef struct { float rate[3], lo[3], hi[3], cap[3]; } GainSet;
static const GainSet GS_ROAD = { { 4.99999987e-05f, 4.99999987e-05f, 4.99999987e-06f }, { 0.150000006f, 0, 0.0399999991f }, { 0.400000006f, 0.100000001f, 0.0599999987f }, { 0.150000006f, 0, 0.0500000007f } };
static const GainSet GS_PHYS = { { 4.99999987e-05f, 4.99999987e-05f, 4.99999987e-06f }, { 0.150000006f, 0, 0 }, { 0.5f, 0.5f, 0.0799999982f }, { 0.150000006f, 0, 0.0500000007f } };
static const GainSet GS_SUV = { { 4.99999987e-05f, 4.99999987e-05f, 4.99999987e-06f }, { 0.400000006f, 0, 0 }, { 0.699999988f, 0.5f, 0.0799999982f }, { 0.400000006f, 0, 0.100000001f } };
static const GainSet GS_SX = { { 4.99999987e-06f, 4.99999987e-05f, 4.99999987e-06f }, { 0.275000006f, 0, 0 }, { 0.300000012f, 0.300000012f, 0.0599999987f }, { 0, 0, 0 } };
static void gains_set(APid *c, const GainSet *g)
{
    for (int k = 0; k < 3; k++) { c->rate[k] = g->rate[k]; c->glo[k] = g->lo[k]; c->ghi[k] = g->hi[k]; }
}

static void driver_init(AiDriver *d, const AiRacer *r)                  /* 0x425ab0 */
{
    memset(d, 0, sizeof *d);
    apid_init(&d->sp, 0, 0.2f);                                         /* 0x783828 */
    apid_init(&d->vp, 0, 0.2f);
    d->vp.simple = 1;
    hist_init(&d->vp.h, 4, 4);                                          /* 0x7e8ea0 / 0x7e8ea4 */
    d->maxgas = 1.0f;
    if (r->owner_kind == 2) {
        int i = r->w->difficulty;
        d->maxgas = MAXGAS[i < 0 ? 0 : i > 3 ? 3 : i];
    }
    d->go = 1; d->fwd = 1;
    d->air_since = -1;
    hist_init(&d->f[0], 5, 5); hist_init(&d->f[1], 5, 5); hist_init(&d->f[2], 5, 5);
    hist_init(&d->f[3], 4, 4); hist_init(&d->f[4], 4, 4); hist_init(&d->f[5], 4, 4);
    d->sp.period = 0.2f;
    for (int k = 0; k < 3; k++) d->sp.thr[k] = 0.01f;                   /* 0x7838e0 */
    gains_set(&d->sp, &GS_ROAD);
    d->gear = -1;
    /* 0x4260b0: an AI racer (controller 3) in a drift-type race gets the
       drift driver instead (base 0x423330: gear request 1) */
    const AiWorld *w = r->w;
    if (r->owner_kind == 2 && !w->drag && !w->x890108 && (w->x890114 || w->drift || w->x890104)) {
        memset(d, 0, sizeof *d);
        d->gear = 1;                                                    /* 0x423330 +0x1e4 */
        d->drift = 1;
    }
}
/* vt+0x38 (0x4054a0): the action's speed for the driver */
static void driver_setspeed(AiDriver *d, float v, float x234, float x238, int go)
{
    if (d->drift) {                                                     /* 0x405b60 */
        d->dd_speed = v;
        if (31.2919006f < v) d->dd_speed = 31.2919006f;                 /* 0x7844bc */
        return;
    }
    d->vtarget = v; d->x234 = x234; d->x238 = x238; d->go = (char)go;
}
/* vt+0x3c (0x405910): the action's plan */
static void driver_setplan(AiRacer *r, const AiPlan *P)
{
    if (r->drv->drift) { if (P) r->dplan = *P; return; }                /* 0x40cf60 */
    if (P && -1 < P->n && P->n < 21) r->dplan = *P;
}

/* 0x415630: the point `look` metres on along the plan, corners counting
   extra by how much the path is longer than the chord (x 7 for the first
   5 s of the race, else x 1) */
static int plan_target(AiRacer *r, float look, float *o)
{
    AiPlan *P = &r->dplan;
    const AiCarView *c = &r->car;
    if (P->n == 0) { o[0] = c->pos[0]; o[1] = c->pos[1]; o[2] = c->pos[2]; return -1; }
    if (P->n == 1) {
        float v[3] = { P->p[0][0] - c->pos[0], P->p[0][1] - c->pos[1], P->p[0][2] - c->pos[2] };
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float k = l == 0 ? 0 : look / l;                                /* 0x43cf90 */
        for (int i = 0; i < 3; i++) o[i] = v[i] * k + c->pos[i];
        return 0;
    }
    float cum[PLAN_MAX + 2];
    int i = plan_cur(P, r->w->time, c->pos, c->vel);
    float d0 = d3(P->p[i], c->pos);
    cum[i + 1] = d0;
    int j = i;
    if (d0 < look) {
        float k = 1.0f;                                                 /* 0x7837ac */
        float since = r->w->race_start < 0 ? -1.0f : r->w->time - r->w->race_start;   /* 0x402000 */
        if (since < 5.0f) k = 7.0f;                                     /* 0x7837b4 / 0x7837b0 */
        j = i + 1;
        const float *pp = c->pos;
        float prevseg = d0;
        for (; j < P->n; j++) {
            float seg = d3(P->p[j], P->p[j - 1]);
            float chord = d3(P->p[j], pp);
            cum[j + 1] = ((seg + prevseg) - chord) * k + cum[j] + seg;
            if (look <= cum[j + 1]) break;
            prevseg = seg; pp = P->p[j - 1];
        }
    }
    int last = P->n - 1;
    if (j <= last) {
        const float *b;
        float bd;
        int ret;
        if (i < j) { ret = j - 1; b = P->p[j - 1]; bd = cum[j]; }
        else { ret = -1; b = c->pos; bd = 0; }
        float t = (look - bd) / (cum[j + 1] - bd);
        for (int q = 0; q < 3; q++) o[q] = (P->p[j][q] - b[q]) * t + b[q];   /* 0x43d0c0 */
        return ret;
    }
    float v[3] = { P->p[last][0] - P->p[last - 1][0], P->p[last][1] - P->p[last - 1][1], P->p[last][2] - P->p[last - 1][2] };
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), want = look - cum[P->n];
    float k = l == 0 ? 0 : want / l;
    for (int q = 0; q < 3; q++) o[q] = v[q] * k + P->p[last][q];
    return last;
}
/* 0x415f80: the steering target: how far ahead (by speed, x the class
   curve), then the point there on the plan (no plan: straight ahead, and
   stop) */
static void driver_target(AiRacer *r)
{
    AiDriver *d = r->drv;
    const AiCarView *c = &r->car;
    float look;
    int m = c->mover, idx = -1;
    if (m == 1 || m == 4 || (m == 3 && c->ctrl_type != 4)) {
        float a = acurve(&LOOK, c->speed);
        look = acurve(m == 1 ? &LOOKC_P : &LOOKC_M, (float)r->perf.klass) * a;
    } else {
        idx = (m == 3) ? 4 : 3;
        look = c->speed * LOOKK[idx];
        if (look < LOOKMIN[idx]) look = LOOKMIN[idx];
    }
    float o[3];
    if (r->dplan.n < 1) {
        float k = 5.0f <= look ? look : 5.0f;                           /* 0x7843e8 */
        if (!(c->speed < 5.0f)) {                                       /* 0x403ba0: along the velocity */
            float l = sqrtf(c->vel[0] * c->vel[0] + c->vel[1] * c->vel[1] + c->vel[2] * c->vel[2]);
            float s = l == 0 ? 0 : k / l;
            for (int i = 0; i < 3; i++) o[i] = c->pos[i] + c->vel[i] * s;
        } else {                                                        /* 0x403af0: along the heading */
            float h = (float)c->heading * U16RAD_F;
            o[0] = c->pos[0] + cosf(h) * k; o[1] = c->pos[1] + sinf(h) * k; o[2] = c->pos[2];
        }
        d->vtarget = 0; d->x234 = 0;
    } else plan_target(r, look, o);
    d->tgt[0] = o[0]; d->tgt[1] = o[1];
}
/* 0x404af0: how far the car is right of the plan's line */
static float plan_offset(const AiPlan *P, const float *q)
{
    if (P->n < 1) return 0;
    if (P->n == 1) return d3(q, P->p[0]);
    float v[3] = { q[0] - P->p[0][0], q[1] - P->p[0][1], q[2] - P->p[0][2] };
    float s[3] = { P->p[1][0] - P->p[0][0], P->p[1][1] - P->p[0][1], P->p[1][2] - P->p[0][2] };
    if (0 < s[0] * v[0] + s[1] * v[1] + s[2] * v[2]) {
        for (int i = 2; i < P->n; i++) {
            float t[3] = { P->p[i][0] - P->p[i - 1][0], P->p[i][1] - P->p[i - 1][1], P->p[i][2] - P->p[i - 1][2] };
            float dd = t[0] * (q[0] - P->p[i - 1][0]) + t[1] * (q[1] - P->p[i - 1][1]) + t[2] * (q[2] - P->p[i - 1][2]);
            if (!(0 < dd)) break;
            for (int k = 0; k < 3; k++) { v[k] = q[k] - P->p[i - 1][k]; s[k] = t[k]; }
        }
    }
    float n[3] = { s[1], -s[0], 0 };                                    /* 0x43d1e0 with (0,0,1) */
    float l = sqrtf(n[0] * n[0] + n[1] * n[1]);
    if (l == 0) { n[0] = 1; n[1] = 0; } else { n[0] /= l; n[1] /= l; }  /* 0x43cf10 */
    return n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
}
/* 0x4054d0: the steering errors: heading off the target, the same
   blended with the velocity's (from 5 to 10 m/s), and the way back onto
   the plan's line within 5 m */
static unsigned short driver_errors(AiRacer *r, float *e_head, float *e_blend, float *e_off)
{
    AiDriver *d = r->drv;
    const AiCarView *c = &r->car;
    unsigned short a = ang16(d->tgt[0] - c->pos[0], d->tgt[1] - c->pos[1]);
    *e_head = wrap_pi((float)(unsigned short)(c->heading - a) * U16RAD_F);
    float k = agraph(BLEND_G, 2, c->absv);
    if (k <= 0) *e_blend = *e_head;
    else {
        unsigned short va = ang16(c->vel[0], c->vel[1]);
        float f = wrap_pi((float)(unsigned short)(va - a) * U16RAD_F);
        *e_blend = f * k + (1.0f - k) * *e_head;
    }
    float off = plan_offset(&r->dplan, c->pos);
    *e_off = wrap_pi((float)ang16(5.0f, off) * U16RAD_F);               /* 0x783938 */
    return a;
}
/* 0x59fa10: the slide angle, heading against the velocity */
static float car_slip_angle(const AiCarView *c)
{
    if (c->speed < 1.0f) return 0;                                      /* 0x7a1824 */
    float a = (float)(unsigned short)(c->heading - ang16(c->vel[0], c->vel[1])) * U16RAD_F;
    if (3.14159274f < a) a -= 3.14159274f + 3.14159274f;
    return a;
}
static int world_driftlike(const AiWorld *w);
/* 0x4057c0: wheel slip beyond 4 (forward, sideways) into their histories */
static void driver_slips(AiRacer *r, float dt)
{
    AiDriver *d = r->drv;
    float sf = 0, sl = 0;
    for (int i = 0; i < 4; i++) {
        float a = r->car.slip[i][0], b = fabsf(r->car.slip[i][1]);
        float ca = a < -4.0f ? -4.0f : a > 4.0f ? 4.0f : a;             /* 0x7839f4 */
        if (a != ca) sf = a + sf;
        float cb = b < -4.0f ? -4.0f : b > 4.0f ? 4.0f : b;             /* 0x7839f8 */
        if (b != cb) sl = sl + b;
    }
    if (r->car.speed < 0) sf = -sf;
    hist_push(&d->f[4], sf, dt, 0, 0);
    hist_push(&d->f[5], sl, dt, 0, 0);
}
/* 0x410040: traction control on (not in the first 4 s of a race with the
   car sliding more than 5 degrees) */
static int driver_tc(const AiRacer *r)
{
    const AiWorld *w = r->w;
    float s = car_slip_angle(&r->car);
    int early = w->race_start < 0 || (w->time - w->race_start) <= 4.0f; /* 0x7844f8 */
    float lim = 5.0f * DEG_F, c = s;                                    /* 0x783a50 */
    if (c <= -lim) c = -lim;
    if (lim < c) c = lim;
    return !r->drv->no_tc && r->owner_kind != 3 && (!early || s == c) && !r->human;
}
/* 0x41d8d0: throttle and brake for the target speed */
static void driver_speed(AiRacer *r, float *gas, float *brake, int mode, float dt)
{
    AiDriver *d = r->drv;
    const AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    if (!d->stop) {
        float t = fabsf(d->vtarget);
        if (t < 5.0f) {                                                 /* 0x783a48 */
            if (!(fabsf(t - c->absv) > 1.0f)) { *gas = 0; *brake = 0; goto clamp; }
            if (t <= c->absv) { *gas = 0; *brake = 1.0f; goto clamp; }
            *gas = 1.0f; *brake = 0; goto clamp;
        }
    }
    float t = fabsf(d->vtarget), out, lo, hi;
    if ((world_driftlike(w) && r->in_race) || t <= 30.0f || r->owner_kind == 3) {   /* 0x784478 */
        const Hist *h = &d->f[3];
        float P = hist_p(h), I = hist_i(h), D = hist_d(h);
        if (D <= -10.0f) D = -10.0f;                                    /* 0x7e8ea8 */
        if (10.0f < D) D = 10.0f;
        float kp, ki, kd;
        if (t + 2.0f <= c->absv) { kp = KP_DEC[mode]; ki = KI_DEC[mode]; kd = KD_DEC[mode]; hi = 0; lo = -1.0f; }
        else { kp = KP_ACC[mode]; ki = KI_ACC[mode]; kd = KD_ACC[mode]; lo = 0; hi = d->maxgas; }
        out = kp * P + ki * I + D * kd;
    } else {
        APid *p = &d->vp;
        p->period = 0.2f;
        p->glo[0] = -0.300000012f; p->ghi[0] = -0.200000003f;           /* 0x783908 / 0x783904 */
        p->glo[1] = 0; p->ghi[1] = 0;
        p->glo[2] = -0.600000024f; p->ghi[2] = 0;                       /* 0x783918 / 0x783914 */
        p->inlo[2] = -10.0f; p->inhi[2] = 10.0f;
        for (int k = 0; k < 3; k++) { p->thr[k] = 0.01f; p->rate[k] = 4.99999987e-06f; }   /* 0x78391c, 0x7838f8.. */
        int jump = fabsf(d->vtarget - d->vprev) > 1.0f;
        float ex = c->absv - t;
        /* 0x4183f0 */
        hist_push(&p->h, ex, dt, jump, 0);
        apid_update(p, hist_d(&p->h) - agraph(EXCESS_G, 9, ex), dt, t, w->time);
        if (c->absv < 10.0f) { p->g[0] = -1.0f; p->g[1] = 0; p->g[2] = 0; }   /* 0x783920.. */
        out = apid_out(p);
        lo = -1.0f; hi = 1.0f;
    }
    {
        float lim = acurve(&PEDAL, c->absv) * dt;
        float v = out;
        if (v <= -lim) v = -lim;
        if (lim < v) v = lim;
        v = (d->gas - d->brake) + v;
        if (v <= lo) v = lo;
        if (hi < v) v = hi;
        float tl = 1.0f;
        if (driver_tc(r)) {
            const Hist *h = &d->f[4];
            float P = hist_p(h), I = hist_i(h), D = hist_d(h);
            if (v < 0) tl = 0.0199999996f * P + 0.0199999996f * I + 0.00499999989f * D + 1.0f;   /* 0x7839fc.. */
            else tl = -0.200000003f * P + -0.0199999996f * I + -0.00499999989f * D + 1.0f;      /* 0x783a08.. */
            if (tl <= 0) tl = 0;
        }
        if (999.0f < hist_p(&d->f[5])) tl = 0;                          /* 0x7838e8 */
        if (999.0f < fabsf(apid_in(&d->sp, 2))) tl = 0;                 /* 0x7838f0 */
        if (world_driftlike(w) && r->in_race) {
            float s = car_slip_angle(c);
            tl = (sin16f(s + s + 1.57079637f) + 1.0f) * tl * 0.5f;
        }
        if (v < 0) {
            float b = -v;
            if (!(b < tl)) b = tl;
            if (driver_tc(r) && !(b < tl)) b = tl;
            *gas = 0;
            *brake = b < 0 ? 0 : b;
        } else {
            float g = v;
            if (!(g < tl)) g = tl;
            if (driver_tc(r) && !(g < tl)) g = tl;
            *gas = g; *brake = 0;
        }
    }
clamp:
    {
        float g = *gas;
        if (g <= 0) g = 0;
        if (d->maxgas < g) g = d->maxgas;
        *gas = g;
        float b = *brake;
        if (!(0 < b)) b = 0;
        else if (1.0f < b) b = 1.0f;
        *brake = b;
    }
}
/* the driver's tick (vtable slot 1, 0x421030): steer for the target with
   the adaptive PID, keep the target speed, and hand the car its controls */
/* ---- the drift driver's tick (0x416120) ---------------------------------- */
/* 0x405940: the angle wrapped by whole turns (round to nearest) */
static float wrap_turns(float x)
{
    float f = x * 0.159154937f + 0.5f;                                  /* 0x784498 / 0x784260 */
    float n = (float)(int)f;                                            /* _ftol */
    if (f < n) n -= 1.0f;
    return x - n * 6.28318548f;                                         /* 0x78448c */
}
/* 0x401090 */
static float clamp3(float x, float lo, float hi)
{
    float v = x <= lo ? lo : x;
    if (!(v <= hi)) v = hi;
    return v;
}
/* 0x591d90: heading against the velocity's direction (no speed test) */
static float car_slide(const AiCarView *c)
{
    float a = (float)(unsigned short)(c->heading - ang16(c->vel[0], c->vel[1])) * U16RAD_F;
    if (3.14159274f < a) a -= 3.14159274f + 3.14159274f;                /* 0x7a1788 */
    return a;
}
/* 0x405990: turn the wheels towards `a` (rad) at 360 deg/s, within 45 deg */
static void dd_steer(AiDriver *d, float a, float dt)
{
    float cur = wrap_turns((float)d->steer * U16RAD_F);
    float r = 360.0f * dt * DEG_F;                                      /* 0x783a68 */
    float dl = a - cur;
    if (!(-r < dl)) dl = -r;
    if (!(dl <= r)) dl = r;
    float n = dl + cur;
    if (n <= -0.785398185f) n = -0.785398185f;                          /* 0x7844a0 */
    else if (!(n <= 0.785398185f)) n = 0.785398185f;                    /* 0x78449c */
    d->steer = (unsigned short)(int)(n * 10430.3779f);                  /* 0x78423c */
}
static void drift_driver_tick(AiRacer *r, float dt)
{
    AiDriver *d = r->drv;
    const AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    float v = c->absv, aim = 0, p1[3], p2[3];
    if (r->dplan.n < 1) {                                               /* 0x403af0(10 m) */
        float h = (float)c->heading * U16RAD_F;
        p1[0] = c->pos[0] + cosf(h) * 10.0f; p1[1] = c->pos[1] + sinf(h) * 10.0f; p1[2] = c->pos[2];
    } else {
        float a = fabsf(plan_offset(&r->dplan, c->pos)) * 0.75f + 0.04f * v + 15.0f;   /* 0x783aa8 0x783aa0 0x783a9c */
        if (a <= 15.0f) a = 15.0f;                                      /* 0x783a8c */
        if (!(a <= 30.0f)) a = 30.0f;                                   /* 0x783a90 */
        plan_target(r, a, p1);
        float b = 0.08f * v + a;                                        /* 0x783aa4 */
        if (b <= 8.0f) b = 8.0f;                                        /* 0x783a94 */
        if (!(b <= 12.0f)) b = 12.0f;                                   /* 0x783a98 */
        plan_target(r, b, p2);
        aim = (float)ang16(p2[0] - p1[0], p2[1] - p1[1]) * U16RAD_F;
    }
    d->tgt[0] = p1[0]; d->tgt[1] = p1[1];
    float tgt = wrap_turns((float)ang16(d->tgt[0] - c->pos[0], d->tgt[1] - c->pos[1]) * U16RAD_F);
    float psi = (float)c->heading * U16RAD_F;
    float lim = 120.0f * DEG_F;                                         /* 0x783a6c */
    if (d->gear == 0) lim -= 30.0f * DEG_F;                             /* vt+0x20; 0x783a70 */
    int back = lim < fabsf(wrap_turns(psi - tgt));
    if (d->dd_speed < 0) back = 1;
    if (d->dd_stop) {
        d->brake = 1.0f;
        d->gas = 1.0f;
        dd_steer(d, 0, 1.0f);
        d->gear = 1;
    } else if (!back && 10.0f * 0.447027f <= v) {                       /* 0x783a64 x 0x7842a8 */
        /* slide at the angle the target asks for */
        float phv = (float)ang16(c->vel[0], c->vel[1]) * U16RAD_F;
        float s = car_slide(c);
        float srate = wrap_turns(s - d->dd_pslip) / dt;
        d->dd_pslip = s;
        float M = clamp3(45.0f * DEG_F + 0.03f * v, 45.0f * DEG_F, 70.0f * DEG_F);   /* 0x783a7c 0x783a80 0x783a74 0x783a78 */
        float t1 = clamp3((v - 0.0f) * -0.1f, 0.0f, 1.0f);              /* 0x783ac4 0x783ac0 0x783ab8 0x783abc */
        float term1 = clamp3(wrap_turns(aim - phv) * t1, -35.0f * DEG_F, 35.0f * DEG_F);   /* 0x783a88 */
        float t2 = clamp3(0.05f * v, 0.0f, 2.0f);                       /* 0x783ab4 0x783aac 0x783ab0 */
        float term2 = clamp3(wrap_turns(tgt - phv) * t2, -45.0f * DEG_F, 45.0f * DEG_F);   /* 0x783a84 */
        float delta = clamp3(term2 + term1, -M, M);
        d->dd_delta = delta;
        float e = wrap_turns(s - delta);
        float u = e * 1.5f + -0.02f * srate;                            /* 0x785110 0x783a60 */
        dd_steer(d, u * -1.0f, dt);                                     /* 0x7842a0 */
        float E = delta < 0 ? -e : e;
        float U = 0 < e ? -u : u;
        float c1 = (-0.349065852f - E) * 2.86478901f;                   /* 0x78510c 0x785108 */
        c1 = c1 <= 0 ? 0 : (c1 <= 1.0f ? c1 : 1.0f);
        float c2 = (0.261799395f - U) * 5.72957802f;                    /* 0x785104 0x785100 */
        c2 = c2 <= 0 ? 0 : (c2 <= 1.0f ? c2 : 1.0f);
        float c3 = (v - 8.94054508f) * 0.111849999f;                    /* 0x784568 0x7850fc */
        c3 = c3 <= 0 ? 0 : (c3 <= 1.0f ? c3 : 1.0f);
        d->hand = c3 * (c1 * c2);
        if (!(2.0f <= d->dd_speed) && !d->dd_stop) {                    /* 0x7844c0 */
            d->brake = 1.0f; d->gas = 0; d->gear = -1;
        } else {
            float g1 = clamp3((1.0f * d->dd_speed - v + 2.23514f) * 0.111849999f, 0, 1.0f);   /* 0x783a5c 0x7850f8 */
            float g2 = clamp3(v * 0.111849999f, 0, 1.0f);
            d->gas = clamp3(g1 - (E - 0.349065852f) * g2 * 0.9f, 0, 1.0f); /* 0x7850f4 0x7850f0 */
            d->brake = 0; d->gear = -1;
        }
    } else {
        /* slow, or the target behind: creep or reverse at it */
        float at = fabsf(d->dd_speed);
        if (!(2.0f <= at)) { d->brake = 1.0f; d->gas = 0; }
        else {
            float x = at - v;
            if (x <= -1.0f) x = -1.0f;                                  /* 0x7842a0 */
            if (1.0f < x) { d->brake = 0; d->gas = 1.0f; }
            else if (0 <= x) { d->brake = 0; d->gas = x; }
            else { d->brake = -x; d->gas = 0; }
        }
        d->hand = 0;
        d->gear = back ? 0 : -1;
        float a = wrap_turns(tgt - psi);
        if (back) a = -a;
        dd_steer(d, a, dt);
    }
    /* drift-type race, full simulation: the drivetrain's clutch kick (+0x74)
       while the handbrake is past 0.8 */
    if (!w->drag && !w->x890108 && (w->x890114 || w->drift || w->x890104) && c->mover == 1)
        d->clutch_kick = 0.8f < d->hand;                                /* 0x7850ec */
}

static void driver_tick(AiRacer *r, float dt)
{
    AiDriver *d = r->drv;
    if (d->drift) { drift_driver_tick(r, dt); return; }
    const AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    float h = dt <= 0.0001f ? 0.0001f : dt;                             /* 0x7843ec */
    float cur = (float)d->steer * 0.00549316406f;                       /* 0x784248 */
    if (180.0f < cur) cur -= 360.0f;
    float sv = c->absv;
    if (c->speed < 0) sv = -sv;
    driver_target(r);
    int front = 0, driven = 0;
    for (int i = 0; i < 4; i++)
        if (c->contact[i]) {
            if (i < 2) front = 1;
            int dr = c->rear_frac < 0.2f ? i < 2 : (0.8f < c->rear_frac ? i >= 2 : 1);   /* 0x592870 */
            if (dr) driven = 1;
        }
    int mode, trafficlike;
    if (c->mover == 1) { mode = 1; trafficlike = 0; }
    else if (c->mover == 3) { mode = c->ctrl_type == 4 ? 4 : 0; trafficlike = mode == 4; }
    else if (c->mover == 4) { mode = 0; trafficlike = 0; }
    else { mode = 3; trafficlike = 1; }
    d->x248 -= dt;
    if (0 < d->x248) mode = 5;
    if (d->stop) mode = 6;
    float e0, e1, e2;
    driver_errors(r, &e0, &e1, &e2);
    if (d->cmd == 1) {                                                  /* vt+0x60 0x405720 */
        hist_reset(&d->f[0], e0); hist_reset(&d->f[1], e2); hist_reset(&d->f[2], e1);
        d->cmd = 0;
    } else if (d->cmd == 2) {                                           /* vt+0x68 0x4056c0 */
        hist_reset_d(&d->f[0], e0); hist_reset_d(&d->f[1], e2); hist_reset_d(&d->f[2], e1);
        d->cmd = 0;
    }
    float steer, Pst = 0;
    if (front) {
        int nod = d->cmd == 4;
        d->air_since = -1;
        hist_push(&d->f[0], e0, dt, nod, 0);
        hist_push(&d->f[1], e2, dt, nod, 0);
        hist_push(&d->f[2], e1, dt, nod, 0);
        if (nod) d->cmd = 0;
        const Hist *A = &d->f[0], *B = &d->f[2];
        float a = hist_p(A), b = hist_p(B), wa = 1.0f, wb = 0;
        if (!(a * b < 0)) {
            float q = b * b + a * a;
            wa = wb = 0.5f;
            if (!(q < 1e-05f)) { wa = 1.0f / q * a * a; wb = 1.0f / q * b * b; }   /* 0x7845a0 */
        }
        float P = wa * a + wb * b;
        Pst = P;
        float I = hist_i(A) * wa + hist_i(B) * wb;
        float D = hist_d(B) * wb + hist_d(A) * wa;
        if (I <= -0.104719758f) I = -0.104719758f;                      /* 0x81e5ac */
        if (0.104719758f < I) I = 0.104719758f;
        if (D <= -1.0f) D = -1.0f;                                      /* 0x783934 */
        if (1.0f < D) D = 1.0f;
        if (trafficlike || c->mover == 4 || mode == 5)
            steer = -(SKP[mode] * P) - SKD[mode] * D - SKI[mode] * I;
        else {
            float sd = fabsf(D);
            if (D * P < 0) sd = -sd;
            float pd = fabsf(P * RADDEG_F);
            d->sp.in[0] = -P; d->sp.in[1] = -I; d->sp.in[2] = -D;
            d->sp.period = 0.2f;
            for (int k = 0; k < 3; k++) d->sp.thr[k] = 0.01f;
            const GainSet *gs = c->mover == 1 ? (r->suv ? &GS_SUV : &GS_PHYS)
                              : (w->streetx || w->x89010c) ? &GS_SX : &GS_ROAD;
            gains_set(&d->sp, gs);
            apid_update(&d->sp, sd * RADDEG_F - agraph(ADAPT_G, 10, pd), dt, -99999.0f, w->time);
            if (c->speed < 10.0f) {                                     /* 0x7838e4 */
                d->sp.g[0] = acurve(&G0, c->absv);
                d->sp.g[1] = acurve(&G1, c->absv);
                d->sp.g[2] = acurve(&G2, c->absv);
            }
            steer = apid_out(&d->sp);
            if (999.0f < apid_in(&d->sp, 2)) steer = 999.0f * steer;    /* 0x7838f0 / 0x7838f4 */
        }
        /* pinned against a slow car (or a resting object) for over 0.6 s:
           steer away from it */
        for (int i = 0; i < r->ncontact; i++) {
            const AiContact *k = &r->contact[i];
            float age = w->time - k->start;
            if (!(w->time - k->last < 0.1f) || !(0.6f < age)) continue;   /* 0x784358 / 0x783944 */
            if (k->car ? !(k->other_absv < 60.0f * 0.447027266f) : !k->wall) continue;   /* 0x783940 */
            float dx = k->pt[0] - c->pos[0], dy = k->pt[1] - c->pos[1];
            float f[2] = { c->vel[0], c->vel[1] };                      /* 0x591de0 */
            if (c->speed < 0) { f[0] = -f[0]; f[1] = -f[1]; }
            if (!(1.0f < f[0] * f[0] + f[1] * f[1])) {
                float hh = (float)c->heading * U16RAD_F;
                f[0] = cosf(hh); f[1] = sinf(hh);
            } else { float l = sqrtf(f[0] * f[0] + f[1] * f[1]); f[0] /= l; f[1] /= l; }
            float right = f[1] * dx - f[0] * dy;
            float m = (age - 0.6f) / 1.0f;                              /* 0x783948 */
            if (!(m < 1.0f)) m = 1.0f;
            float a = acurve(&MAXST, sv) * DEG_F * m;
            steer = 0 < right ? a : -a;
            break;
        }
    } else {
        if (d->air_since < 0) d->air_since = w->time;
        d->cmd = 0;
        if (w->time - d->air_since <= 0.3f)                             /* 0x783958 */
            steer = wrap_pi((float)d->steer * U16RAD_F);
        else {
            steer = -car_slip_angle(c);
            if (c->mover == 3) {
                float l = 5.0f * DEG_F;                                 /* 0x78393c */
                if (steer <= -l) steer = -l;
                if (l < steer) steer = l;
            }
        }
        const GainSet *gs = (c->mover == 1) ? (r->suv ? &GS_SUV : &GS_PHYS) : &GS_ROAD;
        for (int k = 0; k < 3; k++) if (!(d->sp.g[k] < gs->cap[k])) d->sp.g[k] = gs->cap[k];
    }
    if (!(-0.785398185f < steer)) steer = -0.785398185f;                /* 0x7844a0 */
    else if (0.785398185f < steer) steer = 0.785398185f;
    float s = steer;
    if (d->cmd == 3) { hist_reset(&d->f[3], fabsf(d->vtarget) - c->absv); d->cmd = 0; }   /* vt+0x70 */
    hist_push(&d->f[3], fabsf(d->vtarget) - c->absv, dt, 0, 0);
    driver_slips(r, dt);
    float gas = 0, brake = 0;
    int gear = -1, speedctl = d->stop;
    if (!driven) {
        gas = d->maxgas; brake = 0;
        hist_reset(&d->f[3], 0);                                        /* vt+0x6c */
    } else if (fabsf(d->vtarget) < 2.0f) {                              /* 0x7844c0 */
        brake = 1.0f; gas = 0;
    } else if (0 < d->vtarget) {
        if (sv < -7.0f) d->fwd = 0;                                     /* 0x783a24 + 0x7844c0 */
        else if (-2.0f < sv) d->fwd = 1;                                /* 0x7855d0 */
        if (d->fwd) speedctl = 1;
        else { hist_reset(&d->f[3], 0); steer = -s; brake = 0.9f; gas = 0; }
    } else {
        if (7.0f < sv) d->fwd = 0;
        else if (sv < 2.0f) d->fwd = 1;
        if (!d->fwd) { hist_reset(&d->f[3], 0); brake = 0.9f; gas = 0; }
        else { gear = 0; steer = -s; speedctl = 1; }
    }
    if (speedctl) driver_speed(r, &gas, &brake, mode, dt);
    float maxd = 45.0f;                                                 /* 0x7844d4 */
    if (trafficlike || w->drag || w->x890108 || !(w->x890114 || w->drift || w->x890104)) {
        float m = acurve(&MAXST, sv) * 1.0f;                            /* 0x783a14 */
        maxd = 0;
        if (0 < m) { maxd = m; if (45.0f < maxd) maxd = 45.0f; }
    }
    float want = steer / (maxd * DEG_F), now = cur / maxd;
    float tw = want <= -1.0f ? -1.0f : (1.0f < want ? 1.0f : want);
    float delta = tw - now, nd;
    if (trafficlike) {
        float rr = acurve(&STR_E, fabsf(Pst * RADDEG_F)) * acurve(&STR_V, sv) * 1.0f;   /* 0x783a18 */
        if ((0 < now && tw < now) || (now < 0 && now < tw)) rr *= 2.0f; /* 0x783a1c */
        rr *= h;
        if (delta <= -rr) delta = -rr;
        if (rr < delta) delta = rr;
    } else {
        float rr = 100.0f * h;                                          /* 0x783a20 */
        if (delta <= -rr) delta = -rr;
        if (rr < delta) delta = rr;
    }
    nd = (delta + now) * maxd;
    if (trafficlike) nd *= c->steer_gain;
    d->x234 -= dt;
    if (d->x234 <= 0) d->x234 = 0;
    if (w->race_state == 3) {                                           /* the countdown: rev against the brake */
        gear = 1;
        d->brake = 1.0f;
        d->gas = 1.0f;
    } else if (d->go) { d->gas = gas; d->brake = brake; }
    else { d->gas = 0; d->brake = 0; }
    if (c->nos_level != 0 && !d->nos) d->nos = 1;
    if (d->nos) {
        if (w->drag || w->x890108 || d->vtarget < d->vprev) d->nos = 0;
        else { d->gas = d->maxgas; d->brake = 0; }
    }
    d->hb_time -= dt;
    d->vprev = d->vtarget;
    if (0 < d->hb_time && 8.94054508f < fabsf(sv)) { d->brake = 0; d->hand = 1.0f; d->gas = 0; }   /* 0x784568 */
    else d->hand = 0;
    d->gear = gear;
    int a16 = (int)(nd * 65536.0f);
    d->steer = (unsigned short)(a16 / 360);
    if (d->stop) { d->brake = 1.0f; d->hand = 1.0f; d->gas = 0; }       /* 0x78394c / 0x783950 (grip x1.4 0x783954) */
}

/* ---- the controller (AI racer 0x4260b0 -> 0x420500) ----------------------- */
/* 0x411dd0: a lane's speed at path distance d, between the points around it */
static float fl_speed_at(const FList *L, int lane, float d)
{
    int i = fl_nearest_dist(L, d, 2, 0, 0);
    if (i == -1) return L->n < 1 ? 0 : fp_speed(L->p[L->n - 1], lane);
    const FPoint *A = L->p[i];
    for (int j = i + 1; j < L->n; j++) {
        const FPoint *B = L->p[j];
        if (B->sec == A->sec && B->pt == A->pt) continue;
        float base = A->dist - A->off, va = fp_speed(A, lane), vb = fp_speed(B, lane);
        return (vb - va) * ((d - base) / (B->dist - base)) + va;
    }
    return fp_speed(A, lane);
}
/* goal "Win road race" vt+0x44 / vt+0x34 (0x41c5e0): the driver's target
   speed: the lane's speed a random few metres from the car (per skill,
   re-rolled every blob[0xad4] s), x the skill's scale, x the catch-up */
static float offset_limit(const AiRacer *r);                           /* aiencounter.c, 0x40dc00 */
static int racer_rank(const AiRacer *r);                                /* RacingCar +0xa */
static void goal_speed(AiRacer *r)
{
    if (r->goal == GOAL_TRAFFIC) { traffic_goal_speed(r); return; }    /* 0x4204e0 -> 0x41c380 */
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    float spd, x = 0, y = 0, cap = 99999.0f;
    int go = 1;
    if (r->goal == GOAL_PARK || r->goal == GOAL_PURSUIT) {
        /* Park (0x41c990): the lane's speed a random few metres on, x the
           skill's scale -- without the road race's link / drag / street X /
           outrun cases -- and its vt+0x4c (0x401d30) caps the speed at 0 */
        int skill = r->skill;
        float lim;
        if (r->last_kind2_link == -1) {
            if (r->link_time == 0) {                                    /* +0x754 */
                if (0 <= skill && skill < 10 && *blob_f(w, 0xad4) < w->time - r->rand_time) {
                    float rr = *blob_f(w, skill * 0x68 + 0x584);
                    r->rand_off = ai_rand(w, rr + rr) - rr;
                    r->rand_time = w->time;
                }
                float pp = 0, lat;
                if (r->nent == 0) { if (r->L.n) fl_locate(n, &r->L, r->car.pos, &pp, &lat, r->ls.lane); }
                else if (r->own >= 0) pp = r->ent[r->own].pathpos;
                float v = fl_speed_at(&r->L, r->ls.lane, r->rand_off + pp);
                if (fabsf(v) < 0.001f) v = 30.0f;                       /* 0x784478 */
                lim = v * *blob_f(w, (skill < 1 ? 0 : skill) * 0x68 + 0x538);
            } else lim = 99999.0f;                                      /* 0x784360 */
        } else {
            Pt *p = lk_pt(n, n->links + r->last_kind2_link * 0x20);
            lim = (p && r->ls.lane >= 0) ? pt_lanespeed(p, r->ls.lane) : 0;   /* 0x401ca0 */
        }
        cap = r->catchup * lim;
        const AiAction *a = r->act;
        if (!r->rpt) spd = 0;
        else {
            if (a && !a->use_cap) { spd = a->speed; x = a->x1f4; }
            else spd = cap;
            if (a) { go = a->go; y = a->x1f8; }
        }
        float glim = r->goal == GOAL_PURSUIT ? offset_limit(r) : 0.0f;  /* vt+0x4c 0x40dc00 / 0x401d30 */
        if (glim < spd) spd = glim;
        if (cap < spd) spd = cap;
        driver_setspeed(r->drv, spd, x, y, go);
        return;
    }
    if (!r->rpt) spd = 0;
    else {
        int skill = r->skill;
        float lim;
        if (r->last_kind2_link == -1) {
            lim = 99999.0f;
            int linked = r->link_time != 0;                             /* 0x401fa0 */
            if (!linked && (!(w->drag || w->x890108) || !r->in_race)) {
                if (0 <= skill && skill < 10 && *blob_f(w, 0xad4) < w->time - r->rand_time) {
                    float rr = *blob_f(w, skill * 0x68 + 0x584);
                    r->rand_off = ai_rand(w, rr + rr) - rr;
                    r->rand_time = w->time;
                }
                float pp = 0, k = 1.0f, lat;
                if (r->nent == 0) { if (r->L.n) fl_locate(n, &r->L, r->car.pos, &pp, &lat, r->ls.lane); }
                else if (r->own >= 0) pp = r->ent[r->own].pathpos;
                float q = pp;
                int sc = skill < 1 ? 0 : skill;
                if (w->streetx || w->x89010c) k = *blob_f(w, sc * 0x68 + 0x540);
                else if (w->mode == 7) {
                    /* the Outrun (0x41c7e4): the leader 0.84 of the lane's
                       speed, the others 1.0 (0x78357c / 0x783598, equal at
                       every stage), no random offset */
                    k = racer_rank(r) == 1 ? 0.84f : 1.0f;
                } else {
                    q = r->rand_off + pp;
                    k = *blob_f(w, sc * 0x68 + 0x538);
                }
                float v = fl_speed_at(&r->L, r->ls.lane, q);
                if (fabsf(v) < 0.001f) v = 30.0f;                       /* 0x784478 */
                lim = v * k;
            }
        } else {
            Pt *p = lk_pt(n, n->links + r->last_kind2_link * 0x20);
            lim = (p && r->ls.lane >= 0) ? pt_lanespeed(p, r->ls.lane) : 0;   /* 0x401ca0 */
        }
        cap = r->catchup * lim;
        /* the action's speed (+0x1f0, +0x1f4) unless it asks for the capped
           one (+0x1fc); its +0x1f8 and go flag (+0x1fd) */
        const AiAction *a = r->act;
        if (a && !a->use_cap) { spd = a->speed; x = a->x1f4; }
        else spd = cap;
        if (a) { go = a->go; y = a->x1f8; }
    }
    if (!(spd < 1e9f)) spd = 1e9f;                                      /* vt+0x4c 0x40ddd0 */
    if (cap < spd) spd = cap;
    driver_setspeed(r->drv, spd, x, y, go);
}
/* 0x420a60: five waypoints straight ahead over `dist` (no road yet) */
static void plan_straight(AiPlan *P, float dist, const AiCarView *c)
{
    float d[3];
    float l = sqrtf(c->vel[0] * c->vel[0] + c->vel[1] * c->vel[1] + c->vel[2] * c->vel[2]);
    if (!(l < 1.0f)) { d[0] = c->vel[0] / l; d[1] = c->vel[1] / l; d[2] = c->vel[2] / l; }
    else { float h = (float)c->heading * U16RAD_F; d[0] = cosf(h); d[1] = sinf(h); d[2] = 0; }
    plan_keep(P, NULL, 0, 0);
    dist = dist * 0.2f;                                                 /* 0x784554 */
    for (int i = 0; i < 5; i++) {
        float q[3] = { c->pos[0] + d[0] * (float)i * dist, c->pos[1] + d[1] * (float)i * dist, c->pos[2] + d[2] * (float)i * dist };
        plan_add(P, q, -1, -1, 255, 0, NULL);
    }
    P->valid = 0;
}
/* 0x41d0e0: forget link decisions whose link left the path */
static void linkmem_tidy(AiRacer *r)
{
    const AiNet *n = r->w->net;
    for (int i = 0; i < 16; i++) {
        uint32_t m = r->linkmem[i];
        if (!(m & 1) || (m & 4)) continue;
        Lk *l = n->links + (m >> 4 & 0x1fff) * 0x20;
        int s = rds16(l + 10), k = rds16(l + 6);                        /* 0x5d3f80 */
        if (s >= 0 && fl_find(&r->L, s, k) != -1) continue;
        r->linkmem[i] &= ~1u;
    }
}
/* 0x4023e0 + vt+0x38(0): stop where it is */
static void racer_stop(AiRacer *r)
{
    driver_setspeed(r->drv, 0, 0, 0, 1);
    r->finished = 1;                                                    /* +0x710 */
}
/* 0x40abb0: the path ends at the route's end */
static int fl_atend(const AiNet *n, const FList *L, float pathpos)
{
    int i = fl_nearest_dist(L, pathpos, 1, 0, 0), left = 0;
    const FPoint *f = NULL;
    if (i != -1) { f = L->p[i]; left = L->n - i; }
    if (!L->bit21) {
        if (!f) return 0;
        const unsigned char *s = (f->sec >= 0 && f->pt != -1) ? n->sec[f->sec] : NULL;
        if (!s || rd32(s + 0xc) != 1) return 0;
    }
    return left <= 1;
}
/* ---- the catch-up (vt+0x80, 0x4172d0) ------------------------------------ */
static int racer_own_seen(const AiRacer *r)
{
    for (int i = 0; i < r->w->nseen; i++) if (r->w->seen[i].racer == r) return i;
    return -1;
}
static float skill_lerp(float a, float b, int skill) { return (b - a) * ((float)skill / 9.0f) + a; }   /* 0x576c60(0,a,9,b) */
/* 0x4084a0: the gap term (catch-up curves by gap, a pull from the speed
   difference) and the rank term, against the target human */
static void catchup_terms(const AiRacer *r, const AiSeen *me, const AiSeen *tg, float *gap_t, float *rank_t)
{
    const AiWorld *w = r->w;
    *gap_t = 0; *rank_t = 0;
    if (!w->catchup_opt) return;
    /* 0x4084a0: the Outrun's gaps behind the leader (RacingCar +0x24, in
       race_dist in mode 7), else the race distance */
    float gap = w->mode == 7 ? tg->race_dist - me->race_dist : (me->race_dist - tg->race_dist) * w->track_len;
    if (!(gap <= *blob_f(w, 0xadc))) return;
    const unsigned char *A, *B;
    if (!(w->streetx || w->x89010c)) {
        static const int LAPS[2] = { 0, 4 };                            /* 0x7e95c8 */
        int k6 = 0;
        if (!w->sprint) for (int i = 1; i >= 0; i--) if (LAPS[i] <= w->laps) { k6 = i; break; }
        int k5 = w->traffic_level == 2 || w->traffic_level == 3;
        A = w->blob + (k5 + k6 * 2) * 0x58;
        B = w->blob + (k5 + 4 + k6 * 2) * 0x58;
    } else { A = w->blob + 0x2c0; B = w->blob + 0x318; }
    int skill = r->skill < 1 ? 0 : r->skill;
    float g = skill_lerp(agraph((const float *)A, 11, gap), agraph((const float *)B, 11, gap), skill);
    if (w->mode == 7 && me->race_pos != 1) g *= 1.5f;                   /* 0x783b78 */
    float vme = me->v->mover == 4 ? me->v->speed : me->v->absv;
    float pull = ((vme - tg->v->absv) * -0.005f) / (0.01f * gap * gap + 1.0f);
    if (pull <= -0.05f) pull = -0.05f;
    if (0.05f < pull) pull = 0.05f;
    *gap_t = pull + g;
    if (!(w->streetx || w->x89010c) || w->blob[0x420]) {
        int d = tg->race_pos - me->race_pos;
        int idx = d < 0 ? d + 8 : d + 7;
        if (idx < 0) idx = 0;
        if (idx > 15) idx = 15;
        *rank_t = skill_lerp(*blob_f(w, 0x424 + idx * 4), *blob_f(w, 0x464 + idx * 4), skill);
    }
}
static void racer_catchup(AiRacer *r)
{
    AiWorld *w = r->w;
    float m = 1.0f;
    int me_i = racer_own_seen(r);
    const AiSeen *me = me_i >= 0 ? &w->seen[me_i] : NULL;
    int allow = r->allow_catchup && !r->human && !r->car_player && r->in_race && me;   /* car +0x18 none */
    if (w->drag || w->x890108 || w->drift || w->x890104) allow = 0;
    if (w->mode == 0) { m = 0.75f; goto out; }                           /* 0x783b74 */
    if ((w->mode == 7 && me && me->race_pos == 1) || !allow) goto out;
    int act = r->act && r->act->catchup;
    int skill = r->skill < 1 ? 0 : r->skill;
    if (w->time - r->catch_time <= *blob_f(w, 0xad8)) { m = r->catchup; goto out; }
    r->catch_time = w->time;
    r->catch_target = -1;
    const AiSeen *tg = NULL;
    if (0 < w->humans && act && *blob_f(w, skill * 0x68 + 0x580) <= w->race_time) {
        for (int i = 0; i < w->nseen; i++) if (w->seen[i].human && w->seen[i].active) { tg = &w->seen[i]; r->catch_target = i; break; }
        if (tg) {
            int split = w->streetx || w->x89010c;                       /* 0x401930 */
            float weight = (!split || w->blob[0x421]) ? w->catchup_weight : 1.0f;
            float gt, rt;
            catchup_terms(r, me, tg, &gt, &rt);
            m = weight * rt + 1.0f + gt;
        }
    }
    if (act && tg && (!(w->streetx || w->x89010c) || w->blob[0x422]) && w->catchup_opt) {
        /* the human's pace against the racing line (0x5eb200: its ring), by
           the skill, floored per skill */
        int split = w->streetx || w->x89010c;
        float floor = *blob_f(w, skill * 0x68 + (split ? 0x544 : 0x53c));
        float v = tg->racer && (tg->racer->human || tg->racer->car_player) ? tg->racer->pace.avg : 1.0f;   /* (no tracker: the harness's neutral) */
        const float *g = blob_f(w, (v < 1.0f ? 0x4cc : 0x49c) + tg->race_pos * 8);
        float f = (g[1] - g[0]) * ((float)skill / 9.0f) + g[0];        /* 0x413390 2 points over 0..9 */
        float x = (v - 1.0f) * f + 1.0f;
        m = (x < floor ? floor : x) * m;                                /* 0x401050 max */
    }
    if (act && tg && (!(w->streetx || w->x89010c) || w->blob[0x423])) {
        int laps = w->laps < 2 ? 1 : w->laps;
        m = agraph(blob_f(w, 0x504), 4, me->race_dist / (float)laps) * (m - 1.0f) + 1.0f;
    }
out:
    r->car_catchup = m <= 1.0f ? 1.0f : m;
    r->catchup = m;
}

static void act_nitrous(AiRacer *r, AiAction *a);
static void racer_apply(AiRacer *r);

/* ---- the cars around, through the racers table ---------------------------- */
/* 0x89e970[car]: the car with that index */
static AiSeen *seen_car(AiWorld *w, int car)
{
    if (car < 0) return NULL;
    for (int i = 0; i < w->nseen; i++) if (w->seen[i].v && w->seen[i].index == car) return &w->seen[i];
    return NULL;
}
static const RacerEnt *own_ent(const AiRacer *r) { return r->own >= 0 && r->own < r->nent ? &r->ent[r->own] : NULL; }
/* 0x60c120: the car's entry in the table */
static const RacerEnt *ent_of(const AiRacer *r, int car)
{
    if (car < 0) return NULL;
    for (int i = 0; i < r->nent; i++) if (r->ent[i].car == car) return &r->ent[i];
    return NULL;
}
/* 0x60bff0: how far the entry is ahead of the car along its path */
static float ent_ahead(const AiRacer *r, const RacerEnt *e)
{
    const RacerEnt *o = own_ent(r);
    return e->pathpos - (o ? o->pathpos : 0);
}
static float own_ext(const AiRacer *r, int k) { const RacerEnt *o = own_ent(r); return o ? o->ext[k] : 0; }
static float own_lat(const AiRacer *r, int lane) { const RacerEnt *o = own_ent(r); return o ? ent_lat(o, lane) : 0; }
static int seen_human(const AiSeen *s) { return s && s->kind == 1; }    /* 0x614880 */
static int seen_traffic(const AiSeen *s) { return s && s->kind == 3; }  /* 0x6148c0 */
static int seen_in_race(const AiSeen *s) { return s && s->in_race; }    /* 0x60bea0 */
static int world_splitlike(const AiWorld *w) { return w->drift || w->x890104 || w->streetx || w->x89010c; }
/* 0x5eeff0 (on the race manager): the start's grace -- AI racers 100..105
   leave the others alone for 12 s less half a second per id after the start */
static int race_grace(const AiRacer *r)
{
    const AiWorld *w = r->w;
    if (w->drag || w->x890108 || w->event == 1099) return 0;
    if (r->id < 100 || r->id > 105 || w->race_start < 0) return 0;
    return w->time - w->race_start <= (float)(100 - r->id) * 0.5f + 12.0f;   /* 0x80227c / 0x802278 */
}
/* 0x5ed580: seconds since the car was last put down (car +0x9c0) */
static float since_reset(const AiRacer *r) { return r->reset_time < 0 ? 99999.0f : r->w->time - r->reset_time; }
/* 0x5ec3a0: the car's acceleration along its nose */
static float car_acc_fwd(const AiCarView *c) { return c->acc[0] * c->fwd[0] + c->acc[1] * c->fwd[1] + c->acc[2] * c->fwd[2]; }
/* 0x403a40: can the car close `dist` on the other within 3 s from the
   relative speed and both accelerations */
static int can_close(const AiRacer *r, const AiSeen *o, float dist, float relv)
{
    const AiCarView *c = &r->car;
    float am = car_acc_fwd(c);
    float dv = (c->fwd[0] * o->v->fwd[0] + c->fwd[1] * o->v->fwd[1] <= 0) ? o->acc + am : am - o->acc;   /* 0x60be30 */
    return dist < -((3.0f * dv * 0.5f + relv) * 3.0f);                 /* 0x7e8e0c */
}
/* 0x403820: the entry is not behind the car (a human may be half a metre
   behind), and is not the car itself */
static int ent_not_behind(const AiRacer *r, const RacerEnt *e)
{
    if (!e || e->self) return 0;
    const AiSeen *o = seen_car(r->w, e->car);
    float thr = seen_human(o) ? -0.5f : 0.0f;                           /* 0x783790 / 0x783794 */
    return -(own_ext(r, 1) + e->ext[1] + thr) < ent_ahead(r, e);
}
/* 0x40cc40: how far q lies right of the route point, along the way the
   car travels it */
static float pt_latq(const AiNet *n, Pt *p, const float *q, int back)
{
    float d[2] = { pt_dx(p), pt_dy(p) };
    if (back) pt_backdir(n, p, d);
    return (q[0] - pt_x(p)) * d[1] - (q[1] - pt_y(p)) * d[0];
}
/* the FuturePoint's lane limits (+0x18.. / +0x2c..) */
static float fp_lo(const FPoint *f, int lane) { int s = lslot(lane); return s < 0 ? 0 : f->lo[s]; }
static float fp_hi(const FPoint *f, int lane) { int s = lslot(lane); return s < 0 ? 0 : f->hi[s]; }

/* ---- contacts (the collision pair list 0x86b490) -------------------------- */
/* 0x402a40: where a contact touches the car, in its frame of travel
   (0x591de0): 0 front, 1 rear (within 95 % of its width), 2 / 3 a side */
static int contact_side(const AiRacer *r, const AiContact *k)
{
    const AiCarView *c = &r->car;
    float f[2] = { c->vel[0], c->vel[1] };
    if (c->speed < 0) { f[0] = -f[0]; f[1] = -f[1]; }
    if (!(1.0f < c->vel[0] * c->vel[0] + c->vel[1] * c->vel[1] + c->vel[2] * c->vel[2])) {
        float h = (float)c->heading * U16RAD_F;
        f[0] = cosf(h); f[1] = sinf(h);
    } else v2norm(f, f);
    float dx = k->pt[0] - c->pos[0], dy = k->pt[1] - c->pos[1];
    float lat = f[1] * dx - f[0] * dy, along = f[0] * dx + f[1] * dy;
    float hw = r->halfwidth;
    if (lat < 0.95f * hw && -0.95f * hw < lat) return 0 < along ? 0 : 1;   /* 0x78366c */
    return lat <= 0 ? 2 : 3;
}
/* a contact lasting 0.4 s, touched within the last 0.25 s, with the car
   slower than 5 m/s (0x7836e4 / 0x784d28 / 0x7836e8) */
static int contact_pinned(const AiRacer *r, const AiContact *k)
{
    const AiWorld *w = r->w;
    float v = r->car.absv;
    return 0.4f <= w->time - k->start && w->time - k->last < 0.25f && (v < 5.0f || fabsf(v - 5.0f) < 0.001f);
}
/* 0x402ae0: the car it is in contact with (in the race) whose pair has
   taken at least `thr` of impulse (pair +0x60), a human player first */
static int hit_car_thr(const AiRacer *r, float thr, int need_in_race)
{
    AiWorld *w = r->w;
    const RacerEnt *best = NULL;
    for (int i = 0; i < r->ncontact; i++) {
        const AiContact *k = &r->contact[i];
        if (!(thr <= k->j)) continue;
        if (!k->car) continue;                                          /* 0x592c60; props: 0x404800 is 0 */
        if (r->goal == GOAL_PURSUIT && k->other == r->op_target) continue;   /* vt+0x84 0x41bea0 */
        const RacerEnt *e = ent_of(r, k->other);                        /* 0x60c0f0 */
        if (!e) continue;
        const AiSeen *o = seen_car(w, k->other);
        if (need_in_race && !seen_in_race(o)) continue;
        if (!best) { best = e; continue; }
        int hn = seen_human(o), hb = seen_human(seen_car(w, best->car));
        if (hn && !hb) best = e;
        else if (hn == hb) {
            float b = ent_ahead(r, best), d = ent_ahead(r, e);
            if ((b < 0 && 0 < d) || d < b) best = e;
        }
    }
    return best ? best->car : -1;
}
static int hit_car(const AiRacer *r, int need_in_race) { return hit_car_thr(r, -1.0f, need_in_race); }

/* 0x40df50: the car against its route point's road edges. 1: heading off
   the road (or off it) within a second, 2: reversing while facing back
   along the road, 0: fine. With `ahead`, a driver's target in front of
   the car clears a 1. */
static int road_check(AiRacer *r, int ahead)
{
    const AiNet *n = r->w->net;
    const AiCarView *c = &r->car;
    Pt *p = r->rpt;
    if (!p) return 0;
    int back = !r->rdir;
    if (fabsf(c->absv) <= 1.0f) {
        float h = (float)c->heading * U16RAD_F;
        float q[2] = { c->pos[0] + cosf(h) * 2.0f, c->pos[1] + sinf(h) * 2.0f };
        float dx = pt_dx(p), dy = pt_dy(p);
        float ln = (c->pos[0] - pt_x(p)) * dy - (c->pos[1] - pt_y(p)) * dx;
        float la = (q[0] - pt_x(p)) * dy - (q[1] - pt_y(p)) * dx;
        float wl = pt_wl(p), wr = pt_wr(p);
        float rnow = wr - ln, rq = wr - la, lnow = wl + ln, lq = wl + la;
        if (!(rq < 0) || !(rq - rnow < 0)) {
            if (!(lq < 0)) return 0;
            if (!(lq - lnow < 0)) return 0;
        }
    } else {
        float q[2] = { c->pos[0] + c->vel[0] * 0.1f, c->pos[1] + c->vel[1] * 0.1f };   /* 0x78365c */
        float ln = pt_latq(n, p, c->pos, back), lq = pt_latq(n, p, q, back);
        float A = back ? pt_wr(p) : pt_wl(p), B = back ? pt_wl(p) : pt_wr(p);
        float rnow = B - ln, lnow = A + ln, rq = B - lq, lqm = A + lq;
        float rate = (lqm - lnow) / 0.1f, t;
        if (rate <= 0.001f || rnow <= 0 || lnow <= 0) {
            t = 99999.0f;
            if (0.001f < rate && 0 < lnow && 0 < rnow) t = lnow / rate;
        } else t = rnow / rate;
        if (1.0f < t && (!(rnow < 0) || !(rq < rnow))) {
            if (!(lnow < 0)) return 0;
            if (!(lqm < lnow)) return 0;
        }
        if (c->speed <= 0) {
            float a[2] = { c->fwd[0], c->fwd[1] }, b[2] = { pt_dx(p), pt_dy(p) };
            v2norm(a, a); v2norm(b, b);
            return a[0] * b[0] + a[1] * b[1] <= 0.342f ? 2 : 0;         /* 0x783668 */
        }
    }
    if (ahead && car_ahead(c, r->drv->tgt, 0.5f)) return 0;             /* driver vt+0x80, 0x4036e0 (0x783664) */
    return 1;
}

/* ---- predicting collisions along the plans (0x428ce0 / 0x4275e0) ---------- */
/* a box along a plan (0x50 bytes, 0x4040a0): at each waypoint, along the
   segment that leads to it (the first one along the car) */
typedef struct { float p[2], x[2], y[2], ext[2]; } Obb;
static int obb_track(const AiPlan *P, Obb *o, float hl, float hw, const float dir0[2], float scale)
{
    float d[2];
    v2norm(d, dir0);
    int n = P->n < 1 ? 0 : P->n;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            float s[2] = { P->p[i - 1][0] - P->p[i][0], P->p[i - 1][1] - P->p[i][1] };
            v2norm(d, s);
        }
        o[i].p[0] = P->p[i][0]; o[i].p[1] = P->p[i][1];
        o[i].x[0] = d[0]; o[i].x[1] = d[1];
        o[i].y[0] = -d[1]; o[i].y[1] = d[0];
        o[i].ext[0] = fabsf(hl) * scale; o[i].ext[1] = fabsf(hw) * scale;
    }
    return n;
}
/* 0x4043f0: when the projections on an axis overlap */
static void axis_window(float pv, float pp, float r, float *t0, float *t1)
{
    if (!(fabsf(pv) < 0.0001f)) {                                       /* 0x7843ec */
        float k = 1.0f / pv;
        *t1 = (r - pp) * k;
        *t0 = -((pp + r) * k);
        if (pv < 0) { float t = *t0; *t0 = *t1; *t1 = t; }
        return;
    }
    if (r < fabsf(pp)) { *t0 = 9999.0f; *t1 = 9999.0f; return; }
    *t0 = -9999.0f; *t1 = 9999.0f;
}
/* 0x404240: the overlap of two windows */
static int window_meet(float a0, float a1, float b0, float b1, float *o0, float *o1)
{
    float v[4] = { a0, a1, b0, b1 };
    for (int pass = 3; pass > 0; pass--)
        for (int i = 0; i < pass; i++) if (v[i + 1] < v[i]) { float t = v[i]; v[i] = v[i + 1]; v[i + 1] = t; }
    if (a0 <= v[1] && v[1] < a1 && b0 <= v[1] && v[1] < b1) { *o0 = v[1]; *o1 = v[2]; return 1; }
    return 0;
}
/* 0x4044c0: when box B, at rp from A and moving at rv relative to it,
   first touches A (separating axes); 0 already touching, 99999 never */
static float obb_sweep(const Obb *A, const Obb *B, const float rp[2], const float rv[2])
{
    float xx = A->x[0] * B->x[0] + A->x[1] * B->x[1], xy = A->x[0] * B->y[0] + A->x[1] * B->y[1];
    float yx = A->y[0] * B->x[0] + A->y[1] * B->x[1], yy = A->y[0] * B->y[0] + A->y[1] * B->y[1];
    float a0, a1, b0, b1, c0, c1, d0, d1, s0, s1;
    axis_window(rv[0] * A->y[0] + rv[1] * A->y[1], rp[0] * A->y[0] + rp[1] * A->y[1],
                fabsf(yy * B->ext[1]) + fabsf(yx * B->ext[0]) + fabsf(A->ext[1]), &a0, &a1);
    axis_window(A->x[0] * rv[0] + rv[1] * A->x[1], A->x[0] * rp[0] + rp[1] * A->x[1],
                fabsf(xy * B->ext[1]) + fabsf(xx * B->ext[0]) + fabsf(A->ext[0]), &b0, &b1);
    axis_window(B->y[0] * rv[0] + B->y[1] * rv[1], B->y[0] * rp[0] + B->y[1] * rp[1],
                fabsf(yy * A->ext[1]) + fabsf(xy * A->ext[0]) + fabsf(B->ext[1]), &c0, &c1);
    axis_window(rv[0] * B->x[0] + rv[1] * B->x[1], rp[0] * B->x[0] + rp[1] * B->x[1],
                fabsf(yx * A->ext[1]) + fabsf(xx * A->ext[0]) + fabsf(B->ext[0]), &d0, &d1);
    if (!window_meet(a0, a1, b0, b1, &s0, &s1)) return 99999.0f;
    if (!window_meet(s0, s1, c0, c1, &s0, &s1)) return 99999.0f;
    if (!window_meet(s0, s1, d0, d1, &s0, &s1)) return 99999.0f;
    if (s0 <= 0) {
        if (s0 < 0 && 0 < s1) return 0;
        return 99999.0f;
    }
    return s0;
}
/* 0x4275e0: drive both cars along their plans (each at its speed and
   acceleration, waypoint by waypoint) and sweep their boxes against each
   other; 1 and the time when they meet within `horizon` seconds */
static int sweep_hit(AiRacer *r, const AiPlan *mine, const AiSeen *o, float horizon, float *out)
{
    AiWorld *w = r->w;
    const AiCarView *c = &r->car, *ov = o->v;
    AiPlan A = *mine, B;
    if (o->plan) B = *o->plan;
    else memset(&B, 0, sizeof B);
    float s1 = 1.0f, s2 = 1.0f;                                         /* 0x783798, human 0x7837a0 */
    if (seen_traffic(o)) s2 = 1.3f;                                     /* 0x7837a8 */
    float v1 = c->absv, v2 = sqrtf(ov->vel[0] * ov->vel[0] + ov->vel[1] * ov->vel[1] + ov->vel[2] * ov->vel[2]);
    float a1 = car_acc_fwd(c), a2 = o->acc;
    float look = 3.0f * c->absv;                                        /* 0x425910: vt+0x60 x |v| */
    if (c->speed < 0) {
        int t = r->act ? r->act->type : ACT_CHOOSE;
        if (t == ACT_UNSTUCK || t == ACT_CHOOSE || t == ACT_ACCIDENT || A.n == 0) {
            plan_straight(&A, look <= 125.0f ? 125.0f : look, c);
            v1 = fabsf(v1); s1 = 1.0f;                                  /* 0x78379c */
        }
    }
    if (ov->vel[0] * ov->fwd[0] + ov->vel[1] * ov->fwd[1] + ov->vel[2] * ov->fwd[2] < 0 &&
        (o->kind == 2 || o->kind == 3) && o->racer) {
        int t = o->racer->act ? o->racer->act->type : ACT_CHOOSE;
        if (t == ACT_UNSTUCK || t == ACT_CHOOSE || t == ACT_ACCIDENT || B.n == 0) {
            plan_straight(&B, lookahead(o->racer), ov);                 /* 0x425980, 0x60bf10 */
            v2 = fabsf(v2); s2 = 1.0f;
        }
    }
    if (v1 <= 0.001f) v1 = 0.001f;
    if (v2 <= 0.001f) v2 = 0.001f;
    if (v1 < 2.0f) s1 = 1.0f;                                           /* 0x7844c0 / 0x7837a4 */
    if (v2 < 2.0f) s2 = 1.0f;
    Obb OA[PLAN_MAX], OB[PLAN_MAX];
    float h = (float)c->heading * U16RAD_F, d1v[2] = { cosf(h), sinf(h) }, d2v[2] = { ov->fwd[0], ov->fwd[1] };
    int na = obb_track(&A, OA, r->half_len, r->halfwidth, d1v, s1);
    int nb = obb_track(&B, OB, o->half[0], o->half[1], d2v, s2);
    int i = plan_cur(&A, w->time, c->pos, c->vel), j = plan_cur(&B, w->time, ov->pos, ov->vel);
    float best = 99999.0f, el = 0;
    float p1[2] = { c->pos[0], c->pos[1] }, p2[2] = { ov->pos[0], ov->pos[1] };
    while (i < na) {
        if (nb <= j || horizon < el) break;
        float d1[2] = { OA[i].p[0] - p1[0], OA[i].p[1] - p1[1] }, d2[2] = { OB[j].p[0] - p2[0], OB[j].p[1] - p2[1] };
        float l1 = v2len(d1[0], d1[1]), l2 = v2len(d2[0], d2[1]);
        float t1 = l1 / v1, dt = l2 / v2;
        if (t1 < dt) dt = t1;
        float rp[2] = { p2[0] - p1[0], p2[1] - p1[1] }, u1[2], u2[2];
        v2setlen(u1, d1, v1); v2setlen(u2, d2, v2);
        float rv[2] = { u2[0] - u1[0], u2[1] - u1[1] };
        float t = obb_sweep(&OA[i], &OB[j], rp, rv);
        if (t <= dt && 0 <= t) { best = t + el; break; }
        if (1e-05f < fabsf(l1)) { float k = dt * v1 / l1; p1[0] += d1[0] * k; p1[1] += d1[1] * k; }   /* 0x7845a0 */
        v1 = dt * a1 + v1;
        if (v1 <= 0.001f) v1 = 0.001f;
        if (1e-05f < fabsf(l2)) { float k = dt * v2 / l2; p2[0] += d2[0] * k; p2[1] += d2[1] * k; }
        v2 = dt * a2 + v2;
        if (v2 <= 0.001f) v2 = 0.001f;
        if (fabsf(t1 - dt) > 0.001f) j++;
        else i++;
        el += dt;
    }
    *out = best;
    return best < horizon && fabsf(best - 99999.0f) > 0.001f;
}
/* 0x428ce0: the soonest of the cars not behind it the plan runs into
   within `horizon` (-1 none; *t untouched then) */
static int predict_hit(AiRacer *r, const AiPlan *P, float horizon, float *t)
{
    AiWorld *w = r->w;
    if (!(0 < P->n && P->n < 21)) return -1;
    int who = -1;
    float best = 99999.0f;
    for (int i = 0; i < r->nent; i++) {
        const RacerEnt *e = &r->ent[i];
        /* ctrl vt+0x84 (0x41bea0): the goal's own target car -- none for a
           road race, OffsetPursuit's target (0x503a30) */
        if (r->goal == GOAL_PURSUIT && e->car == r->op_target) continue;
        if (!ent_not_behind(r, e)) continue;
        const AiSeen *o = seen_car(w, e->car);
        if (!o) continue;
        float tt = 1000.0f;                                             /* 0x449a4000 */
        if (sweep_hit(r, P, o, horizon, &tt) && tt < best) { who = e->car; best = tt; }
    }
    *t = best;
    return who;
}
/* 0x428df0: the plan for a lane state and whether it runs into nobody
   within `horizon` */
static int lane_try(AiRacer *r, const AiLane *ls, AiPlan *P, float horizon, int *who, float *t)
{
    *who = -1; *t = 99999.0f;
    plan_build(r, ls, P);
    *who = predict_hit(r, P, horizon, t);
    if (*who >= 0 && *t <= horizon) return 0;
    return 1;
}

/* ---- the actions ----------------------------------------------------------- */
static Pool g_act_pool = { sizeof(AiAction), NULL, 0 };
static AiAction *act_new(int type, int prio, float dur)                 /* 0x4234a0 / 0x40c960 */
{
    AiAction *a = (AiAction *)pool_take(&g_act_pool);
    if (!a) return NULL;
    a->type = type; a->prio = prio;
    a->limited = 0.0f <= dur; a->dur = dur;
    a->use_cap = 1; a->catchup = 1; a->go = 1;                          /* +0x1fc / +0x1fe / +0x1fd */
    a->target = -1;
    return a;
}
static void act_free(AiAction *a) { pool_give(&g_act_pool, a); }
/* the controller's vt+0x9c (0x408280): the skill approves a move against a
   car with a probability (blob skill +0x530); a refusal holds for 1 s */
static int racer_approve(AiRacer *r, int car)
{
    AiWorld *w = r->w;
    if (1.0f <= w->time - r->approve_time) r->approve_car = -1;          /* 0x783b44 */
    if (car == r->approve_car) return 0;
    float x = ai_rand(w, 1.0f);
    int skill = r->skill == -2 ? 0 : r->skill;
    if (skill < 0) skill = 0;
    if (x < *blob_f(w, skill * 0x68 + 0x530)) return 1;
    r->approve_car = car;
    r->approve_time = w->time;
    return 0;
}
/* 0x40d5c0: a moment with a human player: the AI may sound its horn (and
   flash) at it -- kept as the car's horn request (car +0x92c..+0x938) */
static void racer_react(AiRacer *r, int car)
{
    AiWorld *w = r->w;
    const AiSeen *o = seen_car(w, car);
    if (!seen_human(o)) return;
    const RacerEnt *e = ent_of(r, car);
    if (!e) return;
    if (r->car.ctrl_type == 3) { if (!ent_not_behind(r, e)) return; }
    else if (r->car.ctrl_type != 4) return;
    float on = ai_rand(w, 0.4f) + 0.3f - 0.2f;                          /* 0x783524 / 0x783520 */
    float off = ai_rand(w, 0.2f) + 0.15f - 0.1f;                        /* 0x78352c / 0x783528 */
    float on2 = ai_rand(w, 0.4f) + 0.3f - 0.2f;
    int two = ai_rand(w, 1.0f) < 0.5f, flash = 0, lng = 0;
    float p;
    if (e->rv[1] <= 50.0f) {                                            /* 0x783530 */
        if (r->owner_kind == 3 && e->rv[1] < -3.0f) p = 1.0f;           /* 0x783534 */
        else p = 0.75f;                                                 /* (the other is a human) */
    } else { p = 1.0f; two = 1; on2 = 0.3f + 0.3f + on2; }
    if (ai_rand(w, 1.0f) < p) {
        flash = 1;
        float q = e->rv[1] <= 50.0f ? 0.5f : 0.9f;
        lng = ai_rand(w, 1.0f) < q;
    }
    /* 0x5ec160: a new pattern once the last one is over */
    if (r->horn_on + r->horn_off + r->horn_on2 < w->time - r->horn_t) {
        r->horn_t = w->time; r->horn_on = on; r->horn_off = off; r->horn_on2 = on2;
        r->horn_two = (char)lng; r->horn_flash = (char)flash; r->horn_x928 = (char)two;
    }
}

/* -- GetUnstuck (type 9, vtable 0x785784) -- */
/* 0x4032a0: the path point a few ahead of the car (3; 1 in drift-type races) */
static const FPoint *unstuck_point(const AiRacer *r)
{
    const RacerEnt *o = own_ent(r);
    if (r->L.n < 1) return NULL;
    int i = (o ? o->idx : 0) + (world_splitlike(r->w) ? 1 : 3);
    if (r->L.n - 1 < i) i = r->L.n - 1;
    if (i < 0) i = 0;
    return r->L.p[i];
}
/* 0x40e7c0: stuck -- far off the road, slow and heading off it, or pinned
   by the front or a side for 0.4 s */
static int unstuck_prio(AiRacer *r)
{
    AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    int stuck = 0;
    if (race_grace(r)) return -1;
    if (since_reset(r) <= 2.0f) return -1;                              /* 0x7836f8 */
    if (!(2 < c->wheels)) return -1;                                    /* 0x60bef0 */
    if (!world_splitlike(w)) {
        int far = 0;
        if (r->rpt) {
            int back = !r->rdir;
            far = !(pt_outside(r->rpt, pt_latq(w->net, r->rpt, c->pos, back), back) < 20.0f);   /* 0x7836f4 */
        }
        if (far) stuck = 1;
        else if (((c->ctrl_type == 3 && !(25.0f < c->absv)) || (c->ctrl_type == 4 && !(20.0f < c->absv))) &&
                 road_check(r, 1) == 1) stuck = 1;                       /* 0x7836ec / 0x7836f0 */
    }
    for (int i = 0; i < r->ncontact && !stuck; i++) {
        int s = contact_side(r, &r->contact[i]);
        if ((s == 0 || s == 2 || s == 3) && contact_pinned(r, &r->contact[i])) stuck = 1;
    }
    return stuck ? 500 : -1;
}
static AiAction *unstuck_new(int prio, float hold)                      /* 0x423c30 */
{
    AiAction *a = act_new(ACT_UNSTUCK, prio, 5.0f);                     /* 0x783628 */
    if (!a) return NULL;
    a->t210 = 0;
    a->t214 = hold;
    a->flag = !(fabsf(hold + 1.0f) < 0.001f);
    return a;
}
/* Begin 0x420940: back off towards the road a few points ahead, in lane 2,
   at -15 m/s (-6 for a traffic car) */
static void unstuck_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    plan_keep(&a->plan, NULL, w->time, 0);
    a->use_cap = 0; a->x1f8 = 0; a->catchup = 0; a->go = 1;
    if (!r->rpt) return;
    const FPoint *f = unstuck_point(r);
    if (f) {
        float q[3];
        fp_point(n, f, q, f->dist, 0, r->ls.lane);                      /* 0x4122c0 */
        if (world_splitlike(w)) a->plan.near = 1;
        plan_add(&a->plan, q, (int)(pt_flags(r->rpt) & 0x7ff), pt_index(n, r->rpt), 2, 0, NULL);
    }
    plan_fill(r, &a->plan, 1.0f, 2, 0, 0.15f);
    a->speed = r->car.ctrl_type == 3 ? -15.0f : -6.0f;                  /* 0x7836d8 / 0x7836dc */
}
/* vt+0x18 0x414440: done once it faces the road ahead and nothing pins
   its front */
static int unstuck_done(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    if (since_reset(r) <= 2.0f) return 0;
    if (a->flag && w->time - a->t210 < a->t214) return 0;
    const FPoint *f = unstuck_point(r);
    if (!f) return 0;
    float q[3];
    fp_point(w->net, f, q, f->dist, 0, r->ls.lane);                     /* 0x4143f0 */
    float d[2] = { q[0] - r->car.pos[0], q[1] - r->car.pos[1] }, fw[2] = { r->car.fwd[0], r->car.fwd[1] };
    v2norm(d, d); v2norm(fw, fw);
    int facing = 0.9f <= d[0] * fw[0] + d[1] * fw[1];                   /* 0x7836e0 */
    for (int i = 0; i < r->ncontact; i++)
        if (contact_side(r, &r->contact[i]) == 0 && 0.4f <= w->time - r->contact[i].start && r->car.absv < 5.0f)
            return 0;
    return facing;
}
/* vt+0x1c 0x40e9f0: give up -- lost the road, wheels up, reversing into a
   wall, or the rear pinned */
static int unstuck_fail(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    if (!r->rpt || !(2 < r->car.wheels)) return 1;
    if (a->flag && w->time - a->t210 < a->t214) return 0;
    if (road_check(r, 1) == 2) return 1;
    for (int i = 0; i < r->ncontact; i++) {
        const AiContact *k = &r->contact[i];
        if (contact_side(r, k) == 1 && 0.4f <= w->time - k->start &&
            (fabsf(r->car.absv) < 5.0f || fabsf(r->car.absv - 5.0f) < 0.001f)) return 1;
    }
    return 0;
}

/* -- IJustGotHit (type 11, vtable 0x7857b4) -- */
static int hit_prio(AiRacer *r, int *car)                               /* 0x403320 */
{
    if (race_grace(r)) return -1;                                       /* (race record +0xf6 != 1) */
    *car = hit_car(r, 1);
    return *car < 0 ? -1 : 400;
}
/* 0x40dee0: brake to what the tyres give over t seconds */
static void act_brake(AiRacer *r, AiAction *a, float t)
{
    float v = r->car.absv;
    float drag = r->aero * v * 0.5f, wgt = 9.8f * r->mass;              /* 0x7fbef0 */
    a->x1f4 = t;
    a->speed = -((r->grip * (wgt + drag)) / r->mass) * t + v;
}
/* 0x402940: the other car's speed less `less`, reaching it after the gap
   closes at the relative speed */
static void act_match(AiRacer *r, AiAction *a, const RacerEnt *e, float less)
{
    const AiSeen *o = seen_car(r->w, e->car);
    float v = o ? sqrtf(o->v->vel[0] * o->v->vel[0] + o->v->vel[1] * o->v->vel[1] + o->v->vel[2] * o->v->vel[2]) : 0;
    a->speed = v - less;
    if (fabsf(e->rv[1]) <= 0.01f) a->x1f4 = 99999.0f;                    /* 0x78425c */
    else a->x1f4 = ((ent_ahead(r, e) - own_ext(r, 1)) - e->ext[1]) / e->rv[1];
    if (a->speed <= 0) a->speed = 0;
    if (!(0.1f < a->x1f4)) a->x1f4 = 0.1f;                              /* 0x784358 */
}
/* Begin 0x423e20: move off sideways from the car that hit it (5 m clear of
   both), or hold the line while it is alongside; brake when about to run
   off the road with it ahead; follow a human ahead at its speed */
static void hit_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    AiLane *ls = &r->ls;
    const RacerEnt *e = ent_of(r, a->target);
    float off = 0, rate = 0.25f;                                        /* 0x783718 */
    a->x1f8 = 0; a->use_cap = 0; a->go = 1;
    int ahead = 0, along = 0;
    if (e && a->target >= 0) {
        float tl = ent_lat(e, ls->lane), te = e->ext[0], oe = own_ext(r, 0);
        off = ls->off;
        if (!(tl - own_lat(r, ls->lane) < 0)) { float k = ((tl - te) - oe) - 5.0f; if (!(off < k)) off = k; }   /* 0x783714 */
        else { float k = 5.0f + oe + te + tl; if (off <= k) off = k; }
        float d = ent_ahead(r, e), sum = e->ext[1] + own_ext(r, 1);
        ahead = sum - 2.8f < d;                                         /* 0x78370c */
        if (d < 2.8f - sum) { off = ls->off; along = 1; rate = 0.05f; } /* 0x7e8e64 */
    }
    if (ls->lane != -1) { ls->off = off; ls->rate = rate; }
    plan_build(r, ls, &a->plan);
    if (world_splitlike(w) || !e || a->target < 0) { a->use_cap = 1; goto out; }
    if (road_check(r, 0) == 1 && 0 < ent_ahead(r, e)) { act_brake(r, a, 0.2f); goto out; }   /* 0x783704 */
    {
        const AiSeen *o = seen_car(w, a->target);
        float spd;
        if (!seen_human(o) || r->owner_kind == 3) spd = 1000.0f;        /* ctrl vt+0x7c 0x5a9f50 (traffic: its own rule) */
        else if (ahead) { act_match(r, a, e, 0); goto out; }            /* 0x783708 */
        else if (!along) { a->use_cap = 1; goto out; }
        else spd = 1000.0f;
        a->speed = spd;
        a->x1f4 = 0;
    }
out:
    if (!a->use_cap && a->speed < 20.0f && (w->race_start < 0 || 10.0f < w->time - w->race_start))   /* 0x783710 / 0x784560 */
        a->speed = 20.0f;
}
/* vt+0x18 0x41cc90: done once nothing hits it (else the newest hitter) */
static int hit_done(AiRacer *r, AiAction *a)
{
    int c = hit_car(r, 1);
    if (c >= 0) a->target = c;
    return c < 0;
}

/* -- AvoidAvoidable (type 1, vtable 0x785858) -- */
/* 0x42ba20: the driver's plan runs into a car within 3 s */
static int avoid_prio(AiRacer *r, int *car, float *t)
{
    *car = -1;
    float tt = 99999.0f;
    int who = predict_hit(r, &r->dplan, 3.0f, &tt);                     /* 0x783670 */
    if (who < 0) return -1;
    *car = who; *t = tt;
    return 300;
}
static AiAction *avoid_new(AiRacer *r, int prio, int car, float k, float t)   /* 0x4243b0 */
{
    AiAction *a = act_new(ACT_AVOID, prio, -1.0f);                      /* 0x783630 */
    if (!a) return NULL;
    a->target = car;
    a->t21c = t;
    a->t22c = 2.0f + t;                                                 /* 0x783728 */
    a->t224 = k;
    a->t228 = r->w->time;
    a->t220 = 0;                                                        /* a traffic car's lane link (ctrl +0x7a0): no traffic here */
    return a;
}
/* 0x428fd0: AvoidAvoidable's search: offsets either side of the current
   one, a tenth of the lane's width apart, at rates 0.05..0.15, until a plan
   runs into nobody within the horizon; against a car outside the race the
   two offsets clear of it */
static int avoid_plan(AiRacer *r, AiLane *ls, AiPlan *P, float horizon, int low_first, int fast)
{
    AiWorld *w = r->w;
    const RacerEnt *own = own_ent(r);
    int lane = ls->lane, who = -1;
    float tm = 99999.0f;
    if (lane_try(r, ls, P, horizon, &who, &tm)) return 1;
    int who0 = who;
    if (ls->stamp != 0 && w->time - ls->stamp < 0.5f) return 1;         /* 0x783814 */
    if (!own || r->L.n < 1) return 0;
    const FPoint *f = r->L.p[own->idx < 0 ? 0 : own->idx >= r->L.n ? r->L.n - 1 : own->idx];
    float lo = fp_lo(f, lane), hi = fp_hi(f, lane);
    float step = (hi - lo) * 0.1f;
    if (step <= 1.0f) step = 1.0f;                                      /* 0x7837d4 */
    float cur = ls->off;
    AiLane cand = { -1, 0, 0, 0 };
    int found = 0;
    for (float rate = 0.05f; rate <= 0.151f; rate += 0.05f) {           /* 0x7e8e64 / 0x7837d8 / 0x7837dc */
        int more_hi = 1, more_lo = 1;
        float k = step;
        while (!found && (more_hi || more_lo)) {
            float a = cur - k, b = cur + k;
            more_lo = lo < a;
            more_hi = b < hi;
            float first = low_first ? a : b, second = low_first ? b : a;
            int first_ok = low_first ? more_lo : more_hi, second_ok = low_first ? more_hi : more_lo;
            if (first_ok) {
                cand.lane = lane; cand.off = first; cand.rate = rate; cand.stamp = 0;
                who = -1; tm = 99999.0f;
                plan_build(r, &cand, P);
                found = 0;
                if ((who = predict_hit(r, P, horizon, &tm)) < 0 || horizon < tm) found = 1;
            }
            if (!found && second_ok) {
                cand.lane = lane; cand.off = second; cand.rate = rate; cand.stamp = 0;
                found = lane_try(r, &cand, P, horizon, &who, &tm);
            }
            k += step;
        }
        if (found) { *ls = cand; return 1; }
    }
    if (who0 < 0) return 0;
    if (r->in_race && seen_in_race(seen_car(w, who0))) return 0;
    /* a car outside the race: the offsets clear of it either side */
    const RacerEnt *e = ent_of(r, who0);
    if (!e) return 0;
    float wd = e->ext[0] + own->ext[0], need = wd + 5.0f;               /* 0x78380c */
    float el = ent_lat(e, lane);
    float A = el - need, B = el + need;
    if (!(el < ls->off)) { if (ls->off < A) A = ls->off; }              /* 0x401070 */
    else { if (B < ls->off) B = ls->off; }                              /* 0x401050 */
    float C = el - wd, D = el + wd;
    float costA = 0, costB = 1.0f;
    if (el < ls->off) { costA = 1.0f; costB = 0; }
    for (int i = 0; i < r->ncontact; i++)
        if (r->contact[i].car) {
            float ca = fabsf(A - el), cb = fabsf(B - el);
            if (2.0f < fabsf(ca - cb)) { costB = cb; costA = ca; }       /* 0x783810 */
            break;
        }
    if (fast) {
        for (int i = own->idx; i <= e->idx && i < r->L.n; i++) {
            if (i < 0) continue;
            const FPoint *g = r->L.p[i];
            float l = fp_lo(g, lane), h = fp_hi(g, lane);
            float cc = l < C ? C : l; if (h < cc) cc = h;
            if (cc != C) costA = 99999.0f;
            float dd = D <= l ? l : D; if (h < dd) dd = h;
            if (dd != D) costB = 99999.0f;
        }
    }
    AiLane la = { lane, A, 999.0f, w->time }, lb = { lane, B, 999.0f, w->time };
    AiPlan PA, PB;
    memset(&PA, 0, sizeof PA); memset(&PB, 0, sizeof PB);
    int okA = fabsf(costA - 99999.0f) > 0.001f, okB = fabsf(costB - 99999.0f) > 0.001f;
    if (okA) plan_build(r, &la, &PA);
    if (okB) plan_build(r, &lb, &PB);
    if (okA && (!okB || costA < costB)) { *ls = la; *P = PA; }
    else if (okB) { *ls = lb; *P = PB; }
    return 0;
}
/* Begin 0x42f330: find an offset that misses (0x428fd0); failing that,
   against an overlapped car stop and burn nitrous, else follow the car
   ahead; or keep the capped speed */
static void avoid_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    a->go = 1; a->catchup = 1;
    const RacerEnt *e = ent_of(r, a->target);
    int follow = 0, stop = 0;
    if (!e || a->target < 0 || race_grace(r)) plan_build(r, &r->ls, &a->plan);
    else {
        int low = !(e->rv[0] <= 0);
        int fast = !(c->speed < 11.1757f);                              /* 0x783734 (25 mph) */
        /* 0x42dec0 by the goal's lanes (vt+0x20): the racing line 0x428fd0,
           the traffic lanes (a drag race) 0x42bf00 */
        int lc = goal_laneclass(r);
        int found = lc == 1 ? avoid_plan(r, &r->ls, &a->plan, 3.0f, low, fast)
                  : lc == 2 ? traffic_avoid(r, &r->ls, &a->plan, 3.0f, low) : 0;
        if (!found &&
            !seen_traffic(seen_car(w, a->target))) {                    /* (0x60bf70: the other just reset -- not tracked) */
            if (fabsf(a->t21c) < 0.001f) {
                float ac = car_acc_fwd(c), t = a->t21c;
                if ((ac * t * 0.5f + e->rv[1]) * t + ent_ahead(r, e) <= 3.0f) stop = 1;   /* 0x783730 */
                else follow = 1;
            } else if (0 < ent_ahead(r, e)) follow = 1;
        }
    }
    int split = w->streetx || w->x89010c || w->drift || w->x890104;
    if (!follow || split) {
        if (stop) {
            a->speed = 2.0f * 1000.0f;                                  /* ctrl vt+0x7c x 2 */
            a->use_cap = 0; a->x1f8 = 0; a->x1f4 = 0;
            act_nitrous(r, a);                                          /* 0x40e390 */
        } else {
            if (!w->drag && !w->x890108) a->use_cap = 1;
            else { a->speed = 1000.0f; a->use_cap = 0; }
            a->x1f4 = 0; a->x1f8 = 2.0f;
        }
    } else {
        a->use_cap = 0;
        float rv = e ? e->rv[1] : 0;
        if (!(rv < 0)) rv = 0;
        a->x1f4 = a->t21c; a->x1f8 = 2.0f;
        float s = rv * -0.25f + c->absv;                                /* 0x783738 */
        a->speed = s <= 20.0f ? 20.0f : s;                              /* 0x78372c */
    }
}
/* vt+0x18 0x42ba80: done when the car is gone, or 2 s past the predicted
   meeting with nothing new ahead (a new car ahead becomes the target) */
static int avoid_done(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    float el = w->time - a->t228, dur = a->t22c, tt = 99999.0f;
    const RacerEnt *e = ent_of(r, a->target);
    if (a->target < 0 || !e) return 1;
    int who = predict_hit(r, &r->dplan, 3.0f, &tt);
    int keep = 0;
    if (!w->drag && !w->x890108) {
        const AiSeen *o = seen_car(w, a->target);
        if (seen_traffic(o) && e) keep = who >= 0 || !(ent_ahead(r, e) < -10.0f);   /* 0x785bac */
        else keep = who >= 0;
    } else keep = who >= 0 && tr_counts(r, who);                        /* 0x4140e0 */
    if (!keep) {
        if (w->drag || w->x890108 || dur <= el) return 1;
    }
    if (who >= 0) { a->target = who; a->t21c = tt; }
    return 0;
}

/* -- the goal "Park" (0x40cb80, vtable 0x784968) and StopDriving (type 16,
   vtable 0x78591c): what every finisher's controller is given (0x42b330) -- */
/* 0x428490's lane classes: none (-1 / 10+) 0, the racing line (0, 1) 1, a
   traffic lane (2..9) 2 */
static int lane_class(int lane) { return lane < 0 ? 0 : lane < 2 ? 1 : lane < 10 ? 2 : 0; }
/* goal vt+0x20: the lanes the goal drives in -- the road race 0x40ca60 the
   racing line; Park 0x40ddf0 the racing line for a human-owned car (outside
   drag), else the traffic lanes */
static int goal_laneclass(const AiRacer *r)
{
    if (r->goal == GOAL_TRAFFIC || goal_drag(r->goal)) return 2;       /* 0x40caa0 */
    if (r->goal != GOAL_PARK) return 1;
    return r->owner_kind == 1 && !r->w->drag && !r->w->x890108 ? 1 : 2;
}
/* 0x40ae90: the point has the lane -- the racing line on either side
   (section +0x74 / +0x75 of its direction), a traffic lane its +0x82 byte */
static int fp_haslane(const AiNet *n, const FPoint *f, int lane)
{
    if (lane == -1 || f->sec < 0 || f->pt == -1) return 0;   /* (the original reads no section then) */
    const unsigned char *s = n->sec[f->sec];
    if (!s) return 0;
    int c = lane_class(lane), m = fp_lanemap(f, lane);
    if (c == 1) return s[0x74 + m * 8] || s[0x75 + m * 8];
    return c == 2 && s[0x82 + m];
}
/* 0x40aa90: the lane runs the whole path (to its last point) */
static int fl_lane_ok(const FList *L, int lane)
{
    float last = L->n ? L->p[L->n - 1]->dist : 0;
    if (lane == 0) return last <= L->lanemin[0];                       /* +0xb0 */
    if (1 < lane && lane < 6) return last <= L->lanemin[lane - 1];     /* +0xac + lane * 4 */
    return 0;
}
/* 0x40af30: from traffic lane b to a, a runs on at least thr further */
static int fl_lane_gap(const FList *L, int a, int b, float thr)
{
    if (lane_class(a) == 2 && lane_class(b) == 2) return thr <= L->lanemin[a - 1] - L->lanemin[b - 1];
    return 1;
}
/* 0x412500: may the car move from lane `from` to the lane `to` at the
   path point? (a drag race steps the lanes between: aidrag.c) */
static int lane_step_ok(const AiRacer *r, const FPoint *f, int from, int to)
{
    const AiNet *n = r->w->net;
    if (to != 0 && (to < 2 || 5 < to)) { drag_why(r, "lane %d out of 2..5", to); return 0; }
    if (!fp_haslane(n, f, to)) { drag_why(r, "lane %d absent at sec %d pt %d", to, f->sec, f->pt); return 0; }
    /* the car's controller is not type 2: the lane must run the path or
       run 25 m (0x783cc8) further than the one it leaves */
    if (r->car.ctrl_type != 2 && !fl_lane_ok(f->list, to) && !fl_lane_gap(f->list, to, from, 25.0f)) {
        drag_why(r, "lane %d runs short (non-drag rule)", to); return 0;
    }
    return drag_lane_steps(r, f, from, to);
}
/* 0x40f4a0 (no lanes excluded): the traffic lane at the car's route point
   nearest its lateral position (backward along the road: lanes 6..9, as
   2..5), -1 none */
static int nearest_traffic_lane(const AiRacer *r)
{
    const AiNet *n = r->w->net;
    Pt *p = r->rpt;
    if (!p) return -1;
    const unsigned char *s = sec_of(n, p);
    if (!s) return -1;
    int back = !r->rdir, first = back ? 6 : 2, best = -1;
    float lat = pt_latq(n, p, r->car.pos, back), bd = 8.999999e9f;
    for (int m = first; m < first + 4; m++) {
        if (!s[0x82 + m]) continue;
        float d = fabsf(pt_laneoff(p, m) - lat);
        if (d < bd) { bd = d; best = m; }
    }
    if (best != -1 && back) best -= 4;
    return best;
}
static void lane_set(AiLane *ls, int lane)                               /* 0x405410 / 0x40d4a0 */
{
    ls->lane = lane; ls->off = 0; ls->rate = 0.151f; ls->stamp = 0;     /* 0x7837d8 */
}
/* 0x40fc40: the lane state still holds on the path */
static int lane_holds(const AiLane *ls, const FList *L)
{
    int l = ls->lane;
    if (l == -1) return 0;
    return (0 <= l && l <= 1) || l == 2 || fl_lane_ok(L, l);
}
/* 0x420ed0: the plan on a traffic lane -- the one it is in, else the
   nearest at its route point; failing that the lane it has, or the racing
   line when that no longer holds. 1 when on a traffic lane */
static int traffic_lane_plan(AiRacer *r, AiLane *ls, AiPlan *P)
{
    if (1 < ls->lane && ls->lane < 6) { plan_build(r, ls, P); return 1; }
    int lane = nearest_traffic_lane(r);
    if (lane != -1) {
        AiLane t;
        lane_set(&t, lane);
        plan_build(r, &t, P);
        *ls = t;                                                        /* 0x40fbe0 */
        return 1;
    }
    if (!lane_holds(ls, &r->L)) lane_set(ls, 0);
    plan_build(r, ls, P);
    return 0;
}
/* 0x428e70: move to `lane` if every lane on the way can be stepped into
   and the plan there runs into nobody within the horizon */
static int lane_move(AiRacer *r, AiLane *ls, AiPlan *P, int lane, float horizon)
{
    const FList *L = &r->L;
    const FPoint *f = (0 <= L->cur && L->cur < L->n) ? L->p[(int)L->cur] : NULL;   /* the path point at the car */
    int cur = ls->lane, ok = 1;
    if (!f) return 0;
    if (cur < lane) { while (cur < lane) { if (!ok) return 0; ok = lane_step_ok(r, f, cur, cur + 1); cur++; } }
    else {
        if (cur <= lane) return 0;
        while (lane < cur) { if (!ok) return 0; ok = lane_step_ok(r, f, cur, cur - 1); cur--; }
    }
    if (!ok) return 0;
    AiLane t;
    lane_set(&t, lane);
    int who; float tm;
    if (!lane_try(r, &t, P, horizon, &who, &tm)) return 0;
    *ls = t;
    return 1;
}
/* 0x42bf00: on a traffic lane, the nearest lane either side (up to 3
   away, the lower first when asked) whose plan misses everyone; else the
   plan of the lane it is in */
static int traffic_avoid(AiRacer *r, AiLane *ls, AiPlan *P, float horizon, int low_first)
{
    AiPlan T = *P;                                                      /* 0x403fc0 */
    if (ls->lane < 2 || 5 < ls->lane) {
        if (!traffic_lane_plan(r, ls, &T)) goto keep;
    }
    int up = ls->lane, dn = ls->lane;
    for (int k = 1; k < 4; k++) {
        dn--; up++;
        int a = low_first ? dn : up, b = low_first ? up : dn;
        if (lane_move(r, ls, &T, a, horizon) || lane_move(r, ls, &T, b, horizon)) { *P = T; return 1; }   /* 0x401d40 */
    }
keep:
    plan_build(r, ls, P);
    return 0;
}
/* 0x428490: the controller moves to the lanes its goal drives in */
static void goal_lanes(AiRacer *r)
{
    int want = goal_laneclass(r);
    if (lane_class(r->ls.lane) == want) return;
    if (want == 1) lane_set(&r->ls, 0);                                 /* 0x413860 -> vt+0x20(0) 0x40d4a0 */
    else if (want == 2) {                                               /* 0x4233e0 */
        if (r->L.n != 0 && r->nent != 0) {
            AiPlan P;
            memset(&P, 0, sizeof P);                                    /* 0x41d160 */
            traffic_lane_plan(r, &r->ls, &P);
        } else {
            int lane = nearest_traffic_lane(r);
            if (lane != -1) lane_set(&r->ls, lane);
        }
    }
}
/* StopDriving Begin 0x42f6e0: the plan for its lane unless the driver's plan
   runs into someone within 3 s -- then a way round (0x428fd0 on the racing
   line, 0x42bf00 on the traffic lanes, 2 s); the speed 0, go on */
static void stop_begin(AiRacer *r, AiAction *a)
{
    float t;
    if (predict_hit(r, &r->dplan, 3.0f, &t) < 0) plan_build(r, &r->ls, &a->plan);   /* 0x783670 */
    else if (goal_laneclass(r) == 1) avoid_plan(r, &r->ls, &a->plan, 2.0f, 1, 0);
    else if (goal_laneclass(r) == 2) traffic_avoid(r, &r->ls, &a->plan, 2.0f, 1);
    a->speed = 0; a->x1f4 = 0; a->x1f8 = 0;
    a->use_cap = 0; a->go = 1; a->catchup = 1;                          /* +0x1fc / +0x1fd / +0x1fe */
}
/* the goal's default action (vt+0x3c): Park 0x427060 StopDriving (600, no
   time limit); the road race 0x4269e0 JustDrive */
static AiAction *goal_default(const AiRacer *r)
{
    if (r->goal == GOAL_PARK) return act_new(ACT_STOP, 600, -1.0f);   /* 0x783644 */
    if (r->goal == GOAL_PURSUIT) {                                      /* 0x426f20 DriveAtOffset */
        AiAction *a = act_new(ACT_DRIVEOFFSET, 0, -1.0f);
        if (a) { a->target = r->op_target; a->t214 = r->op_long; a->t218 = r->op_lat; a->t21c = 0; }
        return a;
    }
    if (goal_drag(r->goal)) return act_new(TR_DRIVE, 0, -1.0f);        /* 0x426ec0 DriveTrafficLane */
    return act_new(ACT_JUSTDRIVE, 0, -1.0f);
}

/* -- Block (type 4, vtable 0x785728) -- */
/* 0x414130: block the first race car behind that is on the road, fast and
   closing (a human within 35 m, or within 30 s / 2 s), when the car runs
   over 80 % of its lane speed */
static int block_prio(AiRacer *r, int *car)
{
    AiWorld *w = r->w;
    *car = -1;
    float ls = r->rpt && r->ls.lane >= 0 ? pt_lanespeed(r->rpt, r->ls.lane) : 0;   /* 0x5ec070 -> 0x4024f0 */
    if (ls <= 0.1f) ls = 0.1f;                                          /* 0x784358 */
    if (!(0.8f < r->car.absv / ls) || race_grace(r)) return -1;         /* 0x7836ac */
    const RacerEnt *own = own_ent(r);
    int lvl = r->block & 0xff;
    for (int i = r->own + 1; i < r->nent; i++) {
        const RacerEnt *e = &r->ent[i];
        const AiSeen *o = seen_car(w, e->car);
        if (!seen_in_race(o)) continue;
        float ov = sqrtf(o->v->vel[0] * o->v->vel[0] + o->v->vel[1] * o->v->vel[1] + o->v->vel[2] * o->v->vel[2]);
        if (!(10.0f <= ov)) continue;                                   /* 0x7836a4 */
        int human = seen_human(o);
        const FPoint *f = r->L.n > 0 ? r->L.p[e->idx < 0 ? 0 : e->idx >= r->L.n ? r->L.n - 1 : e->idx] : NULL;
        float lat = ent_lat(e, 0), a0, b0;
        if (lat <= 0) { a0 = f ? f->lo[0] : 0; b0 = lat; }
        else { a0 = lat; b0 = f ? f->hi[0] : 0; }
        float d = ent_ahead(r, e), rv = e->rv[1];
        if (w->blob[0x938 + lvl * 8]) d = e->ext[1] + (own ? own->ext[1] : 0) + d;
        if (!(a0 - b0 < 0)) return -1;
        if (!(d < 0)) return -1;
        float first = r->L.n ? r->L.p[0]->dist : 0;                      /* 0x401f70 */
        if (!(first < e->pathpos)) return -1;
        if (!(-50.0f < d)) return -1;                                   /* 0x7836a0 */
        if (!(-5.0f < rv)) return -1;                                   /* 0x78369c */
        const RacerEnt *pick = NULL;
        if (0.001f < rv) {
            float t = fabsf(d / rv), lim = human ? 30.0f : 2.0f;        /* 0x783690 / 0x783694 */
            if (t < lim && t < 99999.0f) pick = e;
        }
        if (!pick && human && -35.0f < d) pick = e;                     /* 0x783698 */
        if (!pick) return -1;
        *car = pick->car;
        int prio;
        memcpy(&prio, w->blob + 0x934 + lvl * 8, 4);
        return prio;
    }
    return -1;
}
/* Begin 0x423a10: onto the line of the car behind, faster the sooner it
   arrives (0.15 under 10 m / 2 s, 0.05 at 10 s; graph 0x81e7dc) */
static void block_begin(AiRacer *r, AiAction *a)
{
    static const float G[4] = { 2.0f, 0.15f, 10.0f, 0.05f };            /* 0x7e9bf0 */
    AiLane *ls = &r->ls;
    const RacerEnt *e = ent_of(r, a->target);
    float off = 0, rate = 0.15f;                                        /* 0x7e9bf4 */
    if (e && a->target >= 0) {
        off = ent_lat(e, ls->lane);
        float d = ent_ahead(r, e);
        if (d < 10.0f) rate = 0.15f;                                    /* 0x7836a8 */
        else if (0.001f < fabsf(e->rv[1])) rate = agraph(G, 2, d / e->rv[1]);
    }
    if (ls->lane != -1) { ls->rate = rate; ls->off = off; }
    plan_build(r, ls, &a->plan);
    a->speed = 1000.0f;                                                 /* ctrl vt+0x7c */
    a->use_cap = 1; a->x1f4 = 0; a->x1f8 = 2.0f; a->go = 1; a->catchup = 1;
}
/* vt+0x18 0x41cc40: done when nobody is left to block (or a new target) */
static int block_done(AiRacer *r, AiAction *a)
{
    int car = -1;
    if (0 <= block_prio(r, &car)) { if (a->target != car) a->target = car; return 0; }
    return 1;
}
/* vt+0x1c 0x40e5d0: the blocked car got level */
static int block_passed(AiRacer *r, AiAction *a)
{
    const RacerEnt *e = ent_of(r, a->target);
    if (!e || a->target < 0) return 0;
    return 0 < ent_ahead(r, e) + e->ext[1] + own_ext(r, 1);
}

/* -- Pass (type 0, vtable 0x7856d8) -- */
/* 0x419e10 (search distance -1): how the path bends from the car's point
   over half the rest of the list to its end */
static float path_bend(const AiRacer *r, int lane, int from)
{
    const AiNet *n = r->w->net;
    const FList *L = &r->L;
    if (L->n == 0 || L->n - from < 2 || from < 0) return 0;
    int mid = (L->n - from) / 2 + from, last = L->n - 1;
    float a[3], b[3], c[3];
    fp_point(n, L->p[from], a, L->p[from]->dist, 0, lane);
    fp_point(n, L->p[mid], b, L->p[mid]->dist, 0, lane);
    fp_point(n, L->p[last], c, L->p[last]->dist, 0, lane);
    float u[2] = { a[0] - b[0], a[1] - b[1] }, v[2] = { b[0] - c[0], b[1] - c[1] };
    v2norm(u, u); v2norm(v, v);
    float z = u[0] * v[1] - u[1] * v[0];
    return 0 <= z ? -fabsf(z) : fabsf(z);
}
/* 0x41d2d0: which side to pass on: 1 either, 2 the low side, 3 the high;
   inside the bend ahead when both cars are on the line, else away from
   the other, where the road leaves room */
static int pass_side(const AiRacer *r, const AiLane *ls, const RacerEnt *e)
{
    const RacerEnt *own = own_ent(r);
    int lane = ls->lane;
    float tl = ent_lat(e, lane), ml = own_lat(r, lane);
    float bend = path_bend(r, lane, own ? own->idx : 0);
    int clear = 0 < (e->pathpos - (own ? own->pathpos : 0)) - (5.0f + (own ? own->ext[1] : 0) + e->ext[1]);   /* 0x783800 */
    int side = 3;
    if (r->owner_kind != 3 || !(tl < 5.0f)) {                           /* 0x7837f4 */
        if (1.0f < fabsf(ml) || 1.0f < fabsf(tl) || ent_ahead(r, e) <= 50.0f || fabsf(bend) < 0.12f) {   /* 0x7837f0 / 0x7837fc / 0x785368 */
            if (fabsf(ml - tl) < 2.0f && clear) side = 1;               /* 0x7837f8 */
            else side = ml <= tl ? 2 : 3;
        } else side = bend <= 0 ? 2 : 3;
    }
    const FPoint *f = r->L.n > 0 ? r->L.p[e->idx < 0 ? 0 : e->idx >= r->L.n ? r->L.n - 1 : e->idx] : NULL;
    float need = (own ? own->ext[0] : 0) + e->ext[0] + 5.0f;            /* 0x783804 */
    float lo = f ? fp_lo(f, lane) : 0, hi = f ? fp_hi(f, lane) : 0;
    int room_lo = !(tl - need < lo);
    if ((side != 2 && side != 1) || room_lo) {
        if ((side == 3 || side == 1) && hi < tl + need && room_lo && clear) return 2;
    } else if (tl + need <= hi && clear) return 3;
    return side;
}
/* 0x429890: the lane state that passes the car on its side (3 m clear) and
   whether its plan runs into nobody within the horizon */
static int pass_plan(AiRacer *r, AiLane *ls, AiPlan *P, const RacerEnt *e, float horizon)
{
    int side = pass_side(r, ls, e);
    float tl = ent_lat(e, ls->lane), ml = own_lat(r, ls->lane);
    float d = tl - ml, need = (own_ext(r, 0)) + e->ext[0] + 3.0f;       /* 0x783818 */
    float v = ls->off;
    if (!(fabsf(need) < fabsf(tl - ls->off))) {
        if (side == 2) v = tl - need;
        else if (side == 1) { if (0 < d) v = tl - need; else if (d < 0) v = need + tl; }
        else if (side == 3) v = need + tl;
    }
    if (ls->lane != -1) { ls->off = v; ls->rate = 0.05f; }              /* 0x7e8e64 */
    int who; float t;
    return lane_try(r, ls, P, horizon, &who, &t);
}
/* 0x42b5b0: from 5 s after the start, the farthest car ahead it can catch
   (or one under 10 m ahead and not pulling away) that a pass plan clears */
static int pass_prio(AiRacer *r, int *car, AiLane *out)
{
    AiWorld *w = r->w;
    int found = 0;
    if (race_grace(r)) return -1;
    if (w->race_start < 0 || !(5.0f < w->time - w->race_start)) return -1;   /* 0x7843e8 */
    for (int i = r->nent - 1; i >= 0; i--) {
        const RacerEnt *e = &r->ent[i];
        float d = (ent_ahead(r, e) - e->ext[1]) - own_ext(r, 1), rv = e->rv[1];
        const AiSeen *o = seen_car(w, e->car);
        if (!(0 < d) || !seen_in_race(o) || e->self) continue;
        if (!(can_close(r, o, d, rv) || (d < 10.0f && rv < 1.0f))) continue;   /* 0x78368c */
        AiPlan P;
        memset(&P, 0, sizeof P);
        plan_keep(&P, NULL, w->time, 0);
        *out = r->ls;
        if (pass_plan(r, out, &P, e, 3.0f)) { *car = e->car; found = 1; }
    }
    return found ? 100 : -1;
}
static AiAction *pass_new(AiRacer *r, int prio, int car)                /* 0x423610 */
{
    AiAction *a = act_new(ACT_PASS, prio, 5.0f);                        /* 0x783614 */
    if (!a) return NULL;
    a->target = car;
    a->t21c = r->w->time;
    return a;
}
/* Begin 0x42b940: the pass plan while the car is ahead (else the lane
   state's plan); nitrous when close in speed */
static void pass_begin(AiRacer *r, AiAction *a)
{
    const RacerEnt *e = ent_of(r, a->target);
    if (!e || a->target < 0 || ent_ahead(r, e) <= 0) plan_build(r, &r->ls, &a->plan);
    else {
        AiLane t = r->ls;
        if (!pass_plan(r, &t, &a->plan, e, 3.0f)) plan_build(r, &r->ls, &a->plan);
        else r->ls = t;
        if (fabsf(e->rv[1]) < 5.0f) act_nitrous(r, a);                  /* 0x7843e8 */
    }
    a->go = 1; a->catchup = 1; a->use_cap = 1;
}
/* vt+0x18 0x42b7f0: retarget to a farther car when one can be passed;
   done 3 s on once the car is 10 m behind */
static int pass_done(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    if (0 < r->nent && r->ent[0].car != a->target) {
        AiLane t = { -1, 0, 0, 0 };
        int car = -1;
        if (0 <= pass_prio(r, &car, &t) && r->ent[0].car != a->target) {
            r->ls = t;
            a->target = car;
            a->t21c = w->time;
        }
    }
    if (!(w->time - a->t21c < 3.0f)) {                                  /* 0x783688 */
        const RacerEnt *e = ent_of(r, a->target);
        if (e) {
            float need = own_ext(r, 1) + e->ext[1] + 10.0f;              /* 0x783684 */
            if (-need <= ent_ahead(r, e)) return 0;
        }
        return 1;
    }
    return 0;
}
/* vt+0x1c 0x40e4a0: the car is gone, or 3 s on still over 10 m ahead and
   out of reach */
static int pass_fail(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    const RacerEnt *e = ent_of(r, a->target);
    if (!e) return 1;
    if (3.0f < w->time - a->t21c) {
        float d = ent_ahead(r, e);
        const AiSeen *o = seen_car(w, a->target);
        if (0 < d && o && !can_close(r, o, d, e->rv[1]) && 10.0f < d) return 1;
    }
    return 0;
}

/* -- AccidentRecovery (type 13, vtable 0x7857e8) -- */
/* 0x403380: the car's up axis is above `cosine` */
static int car_upright(const AiCarView *c, float cosine) { return cosine < c->up[2]; }
static AiAction *accident_new(AiRacer *r, int prio)                     /* 0x4241c0 */
{
    AiAction *a = act_new(ACT_ACCIDENT, prio, -1.0f);
    if (!a) return NULL;
    a->t210 = r->w->time;
    a->flag = 0;
    return a;
}
/* Begin 0x424210: stop; after 5 s ask to be put back on the road */
static void accident_begin(AiRacer *r, AiAction *a)
{
    AiWorld *w = r->w;
    plan_build(r, &r->ls, &a->plan);
    a->use_cap = 0; a->speed = 0; a->x1f8 = 0; a->x1f4 = 0; a->go = 0;
    if (5.0f <= w->time - a->t210 && !a->flag) {                        /* 0x783720 */
        r->want_reset = 1;                                              /* 0x5ebf60 */
        a->flag = 1;
    }
}
static int accident_done(AiRacer *r, AiAction *a)                       /* 0x4033d0 */
{
    if (a->flag) return 1;
    return r->car.wheels == 4 && car_upright(&r->car, 0.707f);          /* 0x78371c */
}

/* -- JustDrive (type 6, vtable 0x785684) -- */
/* Begin 0x42b570 -> 0x42b340: back to the racing line (0.05 m across per
   metre, 0.2 in street X) when nothing ahead closes within 0.75 s and that
   plan runs into nobody for 3 s; else the lane state stays (a traffic
   lane's state is dropped for the line) */
static void best_line(AiRacer *r, AiAction *a)                         /* 0x42b340 */
{
    AiWorld *w = r->w;
    float rate = (w->streetx || w->x89010c) ? 0.2f : 0.05f;            /* 0x783680 / 0x7e8e64 */
    AiLane line = { 0, 0, rate, 0 };
    float tmin = 99999.0f;
    for (int i = 0; i < r->own; i++) {
        const RacerEnt *e = &r->ent[i];
        float d = ent_ahead(r, e), v = e->rv[1], t = 99999.0f;
        if (fabsf(v) <= 0.1f || (t = (-1.0f / v) * d, 0 < t))
            if (t < tmin) tmin = t;
    }
    AiLane *ls = &r->ls;
    int done = 0;
    if (0.75f < tmin && (ls->lane != 0 || !(fabsf(ls->off) < 0.001f) || !(fabsf(ls->rate - rate) < 0.001f))) {   /* 0x78367c */
        AiPlan P;
        memset(&P, 0, sizeof P);
        plan_keep(&P, NULL, w->time, 0);
        int who; float t;
        if (lane_try(r, &line, &P, 3.0f, &who, &t)) { *ls = line; a->plan = P; done = 1; }
    }
    if (!done) {
        if (ls->lane != 0) *ls = line;
        plan_build(r, ls, &a->plan);
    }
}
static void justdrive_begin(AiRacer *r, AiAction *a)                    /* 0x42b570 */
{
    best_line(r, a);
    act_nitrous(r, a);                                                  /* 0x40e390 */
    a->use_cap = 1; a->x1f8 = 0; a->go = 1; a->catchup = 1;
}

/* ---- the AI's nitrous ------------------------------------------------------- */
/* 0x592480: seconds of nitrous left */
static float nos_left(const AiRacer *r) { return r->nos_frac * r->nos_cap; }
/* 0x402590: the engine speed lies in its gear's band (down-shift point to
   95 % of the way to the up-shift point) */
static int rpm_in_band(const AiRacer *r)
{
    float lo = r->band_lo, hi = r->band_hi, top = (hi - lo) * 0.95f + lo;   /* 0x783538 */
    float v = r->rpm < lo ? lo : r->rpm;
    if (top < v) v = top;
    return v == r->rpm;
}
/* 0x40d850: nitrous may burn: enough left (or burning), all wheels down,
   touching no resting object, the driven wheels not spinning, in gear and
   the engine out of the band */
static int nos_gate(AiRacer *r)
{
    AiWorld *w = r->w;
    int skill = r->skill < 1 ? 0 : r->skill;
    float minrem = *blob_f(w, skill * 0x68 + 0x570);
    if ((!r->nos_burn && nos_left(r) < minrem) || r->car.wheels != 4) return 0;
    for (int i = 0; i < r->ncontact; i++) if (r->contact[i].wall) return 0;
    {   /* 0x402660: the driven wheels' mean slip over 2 */
        int n = 0;
        float s = 0;
        for (int i = 0; i < 4; i++) {
            int dr = r->car.rear_frac < 0.2f ? i < 2 : (0.8f < r->car.rear_frac ? i >= 2 : 1);   /* 0x592870 */
            if (dr) { n++; s += fabsf(r->car.slip[i][0]); }
        }
        if (0 < n) s /= (float)n;
        if (2.0f < s) return 0;                                         /* 0x78353c */
    }
    int mingear = 2;                                                    /* 0x7e8d90 */
    if (w->drag || w->x890108) mingear = w->blob[skill * 0x68 + 0x588] + 1;
    return mingear <= r->gear && !rpm_in_band(r);
}
/* the controller's vt+0xa0 (0x416f70): go for nitrous -- slower than 80 %
   of the planned speed (or in a traffic lane), or a straight for over a
   second, past the skill's cooldowns; the last second of the bottle kept
   for the last quarter of the last lap */
static int nos_decide(AiRacer *r)
{
    AiWorld *w = r->w;
    if (r->car.mover == 4 || !r->in_race) return 0;
    if (!w->drag && !w->x890108) {
        int not_outrun = w->mode != 7;
        float rd = r->race_dist < 0 ? 0 : r->race_dist;
        int lap = (int)floorf(rd);
        float pct = (rd - (float)lap) * 100.0f;                          /* 0x5d5d10 from the finish line */
        int skill = r->skill < 1 ? 0 : r->skill;
        int lastlap = lap == w->laps - 1;
        int final = lastlap && 0.75f < 0.01f * pct;                     /* 0x783b58 */
        if (w->blob[skill * 0x68 + 0x589] && nos_left(r) <= 1.0f && !final && not_outrun) return 0;   /* 0x783b5c */
        float maxburn = not_outrun ? *blob_f(w, skill * 0x68 + 0x574) : 2.0f;   /* 0x783b6c */
        if (r->nos_burn && !(w->time - r->nos_start < maxburn)) return 0;
        float cool = *blob_f(w, skill * 0x68 + (lastlap ? 0x56c : 0x568));
        if (final) cool = 0;
        if (!r->nos_burn && w->time - r->nos_stop <= cool) return 0;
        int lane = r->ls.lane, slow = 0, straight = 0;
        float sp = r->rpt && lane >= 0 ? pt_lanespeed(r->rpt, lane) : 0;
        if (r->L.cur != -1 && r->L.cur < r->L.n) sp = fp_speed(r->L.p[r->L.cur], lane);
        if (1 < lane && lane < 6) slow = 1;
        else if (0.001f < sp) slow = r->car.absv / sp < (not_outrun ? 0.8f : 1.0f);   /* 0x783b60 / 0x783b64 */
        {   /* 0x412220: the speeds ahead never drop */
            const RacerEnt *o = own_ent(r);
            int ok = 1;
            for (int i = o ? o->idx : 0; ok && i < r->L.n - 1; i++)
                if (i >= 0) ok = fp_speed(r->L.p[i], lane) <= fp_speed(r->L.p[i + 1], lane);
            if (!ok) r->nos_straight = 0;
            else if (r->nos_straight == 0) r->nos_straight = w->time;
        }
        if (r->nos_straight != 0 && 1.0f < w->time - r->nos_straight) straight = 1;   /* 0x783b68 */
        if (!slow && !straight) return 0;
    }
    return nos_gate(r);
}
/* 0x40e390: the action asks for nitrous when the AI may burn it */
static void act_nitrous(AiRacer *r, AiAction *a)
{
    a->nitrous = 0;
    if (r->owner_kind == 2 && !r->nos_burn && nos_decide(r)) a->nitrous = 1;
}
/* the controller's vt+0xb4 (0x408430): burned long enough (always in drag) */
static int nos_burned(const AiRacer *r)
{
    const AiWorld *w = r->w;
    if (w->drag || w->x890108) return 1;
    int skill = r->skill < 1 ? 0 : r->skill;
    return *blob_f(w, skill * 0x68 + 0x570) < w->time - r->nos_start;
}
static void nos_stop(AiRacer *r)                                        /* 0x592500 + vt+0xa8 */
{
    r->nos_burn = 0;
    r->nos_stop = r->w->time;
}

/* ---- the action set's vtables ------------------------------------------------ */
static void driveoffset_begin(AiRacer *r, AiAction *a);                /* aiencounter.c, 0x42bc50 */
static void act_begin(AiRacer *r, AiAction *a)                          /* vt+8 (via 0x41bf10) */
{
    switch (a->type) {
    case ACT_PASS: pass_begin(r, a); break;
    case ACT_AVOID: avoid_begin(r, a); break;
    case ACT_BLOCK: block_begin(r, a); break;
    case ACT_UNSTUCK: unstuck_begin(r, a); break;
    case ACT_HIT: hit_begin(r, a); break;
    case ACT_ACCIDENT: accident_begin(r, a); break;
    case ACT_STOP: stop_begin(r, a); break;
    case ACT_DRIVEOFFSET: driveoffset_begin(r, a); break;
    case ACT_HITWALL: hitwall_begin(r, a); break;
    case ACT_HITDRAG: hitdrag_begin(r, a); break;
    case TR_AVOID: case TR_DRIVE: case TR_OBEY: tr_exec(r, a); break;
    default: justdrive_begin(r, a); break;
    }
}
static void act_end(AiRacer *r, AiAction *a)                            /* vt+0xc */
{
    if (a->type == ACT_UNSTUCK) r->drv->cmd = 1;                        /* 0x40eb60: driver vt+0x60 */
}
static int act_every_frame(const AiAction *a) { return a->type == ACT_BLOCK || a->type == ACT_DRIVEOFFSET; }   /* vt+0x10: 0x570a60 / 0x404800 */
/* 0x403120: the status from the tests (vt+0x18 -> 1, vt+0x1c -> 2, vt+0x20
   -> 3); 0x401990: done on 1 or 2, or 3 with a time limit */
static int act_status(AiRacer *r, AiAction *a)
{
    float el = r->w->time - a->start;
    int t3 = el > a->dur && a->limited, t2 = 0, t1 = 0;                 /* 0x403250 / 0x403180 / 0x4031d0 */
    switch (a->type) {
    case ACT_PASS: t1 = pass_done(r, a); t2 = pass_fail(r, a); break;
    case ACT_AVOID: t1 = avoid_done(r, a); break;
    case ACT_BLOCK: t1 = block_done(r, a); t2 = block_passed(r, a); break;
    case ACT_UNSTUCK: t1 = unstuck_done(r, a); t2 = unstuck_fail(r, a); break;
    case ACT_HIT: case ACT_HITDRAG: t1 = hit_done(r, a); break;       /* 0x41cc90 both */
    case ACT_HITWALL: t1 = !wall_contact(r); break;                     /* 0x4034a0 */
    case ACT_ACCIDENT: t1 = accident_done(r, a); break;
    case TR_AVOID: case TR_OBEY: t1 = tr_done(r, a); break;
    default: break;
    }
    a->status = t1 ? 1 : t2 ? 2 : t3 ? 3 : 0;
    return a->status == 1 || a->status == 2 || (a->status == 3 && a->limited);
}
/* the goal "Win road race"'s chooser 0x42d0f0, in its order: GetUnstuck,
   IJustGotHit, AvoidAvoidable, AccidentRecovery, Block, Pass, else JustDrive */
static AiAction *goal_choose(AiRacer *r, int cur, int prio)
{
    AiWorld *w = r->w;
    const AiCarView *c = &r->car;
    if (r->goal == GOAL_PARK) return goal_default(r);                  /* Park's chooser 0x402780 */
    if (goal_drag(r->goal)) return drag_choose(r, cur, prio);           /* 0x42d470 / 0x426a70 */
    if (cur != ACT_UNSTUCK && prio <= 500 && 6.0f < w->time - r->unstuck_time &&   /* vt+0x94 0x4083a0 (0x783b50) */
        unstuck_prio(r) >= 0) {
        /* vt+0x98 0x4083e0: back off for 1.6 s when off the road (vt+0x2c
           0x40d4f0) with five samples of no progress, else 1.3 */
        float hold = 1.3f;                                              /* 0x783b48 */
        if (r->rpt) {
            int back = !r->rdir;
            float out = pt_outside(r->rpt, pt_latq(w->net, r->rpt, c->pos, back), back);
            if (0 < out && r->prog5.count == r->prog5.n && r->prog5.avg < 0.2f) hold = 1.6f;   /* 0x7e9574 / 0x783b4c */
        }
        r->unstuck_time = w->time;
        return unstuck_new(500, hold);
    }
    int car = -1;
    if (cur != ACT_HIT && prio <= 400 && hit_prio(r, &car) >= 0) {
        racer_react(r, car);
        AiAction *a = act_new(ACT_HIT, 400, -1.0f);                     /* 0x423cc0 */
        if (a) a->target = car;
        return a;
    }
    float t = 99999.0f;
    if (cur != ACT_AVOID && prio < 301 && avoid_prio(r, &car, &t) >= 0) {
        racer_react(r, car);
        if (racer_approve(r, car)) return avoid_new(r, 300, car, 2.0f, t);
    }
    if (!(cur == ACT_ACCIDENT || 500 < prio || 1 < c->wheels || car_upright(c, 0.707f)))
        return accident_new(r, 500);
    if (r->goal == GOAL_PURSUIT) return goal_default(r);                /* 0x42da70: no Block, no Pass */
    int bprio;
    memcpy(&bprio, w->blob + 0x934 + (r->block & 0xff) * 8, 4);
    int pb;
    if (!w->streetx && !w->x89010c && cur != ACT_BLOCK && prio <= bprio && (pb = block_prio(r, &car)) >= 0) {
        AiAction *a = act_new(ACT_BLOCK, pb, -1.0f);                    /* 0x4238c0 */
        if (a) a->target = car;
        return a;
    }
    AiLane ls = { -1, 0, 0, 0 };
    int pp;
    if (cur == ACT_PASS || 100 < prio || (pp = pass_prio(r, &car, &ls)) < 0 || !racer_approve(r, car))
        return act_new(ACT_JUSTDRIVE, 0, -1.0f);                        /* 0x4269e0 */
    r->ls = ls;
    return pass_new(r, pp, car);
}

/* 0x402180: may the car ride the far mover (4)? Not in drag / drift-type
   races, not while either mover timer runs, a plan of 3 points or more, at
   5 m/s or more on at least one wheel, an action other than 8 / 9 / 10 / 11
   / 13, an AI (skill != -2), 120 m or more from every view, 30 s into the
   race and not finished. (Event 1099's drag-goal case, 0x86b320 /
   0x86b328 == 4, is a mode the port does not have.) */
static int far_ok(const AiRacer *r)
{
    const AiWorld *w = r->w;
    if (w->drag || w->x890108 || w->drift || w->x890104) return 0;     /* 0x89e7d8 0x890108 0x89e7d9 0x890104 */
    if (r->dplan.n <= 2) return 0;                                      /* driver +0x10 */
    if (0 < r->far_hold || 0 < r->phys_time) return 0;                 /* 0x401820(+0x730 / +0x734) */
    if (r->car.speed < 5.0f || r->car.wheels <= 0) return 0;            /* car +0x42c (0x783504), +0x424 */
    if (!r->act) return 0;
    int ty = r->act->type;
    if (ty == 8 || ty == 9 || ty == 10 || ty == 11 || ty == 13) return 0;
    if (r->skill == -2) return 0;                                       /* vt+0x78 */
    float view = 99999.0f;                                              /* 0x5f1160 */
    for (int i = 0; i < w->ncam; i++) {
        float d = d3(r->car.pos, w->cam[i]);
        if (d < view) view = d;
    }
    if (view < 120.0f || w->race_time < 30.0f) return 0;                /* 0x78350c, 0x783508 */
    if (r->in_race && r->entry_done) return 0;                          /* 0x5ee250 */
    return 1;
}
/* the think (0x42abd0, every 0.2 s at least, in the car's frame slot) */
static void goal_think(AiRacer *r);                                     /* aiencounter.c */
static void racer_think(AiRacer *r)
{
    AiWorld *w = r->w;
    r->think_time = w->time;                                            /* +0x778 */
    r->think_left += 0.1f;                                              /* +0x6f0 */
    if (r->finished) { racer_stop(r); goto out; }
    if (!r->goal) {                                                     /* vt+0x88 0x42c060 */
        /* (event 1099 with 0x86b320 / 0x86b328 == 4 takes the drag goals
           too: a mode the port does not have) */
        if (w->drag || w->x890108) {
            r->goal = r->owner_kind == 1 ? GOAL_DRAG_PLAYER : GOAL_DRAG_WIN;
            goal_lanes(r);                                              /* 0x42aa20 -> 0x428490 */
        } else r->goal = GOAL_ROAD_RACE;
    }
    if (!r->act) {
        r->act = goal_default(r);                                       /* goal vt+0x3c */
        if (!r->act) goto out;
        r->act->start = w->time;
        if (r->dplan.n == 0) {
            AiPlan P;
            memset(&P, 0, sizeof P);
            plan_straight(&P, lookahead(r), &r->car);                   /* 0x420b40 + 0x425910 */
            driver_setplan(r, &P);
        }
    }
    goal_think(r);                                                      /* goal vt+0x44 */
    linkmem_tidy(r);
    uint64_t tp = SDL_GetPerformanceCounter();
    int okpath = racer_updatepath(r);
    ap_add(AP_TH_PATH, tp);
    if (!okpath) {
        /* the road race goal (kind 3) asks to be put back on the road
           (car +0x96c, placed by 0x60139f) */
        if (r->goal == GOAL_PURSUIT || r->goal == GOAL_PARK) racer_stop(r);   /* category 4: SteerStraightAhead, +0x710 */
        else if (w->mode != 7) r->want_reset = 1;
        goto out;
    }
    {
        float own = r->own >= 0 ? r->ent[r->own].pathpos : 0;
        float last = r->L.n ? r->L.p[r->L.n - 1]->dist : 0;
        if (last < own) { racer_stop(r); goto out; }
        if (fl_atend(w->net, &r->L, own)) r->at_end = 1;               /* +0x77c */
        if (r->at_end) { racer_stop(r); goto out; }
    }
    if (r->owner_kind == 2 && r->nos_burn && !nos_decide(r) && nos_burned(r)) nos_stop(r);
    /* 0x403120 / 0x401990: a finished action ends and the goal picks the
       next; else the goal may replace it with one of higher priority */
    uint64_t tc = SDL_GetPerformanceCounter();
    if (act_status(r, r->act)) {
        act_end(r, r->act);
        act_free(r->act);
        r->act = NULL;
        r->act = goal_choose(r, ACT_CHOOSE, -1);
        if (!r->act) r->act = goal_default(r);
        if (!r->act) goto out;
        r->act->start = w->time;
    } else {
        AiAction *a = goal_choose(r, r->act->type, r->act->prio);      /* 0x402360 */
        if (a && r->act->prio < a->prio) {
            act_end(r, r->act);
            act_free(r->act);
            r->act = a;
            a->start = w->time;
        } else act_free(a);
    }
    ap_add(AP_TH_CHOOSE, tc);
    tc = SDL_GetPerformanceCounter();
    act_begin(r, r->act);                                               /* vt+0xb0 0x41bf10 */
    ap_add(AP_TH_BEGIN, tc);
    {
        int ty = r->act->type;
        double ms = (double)(SDL_GetPerformanceCounter() - tc) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (ty >= 0 && ty < AP_TYPES) {
            apt_sum[ty] += ms; apt_cnt[ty]++;
            if (ms > apt_max[ty]) apt_max[ty] = ms;
        }
    }
    tc = SDL_GetPerformanceCounter();
    racer_apply(r);
    ap_add(AP_TH_APPLY, tc);
    /* the think's end (0x42b26c, 0x7e8d70 = 1): onto mover 4 or a new path
       for it (0x5918a0), or back to the chooser 0x5f4400 */
    if (far_ok(r)) { r->far = 1; r->far_repath = 1; }
    else r->far = 0;
out:
    r->nent = 0; r->own = -1;                                           /* 0x41bf80 */
    r->force_replan = 0;
}
/* 0x420810: the driver takes the action's plan, then the catch-up (vt+0x80)
   and the goal's speed; nitrous on when the action asked; the driver's
   filters restart on a fresh plan */
static void racer_apply(AiRacer *r)
{
    AiWorld *w = r->w;
    AiAction *a = r->act;
    driver_setplan(r, &a->plan);
    racer_catchup(r);
    goal_speed(r);
    if (a->nitrous) {
        if (!r->nos_burn) r->nos_start = w->time;                       /* vt+0xa4 (+0x7a8) */
        if (!r->nos_burn && 0 < r->nos_frac) r->nos_burn = 1;           /* 0x59faf0 */
    }
    if (a->plan.time == w->time &&
        (w->drag || w->x890108 || !(w->x890114 || w->drift || w->x890104) || !r->in_race))
        r->drv->cmd = 4;
    plan_keep(&a->plan, NULL, w->time, 0);
}

/* ---- the frame -------------------------------------------------------------- */
static void racer_frame_ctrl(AiRacer *r, float dt)
{
    AiWorld *w = r->w;
    if (0 < r->far_hold) {                                              /* 0x42abd0: +0x730 running */
        r->far = 0;                                                     /* mover 4 -> 0x5f4400 */
        r->far_hold -= dt;                                              /* 0x577310 */
        if (r->far_hold <= 0) r->far_hold = 0;
    }
    r->think_left -= dt;
    r->nent = 0; r->own = -1;
    if (r->nos_burn && !(0 < r->nos_frac)) r->nos_burn = 0;            /* 0x59fa30: the bottle ran dry */
    /* AI racers think in their slot of the 8-frame cycle (4 in drag), at
       least 0.2 s apart (0x42abd0) */
    int cyc = (w->drag || w->x890108) && r->car.mover != 4 ? 4 : 8;
    int k = (int)(w->frame % (unsigned)cyc);
    int turn = k < w->nai && k == r->slot;
    if (r->owner_kind == 1) turn = (int)(w->frame % 8u) == r->carno;   /* a human-owned car: frame % 8 == car +10 */
    if ((!(w->time - r->think_time < 0.2f) && turn) || !r->act) {      /* 0x784554 */
        uint64_t t0 = SDL_GetPerformanceCounter();
        racer_think(r);
        ap_add(AP_THINK, t0);
        return;
    }
    if (r->owner_kind == 2 && r->nos_burn && !nos_gate(r) && nos_burned(r)) nos_stop(r);
    if (r->finished) return;
    goal_speed(r);                                                      /* goal vt+0x34 */
    if (r->car.mover != 4 && act_every_frame(r->act)) {                 /* action vt+0x10 */
        uint64_t t0 = SDL_GetPerformanceCounter();
        int ok = racer_updatepath(r);
        ap_add(AP_FR_PATH, t0);
        t0 = SDL_GetPerformanceCounter();
        if (ok) { act_begin(r, r->act); racer_apply(r); }
        else racer_stop(r);                                             /* 0x402530 */
        ap_add(AP_FR_APPLY, t0);
        r->nent = 0; r->own = -1;
        r->force_replan = 0;
    }
}

/* 0x42c0c0 (the racer's tick) and its mover rules: full simulation until
   the race runs, for 5 s after touching a human player's car (and on until
   all four wheels are down), else the Mellovator */
static void racer_tick(AiRacer *r, float dt)
{
    AiWorld *w = r->w;
    if ((w->drag || w->x890108) && drag_start(r)) return;              /* the start delay (+0x75c) */
    if (!w->race_active && !w->drag && !w->x890108) {
        driver_setspeed(r->drv, 0, 0, 0, 1);
        r->phys_time = 5.0f;                                            /* 0x783518 */
    }
    if (0 < r->phys_time) {
        r->phys_time -= dt;
        if (r->phys_time <= 0) {
            r->phys_time = 0;
            if (r->car.wheels < 4) r->phys_time = 1.0f;                 /* 4000 ticks */
        }
    }
    /* every 1.5 s the progress along the route (race entry +0x18 x the lap
       length) into two rings; ten slow samples running (7.5 s) with no
       view within 50 m ask to be put back on the road */
    if (w->race_active && w->mode != 0 && w->mode != 7) {
        if (!(w->time < r->prog_next)) {                                /* +0x7a0 (0x7e9578) */
            r->prog_next = w->time + 1.5f;
            ring_push(&r->prog5, fabsf(r->race_rate) * w->track_len);
        }
        if (r->act && r->act->type != 8 && !r->entry_done && !(w->time < r->stall_next)) {   /* +0x738 (0x7834a4), 0x5ee250 */
            r->stall_next = w->time + 1.5f;
            ring_push(&r->prog10, fabsf(r->race_rate) * w->track_len);
            float view = 99999.0f;                                      /* 0x5f1160 */
            for (int i = 0; i < w->ncam; i++) {
                float d = d3(r->car.pos, w->cam[i]);
                if (d < view) view = d;
            }
            if (!(since_reset(r) < 2.0f) && r->prog10.count == r->prog10.n &&   /* 0x5ebfa0 (0x7a55c0) */
                r->prog10.avg < 0.2f && !(view < 50.0f))                /* 0x7834a0 / 0x783514 */
                r->want_reset = 1;
        }
    }
    if (w->race_active) racer_frame_ctrl(r, dt);
}

/* ---- a human player's controller (type 1: ctor 0x4266d0, vtable 0x785230) --
   The original keeps a controller on the human's car as well. It tracks the
   car on the road net as a racer does (route point, branch choices, its own
   FuturePoint list at +0x68) and every frame predicts where the car goes:
   the plan its driver holds (vt+0x3c, 0x40cf60) is what the AI's collision
   prediction sees of the human. The player object (0x602650) samples the
   car's pace against the racing line's speeds once a second; the AI's
   catch-up reads it. The free-roam GPS route (0x42a360, +0x3e4) plays no
   part in a race. */

/* 0x5d8b00: a route point's distance measured on the race route: how far
   its progress lies from `prog`, less 50 m, at least its distance in space */
static float pt_progdist(const AiWorld *w, Pt *p, const float *q, float prog)
{
    float pz[3] = { pt_x(p), pt_y(p), pt_z(p) };
    float d = d3(pz, q), dp = rdf(p + 8) - prog;
    if (w->circuit) {
        if (dp < -w->track_half) dp += w->track_len;
        else if (w->track_half < dp) dp -= w->track_len;
    }
    float m = fabsf(dp) - 50.0f;                                        /* 0x785b80 */
    return m < d ? d : m;
}
/* 0x5e2e40: the section's point nearest q within `maxd`. flags 1: q lies
   more than 0.1 m behind it, 2: not; 4: a racing-route point (0x800); 0x20:
   measured on the race route; 0x200 / 0x400: point flags 0x2000000 /
   0x8000000 */
static Pt *sec_nearest(const AiWorld *w, const unsigned char *sec, const float *q, float maxd, unsigned flags, float prog)
{
    if (!sec) return NULL;
    if (!(rdf(sec + 0x60) - maxd <= q[0] && q[0] <= rdf(sec + 0x68) + maxd &&      /* 0x43d410 */
          rdf(sec + 0x64) - maxd <= q[1] && q[1] <= rdf(sec + 0x6c) + maxd)) return NULL;
    int onroute = (flags & 0x20) && world_race_route(w);
    Pt *best = NULL;
    float bd = maxd;
    int np = sec_npts(sec);
    for (int i = 0; i < np; i++) {
        Pt *p = sec_pt(sec, i);
        float d, ahead;
        if (onroute) {
            d = pt_progdist(w, p, q, prog);
            ahead = rdf(p + 8) - prog;
            if (w->circuit) {
                if (ahead < -w->track_half) ahead += w->track_len;
                else if (w->track_half < ahead) ahead -= w->track_len;
            }
        } else {
            float pz[3] = { pt_x(p), pt_y(p), pt_z(p) };
            d = d3(pz, q);
            ahead = -((q[0] - pt_x(p)) * pt_dx(p) + (q[1] - pt_y(p)) * pt_dy(p));
        }
        uint32_t f = pt_flags(p);
        if ((!(flags & 1) || 0.1f < ahead) && (!(flags & 2) || ahead < 0.1f) && d < bd &&      /* 0x784358 */
            (!(flags & 4) || (f & 0x800)) && (!(flags & 0x200) || (f & 0x2000000)) &&
            (!(flags & 0x400) || (f & 0x8000000))) { bd = d; best = p; }
        int st = -2 - (int)(-0.02f * d);                                /* 0x7852ac */
        if (st > 0) i += st;
    }
    return best;
}
/* how far q lies outside the road at p across (0x40cd00 / 0x401b70; back:
   0x40cd90 / 0x401bc0) and along (0x40ce90 / 0x40ce10; back 0x40cee0),
   negative inside */
static void pt_outside2(const AiNet *n, Pt *p, const float *q, int back, float *lat, float *along)
{
    float dx = q[0] - pt_x(p), dy = q[1] - pt_y(p), d[2];
    if (!back) { d[0] = pt_dx(p); d[1] = pt_dy(p); } else pt_backdir(n, p, d);
    float l = dx * d[1] - dy * d[0], a = dx * d[0] + dy * d[1];
    if (!back) *lat = 0 < l ? l - pt_wr(p) : -(pt_wl(p) + l);
    else       *lat = l < 0 ? -(pt_wr(p) + l) : l - pt_wl(p);
    if (!back) *along = 0 < a ? a - pt_seglen(p) : -(pt_prevseglen(n, p) + a);
    else       *along = a < 0 ? -(pt_seglen(p) + a) : a - pt_prevseglen(n, p);
}
static int sec_has_lanes(const unsigned char *sec)                      /* 0x5d4460 */
{
    for (int k = 0; k < 8; k++) if (sec[0x84 + k]) return 1;
    return 0;
}
/* TopologyCoordinate wall tests (HandleReset 0x6009a0, GetNearestRoutePoint
   flag 0x1000). The polygons are the 0x5e0d30 gather (wcoll_gather); the
   cached interest box is p +- (r + 0.5) (SetInterestBBox 0x5e6fa0). */
static WColl *s_topo_w;                     /* set for one road_search(.., 0x1000) */
static const float *s_topo_car;             /* car +0x60 for that search (NULL: the controller's car.pos) */
static WCollPoly s_topo_poly[160];          /* BuildTrackPolygons keeps at most 160 */
/* PointInShaft 0x5d35b0: q lies over the polygon -- every edge's
   cross(v_i - q, v_i+1 - q) . n >= 0 (0x43d1e0 = a x b); d = -(v0 - q) . n */
static int topo_shaft(const WCollPoly *p, const float q[3], float *dist)
{
    float e[4][3];
    for (int i = 0; i < p->nv; i++) for (int k = 0; k < 3; k++) e[i][k] = p->v[i][k] - q[k];
    for (int i = 0; i < p->nv; i++) {
        const float *a = e[i], *b = e[(i + 1) % p->nv];
        float c0 = a[1] * b[2] - a[2] * b[1], c1 = a[2] * b[0] - a[0] * b[2], c2 = a[0] * b[1] - a[1] * b[0];
        if (c0 * p->n[0] + c1 * p->n[1] + c2 * p->n[2] < 0.0f) return 0;
    }
    if (dist) *dist = -(e[0][0] * p->n[0] + e[0][1] * p->n[1] + e[0][2] * p->n[2]);
    return 1;
}
static int topo_gather(WColl *w, const float c[3], float r)
{
    WCollCtx gc; wcoll_ctx_init(&gc);
    float mn[2] = { c[0] - r - 0.5f, c[1] - r - 0.5f }, mx[2] = { c[0] + r + 0.5f, c[1] + r + 0.5f };
    return wcoll_gather(w, &gc, c, mn, mx, s_topo_poly, 160);
}
/* SegmentIntersectsPossiblyLong 0x5e7ed0: pieces of at most 30 m (0x7a4644),
   each with its own interest box (mid, len / 2); TopologyCoordinate::
   SegmentIntersects 0x5d3920 tests ALL gathered polygons two-sided
   (0x5d37f0 -> 0x5d36f0: the ends strictly either side of the plane, the
   crossing p1 + |a| / (|a| + |b|) (p2 - p1) in the shaft). 0x5d37f0's bbox
   cull and nearest-hit distance do not change a yes / no answer. */
static int topo_seg_hits(WColl *w, const float p1[3], const float p2[3])
{
    float dv[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };
    float len = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    int n = 1;
    if (!(len < 30.0f)) { n = (int)(len / 30.0f); if ((float)n < len / 30.0f) n++; }
    for (int s = n - 1; s >= 0; s--) {                  /* from p2 toward p1 */
        float a[3], b[3], mid[3];
        for (int k = 0; k < 3; k++) {
            a[k] = p1[k] + dv[k] * (float)s / (float)n;
            b[k] = p1[k] + dv[k] * (float)(s + 1) / (float)n;
            mid[k] = 0.5f * (a[k] + b[k]);
        }
        int np = topo_gather(w, mid, 0.5f * len / (float)n);
        for (int i = 0; i < np; i++) {
            const WCollPoly *p = &s_topo_poly[i];
            float da = (a[0] - p->v[0][0]) * p->n[0] + (a[1] - p->v[0][1]) * p->n[1] + (a[2] - p->v[0][2]) * p->n[2];
            float db = (b[0] - p->v[0][0]) * p->n[0] + (b[1] - p->v[0][1]) * p->n[1] + (b[2] - p->v[0][2]) * p->n[2];
            if (!(da * db < 0.0f)) continue;
            float t = fabsf(da) / (fabsf(da) + fabsf(db)), x[3];
            for (int k = 0; k < 3; k++) x[k] = a[k] + (b[k] - a[k]) * t;
            if (topo_shaft(p, x, NULL)) return 1;
        }
    }
    return 0;
}
/* PointIsCloserThanThisToWall 0x5e8110: interest box (p, 2 dist); only the
   flag-0x2 wall list (+0x2e4, count +0x2e8); over one with |d| <= dist */
static int topo_near_wall(WColl *w, const float p[3], float dist)
{
    int np = topo_gather(w, p, 2.0f * dist);
    for (int i = 0; i < np; i++) {
        float d;
        if ((s_topo_poly[i].flags & 2) && topo_shaft(&s_topo_poly[i], p, &d) && fabsf(d) <= dist) return 1;
    }
    return 0;
}
/* 0x40f180: the nearest route point over all sections. flags as 0x5e2e40,
   and 0x40: the car inside the road at it; 0x80 / 0x100: sections with /
   without traffic lanes; 0x800: within 100 m; 0x2000: height within 4 m;
   0x1000 (0x40f390): a candidate under 90 m (0x784e8c) whose point (no +1 m,
   0x401a00) cannot see the car (+0x60) past the topology or lies within
   1.5 m of a wall is passed over */
static Pt *road_search(const AiRacer *r, unsigned flags)
{
    const AiWorld *w = r->w;
    const AiNet *n = w->net;
    const float *q = r->car.pos;
    float bd = (flags & 0x800) ? 100.0f : 999999.0f;
    Pt *best = NULL;
    for (int s = 0; s < SEC_MAX; s++) {
        const unsigned char *sec = n->sec[s];
        if (!sec) continue;
        if ((flags & 0x80) && !sec_has_lanes(sec)) continue;
        if ((flags & 0x100) && sec_has_lanes(sec)) continue;
        Pt *p = sec_nearest(w, sec, q, bd, flags, r->progress);
        if (!p) continue;
        float d;
        if ((flags & 0x20) && world_race_route(w)) d = pt_progdist(w, p, q, r->progress);
        else { float pz[3] = { pt_x(p), pt_y(p), pt_z(p) }; d = d3(pz, q); }
        if ((flags & 0x2000) && !(fabsf(q[2] - pt_z(p)) < 4.0f)) continue;   /* 0x78378c */
        if (flags & 0x40) {
            float lat, al;
            pt_outside2(n, p, q, w->reversed, &lat, &al);
            if (!(lat < 0) || !(al < 0)) continue;
        }
        if (d < bd && (flags & 0x1000) && s_topo_w && d < 90.0f) {
            float pz[3] = { pt_x(p), pt_y(p), pt_z(p) };
            int sh = topo_seg_hits(s_topo_w, pz, s_topo_car ? s_topo_car : q), nw = sh ? 0 : topo_near_wall(s_topo_w, pz, 1.5f);
#ifdef OPENUG2_SIM_PRE_H
            printf("reset repick: cand d %.1f pt (%.1f %.1f %.2f) car (%.1f %.1f %.2f) seg %d wall %d\n", (double)d,
                   (double)pz[0], (double)pz[1], (double)pz[2], (double)(s_topo_car ? s_topo_car : q)[0], (double)(s_topo_car ? s_topo_car : q)[1], (double)(s_topo_car ? s_topo_car : q)[2], sh, nw);
#endif
            if (sh || nw) continue;
        }
        if (d < bd) { bd = d; best = p; }
    }
    return best;
}
/* vt+0x30, 0x411490: find the road again -- the current section's nearest
   point within 50 m while the car is within 5 m of its stretch of road,
   else a search of every section (first: one with traffic lanes, the car
   on its road within 100 m and 4 m of height) */
static void human_findroad(AiRacer *r, int force)
{
    const AiNet *n = r->w->net;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    if (r->rpt) {
        Pt *p = sec_nearest(r->w, sec_of(n, r->rpt), r->car.pos, 50.0f, 0x420, r->progress);   /* 0x783c20 */
        r->rpt = p;
        if (p) {
            float lat, al;
            pt_outside2(n, p, r->car.pos, !r->rdir, &lat, &al);
            if (5.0f < lat || 5.0f < al) r->rpt = NULL;                 /* 0x783c24 */
        }
    }
    if (!r->rpt) {
        unsigned fl = force ? 0x420 : 0x460;
        Pt *p = road_search(r, fl | 0x28c0);
        if (!p) p = road_search(r, fl);
        r->rpt = p;
    }
}
/* 0x40a390: the car travels against the way it is tracked in (its nose or
   velocity more than 45 degrees off the way at its route point) */
static int human_dirflag(const AiRacer *r)
{
    int c = r->rdir;
    Pt *p = r->rpt;
    if (!p) return !c;
    float d[2], v[2];
    if (!c) pt_backdir(r->w->net, p, d); else { d[0] = pt_dx(p); d[1] = pt_dy(p); }
    if (r->car.absv * 2.237f <= 10.0f) { v[0] = r->car.fwd[0]; v[1] = r->car.fwd[1]; }   /* 0x78426c, 0x784560 */
    else v2norm(v, r->car.vel);
    if (v[1] * d[1] + v[0] * d[0] < -0.707f) return c;                  /* 0x7846e8 */
    return !c;
}
/* 0x418620: the car heads into a link's gate (both its gate points ahead,
   0..300 m past the link's progress): when the gate's road is not the one
   it is on, take the gate road's nearest point ahead (inside the road,
   height within 5 m) */
#ifdef OPENUG2_SIM_PRE_H
static long g_lg_cand, g_lg_calls;   /* LKCHECK: links tested per call */
#endif
static Pt *lg_scan(AiRacer *r, int use_index)
{
    const AiWorld *w = r->w;
    const AiNet *n = w->net;
    Pt *best = NULL;
    float bd = 0;
    /* port-only speed-up, same result: off a circuit the window is a plain
       progress range, so only the links the sorted index puts in it (padded
       2 m against rounding) are tested -- in link order, as the full loop */
    int cand[256], nc = -1;
    if (use_index && !w->circuit && n->lk_x && n->lk_n == n->nlinks) {
        /* d > 0 is exactly x < progress (x > progress reversed) for finite
           floats; the 300 m end is padded 2 m against rounding */
        float pr = r->progress;
        int lo = 0, hi = n->lk_n;
        if (!w->reversed) {   /* x in [pr - 302, pr) */
            float p0 = pr - 302.0f;
            while (lo < hi) { int m = (lo + hi) / 2; if (n->lk_x[m] < p0) lo = m + 1; else hi = m; }
        } else {              /* x in (pr, pr + 302] */
            while (lo < hi) { int m = (lo + hi) / 2; if (n->lk_x[m] <= pr) lo = m + 1; else hi = m; }
        }
        nc = 0;
        for (int k = lo; k < n->lk_n && (w->reversed ? n->lk_x[k] <= pr + 302.0f : n->lk_x[k] < pr); k++) {
            if (nc == 256) { nc = -1; break; }
            int v = n->lk_i[k], q = nc++;
            while (q > 0 && cand[q - 1] > v) { cand[q] = cand[q - 1]; q--; }
            cand[q] = v;
        }
    }
    int cnt = nc >= 0 ? nc : n->nlinks;
#ifdef OPENUG2_SIM_PRE_H
    { static int said; if (use_index && nc < 0 && said++ < 3) printf("lkcheck: full scan (circuit %d, index %d of %d, progress %.1f)\n", w->circuit, n->lk_n, n->nlinks, r->progress); }
#endif
#ifdef OPENUG2_SIM_PRE_H
    if (use_index) { g_lg_cand += cnt; g_lg_calls++; }
#endif
    for (int c = 0; c < cnt; c++) {
        int i = nc >= 0 ? cand[c] : c;
        Lk *l = n->links + i * 0x20;
        float d = progress_diff(w, r->progress, rdf(l));
        if (w->reversed) d = -d;
        if (!(!(d <= 0) && d < 300.0f)) continue;                       /* 0x7852a8 */
        Pt *g[2];
        for (int k = 0; k < 2; k++) {                                   /* 0x5d3fb0 / 0x5d3ff0 */
            int s = rds16(l + 0x18 + 2 * k), x = rds16(l + 0x1c + 2 * k);
            g[k] = (s != -1 && x != -1 && s >= 0 && s < SEC_MAX && n->sec[s]) ? sec_pt(n->sec[s], x) : NULL;
        }
        if (!g[0] || !g[1]) continue;
        const float *q = r->car.pos;
        int before = (q[0] - pt_x(g[0])) * pt_dx(g[0]) + (q[1] - pt_y(g[0])) * pt_dy(g[0]) < 0 &&
                     (q[0] - pt_x(g[1])) * pt_dx(g[1]) + (q[1] - pt_y(g[1])) * pt_dy(g[1]) < 0;   /* 0x40cbc0 */
        if (before) {
            float v[2];
            v2norm(v, r->car.vel);
            float db = v[0] * pt_dx(g[1]) + v[1] * pt_dy(g[1]), da = v[0] * pt_dx(g[0]) + v[1] * pt_dy(g[0]);
            if (bd < da) { bd = da; best = g[0]; }
            if (bd < db) { bd = db; best = g[1]; }
        }
    }
    return best;
}
static void human_linkgates(AiRacer *r)
{
    const AiWorld *w = r->w;
    const AiNet *n = w->net;
    /* (the tests are side-effect free and ANDed: the cheap ones go first --
       in free roam the loop covers the whole city's links every tick) */
    if (!(1.0f < r->car.speed)) return;
    Pt *best = lg_scan(r, 1);
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): OPENUG2_LKCHECK compares with the full loop */
    {   static int on = -1; static long nchk, nbad;
        if (on < 0) on = getenv("OPENUG2_LKCHECK") != NULL;
        if (on) {
            Pt *ref = lg_scan(r, 0);
            nchk++; if (ref != best) nbad++;
            if (ref != best || nchk == 1 || nchk % 600 == 0) printf("lkcheck: %ld ticks, %ld differ, %.1f of %d links tested a call\n", nchk, nbad, g_lg_calls ? (double)g_lg_cand / g_lg_calls : 0.0, n->nlinks);
        }
    }
#endif
    if (!best) return;
    int cur = (r->rpt && sec_of(n, r->rpt)) ? rdu16(sec_of(n, r->rpt) + 10) : -1;
    if ((int)(pt_flags(best) & 0x7ff) == cur) return;
    Pt *p = sec_nearest(w, sec_of(n, best), r->car.pos, 99999.0f, 1, 0);
    if (!p) return;
    float lat, al;
    pt_outside2(n, p, r->car.pos, 0, &lat, &al);                        /* 0x40cd00 */
    if (lat < 0 && fabsf(pt_z(p) - r->car.pos[2]) < 5.0f) {             /* 0x7843e8 */
        ctrl_setpoint(r, p);
        fl_reset(&r->L);
        for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    }
}
static float human_look(const AiRacer *r)                               /* 0x404910 with vt+0x60 = 2 (0x40a240) */
{
    float t = 2.0f * r->car.absv;
    return t <= 125.0f ? 125.0f : t;
}
/* 0x42a190: keep the car on its path list -- on it while within the list's
   span and its half width off the road; else find the road again -- then
   extend the list ahead */
static void human_trackpath(AiRacer *r)
{
    const AiNet *n = r->w->net;
    FList *L = &r->L;
    int at = -1, on = 0;
    if (L->n != 0) {
        float pp = 0, lat = 0;
        fl_locate(n, L, r->car.pos, &pp, &lat, 0);                      /* lane: 0x4116a0 */
        at = fl_closest(n, L, r->car.pos, 0, 0, 0, 0);
        if (at >= 0) {
            FPoint *f = L->p[at];
            if (L->p[0]->dist - 50.0f <= pp && pp < L->p[L->n - 1]->dist && fp_outside(n, f, lat, 0) < r->halfwidth) {
                r->rpt = pt_at(n, f->sec, f->pt);                       /* 0x401ed0 */
                int a = L->cur2, b = at;                                /* 0x4120c0 */
                int lo = a <= b ? a : b, hi = a <= b ? b : a;
                if (lo + 1 < L->n && hi < L->n)
                    for (int i = lo + 1; i <= hi; i++) if (L->p[i]->link) ctrl_passlink(r, L->p[i]->link);
                r->rdir = !f->back;
                on = 1;
            }
        }
    }
    if (!on) { human_findroad(r, 1); fl_reset(L); at = -1; }
    if (!r->rpt) { fl_reset(L); return; }
    fl_rebuild(r, at, r->rpt, !r->rdir, human_look(r));
}
/* 0x420b80: from the car's motion -- 0.1 s steps of its velocity and
   acceleration (up to 20, while it keeps its way, over `dist`); straight
   ahead (0x420a60) when it accelerates harder than 0.75 x its speed */
static void plan_motion(AiPlan *P, float dist, const AiCarView *c, float now)
{
    plan_keep(P, NULL, now, 0);
    float p[2] = { c->pos[0], c->pos[1] }, v[2] = { c->vel[0], c->vel[1] }, a[2] = { c->acc[0], c->acc[1] };
    if (!(v2len(a[0], a[1]) < v2len(v[0], v[1]) * 0.75f)) { plan_straight(P, dist, c); return; }   /* 0x785510 */
    float sum = 0;
    if (0 < dist)
        for (int k = 0; P->n < PLAN_MAX && k < 20; k++) {
            float nv[2] = { v[0] + a[0] * 0.1f, v[1] + a[1] * 0.1f };   /* 0x7837cc */
            float np[2] = { p[0] + nv[0] * 0.1f, p[1] + nv[1] * 0.1f };
            float q[3] = { np[0], np[1], 0 };
            plan_add(P, q, -1, -1, 255, 1, NULL);
            sum += v2len(p[0] - np[0], p[1] - np[1]);
            p[0] = np[0]; p[1] = np[1]; v[0] = nv[0]; v[1] = nv[1];
            if (nv[0] * c->vel[0] + nv[1] * c->vel[1] < 0 || !(sum < dist)) break;
        }
    P->valid = 0;
}
/* 0x41e860 (via 0x4221f0): along the path list from the car's point, at
   the lateral offset `cur` moving towards `target` at `rate` per metre,
   kept inside the road's edges */
static void human_planfill(AiRacer *r, AiPlan *P, float cur, float target, float rate)
{
    const AiNet *n = r->w->net;
    FList *L = &r->L;
    int lane = 0;                                                       /* 0x4116a0 */
    int i = P->n == 0 ? L->cur : fl_closest(n, L, P->p[P->n - 1], 1, lane, L->cur, 0.1f);
    if (i < 0) return;
    for (; i < L->n && P->n < PLAN_MAX; i++) {
        FPoint *f = L->p[i];
        float hi = fp_right(n, f, lane), lo = fp_left(n, f, lane);      /* vt+0x10 / vt+0xc */
        float v = cur < lo ? lo : cur;
        if (hi < v) v = hi;
        float q[3];
        fp_point(n, f, q, f->dist, v, lane);
        Pt *p = pt_at(n, f->sec, f->pt);
        if (p) plan_add(P, q, (int)(pt_flags(p) & 0x7ff), pt_index(n, p), lane, 1, &r->car);
        float step = fp_tonext(n, f) * rate;                            /* vt+0x14 */
        if (target < v) { cur = v - step; if (!(target < cur)) cur = target; }
        else { cur = v + step; if (!(cur < target)) cur = target; }
    }
}
/* the player object's pace sampler (0x602650, in game): every second the
   car's |v| over the racing line's speed at its route point (0 with no
   speed there and standing, else 1) into a ring of 10 */
static void human_pace(AiRacer *r, float dt)
{
    const AiNet *n = r->w->net;
    r->pace_timer -= dt;
    if (!(r->pace_timer < 0)) return;
    if (r->rpt && sec_of(n, r->rpt)) {                                  /* 0x40f690 */
        float b = (float)r->rpt[0x2c], s;
        if (0 < b) s = r->car.absv / b;
        else s = r->car.absv < 0.01f ? 0.0f : 1.0f;                     /* 0x78425c */
        ring_push(&r->pace, s);
        if (r->pace_max < r->pace.avg) r->pace_max = r->pace.avg;
    }
    r->pace_timer = 1.0f;                                               /* 0x7a5514 */
}

/* ---- the public side ----------------------------------------------------- */
AiWorld *aiworld_new(const AiNet *net, const void *blob, unsigned seed)
{
    AiWorld *w = (AiWorld *)calloc(1, sizeof *w);
    if (!w) return NULL;
    w->net = net;
    w->blob = (const unsigned char *)blob;
    w->rng = seed;
    w->shortcut_time = -1.0f;
    w->race_start = -1.0f;
    w->mode = 1;
    w->time_prev = -1.0f;
    w->catchup_weight = 1.0f;
    return w;
}
void aiworld_free(AiWorld *w) { free(w); }
void aiworld_setup(AiWorld *w, const AiRaceSetup *s)
{
    w->drag = s->drag; w->drift = s->drift; w->streetx = s->streetx; w->x89010c = s->x89010c;
    w->x890104 = s->x890104; w->x890108 = s->x890108; w->x890114 = s->x890114;
    w->mode = s->mode; w->circuit = s->circuit;
    w->track_len = s->track_len; w->track_half = s->track_len * 0.5f;
    w->difficulty = s->difficulty; w->shortcut_time = s->shortcut_time;
    w->catchup_opt = s->catchup_opt; w->traffic_level = s->traffic_level; w->laps = s->laps;
    w->sprint = s->sprint; w->humans = s->humans; w->catchup_weight = s->catchup_weight;
    w->event = s->event;
    w->reversed = s->reversed;
}
void aiworld_frame(AiWorld *w, float time, int race_state, int race_active, float race_start)
{
    w->time = time;
    w->race_state = race_state;
    w->race_active = race_active;
    w->race_start = race_start;
    w->race_time = race_start < 0 ? 0 : time - race_start;
    w->dt = w->time_prev < 0 || !(w->time_prev < time) ? 0 : time - w->time_prev;
    w->time_prev = time;
    w->frame++;
}
void aiworld_set_views(AiWorld *w, const float *eyes, int n)
{
    if (n > 4) n = 4;
    w->ncam = n < 0 ? 0 : n;
    for (int i = 0; i < w->ncam; i++) for (int k = 0; k < 3; k++) w->cam[i][k] = eyes[3 * i + k];
}
static void view_from(AiCarView *v, const AiCarInfo *c)
{
    memset(v, 0, sizeof *v);
    for (int i = 0; i < 3; i++) {
        v->pos[i] = c->pos[i]; v->vel[i] = c->vel[i]; v->fwd[i] = c->fwd[i];
        v->left[i] = c->left[i]; v->up[i] = c->up[i]; v->acc[i] = c->acc[i];
    }
    v->heading = (unsigned short)(int)floorf(c->heading * (32768.0f / 3.14159265f) + 0.5f);
    v->speed = c->speed; v->absv = c->absv;
    v->wheels = c->wheels;
    for (int i = 0; i < 4; i++) { v->contact[i] = c->contact[i]; v->slip[i][0] = c->slip[i][0]; v->slip[i][1] = c->slip[i][1]; }
    v->mover = c->mover; v->ctrl_type = c->ctrl_type;
    v->rear_frac = c->rear_frac; v->steer_gain = c->steer_gain;
    v->nos_level = c->nos_level;
}
static void enc_seen_race(AiWorld *w, AiSeen *s);                       /* aiencounter.c */
void aiworld_set_car(AiWorld *w, int slot, const AiCarInfo *c, int index)
{
    if (slot < 0 || slot >= 24) return;
    view_from(&w->view[slot], c);
    AiSeen *s = &w->seen[slot];
    AiRacer *keep = s->racer;
    memset(s, 0, sizeof *s);
    s->racer = keep;
    s->v = keep ? &keep->car : &w->view[slot];
    if (keep) keep->car = w->view[slot];
    s->half[0] = c->half_len; s->half[1] = c->half_wid;
    float a = c->acc[0] * c->vel[0] + c->acc[1] * c->vel[1] + c->acc[2] * c->vel[2];
    s->acc = c->absv > 0 ? a / c->absv : 0;                             /* 0x60be30 */
    s->progress = c->progress;
    s->index = index;
    s->active = c->active;
    s->race_dist = c->race_dist; s->race_pos = c->race_pos; s->human = c->human;
    s->kind = c->traffic ? 3 : c->human ? 1 : 2;
    if (w->mode == 7) enc_seen_race(w, s);                             /* the Outrun's RacingCars */
    s->wrong_way = c->wrong_way;
    s->in_race = c->in_race;
    if (keep) {
        s->plan = &keep->dplan;
        keep->progress = c->progress;
        keep->half_len = c->half_len;
        keep->rpm = c->rpm; keep->gear = c->gear; keep->band_lo = c->band_lo; keep->band_hi = c->band_hi;
        keep->nos_frac = c->nos_frac; keep->nos_cap = c->nos_cap;
        /* race entry +0x18 (0x5f7c30): 2/3 of the new rate */
        if (0 < w->dt) keep->race_rate = 0.667f * ((s->race_dist - keep->race_dist) / w->dt) + (1.0f - 0.667f) * keep->race_rate;
        keep->race_dist = s->race_dist;
    }
    if (w->nseen < slot + 1) w->nseen = slot + 1;
}
void aicar_from_n2(AiCarInfo *c, const N2Sim *sim, const N2State *st, const float prev_vel[3], float dt)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 3; i++) {
        c->pos[i] = st->pos[i]; c->vel[i] = st->vel[i];
        c->fwd[i] = st->rot[i]; c->left[i] = st->rot[3 + i]; c->up[i] = st->rot[6 + i];
        c->acc[i] = (prev_vel && dt > 0) ? (st->vel[i] - prev_vel[i]) / dt : 0;
    }
    c->heading = st->heading;
    c->speed = st->speed;
    c->absv = sqrtf(st->vel[0] * st->vel[0] + st->vel[1] * st->vel[1] + st->vel[2] * st->vel[2]);
    c->wheels = st->wheels_down;
    for (int i = 0; i < 4; i++) {
        c->contact[i] = (unsigned char)(st->wheel_contact[i] != 0);
        c->slip[i][0] = st->wheel_slipv[i][0]; c->slip[i][1] = st->wheel_slipv[i][1];
    }
    c->mover = st->mover;
    c->ctrl_type = 3;
    c->rear_frac = n2sim_param(sim, 0x1c0);
    c->steer_gain = n2sim_param(sim, 0x270);
    c->nos_level = st->nos_on ? 1 : 0;
    c->nos_frac = st->nos_frac; c->nos_cap = st->nos_cap;
    c->rpm = st->rpm; c->gear = st->gear;
    n2sim_gear_band(sim, &c->band_lo, &c->band_hi);
    c->active = 1;
    c->in_race = 1;
}
AiRacer *airacer_new(AiWorld *w, int slot, int skill, const N2Sim *sim, int suv, float half_wid)
{
    AiRacer *r = (AiRacer *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->drv = (AiDriver *)calloc(1, sizeof *r->drv);
    if (!r->drv) { free(r); return NULL; }
    r->w = w;
    r->index = slot;
    r->slot = w->nai++;
    r->skill = skill;
    r->owner_kind = 2;
    r->in_race = 1;
    r->suv = suv;
    r->halfwidth = half_wid;
    r->ls.lane = -1;
    r->last_kind2_link = -1;
    r->catchup = 1.0f;
    r->car_catchup = 1.0f;
    r->catch_time = -1e9f;
    r->catch_target = -1;
    r->allow_catchup = 1;
    r->think_time = -1e9f;
    r->rand_time = -1e9f;
    r->own = -1;
    r->rdir = 1;
    r->id = 100 + (slot > 0 ? slot - 1 : 0);
    r->block = 1;
    r->approve_car = -1;
    r->reset_time = -1.0f;
    r->unstuck_time = -1e9f;
    r->nos_start = r->nos_stop = -1e9f;
    r->horn_t = -1e9f;
    r->half_len = 2.2f;
    r->mass = n2sim_param(sim, 0x110);
    r->aero = n2sim_param(sim, 0x264) + n2sim_param(sim, 0x268);
    r->grip = (n2sim_param(sim, 0xdc) + n2sim_param(sim, 0xfc)) * 0.5f;
    r->drag260 = n2sim_param(sim, 0x260);
    ring_init(&r->prog5, 5);                                            /* 0x7e957c */
    ring_init(&r->prog10, 10);
    r->perf.vmax = n2sim_accel_curve(sim, r->perf.acc);
    {
        float rating = n2sim_accel_rating(sim);
        static const float THR[4] = { 0, 273, 401, 500 };               /* 0x7a1a84 */
        int k = 3;
        for (int i = 3; i >= 0; i--) if (THR[i] < rating) { k = 3 - i; break; }
        r->perf.klass = k;
    }
    fl_reset(&r->L);
    driver_init(r->drv, r);
    if (slot >= 0 && slot < 24) {
        w->seen[slot].racer = r;
        w->seen[slot].v = &r->car;
        if (w->nseen < slot + 1) w->nseen = slot + 1;
    }
    return r;
}
AiRacer *airacer_new_human(AiWorld *w, int slot, const N2Sim *sim, int suv, float half_wid)
{
    AiRacer *r = (AiRacer *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->drv = (AiDriver *)calloc(1, sizeof *r->drv);     /* (the human's PlayerSteering: only its plan is read) */
    if (!r->drv) { free(r); return NULL; }
    r->w = w;
    r->index = slot;
    r->slot = -1;
    r->human = 1;
    r->skill = -2;                                      /* racer info +0x901 */
    r->owner_kind = 1;
    r->in_race = 1;
    r->suv = suv;
    r->halfwidth = half_wid;                            /* 0x5ec250: half the collision box's width */
    r->ls.lane = 0;                                     /* vt+0x1c 0x418570 */
    r->last_kind2_link = -1;
    r->catchup = r->car_catchup = 1.0f;
    r->catch_target = -1;
    r->own = -1;
    r->rdir = 1;
    r->approve_car = -1;
    r->reset_time = -1.0f;
    r->half_len = 2.2f;
    ring_init(&r->pace, 10);                            /* 0x5f1a50 */
    r->perf.vmax = n2sim_accel_curve(sim, r->perf.acc);
    {
        float rating = n2sim_accel_rating(sim);
        static const float THR[4] = { 0, 273, 401, 500 };               /* 0x7a1a84 */
        int k = 3;
        for (int i = 3; i >= 0; i--) if (THR[i] < rating) { k = 3 - i; break; }
        r->perf.klass = k;
    }
    fl_reset(&r->L);
    if (slot >= 0 && slot < 24) {
        w->seen[slot].racer = r;
        w->seen[slot].v = &r->car;
        if (w->nseen < slot + 1) w->nseen = slot + 1;
    }
    return r;
}
/* the human controller's tick (0x42caa0) and the player's pace sampler */
void airacer_human_frame(AiRacer *r, float dt)
{
    AiWorld *w = r->w;
    const AiNet *n = w->net;
    FList *L = &r->L;
    int u = human_dirflag(r);                                           /* 0x40a390 */
    if (u == r->rdir) {
        fl_reset(L);
        for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
        r->rdir = !u;
    }
    linkmem_tidy(r);                                                    /* 0x41d0e0 */
    hitch_mark("rt.hum_pre");
    human_linkgates(r);                                                 /* 0x418620 */
    hitch_mark("rt.hum_gates");
    human_trackpath(r);                                                 /* 0x42a190 */
    hitch_mark("rt.hum_track");
    AiPlan P;
    memset(&P, 0, sizeof P);
    plan_keep(&P, NULL, w->time, 0);
    if (L->n == 0) {
        if (r->car.speed < 10.0f) plan_straight(&P, human_look(r), &r->car);   /* 0x783c34 */
        else plan_motion(&P, human_look(r), &r->car, w->time);          /* 0x4259c0 */
    } else {
        float pp = 0, lat = 0;
        fl_locate(n, L, r->car.pos, &pp, &lat, 0);                      /* 0x419bc0 */
        float rate;
        if (r->rpt == r->rpt_prev) rate = 0.0001f < dt ? (lat - r->lat_prev) / dt : 0;   /* 0x7843ec */
        else rate = r->lat_rate;
        r->rpt_prev = r->rpt;
        r->lat_prev = lat;
        r->lat_rate = 0.05f * rate + (1.0f - 0.05f) * r->lat_rate;      /* 0x783c3c */
        if (r->car.speed < 10.0f) plan_straight(&P, human_look(r), &r->car);   /* 0x425910 */
        else {                                                          /* 0x4221f0 */
            plan_keep(&P, NULL, w->time, 0);
            human_planfill(r, &P, lat, lat, 0);
        }
        /* (0x81cbb0, never set in the retail game, would bend the plan
           towards where the car is drifting) */
    }
    r->dplan = P;                                                       /* driver vt+0x3c (0x40cf60) */
    hitch_mark("rt.hum_plan");
    human_pace(r, dt);
}
void airacer_free(AiRacer *r)
{
    if (!r) return;
    /* the AI manager's think list (0x890080 +0x1c) drops a destroyed
       controller: the racers after it move up a place. (The pre-race
       scene's hand-off replaces every AI controller; without this the new
       ones got slots 3..5 and, in a drag race's 4-frame cycle, slots 4 and
       5 never thought again -- two AIs drove the start's plan into a wall.) */
    if (r->slot >= 0) {
        for (int i = 0; i < 24; i++) {
            AiRacer *o = r->w->seen[i].racer;
            if (o && o != r && o->slot > r->slot) o->slot--;
        }
        if (0 < r->w->nai) r->w->nai--;
    }
    for (int i = 0; i < 24; i++) if (r->w->seen[i].racer == r) { r->w->seen[i].racer = NULL; r->w->seen[i].v = &r->w->view[i]; }
    fl_reset(&r->L);
    act_free(r->act);
    free(r->drv);
    free(r);
}
void airacer_set_racer(AiRacer *r, int id, int block)
{
    r->id = id;
    r->block = block;
}
void airacer_placed(AiRacer *r)                                          /* car +0x9c0 / +0x974 */
{
    r->reset_time = r->w->time;
}
/* the car's reset (0x6012b0 on car +0x96c -> 0x5fbed0): on its route
   point's line, facing the section's way (+0x70 bit 1: backward), at its
   speed along that way, at most 8 m/s; the controller starts over
   (vt+0x34 0x40d550) */
int airacer_reset_pose(AiRacer *r, float pos[3], float *heading, float *speed)
{
    const AiNet *n = r->w->net;
    Pt *p = r->rpt;
    if (!p) return 0;
    const unsigned char *sec = sec_of(n, p);
    int back = sec && (rd32(sec + 0x70) & 2);
    float d[2] = { pt_dx(p), pt_dy(p) };
    if (!r->rdir) pt_backdir(n, p, d);                                  /* 0x401b20 */
    float k = d[0] * r->car.fwd[0] + d[1] * r->car.fwd[1];
    if (k <= 0) k = 0;
    float v = r->car.speed <= 0 ? 0 : r->car.speed;
    float s = k * v;
    if (!(s < 8.0f)) s = 8.0f;                                          /* 0x8021cc */
    float o[2] = { pt_dx(p), pt_dy(p) };
    if (back) pt_backdir(n, p, o);
    pos[0] = pt_x(p); pos[1] = pt_y(p); pos[2] = pt_z(p);
    *heading = atan2f(o[1], o[0]);
    *speed = s;
    r->rdir = !back;
    /* 0x40d550 */
    r->think_left = (float)r->index * 0.1f * -0.0454545f;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    r->finished = 0;
    r->at_end = 0;
    r->drv->cmd = 1;
    fl_reset(&r->L);
    r->reset_time = r->w->time;                                         /* 0x5f52c0 (+0x9c0), 0x5ebf70(2) */
    r->want_reset = 0;
    return 1;
}
/* Player::HandleReset 0x6009a0's route reset (-> Car::ResetToRoutePoint
   0x5fbed0). The speed is the car's (+0x42c, >= 0) times the way . the car's
   forward (+0x90), at most 8 m/s: the way is the locator's direction (car
   +0x900, race_dir) when a race route is loaded (0x88f2be), else the route
   point's own way, reversed when the controller runs backward (+0x10 == 0,
   GetPreviousPoint). The heading is the point's way, reversed (-1) when the
   section runs backward (+0x70 bit 1) in a race, or when the controller runs
   backward in free roam. The controller starts over as the AI's (vt+0x18 /
   vt+0x34). */
int airacer_player_reset_pose(AiRacer *r, const float *race_dir, struct WColl *wc, const float box[3], const float org[3],
                              float pos[3], float *heading, float *speed, int *repicked)
{
    const AiNet *n = r->w->net;
    Pt *p = r->rpt;
    if (repicked) *repicked = 0;
    if (!p) return 0;
    const unsigned char *sec = sec_of(n, p);
    int back = race_dir ? sec && (rd32(sec + 0x70) & 2) : !r->rdir;
    float d[2];
    if (race_dir) { d[0] = race_dir[0]; d[1] = race_dir[1]; }
    else { d[0] = pt_dx(p); d[1] = pt_dy(p); if (!r->rdir) pt_backdir(n, p, d); }
    float k = d[0] * r->car.fwd[0] + d[1] * r->car.fwd[1];
    if (k <= 0) k = 0;
    float v = r->car.speed <= 0 ? 0 : r->car.speed;
    float s = k * v;
    if (!(s < 8.0f)) s = 8.0f;                                          /* 0x8021cc */
    /* 0x600b58..0x600cfa: the point 1 m up (short +0x12 << 13); when the
       segment from it to the car's box centre crosses the topology
       (SegmentIntersectsPossiblyLong(pt, box, 0, 0, 1)) or it lies within
       1.5 m of a wall (0x5e8110), GetNearestRoutePoint(car, 0x1020, car
       +0x8fc) picks the point (no +1 m, 0x401a00) and the way: with a race
       route its section's +0x70 bit 1 backward, else backward when its way .
       the car's forward (+0x90) < 0. Skipped (wc NULL) when
       IsTooFarFromDragTrack 0x5eaec0. Then the 150 m (0x78a584) test from the
       box centre (0x600cfa). */
    float pt[3] = { pt_x(p), pt_y(p), pt_z(p) + 1.0f };
#ifdef OPENUG2_SIM_PRE_H
    if (wc && box) {   /* sim check: the tests at this reset, and a through-the-ground / in-the-air pair */
        float lo[3] = { box[0], box[1], box[2] - 50.0f }, hi[3] = { box[0], box[1], box[2] + 3.0f }, hi2[3] = { box[0], box[1], box[2] + 1.0f };
        printf("reset topo: seg %d wall %d (point %.1f %.1f %.1f, box %.1f %.1f %.1f) | selftest down %d up %d\n",
               topo_seg_hits(wc, pt, box), topo_near_wall(wc, pt, 1.5f), (double)pt[0], (double)pt[1], (double)pt[2],
               (double)box[0], (double)box[1], (double)box[2], topo_seg_hits(wc, hi2, lo), topo_seg_hits(wc, hi2, hi));
    }
    int force_repick = wc && box && getenv("OPENUG2_RS_FORCE_REPICK") != NULL;   /* sim: run the re-pick path */
#else
    const int force_repick = 0;
#endif
    if (wc && box && (force_repick || topo_seg_hits(wc, pt, box) || topo_near_wall(wc, pt, 1.5f))) {
        s_topo_w = wc; s_topo_car = org;
        Pt *q = road_search(r, 0x1020);
        s_topo_w = NULL; s_topo_car = NULL;
#ifdef OPENUG2_SIM_PRE_H
        printf("reset repick: %s\n", q ? "picked" : "none");
#endif
        if (q) {
            p = q; sec = sec_of(n, p);
            pt[0] = pt_x(p); pt[1] = pt_y(p); pt[2] = pt_z(p);
            if (race_dir) back = sec && (rd32(sec + 0x70) & 2);
            else back = pt_dx(p) * r->car.fwd[0] + pt_dy(p) * r->car.fwd[1] < 0.0f;   /* 0x401a60 */
            if (repicked) *repicked = 1;
        }
    }
    if (box) {
        float dx = pt[0] - box[0], dy = pt[1] - box[1], dz = pt[2] - box[2];
        if (!(sqrtf(dx * dx + dy * dy + dz * dz) < 150.0f)) return 0;   /* -> the saved pose */
    }
    r->rpt = p;                       /* ResetToRoutePoint 0x5fbed0 runs from this point */
    float o[2] = { pt_dx(p), pt_dy(p) };
    if (back) pt_backdir(n, p, o);
    pos[0] = pt_x(p); pos[1] = pt_y(p); pos[2] = pt_z(p);
    *heading = atan2f(o[1], o[0]);
    *speed = s;
    r->rdir = !back;
    r->think_left = (float)r->index * 0.1f * -0.0454545f;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    r->finished = 0;
    r->at_end = 0;
    r->drv->cmd = 1;
    fl_reset(&r->L);
    r->reset_time = r->w->time;
    r->want_reset = 0;
    return 1;
}
/* HandleReset 0x600d0b..0x600d81, a drag race (0x89e7d8 || 0x890108): the lane
   = GetNearestTrafficLane 0x40f4a0(car, car +0x60, point, section +0x70 bit 1,
   0xff, 0xff) -- the traffic lane (2..5 forward, 6..9 backward, section byte
   +0x82 + lane set) whose offset (0x401c40) is nearest the car's lateral
   (car - point) x (dy, -dx) (backward: the backward direction 0x401aa0),
   backward ones returned - 4 and turned back by MakeDrivingLineNumberBackwards
   0x403db0 (2..5 -> 6..9) -- then TrackRoutePoint::GetForwardsDrivingLineOffset
   0x5e2da0: a forward lane (0x403d60 / 0x403d80) its 0x401c40 offset, else the
   lateral, in the forward frame, of GetBackwardPosition 0x5dd430(0, offset).
   ResetToRoutePoint places the car at GetForwardPosition(0, that) (0x5fbf0a). */
static void pt_pos3(const AiNet *n, Pt *p, int back, float along, float lat, float *o);
static float drag_reset_lat(const AiNet *n, Pt *p, const float carpos[2]);
float airacer_drag_reset_offset(const AiRacer *r, const float carpos[2], float out[3])
{
    const AiNet *n = r ? r->w->net : NULL;
    Pt *p = r ? r->rpt : NULL;
    if (!n || !p) return 0.0f;
    float off = drag_reset_lat(n, p, carpos);
    pt_pos3(n, p, 0, 0.0f, off, out);             /* GetForwardPosition(0, off) */
    return off;
}
static float drag_reset_lat(const AiNet *n, Pt *p, const float carpos[2])
{
    const unsigned char *sec = sec_of(n, p);
    if (!sec) return 0.0f;
    int back = (rd32(sec + 0x70) >> 1) & 1;
    float d[2];
    if (!back) { d[0] = pt_dx(p); d[1] = pt_dy(p); }
    else pt_backdir(n, p, d);
    float c = (carpos[0] - pt_x(p)) * d[1] - (carpos[1] - pt_y(p)) * d[0];
    int first = back ? 6 : 2, best = -1;
    float bd = 8.999999e9f;
    for (int lane = first; lane < first + 4; lane++) {
        if (!sec[0x82 + lane]) continue;
        float e = fabsf(pt_laneoff(p, lane) - c);
        if (e < bd) { bd = e; best = lane; }
    }
    if (best < 0) return 0.0f;                    /* -1: offset 0 (0x600d6b) */
    /* best is already 6..9 for a backward section (- 4 then + 4) */
    if (best >= 2 && best < 6) return pt_laneoff(p, best);
    float o[3];
    pt_pos3(n, p, 1, 0.0f, pt_laneoff(p, best), o);
    return (o[0] - pt_x(p)) * pt_dy(p) - (o[1] - pt_y(p)) * pt_dx(p);
}
int airacer_route_point(const AiRacer *r, float pos[3])
{
    Pt *p = r ? r->rpt : NULL;
    if (!p) return 0;
    pos[0] = pt_x(p); pos[1] = pt_y(p); pos[2] = pt_z(p);
    return 1;
}
/* the car was put down elsewhere (0x5fbd60): the controller finds its road
   again (vt+0x30(1), 0x42cdc0) and starts over (vt+0x34, 0x40d550) */
void airacer_put_down(AiRacer *r)
{
    ctrl_lostroad(r, 1);
    r->think_left = (float)r->index * 0.1f * -0.0454545f;
    for (int i = 0; i < 16; i++) r->linkmem[i] &= ~3u;
    r->finished = 0;
    r->at_end = 0;
    r->drv->cmd = 1;
    r->reset_time = r->w->time;                                         /* 0x5f52c0 (+0x9c0) */
}
void airacer_contact(AiRacer *r, int other, int other_is_car, float other_absv, const float pt[3], float j, int wall)
{
    float now = r->w->time;
    if (r->traffic && r->tmover) r->tmover = N2_MOVER_MELLO;           /* vt+0x38 0x408ed0 (car +0x5cc set) */
    AiContact *k = NULL;
    for (int i = 0; i < r->ncontact; i++)
        if (r->contact[i].other == other && now - r->contact[i].last < 0.1f) { k = &r->contact[i]; break; }
    if (!k) {
        if (r->ncontact == 8) {
            int o = 0;
            for (int i = 1; i < 8; i++) if (r->contact[i].last < r->contact[o].last) o = i;
            k = &r->contact[o];
        } else k = &r->contact[r->ncontact++];
        k->other = other; k->car = other_is_car; k->start = now; k->j = 0;
    }
    k->wall = wall;
    k->j += j;                                                          /* 0x5a9620 */
    k->last = now;
    k->other_absv = other_absv;
    k->pt[0] = pt[0]; k->pt[1] = pt[1]; k->pt[2] = pt[2];
}
void airacer_touched_human(AiRacer *r)                                  /* 0x402450 */
{
    if (r->far) { r->far_hold = 2.0f; r->far = 0; }                    /* on mover 4: +0x730 = 2 s (0x783510) */
    r->phys_time = 5.0f;                                                /* 0x7e8d60 x 4000 */
}
void airacer_far_hold(AiRacer *r, float sec)                            /* 0x591540 */
{
    r->far_hold = sec;
}
int airacer_far_repath(AiRacer *r)
{
    int k = r->far && r->far_repath;
    r->far_repath = 0;
    return k;
}
/* 0x5918a0's driver side: 0x405120 respaces the plan's points evenly by
   length (in place, plan +0x1c4 = the length, +0x1b8 / +0x1bc = 0), then the
   points, the driver's +0x230 / +0x234 and 0x4038d0's acceleration at the
   car's speed less the drag (its mover-4 grade term is the path's, added
   by n2sim_far_path). PerfConfig +0x10 (a speed above which only the drag
   is left) is taken as unset: its writer was not found. */
int airacer_far_plan(AiRacer *r, float draft, float (*p)[3], float *len, float *vtarget, float *x234, float *acc)
{
    AiPlan *P = &r->dplan;
    int n = P->n;
    if (n < 2) return n < 0 ? 0 : n;
    if (n > PLAN_MAX) n = PLAN_MAX;
    if (2 < n) {                                                        /* 0x405120 */
        float d[PLAN_MAX], tot = 0, q[PLAN_MAX][3];
        for (int i = 0; i < n; i++) {
            if (0 < i) tot += d3(P->p[i], P->p[i - 1]);
            d[i] = tot;
        }
        for (int k = 0; k < n; k++) {
            float x = (float)k * tot * (1.0f / (float)(n - 1)), f;       /* 0x576790 over (d[i], i) */
            if (x < d[0]) f = 0;
            else if (d[n - 1] <= x) f = (float)(n - 1);
            else {
                f = 0;
                for (int i = 0; i < n - 1; i++)
                    if (d[i] <= x && x < d[i + 1]) {
                        f = fabsf(d[i + 1] - d[i]) <= 1e-6f ? (float)i + 0.5f
                          : (x - d[i]) / (d[i + 1] - d[i]) + (float)i;
                        break;
                    }
            }
            int i = (int)f;                                             /* 0x405020 */
            float u = f - (float)i;
            if (u == 0 || n - 1 <= i) memcpy(q[k], P->p[i < n ? i : n - 1], sizeof q[k]);
            else for (int c = 0; c < 3; c++) q[k][c] = P->p[i][c] * (1.0f - u) + P->p[i + 1][c] * u;
        }
        for (int k = 0; k < n; k++) memcpy(P->p[k], q[k], sizeof q[k]);
        P->len = tot;
        P->cur_time = 0; P->x1bc = 0;
    }
    for (int i = 0; i < n; i++) memcpy(p[i], P->p[i], sizeof p[i]);
    *len = P->len;
    *vtarget = r->drv->vtarget;                                         /* vt+0x74 0x40fff0 */
    *x234 = r->drv->x234;                                               /* vt+0x78 0x410000 */
    float v = r->car.speed / (0 < r->car_catchup ? r->car_catchup : 1.0f);   /* car +0x468 */
    *acc = car_accel_at(r, v) - ((1.0f - draft) * r->drag260 * v * v) / r->mass;   /* 0x4038d0 */
    return n;
}
int airacer_mover(const AiRacer *r)                                     /* 0x5f4400 */
{
    if (r->traffic) return r->tmover;
    const AiWorld *w = r->w;
    if (r->entry_done) return N2_MOVER_PHYSICS;                        /* 0x42c0c0: a finished entry (0x5ee250) -> 0x5f46a0(1) */
    if (r->far) return N2_MOVER_FAR;                                   /* 0x5f46a0(4) */
    if (0 < r->phys_time || w->drag || w->x890108 || w->drift || w->x890104 || w->x890114) return N2_MOVER_PHYSICS;
    return N2_MOVER_MELLO;
}
/* 0x42b330 -> 0x42aa20(0xb): the goal and the action end (0x402070), the
   goal Park; then the lanes it drives in (0x428490). The next think starts
   StopDriving. */
void airacer_park(AiRacer *r)
{
    if (r->act) { act_end(r, r->act); act_free(r->act); r->act = NULL; }
    r->goal = GOAL_PARK;
    r->entry_done = 1;                                                  /* race entry +0x10 */
    goal_lanes(r);
}
/* 0x5eba30(3) on a human player's finished car: its controller (type 1)
   gives way to an AI racer controller (0x4260b0) on the same car -- the
   car stays human-owned (racer info +4 == 1, skill -2) and keeps its
   player (car +0x18), whose pace ring goes on sampling */
AiRacer *airacer_take_over(AiWorld *w, AiRacer *human, const N2Sim *sim, int suv, float half_wid)
{
    int slot = human ? human->index : 0;
    if (human && slot >= 0 && slot < 24 && w->seen[slot].racer == human) {
        w->seen[slot].racer = NULL; w->seen[slot].v = &w->view[slot];
    }
    AiRacer *r = airacer_new(w, slot, -2, sim, suv, half_wid);         /* vt+0x78 0x610510: racer info +0x901 */
    if (!r) return NULL;
    w->nai--;                                                           /* not in the AI manager's think list */
    r->slot = -1;
    r->owner_kind = 1;
    r->car_player = 1;
    r->carno = 0;
    r->id = human ? human->id : 0;
    if (human) {
        r->car = human->car;
        r->pace = human->pace; r->pace_timer = human->pace_timer; r->pace_max = human->pace_max;
    } else ring_init(&r->pace, 10);
    return r;
}
int airacer_want_reset(AiRacer *r)
{
    int v = r->want_reset;
    r->want_reset = 0;
    return v;
}
void airacer_frame(AiRacer *r, float dt, N2Controls *out)
{
    AiWorld *w = r->w;
    /* drop contacts no longer touching */
    int n = 0;
    for (int i = 0; i < r->ncontact; i++) if (w->time - r->contact[i].last < 0.5f) r->contact[n++] = r->contact[i];
    r->ncontact = n;
    uint64_t t0 = SDL_GetPerformanceCounter();
    if (r->traffic) traffic_tick(r, dt);
    else {
        if (r->dctl) drag_ctl_tick(r);                                  /* 0x42df10 */
        racer_tick(r, dt);
    }
    uint64_t td = SDL_GetPerformanceCounter();
    driver_tick(r, dt);
    if (!r->traffic && !r->human) { ap_add(AP_DRIVER, td); ap_add(AP_FRAME, t0); ap_report(); }
    if (r->car_player) human_pace(r, dt);                               /* the player object's sampler (0x602650) */
    AiDriver *d = r->drv;
    out->steer = d->steer;
    out->throttle = d->gas;
    out->brake = d->brake;
    out->handbrake = d->hand;
    out->gear_req = d->gear;
    out->nitrous = r->nos_burn;
    out->clutch_kick = d->clutch_kick;
}
int airacer_action(const AiRacer *r) { return r->act ? r->act->type : -1; }
/* 0x5f6190 (a non-human car's tick): the stored horn pattern is read only
   while car +0x925 is set -- then after +0x930 s the horn sounds for
   +0x930 s when +0x928 says so. Nothing in the retail game sets +0x925,
   so the racers' horn requests (0x40d5c0) never sound; the tick keeps the
   headlights flag +0x93d on instead. */
int airacer_horn(const AiRacer *r)
{
    const int trigger = 0;                                              /* car +0x925 (never set) */
    if (r->human || !trigger) return 0;
    float t = r->w->time - r->horn_t;
    if (t < r->horn_on) return 0;
    t -= r->horn_on;
    return t < r->horn_on && r->horn_x928;
}
float airacer_target_speed(const AiRacer *r) { return r->drv->vtarget; }
float airacer_car_catchup(const AiRacer *r) { return r->car_catchup; }
float airacer_pace(const AiRacer *r) { return r->pace.avg; }
int airacer_path(const AiRacer *r, float *xyz, int max)
{
    int k = 0;
    if (!xyz) return r->dplan.n;
    for (int i = 0; i < r->dplan.n && k < max; i++, k++) {
        xyz[3 * k] = r->dplan.p[i][0]; xyz[3 * k + 1] = r->dplan.p[i][1]; xyz[3 * k + 2] = r->dplan.p[i][2];
    }
    return k;
}

/* ---- the race route's speeds (0x5ddfc0 + 0x5e8780(50), at race load) ----
   Every racing-route point gets a node (400 bytes) with, per direction of
   travel, up to three branches on the racing line; links join them across
   sections. Corner speeds come from the line's curvature over a 30 m chord
   (sqrt(9.8 g / k), g the class's cornering grip), then braking and the
   class's acceleration limit them along the line until nothing changes;
   the speeds go into the points' lane-0/1 speed bytes. In a race, side
   links (kinds 0/1/4) are then marked by whether they gain more than 20 m
   over 10 s of driving. The racing line itself (0x4679) is never re-solved
   in the retail game. */
typedef struct RNode RNode;
typedef struct RBr {
    RNode *node;               /* +0x00 */
    int    dir;                /* +0x04 */
    float  curv;               /* +0x08 */
    float  x0c, x10, x14;      /* +0x0c */
    float  centre[2];          /* +0x18 */
    struct RBr *nextc, *prevc; /* +0x20 / +0x24 the neighbours */
    struct RBr *next, *prev;   /* +0x28 / +0x2c the ones far enough away */
    float  pos[3];             /* +0x30 on the racing line */
    float  x3c;
} RBr;
struct RNode {
    Pt    *pt;                 /* +0x00 */
    short  count[2];           /* +0x04 */
    RBr    br[2][3];           /* +0x10 / +0xd0 */
};
typedef struct {
    int    n; float x0, x1, inv; const float *d;
} RCurve;
static float rcurve(const RCurve *c, float x)
{
    ACurve a = { c->n, c->x0, c->x1, c->inv, c->d };
    return acurve(&a, x);
}
static unsigned char *ptw(Pt *p) { return (unsigned char *)(uintptr_t)p; }
static int pt_speed_byte(const RBr *b) { return b->node->pt[0x2c + b->dir]; }
/* 0x5dd360 / 0x5dd490 on a route point */
static void pt_pos3(const AiNet *n, Pt *p, int back, float along, float lat, float *o)
{
    FPoint f;
    memset(&f, 0, sizeof f);
    f.back = back;
    fp_pos3(n, &f, p, o, along, lat);
}
/* 0x40cbc0: how far q lies along the travel past p */
static float pt_along(const AiNet *n, Pt *p, const float *q, int back)
{
    float d[2];
    if (!back) { d[0] = pt_dx(p); d[1] = pt_dy(p); }
    else pt_backdir(n, p, d);
    return (q[0] - pt_x(p)) * d[0] + (q[1] - pt_y(p)) * d[1];
}
static void rnode_init(RNode *nd, Pt *p, RNode *next, RNode *prev)     /* 0x5d4a20 */
{
    memset(nd, 0, sizeof *nd);
    nd->pt = p;
    nd->count[0] = 1;
    nd->br[0][0].node = nd; nd->br[0][0].dir = 0;
    nd->br[0][0].nextc = nd->br[0][0].next = next ? &next->br[0][0] : NULL;
    nd->br[0][0].prevc = nd->br[0][0].prev = prev ? &prev->br[0][0] : NULL;
    nd->count[1] = 1;
    nd->br[1][0].node = nd; nd->br[1][0].dir = 1;
    nd->br[1][0].nextc = nd->br[1][0].next = prev ? &prev->br[1][0] : NULL;
    nd->br[1][0].prevc = nd->br[1][0].prev = next ? &next->br[1][0] : NULL;
}
/* 0x5d4ce0: a new branch, a copy of the one before it */
static RBr *rnode_addbr(RNode *nd, int dir)
{
    int c = nd->count[dir];
    if (c >= 3) c = 2;                  /* the original has no bound (3 slots per direction) */
    RBr *b = &nd->br[dir][c];
    *b = nd->br[dir][c - 1];
    nd->count[dir] = (short)(c + 1);
    return b;
}
static RBr *rbr_pick(const AiNet *n, RBr *b, float sq, int fwd)        /* 0x5e5920 / 0x5e59e0 */
{
    RBr *q = fwd ? b->nextc : b->prevc;
    for (int k = 0; ; k++) {
        if (!q || !(fwd ? q->nextc : q->prevc) || 9 < k) return q;
        float a = pt_along(n, b->node->pt, q->pos, b->dir);
        if (fwd ? (0 < a) : (a < 0)) {
            if (sq <= a * a) return q;
            float dx = q->pos[0] - b->pos[0], dy = q->pos[1] - b->pos[1], dz = q->pos[2] - b->pos[2];
            if (sq <= dy * dy + dz * dz + dx * dx) return q;
        }
        q = fwd ? q->nextc : q->prevc;
    }
}
static int rbr_brake(RBr *b, RBr *o, float decel)                       /* 0x5d4b80 */
{
    float v = (float)pt_speed_byte(b), vo = (float)pt_speed_byte(o), d = d3(b->pos, o->pos);
    float lim = sqrtf(vo * vo + d * decel + d * decel);
    if (v > lim) { ptw(b->node->pt)[0x2c + b->dir] = (unsigned char)((int)(lim * 65536.0f) >> 16); return 1; }
    return 0;
}
static int rbr_accel(RBr *b, RBr *o, const RCurve *cv)                  /* 0x5d9070 */
{
    float v = (float)pt_speed_byte(b), vo = (float)pt_speed_byte(o), d = d3(b->pos, o->pos);
    float a = rcurve(cv, vo);
    float lim = sqrtf(a * d + a * d + vo * vo);
    if (v > lim) { ptw(b->node->pt)[0x2c + b->dir] = (unsigned char)((int)(lim * 65536.0f) >> 16); return 1; }
    return 0;
}
static int rnode_brake(RNode *nd, int dir, float decel, int chain)      /* 0x5d8fc0 */
{
    int ch = 0;
    for (int k = 0; k < nd->count[dir]; k++) {
        RBr *b = &nd->br[dir][k], *nx = b->next;
        if (!nx) continue;
        ch |= rbr_brake(b, nx, decel);
        if (chain) for (RBr *q = b->nextc; q && q != nx; q = q->nextc) ch |= rbr_brake(q, nx, decel);
    }
    return ch;
}
static int rnode_accel(RNode *nd, int dir, const RCurve *cv, int chain) /* 0x5d9140 */
{
    int ch = 0;
    for (int k = 0; k < nd->count[dir]; k++) {
        RBr *b = &nd->br[dir][k], *pv = b->prev;
        if (!pv) continue;
        ch |= rbr_accel(b, pv, cv);
        if (chain) for (RBr *q = b->prevc; q && q != pv; q = q->prevc) ch |= rbr_accel(q, pv, cv);
    }
    return ch;
}
static int sec_usable(const AiNet *n, int s)                            /* +0x70 bits 9, 10 clear */
{
    if (s < 0 || s >= SEC_MAX || !n->sec[s]) return 0;
    uint32_t f = rd32(n->sec[s] + 0x70);
    return !(f >> 9 & 1) && !(f >> 10 & 1);
}
/* 0x5ddde0: the route distance reached after `limit` seconds at the
   line's speeds from a node (jumping across junction links, +3 s) */
static float rnode_reach(const AiNet *n, RNode *nd, int dir, float limit, const int *base, RNode *nodes)
{
    RBr *b = &nd->br[dir][0], *q = b->next;
    float t = 0, vp = (float)pt_speed_byte(b);
    const float *pos = b->pos;
    if (!q) return rdf(nd->pt + 8);
    while (t < limit) {
        float v = (float)pt_speed_byte(q);
        t = d3(pos, q->pos) / ((v + vp) * 0.5f) + t;
        Pt *p = q->node->pt;
        const float *qp = q->pos;
        RBr *nx = NULL;
        int jumped = 0;
        for (int k = 0; k < pt_nlinks(n, p); k++) {
            Lk *l = pt_link(n, p, k);
            uint32_t f = lk_flags(l);
            if ((f & 0xf) == 2 && (f >> 26 & 1) && (int)(f >> 18 & 1) == (q->dir != 0)) {
                int s = (f & 0xf) == 3 ? rds16(l + 10) : rds16(l + 8);
                int pi = (f & 0xf) == 3 ? rds16(l + 6) : rds16(l + 4);
                t = 3.0f + t;                                           /* 0x7a4798 */
                nx = &nodes[base[s] + pi].br[(f >> 23 ^ f >> 18) & 1][0];
                jumped = 1;
                break;
            }
        }
        if (!jumped) nx = q->next;
        pos = qp; vp = v; q = nx;
        if (!q) return rdf(nd->pt + 8);
    }
    return rdf(q->node->pt + 8);
}
/* tuning sets by the race's route type (0x7a4660, 8 floats each) */
static const float RSET[7][8] = {
    { 2.5f, 0.5f, 2.0f, 0.5f, 30.0f, 1.0f, 1.0f, 1.0f },
    { 2.5f, 1.0f, 2.0f, 0.5f, 10.0f, 1.0f, 1.0f, 0.699999988f },
    { 2.5f, 0.899999976f, 2.0f, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f },
    { 2.5f, 0.899999976f, 2.0f, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f },
    { 2.5f, 1.0f, 1.79999995f, 0.800000012f, 15.0f, 1.0f, 1.0f, 0.949999988f },
    { 1.60000002f, 1.0f, 2.0f, 1.0f, 50.0f, 1.0f, 1.0f, 1.25f },
    { 1.60000002f, 1.0f, 2.0f, 1.0f, 50.0f, 1.0f, 1.0f, 1.0f },
};
/* 0x5e9e80: the route type from the race record's flags (+0x94) */
int ainet_route_type(unsigned flags, int event)
{
    if (flags & 1) return 2;
    if (flags & 4) return 2;
    if (flags & 2) return 0;
    if (flags & 8) return 1;
    if (flags & 0x800) return 4;
    if (flags & 0x1000) return 4;
    if (flags & 0x2000) {
        int e = event < 0x125e ? 0x125d : (0x1261 < event ? 0x1261 : event);
        return (event != e) + 5;
    }
    return 3;
}
void ainet_prepare(AiNet *n, const AiRoutePrep *rp)
{
    /* 0x5ddfc0 */
    int klass = rp->klass < 0 ? 3 : rp->klass > 3 ? 3 : rp->klass;
    RCurve cv[4];
    for (int k = 0; k < 4; k++) {                                       /* 0x5a78e0 */
        const unsigned char *r = rp->class_curves + k * 0x10c;
        cv[k].n = 0x40; cv[k].x0 = 0; cv[k].x1 = rdf(r); cv[k].inv = 63.0f / cv[k].x1; cv[k].d = (const float *)(r + 0xc);
    }
    for (int s = 0; s < 0x5dc && s < SEC_MAX; s++) {
        const unsigned char *sc = n->sec[s];
        if (!sc || !(rd32(sc + 0x70) >> 6 & 1) || (rd32(sc + 0x70) >> 9 & 1)) continue;
        for (int dir = 0; dir < 2; dir++) {
            if (sc[0x74 + dir * 8]) continue;
            unsigned char v = (unsigned char)((int)(cv[klass].x1 * 65536.0f) >> 16);   /* 0x5d9340 */
            for (int i = 0; i < sec_npts(sc); i++) ptw(sec_pt(sc, i))[0x2c + dir] = v;
            unsigned char *w = (unsigned char *)(uintptr_t)sc;
            w[0x74 + dir * 8] = 0; w[0x75 + dir * 8] = 1;
            float k999 = 999.0f; memcpy(w + 0x78 + dir * 8, &k999, 4);
        }
    }
    /* 0x5e8780: the nodes */
    int total = 0, *base = (int *)calloc(0x5dc, sizeof *base);
    for (int s = 0; s < 0x5dc && s < SEC_MAX; s++) {
        const unsigned char *sc = n->sec[s];
        if (sc && (rd32(sc + 0x70) >> 6 & 1) && !(rd32(sc + 0x70) >> 9 & 1)) total += sec_npts(sc);
    }
    RNode *nodes = (RNode *)calloc((size_t)(total ? total : 1), sizeof *nodes);
    if (!nodes || !base) { free(nodes); free(base); return; }
    for (int s = 0, at = 0; s < 0x5dc && s < SEC_MAX; s++) {
        const unsigned char *sc = n->sec[s];
        if (!sc || !(rd32(sc + 0x70) >> 6 & 1) || (rd32(sc + 0x70) >> 9 & 1)) continue;
        int np = sec_npts(sc);
        for (int i = 0; i < np; i++)
            rnode_init(&nodes[at + i], sec_pt(sc, i), i == np - 1 ? NULL : &nodes[at + i + 1], i == 0 ? NULL : &nodes[at + i - 1]);
        base[s] = at;
        at += np;
    }
    for (int li = 0; li < n->nlinks; li++) {
        Lk *l = n->links + li * 0x20;
        uint32_t f = lk_flags(l);
        unsigned kind = f & 0xf;
        if (kind == 7 || kind == 8 || (f >> 26 & 1)) continue;
        int sa = rds16(l + 10), sb = kind == 3 ? sa : rds16(l + 8);
        if (!sec_usable(n, sa) || !sec_usable(n, sb) || sa == sb) continue;
        int db = (int)((f >> 23 ^ (f >> 18 & 0xff)) & 1), da = (int)(f >> 18 & 1);
        Pt *pa = sec_pt(n->sec[sa], rds16(l + 6)), *pb = lk_pt(n, l);
        if (!pb) continue;
        if (!(pt_flags(pa) >> (da ? 11 : 18) & 1)) continue;
        if (!(pt_flags(pb) >> (db ? 11 : 18) & 1)) continue;
        RNode *A = &nodes[base[sa] + rds16(l + 6)], *B = &nodes[base[sb] + pt_index(n, pb)];
        /* 0x5d8760: the link back the other way, if Always: B no longer
           continues from its old neighbour */
        Lk *back = NULL;
        {
            Pt *pp = sec_pt(n->sec[sa], rds16(l + 6));
            if (pb == pp) back = l;
            else for (int k = 0; k < pt_nlinks(n, pb); k++) {
                Lk *m = pt_link(n, pb, k);
                uint32_t mf = lk_flags(m);
                if ((mf >> 18 & 1) == ((f >> 23 ^ f >> 18) & 1)) continue;
                int ms = (mf & 0xf) == 3 ? rds16(m + 10) : rds16(m + 8), mp = (mf & 0xf) == 3 ? rds16(m + 6) : rds16(m + 4);
                if (ms >= 0 && ms < SEC_MAX && n->sec[ms] && sec_pt(n->sec[ms], mp) == pp) { back = m; break; }
            }
        }
        if (back && (lk_flags(back) & 0xf) == 2) {
            RBr *pv = B->br[db][0].prevc;
            if (pv) {
                B->br[db][0].prevc = NULL; B->br[db][0].prev = NULL;
                RNode *o = pv->node;
                for (int k = 0; k < o->count[db]; k++) { o->br[db][k].nextc = NULL; o->br[db][k].next = NULL; }
            }
        }
        if (kind == 2) {
            RBr *nx = A->br[da][0].nextc;
            if (nx) {
                RNode *o = nx->node;
                for (int k = 0; k < o->count[da]; k++) { o->br[da][k].prevc = NULL; o->br[da][k].prev = NULL; }
            }
            if (!B->br[db][0].prevc) {
                A->br[da][0].nextc = &B->br[db][0];
                B->br[db][0].prevc = &A->br[da][0];
            } else {
                RBr *nb = rnode_addbr(B, db);
                A->br[da][0].nextc = nb;
                nb->prevc = &A->br[da][0];
            }
        } else {
            if (!B->br[db][0].prevc) {
                RBr *na = rnode_addbr(A, da);
                na->nextc = &B->br[db][0];
                B->br[db][0].prevc = na;
            } else {
                RBr *na = rnode_addbr(A, da), *nb = rnode_addbr(B, db);
                na->nextc = nb;
                nb->prevc = na;
            }
        }
    }
    /* 0x5d4c30 */
    const float *set = RSET[rp->route_type < 0 ? 3 : rp->route_type > 6 ? 6 : rp->route_type];
    static const float BR[4] = { 12.0f, 10.6000004f, 9.30000019f, 8.0f }, CG[4] = { 3.70000005f, 3.4000001f, 3.0999999f, 2.79999995f };
    static const float BRS[4] = { 10.0f, 9.0f, 8.0f, 6.0f }, CGS[4] = { 3.0f, 3.0f, 3.0f, 2.0f };
    float decel = set[6] * (rp->suv ? BRS : BR)[klass];
    float grip = set[7] * (rp->suv ? CGS : CG)[klass];
    /* the line, its curvature over 30 m and the corner speeds */
    for (int i = 0; i < total; i++)                                     /* 0x5e3ea0 */
        for (int d = 0; d < 2; d++)
            for (int k = 0; k < nodes[i].count[d]; k++) {
                RBr *b = &nodes[i].br[d][k];
                Pt *p = nodes[i].pt;
                float w = pt_wr(p) < pt_wl(p) ? pt_wl(p) : pt_wr(p);
                pt_pos3(n, p, b->dir != 0, 0, (float)(signed char)p[0x22 + b->dir] * (1.0f / 128.0f) * w, b->pos);
            }
    for (int i = 0; i < total; i++)                                     /* 0x5e5aa0 with 900 */
        for (int d = 0; d < 2; d++)
            for (int k = 0; k < nodes[i].count[d]; k++) {
                RBr *b = &nodes[i].br[d][k];
                b->next = rbr_pick(n, b, 900.0f, 1);                    /* 0x7a4788 */
                b->prev = rbr_pick(n, b, 900.0f, 0);
            }
    for (int i = 0; i < total; i++) {
        for (int d = 0; d < 2; d++)                                     /* 0x5d4ad0 */
            for (int k = 0; k < nodes[i].count[d]; k++) {
                RBr *b = &nodes[i].br[d][k];
                if (b->nextc && b->prevc) {
                    float c[2] = { 0, 0 };
                    b->curv = b->prev && b->next ? curvature3c(b->prev->pos, b->pos, b->next->pos, c) : 0;
                    b->centre[0] = c[0]; b->centre[1] = c[1];
                    b->x0c = b->x10 = b->x14 = 0;
                }
            }
        float vtop = cv[klass].x1;                                      /* 0x5d8f00 */
        for (int d = 0; d < 2; d++) {
            float v = 0;
            if (0 < nodes[i].count[d]) {
                float num = grip * 9.80000019f, kmin = num / (vtop * vtop);   /* 0x7a51b8 */
                for (int k = 0; k < nodes[i].count[d]; k++) {
                    float c = fabsf(nodes[i].br[d][k].curv);
                    if (!(kmin <= c)) c = kmin;
                    float s = sqrtf(num / c);
                    if (k != 0 && v < s) s = v;
                    v = s;
                }
            }
            ptw(nodes[i].pt)[0x2c + d] = (unsigned char)((int)(v * 65536.0f) >> 16);
        }
    }
    for (int i = 0; i < total; i++)                                     /* 0x5e5aa0 with 1 */
        for (int d = 0; d < 2; d++)
            for (int k = 0; k < nodes[i].count[d]; k++) {
                RBr *b = &nodes[i].br[d][k];
                b->next = rbr_pick(n, b, 1.0f, 1);                      /* 0x7a478c */
                b->prev = rbr_pick(n, b, 1.0f, 0);
            }
    /* braking and acceleration along the line, at most 10 sweeps */
    int changed = 1;
    for (int it = 0; changed && it <= 9; it++) {
        changed = 0;
        for (int li = 0; li < n->nlinks; li++) {
            Lk *l = n->links + li * 0x20;
            uint32_t f = lk_flags(l);
            unsigned kind = f & 0xf;
            if (kind == 7 || kind == 8 || (f >> 26 & 1)) continue;
            int sa = rds16(l + 10), sb = kind == 3 ? sa : rds16(l + 8);
            if (!sec_usable(n, sa) || !sec_usable(n, sb) || sa == sb) continue;
            int pa = rds16(l + 6), pbi = kind == 3 ? pa : rds16(l + 4);
            changed |= rnode_brake(&nodes[base[sa] + pa], (int)(f >> 18 & 1), decel, 1);
            changed |= rnode_accel(&nodes[base[sb] + pbi], (int)((f >> 23 ^ (f >> 18 & 0xff)) & 1), &cv[klass], 1);
        }
        for (int i = 0; i < total; i++) {
            changed |= rnode_brake(&nodes[i], 1, decel, 0);
            changed |= rnode_accel(&nodes[i], 0, &cv[klass], 0);
        }
        for (int i = total - 1; i >= 0; i--) {
            changed |= rnode_brake(&nodes[i], 0, decel, 0);
            changed |= rnode_accel(&nodes[i], 1, &cv[klass], 0);
        }
    }
    /* the side links of a race route, by their gain over 10 s */
    if (rp->race_route && !(rp->race_flags & 2)) {
        for (int li = 0; li < n->nlinks; li++) {
            unsigned char *l = (unsigned char *)(uintptr_t)(n->links + li * 0x20);
            uint32_t f = rd32(l + 0xc);
            unsigned kind = f & 0xf;
            if (kind != 4 && kind != 0 && kind != 1) continue;
            int sa = rds16(l + 10), sb = kind == 3 ? sa : rds16(l + 8);
            if (!sec_usable(n, sa) || !sec_usable(n, sb)) continue;
            int pa = rds16(l + 6), pbi = kind == 3 ? pa : rds16(l + 4);
            int da = (int)(f >> 18 & 1), db = (int)((f >> 23 ^ f >> 18) & 1);
            if (!(pt_flags(sec_pt(n->sec[sa], pa)) >> (da ? 11 : 18) & 1)) continue;
            if (!(pt_flags(sec_pt(n->sec[sb], pbi)) >> (db ? 11 : 18) & 1)) continue;
            if (sa == sb) continue;
            float ra = rnode_reach(n, &nodes[base[sa] + pa], da, 10.0f, base, nodes);   /* 0x7a4790 */
            float rb = rnode_reach(n, &nodes[base[sb] + pbi], db, 10.0f, base, nodes);
            float g = rb - ra;
            if (rp->circuit) {
                if (g < -rp->lap_half) g += rp->lap_len;
                else if (rp->lap_half < g) g -= rp->lap_len;
            }
            if (g < -20.0f) f = (f & 0xfffffff1u) | 1;                  /* 0x7a4794 */
            else if (20.0f < g) f &= 0xfffffff0u;
            else continue;
            l[0xc] = (unsigned char)f; l[0xd] = (unsigned char)(f >> 8); l[0xe] = (unsigned char)(f >> 16); l[0xf] = (unsigned char)(f >> 24);
        }
    }
    free(nodes);
    free(base);
}

/* ---- the race's reference times (0x5f6e70, racemgr +0xc34) --------------- */
/* 0x5dd550(dir, 0, net, 0, 1): the next race route point -- a link of
   kind 2 (the race route) or 0 leaving in the travel direction (link bit
   18 != dir; bit 23 turns the direction round), else the next point of the
   section that way; points off the race route (flag 0x800) end it */
static Pt *pt_next_race(const AiNet *n, Pt *p, int *dir)
{
    if (!(rd32(p + 0xc) >> 11 & 1)) return NULL;
    Pt *t = NULL;
    for (int k = 0; k < pt_nlinks(n, p); k++) {
        Lk *l = pt_link(n, p, k);
        uint32_t f = lk_flags(l);
        if ((int)(f >> 18 & 1) == *dir) continue;
        if ((f & 0xf) != 2 && (f & 0xf) != 0) continue;
        Pt *q = lk_pt(n, l);
        if (f >> 23 & 1) *dir = !*dir;
        if (q != p) { t = q; break; }
        break;                                                           /* back onto itself: along the section */
    }
    if (!t) {
        const unsigned char *s = sec_of(n, p);
        if (!s) return NULL;
        int i = pt_index(n, p);
        if (*dir == 0) { if (i == 0) return NULL; t = sec_pt(s, i - 1); }
        else { if (rds16(s + 0x34) <= i + 1) return NULL; t = sec_pt(s, i + 1); }
    }
    if (!t || !(rd32(t + 0xc) >> 11 & 1)) return NULL;
    return t;
}
/* 0x5f6e70: from the route point nearest the start line (within 40 m) along
   the race route, at each point a 1 m step along its racing line (its
   offset byte +0x22 x the wider of its widths): the race fraction it covers
   over the time it takes at the point's racing-line speed (+0x2c) -- kept
   when both ends lie on one route section and the progress moved 1/4..4
   times the step -- into the first empty of 256 bins of the race; it stops
   past 3/4 of the route within 200 m of the finish line. Empty bins take
   the bin before (the first 40 m/s), then out[1 + 2i] is the rate, out[2 + 2i]
   the time to reach bin i and out[0] the whole race's */
void ainet_reftime(const AiNet *n, const float start[3], float route_len, const AiRefLoc *L, float out[513])
{
    Pt *bin_pt[256];
    memset(out, 0, 513 * sizeof *out);
    memset(bin_pt, 0, sizeof bin_pt);
    Pt *p = NULL;
    float best = 40.0f;                                                 /* 0x5e4080(start, 40, 4) */
    for (int s = 0; s < SEC_MAX; s++) {
        Pt *q = n->sec[s] ? sec_nearest(NULL, n->sec[s], start, best, 4, 0) : NULL;
        if (q) { float pz[3] = { pt_x(q), pt_y(q), pt_z(q) }; best = d3(pz, start); p = q; }
    }
    if (p) {
        L->reset(L->user);                                              /* 0x5da2f0 */
        float acc = 0, last = 0;
        for (int cnt = 0; ; cnt++) {
            const unsigned char *s = sec_of(n, p);
            int back = s ? (int)(rd32(s + 0x70) >> 1 & 1) : 0;
            int dir = back == 0;
            float pp[3] = { pt_x(p), pt_y(p), pt_z(p) }, progB = 0;
            int secA = 0, secB = 0;
            L->locate(L->user, pp, &last, NULL);                        /* 0x5da330(point) */
            if (s && (s[0x74 + back * 8] || s[0x75 + back * 8])) {
                float w = (float)rdu16(p + 0x16) / 256.0f, w2 = (float)rdu16(p + 0x14) / 256.0f;
                if (w < w2) w = w2;
                float off = (float)(signed char)p[0x22 + back] / 128.0f * w;
                float d[2], A[3], B[3];
                if (!back) { d[0] = pt_dx(p); d[1] = pt_dy(p); }
                else pt_backdir(n, p, d);                               /* 0x5dd2f0 / 0x5dd430 */
                A[0] = pt_x(p) + d[1] * off; A[1] = pt_y(p) - d[0] * off; A[2] = pt_z(p);
                B[0] = A[0] + d[0]; B[1] = A[1] + d[1]; B[2] = A[2];
                float progA;
                float f0 = L->locate(L->user, A, &progA, &secA);
                float f1 = L->locate(L->user, B, &progB, &secB);
                last = progB;
                if (0.5f < f1 - f0) f0 += 1.0f;
                else if (0.5f < f0 - f1) f1 += 1.0f;
                float seg = sqrtf((A[0] - B[0]) * (A[0] - B[0]) + (A[1] - B[1]) * (A[1] - B[1]));
                float dprog = L->diff(L->user, progB, progA);           /* 0x5d5cd0 */
                float v = (float)p[0x2c + back];                        /* the racing line's speed byte */
                float rate = 0;
                if (0 < v) rate = (f1 - f0) / (seg / v);
                float ratio = fabsf(dprog) / seg;
                if (secA != secB || ratio < 0.25f || 4.0f < ratio) rate = 0;
                if (!(0 < f0)) f0 = 0;
                int bin = (int)((f0 - (float)(int)f0) * 256.0f);
                if (bin > 255) bin = 255;
                if (0 < rate && out[1 + bin * 2] == 0) { out[1 + bin * 2] = rate; bin_pt[bin] = p; }
            }
            Pt *q = pt_next_race(n, p, &dir);
            if (!q) break;
            acc += sqrtf((pt_x(p) - pt_x(q)) * (pt_x(p) - pt_x(q)) + (pt_y(p) - pt_y(q)) * (pt_y(p) - pt_y(q)));
            if (route_len * 0.75f < acc) {                              /* 0x785510 */
                float dd = L->diff(L->user, last, L->finish);   /* the locator's last progress */
                if (0 <= dd && dd < 200.0f) break;                      /* 0x78aaac */
            }
            if (cnt >= 0x2711) break;
            p = q;
        }
    }
    float t = 0, prev = 40.0f / route_len;                              /* 0x7864d8 */
    for (int i = 0; i < 256; i++) {
        out[2 + i * 2] = t;
        if (out[1 + i * 2] == 0) out[1 + i * 2] = prev;
        t += 0.00390625f / out[1 + i * 2];
        prev = out[1 + i * 2];
    }
    out[0] = t;
}
/* 0x5edd90: the reference time at a race distance (laps, the fraction
   in the lap): whole laps x the race's, plus the bin's start and the rest
   at its rate */
float ainet_reftime_at(const float tab[513], float d)
{
    if (!(0 < d)) d = 0;
    int nl = (int)d;
    float f = d - (float)nl;
    int bin = (int)(f * 256.0f);
    if (bin > 255) bin = 255;
    return (float)nl * tab[0] + (f - (float)bin * 0.00390625f) / tab[1 + bin * 2] + tab[2 + bin * 2];
}

/* the race traffic (TrafficController, intersections, teleporter) */
#include "aitraffic.c"
/* the drag race (goals 2 / 3, DragController) */
#include "aidrag.c"
/* free roam's random encounter racers and the Outrun */
#include "aiencounter.c"

/* ---- the GPS: PathfindingManager (0x81cbc0) + RealController's GPS path ----
   E:/re-tools/out/career/c7/GPS.md section 2. The graph is this road net;
   postedness 2 (a human player) throughout. */
#define PF_OPEN_MAX   0x9f      /* PFM +0x44 */
#define PF_CLOSED_MAX 0x4ff     /* PFM +0x2c4 */
#define PF_DEAD 99999.0f        /* 0x47c34f80 */
typedef struct { Pt *pt; char dir; float g; } PfLite;   /* PathfindingNodeLite (pool 0x81cbbc) */
struct AiGps {
    const AiNet *net;
    /* ThePathfindingManager 0x81cbc0 */
    int    state;              /* +0: 0 idle, 1 open list, 2 A*, 3 finished */
    int    status, last;       /* +4 / +8 */
    float  start[3], sdir[2], target[3];   /* +0x10 / +0x20 / +0x30 */
    int    cursor;             /* +0x40 section of the open-list scan */
    PfNode *open[PF_OPEN_MAX]; int nopen;  /* +0x44 / +0x16c4: sorted, the cheapest last */
    PfLite closed[PF_CLOSED_MAX]; int nclosed;   /* +0x2c4 / +0x16c8 */
    int    nopen0;             /* +0x16cc */
    int    algorithm, posted;  /* +0x16d0 / +0x16d4 */
    PfNode result;             /* +0x16e0 */
    char   found;              /* +0x17a0 */
    char   car;                /* +0x17a4: the request's car (the player's) */
    PfNode pool[PF_OPEN_MAX + 2]; PfNode *pfree[PF_OPEN_MAX + 2]; int npfree;   /* pool 0x81cbb8 */
    /* the player's RealController */
    int    gstate;             /* +0x3e4: 0 none, 1 following, 3 no path */
    Pt    *rp;                 /* +0x3e8 the GPS route point */
    char   fwd;                /* +0x3ec */
    PfNode node;               /* +0x3f0 (+0x400 its target) */
    AiWorld *w; AiRacer *r;    /* r->L: the GPS FuturePoints +0x4b0 (+0xac set) */
    float  halfwid;            /* 0x5ec250: half the car-space box's width */
};

static PfNode *pf_alloc(AiGps *g)
{
    if (g->npfree < 1) return NULL;
    PfNode *N = g->pfree[--g->npfree];
    N->start = N->cur = NULL;
    N->nsucc = N->ndec = 0; N->atgoal = N->startdir = N->dir = 0;
    return N;
}
static void pf_free(AiGps *g, PfNode *N) { if (N) g->pfree[g->npfree++] = N; }

/* 0x5d8bb0: the point is posted for this postedness (flags +0xc bit 11 / 25 / 27) */
static int pt_posted(Pt *p, int P)
{
    uint32_t f = pt_flags(p);
    return P == 0 ? (int)(f >> 11 & 1) : P == 1 ? (int)(f >> 25 & 1) : P == 2 ? (int)(f >> 27 & 1) : 0;
}
/* sign class by postedness (0, 2 nibble 0; 1 traffic nibble 1): kinds 0
   Faster, 1 Slower, 2 Always, 3 Stop, 4 Equal, 5 Don't take, 6 Misc, 7 None,
   8 Player Always, 9 Avoid Object, 10 Never (names 0x800cac) */
static int lk_class(Lk *l, int P) { return P == 1 ? (int)(lk_flags(l) >> 4 & 0xf) : (int)(lk_flags(l) & 0xf); }
static int lk_always(Lk *l, int P)                                      /* 0x5dcbe0 IsAlways */
{
    int k = lk_class(l, P);
    return P == 2 ? k == 2 || k == 8 : (P == 0 || P == 1) && k == 2;
}
static int lk_ignore(Lk *l, int P)                                      /* 0x5dcb90 Ignore */
{
    int k = lk_class(l, P);
    return P == 0 ? k == 7 || k == 8 : (P == 1 || P == 2) && k == 7;
}
static int lk_never(Lk *l, int P)                                       /* 0x5dcc30 IsNeverTake */
{
    int k = lk_class(l, P);
    return (P >= 0 && P <= 2) && (k == 5 || k == 10);
}
/* 0x5d8a40 -> 0x401cc0: the next point of the section that way, NULL at its end */
static Pt *pt_next(const AiNet *n, Pt *p, int fwd)
{
    const unsigned char *s = sec_of(n, p);
    int i = pt_index(n, p);
    if (!s) return NULL;
    if (fwd) return i + 1 < sec_npts(s) ? sec_pt(s, i + 1) : NULL;
    return i ? sec_pt(s, i - 1) : NULL;
}
/* 0x40cbc0: how far q lies along the travel at p (forward, or back over the segment before it) */
static float pt_travel(const AiNet *n, Pt *p, Pt *q, int back)
{
    float d[2];
    if (!back) { d[0] = pt_dx(p); d[1] = pt_dy(p); } else pt_backdir(n, p, d);
    return (pt_y(q) - pt_y(p)) * d[1] + (pt_x(q) - pt_x(p)) * d[0];
}
/* 0x40cd00: how far q lies outside the road's half widths at p */
static float pt_latout(Pt *p, const float *q)
{
    float lat = (q[0] - pt_x(p)) * pt_dy(p) - (q[1] - pt_y(p)) * pt_dx(p);
    return 0 < lat ? lat - pt_wr(p) : -(pt_wl(p) + lat);
}
/* 0x40ce90 -> 0x40ce10: how far q lies outside the segments either side of p */
static float pt_alongout(const AiNet *n, Pt *p, const float *q)
{
    float al = (q[0] - pt_x(p)) * pt_dx(p) + (q[1] - pt_y(p)) * pt_dy(p);
    return 0 < al ? al - pt_seglen(p) : -(pt_prevseglen(n, p) + al);
}
/* 0x413250 CloseEnoughToGoal: the target inside the point's road, |dz| < 4 m */
static int pf_close_enough(const AiNet *n, const PfNode *N, Pt *p)
{
    return pt_latout(p, N->target) < 0 && pt_alongout(n, p, N->target) < 0 &&   /* 0x783d40 */
           fabsf(pt_z(p) - N->target[2]) < 4.0f;                                /* 0x783d44 */
}
/* 0x40c580 AddSuccessor: a link's decision is recorded unless it is always taken */
static void pf_addsucc(PfNode *N, const AiNet *n, Pt *p, int dir, Lk *l, int P)
{
    if (N->nsucc >= 3) return;
    N->succ[(int)N->nsucc].pt = p;
    N->succ[(int)N->nsucc].dir = (unsigned char)dir;
    N->succ[(int)N->nsucc].dec = (l && !lk_always(l, P)) ? (unsigned short)lk_index(n, l) : 0xffff;
    N->nsucc++;
}
/* 0x41ae40 FigureOutSuccessors: each link it may take (an always link alone)
   and the road straight on */
static void pf_figure(PfNode *N, const AiNet *n, int P)
{
    N->nsucc = 0;
    if (!N->cur || N->atgoal) return;
    int nl = pt_nlinks(n, N->cur);
    for (int k = 0; k < nl; k++) {
        Lk *l = pt_link(n, N->cur, k);
        uint32_t f = lk_flags(l);
        if ((int)(f >> 18 & 1) == (N->dir != 0)) {
            if ((f & 0xf) == 3) return;
            continue;
        }
        int nd = (int)(f >> 23 & 1) != N->dir;
        if (lk_always(l, P)) { pf_addsucc(N, n, lk_pt(n, l), nd, l, P); return; }
        if (!lk_ignore(l, P) && !lk_never(l, P)) pf_addsucc(N, n, lk_pt(n, l), nd, l, P);
    }
    Pt *q = pt_next(n, N->cur, N->dir != 0);
    if (q && N->nsucc < 3) {
        N->succ[(int)N->nsucc].pt = q;
        N->succ[(int)N->nsucc].dir = N->dir != 0;
        N->succ[(int)N->nsucc].dec = 0xffff;
        N->nsucc++;
    }
}
static void pf_estimate(PfNode *N)                                      /* (0x3ffdc on GC: EstimateCostToGoal) */
{
    if (!N->cur) N->h = PF_DEAD;
    else if (N->atgoal) N->h = 0;
    else N->h = v2len(pt_x(N->cur) - N->target[0], pt_y(N->cur) - N->target[1]);
}
/* 0x4201d0 PathfindingNode::Init */
static void pf_init(PfNode *N, const AiNet *n, Pt *p, int dir, const float *target, int P)
{
    N->start = N->cur = p;
    N->startdir = N->dir = (char)dir;
    N->g = 0;
    N->ndec = 0; N->atgoal = 0;
    memcpy(N->target, target, sizeof N->target);
    pf_estimate(N);
    pf_figure(N, n, P);
}
/* 0x420270 Traverse: along the road (always links taken) until the goal, a
   dead end (unposted point, a Stop link, 10 km) or a junction with a choice */
static void pf_traverse(PfNode *N, const AiNet *n, int P)
{
    float g0 = N->g;
    int stop = 0;
    for (long guard = 0; N->cur && !stop && guard < 1000000; guard++) {
        Pt *p = N->cur;
        if (N->g - g0 > 10000.0f) { N->cur = NULL; break; }             /* 0x784500 */
        if (pf_close_enough(n, N, p)) { N->atgoal = 1; break; }
        if (!pt_posted(p, P)) { N->cur = NULL; break; }
        int nl = pt_nlinks(n, p);
        Lk *must = NULL;
        for (int k = 0; k < nl && !must && !stop; k++) {
            Lk *l = pt_link(n, p, k);
            uint32_t f = lk_flags(l);
            if ((int)(f >> 18 & 1) == (N->dir != 0)) continue;
            if ((f & 0xf) == 3) { N->cur = NULL; break; }
            if (lk_always(l, P)) must = l;
            else if (!lk_ignore(l, P) && !lk_never(l, P)) stop = 1;
        }
        if (stop || !N->cur) continue;
        if (must) {
            Pt *q = lk_pt(n, must);
            N->g = pt_travel(n, p, q, N->dir == 0) + N->g;
            N->cur = q;
            N->dir ^= (char)(lk_flags(must) >> 23 & 1);
        } else {
            N->g = (N->dir ? pt_seglen(p) : pt_prevseglen(n, p)) + N->g;   /* 0x401a30 */
            N->cur = pt_next(n, p, N->dir != 0);
        }
    }
    pf_estimate(N);
    pf_figure(N, n, P);
}
/* 0x41ad40 GetSuccessor: the node moved to successor k, the decisions of
   the junction recorded (the links before k not taken, k taken) */
static int pf_successor(const PfNode *N, const AiNet *n, int k, PfNode *o)
{
    if (k >= N->nsucc) return 0;
    *o = *N;                                                            /* 0x40a2d0 */
    o->cur = N->succ[k].pt;
    o->dir = (char)N->succ[k].dir;
    o->g = pt_travel(n, N->cur, o->cur, N->dir == 0) + o->g;
    for (int i = 0; i < k; i++)
        if (o->ndec < 0x3f) { o->dec[(int)o->ndec] = (unsigned short)((o->dec[(int)o->ndec] & 0xc000) | (N->succ[i].dec & 0x1fff)); o->ndec++; }
    if (N->succ[k].dec != 0xffff && o->ndec < 0x3f) {
        o->dec[(int)o->ndec] = (unsigned short)((o->dec[(int)o->ndec] & 0xc000) | (N->succ[k].dec & 0x1fff) | 0x2000);
        o->ndec++;
    }
    return 1;
}

/* bGetTicker / bGetTickerDifference (ms) */
static double pf_ms(uint64_t t0)
{
    return (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}
static int pf_budget(uint64_t t0) { return pf_ms(t0) + 0.4 < 1.0; }    /* 0x783d50 / 0x783d54, 0x783d4c */

static void pf_sort(AiGps *g)                                           /* 0x40c4d0 */
{
    int sw = 1;
    for (int pass = 0; pass < g->nopen && sw; pass++) {
        sw = 0;
        for (int i = 0; i < g->nopen - 1; i++) {
            PfNode *a = g->open[i], *b = g->open[i + 1];
            if (a->h + a->g < b->h + b->g) { g->open[i] = b; g->open[i + 1] = a; sw = 1; }
        }
    }
}
static void pf_clearlists(AiGps *g)                                     /* 0x413160 */
{
    for (int i = 0; i < g->nopen; i++) pf_free(g, g->open[i]);
    g->nopen = 0;
    g->nclosed = 0;
}
/* 0x41ea30 SetGPSState (RealController vt+0x4c) */
static void gps_setstate(AiGps *g, int s, const PfNode *N)
{
    g->gstate = s;
    if (N) g->node = *N;
}

/* 0x422b90 BuildInitialOpenList: in every section whose box holds the start,
   the first point the car's heading is not square to, posted, whose road
   (+5 m) holds the start within 4 m height: a node each way there */
static void pf_build(AiGps *g, uint64_t t0)
{
    const AiNet *n = g->net;
    while (g->cursor < n->norder) {
        if (!pf_budget(t0)) return;
        const unsigned char *s = n->order[g->cursor];
        if (g->start[0] >= rdf(s + 0x60) && g->start[0] <= rdf(s + 0x68) &&
            g->start[1] >= rdf(s + 0x64) && g->start[1] <= rdf(s + 0x6c)) {   /* 0x43d410 */
            int np = sec_npts(s), done = 0;
            for (int i = 0; i < np && !done; ) {
                Pt *p = sec_pt(s, i);
                float dot = pt_dx(p) * g->sdir[0] + pt_dy(p) * g->sdir[1];
                if (0 < fabsf(dot) && pt_posted(p, g->posted)) {        /* 0x783d48 */
                    float qx = g->start[0] - pt_x(p), qy = g->start[1] - pt_y(p);
                    float al = qx * pt_dx(p) + qy * pt_dy(p), lat = qx * pt_dy(p) - qy * pt_dx(p);
                    if (-5.0f <= al && al <= pt_seglen(p) + 5.0f && -pt_wl(p) - 5.0f <= lat && lat <= pt_wr(p) + 5.0f &&   /* 0x783d3c */
                        fabsf(pt_z(p) - g->start[2]) < 4.0f && g->nopen + 2 <= PF_OPEN_MAX) {        /* 0x783d44 */
                        PfNode *a = pf_alloc(g), *b = pf_alloc(g);
                        if (a && b) {
                            pf_init(a, n, p, 0 < dot, g->target, g->posted);
                            pf_init(b, n, p, !(0 < dot), g->target, g->posted);
                            pf_traverse(a, n, g->posted);
                            pf_traverse(b, n, g->posted);
                            g->open[g->nopen++] = a;
                            g->open[g->nopen++] = b;
                        } else { pf_free(g, a); pf_free(g, b); }
                        done = 1;
                    }
                }
                int st = (int)(v2len(pt_x(p) - g->start[0], pt_y(p) - g->start[1]) * 0.02f);   /* 0x7851c0, _ftol */
                i += st < 1 ? 1 : st;
            }
        }
        g->cursor++;
    }
    pf_sort(g);
    g->state = 2;
    g->nopen0 = g->nopen;
}
/* 0x422f90 ProcessAStar: expand the cheapest node; a successor is dropped
   when the open or closed list has its (point, direction) at no more cost */
static void pf_astar(AiGps *g, uint64_t t0)
{
    const AiNet *n = g->net;
    while (g->nopen >= 1 && !g->found && pf_budget(t0)) {
        PfNode *c = g->open[--g->nopen];
        if (!c->atgoal) {
            for (int k = 0; k < c->nsucc; k++) {
                PfNode *s = pf_alloc(g);
                if (!s) break;
                pf_successor(c, n, k, s);
                pf_traverse(s, n, g->posted);
                int io = -1, ic = -1, skip = 0;
                for (int i = 0; i < g->nopen && !skip; i++) {
                    PfNode *o = g->open[i];
                    if (s->cur == o->cur && s->dir == o->dir) { io = i; skip = !(s->g < o->g); }
                }
                for (int i = 0; i < g->nclosed && !skip; i++) {
                    PfLite *q = &g->closed[i];
                    if (s->cur == q->pt && s->dir == q->dir) { ic = i; skip = !(s->g < q->g); }
                }
                if (skip) { pf_free(g, s); continue; }
                if (ic >= 0) {
                    for (int i = ic; i + 1 < g->nclosed; i++) g->closed[i] = g->closed[i + 1];
                    g->nclosed--;
                }
                if (io < 0) {
                    if (g->nopen < PF_OPEN_MAX) g->open[g->nopen++] = s;
                    else pf_free(g, s);                                 /* (the original drops it unfreed) */
                } else { *g->open[io] = *s; pf_free(g, s); }           /* 0x40a2d0 */
                pf_sort(g);
            }
            if (g->nclosed < PF_CLOSED_MAX) {
                PfLite *q = &g->closed[g->nclosed++];
                q->pt = c->cur; q->dir = c->dir != 0; q->g = c->g;
            }
        } else if (!g->found) { g->result = *c; g->found = 1; }
        pf_free(g, c);
    }
    if (g->found || g->nopen == 0) { pf_clearlists(g); g->state = 3; }
}
/* 0x4268a0 Service: the time-sliced search; when finished the status and
   the requesting car's controller SetGPSState (found 1, none 3) */
static void pf_service(AiGps *g)
{
    uint64_t t0 = SDL_GetPerformanceCounter();
    if (g->state == 1) pf_build(g, t0);
    if (g->state == 2 && g->algorithm == 1) pf_astar(g, t0);
    if (g->state != 3) return;
    PfNode res;
    memset(&res, 0, sizeof res);
    if (g->found) { res = g->result; g->status = 2; }
    else { pf_init(&res, g->net, NULL, 0, g->target, 2); g->status = g->nopen0 > 0 ? 3 : 4; }
    g->last = g->status;
    if (!g->car) return;
    gps_setstate(g, g->status == 2 ? 1 : (g->status == 3 || g->status == 4) ? 3 : 0, &res);
    g->state = 0; g->cursor = -1;
    g->nopen = g->nclosed = 0;
    g->algorithm = g->posted = 0;
    g->found = 0;
}

AiGps *aigps_new(const AiNet *net)
{
    if (!net) return NULL;
    AiGps *g = (AiGps *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->net = net;
    g->cursor = -1;
    g->halfwid = 1.0f;
    for (int i = 0; i < PF_OPEN_MAX + 2; i++) g->pfree[g->npfree++] = &g->pool[i];
    g->w = aiworld_new(net, NULL, 0);
    g->r = (AiRacer *)calloc(1, sizeof *g->r);
    if (!g->w || !g->r) { aiworld_free(g->w); free(g->r); free(g); return NULL; }
    g->w->mode = 0;                                     /* free roam */
    AiRacer *r = g->r;                                  /* the human's RealController (0x4266d0) as its GPS list sees it */
    r->w = g->w;
    r->human = 1; r->skill = -2; r->owner_kind = 1;
    r->index = -1; r->slot = -1;
    r->ls.lane = 0;
    r->last_kind2_link = -1;
    r->rdir = 1;
    r->own = -1;
    r->gps = &g->node;                                  /* vt+0x64 0x418960 reads +0x428 */
    fl_reset(&r->L);
    r->L.xac = 1;                                       /* +0x4b0 +0xac */
    return g;
}
void aigps_free(AiGps *g)
{
    if (!g) return;
    if (g->r) { fl_reset(&g->r->L); free(g->r); }
    aiworld_free(g->w);
    free(g);
}
void aigps_set_halfwidth(AiGps *g, float half_wid) { if (g && 0 < half_wid) g->halfwid = half_wid; }
/* 0x40c440 SubmitRequest(pos, dir, target, postedness 2, algorithm 1, car):
   only when the manager is idle */
void aigps_submit(AiGps *g, const float pos[3], const float fwd[2], const float target[3])
{
    if (!g || g->state != 0) return;
    memcpy(g->start, pos, sizeof g->start);
    g->sdir[0] = fwd[0]; g->sdir[1] = fwd[1];
    memcpy(g->target, target, sizeof g->target);
    g->posted = 2;
    g->algorithm = 1;
    g->car = 1;
    g->cursor = 0;
    g->found = 0;
    g->state = 1;
    g->status = 1;
}
/* 0x4131f0 CancelRequest */
void aigps_cancel(AiGps *g)
{
    if (!g) return;
    if (g->car) { gps_setstate(g, 0, NULL); g->car = 0; }
    pf_clearlists(g);
    g->state = 0;
    g->algorithm = g->posted = 0;
    g->found = 0;
    g->status = g->last = 0;
    g->cursor = -1;
}
/* 0x41af90 GetClosestRoutePoint: walk the solved path from its start through
   its decisions; the first point whose road (+5 m) holds q, NULL when the
   goal or the path's end comes first. *back: the way along it. */
static Pt *pf_closest_route_point(const PfNode *N, const AiNet *n, const float *q, int P, int *back)
{
    int dir = N->startdir != 0, di = 0;
    Pt *p = N->start, *res = NULL;
    for (long guard = 0; p && guard < 1000000; guard++) {
        if (pt_latout(p, N->target) < 0 && pt_alongout(n, p, N->target) < 0) break;     /* 0x783d40 */
        if (q && pt_latout(p, q) < 5.0f && pt_alongout(n, p, q) < 5.0f) { res = p; break; }   /* 0x783d3c */
        int nl = pt_nlinks(n, p);
        Lk *must = NULL;
        for (int k = 0; k < nl && !must; k++) {
            Lk *l = pt_link(n, p, k);
            uint32_t f = lk_flags(l);
            int b18 = (int)(f >> 18 & 1);
            if (b18 == dir && (f & 0xf) == 3) break;
            if (b18 == dir || lk_ignore(l, P) || lk_never(l, P)) continue;
            if (lk_always(l, P)) { must = l; continue; }
            if (di < N->ndec && (unsigned)(lk_index(n, l) & 0xffff) == (unsigned)(N->dec[di] & 0x1fff)) {
                if (N->dec[di] >> 13 & 1) must = l;
                di++;
            }
        }
        if (must) { p = lk_pt(n, must); dir ^= (int)(lk_flags(must) >> 23 & 1); }
        else p = pt_next(n, p, dir);
    }
    if (back) *back = dir == 0;
    return res;
}
/* 0x42a360 RealController::UpdateGPSFuturePoints: arrival; keep the GPS
   list while the car is on it, else find the car on the solved path (none:
   RecalculateGPSPath 0x4188c0); then FuturePoints::Update 0x428030 */
static void gps_follow(AiGps *g, const float *pos, const float *fwd, float speed)
{
    const AiNet *n = g->net;
    AiRacer *r = g->r;
    FList *L = &r->L;
    int lane = 0;                                                       /* 0x4116a0 */
    int at = -1;
    memcpy(r->car.pos, pos, sizeof r->car.pos);
    r->car.fwd[0] = fwd[0]; r->car.fwd[1] = fwd[1]; r->car.fwd[2] = 0;
    r->car.absv = r->car.speed = speed;
    if (g->gstate != 0 && v2len(pos[0] - g->node.target[0], pos[1] - g->node.target[1]) < 30.0f &&   /* 0x783c54 */
        fabsf(pos[2] - g->node.target[2]) < 4.0f) {                     /* 0x783c58 */
        g->gstate = 0;
        aigps_cancel(g);
    }
    if (g->gstate == 0) { g->rp = NULL; return; }
    if (g->gstate == 3) g->rp = NULL;
    else {
        int keep = 0;
        if (L->n != 0) {
            float D = 0, lat = 0;
            fl_locate(n, L, pos, &D, &lat, lane);                      /* GetDL 0x419bc0 */
            at = fl_closest(n, L, pos, 0, lane, 0, 0);
            if (at != -1 && L->p[0]->dist - 50.0f <= D && D <= L->p[L->n - 1]->dist &&   /* 0x785b80 */
                fp_outside(n, L->p[at], lat, lane) <= g->halfwid) keep = 1;            /* 0x40ada0 */
        }
        if (!keep) {
            int back = 0;
            g->rp = pf_closest_route_point(&g->node, n, pos, 2, &back);
            g->fwd = back == 0;
            fl_reset(L);
            at = -1;
        }
    }
    if (!g->rp) {
        aigps_submit(g, pos, fwd, g->node.target);                      /* 0x4188c0 */
        fl_reset(L);
        return;
    }
    float look = speed * 1.5f;                                          /* 0x783c48 */
    if (look <= 100.0f) look = 100.0f;                                  /* 0x783c4c */
    fl_rebuild(r, at, g->rp, !g->fwd, 30.0f + look);                    /* 0x783c50 */
    g->gstate = 1;
}
void aigps_tick(AiGps *g, const float pos[3], const float fwd[2], float speed)
{
    if (!g) return;
    pf_service(g);                                      /* World::DoTimestep 0x609ce0 */
    gps_follow(g, pos, fwd, speed);                     /* RealController::Think 0x42caa0 */
}
int aigps_req_status(const AiGps *g) { return g ? g->status : 0; }
int aigps_last_result(const AiGps *g) { return g ? g->last : 0; }
int aigps_ctrl_state(const AiGps *g) { return g ? g->gstate : 0; }
/* Player::RenderGPSArrow 0x5f3cb0's aim: WantToDisplayGPSArrow 0x5f1640,
   GetDL, then GetWorldPosition 0x419d20 at D + max(1.5 v, 100) m (0x40a2a0)
   on the route's centre line */
int aigps_arrow_point(const AiGps *g, const float pos[3], float speed, float out[3])
{
    if (!g || (g->gstate != 1 && g->gstate != 2) || !g->rp) return 0;
    const AiNet *n = g->net;
    const FList *L = &g->r->L;
    if (L->n == 0) return 0;
    float D = 0, lat = 0;
    int at = fl_locate(n, L, pos, &D, &lat, 0);
    if (at == -1) return 0;
    float look = speed * 1.5f;
    if (look <= 100.0f) look = 100.0f;
    float d = D + look;
    int i = fl_nearest_dist(L, d, 1, at, 0);                           /* GetClosestPoint(d, 1, from, 0) */
    if (i == -1) return 0;
    if (i > 0) {
        const FPoint *a = L->p[i - 1], *b = L->p[i];
        float pa[3], pb[3];
        fp_point(n, b, pb, b->dist, 0, -1);
        fp_point(n, a, pa, a->dist, 0, -1);
        float t = (d - a->dist) / (b->dist - a->dist);
        for (int k = 0; k < 3; k++) out[k] = pa[k] + (pb[k] - pa[k]) * t;   /* bScaleAdd */
        return 1;
    }
    fp_point(n, L->p[0], out, d, 0, -1);
    return 1;
}
/* debug: the solved path's route points, start to goal (the walk of 0x41af90) */
int aigps_path(const AiGps *g, float *xyz, int max)
{
    if (!g || g->gstate == 0 || !g->node.start) return 0;
    const AiNet *n = g->net;
    const PfNode *N = &g->node;
    int dir = N->startdir != 0, di = 0, c = 0;
    Pt *p = N->start;
    for (long guard = 0; p && guard < 1000000; guard++) {
        if (xyz && c < max) { xyz[c * 3] = pt_x(p); xyz[c * 3 + 1] = pt_y(p); xyz[c * 3 + 2] = pt_z(p); }
        c++;
        if (xyz && c >= max) break;
        if (pt_latout(p, N->target) < 0 && pt_alongout(n, p, N->target) < 0) break;
        int nl = pt_nlinks(n, p);
        Lk *must = NULL;
        for (int k = 0; k < nl && !must; k++) {
            Lk *l = pt_link(n, p, k);
            uint32_t f = lk_flags(l);
            int b18 = (int)(f >> 18 & 1);
            if (b18 == dir && (f & 0xf) == 3) break;
            if (b18 == dir || lk_ignore(l, 2) || lk_never(l, 2)) continue;
            if (lk_always(l, 2)) { must = l; continue; }
            if (di < N->ndec && (unsigned)(lk_index(n, l) & 0xffff) == (unsigned)(N->dec[di] & 0x1fff)) {
                if (N->dec[di] >> 13 & 1) must = l;
                di++;
            }
        }
        if (must) { p = lk_pt(n, must); dir ^= (int)(lk_flags(must) >> 23 & 1); }
        else p = pt_next(n, p, dir);
    }
    return c;
}

/* ---- the controller's FuturePoints for the jump cam (jumpcam.c) ---------- */
int airacer_fp_count(const AiRacer *r) { return r ? r->L.n : 0; }
float airacer_fp_dist(const AiRacer *r, int i)
{
    return r && i >= 0 && i < r->L.n && r->L.p[i] ? r->L.p[i]->dist : 0.0f;
}
int airacer_fp_closest(const AiRacer *r, const float pos[3])
{
    if (!r || !r->w || !r->w->net || r->L.n <= 0) return -1;
    int lane = r->ls.lane > 0 ? r->ls.lane : 0;
    return fl_closest(r->w->net, &r->L, pos, 1, lane, 0, 0);
}
int airacer_fp_pos(const AiRacer *r, int i, int what, float inset, float out[3])
{
    if (!r || !r->w || !r->w->net || i < 0 || i >= r->L.n || !r->L.p[i]) return 0;
    const AiNet *n = r->w->net;
    const FPoint *f = r->L.p[i];
    if (!pt_at(n, f->sec, f->pt)) return 0;
    int lane = r->ls.lane > 0 ? r->ls.lane : 0;                         /* vt+0x1c, & (lane < 1) - 1 */
    switch (what) {
    case 0: fp_point(n, f, out, f->dist, 0, -1); break;                 /* 0x412470: -(lane offset + shift) */
    case 1: fp_point(n, f, out, f->dist, fp_right(n, f, lane) - inset, lane); break;
    case 2: fp_point(n, f, out, f->dist, fp_left(n, f, lane) + inset, lane); break;
    case 3: fp_point(n, f, out, f->dist, f->lo[0], 0); break;
    default: fp_point(n, f, out, f->dist, f->hi[0], 0); break;
    }
    return 1;
}
