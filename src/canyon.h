/* canyon.h -- Carbon's canyon duel for the World Expansion's Palmont (port).
 *
 * NFSC.exe's rules (E:/re-tools/out/CANYON_PLAN.md): two legs on the same road. The chaser
 * scores; the leader does not. Leg 1 the player chases, leg 2 the player leads and the
 * opponent chases; the points stay from leg 1. Scoring (0x62bd50) runs on a fixed 0.1 s tick:
 * the chaser's points go up by speedf x the zone's multiplier x 1000 x 0.1 while the chaser
 * makes forward progress. The zone is the leader's lead along the road (m, the last 4 frames
 * averaged): ahead (the chaser passed) x3, < 16 m x2, < 30 x1.5, < 45 x1, < 80 x0.5, then 0.
 * The chaser 10 s in front -> the chaser wins; the chaser 10 s past the last zone -> the chaser
 * loses. Leg 2 ends at once when the opponent's points reach the player's. */
#ifndef OPENUG2_CANYON_H
#define OPENUG2_CANYON_H

enum { CANYON_RACING = 0, CANYON_CAUGHT = 2, CANYON_OVERBOARD = 3, CANYON_PASSED = 4, CANYON_LOST = 5 };
#define CANYON_NZONE 8

typedef struct {
    int leg;                    /* 1: the player chases, 2: the player leads */
    int state;                  /* CANYON_*: +0x2c (CanyonRaceState) */
    float score[2];             /* [0] the player, [1] the opponent (slot +0: the chaser's points) */
    float leg1[2];              /* the scores when leg 1 ended */
    int zone;                   /* the chaser's zone 0..7 (+0x28) */
    float zone_frac;            /* how far through it (+0x24, the meter) */
    float gap;                  /* the leader's lead, m, averaged */
    float tick;                 /* to the next 0.1 s tick (+0x20) */
    float ztime[CANYON_NZONE];  /* the chaser's time in each zone (slot +0x24..); 0 / 7 reset on leaving */
    float gs[4]; int gi, primed;/* the gap's last samples (0x6166a0) */
    float maxdist;              /* the chaser's furthest race distance (0x6200e0) */
    float last_pts;             /* the last tick's points, for the HUD */
    int overboard_who;          /* CANYON_OVERBOARD: 0 the player, 1 the opponent */
} Canyon;

/* the rules' data (gpcore canyon class; NFSC.exe tuning defaults 0x616750) */
float canyon_zone_dist(int z);  /* 0 ahead (0 m) .. 7 behind (120 m) */
float canyon_zone_mult(int z);
#define CANYON_PASS_TIME 10.0f
#define CANYON_LOSE_TIME 10.0f

void canyon_begin(Canyon *c, int leg);   /* leg 1 clears the points; leg 2 keeps them */
/* one frame of the race: lead_m = the leader's race distance less the chaser's (m), the
   chaser's race distance (m, for the forward test) and its speed (m/s). Returns the state. */
int canyon_update(Canyon *c, float dt, float lead_m, float chaser_dist, float chaser_speed);
void canyon_overboard(Canyon *c, int who);
int canyon_chaser(const Canyon *c);      /* 0 the player, 1 the opponent */

#endif
