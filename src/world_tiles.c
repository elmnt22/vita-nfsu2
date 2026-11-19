#include "world_tiles.h"
#include "world.h"
#include "world_instance.h"
#include "nfsu2.h"
#include "smokeable.h"
#include "beyond.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __vita__
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#ifndef OPENUG2_SIM_PRE_H   /* (the Windows simulator stands in for a few sceIo calls only) */
#include <psp2/kernel/threadmgr.h>
#endif
#include <dirent.h>
#include "SDL.h"
void vita_mark(const char *fmt, ...);
#define VMARK(...) vita_mark(__VA_ARGS__)
static void wt_mkdir(const char *p) { sceIoMkdir(p, 0777); }
static unsigned wt_ms(void) { return SDL_GetTicks(); }
static void wt_sleep(unsigned ms) { SDL_Delay(ms); }
#else
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#define VMARK(...) ((void)0)
static void wt_mkdir(const char *p) { mkdir(p, 0777); }
static unsigned wt_ms(void) { return (unsigned)(clock() * 1000.0 / CLOCKS_PER_SEC); }
static void wt_sleep(unsigned ms) { (void)ms; }
#endif
static int wt_remove(const char *p) { return remove(p) == 0; }
static int wt_rename(const char *a, const char *b) { return rename(a, b) == 0; }
static void wt_lock(volatile int *l) { while (__sync_lock_test_and_set(l, 1)) wt_sleep(1); }
static void wt_unlock(volatile int *l) { __sync_lock_release(l); }

/* ---- file layout, version 3 (little endian, as written by the console) ----
 *   tile  u32 "UG2T", version, tile_x, tile_y, mesh_count, payload size,
 *         stored size, flags (1 = payload LZ4-compressed), then the stored
 *         bytes. The payload is, per mesh, a WTileMesh and its streams:
 *           (every 32-bit plane is stored as four byte lanes)
 *           positions  3 planes of i32 deltas of round(p * 1024) (1 mm grid;
 *                      shared vertices stay identical, so seams cannot open)
 *           uv         2 planes of i32 deltas of round(uv * 16384)
 *           normals    nverts*3 f32 (only with authored_normals)
 *           indices    u16 deltas
 *           prelight   4 planes of u8 deltas (only with has_vcol)
 *         WTM_RAWPOS / WTM_RAWUV store plain f32 planes instead when a value
 *         would not fit the grid.
 *   index u32 "UG2I", version, f32 tile size, u32 tile count, f32 bounds[4],
 *         then per tile WtIndexEntry (with the union box of its meshes).
 *   sky + vista meshes are track-wide: one "global" tile (x = y = -32768). */
typedef struct {
    uint32_t nverts, nidx, texkey;
    uint8_t cat, mat_exact, scen, world_role;
    uint8_t inst, authored_normals, has_vcol, scen_has;
    uint16_t scen_region, scen_index;
    uint8_t terrain_biased, vrepair, draw_mode, flags;
    float world_bounds[6];
    char sname[32];
    char aname[28];
} WTileMesh;
#define WTM_VISTA  1
#define WTM_RAWPOS 2
#define WTM_RAWUV  4
#define WTM_REFLECT 8   /* N2Mesh.reflect: the original's wet-road reflection slice */
#define WT_POS_Q   1024.0f
#define WT_UV_Q    16384.0f

typedef struct { int16_t x, y; uint32_t meshes, bytes, tris; float bb[4]; } WtIndexEntry;
typedef struct { int16_t x, y; uint32_t meshes, bytes, tris; } WtIndexEntryV2;   /* older sets */

typedef struct { unsigned char *p; size_t n, cap; long meshes, tris; float bb[4]; } WtBuf;
static long long g_wt_reflect_meshes;   /* conversion: meshes carrying the reflection flag */

/* converting: the world loader must build the old way and must not cache
   "no tile set" (it did, and the session after a conversion never used it) */
static volatile int g_wt_converting;
static int g_wt_enabled = -1;

/* ---- pack: one file per track ---------------------------------------------
 *   u32 "UG2P", WTILE_VERSION, entry count, table offset; then the tile
 *   files' bytes unchanged, back to back; then the table, count x WtPackEnt
 *   (offsets from the start of the pack). The card pays ~270 ms per file open
 *   in a 2400-file directory (10-17: a track switch read 55 tiles, 3.2 MB, in
 *   14.8 s), so a track's tiles are read through one handle instead. A tile
 *   the pack lacks still comes from its loose <track>_x_y.bin. Written as
 *   .pak.tmp and renamed when complete: a .pak on the card is always whole. */
typedef struct { int16_t x, y; uint32_t off, len; } WtPackEnt;
#define WTPACK_MAGIC 0x50324755u   /* "UG2P" */
#define WTPACK_HDR   16u
/* the tile set version a track's files carry: the World Expansion's come from the
   BeyondDev converter and keep its version (the city bumps -- conversion fixes -- do
   not touch them, the format is the same) */
static int wt_ver(const char *track) { return beyond_is_track(track) ? WTILE_BEYOND_VERSION : WTILE_VERSION; }
static void wt_pack_path(char *out, size_t cap, const char *track, int tmp) {
    snprintf(out, cap, "%s/%.63s_v%d.pak%s", WTILE_DIR, track, wt_ver(track), tmp ? ".tmp" : "");
}

typedef struct { WtPackEnt *e; int n; } WtPackTab;
static WtPackTab *wt_pack_probe(const char *track, int force);

typedef struct { FILE *f; WtPackEnt *e; int n, cap; uint32_t pos; } WtPackW;
static int wt_pw_open(WtPackW *w, const char *track) {
    char path[256]; wt_pack_path(path, sizeof path, track, 1);
    memset(w, 0, sizeof *w);
    uint32_t hdr[4] = { WTPACK_MAGIC, (uint32_t)wt_ver(track), 0, 0 };   /* count + table offset at the end */
    if (!(w->f = fopen(path, "wb"))) return 0;
    if (fwrite(hdr, sizeof hdr, 1, w->f) != 1) { fclose(w->f); w->f = NULL; wt_remove(path); return 0; }
    w->pos = WTPACK_HDR;
    return 1;
}
static int wt_pw_put(WtPackW *w, const void *p, size_t n) {
    if (!n) return 1;
    if (fwrite(p, n, 1, w->f) != 1) return 0;
    w->pos += (uint32_t)n;
    return 1;
}
/* the bytes put since `off` are tile (tx, ty) */
static int wt_pw_entry(WtPackW *w, int tx, int ty, uint32_t off) {
    if (w->n == w->cap) {
        int cap = w->cap ? w->cap * 2 : 1024;
        WtPackEnt *g = (WtPackEnt *)realloc(w->e, (size_t)cap * sizeof *g);
        if (!g) return 0;
        w->e = g; w->cap = cap;
    }
    w->e[w->n++] = (WtPackEnt){ (int16_t)tx, (int16_t)ty, off, w->pos - off };
    return 1;
}
/* ok: table + header, then .tmp -> .pak; else the .tmp goes */
static int wt_pw_finish(WtPackW *w, const char *track, int ok) {
    char tmp[256], fin[256];
    wt_pack_path(tmp, sizeof tmp, track, 1); wt_pack_path(fin, sizeof fin, track, 0);
    if (!w->f) ok = 0;
    if (ok) {
        uint32_t hdr[4] = { WTPACK_MAGIC, (uint32_t)wt_ver(track), (uint32_t)w->n, w->pos };
        ok = (!w->n || fwrite(w->e, sizeof *w->e, (size_t)w->n, w->f) == (size_t)w->n) &&
             fflush(w->f) == 0 && fseek(w->f, 0, SEEK_SET) == 0 && fwrite(hdr, sizeof hdr, 1, w->f) == 1;
    }
    if (w->f && fclose(w->f) != 0) ok = 0;
    w->f = NULL;
    free(w->e); w->e = NULL; w->n = w->cap = 0;
    if (ok) { wt_remove(fin); ok = wt_rename(tmp, fin); }
    if (!ok) wt_remove(tmp);
    return ok;
}

/* ---- LZ4 block format (own implementation; the converter verifies every
   tile by decompressing it again, and stores it raw if anything differs) -- */
/* PERF_PLAN P2 (11-15): hash chains + lazy matching instead of one greedy
   probe. Same LZ4 block format, so every decoder and every older cache is
   unchanged; tools/tilepak_bench.c on STREAML4RA: 63.4 -> 49.1 MB at depth
   16 (the Vita's own conversion), 47.8 MB at 64 (the Prep Tool). The card
   reads ~10 MB/s while streaming (hwlog67), so a smaller pack is faster
   reads. Conversion runs one tile at a time on one thread. */
#if defined(__vita__) && !defined(OPENUG2_SIM_PRE_H)   /* the console; the simulator + Prep Tool: 64 */
#define WT_LZ4_DEPTH 16
#else
#define WT_LZ4_DEPTH 64
#endif
#define WT_LZ4_HBITS 16
static int32_t *g_lz_head, *g_lz_prev;   /* prev: the last 64 KB of positions */
static int lz4_find(const uint8_t *s, int ip, int mlimit, int *off) {
    uint32_t seq; memcpy(&seq, s + ip, 4);
    int best = 0, bo = 0, depth = WT_LZ4_DEPTH;
    for (int ref = g_lz_head[(seq * 2654435761u) >> (32 - WT_LZ4_HBITS)];
         ref >= 0 && ip - ref <= 65535 && depth-- > 0; ref = g_lz_prev[ref & 0xffff]) {
        if (s[ref + best] != s[ip + best]) continue;
        uint32_t rs; memcpy(&rs, s + ref, 4);
        if (rs != seq) continue;
        int ml = 4;
        while (ip + ml < mlimit && s[ref + ml] == s[ip + ml]) ml++;
        if (ml > best) { best = ml; bo = ip - ref; if (ip + ml >= mlimit) break; }
    }
    *off = bo;
    return best;
}
static void lz4_insert(const uint8_t *s, int ip) {
    uint32_t seq; memcpy(&seq, s + ip, 4);
    uint32_t h = (seq * 2654435761u) >> (32 - WT_LZ4_HBITS);
    g_lz_prev[ip & 0xffff] = g_lz_head[h]; g_lz_head[h] = ip;
}
/* one sequence: lit literals then (ml >= 4) a match at off; ml 0 = the last */
static int lz4_emit(uint8_t *dst, int op, int cap, const uint8_t *lits, int lit, int off, int ml) {
    if (op + 1 + lit + lit / 255 + 1 + 2 + (ml ? (ml - 4) / 255 + 1 : 0) > cap) return -1;
    uint8_t *tok = dst + op++;
    int ln = lit >= 15 ? 15 : lit, mn = ml ? (ml - 4 >= 15 ? 15 : ml - 4) : 0;
    *tok = (uint8_t)(ln << 4 | mn);
    if (lit >= 15) { int r = lit - 15; while (r >= 255) { dst[op++] = 255; r -= 255; } dst[op++] = (uint8_t)r; }
    memcpy(dst + op, lits, (size_t)lit); op += lit;
    if (!ml) return op;
    dst[op++] = (uint8_t)(off & 255); dst[op++] = (uint8_t)(off >> 8);
    if (ml - 4 >= 15) { int r = ml - 4 - 15; while (r >= 255) { dst[op++] = 255; r -= 255; } dst[op++] = (uint8_t)r; }
    return op;
}
static int lz4_compress(const uint8_t *src, int n, uint8_t *dst, int cap) {
    if (!g_lz_head) {
        g_lz_head = (int32_t *)malloc(sizeof(int32_t) << WT_LZ4_HBITS);
        g_lz_prev = (int32_t *)malloc(sizeof(int32_t) << 16);
        if (!g_lz_head || !g_lz_prev) { free(g_lz_head); free(g_lz_prev); g_lz_head = g_lz_prev = NULL; return 0; }
    }
    for (int i = 0; i < (1 << WT_LZ4_HBITS); i++) g_lz_head[i] = -1;
    int ip = 0, anchor = 0, op = 0;
    const int mflimit = n - 12, mlimit = n - 5;   /* the tail stays literal */
    while (ip < mflimit) {
        int off, ml = lz4_find(src, ip, mlimit, &off);
        if (ml < 4) { lz4_insert(src, ip); ip++; continue; }
        lz4_insert(src, ip);
        if (ip + 1 < mflimit) {   /* lazy: a longer match one byte on wins */
            int off2, ml2 = lz4_find(src, ip + 1, mlimit, &off2);
            if (ml2 > ml + 1) { ip++; ml = ml2; off = off2; }
        }
        op = lz4_emit(dst, op, cap, src + anchor, ip - anchor, off, ml);
        if (op < 0) return 0;
        for (int k = 1; k < ml && ip + k < mflimit; k++) lz4_insert(src, ip + k);
        ip += ml; anchor = ip;
    }
    op = lz4_emit(dst, op, cap, src + anchor, n - anchor, 0, 0);
    return op < 0 ? 0 : op;
}

static int lz4_decompress(const uint8_t *src, int n, uint8_t *dst, int outn) {
    int ip = 0, op = 0;
    while (ip < n) {
        int tok = src[ip++], lit = tok >> 4;
        if (lit == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; lit += b; } while (b == 255); }
        if (lit > n - ip || lit > outn - op) return 0;
        memcpy(dst + op, src + ip, (size_t)lit); ip += lit; op += lit;
        if (ip >= n) break;   /* the last sequence has no match */
        if (n - ip < 2) return 0;
        int off = src[ip] | src[ip + 1] << 8; ip += 2;
        if (off == 0 || off > op) return 0;
        int ml = (tok & 15) + 4;
        if ((tok & 15) == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; ml += b; } while (b == 255); }
        if (ml > outn - op) return 0;
        /* 11-15: word copies (PC bench: 2.3x faster than a byte loop); an
           overlapping match copies 8 bytes at a time only when each chunk's
           source is already written (off >= 8) */
        uint8_t *d = dst + op; const uint8_t *m = d - off;
        if (off >= ml) memcpy(d, m, (size_t)ml);
        else if (off == 1) memset(d, m[0], (size_t)ml);
        else if (off >= 8) {
            int k = 0;
            for (; k + 8 <= ml; k += 8) memcpy(d + k, m + k, 8);
            for (; k < ml; k++) d[k] = m[k];
        } else for (int k = 0; k < ml; k++) d[k] = m[k];   /* may overlap */
        op += ml;
    }
    return op == outn;
}

/* ---- payload encoding ---------------------------------------------------- */
static int wt_put(WtBuf *b, const void *src, size_t n) {
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 64 * 1024;
        while (cap < b->n + n) cap *= 2;
        unsigned char *p = (unsigned char *)realloc(b->p, cap);
        if (!p) return 0;
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, src, n); b->n += n;
    return 1;
}

static int wt_fits(const float *v, int stride, int count, int comp, float q) {
    for (int i = 0; i < count; i++) {
        float x = v[i * stride + comp] * q;
        if (!(x > -2.0e9f && x < 2.0e9f)) return 0;   /* also rejects NaN */
    }
    return 1;
}

/* A plane of 32-bit words written as four byte lanes (all low bytes, then
   all second bytes, ...): small deltas leave long zero runs for LZ4. */
static int wt_put_lanes(WtBuf *b, const uint32_t *w, int count) {
    size_t need = (size_t)count * 4;
    if (b->n + need > b->cap) {   /* grow once, then write in place */
        size_t cap = b->cap ? b->cap : 64 * 1024;
        while (cap < b->n + need) cap *= 2;
        unsigned char *p = (unsigned char *)realloc(b->p, cap);
        if (!p) return 0;
        b->p = p; b->cap = cap;
    }
    unsigned char *o = b->p + b->n;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < count; i++) *o++ = (unsigned char)(w[i] >> (8 * k));
    b->n += need;
    return 1;
}
/* one plane of i32 deltas of round(v[i*stride+comp] * q), or raw f32 bits */
static int wt_put_plane(WtBuf *b, const float *v, int stride, int count, int comp, float q, int quantize) {
    uint32_t *w = (uint32_t *)malloc((size_t)(count ? count : 1) * 4);
    if (!w) return 0;
    int32_t prev = 0;
    for (int i = 0; i < count; i++) {
        if (quantize) {
            int32_t x = (int32_t)lrintf(v[i * stride + comp] * q);
            w[i] = (uint32_t)x - (uint32_t)prev; prev = x;
        } else memcpy(&w[i], &v[i * stride + comp], 4);
    }
    int ok = wt_put_lanes(b, w, count);
    free(w);
    return ok;
}

static int wt_put_mesh(WtBuf *b, const N2Mesh *m, int vista) {
    if (!m->verts || !m->idx || m->nverts <= 0 || m->nidx <= 0) return 1;   /* nothing to keep */
    WTileMesh h;
    memset(&h, 0, sizeof h);
    int nv = m->nverts;
    h.nverts = (uint32_t)nv; h.nidx = (uint32_t)m->nidx; h.texkey = m->texkey;
    h.cat = (uint8_t)m->cat; h.mat_exact = m->mat_exact; h.scen = m->scen; h.world_role = m->world_role;
    h.inst = m->inst; h.authored_normals = m->authored_normals; h.has_vcol = m->vcol != NULL;
    h.scen_has = m->scen_has; h.scen_region = m->scen_region; h.scen_index = m->scen_index;
    h.terrain_biased = m->terrain_biased; h.vrepair = m->vrepair; h.draw_mode = m->draw_mode;
    h.flags = (vista ? WTM_VISTA : 0) | (m->reflect ? WTM_REFLECT : 0);
    int pos_ok = 1, uv_ok = 1;
    for (int c = 0; c < 3; c++) pos_ok &= wt_fits(m->verts, 5, nv, c, WT_POS_Q);
    for (int c = 3; c < 5; c++) uv_ok &= wt_fits(m->verts, 5, nv, c, WT_UV_Q);
    if (!pos_ok) h.flags |= WTM_RAWPOS;
    if (!uv_ok) h.flags |= WTM_RAWUV;
    memcpy(h.world_bounds, m->world_bounds, sizeof h.world_bounds);
    if (!m->world_bounds_valid) {   /* compute it: the loader relies on it */
        float *bb = h.world_bounds;
        bb[0] = bb[1] = bb[2] = 1e30f; bb[3] = bb[4] = bb[5] = -1e30f;
        for (int v = 0; v < nv; v++)
            for (int c = 0; c < 3; c++) {
                float x = m->verts[v*5 + c];
                if (x < bb[c]) bb[c] = x;
                if (x > bb[c+3]) bb[c+3] = x;
            }
    }
    memcpy(h.sname, m->sname, sizeof h.sname);
    memcpy(h.aname, m->aname, sizeof h.aname);
    if (!wt_put(b, &h, sizeof h)) return 0;
    for (int c = 0; c < 3; c++)
        if (!wt_put_plane(b, m->verts, 5, nv, c, WT_POS_Q, pos_ok)) return 0;
    for (int c = 3; c < 5; c++)
        if (!wt_put_plane(b, m->verts, 5, nv, c, WT_UV_Q, uv_ok)) return 0;
    if (m->authored_normals && !wt_put(b, m->verts + (size_t)nv * 5, (size_t)nv * 3 * sizeof(float))) return 0;
    uint16_t prev = 0;
    for (int i = 0; i < m->nidx; i++) {
        uint16_t d = (uint16_t)(m->idx[i] - prev); prev = m->idx[i];
        if (!wt_put(b, &d, 2)) return 0;
    }
    if (m->vcol)
        for (int c = 0; c < 4; c++) {
            uint8_t p8 = 0;
            for (int v = 0; v < nv; v++) {
                uint8_t d = (uint8_t)(m->vcol[v*4 + c] - p8); p8 = m->vcol[v*4 + c];
                if (!wt_put(b, &d, 1)) return 0;
            }
        }
    const float *wb = h.world_bounds;
    if (!b->meshes) { b->bb[0] = wb[0]; b->bb[1] = wb[1]; b->bb[2] = wb[3]; b->bb[3] = wb[4]; }
    else {
        if (wb[0] < b->bb[0]) b->bb[0] = wb[0];
        if (wb[1] < b->bb[1]) b->bb[1] = wb[1];
        if (wb[3] > b->bb[2]) b->bb[2] = wb[3];
        if (wb[4] > b->bb[3]) b->bb[3] = wb[4];
    }
    b->meshes++; b->tris += m->nidx / 3;
    if (m->reflect) g_wt_reflect_meshes++;
    return 1;
}

/* bytes of one mesh's streams in the payload / once decoded */
static size_t wt_enc_size(const WTileMesh *h) {
    size_t nv = h->nverts;
    return nv * 5 * 4 + (h->authored_normals ? nv * 3 * 4 : 0) + (size_t)h->nidx * 2 + (h->has_vcol ? nv * 4 : 0);
}
static size_t wt_dec_size(const WTileMesh *h) {
    size_t nv = h->nverts;
    return nv * (h->authored_normals ? 8 : 5) * 4 + (((size_t)h->nidx * 2 + 3) & ~(size_t)3) +
           (h->has_vcol ? nv * 4 : 0);
}

static void wt_mesh_centre(const N2Mesh *m, float *x, float *y) {
    if (m->world_bounds_valid) {
        *x = 0.5f * (m->world_bounds[0] + m->world_bounds[3]);
        *y = 0.5f * (m->world_bounds[1] + m->world_bounds[4]);
        return;
    }
    float mn[2] = {1e30f, 1e30f}, mx[2] = {-1e30f, -1e30f};
    for (int v = 0; v < m->nverts; v++)
        for (int c = 0; c < 2; c++) {
            float p = m->verts[v*5 + c];
            if (p < mn[c]) mn[c] = p;
            if (p > mx[c]) mx[c] = p;
        }
    *x = 0.5f * (mn[0] + mx[0]); *y = 0.5f * (mn[1] + mx[1]);
}

static long long g_wt_raw_total, g_wt_stored_total;

/* ---- ready tiles (PERF_PLAN P2, 11-15) ------------------------------------
 * A tile can instead hold its meshes in the decoded layout (what wt_decode
 * builds: f32 verts [+ normals], u16 indices, prelight, decoded bounds), so
 * a load is one LZ4 pass into the block the meshes live in. Tile header
 * version WTILE_READY_VERSION (older decoders refuse the tile instead of
 * misreading it). Payload: u32 "RDY1", mesh count, header offset, pad to
 * 16, the geometry, then every WTileMesh (world_bounds = the decoded ones):
 * the headers last, so a full load trims them off its block in place.
 * Written when readytiles.txt / OPENUG2_READYTILES is present at
 * conversion: the converter builds the v6 payload, decodes it with the
 * loader's own decoder and stores that, so a ready tile loads into exactly
 * the meshes the v6 tile does. */
#define WTILE_READY_VERSION (0x100 | WTILE_VERSION)
#define WT_READY_MAGIC 0x31594452u   /* "RDY1" */
static int wt_ready_fmt(void) {
    static int on = -1;
    if (on < 0) {
        FILE *f = fopen("ux0:data/NFSU2/readytiles.txt", "rb");
        on = f != NULL || getenv("OPENUG2_READYTILES") != NULL;
        if (f) fclose(f);
        if (on) printf("tiles: writing ready tiles (decoded layout)\n");
    }
    return on;
}
static int wt_ready_payload(const WtBuf *b, int tx, int ty, unsigned char **outp, size_t *outn);

/* one tile file's bytes (header + stored payload) appended to the pack */
static int wt_write_tile(WtPackW *pw, int tx, int ty, const WtBuf *b0, size_t *stored_out) {
    WtBuf rb; const WtBuf *b = b0;
    unsigned char *rp = NULL; size_t rn = 0;
    if (wt_ready_fmt() && b0->n && wt_ready_payload(b0, tx, ty, &rp, &rn)) {
        rb = *b0; rb.p = rp; rb.n = rn; rb.cap = rn; b = &rb;
    }
    /* compress, then prove it by decompressing; raw if it does not pay or fails */
    int cap = (int)(b->n + b->n / 255 + 64);
    uint8_t *z = b->n ? (uint8_t *)malloc((size_t)cap) : NULL;
    int zn = z ? lz4_compress(b->p, (int)b->n, z, cap) : 0;
    if (zn > 0 && (size_t)zn < b->n) {
        uint8_t *chk = (uint8_t *)malloc(b->n);
        int good = chk && lz4_decompress(z, zn, chk, (int)b->n) && !memcmp(chk, b->p, b->n);
        free(chk);
        if (!good) { printf("tiles: compression check failed for (%d,%d); stored raw\n", tx, ty); zn = 0; }
    } else zn = 0;
    uint32_t off = pw->pos;
    uint32_t hdr[8] = { 0x54324755u /* "UG2T" */, rp ? WTILE_READY_VERSION : WTILE_VERSION, (uint32_t)tx, (uint32_t)ty,
                        (uint32_t)b->meshes, (uint32_t)b->n, (uint32_t)(zn ? zn : (int)b->n), zn ? 1u : 0u };
    const void *body = zn ? (const void *)z : (const void *)b->p;
    size_t bodyn = zn ? (size_t)zn : b->n;
    int ok = wt_pw_put(pw, hdr, sizeof hdr) && wt_pw_put(pw, body, bodyn) && wt_pw_entry(pw, tx, ty, off);
    free(z); free(rp);
    g_wt_raw_total += (long long)b->n; g_wt_stored_total += (long long)bodyn;
    if (stored_out) *stored_out = bodyn + sizeof hdr;
    return ok;
}

static void wt_index_path_v(char *out, size_t cap, const char *track, int version) {
    snprintf(out, cap, "%s/%s_v%d.idx", WTILE_DIR, track, version);
}
static void wt_index_path(char *out, size_t cap, const char *track) {
    wt_index_path_v(out, cap, track, wt_ver(track));
}

int world_tiles_ready(const char *trackname) {
    char path[256]; wt_index_path(path, sizeof path, trackname);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t hdr[2] = {0};
    int ok = fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == 0x49324755u && hdr[1] == (uint32_t)wt_ver(trackname);
    fclose(f);
    return ok;
}

/* A previous tile set (any older version) already says which cells hold
   anything: mark those, so a reconversion skips the open sea. */
static int wt_mark_from_old_index(const char *track, int cx0, int cy0, int ncols, int nrows,
                                  unsigned char *want) {
    for (int v = WTILE_VERSION - 1; v >= 1; v--) {
        char path[256]; wt_index_path_v(path, sizeof path, track, v);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        uint32_t hdr[2], count = 0; float size = 0, bb[4];
        int ok = fread(hdr, 4, 2, f) == 2 && hdr[0] == 0x49324755u && hdr[1] == (uint32_t)v &&
                 fread(&size, 4, 1, f) == 1 && size == WTILE_SIZE && fread(&count, 4, 1, f) == 1 &&
                 count > 0 && count < 100000 && fread(bb, sizeof bb, 1, f) == 1;
        int marked = 0;
        for (uint32_t i = 0; ok && i < count; i++) {
            WtIndexEntryV2 e; WtIndexEntry e3;   /* version 3 entries carry a box too */
            if (v >= 3) { if (fread(&e3, sizeof e3, 1, f) != 1) { ok = 0; break; } e.x = e3.x; e.y = e3.y; }
            else if (fread(&e, sizeof e, 1, f) != 1) { ok = 0; break; }
            if (e.x == -32768) continue;
            int cx = (int)floorf((float)e.x / WTILE_CELL), cy = (int)floorf((float)e.y / WTILE_CELL);
            int c = cx - cx0, r = cy - cy0;
            if (c >= 0 && r >= 0 && c < ncols && r < nrows && !want[r * ncols + c]) { want[r * ncols + c] = 1; marked++; }
        }
        fclose(f);
        if (ok && marked) { printf("tiles: %d cells known from the version %d tile set\n", marked, v); return marked; }
        memset(want, 0, (size_t)(ncols * nrows));
    }
    return 0;
}

int world_tiles_convert(const char *troot, const char *trackname, WTileProgress *p) {
    WTileProgress dummy;
    if (!p) p = &dummy;
    memset((void *)p, 0, sizeof *p);
    p->start_ms = p->now_ms = wt_ms();
    wt_mkdir("ux0:data/NFSU2/cache");
    wt_mkdir(WTILE_DIR);
    float bb[4];
    if (!world_instance_bounds(troot, trackname, bb)) {
        printf("tiles: no district bounds for %s\n", trackname);
        p->done = 1; return 0;
    }
    const float cell = WTILE_SIZE * WTILE_CELL;
    int cx0 = (int)floorf(bb[0] / cell), cy0 = (int)floorf(bb[1] / cell);
    int cx1 = (int)floorf(bb[2] / cell), cy1 = (int)floorf(bb[3] / cell);
    int ncols = cx1 - cx0 + 1, nrows = cy1 - cy0 + 1;
    unsigned char *want = (unsigned char *)calloc((size_t)(ncols * nrows), 1);
    if (!want) { p->done = 1; return 0; }
    int known = wt_mark_from_old_index(trackname, cx0, cy0, ncols, nrows, want);
    if (known) p->cells_total = known;
    else {   /* first conversion: every cell some district box comes near */
        float (*boxes)[4] = NULL; int nboxes = 0;
        world_instance_district_boxes(troot, trackname, &boxes, &nboxes);
        const float reach = cell * 0.7072f + 8.0f;
        for (int r = 0; r < nrows; r++) for (int c = 0; c < ncols; c++) {
            float fx = (cx0 + c + 0.5f) * cell, fy = (cy0 + r + 0.5f) * cell;
            int hit = nboxes == 0;
            for (int i = 0; i < nboxes && !hit; i++)
                hit = fx > boxes[i][0] - reach && fx < boxes[i][2] + reach &&
                      fy > boxes[i][1] - reach && fy < boxes[i][3] + reach;
            want[r * ncols + c] = (unsigned char)hit;
            p->cells_total += hit;
        }
        free(boxes);
    }
    {   /* test aid: OPENUG2_CONVCELL="x y" converts only the cell holding that point */
        const char *cc = getenv("OPENUG2_CONVCELL"); float qx, qy;
        if (cc && sscanf(cc, "%f %f", &qx, &qy) == 2) {
            int qc = (int)floorf(qx / cell) - cx0, qr = (int)floorf(qy / cell) - cy0;
            memset(want, 0, (size_t)(ncols * nrows)); p->cells_total = 0;
            if (qc >= 0 && qr >= 0 && qc < ncols && qr < nrows) { want[qr * ncols + qc] = 1; p->cells_total = 1; }
        }
    }
    printf("tiles: converting %s, bounds (%.0f %.0f)-(%.0f %.0f), %d cells of %.0f m\n",
           trackname, bb[0], bb[1], bb[2], bb[3], p->cells_total, cell);
    VMARK("tiles: conversion start, %d cells", p->cells_total);

    int icap = 1024, icount = 0;
    WtIndexEntry *index = (WtIndexEntry *)malloc((size_t)icap * sizeof *index);
    WtBuf tiles[WTILE_CELL * WTILE_CELL];
    WtBuf global;
    memset(tiles, 0, sizeof tiles); memset(&global, 0, sizeof global);
    int have_global = 0, ok = index != NULL;
    WtPackW pw;
    if (!wt_pw_open(&pw, trackname)) { printf("tiles: cannot write the %s pack\n", trackname); ok = 0; }
    g_wt_raw_total = g_wt_stored_total = g_wt_reflect_meshes = 0;
    world_instance_set_convert(1);
    /* the translated cities only: Bayview's tiles stay byte-identical */
    world_instance_set_compact(strstr(trackname, "L5RA") || strstr(trackname, "L2RA"));
    g_wt_converting = 1;
    /* a cell needs every mesh whose centre lies inside it; any such mesh's
       box reaches within the cell's half diagonal of the centre */
    const float radius = cell * 0.7072f + 8.0f;

    for (int cy = cy0; ok && cy <= cy1; cy++)
    for (int cx = cx0; ok && cx <= cx1; cx++) {
        if (!want[(cy - cy0) * ncols + (cx - cx0)]) continue;
        float fx = (cx + 0.5f) * cell, fy = (cy + 0.5f) * cell;
        unsigned t0 = wt_ms();
        WorldNeighborhood nb;
        memset(&nb, 0, sizeof nb);
        WLoadOptions opt = { 1, fx, fy, radius, 0 };
        int nm = world_neighborhood_load(&nb, troot, trackname, &opt);
        if (nm <= 0) {   /* open water / outside every district */
            world_neighborhood_free(&nb);
            p->cells_failed++; p->cells_done++; p->now_ms = wt_ms();
            continue;
        }
        for (int i = 0; ok && i < nb.scene.count; i++) {
            const N2Mesh *m = &nb.scene.meshes[i];
            if (m->cat == N2_SKY) {   /* track-wide */
                if (!have_global) ok = wt_put_mesh(&global, m, 0);
                continue;
            }
            float mx, my; wt_mesh_centre(m, &mx, &my);
            int tx = (int)floorf(mx / WTILE_SIZE), ty = (int)floorf(my / WTILE_SIZE);
            int lx = tx - cx * WTILE_CELL, ly = ty - cy * WTILE_CELL;
            {   static const char *cf = (const char *)1; if (cf == (const char *)1) cf = getenv("OPENUG2_CONVFIND");
                if (cf && strstr(m->sname, cf)) printf("convfind: cell (%d %d) %s cat=%d role=%d centre (%.0f %.0f) tile (%d %d)%s\n",
                    cx, cy, m->sname, m->cat, m->world_role, mx, my, tx, ty,
                    (lx < 0 || ly < 0 || lx >= WTILE_CELL || ly >= WTILE_CELL) ? " another cell's" : ""); }
            if (lx < 0 || ly < 0 || lx >= WTILE_CELL || ly >= WTILE_CELL) continue;   /* another cell's */
            ok = wt_put_mesh(&tiles[ly * WTILE_CELL + lx], m, 0);
        }
        if (ok && !have_global) {
            for (int i = 0; ok && i < nb.vista.count; i++) ok = wt_put_mesh(&global, &nb.vista.meshes[i], 1);
            size_t stored = 0;
            if (ok) ok = wt_write_tile(&pw, -32768, -32768, &global, &stored);
            if (ok && icount < icap) {
                index[icount++] = (WtIndexEntry){ -32768, -32768, (uint32_t)global.meshes,
                                                  (uint32_t)global.n, (uint32_t)global.tris,
                                                  { global.bb[0], global.bb[1], global.bb[2], global.bb[3] } };
                p->bytes += (long long)stored;
            }
            have_global = 1;
            free(global.p); memset(&global, 0, sizeof global);
        }
        int written = 0; long cell_meshes = 0; size_t cell_bytes = 0;
        for (int k = 0; ok && k < WTILE_CELL * WTILE_CELL; k++) {
            WtBuf *b = &tiles[k];
            if (b->meshes) {
                int tx = cx * WTILE_CELL + k % WTILE_CELL, ty = cy * WTILE_CELL + k / WTILE_CELL;
                size_t stored = 0;
                ok = wt_write_tile(&pw, tx, ty, b, &stored);
                if (ok && icount == icap) {
                    WtIndexEntry *g = (WtIndexEntry *)realloc(index, (size_t)icap * 2 * sizeof *index);
                    if (g) { index = g; icap *= 2; } else ok = 0;
                }
                if (ok) {
                    index[icount++] = (WtIndexEntry){ (int16_t)tx, (int16_t)ty, (uint32_t)b->meshes,
                                                      (uint32_t)b->n, (uint32_t)b->tris,
                                                      { b->bb[0], b->bb[1], b->bb[2], b->bb[3] } };
                    written++; cell_meshes += b->meshes; cell_bytes += stored;
                    p->meshes += b->meshes; p->tris += b->tris; p->bytes += (long long)stored;
                }
            }
            b->n = 0; b->meshes = b->tris = 0;   /* keep the buffer for the next cell */
        }
        world_neighborhood_free(&nb);
        p->tiles_written += written;
        p->cells_done++; p->now_ms = wt_ms();
        printf("tiles: cell (%d,%d) %d tiles, %ld meshes, %u KB stored in %u ms (%d/%d)\n",
               cx, cy, written, cell_meshes, (unsigned)(cell_bytes >> 10), wt_ms() - t0,
               p->cells_done, p->cells_total);
    }
    world_instance_set_convert(0); world_instance_set_compact(0);
    g_wt_converting = 0;
    g_wt_enabled = -1;   /* decide again now that the tile set exists */
#ifdef __vita__
    {   /* the conversion filled the placement cache; tile loading never uses it */
        extern void world_instance_cache_flush(void);
        world_instance_cache_flush();
    }
#endif
    free(want);
    for (int k = 0; k < WTILE_CELL * WTILE_CELL; k++) free(tiles[k].p);
    free(global.p);

    /* the pack whole on the card before the index says the set exists */
    if (!wt_pw_finish(&pw, trackname, ok)) ok = 0;
    if (ok) wt_pack_probe(trackname, 1);
    if (ok) {   /* the index goes last: its presence means the set is complete */
        char path[256]; wt_index_path(path, sizeof path, trackname);
        FILE *f = fopen(path, "wb");
        uint32_t hdr[3] = { 0x49324755u /* "UG2I" */, (uint32_t)wt_ver(trackname), 0 };
        float size = WTILE_SIZE;
        hdr[2] = (uint32_t)icount;
        ok = f && fwrite(hdr, 4, 2, f) == 2 && fwrite(&size, 4, 1, f) == 1 &&
             fwrite(&hdr[2], 4, 1, f) == 1 && fwrite(bb, sizeof bb, 1, f) == 1 &&
             fwrite(index, sizeof *index, (size_t)icount, f) == (size_t)icount;
        if (f && fclose(f) != 0) ok = 0;
    }
    free(index);
    p->now_ms = wt_ms();
    printf("tiles: %s -- %d tiles, %lld meshes, %lld triangles, %lld MB stored (%lld MB before "
           "compression), %lld road-reflection meshes, %d of %d cells empty, %.1f s\n",
           ok ? "done" : "FAILED", p->tiles_written, (long long)p->meshes, (long long)p->tris,
           (long long)(p->bytes >> 20), (long long)(g_wt_raw_total >> 20), g_wt_reflect_meshes, p->cells_failed, p->cells_total,
           (p->now_ms - p->start_ms) / 1000.0);
    VMARK("tiles: conversion %s, %d tiles, %lld MB", ok ? "done" : "FAILED",
          p->tiles_written, (long long)(p->bytes >> 20));
    p->ok = ok;
    p->done = 1;
    return ok;
}

/* ======================================================================
 * Tile loader: assembles a neighbourhood scene from the prebuilt tiles.
 * Loaded tiles stay in memory while they are near; their meshes are shared
 * (reference counted) with every resident that uses them, so an area two
 * builds overlap is read, decoded and packed once. Loader thread only,
 * except the read-ahead below.
 * ====================================================================== */
#include "world_scenery.h"

/* ground: only the tile's road + terrain meshes were decoded (an edge tile:
   centre beyond the object radius, so only its ground is placed) */
typedef struct { int tx, ty; N2Mesh *m; int n; size_t bytes; int ground; } WtLoaded;
static WtLoaded *g_wt_loaded; static int g_wt_nloaded, g_wt_capload;
static WtLoaded g_wt_global; static int g_wt_have_global;
/* the World Expansion (beyond.h) has no sky of its own: the city's dome and
   cap from STREAML4RA's global tile (1 read, -1 none on the card) */
static WtLoaded g_wt_donor; static int g_wt_have_donor;
static WtIndexEntry *g_wt_index; static int g_wt_nindex;
static char g_wt_track[64];
static long long g_wt_bytes_read;

/* a track's index (any thread; the caller frees *out) */
static int wt_index_read(const char *track, WtIndexEntry **out, int *nout) {
    *out = NULL; *nout = 0;
    char path[256]; wt_index_path(path, sizeof path, track);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t hdr[2], count = 0; float size = 0, bb[4];
    WtIndexEntry *ix = NULL;
    int ok = fread(hdr, 4, 2, f) == 2 && hdr[0] == 0x49324755u && hdr[1] == (uint32_t)wt_ver(track) &&
             fread(&size, 4, 1, f) == 1 && size == WTILE_SIZE && fread(&count, 4, 1, f) == 1 &&
             count < 100000 && fread(bb, sizeof bb, 1, f) == 1;
    if (ok && (ix = (WtIndexEntry *)malloc((count ? count : 1) * sizeof *ix)))
        ok = fread(ix, sizeof *ix, count, f) == count;
    else ok = 0;
    fclose(f);
    if (!ok) { free(ix); return 0; }
    *out = ix; *nout = (int)count;
    return 1;
}

/* ---- pack registry + reads -------------------------------------------------
   Tables are loaded once per track and never freed (~10 KB each), so a
   reader may hold one without a lock. "No pack" is remembered too (a probe is
   a file open) and asked again at the next track switch; the migration
   publishes a new pack itself. Readers count themselves in
   g_wt_loose_inflight before they look, so the migration (publish, then wait
   for zero, then delete) never pulls a loose file from under a reader. */
static struct { char track[64]; WtPackTab *volatile tab; volatile int used, probed; } g_wt_pk[16];
static volatile int g_wt_pk_lock;
static volatile int g_wt_loose_inflight;
static volatile unsigned g_wt_last_io_ms;   /* the streamer's last card read: the migration keeps off */

static WtPackTab *wt_pack_load(const char *track) {
    char path[256]; wt_pack_path(path, sizeof path, track, 0);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint32_t hdr[4];
    long size = 0;
    WtPackTab *t = NULL;
    int ok = fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == WTPACK_MAGIC && hdr[1] == (uint32_t)wt_ver(track) &&
             hdr[2] < 100000 && hdr[3] >= WTPACK_HDR &&
             fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 &&
             (unsigned long)size == hdr[3] + (unsigned long)hdr[2] * sizeof(WtPackEnt);   /* whole */
    if (ok && (t = (WtPackTab *)calloc(1, sizeof *t)) &&
        (t->e = (WtPackEnt *)malloc((hdr[2] ? hdr[2] : 1) * sizeof *t->e)))
        ok = fseek(f, (long)hdr[3], SEEK_SET) == 0 && fread(t->e, sizeof *t->e, hdr[2], f) == hdr[2];
    else ok = 0;
    fclose(f);
    for (uint32_t i = 0; ok && i < hdr[2]; i++)
        ok = t->e[i].off >= WTPACK_HDR && t->e[i].len >= 32 && t->e[i].off + t->e[i].len <= hdr[3] &&
             t->e[i].off + t->e[i].len > t->e[i].off;
    if (!ok) {
        if (t) free(t->e);
        free(t);
        printf("tiles pack: %s unusable, ignored\n", path);
        return NULL;
    }
    t->n = (int)hdr[2];
    return t;
}
static WtPackTab *wt_pack_get(const char *track) {
    for (int i = 0; i < 16; i++)
        if (g_wt_pk[i].used && !strcmp(g_wt_pk[i].track, track)) return g_wt_pk[i].tab;
    return NULL;
}
/* force: load again even after "no pack" (and replace a table: the old one
   leaks, a reader may still hold it) */
static WtPackTab *wt_pack_probe(const char *track, int force) {
    wt_lock(&g_wt_pk_lock);
    int s = -1;
    for (int i = 0; i < 16 && s < 0; i++)
        if (g_wt_pk[i].used && !strcmp(g_wt_pk[i].track, track)) s = i;
    for (int i = 0; i < 16 && s < 0; i++)
        if (!g_wt_pk[i].used) {
            s = i;
            snprintf(g_wt_pk[i].track, sizeof g_wt_pk[i].track, "%s", track);
            __sync_synchronize(); g_wt_pk[i].used = 1;
        }
    WtPackTab *t = s >= 0 ? g_wt_pk[s].tab : NULL;
    if (s >= 0 && (force || (!t && !g_wt_pk[s].probed))) {
        WtPackTab *n = wt_pack_load(track);
        g_wt_pk[s].probed = 1;
        if (n) { __sync_synchronize(); g_wt_pk[s].tab = t = n; }
        __sync_synchronize();
    }
    wt_unlock(&g_wt_pk_lock);
    return t;
}
static const WtPackEnt *wt_pack_find(const WtPackTab *t, int tx, int ty) {
    for (int i = 0; t && i < t->n; i++)
        if (t->e[i].x == tx && t->e[i].y == ty) return &t->e[i];
    return NULL;
}

/* positioned reads: one handle per track and reader, no shared file offset */
#ifdef __vita__
typedef SceUID WtFile;
#define WT_NOFILE (-1)
static WtFile wt_open_r(const char *p) { SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0); return fd < 0 ? WT_NOFILE : fd; }
static void wt_close(WtFile f) { sceIoClose(f); }
static int wt_pread(WtFile f, void *buf, long n, long off) {
    long got = 0;
    while (got < n) {
        long want = n - got > (4L << 20) ? (4L << 20) : n - got;
        int k = sceIoPread(f, (char *)buf + got, (SceSize)want, (SceOff)(off + got));
        if (k <= 0) return 0;
        got += k;
    }
    return 1;
}
/* loose files: newlib's read() as before (fread trickles through a small
   stdio buffer on the Vita) */
typedef int WtLFile;
#define WT_NOLFILE (-1)
static WtLFile wt_lopen(const char *p, long *n) {
    int fd = open(p, O_RDONLY);
    if (fd < 0) return WT_NOLFILE;
    *n = (long)lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (*n < 0) { close(fd); return WT_NOLFILE; }
    return fd;
}
static int wt_lread(WtLFile fd, void *buf, long n) {   /* the next n bytes */
    long got = 0;
    while (got < n) {
        long want = n - got > (4L << 20) ? (4L << 20) : n - got;
        long k = (long)read(fd, (char *)buf + got, (size_t)want);
        if (k <= 0) return 0;
        got += k;
    }
    return 1;
}
static void wt_lclose(WtLFile fd) { close(fd); }
#else
typedef FILE *WtFile;
#define WT_NOFILE NULL
static WtFile wt_open_r(const char *p) { return fopen(p, "rb"); }
static void wt_close(WtFile f) { fclose(f); }
static int wt_pread(WtFile f, void *buf, long n, long off) {
    return fseek(f, off, SEEK_SET) == 0 && (n == 0 || fread(buf, (size_t)n, 1, f) == 1);
}
typedef FILE *WtLFile;
#define WT_NOLFILE NULL
static WtLFile wt_lopen(const char *p, long *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (*n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    return f;
}
static int wt_lread(WtLFile f, void *buf, long n) { return n == 0 || fread(buf, (size_t)n, 1, f) == 1; }
static void wt_lclose(WtLFile f) { fclose(f); }
#endif

/* a pack handle per reader: [read-ahead 0 / loader 1 / preload 2] */
typedef struct { char track[64]; WtFile f; volatile int busy; } WtPackFd;
static WtPackFd g_wt_pfd[3] = { { "", WT_NOFILE, 0 }, { "", WT_NOFILE, 0 }, { "", WT_NOFILE, 0 } };

/* where one tile's stored bytes are: in the pack or a loose file */
typedef struct { WtFile f; WtLFile lf; int own, packed, counted; WtPackFd *slot; long off, len; } WtSrc;
static int wt_src_open(const char *track, int tx, int ty, int slot, WtSrc *s) {
    memset(s, 0, sizeof *s); s->f = WT_NOFILE; s->lf = WT_NOLFILE;
    __sync_fetch_and_add(&g_wt_loose_inflight, 1);   /* (a full barrier) before the look */
    s->counted = 1;
    const WtPackEnt *e = wt_pack_find(wt_pack_get(track), tx, ty);
    if (e) {
        WtPackFd *pf = &g_wt_pfd[slot];
        if (__sync_bool_compare_and_swap(&pf->busy, 0, 1)) {
            if (pf->f == WT_NOFILE || strcmp(pf->track, track)) {
                if (pf->f != WT_NOFILE) wt_close(pf->f);
                char path[256]; wt_pack_path(path, sizeof path, track, 0);
                pf->f = wt_open_r(path);
                snprintf(pf->track, sizeof pf->track, "%s", track);
            }
            if (pf->f != WT_NOFILE) { s->f = pf->f; s->slot = pf; }
            else { __sync_synchronize(); pf->busy = 0; }
        } else {   /* a second reader of this kind (not expected): a handle of its own */
            char path[256]; wt_pack_path(path, sizeof path, track, 0);
            if ((s->f = wt_open_r(path)) != WT_NOFILE) s->own = 1;
        }
        if (s->f != WT_NOFILE) {
            s->packed = 1; s->off = (long)e->off; s->len = (long)e->len;
            __sync_fetch_and_sub(&g_wt_loose_inflight, 1); s->counted = 0;
            return 1;
        }
    }
    char path[256];
    snprintf(path, sizeof path, "%s/%s_%d_%d.bin", WTILE_DIR, track, tx, ty);
    s->f = WT_NOFILE;
    if ((s->lf = wt_lopen(path, &s->len)) == WT_NOLFILE) {
        __sync_fetch_and_sub(&g_wt_loose_inflight, 1);
        return 0;
    }
    return 1;
}
static int wt_src_read(WtSrc *s, void *buf) {
    return s->packed ? wt_pread(s->f, buf, s->len, s->off) : wt_lread(s->lf, buf, s->len);
}
static void wt_src_close(WtSrc *s) {
    if (s->own) wt_close(s->f);
    if (s->lf != WT_NOLFILE) wt_lclose(s->lf);
    s->lf = WT_NOLFILE;
    if (s->slot) { __sync_synchronize(); s->slot->busy = 0; }
    if (s->counted) __sync_fetch_and_sub(&g_wt_loose_inflight, 1);
    s->f = WT_NOFILE; s->own = 0; s->slot = NULL; s->counted = 0;
    g_wt_last_io_ms = wt_ms();
}

#ifdef OPENUG2_SIM_PRE_H
static int g_tilestat_tex[4];
static int wt_read_decode(const char *track, int tx, int ty, WtLoaded *out, int ground);
static void wt_release(WtLoaded *t);
static int wt_ground_cat(int cat);
#endif
static int wt_index_load(const char *track) {
    if (g_wt_index && !strcmp(g_wt_track, track)) return 1;
    free(g_wt_index); g_wt_index = NULL; g_wt_nindex = 0;
    if (!wt_index_read(track, &g_wt_index, &g_wt_nindex)) return 0;
    snprintf(g_wt_track, sizeof g_wt_track, "%s", track);
    WtPackTab *t = wt_pack_probe(track, 0);
    printf("world tiles: %s index %d tiles, %s\n", track, g_wt_nindex,
           t ? "pack" : "loose files (no pack yet)");
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_TILESTAT -- the whole set's geometry by role and,
           for the far stand-ins, by height (for an always-resident skyline estimate) */
        static int done;
        if (!done && getenv("OPENUG2_TILESTAT")) {
            done = 1;
            static const float HB[] = { 10, 15, 20, 25, 30, 40, 60, 1e9f };
            enum { NB = 8 };
            long long rv[3] = {0}, rb[3] = {0}, rm[3] = {0}, hv[NB] = {0}, hb[NB] = {0}, hm[NB] = {0};
            long long texs = 0; int tiles_si = 0;
            for (int i = 0; i < g_wt_nindex; i++) {
                WtLoaded L;
                if (!wt_read_decode(track, g_wt_index[i].x, g_wt_index[i].y, &L, 0)) continue;
                int had = 0;
                for (int k = 0; k < L.n; k++) {
                    const N2Mesh *m = &L.m[k];
                    if (wt_ground_cat(m->cat)) continue;
                    int r = m->world_role > 2 ? 0 : m->world_role;
                    long long b = (long long)m->nverts * 28 + (long long)m->nidx * 2;
                    rv[r] += m->nverts; rb[r] += b; rm[r]++;
                    if (r == 2 && m->world_bounds_valid) {
                        float h = m->world_bounds[5] - m->world_bounds[2];
                        int q = 0; while (q < NB - 1 && h >= HB[q]) q++;
                        hv[q] += m->nverts; hb[q] += b; hm[q]++; had = 1;
                        for (int c = 0; c < 4; c++) {   /* distinct textures of the >= 20/25/30/40 m sets */
                            static const float th[4] = { 20, 25, 30, 40 };
                            static uint32_t tk[4][8192]; static int ntk[4];
                            if (h < th[c]) continue;
                            int f = 0; for (int z = 0; z < ntk[c] && !f; z++) f = tk[c][z] == m->texkey;
                            if (!f && ntk[c] < 8192) tk[c][ntk[c]++] = m->texkey;
                            g_tilestat_tex[c] = ntk[c];
                        }
                    }
                }
                tiles_si += had; (void)texs;
                wt_release(&L);
            }
            static const char *rn[3] = { "drawn", "collision", "stand-in" };
            for (int r = 0; r < 3; r++)
                printf("tilestat %s: role %s %lld meshes, %lld verts, %lld KB (GPU layout)\n", track, rn[r], rm[r], rv[r], rb[r] >> 10);
            for (int q = 0; q < NB; q++)
                printf("tilestat %s: stand-ins height %s%.0f m: %lld meshes, %lld verts, %lld KB\n", track,
                       q ? ">=" : "<", (double)(q ? HB[q-1] : HB[0]), hm[q], hv[q], hb[q] >> 10);
            printf("tilestat %s: %d of %d tiles have stand-ins\n", track, tiles_si, g_wt_nindex);
            printf("tilestat %s: distinct stand-in textures >=20 m %d, >=25 m %d, >=30 m %d, >=40 m %d\n", track,
                   g_tilestat_tex[0], g_tilestat_tex[1], g_tilestat_tex[2], g_tilestat_tex[3]);
        }
    }
#endif
    return 1;
}

static int g_wt_off_file;   /* notileload.txt: no preload either */
int world_tiles_enabled(const char *track) {
#ifdef __vita__
    if (g_wt_converting) return 0;
    if (g_wt_enabled < 0) {
        FILE *off = fopen("ux0:data/NFSU2/notileload.txt", "r");
        if (off) { fclose(off); g_wt_off_file = 1; }
        g_wt_enabled = !off && wt_index_load(track);
        printf("world tiles: %s\n", g_wt_enabled ? "loading the world from prebuilt tiles"
                                                 : off ? "off (notileload.txt)" : "no tile set; building as before");
        if (g_wt_enabled) {
            extern void res_pin_strip(void);
            extern void world_instance_library_release(void);
            extern void world_instance_cache_flush(void);
            world_instance_library_release();
            world_instance_cache_flush();
            res_pin_strip();
        }
    }
    return g_wt_enabled > 0 && wt_index_load(track);
#else
    (void)track; return 0;
#endif
}

static int wt_index_of(int tx, int ty) {
    for (int i = 0; i < g_wt_nindex; i++)
        if (g_wt_index[i].x == tx && g_wt_index[i].y == ty) return i;
    return -1;
}

static float wt_rect_d2(float x0, float y0, float x1, float y1, float fx, float fy) {
    float dx = fx < x0 ? x0 - fx : fx > x1 ? fx - x1 : 0.0f;
    float dy = fy < y0 ? y0 - fy : fy > y1 ? fy - y1 : 0.0f;
    return dx*dx + dy*dy;
}
/* distance from (fx, fy) to the union box of a tile's meshes */
static float wt_tile_d2(const WtIndexEntry *e, float fx, float fy) {
    return wt_rect_d2(e->bb[0], e->bb[1], e->bb[2], e->bb[3], fx, fy);
}

static void wt_release(WtLoaded *t) {
    for (int i = 0; i < t->n; i++) n2_mesh_geometry_release(&t->m[i]);
    free(t->m); t->m = NULL; t->n = 0; t->bytes = 0;
}

/* Per-thread scratch for a tile's file bytes and its LZ4 payload: both die
   with the decode, and at ~1 MB each, allocating them per tile (twice per
   tile, every few seconds of driving) carved the heap into holes too small
   for the next tile once the kernel blocks ran out (10-11: OOM in wt_decode
   after ~50 builds). Grown, never shrunk; one set for the loader thread, one
   for the read-ahead thread. */
typedef struct { unsigned char *p; size_t cap; volatile int busy; } WtScratch;
static WtScratch g_wt_scr[2][2];   /* [loader 1 / read-ahead 0][file, payload] */
static unsigned char *wt_scratch(WtScratch *s, size_t n) {
    if (n <= s->cap) return s->p;
    size_t cap = (n + (n >> 2) + 0xFFFF) & ~(size_t)0xFFFF;   /* headroom: grow rarely */
    free(s->p);
    s->p = (unsigned char *)malloc(cap);
    s->cap = s->p ? cap : 0;
    return s->p;
}

/* Decodes one tile file into meshes whose geometry lives in one block per
   tile (N2GeomBlock), freed when the last mesh using it is released. Runs on
   the loader or the read-ahead thread; touches no shared state. `scr`, when
   given, holds the decompressed payload. ground_only: road + terrain meshes
   only -- an edge tile contributes nothing else, and its buildings were half
   of all kept tile memory downtown (10-13: 37 edge tiles 40 MB, ground 5). */
static int wt_ground_cat(int cat) { return cat == N2_ROAD || cat == N2_TERRAIN; }
/* What a tile without its buildings keeps (WtLoaded.ground): its ground, and
   its far LOD stand-ins (world role 2) -- the far ring and the edge tiles
   draw those out to WORLD_STANDIN_GATE (main.c) so the city does not end
   where the full buildings do. Stand-ins are ~20% of a tile's geometry. */
#define WT_STANDIN_MIN_H 10.0f   /* shorter stand-ins (walls, kiosks) stay with the full tiles: 8.9 -> 3.5 MB a ring */
#define WT_GROUND_ONLY 1   /* WtLoaded.ground: ground + stand-ins (an edge tile) */
#define WT_FAR_ONLY    2   /*                  stand-ins only (the far ring, past the ground reach) */
static int wt_reduced_keep(int mode, int cat, int role, const float *wb) {
    int g = wt_ground_cat(cat);
    int sa = !g && role == 2 && wb[5] - wb[2] >= WT_STANDIN_MIN_H;
    return mode == WT_FAR_ONLY ? sa : g || sa;
}
/* The far ring: tiles this far past the ground reach contribute their
   stand-ins only. ux0:data/NFSU2/farring.txt = metres (0 = off). */
#define WT_FAR_EXTRA_M 192.0f   /* 11-14: 320 m tried (run G: heap 110/115 MB, GPU hang after 18 min) -- back to 192 */
static float wt_far_extra(void) {
    static float v = -1.0f;
    if (v < 0.0f) {
        v = WT_FAR_EXTRA_M;
        FILE *f = fopen("ux0:data/NFSU2/farring.txt", "r");
        if (f) { float x; if (fscanf(f, "%f", &x) == 1 && x >= 0.0f && x <= 512.0f) v = x; fclose(f); }
        printf("world tiles: far stand-in ring %.0f m past the ground reach\n", v);
    }
    return v;
}
/* decoded tile geometry alive anywhere (the cache, the read-ahead, residents
   still drawing or retiring): PERF_PLAN P0.2 census */
static volatile long long g_wt_geo_live;
/* a ready tile (see wt_ready_payload): one LZ4 pass straight into the
   block the meshes live in; a reduced (ground / far) load copies only the
   meshes it keeps out of the scratch */
/* geometry the source game never shows: Carbon's reflection blockers (all-black
   vertex colours, ~190 m up: the "black blocks" over Palmont 11-19) */
static int wt_never_drawn(const WTileMesh *h) {
    return strstr(h->sname, "REFBLOCKER") != NULL || strstr(h->sname, "_REFLECTIONS") != NULL;
}
static int wt_decode_ready(unsigned char *data, long len, const uint32_t *hdr, int tx, int ty,
                           WtLoaded *out, WtScratch *scr, int ground_only) {
    size_t plen = hdr[5];
    int ok = hdr[4] < 200000 && hdr[6] == (uint32_t)(len - 32) && plen < (256u << 20) && plen >= 12;
    if (!ok) return 0;
    unsigned char *buf = ground_only ? (scr ? wt_scratch(scr, plen) : (unsigned char *)malloc(plen))
                                     : (unsigned char *)malloc(plen);
    if (!buf) return 0;
    if (hdr[7] & 1) ok = lz4_decompress(data + 32, (int)hdr[6], buf, (int)plen);
    else memcpy(buf, data + 32, plen);
    uint32_t rh[3];
    if (ok) { memcpy(rh, buf, sizeof rh); ok = rh[0] == WT_READY_MAGIC && rh[1] == hdr[4] && rh[2] >= 16 &&
                                               rh[2] + (size_t)rh[1] * sizeof(WTileMesh) == plen; }
    uint32_t nm = ok ? rh[1] : 0, nsel = 0;
    size_t gp = 0, total = 0;
    WTileMesh *hs = NULL;   /* the headers, copied out before the block is trimmed */
    if (ok && nm) { hs = (WTileMesh *)malloc((size_t)nm * sizeof *hs); if (hs) memcpy(hs, buf + rh[2], (size_t)nm * sizeof *hs); else ok = 0; }
    for (uint32_t k = 0; ok && k < nm; k++) {   /* sizes + bounds checks */
        const WTileMesh h = hs[k];
        if (h.nverts == 0 || h.nverts > 65536 || h.nidx == 0 || h.nidx > (1u << 22)) { ok = 0; break; }
        gp += wt_dec_size(&h);
        if (16 + gp > rh[2]) { ok = 0; break; }
        if ((ground_only && !wt_reduced_keep(ground_only, h.cat, h.world_role, h.world_bounds)) || wt_never_drawn(&h)) continue;
        total += wt_dec_size(&h); nsel++;
    }
    N2GeomBlock *blk = NULL; N2SharedGeometry *geoms = NULL; unsigned char *geo = NULL;
    if (ok) {
        blk = (N2GeomBlock *)calloc(1, sizeof *blk);
        geoms = nsel ? (N2SharedGeometry *)calloc(nsel, sizeof *geoms) : NULL;
        out->m = nsel ? (N2Mesh *)calloc(nsel, sizeof *out->m) : NULL;
        if (ground_only) geo = (unsigned char *)malloc(total ? total : 4);
        else {   /* drop the headers off the end: shrinks in place */
            unsigned char *nb = (unsigned char *)realloc(buf, 16 + gp);
            if (nb) buf = nb;
            geo = buf;
        }
        if (!blk || !geo || (nsel && (!geoms || !out->m))) ok = 0;
    }
    if (!ok) {
        free(blk); free(geoms); free(out->m); out->m = NULL; free(hs);
        if (ground_only && geo) free(geo);
        if (!ground_only || !scr) free(buf);
        return 0;
    }
    size_t bytes = ground_only ? total : 16 + gp;
    blk->data = geo; blk->geoms = geoms; blk->refs = 1;
    blk->acct = &g_wt_geo_live; blk->bytes = (long long)bytes;
    __sync_fetch_and_add(&g_wt_geo_live, (long long)bytes);
    size_t sp = 16, dp = ground_only ? 0 : 16;
    for (uint32_t k = 0; k < nm; k++) {
        const WTileMesh h = hs[k];
        size_t ds = wt_dec_size(&h);
        if ((ground_only && !wt_reduced_keep(ground_only, h.cat, h.world_role, h.world_bounds)) || wt_never_drawn(&h)) { sp += ds; continue; }
        if (ground_only) memcpy(geo + dp, buf + sp, ds);
        int nv = (int)h.nverts;
        unsigned char *base = geo + dp;
        float *v = (float *)base;
        size_t vb = (size_t)nv * (h.authored_normals ? 8 : 5) * 4;
        uint16_t *ix = (uint16_t *)(base + vb);
        unsigned char *col = h.has_vcol ? base + vb + ((((size_t)h.nidx * 2) + 3) & ~(size_t)3) : NULL;
        N2Mesh *m = &out->m[out->n];
        N2SharedGeometry *g = &geoms[out->n];
        m->verts = v; m->idx = ix; m->vcol = col;
        g->refs = 1; g->verts = m->verts; g->idx = m->idx; g->vcol = m->vcol;
        g->block = blk; blk->refs++;
        m->shared_geometry = g;
        m->nverts = nv; m->nidx = (int)h.nidx; m->texkey = h.texkey;
        m->cat = h.cat; m->mat_exact = h.mat_exact; m->scen = h.scen; m->world_role = h.world_role;
        m->inst = h.inst; m->authored_normals = h.authored_normals;
        m->scen_has = h.scen_has; m->scen_region = h.scen_region; m->scen_index = h.scen_index;
        m->terrain_biased = h.terrain_biased; m->vrepair = h.vrepair; m->draw_mode = h.draw_mode;
        m->reflect = (h.flags & WTM_REFLECT) != 0;
        memcpy(m->world_bounds, h.world_bounds, sizeof m->world_bounds);
        m->world_bounds_valid = 1;
        memcpy(m->sname, h.sname, sizeof m->sname); m->sname[sizeof m->sname - 1] = 0;
        if (strstr(m->sname, "REFBLOCKER")) m->world_role = 4;   /* Carbon reflection blockers: reflection pass only, never drawn (port) */
        memcpy(m->aname, h.aname, sizeof m->aname); m->aname[sizeof m->aname - 1] = 0;
        out->n++;
        sp += ds; dp += ds;
    }
    free(hs);
    if (ground_only && !scr) free(buf);
    out->bytes = bytes + (size_t)nsel * (sizeof *geoms + sizeof *out->m);
    if (__atomic_sub_fetch(&blk->refs, 1, __ATOMIC_ACQ_REL) == 0) n2_block_free(blk);
    return 1;
}

static int wt_decode(unsigned char *data, long len, int tx, int ty, WtLoaded *out, WtScratch *scr,
                     int ground_only) {
    memset(out, 0, sizeof *out); out->tx = tx; out->ty = ty; out->ground = ground_only;
    uint32_t hdr[8];
    int ok = data && len >= (long)sizeof hdr;
    if (ok) memcpy(hdr, data, sizeof hdr);
    if (ok && hdr[0] == 0x54324755u && hdr[1] == WTILE_READY_VERSION)
        return wt_decode_ready(data, len, hdr, tx, ty, out, scr, ground_only);
    ok = ok && hdr[0] == 0x54324755u && (hdr[1] == WTILE_VERSION || hdr[1] == WTILE_BEYOND_VERSION) && hdr[4] < 200000 &&
         hdr[6] == (uint32_t)(len - (long)sizeof hdr) && hdr[5] < (256u << 20);
    unsigned char *payload = NULL;
    if (ok) {
        if (hdr[7] & 1) {
            payload = scr ? wt_scratch(scr, hdr[5] ? hdr[5] : 1)
                          : (unsigned char *)malloc(hdr[5] ? hdr[5] : 1);
            ok = payload && lz4_decompress(data + sizeof hdr, (int)hdr[6], payload, (int)hdr[5]);
        } else payload = data + sizeof hdr;
    }
    size_t plen = ok ? hdr[5] : 0, total = 0;
    uint32_t nsel = 0;
    /* pass 1: sizes */
    for (size_t pos = 0, k = 0; ok && k < hdr[4]; k++) {
        WTileMesh h;
        if (pos + sizeof h > plen) { ok = 0; break; }
        memcpy(&h, payload + pos, sizeof h);
        if (h.nverts == 0 || h.nverts > 65536 || h.nidx == 0 || h.nidx > (1u << 22)) { ok = 0; break; }
        pos += sizeof h + wt_enc_size(&h);
        if (pos > plen) { ok = 0; break; }
        if ((ground_only && !wt_reduced_keep(ground_only, h.cat, h.world_role, h.world_bounds)) || wt_never_drawn(&h)) continue;
        total += wt_dec_size(&h); nsel++;
    }
    N2GeomBlock *blk = NULL; N2SharedGeometry *geoms = NULL; unsigned char *geo = NULL;
    if (ok) {
        blk = (N2GeomBlock *)calloc(1, sizeof *blk);
        geoms = nsel ? (N2SharedGeometry *)calloc(nsel, sizeof *geoms) : NULL;
        out->m = nsel ? (N2Mesh *)calloc(nsel, sizeof *out->m) : NULL;
        geo = (unsigned char *)malloc(total ? total : 4);
        if (!blk || !geo || (nsel && (!geoms || !out->m))) ok = 0;
    }
    if (!ok) {
        free(blk); free(geoms); free(geo); free(out->m); out->m = NULL;
        if (payload && payload != data + sizeof hdr && !scr) free(payload);
        return 0;
    }
    blk->data = geo; blk->geoms = geoms; blk->refs = 1;   /* the decoder's own hold */
    blk->acct = &g_wt_geo_live; blk->bytes = (long long)total;
    __sync_fetch_and_add(&g_wt_geo_live, (long long)total);
    /* pass 2: decode */
    size_t pos = 0, gp = 0;
    for (uint32_t k = 0; k < hdr[4]; k++) {
        WTileMesh h;
        memcpy(&h, payload + pos, sizeof h); pos += sizeof h;
        if ((ground_only && !wt_reduced_keep(ground_only, h.cat, h.world_role, h.world_bounds)) || wt_never_drawn(&h)) { pos += wt_enc_size(&h); continue; }
        int nv = (int)h.nverts;
        float *v = (float *)(geo + gp);
        for (int c = 0; c < 5; c++) {
            int raw = c < 3 ? (h.flags & WTM_RAWPOS) : (h.flags & WTM_RAWUV);
            float inv = 1.0f / (c < 3 ? WT_POS_Q : WT_UV_Q);
            uint32_t acc = 0;
            const unsigned char *l0 = payload + pos, *l1 = l0 + nv, *l2 = l1 + nv, *l3 = l2 + nv;
            for (int i = 0; i < nv; i++) {
                uint32_t w = (uint32_t)l0[i] | (uint32_t)l1[i] << 8 | (uint32_t)l2[i] << 16 | (uint32_t)l3[i] << 24;
                if (raw) memcpy(&v[i*5 + c], &w, 4);
                else { acc += w; v[i*5 + c] = (float)(int32_t)acc * inv; }
            }
            pos += (size_t)nv * 4;
        }
        if (h.authored_normals) { memcpy(v + (size_t)nv * 5, payload + pos, (size_t)nv * 12); pos += (size_t)nv * 12; }
        gp += (size_t)nv * (h.authored_normals ? 8 : 5) * 4;
        uint16_t *ix = (uint16_t *)(geo + gp);
        uint16_t acc16 = 0;
        for (uint32_t i = 0; i < h.nidx; i++) {
            uint16_t d; memcpy(&d, payload + pos, 2); pos += 2;
            acc16 = (uint16_t)(acc16 + d); ix[i] = acc16;
        }
        gp += ((size_t)h.nidx * 2 + 3) & ~(size_t)3;
        unsigned char *col = NULL;
        if (h.has_vcol) {
            col = geo + gp;
            for (int c = 0; c < 4; c++) {
                uint8_t a8 = 0;
                for (int i = 0; i < nv; i++) { a8 = (uint8_t)(a8 + payload[pos++]); col[i*4 + c] = a8; }
            }
            gp += (size_t)nv * 4;
            n2_fix_flat_prelight(v, col, nv);   /* tiles cached before the fix */
        }
        N2Mesh *m = &out->m[out->n];
        N2SharedGeometry *g = &geoms[out->n];
        m->verts = v; m->idx = ix; m->vcol = col;
        g->refs = 1; g->verts = m->verts; g->idx = m->idx; g->vcol = m->vcol;
        g->block = blk; blk->refs++;
        m->shared_geometry = g;
        m->nverts = nv; m->nidx = (int)h.nidx; m->texkey = h.texkey;
        m->cat = h.cat; m->mat_exact = h.mat_exact; m->scen = h.scen; m->world_role = h.world_role;
        m->inst = h.inst; m->authored_normals = h.authored_normals;
        m->scen_has = h.scen_has; m->scen_region = h.scen_region; m->scen_index = h.scen_index;
        m->terrain_biased = h.terrain_biased; m->vrepair = h.vrepair; m->draw_mode = h.draw_mode;
        m->reflect = (h.flags & WTM_REFLECT) != 0;
        {   /* test aid: OPENUG2_MESHDUMP_KEY=<hex texkey> logs the tile meshes that use it */
            const char *e = getenv("OPENUG2_MESHDUMP_KEY");
            if (e && strtoul(e, NULL, 16) == h.texkey) {
                printf("MESHDUMP %.31s / %.27s nv %d vcol %d at %.0f %.0f %.0f:", h.sname, h.aname, nv, h.has_vcol,
                       v[0], v[1], v[2]);
                for (int i = 0; col && i < nv && i < 8; i++) printf(" %02x%02x%02x%02x", col[i*4], col[i*4+1], col[i*4+2], col[i*4+3]);
                printf(" uv");
                for (int i = 0; i < nv && i < 4; i++) printf(" %.2f,%.2f", v[i*5+3], v[i*5+4]);
                printf("\n");
            }
        }
        {   /* bounds of the decoded (1 mm grid) positions, so no vertex
               lies outside its own box */
            float *wb = m->world_bounds;
            wb[0] = wb[3] = v[0]; wb[1] = wb[4] = v[1]; wb[2] = wb[5] = v[2];
            for (int i = 1; i < nv; i++)
                for (int c = 0; c < 3; c++) {
                    float x = v[i*5 + c];
                    if (x < wb[c]) wb[c] = x;
                    if (x > wb[c+3]) wb[c+3] = x;
                }
        }
        m->world_bounds_valid = 1;
        memcpy(m->sname, h.sname, sizeof m->sname); m->sname[sizeof m->sname - 1] = 0;
        if (strstr(m->sname, "REFBLOCKER")) m->world_role = 4;   /* Carbon reflection blockers: reflection pass only, never drawn (port) */
        memcpy(m->aname, h.aname, sizeof m->aname); m->aname[sizeof m->aname - 1] = 0;
        out->n++;
    }
    if (payload != data + sizeof hdr && !scr) free(payload);
    out->bytes = total + (size_t)nsel * (sizeof *geoms + sizeof *out->m);
    /* drop the decoder's hold: from here the meshes keep the block alive */
    if (__atomic_sub_fetch(&blk->refs, 1, __ATOMIC_ACQ_REL) == 0) n2_block_free(blk);
    return 1;
}

/* the v6 payload in b -> a ready payload (malloc'd), 0 on any failure */
static int wt_ready_payload(const WtBuf *b, int tx, int ty, unsigned char **outp, size_t *outn) {
    *outp = NULL; *outn = 0;
    size_t L = 32 + b->n;
    unsigned char *img = (unsigned char *)malloc(L);
    if (!img) return 0;
    uint32_t hdr[8] = { 0x54324755u, WTILE_VERSION, (uint32_t)tx, (uint32_t)ty,
                        (uint32_t)b->meshes, (uint32_t)b->n, (uint32_t)b->n, 0 };
    memcpy(img, hdr, 32); if (b->n) memcpy(img + 32, b->p, b->n);
    WtLoaded t;
    int ok = wt_decode(img, (long)L, tx, ty, &t, NULL, 0) && t.n == (int)b->meshes;
    const N2GeomBlock *blk = ok && t.n ? t.m[0].shared_geometry->block : NULL;
    size_t geon = blk ? (size_t)blk->bytes : 0;
    size_t goff = 16, hoff = goff + ((geon + 3) & ~(size_t)3);
    unsigned char *o = ok ? (unsigned char *)calloc(1, hoff + (size_t)t.n * sizeof(WTileMesh) + 4) : NULL;
    if (!o) ok = 0;
    size_t pos = 0, gp = 0;
    for (int k = 0; ok && k < t.n; k++) {
        WTileMesh h;
        memcpy(&h, b->p + pos, sizeof h);
        pos += sizeof h + wt_enc_size(&h);
        const N2Mesh *m = &t.m[k];
        /* the decoded meshes sit back to back in the block, in order */
        ok = (unsigned char *)m->verts == (unsigned char *)blk->data + gp && m->nverts == (int)h.nverts;
        memcpy(h.world_bounds, m->world_bounds, sizeof h.world_bounds);
        memcpy(o + hoff + (size_t)k * sizeof h, &h, sizeof h);
        gp += wt_dec_size(&h);
    }
    if (ok && gp != geon) ok = 0;
    if (ok) {
        uint32_t rh[3] = { WT_READY_MAGIC, (uint32_t)t.n, (uint32_t)hoff };
        memcpy(o, rh, sizeof rh);
        if (geon) memcpy(o + goff, blk->data, geon);
        *outp = o; *outn = hoff + (size_t)t.n * sizeof(WTileMesh);
#if defined(OPENUG2_SIM_PRE_H)
        {   /* test aid (simulator, OPENUG2_READY_VERIFY=1): the ready decoder
               gives exactly the v6 decoder's meshes, full and reduced loads */
            static int vf = -1; if (vf < 0) vf = getenv("OPENUG2_READY_VERIFY") != NULL;
            static long vt, vbad;
            for (int mode = 0; vf && mode < 3; mode++) {
                int md = mode == 0 ? 0 : mode == 1 ? WT_GROUND_ONLY : WT_FAR_ONLY;
                uint32_t h2[8] = { 0x54324755u, WTILE_READY_VERSION, (uint32_t)tx, (uint32_t)ty,
                                   (uint32_t)t.n, (uint32_t)*outn, (uint32_t)*outn, 0 };
                unsigned char *im2 = (unsigned char *)malloc(32 + *outn);
                memcpy(im2, h2, 32); memcpy(im2 + 32, o, *outn);
                WtLoaded r, q;
                int a1 = wt_decode(img, (long)L, tx, ty, &q, NULL, md);
                int a2 = wt_decode(im2, (long)(32 + *outn), tx, ty, &r, NULL, md);
                int bad = !a1 || !a2 || q.n != r.n || q.bytes + (md ? 0 : 16) != r.bytes;   /* a full ready load keeps its 16-byte lead */
                for (int k = 0; !bad && k < q.n; k++) {
                    const N2Mesh *x = &q.m[k], *y = &r.m[k];
                    size_t nv = (size_t)x->nverts;
                    bad = x->nverts != y->nverts || x->nidx != y->nidx || x->texkey != y->texkey ||
                          x->cat != y->cat || x->mat_exact != y->mat_exact || x->scen != y->scen ||
                          x->world_role != y->world_role || x->inst != y->inst ||
                          x->authored_normals != y->authored_normals || x->scen_has != y->scen_has ||
                          x->scen_region != y->scen_region || x->scen_index != y->scen_index ||
                          x->terrain_biased != y->terrain_biased || x->vrepair != y->vrepair ||
                          x->draw_mode != y->draw_mode || x->reflect != y->reflect ||
                          memcmp(x->world_bounds, y->world_bounds, sizeof x->world_bounds) ||
                          strcmp(x->sname, y->sname) || strcmp(x->aname, y->aname) ||
                          memcmp(x->verts, y->verts, nv * (x->authored_normals ? 8 : 5) * 4) ||
                          memcmp(x->idx, y->idx, (size_t)x->nidx * 2) ||
                          (!x->vcol != !y->vcol) || (x->vcol && memcmp(x->vcol, y->vcol, nv * 4));
                }
                vt++; if (bad) { vbad++; printf("ready verify: (%d,%d) mode %d DIFFERS\n", tx, ty, md); }
                if (a1) wt_release(&q);
                if (a2) wt_release(&r);
                free(im2);
                if (vt % 600 == 0 || bad) printf("ready verify: %ld loads compared, %ld differ\n", vt, vbad);
            }
        }
#endif
    } else free(o);
    if (t.m || t.n) wt_release(&t);
    free(img);
    return ok;
}

static volatile unsigned g_wt_read_ms, g_wt_decode_ms;   /* loader-thread work, per assembly */
static int wt_read_decode_t(const char *track, int tx, int ty, WtLoaded *out, int timed, int ground);
static int wt_read_decode(const char *track, int tx, int ty, WtLoaded *out, int ground) {
    return wt_read_decode_t(track, tx, ty, out, 1, ground);
}
/* one tile's stored bytes, from the pack or its loose file, into the scratch
   (or a plain allocation without one) */
static unsigned char *wt_fetch(const char *track, int tx, int ty, int slot, WtScratch *s,
                               long *out_len, int *packed) {
    WtSrc src;
    if (!wt_src_open(track, tx, ty, slot, &src)) return NULL;
    long n = src.len;
    unsigned char *buf = s ? wt_scratch(s, n > 0 ? (size_t)n : 1) : (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (buf && !wt_src_read(&src, buf)) { if (!s) free(buf); buf = NULL; }
    *packed = src.packed;
    wt_src_close(&src);
    *out_len = n;
    return buf;
}
static unsigned char *wt_preload_take(const char *track, int tx, int ty, WtScratch *s, long *len);
static volatile long long g_wt_n_pack, g_wt_n_loose, g_wt_n_pre;   /* loader-thread reads by source */
static int wt_read_decode_t(const char *track, int tx, int ty, WtLoaded *out, int timed, int ground) {
    long len = 0;
    /* timed = the loader thread, else the read-ahead: each has its own
       scratch and pack handle (a second caller on the same thread kind, not
       expected, gets plain allocations) */
    WtScratch *sc = g_wt_scr[timed ? 1 : 0];
    int mine = __sync_bool_compare_and_swap(&sc[0].busy, 0, 1);
    unsigned t0 = wt_ms();
    int packed = 0, pre = 0;
    /* raw bytes the preload fetched for this track's next start come first */
    unsigned char *data = wt_preload_take(track, tx, ty, mine ? &sc[0] : NULL, &len);
    if (data) pre = 1;
    else data = wt_fetch(track, tx, ty, timed ? 1 : 0, mine ? &sc[0] : NULL, &len, &packed);
    unsigned t1 = wt_ms();
    int ok = 0;
    if (data) {
        if (!pre) __sync_fetch_and_add(&g_wt_bytes_read, (long long)len);
        if (timed) { if (pre) g_wt_n_pre++; else if (packed) g_wt_n_pack++; else g_wt_n_loose++; }
        ok = wt_decode(data, len, tx, ty, out, mine ? &sc[1] : NULL, ground);
        if (timed) { g_wt_read_ms += t1 - t0; g_wt_decode_ms += wt_ms() - t1; }
        if (!mine) free(data);
        if (!ok) printf("world tiles: %s (%d,%d) unreadable (%s)\n", track, tx, ty,
                        pre ? "preload" : packed ? "pack" : "loose file");
    }
    if (mine) { __sync_synchronize(); sc[0].busy = 0; }
    return ok;
}

/* ---- read-ahead --------------------------------------------------------
   A low-priority thread reads AND decodes the tiles around where the next
   build will be centred while the loader is idle, so a build mostly finds
   its new tiles ready (the card manages ~8 MB/s). Per index entry: 0
   nothing, 1 being prepared, 2 ready in g_wt_ready, 3 owned by the
   assembler. Transitions are compare-and-swap.
   A tile whose centre lies beyond the object radius of the aim is prepared
   ground only (WtLoaded.ground). An owned ground-only tile (g_wt_owned_ground)
   that comes within the radius of the aim gets its full decode prepared
   here too (g_wt_up, state g_wt_upst 0 none / 1 being prepared / 2 ready),
   so the build that needs its buildings swaps it in without reading. */
static volatile int *g_wt_state;
static WtLoaded *volatile *g_wt_ready;
static WtLoaded *volatile *g_wt_up;
static volatile int *g_wt_upst;
static volatile char *g_wt_owned_ground;
static volatile long long g_wt_ready_bytes;
static volatile float g_wt_aim[2]; static volatile int g_wt_aim_set;
static volatile float g_wt_radius = 400.0f;
static volatile float g_wt_object_radius = 400.0f;
static volatile int g_wt_prefetch_on;
static volatile int g_wt_paused;      /* world_tiles_trim(1) .. (0) */
static volatile long long g_wt_prefetch_hits;
static volatile int g_wt_pf_busy;     /* the read-ahead is past its pause check */
static int g_wt_up_ready, g_wt_up_now;   /* per assembly: upgrades swapped in / read by the build */
/* stored bytes in hand (state 4, 11-15 PERF_PLAN P1): when kernel blocks are
   too short for a decoded tile, the read-ahead still reads the tile's stored
   (compressed) bytes -- a fraction of its decoded size -- so the build
   decodes it without waiting on the card. hwlog59: kernel user free swung
   12-30 MB with each area handoff, the 4 MB room rule held the read-ahead
   off about half the time and only 24% of new tiles were ready. */
static unsigned char *volatile *g_wt_raw;
static long *g_wt_rawlen;
static volatile long long g_wt_raw_bytes, g_wt_raw_hits;
#define WT_RAW_CAP (8LL << 20)
static void wt_raw_drop(int i) {   /* any thread; CAS 4 -> 1 -> 0 */
    if (!g_wt_raw || g_wt_state[i] != 4 || !__sync_bool_compare_and_swap(&g_wt_state[i], 4, 1)) return;
    unsigned char *p = g_wt_raw[i]; g_wt_raw[i] = NULL;
    if (p) { __sync_fetch_and_sub(&g_wt_raw_bytes, (long long)g_wt_rawlen[i]); free(p); }
    __sync_synchronize(); g_wt_state[i] = 0;
}
#define WT_PREFETCH_CAP (24LL << 20)
#define WT_PREFETCH_ROOM_MB 4u    /* a tile (<= ~3 MB decoded) must fit in kernel blocks; 12 MB cut read-ahead hits 83% -> 33% (10-11) */

void world_tiles_aim(float x, float y) {
    g_wt_aim[0] = x; g_wt_aim[1] = y; g_wt_aim_set = 1;
}

/* the tile's centre within the object radius of (fx, fy) */
static int wt_objects_at(const WtIndexEntry *e, float fx, float fy, float radius) {
    float cx = ((float)e->x + 0.5f) * WTILE_SIZE - fx, cy = ((float)e->y + 0.5f) * WTILE_SIZE - fy;
    return cx * cx + cy * cy <= radius * radius;
}

/* hand back a prepared upgrade (any thread, CAS on its state) */
static void wt_up_drop(int i) {
    if (!g_wt_upst || g_wt_upst[i] != 2 || !__sync_bool_compare_and_swap(&g_wt_upst[i], 2, 1)) return;
    WtLoaded *t = g_wt_up[i]; g_wt_up[i] = NULL;
    if (t) { __sync_fetch_and_sub(&g_wt_ready_bytes, (long long)t->bytes); wt_release(t); free(t); }
    __sync_synchronize(); g_wt_upst[i] = 0;
}

static long g_wt_miss_in, g_wt_miss_out, g_wt_miss_full, g_wt_miss_busy;   /* read-ahead census (wt_take) */
/* the read-ahead loop's turns by outcome: [0] off/paused, [1] a build's texture
   fetch, [2] the 24 MB cap, [3] short room (stored bytes path), [4] nothing in
   reach, [5] prepared a tile */
static volatile long g_wt_ra_turn[6];
static volatile int g_wt_ra_work;      /* the read-ahead is on a tile right now */
static volatile int g_wt_assembling;   /* an assembly is running (the migration keeps off) */
/* co-decode (11-15 play1115c): in downtown the kernel blocks stay under the
   read-ahead's room rule, so it keeps only stored bytes (state 4) and the
   build decoded them one by one (handoffs 673 ms vs 357 on the route) while
   the read-ahead idled. During a build's tile walk the read-ahead decodes
   the same tiles from the other end of the index with the build's own mode:
   no extra memory (the build would decode them now anyway), two cores. */
static volatile int g_wt_co_on;
static volatile float g_wt_co_x, g_wt_co_y, g_wt_co_obj, g_wt_co_reach, g_wt_co_far;
static volatile long g_wt_co_tiles;
static volatile int g_wt_thread_up;
static int wt_thread_up(void);

/* ---- preload: the next start's tiles while the player is in the menus ----
   world_tiles_preload names a track (any, also not the current one) and a
   spot; the read-ahead thread reads the stored (compressed) bytes of the
   tiles a build there would use, nearest first, into 1 MB arena chunks
   (kernel blocks: nothing left behind in the heap), up to WT_PL_CAP or
   WT_PL_CAP_TRIM while the front end holds the world trimmed. The next
   build of that track takes them instead of reading the card; the build
   that reaches the spot hands the rest back. Only the read-ahead thread
   allocates or frees chunks; a loader copies an entry out under the lock. */
#define WT_PL_CHUNK    (1u << 20)
#define WT_PL_CAP      (16LL << 20)
#define WT_PL_CAP_TRIM (8LL << 20)
typedef struct { int16_t x, y; uint32_t len; unsigned char *p; } WtPlEnt;
typedef struct { unsigned char *p; size_t cap, used; } WtPlChunk;
static struct { volatile int gen, on, miss; char track[64]; float x, y; } g_wt_plq;   /* the request */
static volatile int g_wt_plq_lock;
static char g_wt_pl_track[64];                  /* what the cache holds (under g_wt_pl_lock) */
static WtPlEnt *g_wt_pl; static int g_wt_npl, g_wt_capl;
static WtPlChunk *g_wt_plc; static int g_wt_nplc, g_wt_capplc;
static volatile long long g_wt_pl_bytes;        /* chunk bytes held */
static volatile int g_wt_pl_lock, g_wt_pl_any, g_wt_pl_drop, g_wt_pl_work;

static unsigned char *wt_preload_take(const char *track, int tx, int ty, WtScratch *s, long *len) {
    if (!g_wt_pl_any) return NULL;
    unsigned char *buf = NULL;
    wt_lock(&g_wt_pl_lock);
    if (!strcmp(g_wt_pl_track, track))
        for (int i = 0; i < g_wt_npl; i++) {
            WtPlEnt *e = &g_wt_pl[i];
            if (e->x != tx || e->y != ty || !e->p) continue;
            buf = s ? wt_scratch(s, e->len) : (unsigned char *)malloc(e->len);
            if (buf) { memcpy(buf, e->p, e->len); *len = (long)e->len; e->p = NULL; }
            break;
        }
    wt_unlock(&g_wt_pl_lock);
    return buf;
}

void world_tiles_preload(const char *track, float x, float y) {
#ifdef __vita__
    if (track && g_wt_off_file) return;
    wt_lock(&g_wt_plq_lock);
    int same = track && g_wt_plq.on && !strcmp(g_wt_plq.track, track) &&
               fabsf(g_wt_plq.x - x) < 1.0f && fabsf(g_wt_plq.y - y) < 1.0f;
    if (!same) {
        if (track) { snprintf(g_wt_plq.track, sizeof g_wt_plq.track, "%s", track); g_wt_plq.x = x; g_wt_plq.y = y; }
        g_wt_plq.on = track != NULL; g_wt_plq.miss = 0;
        __sync_synchronize();
        g_wt_plq.gen++;
    }
    wt_unlock(&g_wt_plq_lock);
    if (same || !track) return;
    printf("world tiles: preload %s at (%.0f, %.0f)\n", track, x, y);
    wt_thread_up();
#else
    (void)track; (void)x; (void)y;
#endif
}

/* after a build: one that reached the preload spot used what it could and
   the rest goes; three builds of that track elsewhere (in play, not in the
   menus) mean the aim was wrong */
static void wt_preload_after(const char *track, float fx, float fy, float radius) {
    if (!g_wt_plq.on) return;
    int drop = 0;
    wt_lock(&g_wt_plq_lock);
    if (g_wt_plq.on && !strcmp(g_wt_plq.track, track)) {
        float dx = g_wt_plq.x - fx, dy = g_wt_plq.y - fy;
        drop = dx * dx + dy * dy <= radius * radius ? 1 : !g_wt_paused && ++g_wt_plq.miss >= 3 ? 2 : 0;
    }
    wt_unlock(&g_wt_plq_lock);
    if (!drop) return;
    printf("world tiles: preload %s\n", drop == 1 ? "reached, the rest dropped" : "aimed elsewhere, dropped");
    world_tiles_preload(NULL, 0, 0);
}

#ifdef __vita__
static void wt_pl_free_all(void) {   /* read-ahead thread */
    wt_lock(&g_wt_pl_lock);
    g_wt_pl_any = 0;
    for (int i = 0; i < g_wt_nplc; i++) free(g_wt_plc[i].p);
    free(g_wt_plc); g_wt_plc = NULL; g_wt_nplc = g_wt_capplc = 0;
    free(g_wt_pl); g_wt_pl = NULL; g_wt_npl = g_wt_capl = 0;
    g_wt_pl_bytes = 0;
    wt_unlock(&g_wt_pl_lock);
}
/* one tile of preload work (read-ahead thread, outside its busy window:
   touches nothing a reset frees) */
static void wt_preload_step(int paused) {
    static int gen, nidx, done = 1, got;
    static WtIndexEntry *idx; static char *have;
    static char track[64]; static float px, py;
    static unsigned t0;
    if (g_wt_pl_drop) {   /* trimmed below what it held: fill again within the smaller cap */
        g_wt_pl_drop = 0; wt_pl_free_all();
        if (have) { memset(have, 0, (size_t)nidx); done = 0; got = 0; }
    }
    if (g_wt_plq.gen != gen) {
        wt_lock(&g_wt_plq_lock);
        gen = g_wt_plq.gen;
        int on = g_wt_plq.on;
        snprintf(track, sizeof track, "%s", g_wt_plq.track); px = g_wt_plq.x; py = g_wt_plq.y;
        wt_unlock(&g_wt_plq_lock);
        wt_pl_free_all();
        free(idx); idx = NULL; free(have); have = NULL; nidx = 0; done = 1; got = 0;
        if (on && wt_index_read(track, &idx, &nidx) && (have = (char *)calloc((size_t)(nidx ? nidx : 1), 1))) {
            wt_pack_probe(track, 0);
            wt_lock(&g_wt_pl_lock); snprintf(g_wt_pl_track, sizeof g_wt_pl_track, "%s", track); wt_unlock(&g_wt_pl_lock);
            done = 0; t0 = wt_ms();
        }
        g_wt_pl_work = !done;
        return;
    }
    g_wt_pl_work = !done;
    if (done || g_wt_converting) return;
    extern int world_resident_texture_fetch(void);
    if (world_resident_texture_fetch()) return;
    long long cap = paused ? WT_PL_CAP_TRIM : WT_PL_CAP;
    float reach = g_wt_radius + 60.0f;   /* what a build there reads */
    int best = -1; float bd = reach * reach;
    for (int i = 0; i < nidx; i++) {
        if (have[i]) continue;
        if (idx[i].x == -32768) { best = i; break; }   /* sky + vista: a fresh build's first read */
        float d = wt_tile_d2(&idx[i], px, py);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) {
        done = 1; g_wt_pl_work = 0;
        printf("world tiles: preload %s ready: %d tiles, %lld KB held, %u ms\n",
               track, got, g_wt_pl_bytes >> 10, wt_ms() - t0);
        return;
    }
    have[best] = 1;
    WtSrc src;
    if (!wt_src_open(track, idx[best].x, idx[best].y, 2, &src)) return;
    size_t len = (size_t)src.len;
    WtPlChunk *c = g_wt_nplc ? &g_wt_plc[g_wt_nplc - 1] : NULL;
    if (!c || c->cap - c->used < len) {
        size_t sz = len > WT_PL_CHUNK ? len : WT_PL_CHUNK;
        extern unsigned vita_bigblk_room_mb(void);
        if (g_wt_pl_bytes + (long long)sz > cap) {
            wt_src_close(&src);
            done = 1; g_wt_pl_work = 0;
            printf("world tiles: preload %s at its cap: %d tiles, %lld KB held, %u ms\n",
                   track, got, g_wt_pl_bytes >> 10, wt_ms() - t0);
            return;
        }
        /* kernel blocks only, same rule as the read-ahead: try again later */
        if (vita_bigblk_room_mb() < WT_PREFETCH_ROOM_MB + (unsigned)(sz >> 20)) { wt_src_close(&src); have[best] = 0; return; }
        if (g_wt_nplc == g_wt_capplc) {
            int cap2 = g_wt_capplc ? g_wt_capplc * 2 : 16;
            WtPlChunk *g = (WtPlChunk *)realloc(g_wt_plc, (size_t)cap2 * sizeof *g);
            if (!g) { wt_src_close(&src); have[best] = 0; return; }
            g_wt_plc = g; g_wt_capplc = cap2;
        }
        unsigned char *p = (unsigned char *)malloc(sz);
        if (!p) { wt_src_close(&src); have[best] = 0; return; }
        c = &g_wt_plc[g_wt_nplc++];
        c->p = p; c->cap = sz; c->used = 0;
        g_wt_pl_bytes += (long long)sz;
    }
    unsigned char *dst = c->p + c->used;
    int ok = wt_src_read(&src, dst);
    wt_src_close(&src);
    if (!ok) return;
    c->used += len;
    wt_lock(&g_wt_pl_lock);
    if (g_wt_npl == g_wt_capl) {
        int cap2 = g_wt_capl ? g_wt_capl * 2 : 64;
        WtPlEnt *g = (WtPlEnt *)realloc(g_wt_pl, (size_t)cap2 * sizeof *g);
        if (g) { g_wt_pl = g; g_wt_capl = cap2; }
    }
    if (g_wt_npl < g_wt_capl) {
        g_wt_pl[g_wt_npl++] = (WtPlEnt){ idx[best].x, idx[best].y, (uint32_t)len, dst };
        g_wt_pl_any = 1; got++;
    }
    wt_unlock(&g_wt_pl_lock);
}

static int wt_prefetch_thread(void *arg) {
    (void)arg;
    int co_went = 0;   /* the last turn co-decoded: no nap */
    for (;;) {
        g_wt_pf_busy = 0; g_wt_ra_work = 0;
        if (!(co_went && g_wt_co_on)) SDL_Delay(40);
        co_went = 0;
        g_wt_pf_busy = 1;
        __sync_synchronize();
        if (!g_wt_prefetch_on || !g_wt_aim_set || !g_wt_state || g_wt_paused) {
            g_wt_ra_turn[0]++;
            /* nothing to prepare for this track (or the front end is up):
               the preload's turn, outside the busy window a reset waits on */
            int paused = g_wt_paused;
            g_wt_pf_busy = 0;
            wt_preload_step(paused);
            continue;
        }
        /* not while a build reads its textures from the same card: reading
           beside it doubled their cost (10-09). The rest of a build is CPU
           work, and pausing for all of it starved the read-ahead whenever
           builds ran back to back at speed. */
        extern int world_resident_texture_fetch(void);
        if (world_resident_texture_fetch()) { g_wt_ra_turn[1]++; continue; }
        /* +a build cell (11-15): the next build may centre a cell off the
           aim in any direction (the road aim swings); the sim census had the
           read-ahead idle 88% of its turns while builds read their far ring
           themselves. raextra.txt sets the metres (0 = the old reach). */
        static float ra_extra = -1.0f;
        if (ra_extra < 0) {
            ra_extra = 80.0f;
            FILE *f = fopen("ux0:data/NFSU2/raextra.txt", "r");
            if (f) { float v; if (fscanf(f, "%f", &v) == 1 && v >= 0) ra_extra = v; fclose(f); }
            printf("world tiles: read-ahead reach +%.0f m\n", (double)ra_extra);
        }
        float ax = g_wt_aim[0], ay = g_wt_aim[1], reach = g_wt_radius + 60.0f + wt_far_extra() + ra_extra;
        float object_radius = g_wt_object_radius;
        /* drop prepared tiles that went stale (well away from the aim) */
        for (int i = 0; i < g_wt_nindex; i++) {
            float far = reach + 256.0f;
            if (g_wt_state[i] != 2 && g_wt_state[i] != 4 && g_wt_upst[i] != 2) continue;
            if (wt_tile_d2(&g_wt_index[i], ax, ay) < far * far) continue;
            wt_up_drop(i);
            wt_raw_drop(i);
            if (g_wt_state[i] != 2 || !__sync_bool_compare_and_swap(&g_wt_state[i], 2, 1)) continue;
            WtLoaded *t = g_wt_ready[i]; g_wt_ready[i] = NULL;
            if (t) { __sync_fetch_and_sub(&g_wt_ready_bytes, (long long)t->bytes); wt_release(t); free(t); }
            __sync_synchronize(); g_wt_state[i] = 0;
        }
        if (g_wt_ready_bytes >= WT_PREFETCH_CAP) { g_wt_ra_turn[2]++; g_wt_pf_busy = 0; wt_preload_step(0); continue; }
        /* only into kernel blocks: a speculative tile that overflows into
           the heap leaves a ~1 MB hole there when it goes stale or is
           replaced, and those holes are what starve the builds (10-11) */
        extern unsigned vita_bigblk_room_mb(void);
        unsigned room = vita_bigblk_room_mb();
        if (room < WT_PREFETCH_ROOM_MB && g_wt_co_on && g_wt_raw) {
            /* a build is walking its tiles: decode its stored bytes beside it */
            float cx = g_wt_co_x, cy = g_wt_co_y, far = g_wt_co_far;
            int cb = -1;
            for (int i = g_wt_nindex - 1; i >= 0; i--) {
                const WtIndexEntry *e = &g_wt_index[i];
                if (e->x == -32768 || g_wt_state[i] != 4 || wt_tile_d2(e, cx, cy) > far * far) continue;
                cb = i; break;
            }
            if (cb >= 0 && __sync_bool_compare_and_swap(&g_wt_state[cb], 4, 1)) {
                g_wt_ra_work = 1;
                const WtIndexEntry *e = &g_wt_index[cb];
                unsigned char *rawp = g_wt_raw[cb]; long rawn = g_wt_rawlen[cb]; g_wt_raw[cb] = NULL;
                if (rawp) __sync_fetch_and_sub(&g_wt_raw_bytes, (long long)rawn);
                float rr = g_wt_co_reach;
                int mode = wt_objects_at(e, cx, cy, g_wt_co_obj) ? 0
                         : wt_tile_d2(e, cx, cy) <= rr * rr ? WT_GROUND_ONLY : WT_FAR_ONLY;
                WtLoaded *t = rawp ? (WtLoaded *)calloc(1, sizeof *t) : NULL;
                int ok = t && wt_decode(rawp, rawn, e->x, e->y, t, NULL, mode);
                free(rawp);
                if (ok) { g_wt_ready[cb] = t; __sync_fetch_and_add(&g_wt_ready_bytes, (long long)t->bytes); g_wt_co_tiles++; }
                else free(t);
                __sync_synchronize();
                g_wt_state[cb] = ok ? 2 : 0;   /* failed: the build reads it itself */
                co_went = 1;
                continue;
            }
        }
        if (room < WT_PREFETCH_ROOM_MB) {
            g_wt_ra_turn[3]++;
            /* short: the nearest missing tile's stored bytes only (a tile's
               stored size is well under 1 MB; 1 MB of room keeps it in a
               kernel block, out of the heap) */
            if (room < 1u || !g_wt_raw || g_wt_raw_bytes >= WT_RAW_CAP) continue;
            int rb = -1; float rd = reach * reach;
            for (int i = 0; i < g_wt_nindex; i++) {
                const WtIndexEntry *e = &g_wt_index[i];
                if (e->x == -32768 || g_wt_state[i] != 0) continue;
                float d = wt_tile_d2(e, ax, ay);
                if (d < rd) { rd = d; rb = i; }
            }
            if (rb < 0 || !__sync_bool_compare_and_swap(&g_wt_state[rb], 0, 1)) continue;
            g_wt_ra_work = 1;
            long len = 0; int packed = 0;
            unsigned char *p = wt_preload_take(g_wt_track, g_wt_index[rb].x, g_wt_index[rb].y, NULL, &len);
            if (!p) p = wt_fetch(g_wt_track, g_wt_index[rb].x, g_wt_index[rb].y, 0, NULL, &len, &packed);
            if (p) { g_wt_raw[rb] = p; g_wt_rawlen[rb] = len; __sync_fetch_and_add(&g_wt_raw_bytes, (long long)len); }
            __sync_synchronize();
            g_wt_state[rb] = p ? 4 : 0;
            if (!p) SDL_Delay(500);
            continue;
        }
        /* nearest work in reach of the aim: a missing tile, or an owned
           ground-only one whose buildings the aim needs, or stored bytes in
           hand that room now lets it decode */
        int best = -1, up = 0; float bd = reach * reach;
        for (int i = 0; i < g_wt_nindex; i++) {
            const WtIndexEntry *e = &g_wt_index[i];
            if (e->x == -32768) continue;
            int st = g_wt_state[i];
            int want_up = st == 3 && g_wt_owned_ground[i] && g_wt_upst[i] == 0 && wt_objects_at(e, ax, ay, object_radius);
            if (st != 0 && st != 4 && !want_up) continue;
            float d = wt_tile_d2(e, ax, ay);
            if (d < bd) { bd = d; best = i; up = want_up; }
        }
        if (best < 0) { g_wt_ra_turn[4]++; g_wt_pf_busy = 0; wt_preload_step(0); continue; }   /* all in hand */
        g_wt_ra_turn[5]++;
        g_wt_ra_work = 1;
        const WtIndexEntry *e = &g_wt_index[best];
        if (up) {
            if (!__sync_bool_compare_and_swap(&g_wt_upst[best], 0, 1)) continue;
            WtLoaded *t = (WtLoaded *)calloc(1, sizeof *t);
            int ok = t && wt_read_decode_t(g_wt_track, e->x, e->y, t, 0, 0);
            if (ok) { g_wt_up[best] = t; __sync_fetch_and_add(&g_wt_ready_bytes, (long long)t->bytes); }
            else free(t);
            __sync_synchronize();
            g_wt_upst[best] = ok ? 2 : 0;
            if (!ok) SDL_Delay(500);
            continue;
        }
        unsigned char *rawp = NULL; long rawn = 0;
        if (g_wt_state[best] == 4) {   /* bytes in hand: decode them now */
            if (!__sync_bool_compare_and_swap(&g_wt_state[best], 4, 1)) continue;
            rawp = g_wt_raw[best]; rawn = g_wt_rawlen[best]; g_wt_raw[best] = NULL;
            if (rawp) __sync_fetch_and_sub(&g_wt_raw_bytes, (long long)rawn);
        } else if (!__sync_bool_compare_and_swap(&g_wt_state[best], 0, 1)) continue;
        WtLoaded *t = (WtLoaded *)calloc(1, sizeof *t);
        int mode = wt_objects_at(e, ax, ay, object_radius) ? 0
                 : wt_tile_d2(e, ax, ay) > (g_wt_radius + 60.0f + ra_extra) * (g_wt_radius + 60.0f + ra_extra) ? WT_FAR_ONLY : WT_GROUND_ONLY;
        int ok;
        if (rawp) { ok = t && wt_decode(rawp, rawn, e->x, e->y, t, NULL, mode); free(rawp); }
        else ok = t && wt_read_decode_t(g_wt_track, e->x, e->y, t, 0, mode);
        if (ok) { g_wt_ready[best] = t; __sync_fetch_and_add(&g_wt_ready_bytes, (long long)t->bytes); }
        else free(t);
        __sync_synchronize();
        g_wt_state[best] = ok ? 2 : 0;
        if (!ok) SDL_Delay(500);   /* unreadable: do not spin on it */
    }
    return 0;
}
#endif

/* one read-ahead thread for the whole run, across track switches; started
   by the first assembly or the first preload */
static int wt_thread_up(void) {
#ifdef __vita__
    static volatile int claimed; static int off = -1;
    if (g_wt_thread_up) return 1;
    if (!__sync_bool_compare_and_swap(&claimed, 0, 1)) return 0;   /* another thread is starting it */
    if (off < 0) {
        FILE *f = fopen("ux0:data/NFSU2/noreadahead.txt", "r");   /* A/B switch */
        off = f != NULL;
        if (f) { fclose(f); printf("world tiles: read-ahead off (noreadahead.txt)\n"); }
    }
    if (!off && SDL_CreateThread(wt_prefetch_thread, "tile-readahead", NULL)) g_wt_prefetch_on = g_wt_thread_up = 1;
    __sync_synchronize();
    claimed = 0;
    return g_wt_thread_up;
#else
    return 0;
#endif
}

/* Before something big that is not the world (the front end over a
   finished race): hand back what the read-ahead holds and the decode
   scratch, and hold the read-ahead off until world_tiles_trim(0). The
   preload stays (that is what the menus are for) within its smaller cap. */
static void wt_trim(int pause, int keep_preload) {
    g_wt_paused = pause;
    if (!pause) return;
    long long freed = 0;
    for (int i = 0; g_wt_state && i < g_wt_nindex; i++) {
        if (g_wt_upst[i] == 2) { freed += g_wt_up[i] ? (long long)g_wt_up[i]->bytes : 0; wt_up_drop(i); }
        if (g_wt_state[i] == 4) { freed += g_wt_rawlen[i]; wt_raw_drop(i); continue; }
        if (g_wt_state[i] != 2 || !__sync_bool_compare_and_swap(&g_wt_state[i], 2, 1)) continue;
        WtLoaded *t = g_wt_ready[i]; g_wt_ready[i] = NULL;
        if (t) { freed += (long long)t->bytes; __sync_fetch_and_sub(&g_wt_ready_bytes, (long long)t->bytes); wt_release(t); free(t); }
        __sync_synchronize(); g_wt_state[i] = 0;
    }
    for (int k = 0; k < 2; k++) {
        WtScratch *sc = g_wt_scr[k];
        if (!__sync_bool_compare_and_swap(&sc[0].busy, 0, 1)) continue;   /* decoding now */
        freed += (long long)(sc[0].cap + sc[1].cap);
        free(sc[0].p); free(sc[1].p);
        sc[0].p = sc[1].p = NULL; sc[0].cap = sc[1].cap = 0;
        __sync_synchronize(); sc[0].busy = 0;
    }
    if (!keep_preload && g_wt_pl_bytes > WT_PL_CAP_TRIM) { freed += g_wt_pl_bytes; g_wt_pl_drop = 1; }
    printf("world tiles: trimmed %lld KB (read-ahead paused%s)\n", freed >> 10,
           g_wt_pl_bytes ? ", preload kept" : "");
}
void world_tiles_trim(int pause) { wt_trim(pause, 0); }

static void wt_prefetch_start(void) {
    if (g_wt_state || g_wt_nindex <= 0) return;
    size_t n = (size_t)g_wt_nindex;
    g_wt_ready = (WtLoaded *volatile *)calloc(n, sizeof *g_wt_ready);
    g_wt_up = (WtLoaded *volatile *)calloc(n, sizeof *g_wt_up);
    g_wt_upst = (volatile int *)calloc(n, sizeof *g_wt_upst);
    g_wt_owned_ground = (volatile char *)calloc(n, 1);
    g_wt_raw = (unsigned char *volatile *)calloc(n, sizeof *g_wt_raw);
    g_wt_rawlen = (long *)calloc(n, sizeof *g_wt_rawlen);
    if (!g_wt_raw || !g_wt_rawlen) { free((void *)g_wt_raw); free(g_wt_rawlen); g_wt_raw = NULL; g_wt_rawlen = NULL; }   /* optional */
    volatile int *st = (volatile int *)calloc(n, sizeof *st);
    if (!st || !g_wt_ready || !g_wt_up || !g_wt_upst || !g_wt_owned_ground) {
        free((void *)g_wt_raw); free(g_wt_rawlen); g_wt_raw = NULL; g_wt_rawlen = NULL;
        free((void *)st); free((void *)g_wt_ready); free((void *)g_wt_up);
        free((void *)g_wt_upst); free((void *)g_wt_owned_ground);
        g_wt_ready = g_wt_up = NULL; g_wt_upst = NULL; g_wt_owned_ground = NULL;
        return;
    }
    for (int i = 0; i < g_wt_nloaded; i++) {   /* already owned by the assembler */
        int k = wt_index_of(g_wt_loaded[i].tx, g_wt_loaded[i].ty);
        if (k >= 0) { st[k] = 3; g_wt_owned_ground[k] = (char)g_wt_loaded[i].ground; }
    }
    __sync_synchronize();
    g_wt_state = st;                          /* last: the thread tests it first */
    wt_thread_up();
    printf("world tiles: read-ahead %s\n", g_wt_prefetch_on ? "on" : "unavailable");
}

/* ---- migration: loose tiles -> one pack per track --------------------------
   Caches converted before packs hold a file per tile (2377 on a full set).
   A low-priority thread packs them track by track, in index order, into
   <track>_vN.pak.tmp, renames it, publishes it to the readers, waits until
   no reader is between its pack look and its loose read, then deletes the
   track's loose files. It reads only while the streamer is quiet (no
   assembly, read-ahead tile, preload work or texture fetch, and no tile read
   for 400 ms), one tile at a time. Killed part way: the .tmp is removed and
   rebuilt at the next start; loose files go only once their pack is whole,
   covers the index and is published, and a track whose pack is there but
   still has loose files is only cleaned. */
#ifdef __vita__
typedef struct { char name[48]; int idx, pak, tmp, nloose; } WtMigTrack;
typedef struct { int16_t x, y; uint8_t t; } WtMigLoose;
#define WT_MIG_BUF (512u << 10)   /* the copy buffer: a kernel block, one per run */

static int wt_mig_idle(void) {
    extern int world_resident_texture_fetch(void);
    return !g_wt_converting && !g_wt_assembling && !g_wt_ra_work && !g_wt_pl_work &&
           !world_resident_texture_fetch() && wt_ms() - g_wt_last_io_ms > 400;
}
/* Run at boot behind the optimizing screen (world_tiles_migrate_sync): it
   has the card to itself -- in the background it starved the boot and the
   music. g_wt_mig_prog counts tiles copied + loose files removed. */
static int g_wt_mig_sync;
static WTileProgress *g_wt_mig_prog;
static char g_wt_mig_name[48];
static void wt_mig_step(void) {
    if (g_wt_mig_prog) { g_wt_mig_prog->cells_done++; g_wt_mig_prog->now_ms = wt_ms(); }
}
static unsigned wt_mig_wait(void) {   /* ms spent waiting */
    if (g_wt_mig_sync) return 0;
    unsigned t0 = wt_ms();
    while (!wt_mig_idle()) SDL_Delay(100);
    return wt_ms() - t0;
}
static int wt_mig_track(WtMigTrack *tr, int *ntr, const char *name, size_t n) {
    if (!n || n >= sizeof tr[0].name) return -1;
    for (int i = 0; i < *ntr; i++) if (strlen(tr[i].name) == n && !strncmp(tr[i].name, name, n)) return i;
    if (*ntr >= 32) return -1;
    WtMigTrack *t = &tr[(*ntr)++];
    memset(t, 0, sizeof *t); memcpy(t->name, name, n);
    return *ntr - 1;
}
/* "<track>_<x>_<y>.bin": 1 with the track name's length */
static int wt_parse_loose(const char *s, size_t *tn, int *x, int *y) {
    size_t n = strlen(s);
    char buf[96];
    if (n < 9 || n >= sizeof buf || strcmp(s + n - 4, ".bin")) return 0;
    memcpy(buf, s, n - 4); buf[n - 4] = 0;
    char *u2 = strrchr(buf, '_');
    if (!u2 || u2 == buf) return 0;
    *u2 = 0;
    char *u1 = strrchr(buf, '_');
    if (!u1 || u1 == buf) return 0;
    char *e1, *e2;
    long lx = strtol(u1 + 1, &e1, 10), ly = strtol(u2 + 1, &e2, 10);
    if (*e1 || *e2 || e1 == u1 + 1 || e2 == u2 + 1 || lx < -32768 || lx > 32767 || ly < -32768 || ly > 32767) return 0;
    *tn = (size_t)(u1 - buf); *x = (int)lx; *y = (int)ly;
    return 1;
}
static int wt_ends(const char *s, const char *suf, size_t *stem) {
    size_t n = strlen(s), k = strlen(suf);
    if (n <= k || strcmp(s + n - k, suf)) return 0;
    *stem = n - k;
    return 1;
}
/* every tile the index lists is in the pack */
static int wt_mig_covered(const char *track, const WtPackTab *t) {
    WtIndexEntry *ix; int n, ok = 1;
    if (!t || !wt_index_read(track, &ix, &n)) return 0;
    for (int i = 0; ok && i < n; i++) ok = wt_pack_find(t, ix[i].x, ix[i].y) != NULL;
    free(ix);
    return ok;
}
static int wt_mig_pack(const char *track, unsigned char *buf, unsigned *waited) {
    WtIndexEntry *ix; int n;
    if (!wt_index_read(track, &ix, &n)) return 0;
    WtPackW pw;
    if (!wt_pw_open(&pw, track)) { free(ix); printf("tiles pack: cannot write the %s pack\n", track); return 0; }
    unsigned t0 = wt_ms(), w0 = *waited;
    int ok = 1;
    for (int i = 0; ok && i < n; i++) {
        *waited += wt_mig_wait();
        wt_mig_step();
        char path[256];
        snprintf(path, sizeof path, "%s/%.63s_%d_%d.bin", WTILE_DIR, track, ix[i].x, ix[i].y);
        long size = 0;
        WtLFile fd = wt_lopen(path, &size);
        if (fd == WT_NOLFILE) { printf("tiles pack: %s missing; %s stays loose\n", path, track); ok = 0; break; }
        uint32_t off = pw.pos;
        ok = size >= 32 && size < (256L << 20);
        for (long got = 0; ok && got < size; ) {
            long k = size - got > (long)WT_MIG_BUF ? (long)WT_MIG_BUF : size - got;
            if (!wt_lread(fd, buf, k)) { ok = 0; break; }
            if (!got) {   /* the tile's own header: this version, and the size it says */
                uint32_t h[8];
                memcpy(h, buf, sizeof h);
                ok = h[0] == 0x54324755u && h[1] == WTILE_VERSION && (long)h[6] + 32 == size;
            }
            if (ok) ok = wt_pw_put(&pw, buf, (size_t)k);
            got += k;
        }
        wt_lclose(fd);
        if (ok) ok = wt_pw_entry(&pw, ix[i].x, ix[i].y, off);
        else printf("tiles pack: %s unreadable; %s stays loose\n", path, track);
    }
    uint32_t bytes = pw.pos;
    ok = wt_pw_finish(&pw, track, ok);
    if (ok) printf("tiles pack: %s %d tiles, %u MB in %u ms (%u ms of it waiting for the streamer)\n",
                   track, n, bytes >> 20, wt_ms() - t0, *waited - w0);
    free(ix);
    return ok;
}
static void wt_migrate_run(void) {
    if (!g_wt_mig_sync) SDL_Delay(5000);   /* past the boot's first builds */
    wt_mig_wait();
    unsigned t0 = wt_ms(), waited = 0;
    char sidx[24], spak[24], stmp[24];
    snprintf(sidx, sizeof sidx, "_v%d.idx", WTILE_VERSION);
    snprintf(spak, sizeof spak, "_v%d.pak", WTILE_VERSION);
    snprintf(stmp, sizeof stmp, "_v%d.pak.tmp", WTILE_VERSION);
    WtMigTrack tr[32]; int ntr = 0;
    WtMigLoose *lo = NULL; int nlo = 0, caplo = 0, nent = 0;
    DIR *d = opendir(WTILE_DIR);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *s = de->d_name;
        size_t stem; int x, y, t;
        nent++;
        if (wt_ends(s, sidx, &stem)) { if ((t = wt_mig_track(tr, &ntr, s, stem)) >= 0) tr[t].idx = 1; }
        else if (wt_ends(s, spak, &stem)) { if ((t = wt_mig_track(tr, &ntr, s, stem)) >= 0) tr[t].pak = 1; }
        else if (wt_ends(s, stmp, &stem)) { if ((t = wt_mig_track(tr, &ntr, s, stem)) >= 0) tr[t].tmp = 1; }
        else if (wt_parse_loose(s, &stem, &x, &y) && (t = wt_mig_track(tr, &ntr, s, stem)) >= 0) {
            if (nlo == caplo) {
                int cap = caplo ? caplo * 2 : 1024;
                WtMigLoose *g = (WtMigLoose *)realloc(lo, (size_t)cap * sizeof *g);
                if (!g) break;
                lo = g; caplo = cap;
            }
            lo[nlo++] = (WtMigLoose){ (int16_t)x, (int16_t)y, (uint8_t)t };
            tr[t].nloose++;
        }
    }
    closedir(d);
    int todo = 0;
    for (int i = 0; i < ntr; i++) todo += tr[i].idx && (tr[i].nloose || tr[i].tmp);
    printf("tiles pack: %d files listed in %u ms, %d loose tiles, %d track(s) to pack or clean\n",
           nent, wt_ms() - t0, nlo, todo);
    if (g_wt_mig_prog) {   /* every loose tile is copied (unless packed already) and removed */
        int total = 0;
        for (int i = 0; i < ntr; i++)
            if (tr[i].idx && tr[i].nloose) total += tr[i].nloose * (tr[i].pak ? 1 : 2);
        g_wt_mig_prog->cells_total = total;
        g_wt_mig_prog->start_ms = g_wt_mig_prog->now_ms = wt_ms();
    }
    unsigned char *buf = todo ? (unsigned char *)malloc(WT_MIG_BUF) : NULL;
    for (int i = 0; buf && i < ntr; i++) {
        WtMigTrack *t = &tr[i];
        if (!t->idx) continue;   /* no complete set of this version: not ours to touch */
        if (t->tmp) { char p[256]; wt_pack_path(p, sizeof p, t->name, 1); wt_remove(p); }   /* killed mid-pack */
        if (!t->nloose) continue;
        snprintf(g_wt_mig_name, sizeof g_wt_mig_name, "%s", t->name);
        WtPackTab *pk = t->pak ? wt_pack_probe(t->name, 1) : NULL;
        if (!wt_mig_covered(t->name, pk)) {
            if (!wt_mig_pack(t->name, buf, &waited)) continue;
            pk = wt_pack_probe(t->name, 1);   /* published: readers take it from their next tile */
            if (!wt_mig_covered(t->name, pk)) { printf("tiles pack: %s did not verify; loose files kept\n", t->name); continue; }
        }
        /* a reader that looked before the publish may still be on a loose file */
        __sync_synchronize();
        while (g_wt_loose_inflight) SDL_Delay(5);
        unsigned t1 = wt_ms(); int del = 0, seen = 0;
        for (int k = 0; k < nlo; k++) {
            if (lo[k].t != (uint8_t)i) continue;
            if (!(seen++ & 7)) waited += wt_mig_wait();
            char p[256];
            snprintf(p, sizeof p, "%s/%.47s_%d_%d.bin", WTILE_DIR, t->name, lo[k].x, lo[k].y);
            del += wt_remove(p);
            wt_mig_step();
        }
        printf("tiles pack: %s %d loose tiles removed in %u ms\n", t->name, del, wt_ms() - t1);
    }
    free(buf); free(lo);
    printf("tiles pack: migration done in %u s (%u s waiting for the streamer)\n",
           (wt_ms() - t0) / 1000, waited / 1000);
}
static int wt_migrate_thread(void *arg) {
    (void)arg;
#ifndef OPENUG2_SIM_PRE_H
    sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 191);   /* the lowest user priority */
    wt_migrate_run();
    /* exit AND delete: an SDL_Thread nobody waits for keeps its 2 MB
       kernel-memory stack (the SDL_Thread record, a few bytes, stays) */
    return sceKernelExitDeleteThread(0);
#else
    wt_migrate_run();
    return 0;
#endif
}
#endif
/* The boot's one-time packing, on the caller's thread with the card to
   itself; progress in p (cells = tiles copied + removed). 1 when nothing
   was left loose afterwards or there was nothing to do. */
int world_tiles_migrate_sync(WTileProgress *p) {
#ifdef __vita__
    g_wt_mig_sync = 1; g_wt_mig_prog = p;
    wt_migrate_run();
    g_wt_mig_sync = 0; g_wt_mig_prog = NULL;
#endif
    if (p) { p->ok = 1; p->done = 1; }
    return 1;
}
const char *world_tiles_migrate_track(void) {
#ifdef __vita__
    return g_wt_mig_name;
#else
    return "";
#endif
}
/* Cheap boot test: any loose tile file left in the cache directory. */
int world_tiles_migrate_needed(void) {
#ifdef __vita__
    DIR *d = opendir(WTILE_DIR);
    if (!d) return 0;
    struct dirent *de; int found = 0;
    char sidx[24]; snprintf(sidx, sizeof sidx, "_v%d.idx", WTILE_VERSION);
    while (!found && (de = readdir(d))) {
        size_t stem; int x, y;
        found = wt_parse_loose(de->d_name, &stem, &x, &y) ||
                (strstr(de->d_name, ".pak.tmp") != NULL);
    }
    closedir(d);
    return found;
#else
    return 0;
#endif
}
void world_tiles_migrate_start(void) {
#ifdef __vita__
    static volatile int started;
    if (!__sync_bool_compare_and_swap(&started, 0, 1)) return;
    if (!SDL_CreateThread(wt_migrate_thread, "tile-pack", NULL)) { printf("tiles pack: no migration thread\n"); started = 0; }
#endif
}

/* Tile sets of older versions (an update raised WTILE_VERSION; v0.1.0 wrote
   v5): once this version's index for a track is on the card -- its
   conversion completed -- that track's older .idx / .pak / .pak.tmp are dead
   weight (~50-65 MB for the city). Kept until then: a reconversion reads the
   old index to skip the open sea (wt_mark_from_old_index). */
static void wt_drop_old_run(void) {
    DIR *d = opendir(WTILE_DIR);
    if (!d) return;
    static char names[64][96]; int nn = 0;
    struct dirent *de;
    while ((de = readdir(d)) && nn < 64) {
        const char *s = de->d_name, *u = strrchr(s, '_');
        int v = 0; char ext[16] = "";
        if (!u || (size_t)(u - s) >= 64 || strlen(s) >= sizeof names[0]) continue;
        if (sscanf(u, "_v%d.%15s", &v, ext) != 2 || v <= 0) continue;
        if (strcmp(ext, "idx") && strcmp(ext, "pak") && strcmp(ext, "pak.tmp")) continue;
        snprintf(names[nn++], sizeof names[0], "%s", s);
    }
    closedir(d);
    int del = 0;
    for (int i = 0; i < nn; i++) {
        char track[64], cur[256], p[256];
        const char *u = strrchr(names[i], '_');
        snprintf(track, sizeof track, "%.*s", (int)(u - names[i]), names[i]);
        { int v = 0; char e[16]; if (sscanf(u, "_v%d.%15s", &v, e) != 2 || v >= wt_ver(track)) continue; }
        wt_index_path(cur, sizeof cur, track);
        FILE *f = fopen(cur, "rb");
        if (!f) continue;                   /* this version not complete yet: keep the old set */
        fclose(f);
        snprintf(p, sizeof p, "%s/%s", WTILE_DIR, names[i]);
        if (wt_remove(p)) { del++; printf("tiles: removed %s (older tile version)\n", names[i]); }
    }
    if (del) printf("tiles: %d file(s) of older tile versions removed\n", del);
}
#if defined(__vita__) && !defined(OPENUG2_SIM_PRE_H)
static int wt_drop_old_thread(void *arg) {
    (void)arg;
    sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 191);   /* the lowest user priority */
    wt_drop_old_run();
    return sceKernelExitDeleteThread(0);
}
#endif
void world_tiles_drop_old_versions(void) {
#if defined(__vita__) && !defined(OPENUG2_SIM_PRE_H)
    if (!SDL_CreateThread(wt_drop_old_thread, "tile-old", NULL)) wt_drop_old_run();
#else
    wt_drop_old_run();
#endif
}

/* The assembler's tile: take a prepared one when it is there (or being
   prepared), else read and decode now (ground only unless `full`). Marks
   the entry owned. A prepared tile may be ground only; the caller upgrades. */
static int wt_take(const char *track, int idx, int tx, int ty, WtLoaded *out, int mode) {   /* mode: 0 full, WT_GROUND_ONLY, WT_FAR_ONLY */
    if (idx < 0 || !g_wt_state) return wt_read_decode(track, tx, ty, out, mode);
    int waited = 0;
    for (;;) {
        int st = g_wt_state[idx];
        if (st == 2 && __sync_bool_compare_and_swap(&g_wt_state[idx], 2, 3)) {
            WtLoaded *t = g_wt_ready[idx]; g_wt_ready[idx] = NULL;
            __sync_fetch_and_sub(&g_wt_ready_bytes, (long long)t->bytes);
            *out = *t; free(t);
            g_wt_prefetch_hits++;
            g_wt_owned_ground[idx] = (char)out->ground;
            return 1;
        }
        if (st == 4 && __sync_bool_compare_and_swap(&g_wt_state[idx], 4, 3)) {   /* stored bytes in hand */
            unsigned char *p = g_wt_raw[idx]; long n = g_wt_rawlen[idx]; g_wt_raw[idx] = NULL;
            __sync_fetch_and_sub(&g_wt_raw_bytes, (long long)n);
            WtScratch *sc = g_wt_scr[1];
            int mine = __sync_bool_compare_and_swap(&sc[0].busy, 0, 1);
            unsigned t1 = wt_ms();
            int ok = p && wt_decode(p, n, tx, ty, out, mine ? &sc[1] : NULL, mode);
            g_wt_decode_ms += wt_ms() - t1;
            if (mine) { __sync_synchronize(); sc[0].busy = 0; }
            free(p);
            if (ok) { g_wt_raw_hits++; g_wt_owned_ground[idx] = (char)out->ground; return 1; }
            if (wt_read_decode(track, tx, ty, out, mode)) { g_wt_owned_ground[idx] = (char)out->ground; return 1; }
            g_wt_state[idx] = 0;
            return 0;
        }
        if (st == 0 && __sync_bool_compare_and_swap(&g_wt_state[idx], 0, 3)) {
            {   /* read-ahead census (11-15, PERF_PLAN read-ahead >= 90%): a tile the
                   build reads itself -- in the read-ahead's reach of its aim
                   (it did not get to it) or out of it (aimed elsewhere) */
                float reach = g_wt_radius + 60.0f + wt_far_extra();
                float d2 = g_wt_aim_set ? wt_tile_d2(&g_wt_index[idx], g_wt_aim[0], g_wt_aim[1]) : 1e30f;
                if (d2 < reach * reach) g_wt_miss_in++; else g_wt_miss_out++;
                if (mode == 0) g_wt_miss_full++;
            }
            if (wt_read_decode(track, tx, ty, out, mode)) { g_wt_owned_ground[idx] = (char)out->ground; return 1; }
            g_wt_state[idx] = 0;
            return 0;
        }
        if (st == 3) return wt_read_decode(track, tx, ty, out, mode);   /* (not expected) */
        if (st == 1 && !waited) { waited = 1; g_wt_miss_busy++; }
#ifdef __vita__
        SDL_Delay(2);   /* the read-ahead is preparing it right now */
        /* a wait this long means the read-ahead itself is stuck (audit 11-17):
           said once, so a hang report shows where */
        if (++waited == 1500) printf("wt_take: tile %d still being prepared after 3 s (state %d)\n", idx, g_wt_state[idx]);
#endif
    }
}

/* An owned ground-only tile whose buildings this build needs: the read-ahead's
   full decode when prepared (waiting if it is being prepared), else read now. */
static int wt_upgrade(const char *track, int idx, WtLoaded *t) {
    WtLoaded full;
    int upwait = 0; (void)upwait;
    if (idx < 0 || !g_wt_upst) {
        if (!wt_read_decode(track, t->tx, t->ty, &full, 0)) return 0;
        g_wt_up_now++;
    } else for (;;) {
        int st = g_wt_upst[idx];
        if (st == 2 && __sync_bool_compare_and_swap(&g_wt_upst[idx], 2, 1)) {
            WtLoaded *u = g_wt_up[idx]; g_wt_up[idx] = NULL;
            __sync_fetch_and_sub(&g_wt_ready_bytes, (long long)u->bytes);
            full = *u; free(u);
            __sync_synchronize(); g_wt_upst[idx] = 0;
            g_wt_up_ready++;
            break;
        }
        if (st == 0 && __sync_bool_compare_and_swap(&g_wt_upst[idx], 0, 1)) {
            int ok = wt_read_decode(track, t->tx, t->ty, &full, 0);
            __sync_synchronize(); g_wt_upst[idx] = 0;
            if (!ok) return 0;
            g_wt_up_now++;
            break;
        }
#ifdef __vita__
        SDL_Delay(2);
        if (++upwait == 1500) printf("wt_upgrade: tile %d still being prepared after 3 s\n", idx);
#endif
    }
    wt_release(t);            /* meshes a resident still uses live on in it */
    *t = full;
    if (idx >= 0 && g_wt_owned_ground) g_wt_owned_ground[idx] = 0;
    return 1;
}

/* A far-ring tile (stand-ins only) that came within the ground reach: its
   ground too, read now (a few per build, ~100-300 KB each). */
static int wt_regrade(const char *track, WtLoaded *t) {
    WtLoaded r;
    if (!wt_read_decode(track, t->tx, t->ty, &r, WT_GROUND_ONLY)) return 0;
    wt_release(t);            /* meshes a resident still uses live on in it */
    *t = r;
    return 1;
}

/* A full tile that fell well outside the object radius (behind the car):
   keep only its ground. The ground meshes' geometry is copied into a block
   of its own (no card read) and the full block goes once no resident uses
   it. The packed vertex cache starts empty and is rebuilt on first use. */
static int wt_downgrade(WtLoaded *t) {
    int n = 0; size_t total = 0;
    for (int i = 0; i < t->n; i++) {
        const N2Mesh *m = &t->m[i];
        if (!wt_reduced_keep(WT_GROUND_ONLY, m->cat, m->world_role, m->world_bounds)) continue;
        size_t nv = (size_t)m->nverts;
        total += nv * (m->authored_normals ? 8 : 5) * 4 + (((size_t)m->nidx * 2 + 3) & ~(size_t)3) +
                 (m->vcol ? nv * 4 : 0);
        n++;
    }
    N2GeomBlock *blk = (N2GeomBlock *)calloc(1, sizeof *blk);
    N2SharedGeometry *geoms = n ? (N2SharedGeometry *)calloc((size_t)n, sizeof *geoms) : NULL;
    N2Mesh *ms = n ? (N2Mesh *)calloc((size_t)n, sizeof *ms) : NULL;
    unsigned char *geo = (unsigned char *)malloc(total ? total : 4);
    if (!blk || !geo || (n && (!geoms || !ms))) { free(blk); free(geoms); free(ms); free(geo); return 0; }
    blk->data = geo; blk->geoms = geoms; blk->refs = 1;
    size_t gp = 0; int k = 0;
    for (int i = 0; i < t->n; i++) {
        const N2Mesh *src = &t->m[i];
        if (!wt_reduced_keep(WT_GROUND_ONLY, src->cat, src->world_role, src->world_bounds)) continue;
        size_t nv = (size_t)src->nverts;
        size_t vb = nv * (src->authored_normals ? 8 : 5) * 4, ib = (size_t)src->nidx * 2;
        N2Mesh *m = &ms[k]; N2SharedGeometry *g = &geoms[k];
        *m = *src;
        m->verts = (float *)(geo + gp); memcpy(m->verts, src->verts, vb); gp += vb;
        m->idx = (uint16_t *)(geo + gp); memcpy(m->idx, src->idx, ib); gp += (ib + 3) & ~(size_t)3;
        if (src->vcol) { m->vcol = geo + gp; memcpy(m->vcol, src->vcol, nv * 4); gp += nv * 4; }
        g->refs = 1; g->verts = m->verts; g->idx = m->idx; g->vcol = m->vcol;
        g->block = blk; blk->refs++;
        m->shared_geometry = g;
        k++;
    }
    size_t bytes = total + (size_t)n * (sizeof *geoms + sizeof *ms);
    wt_release(t);
    t->m = ms; t->n = n; t->bytes = bytes; t->ground = 1;
    if (__atomic_sub_fetch(&blk->refs, 1, __ATOMIC_ACQ_REL) == 0) n2_block_free(blk);
    return 1;
}

static void wt_forget(WtLoaded *t) {
    if (g_wt_state) {
        int i = wt_index_of(t->tx, t->ty);
        if (i >= 0) { wt_up_drop(i); g_wt_owned_ground[i] = 0; g_wt_state[i] = 0; }
    }
    wt_release(t);
}

static WtLoaded *wt_get(const char *track, int idx, int tx, int ty, int mode) {
    for (int i = 0; i < g_wt_nloaded; i++)
        if (g_wt_loaded[i].tx == tx && g_wt_loaded[i].ty == ty) return &g_wt_loaded[i];
    if (g_wt_nloaded == g_wt_capload) {
        int cap = g_wt_capload ? g_wt_capload * 2 : 128;
        WtLoaded *g = (WtLoaded *)realloc(g_wt_loaded, (size_t)cap * sizeof *g);
        if (!g) return NULL;
        g_wt_loaded = g; g_wt_capload = cap;
    }
    WtLoaded *t = &g_wt_loaded[g_wt_nloaded];
    if (!wt_take(track, idx, tx, ty, t, mode)) return NULL;
    g_wt_nloaded++;
    return t;
}

static int wt_push(N2Scene *s, const N2Mesh *src, int role, int drop_vcol) {
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 1024;
        N2Mesh *g = (N2Mesh *)realloc(s->meshes, (size_t)cap * sizeof *g);
        if (!g) return 0;
        s->meshes = g; s->cap = cap;
    }
    N2Mesh m = *src;
    m.world_role = (unsigned char)role;
    if (drop_vcol) m.vcol = NULL;
    __atomic_add_fetch(&m.shared_geometry->refs, 1, __ATOMIC_RELAXED);
    s->meshes[s->count++] = m;
    return 1;
}

/* Buildings arriving at the edge: which tiles gave objects to the last
   assembly, so this one can mark the ones that are new (N2Mesh.fade_in; main
   fades them in instead of popping). A first build, or one that is mostly new
   (a teleport, a switch), marks nothing. */
static unsigned char *g_wt_obj_prev, *g_wt_obj_cur;
static int g_wt_obj_n, g_wt_obj_have_prev;

/* Same near/far rule as the instance build (WINST_LOD_NEAR_M). */
#define WT_LOD_NEAR_M 280.0f
/* Hysteresis: a tile with its full models keeps them until it is one 80 m
   build step past the LOD ring, so the swap back to stand-ins lands further
   from the car (the build centre leads it; the census saw stand-ins replace
   full models 100-200 m from the camera). Mostly behind the car: culled. */
#define WT_LOD_HYST_M 80.0f

static int wt_assemble_once(N2Scene *scene, N2Scene *vista, const char *troot, const char *track,
                            float fx, float fy, float radius, float object_radius,
                            int scenery_event, WInstStats *stats);
static WGSelection sel; static int sel_event = 0x7fffffff, sel_effective;
static int g_wt_last_count;   /* meshes in the last assembled scene (reserve) */
static void wt_forget_all(void) {
    for (int i = 0; i < g_wt_nloaded; i++) wt_forget(&g_wt_loaded[i]);
    g_wt_nloaded = 0;
}
static void wt_scene_drop(N2Scene *s) {
    for (int i = 0; i < s->count; i++) n2_mesh_geometry_release(&s->meshes[i]);
    free(s->meshes); memset(s, 0, sizeof *s);
}
void world_tiles_reset(void) {
    g_wt_obj_have_prev = 0;
    int was_paused = g_wt_paused;
    g_wt_paused = 1;
    __sync_synchronize();
#ifdef __vita__
    while (g_wt_prefetch_on && g_wt_pf_busy) SDL_Delay(5);   /* the read-ahead's tile in hand */
#endif
    wt_trim(1, 1);                          /* read-ahead tiles + decode scratch; the preload is for the next track */
    wt_forget_all();
    if (g_wt_have_global) { wt_release(&g_wt_global); g_wt_have_global = 0; }
    if (g_wt_have_donor > 0) wt_release(&g_wt_donor);
    g_wt_have_donor = 0;
    free(g_wt_loaded); g_wt_loaded = NULL; g_wt_capload = 0;
    for (int i = 0; g_wt_state && i < g_wt_nindex; i++) wt_up_drop(i);
    free((void *)g_wt_state); g_wt_state = NULL;
    free((void *)g_wt_ready); g_wt_ready = NULL;
    free((void *)g_wt_up); g_wt_up = NULL;
    free((void *)g_wt_upst); g_wt_upst = NULL;
    free((void *)g_wt_owned_ground); g_wt_owned_ground = NULL;
    free((void *)g_wt_raw); g_wt_raw = NULL; free(g_wt_rawlen); g_wt_rawlen = NULL; g_wt_raw_bytes = 0;
    g_wt_ready_bytes = 0; g_wt_aim_set = 0;
    free(g_wt_index); g_wt_index = NULL; g_wt_nindex = 0; g_wt_track[0] = 0;
    free(sel.items); memset(&sel, 0, sizeof sel); sel_event = 0x7fffffff; sel_effective = 0;
    g_wt_enabled = -1;                      /* decided again for the next track */
    /* "no pack" is asked again: the migration may have packed it meanwhile */
    wt_lock(&g_wt_pk_lock);
    for (int i = 0; i < 16; i++) if (g_wt_pk[i].used && !g_wt_pk[i].tab) g_wt_pk[i].probed = 0;
    wt_unlock(&g_wt_pk_lock);
    __sync_synchronize();
    g_wt_paused = was_paused;
    /* the read-ahead thread stays: the next track's first assembly sets up
       its state at once (wt_assemble_once), and a preload keeps filling */
    printf("world tiles: reset for a track switch%s\n", g_wt_pl_any ? " (preload kept)" : "");
}
int world_tiles_assemble(N2Scene *scene, N2Scene *vista, const char *troot, const char *track,
                         float fx, float fy, float radius, int scenery_event, WInstStats *stats) {
    __sync_fetch_and_add(&g_wt_assembling, 1);   /* the migration keeps off the card */
    int n = wt_assemble_once(scene, vista, troot, track, fx, fy, radius, radius, scenery_event, stats);
    if (n <= 0) {
        /* Keep overlapping tiles and road coverage. Re-reading the whole
           neighborhood leaves the car driving toward a stale resident. */
        wt_scene_drop(scene); wt_scene_drop(vista);
        float object_radius = radius * 0.6f;
        printf("world tiles: retrying with scenery radius %.0f m, road and terrain radius %.0f m\n",
               object_radius, radius + 60.0f);
        n = wt_assemble_once(scene, vista, troot, track, fx, fy, radius, object_radius,
                             scenery_event, stats);
        if (n <= 0) { wt_scene_drop(scene); wt_scene_drop(vista); }
    }
    g_wt_co_on = 0;
    __sync_fetch_and_sub(&g_wt_assembling, 1);
    /* the loose-file packer runs at boot (world_tiles_migrate_sync), not here */
    return n;
}
static int wt_assemble_once(N2Scene *scene, N2Scene *vista, const char *troot, const char *track,
                            float fx, float fy, float radius, float object_radius,
                            int scenery_event, WInstStats *stats) {
    if (!scene || !vista || !wt_index_load(track)) return 0;
    /* 11-14 hwlog55: the mesh array doubled by realloc mid-build (6 MB at
       16k meshes) and failed with 35 MB free in holes, sending the build to
       the 240 m retry. Reserve the last build's count + 1/8 up front, so the
       one big block is asked for while the old scene still holds its own. */
    {
        int want = g_wt_last_count + g_wt_last_count / 8;
        if (want < 4096) want = 4096;
        if (scene->cap < want) {
            N2Mesh *g = (N2Mesh *)realloc(scene->meshes, (size_t)want * sizeof *g);
            if (g) { scene->meshes = g; scene->cap = want; }
        }
    }
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_SIM_TILEFIND=<name part> -- every tile mesh whose name has it */
        static int done;
        const char *tf = getenv("OPENUG2_SIM_TILEFIND");
        if (!done && tf) {
            done = 1;
            for (int k = 0; k < g_wt_nindex; k++) {
                const WtIndexEntry *e = &g_wt_index[k];
                WtLoaded t; memset(&t, 0, sizeof t);
                if (!wt_read_decode(track, e->x, e->y, &t, 0)) continue;
                for (int i = 0; i < t.n; i++) {
                    const N2Mesh *m = &t.m[i];
                    if (!strstr(m->sname, tf)) continue;
                    const float *b = m->world_bounds;
                    printf("tilefind: tile (%d %d) %s cat=%d role=%d tex %08x verts %d tris %d scen %d %d:%d box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
                           e->x, e->y, m->sname, m->cat, m->world_role, m->texkey, m->nverts, m->nidx / 3,
                           m->scen_has, m->scen_region, m->scen_index, b[0], b[1], b[2], b[3], b[4], b[5]);
                }
                wt_release(&t);
            }
            printf("tilefind: done\n");
        }
    }
    {   /* test aid (simulator): OPENUG2_SKY_CENSUS -- the skyline's candidates over every tile */
        static int done;
        if (!done && getenv("OPENUG2_SKY_CENSUS")) {
            done = 1;
            static const float H[5] = { 0, 10, 20, 45, 90 };
            long cnt[3][5] = {{0}}, vtx[3][5] = {{0}}, tri[3][5] = {{0}}, tiles = 0;
            float zmin = 1e9f; int texn[5] = {0};
            for (int k = 0; k < g_wt_nindex; k++) {
                const WtIndexEntry *e = &g_wt_index[k];
                if (e->x == -32768) continue;
                WtLoaded t; memset(&t, 0, sizeof t);
                if (!wt_read_decode(track, e->x, e->y, &t, 0)) continue;
                tiles++;
                for (int i = 0; i < t.n; i++) {
                    const N2Mesh *m = &t.m[i];
                    if (m->cat == N2_ROAD || m->cat == N2_TERRAIN || m->cat == N2_SKY) continue;
                    int r = m->world_role == 2 ? 0 : m->world_role == 0 ? 1 : 2;
                    float top = m->world_bounds[5];
                    if (m->world_bounds[2] < zmin) zmin = m->world_bounds[2];
                    float h = m->world_bounds[5] - m->world_bounds[2];
                    (void)top;
                    for (int j = 0; j < 5; j++) if (h >= H[j]) { cnt[r][j]++; vtx[r][j] += m->nverts; tri[r][j] += m->nidx / 3; }
                    for (int j = 0; j < 5; j++) if (h >= H[j] && r != 2) {
                        static uint32_t keys[5][4096]; static int nk[5];
                        int f = 0; for (int q = 0; q < nk[j] && !f; q++) f = keys[j][q] == m->texkey;
                        if (!f && nk[j] < 4096) keys[j][nk[j]++] = m->texkey;
                        if (k == g_wt_nindex - 1 || 1) texn[j] = nk[j];
                    }
                }
                wt_release(&t);
            }
            {   /* ring census: what the tiles 460..652 m from this first build's centre hold */
                double gb = 0, sb[4] = {0}, ob = 0; int nt = 0;
                static const float RH[4] = { 0, 10, 20, 45 };
                for (int k = 0; k < g_wt_nindex; k++) {
                    const WtIndexEntry *e = &g_wt_index[k];
                    if (e->x == -32768) continue;
                    float d2 = wt_tile_d2(e, fx, fy);
                    if (d2 <= 460.0f * 460.0f || d2 > 652.0f * 652.0f) continue;
                    WtLoaded t; memset(&t, 0, sizeof t);
                    if (!wt_read_decode(track, e->x, e->y, &t, 0)) continue;
                    nt++;
                    for (int i = 0; i < t.n; i++) {
                        const N2Mesh *m = &t.m[i];
                        double by = (double)m->nverts * (m->authored_normals ? 32 : 20) + m->nidx * 2 + (m->vcol ? m->nverts * 4 : 0) + 240;
                        float h = m->world_bounds[5] - m->world_bounds[2];
                        if (m->cat == N2_ROAD || m->cat == N2_TERRAIN) gb += by;
                        else if (m->world_role == 2) { for (int j = 0; j < 4; j++) if (h >= RH[j]) sb[j] += by; }
                        else ob += by;
                    }
                    wt_release(&t);
                }
                printf("ring census at (%.0f,%.0f): %d tiles, ground %.1f MB, stand-ins h>=0 %.1f h>=10 %.1f h>=20 %.1f h>=45 %.1f MB, other %.1f MB\n",
                       fx, fy, nt, gb / 1048576.0, sb[0] / 1048576.0, sb[1] / 1048576.0, sb[2] / 1048576.0, sb[3] / 1048576.0, ob / 1048576.0);
            }
            printf("sky census: %ld tiles, lowest z %.0f, textures %d %d %d %d %d\n", tiles, zmin, texn[0], texn[1], texn[2], texn[3], texn[4]);
            for (int r = 0; r < 3; r++) for (int j = 0; j < 5; j++)
                printf("sky census: role %s h>=%.0f: %ld meshes, %ld verts, %ld tris\n",
                       r == 0 ? "standin" : r == 1 ? "plain" : "full", H[j], cnt[r][j], vtx[r][j], tri[r][j]);
        }
    }
#endif
    unsigned t0 = wt_ms();
    long long read0 = g_wt_bytes_read, hits0 = g_wt_prefetch_hits, rawh0 = g_wt_raw_hits;
    long long npack0 = g_wt_n_pack, nloose0 = g_wt_n_loose, npre0 = g_wt_n_pre;
    /* read-ahead state from the first build of a track on (not only after
       it): with no aim yet (a switch, the boot), it works round this build's
       own centre beside the loader */
    g_wt_radius = radius;
    g_wt_object_radius = object_radius;
    if (!g_wt_aim_set) world_tiles_aim(fx, fy);
    wt_prefetch_start();
    g_wt_read_ms = g_wt_decode_ms = 0;
    g_wt_up_ready = g_wt_up_now = 0;
    if (!g_wt_have_global) {
        g_wt_have_global = wt_read_decode(track, -32768, -32768, &g_wt_global, 0);
        if (!g_wt_have_global) return 0;
    }
    if (!g_wt_have_donor && beyond_is_track(track)) {
        wt_pack_probe("STREAML4RA", 0);
        g_wt_have_donor = wt_read_decode("STREAML4RA", -32768, -32768, &g_wt_donor, 0) ? 1 : -1;
        int ns = 0; float bb[6] = { 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f };
        for (int i = 0; g_wt_have_donor > 0 && i < g_wt_donor.n; i++) {
            const N2Mesh *m = &g_wt_donor.m[i];
            if (m->cat != N2_SKY) continue;
            ns++;
            for (int a = 0; a < 3; a++) { if (m->world_bounds[a] < bb[a]) bb[a] = m->world_bounds[a];
                                          if (m->world_bounds[a+3] > bb[a+3]) bb[a+3] = m->world_bounds[a+3]; }
        }
        printf("world tiles: %s sky borrowed from STREAML4RA: %d meshes, (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
               track, ns, bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]);
    }
    /* scenery selection for this event, kept between builds */
    if (sel_event != scenery_event) {
        free(sel.items); memset(&sel, 0, sizeof sel);
        sel_event = scenery_event; sel_effective = scenery_event;
        if (scenery_event) {
            const char *stem = !strncmp(track, "STREAM", 6) ? track + 6 : track;
            char path[1024]; snprintf(path, sizeof path, "%s/%s.BUN", troot, stem);
            long len = 0; unsigned char *data = n2_read_file(path, &len);
            WGTable table;
            int valid = data && wg_open_file(data, (size_t)len, &table);
            if (valid && !wg_event_group_present(&table, scenery_event)) sel_effective = 0;
            if (valid && sel_effective && !wg_selection_open(&table, sel_effective, &sel)) {
                printf("world tiles: scenery selection for event %d failed\n", scenery_event);
                memset(&sel, 0, sizeof sel);
            }
            free(data);
        }
    }
    long hidden = 0, loaded_now = 0;
    for (int i = 0; i < g_wt_global.n; i++) {   /* sky into the scene, vista apart */
        const N2Mesh *m = &g_wt_global.m[i];
        if (!(m->cat == N2_SKY ? wt_push(scene, m, m->world_role, 0) : wt_push(vista, m, m->world_role, 0)))
            return 0;
    }
    for (int i = 0; g_wt_have_donor > 0 && i < g_wt_donor.n; i++)   /* (beyond.h) */
        if (g_wt_donor.m[i].cat == N2_SKY && !wt_push(scene, &g_wt_donor.m[i], g_wt_donor.m[i].world_role, 0)) return 0;
    float reach = radius + 60.0f;   /* ground is placed out to radius + 60 */
    float far_reach = reach + wt_far_extra();   /* stand-ins only from here (the far ring) */
    /* Release the cache's references before acquiring any more tiles.
       Residents keep their own references until the swap and retirement. */
    int kept = 0, downgraded = 0, regraded = 0;
    for (int i = 0; i < g_wt_nloaded; i++) {
        WtLoaded *t = &g_wt_loaded[i];
        int k = wt_index_of(t->tx, t->ty);
        if (k < 0 || wt_tile_d2(&g_wt_index[k], fx, fy) > far_reach * far_reach) {
            wt_forget(t);
            continue;
        }
        if (object_radius < radius && !t->ground &&
            !wt_objects_at(&g_wt_index[k], fx, fy, object_radius) && wt_downgrade(t)) {
            if (g_wt_owned_ground) g_wt_owned_ground[k] = 1;
            downgraded++;
        }
        g_wt_loaded[kept++] = *t;
    }
    g_wt_nloaded = kept;
    if (g_wt_obj_n != g_wt_nindex) {
        free(g_wt_obj_prev); free(g_wt_obj_cur);
        g_wt_obj_prev = (unsigned char *)calloc((size_t)(g_wt_nindex ? g_wt_nindex : 1), 1);
        g_wt_obj_cur = (unsigned char *)calloc((size_t)(g_wt_nindex ? g_wt_nindex : 1), 1);
        g_wt_obj_n = g_wt_obj_prev && g_wt_obj_cur ? g_wt_nindex : 0;
        g_wt_obj_have_prev = 0;
    }
    if (g_wt_obj_n) memset(g_wt_obj_cur, 0, (size_t)g_wt_obj_n);
    int scene0 = scene->count, obj_tiles = 0, obj_new = 0, faded = 0, xfaded = 0;
    g_wt_co_x = fx; g_wt_co_y = fy; g_wt_co_obj = object_radius; g_wt_co_reach = reach; g_wt_co_far = far_reach;
    __sync_synchronize();
    g_wt_co_on = !getenv("OPENUG2_NO_CODECODE");
    for (int k = 0; k < g_wt_nindex; k++) {
        const WtIndexEntry *e = &g_wt_index[k];
        /* a tile whose meshes' union box touches the reach circle */
        if (e->x == -32768 || wt_tile_d2(e, fx, fy) > far_reach * far_reach) continue;
        /* edge tiles (centre beyond the radius) give only their ground:
           decoded ground only, and upgraded once their buildings are due */
        int tile_objects = wt_objects_at(e, fx, fy, object_radius);
        int had = g_wt_nloaded;
        int in_reach = wt_tile_d2(e, fx, fy) <= reach * reach;
        WtLoaded *t = wt_get(track, k, e->x, e->y, tile_objects ? 0 : in_reach ? WT_GROUND_ONLY : WT_FAR_ONLY);
        if (!t) return 0;
        loaded_now += g_wt_nloaded > had;
        if (tile_objects && t->ground && !wt_upgrade(track, k, t)) return 0;
        if (!tile_objects && in_reach && t->ground == WT_FAR_ONLY) { if (!wt_regrade(track, t)) return 0; regraded++; }
        /* Whole-tile decisions: a world batch is one tile's run of one
           material, so its GPU buffers carry over to the next build only if
           the tile contributes exactly the same meshes. Per-mesh radius and
           LOD tests changed edge and LOD-ring tiles on every 80 m step.
           Objects go by tile CENTRE (same area as the per-mesh test; "union box
           touches the circle" was 1.6x the meshes and ran the heap out). Ground
           keeps the exact per-mesh reach so support never ends short of the
           safety net; only edge ground batches change between builds. */
        float tcx = ((float)e->x + 0.5f) * WTILE_SIZE - fx;
        float tcy = ((float)e->y + 0.5f) * WTILE_SIZE - fy;
        float tc2 = tcx*tcx + tcy*tcy;
        int was_near = g_wt_obj_n && g_wt_obj_have_prev && (g_wt_obj_prev[k] & 2);
        int near = tc2 < WT_LOD_NEAR_M * WT_LOD_NEAR_M ||
                   (was_near && tc2 < (WT_LOD_NEAR_M + WT_LOD_HYST_M) * (WT_LOD_NEAR_M + WT_LOD_HYST_M));
        int fresh = 0, xfade = 0;
        if (g_wt_obj_n) {   /* every tile giving buildings or stand-ins: new ones fade in */
            g_wt_obj_cur[k] = (unsigned char)(1 | (near && tile_objects ? 2 : 0));   /* 2: its full models drawn */
            obj_tiles++;
            fresh = g_wt_obj_have_prev && !g_wt_obj_prev[k];
            obj_new += fresh;
            /* its full models just replaced its stand-ins: the stand-ins stay
               this one build, fading out over them (N2Mesh.fade_in 2, main.c) */
            xfade = !fresh && g_wt_obj_have_prev && near && tile_objects && !was_near;
        }
        for (int i = 0; i < t->n; i++) {
            const N2Mesh *m = &t->m[i];
            const float *b = m->world_bounds;
            int ground = m->cat == N2_ROAD || m->cat == N2_TERRAIN;
            if (ground ? wt_rect_d2(b[0], b[1], b[3], b[4], fx, fy) > reach * reach
                       : !tile_objects && m->world_role != 2) continue;   /* a tile without buildings: stand-ins */
            if (m->scen_has && sel.count && !wg_selection_visible(&sel, m->scen_region, m->scen_index)) {
                hidden++; continue;
            }
            if (!m->scen_has && ground && sel.count) {   /* tiles built before 11-18: an event-grouped
                                                           identity chunk by name (world_instance.c) */
                const uint16_t (*idr)[2] = NULL; int nidr = 0, vis = 0;
                world_instance_ident_refs(troot, track, m->aname, &idr, &nidr);
                for (int k = 0; k < nidr && !vis; k++) vis = wg_selection_visible(&sel, idr[k][0], idr[k][1]);
                if (nidr && !vis) { hidden++; continue; }
            }
            /* Carbon's reflection barriers (Malibu*_ReflectionBarrier): geometry for
               its reflection pass only. Drawn, they were a giant black wall in
               Palmont (11-19). Packs converted before then still hold them;
               nfsu2.h drops them from new conversions. Bayview has none. */
            if (strstr(m->sname, "REFLECTIONBARRI") && m->world_role != 1 && m->world_role != 4) continue;
            int role = m->world_role, drop_vcol = 0;
            if (role == 1 || role == 2) {   /* a LOD pair: full model (1) + stand-in (2) */
                if (near && role == 2) {
                    if (!xfade || ground) continue;
                    if (!wt_push(scene, m, 2, 0)) return 0;
                    scene->meshes[scene->count - 1].fade_in = 2; xfaded++;
                    continue;
                }
                if (near) role = 0; else if (role == 1) drop_vcol = 1;
            }
            /* a smokeable's scenery instance: kept (textures, its meshes) but
               drawn by smokeable.c, not batched */
            if (role != 1 && role != 4 && m->scen_has && smk_is_prop(m->scen_region, m->scen_index)) role = 3;
            if (!wt_push(scene, m, role, drop_vcol)) return 0;
            if (fresh && !ground && role != 3) { scene->meshes[scene->count - 1].fade_in = 1; faded++; }
        }
    }
    if (faded && obj_new * 3 > obj_tiles) {   /* mostly new: a teleport, not the edge moving */
        for (int i = scene0; i < scene->count; i++) if (scene->meshes[i].fade_in == 1) scene->meshes[i].fade_in = 0;
        faded = 0;
    }
    if (g_wt_obj_n) {
        unsigned char *sw = g_wt_obj_prev; g_wt_obj_prev = g_wt_obj_cur; g_wt_obj_cur = sw;
        g_wt_obj_have_prev = 1;
    }
    if (faded) printf("world tiles: %d new edge tiles, %d meshes fade in\n", obj_new, faded);
    if (xfaded) printf("world tiles: %d stand-ins fade out over their full models\n", xfaded);
    /* keep only the tiles this build used: meshes the resident uses stay
       alive through its own references anyway */
    int kept_ground = 0, kept_far = 0; size_t kept_bytes = 0, far_bytes = 0;
    for (int i = 0; i < g_wt_nloaded; i++) {
        WtLoaded *t = &g_wt_loaded[i];
        int k = wt_index_of(t->tx, t->ty);
        /* well behind the object radius (half a tile past it, so a tile on
           the edge does not flip back and forth; the kept band ends only
           ~150 m past the radius): ground only from here on */
        if (!t->ground && !wt_objects_at(&g_wt_index[k], fx, fy, object_radius + 0.5f * WTILE_SIZE) && wt_downgrade(t)) {
            if (g_wt_owned_ground) g_wt_owned_ground[k] = 1;
            downgraded++;
        }
        kept_bytes += t->bytes; kept_ground += t->ground != 0;
        if (t->ground == WT_FAR_ONLY) { kept_far++; far_bytes += t->bytes; }
    }
    if (stats) {
        memset(stats, 0, sizeof *stats);
        snprintf(stats->bundle, sizeof stats->bundle, "%s", track);
        stats->scenery_effective = sel_effective;
        stats->scenery_hidden = hidden;
        stats->meshes_placed = scene->count;
    }
    g_wt_radius = radius;
    wt_prefetch_start();
    /* reads by the loader itself, by source: pack / loose file / preloaded bytes */
    printf("world tiles: %d meshes at (%.0f, %.0f) r %.0f: %ld new tiles (%lld ready from read-ahead, %lld from its stored bytes, "
           "%lld KB read now: read %u ms, decode %u ms; %lld pack, %lld loose, %lld preloaded), %d kept "
           "(%u KB, %d ground only; upgrades %d ready, %d read now; %d cut to ground; far ring %d tiles %u KB, %d regraded), %ld hidden by scenery, %u ms\n",
           scene->count, fx, fy, radius, loaded_now, (long long)(g_wt_prefetch_hits - hits0),
           (long long)(g_wt_raw_hits - rawh0), (g_wt_bytes_read - read0) >> 10, g_wt_read_ms, g_wt_decode_ms, g_wt_n_pack - npack0,
           g_wt_n_loose - nloose0, g_wt_n_pre - npre0, kept, (unsigned)(kept_bytes >> 10),
           kept_ground, g_wt_up_ready, g_wt_up_now, downgraded, kept_far, (unsigned)(far_bytes >> 10), regraded, hidden, wt_ms() - t0);
    printf("world tiles: geometry %lld KB live: %u KB this cache, %lld KB read-ahead (+%lld KB stored bytes), %lld KB held by older areas\n",
           (long long)(g_wt_geo_live >> 10), (unsigned)(kept_bytes >> 10), (long long)(g_wt_ready_bytes >> 10),
           (long long)(g_wt_raw_bytes >> 10),
           (long long)((g_wt_geo_live - (long long)kept_bytes - g_wt_ready_bytes) >> 10));
    printf("world tiles: read-ahead misses so far: %ld in reach of its aim, %ld out of reach, %ld full decodes among them, %ld waited on mid-prepare; aim (%.0f, %.0f) vs build (%.0f, %.0f)\n",
           g_wt_miss_in, g_wt_miss_out, g_wt_miss_full, g_wt_miss_busy, (double)g_wt_aim[0], (double)g_wt_aim[1], fx, fy);
    printf("world tiles: read-ahead turns so far: off %ld, texture fetch %ld, cap %ld, short room %ld, nothing in reach %ld, prepared %ld, co-decoded %ld; %lld KB ready now\n",
           g_wt_ra_turn[0], g_wt_ra_turn[1], g_wt_ra_turn[2], g_wt_ra_turn[3], g_wt_ra_turn[4], g_wt_ra_turn[5], g_wt_co_tiles, (long long)(g_wt_ready_bytes >> 10));
    wt_preload_after(track, fx, fy, radius);
    VMARK("  tiles assembled: %d meshes, %ld new tiles", scene->count, loaded_now);
    g_wt_last_count = scene->count;
    return scene->count;
}

/* Every stand-in of the track at least minh tall, tile by tile, for the
   always-resident skyline (skyline.c). Its own index copy, the preload's pack
   handle slot (a second handle if that is busy) and plain allocations, so it
   runs beside the loader and the read-ahead; a short pause per tile leaves
   the card to the streaming. Returns the tiles read, -1 without a tile set. */
int world_tiles_each_standin(const char *track, float minh,
                             void (*cb)(void *ctx, const N2Mesh *m), void *ctx,
                             SDL_atomic_t *abort_flag) {
    WtIndexEntry *idx = NULL; int n = 0;
    if (!wt_index_read(track, &idx, &n)) return -1;
    wt_pack_probe(track, 0);   /* the pack opens with the streaming; the Prep Tool has none */
    int read = 0;
    for (int i = 0; i < n; i++) {
        if (abort_flag && SDL_AtomicGet(abort_flag)) break;
        if (idx[i].x == -32768) continue;   /* the track's global entry */
        long len = 0; int packed = 0;
        unsigned char *data = wt_fetch(track, idx[i].x, idx[i].y, 2, NULL, &len, &packed);
        if (!data) continue;
        WtLoaded L;
        int ok = wt_decode(data, len, idx[i].x, idx[i].y, &L, NULL, WT_FAR_ONLY);
        free(data);
        if (!ok) continue;
        read++;
        for (int k = 0; k < L.n; k++) {
            const N2Mesh *m = &L.m[k];
            if (m->world_role != 2 || !m->world_bounds_valid) continue;
            if (m->world_bounds[5] - m->world_bounds[2] < minh) continue;
            cb(ctx, m);
        }
        wt_release(&L);
        SDL_Delay(4);
    }
    free(idx);
    return read;
}
