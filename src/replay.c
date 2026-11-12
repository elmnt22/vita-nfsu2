/* replay.c -- the retail NFSU2 instant replay (step 8b). See replay.h.
 *
 * Retail facts (SPEED2.EXE v1.2 / GC Speed.elf):
 *  - ReplayManager 0x8650b8: +0x14 paused, +0x18 slow motion, +0x1c skip
 *    this frame (BeginFrame 80199620: paused, or slow and frame % 4 != 0),
 *    +0x78 / +0x7c the first / present tick, +0x80 end of time, +0x84 /
 *    +0x88 the race clock (4000/s) at the start / present, +0xa8 = 2 instant
 *    snapshots, +0xb0 = 5 lap snapshots, +0xb4 = 0 highlights, +0xb8 / +0xbc
 *    the instant / lap counts, +0xc4 = 15 s the lap interval.
 *  - MaybeTakeInstantReplaySnapshot 801998e0: every 300 ticks; with no
 *    highlights the oldest INSTANT_REPLAY goes when 2 are kept.
 *  - MaybeTakeLapSnapshot 80199abc: when count * interval <= clock since the
 *    start; at 5: interval * 2 and LAP_SNAPSHOT 1 and 3 deleted.
 *  - EndFrame 80199c04: recording, present = tick + 1; replaying, at the
 *    present's last tick paused and message 0x7c8d5450 (the UI shows).
 *  - ReplayMenu (UI_ReplayControl.fng): ctor 0x4c0aa0, StartLoading
 *    0x4a4bf0, ServiceLoading 0x4c10b0, JumpBack 0x4a4ca0, JumpForward
 *    0x4a4dd0, TogglePause 0x4a4e80, SlowMotion 0x4a4fb0, ChooseCamera
 *    0x4a5090, Exit 0x4a50e0, Hide 0x4a5200, StartExiting 0x4c1220,
 *    notifications 0x4d7bd0.
 *  - Player::CycleReplayCameraType 0x5fafd0 (+0x21c = (t+1) % 6, POVs
 *    0x802130 {0,2,4,1,0,5}), UpdateIceCameras 0x5fb050, the restore's
 *    camera reset 0x5faf80, bRandom 0x43c1f0 (seed 0x7ea624 = 0x12345678).
 */
#include "replay.h"
#include "hud.h"
#include "feng.h"
#include "smokeable.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the recording -------------------------------------------------------- */
/* GUESS: the budget. Retail sizes its input log from the free memory (GC
   MarkTheBeginningOfTime: min(free - 8 snapshots of 0x171e8 B, 0x40000));
   the port's kinematic frames are larger, so it takes 3 MB (halved while
   the allocation fails) and halves its frame rate whenever that fills. */
#define RP_BUDGET    (3u << 20)
#define RP_MINBUF    (256u << 10)
#define RP_MAXF      16384           /* frame index entries */
#define RP_VB_MAX    1024            /* smokeable virgin-set changes */
#define RP_INST_MAX  2               /* +0xa8 */
#define RP_LAP_MAX   5               /* +0xb0 */
#define RP_LAP_IV    15.0f           /* +0xc4 */
#define RP_MARK_MAX  10              /* REPLAY_TIME_1..10 */

typedef struct { int tick; float t; } RpMark;

static struct {
    int on, ended;                   /* MarkTheBeginningOfTime / +0x80 */
    unsigned char *buf; size_t cap, used;
    int *ftick; size_t *foff; int nf;
    int stride;                      /* ticks per stored frame (2, then doubling) */
    int present;                     /* +0x7c: ticks recorded */
    float t0, tnow;                  /* +0x84 / +0x88 in s */
    int next_inst;                   /* +0x74 */
    RpMark inst[RP_INST_MAX]; int ninst;
    RpMark lap[RP_LAP_MAX]; int nlap; float lap_iv;
    RpFrame last; int last_tick, last_stored;
    int vb_tick[RP_VB_MAX]; unsigned char *vb[RP_VB_MAX]; int nvb, vb_bytes;
    unsigned char *vtmp;
    int thinned;
    float botacc[RP_SLOTS];          /* bump-stop peaks since the last stored frame */
    int *evtick; RpContact *ev; int nev, evfull;   /* contact effects, in tick order */
    int fbrake, fskid, fslip, fbot;  /* stored car-frames with those (the log) */
    unsigned char *sb; size_t sbn; int nsnd, sbfull;   /* sound calls: [tick u32][len u16][pad][bytes] */
} R;
#define RP_EV_MAX    1024            /* contact effects kept (64 KB) */
#define RP_SB_CAP    (512u << 10)    /* sound calls kept (q5 races: 60-200 KB/min, ~2.6 min worst seen) */
void (*rp_contact_fx)(const RpContact *c);
void (*rp_sound_fx)(const void *ev, int len);
void (*rp_restore_fx)(void);

static int g_rp_log = -1;            /* OPENUG2_RP_LOG: positions at every 300th tick */
static int rp_logging(void)
{
    if (g_rp_log < 0) g_rp_log = getenv("OPENUG2_RP_LOG") != NULL;
    return g_rp_log;
}

static void cam_reset(void);
static void vb_free(void)
{
    for (int i = 0; i < R.nvb; i++) free(R.vb[i]);
    R.nvb = 0;
    free(R.vtmp); R.vtmp = NULL;
}

void rp_reset(void)
{
    free(R.buf); free(R.ftick); free(R.foff); free(R.evtick); free(R.ev); free(R.sb);
    vb_free();
    memset(&R, 0, sizeof R);
    cam_reset();                     /* a new race: a new Player (+0x21c = 0) */
    smk_rp_off();
}

int rp_begin(float t)
{
    rp_reset();
    size_t cap = RP_BUDGET;
    while (cap >= RP_MINBUF && !(R.buf = (unsigned char *)malloc(cap))) cap >>= 1;
    R.ftick = (int *)malloc(RP_MAXF * sizeof *R.ftick);
    R.foff = (size_t *)malloc(RP_MAXF * sizeof *R.foff);
    R.evtick = (int *)malloc(RP_EV_MAX * sizeof *R.evtick);
    R.ev = (RpContact *)malloc(RP_EV_MAX * sizeof *R.ev);
    R.sb = (unsigned char *)malloc(RP_SB_CAP);
    if (!R.buf || !R.ftick || !R.foff || !R.evtick || !R.ev || !R.sb) {          /* GC: < 0x2800 B free -> no replay */
        printf("replay: no memory for the recording\n");
        rp_reset();
        return 0;
    }
    R.cap = cap; R.on = 1; R.stride = 2;
    R.t0 = R.tnow = t; R.lap_iv = RP_LAP_IV;
    R.last_tick = -1;
    printf("replay: recording from race clock %.2f s (%u KB buffer)\n", t, (unsigned)(cap >> 10));
    return 1;
}

int rp_recording(void) { return R.on && !R.ended; }
int rp_lap_markers(void) { return R.on ? R.nlap : 0; }
int rp_instant_markers(void) { return R.on ? R.ninst : 0; }

/* ---- frame coding ---------------------------------------------------------- */
static void put(unsigned char **p, const void *v, size_t n) { memcpy(*p, v, n); *p += n; }
static void put16(unsigned char **p, int v)
{
    if (v > 32767) v = 32767; if (v < -32768) v = -32768;
    int16_t s = (int16_t)v; put(p, &s, 2);
}
static void putu16(unsigned char **p, float v) { int i = (int)(v + 0.5f); if (i < 0) i = 0; if (i > 65535) i = 65535; uint16_t u = (uint16_t)i; put(p, &u, 2); }
static void put8(unsigned char **p, int v) { if (v > 127) v = 127; if (v < -128) v = -128; signed char c = (signed char)v; put(p, &c, 1); }
static void putu8(unsigned char **p, int v) { if (v < 0) v = 0; if (v > 255) v = 255; unsigned char c = (unsigned char)v; put(p, &c, 1); }
static int rnd(float v) { return (int)lrintf(v); }

static void get(const unsigned char **p, void *v, size_t n) { memcpy(v, *p, n); *p += n; }
static int get16(const unsigned char **p) { int16_t s; get(p, &s, 2); return s; }
static int getu16(const unsigned char **p) { uint16_t s; get(p, &s, 2); return s; }
static int get8(const unsigned char **p) { signed char c; get(p, &c, 1); return c; }
static int getu8(const unsigned char **p) { unsigned char c; get(p, &c, 1); return c; }

/* a rotation (3x3, rows) as a unit quaternion and back */
static void m2q(const float *m, float q[4])
{
    float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (m[7] - m[5]) / s; q[1] = (m[2] - m[6]) / s; q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        q[3] = (m[7] - m[5]) / s; q[0] = 0.25f * s; q[1] = (m[1] + m[3]) / s; q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        q[3] = (m[2] - m[6]) / s; q[0] = (m[1] + m[3]) / s; q[1] = 0.25f * s; q[2] = (m[5] + m[7]) / s;
    } else {
        float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        q[3] = (m[3] - m[1]) / s; q[0] = (m[2] + m[6]) / s; q[1] = (m[5] + m[7]) / s; q[2] = 0.25f * s;
    }
    float l = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (l < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; i++) q[i] /= l;
}
static void q2m(const float q[4], float *m)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2*(y*y + z*z); m[1] = 2*(x*y - z*w);     m[2] = 2*(x*z + y*w);
    m[3] = 2*(x*y + z*w);     m[4] = 1 - 2*(x*x + z*z); m[5] = 2*(y*z - x*w);
    m[6] = 2*(x*z - y*w);     m[7] = 2*(y*z + x*w);     m[8] = 1 - 2*(x*x + y*y);
}
static void putq(unsigned char **p, const float *m)
{
    float q[4]; m2q(m, q);
    for (int i = 0; i < 4; i++) put16(p, rnd(q[i] * 32767.0f));
}
static void getq(const unsigned char **p, float q[4])
{
    float l = 0;
    for (int i = 0; i < 4; i++) { q[i] = (float)get16(p) / 32767.0f; l += q[i] * q[i]; }
    l = sqrtf(l);
    if (l < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; i++) q[i] /= l;
}

/* car flags */
#define CF_NOS    1
#define CF_ENG    2
#define CF_LIGHT  4                  /* a traffic car: the short form */
#define CF_SKID   8                  /* tyre marks: skid u8 x4 *255, width u8 x4 *500 */
#define CF_SLIP   16                 /* sliding patches: slipv i16 x8 *100 */
#define CF_BOT    32                 /* bump-stop sum f32 */
#define CF_SND    64                 /* sound: skid channels u8 x3 *255, RPS_* u8 */

/* the frame: tick u32, clock f32, lap u8, lap time f32, ncar u8, nprop u8,
   cars (slot u8, flags u8, pos f32x3, quat i16x4, vel i16x3 *50, then the
   full or short form), props (spawner u16, pos f32x3, quat i16x4) */
static size_t encode(unsigned char *o, int tick, const RpFrame *f, int np, const uint16_t *sp, const float (*M)[16])
{
    unsigned char *p = o;
    int nc = 0;
    for (int s = 0; s < RP_SLOTS; s++) nc += f->car[s].on != 0;
    uint32_t t32 = (uint32_t)tick; put(&p, &t32, 4);
    put(&p, &f->t, 4);
    putu8(&p, f->lap);
    put(&p, &f->laptime, 4);
    putu8(&p, nc); putu8(&p, np);
    for (int s = 0; s < RP_SLOTS; s++) {
        const RpCar *c = &f->car[s];
        if (!c->on) continue;
        int light = s >= RP_TR0, skid = 0, slip = 0;
        for (int w = 0; w < 4; w++) {
            skid |= c->wheel_skid[w] > 0.0f;
            slip |= rnd(c->wheel_slipv[w][0] * 100.0f) != 0 || rnd(c->wheel_slipv[w][1] * 100.0f) != 0;
        }
        int fl = (c->nos_on ? CF_NOS : 0) | (c->engaged ? CF_ENG : 0) | (light ? CF_LIGHT : 0);
        int snd = c->snd || rnd(c->skid3[0] * 255.0f) || rnd(c->skid3[1] * 255.0f) || rnd(c->skid3[2] * 255.0f);
        if (!light) fl |= (skid ? CF_SKID : 0) | (slip ? CF_SLIP : 0) | (c->bottom > 0.0f ? CF_BOT : 0) | (snd ? CF_SND : 0);
        putu8(&p, s);
        putu8(&p, fl);
        putu8(&p, c->lights);
        put(&p, c->pos, 12);
        putq(&p, c->rot);
        for (int i = 0; i < 3; i++) put16(&p, rnd(c->vel[i] * 50.0f));
        put16(&p, rnd(c->speed * 50.0f));
        put8(&p, rnd(c->wheel_steer[0] * 160.0f)); put8(&p, rnd(c->wheel_steer[1] * 160.0f));
        for (int w = 0; w < 4; w++) put8(&p, rnd(c->wheel_comp[w] * 500.0f));
        int cb = 0;
        for (int w = 0; w < 4; w++) cb |= (c->wheel_contact[w] != 0) << w;
        putu8(&p, cb);
        if (light) { put8(&p, c->vis); continue; }
        putu16(&p, c->rpm); putu16(&p, c->rpm_tach);
        putu8(&p, rnd(c->throttle * 255.0f)); put8(&p, c->gear); putu8(&p, c->wheels_down);
        for (int w = 0; w < 4; w++) put16(&p, rnd(c->wheel_w[w] * 10.0f));
        for (int w = 0; w < 4; w++) putu8(&p, rnd(c->wheel_slip[w] * 255.0f));
        for (int w = 0; w < 4; w++) putu8(&p, c->wheel_surface[w]);
        if (fl & CF_SKID) {
            for (int w = 0; w < 4; w++) putu8(&p, rnd(c->wheel_skid[w] * 255.0f));
            for (int w = 0; w < 4; w++) putu8(&p, rnd(c->wheel_skid_w[w] * 500.0f));
        }
        if (fl & CF_SLIP)
            for (int w = 0; w < 4; w++) { put16(&p, rnd(c->wheel_slipv[w][0] * 100.0f)); put16(&p, rnd(c->wheel_slipv[w][1] * 100.0f)); }
        if (fl & CF_BOT) put(&p, &c->bottom, 4);
        if (fl & CF_SND) { for (int i = 0; i < 3; i++) putu8(&p, rnd(c->skid3[i] * 255.0f)); putu8(&p, c->snd); }
    }
    for (int i = 0; i < np; i++) {
        uint16_t s16 = sp[i]; put(&p, &s16, 2);
        put(&p, &M[i][12], 12);
        float R9[9] = { M[i][0], M[i][1], M[i][2], M[i][4], M[i][5], M[i][6], M[i][8], M[i][9], M[i][10] };
        putq(&p, R9);
    }
    return (size_t)(p - o);
}
/* the worst case of one frame */
#define RP_FRAME_MAX (18 + RP_SLOTS * 104 + RP_PROPS * 24)

typedef struct {
    int tick; RpFrame f;
    int np; uint16_t sp[RP_PROPS]; float q[RP_PROPS][4]; float pp[RP_PROPS][3];
} RpDec;

static void decode(int fi, RpDec *d)
{
    const unsigned char *p = R.buf + R.foff[fi];
    memset(d, 0, sizeof *d);
    uint32_t t32; get(&p, &t32, 4); d->tick = (int)t32;
    get(&p, &d->f.t, 4);
    d->f.lap = getu8(&p);
    get(&p, &d->f.laptime, 4);
    int nc = getu8(&p), np = getu8(&p);
    for (int s = 0; s < RP_SLOTS; s++) d->f.car[s].vis = -1;
    for (int k = 0; k < nc; k++) {
        int s = getu8(&p), fl = getu8(&p);
        RpCar tmp, *c = s < RP_SLOTS ? &d->f.car[s] : &tmp;
        c->on = 1;
        c->nos_on = (fl & CF_NOS) != 0; c->engaged = (fl & CF_ENG) != 0;
        c->lights = (unsigned char)getu8(&p);
        get(&p, c->pos, 12);
        float q[4]; getq(&p, q); q2m(q, c->rot);
        for (int i = 0; i < 3; i++) c->vel[i] = (float)get16(&p) / 50.0f;
        c->speed = (float)get16(&p) / 50.0f;
        c->wheel_steer[0] = (float)get8(&p) / 160.0f; c->wheel_steer[1] = (float)get8(&p) / 160.0f;
        for (int w = 0; w < 4; w++) c->wheel_comp[w] = (float)get8(&p) / 500.0f;
        int cb = getu8(&p);
        for (int w = 0; w < 4; w++) c->wheel_contact[w] = (cb >> w) & 1;
        c->vis = -1;
        if (fl & CF_LIGHT) { c->vis = (signed char)get8(&p); continue; }
        c->rpm = (float)getu16(&p); c->rpm_tach = (float)getu16(&p);
        c->throttle = (float)getu8(&p) / 255.0f; c->gear = get8(&p); c->wheels_down = getu8(&p);
        for (int w = 0; w < 4; w++) c->wheel_w[w] = (float)get16(&p) / 10.0f;
        for (int w = 0; w < 4; w++) c->wheel_slip[w] = (float)getu8(&p) / 255.0f;
        for (int w = 0; w < 4; w++) c->wheel_surface[w] = getu8(&p);
        if (fl & CF_SKID) {
            for (int w = 0; w < 4; w++) c->wheel_skid[w] = (float)getu8(&p) / 255.0f;
            for (int w = 0; w < 4; w++) c->wheel_skid_w[w] = (float)getu8(&p) / 500.0f;
        }
        if (fl & CF_SLIP)
            for (int w = 0; w < 4; w++) { c->wheel_slipv[w][0] = (float)get16(&p) / 100.0f; c->wheel_slipv[w][1] = (float)get16(&p) / 100.0f; }
        if (fl & CF_BOT) get(&p, &c->bottom, 4);
        if (fl & CF_SND) { for (int i = 0; i < 3; i++) c->skid3[i] = (float)getu8(&p) / 255.0f; c->snd = (unsigned char)getu8(&p); }
    }
    d->np = np < RP_PROPS ? np : RP_PROPS;
    for (int i = 0; i < np; i++) {
        uint16_t s16; get(&p, &s16, 2);
        float pp[3], q[4]; get(&p, pp, 12); getq(&p, q);
        if (i < RP_PROPS) { d->sp[i] = s16; memcpy(d->pp[i], pp, sizeof pp); memcpy(d->q[i], q, sizeof q); }
    }
}

/* the store is full: every other frame goes and the frame rate halves (the
   lap snapshots' doubling, applied to the port's frames) */
static void thin(void)
{
    if (R.nf < 4) return;
    size_t w = 0; int n = 0;
    for (int i = 0; i < R.nf; i += 2) {
        size_t len = (i + 1 < R.nf ? R.foff[i + 1] : R.used) - R.foff[i];
        memmove(R.buf + w, R.buf + R.foff[i], len);
        R.ftick[n] = R.ftick[i]; R.foff[n] = w;
        w += len; n++;
    }
    R.nf = n; R.used = w; R.stride *= 2; R.thinned++;
    printf("replay: store full, every other frame dropped (%d frames, %u KB, one per %d ticks)\n",
           R.nf, (unsigned)(R.used >> 10), R.stride);
}

static int store(int tick, const RpFrame *f)
{
    static uint16_t sp[RP_PROPS]; static float M[RP_PROPS][16];
    int np = smk_rp_live(RP_PROPS, sp, M);
    for (int pass = 0; pass < 3; pass++) {
        if (R.nf < RP_MAXF && R.used + RP_FRAME_MAX <= R.cap) {
            R.ftick[R.nf] = tick; R.foff[R.nf] = R.used;
            R.used += encode(R.buf + R.used, tick, f, np, sp, (const float (*)[16])M);
            R.nf++;
            return 1;
        }
        thin();
    }
    return 0;
}

/* the smokeables' virgin set, kept when it changes */
static void store_virgins(int tick)
{
    int nsp = smk_rp_nsp(), nb = (nsp + 7) / 8;
    if (nsp <= 0) return;
    if (!R.vtmp || R.vb_bytes != nb) {
        free(R.vtmp); R.vtmp = (unsigned char *)malloc((size_t)nb);
        if (!R.vtmp) return;
        if (R.vb_bytes != nb) { for (int i = 0; i < R.nvb; i++) free(R.vb[i]); R.nvb = 0; }
        R.vb_bytes = nb;
    }
    smk_rp_virgins(R.vtmp);
    if (R.nvb && !memcmp(R.vb[R.nvb - 1], R.vtmp, (size_t)nb)) return;
    if (R.nvb == RP_VB_MAX) return;
    unsigned char *c = (unsigned char *)malloc((size_t)nb);
    if (!c) return;
    memcpy(c, R.vtmp, (size_t)nb);
    R.vb_tick[R.nvb] = tick; R.vb[R.nvb] = c; R.nvb++;
}

static void log_cars(const char *what, int tick, const RpFrame *f)
{
    printf("replay %s: tick %d clock %.3f", what, tick, f->t);
    for (int s = 0; s < RP_SLOTS; s++)
        if (f->car[s].on && (s < RP_TR0 || s == RP_TR0))
            printf(" | %d %.3f %.3f %.3f L%x S%.2f", s, f->car[s].pos[0], f->car[s].pos[1], f->car[s].pos[2],
                   f->car[s].lights, f->car[s].wheel_skid[0] + f->car[s].wheel_skid[1] + f->car[s].wheel_skid[2] + f->car[s].wheel_skid[3]);
    printf("\n");
}

static void end_log(void)
{
    float mins = (R.tnow - R.t0) / 60.0f;
    printf("replay: end of time at tick %d (clock %.2f s): %d frames, %u KB (+%d KB props), one frame per %d ticks, "
           "%.0f KB/min; markers %d instant, %d lap; %d contact effects; car-frames braking %d skidding %d "
           "sliding %d bottoming %d; %d sound calls (%u KB)\n",
           R.present, R.tnow, R.nf, (unsigned)(R.used >> 10), (R.nvb * R.vb_bytes) >> 10, R.stride,
           mins > 0.01f ? (float)(R.used >> 10) / mins * (float)R.stride / 2.0f : 0.0f, R.ninst, R.nlap, R.nev,
           R.fbrake, R.fskid, R.fslip, R.fbot, R.nsnd, (unsigned)(R.sbn >> 10));
}

void rp_end_of_time(void)
{
    if (!R.on || R.ended) return;
    if (R.last_tick >= 0 && !R.last_stored) store(R.last_tick, &R.last);
    R.ended = 1;
    end_log();
}

void rp_record(const RpFrame *f)
{
    if (!R.on || R.ended) return;
    int tick = R.present;
    R.tnow = f->t;
    /* MaybeTakeInstantReplaySnapshot: every 300 ticks, the oldest of 2 goes */
    if (tick >= R.next_inst) {
        R.next_inst = tick + 300;
        if (R.ninst >= RP_INST_MAX) { memmove(&R.inst[0], &R.inst[1], sizeof R.inst[0] * (RP_INST_MAX - 1)); R.ninst--; }
        R.inst[R.ninst].tick = tick; R.inst[R.ninst].t = f->t; R.ninst++;
    }
    /* MaybeTakeLapSnapshot */
    for (int again = 1; again; ) {
        again = 0;
        if (R.lap_iv * (float)R.nlap <= f->t - R.t0) {
            if (R.nlap == RP_LAP_MAX) {
                R.lap_iv += R.lap_iv;
                int k = 0;                  /* every second one deleted: 0, 2, 4 kept */
                for (int i = 0; i < R.nlap; i += 2) R.lap[k++] = R.lap[i];
                R.nlap = k;
                again = R.nlap < RP_LAP_MAX;
            } else { R.lap[R.nlap].tick = tick; R.lap[R.nlap].t = f->t; R.nlap++; }
        }
    }
    store_virgins(tick);
    R.last = *f; R.last_tick = tick; R.last_stored = 0;
    for (int s = 0; s < RP_SLOTS; s++)
        if (f->car[s].on && f->car[s].bottom > R.botacc[s]) R.botacc[s] = f->car[s].bottom;
    if (tick % R.stride == 0) {
        static RpFrame g;             /* the bump-stop peaks of the ticks between frames */
        g = *f;
        for (int s = 0; s < RP_SLOTS; s++) {
            RpCar *c = &g.car[s];
            c->bottom = R.botacc[s]; R.botacc[s] = 0.0f;
            if (!c->on) continue;
            R.fbrake += (c->lights & RPL_BRAKE) != 0; R.fbot += c->bottom > 0.0f;
            R.fskid += c->wheel_skid[0] + c->wheel_skid[1] + c->wheel_skid[2] + c->wheel_skid[3] > 0.0f;
            R.fslip += c->wheel_slipv[0][0] != 0.0f || c->wheel_slipv[1][0] != 0.0f || c->wheel_slipv[2][0] != 0.0f || c->wheel_slipv[3][0] != 0.0f;
        }
        if (!store(tick, &g)) {       /* AddData full: MarkTheEndOfTime */
            R.present = tick + 1;
            R.ended = 1;
            printf("replay: store full -- end of time\n");
            end_log();
            return;
        }
        R.last_stored = 1;
    }
    if (rp_logging() && tick % 300 == 0) log_cars("rec", tick, f);
    R.present = tick + 1;            /* EndFrame: +0x7c = tick + 1 */
}

void rp_contact(const RpContact *c)
{
    if (!R.on || R.ended || !R.ev) return;
    if (R.nev >= RP_EV_MAX) {
        if (!R.evfull) printf("replay: %d contact effects kept, the later ones not\n", RP_EV_MAX);
        R.evfull = 1;
        return;
    }
    R.evtick[R.nev] = R.present;      /* the tick being recorded */
    R.ev[R.nev++] = *c;
}
void rp_sound(const void *ev, int len)
{
    if (!R.on || R.ended || !R.sb || len <= 0 || len > RP_SND_LEN) return;
    size_t need = 8 + (((size_t)len + 3) & ~(size_t)3);
    if (R.sbn + need > RP_SB_CAP) {
        if (!R.sbfull) printf("replay: %d sound calls kept, the later ones not\n", R.nsnd);
        R.sbfull = 1;
        return;
    }
    unsigned char *p = R.sb + R.sbn;
    uint32_t t32 = (uint32_t)R.present; uint16_t l16 = (uint16_t)len;
    memcpy(p, &t32, 4); memcpy(p + 4, &l16, 2); memset(p + 6, 0, 2);
    memcpy(p + 8, ev, (size_t)len);
    R.sbn += need; R.nsnd++;
}
/* the sound calls of one playback tick: a cursor that follows the ticks,
   sought again after a jump */
static size_t g_scur; static int g_stick = -2, g_snd_played;
static float g_bot[RP_SLOTS];        /* playback: bump-stop peaks since the last rp_sample */
static void sounds_at(int tick)
{
    if (!R.sb || !R.sbn) return;
    if (tick != g_stick + 1) {
        g_scur = 0;
        while (g_scur < R.sbn) {
            uint32_t t; uint16_t l; memcpy(&t, R.sb + g_scur, 4); memcpy(&l, R.sb + g_scur + 4, 2);
            if ((int)t >= tick) break;
            g_scur += 8 + (((size_t)l + 3) & ~(size_t)3);
        }
    }
    g_stick = tick;
    while (g_scur < R.sbn) {
        uint32_t t; uint16_t l; memcpy(&t, R.sb + g_scur, 4); memcpy(&l, R.sb + g_scur + 4, 2);
        if ((int)t > tick) break;
        if ((int)t == tick && rp_sound_fx) { rp_sound_fx(R.sb + g_scur + 8, l); g_snd_played++; }
        g_scur += 8 + (((size_t)l + 3) & ~(size_t)3);
    }
}
/* the contact effects of one playback tick */
static int g_ev_played;              /* the log at the exit */
static void contacts_at(int tick)
{
    if (!rp_contact_fx || R.nev <= 0) return;
    int lo = 0, hi = R.nev;          /* the first at or after tick */
    while (lo < hi) { int m = (lo + hi) / 2; if (R.evtick[m] < tick) lo = m + 1; else hi = m; }
    for (int i = lo; i < R.nev && R.evtick[i] == tick; i++) { rp_contact_fx(&R.ev[i]); g_ev_played++; }
}

/* ---- sampling ---------------------------------------------------------- */
static RpDec g_da, g_db; static int g_dai = -1, g_dbi = -1;

static const RpDec *dec_cached(int fi)
{
    if (fi == g_dai) return &g_da;
    if (fi == g_dbi) return &g_db;
    /* keep the most recent other one */
    if (g_dai < g_dbi || g_dai < 0) { decode(fi, &g_da); g_dai = fi; return &g_da; }
    decode(fi, &g_db); g_dbi = fi; return &g_db;
}

static void lerp_car(RpCar *o, const RpCar *a, const RpCar *b, float u)
{
    *o = *a;
    if (!b->on) return;
    for (int i = 0; i < 3; i++) { o->pos[i] = a->pos[i] + (b->pos[i] - a->pos[i]) * u; o->vel[i] = a->vel[i] + (b->vel[i] - a->vel[i]) * u; }
    float qa[4], qb[4], q[4];
    m2q(a->rot, qa); m2q(b->rot, qb);
    float d = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3], sg = d < 0 ? -1.0f : 1.0f, l = 0;
    for (int i = 0; i < 4; i++) { q[i] = qa[i] + (sg * qb[i] - qa[i]) * u; l += q[i] * q[i]; }
    l = sqrtf(l);
    if (l > 1e-6f) { for (int i = 0; i < 4; i++) q[i] /= l; q2m(q, o->rot); }
    o->speed = a->speed + (b->speed - a->speed) * u;
    o->rpm = a->rpm + (b->rpm - a->rpm) * u;
    o->rpm_tach = a->rpm_tach + (b->rpm_tach - a->rpm_tach) * u;
    o->throttle = a->throttle + (b->throttle - a->throttle) * u;
    for (int w = 0; w < 2; w++) o->wheel_steer[w] = a->wheel_steer[w] + (b->wheel_steer[w] - a->wheel_steer[w]) * u;
    for (int w = 0; w < 4; w++) {
        o->wheel_w[w] = a->wheel_w[w] + (b->wheel_w[w] - a->wheel_w[w]) * u;
        o->wheel_comp[w] = a->wheel_comp[w] + (b->wheel_comp[w] - a->wheel_comp[w]) * u;
        o->wheel_slip[w] = a->wheel_slip[w] + (b->wheel_slip[w] - a->wheel_slip[w]) * u;
        o->wheel_skid[w] = a->wheel_skid[w] + (b->wheel_skid[w] - a->wheel_skid[w]) * u;
        o->wheel_skid_w[w] = a->wheel_skid[w] > 0.0f ? a->wheel_skid_w[w] : b->wheel_skid_w[w];
        for (int j = 0; j < 2; j++) o->wheel_slipv[w][j] = a->wheel_slipv[w][j] + (b->wheel_slipv[w][j] - a->wheel_slipv[w][j]) * u;
    }
    for (int i = 0; i < 3; i++) o->skid3[i] = a->skid3[i] + (b->skid3[i] - a->skid3[i]) * u;
    o->bottom = 0.0f;                  /* an impulse: on its stored tick only */
}

static int frame_at(int tick)        /* the last stored frame at or before tick */
{
    int lo = 0, hi = R.nf - 1;
    if (R.nf <= 0) return -1;
    if (tick <= R.ftick[0]) return 0;
    while (lo < hi) {
        int m = (lo + hi + 1) / 2;
        if (R.ftick[m] <= tick) lo = m; else hi = m - 1;
    }
    return lo;
}

static int sample_tick(int tick, RpFrame *out)
{
    int i = frame_at(tick);
    if (i < 0) return 0;
    const RpDec *a = dec_cached(i);
    RpDec A = *a;                     /* the cache may move under b */
    if (i + 1 >= R.nf || tick <= A.tick) {
        *out = A.f;
    } else {
        const RpDec *b = dec_cached(i + 1);
        float u = (float)(tick - A.tick) / (float)(b->tick - A.tick);
        out->t = A.f.t + (b->f.t - A.f.t) * u;
        out->lap = A.f.lap;
        out->laptime = A.f.laptime + (b->f.laptime - A.f.laptime) * u;
        if (b->f.lap != A.f.lap) out->laptime = u < 0.5f ? A.f.laptime : b->f.laptime;
        for (int s = 0; s < RP_SLOTS; s++) lerp_car(&out->car[s], &A.f.car[s], &b->f.car[s], u);
        A.f = *out;                    /* the props below lerp too */
        for (int k = 0; k < A.np; k++)
            for (int j = 0; j < b->np; j++) if (b->sp[j] == A.sp[k]) {
                float q[4], d = 0, l = 0;
                for (int c = 0; c < 4; c++) d += A.q[k][c] * b->q[j][c];
                for (int c = 0; c < 4; c++) { q[c] = A.q[k][c] + ((d < 0 ? -b->q[j][c] : b->q[j][c]) - A.q[k][c]) * u; l += q[c] * q[c]; }
                l = sqrtf(l); if (l > 1e-6f) for (int c = 0; c < 4; c++) A.q[k][c] = q[c] / l;
                for (int c = 0; c < 3; c++) A.pp[k][c] += (b->pp[j][c] - A.pp[k][c]) * u;
                break;
            }
    }
    /* the smokeables: the virgin set in force at tick, the live props' poses */
    static float M[RP_PROPS][16];
    for (int k = 0; k < A.np; k++) {
        float R9[9]; q2m(A.q[k], R9);
        float *m = M[k];
        m[0] = R9[0]; m[1] = R9[1]; m[2] = R9[2]; m[3] = 0;
        m[4] = R9[3]; m[5] = R9[4]; m[6] = R9[5]; m[7] = 0;
        m[8] = R9[6]; m[9] = R9[7]; m[10] = R9[8]; m[11] = 0;
        m[12] = A.pp[k][0]; m[13] = A.pp[k][1]; m[14] = A.pp[k][2]; m[15] = 1;
    }
    const unsigned char *bits = NULL;
    for (int v = 0; v < R.nvb; v++) { if (R.vb_tick[v] <= tick || !bits) bits = R.vb[v]; if (R.vb_tick[v] > tick) break; }
    if (bits) smk_rp_set(bits, A.np, A.sp, (const float (*)[16])M);
    else smk_rp_off();               /* no props recorded: the live ones */
    return 1;
}

/* ---- the camera (Player +0x210..+0x224) ------------------------------- */
static const int POV_OF_TYPE[6] = { 0, 2, 4, 1, 0, 5 };          /* nReplayCameraTypes 0x802130 */
static int set_of_pov(int pov) { switch (pov) { case 2: return 0; case 4: return 1; case 1: return 2; case 5: return 4; default: return 3; } }
static uint32_t g_seed = 0x12345678u;                              /* 0x7ea624 */
static float b_random(float range)                                  /* bRandom 0x43c1f0 */
{
    uint32_t r = g_seed % 0x7fffffffu, a = g_seed ^ 0x1d872b41u, b = a >> 5 ^ a;
    g_seed = b << 27 ^ b ^ a;
    return (float)r * 4.656612873e-10f * range;
}
static struct {
    int type, prev;                  /* +0x21c / +0x218 */
    float cut; int cut_ok;           /* +0x214 (real clock) */
    int want, want_set, want_snap;
} C;

static void cam_reset(void) { memset(&C, 0, sizeof C); }

/* ---- the screen (ReplayMenu) ------------------------------------------- */
static struct {
    Hud *h; int from, drag_drift, laps;
    int hidden, hide_saved;          /* +0x74 / +0x75 */
    int first, exiting, take_present;/* +0xd3 / +0xd2 / +0xd0 */
    int target, switched;            /* +0xcc (marker index, RP_PRESENT) / +0xd1 */
    float load_t0;                   /* +0xd4 */
    RpMark mk[RP_MARK_MAX]; int nm;  /* +0x78.. */
    float start, dur, bw, bleft, by; /* +0xd8 / +0xdc / +0xe0 / +0xe4 / +0xe8 */
    int paused, slow, frames;        /* manager +0x14 / +0x18, the frame counter */
    int tick, closed, shown;
    float real;
    RpFrame cur; int have_cur;
} U;
#define RP_NONE    (-1)
#define RP_PRESENT (-2)

int rp_up(void) { return U.h != NULL; }
int rp_playing(void) { return U.h && U.shown; }
float rp_rate(void) { return !U.h ? 1.0f : U.paused || U.target != RP_NONE ? 0.0f : U.slow ? 0.25f : 1.0f; }

static void scr(const char *obj, const char *script) { if (U.h) hud_screen_script(U.h, obj, script); }
static void scr_h(uint32_t obj, const char *script) { if (U.h) hud_screen_script_h(U.h, obj, script); }
#define EVENT_HANDLER 0x47ff4e7cu
#define LOADING_GROUP 0xa9723345u

static void reset_buttons(void)      /* FEngSendMessageToPackage RESET_BUTTONS */
{
    for (int o = 0; o < FENG_MAX_OBJ; o++) hud_screen_obj_msg(U.h, o, "RESET_BUTTONS");
}

static float marker_t(int m) { return m == RP_PRESENT ? R.tnow : U.mk[m].t; }
static float now_t(void) { return U.target != RP_NONE ? marker_t(U.target) : U.have_cur ? U.cur.t : R.t0; }

static void start_loading(int m)     /* 0x4a4bf0 */
{
    scr("REPLAY_PLAY", "HIDE");
    U.load_t0 = U.real;
    if (U.target == RP_NONE) {
        if (!U.first && !U.exiting) scr_h(LOADING_GROUP, "Show");
        /* RaceCoordinator::BeginLoading(3) */
    }
    U.target = m; U.switched = 0;
    U.paused = 1;
}

static void hide_toggle(void)        /* Hide 0x4a5200 */
{
    if (!U.hidden) { scr_h(EVENT_HANDLER, "HIDE"); U.hidden = 1; }
    else { scr_h(EVENT_HANDLER, "SHOW"); U.hidden = 0; }
}

static void restore_camera_reset(void)   /* RestoreSnapshot -> 0x5faf80 per player */
{
    C.cut = U.real; C.cut_ok = 1;    /* +0x214 = now, +0x224 = 1 */
    C.want_snap = 1;                 /* DefaultDriveCamera(1): the camera snaps */
    /* GUESS: ChangeCameraMover keeps the cubic mover it already has, so the
       replay camera type's POV stays */
}

static void do_exit(void)            /* Exit 0x4a50e0 */
{
    scr_h(LOADING_GROUP, "Hide");    /* EndLoading(6) */
    U.paused = 0; U.slow = 0;
    C.want = 1; C.want_set = -1;     /* RestoreCameraMover: the player's own view */
    smk_rp_off();
    U.closed = U.from;
    hud_free(&U.h);
    printf("replay: exit (%d contact effects, %d sound calls played)\n", g_ev_played, g_snd_played);
    g_ev_played = g_snd_played = 0; g_stick = -2;
}

static void start_exiting(void)      /* 0x4c1220 */
{
    if (U.exiting) return;
    U.exiting = 1;
    start_loading(RP_PRESENT);       /* THE_PRESENT (the caller's saved world) */
    if (!U.hidden) hide_toggle();
}

static void toggle_pause(void)       /* 0x4a4e80 */
{
    if (R.present - 1 - U.tick <= 0) return;
    if (!U.paused) {
        U.paused = 1;
        if (!U.hidden) { scr("REPLAY_PLAY", "HIGHLIGHT"); scr("REPLAY_SLOW", "SHOW"); scr("REPLAY_PAUSE", "HIDE"); }
        else scr("REPLAY_PAUSE", "BLINK_TO_HIDE");
    } else {
        U.paused = 0;
        if (!U.hidden) { scr("REPLAY_PAUSE", "HIGHLIGHT"); scr("REPLAY_PLAY", "HIDE"); }
        else scr("REPLAY_PLAY", "BLINK_TO_HIDE");
    }
}

static void slow_motion(void)        /* 0x4a4fb0 */
{
    if (U.paused) return;
    if (!U.slow) {
        U.slow = 1;
        if (!U.hidden) { scr("REPLAY_PAUSE", "SHOW"); scr("REPLAY_SLOW", "HIGHLIGHT"); return; }
        scr("REPLAY_SLOW", "BLINK_TO_HIDE");
    } else {
        U.slow = 0;
        if (!U.hidden) { reset_buttons(); scr("REPLAY_SLOW", "SHOW"); return; }
        scr("REPLAY_SLOW", "BLINK_TO_HIDE");
    }
}

static void jump_back(void)          /* 0x4a4ca0 */
{
    U.slow = 0;
    reset_buttons();
    if (!U.hidden) { scr("SKIP_BACK", "BLINK_TO_SHOW"); scr("REPLAY_PLAY", "HIDE"); scr("REPLAY_PAUSE", "SHOW"); }
    else scr("SKIP_BACK", "BLINK_TO_HIDE");
    int m = U.nm - 1;
    for (; m >= 0; m--) if (U.mk[m].t < now_t() - 1.0f) break;
    if (m < 0) m = 0;
    if (!U.exiting) start_loading(m);
}

static void jump_forward(void)       /* 0x4a4dd0 */
{
    U.slow = 0;
    scr("SKIP_FORWARD", U.hidden ? "BLINK_TO_HIDE" : "BLINK_TO_SHOW");
    for (int m = 0; m < U.nm; m++)
        if (now_t() < U.mk[m].t) { if (!U.exiting) start_loading(m); return; }
}

static void cycle_camera(int cur_setting)   /* Player::CycleReplayCameraType 0x5fafd0 */
{
    int was = C.type;
    C.type = (C.type + 1) % 6;
    if (C.type == 0) return;
    /* from type 0, a new type showing the POV the cubic mover already has is
       passed over */
    if (was == 0 && set_of_pov(POV_OF_TYPE[C.type]) == cur_setting)
        C.type = (C.type + 1) % 6;
    C.want = 1; C.want_set = set_of_pov(POV_OF_TYPE[C.type]); C.want_snap = 1;   /* DefaultDriveCamera(1) + POV */
}
static int g_cur_setting = 3;

static void choose_camera(void)      /* 0x4a5090 */
{
    scr("REPLAY_MOVE_CAMERA", U.hidden ? "BLINK_TO_HIDE" : "BLINK_TO_SHOW");
    cycle_camera(g_cur_setting);
    printf("replay: camera type %d\n", C.type);
}

int rp_open(const char *dataroot, int from, int drag_or_drift, int laps)
{
    if (U.h) return 0;
    if (!R.on || R.nf <= 0) return 0;
    if (!R.ended) {                   /* the pause menu: the recording so far */
        if (R.last_tick >= 0 && !R.last_stored) { store(R.last_tick, &R.last); R.last_stored = 1; }
    }
    memset(&U, 0, sizeof U);
    U.h = hud_init_screen(dataroot, "GLOBAL/InGameCommon.bun", "UI_ReplayControl.fng");
    if (!U.h) { printf("replay: UI_ReplayControl.fng not loaded\n"); return 0; }
    U.from = from; U.drag_drift = drag_or_drift; U.laps = laps;
    U.target = RP_NONE;
    U.take_present = 1; U.first = 1;
    hud_screen_label_h(U.h, 0x788c05e8u, 0x90038d25u);   /* the lap label */
    hud_screen_label_h(U.h, 0x8a0ab225u, 0x90732102u);   /* REPLAY_HIDE */
    hud_screen_hide_h(U.h, 0x7139516du, 1);
    hud_screen_hide_h(U.h, 0x5497b9bfu, 1);
    if (drag_or_drift) { scr_h(0x788c05e8u, "HIDE"); scr_h(0x57f0e3a5u, "HIDE"); scr_h(0xf2685238u, "HIDE"); }
    /* the markers: the lap snapshots (not one within 20 s of the present
       unless it is the first marker) and the first instant snapshot, in
       time order */
    RpMark all[RP_INST_MAX + RP_LAP_MAX]; int kind[RP_INST_MAX + RP_LAP_MAX], na = 0;
    for (int i = 0; i < R.nlap; i++) { all[na] = R.lap[i]; kind[na++] = 1; }
    for (int i = 0; i < R.ninst; i++) { all[na] = R.inst[i]; kind[na++] = 0; }
    for (int i = 1; i < na; i++)      /* the snapshot list's order: when taken */
        for (int j = i; j > 0 && all[j].tick < all[j - 1].tick; j--) {
            RpMark t = all[j]; all[j] = all[j - 1]; all[j - 1] = t;
            int k = kind[j]; kind[j] = kind[j - 1]; kind[j - 1] = k;
        }
    int ninst = 0;
    for (int i = 0; i < na && U.nm < RP_MARK_MAX; i++) {
        int skip = 0;
        if (kind[i] == 1 && U.nm > 0 && R.tnow - all[i].t < 20.0f) skip = 1;
        if (kind[i] == 0) { if (ninst > 0) skip = 1; ninst++; }
        if (!skip) U.mk[U.nm++] = all[i];
    }
    if (U.nm == 0) { hud_free(&U.h); return 0; }
    U.dur = R.tnow - R.t0; U.start = R.t0;
    if (U.dur < 1e-3f) U.dur = 1e-3f;
    float bx, by, bw, bh;
    if (!hud_screen_box(U.h, "ScrollBar_Backing", &bx, &by, &bw, &bh)) { bx = -88; by = 122; bw = 396; bh = 9; }
    U.bw = bw; U.bleft = bx - bw * 0.5f; U.by = by;
    hud_screen_hide(U.h, "ScrollBar_fill", 0);
    hud_screen_hide(U.h, "REPLAY_CURRENT", 0);
    for (int i = 0; i < RP_MARK_MAX; i++) {
        char nm[24]; snprintf(nm, sizeof nm, "REPLAY_TIME_%d", i + 1);
        if (i >= U.nm) { hud_screen_hide(U.h, nm, 1); continue; }
        float frac = (U.mk[i].t - U.start) / U.dur, mx, my, mw, mh;
        hud_screen_hide(U.h, nm, 0);
        if (!hud_screen_box(U.h, nm, &mx, &my, &mw, &mh)) continue;
        /* FEngSetTopLeft(frac * w + left - w/2, the bar's y) */
        hud_screen_pos(U.h, nm, frac * U.bw + U.bleft, U.by + mh * 0.5f);
    }
    U.paused = 0;                     /* RequestUnPause(5) */
    start_loading(U.nm - 1);
    scr_h(EVENT_HANDLER, "HIDE");
    C.want_snap = 1;                  /* ChangeCameraMover(0, 1) */
    printf("replay: open (from %s), %d markers:", from == RP_FROM_POSTRACE ? "post race" : "pause",
           U.nm);
    for (int i = 0; i < U.nm; i++) printf(" %.1f", U.mk[i].t);
    printf(" s; %d ticks, %.1f s\n", R.present, U.dur);
    return 1;
}

static void service_loading(void)    /* 0x4c10b0 */
{
    if (U.take_present) U.take_present = 0;   /* THE_PRESENT: the caller kept it */
    if (U.target == RP_NONE) return;
    if (!U.switched) { U.switched = 1; return; }   /* TrackStreamer::SwitchZones */
    if (U.real - U.load_t0 < 0.5f) return;         /* 0x78e000 */
    /* (the streamer: the port's world streams round the camera by itself) */
    U.first = 0;
    int m = U.target;
    U.paused = 0;
    U.target = RP_NONE; U.switched = 0;
    if (rp_restore_fx) rp_restore_fx();          /* RestoreSnapshot: the transient world goes */
    memset(g_bot, 0, sizeof g_bot);
    if (m == RP_PRESENT) { do_exit(); return; }
    U.tick = U.mk[m].tick;
    U.have_cur = sample_tick(U.tick, &U.cur);
    U.shown = 1;
    restore_camera_reset();
    scr_h(LOADING_GROUP, "Hide");   /* EndLoading(5) */
    if (!U.hidden) { scr_h(EVENT_HANDLER, "SHOW"); scr("REPLAY_PLAY", "HIDE"); }
    printf("replay: at marker %d (tick %d, clock %.2f s)\n", m, U.tick, U.mk[m].t);
}

void rp_key(int k)
{
    if (!U.h) return;
    /* the console's map (GC NotificationMessage__10ReplayMenu 800f8c60), the
       one UI_ReplayControl's prompts show: START (0xb5af2461) StartExiting,
       PAD_BACK (0x911ab364) ChooseCamera, PAD_BUTTON1 (0xc519bfc0)
       SlowMotion. The PC's 0x4d7bd0 rotates those three (Back exits, Start
       slow motion, Button1 camera) for its keyboard, prompts hidden. */
    switch (k) {
    case RP_K_START:   start_exiting(); break;
    case RP_K_BACK:    choose_camera(); break;
    case RP_K_BUTTON1: slow_motion(); break;
    case RP_K_ACCEPT:  toggle_pause(); break;
    case RP_K_LTRIG:   jump_back(); break;
    case RP_K_RTRIG:   jump_forward(); break;
    case RP_K_BUTTON0: hide_toggle(); break;
    }
}

int rp_tick(void)
{
    if (!U.h || !U.shown) return 0;
    if (U.target != RP_NONE) return 0;
    int skip = U.paused ? 1 : U.slow ? (U.frames % 4 != 0) : 0;   /* BeginFrame */
    U.frames++;
    if (skip) return 0;
    int adv = U.tick < R.present - 1;
    if (adv) { U.tick++; contacts_at(U.tick); sounds_at(U.tick); }
    U.have_cur = sample_tick(U.tick, &U.cur);
    if (adv && U.have_cur)           /* the bump-stop peaks of every tick played, to the next sample */
        for (int s = 0; s < RP_SLOTS; s++) if (U.cur.car[s].bottom > g_bot[s]) g_bot[s] = U.cur.car[s].bottom;
    if (rp_logging() && U.tick % 300 == 0 && U.have_cur) log_cars("play", U.tick, &U.cur);
    if (U.tick >= R.present - 1 && !U.paused) {   /* EndFrame: the end */
        U.paused = 1;
        scr_h(EVENT_HANDLER, "SHOW");             /* 0x7c8d5450 */
        if (U.hidden && !U.hide_saved) hide_toggle();
        U.hide_saved = 0;
        printf("replay: the end (tick %d)\n", U.tick);
    }
    return adv;
}

int rp_sample(RpFrame *out)
{
    if (!U.h || !U.shown || !U.have_cur) return 0;
    *out = U.cur;
    /* a frame that played several ticks (the Vita's 2-3 a frame) still has
       the stored ticks' bump-stop impulses; a held frame (paused) has none */
    for (int s = 0; s < RP_SLOTS; s++) { out->car[s].bottom = g_bot[s]; g_bot[s] = 0.0f; }
    return 1;
}

static void ice_cameras(float speed)  /* UpdateIceCameras 0x5fb050, a replay, no ICE camera */
{
    if (U.paused) return;
    float tsc = C.cut_ok ? U.real - C.cut : 0.0f;
    if (1.5f < tsc && tsc < 4.0f) { C.prev = C.type; return; }
    if (C.type != 0) { C.prev = C.type; return; }
    /* DragCameraManager::GetDragCamera: the route's ICE cameras (not ported)
       -> none, the cubic mover stays */
    int pick = 0, exclude = 0;
    if (C.prev != 0) { pick = 1; exclude = 1; }
    else if (fabsf(speed) >= 10.0f && tsc > 4.0f) {   /* GUESS: 0x5fb180's mover check true */
        if (b_random(1.0f) < 0.3f) pick = 1;
    }
    if (pick) {
        float r = b_random(5.0f), acc = 0.0f;           /* weights 0x7a5534.. all 1 */
        int pov = 0;
        for (int i = 1; i <= 5; i++) {
            acc += 1.0f;
            pov = POV_OF_TYPE[i];
            if ((!exclude || set_of_pov(pov) != g_cur_setting) && r <= acc) break;
        }
        C.want = 1; C.want_set = set_of_pov(pov);
        C.cut = U.real; C.cut_ok = 1;
        printf("replay: camera cut (view %d)\n", pov);
    }
    C.prev = C.type;
}

void rp_frame(float real_dt, int cur_setting, float speed)
{
    if (!U.h) return;
    U.real += real_dt;
    g_cur_setting = cur_setting;
    service_loading();
    if (!U.h) return;
    ice_cameras(speed);
    /* the tick message 0xc98356ba */
    if (!U.hidden && U.paused && U.target == RP_NONE) {
        hud_screen_script_once(U.h, "REPLAY_PAUSE", "HIDE");
        hud_screen_script_once(U.h, "REPLAY_PLAY", "HIGHLIGHT");
    }
    float frac = (now_t() - U.start) / U.dur;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    float x, y, w, h;
    int o = hud_screen_find(U.h, "REPLAY_CURRENT");
    if (o >= 0 && hud_screen_obj_box(U.h, o, &x, &y, &w, &h)) hud_screen_obj_pos(U.h, o, frac * U.bw + U.bleft, y);
    o = hud_screen_find(U.h, "ScrollBar_fill");
    if (o >= 0 && hud_screen_obj_box(U.h, o, &x, &y, &w, &h)) {
        hud_screen_obj_size(U.h, o, frac * U.bw, h);
        hud_screen_obj_pos(U.h, o, U.bleft + frac * U.bw * 0.5f, y);
    }
    int lap = (U.have_cur ? U.cur.lap : 0) + 1;
    if (lap > U.laps) lap = U.laps;
    char b[32];
    snprintf(b, sizeof b, "/%d", U.laps); hud_screen_text_h(U.h, 0xf2685238u, b);
    hud_screen_hide_h(U.h, 0x57f0e3a5u, 0);
    snprintf(b, sizeof b, "%d", lap); hud_screen_text_h(U.h, 0x57f0e3a5u, b);
    hud_time_fmt(b, (int)((U.have_cur ? U.cur.laptime : 0.0f) * 4000.0f), 4);
    hud_screen_text_h(U.h, 0x30d9802eu, b);
}

int rp_camera(int *setting)
{
    int r = (C.want ? 1 : 0) | (C.want_snap ? 2 : 0);
    *setting = C.want_set;
    C.want = 0; C.want_snap = 0;
    return r;
}

int rp_closed(void)
{
    int f = U.closed;
    U.closed = 0;
    return f;
}

void rp_draw(const RProg *rp, GpuMesh *quad, int width, int height, int dt_ms)
{
    if (U.h) hud_screen_draw(U.h, rp, quad, width, height, dt_ms);
}
