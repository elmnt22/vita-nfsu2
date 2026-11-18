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
                 (60 B), 0x134b03 rebased indices. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
