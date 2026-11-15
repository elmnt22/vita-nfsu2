/* PERF_PLAN P2 (11-15): size/time study of the world tile pack (PC only).
 * usage: tilepak_bench <track>_v6.pak
 * For every tile: the stored size now (greedy LZ4), the size with an
 * optimal-ish LZ4 (hash chains + lazy matching; same block format, so the
 * Vita's decoder is unchanged), and the size of the decoded ("ready")
 * layout, compressed both ways. Times the decoders. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct { int16_t x, y; uint32_t off, len; } WtPackEnt;
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
#define WTM_RAWPOS 2
#define WTM_RAWUV  4

static int lz4_decompress(const uint8_t *src, int n, uint8_t *dst, int outn) {
    int ip = 0, op = 0;
    while (ip < n) {
        int tok = src[ip++], lit = tok >> 4;
        if (lit == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; lit += b; } while (b == 255); }
        if (lit > n - ip || lit > outn - op) return 0;
        memcpy(dst + op, src + ip, (size_t)lit); ip += lit; op += lit;
        if (ip >= n) break;
        if (n - ip < 2) return 0;
        int off = src[ip] | src[ip + 1] << 8; ip += 2;
        if (off == 0 || off > op) return 0;
        int ml = (tok & 15) + 4;
        if ((tok & 15) == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; ml += b; } while (b == 255); }
        if (ml > outn - op) return 0;
        const uint8_t *m = dst + op - off;
        for (int k = 0; k < ml; k++) dst[op + k] = m[k];
        op += ml;
    }
    return op == outn;
}


/* the same decoder with word copies (non-overlapping matches, literals) */
static int lz4_fast(const uint8_t *src, int n, uint8_t *dst, int outn) {
    int ip = 0, op = 0;
    while (ip < n) {
        int tok = src[ip++], lit = tok >> 4;
        if (lit == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; lit += b; } while (b == 255); }
        if (lit > n - ip || lit > outn - op) return 0;
        memcpy(dst + op, src + ip, (size_t)lit); ip += lit; op += lit;
        if (ip >= n) break;
        if (n - ip < 2) return 0;
        int off = src[ip] | src[ip + 1] << 8; ip += 2;
        if (off == 0 || off > op) return 0;
        int ml = (tok & 15) + 4;
        if ((tok & 15) == 15) { int b; do { if (ip >= n) return 0; b = src[ip++]; ml += b; } while (b == 255); }
        if (ml > outn - op) return 0;
        uint8_t *d = dst + op; const uint8_t *m = d - off;
        if (off >= ml) memcpy(d, m, (size_t)ml);
        else if (off >= 8) { int k = 0; for (; k + 8 <= ml; k += 8) memcpy(d + k, m + k, 8); for (; k < ml; k++) d[k] = m[k]; }
        else if (off == 1) memset(d, m[0], (size_t)ml);
        else for (int k = 0; k < ml; k++) d[k] = m[k];
        op += ml;
    }
    return op == outn;
}

/* hash-chain LZ4 with lazy matching (block format identical) */
static int emit(uint8_t *dst, int op, int cap, const uint8_t *lits, int lit, int off, int ml) {
    if (op + 1 + lit + lit / 255 + 1 + 2 + (ml >= 4 ? (ml - 4) / 255 + 1 : 0) > cap) return -1;
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
#define HBITS 16
static int32_t *g_head, *g_prev;
static int find(const uint8_t *s, int ip, int mlimit, int *off, int depth) {
    uint32_t seq; memcpy(&seq, s + ip, 4);
    int best = 0, bo = 0;
    for (int ref = g_head[(seq * 2654435761u) >> (32 - HBITS)]; ref >= 0 && ip - ref <= 65535 && depth-- > 0; ref = g_prev[ref & 0xffff]) {
        if (s[ref + best] != s[ip + best]) continue;
        uint32_t rs; memcpy(&rs, s + ref, 4); if (rs != seq) continue;
        int ml = 4; while (ip + ml < mlimit && s[ref + ml] == s[ip + ml]) ml++;
        if (ml > best) { best = ml; bo = ip - ref; if (ip + ml >= mlimit) break; }
    }
    *off = bo; return best;
}
static void ins(const uint8_t *s, int ip) {
    uint32_t seq; memcpy(&seq, s + ip, 4);
    uint32_t h = (seq * 2654435761u) >> (32 - HBITS);
    g_prev[ip & 0xffff] = g_head[h]; g_head[h] = ip;
}
static int lz4hc(const uint8_t *src, int n, uint8_t *dst, int cap, int depth) {
    if (!g_head) { g_head = malloc(sizeof(int32_t) << HBITS); g_prev = malloc(sizeof(int32_t) << 16); }
    for (int i = 0; i < (1 << HBITS); i++) g_head[i] = -1;
    int ip = 0, anchor = 0, op = 0;
    const int mflimit = n - 12, mlimit = n - 5;
    while (ip < mflimit) {
        int off, ml = find(src, ip, mlimit, &off, depth);
        if (ml < 4) { ins(src, ip); ip++; continue; }
        /* lazy: a longer match one byte on wins */
        ins(src, ip);
        if (ip + 1 < mflimit) {
            int off2, ml2 = find(src, ip + 1, mlimit, &off2, depth);
            if (ml2 > ml + 1) { ip++; ml = ml2; off = off2; }
        }
        op = emit(dst, op, cap, src + anchor, ip - anchor, off, ml);
        if (op < 0) return 0;
        for (int k = 1; k < ml && ip + k < mflimit; k++) ins(src, ip + k);
        ip += ml; anchor = ip;
    }
    op = emit(dst, op, cap, src + anchor, n - anchor, 0, 0);
    return op < 0 ? 0 : op;
}

/* the decoded layout as wt_decode builds it (verts f32 x5 [+3 normals], idx u16 padded, vcol) */
static size_t ready_layout(const uint8_t *pl, size_t plen, uint32_t nm, uint8_t *out) {
    size_t pos = 0, gp = 0;
    for (uint32_t k = 0; k < nm && pos + sizeof(WTileMesh) <= plen; k++) {
        WTileMesh h; memcpy(&h, pl + pos, sizeof h);
        memcpy(out + gp, &h, sizeof h); gp += sizeof h;   /* header kept */
        pos += sizeof h;
        int nv = (int)h.nverts;
        float *v = (float *)(out + gp);
        for (int c = 0; c < 5; c++) {
            int raw = c < 3 ? (h.flags & WTM_RAWPOS) : (h.flags & WTM_RAWUV);
            float inv = 1.0f / (c < 3 ? 1024.0f : 16384.0f);
            uint32_t acc = 0;
            const uint8_t *l0 = pl + pos, *l1 = l0 + nv, *l2 = l1 + nv, *l3 = l2 + nv;
            for (int i = 0; i < nv; i++) {
                uint32_t w = l0[i] | l1[i] << 8 | l2[i] << 16 | (uint32_t)l3[i] << 24;
                if (raw) memcpy(&v[i*5 + c], &w, 4);
                else { acc += w; v[i*5 + c] = (float)(int32_t)acc * inv; }
            }
            pos += (size_t)nv * 4;
        }
        if (h.authored_normals) { memcpy(v + (size_t)nv * 5, pl + pos, (size_t)nv * 12); pos += (size_t)nv * 12; }
        gp += (size_t)nv * (h.authored_normals ? 8 : 5) * 4;
        uint16_t *ix = (uint16_t *)(out + gp); uint16_t a16 = 0;
        for (uint32_t i = 0; i < h.nidx; i++) { uint16_t d; memcpy(&d, pl + pos, 2); pos += 2; a16 += d; ix[i] = a16; }
        gp += ((size_t)h.nidx * 2 + 3) & ~(size_t)3;
        if (h.has_vcol) {
            for (int c = 0; c < 4; c++) { uint8_t a = 0; for (int i = 0; i < nv; i++) { a += pl[pos++]; out[gp + i*4 + c] = a; } }
            gp += (size_t)nv * 4;
        }
    }
    return gp;
}

static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s pak [depth]\n", argv[0]); return 1; }
    int depth = argc > 2 ? atoi(argv[2]) : 64;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("pak"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *pak = malloc(sz); fread(pak, 1, sz, f); fclose(f);
    uint32_t *ph = (uint32_t *)pak; uint32_t n = ph[2];
    WtPackEnt *e = (WtPackEnt *)(pak + ph[3]);
    double t_f1 = 0, t_f2 = 0, t_dec = 0, t_ready = 0, t_dec2 = 0;
    long long s_now = 0, s_hc = 0, s_raw = 0, s_ready = 0, s_ready_hc = 0, s_ready_g = 0;
    uint8_t *raw = malloc(64 << 20), *z = malloc(80 << 20), *rd = malloc(160 << 20), *chk = malloc(160 << 20);
    int bad = 0;
    for (uint32_t t = 0; t < n; t++) {
        uint8_t *tp = pak + e[t].off; uint32_t hdr[8]; memcpy(hdr, tp, 32);
        uint8_t *body = tp + 32; int plen = (int)hdr[5];
        s_now += hdr[6] + 32;
        double a = now();
        if (hdr[7] & 1) { if (!lz4_decompress(body, (int)hdr[6], raw, plen)) { bad++; continue; } }
        else memcpy(raw, body, plen);
        t_dec += now() - a;
        s_raw += plen;
        a = now();
        size_t rn = ready_layout(raw, plen, hdr[4], rd);
        t_ready += now() - a;
        s_ready += rn;
        int zn = lz4hc(raw, plen, z, 80 << 20, depth);
        if (!zn || !lz4_decompress(z, zn, chk, plen) || memcmp(chk, raw, plen)) { bad++; zn = plen; }
        s_hc += zn + 32;
        a = now(); lz4_fast(z, zn, chk, plen); t_f1 += now() - a;
        int zr = lz4hc(rd, (int)rn, z, 80 << 20, depth);
        { double b0 = now(); lz4_fast(z, zr, chk, (int)rn); t_f2 += now() - b0; }
        a = now();
        if (!zr || !lz4_decompress(z, zr, chk, (int)rn) || memcmp(chk, rd, rn)) { bad++; zr = (int)rn; }
        t_dec2 += now() - a;
        s_ready_hc += zr + 32;
        (void)s_ready_g;
    }
    printf("%u tiles, %d bad\n", n, bad);
    printf("stored now (greedy LZ4)   %8.1f MB\n", s_now / 1e6);
    printf("same payload, chain LZ4   %8.1f MB (%.0f%%)\n", s_hc / 1e6, 100.0 * s_hc / s_now);
    printf("raw payload               %8.1f MB\n", s_raw / 1e6);
    printf("ready layout raw          %8.1f MB\n", s_ready / 1e6);
    printf("ready layout, chain LZ4   %8.1f MB (%.0f%%)\n", s_ready_hc / 1e6, 100.0 * s_ready_hc / s_now);
    printf("PC times: lz4 %.0f ms, lanes->floats %.0f ms, lz4 of ready layout %.0f ms\n",
           t_dec * 1e3, t_ready * 1e3, t_dec2 * 1e3);
    printf("fast decoder: chain-LZ4 payload %.0f ms, chain-LZ4 ready layout %.0f ms\n", t_f1 * 1e3, t_f2 * 1e3);
    return 0;
}
