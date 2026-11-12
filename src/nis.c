/* nis.c -- the pre-race scene (NIS); see nis.h. */
#include "nis.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

unsigned nis_hash(const char *s)                                        /* 0x43db50 */
{
    unsigned h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}

/* 0x43e6d0: the 16-bit angle of (x, y) (as the rest of the port does it) */
static unsigned short ang16(float x, float y)
{
    return (unsigned short)(int)floorf(atan2f(y, x) * (32768.0f / 3.14159265f) + 0.5f);
}
static float u16rad(unsigned a) { return (float)(a & 0xffff) * (6.28318531f / 65536.0f); }

/* a top-level chunk of a bundle, read on its own */
static unsigned char *read_top_chunk(const char *path, unsigned id, unsigned *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char h[8];
    long at = 0;
    unsigned char *out = NULL;
    while (fseek(f, at, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        unsigned cid = rd32(h), sz = rd32(h + 4);
        if (cid == id) {
            out = (unsigned char *)malloc(sz ? sz : 1);
            if (out && fread(out, 1, sz, f) != sz) { free(out); out = NULL; }
            if (out) *len = sz;
            break;
        }
        at += 8 + (long)sz;
    }
    fclose(f);
    return out;
}
static unsigned char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 ? (unsigned char *)malloc(n) : NULL;
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = n;
    return b;
}

/* ---- tables --------------------------------------------------------------- */
static struct {
    int ok;
    unsigned char *common; unsigned common_len;     /* InGameCommon 0x80037050 */
    const unsigned char *sel; unsigned sel_len;      /* its 0x37070 (ptr 0x827ad4) */
    unsigned char *cams; unsigned cams_len;          /* InGameRace 0x8003b201 */
} T;

int nis_init(const char *dataroot)
{
    if (T.ok) return 1;
    char p[1024];
    snprintf(p, sizeof p, "%s/GLOBAL/InGameCommon.bun", dataroot);
    T.common = read_top_chunk(p, 0x80037050u, &T.common_len);
    snprintf(p, sizeof p, "%s/GLOBAL/InGameRace.bun", dataroot);
    T.cams = read_top_chunk(p, 0x8003b201u, &T.cams_len);
    if (!T.common || !T.cams) {
        printf("nis: scene tables %s, scene cameras %s\n", T.common ? "ok" : "MISSING", T.cams ? "ok" : "MISSING");
        free(T.common); free(T.cams); memset(&T, 0, sizeof T);
        return 0;
    }
    for (unsigned q = 0; q + 8 <= T.common_len;) {
        unsigned cid = rd32(T.common + q), sz = rd32(T.common + q + 4);
        if (cid == 0x37070u && q + 8 + sz <= T.common_len) { T.sel = T.common + q + 8; T.sel_len = sz; }
        q += 8 + sz;
    }
    if (!T.sel || T.sel_len < 0x91c) {
        printf("nis: no scene selection table\n");
        free(T.common); free(T.cams); memset(&T, 0, sizeof T);
        return 0;
    }
    T.ok = 1;
    printf("nis: %u race intro scenes, %u forward / %u reverse event rows\n",
           rd32(T.sel), rd32(T.sel + 0x14), rd32(T.sel + 0x18));
    return 1;
}

/* 0x430070: the per-event allow mask (12-byte rows {u16 event, u16, u32
   mask of entries 0..31, u16 mask of entries 32..}); no row: allowed */
static int event_allows(unsigned idx, int event, int fwd)
{
    unsigned n = rd32(T.sel + (fwd ? 0x14 : 0x18));
    const unsigned char *r = T.sel + (fwd ? 0x37c : 0x91c);
    for (unsigned k = 0; k < n; k++, r += 12) {
        if ((r + 12) > T.sel + T.sel_len) break;
        if ((int)rd16(r) != (event & 0xffff)) continue;
        if (idx > 31) return (rd16(r + 8) >> ((idx - 32) & 31) & 1) != 0;
        return (rd32(r + 4) >> idx & 1) != 0;
    }
    return 1;
}
static int entry_ok(const NisPickCtx *c, const unsigned char *e)       /* 0x42fe00 */
{
    unsigned flags = e[5];
    if (!(flags & 0x40)) return 0;
    if ((c->hmask & flags) != c->hmask) return 0;
    if (c->racers < e[6] || c->racers > e[7]) return 0;
    unsigned h = rd32(e);
    /* 0x42fcf0: the track type, or IntroNis13 v0 on type 2 for a type-3
       track with under 3 racers, or a type-2 entry for a type-1 track with
       two players */
    int tt = (signed char)e[8];
    if (c->track_type != tt) {
        int ok = 0;
        if (c->track_type == 3 && tt == 2 && c->racers < 3 && h == nis_hash("IntroNis13") && e[4] == 0) ok = 1;
        if (c->track_type == 1 && tt == 2 && c->two_player) ok = 1;
        if (!ok) return 0;
    }
    /* 0x42fd60: IntroNis15 with the player at start 2 needs hydraulics > 1 */
    if (h == nis_hash("IntroNis15") && c->player_start == 2 && !(c->hydraulics > 1)) return 0;
    if (c->option_0d && h != nis_hash("IntroNis08")) return h == nis_hash("IntroNis12");
    return 1;
}
static int two_player_ok(const unsigned char *e)                        /* 0x42fda0 */
{
    unsigned h = rd32(e);
    if (h == nis_hash("IntroNis12") && e[4] == 2) return 1;
    if (h == nis_hash("IntroNis13") && (e[4] == 0 || e[4] == 2)) return 1;
    return 0;
}
int nis_pick(const NisPickCtx *c, AiRng *rng, unsigned *hash, int *variant)
{
    *hash = 0; *variant = 0;
    if (!T.ok) return 0;
    if (c->event == 0x1262) return 0;   /* the dyno: DynoEnter, not a race intro */
    if (!c->crowd) return 0;            /* 0x5bf6d0: g_CrowdEnable off, no intro */
    unsigned n = rd32(T.sel), cand[64];
    int nc = 0, fwd = c->dir == 0;      /* 0x437160: dir 0 -> 1 (== 1: forward table) */
    for (unsigned i = 0; i < n && i < 64; i++) {
        const unsigned char *e = T.sel + 0x1c + i * 12;
        if (!event_allows(i, c->event, fwd) || !entry_ok(c, e)) continue;
        if (c->two_player && !two_player_ok(e)) continue;
        cand[nc++] = i;
    }
    if (!nc) return 0;
    const unsigned char *e = T.sel + 0x1c + cand[airng_int(rng, nc)] * 12;   /* 0x43c1c0 */
    *hash = rd32(e); *variant = e[4];
    return 1;
}

/* ---- EAGL animation banks ------------------------------------------------- */
typedef struct { unsigned char *data; unsigned len; unsigned bank; } Bank;

/* An EAGL ELF object of a bundle: its .data (pointers in it are .data
   offsets, every relocation being internal) and the first symbol whose name
   starts with `prefix` (__AnimationBank:::bank, __Skeleton:::BipNN) */
static int elf_take(const unsigned char *E, unsigned n, const char *prefix,
                    unsigned char **data, unsigned *len, unsigned *sym, char *symname, int symcap)
{
    if (n < 0x34 || memcmp(E, "\x7f" "ELF", 4)) return 0;
    unsigned shoff = rd32(E + 0x20), shent = rd16(E + 0x2e), shnum = rd16(E + 0x30), shstr = rd16(E + 0x32);
    if (shoff + shnum * shent > n || shstr >= shnum) return 0;
    const unsigned char *ss = E + shoff + shstr * shent;
    unsigned stroff = rd32(ss + 0x10);
    int dsec = -1, symtab = -1;
    for (unsigned i = 0; i < shnum; i++) {
        const unsigned char *s = E + shoff + i * shent;
        const char *nm = (const char *)E + stroff + rd32(s);
        if (!strcmp(nm, ".data")) dsec = (int)i;
        if (rd32(s + 4) == 2) symtab = (int)i;
    }
    if (dsec < 0 || symtab < 0) return 0;
    const unsigned char *ds = E + shoff + dsec * shent, *sy = E + shoff + symtab * shent;
    const unsigned char *st = E + shoff + rd32(sy + 0x18) * shent;
    unsigned dof = rd32(ds + 0x10), dlen = rd32(ds + 0x14);
    if (dof + dlen > n) return 0;
    int found = 0;
    size_t pl = strlen(prefix);
    for (unsigned k = 0; k < rd32(sy + 0x14) / 16; k++) {
        const unsigned char *y = E + rd32(sy + 0x10) + k * 16;
        const char *nm = (const char *)E + rd32(st + 0x10) + rd32(y);
        if (!strncmp(nm, prefix, pl)) {
            *sym = rd32(y + 4); found = 1;
            if (symname) snprintf(symname, (size_t)symcap, "%s", nm);
            break;
        }
    }
    if (!found) return 0;
    *data = (unsigned char *)malloc(dlen ? dlen : 1);
    if (!*data) return 0;
    memcpy(*data, E + dof, dlen);
    *len = dlen;
    return 1;
}
static int bank_from_elf(const unsigned char *E, unsigned n, Bank *b)
{
    if (!elf_take(E, n, "__AnimationBank", &b->data, &b->len, &b->bank, NULL, 0)) return 0;
    if (b->bank + 0x18 <= b->len) return 1;
    free(b->data); b->data = NULL;
    return 0;
}
/* 0x6e2c10: an animation of the bank by name (bank +4 count, +0x10 the
   animations, +0x14 their names) */
static const unsigned char *bank_find(const Bank *b, const char *name)
{
    const unsigned char *D = b->data, *bk = D + b->bank;
    unsigned cnt = rd32(bk + 4), pa = rd32(bk + 0x10), pn = rd32(bk + 0x14);
    for (unsigned i = 0; i < cnt; i++) {
        if (pn + 4 * i + 4 > b->len || pa + 4 * i + 4 > b->len) break;
        unsigned no = rd32(D + pn + 4 * i), ao = rd32(D + pa + 4 * i);
        if (no < b->len && ao < b->len && !strcmp((const char *)D + no, name)) return D + ao;
    }
    return NULL;
}

/* Codecs 10 and 11 (0x6e4950 / 0x6e4c50): a delta stream {u16 nch, u8 bits
   4/8/16, u8}, nch x {min, scale, init}, then per frame 1.. packed q:
   v[f] = v[f-1] + q * scale + min (0x6efa20); between frames the next
   delta times the fraction (0x6efe40). Codec 10 writes one pose float per
   channel, codec 11 four (index table: one entry per 4 channels). */
typedef struct {
    const unsigned char *a;       /* the sub-animation */
    const unsigned char *s;       /* its stream */
    int type, nch, bits, frames, cur;
    float v[160];                 /* the cache: values at frame `cur` (35 bones x 4) */
} Codec;
static float q_at(const Codec *c, int frame, int ch)    /* the quantum leading into `frame` */
{
    const unsigned char *base = c->s + 4 + 12 * c->nch;
    if (c->bits == 4) {
        const unsigned char *b = base + (size_t)((c->nch + 1) / 2) * (frame - 1);
        return (float)(ch & 1 ? b[ch / 2] >> 4 : b[ch / 2] & 15);
    }
    if (c->bits == 8) return (float)base[(size_t)c->nch * (frame - 1) + ch];
    return (float)rd16(base + 2 * ((size_t)c->nch * (frame - 1) + ch));
}
static void codec_seek(Codec *c, int to)                               /* 0x6efa20 */
{
    if (to == c->cur) return;
    if (to < c->cur || c->cur < 0 || to == 0) {
        for (int i = 0; i < c->nch; i++) c->v[i] = rdf(c->s + 4 + 12 * i + 8);
        c->cur = 0;
    }
    for (int f = c->cur + 1; f <= to; f++)
        for (int i = 0; i < c->nch; i++)
            c->v[i] = q_at(c, f, i) * rdf(c->s + 4 + 12 * i + 4) + c->v[i] + rdf(c->s + 4 + 12 * i);
    c->cur = to;
}
static int codec_init(Codec *c, const unsigned char *a)
{
    memset(c, 0, sizeof *c);
    c->a = a; c->type = rd16(a);
    if (c->type != 10 && c->type != 11) return 0;
    c->s = a + 0;   /* fixed below: the stream pointer is a .data offset */
    return 1;
}
static void codec_eval(Codec *c, float t, float *pose)                 /* 0x6e4950 / 0x6e4c50 */
{
    float r = t < 0 ? t + 0.4999990f : t - 0.4999990f;                /* 0x7c1a3c */
    int i = (int)lrintf(r);
    int k = i < c->frames ? (i < 0 ? 0 : i) : c->frames - 1;
    codec_seek(c, k);
    int per = c->type == 11 ? 4 : 1, groups = c->nch / per;
    const unsigned char *idx = c->a + 10;
    if (t != (float)i && i + 1 < c->frames) {                          /* 0x6efe40 */
        float fr = t - (float)i;
        for (int g = 0; g < groups; g++)
            for (int j = 0; j < per; j++) {
                int ch = g * per + j;
                float d = (q_at(c, i + 1, ch) * rdf(c->s + 4 + 12 * ch + 4) + rdf(c->s + 4 + 12 * ch)) * fr;
                pose[rd16(idx + 2 * g) + j] = c->v[ch] + d;
            }
        return;
    }
    for (int g = 0; g < groups; g++)
        for (int j = 0; j < per; j++) pose[rd16(idx + 2 * g) + j] = c->v[g * per + j];
}

/* ---- the scene ------------------------------------------------------------ */
typedef struct {
    int on;
    Codec sub[2][4]; int nsub[2];   /* ZPM_CarK_t, ZPM_CarK_q containers (type 15) */
    float frame, end;               /* anim object +0x68, +0x64 */
    float pose[24];                 /* two bones x 12 (0x6e3c70 from ROOT) */
    int count;                      /* slot +0xc: places so far */
} Slot;

typedef struct {
    const unsigned char *k;         /* the active key (0x88 B) */
    int idx;
    float p0[3][3], t0[3][3], p1[3][3], t1[3][3];   /* eye, look splines: [chan][axis] */
    float fov[4];                   /* p0 t0 p1 t1 */
    float k0, k1;                   /* the key's param and the next one's (+0x6c / +0x70) */
} IceKey;

/* an EAGL skeleton (bundle 0xe34009, __Skeleton:::BipNN): +8 bone count,
   +0xc the x-scale table (0 in every NIS skeleton), bones from +0x10,
   0x70 each {scale3, parent, quat4, trans3, ?, inverse bind 4x4 at +0x30} */
typedef struct { unsigned char *data; unsigned len, off; unsigned hash; int nb; } Skel;

/* a scene character: the item (0x437be0) and its animation object
   (0x4364e0: +0x34 flags, +0x38..+0x44 range, +0x60 rate, +0x64 length,
   +0x68 frame) */
typedef struct {
    float M[16];                    /* item +0x10: the matrix in scene space */
    unsigned solid;                 /* item +0x50 (the model, 0x61d050) */
    const Skel *sk;
    const Bank *bank;
    const unsigned char *sub[4]; int nsub;   /* the _s container's codecs */
    Codec c11[4];                   /* caches of the codec-11 subs */
    unsigned flags;                 /* +0x34 */
    float rate, r0, r1, len, frame; /* +0x60, +0x40, +0x44, +0x64, +0x68 */
    float gz; int gfound;           /* item +0x20 / +0x19: the smoothed ground */
    float *pose;                    /* nb x 12 (+0x24) */
    float *bw;                      /* nb x 16: bones in the model (0x6e3b10) */
    float *skin;                    /* nb x 16: x inverse bind (+0x2c) */
    float *ib;                      /* nb x 16: the skeleton's inverse binds, read once */
    int skin_ok;                    /* skin is of the current pose (built when asked) */
    float W[16];                    /* the world object's matrix (+0x10) */
    int shadow;                     /* item +0x24: 0x7ea1e0 row, -1 none */
    double gstate[NIS_GROUND_STATE / sizeof(double)];   /* the ground lookup's own state */
} NisChar;

struct Nis {
    char name[32]; unsigned hash; int variant;
    float len, cd_end, cd_start;    /* header +0x28 / +0x2c / +0x30, frames at 30 */
    int type;                       /* header +0x18 */
    Bank bank[8]; int nbank;
    Slot slot[8];
    float time;                     /* +0x2c */
    float t_cd, t_len;              /* +0x30 +0x34 */
    float interval;
    int playing, cars_on, handed;   /* +0x50, feature 1 (cars), hand-off done */
    float R[4], T[3];               /* +0x60 rotation (2x2 of z), +0xa0 translation */
    NisGroundFn ground; void *gu;
    float zref;                     /* ICE +0x1c60: ground at the scene origin */
    const unsigned char *track;     /* the camera track ("Track <variant>") */
    IceKey key;
    Skel skel[8]; int nskel;
    unsigned char *items; int nitems;   /* 0x37040 items, 0x90 bytes each */
    NisChar *ch; int nch;
    char path[1024];
    /* the event track (instance +0x124): +0xc clock, +0x10 next record,
       +0x14 count, records from +0x18 */
    NisEvent *ev; int nev, ev_i, ev_cap;
    float ev_t;
    int fired0, fired1;             /* records fired by the last tick */
    struct { int type; float k; int par; } fx[8][4];   /* 0x827cd0.. per car */
};

/* ---- the event track ------------------------------------------------------ */
/* the SEQ_* templates 0x7ea240..0x7ea4dc: {type, time, car, a, b, f, g} */
static const NisEvent k_seq_nitro[4] = {          /* 0x430dc0: f x intensity */
    { 6, 0, 1, 0, 0, 0.5f, 0.5f }, { 1, 2, 1, 10, 3, 2.0f, 0 },
    { 1, 5, 1, 10, 3, 0, 0 },      { 6, 10, 1, 0, 0, 0, 0 } };
static const NisEvent k_seq_burn[16] = {          /* 0x430ea0: 10 from the start, 6 from the end */
    { 5, 0, 1, 0, 0, 5, 0 },  { 6, 0, 1, 0, 0, 0.1f, -2 }, { 5, 3, 1, 0, 0, 10, 0 },
    { 6, 3, 1, 0, 0, 0, -2 }, { 6, 8, 1, 0, 0, 0, 2 },     { 6, 12, 1, 0, 0, 0, -1 },
    { 6, 17, 1, 0, 0, 0, 1 }, { 6, 22, 1, 0, 0, 0, -0.5f },{ 6, 27, 1, 0, 0, 0, 0.5f },
    { 6, 32, 1, 0, 0, 0, 1 },
    { 5, -20, 1, 0, 0, 10, 0 }, { 5, -15, 1, 0, 0, 8, 0 }, { 5, -10, 1, 0, 0, 6, 0 },
    { 6, -10, 1, 0, 0, 0, 0 },  { 5, -5, 1, 0, 0, 3, 0 },  { 5, 0, 1, 0, 0, 0, 0 } };
static const NisEvent k_seq_tq[4] = {             /* 0x431130: first, the two alternating, last */
    { 6, 0, 1, 0, 0, 0.5f, 0.5f }, { 6, 0, 1, 0, 0, -0.5f, 0.5f },
    { 6, 0, 1, 0, 0, 0.5f, -0.5f }, { 6, 0, 1, 0, 0, 0, 0 } };

static NisEvent *ev_add(Nis *n)
{
    if (n->nev == n->ev_cap) {
        int c = n->ev_cap ? n->ev_cap * 2 : 64;
        NisEvent *e = (NisEvent *)realloc(n->ev, c * sizeof *e);
        if (!e) return NULL;
        n->ev = e; n->ev_cap = c;
    }
    NisEvent *e = &n->ev[n->nev++];
    memset(e, 0, sizeof *e);
    return e;
}
/* a template record at base time t for car c (the expanders copy the
   template, add t to its time and set the car) */
static NisEvent *ev_tmpl(Nis *n, const NisEvent *tp, float t, int car)
{
    NisEvent *e = ev_add(n);
    if (!e) return NULL;
    *e = *tp; e->time = tp->time + t; e->car = car;
    return e;
}
static int ev_car(const char *s)                  /* car numbers 8 and up are -1 */
{
    int v = s ? atoi(s) : 0;
    if (s && s[0] == '0' && s[1] == 'x') v = (int)strtol(s + 2, NULL, 16);   /* 0x43de50 */
    return v < 8 ? v : -1;
}
static int ev_int(const char *s)                  /* 0x4401c0 -> 0x43de50 */
{
    if (!s) return 0;
    if (s[0] == '0' && s[1] == 'x') return (int)strtol(s + 2, NULL, 16);
    return atoi(s);
}
static float ev_flt(const char *s) { return s ? (float)atof(s) : 0.0f; }   /* 0x440250 */
static int ev_type(const char *s)                 /* 0x430b20 */
{
    static const char *const k[] = { "CAR_TRIGGER_EFFECT", "CAR_HYDRAULIC_BOUNCE",
        "CAR_HYDRAULIC_EXTEND", "CAR_BRAKE_LOCK", "CAR_BURNOUT_ADDITIVE", "CAR_ENGINE_TORQUE",
        "SEQ_CAR_NITRO", "SEQ_CAR_BURNOUT", "SEQ_CAR_TORQUE", "SCREEN_FLASH",
        "WORLD_ANIM_EVENT", "WORLD_MESSAGE", "CAR_AUDIO_PATTERN", "CAR_AUDIO_REV" };
    for (int i = 0; s && i < 14; i++) if (!strcmp(s, k[i])) return i + 1;
    return 0;
}
static int ev_fx(const char *s)                   /* 0x430c90 */
{
    static const char *const k[] = { "CARFX_NITRO", "CARFX_EXHAUST_SMOKE",
                                     "CARFX_EXHAUST_BLOWOFF", "CARFX_NOS_BLOWOFF" };
    for (int i = 0; s && i < 4; i++) if (!strcmp(s, k[i])) return 10 + i;
    return -1;
}
static int ev_wheel(const char *s)                /* 0x430d40 */
{
    static const char *const k[] = { "CAR_WHEEL_FL", "CAR_WHEEL_FR", "CAR_WHEEL_RR", "CAR_WHEEL_RL" };
    for (int i = 0; s && i < 4; i++) if (!strcmp(s, k[i])) return i;
    return -1;
}
static int ev_world(const char *s)                /* 0x430d00 */
{
    if (s && !strcmp(s, "DOOR_POSITION")) return 1;
    if (s && !strcmp(s, "ANIM_POSITION")) return 2;
    return -1;
}

/* 0x432d00: one "EVENT:" line; tok[0] is the time */
static void ev_line(Nis *n, char **tok, int nt)
{
#define TK(i) ((i) < nt ? tok[i] : NULL)
    float t = ev_flt(TK(0));
    int ty = ev_type(TK(1));
    NisEvent *e;
    switch (ty) {
    case NIS_EV_EFFECT:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_car(TK(2)); e->a = ev_fx(TK(3));
        e->f = ev_flt(TK(4)); e->b = ev_int(TK(5));
        break;
    case NIS_EV_HYD_BOUNCE: case NIS_EV_HYD_EXTEND:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_car(TK(2)); e->a = ev_wheel(TK(3)); e->f = ev_flt(TK(4));
        break;
    case NIS_EV_BRAKE_LOCK: case NIS_EV_BURNOUT_ADD:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_car(TK(2)); e->f = ev_flt(TK(3));
        break;
    case NIS_EV_TORQUE: case NIS_EV_AUDIO_REV:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_car(TK(2)); e->f = ev_flt(TK(3)); e->g = ev_flt(TK(4));
        break;
    case NIS_EV_SCREEN_FLASH:                     /* the car number unclamped */
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_int(TK(2)); e->f = ev_flt(TK(3));
        break;
    case NIS_EV_WORLD_MSG:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_world(TK(2)); e->f = ev_flt(TK(3)); e->g = ev_flt(TK(4));
        if (nt > 5) e->a = ev_int(TK(5));
        break;
    case NIS_EV_AUDIO_PATTERN:
        if (!(e = ev_add(n))) return;
        e->type = ty; e->time = t; e->car = ev_car(TK(2)); e->a = ev_int(TK(3)); e->f = ev_flt(TK(4));
        break;
    case NIS_EV_SEQ_NITRO: {                      /* 0x430dc0(time, car, intensity) */
        int c = ev_car(TK(2)); float k = ev_flt(TK(3));
        for (int i = 0; i < 4; i++) {
            if (!(e = ev_tmpl(n, &k_seq_nitro[i], t, c))) return;
            e->f *= k;
        }
        break; }
    case NIS_EV_SEQ_BURNOUT: {                    /* 0x430ea0(time, car, length) */
        int c = ev_car(TK(2)); float len = ev_flt(TK(3));
        for (int i = 0; i < 16; i++)
            if (!ev_tmpl(n, &k_seq_burn[i], i < 10 ? t : t + len, c)) return;
        break; }
    case NIS_EV_SEQ_TORQUE: {                     /* 0x431130(time, car, length, size, period) */
        int c = ev_car(TK(2));
        float len = ev_flt(TK(3)), amp = ev_flt(TK(4)), per = ev_flt(TK(5));
        if (!(e = ev_tmpl(n, &k_seq_tq[0], t, c))) return;
        e->f *= amp;
        float u = 0;
        int alt = 1;
        if (0 < len) {
            do {
                u += per;
                if (!(e = ev_tmpl(n, &k_seq_tq[alt ? 1 : 2], t + u, c))) return;
                e->f *= amp;
                alt = !alt;
            } while (u < len);
        }
        if (!(e = ev_tmpl(n, &k_seq_tq[3], t + u + per, c))) return;
        e->f *= amp;
        break; }
    default:                                      /* WORLD_ANIM_EVENT: no scene has one */
        break;
    }
#undef TK
}

/* 0x5d40f0: heapsort (1-based, sift-down) by 0x430db0 (a.time - b.time) */
static void ev_sort(NisEvent *v, int n)
{
    if (n < 2) return;
    NisEvent *ra = v - 1, rra;
    int l = n / 2 + 1, ir = n, i, j;
    for (;;) {
        if (l > 1) rra = ra[--l];
        else {
            rra = ra[ir]; ra[ir] = ra[1];
            if (--ir == 1) { ra[1] = rra; return; }
        }
        i = l; j = l + l;
        while (j <= ir) {
            if (j < ir && ra[j].time - ra[j + 1].time < 0) j++;
            if (rra.time - ra[j].time < 0) { ra[i] = ra[j]; i = j; j <<= 1; }
            else j = ir + 1;
        }
        ra[i] = rra;
    }
}

/* 0x432c00: NIS\Scene_<name>_EvtTrk.txt, one record per "EVENT:" line
   head (0x43e4a0), its arguments the rest of that line */
static void ev_load(Nis *n, const char *dataroot)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/NIS/Scene_%s_EvtTrk.txt", dataroot, n->name);
    long len = 0;
    unsigned char *b = read_file(p, &len);
    n->nev = 0;
    if (!b) return;
    char *s = (char *)realloc(b, len + 1);
    if (!s) { free(b); return; }
    s[len] = 0;
    for (char *line = s; line && *line;) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char *tok[16]; int nt = 0;
        for (char *q = strtok(line, " \t"); q && nt < 16; q = strtok(NULL, " \t")) tok[nt++] = q;
        if (nt > 0 && !strcmp(tok[0], "EVENT:")) ev_line(n, tok + 1, nt - 1);
        line = nl ? nl + 1 : NULL;
    }
    free(s);
    ev_sort(n->ev, n->nev);
}

/* 0x4395f0 */
static void ev_tick(Nis *n, float dt)
{
    const float k = 0.0333333351f;               /* 0x786460 */
    int i0 = n->ev_i;
    n->ev_t += dt;
    if (i0 != n->nev && n->ev[i0].time * k < n->ev_t) {
        do {
            if (n->nev <= n->ev_i) break;
            n->ev_i++;
        } while (n->ev_i < n->nev && n->ev[n->ev_i].time * k < n->ev_t);
    }
    n->fired0 = i0; n->fired1 = n->ev_i;
    /* 0x4392e0 type 1 keeps the per-car effect state */
    for (int i = i0; i < n->ev_i; i++) {
        const NisEvent *e = &n->ev[i];
        if (e->type != NIS_EV_EFFECT || e->car < 1 || e->car > 8 || e->a < 10 || e->a > 13) continue;
        int fx = e->a - 10;
        n->fx[e->car - 1][fx].type = e->a;
        n->fx[e->car - 1][fx].k = e->f;
        n->fx[e->car - 1][fx].par = e->b;
    }
}

int nis_fired(const Nis *n, const NisEvent **out)
{
    *out = n->ev ? n->ev + n->fired0 : NULL;
    return n->fired1 - n->fired0;
}
int nis_nevents(const Nis *n) { return n->nev; }
void nis_car_fx(const Nis *n, int car, int fx, float *intensity, int *param)
{
    *intensity = 0; *param = 0;
    if (car < 1 || car > 8 || fx < 10 || fx > 13) return;
    *intensity = n->fx[car - 1][fx - 10].k;
    *param = n->fx[car - 1][fx - 10].par;
}

/* ---- the scripted engine sound ---------------------------------------------- */
/* SPEED2.EXE 0x7f0070..0x7f095c: the 16 rev patterns, {u16 rpm, u8 throttle, pad} each */
static const unsigned char k_revpat[2284] = {
    0x7c, 0x13, 0x00, 0x00, 0x5e, 0x13, 0x28, 0x00, 0x5d, 0x13, 0x2e, 0x00, 0x5d, 0x13, 0x2e, 0x00,
    0x62, 0x13, 0x32, 0x00, 0x69, 0x13, 0x32, 0x00, 0x71, 0x13, 0x31, 0x00, 0x78, 0x13, 0x32, 0x00,
    0x7f, 0x13, 0x32, 0x00, 0x87, 0x13, 0x32, 0x00, 0x8f, 0x13, 0x32, 0x00, 0x96, 0x13, 0x32, 0x00,
    0x9e, 0x13, 0x32, 0x00, 0x9a, 0x13, 0x26, 0x00, 0x83, 0x13, 0x1c, 0x00, 0x63, 0x13, 0x11, 0x00,
    0x24, 0x13, 0x00, 0x00, 0xe2, 0x12, 0x00, 0x00, 0xa0, 0x12, 0x00, 0x00, 0x5f, 0x12, 0x00, 0x00,
    0x1d, 0x12, 0x00, 0x00, 0xdd, 0x11, 0x00, 0x00, 0x9d, 0x11, 0x00, 0x00, 0x5d, 0x11, 0x00, 0x00,
    0x2e, 0x11, 0x00, 0x00, 0x74, 0x10, 0x00, 0x00, 0xe8, 0x0f, 0x00, 0x00, 0x59, 0x18, 0x3d, 0x00,
    0xaa, 0x1a, 0x64, 0x00, 0x45, 0x1a, 0x25, 0x00, 0x1d, 0x19, 0x00, 0x00, 0x71, 0x18, 0x00, 0x00,
    0x69, 0x17, 0x00, 0x00, 0x3f, 0x16, 0x00, 0x00, 0xee, 0x14, 0x00, 0x00, 0x75, 0x13, 0x00, 0x00,
    0xdc, 0x11, 0x00, 0x00, 0x14, 0x11, 0x00, 0x00, 0x59, 0x10, 0x00, 0x00, 0xb6, 0x18, 0x1e, 0x00,
    0x1c, 0x1e, 0x64, 0x00, 0xb9, 0x1d, 0x25, 0x00, 0x93, 0x1c, 0x00, 0x00, 0x8e, 0x1b, 0x00, 0x00,
    0xdc, 0x19, 0x00, 0x00, 0xcd, 0x17, 0x00, 0x00, 0x5b, 0x15, 0x00, 0x00, 0x92, 0x12, 0x00, 0x00,
    0xd5, 0x0f, 0x3c, 0x00, 0xd8, 0x10, 0x64, 0x00, 0xc8, 0x12, 0x64, 0x00, 0xb7, 0x14, 0x64, 0x00,
    0x67, 0x16, 0x5a, 0x00, 0x9d, 0x15, 0x03, 0x00, 0x74, 0x14, 0x00, 0x00, 0x4d, 0x13, 0x00, 0x00,
    0x2e, 0x12, 0x00, 0x00, 0x1c, 0x11, 0x00, 0x00, 0x07, 0x10, 0x00, 0x00, 0xf8, 0x0e, 0x00, 0x00,
    0xee, 0x0d, 0x00, 0x00, 0xe9, 0x0c, 0x00, 0x00, 0xeb, 0x0b, 0x00, 0x00, 0xf0, 0x0a, 0x00, 0x00,
    0xfb, 0x09, 0x00, 0x00, 0x0c, 0x09, 0x00, 0x00, 0x20, 0x08, 0x00, 0x00, 0x39, 0x07, 0x00, 0x00,
    0x59, 0x06, 0x00, 0x00, 0x69, 0x05, 0x00, 0x00, 0x79, 0x04, 0x00, 0x00, 0xee, 0x03, 0x00, 0x00,
    0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x17, 0x61, 0x00, 0x9c, 0x18, 0x4b, 0x00,
    0x25, 0x19, 0x32, 0x00, 0xbb, 0x19, 0x47, 0x00, 0xd9, 0x1b, 0x64, 0x00, 0xbb, 0x1d, 0x45, 0x00,
    0x13, 0x1d, 0x00, 0x00, 0xf9, 0x1b, 0x00, 0x00, 0xd7, 0x1a, 0x00, 0x00, 0xb4, 0x19, 0x00, 0x00,
    0x58, 0x18, 0x00, 0x00, 0xff, 0x16, 0x10, 0x00, 0x03, 0x17, 0x33, 0x00, 0xfe, 0x17, 0x52, 0x00,
    0x37, 0x1a, 0x59, 0x00, 0xaa, 0x1c, 0x59, 0x00, 0x7d, 0x1e, 0x34, 0x00, 0xc6, 0x1d, 0x00, 0x00,
    0xad, 0x1c, 0x00, 0x00, 0x53, 0x1a, 0x00, 0x00, 0x88, 0x17, 0x00, 0x00, 0xd5, 0x14, 0x00, 0x00,
    0x54, 0x12, 0x00, 0x00, 0x01, 0x11, 0x00, 0x00, 0xec, 0x0f, 0x00, 0x00, 0xca, 0x0e, 0x00, 0x00,
    0x0c, 0x0d, 0x00, 0x00, 0xf7, 0x0b, 0x00, 0x00, 0xb1, 0x0c, 0x53, 0x00, 0x93, 0x0e, 0x64, 0x00,
    0x77, 0x10, 0x64, 0x00, 0x62, 0x12, 0x64, 0x00, 0x59, 0x14, 0x64, 0x00, 0x60, 0x16, 0x64, 0x00,
    0x7b, 0x18, 0x64, 0x00, 0xb8, 0x1a, 0x64, 0x00, 0x1e, 0x1d, 0x64, 0x00, 0xb2, 0x1f, 0x64, 0x00,
    0x79, 0x22, 0x64, 0x00, 0x6a, 0x25, 0x64, 0x00, 0x13, 0x04, 0x1e, 0x00, 0xd1, 0x07, 0x64, 0x00,
    0x47, 0x0e, 0x64, 0x00, 0xe3, 0x12, 0x64, 0x00, 0xa2, 0x12, 0x64, 0x00, 0x3e, 0x11, 0x64, 0x00,
    0x5e, 0x10, 0x64, 0x00, 0x0d, 0x10, 0x64, 0x00, 0x48, 0x10, 0x64, 0x00, 0x18, 0x11, 0x64, 0x00,
    0x81, 0x12, 0x64, 0x00, 0x85, 0x14, 0x64, 0x00, 0xbf, 0x16, 0x64, 0x00, 0xa8, 0x17, 0x28, 0x00,
    0xa7, 0x16, 0x00, 0x00, 0x7f, 0x15, 0x00, 0x00, 0x58, 0x14, 0x00, 0x00, 0x33, 0x13, 0x00, 0x00,
    0x14, 0x12, 0x00, 0x00, 0xfb, 0x10, 0x00, 0x00, 0xe5, 0x0f, 0x00, 0x00, 0xd3, 0x0e, 0x00, 0x00,
    0xc7, 0x0d, 0x00, 0x00, 0xbe, 0x0c, 0x00, 0x00, 0xba, 0x0b, 0x00, 0x00, 0xba, 0x0a, 0x00, 0x00,
    0xbe, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x0e, 0x30, 0x00, 0x7f, 0x0e, 0x30, 0x00,
    0xa2, 0x0e, 0x31, 0x00, 0xc6, 0x0e, 0x31, 0x00, 0xeb, 0x0e, 0x31, 0x00, 0x05, 0x0f, 0x2d, 0x00,
    0xf9, 0x0e, 0x28, 0x00, 0xdd, 0x0e, 0x27, 0x00, 0xc0, 0x0e, 0x27, 0x00, 0x7f, 0x0e, 0x1a, 0x00,
    0xe5, 0x0d, 0x10, 0x00, 0x24, 0x0d, 0x05, 0x00, 0x23, 0x0c, 0x00, 0x00, 0x22, 0x0b, 0x00, 0x00,
    0x28, 0x0a, 0x00, 0x00, 0x35, 0x09, 0x00, 0x00, 0x45, 0x08, 0x00, 0x00, 0xc5, 0x07, 0x1b, 0x00,
    0xe3, 0x07, 0x2a, 0x00, 0x0b, 0x08, 0x2a, 0x00, 0x33, 0x08, 0x2a, 0x00, 0x5c, 0x08, 0x2a, 0x00,
    0x83, 0x08, 0x17, 0x00, 0xa9, 0x08, 0x17, 0x00, 0x42, 0x08, 0x00, 0x00, 0x52, 0x07, 0x00, 0x00,
    0x62, 0x06, 0x00, 0x00, 0x72, 0x05, 0x00, 0x00, 0x82, 0x04, 0x00, 0x00, 0xe9, 0x03, 0x00, 0x00,
    0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x5f, 0x06, 0x53, 0x00,
    0x11, 0x0b, 0x64, 0x00, 0x9a, 0x10, 0x64, 0x00, 0xe0, 0x16, 0x64, 0x00, 0x5a, 0x1a, 0x46, 0x00,
    0xd2, 0x17, 0x00, 0x00, 0x52, 0x17, 0x1e, 0x00, 0x84, 0x1d, 0x64, 0x00, 0x79, 0x24, 0x64, 0x00,
    0xd3, 0x26, 0x64, 0x00, 0xda, 0x26, 0x64, 0x00, 0xec, 0x26, 0x64, 0x00, 0xb2, 0x26, 0x64, 0x00,
    0x84, 0x24, 0x10, 0x00, 0xe6, 0x21, 0x00, 0x00, 0x2b, 0x1f, 0x00, 0x00, 0x63, 0x1c, 0x00, 0x00,
    0x97, 0x19, 0x00, 0x00, 0xc6, 0x16, 0x00, 0x00, 0xf7, 0x13, 0x00, 0x00, 0x2b, 0x11, 0x00, 0x00,
    0x5c, 0x0e, 0x00, 0x00, 0x8f, 0x0b, 0x00, 0x00, 0x49, 0x09, 0x00, 0x00, 0x59, 0x08, 0x00, 0x00,
    0x69, 0x07, 0x00, 0x00, 0x79, 0x06, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x1c, 0x04, 0x0a, 0x00,
    0xda, 0x07, 0x60, 0x00, 0xd5, 0x0c, 0x64, 0x00, 0xa2, 0x12, 0x64, 0x00, 0x1b, 0x19, 0x64, 0x00,
    0x1d, 0x1f, 0x5a, 0x00, 0x93, 0x1d, 0x03, 0x00, 0xd0, 0x1a, 0x00, 0x00, 0x4b, 0x18, 0x0a, 0x00,
    0x78, 0x1d, 0x60, 0x00, 0xc2, 0x23, 0x5a, 0x00, 0x7f, 0x22, 0x03, 0x00, 0xda, 0x1f, 0x00, 0x00,
    0x13, 0x1d, 0x00, 0x00, 0x47, 0x1a, 0x00, 0x00, 0x78, 0x17, 0x00, 0x00, 0xab, 0x14, 0x00, 0x00,
    0xdc, 0x11, 0x00, 0x00, 0x0d, 0x0f, 0x00, 0x00, 0x41, 0x0c, 0x00, 0x00, 0x81, 0x09, 0x00, 0x00,
    0x91, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x04, 0x2f, 0x00, 0xa8, 0x04, 0x36, 0x00,
    0x8a, 0x06, 0x48, 0x00, 0xc0, 0x09, 0x55, 0x00, 0xb2, 0x0d, 0x57, 0x00, 0x60, 0x13, 0x64, 0x00,
    0xea, 0x19, 0x64, 0x00, 0xb7, 0x20, 0x61, 0x00, 0xd2, 0x1f, 0x0c, 0x00, 0x1f, 0x1d, 0x00, 0x00,
    0x55, 0x1a, 0x00, 0x00, 0x86, 0x17, 0x00, 0x00, 0xb8, 0x14, 0x00, 0x00, 0xef, 0x11, 0x00, 0x00,
    0x20, 0x0f, 0x00, 0x00, 0xc8, 0x0c, 0x14, 0x00, 0x07, 0x0b, 0x06, 0x00, 0xfe, 0x08, 0x00, 0x00,
    0x0e, 0x08, 0x00, 0x00, 0x1e, 0x07, 0x00, 0x00, 0xac, 0x09, 0x64, 0x00, 0x9a, 0x0a, 0x64, 0x00,
    0xcb, 0x0a, 0x64, 0x00, 0xbb, 0x0a, 0x64, 0x00, 0x80, 0x0b, 0x64, 0x00, 0x15, 0x0c, 0x64, 0x00,
    0xa8, 0x0c, 0x64, 0x00, 0x41, 0x0d, 0x64, 0x00, 0xde, 0x0d, 0x64, 0x00, 0x7b, 0x0e, 0x64, 0x00,
    0x1c, 0x0f, 0x64, 0x00, 0xbf, 0x0f, 0x64, 0x00, 0x65, 0x10, 0x64, 0x00, 0x0d, 0x11, 0x64, 0x00,
    0xb7, 0x11, 0x64, 0x00, 0x63, 0x12, 0x64, 0x00, 0x12, 0x13, 0x64, 0x00, 0xc2, 0x13, 0x64, 0x00,
    0x6e, 0x14, 0x64, 0x00, 0x21, 0x15, 0x64, 0x00, 0xd7, 0x15, 0x64, 0x00, 0x8d, 0x16, 0x64, 0x00,
    0x46, 0x17, 0x64, 0x00, 0x00, 0x18, 0x64, 0x00, 0xba, 0x18, 0x64, 0x00, 0x76, 0x19, 0x64, 0x00,
    0x8e, 0x19, 0x10, 0x00, 0x1e, 0x19, 0x00, 0x00, 0xb8, 0x18, 0x0e, 0x00, 0xd7, 0x18, 0x4c, 0x00,
    0x8a, 0x19, 0x64, 0x00, 0x47, 0x1a, 0x64, 0x00, 0x04, 0x1b, 0x64, 0x00, 0xc1, 0x1b, 0x64, 0x00,
    0x7f, 0x1c, 0x64, 0x00, 0x3d, 0x1d, 0x64, 0x00, 0xfb, 0x1d, 0x64, 0x00, 0xba, 0x1e, 0x64, 0x00,
    0x7a, 0x1f, 0x64, 0x00, 0x2a, 0x20, 0x5a, 0x00, 0x70, 0x1f, 0x03, 0x00, 0xbe, 0x1d, 0x00, 0x00,
    0xe0, 0x1b, 0x00, 0x00, 0x21, 0x1a, 0x00, 0x00, 0x66, 0x18, 0x00, 0x00, 0x15, 0x17, 0x00, 0x00,
    0x5a, 0x16, 0x00, 0x00, 0xce, 0x15, 0x00, 0x00, 0xd3, 0x22, 0x3e, 0x00, 0x7b, 0x23, 0x44, 0x00,
    0xb4, 0x22, 0x06, 0x00, 0xaa, 0x21, 0x00, 0x00, 0xf6, 0x1f, 0x00, 0x00, 0xde, 0x1d, 0x00, 0x00,
    0x66, 0x1b, 0x00, 0x00, 0x83, 0x18, 0x00, 0x00, 0x43, 0x15, 0x00, 0x00, 0x58, 0x12, 0x00, 0x00,
    0x3c, 0x11, 0x00, 0x00, 0x26, 0x10, 0x00, 0x00, 0x16, 0x0f, 0x00, 0x00, 0x0b, 0x0e, 0x00, 0x00,
    0x07, 0x0d, 0x00, 0x00, 0x07, 0x0c, 0x00, 0x00, 0x0d, 0x0b, 0x00, 0x00, 0x16, 0x0a, 0x00, 0x00,
    0x26, 0x09, 0x00, 0x00, 0x3a, 0x08, 0x00, 0x00, 0x34, 0x06, 0x00, 0x00, 0x0a, 0x06, 0x31, 0x00,
    0x2c, 0x06, 0x32, 0x00, 0xa5, 0x06, 0x4c, 0x00, 0xbf, 0x07, 0x5e, 0x00, 0x6a, 0x09, 0x64, 0x00,
    0x28, 0x0b, 0x64, 0x00, 0xf6, 0x0c, 0x64, 0x00, 0xc9, 0x0e, 0x64, 0x00, 0xaa, 0x10, 0x64, 0x00,
    0x97, 0x12, 0x64, 0x00, 0x92, 0x14, 0x64, 0x00, 0xdd, 0x15, 0x46, 0x00, 0x03, 0x15, 0x00, 0x00,
    0x49, 0x14, 0x1c, 0x00, 0xec, 0x15, 0x64, 0x00, 0x10, 0x18, 0x64, 0x00, 0x49, 0x1a, 0x64, 0x00,
    0xa7, 0x1c, 0x64, 0x00, 0x29, 0x1f, 0x5e, 0x00, 0xf2, 0x1e, 0x07, 0x00, 0xe0, 0x1d, 0x00, 0x00,
    0xc3, 0x1c, 0x00, 0x00, 0x7e, 0x1c, 0x1e, 0x00, 0xd2, 0x1e, 0x64, 0x00, 0x98, 0x21, 0x64, 0x00,
    0x83, 0x24, 0x64, 0x00, 0x9c, 0x26, 0x5a, 0x00, 0xb0, 0x25, 0x03, 0x00, 0xa4, 0x24, 0x00, 0x00,
    0x91, 0x22, 0x00, 0x00, 0xc0, 0x20, 0x00, 0x00, 0x16, 0x1f, 0x00, 0x00, 0x7e, 0x1d, 0x00, 0x00,
    0x02, 0x1c, 0x00, 0x00, 0x98, 0x1a, 0x00, 0x00, 0x3c, 0x19, 0x00, 0x00, 0xf4, 0x17, 0x00, 0x00,
    0xc3, 0x16, 0x00, 0x00, 0xf1, 0x13, 0x00, 0x00, 0x26, 0x11, 0x00, 0x00, 0x57, 0x0e, 0x00, 0x00,
    0xde, 0x0b, 0x00, 0x00, 0x0b, 0x0d, 0x00, 0x00, 0x3c, 0x0e, 0x00, 0x00, 0xe6, 0x0d, 0x00, 0x00,
    0xe9, 0x0c, 0x00, 0x00, 0xe9, 0x0b, 0x00, 0x00, 0xef, 0x0a, 0x00, 0x00, 0xf9, 0x09, 0x00, 0x00,
    0x09, 0x09, 0x00, 0x00, 0x1d, 0x08, 0x00, 0x00, 0x37, 0x07, 0x00, 0x00, 0x4c, 0x06, 0x00, 0x00,
    0x5c, 0x05, 0x00, 0x00, 0x6c, 0x04, 0x00, 0x00, 0xec, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x07, 0x60, 0x00, 0x27, 0x0c, 0x64, 0x00, 0xda, 0x11, 0x64, 0x00, 0x43, 0x18, 0x64, 0x00,
    0x1b, 0x1e, 0x51, 0x00, 0xbf, 0x1f, 0x2f, 0x00, 0x07, 0x21, 0x31, 0x00, 0x7b, 0x21, 0x26, 0x00,
    0xa3, 0x21, 0x26, 0x00, 0xca, 0x21, 0x26, 0x00, 0xf1, 0x21, 0x26, 0x00, 0x1b, 0x22, 0x26, 0x00,
    0xb5, 0x21, 0x18, 0x00, 0x57, 0x1f, 0x00, 0x00, 0x93, 0x1c, 0x00, 0x00, 0xbe, 0x19, 0x00, 0x00,
    0xf7, 0x16, 0x00, 0x00, 0xaa, 0x14, 0x15, 0x00, 0xda, 0x19, 0x64, 0x00, 0x93, 0x19, 0x10, 0x00,
    0xcf, 0x16, 0x00, 0x00, 0x98, 0x1b, 0x53, 0x00, 0x41, 0x1d, 0x25, 0x00, 0x9e, 0x1a, 0x00, 0x00,
    0xd2, 0x17, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x37, 0x12, 0x00, 0x00, 0x6b, 0x0f, 0x00, 0x00,
    0x9b, 0x0c, 0x00, 0x00, 0xdd, 0x09, 0x00, 0x00, 0x8b, 0x08, 0x00, 0x00, 0x9b, 0x07, 0x00, 0x00,
    0xab, 0x06, 0x00, 0x00, 0xbb, 0x05, 0x00, 0x00, 0xcb, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7c, 0x04, 0x1e, 0x00, 0x9f, 0x08, 0x64, 0x00, 0xbd, 0x0d, 0x64, 0x00, 0xab, 0x13, 0x64, 0x00,
    0x3c, 0x1a, 0x64, 0x00, 0x29, 0x21, 0x64, 0x00, 0x3c, 0x26, 0x64, 0x00, 0x24, 0x25, 0x10, 0x00,
    0x94, 0x22, 0x00, 0x00, 0xe1, 0x1f, 0x00, 0x00, 0x1a, 0x1d, 0x00, 0x00, 0x4d, 0x1a, 0x00, 0x00,
    0x7d, 0x17, 0x00, 0x00, 0xcb, 0x19, 0x3c, 0x00, 0x7d, 0x20, 0x64, 0x00, 0xab, 0x1f, 0x10, 0x00,
    0xf6, 0x1c, 0x00, 0x00, 0xcb, 0x1c, 0x1e, 0x00, 0x45, 0x23, 0x64, 0x00, 0xa4, 0x26, 0x64, 0x00,
    0x37, 0x26, 0x46, 0x00, 0xd2, 0x23, 0x00, 0x00, 0x2c, 0x21, 0x00, 0x00, 0x71, 0x1e, 0x00, 0x00,
    0xa5, 0x1b, 0x00, 0x00, 0xd8, 0x18, 0x00, 0x00, 0x0e, 0x16, 0x00, 0x00, 0x3f, 0x13, 0x00, 0x00,
    0x71, 0x10, 0x00, 0x00, 0xa8, 0x0d, 0x00, 0x00, 0xd8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc4, 0x04, 0x1e, 0x00, 0xfc, 0x08, 0x64, 0x00, 0x32, 0x0e, 0x64, 0x00, 0x2b, 0x14, 0x64, 0x00,
    0xc1, 0x1a, 0x64, 0x00, 0x71, 0x1d, 0x2a, 0x00, 0x78, 0x1c, 0x15, 0x00, 0x37, 0x1a, 0x00, 0x00,
    0x67, 0x17, 0x00, 0x00, 0x77, 0x1a, 0x53, 0x00, 0xc6, 0x20, 0x5a, 0x00, 0x95, 0x1f, 0x03, 0x00,
    0xd2, 0x20, 0x3c, 0x00, 0xd9, 0x24, 0x46, 0x00, 0x9f, 0x24, 0x1e, 0x00, 0x60, 0x26, 0x46, 0x00,
    0x74, 0x26, 0x1e, 0x00, 0x6d, 0x26, 0x46, 0x00, 0xbf, 0x26, 0x46, 0x00, 0xac, 0x25, 0x28, 0x00,
    0x70, 0x26, 0x3c, 0x00, 0x38, 0x26, 0x46, 0x00, 0xd4, 0x23, 0x00, 0x00, 0x2c, 0x21, 0x00, 0x00,
    0x73, 0x1e, 0x00, 0x00, 0xa7, 0x1b, 0x00, 0x00, 0xd8, 0x18, 0x00, 0x00, 0x11, 0x16, 0x00, 0x00,
    0x3f, 0x13, 0x00, 0x00, 0x72, 0x10, 0x00, 0x00, 0x56, 0x18, 0x31, 0x00, 0x7d, 0x18, 0x1b, 0x00,
    0x4b, 0x17, 0x00, 0x00, 0xf4, 0x15, 0x00, 0x00, 0xa4, 0x14, 0x00, 0x00, 0x8f, 0x13, 0x0a, 0x00,
    0xc5, 0x14, 0x60, 0x00, 0xc7, 0x16, 0x64, 0x00, 0xed, 0x18, 0x64, 0x00, 0x34, 0x1b, 0x64, 0x00,
    0xa4, 0x1d, 0x64, 0x00, 0x44, 0x20, 0x64, 0x00, 0xca, 0x22, 0x64, 0x00, 0x45, 0x24, 0x64, 0x00,
    0xcb, 0x25, 0x64, 0x00, 0xc9, 0x26, 0x64, 0x00, 0xc3, 0x26, 0x64, 0x00, 0xb4, 0x26, 0x64, 0x00,
    0xc1, 0x26, 0x64, 0x00, 0x11, 0x26, 0x64, 0x00, 0x3b, 0x24, 0x64, 0x00, 0xfe, 0x22, 0x5a, 0x00,
    0x31, 0x20, 0x03, 0x00, 0x62, 0x1d, 0x00, 0x00, 0x09, 0x1b, 0x53, 0x00, 0x97, 0x1d, 0x64, 0x00,
    0x24, 0x21, 0x64, 0x00, 0x97, 0x25, 0x64, 0x00, 0xa8, 0x26, 0x64, 0x00, 0xd1, 0x23, 0x10, 0x00,
    0x09, 0x25, 0x53, 0x00, 0x94, 0x26, 0x64, 0x00, 0x9c, 0x26, 0x64, 0x00, 0x93, 0x26, 0x64, 0x00,
    0xc4, 0x26, 0x64, 0x00, 0xd2, 0x22, 0x10, 0x00, 0xa0, 0x1e, 0x00, 0x00, 0x6e, 0x1a, 0x1e, 0x00,
    0x04, 0x1b, 0x64, 0x00, 0xeb, 0x1f, 0x64, 0x00, 0x97, 0x1f, 0x28, 0x00, 0x65, 0x1b, 0x00, 0x00,
    0x33, 0x17, 0x00, 0x00, 0x01, 0x13, 0x00, 0x00, 0xcf, 0x0e, 0x00, 0x00, 0x9d, 0x0a, 0x00, 0x00,
    0xdd, 0x08, 0x00, 0x00, 0xed, 0x07, 0x00, 0x00, 0xac, 0x07, 0x53, 0x00, 0xa7, 0x08, 0x53, 0x00,
    0xa8, 0x09, 0x53, 0x00, 0xb3, 0x0a, 0x52, 0x00, 0xbf, 0x0b, 0x52, 0x00, 0xd3, 0x0c, 0x52, 0x00,
    0xed, 0x0d, 0x52, 0x00, 0x2b, 0x0f, 0x52, 0x00, 0x8d, 0x10, 0x52, 0x00, 0x1f, 0x12, 0x52, 0x00,
    0x0f, 0x14, 0x53, 0x00, 0xee, 0x14, 0x1a, 0x00, 0xed, 0x13, 0x00, 0x00, 0xcb, 0x12, 0x00, 0x00,
    0xad, 0x11, 0x00, 0x00, 0x95, 0x10, 0x00, 0x00, 0x84, 0x0f, 0x00, 0x00, 0x77, 0x0e, 0x00, 0x00,
    0x70, 0x0d, 0x00, 0x00, 0x76, 0x0c, 0x00, 0x00, 0x79, 0x0b, 0x00, 0x00, 0x82, 0x0a, 0x00, 0x00,
    0x90, 0x09, 0x00, 0x00, 0xa1, 0x08, 0x00, 0x00, 0xb9, 0x07, 0x00, 0x00, 0xd2, 0x06, 0x00, 0x00,
    0xe3, 0x05, 0x00, 0x00, 0xf3, 0x04, 0x00, 0x00, 0xc2, 0x15, 0x64, 0x00, 0x79, 0x16, 0x64, 0x00,
    0x31, 0x17, 0x64, 0x00, 0xea, 0x17, 0x64, 0x00, 0xa4, 0x18, 0x64, 0x00, 0x5f, 0x19, 0x64, 0x00,
    0x1c, 0x1a, 0x64, 0x00, 0xd9, 0x1a, 0x64, 0x00, 0x94, 0x1b, 0x62, 0x00, 0x74, 0x1b, 0x0c, 0x00,
    0xd9, 0x1a, 0x00, 0x00, 0x64, 0x19, 0x00, 0x00, 0xa6, 0x17, 0x00, 0x00, 0xf1, 0x15, 0x00, 0x00,
    0x31, 0x14, 0x00, 0x00, 0x75, 0x12, 0x00, 0x00, 0xbc, 0x10, 0x00, 0x00, 0x09, 0x0f, 0x00, 0x00,
    0x5c, 0x0d, 0x00, 0x00, 0xbb, 0x0b, 0x00, 0x00, 0x25, 0x0a, 0x00, 0x00, 0x13, 0x09, 0x00, 0x00,
    0xae, 0x08, 0x00, 0x00, 0x54, 0x08, 0x00, 0x00, 0xfb, 0x07, 0x00, 0x00,
};

void nis_rev_init(NisRev *r)
{
    memset(r, 0, sizeof *r);
    r->state = 1; r->rpm = 1000.0f; r->thr = 0;
}
void nis_rev_msg(NisRev *r, int msg, int a, int b)                     /* 0x459550 */
{
    if (msg == 7) { r->msg_pat = 1; r->pat = a; r->pat_k = (float)b; }
    else if (msg == 8) { r->msg_rev = 1; r->rev_n = (float)a; r->rev_k = (float)b; }
}
/* 0x46b240 + 0x4599e0: x in 0..4 between two entries */
static float rev_lerp(float a, float b, int x)
{
    float f = (float)x / 4.0f;
    if (!(0 < f)) f = 0;
    else if (1 < f) f = 1;
    return (b - a) * f + a;
}
static void rev_entry(NisRev *r)
{
    const unsigned char *p = r->p;
    r->rpm = rev_lerp((float)rd16(p), (float)rd16(p + 4), r->step);
    r->thr = rev_lerp((float)p[2], (float)p[6], r->step);
}
int nis_rev_step(NisRev *r, int nis_mode)                              /* 0x47c490 -> 0x46fa30 */
{
    if (!nis_mode) { r->state = 1; r->rpm = 1000.0f; r->thr = 0; return 0; }
    if (r->state == 1 && r->msg_rev) {
        r->state = 2; r->msg_rev = 0;
        r->revs = (int)r->rev_n + 1;
    }
    if (r->msg_pat) {
        /* the tables by pattern (0x46ff64), anything else the default */
        static const unsigned short at[14] = { 0x198, 0x238, 0x2a8, 0x328, 0x398, 0x3f8, 0x448,
                                               0x558, 0x640, 0x6d0, 0x750, 0x7c8, 0x888, 0x8f8 };
        static const unsigned char cnt[14] = { 0x27, 0x1a, 0x1e, 0x1b, 0x16, 0x13, 0x43,
                                               0x38, 0x22, 0x1e, 0x1d, 0x2f, 0x1b, 0x18 };
        r->msg_pat = 0;
        unsigned i = (unsigned)(r->pat - 2);
        r->p = k_revpat + (i < 14 ? at[i] - 0x70 : 0);
        r->left = i < 14 ? cnt[i] : 0x48;
        r->state = 3; r->step = 0;
        rev_entry(r);
    }
    if (r->state == 2) {
        int v = r->revs - 1;
        r->revs = v < 0 ? 0 : v;
        if (r->revs == 0) r->state = 1;
        else { r->rpm += 900.0f * r->rev_k * 0.01f; r->thr += 15.0f; }
    } else if (r->state == 3) {
        if (++r->step == 4) {
            r->step = 0;
            if (--r->left == 0) r->state = 1;
            else r->p += 4;
        }
        if (r->state != 1) rev_entry(r);
    } else {
        r->rpm -= 500.0f; r->thr -= 15.0f;
    }
    r->rpm = 1000.0f < r->rpm ? (10000.0f < r->rpm ? 10000.0f : r->rpm) : 1000.0f;
    r->thr = 0 < r->thr ? (100.0f < r->thr ? 100.0f : r->thr) : 0;
    return 1;
}

const char *nis_name(const Nis *n) { return n ? n->name : ""; }
float nis_time(const Nis *n) { return n->time; }
float nis_length(const Nis *n) { return n->t_len; }
float nis_countdown_start(const Nis *n) { return n->t_cd; }
float nis_countdown_interval(const Nis *n) { return n->interval; }
int   nis_playing(const Nis *n) { return n->playing; }

static int container_bind(Nis *n, const char *name, Codec *out, int *nout, float *end)
{
    for (int b = 0; b < n->nbank; b++) {
        const unsigned char *a = bank_find(&n->bank[b], name);
        if (!a) continue;
        const Bank *bk = &n->bank[b];
        if (rd16(a) != 15) return 0;
        int cnt = (int)rd16(a + 8);
        *nout = 0;
        for (int i = 0; i < cnt && *nout < 4; i++) {
            unsigned so = rd32(a + 0xc + 4 * i);
            if (so + 12 > bk->len) continue;
            Codec *c = &out[*nout];
            if (!codec_init(c, bk->data + so)) continue;
            unsigned st = rd32(c->a + 4);
            if (st + 4 > bk->len) continue;
            c->s = bk->data + st;
            c->nch = (int)rd16(c->s); c->bits = c->s[2];
            c->frames = (int)rd16(c->a + 8);
            if (c->nch > 160 || c->frames < 1 || (c->bits != 4 && c->bits != 8 && c->bits != 16)) continue;
            c->cur = -1;
            (*nout)++;
        }
        *end = (float)rd16(a + 0xa);            /* 0x6e2110: the container's frames */
        return 1;
    }
    return 0;
}

/* ---- characters (items 0x37040) -------------------------------------------- */
/* 0x4013f0: o = a * b (row vectors) */
static void m4mul(const float *a, const float *b, float *o)
{
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(o, r, sizeof r);
}
/* 0x441000: a point through a row-vector matrix */
static void m4pt(const float *m, const float *p, float *o)
{
    float x = p[0], y = p[1], z = p[2];
    o[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
    o[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
    o[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
}
/* 0x6e2d60: a pose (scale3, x-scale, quat xyzw, translation) to a matrix;
   0x6e3b10 then scales the x column by the extra x-scale */
static void pose_matrix(const float *p, float *m)
{
    float sx = p[0], sy = p[1], sz = p[2], x = p[4], y = p[5], z = p[6], w = p[7];
    float y2 = y + y, z2 = z + z, x2 = x + x, xw = x2 * w, xx = x2 * x;
    m[0] = (1.0f - (z2 * z + y2 * y)) * sx;  m[1] = (x * y2 + z2 * w) * sx;  m[2] = (z2 * x - w * y2) * sx;  m[3] = 0;
    m[4] = (x * y2 - z2 * w) * sy;  m[5] = (1.0f - (z2 * z + xx)) * sy;  m[6] = (z2 * y + xw) * sy;  m[7] = 0;
    m[8] = (z2 * x + w * y2) * sz;  m[9] = (z2 * y - xw) * sz;  m[10] = (1.0f - (y2 * y + xx)) * sz;  m[11] = 0;
    m[12] = p[8]; m[13] = p[9]; m[14] = p[10]; m[15] = 1.0f;
    m[0] *= p[3]; m[4] *= p[3]; m[8] *= p[3];
}
static const unsigned char *skel_bone(const Skel *k, int i) { return k->data + k->off + 0x10 + (unsigned)i * 0x70; }

/* Codecs 20 (3 floats per output: translations) and 21 (1 float per output),
   0x6e9780 / 0x6e82d0 with their setups 0x6e89c0 / 0x6e78f0. The animation:
   +4 the output pose indices (u16), +8 the key times (u16, or 0: one key
   per frame), +0xc u16 keys, +0xe u16 outputs, +0x10 u8 block shift,
   +0x11 u8 static outputs, +0x14 per output {f min[c], f range[c], u16
   delta min[c], u16 delta range[c]}, then blocks of 2^shift keys: the
   block's first key as u16 values, then a u8 delta per key. */
static int c20_keyval(const Bank *b, const unsigned char *A, int k, int nc, float *out, int cap)
{
    const unsigned char *E = b->data + b->len;
    int nout = (int)rd16(A + 0xe), bits = A[0x10];
    int rec = nc == 3 ? 0x24 : 0xc;
    float k1 = 1.5259022e-05f;                                          /* 0x7c1da0 / 0x7c1d18: 1/65535 */
    float k2 = 0.003921569f;                                            /* 0x7c1d98 / 0x7c1d10: 1/255 */
    int row = nout * nc;
    unsigned bsz = ((unsigned)(((1 << bits) + 1) * row) + 1) & ~1u;
    const unsigned char *base = A + 0x14 + nout * rec + bsz * (unsigned)(k >> bits);
    int w = k & ((1 << bits) - 1);
    if (nout * nc > cap || base + (size_t)(w + 2) * row > E) return 0;
    for (int o = 0; o < nout; o++) {
        const unsigned char *r = A + 0x14 + o * rec;
        for (int c = 0; c < nc; c++) {
            float mn = rdf(r + 4 * c), sc = rdf(r + 4 * (nc + c));
            unsigned dmin = rd16(r + 4 * 2 * nc + 2 * c), drng = rd16(r + 4 * 2 * nc + 2 * nc + 2 * c);
            float scale = sc * k1;
            float t = (float)dmin * sc * k1, doff = (t + t) - sc;
            float u = (float)drng * sc * k1, dsc = (u + u) * k2;
            float v = (float)rd16(base + 2 * (o * nc + c)) * scale + mn;
            for (int j = 1; j <= w; j++) v = (float)base[(j + 1) * row + o * nc + c] * dsc + doff + v;
            out[o * nc + c] = v;
        }
    }
    return 1;
}
static void c20_eval(const Bank *b, const unsigned char *A, float t, float *pose, int npose, int nc)
{
    const unsigned char *D = b->data, *E = D + b->len;
    int nout = (int)rd16(A + 0xe), nk = (int)rd16(A + 0xc), bits = A[0x10], nst = A[0x11];
    int rec = nc == 3 ? 0x24 : 0xc;
    unsigned oidx = rd32(A + 4), ktab = rd32(A + 8);
    if (nout && nk > 0 && oidx + 2u * nout <= b->len) {
        float r = t < 0 ? t + 0.4999990f : t - 0.4999990f;               /* 0x6e4900 */
        int fi = (int)lrintf(r), k;
        float frac = 0; int interp = 0;
        if (!ktab) {
            k = fi < 0 ? 0 : fi >= nk ? nk - 1 : fi;
            if (t != (float)fi) { frac = t - (float)fi; interp = 1; }
        } else {
            if (ktab + 2u * (unsigned)(nk > 1 ? nk - 1 : 1) > b->len) return;
            if (fi < (int)rd16(D + ktab)) k = 0;
            else {
                int i = 0;
                while (i < nk - 2 && (int)rd16(D + ktab + 2 * (i + 1)) <= fi) i++;
                k = i + 1;
            }
            if (k) {
                float t0 = (float)rd16(D + ktab + 2 * (k - 1));
                if (t != t0 && k < nk - 1) { frac = (t - t0) / ((float)rd16(D + ktab + 2 * k) - t0); interp = 1; }
            } else if (t != 0.0f) { frac = t / (float)rd16(D + ktab); interp = 1; }
        }
        float v0[3 * 64], v1[3 * 64];
        if (c20_keyval(b, A, k, nc, v0, 3 * 64)) {
            if (interp && k < nk - 1 && c20_keyval(b, A, k + 1, nc, v1, 3 * 64))
                for (int i = 0; i < nout * nc; i++) v0[i] = (v1[i] - v0[i]) * frac + v0[i];
            for (int o = 0; o < nout; o++) {
                int ix = (int)rd16(D + oidx + 2 * o);
                for (int c = 0; c < nc; c++) if (ix + c < npose) pose[ix + c] = v0[o * nc + c];
            }
        }
    }
    if (nst) {                                                           /* 0x6e88f0: the static outputs */
        int row = nout * nc, rem = ((1 << bits) - 1) & nk;
        uintptr_t p = (uintptr_t)(A + nout * rec + (((((1 << bits) + 1) * row) + 1) & ~1) * (nk >> bits) + 0x14);
        if (rem) p = ((uintptr_t)((rem + 1) * row + 1) + p) & ~(uintptr_t)1;
        if (!nout) p = (p + 1) & ~(uintptr_t)1;
        const unsigned char *ix = (const unsigned char *)p;
        const unsigned char *vals = (const unsigned char *)(((uintptr_t)ix + (uintptr_t)nst * 2 + 3) & ~(uintptr_t)3);
        if (vals + 4 * nc * nst > E) return;
        for (int i = 0; i < nst; i++) {
            int x = (int)rd16(ix + 2 * i);
            for (int c = 0; c < nc; c++) if (x + c < npose) pose[x + c] = rdf(vals + 4 * (nc * i + c));
        }
    }
}

/* 0x434b00: the animation object's frame (frames at 30 x rate per second).
   Flag 8 runs forward (held at the end, or looped with 0x20), 0x10 runs
   there and back; 0x40 limits it to [+0x40, +0x44]. The 0x80/0x100 lead-in
   segments (+0x6c/+0x70) are 0 in every scene item and stay inert. */
static int char_advance(NisChar *c, float dt)
{
    float f = 30.0f * dt * c->rate;                                      /* 0x785bd8 */
    float span, lo, hi;
    if (c->flags & 0x40) { span = c->r1 - c->r0; lo = c->r0; hi = c->r1; }
    else { span = c->len; lo = 0; hi = c->len; }
    if (!(c->flags & 8) && !(c->flags & 0x10)) c->flags |= 8;
    else if ((c->flags & 8) && (c->flags & 0x10)) c->flags &= ~0x10u;
    if (c->flags & 8) {
        float fr = c->frame + f;
        if (hi < fr) {
            if (!(c->flags & 0x20)) { c->frame = hi; return 1; }
            c->frame = fr - span;
            return 0;
        }
        c->frame = fr;
        return 0;
    }
    /* 0x10 (there and back, direction in anim +8): no scene item sets it */
    return 0;
}
/* 0x431400 (forced): the codecs into the pose, 0x6e3b10 to matrices, then
   x the inverse bind (0x43c990: ib * m). The pose runs every tick (the
   ground follows bone 1); the inverse bind step only feeds drawing and the
   flares, so char_skin does it once per pose, when one of them asks. */
static void char_pose(NisChar *c)
{
    const Skel *k = c->sk;
    for (int i = 0; i < c->nsub; i++) {
        int ty = (int)rd16(c->sub[i]);
        if (ty == 11) codec_eval(&c->c11[i], c->frame, c->pose);
        else if (ty == 20) c20_eval(c->bank, c->sub[i], c->frame, c->pose, k->nb * 12, 3);
        else if (ty == 21) c20_eval(c->bank, c->sub[i], c->frame, c->pose, k->nb * 12, 1);
    }
    for (int i = 0; i < k->nb; i++) {
        float *m = c->bw + 16 * i;
        pose_matrix(c->pose + 12 * i, m);
        int par = (int)rd32(skel_bone(k, i) + 0xc);
        if (par >= 0 && par < i) m4mul(m, c->bw + 16 * par, m);         /* 0x811e34 -> 0x6efa00 */
    }
    c->skin_ok = 0;
}
static void char_skin(NisChar *c)
{
    if (c->skin_ok) return;
    for (int i = 0; i < c->sk->nb; i++) m4mul(c->ib + 16 * i, c->bw + 16 * i, c->skin + 16 * i);
    c->skin_ok = 1;
}
/* the world object (+0x10) = item matrix x the scene root (0x43b000: the
   marker's rotation, then its point) */
static void char_world(const Nis *n, NisChar *c)
{
    float S[16] = { n->R[0], n->R[1], 0, 0,  n->R[2], n->R[3], 0, 0,  0, 0, 1, 0,  n->T[0], n->T[1], n->T[2], 1 };
    m4mul(c->M, S, c->W);
}
/* 0x431280: bone b in the world */
static void char_bone_world(const NisChar *c, int b, float o[3])
{
    m4pt(c->W, c->bw + 16 * b + 12, o);
}
/* 0x436050 / 0x437be0: stand on the collision ground under the pelvis,
   0.05 above it, smoothed half-and-half after the first find */
static void char_ground(Nis *n, NisChar *c, int first)
{
    if (!n->ground) return;
    float p[3];
    char_bone_world(c, 1, p);
    int found = 0;
    float g = n->ground(n->gu, c->gstate, p, &found);                   /* 0x5e72e0 */
    if (!found) return;
    if (first || !c->gfound) { c->gz = g; c->gfound = 1; }
    else c->gz = 0.5f * g + (1.0f - 0.5f) * c->gz;                       /* 0x785c30 */
    c->M[14] = (c->gz - c->W[14]) + c->M[14] + 0.05f;                     /* 0x785c20 */
    char_world(n, c);
}
static void char_update(Nis *n, NisChar *c, float dt)                    /* 0x436050 */
{
    char_advance(c, dt);
    char_pose(c);
    char_world(n, c);
    char_ground(n, c, 0);
}

static const unsigned char *bank_find_hash(const Nis *n, unsigned h, const Bank **which)
{
    for (int b = 0; b < n->nbank; b++) {
        const Bank *bk = &n->bank[b];
        const unsigned char *D = bk->data, *k = D + bk->bank;
        unsigned cnt = rd32(k + 4), pa = rd32(k + 0x10), pn = rd32(k + 0x14);
        for (unsigned i = 0; i < cnt; i++) {
            if (pn + 4 * i + 4 > bk->len || pa + 4 * i + 4 > bk->len) break;
            unsigned no = rd32(D + pn + 4 * i), ao = rd32(D + pa + 4 * i);
            if (no < bk->len && ao + 12 <= bk->len && nis_hash((const char *)D + no) == h) { *which = bk; return D + ao; }
        }
    }
    return NULL;
}
/* ---- the same EAGL decoding for the world animations (worldanim.c) ------- */
struct NisBanks { Bank *b; int n; };
NisBanks *nis_banks_load(const unsigned char *d, long len)
{
    NisBanks *k = (NisBanks *)calloc(1, sizeof *k);
    if (!k) return NULL;
    for (long q = 0; q + 8 <= len;) {
        unsigned cid = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > len) break;
        if (cid == 0xe34010u)
            for (unsigned j = 0; j + 4 <= sz; j++)
                if (!memcmp(d + q + 8 + j, "\x7f" "ELF", 4)) {
                    Bank nb;
                    Bank *g = (Bank *)realloc(k->b, (size_t)(k->n + 1) * sizeof *g);
                    if (g) { k->b = g; if (bank_from_elf(d + q + 8 + j, sz - j, &nb)) k->b[k->n++] = nb; }
                    break;
                }
        q += 8 + (long)sz;
    }
    return k;
}
int nis_banks_count(const NisBanks *k) { return k ? k->n : 0; }
void nis_banks_free(NisBanks *k)
{
    if (!k) return;
    for (int i = 0; i < k->n; i++) free(k->b[i].data);
    free(k->b);
    free(k);
}
struct NisChan { const Bank *bank; const unsigned char *sub[4]; Codec c11[4]; int nsub; };
/* 0x434850 (the animation by name hash) -> 0x6e2d40 (its instance): a type
   15 container of codec 11 / 20 / 21 parts, as char_init binds them */
NisChan *nis_chan_bind(const NisBanks *k, unsigned h, float *frames)
{
    if (!k) return NULL;
    const Bank *bk = NULL;
    const unsigned char *a = NULL;
    for (int b = 0; b < k->n && !a; b++) {
        const unsigned char *D = k->b[b].data, *t = D + k->b[b].bank;
        unsigned cnt = rd32(t + 4), pa = rd32(t + 0x10), pn = rd32(t + 0x14);
        for (unsigned i = 0; i < cnt; i++) {
            if (pn + 4 * i + 4 > k->b[b].len || pa + 4 * i + 4 > k->b[b].len) break;
            unsigned no = rd32(D + pn + 4 * i), ao = rd32(D + pa + 4 * i);
            if (no < k->b[b].len && ao + 12 <= k->b[b].len && nis_hash((const char *)D + no) == h) {
                a = D + ao; bk = &k->b[b]; break;
            }
        }
    }
    if (!a || rd16(a) != 15) return NULL;
    NisChan *c = (NisChan *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->bank = bk;
    int cnt = (int)rd16(a + 8);
    for (int i = 0; i < cnt && c->nsub < 4; i++) {
        unsigned so = rd32(a + 0xc + 4 * i);
        if (so + 0x14 > bk->len) continue;
        const unsigned char *s = bk->data + so;
        int ty = (int)rd16(s);
        if (ty == 11) {
            Codec *q = &c->c11[c->nsub];
            if (!codec_init(q, s)) continue;
            unsigned st = rd32(s + 4);
            if (st + 4 > bk->len) continue;
            q->s = bk->data + st;
            q->nch = (int)rd16(q->s); q->bits = q->s[2];
            q->frames = (int)rd16(s + 8);
            if (q->nch > 160 || q->frames < 1 || (q->bits != 4 && q->bits != 8 && q->bits != 16)) continue;
            int bad = 0;                                                /* pose slots in the scratch pose */
            for (int g = 0; g < q->nch / 4; g++) bad |= rd16(s + 10 + 2 * g) + 4 > NIS_CHAN_POSE;
            if (bad) continue;
            q->cur = -1;
        } else if (ty != 20 && ty != 21) continue;
        c->sub[c->nsub++] = s;
    }
    if (frames) *frames = (float)rd16(a + 0xa);                       /* 0x6e2110 */
    return c;
}
void nis_chan_eval(NisChan *c, float frame, float *pose, int npose)
{
    if (!c || npose > NIS_CHAN_POSE) return;
    float t[NIS_CHAN_POSE];
    memcpy(t, pose, sizeof(float) * (size_t)npose);
    for (int i = 0; i < c->nsub; i++) {
        int ty = (int)rd16(c->sub[i]);
        if (ty == 11) codec_eval(&c->c11[i], frame, t);
        else if (ty == 20) c20_eval(c->bank, c->sub[i], frame, t, NIS_CHAN_POSE, 3);
        else if (ty == 21) c20_eval(c->bank, c->sub[i], frame, t, NIS_CHAN_POSE, 1);
    }
    memcpy(pose, t, sizeof(float) * (size_t)npose);
}
void nis_chan_free(NisChan *c) { free(c); }
void nis_root_rest(float pose[24])
{
    for (int b = 0; b < 2; b++) {                                       /* 0x6e3c70, as slot_place */
        float *p = pose + 12 * b;
        memset(p, 0, 12 * sizeof *p);
        p[0] = p[1] = p[2] = p[3] = 1.0f; p[7] = 1.0f; p[11] = 1.0f;
    }
}
void nis_root_bone1(const float pose[24], float m[16])
{
    float m0[16];                                                       /* PoseSQTToGlobal */
    pose_matrix(pose, m0);
    pose_matrix(pose + 12, m);
    m4mul(m, m0, m);
}

/* 0x437be0 for one item (type 1): 1 when it makes a character */
static int char_init(Nis *n, NisChar *c, const unsigned char *it)
{
    memset(c, 0, sizeof *c);
    if (rd32(it) != 1) return 0;                    /* 0x433020: type 6 items draw nothing here */
    for (int i = 0; i < 16; i++) c->M[i] = rdf(it + 0x10 + 4 * i);
    c->solid = rd32(it + 0x50);
    unsigned fl = rd32(it + 0xc), skh = rd32(it + 0x70), anh = rd32(it + 0x6c);
    if (!skh || !anh) return 0;                     /* ROOT-skeleton props: none in any scene */
    for (int i = 0; i < n->nskel; i++) if (n->skel[i].hash == skh) c->sk = &n->skel[i];
    const unsigned char *a = bank_find_hash(n, anh, &c->bank);
    if (!c->sk || !a || rd16(a) != 15) return 0;
    float rate = rdf(it + 0x74);
    c->rate = rate == 0.0f ? 1.0f : rate;           /* 0x7d7d3c */
    unsigned f = 0;
    if (fl & 0x40) f = 0x60; else if (fl & 0x20) f = 0x20;
    f |= (fl & 0x10) ? 0x10 : 8;
    if (fl & 0x80) f |= 0x80;
    f |= fl & 0x700;
    c->flags = f | 1;                               /* skinned on its own skeleton */
    c->rate *= 0.5f;                                /* 0x784260: skeleton + animation */
    c->len = (float)rd16(a + 0xa);                  /* 0x6e2110 */
    int cnt = (int)rd16(a + 8);
    for (int i = 0; i < cnt && c->nsub < 4; i++) {
        unsigned so = rd32(a + 0xc + 4 * i);
        if (so + 0x14 > c->bank->len) continue;
        const unsigned char *s = c->bank->data + so;
        int ty = (int)rd16(s);
        if (ty == 11) {
            Codec *q = &c->c11[c->nsub];
            if (!codec_init(q, s)) continue;
            unsigned st = rd32(s + 4);
            if (st + 4 > c->bank->len) continue;
            q->s = c->bank->data + st;
            q->nch = (int)rd16(q->s); q->bits = q->s[2];
            q->frames = (int)rd16(s + 8);
            if (q->nch > 160 || q->frames < 1 || (q->bits != 4 && q->bits != 8 && q->bits != 16)) continue;
            q->cur = -1;
        } else if (ty != 20 && ty != 21) continue;
        c->sub[c->nsub++] = s;
    }
    int nb = c->sk->nb;
    c->pose = (float *)malloc(sizeof(float) * 12 * nb);
    c->bw = (float *)malloc(sizeof(float) * 16 * nb);
    c->skin = (float *)malloc(sizeof(float) * 16 * nb);
    c->ib = (float *)malloc(sizeof(float) * 16 * nb);
    if (!c->pose || !c->bw || !c->skin || !c->ib) {
        free(c->pose); free(c->bw); free(c->skin); free(c->ib); memset(c, 0, sizeof *c); return 0;
    }
    for (int i = 0; i < nb; i++)
        for (int j = 0; j < 16; j++) c->ib[16 * i + j] = rdf(skel_bone(c->sk, i) + 0x30 + 4 * j);
    /* 0x6e3c70: the skeleton's rest pose (x-scale 1: no table) */
    for (int i = 0; i < nb; i++) {
        const unsigned char *b = skel_bone(c->sk, i);
        float *p = c->pose + 12 * i;
        p[0] = rdf(b); p[1] = rdf(b + 4); p[2] = rdf(b + 8); p[3] = 1.0f;
        for (int j = 0; j < 4; j++) p[4 + j] = rdf(b + 0x10 + 4 * j);
        for (int j = 0; j < 3; j++) p[8 + j] = rdf(b + 0x20 + 4 * j);
        p[11] = 1.0f;
    }
    if (fl & 0x40) {                                /* 0x4313d0 */
        c->flags |= 0x40;
        c->r0 = (float)(int)rd32(it + 0x80) * c->rate;
        c->r1 = (float)(int)rd32(it + 0x84) * c->rate;
    }
    c->flags |= 0x80;                               /* lead-in +0x6c = item +0x78 (0 in every scene) */
    c->shadow = nb == 0x23 ? 0 : nb == 0x18 ? 1 : -1;
    return 1;
}
static void chars_free(Nis *n)
{
    for (int i = 0; i < n->nch; i++) { free(n->ch[i].pose); free(n->ch[i].bw); free(n->ch[i].skin); free(n->ch[i].ib); }
    free(n->ch); n->ch = NULL; n->nch = 0;
}
static void chars_make(Nis *n)
{
    chars_free(n);
    if (!n->nitems) return;
    n->ch = (NisChar *)calloc((size_t)n->nitems, sizeof *n->ch);
    if (!n->ch) return;
    for (int i = 0; i < n->nitems; i++)
        if (char_init(n, &n->ch[n->nch], n->items + 0x90 * i)) n->nch++;
}
/* 0x437be0 tail: the first pose, on the ground */
static void chars_start(Nis *n)
{
    for (int i = 0; i < n->nch; i++) {
        NisChar *c = &n->ch[i];
        c->frame = 0; c->gfound = 0;
        for (int j = 0; j < c->nsub; j++) c->c11[j].cur = -1;
        char_pose(c);
        char_world(n, c);
        char_ground(n, c, 1);
    }
}

int nis_nchars(const Nis *n) { return n ? n->nch : 0; }
int nis_char(const Nis *n, int i, NisCharView *out)
{
    if (!n || i < 0 || i >= n->nch) return 0;
    NisChar *c = (NisChar *)&n->ch[i];   /* the skin is a cache of the pose */
    char_skin(c);
    out->solid = c->solid;
    out->nbones = c->sk->nb;
    out->skin = c->skin;
    memcpy(out->W, c->W, sizeof out->W);
    char_bone_world(c, 1, out->pelvis);
    return 1;
}
int nis_char_pelvis(const Nis *n, int i, float pelvis[3])
{
    if (!n || i < 0 || i >= n->nch) return 0;
    char_bone_world(&n->ch[i], 1, pelvis);
    return 1;
}
/* 0x433220: the shadow quad between the feet of row 0x7ea1e0[shadow]
   (flag girl 5/9, crowd 5/8): 0.25 along, 0.35 across, each corner on the
   ground + 0.05 (0x785c24) */
int nis_char_shadow(const Nis *n, int i, float q[4][3])
{
    static const int feet[2][2] = { { 5, 9 }, { 5, 8 } };
    if (!n || i < 0 || i >= n->nch) return 0;
    const NisChar *c = &n->ch[i];
    if (c->shadow < 0 || feet[c->shadow][1] >= c->sk->nb) return 0;
    float A[3], B[3];
    char_bone_world(c, feet[c->shadow][0], A);
    char_bone_world(c, feet[c->shadow][1], B);
    float dx = 1.0f, dy = 0.0f;
    if (A[0] != B[0] || A[1] != B[1]) {                                  /* 0x43ce50 */
        dx = A[0] - B[0]; dy = A[1] - B[1];
        float l = sqrtf(dx * dx + dy * dy);
        if (l > 0) { dx /= l; dy /= l; }
    }
    float f1 = dx * 0.5f * 0.5f, f2 = 0.5f * 0.5f * dy;                 /* 0x785c2c x 0x784260 */
    float px = 0.7f * 0.5f * -dy, py = 0.7f * 0.5f * dx;                 /* 0x785c28 x 0x784260 */
    q[0][0] = (f1 - px) + A[0];  q[0][1] = (f2 - py) + A[1];
    q[1][0] = px + f1 + A[0];    q[1][1] = A[1] + py + f2;
    q[2][0] = (B[0] - f1) + px;  q[2][1] = (B[1] - f2) + py;
    q[3][0] = B[0] - (px + f1);  q[3][1] = B[1] - (py + f2);
    for (int k = 0; k < 4; k++) {
        float p[3] = { q[k][0], q[k][1], B[2] };
        int found = 0;
        float g = n->ground ? n->ground(n->gu, (void *)c->gstate, p, &found) : 0;
        q[k][2] = (found ? g : B[2]) + 0.05f;
    }
    return 1;
}
/* 0x4335c0: the hand flares of a flag girl model (0x437f8b: a 0x23-bone
   skeleton whose solid is one of FLAG_GIRL_MODEL_n_FLARES, table 0x7ea110):
   two bind-pose points per hand through the hand bones of row 0x7ea1e0
   (+8 / +0xc: 18, 28) and the world matrix; dir = first - second point */
int nis_char_flares(const Nis *n, int i, float pos[2][3], float dir[2][3])
{
    static const char *const names[4] = { "FLAG_GIRL_MODEL_5_FLARES", "FLAG_GIRL_MODEL_7_FLARES",
                                           "FLAG_GIRL_MODEL_8_FLARES", "FLAG_GIRL_MODEL_9_FLARES" };
    static const float pts[4][12] = {
        { 0.68651f, -0.0878f, 1.20303f, 0.59256f, 0.0478f, 1.20204f,
         -0.68651f, -0.0878f, 1.20303f, -0.59256f, 0.0478f, 1.20204f },
        { 0.6642f, -0.22838f, 1.22188f, 0.56942f, 0.05576f, 1.20907f,
         -0.6642f, -0.22838f, 1.22188f, -0.56942f, 0.05576f, 1.20907f },
        { 0.67353f, -0.03972f, 1.14111f, 0.58962f, 0.08162f, 1.20232f,
         -0.67353f, -0.03972f, 1.14111f, -0.58962f, 0.08162f, 1.20232f },
        { 0.61865f, -0.08303f, 1.03653f, 0.5235f, 0.07247f, 1.09304f,
         -0.61865f, -0.08303f, 1.03653f, -0.5235f, 0.07247f, 1.09304f } };
    static const int hands[2][2] = { { 18, 28 }, { 15, 21 } };
    if (!n || i < 0 || i >= n->nch) return 0;
    const NisChar *c = &n->ch[i];
    if (c->sk->nb != 0x23 || c->shadow < 0) return 0;
    int f = -1;
    for (int k = 0; k < 4; k++) if (nis_hash(names[k]) == c->solid) f = k;
    if (f < 0) return 0;
    char_skin((NisChar *)c);
    for (int h = 0; h < 2; h++) {
        int b = hands[c->shadow][h];
        if (b >= c->sk->nb) return 0;
        float a[3], q[3];
        m4pt(c->skin + 16 * b, pts[f] + 6 * h, a);
        m4pt(c->skin + 16 * b, pts[f] + 6 * h + 3, q);
        m4pt(c->W, a, pos[h]);
        float w[3]; m4pt(c->W, q, w);
        for (int j = 0; j < 3; j++) dir[h][j] = pos[h][j] - w[j];
    }
    return 1;
}
const char *nis_bundle_path(const Nis *n) { return n ? n->path : ""; }

static void walk_bundle(Nis *n, const unsigned char *b, unsigned len, const unsigned char **hdr)
{
    for (unsigned q = 0; q + 8 <= len;) {
        unsigned cid = rd32(b + q), sz = rd32(b + q + 4);
        if (q + 8 + sz > len) break;
        const unsigned char *d = b + q + 8;
        if (cid & 0x80000000u) walk_bundle(n, d, sz, hdr);
        else if (cid == 0x37030u && sz >= 0x34) *hdr = d;
        else if (cid == 0x37040u) {
            /* 0x4377f0: the item data starts 16-aligned in the chunk */
            uintptr_t a = ((uintptr_t)d + 15) & ~(uintptr_t)15;
            if (*hdr && a + 0x90 <= (uintptr_t)(d + sz)) {
                unsigned char *grow = (unsigned char *)realloc(n->items, (size_t)(n->nitems + 1) * 0x90);
                if (grow) { n->items = grow; memcpy(grow + n->nitems * 0x90, (const void *)a, 0x90); n->nitems++; }
            }
        }
        else if (cid == 0xe34009u && n->nskel < 8) {
            for (unsigned j = 0; j + 4 <= sz; j++)
                if (!memcmp(d + j, "\x7f" "ELF", 4)) {
                    Skel *k = &n->skel[n->nskel];
                    char nm[64];
                    if (elf_take(d + j, sz - j, "__Skeleton:::", &k->data, &k->len, &k->off, nm, (int)sizeof nm)) {
                        if (k->off + 0x10 <= k->len) {
                            k->nb = (int)rd32(k->data + k->off + 8);
                            k->hash = nis_hash(nm + 13);
                            if (k->nb > 0 && k->nb <= 64 && k->off + 0x10 + (unsigned)k->nb * 0x70 <= k->len) { n->nskel++; break; }
                        }
                        free(k->data); memset(k, 0, sizeof *k);
                    }
                    break;
                }
        }
        else if (cid == 0xe34010u && n->nbank < 8) {
            for (unsigned j = 0; j + 4 <= sz; j++)
                if (!memcmp(d + j, "\x7f" "ELF", 4)) {
                    if (bank_from_elf(d + j, sz - j, &n->bank[n->nbank])) n->nbank++;
                    break;
                }
        }
        q += 8 + sz;
    }
}

Nis *nis_load(const char *dataroot, unsigned hash, int variant)
{
    char name[32] = "";
    for (int i = 1; i <= 40 && !name[0]; i++) {
        char t[32]; snprintf(t, sizeof t, "IntroNis%02d", i);
        if (nis_hash(t) == hash) snprintf(name, sizeof name, "%s", t);
    }
    /* the career start scene (ChooseAnim 800464ac type 4, IsFirstTimeInCareer):
       NIS/Scene_CareerNis1_BundleB.bun, header type 4, 405 frames */
    if (!name[0] && nis_hash("CareerNis1") == hash) snprintf(name, sizeof name, "CareerNis1");
    /* the dyno's intro / outro (CAnimChooser event 0x1262): NIS/Scene_DynoEnter|DynoExit_BundleB.bun */
    if (!name[0] && nis_hash("DynoEnter") == hash) snprintf(name, sizeof name, "DynoEnter");
    if (!name[0] && nis_hash("DynoExit") == hash) snprintf(name, sizeof name, "DynoExit");
    if (!name[0]) return NULL;
    char p[1024];
    snprintf(p, sizeof p, "%s/NIS/Scene_%s_BundleB.bun", dataroot, name);
    long len = 0;
    unsigned char *b = read_file(p, &len);
    if (!b) { printf("nis: %s missing\n", p); return NULL; }
    Nis *n = (Nis *)calloc(1, sizeof *n);
    if (!n) { free(b); return NULL; }
    snprintf(n->name, sizeof n->name, "%s", name);
    snprintf(n->path, sizeof n->path, "%s", p);
    n->hash = hash; n->variant = variant;
    const unsigned char *hdr = NULL;
    walk_bundle(n, b, (unsigned)len, &hdr);
    if (!hdr) { printf("nis: %s has no scene header\n", name); free(b); nis_free(n); return NULL; }
    n->type = (int)rd32(hdr + 0x18);
    n->len = (float)(int)rd32(hdr + 0x28);
    n->cd_end = (float)(int)rd32(hdr + 0x2c);
    n->cd_start = (float)(int)rd32(hdr + 0x30);
    free(b);
    /* 0x43acf0: a car path per start position K (ZPM_CarK_t / _q on the
       ROOT skeleton; ZPM_CarK_s is never bound), rate 0.5, held at the
       end; a slot with neither animation drives no car */
    int ncars = 0;
    for (int k = 0; k < 8; k++) {
        Slot *s = &n->slot[k];
        char nt[24], nq[24];
        snprintf(nt, sizeof nt, "ZPM_Car%d_t", k + 1);
        snprintf(nq, sizeof nq, "ZPM_Car%d_q", k + 1);
        float e = 0;
        int t = container_bind(n, nt, s->sub[0], &s->nsub[0], &e);
        if (t) s->end = e;
        int q = container_bind(n, nq, s->sub[1], &s->nsub[1], &e);
        if (q) s->end = e;                     /* the last bound sets +0x64 (0x434a80) */
        s->on = t || q;
        ncars += s->on;
    }
    chars_make(n);                             /* 0x435fc0: the scene items */
    ev_load(n, dataroot);                      /* 0x43b2cb -> 0x432c00 */
    /* the scene camera: InGameRace 0x3b210 {hash, tracks}, "Track <variant>" */
    char tn[16]; snprintf(tn, sizeof tn, "Track %d", variant);
    for (unsigned q = 0; q + 16 <= T.cams_len && !n->track;) {
        unsigned sz = rd32(T.cams + q + 4);
        const unsigned char *d = T.cams + q + 8;
        if (q + 8 + sz > T.cams_len) break;
        if (rd32(d) == hash) {
            const unsigned char *t = d + 8, *e = d + sz;
            for (unsigned i = 0; i < rd32(d + 4) && t + 0x20 <= e; i++) {
                int nk = (signed char)t[0x10];
                if (!strncmp((const char *)t + 0x12, tn, 14)) { n->track = t; break; }
                t += 0x20 + (nk > 0 ? nk : 0) * 0x88;
            }
        }
        q += 8 + sz;
    }
    printf("nis: %s v%d (%s), %d car paths, %d characters of %d items (%d skeletons), %d frames, "
           "countdown frames %d..%d, camera %s (%d keys), %d track events\n",
           name, variant, n->type == 0 ? "race intro" : "other", ncars, n->nch, n->nitems, n->nskel,
           (int)n->len, (int)n->cd_start, (int)n->cd_end, n->track ? tn : "MISSING",
           n->track ? (signed char)n->track[0x10] : 0, n->nev);
    return n;
}

void nis_free(Nis *n)
{
    if (!n) return;
    chars_free(n);
    for (int i = 0; i < n->nbank; i++) free(n->bank[i].data);
    for (int i = 0; i < n->nskel; i++) free(n->skel[i].data);
    free(n->items);
    free(n->ev);
    free(n);
}

/* ---- cars (0x434b00 / 0x431400 / 0x4309d0) ---------------------------------- */
static void slot_place(Nis *n, int k, float dt, NisCar *out)
{
    Slot *s = &n->slot[k];
    /* 0x434b00: frame += 30 x dt x 0.5, held at the end */
    float f = s->frame + 30.0f * dt * 0.5f;
    s->frame = f > s->end ? s->end : f;
    /* 0x431400: the pose from the ROOT skeleton's rest (0x6e3c70: scale 1,
       x-scale 1, identity rotation, no offset; GLOBALB 0xe34009 "ROOT" is
       ROOT (no parent) + ROOT0 (child of ROOT), both at rest), then the
       q (0x7ea234) and t (0x7ea230) channels */
    for (int b = 0; b < 2; b++) {
        float *p = s->pose + 12 * b;
        memset(p, 0, 12 * sizeof *p);
        p[0] = p[1] = p[2] = p[3] = 1.0f; p[7] = 1.0f; p[11] = 1.0f;
    }
    for (int i = 0; i < s->nsub[1]; i++) codec_eval(&s->sub[1][i], s->frame, s->pose);
    for (int i = 0; i < s->nsub[0]; i++) codec_eval(&s->sub[0][i], s->frame, s->pose);
    /* 0x6e2d60 bone 1 (its parent, bone 0, is the identity at rest) */
    const float *p = s->pose + 12;
    float x = p[4], y = p[5], z = p[6], w = p[7];
    float y2 = y + y, z2 = z + z;
    float m0 = (1.0f - (z2 * z + y2 * y)) * p[0] * p[3];   /* row 0: x axis */
    float m1 = (x * y2 + z2 * w) * p[0];
    float tx = p[8], ty = p[9];
    /* x inst+0x60 (rotation by the marker) x inst+0xa0 (the marker point) */
    float c = n->R[0], sn = n->R[1];
    out->on = 1;
    out->x = tx * c - ty * sn + n->T[0];
    out->y = tx * sn + ty * c + n->T[1];
    out->heading = ang16(m0 * c - m1 * sn, m0 * sn + m1 * c);
    out->count = s->count;
    s->count++;
}

void nis_start(Nis *n, const float marker[2], unsigned short marker_ang,
               NisGroundFn ground, void *gu, NisCar cars[8])
{
    n->ground = ground; n->gu = gu;
    /* 0x431990: the origin on the ground under the marker (reference 0),
       turned by the marker's angle (0x42fc60 +0x4000, then -0x4000) */
    float o[3] = { marker[0], marker[1], 0 };
    int found = 0;
    float gz = ground ? ground(gu, NULL, o, &found) : 0;
    o[2] = found ? gz : 0;
    float a = u16rad(marker_ang);
    n->R[0] = cosf(a); n->R[1] = sinf(a); n->R[2] = -n->R[1]; n->R[3] = n->R[0];
    memcpy(n->T, o, sizeof o);
    /* 0x4427c0: the ground at the scene origin, for the camera's keys */
    n->zref = o[2];
    if (ground) { int f2 = 0; float z2 = ground(gu, NULL, o, &f2); if (f2) n->zref = z2; }
    chars_start(n);                                                      /* 0x435fc0 -> 0x437be0 */
    /* 0x437910 */
    n->time = 0;
    n->playing = 1;
    n->t_len = n->len * (1.0f / 30.0f);
    n->t_cd = n->cd_start * (1.0f / 30.0f);
    n->interval = (n->cd_end / 30.0f - n->t_cd) * 0.333333f;            /* 0x786348 */
    n->cars_on = 1; n->handed = 0;
    memset(&n->key, 0, sizeof n->key);
    n->ev_t = 0; n->ev_i = 0; n->fired0 = n->fired1 = 0;
    memset(n->fx, 0, sizeof n->fx);
    /* 0x43b000: every slot's frame and count to 0, then 0x439240(0) */
    for (int k = 0; k < 8; k++) {
        n->slot[k].frame = 0; n->slot[k].count = 0;
        cars[k].on = 0;
        if (n->slot[k].on) slot_place(n, k, 0, &cars[k]);
    }
}

int nis_tick(Nis *n, float dt, NisCar cars[8])
{
    int r = 0;
    for (int k = 0; k < 8; k++) cars[k].on = 0;
    n->time += dt;
    /* 0x439240: past StartCountdown, the hand-off (0x437a00) */
    if (n->cars_on) {
        if (n->t_cd < n->time) { n->cars_on = 0; n->handed = 1; r |= NIS_TICK_HANDOFF; }
        else for (int k = 0; k < 8; k++) if (n->slot[k].on) slot_place(n, k, dt, &cars[k]);
    }
    for (int i = 0; i < n->nch; i++) char_update(n, &n->ch[i], dt);     /* items vt+0x18 */
    ev_tick(n, dt);                                                      /* +0x124 */
    if (n->type == 0 && n->playing && (n->t_cd <= n->time || n->t_len < n->time)) {
        n->playing = 0;
        r |= NIS_TICK_CHASECAM;
    }
    return r;
}

/* 0x43b460 -> 0x43af80(StartCountdown time): the scene jumps to the
   countdown. Every character starts its animation over and runs one update
   of that length; every slot starts over (anim frame, place, count 0), then
   0x439240(StartCountdown) -- as a tick of that length, before the time
   is set -- places the cars there (or hands them off when the scene was
   already past it). */
int nis_skip(Nis *n, NisCar cars[8])
{
    int r = 0;
    for (int k = 0; k < 8; k++) cars[k].on = 0;
    for (int i = 0; i < n->nch; i++) {             /* items vt+0x14 (0x431260): frame 0, then one update */
        n->ch[i].frame = 0;
        char_update(n, &n->ch[i], n->t_cd);
    }
    /* the event track is ticked by the StartCountdown time: everything up
       to (clock + that) fires at once */
    ev_tick(n, n->t_cd);
    for (int k = 0; k < 8; k++) { n->slot[k].frame = 0; n->slot[k].count = 0; }
    if (n->cars_on) {
        if (n->t_cd < n->time) { n->cars_on = 0; n->handed = 1; r |= NIS_TICK_HANDOFF; }
        else for (int k = 0; k < 8; k++) if (n->slot[k].on) slot_place(n, k, n->t_cd, &cars[k]);
    }
    n->time = n->t_cd;
    return r;
}

/* ---- the scene camera (ICE mover) ----------------------------------------- */
static float key_param(const unsigned char *trk, int i)                 /* 0x442d00 */
{
    int nk = (signed char)trk[0x10];
    if (i < 0) return 0;
    if (i < nk) return rdf(trk + 0x20 + i * 0x88 + 0xc);
    return 1.0f;
}
static const unsigned char *key_at(const unsigned char *trk, int i)    /* 0x4418e0 */
{
    int nk = (signed char)trk[0x10];
    return i >= 0 && i < nk ? trk + 0x20 + i * 0x88 : NULL;
}
/* 0x447a30 / 0x447a80: the key's eye / look point (which 0 start, 1 end);
   scene space (3) to the world with the ground offset (0x4427c0) */
static void key_point(const Nis *n, const unsigned char *k, int look, int which, float o[3])
{
    const unsigned char *p = k + (look ? 0x30 : 0x18) + 12 * which;
    float l[3] = { rdf(p), rdf(p + 4), rdf(p + 8) };
    if (k[look ? 5 : 4] != 3) { memcpy(o, l, sizeof l); return; }
    o[0] = l[0] * n->R[0] + l[1] * n->R[2] + n->T[0];
    o[1] = l[0] * n->R[1] + l[1] * n->R[3] + n->T[1];
    o[2] = l[2] + n->T[2];
    if (n->ground) {                                                     /* 0x442700 */
        int found = 0;
        float z = n->ground(n->gu, NULL, o, &found);
        if (found) o[2] += z - n->zref;
    }
}
/* 0x44a160 -> 0x4434f0: does key a's side `as` run on into key b's side
   `bs` (same tangent length, lens, type, spaces and points) */
static int keys_continue(const unsigned char *a, int as, const unsigned char *b, int bs)
{
    if (rdf(a + 0x10 + 4 * as) != rdf(b + 0x10 + 4 * bs)) return 0;
    if (rdf(a + 0x48 + 4 * as) != rdf(b + 0x48 + 4 * bs)) return 0;
    if (a[0] != b[0] || a[4] != b[4] || a[5] != b[5]) return 0;
    for (int i = 0; i < 3; i++) {
        if (fabsf(rdf(a + 0x18 + 12 * as + 4 * i) - rdf(b + 0x18 + 12 * bs + 4 * i)) > 1e-5f) return 0;
        if (fabsf(rdf(a + 0x30 + 12 * as + 4 * i) - rdf(b + 0x30 + 12 * bs + 4 * i)) > 1e-5f) return 0;
    }
    return 1;
}
/* 0x450040: the eye, look and lens tangents at the key's start (0) or end
   (1): the key's own change x its TANGENT_LEN, or, running on from the
   neighbouring key, a blend of both keys' changes weighted by their
   durations */
static void key_tangents(const Nis *n, int ki, int which, float te[3], float tl[3], float *tlens)
{
    const unsigned char *k = key_at(n->track, ki);
    if (!k[0]) {                                   /* no camera type: no tangents */
        memset(te, 0, 12); memset(tl, 0, 12); *tlens = 0;
        return;
    }
    float TL = rdf(k + 0x10 + 4 * which);
    float e0[3], e1[3], l0[3], l1[3];
    key_point(n, k, 0, 0, e0); key_point(n, k, 0, 1, e1);
    key_point(n, k, 1, 0, l0); key_point(n, k, 1, 1, l1);
    float de[3], dl[3], dlens = rdf(k + 0x4c) - rdf(k + 0x48);
    for (int i = 0; i < 3; i++) { de[i] = e1[i] - e0[i]; dl[i] = l1[i] - l0[i]; }
    const unsigned char *nb = key_at(n->track, ki + (which ? 1 : -1));   /* 0x442ee0 */
    if (nb && nb[0] && keys_continue(k, which, nb, which ^ 1)) {
        float ne0[3], ne1[3], nl0[3], nl1[3];
        key_point(n, nb, 0, 0, ne0); key_point(n, nb, 0, 1, ne1);
        key_point(n, nb, 1, 0, nl0); key_point(n, nb, 1, 1, nl1);
        int ni = ki + (which ? 1 : -1);
        float ds = key_param(n->track, ki + 1) - key_param(n->track, ki);     /* 0x442d70 */
        float dn = key_param(n->track, ni + 1) - key_param(n->track, ni);
        float a = ds / (dn + ds), b = 1.0f - a;
        if (dn > 1e-6f) a = a / dn * ds;
        float f1 = TL * a, f2 = TL * b;
        for (int i = 0; i < 3; i++) {
            te[i] = (ne1[i] - ne0[i]) * f1 + de[i] * f2;
            tl[i] = (nl1[i] - nl0[i]) * f1 + dl[i] * f2;
        }
        *tlens = f1 * (rdf(nb + 0x4c) - rdf(nb + 0x48)) + f2 * dlens;
        return;
    }
    for (int i = 0; i < 3; i++) { te[i] = de[i] * TL; tl[i] = dl[i] * TL; }
    *tlens = dlens * TL;
}
static float lens_angle(float lens)                                      /* fov u16 of a lens */
{
    return (float)((ang16(lens, 1.33f * 12.0f) & 0x7fff) << 1);         /* 0x7870a8 x 0x7870ac */
}
static float lens_tangent(float lens, float t)                           /* 0x443d80 */
{
    short a = (short)(ang16(lens, 1.33f * 12.0f) << 1);
    short b = (short)(ang16(t * 0.01f + lens, 1.33f * 12.0f) << 1);
    return (float)((int)b - (int)a) * 100.0f;
}
static float hermite(float p0, float t0, float p1, float t1, float u)  /* 0x574830 / 0x574870 */
{
    float D = p1 - p0;
    float a = (t0 + t1) - (D + D), b = (D * 3.0f - t1) - (t0 + t0);
    return ((u * a + b) * u + t0) * u + p0;
}
static void key_setup(Nis *n, int ki)                                    /* 0x451b70 */
{
    IceKey *K = &n->key;
    const unsigned char *k = key_at(n->track, ki);
    K->k = k; K->idx = ki;
    float te0[3], tl0[3], te1[3], tl1[3], tn0, tn1;
    key_tangents(n, ki, 0, te0, tl0, &tn0);
    key_tangents(n, ki, 1, te1, tl1, &tn1);
    float e0[3], e1[3], l0[3], l1[3];
    key_point(n, k, 0, 0, e0); key_point(n, k, 0, 1, e1);
    key_point(n, k, 1, 0, l0); key_point(n, k, 1, 1, l1);
    for (int i = 0; i < 3; i++) {
        K->p0[0][i] = e0[i]; K->t0[0][i] = te0[i]; K->p1[0][i] = e1[i]; K->t1[0][i] = te1[i];
        K->p0[1][i] = l0[i]; K->t0[1][i] = tl0[i]; K->p1[1][i] = l1[i]; K->t1[1][i] = tl1[i];
    }
    float ls = rdf(k + 0x48), le = rdf(k + 0x4c);
    K->fov[0] = lens_angle(ls); K->fov[1] = lens_tangent(ls, tn0);
    K->fov[2] = lens_angle(le); K->fov[3] = lens_tangent(le, tn1);
    /* smooth entry (+1) and the car-space lag (+9) are 0 in every scene
       camera of InGameRace.bun, the hand-over 0x444980 never runs */
    K->k0 = key_param(n->track, ki);
    K->k1 = key_param(n->track, ki + 1);
}

/* 0x443df0: the f-stop of an aperture value, 2^(i/6) for i = ftol(ap)
   (the truncating _ftol2) clamped to 0..36 */
static float aperture_fstop(float ap)
{
    int i = (int)ap;
    if (i < 1) return 1.0f;
    if (i > 36) i = 36;
    return powf(2.0f, (float)i / 6.0f);
}

int nis_camera(Nis *n, double now, const float car_com[3], const float car_rot[9],
               CcOut *out, float *near_clip, float *letterbox, float dof[2])
{
    if (!n || !n->track || n->t_cd <= 0) return 0;
    /* 0x44fe80: param = time / StartCountdown, only inside [0, 1] */
    float p = n->time / n->t_cd;
    if (!(p >= 0.0f && p <= 1.0f)) return 0;
    int nk = (signed char)n->track[0x10], ki = nk - 1;                  /* 0x442ae0 */
    while (ki > 0 && !(key_param(n->track, ki) <= p)) ki--;
    if (ki < 0 || !key_at(n->track, ki)) return 0;
    if (n->key.k != key_at(n->track, ki)) key_setup(n, ki);
    IceKey *K = &n->key;
    const unsigned char *k = K->k;
    float u = (p - K->k0) / (K->k1 - K->k0);
    /* near clip (+0x50/+0x54, no tangents) */
    if (near_clip) *near_clip = hermite(rdf(k + 0x50), 0, rdf(k + 0x54), 0, u);
    /* 0x4462a0 fov: the spline when the eye is cubic, else a straight blend */
    unsigned short fov = k[2] ? (unsigned short)(int)hermite(K->fov[0], K->fov[1], K->fov[2], K->fov[3], u)
                              : (unsigned short)(int)(K->fov[2] * u + (1.0f - u) * K->fov[0]);
    /* 0x446180 / 0x446210 eye and look */
    float eye[3], look[3];
    for (int i = 0; i < 3; i++) {
        eye[i] = k[2] ? hermite(K->p0[0][i], K->t0[0][i], K->p1[0][i], K->t1[0][i], u)
                      : K->p0[0][i] * (1.0f - u) + K->p1[0][i] * u;
        look[i] = k[3] ? hermite(K->p0[1][i], K->t0[1][i], K->p1[1][i], K->t1[1][i], u)
                       : K->p0[1][i] * (1.0f - u) + K->p1[1][i] * u;
    }
    /* spaces: 0 car (0x401340: the car's rows and centre), 2 the car's
       position added; scene space was applied with the key */
    for (int w = 0; w < 2; w++) {
        float *v = w ? look : eye;
        int sp = k[4 + w];
        if (sp == 0) {
            float q[3] = { v[0], v[1], v[2] };
            for (int i = 0; i < 3; i++)
                v[i] = q[0] * car_rot[i] + q[1] * car_rot[3 + i] + q[2] * car_rot[6 + i] + car_com[i];
        } else if (sp == 2) for (int i = 0; i < 3; i++) v[i] += car_com[i];
    }
    float V[4][4];
    chasecam_lookat(eye, look, V);                                         /* 0x5ba9a0 */
    /* noise: magnitude x 100 on the camera's pitch and yaw channels at
       0.618 and 0.382 x frequency x 1000 (camera +0x78/+0x7c, +0x88/+0x8c),
       through the shake 0x441340 at the game clock */
    float mag = hermite(rdf(k + 0x58), 0, rdf(k + 0x5c), 0, u) * 100.0f;
    float frq = hermite(rdf(k + 0x60), 0, rdf(k + 0x64), 0, u);
    float t = (float)now;
    float nz[4] = { 0, 0, chasecam_noise(0.618f * frq * 1000.0f * t) * mag,
                          chasecam_noise(frq * 1000.0f * 0.382f * t) * mag };
    chasecam_shake_view(V, nz, fov);
    /* constraints to the world (+7) and to cars (+6) are 0 in every scene
       camera of InGameRace.bun */
    if (letterbox) *letterbox = hermite((float)k[0x72], 0, (float)k[0x73], 0, u) * 0.125f * 0.01f;
    /* depth of field (camera +0xb4 centre / +0xb8 range, metres): off from
       aperture 37 up. Focal length 0x443d50 = 1.33 x 12 / tan(fov/2) mm (the
       key's lens back), f-stop 0x443df0, circle of confusion 0.03 mm, focus
       distance (+0x68/+0x6c) >= 0.01 m; near and far limits of acceptable
       focus from the hyperfocal distance, far 100 km beyond it. */
    if (dof) {
        dof[0] = dof[1] = 0.0f;
        float ap = hermite((float)k[0x70], 0, (float)k[0x71], 0, u);
        if (ap < 37.0f) {
            float fd = hermite(rdf(k + 0x68), 0, rdf(k + 0x6c), 0, u);
            if (fd < 0.01f) fd = 0.01f;
            float F1 = fd * 1000.0f;
            float half = (float)((fov >> 1) & 0x7fff) * (6.2831853f / 65536.0f);
            float f = 1.33f / (sinf(half) / cosf(half)) * 12.0f;
            float H = f * f / (aperture_fstop(ap) * 0.03f) + f;
            float a = (H - f) * F1;
            float nearm = a / ((F1 + H) - (f + f)) * 0.001f;
            float farm = 100000.0f;
            if (H - F1 > 1e-5f) farm = a / (H - F1) * 0.001f;
            dof[0] = (farm + nearm) * 0.5f;
            dof[1] = farm - nearm;
        }
    }
    chasecam_out(V, fov, out);
    return 1;
}
