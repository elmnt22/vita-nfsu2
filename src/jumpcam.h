/* UG2 Jump Cams (Player camera zones 6 / 7): the slow-motion shot of the
 * player's car over a jump or in a crash.
 *
 * Retail (SPEED2.EXE, decomps E:/re-tools/out/cam5p*, p2P_jc*):
 *   trigger (a) the jump: Car::FinishMovement 0x604740 every 8th world step
 *   trigger (b) the crash: Collision::CheckForWildCollision 0x59fb70 ->
 *               Player::RequestZone(6, 0, 1, 1) 0x6022e0
 *   Player::IsJumpCamAvailable 0x5f1ee0 (the controller's FuturePoints)
 *   Car::AverageAir 0x5ec3d0 (the ballistic flight ahead)
 *   World::SetTimeScale 0x5ea280 (0.4 for 8 s, ClearZone(1) at the end),
 *   World::GetTimestep 0x5ea360, Player::ClearZone 0x5ffe70
 *   ChangeCameraMover 0x5f1fd0 cases 0xf / 0x10 -> TrackCarCameraMover
 *   (ctor 0x44bea0, Update 0x44c350): a still eye beside the landing,
 *   looking at the car with a 20 degree lens.
 */
#ifndef JUMPCAM_H
#define JUMPCAM_H

#include "chasecam.h"
#include "wcoll.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AiRacer;
struct RacePath;

typedef struct {
    float pos[3];          /* car +0x60 (body origin, world) */
    float rot[9];          /* rows forward, left, up (+0x90) */
    float vel[3];          /* +0x70 */
    float speed;           /* +0x430, |v| */
    int   wheels_down;     /* +0x424 */
    int   dampen;          /* +0x38 DampenMode */
    float wheel_pos[4][3]; /* wheel centres, world */
    float wheel_r[4];      /* P +0x20 + 0x30 i */
    float aero, mass;      /* P +0x264 + P +0x268, P +0x110 (AverageAir's downforce) */
    float box_centre[3];   /* model box centre, car frame (the LOS start, model +0x110) */
    float race_frac;       /* car +0x8f8, the race fraction 0..1 (zone 7's lens) */
} JcCar;

typedef struct {
    int   option;          /* the Jump Cam option (player +0x24 = 0x83aa20, 127.5 on) */
    int   racing;          /* *0x890118 == 4 */
    int   drift;           /* 0x89e7d9 || 0x890104 */
    int   two_players;     /* 0x8900bc == 2 */
    int   wrong_way;       /* player +0x248 */
    const struct RacePath *rp;   /* the race route (0x88f2be valid), NULL in free roam */
} JcEnv;

/* drop any zone, time scale 1 (a new race / free roam / the race is over) */
void jumpcam_reset(void);
/* one world step (60 Hz) of FinishMovement's camera part, after the player's
   car moved. fp = the car's controller (its FuturePoints), may be NULL. */
void jumpcam_step(const JcCar *car, const JcEnv *env, const struct AiRacer *fp, WColl *w);
/* the player's car went into crash mode in a car-car hit (impulse j):
   CheckForWildCollision's jump cam request; 1 when zone 6 was entered
   (RequestZone succeeded: the caller then queues the collision stream and
   puts the car's full visible damage on, 0x5a0107) */
int  jumpcam_crash(const JcCar *car, const JcEnv *env, const struct AiRacer *fp, WColl *w, float j);
/* called as a zone ends (ClearZone): main.c repairs the player car's damage */
extern void (*jumpcam_on_clear)(void);
/* the zone: 0 none, 6 the jump / crash shot, 7 the second shot */
int  jumpcam_zone(void);
/* the zone's TrackCarCameraMover drives the view: 1 and out filled */
int  jumpcam_camera(const JcCar *car, float dt, CcOut *out);

#ifdef __cplusplus
}
#endif
#endif
