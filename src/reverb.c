/* reverb.c -- the original's software reverb (see reverb.h).

   The patch: SOUND/FXEDIT/<name>.fx, a 0x38-byte header ("FX" 0b 00 ..;
   +8 + 4k = config k's module count, +0x20 + 4k = its connection count,
   big-endian), then the configs one after the other: the modules (byte 0
   type, byte 1 output map, big-endian shorts at +2 / +4 / +6; sizes
   filtermodsizetable 4,8,4,4,8,8,4,8,4,4) and 12-byte connections (src,
   dst module, src output, dst input). MIX_initreverb (GC 802e62a0) takes
   the config of the channel mode (stereo: config 1). SNDMIXI_initfx
   (802e5c3c) builds the SFILTER modules, SFILTER_connect links them, and
   SNDMIXI_modlapifxadd (802e57a8) pulls each output channel's module
   every mixer buffer: a module pulls its upstream (+8 / +0xc) first.

   The DSP (GC decompiles): echo = SFILTER_echo 802e9f18 / modifyECHO,
   lpfRC = 802eaa38 / 802eab20, FIR8 = SNDI_fir8 802ea978 with
   calcFIRCoeffs 802ea5a4, reson = resonx87 802eaea4 / modifyRESON 802eb09c,
   amp = 802e9c74 / 802e9d48, mixer 802ead48, splitter 802eb530, source
   802eb6ac. SNDI_sin / SNDI_cos / SNDI_rootof1plusx (polynomial
   approximations) are sinf / cosf / sqrtf here.

   The choice of patch and the send level: SFXObj_Reverb::InitSFX (PC
   0x4632c0), SFXCTL_Tunnel::UpdateParams 0x483250 (tunnels),
   UpdateCityVerb 0x47c6d0 (the city's reverb layer), AdjustReverbOffset
   0x4713f0, SetCurrentReverbType 0x45db90, the level update 0x4714a0 with
   g_REVERBFXMODULES 0x7f3778 (24-byte records), SFXObj_Reverb::
   ProcessUpdate 0x4633f0 (Reset + SetCustom of the new patch). The car
   engine objects send to fx bus 0 at 0x82b078 (SNDfxlevel 0x723bc1 from
   0x473fb6 / 0x47418b / 0x4742e1). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reverb.h"
#include "carsfx.h"

volatile int g_reverb_on = 1;

#define RV_RATE   44100      /* the mixer rate (initreverb's param_1) */
#define RV_NPATCH 12
#define RV_MAXMOD 16
#define RV_BLK    256
#define RV_POOL   32768      /* echo lines: the largest patch needs ~12k */
#define RV_PI     3.14159265f

/* PC 0x7ed608 */
static const char *const k_rv_name[RV_NPATCH] = {
    "Car_Show_Sml", "Car_Show_Bass", "Car_Show_Bass2", "Garage", "Garage_Sml", "Simple_Tunnel",
    "Simple_Tunnel_Sml", "Hills_Close", "Hills", "City_Open", "City_Dense", "Alley" };
/* g_REVERBFXMODULES 0x7f3778: +0 wet mB, +4 dry mB, +8 wet 2 mB, +0xc dry 2
   mB, +0x10 (x 0.001 -> +0xbc), +0x14 the cross-fade (cInterpLine) */
static const struct { short wet, dry, wet2, dry2, t10, xfade; } k_rvm[RV_NPATCH] = {
    { -1800, 0, -1800, 0, 140, 60 }, { -1800, 0, -1800, 0, 140, 60 }, { -1800, 0, -1800, 0, 140, 60 },
    { -500, 0, -500, 0, 140, 60 },   { -500, 0, -500, 0, 140, 60 },   { 0, 0, 0, 0, 140, 60 },
    { -300, 0, -300, 0, 120, 60 },   { -1800, 0, -1800, 0, 140, 60 }, { -1600, 0, -1600, 0, 140, 60 },
    { -1600, 0, -1600, 0, 140, 60 }, { -1200, 0, -1200, 0, 140, 120 }, { -900, 0, -900, 0, 140, 80 } };

static unsigned char g_fx[RV_NPATCH][512];
static int g_fxlen[RV_NPATCH];

static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }
static int be16(const unsigned char *p) { return p[0] << 8 | p[1]; }

int reverb_load(const char *dataroot)
{
    int ok = 0;
    for (int k = 0; k < RV_NPATCH; k++) {
        char path[600];
        snprintf(path, sizeof path, "%s/SOUND/FXEDIT/%s.fx", dataroot, k_rv_name[k]);
        FILE *f = fopen(path, "rb");
        g_fxlen[k] = 0;
        if (!f) continue;
        g_fxlen[k] = (int)fread(g_fx[k], 1, sizeof g_fx[k], f);
        fclose(f);
        if (g_fxlen[k] >= 0x38 && g_fx[k][0] == 'F' && g_fx[k][1] == 'X') ok++;
        else g_fxlen[k] = 0;
    }
    printf("reverb: %d of %d FXEDIT patches\n", ok, RV_NPATCH);
    return ok;
}

/* ---- the SFILTER graph (audio thread) ---- */
typedef struct {
    int type, in1, in2, done;
    float g;                     /* echo: (p >> 1) / 127; out = v * -g + buf */
    float *buf; int len, pos;
    float a, b, y;               /* lpfRC: y = y * a + b * x */
    float amp;                   /* amp: p / 256 */
    float h[8], c[5];            /* FIR8: the history, the 9-tap symmetric coefficients */
    float rate, f1, f2, r, b1, gain, y1, y2;   /* reson */
    float out[RV_BLK];
} RvMod;
static RvMod g_m[RV_MAXMOD];
static int g_nm, g_out[2] = { -1, -1 };
static float g_pool[RV_POOL];
static volatile int g_rv_want = -1;
static int g_rv_cur = -1, g_rv_idle = 1;
static volatile float g_rv_wet;

/* SNDI_findprime 802ee3ec: the first prime >= d * rate / 1000 (>= 3) */
static int rv_findprime(int rate, int d)
{
    int n = d * rate / 1000;
    if (n < 3) n = 3;
    for (;; n++) {
        int s = (int)sqrtf((float)n) + 1, i, prime = 1;
        for (i = 2; i < s; i++) if (n % i == 0) { prime = 0; break; }
        if (prime && n % 2) return n;
    }
}
/* SNDI_findmult16 802eca40 */
static int rv_findmult16(int rate, int ms) { return rv_findprime(rate, ms / 16) * 16; }

/* calcFIRCoeffs 802ea5a4: 2 = low pass (hi), 3 = high pass (lo), 4 = band
   pass (lo..hi); fc in fractions of Nyquist; Hamming window over the half */
static void rv_fir_coeffs(float *c, int type, float lo, float hi)
{
    float center = 0.0f, sum = 0.0f;
    if ((type == 3 || type == 4) && lo > 0.8f) lo = 0.8f;
    for (int k = 1; k < 5; k++) {
        float x = (float)k * RV_PI;
        if (type == 3) c[4 - k] = sinf(lo * x) / x;
        else if (type == 2) c[4 - k] = sinf(hi * x) / x;
        else c[4 - k] = 2.0f * sinf((hi - lo) * x * 0.5f) * cosf((hi + lo) * x * 0.5f) / x;
    }
    center = type == 3 ? lo : type == 2 ? hi : hi - lo;
    c[4] = center;
    for (int k = 0; k < 5; k++) {
        float w = cosf(0.7853982f * (float)k);
        if (type == 3) {
            c[k] *= w * 0.46f - 0.54f;
            if (k & 1) sum -= c[k]; else sum += c[k];
        } else {
            c[k] *= 0.54f - w * 0.46f;
            sum += c[k];
        }
    }
    if (type == 3) { c[4] += 1.0f; sum += 1.0f; }
    float norm = 2.0f * sum - c[4];
    if (norm < 0.0f) norm = -norm;
    if (norm > 0.0f) for (int k = 0; k < 5; k++) c[k] /= norm;
}

static const int k_modsize[10] = { 4, 8, 4, 4, 8, 8, 4, 8, 4, 4 };

/* MIX_initreverb + SNDMIXI_initfx for patch k (stereo); 0 = no graph */
static int rv_build(int k)
{
    g_nm = 0; g_out[0] = g_out[1] = -1;
    if (k < 0 || k >= RV_NPATCH || !g_fxlen[k]) return 0;
    const unsigned char *d = g_fx[k];
    const int len = g_fxlen[k];
    int cfg = 1;                                   /* channel mode 2 - 1 */
    while (cfg > 0 && !be32(d + 8 + 4 * cfg)) cfg--;
    int p = 0x38;
    for (int c = 0; c < cfg; c++) {                /* skip the lower configs */
        int nm = (int)be32(d + 8 + 4 * c);
        for (int i = 0; i < nm && p < len; i++) p += d[p] < 10 ? k_modsize[d[p]] : 4;
        p += (int)be32(d + 0x20 + 4 * c) * 12;
    }
    int nm = (int)be32(d + 8 + 4 * cfg), nc = (int)be32(d + 0x20 + 4 * cfg);
    if (nm > RV_MAXMOD) nm = RV_MAXMOD;
    int used = 0;
    for (int i = 0; i < nm; i++) {
        if (p + 4 > len || d[p] > 9) return 0;
        RvMod *m = &g_m[i];
        memset(m, 0, sizeof *m);
        m->type = d[p]; m->in1 = m->in2 = -1;
        int p2 = be16(d + p + 2), p4 = k_modsize[m->type] > 4 ? be16(d + p + 4) : 0,
            p6 = k_modsize[m->type] > 4 ? be16(d + p + 6) : 0;
        const float R = (float)RV_RATE;
        switch (m->type) {
        case 1: {                                  /* lpfRC: f +4, gain +6 */
            float c = 2.0f * (float)p4 / R;
            m->a = (1.0f - c) * (float)p6 / 256.0f;
            m->b = c * (float)p6 / 256.0f;
            break; }
        case 2: rv_fir_coeffs(m->c, 2, 0.0f, 2.0f * (float)p2 / R); break;
        case 3: rv_fir_coeffs(m->c, 3, 2.0f * (float)p2 / R, 0.0f); break;
        case 4: rv_fir_coeffs(m->c, 4, 2.0f * (float)p4 / R, 2.0f * (float)p6 / R); break;
        case 5: {                                  /* reson: freq +2, bandwidth +4, gain +6 */
            m->rate = R; m->f1 = (float)p2; m->f2 = (float)p4;
            m->r = 1.0f - m->f2 * RV_PI / R;
            float r2 = m->r * m->r;
            m->b1 = (m->r + m->r) / (r2 + 1.0f) * cosf(m->f1 * 6.2831853f / R);
            float q = 1.0f - m->b1 * m->b1;
            m->gain = (1.0f - r2) * ((float)p6 / 256.0f) * sqrtf(q > 0.0f ? q : 0.0f);
            break; }
        case 6: m->amp = (float)p2 / 256.0f; break;
        case 7: {                                  /* echo: gain +2, delay (ms) +4 */
            m->g = (float)(p2 >> 1) / 127.0f;
            m->len = rv_findmult16(RV_RATE, p4);
            if (used + m->len > RV_POOL) return 0;
            m->buf = g_pool + used; used += m->len;
            memset(m->buf, 0, sizeof(float) * (size_t)m->len);
            break; }
        default: break;
        }
        int ob = d[p + 1];                         /* remap(2, 2, b) = b: slot 1 = left, 2 = right */
        if (ob) {
            if (m->type == 9) {
                if ((ob & 15) == 1 || (ob & 15) == 2) g_out[(ob & 15) - 1] = i;
                ob >>= 4;
            }
            if (ob == 1 || ob == 2) g_out[ob - 1] = i;
        }
        p += k_modsize[m->type];
    }
    g_nm = nm;
    for (int i = 0; i < nc && p + 12 <= len; i++, p += 12) {   /* SFILTER_connect: dst input (byte 9) <- src */
        int s = (int)be32(d + p), t = (int)be32(d + p + 4), in = d[p + 9];
        if (s < 0 || s >= nm || t < 0 || t >= nm) continue;
        if (in == 1 && g_m[t].in1 < 0) g_m[t].in1 = s;
        else if (in == 2 && g_m[t].in2 < 0) g_m[t].in2 = s;
    }
    return 1;
}

static const float *rv_pull(int i, const float *bus, int n);
static const float k_zero[RV_BLK];
static const float *rv_in(int j, const float *bus, int n) { return j >= 0 ? rv_pull(j, bus, n) : k_zero; }

/* one module's block (each output pulled once per block: the splitter's
   second consumer gets its cached copy) */
static const float *rv_pull(int i, const float *bus, int n)
{
    RvMod *m = &g_m[i];
    if (m->done) return m->out;
    m->done = 1;
    float *o = m->out;
    if (m->type == 0) { memcpy(o, bus, sizeof(float) * (size_t)n); return o; }
    if (m->type == 8) {
        const float *a = rv_in(m->in1, bus, n), *b = rv_in(m->in2, bus, n);
        for (int k = 0; k < n; k++) o[k] = a[k] + b[k];
        return o;
    }
    const float *x = rv_in(m->in1, bus, n);
    switch (m->type) {
    case 1:
        for (int k = 0; k < n; k++) o[k] = m->y = m->y * m->a + m->b * x[k];
        break;
    case 2: case 3: case 4: {
        float *h = m->h; const float *c = m->c;
        for (int k = 0; k < n; k++) {
            o[k] = c[4] * h[3] + c[3] * (h[2] + h[4]) + c[2] * (h[1] + h[5]) + c[0] * (x[k] + h[7]) + c[1] * (h[0] + h[6]);
            h[7] = h[6]; h[6] = h[5]; h[5] = h[4]; h[4] = h[3]; h[3] = h[2]; h[2] = h[1]; h[1] = h[0]; h[0] = x[k];
        }
        break; }
    case 5:
        if (m->f2 >= 2.0f * m->rate * 0.31830987f || m->f1 <= 0.0f || m->f1 >= (float)((int)m->rate >> 1)) {
            memcpy(o, x, sizeof(float) * (size_t)n);
            break;
        }
        for (int k = 0; k < n; k++) {
            float y = m->gain * (x[k] + 1e-20f) + (m->r + m->r) * m->b1 * m->y1 - m->r * m->r * m->y2;
            m->y2 = m->y1; m->y1 = y; o[k] = y;
        }
        break;
    case 6:
        for (int k = 0; k < n; k++) o[k] = x[k] * m->amp;
        break;
    case 7: {
        float g = m->g, *buf = m->buf;
        int pos = m->pos, L = m->len;
        for (int k = 0; k < n; k++) {
            if (pos >= L) pos = 0;
            float v = buf[pos] * g + x[k] + 1e-20f;
            o[k] = v * -g + buf[pos];
            buf[pos++] = v;
        }
        m->pos = pos;
        break; }
    case 9:
        memcpy(o, x, sizeof(float) * (size_t)n);
        break;
    default:
        memset(o, 0, sizeof(float) * (size_t)n);
        break;
    }
    return o;
}

int reverb_patch(void) { return g_rv_cur; }
float reverb_send_level(void) { return g_rv_wet; }

int reverb_process(const float *bus, float *outl, float *outr, int n)
{
    int want = g_rv_want;
    if (want != g_rv_cur) {   /* ProcessUpdate: SNDSYS_service, Reset, SetCustom(patch) */
        g_rv_cur = rv_build(want) ? want : -1;
        g_rv_idle = 1;
    }
    float pk_in = 0.0f;
    for (int k = 0; k < n; k++) { float a = bus[k] < 0.0f ? -bus[k] : bus[k]; if (a > pk_in) pk_in = a; }
    if (g_rv_cur < 0 || (g_rv_idle && pk_in == 0.0f)) {
        memset(outl, 0, sizeof(float) * (size_t)n);
        memset(outr, 0, sizeof(float) * (size_t)n);
        return 0;
    }
    float pk_out = 0.0f;
    for (int at = 0; at < n; at += RV_BLK) {
        int b = n - at < RV_BLK ? n - at : RV_BLK;
        for (int i = 0; i < g_nm; i++) g_m[i].done = 0;
        for (int c = 0; c < 2; c++) {
            float *o = (c ? outr : outl) + at;
            if (g_out[c] < 0) { memset(o, 0, sizeof(float) * (size_t)b); continue; }
            const float *s = rv_pull(g_out[c], bus + at, b);
            for (int k = 0; k < b; k++) { o[k] = s[k]; float a = s[k] < 0.0f ? -s[k] : s[k]; if (a > pk_out) pk_out = a; }
        }
    }
    /* port: no input and the tail under -120 dB -> stop running the graph
       until the bus has signal again (the output it would add is inaudible) */
    g_rv_idle = pk_in == 0.0f && pk_out < 1e-6f;
    return 1;
}

/* ---- the reverb parameter map (ParameterMapLayer, GC 8022bb30) ---- */
static struct {
    int ok;
    float x0, y0, x1, y1;
    int nsets, setsize, f0, f2;
    unsigned char *sets;
    unsigned char *nodes; int nnodes, wide;
} g_pm;

void reverb_map_load(const unsigned char *b, long len)
{
    free(g_pm.sets); free(g_pm.nodes);
    memset(&g_pm, 0, sizeof g_pm);
    if (!b) return;
    for (long p = 0; p + 8 <= len; ) {
        unsigned id, sz; memcpy(&id, b + p, 4); memcpy(&sz, b + p + 4, 4);
        if ((long)sz > len - p - 8) break;
        if (id == 0x8003b600u) {                  /* LoaderParameterMaps */
            for (long q = p + 8; q + 8 <= p + 8 + (long)sz; ) {
                unsigned lid, lsz; memcpy(&lid, b + q, 4); memcpy(&lsz, b + q + 4, 4);
                if (lid == 0x8003b601u) {         /* ParameterMapLayer::Load */
                    int hdr[9] = { 0 }, have = 0, toff[8] = { 0 }, nf = 0;
                    const unsigned char *sets = NULL, *n8 = NULL, *n16 = NULL;
                    for (long r = q + 8; r + 8 <= q + 8 + (long)lsz; ) {
                        unsigned cid, csz; memcpy(&cid, b + r, 4); memcpy(&csz, b + r + 4, 4);
                        const unsigned char *c = b + r + 8;
                        if (cid == 0x3b602 && csz >= 36) { memcpy(hdr, c, 36); have = 1; nf = hdr[8] < 8 ? hdr[8] : 8; }
                        else if (cid == 0x3b604 && have && (int)csz == hdr[8] * 4) memcpy(toff, c, (size_t)nf * 4);
                        else if (cid == 0x3b605 && have && (int)csz == hdr[6] * hdr[7]) sets = c;
                        else if (cid == 0x3b607 && have && (int)csz == hdr[5] * 4) n8 = c;
                        else if (cid == 0x3b608 && have && (int)csz == hdr[5] * 8) n16 = c;
                        r += 8 + (long)csz;
                    }
                    if (have && (unsigned)hdr[0] == 0x79b2e325u /* bStringHash("Reverb") */ && sets && nf >= 3 && (n8 || n16)) {
                        float f[4]; memcpy(f, hdr + 1, 16);
                        g_pm.x0 = f[0]; g_pm.y0 = f[1]; g_pm.x1 = f[2]; g_pm.y1 = f[3];
                        g_pm.nsets = hdr[6]; g_pm.setsize = hdr[7]; g_pm.f0 = toff[0]; g_pm.f2 = toff[2];
                        g_pm.nnodes = hdr[5]; g_pm.wide = n16 != NULL;
                        size_t ns = (size_t)hdr[6] * (size_t)hdr[7], nn = (size_t)hdr[5] * (n16 ? 8 : 4);
                        g_pm.sets = malloc(ns); g_pm.nodes = malloc(nn);
                        if (g_pm.sets && g_pm.nodes) {
                            memcpy(g_pm.sets, sets, ns); memcpy(g_pm.nodes, n16 ? n16 : n8, nn);
                            g_pm.ok = 1;
                        }
                    }
                }
                q += 8 + (long)lsz;
            }
            break;
        }
        p += 8 + (long)sz;
    }
    printf("reverb: parameter map %s (%d sets, %d nodes)\n", g_pm.ok ? "loaded" : "none", g_pm.nsets, g_pm.nnodes);
}

/* GetParameterSetIndexFromQuadData8 / 16 (8022c1b4 / 8022c280, PC 0x5f0930
   / 0x5f0a30): down the quad tree (a node's first entry 0 = a leaf, its set
   in the second). Its bounds test returns set 0 only when x < xmin AND
   x > xmax AND y < ymin AND y > ymax (0x5f0944..0x5f098e, GC the same), which
   never holds -- so a point outside the map walks the tree too and lands
   on the nearest edge leaf (R1 11-01; was: outside -> set 0) */
static int pm_set(float x, float y)
{
    float x0 = g_pm.x0, x1 = g_pm.x1, y0 = g_pm.y0, y1 = g_pm.y1;
    unsigned u = 0;
    for (int guard = 0; guard < 64; guard++) {
        if (u >= (unsigned)g_pm.nnodes) return 0;
        unsigned e[4];
        if (g_pm.wide) { const unsigned char *nd = g_pm.nodes + 8 * u; for (int k = 0; k < 4; k++) e[k] = nd[2 * k] | nd[2 * k + 1] << 8; }
        else { const unsigned char *nd = g_pm.nodes + 4 * u; for (int k = 0; k < 4; k++) e[k] = nd[k]; }
        if (!e[0]) return (int)e[1] < g_pm.nsets ? (int)e[1] : 0;
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        if (cx <= x) { x0 = cx; if (cy <= y) { u = e[3]; y0 = cy; } else { u = e[2]; y1 = cy; } }
        else         { x1 = cx; if (cy <= y) { u = e[1]; y0 = cy; } else { u = e[0]; y1 = cy; } }
    }
    return 0;
}

/* ---- SFXCTL_Tunnel's reverb state (game thread) ---- */
static struct {
    int init, fe;
    int type;          /* 0x82af2c */
    int applied;       /* 0x82b060 */
    float offset;      /* 0x82a99c (mB) */
    int pending, lerp; /* +0xd4, +0xd5 */
    float it, idur;    /* the cInterpLine at +0xb8 (+0xcc value, +0xd0 done) */
    int zone, in_tunnel;   /* +0xb4, +0x70 */
    float tw, tw2, td, td2;   /* targets 0x82b064, 0x82af30, 0x82afac, 0x82b074 */
    float cw, cw2, cd, cd2;   /* currents 0x82a5b8, 0x82a5cc, 0x82b618, 0x82a5d0 */
} g_tv;

/* NFSMixShape::GetQ15FromHundredthsdB 0x467ff0 (x 1/32767): 0 mB = 0x7fff,
   else the 602-entry table 0x7ee7ec (one mB a step) shifted right once per
   -602 mB, under 50 -> 0. The table is 32767 x 10^((mB - 1) / 2000), not
   10^(mB / 2000) (checked against the EXE: -601 -> 16384, -2000 -> 3272), so
   the exact integer path (carsfx_mb2lin, the same table) is used (G5 11-01) */
static float rv_lin(int mb)
{
    if (mb < -10000) mb = -10000;
    if (mb > 0) mb = 0;
    return (float)carsfx_mb2lin(mb) / 32767.0f;
}
/* SetCurrentReverbType 0x45db90 (with +0xd7 set in game) */
static void tv_set_type(int type, int off) { g_tv.type = type; g_tv.offset = (float)off; g_tv.pending = 1; }
/* AdjustReverbOffset 0x4713f0 */
static void tv_adjust(int off)
{
    if (g_tv.pending || g_tv.lerp) return;
    g_tv.offset = (float)off;
    g_tv.tw = rv_lin(k_rvm[g_tv.applied].wet + off);
    g_tv.tw2 = rv_lin(k_rvm[g_tv.applied].wet2 + off);
}
static float tv_step(float c, float t)   /* +-0.25 an update (0x784d28) */
{
    if (c + 0.25f < t) return c + 0.25f;
    if (c - 0.25f > t) return c - 0.25f;
    return t;
}

void reverb_update(int in_game, const float car[3], int tunnel, float dt)
{
#ifdef OPENUG2_SIM_PRE_H
    static int env_read;
    if (!env_read) { env_read = 1; if (getenv("OPENUG2_REVERB_OFF")) g_reverb_on = 0; }
#endif
    if (!in_game) {
        /* SFXObj_Reverb::InitSFX in the front end: Garage (0x82af6c) unless
           0x89e7e5 / 0x836474 (then Car_Show_Sml 0x82af60). Those two are the
           car show flags (TheRaceParameters' car show bool + g_tweakIsCarShow,
           GC FEngHud ctor 801139f8 = PC 0x4f8c3e / 0x4f8c46); no store to
           either in PC or GC (only reads), so the Garage branch (CONFIRMED
           G3 11-01). The tunnel controller
           does not run: the send stays at its constructor 0 */
        g_tv.init = 0;
        g_rv_want = 3;
        g_rv_wet = 0.0f;
        return;
    }
    if (!g_tv.init) {   /* the constructor 0x45d950, then InitSFX in game: Hills (0x82af80), type 8 */
        memset(&g_tv, 0, sizeof g_tv);
        g_tv.init = 1;
        g_tv.type = g_tv.applied = 5;
        g_tv.td = g_tv.td2 = g_tv.cd = g_tv.cd2 = 1.0f;
        g_tv.zone = -1;
        g_tv.idur = 0.0f; g_tv.it = 1.0f;
        g_rv_want = 8;
        tv_set_type(8, 0);
    }
    /* UpdateParams 0x483250: a tunnel zone under the car */
    if (tunnel) {
        if (!g_tv.in_tunnel) {
            g_tv.in_tunnel = 1;
            tv_set_type(tunnel == 5 ? 5 : (tunnel >= 6 && tunnel <= 7) ? 6 : 3, 0);
        }
    } else if (g_tv.in_tunnel) {
        g_tv.in_tunnel = 0;
        int t = g_tv.zone >= 0 && g_tv.zone < RV_NPATCH ? g_tv.zone : 9;
        tv_set_type(t, 0);
    }
    /* UpdateCityVerb 0x47c6d0: the reverb layer at the car (data int 0 =
       the zone, int 2 = the offset) */
    {
        int prev = g_tv.zone, off = 0;
        if (g_pm.ok && car) {
            const unsigned char *s = g_pm.sets + (size_t)pm_set(car[0], car[1]) * (size_t)g_pm.setsize;
            memcpy(&g_tv.zone, s + g_pm.f0, 4);
            memcpy(&off, s + g_pm.f2, 4);
        }
        if (g_tv.zone > 11 || g_tv.zone < 0) g_tv.zone = 9;   /* in game */
        int t = g_tv.zone;                                     /* ReverbZoneCrossMap 0x7ed5d8: identity */
        if (g_tv.zone != prev && !g_tv.in_tunnel) tv_set_type(t, off);
        else tv_adjust(off);
    }
    /* the level update 0x4714a0 */
    int done = g_tv.it >= g_tv.idur;
    if (!done) g_tv.it += dt * 1000.0f;
    done = g_tv.it >= g_tv.idur;
    if (g_tv.pending) {
        if (done) {
            g_tv.pending = 0; g_tv.lerp = 1;
            g_tv.applied = g_tv.type;
            g_rv_want = g_tv.applied;   /* ProcessUpdate 0x4633f0: Reset + SetCustom */
            g_tv.it = 0.0f; g_tv.idur = (float)k_rvm[g_tv.applied].xfade;   /* 0x471516: cInterpLine::Initialize(0, 1, rec +0x14, 0) 0x459990 = int x 0.001 (0x78435c) s, curve 0 = linear (Update 0x46b100 default); ms here (CONFIRMED G5 11-01) */
            g_tv.cw = g_tv.tw; g_tv.cd = g_tv.td; g_tv.cw2 = g_tv.tw2; g_tv.cd2 = g_tv.td2;
            int off = (int)g_tv.offset;
            g_tv.tw = rv_lin(k_rvm[g_tv.applied].wet + off);
            g_tv.tw2 = rv_lin(k_rvm[g_tv.applied].wet2 + off);
            g_tv.td = rv_lin(k_rvm[g_tv.applied].dry);
            g_tv.td2 = rv_lin(k_rvm[g_tv.applied].dry2);
            done = 0;
        }
    } else if (g_tv.lerp && done) {
        g_tv.lerp = 0;
        g_tv.cw = g_tv.tw; g_tv.cd = g_tv.td; g_tv.cw2 = g_tv.tw2; g_tv.cd2 = g_tv.td2;
    }
    float wet;
    if (!g_tv.pending && !g_tv.lerp) {
        g_tv.cw = tv_step(g_tv.cw, g_tv.tw); g_tv.cd = tv_step(g_tv.cd, g_tv.td);
        g_tv.cw2 = tv_step(g_tv.cw2, g_tv.tw2); g_tv.cd2 = tv_step(g_tv.cd2, g_tv.td2);
        wet = g_tv.cw;
    } else {
        float f = g_tv.idur > 0.0f ? g_tv.it / g_tv.idur : 1.0f;
        if (f > 1.0f) f = 1.0f;
        wet = g_tv.cw + (g_tv.tw - g_tv.cw) * f;
    }
    g_rv_wet = wet;
#ifdef OPENUG2_SIM_PRE_H
    static int log_patch = -2, log_zone = -2;
    if (getenv("OPENUG2_REVERB_LOG") && (g_tv.applied != log_patch || g_tv.zone != log_zone) && !g_tv.pending && !g_tv.lerp) {
        printf("[reverb] patch %d (%s) zone %d offset %.0f tunnel %d wet %.3f at (%.0f,%.0f)\n", g_tv.applied,
               k_rv_name[g_tv.applied], g_tv.zone, g_tv.offset, tunnel, wet, car ? car[0] : 0.0f, car ? car[1] : 0.0f);
        log_patch = g_tv.applied; log_zone = g_tv.zone;
    }
#endif
}
