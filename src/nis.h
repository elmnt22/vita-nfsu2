/* nis.h -- the pre-race scene ("NIS", IntroNisNN), ported from SPEED2.EXE
 * v1.2: the scene choice (0x437160 -> 0x436840, category 0), the scene
 * instance (0x438f30: start 0x43b000 / 0x437910, tick 0x43abc0), its car
 * paths (ZPM_CarN animations on the ROOT skeleton, 0x43acf0 / 0x439240 /
 * 0x4309d0) and the scene camera (ICE mover type 0x12: key setup 0x451b70,
 * update 0x453f50).
 *
 * Data: GLOBAL/InGameCommon.bun chunk 0x80037050 (the scene tables),
 * GLOBAL/InGameRace.bun chunk 0x8003b201 (the scene cameras, the same in
 * InGameDrag/Drift/FreeRoam), NIS/Scene_<name>_BundleB.bun (the scene
 * header 0x37030 and the EAGL animation banks 0xe34010).
 *
 * v1.3: the scene's characters (items 0x37040, skeletons, codecs 11/20/21,
 * ground snapping, foot shadows); nisdraw.c draws them.
 *
 * v1.4: the event track (EvtTrk.txt) and the scripted engine sound.
 *
 * Not ported (see E:/re-tools/out/NIS.md): the flag girl's hand flares
 * (0x4335c0: particle emitters + a light), the car effects the event track
 * switches (nitrous flames, exhaust smoke, blow-off: the car particle
 * system 0x615cc0), hydraulics, world messages,
 * the spotlight animations (XO_Spotlight*, codec 17: no item uses them).
 * v1.5: the camera's letterbox and depth of field values (drawn by main.c /
 * render.c). Per-key colour treatment (FX tint/gain/bloom)
 * is not drawn in retail either: FX intensity is 0 on every scene key, so
 * the treatment call 0x5d75e0 never runs, and the camera update never reads
 * the FX_BLOOM spline. */
#ifndef OPENUG2_NIS_H
#define OPENUG2_NIS_H

#include "chasecam.h"
#include "airace.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0x5e72e0: collision ground height at p (p[2] the reference height);
   found = 0 when there is none. state: NIS_GROUND_STATE zeroed bytes the
   caller may keep its lookup state in (one block per character), or NULL */
#define NIS_GROUND_STATE 64
typedef float (*NisGroundFn)(void *user, void *state, const float p[3], int *found);

/* Reads the scene tables and the scene cameras (once). 1 on success. */
int nis_init(const char *dataroot);

/* 0x436840 category 0 (race intro): the scenes allowed for this race
   (per-event table 0x430070, filters 0x42fe00 / 0x42fcf0 / 0x42fd60,
   two players 0x42fda0), one picked at random (0x43c1c0). */
typedef struct {
    int racers;            /* cars in the race (racemgr +0x24) */
    int humans;
    unsigned hmask;        /* human start positions: bit (start - 1) */
    int event;             /* 0x89e7a0 */
    int dir;               /* 0x89e7a4: 0 forward */
    int track_type;        /* race record +0xf6 (0x5d3e40) */
    int player_start;      /* the player's start position (0x8900ac) */
    int hydraulics;        /* the player's car +0x581 (part 0xa1 >> 5) */
    int crowd;             /* g_CrowdEnable (0x870cd0, 0x5bf6d0) */
    int two_player;        /* 0x8900bc == 2 */
    int option_0d;         /* 0x863470 +0xd == 1: only IntroNis08 / 12 */
} NisPickCtx;
int nis_pick(const NisPickCtx *c, AiRng *rng, unsigned *hash, int *variant);
/* bStringHash (0x43db50) */
unsigned nis_hash(const char *s);

typedef struct Nis Nis;
/* NIS/Scene_<name>_BundleB.bun for a scene picked above */
Nis *nis_load(const char *dataroot, unsigned hash, int variant);
void nis_free(Nis *n);
const char *nis_name(const Nis *n);

/* 0x43b000: the scene stands on the start line marker (Forward/
   BackwardStartLine: xy and its 16-bit angle; 0x42fc60 / 0x431990), on the
   collision ground there; the cars' first places (count 0) come with it. */
typedef struct { int on; float x, y; unsigned short heading; int count; } NisCar;
void nis_start(Nis *n, const float marker[2], unsigned short marker_ang,
               NisGroundFn ground, void *gu, NisCar cars[8]);

/* 0x43abc0: one frame. cars[K-1] is the target of the car at start
   position K (0x4309d0 -> 0x5a40a0) while the scene drives the cars.
   Returns flags: */
#define NIS_TICK_HANDOFF  1   /* 0x437a00 this frame: cars back to their drivers */
#define NIS_TICK_CHASECAM 2   /* the views go to the chase camera (0x5fa820(1)) */
int nis_tick(Nis *n, float dt, NisCar cars[8]);

/* 0x43b460 (the skip button, 0x603bc0 message 6): to StartCountdown.
   Returns the nis_tick flags. */
int nis_skip(Nis *n, NisCar cars[8]);

float nis_time(const Nis *n);              /* inst +0x2c */
float nis_length(const Nis *n);            /* inst +0x34: LENGTH frames / 30 */
float nis_countdown_start(const Nis *n);   /* inst +0x30: "StartCountdown" */
float nis_countdown_interval(const Nis *n);/* racemgr +0xc (0x437910) */
int   nis_playing(const Nis *n);           /* inst +0x50 (0x432500) */

/* ---- the scene's characters (flag girl, crowd) ----------------------------
   Items 0x37040 of type 1 (0x433020 -> vt 0x785fb4: init 0x437be0, update
   0x436050), each a skinned solid on an EAGL skeleton (bundle 0xe34009)
   playing one animation (codecs 11/20/21) and standing on the ground. They
   live as long as the scene instance (the race manager stops the scene at
   POSTRACE, 0x43b640), so they stay at the start line during the race. */
int nis_nchars(const Nis *n);
typedef struct {
    unsigned solid;          /* item +0x50: the solid's name hash (0x134011 +0x18) */
    int nbones;
    const float *skin;       /* nbones x 4x4 row-vector: model -> posed model (BlendMatrices) */
    float W[16];             /* world matrix (row vectors, translation in [12..14]) */
    float pelvis[3];         /* bone 1 in the world (0x431280(1): the light position) */
} NisCharView;
int nis_char(const Nis *n, int i, NisCharView *out);
/* bone 1 in the world only (no skinning matrices): for culling first */
int nis_char_pelvis(const Nis *n, int i, float pelvis[3]);
/* 0x433220: the shadow quad between the feet (CHARACTERSHADOW), corners on
   the ground + 0.05; 0 when the character casts none */
int nis_char_shadow(const Nis *n, int i, float q[4][3]);
/* 0x4335c0: a flag girl's two hand flares (models FLAG_GIRL_MODEL_5/7/8/9_
   FLARES): where each burns and the way it points; 0 when she holds none */
int nis_char_flares(const Nis *n, int i, float pos[2][3], float dir[2][3]);
/* the scene's bundle (solids and textures of the characters) */
const char *nis_bundle_path(const Nis *n);

/* ---- the event track (NIS/Scene_<name>_EvtTrk.txt) ------------------------
   Read by 0x432c00 (line parser 0x432d00, names 0x430b20 / 0x430c90 /
   0x430d40 / 0x430d00; SEQ_CAR_NITRO / _BURNOUT / _TORQUE expand into record
   templates 0x7ea240.. through 0x430dc0 / 0x430ea0 / 0x431130), heap-sorted
   by time (0x5d40f0, compare 0x430db0), played by 0x4395f0 after the cars
   and the characters each scene tick: every record whose time (frames x
   0.0333333) is below the track clock fires (0x4392e0). The record is the
   game's 0x1c bytes: */
typedef struct {
    int type;      /* +0x00 NIS_EV_* */
    float time;    /* +0x04 frames at 30 */
    int car;       /* +0x08 ZPM_CarN: start position N (1..7), -1 none */
    int a;         /* +0x0c effect (CARFX_*), wheel, world message, pattern */
    int b;         /* +0x10 effect parameter, world message int */
    float f, g;    /* +0x14 +0x18 */
} NisEvent;
enum {
    NIS_EV_EFFECT = 1, NIS_EV_HYD_BOUNCE, NIS_EV_HYD_EXTEND, NIS_EV_BRAKE_LOCK,
    NIS_EV_BURNOUT_ADD, NIS_EV_TORQUE, NIS_EV_SEQ_NITRO, NIS_EV_SEQ_BURNOUT,
    NIS_EV_SEQ_TORQUE, NIS_EV_SCREEN_FLASH, NIS_EV_WORLD_ANIM, NIS_EV_WORLD_MSG,
    NIS_EV_AUDIO_PATTERN, NIS_EV_AUDIO_REV
};
/* the records fired by the last nis_tick / nis_skip, in track order */
int nis_fired(const Nis *n, const NisEvent **out);
int nis_nevents(const Nis *n);
/* 0x4392e0 type 1: the per-car effect state (0x827cd0.. 0x827e28, one
   {type, intensity, parameter} per CARFX_NITRO 10 / EXHAUST_SMOKE 11 /
   EXHAUST_BLOWOFF 12 / NOS_BLOWOFF 13), drawn each scene tick by the car
   effect renderer 0x61fb00 -> 0x615cc0 while the intensity is above 0 */
void nis_car_fx(const Nis *n, int car, int fx, float *intensity, int *param);

/* ---- the scripted engine sound (0x46fa30) ----------------------------------
   The car sound's message handler 0x459550 (vt 0x78baf0 +0x48) takes
   CAR_AUDIO_PATTERN (message 7: a rev pattern, 16 tables 0x7f0070..) and
   CAR_AUDIO_REV (message 8: free revving for a time). While a NIS has the
   engine (0x47c490: a scene loaded and the countdown not yet at GO, state 4)
   the engine sound plays these instead of the car: state 1 idles down (rpm
   -500 and throttle -15 a step), 2 revs, 3 plays a pattern (an entry of
   {u16 rpm, u8 throttle} each 4 steps, linear in between); rpm is held in
   1000..10000 and throttle in 0..100. Outside it the state is reset (1,
   1000 rpm, throttle 0). */
typedef struct {
    int msg_pat, pat; float pat_k;      /* receiver +0xc0 +0xc4 +0xc8 */
    int msg_rev; float rev_n, rev_k;    /* receiver +0xb4 +0xb8 +0xbc */
    int state, step, left, revs;        /* engine +0xdc +0xe0 +0xe4 +0xd4 */
    const unsigned char *p;             /* +0xe8 */
    float rpm, thr;                     /* +0xec +0xf0 */
} NisRev;
void nis_rev_init(NisRev *r);
void nis_rev_msg(NisRev *r, int msg, int a, int b);     /* 0x459550 */
/* one engine-sound update (0x47c490 -> 0x46fa30); nis_mode = a scene has
   the engine. Returns nis_mode; rpm / thr hold the engine sound's input */
int nis_rev_step(NisRev *r, int nis_mode);

/* 0x453f50: the scene camera for the view of the car at car_com/car_rot
   (rigid body centre of mass and rows forward/left/up). now = the game
   clock in seconds (0x86518c / 4000). Returns 0 when the scene has no
   camera now (time outside [0, StartCountdown]). near_clip, letterbox
   (camera +0xbc / +0xc8: bar height as a fraction of 480 lines, drawn by
   0x5cbdc0) and dof (camera +0xb4 / +0xb8: focus centre and range in
   metres, both 0 = no depth of field) may be NULL. */
int nis_camera(Nis *n, double now, const float car_com[3], const float car_rot[9],
               CcOut *out, float *near_clip, float *letterbox, float dof[2]);

/* ---- the EAGL decoding for others (the world animations, worldanim.c) ----
   The animation banks (0xe34010 top-level chunks) of a bundle; a channel is
   one animation of them by name hash (0x434850 -> 0x6e2d40: a type 15
   container of codec 11 / 20 / 21 parts; frames = its length, 0x6e2110),
   evaluated into a pose (12 floats a bone: scale3, x-scale, quat xyzw,
   translation, 1) of at most NIS_CHAN_POSE floats. The ROOT skeleton
   (GLOBALB 0xe34009 "ROOT": ROOT + ROOT0, both at rest = identity) has two
   bones; nis_root_bone1 is PoseSQTToGlobal's bone 1 (row vectors,
   translation in [12..14]). */
#define NIS_CHAN_POSE (12 * 64)
typedef struct NisBanks NisBanks;
typedef struct NisChan NisChan;
NisBanks *nis_banks_load(const unsigned char *bundle, long len);
int  nis_banks_count(const NisBanks *b);
void nis_banks_free(NisBanks *b);
NisChan *nis_chan_bind(const NisBanks *b, unsigned name_hash, float *frames);
void nis_chan_eval(NisChan *c, float frame, float *pose, int npose);
void nis_chan_free(NisChan *c);
void nis_root_rest(float pose[24]);
void nis_root_bone1(const float pose[24], float m[16]);

#ifdef __cplusplus
}
#endif
#endif
