/* chasecam.c -- the retail NFSU2 player camera; see chasecam.h. */
#include "chasecam.h"
#include <math.h>
#include <string.h>

/* 0x7ec610 smoothing records, two per view, blended at 0x787154 = 0.5.
 * [0] eye dur, [1] look dur, [2] fov dur, [3] up dur, [4..6] up gains (world
 * accel), [8..10] up min, [12..14] up max, [16..18] eye gains (local accel),
 * [20..22] eye min, [24..26] eye max, [28..30] look gains, [32..34] look
 * min, [36..38] look max, [40..42] direction durations. */
static const float REC_CHASE_FAR[2][44] = {   /* views 0 and 5 */
    { 1.5f,0.5f,1,1, 0.006f,0.006f,0,0, -1,-1,1,0, 1,1,1,0, -0.25f,0,0,0, -2.5f,-1,-1,0,
      2,1,1,0, -0.1f,0,0,0, -2,0,-1,0, 2,0,1,0, 0.5f,0.5f,0.7f,0 },
    { 0.5f,0.2f,0.5f,0.5f, 0.002f,0.002f,0,0, -1,-1,1,0, 1,1,1,0, -0.05f,0,0,0, -0.5f,-1,-1,0,
      0.5f,1,1,0, -0.02f,0,0,0, -0.5f,0,-1,0, 0.5f,0,1,0, 0.1f,0.1f,0.4f,0 } };
static const float REC_CHASE_NEAR[2][44] = {  /* view 1 */
    { 1.5f,0.5f,1,0.9f, 0.005f,0.005f,0,0, -1,-1,1,0, 1,1,1,0, -0.2f,0,0,0, -2,-1,-1,0,
      1.5f,1,1,0, -0.1f,0,0,0, -2,0,-1,0, 2,0,1,0, 0.5f,0.5f,0.7f,0 },
    { 0.5f,0.2f,0.5f,0.1f, 0.001f,0.001f,0,0, -1,-1,1,0, 1,1,1,0, -0.05f,0,0,0, -0.5f,-1,-1,0,
      0.5f,1,1,0, -0.02f,0,0,0, -0.5f,0,-1,0, 0.5f,0,1,0, 0.1f,0.1f,0.3f,0 } };
static const float REC_FIXED[2][44] = {       /* views 2 and 3 */
    { 0,0,1,0 }, { 0,0,0.5f,0 } };
static const float REC_HOOD[2][44] = {        /* view 4 */
    { 0.7f,0.7f,1,0.7f, 0,0,0,0, 0,0,0,0, 0,0,0,0, -0.01f,0,0,0, -0.1f,-0.1f,-0.1f,0,
      0.2f,0.1f,0.1f,0, 0,0.005f,0,0, 0,-0.08f,0,0, 0,0.08f,0,0, 0.5f,0.5f,0.5f,0 },
    { 0.5f,0.5f,0.5f,0.5f, 0,0,0,0, 0,0,0,0, 0,0,0,0, -0.01f,0,0,0, -0.1f,-0.1f,-0.1f,0,
      0.2f,0.1f,0.1f,0, 0,0.005f,0,0, 0,-0.08f,0,0, 0,0.08f,0,0, 0.1f,0.1f,0.1f,0 } };

/* 0x7eb5b4: FOV per view, u16 angle (horizontal at 4:3) */
static const unsigned short VIEW_FOV[6] = { 0x32dc, 0x32dc, 0x4000, 0x4000, 0x4000, 0x32dc };

/* 0x502500: menu setting -> camera info id */
static int setting_view(int s)
{
    switch (s) { case 0: return 2; case 1: return 4; case 2: return 1; case 4: return 5; default: return 0; }
}
static int is_chase(int v) { return v == 0 || v == 1 || v == 5; }

/* ---- 0x5749e0 cubic --------------------------------------------------- */
static void cub_init(CcCubic *c, float v, float dur)
{
    memset(c, 0, sizeof *c);
    c->val = c->tgt = v; c->dur = dur; c->tmax = 10.0f; c->flag = 1;
}
static void cub_target(CcCubic *c, float x)        /* 0x570ba0 */
{
    c->tgt = x;
    if (x != c->val) c->state = 2;
}
static void cub_snap(CcCubic *c) { c->val = c->tgt; c->vel = c->tvel; c->state = 0; }
static void cub_step(CcCubic *c, float dt)
{
    if (c->state == 2) {
        float D = c->tgt - c->val;
        c->t = 0.0f;
        c->d = c->val; c->c = c->vel;
        c->a = c->tvel + c->vel - 2.0f * D;
        c->b = 3.0f * D - c->tvel - 2.0f * c->vel;
        if (!c->flag) c->state = 1;
    }
    if (c->state == 0) return;
    c->t = c->dur <= 1e-5f ? 1.0f : c->t + dt / c->dur;
    if (c->t < c->tmax) {
        float t = c->t;
        c->val = ((c->a * t + c->b) * t + c->c) * t + c->d;
        c->vel = (2.0f * c->b + 3.0f * c->a * t) * t + c->c;
    } else {
        c->state = 0; c->val = c->tgt; c->vel = c->tvel;
    }
}
static void cub3_target(CcCubic *c, const float v[3]) { for (int i = 0; i < 3; i++) cub_target(&c[i], v[i]); }
static void cub3_snap(CcCubic *c) { for (int i = 0; i < 3; i++) cub_snap(&c[i]); }
static void cub3_dur(CcCubic *c, float d) { for (int i = 0; i < 3; i++) c[i].dur = d; }

/* ---- small vector helpers ---------------------------------------------- */
static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void cross3(const float *a, const float *b, float *o)
{
    float r[3] = { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
    o[0] = r[0]; o[1] = r[1]; o[2] = r[2];
}
static float norm3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- 0x441270 / 0x4411b0 noise ----------------------------------------- */
static float lattice(int i)
{
    unsigned n = ((unsigned)i << 13) ^ (unsigned)i;
    n = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
    return 1.0f - (float)n / 1073741824.0f;
}
static float vnoise(float x)
{
    float f = floorf(x);
    int i = (int)f;
    float a = lattice(i), b = lattice(i + 1);
    return a + (b - a) * (x - f);
}
static float noise6(float x)
{
    float s = 0.0f, fr = 1.0f, am = 1.0f;
    for (int k = 0; k < 6; k++) { s += vnoise(fr * x) * am; fr += fr; am *= 0.5f; }
    return s;
}

/* ---- EA view (row vectors, camera x right / y down / z forward) -------- */
static void ea_lookat(const float eye[3], const float at[3], const float up[3], float V[4][4])
{
    float f[3] = { at[0]-eye[0], at[1]-eye[1], at[2]-eye[2] }, r[3], d[3], nu[3] = { -up[0], -up[1], -up[2] };
    norm3(f);
    cross3(nu, f, r); norm3(r);
    cross3(f, r, d); norm3(d);
    for (int i = 0; i < 3; i++) { V[i][0] = r[i]; V[i][1] = d[i]; V[i][2] = f[i]; V[i][3] = 0.0f; }
    V[3][0] = -dot3(eye, r); V[3][1] = -dot3(eye, d); V[3][2] = -dot3(eye, f); V[3][3] = 1.0f;
}
static void ea_eye(float V[4][4], float eye[3])   /* inverse of the rigid part */
{
    for (int i = 0; i < 3; i++)
        eye[i] = -(V[i][0]*V[3][0] + V[i][1]*V[3][1] + V[i][2]*V[3][2]);
}
static void ea_cam_point(float V[4][4], const float p[3], float q[3])
{
    for (int j = 0; j < 3; j++) q[j] = p[0]*V[0][j] + p[1]*V[1][j] + p[2]*V[2][j] + V[3][j];
}
static void mul44(float A[4][4], float B[4][4], float O[4][4])
{
    float R[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            R[i][j] = A[i][0]*B[0][j] + A[i][1]*B[1][j] + A[i][2]*B[2][j] + A[i][3]*B[3][j];
    memcpy(O, R, sizeof R);
}

/* ---- camera shakers (PC 0x5728c0 .. 0x572fc4, GC 8019d8d4..) -----------
   tShaker {+0 vector, +0x10 graph, +0x14 time, +0x18 duration, +0x1c
   rate}: StartShaking 0x572810 stores -vector, graph 0x7fb7a0, time 0;
   UpdateCameraShakers 0x5728c0 advances time by frame dt / duration and
   drops the graph once time leaves [0, 1]; GetValue 0x572860 = vector x
   graph(time) when ftol(rate x time) is even, else 0 (the on / off
   judder). The player's car feeds its acceleration (car +0x80) through
   MaybeCameraShake 0x572e50 every world step (0x60a144). */
typedef struct { float v[3]; int on; float t, dur, rate; } Shaker;
static Shaker g_shk[2];
static float shk_graph(float x)                       /* 0x5722a0 on 0x7fb36c */
{
    static const float P[5][2] = { {0, 1}, {.1f, .7f}, {.3f, .5f}, {.5f, .4f}, {1, 0} };
    if (x < P[0][0] || !(x < P[4][0])) return 0.0f;
    int i = 0;
    while (i < 5 && !(x < P[i][0])) i++;
    if (i > 0) i--;
    return P[i][1] + (P[i + 1][1] - P[i][1]) / (P[i + 1][0] - P[i][0]) * (x - P[i][0]);
}
static void shk_value(const Shaker *s, float out[3])  /* 0x572860 */
{
    float g = 0.0f;
    if (!(lrintf(s->rate * s->t) & 1) && s->on) g = shk_graph(s->t);
    for (int i = 0; i < 3; i++) out[i] = s->v[i] * g;
}
void chasecam_shakers_reset(void) { memset(g_shk, 0, sizeof g_shk); }   /* 0x5729e0 */
static void shk_tick(float dt)                        /* 0x5728c0 (the 0x8650c4 test shake: debug) */
{
    for (int k = 0; k < 2; k++) {
        Shaker *s = &g_shk[k];
        if (!s->on) continue;
        s->t += dt / s->dur;
        if (s->t < 0.0f || !(s->t < 1.0f)) s->on = 0;
    }
}
/* MaybeCameraShake 0x572e50: not for a drive camera in the bumper view
   (0x5ea7c0: camera type 1 with the option's view 3); a = acc x (0.7, 0.5,
   2) (0x7fb750); |a| >= 250 (0x79dcd4), capped 600 (0x79dcd8): f = (|a| -
   250) / 350, size 0.08 + 0.08 f (0x79dcdc / 0x79dce0), started only when
   above the running shake's graph value; duration 0.3 + 0.4 f (0x79dce4 /
   0x79dce8), rate 17 (0x79dc68); in jump cam zone 7: size x0.8, duration
   x1.2 (chasecam_set_world). */
static int g_cc_zone; static float g_cc_tscale = 1.0f;
void chasecam_set_world(int zone, float time_scale) { g_cc_zone = zone; g_cc_tscale = time_scale; }
static void shk_maybe(int k, const float acc[3])
{
    float a[3] = { 0.7f * acc[0], 0.5f * acc[1], 2.0f * acc[2] };
    float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (m < 250.0f) return;
    if (m > 600.0f) m = 600.0f;
    float f = (m - 250.0f) / (600.0f - 250.0f);
    float size = 0.08f + (0.16f - 0.08f) * f;
    if (g_cc_zone == 7) size *= 0.8f;
    Shaker *s = &g_shk[k];
    if (!(size > (s->on ? shk_graph(s->t) : 0.0f))) return;
    float dur = 0.3f + (0.7f - 0.3f) * f;
    if (g_cc_zone == 7) dur *= 1.2f;
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);   /* 0x43cf90 */
    for (int i = 0; i < 3; i++) s->v[i] = -(a[i] / n * size);
    s->on = 1; s->t = 0.0f; s->dur = dur; s->rate = 17.0f;
}
/* ApplyCameraShake 0x572a50 (GC 8019d9d8): views 1 / 3 (the player's and
   the mirror) take shaker 0, view 2 shaker 1; skipped while the world's
   time scale is below 1. s = the shake in camera space (eMulVector, no
   translation); R = RotX(ftol(-1200 (s.y + s.z))) RotY(ftol(2000 s.x))
   RotZ(0 (s.x + s.y)) (vShakeRotation 0x7fb740); V = V R */
static void shk_apply(int k, float V[4][4])
{
    float w[3], s[3];
    if (g_cc_tscale < 1.0f) return;
    shk_value(&g_shk[k], w);
    if (w[0] == 0.0f && w[1] == 0.0f && w[2] == 0.0f) return;
    for (int j = 0; j < 3; j++) s[j] = w[0] * V[0][j] + w[1] * V[1][j] + w[2] * V[2][j];
    float ax = (float)(((int)lrintf(-1200.0f * (s[1] + s[2]))) & 0xffff) * (6.2831853f / 65536.0f);
    float ay = (float)(((int)lrintf(2000.0f * s[0])) & 0xffff) * (6.2831853f / 65536.0f);
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
    float RX[4][4] = { {1, 0, 0, 0}, {0, cx, sx, 0}, {0, -sx, cx, 0}, {0, 0, 0, 1} };   /* 0x5bacf0 */
    float RY[4][4] = { {cy, 0, -sy, 0}, {0, 1, 0, 0}, {sy, 0, cy, 0}, {0, 0, 0, 1} };   /* 0x5bada0 */
    float R[4][4];
    mul44(RX, RY, R);
    mul44(V, R, V);
}

/* 0x444390 / 0x44ac10 rebuild: the eye moved; aim so `keep` (world) stays at
 * the same spot on screen, rolling about the old camera up. */
static void rebuild_keep(float V[4][4], const float eye[3], const float keep[3])
{
    float q[3]; ea_cam_point(V, keep, q);
    if (norm3(q) < 1e-6f || q[2] <= 0.0f) return;
    float up[3] = { -V[0][1], -V[1][1], -V[2][1] };
    float L[4][4]; ea_lookat(eye, keep, up, L);
    /* Q takes the camera direction q onto +z (Rodrigues) */
    float z[3] = { 0, 0, 1 }, v[3]; cross3(q, z, v);
    float c = q[2], k = 1.0f / (1.0f + c);
    float Q[3][3] = {
        { 1 - k*(v[1]*v[1]+v[2]*v[2]), -v[2] + k*v[0]*v[1],          v[1] + k*v[0]*v[2] },
        { v[2] + k*v[0]*v[1],          1 - k*(v[0]*v[0]+v[2]*v[2]),  -v[0] + k*v[1]*v[2] },
        { -v[1] + k*v[0]*v[2],         v[0] + k*v[1]*v[2],           1 - k*(v[0]*v[0]+v[1]*v[1]) } };
    /* V' = L * Q (row vectors): keep, straight ahead in L, lands on q */
    float N[4][4] = {{0}};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            N[i][j] = L[i][0]*Q[0][j] + L[i][1]*Q[1][j] + L[i][2]*Q[2][j];
    for (int i = 0; i < 3; i++) N[i][3] = 0.0f;
    N[3][3] = 1.0f;
    memcpy(V, N, sizeof N);
}

/* ---- load / select ------------------------------------------------------ */
static const CcInfo DEFAULT_INFO[6] = {
    { 0, 0, -5.8f, 1.7f, 0 }, { 1, 0, -4.7f, 1.7f, 0 }, { 2, 500, 0.936f, 1.01f, 0 },
    { 3, 0, -0.195f, 0.805f, -0.44f }, { 4, 0, 0.58f, 1.27f, 0 }, { 5, 3040, -5.8f, 3.45f, 0 } };

int chasecam_load(ChaseCam *c, const unsigned char *rec)
{
    memset(c, 0, sizeof *c);
    for (int m = 0; m < 2; m++) memcpy(c->info[m], DEFAULT_INFO, sizeof DEFAULT_INFO);
    int got = 0;
    if (rec) {
        int cnt[2]; memcpy(cnt, rec + 0x720, sizeof cnt);
        for (int m = 0; m < 2; m++) {
            if (cnt[m] < 1 || cnt[m] > 6) continue;
            for (int k = 0; k < cnt[m]; k++) {
                const unsigned char *e = rec + 0x730 + (m * 6 + k) * 16;
                CcInfo in;
                memcpy(&in.view, e, 2); memcpy(&in.pitch, e + 2, 2);
                memcpy(&in.x, e + 4, 4); memcpy(&in.z, e + 8, 4); memcpy(&in.y, e + 12, 4);
                if (in.view >= 0 && in.view < 6 && isfinite(in.x) && isfinite(in.z) && isfinite(in.y)) {
                    c->info[m][in.view] = in; got++;
                }
            }
        }
    }
    wcoll_ctx_init(&c->wctx);
    c->loaded = 1;
    c->setting = 3;
    c->view = setting_view(3);
    return got;
}

static void load_rec(ChaseCam *c, int view)
{
    const float (*r)[44];
    switch (view) {
    case 1: r = REC_CHASE_NEAR; break;
    case 2: case 3: r = REC_FIXED; break;
    case 4: r = REC_HOOD; break;
    default: r = REC_CHASE_FAR; break;
    }
    for (int i = 0; i < 44; i++) c->rec[i] = r[0][i] + (r[1][i] - r[0][i]) * 0.5f;   /* 0x445750 */
}

void chasecam_set(ChaseCam *c, int setting, const CcCar *car)
{
    (void)car;
    if (setting < 0 || setting > 4) setting = 3;
    c->setting = setting;
    c->view = setting_view(setting);
    c->started = 0;   /* next update builds the new mover (ctor 0x451660) */
    c->handover = 1;  /* ... taking over from the last camera */
}

void chasecam_snap(ChaseCam *c) { c->started = 0; c->handover = 0; }

/* 0x444530 / 0x444190 (CameraMover::MinGapTopology): keep the eye out of
   the world -- pulled in along the line from the car, pushed off nearby
   faces, never inside the car's own sphere */
static void keep_out_of_world(float V[4][4], WColl *w, WCollCtx *ctx, const float start[3], float R)
{
    float e[3]; ea_eye(V, e);
    float dv[3] = { e[0]-start[0], e[1]-start[1], e[2]-start[2] };
    float len = sqrtf(dot3(dv, dv));
    if (len > 1e-4f) {
        static WCollPoly P[192];
        float mid[3] = { (e[0]+start[0])*0.5f, (e[1]+start[1])*0.5f, (e[2]+start[2])*0.5f };
        float rad = (len + 1.0f) * 0.5f;
        float mn[2] = { mid[0]-rad, mid[1]-rad }, mx[2] = { mid[0]+rad, mid[1]+rad };
        int n = wcoll_gather(w, ctx, start, mn, mx, P, 192);
        float dir[3] = { dv[0]/len, dv[1]/len, dv[2]/len }, best = len;
        const float RAD = 0.7f;
        for (int k = 0; k < n; k++) {
            const WCollPoly *p = &P[k];
            int s = p->surface;
            if (s == 19 || (s >= 21 && s <= 28)) continue;   /* the camera passes these */
            float dn = dot3(dir, p->n);
            if (dn >= 0.0f) continue;                        /* front faces only */
            float t = (p->d - dot3(start, p->n)) / dn;
            if (t < 0.0f || t >= best) continue;
            float h[3] = { start[0]+dir[0]*t, start[1]+dir[1]*t, start[2]+dir[2]*t };
            int inside = 1;
            for (int j = 0; j < p->nv && inside; j++) {
                const float *a = p->v[j], *b = p->v[(j + 1) % p->nv];
                float ra[3] = { a[0]-h[0], a[1]-h[1], a[2]-h[2] }, rb[3] = { b[0]-h[0], b[1]-h[1], b[2]-h[2] }, x[3];
                cross3(ra, rb, x);
                if (dot3(x, p->n) < 0.0f) inside = 0;
            }
            if (inside) best = t;
        }
        int moved = 0;
        if (best < len) {
            float t = fmaxf(best - RAD, 0.0f);
            for (int i = 0; i < 3; i++) e[i] = start[i] + dir[i] * t;
            moved = 1;
        }
        for (int pass = 0; pass < 3; pass++) {   /* push out of nearby faces */
            float acc[3] = { 0, 0, 0 }; int cnt = 0;
            for (int k = 0; k < n; k++) {
                const WCollPoly *p = &P[k];
                int s = p->surface;
                if (s == 19 || (s >= 21 && s <= 28)) continue;
                float rel[4][3];
                for (int j = 0; j < p->nv; j++)
                    for (int i = 0; i < 3; i++) rel[j][i] = p->v[j][i] - e[i];
                int inside = 1;
                for (int j = 0; j < p->nv && inside; j++) {
                    float x[3]; cross3(rel[j], rel[(j + 1) % p->nv], x);
                    if (dot3(x, p->n) < 0.0f) inside = 0;
                }
                if (!inside) continue;
                float dd = -dot3(rel[0], p->n);
                if (dd >= 0.0f && dd < RAD) {
                    for (int i = 0; i < 3; i++) acc[i] += p->n[i] * (RAD - dd);
                    cnt++;
                }
            }
            if (!cnt) break;
            for (int i = 0; i < 3; i++) e[i] += acc[i] / (float)cnt;
            moved = 1;
        }
        float d2[3] = { e[0]-start[0], e[1]-start[1], e[2]-start[2] };
        if (dot3(d2, d2) < R * R) {   /* never inside the car's own sphere */
            float dxy = d2[0]*d2[0] + d2[1]*d2[1];
            if (dxy < R * R) { e[2] = start[2] + sqrtf(R*R - dxy); moved = 1; }
        }
        if (moved) rebuild_keep(V, e, start);
    }
}

/* ---- 0x445eb0 GetEyeOffsetAndAngle --------------------------------------- */
/* overrides of the car's own POV entry (x fwd, y left, z up; pitch u16) */
static const float BIRDSEYE_OFF[2][3] = { { -17.2f, 0, 6.0f }, { -15.8f, 0, 4.4f } }; /* 0x7ec400 vCubicBirdsEyeOffset */
static const unsigned short BIRDSEYE_PITCH[2] = { 5200, 3540 };                   /* 0x787080 aCubicBirdsEyeAngle */
static const float SMOKESHOW_OFF[3] = { -10.0f, 0, 5.0f };   /* 0x7ec5f0 SmokeShowEyeOffset */
static const unsigned short SMOKESHOW_PITCH = 0xbbb;         /* 0x82a4d0 (set by 0x77be70) */
static const float HYD_OFF[3] = { -5.0f, 0, 1.5f };          /* 0x7ec600 HydraulicsEyeOffset */
static const unsigned short HYD_PITCH = 0x38e;               /* 0x82a4dc (set by 0x77be80) */

static CcInfo eye_offset(const ChaseCam *c, const CcCar *car, int pov)
{
    CcInfo o = c->info[0][pov];
    const float *t = NULL; unsigned short p = 0;
    if (car->burnout) { t = SMOKESHOW_OFF; p = SMOKESHOW_PITCH; }            /* 0x89e7e2 || 0x890100 */
    else if (c->lookback && c->birdseye) {                                   /* mover +0x51, player +0x2b == 0 */
        t = BIRDSEYE_OFF[0]; p = BIRDSEYE_PITCH[0];                          /* [eGetCurrentViewMode() == 3] */
    } else if (car->hyd_mode) { t = HYD_OFF; p = HYD_PITCH; }                /* driver vt+0x54 */
    if (t) { o.x = t[0]; o.y = t[1]; o.z = t[2]; o.pitch = p; }
    return o;
}

/* 0x445ac0 HighliteMode: OutsidePOV (0x445a90, the POV in use is 0/1/5)
   and hydraulics at work, or the dampen mode with two wheels or fewer down */
static int highlite(const ChaseCam *c, const CcCar *car)
{
    if (!is_chase(c->pov_active)) return 0;
    return car->hyd_active || (car->dampen && car->wheels_down <= 2);
}

/* 0x5f14f0: p is within r of another car's model box (the car list, the
   followed car left out); the distance gate compares the squared distance
   with r + 5 (sic) */
static int in_other_car(const float p[3], float r, const CcObstacle *obs, int nobs)
{
    for (int k = 0; k < nobs; k++) {
        const CcObstacle *o = &obs[k];
        float d[3] = { p[0]-o->pos[0], p[1]-o->pos[1], p[2]-o->pos[2] };
        if (!(dot3(d, d) < r + 5.0f)) continue;
        float l[3] = { dot3(d, o->rot), dot3(d, o->rot + 3), dot3(d, o->rot + 6) };
        int in = 1;                                                          /* 0x43d7a0 */
        for (int i = 0; i < 3 && in; i++)
            if (o->bbmin[i] - r > l[i] || r + o->bbmax[i] < l[i]) in = 0;
        if (in) return 1;
    }
    return 0;
}

/* TopologyCoordinate::ConstrainToTopology 0x5d39f0 (type 1, two-sided,
   radius 0.5) as a test: does a world face cross the segment p -> q
   (0x5d37f0 over the polygons around it)? */
static int seg_blocked(WColl *w, WCollCtx *ctx, const float p[3], const float q[3])
{
    static WCollPoly P[96];
    float dv[3] = { q[0]-p[0], q[1]-p[1], q[2]-p[2] };
    float len = sqrtf(dot3(dv, dv));
    if (len < 1e-5f) return 0;
    float mn[2] = { fminf(p[0], q[0]) - 0.5f, fminf(p[1], q[1]) - 0.5f };
    float mx[2] = { fmaxf(p[0], q[0]) + 0.5f, fmaxf(p[1], q[1]) + 0.5f };
    int n = wcoll_gather(w, ctx, p, mn, mx, P, 96);
    for (int k = 0; k < n; k++) {
        const WCollPoly *f = &P[k];
        float dn = dot3(dv, f->n);
        if (fabsf(dn) < 1e-9f) continue;
        float t = (f->d - dot3(p, f->n)) / dn;
        if (t < 0.0f || t > 1.0f) continue;
        float h[3] = { p[0]+dv[0]*t, p[1]+dv[1]*t, p[2]+dv[2]*t };
        int inside = 1;
        for (int j = 0; j < f->nv && inside; j++) {
            const float *a = f->v[j], *b = f->v[(j + 1) % f->nv];
            float ra[3] = { a[0]-h[0], a[1]-h[1], a[2]-h[2] }, rb[3] = { b[0]-h[0], b[1]-h[1], b[2]-h[2] }, x[3];
            cross3(ra, rb, x);
            if (dot3(x, f->n) < 0.0f) inside = 0;
        }
        if (inside && t < 1.0f) return 1;                 /* nearer than the segment's end */
    }
    return 0;
}

int chasecam_seg_blocked(WColl *w, const float p[3], const float q[3])
{
    static WCollCtx ctx; static int init;
    if (!w) return 0;
    if (!init) { wcoll_ctx_init(&ctx); init = 1; }
    return seg_blocked(w, &ctx, p, q);
}

/* the shared camera's state after a frame (camera +0x40 eye, +0x60 target =
   eye + forward x +0xb0, the distance to the followed car set by 0x453570,
   +0xc4 fov; the rates Camera::SetCameraMatrix keeps from the last frame) */
static void cam_record(ChaseCam *c, const CcOut *o, const float car_pos[3], float dt)
{
    float d[3], tgt[3], fov = o->hfov43 * (65536.0f / 6.2831853f);
    if (c->cam_valid) for (int i = 0; i < 3; i++) d[i] = c->cam_eye[i] - car_pos[i];
    else for (int i = 0; i < 3; i++) d[i] = o->eye[i] - car_pos[i];
    float dist = sqrtf(dot3(d, d));
    for (int i = 0; i < 3; i++) tgt[i] = o->eye[i] + o->fwd[i] * dist;
    if (c->cam_valid && dt > 1e-5f) {
        for (int i = 0; i < 3; i++) {
            c->cam_veye[i] = (o->eye[i] - c->cam_eye[i]) / dt;
            c->cam_vtgt[i] = (tgt[i] - c->cam_tgt[i]) / dt;
        }
        c->cam_vfov = (fov - c->cam_fov) / dt;
    } else {
        memset(c->cam_veye, 0, sizeof c->cam_veye); memset(c->cam_vtgt, 0, sizeof c->cam_vtgt);
        c->cam_vfov = 0.0f;
    }
    memcpy(c->cam_eye, o->eye, sizeof c->cam_eye);
    memcpy(c->cam_tgt, tgt, sizeof c->cam_tgt);
    c->cam_fov = fov;
    c->cam_valid = 1;
}
void chasecam_observe(ChaseCam *c, const CcOut *o, const float car_pos[3], float dt)
{
    cam_record(c, o, car_pos, dt);
    c->started = 0; c->handover = 1;   /* the drive camera's ctor takes over from it */
}

/* 0x444980 (eye) / 0x444a70 (look): the cubic starts where the camera's point
   will be after dt, in the new mover's frame (rows negated for look back),
   moving at the camera's rate less the car's, x the cubic's duration */
static void hand_over3(CcCubic *cb, const float pt[3], const float rate[3], const float cvel[3],
                       float F[3][3], const float org[3], float sg, float dt)
{
    float p[3], v[3];
    for (int i = 0; i < 3; i++) { p[i] = pt[i] + rate[i] * dt - org[i]; v[i] = (rate[i] - cvel[i]) * cb[0].dur; }
    for (int i = 0; i < 3; i++) {
        float s = i < 2 ? sg : 1.0f;
        float val = s * dot3(p, F[i]), vel = s * dot3(v, F[i]);
        cb[i].val = val; if (val != cb[i].tgt) cb[i].state = 2;    /* 0x570ae0 */
        cb[i].vel = vel; if (vel != cb[i].tvel) cb[i].state = 2;   /* 0x570b40 */
    }
}

/* ---- 0x453570 per frame -------------------------------------------------- */
void chasecam_update(ChaseCam *c, const CcCar *car, float dt,
                     WColl *w, const CcObstacle *obs, int nobs, CcOut *out)
{
    int reset = !c->started;
    const float *fwd = car->rot, *cup = car->rot + 6;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    c->now += dt;
    if (reset) c->pov_active = c->view;                 /* ctor: +0x48 = +0x4c = the POV */
    if (reset) chasecam_shakers_reset();                /* Player::DefaultDriveCamera 0x5fa862 */

    float ctr[3], start[3];
    for (int i = 0; i < 3; i++) ctr[i] = 0.5f * (car->bbmin[i] + car->bbmax[i]);
    for (int i = 0; i < 3; i++)
        start[i] = car->pos[i] + ctr[0]*car->rot[i] + ctr[1]*car->rot[3 + i] + ctr[2]*car->rot[6 + i];

    /* 0x453570: the bumper and hood views fall back on POV 0 while they
       cannot be used -- hydraulics mode, the hold, the eye inside another
       car (CameraWouldBeInCar 0x445fc0), or a face within 2 m along the
       car's up from its centre (the car on its side or roof) */
    int snap = 0, act = c->view;
    if (act == 4 || act == 2) {
        int hl = highlite(c, car);
        CcInfo eo = eye_offset(c, car, c->view);
        float ep[3];
        for (int i = 0; i < 3; i++)
            ep[i] = car->pos[i] + eo.x*car->rot[i] + eo.y*car->rot[3 + i] + eo.z*car->rot[6 + i];
        int inside = in_other_car(ep, 0.5f, obs, obs ? nobs : 0);         /* 0x787088 */
        int keep = 0;
        if (!car->hyd_mode && !hl && !inside) {
            /* p = car + model +0x110 (GUESS: the model box centre), q = p
               + up x 2 (0x78715c) */
            float q[3] = { start[0] + cup[0]*2.0f, start[1] + cup[1]*2.0f, start[2] + cup[2]*2.0f };
            if (!w || !seg_blocked(w, &c->wctx, start, q)) keep = 1;
        }
        if (keep) snap |= c->pov_active == 0;
        else { snap |= c->pov_active != 0; act = 0; }
    }
    c->pov_active = act;
    int view = act, chase = is_chase(view);

    load_rec(c, view);                                  /* 0x446dc0 on +0x4c, every frame */
    /* 0x453570: no eye lag (gains [16..18]) in a drift race or the smoke show */
    if (car->drift || car->burnout) c->rec[16] = c->rec[17] = c->rec[18] = 0.0f;
    if (reset) {   /* ctor 0x451660 */
        for (int i = 0; i < 3; i++) {
            cub_init(&c->dir[i], fwd[i], c->rec[40 + i]);
            cub_init(&c->eye[i], 0, c->rec[0]);
            cub_init(&c->look[i], 0, c->rec[1]);
            cub_init(&c->up[i], i == 2 ? 1.0f : 0.0f, c->rec[3]);
        }
        cub_init(&c->fov, VIEW_FOV[view], c->rec[2]);
        c->acc_n = c->acc_i = 0;
        memset(c->acc_sum, 0, sizeof c->acc_sum);
        c->t_ground = c->now;
        c->started = 1;
    }

    /* 0x446f10: four-sample moving average of the car's acceleration */
    if (c->acc_n == 4) for (int i = 0; i < 3; i++) c->acc_sum[i] -= c->acc_ring[c->acc_i][i];
    else c->acc_n++;
    for (int i = 0; i < 3; i++) { c->acc_ring[c->acc_i][i] = car->acc[i]; c->acc_sum[i] += car->acc[i]; }
    c->acc_i = (c->acc_i + 1) & 3;
    for (int i = 0; i < 3; i++) c->acc_avg[i] = c->acc_sum[i] / (float)c->acc_n;
    const float *aw = c->acc_avg;

    int hold = highlite(c, car);                        /* 0x445ac0 */
    cub3_dur(c->eye, hold ? c->rec[0] / 1.3f : c->rec[0]);
    cub3_dur(c->look, c->rec[1]);
    c->fov.dur = c->rec[2];
    cub3_dur(c->up, c->rec[3]);
    for (int i = 0; i < 3; i++) {
        cub_step(&c->dir[i], dt); cub_step(&c->eye[i], dt);
        cub_step(&c->look[i], dt); cub_step(&c->up[i], dt);
    }
    cub_step(&c->fov, dt);

    /* 0x44de90 SetDesired(frame, POV, record, snap = +0x52) */
    int sn = reset || snap;
    /* 0x445c20 follow direction */
    if (car->wheels_down > 1) c->t_ground = c->now;
    if (chase) {
        if (sn || !hold) {
            float v[3] = { car->vel[0], car->vel[1], car->vel[2] };
            float vf = dot3(v, fwd);
            if (vf < 0.0f) for (int i = 0; i < 3; i++) v[i] += fwd[i] * (-2.0f * vf);
            float t = clampf((car->speed - 8.0f) / 5.0f, 0.0f, 1.0f);   /* graph 0x82a4e0 */
            norm3(v);
            for (int i = 0; i < 3; i++) v[i] = v[i] * t + fwd[i] * (1.0f - t);
            v[2] *= clampf(1.0f - (float)(c->now - c->t_ground) / 2.0f, 0.0f, 1.0f);
            cub3_target(c->dir, v);
            if (sn) cub3_snap(c->dir);
        }
    } else {
        cub3_target(c->dir, fwd);
        cub3_snap(c->dir);
    }

    /* 0x445e00 frame: chase follows the direction, the rest ride the car */
    float F[3][3];
    if (chase) {
        float d[3] = { c->dir[0].val, c->dir[1].val, c->dir[2].val }, z[3] = { 0, 0, 1 };
        if (norm3(d) < 1e-6f) memcpy(d, fwd, sizeof d);
        memcpy(F[0], d, sizeof d);
        cross3(z, F[0], F[1]);
        cross3(F[0], F[1], F[2]);
    } else memcpy(F, car->rot, sizeof F);
    const float *org = car->pos;
    float al[3];
    for (int i = 0; i < 3; i++) {
        float l2 = dot3(F[i], F[i]);
        al[i] = l2 > 1e-12f ? dot3(aw, F[i]) / l2 : 0.0f;
    }

    /* 0x445eb0 offsets */
    CcInfo in = eye_offset(c, car, view);
    float pa = in.pitch * (6.2831853f / 65536.0f);
    float tn = tanf(pa);
    float off[3] = { in.x, in.y, in.z }, lk[3];
    if (chase) { lk[0] = 0.0f; lk[1] = 0.0f; lk[2] = in.x * tn + in.z; }
    else       { lk[0] = in.x + 1.0f; lk[1] = 0.0f; lk[2] = in.z - tn; }   /* the look has no lateral part */
    float om = hold ? 1.3f : 1.0f;

    const float *r = c->rec;
    float et[3], lt[3], ut[3];
    for (int i = 0; i < 3; i++) {
        et[i] = clampf(al[i] * r[16 + i], r[20 + i], r[24 + i]) + off[i] * om;
        lt[i] = clampf(al[i] * r[28 + i], r[32 + i], r[36 + i]) + lk[i];
    }
    ut[0] = clampf(r[4] * aw[0], r[8], r[12]);
    ut[1] = clampf(r[5] * aw[1], r[9], r[13]);
    ut[2] = clampf(r[6] * aw[2] + 1.0f, r[10], r[14]);
    cub3_target(c->eye, et); cub3_target(c->look, lt); cub3_target(c->up, ut);
    cub_target(&c->fov, VIEW_FOV[view]);
    float sg = c->lookback && !c->birdseye ? -1.0f : 1.0f;   /* 0x453570 look back */
    int handed = 0;
    if (reset && c->handover && c->cam_valid) {
        /* ctor 0x451660: 0x444910 fov, 0x444980 eye, 0x444a70 look; snapped
           when the eye is more than 20 m (0x78714c) from its target */
        c->fov.val = c->cam_fov + c->cam_vfov * dt; if (c->fov.val != c->fov.tgt) c->fov.state = 2;
        c->fov.vel = c->cam_vfov * c->fov.dur;      if (c->fov.vel != c->fov.tvel) c->fov.state = 2;
        hand_over3(c->eye, c->cam_eye, c->cam_veye, car->vel, F, org, sg, dt);
        hand_over3(c->look, c->cam_tgt, c->cam_vtgt, car->vel, F, org, sg, dt);
        float e2 = 0.0f;
        for (int i = 0; i < 3; i++) e2 += (c->eye[i].val - c->eye[i].tgt) * (c->eye[i].val - c->eye[i].tgt);
        if (sqrtf(e2) > 20.0f) { cub_snap(&c->fov); cub3_snap(c->eye); cub3_snap(c->look); }
        handed = 1;
    }
    c->handover = 0;
    if ((sn && !handed) || view == 2 || view == 3) {
        cub3_snap(c->eye); cub3_snap(c->look); cub3_snap(c->up); cub_snap(&c->fov);
    }

    float eye[3], look[3], up[3];
    for (int i = 0; i < 3; i++) {
        eye[i]  = org[i] + sg*c->eye[0].val*F[0][i]  + sg*c->eye[1].val*F[1][i]  + c->eye[2].val*F[2][i];
        look[i] = org[i] + sg*c->look[0].val*F[0][i] + sg*c->look[1].val*F[1][i] + c->look[2].val*F[2][i];
    }
    if (chase) { up[0] = c->up[0].val; up[1] = c->up[1].val; up[2] = 1.0f; }
    else memcpy(up, cup, sizeof up);
    c->fov_u16 = (unsigned short)clampf(c->fov.val, 0x800, 0x7000);
    ea_lookat(eye, look, up, c->V);

    float ext[3] = { car->bbmax[0]-ctr[0], car->bbmax[1]-ctr[1], car->bbmax[2]-ctr[2] };
    float R = sqrtf(dot3(ext, ext)) + 1.0f;

    /* 0x444530 / 0x444190: keep the eye out of the world (chase views) */
    if (chase && w) keep_out_of_world(c->V, w, &c->wctx, start, R);

    /* 0x44b040 / 0x44ac10 (MinGapCars, traffic only): rise over cars beside
       the eye */
    if (chase && obs && nobs > 0) {
        float e[3]; ea_eye(c->V, e);
        int moved = 0;
        for (int iter = 0; iter < 4; iter++) {
            int hit = 0;
            for (int k = 0; k < nobs; k++) {
                const CcObstacle *o = &obs[k];
                if (!o->traffic) continue;                  /* driver type 3 */
                float dc[3] = { o->pos[0]-e[0], o->pos[1]-e[1], o->pos[2]-e[2] };
                if (!(dot3(dc, dc) < 144.0f)) continue;     /* 12 m (0x7870c8) of the eye */
                /* a car passing fast (relative speed over 15 m/s, 0x7870c4) is
                   left alone unless it lies along that motion from the eye or
                   the eye from the followed car (dots 0.8, 0x7870c0) */
                float rv[3] = { car->vel[0]-o->vel[0], car->vel[1]-o->vel[1], car->vel[2]-o->vel[2] };
                if (dot3(rv, rv) > 225.0f) {
                    float dn[3] = { dc[0], dc[1], dc[2] };
                    norm3(dn); norm3(rv);
                    if (dot3(dn, rv) < 0.8f) {
                        float ef[3] = { e[0]-org[0], e[1]-org[1], e[2]-org[2] };
                        norm3(ef);
                        if (dot3(ef, rv) < 0.8f) continue;
                    }
                }
                float oc[3], oe[3], c3[3];
                for (int i = 0; i < 3; i++) { c3[i] = 0.5f*(o->bbmin[i]+o->bbmax[i]); oe[i] = o->bbmax[i]-c3[i]; }
                for (int i = 0; i < 3; i++)
                    oc[i] = o->pos[i] + c3[0]*o->rot[i] + c3[1]*o->rot[3+i] + c3[2]*o->rot[6+i];
                float R2 = sqrtf(dot3(oe, oe)) + 1.0f; R2 *= R2;
                float de[3] = { e[0]-oc[0], e[1]-oc[1], e[2]-oc[2] };
                if (dot3(de, de) >= R2) continue;
                float zmn = 1e30f, zmx = -1e30f;
                for (int b = 0; b < 8; b++) {
                    float p[3] = { b & 1 ? o->bbmax[0] : o->bbmin[0], b & 2 ? o->bbmax[1] : o->bbmin[1],
                                   b & 4 ? o->bbmax[2] : o->bbmin[2] };
                    float z = o->pos[2] + p[0]*o->rot[2] + p[1]*o->rot[5] + p[2]*o->rot[8];
                    zmn = fminf(zmn, z); zmx = fmaxf(zmx, z);
                }
                if (e[2] < zmn - 0.5f || e[2] > zmx + 0.5f) continue;
                float dxy = de[0]*de[0] + de[1]*de[1];
                float nz = sqrtf(fmaxf(R2 + 0.1f - dxy, 0.0f)) + oc[2];
                if (nz > e[2]) { e[2] = nz; hit = moved = 1; }
            }
            if (!hit) break;
        }
        if (moved) rebuild_keep(c->V, e, org);
    }

    /* the world step's MaybeCameraShake (0x60a144), the frame's
       UpdateCameraShakers (0x5814b2), then ApplyCameraShake (0x453b55,
       before the view's noise shake) */
    if (c->view != 3) shk_maybe(0, car->acc);
    shk_tick(dt);
    shk_apply(0, c->V);
    /* 0x452ed0 / 0x441340 shake */
    {
        static const float TAB[5][4] = { {0,0,1,1}, {.1f,.5f,1.2f,1.1f}, {.3f,.8f,1.5f,1.2f},
                                          {.6f,1,2,1.3f}, {1,1,3,1.4f} };
        float top = car->vtop > 1.0f ? car->vtop : 100.0f;
        float x = car->speed * 4.0f / top, row[4];
        int i = (int)lrintf(x);   /* 0x446d00, round-to-nearest ftol and all */
        if (i < 0) memcpy(row, TAB[0], sizeof row);
        else if (i >= 4) memcpy(row, TAB[4], sizeof row);
        else {
            float base = (float)i; if (x < base) base -= 1.0f;
            float t = x - base;
            for (int k = 0; k < 4; k++) row[k] = TAB[i][k] + (TAB[i + 1][k] - TAB[i][k]) * t;
        }
        float bf = 0, ba = 0;
        for (int k = 0; k < 4; k++) {
            int s = car->surface[k]; if (s < 0 || s >= WCOLL_NSURF) s = 1;
            bf += wcoll_surfaces[s].bump_freq; ba += wcoll_surfaces[s].bump_amp;
        }
        bf *= 0.25f; ba = 10.0f * ba * 0.25f;
        /* 0x453570: the view's scale 0x7ebe90 (1.0 for every view), x0.2
           (0x7870b8) outside a drag race */
        float l6c = row[0] * (car->drag ? 1.0f : 0.2f), l70 = row[2];
        if (car->nitrous) { l6c += 0.2f; l70 *= 1.4f; }
        float p4 = row[1] * ba * 1.0f, l44 = row[3] * bf;
        static const float F1[4] = { 7.5f, 6.875f, 6.125f, 5 }, A1[4] = { .003f, .002f, .7f, .5f },
                           F2[4] = { 3, 5, 7, 5.5f },        A2[4] = { .007f, .01f, .6f, .8f };
        /* the noise runs at 22 Hz and its octaves far above: retail point
           samples it at 60 fps; at 30 fps a point sample is a random jump
           every frame (the drag race's full-scale shake judders), so below
           50 fps the shake is the mean over the last 1.5 frames, 6 sub-
           samples: the frame-to-frame jump then equals retail's at 60 fps
           (modelled: 0.34 vs 0.33 at the drag's 22.5 Hz; 30 fps unfiltered
           0.52), the shake's overall size ~85% */
        float t = (float)c->now, nz[4];
        int ns = dt > 1.0f / 50.0f ? 6 : 1;
        for (int k = 0; k < 4; k++) {
            float sum = 0.0f;
            for (int j = 0; j < ns; j++) {
                float tj = t - 1.5f * dt * (float)j / (float)ns;
                sum += noise6(F1[k] * l70 * tj) * A1[k] * l6c + noise6(F2[k] * l44 * tj) * A2[k] * p4;
            }
            nz[k] = sum / (float)ns;
        }
        chasecam_shake_view(c->V, nz, c->fov_u16);
    }

    chasecam_out(c->V, c->fov_u16, out);
    out->znear = view == 3 ? 0.4f : 0.5f;   /* camera +0xbc: 0x7eb5b0 / 0x784260 */
    /* 0x453570 (camera +0x280): the car is not drawn from inside it -- the
       POV in use not an outside one (ids 0, 1, 5), and not the hood view
       (+0x4c == 4) unless Look Back (+0x51) is held */
    out->hide_car = !(view == 0 || view == 1 || view == 5) && (c->lookback || view != 4);
    cam_record(c, out, car->pos, dt);
}

float chasecam_noise(float x) { return noise6(x); }
void chasecam_lookat(const float eye[3], const float at[3], float V[4][4])
{
    static const float up[3] = { 0, 0, 1 };
    ea_lookat(eye, at, up, V);
}
void chasecam_shake_view(float V[4][4], const float nz[4], unsigned short fov_u16)
{
    {
        float hf = fov_u16 * 0.5f * (6.2831853f / 65536.0f);
        float sh = sinf(hf) / 0.5f, ang[2];
        for (int k = 0; k < 2; k++) {
            float a = (float)((int)(nz[2 + k] * 65536.0f) / 360) * (6.2831853f / 65536.0f);
            ang[k] = asinf(clampf(sinf(a) * sh, -1.0f, 1.0f));   /* 0x4412e0 */
        }
        float M[4][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {nz[0], nz[1], 0, 1} };
        float cx = cosf(ang[0]), sx = sinf(ang[0]), cy = cosf(ang[1]), sy = sinf(ang[1]);
        float Rx[4][4] = { {1,0,0,0}, {0,cx,sx,0}, {0,-sx,cx,0}, {0,0,0,1} };
        float Ry[4][4] = { {cy,0,-sy,0}, {0,1,0,0}, {sy,0,cy,0}, {0,0,0,1} };
        mul44(M, Rx, M); mul44(M, Ry, M);
        mul44(V, M, V);
    }
}
/* ShowcaseCameraMover (mover 0x14): BuildPhotoCameraMatrix PC 0x446400 from
   SetShowcaseCameraParams' nine floats (SCCarSetup +8..+0x28): the target
   the car's position plus p[3..5] in its frame on the ground (forward
   flattened onto the ground's normal, left, the normal); the eye at
   (cos az, sin az, sin el) x p[2] from it in that frame, az p[0] and el p[1]
   degrees (at least 3); looking at it with up +z; MinGapTopology; the FOV
   p[6] degrees (Update 800764a0) */
static float photo_ang(float deg) { return (float)((int)(deg * 65536.0f) / 360) * (6.2831853f / 65536.0f); }
void chasecam_photo(ChaseCam *c, const CcCar *car, const float gup[3], const float p[9], WColl *w, CcOut *out)
{
    /* the normal = TopologyCoordinate::NewGetElevation(car position, 0,
       &normal, 0) (PC 0x446400): the road's under the car; the car's up
       only when there is no collision polygon there */
    float n[3] = { gup[0], gup[1], gup[2] }, f[3], l[3];
    if (w) {
        float gn[3]; int found = 0;
        wcoll_ground(w, &c->wctx, car->pos, NULL, gn, &found);
        if (found && gn[2] > 0.0f) { n[0] = gn[0]; n[1] = gn[1]; n[2] = gn[2]; }
    }
    if (norm3(n) < 1e-6f) { n[0] = n[1] = 0.0f; n[2] = 1.0f; }
    float fd = dot3(car->rot, n);
    for (int i = 0; i < 3; i++) f[i] = car->rot[i] - fd * n[i];
    if (norm3(f) < 1e-6f) { f[0] = 1.0f; f[1] = f[2] = 0.0f; }
    cross3(n, f, l); norm3(l);
    float tgt[3], eye[3], el = p[1] < 3.0f ? 3.0f : p[1];
    float az = photo_ang(p[0]), ea = photo_ang(el);
    float o[3] = { cosf(az) * p[2], sinf(az) * p[2], sinf(ea) * p[2] };
    for (int i = 0; i < 3; i++) {
        tgt[i] = car->pos[i] + p[3] * f[i] + p[4] * l[i] + p[5] * n[i];
        eye[i] = tgt[i] + o[0] * f[i] + o[1] * l[i] + o[2] * n[i];
    }
    static const float up[3] = { 0, 0, 1 };
    ea_lookat(eye, tgt, up, c->V);
    if (w) {
        float ctr[3], start[3];
        for (int i = 0; i < 3; i++) ctr[i] = 0.5f * (car->bbmin[i] + car->bbmax[i]);
        for (int i = 0; i < 3; i++)
            start[i] = car->pos[i] + ctr[0]*car->rot[i] + ctr[1]*car->rot[3 + i] + ctr[2]*car->rot[6 + i];
        float ext[3] = { car->bbmax[0]-ctr[0], car->bbmax[1]-ctr[1], car->bbmax[2]-ctr[2] };
        keep_out_of_world(c->V, w, &c->wctx, start, sqrtf(dot3(ext, ext)) + 1.0f);
    }
    c->fov_u16 = (unsigned short)clampf((float)((int)(p[6] * 65536.0f) / 360), 0x800, 0x7000);
    chasecam_out(c->V, c->fov_u16, out);
    cam_record(c, out, car->pos, 0.0f);
    c->started = 0;   /* the drive camera's next ctor takes over from it */
    c->handover = 1;
}
/* RearViewMirrorCameraMover::Update (PC 0x44ba80 / GC 8006cadc): the car's
   matrix transposed, eRotateX 0x4000, eRotateY 0x4000, eRotateZ 0 -- camera
   x = the car's left, y = its down, z = its back: a rigid camera at the car
   looking backwards -- with the translation at -pos, plus RVMOffsetInCar in
   camera space (PC folds it to (0, 0.5, 0) at 0x7ec4c0: 0.5 m up; GC data
   has 0); FOV 20000 (unless Camera::StopUpdating), near 0.75 (0x7870d4), far
   192 (0x7870d8). ApplyCameraShake (0x44be7b, GC 8019d9d8) with the
   player's shaker 0 (shk_apply). */
void chasecam_rvm(const CcCar *car, CcOut *out)
{
    const float *fwd = car->rot, *up = car->rot + 6;
    float eye[3], at[3], V[4][4];
    for (int i = 0; i < 3; i++) { eye[i] = car->pos[i] + 0.5f * up[i]; at[i] = eye[i] - fwd[i]; }
    ea_lookat(eye, at, up, V);
    shk_apply(0, V);                                   /* 0x44be7b: view 3 -> shaker 0 */
    chasecam_out(V, 20000, out);
    out->znear = 0.75f;
    out->hide_car = 1;   /* ctor 0x44ba10: camera +0x280 = the car, +0x284 = 0 */
}
/* eViewPlatInfo::CalculateViewMatricies 0x5c7ec0 (GC CreateViewMatricies 801d8c68), player views
   1/2: the half-angles are scaled linearly -- x = 1.1 x fov/2, y = (H/W) x (1.1/0.8696) x fov/2 --
   with H/W of the view's own target (the Vita screen here; retail on a 16:9 screen does the same).
   Out: the GL vertical fov and the aspect for mat_persp (x scale = cot(y)/aspect = cot(x)). */
void retail_view_proj(float fov, int W, int H, float *fovy, float *aspect)
{
    float hx = 0.55f * fov, hy = (float)H / (float)W * (1.1f / 0.8696f) * 0.5f * fov;
    *fovy = 2.0f * hy;
    *aspect = tanf(hx) / tanf(hy);
}

void chasecam_out(float V[4][4], unsigned short fov_u16, CcOut *out)
{
    ea_eye(V, out->eye);
    for (int i = 0; i < 3; i++) {
        out->fwd[i] = V[i][2];
        out->up[i] = -V[i][1];
        out->look[i] = out->eye[i] + out->fwd[i];
    }
    static const float SG[4] = { 1, -1, -1, 1 };   /* EA camera -> GL eye space */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out->V[i * 4 + j] = V[i][j] * SG[j];
    out->hfov43 = fov_u16 * (6.2831853f / 65536.0f);
    out->fovy = 2.0f * atanf(tanf(out->hfov43 * 0.5f) * 0.75f);
    out->znear = 0.0f;
    out->hide_car = 0;
}
