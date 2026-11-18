/* xpconv.c -- translates a Most Wanted (2005) world stream into Underground 2's
   own format, so OpenUG2's world loader / tile converter read it as a UG2 track.

   usage: xpconv mw STREAML2RA.BUN OUT.BUN [--stats]

   Stage 1 (11-17): solids (0x80134010 objects) + scenery infos/instances
   (0x34102 / 0x34103) are rewritten; every other chunk is copied as it is.
   Format notes: E:/re-tools/out/EXPANSION_PLAN.md "Approach decided at X0".

   Solid object 0x80134010 children:
     0x134011 header: MW = UG2 up to +0x94; MW floats +0x98/+0x9c, name follows
              -> UG2 floats +0x94/+0x98, two zero words, name at +0xa4
     0x80134100 mesh: 0x134900 descriptor, N x 0x134b01 vertex streams (one per
              effect run), 0x134b02 MW shading groups (0x68 B), 0x134b03 indices
              (per group, relative to its stream), 0x134c02 group names
              -> one 0x134900 (UG2 layout, 36 B vertices flagged 0x4180), ONE
                 0x134b01 (36 B pos/normal/colour/uv), 0x134b02 UG2 records
                 (60 B), 0x134b03 rebased indices.

   Collision (11-17): each section's MW CARP pack (0x3b801) -> a UG2 collision
   pack (0x80034130, the layout wcoll.c reads) plus an origin sub-chunk
   0x341f0 (polygon xy relative to it: Rockport runs past UG2's +-4096 m).

   MW CARP pack (0x3b801), after 0x11 filler:
     +0 u32 size, +4 u32 section, 'CARP'/'Arti'/'Name' blocks, then from +0x40
     16-byte entries {u16 index, "ac" article | "ic" instance list, u32
     type | size << 8, u32 count, u32 offset (from the entry)}.
   Article: u16 strips, u16 strip bytes, u16 walls, u16 wall bytes, u8 0,
     u8 surfaces, u16 surface bytes, u16 index, pad; strips (16 B: float
     base xyz, u16 flags, u16 vertex offset from the strip table), packed
     vertices (s16 xyz / 128 from the base, u16: vertex 0 low byte = count,
     vertex 1 = winding flag, vertex 2+ low byte = surface of the triangle
     ending there), walls (32 B: float x ylo z, u32 surface index in the low
     byte; float x yhi z, float), surfaces (u32 VLT keys: "asphalt", ...).
   Instance (64 B): +0 row 0, +0x18 u16 group, +0x1a u16 article, +0x20 row 2,
     +0x30 -position. Never rotated in Rockport (all rows identity).
   MW physics space: (x, y up, z) = (-world y, world z, world x). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "xpconv.h"

static int g_oom;
typedef struct { uint8_t *p; size_t n, cap; long long base; } Buf;   /* base: file offset of p[0] */
static void need(Buf *b, size_t k) {
    if (b->n + k <= b->cap) return;
    size_t c = b->cap ? b->cap : 1 << 20;
    while (c < b->n + k) c *= 2;
    b->p = realloc(b->p, c); if (!b->p) { fprintf(stderr, "out of memory\n"); exit(1); }
    b->cap = c;
}
static void put(Buf *b, const void *d, size_t k) { need(b, k); if (g_oom) return; memcpy(b->p + b->n, d, k); b->n += k; }
static void put32(Buf *b, uint32_t v) { put(b, &v, 4); }
static void putz(Buf *b, size_t k) { need(b, k); if (g_oom) return; memset(b->p + b->n, 0, k); b->n += k; }
static void putfill(Buf *b, size_t k) { need(b, k); if (g_oom) return; memset(b->p + b->n, 0x11, k); b->n += k; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
/* opens a chunk; returns where its size word is */
static size_t open_chunk(Buf *b, uint32_t id) { put32(b, id); size_t at = b->n; put32(b, 0); return at; }
static void close_chunk(Buf *b, size_t at) { if (g_oom) return; uint32_t s = (uint32_t)(b->n - at - 4); memcpy(b->p + at, &s, 4); }
/* 0x11 filler so the data after it starts at an absolute multiple of a (EA's
   AlignReader); the reader skips it in 4-byte steps */
static void align_fill(Buf *b, size_t a) { size_t k = (size_t)((a - (unsigned long long)(b->base + b->n) % a) % a); putfill(b, k); }
static size_t skip_fill(const uint8_t *p, size_t n) { size_t i = 0; while (i < n && p[i] == 0x11) i++; return i & ~(size_t)3; }

static XpStats st;

/* ---- one MW mesh (0x80134100) -> UG2 ---- */
#define MAXG 256
#define MAXS 16
/* the night relight (X4): MW's daytime vertex colours x these (B, G, R): a cool,
   dim night like Bayview's prelight; 0 for the sky solids */
static int g_night, g_relight;
/* shading group record: MW 0x68 B (+0x3c vertices, +0x40 triangles); Carbon 0x90 B
   (+0x40 vertices, +0x60 triangles, +0x64 index start, +0x7c index count). Both:
   +0 bmin, +0xc bmax, +0x18 texture slots, +0x30 effect (a new vertex stream at each
   change; Carbon strides 36/44/48/52 B, the first 36 B always pos/normal/colour/uv) */
static size_t g_grec = 0x68, g_gvo = 0x3c, g_gto = 0x40;
static const float k_night[3] = { 0.50f, 0.40f, 0.36f };
static void conv_mesh(Buf *o, const uint8_t *p, size_t n)
{
    const uint8_t *vb[MAXS]; size_t vbn[MAXS]; int nvb = 0;
    const uint8_t *grp = NULL; size_t ngrp = 0;
    const uint8_t *idx = NULL; size_t nidx = 0;
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(p + q), sz = rd32(p + q + 4);
        const uint8_t *d = p + q + 8;
        if (q + 8 + sz > n) { st.bad++; return; }
        if (id == 0x134b01 && nvb < MAXS) { size_t f = skip_fill(d, sz); vb[nvb] = d + f; vbn[nvb] = sz - f; nvb++; }
        else if (id == 0x134b02) { size_t f = skip_fill(d, sz); grp = d + f; ngrp = (sz - f) / g_grec; }
        else if (id == 0x134b03) { size_t f = skip_fill(d, sz); idx = d + f; nidx = (sz - f) / 2; }
        q += 8 + sz;
    }
    if (!grp || !ngrp || ngrp > MAXG || !nvb) { st.bad++; return; }
    if (nvb > 1) st.streams_multi++;
    /* groups -> streams: a new stream at each change of EffectId (+0x30) */
    int gs[MAXG]; uint32_t gv[MAXG], gt[MAXG], eff[MAXG];
    uint32_t svert[MAXS] = {0};
    int s = 0;
    for (size_t g = 0; g < ngrp; g++) {
        const uint8_t *r = grp + g * g_grec;
        eff[g] = rd32(r + 0x30);
        if (g > 0 && eff[g] != eff[g - 1]) s++;
        if (s >= nvb) { st.bad++; return; }
        gs[g] = s; gv[g] = rd32(r + g_gvo); gt[g] = rd32(r + g_gto);
        svert[s] += gv[g];
        if (eff[g] < 32) st.effects[eff[g]]++;
    }
    if (s + 1 != nvb) { st.bad++; return; }
    /* stream strides and the merged vertex base of each stream */
    uint32_t stride[MAXS], base[MAXS], total = 0;
    for (int k = 0; k < nvb; k++) {
        if (!svert[k]) { st.bad++; return; }
        stride[k] = (uint32_t)(vbn[k] / svert[k]);
        if (stride[k] < 36) { st.bad++; return; }
        base[k] = total; total += svert[k];
    }
    if (total > 65535) { st.bad++; return; }
    size_t tot_idx = 0; for (size_t g = 0; g < ngrp; g++) tot_idx += gt[g] * 3;
    if (tot_idx > nidx) { st.bad++; return; }

    size_t mesh = open_chunk(o, 0x80134100);
    /* descriptor (UG2): +8 version 0x10, +0xc flags 0x4180 (36 B vertices), +0x10 groups,
       +0x24 tris, +0x34 vertices */
    {   size_t c = open_chunk(o, 0x134900);
        uint8_t d[68]; memset(d, 0, sizeof d);
        uint32_t v;
        v = 0x10; memcpy(d + 8, &v, 4); v = 0x4180; memcpy(d + 0xc, &v, 4);
        v = (uint32_t)ngrp; memcpy(d + 0x10, &v, 4);
        v = (uint32_t)(tot_idx / 3); memcpy(d + 0x24, &v, 4);
        v = total; memcpy(d + 0x34, &v, 4);
        put(o, d, sizeof d); close_chunk(o, c); }
    /* vertices: pos, normal, colour, uv (the first 36 bytes of every MW layout) */
    {   size_t c = open_chunk(o, 0x134b01); align_fill(o, 0x80);
        for (int k = 0; k < nvb; k++)
            for (uint32_t i = 0; i < svert[k]; i++) {
                uint8_t vx[36]; memcpy(vx, vb[k] + (size_t)i * stride[k], 36);
                if (g_night)   /* Rockport is daytime: its prelight down to a night level (B, G, R, A) */
                    for (int ch = 0; ch < 3; ch++) vx[24 + ch] = (uint8_t)(vx[24 + ch] * k_night[ch] + 0.5f);
                put(o, vx, 36);
            }
        close_chunk(o, c); }
    /* UG2 records: +0 bmin, +12 index count, +16 bmax, +28 texture slot, +32 matid,
       +52 index start, +56 flags */
    {   size_t c = open_chunk(o, 0x134b02); align_fill(o, 0x10);
        uint32_t start = 0;
        for (size_t g = 0; g < ngrp; g++) {
            const uint8_t *r = grp + g * g_grec;
            uint8_t u[60]; memset(u, 0, sizeof u);
            uint32_t cnt = gt[g] * 3, tex = r[0x18], mat = r[0x1d];
            memcpy(u + 0, r + 0, 12); memcpy(u + 12, &cnt, 4); memcpy(u + 16, r + 12, 12);
            memcpy(u + 28, &tex, 4); memcpy(u + 32, &mat, 4); memcpy(u + 52, &start, 4);
            start += cnt;
            put(o, u, 60);
        }
        close_chunk(o, c); }
    /* indices, rebased onto the merged buffer */
    {   size_t c = open_chunk(o, 0x134b03); align_fill(o, 0x10);
        size_t at = 0;
        for (size_t g = 0; g < ngrp; g++) {
            uint32_t b = base[gs[g]];
            for (uint32_t i = 0; i < gt[g] * 3; i++, at++) {
                uint16_t x; memcpy(&x, idx + at * 2, 2);
                uint32_t y = x + b; uint16_t z = (uint16_t)y;
                if (y >= total) { st.dropped++; z = 0; }
                put(o, &z, 2);
            }
        }
        close_chunk(o, c); }
    close_chunk(o, mesh);
    st.groups += (long)ngrp; st.verts += total; st.tris += (long)(tot_idx / 3);
}

/* ---- one MW solid object (0x80134010) ---- */
static void conv_object(Buf *o, const uint8_t *p, size_t n)
{
    g_night = g_relight;   /* the sky keeps its colours */
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(p + q), sz = rd32(p + q + 4);
        if (q + 8 + sz > n) break;
        if (id == 0x134011) {
            size_t f = skip_fill(p + q + 8, sz);
            if (sz - f > 0xa4 && !memcmp(p + q + 8 + f + 0xa0, "SKY", 3)) g_night = 0;
            break;
        }
        q += 8 + sz;
    }
    size_t obj = open_chunk(o, 0x80134010);
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(p + q), sz = rd32(p + q + 4);
        const uint8_t *d = p + q + 8;
        if (q + 8 + sz > n) { st.bad++; break; }
        if (id == 0x134011) {
            size_t f = skip_fill(d, sz);
            const uint8_t *h = d + f; size_t hn = sz - f;
            if (hn < 0xa0) { put(o, p + q, 8 + sz); }
            else {
                size_t c = open_chunk(o, 0x134011); align_fill(o, 0x10);
                uint8_t u[0xa4]; memset(u, 0, sizeof u);
                memcpy(u, h, 0x94); memcpy(u + 0x94, h + 0x98, 8);
                put(o, u, sizeof u);
                put(o, h + 0xa0, hn - 0xa0);          /* the name (+ its padding) */
                close_chunk(o, c);
            }
        } else if (id == 0x80134100) { conv_mesh(o, d, sz); st.solids++; }
        else if (id == 0x134c02 || id == 0x134017 || id == 0x134018 || id == 0x134019) { /* not in UG2's reader */ }
        else put(o, p + q, 8 + sz);
        q += 8 + sz;
    }
    close_chunk(o, obj);
}

/* ---- scenery ---- */
static void conv_infos(Buf *o, const uint8_t *d, size_t sz)
{
    size_t f = skip_fill(d, sz); size_t n = (sz - f) / 0x48;
    size_t c = open_chunk(o, 0x34102); align_fill(o, 0x10);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = d + f + i * 0x48;
        uint8_t u[0x44]; memset(u, 0, sizeof u);
        memcpy(u, r, 24);                 /* name (24 of UG2's 32) */
        memcpy(u + 0x20, r + 0x18, 12);   /* solid keys 1-3 (LODs) */
        memcpy(u + 0x3c, r + 0x38, 4);    /* radius */
        memcpy(u + 0x40, r + 0x40, 4);    /* hierarchy key */
        put(o, u, sizeof u); st.infos++;
    }
    close_chunk(o, c);
}
static int g_game;
/* Carbon instance (0x60 B): +0 bmin, +0xc flags, +0x10 bmax, +0x1c s16 preculler, +0x20 position,
   +0x2c float 3x3 (scale included), +0x50 key, +0x54 scenery info number */
static void conv_insts_carbon(Buf *o, const uint8_t *d, size_t sz)
{
    size_t f = skip_fill(d, sz); size_t n = (sz - f) / 0x60;
    size_t c = open_chunk(o, 0x34103); align_fill(o, 0x10);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = d + f + i * 0x60;
        uint8_t u[0x40]; memset(u, 0, sizeof u);
        memcpy(u, r, 12); memcpy(u + 12, r + 0x10, 12);    /* bbox */
        memcpy(u + 24, r + 0x54, 2);                       /* scenery info number */
        int32_t pc = (int16_t)(r[0x1c] | r[0x1d] << 8); memcpy(u + 28, &pc, 4);
        memcpy(u + 32, r + 0x20, 12);                      /* position */
        for (int k = 0; k < 9; k++) {                      /* rotation: s16, 0x2000 = 1 */
            float m; memcpy(&m, r + 0x2c + 4 * k, 4);
            long q = lroundf(m * 8192.0f); int16_t v = (int16_t)(q < -32768 ? -32768 : q > 32767 ? 32767 : q);
            memcpy(u + 44 + 2 * k, &v, 2);
        }
        put(o, u, sizeof u); st.insts++;
    }
    close_chunk(o, c);
}
static void conv_insts(Buf *o, const uint8_t *d, size_t sz)
{
    if (g_game == XP_GAME_CARBON) { conv_insts_carbon(o, d, sz); return; }
    size_t f = skip_fill(d, sz); size_t n = (sz - f) / 0x40;
    size_t c = open_chunk(o, 0x34103); align_fill(o, 0x10);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = d + f + i * 0x40;
        uint8_t u[0x40];
        memcpy(u, r, 24);                 /* bbox */
        memcpy(u + 24, r + 62, 2);        /* scenery info number */
        memset(u + 26, 0, 2);             /* UG2 instance flags (MW exclude flags differ) */
        int32_t pc = (int16_t)(r[28] | r[29] << 8); memcpy(u + 28, &pc, 4);   /* preculler index */
        memcpy(u + 32, r + 32, 30);       /* position + packed rotation */
        memset(u + 62, 0, 2);
        put(o, u, sizeof u); st.insts++;
    }
    close_chunk(o, c);
}

/* ---- collision: MW CARP pack (0x3b801) -> UG2 collision pack ---- */
typedef struct { float v[4][3]; int nv, flags, surf, grp; float bb[4]; } XPoly;
static XPoly *g_cp; static size_t g_ncp, g_cpcap;
static void cp_add(const XPoly *p)
{
    if (g_ncp == g_cpcap) {
        size_t c = g_cpcap ? g_cpcap * 2 : 4096;
        XPoly *n = realloc(g_cp, c * sizeof *n); if (!n) { g_oom = 1; return; }
        g_cp = n; g_cpcap = c;
    }
    XPoly *o = &g_cp[g_ncp++]; *o = *p;
    o->bb[0] = o->bb[2] = o->v[0][0]; o->bb[1] = o->bb[3] = o->v[0][1];
    for (int i = 1; i < o->nv; i++) {
        if (o->v[i][0] < o->bb[0]) o->bb[0] = o->v[i][0];
        if (o->v[i][0] > o->bb[2]) o->bb[2] = o->v[i][0];
        if (o->v[i][1] < o->bb[1]) o->bb[1] = o->v[i][1];
        if (o->v[i][1] > o->bb[3]) o->bb[3] = o->v[i][1];
    }
}
/* MW surface key (VLT hash of the name) -> UG2 surface (wcoll_surfaces order) */
static int mw_surface(uint32_t key, int wall)
{
    switch (key) {
    case 0x19db2f1eu: case 0x35187254u: case 0x6ca26f9bu: return wall ? 20 : 1;   /* asphalt(_no_leaves), concrete */
    case 0x772fb736u: case 0x58ec09ddu: case 0x1a20e734u:                          /* grass, grass_shoulder, golf_fairway */
    case 0x3e25caabu: case 0xa3840311u: return wall ? 22 : 15;                      /* golf_rough, golf_green */
    case 0xc1c577d6u: return wall ? 20 : 9;                                         /* gravel */
    case 0xd929e923u: case 0x999acd78u: return wall ? 20 : 8;                       /* dirt, sand */
    case 0x3830babbu: return wall ? 20 : 11;                                        /* cobble */
    case 0x02fc0fc9u: case 0x121453e7u: return wall ? 20 : 10;                      /* stone, railroad */
    case 0x52e42012u: return 17;                                                    /* wood */
    case 0xba404853u: return 26;                                                    /* metal */
    case 0x1d5f6d70u: return 19;                                                    /* glass */
    default: return wall ? 20 : 1;
    }
}
/* MW physics space -> UG2 world */
static void mw_world(const float *ph, float *w) { w[0] = ph[2]; w[1] = -ph[0]; w[2] = ph[1]; }
static int16_t q16(float v) { long k = lroundf(v); return (int16_t)(k < -32768 ? -32768 : k > 32767 ? 32767 : k); }

/* the minimap collector (Carbon has no map art for its city): every ground triangle's
   xy + its surface key, instead of the collision pack */
typedef struct { float xy[6]; uint32_t key; } MmTri;
static MmTri *g_mm; static size_t g_mmn, g_mmcap; static int g_mm_on;
static void mm_add(const float *a, const float *b, const float *c, uint32_t key)
{
    if (g_mmn == g_mmcap) {
        size_t n = g_mmcap ? g_mmcap * 2 : 65536;
        MmTri *t = realloc(g_mm, n * sizeof *t); if (!t) { g_oom = 1; return; }
        g_mm = t; g_mmcap = n;
    }
    MmTri *t = &g_mm[g_mmn++];
    t->xy[0] = a[0]; t->xy[1] = a[1]; t->xy[2] = b[0]; t->xy[3] = b[1]; t->xy[4] = c[0]; t->xy[5] = c[1];
    t->key = key;
}
static void bake_article(const uint8_t *a, size_t an, const float *ip, int grp)
{
    if (an < 16) { st.bad++; return; }
    int ns = a[0] | a[1] << 8, ssz = a[2] | a[3] << 8, nb = a[4] | a[5] << 8, bsz = a[6] | a[7] << 8, nsf = a[9];
    if ((size_t)16 + ssz + bsz + 4 * (size_t)nsf > an) { st.bad++; return; }
    const uint8_t *sf = a + 16 + ssz + bsz;
    for (int s = 0; s < ns; s++) {
        const uint8_t *h = a + 16 + 16 * s;
        float base[3]; memcpy(base, h, 12);
        int off = h[14] | h[15] << 8;
        const uint8_t *v = a + 16 + off;
        if (off + 8 > ssz) { st.bad++; continue; }
        int n = v[6];
        if (off + 8 * n > ssz) { st.bad++; continue; }
        float P[256][3];
        for (int i = 0; i < n; i++) {
            float ph[3];
            for (int k = 0; k < 3; k++) ph[k] = base[k] + (int16_t)(v[8*i + 2*k] | v[8*i + 2*k + 1] << 8) / 128.0f - ip[k];
            mw_world(ph, P[i]);
        }
        for (int i = 0; i + 2 < n; i++) {
            XPoly t; memset(&t, 0, sizeof t);
            memcpy(t.v[0], P[i], 12); memcpy(t.v[1], P[i + 1], 12); memcpy(t.v[2], P[i + 2], 12);
            float e1[3], e2[3];
            for (int k = 0; k < 3; k++) { e1[k] = t.v[1][k] - t.v[0][k]; e2[k] = t.v[2][k] - t.v[0][k]; }
            float nx = e1[1]*e2[2] - e1[2]*e2[1], ny = e1[2]*e2[0] - e1[0]*e2[2], nz = e1[0]*e2[1] - e1[1]*e2[0];
            float l = sqrtf(nx*nx + ny*ny + nz*nz);
            if (!(l > 1e-4f)) continue;                     /* degenerate (strip joins) */
            if (nz < 0) {                                   /* counter-clockwise from above */
                float x[3]; memcpy(x, t.v[1], 12); memcpy(t.v[1], t.v[2], 12); memcpy(t.v[2], x, 12); nz = -nz;
            }
            memcpy(t.v[3], t.v[2], 12);
            t.nv = 3;
            int si = v[8 * (i + 2) + 6];
            if (g_mm_on) { if (nz / l >= 0.25f) mm_add(t.v[0], t.v[1], t.v[2], si < nsf ? rd32(sf + 4 * si) : 0); continue; }
            t.surf = mw_surface(si < nsf ? rd32(sf + 4 * si) : 0, 0);
            t.flags = nz / l < 0.25f ? 0x08 : 0x01;          /* steep: UG2's 0x8 wall triangles; ground: layered */
            t.grp = grp;
            cp_add(&t); st.ctris++;
        }
    }
    const uint8_t *b = a + 16 + ssz;
    for (int i = 0; !g_mm_on && i < nb && 32 * (i + 1) <= bsz; i++) {
        const uint8_t *r = b + 32 * i;
        float p0[3], p1[3], w0[3], w1[3];
        memcpy(p0, r, 12); memcpy(p1, r + 16, 12);
        for (int k = 0; k < 3; k++) { p0[k] -= ip[k]; p1[k] -= ip[k]; }
        mw_world(p0, w0); mw_world(p1, w1);
        float zlo = w0[2] < w1[2] ? w0[2] : w1[2], zhi = w0[2] < w1[2] ? w1[2] : w0[2];
        /* spans over 250 m (MW's "endless" walls, article y +-576) are fitted to the
           pack's ground in conv_carp: packed heights reach +-128 m from the middle */
        XPoly q; memset(&q, 0, sizeof q);
        q.v[0][0] = w0[0]; q.v[0][1] = w0[1]; q.v[0][2] = zlo;
        q.v[1][0] = w1[0]; q.v[1][1] = w1[1]; q.v[1][2] = zlo;
        q.v[2][0] = w1[0]; q.v[2][1] = w1[1]; q.v[2][2] = zhi;
        q.v[3][0] = w0[0]; q.v[3][1] = w0[1]; q.v[3][2] = zhi;
        q.nv = 4; q.flags = 0x12;                           /* UG2's wall quads */
        int si = r[12];
        q.surf = mw_surface(si < nsf ? rd32(sf + 4 * si) : 0, 1);
        q.grp = grp;
        cp_add(&q); st.cwalls++;
    }
}
static const uint8_t *g_art[2048]; static size_t g_artn[2048];
static void conv_carp(Buf *o, const uint8_t *d, size_t sz, const uint8_t *raw)
{
    size_t f = skip_fill(d, sz);
    const uint8_t *h = d + f; size_t hn = sz - f;
    if (hn < 0x50) { put(o, raw, 8 + sz); return; }
    int sec = (int)rd32(h + 4);
    memset(g_art, 0, sizeof g_art);
    const uint8_t *inst = NULL; uint32_t ninst = 0;
    for (size_t q = 0x40; q + 16 <= hn; q += 16) {
        const uint8_t *e = h + q;
        int isa = e[2] == 'a' && e[3] == 'c', isi = e[2] == 'i' && e[3] == 'c';
        if (!isa && !isi) break;
        uint32_t ts = rd32(e + 4), cnt = rd32(e + 8), off = rd32(e + 12);
        if (q + off + (ts >> 8) > hn) { st.bad++; continue; }
        int idx = e[0] | e[1] << 8;
        if (isa && idx < 2048) { g_art[idx] = e + off; g_artn[idx] = ts >> 8; }
        if (isi) { inst = e + off; ninst = cnt; if ((size_t)64 * cnt > (ts >> 8)) ninst = (ts >> 8) / 64; }
    }
    g_ncp = 0;
    for (uint32_t i = 0; inst && i < ninst; i++) {
        const uint8_t *r = inst + 64 * i;
        float r0[3], ip[3]; memcpy(r0, r, 12); memcpy(ip, r + 0x30, 12);
        if (fabsf(r0[0] - 1.0f) > 1e-3f) st.crot++;        /* not met in Rockport: baked unrotated */
        int grp = r[0x18] | r[0x19] << 8, ai = r[0x1a] | r[0x1b] << 8;
        if (ai >= 2048 || !g_art[ai]) { st.bad++; continue; }
        /* MW scenery groups (race barriers, ...): the MW group id (L2RA 0x34109, named
           in the translated companion's 0x3412f); a race switches its own on */
        bake_article(g_art[ai], g_artn[ai], ip, grp);
        if (grp) st.cgrouped++;
    }
    if (g_oom || g_mm_on) return;   /* the minimap collector wants the triangles only */
    if (!g_ncp) { st.cempty++; return; }
    {   /* walls taller than the packed 250 m: MW's endless walls (-128..1024 m). Keep
           the part around this pack's ground; keeping the top put them in the sky
           (11-18: the car drove under them off the road north of Rockport event 7302) */
        float gmin = 1e30f, gmax = -1e30f;
        for (size_t i = 0; i < g_ncp; i++)
            if (!(g_cp[i].flags & 0x10))
                for (int k = 0; k < g_cp[i].nv; k++) {
                    if (g_cp[i].v[k][2] < gmin) gmin = g_cp[i].v[k][2];
                    if (g_cp[i].v[k][2] > gmax) gmax = g_cp[i].v[k][2];
                }
        for (size_t i = 0; i < g_ncp; i++) {
            XPoly *q = &g_cp[i];
            if (!(q->flags & 0x10) || q->v[2][2] - q->v[0][2] <= 250.0f) continue;
            float lo = q->v[0][2], hi = q->v[2][2];
            float want = gmin <= gmax ? gmin - 50.0f : -50.0f;   /* no ground here: Rockport's sea level */
            if (lo < want) lo = want;
            if (lo + 250.0f > hi) lo = hi - 250.0f;
            hi = lo + 250.0f;
            q->v[0][2] = q->v[1][2] = lo; q->v[2][2] = q->v[3][2] = hi;
            st.ctall++;
        }
    }
    {   /* ground first (UG2 ends a cell's ground at its first wall); stable, so
           the PC tool and the console write the same bytes */
        XPoly *t = malloc(g_ncp * sizeof *t);
        if (!t) { g_oom = 1; return; }
        size_t k = 0;
        for (int pass = 0; pass < 2; pass++)
            for (size_t i = 0; i < g_ncp; i++)
                if (((g_cp[i].flags & 0xa) != 0) == pass) t[k++] = g_cp[i];
        memcpy(g_cp, t, g_ncp * sizeof *t); free(t);
    }
    if (g_ncp > 65535) { st.coverflow++; g_ncp = 65535; }
    /* the 32 m grid over the polygons */
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < g_ncp; i++)
        for (int k = 0; k < g_cp[i].nv; k++)
            for (int a = 0; a < 3; a++) {
                if (g_cp[i].v[k][a] < mn[a]) mn[a] = g_cp[i].v[k][a];
                if (g_cp[i].v[k][a] > mx[a]) mx[a] = g_cp[i].v[k][a];
            }
    float x0 = floorf(mn[0] / 32.0f) * 32.0f, y0 = floorf(mn[1] / 32.0f) * 32.0f;
    int W = (int)((mx[0] - x0) / 32.0f) + 1, H = (int)((mx[1] - y0) / 32.0f) + 1;
    if (W > 120 || H > 120) { st.coverflow++; return; }    /* polygon xy is s16 / 8 from the origin */
    /* cell lists: (cell, polygon | sub-cell mask << 16), polygons in order */
    size_t nl = 0, lcap = g_ncp * 4 + 16;
    uint32_t *lc = malloc(lcap * sizeof *lc), *lp = malloc(lcap * sizeof *lp);
    uint32_t *cnt = calloc((size_t)W * H + 1, sizeof *cnt);
    if (!lc || !lp || !cnt) { free(lc); free(lp); free(cnt); g_oom = 1; return; }
    for (size_t i = 0; i < g_ncp && !g_oom; i++) {
        const float *bb = g_cp[i].bb;
        int cx0 = (int)((bb[0] - x0) / 32.0f), cx1 = (int)((bb[2] - x0) / 32.0f);
        int cy0 = (int)((bb[1] - y0) / 32.0f), cy1 = (int)((bb[3] - y0) / 32.0f);
        if (cx1 >= W) cx1 = W - 1;
        if (cy1 >= H) cy1 = H - 1;
        for (int cy = cy0; cy <= cy1 && !g_oom; cy++)
            for (int cx = cx0; cx <= cx1; cx++) {
                float ox = x0 + 32.0f * cx, oy = y0 + 32.0f * cy;
                int sx0 = (int)floorf((bb[0] - ox) / 8.0f), sx1 = (int)floorf((bb[2] - ox) / 8.0f);
                int sy0 = (int)floorf((bb[1] - oy) / 8.0f), sy1 = (int)floorf((bb[3] - oy) / 8.0f);
                if (sx0 < 0) sx0 = 0;
                if (sy0 < 0) sy0 = 0;
                if (sx1 > 3) sx1 = 3;
                if (sy1 > 3) sy1 = 3;
                uint32_t m = 0;
                for (int sy = sy0; sy <= sy1; sy++) for (int sx = sx0; sx <= sx1; sx++) m |= 1u << (sx + 4 * sy);
                if (!m) continue;
                if (nl == lcap) {
                    lcap *= 2;
                    uint32_t *a = realloc(lc, lcap * sizeof *a); if (a) lc = a;
                    uint32_t *b = realloc(lp, lcap * sizeof *b); if (b) lp = b;
                    if (!a || !b) { g_oom = 1; break; }
                }
                lc[nl] = (uint32_t)(cy * W + cx); lp[nl] = (uint32_t)i | m << 16; nl++;
                cnt[cy * W + cx]++;
            }
    }
    if (g_oom || nl > 65535) { if (!g_oom) st.coverflow++; free(lc); free(lp); free(cnt); return; }
    uint32_t *start = calloc((size_t)W * H + 1, sizeof *start);
    uint32_t *ord = malloc((nl ? nl : 1) * sizeof *ord);
    if (!start || !ord) { free(lc); free(lp); free(cnt); free(start); free(ord); g_oom = 1; return; }
    for (int c = 1; c <= W * H; c++) start[c] = start[c - 1] + cnt[c - 1];
    memset(cnt, 0, (size_t)W * H * sizeof *cnt);
    for (size_t e = 0; e < nl; e++) ord[start[lc[e]] + cnt[lc[e]]++] = lp[e];   /* stable: polygons stay in order */

    /* the pack; its 0x34131 header data on a 16-byte file boundary (wcoll's reader) */
    size_t pk = open_chunk(o, 0x80034130);
    size_t hc = open_chunk(o, 0x34131);
    align_fill(o, 0x10);
    uint8_t hd[48]; memset(hd, 0, sizeof hd);
    hd[0] = (uint8_t)sec; hd[1] = (uint8_t)(sec >> 8);
    hd[4] = (uint8_t)W; hd[6] = (uint8_t)H;
    memcpy(hd + 8, &x0, 4); memcpy(hd + 12, &y0, 4);
    memcpy(hd + 16, mn, 12); memcpy(hd + 32, mx, 12);
    put(o, hd, sizeof hd);
    close_chunk(o, hc);
    size_t pc = open_chunk(o, 0x34134);
    for (size_t i = 0; i < g_ncp; i++) {
        const XPoly *p = &g_cp[i];
        uint8_t u[32]; memset(u, 0, sizeof u);
        int16_t X[4], Y[4];
        float zm = 0; for (int k = 0; k < 4; k++) zm += p->v[k][2];
        int16_t zb = q16(zm * 0.25f);
        for (int k = 0; k < 4; k++) {
            X[k] = q16((p->v[k][0] - x0) * 8.0f); Y[k] = q16((p->v[k][1] - y0) * 8.0f);
            int16_t zl = q16((p->v[k][2] - zb) * 256.0f);
            memcpy(u + 8 + 2*k, &X[k], 2); memcpy(u + 0x10 + 2*k, &Y[k], 2); memcpy(u + 0x18 + 2*k, &zl, 2);
        }
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        for (int k = 1; k < 4; k++) {
            if (X[k] < X[ix0]) ix0 = k;
            if (Y[k] < Y[iy0]) iy0 = k;
            if (X[k] > X[ix1]) ix1 = k;
            if (Y[k] > Y[iy1]) iy1 = k;
        }
        u[0] = (uint8_t)(p->grp ? 255 : 0); u[1] = (uint8_t)(ix0 | iy0 << 2 | ix1 << 4 | iy1 << 6);
        u[2] = (uint8_t)p->surf; u[3] = (uint8_t)(p->flags | (p->nv == 4 ? 0x10 : 0));
        memcpy(u + 4, &zb, 2);
        u[6] = p->grp ? (uint8_t)p->grp : 0xff; u[7] = p->grp ? (uint8_t)(p->grp >> 8) : 0xff;   /* (port) MW group id, wcoll grp_on */
        put(o, u, sizeof u);
    }
    close_chunk(o, pc);
    size_t lcn = open_chunk(o, 0x34136);
    for (size_t e = 0; e < nl; e++) { uint16_t a[2] = { (uint16_t)(ord[e] & 0xffff), (uint16_t)(ord[e] >> 16) }; put(o, a, 4); }
    close_chunk(o, lcn);
    size_t cc = open_chunk(o, 0x34137);
    for (int c = 0; c < W * H; c++) {
        uint16_t a[2] = { (uint16_t)start[c], (uint16_t)(start[c + 1] - start[c]) }; put(o, a, 4);
    }
    close_chunk(o, cc);
    size_t oc = open_chunk(o, 0x341f0);                      /* (port) the polygons' xy origin */
    put(o, "XPC1", 4); put(o, &x0, 4); put(o, &y0, 4); put32(o, 0);
    close_chunk(o, oc);
    close_chunk(o, pk);
    st.cpacks++; st.cpolys += (long)g_ncp;
    free(lc); free(lp); free(cnt); free(start); free(ord);
}

/* containers walked (rebuilt), everything else copied */
/* ---- the night lights (X4): MW has no light flares, so its lamp scenery makes them:
   each lamp model's head (xp_lamps.inc, from lampheads.py) at every instance becomes a
   UG2 world flare (0x80135100 pack per scenery section, read by aciddraw's wf_pack) ---- */
typedef struct { uint32_t key; int n; float h[4][3]; } XpLamp;
static const XpLamp k_lamps[] = {
#include "xp_lamps.inc"
};
static const XpLamp *lamp_find(uint32_t key)
{
    for (size_t i = 0; i < sizeof k_lamps / sizeof *k_lamps; i++) if (k_lamps[i].key == key) return &k_lamps[i];
    return NULL;
}
#define XP_MAXFL 4096
static void lamp_pack(Buf *o, const uint8_t *d, size_t n)
{
    const uint8_t *sec = NULL, *inf = NULL, *ins = NULL; size_t ninf = 0, nins = 0;
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(d + q), sz = rd32(d + q + 4); const uint8_t *c = d + q + 8;
        if (q + 8 + sz > n) return;
        size_t f = skip_fill(c, sz);
        if (id == 0x34101 && sz >= 0x10) sec = c;
        else if (id == 0x34102) { inf = c + f; ninf = (sz - f) / 0x48; }
        else if (id == 0x34103) { ins = c + f; nins = (sz - f) / 0x40; }
        q += 8 + sz;
    }
    if (!sec || !inf || !ins) return;
    static float fl[XP_MAXFL][3]; int nf = 0;
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < nins && nf < XP_MAXFL; i++) {
        const uint8_t *r = ins + i * 0x40;
        unsigned k = (unsigned)(r[62] | r[63] << 8); if (k >= ninf) continue;
        const XpLamp *L = lamp_find(rd32(inf + k * 0x48 + 0x18)); if (!L) continue;
        float m[9], t[3];
        for (int j = 0; j < 9; j++) m[j] = (float)(int16_t)(r[0x2c + 2 * j] | r[0x2d + 2 * j] << 8) / 8192.0f;
        for (int j = 0; j < 3; j++) memcpy(&t[j], r + 0x20 + 4 * j, 4);
        for (int h = 0; h < L->n && nf < XP_MAXFL; h++) {
            float *p = fl[nf++];
            for (int c = 0; c < 3; c++) {   /* row-vector basis, as winst_decode_placement */
                p[c] = t[c] + L->h[h][0] * m[c] + L->h[h][1] * m[3 + c] + L->h[h][2] * m[6 + c];
                if (p[c] < mn[c]) mn[c] = p[c];
                if (p[c] > mx[c]) mx[c] = p[c];
            }
        }
    }
    if (!nf) return;
    size_t pk = open_chunk(o, 0x80135100);
    size_t hc = open_chunk(o, 0x135101);
    uint8_t h[0x60]; memset(h, 0, sizeof h);
    uint32_t u = (uint32_t)nf; memcpy(h, &u, 4); memcpy(h + 4, &u, 4); memcpy(h + 8, &u, 4);
    memcpy(h + 0x30, mn, 12); memcpy(h + 0x40, mx, 12);
    memcpy(h + 0x50, &u, 4); memcpy(h + 0x54, sec + 0x0c, 4);
    put(o, h, sizeof h); close_chunk(o, hc);
    size_t rc = open_chunk(o, 0x135102); align_fill(o, 0x10);
    for (int i = 0; i < nf; i++) {
        uint8_t r[0x30]; memset(r, 0, sizeof r);
        r[0] = r[4] = 0x0b; r[12] = r[13] = r[14] = 0xff;
        memcpy(r + 0x10, fl[i], 12);
        const float dn[3] = { 0.0f, 0.0f, -1.0f }; memcpy(r + 0x20, dn, 12);
        r[0x2c] = 9; r[0x2d] = 2;   /* Bayview's street lamps: type 9, flags 2, facing down */
        put(o, r, sizeof r);
    }
    close_chunk(o, rc); close_chunk(o, pk);
    st.lights += nf;
}
/* Carbon texture packs (TPK version 8) -> the UG2 layout the loader reads (n2_tpk_decode):
   Carbon's 0x33310004 rows are 0x58 bytes (12 zero, then +0 key, +8 pixel offset, +0x10
   size, +0x1c w/h u16, +0x22 format 0x22/0x24/0x26, +0x29 alpha usage, +0x2a alpha blend)
   + a length byte + the name. UG2 rows are 0x7c: 12 bytes, a 24-byte name, then the key at
   name+0x18, offset +0x24, size +0x2c, w/h +0x38, format +0x3e, order/usage/blend/wz
   +0x45/+0x49/+0x4a/+0x4b. The pixel block is UG2's already (0x78 bytes of 0x11 filler,
   offsets from the end of it).
   Alpha (port guess from the names): usage 4/6 = cut out (fences, lit windows), 5/7 =
   blended (flares, neon, water), blend 2 = additive. Carbon sets most cut-outs in the
   material, not the texture: its DXT3 textures are all cut-outs (railings, chain-link,
   foliage, panoramas) and so are its DXT5 ORG_ (plant) ones; other DXT5 alpha is not. */
static long g_tpk_tex, g_tpk_alpha;
static void tpk_rows_carbon(Buf *o, const uint8_t *d, size_t n)
{
    size_t c = open_chunk(o, 0x33310004);
    for (size_t q = 0; q + 0x59 <= n; ) {
        const uint8_t *C = d + q + 0xc;
        size_t L = d[q + 0x58];
        if (q + 0x59 + L > n) { st.bad++; break; }
        uint8_t r[0x7c]; memset(r, 0, sizeof r);
        uint8_t *N = r + 12;
        const char *nm = (const char *)d + q + 0x59;
        size_t k = 0;
        if (!(nm[0] >= 'A' && nm[0] <= 'Z')) N[k++] = 'X';   /* the loader finds a row by its capital */
        for (size_t j = 0; j < L && nm[j] && k < 23; j++) N[k++] = (uint8_t)nm[j];
        memcpy(N + 0x18, C, 4);            /* key */
        memcpy(N + 0x1c, C + 4, 4);        /* class */
        memcpy(N + 0x24, C + 8, 4);        /* pixel offset */
        memcpy(N + 0x2c, C + 0x10, 4);     /* size (whole mip chain) */
        memcpy(N + 0x38, C + 0x1c, 4);     /* w, h */
        N[0x3e] = C[0x22];
        int au = C[0x29], ab = C[0x2a];
        if (au == 4 || au == 6) { N[0x45] = 5; N[0x49] = 1; N[0x4b] = 1; }
        else if (au == 5 || au == 7) { N[0x45] = 6; N[0x49] = 2; N[0x4a] = ab == 2 ? 2 : 1; }
        else if (C[0x22] == 0x24 || (C[0x22] == 0x26 && L > 4 && !memcmp(nm, "ORG_", 4))) { N[0x45] = 5; N[0x49] = 1; N[0x4b] = 1; }
        else N[0x4b] = 1;
        if (ab == 2) N[0x4a] = 2;
        if (N[0x49] || N[0x4a]) g_tpk_alpha++;
        put(o, r, sizeof r); g_tpk_tex++;
        q += 0x59 + L;
    }
    close_chunk(o, c);
}
static void conv_tpk_carbon(Buf *o, const uint8_t *d, size_t sz)
{
    size_t top = open_chunk(o, 0xb3300000);
    for (size_t q = 0; q + 8 <= sz; ) {
        uint32_t id = rd32(d + q), n = rd32(d + q + 4);
        if (q + 8 + n > sz) { st.bad++; break; }
        const uint8_t *p = d + q + 8;
        if (id == 0xb3310000) {
            size_t h = open_chunk(o, id);
            for (size_t r = 0; r + 8 <= n; ) {
                uint32_t cid = rd32(p + r), cn = rd32(p + r + 4);
                if (r + 8 + cn > n) { st.bad++; break; }
                if (cid == 0x33310004) tpk_rows_carbon(o, p + r + 8, cn);
                else if (cid != 0x33310005) put(o, p + r, 8 + cn);   /* 0x33310005: Carbon's per-texture FourCCs */
                r += 8 + cn;
            }
            close_chunk(o, h);
        } else if (id == 0xb3320000) {
            size_t h = open_chunk(o, id);
            for (size_t r = 0; r + 8 <= n; ) {
                uint32_t cid = rd32(p + r), cn = rd32(p + r + 4);
                if (r + 8 + cn > n) { st.bad++; break; }
                if (cid) put(o, p + r, 8 + cn);   /* its alignment padding goes */
                r += 8 + cn;
            }
            close_chunk(o, h);
        } else put(o, d + q, 8 + n);
        q += 8 + n;
    }
    close_chunk(o, top);
}
static int is_walked(uint32_t id) { return id == 0x80134000 || id == 0x80034100; }
static void conv_chunk(Buf *o, uint32_t id, const uint8_t *d, size_t sz, const uint8_t *raw);
static void walk(Buf *o, const uint8_t *p, size_t n)
{
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(p + q), sz = rd32(p + q + 4);
        if (q + 8 + sz > n) { st.bad++; return; }
        conv_chunk(o, id, p + q + 8, sz, p + q);
        q += 8 + sz;
    }
}
static void conv_chunk(Buf *o, uint32_t id, const uint8_t *d, size_t sz, const uint8_t *raw)
{
    if (id == 0x80134010) conv_object(o, d, sz);
    else if (id == 0x34102) conv_infos(o, d, sz);
    else if (id == 0x34103) conv_insts(o, d, sz);
    else if (id == 0x3b801) conv_carp(o, d, sz, raw);
    else if (id == 0xb3300000 && g_game == XP_GAME_CARBON) conv_tpk_carbon(o, d, sz);
    else if (is_walked(id)) { size_t c = open_chunk(o, id); walk(o, d, sz); close_chunk(o, c); }
    else put(o, raw, 8 + sz);
    if (id == 0x80034100 && g_relight) lamp_pack(o, d, sz);   /* after its section */
}

/* One top-level chunk in memory at a time (MW's largest 7.8 MB, Carbon's 13.8 MB),
   so the same code runs on the Vita. Top-level chunks keep their input offset
   mod 0x80 (an id-0 padding chunk in front), as the loaders expect of TPKs. */
int xp_convert(const char *in_path, const char *out_path, int game,
               XpProgress progress, void *ud, XpStats *out_stats)
{
    g_relight = game == XP_GAME_MW;   /* Carbon is a night city already */
    g_game = game;
    if (game == XP_GAME_MW) { g_grec = 0x68; g_gvo = 0x3c; g_gto = 0x40; }
    else { g_grec = 0x90; g_gvo = 0x40; g_gto = 0x60; }
    memset(&st, 0, sizeof st); g_oom = 0; g_tpk_tex = g_tpk_alpha = 0;
    FILE *f = fopen(in_path, "rb"); if (!f) return XP_ERR_OPEN_IN;
    FILE *w = fopen(out_path, "wb"); if (!w) { fclose(f); return XP_ERR_OPEN_OUT; }
    fseek(f, 0, SEEK_END);
#ifdef _WIN32
    long long total = _ftelli64(f);
#else
    long long total = ftell(f);   /* the Vita's newlib ftell is 32-bit: fine below 2 GB */
#endif
    fseek(f, 0, SEEK_SET);
    Buf o = {0}; uint8_t *chunk = NULL; size_t chunk_cap = 0;
    long long in_at = 0; int rc = XP_OK;
    while (in_at + 8 <= total) {
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) { rc = XP_ERR_READ; break; }
        uint32_t id = rd32(h), sz = rd32(h + 4);
        if (in_at + 8 + (long long)sz > total) { rc = XP_ERR_FORMAT; break; }
        if (8 + (size_t)sz > chunk_cap) {
            uint8_t *nc = realloc(chunk, 8 + (size_t)sz); if (!nc) { rc = XP_ERR_MEMORY; break; }
            chunk = nc; chunk_cap = 8 + (size_t)sz;
        }
        memcpy(chunk, h, 8);
        if (fread(chunk + 8, 1, sz, f) != sz) { rc = XP_ERR_READ; break; }
        size_t want = (size_t)(in_at % 0x80), have = (size_t)((o.base + o.n) % 0x80);
        if (want != have) {
            size_t k = (want - have + 0x80) % 0x80; if (k < 8) k += 0x80;
            put32(&o, 0); put32(&o, (uint32_t)(k - 8)); putz(&o, k - 8);
        }
        conv_chunk(&o, id, chunk + 8, sz, chunk);
        if (g_oom) { rc = XP_ERR_MEMORY; break; }
        if (o.n && fwrite(o.p, 1, o.n, w) != o.n) { rc = XP_ERR_WRITE; break; }
        o.base += o.n; o.n = 0;
        in_at += 8 + sz;
        if (progress && progress(ud, in_at, total)) { rc = XP_ERR_CANCELLED; break; }
    }
    st.in_bytes = total; st.out_bytes = o.base;
    free(g_cp); g_cp = NULL; g_ncp = g_cpcap = 0;
    free(o.p); free(chunk); fclose(f);
    if (fclose(w) && rc == XP_OK) rc = XP_ERR_WRITE;
    if (rc != XP_OK) remove(out_path);   /* never leave a half-written stream */
    if (g_tpk_tex) printf("xp_convert: %ld Carbon textures translated (%ld with alpha)\n", g_tpk_tex, g_tpk_alpha);
    if (out_stats) *out_stats = st;
    return rc;
}

/* ---- the minimap: MW TRACKS/L2RA/MINI_MAP.BIN -> the game's BMAP file ----
   MINI_MAP.BIN = 64 chunks 0x3a100, each a JDLZ-packed TPK holding one 128x128
   DXT3 tile MINI_MAP_CHOP<n> (n = row * 8 + column, north up): a 1024 px map.
   Its world window was fitted to the collision's asphalt (11-17: 82% of the
   asphalt vertices land on road pixels, exactly 6.5 m a pixel). */
static int jdlz(const uint8_t *s, int slen, uint8_t *out, int cap)   /* nfsu2.h n2_jdlz */
{
    if (slen < 16 || memcmp(s, "JDLZ", 4)) return 0;
    int usize = (int)rd32(s + 8); if (usize > cap) usize = cap;
    int op = 0, ip = 16, f1 = 1, f2 = 1;
    while (ip < slen && op < usize) {
        if (f1 == 1) { if (ip >= slen) break; f1 = s[ip++] | 0x100; }
        if (f2 == 1) { if (ip >= slen) break; f2 = s[ip++] | 0x100; }
        if (f1 & 1) {
            int len, dist;
            if (ip + 2 > slen) break;
            if (f2 & 1) { len = (s[ip+1] | ((s[ip] & 0xF0) << 4)) + 3; dist = (s[ip] & 0x0F) + 1; }
            else        { dist = (s[ip+1] | ((s[ip] & 0xE0) << 3)) + 17; len = (s[ip] & 0x1F) + 3; }
            ip += 2;
            if (dist > op) break;
            for (int k = 0; k < len && op < usize; k++) { out[op] = out[op - dist]; op++; }
            f2 >>= 1;
        } else { if (ip >= slen) break; out[op++] = s[ip++]; }
        f1 >>= 1;
    }
    return op;
}
static const uint8_t *find_id(const uint8_t *p, size_t n, uint32_t id)
{
    for (size_t i = 0; i + 8 <= n; i += 4) if (rd32(p + i) == id) return p + i;
    return NULL;
}
/* Carbon: Palmont's map drawn from its own collision in MW's map style -- road
   surfaces light grey on dark grey; the window
   squares the city's roads (+6%). in_path = the raw STREAML5RA.BUN */
static int is_road_key(uint32_t k) { return k == 0x19db2f1eu || k == 0x35187254u || k == 0x6ca26f9bu; }
static void mm_fill(uint8_t *img, int W, const float *xy, float x0, float y0, float px, const uint8_t *rgb)
{
    float X[3], Y[3];
    for (int k = 0; k < 3; k++) { X[k] = (xy[2*k] - x0) / px; Y[k] = (y0 - xy[2*k+1]) / px; }
    float mnx = fminf(X[0], fminf(X[1], X[2])), mxx = fmaxf(X[0], fmaxf(X[1], X[2]));
    float mny = fminf(Y[0], fminf(Y[1], Y[2])), mxy = fmaxf(Y[0], fmaxf(Y[1], Y[2]));
    int ix0 = (int)floorf(mnx), ix1 = (int)ceilf(mxx), iy0 = (int)floorf(mny), iy1 = (int)ceilf(mxy);
    if (ix1 < 0 || iy1 < 0 || ix0 >= W || iy0 >= W) return;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= W) ix1 = W - 1;
    if (iy1 >= W) iy1 = W - 1;
    float d = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    int any = 0;
    if (fabsf(d) > 1e-9f)
        for (int y = iy0; y <= iy1; y++)
            for (int x = ix0; x <= ix1; x++) {
                float cx = x + 0.5f, cy = y + 0.5f;
                float a = ((X[1] - cx) * (Y[2] - cy) - (X[2] - cx) * (Y[1] - cy)) / d;
                float b = ((X[2] - cx) * (Y[0] - cy) - (X[0] - cx) * (Y[2] - cy)) / d;
                if (a < 0 || b < 0 || a + b > 1) continue;
                memcpy(img + ((size_t)y * W + x) * 4, rgb, 3); any = 1;
            }
    if (!any) {   /* thinner than a pixel: its middle */
        int x = (int)((X[0] + X[1] + X[2]) / 3), y = (int)((Y[0] + Y[1] + Y[2]) / 3);
        if (x >= 0 && y >= 0 && x < W && y < W) memcpy(img + ((size_t)y * W + x) * 4, rgb, 3);
    }
}
static void conv_carp(Buf *o, const uint8_t *d, size_t sz, const uint8_t *raw);
static int minimap_carbon(const char *in_path, const char *out_path)
{
    FILE *f = fopen(in_path, "rb"); if (!f) return XP_ERR_OPEN_IN;
    g_mmn = 0; g_mm_on = 1; g_oom = 0;
    uint8_t *chunk = NULL; size_t cap = 0; int rc = XP_OK;
    Buf scratch = {0};
    for (;;) {
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) break;
        uint32_t id = rd32(h), sz = rd32(h + 4);
        if (id != 0x3b801) { if (fseek(f, (long)sz, SEEK_CUR)) break; continue; }
        if (8 + (size_t)sz > cap) { uint8_t *n = realloc(chunk, 8 + (size_t)sz); if (!n) { rc = XP_ERR_MEMORY; break; } chunk = n; cap = 8 + sz; }
        memcpy(chunk, h, 8);
        if (fread(chunk + 8, 1, sz, f) != sz) { rc = XP_ERR_READ; break; }
        conv_carp(&scratch, chunk + 8, sz, chunk);
        scratch.n = 0;
        if (g_oom) { rc = XP_ERR_MEMORY; break; }
    }
    fclose(f); free(chunk); free(scratch.p);
    g_mm_on = 0;
    free(g_cp); g_cp = NULL; g_ncp = g_cpcap = 0;
    size_t nr = 0;
    for (size_t i = 0; i < g_mmn; i++) nr += is_road_key(g_mm[i].key);
    if (rc == XP_OK && nr < 100) rc = XP_ERR_FORMAT;
    float *xs = NULL, *ys = NULL;
    if (rc == XP_OK) { xs = malloc(nr * sizeof *xs); ys = malloc(nr * sizeof *ys); if (!xs || !ys) rc = XP_ERR_MEMORY; }
    const int W = 1024;
    uint8_t *img = rc == XP_OK ? malloc((size_t)W * W * 4) : NULL;
    if (rc == XP_OK && !img) rc = XP_ERR_MEMORY;
    if (rc == XP_OK) {
        size_t k = 0;
        for (size_t i = 0; i < g_mmn; i++)
            if (is_road_key(g_mm[i].key)) { xs[k] = (g_mm[i].xy[0] + g_mm[i].xy[2] + g_mm[i].xy[4]) / 3; ys[k] = (g_mm[i].xy[1] + g_mm[i].xy[3] + g_mm[i].xy[5]) / 3; k++; }
        /* the city = the largest 8-connected cluster of 256 m cells with road in them (Carbon's
           canyon tracks lie kilometres away, outside it) */
        float gx0 = 1e30f, gy0 = 1e30f, gx1 = -1e30f, gy1 = -1e30f;
        for (size_t i = 0; i < nr; i++) { gx0 = fminf(gx0, xs[i]); gx1 = fmaxf(gx1, xs[i]); gy0 = fminf(gy0, ys[i]); gy1 = fmaxf(gy1, ys[i]); }
        int GW = (int)((gx1 - gx0) / 256.0f) + 1, GH = (int)((gy1 - gy0) / 256.0f) + 1;
        int *lab = calloc((size_t)GW * GH, sizeof *lab), *stk = malloc((size_t)GW * GH * sizeof *stk);
        float ax = 0, bx = 0, ay = 0, by = 0;
        if (!lab || !stk) { ax = gx0; bx = gx1; ay = gy0; by = gy1; }   /* no room: every road */
        else {
            for (size_t i = 0; i < nr; i++) lab[(int)((ys[i] - gy0) / 256.0f) * GW + (int)((xs[i] - gx0) / 256.0f)] = -1;
            int best = 0, bestn = 0, nl = 0;
            for (int c = 0; c < GW * GH; c++) {
                if (lab[c] != -1) continue;
                int sp = 0, n = 0; lab[c] = ++nl; stk[sp++] = c;
                while (sp) {
                    int e = stk[--sp], ex = e % GW, ey = e / GW; n++;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int nx = ex + dx, ny = ey + dy;
                        if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || lab[ny * GW + nx] != -1) continue;
                        lab[ny * GW + nx] = nl; stk[sp++] = ny * GW + nx;
                    }
                }
                if (n > bestn) { bestn = n; best = nl; }
            }
            ax = ay = 1e30f; bx = by = -1e30f;
            for (size_t i = 0; i < nr; i++)
                if (lab[(int)((ys[i] - gy0) / 256.0f) * GW + (int)((xs[i] - gx0) / 256.0f)] == best) {
                    ax = fminf(ax, xs[i]); bx = fmaxf(bx, xs[i]); ay = fminf(ay, ys[i]); by = fmaxf(by, ys[i]);
                }
        }
        free(lab); free(stk);
        float size = fmaxf(bx - ax, by - ay) * 1.06f;
        size = ceilf(size / 64.0f) * 64.0f;
        float x0 = (ax + bx) * 0.5f - size * 0.5f, y0 = (ay + by) * 0.5f + size * 0.5f, px = size / W;
        const uint8_t land[3] = { 41, 41, 41 }, road[3] = { 139, 138, 139 };
        for (int i = 0; i < W * W; i++) { memcpy(img + 4 * (size_t)i, land, 3);   /* Palmont is land all round (collision is mostly roads) */
            img[4 * (size_t)i + 3] = 255; }
        for (size_t i = 0; i < g_mmn; i++) if (!is_road_key(g_mm[i].key)) mm_fill(img, W, g_mm[i].xy, x0, y0, px, land);
        for (size_t i = 0; i < g_mmn; i++) if (is_road_key(g_mm[i].key)) mm_fill(img, W, g_mm[i].xy, x0, y0, px, road);
        const float zoom = 1.5f * size / 5633.0f;   /* Bayview's radar reach (TRACKMAP4000: 5633 m, 1.5) */
        printf("xp_minimap carbon: %zu ground triangles (%zu road), %.0f m from (%.0f, %.0f)\n", g_mmn, nr, size, x0, y0);
        FILE *o = fopen(out_path, "wb");
        if (!o) rc = XP_ERR_OPEN_OUT;
        else {
            uint32_t hd[3] = { 0, (uint32_t)W, (uint32_t)W }; memcpy(hd, "BMAP", 4);
            float fl[4] = { x0, y0, size, zoom };
            int ok = fwrite(hd, 4, 3, o) == 3 && fwrite(fl, 4, 4, o) == 4 && fwrite(img, 4, (size_t)W * W, o) == (size_t)W * W;
            if (fclose(o) != 0 || !ok) { rc = XP_ERR_WRITE; remove(out_path); }
        }
    } else {
        printf("xp_minimap carbon: %zu ground triangles, %zu road -- keys:", g_mmn, nr);
        for (size_t i = 0; i < g_mmn && i < 200000; i += 20000) printf(" %08x", g_mm[i].key);
        printf("\n");
    }
    free(xs); free(ys); free(img);
    free(g_mm); g_mm = NULL; g_mmn = g_mmcap = 0;
    return rc;
}
int xp_minimap(const char *in_path, const char *out_path, int game)
{
    if (game == XP_GAME_CARBON) return minimap_carbon(in_path, out_path);
    if (game != XP_GAME_MW) return XP_ERR_FORMAT;
    const float x0 = -1222.4f, y0 = 5063.4f, size = 6656.0f;
    const float zoom = 1.5f * 6656.0f / 5633.0f;              /* Bayview's radar reach (TRACKMAP4000: 5633 m, 1.5) */
    FILE *f = fopen(in_path, "rb"); if (!f) return XP_ERR_OPEN_IN;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return d ? XP_ERR_READ : XP_ERR_MEMORY; }
    fclose(f);
    const int W = 1024;
    uint8_t *img = calloc((size_t)W * W, 4), *u = malloc(1 << 16);
    if (!img || !u) { free(d); free(img); free(u); return XP_ERR_MEMORY; }
    int tiles = 0;
    for (long q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > n) break;
        int un = id == 0x3a100 ? jdlz(d + q + 8, (int)sz, u, 1 << 16) : 0;
        const uint8_t *t4 = un ? find_id(u, (size_t)un, 0x33310004) : NULL;
        const uint8_t *px = un ? find_id(u, (size_t)un, 0x33320002) : NULL;
        if (t4 && px && !memcmp(t4 + 8 + 12, "MINI_MAP_CHOP", 13)) {
            int k = atoi((const char *)t4 + 8 + 25);
            uint32_t pn = rd32(px + 4); const uint8_t *b = px + 8;
            while (pn && *b == 0x11) { b++; pn--; }
            if (k >= 0 && k < 64 && pn >= 128 * 128 && px + 8 + rd32(px + 4) <= u + un) {
                int tx = (k % 8) * 128, ty = (k / 8) * 128;
                for (int by = 0; by < 32; by++)
                    for (int bx = 0; bx < 32; bx++, b += 16) {      /* DXT3 */
                        uint16_t c0 = (uint16_t)(b[8] | b[9] << 8), c1 = (uint16_t)(b[10] | b[11] << 8);
                        uint32_t bits = rd32(b + 12);
                        int pal[4][3];
                        int cs[2] = { c0, c1 };
                        for (int c = 0; c < 2; c++) {
                            pal[c][0] = (cs[c] >> 11 & 31) * 255 / 31;
                            pal[c][1] = (cs[c] >> 5 & 63) * 255 / 63;
                            pal[c][2] = (cs[c] & 31) * 255 / 31;
                        }
                        for (int c = 0; c < 3; c++) {
                            pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
                            pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
                        }
                        for (int p = 0; p < 16; p++) {
                            uint8_t *o = img + ((size_t)(ty + by * 4 + p / 4) * W + tx + bx * 4 + p % 4) * 4;
                            const int *c = pal[bits >> (2 * p) & 3];
                            o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2];
                            o[3] = 255;                         /* drawn by colour, as UG2's TRACKMAPs */
                        }
                    }
                tiles++;
            }
        }
        q += 8 + sz;
    }
    free(d); free(u);
    int rc = XP_OK;
    if (tiles != 64) rc = XP_ERR_FORMAT;
    else {
        FILE *o = fopen(out_path, "wb");
        if (!o) rc = XP_ERR_OPEN_OUT;
        else {
            uint32_t hd[3] = { 0, (uint32_t)W, (uint32_t)W }; memcpy(hd, "BMAP", 4);
            float fl[4] = { x0, y0, size, zoom };
            int ok = fwrite(hd, 4, 3, o) == 3 && fwrite(fl, 4, 4, o) == 4 && fwrite(img, 4, (size_t)W * W, o) == (size_t)W * W;
            if (fclose(o) != 0 || !ok) { rc = XP_ERR_WRITE; remove(out_path); }
        }
    }
    free(img);
    return rc;
}

long long xp_file_size(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END);
#ifdef _WIN32
    long long n = _ftelli64(f);
#else
    long long n = ftell(f);
#endif
    fclose(f);
    return n;
}
int xp_marker_write(const char *marker_path, int game, long long source_size)
{
    FILE *f = fopen(marker_path, "w"); if (!f) return 0;
    int ok = fprintf(f, "xpconv %d %d %lld\n", XP_VERSION, game, source_size) > 0;
    return fclose(f) == 0 && ok;
}
int xp_marker_ok(const char *marker_path, int game, long long source_size)
{
    FILE *f = fopen(marker_path, "r"); if (!f) return 0;
    int v = 0, g = 0; long long n = -1;
    int got = fscanf(f, "xpconv %d %d %lld", &v, &g, &n);
    fclose(f);
    return got == 3 && v == XP_VERSION && g == game && (source_size < 0 || n == source_size);
}

/* ---- the companion stream (L2RA.BUN): MW's scenery groups -> UG2's ----
   MW 0x34108: 6-byte placements {u16 section (letter*100+n), u16 instance row, u16 flags}.
   MW 0x34109: groups {u32 11, u32 11, u32 name hash, u16 id, u16 count, u32 flags,
     u16 placement[count], pad to 4}; id 1 = the always-on default, the rest named
     BARRIER_SPLINE_<n> (race barriers: race_bin field 0xe244f26b lists a race's own).
   UG2 (world_group_reader.h): 0x34107 8-byte placements {section, row, flags, groups
     using it}; 0x34108 groups {+8 name[32], +40 hash, +48 count, u16 placement[count],
     pad to 4}. Collision (wcoll.c): 0x3412f, 32-byte records {hash, +9 name}, indexed
     by the MW group id the collision polygons carry (xpconv conv_carp). Everything
     else is copied. */
static uint32_t xp_strhash(const char *s) { uint32_t h = 0xffffffffu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }
static void xp_group_name(uint32_t hash, int id, char *out, size_t n)
{
    /* MW has _07 next to _7xx; Carbon numbers up to 80xx */
    enum { NK = 10000 + 10 + 100 };
    static uint32_t kh[NK]; static int kdone;
    if (!kdone) {
        char t[40]; int m = 0;
        for (int k = 0; k < 10000; k++) { snprintf(t, sizeof t, "BARRIER_SPLINE_%d", k); kh[m++] = xp_strhash(t); }
        for (int k = 0; k < 10; k++) { snprintf(t, sizeof t, "BARRIER_SPLINE_%02d", k); kh[m++] = xp_strhash(t); }
        for (int k = 0; k < 100; k++) { snprintf(t, sizeof t, "BARRIER_SPLINE_%03d", k); kh[m++] = xp_strhash(t); }
        kdone = 1;
    }
    for (int m = 0; m < NK; m++)
        if (kh[m] == hash) {
            if (m < 10000) snprintf(out, n, "BARRIER_SPLINE_%d", m);
            else if (m < 10010) snprintf(out, n, "BARRIER_SPLINE_%02d", m - 10000);
            else snprintf(out, n, "BARRIER_SPLINE_%03d", m - 10010);
            return;
        }
    snprintf(out, n, "XPGROUP_%d", id);   /* not a race barrier: stays as it is (shown, never solid) */
}
/* Carbon's track-section boundaries (0x34152 in 0x80034150): MW's record (+8 s16 section,
   +10 u8 points, +0xc bbox, +0x1c 2 floats, +0x24 points) with 12 zero bytes more at +0xc
   -> MW/UG2's layout, the rest of 0x80034150 as it is */
static void put_sections_carbon(Buf *o, const uint8_t *p, uint32_t n)
{
    size_t top = open_chunk(o, 0x80034150);
    for (uint32_t q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(p + q), sz = rd32(p + q + 4);
        if (q + 8 + sz > n) break;
        if (id != 0x34152) { put(o, p + q, 8 + sz); q += 8 + sz; continue; }
        const uint8_t *t = p + q + 8;
        size_t c = open_chunk(o, 0x34152);
        for (uint32_t r = 0; r + 4 <= sz; ) {
            uint32_t w = rd32(t + r);
            if (w == 0 || w == 0x11111111u) { put(o, t + r, 4); r += 4; continue; }
            uint32_t k = r + 0x30 <= sz ? t[r + 10] : 0;
            if (!k || r + 0x30 + 8 * k > sz) { put(o, t + r, sz - r); break; }
            put(o, t + r, 0xc); put(o, t + r + 0x18, 0x18 + 8 * k);
            r += 0x30 + 8 * k;
        }
        close_chunk(o, c);
        q += 8 + sz;
    }
    close_chunk(o, top);
}
int xp_companion(const char *in_path, const char *out_path, int game)
{
    if (game != XP_GAME_MW && game != XP_GAME_CARBON) return XP_ERR_FORMAT;
    FILE *f = fopen(in_path, "rb"); if (!f) return XP_ERR_OPEN_IN;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return d ? XP_ERR_READ : XP_ERR_MEMORY; }
    fclose(f);
    const uint8_t *pl = NULL, *gr = NULL; uint32_t pln = 0, grn = 0;
    for (long q = 0; q + 8 <= n; ) {
        uint32_t id = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > n) break;
        if (id == 0x34108) { pl = d + q + 8; pln = sz; }
        if (id == 0x34109) { gr = d + q + 8; grn = sz; }
        q += 8 + (long)sz;
    }
    Buf o = {0}; g_oom = 0;
    int rc = XP_OK;
    if (pl && gr) {
        uint32_t np = pln / 6;
        uint16_t *refs = calloc(np ? np : 1, 2);
        int maxid = 0;
        if (!refs) { free(d); return XP_ERR_MEMORY; }
        for (uint32_t p = 0; p + 20 <= grn; ) {                    /* pass 1: references, ids */
            if (!rd32(gr + p)) { p += 4; continue; }                 /* Carbon: records 8-aligned */
            uint32_t cnt = (uint32_t)(gr[p + 14] | gr[p + 15] << 8); int gid = gr[p + 12] | gr[p + 13] << 8;
            if (p + 20 + 2 * cnt > grn) { rc = XP_ERR_FORMAT; break; }
            for (uint32_t j = 0; j < cnt; j++) { unsigned m = gr[p + 20 + 2*j] | gr[p + 21 + 2*j] << 8; if (m < np) refs[m]++; }
            if (gid > maxid) maxid = gid;
            p = (p + 20 + 2 * cnt + 3) & ~3u;
        }
        /* the rest of the file first, then the three tables */
        for (long q = 0; rc == XP_OK && q + 8 <= n; ) {
            uint32_t id = rd32(d + q), sz = rd32(d + q + 4);
            if (q + 8 + (long)sz > n) break;
            if (id == 0x80034150 && game == XP_GAME_CARBON) put_sections_carbon(&o, d + q + 8, sz);
            else if (id != 0x34108 && id != 0x34109) put(&o, d + q, 8 + sz);
            q += 8 + (long)sz;
        }
        size_t c = open_chunk(&o, 0x34107);
        for (uint32_t k = 0; k < np; k++) {
            uint16_t r[4]; memcpy(r, pl + 6 * k, 6); r[3] = refs[k]; put(&o, r, 8);
        }
        close_chunk(&o, c);
        uint8_t (*rec)[32] = calloc((size_t)maxid + 1, 32);
        if (!rec) { free(refs); free(d); free(o.p); return XP_ERR_MEMORY; }
        c = open_chunk(&o, 0x34108);
        for (uint32_t p = 0; rc == XP_OK && p + 20 <= grn; ) {
            if (!rd32(gr + p)) { p += 4; continue; }
            uint32_t cnt = (uint32_t)(gr[p + 14] | gr[p + 15] << 8); int gid = gr[p + 12] | gr[p + 13] << 8;
            char nm[32]; xp_group_name(rd32(gr + p + 8), gid, nm, sizeof nm);
            uint8_t h[52]; memset(h, 0, sizeof h);
            memcpy(h + 8, nm, strlen(nm)); uint32_t hv = xp_strhash(nm); memcpy(h + 40, &hv, 4); memcpy(h + 48, &cnt, 4);
            put(&o, h, 52);
            uint32_t kept = 0;
            for (uint32_t j = 0; j < cnt; j++) {
                uint16_t m = (uint16_t)(gr[p + 20 + 2*j] | gr[p + 21 + 2*j] << 8);
                if (m < np) { put(&o, &m, 2); kept++; }
            }
            if (kept != cnt) memcpy(o.p + o.n - 2 * kept - 4, &kept, 4);
            while (o.n & 3) put(&o, "\0", 1);
            memcpy(rec[gid], &hv, 4); memcpy(rec[gid] + 9, nm, strlen(nm) < 23 ? strlen(nm) : 22);
            p = (p + 20 + 2 * cnt + 3) & ~3u;
        }
        close_chunk(&o, c);
        c = open_chunk(&o, 0x3412f);
        memcpy(rec[0] + 9, "NONE", 4);                               /* index 0: never switched */
        put(&o, rec, ((size_t)maxid + 1) * 32);
        close_chunk(&o, c);
        free(rec); free(refs);
    } else put(&o, d, (size_t)n);                                    /* no groups: as it is */
    free(d);
    if (g_oom) { free(o.p); return XP_ERR_MEMORY; }
    if (rc != XP_OK) { free(o.p); return rc; }
    FILE *w = fopen(out_path, "wb");
    if (!w) { free(o.p); return XP_ERR_OPEN_OUT; }
    int ok = fwrite(o.p, 1, o.n, w) == o.n;
    ok &= fclose(w) == 0;
    free(o.p);
    return ok ? XP_OK : XP_ERR_WRITE;
}
