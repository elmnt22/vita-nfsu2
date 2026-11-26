/* rstream.c -- retail TrackStreamer spike (11-23). See rstream.h.
 *
 * What this ports, by spec section (E:/re-tools/out/rstream/RETAIL_STREAMER_SPEC.md):
 *  1.2/1.3  the section table (L4RA.BUN 0x34110) and the VisibleSectionManager
 *           (0x80034150: boundaries 0x34152, drivable lists 0x34153, loading
 *           sections 0x34155), FindDrivableSection with move-to-front.
 *  1.4      DetermineStreamingSections: Y0 + X0 + Z0 + the zone's list (a
 *           LoadingSection naming the zone overrides it with GetSectionsToLoad).
 *           Deviation: the zone 1.5 s ahead (max 100 m) is added as well, to keep
 *           pop-in no worse than the tiles' read-ahead.
 *  2.2      load order Y -> X -> the rest by priority band (75 m bands of the
 *           distance to the section); PC frees a section as soon as it leaves
 *           the wanted list (X/Y stay).
 *  4        activation: a solid list becomes per-solid meshes (the port's own
 *           prototype parser, winst_solid_meshes, so materials, filler skips and
 *           vertex repair match the tile build exactly); scenery headers are
 *           registered under their scenery section number.
 *  5.2/5.3  WhatSectionsShouldWeDraw, TreeCull (states 0/1/2), DrawAScenery
 *           (precull, view exclusion, frustum, pixel size, LOD choice).
 *  9.1-9.3  H = 256 / tan(fov/2); the LOD-0 density test; per-mesh frustum test
 *           unless the instance is wholly inside; render state from the
 *           texture record (alpha test, blend type, no depth write when blended)
 *           and the bucket order (opaque + alpha test first, then blended rows
 *           by sort class).
 * Not ported (spike): the mirror / reflection / env-map views, scenery groups
 * and overrides (0x34105/0x34107), type-0xc prediction zones, track-specific
 * Z sections, light flares from position markers, wind sway, two-player.
 *
 * Threads: one worker reads and parses sections and decodes their textures;
 * the main thread uploads (time-budgeted), activates and draws. Everything the
 * two share sits behind g_mx. */
#include "rstream.h"
#include "world.h"
#include "world_instance.h"
#include "resource.h"
#include "nfsu2.h"
#include "smokeable.h"
#include "world_scenery.h"
#include "xpcity.h"
#include "xpconv.h"
#include "world_tiles.h"
#include <strings.h>
#include "SDL.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__

int render_world_dxt_enabled(void);
/* the Vita SDL shim has threads but no mutexes: a spin lock (as world_tiles.c) */
typedef volatile int RsLock;
static void rs_lock(RsLock *l) { while (__sync_lock_test_and_set(l, 1)) SDL_Delay(0); }
static void rs_unlock(RsLock *l) { __sync_lock_release(l); }
void vita_set_reflect(int on);

/* ---------------------------------------------------------------- switch */
static int g_rs_on = -1;
static int g_rs_precull_always;   /* "precull" in the switch file: no speed gate (test option) */
int rstream_enabled(void) {
    if (g_rs_on < 0) {
        FILE *f = fopen("ux0:data/NFSU2/retailstream.txt", "r");
        char opt[128] = "";
        if (f) { size_t n = fread(opt, 1, sizeof opt - 1, f); opt[n] = 0; fclose(f); }
        const char *e = getenv("OPENUG2_RSTREAM");
        if (e && !*opt) snprintf(opt, sizeof opt, "%s", e);
        /* on by default (11-24): the city draws from the game data, no tile
           cache. retailstream.txt "off" (or "tiles") goes back to the tiles
           (they are converted again on the next boot) */
        g_rs_on = !(strstr(opt, "off") || strstr(opt, "tiles") || (e && !strcmp(e, "0")));
        g_rs_precull_always = strstr(opt, "precull") != NULL;
        printf("rstream: retail streamer %s%s\n", g_rs_on ? "ON" : "off (retailstream.txt)",
               g_rs_on && g_rs_precull_always ? ", precull at every speed" : "");
    }
    return g_rs_on;
}
static const char *rs_base(const char *t) {
    const char *b = strrchr(t, '/'); b = b ? b + 1 : t;
    const char *c = strrchr(b, '\\'); return c ? c + 1 : b;
}
int rstream_track_ok(const char *trackname) {
    if (!trackname || !rstream_enabled()) return 0;
    const char *b = rs_base(trackname);
    /* every UG2 track set (the city; 11-25 the drag strips L4RB/L4RD, drift venues L4RC,
       Street X L4RF, URL L4RG), and the expansion cities from their raw files (xpcity_lean) */
    return (!strncmp(b, "STREAML4R", 9) && b[9] >= 'A' && b[9] <= 'G') || xpcity_lean(b);
}

/* ---------------------------------------------------------------- helpers */
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }
static float rdf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }

/* fixed-size records after a 0x11 filler run (winst_fixed_records' rule) */
static const unsigned char *rs_records(const unsigned char *p, long len, int stride, int *count) {
    long f = 0;
    if (len % stride) {
        while (f < len && p[f] == 0x11) f++;
        if ((len - f) % stride) f &= ~3L;
    }
    if (len - f < 0 || (len - f) % stride) { *count = 0; return NULL; }
    *count = (int)((len - f) / stride);
    return p + f;
}

/* ---------------------------------------------------------------- data */
#define RS_NUM 2800            /* section numbers: letter*100 + n, up to Z99 */
#define RS_MAXV 65535

typedef struct RsSec RsSec;
typedef struct RsSolid RsSolid;

typedef struct {
    N2Batch b;                 /* vbo/ibo/ioff/index_count (+tex at draw) */
    float lo[3], hi[3];        /* model-space bounds */
    int tex;                   /* slot in g_tex, -1 none */
    int page;                  /* build: the vertex page; GPU names after upload */
    unsigned char reflect, mat_exact, glow;
    int nv, vb;                /* vertices, first vertex in its page */
} RsMesh;

struct RsSolid {
    uint32_t hash; int polys; float volume, density;
    int m0, nm;                /* meshes of the owning section */
    RsSec *sec;
    RsSolid *next;             /* hash chain (main thread) */
    RsSolid *xnext;            /* the worker's X-section chain (bake) */
};

/* 0x34103 instance / 0x34104 tree node, parsed once on the worker (the raw
   records are unaligned little-endian bytes: reading them per frame was most
   of the 2-3 ms cull on the Vita, hwlog84) */
typedef struct {
    float lo[3], hi[3];        /* world box */
    float pos[3];              /* +0x20 */
    int pre;                   /* +0x1c precull row, -1 none */
    short info;                /* +0x18 */
    unsigned short flags;      /* +0x1a */
    short rot[9];              /* +0x2c, /8192 */
} RsInst;
typedef struct { float lo[3], hi[3]; short n, ch[5]; } RsNode;
typedef struct {
    int number;
    int ninfo, ninst, nnode, npre;
    unsigned char *info, *inst, *node, *pre;   /* raw records, copied out */
    RsInst *in; RsNode *nd;                    /* parsed */
    float *infR;                               /* info +0x3c bounding radius */
    unsigned char *hide; unsigned hide_gen;    /* group/prop hidden per instance, by g_sel_gen */
    RsSolid *(*model)[3]; unsigned gen;         /* resolved per info, by g_gen */
    RsSec *sec;
    /* world-space bake (worker): every instance whose models were in (its own
       section or an X library) has its LOD A and B meshes copied, transformed,
       into the section's bake pages in piece order, so the instances the
       per-frame culls keep draw as a few merged index ranges */
    unsigned char *bk;                         /* per instance: 1 baked */
    RsSolid *(*bm)[2];                         /* per info: the baked LOD A / B models */
    struct RsPiece *pc; int npc;
    unsigned char *vis;                        /* per frame: 0, or the LOD drawn + 1 */
    int touched;
} RsScen;
typedef struct RsPiece {
    const RsMesh *src;         /* texture / mode / reflect / glow */
    unsigned ioff, count;      /* index bytes offset, index count in the bake page */
    unsigned short inst; unsigned char lod, page;
} RsPiece;

typedef struct { BatchedVertex *v; int nv, cv; uint16_t *ix; int ni, ci; GLuint vbo, ibo; } RsPage;

struct RsSec {
    int number; char name[8];
    long off, size; float cx, cy, rad; int base_prio;
    int state;                 /* 0 out, 1 queued, 2 building, 3 built (CPU), 4 active */
    int wanted;                /* wanted by the current list */
    int prio;
    int cancel;
    RsPage *page; int npage;
    RsMesh *mesh; int nmesh;
    RsSolid *sol; int nsol;
    RsScen *scen; int nscen;
    RsPage *bpage; int nbpage; /* bake pages */
    long bake_v;               /* baked vertices (stats) */
    long unbaked;              /* instances left on the per-instance path */
    int *texs; int ntex;       /* g_tex slots this section's meshes use */
    Uint32 t_queued, t_built;
    long bytes_gpu;
};

typedef struct { int number, npts; float lo[2], hi[2]; float *pts; } RsBound;
typedef struct { int number, n; int16_t v[72]; } RsDriv;
typedef struct { int nd, ne; int16_t d[16], e[8]; } RsLoad;

/* texture table: fixed open addressing, never rehashed (slots are stable) */
#define RS_TEXCAP 8192
/* the worker stops decoding while this much decoded pixel data waits for its
   upload: the main thread only uploads while the world draws, so a cold start
   (X0's 846 textures under the loading screen) piled ~40 MB into the heap
   (hwlog84: heap 114 of 115 MB, out-of-memory failures elsewhere) */
#define RS_DEC_CAP (3L << 20)
typedef struct {
    uint32_t key;
    int state;                 /* 0 empty, 1 queued, 2 decoded (CPU), 3 bound, 4 failed */
    GLuint tex; unsigned char mode, order;
    N2Tex t;
} RsTex;

static struct {
    char troot[512], stream[640], comp[640];
    int active;
    char stem[48];             /* L4RA, L2RA, L5RA */
    int xp;                    /* XP_GAME_MW: a raw MW stream (sections translated as read, xp_section) */
    RsSec *sec; int nsec;
    short secof[RS_NUM];       /* number -> g.sec index, -1 */
    RsBound *bnd; int nbnd;    /* drivable boundaries (move-to-front order) */
    RsDriv *drv[RS_NUM];       /* drivable visible lists by number */
    RsLoad *ld; int nld;
    int lod_offset;
    /* scenery groups (0x34107/0x34108 in the companion): the tile build's rule,
       world_scenery.h wg_selection_visible, for the current event and career stage */
    unsigned char *compdata; long complen; WGTable wgt; int wgt_ok;
    WGSelection sel; int sel_on, sel_key[3];
    /* textures */
    unsigned char *pin; long pinlen; N2Tpk tpk;
    int cold;                  /* started, its first area not in yet (rstream_cold) */
    Uint32 cold_t0, cold_last; /* the cold wait's start, restarted after a gap in the updates:
                                  a front-end boot draws the world one frame, then the menus
                                  run without it (sim: given up at once after them) */
    unsigned pin_gen;          /* res_pin_gen() when g.pin was taken: the pin is borrowed,
                                  a re-pin frees it (hwlog86: worker read a freed copy) */
    uint32_t *keys; int nkeys;
    /* the tile builder's other texture sources, in its key order (world_instance.c
       library: bundle, LOC4DYNTEX, InGameCommon, regional ads): a material slot is
       "exact" (its authored blend/alpha mode applies) only when its key is in one */
    unsigned char *loc4; long loc4len;
    unsigned char *common; long commonlen; N2Tpk commontpk;
    unsigned char *ads; long adslen; N2Tpk adstpk; int nads;
    /* streaming position */
    float pos[3], vel[3]; Uint32 t_pos; int have_pos;
    int zone, pzone;
    /* worker */
    SDL_Thread *thr; int quit;
    /* stats */
    long st_draws, st_inst, st_frames, st_cull[8], st_tex_up, st_sec_up, st_unload;
    long st_binst, st_bruns, st_bpieces;
    /* decoded pixels waiting for the main thread's upload (state 2) */
    volatile long dec_bytes; volatile int ndec; long dec_peak; long st_dec_wait_ms;
    long tex_gpu;              /* uploaded texture bytes (decoded size: DXT exact, RGB(A) close) */
    double st_up_ms, st_draw_ms, st_upd_ms;
    Uint32 st_t0;
} g;
static RsTex g_tex[RS_TEXCAP];
static RsSolid *g_solhash[16384];
static RsScen *g_hdr[RS_NUM];
static unsigned g_gen = 1;
static unsigned g_sel_gen = 1;   /* bumped when the scenery-group selection changes */
static RsLock g_mx;

static int rs_letter(int number) { return number / 100; }   /* 1 = A ... 24 = X, 25 = Y, 26 = Z */

/* ---------------------------------------------------------------- table load */
typedef struct { const unsigned char *d; } RsWalk;
static int rs_comp_visit(const unsigned char *d, uint32_t tag, long beg, long end, void *ctx) {
    (void)ctx;
    long len = end - beg; const unsigned char *p = d + beg;
    if (tag == 0x00034110u) {
        /* MW (0x5c B): one more size word at +0x20, the rest 4 bytes on */
        const int st = g.xp ? 0x5c : 0x50, sh = g.xp ? 4 : 0;
        int n; const unsigned char *r = rs_records(p, len, st, &n);
        if (!r) { printf("rstream: section table size %ld not records\n", len); return 0; }
        g.sec = (RsSec *)calloc((size_t)n, sizeof *g.sec);
        if (!g.sec) return 0;
        for (int i = 0; i < n; i++) {
            const unsigned char *q = r + i * st;
            RsSec *s = &g.sec[g.nsec];
            s->number = rd16(q + 8);
            if (s->number <= 0 || s->number >= RS_NUM || rd32(q + 0x10) != 1) continue;
            memcpy(s->name, q, 7); s->name[7] = 0;
            s->off = (long)rd32(q + 0x14); s->size = (long)rd32(q + 0x18);
            s->base_prio = (int)rd32(q + 0x20 + sh);
            s->cx = rdf(q + 0x24 + sh); s->cy = rdf(q + 0x28 + sh); s->rad = rdf(q + 0x2c + sh);
            g.secof[s->number] = (short)g.nsec;
            g.nsec++;
        }
        return 0;
    }
    if (tag == 0x00034151u) {
        long q = 0; while (q < len && p[q] == 0x11) q++;
        q &= ~3L;
        if (len - q >= 4) g.lod_offset = (int)rd32(p + q);
        return 0;
    }
    if (tag == 0x00034152u) {
        long q = 0; while (q < len && p[q] == 0x11) q++;
        q &= ~3L;
        int cap = 512; g.bnd = (RsBound *)calloc((size_t)cap, sizeof *g.bnd);
        while (g.bnd && q + 0x24 <= len) {
            int num = rd16(p + q + 8), np = rd16(p + q + 0xa);
            if (np < 0 || np > 1024 || q + 0x24 + 8L * np > len) break;   /* MW: up to 260 points */
            int L = num / 100, n = num % 100;
            if (g.nbnd == cap) {   /* Palmont: ~640 (512 dropped its northern zones: nothing drawn there) */
                RsBound *nb = (RsBound *)realloc(g.bnd, (size_t)cap * 2 * sizeof *g.bnd);
                if (!nb) break;
                memset(nb + cap, 0, (size_t)cap * sizeof *nb);
                g.bnd = nb; cap *= 2;
            }
            if (L >= 1 && L <= 20 && n >= 1 && n < (g.lod_offset ? g.lod_offset : 20)) {
                RsBound *b = &g.bnd[g.nbnd];
                b->number = num; b->npts = np;
                b->lo[0] = rdf(p + q + 0xc); b->lo[1] = rdf(p + q + 0x10);
                b->hi[0] = rdf(p + q + 0x14); b->hi[1] = rdf(p + q + 0x18);
                b->pts = (float *)malloc((size_t)np * 8);
                if (b->pts) { memcpy(b->pts, p + q + 0x24, (size_t)np * 8); g.nbnd++; }
            }
            q += 0x24 + 8L * np;
        }
        return 0;
    }
    if (tag == 0x00034153u && g.xp) {
        /* MW: variable records, +0xc s16 section, +0xe u8 count, +0xf u8 capacity,
           +0x12 the list, each padded to 4 bytes (wcoll.c's xp reader) */
        long q = 0; while (q < len && p[q] == 0x11) q++;
        q &= ~3L;
        while (q + 0x12 <= len) {
            int num = rd16(p + q + 0xc), cnt = p[q + 0xe], cap = p[q + 0xf];
            long step = (0x12 + 2L * cap + 3) & ~3L;
            if (cnt > cap || q + 0x12 + 2L * cnt > len) break;
            if (num > 0 && num < RS_NUM && cnt <= 72 && !g.drv[num]) {
                RsDriv *dv = (RsDriv *)calloc(1, sizeof *dv);
                if (dv) {
                    dv->number = num; dv->n = cnt;
                    for (int k = 0; k < cnt; k++) dv->v[k] = rd16(p + q + 0x12 + 2 * k);
                    g.drv[num] = dv;
                }
            }
            q += step;
        }
        return 0;
    }
    if (tag == 0x00034153u) {
        int n; const unsigned char *r = rs_records(p, len, 0xa4, &n);
        for (int i = 0; r && i < n; i++) {
            const unsigned char *q = r + i * 0xa4;
            int num = rd16(q + 8), cnt = (int)rd32(q + 0x10);
            if (num <= 0 || num >= RS_NUM || cnt < 0 || cnt > 72) continue;
            RsDriv *dv = (RsDriv *)calloc(1, sizeof *dv);
            if (!dv) continue;
            dv->number = num; dv->n = cnt;
            for (int k = 0; k < cnt; k++) dv->v[k] = rd16(q + 0x14 + 2 * k);
            g.drv[num] = dv;
        }
        return 0;
    }
    if (tag == 0x00034155u) {
        int n; const unsigned char *r = rs_records(p, len, 0x4c, &n);
        if (r && n > 0 && (g.ld = (RsLoad *)calloc((size_t)n, sizeof *g.ld))) {
            for (int i = 0; i < n; i++) {
                const unsigned char *q = r + i * 0x4c; RsLoad *l = &g.ld[g.nld];
                l->nd = rd16(q + 0x18); l->ne = rd16(q + 0x3a);
                if (l->nd < 0 || l->nd > 16 || l->ne < 0 || l->ne > 8) continue;
                for (int k = 0; k < l->nd; k++) l->d[k] = rd16(q + 0x1a + 2 * k);
                for (int k = 0; k < l->ne; k++) l->e[k] = rd16(q + 0x3c + 2 * k);
                g.nld++;
            }
        }
        return 0;
    }
    return (tag >> 28) == 8;   /* descend into containers */
}

/* ---------------------------------------------------------------- zones */
static int rs_inside(const RsBound *b, float x, float y) {
    if (x < b->lo[0] || x > b->hi[0] || y < b->lo[1] || y > b->hi[1]) return 0;
    int in = 0;
    for (int i = 0, j = b->npts - 1; i < b->npts; j = i++) {
        float xi = b->pts[i*2], yi = b->pts[i*2+1], xj = b->pts[j*2], yj = b->pts[j*2+1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
static int rs_find_zone(float x, float y) {
    for (int i = 0; i < g.nbnd; i++) {
        if (!rs_inside(&g.bnd[i], x, y)) continue;
        if (i) { RsBound t = g.bnd[i]; memmove(g.bnd + 1, g.bnd, (size_t)i * sizeof *g.bnd); g.bnd[0] = t; }
        return g.bnd[0].number;
    }
    return 0;
}
static const RsBound *rs_bound(int number) {
    for (int i = 0; i < g.nbnd; i++) if (g.bnd[i].number == number) return &g.bnd[i];
    return NULL;
}

/* ---------------------------------------------------------------- textures */
static int rs_tex_slot(uint32_t key, int insert) {
    uint32_t h = (key * 2654435761u) & (RS_TEXCAP - 1);
    for (int n = 0; n < RS_TEXCAP; n++, h = (h + 1) & (RS_TEXCAP - 1)) {
        if (g_tex[h].state && g_tex[h].key == key) return (int)h;
        if (!g_tex[h].state) {
            if (!insert) return -1;
            g_tex[h].key = key; g_tex[h].state = 1;
            return (int)h;
        }
    }
    return -1;
}
/* render state from the texture record: the port's mapping (n2_tex_mode), so a
   mesh draws exactly as in the tile build (spec 9.3 maps more blend types; kept
   to the tiles' rules for a like-for-like spike) */
static unsigned char rs_tex_mode(const N2Tex *t) { return (unsigned char)n2_tex_mode(t); }
static long rs_tex_bytes(const N2Tex *t) {
    long px = (long)t->w * (long)t->h;
    return (t->rgb ? px * 3 : 0) + (t->alpha ? px : 0) + (t->dxt ? t->dxtlen : 0);
}
/* worker (and the main thread once for a sky texture): decode one queued key
   from the pinned bundle. n2_tpk_keep_dxt / n2_tpk_skip_pixels are this
   file's statics, read more than once inside n2_tpk_decode: two decodes at
   once flipped them under each other and the DXT1 path wrote through a NULL
   rgb buffer (hwlog89_lean_R data abort in n2_dxt1). One decode at a time. */
static RsLock g_decmx;
static void rs_tex_decode_one(int slot);
static void rs_tex_decode(int slot) {
    rs_lock(&g_decmx);
    rs_tex_decode_one(slot);
    rs_unlock(&g_decmx);
}
static void rs_tex_decode_one(int slot) {
    RsTex *e = &g_tex[slot];
    N2Tex t; memset(&t, 0, sizeof t);
    if (g.pin && res_pin_gen() != g.pin_gen) {   /* stale pin: leave it queued, the main thread stops us */
        rs_lock(&g_mx); e->state = 1; rs_unlock(&g_mx);
        return;
    }
    int dxt = render_world_dxt_enabled();
    n2_tpk_keep_dxt = dxt; n2_tpk_skip_pixels = dxt;
    int ok = g.pin && n2_tpk_decode(g.pin, g.pinlen, g.tpk, e->key, &t);
    n2_tpk_skip_pixels = 0;
    int src = 0;
    if (!ok) {   /* the other sources, as world_texture_decode: whole pixels, no DXT plan */
        n2_tpk_keep_dxt = 0;
        if (g.loc4 && (ok = n2_load_car_tex_by_key(g.loc4, g.loc4len, e->key, &t))) {
            t.order = t.usage = t.blend = t.wz = 0;   /* LOC4's opaque world policy (world.c) */
            src = 1;
        }
        if (!ok && g.common && (ok = n2_tpk_decode(g.common, g.commonlen, g.commontpk, e->key, &t))) src = 2;
        if (!ok && g.ads && (ok = n2_tpk_decode(g.ads, g.adslen, g.adstpk, e->key, &t))) src = 3;
        if (ok && t.rgb) render_prehalve_tex(&t);
        goto done;
    }
    int w, h, o, b;
    if (ok && dxt && (!t.dxt || !t.dxtfmt || !render_dxt_base_plan(&t, &w, &h, &o, &b))) {
        free(t.rgb); free(t.alpha); free(t.dxt); memset(&t, 0, sizeof t);
        n2_tpk_keep_dxt = 0;
        ok = n2_tpk_decode(g.pin, g.pinlen, g.tpk, e->key, &t);
        if (ok && t.rgb) render_prehalve_tex(&t);
    }
done:
    n2_tpk_keep_dxt = 0;
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_RS_TEXDUMP=<key hex> logs that texture's decode */
        const char *e2 = getenv("OPENUG2_RS_TEXDUMP");
        if (e2 && (uint32_t)strtoul(e2, NULL, 16) == e->key) {
            long sum = 0, n = 0;
            static const unsigned char z8[8];
            long l1o = (long)(t.w / 4) * (t.h / 4) * (t.dxtfmt == 1 ? 8 : 16);   /* level 1 (the base level is never read) */
            const unsigned char *L1 = t.dxt && t.dxtlen >= l1o + 8 ? t.dxt + l1o : z8;
            if (t.rgb) for (long i = 0; i < (long)t.w * t.h * 3; i += 97) { sum += t.rgb[i]; n++; }
            printf("rstream texdump %08x: src %d ok %d %dx%d dxt %p fmt %d rgb %p (mean %ld) bytes %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   e->key, src, ok, t.w, t.h, (void *)t.dxt, t.dxtfmt, (void *)t.rgb, n ? sum / n : -1,
                   L1[0], L1[1], L1[2], L1[3], L1[4], L1[5], L1[6], L1[7]);
        }
    }
#endif
    rs_lock(&g_mx);
    if (ok) {
        e->t = t; e->mode = rs_tex_mode(&t); e->order = t.order; e->state = 2;
        g.dec_bytes += rs_tex_bytes(&t); g.ndec++;
        if (g.dec_bytes > g.dec_peak) g.dec_peak = g.dec_bytes;
    } else e->state = 4;
    rs_unlock(&g_mx);
}
/* main: upload decoded textures within the budget */
static void rs_tex_upload(Uint64 deadline) {
    if (!g.ndec) return;   /* nothing decoded: no 8192-slot walk */
    for (int i = 0; i < RS_TEXCAP; i++) {
        if (g_tex[i].state != 2) continue;
        if (SDL_GetPerformanceCounter() > deadline) return;
        RsTex *e = &g_tex[i];
        GLuint id = render_world_dxt_enabled() && e->t.dxt && e->t.dxtfmt
                  ? upload_tpk_texture_to_gpu(&e->t) : (e->t.rgb ? upload_tex(&e->t) : 0);
        long nb = rs_tex_bytes(&e->t);
        free(e->t.rgb); free(e->t.alpha); free(e->t.dxt); memset(&e->t, 0, sizeof e->t);
        rs_lock(&g_mx);
        e->tex = id; e->state = id ? 3 : 4;
        if (id) g.tex_gpu += nb;
        g.dec_bytes -= nb; g.ndec--;
        rs_unlock(&g_mx);
        g.st_tex_up++;
    }
}

/* ---------------------------------------------------------------- build (worker) */
typedef struct { RsSec *s; const unsigned char *d; long dlen; int nsol_cap, nmesh_cap, nscen_cap; } RsBuild;

static int rs_page_room(RsSec *s, int nv) {
    RsPage *p = s->npage ? &s->page[s->npage - 1] : NULL;
    if (p && p->nv + nv <= RS_MAXV) return s->npage - 1;
    RsPage *np = (RsPage *)realloc(s->page, (size_t)(s->npage + 1) * sizeof *np);
    if (!np) return -1;
    s->page = np; memset(&np[s->npage], 0, sizeof *np);
    return s->npage++;
}

/* ---------------------------------------------------------------- sky
   SKYDOME (solid be43edbb: the dome and its cap) is in the X library and
   placed by a scenery instance (Z0 in L4RA), but the retail scenery draw
   never draws it (rs_hash_valid): it is the sky pass's. The tile build kept
   it in its track-wide tile; here it is kept aside at the build and placed
   once on the main thread (rstream_sky). */
#define RS_SKYMAX 8
typedef struct { uint32_t solid, texkey; int nv, ni; float *v; unsigned char *col; uint16_t *ix; } RsSkySrc;
static RsSkySrc g_skysrc[RS_SKYMAX]; static int g_nskysrc;   /* worker writes under g_mx */
static N2Batch g_skyb[RS_SKYMAX]; static int g_nskyb, g_sky_slot[RS_SKYMAX];
static int g_sky_state;   /* 0 not placed, 1 placed (batches made), 2 no instance found yet */
static void rs_sky_keep(uint32_t solid, const N2Mesh *m) {
    if (m->nverts <= 0 || m->nidx < 3 || m->nverts > RS_MAXV) return;
    rs_lock(&g_mx);
    int dup = 0;
    for (int i = 0; i < g_nskysrc; i++) dup |= g_skysrc[i].solid == solid && g_skysrc[i].texkey == m->texkey;
    if (!dup && g_nskysrc < RS_SKYMAX) {
        RsSkySrc *k = &g_skysrc[g_nskysrc];
        k->v = (float *)malloc((size_t)m->nverts * 5 * sizeof(float));
        k->col = m->vcol ? (unsigned char *)malloc((size_t)m->nverts * 4) : NULL;
        k->ix = (uint16_t *)malloc((size_t)m->nidx * sizeof(uint16_t));
        if (k->v && k->ix && (!m->vcol || k->col)) {
            memcpy(k->v, m->verts, (size_t)m->nverts * 5 * sizeof(float));
            if (k->col) memcpy(k->col, m->vcol, (size_t)m->nverts * 4);
            for (int t = 0; t < m->nidx; t++)
                k->ix[t] = (uint16_t)(m->idx[t] >= 0 && m->idx[t] < m->nverts ? m->idx[t] : 0);
            k->solid = solid; k->texkey = m->texkey; k->nv = m->nverts; k->ni = m->nidx;
            g_nskysrc++;
        } else { free(k->v); free(k->col); free(k->ix); memset(k, 0, sizeof *k); }
    }
    rs_unlock(&g_mx);
}
/* the smokeable props' models (a few dozen solids), kept as CPU meshes: the
   lean resident has no tile cache to take them from, and smokeable.c draws,
   hits and smashes them from the resident scene (role 3) */
typedef struct { uint32_t solid; N2Mesh m; } RsProp;
static RsProp *g_prop; static int g_nprop, g_cprop;
static volatile int g_prop_gen;
int rstream_prop_gen(void) { return g_prop_gen; }
static void rs_prop_keep(uint32_t solid, const N2Mesh *m) {
    if (m->nverts <= 0 || m->nidx < 3 || !smk_is_model(solid)) return;
    N2Mesh c = *m;
    c.shared_geometry = NULL; c.authored_normals = 0;
    c.verts = (float *)malloc((size_t)m->nverts * 5 * sizeof(float));
    c.idx = (uint16_t *)malloc((size_t)m->nidx * sizeof(uint16_t));
    c.vcol = m->vcol ? (unsigned char *)malloc((size_t)m->nverts * 4) : NULL;
    if (!c.verts || !c.idx || (m->vcol && !c.vcol)) { free(c.verts); free(c.idx); free(c.vcol); return; }
    memcpy(c.verts, m->verts, (size_t)m->nverts * 5 * sizeof(float));
    memcpy(c.idx, m->idx, (size_t)m->nidx * sizeof(uint16_t));
    if (c.vcol) memcpy(c.vcol, m->vcol, (size_t)m->nverts * 4);
    rs_lock(&g_mx);
    int dup = 0;   /* a re-read section: the same solid again */
    for (int i = 0; i < g_nprop && !dup; i++)
        dup = g_prop[i].solid == solid && g_prop[i].m.texkey == m->texkey &&
              g_prop[i].m.nverts == m->nverts && g_prop[i].m.nidx == m->nidx;
    if (!dup && g_nprop == g_cprop) {
        int cap = g_cprop ? g_cprop * 2 : 64;
        RsProp *n = (RsProp *)realloc(g_prop, (size_t)cap * sizeof *n);
        if (n) { g_prop = n; g_cprop = cap; }
    }
    if (!dup && g_nprop < g_cprop) { g_prop[g_nprop].solid = solid; g_prop[g_nprop].m = c; g_nprop++; g_prop_gen++; c.verts = NULL; c.idx = NULL; c.vcol = NULL; }
    rs_unlock(&g_mx);
    free(c.verts); free(c.idx); free(c.vcol);
}
static void rs_prop_free(void) {
    rs_lock(&g_mx);
    for (int i = 0; i < g_nprop; i++) { free(g_prop[i].m.verts); free(g_prop[i].m.idx); free(g_prop[i].m.vcol); }
    free(g_prop); g_prop = NULL; g_nprop = g_cprop = 0;
    rs_unlock(&g_mx);
}
int rstream_prop_place(uint32_t model, const float M[16], N2Scene *dst) {
    int n = 0;
    rs_lock(&g_mx);
    for (int i = 0; i < g_nprop; i++)
        if (g_prop[i].solid == model && winst_place_mesh(dst, &g_prop[i].m, M, g_prop[i].m.aname, NULL)) n++;
    rs_unlock(&g_mx);
    return n;
}
static void rs_sky_free(int sources) {
    for (int i = 0; i < g_nskyb; i++) {
        if (g_skyb[i].vbo) glDeleteBuffers(1, &g_skyb[i].vbo);
        if (g_skyb[i].ibo) glDeleteBuffers(1, &g_skyb[i].ibo);
    }
    memset(g_skyb, 0, sizeof g_skyb); g_nskyb = 0; g_sky_state = 0;
    if (!sources) return;
    for (int i = 0; i < g_nskysrc; i++) { free(g_skysrc[i].v); free(g_skysrc[i].col); free(g_skysrc[i].ix); }
    memset(g_skysrc, 0, sizeof g_skysrc); g_nskysrc = 0;
}

/* Palmont's sea (PAN_Ocean_Xenon, one placement, a plain move): cut open where the city's
   streets lie below it (world_tiles.c wt_sea_mask, the tile build's rule). Its placement
   is in the same section's scenery. */
static void rs_sea_cut(RsBuild *B, N2Scene *sc) {
    int want = 0;
    for (int i = 0; i < sc->count; i++) want |= !strcasecmp(sc->meshes[i].sname, "PAN_OCEAN_XENON");
    if (!want) return;
    N2Leaf L[16]; int ni = 0, nn = 0; N2Leaf I[16];
    n2_find_leaves(B->d, 0, B->dlen, 0x34102u, L, &ni, 16);
    n2_find_leaves(B->d, 0, B->dlen, 0x34103u, I, &nn, 16);
    float ox = 0, oy = 0; int found = 0;
    for (int a = 0; a < ni && a < nn && !found; a++) {   /* each scenery pack: its infos, its instances */
        const unsigned char *ip = B->d + L[a].off; int is = (int)L[a].size, ipad = n2_skip_filler(ip, is);
        const unsigned char *np = B->d + I[a].off; int ns = (int)I[a].size, npad = n2_skip_filler(np, ns);
        int ninfo = (is - ipad) / 0x44, ninst = (ns - npad) / 0x40;
        for (int k = 0; k < ninst && !found; k++) {
            const unsigned char *r = np + npad + k * 0x40;
            int inf = (uint16_t)rd16(r + 24);
            if (inf >= ninfo) continue;
            char nm[25]; memcpy(nm, ip + ipad + inf * 0x44, 24); nm[24] = 0;
            if (strcasecmp(nm, "PAN_OCEAN_XENON")) continue;
            ox = rdf(r + 32); oy = rdf(r + 36); found = 1;
        }
    }
    if (!found) { printf("rstream: sea model without its placement in %s: not cut\n", B->s->name); return; }
    char trk[64]; snprintf(trk, sizeof trk, "STREAM%s", g.stem);
    for (int i = 0; i < sc->count; i++)
        if (!strcasecmp(sc->meshes[i].sname, "PAN_OCEAN_XENON")) world_tiles_sea_cut_placed(&sc->meshes[i], trk, ox, oy);
}
static void rs_build_solid(RsBuild *B, long beg, long end) {
    RsSec *s = B->s;
    N2Leaf h[2]; int nh = 0;
    n2_find_leaves(B->d, beg, end, 0x00134011u, h, &nh, 2);
    if (nh < 1) return;
    const unsigned char *hp = B->d + h[0].off; int hs = (int)h[0].size;
    int pad = n2_skip_filler(hp, hs);
    if (hs - pad < 0x9c) return;
    hp += pad;
    N2Scene sc; memset(&sc, 0, sizeof sc);
    winst_solid_meshes(B->d, beg, end, g.keys, g.nkeys, &sc);
    if (g.xp == XP_GAME_CARBON) rs_sea_cut(B, &sc);
    /* Palmont's backdrops, the tile build's rules (it draws neither):
       - PAN_* (the T8x/T9x panoramas, PAN_MUDriftA_Trn08 spans 12 km) and TRN_PANO_*:
         backdrop unless it measures as a ground sheet (the seas) -- world_instance.c,
         n2_walk_meshes; its vista tier is off;
       - SKYDOME_Xenon (Z0, solid ed85ba82): the high-detail dome. The sky pass takes the
         sky from SKYDOME (be43edbb) as the tile build does; the name rule (nfsu2.h: an
         uppercase run "SKYDOME" or "SKY_") misses "SKYDOME_Xenon", so it drew as ordinary
         scenery -- a flat fog-coloured slab over the canyons, 11-25 */
    if (g.xp == XP_GAME_CARBON && sc.count) {
        const char *nm0 = sc.meshes[0].sname;
        int drop = !strncasecmp(nm0, "SKYDOME", 7);
        for (int i = 0; i < sc.count && drop; i++) drop = sc.meshes[i].cat != N2_SKY;   /* not the sky's own source */
        if (!drop && (!strncasecmp(nm0, "PAN", 3) || !strncasecmp(nm0, "TRN_PANO", 8))) {
            float mtx[16]; N2Geom geo;
            n2_obj_matrix(B->d, beg, end, mtx);   /* identity when it has none */
            drop = !(n2_obj_geom(B->d, beg, end, mtx, &geo) && n2_is_ground_sheet(&geo));
        }
        if (drop) {
            static int logn;   /* the worker only */
            if (logn++ < 4) printf("rstream: backdrop %s in %s left out (the tile build's rule)%c", nm0, s->name, 10);
            winst_solid_free(&sc); memset(&sc, 0, sizeof sc);
        }
    }
    if (s->nsol == B->nsol_cap) {
        int c = B->nsol_cap ? B->nsol_cap * 2 : 256;
        RsSolid *n = (RsSolid *)realloc(s->sol, (size_t)c * sizeof *n);
        if (!n) { winst_solid_free(&sc); return; }
        s->sol = n; B->nsol_cap = c;
    }
    RsSolid *so = &s->sol[s->nsol++]; memset(so, 0, sizeof *so);
    so->hash = rd32(hp + 0x10); so->polys = rd16(hp + 0x14);
    so->volume = rdf(hp + 0x94); so->density = rdf(hp + 0x98);
    so->m0 = s->nmesh; so->sec = s;
    for (int i = 0; i < sc.count; i++) {
        const N2Mesh *m = &sc.meshes[i];
        if (m->cat == N2_SKY) { rs_sky_keep(so->hash, m); continue; }
        rs_prop_keep(so->hash, m);
        if (m->nverts <= 0 || m->nidx < 3 || m->nverts > RS_MAXV) continue;
        /* a material piece carries its solid's whole vertex pool (n2_add_pair
           takes an index sub-range): keep only the vertices its indices use
           (X0: 973k vertices for 468k indices before this) */
        int *remap = (int *)malloc((size_t)m->nverts * sizeof *remap);
        if (!remap) break;
        for (int v = 0; v < m->nverts; v++) remap[v] = -1;
        int used = 0;
        for (int t = 0; t < m->nidx; t++) {
            int v = m->idx[t];
            if (v >= 0 && v < m->nverts && remap[v] < 0) remap[v] = used++;
        }
        int pg = rs_page_room(s, used);
        if (pg < 0) { free(remap); break; }
        if (pg < 0) break;
        RsPage *p = &s->page[pg];
        if (p->nv + used > p->cv) {
            int c = p->cv ? p->cv : 4096; while (c < p->nv + used) c *= 2;
            if (c > RS_MAXV + 1) c = RS_MAXV + 1;
            BatchedVertex *nv = (BatchedVertex *)realloc(p->v, (size_t)c * sizeof *nv);
            if (!nv) { free(remap); break; }
            p->v = nv; p->cv = c;
        }
        if (p->ni + m->nidx > p->ci) {
            int c = p->ci ? p->ci : 8192; while (c < p->ni + m->nidx) c *= 2;
            uint16_t *ni = (uint16_t *)realloc(p->ix, (size_t)c * sizeof *ni);
            if (!ni) { free(remap); break; }
            p->ix = ni; p->ci = c;
        }
        if (s->nmesh == B->nmesh_cap) {
            int c = B->nmesh_cap ? B->nmesh_cap * 2 : 512;
            RsMesh *nm = (RsMesh *)realloc(s->mesh, (size_t)c * sizeof *nm);
            if (!nm) { free(remap); break; }
            s->mesh = nm; B->nmesh_cap = c;
        }
        float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
        if (!nor) { free(remap); break; }
        render_mesh_normals(m, nor);
        RsMesh *rm = &s->mesh[s->nmesh++]; memset(rm, 0, sizeof *rm);
        for (int c = 0; c < 3; c++) { rm->lo[c] = 1e30f; rm->hi[c] = -1e30f; }
        int vb = p->nv;
        for (int v = 0; v < m->nverts; v++) {
            if (remap[v] < 0) continue;
            BatchedVertex *o = &p->v[vb + remap[v]]; const float *q = m->verts + v * 5;
            o->pos[0] = q[0]; o->pos[1] = q[1]; o->pos[2] = q[2];
            o->uv[0] = q[3]; o->uv[1] = q[4];
            BV_SET_NORMAL(o, nor[v*3], nor[v*3+1], nor[v*3+2]);
            if (m->vcol) memcpy(o->col, m->vcol + v * 4, 4);
            else o->col[0] = o->col[1] = o->col[2] = o->col[3] = 255;
            for (int c = 0; c < 3; c++) { if (q[c] < rm->lo[c]) rm->lo[c] = q[c]; if (q[c] > rm->hi[c]) rm->hi[c] = q[c]; }
        }
        free(nor);
        rm->b.ioff = (unsigned)(p->ni * 2);
        for (int t = 0; t < m->nidx; t++) {
            int v = m->idx[t];
            p->ix[p->ni + t] = (uint16_t)(vb + (v >= 0 && v < m->nverts ? remap[v] : 0));
        }
        free(remap);
        rm->b.index_count = m->nidx; rm->nv = used; rm->vb = vb;
        rm->b.texkey = m->texkey;
        rm->b.nmesh = 1;
        p->nv += used; p->ni += m->nidx;
        rm->page = pg;
        rm->reflect = m->reflect; rm->mat_exact = m->mat_exact; rm->glow = m->cat == N2_GLOW;
        rm->tex = -1;
        if (m->texkey) {
            rs_lock(&g_mx);
            rm->tex = rs_tex_slot(m->texkey, 1);
            rs_unlock(&g_mx);
        }
    }
    so->nm = s->nmesh - so->m0;
    winst_solid_free(&sc);
}

static void rs_build_scenery(RsBuild *B, long beg, long end) {
    RsSec *s = B->s;
    N2Leaf L[8]; int n = 0;
    const unsigned char *hd = NULL; long hl = 0;
    const unsigned char *rec[7] = {0}; long rl[7] = {0};
    for (uint32_t id = 0x34101; id <= 0x34106; id++) {
        n = 0; n2_find_leaves(B->d, beg, end, id, L, &n, 1);
        if (n) { rec[id - 0x34100] = B->d + L[0].off; rl[id - 0x34100] = L[0].size; }
    }
    hd = rec[1]; hl = rl[1];
    if (!hd || !rec[2] || !rec[3]) return;
    long hp = 0; while (hp < hl && hd[hp] == 0x11) hp++;
    hp &= ~3L;
    if (hl - hp < 0x10) return;
    if (s->nscen == B->nscen_cap) {
        int c = B->nscen_cap ? B->nscen_cap * 2 : 4;
        RsScen *ns = (RsScen *)realloc(s->scen, (size_t)c * sizeof *ns);
        if (!ns) return;
        s->scen = ns; B->nscen_cap = c;
    }
    RsScen *sc = &s->scen[s->nscen]; memset(sc, 0, sizeof *sc);
    sc->number = (int)rd32(hd + hp + 0xc);
    sc->sec = s;
    static const int stride[7] = { 0, 0, 0x44, 0x40, 0x24, 0, 0x80 };
    unsigned char **dst[7] = { NULL, NULL, &sc->info, &sc->inst, &sc->node, NULL, &sc->pre };
    int *cnt[7] = { NULL, NULL, &sc->ninfo, &sc->ninst, &sc->nnode, NULL, &sc->npre };
    for (int k = 2; k <= 6; k++) {
        if (!stride[k] || !rec[k]) continue;
        int c; const unsigned char *r = rs_records(rec[k], rl[k], stride[k], &c);
        if (!r || c <= 0) continue;
        *dst[k] = (unsigned char *)malloc((size_t)c * stride[k]);
        if (!*dst[k]) continue;
        memcpy(*dst[k], r, (size_t)c * stride[k]); *cnt[k] = c;
    }
    if (!sc->info || !sc->inst || sc->number <= 0 || sc->number >= RS_NUM) {
        free(sc->info); free(sc->inst); free(sc->node); free(sc->pre); return;
    }
    sc->model = calloc((size_t)sc->ninfo, sizeof *sc->model);
    sc->in = (RsInst *)malloc((size_t)sc->ninst * sizeof *sc->in);
    sc->nd = sc->nnode ? (RsNode *)malloc((size_t)sc->nnode * sizeof *sc->nd) : NULL;
    sc->infR = (float *)malloc((size_t)sc->ninfo * sizeof *sc->infR);
    sc->hide = (unsigned char *)malloc((size_t)sc->ninst);
    if (!sc->model || !sc->in || (sc->nnode && !sc->nd) || !sc->infR || !sc->hide) {
        free(sc->info); free(sc->inst); free(sc->node); free(sc->pre);
        free(sc->model); free(sc->in); free(sc->nd); free(sc->infR); free(sc->hide); return;
    }
    for (int i = 0; i < sc->ninst; i++) {
        const unsigned char *r = sc->inst + i * 0x40; RsInst *o = &sc->in[i];
        for (int c = 0; c < 3; c++) { o->lo[c] = rdf(r + 4 * c); o->hi[c] = rdf(r + 12 + 4 * c); o->pos[c] = rdf(r + 0x20 + 4 * c); }
        o->info = rd16(r + 0x18); o->flags = (unsigned short)(r[0x1a] | r[0x1b] << 8);
        int pi = (int)rd32(r + 0x1c); o->pre = pi >= 0 && pi < sc->npre ? pi : -1;
        for (int k = 0; k < 9; k++) o->rot[k] = rd16(r + 0x2c + 2 * k);
    }
    for (int i = 0; i < sc->nnode; i++) {
        const unsigned char *r = sc->node + i * 0x24; RsNode *o = &sc->nd[i];
        for (int c = 0; c < 3; c++) { o->lo[c] = rdf(r + 4 * c); o->hi[c] = rdf(r + 12 + 4 * c); }
        o->n = rd16(r + 0x18); if (o->n > 5) o->n = 5; if (o->n < 0) o->n = 0;
        for (int k = 0; k < 5; k++) o->ch[k] = rd16(r + 0x1a + 2 * k);
    }
    for (int k = 0; k < sc->ninfo; k++) sc->infR[k] = rdf(sc->info + k * 0x44 + 0x3c);
    sc->hide_gen = 0;
    free(sc->node); sc->node = NULL;   /* parsed copy replaces it */
    s->nscen++;
}

static int rs_sec_visit(const unsigned char *d, uint32_t tag, long beg, long end, void *ctx) {
    (void)d;
    RsBuild *B = (RsBuild *)ctx;
    if (tag == 0x80134000u) return 1;
    if (tag == 0x80134010u) { rs_build_solid(B, beg, end); return 0; }
    if (tag == 0x80034100u) { rs_build_scenery(B, beg, end); return 0; }
    return 0;
}

static void rs_sec_free_cpu(RsSec *s) {
    for (int i = 0; i < s->npage; i++) { free(s->page[i].v); free(s->page[i].ix); s->page[i].v = NULL; s->page[i].ix = NULL; }
    for (int i = 0; i < s->nbpage; i++) { free(s->bpage[i].v); free(s->bpage[i].ix); s->bpage[i].v = NULL; s->bpage[i].ix = NULL; }
}
static void rs_sec_free_all(RsSec *s) {
    rs_sec_free_cpu(s);
    for (int i = 0; i < s->npage; i++) {
        if (s->page[i].vbo) glDeleteBuffers(1, &s->page[i].vbo);
        if (s->page[i].ibo) glDeleteBuffers(1, &s->page[i].ibo);
    }
    free(s->page); s->page = NULL; s->npage = 0;
    for (int i = 0; i < s->nbpage; i++) {
        if (s->bpage[i].vbo) glDeleteBuffers(1, &s->bpage[i].vbo);
        if (s->bpage[i].ibo) glDeleteBuffers(1, &s->bpage[i].ibo);
    }
    free(s->bpage); s->bpage = NULL; s->nbpage = 0; s->bake_v = 0; s->unbaked = 0;
    free(s->mesh); s->mesh = NULL; s->nmesh = 0;
    free(s->sol); s->sol = NULL; s->nsol = 0;
    for (int i = 0; i < s->nscen; i++) {
        RsScen *c = &s->scen[i];
        free(c->info); free(c->inst); free(c->node); free(c->pre); free(c->model);
        free(c->in); free(c->nd); free(c->infR); free(c->hide);
        free(c->bk); free(c->bm); free(c->pc); free(c->vis);
    }
    free(s->scen); s->scen = NULL; s->nscen = 0;
    free(s->texs); s->texs = NULL; s->ntex = 0;
    s->bytes_gpu = 0;
}

/* read() with the whole size at once: newlib's fread trickles through a small
   stdio buffer (~2 MB/s from the card, nfsu2.h n2_read_file) */
static int rs_read_at(int fd, long off, unsigned char *d, long n) {
    if (lseek(fd, off, SEEK_SET) != off) return 0;
    long got = 0;
    while (got < n) {
        long r = (long)read(fd, d + got, (size_t)(n - got));
        if (r <= 0) return 0;
        got += r;
    }
    return 1;
}
/* ---------------------------------------------------------------- bake (worker) */
/* X-library solids by hash, for the worker only (X sections never unload while
   the streamer runs; the main thread's g_solhash is not safe to read here) */
static RsSolid *g_xhash[4096];
static uint32_t rs_xh(uint32_t h) { return (h * 2654435761u) >> 20; }   /* 12 bits */
static void rs_xregister(RsSec *s) {
    for (int i = 0; i < s->nsol; i++) {
        RsSolid *so = &s->sol[i]; uint32_t b = rs_xh(so->hash);
        so->xnext = g_xhash[b]; g_xhash[b] = so;
    }
}
static int rs_hash_valid(uint32_t h) { return h && h != 0xbe43edbbu && h != 0x72923c81u; }
typedef struct { RsSec *s; int *head, *next; } RsOwn;
static RsSolid *rs_bfind(const RsOwn *o, uint32_t h) {
    for (int i = o->head[h & 1023]; i >= 0; i = o->next[i]) if (o->s->sol[i].hash == h) return &o->s->sol[i];
    for (RsSolid *so = g_xhash[rs_xh(h)]; so; so = so->xnext) if (so->hash == h) return so;
    return NULL;
}
static int rs_piece_key(const RsMesh *m) { return m->tex >= 0 ? m->tex : -1 - (int)(m->b.texkey & 0x3fffffff); }
static int rs_piece_cmp(const void *a, const void *b) {
    const RsPiece *x = (const RsPiece *)a, *y = (const RsPiece *)b;
    int kx = rs_piece_key(x->src), ky = rs_piece_key(y->src);
    if (kx != ky) return kx < ky ? -1 : 1;
    int fx = x->src->mat_exact | x->src->reflect << 1 | x->src->glow << 2, fy = y->src->mat_exact | y->src->reflect << 1 | y->src->glow << 2;
    if (fx != fy) return fx - fy;
    if (x->src->b.texkey != y->src->b.texkey) return x->src->b.texkey < y->src->b.texkey ? -1 : 1;
    if (x->lod != y->lod) return x->lod - y->lod;
    if (x->inst != y->inst) return x->inst - y->inst;
    return x->src < y->src ? -1 : x->src > y->src;
}
/* same draw state: may share one draw call */
static int rs_piece_same(const RsMesh *a, const RsMesh *b) {
    return a == b || (a->tex == b->tex && a->b.texkey == b->b.texkey && a->mat_exact == b->mat_exact &&
                      a->reflect == b->reflect && a->glow == b->glow);
}
static int rs_bpage_room(RsSec *s, int nv) {
    RsPage *p = s->nbpage ? &s->bpage[s->nbpage - 1] : NULL;
    if (p && p->nv + nv <= RS_MAXV && s->nbpage < 255) return s->nbpage - 1;
    if (s->nbpage >= 255) return -1;
    RsPage *np = (RsPage *)realloc(s->bpage, (size_t)(s->nbpage + 1) * sizeof *np);
    if (!np) return -1;
    s->bpage = np; memset(&np[s->nbpage], 0, sizeof *np);
    return s->nbpage++;
}
static int rs_bake_piece(RsSec *s, RsPiece *pc, const RsPage *sp, const RsInst *in) {
    const RsMesh *m = pc->src;
    if (!sp->v || !sp->ix || m->nv <= 0 || m->vb + m->nv > sp->nv) return 0;
    int pg = rs_bpage_room(s, m->nv);
    if (pg < 0) return 0;
    RsPage *p = &s->bpage[pg];
    int ni = (int)m->b.index_count, i0 = (int)(m->b.ioff / 2);
    if (p->nv + m->nv > p->cv) {
        int c = p->cv ? p->cv : 8192; while (c < p->nv + m->nv) c *= 2;
        if (c > RS_MAXV + 1) c = RS_MAXV + 1;
        BatchedVertex *nv = (BatchedVertex *)realloc(p->v, (size_t)c * sizeof *nv);
        if (!nv) return 0;
        p->v = nv; p->cv = c;
    }
    if (p->ni + ni > p->ci) {
        int c = p->ci ? p->ci : 16384; while (c < p->ni + ni) c *= 2;
        uint16_t *nx = (uint16_t *)realloc(p->ix, (size_t)c * sizeof *nx);
        if (!nx) return 0;
        p->ix = nx; p->ci = c;
    }
    /* the instance matrix, as rs_draw_instance builds it (column-major) */
    float M[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if (!(in->flags & 0x100)) {
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++) M[row * 4 + col] = (float)in->rot[row * 3 + col] * (1.0f / 8192.0f);
        M[12] = in->pos[0]; M[13] = in->pos[1]; M[14] = in->pos[2];
    }
    int vb = p->nv;
    for (int v = 0; v < m->nv; v++) {
        const BatchedVertex *a = &sp->v[m->vb + v]; BatchedVertex *o = &p->v[vb + v];
        *o = *a;
        float x = a->pos[0], y = a->pos[1], z = a->pos[2];
        o->pos[0] = M[0]*x + M[4]*y + M[8]*z + M[12];
        o->pos[1] = M[1]*x + M[5]*y + M[9]*z + M[13];
        o->pos[2] = M[2]*x + M[6]*y + M[10]*z + M[14];
#ifdef __vita__
        float nx = a->normal[0] * (1.0f / 127.0f), ny = a->normal[1] * (1.0f / 127.0f), nz = a->normal[2] * (1.0f / 127.0f);
#else
        float nx = a->normal[0], ny = a->normal[1], nz = a->normal[2];
#endif
        float wx = M[0]*nx + M[4]*ny + M[8]*nz, wy = M[1]*nx + M[5]*ny + M[9]*nz, wz = M[2]*nx + M[6]*ny + M[10]*nz;
        float l = sqrtf(wx*wx + wy*wy + wz*wz);
        if (l > 1e-8f) { wx /= l; wy /= l; wz /= l; }   /* the shaders normalise vN */
        BV_SET_NORMAL(o, wx, wy, wz);
    }
    for (int t = 0; t < ni; t++) {
        int q = (int)sp->ix[i0 + t] - m->vb;
        p->ix[p->ni + t] = (uint16_t)(vb + (q >= 0 && q < m->nv ? q : 0));
    }
    pc->page = (unsigned char)pg; pc->ioff = (unsigned)(p->ni * 2); pc->count = (unsigned)ni;
    p->nv += m->nv; p->ni += ni;
    s->bake_v += m->nv;
    return 1;
}
/* the section's scenery, baked; returns the instances left unbaked */
static long rs_bake(RsSec *s) {
    long left = 0;
    RsOwn O; O.s = s;
    int head[1024]; O.head = head; for (int i = 0; i < 1024; i++) head[i] = -1;
    O.next = (int *)malloc((size_t)(s->nsol ? s->nsol : 1) * sizeof(int));
    if (!O.next) { for (int i = 0; i < s->nscen; i++) left += s->scen[i].ninst; return left; }
    for (int i = 0; i < s->nsol; i++) { int b = (int)(s->sol[i].hash & 1023); O.next[i] = head[b]; head[b] = i; }
    for (int c = 0; c < s->nscen; c++) {
        RsScen *sc = &s->scen[c];
        sc->bk = (unsigned char *)calloc((size_t)sc->ninst, 1);
        sc->vis = (unsigned char *)calloc((size_t)sc->ninst, 1);
        sc->bm = calloc((size_t)sc->ninfo, sizeof *sc->bm);
        unsigned char *iok = (unsigned char *)malloc((size_t)sc->ninfo);
        if (!sc->bk || !sc->vis || !sc->bm || !iok || sc->ninst > 65535) {
            free(sc->bk); free(sc->vis); free(sc->bm); free(iok); sc->bk = sc->vis = NULL; sc->bm = NULL;
            left += sc->ninst; continue;
        }
        long np = 0;
        for (int k = 0; k < sc->ninfo; k++) {
            int ok = 1;
            for (int l = 0; l < 2; l++) {
                uint32_t h = rd32(sc->info + k * 0x44 + 0x20 + l * 4);
                sc->bm[k][l] = rs_hash_valid(h) ? rs_bfind(&O, h) : NULL;
                /* a LOD B nobody has (22 in L4RA) draws nothing, as on the old path */
                if (rs_hash_valid(h) && !sc->bm[k][l] && l == 0) ok = 0;
            }
            iok[k] = (unsigned char)ok;
        }
        for (int i = 0; i < sc->ninst; i++) {
            int info = sc->in[i].info;
            if (info < 0 || info >= sc->ninfo || !iok[info]) continue;
            for (int l = 0; l < 2; l++) if (sc->bm[info][l]) np += sc->bm[info][l]->nm;
        }
        sc->pc = np ? (RsPiece *)malloc((size_t)np * sizeof *sc->pc) : NULL;
        if (np && !sc->pc) { free(iok); left += sc->ninst; continue; }
        long n = 0;
        for (int i = 0; i < sc->ninst; i++) {
            int info = sc->in[i].info;
            if (info < 0 || info >= sc->ninfo || !iok[info]) continue;
            for (int l = 0; l < 2; l++) {
                const RsSolid *so = sc->bm[info][l];
                for (int k = 0; so && k < so->nm; k++) {
                    const RsMesh *m = &so->sec->mesh[so->m0 + k];
                    if (m->nv <= 0 || m->b.index_count < 3) continue;
                    RsPiece *pc = &sc->pc[n++]; pc->src = m; pc->inst = (unsigned short)i; pc->lod = (unsigned char)l;
                }
            }
        }
        qsort(sc->pc, (size_t)n, sizeof *sc->pc, rs_piece_cmp);
        /* write in piece order; an instance whose piece failed stays unbaked */
        long w = 0;
        for (long q = 0; q < n; q++) {
            RsPiece *pc = &sc->pc[q];
            const RsSolid *so = sc->bm[sc->in[pc->inst].info][pc->lod];
            if (!rs_bake_piece(s, pc, &so->sec->page[pc->src->page], &sc->in[pc->inst])) { sc->bk[pc->inst] = 2; continue; }
            sc->pc[w++] = *pc;
        }
        sc->npc = (int)w;
        for (int i = 0; i < sc->ninst; i++) {
            int info = sc->in[i].info;
            int ok = info >= 0 && info < sc->ninfo && iok[info] && sc->bk[i] != 2;
            sc->bk[i] = (unsigned char)ok;
            left += !ok;
        }
        free(iok);
    }
    free(O.next);
    return left;
}

static void rs_build(RsSec *s, int fd) {
    Uint32 t0 = SDL_GetTicks();
    if (rs_letter(s->number) == 25) return;   /* Y: texture packs only; textures come by key from the pinned bundle */
    unsigned char *d = (unsigned char *)malloc((size_t)s->size);
    if (!d) { printf("rstream: no memory for %s (%ld KB)\n", s->name, s->size / 1024); return; }
    if (!rs_read_at(fd, s->off, d, s->size)) {
        printf("rstream: read failed for %s\n", s->name); free(d); return;
    }
    long dlen = s->size;
    Uint32 tx = 0;
    if (g.xp) {   /* a raw MW section: its models + scenery in UG2's format (xpconv.c) */
        Uint32 x0 = SDL_GetTicks();
        unsigned char *u = NULL; size_t un = 0;
        int rc = xp_section(d, (size_t)s->size, g.xp, &u, &un);
        free(d);
        if (rc != XP_OK) { free(u); printf("rstream: %s: translation failed (rc %d)\n", s->name, rc); return; }
        if (!u) u = (unsigned char *)calloc(1, 8), un = 0;   /* no models or scenery (textures only): an empty section */
        if (!u) { printf("rstream: %s: no memory\n", s->name); return; }
        d = u; dlen = (long)un;
        tx = SDL_GetTicks() - x0;
    }
    Uint32 t1 = SDL_GetTicks();
    RsBuild B; memset(&B, 0, sizeof B); B.s = s; B.d = d; B.dlen = dlen;
    asset_chunks_walk(d, 0, dlen, rs_sec_visit, &B);
    free(d);
    long unbaked = 0;
    if (rs_letter(s->number) == 24) rs_xregister(s);   /* model library: before anyone bakes from it */
    else if (s->nscen) unbaked = rs_bake(s);
    s->unbaked = unbaked;
    /* the section's texture slots, distinct */
    int cap = s->nmesh ? s->nmesh : 1;
    s->texs = (int *)malloc((size_t)cap * sizeof *s->texs);
    for (int i = 0; s->texs && i < s->nmesh; i++) {
        int t = s->mesh[i].tex, dup = 0;
        if (t < 0) continue;
        for (int k = 0; k < s->ntex && !dup; k++) dup = s->texs[k] == t;
        if (!dup) s->texs[s->ntex++] = t;
    }
    /* decode the ones nobody has yet */
    for (int k = 0; k < s->ntex; k++) {
        int t = s->texs[k];
        rs_lock(&g_mx);
        int go = g_tex[t].state == 1;
        if (go) g_tex[t].state = 5;   /* decoding */
        rs_unlock(&g_mx);
        if (!go) continue;
        Uint32 w0 = SDL_GetTicks();
        while (g.dec_bytes > RS_DEC_CAP && !g.quit) SDL_Delay(4);   /* wait for uploads */
        g.st_dec_wait_ms += SDL_GetTicks() - w0;
        rs_tex_decode(t);
    }
    Uint32 t2 = SDL_GetTicks();
    long mv = 0; for (int i = 0; i < s->nmesh; i++) mv += s->mesh[i].nv;
    static int logn;
    if (logn++ < 400)
        printf("rstream: built %s (%ld KB): read %u ms%s, parse+tex+bake %u ms, %d solids %d meshes (%ld vertices) %d scenery, %d pages, %d textures, "
               "baked %ld vertices in %d pages, %ld instances unbaked\n",
               s->name, s->size / 1024, (unsigned)(t1 - t0 - tx), g.xp ? (tx ? " + translate" : "") : "", (unsigned)(t2 - t1), s->nsol, s->nmesh, mv, s->nscen, s->npage, s->ntex,
               s->bake_v, s->nbpage, unbaked);
    if (g.xp && logn <= 400) printf("rstream:   (%s translated in %u ms)\n", s->name, (unsigned)tx);
}

static int rs_worker(void *arg) {
    (void)arg;
    int f = open(g.stream, O_RDONLY);
    if (f < 0) { printf("rstream: cannot open %s\n", g.stream); return 0; }
    for (;;) {
        rs_lock(&g_mx);
        RsSec *best = NULL;
        while (!g.quit) {
            best = NULL;
            for (int i = 0; i < g.nsec; i++) {
                RsSec *s = &g.sec[i];
                if (s->state == 1 && s->wanted && (!best || s->prio < best->prio)) best = s;
            }
            if (best) break;
            rs_unlock(&g_mx); SDL_Delay(5); rs_lock(&g_mx);
        }
        if (g.quit) { rs_unlock(&g_mx); break; }
        best->state = 2;
        rs_unlock(&g_mx);
        rs_build(best, f);
        rs_lock(&g_mx);
        best->state = 3; best->t_built = SDL_GetTicks();
        rs_unlock(&g_mx);
    }
    close(f);
    return 0;
}

/* ---------------------------------------------------------------- activation (main) */
static uint32_t rs_hh(uint32_t h) { return (h * 2654435761u) >> 18; }   /* 14 bits */
static void rs_register(RsSec *s) {
    for (int i = 0; i < s->nsol; i++) {
        RsSolid *so = &s->sol[i];
        uint32_t b = rs_hh(so->hash);
        so->next = g_solhash[b]; g_solhash[b] = so;
    }
    for (int i = 0; i < s->nscen; i++) {
        RsScen *c = &s->scen[i];
        if (!g_hdr[c->number]) g_hdr[c->number] = c;
    }
    g_gen++;
}
static void rs_unregister(RsSec *s) {
    for (int i = 0; i < s->nsol; i++) {
        RsSolid *so = &s->sol[i];
        RsSolid **pp = &g_solhash[rs_hh(so->hash)];
        while (*pp && *pp != so) pp = &(*pp)->next;
        if (*pp) *pp = so->next;
    }
    for (int i = 0; i < s->nscen; i++) if (g_hdr[s->scen[i].number] == &s->scen[i]) g_hdr[s->scen[i].number] = NULL;
    g_gen++;
}
static RsSolid *rs_solid(uint32_t hash) {
    if (!hash || hash == 0xbe43edbbu || hash == 0x72923c81u) return NULL;
    for (RsSolid *so = g_solhash[rs_hh(hash)]; so; so = so->next) if (so->hash == hash) return so;
    return NULL;
}
/* GPU upload of a built section: 1 = done */
static int rs_activate(RsSec *s) {
    for (int k = 0; k < s->ntex; k++) { int st = g_tex[s->texs[k]].state; if (st != 3 && st != 4) return 0; }
    long bytes = 0;
    /* model pages go to the GPU only for instances left unbaked: an X library
       and a fully baked section draw from the bake pages alone (the X CPU copy
       stays for later bakes) */
    int models = rs_letter(s->number) != 24 && s->unbaked > 0;
    for (int i = 0; i < s->npage && models; i++) {
        RsPage *p = &s->page[i];
        if (!p->nv || !p->ni) continue;
        glGenBuffers(1, &p->vbo); glGenBuffers(1, &p->ibo);
        glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)p->nv * (GLsizeiptr)sizeof(BatchedVertex), p->v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)p->ni * 2, p->ix, GL_STATIC_DRAW);
        bytes += (long)p->nv * (long)sizeof(BatchedVertex) + (long)p->ni * 2;
    }
    for (int i = 0; i < s->nbpage; i++) {
        RsPage *p = &s->bpage[i];
        if (!p->nv || !p->ni) continue;
        glGenBuffers(1, &p->vbo); glGenBuffers(1, &p->ibo);
        glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)p->nv * (GLsizeiptr)sizeof(BatchedVertex), p->v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)p->ni * 2, p->ix, GL_STATIC_DRAW);
        bytes += (long)p->nv * (long)sizeof(BatchedVertex) + (long)p->ni * 2;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    /* an X library keeps its CPU vertices: later sections bake from them */
    if (rs_letter(s->number) != 24) rs_sec_free_cpu(s);
    for (int i = 0; i < s->nmesh; i++) {
        RsMesh *m = &s->mesh[i];
        m->b.vbo = s->page[m->page].vbo; m->b.ibo = s->page[m->page].ibo;
    }
    s->bytes_gpu = bytes;
    rs_register(s);
    s->state = 4;
    g.st_sec_up++;
    return 1;
}
static void rs_unload(RsSec *s) {
    if (s->state == 4) rs_unregister(s);
    rs_sec_free_all(s);
    s->state = 0;
    g.st_unload++;
}

/* ---------------------------------------------------------------- wanted list */
static void rs_want(int number, int extra) {
    if (number <= 0 || number >= RS_NUM) return;
    int i = g.secof[number];
    if (i < 0) return;
    RsSec *s = &g.sec[i];
    if (!s->wanted || extra < s->prio) s->prio = extra;   /* the band offset, finished below */
    s->wanted = 1;
}
static void rs_want_zone(int zone, int band_add) {
    if (zone <= 0) return;
    int lo = g.lod_offset ? g.lod_offset : 20;
    /* a LoadingSection naming the zone overrides the drivable list */
    for (int i = 0; i < g.nld; i++) {
        const RsLoad *l = &g.ld[i]; int has = 0;
        for (int k = 0; k < l->nd && !has; k++) has = l->d[k] == zone;
        if (!has) continue;
        for (int k = 0; k < l->nd; k++) {
            const RsDriv *dv = l->d[k] > 0 && l->d[k] < RS_NUM ? g.drv[l->d[k]] : NULL;
            for (int j = 0; dv && j < dv->n; j++) rs_want(dv->v[j], band_add);
        }
        for (int k = 0; k < l->ne; k++) {
            int e = l->e[k], n = e % 100, L = e / 100;
            rs_want(e, band_add);
            if (L >= 1 && L <= 20 && n >= 1 && n < lo) rs_want(e + lo, band_add);
        }
        return;
    }
    const RsDriv *dv = g.drv[zone];
    for (int j = 0; dv && j < dv->n; j++) rs_want(dv->v[j], band_add);
}
static void rs_rebuild_wanted(void) {
    rs_lock(&g_mx);
    for (int i = 0; i < g.nsec; i++) g.sec[i].wanted = 0;
    rs_want(2500, 0); rs_want(2400, 0); rs_want(2600, 0);
    rs_want_zone(g.zone, 0);
    if (g.pzone != g.zone) rs_want_zone(g.pzone, 1);
    /* priority (2.2): Y -> X -> the rest, then 75 m bands of the distance to
       the section (the predicted zone's own sections one band later), then
       the authored base priority */
    for (int i = 0; i < g.nsec; i++) {
        RsSec *s = &g.sec[i];
        if (!s->wanted) continue;
        int L = rs_letter(s->number);
        int phase = L == 25 ? 0 : L == 24 ? 1 : 2;
        float dx = s->cx - g.pos[0], dy = s->cy - g.pos[1];
        float d = sqrtf(dx * dx + dy * dy) - s->rad; if (d < 0) d = 0;
        int band = s->rad > 0 ? (int)(d / 75.0f) : 0;
        band += s->prio; if (band > 3) band = 3;
        s->prio = phase * 10000000 + band * 100000 + (s->base_prio > 99999 ? 99999 : s->base_prio);
        if (s->state == 0) { s->state = 1; s->t_queued = SDL_GetTicks(); }
    }
    rs_unlock(&g_mx);
}

/* ---------------------------------------------------------------- start / stop */
static void rs_free_tables(void) {
    free(g.sec); g.sec = NULL; g.nsec = 0;
    for (int i = 0; i < g.nbnd; i++) free(g.bnd[i].pts);
    free(g.bnd); g.bnd = NULL; g.nbnd = 0;
    for (int i = 0; i < RS_NUM; i++) { free(g.drv[i]); g.drv[i] = NULL; }
    free(g.ld); g.ld = NULL; g.nld = 0;
    free(g.sel.items); memset(&g.sel, 0, sizeof g.sel); g.sel_on = 0;
    free(g.compdata); g.compdata = NULL; g.complen = 0; g.wgt_ok = 0;
    free(g.keys); g.keys = NULL; g.nkeys = 0;
    free(g.tpk.blk); memset(&g.tpk, 0, sizeof g.tpk);
    free(g.adstpk.blk); memset(&g.adstpk, 0, sizeof g.adstpk);
    free(g.ads); g.ads = NULL; g.adslen = 0; g.nads = 0;
    free(g.commontpk.blk); memset(&g.commontpk, 0, sizeof g.commontpk);
    free(g.common); g.common = NULL; g.commonlen = 0;
    free(g.loc4); g.loc4 = NULL; g.loc4len = 0;
}
/* parked: the worker stopped and the pinned bundle let go, but the X model
   library, every uploaded texture and the tables kept for a restart on the
   same stream (a same-city reload: X0 took ~5 s again on HW, hwlog88) */
static int g_parked;
void rstream_park_for(const char *next_track) {
    if (!g.active || !rstream_track_ok(next_track)) { rstream_stop(); return; }
    if (g.thr) {
        rs_lock(&g_mx); g.quit = 1; rs_unlock(&g_mx);
        SDL_WaitThread(g.thr, NULL); g.thr = NULL;
    }
    int keep = 0;
    for (int i = 0; i < g.nsec; i++) {
        RsSec *s = &g.sec[i]; int L = rs_letter(s->number);
        s->wanted = 0;
        if (L == 24 || L == 25) { keep += s->state == 3 || s->state == 4; continue; }
        if (s->state) rs_unload(s);
    }
    free(g.tpk.blk); memset(&g.tpk, 0, sizeof g.tpk);
    g.pin = NULL; g.pinlen = 0;   /* borrowed: the caller unpins it next */
    int ntex = 0; for (int i = 0; i < RS_TEXCAP; i++) ntex += g_tex[i].state == 3;
    g.active = 0; g.quit = 0; g.have_pos = 0; g.zone = g.pzone = 0;
    g_parked = 1;
    printf("rstream: parked for the reload (%d library sections, %d textures, %ld KB kept)\n",
           keep, ntex, g.tex_gpu / 1024);
}
void rstream_stop(void) {
    if (!g.active && !g.thr && !g_parked) return;
    g_parked = 0;
    if (g.thr) {
        rs_lock(&g_mx); g.quit = 1; rs_unlock(&g_mx);
        SDL_WaitThread(g.thr, NULL); g.thr = NULL;
    }
    for (int i = 0; i < g.nsec; i++) if (g.sec[i].state) rs_unload(&g.sec[i]);
    for (int i = 0; i < RS_TEXCAP; i++) {
        if (g_tex[i].state == 3 && g_tex[i].tex) glDeleteTextures(1, &g_tex[i].tex);
        free(g_tex[i].t.rgb); free(g_tex[i].t.alpha); free(g_tex[i].t.dxt);
    }
    memset(g_tex, 0, sizeof g_tex);
    g.dec_bytes = 0; g.ndec = 0; g.tex_gpu = 0;
    memset(g_solhash, 0, sizeof g_solhash); memset(g_hdr, 0, sizeof g_hdr); memset(g_xhash, 0, sizeof g_xhash);
    rs_free_tables();
    rs_sky_free(1);
    rs_prop_free();
    g.active = 0; g.quit = 0; g.have_pos = 0; g.zone = g.pzone = 0;
    printf("rstream: stopped\n");
}
int rstream_active(void) { return g.active; }

/* the sky batches, world space, texture keys for the sky profile (the tile
   build's n2_sky_remap_key); 0 until the dome's library and its instance are in */
N2Batch *rstream_sky(int profile, int *count) {
    *count = 0;
    if (!g.active) return NULL;
    static int last_profile = -999;
    if (g_sky_state == 1 && profile != last_profile) rs_sky_free(0);
    if (g_sky_state != 1) {
        static Uint32 last_try;
        if (g_sky_state == 2 && SDL_GetTicks() - last_try < 500) return NULL;
        last_try = SDL_GetTicks();
        rs_lock(&g_mx); int nsrc = g_nskysrc; rs_unlock(&g_mx);
        if (!nsrc) { g_sky_state = 2; return NULL; }
        /* the instance placing the dome solid, in any built section */
        const RsInst *in = NULL;
        for (int i = 0; i < g.nsec && !in; i++) {
            RsSec *s = &g.sec[i];
            if (s->state < 3) continue;
            for (int c = 0; c < s->nscen && !in; c++) {
                RsScen *sc = &s->scen[c];
                for (int k = 0; k < sc->ninst && !in; k++) {
                    int inf = sc->in[k].info;
                    if (inf < 0 || inf >= sc->ninfo) continue;
                    uint32_t h = rd32(sc->info + inf * 0x44 + 0x20);
                    for (int q = 0; q < nsrc && !in; q++) if (g_skysrc[q].solid == h) in = &sc->in[k];
                }
            }
        }
        if (!in) { g_sky_state = 2; return NULL; }
        float M[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        if (!(in->flags & 0x100)) {
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 3; col++) M[row * 4 + col] = (float)in->rot[row * 3 + col] * (1.0f / 8192.0f);
            M[12] = in->pos[0]; M[13] = in->pos[1]; M[14] = in->pos[2];
        }
        for (int q = 0; q < nsrc && g_nskyb < RS_SKYMAX; q++) {
            const RsSkySrc *k = &g_skysrc[q];
            BatchedVertex *bv = (BatchedVertex *)malloc((size_t)k->nv * sizeof *bv);
            if (!bv) continue;
            N2Batch *b = &g_skyb[g_nskyb]; memset(b, 0, sizeof *b);
            for (int c = 0; c < 3; c++) { b->bbox_min[c] = 1e30f; b->bbox_max[c] = -1e30f; }
            for (int v = 0; v < k->nv; v++) {
                const float *p = k->v + v * 5; BatchedVertex *o = &bv[v];
                float x = p[0], y = p[1], z = p[2];
                o->pos[0] = M[0]*x + M[4]*y + M[8]*z + M[12];
                o->pos[1] = M[1]*x + M[5]*y + M[9]*z + M[13];
                o->pos[2] = M[2]*x + M[6]*y + M[10]*z + M[14];
                o->uv[0] = p[3]; o->uv[1] = p[4];
                BV_SET_NORMAL(o, 0.0f, 0.0f, 1.0f);
                if (k->col) memcpy(o->col, k->col + v * 4, 4);
                else o->col[0] = o->col[1] = o->col[2] = o->col[3] = 255;
                for (int c = 0; c < 3; c++) {
                    if (o->pos[c] < b->bbox_min[c]) b->bbox_min[c] = o->pos[c];
                    if (o->pos[c] > b->bbox_max[c]) b->bbox_max[c] = o->pos[c];
                }
            }
            glGenBuffers(1, &b->vbo); glGenBuffers(1, &b->ibo);
            glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)k->nv * sizeof *bv), bv, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)k->ni * sizeof(uint16_t)), k->ix, GL_STATIC_DRAW);
            free(bv);
            b->index_count = k->ni; b->nmesh = 1;
            b->texkey = n2_sky_remap_key(profile, k->texkey);
            rs_lock(&g_mx);
            int slot = rs_tex_slot(b->texkey, 1);
            int go = slot >= 0 && g_tex[slot].state == 1;
            if (go) g_tex[slot].state = 5;
            rs_unlock(&g_mx);
            if (go) rs_tex_decode(slot);   /* once: the upload below (or the next frame's) binds it */
            printf("rstream:   sky batch %d: solid %08x texture %08x -> %08x, %d vertices, %d indices, z %.0f..%.0f""\n",
                   g_nskyb, k->solid, k->texkey, b->texkey, k->nv, k->ni, b->bbox_min[2], b->bbox_max[2]);
            g_sky_slot[g_nskyb++] = slot;
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        g_sky_state = 1; last_profile = profile;
        printf("rstream: sky placed at (%.0f %.0f %.0f): %d batch(es) for profile %d\n",
               M[12], M[13], M[14], g_nskyb, profile);
    }
    int n = 0;
    for (int i = 0; i < g_nskyb; i++) {
        int t = g_sky_slot[i];
        g_skyb[i].tex = t >= 0 && g_tex[t].state == 3 ? g_tex[t].tex : 0;
        n += g_skyb[i].tex != 0;
    }
    if (!n) { rs_tex_upload(SDL_GetPerformanceCounter() + SDL_GetPerformanceFrequency() / 500); return NULL; }
    /* only the pieces with a texture, in order: Palmont's SKYDOME carries five, three of them
       (95a0861e afa7d64f ff91ce2b) Carbon's own sky in its GLOBALB, not on the card -- drawn
       untextured they blacked out the night dome (LOC4's pair) over the whole sky */
    static N2Batch out[RS_SKYMAX];
    int k = 0;
    for (int i = 0; i < g_nskyb; i++) if (g_skyb[i].tex) out[k++] = g_skyb[i];
    *count = k;
    return out;
}

/* the scenery set (resident_args.scenery_event: -1 free roam, > 0 a race's
   event, 0 unfiltered) -- the selection world_instance.c builds for the tiles */
void rstream_set_event(int event) {
    if (!g.active || !g.wgt_ok) return;
    int key[3] = { event, g_wg_career_stage, g_wg_career_owns_car };
    if (!memcmp(key, g.sel_key, sizeof key)) return;
    memcpy(g.sel_key, key, sizeof key);
    free(g.sel.items); memset(&g.sel, 0, sizeof g.sel); g.sel_on = 0;
    int eff = event;
    if (eff && !wg_event_group_present(&g.wgt, eff)) eff = 0;   /* no group of its own: unfiltered */
    if (eff && wg_selection_open(&g.wgt, eff, &g.sel)) g.sel_on = 1;
    g_sel_gen++;
    printf("rstream: scenery groups for event %d (career stage %d): %s, %d overrides\n",
           event, g_wg_career_stage, g.sel_on ? "selected" : "unfiltered", (int)g.sel.count);
}

int rstream_start(const char *track_root, const char *trackname) {
    if (!rstream_track_ok(trackname)) { rstream_stop(); return 0; }
    char stream[640];
    const char *b = rs_base(trackname);
    snprintf(stream, sizeof stream, "%s/%s%s", track_root, b, strstr(b, ".BUN") ? "" : ".BUN");
    int xp = xpcity_lean_game(b);
    if (xp) xpcity_stream_path(b, stream, sizeof stream);   /* the raw MW stream; its companion: the translated one in track_root */
    if (g.active && !strcmp(stream, g.stream)) return 1;
    if (g_parked && !strcmp(stream, g.stream)) {   /* the same stream again: the kept library */
        Uint32 u0 = SDL_GetTicks();
        g.pin = res_bundle(g.stream, &g.pinlen);
        g.pin_gen = res_pin_gen();
        if (g.pin) g.tpk = n2_tpk_open(g.pin, g.pinlen);
        g.quit = 0;
        g.thr = SDL_CreateThread(rs_worker, "rstream", NULL);
        if (!g.thr) { printf("rstream: no worker thread\n"); rstream_stop(); return 0; }
        g_parked = 0;
        g.active = 1;
        g.cold = 1; g.cold_t0 = 0;
        g.st_t0 = SDL_GetTicks();
        printf("rstream: resumed from park (%u ms)\n", (unsigned)(SDL_GetTicks() - u0));
        return 1;
    }
    rstream_stop();
    g.xp = xp;
    snprintf(g.troot, sizeof g.troot, "%s", track_root);
    snprintf(g.stream, sizeof g.stream, "%s", stream);
    char stem[64]; snprintf(stem, sizeof stem, "%s", b + 6);
    char *dot = strstr(stem, ".BUN"); if (dot) *dot = 0;
    snprintf(g.comp, sizeof g.comp, "%s/%s.BUN", track_root, stem);
    snprintf(g.stem, sizeof g.stem, "%s", stem);
    Uint32 t0 = SDL_GetTicks();
    for (int i = 0; i < RS_NUM; i++) g.secof[i] = -1;
    long clen = 0;
    unsigned char *comp = n2_read_file(g.comp, &clen);
    if (!comp) { printf("rstream: cannot read %s\n", g.comp); return 0; }
    asset_chunks_walk(comp, 0, clen, rs_comp_visit, NULL);
    g.compdata = comp; g.complen = clen;   /* kept: the group table views it */
    g.wgt_ok = wg_open_file(comp, (size_t)clen, &g.wgt);
    g.sel_on = 0; g.sel_key[0] = g.sel_key[1] = g.sel_key[2] = -99999;
    if (!g.nsec || !g.nbnd) { printf("rstream: %s: %d sections, %d zones -- not used\n", g.comp, g.nsec, g.nbnd); rs_free_tables(); return 0; }
    g.pin = res_bundle(g.stream, &g.pinlen);
    g.pin_gen = res_pin_gen();
    if (g.pin) {
        g.tpk = n2_tpk_open(g.pin, g.pinlen);
        g.keys = (uint32_t *)malloc(16384 * sizeof *g.keys);
        if (g.keys) g.nkeys = n2_tpk_keys(g.pin, g.tpk, g.keys, 16384);
    }
    {   /* LOC4DYNTEX, InGameCommon, the regional ad pack: the tile builder's key order */
        char pth[700];
        snprintf(pth, sizeof pth, "%s/LOC4DYNTEX.BIN", track_root);
        g.loc4 = n2_read_file(pth, &g.loc4len);
        if (g.loc4 && g.keys && g.nkeys < 16384)
            g.nkeys += n2_car_tex_keys(g.loc4, g.loc4len, g.keys + g.nkeys, 16384 - g.nkeys);
        snprintf(pth, sizeof pth, "%s/../GLOBAL/InGameCommon.bun", track_root);
        g.common = n2_read_file(pth, &g.commonlen);
        if (g.common) {
            g.commontpk = n2_tpk_open(g.common, g.commonlen);
            if (g.keys && g.nkeys < 16384) g.nkeys += n2_tpk_keys(g.common, g.commontpk, g.keys + g.nkeys, 16384 - g.nkeys);
        }
        g.ads = n2_read_global_ads(track_root, &g.adslen);   /* a copy (nfsu2.h) */
        if (g.ads) {
            g.adstpk = n2_tpk_open(g.ads, g.adslen);
            if (g.keys && g.nkeys < 16384) {
                g.nads = n2_tpk_keys(g.ads, g.adstpk, g.keys + g.nkeys, 16384 - g.nkeys);
                g.nkeys += g.nads;
            }
        }
    }
    int ndrv = 0; for (int i = 0; i < RS_NUM; i++) ndrv += g.drv[i] != NULL;
    printf("rstream: %s: %d sections, %d zones, %d visible lists, %d loading sections, LOD offset %d; "
           "pinned bundle %ld KB, %d texture blocks, %d keys (%d ads) (%u ms)\n",
           stem, g.nsec, g.nbnd, ndrv, g.nld, g.lod_offset, g.pinlen / 1024, g.tpk.nblk, g.nkeys, g.nads,
           (unsigned)(SDL_GetTicks() - t0));
    g.quit = 0;
    g.thr = SDL_CreateThread(rs_worker, "rstream", NULL);
    if (!g.thr) { printf("rstream: no worker thread\n"); rs_free_tables(); return 0; }
    g.active = 1;
    g.cold = 1; g.cold_t0 = 0;
    g.st_t0 = SDL_GetTicks();
    return 1;
}

/* ---------------------------------------------------------------- per frame (main) */
void rstream_update(const float cam[3]) {
    if (g.active && g.pin && res_pin_gen() != g.pin_gen) {
        printf("rstream: the pinned bundle changed under the streamer -- restarting\n");
        rstream_stop();
        return;
    }
    if (!g.active) return;
    Uint64 f = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    Uint32 now = SDL_GetTicks();
    if (g.have_pos && now > g.t_pos) {
        float dt = (float)(now - g.t_pos) * 0.001f;
        for (int c = 0; c < 3; c++) {
            float v = (cam[c] - g.pos[c]) / dt;
            g.vel[c] = g.vel[c] * 0.8f + v * 0.2f;
        }
        float jump = fabsf(cam[0] - g.pos[0]) + fabsf(cam[1] - g.pos[1]);
        if (jump > 50.0f) g.vel[0] = g.vel[1] = g.vel[2] = 0;   /* a teleport, not a speed */
    }
    memcpy(g.pos, cam, sizeof g.pos); g.t_pos = now; g.have_pos = 1;
    int zone = rs_find_zone(cam[0], cam[1]);
    float sp = sqrtf(g.vel[0] * g.vel[0] + g.vel[1] * g.vel[1]);
    int pz = zone;
    if (sp > 1.0f) {
        float t = 1.5f; if (sp * t > 100.0f) t = 100.0f / sp;
        int z = rs_find_zone(cam[0] + g.vel[0] * t, cam[1] + g.vel[1] * t);
        if (z) pz = z;
        /* FindDrivableSection reordered the list for the prediction: put the eye's zone back first */
        if (zone) rs_find_zone(cam[0], cam[1]);
    }
    static int frames;
    if (zone && (zone != g.zone || pz != g.pzone || ++frames >= 60)) {
        frames = 0;
        int changed = zone != g.zone || pz != g.pzone;
        g.zone = zone; g.pzone = pz;
        rs_rebuild_wanted();
        if (changed) {   /* PC: unwanted sections out at once (X and Y stay) */
            for (int i = 0; i < g.nsec; i++) {
                RsSec *s = &g.sec[i]; int L = rs_letter(s->number);
                if (s->wanted || L == 24 || L == 25) continue;
                rs_lock(&g_mx);
                int st = s->state;
                if (st == 1) s->state = 0;
                rs_unlock(&g_mx);
                if (st == 3 || st == 4) rs_unload(s);
            }
        }
    }
    /* uploads: textures, then built sections, ~3 ms a frame (more while nothing is drawn yet) */
    int have4 = 0; for (int i = 0; i < g.nsec && !have4; i++) have4 = g.sec[i].state == 4 && rs_letter(g.sec[i].number) < 24;
    Uint64 budget = f * (have4 ? 3 : 30) / 1000, deadline = t0 + budget;
    rs_tex_upload(deadline);
    /* built sections (state 3) are rare: test the state first, read the clock
       only around real work (hwlog84: ~800 clock reads a frame were 1.7 ms) */
    int over = 0;
    for (int pass = 0; pass < 2 && !over; pass++)
        for (int i = 0; i < g.nsec && !over; i++) {
            RsSec *s = &g.sec[i];
            if (s->state != 3) continue;   /* only the worker moves 2 -> 3 */
            rs_lock(&g_mx); int st = s->state, w = s->wanted; rs_unlock(&g_mx);
            if (st != 3) continue;
            if (!w && rs_letter(s->number) < 24) { rs_unload(s); continue; }
            if (pass == 0 && rs_letter(s->number) < 24) continue;   /* X/Y/Z first */
            Uint64 a0 = SDL_GetPerformanceCounter();
            if (a0 >= deadline) { over = 1; break; }
            if (rs_activate(s)) g.st_up_ms += (double)(SDL_GetPerformanceCounter() - a0) * 1000.0 / (double)f;
        }
    if (g.cold) {
        int left = 0;
        if (!g.cold_t0 || now - g.cold_last > 1000) g.cold_t0 = now ? now : 1;
        g.cold_last = now;
        if (rstream_area_ready(&left) || now - g.cold_t0 > 20000) {
            printf("rstream: first area %s %u ms after its first frame (%d left)\n",
                   left ? "given up" : "in", (unsigned)(now - g.cold_t0), left);
            g.cold = 0;
        }
    }
    g.st_upd_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)f;
}

/* the loading screen's wait (retail holds it until the TrackStreamer has
   the start area in): every wanted section on the GPU and no texture still
   queued or decoded -- 1 also when not streaming or outside every zone */
int rstream_cold(void) { return g.active && g.cold; }
void rstream_rewarm(void) { if (g.active) { g.cold = 1; g.cold_t0 = 0; } }

int rstream_area_ready(int *left) {
    int n = 0;
    if (left) *left = 0;
    if (!g.active) return 1;
    if (!g.have_pos) { if (left) *left = -1; return 0; }
    if (!g.zone) return 1;
    rs_lock(&g_mx);
    for (int i = 0; i < g.nsec; i++) if (g.sec[i].wanted && g.sec[i].state != 4) n++;
    if (g.ndec) n++;
    for (int i = 0; i < RS_TEXCAP && !n; i++) if (g_tex[i].state == 1) n++;
    rs_unlock(&g_mx);
    if (left) *left = n;
    return n == 0;
}

/* ---------------------------------------------------------------- draw */
/* 0 outside, 1 crossing, 2 wholly inside (unnormalised planes) */
static int rs_box_state(const float pl[6][4], const float lo[3], const float hi[3]) {
    int inside = 1;
    for (int p = 0; p < 6; p++) {
        const float *v = pl[p];
        float dmax = v[3], dmin = v[3];
        for (int c = 0; c < 3; c++) {
            if (v[c] >= 0) { dmax += v[c] * hi[c]; dmin += v[c] * lo[c]; }
            else { dmax += v[c] * lo[c]; dmin += v[c] * hi[c]; }
        }
        float margin = .01f * (fabsf(v[0]) + fabsf(v[1]) + fabsf(v[2]));
        if (dmax < -margin) return 0;
        if (dmin < 0) inside = 0;
    }
    return inside ? 2 : 1;
}
static void rs_mul(const float a[16], const float b[16], float o[16]) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c*4+r] = a[r]*b[c*4] + a[4+r]*b[c*4+1] + a[8+r]*b[c*4+2] + a[12+r]*b[c*4+3];
}
/* world AABB of a model-space box under M */
static void rs_xbox(const float M[16], const float lo[3], const float hi[3], float wlo[3], float whi[3]) {
    float c[3] = { (lo[0]+hi[0])*.5f, (lo[1]+hi[1])*.5f, (lo[2]+hi[2])*.5f };
    float e[3] = { (hi[0]-lo[0])*.5f, (hi[1]-lo[1])*.5f, (hi[2]-lo[2])*.5f };
    for (int i = 0; i < 3; i++) {
        float w = M[12+i] + M[i]*c[0] + M[4+i]*c[1] + M[8+i]*c[2];
        float x = fabsf(M[i])*e[0] + fabsf(M[4+i])*e[1] + fabsf(M[8+i])*e[2];
        wlo[i] = w - x; whi[i] = w + x;
    }
}

typedef struct { const RsSolid *s; int mtx; int state; } RsRec;   /* mtx: -1 identity, else index */
#define RS_MAXREC 6000
static RsRec g_rec[RS_MAXREC]; static float g_mtx[RS_MAXREC][16]; static int g_nrec, g_nmtx;
/* a deferred draw: one instanced mesh (mtx = its matrix) or one merged bake run (mtx -1) */
typedef struct { const RsMesh *m; GLuint vbo, ibo; unsigned ioff, count; int mtx; unsigned char row; } RsDef;
static RsScen *g_touched[512]; static int g_ntouched;
#define RS_MAXDEF 8192
static RsDef g_def[4][RS_MAXDEF]; static int g_ndef[4];   /* 0 reflect, 1 cutout, 2 blend/add, 3 glow */

typedef struct {
    const RsDrawCtx *c; float H; int cell; unsigned cull;
    float dir[3];
    int aux;
} RsCull;

/* groups + smackable props, per instance, once per selection (bsearches) */
static void rs_hide_fill(RsScen *sc) {
    for (int i = 0; i < sc->ninst; i++) {
        unsigned char h = 0;
        if (g.sel_on && !wg_selection_visible(&g.sel, (unsigned)sc->number, (unsigned)i)) h = 1;   /* the tiles' rule */
        else if (smk_is_prop((unsigned)sc->number, (unsigned)i)) h = 2;   /* smokeable.c draws, hits and smashes those */
        sc->hide[i] = h;
    }
    sc->hide_gen = g_sel_gen;
}

static void rs_draw_instance(RsCull *K, RsScen *sc, int ii, int state) {
    const RsInst *in = &sc->in[ii];
    int info = in->info;
    unsigned flags = in->flags;
    if (info < 0 || info >= sc->ninfo) return;
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_RS_PT="x y z r" lists (once each) the instances whose box comes within r of the point */
        static int pt_on = -1; static float P[4]; static int seen[64][2], nseen;
        if (pt_on < 0) { const char *e = getenv("OPENUG2_RS_PT"); pt_on = e && sscanf(e, "%f %f %f %f", &P[0], &P[1], &P[2], &P[3]) == 4; }
        if (pt_on && !K->aux && P[0] > in->lo[0] - P[3] && P[0] < in->hi[0] + P[3] && P[1] > in->lo[1] - P[3] && P[1] < in->hi[1] + P[3]
            && P[2] > in->lo[2] - P[3] && P[2] < in->hi[2] + P[3]) {
            int k = 0; while (k < nseen && !(seen[k][0] == sc->number && seen[k][1] == ii)) k++;
            if (k == nseen && nseen < 64) {
                seen[nseen][0] = sc->number; seen[nseen][1] = ii; nseen++;
                const RsSolid *a = sc->bk && sc->bk[ii] ? sc->bm[info][0] : NULL, *b = sc->bk && sc->bk[ii] ? sc->bm[info][1] : NULL;
                printf("rs pt: %s inst %d info %d flags %04x hide %d baked %d box %.1f %.1f %.1f .. %.1f %.1f %.1f pos %.1f %.1f %.1f R %.1f"
                       " rot %d %d %d | %d %d %d | %d %d %d solids %08x %08x\n",
                       sc->sec->name, ii, info, flags, sc->hide[ii], sc->bk ? sc->bk[ii] : -1,
                       in->lo[0], in->lo[1], in->lo[2], in->hi[0], in->hi[1], in->hi[2], in->pos[0], in->pos[1], in->pos[2], sc->infR[info],
                       in->rot[0], in->rot[1], in->rot[2], in->rot[3], in->rot[4], in->rot[5], in->rot[6], in->rot[7], in->rot[8],
                       a ? a->hash : 0, b ? b->hash : 0);
                if (g.wgt_ok) for (size_t o = 0; o < g.wgt.override_count; o++) {
                    const unsigned char *r = g.wgt.overrides + 8 * o;
                    if (wg_u16(r) != sc->number || wg_u16(r + 2) != ii) continue;
                    printf("rs pt:   override %u flags %04x groups:", (unsigned)o, wg_u16(r + 4));
                    for (size_t p = 0; p < g.wgt.group_bytes;) {
                        const unsigned char *gp = g.wgt.groups + p; uint32_t refs = wg_u32(gp + 48);
                        for (uint32_t j = 0; j < refs; j++) if (wg_u16(gp + 52 + 2 * (size_t)j) == o) printf(" %.40s", (const char *)gp + 8);
                        p += (size_t)((52ULL + 2ULL * refs + 3) & ~3ULL);
                    }
                    printf("\n");
                }
            }
        }
    }
#endif
    /* 1 precull */
    if (K->cell >= 0 && in->pre >= 0 && (sc->pre[in->pre * 0x80 + (K->cell >> 3)] >> (K->cell & 7) & 1)) { g.st_cull[0]++; return; }
    /* scenery groups / props (the port's rules, not the 0x10 exclusion bit) */
    if (sc->hide[ii]) { g.st_cull[sc->hide[ii] == 1 ? 7 : 6]++; return; }
    if (K->aux) {   /* an auxiliary view: near, and standing up where asked */
        const RsDrawCtx *c = K->c;
        if (in->hi[2] < c->minz || in->hi[2] - in->lo[2] < c->minh) return;
        float dx = c->cam[0] < in->lo[0] ? in->lo[0] - c->cam[0] : c->cam[0] > in->hi[0] ? c->cam[0] - in->hi[0] : 0.0f;
        float dy = c->cam[1] < in->lo[1] ? in->lo[1] - c->cam[1] : c->cam[1] > in->hi[1] ? c->cam[1] - in->hi[1] : 0.0f;
        if (dx * dx + dy * dy > c->reach * c->reach) return;
    }
    /* 2 view exclusion */
    if (((flags & 0xff) ^ 0x60) & K->cull) { g.st_cull[1]++; return; }
    /* 3 frustum */
    if (state == 1) {
        state = rs_box_state(K->c->planes, in->lo, in->hi);
        if (!state) { g.st_cull[2]++; return; }
    }
    /* 4 pixel size */
    float R = sc->infR[info] + 6.0f;
    float v[3] = { in->pos[0] - K->c->cam[0], in->pos[1] - K->c->cam[1], in->pos[2] - K->c->cam[2] };
    int pixels;
    if (v[0]*K->dir[0] + v[1]*K->dir[1] + v[2]*K->dir[2] < -R) pixels = 0;
    else {
        float d = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        pixels = (int)(d - R > R ? K->H * R / (d - R) : K->H);
    }
    if (pixels < 17) { g.st_cull[3]++; return; }   /* < 2, then the main view's 17 */
    /* 5 LOD */
    if (sc->bk && sc->bk[ii]) {   /* baked: mark it; the bake runs draw it */
        const RsSolid *a = sc->bm[info][0], *b = sc->bm[info][1];
        float fb = 0.0f;
        if (a) {
            if (a->polys < 40) fb = 4.0f;
            else if (a->volume > 0.0f) fb = (float)pixels / (a->density > 6.0f ? a->density : 6.0f);
        }
        int lod = fb >= 4.0f ? 0 : 1;
        if (!(lod ? b : a)) return;
        if (!sc->touched) {
            if (g_ntouched >= 512) { g.st_cull[5]++; return; }
            g_touched[g_ntouched++] = sc; sc->touched = 1;
        }
        sc->vis[ii] = (unsigned char)(lod + 1);
        g.st_binst++;
        return;
    }
    if (sc->gen != g_gen) {
        for (int k = 0; k < sc->ninfo; k++)
            for (int m = 0; m < 3; m++) sc->model[k][m] = rs_solid(rd32(sc->info + k * 0x44 + 0x20 + m * 4));
        sc->gen = g_gen;
    }
    const RsSolid *m0 = sc->model[info][0], *so = sc->model[info][1];
    float fl = 0.0f;
    if (m0) {
        if (m0->polys < 40) fl = 4.0f;
        else if (m0->volume > 0.0f) fl = (float)pixels / (m0->density > 6.0f ? m0->density : 6.0f);
    }
    if (fl >= 4.0f) so = m0;   /* worldDetail == 2 (the PC's top setting) */
    if (!so) {
        if (!m0 && !sc->model[info][1] && rd32(sc->info + info * 0x44 + 0x20)) g.st_cull[4]++;   /* the model's section is not in */
        return;
    }
    if (!so->nm) return;
    if (g_nrec >= RS_MAXREC) { g.st_cull[5]++; return; }
    RsRec *r = &g_rec[g_nrec++];
    r->s = so; r->state = state; r->mtx = -1;
    if (!(flags & 0x100)) {
        float *M = g_mtx[g_nmtx];
        M[3] = M[7] = M[11] = 0.0f; M[15] = 1.0f;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++) M[row * 4 + col] = (float)in->rot[row * 3 + col] * (1.0f / 8192.0f);
        M[12] = in->pos[0]; M[13] = in->pos[1]; M[14] = in->pos[2];
        r->mtx = g_nmtx++;
    }
}

static void rs_tree_cull(RsCull *K, RsScen *sc) {
    if (sc->hide_gen != g_sel_gen) rs_hide_fill(sc);
    if (!sc->nnode) {
        for (int i = 0; i < sc->ninst; i++) rs_draw_instance(K, sc, i, 1);
        return;
    }
    int stk[64], sst[64], sp = 0;
    stk[sp] = 0; sst[sp] = 1; sp++;
    while (sp > 0) {
        sp--;
        int n = stk[sp], st = sst[sp];
        if (n < 0 || n >= sc->nnode) continue;
        const RsNode *nd = &sc->nd[n];
        if (st == 1) {
            st = rs_box_state(K->c->planes, nd->lo, nd->hi);
            if (!st) continue;
        }
        for (int k = 0; k < nd->n; k++) {
            int ch = nd->ch[k];
            if (ch < 0) { if (sp < 64) { stk[sp] = -ch; sst[sp] = st; sp++; } }
            else if (ch < sc->ninst) rs_draw_instance(K, sc, ch, st);
        }
    }
}

static GLuint g_lasttex; static int g_lastcut, g_lastmtx;
static long g_st_mtxsw, g_st_texsw, g_st_ident;
static void rs_bind_tex(const RsDrawCtx *c, const RsMesh *m, N2Batch *b) {
    GLuint t = 0;
    if (m->tex >= 0 && g_tex[m->tex].state == 3) t = g_tex[m->tex].tex;
    else if (m->b.texkey) t = world_texture_lookup(m->b.texkey, NULL);
    b->tex = t;
    if (t == g_lasttex) return;
    g_st_texsw++;
    if (t) { glUniform1f(c->rp->uUseTex, 1.0f); glBindTexture(GL_TEXTURE_2D, t); }
    else { glUniform1f(c->rp->uUseTex, 0.0f); glUniform3f(c->rp->uColor, 0.28f, 0.29f, 0.31f); }
    g_lasttex = t;
}
static void rs_set_mtx(const RsDrawCtx *c, int mtx) {
    if (mtx == g_lastmtx) return;
    g_st_mtxsw++;
    if (mtx < 0) { glUniformMatrix4fv(c->rp->uMVP, 1, GL_FALSE, c->MVP); render_model(c->rp, NULL); }
    else {
        float M2[16]; rs_mul(c->MVP, g_mtx[mtx], M2);
        glUniformMatrix4fv(c->rp->uMVP, 1, GL_FALSE, M2); render_model(c->rp, g_mtx[mtx]);
    }
    g_lastmtx = mtx;
}
static void rs_def_batch(const RsDef *d, N2Batch *b) {
    *b = d->m->b; b->vbo = d->vbo; b->ibo = d->ibo; b->ioff = d->ioff; b->index_count = d->count;
}
static int rs_mesh_visible(const RsDrawCtx *c, const RsRec *r, const RsMesh *m) {
    if (r->state == 2) return 1;
    float lo[3], hi[3];
    if (r->mtx < 0) { memcpy(lo, m->lo, sizeof lo); memcpy(hi, m->hi, sizeof hi); }
    else rs_xbox(g_mtx[r->mtx], m->lo, m->hi, lo, hi);
    return rs_box_state(c->planes, lo, hi) != 0;
}
static unsigned char rs_mesh_mode(const RsMesh *m) {
    if (m->tex >= 0 && g_tex[m->tex].state == 3) return m->mat_exact ? g_tex[m->tex].mode : N2_DRAW_OPAQUE;
    unsigned char md = 0;
    if (m->b.texkey && world_texture_lookup(m->b.texkey, &md) && m->mat_exact) return md;
    return N2_DRAW_OPAQUE;
}

int rstream_draw(const RsDrawCtx *c) {
    if (!g.active) return 0;
    Uint64 f = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    RsCull K; memset(&K, 0, sizeof K);
    K.c = c;
    const int aux = c->aux;
    long aux_cull[8], aux_b[3];   /* an aux view leaves the main view's stats alone */
    if (aux) { memcpy(aux_cull, g.st_cull, sizeof aux_cull); aux_b[0] = g.st_binst; aux_b[1] = g.st_bruns; aux_b[2] = g.st_bpieces; }
    K.aux = aux;
    float th = tanf(c->hfov43 * 0.5f);
    K.H = th != 0.0f ? 256.0f / th : 10.0f;
    if (aux && c->pix > 0.0f) K.H *= c->pix;
    float ll = sqrtf(c->look[0]*c->look[0] + c->look[1]*c->look[1] + c->look[2]*c->look[2]);
    for (int i = 0; i < 3; i++) K.dir[i] = ll > 1e-6f ? c->look[i] / ll : 0.0f;
    /* precull mode 3: only at camera speed >= 17.88 m/s */
    float sp = sqrtf(g.vel[0]*g.vel[0] + g.vel[1]*g.vel[1] + g.vel[2]*g.vel[2]);
    K.cell = sp >= 17.88f || g_rs_precull_always ? ((((int)c->cam[1] >> 5) & 31) * 32 + (((int)c->cam[0] >> 5) & 31)) : -1;
    /* a player view (0x02) + forward track direction [guess 0x08]. 0x10 ("always")
       is left out: the port decides group members by its own selection above,
       as the tile build does (world_scenery.h) */
    K.cull = 0x02 | 0x08;
    g_nrec = g_nmtx = 0; g_ndef[0] = g_ndef[1] = g_ndef[2] = g_ndef[3] = 0; g_ntouched = 0;
    /* WhatSectionsShouldWeDraw */
    int zone = rs_find_zone(c->cam[0], c->cam[1]);
    static RsScen *list[160]; int nl = 0;
    for (int n = 2600; n < 2700 && nl < 160; n++) if (g_hdr[n]) list[nl++] = g_hdr[n];
    if (zone && g.drv[zone]) {
        const RsDriv *dv = g.drv[zone];
        for (int j = 0; j < dv->n && nl < 160; j++) {
            int n = dv->v[j];
            if (n > 0 && n < RS_NUM && g_hdr[n] && !(n >= 2600 && n < 2700)) list[nl++] = g_hdr[n];
        }
    } else
        for (int n = 1; n < 2600 && nl < 160; n++) if (g_hdr[n] && n % 100 < 10) list[nl++] = g_hdr[n];
    for (int i = 0; i < nl; i++) rs_tree_cull(&K, list[i]);
    Uint64 t1 = SDL_GetPerformanceCounter();

    /* draw: opaque now (instance order), the rest deferred into retail's order */
    int draws = 0;
    g_lasttex = (GLuint)-1; g_lastmtx = -2; g_lastcut = 0;
    glUniform1f(c->rp->uAlphaTest, 0.0f);
    for (int r = 0; r < g_nrec; r++) {
        const RsRec *R = &g_rec[r]; const RsSolid *so = R->s;
        for (int k = 0; k < so->nm; k++) {
            RsMesh *m = &so->sec->mesh[so->m0 + k];
            if (!m->b.vbo || !rs_mesh_visible(c, R, m)) continue;
            unsigned char md = rs_mesh_mode(m);
            int q = m->glow ? 3 : md == N2_DRAW_BLEND || md == N2_DRAW_ADD ? 2 : md == N2_DRAW_CUTOUT ? 1 : m->reflect && c->refl_ready ? 0 : -1;
            if (aux && (q == 1 || draws >= c->max_draws)) continue;
#ifdef OPENUG2_SIM_PRE_H
            {   /* test aid (simulator): OPENUG2_RS_HIDEQ=<q> hides a class (-1 opaque, 0 reflect, 1 cutout, 2 blend, 3 glow) */
                static int hq = -9; if (hq == -9) { const char *e = getenv("OPENUG2_RS_HIDEQ"); hq = e && *e ? atoi(e) : -5; }
                if (hq == q) continue;
                /* OPENUG2_RS_HIDEH=<lo>:<hi> (hex) hides opaque meshes of solids with lo <= hash < hi */
                static uint32_t hlo = 1, hhi = 0;
                if (hlo == 1 && hhi == 0) { const char *e = getenv("OPENUG2_RS_HIDEH"); hlo = 0; if (e && *e) { hlo = (uint32_t)strtoul(e, NULL, 16); const char *c2 = strchr(e, ':'); hhi = c2 ? (uint32_t)strtoul(c2 + 1, NULL, 16) : 0; } }
                if (q < 0 && hhi && so->hash >= hlo && so->hash < hhi) {
                    static int hn; if (hn++ < 40) printf("rstream hide: %s solid %08x mesh %d key %08x\n", so->sec->name, so->hash, (int)(m - so->sec->mesh) - so->m0, m->b.texkey);
                    continue;
                }
            }
#endif
            if (q >= 0) {
                if (g_ndef[q] < RS_MAXDEF) {
                    RsDef *d = &g_def[q][g_ndef[q]++]; d->m = m; d->vbo = m->b.vbo; d->ibo = m->b.ibo;
                    d->ioff = m->b.ioff; d->count = m->b.index_count; d->mtx = R->mtx;
                    d->row = q == 2 && m->tex >= 0 ? g_tex[m->tex].order : 0;
                }
                continue;
            }
#ifdef OPENUG2_SIM_PRE_H
            {   /* test aid (simulator): OPENUG2_RS_KEY=<texkey hex> logs (once) and hides those meshes */
                static uint32_t dk = 1; static int dn;
                if (dk == 1) { const char *e = getenv("OPENUG2_RS_KEY"); dk = e ? (uint32_t)strtoul(e, NULL, 16) : 0; }
                if (dk && m->b.texkey == dk) {
                    if (dn++ < 8) {
                        int mi = (int)(m - so->sec->mesh);
                        printf("rstream key %08x: section %s solid %08x (%d polys) mesh %d of %d, %u indices, box %.1f %.1f %.1f .. %.1f %.1f %.1f, mtx %d, tex state %d mode %d exact %d glow %d\n",
                               dk, so->sec->name, so->hash, so->polys, mi - so->m0, so->nm, m->b.index_count,
                               m->lo[0], m->lo[1], m->lo[2], m->hi[0], m->hi[1], m->hi[2], R->mtx,
                               m->tex >= 0 ? g_tex[m->tex].state : -1, md, m->mat_exact, m->glow);
                        if (R->mtx >= 0) { const float *M = g_mtx[R->mtx]; printf("  M %.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f | %.1f %.1f %.1f\n", M[0],M[1],M[2],M[4],M[5],M[6],M[8],M[9],M[10],M[12],M[13],M[14]); }
                    }
                    continue;
                }
            }
#endif
            rs_set_mtx(c, R->mtx);
            rs_bind_tex(c, m, &m->b);
            draw_batch(&m->b); draws++; g_st_ident += R->mtx < 0;
        }
    }
    /* baked runs: consecutive visible pieces of one draw state in one page are
       one index range */
    for (int t = 0; t < g_ntouched; t++) {
        RsScen *sc = g_touched[t];
        const RsPiece *run = NULL; unsigned rcount = 0;
        for (int q = 0; q <= sc->npc; q++) {
            const RsPiece *pc = q < sc->npc ? &sc->pc[q] : NULL;
            int on = pc && sc->vis[pc->inst] == pc->lod + 1;
            if (on) g.st_bpieces++;
            if (run && on && pc->page == run->page && pc->ioff == run->ioff + rcount * 2 && rs_piece_same(pc->src, run->src)) {
                rcount += pc->count; continue;
            }
            if (run) {   /* flush */
                const RsMesh *m = run->src; const RsPage *bp = &sc->sec->bpage[run->page];
                if (bp->vbo) {
                    unsigned char md = rs_mesh_mode(m);
                    int cq = m->glow ? 3 : md == N2_DRAW_BLEND || md == N2_DRAW_ADD ? 2 : md == N2_DRAW_CUTOUT ? 1 : m->reflect && c->refl_ready ? 0 : -1;
                    if (aux && (cq == 1 || draws >= c->max_draws)) cq = -9;
#ifdef OPENUG2_SIM_PRE_H
                    static int hq = -9; if (hq == -9) { const char *e = getenv("OPENUG2_RS_HIDEQ"); hq = e && *e ? atoi(e) : -5; }
                    if (hq == cq) cq = -9;
                    /* OPENUG2_RS_HIDEK=<lo>:<hi> (hex) hides baked pieces whose texture key is lo <= key < hi */
                    static uint32_t klo = 1, khi = 0;
                    if (klo == 1 && khi == 0) { const char *e = getenv("OPENUG2_RS_HIDEK"); klo = 0; if (e && *e) { klo = (uint32_t)strtoul(e, NULL, 16); const char *c2 = strchr(e, ':'); khi = c2 ? (uint32_t)strtoul(c2 + 1, NULL, 16) : 0; } }
                    if (khi && m->b.texkey >= klo && m->b.texkey < khi) {
                        static int kn; if (kn++ < 40) printf("rstream hidek: %s key %08x tex %d state %d mode %d count %u\n", sc->sec->name, m->b.texkey,
                                                            m->tex, m->tex >= 0 ? g_tex[m->tex].state : -1, md, rcount);
                        cq = -9;
                    }
#endif
                    if (cq >= 0) {
                        if (g_ndef[cq] < RS_MAXDEF) {
                            RsDef *d = &g_def[cq][g_ndef[cq]++]; d->m = m; d->vbo = bp->vbo; d->ibo = bp->ibo;
                            d->ioff = run->ioff; d->count = rcount; d->mtx = -1;
                            d->row = cq == 2 && m->tex >= 0 ? g_tex[m->tex].order : 0;
                        }
                    } else if (cq == -1) {
                        N2Batch b = m->b; b.vbo = bp->vbo; b.ibo = bp->ibo; b.ioff = run->ioff; b.index_count = rcount;
                        rs_set_mtx(c, -1); rs_bind_tex(c, m, &b);
                        draw_batch(&b); draws++; g_st_ident++;
                    }
                    g.st_bruns++;
                }
                run = NULL;
            }
            if (on) { run = pc; rcount = pc->count; }
        }
        memset(sc->vis, 0, (size_t)sc->ninst); sc->touched = 0;
    }
    /* worldreflect (row 0, effect 2) */
    if (g_ndef[0]) {
        vita_set_reflect(1);
        for (int q = 0; q < g_ndef[0]; q++) {
            const RsDef *d = &g_def[0][q]; N2Batch b; rs_def_batch(d, &b);
            rs_set_mtx(c, d->mtx); rs_bind_tex(c, d->m, &b); draw_batch(&b); draws++;
        }
        vita_set_reflect(0);
    }
    /* alpha test */
    if (g_ndef[1]) {
        glUniform1f(c->rp->uAlphaTest, 1.0f);
        for (int q = 0; q < g_ndef[1]; q++) {
            const RsDef *d = &g_def[1][q]; N2Batch b; rs_def_batch(d, &b);
            rs_set_mtx(c, d->mtx); rs_bind_tex(c, d->m, &b); draw_batch(&b); draws++;
        }
        glUniform1f(c->rp->uAlphaTest, 0.0f);
    }
    /* blended rows 1.. by sort class, submit order inside a row; depth write off */
    if (g_ndef[2]) {
        glEnable(GL_BLEND); glDepthMask(GL_FALSE);
        glUniform1f(c->rp->uTextureAlpha, 1.0f);
        int lastadd = -1;
        for (int row = 0; row < 16; row++)
            for (int q = 0; q < g_ndef[2]; q++) {
                const RsDef *d = &g_def[2][q];
                if (d->row != row) continue;
                int add = rs_mesh_mode(d->m) == N2_DRAW_ADD;
                if (add != lastadd) { glBlendFunc(GL_SRC_ALPHA, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); lastadd = add; }
                N2Batch b; rs_def_batch(d, &b);
                rs_set_mtx(c, d->mtx); rs_bind_tex(c, d->m, &b); draw_batch(&b); draws++;
            }
        glUniform1f(c->rp->uTextureAlpha, 0.0f);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    /* light emitters (N2_GLOW: neon, bulbs, flares): render_world_glows' state */
    if (g_ndef[3]) {
        float fog[3]; glGetUniformfv(c->rp->prog, c->rp->uFogColor, fog);
        glUniform1f(c->rp->uAlpha, 1); glUniform1f(c->rp->uSoft, 0);
        glUniform3f(c->rp->uFogColor, 0, 0, 0);
        glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int q = 0; q < g_ndef[3]; q++) {
            const RsDef *d = &g_def[3][q]; N2Batch b; rs_def_batch(d, &b);
            rs_set_mtx(c, d->mtx);
            rs_bind_tex(c, d->m, &b);
            GLuint t = b.tex;
            glUniform1f(c->rp->uUnlit, t ? 0 : 1); glUniform1f(c->rp->uEmissiveTex, t ? 1 : 0);
            glUniform3f(c->rp->uColor, 1, t ? 1 : .85f, t ? 1 : .5f);
            draw_batch(&b); draws++;
        }
        glUniform3fv(c->rp->uFogColor, 1, fog);
        glUniform1f(c->rp->uEmissiveTex, 0); glUniform1f(c->rp->uUnlit, 0);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    rs_set_mtx(c, -1);
    if (aux) {
        memcpy(g.st_cull, aux_cull, sizeof aux_cull);
        g.st_binst = aux_b[0]; g.st_bruns = aux_b[1]; g.st_bpieces = aux_b[2];
        return draws;
    }
    Uint64 t2 = SDL_GetPerformanceCounter();

    /* stats, every 600 frames */
    g.st_draws += draws; g.st_inst += g_nrec; g.st_frames++;
    g.st_draw_ms += (double)(t2 - t0) * 1000.0 / (double)f;
    static double cull_ms; cull_ms += (double)(t1 - t0) * 1000.0 / (double)f;
    if (g.st_frames >= 600) {
        double F = (double)g.st_frames;
        int nact = 0, nbuilt = 0, nq = 0, nwant = 0, nmiss = 0; long gpu = 0;
        for (int i = 0; i < g.nsec; i++) {
            const RsSec *s = &g.sec[i];
            nact += s->state == 4; nbuilt += s->state == 3; nq += s->state == 1 || s->state == 2;
            nwant += s->wanted; nmiss += s->wanted && s->state != 4 && rs_letter(s->number) != 25;
            gpu += s->bytes_gpu;
        }
        int ntex = 0; for (int i = 0; i < RS_TEXCAP; i++) ntex += g_tex[i].state == 3;
        long bakev = 0, unb = 0, xcpu = 0;
        for (int i = 0; i < g.nsec; i++) {
            const RsSec *s = &g.sec[i];
            if (s->state != 4) continue;
            bakev += s->bake_v; unb += s->unbaked;
            if (rs_letter(s->number) == 24) for (int k = 0; k < s->npage; k++) xcpu += (long)s->page[k].nv * (long)sizeof(BatchedVertex) + (long)s->page[k].ni * 2;
        }
        printf("rstream bake: %.0f baked instances/frame drawn as %.0f runs (%.0f pieces) | %ld KB baked vertices resident, %ld instances unbaked, X library CPU copy %ld KB\n",
               g.st_binst / F, g.st_bruns / F, g.st_bpieces / F, bakev * (long)sizeof(BatchedVertex) / 1024, unb, xcpu / 1024);
        g.st_binst = g.st_bruns = g.st_bpieces = 0;
        printf("rstream: zone %d (pred %d) | %.0f draws %.0f instances/frame, scenery %d hdrs | draw %.2f ms (cull %.2f) update %.2f ms (uploads %.2f) "
               "| culled/frame: groups %.0f props %.0f precull %.0f excl %.0f frustum %.0f pixels %.0f model-missing %.0f full %.0f "
               "| switches/frame: matrix %.0f texture %.0f, identity draws %.0f | sections: wanted %d active %d built %d queued %d, wanted-not-in %d, unloads %ld | GPU %ld KB, textures %d (+%ld, %ld KB)\n",
               g.zone, g.pzone, g.st_draws / F, g.st_inst / F, nl, g.st_draw_ms / F, cull_ms / F, g.st_upd_ms / F, g.st_up_ms / F,
               g.st_cull[7] / F, g.st_cull[6] / F, g.st_cull[0] / F, g.st_cull[1] / F, g.st_cull[2] / F, g.st_cull[3] / F, g.st_cull[4] / F, g.st_cull[5] / F, g_st_mtxsw / F, g_st_texsw / F, g_st_ident / F,
               nwant, nact, nbuilt, nq, nmiss, g.st_unload, gpu / 1024, ntex, g.st_tex_up, g.tex_gpu / 1024);
        g.st_draws = g.st_inst = g.st_frames = 0; memset(g.st_cull, 0, sizeof g.st_cull);
        printf("rstream: decoded backlog %ld KB now, %ld KB peak, worker waited %ld ms\n",
               g.dec_bytes / 1024, g.dec_peak / 1024, g.st_dec_wait_ms);
        g.st_draw_ms = g.st_upd_ms = g.st_up_ms = cull_ms = 0; g.st_tex_up = 0; g_st_mtxsw = g_st_texsw = g_st_ident = 0;
    }
    return draws;
}

#else   /* not the Vita build: the streamer is off */
int rstream_enabled(void) { return 0; }
int rstream_track_ok(const char *t) { (void)t; return 0; }
int rstream_active(void) { return 0; }
N2Batch *rstream_sky(int p, int *n) { (void)p; *n = 0; return NULL; }
int rstream_prop_place(uint32_t m, const float M[16], N2Scene *d) { (void)m; (void)M; (void)d; return 0; }
int rstream_prop_gen(void) { return 0; }
int rstream_start(const char *r, const char *t) { (void)r; (void)t; return 0; }
void rstream_set_event(int e) { (void)e; }
void rstream_stop(void) {}
void rstream_park_for(const char *t) { (void)t; }
void rstream_update(const float cam[3]) { (void)cam; }
int rstream_draw(const RsDrawCtx *c) { (void)c; return 0; }
int rstream_area_ready(int *left) { if (left) *left = 0; return 1; }
int rstream_cold(void) { return 0; }
void rstream_rewarm(void) {}
#endif
