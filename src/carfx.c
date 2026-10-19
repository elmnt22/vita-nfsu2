/* carfx.c -- see carfx.h. Everything here is decoded from SPEED2.EXE:
 *   material params   0x5bd4a0 (from the GLOBALB light-material payload +0x30)
 *   light context     0x48f7a0 -> 0x48f600 -> 0x48ed60
 *   rig               .data 0x7f3e10 (the in-game car rig, 3 white lights)
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

int carfx_sh(float sh[10][4], const float pos[3], const float camfwd[3],
             const void *lights, int nlights, int stride) {
    memset(sh, 0, sizeof(float) * 40);
    /* rig 0x7f3e10: {on, heading, pitch, r, g, b, intensity}, colours x255
       then /255 again in 0x48ed60, so 0.2 white each. Turned with the camera
       (the rig is rotated by the view before the track lights join it). */
    static const float rig[3][3] = { {149.0f, 100.0f, 0.2f}, {303.0f, 36.0f, 0.2f},
                                     {76.0f, 22.0f, 0.2f} };
    float fx = camfwd[0], fy = camfwd[1], fl = sqrtf(fx * fx + fy * fy);
    if (fl < 1e-4f) { fx = 1.0f; fy = 0.0f; } else { fx /= fl; fy /= fl; }
    float rx = fy, ry = -fx;   /* camera right, horizontal */
    for (int i = 0; i < 3; i++) {
        float h = rig[i][0] * 0.0174532925f, p = rig[i][1] * 0.0174532925f;
        float ch = cosf(h), shh = sinf(h), cp = cosf(p), sp = sinf(p);
        float dx = cp * (ch * fx + shh * rx), dy = cp * (ch * fy + shh * ry), dz = sp;
        sh_light(sh, rig[i][2], rig[i][2], rig[i][2], dx, dy, dz);
    }
    /* track lights: the 10 nearest that reach the car (0x4893a0 weight) */
    int best[10], nb = 0; float bestd[10];
    const unsigned char *base = (const unsigned char *)lights;
    for (int i = 0; lights && i < nlights; i++) {
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
