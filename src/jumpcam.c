/* UG2 Jump Cams: Player camera zones 6 / 7 with the world's slow motion.
 * See jumpcam.h for the retail functions; the decode notes are in GAPS.md
 * (Camera: "Crash cam + crash slow-mo = UG2 Jump Cams"). */
#include "jumpcam.h"
#include "airacer.h"
#include "n2sim.h"
#include "racepath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define JC_STEP (1.0f / 60.0f)          /* the world step the port runs */

static struct {
    int    zone, prev;                  /* player +0x58 / +0x5c */
    double now;                         /* world time, s (0x86518c / 4000) */
    float  scale, left;                 /* World +0x108 / +0x10c */
    double stamp[8];                    /* player +0x68 + 4 zone: the zone's last start, < 0 never */
    double t544;                        /* car +0x544: when the jump shot gives up */
    double t548;                        /* car +0x548: SetDampenMode's time */
    int    last_dampen;
    unsigned frame;                     /* 0x7fb724 */
    int    first, last;                 /* player +0x644 / +0x648 */
    int    cam;                         /* the TrackCarCameraMover runs (not the Cubic fallback) */
    float  eye[3], second[3];           /* player +0x270 / +0x290 */
    float  side;                        /* CameraDataItems +0x88 (+0x84 = 0.2 up) */
    float  f0, f1;                      /* CameraDataItems +0xc / +0x10: the race fraction range
                                           the splines run over */
    WCollCtx ctx; int ctx_ok;
} J = { .scale = 1.0f, .stamp = { -1, -1, -1, -1, -1, -1, -1, -1 }, .first = -1, .last = -1, .f1 = 1.0f };

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void set_scale(float s, float dur)                       /* World::SetTimeScale 0x5ea280 */
{
    J.scale = s; J.left = dur;
    n2sim_set_time_scale(s);
}

static float ground_at(WColl *w, const float p[3], float n[3])
{
    float nn[3] = { 0, 0, 1 };
    int surf = 0, found = 0;
    if (!w) { if (n) memcpy(n, nn, sizeof nn); return p[2]; }
    if (!J.ctx_ok) { wcoll_ctx_init(&J.ctx); J.ctx_ok = 1; }
    float z = wcoll_ground(w, &J.ctx, p, &surf, n ? n : nn, &found);
    if (!found && n) { n[0] = 0; n[1] = 0; n[2] = 1; }
    return z;
}

/* the Graphs FinishMovement reads at x = ftol(player +0x24) & 0xff (0x75bed4
   truncates: the option's 127.5 is 127): two-point lines (GAPS 10-29) */
static float graph(float y0, float y1, float x) { return y0 + (y1 - y0) * x / 255.0f; }

/* Car::AverageAir 0x5ec3d0: the car's tyre bottoms, averaged, flown ahead
   ballistically for `horizon` seconds in ftol(speed / 3 x horizon) steps
   (sliding along the ground while on it, with gravity + the downforce);
   max = the highest point over the ground, air = how long it is in the air
   (with stop: until it first comes down). Returns the summed height / steps. */
static float average_air(const JcCar *c, WColl *w, float horizon, float *pmax, float *pair, int stop)
{
    float speed = c->speed;
    int steps = (int)(speed / 3.0f * horizon);                          /* 0x7a5648 */
    if (steps < 1) { if (pmax) *pmax = 0; if (pair) *pair = 0; return 0.0f; }
    const float *up = c->rot + 6;
    float st[3] = { 0, 0, 0 };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 3; i++) st[i] += 0.25f * (c->wheel_pos[k][i] - c->wheel_r[k] * up[i]);
    float h0 = st[2] - ground_at(w, st, NULL);
    float sum = 0.0f < h0 ? h0 : 0.0f, mx = sum;                       /* 0 <= h0 kept, else 0 */
    float dt = horizon / (float)steps;
    float air = sum > 0.0f ? dt : 0.0f, last = sum > 0.0f ? -dt : 0.0f;
    float down = c->mass > 0 ? c->aero * 0.3f * speed / c->mass : 0.0f;   /* 0x7a564c */
    float v[3] = { c->vel[0], c->vel[1], c->vel[2] };
    float acc = sum > 0.0f ? -0.0f : -(9.8f + down);                    /* 0x7fbef0 */
    int i = 1;
    for (; i < steps; i++) {
        float t = (float)i * dt, tau = t - last;
        float p[3], n[3];
        float a3[3] = { 0, 0, acc };
        for (int k = 0; k < 3; k++) p[k] = st[k] + (v[k] + a3[k] * (tau * 0.5f)) * tau;
        float gz = ground_at(w, p, n);
        float hh = p[2] - gz;
        if (hh <= 0.0f) {
            if (stop && air > 0.0f) { air = tau; break; }
            st[0] = p[0]; st[1] = p[1]; st[2] = gz;
            acc = -(9.8f + down);
            float x[3] = { n[1] * v[2] - n[2] * v[1], n[2] * v[0] - n[0] * v[2], n[0] * v[1] - n[1] * v[0] };
            float y[3] = { x[1] * n[2] - x[2] * n[1], x[2] * n[0] - x[0] * n[2], x[0] * n[1] - x[1] * n[0] };
            float l = sqrtf(dot3(y, y));                                /* 0x43cf90: to the speed */
            if (l > 0) for (int k = 0; k < 3; k++) v[k] = y[k] * speed / l;
            last = t;
        } else {
            sum += hh;
            if (mx <= hh) mx = hh;
            if (air <= tau) air = tau;
            acc = -9.8f;
        }
    }
    if (pmax) *pmax = mx;
    if (pair) *pair = air;
    return sum / (float)i;
}

/* Player::IsJumpCamAvailable 0x5f1ee0: from the FuturePoint closest to the
   car, the run that first reaches 75 m (0x7a61dc) ahead, more than 4
   points long -> player +0x644 / +0x648 */
static int jump_cam_available(const JcCar *c, const AiRacer *fp)
{
    J.first = J.last = -1;
    int n = airacer_fp_count(fp);
    if (!fp || n <= 0) return 0;
    int c0 = airacer_fp_closest(fp, c->pos);
    if (c0 < 0) return 0;
    float d0 = airacer_fp_dist(fp, c0);
    for (int i = c0; i < n; i++) {
        if (75.0f <= airacer_fp_dist(fp, i) - d0) {
            if (4 < i + (1 - c0)) { J.first = c0; J.last = i; return 1; }
            break;
        }
    }
    return 0;
}

/* 0x403df0: do the 2D segments a-b and c-d cross */
static int seg2_cross(const float *a, const float *b, const float *c, const float *d)
{
    float r[2] = { b[0] - a[0], b[1] - a[1] }, s[2] = { d[0] - c[0], d[1] - c[1] };
    float den = r[0] * s[1] - r[1] * s[0];
    if (fabsf(den) <= 0.001f) return 0;                                 /* 0x78435c */
    float q[2] = { c[0] - a[0], c[1] - a[1] };
    float t = (q[0] * s[1] - q[1] * s[0]) / den, u = (q[0] * r[1] - q[1] * r[0]) / den;
    return 0.0f <= t && t <= 1.0f && 0.0f <= u && u <= 1.0f;
}
/* LineSegmentIsInBounds 0x41ce90: a -> b crosses every point's lane-0 width
   (limits +0x18 / +0x2c) from i0 to i1 */
static int segment_in_bounds(const AiRacer *fp, const float a[3], const float b[3], int i0, int i1)
{
    if (i0 < 0 || i1 < 0 || i0 > i1) return 0;
    for (int i = i0; i <= i1; i++) {
        float lo[3], hi[3];
        if (!airacer_fp_pos(fp, i, 3, 0, lo) || !airacer_fp_pos(fp, i, 4, 0, hi)) return 0;
        if (!seg2_cross(lo, hi, a, b)) return 0;
    }
    return 1;
}

/* the line the Cubic fallback looks along: the car's model box centre */
static void los_start(const JcCar *c, float o[3])
{
    for (int i = 0; i < 3; i++)
        o[i] = c->pos[i] + c->box_centre[0] * c->rot[i] + c->box_centre[1] * c->rot[3 + i] + c->box_centre[2] * c->rot[6 + i];
}

/* ChangeCameraMover 0x5f1fd0 case 0xf: the eye 2 m (0x7a5520) inside the road
   edge at the landing point (+0x648, or 2 points before it), on the side
   less ahead along the car's velocity -- the other side when the line from
   the car to it leaves the road; the second shot's eye on the other side 2
   points after the take-off; 1 m up, kept 1 m over the ground. Then the
   TrackCarCameraMover unless the world blocks the car -> eye line. */
static void mover_0xf(const JcCar *c, const JcEnv *e, const AiRacer *fp, WColl *w)
{
    J.cam = 0;
    if (J.first < 0 || J.last < 0) return;
    int idx = J.last;
    if (4 < J.last - J.first) idx = J.last - 2;
    /* CameraDataItems +0xc / +0x10 (0x5f2369): with a race route the car's
       race fraction now (car +0x8f8) and a fresh locator's (0x5ded90,
       +0x20) at the landing point (FuturePoint 0x4122c0 at the car's lane
       offset 0x412470); without one 0 / 1 */
    J.f0 = 0.0f; J.f1 = 1.0f;
    float L[3];
    if (e->rp && airacer_fp_pos(fp, idx, 0, 0.0f, L)) {
        RpLoc l;
        rploc_init(&l);
        racepath_locate(e->rp, &l, L);
        J.f0 = c->race_frac; J.f1 = l.frac;
    }
    float A[3], B[3], SA[3], SB[3];
    if (!airacer_fp_pos(fp, idx, 1, 2.0f, A) || !airacer_fp_pos(fp, idx, 2, 2.0f, B)) return;
    if (!airacer_fp_pos(fp, J.first + 2, 2, 2.0f, SB) || !airacer_fp_pos(fp, J.first + 2, 1, 2.0f, SA)) return;
    float da[3] = { A[0] - c->pos[0], A[1] - c->pos[1], A[2] - c->pos[2] };
    float db[3] = { B[0] - c->pos[0], B[1] - c->pos[1], B[2] - c->pos[2] };
    int right = dot3(da, c->vel) <= dot3(db, c->vel);
    if (!segment_in_bounds(fp, c->pos, right ? A : B, J.first, idx - 1)) right = !right;
    memcpy(J.eye, right ? A : B, sizeof J.eye);
    memcpy(J.second, right ? SB : SA, sizeof J.second);
    J.side = right ? -0.4f : 0.4f;
    J.eye[2] += 1.0f; J.second[2] += 1.0f;
    float g = ground_at(w, J.eye, NULL);
    if (J.eye[2] < g) J.eye[2] = g + 1.0f;
    float s[3];
    los_start(c, s);
    J.cam = !chasecam_seg_blocked(w, s, J.eye);
    printf("jumpcam: zone 6 shot, eye (%.1f %.1f %.1f) %s side, %.1f m from the car%s%c",
           J.eye[0], J.eye[1], J.eye[2], right ? "right" : "left", sqrtf(da[0] * da[0] + da[1] * da[1]),
           J.cam ? "" : " -- blocked, the drive camera stays", 10);
}
/* case 0x10: the second shot from +0x290, at least the car's height and 1 m
   over the ground there, the framing turned to the other side */
static void mover_0x10(const JcCar *c, const JcEnv *e, WColl *w)
{
    if (e->rp) J.f0 = c->race_frac;                             /* +0xc again, +0x10 kept (0x5f2b3b) */
    if (J.second[2] < c->pos[2]) J.second[2] = c->pos[2];
    float g = ground_at(w, J.second, NULL);
    if (J.second[2] < g) J.second[2] = g + 1.0f;
    memcpy(J.eye, J.second, sizeof J.eye);
    J.side = -J.side;
    float s[3];
    los_start(c, s);
    J.cam = !chasecam_seg_blocked(w, s, J.eye);
    printf("jumpcam: zone 7 shot, eye (%.1f %.1f %.1f)%s%c", J.eye[0], J.eye[1], J.eye[2],
           J.cam ? "" : " -- blocked, the drive camera stays", 10);
}

void (*jumpcam_on_clear)(void);
static void clear_zone(void)                                    /* Player::ClearZone 0x5ffe70 */
{
    if (!J.zone) return;
    /* GC ClearZone 802147cc, zones 1 / 2 / 6 / 7: RestoreCameraMover, then the
       player car's CarDamage SetDamageToZero + SetVisibleDamageEnabled(0) */
    if (jumpcam_on_clear) jumpcam_on_clear();
    printf("jumpcam: zone %d over (%.2f s world time)%c", J.zone, J.now - J.stamp[J.zone], 10);
    J.cam = 0;                                                  /* RestoreCameraMover */
    set_scale(1.0f, 0.0f);
    J.prev = J.zone;
    J.zone = 0;
}

/* the zone gate FinishMovement and RequestZone share: not a drift race, the
   zone's 5 s cool-down (0x7a550c), racing, one player, the option on */
static int zone_ok(const JcEnv *e, int zone)
{
    if (e->drift || !e->racing || e->two_players || !e->option) return 0;
    if (J.stamp[zone] >= 0 && J.now - J.stamp[zone] < 5.0f) return 0;
    return 1;
}

static void enter(int zone, const JcCar *c, const JcEnv *e, const AiRacer *fp, WColl *w)
{
    J.stamp[zone] = J.now;
    set_scale(0.4f, 8.0f);                                      /* 0x7a5524, 0x7a54f8 */
    if (zone == 6) mover_0xf(c, e, fp, w); else mover_0x10(c, e, w);
    J.first = J.last = -1;
    J.zone = zone;
    printf("jumpcam: zone %d, slow motion 0.4 for 8 s%c", zone, 10);
}

void jumpcam_reset(void)
{
    if (J.zone) clear_zone();
    set_scale(1.0f, 0.0f);
    for (int i = 0; i < 8; i++) J.stamp[i] = -1;
    J.zone = J.prev = 0; J.cam = 0; J.first = J.last = -1;
    J.last_dampen = 0;
}

int jumpcam_zone(void) { return J.zone; }

int jumpcam_crash(const JcCar *c, const JcEnv *e, const AiRacer *fp, WColl *w, float j)
{
    if (e->drift || j < 30.0f) return 0;                        /* 0x7a1864 */
    if (J.zone == 6 || J.zone == 7) return 0;
    if (!jump_cam_available(c, fp)) return 0;
    if (!zone_ok(e, 6)) return 0;                               /* RequestZone(6, 0, 1, 1) 0x6022e0 */
    printf("jumpcam: crash (impulse %.1f)%c", j, 10);
    enter(6, c, e, fp, w);
    return 1;
}

void jumpcam_step(const JcCar *c, const JcEnv *e, const AiRacer *fp, WColl *w)
{
    if (J.left > 0.0f) {                                        /* World::GetTimestep 0x5ea360 (real time) */
        J.left -= JC_STEP;
        if (J.left <= 0.0f) {
            J.left = 0.0f;
            set_scale(1.0f, 0.0f);
            clear_zone();                                       /* the end callback 0x6022d0: ClearZone(1) */
        }
    }
    J.now += JC_STEP * J.scale;
    J.frame++;
    if (c->dampen == 1 && J.last_dampen != 1) J.t548 = J.now;
    J.last_dampen = c->dampen;
    if (J.zone == 6 || J.zone == 7) {                           /* back on the ground */
        if (c->dampen == 1) {
            if (c->wheels_down == 4 && J.now - J.t548 > 0.5f) clear_zone();   /* 0x7a5608 */
        } else if (c->wheels_down > 0) clear_zone();
    }
    if ((J.frame + 3) & 7) return;
    float x = e->option ? 127.0f : 0.0f;
    if (J.zone != 6 && J.zone != 7 && c->wheels_down == 0 && !e->wrong_way && graph(40, 20, x) < c->speed) {
        float a = 0, b = 0;
        average_air(c, w, graph(3, 0.6f, x), &a, &b, 1);       /* 0x890150 */
        J.t544 = J.now + (b < 1.0f ? 5.0f : 0.5f * b);         /* 0x7843e8 / 0x7a5614 */
        if (graph(2, 0.5f, x) < a && graph(1.5f, 0.3f, x) < b && jump_cam_available(c, fp)) {   /* 0x89e790 / 0x89e824 */
            if (zone_ok(e, 6)) {
                printf("jumpcam: jump, %.1f m high, %.2f s in the air ahead, %.1f m/s%c", a, b, c->speed, 10);
                enter(6, c, e, fp, w);
            } else J.first = J.last = -1;
        }
    }
    if (J.zone == 6) {
        if (c->vel[2] < 0.0f && J.second[2] < c->pos[2]) J.second[2] = c->pos[2];
        if (c->dampen == 1) {
            if (J.now - J.t548 > 3.0f) {                        /* 0x7a560c */
                clear_zone();
                if (zone_ok(e, 7)) enter(7, c, e, fp, w);
            }
        } else if (J.t544 < J.now) {
            clear_zone();
            float mx = 0, air = 0;
            average_air(c, w, 1.0f, &mx, &air, 1);
            if (0.5f < air && zone_ok(e, 7)) enter(7, c, e, fp, w);
        }
    }
}

/* TrackCarCameraMover::Update 0x44c350 with the zone's CameraDataItems: the
   eye still; the lens from the fov spline (items +0x8 = 0: ftol(deg x
   182.04), 0x44c6fe) at 31 x t, t = (car +0x8f8 - items +0xc) / (+0x10 -
   +0xc) clamped 0..1 (NaN -> 0): zone 6 all 20 degrees, zone 7 20 - 0.25 k;
   the aim the car raised by 0.2 and moved sideways by +-0.4 of the
   half-height of the view at the car's distance; up +z (the bank spline's
   points are 0); near 0.5 (0x7870e8). The target spring (CalcSpringForce
   0x444cf0, items +0x64..+0x74 all 0) stays at rest. */
int jumpcam_camera(const JcCar *c, float dt, CcOut *out)
{
    (void)dt;
    chasecam_set_world(J.zone, J.scale);
    if (!(J.zone == 6 || J.zone == 7) || !J.cam) return 0;
    float d[3] = { J.eye[0] - c->pos[0], J.eye[1] - c->pos[1], J.eye[2] - c->pos[2] };
    float dist = sqrtf(dot3(d, d));
    if (dist < 1.0f) dist = 1.0f;
    float deg = 20.0f;                                          /* 0x8020b8 */
    if (J.zone == 7) {
        float t = (c->race_frac - J.f0) / (J.f1 - J.f0);
        if (!(t > 0.0f)) t = 0.0f;                              /* also NaN: (t < 0) == unordered keeps 0 */
        if (t > 1.0f) t = 1.0f;
        deg = 20.0f - 0.25f * (t * 31.0f);                      /* 0x8020bc, 0x787d90 */
    }
    unsigned short fov = (unsigned short)(int)(deg * 182.04445f);   /* 0x787d8c */
    float half = (float)(fov >> 1) * (6.2831853f / 65536.0f);
    float tn = sinf(half) / cosf(half);
    float tgt[3] = { c->pos[0], c->pos[1], c->pos[2] + tn * 0.2f * dist };   /* +0x84 */
    float dir[3] = { d[0] / dist, d[1] / dist, d[2] / dist };
    float rt[3] = { dir[1], -dir[0], 0 };                       /* dir x (0,0,1) */
    float sd = tn * J.side * dist;                              /* +0x88 */
    for (int i = 0; i < 3; i++) tgt[i] += rt[i] * sd;
    float V[4][4];
    chasecam_lookat(J.eye, tgt, V);
    chasecam_out(V, fov, out);
    out->znear = 0.5f;
    return 1;
}
