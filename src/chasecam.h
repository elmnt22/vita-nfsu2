/* chasecam.h -- the retail NFSU2 player camera (CubicCameraMover), ported
 * from SPEED2.EXE v1.2: ctor 0x451660, update 0x453570, ideal frame
 * 0x44de90, follow direction 0x445c20, cubic smoother 0x5749e0, shake
 * 0x452ed0/0x441340, world collision 0x444530/0x444190, car obstacles
 * 0x44b040/0x44ac10.
 *
 * Geometry comes from the car's own GLOBALB 0x34600 record (+0x720: six
 * camera entries {short view, u16 pitch, float x(fwd), z(up), y(left)},
 * then six more for game mode 3); the smoothing and acceleration lag come
 * from the per-view tables at 0x7ec610 evaluated at 0x787154 = 0.5; the FOV
 * per view from 0x7eb5b4. */
#ifndef OPENUG2_CHASECAM_H
#define OPENUG2_CHASECAM_H

#include "wcoll.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0x5749e0 cubic: re-planned each step from value/velocity to the target
 * over `dur` seconds and evaluated at dt/dur */
typedef struct {
    float val, vel, tgt, tvel;     /* +0x00 +0x04 +0x08 +0x0c */
    float a, b, c, d;              /* +0x10..+0x1c */
    float t, dur, tmax;            /* +0x20 +0x24 +0x28 */
    short state, flag;             /* +0x2c +0x2e */
} CcCubic;

typedef struct { short view; unsigned short pitch; float x, z, y; } CcInfo;

/* What the camera reads off the car object (car +0x40 embeds the physics
 * car state). */
typedef struct {
    float pos[3];        /* car origin, world (car +0x50) */
    float rot[9];        /* rows forward, left, up (car +0x90) */
    float vel[3];        /* m/s (car +0x70) */
    float acc[3];        /* m/s^2 (car +0x80) */
    float speed;         /* |v| (car +0x430) */
    float vtop;          /* top speed for the shake table, m/s */
    int   wheels_down;   /* car +0x424 */
    int   nitrous;       /* hold state (0x5ed290/0x5ed300 effect timers) */
    int   drag;          /* a drag race (0x89e7d8): the shake at full scale */
    int   surface[4];    /* retail surface id under each wheel (car +0x394) */
    float bbmin[3], bbmax[3];   /* model box, car frame (model +0xe0/+0xf0) */
} CcCar;

typedef struct { float pos[3], rot[9], bbmin[3], bbmax[3]; } CcObstacle;

typedef struct {
    float eye[3], fwd[3], up[3];   /* final camera, world */
    float look[3];                 /* eye + fwd */
    float V[16];                   /* GL view matrix (column-major) */
    float fovy;                    /* vertical FOV, radians, 4:3 retail horizontal kept */
    float hfov43;                  /* the retail horizontal FOV at 4:3, radians */
} CcOut;

typedef struct {
    int loaded, started;
    CcInfo info[2][6];             /* [game mode 3 ? 1 : 0][view] */
    int setting;                   /* menu setting 0..4 (0x83aa74) */
    int view;                      /* camera info id (0x502500) */
    float rec[44];                 /* 0x7ec610 table at 0.5 */
    CcCubic dir[3], eye[3], look[3], up[3], fov;
    float acc_ring[4][3], acc_sum[3], acc_avg[3];
    int acc_n, acc_i;
    double now;                    /* seconds (0x86518c / 4000) */
    double t_ground;               /* param +0x58 */
    float V[4][4];                 /* EA view, row vectors: p_cam = p * V */
    unsigned short fov_u16;        /* view +0xc4 */
    WCollCtx wctx;
    /* Look Back held (JOY event 0x7e: controller +0x249 -> mover vt[5] ->
       +0x51): 0x453570 turns the eye and look offsets' forward and left
       parts round (x -1), the camera then sits ahead of the car looking
       back. The offset table 0x7ec400 path needs mover +0x20, which only
       another mover class sets. */
    int lookback;
} ChaseCam;

/* Reads the camera entries from the car's 0x34600 record (0x890 bytes). */
int  chasecam_load(ChaseCam *c, const unsigned char *rec);
/* Menu setting 0 bumper, 1 hood, 2 near, 3 far, 4 high (drift). The new
 * mover starts at its ideal (the ctor's hand-over 0x444980 is not ported). */
void chasecam_set(ChaseCam *c, int setting, const CcCar *car);
/* Jump straight to the ideal (teleport / first frame). */
void chasecam_snap(ChaseCam *c);
void chasecam_update(ChaseCam *c, const CcCar *car, float dt,
                     WColl *w, const CcObstacle *obs, int nobs, CcOut *out);

/* Pieces other camera movers share (the NIS camera, nis.c):
   0x441270 noise; 0x5ba9a0 look-at with up = +z into an EA view (row
   vectors, camera x right / y down / z forward); 0x441340 shake applied to
   such a view from the four noise channels (x, y offset, pitch, yaw) at the
   view's FOV; and the view and FOV as a CcOut. */
float chasecam_noise(float x);
void  chasecam_lookat(const float eye[3], const float at[3], float V[4][4]);
void  chasecam_shake_view(float V[4][4], const float nz[4], unsigned short fov_u16);
void  chasecam_out(float V[4][4], unsigned short fov_u16, CcOut *out);

#ifdef __cplusplus
}
#endif
#endif
