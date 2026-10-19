/* vinyl.c -- see vinyl.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vinyl.h"
#include "nfsu2.h"
#include <SDL.h>
unsigned g_vt[6];   /* stage times (ms): open, vinyl decode, vinyl blend, door load, door blend, out */

#define VINYL_MAX 1024
static VinylPart g_vp[VINYL_MAX];
static int g_nvp, g_vp_loaded;
#define DECAL_MAX 256
static DecalPart g_dp[DECAL_MAX];
static int g_ndp;

static uint32_t vhash(const char *s) {
    uint32_t h = 0xffffffffu;
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return h;
}

int vinyl_db_load(const unsigned char *db, long len) {
    if (g_vp_loaded) return g_nvp;
    long str = -1, off = -1, attr = -1, parts = -1, plen = 0;
    for (long q = 0; q + 8 <= len; ) {
        uint32_t ci = n2_u32(db + q), cz = n2_u32(db + q + 4);
        if (q + 8 + (long)cz > len) break;
        if (ci == 0x34606u) str = q + 8;
        if (ci == 0x3460Cu) off = q + 8;
        if (ci == 0x34605u) attr = q + 8;
        if (ci == 0x34604u) { parts = q + 8; plen = cz; }
        q += 8 + cz;
    }
    if (str < 0 || off < 0 || attr < 0 || parts < 0) return 0;
    g_nvp = 0;
    for (long q = parts; q + 14 <= parts + plen && g_nvp < VINYL_MAX; q += 14) {
        const unsigned char *r = db + q;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!n2_u32(r) && !soff) break;
        if (r[4] == 52 && aoff != 0xffffu && g_ndp < DECAL_MAX) {   /* DECAL_TEXTURE */
            DecalPart dp; memset(&dp, 0, sizeof dp);
            dp.hash = n2_u32(r);
            snprintf(dp.dbg, sizeof dp.dbg, "%s", (const char *)db + str + soff * 4L);
            const unsigned char *a = db + off + aoff * 2L;
            unsigned n = a[0] | a[1] << 8;
            for (unsigned k = 0; k < n && k < 64; k++) {
                unsigned ai = a[2 + 2 * k] | a[3 + 2 * k] << 8;
                uint32_t key = n2_u32(db + attr + ai * 8L), val = n2_u32(db + attr + ai * 8L + 4);
                if (key == 0x0019cbc0u) dp.name = val;           /* NAME */
                else if (key == 0xebb03e66u) dp.brand = val;     /* BRAND_NAME */
                else if (key == 0x4732da07u) dp.lang = val;      /* LANGUAGEHASH */
            }
            if (dp.name) g_dp[g_ndp++] = dp;
            continue;
        }
        if (r[4] != 50 || aoff == 0xffffu) continue;          /* VINYL */
        VinylPart v; memset(&v, 0, sizeof v);
        v.hash = n2_u32(r);
        unsigned upg = r[5] | r[6] << 8;
        v.group = (unsigned char)(upg & 0x1f);
        v.level = (unsigned char)((upg >> 5) & 7);
        snprintf(v.name, sizeof v.name, "%s", (const char *)db + str + soff * 4L);
        const unsigned char *a = db + off + aoff * 2L;
        unsigned n = a[0] | a[1] << 8;
        for (unsigned k = 0; k < n && k < 64; k++) {
            unsigned ai = a[2 + 2 * k] | a[3 + 2 * k] << 8;
            uint32_t key = n2_u32(db + attr + ai * 8L), val = n2_u32(db + attr + ai * 8L + 4);
            switch (key) {
            case 0xfd35fe70u:                                  /* TEXTURE */
                if (val != 0xffffffffu) snprintf(v.tex, sizeof v.tex, "%s", (const char *)db + str + val * 4L);
                break;
            case 0x039dd714u: v.remap = (unsigned char)(val != 0); break;   /* REMAP */
            case 0x6212682bu: v.ncol = (unsigned char)val; break;          /* NUMREMAPCOLOURS */
            case 0x564b8cb6u: v.unique = val == 0x40; break;               /* NUMCOLOURS */
            case 0x4732da07u: v.lang = val; break;                         /* LANGUAGEHASH */
            }
        }
        if (!v.tex[0]) continue;
        g_vp[g_nvp++] = v;
    }
    g_vp_loaded = 1;
    printf("vinyl: %d vinyl parts, %d decals in the part database\n", g_nvp, g_ndp);
    return g_nvp;
}

int decal_count(void) { return g_ndp; }
const DecalPart *decal_at(int i) { return i >= 0 && i < g_ndp ? &g_dp[i] : NULL; }
const DecalPart *decal_by_name(uint32_t name) {
    for (int i = 0; name && i < g_ndp; i++) if (g_dp[i].name == name) return &g_dp[i];
    return NULL;
}

int vinyl_count(void) { return g_nvp; }
const VinylPart *vinyl_at(int i) { return i >= 0 && i < g_nvp ? &g_vp[i] : NULL; }
const VinylPart *vinyl_find(uint32_t hash) {
    for (int i = 0; hash && i < g_nvp; i++) if (g_vp[i].hash == hash) return &g_vp[i];
    return NULL;
}

/* ------------------------------------------------------------ VINYLS.BIN */

/* One 8-bit palettised slot (HUFF, format 0x29): the base level's indices and
   the palette, 256 BGRA entries 64 bytes past the record's offset +0x2c. */
typedef struct { uint32_t key; int w, h; unsigned char *idx; unsigned char pal[256][4]; } P8;

/* The open VINYLS.BIN, kept between composites while the same car's skin is
   rebuilt (the shop's list rebuilds it on every cursor move), and the last
   few decoded slots: moving through the list decodes only the new vinyl. */
#define P8_CACHE 6
static struct {
    char path[512]; FILE *f; unsigned char *head; long hlen; const unsigned char *slots; uint32_t nslot;
    P8 p8[P8_CACHE]; unsigned age[P8_CACHE], clock;
} g_vf;

static void vfile_close(void) {
    if (g_vf.f) fclose(g_vf.f);
    free(g_vf.head);
    for (int i = 0; i < P8_CACHE; i++) free(g_vf.p8[i].idx);
    memset(&g_vf, 0, sizeof g_vf);
}
static int vfile_open(const char *path) {
    if (g_vf.f && !strcmp(g_vf.path, path)) return 1;
    vfile_close();
    g_vf.f = fopen(path, "rb");
    if (!g_vf.f) return 0;
    snprintf(g_vf.path, sizeof g_vf.path, "%s", path);
    /* the header block (0xb3310000) follows the 0xb3300000 chunk's first child */
    long want = 0x20000;
    g_vf.head = (unsigned char *)malloc((size_t)want);
    if (!g_vf.head) { vfile_close(); return 0; }
    g_vf.hlen = (long)fread(g_vf.head, 1, (size_t)want, g_vf.f);
    g_vf.slots = n2_tpk_slots(g_vf.head, g_vf.hlen, &g_vf.nslot);
    if (!g_vf.slots || (long)(g_vf.slots - g_vf.head) + (long)g_vf.nslot > g_vf.hlen) { vfile_close(); return 0; }
    return 1;
}

static int p8_decode(uint32_t key, P8 *out) {
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i + 0x18 <= g_vf.nslot; i += 0x18) {
        const unsigned char *s = g_vf.slots + i;
        if (n2_u32(s) != key) continue;
        long absoff = (long)n2_u32(s + 4), enc = (long)n2_u32(s + 8), dec = (long)n2_u32(s + 12);
        if (enc < 20 || dec <= 144 || dec > (8L << 20)) return 0;
        unsigned char *src = (unsigned char *)malloc((size_t)enc), *raw = (unsigned char *)malloc((size_t)dec);
        int ok = src && raw && fseek(g_vf.f, absoff, SEEK_SET) == 0 &&
                 (long)fread(src, 1, (size_t)enc, g_vf.f) == enc && !memcmp(src, "HUFF", 4) &&
                 n2_huff(src + 16, enc - 16, raw, dec) == dec;
        free(src);
        if (ok) {
            const unsigned char *rec = raw + dec - 144;
            int w = rec[0x38] | rec[0x39] << 8, h = rec[0x3a] | rec[0x3b] << 8;
            long po = (long)n2_u32(rec + 0x2c) + 64;
            ok = n2_u32(rec + 0x18) == key && w >= 8 && h >= 8 && w <= 1024 && h <= 1024 &&
                 (long)w * h <= po && po + 1024 <= dec - 144 + 12;
            if (ok) {
                out->key = key; out->w = w; out->h = h;
                memcpy(out->pal, raw + po, 1024);
                out->idx = raw; raw = NULL;          /* the indices lead the payload */
            }
        }
        free(raw);
        return ok;
    }
    return 0;
}
/* the slot, from the cache or decoded into it (least recently used out) */
static const P8 *p8_get(uint32_t key) {
    int slot = -1;
    for (int i = 0; i < P8_CACHE; i++)
        if (g_vf.p8[i].idx && g_vf.p8[i].key == key) { g_vf.age[i] = ++g_vf.clock; return &g_vf.p8[i]; }
    for (int i = 0; i < P8_CACHE; i++)
        if (!g_vf.p8[i].idx) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < P8_CACHE; i++) if (g_vf.age[i] < g_vf.age[slot]) slot = i;
        free(g_vf.p8[slot].idx);
    }
    if (!p8_decode(key, &g_vf.p8[slot])) { memset(&g_vf.p8[slot], 0, sizeof g_vf.p8[slot]); return NULL; }
    g_vf.age[slot] = ++g_vf.clock;
    return &g_vf.p8[slot];
}

/* --------------------------------------------------------------- compose */

/* The overlay, premultiplied in 16 bits while it builds: P = colour * cover,
   A = cover (65535 = 1). A layer of colour S over it by t (0..65536):
   P = S t + P (1 - t), A = t + A (1 - t) -- 0x611b00's weighted sum with
   weights t and 1 - t, so the paint under it ends as paint * (1 - A). */
typedef struct { uint16_t p[3], a; } Px;

/* a mask value's weight: t = a/128 capped at 1, in 1/65536 */
static uint32_t mask_t(unsigned a) { return a >= 128 ? 65536u : a * 512u; }

static void blend_t(Px *d, const unsigned char s[3], uint32_t t) {
    uint32_t u = 65536u - t;
    for (int c = 0; c < 3; c++)
        d->p[c] = (uint16_t)(((uint32_t)s[c] * 257u * t + (uint32_t)d->p[c] * u) >> 16);
    d->a = (uint16_t)(((65535u * t) + (uint32_t)d->a * u) >> 16);
}

/* 0x611b00 with the art's R, G, B (/255) as the weights of colours 1..3:
   each term truncated, the sum capped at 255; weights at or under 1/255 skip */
static void remap(unsigned char out[3], const unsigned char w[3], const unsigned char col[3][3]) {
    int sum[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        if (w[k] <= 1) continue;
        float f = w[k] * (1.0f / 255.0f);
        for (int c = 0; c < 3; c++) sum[c] += (int)(f * col[k][c]);
    }
    for (int c = 0; c < 3; c++) out[c] = (unsigned char)(sum[c] > 255 ? 255 : sum[c]);
}

/* One layer onto the overlay at S x S. Where the art is larger than the skin
   (the Vita's 256 from 512 art), each skin texel takes the box of art texels
   under it: the mean weight, and their colours weighted by theirs. */
static void layer_blend(Px *px, int S, int aw, int ah, const unsigned char *aidx, const unsigned char *midx,
                        const unsigned char col[256][3], const uint32_t tt[256]) {
    int fx = aw / S, fy = ah / S;
    if (fx < 1 || fy < 1 || aw != fx * S || ah != fy * S) {   /* nearest */
        for (int y = 0; y < S; y++) {
            int sy = y * ah / S;
            for (int x = 0; x < S; x++) {
                long si = (long)sy * aw + x * aw / S;
                uint32_t t = tt[midx[si]];
                if (t) blend_t(&px[(long)y * S + x], col[aidx[si]], t);
            }
        }
        return;
    }
    int n = fx * fy;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            uint32_t tsum = 0, csum[3] = { 0, 0, 0 };
            for (int j = 0; j < fy; j++) {
                long row = (long)(y * fy + j) * aw + x * fx;
                for (int i = 0; i < fx; i++) {
                    uint32_t t = tt[midx[row + i]];
                    if (!t) continue;
                    const unsigned char *c = col[aidx[row + i]];
                    tsum += t;
                    uint32_t t8 = t >> 8;                /* 0..256: keeps the sums in 32 bits */
                    csum[0] += c[0] * t8; csum[1] += c[1] * t8; csum[2] += c[2] * t8;
                }
            }
            if (!tsum) continue;
            unsigned char s[3];
            uint32_t w8 = tsum >> 8;
            if (!w8) w8 = 1;
            for (int c = 0; c < 3; c++) { uint32_t v = csum[c] / w8; s[c] = (unsigned char)(v > 255 ? 255 : v); }
            blend_t(&px[(long)y * S + x], s, (tsum + (uint32_t)n / 2) / (uint32_t)n);
        }
}

/* The door lines don't change while a car is in the shop: decoded once per
   car (and kit / stock variant), kept as the texels they touch at the skin's
   size. */
typedef struct { uint32_t i, t; unsigned char rgb[3]; } DoorPx;
static struct { char name[96]; const unsigned char *src; int S; DoorPx *px; long n; int ok; } g_door;
static int door_get(const unsigned char *textures, long tlen, const char *car, const char *doorline, int S) {
    char nm[96];
    snprintf(nm, sizeof nm, "%s_DOORLINE%s", car, doorline);
    if (g_door.src == textures && g_door.S == S && !strcmp(g_door.name, nm)) return g_door.ok;
    free(g_door.px);
    memset(&g_door, 0, sizeof g_door);
    snprintf(g_door.name, sizeof g_door.name, "%s", nm);
    g_door.src = textures; g_door.S = S;
    N2Tex art, mask;
    int ok = n2_load_car_tex_by_key(textures, tlen, vhash(nm), &art);
    snprintf(nm, sizeof nm, "%s_DOORLINE%s_MASK", car, doorline);
    int okm = ok && n2_load_car_tex_by_key(textures, tlen, vhash(nm), &mask);
    if (ok && okm && art.rgb && mask.rgb && art.w == mask.w && art.h == mask.h) {
        /* the 32-bit art and the mask's green as a 1-texel palette each, so
           the layer path does the box: index = texel number is too big, so
           go through the planes directly */
        int fx = art.w / S, fy = art.h / S;
        int box = fx >= 1 && fy >= 1 && art.w == fx * S && art.h == fy * S;
        long cap = 4096, n = 0;
        DoorPx *dp = (DoorPx *)malloc((size_t)cap * sizeof *dp);
        for (int y = 0; dp && y < S; y++)
            for (int x = 0; x < S; x++) {
                uint32_t tsum = 0, csum[3] = { 0, 0, 0 }; int cnt = 0;
                int bx = box ? fx : 1, by = box ? fy : 1;
                for (int j = 0; j < by; j++)
                    for (int i = 0; i < bx; i++) {
                        long si = box ? (long)(y * fy + j) * art.w + x * fx + i
                                      : (long)(y * art.h / S) * art.w + x * art.w / S;
                        cnt++;
                        uint32_t t = mask_t(mask.rgb[si * 3 + 1]);
                        if (!t) continue;
                        tsum += t; uint32_t t8 = t >> 8;
                        for (int c = 0; c < 3; c++) csum[c] += art.rgb[si * 3 + c] * t8;
                    }
                if (!tsum) continue;
                if (n == cap) {
                    DoorPx *g = (DoorPx *)realloc(dp, (size_t)(cap *= 2) * sizeof *dp);
                    if (!g) { free(dp); dp = NULL; break; }
                    dp = g;
                }
                DoorPx *d = &dp[n++];
                uint32_t w8 = tsum >> 8; if (!w8) w8 = 1;
                d->i = (uint32_t)(y * S + x);
                d->t = (tsum + (uint32_t)cnt / 2) / (uint32_t)cnt;
                for (int c = 0; c < 3; c++) { uint32_t v = csum[c] / w8; d->rgb[c] = (unsigned char)(v > 255 ? 255 : v); }
            }
        if (dp) { g_door.px = dp; g_door.n = n; g_door.ok = 1; }
    } else printf("vinyl: %s door lines%s not found\n", car, doorline);
    if (ok) { free(art.rgb); free(art.alpha); free(art.dxt); }
    if (okm) { free(mask.rgb); free(mask.alpha); free(mask.dxt); }
    return g_door.ok;
}

void vinyl_cache_release(void) {
    vfile_close();
    free(g_door.px);
    memset(&g_door, 0, sizeof g_door);
}

unsigned char *vinyl_compose(const char *path,
                             const unsigned char *textures, long tlen,
                             const char *car, const VinylLayer layer[VINYL_LAYERS],
                             const char *doorline, int S, int *ow, int *oh) {
    *ow = *oh = 0;
    Px *px = NULL;
    int any = 0;
    char nm[96];
    uint32_t T = SDL_GetTicks(); memset(g_vt, 0, sizeof g_vt);
#define VT(i) do { uint32_t t2 = SDL_GetTicks(); g_vt[i] += t2 - T; T = t2; } while (0)
    /* the vinyls, bottom layer first */
    int need = 0;
    for (int l = 0; l < VINYL_LAYERS; l++) if (layer[l].part) need = 1;
    if (need) {
        if (path && vfile_open(path)) {
            VT(0);
            for (int l = 0; l < VINYL_LAYERS; l++) {
                const VinylPart *vp = layer[l].part;
                if (!vp) continue;
                snprintf(nm, sizeof nm, "%s_%s", car, vp->tex);
                const P8 *art = p8_get(vhash(nm));
                /* (the art's slot can be pushed out by the mask's: keep a copy of what's used) */
                int aw = art ? art->w : 0, ah = art ? art->h : 0;
                static unsigned char col[256][3];
                if (art)
                    for (int i = 0; i < 256; i++) {
                        const unsigned char *c = art->pal[i];   /* B G R A */
                        if (vp->remap) {
                            unsigned char wgt[3] = { c[2], c[1], c[0] };
                            remap(col[i], wgt, layer[l].col);
                        } else { col[i][0] = c[2]; col[i][1] = c[1]; col[i][2] = c[0]; }
                    }
                const unsigned char *aidx = art ? art->idx : NULL;
                uint32_t akey = art ? art->key : 0;
                snprintf(nm, sizeof nm, "%s_%s_MASK", car, vp->tex);
                const P8 *mask = art ? p8_get(vhash(nm)) : NULL;
                /* the mask's decode may have evicted the art: fetch it again */
                if (mask && art && (art->key != akey || art->idx != aidx)) { art = p8_get(akey); aidx = art ? art->idx : NULL; }
                VT(1);
                if (!art || !mask || mask->w != aw || mask->h != ah) {
                    printf("vinyl: %s_%s missing or unreadable\n", car, vp->tex);
                    continue;
                }
                static uint32_t tt[256];
                for (int i = 0; i < 256; i++) tt[i] = mask_t(mask->pal[i][0]);   /* grey: B = G = R */
                if (!px) px = (Px *)calloc((size_t)S * S, sizeof *px);
                if (!px) break;
                layer_blend(px, S, aw, ah, aidx, mask->idx, (const unsigned char (*)[3])col, tt);
                any = 1;
                VT(2);
            }
        } else if (path) printf("vinyl: %s unreadable\n", path);
    }
    /* layer 5: the door lines (32-bit art, the mask's green) */
    if (doorline && textures) {
        int ok = door_get(textures, tlen, car, doorline, S);
        VT(3);
        if (ok) {
            if (!px) px = (Px *)calloc((size_t)S * S, sizeof *px);
            if (px)
                for (long k = 0; k < g_door.n; k++)
                    blend_t(&px[g_door.px[k].i], g_door.px[k].rgb, g_door.px[k].t);
            any = 1;
        }
        VT(4);
    }
    if (!px || !any) { free(px); return NULL; }
    /* un-premultiply for mix(paint, rgb, a): colour = P / A through a
       reciprocal table (no divide per texel) */
    static uint32_t rcp[4097];
    if (!rcp[1]) for (int i = 1; i <= 4096; i++) rcp[i] = (uint32_t)(((uint64_t)255 << 24) / (uint64_t)(i * 16));
    unsigned char *out = (unsigned char *)malloc((size_t)S * S * 4);
    if (out)
        for (long i = 0; i < (long)S * S; i++) {
            const Px *p = &px[i];
            unsigned char *o = out + i * 4;
            if (!p->a) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            unsigned ai = (p->a + 8u) >> 4; if (!ai) ai = 1;
            for (int c = 0; c < 3; c++) {
                uint32_t v = (uint32_t)(((uint64_t)p->p[c] * rcp[ai] + (1u << 23)) >> 24);
                o[c] = (unsigned char)(v > 255 ? 255 : v);
            }
            o[3] = (unsigned char)(p->a >> 8);
        }
    free(px);
    /* the cut-away texels take a neighbour's colour, so filtering the edge of
       a design does not pull black into it */
    if (out)
        for (int pass = 0; pass < 2; pass++)
            for (int y = 0; y < S; y++)
                for (int x = 0; x < S; x++) {
                    unsigned char *o = out + ((long)y * S + x) * 4;
                    if (o[3]) continue;
                    static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
                    for (int k = 0; k < 4; k++) {
                        int nx = x + dx[k], ny = y + dy[k];
                        if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                        const unsigned char *q = out + ((long)ny * S + nx) * 4;
                        if (q[3] || (pass && (q[0] | q[1] | q[2]))) { memcpy(o, q, 3); break; }
                    }
                }
    VT(5);
    if (out) { *ow = S; *oh = S; }
    return out;
}
