/* Always-resident skyline: see skyline.h. */
#include "skyline.h"
#include "world_tiles.h"
#include "beyond.h"
#include "xpcity.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define SKYLINE_MIN_H   30.0f    /* tilestat 11-14: >= 30 m = 7.8 MB, 372 textures, city-wide */
#define SKYLINE_CELL_M  128.0f   /* the inner-edge test unit (= the tile size) */
#define SKYLINE_BLOCK   2        /* cells per block side: a block (256 m) is one buffer and, wholly past the inner edge, one draw */
#define SKYLINE_INNER_M 450.0f   /* a cell is drawn only when all of it is past this */
#define SKYLINE_OUTER_M 1500.0f
#define SKYLINE_FILE_VER 1
#define SKYLINE_MAXV    65535
/* the World Expansion (beyond.h) is ~12x the city: a higher cut, then the
   tallest stand-ins up to the city's own GPU size (7.8 MB at 30 m) */
#define SKYLINE_BEYOND_MIN_H  45.0f
#define SKYLINE_BEYOND_BUDGET (6L << 20)   /* cache bytes; the GPU copy is ~1.3x (10.8 MB at 8 MB) */
/* ux0:data/NFSU2/skyminh.txt "<m>" or OPENUG2_SKYMINH: the height cut (A/B runs;
   another height has its own cache file) */
static int sky_beyond(void);
static int sky_xp(void);   /* Rockport / Palmont: as big, the same cut and budget */
static float sky_min_h(void) {
    if (sky_beyond() || sky_xp()) return SKYLINE_BEYOND_MIN_H;   /* (own cache file name, by the height) */
    static float v = -1.0f;
    if (v < 0.0f) {
        v = SKYLINE_MIN_H;
        const char *e = getenv("OPENUG2_SKYMINH"); float x;
        FILE *f = fopen("ux0:data/NFSU2/skyminh.txt", "r");
        if (f) { if (fscanf(f, "%f", &x) == 1 && x >= 10.0f && x <= 80.0f) v = x; fclose(f); }
        else if (e && (x = (float)atof(e)) >= 10.0f && x <= 80.0f) v = x;
    }
    return v;
}

typedef struct { float p[3]; signed char n[4]; unsigned char c[4]; } SkyV;   /* 20 B, the cache's own layout */
typedef struct { uint32_t texkey, nv, ni; float bb[6]; } SkyMesh;
typedef struct { int m0, m1, nv, ni; float bb[6]; int sub_i[SKYLINE_BLOCK*SKYLINE_BLOCK + 1];
                 float sub_bb[SKYLINE_BLOCK*SKYLINE_BLOCK][6]; } SkyRun;   /* meshes [m0,m1): one buffer; sub_i = its cells' index offsets */

static struct {
    char track[64];
    SDL_Thread *thread;
    SDL_atomic_t state;          /* 0 idle, 1 loading, 2 ready to upload, 3 uploading/done, -1 failed */
    SDL_atomic_t abort;
    SkyMesh *mesh; int nmesh, cmesh;
    SkyV *v; long nv, cv;
    uint16_t *ix; long ni, ci;
    long *voff, *ioff;           /* per mesh, into v / ix */
    int *order;                  /* meshes sorted by cell */
    SkyRun *run; int nrun, next_run;
    N2Batch *batch; int nbatch;
    SkyRun *bsub;                /* per batch: its cells' index ranges and boxes */
    int off;                     /* noskyline.txt */
    unsigned t0;
    int built;                   /* 1: built from the tiles this session (not the cache) */
} S;

/* ---- texture mean colours (key -> rgb), kept in cache/tiles/texavg.bin ---- */
#define TA_CAP 8192
static struct { uint32_t key; unsigned char rgb[3], set; } g_ta[TA_CAP];
static int g_ta_n, g_ta_dirty, g_ta_loaded;
static const char *TA_PATH = WTILE_DIR "/texavg.bin";

static int ta_find(uint32_t key, int add) {
    if (!key) return -1;
    for (uint32_t h = (key * 2654435761u) & (TA_CAP - 1), q = 0; q < TA_CAP; q++, h = (h + 1) & (TA_CAP - 1)) {
        if (g_ta[h].set && g_ta[h].key == key) return (int)h;
        if (!g_ta[h].set) {
            if (!add || g_ta_n >= TA_CAP * 3 / 4) return -1;
            g_ta[h].key = key; g_ta[h].set = 1; g_ta_n++; return (int)h;
        }
    }
    return -1;
}
static void ta_load(void) {
    if (g_ta_loaded) return;
    g_ta_loaded = 1;
    FILE *f = fopen(TA_PATH, "rb");
    if (!f) return;
    unsigned char r[8];
    while (fread(r, 8, 1, f) == 1) {
        uint32_t k; memcpy(&k, r, 4);
        int h = ta_find(k, 1);
        if (h >= 0) memcpy(g_ta[h].rgb, r + 4, 3);
    }
    fclose(f);
    g_ta_dirty = 0;
}
static void ta_save(void) {
    if (!g_ta_dirty) return;
    FILE *f = fopen(TA_PATH, "wb");
    if (!f) return;
    for (int i = 0; i < TA_CAP; i++) if (g_ta[i].set) {
        unsigned char r[8]; memcpy(r, &g_ta[i].key, 4); memcpy(r + 4, g_ta[i].rgb, 3); r[7] = 0;
        fwrite(r, 8, 1, f);
    }
    fclose(f);
    g_ta_dirty = 0;
}
static void rgb565(uint16_t c, int *r, int *g, int *b) {
    *r = ((c >> 11) & 31) * 255 / 31; *g = ((c >> 5) & 63) * 255 / 63; *b = (c & 31) * 255 / 31;
}
void skyline_texavg_note(uint32_t key, const N2Tex *t) {
    if (!key || !t || t->w <= 0 || t->h <= 0) return;
    ta_load();
    int h = ta_find(key, 0);
    if (h >= 0) return;   /* known (a key's pixels never change within a bundle) */
    long sr = 0, sg = 0, sb = 0, n = 0;
    if (t->rgb) {
        long px = (long)t->w * t->h, step = px / 1024 + 1;
        for (long i = 0; i < px; i += step) {
            if (t->alpha && t->alpha[i] < 128) continue;
            sr += t->rgb[i*3]; sg += t->rgb[i*3+1]; sb += t->rgb[i*3+2]; n++;
        }
    } else if (t->dxt && (t->dxtfmt == 1 || t->dxtfmt == 3)) {
        int bs = t->dxtfmt == 1 ? 8 : 16, co = t->dxtfmt == 1 ? 0 : 8;
        long nb = (long)((t->w + 3) / 4) * ((t->h + 3) / 4);
        if ((long)t->dxtlen < nb * bs) nb = t->dxtlen / bs;
        long step = nb / 1024 + 1;
        for (long i = 0; i < nb; i += step) {
            const unsigned char *b = t->dxt + i * bs + co;
            uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
            int r0, g0, b0, r1, g1, b1; rgb565(c0, &r0, &g0, &b0); rgb565(c1, &r1, &g1, &b1);
            sr += r0 + r1; sg += g0 + g1; sb += b0 + b1; n += 2;
        }
    }
    if (!n) return;
    h = ta_find(key, 1);
    if (h < 0) return;
    g_ta[h].rgb[0] = (unsigned char)(sr / n); g_ta[h].rgb[1] = (unsigned char)(sg / n); g_ta[h].rgb[2] = (unsigned char)(sb / n);
    g_ta_dirty = 1;
}

/* ---- the loader thread ---- */
static int grow(void **p, long *cap, long need, size_t el) {
    if (need <= *cap) return 1;
    long c = *cap ? *cap : 4096;
    while (c < need) c *= 2;
    void *q = realloc(*p, (size_t)c * el);
    if (!q) return 0;
    *p = q; *cap = c; return 1;
}
static void collect(void *ctx, const N2Mesh *m) {
    (void)ctx;
    if (!m->verts || !m->idx || m->nverts <= 0 || m->nidx <= 0 || m->nverts > SKYLINE_MAXV) return;
    long cm = S.cmesh;
    if (!grow((void **)&S.mesh, &cm, S.nmesh + 1, sizeof *S.mesh)) return;
    S.cmesh = (int)cm;
    if (!grow((void **)&S.v, &S.cv, S.nv + m->nverts, sizeof *S.v)) return;
    if (!grow((void **)&S.ix, &S.ci, S.ni + m->nidx, sizeof *S.ix)) return;
    float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
    if (!nor) return;
    render_mesh_normals(m, nor);
    SkyMesh *o = &S.mesh[S.nmesh++];
    o->texkey = m->texkey; o->nv = (uint32_t)m->nverts; o->ni = (uint32_t)m->nidx;
    memcpy(o->bb, m->world_bounds, sizeof o->bb);
    for (int i = 0; i < m->nverts; i++) {
        SkyV *d = &S.v[S.nv + i]; const float *p = m->verts + i * 5;
        d->p[0] = p[0]; d->p[1] = p[1]; d->p[2] = p[2];
        for (int c = 0; c < 3; c++) d->n[c] = bv_snorm8(nor[i*3 + c]);
        d->n[3] = 0;
        if (m->vcol) memcpy(d->c, m->vcol + i * 4, 4);
        else d->c[0] = d->c[1] = d->c[2] = d->c[3] = 255;
    }
    memcpy(S.ix + S.ni, m->idx, (size_t)m->nidx * 2);
    S.nv += m->nverts; S.ni += m->nidx;
    free(nor);
}
static int sky_beyond(void) { return beyond_is_track(S.track); }
static int sky_xp(void) { return xpcity_is_city(S.track); }
/* keep the tallest stand-ins whose vertices + indices fit `budget` bytes (in their collected order) */
static float *g_trim_h;
static int trim_cmp(const void *a, const void *b) {
    float x = g_trim_h[*(const int *)a], y = g_trim_h[*(const int *)b];
    return x > y ? -1 : x < y ? 1 : *(const int *)a - *(const int *)b;
}
static void trim_budget(long budget) {
    long total = S.nv * (long)sizeof(SkyV) + S.ni * 2;
    if (total <= budget || S.nmesh <= 0) return;
    int *ord = (int *)malloc((size_t)S.nmesh * sizeof *ord);
    float *h = (float *)malloc((size_t)S.nmesh * sizeof *h);
    unsigned char *keep = (unsigned char *)calloc((size_t)S.nmesh, 1);
    if (!ord || !h || !keep) { free(ord); free(h); free(keep); return; }
    for (int i = 0; i < S.nmesh; i++) { ord[i] = i; h[i] = S.mesh[i].bb[5] - S.mesh[i].bb[2]; }
    g_trim_h = h;
    qsort(ord, (size_t)S.nmesh, sizeof *ord, trim_cmp);
    long used = 0; float cut = 0.0f;
    for (int k = 0; k < S.nmesh; k++) {
        const SkyMesh *m = &S.mesh[ord[k]];
        long b = (long)m->nv * (long)sizeof(SkyV) + (long)m->ni * 2;
        if (used + b > budget) continue;
        used += b; keep[ord[k]] = 1; cut = h[ord[k]];
    }
    int nm = 0; long vo = 0, io = 0, wv = 0, wi = 0;
    for (int i = 0; i < S.nmesh; i++) {
        const SkyMesh m = S.mesh[i];
        if (keep[i]) {
            memmove(S.v + wv, S.v + vo, (size_t)m.nv * sizeof *S.v);
            memmove(S.ix + wi, S.ix + io, (size_t)m.ni * 2);
            S.mesh[nm++] = m; wv += m.nv; wi += m.ni;
        }
        vo += m.nv; io += m.ni;
    }
    printf("skyline: %d of %d stand-ins kept for a %ld KB budget (%ld KB before; shortest kept %.0f m)\n",
           nm, S.nmesh, budget >> 10, total >> 10, (double)cut);
    S.nmesh = nm; S.nv = wv; S.ni = wi;
    free(ord); free(h); free(keep);
}
static void cache_path(char *out, size_t n) {
    if (sky_beyond())   /* the budget is in the name: another budget builds again */
        snprintf(out, n, "%s/%s_sky_v%d_%d_h%d_b%ld.bin", WTILE_DIR, S.track, WTILE_BEYOND_VERSION, SKYLINE_FILE_VER,
                 (int)sky_min_h(), SKYLINE_BEYOND_BUDGET >> 10);
    else if (sky_xp())
        snprintf(out, n, "%s/%s_sky_v%d_%d_h%d_b%ld.bin", WTILE_DIR, S.track, WTILE_VERSION, SKYLINE_FILE_VER,
                 (int)sky_min_h(), SKYLINE_BEYOND_BUDGET >> 10);
    else if (sky_min_h() == SKYLINE_MIN_H) snprintf(out, n, "%s/%s_sky_v%d_%d.bin", WTILE_DIR, S.track, WTILE_VERSION, SKYLINE_FILE_VER);
    else snprintf(out, n, "%s/%s_sky_v%d_%d_h%d.bin", WTILE_DIR, S.track, WTILE_VERSION, SKYLINE_FILE_VER, (int)sky_min_h());
}
/* an expansion city translated again (xpcity.c): its stand-ins carry the old vertex colours */
void skyline_forget(const char *track) {
    char p[256];
    snprintf(p, sizeof p, "%s/%s_sky_v%d_%d.bin", WTILE_DIR, track, WTILE_VERSION, SKYLINE_FILE_VER); remove(p);
    snprintf(p, sizeof p, "%s/%s_sky_v%d_%d_h%d.bin", WTILE_DIR, track, WTILE_VERSION, SKYLINE_FILE_VER, (int)sky_min_h()); remove(p);
    snprintf(p, sizeof p, "%s/%s_sky_v%d_%d_h%d_b%ld.bin", WTILE_DIR, track, WTILE_VERSION, SKYLINE_FILE_VER,
             (int)SKYLINE_BEYOND_MIN_H, SKYLINE_BEYOND_BUDGET >> 10); remove(p);
}
static int cache_read(void) {
    char path[256]; cache_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t h[6];
    int ok = fread(h, sizeof h, 1, f) == 1 && h[0] == 0x31594b53u /* SKY1 */ && h[1] == SKYLINE_FILE_VER &&
             h[2] == (uint32_t)(sky_min_h() * 10.0f) && h[3] < 1000000u && h[4] < 20000000u && h[5] < 60000000u;
    if (ok) {
        S.nmesh = S.cmesh = (int)h[3]; S.nv = S.cv = (long)h[4]; S.ni = S.ci = (long)h[5];
        S.mesh = (SkyMesh *)malloc((size_t)S.nmesh * sizeof *S.mesh + 1);
        S.v = (SkyV *)malloc((size_t)S.nv * sizeof *S.v + 1);
        S.ix = (uint16_t *)malloc((size_t)S.ni * 2 + 1);
        ok = S.mesh && S.v && S.ix &&
             fread(S.mesh, sizeof *S.mesh, (size_t)S.nmesh, f) == (size_t)S.nmesh &&
             fread(S.v, sizeof *S.v, (size_t)S.nv, f) == (size_t)S.nv &&
             fread(S.ix, 2, (size_t)S.ni, f) == (size_t)S.ni;
    }
    fclose(f);
    if (!ok) { free(S.mesh); free(S.v); free(S.ix); S.mesh = NULL; S.v = NULL; S.ix = NULL;
               S.nmesh = S.cmesh = 0; S.nv = S.cv = S.ni = S.ci = 0; }
    return ok;
}
static void cache_write(void) {
    char path[256], tmp[260]; cache_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    uint32_t h[6] = { 0x31594b53u, SKYLINE_FILE_VER, (uint32_t)(sky_min_h() * 10.0f),
                      (uint32_t)S.nmesh, (uint32_t)S.nv, (uint32_t)S.ni };
    int ok = fwrite(h, sizeof h, 1, f) == 1 &&
             fwrite(S.mesh, sizeof *S.mesh, (size_t)S.nmesh, f) == (size_t)S.nmesh &&
             fwrite(S.v, sizeof *S.v, (size_t)S.nv, f) == (size_t)S.nv &&
             fwrite(S.ix, 2, (size_t)S.ni, f) == (size_t)S.ni;
    if (fclose(f) != 0) ok = 0;
    remove(path);
    if (ok) rename(tmp, path); else remove(tmp);
}
/* block coordinates, and the cell within the block (0..SKYLINE_BLOCK^2-1) */
static int cell_of(const SkyMesh *m, int *bx, int *by) {
    int cx = (int)floorf(0.5f * (m->bb[0] + m->bb[3]) / SKYLINE_CELL_M);
    int cy = (int)floorf(0.5f * (m->bb[1] + m->bb[4]) / SKYLINE_CELL_M);
    *bx = cx >= 0 ? cx / SKYLINE_BLOCK : -((-cx + SKYLINE_BLOCK - 1) / SKYLINE_BLOCK);
    *by = cy >= 0 ? cy / SKYLINE_BLOCK : -((-cy + SKYLINE_BLOCK - 1) / SKYLINE_BLOCK);
    return (cy - *by * SKYLINE_BLOCK) * SKYLINE_BLOCK + (cx - *bx * SKYLINE_BLOCK);
}
static int order_cmp(const void *a, const void *b) {
    const SkyMesh *x = &S.mesh[*(const int *)a], *y = &S.mesh[*(const int *)b];
    int ax, ay, bx, by; int as = cell_of(x, &ax, &ay), bs = cell_of(y, &bx, &by);
    if (ay != by) return ay < by ? -1 : 1;
    if (ax != bx) return ax < bx ? -1 : 1;
    if (as != bs) return as < bs ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}
static int plan(void) {
    S.voff = (long *)malloc((size_t)(S.nmesh + 1) * sizeof *S.voff);
    S.ioff = (long *)malloc((size_t)(S.nmesh + 1) * sizeof *S.ioff);
    S.order = (int *)malloc((size_t)(S.nmesh + 1) * sizeof *S.order);
    S.run = (SkyRun *)malloc((size_t)(S.nmesh + 1) * sizeof *S.run);
    if (!S.voff || !S.ioff || !S.order || !S.run) return 0;
    long vo = 0, io = 0;
    for (int i = 0; i < S.nmesh; i++) { S.voff[i] = vo; S.ioff[i] = io; vo += S.mesh[i].nv; io += S.mesh[i].ni; S.order[i] = i; }
    if (vo != S.nv || io != S.ni) return 0;
    qsort(S.order, (size_t)S.nmesh, sizeof *S.order, order_cmp);
    S.nrun = 0;
    for (int i0 = 0; i0 < S.nmesh;) {
        int cx, cy; cell_of(&S.mesh[S.order[i0]], &cx, &cy);
        SkyRun *r = &S.run[S.nrun++];
        r->m0 = i0; r->nv = 0; r->ni = 0;
        for (int c = 0; c < 3; c++) { r->bb[c] = 1e30f; r->bb[c+3] = -1e30f; }
        for (int q = 0; q < SKYLINE_BLOCK*SKYLINE_BLOCK; q++)
            for (int c = 0; c < 3; c++) { r->sub_bb[q][c] = 1e30f; r->sub_bb[q][c+3] = -1e30f; }
        int i = i0, sub_next = 0;
        for (; i < S.nmesh; i++) {
            const SkyMesh *m = &S.mesh[S.order[i]];
            int x, y, sub = cell_of(m, &x, &y);
            if (x != cx || y != cy || r->nv + (int)m->nv > SKYLINE_MAXV) break;
            while (sub_next <= sub) r->sub_i[sub_next++] = r->ni;   /* a cell starts where its first mesh does */
            r->nv += (int)m->nv; r->ni += (int)m->ni;
            for (int c = 0; c < 3; c++) {
                if (m->bb[c] < r->bb[c]) r->bb[c] = m->bb[c];
                if (m->bb[c+3] > r->bb[c+3]) r->bb[c+3] = m->bb[c+3];
                if (m->bb[c] < r->sub_bb[sub][c]) r->sub_bb[sub][c] = m->bb[c];
                if (m->bb[c+3] > r->sub_bb[sub][c+3]) r->sub_bb[sub][c+3] = m->bb[c+3];
            }
        }
        while (sub_next <= SKYLINE_BLOCK*SKYLINE_BLOCK) r->sub_i[sub_next++] = r->ni;
        r->m1 = i; i0 = i;
    }
    return 1;
}
static int loader(void *arg) {
    (void)arg;
    int ok = cache_read();
    if (!ok && !SDL_AtomicGet(&S.abort)) {
        int tiles = world_tiles_each_standin(S.track, sky_min_h(), collect, NULL, &S.abort);
        ok = tiles > 0 && S.nmesh > 0 && !SDL_AtomicGet(&S.abort);
        if (ok && (sky_beyond() || sky_xp())) trim_budget(SKYLINE_BEYOND_BUDGET);
        printf("skyline: built from %d tiles: %d stand-ins >= %.0f m, %ld verts, %ld KB\n", tiles, S.nmesh,
               (double)sky_min_h(), S.nv, (S.nv * (long)sizeof(SkyV) + S.ni * 2) >> 10);
        if (ok) { cache_write(); S.built = 1; }
    }
    if (ok && !SDL_AtomicGet(&S.abort)) ok = plan();
    SDL_AtomicSet(&S.state, ok && !SDL_AtomicGet(&S.abort) ? 2 : -1);
    return 0;
}

static void free_cpu(void) {
    free(S.mesh); free(S.v); free(S.ix); free(S.voff); free(S.ioff); free(S.order); free(S.run);
    S.mesh = NULL; S.v = NULL; S.ix = NULL; S.voff = S.ioff = NULL; S.order = NULL; S.run = NULL;
    S.nmesh = S.cmesh = 0; S.nv = S.cv = S.ni = S.ci = 0; S.nrun = S.next_run = 0;
}

void skyline_reset(void) {
    if (S.thread) { SDL_AtomicSet(&S.abort, 1); SDL_WaitThread(S.thread, NULL); S.thread = NULL; }
    for (int i = 0; i < S.nbatch; i++) { glDeleteBuffers(1, &S.batch[i].vbo); glDeleteBuffers(1, &S.batch[i].ibo); }
    free(S.batch); S.batch = NULL; S.nbatch = 0;
    free(S.bsub); S.bsub = NULL;
    free_cpu();
    ta_save();
    S.track[0] = 0;
    SDL_AtomicSet(&S.state, 0); SDL_AtomicSet(&S.abort, 0);
}

void skyline_start(const char *track) {
    if (S.off == 0) {
        FILE *f = fopen("ux0:data/NFSU2/noskyline.txt", "r");
        S.off = f || getenv("OPENUG2_NO_SKYLINE") ? 1 : 2;
        if (f) fclose(f);
        printf("skyline: %s\n", S.off == 1 ? "off (noskyline.txt)" : "on");
    }
    if (S.off == 1 || !track || !track[0]) return;
    if (!strcmp(S.track, track) && SDL_AtomicGet(&S.state) != 0) return;   /* this track's, loading or loaded */
    if (S.track[0]) skyline_reset();
    if (!world_tiles_enabled(track)) return;
    ta_load();
    snprintf(S.track, sizeof S.track, "%s", track);
    S.t0 = SDL_GetTicks(); S.built = 0;
    SDL_AtomicSet(&S.abort, 0);
    SDL_AtomicSet(&S.state, 1);
    S.thread = SDL_CreateThread(loader, "skyline", NULL);
    if (!S.thread) { SDL_AtomicSet(&S.state, -1); printf("skyline: no thread\n"); }
}

void skyline_pump(void) {
    int st = SDL_AtomicGet(&S.state);
    if (st == -1 && S.thread) {
        SDL_WaitThread(S.thread, NULL); S.thread = NULL; free_cpu();
        printf("skyline: not available for %s\n", S.track);
        return;
    }
    if (st != 2 && st != 3) return;
    if (st == 2) {
        SDL_WaitThread(S.thread, NULL); S.thread = NULL;
        S.batch = (N2Batch *)calloc((size_t)(S.nrun ? S.nrun : 1), sizeof *S.batch);
        S.bsub = (SkyRun *)malloc((size_t)(S.nrun ? S.nrun : 1) * sizeof *S.bsub);
        if (S.bsub) memcpy(S.bsub, S.run, (size_t)S.nrun * sizeof *S.bsub);
        if (!S.batch || !S.bsub) { SDL_AtomicSet(&S.state, -1); free_cpu(); return; }
        S.next_run = 0; S.nbatch = 0;
        SDL_AtomicSet(&S.state, 3);
    }
    if (!S.run) return;   /* done */
    /* a few cells a frame: one cell is at most 65535 x 28 B */
    BatchedVertex *bv = NULL; uint16_t *bi = NULL;
    for (int budget = 0; S.next_run < S.nrun && budget < 6; budget++) {
        const SkyRun *r = &S.run[S.next_run++];
        bv = (BatchedVertex *)realloc(bv, (size_t)r->nv * sizeof *bv);
        bi = (uint16_t *)realloc(bi, (size_t)r->ni * 2);
        if (!bv || !bi) break;
        int vo = 0, io = 0;
        for (int k = r->m0; k < r->m1; k++) {
            int mi = S.order[k]; const SkyMesh *m = &S.mesh[mi];
            int h = ta_find(m->texkey, 0);
            /* unseen texture: a dim neutral; night city facades average about this */
            int tr = h >= 0 ? g_ta[h].rgb[0] : 70, tg = h >= 0 ? g_ta[h].rgb[1] : 68, tb = h >= 0 ? g_ta[h].rgb[2] : 66;
            const SkyV *sv = S.v + S.voff[mi];
            for (uint32_t i = 0; i < m->nv; i++) {
                BatchedVertex *o = &bv[vo + (int)i];
                o->pos[0] = sv[i].p[0]; o->pos[1] = sv[i].p[1]; o->pos[2] = sv[i].p[2];
                o->uv[0] = o->uv[1] = 0.0f;
                BV_SET_NORMAL(o, sv[i].n[0] / 127.0f, sv[i].n[1] / 127.0f, sv[i].n[2] / 127.0f);
                o->col[0] = (unsigned char)(sv[i].c[0] * tr / 255); o->col[1] = (unsigned char)(sv[i].c[1] * tg / 255);
                o->col[2] = (unsigned char)(sv[i].c[2] * tb / 255); o->col[3] = 255;
            }
            const uint16_t *si = S.ix + S.ioff[mi];
            for (uint32_t t = 0; t < m->ni; t++) bi[io + (int)t] = (uint16_t)(si[t] + vo);
            vo += (int)m->nv; io += (int)m->ni;
        }
        N2Batch *b = &S.batch[S.nbatch];
        memset(b, 0, sizeof *b);
        glGenBuffers(1, &b->vbo); glGenBuffers(1, &b->ibo);
        glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vo * sizeof *bv), bv, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)io * 2), bi, GL_STATIC_DRAW);
        b->index_count = io; b->nmesh = r->m1 - r->m0; b->tall = 1; b->drawmode = N2_DRAW_OPAQUE;
        for (int c = 0; c < 3; c++) { b->bbox_min[c] = r->bb[c]; b->bbox_max[c] = r->bb[c+3]; }
        S.nbatch++;
    }
    free(bv); free(bi);
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (S.next_run >= S.nrun) {
        int known = 0;
        for (int i = 0; i < S.nmesh; i++) known += ta_find(S.mesh[i].texkey, 0) >= 0;
        printf("skyline: %s ready, %d blocks, %d stand-ins (%d with a texture colour), %ld KB GPU, %s, %u ms\n",
               S.track, S.nbatch, S.nmesh, known, (S.nv * (long)sizeof(BatchedVertex) + S.ni * 2) >> 10,
               S.built ? "built from the tiles" : "from the cache", SDL_GetTicks() - S.t0);
        free_cpu();
        ta_save();
    }
}

static float box_d2(const float cam[3], const float *lo, const float *hi) {
    float nx = cam[0] < lo[0] ? lo[0]-cam[0] : cam[0] > hi[0] ? cam[0]-hi[0] : 0;
    float ny = cam[1] < lo[1] ? lo[1]-cam[1] : cam[1] > hi[1] ? cam[1]-hi[1] : 0;
    return nx*nx + ny*ny;
}
static void draw_range(const N2Batch *b, int first, int count) {
    if (count <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchedVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchedVertex), (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, BV_NORMAL_GL_TYPE, BV_NORMAL_GL_NORM, sizeof(BatchedVertex),
                          (void*)offsetof(BatchedVertex, normal));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BatchedVertex),
                          (void*)offsetof(BatchedVertex, col));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, (void *)((size_t)first * 2));
}
int skyline_draw(const float cam[3], int (*in_view)(const float lo[3], const float hi[3], void *ctx),
                 void *ctx, void (*begin)(void *ctx)) {
    int n = SDL_AtomicGet(&S.state) == 3 ? S.nbatch : 0, draws = 0;
    float inner = skyline_inner(), in2 = inner * inner, out2 = SKYLINE_OUTER_M * SKYLINE_OUTER_M;
    enum { NS = SKYLINE_BLOCK * SKYLINE_BLOCK };
    for (int q = 0; q < n; q++) {
        const N2Batch *b = &S.batch[q]; const SkyRun *r = &S.bsub[q];
        if (box_d2(cam, b->bbox_min, b->bbox_max) > out2) continue;
        /* the block's cells wholly past the inner edge */
        int ok[NS], all = 1, anyc = 0;
        for (int c = 0; c < NS; c++) {
            int has = r->sub_i[c+1] > r->sub_i[c];
            ok[c] = has && box_d2(cam, r->sub_bb[c], r->sub_bb[c] + 3) >= in2;
            if (has && !ok[c]) all = 0;
            anyc |= ok[c];
        }
        if (!anyc || !in_view(b->bbox_min, b->bbox_max, ctx)) continue;
        if (!draws && begin) begin(ctx);
        if (all) { draw_range(b, 0, b->index_count); draws++; continue; }
        for (int c = 0; c < NS;) {   /* adjacent index ranges in one draw */
            if (!ok[c]) { c++; continue; }
            int e = c + 1;
            while (e < NS && (ok[e] || r->sub_i[e+1] == r->sub_i[e])) e++;
            draw_range(b, r->sub_i[c], r->sub_i[e] - r->sub_i[c]); draws++;
            c = e;
        }
    }
    return draws;
}

const N2Batch *skyline_batches(int *n) {
    if (n) *n = SDL_AtomicGet(&S.state) == 3 ? S.nbatch : 0;
    return S.batch;
}
/* ux0:data/NFSU2/skyinner.txt "<m>" or OPENUG2_SKYINNER: the inner edge (A/B runs) */
float skyline_inner(void) {
    static float v = -1.0f;
    if (v < 0.0f) {
        v = SKYLINE_INNER_M;
        const char *e = getenv("OPENUG2_SKYINNER"); float x;
        FILE *f = fopen("ux0:data/NFSU2/skyinner.txt", "r");
        if (f) { if (fscanf(f, "%f", &x) == 1 && x >= 100.0f && x <= 1000.0f) v = x; fclose(f); }
        else if (e && (x = (float)atof(e)) >= 100.0f && x <= 1000.0f) v = x;
        printf("skyline: inner edge %.0f m\n", (double)v);
    }
    return v;
}
float skyline_outer(void) { return SKYLINE_OUTER_M; }
