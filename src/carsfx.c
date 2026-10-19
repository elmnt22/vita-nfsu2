/* carsfx.c — the opponents' and traffic's engine sounds (see carsfx.h).
 * Addresses are SPEED2.EXE; RE notes in re-tools/out/AUDIO_AI.md. */
#include "carsfx.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "carsfx_tables.h"

/* ---- the original's integer mix math ------------------------------------ */

int carsfx_mb2lin(int mb)                               /* 0x467ff0 */
{
    if (mb == 0) return 0x7fff;
    int q = -(mb / 602), r = mb % 602;
    if (q > 31) return 0;
    int v = k_db2lin[r + 601] >> q;
    return v < 50 ? 0 : v;
}

int carsfx_lin2mb(unsigned lin)                         /* 0x468050 */
{
    static const unsigned mask[12] = { 0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff };
    if (lin == 0) return -10000;
    int k = 0;
    while (!(lin & (0x4000u >> k))) {
        if (k == 16) return -10000;
        k++;
    }
    int m = (int)lin - (int)(0x4000u >> k);
    unsigned idx = k < 5 ? (unsigned)(m >> (5 - k)) : ((unsigned)m << (k - 5)) | mask[k - 5];
    if (idx > 511) idx = 511;
    int mb = (-1 - k) * 602 + k_lin2db[idx];
    return mb < -10000 ? -10000 : mb;
}

static int fade_at(int i) { return k_fade[i < 0 ? 0 : i > 513 ? 513 : i]; }

int carsfx_curve(unsigned type, unsigned x, int db)     /* 0x468160 */
{
    for (;;) {
        int i = (int)x >> 6, v;
        int fr = (int)((x & 0x1e) << 9 | 0x3ff);
        switch (type) {
        case 0:
            v = fade_at(i);
            if (v) v += ((fade_at(i + 1) - v) * fr) >> 15;
            goto out;
        case 4: {
            int a = 0x7fff - fade_at(0x1ff - i), b = 0x7fff - fade_at(0x200 - i);
            v = (((b - a) * fr) >> 15) + a;
            goto out;
        }
        case 2: case 6:
            if (!db) { v = carsfx_curve(type - 2, x, 0); return v * v >> 15; }
            v = carsfx_curve(type - 2, x, 1) << 1;
            return v > -0x2581 ? v : -10000;
        case 8:
            return db ? carsfx_lin2mb(0x7fff - x) : (int)(0x7fff - x);
        case 1: case 3: case 5: case 7: case 9:
            if (db) return -carsfx_curve(type - 1, x, 1);
            x = 0x7fff - x; type--;
            continue;
        default:
            return 0;
        }
out:
        if (!db) return v;
        v = carsfx_lin2mb((unsigned)v);
        return v > -0x2581 ? v : -10000;
    }
}

static int ftol(float f) { return (int)f; }             /* 0x75bed4 (truncates) */
static float sin16(unsigned a) { return sinf((float)(a & 0xffff) * 9.58738e-05f); }  /* 0x43c2b0 */
/* 0x43c490: arcsine in 16-bit angle units (0x4000 = 90 deg); the original
   reads a table, this is the same function */
static int asin16(float v)
{
    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
    return ftol(asinf(v) * (32768.0f / 3.14159265f));
}
static unsigned g_seed = 0x1d872b41u;                   /* 0x7ea624 */
static unsigned rng_int(unsigned n)                     /* 0x43c1c0 */
{
    unsigned r = g_seed % n, a = g_seed ^ 0x1d872b41u, b = a >> 5 ^ a;
    g_seed = b << 27 ^ b ^ a;
    return r;
}
static float rng_float(float x)                         /* 0x43c1f0 */
{
    unsigned r = g_seed % 0x7fffffffu, a = g_seed ^ 0x1d872b41u, b = a >> 5 ^ a;
    g_seed = b << 27 ^ b ^ a;
    return (float)r * 4.65661e-10f * x;
}

/* ---- the mixmaps (SOUND/MIXMAPS/MAPOUTPUT*.mxb) ------------------------- */

typedef struct { int mode, dsel, asel, curve[4], lo[4], hi[4]; } MixSub;
typedef struct { int nsub; MixSub sub[8]; } Mix3D;
/* a master channel: its output id, level, 3D record, and the object
   slots it feeds (out-map, header [5]: entry slot (e >> 16) & 0xf, mB
   offset (short)e; evaluator 0x477aa0) */
typedef struct { unsigned out; int level, rec, nsl, slot[8], off[8]; } MixMaster;
typedef struct { int n3d; Mix3D r3d[8]; int nmst; MixMaster mst[16]; } MixClass;
static MixClass g_mix[4][6];    /* [mixmap][class 0..5]; AI = 2, traffic = 3, collisions = 5 */
static int g_mixmap;

static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static int mix_parse(const unsigned char *d, long len, MixClass *out)
{
    if (len < 0x20) return 0;
    unsigned nmaps = rd32(d + 4);
    for (unsigned m = 0; m < nmaps && m < 9; m++) {
        unsigned o = rd32(d + 0x10 + 4 * m);
        if (o + 0x20 > (unsigned)len) return 0;
        unsigned cls = rd32(d + o) & 0xffff;
        if (cls > 5) continue;
        MixClass *mc = &out[cls];
        memset(mc, 0, sizeof *mc);
        unsigned t3 = rd32(d + o + 8), tm = rd32(d + o + 16), om = rd32(d + o + 20);
        if (t3 != 0xffffffffu && o + t3 + 0x10 <= (unsigned)len) {      /* 3D ctls (0x468580) */
            unsigned p = o + t3, cnt = rd32(d + p), q = p + 0x10;
            for (unsigned r = 0; r < cnt && r < 8; r++) {
                if (q + 4 > (unsigned)len) return 0;
                unsigned h = rd32(d + q), ns = h >> 24 & 0xf;
                Mix3D *x = &mc->r3d[r];
                x->nsub = ns > 8 ? 8 : (int)ns;
                for (unsigned s = 0; s < ns; s++) {
                    const unsigned char *b = d + q + 4 + 24 * s;
                    if (b + 24 > d + len) return 0;
                    if (s >= 8) continue;
                    unsigned s0 = rd32(b);
                    MixSub *u = &x->sub[s];
                    u->mode = (int)(s0 >> 24 & 0xf);
                    u->dsel = (int)(s0 >> 12 & 0xf);
                    u->asel = (int)(s0 >> 8 & 0xf);
                    /* the curve of the quadrant's first edge (0x468580: front
                       >>28, side >>16, back >>24, other side >>20) */
                    unsigned cv = rd32(b + 4);
                    u->curve[0] = (int)(cv >> 28 & 0xf); u->curve[1] = (int)(cv >> 16 & 0xf);
                    u->curve[2] = (int)(cv >> 24 & 0xf); u->curve[3] = (int)(cv >> 20 & 0xf);
                    for (int e = 0; e < 4; e++) {
                        unsigned v = rd32(b + 8 + 4 * e);
                        u->lo[e] = (int)(v & 0x7fff); u->hi[e] = (int)(v >> 16 & 0x7fff);
                    }
                }
                q += 4 + 24 * ns;
                mc->n3d = (int)r + 1;
            }
        }
        if (tm != 0xffffffffu && o + tm + 0x10 <= (unsigned)len) {      /* channels (0x479030) */
            unsigned p = o + tm, cnt = rd32(d + p), q = p + 0x10;
            for (unsigned r = 0; r < cnt; r++) {
                if (q + 12 > (unsigned)len) return 0;
                unsigned h = rd32(d + q), l = rd32(d + q + 4), ni = h >> 16 & 0xff;
                if (h & 0x10000000u) { q += 8 + 4 * ni; continue; }   /* sub channel */
                if (mc->nmst < 16) {
                    MixMaster *mm = &mc->mst[mc->nmst++];
                    mm->out = rd32(d + q + 8);
                    mm->level = (short)(l >> 16);
                    mm->rec = -1;
                    for (unsigned k = 0; k < ni; k++) {
                        unsigned in = rd32(d + q + 12 + 4 * k);
                        if ((in >> 28) == 9 && mm->rec < 0) mm->rec = (int)(in & 0xff);
                    }
                    /* the channel's out-map group, in channel order */
                    if (om != 0xffffffffu && o + om + 4 <= (unsigned)len) {
                        unsigned c = rd32(d + o + om) & 0xf;
                        if (o + om + 4 + 4 * c > (unsigned)len) return 0;
                        for (unsigned k = 0; k < c; k++) {
                            unsigned e = rd32(d + o + om + 4 + 4 * k);
                            if (mm->nsl < 8) {
                                mm->slot[mm->nsl] = (int)(e >> 16 & 0xf);
                                mm->off[mm->nsl++] = (short)(e & 0xffff);
                            }
                        }
                        om += 4 * (c + 1);
                    }
                }
                q += 12 + 4 * ni;
            }
        }
    }
    return 1;
}

static void fx_load(const char *dataroot);
static void co_reset(void);

int carsfx_init(const char *dataroot)
{
    static const char *const fn[4] = { "MAPOUTPUT", "MAPOUTPUTDFT", "MAPOUTPUTDRG", "MAPOUTPUTURL" };
    int n = 0;
    memset(g_mix, 0, sizeof g_mix);
    for (int m = 0; m < 4; m++) {
        char path[512];
        snprintf(path, sizeof path, "%s/SOUND/MIXMAPS/%s.mxb", dataroot, fn[m]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
        if (d && fread(d, 1, (size_t)len, f) == (size_t)len && mix_parse(d, len, g_mix[m])) n++;
        free(d); fclose(f);
    }
    fx_load(dataroot);
    printf("car sounds: %d of 4 mixmaps (AI engine %d mB, traffic engine %d mB)\n", n,
           g_mix[0][2].nmst ? -g_mix[0][2].mst[0].level : 0, g_mix[0][3].nmst ? -g_mix[0][3].mst[0].level : 0);
    return n;
}

/* the 3D gain of record r for a source at distance d (by sel) and azimuth
   az (0x468580 -> 0x468380), mB */
static int mix_3d(const Mix3D *x, int mode, const int d[2], const unsigned az[2])
{
    if (x->nsub <= 0) return -10000;
    const MixSub *u = &x->sub[0];
    for (int s = 0; s < x->nsub; s++)
        if (x->sub[s].mode == mode) { u = &x->sub[s]; break; }
    int dist = d[u->dsel ? 1 : 0];
    unsigned a = az[u->asel ? 1 : 0] & 0xffff;
    int q = (int)(a >> 14), f = (int)(a & 0x3fff), qb = (q + 1) & 3;
    if (dist > u->hi[q] && dist > u->hi[qb]) return -10000;
    int xs[2];
    for (int k = 0; k < 2; k++) {
        int e = k ? qb : q, lo = u->lo[e], hi = u->hi[e];
        int c = dist < lo ? lo : dist > hi ? hi : dist;
        xs[k] = hi > lo ? ftol((float)(c - lo) / (float)(hi - lo) * 32767.0f) : (dist <= lo ? 0 : 32767);
    }
    int c0 = carsfx_curve((unsigned)u->curve[q], (unsigned)xs[0], 0);
    int c1 = f ? carsfx_curve((unsigned)u->curve[q], (unsigned)xs[1], 0) : 0x7fff;
    return carsfx_lin2mb((unsigned)((((0x7fff - 2 * f) * c0) >> 15) + ((2 * f * c1) >> 15)));
}

/* an object's mixmap volume in one slot (vt+0x38 0x45e390): its master
   channel's level plus the 3D record it reads (the channels' other inputs
   -- sub channels with 0xa0000000-type inputs and NFSMixCtl -- are not
   decoded: 0 dB), floor -level - 10000 (0x479030), plus the slot's out-map
   offset (0x477aa0: below -10000 -> 0, above 0 -> 0x7fff, else 0x467ff0).
   A slot no channel feeds -> 0. */
static int mix_objvol(int cls, unsigned out, int slot, int mode, const int d[2], const unsigned az[2])
{
    const MixClass *mc = &g_mix[g_mixmap][cls];
    for (int i = 0; i < mc->nmst; i++) {
        const MixMaster *m = &mc->mst[i];
        if (m->out != out) continue;
        for (int k = 0; k < m->nsl; k++) {
            if (m->slot[k] != slot) continue;
            int mb = -m->level;
            if (m->rec >= 0 && m->rec < mc->n3d) mb += mix_3d(&mc->r3d[m->rec], mode, d, az);
            if (mb < -m->level - 10000) mb = -m->level - 10000;
            mb += m->off[k];
            if (mb < -10000) return 0;
            if (mb > 0) return 0x7fff;
            return carsfx_mb2lin(mb) & 0x7fff;
        }
    }
    return 0;
}

/* ---- the 3D position controller (SFXCTL_3DCarPos 0x45df00 / 0x45e1f0) --- */

typedef struct {
    int d[2];               /* +8 to the camera, +4 to the player's car (m, int) */
    unsigned az[2];         /* +0x10 camera-relative, +0xc car-relative */
    float dcur, dprev; int have;
    float dop[4]; int ndop, idop;
} Pos3D;

static unsigned azimuth(const float src[2], const float ref[2], const float fwd[2])   /* 0x45de60 */
{
    float vx = src[0] - ref[0], vy = src[1] - ref[1], l = sqrtf(vx * vx + vy * vy);
    float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    float fx = fl > 0 ? fwd[0] / fl : 1, fy = fl > 0 ? fwd[1] / fl : 0;
    if (l > 0) { vx /= l; vy /= l; }
    int s = 0x4000 - asin16(vx * fx + vy * fy);
    if (vx * fy - vy * fx < 0) s = -1 - s;
    return (unsigned)s & 0xffff;
}

static void pos3d_update(Pos3D *p, const float pos[2], const CarSfxListener *L, float dt)
{
    p->az[0] = azimuth(pos, L->cam, L->cam_fwd);
    p->d[0] = ftol(hypotf(pos[0] - L->cam[0], pos[1] - L->cam[1]));
    p->az[1] = azimuth(pos, L->car, L->car_fwd);
    p->d[1] = ftol(hypotf(pos[0] - L->car[0], pos[1] - L->car[1]));
    /* Doppler on the player-car distance (+0x6c = 1) */
    p->dcur = (float)p->d[1];
    float r = 1.0f;
    if (p->have && p->dcur >= 0 && p->dprev >= 0 && dt > 0)
        r = 343.0f / ((p->dcur - p->dprev) / dt + 343.0f);
    p->dop[p->idop] = r; p->idop = (p->idop + 1) & 3;   /* 4-sample average (0x576970) */
    if (p->ndop < 4) p->ndop++;
    p->dprev = p->dcur; p->have = 1;
}
static float pos3d_pitch(const Pos3D *p, int idx)       /* 0x45e290 */
{
    static const float scale[6] = { 0.35f, 0.33f, 0.6f, 0.3f, 0.3f, 0.3f };   /* 0x7f3760 */
    float a = 0;
    for (int i = 0; i < p->ndop; i++) a += p->dop[i];
    a = p->ndop ? a / (float)p->ndop : 1.0f;
    float v = (a - 1.0f) * scale[idx] + 1.0f;
    return v > 0.8f ? (v > 1.2f ? 1.2f : v) : 0.8f;
}

/* ---- ramps (0x459990 init, 0x46b100 step) ------------------------------- */

typedef struct { float t, dur, from, to; int curve; float val; int done; } Ramp;

static void ramp_init(Ramp *r, float from, float to, int ms, int curve)
{
    r->dur = (float)ms * 0.001f;
    if (r->dur <= 0) r->dur = 0.01f;
    r->to = to; r->from = from; r->curve = curve;
    r->done = 0; r->val = from; r->t = 0;
}
static void ramp_step(Ramp *r, float dt)
{
    if (r->done) return;
    float t = dt + r->t;
    r->t = t;
    if (t > r->dur) { r->val = r->to; r->done = 1; return; }
    float u = t / r->dur;
    switch (r->curve) {
    case 1: break;
    case 2: r->val = (r->to - r->from) * u * u + r->from; break;
    case 3: { float f = (1.0f / r->dur) * t - 1.0f; r->val = (r->to - r->from) * (1.0f - f * f) + r->from; break; }
    case 6: {
        int x = ftol(u * 32767.0f);
        x = x < 1 ? 0 : x > 0x7fff ? 0x7fff : x;
        int c = carsfx_curve(2, (unsigned)(0x7fff - x), 0);
        r->val = (float)c * 3.05185e-05f * (r->to - r->from) + r->from;
        break;
    }
    default: r->val = u * (r->to - r->from) + r->from; break;
    }
}
/* step toward a moving target (0x47aaa0 / the tracking cases) */
static void ramp_track(Ramp *r, float target, float dt)
{
    r->to = target;
    ramp_step(r, dt);
    if (r->done) r->val = target;
}

/* a running average (0x576970 push, 0x576a10 refill) */
typedef struct { float h[16]; int n, cnt, i; float avg; } Avg;
static void avg_init(Avg *a, int n) { memset(a, 0, sizeof *a); a->n = n; }
static void avg_push(Avg *a, float x)
{
    a->h[a->i] = x; a->i = (a->i + 1) % a->n;
    if (a->cnt < a->n) a->cnt++;
    float s = 0;
    for (int k = 0; k < a->cnt; k++) s += a->h[k];
    a->avg = s / (float)a->cnt;
}
static void avg_fill(Avg *a, float x)
{
    for (int k = 0; k < a->n; k++) a->h[k] = x;
    a->cnt = a->n; a->avg = x;
}

/* ---- SFXCTL_AIPhysics (vt 0x78a030, update 0x47c490) --------------------- */

/* a threshold tracker (0x45b4c0, config 0x45b7c0) */
typedef struct { float a, b, c, d, e; Avg f; int on; float t1, t2; } Track;
static void track_init(Track *t, float a, float b, float c, float d, float e, int n)
{
    memset(t, 0, sizeof *t);
    t->a = a; t->b = b; t->c = c; t->d = d; t->e = e; avg_init(&t->f, n);
    t->t1 = c;
}
static void track_step(Track *t, float x, float dt)
{
    avg_push(&t->f, x);
    float v = t->f.avg * t->e;
    if (!t->on) {
        if (v > t->b * t->e) { t->on = 1; t->t2 = t->d; return; }
        if (!(v > t->a * t->e)) { t->t1 = t->c; return; }
        t->t1 -= dt;
        if (!(t->t1 < 0)) return;
        t->on = 1; t->t2 = t->d;
    } else if (!(t->a * t->e < v)) {
        t->t2 -= dt;
        if (!(t->t2 < 0)) return;
        t->on = 0; t->t1 = t->c;
    } else t->t2 = t->d;
}

/* AccelTrans records (0x7f3284 / 0x7f32c0, one per car at random) */
typedef struct { float r0; int rpm_up, ms, settle_ms, back_ms; } AtRec;
static const AtRec k_atrec[2] = { { 0.5f, 7000, 350, 3000, 1500 }, { 0.5f, 6500, 400, 2500, 1500 } };
/* the AI shift record 0x7f2ec0, fields +0xc .. +0x5c in order (+8 and
   +0x40, the 0.05 s shift-sound delays, are for the shift sound the AI
   does not have) */
static const struct {
    int up_drop, up_ms, up_bump, up_bump_ms, vol_ms; float vol; int w2_amp, w2_period, w2_ms,
        w1_amp, w1_period, w1_ms, unk3c, dn_blip, dn_ms, dn_rise, dn_rise_ms, dn_fall, dn_fall_ms;
    float dn_k;
} k_shrec = {
    600, 450, 1500, 320, 500, 0.2f, 6000, 250, 700, 400, 250, 550,
    12000, 800, 250, 1200, 600, 500, 370, 0.85f };
/* the downshift rpm per gear (0x7886ac) */
static const float k_D[9] = { 6000, 6000, 5700, 6200, 6650, 7000, 7300, 7400, 7500 };
/* the engine slots' recorded max / min rpm (0x7f0de0 +0x1c / +0x20) */
static const float k_slot_rpm[41][2] = {
    {9229, 1350}, {8222, 1500}, {8007, 1250}, {8311, 1250}, {8836, 1000}, {8338, 1000}, {6888, 1000},
    {7099, 800}, {7145, 1000}, {8292, 1450}, {7239, 1100}, {8936, 1250}, {6509, 750}, {5672, 750},
    {5121, 750}, {6975, 1000}, {6313, 1000}, {6881, 1000}, {7037, 1526}, {8222, 1250}, {7158, 1500},
    {7085, 1500}, {8284, 1250}, {7158, 1500}, {7363, 1750}, {6599, 1200}, {6510, 1200}, {6596, 1250},
    {6773, 1000}, {5653, 1350}, {6548, 1250}, {6566, 1150}, {6840, 1250}, {6786, 1200}, {7301, 1250},
    {7421, 1250}, {6508, 1250}, {6958, 1000}, {4782, 1000}, {7052, 1000}, {8326, 1250} };

typedef struct {
    int on, slot, bank, detune;
    float time;
    /* AIPhysics */
    Track tr[4];            /* +0x100 steer+, +0x14c steer-, +0x198 speeding up, +0x1e4 slowing */
    int sm, sm_prev; float sm_t;                    /* +0x280.. */
    int bc, gear, pgear;    /* +0xbc on throttle, +0xc8 gear, +0xcc previous */
    float rpm, thr, off, scale, slew;               /* +0x70 +0x84 +0x298 +0x294 +0x2b0 */
    float bc100, pbc100;                            /* +0x8c +0x90 */
    float vprev; int have_v;
    struct { float thr, rpm; int bc, gear; float bc100; } out;     /* +0x68 block */
    /* AIShifting (0x483130) */
    int sh;                 /* +0x78 */
    float a8;
    Ramp sthr, srpm, svol, w1, w2, strk;            /* +0xac +0xc8 +0xe4 +0x104 +0x120 +0x140 */
    int wob;                /* +0x13c */
    int w80, w84, w88, w8c, w90, w94;
    /* AccelTrans (0x4831a0) */
    int at; float at_cc; int at_bc, at_pbc; const AtRec *atr;
    Ramp arpm, a90, athr;   /* +0x74 +0x90 +0xac */
    /* SFXCTL_Engine (0x47c010) */
    float erpm, eprev, load, volw, rpmw, loadw; int vol;
    unsigned short ph14c, ph14e, ph150;
    Avg lf;                 /* +0x90 load average */
    int lpf;                /* +0x138 */
    Pos3D p3;
} AiSnd;

static AiSnd g_ai[CARSFX_AI_MAX];
static int g_nai, g_streetx, g_drift, g_ndetune;

static void ai_reset(AiSnd *a)
{
    memset(a, 0, sizeof *a);
    track_init(&a->tr[0], 1, 3, 0.5f, 0.5f, 1, 15);
    track_init(&a->tr[1], -1, -3, 0.5f, 0.5f, -1, 15);
    track_init(&a->tr[2], 0, 1, 0.4f, 0.5f, 1, 3);
    track_init(&a->tr[3], -0.1f, -1, 0.1f, 0.5f, -1, 3);
    a->gear = a->pgear = 2; a->rpm = 1000; a->scale = 0.1f;
    a->out.rpm = 1000; a->out.gear = 2;
    a->erpm = a->eprev = 1000;
    a->ph14c = a->ph14e = a->ph150 = 0x4097;
    avg_init(&a->lf, 3);
    a->atr = &k_atrec[rng_int(2)];
}

/* 0x46d620 */
static void sm_set(AiSnd *a, int s) { a->sm_t = a->time; a->sm_prev = a->sm; a->sm = s; }
static void sm_step(AiSnd *a, int prerace, float mph)
{
    int f1 = prerace, f2 = mph < 5.0f, f3 = a->tr[2].on, f4 = a->tr[3].on;
    int f5 = f4 && a->tr[0].on, f6 = f4 && a->tr[1].on;
    static const float hold[8] = { 1, 1, 1, 1, 0.5f, 0.5f, 0.5f, 24 };   /* 0x788578 */
    switch (a->sm) {
    case 0: sm_set(a, f1 ? 1 : 3); break;
    case 1: if (!f1) sm_set(a, 3); break;
    case 2: if (f3) sm_set(a, 3); break;
    case 3:
        if (a->time - 1.0f < a->sm_t) break;
        if (f5) sm_set(a, 5); else if (f6) sm_set(a, 6); else if (f4) sm_set(a, 4); else if (f2) sm_set(a, 2);
        break;
    case 4:
        if (a->time - 0.5f < a->sm_t) break;
        if (f5) sm_set(a, 5); else if (f6) sm_set(a, 6); else if (f3 && !f4) sm_set(a, 3);
        break;
    case 5: case 6:
        if (a->time - hold[a->sm] < a->sm_t) break;
        if (f3) { if (!f4) sm_set(a, 3); else sm_set(a, 4); }
        else if (f4) sm_set(a, 4);
        break;
    }
}

static void aiphys_update(AiSnd *a, const CarSfxCar *c, const CarSfxListener *L, float dt)
{
    float mph = c->speed * 2.237f;
    if (c->nis) {                   /* 0x46fa30: the scene's rev pattern has the engine */
        a->out.rpm = c->nis_rpm; a->out.thr = c->nis_thr;
        return;
    }
    /* 0x47bcc0: the trackers and the state machine */
    float deg = (float)c->steer * 0.00549325f;
    if (deg > 180.0f) deg -= 360.0f;
    track_step(&a->tr[0], deg, dt);
    track_step(&a->tr[1], deg, dt);
    /* car +0x430 minus the length of +0x520 (the previous tick's
       velocity): the speed gained over one simulation tick */
    float ticks = dt * 60.0f;
    float dv = a->have_v && ticks > 0 ? (c->speed - a->vprev) / ticks : 0.0f;
    a->vprev = c->speed; a->have_v = 1;
    track_step(&a->tr[2], dv, dt);
    track_step(&a->tr[3], dv, dt);
    sm_step(a, !L->racing, mph);
    a->pbc100 = a->bc100;
    /* 0x45d560: the rev slew */
    float k1, k2; const float *G;
    static const float G0[9] = { 40, 40, 40.5f, 21, 12, 6.5f, 3.6f, 2, 1 };       /* 0x788630 */
    static const float Gd[9] = { 40, 40, 40.5f, 21, 12, 7.5f, 3.6f, 2, 1 };       /* 0x788688 */
    static const float Gx[9] = { 40, 40, 30.5f, 12.5f, 6, 3.5f, 2.6f, 1.5f, 1 };  /* 0x78865c */
    if (g_drift) { k1 = 3; k2 = 1.5f; G = Gd; }
    else if (g_streetx) { k1 = 4; k2 = 1.5f; G = Gx; }
    else { k1 = 3.02f; k2 = 2.5f; G = G0; }
    int gi = a->gear < 0 ? 0 : a->gear > 8 ? 8 : a->gear;
    float s = k1 * G[gi] * (a->bc ? 1.0f : k2);
    if (a->gear < 5) s *= a->scale;
    a->slew = dt * 33.3333f * s;
    /* 0x45d670 */
    float sc = fabsf(a->tr[2].f.avg) / 0.18f;
    a->scale = sc > 0.1f ? (sc > 1.0f ? 1.0f : sc) : 0.1f;
    if (L->racing && a->sh == 0) a->bc = c->nos || a->sm == 3;
    a->bc100 = a->bc ? 100.0f : 0.0f;
    /* 0x470d50: rpm */
    if (a->sh != 0) {
        if (a->sh >= 4 && a->sh <= 7) a->rpm = 8000;
        else a->rpm = k_D[gi] + a->off + 100.0f;
    } else if (!a->bc) {
        float tgt = mph < 3.0f ? 1000.0f : 4000.0f, rt = mph < 3.0f ? 50.0f : a->slew;   /* 0x468e30 */
        a->rpm = a->rpm < tgt ? (a->rpm + rt > tgt ? tgt : a->rpm + rt) : (a->rpm - rt < tgt ? tgt : a->rpm - rt);
    } else if (a->tr[2].f.avg < 1.0f && mph < 5.0f) {
        float tgt = 9000.0f, rt = a->slew;
        a->rpm = a->rpm < tgt ? (a->rpm + rt > tgt ? tgt : a->rpm + rt) : (a->rpm - rt < tgt ? tgt : a->rpm - rt);
    } else {
        float r = a->rpm + a->slew;
        a->rpm = r < 10000.0f ? r : 10000.0f;
    }
    /* 0x470ee0: throttle */
    if (!a->bc) { float t = a->thr - 50.0f; a->thr = t > 0 ? t : 0; }
    else { float t = a->thr + 50.0f; a->thr = t < 100.0f ? t : 100.0f; }
    a->pgear = a->gear;
    /* 0x45d7e0: the gear the sound plays in (racing only) */
    if (L->racing) {
        if (c->gear == 0) a->gear = 2;
        else {
            float k = g_streetx ? 1.25f : g_drift ? 1.65f : 1.22f;   /* 0x7886dc.. */
            int g = c->vtop > 0 ? ftol(c->speed / c->vtop * k * 8.0f) : 2;
            if (g < 3) g = 2;
            if (g > c->gears + 1) g = c->gears + 1;
            int dg = a->gear - g;
            if (dg > 1 || dg < -1) a->gear = a->pgear = g;
            if (a->rpm > 9300.0f) {
                a->gear = g < 4 ? 3 : g > 7 ? 7 : g;
                a->off = rng_float(700.0f);                          /* 0x7886e8 */
                a->pgear = a->gear - 1;
            } else if (a->gear > 2 && a->rpm < k_D[a->gear > 8 ? 8 : a->gear]) {
                a->gear = g;
                if (a->gear == 2 && mph > 10.0f) a->gear = 3;
                a->pgear = a->gear + 1;
            }
        }
    }
    a->out.thr = a->thr; a->out.rpm = a->rpm;
    a->out.bc = a->bc; a->out.gear = a->gear; a->out.bc100 = a->bc100;
}

/* ---- AIShifting (0x483130 -> 0x46ec70 / 0x46ee60 / 0x482d50) ------------- */

static void sh_wobble_off(AiSnd *a)                     /* 0x45c430 */
{
    a->wob = 0; a->w80 = a->w84 = a->w88 = a->w8c = a->w90 = a->w94 = 0;
    a->sh = 0;
}
static void sh_wobble_step(AiSnd *a, float dt)          /* 0x46f000 */
{
    if (!a->wob) return;
    ramp_step(&a->w1, dt); ramp_step(&a->w2, dt);
    if (a->w1.done) { sh_wobble_off(a); return; }
    a->w90 = ftol(a->w1.val); a->w94 = k_shrec.w1_period;
    a->w80 = ftol(a->w2.val); a->w84 = k_shrec.w2_period;
}
static void sh_begin(AiSnd *a, int state, int ms_thr)
{
    a->w80 = a->w84 = a->w88 = a->w8c = a->w90 = a->w94 = 0; a->wob = 0;
    a->sh = state;
    a->a8 = a->eprev;                     /* the engine's previous rpm; swap */
    float t = a->erpm; a->erpm = a->eprev; a->eprev = t;
    ramp_init(&a->sthr, a->out.thr, 0, ms_thr, 0);
}
static void sh_update(AiSnd *a, float dt)
{
    if (a->gear > a->pgear) {                            /* 0x46ec70: upshift */
        a->w80 = a->w84 = a->w88 = a->w8c = a->w90 = a->w94 = 0; a->wob = 0; a->sh = 0;
        if (!(a->rpm < 3000.0f)) {
            sh_begin(a, 1, 100);
            ramp_init(&a->srpm, a->a8, a->a8 - (float)k_shrec.up_drop, k_shrec.up_ms, 0);
            ramp_init(&a->svol, 0, 0, 0, 0); a->svol.dur = 0.001f;
            ramp_init(&a->strk, a->a8, a->a8 - 1200.0f, k_shrec.up_bump_ms + k_shrec.up_ms, 6);
        }
    } else if (a->gear < a->pgear && a->gear != 1) {     /* 0x46ee60: downshift */
        a->w80 = a->w84 = a->w88 = a->w8c = a->w90 = a->w94 = 0; a->wob = 0;
        sh_begin(a, 4, 50);
        float to = a->a8 - (float)k_shrec.dn_blip;
        to = to > 1000.0f ? (to > 10000.0f ? 10000.0f : to) : 1000.0f;
        ramp_init(&a->srpm, a->a8, to, k_shrec.dn_ms, 0);
        ramp_init(&a->svol, 0, 0, 0, 0); a->svol.dur = 0.001f;
    }
    /* 0x482d50 */
    if (a->sh == 0) return;
    if (!a->strk.done) ramp_track(&a->strk, a->rpm, dt);           /* 0x47bf20 */
    if (a->sh == 2 || a->sh == 3 || a->sh == 7) ramp_track(&a->srpm, a->rpm, dt);
    else ramp_step(&a->srpm, dt);
    if (a->sh == 2 || a->sh == 3 || a->sh == 7) ramp_track(&a->sthr, a->out.thr, dt);   /* 0x47bea0 */
    else ramp_step(&a->sthr, dt);
    ramp_step(&a->svol, dt);
    sh_wobble_step(a, dt);
    switch (a->sh) {
    case 1:
        if (!a->srpm.done) break;
        a->sh = 2;
        {   float from = a->rpm + (float)k_shrec.up_bump;
            from = from > 1000.0f ? (from > 10000.0f ? 10000.0f : from) : 1000.0f;
            ramp_init(&a->srpm, from, a->rpm, k_shrec.up_bump_ms, 0);
        }
        ramp_init(&a->sthr, a->out.thr, a->out.thr, 0, 0);    /* 0x7885a0/0x7885b0: x1.0, 0 ms */
        ramp_init(&a->svol, k_shrec.vol, 0, k_shrec.vol_ms, 0);
        if (a->gear < 5) {                                     /* 0x46f080 */
            float f = a->gear == 3 ? 0.85f : a->gear == 4 ? 0.7f : 1.0f;
            a->wob = 1;
            ramp_init(&a->w1, f * (float)k_shrec.w1_amp, 0, k_shrec.w1_ms, 0);
            ramp_init(&a->w2, (float)k_shrec.w2_amp, 0, k_shrec.w2_ms, 0);
        }
        sh_wobble_step(a, dt);
        break;
    case 2:
        if (a->srpm.done) a->sh = 3;
        break;
    case 3:
        if ((a->w1.done && a->w2.done) || !a->wob) sh_wobble_off(a);
        break;
    case 4:
        if (!a->srpm.done) break;
        a->sh = 5;
        {   int ms = k_shrec.dn_rise_ms;
            if (a->gear < 3) ms = ftol((float)ms * 0.7f);
            float to = a->rpm + (float)k_shrec.dn_rise;
            to = to > 1000.0f ? (to > 10000.0f ? 10000.0f : to) : 1000.0f;
            ramp_init(&a->srpm, a->erpm, to, ms, 6);
            ramp_init(&a->sthr, a->load, 100.0f, 215, 0);      /* 0x7ed29c / 0x7ed2a0 */
        }
        break;
    case 5:
        if (!a->srpm.done) break;
        a->sh = 6;
        {   int ms = k_shrec.dn_fall_ms;
            if (a->gear < 3) ms = ftol((float)ms * 0.7f);      /* (and the shifter's +0x64 <= 1) */
            ramp_init(&a->srpm, a->erpm, a->erpm - (float)k_shrec.dn_fall, ms, 0);
            ramp_init(&a->sthr, 100.0f, 0, 215, 0);
        }
        break;
    case 6:
        if (!a->srpm.done) break;
        a->sh = 7;
        {   float x = fabsf((a->erpm - a->rpm) * k_shrec.dn_k);
            x = x > 0 ? (x > 800.0f ? 800.0f : x) : 0;
            ramp_init(&a->srpm, a->erpm, a->rpm, ftol(x), 6);
            ramp_init(&a->sthr, 0, a->out.thr, 60, 0);
        }
        break;
    case 7:
        if (a->srpm.done) sh_wobble_off(a);
        break;
    }
}

/* ---- AccelTrans (vt 0x789fb8, 0x4831a0) --------------------------------- */

static void at_update(AiSnd *a, const CarSfxCar *c, float dt)
{
    a->at_pbc = a->at_bc;
    a->at_bc = a->out.bc;
    float mph = c->speed * 2.237f;
    if (a->at_bc && !a->at_pbc) {
        int edge = a->out.bc100 - a->pbc100 >= 30.0f && a->at == 0;
        /* 0x45cd30 (and game mode 0x89e7b0 != 8, not ported) */
        if (edge && mph <= 15.0f && !c->nis && a->sh == 0 && a->out.gear == 2 && a->out.rpm <= 1500.0f) {
            a->at_cc = a->time; a->at = 2;                   /* 0x45cc30: launch blip */
            ramp_init(&a->arpm, a->out.rpm, a->out.rpm + (float)a->atr->rpm_up, a->atr->ms, 6);
            ramp_init(&a->a90, 0, a->atr->r0, a->atr->ms, 0);
            ramp_init(&a->athr, a->out.thr, a->out.thr, 0, 0);
        } else if (edge && a->sh == 0) {                     /* 0x470030 */
            int gg = a->out.gear < 0 ? 0 : a->out.gear > 9 ? 9 : a->out.gear;
            if (!(c->up[gg] - 250.0f < a->out.rpm) && !(a->out.rpm < 3000.0f) && !(a->time - a->at_cc < 2.0f)) {
                a->at = 1;                                   /* 0x45cb70 (an AI car passes the gear test) */
                ramp_init(&a->arpm, a->out.rpm + 1000.0f, a->out.rpm, 500, 6);
                ramp_init(&a->a90, 0.8f, 0.8f, 200, 0);      /* 0x788610 */
                ramp_init(&a->athr, 100.0f, 100.0f, 0, 0);
                a->at_cc = a->time;
            }
        }
    }
    /* 0x47c290 */
    ramp_step(&a->a90, dt);
    switch (a->at) {                                         /* 0x47c1a0 */
    case 1: case 3: case 4: ramp_track(&a->arpm, a->out.rpm, dt); break;
    case 2: ramp_step(&a->arpm, dt); break;
    }
    if (a->at >= 1 && a->at <= 4) {                          /* 0x47c220 */
        ramp_track(&a->athr, a->out.thr, dt);
        if (a->athr.done) ramp_init(&a->athr, a->out.thr, a->out.thr, 0, 0);
    }
    if (a->at == 0) return;
    if (!a->out.bc && a->at != 4) {
        ramp_init(&a->arpm, a->erpm, a->out.rpm, a->atr->back_ms, 6);
        a->at = 4;
        ramp_init(&a->a90, a->a90.val, 0, a->atr->back_ms, 0);
    }
    switch (a->at) {
    case 2:
        if (!a->arpm.done) break;
        a->at = 3;
        ramp_init(&a->arpm, a->arpm.val, a->out.rpm, a->atr->settle_ms, 0);
        ramp_init(&a->a90, a->a90.val, 0, a->atr->settle_ms, 0);
        break;
    default:
        if (a->arpm.done) a->at = 0;
        break;
    }
}

/* ---- SFXCTL_Engine (vt 0x789f70, 0x47c010) ------------------------------ */

static void eng_update(AiSnd *a, float dt)
{
    /* vt+0x3c 0x45c7a0: the shift wobbles (each reads its period and its
       amplitude from neighbouring fields, as the original does) */
    if (a->sh != 0) {
        int p1 = a->w94 < 1 ? 1 : a->w94 > 10000 ? 10000 : a->w94;
        int p2 = a->w8c < 1 ? 1 : a->w8c > 10000 ? 10000 : a->w8c;
        int p3 = a->w84 < 1 ? 1 : a->w84 > 10000 ? 10000 : a->w84;
        if (a->w90) {
            a->ph14c = (unsigned short)((unsigned short)(a->ph14c - ftol(dt / ((float)p1 * 0.001f) * -65535.0f)) % 0xffff);
            a->rpmw = sin16(a->ph14c) * (float)p2;
        }
        if (a->w88) {
            a->ph14e = (unsigned short)((unsigned short)(a->ph14e - ftol(dt / ((float)p2 * 0.001f) * -65535.0f)) % 0xffff);
            a->loadw = sin16(a->ph14e) * (float)p3;
        }
        if (a->w80) {
            a->ph150 = (unsigned short)((unsigned short)(a->ph150 - ftol(dt / ((float)p3 * 0.001f) * -65535.0f)) % 0xffff);
            a->volw = sin16(a->ph150) * (float)p1;
        }
    } else {
        a->volw = a->rpmw = a->loadw = 0;
        a->ph14c = a->ph14e = a->ph150 = 0x4097;
    }
    /* vt+0x24 0x46f7d0: rpm */
    float r = a->sh != 0 ? a->srpm.val : a->at ? a->arpm.val : a->out.rpm;
    a->eprev = a->erpm;
    a->erpm = r + a->rpmw + a->rpmw;      /* + the spawn bump +0x1c8 (not ported, 0) */
    /* vt+0x28 0x45c710: load */
    if (a->sh != 0) avg_fill(&a->lf, a->sthr.val);
    else if (a->at) avg_fill(&a->lf, a->athr.val);
    else avg_push(&a->lf, a->out.thr);
    a->load = a->lf.avg + a->loadw;
    /* vt+0x2c 0x45c6c0: volume */
    a->vol = 0x7fff;
    if (a->sh != 0) a->vol = 0x7fff - ftol(a->svol.val * -32767.0f);
    a->vol += ftol(a->volw);
}

/* 0x46f460: the low-pass cutoff by the camera distance */
static int eng_lpf(int dcam)
{
    float x = ((float)dcam - 6.5f) / 22.0f;
    x = x > 0 ? (x > 1 ? 1 : x) : 0;
    int g = carsfx_curve(4, (unsigned)ftol(x * 32766.0f), 1) * 2;
    if (g < -0x2580) g = -10000;
    int v = ftol(725.0f + (24000.0f - 725.0f) * (float)carsfx_mb2lin(g) / 32767.0f);
    return v < 1 ? 0 : v > 0x7fff ? 0x7fff : v;
}

/* ---- traffic engines (CARSFX_TrafficEngine 0x462240, slots 0x463e30) ---- */

typedef struct { int car; Pos3D p3; } TrSlot;
static TrSlot g_trs[CARSFX_TR_MAX];

/* ---- the effects ---------------------------------------------------------- */

/* stitches: InGameCommon.bun chunk 0x8003b500 (loader 0x47bc30) holds
   0x3b502 records {+4 u16 vol, +6 u16 id (volume table index), +8 list,
   +9 sample count}, each followed by a chunk of 16-byte samples {+0 sample,
   +1 event (0 AEMS_StichCollision, 1 AEMS_StichWoosh), +2 u16 vol, +4 u16
   pitch (0x1000 = 1.0, our reading), +6 u16 delay ms, +8 u16 pan offset,
   +10 s16 vol randomness, +12 s16 pitch randomness}. List 0 = collisions
   (289), list 1 = wooshes (60); 0x43be60 picks one by index. */
typedef struct { unsigned char smp, ev; unsigned short vol, pitch, delay, pan; short vrnd, prnd; } StitchSmp;
typedef struct { unsigned short vol, id; int nsmp; const StitchSmp *smp; } StitchDef;
static StitchDef g_stitch[2][320];
static int g_nstitch[2];
static StitchSmp *g_stitch_smp;

static int stitch_load(const char *dataroot)
{
    char path[512];
    snprintf(path, sizeof path, "%s/GLOBAL/InGameCommon.bun", dataroot);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { free(d); fclose(f); return 0; }
    fclose(f);
    free(g_stitch_smp); g_stitch_smp = NULL;
    g_nstitch[0] = g_nstitch[1] = 0;
    int total = 0;
    for (int pass = 0; pass < 2; pass++) {
        int ns = 0;
        for (long o = 0; o + 8 <= len; o += 4) {          /* bundle chunks are 4-aligned */
            if (rd32(d + o) != 0x8003b500u) continue;
            long e = o + 8 + (long)rd32(d + o + 4), p = o + 8;
            if (e > len) break;
            while (p + 8 <= e && rd32(d + p) == 0x3b502u) {
                long q = p + 8 + (long)rd32(d + p + 4);
                if (q + 8 > e) break;
                const unsigned char *df = d + p + 8;
                int lst = df[8], cnt = df[9];
                long q2 = q + 8 + (long)rd32(d + q + 4);
                if (q + 8 + 16L * cnt > e) break;
                if (pass && lst < 2 && g_nstitch[lst] < 320) {
                    StitchDef *s = &g_stitch[lst][g_nstitch[lst]++];
                    s->vol = (unsigned short)(df[4] | df[5] << 8);
                    s->id = (unsigned short)(df[6] | df[7] << 8);
                    s->nsmp = cnt;
                    StitchSmp *m = g_stitch_smp + ns;
                    s->smp = m;
                    for (int k = 0; k < cnt; k++) {
                        const unsigned char *b = d + q + 8 + 16 * k;
                        m[k].smp = b[0]; m[k].ev = b[1];
                        m[k].vol = (unsigned short)(b[2] | b[3] << 8);
                        m[k].pitch = (unsigned short)(b[4] | b[5] << 8);
                        m[k].delay = (unsigned short)(b[6] | b[7] << 8);
                        m[k].pan = (unsigned short)(b[8] | b[9] << 8);
                        m[k].vrnd = (short)(b[10] | b[11] << 8);
                        m[k].prnd = (short)(b[12] | b[13] << 8);
                    }
                }
                ns += cnt;
                p = q2;
            }
            o = e - 4;
        }
        if (!pass) {
            total = ns;
            g_stitch_smp = ns ? (StitchSmp *)calloc((size_t)ns, sizeof *g_stitch_smp) : NULL;
            if (!g_stitch_smp) break;
        }
    }
    free(d);
    printf("car sounds: stitches %d collision + %d woosh (%d samples)\n", g_nstitch[0], g_nstitch[1], total);
    return g_nstitch[0] + g_nstitch[1];
}

/* the bottom-out stitches (0x47ea30: 0x4e.. on landing, 0x56.. on a tilt,
   0x2e.. on surface 6) */
static const int k_bo_first[3] = { 0x4e, 0x56, 0x2e };

/* the voices: AI car k uses g_fxvoice[k * 10 ..], traffic slot i
   g_fxvoice[80 + i * 20 ..] */
#define FXA(k, j) (&g_fxvoice[(k) * 10 + (j)])
#define FXT(i, j) (&g_fxvoice[80 + (i) * 20 + (j)])
enum { FA_WHINE, FA_BOV, FA_NOS, FA_NOS1, FA_SQ0, FA_SQ1, FA_SQ2, FA_RUB, FA_STEREO };
enum { FT_HORN, FT_STEREO, FT_WOOSH, FT_BO = FT_WOOSH + 4, FT_BOEND = FT_BO + 13 };

static void fx_start(AudioFxVoice *v, int set, int clip, int loop, float delay, float pitch)
{
    v->set = set; v->clip = clip; v->loop = loop; v->delay = delay; v->pitch = pitch;
    v->gain = 0; v->done = 0;
    v->on = 1;
    v->trig++;
}
static void fx_stop(AudioFxVoice *v) { v->on = 0; v->gain = 0; }
/* the port's levels for effects the player also has (audio.c: squeal 0.45,
   rubber 0.35, nitrous 0.35 of the player's engine) over the effect
   mixer's 0.6; the mixmaps set the rest (the turbo too: the player's now
   plays through the same mixmap path) */
static float fx_gain(int vol, float ref, float port) { return (float)vol / 32767.0f / ref * port / 0.6f; }

/* a stitch on voices v[0..nv): every sample at its delay, volume and pitch
   (0x46cdd0 -> 0x45b280); returns the voices used */
typedef struct { int n; int vol[13]; } StitchPlay;
static int stitch_start(AudioFxVoice *v, int nv, int lst, int idx, StitchPlay *sp)
{
    sp->n = 0;
    if (lst < 0 || lst > 1 || idx < 0 || idx >= g_nstitch[lst]) return 0;
    const StitchDef *s = &g_stitch[lst][idx];
    int tab = lst ? (s->id < 60 ? k_stitch_vol1[s->id] : 0x7fff) : (s->id < 289 ? k_stitch_vol0[s->id] : 0x7fff);
    for (int k = 0; k < s->nsmp && k < nv; k++) {
        const StitchSmp *m = &s->smp[k];
        int vol = m->vol - (m->vrnd ? (int)rng_int((unsigned)m->vrnd) : 0);
        int pr = m->prnd < 0 ? -m->prnd : m->prnd;
        int pitch = m->pitch - (pr ? (int)rng_int((unsigned)pr) : 0);
        pitch = pitch < 0 ? 0 : pitch > 0x2000 ? 0x2000 : pitch;
        sp->vol[k] = ((tab * s->vol >> 15) * vol) >> 15;
        fx_start(&v[k], m->ev ? AFX_WOOSH : AFX_COLL, m->smp, 0, (float)m->delay * 0.001f,
                 pitch ? (float)pitch / 4096.0f : 1.0f);
        sp->n = k + 1;
    }
    return sp->n;
}
/* a stitch's volumes (0x47bb30 -> 0x46cf70); 1 while a sample still plays */
static int stitch_update(AudioFxVoice *v, const StitchPlay *sp, int objvol, float ref)
{
    int live = 0;
    for (int k = 0; k < sp->n; k++) {
        if (!v[k].on) continue;
        if (v[k].done) { fx_stop(&v[k]); continue; }
        int g = sp->vol[k] * objvol >> 15;
        v[k].gain = fx_gain(g < 0 ? 0 : g > 0x7fff ? 0x7fff : g, ref, 0.6f);
        live = 1;
    }
    return live;
}
static void stitch_stop(AudioFxVoice *v, StitchPlay *sp)
{
    for (int k = 0; k < sp->n; k++) fx_stop(&v[k]);
    sp->n = 0;
}

/* ---- AI racers: turbo, nitrous, skids, stereo ---- */

typedef struct {
    /* CARSFX_Turbo (0x47ca70) */
    int t_on, t_rec, t_set, t_st, t_seq, t_bov, t_var, t_bvol, t_whine;
    float t_in, t_boost, t_frac, t_seqt;
    Ramp t_ramp;            /* +0xc4 */
    /* CARSFX_Nitrous (0x47cbe0) */
    int n_on;
    /* CARSFX_AIStereo (0x47f480) */
    int s_on, s_var, s_play;
    int init;
} AiFx;
static AiFx g_aifx[CARSFX_AI_MAX];
static int g_aitb[CARSFX_AI_MAX], g_naitb;
static unsigned g_stereo_n;                          /* 0x82cb6c */
static float g_fxtime;                               /* owner +0x34 */

/* k_turbo_rec: 0x7f3338 {whine vol +4, blow-off vol (variant 1) +8,
   (variants 2, 3) +0xc}; +0x10 40.0 max boost, +0x14 0.5 decay */
static const int k_turbo_rec[15][3] = {
    { 6500, 24500, 16500 }, { 6500, 24000, 18000 }, { 8500, 22500, 15000 }, { 6500, 22000, 15000 },
    { 7500, 22000, 15000 }, { 6000, 18500, 14500 }, { 6500, 24000, 18000 }, { 9500, 24500, 18500 },
    { 8000, 24500, 16500 }, { 6000, 20000, 16000 }, { 6000, 18000, 16500 }, { 9500, 18000, 16000 },
    { 8500, 16500, 12500 }, { 10500, 24000, 28500 }, { 7250, 18000, 16500 } };

/* the boost (0x472770): rises by the engine load over 20 %, decays 0.5 a
   frame, capped by the revs (0.4..1 of 40 above 1000 rpm); at the cap a
   sequence holds 1 s then fades the whine over 2 s (k_fade 0 -> 307) */
static void turbo_boost(AiFx *x, float rpm, float dt)
{
    x->t_boost = x->t_in > 0.01f ? x->t_boost + x->t_in : x->t_boost - 0.5f;
    if (x->t_boost < 0) x->t_boost = 0;
    if (x->t_boost > 40.0f) x->t_boost = 40.0f;
    float f = (float)(ftol(rpm) - 1000) * 0.01f;
    f = f > 0 ? (f > 1 ? 1 : f) : 0;
    float cap = (0.6f * f + 0.4f) * 40.0f;
    if (x->t_boost > cap) x->t_boost = cap;
    if (x->t_boost == cap && !x->t_seq) { x->t_seq = 1; x->t_seqt = 0; }
    else if (x->t_boost != 40.0f && x->t_seq) x->t_seq = 0;
    x->t_frac = x->t_boost / 40.0f;
    x->t_whine = ftol(x->t_frac * 32767.0f) * k_turbo_rec[x->t_rec][0] >> 15;
    if (x->t_seq) {
        x->t_seqt += dt;
        float s = x->t_seqt < 1.0f ? 0.0f : x->t_seqt < 3.0f ? (x->t_seqt - 1.0f) * 0.5f * 307.2f : 307.2f;
        x->t_whine = fade_at(ftol(s)) * x->t_whine >> 15;
    }
}

/* CARSFX_Turbo (vt+0x24 0x47cad0, voices 0x45ece0) on one car: the whine
   on vw and the blow-off on vb from bank set `set`. load and bc100 are its
   engine controller's load and throttle (0..100), sh the shift state, rpm
   the engine's; the volumes are the mixmap object `out` of class cls
   (slot 0 the whine, 1 blow-off variant 1, 2 variants 2 and 3) at p3. */
static void turbo_update(AiFx *x, AudioFxVoice *vw, AudioFxVoice *vb, int set, float load, float bc100, int sh,
                         float rpm, int cls, unsigned out, const Pos3D *p3, int mode, float dt, float ref)
{
    const int *d = p3->d; const unsigned *az = p3->az;
    x->t_in = load > 20.0f ? load * 0.01f : 0.0f;
    if (x->t_bov) {                                        /* 0x4726e0 */
        ramp_step(&x->t_ramp, dt);
        if (bc100 > 20.0f && x->t_ramp.val == 1.0f && sh == 0) ramp_init(&x->t_ramp, 1, 0, 150, 0);
        if (vb->done || x->t_ramp.val == 0.0f) { x->t_bov = 0; fx_stop(vb); }
    }
    if (!vw->on) fx_start(vw, set, 0, 1, 0, 1);
    else turbo_boost(x, rpm, dt);
    if (x->t_st == 0) { if (load > 20.0f) x->t_st = 1; }
    else if (x->t_st == 1) {
        if (load < 20.0f) {
            x->t_st = 2;
            if (!x->t_bov) {                               /* 0x45ee70 */
                x->t_var = x->t_frac > 0.75f ? (int)rng_int(2) + 2 : 1;
                x->t_bvol = ftol((float)k_turbo_rec[x->t_rec][x->t_var == 1 ? 1 : 2] * x->t_frac);
                fx_start(vb, set, x->t_var, 0, 0, 1);
                x->t_bov = 1;
                ramp_init(&x->t_ramp, 1, 1, 1, 0);
            }
            x->t_boost = 0;
            turbo_boost(x, rpm, dt);
        }
    } else if (x->t_st == 2 && !x->t_bov) x->t_st = 0;
    int w = x->t_whine * mix_objvol(cls, out, 0, mode, d, az) >> 15;
    vw->gain = fx_gain(w < 0 ? 0 : w > 0x7fff ? 0x7fff : w, ref, 0.6f);
    if (x->t_bov) {
        int b = ftol((float)(x->t_bvol * mix_objvol(cls, out, x->t_var == 1 ? 1 : 2, mode, d, az) >> 15) * x->t_ramp.val);
        vb->gain = fx_gain(b < 0 ? 0 : b > 0x7fff ? 0x7fff : b, ref, 0.6f);
    }
}

static void ai_fx_update(int k, AiSnd *a, AiFx *x, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    const int *d = a->p3.d; const unsigned *az = a->p3.az;
    if (x->t_on)
        turbo_update(x, FXA(k, FA_WHINE), FXA(k, FA_BOV), AFX_TURBO + x->t_set, a->load, a->out.bc100, a->sh,
                     a->erpm, 2, 0x40020020u, &a->p3, L->mode, dt, ref);
    /* nitrous (0x484f20 / 0x472af0 / 0x45f2a0, voice 0x45f4b0): FX_NITROUS
       read as engage clip 1, burn loop clip 0, release clip 2 (the
       player's reading) */
    if (c->nos && !x->n_on) {
        x->n_on = 1;
        fx_start(FXA(k, FA_NOS1), AFX_NOS, 0, 0, 0, 1);
        fx_start(FXA(k, FA_NOS), AFX_NOS, 1, 1, 0, 1);
    } else if (!c->nos && x->n_on) {
        x->n_on = 0;
        fx_stop(FXA(k, FA_NOS));
        fx_start(FXA(k, FA_NOS1), AFX_NOS, 2, 0, 0, 1);
    }
    {   int nv = mix_objvol(2, 0x40020030u, 0, L->mode, d, az);
        if (FXA(k, FA_NOS1)->done) fx_stop(FXA(k, FA_NOS1));
        FXA(k, FA_NOS)->gain = FXA(k, FA_NOS1)->gain = fx_gain(nv, ref, 0.35f);
    }
    /* skids (0x460a70 / 0x460c70), one voice for an AI car: the surface
       the wheels agree on, 0x2d3b on surfaces 14, 15, 22, silent with the
       left wheels (FL, RL) off the ground; channels = the controller's
       sides averaged. FX_SKID read as squeal clip 3 - c per channel c plus
       the rubber clip 4 (the player's reading). */
    {   int s0 = c->surface[0], s1 = c->surface[1], s = s0;
        if (s0 != s1) {
            if (c->surface[2] == s0) { if (c->surface[3] != s0) s = s1; }
            else if (c->surface[2] == s1) s = s1;
        }
        int g = (s < 14 || (s > 15 && s != 22)) ? 0x7fff : 0x2d3b;     /* DAT_007ed240 */
        if (!c->contact[0] && !c->contact[3]) g = 0;
        static const int slot[3] = { 3, 5, 4 };
        float sum = 0;
        for (int ch = 0; ch < 3; ch++) {
            AudioFxVoice *v = FXA(k, FA_SQ0 + ch);
            int lvl = ftol(c->skid[ch] * 1024.0f);
            lvl = lvl < 0 ? 0 : lvl > 0x400 ? 0x400 : lvl;
            int vol = mix_objvol(2, 0x40020050u, slot[ch], L->mode, d, az) * g >> 15;
            if (!v->on) fx_start(v, AFX_SKID, 3 - ch, 1, 0, 1);
            v->gain = fx_gain(vol, ref, 0.45f) * (float)lvl / 1024.0f;
            sum += (float)lvl / 1024.0f;
        }
        AudioFxVoice *r = FXA(k, FA_RUB);
        if (!r->on) fx_start(r, AFX_SKID, 4, 1, 0, 1);
        r->gain = fx_gain(mix_objvol(2, 0x40020050u, 3, L->mode, d, az) * g >> 15, ref, 0.35f) * (sum > 1 ? 1 : sum);
    }
    /* stereo (0x4635b0 / 0x4636e0): ENV_STATIC variant 8/9 = ENV_COMMON
       clip 6/7 (the patch's variant table 0x90c: bank entry 7/8), looped,
       while the mixmap gives it any volume */
    if (x->s_on) {
        int sv = mix_objvol(2, 0x400200f0u, 0, L->mode, d, az);
        AudioFxVoice *v = FXA(k, FA_STEREO);
        if (!x->s_play && sv) { x->s_play = 1; fx_start(v, AFX_ENV, x->s_var - 2, 1, 0, 1); }
        else if (x->s_play && !sv) { x->s_play = 0; fx_stop(v); }
        if (x->s_play) v->gain = fx_gain(sv, ref, 0.6f);
    }
}

/* ---- traffic: woosh, horn, stereo, bottom-out ---- */

typedef struct {
    /* CARSFX_TrafficWoosh: +0xb4 voice, +0xb8 last start */
    int w_on; float w_t; StitchPlay w_sp;
    /* CARSFX_TrafficHorn (0x475820): +0xb4 voice, +0xb8 length, +0xbc
       start, +0xc0 end, +0xc5 timed, +0xc8 last refusal, ramp +0xcc */
    int h_on, h_timed, h_fade; float h_len, h_t0, h_end, h_no;
    Ramp h_ramp;
    /* CARSFX_TrafficStereo */
    int s_on, s_var, s_play;
    /* CARSFX_TrafficBottomOut: pair (FL FR), (RR RL), (FR RR), (RL FL)
       down flags +0xbc.. and air timers +0xc0.., tilt latch +0xdc */
    int b_on, b_down[4], b_tilt; float b_air[4]; StitchPlay b_sp;
} TrFx;
static TrFx g_trfx[CARSFX_TR_MAX];
static unsigned g_woosh_n, g_bo_n[2];               /* 0x82cb68, 0x82cbf4 / 0x82cbf8 */
static float g_horn_last;                           /* 0x82bc50 */

/* a slot takes a new car: the objects attach (stereo 0x4765f0 / 0x476580:
   one in three, never the taxis 0x1f / 0x22) */
static void tr_fx_attach(TrFx *x, int cartype)
{
    memset(x, 0, sizeof *x);
    x->s_var = 8 + (int)rng_int(2);
    if (cartype != 0x1f && cartype != 0x22) {
        x->s_on = rng_int(3) == 0;
        x->s_var = 8 + (int)rng_int(2);
    }
    x->h_end = -100.0f; x->h_no = -100.0f;
}
static void tr_fx_detach(int i, TrFx *x)
{
    for (int j = 0; j < 20; j++) fx_stop(FXT(i, j));
    memset(x, 0, sizeof *x);
}

static void tr_fx_update(int i, TrFx *x, const Pos3D *p3, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    const int *d = p3->d; const unsigned *az = p3->az;
    float now = g_fxtime;
    float dx = L->car[0] - c->pos[0], dy = L->car[1] - c->pos[1], dist = sqrtf(dx * dx + dy * dy);
    float rvx = L->car_vel[0] - c->vel[0], rvy = L->car_vel[1] - c->vel[1];
    float rel = sqrtf(rvx * rvx + rvy * rvy);                   /* 0x462670 */
    /* woosh (0x475e10): the player within 5 m passing over 15 m/s, at
       most every 3 s; stitch 9 + (0..5 in turn) + 6 x speed band */
    if (!x->w_on && dist < 5.0f && rel > 15.0f && now >= x->w_t + 3.0f) {
        int n = (int)(g_woosh_n % 6);
        float u = (rel - 15.0f) / (60.0f - 15.0f);
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        u = u > 0.99f ? 0.99f : u;
        float u127 = u * 127.0f;
        u127 = u127 > 127.0f ? 127.0f : u127;
        int band = ftol(u127 * -0.0234375f);
        g_woosh_n = (unsigned)n + 1;
        x->w_on = stitch_start(FXT(i, FT_WOOSH), 4, 1, n - band * 6 + 9, &x->w_sp) > 0;
        x->w_t = now;
    }
    if (x->w_on) {                                              /* 0x47e8d0 */
        int live = stitch_update(FXT(i, FT_WOOSH), &x->w_sp, mix_objvol(3, 0x40030010u, 2, L->mode, d, az), ref);
        if (rel <= 15.0f || !live) { stitch_stop(FXT(i, FT_WOOSH), &x->w_sp); x->w_on = 0; }
    }
    /* horn (0x47e6e0): the player within 20 m, ahead of the traffic car
       (40 deg of its travel) and not driving its way (20 deg; drag: 115
       deg, no heading test), the player over 15 m/s forward, 3 s since
       its last horn and refusal; then 30 % (or 5 s since any horn) honks
       ENV_STATIC variant 5..7 = ENV_COMMON clips 3..5 for 1.2..4 s */
    if (x->h_on) ramp_step(&x->h_ramp, dt);
    if (!x->h_on) {
        if (now > x->h_end + 3.0f && now > x->h_no + 3.0f && L->car_fspeed > 15.0f && dist <= 20.0f) {
            float vl = sqrtf(c->vel[0] * c->vel[0] + c->vel[1] * c->vel[1]);
            float tx = vl > 0 ? c->vel[0] / vl : 1, ty = vl > 0 ? c->vel[1] / vl : 0;
            float px = dist > 0 ? dx / dist : 1, py = dist > 0 ? dy / dist : 0;
            float fl = sqrtf(L->car_fwd[0] * L->car_fwd[0] + L->car_fwd[1] * L->car_fwd[1]);
            float fx = fl > 0 ? L->car_fwd[0] / fl : 1, fy = fl > 0 ? L->car_fwd[1] / fl : 0;
            int ok = px * tx + py * ty >= sinf(L->drag ? 3.578f : 2.269f);   /* 0x43c350 */
            if (ok && !L->drag && fx * tx + fy * ty > sinf(1.919f)) ok = 0;
            if (ok) {
                if (!(rng_float(1.0f) < 0.3f) && !(g_horn_last + 5.0f < now)) x->h_no = now;
                else {
                    int var = (int)rng_int(3) + 5;
                    x->h_timed = 1;
                    x->h_len = rng_float(4.0f - 1.2f) + 1.2f;
                    fx_start(FXT(i, FT_HORN), AFX_ENV, var - 2, 1, 0, 1);     /* 0x4759c0 */
                    x->h_on = 1; x->h_fade = 0;
                    g_horn_last = now; x->h_t0 = now;
                    ramp_init(&x->h_ramp, 1, 1, 1, 0);
                }
            }
        }
    } else {
        if (!x->h_fade && (!(now < x->h_t0 + x->h_len) || !x->h_timed)) {
            x->h_fade = 1;                                      /* 0x462480 */
            ramp_init(&x->h_ramp, 1, 0, 150, 0);
        }
        if (x->h_fade && x->h_ramp.done) {                      /* 0x4624e0 */
            fx_stop(FXT(i, FT_HORN));
            x->h_on = 0; x->h_timed = 0; x->h_end = now;
        }
    }
    if (x->h_on) {                                              /* 0x462550 */
        AudioFxVoice *v = FXT(i, FT_HORN);
        int hp = ftol(pos3d_pitch(p3, 0) * 4096.0f);
        hp = hp < 0 ? 0 : hp > 0x1fa0 ? 0x1fa0 : hp;
        int hv = ftol((float)mix_objvol(3, 0x40030020u, 1, L->mode, d, az) * x->h_ramp.val);
        v->gain = fx_gain(hv < 0 ? 0 : hv > 0x7fff ? 0x7fff : hv, ref, 0.6f);
        v->pitch = (float)hp / 4096.0f;
    }
    /* stereo, as the AI's */
    if (x->s_on) {
        int sv = mix_objvol(3, 0x40030030u, 0, L->mode, d, az);
        AudioFxVoice *v = FXT(i, FT_STEREO);
        if (!x->s_play && sv) { x->s_play = 1; fx_start(v, AFX_ENV, x->s_var - 2, 1, 0, 1); }
        else if (x->s_play && !sv) { x->s_play = 0; fx_stop(v); }
        if (x->s_play) v->gain = fx_gain(sv, ref, 0.6f);
    }
    /* bottom-out (0x47ed80): an axle pair touching down (front FL+FR, rear
       RR+RL) after 0.05 s off the ground -> stitch 0x4e.. in turn; once the
       car has tipped (up axis z < 0.9 with a wheel up), any pair touching
       down -> 0x56.. in turn. The air timers start from the clock (the
       original's), so any new touchdown counts. */
    {   const int *w = c->contact;
        int p01 = w[0] && w[1], p23 = w[2] && w[3], p12 = w[1] && w[2], p30 = w[3] && w[0];
        int pair[4] = { p01, p23, p12, p30 };
        int down = w[0] + w[1] + w[2] + w[3];
        if (!x->b_tilt && c->upz < 0.9f && down < 4) x->b_tilt = 1;
        int land = (p01 && x->b_air[0] > 0.05f && !x->b_down[0]) || (p23 && x->b_air[1] > 0.05f && !x->b_down[1]);
        int mode = -1;
        if (!x->b_tilt) { if (land) mode = 0; }
        else if ((pair[0] && !x->b_down[0]) || (pair[1] && !x->b_down[1]) ||
                 (pair[2] && !x->b_down[2]) || (pair[3] && !x->b_down[3])) mode = 1;
        else if (land) mode = 0;
        if (mode >= 0) {                                        /* 0x47ea30 */
            stitch_stop(FXT(i, FT_BO), &x->b_sp);
            unsigned n = g_bo_n[mode] % 4;
            g_bo_n[mode] = n + 1;
            x->b_on = stitch_start(FXT(i, FT_BO), FT_BOEND - FT_BO, 0, k_bo_first[mode] + (int)n, &x->b_sp) > 0;
        }
        if (down == 4 && x->b_tilt) x->b_tilt = 0;
        for (int p = 0; p < 4; p++) {
            if (!pair[p]) {
                if (x->b_down[p]) { x->b_down[p] = 0; x->b_air[p] = now; }
                x->b_air[p] += dt;
            } else x->b_down[p] = 1;
        }
    }
    if (x->b_on) {                                              /* 0x47f100 */
        x->b_on = stitch_update(FXT(i, FT_BO), &x->b_sp, mix_objvol(3, 0x40030040u, 0, L->mode, d, az), ref);
        if (!x->b_on) stitch_stop(FXT(i, FT_BO), &x->b_sp);
    }
}

/* the stitches and the effect banks (with the collision clips the
   bottom-out stitches and the collision hits, 6..0x5d, play) */
static void fx_load(const char *dataroot)
{
    stitch_load(dataroot);
    int coll[256], nc = 0;
    unsigned char seen[256] = { 0 };
    for (int id = 0; id < g_nstitch[0]; id++) {
        int bo = 0;
        for (int m = 0; m < 3; m++) bo |= id >= k_bo_first[m] && id < k_bo_first[m] + 4;
        if (!bo && (id < 6 || id >= 0x5e)) continue;
        const StitchDef *sd = &g_stitch[0][id];
        for (int k = 0; k < sd->nsmp; k++)
            if (!sd->smp[k].ev && !seen[sd->smp[k].smp]) { seen[sd->smp[k].smp] = 1; coll[nc++] = sd->smp[k].smp; }
    }
    audio_load_carfx_banks(dataroot, coll, nc);
    co_reset();
}

int carsfx_ai_turbo_banks(int tbank[CARSFX_AI_MAX])
{
    for (int i = 0; i < g_naitb; i++) tbank[i] = g_aitb[i];
    return g_naitb;
}


/* ---- collisions (class 5: CSTATE_Collision 0x47f7b0 / SFXObj_Collision
   0x47e220) ------------------------------------------------------------- */

/* Every resolved contact (0x5a9620) feeds the pair's contact effect (pair
   +0x244, made at the pair's first resolution 0x5a9b10, gone with the pair
   0x5ae470): +0x20 first point, +0x30 latest point, +0x40 slide, +0x60 sum
   of |impulse|, +0x70 the impulse's direction, +0xc its time. The factory
   0x481cd0 gives a new effect one record (vtables 0x78bee0 car vs world,
   0x78be90 car vs car; 0x78bb08 smackables, which the port has none of).
   Within 100 m of the camera (0x5f1160) each contact inits the record
   (0x46a1f0: type, strength), queues its hit once (0x4797b0, filter
   vt+0x14) and asks for its scrape (0x469de0). Per frame the queued hits
   start (0x481850, at most 5 at once, 0x7ecbc8), then each record updates
   (0x481f10): scrape start (0x46a460) / update (0x479970), the end 0.3 s
   after the last contact or with the pair (0x46a1a0), cleanup (0x46a3a0). */
#define CO_REC 16           /* records (the factory's queue limit is 10) */
#define CO_EFF 48           /* contact effects (pairs that resolved) */
#define CO_HITS 5           /* 0x7ecbc8 */
#define CO_SMP 12           /* samples of the largest collision stitch */
#define FXC(h, j) (&g_fxvoice[200 + (h) * CO_SMP + (j)])
#define FXS(r) (&g_fxvoice[200 + CO_HITS * CO_SMP + (r)])

typedef struct {
    int on, src, a, b, poly, rec;
    float p0[3], pt[3], nrm[3], slide, isum, t;     /* +0x20 +0x30 +0x70 +0x40 +0x60 +0xc */
    int seen;
} CoEff;
typedef struct {
    int on, eff;                /* +0x24 the contact effect, -1 = gone */
    int kind;                   /* 0x459680: 0 car vs world, 1 car vs car */
    CarSfxCollCar car[2];       /* the pair's cars (b: car vs car only) */
    int init, nohit, sreq;      /* +0x18 +0x20 +0x22 */
    int type, flags, sel;       /* +0x124, +0x128, +0x118 (0 = car[0], 1 = car[1]) */
    int str, sstr;              /* +0x50 hit strength, +0x54 scrape 0..127 */
    float p[3], n0[3], dist;    /* +0x30, +0x40, +0xa8 */
    int pend, hit, hitdone, scr, ended, fading;     /* +0x132 +0x133 +0x134 +0x135 +0x9c */
    Ramp fade;                  /* +0x58 */
    int hb;                     /* the hit's voice block */
    StitchPlay sp;
    Pos3D p3;
} CoRec;
static CoEff g_coe[CO_EFF];
static CoRec g_cor[CO_REC];
static int g_co_hbused[CO_HITS];
static int g_co_pq[CO_REC], g_co_npq;               /* the queue (owner +4) */
static unsigned g_co_rr[6];                          /* 0x82cbd8 .. 0x82cbec */
static float g_co_lastn[3], g_co_lastp[3];           /* 0x82bcb0, 0x82bcc0 */
static float g_co_cam[3];                            /* the camera (0x5f1160) */
static int g_co_live;                                /* sounds allowed (not paused) */

/* the hit strength curves (0x469e50: hits 0..1 over 0..3, x 0x7884f4 per
   type) and the scrape's (0..1 over 0..40 m/s) */
static const float k_co_fac[9] = { 18.0f, 2.5f, 15.0f, 1.0f, 20.0f, 8.0f, 1.0f, 2.5f, 2.5f };
static int co_curve(float x, float hi)                /* 0x4599e0 x 127, truncated */
{
    if (fabsf(hi) < 1e-6f) hi += 1e-6f;
    float t = x / hi;
    if (!(t > 0)) return 0;
    if (t > 1) t = 1;
    return ftol(t * 127.0f);
}
static float co_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void co_norm(float *o, const float *v)        /* 0x43cf10 */
{
    float l = sqrtf(co_dot(v, v));
    if (l != 0) { o[0] = v[0] / l; o[1] = v[1] / l; o[2] = v[2] / l; }
    else { o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; }
}
static float co_dist(const float *a, const float *b)
{
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return sqrtf(co_dot(d, d));
}

/* 0x46a770: car vs car -- 0 unless the cars are parallel (|fwd.fwd| > 0.707);
   parallel and the point ahead of / behind both (|dir.fwd| > 0.8): 0, else 1 */
static int co_type_cc(const CoRec *r)
{
    const CarSfxCollCar *A = &r->car[0], *B = &r->car[1];
    float fa[3] = { A->fwd[0], A->fwd[1], 0 }, fb[3] = { B->fwd[0], B->fwd[1], 0 }, na[3], nb[3];
    co_norm(na, fa); co_norm(nb, fb);
    if (!(fabsf(co_dot(na, nb)) > 0.707f)) return 0;
    float da[3], db[3], ra[3], rb[3];
    co_norm(da, A->fwd); co_norm(db, B->fwd);
    for (int i = 0; i < 3; i++) { ra[i] = r->p[i] - A->pos[i]; rb[i] = r->p[i] - B->pos[i]; }
    co_norm(ra, ra); co_norm(rb, rb);
    if (!(fabsf(co_dot(ra, da)) > 0.8f)) return 1;
    if (!(fabsf(co_dot(rb, db)) > 0.8f)) return 1;
    return 0;
}
/* 0x46aa20: car vs world by the contact normal n -- mostly vertical: the
   car's up.z < 0.1 -> 2 (on its roof / side), > 0.707 -> 3 (wheels first),
   else 4; mostly horizontal: along the car (|fwd.n| > 0.707) 4, else 5 */
static int co_type_cw(const CoRec *r, const float *n)
{
    const CarSfxCollCar *c = &r->car[0];
    if (fabsf(n[2]) > 0.707f) {
        if (-c->up[2] > -0.1f) return 2;
        return c->up[2] > 0.707f ? 3 : 4;
    }
    return fabsf(co_dot(c->fwd, r->n0)) > 0.707f ? 4 : 5;
}

/* 0x46a1f0 (+ the subclass init 0x46a9e0 / 0x46ab90) and the strength
   (0x479a20, car vs world 0x47a700) */
static void co_init(CoRec *r, const CoEff *e)
{
    memcpy(r->p, e->p0, sizeof r->p);
    memcpy(r->n0, e->nrm, sizeof r->n0);
    r->flags = 0x10 | (r->car[0].human || (r->kind == 1 && r->car[1].human));
    r->type = r->kind == 1 ? co_type_cc(r) : co_type_cw(r, e->nrm);
    r->dist = co_dist(r->p, g_co_cam);
    r->hit = r->scr = 0;
    /* +0x118: the human player's car if one is in it, else the faster */
    r->sel = 0;
    if (r->kind == 1) {
        float sa = r->car[0].speed * 2.237f, sb = r->car[1].speed * 2.237f;
        if (r->flags & 1) {
            if (r->car[0].human && r->car[1].human) r->sel = sb >= sa ? 0 : 1;
            else r->sel = r->car[0].human ? 0 : 1;
        } else r->sel = sb < sa ? 0 : 1;
    }
    r->init = 1;
    r->str = co_curve(e->isum, 3.0f * k_co_fac[r->type]);
    if (r->kind == 0) {                                   /* 0x47a700 */
        const CarSfxCollCar *c = &r->car[0];
        if (c->up[2] < 0.9f && (float)c->wheels * 0.25f < 1.0f) {
            r->str += 50;
            if (r->str > 127) r->str = 127;
        }
    }
}

/* vt+0x14: car vs car 0x459880 (once); car vs world 0x46aba0 -- not a
   repeat of the last hit's ground thump (same normal > 0.95, within 1 m,
   |n.z| > 0.707), and no side hit (type 5) of the same car on a normal
   within 0.8 already */
static int co_filter(CoRec *r, const CoEff *e)
{
    if (r->nohit) return 0;
    if (r->kind != 0) return 1;
    float ln[3], n[3];
    co_norm(ln, g_co_lastn); co_norm(n, e->nrm);
    if (co_dot(ln, n) > 0.95f) {
        float l1 = sqrtf(co_dot(ln, ln)), n95[3] = { e->nrm[0] * 0.95f, e->nrm[1] * 0.95f, e->nrm[2] * 0.95f };
        float n105[3] = { e->nrm[0] * 1.05f, e->nrm[1] * 1.05f, e->nrm[2] * 1.05f };
        if (l1 > sqrtf(co_dot(n95, n95)) && sqrtf(co_dot(n105, n105)) > sqrtf(co_dot(g_co_lastn, g_co_lastn)) &&
            co_dist(e->pt, g_co_lastp) < 1.0f && fabsf(e->nrm[2]) > 0.707f)
            return 0;
    }
    const CarSfxCollCar *me = &r->car[r->sel];
    for (int i = 0; i < CO_REC; i++) {
        const CoRec *o = &g_cor[i];
        if (!o->on || !o->init || o->type != 5 || o->kind != 0) continue;
        const CarSfxCollCar *oc = &o->car[o->sel];
        if (oc->id != me->id) continue;
        if (o == r) { if (r->hitdone) return 0; continue; }
        if (co_dot(o->n0, e->nrm) > 0.8f) return 0;
    }
    return 1;
}

/* vt+0x4c: the stitch -- car vs car 0x47a610, car vs world 0x47a770: base
   + round robin + 4 x trunc(4 x curve[strength x 0.15625]) */
static int co_stitch(CoRec *r)
{
    int s = r->str;
    int i = ftol((float)s * 0.15625f);
    i = i > 0 ? (i > 19 ? 19 : i) : 0;
    int m = -ftol(k_coll_curve[i] * -4.0f), rr;
    if (r->kind == 1) {
        if (r->type == 0) { rr = (int)(g_co_rr[0]++ % 4); return 6 + rr + 4 * m; }
        if (r->type == 1) { rr = (int)(g_co_rr[1]++ % 4); return 0x16 + rr + 4 * m; }
        return 0;
    }
    /* the human player's car in its jump-camera state (player +0x58 == 6)
       adds 60 first: the port has no jump camera */
    switch (r->type) {
    case 2: case 6: rr = (int)(g_co_rr[4]++ % 2); return rr + 2 * (0x23 + m);
    case 3: rr = (int)(g_co_rr[5]++ % 4); return 0x4e + rr + 4 * m;
    case 4: rr = (int)(g_co_rr[2]++ % 4); return 0x26 + rr + 4 * m;
    case 5: rr = (int)(g_co_rr[3]++ % 4); return 0x36 + rr + 4 * m;
    default: return 0x26;
    }
}

static void co_hit_stop(CoRec *r)                     /* vt+0x48 */
{
    if (r->hb >= 0) { stitch_stop(FXC(r->hb, 0), &r->sp); g_co_hbused[r->hb] = 0; }
    r->hb = -1;
    r->hit = 0;
}
static void co_end(CoRec *r);
static void co_hit_start(CoRec *r)                    /* 0x46a540 */
{
    const CoEff *e = r->eff >= 0 ? &g_coe[r->eff] : NULL;
    if (!e) { co_end(r); return; }
    memcpy(g_co_lastn, e->nrm, sizeof g_co_lastn);
    memcpy(g_co_lastp, e->pt, sizeof g_co_lastp);
    memcpy(r->p, e->pt, sizeof r->p);
    int id = co_stitch(r);
#ifdef CARSFX_TRACE
    printf("coll: rec %d kind %d type %d strength %d dist %.1f flags %x -> stitch %d\n",
           (int)(r - g_cor), r->kind, r->type, r->str, r->dist, r->flags, id);
#endif
    r->hit = r->hitdone = 1;
    r->hb = -1;
    for (int h = 0; h < CO_HITS; h++) if (!g_co_hbused[h]) { r->hb = h; break; }
    if (r->hb < 0) return;
    g_co_hbused[r->hb] = 1;
    stitch_start(FXC(r->hb, 0), CO_SMP, 0, id, &r->sp);
}
static void co_end(CoRec *r)                          /* 0x46a1a0 */
{
    if (r->ended) return;
    r->ended = 1;
    if (r->eff >= 0 && g_coe[r->eff].rec == (int)(r - g_cor)) g_coe[r->eff].rec = -1;
    r->eff = -1;
    if (r->scr) { r->fading = 1; ramp_init(&r->fade, 1.0f, 0.0f, 100, 0); }
}
static void co_free(CoRec *r)
{
    co_hit_stop(r);
    fx_stop(FXS(r - g_cor));
    if (r->eff >= 0 && g_coe[r->eff].rec == (int)(r - g_cor)) g_coe[r->eff].rec = -1;
    memset(r, 0, sizeof *r);
    r->hb = -1; r->eff = -1;
}

static void co_reset(void)
{
    for (int i = 0; i < CO_REC; i++) co_free(&g_cor[i]);
    memset(g_coe, 0, sizeof g_coe);
    memset(g_co_hbused, 0, sizeof g_co_hbused);
    g_co_npq = 0;
}

static int co_key(const CoEff *e, int src, int a, int b, int poly)
{
    return e->on && e->src == src && e->a == a && e->b == b && e->poly == poly;
}

void carsfx_coll_hit(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                     float slide, const CarSfxCollCar *ca, const CarSfxCollCar *cb)
{
    if (!g_co_live || !ca) return;
    if (b >= 0 && !cb) return;
    int ei = -1;
    for (int i = 0; i < CO_EFF && ei < 0; i++) if (co_key(&g_coe[i], src, a, b, poly)) ei = i;
    if (ei < 0) {                                         /* 0x5a9b10 -> 0x5a9430 */
        for (int i = 0; i < CO_EFF && ei < 0; i++) if (!g_coe[i].on) ei = i;
        if (ei < 0) return;
        CoEff *e = &g_coe[ei];
        memset(e, 0, sizeof *e);
        e->on = 1; e->src = src; e->a = a; e->b = b; e->poly = poly; e->rec = -1; e->seen = 1;
        memcpy(e->p0, point, sizeof e->p0);
        if (g_co_npq < 10) {                              /* the factory 0x481cd0 */
            for (int r = 0; r < CO_REC; r++) {
                if (g_cor[r].on) continue;
                CoRec *R = &g_cor[r];
                memset(R, 0, sizeof *R);
                R->on = 1; R->eff = ei; R->hb = -1;
                R->kind = b >= 0;                         /* 0x459680 */
                e->rec = r;
                break;
            }
        }
    }
    CoEff *e = &g_coe[ei];                                /* 0x5a9620 */
    e->isum += sqrtf(co_dot(impulse, impulse));
    e->t = g_fxtime;
    memcpy(e->pt, point, sizeof e->pt);
    e->slide = slide;
    co_norm(e->nrm, impulse);
    if (co_dist(e->p0, g_co_cam) >= 100.0f) return;      /* 0x7a1828 */
    if (e->rec < 0) return;
    CoRec *r = &g_cor[e->rec];
    if (r->ended) return;
    r->car[0] = *ca;
    if (cb) r->car[1] = *cb;
    /* 0x4797b0 */
    if (!r->init) co_init(r, e);
    if (!r->hit && !r->hitdone && !r->pend) {
        if (co_filter(r, e)) {                            /* 0x479770 */
            if (g_co_npq < CO_REC) { g_co_pq[g_co_npq++] = e->rec; r->pend = 1; }
        } else r->nohit = 1;                              /* vt+0x1c */
    }
    /* 0x469de0: the scrape when the slide reaches the curve's start (0) */
    if (!r->scr && slide >= 0.0f) r->sreq = 1;
}

void carsfx_coll_pairs(int src, const int (*pairs)[3], int n)
{
    for (int i = 0; i < CO_EFF; i++) {
        CoEff *e = &g_coe[i];
        if (!e->on || e->src != src) continue;
        int alive = 0;
        for (int k = 0; k < n && !alive; k++)
            alive = pairs[k][0] == e->a && pairs[k][1] == e->b && pairs[k][2] == e->poly;
        if (alive) continue;
        if (e->rec >= 0) g_cor[e->rec].eff = -1;          /* 0x5a9570 -> the record ends */
        e->on = 0;
    }
}

/* 0x481850: the queued hits start -- all when they fit in 5, else the
   human player's first, then the nearest, taking the places of the
   records at the far end of the same order */
static int co_order(int *out, const int *in, int n)
{
    int m = 0, used[CO_REC] = { 0 };
    for (int i = 0; i < n; i++) if (g_cor[in[i]].flags & 1) { out[m++] = in[i]; used[i] = 1; }
    for (;;) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (!used[i] && (best < 0 || g_cor[in[best]].dist > g_cor[in[i]].dist)) best = i;
        if (best < 0) break;
        used[best] = 1; out[m++] = in[best];
    }
    return m;
}
static void co_start_queued(void)
{
    int playing = 0;
    for (int i = 0; i < CO_REC; i++) playing += g_cor[i].on && g_cor[i].hit;
    if (g_co_npq + playing <= CO_HITS) {
        for (int i = 0; i < g_co_npq; i++) co_hit_start(&g_cor[g_co_pq[i]]);
    } else {
        int A[CO_REC], B[CO_REC], all[CO_REC], na, nb, nall = 0;
        na = co_order(A, g_co_pq, g_co_npq);
        for (int i = 0; i < CO_REC; i++) if (g_cor[i].on) all[nall++] = i;
        nb = co_order(B, all, nall);
        int ia = 0, ib = nb - 1, stole = 0, go = 1;
        while (ia < na && ib >= 0) {
            CoRec *a = &g_cor[A[ia]], *b = &g_cor[B[ib]];
            int steal = 0;
            if (a->flags & 1) { if (!(b->flags & 1)) steal = 1; }
            else if (b->flags & 1) go = 0;
            else if (a->dist < b->dist) steal = 1;
            if (steal) { co_hit_stop(b); co_hit_start(a); stole = 1; }
            if (!stole || ib == 0) break;
            ib--; ia++;
            if (!go) break;
        }
    }
    for (int i = 0; i < g_co_npq; i++) g_cor[g_co_pq[i]].pend = 0;
    g_co_npq = 0;
}

static void co_update(float dt, const CarSfxListener *L, float ref)
{
    co_start_queued();
    for (int i = 0; i < CO_REC; i++) {
        CoRec *r = &g_cor[i];
        if (!r->on) continue;
        if (r->init) {                                    /* 0x481f10 */
            const CoEff *e = r->eff >= 0 ? &g_coe[r->eff] : NULL;
            if (r->sreq) {                                /* 0x46a460 */
                r->sreq = 0;
                if (e) {                                  /* at the record's +0x30 */
                    r->scr = 1; r->fading = 0;
                    fx_start(FXS(i), AFX_SCRAPE, 0, 1, 0, 1);
                } else co_end(r);
            }
            if (!r->ended && (!e || (g_fxtime - e->t) > 0.3f)) co_end(r);   /* 0x4595d0 */
            e = r->eff >= 0 ? &g_coe[r->eff] : NULL;
            pos3d_update(&r->p3, r->p, L, dt);
            /* 0x461ce0: the hit's volume = the type's slot, the scrape's
               slot 9 (pitch 0x1000: 0x45e3d0 changes it only in a pause) */
            if (r->hit && r->hb >= 0)
                stitch_update(FXC(r->hb, 0), &r->sp, mix_objvol(5, 0x40050000u, r->type, L->mode, r->p3.d, r->p3.az), ref);
            if (r->scr) {                                 /* 0x479970 */
                ramp_step(&r->fade, dt);
                if (!r->ended && e) r->sstr = co_curve(e->slide, 40.0f);   /* 0x479ad0 */
                int vol = mix_objvol(5, 0x40050000u, 9, L->mode, r->p3.d, r->p3.az);
                if (r->fading) vol = ftol(r->fade.val * (float)vol);
                vol = vol < 0 ? 0 : vol > 0x7fff ? 0x7fff : vol;
                /* the FX_Scrape patch maps the parameter through its two
                   tables */
                AudioFxVoice *v = FXS(i);
                v->gain = fx_gain(vol * k_scrape_vol[r->sstr] >> 15, ref, 0.6f);
                v->pitch = (float)k_scrape_pitch[r->sstr] / 4096.0f;
            }
            if (r->ended) {                               /* 0x46a3a0 */
                int live = 0;
                if (r->hb >= 0) for (int k = 0; k < r->sp.n; k++) live |= FXC(r->hb, k)->on && !FXC(r->hb, k)->done;
                if (!live && r->hit) co_hit_stop(r);
                if (r->fading && r->fade.done && r->scr) { fx_stop(FXS(i)); r->scr = 0; }
            }
        } else if (r->eff < 0) r->ended = 1;              /* never in range */
        if (!r->hit && !r->scr && r->ended) co_free(r);
    }
}

/* ---- the field ----------------------------------------------------------- */

static int engine_class(int row)                        /* 0x458f00 */
{
    switch (row) {
    case 1: case 2: case 8: case 11: case 15: case 16: case 17: case 27: case 29: case 30: case 31: case 32:
        return 1;
    case 9: case 13: case 14: case 20: case 21:
        return 2;
    default:
        return 0;
    }
}
static const int k_cls0[] = { 0, 1, 2, 3, 4, 5, 10, 11, 18, 20, 21, 23, 24, 34, -1 };   /* 0x7ecaa8 */
static const int k_cls1[] = { 1, 2, 6, 15, 16, 18, 20, 21, 24, 25, 28, -1 };            /* 0x7ecae4 */
static const int k_cls2[] = { 12, 13, 9, 15, -1 };                                      /* 0x7ecb14 */
static int slot_ok(int cls, int slot)                   /* 0x458e70 */
{
    const int *l = cls == 0 ? k_cls0 : cls == 1 ? k_cls1 : cls == 2 ? k_cls2 : NULL;
    if (!l || slot < 0) return 0;
    for (; *l >= 0; l++) if (*l == slot) return 1;
    return 0;
}
static int slot_random(int cls)
{
    if (cls == 0) return k_cls0[rng_int(14)];
    if (cls == 1) return k_cls1[rng_int(11)];
    if (cls == 2) return k_cls2[rng_int(4)];
    return 0;
}
static int in_picks(const int *pick, int slot)
{
    for (int i = 0; i < 3; i++) if (pick[i] == slot) return 1;
    return 0;
}
static int slot_fresh(const int *pick, int cls)
{
    int s = slot_random(cls);
    for (int k = 0; k < 100 && in_picks(pick, s); k++) s = slot_random(cls);
    return s;
}

/* the row's engine slot at performance level 0 (0x7f2230 record) */
static const unsigned char k_row_engine[33] = {
    18, 6, 25, 29, 22, 22, 18, 31, 6, 39, 31, 25, 29, 14, 38, 15, 4, 26, 22, 23, 14, 39, 23, 23, 4, 10, 10, 4, 33, 9, 26, 28, 28 };

/* 0x463f60: at most three engine banks for the whole field */
static int pick_banks(const int *rows, int n, int *carslot, int pick[3])
{
    int cls[CARSFX_AI_MAX], own[CARSFX_AI_MAX], present[3] = { 0, 0, 0 }, cnt = 0;
    pick[0] = pick[1] = pick[2] = -1;
    for (int k = 0; k < n; k++) {
        int r = rows[k] < 0 || rows[k] > 32 ? 18 : rows[k];
        cls[k] = engine_class(r); own[k] = k_row_engine[r]; carslot[k] = -1;
        present[cls[k]] = 1;
    }
    for (int k = 0; k < n; k++) {                       /* each class keeps its first car's own bank */
        int c = cls[k];
        if (pick[c] != -1 || !slot_ok(c, own[k]) || in_picks(pick, own[k])) continue;
        carslot[k] = own[k]; pick[c] = own[k];
        if (++cnt == 3 || cnt == n) goto done;
    }
    for (int k = 0; k < n; k++) {                       /* a class with no usable own bank: one at random */
        int c = cls[k];
        if (pick[c] != -1) continue;
        int s = slot_fresh(pick, c);
        pick[c] = s; carslot[k] = s;
        if (++cnt == 3 || cnt == n) goto done;
    }
    for (int p = 0, c = 0; p < 3 && cnt != 3 && cnt != n; p++) {   /* spare picks go to other cars */
        if (pick[p] != -1) continue;
        for (int tries = 0; tries < 3; tries++) {
            while (!present[c]) c = (c + 1) % 3;
            int k;
            for (k = 0; k < n; k++) if (cls[k] == c && carslot[k] == -1) break;
            c = (c + 1) % 3;
            if (k == n) continue;
            int s = own[k];
            if (!slot_ok(cls[k], s) || in_picks(pick, s)) s = slot_fresh(pick, cls[k]);
            pick[p] = s; carslot[k] = s; cnt++;
            break;
        }
    }
done:
    for (int k = 0, i = 0; k < n; k++) {                /* the rest share round robin */
        if (carslot[k] != -1) continue;
        for (int t = 0; t < 3 && !slot_ok(cls[k], pick[i]); t++) i = (i + 1) % 3;
        carslot[k] = pick[i];
        i = (i + 1) % 3;
    }
    return cnt;
}

int carsfx_race_begin(int mixmap, int streetx, int drift, const int *rows, int nai, int bank_slot[3])
{
    carsfx_race_end();
    g_mixmap = mixmap >= 0 && mixmap < 4 ? mixmap : 0;
    g_streetx = streetx; g_drift = drift;
    g_nai = nai < 0 ? 0 : nai > CARSFX_AI_MAX ? CARSFX_AI_MAX : nai;
    g_naitb = 0; g_fxtime = 0;
    int carslot[CARSFX_AI_MAX], pick[3];
    pick_banks(rows, g_nai, carslot, pick);
    int nb = 0;
    for (int i = 0; i < 3; i++) {
        if (pick[i] < 0) continue;
        int dup = 0;
        for (int j = 0; j < nb; j++) dup |= bank_slot[j] == pick[i];
        if (!dup) bank_slot[nb++] = pick[i];
    }
    for (int k = 0; k < g_nai; k++) {
        AiSnd *a = &g_ai[k];
        ai_reset(a);
        a->on = 1;
        a->slot = carslot[k] >= 0 && carslot[k] < 41 ? carslot[k] : 0;
        a->bank = -1;
        for (int j = 0; j < nb; j++) if (bank_slot[j] == carslot[k]) a->bank = j;
        g_ndetune = (g_ndetune + 100) % 600;           /* 0x473ce0 */
        a->detune = g_ndetune - 300;
        /* the effects: the turbo record of the car's row at upgrade level 0
           (0x45eb30, 0x5995e0(4) is 0 for the AI), the stereo roll (every
           6th AI car, 0x4765f0; 0x476580 rolls the variant again) */
        AiFx *x = &g_aifx[k];
        memset(x, 0, sizeof *x);
        x->t_rec = audio_row_turbo(rows[k]);
        x->t_set = -1;
        for (int j = 0; j < g_naitb; j++) if (g_aitb[j] == x->t_rec) x->t_set = j;
        if (x->t_set < 0 && g_naitb < CARSFX_AI_MAX) { x->t_set = g_naitb; g_aitb[g_naitb++] = x->t_rec; }
        x->s_var = 8 + (int)rng_int(2);
        g_stereo_n = (g_stereo_n + 1) % 6;
        x->s_on = g_stereo_n == 0;
        x->s_var = 8 + (int)rng_int(2);
        printf("car sounds: opponent %d audio row %d -> engine CAR_%02d (bank %d)\n", k, rows[k], a->slot, a->bank);
    }
    for (int i = 0; i < CARSFX_TR_MAX; i++) g_trs[i].car = -1;
    return nb;
}

int carsfx_ai_bank(int k) { return k >= 0 && k < g_nai ? g_ai[k].bank : -1; }

void carsfx_race_end(void)
{
    g_nai = 0;
    memset(g_ai, 0, sizeof g_ai);
    for (int i = 0; i < CARSFX_TR_MAX; i++) g_trs[i].car = -1;
    for (int v = 0; v < AUDIO_CARVOICES; v++) g_carvoice[v].on = 0;
    for (int v = 0; v < AUDIO_FXVOICES; v++) g_fxvoice[v].on = 0;
    memset(g_aifx, 0, sizeof g_aifx);
    memset(g_trfx, 0, sizeof g_trfx);
    co_reset();
}

/* the player's engine level in the same mix (map 1 engine -300 mB, its 3D
   record at chase-camera range: 0 dB) -- the port's player engine plays
   at its own fixed level, so the others are set relative to it */
static float player_ref(void) { return (float)carsfx_mb2lin(-300) / 32767.0f; }

/* the player's turbo: the same CARSFX_Turbo on the player's car, map
   class 1 object 0x40010020 (g_fxvoice 276 whine, 277 blow-off) */
#define FXP(j) (&g_fxvoice[200 + CO_HITS * CO_SMP + CO_REC + (j)])
static AiFx g_ptx;
static Pos3D g_pt_p3;
static void player_turbo(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll || !L->pturbo || L->ptrec < 0 || L->ptrec >= 15) {
        if (g_ptx.t_on) { fx_stop(FXP(0)); fx_stop(FXP(1)); memset(&g_ptx, 0, sizeof g_ptx); }
        return;
    }
    if (!g_ptx.t_on || g_ptx.t_rec != L->ptrec) {
        fx_stop(FXP(0)); fx_stop(FXP(1));
        memset(&g_ptx, 0, sizeof g_ptx);
        memset(&g_pt_p3, 0, sizeof g_pt_p3);
        g_ptx.t_on = 1; g_ptx.t_rec = L->ptrec;
    }
    pos3d_update(&g_pt_p3, L->car, L, dt);
    turbo_update(&g_ptx, FXP(0), FXP(1), AFX_PTURBO, L->pload, L->pthr, 0, L->prpm, 1, 0x40010020u, &g_pt_p3,
                 L->mode, dt, ref);
}

void carsfx_update(float dt, const CarSfxListener *L, const CarSfxCar *ai, int nai,
                   const CarSfxCar *tr, int ntr)
{
    if (dt <= 0) return;
    g_fxtime += dt;
    float ref = player_ref();
    g_co_cam[0] = L->cam[0]; g_co_cam[1] = L->cam[1]; g_co_cam[2] = L->cam_z;
    if (!L->coll && g_co_live) co_reset();
    g_co_live = L->coll;
    if (nai > g_nai) nai = g_nai;
    for (int k = 0; k < CARSFX_AI_MAX; k++) {
        AudioCarVoice *v = &g_carvoice[k];
        AiSnd *a = &g_ai[k];
        if (k >= nai || !a->on || !ai[k].on || a->bank < 0) {
            v->on = 0;
            for (int j = 0; j < 10; j++) fx_stop(FXA(k, j));
            g_aifx[k].n_on = g_aifx[k].s_play = g_aifx[k].t_bov = 0;
            continue;
        }
        const CarSfxCar *c = &ai[k];
        a->time += dt;
        aiphys_update(a, c, L, dt);
        sh_update(a, dt);
        at_update(a, c, dt);
        eng_update(a, dt);
        pos3d_update(&a->p3, c->pos, L, dt);
        a->lpf = eng_lpf(a->p3.d[0]);
        /* CARSFX_AIEngine 0x4610f0 */
        const float *sr = k_slot_rpm[a->slot];
        float rpm = (a->erpm - 1000.0f) * 0.000111111f * (sr[0] - sr[1]) + sr[1];
        int objvol = mix_objvol(2, 0x40020000u, 0, L->mode, a->p3.d, a->p3.az);
        int vol = ((a->vol * objvol) >> 15) - 0x7fff;
        vol = vol < -0x7fff ? -0x7fff : vol > 0 ? 0 : vol;
        int pof = ftol((float)(a->detune + 16000) * pos3d_pitch(&a->p3, 2) - 16000.0f);
        pof = pof < -0x3fff ? -0x3fff : pof > 0x3fff ? 0x3fff : pof;
        float load = a->load * 10.24f;
        load = load > 0 ? (load > 1024.0f ? 1024.0f : load) : 0;
        /* the voice: the SPU bank's loops through the port's band mixer
           (the CAR patch is not decoded): engine rpm 1000..10000 onto its
           1000..8000 scale, by the slot's recorded range */
        float fr = sr[0] > sr[1] ? (rpm - sr[1]) / (sr[0] - sr[1]) : 0;
        fr = fr > 0 ? (fr > 1 ? 1 : fr) : 0;
        v->bank = a->bank;
        v->rpm = ENG_IDLE_RPM + fr * (ENG_REDLINE_RPM - ENG_IDLE_RPM);
        v->load = load / 1024.0f;
        v->gain = (float)(vol + 0x7fff) / 32767.0f / ref;
        v->pitch = 1.0f + (float)pof / 16000.0f;
        v->cutoff = (float)a->lpf;
        v->on = 1;
        AiFx *x = &g_aifx[k];
        if (!x->init) { x->init = 1; x->t_on = c->turbo && x->t_set >= 0; }
        ai_fx_update(k, a, x, c, L, dt, ref);
    }
    /* traffic: a slot for a car within 70 m of the player's car (0x463e30) */
    for (int i = 0; i < CARSFX_TR_MAX; i++) {
        TrSlot *t = &g_trs[i];
        if (t->car < 0) continue;
        const CarSfxCar *c = t->car < ntr ? &tr[t->car] : NULL;
        if (!c || !c->on || hypotf(c->pos[0] - L->car[0], c->pos[1] - L->car[1]) >= 70.0f) {
            t->car = -1;
            tr_fx_detach(i, &g_trfx[i]);
        }
    }
    for (int j = 0; j < ntr && j < CARSFX_TR_REG; j++) {
        if (!tr[j].on || hypotf(tr[j].pos[0] - L->car[0], tr[j].pos[1] - L->car[1]) >= 70.0f) continue;
        int have = 0, fre = -1;
        for (int i = 0; i < CARSFX_TR_MAX; i++) {
            if (g_trs[i].car == j) have = 1;
            else if (g_trs[i].car < 0 && fre < 0) fre = i;
        }
        if (have || fre < 0) continue;
        memset(&g_trs[fre], 0, sizeof g_trs[fre]);
        g_trs[fre].car = j;
        tr_fx_detach(fre, &g_trfx[fre]);
        tr_fx_attach(&g_trfx[fre], tr[j].cartype);
    }
    for (int i = 0; i < CARSFX_TR_MAX; i++) {
        AudioCarVoice *v = &g_carvoice[CARSFX_AI_MAX + i];
        TrSlot *t = &g_trs[i];
        if (t->car < 0) { v->on = 0; continue; }
        pos3d_update(&t->p3, tr[t->car].pos, L, dt);
        int objvol = mix_objvol(3, 0x40030000u, 0, L->mode, t->p3.d, t->p3.az);
        int pof = ftol(pos3d_pitch(&t->p3, 1) * 16383.0f - 16383.0f);
        v->bank = -1;
        v->rpm = 0; v->load = 0;
        v->gain = (float)objvol / 32767.0f / ref;
        v->pitch = 1.0f + (float)pof / 16383.0f;
        v->cutoff = 24000.0f;
        v->on = 1;
        tr_fx_update(i, &g_trfx[i], &t->p3, &tr[t->car], L, dt, ref);
    }
    if (g_co_live) co_update(dt, L, ref);
    player_turbo(dt, L, ref);
}
