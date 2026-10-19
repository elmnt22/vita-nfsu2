/* partanim.c -- the opening car parts (see partanim.h). */
#include "partanim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return rd16(p) | rd16(p + 2) << 16; }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

#define PA_STYLES 7                      /* hood 6, trunk 1, doors 7 */
typedef struct {
    int end;                             /* the container's frames (anim +0x64) */
    int nq, nt;                          /* keyframes of each channel, 0 = static */
    float *q, *t;                        /* x y z w / x y z per frame */
    int ok;
} Anim;
typedef struct {
    int state;                           /* 0 open 1 closed 2 opening 3 closing */
    int style, flag, door_style;
    float speed, start;
} PartState;
struct PartAnims {
    Anim a[PA_PARTS][PA_STYLES];
    float rest[21][16];                  /* chunk 0x37100 */
    int nrest;
    PartState st[PA_PARTS];
};

static const char *const k_hood[6] = { "HOOD_REG", "HOOD_FRONT", "LEFT_HOOD_SPLIT", "RIGHT_HOOD_SPLIT",
                                       "FRONT_HOOD_SPLIT", "BACK_HOOD_SPLIT" };
static const char *const k_door[7] = { "DOOR_REG", "DOOR_SCISSOR", "DOOR_SUICIDE", "DOOR_WIDE1",
                                       "DOOR_WIDE2", "DOOR_WIDE3", "DOOR_WIDE4" };

/* "<CAR>_ZAM_<x>_q" -> the part and style it animates, -1 none */
static int anim_slot(const char *nm, int *part, int *style, int *isq) {
    const char *z = strstr(nm, "_ZAM_");
    size_t l = strlen(nm);
    if (!z || l < 3 || nm[l - 2] != '_' || (nm[l - 1] != 'q' && nm[l - 1] != 't')) return 0;
    *isq = nm[l - 1] == 'q';
    char key[48]; size_t kl = l - 2 - (size_t)(z + 5 - nm);
    if (kl >= sizeof key) return 0;
    memcpy(key, z + 5, kl); key[kl] = 0;
    for (int s = 0; s < 6; s++) if (!strcmp(key, k_hood[s])) { *part = PA_HOOD; *style = s; return 1; }
    if (!strcmp(key, "TRUNK_REG")) { *part = PA_TRUNK; *style = 0; return 1; }
    for (int side = 0; side < 2; side++)
        for (int s = 0; s < 7; s++) {
            char n[40]; snprintf(n, sizeof n, "%s_%s", side ? "RIGHT" : "LEFT", k_door[s]);
            if (!strcmp(key, n)) { *part = side ? PA_RIGHT_DOOR : PA_LEFT_DOOR; *style = s; return 1; }
        }
    return 0;
}

/* a type 15 container's first raw (type 0) channel */
static void take_channel(Anim *A, const unsigned char *D, unsigned len, const unsigned char *c, int isq) {
    if (rd16(c) != 15) return;
    A->end = (int)rd16(c + 0xa);
    if (rd16(c + 8) < 1) return;
    unsigned so = rd32(c + 0xc);
    if (so + 0x10 > len || rd16(D + so) != 0) return;
    unsigned nf = rd32(D + so + 4), nc = rd32(D + so + 8), fr = rd32(D + so + 0xc);
    unsigned data = so + 0x10 + 8 * nf;
    if (nc != (isq ? 4u : 3u) || fr < 1 || fr > 1000 || data + 4 * nc * fr > len) return;   /* static */
    float *v = (float *)malloc(sizeof(float) * nc * fr);
    if (!v) return;
    for (unsigned i = 0; i < nc * fr; i++) v[i] = rdf(D + data + 4 * i);
    if (isq) { free(A->q); A->q = v; A->nq = (int)fr; }
    else     { free(A->t); A->t = v; A->nt = (int)fr; }
}

/* one EAGL ELF object: its .data and the __AnimationBank symbol */
static void take_bank(PartAnims *pa, const unsigned char *E, unsigned n) {
    if (n < 0x34 || memcmp(E, "\x7f" "ELF", 4)) return;
    unsigned shoff = rd32(E + 0x20), shent = rd16(E + 0x2e), shnum = rd16(E + 0x30), shstr = rd16(E + 0x32);
    if (shoff + shnum * shent > n || shstr >= shnum) return;
    unsigned stroff = rd32(E + shoff + shstr * shent + 0x10);
    int dsec = -1, symtab = -1;
    for (unsigned i = 0; i < shnum; i++) {
        const unsigned char *s = E + shoff + i * shent;
        if (stroff + rd32(s) < n && !strcmp((const char *)E + stroff + rd32(s), ".data")) dsec = (int)i;
        if (rd32(s + 4) == 2) symtab = (int)i;
    }
    if (dsec < 0 || symtab < 0) return;
    const unsigned char *ds = E + shoff + dsec * shent, *sy = E + shoff + symtab * shent;
    const unsigned char *st = E + shoff + rd32(sy + 0x18) * shent;
    unsigned dof = rd32(ds + 0x10), dlen = rd32(ds + 0x14), bank = ~0u;
    if (dof + dlen > n) return;
    for (unsigned k = 0; k < rd32(sy + 0x14) / 16; k++) {
        const unsigned char *y = E + rd32(sy + 0x10) + k * 16;
        unsigned no = rd32(st + 0x10) + rd32(y);
        if (no < n && !strncmp((const char *)E + no, "__AnimationBank", 15)) { bank = rd32(y + 4); break; }
    }
    const unsigned char *D = E + dof;
    if (bank == ~0u || bank + 0x18 > dlen) return;
    unsigned cnt = rd32(D + bank + 4), pa_ = rd32(D + bank + 0x10), pn = rd32(D + bank + 0x14);
    for (unsigned i = 0; i < cnt; i++) {
        if (pn + 4 * i + 4 > dlen || pa_ + 4 * i + 4 > dlen) break;
        unsigned no = rd32(D + pn + 4 * i), ao = rd32(D + pa_ + 4 * i);
        if (no >= dlen || ao + 0x10 > dlen || !memchr(D + no, 0, dlen - no)) continue;
        int part, style, isq;
        if (!anim_slot((const char *)D + no, &part, &style, &isq)) continue;
        Anim *A = &pa->a[part][style];
        take_channel(A, D, dlen, D + ao, isq);
        A->ok = 1;
    }
}

PartAnims *partanim_load(const char *dataroot, const char *car) {
    char path[512];
    snprintf(path, sizeof path, "%s/CARS/%s/PARTS_ANIMATIONS.bin", dataroot, car);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return NULL; }
    fclose(f);
    PartAnims *pa = (PartAnims *)calloc(1, sizeof *pa);
    if (!pa) { free(d); return NULL; }
    for (long o = 0; o + 8 <= len; ) {
        unsigned id = rd32(d + o), sz = rd32(d + o + 4);
        long body = o + 8, end = body + (long)sz;
        if (end > len) break;
        if (id == 0xe34010u) {
            for (long j = body; j + 4 <= end; j++)
                if (!memcmp(d + j, "\x7f" "ELF", 4)) { take_bank(pa, d + j, (unsigned)(end - j)); break; }
        } else if (id == 0x37100u) {   /* padding to 16, then {count, hash, 8 bytes}, then the matrices */
            long r = (body + 15) & ~15L;
            while (r < end && rd32(d + r) == 0x11111111u) r += 4;
            for (int k = 0; k < 21 && r + 0x10 + 0x40 * (k + 1) <= end; k++) {
                for (int e = 0; e < 16; e++) pa->rest[k][e] = rdf(d + r + 0x10 + 0x40 * k + 4 * e);
                pa->nrest = k + 1;
            }
        }
        o = end;
    }
    free(d);
    for (int p = 0; p < PA_PARTS; p++) { pa->st[p].state = 1; pa->st[p].speed = 1.0f; }
    int n = 0;
    for (int p = 0; p < PA_PARTS; p++) for (int s = 0; s < PA_STYLES; s++) n += pa->a[p][s].ok;
    printf("partanim: %s: %d animations, %d rest matrices\n", car, n, pa->nrest);
    return pa;
}

void partanim_free(PartAnims *pa) {
    if (!pa) return;
    for (int p = 0; p < PA_PARTS; p++)
        for (int s = 0; s < PA_STYLES; s++) { free(pa->a[p][s].q); free(pa->a[p][s].t); }
    free(pa);
}

/* the anim a style (and split-hood half) plays: 0x434550 */
static int anim_style(int part, int style, int half) {
    if (part == PA_HOOD && (style == 2 || style == 3)) return half ? 3 : 2;
    if (part == PA_HOOD && (style == 4 || style == 5)) return half != 1 ? 5 : 4;
    return style;
}
static int is_split(int part, int style) { return part == PA_HOOD && style >= 2 && style <= 5; }
/* 0x433d10: the open / close time in seconds */
static float anim_dur(const PartAnims *pa, int part) {
    const PartState *S = &pa->st[part];
    const Anim *A = &pa->a[part][anim_style(part, S->style, 0)];
    float rate = is_split(part, S->style) ? S->speed * 0.75f : S->speed;
    return A->end > 0 && rate > 0 ? (float)A->end / 30.0f / rate : 0.0f;
}
/* 0x433d50 */
static void set_state(PartAnims *pa, int part, int want, float now) {
    PartState *S = &pa->st[part];
    if (S->state == want) return;
    switch (S->state) {
    case 0: if (want == 3) { S->state = 3; S->start = now; } break;
    case 1: if (want == 2) { S->state = 2; S->start = now; } break;
    case 2: case 3:
        if ((S->state == 2 && want == 0) || (S->state == 3 && want == 1)) { S->state = want; break; }
        if (want == 2 || want == 3) {   /* reversed half way: carry on from the same pose */
            float dur = anim_dur(pa, part);
            S->state = want;
            S->start = now - (dur - (now - S->start));
        }
        break;
    }
}

void partanim_request(PartAnims *pa, int part, int open, int style, int flag, float now) {   /* 0x433e20 */
    if (!pa || part < 0 || part >= PA_PARTS) return;
    PartState *S = &pa->st[part];
    if (open ? (S->state == 2 || S->state == 0) : (S->state == 3 || S->state == 1)) return;
    S->speed = 1.0f;
    S->flag = flag;
    if (part == PA_LEFT_DOOR || part == PA_RIGHT_DOOR) {   /* closing plays the style it opened with */
        if (open && S->state == 1) S->door_style = style;
        else if (!open && S->state == 0) style = S->door_style;
    }
    S->style = style;
    set_state(pa, part, open ? 2 : 3, now);
}

int partanim_state(const PartAnims *pa, int part) {
    return pa && part >= 0 && part < PA_PARTS ? pa->st[part].state : 1;
}

/* the rest matrix whose translation a static _t channel uses (0x433f40) */
static const float *rest_of(const PartAnims *pa, int part, int as) {
    int k = part == PA_HOOD ? as : part == PA_TRUNK ? 12
          : part == PA_LEFT_DOOR ? (as < 3 ? 6 + as : 13 + as - 3) : (as < 3 ? 9 + as : 17 + as - 3);
    return k < pa->nrest ? pa->rest[k] : NULL;
}
/* the part's matrix at a (fractional) frame: rotation(q) + pivot */
static void pose(const PartAnims *pa, int part, int as, float fr, float m[16]) {
    const Anim *A = &pa->a[part][as];
    float q[4] = { 0, 0, 0, 1 }, t[3] = { 0, 0, 0 };
    if (A->nq) {
        float f = fr < 0 ? 0 : fr > (float)(A->nq - 1) ? (float)(A->nq - 1) : fr;
        int i = (int)f; float u = f - (float)i;
        const float *a = A->q + 4 * i, *b = A->q + 4 * (i + 1 < A->nq ? i + 1 : i);
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], sg = dot < 0 ? -1.0f : 1.0f, l = 0;
        for (int c = 0; c < 4; c++) { q[c] = a[c] * (1 - u) + b[c] * u * sg; l += q[c] * q[c]; }
        l = l > 0 ? 1.0f / sqrtf(l) : 1.0f;
        for (int c = 0; c < 4; c++) q[c] *= l;
    }
    if (A->nt) {
        float f = fr < 0 ? 0 : fr > (float)(A->nt - 1) ? (float)(A->nt - 1) : fr;
        int i = (int)f; float u = f - (float)i;
        const float *a = A->t + 3 * i, *b = A->t + 3 * (i + 1 < A->nt ? i + 1 : i);
        for (int c = 0; c < 3; c++) t[c] = a[c] + (b[c] - a[c]) * u;
    }
    if (fabsf(t[0]) < 1e-6f && fabsf(t[1]) < 1e-6f && fabsf(t[2]) < 1e-6f) {   /* 0x434190: the rest pivot */
        const float *r = rest_of(pa, part, as);
        if (r) { t[0] = r[12]; t[1] = r[13]; t[2] = r[14]; }
    }
    /* 0x6e2d60: row vectors */
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w);     m[2] = 2 * (x * z - y * w);     m[3] = 0;
    m[4] = 2 * (x * y - z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z + x * w);     m[7] = 0;
    m[8] = 2 * (x * z + y * w);     m[9] = 2 * (y * z - x * w);     m[10] = 1 - 2 * (x * x + y * y); m[11] = 0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
}
static void m4mul(const float *a, const float *b, float *o) {   /* o = a * b, row vectors */
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(o, r, sizeof r);
}
static void rigid_inverse(const float *m, float *o) {   /* rotation transposed, translation back */
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i * 4 + j] = m[j * 4 + i];
    o[3] = o[7] = o[11] = 0; o[15] = 1;
    for (int j = 0; j < 3; j++) o[12 + j] = -(m[12] * o[j] + m[13] * o[4 + j] + m[14] * o[8 + j]);
}

int partanim_matrix(PartAnims *pa, int part, int half, float now, float out[16]) {   /* 0x434190 */
    if (!pa || part < 0 || part >= PA_PARTS) return 0;
    PartState *S = &pa->st[part];
    if (S->state == 1) return 0;
    int as = anim_style(part, S->style, half);
    const Anim *A = &pa->a[part][as];
    if (!A->ok || A->end <= 0) return 0;
    float dur = anim_dur(pa, part), el = now - S->start, tt;
    switch (S->state) {
    case 0: tt = dur; break;
    case 2: tt = el < dur ? el : dur; if (el >= dur) S->state = 0; break;
    default: tt = dur - (el < dur ? el : dur); if (el >= dur) S->state = 1; break;
    }
    float rate = S->speed;
    if (is_split(part, S->style)) rate = S->flag ? S->speed * (half ? 1.25f : 0.75f) : 1.0f;
    float fr = 30.0f * rate * (tt < 0 ? 0 : tt);
    float m0[16], m1[16], inv[16];
    pose(pa, part, as, 0.0f, m0);
    pose(pa, part, as, fr, m1);
    rigid_inverse(m0, inv);
    m4mul(inv, m1, out);
    return 1;
}
