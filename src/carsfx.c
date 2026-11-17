/* carsfx.c — the opponents' and traffic's engine sounds (see carsfx.h).
 * Addresses are SPEED2.EXE; RE notes in re-tools/out/AUDIO_AI.md. */
#include "carsfx.h"
#include "audio.h"
#include "options.h"
#include "speech.h"     /* the thunder stream */
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
/* a master channel: its output id, level, 3D connects (its 0x8/0x9 inputs
   in order, 0x480420), and the object slots it feeds (out-map, header [5]:
   entry slot (e >> 16) & 0xf, 3D connect (e >> 20) & 0xf, mB offset
   (short)e; evaluator 0x477aa0) */
typedef struct { unsigned out; int level, rec, n3, r3[4], nsl, slot[8], off[8], sel[8]; } MixMaster;
typedef struct { int n3d; Mix3D r3d[8]; int nmst; MixMaster mst[16]; } MixClass;
static MixClass g_mix[4][9];    /* [mixmap][class 0..8]; AI = 2, traffic = 3, collisions = 5, planes = 7 */
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
        if (cls > 8) continue;
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
                        if ((in & 0xe0000000u) == 0x80000000u && mm->n3 < 4) mm->r3[mm->n3++] = (int)(in & 0xff);
                    }
                    /* the channel's out-map group, in channel order */
                    if (om != 0xffffffffu && o + om + 4 <= (unsigned)len) {
                        unsigned c = rd32(d + o + om) & 0xf;
                        if (o + om + 4 + 4 * c > (unsigned)len) return 0;
                        for (unsigned k = 0; k < c; k++) {
                            unsigned e = rd32(d + o + om + 4 + 4 * k);
                            if (mm->nsl < 8) {
                                mm->slot[mm->nsl] = (int)(e >> 16 & 0xf);
                                mm->sel[mm->nsl] = (int)(e >> 20 & 0xf);
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
static void md_load(const unsigned char *d, long len, int m);
static char g_aproot[512];                              /* the data root, for the race's AI patch banks */

int carsfx_init(const char *dataroot)
{
    static const char *const fn[4] = { "MAPOUTPUT", "MAPOUTPUTDFT", "MAPOUTPUTDRG", "MAPOUTPUTURL" };
    int n = 0;
    snprintf(g_aproot, sizeof g_aproot, "%s", dataroot);
    memset(g_mix, 0, sizeof g_mix);
    for (int m = 0; m < 4; m++) {
        char path[512];
        snprintf(path, sizeof path, "%s/SOUND/MIXMAPS/%s.mxb", dataroot, fn[m]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
        if (d && fread(d, 1, (size_t)len, f) == (size_t)len && mix_parse(d, len, g_mix[m])) { md_load(d, len, m); n++; }
        free(d); fclose(f);
    }
    fx_load(dataroot);
    printf("car sounds: %d of 4 mixmaps (AI engine %d mB, traffic engine %d mB)\n", n,
           g_mix[0][2].nmst ? -g_mix[0][2].mst[0].level : 0, g_mix[0][3].nmst ? -g_mix[0][3].mst[0].level : 0);
    return n;
}

/* the 3D gain of record r for a source at distance d (by sel) and azimuth
   az (0x468580 -> 0x468380), mB */
static const MixSub *mix_sub(const Mix3D *x, int mode)
{
    for (int s = 0; s < x->nsub; s++)
        if (x->sub[s].mode == mode) {
#ifdef OPENUG2_SIM_PRE_H
            static int seen[8];
            if (mode > 0 && mode < 8 && !seen[mode]) { seen[mode] = 1; printf("[mix3d] mode %d sub table in use (%d subs)\n", mode, x->nsub); }
#endif
            return &x->sub[s];
        }
    return &x->sub[0];
}
static int mix_3d(const Mix3D *x, int mode, const int d[2], const unsigned az[2])
{
    if (x->nsub <= 0) return -10000;
    const MixSub *u = mix_sub(x, mode);
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

/* ---- the mixmaps' dynamic inputs (GC NFSMixMaster::ProcessMix) ----------
   Every channel (header [3] sub channels, [4] masters) sums its fixed inputs
   onto its base (+0x18: 0 for a sub, -level for a master) and clamps to
   [+0x20, +0x1c] (sub: the record's down / up; master: -level - 10000 /
   +level). Inputs: 0x8/0x9 = 3D record (added per out-map entry, mix_3d),
   0x3CC000ss = sub channel ss of class CC, 0xaTCC3iii / 0xbTCC3iii =
   NFSEvtMixCtl iii of class CC (header [6], 7 dwords: type in bits 24-27
   0 AR / 1 ASR / 2 ATR, range, scale id, trigger id, A, S, R),
   0x0NCC6iii = NFSMixCtl iii (header [1], 3 dwords: id = curve << 24 |
   source, word, scale id). A scale id of 0 reads nUnityMixScale (GC
   GetScaleOutputPtr: r13 - 0x75d0 = 0x8046d810 = 0x7fff, unity). */
#define MD_NCH 24
#define MD_NIN 16
#define MD_NEVT 32
#define MD_NDYN 12
#define MD_COPY 16          /* object copies of a class: traffic 6, collision records (CO_REC) 16 */
typedef struct { int sub, up, down, nin; unsigned in[MD_NIN]; } MdChan;
typedef struct { unsigned id, word, scale; } MdDyn;
typedef struct { unsigned type, scale, trig, a, s, r; int range; } MdEvt;
typedef struct { int nch, nsub, ndyn, nevt; MdChan ch[MD_NCH]; MdDyn dyn[MD_NDYN]; MdEvt evt[MD_NEVT]; } MdClass;
typedef struct { float t, trel; int on, lvl0, out, env; } MdEs;    /* NFSEvtMixCtl +0x24 / +0x28 / +4 / +8 / +0x18 / +0x1c */
static MdClass g_md[4][9];                  /* [mixmap][class 0..8] */
static MdEs g_mdes[9][MD_NEVT][MD_COPY];
static int g_mdev[9][MD_NEVT], g_mdevsum[9][MD_NEVT], g_mddyn[9][MD_NDYN], g_mdsub[9][MD_NCH], g_mdfix[9][16];
/* the objects' outputs the events read (set during a frame, read at the next) */
static int g_md_bo[2];                      /* CARSFX_BottomOut +4 land (0x47ea30) / +8 bottom-out stitch (0x47ec60) */
static int g_md_db;                         /* CARSFX_DriveBy +0x18 (output 5): a whoosh stitch started (GC 800a2264 /
                                               800a2394 = 1 after Play; ProcessUpdate 800a23c8 clears it) */
static int g_md_trw[MD_COPY], g_md_trh[MD_COPY];   /* SFXObj TrafficWoosh +0xc start pulse / TrafficHorn +8 playing */
static int g_md_co[MD_COPY][4];             /* SFXObj_Collision +4 / +8 pulses, +0x18 / +0x1c strengths (kept) */
static const CarSfxListener *g_md_L;
volatile int g_eng_dyn_mb, g_mus_ext_mb, g_amb_ext_mb;    /* -> audio.c: master 0x40010000's inputs; map 0 sub 1's car inputs */

static void md_parse(const unsigned char *d, long len, MdClass *out)
{
    unsigned nmaps = rd32(d + 4);
    for (unsigned m = 0; m < nmaps && m < 9; m++) {
        unsigned o = rd32(d + 0x10 + 4 * m);
        if (o + 0x20 > (unsigned)len) return;
        unsigned cls = rd32(d + o) & 0xffff;
        if (cls > 8) continue;
        MdClass *mc = &out[cls];
        memset(mc, 0, sizeof *mc);
        unsigned td = rd32(d + o + 4), te = rd32(d + o + 24);
        if (td != 0xffffffffu && o + td + 0x10 <= (unsigned)len) {
            unsigned p = o + td, c = rd32(d + p);
            for (unsigned k = 0; k < c && k < MD_NDYN && p + 0x10 + 12 * k + 12 <= (unsigned)len; k++) {
                const unsigned char *b = d + p + 0x10 + 12 * k;
                mc->dyn[k].id = rd32(b); mc->dyn[k].word = rd32(b + 4); mc->dyn[k].scale = rd32(b + 8);
                mc->ndyn = (int)k + 1;
            }
        }
        if (te != 0xffffffffu && o + te + 0x10 <= (unsigned)len) {
            unsigned p = o + te, c = rd32(d + p);
            for (unsigned k = 0; k < c && k < MD_NEVT && p + 0x10 + 28 * k + 28 <= (unsigned)len; k++) {
                const unsigned char *b = d + p + 0x10 + 28 * k;
                MdEvt *e = &mc->evt[k];
                unsigned rg = rd32(b + 4) & 0xffff;
                e->type = rd32(b) >> 24 & 0xf;
                e->range = rg < 0x2711 ? (int)rg : (int)(short)rg;
                e->scale = rd32(b + 8); e->trig = rd32(b + 12);
                e->a = rd32(b + 16); e->s = rd32(b + 20); e->r = rd32(b + 24);
                mc->nevt = (int)k + 1;
            }
        }
        for (int part = 0; part < 2; part++) {          /* [3] subs, then [4] masters */
            unsigned t = rd32(d + o + 12 + 4 * part);
            if (t == 0xffffffffu || o + t + 0x10 > (unsigned)len) continue;
            unsigned p = o + t, cnt = rd32(d + p), q = p + 0x10;
            for (unsigned r = 0; r < cnt; r++) {
                if (q + 12 > (unsigned)len) return;
                unsigned h = rd32(d + q), l = rd32(d + q + 4), ni = h >> 16 & 0xff;
                int sub = (h & 0x10000000u) != 0;
                unsigned ip = q + (sub ? 8 : 12);
                if (ip + 4 * ni > (unsigned)len) return;
                if (mc->nch < MD_NCH) {
                    MdChan *ch = &mc->ch[mc->nch++];
                    ch->sub = sub;
                    if (sub) { ch->up = (int)(l >> 16 & 0x7fff); ch->down = (int)(l & 0xffff) - 0x10000; mc->nsub++; }
                    else { ch->up = (short)(l >> 16); ch->down = -ch->up - 10000; }
                    ch->nin = ni > MD_NIN ? MD_NIN : (int)ni;
                    for (int k = 0; k < ch->nin; k++) ch->in[k] = rd32(d + ip + 4 * k);
                }
                q = ip + 4 * ni;
            }
        }
    }
}

static void md_load(const unsigned char *d, long len, int m)
{
    memset(g_md[m], 0, sizeof g_md[m]);
    md_parse(d, len, g_md[m]);
}

static int md_q15(float v) { int i = ftol(v * 32767.0f); return i < 0 ? 0 : i > 0x7fff ? 0x7fff : i; }
/* MasterVol f14 (+0x3c, PC 0x471970) = g_IG_Music_Scale (0x7ed344): f1, the
   in-game music slider's attenuation (1 - level x master x
   curve0(0x7fff - master) / 32768), or 0x7fff in a drag (0x89e7d8) or drift
   (0x89e7d9) race. Decoded 11-09 from GC SFXCTL_MasterVol::UpdateParams
   (80096ca0): 0x7fff when TheRaceParameters +0x40 / +0x44 (drag / drift) or
   g_tweakIsDragRace / g_tweakIsDriftRace (debug tweaks, always 0) or
   g_pEAXS_PFController +0x98 == 0 -- the number of jukebox songs in mode 2 / 3
   (in game / all), counted by EAXSound::InitEATRAX (8007bc70). */
static int md_igscale(void)
{
    if (g_mixmap == 1 || g_mixmap == 2 || (g_md_L && g_md_L->drag)) return 0x7fff;
    int ig = 0;
    for (int i = 0; i < 27; i++) ig += g_opt.pl_mode[i] == 2 || g_opt.pl_mode[i] == 3;
    if (!ig) return 0x7fff;
    float m = g_opt.master;
    float x = (float)carsfx_curve(0, (unsigned)(0x7fff - md_q15(m)), 0) * 3.05185e-5f;
    return md_q15(1.0f - g_opt.ig_music * m * x);
}
/* CARSFX_Rain (type 0x10090) on view 1: the rain's intensity (Rain +0x1f0,
   carsfx_set_rain), its FX_Weather instance (+0xd0) and the stopping flag
   (+0xcc) -- UpdateMixerOutputs 0x461890 publishes playing && !stopping */
static struct { float I, I_prev, stop_t, thunder_t, thunder_period; int playing, stopping, thunder_n; } g_rain;
static int g_tun_type;   /* SFXCTL_Tunnel +0x78 while +0x70 (in a tunnel), else 0 */
void carsfx_set_tunnel(int type) { g_tun_type = type; }
static void weather_stop(void);
/* an output an event or ctl reads (UpdateParams of the SFXCTL / SFXObj
   objects: 0x6CC000f = SFXCTL field f of class CC, 0x4CCOOOf = object OOO
   field f); copy = the object copy (traffic slot / collision record) */
static int md_src(unsigned id, int copy)
{
    const CarSfxListener *L = g_md_L;
    switch (id) {
    case 0x6000000au: return md_q15(g_opt.master * g_opt.speech);   /* MasterVol slider levels: speech */
    case 0x6000000bu: return md_q15(g_opt.master * g_opt.sfx);      /* sfx */
    case 0x6000000eu: case 0x60010032u: return md_igscale();       /* f14, Shifting f2 */
    case 0x40000010u: return (g_speech_state & 1) != 0;             /* SFXObj_Speech +4: speech in the front end */
    case 0x40000011u: return (g_speech_state & 2) != 0;             /* +8: speech in game */
    case 0x60010000u: case 0x60010001u: case 0x60010002u: case 0x60010003u: {   /* SFXCTL_Physics (0x470a60) */
        static const float top[4] = { 30.0f, 60.0f, 100.0f, 140.0f };
        return L && L->pwheels ? md_q15(fabsf(L->pspeed) * 2.237f / top[id & 3]) : 0;
    }
    case 0x60010005u:                                               /* airborne: no wheel down */
        return L && L->pwheels && !(L->pcontact[0] | L->pcontact[1] | L->pcontact[2] | L->pcontact[3]);
    case 0x60010006u:   /* EAXCar view +0xa8: {0, 1, 5} -> 32000, 4 -> 4000, else 0
                           (smoothed +-0x3fff an update). +0xa8 = the POV type
                           (0x45922c = Player::GetPovType 0x5eb400: bumper 2, hood 4,
                           near 1, far 0, high 5; main.c): the outside views 32000,
                           hood 4000, bumper 0 (confirmed G2 10-31) */
        if (!L || !L->pwheels) return 0;
        return L->pview == 0 || L->pview == 1 || L->pview == 5 ? 32000 : L->pview == 4 ? 4000 : 0;
    case 0x60010007u:   /* f7 / f8, SFXCTL_Physics 0x470a60 (decoded G2 10-31): the player
                           (+0x44 -> +0x30 -> +0x18) in camera zone 6 or 7 (+0x58) sets f7 (+0x20)
                           when its mover (+0x238) is not a CubicCameraMover (+0xc != 1, i.e. the
                           TrackCar jump cam runs), else f8 (+0x24) */
        return L && L->pzone67 && !L->pcubic;
    case 0x60010008u:
        return L && L->pzone67 && L->pcubic;
    case 0x60010023u: {                                             /* SFXCTL_Skid f3: GetSkidMag max << 5 */
        float s = g_skid3[0];
        if (g_skid3[1] > s) s = g_skid3[1];
        if (g_skid3[2] > s) s = g_skid3[2];
        return md_q15(s);
    }
    case 0x60010033u: return md_q15(g_opt.master * g_opt.sfx);      /* Shifting f3..f5: GetMasterVolume(4/5/6) */
    case 0x60010034u: return md_q15(g_opt.master * g_opt.engine);
    case 0x60010035u: return md_q15(g_opt.master * g_opt.tire);
    case 0x40010030u: return g_nitro != 0;                          /* CARSFX_Nitrous +4 (0x472af0 / 0x45f2a0) */
    case 0x40010041u: return g_sput_out != 0;                       /* CAR_Sputter +0xf8 (0x45f760) */
    case 0x40010016u:   /* CARSFX_Shift::UpdateMixerOutputs (GC 80098404): output 6 = the
                           controller's (+0xc8) shift state +0x80 == 1, an upshift's first phase
                           (GUESS as player_swtn: that phase = n2sim's shifting window) */
        return L && L->pwheels && L->pshifting && L->pshiftup;
    case 0x40010092u: return g_rain.playing && !g_rain.stopping;   /* CARSFX_Rain +0xc (0x461890): the wet-road layer */
    /* SFXCTL_Tunnel::UpdateMixerOutputs (GC 800963d0), the player's car in a
       tunnel zone (+0x70): f0 type 5 (0x20), f1 types 6 / 7, f2 type 20, f3 any */
    case 0x60010070u: return g_tun_type && !(g_tun_type == 6 || g_tun_type == 7 || g_tun_type == 20);
    case 0x60010071u: return g_tun_type == 6 || g_tun_type == 7;
    case 0x60010072u: return g_tun_type == 20;
    case 0x60010073u: return g_tun_type != 0;
    case 0x400100c5u: return g_md_db;
    case 0x400100b0u: return g_md_bo[0];
    case 0x400100b1u: return g_md_bo[1];
    case 0x40030003u: return md_q15(g_opt.master * g_opt.sfx);      /* TrafficEngine +0x10 (0x462240) */
    case 0x40030012u: return copy < MD_COPY ? g_md_trw[copy] : 0;
    case 0x40030021u: return copy < MD_COPY ? g_md_trh[copy] : 0;
    case 0x40050000u: case 0x40050001u: return copy < MD_COPY ? g_md_co[copy][id & 1] : 0;
    case 0x40050005u: case 0x40050006u: return copy < MD_COPY ? g_md_co[copy][2 + ((id & 1) ^ 1)] : 0;
    default: return 0;  /* not ported: NIS / movies, race mode 8, ambience, class-6 woosh, pause (g_game_mute) */
    }
}
/* UpdateAREvent 800b65a0 / UpdateASREvent 800b6708 / UpdateATREvent 800b68b4 */
static void md_env(MdEs *e, const MdEvt *v, int trig)
{
    float A = (float)(v->a & 0xfff) * 16.666670f, R = (float)(v->r & 0xfff) * 16.666670f, x;
    unsigned w;
    if (v->type == 2) {                                         /* ATR */
        if (A <= e->t) {
            if (trig) {
                if (e->trel != 0.0f) { e->trel = 0.0f; e->on = 1; e->lvl0 = e->out; e->env = 0x7fff; e->t = 0.0f; return; }
                e->env = 0; e->trel = 0.0f; e->t = A; e->on = 0; e->lvl0 = 0; return;
            }
            if (e->trel == 0.0f) e->trel = e->t;
            float el = e->t - e->trel;
            if (R < el) { e->lvl0 = 0; e->env = 0x7fff; e->trel = 0.0f; e->on = 0; e->t = 0.0f; return; }
            x = 32767.0f - el * 32767.0f / R; w = v->r;
        } else {
            if (!trig) { e->env = 0; e->on = 1; e->lvl0 = e->out; e->t = A; return; }
            x = e->t * 32767.0f / A; w = v->a;
        }
    } else {                                                    /* AR, ASR (sustain S) */
        float S = v->type == 1 ? (float)(v->s & 0xfff) * 16.666670f : 0.0f;
        if (A <= e->t) {
            if (v->type == 1 && e->t - A <= S) { e->env = 0; return; }
            float el = e->t - (A + S);
            if (R <= el) { e->env = 0x7fff; e->t = 0.0f; return; }
            x = 32767.0f - el * 32767.0f / R; w = v->r;
            if (x < 0.0f) x = 0.0f; if (x > 32767.0f) x = 32767.0f;
        } else { x = e->t * 32767.0f / A; w = v->a; }
    }
    int c = carsfx_curve((w >> 12) & 0xf, (unsigned)(int)x, 0);
    e->env = v->range < 0 ? c : 0x7fff - c;
}
/* UpdateEvtMixCtl 800b6ab8: the event's mB */
static int md_evt(MdEs *e, const MdEvt *v, int trig, int scale, float dt)
{
    if (e->t == 0.0f && !trig) { e->on = 0; e->trel = 0.0f; return e->out = 0; }
    e->t += dt * 1000.0f;
    if (v->type > 2) { e->out = 0; return 0; }
    md_env(e, v, trig);
    float k = (32767.0f - (float)e->env) * (1.0f / 32768.0f);
    int o;
    if (!e->on) o = (int)(k * (float)v->range);
    else if (!trig) o = (int)(k * (float)e->lvl0);
    else o = (int)(k * (float)(v->range - e->lvl0)) + e->lvl0;
    e->out = o;
    return (int)((float)scale / 32767.0f * (float)o);
}
/* NFSMixCtl (GetMixScaleRatio / UpdateMixCtl): the source through its curve
   (id bits 24-28) to mB: word bit 15 clear = offset +(word & 0x7fff), range
   -(word & 0x7fff); set = offset 0, range (short)word */
static int md_dyn(const MdDyn *c)
{
    int w = (int)(c->word & 0xffff), off = 0, range;
    if (!(w & 0x8000)) { off = w & 0x7fff; range = -off; } else range = (short)w;
    int src = md_src((c->id & 0xe0fffff0u) | (c->id & 0xf), 0);
    src = src < 0 ? 0 : src > 0x7fff ? 0x7fff : src;
    int q = carsfx_curve(c->id >> 24 & 0x1f, (unsigned)src, 0);
    int k28 = 0x7fff - carsfx_mb2lin(range);
    int out = carsfx_lin2mb((unsigned)(0x7fff - (((0x7fff - q) * k28) >> 15))) + off;
    if (c->scale) out = out * md_src(c->scale, 0) >> 15;
    return out;
}
static int md_in(unsigned in, unsigned own, int *is3d)
{
    unsigned cls = in >> 16 & 0xff, idx = in & 0xfff;
    *is3d = 0;
    if ((in & 0xe0000000u) == 0x80000000u) { *is3d = 1; return 0; }
    if (cls > 8) return 0;
    if (in >> 28 == 3) return idx < MD_NCH ? g_mdsub[cls][idx] : 0;
    if (in >> 28 >= 0xa && (in >> 12 & 0xf) == 3) return idx >= MD_NEVT ? 0 : cls == own ? g_mdev[cls][idx] : g_mdevsum[cls][idx];
    if (in >> 28 == 0 && (in >> 12 & 0xf) == 6) return idx < MD_NDYN ? g_mddyn[cls][idx] : 0;
    return 0;
}
/* once a frame, before the objects: events, ctls, then every class's
   channels in order */
static void md_update(const CarSfxListener *L, float dt)
{
    static const int ncopy[9] = { 1, 1, 1, CARSFX_TR_MAX, 1, MD_COPY, 1, 1, 1 };
    MdClass *map = g_md[g_mixmap];
    g_md_L = L;
    for (int c = 0; c < 9; c++) {
        const MdClass *mc = &map[c];
        for (int i = 0; i < mc->nevt; i++) {
            const MdEvt *v = &mc->evt[i];
            int best = 0, sum = 0;
            for (int k = 0; k < ncopy[c]; k++) {
                /* class 0 event 0 (MasterVol f6, paused) is the port's
                   g_game_mute in audio.c */
                int trig = c == 0 && i == 0 ? 0 : md_src(v->trig, k) != 0;
                int o = md_evt(&g_mdes[c][i][k], v, trig, v->scale ? md_src(v->scale, k) : 0x7fff, dt);
                /* a channel of another class gets every copy's output as its
                   own input, summed by ProcessMix and then clamped (GC
                   ConnectMixChannel 800b4d94..800b4e48 loops over the
                   class's states; ProcessMix 800b1b20). A same-class
                   channel reads its own state; the port runs one channel
                   set per class, so that keeps the copy with the largest
                   level. */
                sum += o;
                if (abs(o) > abs(best)) best = o;
            }
            g_mdev[c][i] = best; g_mdevsum[c][i] = sum;
        }
        /* class 0 ctls 0..5 = the option sliders (MasterVol f0..f5): the
           port applies those as bus gains (audio_set_levels) */
        for (int i = 0; i < mc->ndyn; i++) g_mddyn[c][i] = c == 0 && i < 6 ? 0 : md_dyn(&mc->dyn[i]);
    }
    for (int c = 0; c < 9; c++) {
        const MdClass *mc = &map[c];
        int ns = 0, nm = 0;
        for (int i = 0; i < mc->nch; i++) {
            const MdChan *ch = &mc->ch[i];
            int v = ch->sub ? 0 : -ch->up, is3d;
            for (int k = 0; k < ch->nin; k++) v += md_in(ch->in[k], (unsigned)c, &is3d);
            if (ch->sub) {
                if (v > ch->up) v = ch->up; if (v < ch->down) v = ch->down;
                if (ns < MD_NCH) g_mdsub[c][ns++] = v;
            } else if (nm < 16) g_mdfix[c][nm++] = v + ch->up;   /* the inputs' sum; the clamp goes after 3D */
        }
    }
    /* the player's engine: master 0x40010000 (class 1 master 0, no 3D in
       the port's engine path), clamped */
    {   const MixClass *m1 = &g_mix[g_mixmap][1];
        int lv = m1->nmst ? m1->mst[0].level : 300, v = -lv + g_mdfix[1][0];
        if (v > lv) v = lv; if (v < -lv - 10000) v = -lv - 10000;
        g_eng_dyn_mb = v + lv;
    }
    /* map 0 sub 1 (the in-game music's duck, clamp -1320 in audio.c): its
       inputs from the other classes (the class 0 ones are audio.c's) */
    {   const MdClass *m0 = &map[0];
        int v = 0, is3d;
        for (int i = 0, ns = 0; i < m0->nch; i++) {
            if (!m0->ch[i].sub) continue;
            if (ns++ != 1) continue;
            for (int k = 0; k < m0->ch[i].nin; k++)
                if ((m0->ch[i].in[k] >> 16 & 0xff) != 0) v += md_in(m0->ch[i].in[k], 0, &is3d);
        }
        g_mus_ext_mb = v;
        /* map 0 master 4 (SFXObj_Ambience, 0x40000020): its class-1
           inputs (dyn 9, evts 0x17 / 0x18 in the shipped maps) -> audio.c's
           bed level; the class-0 ones (speech ducks) are audio.c's */
        v = 0;
        for (int i = 0, nm = 0; i < m0->nch; i++) {
            if (m0->ch[i].sub) continue;
            if (nm++ != 4) continue;
            for (int k = 0; k < m0->ch[i].nin; k++)
                if ((m0->ch[i].in[k] >> 16 & 0xff) == 1) v += md_in(m0->ch[i].in[k], 0, &is3d);
        }
        g_amb_ext_mb = v;
    }
#ifdef OPENUG2_SIM_PRE_H
    {   static float acc; static int on = -1;
        if (on < 0) on = getenv("OPENUG2_MIXDYN_LOG") != NULL;
        acc += dt;
        if (on && acc >= 0.25f) {
            acc = 0;
            printf("[mixdyn] amb %d eng %d mus %d sub5 %d sub8 %d ev0 %d ev1 %d ev2 %d ev4 %d ev21 %d ev22 %d tr %d co %d/%d dyn5 %d spd %d\n",
                   g_amb_ext_mb, g_eng_dyn_mb, g_mus_ext_mb, g_mdsub[1][5], g_mdsub[1][8], g_mdev[1][0], g_mdev[1][1], g_mdev[1][2],
                   g_mdev[1][4], g_mdev[1][21], g_mdev[1][22], g_mdev[3][0], g_mdev[5][0], g_mdev[5][1],
                   g_mddyn[1][5], md_src(0x60010000u, 0));
        }
    }
#endif
    /* the pulse outputs are cleared by their objects' next update */
    g_md_bo[0] = g_md_bo[1] = 0; g_md_db = 0;
    memset(g_md_trw, 0, sizeof g_md_trw);
    for (int k = 0; k < MD_COPY; k++) g_md_co[k][0] = g_md_co[k][1] = 0;
    g_md_L = NULL;
}

/* an object's mixmap volume in one slot (vt+0x38 0x45e390): its master
   channel's base -level plus its fixed inputs (md_update) and the 3D record
   the slot reads, clamped to [-level - 10000, +level] (0x479030), plus the
   slot's out-map offset (0x477aa0: below -10000 -> 0, above 0 -> 0x7fff,
   else 0x467ff0). A slot no channel feeds -> 0. */
static int mix_objvol(int cls, unsigned out, int slot, int mode, const int d[2], const unsigned az[2])
{
    const MixClass *mc = &g_mix[g_mixmap][cls];
    for (int i = 0; i < mc->nmst; i++) {
        const MixMaster *m = &mc->mst[i];
        if (m->out != out) continue;
        for (int k = 0; k < m->nsl; k++) {
            if (m->slot[k] != slot) continue;
            int mb = -m->level + (i < 16 ? g_mdfix[cls][i] : 0), r = m->n3 > 0 && m->sel[k] < m->n3 ? m->r3[m->sel[k]] : -1;
            if (r >= 0 && r < mc->n3d) mb += mix_3d(&mc->r3d[r], mode, d, az);   /* the slot's 3D connect */
            if (mb > m->level) mb = m->level;
            if (mb < -m->level - 10000) mb = -m->level - 10000;
            mb += m->off[k];
            if (mb < -10000) return 0;
            if (mb > 0) return 0x7fff;
            return carsfx_mb2lin(mb) & 0x7fff;
        }
    }
    return 0;
}
/* an object's azimuth word in one slot (vt+0x3c 0x45e3b0 = SFX_Base::
   GetAzimOutput: the output's high u16). 0x477aa0 writes it from the slot's
   3D connect (NFS3DMixCtl +0x14 = the azimuth its active sub selects,
   0x468580); a channel without 3D connects writes *0x82bc7c = 4 (front);
   a slot no channel feeds reads 0 */
static unsigned mix_objaz(int cls, unsigned out, int slot, int mode, const unsigned az[2])
{
    const MixClass *mc = &g_mix[g_mixmap][cls];
    for (int i = 0; i < mc->nmst; i++) {
        const MixMaster *m = &mc->mst[i];
        if (m->out != out) continue;
        for (int k = 0; k < m->nsl; k++) {
            if (m->slot[k] != slot) continue;
            if (m->n3 <= 0) return 4;
            int r = m->sel[k] < m->n3 ? m->r3[m->sel[k]] : -1;
            if (r < 0 || r >= mc->n3d || mc->r3d[r].nsub <= 0) return 0;
            return az[mix_sub(&mc->r3d[r], mode)->asel ? 1 : 0] & 0xffff;
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
static const float k_slot_rpm[43][2] = {
    {9229, 1350}, {8222, 1500}, {8007, 1250}, {8311, 1250}, {8836, 1000}, {8338, 1000}, {6888, 1000},
    {7099, 800}, {7145, 1000}, {8292, 1450}, {7239, 1100}, {8936, 1250}, {6509, 750}, {5672, 750},
    {5121, 750}, {6975, 1000}, {6313, 1000}, {6881, 1000}, {7037, 1526}, {8222, 1250}, {7158, 1500},
    {7085, 1500}, {8284, 1250}, {7158, 1500}, {7363, 1750}, {6599, 1200}, {6510, 1200}, {6596, 1250},
    {6773, 1000}, {5653, 1350}, {6548, 1250}, {6566, 1150}, {6840, 1250}, {6786, 1200}, {7301, 1250},
    {7421, 1250}, {6508, 1250}, {6958, 1000}, {4782, 1000}, {7052, 1000}, {8326, 1250},
    {5200, 750}, {6509, 750} };   /* 41, 42: CAR_14's / CAR_12's files, their own records */

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
    a->erpm = r + a->rpmw + a->rpmw;      /* + the bump +0x1c8: 0 for AI (only the player's
                                             physics ctl 0x470940 arms it; AIPhysics vt+0x18 is a ret) */
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
   0x2e.. as well while the nearest view 0x459ac0 is in camera zone 6, the
   jump cam) */
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
    v->gain = 0; v->done = 0; v->pan = 0;
    v->on = 1;
    v->trig++;
}
static void fx_stop(AudioFxVoice *v) { v->on = 0; v->gain = 0; v->pan = 0; }
/* azimuth (0 front, 0x4000 right, 0x8000 back, 0x10000 = 360 deg; the word
   mix_objaz gives) -> stereo pan -1..1 for audio.c's equal-power voices.
   The EA sound library's speaker table: SNDI_precalcaztospkrvol 0x72559b /
   SNDI_aztospkrvol 0x725878 with the stereo speakers at 90 and 270 deg
   (SNDI_spkrconfig 0x7258e0): az >> 8 (256 steps, x 0.00549 deg), equal
   power (SNDI_equalpower 0x72554c: sin / cos of the angle's share of the
   speaker gap x pi/2) between the speakers around the front and around the
   back, each gain rounded to 1/255. Speaker 0 (90 deg) is the right one
   (GC SNDDRV_DSPMixerSetPan: gain 0 -> the AX right mix). The pan returned
   gives audio.c's cos/sin pair the same angle (its x sqrt2 keeps the centre
   at the Mono level) */
static float az_pan(unsigned az)
{
    float th = (float)(az >> 8 & 0xff) * 256.0f * 0.0054931640625f, x;
    if (th <= 90.0f) x = (th + 90.0f) / 180.0f;
    else if (th >= 270.0f) x = (th - 270.0f) / 180.0f;
    else x = (270.0f - th) / 180.0f;
    x *= 1.5707964f;
    int gr = (int)(sinf(x) * 255.0f + 0.5f), gl = (int)(cosf(x) * 255.0f + 0.5f);
    return atan2f((float)gr, (float)gl) * (4.0f / 3.14159265f) - 1.0f;
}
/* the port's levels for effects the player also has (audio.c: squeal 0.45,
   rubber 0.35, nitrous 0.35 of the player's engine) over the effect
   mixer's 0.6; the mixmaps set the rest (the turbo too: the player's now
   plays through the same mixmap path) */
static float fx_gain(int vol, float ref, float port) { return (float)vol / 32767.0f / ref * port / 0.6f; }

/* a stitch on voices v[0..nv): every sample at its delay, volume and pitch
   (0x46cdd0 -> 0x45b280); returns the voices used */
typedef struct { int n; int vol[13]; unsigned short pan[13]; } StitchPlay;
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
        sp->pan[k] = m->pan;
        fx_start(&v[k], m->ev ? AFX_WOOSH : AFX_COLL, m->smp, 0, (float)m->delay * 0.001f,
                 pitch ? (float)pitch / 4096.0f : 1.0f);
        sp->n = k + 1;
    }
    return sp->n;
}
/* a stitch's volumes and azimuths (0x47bb30 -> 0x46cf70: the event's
   azimuth = the sample's pan word + the caller's azimuth (vt+0x3c of the
   slot) mod 0x10000 -> the patch's player azimuth); 1 while a sample still
   plays */
static int stitch_update(AudioFxVoice *v, const StitchPlay *sp, int objvol, unsigned az, float ref)
{
    int live = 0;
    for (int k = 0; k < sp->n; k++) {
        if (!v[k].on) continue;
        if (v[k].done) { fx_stop(&v[k]); continue; }
        int g = sp->vol[k] * objvol >> 15;
        v[k].gain = fx_gain(g < 0 ? 0 : g > 0x7fff ? 0x7fff : g, ref, 0.6f);
        v[k].pan = az_pan((sp->pan[k] + az) & 0xffff);
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
    /* CARSFX_Nitrous (0x47cbe0); n_bank = the 0x7ed1a0 index (0 Nitrous_00,
       1 Nitrous_01, SetupLoadData 0x465830) */
    int n_on, n_bank;
    /* CARSFX_Skids: k_bank = engine class == 2 (SetupLoadData 0x465b90,
       0x7ed1f8 [k_bank + 2 drift]) -> set AFX_PSKID + k_bank */
    int k_bank;
    /* CARSFX_AIStereo (0x47f480) */
    int s_on, s_var, s_play;
    int init;
} AiFx;
static AiFx g_aifx[CARSFX_AI_MAX];
static int g_aitb[CARSFX_AI_MAX], g_naitb;
/* carsfx_ai_perf: [k][0] turbo level, [k][1] nitrous level */
static int g_aiperf[CARSFX_AI_MAX][2];
void carsfx_ai_perf(const int *turbo_level, const int *nos_level, int n)
{
    memset(g_aiperf, 0, sizeof g_aiperf);
    for (int k = 0; k < n && k < CARSFX_AI_MAX; k++) {
        g_aiperf[k][0] = turbo_level ? turbo_level[k] : 0;
        g_aiperf[k][1] = nos_level ? nos_level[k] : 0;
    }
}
/* carsfx_ai_engine_level: each AI car's EAXCar +0x94 (CarPreSetup 0x4593b0) */
static int g_aielv[CARSFX_AI_MAX];
void carsfx_ai_engine_level(const int *level, int n)
{
    memset(g_aielv, 0, sizeof g_aielv);
    for (int k = 0; k < n && k < CARSFX_AI_MAX; k++) g_aielv[k] = level ? level[k] : 0;
}
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
    vw->pan = az_pan(mix_objaz(cls, out, 0, mode, az));             /* 0x45ece0: +0x10 = vt+0x3c(0) */
    if (x->t_bov) {
        int b = ftol((float)(x->t_bvol * mix_objvol(cls, out, x->t_var == 1 ? 1 : 2, mode, d, az) >> 15) * x->t_ramp.val);
        vb->gain = fx_gain(b < 0 ? 0 : b > 0x7fff ? 0x7fff : b, ref, 0.6f);
        vb->pan = az_pan(mix_objaz(cls, out, x->t_var == 1 ? 1 : 2, mode, az));
    }
}

static void ai_patch_update(int k, AiSnd *a, AiFx *x, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref);
static void ai_patch_stop(int k);
static void ai_patch_begin(void);
static void ai_fx_update(int k, AiSnd *a, AiFx *x, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    const int *d = a->p3.d; const unsigned *az = a->p3.az;
    /* turbo, nitrous and skids: their AEMS patches (ai_patch_update) */
    ai_patch_update(k, a, x, c, L, dt, ref);
    /* stereo (0x4635b0 / 0x4636e0): ENV_STATIC variant 8/9 = ENV_COMMON
       clip 6/7 (the patch's variant table 0x90c: bank entry 7/8), looped,
       while the mixmap gives it any volume */
    if (x->s_on) {
        int sv = mix_objvol(2, 0x400200f0u, 0, L->mode, d, az);
        AudioFxVoice *v = FXA(k, FA_STEREO);
        if (!x->s_play && sv) { x->s_play = 1; fx_start(v, AFX_ENV, x->s_var - 2, 1, 0, 1); }
        else if (x->s_play && !sv) { x->s_play = 0; fx_stop(v); }
        if (x->s_play) {
            v->gain = fx_gain(sv, ref, 0.6f);
            v->pan = az_pan(mix_objaz(2, 0x400200f0u, 0, L->mode, az));   /* 0x4636e0: +0x10 = vt+0x3c(0) */
        }
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
    if (i < MD_COPY) g_md_trh[i] = 0;
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
        if (x->w_on && i < MD_COPY) g_md_trw[i] = 1;            /* TrafficWoosh +0xc: the stitch started */
        x->w_t = now;
    }
    if (x->w_on) {                                              /* 0x47e8d0 */
        int live = stitch_update(FXT(i, FT_WOOSH), &x->w_sp, mix_objvol(3, 0x40030010u, 2, L->mode, d, az),
                                 mix_objaz(3, 0x40030010u, 2, L->mode, az), ref);
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
    if (i < MD_COPY) g_md_trh[i] = x->h_on;                     /* TrafficHorn +8: playing */
    if (x->h_on) {                                              /* 0x462550 */
        AudioFxVoice *v = FXT(i, FT_HORN);
        int hp = ftol(pos3d_pitch(p3, 0) * 4096.0f);
        hp = hp < 0 ? 0 : hp > 0x1fa0 ? 0x1fa0 : hp;
        int hv = ftol((float)mix_objvol(3, 0x40030020u, 1, L->mode, d, az) * x->h_ramp.val);
        v->gain = fx_gain(hv < 0 ? 0 : hv > 0x7fff ? 0x7fff : hv, ref, 0.6f);
        v->pitch = (float)hp / 4096.0f;
        v->pan = az_pan(mix_objaz(3, 0x40030020u, 1, L->mode, az));   /* vt+0x3c(1) */
    }
    /* stereo, as the AI's */
    if (x->s_on) {
        int sv = mix_objvol(3, 0x40030030u, 0, L->mode, d, az);
        AudioFxVoice *v = FXT(i, FT_STEREO);
        if (!x->s_play && sv) { x->s_play = 1; fx_start(v, AFX_ENV, x->s_var - 2, 1, 0, 1); }
        else if (x->s_play && !sv) { x->s_play = 0; fx_stop(v); }
        if (x->s_play) v->pan = az_pan(mix_objaz(3, 0x40030030u, 0, L->mode, az));
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
        x->b_on = stitch_update(FXT(i, FT_BO), &x->b_sp, mix_objvol(3, 0x40030040u, 0, L->mode, d, az),
                               mix_objaz(3, 0x40030040u, 0, L->mode, az), ref);
        if (!x->b_on) stitch_stop(FXT(i, FT_BO), &x->b_sp);
    }
}

/* the stitches and the effect banks (with the collision clips the
   bottom-out stitches, the collision hits 6..0x5d and the car part
   sounds 0..5 play) */
static void fx_load(const char *dataroot)
{
    stitch_load(dataroot);
    int coll[256], nc = 0;
    unsigned char seen[256] = { 0 };
    for (int id = 0; id < g_nstitch[0]; id++) {
        int bo = 0;
        for (int m = 0; m < 3; m++) bo |= id >= k_bo_first[m] && id < k_bo_first[m] + 4;
        if (!bo && id >= 0x5e) continue;                 /* 0..5: PlayCarPartSnd's */
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
#define CO_SLOSH 8          /* water-drum slosh sets of 3 voices */
#define FXW(k, j) (&g_fxvoice[200 + CO_HITS * CO_SMP + CO_REC + 2 + (k) * 3 + (j)])

typedef struct {
    int on, src, a, b, poly, rec;
    float p0[3], pt[3], nrm[3], slide, isum, t;     /* +0x20 +0x30 +0x70 +0x40 +0x60 +0xc */
    int seen;
} CoEff;
typedef struct {
    int on, eff;                /* +0x24 the contact effect, -1 = gone */
    int kind;                   /* 0x459680: 0 car vs world, 1 car vs car, 2 a smokeable */
    int prop, cat;              /* kind 2: +0x134 the smokeable (caller's id), +0x130 its category */
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
    int ws;                     /* +0x14c: its slosh set, -1 = none */
    int azl;                    /* object +0xbc: latch the azimuths (0x461e10 at attach) */
    unsigned azh, azs;          /* +0xc8 the hit's (type slot), +0xdc the scrape's (slot 9) */
} CoRec;
static CoEff g_coe[CO_EFF];
static CoRec g_cor[CO_REC];
static int g_co_hbused[CO_HITS];
static int g_co_pq[CO_REC], g_co_npq;               /* the queue (owner +4) */
static unsigned g_co_rr[6];                          /* 0x82cbd8 .. 0x82cbec */
static unsigned g_co_rrs[19];                        /* the smokeables' per category (0x479b40) */
static float g_co_lastn[3], g_co_lastp[3];           /* 0x82bcb0, 0x82bcc0 */
static float g_co_cam[3];                            /* the camera (0x5f1160) */
static int g_co_wowner[CO_SLOSH];                    /* slosh set k: owning record + 1, 0 = none */
static int (*g_co_slosh_claim)(int prop);
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
    if (r->kind == 2) {                                   /* 0x47a960: no car, flags 0 */
        r->flags = r->car[0].id >= 0 ? 0x10 | (r->car[0].human != 0) : 0;
        r->type = r->flags & 0x10 ? 7 : 8;                /* GetSurfaceType 0x4598b0 */
    } else {
    r->flags = 0x10 | (r->car[0].human || (r->kind == 1 && r->car[1].human));
    r->type = r->kind == 1 ? co_type_cc(r) : co_type_cw(r, e->nrm);
    }
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
    if (r->kind == 2) {          /* ShouldBeginImpact (GC 800835e4): the player's, or
                                    strength over 9; one hit per prop at a time (the
                                    walk stops at the first record of no smokeable) */
        if (!(r->flags & 1) && !(r->str > 9)) return 0;
        for (int i = 0; i < CO_REC; i++) {
            const CoRec *o = &g_cor[i];
            if (!o->on || o->eff < 0) continue;
            if (o->kind != 2) return 1;
            if (o->prop == r->prop && o->hit) return 0;
        }
        return 1;
    }
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
    if (r->kind == 2) {                                   /* 0x479b40 */
        static const short base[19] = {                   /* type 7; type 8 = +6 */
            259, 199, 259, 124, 139, 94, 94, 259, 259, 229, 214, 229, 244, 259, 259, 154, 274, 169, 109 };
        int c = r->cat >= 0 && r->cat < 19 ? r->cat : 0;
        int hard = ftol(k_coll_curve[i] * -2.0f);         /* 0 or -1 */
        rr = (int)(g_co_rrs[c]++ % 3);
        return base[c] + (r->type == 8 ? 6 : 0) + rr - 3 * hard;
    }
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
    r->azl = 1;
    r->hb = -1;
    for (int h = 0; h < CO_HITS; h++) if (!g_co_hbused[h]) { r->hb = h; break; }
    if (r->hb < 0) return;
    g_co_hbused[r->hb] = 1;
    stitch_start(FXC(r->hb, 0), CO_SMP, 0, id, &r->sp);
}
/* stSmokableCollision::BeginImpact 0x46ae00, after the hit (0x46a540): a
   WATER_DRUM whose motion emitters have started and that has not sloshed
   yet gets three SND_BarrelSplash instances (FX_ROADNOISE_TRANS, member
   data {variant 3, vol, pitch, pan, 1, 0}) */
static const int k_slosh_pitch[3] = { 2000, 4000, 7000 };   /* 0x481fe0: 0x1000 = 1.0 */
static void co_slosh_begin(CoRec *r)
{
    if (r->kind != 2 || r->cat != 5 || r->ws >= 0 || !g_co_slosh_claim || !g_co_slosh_claim(r->prop)) return;
    for (int k = 0; k < CO_SLOSH; k++) {
        int busy = g_co_wowner[k] != 0;
        for (int j = 0; j < 3; j++) busy |= FXW(k, j)->on;
        if (busy) continue;
        g_co_wowner[k] = (int)(r - g_cor) + 1;
        r->ws = k;
        for (int j = 0; j < 3; j++) fx_start(FXW(k, j), AFX_SPLASH, 0, 0, 0, (float)k_slosh_pitch[j] / 4096.0f);
        printf("coll: water drum slosh (prop %d, set %d)\n", r->prop, k);
        return;
    }
}
/* Update 0x481fe0: vol = the record's volume x 17000 (0x7ecf90) >> 15, with a
   car in it x the car's speed map (0x82bcd0: 15..60 mph -> 16002..32767);
   the first instance found finished is released and the rest wait a frame.
   A released record's instances play out (the sound system holds them). */
static void co_slosh_update(CoRec *r, int objvol, float ref)
{
    if (r->ws < 0) return;
    int vol = objvol * 17000 >> 15;
    if (r->flags & 0x10) {
        float t = (r->car[r->sel].speed * 2.237f - 15.0f) / 45.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        vol = vol * ftol(16002.0f + t * (32767.0f - 16002.0f)) >> 15;
    }
    vol = vol < 0 ? 0 : vol > 0x7fff ? 0x7fff : vol;
    for (int j = 0; j < 3; j++) {
        AudioFxVoice *v = FXW(r->ws, j);
        if (!v->on) continue;
        if (v->done) { fx_stop(v); return; }
        v->gain = fx_gain(vol, ref, 0.6f);
        v->pan = az_pan(r->azh);                    /* member +0x10 = record +0xc8 */
    }
}
static void co_begin_impact(CoRec *r)                 /* vt BeginImpact */
{
    co_hit_start(r);
    co_slosh_begin(r);
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
    if (r->ws >= 0) g_co_wowner[r->ws] = 0;               /* its sloshes play out */
    memset(r, 0, sizeof *r);
    r->hb = -1; r->eff = -1; r->ws = -1;
}

static void co_reset(void)
{
    for (int i = 0; i < CO_REC; i++) co_free(&g_cor[i]);
    for (int k = 0; k < CO_SLOSH; k++) for (int j = 0; j < 3; j++) fx_stop(FXW(k, j));
    memset(g_coe, 0, sizeof g_coe);
    memset(g_co_hbused, 0, sizeof g_co_hbused);
    g_co_npq = 0;
}

static int co_key(const CoEff *e, int src, int a, int b, int poly)
{
    return e->on && e->src == src && e->a == a && e->b == b && e->poly == poly;
}

static void co_contact(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                       float slide, const CarSfxCollCar *ca, const CarSfxCollCar *cb, int kind, int prop, int cat)
{
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
                R->on = 1; R->eff = ei; R->hb = -1; R->ws = -1;
                R->kind = kind;                           /* 0x459680 */
                R->prop = prop; R->cat = cat;
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
    if (ca) r->car[0] = *ca;
    else { memset(&r->car[0], 0, sizeof r->car[0]); r->car[0].id = -1; }
    if (cb) r->car[1] = *cb;
    /* 0x4797b0 */
    if (!r->init) co_init(r, e);
    if (!r->hit && !r->hitdone && !r->pend) {
        if (co_filter(r, e)) {                            /* 0x479770 */
            if (g_co_npq < CO_REC) { g_co_pq[g_co_npq++] = e->rec; r->pend = 1; }
        } else r->nohit = 1;                              /* vt+0x1c */
    }
    /* 0x469de0: the scrape when the slide reaches the curve's start (0);
       a smokeable's never begins (ShouldBeginScrape GC 0) */
    if (!r->scr && slide >= 0.0f && r->kind != 2) r->sreq = 1;
}

void carsfx_coll_hit(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                     float slide, const CarSfxCollCar *ca, const CarSfxCollCar *cb)
{
    if (!g_co_live || !ca) return;
    if (b >= 0 && !cb) return;
    co_contact(src, a, b, poly, point, impulse, slide, ca, cb, b >= 0, -1, 0);
}

void carsfx_coll_prop(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                      const CarSfxCollCar *car, int prop, int cat)
{
    if (!g_co_live) return;
    co_contact(src, a, b, poly, point, impulse, 0.0f, car, NULL, 2, prop, cat);
}

void carsfx_set_slosh_claim(int (*claim)(int prop)) { g_co_slosh_claim = claim; }

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
        for (int i = 0; i < g_co_npq; i++) co_begin_impact(&g_cor[g_co_pq[i]]);
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
            if (steal) { co_hit_stop(b); co_begin_impact(a); stole = 1; }
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
                    r->scr = 1; r->fading = 0; r->azl = 1;
                    fx_start(FXS(i), AFX_SCRAPE, 0, 1, 0, 1);
                } else co_end(r);
            }
            if (!r->ended && (!e || (g_fxtime - e->t) > 0.3f)) co_end(r);   /* 0x4595d0 */
            e = r->eff >= 0 ? &g_coe[r->eff] : NULL;
            pos3d_update(&r->p3, r->p, L, dt);
            /* 0x461ce0: the hit's volume = the type's slot, the scrape's
               slot 9 (pitch 0x1000: 0x45e3d0 changes it only in a pause) */
            if (r->azl) {                                 /* +0xbc: the azimuths once per attach */
                r->azl = 0;
                /* SFXObj_Collision outputs (PC 0x461b60): with the hit
                   playing (+0x132), type 0/1 -> +4 = 1, +0x18 = s; type
                   4/5 -> +8 = 1, +0x1c = s; s = ((str x near x 0xff >> 15)
                   x sfx) >> 15, near = 1 - (distance to the closest
                   player's car, 0..40 m) / 40. The strengths stay set.
                   GUESS: the record slot stands for the pooled object;
                   distance in 2D */
                if (r->hit && i < MD_COPY) {
                    float dx = r->p[0] - L->car[0], dy = r->p[1] - L->car[1], dd = sqrtf(dx * dx + dy * dy);
                    dd = dd > 40.0f ? 40.0f : dd;
                    int nr = ftol((1.0f - dd / 40.0f) * 32767.0f);
                    int st = r->str < 0 ? 0 : r->str > 127 ? 127 : r->str;
                    int s = (st * nr * 0xff >> 15) * md_q15(g_opt.master * g_opt.sfx) >> 15;
                    if (r->type == 0 || r->type == 1) { g_md_co[i][0] = 1; g_md_co[i][2] = s; }
                    else if (r->type == 4 || r->type == 5) { g_md_co[i][1] = 1; g_md_co[i][3] = s; }
                }
                r->azh = mix_objaz(5, 0x40050000u, r->type, L->mode, r->p3.az);
                r->azs = mix_objaz(5, 0x40050000u, 9, L->mode, r->p3.az);
            }
            if (r->hit && r->hb >= 0)
                stitch_update(FXC(r->hb, 0), &r->sp, mix_objvol(5, 0x40050000u, r->type, L->mode, r->p3.d, r->p3.az),
                              r->azh, ref);
            if (r->ws >= 0)                               /* the record's volume (+0xc0) */
                co_slosh_update(r, mix_objvol(5, 0x40050000u, r->type, L->mode, r->p3.d, r->p3.az), ref);
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
                v->pan = az_pan(r->azs);                  /* 0x459770 +0xc = record +0xdc */
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
    for (int k = 0; k < CO_SLOSH; k++)                    /* released sloshes that ran out */
        if (!g_co_wowner[k])
            for (int j = 0; j < 3; j++) if (FXW(k, j)->on && FXW(k, j)->done) fx_stop(FXW(k, j));
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
/* CARSFX_Skids::SetupLoadData 0x465b90, every car (the player too): the
   bank 0x7ed1f8 [(0x458f00(EAXCar +0x8c row) == 2) + 2 x drift], drift =
   0x89e7d9 (a drift race) || 0x890104 (0 in the port), stored 0x82bc2c */
static const char *const k_skid_bank[4] = { "SKID_PAV", "SKID_PAV2", "SKIDS_DRIFT", "SKIDS_DRIFT2" };
static int g_prow = -1, g_in_begin;     /* the player's audio row (carsfx_player_car) */
static const char *g_pskid;             /* the player's bank asked for */
static void player_skid_stop(void);
static void player_skid_sync(int hold)
{
    if (g_prow < 0) return;
    const char *b = k_skid_bank[(engine_class(g_prow) == 2) + 2 * (g_drift != 0)];
    if (b == g_pskid) return;
    g_pskid = b;
    if (hold) audio_hold(1);
    player_skid_stop();                                 /* its voices play AFX_SKID; restarts on the new bank (player_skid) */
    audio_set_skid_bank(b);
    if (hold) audio_hold(0);
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

/* 0x463f60: at most three engine banks for the whole field; a car's own
   slot = its EAXCar +0x7c = the 0x7f2230 record at row x 4 + its engine
   sound level +0x94 (CarPreSetup 0x4593b0; audio_row_engine_slot) */
static int pick_banks(const int *rows, int n, int *carslot, int pick[3])
{
    int cls[CARSFX_AI_MAX], own[CARSFX_AI_MAX], present[3] = { 0, 0, 0 }, cnt = 0;
    pick[0] = pick[1] = pick[2] = -1;
    for (int k = 0; k < n; k++) {
        int r = rows[k] < 0 || rows[k] > 32 ? 18 : rows[k];
        cls[k] = engine_class(r); own[k] = audio_row_engine_slot(r, g_aielv[k]); carslot[k] = -1;
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
    g_in_begin = 1;
    carsfx_race_end();
    g_in_begin = 0;
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
        a->slot = carslot[k] >= 0 && carslot[k] < 43 ? carslot[k] : 0;
        a->bank = -1;
        for (int j = 0; j < nb; j++) if (bank_slot[j] == carslot[k]) a->bank = j;
        g_ndetune = (g_ndetune + 100) % 600;           /* 0x473ce0 */
        a->detune = g_ndetune - 300;
        /* the effects: the turbo record of the car's row at its turbo level
           (0x45eb30: 0x5995e0(4) of EAXCar +0x74 = the DriverInfo that
           CarPreSetup 0x4593b0 got, whose +0x300 = RideInfo +0x2f0 = the
           RidePhysicsInfo levels MatchAICarPerformanceToPlayer 0x532010
           fitted -- the same levels the shift flame 0x5f5f5c reads; G4
           11-01, it read level 0), the nitrous bank by its nitrous level
           (0x45f210 / 0x465830), the stereo roll (every 6th AI car,
           0x4765f0; 0x476580 rolls the variant again) */
        AiFx *x = &g_aifx[k];
        memset(x, 0, sizeof *x);
        x->t_rec = audio_row_turbo(rows[k], g_aiperf[k][0]);
        x->n_bank = g_aiperf[k][1] == 2 || g_aiperf[k][1] == 3;   /* 0x7ed1a0 index */
        x->k_bank = engine_class(rows[k] < 0 || rows[k] > 32 ? 18 : rows[k]) == 2;   /* 0x465b90 */
        x->t_set = -1;
        for (int j = 0; j < g_naitb; j++) if (g_aitb[j] == x->t_rec) x->t_set = j;
        if (x->t_set < 0 && g_naitb < CARSFX_AI_MAX) { x->t_set = g_naitb; g_aitb[g_naitb++] = x->t_rec; }
        x->s_var = 8 + (int)rng_int(2);
        g_stereo_n = (g_stereo_n + 1) % 6;
        x->s_on = g_stereo_n == 0;
        x->s_var = 8 + (int)rng_int(2);
        printf("car sounds: opponent %d audio row %d, engine sound level %d (own slot %d) -> engine slot %d CAR_%02d (bank %d), "
               "turbo level %d -> turbo bank %d, nitrous level %d -> Nitrous_0%d, skids %s\n",
               k, rows[k], g_aielv[k], audio_row_engine_slot(rows[k], g_aielv[k]), a->slot, audio_slot_file(a->slot), a->bank,
               g_aiperf[k][0], x->t_rec, g_aiperf[k][1], x->n_bank, k_skid_bank[x->k_bank + 2 * (g_drift != 0)]);
    }
    for (int i = 0; i < CARSFX_TR_MAX; i++) g_trs[i].car = -1;
    ai_patch_begin();
    player_skid_sync(1);                                /* the player's bank: the race's drift flag */
    return nb;
}

int carsfx_ai_bank(int k) { return k >= 0 && k < g_nai ? g_ai[k].bank : -1; }

static void planes_reset(void);
static void emitters_reset(void);
static void part_snd_reset(void);
void carsfx_race_end(void)
{
    part_snd_reset();
    g_nai = 0;
    memset(g_ai, 0, sizeof g_ai);
    for (int i = 0; i < CARSFX_TR_MAX; i++) g_trs[i].car = -1;
    for (int v = 0; v < AUDIO_CARVOICES; v++) g_carvoice[v].on = 0;
    for (int v = 0; v < AUDIO_FXVOICES; v++) g_fxvoice[v].on = 0;
    memset(g_aifx, 0, sizeof g_aifx);
    for (int k = 0; k < CARSFX_AI_MAX; k++) ai_patch_stop(k);
    player_skid_stop();
    weather_stop();
    planes_reset();
    emitters_reset();
    memset(g_trfx, 0, sizeof g_trfx);
    co_reset();
    g_drift = 0;                                        /* out of the race: not a drift race */
    if (!g_in_begin) player_skid_sync(1);
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

/* ---- CARSFX_RoadNoise on the player's car (type 0x10060: map class 1
   object 0x40010060, channels 0 right loop, 1 left loop, 2 / 3 right / left
   transition, 4 / 5 the base loops). Voices g_fxvoice 302.. ---- */
#define FXR(j) (&g_fxvoice[302 + (j)])
enum { FR_LOOP_R, FR_LOOP_L, FR_BASE_R, FR_BASE_L, FR_TRANS_R, FR_TRANS_L };
/* RoadNoiseVolumes 0x7ecf4c, by loop */
static const int k_rn_vol[8] = { 15000, 24000, 32000, 24000, 24000, 5000, 19000, 32500 };
/* RoadNoiseVolGraph 0x82c69c (static init 0x77c460: 5 points at 0x7f09b0), mph -> vol */
static const float k_rn_graph[5][2] = { { 0, 0 }, { 60, 28000 }, { 100, 32500 }, { 150, 24500 }, { 175, 18000 } };
typedef struct { float y0, y1, x0, x1; } RnSlope;     /* Slope::GetValue 0x4599e0 */
static const RnSlope k_rn_pitch = { 1500, 4500, 0, 100 },      /* RoadNoiseSpeedToPitch 0x7f09d8 */
                     k_rn_tvol = { 20000, 32767, 12, 60 },     /* RoadNoiseTransitionVolSlope 0x7f0978 */
                     k_rn_tpitch = { 3600, 4400, 12, 60 };     /* RoadNoiseTransitionPitchSlope 0x7f0994 */
static float rn_slope(const RnSlope *s, float x)
{
    float t = (x - s->x0) / (s->x1 - s->x0);
    t = t > 0 ? (t < 1 ? t : 1) : 0;
    return s->y0 + (s->y1 - s->y0) * t;
}
static float rn_graph(float x)                         /* Graph::GetValue 0x576790 */
{
    if (x <= k_rn_graph[0][0]) return k_rn_graph[0][1];
    if (x >= k_rn_graph[4][0]) return k_rn_graph[4][1];
    int i = 0;
    while (i < 3 && x > k_rn_graph[i + 1][0]) i++;
    return k_rn_graph[i][1] + (x - k_rn_graph[i][0]) * (k_rn_graph[i + 1][1] - k_rn_graph[i][1]) /
                              (k_rn_graph[i + 1][0] - k_rn_graph[i][0]);
}
static int rn_loop(int t)                              /* 0x45f940: terrain -> loop / volume index */
{
    switch (t) {
    case 1: case 3: return 5;
    case 2: case 0xd: return 4;
    case 7: case 0x1a: return 7;
    case 8: case 10: case 0xf: return 0;
    case 0xb: case 0xc: case 0x11: return 2;
    case 0xe: return 3;
    case 0x10: return 1;
    case 0x19: return 6;
    default: return -1;
    }
}
static int rn_trans(int t) { return t == 0xb || t == 0x10 ? 0 : t == 0xe ? 2 : -1; }   /* 0x45f9d0 */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static struct {
    int on, terr[2], prev[2], loop[2];   /* side 0 left (wheel ctl +0xb0, FL/RL), 1 right (+0xb4, FR/RR) */
    int vol[2], pitch[2];                /* +0x100 / +0xfc, +0x108 / +0x104 */
    float logt;
} g_rn;
static Pos3D g_rn_p3;
/* AUD_RoadTransitions (PlayTransition 0x4733aa / 0x47353d, GC
   8009cdd8): with each transition, unless the side's last one still plays,
   collision stitch 0x4e + (0..3 in turn, a counter per side: 0x82cb48 left,
   0x82cb44 right) -- the bottom-out landing ones -- played at 0x7fff /
   0x1000 (0x46cdd0, GC cStichWrapper::Play); ProcessUpdate 0x47cdb0 updates
   it on channel 7 (left, +0xd4) / 6 (right, +0xd0) and frees it once it
   ends. Voices 579 + 4 side (at most 3 samples) */
#define FXRS(s) (&g_fxvoice[579 + 4 * (s)])
static StitchPlay g_rn_st[2];
static int g_rn_st_on[2];
static unsigned g_rn_st_n[2];
static void rn_stop(void)
{
    for (int j = 0; j < 6; j++) fx_stop(FXR(j));
    for (int s = 0; s < 2; s++) { stitch_stop(FXRS(s), &g_rn_st[s]); g_rn_st_on[s] = 0; }
    memset(&g_rn, 0, sizeof g_rn);
}
/* PlayTransition 0x4731c0: a one-shot of FX_ROADNOISE_TRANS variant `type`
   (clips 11..13) on side s (0 left: channel 3, 1 right: channel 2), volume
   by type and speed, pitch by speed, then the side's AUD_RoadTransitions
   stitch (above). */
static void rn_transition(int type, int s, float mph, int mode, float ref)
{
    if (!g_rn_st_on[s]) {
        unsigned n = g_rn_st_n[s] % 4;
        g_rn_st_n[s] = n + 1;
        g_rn_st_on[s] = stitch_start(FXRS(s), 4, 0, 0x4e + (int)n, &g_rn_st[s]) > 0;
        printf("[road] side %c stitch 0x%x (%d samples)\n", s ? 'R' : 'L', 0x4e + n, g_rn_st[s].n);
    }
    int base = type == 0 || type == 1 ? 12000 : type == 2 || type == 3 ? 0x5fb4 : 0x7fff;
    int v = base * ftol(rn_slope(&k_rn_tvol, mph)) >> 15;
    v = mix_objvol(1, 0x40010060u, s ? 2 : 3, mode, g_rn_p3.d, g_rn_p3.az) * v >> 15;
    AudioFxVoice *fv = FXR(s ? FR_TRANS_R : FR_TRANS_L);
    int clip = 11 + clampi(type, 0, 15);
    if (clip > 13) { fx_stop(fv); return; }   /* the table holds 3 variants */
    fx_start(fv, AFX_ROAD, clip, 0, 0, (float)clampi(ftol(rn_slope(&k_rn_tpitch, mph)), 0, 0x1fff) / 4096.0f);
    fv->gain = fx_gain(clampi(v, 0, 0x7fff), ref, 0.6f);
    fv->pan = az_pan((unsigned)clampi((int)mix_objaz(1, 0x40010060u, s ? 2 : 3, mode, g_rn_p3.az), 0, 0xffff));   /* member +0x10: vt+0x3c(channel), once */
    printf("[road] transition %d side %c clip %d vol %d mph %.0f\n", type, s ? 'R' : 'L', clip, v, mph);
}
static void player_road(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll || !L->pwheels) { if (g_rn.on) rn_stop(); return; }
    if (!g_rn.on || !FXR(FR_BASE_L)->on) {   /* InitSFX 0x472e50: the two base loops, FX_ROADNOISE variant 4
                                                (carsfx_race_end stops every voice) */
        memset(&g_rn, 0, sizeof g_rn);
        memset(&g_rn_p3, 0, sizeof g_rn_p3);
        g_rn.on = 1; g_rn.loop[0] = g_rn.loop[1] = -1;
        fx_start(FXR(FR_BASE_R), AFX_ROAD, 4, 1, 0, 1);
        fx_start(FXR(FR_BASE_L), AFX_ROAD, 4, 1, 0, 1);
    }
    /* MAPOUTPUT map 0, object 0x40010060: master 7 (level 1625) feeds slots
       0..3, master 9 (1075) slots 6/7 and master 8 slots 4/5 -- the base
       loops -- at level 10000 (-100 dB). Its input 0xb2013005 = class-1 mix
       event 5 (range +10000, A/R 0x91a3), triggered by 0x40010092 =
       CARSFX_Rain (0x10090) field 2, set by 0x461890 while the rain plays:
       the base loops are the wet-road layer, silent while dry (md_src
       0x40010092, player_weather) */
    pos3d_update(&g_rn_p3, L->car, L, dt);
    /* SFXCTL_Wheel (0x8008f9d4): slip vectors summed per side over the
       wheels on the ground (+0x90 = FR+RR, +0x98 = FL+RL), friction use
       |car +0x3d4| per wheel (+0xa0..+0xac); terrain per side
       (GenerateTerrainTypes 0x8008fb08: kept while the side's wheels disagree) */
    float sv[2][2] = { { 0, 0 }, { 0, 0 } };   /* [0] left, [1] right */
    for (int w = 0; w < 4; w++) {
        if (!L->pcontact[w]) continue;
        int sd = w == 1 || w == 2;
        sv[sd][0] += L->pslipv[w][0]; sv[sd][1] += L->pslipv[w][1];
    }
    float fr[4];
    for (int w = 0; w < 4; w++) fr[w] = fabsf(L->pfric[w]);
    int down[2] = { L->pcontact[0] || L->pcontact[3], L->pcontact[1] || L->pcontact[2] };   /* +0xe8 / +0xe9 */
    int nt[2] = { L->psurface[0] == L->psurface[3] ? L->psurface[0] : g_rn.terr[0],
                  L->psurface[1] == L->psurface[2] ? L->psurface[1] : g_rn.terr[1] };
    /* GenerateRoadNoise 0x4735e0 */
    float mph = L->pspeed * 2.237f;
    float A = (float)(ftol(rn_graph(mph)) * 0x7fff >> 15), P = rn_slope(&k_rn_pitch, mph);
    float mL = fminf(hypotf(sv[0][0], sv[0][1]) * 0.01f, 0.15f), mR = fminf(hypotf(sv[1][0], sv[1][1]) * 0.01f, 0.15f);
    float vL = (1 + mL) * A * (1 + fminf((fr[3] + fr[0]) * 0.5f * 1.0f * 0.1f, 0.1f));
    float vR = (1 + 0.1f) * (1 + mR) * A;   /* the right side's bump factor is the constant 1 + 0.1 */
    float pL = (1 + fminf(hypotf(sv[0][0], sv[0][1]) * 0.01f, 0.2f)) * P * (1 + fminf((fr[3] + fr[0]) * 0.5f * 0.15f, 0.15f));
    float pR = (1 + fminf(hypotf(sv[1][0], sv[1][1]) * 0.01f, 0.2f)) * P * (1 + fminf((fr[2] + fr[1]) * 0.5f * 0.15f, 0.15f));
    g_rn.vol[0] = ftol(vL > 32000 ? 32000 : vL); g_rn.vol[1] = ftol(vR > 32000 ? 32000 : vR);
    g_rn.pitch[0] = ftol(pL > 6000 ? 6000 : pL); g_rn.pitch[1] = ftol(pR > 6000 ? 6000 : pR);
    /* ProcessUpdate 0x47cdb0: a side whose terrain changed restarts its loop
       (loop -1 releases it) and plays a transition: leaving terrain 0xb / 0x10
       = 1, else entering one = rn_trans */
    for (int sd = 0; sd < 2; sd++) {
        if (nt[sd] == g_rn.terr[sd]) continue;
        g_rn.prev[sd] = g_rn.terr[sd]; g_rn.terr[sd] = nt[sd];
        int lp = rn_loop(nt[sd]), tr = rn_trans(nt[sd]);
        int out = g_rn.prev[sd] != 1 && (g_rn.prev[sd] == 0xb || g_rn.prev[sd] == 0x10) ? 1 : -1;
        AudioFxVoice *fv = FXR(sd ? FR_LOOP_R : FR_LOOP_L);
        g_rn.loop[sd] = lp;
        /* the loops' variant table (+0xe18) holds 7: loop 7 (terrain 7 / 0x1a) has no sample here (GAPS) */
        if (lp < 0 || lp > 6) fx_stop(fv);
        else fx_start(fv, AFX_ROAD, lp, 1, 0, 1);
        printf("[road] side %c terrain %d -> %d loop %d\n", sd ? 'R' : 'L', g_rn.prev[sd], nt[sd], lp);
        if (down[sd] && (out != -1 || tr != -1)) rn_transition(out != -1 ? out : tr, sd, mph, L->mode, ref);
    }
    const unsigned out = 0x40010060u;
    for (int sd = 0; sd < 2; sd++) {   /* 0 left: loop channel 1, base 5; 1 right: 0, 4 */
        AudioFxVoice *lv = FXR(sd ? FR_LOOP_R : FR_LOOP_L), *bv = FXR(sd ? FR_BASE_R : FR_BASE_L);
        int li = g_rn.loop[sd];
        int v = li >= 0 && li < 8 ? mix_objvol(1, out, sd ? 0 : 1, L->mode, g_rn_p3.d, g_rn_p3.az) *
                                    (k_rn_vol[li] * g_rn.vol[sd] >> 15) >> 15 : 0;
        if (!down[sd]) v = 0;
        lv->gain = fx_gain(clampi(v, 0, 0x7fff), ref, 0.6f);
        lv->pitch = (float)clampi(g_rn.pitch[sd], 0, 0x1fff) / 4096.0f;
        /* the mono players' azimuths: vt+0x3c of each side's channel (0x47cdb0) */
        lv->pan = az_pan(mix_objaz(1, out, sd ? 0 : 1, L->mode, g_rn_p3.az));
        bv->pan = az_pan(mix_objaz(1, out, sd ? 4 : 5, L->mode, g_rn_p3.az));
        /* the base loops both take the left volume (+0x100) x RoadNoiseVolumes[4] */
        int b = mix_objvol(1, out, sd ? 4 : 5, L->mode, g_rn_p3.d, g_rn_p3.az) * (g_rn.vol[0] * 24000 >> 15) >> 15;
        bv->gain = fx_gain(clampi(b, 0, 0x7fff), ref, 0.6f);
        bv->pitch = lv->pitch;
        AudioFxVoice *tv = FXR(sd ? FR_TRANS_R : FR_TRANS_L);
        if (tv->on && tv->done) fx_stop(tv);
        if (g_rn_st_on[sd]) {   /* channel 7 left, 6 right */
            int ch = sd ? 6 : 7;
            g_rn_st_on[sd] = stitch_update(FXRS(sd), &g_rn_st[sd], mix_objvol(1, out, ch, L->mode, g_rn_p3.d, g_rn_p3.az),
                                           mix_objaz(1, out, ch, L->mode, g_rn_p3.az), ref);
            if (!g_rn_st_on[sd]) stitch_stop(FXRS(sd), &g_rn_st[sd]);
        }
    }
}

/* ---- CARSFX_BottomOut on the player's car (type 0x100b0: class 1 object
   0x400100b0). UpdateParams 0x47ed80 / ProcessUpdate 0x47f100 are the
   traffic bottom-out's own (the same vtable slots): touchdowns -> stitch
   0x4e.. / 0x56.. (LandJumpPlay 0x47ea30, voice +0xb4, channel 0). The
   strength it passes (the wheels' travel past 0.4 x 31.75) only feeds
   n - 4 * (int)(x * -1/128), always n, so it is not computed. Event 5
   (0x5f5d8f: bump-stop impulses over the surface range, x 32767; handler
   0x469bc0 keeps param >> 8) -> BottomOutPlay 0x47ec60: stitch 0x4e + (0..3
   in turn, its own counter 0x82cbfc) on voice +0xb0 unless that one still
   plays. ---- */
#define FXB(j) (&g_fxvoice[308 + (j)])
#define FXZ(j) (&g_fxvoice[336 + (j)])
enum { FB_LAND = 0, FB_EVT = 13, FB_END = 26 };
static struct { int on, down[4], tilt, l_on, e_on, z_on; float air[4]; StitchPlay l_sp, e_sp, z_sp; } g_pbo;
static unsigned g_bo_evt_n, g_bo_zone_n;                /* 0x82cbfc, 0x82cbf0 */
static Pos3D g_pbo_p3;
static void player_bottom(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll || !L->pwheels) {
        if (g_pbo.on) { stitch_stop(FXB(FB_LAND), &g_pbo.l_sp); stitch_stop(FXB(FB_EVT), &g_pbo.e_sp); stitch_stop(FXZ(0), &g_pbo.z_sp); }
        memset(&g_pbo, 0, sizeof g_pbo);
        return;
    }
    if (!g_pbo.on) { memset(&g_pbo, 0, sizeof g_pbo); memset(&g_pbo_p3, 0, sizeof g_pbo_p3); g_pbo.on = 1; }
    pos3d_update(&g_pbo_p3, L->car, L, dt);
    float now = g_fxtime;
    if (L->pbottom > 0 && !g_pbo.e_on) {                 /* 0x47ec60 */
        unsigned n = g_bo_evt_n % 4;
        g_bo_evt_n = n + 1;
        g_pbo.e_on = stitch_start(FXB(FB_EVT), FB_END - FB_EVT, 0, 0x4e + (int)n, &g_pbo.e_sp) > 0;
        g_md_bo[1] = 1;                                 /* +8: BottomOutPlay started */
        printf("[bottom] event 5 param %d -> stitch %d\n", L->pbottom >> 8, 0x4e + (int)n);
    }
    const int *w = L->pcontact;
    int c0 = !!w[0], c1 = !!w[1], c2 = !!w[2], c3 = !!w[3];
    int pair[4] = { c0 && c1, c2 && c3, c1 && c2, c3 && c0 };
    int down = c0 + c1 + c2 + c3;
    if (!g_pbo.tilt && L->pupz < 0.9f && down < 4) g_pbo.tilt = 1;
    int land = (pair[0] && g_pbo.air[0] > 0.05f && !g_pbo.down[0]) || (pair[1] && g_pbo.air[1] > 0.05f && !g_pbo.down[1]);
    int mode = -1;
    if (!g_pbo.tilt) { if (land) mode = 0; }
    else if ((pair[0] && !g_pbo.down[0]) || (pair[1] && !g_pbo.down[1]) ||
             (pair[2] && !g_pbo.down[2]) || (pair[3] && !g_pbo.down[3])) mode = 1;
    else if (land) mode = 0;
    if (mode >= 0) {                                    /* 0x47ea30 */
        stitch_stop(FXB(FB_LAND), &g_pbo.l_sp);
        unsigned n = g_bo_n[mode] % 4;
        g_bo_n[mode] = n + 1;
        g_pbo.l_on = stitch_start(FXB(FB_LAND), FB_EVT - FB_LAND, 0, k_bo_first[mode] + (int)n, &g_pbo.l_sp) > 0;
        g_md_bo[0] = 1;                                 /* +4: LandJumpPlay (0x47ea30) */
        printf("[bottom] land mode %d -> stitch %d\n", mode, k_bo_first[mode] + (int)n);
        if (L->pjumpcam) {                              /* view +0x58 == 6: stitch 0x2e.. on +0xb8, channel 2 */
            stitch_stop(FXZ(0), &g_pbo.z_sp);
            unsigned z = g_bo_zone_n % 4;
            g_bo_zone_n = z + 1;
            g_pbo.z_on = stitch_start(FXZ(0), 13, 0, k_bo_first[2] + (int)z, &g_pbo.z_sp) > 0;
        }
    }
    if (down == 4 && g_pbo.tilt) g_pbo.tilt = 0;
    for (int p = 0; p < 4; p++) {
        if (!pair[p]) {
            if (g_pbo.down[p]) { g_pbo.down[p] = 0; g_pbo.air[p] = now; }
            g_pbo.air[p] += dt;
        } else g_pbo.down[p] = 1;
    }
    int ov = mix_objvol(1, 0x400100b0u, 0, L->mode, g_pbo_p3.d, g_pbo_p3.az);   /* 0x47f100: channel 0 */
    unsigned oa = mix_objaz(1, 0x400100b0u, 0, L->mode, g_pbo_p3.az);
    if (g_pbo.l_on && !(g_pbo.l_on = stitch_update(FXB(FB_LAND), &g_pbo.l_sp, ov, oa, ref))) stitch_stop(FXB(FB_LAND), &g_pbo.l_sp);
    if (g_pbo.e_on && !(g_pbo.e_on = stitch_update(FXB(FB_EVT), &g_pbo.e_sp, ov, oa, ref))) stitch_stop(FXB(FB_EVT), &g_pbo.e_sp);
    if (g_pbo.z_on && !(g_pbo.z_on = stitch_update(FXZ(0), &g_pbo.z_sp,
                                                   mix_objvol(1, 0x400100b0u, 2, L->mode, g_pbo_p3.d, g_pbo_p3.az),
                                                   mix_objaz(1, 0x400100b0u, 2, L->mode, g_pbo_p3.az), ref)))
        stitch_stop(FXZ(0), &g_pbo.z_sp);
}

/* ---- EAXCommon::PlayCarPartSnd 0x481500 (GC 8007e864): the in-game sound
   of an opening / closing hood, trunk or door (0x61f820 with EAXSound +0x90
   != 1, i.e. outside the front end). It builds a cStichWrapper (0x45b1f0)
   from collision stitch list 0 entry `type` (0x43be60 on EAXSound +0xd0:
   door close 0 / open 1, hood close 2 / open 3, trunk close 4 / open 5) into
   the first free of 2 slots (+0x28, +0x2c; both busy -> nothing) and plays
   it (0x46cdd0) with the params block zeroed (+8 volume 0, +0xc, +0x10).
   EAXCommon::Update 0x4815b0 (GC 8007e8fc) runs its loop twice but on slot
   +0x28 both times (retail bug, GC the same): slot 0 is freed once its
   stitch ends (+0x1c == 0) and otherwise updated (0x47bb30) with volume =
   the attached object's GetQ15Output(4) (vt+0x38, 0x45e390), azimuth 0.
   The attached object is SFXObj_FEHUD (main type 6, InitSFX 800a3e58 ->
   SetSFXBaseObject -> AttachSFXOBJ 8007e47c): mixmap class 6, out
   0x40060000. Slot +0x2c is never updated nor freed: it keeps volume 0. In
   all four shipped mixmaps that channel feeds slots 0 / 3 / 5 only, so
   slot 4 reads 0 and the stitches are inaudible; the port runs the same
   path (fx_load loads their clips, so a slot ends with its samples).
   Voices 523.. (2 slots x 4 samples, the longest of stitches 0..5). ---- */
#define FXCP(s, j) (&g_fxvoice[523 + (s) * 4 + (j)])
static struct { int on; StitchPlay sp; } g_cps[2];
void carsfx_part_snd(int type)
{
    int s = 0;
    if (g_cps[0].on) { s = 1; if (g_cps[1].on) return; }
    g_cps[s].on = stitch_start(FXCP(s, 0), 4, 0, type, &g_cps[s].sp) > 0;   /* fx_start: gain 0 */
}
static void part_snd_reset(void)
{
    for (int s = 0; s < 2; s++) { if (g_cps[s].on) stitch_stop(FXCP(s, 0), &g_cps[s].sp); g_cps[s].on = 0; }
}
static void part_snd_update(const CarSfxListener *L, float ref)
{
    static const int d0[2] = { 0, 0 }; static const unsigned a0[2] = { 0, 0 };
    if (!g_cps[0].on) return;
    int live = 0;
    for (int k = 0; k < g_cps[0].sp.n; k++) live |= FXCP(0, k)->on && !FXCP(0, k)->done;
    if (!live) { stitch_stop(FXCP(0, 0), &g_cps[0].sp); g_cps[0].on = 0; return; }   /* +0x1c == 0: delete */
    stitch_update(FXCP(0, 0), &g_cps[0].sp, mix_objvol(6, 0x40060000u, 4, L->mode, d0, a0), 0, ref);
}

/* ---- CARSFX_SparkChatter (PC vt 0x78acb0): the CSIS class CAR_Sputter of
   the player's SOUND/ENGINE/SWTN_CAR_xx_MB.abk (SetupSFX 0x45f6e0: file and
   volume scale +8 from the table 0x7f3040 by the engine slot CSTATE +0x7c).
   The class is an EA AEMS patch: x86 code (module descriptor = class entry
   of the header +0x38 table + 0x20: +4 code, +8 instance template, +0xc
   size, +0x18.. its players' template offsets) that only calls operators
   by index (the call displacement; table 0x817fe0) on its state (template
   + 0x18) and moves their results between fields. It is run here by
   reading that code: call / load eax / store eax / store byte / add esi.
   Member data (ProcessUpdate 0x45f800, InitSFX 0x485130) from state +0x28:
   engine slot index (record 0x7f0de0 +8), this, rpm, volume (Q15 output 0
   x table +8 >> 15), azimuth, 0, 0, throttle x 10.24, the event 9 toggle
   (UpdateParams 0x45f780: gear N or first only), 0, CSTATE +0xb0 +0x78 set;
   the byte after it is set by each SetMemberData. The patch ticks every
   gVariableTimerPeriod = 20 ms (0x724c20: -1 -> 20). Its players are
   bank players (SNDAEMSI_playerplaybank) of the abk's own samples, options
   pitchmult (0x1000 = 1), vol (0..0x7fff), azimuth (the object's slot-0
   azimuth word, member +0x10, -> az_pan); its ControlClass output (CAR_SputOutput -> 0x47ccd0 -> +0xf8 ->
   UpdateMixerOutputs 0x45f760) is a mixer input the port's mixmap does
   not decode (GAPS). ---- */
enum { AE_CALL, AE_LOAD, AE_STORE, AE_SETB, AE_ADD, AE_MUL, AE_MIN, AE_MAX };
typedef struct { unsigned char k, op; short imm; int at; } AeIns;
#define AE_MAXINS 640
/* one patch instance; the interpreter runs the one g_aep points at (g_sp
   below = *g_aep): CAR_Sputter (fx voices 334/335), a CAR_SWTN instance
   (one player: 349 disengage / 350 engage; file is CAR_Sputter's, shared)
   or an AI car's FX_TURBO / FX_NITROUS / FX_SKID instance (351 + 15 k ..).
   Its players k play fx voice vbase + k from sound set `set`. */
typedef struct {
    unsigned char *file; long flen;
    unsigned char *mem; int msize;
    const AeIns *ins; int nins;
    int ok, dead, idx, vscale, row, nply, ply[8], child, out, toggle, flag, set;
    unsigned loops;
    float tick;
    int vbase;
} AeCtx;
static AeIns g_sp_ins[AE_MAXINS], g_sw_ins[AE_MAXINS];
static AeCtx g_sp_ctx = { .vbase = 334, .set = AFX_SWTN }, g_sw_ctx[4] = { { .vbase = 349, .set = AFX_SWTN }, { .vbase = 350, .set = AFX_SWTN },
                                                                  { .vbase = 577, .set = AFX_SWTN }, { .vbase = 578, .set = AFX_SWTN } },
             *g_aep = &g_sp_ctx;
#define g_sp (*g_aep)
#define SPV(j) (&g_fxvoice[g_sp.vbase + (j)])
static Pos3D g_sp_p3;
static unsigned g_sp_rnd[6];                              /* iSNDrandom state */

static int ae_i(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static void ae_w(unsigned char *p, int v) { memcpy(p, &v, 4); }
static unsigned short ae_h(const unsigned char *p) { unsigned short v; memcpy(&v, p, 2); return v; }
static void ae_wh(unsigned char *p, unsigned short v) { memcpy(p, &v, 2); }
static float ae_f(const unsigned char *p) { float v; memcpy(&v, p, 4); return v; }
static void ae_wf(unsigned char *p, float v) { memcpy(p, &v, 4); }
static int ae_round(float x) { return (int)lrintf(x); }   /* fistp, round to nearest */

static unsigned ae_rand(void)                             /* iSNDrandom (GC 802dcf30) */
{
    unsigned *r = g_sp_rnd;   /* 0 seed, 1..4 = DAT_80430b08..b14, 5 = b18 */
    unsigned o1 = r[1], o2 = r[2], o3 = r[3];
    unsigned c = 0, u2 = r[5] + r[4];
    if (u2 < r[5] || u2 < r[4]) c = 1;
    r[4] = u2;
    unsigned u3 = u2 + r[3] + c;   r[3] = u3;
    unsigned u4 = u3 + r[2] + (u3 < o3); r[2] = u4;
    unsigned u5 = u4 + r[1] + (u4 < o2); r[1] = u5;
    unsigned x = u5 + r[0] + (u5 < o1);
    r[5]++; r[0] = x;
    if (r[5]) return x;
    if (++r[4]) return x;
    if (++r[3]) return x;
    if (++r[2]) return x;
    if (++r[1]) return x;
    return r[0] = x + 1;
}

static int ae_player_no(int at)
{
    for (int p = 0; p < g_sp.nply && p < 8; p++) if (at == g_sp.ply[p]) return p;
    return -1;
}

/* SNDAEMSI_updateplayer (0x720d57) with the bank player's play / update /
   stop (playerplaybank, playerupdatebank, playerstopbank) on an fx voice */
static int ae_player(unsigned char *s, int at, float ref)
{
    int p = ae_player_no(at);
    AudioFxVoice *v = p >= 0 ? SPV(p) : NULL;
    int st = ae_i(s + 0x18);
    st = st < 0 ? 0 : st > 2 ? 2 : st;
    signed char slot = (signed char)s[0x10];
    int nopt = s[0xe];
    if (st != (signed char)s[0xc]) {
        if (st == 0) {
            if (slot >= 0) { if (v) fx_stop(v); s[0x10] = 0xff; }
        } else if (st == 1) {
            if (slot >= 0) { if (v) v->on = 1; }
            else if (!((signed char)s[0xc] == 2 && s[0xd] == 1)) {
                const unsigned char *lst = g_sp.file + ae_i(s + 4);
                int n = ae_i(lst), k = ae_i(s + 0x14);
                k = k >= n ? n - 1 : k < 0 ? 0 : k;
                const unsigned char *e = lst + 4 + 12 * k;
                s[0x10] = e[0];
                int clip = ae_i(e + 4), vol = 0x7fff, pm = 0x1000, az = 0;
                for (int o = 0; o < nopt; o++) {
                    unsigned char *op = s + 0x1c + 12 * o;
                    int x = ae_i(op + 8);
                    if (op[0] == 0) pm = x < 0 ? 0 : x > 0xffff ? 0xffff : x;
                    else if (op[0] == 2) vol = x < 0 ? 0 : x > 0x7fff ? 0x7fff : x;
                    else if (op[0] == 3) az = x;
                    ae_w(op + 4, x);
                }
                if (v && e[0] == 0 && clip > 0 && clip < 32) {   /* bank sound (0 = the null entry) */
                    fx_start(v, g_sp.set, clip, audio_fx_loop(g_sp.set, clip), 0.0f, (float)pm / 4096.0f);
                    v->gain = fx_gain(vol, ref, 0.6f);
                    v->pan = az_pan((unsigned)az);
                    ae_w(s + 8, 1);
#ifdef OPENUG2_SIM_PRE_H
                    if (g_sp.vbase < 351 || getenv("OPENUG2_AIPATCH_LOG"))   /* sim test aid */
                        printf("[patch %d] player %d sound %d pitch %d vol %d\n", g_sp.vbase, p, clip, pm, vol);
#endif
                } else { ae_w(s + 8, -1); s[0x10] = 0xff; }
            }
        } else if (slot >= 0 && v) v->on = 0;            /* pause */
        s[0xd] = s[0xc];
        s[0xc] = (unsigned char)st;
    }
    int r = st;
    if (st == 1 && (signed char)s[0x10] >= 0) {          /* playerupdatebank */
        if (!v || v->done || !v->on) { s[0x10] = 0xff; r = 0; }
        else {
            for (int o = 0; o < nopt; o++) {
                unsigned char *op = s + 0x1c + 12 * o;
                int x = ae_i(op + 8);
                if (x == ae_i(op + 4)) continue;
                ae_w(op + 4, x);
                if (op[0] == 0) v->pitch = (float)(x < 0 ? 0 : x > 0xffff ? 0xffff : x) / 4096.0f;
                else if (op[0] == 2) v->gain = fx_gain(x < 0 ? 0 : x > 0x7fff ? 0x7fff : x, ref, 0.6f);
                else if (op[0] == 3) v->pan = az_pan((unsigned)x);
            }
            r = 1;
        }
    }
    return (signed char)s[0x10] < 0 ? 0 : r;
}

static int ae_op(int op, unsigned char *s, int at, float ref)
{
    switch (op) {
    case 0: { int r = ae_i(s + 0x10); ae_w(s + 0x10, 0); return r; }   /* ClassDestructor */
    case 1: return ae_i(s + 0x14);                                      /* ClassData */
    case 4:                                                             /* destroy */
        if (ae_i(s + 0xc)) {
            g_sp.dead = 1;
            for (int p = 0; p < g_sp.nply; p++) fx_stop(SPV(p));
        }
        return 0;
    case 6: {                                                           /* counter */
        int in = ae_i(s + 0x14);
        if (in >= ae_i(s) && in <= ae_i(s + 4)) return in;
        if (ae_i(s + 0x10) > 0) {
            int c = ae_i(s + 8) + (signed char)s[0xc];
            ae_w(s + 8, c > ae_i(s + 4) ? ae_i(s) : c < ae_i(s) ? ae_i(s + 4) : c);
        }
        return ae_i(s + 8);
    }
    case 7:                                                             /* random */
        if (ae_i(s + 0xc)) {
            unsigned m = (unsigned)ae_i(s + 4);
            ae_w(s + 8, (int)(m ? ae_rand() % m : 0) + ae_i(s));
        }
        return ae_i(s + 8);
    case 8: {                                                           /* randomshuffle */
        if (!ae_i(s + ae_h(s))) return ae_i(s + 0xc);
        unsigned lo = ae_h(s + 8), rng = (unsigned)(ae_h(s + 0xa) - (signed char)s[3]) - lo;
        unsigned k = (rng ? ae_rand() % rng : 0) + lo;
        if (s[2] == 1) {
            ae_w(s + 0xc, s[0x10 + k]);
            s[0x10 + k] = s[0x10 + lo];
            s[0x10 + ae_h(s + 8)] = s[0xc];
        } else {
            ae_w(s + 0xc, ae_h(s + 0x10 + 2 * k));
            ae_wh(s + 0x10 + 2 * k, ae_h(s + 0x10 + 2 * lo));
            ae_wh(s + 0x10 + 2 * ae_h(s + 8), ae_h(s + 0xc));
        }
        ae_w(s + 0xc, ae_i(s + 0xc) + ae_i(s + 4));
        ae_wh(s + 8, (unsigned short)(ae_h(s + 8) + 1));
        if (ae_h(s + 8) < ae_h(s + 0xa)) s[3] = 0;
        else { ae_wh(s + 8, 0); s[3] = 1; }
        return ae_i(s + 0xc);
    }
    case 10: {                                                          /* rangetrig */
        int in = ae_i(s + 0x14);
        if (in < ae_i(s) || in > ae_i(s + 4)) {
            if (in >= ae_i(s + 8) && in <= ae_i(s + 0xc)) s[0x10] = 0;
        } else if (!s[0x10]) { s[0x11] = 1; s[0x10] = 1; return 1; }
        s[0x11] = 0;
        return 0;
    }
    case 11: {                                                          /* delaytrig (0x720376) */
        if (ae_i(s + 8)) ae_wf(s, 0.0f);
        float t = ae_f(s);
        if (t < 0.0f) { s[4] = 0; return 0; }
        if ((float)ae_i(s + 0xc) <= t) { ae_wf(s, -1.0f); s[4] = 1; return 1; }
        ae_wf(s, t + 20.0f); s[4] = 0;
        return 0;
    }
    case 12: {                                                          /* stategen (0x7203bd) */
        const unsigned char *in = s + ae_h(s);
        for (int k = 0; k < s[2]; k++)
            if (ae_i(in + 4 * k)) { ae_w(s + 4, ae_i(s + 8 + 4 * k)); break; }
        return ae_i(s + 4);
    }
    case 14: {                                                          /* envelope (0x720434) */
        int in = ae_i(s + ae_h(s));
        unsigned char prev = s[2], seg = s[3], nseg = s[0x10];
        float val = ae_f(s + 0xc);
#define AE_ENVSEG() do { float tm_ = ae_f(s + 0x18 + 8 * s[3]); ae_wf(s + 4, tm_); \
                         ae_wf(s + 8, (ae_f(s + 0x1c + 8 * s[3]) - ae_f(s + 0xc)) / tm_ * 20.0f); } while (0)
        if (in == 1 && prev == 0) { s[3] = 0; ae_wf(s + 0xc, ae_f(s + 0x14)); AE_ENVSEG(); }
        else if (in == 3 && prev != 3 && (short)seg < (short)ae_h(s + 0x12)) { s[3] = s[0x12]; AE_ENVSEG(); }
        else if ((in == 1 || in == 3) && seg < nseg) {
            float tl = ae_f(s + 4) - 20.0f;
            ae_wf(s + 4, tl);
            if (tl <= 0.0f) {
                ae_wf(s + 0xc, ae_f(s + 0x1c + 8 * seg));
                s[3] = (unsigned char)(seg + 1);
                if (s[3] < nseg) AE_ENVSEG(); else ae_wf(s + 0xc, 0.0f);
            } else ae_wf(s + 0xc, val + ae_f(s + 8));
        } else if (in != 2) ae_wf(s + 0xc, 0.0f);
#undef AE_ENVSEG
        s[2] = (unsigned char)in;
        return ae_round(ae_f(s + 0xc));
    }
    case 17: { int k = ae_i(s + 4); return k > 0 && k <= s[0] ? ae_i(s + 4 + 4 * k) : 0; }   /* mux (0x720684) */
    case 18: {                                                          /* demux (0x72069e) */
        ae_w(s + 8 + 4 * (short)ae_h(s + 2), 0);
        int k = ae_i(s + 4);
        if (k > 0 && k <= s[0]) { ae_w(s + 8 + 4 * k, ae_i(s + 8)); ae_wh(s + 2, (unsigned short)k); }
        return ae_i(s + 0xc);
    }
    case 25: { int b = ae_i(s + 4); return b ? ae_i(s) / b : 0; }      /* divide (0x7207bf) */
    case 29: {                                                          /* ramp (0x720f32) */
        int tg = ae_i(s + 0x18);
        float cur = ae_f(s), ft = (float)tg;
        if (ft == cur) return tg;
        if (tg != ae_i(s + 8) || ae_i(s + 0x10) != ae_i(s + 0xc)) {
            ae_w(s + 8, tg); ae_w(s + 0xc, ae_i(s + 0x10));
            if (ae_i(s + 0x10) <= 0) { ae_wf(s, ft); return tg; }
            ae_wf(s + 4, (ft - cur) / (float)ae_i(s + 0x10) * 20.0f * 0.000244140625f);
        }
        float st = ae_f(s + 4);
        cur += (float)ae_i(s + 0x14) * st;
        if (st < 0.0f ? cur < ft : cur > ft) cur = ft;
        ae_wf(s, cur);
        return ae_round(cur);
    }
    case 13:                                                            /* merge */
        for (int k = 0; k < s[0]; k++) if (ae_i(s + 4 + 4 * k)) return 1;
        return 0;
    case 15: {                                                          /* table */
        int in = ae_i(s + 0xc);
        if (in == ae_i(s + 4)) return ae_i(s + 8);
        ae_w(s + 4, in);
        const unsigned char *t = g_sp.file + ae_i(s);
        int mn = ae_i(t + 4), mx = ae_i(t + 8), n = ae_h(t + 2);
        int x = (in < mn ? mn : in > mx ? mx : in) - mn, r;
        float step = ae_f(t + 0xc);
#define AE_TV(j) (t[0] == 2 ? (int)(short)ae_h(t + 0x10 + 2 * (j)) : t[0] == 1 ? (int)(signed char)t[0x10 + (j)] : ae_i(t + 0x10 + 4 * (j)))
        if (step == 1.0f) r = AE_TV(x);
        else {
            float f = (float)x * step;
            int i = ae_round(f - 0.5f), j = i + 1 < n ? i + 1 : n - 1;
            float fr = f - (float)i, a = (float)AE_TV(i), b = (float)AE_TV(j);
            r = ae_round((b - a) * fr + a);
        }
#undef AE_TV
        ae_w(s + 8, r);
        return r;
    }
    case 16: {                                                          /* delayline */
        unsigned char *in = s + ae_h(s);
        int size = ae_h(s + 2);
        if (ae_i(in + 4) != ae_i(s + 8)) {
            ae_w(s + 8, ae_i(in + 4));
            if (ae_i(in + 4) < 0) ae_w(in + 4, 0);
            int n = ae_round((float)ae_i(in + 4) / 20.0f);
            if (n >= size) n = size - 1;
            ae_wh(s + 4, (unsigned short)(ae_h(s + 6) + n));
        }
        if (ae_h(s + 4) >= size) ae_wh(s + 4, (unsigned short)(ae_h(s + 4) - size));
        if (ae_h(s + 6) >= size) ae_wh(s + 6, 0);
        ae_w(s + 0xc + 4 * ae_h(s + 4), ae_i(in));
        int r = ae_i(s + 0xc + 4 * ae_h(s + 6));
        ae_wh(s + 4, (unsigned short)(ae_h(s + 4) + 1));
        ae_wh(s + 6, (unsigned short)(ae_h(s + 6) + 1));
        return r;
    }
    case 21: {                                                          /* scale */
        float x = (float)ae_i(s + 8);
        for (int k = 1; k < s[0]; k++) x *= (float)ae_i(s + 8 + 4 * k);
        return ae_round(x * ae_f(s + 4));
    }
    case 22: {                                                          /* add */
        int x = ae_i(s + 4);
        for (int k = 1; k < s[0]; k++) x += ae_i(s + 4 + 4 * k);
        return x;
    }
    case 27: return ae_player(s, at, ref);
    case 28: {                                                          /* oscillator */
        int per = ae_i(s + 8);
        if (per <= 0) return 0;
        float step = 20.0f / (float)per, amp = (float)ae_i(s + 0xc), ph = ae_f(s + 4), v;
        while (ph >= 1.0f) ph -= 1.0f;
        if (s[0] == 0) {
            int a = ae_round(ph * 1024.0f), q = a >> 8 & 3, i = a & 0xff;
            static const unsigned short *tb;
            static unsigned short sn[257];
            if (!tb) { for (int k = 0; k <= 256; k++) sn[k] = (unsigned short)lrintf(sinf((float)k * 0.0061359233f) * 65535.0f); tb = sn; }
            int sv = q == 0 ? tb[i] : q == 1 ? tb[256 - i] : q == 2 ? -tb[i] : -tb[256 - i];
            v = (float)sv * amp * 1.5258789e-5f;
        } else if (s[0] == 1) v = ph < 0.5f ? 0.0f : amp;
        else if (s[0] == 2) v = amp * ph;
        else v = ph > 0.5f ? (1.0f - ph) * amp * 2.0f : ph * amp * 2.0f;
        ae_wf(s + 4, step + ph);
        return ae_round(v);
    }
    case 30: {                                                          /* addmax */
        int x = ae_i(s + 8);
        for (int k = 1; k < s[0]; k++) x += ae_i(s + 8 + 4 * k);
        return x > ae_i(s + 4) ? ae_i(s + 4) : x;
    }
    case 31: { int x = ae_i(s + 4) - ae_i(s + 8); return x < ae_i(s) ? ae_i(s) : x; }   /* subtractmin */
    case 35: return ae_round((float)ae_i(s + 8) * (float)ae_i(s + 4) * ae_f(s));          /* scale2 */
    case 38:                                                            /* ControlClass: CAR_SputOutput */
        if (s[0xc]) return 0;                                           /* (no ranges in this patch) */
        if (ae_i(s + 0x14)) { g_sp.child = 0; g_sp.out = 0; }
        else if (ae_i(s + 0x10)) { if (!g_sp.child) g_sp.child = 1; }
        else if (g_sp.child) g_sp.out = ae_i(s + 0x18);
        return g_sp.child;
    }
    return 0;
}

/* read the module's code into instructions (the subset it is made of) */
static int ae_decode(const unsigned char *d, long len, int code, AeIns *out, int max)
{
    int esi = 0, pc = code, n = 0;
#define AE_D32(o) ((int)(d[o] | d[(o) + 1] << 8 | d[(o) + 2] << 16 | (unsigned)d[(o) + 3] << 24))
    while (pc + 16 < len && n < max) {
        const unsigned char *c = d + pc;
        AeIns *i = &out[n];
        /* imul eax,[esi+d] (0f af); the inline min / max: mov ecx,[esi+d];
           cmp eax,ecx; jl / jg +2; mov eax,ecx */
        if ((c[0] == 0x0f && c[1] == 0xaf) || (c[0] == 0x8b && (c[1] & 0x3f) == 0x0e)) {
            const unsigned char *m = c + (c[0] == 0x0f);
            int mod = m[1] >> 6, l, dsp;
            if (mod == 3 || (c[0] == 0x0f && (m[1] & 0x3f) != 0x06)) break;
            dsp = mod == 0 ? 0 : mod == 1 ? (signed char)m[2] : AE_D32(pc + (int)(m - c) + 2);
            l = (int)(m - c) + 2 + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
            i->at = esi + dsp;
            if (c[0] == 0x0f) i->k = AE_MUL;
            else {
                const unsigned char *t = c + l;
                if (t[0] != 0x3b || t[1] != 0xc1 || (t[2] != 0x7c && t[2] != 0x7f) || t[3] != 0x02 || t[4] != 0x8b || t[5] != 0xc1) break;
                i->k = t[2] == 0x7c ? AE_MIN : AE_MAX;
                l += 6;
            }
            n++; pc += l; continue;
        }
        if (c[0] == 0x56) { pc++; continue; }                                     /* push esi */
        if (c[0] == 0x5e) { pc++; continue; }                                     /* pop esi */
        if (c[0] == 0xc3) return n;                                               /* ret */
        if (c[0] == 0x8b && c[1] == 0x74 && c[2] == 0x24) { pc += 4; continue; }  /* mov esi,[esp+8] */
        if (c[0] == 0xe8) { i->k = AE_CALL; i->op = (unsigned char)AE_D32(pc + 1); i->at = esi; n++; pc += 5; continue; }
        if ((c[0] == 0x83 || c[0] == 0x81) && (c[1] == 0xc6 || c[1] == 0xc4)) {   /* add esi / esp */
            int v = c[0] == 0x83 ? (signed char)c[2] : AE_D32(pc + 2);
            if (c[1] == 0xc6) esi += v;
            pc += c[0] == 0x83 ? 3 : 6; continue;
        }
        if (c[0] == 0x89 || c[0] == 0x8b || c[0] == 0xc6 || c[0] == 0x03) {       /* eax <-> [esi+d], byte [esi+d], add eax,[esi+d] */
            int mod = c[1] >> 6, rm = c[1] & 7, reg = c[1] >> 3 & 7, dsp, l;
            if (rm != 6 || reg != 0 || mod == 3) break;
            dsp = mod == 0 ? 0 : mod == 1 ? (signed char)c[2] : AE_D32(pc + 2);
            l = 2 + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
            i->k = c[0] == 0x89 ? AE_STORE : c[0] == 0x8b ? AE_LOAD : c[0] == 0x03 ? AE_ADD : AE_SETB;
            i->at = esi + dsp;
            if (c[0] == 0xc6) { i->imm = c[l]; l++; }
            n++; pc += l; continue;
        }
        break;
    }
#undef AE_D32
    printf("[sputter] module code: unknown byte %02x at +%x\n", d[pc], pc);
    return -1;
}

/* every decoded instruction stays inside the state and calls a ported operator */
static int ae_ops_ok(const AeIns *ins, int nins, int size)
{
    for (int k = 0; k < nins; k++) {
        const AeIns *i = &ins[k];
        if (i->at < 0 || i->at >= size - 0x18) return 0;
        /* ported: 0 1 4 6-8 10-18 21 22 25 27-31 35 38 */
        static const unsigned long long k_ported = 0x3ull | 1ull << 4 | 0x1c0ull | 0x7fc00ull | 3ull << 21 | 1ull << 25 |
                                                   0x1full << 27 | 1ull << 35 | 1ull << 38;
        if (i->k == AE_CALL && (i->op > 63 || !(k_ported >> i->op & 1))) {
            printf("[sputter] operator %d not ported\n", i->op);
            return 0;
        }
    }
    return 1;
}

/* one 20 ms pass of the current instance's module code per tick */
static void ae_tick(float dt, float ref)
{
    g_sp.tick += dt;
    if (g_sp.tick > 0.2f) g_sp.tick = 0.2f;
    for (; g_sp.tick >= 0.02f && !g_sp.dead; g_sp.tick -= 0.02f) {
        unsigned char *b = g_sp.mem + 0x18;
        int eax = 0;
        for (int k = 0; k < g_sp.nins && !g_sp.dead; k++) {
            const AeIns *i = &g_sp.ins[k];
            switch (i->k) {
            case AE_CALL:  eax = ae_op(i->op, b + i->at, i->at, ref); break;
            case AE_LOAD:  eax = ae_i(b + i->at); break;
            case AE_STORE: ae_w(b + i->at, eax); break;
            case AE_SETB:  b[i->at] = (unsigned char)i->imm; break;
            case AE_ADD:   eax += ae_i(b + i->at); break;
            case AE_MUL:   eax = (int)((unsigned)eax * (unsigned)ae_i(b + i->at)); break;
            case AE_MIN:   { int x = ae_i(b + i->at); if (!(eax < x)) eax = x; } break;
            case AE_MAX:   { int x = ae_i(b + i->at); if (!(eax > x)) eax = x; } break;
            }
        }
    }
}

/* ---- CAR_SWTN: the shift sweeteners (CARSFX_Shift, GC 800987a4 / PC
   0x47c890: shift ctl +0x71 -> PlayDisengageSnd 0x4720d0, +0x72 ->
   PlayEngageSnd 0x4722b0). Each play makes a CAR_SWTN instance of the car's
   SWTN_CAR_<slot> abk (the file CAR_Sputter runs from), member data {type
   0 disengage / 1 engage, engine slot, rpm, vol, az, 0, 0}; the module
   picks the sample by type and destroys itself when it is done ---- */
static int g_sw_ok, g_sw_tmpl, g_sw_size, g_sw_vscale, g_sw_prev;
/* g_sw_ctx[type + 2 * alt]: Play (0x4722b0 / its disengage twin) drops the
   handle to the type's old instance (refcount - 1) and makes a new one, so a
   shift while the last one's sample still plays runs two of that type */
static const int k_sw_vbase[4] = { 349, 350, 577, 578 };
static unsigned char g_sw_alt[2];
static float g_sw_rpm0;

static void sw_free(void)
{
    for (int t = 0; t < 4; t++) {
        g_aep = &g_sw_ctx[t];
        if (g_sp.mem) fx_stop(SPV(0));
        free(g_sp.mem);
        memset(&g_sp, 0, sizeof g_sp);
        g_sp.vbase = k_sw_vbase[t]; g_sp.set = AFX_SWTN;
    }
    g_sw_alt[0] = g_sw_alt[1] = 0;
    g_aep = &g_sp_ctx;
    g_sw_ok = 0;
}

static void sw_load(const unsigned char *d, long flen, unsigned loops, int slot)
{
    /* 0x7f3040 [slot] +4: the shift sweetener volume (the +8 column is CAR_Sputter's) */
    static const unsigned short k_swtn_vol4[43] = {
        15000, 18500, 14000, 12000, 13500, 16500, 16000, 14500, 16000, 22000, 12500, 18500, 20000, 18000, 18000,
        16000, 18000, 18000, 16750, 13250, 14000, 15000, 16000, 14000, 15000, 16500, 16000, 18000, 15500, 18000,
        19000, 13500, 15500, 13500, 13500, 22500, 16000, 18000, 13000, 15000, 14000, 18000, 18500 };
    long cls = -1, t = ae_i(d + 0x38);
    int n = t > 0 && t + 4 < flen ? ae_i(d + t) : 0;
    for (int k = 0; k < n && t + 16 + 12 * k <= flen; k++) {
        long nm = ae_i(d + t + 8 + 12 * k);
        if (nm > 0 && nm + 4 + (long)sizeof "CAR_SWTN" <= flen && !strcmp((const char *)d + nm + 4, "CAR_SWTN")) cls = ae_i(d + t + 4 + 12 * k);
    }
    if (cls < 0 || cls + 0x60 > flen) return;
    const unsigned char *md = d + cls + 0x20;
    int code = ae_i(md + 4), tmpl = ae_i(md + 8), size = ae_i(md + 0xc);
    if (code <= 0 || tmpl <= 0 || size <= 0x60 || tmpl + size > flen || md[0] != 1) return;
    g_aep = &g_sw_ctx[0];
    g_sp.ins = g_sw_ins;
    g_sp.nins = ae_decode(d, flen, code, g_sw_ins, AE_MAXINS);
    int ok = g_sp.nins > 0 && ae_ops_ok(g_sw_ins, g_sp.nins, size);
    for (int i = 0; i < 4 && ok; i++) {
        AeCtx *c = &g_sw_ctx[i];
        if (i) { int vb = c->vbase; *c = g_sw_ctx[0]; c->vbase = vb; }
        c->file = (unsigned char *)d; c->flen = flen;   /* CAR_Sputter's file: not freed here */
        c->mem = (unsigned char *)malloc((size_t)size);
        c->msize = size; c->loops = loops; c->idx = slot;
        c->nply = 1; c->ply[0] = ae_i(md + 0x18) - 0x18;
        c->dead = 1;                                    /* no instance yet */
        if (!c->mem) ok = 0;
    }
    g_aep = &g_sp_ctx;
    g_sw_tmpl = tmpl; g_sw_size = size;
    g_sw_vscale = slot >= 0 && slot < 43 ? k_swtn_vol4[slot] : 0;
    g_sw_ok = ok;
    printf("[swtn] CAR_SWTN %d instructions, vol %d%s\n", g_sw_ctx[0].nins, g_sw_vscale, ok ? "" : " (not usable)");
}

/* PlayDisengageSnd / PlayEngageSnd: a new instance of the type */
static void sw_start(int type, const CarSfxListener *L, float in, float ref)
{
    /* ShiftingAttackVolSlope (0x7f095c): 26000 at 4000 rpm .. 32767 at 7000, eval 0x4599e0 */
    float f = (in - 4000.0f) / 3000.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    int slope = (int)(26000.0f + 6767.0f * f);          /* _ftol */
    int ov = mix_objvol(1, 0x40010010u, type ? 2 : 3, L->mode, g_sp_p3.d, g_sp_p3.az);   /* vt+0x38(3 / 2) */
    int vol = (slope * g_sw_vscale >> 15) * ov >> 15;
    int az = (int)mix_objaz(1, 0x40010010u, 0, L->mode, g_sp_p3.az);                   /* vt+0x3c(0) */
    int rpm = (int)L->prpm;
    if (!g_sw_ctx[type + 2 * g_sw_alt[type]].dead) g_sw_alt[type] ^= 1;   /* the old one plays on */
    g_aep = &g_sw_ctx[type + 2 * g_sw_alt[type]];
    fx_stop(SPV(0));
    memcpy(g_sp.mem, g_sp.file + g_sw_tmpl, (size_t)g_sw_size);
    unsigned char *m = g_sp.mem + 0x18 + 0x28;
    ae_w(m, type);
    ae_w(m + 4, g_sp.idx < 0 ? 0 : g_sp.idx > 60 ? 60 : g_sp.idx);
    ae_w(m + 8, rpm < 0 ? 0 : rpm > 10000 ? 10000 : rpm);
    ae_w(m + 0xc, vol < 0 ? 0 : vol > 0x8013 ? 0x8013 : vol);
    ae_w(m + 0x10, az < 0 ? 0 : az > 0x10000 ? 0x10000 : az);
    ae_w(m + 0x14, 0); ae_w(m + 0x18, 0);
    g_sp.dead = 0; g_sp.tick = 0.02f;                   /* the first pass runs this frame */
    ae_tick(0.0f, ref);
    g_aep = &g_sp_ctx;
#ifdef OPENUG2_SIM_PRE_H
    printf("[swtn] %s rpm %d vol %d\n", type ? "engage" : "disengage", rpm, vol);
#endif
}

static void player_swtn(float dt, const CarSfxListener *L, float ref)
{
    if (!g_sw_ok) return;
    int sh = L->pwheels && L->pshifting && g_sp_ctx.ok;
    /* gates: 0x7faaa8 set and 7000.0 (0x78abf8): the engine rpm for the
       disengage, shift ctl +0xa8 for the engage (it also drives the engage's
       volume slope). +0xa8 = [+0x6c] +0x12c read by BeginUpShift 0x46eccf /
       BeginDownShift 0x46eeaf: [+0x6c] is the SFXCTL_Engine (GC GetEngRPM
       800beb80 reads its +0x124), whose UpdateRPM (GC 80091bcc) keeps the last
       update's rpm in +0x128 (PC +0x12c) -- the engine sound rpm one update
       before the shift began */
    float gate = 7000.0f;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_SWTN_GATE")) gate = (float)atof(getenv("OPENUG2_SWTN_GATE"));   /* sim test aid */
#endif
    static float rpm_prev = -1.0f;   /* SFXCTL_Engine +0x12c: the previous update's rpm */
    static int gear_prev = -1;
    static float st1 = -1.0f;        /* SFXCTL_Shifting state 1's ramp (+0xc8) time left */
    if (rpm_prev < 0.0f) rpm_prev = L->prpm;
    if (L->preplay) {
        /* the replay records the shift window only: its edges stand in */
        if (sh && !g_sw_prev) {
            g_sw_rpm0 = rpm_prev;
            if (L->prpm >= gate) sw_start(0, L, L->prpm, ref);
        } else if (!sh && g_sw_prev && L->pwheels && g_sw_rpm0 >= gate) sw_start(1, L, g_sw_rpm0, ref);
        st1 = -1.0f;
    } else {
        /* SFXCTL_Shifting::UpdateParams (PC 0x483130, GC 80090700) runs
           BeginUpShift when the gear goes up (CSTATE +0xc8 > +0xcc; n2sim
           puts the gear in mid-shift, clutch out). BeginUpShift 0x46ec70
           needs the rpm >= 3000 (0x7885c0) and then pulses +0x71 (the
           disengage); state 1 ends when its +0xc8 ramp -- the player's
           shift record +0x10 ms, at least 10 ms -- is done, pulsing +0x72
           (the engage, UpdateGearShiftState 0x482d50). Downshifts pulse
           neither. The record: 0x7f2860 + 0x60 x the 0x7f2230 entry's +4
           at [CSTATE +0x8c row][+0xa4 engine level] (SetupCTL 0x45c330). */
        static const unsigned char k_row_shrec[33][4] = {
            {0,1,2,3}, {4,5,6,7}, {5,5,6,7}, {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, {4,5,6,7},
            {12,13,14,15}, {0,1,2,3}, {4,5,6,7}, {0,1,2,3}, {8,9,10,11}, {12,13,14,15}, {1,2,3,3}, {0,1,2,3},
            {4,5,6,7}, {0,1,2,3}, {0,1,2,3}, {8,9,10,11}, {12,13,14,15}, {0,1,2,3}, {0,1,2,3}, {0,1,2,3},
            {1,1,2,3}, {1,1,2,3}, {1,1,2,3}, {0,1,2,3}, {5,6,6,7}, {5,6,7,11}, {4,5,6,7}, {5,5,6,7} };
        static const unsigned short k_shrec_up_ms[16] = {
            600, 500, 250, 150, 500, 400, 300, 175, 600, 500, 300, 250, 700, 600, 400, 300 };
        const int row = g_prow >= 0 && g_prow < 33 ? g_prow : 18;
        const int lv = L->pengine_level < 0 ? 0 : L->pengine_level > 3 ? 3 : L->pengine_level;
        const int up = L->pwheels && gear_prev >= 0 && L->pgear > gear_prev;
        if (st1 >= 0.0f) {
            st1 -= dt;
            if (st1 <= 0.0f) {
                st1 = -1.0f;
                if (L->pwheels && g_sw_rpm0 >= gate) sw_start(1, L, g_sw_rpm0, ref);
            }
        }
        if (up && L->prpm >= 3000.0f && g_sp_ctx.ok) {
            g_sw_rpm0 = rpm_prev;
            if (L->prpm >= gate) sw_start(0, L, L->prpm, ref);
            float ms = (float)k_shrec_up_ms[k_row_shrec[row][lv]];
            st1 = ms < 10.0f ? 0.01f : ms * 0.001f;
        }
    }
    gear_prev = L->pgear;
    g_sw_prev = sh;
    rpm_prev = L->prpm;
    for (int t = 0; t < 4; t++) {
        g_aep = &g_sw_ctx[t];
        if (!g_sp.dead) ae_tick(dt, ref);
        g_aep = &g_sp_ctx;
    }
}

/* ---- the AI racers' AEMS patches: FX_TURBO_xx (SOUND/TURBO bank of the
   car's turbo record, CARSFX_Turbo 0x47ca70), FX_NITROUS (CARSFX_Nitrous
   0x47cbe0) and FX_SKID (CARSFX_Skids 0x460680), run by the interpreter
   above. One instance each per AI car k (the blow-off is a second FX_TURBO
   instance), players on fx voices 351 + 15 k: whine 0-1, blow-off 2-3,
   nitrous 4-7, skid 8-14 (FX_SKID has 7 players in the PAV banks, 5 in the
   DRIFT ones). Member data (SetMemberData 0x735580) from state +0x28:
     FX_TURBO   type (0 whine, 1..3 blow-off variant), vol, boost x 1024,
                azimuth, throttle 0..0x400, rpm; update byte +0x40
     FX_NITROUS type 0, vol, azimuth, release; update byte +0x38
     FX_SKID    vol x3 (slots 3, 5, 4 by the surface gain), skid channels
                x3 (0..0x400), azimuth (slot 3), drift flag, 0x1000 x3,
                yaw deg, speed mph, 0, understeer, oversteer, rumble
                terrain; update byte +0x6c ---- */
static const char *const k_turbo_abk[15] = {
    "TURBO_SML1_0", "TURBO_SML1_1", "TURBO_SML1_2", "TURBO_SML2_0", "TURBO_SML2_1", "TURBO_SML2_2",
    "TURBO_MED_0", "TURBO_MED_1", "TURBO_MED_2", "TURBO_BIG_0", "TURBO_BIG_1", "TURBO_BIG_2",
    "TURBO_TRUCK_0", "TURBO_TRUCK_1", "TURBO_TRUCK_2" };
typedef struct {
    unsigned char *file; long flen;
    AeIns ins[AE_MAXINS];
    int nins, tmpl, size, nply, ply[8], set, ok;
    char path[512];
} AeProg;
static AeProg g_ap_turbo[CARSFX_AI_MAX], g_ap_nos[2], g_ap_skid[2];   /* g_ap_nos[b]: Nitrous_0b's FX_NITROUS; g_ap_skid[b]: 0x7ed1f8 [b + 2 drift]'s FX_SKID */
enum { AI_TW, AI_TB, AI_NOS, AI_SKID };
static AeCtx g_ai_inst[CARSFX_AI_MAX][4];
/* FX_NITROUS's second instance per AI car (voices 545 + 4 k): Play 0x472af0
   drops the car's handle to the old instance (0x735550 -> 0x735030: refcount
   - 1) and creates a new one; the old one plays its release out and the
   module ends it (ProcessUpdate 0x45f4b0 keeps it while the count > 1) */
static AeCtx g_ai_nos2[CARSFX_AI_MAX];
static unsigned char g_ai_nos_cur[CARSFX_AI_MAX];   /* 0: g_ai_inst[k][AI_NOS], 1: g_ai_nos2[k] */
#define AI_NOS_PV2(k) (545 + 4 * (k))
#define AI_PV(k) (351 + 15 * (k))
#define AI_M(c) ((c)->mem + 0x18 + 0x28)

static void ap_free(AeProg *p) { free(p->file); memset(p, 0, sizeof *p); }

/* the module of the first class whose name starts with cls */
static int ap_load(AeProg *p, const char *path, const char *cls, int set)
{
    if (p->ok && p->set == set && !strcmp(p->path, path)) return 1;   /* already loaded */
    ap_free(p);
    snprintf(p->path, sizeof p->path, "%s", path);
    p->set = set;
    FILE *f = fopen(path, "rb");
    if (!f) { printf("[aipatch] %s: missing\n", path); return 0; }
    fseek(f, 0, SEEK_END); p->flen = ftell(f); fseek(f, 0, SEEK_SET);
    p->file = p->flen > 0x100 ? (unsigned char *)malloc((size_t)p->flen) : NULL;
    int ok = p->file && fread(p->file, 1, (size_t)p->flen, f) == (size_t)p->flen;
    fclose(f);
    const unsigned char *d = p->file;
    long c = -1, cl = (long)strlen(cls);
    if (ok && !memcmp(d, "ABKC", 4)) {
        long t = ae_i(d + 0x38);
        int n = t > 0 && t + 4 < p->flen ? ae_i(d + t) : 0;
        for (int k = 0; k < n && t + 16 + 12 * k <= p->flen && c < 0; k++) {
            long nm = ae_i(d + t + 8 + 12 * k);
            if (nm > 0 && nm + 4 + cl < p->flen && !strncmp((const char *)d + nm + 4, cls, (size_t)cl)) c = ae_i(d + t + 4 + 12 * k);
        }
    }
    const unsigned char *md = c >= 0 && c + 0x40 <= p->flen ? d + c + 0x20 : NULL;
    int code = md ? ae_i(md + 4) : 0, tmpl = md ? ae_i(md + 8) : 0, size = md ? ae_i(md + 0xc) : 0;
    if (!md || code <= 0 || tmpl <= 0 || size <= 0x70 || tmpl + size > p->flen || md[0] < 1 || md[0] > 8) {
        printf("[aipatch] %s: no %s\n", path, cls);
        free(p->file); p->file = NULL;
        return 0;
    }
    p->nply = md[0]; p->tmpl = tmpl; p->size = size;
    for (int i = 0; i < p->nply; i++) p->ply[i] = ae_i(md + 0x18 + 4 * i) - 0x18;
    p->nins = ae_decode(d, p->flen, code, p->ins, AE_MAXINS);
    p->ok = p->nins > 0 && ae_ops_ok(p->ins, p->nins, size);
    printf("[aipatch] %s %s: %d instructions, %d players, state %#x%s\n", strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
           cls, p->nins, p->nply, size, p->ok ? "" : " (not usable)");
    return p->ok;
}

static int ai_alive(const AeCtx *c) { return c->ok && !c->dead; }
static void ai_inst_kill(AeCtx *c)
{
    if (ai_alive(c)) for (int p = 0; p < c->nply; p++) fx_stop(&g_fxvoice[c->vbase + p]);
    c->dead = 1;
}
/* CreateInstance: the template, then the member data the caller writes */
static unsigned char *ai_inst_start(AeCtx *c, const AeProg *p, int vbase)
{
    ai_inst_kill(c);
    if (!p->ok) return NULL;
    if (c->msize < p->size) {
        free(c->mem);
        c->mem = (unsigned char *)malloc((size_t)p->size);
        c->msize = c->mem ? p->size : 0;
    }
    if (!c->mem) return NULL;
    memcpy(c->mem, p->file + p->tmpl, (size_t)p->size);
    c->file = p->file; c->flen = p->flen; c->ins = p->ins; c->nins = p->nins;
    c->set = p->set; c->nply = p->nply; memcpy(c->ply, p->ply, sizeof c->ply);
    c->vbase = vbase; c->ok = 1; c->dead = 0; c->tick = 0.02f;   /* the first pass runs this frame */
    for (int i = 0; i < c->nply; i++) fx_stop(&g_fxvoice[vbase + i]);
    return AI_M(c);
}
static void ai_inst_run(AeCtx *c, float dt, float ref)
{
    if (!ai_alive(c)) return;
    g_aep = c;
    ae_tick(dt, ref);
    g_aep = &g_sp_ctx;
}
static void am(unsigned char *m, int i, int v, int lo, int hi) { ae_w(m + 4 * i, v < lo ? lo : v > hi ? hi : v); }

static void ai_patch_stop(int k)
{
    for (int j = 0; j < 4; j++) ai_inst_kill(&g_ai_inst[k][j]);
    ai_inst_kill(&g_ai_nos2[k]); g_ai_nos_cur[k] = 0;
}

/* the race's programs and their banks (carsfx_race_begin; the turbo banks'
   samples come with audio_load_ai_turbo_banks, sets AFX_TURBO + j) */
static void ai_patch_begin(void)
{
    char path[640];
    player_skid_stop();                                 /* it may share an AI bank's program (freed below) */
    for (int j = 0; j < CARSFX_AI_MAX; j++) {
        if (j >= g_naitb || g_aitb[j] < 0 || g_aitb[j] >= 15) { ap_free(&g_ap_turbo[j]); continue; }
        snprintf(path, sizeof path, "%s/SOUND/TURBO/%s_MB.abk", g_aproot, k_turbo_abk[g_aitb[j]]);
        ap_load(&g_ap_turbo[j], path, "FX_TURBO", AFX_TURBO + j);
    }
    /* SetupLoadData 0x465830 (CARSFX_Nitrous, vt 0x78ac58, class 0x10030 in
       the factory table 0x7ed3b4): table 0x7ed1a0 [level 2 or 3], 0
       Nitrous_00, 1 Nitrous_01, where the level = obj +0xac, written only by
       SetupSFX 0x45f210 (driver types 1 and 2) = 0x5995e0(EAXCar vt+0x2c()
       = +0x74 DriverInfo, +0x300, 2) = the car's NITROUS upgrade level (G3
       decoded, G4 11-01 ported). So each car has its own bank: AI car k
       plays set AFX_PNOS + n_bank; the player's comes from
       audio_set_player_perf. A bank is loaded only while some AI car needs
       it (the Vita's audio budget) */
    for (int b = 0; b < 2; b++) {
        int need = 0;
        for (int k = 0; k < g_nai; k++) need |= g_aifx[k].n_bank == b;
        snprintf(path, sizeof path, "%s/SOUND/NOS/Nitrous_0%d_MB.abk", g_aproot, b);
        if (!need) {
            if (g_ap_nos[b].ok || g_ap_nos[b].file) { ap_free(&g_ap_nos[b]); audio_load_patch_bank(AFX_PNOS + b, NULL); }
            continue;
        }
        if (!(g_ap_nos[b].ok && !strcmp(g_ap_nos[b].path, path)) && ap_load(&g_ap_nos[b], path, "FX_NITROUS", AFX_PNOS + b))
            printf("[aipatch] Nitrous_0%d bank: %d sounds (program file %ld bytes)\n", b,
                   audio_load_patch_bank(AFX_PNOS + b, path), g_ap_nos[b].flen);
    }
    /* SetupLoadData 0x465b90 (CARSFX_Skids, per car: CARSFX +0x9c = its
       EAXCar, row = EAXCar +0x8c): 0x7ed1f8 [(0x458f00(row) == 2) + 2 drift].
       The drift flag is the race's, so a field needs at most two banks: set
       AFX_PSKID + k_bank, each loaded only while some AI car needs it (the
       player's own bank: player_skid_sync) */
    for (int b = 0; b < 2; b++) {
        int need = 0;
        for (int k = 0; k < g_nai; k++) need |= g_aifx[k].k_bank == b;
        const char *bank = k_skid_bank[b + 2 * (g_drift != 0)];
        snprintf(path, sizeof path, "%s/SOUND/SKIDS/%s_MB.abk", g_aproot, bank);
        if (!need) {
            if (g_ap_skid[b].ok || g_ap_skid[b].file) { ap_free(&g_ap_skid[b]); audio_load_patch_bank(AFX_PSKID + b, NULL); }
            continue;
        }
        if (!(g_ap_skid[b].ok && !strcmp(g_ap_skid[b].path, path)) && ap_load(&g_ap_skid[b], path, "FX_SKID", AFX_PSKID + b))
            printf("[aipatch] %s bank: %d sounds (program file %ld bytes)\n", bank,
                   audio_load_patch_bank(AFX_PSKID + b, path), g_ap_skid[b].flen);
    }
}

/* CARSFX_Turbo on AI car k through FX_TURBO: the boost model of
   turbo_update, the whine (PlaySpl, ProcessUpdate 0x45ece0) and the
   blow-off (PlayBlowoff 0x45ee70 -> 0x45ef6a) as patch instances */
static void ai_turbo_patch(int k, AiSnd *a, AiFx *x, const CarSfxListener *L, float dt, float ref)
{
    AeCtx *w = &g_ai_inst[k][AI_TW], *b = &g_ai_inst[k][AI_TB];
    const AeProg *p = &g_ap_turbo[x->t_set];
    const int *d = a->p3.d; const unsigned *az = a->p3.az;
    float load = a->load, bc100 = a->out.bc100, rpm = a->erpm;
    /* +0x38 throttle: the engine controller's +0xb0 (GUESS: its 0..100
       throttle x 10.24; the module does not read it) */
    int thr = ftol(bc100 * 10.24f);
    x->t_in = load > 20.0f ? load * 0.01f : 0.0f;
    if (x->t_bov) {                                        /* 0x4726e0 */
        ramp_step(&x->t_ramp, dt);
        if (bc100 > 20.0f && x->t_ramp.val == 1.0f && a->sh == 0) ramp_init(&x->t_ramp, 1, 0, 150, 0);
        if (!ai_alive(b) || x->t_ramp.val == 0.0f) { x->t_bov = 0; ai_inst_kill(b); }
    }
    if (!ai_alive(w)) {
        unsigned char *m = ai_inst_start(w, p, AI_PV(k));
        if (m) {
            am(m, 0, 0, 0, 5); am(m, 1, 0, 0, 0x7fff); am(m, 2, ftol(x->t_frac * 1024.0f), 0, 0x2000);
            am(m, 3, (int)mix_objaz(2, 0x40020020u, 0, L->mode, az), 0, 0xffff);
            am(m, 4, thr, 0, 0x400); am(m, 5, ftol(rpm), 0, 10000);
            m[0x18] = 1;
        }
    } else turbo_boost(x, rpm, dt);
    if (x->t_st == 0) { if (load > 20.0f) x->t_st = 1; }
    else if (x->t_st == 1) {
        if (load < 20.0f) {
            x->t_st = 2;
            if (!x->t_bov) {                               /* 0x45ee70 */
                x->t_var = x->t_frac > 0.75f ? (int)rng_int(2) + 2 : 1;
                x->t_bvol = ftol((float)k_turbo_rec[x->t_rec][x->t_var == 1 ? 1 : 2] * x->t_frac);
                unsigned char *m = ai_inst_start(b, p, AI_PV(k) + 2);
                if (m) {
                    am(m, 0, x->t_var, 0, 5); am(m, 1, 0, 0, 0x7fff); am(m, 2, ftol(x->t_frac * 1024.0f), 0, 0x2000);
                    am(m, 3, 0, 0, 0xffff); am(m, 4, thr, 0, 0x400); am(m, 5, ftol(rpm), 0, 10000);
                    m[0x18] = 1;
                }
                x->t_bov = 1;
                ramp_init(&x->t_ramp, 1, 1, 1, 0);
            }
            x->t_boost = 0;
            turbo_boost(x, rpm, dt);
        }
    } else if (x->t_st == 2 && !x->t_bov) x->t_st = 0;
    if (ai_alive(w)) {                                     /* 0x45ece0 */
        unsigned char *m = AI_M(w);
        am(m, 1, x->t_whine * mix_objvol(2, 0x40020020u, 0, L->mode, d, az) >> 15, 0, 0x7fff);
        am(m, 2, ftol(x->t_frac * 1024.0f), 0, 0x2000);
        am(m, 3, (int)mix_objaz(2, 0x40020020u, 0, L->mode, az), 0, 0xffff);
        am(m, 4, thr, 0, 0x400); am(m, 5, ftol(rpm), 0, 10000);
        m[0x18] = 1;
    }
    if (x->t_bov && ai_alive(b)) {                         /* its volume only; the azimuth result is dropped */
        unsigned char *m = AI_M(b);
        am(m, 1, ftol((float)(x->t_bvol * mix_objvol(2, 0x40020020u, x->t_var == 1 ? 1 : 2, L->mode, d, az) >> 15) * x->t_ramp.val),
           0, 0x7fff);
        m[0x18] = 1;
    }
    ai_inst_run(w, dt, ref);
    ai_inst_run(b, dt, ref);
}

/* CARSFX_Nitrous on AI car k: Play (0x472af0 -> 0x472bab) on the burn's
   start, Release (0x45f2a0: member 3 = 1) at its end, ProcessUpdate
   0x45f4b0 every frame; the module ends the instance after the release.
   Play also starts a 0 -> 1 400 ms interp on +0xcc (0x4850dc -> 0x459990,
   release 1 -> 0 1000 ms) that nothing reads: ProcessUpdate's volume is
   vt+0x38 and its azimuth vt+0x3c, so it is inert in retail too. A new
   burn while the old instance still plays its release starts a second
   instance; the old one runs on with its last member data until the
   module ends it. */
static void ai_nos_patch(int k, AiFx *x, const AiSnd *a, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    AeCtx *n = g_ai_nos_cur[k] ? &g_ai_nos2[k] : &g_ai_inst[k][AI_NOS];
    AeCtx *o = g_ai_nos_cur[k] ? &g_ai_inst[k][AI_NOS] : &g_ai_nos2[k];   /* the previous burn's instance */
    const int *d = a->p3.d; const unsigned *az = a->p3.az;
    int vol = mix_objvol(2, 0x40020030u, 0, L->mode, d, az);
    int azv = (int)mix_objaz(2, 0x40020030u, 0, L->mode, az), nos = c->nos;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_AI_FX_TEST")) {   /* sim test aid: P s burns (OPENUG2_AI_FX_TEST=P, default 4) */
        float P = (float)atof(getenv("OPENUG2_AI_FX_TEST")); if (P <= 0.05f) P = 4.0f;
        nos = fmodf(a->time, 2.0f * P) > P;
    }
#endif
    if (nos && !x->n_on) {
        x->n_on = 1;
        if (ai_alive(n)) {   /* the last burn's release still plays: it keeps going, a new instance starts */
            AeCtx *t = n; n = o; o = t;
            g_ai_nos_cur[k] ^= 1;
        }
        unsigned char *m = ai_inst_start(n, &g_ap_nos[x->n_bank], g_ai_nos_cur[k] ? AI_NOS_PV2(k) : AI_PV(k) + 4);
        if (m) { am(m, 0, 0, 0, 2); am(m, 1, vol, 0, 0x7fff); am(m, 2, azv, 0, 0xffff); am(m, 3, 0, 0, 1); m[0x10] = 1; }
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_NOSLOG"))   /* sim test aid: which bank each burn plays */
            printf("[aipatch] opponent %d nitrous burn: Nitrous_0%d (set %d, instance %s, slot %d, last one %s)\n", k, x->n_bank,
                   AFX_PNOS + x->n_bank, m ? "started" : "NOT started", g_ai_nos_cur[k], ai_alive(o) ? "still playing" : "done");
#endif
    } else if (!nos && x->n_on) {
        x->n_on = 0;
        if (ai_alive(n)) { am(AI_M(n), 3, 1, 0, 1); AI_M(n)[0x10] = 1; }
    }
    if (ai_alive(n)) {
        unsigned char *m = AI_M(n);
        am(m, 1, vol, 0, 0x7fff); am(m, 2, azv, 0, 0xffff);
        m[0x10] = 1;
        ai_inst_run(n, dt, ref);
    }
    ai_inst_run(o, dt, ref);   /* no member updates: the handle was dropped */
}

/* CARSFX_Skids on AI car k: one FX_SKID instance (InitSkids 0x4608d0, one
   for an AI car), ProcessUpdate 0x460c70 every frame */
static void ai_skid_patch(int k, const AiSnd *a, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    AeCtx *s = &g_ai_inst[k][AI_SKID];
    const int *d = a->p3.d; const unsigned *az = a->p3.az;
    unsigned char *m;
    if (!ai_alive(s)) {
        int b = g_aifx[k].k_bank;
        m = ai_inst_start(s, &g_ap_skid[b], AI_PV(k) + 8);
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_SKIDLOG"))   /* sim test aid: which bank each car's FX_SKID instance plays */
            printf("[aipatch] opponent %d FX_SKID instance: %s (set %d, %s)\n", k, k_skid_bank[b + 2 * (g_drift != 0)],
                   AFX_PSKID + b, m ? "started" : "NOT started");
#endif
        if (!m) return;
        memset(m, 0, 17 * 4);
        am(m, 7, g_drift ? 1 : 0, 0, 1);                   /* 0x82bc2c */
        am(m, 8, 0x1000, 0, 0x1000); am(m, 9, 0x1000, 0, 0x1000); am(m, 10, 0x1000, 0, 0x1000);
    }
    m = AI_M(s);
    /* 0x460a70: the surface the wheels agree on, 0x2d3b on surfaces 14,
       15, 22 (DAT_007ed240), 0 with the left wheels (FL, RL) off the ground */
    int s0 = c->surface[0], s1 = c->surface[1], sf = s0;
    if (s0 != s1) {
        if (c->surface[2] == s0) { if (c->surface[3] != s0) sf = s1; }
        else if (c->surface[2] == s1) sf = s1;
    }
    int g = (sf < 14 || (sf > 15 && sf != 22)) ? 0x7fff : 0x2d3b;
    if (!c->contact[0] && !c->contact[3]) g = 0;
    am(m, 0, mix_objvol(2, 0x40020050u, 3, L->mode, d, az) * g >> 15, 0, 0x7fff);
    am(m, 1, mix_objvol(2, 0x40020050u, 5, L->mode, d, az) * g >> 15, 0, 0x7fff);
    am(m, 2, mix_objvol(2, 0x40020050u, 4, L->mode, d, az) * g >> 15, 0, 0x7fff);
    /* the skid controller's channels (AI: its sides averaged; GUESS: the
       port's c->skid 0..1 x 1024) */
    am(m, 3, ftol(c->skid[0] * 1024.0f), 0, 0x400);
    am(m, 4, ftol(c->skid[1] * 1024.0f), 0, 0x400);
    am(m, 5, ftol(c->skid[2] * 1024.0f), 0, 0x3ff);
    am(m, 6, (int)mix_objaz(2, 0x40020050u, 3, L->mode, az), 0, 0xffff);
    /* yaw (GUESS: the slip angle between heading and velocity), speed mph;
       the modules read neither yaw nor under/oversteer (members 11, 14, 15) */
    {   float cr = c->fwd[0] * c->vel[1] - c->fwd[1] * c->vel[0], dt2 = c->fwd[0] * c->vel[0] + c->fwd[1] * c->vel[1];
        am(m, 11, c->speed > 0.5f ? ftol(fabsf(atan2f(cr, dt2)) * 57.2958f) : 0, 0, 180); }
    am(m, 12, ftol(c->speed * 2.237f), 0, 1000);
    am(m, 13, 0, 0, 0); am(m, 14, 0, 0, 0x400); am(m, 15, 0, 0, 0x400);
    /* +0x44: the left wheel's terrain (+0xb0) 8 or 9 (GUESS: FL's surface) */
    am(m, 16, c->surface[0] == 8 || c->surface[0] == 9, 0, 1);
    m[0x44] = 1;
    ai_inst_run(s, dt, ref);
}

/* CARSFX_Skids on the player's car (class 0x10050, factory 0x7ed430;
   mixmap object 0x40010050 -- GAPS p2A): InitSkids 0x4608d0 gives a
   driver type 1 car (the human player) TWO FX_SKID instances (+0xc4 = 2,
   jump table 0x4608b8; types 2..4 one), one per side, run by the same
   UpdateParams 0x460a70 / ProcessUpdate 0x460c70 as an AI car's:
     instance 0 (+0xb0): vols slots 3/5/4 x gains +0xc8..+0xd0, azimuth
       slot 3, the left side: terrain wheel ctl +0xb0, on the ground +0xe8,
       channels SFXCTL_Skid +0x1a0/+0x1a4/+0x1a8
     instance 1 (+0xb4): vols slots 0/2/1 x gains +0xd4..+0xdc, azimuth
       slot 1, the right side: terrain +0xb4, on the ground +0xe9,
       channels +0x194/+0x198/+0x19c
   (one instance -- AI -- averages the two sides' channels and takes the
   wheels' common surface). Gain 0x2d3b (0x7ed240) on surfaces 14, 15, 22,
   else 0x7fff; 0 with the side off the ground. Member 11 = |CarState::
   GetYaw 0x59fa10| x 57.29578 deg, 12 = car +0x430 x 2.237 mph, 14 / 15 =
   Car::CalculateUndersteerFactor 0x5ed490 / Oversteer 0x5ed530 (the
   modules read neither: written 0), 16 = the side's terrain 8 or 9.
   The bank (0x465b90, player_skid_sync): its sounds 0..4 are already set
   AFX_SKID (audio.c skid_load), so only its program is loaded here (the
   file stays resident like an AI bank's: 205-255 KB, none when an AI car
   of the race holds the same bank's program). Players on fx voices
   531..544. Replaces A5's hand mix (audio.c, SKID_PAV's pairing for every
   bank), kept as the fallback (test aid OPENUG2_PSKID_HAND) */
static AeProg g_ap_pskid;
static AeCtx g_pskid_inst[2];
static Pos3D g_psk_p3;
static void player_skid_stop(void)
{
    ai_inst_kill(&g_pskid_inst[0]);
    ai_inst_kill(&g_pskid_inst[1]);
    g_pskid_patch = 0;
}
static void player_skid(float dt, const CarSfxListener *L, float ref)
{
    static int hand = -1;
    if (hand < 0) hand = getenv("OPENUG2_PSKID_HAND") != NULL;
    if (hand || !L->coll || !L->pwheels || !g_pskid || g_prow < 0) { player_skid_stop(); return; }
    if (!ai_alive(&g_pskid_inst[0]) || !ai_alive(&g_pskid_inst[1])) {
        char path[640];
        int b = engine_class(g_prow) == 2;
        player_skid_stop();
        snprintf(path, sizeof path, "%s/SOUND/SKIDS/%s_MB.abk", g_aproot, g_pskid);
        const AeProg *p = g_ap_skid[b].ok && !strcmp(g_ap_skid[b].path, path) ? &g_ap_skid[b] : NULL;
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_PSKID_OWN")) p = NULL;   /* sim test aid: always the player's own program load */
#endif
        if (p) ap_free(&g_ap_pskid);
        else {
            if (!ap_load(&g_ap_pskid, path, "FX_SKID", AFX_SKID)) return;
            p = &g_ap_pskid;
        }
        memset(&g_psk_p3, 0, sizeof g_psk_p3);
        for (int i = 0; i < 2; i++) {
            AeCtx *s = &g_pskid_inst[i];
            unsigned char *m = ai_inst_start(s, p, 531 + 7 * i);
            if (!m) { player_skid_stop(); return; }
            s->set = AFX_SKID;                             /* the player's copy of the bank (audio.c skid_load) */
            memset(m, 0, 17 * 4);
            am(m, 7, g_drift ? 1 : 0, 0, 1);               /* 0x82bc2c */
            am(m, 8, 0x1000, 0, 0x1000); am(m, 9, 0x1000, 0, 0x1000); am(m, 10, 0x1000, 0, 0x1000);
        }
        printf("[pskid] player FX_SKID instances (2): %s (%d players each%s)\n", g_pskid, g_pskid_inst[0].nply,
               p == &g_ap_pskid ? "" : ", program shared");
    }
    g_pskid_patch = 1;
    pos3d_update(&g_psk_p3, L->car, L, dt);
    const int *d = g_psk_p3.d; const unsigned *az = g_psk_p3.az;
    /* the sides' terrain (wheel ctl +0xb0 / +0xb4, GenerateTerrainTypes: player_road
       keeps it) and on-the-ground flags (+0xe8 FL||RL / +0xe9 FR||RR) */
    const int down[2] = { L->pcontact[0] || L->pcontact[3], L->pcontact[1] || L->pcontact[2] };
    static const int k_slot[2][4] = { { 3, 5, 4, 3 }, { 0, 2, 1, 1 } };   /* vols x3, azimuth */
    float yaw = 0.0f;
    {   float cr = L->car_fwd[0] * L->car_vel[1] - L->car_fwd[1] * L->car_vel[0];
        float dp = L->car_fwd[0] * L->car_vel[0] + L->car_fwd[1] * L->car_vel[1];
        if (fabsf(L->pspeed) > 0.5f) yaw = fabsf(atan2f(cr, dp)) * 57.29578f; }
    for (int i = 0; i < 2; i++) {
        AeCtx *s = &g_pskid_inst[i];
        unsigned char *m = AI_M(s);
        const volatile float *ch = g_skid3s[i == 0 ? 1 : 0];   /* instance 0 = left = port side 1 */
        int sf = g_rn.terr[i];
        int g = (sf < 14 || (sf > 15 && sf != 22)) ? 0x7fff : 0x2d3b;
        if (!down[i]) g = 0;
        am(m, 0, mix_objvol(1, 0x40010050u, k_slot[i][0], L->mode, d, az) * g >> 15, 0, 0x7fff);
        am(m, 1, mix_objvol(1, 0x40010050u, k_slot[i][1], L->mode, d, az) * g >> 15, 0, 0x7fff);
        am(m, 2, mix_objvol(1, 0x40010050u, k_slot[i][2], L->mode, d, az) * g >> 15, 0, 0x7fff);
        /* GUESS (as the AI's): the controller's 0..1 channels x 1024 */
        am(m, 3, ftol(ch[0] * 1024.0f), 0, 0x400);
        am(m, 4, ftol(ch[1] * 1024.0f), 0, 0x400);
        am(m, 5, ftol(ch[2] * 1024.0f), 0, 0x3ff);
        am(m, 6, (int)mix_objaz(1, 0x40010050u, k_slot[i][3], L->mode, az), 0, 0xffff);
        am(m, 11, ftol(yaw), 0, 180);                      /* GUESS: GetYaw = the slip angle */
        am(m, 12, ftol(fabsf(L->pspeed) * 2.237f), 0, 1000);
        am(m, 13, 0, 0, 0); am(m, 14, 0, 0, 0x400); am(m, 15, 0, 0, 0x400);
        am(m, 16, sf == 8 || sf == 9, 0, 1);
        m[0x44] = 1;
        ai_inst_run(s, dt, ref);
    }
#ifdef OPENUG2_SIM_PRE_H
    static float logt;
    if ((logt += dt) >= 1.0f && getenv("OPENUG2_SKIDLOG")) {   /* sim test aid: the player's instances */
        int on[2] = { 0, 0 };
        logt = 0.0f;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < g_pskid_inst[i].nply; j++) on[i] += g_fxvoice[g_pskid_inst[i].vbase + j].on != 0;
        printf("[pskid] L vol %d skid %.2f %.2f %.2f terr %d on %d | R vol %d skid %.2f %.2f %.2f terr %d on %d\n",
               ae_i(AI_M(&g_pskid_inst[0])), g_skid3s[1][0], g_skid3s[1][1], g_skid3s[1][2], g_rn.terr[0], on[0],
               ae_i(AI_M(&g_pskid_inst[1])), g_skid3s[0][0], g_skid3s[0][1], g_skid3s[0][2], g_rn.terr[1], on[1]);
    }
#endif
}

static void ai_patch_update(int k, AiSnd *a, AiFx *x, const CarSfxCar *c, const CarSfxListener *L, float dt, float ref)
{
    if (x->t_on && x->t_set >= 0 && g_ap_turbo[x->t_set].ok) ai_turbo_patch(k, a, x, L, dt, ref);
    ai_nos_patch(k, x, a, c, L, dt, ref);
    ai_skid_patch(k, a, c, L, dt, ref);
}

/* ---- the weather (SetupLoadData 0x4664c0 / 0x466420: IG_GLOBAL/FX_Rain_MB
   class FX_Weather, IG_GLOBAL/WIND_00_MB class FX_WIND_Weather) ----
   CARSFX_Rain (0x10090, map class 1 object 0x40010090): UpdateParams
   0x474770 plays FX_Weather once Rain +0x1f0 is above 0.001 and stops it at
   or below; ProcessUpdate 0x4618c0 writes the members every frame:
     m0 type (1 in the car's views: EAXCar +0xa8 == 4 or 2 with +0xac set),
     m1 0, m2 the mixmap volume of slot `type`, m3 7000, m4 0, m5 stop
   (Stop: m5 = 1 and the instance ends 10 s later). The thunder: a timer
   (+0xb8) runs; above intensity 0.25, outside game modes 2 / 3 / 7 and race
   mode 8, once it passes the period (+0xbc) and the counter (+0xc4) is 0,
   QueueWeatherStream (THUNDERSTRM, variant 1 below 0.5 else 2) and a new
   period 25 + rand(20) s; past the period with the counter set it counts up
   a frame, back to 0 above 32.
   CARSFX_WindWeather (0x100a0, object 0x400100a0 -- the factory table
   0x7ed3b4 entries are {class, name, ?, ctor}: 0x7ed420 = 0x100a0
   "CARSFX_WindWeather", ctor 0x47d500 -> 0x473980; 0x10050 is
   CARSFX_Skids, R2 11-01; it read the skids' slot 0): plays always (Play
   0x4739c0); ProcessUpdate 0x473a10: m0 = the volume of slot 0 x the Slope
   0x7f09f4 (1.0 at 0 mph -> 0.25 at 70 mph, input car +0x430 x 2.237), m1 0,
   m2 4000, m3 0. Players: fx voices 471.. (rain) and 475.. (wind). */
static AeProg g_ap_rain, g_ap_wind;
static AeCtx g_rain_inst, g_wind_inst;
static Pos3D g_rain_p3;
static float g_wx_rng = 1.0f;
void carsfx_set_rain(float I) { g_rain.I = I; }
static int g_wx_rcstate = 4, g_wx_race_mode, g_wx_loading;
void carsfx_set_race_flow(int rcstate, int race_mode, int loading)
{
    g_wx_rcstate = rcstate; g_wx_race_mode = race_mode; g_wx_loading = loading;
}
static void weather_stop(void)   /* every fx voice was stopped: the instances restart next update */
{
    ai_inst_kill(&g_rain_inst); ai_inst_kill(&g_wind_inst);
    g_rain.playing = g_rain.stopping = 0;
}
static int weather_load(void)
{
    static int tried, ok;
    if (tried) return ok;
    tried = 1;
    char path[640];
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/FX_Rain_MB.abk", g_aproot);
    if (ap_load(&g_ap_rain, path, "FX_Weather", AFX_PRAIN))
        printf("[weather] FX_Rain bank: %d sounds\n", audio_load_patch_bank(AFX_PRAIN, path));
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/WIND_00_MB.abk", g_aproot);
    if (ap_load(&g_ap_wind, path, "FX_WIND_Weather", AFX_PWIND))
        printf("[weather] WIND_00 bank: %d sounds\n", audio_load_patch_bank(AFX_PWIND, path));
    ok = 1;
    return ok;
}
static void player_weather(float dt, const CarSfxListener *L, float ref)
{
    if (!g_aproot[0] || !L->coll) {
        if (ai_alive(&g_rain_inst)) ai_inst_kill(&g_rain_inst);
        if (ai_alive(&g_wind_inst)) ai_inst_kill(&g_wind_inst);
        g_rain.playing = g_rain.stopping = 0;
        return;
    }
    weather_load();
    pos3d_update(&g_rain_p3, L->car, L, dt);
    const int *d = g_rain_p3.d; const unsigned *az = g_rain_p3.az;
    /* CARSFX_Rain::UpdateParams */
    g_rain.I_prev = g_rain.I;
    if (g_rain.I <= 0.001f) {
        if (g_rain.playing && !g_rain.stopping && ai_alive(&g_rain_inst)) {   /* Stop */
            g_rain.stopping = 1; g_rain.stop_t = 10.0f;
            am(AI_M(&g_rain_inst), 5, 1, 0, 1);
            printf("[weather] rain sound stops\n");
        }
    } else if (!g_rain.playing) {   /* Play */
        unsigned char *m = ai_inst_start(&g_rain_inst, &g_ap_rain, 471);
        if (m) {
            g_rain.playing = 1; g_rain.stopping = 0;
            for (int i = 0; i < 6; i++) am(m, i, 0, 0, 0x7fff);
            printf("[weather] rain sound plays (intensity %.3f)\n", g_rain.I);
        }
    }
    g_rain.thunder_t += dt;
    /* 0x47481c: skipped while TheCarLoader (0x8a3288) has loads pending
       (+0x14 = 0x8a329c; the port loads no car while the world runs, so never)
       or the world streamer is loading (TheTrackStreamer +0x168 = 0x88ec68) */
    if (g_rain.I > 0.25f && !g_wx_loading && g_rain.thunder_t > g_rain.thunder_period) {
        if (g_rain.thunder_n == 0) {
            g_rain.thunder_t = 0.0f;
            if (speech_stream_queued()) goto thunder_done;   /* IsStrmQueued 0x46c9a0 */
            /* QueueWeatherStream 0x4852a0 (vtable +0x44): the THUNDERSTRM
               stream = nisaudio SoundFX_Select(4, 1 below 0.5 else 2) on the
               speech stream, only while no speech plays; not in CAMERA_FLYBY /
               PRERACE / POSTRACE (2 / 3 / 7) nor race mode 8 */
            if (g_wx_rcstate == 2 || g_wx_rcstate == 3 || g_wx_rcstate == 7 || g_wx_race_mode == 8) {
#ifdef OPENUG2_SIM_PRE_H
                if (getenv("OPENUG2_RAIN_LOG")) printf("[weather] thunder skipped (race state %d mode %d)\n", g_wx_rcstate, g_wx_race_mode);
#endif
            } else {
                int v = g_rain.I < 0.5f ? 1 : 2, q = speech_weather_stream(v);
#ifdef OPENUG2_SIM_PRE_H
                if (getenv("OPENUG2_RAIN_LOG")) printf("[weather] thunder (THUNDERSTRM variant %d)%s\n", v, q ? " queued" : " (speech busy)");
#else
                (void)q;
#endif
            }
            g_wx_rng = fmodf(g_wx_rng * 16807.0f, 2147483647.0f);
            g_rain.thunder_period = 25.0f + (float)((int)g_wx_rng % 20);
            g_rain.thunder_n++;
        } else if (++g_rain.thunder_n > 32) g_rain.thunder_n = 0;
    }
thunder_done:
    if (g_rain.stopping) {
        g_rain.stop_t -= dt;
        if (g_rain.stop_t < 0.0f) { ai_inst_kill(&g_rain_inst); g_rain.playing = g_rain.stopping = 0; }
    }
    if (g_rain.playing && ai_alive(&g_rain_inst)) {   /* ProcessUpdate */
        unsigned char *m = AI_M(&g_rain_inst);
        /* EAXCar +0xa8 = the POV type: 4 hood / 2 bumper (0x4618fd / 0x461910) = in the car */
        int type = L->pview == 4 || L->pview == 2 ? 1 : 0;
        am(m, 2, mix_objvol(1, 0x40010090u, type, L->mode, d, az), 0, 0x7fff);
        am(m, 4, 0, 0, 0);
        am(m, 0, type, 0, 3);
        am(m, 1, 0, 0, 0);
        am(m, 3, 7000, 0, 7000);
        m[0x18] = 1;
        ai_inst_run(&g_rain_inst, dt, ref);
    } else if (g_rain.playing) g_rain.playing = g_rain.stopping = 0;
    /* CARSFX_WindWeather */
    if (!ai_alive(&g_wind_inst)) {
        unsigned char *m = ai_inst_start(&g_wind_inst, &g_ap_wind, 475);
        if (m) { for (int i = 0; i < 4; i++) am(m, i, 0, 0, 0x7fff); printf("[weather] wind plays\n"); }
    }
    if (ai_alive(&g_wind_inst)) {
        float mph = fabsf(L->pspeed) * 2.237f;
        float slope = mph <= 0.0f ? 1.0f : mph >= 70.0f ? 0.25f : 1.0f - 0.75f * mph / 70.0f;
        unsigned char *m = AI_M(&g_wind_inst);
        am(m, 0, ftol((float)mix_objvol(1, 0x400100a0u, 0, L->mode, d, az) * slope), 0, 0x7fff);
        am(m, 2, 4000, 0, 4000);
        am(m, 3, 0, 0, 0);
        m[0x10] = 1;
        ai_inst_run(&g_wind_inst, dt, ref);
    }
#ifdef OPENUG2_SIM_PRE_H
    static float logt;
    if ((logt += dt) >= 2.0f && getenv("OPENUG2_RAIN_LOG")) {
        logt = 0.0f;
        int type = L->pview == 4 || L->pview == 2 ? 1 : 0;
        printf("[weather] rain I %.3f playing %d stopping %d src 40010092=%d rain vol %d wind vol %d mph %.0f\n",
               g_rain.I, g_rain.playing, g_rain.stopping, md_src(0x40010092u, 0),
               mix_objvol(1, 0x40010090u, type, L->mode, d, az), mix_objvol(1, 0x400100a0u, 0, L->mode, d, az),
               fabsf(L->pspeed) * 2.237f);
    }
#endif
}

/* ---- the planes (CSTATEMGR_Planes 0x464410, SFXObj_Plane 0x462810 / 0x462ae0 /
   0x462b70) -------------------------------------------------------------- */
#define PL_MAX 6            /* the longest list (PlanesMain 0x7ed690) */
#define PL_STATES 3         /* EnterWorld 0x476e00: three CSTATE_Plane */
#define FXPL(j) (&g_fxvoice[479 + (j)])
static CarSfxPlane g_pl_in[PL_MAX];
static int g_pl_n;
static float g_pl_car[3];
static struct {
    int on, entry;          /* +0x3c attached, +0x40 the list entry */
    int play;               /* 1: +0xd8 the ENV_STATIC instance, 2: +0xdc the FX_MAIN_MEM one */
    float prev[3], pos[3], speed;   /* +0xb0, +0xc0, +0xd0 */
    Pos3D p3;               /* +0xd4: SFXCTL_3DEnviroObjPos */
} g_pl[PL_STATES];
/* the URL planes' FX_MAIN_MEM (IG_GLOBAL/FX_MAIN_MEM_MB.abk, 5 players):
   members 0 0, 1 volume, 2 pitch, 3 azimuth, 4 the distance (-1000..1000) */
static AeProg g_ap_pmain;
static AeCtx g_pl_mi[PL_STATES];
#define PLM_V(s) (482 + 5 * (s))
static int g_pl_url;
void carsfx_set_url(int url)
{
    g_pl_url = url;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_PLANE_URL")) g_pl_url = 1;   /* test aid: the URL plane patch in free roam */
#endif
}
static int pl_main_ok(void)
{
    static int tried;
    if (!tried && g_aproot[0]) {
        tried = 1;
        char path[640];
        snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/FX_MAIN_MEM_MB.abk", g_aproot);
        if (ap_load(&g_ap_pmain, path, "FX_MAIN_MEM", AFX_PMAIN))
            printf("[planes] FX_MAIN_MEM bank: %d sounds\n", audio_load_patch_bank(AFX_PMAIN, path));
    }
    return g_ap_pmain.ok;
}
void carsfx_set_planes(const CarSfxPlane *p, int n, const float car[3])
{
    g_pl_n = n < 0 ? 0 : n > PL_MAX ? PL_MAX : n;
    if (g_pl_n) memcpy(g_pl_in, p, sizeof *p * (size_t)g_pl_n);
    memcpy(g_pl_car, car, sizeof g_pl_car);
}
static void planes_reset(void)
{
    for (int s = 0; s < PL_STATES; s++) { fx_stop(FXPL(s)); ai_inst_kill(&g_pl_mi[s]); memset(&g_pl[s], 0, sizeof g_pl[s]); }
}
static float pl_dist(const float a[3], const float b[3])           /* bDistBetween 0x43ce10 */
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}
static void pl_attach(int s, int e)     /* vt+0x28: NewAttachment 0x462810 */
{
    g_pl[s].on = 1; g_pl[s].entry = e;
    memcpy(g_pl[s].pos, g_pl_in[e].pos, sizeof g_pl[s].pos);
    /* retail makes the instance here (ENV_STATIC variant 1, or FX_MAIN_MEM
       in URL: 0x89e7e1 unless game state 4 with 0x83aab4 == 8); the speed
       gate in ProcessUpdate releases it on the first update below 0.3 and
       remakes it, so it is made there */
    g_pl[s].play = 0;
}
static void pl_detach(int s)            /* vt+0x2c: Detach 0x462a40 */
{
    fx_stop(FXPL(s));
    ai_inst_kill(&g_pl_mi[s]);
    g_pl[s].on = 0; g_pl[s].play = 0;
}
static void player_planes(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll) { for (int s = 0; s < PL_STATES; s++) if (g_pl[s].on) pl_detach(s); return; }
    /* CSTATEMGR_Planes::UpdateParams 0x464410 */
    for (int e = 0; e < g_pl_n; e++) {
        if (!g_pl_in[e].ok) continue;                               /* GetAnimPosition 0x439f40 */
        int near = pl_dist(g_pl_in[e].pos, g_pl_car) < 2000.0f;      /* 0x78a514, the closest player car */
        int s = 0;
        while (s < PL_STATES && !(g_pl[s].on && g_pl[s].entry == e)) s++;
        if (s < PL_STATES) {                                        /* attached: let go out of range / delaying */
            if (!near || g_pl_in[e].delay) pl_detach(s);
            continue;
        }
        if (!near || g_pl_in[e].delay) continue;
        for (s = 0; s < PL_STATES && g_pl[s].on; s++) {}            /* vt+0x30: a free state */
        if (s == PL_STATES) {                                       /* else the first one farther away */
            float dn = pl_dist(g_pl_in[e].pos, g_pl_car);
            for (s = 0; s < PL_STATES; s++)
                if (pl_dist(g_pl_in[g_pl[s].entry].pos, g_pl_car) > dn) { pl_detach(s); break; }
        }
        if (s < PL_STATES) pl_attach(s, e);
    }
    static const unsigned char k_var[10] = { 1, 2, 0, 0, 3, 4, 5, 6, 7, 8 };   /* ENV_STATIC's variant table */
    int clip = k_var[1] - 1;                                         /* variant 1 -> ENV_COMMON clip 1 */
    for (int s = 0; s < PL_STATES; s++) {
        if (!g_pl[s].on) continue;
        /* SFXObj_Plane::UpdateParams 0x462ae0: the distance moved in one update */
        const float *np = g_pl_in[g_pl[s].entry].pos;
        float mv = pl_dist(np, g_pl[s].prev);
        /* GUESS: retail updates the audio once a frame at 60 Hz; the
           distance is scaled to 1/60 s */
        g_pl[s].speed = dt > 0 ? mv * (1.0f / 60.0f) / dt : mv;
        memcpy(g_pl[s].prev, np, sizeof g_pl[s].prev);
        memcpy(g_pl[s].pos, np, sizeof g_pl[s].pos);
        pos3d_update(&g_pl[s].p3, np, L, dt);   /* GUESS: the port's 2D Pos3D (the plane's height not in its distance) */
        AudioFxVoice *v = FXPL(s);
        /* ProcessUpdate 0x462b70: below 0.3 (0x78758c) the instance goes, else one is made */
        if (g_pl[s].speed < 0.3f) {
            if (g_pl[s].play) { fx_stop(v); ai_inst_kill(&g_pl_mi[s]); g_pl[s].play = 0; }
            continue;
        }
        const int *d = g_pl[s].p3.d; const unsigned *az = g_pl[s].p3.az;
        if (!g_pl[s].play) {
            /* URL (0x89e7e1) and not 0x457ba0 (game state 4 with 0x83aab4 == 8;
               CONFIRMED G3 11-01: ecx = 0x83a9d0, so +0x156a8 = game mode 0x850078 (4 =
               split screen) and +0xe4 = quick-race type 0x83aab4 (8 = Outrun tourney);
               split screen is not ported, so never true): FX_MAIN_MEM {0}, else ENV_STATIC {1} */
            unsigned char *m = g_pl_url && pl_main_ok() ? ai_inst_start(&g_pl_mi[s], &g_ap_pmain, PLM_V(s)) : NULL;
            if (m) { memset(m, 0, 5 * 4); g_pl[s].play = 2; }
            else { g_pl[s].play = 1; fx_start(v, AFX_ENV, clip, audio_fx_loop(AFX_ENV, clip), 0, 1); }
        }
        if (g_pl[s].play == 2) {
            /* 0x462d8c: f = the Pos3D's distance (vt+0x34 = its +8 int), at
               most 1000, negative behind (azimuth 0x4000..0xc000), x 2.6 */
            unsigned a1 = mix_objaz(7, 0x40070000u, 1, L->mode, az);
            float f = (float)d[0];
            if (f > 1000.0f) f = 1000.0f;
            if (a1 > 0x4000 && a1 < 0xc000) f *= -1.0f;
            int dist = ftol(f * 2.6f);
            int pit = ftol(pos3d_pitch(&g_pl[s].p3, 5) * 4096.0f);
            unsigned char *m = AI_M(&g_pl_mi[s]);
            am(m, 0, 0, 0, 0);
            am(m, 1, mix_objvol(7, 0x40070000u, 1, L->mode, d, az), 0, 0x7fff);
            am(m, 2, pit, 0, 0x4000);
            am(m, 3, (int)mix_objaz(7, 0x40070000u, 1, L->mode, az), 0, 0xffff);
            am(m, 4, dist, -1000, 1000);
            m[0x14] = 1;
            ai_inst_run(&g_pl_mi[s], dt, ref);
            if (!ai_alive(&g_pl_mi[s])) g_pl[s].play = 0;
            continue;
        }
        /* GUESS: the mixmap class 7 output 0x40070000 taken as SFXObj_Plane's (id 0x70000) */
        int vol = mix_objvol(7, 0x40070000u, 0, L->mode, d, az);    /* +8: vt+0x38(0), 0..0x7fff */
        int pit = ftol(pos3d_pitch(&g_pl[s].p3, 5) * 4096.0f);      /* +0xc: GenDopplerFactor(5, 0) x 4096, 0..0x1fa0 */
        if (pit < 0) pit = 0; if (pit > 0x1fa0) pit = 0x1fa0;
        v->gain = fx_gain(vol, ref, 0.6f);
        v->pitch = (float)pit / 4096.0f;
        v->pan = az_pan(mix_objaz(7, 0x40070000u, 0, L->mode, az)); /* +0x10: vt+0x3c(0) */
    }
#ifdef OPENUG2_SIM_PRE_H
    static float logt;
    if ((logt += dt) >= 2.0f && getenv("OPENUG2_PLANE_LOG")) {
        logt = 0.0f;
        int nok = 0;
        for (int e = 0; e < g_pl_n; e++) nok += g_pl_in[e].ok;
        printf("[planes] %d of %d found", nok, g_pl_n);
        for (int s = 0; s < PL_STATES; s++)
            if (g_pl[s].on) printf("; state %d entry %d at %.0f m speed %.2f play %d vol %d", s, g_pl[s].entry,
                                   pl_dist(g_pl[s].pos, g_pl_car), g_pl[s].speed, g_pl[s].play,
                                   mix_objvol(7, 0x40070000u, 0, L->mode, g_pl[s].p3.d, g_pl[s].p3.az));
        printf("\n");
    }
#endif
}

/* ---- the trains (CSTATEMGR_Trains 0x464600, SFXObj_Train 0x463020 /
   0x462ae0-like UpdateParams / 0x4631e0) and the fountains
   (CSTATEMGR_Enviro 0x4647e0, SFXObj_Fountain 0x463820 / 0x463920 /
   0x463a60): world emitters on ENV_STATIC ---------------------------------- */
#define TRN_MAX 24          /* uTrainIDs 0x7ed798..0x7ed8b8 */
#define TRN_STATES 6        /* EnterWorld: six CSTATE_Train */
#define FNT_STATES 4        /* EnterWorld: four CSTATE_Fountain (events 4000..4198) */
#define FXTRN(j) (&g_fxvoice[497 + (j)])
#define FXFNT(j) (&g_fxvoice[503 + (j)])
static CarSfxPlane g_trn_in[TRN_MAX];
static int g_trn_n, g_trn_st[TRN_MAX];      /* the entry's +8: 0 new, 1 found, 2 never found */
static int g_env_ev;
static struct { int on, entry; float pos[3]; Pos3D p3; } g_trn[TRN_STATES];
static struct { int on, kind, pt; Pos3D p3; } g_fnt[FNT_STATES];
/* 0x7f0d70 (kind 0, 7 points) and 0x7f0b80 (kind 1, 31 points): x y z, 16 bytes */
static const float k_fnt0[7][3] = {
    { -154, -615, 16 }, { -337, -702, 19 }, { -457, -1518, 13 }, { -133, -1388, 15 }, { -2662, 2237, 229 },
    { -1641, 532, 8 }, { -883, 806, 40 } };
static const float k_fnt1[31][3] = {
    { -106.65f, -575.58f, 17.5f }, { -113.42f, -571.36f, 18.9f }, { -97.8f, -571.66f, 18.86f }, { 102.52f, 581.0f, 17.0f },
    { -93.45f, -575.83f, 17.0f }, { -100.22f, -587.22f, 15.5f }, { -92.1f, -579.93f, 15.5f }, { 137, -642, 17 },
    { -104, -653, 15 }, { -161, -624, 16 }, { -343, -561, 20 }, { -326, -55, 20 }, { -312, -564, 20 }, { -309, -534, 20 },
    { -339, -530, 20 }, { -785, -285, 23 }, { -432, -112, 15 }, { -233, -283, 21 }, { -202, -280, 21 }, { -571, -154, 20 },
    { -580, -163, 20 }, { -571, -143, 19 }, { -564, -126, 16 }, { -526, -27, 13 }, { 603, -177, 15 }, { -2134, 1687, 131 },
    { -1500, 1143, 64 }, { -1214, 498, 19 }, { -813, 725, 36 }, { -841, 717, 36 }, { -123, 1499, 40 } };
static const float *fnt_pt(int kind, int i) { return kind ? k_fnt1[i] : k_fnt0[i]; }
void carsfx_set_trains(const CarSfxPlane *p, int n)
{
    if (n <= 0) { if (g_trn_n) memset(g_trn_st, 0, sizeof g_trn_st); g_trn_n = 0; return; }
    g_trn_n = n > TRN_MAX ? TRN_MAX : n;
    memcpy(g_trn_in, p, sizeof *p * (size_t)g_trn_n);
}
void carsfx_set_event(int ev) { g_env_ev = ev; }

/* ---- CARSFX_DriveBy (0x474930; UpdateParams 0x474a20, ProcessUpdate
   0x47e0d0) on SFXCTL_Tunnel's look-ahead triggers (UpdateDriveBySnds
   0x471080): MB_Stitch_Whoosh stitches, entry slots 0/1 (+0xb4, mix slot
   0), exit slots 2/3 (+0xe4, mix slot 4) ---------------------------------- */
#define DB_NV 4
#define FXDB(j) (&g_fxvoice[507 + DB_NV * (j)])
static struct {
    float t_in, t_out;          /* +0x94, +0xa4 */
    int in_on, out_on;          /* +0x7c, +0x9c: this frame's triggers */
    const void *z_in, *z_out;   /* +0x98, +0xa8 */
    float pos[3];               /* +0x80 */
    float s_in, s_out;          /* +0x90, +0xa0 */
    int probe;                  /* +0x74 */
} g_db;
static int g_db_on[4], g_db_last = -1;      /* +0xb4/+0xb8, +0xe4/+0xe8; +0x114 */
static unsigned g_db_az[4];
static StitchPlay g_db_sp[4];
static unsigned g_db_n5, g_db_n6, g_db_nx;  /* 0x82cb54, 0x82cb58, 0x82cb50 */
static Pos3D g_db_p3;
static float db_slope(float speed)          /* Slope 0x7f0b40: 0..1 over 18..45 (static init 0x77c5b0) */
{
    float t = (speed - 18.0f) / (45.0f - 18.0f);
    t = t > 0 ? (t < 1 ? t : 1) : 0;
    return t;
}
void carsfx_set_driveby(int probe_type, const void *zone, const float probe[3], float car_z, float speed, float dt)
{
    /* g_tun_type is still last frame's +0x78 (+0x70 = in a tunnel): retail
       runs this before UpdateParams' own zone search */
    int in_tun = g_tun_type != 0;
    g_db.t_in -= dt; if (g_db.t_in < 0.0f) g_db.t_in = 0.0f;
    g_db.t_out -= dt; if (g_db.t_out < 0.0f) g_db.t_out = 0.0f;
    g_db.in_on = g_db.out_on = 0;
    g_db.probe = probe_type == 5 || probe_type == 6 || probe_type == 7 ? probe_type : 2;
    if (g_db.probe == 2) zone = NULL;
    float s = db_slope(speed);
    if (!in_tun) {
        if (g_db.probe != 2 && ((g_db.t_in < 0.01f && g_db.z_in == zone) || g_db.z_in != zone) && 0.01f < s) {
            g_db.t_in = 3.0f; g_db.in_on = 1; g_db.z_in = zone;     /* DAT_007886f0 */
            g_db.pos[0] = probe[0]; g_db.pos[1] = probe[1]; g_db.pos[2] = car_z + 10.0f;
            g_db.s_in = s;
        }
        return;
    }
    if (g_db.probe == 2 && g_tun_type == 5 && ((g_db.t_out < 0.01f && g_db.z_out == zone) || g_db.z_out != zone) && 0.01f < s) {
        g_db.out_on = 1; g_db.t_out = 3.0f; g_db.z_out = zone;
        g_db.pos[0] = probe[0]; g_db.pos[1] = probe[1]; g_db.pos[2] = car_z + 10.0f;
        g_db.s_out = s;
    }
}
static void db_stop(int k) { stitch_stop(FXDB(k), &g_db_sp[k]); g_db_on[k] = 0; if (g_db_last == k) g_db_last = -1; }
static void player_driveby(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll) { for (int k = 0; k < 4; k++) if (g_db_on[k]) db_stop(k); g_db.in_on = g_db.out_on = 0; return; }
    /* GUESS: the player car's position for the object's 3D (the tunnel's
       +0x80 trigger point is not read by CARSFX_DriveBy) */
    float pc[3] = { L->car[0], L->car[1], 0 };
    pos3d_update(&g_db_p3, pc, L, dt);
    const int *d = g_db_p3.d; const unsigned *az = g_db_p3.az;
    if (g_db.in_on) {                       /* UpdateParams: an entry whoosh in a free slot */
        int k = !g_db_on[0] ? 0 : !g_db_on[1] ? 1 : -1;
        if (k >= 0) {
            int n = ftol(g_db.s_in * -0.0234375f);              /* 0x78b070 */
            unsigned *c = g_db.probe == 5 ? &g_db_n5 : &g_db_n6;
            int r = (int)(*c % 3u), idx = r + (15 - n) * 3;
            *c = (unsigned)r + 1;
            g_db_az[k] = 0;
            g_db_on[k] = stitch_start(FXDB(k), DB_NV, 1, idx, &g_db_sp[k]) > 0;
            if (g_db_on[k]) g_db_last = k;
            g_md_db = 1;
#ifdef OPENUG2_SIM_PRE_H
            if (getenv("OPENUG2_TUNNEL_LOG")) printf("[driveby] entry whoosh stitch %d (zone type %d, slope %.2f) slot %d vol %d\n",
                                                    idx, g_db.probe, g_db.s_in, k, mix_objvol(1, 0x400100c0u, 0, L->mode, d, az));
#endif
        }
    }
    if (g_db.out_on) {                      /* an exit whoosh */
        int k = !g_db_on[2] ? 2 : !g_db_on[3] ? 3 : -1;
        if (k >= 0) {
            int n = ftol(g_db.s_out * -0.015625f);              /* 0x78b06c */
            int r = (int)(g_db_nx % 3u), idx = r + (18 - n) * 3;
            g_db_nx = (unsigned)r + 1;
            g_db_on[k] = stitch_start(FXDB(k), DB_NV, 1, idx, &g_db_sp[k]) > 0;
            if (g_db_on[k]) g_db_last = k;
            g_md_db = 1;
#ifdef OPENUG2_SIM_PRE_H
            if (getenv("OPENUG2_TUNNEL_LOG")) printf("[driveby] exit whoosh stitch %d (slope %.2f) slot %d vol %d\n",
                                                    idx, g_db.s_out, k, mix_objvol(1, 0x400100c0u, 4, L->mode, d, az));
#endif
        }
    }
    g_db.in_on = g_db.out_on = 0;
    for (int k = 0; k < 4; k++) {           /* ProcessUpdate */
        if (!g_db_on[k]) continue;
        int slot = k < 2 ? 0 : 4;
        /* the entry's azimuth only while it is the last one started; the exit's always */
        if (k >= 2 || g_db_last == k) g_db_az[k] = mix_objaz(1, 0x400100c0u, slot, L->mode, az);
        int vol = mix_objvol(1, 0x400100c0u, slot, L->mode, d, az);
        if (!stitch_update(FXDB(k), &g_db_sp[k], vol, g_db_az[k], ref)) db_stop(k);
    }
}

static float em_dist(const float a[3], const float b[3]) { return pl_dist(a, b); }
static void trn_detach(int s) { fx_stop(FXTRN(s)); g_trn[s].on = 0; }
static void fnt_detach(int s) { fx_stop(FXFNT(s)); g_fnt[s].on = 0; }
static void emitters_reset(void)
{
    for (int s = 0; s < TRN_STATES; s++) trn_detach(s);
    for (int s = 0; s < FNT_STATES; s++) fnt_detach(s);
    for (int k = 0; k < 4; k++) if (g_db_on[k]) db_stop(k);
    memset(g_trn_st, 0, sizeof g_trn_st);
    memset(&g_db, 0, sizeof g_db);
}
static void player_emitters(float dt, const CarSfxListener *L, float ref)
{
    if (!L->coll) {
        for (int s = 0; s < TRN_STATES; s++) if (g_trn[s].on) trn_detach(s);
        for (int s = 0; s < FNT_STATES; s++) if (g_fnt[s].on) fnt_detach(s);
        return;
    }
    /* ENV_STATIC's variant table (0x90c): train 0 -> ENV_COMMON clip 0, fountain 4 -> clip 2 */
    static const unsigned char k_var[10] = { 1, 2, 0, 0, 3, 4, 5, 6, 7, 8 };
    /* CSTATEMGR_Trains::UpdateParams 0x464600 */
    for (int e = 0; e < g_trn_n; e++) {
        if (g_trn_st[e] == 2) continue;
        if (!g_trn_in[e].ok) { g_trn_st[e] = 2; continue; }   /* GetAnimPosition false: never again */
        g_trn_st[e] = 1;
        int near = em_dist(g_trn_in[e].pos, g_pl_car) < 400.0f;   /* 0x78a54c */
        int s = 0;
        while (s < TRN_STATES && !(g_trn[s].on && g_trn[s].entry == e)) s++;
        if (s < TRN_STATES) { if (!near || g_trn_in[e].delay) trn_detach(s); continue; }
        if (!near || g_trn_in[e].delay) continue;
        for (s = 0; s < TRN_STATES && g_trn[s].on; s++) {}
        if (s == TRN_STATES) {
            float dn = em_dist(g_trn_in[e].pos, g_pl_car);
            for (s = 0; s < TRN_STATES; s++)
                if (em_dist(g_trn_in[g_trn[s].entry].pos, g_pl_car) > dn) { trn_detach(s); break; }
        }
        if (s < TRN_STATES) {                                     /* NewAttachment 0x463020: ENV_STATIC {0} at once */
            g_trn[s].on = 1; g_trn[s].entry = e;
            memcpy(g_trn[s].pos, g_trn_in[e].pos, sizeof g_trn[s].pos);
            int clip = k_var[0] - 1;
            fx_start(FXTRN(s), AFX_ENV, clip, audio_fx_loop(AFX_ENV, clip), 0, 1);
        }
    }
    for (int s = 0; s < TRN_STATES; s++) {
        if (!g_trn[s].on) continue;
        if (g_trn_in[g_trn[s].entry].ok) memcpy(g_trn[s].pos, g_trn_in[g_trn[s].entry].pos, sizeof g_trn[s].pos);
        pos3d_update(&g_trn[s].p3, g_trn[s].pos, L, dt);
        const int *d = g_trn[s].p3.d; const unsigned *az = g_trn[s].p3.az;
        AudioFxVoice *v = FXTRN(s);                               /* ProcessUpdate 0x4631e0 */
        v->gain = fx_gain(mix_objvol(8, 0x40080000u, 0, L->mode, d, az), ref, 0.6f);
        v->pitch = 1.0f;                                          /* +0xc 0x1000 */
        v->pan = az_pan(mix_objaz(8, 0x40080000u, 0, L->mode, az));
    }
    /* CSTATEMGR_Enviro::UpdateParams 0x4647e0: events 4000..4199 (the states
       exist only for 4000..4198, EnterWorld 800ab6b0) */
    if (g_env_ev - 4000 >= 0 && g_env_ev - 4000 < 199) {
        for (int kind = 0; kind < 2; kind++) {
            int np = kind ? 31 : 7;
            for (int i = 0; i < np; i++) {
                const float *pt = fnt_pt(kind, i);
                int near = em_dist(pt, g_pl_car) < 150.0f;       /* 0x78a584 */
                int s = 0;
                while (s < FNT_STATES && !(g_fnt[s].on && g_fnt[s].kind == kind && g_fnt[s].pt == i)) s++;
                if (s < FNT_STATES) { if (!near) fnt_detach(s); continue; }
                if (!near) continue;
                for (s = 0; s < FNT_STATES && g_fnt[s].on; s++) {}
                if (s == FNT_STATES) {
                    float dn = em_dist(pt, g_pl_car);
                    for (s = 0; s < FNT_STATES; s++)
                        if (em_dist(fnt_pt(g_fnt[s].kind, g_fnt[s].pt), g_pl_car) > dn) { fnt_detach(s); break; }
                }
                if (s < FNT_STATES) {                             /* NewAttachment 0x463920: ENV_STATIC {4} */
                    g_fnt[s].on = 1; g_fnt[s].kind = kind; g_fnt[s].pt = i;   /* +0x44 = kind */
                    int clip = k_var[4] - 1;
                    fx_start(FXFNT(s), AFX_ENV, clip, audio_fx_loop(AFX_ENV, clip), 0, 1);
                }
            }
        }
    } else for (int s = 0; s < FNT_STATES; s++) if (g_fnt[s].on) fnt_detach(s);
    for (int s = 0; s < FNT_STATES; s++) {
        if (!g_fnt[s].on) continue;
        pos3d_update(&g_fnt[s].p3, fnt_pt(g_fnt[s].kind, g_fnt[s].pt), L, dt);
        const int *d = g_fnt[s].p3.d; const unsigned *az = g_fnt[s].p3.az;
        AudioFxVoice *v = FXFNT(s);                               /* ProcessUpdate 0x463820 */
        int k = g_fnt[s].kind;
        v->gain = fx_gain(mix_objvol(4, 0x40040000u, k, L->mode, d, az), ref, 0.6f);
        v->pitch = (float)(k ? 4000 : 0x1450) / 4096.0f;
        v->pan = az_pan(mix_objaz(4, 0x40040000u, k, L->mode, az));
    }
#ifdef OPENUG2_SIM_PRE_H
    static float logt;
    if ((logt += dt) >= 2.0f && getenv("OPENUG2_PLANE_LOG")) {
        logt = 0.0f;
        int nok = 0;
        for (int e = 0; e < g_trn_n; e++) nok += g_trn_st[e] == 1;
        printf("[trains] %d of %d found", nok, g_trn_n);
        for (int s = 0; s < TRN_STATES; s++)
            if (g_trn[s].on) printf("; state %d entry %d at %.0f m vol %d", s, g_trn[s].entry, em_dist(g_trn[s].pos, g_pl_car),
                                   mix_objvol(8, 0x40080000u, 0, L->mode, g_trn[s].p3.d, g_trn[s].p3.az));
        printf("\n[fountains] event %d", g_env_ev);
        for (int s = 0; s < FNT_STATES; s++)
            if (g_fnt[s].on) printf("; state %d kind %d point %d at %.0f m vol %d", s, g_fnt[s].kind, g_fnt[s].pt,
                                   em_dist(fnt_pt(g_fnt[s].kind, g_fnt[s].pt), g_pl_car),
                                   mix_objvol(4, 0x40040000u, g_fnt[s].kind, L->mode, g_fnt[s].p3.d, g_fnt[s].p3.az));
        printf("\n");
    }
#endif
}

int carsfx_player_car(const char *dataroot, int row)
{
    sw_free();                                          /* (shares CAR_Sputter's file) */
    free(g_sp.file); free(g_sp.mem);
    memset(&g_sp, 0, sizeof g_sp);
    g_sp.vbase = 334; g_sp.set = AFX_SWTN;
    for (int p = 0; p < 2; p++) fx_stop(SPV(p));
    if (row < 0 || row >= 33) row = 18;
    g_prow = row;
    player_skid_sync(0);                                /* 0x465b90: the row's engine class (inside audio_hold) */
    /* EAXCar +0x7c: the 0x7f2230 record at row x 4 + the engine sound level
       +0x94 (CarPreSetup 0x4593b0) */
    int slot = audio_row_engine_slot(row, audio_player_engine_level());
    /* 0x7f3040 {name, ?, volume} by +0x7c (0x45f6e0; > 0x2b -> entry 0): slots 0..40 -> SWTN_CAR_<slot>,
       41 / 42 -> SWTN_CAR_14 / SWTN_CAR_12 with their own volumes */
    static const unsigned short k_swtn_vol[43] = {
        18500, 18000, 14500, 12500, 18500, 19500, 22500, 20000, 22000, 24500, 20000, 22000, 18000, 14000,
        14000, 18000, 20000, 24500, 14500, 13750, 13000, 18500, 16000, 15000, 16000, 20000, 20000, 20000,
        24000, 24000, 22000, 16500, 20000, 16500, 17500, 17500, 19500, 22000, 18500, 18000, 17000, 17000, 18000 };
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/ENGINE/SWTN_CAR_%02d_MB.abk", dataroot, audio_slot_file(slot));
    int nclip = audio_load_swtn_bank(path, &g_sp.loops);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); g_sp.flen = ftell(f); fseek(f, 0, SEEK_SET);
    g_sp.file = g_sp.flen > 0x100 ? (unsigned char *)malloc((size_t)g_sp.flen) : NULL;
    int ok = g_sp.file && fread(g_sp.file, 1, (size_t)g_sp.flen, f) == (size_t)g_sp.flen;
    fclose(f);
    const unsigned char *d = g_sp.file;
    long cls = -1;
    if (ok && !memcmp(d, "ABKC", 4)) {
        long t = ae_i(d + 0x38);
        int n = t > 0 && t + 4 < g_sp.flen ? ae_i(d + t) : 0;
        for (int k = 0; k < n && t + 16 + 12 * k <= g_sp.flen; k++) {
            long nm = ae_i(d + t + 8 + 12 * k);
            if (nm > 0 && nm + 16 < g_sp.flen && !strcmp((const char *)d + nm + 4, "CAR_Sputter")) cls = ae_i(d + t + 4 + 12 * k);
        }
    }
    if (cls < 0 || cls + 0x60 > g_sp.flen) { printf("[sputter] %s: no CAR_Sputter\n", path); return 0; }
    const unsigned char *md = d + cls + 0x20;
    int code = ae_i(md + 4), tmpl = ae_i(md + 8), size = ae_i(md + 0xc);
    if (code <= 0 || tmpl <= 0 || size <= 0x60 || tmpl + size > g_sp.flen) return 0;
    g_sp.mem = (unsigned char *)malloc((size_t)size);
    if (!g_sp.mem) { printf("[sputter] %s: out of memory\n", path); return 0; }   /* audit 11-17 */
    memcpy(g_sp.mem, d + tmpl, (size_t)size);
    g_sp.msize = size;
    g_sp.nply = md[0] < 2 ? md[0] : 2;
    for (int p = 0; p < g_sp.nply; p++) g_sp.ply[p] = ae_i(md + 0x18 + 4 * p) - 0x18;
    g_sp.ins = g_sp_ins;
    g_sp.nins = ae_decode(d, g_sp.flen, code, g_sp_ins, AE_MAXINS);
    if (g_sp.nins > 0 && !ae_ops_ok(g_sp_ins, g_sp.nins, size)) g_sp.nins = -1;
    g_sp.idx = audio_slot_file(slot); g_sp.vscale = k_swtn_vol[slot]; g_sp.row = row;   /* idx: record +8 */
    g_sp.ok = g_sp.nins > 0;
    printf("[sputter] %s: CAR_Sputter %d instructions, %d players, %d samples (loops %x), vol %d\n",
           path, g_sp.nins, g_sp.nply, nclip, g_sp.loops, g_sp.vscale);
    sw_load(g_sp.file, g_sp.flen, g_sp.loops, slot);
    return g_sp.ok;
}

/* SetMemberData: the member data at state +0x28 and the update byte */
static void sp_member(const CarSfxListener *L, int vol, int az)
{
    unsigned char *m = g_sp.mem + 0x18 + 0x28;
    int rpm = ftol(L->prpm), thr = ftol(L->pthr * 10.24f);
    ae_w(m, g_sp.idx);
    ae_w(m + 4, 1);                                     /* this */
    ae_w(m + 8, rpm < 0 ? 0 : rpm > 10000 ? 10000 : rpm);
    ae_w(m + 0xc, vol < 0 ? 0 : vol > 0x8013 ? 0x8013 : vol);
    ae_w(m + 0x10, az < 0 ? 0 : az > 0x10000 ? 0x10000 : az);
    ae_w(m + 0x14, 0); ae_w(m + 0x18, 0);
    ae_w(m + 0x1c, thr < 0 ? 0 : thr > 0x400 ? 0x400 : thr);
    ae_w(m + 0x20, g_sp.toggle);
    ae_w(m + 0x28, 1);                                  /* CSTATE +0xb0 +0x78 (GUESS: set) */
    m[0x2c] = 1;
}

static void player_sputter(float dt, const CarSfxListener *L, float ref)
{
    if (!g_sp.ok || g_sp.dead || !L->pwheels) {
        if (g_sp.ok) for (int p = 0; p < 2; p++) if (SPV(p)->on) fx_stop(SPV(p));
        g_sp.flag = 0;
        g_sput_out = 0;
        return;
    }
    pos3d_update(&g_sp_p3, L->car, L, dt);
    int vol = mix_objvol(1, 0x40010040u, 0, L->mode, g_sp_p3.d, g_sp_p3.az) * g_sp.vscale >> 15;
    int az = (int)mix_objaz(1, 0x40010040u, 0, L->mode, g_sp_p3.az);   /* vt+0x3c 0x45e3b0 */
    if (L->pspark) g_sp.flag = 1;                       /* handler 0x469bc0: CSTATE +0xf0 */
    if (g_sp.flag && (L->pgear == 1 || L->pgear == 2)) {   /* CSTATE +0x50 = gear */  /* UpdateParams 0x45f780 */
        if (g_sp.toggle == 1) g_sp.toggle = 0;
        else { g_sp.flag = 0; g_sp.toggle = 1; }
        printf("[sputter] event 9 -> toggle %d (rpm %.0f, vol %d)\n", g_sp.toggle, L->prpm, vol);
        sp_member(L, vol, az);
    }
    sp_member(L, vol, az);                              /* ProcessUpdate 0x45f800 */
    ae_tick(dt, ref);
    g_sput_out = g_sp.out;                             /* -> +0xf8 (callback 0x472cd0) */
}

void carsfx_update(float dt, const CarSfxListener *L, const CarSfxCar *ai, int nai,
                   const CarSfxCar *tr, int ntr)
{
    if (dt <= 0) return;
    g_fxtime += dt;
    md_update(L, dt);                                   /* the mixmap's events, ctls and channel sums */
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
            ai_patch_stop(k);
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
        /* +0x40 = vt+0x3c(0) = 0x45e3b0 GetAzimOutput (slot 0's 3D azimuth),
           clamped 0..0x10000 (0x461270; the object's vtable starts at
           0x78aee0, so 0x461490 at 0x78af3c is vt+0x5c, not the azimuth) */
        v->pan = az_pan(mix_objaz(2, 0x40020000u, 0, L->mode, a->p3.az));
        v->on = 1;
        AiFx *x = &g_aifx[k];
        if (!x->init) { x->init = 1; x->t_on = c->turbo && x->t_set >= 0; }
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_AI_FX_TEST")) x->t_on = x->t_set >= 0;   /* sim test aid: every AI car turbo */
#endif
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
        v->pan = az_pan(mix_objaz(3, 0x40030000u, 0, L->mode, t->p3.az));   /* +0x10 = vt+0x3c(0): FX_TRAFFIC's player azimuth */
        v->on = 1;
        tr_fx_update(i, &g_trfx[i], &t->p3, &tr[t->car], L, dt, ref);
    }
    if (g_co_live) co_update(dt, L, ref);
    player_turbo(dt, L, ref);
    player_road(dt, L, ref);
    player_bottom(dt, L, ref);
    part_snd_update(L, ref);                            /* EAXCommon::Update 0x4815b0 */
    player_skid(dt, L, ref);                            /* CARSFX_Skids on the player's car */
    player_sputter(dt, L, ref);
    player_swtn(dt, L, ref);
    player_weather(dt, L, ref);   /* CARSFX_Rain, CARSFX_WindWeather */
    player_planes(dt, L, ref);    /* CSTATEMGR_Planes, SFXObj_Plane */
    player_emitters(dt, L, ref);  /* CSTATEMGR_Trains / SFXObj_Train, CSTATEMGR_Enviro / SFXObj_Fountain */
    player_driveby(dt, L, ref);   /* CARSFX_DriveBy */
}
