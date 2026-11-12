/* aciddraw.c -- draws the Acid particles as 0x48bdf0 does; see aciddraw.h.
 * Pass 0: every group's camera billboards (0x5c0e10 -> 0x5bb430), pass 1:
 * every group's plane quads (0x5c7e80). Effect worldnodepth.fx: colour =
 * 2 * texture * vertex colour, alpha = texture * vertex alpha, no fog; depth
 * test on, depth writes off (0x5b7de0 ZWRITEENABLE 0), RGB writes only
 * (0x5c57e0 COLORWRITEENABLE 7); blending from the texture's TPK record
 * (0x5bcc30: additive for blend 2, else source alpha). */
#include "aciddraw.h"
#include "acid.h"
#include "render.h"
#include <SDL.h>       /* SDL_GetPerformanceCounter: the flare lock's wait */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float p[3], uv[2]; unsigned char c[4]; } AVert;   /* 24 bytes, as 0x5bb430 */

static struct {
    GLuint prog, vbo, ibo;
    GLint uMVP, uTex;
    AVert *v; int cap;
    int ready;
    GLuint white;
    GLuint ftex[3]; int fblend[3];          /* light flare textures 0x82cc10 */
    GLuint wtex[3]; int wblend[3];          /* WINDOW_DAMAGE0..2 (GLOBALB): DamagedWindowHashes 1..3 */
} A;

/* One flare for eRenderLightFlare 0x490920: world position, world direction
   (eLightFlare +0x20 through the matrix), type (+0x2c), flags (+0x2d: 1 = two
   sided, 4 = pow falloff), intensity and the flareType (0 = the main view's
   world / car flares, 3 = the pool's). */
typedef struct { float p[3], d[3], k, car[3], ez; unsigned char type, flags, ft, hascar; } FlareQ;
#define FLARE_POOL 32     /* PoolOfFlares 0x82dc08: 2 lists */
#define FLARE_CARQ 2048   /* RenderFlares 0x61ae90: up to 99 cars, each light trail 24 more (R1) */
static FlareQ g_fq[FLARE_POOL], g_cq[FLARE_CARQ];
static int g_nfq, g_ncq;
/* the cars' flares of the last main pass, for the road reflection pass
   (drawn before the cars queue this frame's) */
static FlareQ g_rq[FLARE_CARQ];
static int g_nrq;

/* The world's light flares (bChunkLoaderLightFlares: 0x80135100 packs, one
   per track section: 0x135101 RunTimeLightFlarePackHeader of 0x60 bytes, the
   section's box at +0x30 / +0x40; 0x135102 the 0x30-byte eLightFlares after
   the 0x11 filler). eRenderWorldLightFlares 0x491860 walks the visible
   sections' packs, box-tests each and draws every flare with the identity
   matrix 0x872e90, intensity 1, no reflection and the pass's flareType. */
typedef struct { float p[3], d[3], ez; unsigned char type, flags, ezs; } WFlare;   /* ez/ezs: eLightFlare +0x1c */
typedef struct { float mn[3], mx[3]; int first, n, sec; } WPack;   /* sec: header +0x54 */
static struct {
    WFlare *f; int nf, capf; WPack *pk; int npk, cappk; int on;
    int *sec2pk, nsec, idx_npk;   /* section number -> pack (TheVisibleSectionManager +0x50 user info +8) */
    int (*vis)(const float eye[3], const short **list);
} W;
static struct { WFlare *f; int nf, capf; WPack *pk; int npk, cappk; } WS;   /* the loader's build set */
void aciddraw_world_flares_sections(int (*vis)(const float eye[3], const short **list)) { W.vis = vis; }

/* Port: the area whose buildings are placed (world_tiles: a tile's objects
   are in the scene when its centre lies within the build radius of the
   build focus; further tiles carry only road + terrain). Retail draws the
   whole city, so its flares always sit on a building; here a flare on a tile
   past the radius would hang in the sky over empty ground. r <= 0: no limit. */
static float g_wf_area[3];
void aciddraw_world_flares_area(float cx, float cy, float r) { g_wf_area[0] = cx; g_wf_area[1] = cy; g_wf_area[2] = r; }
static int wf_in_area(const float p[3]) {
    if (g_wf_area[2] <= 0.0f) return 1;
    float tx = (floorf(p[0] / 128.0f) + 0.5f) * 128.0f - g_wf_area[0];   /* WTILE_SIZE tile centre */
    float ty = (floorf(p[1] / 128.0f) + 0.5f) * 128.0f - g_wf_area[1];
    return tx * tx + ty * ty <= g_wf_area[2] * g_wf_area[2];
}

/* SingleLightFlareParameters (0x7f3a30.., 0x2c bytes) per flare type, two
   slots (0x7f3cc8): size at intensity 0 / 1, r g b a (0..255) at intensity
   1, pow exponent (flag 4), pull toward the eye, depth-scale clamp, texture.
   RenderFlares 0x61ae90 sets the headlight's first pow (0x7f3a48) to
   0x7f39f0 = 1.45 in the game (2.8 in the front end) and its sizes from
   0x7f39d8 / dc (0 / 64) each frame: the values below. */
typedef struct { float s0, s1, r, g, b, a, pw, pull, cmin, cmax; int tex; } FlareParm;
static const FlareParm k_fp_head[2] = {
    { 0, 64, 100, 120, 170, 80, 1.45f, 0.8f, 0.01f, 0.048f, 0 },
    { 28, 28, 100, 120, 140, 20, 3.0f, 0.8f, 0.01f, 0.015f, 1 } };
static const FlareParm k_fp[15] = {
    { 0 }, { 20, 30, 128, 25, 25, 120, 0.45f, 0.2f, 0.01f, 0.03f, 2 },
    { 20, 20, 255, 255, 255, 128, 1.2f, 0.2f, 0.005f, 0.01f, 2 }, { 20, 20, 128, 128, 128, 128, 1, 0.2f, 0.005f, 0.01f, 2 },
    { 30, 40, 128, 64, 64, 128, 1, 1, 0.01f, 0.02f, 0 }, { 30, 40, 64, 64, 128, 128, 1, 1, 0.01f, 0.02f, 0 },
    { 30, 40, 128, 128, 128, 128, 1, 1, 0.01f, 0.02f, 0 }, { 0 }, { 0 },
    { 30, 30, 110, 100, 75, 110, 1, 4, 0.001f, 0.1f, 2 }, { 10, 10, 110, 75, 0, 150, 1, 2.7f, 0.001f, 0.1f, 2 },
    { 10, 10, 110, 0, 0, 150, 1, 4, 0.001f, 0.1f, 2 }, { 10, 10, 0, 0, 120, 150, 1, 4, 0.001f, 0.1f, 2 },
    { 30, 30, 110, 75, 0, 150, 1, 4, 0.001f, 0.1f, 2 }, { 1, 10, 105, 26.5f, 0, 70, 1, 1, 0.07f, 0.07f, 2 } };

void aciddraw_light_flare(const float pos[3], int type)
{
    if (g_nfq >= FLARE_POOL || type < 0 || type > 14) return;
    FlareQ *q = &g_fq[g_nfq++];
    memset(q, 0, sizeof *q);
    memcpy(q->p, pos, sizeof q->p);
    q->type = (unsigned char)type; q->k = 1.0f; q->ft = 3;
    q->flags = 0;            /* eInitLightFlarePool 0x48ab60: list 1 flags 0 */
}

void aciddraw_car_flare(const float pos[3], const float dir[3], const float car[3], int type, float intensity)
{
    if (g_ncq >= FLARE_CARQ || type < 0 || type > 14 || !(intensity > 0.0f)) return;
    FlareQ *q = &g_cq[g_ncq++];
    memcpy(q->p, pos, sizeof q->p); memcpy(q->d, dir, sizeof q->d);
    q->type = (unsigned char)type; q->flags = 4;   /* CreateCarLightFlares: +0x2d = 4 */
    q->k = intensity > 1.0f ? 1.0f : intensity; q->ft = 0;
    memcpy(q->car, car, sizeof q->car); q->hascar = 1;
}

static uint32_t fl_u32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float fl_f32(const unsigned char *p) { float f; uint32_t u = fl_u32(p); memcpy(&f, &u, 4); return f; }

static void wf_pack(const unsigned char *d, long beg, long end)
{
    const unsigned char *hdr = NULL; long fo = 0, fn = 0;
    for (long o = beg; o + 8 <= end;) {
        uint32_t id = fl_u32(d + o), sz = fl_u32(d + o + 4);
        long p = o + 8;
        if ((long)sz > end - p) return;
        if (id == 0x135101u && sz >= 0x58) hdr = d + p;
        else if (id == 0x135102u) {
            long k = 0;
            while (k < (long)sz && d[p + k] == 0x11) k++;
            fo = p + k; fn = ((long)sz - k) / 0x30;
        }
        o = p + (long)sz;
    }
    if (!hdr || fn <= 0) return;
    if (WS.npk == WS.cappk) {
        int n = WS.cappk ? WS.cappk * 2 : 256;
        WPack *g = (WPack *)realloc(WS.pk, (size_t)n * sizeof *g);
        if (!g) return;
        WS.pk = g; WS.cappk = n;
    }
    if (WS.nf + fn > WS.capf) {
        int n = WS.capf ? WS.capf : 1024;
        while (n < WS.nf + fn) n *= 2;
        WFlare *g = (WFlare *)realloc(WS.f, (size_t)n * sizeof *g);
        if (!g) return;
        WS.f = g; WS.capf = n;
    }
    WPack *k = &WS.pk[WS.npk++];
    for (int j = 0; j < 3; j++) { k->mn[j] = fl_f32(hdr + 0x30 + 4 * j); k->mx[j] = fl_f32(hdr + 0x40 + 4 * j); }
    k->first = WS.nf; k->n = (int)fn;
    k->sec = (int)fl_u32(hdr + 0x54);
    for (long i = 0; i < fn; i++) {
        const unsigned char *r = d + fo + i * 0x30;
        WFlare *f = &WS.f[WS.nf++];
        for (int j = 0; j < 3; j++) { f->p[j] = fl_f32(r + 0x10 + 4 * j); f->d[j] = fl_f32(r + 0x20 + 4 * j); }
        f->type = r[0x2c]; f->flags = r[0x2d]; f->ez = 0.0f; f->ezs = 0;   /* +0x1c = 999: not yet looked up */
    }
}
static void wf_walk(const unsigned char *d, long beg, long end, int depth)
{
    for (long o = beg; o + 8 <= end;) {
        uint32_t id = fl_u32(d + o), sz = fl_u32(d + o + 4);
        long p = o + 8;
        if ((long)sz > end - p) return;
        if (id == 0x80135100u) wf_pack(d, p, p + (long)sz);
        else if ((id >> 31) && depth < 2 && id != 0x80134000u && (id >> 24) != 0xb3u) wf_walk(d, p, p + (long)sz, depth + 1);
        o = p + (long)sz;
    }
}
/* The packs are reset and added by the world loader's region hook, which
   runs on the resident worker ("world-prepare") while the render thread
   walks them (and writes the reflection heights). The worker builds into its
   own set (WS) and publishes a copy: the lock covers only the pointer and
   count swap, and the render walks (which hold it while they read). */
static volatile int g_wf_lock;
static double g_wf_wait_max[2];   /* worst wait for the lock, ms: [0] render, [1] loader (OPENUG2_FXLOG) */
static void wf_lock(int who)
{
    if (!__sync_lock_test_and_set(&g_wf_lock, 1)) return;
    Uint64 t0 = SDL_GetPerformanceCounter();
    while (__sync_lock_test_and_set(&g_wf_lock, 1)) { }
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (ms > g_wf_wait_max[who]) g_wf_wait_max[who] = ms;
}
#define WF_LOCK() wf_lock(0)
#define WF_UNLOCK() __sync_lock_release(&g_wf_lock)
static void wf_publish(void)
{
    WPack *pk = WS.npk ? (WPack *)malloc((size_t)WS.npk * sizeof *pk) : NULL;
    WFlare *f = WS.nf ? (WFlare *)malloc((size_t)WS.nf * sizeof *f) : NULL;
    int npk = pk ? WS.npk : 0, nf = f ? WS.nf : 0;
    if (!pk || !f) { free(pk); free(f); pk = NULL; f = NULL; npk = nf = 0; }
    if (npk) { memcpy(pk, WS.pk, (size_t)npk * sizeof *pk); memcpy(f, WS.f, (size_t)nf * sizeof *f); }
    wf_lock(1);
    WPack *opk = W.pk; WFlare *of = W.f;
    W.pk = pk; W.npk = npk; W.cappk = npk; W.f = f; W.nf = nf; W.capf = nf; W.idx_npk = -1;
    WF_UNLOCK();
    free(opk); free(of);
}
void aciddraw_world_flares_reset(void) { WS.nf = 0; WS.npk = 0; wf_publish(); }
int aciddraw_world_flares_add(const unsigned char *bundle, long len)
{
    int n0 = WS.nf;
    if (bundle && len > 8) wf_walk(bundle, 0, len, 0);
    int n = WS.nf - n0;
    wf_publish();
    return n;
}
void aciddraw_world_flares_enable(int on) { W.on = on; }

static void flush(GLuint tex, int blend, int nq);
static float clamp255(float c);

/* the flare pass's view: camera right / up / forward in world space, the
   eye, PV for the in-view test and the screen position */
typedef struct {
    const float *V, *PV; float proj, eye[3], R[3], U[3], F[3];
    GLuint btex; int bblend, nq, drawn;
    float stretch;   /* flareType 2: 0x7f39a0 */
} FlareCtx;

/* eRenderLightFlare 0x490920 for flareType 0 (the main view), 2 (the road
   reflection view: the flare at its mirrored height +0x1c, texture
   0x3394fe62 for both slots, the up axis stretched by 0x7f39a0, halved when
   the light is 4 m or more above its mirror (0x78d7b0)) and 3 / 4 (the
   pool), 1 (the car environment cube's views 16..21, below), reflection 0.
   Not ported: the pool's 3 / 4 car-facing fade (only type 0xe, the flag
   girl's, is pooled here, and 0xd / 0xe skip it).
   flareType 1 (0x490cb6): the in-view test is the box P +- 0x7f3d4c (2.0)
   against the view (0x43d540 / 0x43d610 / 0x5bc300) instead of the point
   test, the squared distance fade is 0x78d7a8 / 0x78d7ac (3900 .. 4000)
   and the flare's direction is never loaded (0x487120 is skipped, its slot
   [esp+0x30] stays the zero set at 0x490b31), so the facing dot is 0. */
static void flare_one(FlareCtx *c, const FlareQ *q)
{
    float Pm[3] = { q->p[0], q->p[1], q->ez };
    const float *P = q->ft == 2 ? Pm : q->p, *V = c->V, *M = c->PV;
    {   /* the distance cut below, tested first (same result, cheaper) */
        float ex = c->eye[0] - P[0], ey = c->eye[1] - P[1], ez = c->eye[2] - P[2];
        if (ex * ex + ey * ey + ez * ez > (q->ft == 1 ? 4000.0f : 40000.0f)) return;
    }
    float depth = -(V[2] * P[0] + V[6] * P[1] + V[10] * P[2] + V[14]);
    float cx = M[0] * P[0] + M[4] * P[1] + M[8] * P[2] + M[12];
    float cy = M[1] * P[0] + M[5] * P[1] + M[9] * P[2] + M[13];
    float cw = M[3] * P[0] + M[7] * P[1] + M[11] * P[2] + M[15];
    if (q->ft == 1) {
        /* 0x5bc300: the box in view = not wholly outside one of the planes */
        int out[6] = { 0, 0, 0, 0, 0, 0 };
        for (int n = 0; n < 8; n++) {
            float b[3] = { P[0] + (n & 1 ? 2.0f : -2.0f), P[1] + (n & 2 ? 2.0f : -2.0f), P[2] + (n & 4 ? 2.0f : -2.0f) };
            float x = M[0] * b[0] + M[4] * b[1] + M[8] * b[2] + M[12];
            float y = M[1] * b[0] + M[5] * b[1] + M[9] * b[2] + M[13];
            float z = M[2] * b[0] + M[6] * b[1] + M[10] * b[2] + M[14];
            float w = M[3] * b[0] + M[7] * b[1] + M[11] * b[2] + M[15];
            out[0] += x < -w; out[1] += x > w; out[2] += y < -w; out[3] += y > w; out[4] += z < -w; out[5] += z > w;
        }
        for (int j = 0; j < 6; j++) if (out[j] == 8) return;
    } else {
        if (depth <= 0.0f) return;
        /* 0x5bc450: the point inside the view's planes */
        if (cw <= 0.0f || cx < -cw || cx > cw || cy < -cw || cy > cw) return;
    }
    float de[3] = { c->eye[0] - P[0], c->eye[1] - P[1], c->eye[2] - P[2] };
    float d2 = de[0] * de[0] + de[1] * de[1] + de[2] * de[2];
    float dfar = q->ft == 1 ? 4000.0f : 40000.0f, dnear = q->ft == 1 ? 3900.0f : 32000.0f;
    if (d2 > dfar) return;                                        /* 0x78d79c / 0x78d7ac, squared */
    float fade = d2 > dnear ? 1.0f - (d2 - dnear) / (dfar - dnear) : 1.0f;   /* 0x78d798 / 0x78d7a8 */
    float il = d2 > 0 ? 1.0f / sqrtf(d2) : 0.0f;
    for (int j = 0; j < 3; j++) de[j] *= il;                     /* 0x4870b0: toward the eye, unit */
    /* the direction against the eye: |dot| for flag 1, else dot below 0 is 0
       (0x4910ae); the pow base for flag 4 */
    float dot = q->ft == 1 ? 0.0f : q->d[0] * de[0] + q->d[1] * de[1] + q->d[2] * de[2];
    float base = (q->flags & 1) ? fabsf(dot) : dot > 0.0f ? dot : 0.0f;
    float r[3], u[3];
    memcpy(r, c->R, sizeof r); memcpy(u, c->U, sizeof u);
    if (q->ft == 0) {
        /* FlareRot (flareType 0): the quad turned about the view axis by
           the flare's screen x over the screen width x 240 degrees
           (DAT_0082db00 = x / width * 0x7f3d50) */
        float ang = 0.5f * (cx / cw + 1.0f) * 240.0f * (3.14159265f / 180.0f);
        float cs = cosf(ang), sn = sinf(ang);
        /* the sense, confirmed (G2 10-31): 0x491436 calls eCreateAxisRotationMatrix 0x5ba8b0 (Rodrigues,
           M[1] = xy(1-c) - z s) about the view matrix's column 2 (forward, bNormalize'd at 0x4913d3) with
           bAngle = deg x 65536 / 360 (0x401020); 0x49143c then takes v' = v * M (row vector, i.e. R^T)
           for the view's column 0 (right) and column 1 (EA y = screen down). EA view (r, d, f) has
           f x r = d (eCreateLookAt), so r' = r c - d s = R c + U s and U' = U c - R s with U = -d. */
        for (int j = 0; j < 3; j++) {
            r[j] = c->R[j] * cs + c->U[j] * sn;
            u[j] = c->U[j] * cs - c->R[j] * sn;
        }
    }
    float ds = depth / c->proj;                                 /* view z / view +0xc */
    for (int s = 0; s < 2; s++) {
        int ty = q->type;
        const FlareParm *f = ty == 0 || ty == 7 || ty == 8 ? &k_fp_head[s] : s ? NULL : &k_fp[ty];
        if (!f || f->tex < 0 || f->tex > 2) continue;
        int ti = q->ft == 2 ? 2 : f->tex;   /* GetTextureInfo(0x3394fe62) */
        if (!A.ftex[ti]) continue;
        float k = q->k;
        if (q->flags & 4) { k = powf(base, f->pw) * q->k; if (k > 1.0f) k = 1.0f; }   /* 0x4912a7 */
        float size = (f->s1 - f->s0) * k + f->s0;
        if (!(size > 0.0f)) continue;
        float sc = ds < f->cmin ? f->cmin : ds > f->cmax ? f->cmax : ds;
        size *= sc;
        unsigned char col[4] = { (unsigned char)clamp255(k * f->r), (unsigned char)clamp255(k * f->g),
                                 (unsigned char)clamp255(k * f->b), (unsigned char)clamp255(k * f->a * fade * 2.0f) };
        if (A.ftex[ti] != c->btex || A.fblend[ti] != c->bblend || c->nq >= 1000) {
            flush(c->btex, c->bblend, c->nq); c->drawn += c->nq; c->nq = 0;
            c->btex = A.ftex[ti]; c->bblend = A.fblend[ti];
        }
        float cc[3], rr[3], uu[3];
        /* 0x49159a: flareType 2 scales the up axis once more */
        float us = q->ft != 2 ? 1.0f : q->p[2] - q->ez < 4.0f ? c->stretch : c->stretch * 0.5f;
        for (int j = 0; j < 3; j++) { cc[j] = P[j] + de[j] * f->pull; rr[j] = r[j] * size; uu[j] = u[j] * size * us; }
        AVert *o = A.v + 4 * c->nq;
        for (int j = 0; j < 3; j++) {
            o[0].p[j] = cc[j] - rr[j] - uu[j];
            o[1].p[j] = cc[j] + rr[j] - uu[j];
            o[2].p[j] = cc[j] + rr[j] + uu[j];
            o[3].p[j] = cc[j] - rr[j] + uu[j];
        }
        o[0].uv[0] = 0; o[0].uv[1] = 1; o[1].uv[0] = 1; o[1].uv[1] = 1;
        o[2].uv[0] = 1; o[2].uv[1] = 0; o[3].uv[0] = 0; o[3].uv[1] = 0;
        for (int v = 0; v < 4; v++) memcpy(o[v].c, col, 4);
        c->nq++;
    }
}

/* eRenderWorldLightFlares 0x491860 (GC 800cd1bc)'s pack walk: the view
   eye's drivable section (FindDrivableSection 0x5e00d0, W.vis), each of its
   visible sections' pack (the section's user info +8), kept when its box
   (+0x30 / +0x40) is in view (eView::GetVisibleState 0x5bc300: not wholly
   outside one frustum plane). No distance test. Returns the packs. Without
   the track's section data (W.vis unset or -2) the port's old pick: boxes within
   the flares' 200 m reach (0x78d79c) of the eye, `reach2` squared. */
static int wf_box_in_view(const float *M, const float mn[3], const float mx[3])
{
    int out[6] = { 0, 0, 0, 0, 0, 0 };
    for (int n = 0; n < 8; n++) {
        float b[3] = { n & 1 ? mx[0] : mn[0], n & 2 ? mx[1] : mn[1], n & 4 ? mx[2] : mn[2] };
        float x = M[0] * b[0] + M[4] * b[1] + M[8] * b[2] + M[12];
        float y = M[1] * b[0] + M[5] * b[1] + M[9] * b[2] + M[13];
        float z = M[2] * b[0] + M[6] * b[1] + M[10] * b[2] + M[14];
        float w = M[3] * b[0] + M[7] * b[1] + M[11] * b[2] + M[15];
        out[0] += x < -w; out[1] += x > w; out[2] += y < -w; out[3] += y > w; out[4] += z < -w; out[5] += z > w;
    }
    for (int j = 0; j < 6; j++) if (out[j] == 8) return 0;
    return 1;
}
#define WF_MAXPK 128
static long g_wf_stat[3];   /* main pass: packs taken, flares tried, frames (OPENUG2_FXLOG) */
static int wf_packs(const FlareCtx *c, float reach2, int out[WF_MAXPK])
{
    int n = 0;
    if (!W.on || !W.npk) return 0;
    const short *ls = NULL;
    int nv = W.vis ? W.vis(c->eye, &ls) : -2;
    if (nv != -2) {
        if (W.idx_npk != W.npk) {   /* (re)index the loaded packs by section number */
            int mx = 0;
            for (int i = 0; i < W.npk; i++) if (W.pk[i].sec > mx) mx = W.pk[i].sec;
            free(W.sec2pk); W.nsec = 0;
            W.sec2pk = (int *)malloc((size_t)(mx + 1) * sizeof *W.sec2pk);
            if (!W.sec2pk) return 0;
            W.nsec = mx + 1;
            for (int s = 0; s < W.nsec; s++) W.sec2pk[s] = -1;
            for (int i = 0; i < W.npk; i++) if (W.pk[i].sec >= 0) W.sec2pk[W.pk[i].sec] = i;
            W.idx_npk = W.npk;
        }
        for (int k = 0; k < nv && n < WF_MAXPK; k++) {
            int s = ls[k], i = s >= 0 && s < W.nsec ? W.sec2pk[s] : -1;
            if (i >= 0 && wf_box_in_view(c->PV, W.pk[i].mn, W.pk[i].mx)) out[n++] = i;
        }
        return n;
    }
    for (int i = 0; i < W.npk && n < WF_MAXPK; i++) {
        const WPack *k = &W.pk[i];
        const float *e = c->eye;
        float dx = e[0] < k->mn[0] ? k->mn[0] - e[0] : e[0] > k->mx[0] ? e[0] - k->mx[0] : 0.0f;
        float dy = e[1] < k->mn[1] ? k->mn[1] - e[1] : e[1] > k->mx[1] ? e[1] - k->mx[1] : 0.0f;
        float dz = e[2] < k->mn[2] ? k->mn[2] - e[2] : e[2] > k->mx[2] ? e[2] - k->mx[2] : 0.0f;
        if (dx * dx + dy * dy + dz * dz > reach2) continue;
        if (wf_box_in_view(c->PV, k->mn, k->mx)) out[n++] = i;
    }
    return n;
}

/* the main view's flare passes in 0x5caad0's order: eRenderWorldLightFlares
   (0x491860, flareType 0), RenderFlares (0x61ae90, the cars, flareType 0),
   eRenderLightFlarePool (0x494270(view, 0, 0): lists 0 and 1, flareType 3).
   All three queues are emptied. */
static int light_flares(const float V[16], const float PV[16], float proj)
{
    FlareCtx c; memset(&c, 0, sizeof c);
    c.V = V; c.PV = PV; c.proj = proj;
    for (int j = 0; j < 3; j++) {
        c.R[j] = V[4 * j]; c.U[j] = V[4 * j + 1]; c.F[j] = -V[4 * j + 2];
        c.eye[j] = -(V[4 * j] * V[12] + V[4 * j + 1] * V[13] + V[4 * j + 2] * V[14]);
    }
    {
        WF_LOCK();
        int pk[WF_MAXPK], npk = wf_packs(&c, 40000.0f, pk);
        g_wf_stat[0] += npk;
        for (int i = 0; i < npk; i++) {
            const WPack *k = &W.pk[pk[i]];
            g_wf_stat[1] += k->n;
            for (int n = 0; n < k->n; n++) {
                const WFlare *w = &W.f[k->first + n];
                FlareQ q;
                memcpy(q.p, w->p, sizeof q.p); memcpy(q.d, w->d, sizeof q.d);
                q.type = w->type > 14 ? 0 : w->type; q.flags = w->flags; q.k = 1.0f; q.ft = 0;
                if (w->type > 14 || !wf_in_area(w->p)) continue;
                flare_one(&c, &q);
            }
        }
        WF_UNLOCK();
    }
    for (int i = 0; i < g_ncq; i++) {
        /* RenderFlaresOnCar: eView::GetPixelSize(car position, 3.0) under 4
           pixels draws none of the car's flares */
        const float *P = g_cq[i].car;
        float z = -(V[2] * P[0] + V[6] * P[1] + V[10] * P[2] + V[14]);
        if (g_cq[i].hascar && (z <= 0.0f || 3.0f * proj / z < 4.0f)) continue;
        flare_one(&c, &g_cq[i]);
    }
    for (int i = 0; i < g_nfq; i++) flare_one(&c, &g_fq[i]);
    flush(c.btex, c.bblend, c.nq); c.drawn += c.nq;
    if (++g_wf_stat[2] >= 300) {   /* OPENUG2_FXLOG: the world walk's load per frame */
        static int log = -1;
        if (log < 0) log = getenv("OPENUG2_FXLOG") != NULL;
        if (log) printf("flares: world walk %.1f packs, %.1f flares a frame (%s); worst lock wait render %.3f ms, loader %.3f ms\n",
                        g_wf_stat[0] / 300.0, g_wf_stat[1] / 300.0, W.vis ? "visible sections" : "200 m boxes",
                        g_wf_wait_max[0], g_wf_wait_max[1]);
        g_wf_stat[0] = g_wf_stat[1] = g_wf_stat[2] = 0;
    }
    memcpy(g_rq, g_cq, sizeof(FlareQ) * (size_t)g_ncq); g_nrq = g_ncq;
    g_nfq = 0; g_ncq = 0;
    return c.drawn;
}

void aciddraw_car_flare_mirror(float ez)
{
    if (g_ncq > 0) g_cq[g_ncq - 1].ez = ez;
}

/* The road reflection view's flares (0x5caad0 with eGetView(4): when
   0x870ce4 is set, eRenderWorldLightFlares(view, 2) 0x491860 then
   RenderFlares(view, 1) 0x61ae90, whose cars' flares go through
   eRenderLightFlare with flareType 2 (0x6159de)). A world flare's mirror
   height (+0x1c) is looked up once: the topology's elevation e under it
   (TopologyCoordinate::NewGetElevation), 2e - z, none when there is no
   topology or the light sits within 0.05 of it. The stretch 0x7f39a0 =
   (10 - 1.2) x the rain's wetness (+0x509c) + 1.2. */
int aciddraw_reflect_flares(const float V[16], const float PV[16], float proj, float wet,
                            int (*ground)(const float p[3], float *z))
{
    if (!A.ready || !A.prog || !A.ftex[2] || (!(W.on && W.npk) && !g_nrq)) return 0;
    if (A.cap < 4 * 1000) {
        free(A.v); A.v = (AVert *)malloc(sizeof(AVert) * 4 * 1000);
        if (!A.v) { A.cap = 0; return 0; }
        A.cap = 4 * 1000;
    }
    glUseProgram(A.prog);
    glUniformMatrix4fv(A.uMVP, 1, GL_FALSE, PV);
    glUniform1i(A.uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisableVertexAttribArray(5 - RENDER_PARTICLE_COLOR_LOC);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    FlareCtx c; memset(&c, 0, sizeof c);
    c.V = V; c.PV = PV; c.proj = proj;
    c.stretch = (10.0f - 1.2f) * wet + 1.2f;
    for (int j = 0; j < 3; j++) {
        c.R[j] = V[4 * j]; c.U[j] = V[4 * j + 1]; c.F[j] = -V[4 * j + 2];
        c.eye[j] = -(V[4 * j] * V[12] + V[4 * j + 1] * V[13] + V[4 * j + 2] * V[14]);
    }
    {
        WF_LOCK();
        int pk[WF_MAXPK], npk = wf_packs(&c, 40000.0f, pk);   /* the same walk as light_flares, view 4's eye */
        for (int i = 0; i < npk; i++) {
            const WPack *k = &W.pk[pk[i]];
            for (int n = 0; n < k->n; n++) {
                WFlare *w = &W.f[k->first + n];
                if (w->type > 14 || !wf_in_area(w->p)) continue;
                if (!w->ezs) {
                    float e = 0.0f;
                    w->ezs = 2;
                    if (ground && ground(w->p, &e) && fabsf(e - w->p[2]) >= 0.05f) { w->ez = e - (w->p[2] - e); w->ezs = 1; }
                }
                if (w->ezs != 1) continue;
                FlareQ q; memset(&q, 0, sizeof q);
                memcpy(q.p, w->p, sizeof q.p); memcpy(q.d, w->d, sizeof q.d);
                q.type = w->type; q.flags = w->flags; q.k = 1.0f; q.ft = 2; q.ez = w->ez;
                flare_one(&c, &q);
            }
        }
        WF_UNLOCK();
    }
    for (int i = 0; i < g_nrq; i++) {
        const float *P = g_rq[i].car;
        float z = -(V[2] * P[0] + V[6] * P[1] + V[10] * P[2] + V[14]);
        if (g_rq[i].hascar && (z <= 0.0f || 3.0f * proj / z < 4.0f)) continue;
        FlareQ q = g_rq[i]; q.ft = 2;
        flare_one(&c, &q);
    }
    flush(c.btex, c.bblend, c.nq); c.drawn += c.nq;
    glDisableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return c.drawn;
}

unsigned aciddraw_window_tex(int level)
{
    return A.ready && level >= 1 && level <= 3 ? (unsigned)A.wtex[level - 1] : 0u;
}
int aciddraw_flares_pending(void) { return g_nfq || g_ncq || (W.on && W.npk); }

/* The car environment cube's flares (0x5caad0, views 16..21 when 0x870ce4 is
   set, after each face's scene: eRenderWorldLightFlares(view, 1) 0x491860,
   then eRenderLightFlarePool 0x494270(view, 0, 4)). Into the bound face with
   its V / PV; proj = the face view's +0xc. The pool's queue is drawn but not
   emptied (the main pass does that). Returns the quads drawn. */
int aciddraw_env_flares(const float V[16], const float PV[16], float proj)
{
    if (!A.ready || !A.prog || (!(W.on && W.npk) && !g_nfq)) return 0;
    if (A.cap < 4 * 1000) {
        free(A.v); A.v = (AVert *)malloc(sizeof(AVert) * 4 * 1000);
        if (!A.v) { A.cap = 0; return 0; }
        A.cap = 4 * 1000;
    }
    glUseProgram(A.prog);
    glUniformMatrix4fv(A.uMVP, 1, GL_FALSE, PV);
    glUniform1i(A.uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisableVertexAttribArray(5 - RENDER_PARTICLE_COLOR_LOC);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    FlareCtx c; memset(&c, 0, sizeof c);
    c.V = V; c.PV = PV; c.proj = proj;
    for (int j = 0; j < 3; j++) {
        c.R[j] = V[4 * j]; c.U[j] = V[4 * j + 1]; c.F[j] = -V[4 * j + 2];
        c.eye[j] = -(V[4 * j] * V[12] + V[4 * j + 1] * V[13] + V[4 * j + 2] * V[14]);
    }
    {
        /* the same walk as light_flares from the face view's eye (each flare
           then fades out by 0x78d7ac, 4000 squared, in flare_one) */
        WF_LOCK();
        int pk[WF_MAXPK], npk = wf_packs(&c, 4000.0f, pk);
        for (int i = 0; i < npk; i++) {
            const WPack *k = &W.pk[pk[i]];
            for (int n = 0; n < k->n; n++) {
                const WFlare *w = &W.f[k->first + n];
                if (w->type > 14 || !wf_in_area(w->p)) continue;
                FlareQ q; memset(&q, 0, sizeof q);
                memcpy(q.p, w->p, sizeof q.p); memcpy(q.d, w->d, sizeof q.d);
                q.type = w->type; q.flags = w->flags; q.k = 1.0f; q.ft = 1;
                flare_one(&c, &q);
            }
        }
        WF_UNLOCK();
    }
    for (int i = 0; i < g_nfq; i++) { FlareQ q = g_fq[i]; q.ft = 4; flare_one(&c, &q); }
    flush(c.btex, c.bblend, c.nq); c.drawn += c.nq;
    glDisableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return c.drawn;
}

static GLuint tex_from(const unsigned char *b, long len, N2Tpk *tk, uint32_t key, int *blend)
{
    N2Tex t; memset(&t, 0, sizeof t);
    GLuint id = 0;
    if (n2_tpk_decode(b, len, *tk, key, &t) && t.rgb) { id = upload_tex(&t); *blend = t.blend; }
    free(t.rgb); free(t.alpha); free(t.dxt);
    return id;
}

int aciddraw_init(const char *dataroot, const unsigned char *glob, long globlen)
{
    if (A.ready) return 1;
    A.prog = render_particle_program();
    if (!A.prog) { printf("particles: program link failed\n"); return 0; }
    A.uMVP = glGetUniformLocation(A.prog, "uMVP");
    A.uTex = glGetUniformLocation(A.prog, "uTex");
    glGenBuffers(1, &A.vbo);
    glGenBuffers(1, &A.ibo);
    /* index pattern for up to 1000 quads: 0 1 2, 0 2 3 */
    unsigned short *ix = (unsigned short *)malloc(sizeof *ix * 6 * 1000);
    if (ix) {
        for (int q = 0; q < 1000; q++) {
            unsigned short b = (unsigned short)(q * 4);
            unsigned short *o = ix + q * 6;
            o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof *ix * 6 * 1000, ix, GL_STATIC_DRAW);
        free(ix);
    }
    /* textures: by hash, InGameCommon first (most of them), then GLOBALB (0x4901d0) */
    for (int f = 0; f < 2; f++) {
        long len = 0; unsigned char *b = NULL;
        if (f == 0) {
            char path[1024]; snprintf(path, sizeof path, "%s/GLOBAL/InGameCommon.bun", dataroot);
            b = n2_read_file(path, &len);
        } else { b = (unsigned char *)glob; len = globlen; }
        if (!b) continue;
        N2Tpk tk = n2_tpk_open(b, len);
        for (int i = 0; i < acid_ndefs(); i++) {
            AcidDef *d = acid_def_at(i);
            if (d->gltex || !d->tex) continue;
            /* the same texture key is shared by many definitions */
            for (int j = 0; j < i && !d->gltex; j++)
                if (acid_def_at(j)->tex == d->tex && acid_def_at(j)->gltex) {
                    d->gltex = acid_def_at(j)->gltex; d->blend = acid_def_at(j)->blend;
                }
            if (!d->gltex) d->gltex = tex_from(b, len, &tk, d->tex, &d->blend);
        }
        free(tk.blk);
        if (f == 0) free(b);
    }
    {   /* the light flare textures 0x4908f0: hashes 0x7f39cc (GLOBALB) */
        static const uint32_t k[3] = { 0x8868656fu, 0x88d8e8a2u, 0x3394fe62u };   /* HEADLIGHTFLAREINNER / OUTER, HEADLIGHTGLOW */
        N2Tpk tk = n2_tpk_open(glob, globlen);
        for (int i = 0; i < 3; i++) A.ftex[i] = tex_from(glob, globlen, &tk, k[i], &A.fblend[i]);
        /* the cracked window sheets (GC DamagedWindowHashes 0x8037127c: WINDOW,
           WINDOW_DAMAGE0, 1, 2 by the window's level) */
        static const uint32_t wk[3] = { 0x0a155545u, 0x0a155546u, 0x0a155547u };
        for (int i = 0; i < 3; i++) A.wtex[i] = tex_from(glob, globlen, &tk, wk[i], &A.wblend[i]);
        free(tk.blk);
    }
    int have = 0, want = 0;
    for (int i = 0; i < acid_ndefs(); i++) { want += acid_def_at(i)->tex != 0; have += acid_def_at(i)->gltex != 0; }
    printf("particles: %d emitter definitions, textures %d of %d\n", acid_ndefs(), have, want);
    A.ready = 1;
    return 1;
}

void aciddraw_free(void)
{
    if (!A.ready) return;
    for (int i = 0; i < acid_ndefs(); i++) {
        AcidDef *d = acid_def_at(i);
        if (d->gltex) {
            GLuint t = d->gltex;
            for (int j = i; j < acid_ndefs(); j++) if (acid_def_at(j)->gltex == t) acid_def_at(j)->gltex = 0;
            glDeleteTextures(1, &t);
        }
    }
    for (int i = 0; i < 3; i++) if (A.ftex[i]) glDeleteTextures(1, &A.ftex[i]);
    for (int i = 0; i < 3; i++) if (A.wtex[i]) glDeleteTextures(1, &A.wtex[i]);
    if (A.vbo) glDeleteBuffers(1, &A.vbo);
    if (A.ibo) glDeleteBuffers(1, &A.ibo);
    free(A.v);
    memset(&A, 0, sizeof A);
}

/* cubic 0x4881d0: out = t^3*r0 + t^2*r1 + t*r2 + r3 */
static void cubic(const float *K, float t, float o[4])
{
    float t2 = t * t, t3 = t * t2;
    for (int j = 0; j < 4; j++) o[j] = t3 * K[j] + t2 * K[4 + j] + t * K[8 + j] + K[12 + j];
}
static float clamp255(float c) { return c > 0 ? (c > 255.0f ? 255.0f : c) : 0.0f; }

static const float aspect[4][2] = { {1, 1}, {1, 0.5f}, {0.5f, 1}, {1.1097829e-38f, 1.1097813e-38f} };  /* 0x7f3944 */

static void flush(GLuint tex, int blend, int nq)
{
    if (!nq) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBlendFunc(GL_SRC_ALPHA, blend == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER, A.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(AVert) * 4 * (size_t)nq), A.v, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AVert), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(AVert), (void *)12);
    glVertexAttribPointer(RENDER_PARTICLE_COLOR_LOC, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(AVert), (void *)20);
    glDrawElements(GL_TRIANGLES, nq * 6, GL_UNSIGNED_SHORT, 0);
}

int aciddraw_draw(const float V[16], const float PV[16], float proj, const AcidOwner *incar)
{
    if (!A.ready || !A.prog) return 0;
    AcidGroup *first = acid_group_first();
    if (!first && !aciddraw_flares_pending()) { g_nrq = 0; return 0; }
    if (A.cap < 4 * 1000) {
        free(A.v); A.v = (AVert *)malloc(sizeof(AVert) * 4 * 1000);
        if (!A.v) { A.cap = 0; return 0; }
        A.cap = 4 * 1000;
    }
    /* the camera's right / up in world space and the eye depth row (0x5b7de0) */
    const float R[3] = { V[0], V[4], V[8] }, U[3] = { V[1], V[5], V[9] };
    glUseProgram(A.prog);
    glUniformMatrix4fv(A.uMVP, 1, GL_FALSE, PV);
    glUniform1i(A.uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    /* the world's normals (2) are not in this effect; on the Vita 2 is the colour */
    glDisableVertexAttribArray(5 - RENDER_PARTICLE_COLOR_LOC);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    int drawn = 0;
    for (int pass = 0; pass < 2; pass++) {
        GLuint btex = 0; int bblend = 0, nq = 0;
        for (AcidGroup *g = first; g; g = acid_group_next(g)) {
            int cnt = 0;
            const AcidParticle *p = acid_group_list(g, pass, &cnt);
            if (!p) continue;
            const AcidDef *d = acid_group_def(g);
            if (!d->gltex) continue;
            /* the view's own car in a bumper/hood camera: no engine / exhaust /
               mirror effects, no front-axle / front-wheel ones (0x48c0bf) */
            if (incar && acid_group_owner(g) == incar) {
                int s = d->slot;
                if (s == 1 || s == 5 || s == 6 || s == 9 || s == 10 || s == 11 || s == 26) continue;
            }
            if (d->gltex != btex || d->blend != bblend || nq + cnt > 1000) {
                flush(btex, bblend, nq); drawn += nq; nq = 0;
                btex = d->gltex; bblend = d->blend;
            }
            float inv_life = 1.0f / d->life;
            const float *col = acid_group_col(g);
            int cl = d->cols_log2, rl = d->rows_log2;
            int cmask = ~(-1 << cl), rmask = ~(-1 << rl);
            float du = 1.0f / (float)(1 << cl), dv = 1.0f / (float)(1 << rl);
            float maxs = d->maxsize != 0.0f ? d->maxsize : 512.0f;      /* def +0x8c, 0 -> 512 */
            for (; p; p = acid_particle_next(g, pass, p)) {
                float t = 1.0f - (float)p->life * 0.0009765625f * inv_life;
                float c[4], sz[4];
                cubic(col, t, c);
                for (int j = 0; j < 4; j++) c[j] = clamp255(c[j]);
                cubic(d->size, t, sz);
                float depth = -(V[2] * p->pos[0] + V[6] * p->pos[1] + V[10] * p->pos[2] + V[14]);
                float size = sz[0];
                /* port (Vita budget): a quad wholly behind the eye cannot show */
                if (depth < -(size > 0 ? size : -size)) continue;
                float scr = size > 0 && depth > 0 ? size / depth * proj : 0.0f;
                if (scr > maxs) size = depth / proj * maxs;
                int asp = p->typ >> 4;
                float hw = size * aspect[asp][0] * 0.5f, hh = size * aspect[asp][1] * 0.5f;
                float u0 = (float)(p->frame & cmask) * du;
                float v0 = (float)((p->frame >> cl) & rmask) * dv;
                float u1 = u0 + du, v1 = v0 + dv;
                unsigned char col[4] = { (unsigned char)(int)c[0], (unsigned char)(int)c[1],
                                         (unsigned char)(int)c[2], (unsigned char)(int)c[3] };
                AVert *o = A.v + 4 * nq;
                const float *P = p->pos;
                if (pass == 0) {
                    if (col[3] <= 0x14) continue;   /* 0x5bb430 drops alpha <= 20 */
                    float r[3] = { R[0] * hw, R[1] * hw, R[2] * hw }, u[3] = { U[0] * hh, U[1] * hh, U[2] * hh };
                    for (int j = 0; j < 3; j++) {
                        o[0].p[j] = P[j] + r[j] + u[j];
                        o[1].p[j] = P[j] - r[j] + u[j];
                        o[2].p[j] = P[j] - r[j] - u[j];
                        o[3].p[j] = P[j] + r[j] - u[j];
                    }
                    /* top edge (+u) = texture row v0, as pass 1 and the flares:
                       U here is the GL view's up, where the original's row
                       points down the screen (HW 11-09: the event icons drew
                       upside down with v1 on top) */
                    o[0].uv[0] = u1; o[0].uv[1] = v0;
                    o[1].uv[0] = u0; o[1].uv[1] = v0;
                    o[2].uv[0] = u0; o[2].uv[1] = v1;
                    o[3].uv[0] = u1; o[3].uv[1] = v1;
                } else {
                    int spin = (int)sz[1];
                    int rot = (p->rot & 1) ? p->rot - spin : p->rot + spin;
                    int o4 = p->typ & 0xf;
                    float r[3], u[3];
                    static const float ax[4][2][3] = {
                        { {0,0,0}, {0,0,0} }, { {1,0,0}, {0,1,0} }, { {1,0,0}, {0,0,1} }, { {0,1,0}, {0,0,1} } };
                    if (o4 == 0) { memcpy(r, R, sizeof r); memcpy(u, U, sizeof u); }
                    else { memcpy(r, ax[o4 & 3][0], sizeof r); memcpy(u, ax[o4 & 3][1], sizeof u); }
                    if ((short)rot != 0) {
                        float cs = sinf((float)(unsigned short)(rot + 0x4000) * (6.28318530718f / 65536.0f));
                        float sn = sinf((float)(unsigned short)rot * (6.28318530718f / 65536.0f));
                        if (o4 == 0) {                                   /* 0x5bac90: * Rz */
                            float r2[3] = { r[0] * cs - r[1] * sn, r[0] * sn + r[1] * cs, r[2] };
                            float u2[3] = { u[0] * cs - u[1] * sn, u[0] * sn + u[1] * cs, u[2] };
                            memcpy(r, r2, sizeof r); memcpy(u, u2, sizeof u);
                        } else if (o4 == 1) { r[0] = cs; r[1] = sn; r[2] = 0; u[0] = -sn; u[1] = cs; u[2] = 0; }
                        else if (o4 == 2) { r[0] = cs; r[1] = 0; r[2] = -sn; u[0] = sn; u[1] = 0; u[2] = cs; }
                        else if (o4 == 3) { r[0] = 0; r[1] = cs; r[2] = sn; u[0] = 0; u[1] = -sn; u[2] = cs; }
                    }
                    float a[3], b[3];
                    for (int j = 0; j < 3; j++) { a[j] = hw * r[j] - hh * u[j]; b[j] = hw * r[j] + hh * u[j]; }
                    for (int j = 0; j < 3; j++) {
                        o[0].p[j] = P[j] - a[j];
                        o[1].p[j] = P[j] - b[j];
                        o[2].p[j] = P[j] + a[j];
                        o[3].p[j] = P[j] + b[j];
                    }
                    o[0].uv[0] = u0; o[0].uv[1] = v0;
                    o[1].uv[0] = u1; o[1].uv[1] = v0;
                    o[2].uv[0] = u1; o[2].uv[1] = v1;
                    o[3].uv[0] = u0; o[3].uv[1] = v1;
                }
                for (int k = 0; k < 4; k++) memcpy(o[k].c, col, 4);
                nq++;
            }
        }
        flush(btex, bblend, nq); drawn += nq;
    }
    drawn += light_flares(V, PV, proj);   /* world, car and pool flares after the particles */
    glDisableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return drawn;
}
