/* acid.c -- the original's particle system and car effects; see acid.h.
 * Function addresses are SPEED2.EXE; notes E:/re-tools/out/PARTICLES.md. */
#include "acid.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NGROUPS    400                  /* AcidActiveGroupSlotPool 0x2c x 400 */
#define NPARTICLES 1000                 /* AcidParticleSlotPool 0x20 x 1000 (0x4886e0) */

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

static uint32_t bhash(const char *s)                                   /* 0x43db50 */
{
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

/* the game's xorshift (0x487360 and inline) */
static uint32_t xs(uint32_t x) { x ^= x << 17; x ^= x >> 13; x ^= x << 5; return x; }
/* (x & 0x7fffffff) * 2^-31 */
/* the x87 runs these expressions in double precision (FPCW 0x27f) */
static double xr(uint32_t x) { return (double)(x & 0x7fffffffu) * 4.656612873077393e-10; }
static uint32_t g_seed = 0x23985727u;                                 /* 0x7f3940 */

/* 16-bit angles: 0x43c2b0 sin, 0x43c370 cos (= sin(a + 0x4000)) */
static float sin16(int a) { return sinf((float)(unsigned short)a * (6.28318530718f / 65536.0f)); }
static float cos16(int a) { return sin16(a + 0x4000); }
/* MSVC _ftol: truncation toward zero */
static int ftol(double f) { return (int)f; }

/* ---- data --------------------------------------------------------------- */
static AcidDef *g_def; static int g_ndef, g_capdef;
static AcidEmitter *g_world;            /* manager 0x8357e0 +8: templates / world emitters */
static int g_enabled = 1;               /* 0x870cec */
static float g_cam[3][3]; static int g_ncam;

int acid_ndefs(void) { return g_ndef; }
AcidDef *acid_def_at(int i) { return i >= 0 && i < g_ndef ? &g_def[i] : NULL; }

static const AcidDef *def_by_hash(uint32_t h)                          /* 0x48bc00 */
{
    for (int i = 0; i < g_ndef; i++) if (g_def[i].hash == h) return &g_def[i];
    return NULL;
}

static void decode_def(AcidDef *o, const unsigned char *r)
{
    memset(o, 0, sizeof *o);
    o->hash = rd32(r + 8); o->tex = rd32(r + 0xc); o->group = rd32(r + 0x10);
    o->dirmode = (signed char)r[0x18]; o->cols_log2 = r[0x19]; o->rows_log2 = r[0x1a];
    o->fps = (signed char)r[0x1b]; o->rand_frame = r[0x1c]; o->flip = (signed char)r[0x1d];
    o->inherit = r[0x1e]; o->aspect = (signed char)r[0x1f]; o->orient = r[0x20]; o->always = r[0x21];
    float *f = &o->on_m;                       /* +0x24 .. +0x58, 14 floats */
    for (int i = 0; i < 14; i++) f[i] = rdf(r + 0x24 + 4 * i);
    o->slot = (int)rd32(r + 0x5c);
    for (int i = 0; i < 3; i++) { o->box_c[i] = rdf(r + 0x60 + 4 * i); o->box_s[i] = rdf(r + 0x70 + 4 * i); }
    o->vis = rdf(r + 0x80); o->grav = rdf(r + 0x84); o->drag = rdf(r + 0x88); o->maxsize = rdf(r + 0x8c);
    for (int i = 0; i < 4; i++) o->ctime[i] = rdf(r + 0x90 + 4 * i);
    for (int i = 0; i < 16; i++) o->ckey[i] = rdf(r + 0xa0 + 4 * i);
    for (int i = 0; i < 16; i++) { o->col[i] = rdf(r + 0x100 + 4 * i); o->size[i] = rdf(r + 0x140 + 4 * i); }
    memcpy(o->name, r + 0x1f0, 47); o->name[47] = 0;
}

/* aligned table of a 0x35020/0x35021 chunk (0x491c70): +8 version, +0xc count */
static const unsigned char *table(const unsigned char *bundle, long chunk, long size, unsigned *ver, unsigned *n)
{
    /* the table sits 16-aligned in the file, after 0x11111111 filler; the
       port's compact bundle copies move chunks, so skip the filler instead */
    long a = chunk + 8;
    while (a + 4 <= chunk + 8 + size && rd32(bundle + a) == 0x11111111u) a += 4;
    if (a + 0x10 > chunk + 8 + size) return NULL;
    *ver = rd32(bundle + a + 8); *n = rd32(bundle + a + 0xc);
    return bundle + a + 0x10;
}

static float rand_range(float range, uint32_t *seed)                   /* 0x487360 */
{
    *seed = xs(*seed);
    return (float)(xr(*seed) * range);
}

static void world_add(AcidEmitter *e)                                  /* 0x48bc50 */
{
    if (e->seed == 0) { g_seed = xs(g_seed); e->seed = (uint32_t)(double)(g_seed & 0x7fffffffu); }
    const AcidDef *d = e->def;
    if (d && e->state != 2) {
        if (e->state == 0 || e->state == 1) {
            float m = e->state == 0 ? d->off_m : d->on_m, v = e->state == 0 ? d->off_v : d->on_v;
            float t = m;
            if (m != 0.0f) t = (m - v * m) + rand_range(v * m, &e->seed);
            e->timer = t; e->acc = 0;
            e->next = g_world; g_world = e;                            /* head */
            return;
        }
        if (e->state == 3) goto tail;
    }
    e->state = 2;
tail:
    e->next = NULL;
    AcidEmitter **pp = &g_world;
    while (*pp) pp = &(*pp)->next;
    *pp = e;
}

int acid_load(const unsigned char *b, long len, int templates)
{
    int taken = 0;
    for (long o = 0; o + 8 <= len;) {
        uint32_t id = rd32(b + o), sz = rd32(b + o + 4);
        if ((long)sz > len - o - 8) break;
        unsigned ver = 0, n = 0;
        if (id == 0x35021u) {                                          /* definitions */
            const unsigned char *r = table(b, o, sz, &ver, &n);
            if (r && ver == 4 && (long)(r - b) + (long)n * 0x220 <= o + 8 + (long)sz) {
                if (g_ndef + (int)n > g_capdef) {
                    int cap = g_ndef + (int)n + 32;
                    AcidDef *nd = (AcidDef *)malloc(sizeof *nd * (size_t)cap);
                    if (!nd) return taken;
                    if (g_ndef) memcpy(nd, g_def, sizeof *nd * (size_t)g_ndef);
                    /* templates point into g_def: re-point them */
                    for (AcidEmitter *e = g_world; e; e = e->next)
                        if (e->def) e->def = nd + (e->def - g_def);
                    free(g_def);
                    g_def = nd; g_capdef = cap;
                }
                for (unsigned i = 0; i < n; i++) decode_def(&g_def[g_ndef++], r + 0x220 * i);
                taken += (int)n;
            }
        } else if (id == 0x35020u && templates) {                     /* emitters */
            const unsigned char *r = table(b, o, sz, &ver, &n);
            if (r && ver == 6 && (long)(r - b) + (long)n * 0xd0 <= o + 8 + (long)sz) {
                for (unsigned i = 0; i < n; i++, r += 0xd0) {
                    AcidEmitter *e = (AcidEmitter *)calloc(1, sizeof *e);
                    if (!e) break;
                    e->hash = rd32(r + 8);
                    e->state = (int)rd32(r + 0x10);
                    e->seed = rd32(r + 0x24);
                    e->def = def_by_hash(rd32(r + 0xc));
                    for (int k = 0; k < 16; k++) e->m[k] = rdf(r + 0x40 + 4 * k);
                    memcpy(e->name, r + 0x90, 63);
                    world_add(e);
                    taken++;
                }
            }
        }
        o += 8 + sz;
    }
    return taken;
}

/* ---- pools -------------------------------------------------------------- */
struct AcidGroup {                       /* 0x2c (0x491c00) */
    struct AcidGroup *next, *prev;
    const AcidDef *def;                  /* +0x08 */
    AcidEmitter *em;                     /* +0x0c */
    const AcidOwner *owner;              /* +0x10 */
    AcidParticle head[2];                /* +0x14 / +0x1c list sentinels */
    int count[2];                        /* +0x24 */
    int used;
    int own_col; float col[16];          /* its emitter's colour override, kept with the particles */
};
static AcidGroup g_gp[NGROUPS]; static AcidGroup g_ghead;            /* manager +0 list */
static AcidParticle g_pp[NPARTICLES]; static AcidParticle *g_pfree;
static int g_inited, g_nlive, g_nglive;

static void pools_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    g_ghead.next = g_ghead.prev = &g_ghead;
    g_pfree = NULL;
    for (int i = NPARTICLES - 1; i >= 0; i--) { g_pp[i].next = g_pfree; g_pfree = &g_pp[i]; }
}

static AcidGroup *group_new(AcidEmitter *e, const AcidOwner *owner)  /* 0x43f8f0 + 0x491c00 */
{
    for (int i = 0; i < NGROUPS; i++) if (!g_gp[i].used) {
        AcidGroup *g = &g_gp[i];
        memset(g, 0, sizeof *g);
        g->used = 1; g->em = e; g->owner = owner; g->def = e->def;
        if (e->col) { g->own_col = 1; memcpy(g->col, e->col, sizeof g->col); }
        for (int l = 0; l < 2; l++) g->head[l].next = g->head[l].prev = &g->head[l];
        e->group = g;
        g->prev = g_ghead.prev; g->next = &g_ghead;                   /* tail */
        g_ghead.prev->next = g; g_ghead.prev = g;
        g_nglive++;
        return g;
    }
    return NULL;
}
static void group_free(AcidGroup *g)                                   /* 0x491c50 + 0x43f920 */
{
    g->prev->next = g->next; g->next->prev = g->prev;
    if (g->em && g->em->group == g) g->em->group = NULL;
    g->used = 0; g_nglive--;
}
static int group_empty(const AcidGroup *g)
{
    return g->head[0].next == &g->head[0] && g->head[1].next == &g->head[1];
}
static void particle_free(AcidGroup *g, int l, AcidParticle *p)
{
    p->prev->next = p->next; p->next->prev = p->prev;
    g->count[l]--;
    p->next = g_pfree; g_pfree = p; g_nlive--;
}

void acid_set_cameras(const float (*pos)[3], int n)
{
    g_ncam = n < 0 ? 0 : n > 3 ? 3 : n;
    for (int i = 0; i < g_ncam; i++) memcpy(g_cam[i], pos[i], sizeof g_cam[i]);
}
void acid_set_enabled(int on) { g_enabled = on; }
float acid_rate_scale = 1.0f;   /* emission rate factor (main.c: 0.5 unless Full detail) */
static int g_world_paused;
void acid_world_pause(int on) { g_world_paused = on; }
int acid_live_particles(void) { return g_nlive; }
int acid_live_groups(void) { return g_nglive; }

/* ---- 0x491e50: spawn ---------------------------------------------------- */
/* o = v * M (row vector, 3x3 part plus w * row 3) */
static void vmul4(const float v[4], const float *M, float o[4])
{
    for (int j = 0; j < 4; j++) o[j] = v[0] * M[j] + v[1] * M[4 + j] + v[2] * M[8 + j] + v[3] * M[12 + j];
}
static void m4mul(const float *a, const float *b, float *o)            /* D3DXMatrixMultiply */
{
    float t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(o, t, sizeof t);
}
/* 0x5bacf0 / 0x5bada0 / 0x5bae60: out = in * R(axis, a) (D3DX rotation layout) */
static void rotx(float *m, int a) { float c = cos16(a), s = sin16(a); float R[16] = {1,0,0,0, 0,c,s,0, 0,-s,c,0, 0,0,0,1}; m4mul(m, R, m); }
static void roty(float *m, int a) { float c = cos16(a), s = sin16(a); float R[16] = {c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1}; m4mul(m, R, m); }
static void rotz(float *m, int a) { float c = cos16(a), s = sin16(a); float R[16] = {c,s,0,0, -s,c,0,0, 0,0,1,0, 0,0,0,1}; m4mul(m, R, m); }
/* degrees span -> 16-bit angle as the asm does: ftol(deg * 65536) / 360 */
static int deg16(float deg) { return ftol(deg * 65536.0f) / 360; }

void acid_emit(AcidEmitter *e, float dt, const AcidOwner *owner)
{
    pools_init();
    const AcidDef *d = e->def;
    if (!d) return;
    int vis = 0;
    for (int i = 0; i < g_ncam && !vis; i++) {
        float dx = e->m[12] - g_cam[i][0], dy = e->m[13] - g_cam[i][1], dz = e->m[14] - g_cam[i][2];
        if (sqrtf(dx * dx + dy * dy + dz * dz) < d->vis) vis = 1;
    }
    if (e->seed == 0) { g_seed = xs(g_seed); e->seed = g_seed & 0x7fffffffu; }
    if (!vis) return;
    if (e->state != 1 && !(dt < 0)) return;
    float budget = dt;
    if (dt < 0) budget = -dt;
    else if (e->timer != 0 && dt > e->timer) budget = e->timer;
    float interval = e->acc;
    if (budget > interval) {
        AcidGroup *g = e->group;
        if (!g || g->em != e || g->owner != owner) {
            AcidGroup *n = group_new(e, owner);
            if (n) g = n;                     /* a full pool keeps the old group (asm) */
        }
        if (g) {
            const float *M = e->m;
            float nframes = d->rand_frame ? (float)((1 << d->rows_log2) * (1 << d->cols_log2)) : 1.0f;
            double rr = (double)d->rot * 182.04444885253906;   /* fst: rotr float, rot0 from the unrounded value */
            float rotr = (float)rr, rot0 = (float)(rr * -0.5);
            float cone = d->cone, cone2 = cone * 0.5f;
            float ratev = d->rate_v * d->rate, rate0 = d->rate - ratev;
            float speedv = d->speed_v * d->speed, speed0 = d->speed - speedv;
            float lifev = d->life_v * d->life, life0 = d->life - lifev;
            float kv = d->k_v * d->k, k0 = d->k - kv;
            float hs[3] = { d->box_s[0] * 0.5f, d->box_s[1] * 0.5f, d->box_s[2] * 0.5f };
            uint32_t s = e->seed, st;
            float dir[3];
            do {
                budget -= interval;
                if (d->dirmode) {                         /* ring: 0x49222b */
                    uint32_t s1 = xs(s), s2 = xs(s1);
                    st = s2;
                    int a1 = ftol((xr(s2) * 360.0 - 180.0) * 65536.0) / 360;
                    float c = cos16(a1), sn = sin16(a1);
                    int a2 = ftol((xr(s1) * cone - cone2) * 65536.0) / 360;
                    float R[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                    roty(R, a2);
                    float v[4] = { c, sn, 0, 0 }, w[4], o4[4];
                    vmul4(v, R, w); w[3] = 0;                 /* row 3 of R is (0,0,0,1) */
                    vmul4(w, M, o4);
                    dir[0] = o4[0]; dir[1] = o4[1]; dir[2] = o4[2];
                } else {                                  /* cone: 0x4924e7 */
                    uint32_t s1 = xs(s), s2 = xs(s1), s3 = xs(s2);
                    st = s3;
                    float R[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                    rotx(R, ftol((xr(s1) * cone - cone2) * 65536.0) / 360);
                    roty(R, ftol((xr(s2) * cone - cone2) * 65536.0) / 360);
                    rotz(R, ftol((xr(s3) * cone - cone2) * 65536.0) / 360);
                    float v[4] = { M[8], M[9], M[10], M[11] }, o4[4];
                    vmul4(v, R, o4);
                    dir[0] = o4[0]; dir[1] = o4[1]; dir[2] = o4[2];
                }
                /* spawn point in the box, emitter space -> world */
                uint32_t r1 = xs(st), r2 = xs(r1), r3 = xs(r2), r4 = xs(r3);
                double lx = xr(r1) * d->box_s[0] - hs[0] + d->box_c[0];
                float ly = (float)(xr(r2) * d->box_s[1] - hs[1] + d->box_c[1]);
                float lz = (float)(xr(r3) * d->box_s[2] - hs[2] + d->box_c[2]);
                float pos[3];
                for (int j = 0; j < 3; j++) pos[j] = (float)((double)M[8 + j] * lz + (double)M[4 + j] * ly + M[j] * lx + M[12 + j]);
                int frame = ftol(xr(r4) * nframes);
                uint32_t r5 = xs(r4), r6 = xs(r5), r7 = xs(r6);
                float life = (float)(xr(r5) * lifev + life0);
                double rot = xr(r6) * rotr + rot0;
                float speed = (float)(xr(r7) * speedv + speed0);
                uint32_t last = r7;
                float k = 0;
                if (k0 != 0.0f) {
                    last = xs(r7);
                    k = (float)(xr(last) * kv + k0);
                    if (!(k > 0)) k = 0; else if (k > 1.0f) k = 1.0f;
                    if (!d->inherit) {
                        float v[3];
                        for (int j = 0; j < 3; j++) v[j] = k * e->vel[j] + speed * dir[j];
                        speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                        float l = speed;                                  /* 0x43cf10 */
                        if (l != 0) for (int j = 0; j < 3; j++) dir[j] = v[j] / l;
                        else dir[0] = 1, dir[1] = dir[2] = 0;
                    }
                }
                unsigned short rot16 = (unsigned short)(ftol(rot) & 0xfffe);
                if (d->flip) rot16 |= (unsigned short)(last & 1);
                if (life > 60.0f) life = 60.0f;
                if (!e->nospawn && (g_enabled || d->always) && g_pfree) {
                    int asp = d->aspect; if (asp < 0 || asp > 3) asp = 0;
                    AcidParticle *p = g_pfree; g_pfree = p->next; g_nlive++;
                    p->life = (short)ftol((double)life * 1024.0);
                    p->rot = rot16;
                    double sp = (double)speed * 128.0; if (sp > 65535.0) sp = 65535.0;
                    p->speed = (unsigned short)ftol(sp);
                    p->k = (unsigned char)ftol((double)k * 255.0);
                    p->typ = (unsigned char)((d->orient & 0xf) | asp << 4);
                    memcpy(p->pos, pos, sizeof pos);
                    for (int j = 0; j < 3; j++) p->dir[j] = (signed char)ftol((double)dir[j] * 127.0);
                    p->frame = (unsigned char)frame;
                    int l = d->orient != 0;
                    p->prev = g->head[l].prev; p->next = &g->head[l];
                    g->head[l].prev->next = p; g->head[l].prev = p;
                    g->count[l]++;
                }
                s = xs(last);
                double rate = xr(s) * ratev + rate0;
                if (!(rate > 0)) interval = 0.1f;
                else { interval = (float)(1.0 / (rate * acid_rate_scale)); if (interval > 240.0f) interval = 240.0f; else if (interval < 0) interval = 0; }
            } while (interval <= budget);
            e->seed = s;
            if (group_empty(g)) group_free(g);
        }
    }
    e->acc = dt >= 0 ? interval - budget : 0.0f;
}

/* ---- 0x492e10: tick ----------------------------------------------------- */
void acid_tick(float dt)
{
    pools_init();
    int ticks = ftol(dt * 1024.0f);
    /* world emitters: states 0/1 sit at the head, the loop ends at the first other */
    for (AcidEmitter *e = g_world_paused ? NULL : g_world, *nx; e; e = nx) {
        nx = e->next;
        int st = e->state;
        if (st == 1) acid_emit(e, dt, NULL);
        else if (st != 0) break;
        if (e->timer != 0 && (st == 0 || st == 1)) {
            float t = e->timer - dt;
            if (t > 0) { e->timer = t; continue; }
            const AcidDef *d = e->def;
            float m = st == 0 ? d->on_m : d->off_m, v = st == 0 ? d->on_v : d->off_v;
            e->state = st == 0;
            e->acc = 0;
            e->timer = m == 0.0f ? m : (m - v * m) + rand_range(v * m, &e->seed) + t;
        }
    }
    for (AcidGroup *g = g_ghead.next, *gn; g != &g_ghead; g = gn) {
        gn = g->next;
        const AcidDef *d = g->def;
        int mask = (1 << d->rows_log2) * (1 << d->cols_log2) - 1;
        int fp = d->fps ? ftol(1024.0f / (float)d->fps) : 0;
        float gr = -d->grav, drag = d->drag;
        float wind[3] = { 0, 0, 0 };
        if (d->inherit && g->owner && d->k > 0)
            for (int j = 0; j < 3; j++) wind[j] = dt * g->owner->vel[j];
        for (int l = 0; l < 2; l++) {
            for (AcidParticle *p = g->head[l].next, *pn; p != &g->head[l]; p = pn) {
                pn = p->next;
                int life = p->life - ticks;
                if (life <= 0) { particle_free(g, l, p); continue; }
                int frame = p->frame;
                if (fp) frame = ((p->life / fp - life / fp) + frame) & mask;
                float k = (float)p->k * (1.0f / 255.0f);
                float sp = (float)p->speed * 0.0078125f;
                float vx = p->dir[0] * 0.007874015718698502f * sp;
                float vy = p->dir[1] * 0.007874015718698502f * sp;
                float vz = p->dir[2] * 0.007874015718698502f * sp;
                float sx = vx < 0 ? 1.0f : -1.0f, sy = vy < 0 ? 1.0f : -1.0f, sz = vz < 0 ? 1.0f : -1.0f;
                vx = vx * vx * drag * dt * sx + vx;
                vy = vy * vy * drag * dt * sy + vy;
                vz = (sz * vz * vz * drag + gr) * dt + vz;
                p->pos[0] += vx * dt; p->pos[1] += vy * dt; p->pos[2] += vz * dt;
                if (d->inherit) for (int j = 0; j < 3; j++) p->pos[j] += k * wind[j];
                float mag = sqrtf(vx * vx + vy * vy + vz * vz);
                float inv = fabsf(mag) > 1e-5f ? 1.0f / mag : 1e10f;
                p->life = (short)life;
                p->speed = (unsigned short)ftol(mag * 128.0f);
                p->dir[0] = (signed char)ftol(inv * vx * 127.0f);
                p->dir[1] = (signed char)ftol(inv * vy * 127.0f);
                p->dir[2] = (signed char)ftol(inv * vz * 127.0f);
                p->frame = (unsigned char)frame;
            }
        }
        if (group_empty(g)) group_free(g);
    }
}

void acid_clear(void)
{
    pools_init();
    for (AcidGroup *g = g_ghead.next, *gn; g != &g_ghead; g = gn) {
        gn = g->next;
        for (int l = 0; l < 2; l++)
            for (AcidParticle *p = g->head[l].next, *pn; p != &g->head[l]; p = pn) { pn = p->next; particle_free(g, l, p); }
        group_free(g);
    }
}

AcidGroup *acid_group_first(void) { pools_init(); return g_ghead.next != &g_ghead ? g_ghead.next : NULL; }
AcidGroup *acid_group_next(AcidGroup *g) { return g->next != &g_ghead ? g->next : NULL; }
const AcidDef *acid_group_def(const AcidGroup *g) { return g->def; }
const AcidOwner *acid_group_owner(const AcidGroup *g) { return g->owner; }
const float *acid_group_col(const AcidGroup *g) { return g->own_col ? g->col : g->def->col; }
const AcidParticle *acid_group_list(const AcidGroup *g, int list, int *count)
{
    if (count) *count = g->count[list];
    return g->head[list].next != &g->head[list] ? g->head[list].next : NULL;
}
/* the renderer walks a list until it comes back to the sentinel */
const AcidParticle *acid_particle_next(const AcidGroup *g, int list, const AcidParticle *p)
{
    return p->next != &g->head[list] ? p->next : NULL;
}

/* ---- car effects -------------------------------------------------------- */
static const char *const fx_names[CARFX_COUNT] = {                   /* 0x8027c0 */
    "CARFX_NONE", "CARFX_DRIVE_OVER", "CARFX_DRIVE_OVER2", "CARFX_SKID_SMOKE", "CARFX_TIRE_SPEW",
    "CARFX_BOTTOM_OUT", "CARFX_DAM_RADIATOR", "CARFX_DAM_ENGINE", "CARFX_BLOWN_TIRE",
    "CARFX_DRIVE_ON_FLAT_TIRE", "CARFX_NITRO", "CARFX_EXHAUST_SMOKE", "CARFX_EXHAUST_BLOWOFF",
    "CARFX_NOS_BLOWOFF", "CARFX_NOS_BLOWOFF_TIRES", "CARFX_BREAK_SIDE_MIRROR_LEFT",
    "CARFX_BREAK_SIDE_MIRROR_RIGHT", "CARFX_BREAK_LICENSE_PLATE_FRONT", "CARFX_BREAK_LICENSE_PLATE_RIGHT",
    "CARFX_BREAK_HEADLIGHT_LEFT", "CARFX_BREAK_HEADLIGHT_RIGHT", "CARFX_BREAK_BRAKELIGHT_LEFT",
    "CARFX_BREAK_BRAKELIGHT_RIGHT", "CARFX_BREAK_BRAKELIGHT_CENTRE", "CARFX_BREAK_WINDSHIELD",
    "CARFX_BREAK_WINDOW_REAR", "CARFX_BREAK_WINDOW_LEFT_FRONT", "CARFX_BREAK_WINDOW_LEFT_REAR",
    "CARFX_BREAK_WINDOW_RIGHT_FRONT", "CARFX_BREAK_WINDOW_RIGHT_REAR",
};

/* 0x48b830: a fresh emitter from a template (name, definition, matrix) */
static AcidEmitter *clone(const AcidEmitter *t)
{
    AcidEmitter *e = (AcidEmitter *)calloc(1, sizeof *e);
    if (!e) return NULL;
    memcpy(e->name, t->name, sizeof e->name);
    e->hash = bhash(e->name);
    e->def = t->def;
    memcpy(e->m, t->m, sizeof e->m);
    return e;
}
/* 0x48b8b0: clones of every template whose definition's group is `group` */
static AcidEmitter *clones_of(uint32_t group)
{
    AcidEmitter *head = NULL, **tail = &head;
    if (!group) return NULL;
    for (const AcidEmitter *t = g_world; t; t = t->next)
        if (t->def && t->def->group == group) {
            AcidEmitter *e = clone(t);
            if (e) { *tail = e; tail = &e->next; }
        }
    return head;
}
static void free_list(AcidEmitter *e)
{
    while (e) {
        AcidEmitter *n = e->next;
        if (e->group && e->group->em == e) e->group->em = NULL;
        free(e); e = n;
    }
}

/* 0x883e68: the ground effects of types 1..5 by surface (from 0x800ee0) */
static AcidEmitter *g_ground[7][29];   /* 0x883e68 (0x658 B): DO DO2 SK SP BO hit scrape */ static int g_ground_built;
static void ground_build(void)
{
    if (g_ground_built) return;
    g_ground_built = 1;
    for (int t = 0; t < 7; t++)
        for (int s = 0; s < 29; s++) {
            const char *n = acid_surfaces[s].fx[t];
            g_ground[t][s] = n && *n ? clones_of(bhash(n)) : NULL;
        }
}

/* model marker -> attach slot (0x638190 switch); two slots for some */
static int marker_slots(uint32_t h, int *b)
{
    switch (h) {
    case 0x59a55db5u: return 9;
    case 0xdf066a41u: case 0xf21be91eu: case 0xa5b28001u: case 0xa2799c2bu:
    case 0xbcf8a18bu: case 0xbd7cf15eu: return 10;
    case 0x68100815u: return 11;
    case 0x6ec7e79fu: case 0x7b220ddfu: return 12;
    case 0x1b0763a0u: case 0x3f68f920u: return 13;
    case 0x3df28f69u: case 0xe7e4ef49u: return 14;
    case 0x4cdebfcau: case 0xe898cfaau: return 15;
    case 0x60f8b13cu: case 0x64b0bb1cu: return 16;
    case 0x0ab88f5du: case 0x2f96d0fdu: return 17;
    case 0xa7e6ea53u: case 0x9db90133u: *b = 20; return 18;
    case 0xd09091c6u: case 0xa532fc46u: *b = 21; return 18;
    case 0x31a66786u: case 0xd947f346u: *b = 22; return 19;
    case 0xbf700a79u: case 0x02b52399u: *b = 23; return 19;
    case 0x05f63788u: case 0xc4ec23c8u: return 24;
    case 0x0bea251bu: case 0xc52bf01bu: return 25;
    case 0x725758bfu: case 0xaa35c17fu: return 26;
    case 0x494ed280u: case 0xc805f7e0u: return 27;
    }
    return -1;
}
/* 0x638190 / 0x634800 store x, y, z with the low bit cleared, then set as
   a flag (0x615cc0 mirrors that axis of the emitter); a marker's point is
   read from the marker's own matrix row instead, its flags from the copy */
static void attach_add(AcidCar *c, int slot, const float p[3], int marker, int flip)
{
    if (slot < 0 || slot >= ACID_SLOTS || c->nattach[slot] >= 8) return;
    int k = c->nattach[slot]++;
    float *o = c->attach[slot][k];
    for (int i = 0; i < 3; i++) {
        uint32_t u; memcpy(&u, &p[i], 4); if (!marker) u &= ~1u; memcpy(&o[i], &u, 4);
    }
    c->aflip[slot][k] = (unsigned char)flip;
}

AcidCar *acid_car_new(const float w[4][3], const AcidMarker *mk, int nmk)
{
    AcidCar *c = (AcidCar *)calloc(1, sizeof *c);
    if (!c) return NULL;
    ground_build();
    static const float zero[3] = { 0, 0, 0 };
    attach_add(c, 0, zero, 0, 0);
    const int pr[4][2] = { {0, 1}, {2, 3}, {0, 3}, {1, 2} };
    for (int i = 0; i < 4; i++) {
        float m[3];
        for (int j = 0; j < 3; j++) m[j] = (w[pr[i][0]][j] + w[pr[i][1]][j]) * 0.5f;
        attach_add(c, 1 + i, m, 0, 0);
    }
    for (int i = 0; i < 4; i++) attach_add(c, 5 + i, w[i], 0, 0);
    for (int i = 0; i < nmk; i++) {
        int b = -1, a = marker_slots(mk[i].hash, &b);
        if (a >= 0) attach_add(c, a, mk[i].pos, 1, 0);
        if (b >= 0) attach_add(c, b, mk[i].pos, 1, 0);
    }
    for (int id = 1; id < CARFX_COUNT; id++) c->fx[id] = clones_of(bhash(fx_names[id]));
    return c;
}

void acid_car_free(AcidCar *c)
{
    if (!c) return;
    /* particles owned by this car stay where they are; their groups lose the owner */
    for (AcidGroup *g = acid_group_first(); g; g = acid_group_next(g))
        if (g->owner == &c->own) g->owner = NULL;
    for (int id = 0; id < CARFX_COUNT; id++) free_list(c->fx[id]);
    free(c);
}

/* 0x488270: RGB (0..1) -> hue (sixths / 6, may be < 0), saturation, lightness */
static void rgb_hsl(float r, float g, float b, float *h, float *s, float *l)
{
    float mn = 1.0f, mx = 0.0f;
    if (r < 1.0f) mn = r;
    if (g < mn) mn = g;
    if (b < mn) mn = b;
    if (r > 0.0f) mx = r;
    if (mx < g) mx = g;
    if (mx < b) mx = b;
    *l = (mx + mn) * 0.5f;
    if (mn == mx) { *s = 0.0f; *h = 0.0f; return; }
    float d = mx - mn;
    *s = *l < 0.5f ? d / (mx + mn) : d / (2.0f - mx - mn);
    if (mx == r) *h = (g - b) / d;
    else if (mx == g) *h = (b - r) / d + 2.0f;
    else *h = (r - g) / d + 4.0f;
    *h *= 0.16666667f;
}
static float hue_rgb(float p, float q, float t)
{
    if (t * 6.0f < 1.0f) return (q - p) * t * 6.0f + p;
    if (t + t < 1.0f) return q;
    if (t * 3.0f < 2.0f) return (0.6666667f - t) * (q - p) * 6.0f + p;
    return p;
}
/* 0x4883f0 */
static void hsl_rgb(float h, float s, float l, float *r, float *g, float *b)
{
    if (s == 0.0f) { *r = *g = *b = l; return; }
    float q = l < 0.5f ? (s + 1.0f) * l : (s + l) - s * l, p = (l + l) - q;
    float tr = h + 0.33333334f, tb = h - 0.33333334f;
    if (tr < 0.0f) tr += 1.0f;
    if (tr > 1.0f) tr -= 1.0f;
    if (h < 0.0f) h += 1.0f;
    if (h > 1.0f) h -= 1.0f;
    if (tb < 0.0f) tb += 1.0f;
    if (tb > 1.0f) tb -= 1.0f;
    *r = hue_rgb(p, q, tr); *g = hue_rgb(p, q, h); *b = hue_rgb(p, q, tb);
}
static int inv4(const float *m, float *o)   /* 0x4874e0, row-major */
{
    double a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) a[i][j] = j < 4 ? m[4 * i + j] : (j - 4 == i);
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int i = col + 1; i < 4; i++) if (fabs(a[i][col]) > fabs(a[piv][col])) piv = i;
        if (fabs(a[piv][col]) < 1e-12) return 0;
        for (int j = 0; j < 8; j++) { double t = a[col][j]; a[col][j] = a[piv][j]; a[piv][j] = t; }
        double d = a[col][col];
        for (int j = 0; j < 8; j++) a[col][j] /= d;
        for (int i = 0; i < 4; i++) if (i != col) {
            double f = a[i][col];
            for (int j = 0; j < 8; j++) a[i][j] -= f * a[col][j];
        }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o[4 * i + j] = (float)a[i][4 + j];
    return 1;
}
static void mul4(const float *a, const float *b, float *o)
{
    float t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] + a[4 * i + 3] * b[12 + j];
    memcpy(o, t, sizeof t);
}
/* 0x487940: the colour cubic (rows t^3 t^2 t 1) through keys P (4 RGBA
   rows) given the key times t0..t3: Hermite x [p(0); p(1); p'(0); p'(1)]
   of the cubic through (t_i, P_i) */
static void col_cubic(float *out, const float *P, const float t[4])
{
    float T[16], I[16], M[16];
    for (int i = 0; i < 4; i++) {
        T[4 * i] = t[i] * t[i] * t[i]; T[4 * i + 1] = t[i] * t[i]; T[4 * i + 2] = t[i]; T[4 * i + 3] = 1.0f;
    }
    if (!inv4(T, I)) { memset(out, 0, 16 * sizeof *out); return; }
    static const float B[16] = { 0,0,0,1, 1,1,1,1, 0,0,1,0, 3,2,1,0 };
    static const float H[16] = { 2,-2,1,1, -3,3,-2,-1, 0,0,1,0, 1,0,0,0 };
    mul4(B, I, M);
    mul4(H, M, M);
    mul4(M, P, out);
}
/* 0x48b920: shift every key's hue / saturation / lightness, rebuild the cubic */
static void recolour(const AcidDef *d, float *out, float dh, float ds, float dl)
{
    if (dh < -1.0f) dh = -1.0f; else if (dh > 1.0f) dh = 1.0f;
    if (ds < -1.0f) ds = -1.0f; else if (ds > 1.0f) ds = 1.0f;
    float P[16];
    memcpy(P, d->ckey, sizeof P);
    for (int k = 0; k < 4; k++) {
        float *q = P + 4 * k, h, s, l;
        rgb_hsl(q[0] * 0.003921569f, q[1] * 0.003921569f, q[2] * 0.003921569f, &h, &s, &l);
        h += dh;
        while (h < 0.0f) h += 1.0f;
        while (h > 1.0f) h -= 1.0f;
        s += ds; s = s > 0.0f ? (s > 1.0f ? 1.0f : s) : 0.0f;
        l += dl; l = l > 0.0f ? (l > 1.0f ? 1.0f : l) : 0.0f;
        hsl_rgb(h, s, l, &q[0], &q[1], &q[2]);
        q[0] *= 255.0f; q[1] *= 255.0f; q[2] *= 255.0f;
    }
    float t[4];
    for (int i = 0; i < 4; i++) t[i] = d->ctime[i] * 0.33333334f;
    col_cubic(out, P, t);
}

void acid_car_purge(AcidCar *c, const AcidPurge *p)
{
    if (!c || !p) return;
    c->nattach[28] = c->nattach[29] = 0;
    free_list(c->fx[CARFX_NOS_BLOWOFF]); free_list(c->fx[CARFX_NOS_BLOWOFF_TIRES]);
    c->fx[CARFX_NOS_BLOWOFF] = clones_of(p->hood_group);
    c->fx[CARFX_NOS_BLOWOFF_TIRES] = clones_of(p->wheel_group);
    for (int k = 0; k < 2; k++) {
        AcidEmitter *list = c->fx[k ? CARFX_NOS_BLOWOFF_TIRES : CARFX_NOS_BLOWOFF];
        const float *hsl = k ? p->tire_hsl : p->hood_hsl;
        float *slot = c->purge_col[k];
        for (AcidEmitter *e = list; e; e = e->next) {   /* each clone recoloured into the one slot */
            if (!e->def) continue;
            recolour(e->def, slot, hsl[0], hsl[1], hsl[2]);
            e->col = slot;
        }
    }
    if (p->hood_left && p->has_hood[0]) attach_add(c, 28, p->hood[0], 0, 0);
    if (p->hood_right && p->has_hood[1]) attach_add(c, 28, p->hood[1], 0, 2);
    if (p->wheel_left) {
        float w[3] = { p->wheel[0][0] + p->radius[0], p->wheel[0][1], p->wheel[0][2] };
        attach_add(c, 29, w, 0, 2);
    }
    if (p->wheel_right) {
        float w[3] = { p->wheel[1][0] + p->radius[1], p->wheel[1][1], p->wheel[1][2] };
        attach_add(c, 29, w, 0, 0);
    }
}

void acid_car_fire(AcidCar *c, int id, float intensity, int param3,
                   const float pos[3], const float mat[16], const float vel[3])
{
    if (!c || intensity == 0.0f || id == 0) return;
    float base[16];
    memcpy(base, mat, sizeof base);
    base[12] = pos[0]; base[13] = pos[1]; base[14] = pos[2]; base[15] = 1.0f;
    int wheel = (id == 3 || id == 4 || id == 8 || id == 9) ? param3 >> 16 : 0;
    if (wheel <= 0 || wheel >= 9) wheel = 0;
    while (id) {
        AcidEmitter *list = c->fx[id < CARFX_COUNT ? id : 0];
        if (id > 0 && id < 6) {
            int s = param3 & 0xffff;
            list = s < 29 ? g_ground[id - 1][s] : NULL;
        }
        for (AcidEmitter *e = list; e; e = e->next) {
            if (!e->def) continue;
            float keep[16];
            memcpy(keep, e->m, sizeof keep);
            int slot = wheel ? wheel : e->def->slot;
            if (slot < 0 || slot >= ACID_SLOTS) slot = 0;
            for (int a = 0; a < c->nattach[slot]; a++) {
                float L[16];
                memcpy(L, keep, sizeof L);
                for (int ax = 0; ax < 3; ax++)       /* a flagged axis: that column negated */
                    if (c->aflip[slot][a] & 1 << ax) { L[ax] = -L[ax]; L[4 + ax] = -L[4 + ax]; L[8 + ax] = -L[8 + ax]; }
                L[12] = c->attach[slot][a][0]; L[13] = c->attach[slot][a][1]; L[14] = c->attach[slot][a][2];
                L[15] = 1.0f;
                m4mul(L, base, e->m);
                memcpy(e->vel, vel, sizeof e->vel);
                acid_emit(e, -intensity, &c->own);
            }
            memcpy(e->m, keep, sizeof keep);
        }
        if (id != CARFX_NOS_BLOWOFF) break;
        id = CARFX_NOS_BLOWOFF_TIRES;
    }
}

/* ---- 0x5f5730 ----------------------------------------------------------- */
static float clamp01(float x) { return x > 0 ? (x > 1 ? 1 : x) : 0; }
/* test aid (simulator): OPENUG2_FXLOG logs the car effects as they fire,
   at most once a second per effect and car */
static void fx_log(AcidCar *c, int id, float k)
{
    static int on = -1;
    if (on < 0) on = getenv("OPENUG2_FXLOG") != NULL;
    if (!on || id < 0 || id >= CARFX_COUNT) return;
    static unsigned n[CARFX_COUNT];
    if (c->fxlog[id]-- > 0) return;
    c->fxlog[id] = 60;
    printf("carfx: car %p fires %d at %.2f (#%u)\n", (void *)c, id, k, ++n[id]);
}

void acid_car_frame(AcidCar *c, const AcidCarFrame *f)
{
    if (!c) return;
    memcpy(c->own.vel, f->vel, sizeof c->own.vel);
    float mph = f->speed * 2.23699f;
    int down = 0;
    for (int w = 0; w < 4; w++) {
        if (!f->contact[w]) continue;
        down++;
        int t = f->surface[w];
        if (t < 0 || t >= 29) t = 0;
        const AcidSurface *S = &acid_surfaces[t];
        float slip = sqrtf(f->slipv[w][0] * f->slipv[w][0] + f->slipv[w][1] * f->slipv[w][1]);
        if (!(f->camdist < 150.0f) || slip <= S->lo[2]) c->wheel_timer[w] = 0;   /* 0x7a55f8 */
        else {
            float tm = (c->wheel_timer[w] += f->dt);
            if (S->fx[2] && *S->fx[2]) {
                float k = (slip - S->lo[2]) / (S->hi[2] - S->lo[2]);
                k *= tm < 0.15f ? 0.0f : tm < 0.7f ? 0.25f : tm < 2.0f ? 0.5f : tm < 4.0f ? 0.75f : 1.0f;
                k = clamp01(k);
                c->smoke[w] = k;
                if (k > 0) {
                    float v[3], *pv = NULL;
                    if (f->spin[w] != 0) {
                        float x = k * 50.0f;
                        if (f->spin[w] > 0) x = -x;
                        for (int j = 0; j < 3; j++) v[j] = x * f->mat[j];      /* 0x441000(x,0,0) */
                        pv = v;
                    }
                    acid_car_fire(c, CARFX_SKID_SMOKE, k, (5 + w) << 16 | t, f->pos, f->mat, pv ? pv : f->vel);
                }
            }
        }
        /* rear tyres throw the surface up while their patch slips forward */
        if (f->camdist < 40.0f && w >= 2 && S->lo[3] < f->slipv[w][0] && S->fx[3] && *S->fx[3]) {
            float k = clamp01((f->slipv[w][0] - S->lo[3]) / (S->hi[3] - S->lo[3]));
            if (k > 0) {
                const float *W = f->wheel_mat[w];
                float v[3];
                for (int j = 0; j < 3; j++) v[j] = f->slipv[w][0] * W[j] + f->slipv[w][1] * W[4 + j] + f->vel[j];
                acid_car_fire(c, CARFX_TIRE_SPEW, k, (5 + w) << 16 | t, f->pos, f->mat, v);
            }
        }
    }
    if (down > 2 && f->centre_surface >= 0 && f->centre_surface < 29) {
        int t = f->centre_surface;
        const AcidSurface *S = &acid_surfaces[t];
        float a = 0, b = 0;
        if (S->fx[0] && *S->fx[0]) a = clamp01((mph - S->lo[0]) / (S->hi[0] - S->lo[0]));
        if (S->fx[1] && *S->fx[1]) b = clamp01((mph - S->lo[1]) / (S->hi[1] - S->lo[1]));
        if (a + b > 0) {
            float ex = a + b - 1.0f;
            if (ex > 0) {
                if (!(S->lo[0] < S->lo[1])) { b -= ex + ex; if (b <= 0) b = 0; }
                else { a -= ex + ex; if (a <= 0) a = 0; }
            }
            if (a > 0) acid_car_fire(c, CARFX_DRIVE_OVER, a, t, f->pos, f->mat, f->vel);
            if (b > 0) acid_car_fire(c, CARFX_DRIVE_OVER2, b, t, f->pos, f->mat, f->vel);
        }
        /* bottoming out: this frame's bump-stop impulses over the surface's
           range (the car sound's event 5 at the same strength is audio's) */
        if (S->hi[4] != S->lo[4]) {
            float k = clamp01((f->bottom - S->lo[4]) / (S->hi[4] - S->lo[4]));
            if (k > 0) { acid_car_fire(c, CARFX_BOTTOM_OUT, k, t, f->pos, f->mat, f->vel); fx_log(c, CARFX_BOTTOM_OUT, k); }
        }
    }
    /* the break effects ApplyDamage triggered (Car::TriggerEffect) */
    for (int id = 0; id < CARFX_COUNT; id++)
        if (c->dmg_fxq[id] > 0.0f) {
            acid_car_fire(c, id, c->dmg_fxq[id], 0, f->pos, f->mat, f->vel);
            c->fxlog[id] = 0; fx_log(c, id, c->dmg_fxq[id]);
            c->dmg_fxq[id] = 0.0f;
        }
    /* damage smoke (0x5f5e75): the engine past 0.9, the radiator past 0.65
       with the last breakable fully broken */
    if (c->dmg_on) {
        float d = acid_damage(c);
        if (d > 0.9f) { acid_car_fire(c, CARFX_DAM_ENGINE, 1.0f, 0, f->pos, f->mat, f->vel); fx_log(c, CARFX_DAM_ENGINE, d); }
        else if ((c->dmg_flags & 0x30000000u) == 0x30000000u && d > 0.65f) {
            acid_car_fire(c, CARFX_DAM_RADIATOR, 1.0f, 0, f->pos, f->mat, f->vel); fx_log(c, CARFX_DAM_RADIATOR, d);
        }
    }
    /* nitrous: the flame while driving on it, the blow-off when it burns
       standing still or off the gas (0x5f5f04) */
    if (f->nitrous) {
        int id = (f->throttle > 0.001f && mph >= 5.0f) ? CARFX_NITRO : CARFX_NOS_BLOWOFF;
        acid_car_fire(c, id, 1.0f, 0, f->pos, f->mat, f->vel);
        fx_log(c, id, 1.0f);
        /* 0x5f6088: burning while the race stage (*0x890118) is 0..3 -- on the
           line -- fires NOS_BLOWOFF (13) once more, (1.0, 0, 0) */
        if (f->nitrous == 2) acid_car_fire(c, CARFX_NOS_BLOWOFF, 1.0f, 0, f->pos, f->mat, f->vel);
    }
    /* revving in neutral (0x5f5f4f): with turbo parts of level 2 or more
       (+0.5 for the junkman turbo), the revs falling to half the band arm it and climbing back past
       three quarters spits one exhaust flame, level / 3.5 strong (the car
       sound's backfire, event 9, is audio's) */
    if (f->gear == 1 && f->flame >= 2.0f) {
        if (!c->flame_armed) {
            if (f->rpm <= f->rpm_range * 0.5f) c->flame_armed = 1;
        } else if (f->rpm >= f->rpm_range * 0.75f) {
            acid_car_fire(c, CARFX_NITRO, f->flame * 0.2857143f, f->gear, f->pos, f->mat, f->vel);
            c->flame_armed = 0;
            c->fxlog[CARFX_NITRO] = 0; fx_log(c, CARFX_NITRO, -f->flame * 0.2857143f);   /* negative: the shift flame */
        }
    }
}

/* ---- CarDamage (0x63a360 .. 0x637c70) ------------------------------------- */
int acid_damage_enabled = 1;
void (*acid_impulse_cb)(const void *sim, const float J[3], const float r[3]);
/* the breakable items (0x8036ec, 0x24 B each: window flag, slot, name, index,
   marker hash, the level thresholds 0x610ea0 tests) */
static const struct { uint32_t hash; float thr[4]; } k_break[15] = {
    /* names: GC BreakableCarDamageInfoTable 0x80372120 (+0x20) */
    { 0xc4ec23c8u, { 0, 0.25f, 0.5f, 0.75f } },  /* Side Mirror Left */
    { 0x0bea251bu, { 0, 0.25f, 0.5f, 0.75f } },  /* Side Mirror Right */
    { 0xaa35c17fu, { 0, 0.25f, 0.5f, 0.75f } },  /* License Plate Rear */
    { 0xc805f7e0u, { 0, 0.25f, 0.5f, 0.75f } },  /* License Plate Front */
    { 0x9db90133u, { 0, 0.25f, 0.35f, 0.5f } },  /* Headlight Left */
    { 0xd09091c6u, { 0, 0.25f, 0.35f, 0.5f } },  /* Headlight Right */
    { 0x31a66786u, { 0, 0.25f, 0.45f, 0.65f } }, /* Brakelight Left */
    { 0xbf700a79u, { 0, 0.25f, 0.45f, 0.65f } }, /* Brakelight Right */
    { 0x6ec7e79fu, { 0, 0.15f, 0.3f, 0.45f } },  /* Window Front (windows: +0x18 env flag) */
    { 0x3f68f920u, { 0, 0.05f, 0.1f, 0.15f } },  /* Window Rear */
    { 0x3df28f69u, { 0, 0.05f, 0.1f, 0.15f } },  /* Window Left Front */
    { 0xe898cfaau, { 0, 0.05f, 0.1f, 0.15f } },  /* Window Left Rear */
    { 0x64b0bb1cu, { 0, 0.05f, 0.1f, 0.15f } },  /* Window Right Front */
    { 0x2f96d0fdu, { 0, 0.05f, 0.1f, 0.15f } },  /* Window Right Rear */
    { 0x68100815u, { 0, 0.7f, 0.85f, 0.95f } },  /* Radiator */
};
/* +0x1c: the CarEffect ApplyDamage triggers (0x803714 +0x18 on PC) */
static const unsigned char k_break_fx[15] = {
    15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 27, 28, 29, CARFX_DAM_RADIATOR };
/* BreakableCarDamageIntensities 0x803914 by level */
static const float k_break_k[4] = { 0.0f, 0.1f, 0.4f, 1.0f };
/* 0x60c1d0: the sector (0..31) of a car-space point, round the box's edge */
static int dmg_sector(const float p[2], const float mn[2], const float mx[2])
{
    float u = (p[0] - mn[0]) / (mx[0] - mn[0]), v = (p[1] - mn[1]) / (mx[1] - mn[1]);
    int iu = (int)lrintf(u * 8.0f), iv = (int)lrintf(v * 8.0f);   /* ftol */
    iu = iu < 0 ? 0 : iu > 7 ? 7 : iu;
    iv = iv < 0 ? 0 : iv > 7 ? 7 : iv;
    if (u > v) return u > 1.0f - v ? 8 - iv : 16 - iu;
    return u > 1.0f - v ? 24 + iu : 16 + iv;
}
float acid_damage(const AcidCar *c)
{
    return c && c->dmg_on && acid_damage_enabled ? (float)c->dmg_sum * (1.0f / 4096.0f) : 0.0f;
}
void acid_damage_repair(AcidCar *c)
{
    if (!c) return;
    memset(c->dmg, 0, sizeof c->dmg); c->dmg_sum = 0; c->dmg_flags = 0;
}
static unsigned dmg_breakables(const AcidCar *c);
/* CarDamage::ApplyDamage 0x633ef0 (GC 8024c850): only with visible damage
   on. BodyDamage::ApplyDamage (0x633d50) only drives the vertex hook, an
   empty function (GC CallDamageVertexFunctionForEveryVertex 801d7338 is 4
   bytes) -> nothing; SetEnvironmentMapping is empty too (GC 8024bb1c);
   PlayGlass is an empty function on both (PC 0x4031c0 ret 4, GC 8007d0e4
   blr) -> no glass sound */
static void dmg_apply(AcidCar *c)
{
    if (!c->dmg_vis) return;
    unsigned fl = dmg_breakables(c);
    c->dmg_flags = fl;
    if (fl == c->dmg_applied) return;
    for (int k = 0; k < 15; k++) {
        unsigned lv = fl >> (2 * k) & 3u, was = c->dmg_applied >> (2 * k) & 3u;
        if (lv == was) continue;
        if (lv) c->dmg_fxq[k_break_fx[k]] = k_break_k[lv];   /* TriggerEffect(fx, k, 0, 0) */
        c->dmg_applied = (c->dmg_applied & ~(3u << (2 * k))) | lv << (2 * k);
    }
    c->dmg_shown = fl;                                       /* UpdateCarReplacementTextures */
    if (getenv("OPENUG2_FXLOG"))
        printf("cardamage: car %p visible damage applied, levels %08x\n", (void *)c, fl);
}
void acid_damage_visible(AcidCar *c, int on)
{
    if (!c || c->dmg_vis == (on != 0)) return;
    c->dmg_vis = on != 0;
    if (on) dmg_apply(c);                                    /* RethinkDamage */
}
int acid_damage_window(const AcidCar *c, int w)
{
    if (!c || w < 0 || w > 5) return 0;
    return (int)(c->dmg_shown >> (2 * (8 + w)) & 3u);
}
void acid_damage_setup(AcidCar *c, const float bbmin[2], const float bbmax[2],
                       const AcidMarker *mk, int nmk)
{
    if (!c || !(bbmax[0] > bbmin[0]) || !(bbmax[1] > bbmin[1])) return;
    c->dmg_on = 1;
    c->dmg_min[0] = bbmin[0]; c->dmg_min[1] = bbmin[1];
    c->dmg_max[0] = bbmax[0]; c->dmg_max[1] = bbmax[1];
    memset(c->dmg_item, -1, sizeof c->dmg_item);                     /* ctor: -1 */
    acid_damage_repair(c);
    for (int i = 0; i < nmk; i++)                                     /* 0x610e00 */
        for (int k = 0; k < 15; k++)
            if (mk[i].hash == k_break[k].hash) {
                c->dmg_item[k] = (signed char)dmg_sector(mk[i].pos, c->dmg_min, c->dmg_max);
                break;
            }
}
/* BodyDamage::SetDamage 0x610a70 */
static void dmg_set(AcidCar *c, int s, float f)
{
    f = f > 1.0f ? 1.0f : f < 0.0f ? 0.0f : f;
    int b = (int)lrintf(f * 128.0f);
    c->dmg_sum += b - c->dmg[s];
    c->dmg[s] = (unsigned char)b;
}
/* RecalculateBreakableDamage 0x610ea0 */
static unsigned dmg_breakables(const AcidCar *c)
{
    unsigned fl = 0;
    for (int k = 0; k < 15; k++) {
        if (c->dmg_item[k] < 0) continue;
        float d = acid_damage_enabled ? c->dmg[(int)c->dmg_item[k]] * (1.0f / 128.0f) : 0.0f;
        int lv = 3;
        while (lv > 0 && d < k_break[k].thr[lv]) lv--;
        fl |= (unsigned)lv << (2 * k);
    }
    return fl;
}
/* AddDamageToSector 0x610f30: a change in the breakables' levels only
   sticks from a hit of 0.2 or more, else the sector keeps its old value */
static void dmg_sector_add(AcidCar *c, int s, float add, float hit)
{
    float cur = c->dmg[s] * (1.0f / 128.0f), nw = cur + add;
    nw = nw > 0.0f ? (nw > 1.0f ? 1.0f : nw) : 0.0f;
    if (cur == nw) return;
    dmg_set(c, s, nw);
    unsigned fl = dmg_breakables(c);
    if (fl == c->dmg_flags) return;
    if (hit > 0.2f) c->dmg_flags = fl;
    else dmg_set(c, s, cur);
}
void acid_damage_full(AcidCar *c)
{
    if (!c || !c->dmg_on) return;
    for (int s = 0; s < 32; s++) dmg_sector_add(c, s, 1.0f, 10000.0f);   /* 0x8033e5c8 / cc */
    dmg_apply(c);
}
void acid_damage_zero(AcidCar *c)
{
    if (!c) return;
    acid_damage_repair(c);
    dmg_apply(c);
}
/* CarCollisionBody::AddDamage 0x593bf0 -> CarDamage::AddDamage 0x637c70 */
void acid_damage_hit(AcidCar *c, const float local[3], float jmag)
{
    if (!c || !c->dmg_on || !acid_damage_enabled) return;
    float a = jmag * 0.013f;                                         /* 0x7fbeec */
    if (!(a >= 0.05f)) return;                                       /* 0x7a677c */
    int s = dmg_sector(local, c->dmg_min, c->dmg_max);
    dmg_sector_add(c, s, a, a);
    float k = a;
    for (int i = 1; i <= 4; i++) {                                   /* x 0.5 each step out */
        k *= 0.5f;
        dmg_sector_add(c, (s + i) & 31, k, a);
        dmg_sector_add(c, (s - i) & 31, k, a);
    }
    dmg_apply(c);                                                    /* GC 8024c680 */
    if (getenv("OPENUG2_FXLOG"))
        printf("cardamage: car %p hit %.3f sector %d -> damage %.3f flags %08x\n",
               (void *)c, a, s, acid_damage(c), c->dmg_flags);
}

/* ---- collisions (0x5a9620 -> 0x5dc4c0) ------------------------------------ */
/* surface table +0x34: which side of a contact names the surface (0x592c10) */
static const float k_prio[29] = {
    0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.5f, 0.2f, 0.1f, 0.1f, 0.1f, 1.0f, 1.0f,
    1.0f, 0.1f, 0.5f, 0.5f, 0.5f, 0.1f, 0.1f, 1.0f, 0.2f, 0.2f, 0.0f, 0.2f, 0.2f, 0.0f };

int acid_contact_surface(int a, int b)
{
    if (a < 0 || a >= 29) a = 0;
    if (b < 0 || b >= 29) b = 0;
    return k_prio[a] < k_prio[b] ? b : a;
}

void acid_world_fx(int type, int surface, float k, const float pt[3], const float vel[3])
{
    if (type < 0 || type >= 7 || surface < 0 || surface >= 29) return;
    ground_build();
    for (AcidEmitter *e = g_ground[type][surface]; e; e = e->next) {
        if (!e->def) continue;
        static const float I[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   /* 0x43c780 */
        memcpy(e->m, I, sizeof I);
        e->m[12] = pt[0]; e->m[13] = pt[1]; e->m[14] = pt[2]; e->m[15] = 1.0f;
        memcpy(e->vel, vel, sizeof e->vel);
        acid_emit(e, -k, NULL);
    }
}

void acid_contact(int surface, const float pt[3], const float imp[3], float slide,
                  const float va[3], const float vb[3])
{
    /* 0x5f0fb0: the nearest view's eye, across the ground (x, y) */
    float best = 1e30f;
    for (int i = 0; i < g_ncam; i++) {
        float dx = pt[0] - g_cam[i][0], dy = pt[1] - g_cam[i][1];
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best) best = d;
    }
    if (!(best < 50.0f)) return;                                    /* 0x7a182c */
    if (surface < 0 || surface >= 29) surface = 0;
    const AcidSurface *S = &acid_surfaces[surface];
    float j = sqrtf(imp[0] * imp[0] + imp[1] * imp[1] + imp[2] * imp[2]);
    if (S->lo[6] < slide) {                                         /* scrape */
        float k = 0.0f, r = S->hi[6] - S->lo[6];
        if (fabsf(r) > 1e-5f) {
            float f = (slide - S->lo[6]) / r;
            if (f > 0.0f) k = f > 1.0f ? 1.0f : f;
        }
        const float *v = va;
        if (vb && sqrtf(va[0] * va[0] + va[1] * va[1] + va[2] * va[2]) <
                  sqrtf(vb[0] * vb[0] + vb[1] * vb[1] + vb[2] * vb[2])) v = vb;
        acid_world_fx(6, surface, k, pt, v);
    }
    if (S->lo[5] < j) {                                             /* hit */
        float f = (j - S->lo[5]) / (S->hi[5] - S->lo[5]), k = 0.0f;
        if (f > 0.0f) k = f > 1.0f ? 1.0f : f;
        static const float zero[3] = { 0, 0, 0 };
        acid_world_fx(5, surface, k, pt, zero);
    }
}

/* ---- flag girl flares (0x439780 manager, 0x433090) ----------------------- */
static AcidEmitter *g_flare; static int g_flare_built;

void acid_flare(const float pos[3], const float dir[3])
{
    if (!g_flare_built) { g_flare_built = 1; g_flare = clones_of(bhash("XECH_FLAG_GIRL_FLARE")); }
    float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), v[3];   /* 0x43cf10 */
    if (l == 0.0f) { v[0] = 1; v[1] = 0; v[2] = 0; }
    else { float r = 1.0f / l; v[0] = dir[0] * r; v[1] = dir[1] * r; v[2] = dir[2] * r; }
    for (int j = 0; j < 3; j++) v[j] *= 2.0f;                                    /* 0x785bfc */
    for (AcidEmitter *e = g_flare; e; e = e->next) {
        if (!e->def) continue;
        static const float I[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        memcpy(e->m, I, sizeof I);
        e->m[12] = pos[0]; e->m[13] = pos[1]; e->m[14] = pos[2];
        memcpy(e->vel, v, sizeof e->vel);
        acid_emit(e, -1.0f, NULL);                                               /* 0x785bf8 */
    }
}

/* ---- surface table 0x800ee0 (+0x44.. fx group names and ranges) ---------- */
#define S7(a,al,ah, b,bl,bh, c,cl,ch, d,dl,dh, e,el,eh, f,fl,fh, g,gl,gh) \
    { a,b,c,d,e,f,g }, { al,bl,cl,dl,el,fl,gl }, { ah,bh,ch,dh,eh,fh,gh }
const AcidSurface acid_surfaces[29] = {
    { "NONE",        S7("",0,0, "",50,100, "",50,100, "",50,100, "",50,100, "",50,100, "",20,50) },
    { "ROAD",        S7("XETT_ROAD_DR",20,100, "",50,100, "XETT_ROAD_SK",5,40, "XETT_ROAD_SP",2,200, "XEBO_ROAD",1,2, "XEBO_ROAD",10,15, "XECS_SOLID_WALL",5,50) },
    { "ROAD_WET",    S7("XETT_ROAD_DR",20,100, "",50,100, "XETT_ROAD_SK",5,40, "XETT_ROAD_SP",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "ROAD_DRIFT",  S7("XETT_ROAD_DR",20,100, "",50,100, "XETT_ROAD_SK",5,40, "XETT_ROAD_SP",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "ROAD_SMOKE_1",S7("",20,100, "",50,100, "XEM_ROADSKBLUE",5,40, "",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "ROAD_SMOKE_2",S7("",20,100, "",50,100, "XEM_ROADSKYELW",5,40, "",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "ROAD_SMOKE_3",S7("",20,100, "",50,100, "XEM_ROADSKRED",5,40, "",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "ROAD_BRIDGE", S7("XETT_ROAD_DR",20,100, "",50,100, "XETT_ROAD_SK",5,40, "XETT_ROAD_SP",2,200, "XEBO_ROAD",1,2, "",50,100, "",50,50) },
    { "DIRT",        S7("",3,30, "XETT_DIRTRD_DR",35,100, "XETT_DIRTRD_SK",5,20, "XETT_DIRTRD_SP",1.5f,10, "XEBO_DIRT",1,2, "",50,100, "",50,100) },
    { "GRAVEL",      S7("",3,30, "XETT_DIRTRD_DR",35,100, "XETT_DIRTRD_SK",5,20, "XETT_DIRTRD_SP",1.5f,10, "XEBO_DIRT",1,2, "",50,100, "",50,100) },
    { "ROUGH_ROAD",  S7("XETT_RRCSSW_DR",15,100, "",50,100, "XETT_RRCSSW_SK",5,20, "XETT_RRCSSW_SP",2,100, "XEBO_ROAD",1,2, "",50,100, "",50,100) },
    { "COBBLESTONE", S7("XETT_RRCSSW_DR",15,100, "",50,100, "XETT_RRCSSW_SK",5,20, "XETT_RRCSSW_SP",2,100, "XEBO_ROAD",1,2, "",50,100, "",50,100) },
    { "STAIRS",      S7("XETT_RRCSSW_DR",15,100, "",50,100, "XETT_RRCSSW_SK",5,20, "XETT_RRCSSW_SP",2,100, "XEBO_ROAD",1,2, "",50,100, "",50,100) },
    { "PUDDLE",      S7("XETT_PUDDLE_DRSL",3,30, "XETT_PUDDLE_DR",35,100, "XETT_PUDDLE_SK",5,20, "XETT_PUDDLE_SP",1,100, "",0.6f,3, "",50,100, "",50,100) },
    { "DEEP_WATER",  S7("XETT_DEEPWATER_DRSL",3,30, "XETT_DEEPWATER_DR",35,100, "XETT_DEEPWATER_SK",5,20, "XETT_DEEPWATER_SP",1,100, "",0.6f,3, "",50,100, "",50,100) },
    { "GRASS",       S7("",3,30, "XETT_XETT_GRASS_DR",35,100, "XETT_GRASS_SK",5,20, "XETT_GRASS_SP",2,200, "XEBO_DIRT",1,2, "XECS_GRASS",15,100, "XECS_GRASS",15,50) },
    { "SIDEWALK",    S7("XETT_RRCSSW_DR",15,100, "",50,100, "XETT_RRCSSW_SK",5,20, "XETT_RRCSSW_SP",2,100, "XEBO_ROAD",1,2, "",50,100, "",50,100) },
    { "WOOD",        S7("",20,100, "",50,100, "",20,20, "",20,100, "",0.6f,3, "XECS_WOOD_WALL",4,15, "XECS_WOOD_WALL",15,50) },
    { "PLASTIC",     S7("",20,100, "",50,100, "",20,20, "",20,100, "",0.6f,3, "XECS_PLASTIC_WALL",4,15, "XECS_PLASTIC_WALL",15,50) },
    { "GLASS",       S7("",20,100, "",50,100, "",20,20, "",20,100, "",0.6f,3, "XECS_GLASS_WALL",4,15, "XECS_GLASS_WALL",15,50) },
    { "SOLID_WALL",  S7("",20,100, "",50,100, "",20,100, "",20,100, "",0.6f,3, "XECS_SOLID_WALL",10,15, "XECS_SOLID_WALL",15,50) },
    { "SEE_THROUGH_WALL", S7("",20,100, "",50,100, "",20,100, "",20,100, "",0.6f,3, "",10,15, "",15,50) },
    { "PLANT",       S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "POST",        S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "PILLAR",      S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "METAL_GRATE", S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "METAL",       S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "CHAINLINK",   S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "",50,100, "",50,100) },
    { "CAR",         S7("",15,100, "",50,100, "",2,100, "",5,100, "",0.6f,3, "XECI_CAR",10,15, "",2,4) },
};

/* ---- emitters owned by others (the smokeables) ----------------------- */
AcidEmitter *acid_clones(uint32_t group) { return clones_of(group); }
void acid_free_clones(AcidEmitter *list) { free_list(list); }
AcidEmitter *acid_copy(const AcidEmitter *t)            /* afxCopyEmitter 0x48b830, state 1 */
{
    AcidEmitter *e = t ? clone(t) : NULL;
    if (e) e->state = 1;
    return e;
}
void acid_auto_add(AcidEmitter *e) { if (e) world_add(e); }
void acid_auto_remove(AcidEmitter *e)                   /* RemoveAutoEmitter, then the delete */
{
    if (!e) return;
    for (AcidEmitter **pp = &g_world; *pp; pp = &(*pp)->next)
        if (*pp == e) { *pp = e->next; break; }
    if (e->group && e->group->em == e) e->group->em = NULL;
    free(e);
}

/* KillActiveParticlesForEmitter: every group the emitter fills, and its particles */
static void kill_emitter(AcidEmitter *e)
{
    pools_init();
    for (AcidGroup *g = g_ghead.next, *gn; g != &g_ghead; g = gn) {
        gn = g->next;
        if (g->em != e) continue;
        for (int l = 0; l < 2; l++)
            for (AcidParticle *p = g->head[l].next, *pn; p != &g->head[l]; p = pn) { pn = p->next; particle_free(g, l, p); }
        group_free(g);
    }
}
void acid_trigger_init(AcidTrigger *t, const char *group)
{
    memset(t, 0, sizeof *t);
    t->tmpl = clones_of(bhash(group));
}
void acid_trigger_off(AcidTrigger *t)
{
    if (!t->on) return;
    for (int i = 0; i < t->nlive; i++) { kill_emitter(t->live[i]); acid_auto_remove(t->live[i]); }
    t->nlive = 0;
    t->pos[0] = t->pos[1] = t->pos[2] = 0;
    t->on = 0;
}
void acid_trigger_free(AcidTrigger *t)
{
    acid_trigger_off(t);
    free_list(t->tmpl);
    memset(t, 0, sizeof *t);
}
int acid_trigger_place(AcidTrigger *t, const float pos[3], int (*ground)(float x, float y, float *z))
{
    if (t->on) {
        if (!(fabsf(t->pos[0] - pos[0]) <= 0.001f && fabsf(t->pos[1] - pos[1]) <= 0.001f &&
              fabsf(t->pos[2] - pos[2]) <= 0.001f))                      /* bEqual(.., 0.001) */
            acid_trigger_off(t);
        if (t->on) return 1;
    }
    if (t->tmpl) {
        float p[3] = { pos[0], pos[1], pos[2] };
        if (p[2] == 0.0f && !(ground && ground(p[0], p[1], &p[2]))) return 0;   /* GetElevation */
        for (AcidEmitter *e = t->tmpl; e && t->nlive < 8; e = e->next) {
            AcidEmitter *c = acid_copy(e);
            if (!c) break;
            memset(c->m, 0, sizeof c->m);                                /* identity, at p */
            c->m[0] = c->m[5] = c->m[10] = c->m[15] = 1.0f;
            c->m[12] = p[0]; c->m[13] = p[1]; c->m[14] = p[2];
            acid_auto_add(c);
            t->live[t->nlive++] = c;
        }
    }
    memcpy(t->pos, pos, sizeof t->pos);
    t->on = 1;
    return 1;
}

void acid_shutdown(void)
{
    acid_clear();
    for (int t = 0; t < 7; t++) for (int s = 0; s < 29; s++) { free_list(g_ground[t][s]); g_ground[t][s] = NULL; }
    free_list(g_flare); g_flare = NULL; g_flare_built = 0;
    g_ground_built = 0;
    free_list(g_world); g_world = NULL;
    free(g_def); g_def = NULL; g_ndef = g_capdef = 0;
}
