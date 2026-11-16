/* n2sim.c — the retail NFSU2 car simulation, ported from SPEED2.EXE v1.2.
 * See n2sim.h. Each function names its original address; fields name their
 * original object offsets. Race-mode features that cannot happen in free roam
 * or a circuit/sprint race (drag launch, drift-mode yaw assist,
 * nitrous, the AI catch-up helper, network remote cars) are left out and
 * marked where they hook in. */
#include "n2sim.h"
#include "wcoll.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tables copied from the executable ---------------------------------- */
typedef struct { int n; float x0, x1, inv; const float *d; } Curve;   /* 0x5764d0 */

static const float LAT_0[6]  = { 0,0,0,0,0,0 };                       /* 0x7fc968 */
static const float LAT_2[6]  = { 0, 1.2f, 2.3f, 3.0f, 3.0f, 2.8f };    /* 0x7fc744 */
static const float LAT_4[6]  = { 0, 1.7f, 3.2f, 4.3f, 5.1f, 5.2f };
static const float LAT_6[6]  = { 0, 1.8f, 3.5f, 4.9f, 5.8f, 6.1f };
static const float LAT_8[6]  = { 0, 1.83f, 3.6f, 5.0f, 5.96f, 6.4f };
static const float LAT_10[6] = { 0, 1.86f, 3.7f, 5.1f, 6.13f, 6.7f };
static const float LAT_12[6] = { 0, 1.9f, 3.8f, 5.2f, 6.3f, 7.1f };    /* 0x7fc7bc */
static const Curve LAT[7] = {                                         /* 0x7fc7d4 */
    {6,0,10,0.5f,LAT_0},{6,0,10,0.5f,LAT_2},{6,0,10,0.5f,LAT_4},{6,0,10,0.5f,LAT_6},
    {6,0,10,0.5f,LAT_8},{6,0,10,0.5f,LAT_10},{6,0,10,0.5f,LAT_12} };
static const float ANGDRAG_D[6] = { 0.05f,0.05f,0.25f,0.7f,2,5 };      /* 0x7fc6b0 */
static const Curve ANGDRAG = { 6, 0, 10, 0.5f, ANGDRAG_D };            /* 0x7fc92c */
static const float SETTLE_D[8] = { 1.5f,1,0.6f,0.22f,0.06f,0.03f,0.015f,0 };
static const Curve SETTLE = { 8, 0, 1, 7, SETTLE_D };                  /* 0x7fc940 */
static const float LOWSPIN_G[6] = { 0,0, 0.4f,0, 2,20 };               /* 0x7fc8f8 -> 0x86b4a0 */
/* retail surface table 0x800ee0 (wcoll.c): grip +0x24, slide +0x28,
   rolling +0x2c, drag +0x30 */
#define SURF(i) (&wcoll_surfaces[i])
static const float HB_WHEEL[4] = { 0, 0, 1, 1 };                      /* 0x7fc074 */
static const float CLUTCH_RATE[4] = { -3, 0, 1.5f, 0 };               /* 0x7fc298 */
static const float TACH_FALL[8] = { 2.5f,2.5f,2.5f,2.5f,15,15,15,15 }; /* 0x7a1a2c */

#define PI_F      3.14159265f
#define U16RAD    9.58738019e-05f  /* 0x784240 */
#define RAD2U16   10430.378f
#define RPM       9.549304f        /* 0x7a1d28 rad/s -> rpm (float 0x4118c9e0) */
#define RPM2RAD   0.104719669f     /* 0x7a1d38 (float 0x3dd67750) */
#define MPH       2.237f           /* 0x78426c */
#define HMIN      0.0044f          /* 0x7fc070 */

static float curve(const Curve *c, float x)
{
    float t = (x - c->x0) * c->inv;
    int i = (int)t;
    if (i < 0 || t < 0) return c->d[0];
    if (i >= c->n - 1) return c->d[c->n - 1];
    float f = t - (float)i;
    return (1.0f - f) * c->d[i] + f * c->d[i + 1];
}
static float graph(const float *p, int n, float x)                     /* 0x576790 */
{
    if (n > 1) {
        if (x <= p[0]) return p[1];
        if (p[2*n-2] <= x) return p[2*n-1];
        for (int i = 0; i < n - 1; i++)
            if (p[2*i] <= x && x < p[2*i+2]) {
                float dx = p[2*i+2] - p[2*i];
                if (fabsf(dx) <= 1e-6f) return (p[2*i+3] - p[2*i+1]) * 0.5f + p[2*i+1];
                return (x - p[2*i]) / dx * (p[2*i+3] - p[2*i+1]) + p[2*i+1];
            }
    }
    return p[1];
}
/* 0x43c350: sine through the game's 16-bit angle */
/* 0x43c2b0: the game's sine of a 16-bit angle, a 9th-order Taylor series on
   [-pi/2, pi/2) (not sinf: at 90 degrees it gives 1.0000035, which the
   steering and wheel matrices carry) */
static float sin_u16(unsigned short u)
{
    float x = (float)u * U16RAD, sg = 1.0f;
    if (!(x < 4.71238899f)) x -= 6.28318548f;                          /* 0x786da0 / 0x786d9c */
    else if (!(x < 1.57079637f)) { x -= 3.14159274f; sg = -1.0f; }     /* 0x784254 / 0x786d98 */
    float x2 = x * x, x3 = x2 * x, x5 = x3 * x2, x7 = x5 * x2;
    float a = x - x3 * 0.166666672f;                                    /* 0x786d94 */
    a = a + x5 * 0.00833333377f;                                        /* 0x786d90 */
    a = a - x7 * 0.000198412701f;                                       /* 0x786d8c */
    a = a + x7 * x2 * 2.75573188e-06f;                                  /* 0x786d88 */
    return a * sg;
}
/* 0x43c350: sine of radians through the 16-bit angle (truncated, 0x75bed4) */
static float sin16(float x) { return sin_u16((unsigned short)(int)(x * RAD2U16)); }
/* bFastATan 0x43c570: the angle of (x, y) in 16-bit units from the 256-entry
   octant table at 0x7eacb0 (x 32, the index truncated: 0x75bed4) */
static const unsigned char FAST_ATAN[256] = {
    0, 1, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 17, 18, 19, 20, 22, 23, 24, 26, 27, 28, 29, 31, 32, 33, 34, 36, 37, 38, 39,
    41, 42, 43, 44, 46, 47, 48, 49, 51, 52, 53, 54, 56, 57, 58, 59, 61, 62, 63, 64, 65, 67, 68, 69, 70, 72, 73, 74, 75, 76,
    78, 79, 80, 81, 82, 84, 85, 86, 87, 88, 89, 91, 92, 93, 94, 95, 96, 98, 99, 100, 101, 102, 103, 105, 106, 107, 108, 109,
    110, 111, 113, 114, 115, 116, 117, 118, 119, 120, 121, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 136,
    137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
    161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 176, 177, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 187, 187, 188, 189, 190, 191, 192, 193, 194, 195, 195, 196, 197, 198, 199, 200, 201, 201, 202, 203, 204,
    205, 206, 206, 207, 208, 209, 210, 210, 211, 212, 213, 214, 214, 215, 216, 217, 218, 218, 219, 220, 221, 222, 222, 223,
    224, 225, 225, 226, 227, 228, 228, 229, 230, 231, 231, 232, 233, 233, 234, 235, 236, 236, 237, 238, 238, 239, 240, 241,
    241, 242, 243, 243, 244, 245, 245, 246, 247, 247, 248, 249, 249, 250, 251, 251, 252, 253, 253, 254, 255, 255
};
static unsigned short fast_atan(float x, float y)
{
    int q = 0;
    if (x < 0.0f) { q = 1; x = -x; }
    if (y < 0.0f) { q ^= 3; y = -y; }
    unsigned a;
    if (y < x) a = (unsigned)FAST_ATAN[(int)(y / x * 256.0f) & 0xff] << 5;
    else if (x < y) a = 0x4000u - ((unsigned)FAST_ATAN[(int)(x / y * 256.0f) & 0xff] << 5);
    else a = y == 0.0f ? 0u : 0x2000u;
    if (q == 0) return (unsigned short)a;
    if (q == 3) return (unsigned short)(0u - a);
    if (q == 1) return (unsigned short)(0x8000u - a);
    return (unsigned short)(a + 0x8000u);
}
/* 0x43c570 + wrap to (-pi, pi] */
static float atan2w(float x, float y)
{
    float a = (float)fast_atan(x, y) * U16RAD;
    if (a > PI_F) a -= 2*PI_F;
    return a;
}
static float len3(const float *v) { return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
static void cross(float *o, const float *a, const float *b)          /* 0x43d1e0 */
{
    float x = a[1]*b[2] - a[2]*b[1], y = a[2]*b[0] - a[0]*b[2], z = a[0]*b[1] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
/* row vector times the 3x3 part of a 4x4 row-major matrix */
static void vmul(float *o, const float *v, const float *m)
{
    float x = v[0]*m[0] + v[1]*m[4] + v[2]*m[8];
    float y = v[0]*m[1] + v[1]*m[5] + v[2]*m[9];
    float z = v[0]*m[2] + v[1]*m[6] + v[2]*m[10];
    o[0] = x; o[1] = y; o[2] = z;
}
/* row vector times the transpose (world -> body) */
static void vmulT(float *o, const float *v, const float *m)
{
    float x = v[0]*m[0] + v[1]*m[1] + v[2]*m[2];
    float y = v[0]*m[4] + v[1]*m[5] + v[2]*m[6];
    float z = v[0]*m[8] + v[1]*m[9] + v[2]*m[10];
    o[0] = x; o[1] = y; o[2] = z;
}
static void mmul(float *o, const float *a, const float *b)             /* 0x4013f0 o = a*b */
{
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[r*4+c] = a[r*4]*b[c] + a[r*4+1]*b[4+c] + a[r*4+2]*b[8+c] + a[r*4+3]*b[12+c];
    memcpy(o, t, sizeof t);
}
/* The retail operation order. SPEED2.EXE runs its physics with the x87 at
   single precision (Direct3D 9 sets it at CreateDevice 0x5b9b95, flags 0x50
   without FPU_PRESERVE), so every add / multiply rounds to a float, like
   this C code; only the association differs. These helpers keep it, so that
   the port reproduces the original bit for bit (harness E:/re-tools/emu/acc). */
/* 0x441000: o = v * M + M[12..14] (row vector) */
static void xf441(float *o, const float *v, const float *m)
{
    float x = ((v[2]*m[8] + v[1]*m[4]) + v[0]*m[0]) + m[12];
    float y = ((v[2]*m[9] + v[0]*m[1]) + v[1]*m[5]) + m[13];
    float z = ((v[2]*m[10] + v[0]*m[2]) + v[1]*m[6]) + m[14];
    o[0] = x; o[1] = y; o[2] = z;
}
/* 0x441000 on the rotation only (translation row 0, 1) */
static void xf441R(float *o, const float *v, const float *m)
{
    float x = ((v[2]*m[8] + v[1]*m[4]) + v[0]*m[0]) + 0.0f;
    float y = ((v[2]*m[9] + v[0]*m[1]) + v[1]*m[5]) + 0.0f;
    float z = ((v[2]*m[10] + v[0]*m[2]) + v[1]*m[6]) + 0.0f;
    o[0] = x; o[1] = y; o[2] = z;
}
/* 0x441000 on the transposed rotation (0x43c9d0 with the translation column
   zeroed): o = v * M^T */
static void xf441T(float *o, const float *v, const float *m)
{
    float x = (v[2]*m[2] + v[1]*m[1]) + v[0]*m[0];
    float y = (v[2]*m[6] + v[0]*m[4]) + v[1]*m[5];
    float z = (v[2]*m[10] + v[0]*m[8]) + v[1]*m[9];
    o[0] = x + 0.0f; o[1] = y + 0.0f; o[2] = z + 0.0f;
}
/* 0x4013f0: o = a * b (4x4 row-major), each element summed in the
   compiled order */
static const unsigned char MM_K[16][4] = {
    {0,3,2,1},{3,1,2,0},{2,0,3,1},{3,2,0,1}, {2,3,1,0},{2,0,1,3},{3,2,1,0},{3,2,0,1},
    {2,3,1,0},{2,0,1,3},{3,2,1,0},{3,2,0,1}, {2,3,1,0},{2,0,1,3},{3,2,1,0},{3,2,0,1} };
static void mmul4013(float *o, const float *a, const float *b)
{
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            const unsigned char *k = MM_K[r*4+c];
            float s = a[r*4+k[0]]*b[k[0]*4+c] + a[r*4+k[1]]*b[k[1]*4+c];
            s = s + a[r*4+k[2]]*b[k[2]*4+c];
            t[r*4+c] = s + a[r*4+k[3]]*b[k[3]*4+c];
        }
    memcpy(o, t, sizeof t);
}
static void transpose4(float *o, const float *m)                       /* 0x43c9d0 */
{
    float t[16];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) t[c*4+r] = m[r*4+c];
    memcpy(o, t, sizeof t);
}
/* |v| summed z, y, x */
static float len3r(const float *v) { return sqrtf((v[2]*v[2] + v[1]*v[1]) + v[0]*v[0]); }
/* |v| summed x, z, y */
static float len3xzy(const float *v) { return sqrtf((v[0]*v[0] + v[2]*v[2]) + v[1]*v[1]); }
/* |v| summed y, z, x */
static float len3yzx(const float *v) { return sqrtf((v[1]*v[1] + v[2]*v[2]) + v[0]*v[0]); }
static void ident(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void inv3(float *o, const float *m)                              /* 0x43c7c0, no translation */
{
    float c0 = m[5]*m[10] - m[9]*m[6], c1 = m[10]*m[4] - m[8]*m[6], c2 = m[9]*m[4] - m[8]*m[5];
    float r = 1.0f / (c2*m[2] + (c0*m[0] - c1*m[1]));
    ident(o);
    o[0] = c0*r;  o[1] = -(m[10]*m[1] - m[9]*m[2])*r; o[2] = (m[6]*m[1] - m[5]*m[2])*r;
    o[4] = -c1*r; o[5] = (m[10]*m[0] - m[8]*m[2])*r;  o[6] = -(m[6]*m[0] - m[4]*m[2])*r;
    o[8] = c2*r;  o[9] = -(m[9]*m[0] - m[8]*m[1])*r;  o[10] = (m[5]*m[0] - m[4]*m[1])*r;
}
static void normalize43cf10(float *o, const float *v)                  /* 0x43cf10: v * (1 / |v|) */
{
    float l = len3(v);
    if (l == 0) { o[0] = 1; o[1] = o[2] = 0; return; }
    float r = 1.0f / l, z = v[2];
    o[0] = r * v[0]; o[1] = r * v[1]; o[2] = z * r;
}
static void orthonormalize(float *m)                                   /* 0x5bb0d0 */
{
    float x[3], y[3], z[3];
    normalize43cf10(x, m);
    cross(z, x, m + 4);
    normalize43cf10(z, z);
    cross(y, z, x);
    memset(m, 0, 64);
    memcpy(m, x, 12); memcpy(m + 4, y, 12); memcpy(m + 8, z, 12); m[15] = 1;
}

/* ---- objects ------------------------------------------------------------ */
typedef struct {                       /* RigidBody, 0x210 B, vtable 0x7a20c4 */
    float pos[4];                      /* +0x20 state (32 floats) */
    float R[16];                       /* +0x30 */
    float p[4];                        /* +0x70 linear momentum */
    float L[4];                        /* +0x80 angular momentum */
    float settle, spad[3];             /* +0x90 */
    float v[3], acc_g[3], w[3];        /* +0xa0 +0xb0 +0xc0 */
    float world[16];                   /* +0xd0 */
    float F[3], T[3];                  /* +0x110 +0x120 accumulators */
    float lastF[3], lastT[3];          /* +0x130 +0x140 */
    int   damp_on, angdrag_on, yawI_on, boost;      /* +0x150..0x15c */
    float gravity, mass, invmass;      /* +0x160..0x168 */
    float Iinv[16], Iinv2[16];         /* +0x170 +0x1b0 */
    float com[3];                      /* +0x1f0 */
    float yaw_clamp, top_speed, power; /* +0x200 +0x204 +0x208 */
} Body;

typedef struct {                       /* Wheel, 0x1a0 B, vtable 0x7a20e8 */
    const float *pr, *tyre, *brk;      /* +0x14 +0x18 +0x1c records */
    int   idx;
    float angle, w, I;                 /* +0x24 state, +0x2c inertia */
    float spin[16], world[16], local[16]; /* +0x30 +0x70 +0xb0 */
    float vel[3];                      /* +0xf0 contact velocity, world */
    float slipv[2];                    /* +0x100 */
    float torque;                      /* +0x108 */
    int   locked, dir, surf;           /* +0x10c +0x110 +0x114 */
    float slip;                        /* +0x118 R*w - v */
    int   contact;                     /* +0x11c */
    float tsmooth, Fx, Fy;             /* +0x120 +0x124 +0x128 */
    float force[3], cpt[3], n[3];      /* +0x130 +0x140 +0x150 */
    int   sliding;                     /* +0x160 */
    float load, surfval, slip2, sratio, wobble;     /* +0x164..0x174 */
    float brake, hbrake, grip, power, grip2, hoff, kx, ky;  /* +0x178..0x194 */
} Wheel;

typedef struct Susp {                  /* Suspension, 0xc0 B, vtable 0x7a210c */
    const float *rec;                  /* +0x14 {prog, spring, bump, rebound, arb, max, min, vis} */
    struct Susp *partner;              /* +0x18 */
    float x, xv, force;                /* +0x1c +0x20 +0x24 */
    float mnow[3];                     /* +0x30 */
    float m[16];                       /* +0x40 */
    int   contact;                     /* +0x80 */
    float preload;                     /* +0x88 */
    float mount[3];                    /* +0x90 */
    Wheel *wheel; int steered, idx;    /* +0xa0 +0xa8 +0xac */
} Susp;

typedef struct {                       /* Engine, 0xb0 B, vtable 0x7a235c */
    const float *rec;                  /* +0x18: idle, redline, max, inertia, torque[9], boost[9], brake[3] */
    Curve torque, brake;               /* +0x24 +0x38 */
    float w;                           /* +0x14 rad/s */
    int   flag74;                      /* +0x74 1.05x torque (car option) */
    float thr, load, thrmul;           /* +0x78 +0x7c +0x80 */
    int   limiter;                     /* +0x88 */
    float heat;                        /* +0x8c over-rev heat (drag): -1 off, 2 blown */
    float power;                       /* +0x90 */
    Curve nos;                         /* +0x60 nitrous torque, 0x7fbf38 over idle..redline */
} Engine;

typedef struct {                       /* DriveTrain, 0xf4 B, vtable 0x7a2508 */
    const float *gb;                   /* +0x18 rec+0x2c0: launch, ?, final F, final R, rear, clutch, n, rate, ratios[] */
    float engage;                      /* +0x14 state */
    int   target, gear, lastgear, state, shifting; /* +0x38 +0x3c +0x40 +0x44 +0x48 */
    float drivew;                      /* +0x4c driveline speed at the engine */
    float semi_t; int req;             /* +0x50 +0x54 */
    float prevdiff, shift_t;           /* +0x58 +0x5c */
    int   mode;                        /* +0x60 the controller's transmission (0x407280): 3 = the clutch pedal drives
                                          state/engage (0x594910), anything else the clutch FSM */
    float disrpm;                      /* +0x6c engine rpm while engaged (clutch-pedal mode: the upshift grade's rpm) */
    float sum;                         /* +0x70 */
    int   neutral;                     /* +0x74 */
    float rnd[3];                      /* +0x78 +0x7c +0x80 */
    float up[10], down[10];            /* +0x84 +0xa8 */
    float shift_t0;                    /* +0x64 the graded shift's boost start */
    float q;                           /* +0x68 the last upshift's grade (drag, 0x594f20) */
    float rndrange[6];                 /* +0xcc..0xe0 */
    float win;                         /* +0xe4 shift window scale (drag: by difficulty) */
    float rate, rear;                  /* +0xe8 +0xec */
    int   klass;                       /* +0xf0 the car's performance class */
} Drive;

typedef struct {                       /* Aerodynamics, 0x40 B */
    const float *rec;                  /* P+0x260: drag, downforce F, downforce R, preload */
    float speedscale, scale, thr, ground, on, surfdrag;
} Aero;

typedef struct {                       /* time-windowed average, 0x576aa0 */
    int n, count, idx, oldest;
    float window, sum, avg;
    float val[66], t[66];
} Avg;

typedef struct {                       /* Melov8r, 0x250 B, vtable 0x7a2560 */
    float spin[4];                     /* +0x20 state, wheel rad/s */
    float comp[4], compv[4], load[4];  /* +0x30 +0x40 +0x50 */
    int   contact[4];                  /* +0x60 */
    float tf[4][2];                    /* +0x70 tyre force, body frame, |.| <= 50 */
    float sv[4][2];                    /* +0x90 slip speed, lateral speed */
    int   sliding[4], locked[4];       /* +0xb0 +0xb4 */
    float over[4];                     /* +0xc4 travel past the bump stop */
    float n[4][3];                     /* +0xe0 ground normals */
    float rpm; int gear;               /* +0x120 +0x124 */
    float thr, brake;                  /* +0x128 +0x12c */
    unsigned short steer; int req;     /* +0x130 +0x134 */
    float mult, gripm, perfk;          /* +0x13c +0x140 +0x144 */
    float sload[4];                    /* +0x148 static wheel loads */
    Curve torque, ebrake, nos;         /* +0x158 +0x16c +0x180 */
    float up[10], down[10];            /* +0x19c +0x1c0 shift rpm */
    float hyd[4];                      /* +0x1e8 hydraulic lift, copied each frame */
    float mount[4][3];                 /* +0x210 mount points, world */
    int   surf[4];
    float angle[4];                    /* carstate +0x300, drawn wheel angle */
    int   rest; float rest_t;          /* mover +0x5c +0x58 */
} Mello;

/* 0x574830..0x574f40: a Hermite blend from the value it holds (p0, t0) to
   a target (p1, t1) over `dur` seconds (0x30 bytes; the 2-D form is two of
   them). Setting p0 or p1 to a new value restarts it (state 2); with
   `loop` 0 it then runs once (state 1) and stops on the target (0). */
typedef struct { float p0, t0, p1, t1, a, b, c, d, t, dur, end; short state, loop; } Blend1;
/* mover 4's state (0x5a9030, 0x4c bytes) */
#define FAR_MAXPTS 20                  /* the driver plan's 20 points */
typedef struct {
    float P[FAR_MAXPTS + 2][4];        /* +0x14 the spline (0x57a300 type 2): P[1..n], ends P[0] / P[n+1] */
    int   n;                           /* spline +0x1e */
    int   dirty;                       /* spline +0x18 */
    float t;                           /* +0x0c seconds on this path */
    float k;                           /* +0x10 spline points per metre */
    float s0, v0, a;                   /* +0x1c +0x20 +0x24: s(t) = s0 + v0 t + a t^2 / 2 */
    Blend1 lat;                        /* +0x18 the offset from the path, blended out */
    Blend1 hx, hy;                     /* +0x28 the velocity 0.4 s ahead (heading) */
    float ring[30], rsum, ravg;        /* +0x30 0x579f20(30): the path's lateral acceleration */
    unsigned char rcount, ridx;
    char  slide;                       /* +0x34 over the tyres' lateral grip (0x591950) */
    char  has_path;                    /* +0x48 */
    char  want, jump;                  /* port: the caller re-paths / sets the controller's +0x730 */
} FarMv;

struct N2Sim {
    unsigned char rec[N2_REC_SIZE];
    const float *P;                    /* rec + 0x110 */
    N2GroundFn ground; void *user;
    Body b; Wheel wh[4]; Susp su[4]; Engine e; Drive d; Aero a;
    float steer_m[2][16];              /* steering +0x10 / +0x50 */
    N2Input in, eff;                   /* raw, and after the reverse swap */
    float base;                        /* origin height above the ground at rest */
    unsigned short steer_u16;
    float gripscale, hbramp;           /* mover +0x6c +0x70 */
    /* player controller 0x41e2c0 (n2sim_input) */
    int   trans, gear_req, rflag, held; /* +0x2c0 mode, +0x1e4 gear request, +0x2f0 reverse wanted, +0x2b4 */
    float hbtimer, ctl_t;              /* +0x2bc, clock (0x86518c / 4000) */
    int   launch;                      /* +0x2cc the drag launch timer, 1/4000 s (0x577310) */
    float clutch;                      /* +0x2f4 the clutch pedal, 1 = released (engaged), 0 = floored */
    unsigned trkflags;                 /* the track record's flags (GLOBALB 0x34201 +0x94, *0x883dac) */
    int   sliding;                     /* steering +0x10 */
    Avg   st_in, st_flick;             /* steering +0x14 (0.55 s), +0x48 (0.15 s) */
    float st_max, st_prev;             /* steering +0x80 +0x84 */
    float in_dt, vtop;                 /* last frame step; 0x5b5930 top speed */
    double hit_time;                   /* carstate+0x3c -> +0x140, last crash */
    int   dampen;                      /* car +0x38 DampenMode: 0 normal, 1 crash, 2 collision */
    int   body_colls;                  /* car +0x3c -> +0x18: live Collisions (n2sim_set_body_collisions) */
    float draft;                       /* car +0x4dc (0x5ebd80, World::DoTimestep) */
    int   nos_spray;                   /* a purge press since the last n2sim_nos_spray */
    int   dampen_on; float dampen_t;   /* car +0x54c: time on four wheels since */
    float nos_cap, nos_left, nos_stock[2]; /* carstate +0x414 +0x41c; stock P+0x2a0 +0x2ac */
    double nos_start;                  /* carstate +0x420 */
    int   nos_burn, nos_stage, nos_pkg; /* burning, engine +0x84, fitted level */
    int   rest; float rest_t;          /* mover +0x60 +0x68 */
    float h;                           /* 0x7fc800 */
    double time;                       /* 0x86518c / 4000 */
    /* carstate */
    float cs_speed, cs_absv, cs_tach;  /* +0x3ec +0x3f0 +0x400 */
    float cs_bottom[4];     /* +0x330: bump-stop impulses this frame (0x5abb00, zeroed by 0x5b72d0) */
    float bottom_latch;     /* the largest +0x330 sum since n2sim_take_bottom (see there) */
    int   bottom_age;       /* steps since bottom_latch was set */
    int   cs_wheels, cs_contact[4], cs_surf[4];
    unsigned rng;
    struct { int valid, ok, surf; float x, y, z, n[3]; } gc[4];   /* ground, once per frame */
    int   ground_exact;                /* probe every substep (fast ground) */
    int   mover;                       /* car +0x28: N2_MOVER_PHYSICS / _MELLO */
    float catchup;                     /* car +0x468, 0x4172d0's multiplier */
    /* race type (n2sim_set_race): a drift-type race (!0x89e7d8 && !0x890108
       && (0x890114 || 0x89e7d9 || 0x890104)), the racer kind (2 AI) and the
       difficulty 0x89e7f4 */
    int   driftlike, race_ai, race_diff;
    /* a drag race (0x89e7d8 || 0x890108) and the race state (*0x890118, 4
       racing): the drag branches of the drivetrain, the engine heat */
    int   drag, race_state;
    int   streetx;                     /* a street X race (0x89e7e3 || 0x89010c) */
    unsigned short cs_head;            /* carstate +0x3cc */
    float cs_v[3], cs_w[3];            /* +0x30 +0xd0 */
    float cs_wz_prev, cs_yawacc;       /* car +0x538, car +0x128 (0x604740) */
    Mello m;
    float tm_x, tm_y, tm_head, tm_v;   /* TrafficMover +0xc +0x10 +0x14 +0x18 */
    /* NISMover: target +0x70, last target +0x80, heading (degrees) +0x90,
       place count +0x60, moved this frame +0x94; its chassis' front-spin
       scale +0x250 (1), spin add +0x254 (0) and torque +0x258 / +0x25c
       (0), set by the scene's event track (CAR_BRAKE_LOCK 0x598ed0,
       CAR_BURNOUT_ADDITIVE 0x598ee0, CAR_ENGINE_TORQUE 0x5a4080) */
    float nis_tgt[3], nis_prev[3], nis_head;
    int   nis_count, nis_moving;
    float nis_fspin, nis_spinadd, nis_tq[2];
    float tm_s540;                     /* car +0x540, 0x5f50a0's smoothed speed */
    FarMv far;                         /* mover 4 (0x5a9030) */
    /* hydraulics (0x5a2fd0): the car control's bounce +0x1ec[4] and extend
       +0x1fc[4] (set by the player's stick 0x407400 in main.c, or the
       scene's CAR_HYDRAULIC_BOUNCE / _EXTEND), the lift per wheel car
       +0x3b8[4], what it did chassis +0x48[4] (1 held, 2 up, 3 down) and
       the bounce-fired flags car +0x3c8[4]; wheel order FL FR RR RL */
    float hyd_bounce[4], hyd_extend[4], hyd_h[4];
    int   hyd_code[4];
    unsigned char hyd_hit[4];
    /* PhysicsMover dyno mode (+0x84, SetDynoMode 0x599200): the spot the
       body is held on (+0x90), its tethered virtual position (+0xe0) and
       velocity (+0xf0) */
    int   dyno;
    int   dyno_state;   /* RacingCar +0x1e0 (DynoManager) +4, the run's state 1..4 */
    float dy_spot[3], dy_pos[3], dy_vel[3];
};

#define PF(o) (s->P[(o)/4])            /* float at P+o */

static float frand(N2Sim *s)          /* 0x5ea500 -> 0x43c170, scale 1 */
{
    unsigned v = s->rng % 0x7fffffffu, t = s->rng ^ 0x1d872b41u, u = t >> 5 ^ t;
    s->rng = u << 27 ^ u ^ t;
    return (float)(int)v * 4.656612873077393e-10f;
}

/* ---- rigid body --------------------------------------------------------- */
static void body_mass(N2Sim *s)                                         /* 0x59b940 */
{
    Body *b = &s->b; const float *r = s->P + 0x110/4;
    float m[16];
    b->mass = r[0]; b->invmass = 1.0f / r[0];
    inv3(b->Iinv, r + 4);
    memcpy(m, r + 4, sizeof m); m[10] *= r[20];
    inv3(b->Iinv2, m);
}
static void body_force(Body *b, const float *f, const float *arm)       /* 0x59bb00 */
{
#ifdef OPENUG2_ACC_TRACE      /* the accuracy harness (E:/re-tools/emu/acc/port2.c): every body force */
    OPENUG2_ACC_TRACE(b, f, arm);
#endif
    float F[3] = { f[0], f[1], f[2] }, t[3], l = len3r(F);
    if (l > 1200.0f) { l = 1200.0f / l; F[0] *= l; F[1] *= l; F[2] *= l; }
    b->F[0] += F[0]; b->F[1] += F[1]; b->F[2] += F[2];
    cross(t, arm, f);                  /* the torque uses the force before the clamp */
    l = len3yzx(t);
    if (l > 100.0f) { l = 100.0f / l; t[0] *= l; t[1] *= l; t[2] *= l; }
    b->T[0] += t[0]; b->T[1] += t[1]; b->T[2] += t[2];
}
static void body_impulse(Body *b, const float *j, const float *arm)     /* 0x59bc60 */
{
    float J[3] = { j[0], j[1], j[2] }, t[3], l = len3yzx(J);
    if (l > 1000.0f) { l = 1000.0f / l; J[0] *= l; J[1] *= l; J[2] *= l; }
    b->p[0] += J[0]; b->p[1] += J[1]; b->p[2] += J[2];
    cross(t, arm, J);
    l = len3yzx(t);
    if (l > 10.0f) { l = 10.0f / l; t[0] *= l; t[1] *= l; t[2] *= l; }
    b->L[0] += t[0]; b->L[1] += t[1]; b->L[2] += t[2];
}
static void body_deriv(Body *b, float *d)                                /* 0x59c1e0 */
{
    if (b->top_speed != 0) {           /* the record's top-speed limiter */
        float top = b->power * b->top_speed + 1.9f, pitch = b->R[2], k;
        if (b->boost) top *= 1.03f;
        if (pitch <= 0) { pitch = -pitch; if (pitch > 0.3f) pitch = 0.3f; k = ((1.1f - 1.0f) / 0.3f) * pitch + 1; }
        else { if (pitch > 0.3f) pitch = 0.3f; k = 1 - ((1.0f - 0.9f) / 0.3f) * pitch; }
        if (k * top < sqrtf(b->v[1]*b->v[1] + b->v[0]*b->v[0]) * 2.23602486f &&
            b->R[0]*b->F[0] + b->R[1]*b->F[1] + b->R[2]*b->F[2] > 0) {
            float l[3];
            xf441T(l, b->F, b->R);
            l[0] = -(5.0f * pitch); if (l[0] > -0.5f) l[0] = -0.5f;
            xf441(b->F, l, b->R);
        }
    }
    memset(d, 0, 32 * sizeof *d);
    d[0] = b->v[0]; d[1] = b->v[1]; d[2] = b->v[2];
    for (int r = 0; r < 3; r++) cross(d + 4 + r*4, b->w, b->R + r*4);   /* 0x59c120 */
    d[19] = 1;
    d[20] = b->F[0]; d[21] = b->F[1]; d[22] = b->F[2];
    d[24] = b->T[0]; d[25] = b->T[1]; d[26] = b->T[2];
    float wl = len3(b->w);
    if (b->angdrag_on) {
        float k = -curve(&ANGDRAG, wl);
        d[24] += k * b->L[0]; d[25] += k * b->L[1]; d[26] += k * b->L[2];
    }
    float q = len3(b->v) / 4.0f;
    if (q < wl / 2.0f) q = wl / 2.0f;
    d[28] = curve(&SETTLE, q) <= b->settle ? -10.0f : 0.1f;
    if (b->damp_on) {
        float k = -(2.0f * b->settle);
        d[24] += k * b->L[0]; d[25] += k * b->L[1]; d[26] += k * b->L[2];
        k = -(8.0f * b->settle);
        d[20] += k * b->p[0]; d[21] += k * b->p[1]; d[22] += k * b->p[2];
    }
    if (b->yaw_clamp != 0) {           /* tested on the raw yaw torque +0x128, applied to the derivative */
        if (b->T[2] > b->yaw_clamp) d[26] = b->yaw_clamp;
        else if (-b->yaw_clamp > b->T[2]) d[26] = -b->yaw_clamp;
    }
}
static void body_post(Body *b)                                           /* 0x59c5f0 */
{
    for (int i = 0; i < 3; i++) b->v[i] = b->invmass * b->p[i];
    float l = len3(b->v);
    if (l > 200.0f && b->mass > 0.7f) {
        l = 200.0f / l;
        for (int i = 0; i < 3; i++) { b->p[i] *= l; b->v[i] = b->invmass * b->p[i]; }
    }
    for (int i = 0; i < 3; i++) b->acc_g[i] = b->invmass * 0.102040812f * b->F[i];
    {   /* w = L * (R^T (Iinv R)): the matrix first (0x43c990 x2), then 0x441000 */
        float rt[16], t[16];
        transpose4(rt, b->R);
        mmul4013(t, b->yawI_on ? b->Iinv2 : b->Iinv, b->R);
        mmul4013(t, rt, t);
        xf441(b->w, b->L, t);
    }
    l = len3(b->w);
    if (l > 250.0f) { l = 250.0f / l; b->w[0] *= l; b->w[1] *= l; b->w[2] *= l; }
    if (b->settle < 0) b->settle = 0;
    memcpy(b->world, b->R, 64);
    b->world[12] = b->pos[0]; b->world[13] = b->pos[1]; b->world[14] = b->pos[2]; b->world[15] = 1;
    memcpy(b->lastF, b->F, 12); memcpy(b->lastT, b->T, 12);
    memset(b->F, 0, 12); memset(b->T, 0, 12);
    if (b->gravity > 0) {
        float g[3] = { 0, 0, -(b->mass * b->gravity) }, z[3] = { 0, 0, 0 };
        body_force(b, g, z);
    }
}

/* ---- wheel / tyre ------------------------------------------------------- */
#define W_R(w) ((w)->pr[4])
static void wheel_deriv(N2Sim *s, Wheel *w, float *d)                   /* 0x59d350 */
{
    d[0] = w->w;
    w->tsmooth = ((w->torque - W_R(w) * w->Fx) + w->tsmooth) * 0.5f;
    d[1] = ((w->tsmooth - 0.002f * SURF(w->surf)->rolling * w->w) - w->wobble * 0.1f * w->w) / w->I;
    if (w->locked) d[1] = 0;
    if (!w->sliding && s->h > 0.001f) d[1] -= w->slip / (W_R(w) * s->h);
}
static void wheel_post(Wheel *w)                                         /* 0x59d030 */
{
    ident(w->spin);
    float c = sin16(w->angle + 1.57079637f), sn = sin16(w->angle);
    w->spin[8] = sn; w->spin[2] = -sn; w->spin[0] = c; w->spin[10] = c;
    w->Fx = w->Fy = 0; w->torque = 0; w->slip = 0;
    if (w->angle > 628.318f) { w->angle -= 628.318f; if (w->angle > 6283.18f) w->angle = 0; }
    else if (w->angle < -628.318f) { w->angle += 628.318f; if (w->angle < -6283.18f) w->angle = 0; }
    if (w->dir == 0 ? w->w < 0 : (w->dir == 2 && w->w > 0)) w->w = 0;
    w->dir = w->w > 1e-5f ? 0 : w->w >= -1e-5f ? 1 : 2;
}
static void wheel_contact(Wheel *w, int contact, float force)           /* 0x59d160 */
{
    w->contact = contact;
    if (contact)
        for (int i = 0; i < 3; i++) w->cpt[i] = w->world[12+i] + w->n[i] * -W_R(w);
    if (force > 0) {
        float t[3], k;
        cross(t, w->world + 4, w->n);
        cross(t, t, w->world + 4);
        k = (t[0]*w->n[0] + t[2]*w->n[2] + t[1]*w->n[1]) * 4.0f - 3.0f;
        if (k < 0.3f) k = 0.3f;
        w->load = k * force;
    } else w->load = 0;
}
static void wheel_lockcheck(Wheel *w, float f)                           /* 0x59d400 */
{
    float bt = (w->brk[1] * w->brake * 4.0f + w->brk[2] * w->hbrake * 10.0f) * 1.2f;
    if (bt <= W_R(w) * f + fabsf(w->w) * 0.1f) w->locked = 0;
    else { w->locked = bt > 1.0f; w->w = 0; }
}
static float wheel_lateral(const Wheel *w, float load, float alpha)     /* 0x59d510 */
{
    float t = ((alpha * 57.2957764f) / w->tyre[0]) * 12.0f * 0.5f;
    int i = (int)t;
    float x = (0.8f * load) / w->tyre[1];
    if (i > 5) return curve(&LAT[6], x) * w->tyre[2] * 2.5f;
    float a = curve(&LAT[i], x), c = curve(&LAT[i+1], x);
    return ((c - a) * (t - (float)i) + a) * w->tyre[2] * 2.5f;
}
static void wheel_tyre(N2Sim *s, Wheel *w, int brakelimit_off)        /* 0x5a8280 */
{
    float bf = w->brk[1] * w->brake * 4.0f, hf = w->brk[2] * w->hbrake * 10.0f;
    if (!w->locked) w->torque += w->w > 0 ? -bf : bf;
    if (!w->locked) w->torque += w->w > 0 ? -hf : hf;
    float lv[3];
    xf441T(lv, w->vel, w->world);
    float vf = lv[0], vl = lv[1];
    float alpha = atan2w(vf, vl);
    if (vf < 0) { alpha += PI_F; if (alpha > PI_F) alpha -= 2*PI_F; }
    w->slip = W_R(w) * w->w - vf;
    w->slip2 = w->slip;
    if (w->slip < -0.5f && w->torque < 0 && hf == 0 && !brakelimit_off)
        w->torque *= 0.5f / -w->slip;
    float sl = w->slip, m = sqrtf(sl*sl + vl*vl);
    if (m != 0 && (vl != 0 || vf != 0)) {
        float mu = SURF(w->surf)->slide * w->power * w->grip * w->tyre[4];
        float k = (w->load / m) * mu;
        wheel_lockcheck(w, fabsf(vf) * (w->load / sqrtf(vf*vf + vl*vl)) * mu);
        /* |alpha| (radians) against tyre[0] (12, degrees) is the retail compare */
        if (fabsf(sl) > 0.5f || fabsf(alpha) > w->tyre[0] || w->locked) {
            w->sliding = 1;
            float fx = sl * w->kx * k, fy = -(k * w->ky * vl);
            w->Fx = fx; w->Fy = fy;
            if (s->cs_speed * MPH < 1.0f && mu > 0.1f) { float r = 1.0f / mu; w->Fx = fx * r; w->Fy = r * fy; }
            w->slipv[0] = sl; w->slipv[1] = vl;
            return;
        }
    }
    w->sliding = 0;
    w->Fx = (w->kx / W_R(w)) * w->torque;
    float sv = vf <= 0 ? -1.0f : 1.0f, sa = alpha <= 0 ? 1.0f : -1.0f;
    float lat = wheel_lateral(w, w->load, fabsf(alpha));
    w->slipv[0] = 0; w->slipv[1] = vl;
    w->Fy = lat * w->ky * sa * sv;
}
static void wheel_circle(Wheel *w)                                       /* 0x59d600 */
{
    int boosted = 0;
    if (w->torque > 0 && !w->locked) { boosted = 1; w->Fx *= 1.5f; }
    float m = sqrtf(w->Fx*w->Fx + w->Fy*w->Fy);
    float lim = SURF(w->surf)->grip * w->tyre[3] * w->grip2 * w->power * w->grip * w->load;
    if (m <= lim || m <= 0.001f) { if (boosted) w->Fx /= 1.5f; }
    else { w->Fx *= lim / m; w->Fy *= lim / m; }
    if (lim <= 0.001f || (w->sratio = m / lim) > 1.0f) w->sratio = 1;
    if (w->sliding || w->locked) w->sratio = 1;
    if (!w->contact) w->sratio = 0;
    float lv[3];
    xf441T(lv, w->vel, w->world);
    float a = atan2w(lv[0], lv[1]);
    if (lv[0] < 0) { a += PI_F; if (a > PI_F) a -= 2*PI_F; }
    if (fabsf(lv[0]) > 1.0f) w->Fx -= sin16(a) * w->Fy * 0.15f;
    float f[3] = { w->Fx, w->Fy, w->load };                /* mode 0x7fc7fc = 1 */
    xf441R(w->force, f, w->world);
}

/* ---- suspension --------------------------------------------------------- */
static void susp_preload(Susp *u, float preload, const Body *b)        /* 0x59e0c0 */
{
    u->preload = preload;
    for (int i = 0; i < 3; i++) u->mount[i] = u->wheel->pr[i] - b->com[i];
}
/* Ground under point mp for wheel gi (0x5e72e0). The retail probe runs every
   substep. With a slow ground (the render mesh fallback) the world query runs
   once per wheel per frame and the later substeps use that triangle's plane. */
static int ground_at(N2Sim *s, int gi, const float *mp, float *gzo, float *n, int *surfo)
{
    float gz = 0; int surf = 0, ok;
    n[0] = n[1] = 0; n[2] = 1;
    if (!s->ground_exact && s->gc[gi].valid && fabsf(mp[0] - s->gc[gi].x) < 1.5f && fabsf(mp[1] - s->gc[gi].y) < 1.5f) {
        ok = s->gc[gi].ok; surf = s->gc[gi].surf; memcpy(n, s->gc[gi].n, 12);
        gz = s->gc[gi].z;
        if (ok && n[2] > 0.05f) gz -= (n[0] * (mp[0] - s->gc[gi].x) + n[1] * (mp[1] - s->gc[gi].y)) / n[2];
    } else {
        ok = s->ground && s->ground(s->user, mp[0], mp[1], mp[2], &gz, n, &surf);
        s->gc[gi].valid = 1; s->gc[gi].ok = ok; s->gc[gi].surf = surf;
        s->gc[gi].x = mp[0]; s->gc[gi].y = mp[1]; s->gc[gi].z = gz; memcpy(s->gc[gi].n, n, 12);
    }
    if (surf < 0 || surf >= WCOLL_NSURF) surf = 0;
    *gzo = gz; *surfo = surf;
    return ok;
}
static void susp_post(N2Sim *s, Susp *u)                                 /* 0x5a8790 */
{
    Body *b = &s->b; Wheel *w = u->wheel;
    float mp[3];
    xf441R(mp, u->mount, b->R);
    mp[0] += b->pos[0]; mp[1] += b->pos[1]; mp[2] += b->pos[2];
    /* ground probe 0x59d250 (0x5e72e0 ground height + surface lookup) */
    float gz, n[3]; int surf;
    float dist;
    int ok = ground_at(s, u->idx, mp, &gz, n, &surf);
    /* road noise 0x5d3ca0 at the probe point, wheel +0x168 */
    w->surfval = wcoll_bump(surf, mp[0], mp[1]);
    if (s->drag) surf = 1;                     /* a drag race: every surface is ROAD (0x59d250) */
    if (ok) {
        gz = (gz + w->hoff) + w->surfval;
        w->surf = surf; memcpy(w->n, n, 12);
        float up = b->R[8]*n[0] + b->R[10]*n[2] + b->R[9]*n[1];
        if (up < 0.001f) { dist = -100.0f; w->surf = 0; }
        else dist = ((gz - mp[2]) * n[2]) / up;
    } else { w->surf = s->drag ? 1 : 0; w->n[0] = w->n[1] = 0; w->n[2] = 1; dist = -100.0f; }
    float x = dist + W_R(w), x0 = u->x;
    if (x > u->rec[5]) { u->x = u->rec[5]; u->contact = 1; }
    else if (x < u->rec[6]) { u->x = u->rec[6]; u->contact = 0; }
    else { u->x = x; u->contact = 1; }
    if (s->h > 0.0001f) u->xv = (u->x - x0) / s->h;
    u->mnow[0] = u->mount[0]; u->mnow[1] = u->mount[1]; u->mnow[2] = u->mount[2] + u->x;
    float m[16];
    ident(m);
    memcpy(u->m, m, 64);
    u->m[12] = u->mnow[0]; u->m[13] = u->mnow[1]; u->m[14] = u->mnow[2];
    if (u->steered) mmul4013(m, s->steer_m[u->idx], m);
    m[12] = u->mnow[0]; m[13] = u->mnow[1]; m[14] = u->mnow[2]; m[15] = 1;
    memcpy(w->local, m, 64);                                 /* 0x591480 */
    float r[3], t[3];
    xf441R(r, u->mnow, b->R);
    cross(t, b->w, r);
    w->vel[0] = t[0] + b->v[0]; w->vel[1] = t[1] + b->v[1]; w->vel[2] = t[2] + b->v[2];
    mmul4013(w->world, m, b->world);                         /* 0x43c990, 0x5913d0 */
    if (u->contact) {
        const float *k = u->rec;
        float f = ((u->x > 0 ? u->x : 0) * k[0] + 1.0f) * k[1] * u->x
                + k[u->xv <= 0 ? 3 : 2] * u->xv + u->preload;
        f += (u->x - u->partner->x) * k[4];
        u->force = f;
        if (f >= 0) return;
    }
    u->force = 0;
}
static float susp_bumpstop(N2Sim *s, Susp *u)                            /* 0x59e1a0 */
{
    Body *b = &s->b; Wheel *w = u->wheel;
    float r[3], v[3];
    xf441R(r, u->mnow, b->R);
    cross(v, b->w, r);
    v[0] += b->v[0]; v[1] += b->v[1]; v[2] += b->v[2];
    float k = -(w->n[0]*v[0] + w->n[1]*v[1] + w->n[2]*v[2]);
    if (k < 0) return 0;
    k = 0.225f * k * b->mass;
    float j[3] = { w->n[0]*k, w->n[1]*k, w->n[2]*k };
    body_impulse(b, j, r);
    return k;
}

/* ---- nitrous --------------------------------------------------------------
   The car state's bottle (+0x414 capacity, +0x41c seconds left, +0x420 burn
   start): a burn runs from the press until release or until the bottle is
   empty. While it burns, the tank level picks a stage from 0x7a1790
   {0, 1.4, 1.7} (a normal bottle never passes 1, so stage 1); the engine adds
   throttle x torque curve x P+0x2ac x 0x7a1798[stage] (0x594300) and every
   tyre's grip is scaled by 0x7a17a4[stage] (0x5a4d80). */
static const float NOS_STEP[3] = { 0, 1.4f, 1.7f };                    /* 0x7a1790 */
static const float NOS_TORQUE[4] = { 1.7f, 1.0f, 1.4f, 1.7f };         /* 0x7a1798 */
static const float NOS_GRIP[4] = { 1.7f, 1.0f, 1.4f, 1.7f };           /* 0x7a17a4 */
/* the nitrous torque curve over idle..redline (Engine +0x60, ctor 0x5aa5a0;
   the Mellovator's +0x180 too) */
static const float MELLO_NOS_D[9] = { 0.3f,0.4f,0.45f,0.5f,0.55f,0.6f,0.7f,1.0f,0.7f };   /* 0x7fbf38 */
static float nos_remaining(const N2Sim *s)                              /* 0x592480 */
{
    return s->nos_left - (s->nos_burn ? (float)(s->time - s->nos_start) : 0.0f);
}
static float nos_fraction(const N2Sim *s)                               /* 0x5925b0 */
{
    float r = nos_remaining(s), c = s->nos_cap;
    if (c <= 0) return 0;
    return r <= c ? r / c : 1.0f + (r - c) / c;
}
static void nos_stop(N2Sim *s)                                          /* 0x592500 */
{
    if (!s->nos_burn) return;
    s->nos_left = nos_remaining(s);
    s->nos_burn = 0;
}
static int nos_stage(N2Sim *s)                                          /* 0x59fa30 */
{
    if (!s->nos_burn) return 0;
    float f = nos_fraction(s);
    for (int i = 2; i >= 0; i--) if (f > NOS_STEP[i]) return i + 1;
    nos_stop(s);
    return 0;
}
static int nos_start(N2Sim *s)                                          /* 0x59faf0 */
{
    if (s->nos_burn) return 0;                                          /* 0x59faa0 */
    float f = nos_fraction(s);
    for (int i = 2; i >= 0; i--)
        if (NOS_STEP[i] < f) { s->nos_start = s->time; s->nos_burn = 1; return 1; }
    return 0;
}
/* The nitrous press (Player 0x6034b6): StartNitrous succeeded, then
   0x5ecdb0 -- the gas (driver +0x20c) within 0.001 of 0 or the car under
   5 mph (+0x430 x 2.237), and a burn stage (0x59fa30) -- is a purge: SPRAY
   SHOW (15) instead of the nitrous sound (0x4022c0) and +0x225. The online
   test (0x5f00f0 / 0x890118 == 4) is false offline. */
static void nos_press(N2Sim *s)
{
    if (!nos_start(s)) return;
    if ((fabsf(s->eff.throttle) <= 0.001f || len3(s->b.v) * 2.237f < 5.0f)   /* 0x78435c, 0x78426c, 0x7a5660 */
        && nos_stage(s))
        s->nos_spray = 1;
}
int n2sim_nos_spray(N2Sim *s)
{
    int r = s && s->nos_spray;
    if (s) s->nos_spray = 0;
    return r;
}
void n2sim_nitrous_refill(N2Sim *s)                                     /* 0x591cd0 */
{
    s->nos_cap = s->nos_left = PF(0x2a0);
    s->nos_burn = 0;
}
/* 0x592640 with a positive amount (style-point refill, 0x60177b): the
   level rises by amount x bottle, and may pass a full bottle -- the stages
   1.4 and 1.7 above are reached that way */
void n2sim_nitrous_add(N2Sim *s, float amount)
{
    if (amount > 0) s->nos_left += amount * s->nos_cap;
}
/* FillNitrous 0x592640: the exact retail top-up. The bottle level in
   seconds (+0x41c) is kept in 1/4000 s ticks (ftol(x * 4000 + 0.5)); the
   level fraction f comes from 0x5925b0 (burn-adjusted) while the sum adds to
   the stored +0x41c. Below a full bottle the amount fills at 1 bottle per
   unit; above it the overflow (up to 2 bottles) at 0x7a178c = 1.0 per unit.
   A negative amount drains the same two bands down to empty. */
static float nos_ticks(float sec)
{
    float x = sec * 4000.0f + 0.5f;                                     /* 0x784264, 0x784260 */
    return (float)(int)x / 4000.0f;
}
void n2sim_nitrous_fill(N2Sim *s, float amount)                         /* 0x592640 */
{
    float cap = s->nos_cap, f = nos_fraction(s);
    if (amount > 0) {
        if (f < 1.0f) {
            float a = 1.0f - f < amount ? 1.0f - f : amount;
            s->nos_left = nos_ticks(a * cap + s->nos_left);
            f += a; amount -= a;
        }
        if (amount > 0) {
            float b = 2.0f - f < amount ? 2.0f - f : amount;
            s->nos_left = nos_ticks(1.0f * b * cap + s->nos_left);      /* 0x7a178c */
        }
    } else {
        if (f > 1.0f) {
            float x = 1.0f - f > amount ? 1.0f - f : amount;
            s->nos_left = nos_ticks(1.0f * x * cap + s->nos_left);
            f += x; amount -= x;
        }
        if (amount < 0) {
            float y = -f > amount ? -f : amount;
            s->nos_left = nos_ticks(y * cap + s->nos_left);
        }
    }
}
/* Performance package, category 2 (chunk 0x34a15): one part per level worth
   1/3, 2/3, 1 of the way from the stock record to the upgrade targets; the
   0x5ac4f0 blend moves the bottle size P+0x2a0 towards P+0x580 and takes the
   boost P+0x2ac from P+0x58c whenever any level is fitted. */
void n2sim_set_nitrous(N2Sim *s, int level)
{
    static const float PKG[4] = { 0, 0.333f, 0.667f, 1.0f };
    float *p = (float *)(s->rec + 0x110);
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    float x = PKG[level];
    p[0x2a0/4] = s->nos_stock[0];
    p[0x2ac/4] = s->nos_stock[1];
    if (x != 0) {
        p[0x2a0/4] = (p[0x580/4] - s->nos_stock[0]) * x + s->nos_stock[0];
        p[0x2ac/4] = p[0x58c/4] + p[0x33c/4];   /* + the junkman nitrous (perf_build) */
    }
    s->nos_pkg = level;
    n2sim_nitrous_refill(s);
}

/* ---- engine ------------------------------------------------------------- */
static float engine_torque(N2Sim *s)                                     /* 0x594300 */
{
    Engine *e = &s->e;
    float rpm = e->w * RPM;
    float t = curve(&e->torque, rpm) * e->power;
    if (e->flag74) t *= 1.05f;
    float thr = e->limiter ? 0 : e->thrmul * e->thr;
    t *= thr;
    if (s->nos_stage > 0)              /* nitrous, engine +0x84 */
        t += thr * ((curve(&e->nos, rpm) * PF(0x2ac)) * NOS_TORQUE[s->nos_stage]);
    if (0.6f < e->heat) {              /* over-rev heat +0x8c (0x7fbff0, 0x7a1914) */
        float h = e->heat;
        if (1.0f < h) h = 1.0f;
        t *= 1.0f - ((h - 0.6f) / (1.0f - 0.6f)) * 0.75f;
    }
    float b = -((1.0f - thr) * curve(&e->brake, rpm));
    if (e->limiter) b *= 2.0f;
    b += t;
    if (b < 0 && !e->limiter) b *= s->d.gear == 1 ? 0.65f : 0.85f;
    return b;
}
/* 0x5a11e0: the over-rev heat (a drag race turns it on, 0x5a49a0): past
   the redline it climbs 0.45 a second, below it cools 0.1; at 1.2 the
   engine blows (2). Not while the launch's grade still boosts first gear. */
static void engine_heat(N2Sim *s)
{
    Engine *e = &s->e;
    if (!(0 <= e->heat) || !(e->heat < 2.0f)) return;
    if (s->d.gear == 2 && 0 < s->d.q) return;
    float rate = e->rec[1] < e->w * RPM ? 0.45f : -0.1f;               /* 0x7a1904 / 0x7a1908 */
    float h = rate * s->h + e->heat;
    if (h < 0) h = 0;
    e->heat = h;
    if (1.2f <= h) e->heat = 2.0f;                                      /* 0x7a190c / 0x7a1910 */
}
static void engine_post(N2Sim *s)                                        /* 0x5aa760 */
{
    Engine *e = &s->e; const float *r = e->rec;
    float w = r[0] * RPM2RAD;
    if (w < e->w) w = e->w;
    if (r[2] * RPM2RAD < w) w = r[2] * RPM2RAD;
    e->w = w;
    float rpm = w * RPM;
    e->load = 0;
    if (e->limiter) { if (rpm < r[2] - 0.0125f * r[2]) e->limiter = 0; }
    else if (-25.0f + r[2] < rpm) e->limiter = 1;
    /* 0x5a0f30 turbo gauge drives no forces */
    engine_heat(s);
}

/* ---- drivetrain --------------------------------------------------------- */
#define GB_RATIO(d, g) ((d)->gb[8 + (g)])
#define GB_GEARS(d) (*(const int *)&(d)->gb[6])     /* forward gear count, an int */
static int rear_driven(const Drive *d)  { return d->gb[4] >= 0.001f; }
static int front_driven(const Drive *d) { return d->gb[4] < 0.999f; }
static float drive_speed(N2Sim *s)                                       /* 0x5945f0 */
{
    Drive *d = &s->d; Wheel *w = s->wh; float v = 0;
    if (rear_driven(d)) v = (w[3].w + w[2].w) * 0.5f * d->gb[3];
    if (front_driven(d)) {
        v += (w[1].w + w[0].w) * 0.5f * d->gb[2];
        if (rear_driven(d)) v *= 0.5f;
    }
    return v * GB_RATIO(d, d->gear);
}
static float drive_slip(N2Sim *s)                                        /* 0x5947c0 */
{
    Drive *d = &s->d; Wheel *w = s->wh; float v = 0; int n = 0;
    if (rear_driven(d)) { v = w[3].slip2 + w[2].slip2; n = 2; }
    if (front_driven(d)) { n += 2; v += w[1].slip2 + w[0].slip2; }
    return v / (float)n;
}
static float drive_wheelacc(N2Sim *s)                                    /* 0x594840 */
{
    Drive *d = &s->d; Wheel *w = s->wh; float v = 0;
#define WACC(k) ((w[k].torque - W_R(&w[k]) * w[k].Fx) / w[k].I)
    if (rear_driven(d)) v = (WACC(2) + WACC(3)) * 0.5f * d->gb[3];
    if (front_driven(d)) {
        v += (WACC(0) + WACC(1)) * 0.5f * d->gb[2];
        if (rear_driven(d)) v *= 0.5f;
    }
#undef WACC
    return v * GB_RATIO(d, d->gear);
}
static float lsd_split(N2Sim *s, Wheel *a, Wheel *b)                    /* part of 0x594a30 */
{
    float p = (!a->contact || !b->contact) ? 0.5f : 0.35f;
    (void)s;                           /* carstate +0x42c launch lock modes are 0 */
    float sum = a->w + b->w, f = 0.5f;
    if (fabsf(sum) > 1e-5f) {
        f = b->w / sum;
        if (f >= p) { if (1.0f - p < f) f = 0.35f * 0.5f; }
        else f = 1.0f - 0.35f * 0.5f;
    }
    return f;
}
static void drive_torque(N2Sim *s, float t)                              /* 0x594a30 */
{
    Drive *d = &s->d; Wheel *w = s->wh;
    if (d->gear == 1) return;
    d->sum += t;
    float k = (float)s->cs_wheels * 0.25f;
    if (k < 0.25f) k = 0.25f;
    s->e.load -= k * t;
    t *= GB_RATIO(d, d->gear);
    float tf = (1.0f - d->rear) * d->gb[2] * t, tr = d->gb[3] * d->rear * t;
    float f = lsd_split(s, &w[0], &w[1]);
    if (!w[0].locked) w[0].torque += (1.0f - f) * tf;
    if (!w[1].locked) w[1].torque += f * tf;
    f = lsd_split(s, &w[3], &w[2]);
    if (!w[2].locked) w[2].torque += f * tr;
    if (!w[3].locked) w[3].torque += (1.0f - f) * tr;
}
static void drive_clutch(N2Sim *s)                                       /* 0x594cb0 */
{
    Drive *d = &s->d;
    d->sum = 0;
    if (d->state == 0 || d->state == 2) {
        float dw = s->e.w - d->drivew;
        if (dw > 300.0f) dw = 300.0f; else if (dw < -300.0f) dw = -300.0f;
        float c = d->gb[5], rd = (d->drivew - s->e.w) * RPM;
        if (d->prevdiff != 0 && rd * d->prevdiff < 0 && fabsf(rd) > 2000.0f && fabsf(d->prevdiff) > 2000.0f)
            c *= 0.5f;
        d->prevdiff = rd;
        drive_torque(s, c * d->engage * dw);
    } else if (d->state == 3)
        drive_torque(s, engine_torque(s));
}
static void drive_couple(N2Sim *s)                                       /* 0x594da0 */
{
    Drive *d = &s->d; Wheel *w = s->wh;
    if (d->state != 3) return;
    float te = engine_torque(s), ie = s->e.rec[3];
    float acc = drive_wheelacc(s), k = 1.0f / ie;
    float g = GB_RATIO(d, d->gear);
    if (front_driven(d)) { float r = g * d->gb[2]; k += ((1.0f - d->rear) / w[0].I) * r * r * 0.5f; }
    if (rear_driven(d))  { float r = g * d->gb[3]; k += (d->rear / w[2].I) * r * r * 0.5f; }
    drive_torque(s, ((te + s->e.load) / ie - acc) / k);
}
static void drive_match(N2Sim *s, float target)                          /* 0x5a12d0 */
{
    Drive *d = &s->d; Wheel *w = s->wh;
    float cur = drive_speed(s);
    int slow = s->cs_absv * MPH < 40.0f;
    if (rear_driven(d)) {
        int both = slow || (!w[2].contact && !w[3].contact);
        float dv = (target - cur) / (GB_RATIO(d, d->gear) * d->gb[3]);
        float a = both ? (w[3].w + w[2].w) * 0.5f : w[2].w, b = both ? a : w[3].w;
        w[2].w = a + dv; w[3].w = b + dv;
    }
    if (front_driven(d)) {
        int both = slow || (!w[0].contact && !w[1].contact);
        float dv = (target - cur) / (GB_RATIO(d, d->gear) * d->gb[2]);
        float a = both ? (w[1].w + w[0].w) * 0.5f : w[0].w, b = both ? a : w[1].w;
        w[0].w = a + dv; w[1].w = b + dv;
    }
}
static void drive_after(N2Sim *s)                                        /* 0x5aa830 */
{
    Drive *d = &s->d; Engine *e = &s->e;
    if (d->gear != 1 && d->state == 3 && s->cs_wheels > 0) {
        float diff = drive_speed(s) - e->w;
        if (e->thr <= 0.2f || drive_slip(s) <= 0.1f) {
            float t = fabsf(engine_torque(s)), c = diff * d->gb[5];
            if (c < -t) c = -t;
            if (c > t) c = t;
            e->load += c;
        } else { d->drivew = e->w; drive_match(s, e->w); }
    }
    /* first-gear engine-braking trim (0x7fc8c0 * 0x7a18ac[class]) is 0 for class 0 */
}
static int drive_stopped(N2Sim *s)                                       /* 0x594e90 */
{
    return s->e.thr < 0.3f && s->d.drivew <= s->e.rec[0] * RPM2RAD + 0.0001f;
}
/* the rain's grip loss (TuneWheelParams 0x5a4d80, MellowMover 0x5aad90):
   1 - (1 - 0.975) x GetDesiredRainIntensity 0x5ed5d0 at mover +0x5c -> +0x60 /
   +0x64 = carstate +0x20 / +0x24, the body position 0x5a5540 published at the
   end of the last step (= s->b.pos here) */
float (*n2sim_rain_at)(float x, float y);
static float n2_rain_grip(const N2Sim *s)
{
    return n2sim_rain_at ? 1.0f - (1.0f - 0.975f) * n2sim_rain_at(s->b.pos[0], s->b.pos[1]) : 1.0f;   /* 0x7a19e4 */
}
static void drive_rand(N2Sim *s)                                         /* 0x594710 */
{
    Drive *d = &s->d;
    for (int i = 0; i < 3; i++)
        d->rnd[i] = frand(s) * (d->rndrange[i*2+1] - d->rndrange[i*2]) + d->rndrange[i*2];
}
static void drive_post(N2Sim *s)                                         /* 0x5a1400 */
{
    Drive *d = &s->d; Engine *e = &s->e;
    d->drivew = drive_speed(s);
    if (d->mode == 3) {
        /* clutch-pedal transmission: 0x594910 sets state and engage from the
           pedal; here only the bookkeeping. Engaged: the rpm for the next
           upshift's grade, the graded boost start; disengaged (1) mid-shift:
           the gear goes in. */
        e->thrmul = 1.0f;
        if (d->state == 3) {
            d->disrpm = e->w * RPM;                                     /* 0x7a1d28 */
            if (0 < d->q && d->shift_t0 == 0) d->shift_t0 = (float)s->time;
        } else if (d->state == 1 && d->shifting) { d->shifting = 0; d->gear = d->target; }
    } else
    switch (d->state) {
    case 0:
        e->thrmul = d->neutral ? 1.0f : 0.0f;
        if (d->engage <= 0) { d->engage = 0; d->state = 1; }
        if (d->neutral) d->state = 1;
        break;
    case 1:
        e->thrmul = d->neutral ? 1.0f : 0.0f;
        d->gear = d->target;
        if (d->shifting || !drive_stopped(s)) {
            if (!d->neutral) d->state = 2;
            drive_rand(s);
        }
        break;
    case 2:
        e->thrmul = d->neutral ? 1.0f : 0.7f;
        if (d->engage >= 1.0f) {
            d->engage = 1.0f;
            if (fabsf(e->w - d->drivew) < 50.0f || d->gear == 1) d->state = 3;
        }
        if (drive_stopped(s) && d->gear != 1) d->state = 0;
        if (d->neutral) d->state = 1;
        break;
    case 3:
        if (0 < d->q && d->shift_t0 == 0) d->shift_t0 = (float)s->time;
        e->thrmul = 1.0f;
        if (d->gear != 1 && drive_stopped(s)) d->state = 0;
        if (d->gear == d->target) d->shifting = 0;
        else { d->shifting = 1; d->state = 0; }
        if (d->neutral) d->state = 1;
        break;
    }
    if (d->shift_t != 0) {
        e->thrmul = 0;
        if (0.45f <= (float)s->time - d->shift_t) d->shift_t = 0;
    } else if (d->state == 3 && 0 < d->shift_t0) {
        /* a graded shift (drag): extra throttle fading over 0.5 s */
        float t = (float)s->time - d->shift_t0;
        float f = ((0.5f - t) / 0.5f) * d->q * 2.0f;                    /* 0x7a18cc / 0x7a18d0 */
        if (0 < f) e->thrmul = f + 1.0f;
        if (0.5f <= t) { d->shift_t0 = 0; d->q = 0; }
    }
}
/* 0x594f20: how well an upshift out of `gear` at `rpm` was timed against
   the gear's shift point: 1 perfect (just past it), -1 over-revved, else
   0..1 short of it (rounded to 0.5 / 0.85) */
static const float SHIFT_WIN[4][9] = {                                  /* 0x7fbf60 */
    { 0, 560, 360, 175, 160, 140, 140, 0, 0 }, { 0, 600, 400, 230, 220, 160, 160, 0, 0 },
    { 0, 850, 550, 300, 250, 200, 180, 0, 0 }, { 0, 1100, 750, 400, 325, 290, 230, 0, 0 },
};
static float shift_grade(const N2Sim *s, float rpm, int gear, int round)
{
    const Drive *d = &s->d;
    if (gear < 0 || gear > 8) return 0;
    int k = d->klass < 0 ? 0 : d->klass > 3 ? 3 : d->klass;
    float diff = d->up[gear < 10 ? gear : 9] - rpm;
    float w = SHIFT_WIN[k][gear] * d->win;
    if (!(0 < w)) return 0;
    if (diff < 0) return fabsf(diff) < (1.0f - 0.95f) * w ? 1.0f : -1.0f;   /* 0x7fbf5c */
    float q = 1.0f - diff / (0.95f * w);
    if (q < 0) return 0;
    if (0 < q && round) return q < 0.5f ? 0.5f : 0.85f;                 /* 0x7a1d3c */
    return q;
}
static float drive_uprpm(N2Sim *s, int g, int low)                      /* 0x594670 */
{
    Drive *d = &s->d;
    float f = (g == 3 ? d->rnd[2] : d->rnd[0]);
    if (low) f = d->rndrange[0];
    f += d->up[g];
    float red = s->drag ? s->e.rec[2] : s->e.rec[1];                     /* drag: the limiter */
    return red < f ? red : f;
}
static void drive_auto(N2Sim *s, float rpm)                              /* 0x5aa960, request -1 */
{
    Drive *d = &s->d; Wheel *w = s->wh;
    int g = d->gear;
    d->req = 1;
    if (g < 2) d->target = 2;
    else {
        if (d->state != 3) return;
        float up = drive_uprpm(s, g, 0);
        float down = d->rnd[1] + d->down[g], lowup = 0;
        if (g != 2) lowup = (GB_RATIO(d, g) / GB_RATIO(d, g - 1)) * drive_uprpm(s, g - 1, 1) - 200.0f;
        if (g <= 2) down = s->e.thr < 0.2f ? s->e.rec[0] + 100.0f : 0.0f;
        if (rpm < up || GB_GEARS(d) + 1 <= g) {
            if (rpm <= down && rpm <= lowup) d->target = g - 1;
        } else {
            int ok = 1;
            for (int i = 0; i < 4; i++) ok &= w[i].contact && w[i].slip < 4.0f;
            if (ok) d->target = g + 1;
        }
    }
    /* 0x5a1700 upshift launch boost is drag-only */
}

/* ---- aero --------------------------------------------------------------- */
static void aero_force(N2Sim *s)                                         /* 0x59e620 */
{
    Aero *a = &s->a; Body *b = &s->b;
    float sp = len3(b->v) / a->speedscale;
    float k = -(((((a->surfdrag * a->scale) * a->rec[0]) * sp) * ((1.0f - a->thr) * (2.0f - 1.0f) + 1.0f)) * a->on);
    float f[3] = { k * b->v[0], k * b->v[1], k * b->v[2] }, arm[3] = { 0, 0, 0 };
    if (a->ground >= 0.5f) arm[2] = (1.0f - a->thr) * -0.1f;
    body_force(b, f, arm);
    float up = b->R[10];
    if (up < 0) up = 0;
    if (a->ground >= 0.5f) up = 1;
    float dir = 0;
    if (sp > 0.0001f) { float r = 1.0f / sp; dir = ((b->v[2]*r)*b->R[2] + (b->v[1]*r)*b->R[1]) + (r*b->v[0])*b->R[0]; }
    if (dir < 0) dir = 0;
    float q;
    if (s->driftlike) q = dir * dir * dir * dir;
    else { q = dir * dir; if (q < 0.4f) q = 0.4f; }
    float df, dr;
    if (a->ground != 0) { df = q * a->rec[1] * up * sp; dr = q * a->rec[2] * up * sp; }
    else { float t = (a->rec[2] + a->rec[1]) * q * up * sp; df = 0.4f * t; dr = t * 0.41f; }
    float lf[3] = { 0, 0, -(df * a->on) }, lr[3] = { 0, 0, -(dr * a->on) };
    float pf[3] = { 1, 0, 0 }, pr[3] = { -1, 0, 0 }, wf[3], wp[3];
    xf441R(wf, lf, b->R); xf441R(wp, pf, b->R); body_force(b, wf, wp);
    xf441R(wf, lr, b->R); xf441R(wp, pr, b->R); body_force(b, wf, wp);
}

/* ---- the force pass and tyre solver ------------------------------------- */
static void tyre_solve(N2Sim *s)                                         /* 0x5abdf0 */
{
    Body *b = &s->b; Wheel *w = s->wh; Susp *u = s->su;
    drive_clutch(s);
    for (int i = 0; i < 4; i++) wheel_tyre(s, &w[i], 0);
    float fb[3];
    xf441T(fb, b->F, b->R);
    const float *Ii = b->yawI_on ? b->Iinv2 : b->Iinv;
    for (int it = 0; it < 5; it++) {
        float fx[4], fy[4], sx = fb[0], sy = fb[1], yaw;
        for (int i = 0; i < 4; i++) {
            fx[i] = w[i].local[0] * w[i].Fx + w[i].local[4] * w[i].Fy;
            fy[i] = w[i].local[1] * w[i].Fx + w[i].local[5] * w[i].Fy;
            sx += fx[i]; sy += fy[i];
        }
        /* the retail association (x87 at single precision rounds every op):
           x 1/mass, and the yaw sum as one chain */
        yaw = fy[0] * u[0].mount[0] - fx[0] * u[0].mount[1];
        for (int i = 1; i < 4; i++) { yaw = yaw + fy[i] * u[i].mount[0]; yaw = yaw - fx[i] * u[i].mount[1]; }
        float im = 1.0f / b->mass, ax = sx * im, ay = im * sy;
        yaw /= 1.0f / Ii[10];
        for (int i = 0; i < 4; i++) {
            Wheel *q = &w[i];
            if (q->sliding || q->locked) continue;
            float R = W_R(q);
            q->Fx = ((((q->torque - R * q->Fx) / q->I) * R
                     - ((ax - yaw * u[i].mount[1]) * q->local[0] + (yaw * u[i].mount[0] + ay) * q->local[4])) / R)
                    * q->I + q->Fx;
        }
        drive_couple(s);
    }
    for (int i = 0; i < 4; i++) wheel_circle(&w[i]);
    drive_after(s);
}
/* 0x5ac260 (drift-type races): a yaw torque along the car's up axis that
   pushes the slide on while the car turns into it (two wheels or more
   down), from the slide angle's graph (AI 0x86b570 / human 0x86b3c8) x the
   drive type's gain for the difficulty x the yaw rate; and the rear tyres'
   friction circle (+0x188) from a blend of slide angle and yaw rate. Reads
   the car state published last frame. */
static const float YAW_HUMAN[14] = { 0,0, 0.261799395f,0.1f, 0.523598790f,0.45f, 0.785398185f,0.85f,
                                     1.04719758f,0.95f, 1.55334306f,1.15f, 1.57079637f,0 };   /* 0x86b5b0 */
static const float YAW_AI[14]    = { 0,0, 0.261799395f,0.1f, 0.523598790f,0.45f, 0.785398185f,0.85f,
                                     1.04719758f,0.95f, 1.55334306f,1.15f, 1.57079637f,1 };   /* 0x86b5e8 */
static const float DRIFT_GAIN[3][3] = { { -10, -7, -4 }, { -7.5f, -5, -4 }, { -15, -10, -6 } };   /* 0x7fc2d0 + 0x144 k */
static const float DRIFT_BLEND[3] = { 0.35f, 0.55f, 0.5f };                     /* 0x7fc2dc */
static const float DRIFT_CIRC_K[3] = { 1, 1, 1.125f };                          /* 0x7fc2e0 */
static const float DRIFT_CIRC_D[10] = { 1.1f,0.93f,0.86f,0.82f,0.8f,0.792f,0.78f,0.77f,0.765f,0.76f };
static const Curve DRIFT_CIRC = { 10, 0, 1, 9, DRIFT_CIRC_D };                  /* 0x7fc910 */
static void drift_assist(N2Sim *s)
{
    Body *b = &s->b;
    unsigned short va = fast_atan(s->cs_v[0], s->cs_v[1]);   /* 0x5ac278 */
    float slip = (float)(unsigned short)(s->cs_head - va) * U16RAD;
    if (PI_F < slip) slip -= PI_F + PI_F;
    float yaw = s->cs_w[2];
    float rear = PF(0x1c0);
    int k = !(rear < 0.2f);
    if (0.8f < rear) k = 2;
    int dif = s->race_diff < 0 ? 0 : s->race_diff > 2 ? 2 : s->race_diff;
    float gain = DRIFT_GAIN[k][dif];
    if (0 < slip * yaw && 2 <= s->cs_wheels) {
        float g = graph(s->race_ai ? YAW_AI : YAW_HUMAN, 7, fabsf(slip));
        float f = g * gain * yaw;
        b->T[0] += f * b->R[8]; b->T[1] += f * b->R[9]; b->T[2] += f * b->R[10];
    }
    float x = fabsf(slip) * (1.0f - DRIFT_BLEND[k]) + fabsf(yaw) * DRIFT_BLEND[k];
    float c = curve(&DRIFT_CIRC, x) * DRIFT_CIRC_K[k];
    s->wh[2].grip2 = c; s->wh[3].grip2 = c;
}
/* 0x5a51b0, the full simulation's hydraulics (the Mellovator's 0x5a2fd0,
   with this model's parts): the lifted pair, the lift per wheel car +0x3b8
   and what it did mover +0x74[4] as there (the lift moves no suspension
   here); the bounce impulse goes in at the suspension mount (+0x30) turned
   into the world (0x441000; the body matrix carries no translation), along
   the wheel's ground normal (+0x150), held back while a bouncing wheel's
   suspension hangs (+0x1c < -0.01). (Its gate, mover +0x5c -> +0x3c ->
   byte +0x18 > 0, is not ported.) */
static void hydraulics_phys(N2Sim *s)
{
    Body *b = &s->b;
    const float *bo = s->hyd_bounce;
    int mask = 0;
    if (bo[0] != 0 && bo[1] != 0) mask = 0xc;
    else if (bo[2] != 0 && bo[3] != 0) mask = 3;
    else if ((bo[2] != 0 && bo[1] != 0) || (bo[3] != 0 && bo[0] != 0)) mask = 0x20;
    int ready = mask != 0;
    float rate = s->h * 0.5f;                                 /* 0x7fc800 x 0x7fc098 */
    for (int i = 0; i < 4; i++) {
        float t = s->hyd_extend[i] * 0.2f, *h = &s->hyd_h[i];
        int lift = (1 << i) & mask;
        s->hyd_code[i] = 0;
        if (lift && !(t > 0.2f)) t = 0.2f;
        if (t > *h) {
            float u = *h + rate;
            *h = t < u ? t : u;
            s->hyd_code[i] = 2;
        } else if (t < *h) {
            float u = *h - rate;
            *h = u < 0.0f ? 0.0f : u;
            s->hyd_code[i] = 3;
        } else if (t > 0.0f) s->hyd_code[i] = 1;
        if (lift && *h < 0.2f) ready = 0;
    }
    if (!ready) return;
    for (int i = 0; i < 4; i++)
        if (bo[i] > 0 && s->su[i].x < -0.01f) return;
    for (int i = 0; i < 4; i++) {
        if (!(bo[i] > 0)) continue;
        float r[3], pv[3];
        vmul(r, s->su[i].mnow, b->R);                         /* 0x441000 */
        cross(pv, b->w, r);
        pv[0] += b->v[0]; pv[1] += b->v[1]; pv[2] += b->v[2];
        const float *n = s->su[i].wheel->n;
        float d = pv[0] * n[0] + pv[1] * n[1] + pv[2] * n[2];
        if (d < 0.0f) continue;
        float f = (0.9f - d * 0.3f) * bo[i];
        if (!(f > 0.0f)) continue;
        f *= b->mass;
        float J[3] = { f * n[0], f * n[1], f * n[2] };
        body_impulse(b, J, r);                                /* 0x59bc60 */
        s->hyd_hit[i] = 1;                                    /* car +0x3c8 */
    }
}
/* PhysicsMover::ResolveDynoForces (GC 801c2cb0, the end of ResolveForces): the
   car on the rollers. The body keeps its momentum -- the tyres see the road
   move under them, the engine loads up as on the street -- but its x / y are
   the virtual position, tethered to the dyno spot by a spring (150) and a
   damper (-15); the last substep's force (RB +0x130) pushes it only while it
   accelerates the car (F . v > 0). The non-driven wheels turn at the virtual
   speed along the car (|v| < 0.03 -> 0, Wheel::SetState). */
static void dyno_forces(N2Sim *s)
{
    Body *b = &s->b;
    float F[3] = { 0, 0, 0 }, h = s->h;
    if (b->lastF[0] * b->v[0] + b->lastF[1] * b->v[1] + b->lastF[2] * b->v[2] > 0.0f)
        memcpy(F, b->lastF, sizeof F);
    for (int i = 0; i < 3; i++) {
        F[i] += 150.0f * (s->dy_spot[i] - s->dy_pos[i]) - 15.0f * s->dy_vel[i];
        s->dy_vel[i] += F[i] * (b->invmass * h);
        s->dy_pos[i] += s->dy_vel[i] * h;
    }
    s->dy_pos[2] = b->pos[2];
    b->pos[0] = s->dy_pos[0]; b->pos[1] = s->dy_pos[1];
    /* GUESS (not in ResolveDynoForces): no yaw on the rollers. With the
       undriven axle gripless (TuneWheelParams) nothing holds the car's heading
       and the port's car turned on the spot within a few seconds */
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_SIM_DYNO_NOHOLD drops the yaw hold; the heading logged each second */
        static double lt; static int nohold = -1;
        if (nohold < 0) nohold = getenv("OPENUG2_SIM_DYNO_NOHOLD") != NULL;
        if (s->time - lt >= 1.0 || s->time < lt) {
            lt = s->time;
            printf("dyno: t %.1f heading %.2f deg wz %.3f pos (%.2f %.2f %.2f) off %.3f m%c", s->time,
                   atan2f(b->R[1], b->R[0]) * 57.29578f, b->w[2], b->pos[0], b->pos[1], b->pos[2],
                   sqrtf((s->dy_spot[0] - b->pos[0]) * (s->dy_spot[0] - b->pos[0]) +
                         (s->dy_spot[1] - b->pos[1]) * (s->dy_spot[1] - b->pos[1])), 10);
        }
        if (!nohold) { b->L[2] = 0; b->w[2] = 0; }
    }
#else
    b->L[2] = 0; b->w[2] = 0;
#endif
    float sp = s->dy_vel[0] * b->R[0] + s->dy_vel[1] * b->R[1] + s->dy_vel[2] * b->R[2];
    if (fabsf(sp) < 0.03f) sp = 0;
    for (int i = 0; i < 4; i++) {
        int driven = i < 2 ? front_driven(&s->d) : rear_driven(&s->d);   /* CarState::IsDriveWheel */
        if (!driven && W_R(&s->wh[i]) > 0) s->wh[i].w = sp / W_R(&s->wh[i]);
    }
}
/* on: 0 = off (SetDynoMode(0)), else on, with the DynoManager's state (1..4) */
void n2sim_set_dyno(N2Sim *s, int on)                                    /* 0x599200 */
{
    if (!s) return;
    if (on && !s->dyno) {
        memcpy(s->dy_spot, s->b.pos, sizeof s->dy_spot);
        memcpy(s->dy_pos, s->b.pos, sizeof s->dy_pos);
        memset(s->dy_vel, 0, sizeof s->dy_vel);
    }
    s->dyno = on != 0;
    s->dyno_state = on;
}
int n2sim_dyno(const N2Sim *s) { return s && s->dyno; }
static void force_pass(N2Sim *s)                                         /* 0x5b0520 */
{
    Body *b = &s->b;
    /* 0x5b052d..0x5b0554 (GC ResolveForces 801c1398): on the dyno (+0x7c) while the car's
       DynoManager (car +0x1c -> RacingCar +0x1e0, state +4) is in state 2 (full throttle,
       recording), engine +0x80 = 1.5 (0x7a1a20). drive_post set it this substep already */
    if (s->dyno && s->dyno_state == 2) s->e.thrmul = 1.5f;
    aero_force(s);
    for (int i = 0; i < 4; i++) wheel_contact(s->su[i].wheel, s->su[i].contact, s->su[i].force);
    tyre_solve(s);
    for (int i = 0; i < 4; i++) {
        Wheel *w = s->su[i].wheel;
        if (!s->su[i].contact) continue;
        float arm[3] = { w->cpt[0] - b->pos[0], w->cpt[1] - b->pos[1], w->cpt[2] - b->pos[2] };
        body_force(b, w->force, arm);
    }
    float wl = len3(b->w);                                   /* 0x5a5b60 */
    if (s->cs_wheels < 2 && wl < 0.5f) {
        float k = -(graph(LOWSPIN_G, 3, wl) * b->mass);
        b->T[0] += b->w[0] * k; b->T[1] += b->w[1] * k; b->T[2] += b->w[2] * k;
    }
    if (s->driftlike) drift_assist(s);
    hydraulics_phys(s);                                      /* 0x5a51b0 */
    if (s->dyno) dyno_forces(s);                             /* GC 801c1508 */
}

/* ---- rest detection 0x5992b0 / 0x5a5e20 ----------------------------------- */
static int rest_check_c(N2Sim *s, const int *contact, float thr, int gear)  /* 0x5992b0 */
{
    Body *b = &s->b;
    /* never at rest while the hydraulics bounce (0x5ed290: any bounce > 0)
       or lift / drop a wheel (0x5ed470: codes 2 / 3, the full model only) */
    for (int i = 0; i < 4; i++) if (s->hyd_bounce[i] > 0) return 0;
    if (s->mover == N2_MOVER_PHYSICS)
        for (int i = 0; i < 4; i++) if (s->hyd_code[i] == 2 || s->hyd_code[i] == 3) return 0;
    for (int i = 0; i < 4; i++) if (!contact[i]) return 0;
    if (thr <= 0.3f || gear == 1 || s->cs_wheels < 1) {
        float v = len3(b->v), w = len3(b->w);
        if (v <= 0.7f && w <= 0.7f &&
            ((fabsf(b->R[2]) <= 0.15f && fabsf(b->R[6]) <= 0.15f) || s->cs_wheels < 1) &&
            (s->cs_wheels > 0 || s->rest == 2))
            return v <= 0.25f && w <= 0.25f ? 2 : 1;
    }
    return 0;
}
static int rest_check(N2Sim *s)
{
    int c[4] = { s->wh[0].contact, s->wh[1].contact, s->wh[2].contact, s->wh[3].contact };
    return rest_check_c(s, c, s->eff.throttle, s->d.gear);
}
static int rest_apply(N2Sim *s)
{
    Body *b = &s->b;
    int r = s->rest = rest_check(s);
    if (r == 0 || r == 1) {
        if (r == 1) {
            for (int i = 0; i < 3; i++) { b->p[i] *= 0.95f; b->L[i] *= 0.95f; }
            for (int i = 0; i < 4; i++) s->wh[i].w *= 0.95f;
        }
        if (len3(b->v) > 0.8f) s->rest_t = 0;
        b->gravity = s->dampen == 1 ? 11.0f : 9.8f;           /* car +0x38 == 1 (crash): 0x7fc084 */
        return r;
    }
    memset(b->p, 0, 12); memset(b->L, 0, 12);
    if (s->cs_wheels > 0) for (int i = 0; i < 4; i++) s->wh[i].w = 0;
    b->gravity = 0;
    return 1;
}

/* ---- one substep 0x5aa480 ------------------------------------------------- */
static void post_all(N2Sim *s)                                           /* 0x5a0d80 */
{
    body_post(&s->b);
    for (int i = 0; i < 4; i++) { wheel_post(&s->wh[i]); susp_post(s, &s->su[i]); }
    engine_post(s);
    drive_post(s);
}
static void post_hook(N2Sim *s)                                          /* 0x5b04d0 */
{
    post_all(s);
    int again = rest_apply(s);                               /* 0x5abb00 */
    if (!again)
        for (int i = 0; i < 4; i++)
            if (s->su[i].x == s->su[i].rec[5]) { s->cs_bottom[i] += susp_bumpstop(s, &s->su[i]); again = 1; }
    if (again) { s->h = 0; post_all(s); }
    force_pass(s);
}
static void substep(N2Sim *s, float h)
{
    float db[32], dw[4][2], de, dd;
    body_deriv(&s->b, db);
    for (int i = 0; i < 4; i++) wheel_deriv(s, &s->wh[i], dw[i]);
    de = (engine_torque(s) + s->e.load) / s->e.rec[3];                 /* 0x5a1290 */
    {   Drive *d = &s->d;                                               /* 0x5949b0 */
        float r = d->gb[7] * d->rate;
        if (0 < d->q) r *= d->q + 1.0f;
        else if (d->q < 0 && 2 < d->target) r *= 0.35f;                 /* 0x7a18d4 */
        dd = r * CLUTCH_RATE[d->state];
        if (d->mode == 3) dd = 0; }                                     /* the pedal sets it (0x594910) */
    float *st = s->b.pos;
    for (int i = 0; i < 32; i++) st[i] += h * db[i];
    for (int i = 0; i < 4; i++) { s->wh[i].angle += h * dw[i][0]; s->wh[i].w += h * dw[i][1]; }
    s->e.w += h * de;
    s->d.engage += h * dd;
    post_hook(s);
}

/* ---- per-frame setup ---------------------------------------------------- */
static void steer_geometry(N2Sim *s, unsigned short ang)                /* 0x59def0 */
{
    float L = PF(0x10) - PF(0xa0);
    float a = (float)ang * U16RAD, b;
    if (a > PI_F) a -= 2*PI_F;
    int neg = a < 0;
    if (neg) a = -a;
    b = (L * a) / (a * (PF(0x14) - PF(0x44)) + L);
    if (neg) { float t = -a; a = -b; b = t; }
    float *m = s->steer_m[0];
    ident(m); m[0] = sin16(a + 1.57079637f); m[4] = -sin16(a); m[1] = sin16(a); m[5] = m[0];
    m = s->steer_m[1];
    ident(m); m[0] = sin16(b + 1.57079637f); m[4] = -sin16(b); m[1] = sin16(b); m[5] = m[0];
}
/* 0x5aa960, the clutch-pedal box (drivetrain +0x60 == 3) asked for a gear
   it is not in: with the clutch down (state 1) the shift starts and goes in
   when the pedal comes up (0x5a1400); with it up the box refuses: 0.45 s
   throttle cut (+0x5c, "Missed Shift!") and the request falls back to the
   gear (the return value goes back to the controller, vt+0x28) */
static void drive_pedal_shift(N2Sim *s, int req)
{
    Drive *d = &s->d;
    int g = d->gear;
    if (d->state != 1) { d->shifting = 0; d->shift_t = (float)s->time; s->gear_req = g; return; }
    if (d->shifting) return;
    d->shifting = 1; d->target = req;
    if (req > g && g >= 1 && s->drag) d->q = shift_grade(s, d->disrpm, g, 1);   /* 0x5a1700(+0x6c) */
}
static void frame_setup(N2Sim *s, float dt)
{
    Body *b = &s->b; Aero *a = &s->a;
    Drive *d = &s->d;
    int req = s->gear_req;             /* controller +0x1e4, set by n2sim_input */
    /* 0x5abbf0: outside neutral, a pedal under half loses to the other */
    if (d->gear != 1) {
        N2Input *c = &s->eff;
        if (c->throttle <= c->brake || c->throttle <= c->handbrake) { if (c->throttle < 0.5f) c->throttle = 0; }
        else { if (c->brake < 0.5f) c->brake = 0; if (c->handbrake < 0.5f) c->handbrake = 0; }
    }
    /* 0x5ab990: static wheel loads carried by the suspension preload */
    body_mass(s);
    float xf = PF(0x10) - b->com[0], xr = PF(0xa0) - b->com[0];
    float rear = (xf / (xf - xr)) * 0.5f, spd = s->cs_speed > 0 ? s->cs_speed : 0;
    float tot = (a->rec[2] + a->rec[1]) * a->on * spd * PF(0x26c) + b->mass * 9.8f;
    for (int i = 0; i < 4; i++) {
        susp_preload(&s->su[i], tot * (i < 2 ? 0.5f - rear : rear), b);
        s->wh[i].I = 0.01f;                                  /* 0x7fc6f0 */
    }
    /* 0x5a49a0: normal driving mode. While the race runs (state 4) a drag
       race keeps the engine's over-rev heat on, any other race off */
    if (s->race_state == 4) s->e.heat = s->drag ? (s->e.heat < 0 ? 0 : s->e.heat) : -1.0f;
    float sd = 1.0f;
    if (s->cs_wheels > 0) {
        sd = 0;
        for (int i = 0; i < 4; i++) if (s->cs_contact[i]) sd += SURF(s->cs_surf[i])->drag;
        sd /= (float)s->cs_wheels;
    }
    int yawI = s->cs_wheels != 0;
    if (s->dampen == 1) {
        /* crash mode (car +0x38 == 1, CheckForWildCollision): no damping,
           gravity 11 (0x7fc084), no aero but full surface drag, no yaw
           inertia hold, the throttle held to 0.4 (0x5eb910) */
        b->damp_on = b->angdrag_on = 0; b->gravity = 11.0f;
        a->on = 0; a->surfdrag = 1.0f;
        if (s->eff.throttle > 0.4f) s->eff.throttle = 0.4f;
        yawI = 0;
    } else {
        b->damp_on = b->angdrag_on = 1; b->gravity = 9.8f;
        a->on = 1; a->surfdrag = sd;
    }
    /* in-game (*0x890118 == 4): no settle damping under power or on a slope */
    if (s->eff.throttle > 0.3f || (s->cs_wheels == 4 && fabsf(b->R[2]) > 0.15f)) b->damp_on = 0;
    if (b->yawI_on != yawI) { if (!yawI) b->L[2] /= PF(0x160); b->yawI_on = yawI; }
    /* 0x599170 with car +0x468, the AI catch-up (1 for the player): engine,
       body top speed, aero speed scale and every wheel's grip */
    s->e.power = b->power = a->speedscale = s->catchup; a->scale = 1.0f - s->draft;
    for (int i = 0; i < 4; i++) s->wh[i].power = s->catchup;
    b->yaw_clamp = PF(0x288);
    b->top_speed = PF(0x290);
    if (PF(0x1c0) > 0.8f) s->d.rear = PF(0x1c0);
    /* 0x5abbf0: controller -> engine, steering, gearbox */
    s->nos_stage = nos_stage(s);
    float thr = s->eff.throttle;
    if (thr <= 0.15f) thr = 0;
    else if (thr > 0 && s->d.gear > 1 && s->e.w * RPM < 3000.0f && thr < 0.5f) thr = 0.5f;
    steer_geometry(s, s->steer_u16);
    s->e.thr = thr;
    if (req < 0) drive_auto(s, s->cs_tach);
    else if (d->mode == 3 && req != d->gear) drive_pedal_shift(s, req);  /* 0x5aa960, clutch pedal */
    else if (req != d->gear && req != d->target) {                     /* 0x5aa960 manual path */
        int g = d->gear;
        d->target = req;
        if (req > g && g >= 1 && s->drag) d->q = shift_grade(s, s->cs_tach, g, 1);   /* 0x5a1700 */
    }
    a->thr = thr; a->ground = (float)s->cs_wheels * 0.25f;
    /* 0x5a4d80: brakes, handbrake, rear grip assist */
    /* x0.85 while car +0x464 (the player's assist byte, 0 for the AI) is
       clear, outside drift-type races */
    float grip = s->gripscale * (s->driftlike ? 1.0f : 0.85f), mph = s->cs_speed * MPH;
    grip *= n2_rain_grip(s);   /* x (1 - 0.025 Car::GetRainIntensity 0x5ed5d0) */
    const N2Input *in = &s->eff;
    if (in->handbrake < 0.5f) { s->hbramp += dt; if (s->hbramp > 1) s->hbramp = 1; }
    else { s->hbramp -= 20.0f * dt; if (s->hbramp < 0.5f) s->hbramp = 0.5f; }
    for (int i = 0; i < 4; i++) {
        Wheel *w = &s->wh[i];
        if (in->throttle <= 0.8f || in->brake <= 0.5f || mph >= 10.0f || i < 2) {
            float bias = PF(0x27c);
            if (i < 2) bias = 1.0f - bias;
            w->brake = (bias + bias) * in->brake;
        } else w->brake = mph * 0.05f;
        float hb = HB_WHEEL[i] * in->handbrake;
        if (hb > 0.2f) hb += 0.5f;
        w->hbrake = hb;
        float g = grip;
        if (i > 1 && !s->driftlike) {                        /* 0x5990c0 */
            float slipang = 0;
            if (s->cs_speed >= 1.0f) {                       /* 0x591d90 */
                unsigned short va = fast_atan(s->cs_v[0], s->cs_v[1]);
                slipang = (float)(unsigned short)(s->cs_head - va) * U16RAD;
                if (PI_F < slipang) slipang -= PI_F + PI_F;
            }
            float k = fabsf(b->R[2]) + 1.0f, sa = fabsf(slipang);
            float r = k;
            if (!(w->hbrake > 0.5f && sa < 20.0f * 0.0174532942f)) {
                float f = (PF(0x280) + PF(0x284)) * (sa * (s->cs_absv / 30.0f));
                if (f > 0.35f) f = 0.35f;
                r = f + k;
            }
            g = ((r - 1.0f) * s->hbramp + 1.0f) * grip;
        }
        if (s->nos_stage > 0) g *= NOS_GRIP[s->nos_stage];   /* 0x5a50ff */
        /* 0x5a5114: on the dyno, at full throttle (controller +0x20c == 1), the
           wheels that are not driven (rear bias P+0x1c0 > 0.8: the fronts,
           < 0.2: the rears) lose their grip */
        if (s->dyno && in->throttle == 1.0f) {
            float rb = PF(0x1c0);
            if ((rb < 0.2f && i >= 2) || (rb > 0.8f && i < 2)) g = 0;
        }
        w->grip = g;
        w->wobble = 0;
    }
}

static void curve_init(Curve *c, int n, float x0, float x1, const float *d)
{
    c->n = n; c->x0 = x0; c->x1 = x1; c->inv = (float)(n - 1) / (x1 - x0); c->d = d;
}
/* 0x5ac7d0: shift points from the gearbox (gb = P+0x1b0) and engine (er =
   P+0x1f0) records; up[g] leaves gear g, down[g] drops out of it */
static void shift_points(const float *gb, const float *er, float *up, float *down)
{
    int n = *(const int *)&gb[6];
    if (n < 1 || n > 7) n = 6;
    up[1] = (er[1] - er[0]) * gb[0] + er[0];
    for (int g = 2; g <= n; g++) {
        up[g] = er[1] - 100.0f;                  /* 0x86b33c optimal-shift search is off */
        float cur = gb[8 + g], nx = gb[8 + g + 1];
        down[g + 1] = fabsf(cur) <= 1e-5f ? 0 : (nx / cur) * up[g];
    }
    up[n + 1] = er[2] + 500.0f;
}

/* ---- Mellovator (mover 3) -------------------------------------------------
   The original's cheaper model for AI racers (mover ctor 0x5b6d00, frame
   0x5aad90, component "Melov8r" 0x5b0030): the same rigid body and
   aerodynamics, but instead of wheels, suspensions, engine and drivetrain
   each wheel is a spring from the probe height, a linear tyre clipped to a
   friction circle and one spin state, the engine speed follows the driven
   wheels and the gearbox shifts on fixed rpm. Substeps of at most 1/70 s. */
static const float MELLO_SPINDRAG[6] = { 0,0.05f, 2,0.322f, 20,0.5f };   /* 0x7fc8c8 -> 0x86b578 */
static const float MELLO_ANGDAMP[6] = { 0,0, 0.1f,0, 2,20 };             /* 0x7fc8e0 -> 0x86b498 */
static const float DRIVE_TUNE4[3] = { 1.0f, 1.2f, 1.1f };              /* 0x7fc2b8 + 0x144 k */
#define MELLO_HMAX 0.0142857144f          /* 0x7fc040 */
#define MELLO_CAP  4.0f                /* 0x7a1970 */
#define MELLO_CAP_SX 2.0f              /* 0x7a1974, street X (0x5a38e0) */
#define MELLO_CAP_URL 3.2f             /* 0x7a1978, track flag 0x2000 (0x5a38e0) */

static float mello_ratio(const N2Sim *s, int g) { return PF(0x1d0 + g * 4); }

static void mello_loads(N2Sim *s, float k)                               /* 0x5989f0 */
{
    Mello *m = &s->m; Body *b = &s->b;
    float xf = PF(0x10) - b->com[0];
    float f = (xf / (xf - (PF(0xa0) - b->com[0]))) * 0.5f;
    float t = k * PF(0x26c) + b->mass * 9.8f;
    m->sload[0] = m->sload[1] = t * (0.5f - f);
    m->sload[2] = m->sload[3] = f * t;
}
/* 0x598d50 + 0x5a3700: probe under each mount, spring and damper */
static void mello_susp(N2Sim *s)
{
    Mello *m = &s->m; Body *b = &s->b;
    for (int i = 0; i < 4; i++) {
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1],
                       wr[2] - b->com[2] - m->hyd[i] };              /* - the lift +0x1e8 */
        float *mp = m->mount[i], gz, n[3]; int surf;
        vmul(mp, l, b->R);
        mp[0] += b->pos[0]; mp[1] += b->pos[1]; mp[2] += b->pos[2];
        int ok = ground_at(s, i, mp, &gz, n, &surf);
        if (!ok) gz = mp[2] - 1000.0f;
        memcpy(m->n[i], n, 12); m->surf[i] = surf;
        float up = n[0]*b->R[8] + n[1]*b->R[9] + n[2]*b->R[10];
        float x = up < 0.001f ? -100.0f : (gz - mp[2]) / up;
        x += wr[4];
        const float *k = s->P + (0x170 + 0x20 * (i / 2)) / 4;
        float x0 = m->comp[i];
        if (x < k[6]) { m->comp[i] = k[6]; m->contact[i] = 0; m->over[i] = 0; }
        else {
            if (x <= k[5]) { m->comp[i] = x; m->over[i] = 0; }
            else { m->comp[i] = k[5]; m->over[i] = x - k[5]; }
            m->contact[i] = 1;
        }
        float h = s->h > 0.0001f ? s->h : 0.0001f;
        float v = m->compv[i] = (m->comp[i] - x0) / h;
        float f = 0;
        if (m->contact[i]) {
            float c = m->comp[i];
            f = ((c > 0 ? c : 0) * k[0] + 1.0f) * k[1] * 0.9f * c + 0.9f * k[v <= 0 ? 3 : 2] * v + m->sload[i];
            if (f < 0) f = 0;
        }
        m->load[i] = f;
    }
}
static void mello_post(N2Sim *s)                                         /* 0x5ab190 */
{
    Mello *m = &s->m;
    mello_susp(s);
    for (int i = 0; i < 4; i++) {
        if (m->brake > 0.95f && fabsf(m->spin[i]) < 30.0f) { m->locked[i] = 1; m->spin[i] = 0; }
        if (m->brake < 0.2f) m->locked[i] = 0;
    }
    float frac = PF(0x1c0), w;
    float fw = (m->spin[1] + m->spin[0]) * PF(0x1b8) * 0.5f * RPM;
    float rw = (m->spin[3] + m->spin[2]) * PF(0x1bc) * 0.5f * RPM;
    if (frac < 0.2f) w = fw;
    else if (frac <= 0.8f) w = (rw + fw) * 0.5f;
    else w = rw;
    float r = w * mello_ratio(s, m->gear);
    int ng = *(const int *)&s->P[0x1c8 / 4];
    if (m->req == -1) {
        if (m->gear < 2) m->gear = 2;
        if (m->gear - 1 < ng) {
            int g;
            do {
                g = m->gear;
                if (r <= m->up[g]) break;
                m->gear = g + 1;
                r = w * mello_ratio(s, g + 1);
            } while (g < ng);
        }
        while (m->gear - 1 > 1 && r < m->down[m->gear]) {
            m->gear--;
            r = w * mello_ratio(s, m->gear);
        }
    } else if (m->req == 0) {
        m->gear = 0;
        r = w * mello_ratio(s, 0);
    }
    if (r < PF(0x1f0)) m->rpm = PF(0x1f0);
    else if (PF(0x1f8) < r) m->rpm = PF(0x1f8);
    else m->rpm = r;
}
static void mello_spin_deriv(N2Sim *s, float *d)                         /* 0x598ac0 */
{
    Mello *m = &s->m; Body *b = &s->b;
    for (int i = 0; i < 4; i++) {
        float frac = PF(0x1c0);
        if (i < 2) frac = 1.0f - frac;
        if (m->locked[i]) { d[i] = 0; continue; }
        float x = m->rpm / m->mult;
        float t = curve(&m->torque, x) * m->thr - (1.0f - m->thr) * curve(&m->ebrake, x);
        /* nitrous: + curve(nos, x) * P+0x2ac * 0x7a1798[+0x198], but +0x198 is
           only ever zeroed (ctor 0x5b0030), so a Mellovator car gets none */
        if (m->thr < 0.05f && m->rpm == PF(0x1f0)) t = 0;
        float fd = PF(i < 2 ? 0x1bc : 0x1b8);      /* front reads +0x1bc, as the original */
        float sp = m->spin[i];
        if (sp < -20.0f) sp = -20.0f; else if (sp > 20.0f) sp = 20.0f;
        float bt = 2.5f * m->brake * sp * -0.2f;
        float a = 1.0f, c = 1.25f;
        if (m->mult > 1.0f) { float e = m->mult - 1.0f; a = 3.0f * e + 1.0f; c = (e * 2.5f + 1.0f) * 1.25f; }
        float dr = graph(MELLO_SPINDRAG, 3, len3(b->v));
        if (frac > 0) dr /= c * a;
        d[i] = ((a * (fd * mello_ratio(s, m->gear) * t * frac * 0.5f) + bt) - dr * m->tf[i][0]) / 0.008f;
    }
}
static void mello_forces(N2Sim *s)                                       /* 0x5a38e0 */
{
    Mello *m = &s->m; Body *b = &s->b;
    float head = (float)fast_atan(b->R[0], b->R[1]) * U16RAD;   /* 0x5a3a1f */
    float steer = (float)m->steer * U16RAD;
    float vabs = len3(b->v);
    for (int i = 0; i < 4; i++) {
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1], wr[2] - b->com[2] }, r[3];
        vmul(r, l, b->R);
        float F[3] = { 0, 0, m->load[i] };
        m->sliding[i] = 0;
        if (m->contact[i]) {
            float vx = b->v[0] - r[1] * b->w[2], vy = r[0] * b->w[2] + b->v[1];
            float h = head + (i < 2 ? steer : 0);
            float c = sin16(h + 1.57079637f), sn = sin16(h);
            float vlat = c * vy - sn * vx;
            float vlong = (c * vx + sn * vy) - wr[4] * m->spin[i];
            m->sv[i][0] = vlong; m->sv[i][1] = vlat;
            float mag = sqrtf(vlat * vlat + vlong * vlong), k = 1.0f;
            if (0.05f * m->gripm < mag) k = m->perfk * m->gripm;
            /* n.z < 0.5 scales by 0x7a19b8 = 1; the +0x1e4 grip-cut timer is never set */
            if (mag > 6.0f) m->sliding[i] = 1;
            float fx = -(k * m->load[i] * vlong * 1.25f), fy = -(k * m->load[i] * vlat * 1.25f);
            float a = i < 2 ? steer : 0;
            float c2 = sin16(a + 1.57079637f), s2 = sin16(a);
            F[0] = fx * c2 - fy * s2;
            F[1] = fy * c2 + fx * s2;
        }
        m->tf[i][0] = F[0]; m->tf[i][1] = F[1];
        float W[3];
        vmul(W, F, b->R);
        if (m->contact[i]) {
            /* 0x5a38e0: 4, 2 in street X, 3.2 (0x7a1978) when the loaded
               track's record (*0x883dac, GLOBALB 0x34201) has flag 0x2000 at
               +0x94 -- the URL speedways (L4RG 4701-4716), in any race type */
            float cmax = s->streetx ? MELLO_CAP_SX : MELLO_CAP;
            if (s->trkflags & 0x2000) cmax = MELLO_CAP_URL;
            float pm = sqrtf(W[0] * W[0] + W[1] * W[1]),
                  cap = cmax * m->load[i] * m->gripm;
            if (cap < pm) { W[0] *= cap / pm; W[1] *= cap / pm; }
            float e = m->mult - 1.0f, f = e > 0 ? e * 2.5f + 1.0f : 1.0f;
            W[0] *= f; W[1] *= f;
            if (m->locked[i] || (m->brake > 0.4f && vabs < 0.1f)) {
                float q = m->load[i] / b->R[10];           /* hold on a slope */
                W[0] -= q * b->R[8]; W[1] -= q * b->R[9];
            } else if (pm < cap) {
                float q = m->load[i] / b->R[10];
                W[0] -= fabsf(b->R[4]) * b->R[8] * q; W[1] -= fabsf(b->R[5]) * b->R[9] * q;
            }
        }
        body_force(b, W, r);
        float tm = sqrtf(m->tf[i][0] * m->tf[i][0] + m->tf[i][1] * m->tf[i][1]);
        if (tm > 50.0f) { m->tf[i][0] *= 50.0f / tm; m->tf[i][1] *= 50.0f / tm; }
    }
}
static float mello_penetration(N2Sim *s)                                 /* 0x5a3e80 */
{
    Mello *m = &s->m; Body *b = &s->b;
    float sum = 0;
    for (int i = 0; i < 4; i++) {
        if (!(m->over[i] > 0)) continue;
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1], wr[2] - b->com[2] + m->comp[i] }, r[3], v[3];
        vmul(r, l, b->R);
        cross(v, b->w, r);
        v[0] += b->v[0]; v[1] += b->v[1]; v[2] += b->v[2];
        float k = -(m->n[i][0] * v[0] + m->n[i][1] * v[1] + m->n[i][2] * v[2]);
        if (k > 0) {
            k = 0.2f * k * b->mass;
            float j[3] = { m->n[i][0] * k, m->n[i][1] * k, m->n[i][2] * k };
            body_impulse(b, j, r);
            sum += k;
        }
    }
    return sum;
}
static void mello_post_all(N2Sim *s) { body_post(&s->b); mello_post(s); }   /* 0x5a0d80 */
static int mello_rest(N2Sim *s)                                          /* 0x5a5e20, no wheels */
{
    Mello *m = &s->m; Body *b = &s->b;
    int r = m->rest = rest_check_c(s, m->contact, m->thr, m->gear);
    if (r == 2) {
        memset(b->p, 0, 12); memset(b->L, 0, 12);
        b->gravity = 0;
        return 1;
    }
    if (r == 1) for (int i = 0; i < 3; i++) { b->p[i] *= 0.95f; b->L[i] *= 0.95f; }
    if (len3(b->v) > 0.8f) m->rest_t = 0;
    b->gravity = 9.8f;
    return r;
}
/* 0x5a2fd0, in every force pass (skipped outright while car +0x3c -> +0x18
   > 0 (jg 0x5a3373): the car body's live Collisions, s->body_colls). A pair of bouncing wheels (front: FL FR, rear: RR RL) first
   has the other pair lifted to 0.2 m; each wheel's lift follows extend x
   0.2 at 0.5 m/s up (capped at the target) and 0.5 m/s down (to 0). Once
   the lifted pair is up and no bouncing wheel hangs (compression < -0.01),
   every bouncing wheel whose mount is not moving into the ground gets an
   impulse (0.9 - 0.3 v.n) x bounce x mass along its ground normal. */
static void hydraulics(N2Sim *s)
{
    Mello *m = &s->m; Body *b = &s->b;
    const float *bo = s->hyd_bounce;
    int mask = 0;
    if (s->body_colls > 0) return;
    if (bo[0] != 0 && bo[1] != 0) mask = 0xc;
    else if (bo[2] != 0 && bo[3] != 0) mask = 3;
    else if ((bo[2] != 0 && bo[1] != 0) || (bo[3] != 0 && bo[0] != 0)) mask = 0x20;
    int ready = mask != 0;
    float rate = s->h * 0.5f;                                 /* 0x7fc800 x 0x7fc098 */
    for (int i = 0; i < 4; i++) {
        float t = s->hyd_extend[i] * 0.2f, *h = &s->hyd_h[i];
        int lift = (1 << i) & mask;
        s->hyd_code[i] = 0;
        if (lift && !(t > 0.2f)) t = 0.2f;
        if (t > *h) {
            float u = *h + rate;
            *h = t < u ? t : u;
            s->hyd_code[i] = 2;
        } else if (t < *h) {
            float u = *h - rate;
            *h = u < 0.0f ? 0.0f : u;
            s->hyd_code[i] = 3;
        } else s->hyd_code[i] = 1;
        if (lift && *h < 0.2f) ready = 0;
    }
    for (int i = 0; i < 4; i++)
        if (bo[i] > 0 && m->comp[i] < -0.01f) return;
    if (!ready) return;
    for (int i = 0; i < 4; i++) {
        if (!(bo[i] > 0)) continue;
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1], wr[2] - b->com[2] + m->comp[i] }, r[3];
        vmul(r, l, b->R);                                     /* 0x43c9c0 */
        float pv[3];                                          /* 0x59e150 */
        cross(pv, b->w, r);
        pv[0] += b->v[0]; pv[1] += b->v[1]; pv[2] += b->v[2];
        const float *n = m->n[i];
        float d = pv[0] * n[0] + pv[1] * n[1] + pv[2] * n[2];
        if (d < 0.0f) continue;
        float f = (0.9f - d * 0.3f) * bo[i];
        if (!(f > 0.0f)) continue;
        f *= b->mass;
        float J[3] = { f * n[0], f * n[1], f * n[2] };
        body_impulse(b, J, r);                                /* 0x59bc60 */
        s->hyd_hit[i] = 1;
    }
}
/* 0x5ab130's damping gate: 0x5a3410 is skipped while the car's control
   holds any bounce (0x5ed290) or extend (0x5ed300; that one also tests car
   +0x3f8..+0x404, which nothing ported sets) */
static int hydraulics_held(const N2Sim *s)
{
    for (int i = 0; i < 4; i++) if (s->hyd_bounce[i] > 0 || s->hyd_extend[i] > 0) return 1;
    return 0;
}
static void mello_force_pass(N2Sim *s)                                   /* 0x5ab130 */
{
    Body *b = &s->b;
    mello_forces(s);
    aero_force(s);
    hydraulics(s);
    /* 0x5a3410 low-wheel damping */
    if (!hydraulics_held(s) && s->cs_wheels < 2) {
        float k = -(graph(MELLO_ANGDAMP, 3, len3(b->w)) * b->mass);
        b->T[0] += b->w[0] * k; b->T[1] += b->w[1] * k; b->T[2] += b->w[2] * k;
    }
    /* (its no-wheels heading alignment scales by (dot - 1) <= 0 and so never
       applies a torque) */
}
static void mello_post_hook(N2Sim *s)                                    /* 0x5ab0c0 */
{
    mello_post_all(s);
    int r = mello_rest(s);
    float imp = mello_penetration(s);
    if (r || imp > 0) mello_post_all(s);
    if (s->m.rest == 2) s->m.brake = 1.0f;
    mello_force_pass(s);
}
static void mello_substep(N2Sim *s, float h)                             /* 0x5aa480 */
{
    float db[32], ds[4];
    body_deriv(&s->b, db);
    mello_spin_deriv(s, ds);
    float *st = s->b.pos;
    for (int i = 0; i < 32; i++) st[i] += h * db[i];
    for (int i = 0; i < 4; i++) s->m.spin[i] += h * ds[i];
    mello_post_hook(s);
}
static void mello_controls(N2Sim *s)
{
    Mello *m = &s->m;
    m->steer = s->steer_u16;
    m->thr = s->eff.throttle;
    m->req = s->gear_req;
    float hb = s->eff.handbrake * 0.5f;
    m->brake = s->eff.brake > hb ? s->eff.brake : hb;
}
static void mello_frame(N2Sim *s, float dt)                              /* 0x5aad90 */
{
    Mello *m = &s->m; Body *b = &s->b; Aero *a = &s->a;
    float sd = 1.0f;
    if (s->cs_wheels > 0) {
        sd = 0;
        for (int i = 0; i < 4; i++) if (s->cs_contact[i]) sd += SURF(s->cs_surf[i])->drag;
        sd /= (float)s->cs_wheels;
    }
    int yawI = s->cs_wheels != 0;
    if (s->dampen == 1) {                /* owner +0x38 == 1, crash mode */
        b->damp_on = b->angdrag_on = 0; b->gravity = 11.0f;
        a->on = (float)s->cs_wheels * 0.25f; a->surfdrag = 1.0f;
        yawI = 0;
    } else {
        b->damp_on = b->angdrag_on = 1; b->gravity = 9.8f;
        a->on = 1; a->surfdrag = sd;
    }
    if (b->yawI_on != yawI) { if (!yawI) b->L[2] /= PF(0x160); b->yawI_on = yawI; }
    memcpy(b->com, s->P, 12);                                /* 0x5a29a0 */
    memcpy(s->m.hyd, s->hyd_h, sizeof s->m.hyd);             /* car +0x3b8 -> +0x1e8 */
    body_mass(s);
    b->yaw_clamp = 0;                                        /* never set on this body */
    b->top_speed = PF(0x290);
    mello_loads(s, (a->rec[2] + a->rec[1]) * a->on * (s->cs_speed > 0 ? s->cs_speed : 0));
    mello_controls(s);
    s->nos_stage = nos_stage(s);
    a->thr = m->thr; a->ground = (float)s->cs_wheels * 0.25f;
    /* 0x5a3380: catch-up (0x7fc0a0 = 1) */
    a->speedscale = s->catchup; a->scale = 1.0f - s->draft;  /* 1 - car +0x4dc */
    b->power = s->catchup;
    m->gripm = s->catchup;                                   /* x 0x7a1990 = 1 above 30 mph */
    m->mult = s->catchup;
    /* x (1 - 0.025 rain): Car::GetRainIntensity 0x5ed5d0 (= GetDesiredRainIntensity at car +0x60 / +0x64) */
    m->gripm *= n2_rain_grip(s);
    int n = (int)(dt / MELLO_HMAX) + 1;
    float h = dt / (float)n;
    for (int k = 0; k < n; k++) {
        s->h = h;
        s->time += h;
        mello_substep(s, h);
        orthonormalize(b->R);
    }
    /* 0x5a2b60: publish to the car state */
    s->cs_absv = len3(b->v);
    s->cs_speed = b->v[0]*b->R[0] + b->v[1]*b->R[1] + b->v[2]*b->R[2];
    int nw = 0;
    for (int i = 0; i < 4; i++) {
        m->angle[i] += m->spin[i] * dt;
        s->cs_contact[i] = m->contact[i];
        /* 0x59cb90: a drag or street X race reads every surface as ROAD (1) */
        s->cs_surf[i] = s->drag || s->streetx ? 1 : m->surf[i];
        nw += m->contact[i] != 0;
    }
    s->cs_wheels = nw;
    s->cs_tach = m->rpm;
}
static void body_from_state(N2Sim *s);
/* ---- NISMover (mover 5) ---------------------------------------------------
   ctor 0x5b6fc0 "NISMvr": the Mellovator's body (0x5a7f30), aerodynamics
   and chassis (0x5b0030), the chassis with its own vtable 0x7a25b0 --
   forces 0x5ab540 (the wheels hold the car up, no tyre force), no wheel
   spin derivative (0x40d080), post 0x5ab880 (the wheels roll with the
   body). The mover's post hook is 0x5a4410 (no rest check, no penetration
   pass) and its frame 0x5ab3c0 runs the driver 0x5a44a0, which sets the
   body's velocity toward the scene's target. */
static void nis_forces(N2Sim *s)                                         /* 0x5ab540 */
{
    Mello *m = &s->m; Body *b = &s->b;
    float head = (float)(unsigned short)(int)lrintf(atan2f(b->R[1], b->R[0]) * RAD2U16) * U16RAD;
    float steer = (float)m->steer * U16RAD;
    for (int i = 0; i < 4; i++) {
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1], wr[2] - b->com[2] }, r[3];
        vmul(r, l, b->R);
        m->sliding[i] = 0;
        if (m->contact[i]) {
            /* the slip speeds and the sliding flag as the Mellovator works
               them out (0x5a38e0); the force from them is thrown away */
            float vx = b->v[0] - r[1] * b->w[2], vy = r[0] * b->w[2] + b->v[1];
            float h = head + (i < 2 ? steer : 0);
            float c = sin16(h + 1.57079637f), sn = sin16(h);
            float vlat = c * vy - sn * vx;
            float vlong = (c * vx + sn * vy) - wr[4] * m->spin[i];
            m->sv[i][0] = vlong; m->sv[i][1] = vlat;
            if (sqrtf(vlat * vlat + vlong * vlong) > 6.0f) m->sliding[i] = 1;
        }
        m->tf[i][0] = m->tf[i][1] = 0;
        float F[3] = { 0, 0, m->load[i] }, W[3];
        vmul(W, F, b->R);
        body_force(b, W, r);
    }
    /* 0x5a46a0: + (+0x258 x forward + +0x25c x left) on the body's torque
       accumulator, x and y only (plus the zero vector 0x86b390) */
    b->T[0] += s->nis_tq[0] * b->R[0] + s->nis_tq[1] * b->R[4];
    b->T[1] += s->nis_tq[0] * b->R[1] + s->nis_tq[1] * b->R[5];
}
static void nis_post(N2Sim *s)                                           /* 0x5ab880 */
{
    Mello *m = &s->m; Body *b = &s->b;
    mello_post(s);
    float vf = b->v[0] * b->R[0] + b->v[1] * b->R[1] + b->v[2] * b->R[2];
    if (fabsf(vf) < 0.1f) vf = 0;
    float rf = PF(0x1c0);
    for (int i = 0; i < 4; i++) {
        float w = vf / s->P[(0x20 + 0x30 * i) / 4];
        /* the driven wheels (P+0x1c0 split: < 0.2 front, > 0.8 rear, else
           all four, and those only rolling forward) spin +0x254 turns a
           second extra */
        int awd = !(rf < 0.2f) && !(rf > 0.8f);
        int driven = rf < 0.2f ? i < 2 : rf > 0.8f ? i >= 2 : 1;
        if (driven && (!awd || vf > 0)) w += 360.0f * s->nis_spinadd * 0.0174532924f;
        if (i < 2) w *= s->nis_fspin;
        m->spin[i] = w;
    }
}
static void nis_post_hook(N2Sim *s)                                      /* 0x5a4410 */
{
    /* race mode 8 (0x89e7b0) first recomputes the four Mellovator wheel
       values +0x1f8.. through 0x56d4a0: not ported (no mode 8 here) */
    body_post(&s->b);
    nis_post(s);
    /* 0x5ab130 on the NIS chassis: its forces, aerodynamics, hydraulics,
       low-wheel damping */
    nis_forces(s);
    aero_force(s);
    hydraulics(s);
    if (!hydraulics_held(s) && s->cs_wheels < 2) {
        Body *b = &s->b;
        float k = -(graph(MELLO_ANGDAMP, 3, len3(b->w)) * b->mass);
        b->T[0] += b->w[0] * k; b->T[1] += b->w[1] * k; b->T[2] += b->w[2] * k;
    }
}
static void nis_substep(N2Sim *s, float h)                               /* 0x5aa480 */
{
    float db[32];
    body_deriv(&s->b, db);
    float *st = s->b.pos;
    for (int i = 0; i < 32; i++) st[i] += h * db[i];
    /* the chassis' spin derivative (0x40d080) writes nothing; the spins
       are set again by the post below */
    nis_post_hook(s);
}
/* 0x59b9b0: the body's velocity and angular velocity set outright */
static void body_set_velocity(Body *b, const float v[3], const float w[3])
{
    memcpy(b->v, v, 12); memcpy(b->w, w, 12);
    float wb[3], lb[3];
    for (int i = 0; i < 3; i++) b->p[i] = b->mass * v[i];
    vmulT(wb, w, b->R);
    lb[0] = wb[0] / b->Iinv[0]; lb[1] = wb[1] / b->Iinv[5]; lb[2] = wb[2] / b->Iinv[10];
    vmul(b->L, lb, b->R);
}
static float nis_heading_err(float want, const float *R)                /* degrees, wrapped */
{
    float car = (float)(unsigned short)(int)lrintf(atan2f(R[1], R[0]) * RAD2U16) * 0.0054931640625f;
    float e = want - car;
    if (-e > 180.0f) e = (360.0f - car) + want;
    else if (-e < -180.0f) e = (want - 360.0f) - car;
    return e;
}
static void nis_drive(N2Sim *s, float dt)                                /* 0x5a44a0 */
{
    Body *b = &s->b;
    float inv = 1.0f / dt;
    float v[3] = { (s->nis_tgt[0] - b->pos[0]) * inv, (s->nis_tgt[1] - b->pos[1]) * inv,
                   (s->nis_tgt[2] - b->pos[2]) * inv };
    float yaw = inv * nis_heading_err(s->nis_head, b->R) * 0.0174532942f;
    if (s->nis_count > 3) {
        /* held: an impulse at 0.2 m below the centre of mass takes up the
           horizontal velocity error, the yaw momentum takes up the yaw */
        float J[3] = { (v[0] - b->v[0]) * b->mass, (v[1] - b->v[1]) * b->mass, 0 };
        float r[3] = { 0, 0, -0.2f };                                  /* 0x7a19c0 */
        body_impulse(b, J, r);
        const float *I = b->yawI_on ? b->Iinv2 : b->Iinv;
        b->L[2] += (yaw - b->w[2]) / I[10];
        return;
    }
    float vv[3] = { v[0], v[1], b->v[2] }, ww[3] = { b->w[0], b->w[1], yaw };
    body_set_velocity(b, vv, ww);
}
static void nis_frame(N2Sim *s, float dt)                                /* 0x5ab3c0 */
{
    Mello *m = &s->m; Body *b = &s->b; Aero *a = &s->a;
    b->damp_on = b->angdrag_on = 1;
    if (b->yawI_on) { b->yawI_on = 0; b->L[2] /= PF(0x160); }
    a->on = 0;
    /* (chassis +0x194 = 9.8: a field nothing reads) */
    memcpy(b->com, s->P, 12);                                /* 0x5a29a0 */
    memcpy(s->m.hyd, s->hyd_h, sizeof s->m.hyd);             /* car +0x3b8 -> +0x1e8 */
    body_mass(s);
    m->thr = s->eff.throttle;
    m->req = s->gear_req;
    float hb = s->eff.handbrake * 0.5f;
    m->brake = s->eff.brake > hb ? s->eff.brake : hb;
    a->thr = s->eff.throttle; a->ground = (float)s->cs_wheels * 0.25f;
    /* (car state +0x330..+0x33c = 0: nothing ported reads them) */
    nis_drive(s, dt);
    s->nis_moving = 0;
    int n = (int)(dt / MELLO_HMAX) + 1;
    float h = dt / (float)n;
    for (int k = 0; k < n; k++) {
        s->h = h;
        s->time += h;
        nis_substep(s, h);
        orthonormalize(b->R);
    }
    /* 0x5a2b60 */
    s->cs_absv = len3(b->v);
    s->cs_speed = b->v[0]*b->R[0] + b->v[1]*b->R[1] + b->v[2]*b->R[2];
    int nw = 0;
    for (int i = 0; i < 4; i++) {
        m->angle[i] += m->spin[i] * dt;
        s->cs_contact[i] = m->contact[i];
        /* 0x59cb90: a drag or street X race reads every surface as ROAD (1) */
        s->cs_surf[i] = s->drag || s->streetx ? 1 : m->surf[i];
        nw += m->contact[i] != 0;
    }
    s->cs_wheels = nw;
    s->cs_tach = m->rpm;
}
static void nis_enter(N2Sim *s)                                          /* 0x5b6fc0 + 0x5a2a60 */
{
    Mello *m = &s->m;
    for (int i = 0; i < 4; i++) {
        m->spin[i] = s->wh[i].w; m->angle[i] = s->wh[i].angle;
        m->comp[i] = 0; m->locked[i] = 0;
    }
    m->gear = s->d.gear;
    m->rpm = s->cs_tach;
    m->steer = s->steer_u16;
    /* the mover's own new rigid body (0x5a7f30): damping, angular drag and
       the grounded inertia on, gravity 9.8, no boost or yaw clamp (the
       Mellovator's at-rest check may have left gravity at 0) */
    Body *b = &s->b;
    b->damp_on = b->angdrag_on = b->yawI_on = 1;
    b->gravity = 9.8f; b->power = 1; b->boost = 0; b->yaw_clamp = 0;
    memset(b->F, 0, sizeof b->F); memset(b->T, 0, sizeof b->T);
    b->top_speed = PF(0x290);                                /* body +0x204, always */
    memset(s->nis_tgt, 0, sizeof s->nis_tgt); memset(s->nis_prev, 0, sizeof s->nis_prev);
    s->nis_head = 0; s->nis_count = 0; s->nis_moving = 0;
    s->nis_fspin = 1.0f; s->nis_spinadd = 0; s->nis_tq[0] = s->nis_tq[1] = 0;
    body_from_state(s);
    s->h = 0;
    m->thr = s->eff.throttle; m->req = s->gear_req;
    m->brake = s->eff.brake;
    nis_post_hook(s); nis_post_hook(s);                      /* 0x5a2a60 */
    nis_post_hook(s);                                        /* the ctor's own */
}

/* ---- TrafficMover (mover 2) ----------------------------------------------
   Traffic cars' model (ctor 0x408740, frame 0x408820): a point on the road
   with a heading and a speed, turned about the rear axle by the steering
   angle, sped up and slowed by two speed graphs; the body's height and tilt
   come from the ground under the four wheels (0x5f5010 -> 0x5f49f0) and
   the engine sound from a made-up rpm and gear (0x5f50a0). */
static const float TM_ACC_D[4] = { 3.57621813f, 2.68216372f, 2.01162267f, 1.34108186f };      /* 0x7e95d0 */
static const float TM_BRK_D[4] = { 6.70540905f, 13.4108181f, 20.1162281f, 26.8216362f };   /* 0x7e95e0 */
static const Curve TM_ACC = { 4, 0, 24.5865f, 0.12201818f, TM_ACC_D };    /* 0x7ea03c */
static const Curve TM_BRK = { 4, 0, 24.5865f, 0.12201818f, TM_BRK_D };    /* 0x7ea050 */
#define TM_VMAX 24.5865f                                                  /* 0x7845a4 */
static const float TM_RPM[12] = { 1000,8500, 6500,8500, 6500,8500, 5500,9000, 6500,7200, 6600,7000 };  /* 0x8021d4 */
static const float TM_BAND[7] = { 0, 25.38f, 40.89f, 56.47f, 72.06f, 80.0f, 89.51f };  /* 0x802208.. */
static void tm_pose(N2Sim *s)                                            /* 0x5f5010 -> 0x5f49f0 */
{
    Body *b = &s->b;
    float c = sin16(s->tm_head + 1.57079637f), sn = sin16(s->tm_head);
    float R[9] = { c, sn, 0, -sn, c, 0, 0, 0, 1 }, w[4][3], gz[4], ride = 0, gsum = 0;
    int all = 1;
    for (int i = 0; i < 4; i++) {
        const float *wr = s->P + (0x10 + 0x30 * i) / 4;
        for (int j = 0; j < 3; j++) w[i][j] = R[j] * wr[0] + R[3 + j] * wr[1] + R[6 + j] * wr[2] + b->pos[j];
        float n[3]; int surf = 0;
        int ok = s->ground && s->ground(s->user, w[i][0], w[i][1], w[i][2], &gz[i], n, &surf);
        if (ok) { s->m.surf[i] = surf; memcpy(s->m.n[i], n, 12); } else gz[i] = 0;
        all &= ok != 0;
        ride += wr[4] - wr[2];
        gsum += gz[i];
        w[i][2] = gz[i];                               /* the wheel's ground point (0x5e72e0) */
    }
    if (all) {
        float f[3], l[3], u[3];
        for (int j = 0; j < 3; j++) { f[j] = (w[1][j] - w[2][j]) + (w[0][j] - w[3][j]); l[j] = (w[3][j] - w[2][j]) + (w[0][j] - w[1][j]); }
        float k = len3(f); if (k > 0) { f[0] /= k; f[1] /= k; f[2] /= k; }   /* 0x43cf10 */
        k = len3(l); if (k > 0) { l[0] /= k; l[1] /= k; l[2] /= k; }
        cross(u, f, l);
        memcpy(R, f, 12); memcpy(R + 3, l, 12); memcpy(R + 6, u, 12);
        b->pos[2] = gsum * 0.25f + (ride * 0.25f + s->P[2]) / fabsf(u[2]);
    }
    for (int r = 0; r < 3; r++) { b->R[r * 4] = R[r * 3]; b->R[r * 4 + 1] = R[r * 3 + 1]; b->R[r * 4 + 2] = R[r * 3 + 2]; b->R[r * 4 + 3] = 0; }
    for (int i = 0; i < 4; i++) s->m.contact[i] = s->cs_contact[i] = all;
    s->cs_wheels = all ? 4 : 0;
}
static void tm_engine(N2Sim *s)                                          /* 0x5f50a0 */
{
    float v = s->tm_v;                                 /* car +0x430 (0x8900fc debug override off) */
    if (TM_BAND[6] <= v) v = TM_BAND[6] - 0.01f;
    if (v > s->tm_s540 + 1.0f) v = s->tm_s540 + 1.0f;  /* 0x802224: 1 a call */
    else if (v < s->tm_s540 - 1.0f) v = s->tm_s540 - 1.0f;
    s->tm_s540 = v;
    float rpm = 0; int gear;
    if (v >= TM_BAND[1]) {
        gear = s->d.gear;
        for (int i = 1; i < 6; i++)
            if (v < TM_BAND[i + 1]) {
                gear = i + 2 > 6 ? 6 : i + 2;
                rpm = (TM_RPM[i * 2 + 1] - TM_RPM[i * 2]) * ((v - TM_BAND[i]) / (TM_BAND[i + 1] - TM_BAND[i])) + TM_RPM[i * 2];
                break;
            }
    } else {
        gear = v < -2.0f ? 0 : 2;                      /* 0x802204 */
        float a = fabsf(v) > TM_BAND[1] ? TM_BAND[1] : fabsf(v);
        rpm = a / TM_BAND[1] * (TM_RPM[1] - TM_RPM[0]) + TM_RPM[0];
    }
    if (s->d.gear == gear) {                           /* +-50 rpm a call in the same gear (0x802220) */
        if (s->cs_tach + 50.0f < rpm) rpm = s->cs_tach + 50.0f;
        else if (rpm < s->cs_tach - 50.0f) rpm = s->cs_tach - 50.0f;
    }
    s->d.gear = gear;                                  /* car +0x434 */
    s->cs_tach = rpm;                                  /* car +0x440 */
}
static void tm_enter(N2Sim *s)                                           /* 0x408740 */
{
    Body *b = &s->b;
    s->tm_x = b->pos[0]; s->tm_y = b->pos[1];          /* car +0x60 */
    s->tm_head = (float)(unsigned short)(int)lrintf(atan2f(b->R[1], b->R[0]) * RAD2U16) * U16RAD;   /* car +0x40c */
    s->tm_v = s->cs_speed;                             /* car +0x430 */
    s->tm_s540 = s->tm_v;
    s->cs_head = (unsigned short)(int)lrintf(s->tm_head * RAD2U16);   /* (port) see n2sim_reset */
}
static void tm_frame(N2Sim *s, float dt)                                 /* 0x408820 */
{
    Body *b = &s->b;
    float L = s->P[0x10 / 4] - s->P[0xa0 / 4], d = dt * s->tm_v;
    float c = sin16(s->tm_head + 1.57079637f), sn = sin16(s->tm_head);
    float st = (float)s->steer_u16 * U16RAD;           /* 0x5ebf50 */
    if (st > PI_F) st -= 2 * PI_F;
    float ox = b->pos[0], oy = b->pos[1], oz = b->pos[2];
    if (st <= L * 0.01f && -(L * 0.01f) <= st) {
        s->tm_x += c * d; s->tm_y += sn * d;
    } else {
        float rx = s->tm_x + c * s->P[0xa0 / 4], ry = s->tm_y + sn * s->P[0xa0 / 4];   /* rear axle (0x43d090) */
        float R = L / st;
        float cx = rx - sn * R, cy = ry + c * R;       /* turn centre on the left axis */
        float da = d / R;
        float ca = sin16(da + 1.57079637f), sa = sin16(da);
        float px = s->tm_x - cx, py = s->tm_y - cy;
        s->tm_x = px * ca - py * sa + cx; s->tm_y = px * sa + py * ca + cy;
        s->tm_head += da;
    }
    s->tm_v += curve(&TM_ACC, s->tm_v) * s->eff.throttle * dt
             - curve(&TM_BRK, s->tm_v) * s->eff.brake * dt;   /* driver +0x20c / +0x214 */
    if (s->tm_v <= 0) s->tm_v = 0;
    else if (s->tm_v > TM_VMAX) s->tm_v = TM_VMAX;
    b->pos[0] = s->tm_x; b->pos[1] = s->tm_y;          /* car +0x60 / +0x64, z kept */
    tm_pose(s);
    float v[3] = { b->pos[0] - ox, b->pos[1] - oy, b->pos[2] - oz }, k = len3(v);
    if (fabsf(s->tm_v) > 1e-5f && k > 0) { k = s->tm_v / k; v[0] *= k; v[1] *= k; v[2] *= k; }   /* 0x43cf90 */
    else v[0] = v[1] = v[2] = 0;
    memcpy(b->v, v, 12);                               /* 0x592160 */
    for (int i = 0; i < 3; i++) b->p[i] = b->mass * v[i];
    s->cs_speed = s->tm_v;                             /* +0x3ec */
    s->cs_absv = len3(v);
    unsigned short h = (unsigned short)(int)lrintf(s->tm_head * RAD2U16);
    b->w[0] = b->w[1] = 0;                             /* +0xd0 +0xd4 */
    b->w[2] = (float)(short)(h - s->cs_head) * U16RAD / dt;   /* +0xd8 */
    s->cs_head = h;
    for (int i = 0; i < 3; i++) { s->cs_v[i] = b->v[i]; s->cs_w[i] = b->w[i]; }
    for (int i = 0; i < 4; i++) {                      /* wheel spin v / radius, no slip */
        Wheel *w = &s->wh[i];
        w->w = 1.0f / s->P[(0x20 + 0x30 * i) / 4] * s->tm_v;
        w->angle += w->w * dt;
        s->m.spin[i] = w->w; s->m.angle[i] = w->angle;
        s->m.sliding[i] = 0; s->m.sv[i][0] = s->m.sv[i][1] = 0; s->m.comp[i] = 0;
    }
    s->m.steer = s->steer_u16;
    s->m.thr = s->eff.throttle;
    tm_engine(s);
    s->m.gear = s->d.gear; s->m.rpm = s->cs_tach;
}

/* ---- mover 4: the far route mover (0x5a9030 / 0x59ec80) ---------------------
   The car rides a Catmull-Rom spline through its driver's plan points at
   s(t) = s0 + v0 t + a t^2 / 2 (spline units; k of them a metre). The heading
   follows the path's velocity 0.4 s ahead through a 0.4 s blend, the car's
   offset from the path at the re-path blends to 0 over 0.5 s, and the
   ground under the wheels sets height and tilt (0x5f5010). The original's
   lateral slide offset (+0x2c) and its feed are multiplied by 0 (0x7a17f0 /
   0x7a17f8 / 0x7a17fc), so they are left out. */
static void bl_set_p0(Blend1 *f, float v) { f->p0 = v; if (v != f->p1) f->state = 2; }   /* 0x574b10 */
static void bl_set_t0(Blend1 *f, float v) { f->t0 = v; if (v != f->t1) f->state = 2; }   /* 0x574b50 */
static void bl_set_p1(Blend1 *f, float v) { f->p1 = v; if (v != f->p0) f->state = 2; }   /* 0x574b90 */
static void bl_init(Blend1 *f)
{
    memset(f, 0, sizeof *f);
    f->dur = 1.0f; f->end = 1.0f; f->loop = 1;
}
static void bl_update(Blend1 *f, float dt)                               /* 0x574e30 (no limits) */
{
    if (f->state == 0 || (f->state != 1 && f->state != 2)) return;
    if (f->state == 2) {
        f->t = 0;
        if (!f->loop) f->state = 1;
        float D = f->p1 - f->p0;
        f->d = f->p0; f->c = f->t0;
        f->a = (f->t1 + f->t0) - (D + D);
        f->b = (D * 3.0f - f->t1) - (f->t0 + f->t0);
    }
    float t = dt / f->dur + f->t;
    f->t = t;
    if (t < f->end) {
        f->p0 = ((t * f->a + f->b) * t + f->c) * t + f->d;
        f->t0 = (f->b + f->b + t * f->a * 3.0f) * t + f->c;
    } else {
        f->state = 0;
        if (1.0f <= f->end) { f->p0 = f->p1; f->t0 = f->t1; }
    }
}
/* the spline (as racepath.c's, 0x570240 / 0x570510 / 0x570640 type 2) */
static const float FAR_BASIS[3][4][4] = {                               /* 0x865230 */
    { { 0, -0.5f, 1, -0.5f }, { 1, 0, -2.5f, 1.5f }, { 0, 0.5f, 2, -1.5f }, { 0, 0, -0.5f, 0.5f } },
    { { -0.5f, 2, -1.5f, 0 }, { 0, -5, 4.5f, 0 }, { 0.5f, 4, -4.5f, 0 }, { 0, -1, 1.5f, 0 } },
    { { 2, -3, 0, 0 }, { -5, 9, 0, 0 }, { 4, -9, 0, 0 }, { -1, 3, 0, 0 } },
};
static void far_ext(const FarMv *f, int k, float o[3])                   /* 0x570510; P[k + 1] is point k */
{
    int n = f->n;
    for (int c = 0; c < 3; c++) {
        if (k < -1) o[c] = f->P[1][c] + (f->P[1][c] - f->P[2][c]) * (float)(-k);
        else if (n < k) o[c] = f->P[n][c] + (f->P[n][c] - f->P[n - 1][c]) * (float)(k - n + 1);
        else o[c] = f->P[k + 1][c];
    }
}
static void far_eval(FarMv *f, float t, int deriv, float o[3])          /* 0x570640 */
{
    int n = f->n;
    if (f->dirty) {                                                     /* 0x570240 */
        if (n >= 2)
            for (int c = 0; c < 3; c++) {
                f->P[0][c] = f->P[1][c] - (f->P[2][c] - f->P[1][c]);
                f->P[n + 1][c] = f->P[n][c] + (f->P[n][c] - f->P[n - 1][c]);
            }
        f->dirty = 0;
    }
    if (n == 0) { o[0] = o[1] = o[2] = 0; return; }
    float fl = (float)(int)t;
    if (fl > t) fl -= 1.0f;
    int seg = (int)fl;
    float u = t - fl, Q[4][3];
    if (seg < 0 || seg >= n - 1) for (int j = 0; j < 4; j++) far_ext(f, seg - 1 + j, Q[j]);
    else for (int j = 0; j < 4; j++) memcpy(Q[j], f->P[seg + j], sizeof Q[j]);
    float T[4] = { 1.0f, u, u * u, u * u * u }, w[4];                   /* 0x570440 */
    for (int j = 0; j < 4; j++)
        w[j] = FAR_BASIS[deriv][j][0] * T[0] + FAR_BASIS[deriv][j][1] * T[1] + FAR_BASIS[deriv][j][2] * T[2] + FAR_BASIS[deriv][j][3] * T[3];
    for (int c = 0; c < 3; c++) o[c] = w[0] * Q[0][c] + w[1] * Q[1][c] + w[2] * Q[2][c] + w[3] * Q[3][c];
}
static float far_s(const FarMv *f, float t) { return (t * f->a * 0.5f + f->v0) * t + f->s0; }
static void far_pos(FarMv *f, float t, float o[3])                      /* 0x591ad0 */
{
    float s = far_s(f, t);
    far_eval(f, s, 0, o);
    if (s < 0 && o[2] < f->P[1][2]) o[2] = f->P[1][2];
}
static void far_vel(FarMv *f, float t, float o[3])                      /* 0x591b70: metres a second */
{
    float r = t * f->a + f->v0;
    far_eval(f, far_s(f, t), 1, o);
    o[0] *= r; o[1] *= r; o[2] *= r;
}
static void far_acc(FarMv *f, float t, float o[3])                      /* 0x591be0 */
{
    float r = t * f->a + f->v0, s = far_s(f, t), d1[3], d2[3];
    far_eval(f, s, 1, d1);
    far_eval(f, s, 2, d2);
    r *= r;
    for (int c = 0; c < 3; c++) o[c] = d2[c] * r + d1[c] * f->a;
}
static void unit3(float *v)                                              /* 0x43cf10 */
{
    float l = len3(v);
    if (l != 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
/* 0x591950: the tyres' lateral limit against the path's lateral
   acceleration (its result is used times 0; only the flag +0x34 stays) */
static void far_slide(N2Sim *s, float lat)
{
    FarMv *f = &s->far;
    const float *P = s->P;
    float need = (P[0xd8 / 4] + P[0xf8 / 4]) * 0.5f * P[0x110 / 4] * fabsf(lat);
    float mu = f->slide ? P[0xe0 / 4] + P[0x100 / 4] : P[0xdc / 4] + P[0xfc / 4];
    float have = (9.8f * P[0x110 / 4] + (f->v0 / f->k) * (P[0x268 / 4] + P[0x264 / 4]) * 0.5f) * mu * 0.5f;
    f->slide = have < need;
}
/* 0x59ea20(0.5): over the next 0.45 s, how far the path drops below the
   arc of a car leaving it now (g plus the downforce share) */
static float far_drop(N2Sim *s, float span)
{
    FarMv *f = &s->far;
    const float *P = s->P;
    float g = ((f->v0 / f->k) * (P[0x268 / 4] + P[0x264 / 4]) * 0.5f) / P[0x110 / 4] + 9.8f;
    float p[3], v[3];
    far_pos(f, f->t, p);
    far_vel(f, f->t, v);
    float z0 = p[2], vz = v[2], t0 = 0, best = 0;
    for (int i = 1; i < 10; i++) {
        float dt = (float)i * span * 0.1f, tt = dt + f->t;
        far_pos(f, tt, p);
        float h = (((dt - t0) * -g + vz) * (dt - t0) + z0) - p[2];
        if (h <= 0) {
            float d1[3];
            far_eval(f, far_s(f, tt), 1, d1);
            vz = d1[2] * (tt * f->a + f->v0);
            z0 = p[2]; t0 = dt;
        } else if (best <= h) best = h;
    }
    return best;
}
static void far_enter(N2Sim *s)                                          /* 0x5a9030 */
{
    FarMv *f = &s->far;
    Body *b = &s->b;
    memset(f, 0, sizeof *f);
    bl_init(&f->lat); bl_init(&f->hx); bl_init(&f->hy);
    float v = s->cs_absv;                                                /* car +0x430 */
    float h = (float)s->cs_head * U16RAD, w = 0.4f * b->w[2] * v;      /* car +0x40c, +0x118 */
    bl_set_p0(&f->hx, cosf(h) * v); bl_set_p0(&f->hy, sinf(h) * v);
    bl_set_t0(&f->hx, cosf(h + 1.5707964f) * w); bl_set_t0(&f->hy, sinf(h + 1.5707964f) * w);
    f->lat.loop = f->hx.loop = f->hy.loop = 0;                          /* 0x5918a0 (no path yet) */
    f->want = 1;
}
/* 0x5918a0 + 0x591690 */
void n2sim_far_path(N2Sim *s, const float (*p)[3], int n, float len, float vtarget, float x234, float acc)
{
    FarMv *f = &s->far;
    Body *b = &s->b;
    f->lat.loop = f->hx.loop = f->hy.loop = 0;
    f->want = 0;
    if (n < 2) { f->has_path = 0; return; }
    if (n > FAR_MAXPTS) n = FAR_MAXPTS;
    for (int i = 0; i < n; i++) { f->P[i + 1][0] = p[i][0]; f->P[i + 1][1] = p[i][1]; f->P[i + 1][2] = p[i][2]; f->P[i + 1][3] = 0; }
    f->n = n; f->dirty = 1;
    /* 0x591690 */
    f->t = 0;
    f->k = (float)(n - 1) / len;
    int best = 0;                                                       /* 0x570730 from point 0 */
    {
        float bd = 0;
        for (int i = 0; i < n; i++) {
            float d[3] = { b->pos[0] - f->P[i + 1][0], b->pos[1] - f->P[i + 1][1], b->pos[2] - f->P[i + 1][2] }, l = len3(d);
            if (i == 0 || l < bd) { bd = l; best = i; }
            else break;
        }
    }
    float t = (float)best, stp = 1.0f, ps = 0, pe = 0;                  /* 0x5774c0, tolerance 0.005 */
    for (int it = 0; it < 998; it++) {
        float a[3], d[3];
        far_eval(f, t, 0, a);
        far_eval(f, t, 1, d);
        float L = len3(d);
        if (L == 0) break;
        float e = ((b->pos[0] - a[0]) * d[0] + (b->pos[1] - a[1]) * d[1] + (b->pos[2] - a[2]) * d[2]) / L;
        if (fabsf(e) < 0.005f) break;
        if (ps != 0 && -((e - pe) / ps) == 0) break;
        float sv = e / L;
        if (sv <= -stp) sv = -stp;
        if (stp < sv) sv = stp;
        t += sv;
        if (sv * ps < 0) stp *= 0.5f;
        ps = sv; pe = e;
    }
    float off[3], tan[3];                                               /* 0x591570 */
    far_eval(f, t, 0, off);
    far_eval(f, t, 1, tan);
    for (int c = 0; c < 3; c++) off[c] -= b->pos[c];
    f->s0 = t;
    unit3(tan);
    f->v0 = s->cs_speed * f->k;                                         /* car +0x42c */
    f->a = (vtarget * 0.899f * f->k - f->v0) / (0 < x234 ? x234 : 0.5f);   /* 0x7a1820, 0x7a17c0 */
    float lim = (acc + -9.8f * tan[2]) * f->k;                          /* 0x4038d0 + 0x591620 */
    if (lim < f->a) f->a = lim;
    else if (f->a < -20.0f * f->k) f->a = -20.0f * f->k;                /* 0x7a17d0 */
    float up[3] = { 0, 0, 1 }, side[3];
    cross(side, tan, up);
    bl_set_p0(&f->lat, -(off[0] * side[0] + off[1] * side[1] + off[2] * side[2]));
    bl_set_t0(&f->lat, 0.5f * (side[0] * b->v[0] + side[1] * b->v[1] + side[2] * b->v[2]));
    f->lat.dur = 0.5f;
    f->hx.dur = f->hy.dur = 0.4f;
    f->has_path = 1;
}
int n2sim_far_want(N2Sim *s) { return s->mover == N2_MOVER_FAR && s->far.want; }
int n2sim_far_take_jump(N2Sim *s)
{
    int j = s->mover == N2_MOVER_FAR && s->far.jump;
    s->far.jump = 0;
    return j;
}
static void far_frame(N2Sim *s, float dt)                                /* 0x59ec80 */
{
    FarMv *f = &s->far;
    Body *b = &s->b;
    int stopped = 0;
    if (0 < f->k) stopped = !(0.01f <= fabsf(f->v0 / f->k)) && !(0.01f <= fabsf(f->a / f->k));
    if (!f->has_path || f->n < 2 || stopped || !((float)f->n > f->s0)) {
        f->want = 1;                                                    /* 0x5918a0 */
        memset(b->v, 0, sizeof b->v); memset(b->p, 0, sizeof b->p);     /* car +0x70 */
        s->cs_speed = 0; s->cs_absv = 0;                                /* +0x42c, +0x430 */
        return;
    }
    float o[3] = { b->pos[0], b->pos[1], b->pos[2] };
    if (1.0f < far_drop(s, 0.5f)) f->jump = 1;                          /* 0x591540(1 s) */
    float t = dt + f->t;
    f->t = t;
    float spd = (t * f->a + f->v0) / f->k;
    float hv[3];
    far_vel(f, t + 0.4f, hv);                                           /* 0x7a17e8 */
    bl_set_p1(&f->hx, hv[0]); bl_set_p1(&f->hy, hv[1]);
    bl_update(&f->hx, dt); bl_update(&f->hy, dt);                       /* 0x574f40 */
    hv[0] = f->hx.p0; hv[1] = f->hy.p0;
    {   /* the path's lateral acceleration 0.3 s ahead into the 30-sample mean (0x576970) */
        float tt = t + 0.3f, v[3], ac[3], up[3] = { 0, 0, 1 }, sd[3];
        far_vel(f, tt, v);
        cross(sd, v, up); unit3(sd);
        far_acc(f, tt, ac);
        float x = ac[0] * sd[0] + ac[1] * sd[1] + ac[2] * sd[2];
        if (f->rcount < 30) f->rcount++;
        f->rsum += x - f->ring[f->ridx];
        f->ring[f->ridx] = x;
        if (30 <= ++f->ridx) f->ridx = 0;
        f->ravg = f->rsum / (float)f->rcount;
        far_slide(s, f->ravg);
    }
    unsigned short h = fast_atan(hv[0], hv[1]);                         /* 0x43c570 -> car +0x40c */
    unsigned short h0 = s->cs_head;
    float vn[3], up[3] = { 0, 0, 1 }, sd[3];
    far_vel(f, t, vn);
    cross(sd, vn, up); unit3(sd);
    bl_set_p1(&f->lat, 0);
    bl_update(&f->lat, dt);
    float p[3];
    far_pos(f, t, p);
    for (int c = 0; c < 3; c++) b->pos[c] = p[c] + sd[c] * f->lat.p0;  /* 0x43d0c0 */
    s->tm_head = (float)h * U16RAD;
    tm_pose(s);                                                         /* 0x5f5010 (car +0x5cc set) */
    float v[3] = { b->pos[0] - o[0], b->pos[1] - o[1], b->pos[2] - o[2] };
    if (1e-4f < fabsf(dt)) { v[0] /= dt; v[1] /= dt; v[2] /= dt; }
    else { float k = len3(v); if (k > 0) { k = spd / k; v[0] *= k; v[1] *= k; v[2] *= k; } }   /* 0x43cf90 */
    memcpy(b->v, v, 12);                                                /* 0x592160 */
    for (int i = 0; i < 3; i++) b->p[i] = b->mass * v[i];
    s->cs_absv = len3(v);                                               /* +0x430 */
    s->cs_speed = spd;                                                  /* +0x42c */
    b->w[0] = b->w[1] = 0;
    b->w[2] = (float)(short)(h - h0) * U16RAD / dt;                    /* +0x118 */
    s->cs_head = h;
    for (int i = 0; i < 3; i++) { s->cs_v[i] = b->v[i]; s->cs_w[i] = b->w[i]; }
    for (int i = 0; i < 4; i++) {                                       /* 0x59cb90: spin = speed / radius */
        Wheel *w = &s->wh[i];
        w->w = spd / s->P[(0x20 + 0x30 * i) / 4];
        w->angle += w->w * dt;
        s->m.spin[i] = w->w; s->m.angle[i] = w->angle;
        s->m.sliding[i] = f->slide;                                     /* wheel +0x160 = +0x34 */
        s->m.sv[i][0] = s->m.sv[i][1] = 0; s->m.comp[i] = 0;
    }
    s->m.steer = s->steer_u16;
    s->m.thr = s->eff.throttle;
    s->tm_v = s->cs_absv;                                               /* 0x5f50a0 reads car +0x430 */
    tm_engine(s);
    s->m.gear = s->d.gear; s->m.rpm = s->cs_tach;
}
/* fresh rigid body from the published state (0x59b9b0): momentum from the
   velocity, angular momentum from the angular velocity through the
   diagonal of the body's inertia */
static void body_from_state(N2Sim *s)
{
    Body *b = &s->b;
    float wb[3], lb[3];
    for (int i = 0; i < 3; i++) b->p[i] = b->mass * b->v[i];
    vmulT(wb, b->w, b->R);
    lb[0] = wb[0] / b->Iinv[0]; lb[1] = wb[1] / b->Iinv[5]; lb[2] = wb[2] / b->Iinv[10];
    vmul(b->L, lb, b->R);
    b->yawI_on = 1;
    b->settle = 0;
}
static void mello_init(N2Sim *s)                                         /* 0x5b0030 */
{
    Mello *m = &s->m;
    const float *er = s->P + 0x1f0 / 4;
    curve_init(&m->torque, 9, er[0], er[2], er + 4);
    curve_init(&m->ebrake, 3, er[0], er[2], er + 22);
    curve_init(&m->nos, 9, er[0], er[1], MELLO_NOS_D);
    memset(m->up, 0, sizeof m->up); memset(m->down, 0, sizeof m->down);
    shift_points(s->P + 0x1b0 / 4, er, m->up, m->down);
    float rf = PF(0x1c0);
    int k = rf >= 0.2f ? 1 : 0;
    if (rf > 0.8f) k = 2;
    m->up[1] *= DRIVE_TUNE4[k];
    m->gear = 2; m->mult = m->gripm = 1;
    if (!(m->perfk > 0)) m->perfk = 0.25f;
}
static void mello_enter(N2Sim *s)                                        /* 0x5b6d00 + 0x5a2a60 */
{
    Mello *m = &s->m;
    for (int i = 0; i < 4; i++) {
        m->spin[i] = s->wh[i].w; m->angle[i] = s->wh[i].angle;   /* carstate +0x2f0/+0x300 */
        m->comp[i] = 0; m->locked[i] = 0;
    }
    m->gear = s->d.gear;                                     /* carstate +0x3f4 */
    m->rpm = s->cs_tach;                                     /* carstate +0x400 (0x7fbed8 == 100) */
    m->rest = 0; m->rest_t = 0;
    body_from_state(s);
    s->h = 0;                                                /* 0x7fc800 */
    mello_controls(s);
    mello_post_hook(s);
    mello_post_hook(s);
}
static void physics_enter(N2Sim *s)                                      /* 0x5b5cc0 + 0x5a48a0 */
{
    Mello *m = &s->m;
    body_from_state(s);
    for (int i = 0; i < 4; i++) {
        s->su[i].x = s->su[i].xv = 0;
        s->wh[i].angle = m->angle[i]; s->wh[i].w = m->spin[i];
    }
    s->e.w = s->cs_tach * RPM2RAD; s->e.load = 0; s->e.limiter = 0;   /* carstate +0x400 */
    for (int i = 0; i < 4; i++) {
        Wheel *w = &s->wh[i];
        w->torque = w->Fx = w->Fy = w->tsmooth = 0; w->locked = 0;
    }
    s->d.prevdiff = 0; s->d.shift_t = 0;
    s->d.gear = s->d.target = m->gear;
    s->d.state = 3; s->d.engage = 1; s->d.shifting = 0;
    s->rest = 0;
    s->h = 0;
    frame_setup(s, 0);                                       /* 0x5ab990 preloads */
    post_hook(s);
}

/* ---- public ------------------------------------------------------------- */
N2Sim *n2sim_create(const unsigned char *rec, N2GroundFn ground, void *user)
{
    N2Sim *s = (N2Sim *)calloc(1, sizeof *s);
    if (!s) return NULL;
    memcpy(s->rec, rec, sizeof s->rec);
    s->P = (const float *)(s->rec + 0x110);
    s->nos_stock[0] = s->P[0x2a0/4]; s->nos_stock[1] = s->P[0x2ac/4];
    s->ground = ground; s->user = user;
    s->rng = 0x12345u;
    Body *b = &s->b;                                         /* 0x5a7f30 */
    ident(b->R);
    b->damp_on = b->angdrag_on = b->yawI_on = 1;
    b->gravity = 9.8f; b->power = 1;
    memcpy(b->com, s->P, 12);
    body_mass(s);
    for (int i = 0; i < 4; i++) {                            /* 0x5a8160 / 0x5a86c0 */
        Wheel *w = &s->wh[i]; Susp *u = &s->su[i];
        w->pr = s->P + (0x10 + 0x30*i)/4;
        w->tyre = s->P + (0xd0 + 0x20*(i/2))/4;
        w->brk = s->P + 0x270/4;
        w->idx = i; w->dir = 1;
        w->grip = w->power = w->grip2 = w->kx = w->ky = 1;
        w->I = 0.01f; w->n[2] = 1;
        ident(w->spin); ident(w->world); ident(w->local);
        u->rec = s->P + (0x170 + 0x20*(i/2))/4;
        u->wheel = w; u->steered = i < 2; u->idx = i;
    }
    s->su[0].partner = &s->su[1]; s->su[1].partner = &s->su[0];
    s->su[2].partner = &s->su[3]; s->su[3].partner = &s->su[2];
    Engine *e = &s->e;                                       /* 0x5aa5a0 */
    e->rec = s->P + 0x1f0/4;
    curve_init(&e->torque, 9, e->rec[0], e->rec[2], e->rec + 4);
    curve_init(&e->brake, 3, e->rec[0], e->rec[2], e->rec + 22);
    curve_init(&e->nos, 9, e->rec[0], e->rec[1], MELLO_NOS_D);
    e->thrmul = 1; e->power = 1; e->heat = -1.0f;                     /* 0x7a2690 */
    Drive *d = &s->d;                                        /* 0x5af9d0 / 0x5ac7d0 */
    d->gb = s->P + 0x1b0/4;
    d->gear = d->target = 1; d->state = 2; d->req = 1;
    d->rear = d->gb[4]; d->rate = 1; d->win = 1;
    int n = GB_GEARS(d);
    if (n < 1 || n > 7) n = 6;
    d->up[1] = (e->rec[1] - e->rec[0]) * d->gb[0] + e->rec[0];
    for (int g = 2; g <= n; g++) {
        d->up[g] = e->rec[1] - 100.0f;
        float cur = GB_RATIO(d, g), nx = GB_RATIO(d, g + 1);
        d->down[g + 1] = fabsf(cur) <= 1e-5f ? 0 : (nx / cur) * d->up[g];
    }
    d->up[n + 1] = e->rec[2] + 500.0f;
    d->rndrange[0] = -100; d->rndrange[1] = 0;
    d->rndrange[2] = -100; d->rndrange[3] = 0;
    d->rndrange[4] = -100; d->rndrange[5] = 0;
    drive_rand(s);
    Aero *a = &s->a;
    a->rec = s->P + 0x260/4;
    a->speedscale = a->scale = a->on = a->surfdrag = 1;
    s->gripscale = 1;
    s->cs_wheels = 4; s->cs_tach = 1000.0f;
    s->mover = N2_MOVER_PHYSICS; s->catchup = 1;
    mello_init(s);
    return s;
}

void n2sim_set_mover(N2Sim *s, int mover)                               /* 0x5f46a0 */
{
    if (mover != N2_MOVER_MELLO && mover != N2_MOVER_TRAFFIC && mover != N2_MOVER_NIS && mover != N2_MOVER_FAR) mover = N2_MOVER_PHYSICS;
    if (mover == s->mover) return;
    float dg_w0 = len3(s->b.w); int dg_m0 = s->mover;   /* 11-15 diag */
    s->mover = mover;
    if (mover == N2_MOVER_MELLO) mello_enter(s);
    else if (mover == N2_MOVER_NIS) nis_enter(s);
    else if (mover == N2_MOVER_TRAFFIC) tm_enter(s);
    else if (mover == N2_MOVER_FAR) far_enter(s);
    else physics_enter(s);
    {   static int dg_n; float w1 = len3(s->b.w);
        if ((w1 > 3.0f || dg_w0 > 3.0f) && dg_n < 300) { dg_n++;
            printf("mover switch %d -> %d: |w| %.2f -> %.2f w (%.2f %.2f %.2f) k160 %.3f yawI %d\n", dg_m0, mover, dg_w0, w1,
                   s->b.w[0], s->b.w[1], s->b.w[2], PF(0x160), s->b.yawI_on); } }
}
int n2sim_mover(const N2Sim *s) { return s->mover; }
/* 0x402590's shift band: the full simulation's down-shift point (0x5946e0)
   and up-shift point (0x594670), or the Mellovator's tables (+0x1c0 /
   +0x19c), for the gear the car is in */
void n2sim_gear_band(const N2Sim *s, float *lo, float *hi)
{
    if (s->mover == N2_MOVER_MELLO || s->mover == N2_MOVER_NIS) {
        int g = s->m.gear < 0 ? 0 : s->m.gear > 9 ? 9 : s->m.gear;
        *lo = s->m.down[g]; *hi = s->m.up[g];
        return;
    }
    int g = s->d.gear < 0 ? 0 : s->d.gear > 9 ? 9 : s->d.gear;
    *lo = s->d.rnd[1] + s->d.down[g];
    *hi = drive_uprpm((N2Sim *)s, g, 0);
}

float n2sim_gear_up(const N2Sim *s, int g)
{
    g = g < 0 ? 0 : g > 9 ? 9 : g;
    return s->mover == N2_MOVER_MELLO || s->mover == N2_MOVER_NIS ? s->m.up[g] : s->d.up[g];
}

void n2sim_controls(N2Sim *s, const N2Controls *c)
{
    s->in_dt = s->in_dt > 0 ? s->in_dt : 1.0f / 60.0f;
    s->steer_u16 = c->steer;
    s->eff.throttle = c->throttle; s->eff.brake = c->brake; s->eff.handbrake = c->handbrake;
    s->eff.steer = 0;
    s->gear_req = c->gear_req;
    s->d.neutral = c->clutch_kick != 0;
    if (c->nitrous) nos_press(s); else nos_stop(s);
}
void n2sim_stop_nitrous(N2Sim *s) { nos_stop(s); }   /* 0x592500 */
void n2sim_controller_begin(N2Sim *s)
{
    s->held = 0;
    s->gear_req = s->trans == 1 || s->trans == 3 ? 2 : -1;
    memset(&s->eff, 0, sizeof s->eff);
}

void n2sim_set_perf(N2Sim *s, float level)
{
    if (!(level > 0)) level = 0;                             /* 0x7d7d3c */
    if (level > 10.0f) level = 10.0f;                        /* 0x784560 */
    s->m.perfk = (0.5f - 0.25f) * level * 0.1f + 0.25f;      /* Melov8r +0x144 */
}
void n2sim_set_catchup(N2Sim *s, float mult) { s->catchup = mult > 0 ? mult : 1.0f; }
void n2sim_set_drag(N2Sim *s, int drag, int klass, float shift_win, float clutch_rate, const float rnd[6])
{
    Drive *d = &s->d;
    s->drag = drag;
    d->klass = klass;
    d->win = shift_win;
    d->rate = clutch_rate;
    if (rnd) for (int i = 0; i < 6; i++) d->rndrange[i] = rnd[i];
    drive_rand(s);
}
void n2sim_set_race_state(N2Sim *s, int state) { s->race_state = state; }
void n2sim_set_streetx(N2Sim *s, int on) { s->streetx = on != 0; }
void n2sim_set_track_flags(N2Sim *s, unsigned flags) { s->trkflags = flags; }
float n2sim_engine_heat(const N2Sim *s) { return s->e.heat; }
int n2sim_engine_blown(const N2Sim *s) { return s->e.heat == 2.0f; }        /* 0x5942c0 */
float n2sim_shift_grade(const N2Sim *s, float rpm) { return shift_grade(s, rpm, s->d.gear, 0); }
float n2sim_last_shift(const N2Sim *s) { return s->d.q; }
float n2sim_shift_q(const N2Sim *s, float rpm, int gear) { return shift_grade(s, rpm, gear, 0); }
float n2sim_shift_win(const N2Sim *s, int gear)                             /* 0x594f00 */
{
    if (gear < 0 || gear > 8) return 0.0f;
    int k = s->d.klass < 0 ? 0 : s->d.klass > 3 ? 3 : s->d.klass;
    return SHIFT_WIN[k][gear] * s->d.win;
}
int n2sim_shifting(const N2Sim *s) { return s->d.shifting != 0; }
int n2sim_shift_up(const N2Sim *s) { return s->d.shifting != 0 && s->d.target > s->d.gear; }
int n2sim_shift_cut(const N2Sim *s) { return 0.0f < s->d.shift_t; }
void n2sim_set_race(N2Sim *s, int driftlike, int ai, int difficulty)
{
    s->driftlike = driftlike; s->race_ai = ai; s->race_diff = difficulty;
    if (!driftlike) for (int i = 0; i < 4; i++) s->wh[i].grip2 = 1.0f;
}

/* 0x5b5ab0 + 0x5b4dc0: the acceleration the engine gives through the gears,
   sampled every 3 mph (64-point curve over 0..top) and summed while it beats
   the drag: the car's raw performance rating (PerfConfig +8). */
/* 0x5b5ab0: the PerfConfig's acceleration curve (+0x55c): the driving
   force over the car's mass from the torque curve through the gears, 64
   points over [0, top] (top returned) */
float n2sim_accel_curve(const N2Sim *s, float d64[64])
{
    const float *P = s->P;
    const float *er = P + 0x1f0 / 4, *gb = P + 0x1b0 / 4;
    float rf = P[0x1c0 / 4], fin = rf * P[0x1bc / 4] + (1.0f - rf) * P[0x1b8 / 4];
    float inv = 1.0f / ((1.0f - rf) * P[0x20 / 4] + rf * P[0x80 / 4]);
    float up[10] = { 0 }, down[10] = { 0 }, tab[0x5c] = { 0 };   /* 0x86b620 */
    Curve tq;
    curve_init(&tq, 9, er[0], er[2], er + 4);
    shift_points(gb, er, up, down);
    int ng = *(const int *)&gb[6], g = 2, n = 0;
    float v = 0;
    do {
        /* the torque is read at the rpm of the gear before a shift and
           multiplied by the new gear's ratio, as the original does */
        float rpm = inv * v * gb[8 + g] * fin * RPM;
        if (up[g] <= rpm) { g++; if (ng + 2 <= g) break; }
        tab[n++] = (curve(&tq, rpm) * gb[8 + g] * fin * inv) / P[0x110 / 4];
        v += 1.34108f;
    } while (n < 0x5c);
    if (n - 1 < 0x5c && n > 0) memset(tab + n - 1, 0, (size_t)(0x5c - (n - 1)) * sizeof *tab);
    if (n < 2 || !(v > 0)) { memset(d64, 0, 64 * sizeof *d64); return 0; }
    /* resampled to the PerfConfig's 64-point curve over [0, v] */
    Curve tc = { n, 0, v, (float)(n - 1) / v, tab };
    for (int k = 0; k < 64; k++) d64[k] = curve(&tc, ((float)k * v) / 63.0f);
    return v;
}
float n2sim_accel_rating(const N2Sim *s)
{
    const float *P = s->P;
    float d64[64];
    float v = n2sim_accel_curve(s, d64);
    if (!(v > 0)) return 0;
    /* 0x5b4dc0: integrated while the force beats the drag */
    Curve c64;
    c64.n = 64; c64.x0 = 0; c64.x1 = v; c64.inv = 63.0f / v; c64.d = d64;
    float sum = 0;
    for (float x = 0; x < v; x += 1.34108f) {
        float f = curve(&c64, x), dg = (x * P[0x260 / 4] * x) / P[0x110 / 4];
        if (f < dg) break;
        sum += (f - dg) * 1.34108f;
    }
    return sum;
}
float n2sim_param(const N2Sim *s, int off) { return s->P[off / 4]; }
float n2sim_brake(const N2Sim *s) { return s->eff.brake; }   /* controller +0x214 (0x5eb8f0) */
void n2sim_destroy(N2Sim *s) { free(s); }
#ifdef N2SIM_DEBUG
#include <stdio.h>
void n2sim_debug(const N2Sim *s)
{
    printf("  rest %d grav %.1f F %.2f %.2f %.2f p %.3f %.3f %.3f | susp", s->rest, s->b.gravity,
           s->b.lastF[0], s->b.lastF[1], s->b.lastF[2], s->b.p[0], s->b.p[1], s->b.p[2]);
    for (int i = 0; i < 4; i++) printf(" [x %.3f f %.2f pre %.2f c%d]", s->su[i].x, s->su[i].force, s->su[i].preload, s->su[i].contact);
    printf(" | dt st %d gear %d tgt %d eng %.2f%c", s->d.state, s->d.gear, s->d.target, s->d.engage, 10);
}
#endif

/* ---- player controller ---------------------------------------------------
   The retail player controller, 0x41e2c0, once per frame before the car
   steps: the pedal layer 0x407a30 turns the raw pedals into the throttle,
   brake and handbrake the car reads, and asks for reverse; then the pad
   steering 0x41dfc0 -> 0x416bb0 (0x405dc0 maximum angle, 0x4060e0 rate)
   moves the wheels; then the clutch-pedal box (modes 3/4, 0x594910) and
   the drift clutch kick (0x401950). The event-mode branches that matter
   offline are the drag ones (launch boost, neutral rate limit) and the
   drift ones (steering tables, clutch kick); street X has none here. The
   0x89e7e2 / 0x890100 / 0x890110 branches and the 0x5f00f0 race-end coast
   are online-only (see n2sim_input). Not ported: the steering-wheel path
   0x4061c0, force feedback 0x406460, hydraulics 0x407400 (in main.c),
   sport mode 2 (request -2). */

static void avg_init(Avg *a, float window)                              /* 0x579f80 */
{
    memset(a, 0, sizeof *a);
    a->window = window;
    a->n = (int)(window * 120.0f + 0.5f);
}
static void avg_push(Avg *a, float v, float t)                          /* 0x576aa0 */
{
    if (a->val[a->idx] == 0 && a->t[a->idx] == 0) a->count++;
    else a->sum -= a->val[a->idx];
    a->sum += v;
    a->val[a->idx] = v; a->t[a->idx] = t;
    while (t - a->t[a->oldest] > a->window) {
        if (a->t[a->oldest] > 0) {
            a->sum -= a->val[a->oldest];
            a->val[a->oldest] = 0; a->t[a->oldest] = 0;
            a->count--;
        }
        if (++a->oldest >= a->n) a->oldest = 0;
    }
    a->avg = a->sum / (float)a->count;
    if (++a->idx >= a->n) a->idx = 0;
}

/* 0x5b5930 GetTheoreticalTopSpeed: the first 1.34108 m/s step at which
   drag/mass outgrows the drive acceleration table, capped by the record's
   limiter (mph). The table is RidePhysicsInfo+0x55c (64 points), built by
   0x5b5ab0 inside RebuildPhysicsInfo 0x5b4dc0: */
static float top_speed(N2Sim *s)
{
    const Drive *d = &s->d; const float *er = s->e.rec;
    const float *P = s->P;
    float shift[16] = { 0 }, down[16];
    int gears = GB_GEARS(d), g;
    if (gears < 1 || gears > 7) gears = 6;
    shift_points(d->gb, er, shift, down);              /* 0x5ac7d0 */
    /* 0x5b5ab0 */
    Curve tq; curve_init(&tq, 9, er[0], er[2], er + 4);   /* raw record curve, no power scale */
    float r = d->gb[4], fr = 1.0f - r;
    float fin = r * d->gb[3] + fr * d->gb[2];
    float invR = 1.0f / (fr * P[0x20/4] + r * P[0x80/4]);
    float mass = P[0x110/4];
    static float tmp[0x5c];
    float v = 0;
    int k = 0;
    g = 2;
    do {
        float rpm = invR * v * GB_RATIO(d, g) * fin * RPM;
        if (shift[g] <= rpm) {          /* torque at this rpm, through the next gear */
            g++;
            if (g >= gears + 2) break;
        }
        k++;
        tmp[k - 1] = curve(&tq, rpm) * GB_RATIO(d, g) * fin * invR / mass;
        v += 1.3410819f;
    } while (k < 0x5c);
    for (int i = k - 1; i < 0x5c; i++) tmp[i] = 0;   /* the last entry written is cleared too */
    Curve tc = { k, 0, v, (float)(k - 1) / v, tmp };
    static float tab[64];
    for (int i = 0; i < 64; i++) tab[i] = curve(&tc, (float)i * v / 63.0f);
    Curve ac = { 64, 0, v, 63.0f / v, tab };
    /* 0x5b5930 */
    float drag = s->a.rec[0], x = ac.x0;
    if (x < ac.x1)
        while (x * drag * x / mass <= curve(&ac, x) && (x += 1.3410819f) < ac.x1) {}
    if (P[0x290/4] > 0) {
        float lim = P[0x290/4] * 0.44702727f;
        if (lim < x) return lim;
    }
    return x;
}

/* 0x405dc0: the largest wheel angle the stick may ask for, degrees. Shrinks
   with speed, opens up off the throttle or on the brakes, is held while the
   stick stays past half, is cut for a second after a crash at speed, and in
   a slide lets the driver counter-steer up to the slide angle. */
static float steer_max(N2Sim *s, float x)
{
    static const float MAXA[10] = { 40,20,10,5,3.5f,3.1f,2.9f,2.9f,2.9f,2.9f };      /* 0x7e9ed8 */
    static const float OPEN[10] = { 1,0.92f,0.78f,0.64f,0.54f,0.48f,0.42f,0.38f,0.34f,0.32f }; /* 0x7e9f14 */
    static const float HELD[6]  = { 1,1,1.1f,1.2f,1.25f,1.35f };                      /* 0x7e9f00 */
    static const float CSTR[5]  = { 0.5f,0.6f,0.7f,0.8f,0.95f };                      /* 0x7e9eb0 */
    static const float CSTRD[5] = { 0.2f,0.3f,0.5f,0.7f,1.0f };                       /* 0x7e9ec4 drift */
    static const float HIT[8]   = { 0,0.2f, 0.2f,0.5f, 0.5f,0.7f, 0.7f,1 };          /* 0x7e9f58 */
    static const Curve CA = { 10, 0, 160, 0.05625f, MAXA }, CO = { 10, 0, 160, 0.05625f, OPEN },
                       CH = { 6, 0, 1, 5, HELD }, CC = { 5, 0, 1, 4, CSTR }, CD = { 5, 0, 1, 4, CSTRD };
    const Body *b = &s->b;
    float v = s->cs_speed;
    float m = curve(&CA, v);
    float thr = s->eff.throttle, hb = s->eff.handbrake, br = s->eff.brake;
    m *= (1.0f - ((thr + 1.0f) - (hb + br) * 0.5f) * 0.5f) * curve(&CO, v) + 1.0f;
    m *= curve(&CH, fabsf(s->st_in.avg));
    if (s->hit_time > 0 && !s->driftlike) {  /* carstate+0x3c -> +0x140 */
        float t = (float)(s->time - s->hit_time);
        if (t < 1.0f) {
            float r = v / (s->vtop * 0.7f), g = graph(HIT, 4, t);
            if (!(1.0f >= r)) r = 1.0f;
            m *= (1.0f - r) * (1.0f - g) + g;
        }
    }
    float slip = 0;                           /* 0x59fa10, degrees */
    if (s->cs_speed >= 1.0f) {
        slip = atan2f(b->R[1], b->R[0]) - atan2f(b->v[1], b->v[0]);
        while (slip > PI_F) slip -= 2*PI_F;
        while (slip <= -PI_F) slip += 2*PI_F;
        slip *= 57.2957764f;
    }
    float aslip = fabsf(slip), vel = len3(b->v);
    if (s->sliding) { if (aslip < 5.0f || vel < 0.0f) s->sliding = 0; }
    else if (aslip > 15.0f && vel >= 13.4108181f) s->sliding = 1;
    if (x * slip < 0 && (s->sliding || aslip > m)) {
        float k = curve(s->driftlike ? &CD : &CC, fabsf(x));
        m = aslip * k;
        if (m > k * 45.0f) m = k * 45.0f;
    } else if (fabsf(s->st_in.avg) >= 0.5f && m <= s->st_max) m = s->st_max;
    if (m >= 45.0f) m = 45.0f;
    s->st_max = m;
    return m;
}

/* 0x4060e0: how fast the wheels may turn, degrees a second -- quicker the
   faster the stick has just been moved and the further it is held */
static float steer_rate(N2Sim *s, float x, float dt)
{
    static const float FLICK[6] = { 1,1.05f,1.1f,1.5f,2.2f,3.1f };                    /* 0x7e9f28 */
    static const float OPEN[10] = { 1,0.92f,0.78f,0.64f,0.54f,0.48f,0.42f,0.38f,0.34f,0.32f }; /* 0x7e9f14 */
    static const float HELD[6]  = { 1,1.05f,1.1f,1.2f,1.3f,1.4f };                    /* 0x7e9f3c */
    static const Curve CF = { 6, 0, 10, 0.5f, FLICK }, CO = { 10, 0, 160, 0.05625f, OPEN },
                       CH = { 6, 0, 1, 5, HELD };
    avg_push(&s->st_flick, curve(&CF, fabsf((x - s->st_prev) / dt)), s->ctl_t);
    float k = s->driftlike ? 110.0f : 180.0f;                          /* 0x783ae8 / 0x783ae4 */
    return curve(&CO, s->cs_speed) * k * s->st_flick.avg * curve(&CH, fabsf(s->st_in.avg));
}

/* 0x416bb0: pad steering. The stick goes through the response table, the
   wheels head for stick x maximum angle x the car's steering gain (P+0x270),
   at most the rate above, and the result is the 16-bit wheel angle. */
static void steer_pad(N2Sim *s, float stick, float dt)
{
    static const float PAD[21] = { -1,-0.712f,-0.453f,-0.303f,-0.216f,-0.148f,-0.116f,-0.08f,-0.061f,-0.034f,
                                   0, 0.034f,0.061f,0.08f,0.116f,0.148f,0.216f,0.303f,0.453f,0.712f,1 }; /* 0x7e9e60 */
    static const float PAD4[21] = { -1,-1,-0.688f,-0.492f,-0.319f,-0.228f,-0.16f,-0.123f,-0.085f,-0.05f,
                                    0, 0.05f,0.085f,0.123f,0.16f,0.228f,0.319f,0.492f,0.688f,1,1 };      /* 0x7e9e9c (event modes) */
    static const float YAWACC_G[12] = { 0,1, 1,1.1f, 2,1.2f, 3,1.25f, 4,1.3f, 5,1.35f };   /* 0x7e9f50 */
    static const Curve CP = { 21, -1, 1, 10, PAD }, CP4 = { 21, -1, 1, 10, PAD4 };
    float x = curve(s->driftlike ? &CP4 : &CP, stick);
    float cur = (float)s->steer_u16 * 0.0054931640625f;
    if (cur >= 180.0f) cur -= 360.0f;
    float gain = PF(0x270);
    if (s->driftlike && 0 < x * s->cs_yawacc) gain *= graph(YAWACC_G, 6, fabsf(s->cs_yawacc));
    float t = steer_max(s, x) * gain * x;
    if (t <= -45.0f) t = -45.0f; else if (!(45.0f >= t)) t = 45.0f;
    float r = steer_rate(s, x, dt) * gain * dt;
    float a = cur - r;
    if (t > a) a = t;
    if (!(cur + r >= a)) a = cur + r;
    s->steer_u16 = (unsigned short)((int)(a * 65536.0f) / 360);
    avg_push(&s->st_in, x, s->ctl_t);          /* 0x41dfc0 */
    s->st_prev = x;
}

void n2sim_collision(N2Sim *s) { s->hit_time = s->time; }
void n2sim_ground_exact(N2Sim *s, int on) { s->ground_exact = on; }

/* 0x594910 DriveTrain::SetManualClutchEngaged: the clutch pedal (1 up,
   0 floored) drives the clutch state directly: past 0.95 engaged (3), under
   0.05 out (1), else coming up (2) or going down (0) by its direction */
static void clutch_pedal(N2Sim *s, float c)
{
    Drive *d = &s->d;
    d->mode = 3;
    if (0.95f < c) { d->state = 3; d->engage = 1.0f; return; }           /* 0x78a7dc */
    if (c < 0.05f) { d->state = 1; d->engage = 0; return; }              /* 0x7844f4 */
    if (c == 1.0f) { d->state = 3; d->engage = c; return; }
    if (c == 0) { d->state = 1; d->engage = c; return; }
    d->state = d->engage < c ? 2 : 0;
    d->engage = c;
}
/* 0x407310 RealDriver::SetLowGear (vt+0x30): manual and clutch-pedal
   boxes take first (a drag race starts the 0.25 s launch boost), any other
   mode drives (-1) */
static void low_gear(N2Sim *s)
{
    if (s->trans != 1 && s->trans != 3) { s->gear_req = -1; return; }
    s->gear_req = 2;
    if (s->drag) s->launch = 1000;                                      /* (int)(0.25 * 4000 + 0.5) */
}

void n2sim_input(N2Sim *s, const N2Input *in)
{
    static const float NGAS[12] = { 0,0, 0.575f,0.437f, 0.797f,0.62f, 0.894f,0.746f, 0.95f,0.866f, 1,1 }; /* 0x7ea004 */
    Drive *d = &s->d;
    float dt = s->in_dt > 0 ? s->in_dt : 1.0f / 60.0f;
    s->in = *in;
    s->ctl_t = (float)s->time + 1.0f;
    /* transmission mode, 0x407280 RealDriver::SetTransmission: manual (1)
       and clutch pedal (3) start in the gear the car is in, auto -1; the
       drivetrain's +0x60 takes the mode. 4 is the clutch-pedal box handing
       a shift into or out of R/N to the clutch FSM (0x41e2c0 below). */
    int mode = in->manual == 3 ? 3 : in->manual ? 1 : 0;
    if (mode != (s->trans == 4 ? 3 : s->trans)) {
        s->trans = mode; s->gear_req = mode ? d->gear : -1;
        if (s->mover == N2_MOVER_PHYSICS) d->mode = mode;
    }
    /* clutch pedal +0x2f4 (0x408180): digital press 0 / release 1; analog
       1 - min(v * 1.07, 255) / 255 (0x783b1c, 0x784548) */
    {   float c = in->clutch_pedal * 1.07f;
        s->clutch = 1.0f - (c < 1.0f ? (c > 0 ? c : 0) : 1.0f); }
    /* shift buttons, 0x407fc0 / 0x4080b0. Manual: never out of R, never
       below N, R comes and goes with the pedals. Clutch pedal: from the gear
       the car is in, never out of R, up to top, down to N. Nothing in mode 4
       (or 2, sport, not ported). */
    if (s->trans == 1 && in->shift > 0 && s->gear_req != 0 && s->gear_req < GB_GEARS(d) + 1) s->gear_req++;
    if (s->trans == 1 && in->shift < 0 && s->gear_req != 0 && s->gear_req >= 2) s->gear_req--;
    if (s->trans == 3 && in->shift > 0 && s->gear_req != 0) s->gear_req = d->gear < GB_GEARS(d) + 1 ? d->gear + 1 : d->gear;
    if (s->trans == 3 && in->shift < 0 && s->gear_req != 0) s->gear_req = d->gear >= 2 ? d->gear - 1 : d->gear;
    /* raw pedals (0x416de0: in neutral the gas pedal is eased in) */
    float gas = in->throttle, brk = in->brake, hbr = in->handbrake;
    if (d->gear == 1) gas = graph(NGAS, 6, gas);
    /* 0x407a30 pedal layer */
    float spd = s->cs_speed, gasv = gas, hb = hbr;
    s->hbtimer -= dt;
    if (s->hbtimer <= 0 || fabsf(spd) <= 8.940545f) s->hbtimer = 0;
    else { gasv = 0; hb = 1; }
    if (brk <= 0.6f || !(gas < 0.5f)) { if (spd > -2.0f) s->rflag = 0; }
    else s->rflag = 1;                        /* brake alone: the driver wants to reverse */
    float th, bk, rv;
    if (spd > 0.5f || !s->rflag) { th = gasv; bk = brk; rv = 0; }
    else if (spd >= -2.0f) { th = gasv; bk = 0; rv = brk; }
    else { th = 0; bk = gasv; rv = brk; }    /* reversing: the gas pedal brakes */
    rv += rv;
    if (rv > 1) rv = 1;
    s->eff.brake = bk; s->eff.handbrake = hb;
    /* drag: for 0.25 s after the launch (+0x2cc) the throttle is twice the
       pedal, at least 0.7 (0x7844d8) */
    if (s->drag && s->launch > 0) {
        s->launch -= (int)(dt * 4000.0f);                               /* 0x577310 */
        if (s->launch < 0) s->launch = 0;
        if (s->launch > 0) { float t2 = 2.0f * th; th = 0.7f < t2 ? (1.0f < t2 ? 1.0f : t2) : 0.7f; }
    }
    if (th + 0.2f < rv) { s->eff.throttle = rv; s->gear_req = 0; }
    else {
        if (s->drag && d->gear == 1) {
            /* drag, in neutral: the throttle follows the pedal at +1 / -1.5
               a second (0x783b38 / 0x783b3c), and with the pedal past 0.6
               below idle + 1000 rpm (0x784550) it holds 0.6 */
            float cur = s->eff.throttle, hi = cur + 1.0f * dt, lo = cur - 1.5f * dt;
            if (lo < th) lo = th;
            th = hi < lo ? hi : lo;
            if (0.6f < gas && s->cs_tach < s->e.rec[0] + 1000.0f && th < 0.6f) th = 0.6f;
        }
        s->eff.throttle = th > 0 ? (th > 1 ? 1 : th) : 0;
        if (s->gear_req == 0) low_gear(s);                              /* vt+0x30, 0x407310 */
    }
    /* 0x407e5f: 0x5f00f0 (0x89cf69 / 0x8669f4 / 0x866a14, the online game
       flags) && *0x8900d8 == 6 -> throttle 0, brake 0.5, the online race-end
       coast. Never true offline (and not in the GC build): not ported. The
       0x89e7e2 / 0x890100 / 0x890110 branches above (speed / (|+0x118| * 4
       + 1), throttle curve, rate limit) are dead offline too: 0x89e7e2 is
       set only by 0x526720, quick race type 11, which only a network packet
       (0x601d20) selects. */
    s->eff.steer = in->steer;
    if (in->nitrous) nos_press(s);      /* 0x420877 press; release stops (0x42ad55) */
    else nos_stop(s);
    if (in->steer_ai) { s->steer_u16 = in->steer_u16; s->eff.steer = 0; }
    else steer_pad(s, in->steer, dt);
    /* +0x2b4, the start-line hold: neutral and full brake, so the gas only
       revs the engine; on release the box takes first (the drop-in is the
       launch). GO = SetStartLineState(false) (0x410430, vt+0x1c) ->
       SetLowGear (vt+0x30): auto -1, manual / clutch pedal 1st. */
    if (in->hold) { s->gear_req = 1; s->eff.brake = 1.0f; s->held = 1; }
    else if (s->held) { s->held = 0; low_gear(s); }                     /* 0x410430 -> vt+0x30 */
    if (s->mover != N2_MOVER_PHYSICS) return;                           /* car +0x28 == 1 */
    /* 0x41e2c0, clutch-pedal box: a shift into or out of R / N (either gear
       below 2) goes to the clutch FSM (mode 4, drivetrain +0x60 = 0) until
       it is done (0x5ecd70: +0x48 shifting clear), then back to the pedal */
    if (s->trans == 3) {
        if (d->gear != s->gear_req && (d->gear < 2 || s->gear_req < 2)) { s->trans = 4; d->mode = 0; }
    } else if (s->trans == 4 && !d->shifting) { s->trans = 3; d->mode = 3; }
    if (s->trans == 3) { d->mode = 3; clutch_pedal(s, s->clutch); return; }   /* 0x594910(+0x2f4) */
    /* 0x401950: in a drift-type race (!drag && !0x890108 && (0x890114 ||
       drift || 0x890104)) the drivetrain's clutch kick (+0x74) follows the
       handbrake past 0.6 */
    if (s->driftlike) s->d.neutral = 0.6f < s->eff.handbrake;          /* 0x784564 */
}

void n2sim_reset(N2Sim *s, const float pos[3], float heading)
{
    Body *b = &s->b;
    float c = cosf(heading), sn = sinf(heading);
    ident(b->R);
    b->R[0] = c; b->R[1] = sn; b->R[4] = -sn; b->R[5] = c;
    {   /* stand on the ground plane under the car, not level */
        float gz, n[3]; int surf;
        if (s->ground && s->ground(s->user, pos[0], pos[1], pos[2] + 0.5f, &gz, n, &surf) && n[2] > 0.5f) {
            float f[3] = { c, sn, 0 }, k = f[0]*n[0] + f[1]*n[1];
            f[0] -= k * n[0]; f[1] -= k * n[1]; f[2] -= k * n[2];
            float l = len3(f); f[0] /= l; f[1] /= l; f[2] /= l;
            float lft[3]; cross(lft, n, f);
            memcpy(b->R, f, 12); memcpy(b->R + 4, lft, 12); memcpy(b->R + 8, n, 12);
        }
    }
    /* body origin sits so that the wheels rest at zero compression */
    float wz = 0;
    for (int i = 0; i < 4; i++) wz += s->wh[i].pr[2] - s->wh[i].pr[4];
    wz *= 0.25f;
    s->base = -wz;
    float o[3];
    vmul(o, b->com, b->R);
    for (int i = 0; i < 3; i++) b->pos[i] = pos[i] + o[i] - b->R[8+i] * wz;
    memset(b->p, 0, 16); memset(b->L, 0, 16); memset(b->v, 0, 12); memset(b->w, 0, 12);
    memset(b->F, 0, 12); memset(b->T, 0, 12);
    b->settle = 0;
    /* (port) the car state heading follows the new pose: the movers that
       derive their yaw rate from it (traffic 0x408820, far route) otherwise
       read the old place's heading -- a placed traffic car got up to pi/dt
       (~188 rad/s) of yaw, kept on the switch to the Mellovator near the
       player, and spun itself into the sky (11-15 play test, hwlog72) */
    s->cs_head = (unsigned short)(int)lrintf(atan2f(b->R[1], b->R[0]) * RAD2U16);
    s->steer_u16 = 0; s->sliding = 0;
    avg_init(&s->st_in, 0.55f); avg_init(&s->st_flick, 0.15f);    /* 0x405c60 */
    s->st_max = 45.0f; s->st_prev = 0; s->hbtimer = 0; s->rflag = 0; s->hit_time = 0;
    s->dampen = 0; s->dampen_on = 0; s->dampen_t = 0; s->draft = 0; s->nos_spray = 0;
    s->trans = 0; s->gear_req = -1; s->held = 0; s->launch = 0;
    s->clutch = 1.0f; s->d.mode = 0;                                    /* ctor: +0x2f4 = 1 */
    memset(&s->in, 0, sizeof s->in); s->eff = s->in;
    s->vtop = top_speed(s);
    for (int i = 0; i < 4; i++) {
        Wheel *w = &s->wh[i];
        w->w = 0; w->angle = 0; w->torque = 0; w->Fx = w->Fy = 0; w->tsmooth = 0;
        w->locked = 0; w->dir = 1; w->contact = 1; s->cs_contact[i] = 1;
        s->su[i].x = 0; s->su[i].xv = 0;
    }
    s->e.w = s->e.rec[0] * RPM2RAD; s->e.load = 0; s->e.limiter = 0;
    s->d.gear = 2; s->d.target = 2; s->d.state = 3; s->d.engage = 1; s->d.shifting = 0;
    s->cs_wheels = 4; s->cs_speed = s->cs_absv = 0; s->cs_tach = s->e.rec[0];
    s->rest = 0; s->hbramp = 1;
    s->h = 0;
    for (int i = 0; i < 4; i++) s->gc[i].valid = 0;
    frame_setup(s, 0);                 /* mounts + preloads before the first probe */
    post_all(s);
    for (int i = 0; i < 4; i++) s->su[i].xv = 0;
    force_pass(s);
    if (s->mover == N2_MOVER_MELLO) {
        for (int i = 0; i < 4; i++) s->m.compv[i] = 0;
        mello_enter(s);
    } else if (s->mover == N2_MOVER_NIS) {
        for (int i = 0; i < 4; i++) s->m.compv[i] = 0;
        nis_enter(s);
    }
}

/* Car::ResetDampenMode 0x5fc050 (each Car::Move): crash mode ends after
   0.1 s (0x7a565c) on four wheels with no live Collision; mode 2 ends with
   four wheels down and none; mode 0 -> 2 while the body has one (car +0x3c
   -> +0x18 > 0, s->body_colls, 0x5fc0c6). The original times from the clock
   (+0x54c, 0 = unset); this sums the steps. */
static void reset_dampen(N2Sim *s, float dt)
{
    if (s->dampen == 0) { if (s->body_colls > 0) s->dampen = 2; }
    else if (s->dampen == 2) { if (s->body_colls == 0 && s->cs_wheels == 4) s->dampen = 0; }
    else if (s->dampen == 1) {
        if (s->body_colls != 0 || s->cs_wheels != 4) s->dampen_on = 0;
        else if (!s->dampen_on) { s->dampen_on = 1; s->dampen_t = 0; }
        else if ((s->dampen_t += dt) >= 0.1f) s->dampen = 0;
    }
}
/* Car::SetDampenMode 0x5f6be0 */
void n2sim_set_dampen(N2Sim *s, int mode)
{
    s->dampen = mode;
    if (mode == 1) s->dampen_on = 0;
}
int n2sim_dampen(const N2Sim *s) { return s ? s->dampen : 0; }
void n2sim_set_body_collisions(N2Sim *s, int n) { if (s) s->body_colls = n; }
int n2sim_drag(const N2Sim *s) { return s && s->drag; }

/* 0x5ebc20: the draft car o gives s -- o at 1 m/s or more (car +0x430),
   within the graph 0x89cf30's distance for its speed (0 below 1 m/s, 10 m at
   5, 60 m at 150; never past 60) and inside the 0.95 cone behind its travel
   (0x7a55b8); then the curve 0x8023f0 over the distance's share */
static float draft_from(const N2Sim *s, const N2Sim *o)
{
    static const float DIST[8] = { 0, 0, 1, 0, 5, 10, 150, 60 };          /* 0x8023d0 */
    static const float FALL[6] = { 1.0f, 0.9f, 0.75f, 0.55f, 0.3f, 0 };
    static const Curve FC = { 6, 0, 1, 5, FALL };                         /* 0x8023f0 */
    float sp = len3(o->b.v);
    if (sp < 1.0f) return 0;                                              /* 0x8023d8 */
    float d[3] = { o->b.pos[0] - s->b.pos[0], o->b.pos[1] - s->b.pos[1], o->b.pos[2] - s->b.pos[2] };
    float dist = len3(d), c = dist > 1e-5f ? dist : 1e-5f;
    if (60.0f < c) c = 60.0f;                                             /* 0x8023ec */
    if (dist != c) return 0;
    float f[3] = { o->b.v[0] / sp, o->b.v[1] / sp, o->b.v[2] / sp };
    float k = ((f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) / dist - 0.95f) / (1.0f - 0.95f);
    if (k < 0) return 0;
    float r = graph(DIST, 4, sp) * k;
    if (r < 1e-5f) return 0;
    float x = dist / r;
    return curve(&FC, x < 0 ? 0 : x > 1 ? 1 : x);
}
/* 0x5ebd80: the drafts of every other car in the world, at most 0.9
   (0x7a55bc); World::DoTimestep 0x609ef8 stores it in car +0x4dc before
   each car moves */
float n2sim_draft(const N2Sim *s, N2Sim *const *cars, int n)
{
    float sum = 0;
    for (int i = 0; i < n; i++) {
        if (!cars[i] || cars[i] == s) continue;
        sum += draft_from(s, cars[i]);
        if (!(sum < 0.9f)) return 0.9f;
    }
    return sum;
}
void n2sim_set_draft(N2Sim *s, float d) { s->draft = d; }
float n2sim_get_draft(const N2Sim *s) { return s ? s->draft : 0; }
/* Retail reads +0x330 in the same World::DoTimestep as the move that wrote it
   (GC 80210be0: DoTimestepMove, then Car::TriggerCarTerrainEffects 0x5f5730 per
   car), so every sim step's bump-stop hit is seen. The port's effects run once
   per drawn frame, after 1-2 steps: they take the largest step sum since the
   last take, then clear it; a hit nobody takes within 4 steps (no effects
   running: front end, off-screen car) expires. GUESS: two hits in one drawn
   frame fire once (at the larger), where retail would test each step. */
float n2sim_take_bottom(N2Sim *s)
{
    if (!s) return 0;
    float b = s->bottom_latch;
    s->bottom_latch = 0;
    return b;
}
/* MiscStyleManager 0x56f9b0's hydraulics inputs: the wheels whose bounce
   fired this step (car +0x408 = carstate +0x3c8, cleared at the step's
   start 0x5b72d0) and the wheels moving (0x5ed470: mover +0x74 in {2, 3},
   the full simulation only) */
void n2sim_hyd_counts(const N2Sim *s, int *bounced, int *moving)
{
    int b = 0, m = 0;
    for (int i = 0; s && i < 4; i++) {
        b += s->hyd_hit[i] != 0;
        m += s->mover == N2_MOVER_PHYSICS && (s->hyd_code[i] == 2 || s->hyd_code[i] == 3);
    }
    *bounced = b; *moving = m;
}

static float g_time_scale = 1.0f;                                      /* World +0x108 */
void  n2sim_set_time_scale(float scale) { g_time_scale = scale > 0 ? scale : 1.0f; }
float n2sim_time_scale(void) { return g_time_scale; }
void n2sim_step(N2Sim *s, float dt)                                    /* 0x5b72d0 */
{
    dt *= g_time_scale;
    if (dt <= 0) return;
    if (dt > 0.1f) dt = 0.1f;
    s->in_dt = dt;
    reset_dampen(s, dt);
    memset(s->hyd_hit, 0, sizeof s->hyd_hit);                          /* carstate +0x3c8[4] */
    for (int i = 0; i < 4; i++) s->gc[i].valid = 0;
    for (int i = 0; i < 4; i++) s->cs_bottom[i] = 0;          /* 0x5b730b */
    if (s->mover == N2_MOVER_MELLO) { mello_frame(s, dt); return; }
    if (s->mover == N2_MOVER_NIS) { nis_frame(s, dt); return; }
    if (s->mover == N2_MOVER_TRAFFIC) { tm_frame(s, dt); return; }
    if (s->mover == N2_MOVER_FAR) { far_frame(s, dt); return; }
    frame_setup(s, dt);
    int n = (int)(dt / HMIN) + 1;
    float h = dt / (float)n;
    for (int k = 0; k < n; k++) {
        s->h = h;
        s->time += h;
        substep(s, h);
        orthonormalize(s->b.R);
    }
    {   float bsum = s->cs_bottom[0] + s->cs_bottom[1] + s->cs_bottom[2] + s->cs_bottom[3];
        if (s->bottom_age < 8 && ++s->bottom_age > 4) s->bottom_latch = 0;   /* not taken for 4 steps: no drawn frame wants it */
        if (bsum > s->bottom_latch) { s->bottom_latch = bsum; s->bottom_age = 0; } }
    /* 0x5a5540: publish to the car state */
    Body *b = &s->b;
    s->cs_absv = len3xzy(b->v);
    s->cs_speed = (b->v[2]*b->R[2] + b->v[1]*b->R[1]) + b->v[0]*b->R[0];
    int nw = 0;
    for (int i = 0; i < 4; i++) {
        s->cs_contact[i] = s->wh[i].contact;
        s->cs_surf[i] = s->wh[i].surf;
        nw += s->wh[i].contact != 0;
    }
    s->cs_wheels = nw;
    s->cs_head = fast_atan(b->R[0], b->R[1]);   /* 0x5a562a */
    for (int i = 0; i < 3; i++) { s->cs_v[i] = b->v[i]; s->cs_w[i] = b->w[i]; }
    s->cs_yawacc = (b->w[2] - s->cs_wz_prev) / dt;                      /* 0x604740 */
    s->cs_wz_prev = b->w[2];
    float rpm = s->e.w * RPM;
    int up = s->d.shifting && s->d.target > 2;
    float fall = -TACH_FALL[up * 4] / s->e.rec[3];
    if ((rpm - s->cs_tach) / dt < fall && rpm <= fall * dt + s->cs_tach) rpm = fall * dt + s->cs_tach;
    s->cs_tach = rpm * 0.55f + s->cs_tach * 0.45f;
}

void n2sim_state(const N2Sim *s, N2State *o)
{
    const Body *b = &s->b;
    float c[3];
    vmul(c, b->com, b->R);
    for (int i = 0; i < 3; i++) {
        o->pos[i] = b->pos[i] - c[i] - b->R[8+i] * s->base;
        o->vel[i] = b->v[i]; o->angvel[i] = b->w[i];
        o->rot[i] = b->R[i]; o->rot[3+i] = b->R[4+i]; o->rot[6+i] = b->R[8+i];
    }
    o->heading = atan2f(b->R[1], b->R[0]);
    o->speed = s->cs_speed;
    o->rpm = s->e.w * RPM; o->rpm_tach = s->cs_tach;
    o->throttle = s->e.thr;
    o->nos_frac = nos_fraction(s); o->nos_on = s->nos_stage > 0; o->nos_cap = s->nos_cap;
    o->gear = s->d.gear; o->gears = GB_GEARS(&s->d);
    o->wheels_down = s->cs_wheels;
    o->bottom = s->cs_bottom[0] + s->cs_bottom[1] + s->cs_bottom[2] + s->cs_bottom[3];
    o->engaged = s->mover != N2_MOVER_PHYSICS || s->d.state == 3;   /* 0x5a592b; MellowMover 0x5a2da1: 1 */
    o->vtop = s->vtop;
    o->mover = s->mover;
    if (s->mover == N2_MOVER_MELLO || s->mover == N2_MOVER_TRAFFIC || s->mover == N2_MOVER_NIS || s->mover == N2_MOVER_FAR) {
        const Mello *m = &s->m;
        o->rpm = o->rpm_tach = m->rpm;
        o->throttle = m->thr;
        o->gear = m->gear;
        float sa = (float)m->steer * U16RAD;
        if (sa > PI_F) sa -= 2 * PI_F;
        for (int i = 0; i < 4; i++) {
            const float *wr = s->P + (0x10 + 0x30 * i) / 4;
            const float *k = s->P + (0x170 + 0x20 * (i / 2)) / 4;
            float c = m->comp[i] < k[7] ? m->comp[i] : k[7];     /* drawn travel, 0x5a2b60 */
            float l[3] = { wr[0] - b->com[0], wr[1] - b->com[1], wr[2] - b->com[2] + c }, r[3], v[3];
            vmul(r, l, b->R);
            cross(v, b->w, r);
            v[0] += b->v[0]; v[1] += b->v[1]; v[2] += b->v[2];
            o->wheel_spin[i] = m->angle[i];
            o->wheel_w[i] = m->spin[i];
            {
                float st = i < 2 ? sa : 0, cs = cosf(st), sn = sinf(st);
                float *M = o->wheel_mat[i];
                for (int j = 0; j < 3; j++) {
                    M[j] = cs * b->R[j] + sn * b->R[4 + j];
                    M[4 + j] = -sn * b->R[j] + cs * b->R[4 + j];
                    M[8 + j] = b->R[8 + j];
                }
                M[3] = M[7] = M[11] = 0; M[15] = 1;
            }
            o->wheel_steer[i] = i < 2 ? sa : 0;
            o->wheel_comp[i] = c;
            o->wheel_slip[i] = m->sliding[i] ? 1.0f : 0.0f;
            o->wheel_lslip[i] = m->sv[i][0];
            o->wheel_load[i] = m->load[i];
            o->wheel_contact[i] = m->contact[i];
            for (int j = 0; j < 3; j++) {
                o->wheel_pos[i][j] = b->pos[j] + r[j];
                o->wheel_cpt[i][j] = o->wheel_pos[i][j] - m->n[i][j] * wr[4];
            }
            for (int j = 0; j < 3; j++) o->wheel_mat[i][12 + j] = o->wheel_pos[i][j];
            o->wheel_sliding[i] = m->sliding[i];
            o->wheel_surface[i] = m->surf[i];
            o->wheel_slipv[i][0] = m->sv[i][0]; o->wheel_slipv[i][1] = m->sv[i][1];
            o->wheel_skid[i] = o->wheel_skid_w[i] = 0;
            /* the tyre-mark rule of 0x59dbb0 on what the Mellovator hands its
               wheel objects: sliding (+0x160) only, no grip ratio */
            float lat[3] = { b->R[4], b->R[5], b->R[6] };
            o->wheel_latv[i] = fabsf(lat[0] * v[0] + lat[1] * v[1] + lat[2] * v[2]);
            float vl = sqrtf(v[0] * v[0] + v[1] * v[1]);
            if (m->sliding[i] && m->contact[i] && vl > 1.0f) {
                float l2 = fabsf((lat[0] * -v[1] + lat[1] * v[0]) / vl);
                o->wheel_skid_w[i] = wr[5] * SURF(m->surf[i])->skid_width * (l2 < 0.5f ? 0.5f : l2);
                o->wheel_skid[i] = l2 < 0.3f ? 0.3f : l2;
            }
        }
        return;
    }
    for (int i = 0; i < 4; i++) {
        const Wheel *w = &s->wh[i];
        o->wheel_spin[i] = w->angle;
        o->wheel_w[i] = w->w;
        memcpy(o->wheel_mat[i], w->world, sizeof w->world);
        o->wheel_steer[i] = i < 2 ? atan2f(s->steer_m[i][1], s->steer_m[i][0]) : 0;
        o->wheel_comp[i] = s->su[i].x;
        o->wheel_slip[i] = w->sratio;
        o->wheel_lslip[i] = w->slip2;
        o->wheel_load[i] = w->load;
        o->wheel_contact[i] = w->contact;
        o->wheel_pos[i][0] = w->world[12]; o->wheel_pos[i][1] = w->world[13]; o->wheel_pos[i][2] = w->world[14];
        memcpy(o->wheel_cpt[i], w->cpt, sizeof w->cpt);
        o->wheel_sliding[i] = w->sliding;
        o->wheel_surface[i] = w->surf;
        o->wheel_slipv[i][0] = w->slipv[0]; o->wheel_slipv[i][1] = w->slipv[1];
        o->wheel_skid[i] = o->wheel_skid_w[i] = 0;
        if (s->dyno) {   /* 0x5a5833: on the dyno no friction use / slip; 0x5b74e9: no tyre marks */
            o->wheel_slip[i] = 0; o->wheel_slipv[i][0] = o->wheel_slipv[i][1] = 0;
            continue;
        }
        float vl = sqrtf(w->vel[0] * w->vel[0] + w->vel[1] * w->vel[1]);
        o->wheel_latv[i] = fabsf(w->world[4] * w->vel[0] + w->world[5] * w->vel[1] + w->world[6] * w->vel[2]);
        if ((w->sliding || w->sratio * w->load > 8.0f) && w->contact && vl > 1.0f) {
            /* how square the patch moves across the axle: 1 rolling or
               spinning, 0 sliding sideways (wheel lateral axis = world row 1) */
            float l = fabsf((w->world[4] * -w->vel[1] + w->world[5] * w->vel[0]) / vl);
            o->wheel_skid_w[i] = w->pr[5] * SURF(w->surf)->skid_width * (l < 0.5f ? 0.5f : l);  /* +-half of this */
            if (!w->sliding) l *= 0.333333f;
            o->wheel_skid[i] = l < 0.3f ? 0.3f : l;
        }
    }
}

void n2sim_set_position(N2Sim *s, const float pos[3])
{
    float c[3];
    vmul(c, s->b.com, s->b.R);
    for (int i = 0; i < 3; i++) s->b.pos[i] = pos[i] + c[i] + s->b.R[8+i] * s->base;
}
void n2sim_set_velocity(N2Sim *s, const float vel[3])
{
    for (int i = 0; i < 3; i++) { s->b.v[i] = vel[i]; s->b.p[i] = vel[i] * s->b.mass; }
}
/* a traffic car put down (0x604260 / 0x5f52c0) on the TrafficMover: its
   point, heading and speed along the heading */
void n2sim_traffic_place(N2Sim *s, const float pos[3], float heading, float speed)
{
    n2sim_reset(s, pos, heading);
    float v[3] = { cosf(heading) * speed, sinf(heading) * speed, 0 };
    n2sim_set_velocity(s, v);
    s->cs_speed = speed;
    s->cs_absv = fabsf(speed);
    if (s->mover == N2_MOVER_TRAFFIC) tm_enter(s);
    else n2sim_set_mover(s, N2_MOVER_TRAFFIC);
    s->tm_s540 = s->tm_v;
}

int n2sim_nis_event(N2Sim *s, int what, float a, float b)
{
    /* hydraulics go to the car's control on any mover: wheel a, value b
       (car +0x30 -> +0x1ec / +0x1fc [wheel]) */
    if (what == N2_NISEV_HYD_BOUNCE || what == N2_NISEV_HYD_EXTEND) {
        int w = (int)a;
        if (w < 0 || w > 3) return 0;
        (what == N2_NISEV_HYD_BOUNCE ? s->hyd_bounce : s->hyd_extend)[w] = b;
        return 1;
    }
    if (s->mover != N2_MOVER_NIS) return 0;
    if (what == N2_NISEV_BRAKE_LOCK) s->nis_fspin = a;                   /* 0x598ed0 */
    else if (what == N2_NISEV_BURNOUT) s->nis_spinadd = a;               /* 0x598ee0 */
    else if (what == N2_NISEV_TORQUE) { s->nis_tq[0] = a; s->nis_tq[1] = b; }   /* 0x5a4080 */
    else return 0;
    return 1;
}

void n2sim_nis_target(N2Sim *s, float x, float y, unsigned short heading, int count, float dt)
{                                                                        /* 0x5a40a0 */
    Body *b = &s->b;
    s->nis_count = count;
    float p[3] = { x, y, b->pos[2] };                        /* z: the car's (car +0x68) */
    float dx = x - b->pos[0], dy = y - b->pos[1];
    float hdeg = (float)heading * 0.0054931640625f;          /* 0x784248 */
    float err = nis_heading_err(hdeg, b->R);
    if (sqrtf(dx * dx + dy * dy) > 1.1f || fabsf(err) > 15.0f) {   /* 0x7a19c4 / 0x7a19c8 */
        /* 0x5fbd60: put down there, facing the heading */
        float gp[3] = { x, y, b->pos[2] }, gz, n[3]; int surf;
        if (s->ground && s->ground(s->user, x, y, b->pos[2] + 1.0f, &gz, n, &surf)) gp[2] = gz;
        float h = (float)heading * U16RAD;
        if (h > PI_F) h -= 2 * PI_F;
        /* the put-down moves the car; the chassis keeps what the event
           track set (the port's reset re-enters the mover) */
        float fs = s->nis_fspin, sa = s->nis_spinadd, tq0 = s->nis_tq[0], tq1 = s->nis_tq[1];
        n2sim_reset(s, gp, h);
        /* (port) the mover steers the body's position (the centre of mass)
           onto its target; the reset stands the car's origin there, the
           centre of mass ~0.27 m ahead. The next tick read that as a 0.27 m
           error over 1/60 s: -16 m/s, kept when the scene hands the cars
           over on the next tick -- a skipped pre-race scene sent the grid
           backwards (user report 11-16; sim: every car f -12..-17 m/s) */
        b->pos[0] = x; b->pos[1] = y;
        if (s->mover == N2_MOVER_NIS) {
            s->nis_fspin = fs; s->nis_spinadd = sa; s->nis_tq[0] = tq0; s->nis_tq[1] = tq1;
            s->nis_head = hdeg;
            memcpy(s->nis_tgt, p, sizeof p); memcpy(s->nis_prev, p, sizeof p);
            s->nis_count = count;
        }
        return;
    }
    int same = fabsf(s->nis_tgt[0] - p[0]) <= 0.001f && fabsf(s->nis_tgt[1] - p[1]) <= 0.001f &&
               fabsf(s->nis_tgt[2] - p[2]) <= 0.001f;            /* 0x43d190 */
    if (same && s->nis_head == hdeg) return;
    s->nis_moving = 1;
    s->nis_head = hdeg;
    if (count < 4) memcpy(s->nis_tgt, p, sizeof p);
    else for (int i = 0; i < 3; i++) s->nis_tgt[i] = s->nis_prev[i] * 0.25f + p[i] * 0.75f;   /* 0x7a19e0 */
    memcpy(s->nis_prev, p, sizeof p);
    /* the front wheels: toward the heading error x 25, at most 65 degrees a
       second from where they are (0x7a19cc / 0x7a19d0); rolling faster than
       3.2 m/s, the angle between the car and its velocity instead */
    float st = (float)s->m.steer * 0.0054931640625f;
    if (st >= 180.0f) st -= 360.0f;
    float lo = st - dt * 65.0f, hi = st + dt * 65.0f;
    float f = err * 25.0f;
    f = f < lo ? lo : f > hi ? hi : f;
    float vl = len3(b->v);
    if (vl > 3.2f) {
        float c = (b->R[0] * b->v[0] + b->R[1] * b->v[1] + b->R[2] * b->v[2]) / (len3(b->R) * vl);
        if (c > 1.0f) c = 1.0f; else if (c < -1.0f) c = -1.0f;
        unsigned short a = (unsigned short)(int)(acosf(c) * RAD2U16);   /* 0x4010e0 */
        f = (float)a * 0.0054931640625f;
        f = f < lo ? lo : f > hi ? hi : f;
    }
    if (f < -45.0f) f = -45.0f; else if (f > 45.0f) f = 45.0f;
    s->m.steer = (unsigned short)((int)(f * 65536.0f) / 360);
}

void n2sim_body(const N2Sim *s, N2Body *o)
{
    const Body *b = &s->b;
    memcpy(o->pos, b->pos, 12); memcpy(o->R, b->R, 64);
    memcpy(o->p, b->p, 12); memcpy(o->L, b->L, 12);
    memcpy(o->v, b->v, 12); memcpy(o->w, b->w, 12);
    o->mass = b->mass; o->invmass = b->invmass;
    o->Iinv = b->yawI_on ? b->Iinv2 : b->Iinv;
    memcpy(o->com, b->com, 12);
    o->wheels_down = s->cs_wheels;
}

static void body_refresh(Body *b)                 /* velocity part of 0x59c5f0 */
{
    for (int i = 0; i < 3; i++) b->v[i] = b->invmass * b->p[i];
    float body[3];
    vmulT(body, b->L, b->R);
    vmul(body, body, b->yawI_on ? b->Iinv2 : b->Iinv);
    vmul(b->w, body, b->R);
}

void n2sim_body_pose(N2Sim *s, const float pos[3], const float R[16], const float p[3])
{
    Body *b = &s->b;
    memcpy(b->pos, pos, 12); memcpy(b->R, R, 64); memcpy(b->p, p, 12);
    body_refresh(b);
}

void n2sim_body_impulse(N2Sim *s, const float J[3], const float r[3], float ang)
{
    Body *b = &s->b;
    float j[3] = { J[0], J[1], J[2] }, t[3], l = len3(j);
    if (l > 1000.0f) { l = 1000.0f / l; j[0] *= l; j[1] *= l; j[2] *= l; }
    b->p[0] += j[0]; b->p[1] += j[1]; b->p[2] += j[2];
    cross(t, r, j);
    l = len3(t);
    if (l > 10.0f) { l = 10.0f / l; t[0] *= l; t[1] *= l; t[2] *= l; }
    b->L[0] += t[0] * ang; b->L[1] += t[1] * ang; b->L[2] += t[2] * ang;
    body_refresh(b);
}

/* ---- smokeable props ------------------------------------------------------ */
struct N2Prop { Body b; };

N2Prop *n2prop_new(float mass, const float inertia[3], const float cg[3])
{
    N2Prop *p = (N2Prop *)calloc(1, sizeof *p);
    if (!p) return NULL;
    Body *b = &p->b;
    ident(b->R); ident(b->world); ident(b->Iinv); ident(b->Iinv2);
    b->mass = mass;
    b->invmass = mass > 0 ? 1.0f / mass : 0.0f;
    for (int k = 0; k < 3; k++)
        b->Iinv[k * 5] = b->Iinv2[k * 5] = inertia[k] > 0 ? 1.0f / inertia[k] : 0.0f;
    memcpy(b->com, cg, 12);
    b->gravity = 9.8f;                 /* rb +0x160 default */
    b->damp_on = 1;                    /* Smokeable::Move sets +0x150 every frame */
    return p;
}
void n2prop_free(N2Prop *p) { free(p); }

void n2prop_place(N2Prop *p, const float pos[3], const float R[16])
{
    Body *b = &p->b;
    memcpy(b->pos, pos, 12); memcpy(b->R, R, 64);
    b->R[3] = b->R[7] = b->R[11] = 0; b->R[12] = b->R[13] = b->R[14] = 0; b->R[15] = 1;
    memset(b->p, 0, sizeof b->p); memset(b->L, 0, sizeof b->L);
    memset(b->F, 0, 12); memset(b->T, 0, 12);
    b->settle = 0;
    body_post(b);
}
void n2prop_body(const N2Prop *p, N2Body *o)
{
    const Body *b = &p->b;
    memcpy(o->pos, b->pos, 12); memcpy(o->R, b->R, 64);
    memcpy(o->p, b->p, 12); memcpy(o->L, b->L, 12);
    memcpy(o->v, b->v, 12); memcpy(o->w, b->w, 12);
    o->mass = b->mass; o->invmass = b->invmass;
    o->Iinv = b->Iinv;
    memcpy(o->com, b->com, 12);
    o->wheels_down = 0;
}
void n2prop_body_pose(N2Prop *p, const float pos[3], const float R[16], const float P[3])
{
    Body *b = &p->b;
    memcpy(b->pos, pos, 12); memcpy(b->R, R, 64); memcpy(b->p, P, 12);
    body_refresh(b);
}
void n2prop_impulse(N2Prop *p, const float J[3], const float r[3])
{
    body_impulse(&p->b, J, r);                                 /* 0x59bc60 */
    body_refresh(&p->b);
}
void n2prop_substep(N2Prop *p, float h)
{
    Body *b = &p->b;
    float db[32];
    body_deriv(b, db);
    float *st = b->pos;
    for (int i = 0; i < 32; i++) st[i] += h * db[i];
    body_post(b);
    orthonormalize(b->R);                                     /* eOrthonormalizeMatrix */
}
void n2prop_damp(N2Prop *p, int state)
{
    Body *b = &p->b;
    if (state == 1) {
        for (int i = 0; i < 3; i++) { b->p[i] *= 0.95f; b->L[i] *= 0.95f; }
        b->gravity = 9.8f;
        body_refresh(b);
    } else if (state == 2) {
        memset(b->p, 0, sizeof b->p); memset(b->L, 0, sizeof b->L);
        b->gravity = 0;
        body_refresh(b);
    } else b->gravity = 9.8f;
}
