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
        else if (id == 0x134b02) { size_t f = skip_fill(d, sz); grp = d + f; ngrp = (sz - f) / 0x68; }
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
        const uint8_t *r = grp + g * 0x68;
        eff[g] = rd32(r + 0x30);
        if (g > 0 && eff[g] != eff[g - 1]) s++;
        if (s >= nvb) { st.bad++; return; }
        gs[g] = s; gv[g] = rd32(r + 0x3c); gt[g] = rd32(r + 0x40);
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
            for (uint32_t i = 0; i < svert[k]; i++) put(o, vb[k] + (size_t)i * stride[k], 36);
        close_chunk(o, c); }
    /* UG2 records: +0 bmin, +12 index count, +16 bmax, +28 texture slot, +32 matid,
       +52 index start, +56 flags */
    {   size_t c = open_chunk(o, 0x134b02); align_fill(o, 0x10);
        uint32_t start = 0;
        for (size_t g = 0; g < ngrp; g++) {
            const uint8_t *r = grp + g * 0x68;
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
static void conv_insts(Buf *o, const uint8_t *d, size_t sz)
{
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
            t.surf = mw_surface(si < nsf ? rd32(sf + 4 * si) : 0, 0);
            t.flags = nz / l < 0.25f ? 0x08 : 0x01;          /* steep: UG2's 0x8 wall triangles; ground: layered */
            t.grp = grp;
            cp_add(&t); st.ctris++;
        }
    }
    const uint8_t *b = a + 16 + ssz;
    for (int i = 0; i < nb && 32 * (i + 1) <= bsz; i++) {
        const uint8_t *r = b + 32 * i;
        float p0[3], p1[3], w0[3], w1[3];
        memcpy(p0, r, 12); memcpy(p1, r + 16, 12);
        for (int k = 0; k < 3; k++) { p0[k] -= ip[k]; p1[k] -= ip[k]; }
        mw_world(p0, w0); mw_world(p1, w1);
        float zlo = w0[2] < w1[2] ? w0[2] : w1[2], zhi = w0[2] < w1[2] ? w1[2] : w0[2];
        if (zhi - zlo > 250.0f) zlo = zhi - 250.0f;         /* packed heights reach +-128 m from the middle */
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
        /* MW scenery groups (race barriers, ...): kept, under a group number
           nothing switches on until the events map them */
        bake_article(g_art[ai], g_artn[ai], ip, grp ? 255 : 0);
        if (grp) st.cgrouped++;
    }
    if (g_oom) return;
    if (!g_ncp) { st.cempty++; return; }
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
        u[0] = (uint8_t)p->grp; u[1] = (uint8_t)(ix0 | iy0 << 2 | ix1 << 4 | iy1 << 6);
        u[2] = (uint8_t)p->surf; u[3] = (uint8_t)(p->flags | (p->nv == 4 ? 0x10 : 0));
        memcpy(u + 4, &zb, 2); u[6] = u[7] = 0xff;
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
    else if (is_walked(id)) { size_t c = open_chunk(o, id); walk(o, d, sz); close_chunk(o, c); }
    else put(o, raw, 8 + sz);
}

/* One top-level chunk in memory at a time (MW's largest 7.8 MB, Carbon's 13.8 MB),
   so the same code runs on the Vita. Top-level chunks keep their input offset
   mod 0x80 (an id-0 padding chunk in front), as the loaders expect of TPKs. */
int xp_convert(const char *in_path, const char *out_path, int game,
               XpProgress progress, void *ud, XpStats *out_stats)
{
    (void)game;
    memset(&st, 0, sizeof st); g_oom = 0;
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
    if (out_stats) *out_stats = st;
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
