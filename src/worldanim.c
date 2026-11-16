/* worldanim.c -- the world animations (see worldanim.h). */
#include "worldanim.h"
#include "nis.h"
#include "nfsu2.h"
#include "world.h"
#include "resource.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "SDL.h"

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
/* test aid: OPENUG2_WATEST=1 logs the solids, the entities every 10 s and the
   first time each comes into view; =<entity hash> also follows that one on screen */
static int wa_test(unsigned *watch)
{
    static int t = -1; static unsigned w;
    if (t < 0) {
        const char *s = getenv("OPENUG2_WATEST");
        t = s != NULL;
        w = s ? (unsigned)strtoul(s, NULL, 16) : 0;
        if (w <= 1) w = 0;
    }
    if (watch && !*watch) *watch = w;
    return t;
}

/* 0x4013f0: o = a * b (row vectors) */
static void m4mul(const float *a, const float *b, float *o)
{
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(o, r, sizeof r);
}

/* ---- the data ---------------------------------------------------------------- */
typedef struct {                    /* WorldAnimEntity 0x37080 */
    uint32_t name, solid, tree, anim;
    unsigned char ch; signed char par;
    float M[16];                    /* +0x30 */
} WaEntDef;
typedef struct { uint32_t hash; int e0, ne; } WaTreeDef;   /* 0x37110 closes [e0, e0 + ne) */
typedef struct {                    /* WorldAnimInstance 0x37090 */
    uint32_t id;                    /* +0: GetAnimTree 0x432170's key */
    uint32_t tree, section, flags, shop0, shop1, range, race;
    float rate, d0, d1;
    uint16_t r0, r1, fwdrev;
    float M[16];                    /* +0x40 */
    float lo[3], hi[3];             /* +0x80 / +0x90 */
} WaInstDef;

typedef struct {                    /* CWorldAnimCtrl (ctor 0x435010) */
    int dir;                        /* +0x08 */
    int state;                      /* +0x10: 1 playing, 2 stopped */
    float acc0, acc1;               /* +0x14 / +0x18: delay clocks */
    uint32_t flags;                 /* +0x34 */
    int r0, r1;                     /* +0x38 / +0x3c: loop range */
    float speed;                    /* +0x40 */
    float rate;                     /* +0x5c */
    float len;                      /* +0x60: frames */
    float time;                     /* +0x64: frame */
    float d0, d1;                   /* +0x68 / +0x6c: delays (s) */
    NisChan *ch[3];                 /* +0x44 _t, +0x48 _q, +0x4c _s */
    float pose[24];                 /* +0x24: the ROOT skeleton's pose */
} WaCtrl;

typedef struct {
    const WaEntDef *d;
    int tree;                       /* index into g.tr */
    int parent;                     /* index into g.ent, -1 the tree's root */
    int model;                      /* index into g.mdl, -1 none */
    int has_ctrl, pending, seen;
    WaCtrl c;
    float local[16];                /* the SpaceNode's local matrix */
} WaEnt;
typedef struct {
    const WaInstDef *in;
    int ctl;                        /* tree +0x2c: -1, 0 ShopDoor, 3 FwdRevTrack */
    int e0, ne;
} WaTree;

typedef struct {
    uint32_t hash;
    int state;                      /* 0 not found, 1 meshes on the CPU, 2 uploaded, -1 failed */
    N2Scene sc;
    GpuMesh *gm; int n;
    GLuint *tex; unsigned char *mode;
    float lo[3], hi[3];             /* model space */
    float at[3];                    /* test aid: the solid's own placement */
} WaModel;

static struct {
    unsigned char *bun; long bunlen;     /* the companion bundle (entities point into it) */
    NisBanks *banks;
    WaEntDef *ed; int ned;
    WaTreeDef *td; int ntd;
    WaInstDef *id; int nid;
    WaModel *mdl; int nmdl;
    WaEnt *ent; int nent, cap_ent;
    WaTree *tr; int ntr;
    char pin[1100];                      /* the pinned STREAM bundle (textures) */
    int nis;
    unsigned watch; int watch_n;         /* test aid (OPENUG2_WATEST): the last ANIM_POSITION target */
    int logged;
    float planes[6][4]; int have_planes; /* the main view's frustum of the last drawn frame */
} g;

static void ent_defs(const unsigned char *d, long len)
{
    int ne = 0, ni = 0, nt = 0;
    for (long q = 0; q + 8 <= len;) {                         /* count */
        uint32_t cid = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > len) break;
        ne += cid == 0x37080u; ni += cid == 0x37090u; nt += cid == 0x37110u;
        q += 8 + (long)sz;
    }
    g.ed = (WaEntDef *)calloc((size_t)ne + 1, sizeof *g.ed);
    g.id = (WaInstDef *)calloc((size_t)ni + 1, sizeof *g.id);
    g.td = (WaTreeDef *)calloc((size_t)nt + 1, sizeof *g.td);
    if (!g.ed || !g.id || !g.td) return;
    int open = 0;                                             /* first entity of the open tree */
    for (long q = 0; q + 8 <= len;) {
        uint32_t cid = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > len) break;
        long a = (q + 8 + 15) & ~15L, end = q + 8 + (long)sz;  /* 16-aligned data in the chunk */
        if (cid == 0x37080u && a + 0x70 <= end) {             /* LoaderWorldAnimEntity */
            WaEntDef *e = &g.ed[g.ned++];
            const unsigned char *p = d + a;
            e->name = rd32(p + 4); e->solid = rd32(p + 0xc); e->tree = rd32(p + 0x20); e->anim = rd32(p + 0x24);
            e->ch = p[0x28]; e->par = (signed char)p[0x29];
            for (int i = 0; i < 16; i++) e->M[i] = rdf(p + 0x30 + 4 * i);
        } else if (cid == 0x37110u && a + 4 <= end) {         /* LoaderWorldAnimTreeMarker 0x43a260 */
            WaTreeDef *t = &g.td[g.ntd++];
            t->hash = rd32(d + a); t->e0 = open; t->ne = g.ned - open;
            open = g.ned;
        } else if (cid == 0x37090u && a + 0x9c <= end) {      /* LoaderWorldAnimInstance (0xa8 chunk, data 16-aligned: the box ends at +0x9c) */
            WaInstDef *n = &g.id[g.nid++];
            const unsigned char *p = d + a;
            n->id = rd32(p); n->tree = rd32(p + 4); n->section = rd32(p + 8); n->flags = rd32(p + 0xc);
            n->rate = rdf(p + 0x10); n->d0 = rdf(p + 0x14); n->d1 = rdf(p + 0x18);
            n->r0 = rd16(p + 0x1c); n->r1 = rd16(p + 0x1e);
            n->shop0 = rd32(p + 0x20); n->shop1 = rd32(p + 0x24);
            n->range = rd32(p + 0x30); n->race = rd32(p + 0x38); n->fwdrev = rd16(p + 0x3c);
            for (int i = 0; i < 16; i++) n->M[i] = rdf(p + 0x40 + 4 * i);
            for (int i = 0; i < 3; i++) { n->lo[i] = rdf(p + 0x80 + 4 * i); n->hi[i] = rdf(p + 0x90 + 4 * i); }
        }
        q += 8 + (long)sz;
    }
}

/* ---- the solids (the STREAM bundle's sections) ------------------------------ */
static int mdl_index(uint32_t h)
{
    for (int i = 0; i < g.nmdl; i++) if (g.mdl[i].hash == h) return i;
    return -1;
}
/* a section's bytes in the STREAM bundle (the companion's section table
   0x34110: 0x50-byte rows, +8 section number, +0x14 offset, +0x18 size) */
static int sec_range(uint32_t num, long *off, long *size)
{
    for (long q = 0; q + 8 <= g.bunlen;) {
        uint32_t cid = rd32(g.bun + q), sz = rd32(g.bun + q + 4);
        if (q + 8 + (long)sz > g.bunlen) break;
        if (cid == 0x34110u)
            for (long r = q + 8; r + 0x50 <= q + 8 + (long)sz; r += 0x50)
                if (rd32(g.bun + r + 8) == num) { *off = (long)rd32(g.bun + r + 0x14); *size = (long)rd32(g.bun + r + 0x18); return 1; }
        q += 8 + (long)sz;
    }
    return 0;
}
/* one solid object of a list: its meshes back in model space (the loader
   bakes the solid's own 0x134011 matrix in; a WorldModel draws the raw
   vertices at its node) */
static void mdl_take(WaModel *m, const unsigned char *L, long o, long sz)
{
    uint32_t slot[64];
    int ns = n2_mesh_texslots(L, o + 8, o + 8 + sz, slot, 64);
    n2_walk_meshes(L, o, o + 8 + sz, &m->sc, slot, ns);
    float M[16];
    n2_obj_matrix(L, o + 8, o + 8 + sz, M);
    m->at[0] = M[12]; m->at[1] = M[13]; m->at[2] = M[14];
    float a = M[0], b = M[1], c = M[2], d = M[4], e = M[5], f = M[6], h = M[8], i = M[9], k = M[10];
    float det = a * (e * k - f * i) - b * (d * k - f * h) + c * (d * i - e * h);
    if (fabsf(det) < 1e-12f) det = 1.0f;
    float I[9] = { (e * k - f * i) / det, (c * i - b * k) / det, (b * f - c * e) / det,
                   (f * h - d * k) / det, (a * k - c * h) / det, (c * d - a * f) / det,
                   (d * i - e * h) / det, (b * h - a * i) / det, (a * e - b * d) / det };
    for (int k2 = 0; k2 < 3; k2++) { m->lo[k2] = 1e30f; m->hi[k2] = -1e30f; }
    for (int s = 0; s < m->sc.count; s++) {
        N2Mesh *x = &m->sc.meshes[s];
        for (int v = 0; v < x->nverts; v++) {
            float *p = x->verts + v * 5;
            float px = p[0] - M[12], py = p[1] - M[13], pz = p[2] - M[14];
            p[0] = px * I[0] + py * I[3] + pz * I[6];
            p[1] = px * I[1] + py * I[4] + pz * I[7];
            p[2] = px * I[2] + py * I[5] + pz * I[8];
            for (int k2 = 0; k2 < 3; k2++) { if (p[k2] < m->lo[k2]) m->lo[k2] = p[k2]; if (p[k2] > m->hi[k2]) m->hi[k2] = p[k2]; }
        }
        x->world_bounds_valid = 0;
    }
    m->state = m->sc.count > 0 ? 1 : -1;
}
/* the bundle's chunk index while solids_load runs (resource.c), or NULL */
static ResChunk *g_six; static int g_nsix;
static int solids_scan(FILE *f, long xo, long xs, int found)
{
    /* a section that starts on an indexed header walks the index (same chain,
       same stop rule); otherwise the file, header by header */
    int k = g_six ? res_chunk_find(g_six, g_nsix, xo) : -1;
    for (long o = xo; o + 8 <= xo + xs && found < g.nmdl;) {
        unsigned char h[8];
        uint32_t id, sz;
        if (k >= 0) {
            if (k >= g_nsix || (long)g_six[k].off != o) break;
            id = g_six[k].id; sz = g_six[k].size; k++;
            if (id == 0x80134000u && fseek(f, o + 8, SEEK_SET)) break;
        } else {
            if (fseek(f, o, SEEK_SET) || fread(h, 1, 8, f) != 8) break;
            id = rd32(h); sz = rd32(h + 4);
        }
        if (id == 0x80134000u) {
            /* the list's 0x80134001 -> 0x134003: {hash, 0} per solid */
            long want = sz < 0x2000 ? (long)sz : 0x2000;
            unsigned char *hd = (unsigned char *)malloc((size_t)want + 8);
            int hit = 0;
            if (hd && fread(hd, 1, (size_t)want, f) == (size_t)want)
                for (long p = 0; p + 8 <= want && !hit;) {
                    uint32_t c = rd32(hd + p), s = rd32(hd + p + 4);
                    if (c == 0x80134001u) { p += 8; continue; }    /* into it */
                    if (c == 0x134003u)
                        for (long r = p + 8; r + 8 <= p + 8 + (long)s && r + 8 <= want; r += 8) {
                            int mi = mdl_index(rd32(hd + r));
                            if (mi >= 0 && g.mdl[mi].state == 0) hit = 1;
                        }
                    if (c == 0x80134010u) break;
                    p += 8 + (long)s;
                }
            free(hd);
            unsigned char *L = hit ? (unsigned char *)malloc((size_t)sz + 8) : NULL;
            if (L && !fseek(f, o, SEEK_SET) && fread(L, 1, (size_t)sz + 8, f) == (size_t)sz + 8)
                for (long p = 8; p + 8 <= 8 + (long)sz;) {
                    uint32_t c = rd32(L + p), s = rd32(L + p + 4);
                    if (p + 8 + (long)s > 8 + (long)sz) break;
                    if (c == 0x80134010u) {                         /* a solid: its 0x134011 name hash */
                        for (long r = p + 8; r + 8 <= p + 8 + (long)s;) {
                            uint32_t c2 = rd32(L + r), s2 = rd32(L + r + 4);
                            if (c2 == 0x134011u) {
                                int pad = n2_skip_filler(L + r + 8, (int)s2);
                                if (pad + 0x14 <= (int)s2) {
                                    int mi = mdl_index(rd32(L + r + 8 + pad + 0x10));
                                    if (mi >= 0 && g.mdl[mi].state == 0) { mdl_take(&g.mdl[mi], L, p, (long)s); found++; }
                                }
                                break;
                            }
                            r += 8 + (long)s2;
                        }
                    }
                    p += 8 + (long)s;
                }
            free(L);
        }
        o += 8 + (long)sz;
    }
    return found;
}
static void solids_load(const char *stream_path)
{
    if (!g.nmdl) return;
    FILE *f = fopen(stream_path, "rb");
    if (!f) { printf("world anims: %s missing\n", stream_path); return; }
    clock_t t0 = clock();
    long xo = 0, xs = 0, flen = 0;
    int found = 0, secs = 0;
    if (!fseek(f, 0, SEEK_END)) flen = ftell(f);
    /* Retail: WorldModel's ctor (0x637db0, GC 8024ca1c) -> eModel::Init
       (0x493600) -> eFindSolid(hash) (0x4908e0) over every resident solid
       list; a list loaded later reconnects the eModels it holds
       (NotifySolidLoader GC 800c5b80 -> eModel::ReconnectSolid 0x48d780), and
       WorldModel::Render draws nothing while the solid is missing. The port
       keeps no per-section residency for these props, so "every list" is every
       section of STREAM<stem>.BUN (the track's section table, 0x34110), read
       once at track load: X0 (2400, always resident) first, then the table in
       order until each hash has its solid. A hash found nowhere stays state 0
       = not drawn, as an unconnected eModel. */
    g_six = res_chunk_index(stream_path, &g_nsix);
    if (sec_range(2400, &xo, &xs)) { found = solids_scan(f, xo, xs, found); secs++; }   /* X0 */
    for (long q = 0; q + 8 <= g.bunlen && found < g.nmdl;) {
        uint32_t cid = rd32(g.bun + q), sz = rd32(g.bun + q + 4);
        if (q + 8 + (long)sz > g.bunlen) break;
        if (cid == 0x34110u)
            for (long r = q + 8; r + 0x50 <= q + 8 + (long)sz && found < g.nmdl; r += 0x50) {
                long so = (long)rd32(g.bun + r + 0x14), ss = (long)rd32(g.bun + r + 0x18);
                if (rd32(g.bun + r + 8) == 2400 || ss <= 0 || so + ss > flen) continue;
                int before = found;
                found = solids_scan(f, so, ss, found);
                secs += found > before;
                LOAD_PUMP();   /* a section read: the loading screen keeps moving */
            }
        q += 8 + (long)sz;
    }
    free(g_six); g_six = NULL; g_nsix = 0;
    fclose(f);
    int meshes = 0;
    for (int i = 0; i < g.nmdl; i++) meshes += g.mdl[i].state > 0 ? g.mdl[i].sc.count : 0;
    printf("world anims: %d of %d solids found in %d sections (%d meshes, %.0f ms)\n", found, g.nmdl, secs, meshes,
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    if (wa_test(NULL))                                /* test aid */
        for (int i = 0; i < g.nmdl; i++)
            printf("world anims: solid %08x state %d, %d meshes, placed at (%.0f %.0f %.0f), box %.1f %.1f %.1f .. %.1f %.1f %.1f\n",
                   g.mdl[i].hash, g.mdl[i].state, g.mdl[i].state > 0 ? g.mdl[i].sc.count : 0, g.mdl[i].at[0], g.mdl[i].at[1],
                   g.mdl[i].at[2], g.mdl[i].lo[0], g.mdl[i].lo[1], g.mdl[i].lo[2], g.mdl[i].hi[0], g.mdl[i].hi[1], g.mdl[i].hi[2]);
}

void wa_free(void)
{
    for (int i = 0; i < g.nent; i++)
        for (int k = 0; k < 3; k++) nis_chan_free(g.ent[i].c.ch[k]);
    for (int i = 0; i < g.nmdl; i++) {
        WaModel *m = &g.mdl[i];
        if (m->gm) free_scene_gpu(m->gm, m->n);
        n2_free_scene(&m->sc);
        free(m->tex); free(m->mode);          /* the GL names are the world cache's or ours: */
    }
    nis_banks_free(g.banks);
    free(g.bun); free(g.ed); free(g.td); free(g.id); free(g.mdl); free(g.ent); free(g.tr);
    memset(&g, 0, sizeof g);
}

int wa_load(const char *troot, const char *track)
{
    wa_free();
    if (!troot || !track) return 0;
    const char *stem = !strncmp(track, "STREAM", 6) ? track + 6 : track;
    char p[1100];
    snprintf(p, sizeof p, "%s/%s.BUN", troot, stem);
    g.bun = n2_read_file(p, &g.bunlen);
    if (!g.bun) return 0;
    ent_defs(g.bun, g.bunlen);
    if (!g.nid) { printf("world anims: %s has none\n", stem); return 0; }
    g.banks = nis_banks_load(g.bun, g.bunlen);
    /* the solids the entities draw (CWorldAnimEntity::Init: a WorldModel when
       the entity has a solid and is not its tree's root) */
    g.mdl = (WaModel *)calloc((size_t)g.ned + 1, sizeof *g.mdl);
    for (int i = 0; i < g.ned && g.mdl; i++) {
        const WaEntDef *e = &g.ed[i];
        if (e->solid && e->tree != e->name && mdl_index(e->solid) < 0) g.mdl[g.nmdl++].hash = e->solid;
    }
    snprintf(p, sizeof p, "%s/STREAM%s.BUN", troot, stem);
    snprintf(g.pin, sizeof g.pin, "%s", p);
    solids_load(p);
    printf("world anims: %s: %d instances, %d trees, %d entities, %d animation banks, %d solids\n",
           stem, g.nid, g.ntd, g.ned, nis_banks_count(g.banks), g.nmdl);
    return g.nid;
}

/* ---- CWorldAnimCtrl ----------------------------------------------------------- */
/* QuickSpline::ClampParam 0x43c240 (GC 80192b98): a - floor(a / |b|) x |b|
   (_ftol 0x75bed4 truncates, one off when that lands above the quotient) */
static float clamp_param(float a, float b)
{
    b = fabsf(b);
    float q = a / b, n = (float)(int)q;
    if (n > q) n -= 1.0f;
    return (q - n) * b;
}
/* 0x435140 */
static int advance(WaCtrl *c, float dt)
{
    float sp = c->speed * c->rate;
    float step = 30.0f * sp * dt;                                      /* 0x785bd8 */
    float a0 = 30.0f * c->acc0 * sp, lim0 = 30.0f * c->d0;
    float a1 = 30.0f * c->acc1 * sp, lim1 = 30.0f * c->d1;
    float span, lo, hi;
    uint32_t fl = c->flags;
    if (!(fl & 0x40)) { span = c->len; lo = 0; hi = c->len; }
    else {
        hi = (float)c->r1 * sp; lo = (float)c->r0 * sp; span = hi - lo;
    }
    int loop = (fl & 0x20) && !(fl & 0x800);
    int fwd = (fl >> 3) & 1, pp = (fl >> 4) & 1;
    if (!fwd) { if (!pp) { fl |= 8; fwd = 1; c->flags = fl; } }
    else if (pp) { fl &= ~0x10u; pp = 0; c->flags = fl; }
    int in0 = (c->flags & 0x80) && a0 < lim0 && c->time < hi;
    int in1 = (c->flags & 0x100) && a1 < lim1 && c->time < hi;
    float t = c->time;
    if (c->state != 1) return 0;
    if (in0) {                                                         /* the start delay */
        a0 += step;
        if (lim0 < a0) t = clamp_param(a0, lim0) + t;                 /* 0x4352e8 */
        c->acc0 = 1.0f / sp / 30.0f * a0;
        c->time = t;
        return 0;
    }
    if (in1) {                                                         /* the delay before each loop */
        a1 += step;
        if (lim1 < a1) t = clamp_param(a1, lim1) + t;                 /* 0x435342 */
        c->acc1 = 1.0f / sp / 30.0f * a1;
        c->time = t;
        return 0;
    }
    if (!fwd) {
        if (pp) {                                                      /* there and back, direction +8 */
            if (c->dir == 0) {
                t += step;
                if (hi < t) {
                    if (c->flags & 0x800) c->state = 2;
                    c->dir = 1;
                    t = hi - (t - hi);
                }
            } else if (c->dir == 1) {
                t -= step;
                if (t < lo) {
                    if (c->flags & 0x800) c->state = 2;
                    c->time = lo; c->dir = 0; c->acc1 = 0;
                    return 0;
                }
            }
        }
    } else if (!(c->flags & 0x1000)) {
        t += step;
        if (hi < t) {
            if (!loop) { c->time = hi; return 1; }
            c->acc1 = 0;
            c->time = t - span;
            return 0;
        }
    } else {
        t -= step;
        if (t < lo) {
            if (!loop) { c->time = lo; return 1; }
            c->acc1 = 0;
            c->time = t + span;
            return 0;
        }
    }
    c->time = t;
    return 0;
}
/* 0x4316b0: _q, _s, then _t into the pose; bone 1 to the node */
static void pose_update(WaEnt *e)
{
    WaCtrl *c = &e->c;
    if (c->ch[1]) nis_chan_eval(c->ch[1], c->time, c->pose, 24);
    if (c->ch[2]) nis_chan_eval(c->ch[2], c->time, c->pose, 24);
    if (c->ch[0]) nis_chan_eval(c->ch[0], c->time, c->pose, 24);
    nis_root_bone1(c->pose, e->local);
    e->pending = 0;
}

/* ---- StartWorldAnimations ----------------------------------------------------- */
static int race_ok(uint32_t f, int event)                    /* 0x43a4a0's filter */
{
    if (!f) return 1;
    int ev = event % 100;
    int b0 = (int)(f & 0xff), b1 = (int)(f >> 8 & 0xff), b2 = (int)(f >> 16 & 0xff), b3 = (int)(f >> 24 & 0xff);
    if (ev < b1 || b0 < ev) {
        if (ev < b3) return 0;
        if (b2 < ev) return 0;
    }
    return 1;
}
static void tree_free(void)
{
    for (int i = 0; i < g.nent; i++)
        for (int k = 0; k < 3; k++) nis_chan_free(g.ent[i].c.ch[k]);
    free(g.ent); free(g.tr);
    g.ent = NULL; g.tr = NULL; g.nent = g.ntr = 0;
}
void wa_start(int event, int reverse)
{
    tree_free();
    if (!g.nid) return;
    int cap = 0;
    for (int i = 0; i < g.nid; i++)
        for (int t = 0; t < g.ntd; t++) if (g.td[t].hash == g.id[i].tree) cap += g.td[t].ne;
    g.ent = (WaEnt *)calloc((size_t)cap + 1, sizeof *g.ent);
    g.tr = (WaTree *)calloc((size_t)g.nid + 1, sizeof *g.tr);
    if (!g.ent || !g.tr) { tree_free(); return; }
    int playing = 0, ctrls = 0;
    for (int i = 0; i < g.nid; i++) {
        WaInstDef *in = &g.id[i];
        if (!race_ok(in->race, event)) continue;
        const WaTreeDef *td = NULL;
        for (int t = 0; t < g.ntd && !td; t++) if (g.td[t].hash == in->tree) td = &g.td[t];   /* 0x439ee0 */
        if (!td) continue;
        WaTree *T = &g.tr[g.ntr];
        T->in = in; T->e0 = g.nent; T->ne = 0;
        T->ctl = -1;
        if (in->range) in->flags |= 0x40;
        if (in->shop0) T->ctl = 0;
        else if (in->fwdrev) T->ctl = 3;
        /* flags 0x200 / 0x400 (bRandom delays / start frames, seed
           0x69babe69) are not ported: no instance of any track sets them
           (p2E data scan), so the result is the same */
        for (int k = 0; k < td->ne; k++) {
            const WaEntDef *d = &g.ed[td->e0 + k];
            WaEnt *e = &g.ent[g.nent];
            memset(e, 0, sizeof *e);
            e->d = d; e->tree = g.ntr;
            e->parent = d->par >= 0 && d->par < k ? T->e0 + d->par : -1;
            e->model = d->solid && d->tree != d->name ? mdl_index(d->solid) : -1;
            memcpy(e->local, d->M, sizeof e->local);
            /* 0x438860: the controller */
            WaCtrl *c = &e->c;
            c->speed = 1.0f; c->rate = in->rate == 0.0f ? 1.0f : in->rate;
            c->d0 = in->d0; c->flags |= 0x80;
            c->d1 = in->d1; c->flags |= 0x100;
            c->flags |= in->flags ? in->flags : 0x28;
            nis_root_rest(c->pose);
            static const char *const suf[3] = { "_t", "_q", "_s" };
            int any = 0;
            for (int ch = 0; ch < 3; ch++) {
                if (!(d->ch & (1 << ch))) continue;
                uint32_t h = d->anim;                               /* bStringHash(suffix, seed = anim) */
                for (const char *s = suf[ch]; *s; s++) h = h * 33u + (unsigned char)*s;
                float len = 0;
                c->ch[ch] = nis_chan_bind(g.banks, h, &len);
                if (c->ch[ch]) { c->len = len; any = 1; }           /* vt+0x10 -> +0x60, +0x70 = 1 */
            }
            e->has_ctrl = any;
            if (any) {
                if (in->flags & 0x40) { c->flags |= 0x40; c->r0 = in->r0; c->r1 = in->r1; }
                if (in->shop0 || in->shop1) { c->flags &= ~8u; c->flags |= 0x810; }
                ctrls++;
            }
            if (d->name == in->tree) {                              /* the root: the instance's matrix */
                memcpy(e->local, in->M, sizeof e->local);
            }
            if (!in->shop0 && !in->fwdrev && e->has_ctrl) { c->state = 1; playing++; }   /* Play() */
            g.nent++; T->ne++;
        }
        if (T->ctl == 3)                                            /* FwdRevTrack 0x435880 */
            for (int k = 0; k < T->ne; k++) {
                WaEnt *e = &g.ent[T->e0 + k];
                if (!e->has_ctrl) continue;
                if (reverse == 1) { e->c.flags |= 0x1000; e->c.time = e->c.len; }
                e->c.state = 1; playing++;
            }
        g.ntr++;
    }
    if (g.logged++ < 4)
        printf("world anims: started for event %d%s: %d trees, %d entities, %d animated, %d playing\n",
               event, reverse ? " (reversed)" : "", g.ntr, g.nent, ctrls, playing);
}

/* ---- the frame ------------------------------------------------------------------ */
/* eViewPlatInterface::GetVisibleStateSB 0x5bc300 (GC 801e9608): six planes,
   centre . n + d + half-extent . |n| < 0 (0x7d7d3c = 0.0) on any plane = not
   visible; the farthest corner along n is the same test */
static int box_in_view(const float (*planes)[4], const float lo[3], const float hi[3])
{
    if (!planes) return 1;
    for (int p = 0; p < 6; p++) {
        const float *v = planes[p];
        float d = v[3];
        for (int c = 0; c < 3; c++) d += v[c] * (v[c] >= 0 ? hi[c] : lo[c]);
        if (d < 0.0f) return 0;
    }
    return 1;
}
void wa_update(float dt, int nis_playing)
{
    g.nis = nis_playing;
    if (dt <= 0) return;
    for (int i = 0; i < g.nent; i++) {
        WaEnt *e = &g.ent[i];
        if (!e->has_ctrl) continue;
        advance(&e->c, dt);                                         /* 0x435980 */
        if (e->model < 0) continue;
        /* CWorldAnimEntity::UpdateTimeStep 0x435980 (GC 80048b00): the pose
           follows (UpdateAnimPose 0x4316b0 + SetDirty) when the tree poses
           always (0x2000), a scene plays, or it is no shop's tree and
           IsAABBCurrentlyVisible 0x48ce10 (GC 800c3f5c) holds for the
           instance's box (+0x80 / +0x90, world space: GetVisibleState
           0x5bc300 with no matrix, main view 0x832e50). Out of view the clock
           runs on but the pose stays where it was. The port tests the frustum
           of the last drawn frame (retail's view planes are also last frame's).
           The shop door timer 0x827eec (CAnimManager +0x6c, counted down by
           UpdateShopDoorTimeout 0x4321c0 / GC 80047db4) has one writer:
           ShopNIS_MakeChoiceAndStartStreaming (GC 800479b8) sets 0 for a
           ShopEnter NIS, 10 s for a ShopExit NIS. The port plays no shop NIS,
           so it stays 0 here (CONFIRMED G3 11-01). */
        const WaTree *T = &g.tr[e->tree];
        if ((e->c.flags & 0x2000) || g.nis) e->pending = 1;
        else if (!T->in->shop0 && (!g.have_planes || box_in_view((const float (*)[4])g.planes, T->in->lo, T->in->hi)))
            e->pending = 1;
    }
    static int tick;
    if (wa_test(NULL) && tick++ % 600 == 0)                           /* test aid */
        for (int i = 0; i < g.nent; i++) {
            WaEnt *e = &g.ent[i];
            if (e->model < 0) continue;
            float pos[3];
            wa_entity_pos(e->d->name, pos);
            const WaTree *T = &g.tr[e->tree];
            int vis = !g.have_planes || box_in_view((const float (*)[4])g.planes, T->in->lo, T->in->hi);
            printf("world anims: ent %08x solid %08x st %d time %.1f/%.1f flags %x at (%.0f %.0f %.0f), tree box %s\n",
                   e->d->name, e->d->solid, e->c.state, e->c.time, e->c.len, e->c.flags, pos[0], pos[1], pos[2],
                   vis ? "in view (posed)" : "out of view (pose held)");
        }
}

static void ent_world(const WaEnt *e, float W[16])
{
    memcpy(W, e->local, sizeof e->local);
    for (int p = e->parent, n = 0; p >= 0 && n < 16; p = g.ent[p].parent, n++)
        m4mul(W, g.ent[p].local, W);
    if (e->parent < 0 && e->d->name != g.tr[e->tree].in->tree)      /* a second root: at the instance */
        m4mul(W, g.tr[e->tree].in->M, W);
}

/* The draw's world matrices, the parent chain's product kept per entity for
   the frame (11-15, PERF_PLAN P3): hwlog62 the per-entity chain walk was
   ~0.48 ms/frame for 124 entities. Same product (L_e . L_p1 . L_p2 ...);
   a parent with a pending pose is brought up to date first (SpaceNode::Update
   order: the parent's matrix before the child's). */
static float (*g_wc)[16]; static unsigned *g_wcs; static int g_wc_cap; static unsigned g_wframe;
static const float *ent_chain(int i, int depth)
{
    WaEnt *e = &g.ent[i];
    if (g_wcs[i] == g_wframe) return g_wc[i];
    if (e->pending) pose_update(e);
    if (e->parent >= 0 && depth < 16) m4mul(e->local, ent_chain(e->parent, depth + 1), g_wc[i]);
    else memcpy(g_wc[i], e->local, sizeof e->local);
    g_wcs[i] = g_wframe;
    return g_wc[i];
}
static void ent_world_frame(int i, float W[16])
{
    const WaEnt *e = &g.ent[i];
    memcpy(W, ent_chain(i, 0), 16 * sizeof(float));
    if (e->parent < 0 && e->d->name != g.tr[e->tree].in->tree)      /* a second root: at the instance */
        m4mul(W, g.tr[e->tree].in->M, W);
}

int wa_entity_pos(unsigned name, float pos[3])
{
    for (int i = 0; i < g.nent; i++) {
        WaEnt *e = &g.ent[i];
        if (e->d->name != name) continue;
        if (e->pending) pose_update(e);
        float W[16]; ent_world(e, W);
        pos[0] = W[12]; pos[1] = W[13]; pos[2] = W[14];
        return e->model >= 0;
    }
    return 0;
}

/* CWorldAnimCtrl IsCurrentlyDelayingBeforeWorldStart 0x431610 /
   IsCurrentlyDelayingBetweenLoops 0x431660 */
static int delaying(const WaCtrl *c)
{
    float sp = c->speed * c->rate;
    float hi = (c->flags & 0x40) ? (float)c->r1 * sp : c->len;
    if ((c->flags & 0x80) && 30.0f * c->acc0 * sp < 30.0f * c->d0 && c->time < hi) return 1;
    if ((c->flags & 0x100) && 30.0f * c->acc1 * sp < 30.0f * c->d1 && c->time < hi) return 1;
    return 0;
}
int wa_anim_position(unsigned id, unsigned name, float pos[3], int *delay)
{
    for (int t = 0; t < g.ntr; t++) {
        const WaTree *T = &g.tr[t];
        if (T->in->id != id) continue;                              /* GetAnimTree 0x432170 */
        for (int i = T->e0; i < T->e0 + T->ne && i < g.nent; i++) {
            WaEnt *e = &g.ent[i];
            if (e->d->name != name) continue;
            if (e->pending) pose_update(e);                         /* SpaceNode::Update */
            float W[16]; ent_world(e, W);
            pos[0] = W[12]; pos[1] = W[13]; pos[2] = W[14];
            if (delay) *delay = e->has_ctrl && delaying(&e->c);
            return 1;
        }
        return 0;
    }
    return 0;
}

/* ---- the broadcast ----------------------------------------------------------- */
static int mode_of(float a)
{
    if (fabsf(a - 1.0f) < 1e-6f) return 1;                           /* 0x7862e0 */
    if (fabsf(a) < 1e-6f) return 0;
    return -1;
}
void wa_message(int msg, unsigned target, float a, float b, unsigned shop)
{
    int mode = mode_of(a), n = 0;
    for (int t = 0; t < g.ntr; t++) {
        const WaTree *T = &g.tr[t];
        if (msg == 1) {                                             /* ShopDoor 0x435660 */
            if (!shop || (T->in->shop0 != shop && T->in->shop1 != shop)) continue;
            for (int k = 0; k < T->ne; k++) {
                WaCtrl *c = &g.ent[T->e0 + k].c;
                if (!g.ent[T->e0 + k].has_ctrl) continue;
                if (!(fabsf(b) < 1e-6f)) { c->state = 1; c->speed = c->len / b; }
                else {
                    c->state = 2;
                    if (mode == 1) { c->time = c->len; c->dir = 1; }
                    else { c->time = 0; c->dir = 0; }
                }
                g.ent[T->e0 + k].pending = 1; n++;
            }
        } else if (msg == 2) {                                      /* GenericNIS 0x435750 */
            for (int k = 0; k < T->ne; k++) {
                WaEnt *e = &g.ent[T->e0 + k];
                if (e->d->name != target || !e->has_ctrl) continue;
                WaCtrl *c = &e->c;
                if (!(fabsf(b) < 1e-6f)) {
                    c->rate = 1.0f; c->flags &= ~0x180u; c->state = 1;
                    c->speed = c->len / b;
                } else if (mode == 1) { c->time = c->len; c->state = 2; c->dir = 1; }
                else if (mode == 0) { c->time = 0; c->state = 2; c->dir = 0; }
                else c->time = c->len * a;
                e->pending = 1; n++;
                if (wa_test(NULL)) {                         /* test aid */
                    g.watch = target;
                    float pos[3]; wa_entity_pos(e->d->name, pos);
                    printf("world anims: %08x at frame %.1f of %.1f, (%.0f %.0f %.0f)\n", e->d->name, c->time, c->len,
                           pos[0], pos[1], pos[2]);
                }
            }
        }
    }
    printf("world anims: %s %08x (%.2f, %.2f) -> %d entities\n", msg == 1 ? "DOOR_POSITION" : "ANIM_POSITION",
           msg == 1 ? shop : target, a, b, n);
}

/* ---- RenderWorldModels -------------------------------------------------------- */
static GLuint tex_of(uint32_t key, unsigned char *mode)
{
    *mode = N2_DRAW_OPAQUE;
    if (!key) return 0;
    GLuint t = world_texture_lookup(key, mode);
    if (t) return t;
#ifdef __vita__
    /* not a world mesh's texture: decode it from the pinned bundle's TPK
       (pixels read back from the file), never pinning one here */
    long len = 0;
    unsigned char *d = res_bundle_file_len() > 0 ? res_bundle(g.pin, &len) : NULL;
    if (!d) return 0;
    N2Tpk tp = n2_tpk_open(d, len);
    N2Tex tx; memset(&tx, 0, sizeof tx);
    if (n2_tpk_decode(d, len, tp, key, &tx) && tx.rgb) {
        t = upload_tex(&tx);
        *mode = (unsigned char)n2_tex_mode(&tx);
    }
    free(tx.rgb); free(tx.alpha); free(tx.dxt); free(tp.blk);
#endif
    return t;
}
static int mdl_upload(WaModel *m)
{
    if (m->state == 2) return 1;
    if (m->state != 1) return 0;
    m->n = m->sc.count;
    m->gm = upload_scene(&m->sc);
    m->tex = (GLuint *)calloc((size_t)m->n + 1, sizeof *m->tex);
    m->mode = (unsigned char *)calloc((size_t)m->n + 1, 1);
    if (!m->gm || !m->tex || !m->mode) { m->state = -1; return 0; }
    int miss = 0;
    for (int i = 0; i < m->n; i++) {
        m->tex[i] = tex_of(m->sc.meshes[i].texkey, &m->mode[i]);
        miss += m->sc.meshes[i].texkey && !m->tex[i];
        if (wa_test(NULL))                                   /* test aid */
            printf("world anims: solid %08x mesh %d: texture %08x mode %d, %d vertices%s\n", m->hash, i,
                   m->sc.meshes[i].texkey, m->mode[i], m->sc.meshes[i].nverts, m->sc.meshes[i].vcol ? ", vertex colours" : "");
    }
    printf("world anims: solid %08x uploaded, %d meshes, %d textures missing\n", m->hash, m->n, miss);
    n2_free_scene(&m->sc);
    m->state = 2;
    return 1;
}
void wa_draw(const RProg *rp, const float MVP[16], const float (*planes)[4])
{
    int budget = 2, cur_blend = 0, cur_cut = 0;
    int test = wa_test(&g.watch);
    GLuint lasttex = (GLuint)-1;
    /* the main view's frustum (rows w +- x / y / z of the MVP; the sim's
       caller passes none, so it is taken from the MVP the same way) */
    for (int p = 0; p < 6; p++)
        for (int c = 0; c < 4; c++) g.planes[p][c] = MVP[c * 4 + 3] + (p & 1 ? -1.0f : 1.0f) * MVP[c * 4 + (p >> 1)];
    g.have_planes = 1;
    planes = (const float (*)[4])g.planes;
    /* census (11-15, PERF_PLAN P3): hwlog61 world anims 1.15-1.4 ms/frame */
    static Uint64 t_all; static long c_ent, c_drawn, c_frames;
    Uint64 tq = SDL_GetPerformanceCounter();
    int chain = 1;
    if (g.nent > g_wc_cap) {
        free(g_wc); free(g_wcs);
        g_wc = malloc((size_t)g.nent * sizeof *g_wc); g_wcs = calloc((size_t)g.nent, sizeof *g_wcs);
        g_wc_cap = g_wc && g_wcs ? g.nent : 0;
    }
    if (g_wc_cap < g.nent) chain = 0;
    if (++g_wframe == 0) { g_wframe = 1; if (g_wcs) memset(g_wcs, 0, (size_t)g_wc_cap * sizeof *g_wcs); }
    for (int i = 0; i < g.nent; i++) {
        WaEnt *e = &g.ent[i];
        if (e->model < 0) continue;
        WaModel *m = &g.mdl[e->model];
        if (m->state <= 0) continue;
        c_ent++;
        /* RenderWorldModels 0x61d4b0 -> WorldModel::Render 0x61d150 (GC
           8024cdf0): eView::GetPixelSize(node position, 35 m 0x7a6794) 0x48acb0
           >= 2, then GetVisibleState(model box, node matrix) 0x48f6f0. The
           pose itself was gated in wa_update (UpdateTimeStep); here it is
           only brought up to date (SpaceNode::Update 0x60bb10). The
           camera-inside-box skip (WorldModel +0x18) is never armed: the ctor
           (GC 8024ca1c) stores 0 and nothing else writes it for these. */
        float W[16];
        if (chain) ent_world_frame(i, W);
        else { if (e->pending) pose_update(e); ent_world(e, W); }
        float lo[3], hi[3];
        for (int k = 0; k < 3; k++) { lo[k] = W[12 + k]; hi[k] = W[12 + k]; }
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++) {
                float a = W[r * 4 + k] * m->lo[r], b = W[r * 4 + k] * m->hi[r];
                lo[k] += a < b ? a : b; hi[k] += a < b ? b : a;
            }
        if (g.watch && e->d->name == g.watch && g.watch_n++ % 30 == 0) {   /* test aid: where it is on screen */
            float c[4];
            for (int r = 0; r < 4; r++) c[r] = MVP[r] * W[12] + MVP[4 + r] * W[13] + MVP[8 + r] * W[14] + MVP[12 + r];
            printf("world anims: %08x on screen (%.2f, %.2f) w %.0f, box %s\n", e->d->name, c[3] > 0 ? c[0] / c[3] : 9.0f,
                   c[3] > 0 ? c[1] / c[3] : 9.0f, c[3], box_in_view(planes, lo, hi) ? "in view" : "out");
        }
        {   /* GetPixelSize 0x48acb0: 0 when the point is more than r behind the
               camera (depth along the view's forward row < -r); else
               view+0xc x r / (|d| - r), or 2 x view+0xc within 1.5 r. The size
               falls under 2 px only past r + 17.5 x view+0xc (about 10 km for
               the port's focal length in pixels), beyond any draw distance,
               so only the behind-the-camera case can cull. Depth = clip w. */
            float wd = MVP[3] * W[12] + MVP[7] * W[13] + MVP[11] * W[14] + MVP[15];
            if (wd < -35.0f) continue;
        }
        if (!box_in_view(planes, lo, hi)) continue;
        if (!e->seen && test) {                                             /* test aid */
            e->seen = 1;
            printf("world anims: %08x (solid %08x) in view at (%.0f %.0f %.0f), frame %.1f\n", e->d->name, e->d->solid,
                   W[12], W[13], W[14], e->c.time);
        }
        if (m->state == 1) {
            if (budget-- <= 0) continue;
            if (!mdl_upload(m)) continue;
        }
        /* WorldModel::Render 0x61d150: a light context (elSetupLightContext
           0x48f600, rig 0x7f4068) only when WorldModel +0x1c = eModel::IsLit
           (GC 800c5aa8: eSolid +0xe & 0x40); otherwise 0x5c5930 draws the solid
           with none, prelit like the world. No world-anim solid has the bit
           (STREAML4RA: 14 of the 20 anim solids found, all +0xe = 0; the 0x40
           solids are XS_ props), so the world pass's prelit state is right. */
        c_drawn++;
        float M2[16];
        mat_mul(MVP, W, M2);
        glUniformMatrix4fv(rp->uMVP, 1, GL_FALSE, M2);
        render_model(rp, W);
        for (int k = 0; k < m->n; k++) {
            int md = m->mode[k], blend = md == N2_DRAW_BLEND || md == N2_DRAW_ADD;
            if (m->tex[k] != lasttex) {
                if (m->tex[k]) { glUniform1f(rp->uUseTex, 1.0f); glBindTexture(GL_TEXTURE_2D, m->tex[k]); }
                else { glUniform1f(rp->uUseTex, 0.0f); glUniform3f(rp->uColor, 0.28f, 0.29f, 0.31f); }
                lasttex = m->tex[k];
            }
            int cut = md == N2_DRAW_CUTOUT;
            if (cut != cur_cut) { glUniform1f(rp->uAlphaTest, cut ? 1.0f : 0.0f); cur_cut = cut; }
            if (blend != cur_blend) {
                if (blend) { glEnable(GL_BLEND); glDepthMask(GL_FALSE); glUniform1f(rp->uTextureAlpha, 1.0f); }
                else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); glUniform1f(rp->uTextureAlpha, 0.0f); }
                cur_blend = blend;
            }
            if (blend) glBlendFunc(GL_SRC_ALPHA, md == N2_DRAW_ADD ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            draw_gpumesh(&m->gm[k]);
        }
        glUniformMatrix4fv(rp->uMVP, 1, GL_FALSE, MVP);
        render_model(rp, NULL);
    }
    t_all += SDL_GetPerformanceCounter() - tq;
    if (++c_frames >= 600) {
        double f = 1000.0 / (double)SDL_GetPerformanceFrequency() / (double)c_frames;
        printf("world anims draw over %ld frames: %.3f ms/frame; %.1f entities, %.1f drawn%s\n",
               c_frames, (double)t_all * f, (double)c_ent / c_frames, (double)c_drawn / c_frames,
               chain ? "" : " (no chain cache)");
        t_all = 0; c_ent = c_drawn = c_frames = 0;
    }
    if (cur_cut) glUniform1f(rp->uAlphaTest, 0.0f);
    if (cur_blend) {
        glDisable(GL_BLEND); glDepthMask(GL_TRUE); glUniform1f(rp->uTextureAlpha, 0.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
