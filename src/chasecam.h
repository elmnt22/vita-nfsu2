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
    int   nitrous;       /* nitrous burning (0x59fa30 / 0x5ecdb0): the shake only */
    /* HighliteMode 0x445ac0 (the "hold": eye lag /1.3, offset x1.3,
       direction frozen) = OutsidePOV && (hyd_active || (dampen &&
       wheels_down <= 2)) */
    int   hyd_mode;      /* RealDriver::CouldBeDoingHydraulics 0x410470: driver
                            +0x2fb (bounce / pressurize mode picked) */
    int   hyd_active;    /* Car::AreHydraulicsBouncing 0x5ed290 || ::Extended
                            0x5ed300: a bounce / extend value above 0 */
    int   dampen;        /* car +0x38 DampenMode (set by Collision::
                            CheckForWildCollision, n2sim_dampen) */
    int   drift;         /* a drift race (0x89e7d9 || 0x890104): no eye lag */
    int   burnout;       /* the smoke show (0x89e7e2 || 0x890100; online only) */
    int   drag;          /* a drag race (0x89e7d8): the shake at full scale */
    int   surface[4];    /* retail surface id under each wheel (car +0x394) */
    float bbmin[3], bbmax[3];   /* model box, car frame (model +0xe0/+0xf0) */
} CcCar;

/* Another car near the camera (the car list 0x890080): MinGapCars 0x44b040
   lifts over traffic only (driver type 3), CameraWouldBeInCar 0x5f14f0
   tests every car. vel feeds MinGapCars' fast-passing test. */
typedef struct { float pos[3], rot[9], bbmin[3], bbmax[3], vel[3]; int traffic; } CcObstacle;

typedef struct {
    float eye[3], fwd[3], up[3];   /* final camera, world */
    float look[3];                 /* eye + fwd */
    float V[16];                   /* GL view matrix (column-major) */
    float fovy;                    /* vertical FOV, radians, 4:3 retail horizontal kept */
    float hfov43;                  /* the retail horizontal FOV at 4:3, radians */
    float znear;                   /* near clip, camera +0xbc (0x453570: 0.5, POV id 3
                                      0.4); 0 from the movers that leave it */
    int hide_car;                  /* camera +0x280 = the followed car: RenderCars 0x6316d0
                                      skips it (ShouldCarBeRendered GC 80236c48: no body,
                                      wheels, shadow, flares). 0x453570: set for the inside
                                      POVs (id not 0/1/5) except the hood (id 4) without
                                      Look Back; RearViewMirrorCameraMover's ctor 0x44ba10
                                      sets it too. */
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
       back (with no player, or Player::UseBirdsEye false: see birdseye). */
    int lookback;
    /* Player::UseBirdsEye 0x5eb520 (player +0x2b == 0): Look Back then shows
       the bird's-eye offset 0x7ec400 instead of turning round. The Player
       ctor 0x5f1670 sets +0x2b = 1 and nothing clears it: 0 on PC. */
    int birdseye;
    int pov_active;                /* mover +0x4c: the POV in use (0 while the
                                      bumper / hood view falls back, 0x453570) */
    /* the eView camera the movers share (+0x40 eye, +0x60 target, +0xc4 fov,
       +0x1e0 / +0x200 / +0x264 their rates): what the next mover's ctor hands
       over from (0x444980 / 0x444a70 / 0x444910) */
    float cam_eye[3], cam_tgt[3], cam_fov, cam_veye[3], cam_vtgt[3], cam_vfov;
    int cam_valid;
    int handover;                  /* the next start takes over from the camera */
    /* port addition (user request, not in retail: its OrbitCarCameraMover is only
       the zone / debug camera): the right stick orbits the chase views */
    float orbit_yaw, orbit_pitch, orbit_idle;
    /* port addition (GitHub #30, LiveArea Settings "Right stick look",
       rstickcam.txt; 0 = the rate orbit above): the chase views turn to the
       stick's direction (up = the normal view, down = behind); released below
       120 deg from behind-the-car they ease back, at or past it they cut back.
       Bumper / hood: the stick's lower half is Look Back. While turned, a wall
       pull-in (the retail MinGapTopology pass) recovers smoothly. Idle (yaw 0)
       = the retail camera unchanged. */
    int orbit_mode;
    int orbit_held;                /* the stick out of its dead zone last frame */
    float orbit_tgt;               /* the stick's yaw while held (the release angle) */
    float orbit_dist;              /* the smoothed eye distance after the wall pass, < 0 none */
    int stick_lb;                  /* bumper / hood: the stick's lower half (Look Back) */
} ChaseCam;

/* Reads the camera entries from the car's 0x34600 record (0x890 bytes). */
int  chasecam_load(ChaseCam *c, const unsigned char *rec);
/* Menu setting 0 bumper, 1 hood, 2 near, 3 far, 4 high (drift). The new
 * mover takes over from the last camera (ctor 0x451660: hand-over 0x444980 /
 * 0x444a70 / 0x444910, snapped when the eye is more than 20 m off). */
void chasecam_set(ChaseCam *c, int setting, const CcCar *car);
/* Jump straight to the ideal (teleport / first frame). */
void chasecam_snap(ChaseCam *c);
/* The right stick (-1..1, 0 = released) orbits the chase views around the car;
 * released, the camera eases back behind it. Call every frame before update. */
void chasecam_orbit(ChaseCam *c, float sx, float sy, float dt);
/* Another mover drew this frame (the NIS camera): remember its camera for a
 * later hand-over. car_pos = the followed car (camera +0xb0 distance). */
void chasecam_observe(ChaseCam *c, const CcOut *o, const float car_pos[3], float dt);
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
void  retail_view_proj(float fov, int W, int H, float *fovy, float *aspect);
void  chasecam_out(float V[4][4], unsigned short fov_u16, CcOut *out);
/* RearViewMirrorCameraMover (PC ctor 0x44ba10 / Update 0x44ba80): the mirror's
   view from the car (hfov43 = FOV 20000, znear 0.75; far 192) */
void  chasecam_rvm(const CcCar *car, CcOut *out);
/* ResetCameraShakers 0x5729e0: stop the acceleration jolts (car +0x80 ->
   MaybeCameraShake, applied to the drive camera and the mirror) */
void  chasecam_shakers_reset(void);
/* the jump cam's state for the shakers (MaybeCameraShake 0x572e50 in zone 7:
   size x0.8, duration x1.2; ApplyCameraShake skipped below time scale 1) */
void  chasecam_set_world(int zone, float time_scale);
/* TopologyCoordinate::SegmentIntersectsPossiblyLong-style test: does a world
   face cross p -> q (the camera's own LOS test, 0x5d39f0 over 0x5d37f0) */
int   chasecam_seg_blocked(WColl *w, const float p[3], const float q[3]);
/* ShowcaseCameraMover (0x446400): the photo view of the car from an SCCarSetup's
   nine floats; gup = the ground's normal under the car */
void  chasecam_photo(ChaseCam *c, const CcCar *car, const float gup[3], const float p[9], WColl *w, CcOut *out);

#ifdef __cplusplus
}
#endif
#endif
