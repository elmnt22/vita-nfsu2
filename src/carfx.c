/* carfx.c -- see carfx.h. Everything here is decoded from SPEED2.EXE:
 *   material params   0x5bd4a0 (from the GLOBALB light-material payload +0x30)
 *   light context     0x48f7a0 -> 0x48f600 -> 0x48ed60
 *   rig               .data 0x7f3e10 (the in-game car rig, 3 white lights);
 *                     front-end shaper rigs: FrontB chunk 0x0003B700
 *   track lights      0x48e3d0 / 0x4893a0 (<= 10, weighted by r_in/r_out)
 *   SH accumulation   0x489af0 / 0x4895e0 (+ a -0.05 back light per light) */
#include "carfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { uint32_t hash; CarFxMat m; } CarFxEntry;
static CarFxEntry *g_mats;
static int g_nmats;
static CarFxMat g_paint, g_default;
static int g_have_paint, g_have_default;

static uint32_t cfx_hash(const char *s) {
    uint32_t h = 0xFFFFFFFFu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}
static uint32_t rd32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 0x5bd4a0: f = 28 floats at payload +0x30. */
static void cfx_decode(const float *f, CarFxMat *m) {
    for (int k = 0; k < 3; k++) {
        float dmin = f[0] * f[1 + k], dmax = f[4] * f[5 + k];
        float smin = f[11] * f[12 + k], smax = f[15] * f[16 + k];
        float emin = 0.5f * f[20] * f[21 + k], emax = 0.5f * f[24] * f[25 + k];
        m->diff_min[k] = dmin; m->diff_range[k] = dmax - dmin;
        m->spec_min[k] = smin; m->spec_range[k] = smax - smin;
        m->env_min[k] = emin;  m->env_range[k] = emax - emin;
    }
    m->diff_min[3] = f[8]; m->diff_range[3] = f[9] - f[8];
    m->spec_pow = 1.1f * f[10];
    m->env_pow = f[19];
}

int carfx_load_materials(const unsigned char *d, long len) {
    free(g_mats); g_mats = NULL; g_nmats = 0; g_have_paint = g_have_default = 0;
    if (!d || len < 8) return 0;
    int cap = 0;
    uint32_t paint = cfx_hash("METPAINT_3"), def = cfx_hash("DEFAULT"),
             nothing = cfx_hash("PLAINNOTHING");
    for (long o = 0; o + 8 <= len;) {
        uint32_t id = rd32(d + o), size = rd32(d + o + 4);
        long p = o + 8;
        if ((long)size > len - p) break;
        if (id == 0x00135200u && size >= 0x30 + 28 * 4) {
            uint32_t h = rd32(d + p + 12);
            float f[28]; memcpy(f, d + p + 0x30, sizeof f);
            int ok = 1;
            for (int k = 0; k < 28; k++) if (!(f[k] == f[k]) || fabsf(f[k]) > 1e6f) ok = 0;
            if (ok && h != nothing) {
                if (g_nmats == cap) {
                    cap = cap ? cap * 2 : 256;
                    CarFxEntry *n = (CarFxEntry *)realloc(g_mats, (size_t)cap * sizeof *n);
                    if (!n) break;
                    g_mats = n;
                }
                g_mats[g_nmats].hash = h;
                cfx_decode(f, &g_mats[g_nmats].m);
                if (h == paint) { g_paint = g_mats[g_nmats].m; g_have_paint = 1; }
                if (h == def) { g_default = g_mats[g_nmats].m; g_have_default = 1; }
                g_nmats++;
            }
        }
        o = p + (long)size;
    }
    printf("car shading: %d light materials from GLOBALB (paint %s, default %s)\n",
           g_nmats, g_have_paint ? "METPAINT_3" : "MISSING", g_have_default ? "ok" : "MISSING");
    return g_nmats;
}

const CarFxMat *carfx_material(uint32_t hash) {
    for (int i = 0; i < g_nmats; i++) if (g_mats[i].hash == hash) return &g_mats[i].m;
    return NULL;
}
int carfx_loaded(void) { return g_nmats > 0; }
static const CarFxMat *g_paint_cur;
void carfx_use_paint(uint32_t material) { g_paint_cur = material ? carfx_material(material) : NULL; }
const CarFxMat *carfx_paint(void) {
    if (g_paint_cur) return g_paint_cur;
    return g_have_paint ? &g_paint : carfx_default();
}
const CarFxMat *carfx_default(void) {
    static CarFxMat neutral = { {1,1,1,1}, {0,0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, 1, 1 };
    return g_have_default ? &g_default : &neutral;
}

/* 0x4895e0: one directional light (colour c = r,g,b,w) into the quadratic
   form. Note the yz term takes 0.429043, not 0.858086 like xy and xz: that
   is what the game does. */
static void sh_add(float sh[10][4], const float c[4], float x, float y, float z) {
    for (int ch = 0; ch < 4; ch++) {
        float cs = c[ch];
        float l00 = 0.282095f * cs, a = 0.488603f * cs, b = 1.092548f * cs;
        float l20 = 0.315392f * cs * (3.0f * z * z - 1.0f);
        float l22 = 0.546274f * cs * (x * x - y * y);
        sh[0][ch] += 0.886227f * l00 - 0.247708f * l20;
        sh[1][ch] += 0.429043f * l22;
        sh[2][ch] -= 0.429043f * l22;
        sh[3][ch] += 0.743125f * l20;
        sh[4][ch] += 0.858086f * b * x * y;
        sh[5][ch] += 0.858086f * b * x * z;
        sh[6][ch] += 0.429043f * b * y * z;
        sh[7][ch] += 1.023328f * a * x;
        sh[8][ch] += 1.023328f * a * y;
        sh[9][ch] += 1.023328f * a * z;
    }
}

/* 0x489af0: the light plus a faint opposite fill. */
static void sh_light(float sh[10][4], float r, float g, float b, float dx, float dy, float dz) {
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 1e-6f) return;
    dx /= l; dy /= l; dz /= l;
    float c[4] = { r, g, b, 0.29f * r + 0.54f * g + 0.17f * b };
    sh_add(sh, c, dx, dy, dz);
    float back[4] = { c[0] * -0.05f, c[1] * -0.05f, c[2] * -0.05f, c[3] * -0.05f };
    sh_add(sh, back, -dx, -dy, -dz);
}

/* A 2D grid over the track lights (cell -> lights whose r_out circle touches
   it), rebuilt when the light list changes; the per-car pick then reads one
   cell's few lights instead of all ~2000. */
static struct {
    const void *lights; int n, stride; unsigned gen; int ok;
    float x0, y0, inv; int w, h;
    int *start, *list;
} g_lg;
static unsigned g_lights_gen;
void carfx_lights_changed(void) { g_lights_gen++; }
static int light_grid(const void *lights, int n, int stride) {
    if (g_lg.lights == lights && g_lg.n == n && g_lg.stride == stride && g_lg.gen == g_lights_gen)
        return g_lg.ok;
    free(g_lg.start); free(g_lg.list);
    memset(&g_lg, 0, sizeof g_lg);
    g_lg.lights = lights; g_lg.n = n; g_lg.stride = stride; g_lg.gen = g_lights_gen;
    const unsigned char *base = (const unsigned char *)lights;
    float mn[2] = { 1e30f, 1e30f }, mx[2] = { -1e30f, -1e30f };
    int any = 0;
    for (int i = 0; i < n; i++) {
        const CarFxLight *L = (const CarFxLight *)(base + (size_t)i * (size_t)stride);
        float r = fabsf(L->r_out);
        if (!(r > 0) || !isfinite(L->pos[0]) || !isfinite(L->pos[1]) || !isfinite(r)) continue;
        any = 1;
        for (int a = 0; a < 2; a++) {
            if (L->pos[a] - r < mn[a]) mn[a] = L->pos[a] - r;
            if (L->pos[a] + r > mx[a]) mx[a] = L->pos[a] + r;
        }
    }
    if (!any) { g_lg.ok = 1; g_lg.w = g_lg.h = 0; g_lg.start = NULL; return 1; }   /* every query: none */
    float cell = 64.0f;
    while ((mx[0] - mn[0]) / cell > 512.0f || (mx[1] - mn[1]) / cell > 512.0f) cell *= 2.0f;
    g_lg.x0 = mn[0]; g_lg.y0 = mn[1]; g_lg.inv = 1.0f / cell;
    g_lg.w = (int)((mx[0] - mn[0]) * g_lg.inv) + 1; g_lg.h = (int)((mx[1] - mn[1]) * g_lg.inv) + 1;
    int ncell = g_lg.w * g_lg.h;
    g_lg.start = (int *)calloc((size_t)ncell + 1, sizeof(int));
    if (!g_lg.start) return 0;
    int *cur = NULL;
    for (int pass = 0; pass < 2; pass++) {               /* count, then fill (index order) */
        for (int i = 0; i < n; i++) {
            const CarFxLight *L = (const CarFxLight *)(base + (size_t)i * (size_t)stride);
            float r = fabsf(L->r_out);
        if (!(r > 0) || !isfinite(L->pos[0]) || !isfinite(L->pos[1]) || !isfinite(r)) continue;
            int x0 = (int)floorf((L->pos[0] - r - g_lg.x0) * g_lg.inv);
            int x1 = (int)floorf((L->pos[0] + r - g_lg.x0) * g_lg.inv);
            int y0 = (int)floorf((L->pos[1] - r - g_lg.y0) * g_lg.inv);
            int y1 = (int)floorf((L->pos[1] + r - g_lg.y0) * g_lg.inv);
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 >= g_lg.w) x1 = g_lg.w - 1; if (y1 >= g_lg.h) y1 = g_lg.h - 1;
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    int c = y * g_lg.w + x;
                    if (pass == 0) g_lg.start[c + 1]++;
                    else g_lg.list[cur[c]++] = i;
                }
        }
        if (pass == 0) {
            for (int c = 0; c < ncell; c++) g_lg.start[c + 1] += g_lg.start[c];
            g_lg.list = (int *)malloc((size_t)(g_lg.start[ncell] ? g_lg.start[ncell] : 1) * sizeof(int));
            cur = (int *)malloc((size_t)ncell * sizeof(int));
            if (!g_lg.list || !cur) {
                free(g_lg.list); free(cur); free(g_lg.start);
                g_lg.list = NULL; g_lg.start = NULL; return 0;
            }
            memcpy(cur, g_lg.start, (size_t)ncell * sizeof(int));
        }
    }
    free(cur);
    g_lg.ok = 1;
    return 1;
}

/* Shaper light rigs: {flag, heading, pitch, r, g, b, intensity} x 3.
   0x7f3e10 is the in-game rig (the default of the lookup 0x48a2d0); the
   front end's come from the 0x0003B700 chunk (0x48a280, 0x74-byte records:
   name hash, 0x1c pad, 3 lights of 0x1c) in FrontB. */
static const CarFxRig k_rig_ingame = { 0, { {1, 149, 100, 1, 1, 1, 0.2f}, {1, 303, 36, 1, 1, 1, 0.2f},
                                            {1, 76, 22, 1, 1, 1, 0.2f} } };
static CarFxRig *g_rigs; static int g_nrigs;
static const CarFxRig *g_rig;
int carfx_load_rigs(const unsigned char *d, long len) {
    int n = (int)(len / 0x74);
    free(g_rigs); g_rigs = NULL; g_nrigs = 0; g_rig = NULL;
    if (n <= 0) return 0;
    g_rigs = (CarFxRig *)calloc((size_t)n, sizeof *g_rigs);
    if (!g_rigs) return 0;
    for (int i = 0; i < n; i++) {
        const unsigned char *r = d + (long)i * 0x74;
        g_rigs[i].hash = rd32(r);
        for (int j = 0; j < 3; j++) {
            const unsigned char *q = r + 0x20 + j * 0x1c;
            g_rigs[i].l[j][0] = rd32(q) ? 1.0f : 0.0f;
            for (int k = 1; k < 7; k++) memcpy(&g_rigs[i].l[j][k], q + 4 * k, 4);
        }
    }
    g_nrigs = n;
    return n;
}
int carfx_use_rig(uint32_t hash) {
    g_rig = NULL;
    for (int i = 0; hash && i < g_nrigs; i++)
        if (g_rigs[i].hash == hash) { g_rig = &g_rigs[i]; return 1; }
    return 0;
}
uint32_t carfx_rig_hash(const char *name) {
    char u[64]; int i = 0;
    for (; name[i] && i < 63; i++) u[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    u[i] = 0;
    return cfx_hash(u);
}

int carfx_sh(float sh[10][4], const float pos[3], const float camfwd[3],
             const void *lights, int nlights, int stride) {
    memset(sh, 0, sizeof(float) * 40);
    /* the shaper rig (carfx_use_rig): 0x48ed60 turns each light's
       {heading, pitch} by 0x48a310 into r = (cos p cos(h+180),
       cos p sin(h+180), sin p); a light with its flag set is in camera
       space and goes to world as -r.y right - r.z down + r.x forward
       (right = (0,0,-1) x fwd, down = fwd x right, as 0x444de0 builds the
       view), a flag-0 light is r in world space. Colour = rgb * intensity. */
    float fx = camfwd[0], fy = camfwd[1], fl = sqrtf(fx * fx + fy * fy);
    if (fl < 1e-4f) { fx = 1.0f; fy = 0.0f; } else { fx /= fl; fy /= fl; }
    float rx = fy, ry = -fx;   /* camera right, horizontal */
    const CarFxRig *rig = g_rig ? g_rig : &k_rig_ingame;
    for (int i = 0; i < 3; i++) {
        const float *L = rig->l[i];
        float h = L[1] * 0.0174532925f, p = L[2] * 0.0174532925f;
        float ch = cosf(h), shh = sinf(h), cp = cosf(p), sp = sinf(p);
        float dx, dy, dz = sp;
        if (L[0] != 0.0f) { dx = cp * (shh * rx - ch * fx); dy = cp * (shh * ry - ch * fy); }
        else { dx = -cp * ch; dy = -cp * shh; }
        sh_light(sh, L[3] * L[6], L[4] * L[6], L[5] * L[6], dx, dy, dz);
    }
    if (!lights) return 0;
    /* track lights: the 10 nearest that reach the car (0x4893a0 weight) */
    int best[10], nb = 0; float bestd[10];
    const unsigned char *base = (const unsigned char *)lights;
    /* only the lights whose reach covers pos's grid cell, in index order,
       so the pick (ties included) is the full scan's */
    const int *cand = NULL; int ncand = nlights;
    if (lights && isfinite(pos[0]) && isfinite(pos[1]) && light_grid(lights, nlights, stride)) {
        int cx = (int)floorf((pos[0] - g_lg.x0) * g_lg.inv), cy = (int)floorf((pos[1] - g_lg.y0) * g_lg.inv);
        if (cx < 0 || cy < 0 || cx >= g_lg.w || cy >= g_lg.h) ncand = 0;   /* no light reaches */
        else {
            int c = cy * g_lg.w + cx;
            cand = g_lg.list + g_lg.start[c]; ncand = g_lg.start[c + 1] - g_lg.start[c];
        }
    }
    for (int ci = 0; lights && ci < ncand; ci++) {
        int i = cand ? cand[ci] : ci;
        const CarFxLight *L = (const CarFxLight *)(base + (size_t)i * (size_t)stride);
        float dx = pos[0] - L->pos[0], dy = pos[1] - L->pos[1], dz = pos[2] - L->pos[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= L->r_out * L->r_out) continue;
        int k;
        if (nb < 10) k = nb++;                     /* a free slot */
        else if (d2 >= bestd[9]) continue;         /* full: farther than the 10th */
        else k = 9;
        while (k > 0 && bestd[k - 1] > d2) { best[k] = best[k - 1]; bestd[k] = bestd[k - 1]; k--; }
        best[k] = i; bestd[k] = d2;
    }
    for (int j = 0; j < nb; j++) {
        const CarFxLight *L = (const CarFxLight *)(base + (size_t)best[j] * (size_t)stride);
        float d2 = bestd[j], ri2 = L->r_in * L->r_in, ro2 = L->r_out * L->r_out;
        float w = d2 <= ri2 || ro2 <= ri2 ? 1.0f : 1.0f - (d2 - ri2) / (ro2 - ri2);
        float k = 0.75f * w / 255.0f;
        sh_light(sh, (float)(L->rgba & 0xffu) * k, (float)(L->rgba >> 8 & 0xffu) * k,
                 (float)(L->rgba >> 16 & 0xffu) * k,
                 L->pos[0] - pos[0], L->pos[1] - pos[1], L->pos[2] - pos[2]);
    }
    return nb;
}
